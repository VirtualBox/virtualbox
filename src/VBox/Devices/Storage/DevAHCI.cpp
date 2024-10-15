/* $Id: DevAHCI.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevAHCI - AHCI controller device (disk and cdrom).
 *
 * Implements the AHCI standard 1.1
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

/** @page pg_dev_ahci   AHCI - Advanced Host Controller Interface Emulation.
 *
 * This component implements an AHCI serial ATA controller.  The device is split
 * into two parts.  The first part implements the register interface for the
 * guest and the second one does the data transfer.
 *
 * The guest can access the controller in two ways.  The first one is the native
 * way implementing the registers described in the AHCI specification and is
 * the preferred one.  The second implements the I/O ports used for booting from
 * the hard disk and for guests which don't have an AHCI SATA driver.
 *
 * The data is transfered using the extended media interface, asynchronously if
 * it is supported by the driver below otherwise it weill be done synchronous.
 * Either way a thread is used to process new requests from the guest.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_AHCI
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmstorageifs.h>
#include <VBox/vmm/pdmqueue.h>
#include <VBox/vmm/pdmthread.h>
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/sup.h>
#include <VBox/scsi.h>
#include <VBox/ata.h>
#include <VBox/AssertGuest.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/list.h>
#ifdef IN_RING3
# include <iprt/param.h>
# include <iprt/thread.h>
# include <iprt/semaphore.h>
# include <iprt/alloc.h>
# include <iprt/uuid.h>
# include <iprt/time.h>
#endif
#include "VBoxDD.h"

#if   defined(VBOX_WITH_DTRACE) \
   && defined(IN_RING3) \
   && !defined(VBOX_DEVICE_STRUCT_TESTCASE)
# include "dtrace/VBoxDD.h"
#else
# define VBOXDD_AHCI_REQ_SUBMIT(a,b,c,d)           do { } while (0)
# define VBOXDD_AHCI_REQ_COMPLETED(a,b,c,d)        do { } while (0)
#endif

/** Maximum number of ports available.
 * Spec defines 32 but we have one allocated for command completion coalescing
 * and another for a reserved future feature.
 */
#define AHCI_MAX_NR_PORTS_IMPL  30
/** Maximum number of command slots available. */
#define AHCI_NR_COMMAND_SLOTS   32

/** The current saved state version. */
#define AHCI_SAVED_STATE_VERSION                        9
/** The saved state version before the ATAPI emulation was removed and the generic SCSI driver was used. */
#define AHCI_SAVED_STATE_VERSION_PRE_ATAPI_REMOVE       8
/** The saved state version before changing the port reset logic in an incompatible way. */
#define AHCI_SAVED_STATE_VERSION_PRE_PORT_RESET_CHANGES 7
/** Saved state version before the per port hotplug port was added. */
#define AHCI_SAVED_STATE_VERSION_PRE_HOTPLUG_FLAG       6
/** Saved state version before legacy ATA emulation was dropped. */
#define AHCI_SAVED_STATE_VERSION_IDE_EMULATION          5
/** Saved state version before ATAPI support was added. */
#define AHCI_SAVED_STATE_VERSION_PRE_ATAPI              3
/** The saved state version use in VirtualBox 3.0 and earlier.
 * This was before the config was added and ahciIOTasks was dropped. */
#define AHCI_SAVED_STATE_VERSION_VBOX_30                2
/* for Older ATA state Read handling */
#define ATA_CTL_SAVED_STATE_VERSION 3
#define ATA_CTL_SAVED_STATE_VERSION_WITHOUT_FULL_SENSE 1
#define ATA_CTL_SAVED_STATE_VERSION_WITHOUT_EVENT_STATUS 2

/** The maximum number of release log entries per device. */
#define MAX_LOG_REL_ERRORS 1024

/**
 * Maximum number of sectors to transfer in a READ/WRITE MULTIPLE request.
 * Set to 1 to disable multi-sector read support. According to the ATA
 * specification this must be a power of 2 and it must fit in an 8 bit
 * value. Thus the only valid values are 1, 2, 4, 8, 16, 32, 64 and 128.
 */
#define ATA_MAX_MULT_SECTORS 128

/**
 * Fastest PIO mode supported by the drive.
 */
#define ATA_PIO_MODE_MAX 4
/**
 * Fastest MDMA mode supported by the drive.
 */
#define ATA_MDMA_MODE_MAX 2
/**
 * Fastest UDMA mode supported by the drive.
 */
#define ATA_UDMA_MODE_MAX 6

/**
 * Length of the configurable VPD data (without termination)
 */
#define AHCI_SERIAL_NUMBER_LENGTH            20
#define AHCI_FIRMWARE_REVISION_LENGTH         8
#define AHCI_MODEL_NUMBER_LENGTH             40
#define AHCI_ATAPI_INQUIRY_VENDOR_ID_LENGTH   8
#define AHCI_ATAPI_INQUIRY_PRODUCT_ID_LENGTH 16
#define AHCI_ATAPI_INQUIRY_REVISION_LENGTH    4

/** ATAPI sense info size. */
#define ATAPI_SENSE_SIZE 64

/**
 * Command Header.
 */
typedef struct
{
    /** Description Information. */
    uint32_t           u32DescInf;
    /** Command status. */
    uint32_t           u32PRDBC;
    /** Command Table Base Address. */
    uint32_t           u32CmdTblAddr;
    /** Command Table Base Address - upper 32-bits. */
    uint32_t           u32CmdTblAddrUp;
    /** Reserved */
    uint32_t           u32Reserved[4];
} CmdHdr;
AssertCompileSize(CmdHdr, 32);

/* Defines for the command header. */
#define AHCI_CMDHDR_PRDTL_MASK 0xffff0000
#define AHCI_CMDHDR_PRDTL_ENTRIES(x) ((x & AHCI_CMDHDR_PRDTL_MASK) >> 16)
#define AHCI_CMDHDR_C          RT_BIT(10)
#define AHCI_CMDHDR_B          RT_BIT(9)
#define AHCI_CMDHDR_R          RT_BIT(8)
#define AHCI_CMDHDR_P          RT_BIT(7)
#define AHCI_CMDHDR_W          RT_BIT(6)
#define AHCI_CMDHDR_A          RT_BIT(5)
#define AHCI_CMDHDR_CFL_MASK   0x1f

#define AHCI_CMDHDR_PRDT_OFFSET 0x80
#define AHCI_CMDHDR_ACMD_OFFSET 0x40

/* Defines for the command FIS. */
/* Defines that are used in the first double word. */
#define AHCI_CMDFIS_TYPE                  0 /* The first byte. */
# define AHCI_CMDFIS_TYPE_H2D             0x27 /* Register - Host to Device FIS. */
# define AHCI_CMDFIS_TYPE_H2D_SIZE        20   /* Five double words. */
# define AHCI_CMDFIS_TYPE_D2H             0x34 /* Register - Device to Host FIS. */
# define AHCI_CMDFIS_TYPE_D2H_SIZE        20   /* Five double words. */
# define AHCI_CMDFIS_TYPE_SETDEVBITS      0xa1 /* Set Device Bits - Device to Host FIS. */
# define AHCI_CMDFIS_TYPE_SETDEVBITS_SIZE 8    /* Two double words. */
# define AHCI_CMDFIS_TYPE_DMAACTD2H       0x39 /* DMA Activate - Device to Host FIS. */
# define AHCI_CMDFIS_TYPE_DMAACTD2H_SIZE  4    /* One double word. */
# define AHCI_CMDFIS_TYPE_DMASETUP        0x41 /* DMA Setup - Bidirectional FIS. */
# define AHCI_CMDFIS_TYPE_DMASETUP_SIZE   28   /* Seven double words. */
# define AHCI_CMDFIS_TYPE_PIOSETUP        0x5f /* PIO Setup - Device to Host FIS. */
# define AHCI_CMDFIS_TYPE_PIOSETUP_SIZE   20   /* Five double words. */
# define AHCI_CMDFIS_TYPE_DATA            0x46 /* Data - Bidirectional FIS. */

#define AHCI_CMDFIS_BITS                  1 /* Interrupt and Update bit. */
#define AHCI_CMDFIS_C                     RT_BIT(7) /* Host to device. */
#define AHCI_CMDFIS_I                     RT_BIT(6) /* Device to Host. */
#define AHCI_CMDFIS_D                     RT_BIT(5)

#define AHCI_CMDFIS_CMD                   2
#define AHCI_CMDFIS_FET                   3

#define AHCI_CMDFIS_SECTN                 4
#define AHCI_CMDFIS_CYLL                  5
#define AHCI_CMDFIS_CYLH                  6
#define AHCI_CMDFIS_HEAD                  7

#define AHCI_CMDFIS_SECTNEXP              8
#define AHCI_CMDFIS_CYLLEXP               9
#define AHCI_CMDFIS_CYLHEXP               10
#define AHCI_CMDFIS_FETEXP                11

#define AHCI_CMDFIS_SECTC                 12
#define AHCI_CMDFIS_SECTCEXP              13
#define AHCI_CMDFIS_CTL                   15
# define AHCI_CMDFIS_CTL_SRST             RT_BIT(2) /* Reset device. */
# define AHCI_CMDFIS_CTL_NIEN             RT_BIT(1) /* Assert or clear interrupt. */

/* For D2H FIS */
#define AHCI_CMDFIS_STS                   2
#define AHCI_CMDFIS_ERR                   3

/** Pointer to a task state. */
typedef struct AHCIREQ *PAHCIREQ;

/** Task encountered a buffer overflow. */
#define AHCI_REQ_OVERFLOW    RT_BIT_32(0)
/** Request is a PIO data command, if this flag is not set it either is
 * a command which does not transfer data or a DMA command based on the transfer size. */
#define AHCI_REQ_PIO_DATA    RT_BIT_32(1)
/** The request has the SACT register set. */
#define AHCI_REQ_CLEAR_SACT  RT_BIT_32(2)
/** Flag whether the request is queued. */
#define AHCI_REQ_IS_QUEUED   RT_BIT_32(3)
/** Flag whether the request is stored on the stack. */
#define AHCI_REQ_IS_ON_STACK RT_BIT_32(4)
/** Flag whether this request transfers data from the device to the HBA or
 * the other way around .*/
#define AHCI_REQ_XFER_2_HOST RT_BIT_32(5)

/**
 * A task state.
 */
typedef struct AHCIREQ
{
    /** The I/O request handle from the driver below associated with this request. */
    PDMMEDIAEXIOREQ            hIoReq;
    /** Tag of the task. */
    uint32_t                   uTag;
    /** The command Fis for this task. */
    uint8_t                    cmdFis[AHCI_CMDFIS_TYPE_H2D_SIZE];
    /** The ATAPI command data. */
    uint8_t                    aATAPICmd[ATAPI_PACKET_SIZE];
    /** Physical address of the command header. - GC */
    RTGCPHYS                   GCPhysCmdHdrAddr;
    /** Physical address of the PRDT */
    RTGCPHYS                   GCPhysPrdtl;
    /** Number of entries in the PRDTL. */
    unsigned                   cPrdtlEntries;
    /** Data direction. */
    PDMMEDIAEXIOREQTYPE        enmType;
    /** Start offset. */
    uint64_t                   uOffset;
    /** Number of bytes to transfer. */
    size_t                     cbTransfer;
    /** Flags for this task. */
    uint32_t                   fFlags;
    /** SCSI status code. */
    uint8_t                    u8ScsiSts;
    /** Flag when the buffer is mapped. */
    bool                       fMapped;
    /** Page lock when the buffer is mapped. */
    PGMPAGEMAPLOCK             PgLck;
} AHCIREQ;

/**
 * Notifier queue item.
 */
typedef struct DEVPORTNOTIFIERQUEUEITEM
{
    /** The core part owned by the queue manager. */
    PDMQUEUEITEMCORE    Core;
    /** The port to process. */
    uint8_t             iPort;
} DEVPORTNOTIFIERQUEUEITEM, *PDEVPORTNOTIFIERQUEUEITEM;


/**
 * The shared state of an AHCI port.
 */
typedef struct AHCIPORT
{
    /** Command List Base Address. */
    uint32_t                        regCLB;
    /** Command List Base Address upper bits. */
    uint32_t                        regCLBU;
    /** FIS Base Address. */
    uint32_t                        regFB;
    /** FIS Base Address upper bits. */
    uint32_t                        regFBU;
    /** Interrupt Status. */
    volatile uint32_t               regIS;
    /** Interrupt Enable. */
    uint32_t                        regIE;
    /** Command. */
    uint32_t                        regCMD;
    /** Task File Data. */
    uint32_t                        regTFD;
    /** Signature */
    uint32_t                        regSIG;
    /** Serial ATA Status. */
    uint32_t                        regSSTS;
    /** Serial ATA Control. */
    uint32_t                        regSCTL;
    /** Serial ATA Error. */
    uint32_t                        regSERR;
    /** Serial ATA Active. */
    volatile uint32_t               regSACT;
    /** Command Issue. */
    uint32_t                        regCI;

    /** Current number of active tasks. */
    volatile uint32_t               cTasksActive;
    uint32_t                        u32Alignment1;
    /** Command List Base Address */
    volatile RTGCPHYS               GCPhysAddrClb;
    /** FIS Base Address */
    volatile RTGCPHYS               GCPhysAddrFb;

    /** Device is powered on. */
    bool                            fPoweredOn;
    /** Device has spun up. */
    bool                            fSpunUp;
    /** First D2H FIS was sent. */
    bool                            fFirstD2HFisSent;
    /** Attached device is a CD/DVD drive. */
    bool                            fATAPI;
    /** Flag whether this port is in a reset state. */
    volatile bool                   fPortReset;
    /** Flag whether TRIM is supported. */
    bool                            fTrimEnabled;
    /** Flag if we are in a device reset. */
    bool                            fResetDevice;
    /** Flag whether this port is hot plug capable. */
    bool                            fHotpluggable;
    /** Flag whether the port is in redo task mode. */
    volatile bool                   fRedo;
    /** Flag whether the worker thread is sleeping. */
    volatile bool                   fWrkThreadSleeping;

    bool                            afAlignment1[2];

    /** Number of total sectors. */
    uint64_t                        cTotalSectors;
    /** Size of one sector. */
    uint32_t                        cbSector;
    /** Currently configured number of sectors in a multi-sector transfer. */
    uint32_t                        cMultSectors;
    /** The LUN (same as port number). */
    uint32_t                        iLUN;
    /** Set if there is a device present at the port. */
    bool                            fPresent;
    /** Currently active transfer mode (MDMA/UDMA) and speed. */
    uint8_t                         uATATransferMode;
    /** Exponent of logical sectors in a physical sector, number of logical sectors is 2^exp. */
    uint8_t                         cLogSectorsPerPhysicalExp;
    uint8_t                         bAlignment2;
    /** ATAPI sense data. */
    uint8_t                         abATAPISense[ATAPI_SENSE_SIZE];

    /** Bitmap for finished tasks (R3 -> Guest). */
    volatile uint32_t               u32TasksFinished;
    /** Bitmap for finished queued tasks (R3 -> Guest). */
    volatile uint32_t               u32QueuedTasksFinished;
    /** Bitmap for new queued tasks (Guest -> R3). */
    volatile uint32_t               u32TasksNew;
    /** Bitmap of tasks which must be redone because of a non fatal error. */
    volatile uint32_t               u32TasksRedo;

    /** Current command slot processed.
     * Accessed by the guest by reading the CMD register.
     * Holds the command slot of the command processed at the moment. */
    volatile uint32_t               u32CurrentCommandSlot;

    /** Physical geometry of this image. */
    PDMMEDIAGEOMETRY                PCHSGeometry;

    /** The status LED state for this drive. */
    PDMLED                          Led;

    /** The event semaphore the processing thread waits on. */
    SUPSEMEVENT                     hEvtProcess;

    /** The serial numnber to use for IDENTIFY DEVICE commands. */
    char                            szSerialNumber[AHCI_SERIAL_NUMBER_LENGTH+1]; /** < one extra byte for termination */
    /** The firmware revision to use for IDENTIFY DEVICE commands. */
    char                            szFirmwareRevision[AHCI_FIRMWARE_REVISION_LENGTH+1]; /** < one extra byte for termination */
    /** The model number to use for IDENTIFY DEVICE commands. */
    char                            szModelNumber[AHCI_MODEL_NUMBER_LENGTH+1]; /** < one extra byte for termination */
    /** The vendor identification string for SCSI INQUIRY commands. */
    char                            szInquiryVendorId[AHCI_ATAPI_INQUIRY_VENDOR_ID_LENGTH+1];
    /** The product identification string for SCSI INQUIRY commands. */
    char                            szInquiryProductId[AHCI_ATAPI_INQUIRY_PRODUCT_ID_LENGTH+1];
    /** The revision string for SCSI INQUIRY commands. */
    char                            szInquiryRevision[AHCI_ATAPI_INQUIRY_REVISION_LENGTH+1];
    /** Error counter */
    uint32_t                        cErrors;

    uint32_t                        u32Alignment5;
} AHCIPORT;
AssertCompileSizeAlignment(AHCIPORT, 8);
/** Pointer to the shared state of an AHCI port. */
typedef AHCIPORT *PAHCIPORT;


/**
 * The ring-3 state of an AHCI port.
 *
 * @implements PDMIBASE
 * @implements PDMIMEDIAPORT
 * @implements PDMIMEDIAEXPORT
 */
typedef struct AHCIPORTR3
{
    /** Pointer to the device instance - only to get our bearings in an interface
     *  method, nothing else. */
    PPDMDEVINSR3                    pDevIns;

    /** The LUN (same as port number). */
    uint32_t                        iLUN;

    /** Device specific settings (R3 only stuff). */
    /** Pointer to the attached driver's base interface. */
    R3PTRTYPE(PPDMIBASE)            pDrvBase;
    /** Pointer to the attached driver's block interface. */
    R3PTRTYPE(PPDMIMEDIA)           pDrvMedia;
    /** Pointer to the attached driver's extended interface. */
    R3PTRTYPE(PPDMIMEDIAEX)         pDrvMediaEx;
    /** Port description. */
    char                            szDesc[8];
    /** The base interface. */
    PDMIBASE                        IBase;
    /** The block port interface. */
    PDMIMEDIAPORT                   IPort;
    /** The extended media port interface. */
    PDMIMEDIAEXPORT                 IMediaExPort;

    /** Async IO Thread. */
    R3PTRTYPE(PPDMTHREAD)           pAsyncIOThread;
    /** First task throwing an error. */
    R3PTRTYPE(volatile PAHCIREQ)    pTaskErr;

} AHCIPORTR3;
AssertCompileSizeAlignment(AHCIPORTR3, 8);
/** Pointer to the ring-3 state of an AHCI port. */
typedef AHCIPORTR3 *PAHCIPORTR3;


/**
 * Main AHCI device state.
 *
 * @implements  PDMILEDPORTS
 */
typedef struct AHCI
{
    /** Global Host Control register of the HBA
     *  @todo r=bird: Make this a 'name' doxygen comment with { and add a
     * corrsponding at-} where appropriate. I cannot tell where to put the
     * latter. */

    /** HBA Capabilities - Readonly */
    uint32_t                        regHbaCap;
    /** HBA Control */
    uint32_t                        regHbaCtrl;
    /** Interrupt Status */
    uint32_t                        regHbaIs;
    /** Ports Implemented - Readonly */
    uint32_t                        regHbaPi;
    /** AHCI Version - Readonly */
    uint32_t                        regHbaVs;
    /** Command completion coalescing control */
    uint32_t                        regHbaCccCtl;
    /** Command completion coalescing ports */
    uint32_t                        regHbaCccPorts;

    /** Index register for BIOS access. */
    uint32_t                        regIdx;

    /** Countdown timer for command completion coalescing. */
    TMTIMERHANDLE                   hHbaCccTimer;

    /** Which port number is used to mark an CCC interrupt */
    uint8_t                         uCccPortNr;
    uint8_t                         abAlignment1[7];

    /** Timeout value */
    uint64_t                        uCccTimeout;
    /** Number of completions used to assert an interrupt */
    uint32_t                        uCccNr;
    /** Current number of completed commands */
    uint32_t                        uCccCurrentNr;

    /** Register structure per port */
    AHCIPORT                        aPorts[AHCI_MAX_NR_PORTS_IMPL];

    /** The critical section. */
    PDMCRITSECT                     lock;

    /** Bitmask of ports which asserted an interrupt. */
    volatile uint32_t               u32PortsInterrupted;
    /** Number of I/O threads currently active - used for async controller reset handling. */
    volatile uint32_t               cThreadsActive;

    /** Flag whether the legacy port reset method should be used to make it work with saved states. */
    bool                            fLegacyPortResetMethod;
    /** Enable tiger (10.4.x) SSTS hack or not. */
    bool                            fTigerHack;
    /** Flag whether we have written the first 4bytes in an 8byte MMIO write successfully. */
    volatile bool                   f8ByteMMIO4BytesWrittenSuccessfully;

    /** Device is in a reset state.
     * @todo r=bird: This isn't actually being modified by anyone...  */
    bool                            fReset;
    /** Supports 64bit addressing
     * @todo r=bird: This isn't really being modified by anyone (always false). */
    bool                            f64BitAddr;
    /** Flag whether the controller has BIOS access enabled.
     * @todo r=bird: Not used, just queried from CFGM.  */
    bool                            fBootable;

    bool                            afAlignment2[2];

    /** Number of usable ports on this controller. */
    uint32_t                        cPortsImpl;
    /** Number of usable command slots for each port. */
    uint32_t                        cCmdSlotsAvail;

    /** PCI region \#0: Legacy IDE fake, 8 ports. */
    IOMIOPORTHANDLE                 hIoPortsLegacyFake0;
    /** PCI region \#1: Legacy IDE fake, 1 port. */
    IOMIOPORTHANDLE                 hIoPortsLegacyFake1;
    /** PCI region \#2: Legacy IDE fake, 8 ports. */
    IOMIOPORTHANDLE                 hIoPortsLegacyFake2;
    /** PCI region \#3: Legacy IDE fake, 1 port. */
    IOMIOPORTHANDLE                 hIoPortsLegacyFake3;
    /** PCI region \#4: BMDMA I/O port range, 16 ports, used for the Index/Data
     *                  pair register access. */
    IOMIOPORTHANDLE                 hIoPortIdxData;
    /** PCI region \#5: MMIO registers. */
    IOMMMIOHANDLE                   hMmio;
} AHCI;
AssertCompileMemberAlignment(AHCI, aPorts, 8);
/** Pointer to the state of an AHCI device. */
typedef AHCI *PAHCI;


/**
 * Main AHCI device ring-3 state.
 *
 * @implements  PDMILEDPORTS
 */
typedef struct AHCIR3
{
    /** Pointer to the device instance - only for getting our bearings in
     *  interface methods. */
    PPDMDEVINSR3                    pDevIns;

    /** Status LUN: The base interface. */
    PDMIBASE                        IBase;
    /** Status LUN: Leds interface. */
    PDMILEDPORTS                    ILeds;
    /** Status LUN: Partner of ILeds. */
    R3PTRTYPE(PPDMILEDCONNECTORS)   pLedsConnector;
    /** Status LUN: Media Notifys. */
    R3PTRTYPE(PPDMIMEDIANOTIFY)     pMediaNotify;

    /** Register structure per port */
    AHCIPORTR3                      aPorts[AHCI_MAX_NR_PORTS_IMPL];

    /** Indicates that PDMDevHlpAsyncNotificationCompleted should be called when
     * a port is entering the idle state. */
    bool volatile                   fSignalIdle;
    bool                            afAlignment7[2+4];
} AHCIR3;
/** Pointer to the ring-3 state of an AHCI device. */
typedef AHCIR3 *PAHCIR3;


/**
 * Main AHCI device ring-0 state.
 */
typedef struct AHCIR0
{
    uint64_t                        uUnused;
} AHCIR0;
/** Pointer to the ring-0 state of an AHCI device. */
typedef AHCIR0 *PAHCIR0;


/**
 * Main AHCI device raw-mode state.
 */
typedef struct AHCIRC
{
    uint64_t                        uUnused;
} AHCIRC;
/** Pointer to the raw-mode state of an AHCI device. */
typedef AHCIRC *PAHCIRC;


/** Main AHCI device current context state. */
typedef CTX_SUFF(AHCI)  AHCICC;
/** Pointer to the current context state of an AHCI device. */
typedef CTX_SUFF(PAHCI) PAHCICC;


/**
 * Scatter gather list entry.
 */
typedef struct
{
    /** Data Base Address. */
    uint32_t           u32DBA;
    /** Data Base Address - Upper 32-bits. */
    uint32_t           u32DBAUp;
    /** Reserved */
    uint32_t           u32Reserved;
    /** Description information. */
    uint32_t           u32DescInf;
} SGLEntry;
AssertCompileSize(SGLEntry, 16);

#ifdef IN_RING3
/**
 * Memory buffer callback.
 *
 * @param   pDevIns  The device instance.
 * @param   GCPhys   The guest physical address of the memory buffer.
 * @param   pSgBuf   The pointer to the host R3 S/G buffer.
 * @param   cbCopy   How many bytes to copy between the two buffers.
 * @param   pcbSkip  Initially contains the amount of bytes to skip
 *                   starting from the guest physical address before
 *                   accessing the S/G buffer and start copying data.
 *                   On return this contains the remaining amount if
 *                   cbCopy < *pcbSkip or 0 otherwise.
 */
typedef DECLCALLBACKTYPE(void, FNAHCIR3MEMCOPYCALLBACK,(PPDMDEVINS pDevIns, RTGCPHYS GCPhys,
                                                        PRTSGBUF pSgBuf, size_t cbCopy, size_t *pcbSkip));
/** Pointer to a memory copy buffer callback. */
typedef FNAHCIR3MEMCOPYCALLBACK *PFNAHCIR3MEMCOPYCALLBACK;
#endif

/** Defines for a scatter gather list entry. */
#define SGLENTRY_DBA_READONLY     ~(RT_BIT(0))
#define SGLENTRY_DESCINF_I        RT_BIT(31)
#define SGLENTRY_DESCINF_DBC      0x3fffff
#define SGLENTRY_DESCINF_READONLY 0x803fffff

/* Defines for the global host control registers for the HBA. */

#define AHCI_HBA_GLOBAL_SIZE 0x100

/* Defines for the HBA Capabilities - Readonly */
#define AHCI_HBA_CAP_S64A RT_BIT(31)
#define AHCI_HBA_CAP_SNCQ RT_BIT(30)
#define AHCI_HBA_CAP_SIS  RT_BIT(28)
#define AHCI_HBA_CAP_SSS  RT_BIT(27)
#define AHCI_HBA_CAP_SALP RT_BIT(26)
#define AHCI_HBA_CAP_SAL  RT_BIT(25)
#define AHCI_HBA_CAP_SCLO RT_BIT(24)
#define AHCI_HBA_CAP_ISS  (RT_BIT(23) | RT_BIT(22) | RT_BIT(21) | RT_BIT(20))
# define AHCI_HBA_CAP_ISS_SHIFT(x) (((x) << 20) & AHCI_HBA_CAP_ISS)
# define AHCI_HBA_CAP_ISS_GEN1 RT_BIT(0)
# define AHCI_HBA_CAP_ISS_GEN2 RT_BIT(1)
#define AHCI_HBA_CAP_SNZO RT_BIT(19)
#define AHCI_HBA_CAP_SAM  RT_BIT(18)
#define AHCI_HBA_CAP_SPM  RT_BIT(17)
#define AHCI_HBA_CAP_PMD  RT_BIT(15)
#define AHCI_HBA_CAP_SSC  RT_BIT(14)
#define AHCI_HBA_CAP_PSC  RT_BIT(13)
#define AHCI_HBA_CAP_NCS  (RT_BIT(12) | RT_BIT(11) | RT_BIT(10) | RT_BIT(9) | RT_BIT(8))
#define AHCI_HBA_CAP_NCS_SET(x) (((x-1) << 8) & AHCI_HBA_CAP_NCS) /* 0's based */
#define AHCI_HBA_CAP_CCCS  RT_BIT(7)
#define AHCI_HBA_CAP_NP   (RT_BIT(4) | RT_BIT(3) | RT_BIT(2) | RT_BIT(1) | RT_BIT(0))
#define AHCI_HBA_CAP_NP_SET(x) ((x-1) & AHCI_HBA_CAP_NP) /* 0's based */

/* Defines for the HBA Control register - Read/Write */
#define AHCI_HBA_CTRL_AE  RT_BIT(31)
#define AHCI_HBA_CTRL_IE  RT_BIT(1)
#define AHCI_HBA_CTRL_HR  RT_BIT(0)
#define AHCI_HBA_CTRL_RW_MASK (RT_BIT(0) | RT_BIT(1)) /* Mask for the used bits */

/* Defines for the HBA Version register - Readonly (We support AHCI 1.0) */
#define AHCI_HBA_VS_MJR   (1 << 16)
#define AHCI_HBA_VS_MNR   0x100

/* Defines for the command completion coalescing control register */
#define AHCI_HBA_CCC_CTL_TV 0xffff0000
#define AHCI_HBA_CCC_CTL_TV_SET(x) (x << 16)
#define AHCI_HBA_CCC_CTL_TV_GET(x) ((x & AHCI_HBA_CCC_CTL_TV) >> 16)

#define AHCI_HBA_CCC_CTL_CC 0xff00
#define AHCI_HBA_CCC_CTL_CC_SET(x) (x << 8)
#define AHCI_HBA_CCC_CTL_CC_GET(x) ((x & AHCI_HBA_CCC_CTL_CC) >> 8)

#define AHCI_HBA_CCC_CTL_INT 0xf8
#define AHCI_HBA_CCC_CTL_INT_SET(x) (x << 3)
#define AHCI_HBA_CCC_CTL_INT_GET(x) ((x & AHCI_HBA_CCC_CTL_INT) >> 3)

#define AHCI_HBA_CCC_CTL_EN  RT_BIT(0)

/* Defines for the port registers. */

#define AHCI_PORT_REGISTER_SIZE 0x80

#define AHCI_PORT_CLB_RESERVED 0xfffffc00 /* For masking out the reserved bits. */

#define AHCI_PORT_FB_RESERVED  0xffffff00 /* For masking out the reserved bits. */

#define AHCI_PORT_IS_CPDS      RT_BIT(31)
#define AHCI_PORT_IS_TFES      RT_BIT(30)
#define AHCI_PORT_IS_HBFS      RT_BIT(29)
#define AHCI_PORT_IS_HBDS      RT_BIT(28)
#define AHCI_PORT_IS_IFS       RT_BIT(27)
#define AHCI_PORT_IS_INFS      RT_BIT(26)
#define AHCI_PORT_IS_OFS       RT_BIT(24)
#define AHCI_PORT_IS_IPMS      RT_BIT(23)
#define AHCI_PORT_IS_PRCS      RT_BIT(22)
#define AHCI_PORT_IS_DIS       RT_BIT(7)
#define AHCI_PORT_IS_PCS       RT_BIT(6)
#define AHCI_PORT_IS_DPS       RT_BIT(5)
#define AHCI_PORT_IS_UFS       RT_BIT(4)
#define AHCI_PORT_IS_SDBS      RT_BIT(3)
#define AHCI_PORT_IS_DSS       RT_BIT(2)
#define AHCI_PORT_IS_PSS       RT_BIT(1)
#define AHCI_PORT_IS_DHRS      RT_BIT(0)
#define AHCI_PORT_IS_READONLY  0xfd8000af /* Readonly mask including reserved bits. */

#define AHCI_PORT_IE_CPDE      RT_BIT(31)
#define AHCI_PORT_IE_TFEE      RT_BIT(30)
#define AHCI_PORT_IE_HBFE      RT_BIT(29)
#define AHCI_PORT_IE_HBDE      RT_BIT(28)
#define AHCI_PORT_IE_IFE       RT_BIT(27)
#define AHCI_PORT_IE_INFE      RT_BIT(26)
#define AHCI_PORT_IE_OFE       RT_BIT(24)
#define AHCI_PORT_IE_IPME      RT_BIT(23)
#define AHCI_PORT_IE_PRCE      RT_BIT(22)
#define AHCI_PORT_IE_DIE       RT_BIT(7)  /* Not supported for now, readonly. */
#define AHCI_PORT_IE_PCE       RT_BIT(6)
#define AHCI_PORT_IE_DPE       RT_BIT(5)
#define AHCI_PORT_IE_UFE       RT_BIT(4)
#define AHCI_PORT_IE_SDBE      RT_BIT(3)
#define AHCI_PORT_IE_DSE       RT_BIT(2)
#define AHCI_PORT_IE_PSE       RT_BIT(1)
#define AHCI_PORT_IE_DHRE      RT_BIT(0)
#define AHCI_PORT_IE_READONLY  (0xfdc000ff) /* Readonly mask including reserved bits. */

#define AHCI_PORT_CMD_ICC      (RT_BIT(28) | RT_BIT(29) | RT_BIT(30) | RT_BIT(31))
#define AHCI_PORT_CMD_ICC_SHIFT(x) ((x) << 28)
# define AHCI_PORT_CMD_ICC_IDLE    0x0
# define AHCI_PORT_CMD_ICC_ACTIVE  0x1
# define AHCI_PORT_CMD_ICC_PARTIAL 0x2
# define AHCI_PORT_CMD_ICC_SLUMBER 0x6
#define AHCI_PORT_CMD_ASP      RT_BIT(27) /* Not supported - Readonly */
#define AHCI_PORT_CMD_ALPE     RT_BIT(26) /* Not supported - Readonly */
#define AHCI_PORT_CMD_DLAE     RT_BIT(25)
#define AHCI_PORT_CMD_ATAPI    RT_BIT(24)
#define AHCI_PORT_CMD_CPD      RT_BIT(20)
#define AHCI_PORT_CMD_ISP      RT_BIT(19) /* Readonly */
#define AHCI_PORT_CMD_HPCP     RT_BIT(18)
#define AHCI_PORT_CMD_PMA      RT_BIT(17) /* Not supported - Readonly */
#define AHCI_PORT_CMD_CPS      RT_BIT(16)
#define AHCI_PORT_CMD_CR       RT_BIT(15) /* Readonly */
#define AHCI_PORT_CMD_FR       RT_BIT(14) /* Readonly */
#define AHCI_PORT_CMD_ISS      RT_BIT(13) /* Readonly */
#define AHCI_PORT_CMD_CCS      (RT_BIT(8) | RT_BIT(9) | RT_BIT(10) | RT_BIT(11) | RT_BIT(12))
#define AHCI_PORT_CMD_CCS_SHIFT(x) (x << 8) /* Readonly */
#define AHCI_PORT_CMD_FRE      RT_BIT(4)
#define AHCI_PORT_CMD_CLO      RT_BIT(3)
#define AHCI_PORT_CMD_POD      RT_BIT(2)
#define AHCI_PORT_CMD_SUD      RT_BIT(1)
#define AHCI_PORT_CMD_ST       RT_BIT(0)
#define AHCI_PORT_CMD_READONLY (0xff02001f & ~(AHCI_PORT_CMD_ASP | AHCI_PORT_CMD_ALPE | AHCI_PORT_CMD_PMA))

#define AHCI_PORT_SCTL_IPM         (RT_BIT(11) | RT_BIT(10) | RT_BIT(9) | RT_BIT(8))
#define AHCI_PORT_SCTL_IPM_GET(x)  ((x & AHCI_PORT_SCTL_IPM) >> 8)
#define AHCI_PORT_SCTL_SPD         (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4))
#define AHCI_PORT_SCTL_SPD_GET(x)  ((x & AHCI_PORT_SCTL_SPD) >> 4)
#define AHCI_PORT_SCTL_DET         (RT_BIT(3) | RT_BIT(2) | RT_BIT(1) | RT_BIT(0))
#define AHCI_PORT_SCTL_DET_GET(x)  (x & AHCI_PORT_SCTL_DET)
#define AHCI_PORT_SCTL_DET_NINIT   0
#define AHCI_PORT_SCTL_DET_INIT    1
#define AHCI_PORT_SCTL_DET_OFFLINE 4
#define AHCI_PORT_SCTL_READONLY    0xfff

#define AHCI_PORT_SSTS_IPM         (RT_BIT(11) | RT_BIT(10) | RT_BIT(9) | RT_BIT(8))
#define AHCI_PORT_SSTS_IPM_GET(x)  ((x & AHCI_PORT_SCTL_IPM) >> 8)
#define AHCI_PORT_SSTS_SPD         (RT_BIT(7) | RT_BIT(6) | RT_BIT(5) | RT_BIT(4))
#define AHCI_PORT_SSTS_SPD_GET(x)  ((x & AHCI_PORT_SCTL_SPD) >> 4)
#define AHCI_PORT_SSTS_DET         (RT_BIT(3) | RT_BIT(2) | RT_BIT(1) | RT_BIT(0))
#define AHCI_PORT_SSTS_DET_GET(x)  (x & AHCI_PORT_SCTL_DET)

#define AHCI_PORT_TFD_BSY          RT_BIT(7)
#define AHCI_PORT_TFD_DRQ          RT_BIT(3)
#define AHCI_PORT_TFD_ERR          RT_BIT(0)

#define AHCI_PORT_SERR_X           RT_BIT(26)
#define AHCI_PORT_SERR_W           RT_BIT(18)
#define AHCI_PORT_SERR_N           RT_BIT(16)

/* Signatures for attached storage devices. */
#define AHCI_PORT_SIG_DISK         0x00000101
#define AHCI_PORT_SIG_ATAPI        0xeb140101

/*
 * The AHCI spec defines an area of memory where the HBA posts received FIS's from the device.
 * regFB points to the base of this area.
 * Every FIS type has an offset where it is posted in this area.
 */
#define AHCI_RECFIS_DSFIS_OFFSET  0x00 /* DMA Setup FIS */
#define AHCI_RECFIS_PSFIS_OFFSET  0x20 /* PIO Setup FIS */
#define AHCI_RECFIS_RFIS_OFFSET   0x40 /* D2H Register FIS */
#define AHCI_RECFIS_SDBFIS_OFFSET 0x58 /* Set Device Bits FIS */
#define AHCI_RECFIS_UFIS_OFFSET   0x60 /* Unknown FIS type */

/** Mask to get the LBA value from a LBA range. */
#define AHCI_RANGE_LBA_MASK    UINT64_C(0xffffffffffff)
/** Mas to get the length value from a LBA range. */
#define AHCI_RANGE_LENGTH_MASK UINT64_C(0xffff000000000000)
/** Returns the length of the range in sectors. */
#define AHCI_RANGE_LENGTH_GET(val) (((val) & AHCI_RANGE_LENGTH_MASK) >> 48)

/**
 * AHCI register operator.
 */
typedef struct ahci_opreg
{
    const char *pszName;
    VBOXSTRICTRC (*pfnRead )(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t iReg, uint32_t *pu32Value);
    VBOXSTRICTRC (*pfnWrite)(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t iReg, uint32_t u32Value);
} AHCIOPREG;

/**
 * AHCI port register operator.
 */
typedef struct pAhciPort_opreg
{
    const char *pszName;
    VBOXSTRICTRC (*pfnRead )(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value);
    VBOXSTRICTRC (*pfnWrite)(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t u32Value);
} AHCIPORTOPREG;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifndef VBOX_DEVICE_STRUCT_TESTCASE
RT_C_DECLS_BEGIN
#ifdef IN_RING3
static void ahciR3HBAReset(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIR3 pThisCC);
static int ahciPostFisIntoMemory(PPDMDEVINS pDevIns, PAHCIPORT pAhciPort, unsigned uFisType, uint8_t *pCmdFis);
static void ahciPostFirstD2HFisIntoMemory(PPDMDEVINS pDevIns, PAHCIPORT pAhciPort);
static size_t ahciR3CopyBufferToPrdtl(PPDMDEVINS pDevIns, PAHCIREQ pAhciReq, const void *pvSrc, size_t cbSrc, size_t cbSkip);
static bool ahciR3CancelActiveTasks(PAHCIPORTR3 pAhciPortR3);
#endif
RT_C_DECLS_END


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define AHCI_RTGCPHYS_FROM_U32(Hi, Lo)             ( (RTGCPHYS)RT_MAKE_U64(Lo, Hi) )

#ifdef IN_RING3

# ifdef LOG_USE_C99
#  define ahciLog(a) \
     Log(("R3 P%u: %M", pAhciPort->iLUN, _LogRelRemoveParentheseis a))
# else
#  define ahciLog(a) \
     do { Log(("R3 P%u: ", pAhciPort->iLUN)); Log(a); } while(0)
# endif

#elif defined(IN_RING0)

# ifdef LOG_USE_C99
#  define ahciLog(a) \
     Log(("R0 P%u: %M", pAhciPort->iLUN, _LogRelRemoveParentheseis a))
# else
#  define ahciLog(a) \
     do { Log(("R0 P%u: ", pAhciPort->iLUN)); Log(a); } while(0)
# endif

#elif defined(IN_RC)

# ifdef LOG_USE_C99
#  define ahciLog(a) \
     Log(("GC P%u: %M", pAhciPort->iLUN, _LogRelRemoveParentheseis a))
# else
#  define ahciLog(a) \
     do { Log(("GC P%u: ", pAhciPort->iLUN)); Log(a); } while(0)
# endif

#endif



/**
 * Update PCI IRQ levels
 */
static void ahciHbaClearInterrupt(PPDMDEVINS pDevIns)
{
    Log(("%s: Clearing interrupt\n", __FUNCTION__));
    PDMDevHlpPCISetIrq(pDevIns, 0, 0);
}

/**
 * Updates the IRQ level and sets port bit in the global interrupt status register of the HBA.
 */
static int ahciHbaSetInterrupt(PPDMDEVINS pDevIns, PAHCI pThis, uint8_t iPort, int rcBusy)
{
    Log(("P%u: %s: Setting interrupt\n", iPort, __FUNCTION__));

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->lock, rcBusy);
    if (rc != VINF_SUCCESS)
        return rc;

    if (pThis->regHbaCtrl & AHCI_HBA_CTRL_IE)
    {
        if ((pThis->regHbaCccCtl & AHCI_HBA_CCC_CTL_EN) && (pThis->regHbaCccPorts & (1 << iPort)))
        {
            pThis->uCccCurrentNr++;
            if (pThis->uCccCurrentNr >= pThis->uCccNr)
            {
                /* Reset command completion coalescing state. */
                PDMDevHlpTimerSetMillies(pDevIns, pThis->hHbaCccTimer, pThis->uCccTimeout);
                pThis->uCccCurrentNr = 0;

                pThis->u32PortsInterrupted |= (1 << pThis->uCccPortNr);
                if (!(pThis->u32PortsInterrupted & ~(1 << pThis->uCccPortNr)))
                {
                    Log(("P%u: %s: Fire interrupt\n", iPort, __FUNCTION__));
                    PDMDevHlpPCISetIrq(pDevIns, 0, 1);
                }
            }
        }
        else
        {
            /* If only the bit of the actual port is set assert an interrupt
             * because the interrupt status register was already read by the guest
             * and we need to send a new notification.
             * Otherwise an interrupt is still pending.
             */
            ASMAtomicOrU32((volatile uint32_t *)&pThis->u32PortsInterrupted, (1 << iPort));
            if (!(pThis->u32PortsInterrupted & ~(1 << iPort)))
            {
                Log(("P%u: %s: Fire interrupt\n", iPort, __FUNCTION__));
                PDMDevHlpPCISetIrq(pDevIns, 0, 1);
            }
        }
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->lock);
    return VINF_SUCCESS;
}

#ifdef IN_RING3

/**
 * @callback_method_impl{FNTMTIMERDEV, Assert irq when an CCC timeout occurs.}
 */
static DECLCALLBACK(void) ahciCccTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    RT_NOREF(pDevIns, hTimer);
    PAHCI pThis = (PAHCI)pvUser;

    int rc = ahciHbaSetInterrupt(pDevIns, pThis, pThis->uCccPortNr, VERR_IGNORED);
    AssertRC(rc);
}

/**
 * Finishes the port reset of the given port.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AHCI state.
 * @param   pAhciPort   The port to finish the reset on, shared bits.
 * @param   pAhciPortR3 The port to finish the reset on, ring-3 bits.
 */
static void ahciPortResetFinish(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, PAHCIPORTR3 pAhciPortR3)
{
    ahciLog(("%s: Initiated.\n", __FUNCTION__));

    /* Cancel all tasks first. */
    bool fAllTasksCanceled = ahciR3CancelActiveTasks(pAhciPortR3);
    Assert(fAllTasksCanceled); NOREF(fAllTasksCanceled);

    /* Signature for SATA device. */
    if (pAhciPort->fATAPI)
        pAhciPort->regSIG = AHCI_PORT_SIG_ATAPI;
    else
        pAhciPort->regSIG = AHCI_PORT_SIG_DISK;

    /* We received a COMINIT from the device. Tell the guest. */
    ASMAtomicOrU32(&pAhciPort->regIS, AHCI_PORT_IS_PCS);
    pAhciPort->regSERR |= AHCI_PORT_SERR_X;
    pAhciPort->regTFD  |= ATA_STAT_BUSY;

    if ((pAhciPort->regCMD & AHCI_PORT_CMD_FRE) && (!pAhciPort->fFirstD2HFisSent))
    {
        ahciPostFirstD2HFisIntoMemory(pDevIns, pAhciPort);
        ASMAtomicOrU32(&pAhciPort->regIS, AHCI_PORT_IS_DHRS);

        if (pAhciPort->regIE & AHCI_PORT_IE_DHRE)
        {
            int rc = ahciHbaSetInterrupt(pDevIns, pThis, pAhciPort->iLUN, VERR_IGNORED);
            AssertRC(rc);
        }
    }

    pAhciPort->regSSTS = (0x01 << 8)    /* Interface is active. */
                       | (0x03 << 0);   /* Device detected and communication established. */

    /*
     * Use the maximum allowed speed.
     * (Not that it changes anything really)
     */
    switch (AHCI_PORT_SCTL_SPD_GET(pAhciPort->regSCTL))
    {
        case 0x01:
            pAhciPort->regSSTS |= (0x01 << 4); /* Generation 1 (1.5GBps) speed. */
            break;
        case 0x02:
        case 0x00:
        default:
            pAhciPort->regSSTS |= (0x02 << 4); /* Generation 2 (3.0GBps) speed. */
            break;
    }

    ASMAtomicXchgBool(&pAhciPort->fPortReset, false);
}

#endif /* IN_RING3 */

/**
 * Kicks the I/O thread from RC or R0.
 *
 * @param   pDevIns     The device instance.
 * @param   pAhciPort   The port to kick, shared bits.
 */
static void ahciIoThreadKick(PPDMDEVINS pDevIns, PAHCIPORT pAhciPort)
{
    LogFlowFunc(("Signal event semaphore\n"));
    int rc = PDMDevHlpSUPSemEventSignal(pDevIns, pAhciPort->hEvtProcess);
    AssertRC(rc);
}

static VBOXSTRICTRC PortCmdIssue_w(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t u32Value)
{
    ahciLog(("%s: write u32Value=%#010x\n", __FUNCTION__, u32Value));
    RT_NOREF(pThis, iReg);

    /* Update the CI register first. */
    uint32_t uCIValue = ASMAtomicXchgU32(&pAhciPort->u32TasksFinished, 0);
    pAhciPort->regCI &= ~uCIValue;

    if (   (pAhciPort->regCMD & AHCI_PORT_CMD_CR)
        && u32Value > 0)
    {
        /*
         * Clear all tasks which are already marked as busy. The guest
         * shouldn't write already busy tasks actually.
         */
        u32Value &= ~pAhciPort->regCI;

        ASMAtomicOrU32(&pAhciPort->u32TasksNew, u32Value);

        /* Send a notification to R3 if u32TasksNew was 0 before our write. */
        if (ASMAtomicReadBool(&pAhciPort->fWrkThreadSleeping))
            ahciIoThreadKick(pDevIns, pAhciPort);
        else
            ahciLog(("%s: Worker thread busy, no need to kick.\n", __FUNCTION__));
    }
    else
        ahciLog(("%s: Nothing to do (CMD=%08x).\n", __FUNCTION__, pAhciPort->regCMD));

    pAhciPort->regCI |= u32Value;

    return VINF_SUCCESS;
}

static VBOXSTRICTRC PortCmdIssue_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);

    uint32_t uCIValue = ASMAtomicXchgU32(&pAhciPort->u32TasksFinished, 0);
    ahciLog(("%s: read regCI=%#010x uCIValue=%#010x\n", __FUNCTION__, pAhciPort->regCI, uCIValue));

    pAhciPort->regCI &= ~uCIValue;
    *pu32Value = pAhciPort->regCI;

    return VINF_SUCCESS;
}

static VBOXSTRICTRC PortSActive_w(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t u32Value)
{
    ahciLog(("%s: write u32Value=%#010x\n", __FUNCTION__, u32Value));
    RT_NOREF(pDevIns, pThis, iReg);

    pAhciPort->regSACT |= u32Value;

    return VINF_SUCCESS;
}

static VBOXSTRICTRC PortSActive_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);

    uint32_t u32TasksFinished = ASMAtomicXchgU32(&pAhciPort->u32QueuedTasksFinished, 0);
    pAhciPort->regSACT &= ~u32TasksFinished;

    ahciLog(("%s: read regSACT=%#010x regCI=%#010x u32TasksFinished=%#010x\n",
             __FUNCTION__, pAhciPort->regSACT, pAhciPort->regCI, u32TasksFinished));

    *pu32Value = pAhciPort->regSACT;

    return VINF_SUCCESS;
}

static VBOXSTRICTRC PortSError_w(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: write u32Value=%#010x\n", __FUNCTION__, u32Value));

    if (   (u32Value & AHCI_PORT_SERR_X)
        && (pAhciPort->regSERR & AHCI_PORT_SERR_X))
    {
        ASMAtomicAndU32(&pAhciPort->regIS, ~AHCI_PORT_IS_PCS);
        pAhciPort->regTFD |= ATA_STAT_ERR;
        pAhciPort->regTFD &= ~(ATA_STAT_DRQ | ATA_STAT_BUSY);
    }

    if (   (u32Value & AHCI_PORT_SERR_N)
        && (pAhciPort->regSERR & AHCI_PORT_SERR_N))
        ASMAtomicAndU32(&pAhciPort->regIS, ~AHCI_PORT_IS_PRCS);

    pAhciPort->regSERR &= ~u32Value;

    return VINF_SUCCESS;
}

static VBOXSTRICTRC PortSError_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: read regSERR=%#010x\n", __FUNCTION__, pAhciPort->regSERR));
    *pu32Value = pAhciPort->regSERR;
    return VINF_SUCCESS;
}

static VBOXSTRICTRC PortSControl_w(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pThis, iReg);
    ahciLog(("%s: write u32Value=%#010x\n", __FUNCTION__, u32Value));
    ahciLog(("%s: IPM=%d SPD=%d DET=%d\n", __FUNCTION__,
             AHCI_PORT_SCTL_IPM_GET(u32Value), AHCI_PORT_SCTL_SPD_GET(u32Value), AHCI_PORT_SCTL_DET_GET(u32Value)));

#ifndef IN_RING3
    RT_NOREF(pDevIns, pAhciPort, u32Value);
    return VINF_IOM_R3_MMIO_WRITE;
#else
    if ((u32Value & AHCI_PORT_SCTL_DET) == AHCI_PORT_SCTL_DET_INIT)
    {
        if (!ASMAtomicXchgBool(&pAhciPort->fPortReset, true))
            LogRel(("AHCI#%u: Port %d reset\n", pDevIns->iInstance,
                    pAhciPort->iLUN));

        pAhciPort->regSSTS = 0;
        pAhciPort->regSIG  = UINT32_MAX;
        pAhciPort->regTFD  = 0x7f;
        pAhciPort->fFirstD2HFisSent = false;
        pAhciPort->regSCTL = u32Value;
    }
    else if (   (u32Value & AHCI_PORT_SCTL_DET) == AHCI_PORT_SCTL_DET_NINIT
             && (pAhciPort->regSCTL & AHCI_PORT_SCTL_DET) == AHCI_PORT_SCTL_DET_INIT
             && pAhciPort->fPresent)
    {
        /* Do the port reset here, so the guest sees the new status immediately. */
        if (pThis->fLegacyPortResetMethod)
        {
            PAHCIR3     pThisCC     = PDMDEVINS_2_DATA_CC(pDevIns, PAHCICC);
            PAHCIPORTR3 pAhciPortR3 = &RT_SAFE_SUBSCRIPT(pThisCC->aPorts, pAhciPort->iLUN);
            ahciPortResetFinish(pDevIns, pThis, pAhciPort, pAhciPortR3);
            pAhciPort->regSCTL = u32Value; /* Update after finishing the reset, so the I/O thread doesn't get a chance to do the reset. */
        }
        else
        {
            if (!pThis->fTigerHack)
                pAhciPort->regSSTS = 0x1;   /* Indicate device presence detected but communication not established. */
            else
                pAhciPort->regSSTS = 0x0;   /* Indicate no device detected after COMRESET. [tiger hack] */
            pAhciPort->regSCTL = u32Value;  /* Update before kicking the I/O thread. */

            /* Kick the thread to finish the reset. */
            ahciIoThreadKick(pDevIns, pAhciPort);
        }
    }
    else /* Just update the value if there is no device attached. */
        pAhciPort->regSCTL = u32Value;

    return VINF_SUCCESS;
#endif
}

static VBOXSTRICTRC PortSControl_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: read regSCTL=%#010x\n", __FUNCTION__, pAhciPort->regSCTL));
    ahciLog(("%s: IPM=%d SPD=%d DET=%d\n", __FUNCTION__,
             AHCI_PORT_SCTL_IPM_GET(pAhciPort->regSCTL), AHCI_PORT_SCTL_SPD_GET(pAhciPort->regSCTL),
             AHCI_PORT_SCTL_DET_GET(pAhciPort->regSCTL)));

    *pu32Value = pAhciPort->regSCTL;
    return VINF_SUCCESS;
}

static VBOXSTRICTRC PortSStatus_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: read regSSTS=%#010x\n", __FUNCTION__, pAhciPort->regSSTS));
    ahciLog(("%s: IPM=%d SPD=%d DET=%d\n", __FUNCTION__,
             AHCI_PORT_SSTS_IPM_GET(pAhciPort->regSSTS), AHCI_PORT_SSTS_SPD_GET(pAhciPort->regSSTS),
             AHCI_PORT_SSTS_DET_GET(pAhciPort->regSSTS)));

    *pu32Value = pAhciPort->regSSTS;
    return VINF_SUCCESS;
}

static VBOXSTRICTRC PortSignature_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: read regSIG=%#010x\n", __FUNCTION__, pAhciPort->regSIG));
    *pu32Value = pAhciPort->regSIG;
    return VINF_SUCCESS;
}

static VBOXSTRICTRC PortTaskFileData_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: read regTFD=%#010x\n", __FUNCTION__, pAhciPort->regTFD));
    ahciLog(("%s: ERR=%x BSY=%d DRQ=%d ERR=%d\n", __FUNCTION__,
             (pAhciPort->regTFD >> 8), (pAhciPort->regTFD & AHCI_PORT_TFD_BSY) >> 7,
             (pAhciPort->regTFD & AHCI_PORT_TFD_DRQ) >> 3, (pAhciPort->regTFD & AHCI_PORT_TFD_ERR)));
    *pu32Value = pAhciPort->regTFD;
    return VINF_SUCCESS;
}

/**
 * Read from the port command register.
 */
static VBOXSTRICTRC PortCmd_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: read regCMD=%#010x\n", __FUNCTION__, pAhciPort->regCMD | AHCI_PORT_CMD_CCS_SHIFT(pAhciPort->u32CurrentCommandSlot)));
    ahciLog(("%s: ICC=%d ASP=%d ALPE=%d DLAE=%d ATAPI=%d CPD=%d ISP=%d HPCP=%d PMA=%d CPS=%d CR=%d FR=%d ISS=%d CCS=%d FRE=%d CLO=%d POD=%d SUD=%d ST=%d\n",
             __FUNCTION__, (pAhciPort->regCMD & AHCI_PORT_CMD_ICC) >> 28, (pAhciPort->regCMD & AHCI_PORT_CMD_ASP) >> 27,
             (pAhciPort->regCMD & AHCI_PORT_CMD_ALPE) >> 26, (pAhciPort->regCMD & AHCI_PORT_CMD_DLAE) >> 25,
             (pAhciPort->regCMD & AHCI_PORT_CMD_ATAPI) >> 24, (pAhciPort->regCMD & AHCI_PORT_CMD_CPD) >> 20,
             (pAhciPort->regCMD & AHCI_PORT_CMD_ISP) >> 19, (pAhciPort->regCMD & AHCI_PORT_CMD_HPCP) >> 18,
             (pAhciPort->regCMD & AHCI_PORT_CMD_PMA) >> 17, (pAhciPort->regCMD & AHCI_PORT_CMD_CPS) >> 16,
             (pAhciPort->regCMD & AHCI_PORT_CMD_CR) >> 15, (pAhciPort->regCMD & AHCI_PORT_CMD_FR) >> 14,
             (pAhciPort->regCMD & AHCI_PORT_CMD_ISS) >> 13, pAhciPort->u32CurrentCommandSlot,
             (pAhciPort->regCMD & AHCI_PORT_CMD_FRE) >> 4, (pAhciPort->regCMD & AHCI_PORT_CMD_CLO) >> 3,
             (pAhciPort->regCMD & AHCI_PORT_CMD_POD) >> 2, (pAhciPort->regCMD & AHCI_PORT_CMD_SUD) >> 1,
             (pAhciPort->regCMD & AHCI_PORT_CMD_ST)));
    *pu32Value = pAhciPort->regCMD | AHCI_PORT_CMD_CCS_SHIFT(pAhciPort->u32CurrentCommandSlot);
    return VINF_SUCCESS;
}

/**
 * Write to the port command register.
 * This is the register where all the data transfer is started
 */
static VBOXSTRICTRC PortCmd_w(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: write u32Value=%#010x\n", __FUNCTION__, u32Value));
    ahciLog(("%s: ICC=%d ASP=%d ALPE=%d DLAE=%d ATAPI=%d CPD=%d ISP=%d HPCP=%d PMA=%d CPS=%d CR=%d FR=%d ISS=%d CCS=%d FRE=%d CLO=%d POD=%d SUD=%d ST=%d\n",
             __FUNCTION__, (u32Value & AHCI_PORT_CMD_ICC) >> 28, (u32Value & AHCI_PORT_CMD_ASP) >> 27,
             (u32Value & AHCI_PORT_CMD_ALPE) >> 26, (u32Value & AHCI_PORT_CMD_DLAE) >> 25,
             (u32Value & AHCI_PORT_CMD_ATAPI) >> 24, (u32Value & AHCI_PORT_CMD_CPD) >> 20,
             (u32Value & AHCI_PORT_CMD_ISP) >> 19, (u32Value & AHCI_PORT_CMD_HPCP) >> 18,
             (u32Value & AHCI_PORT_CMD_PMA) >> 17, (u32Value & AHCI_PORT_CMD_CPS) >> 16,
             (u32Value & AHCI_PORT_CMD_CR) >> 15, (u32Value & AHCI_PORT_CMD_FR) >> 14,
             (u32Value & AHCI_PORT_CMD_ISS) >> 13, (u32Value & AHCI_PORT_CMD_CCS) >> 8,
             (u32Value & AHCI_PORT_CMD_FRE) >> 4, (u32Value & AHCI_PORT_CMD_CLO) >> 3,
             (u32Value & AHCI_PORT_CMD_POD) >> 2, (u32Value & AHCI_PORT_CMD_SUD) >> 1,
             (u32Value & AHCI_PORT_CMD_ST)));

    /* The PxCMD.CCS bits are R/O and maintained separately. */
    u32Value &= ~AHCI_PORT_CMD_CCS;

    if (pAhciPort->fPoweredOn && pAhciPort->fSpunUp)
    {
        if (u32Value & AHCI_PORT_CMD_CLO)
        {
            ahciLog(("%s: Command list override requested\n", __FUNCTION__));
            u32Value &= ~(AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ);
            /* Clear the CLO bit. */
            u32Value &= ~(AHCI_PORT_CMD_CLO);
        }

        if (u32Value & AHCI_PORT_CMD_ST)
        {
            /*
             * Set engine state to running if there is a device attached and
             * IS.PCS is clear.
             */
            if (   pAhciPort->fPresent
                && !(pAhciPort->regIS & AHCI_PORT_IS_PCS))
            {
                ahciLog(("%s: Engine starts\n", __FUNCTION__));
                u32Value |= AHCI_PORT_CMD_CR;

                /* If there is something in CI, kick the I/O thread. */
                if (   pAhciPort->regCI > 0
                    && ASMAtomicReadBool(&pAhciPort->fWrkThreadSleeping))
                {
                    ASMAtomicOrU32(&pAhciPort->u32TasksNew, pAhciPort->regCI);
                    LogFlowFunc(("Signal event semaphore\n"));
                    int rc = PDMDevHlpSUPSemEventSignal(pDevIns, pAhciPort->hEvtProcess);
                    AssertRC(rc);
                }
            }
            else
            {
                if (!pAhciPort->fPresent)
                    ahciLog(("%s: No pDrvBase, clearing PxCMD.CR!\n", __FUNCTION__));
                else
                    ahciLog(("%s: PxIS.PCS set (PxIS=%#010x), clearing PxCMD.CR!\n", __FUNCTION__, pAhciPort->regIS));

                u32Value &= ~AHCI_PORT_CMD_CR;
            }
        }
        else
        {
            ahciLog(("%s: Engine stops\n", __FUNCTION__));
            /* Clear command issue register. */
            pAhciPort->regCI = 0;
            pAhciPort->regSACT = 0;
            /* Clear current command slot. */
            pAhciPort->u32CurrentCommandSlot = 0;
            u32Value &= ~AHCI_PORT_CMD_CR;
        }
    }
    else if (pAhciPort->fPresent)
    {
        if ((u32Value & AHCI_PORT_CMD_POD) && (pAhciPort->regCMD & AHCI_PORT_CMD_CPS) && !pAhciPort->fPoweredOn)
        {
            ahciLog(("%s: Power on the device\n", __FUNCTION__));
            pAhciPort->fPoweredOn = true;

            /*
             * Set states in the Port Signature and SStatus registers.
             */
            if (pAhciPort->fATAPI)
                pAhciPort->regSIG = AHCI_PORT_SIG_ATAPI;
            else
                pAhciPort->regSIG = AHCI_PORT_SIG_DISK;
            pAhciPort->regSSTS = (0x01 << 8) | /* Interface is active. */
                                 (0x02 << 4) | /* Generation 2 (3.0GBps) speed. */
                                 (0x03 << 0);  /* Device detected and communication established. */

            if (pAhciPort->regCMD & AHCI_PORT_CMD_FRE)
            {
#ifndef IN_RING3
                return VINF_IOM_R3_MMIO_WRITE;
#else
                ahciPostFirstD2HFisIntoMemory(pDevIns, pAhciPort);
                ASMAtomicOrU32(&pAhciPort->regIS, AHCI_PORT_IS_DHRS);

                if (pAhciPort->regIE & AHCI_PORT_IE_DHRE)
                {
                    int rc = ahciHbaSetInterrupt(pDevIns, pThis, pAhciPort->iLUN, VERR_IGNORED);
                    AssertRC(rc);
                }
#endif
            }
        }

        if ((u32Value & AHCI_PORT_CMD_SUD) && pAhciPort->fPoweredOn && !pAhciPort->fSpunUp)
        {
            ahciLog(("%s: Spin up the device\n", __FUNCTION__));
            pAhciPort->fSpunUp = true;
        }
    }
    else
        ahciLog(("%s: No pDrvBase, no fPoweredOn + fSpunUp, doing nothing!\n", __FUNCTION__));

    if (u32Value & AHCI_PORT_CMD_FRE)
    {
        ahciLog(("%s: FIS receive enabled\n", __FUNCTION__));

        u32Value |= AHCI_PORT_CMD_FR;

        /* Send the first D2H FIS only if it wasn't already sent. */
        if (   !pAhciPort->fFirstD2HFisSent
            && pAhciPort->fPresent)
        {
#ifndef IN_RING3
            return VINF_IOM_R3_MMIO_WRITE;
#else
            ahciPostFirstD2HFisIntoMemory(pDevIns, pAhciPort);
            pAhciPort->fFirstD2HFisSent = true;
#endif
        }
    }
    else if (!(u32Value & AHCI_PORT_CMD_FRE))
    {
        ahciLog(("%s: FIS receive disabled\n", __FUNCTION__));
        u32Value &= ~AHCI_PORT_CMD_FR;
    }

    pAhciPort->regCMD = u32Value;

    return VINF_SUCCESS;
}

/**
 * Read from the port interrupt enable register.
 */
static VBOXSTRICTRC PortIntrEnable_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: read regIE=%#010x\n", __FUNCTION__, pAhciPort->regIE));
    ahciLog(("%s: CPDE=%d TFEE=%d HBFE=%d HBDE=%d IFE=%d INFE=%d OFE=%d IPME=%d PRCE=%d DIE=%d PCE=%d DPE=%d UFE=%d SDBE=%d DSE=%d PSE=%d DHRE=%d\n",
             __FUNCTION__, (pAhciPort->regIE & AHCI_PORT_IE_CPDE) >> 31, (pAhciPort->regIE & AHCI_PORT_IE_TFEE) >> 30,
             (pAhciPort->regIE & AHCI_PORT_IE_HBFE) >> 29, (pAhciPort->regIE & AHCI_PORT_IE_HBDE) >> 28,
             (pAhciPort->regIE & AHCI_PORT_IE_IFE) >> 27, (pAhciPort->regIE & AHCI_PORT_IE_INFE) >> 26,
             (pAhciPort->regIE & AHCI_PORT_IE_OFE) >> 24, (pAhciPort->regIE & AHCI_PORT_IE_IPME) >> 23,
             (pAhciPort->regIE & AHCI_PORT_IE_PRCE) >> 22, (pAhciPort->regIE & AHCI_PORT_IE_DIE) >> 7,
             (pAhciPort->regIE & AHCI_PORT_IE_PCE) >> 6, (pAhciPort->regIE & AHCI_PORT_IE_DPE) >> 5,
             (pAhciPort->regIE & AHCI_PORT_IE_UFE) >> 4, (pAhciPort->regIE & AHCI_PORT_IE_SDBE) >> 3,
             (pAhciPort->regIE & AHCI_PORT_IE_DSE) >> 2, (pAhciPort->regIE & AHCI_PORT_IE_PSE) >> 1,
             (pAhciPort->regIE & AHCI_PORT_IE_DHRE)));
    *pu32Value = pAhciPort->regIE;
    return VINF_SUCCESS;
}

/**
 * Write to the port interrupt enable register.
 */
static VBOXSTRICTRC PortIntrEnable_w(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(iReg);
    ahciLog(("%s: write u32Value=%#010x\n", __FUNCTION__, u32Value));
    ahciLog(("%s: CPDE=%d TFEE=%d HBFE=%d HBDE=%d IFE=%d INFE=%d OFE=%d IPME=%d PRCE=%d DIE=%d PCE=%d DPE=%d UFE=%d SDBE=%d DSE=%d PSE=%d DHRE=%d\n",
             __FUNCTION__, (u32Value & AHCI_PORT_IE_CPDE) >> 31, (u32Value & AHCI_PORT_IE_TFEE) >> 30,
             (u32Value & AHCI_PORT_IE_HBFE) >> 29, (u32Value & AHCI_PORT_IE_HBDE) >> 28,
             (u32Value & AHCI_PORT_IE_IFE) >> 27, (u32Value & AHCI_PORT_IE_INFE) >> 26,
             (u32Value & AHCI_PORT_IE_OFE) >> 24, (u32Value & AHCI_PORT_IE_IPME) >> 23,
             (u32Value & AHCI_PORT_IE_PRCE) >> 22, (u32Value & AHCI_PORT_IE_DIE) >> 7,
             (u32Value & AHCI_PORT_IE_PCE) >> 6, (u32Value & AHCI_PORT_IE_DPE) >> 5,
             (u32Value & AHCI_PORT_IE_UFE) >> 4, (u32Value & AHCI_PORT_IE_SDBE) >> 3,
             (u32Value & AHCI_PORT_IE_DSE) >> 2, (u32Value & AHCI_PORT_IE_PSE) >> 1,
             (u32Value & AHCI_PORT_IE_DHRE)));

    u32Value &= AHCI_PORT_IE_READONLY;

    /* Check if some a interrupt status bit changed*/
    uint32_t u32IntrStatus = ASMAtomicReadU32(&pAhciPort->regIS);

    int rc = VINF_SUCCESS;
    if (u32Value & u32IntrStatus)
        rc = ahciHbaSetInterrupt(pDevIns, pThis, pAhciPort->iLUN, VINF_IOM_R3_MMIO_WRITE);

    if (rc == VINF_SUCCESS)
        pAhciPort->regIE = u32Value;

    return rc;
}

/**
 * Read from the port interrupt status register.
 */
static VBOXSTRICTRC PortIntrSts_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: read regIS=%#010x\n", __FUNCTION__, pAhciPort->regIS));
    ahciLog(("%s: CPDS=%d TFES=%d HBFS=%d HBDS=%d IFS=%d INFS=%d OFS=%d IPMS=%d PRCS=%d DIS=%d PCS=%d DPS=%d UFS=%d SDBS=%d DSS=%d PSS=%d DHRS=%d\n",
             __FUNCTION__, (pAhciPort->regIS & AHCI_PORT_IS_CPDS) >> 31, (pAhciPort->regIS & AHCI_PORT_IS_TFES) >> 30,
             (pAhciPort->regIS & AHCI_PORT_IS_HBFS) >> 29, (pAhciPort->regIS & AHCI_PORT_IS_HBDS) >> 28,
             (pAhciPort->regIS & AHCI_PORT_IS_IFS) >> 27, (pAhciPort->regIS & AHCI_PORT_IS_INFS) >> 26,
             (pAhciPort->regIS & AHCI_PORT_IS_OFS) >> 24, (pAhciPort->regIS & AHCI_PORT_IS_IPMS) >> 23,
             (pAhciPort->regIS & AHCI_PORT_IS_PRCS) >> 22, (pAhciPort->regIS & AHCI_PORT_IS_DIS) >> 7,
             (pAhciPort->regIS & AHCI_PORT_IS_PCS) >> 6, (pAhciPort->regIS & AHCI_PORT_IS_DPS) >> 5,
             (pAhciPort->regIS & AHCI_PORT_IS_UFS) >> 4, (pAhciPort->regIS & AHCI_PORT_IS_SDBS) >> 3,
             (pAhciPort->regIS & AHCI_PORT_IS_DSS) >> 2, (pAhciPort->regIS & AHCI_PORT_IS_PSS) >> 1,
             (pAhciPort->regIS & AHCI_PORT_IS_DHRS)));
    *pu32Value = pAhciPort->regIS;
    return VINF_SUCCESS;
}

/**
 * Write to the port interrupt status register.
 */
static VBOXSTRICTRC PortIntrSts_w(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: write u32Value=%#010x\n", __FUNCTION__, u32Value));
    ASMAtomicAndU32(&pAhciPort->regIS, ~(u32Value & AHCI_PORT_IS_READONLY));

    return VINF_SUCCESS;
}

/**
 * Read from the port FIS base address upper 32bit register.
 */
static VBOXSTRICTRC PortFisAddrUp_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: read regFBU=%#010x\n", __FUNCTION__, pAhciPort->regFBU));
    *pu32Value = pAhciPort->regFBU;
    return VINF_SUCCESS;
}

/**
 * Write to the port FIS base address upper 32bit register.
 */
static VBOXSTRICTRC PortFisAddrUp_w(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: write u32Value=%#010x\n", __FUNCTION__, u32Value));

    pAhciPort->regFBU = u32Value;
    pAhciPort->GCPhysAddrFb = AHCI_RTGCPHYS_FROM_U32(pAhciPort->regFBU, pAhciPort->regFB);

    return VINF_SUCCESS;
}

/**
 * Read from the port FIS base address register.
 */
static VBOXSTRICTRC PortFisAddr_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: read regFB=%#010x\n", __FUNCTION__, pAhciPort->regFB));
    *pu32Value = pAhciPort->regFB;
    return VINF_SUCCESS;
}

/**
 * Write to the port FIS base address register.
 */
static VBOXSTRICTRC PortFisAddr_w(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: write u32Value=%#010x\n", __FUNCTION__, u32Value));

    Assert(!(u32Value & ~AHCI_PORT_FB_RESERVED));

    pAhciPort->regFB = (u32Value & AHCI_PORT_FB_RESERVED);
    pAhciPort->GCPhysAddrFb = AHCI_RTGCPHYS_FROM_U32(pAhciPort->regFBU, pAhciPort->regFB);

    return VINF_SUCCESS;
}

/**
 * Write to the port command list base address upper 32bit register.
 */
static VBOXSTRICTRC PortCmdLstAddrUp_w(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: write u32Value=%#010x\n", __FUNCTION__, u32Value));

    pAhciPort->regCLBU = u32Value;
    pAhciPort->GCPhysAddrClb = AHCI_RTGCPHYS_FROM_U32(pAhciPort->regCLBU, pAhciPort->regCLB);

    return VINF_SUCCESS;
}

/**
 * Read from the port command list base address upper 32bit register.
 */
static VBOXSTRICTRC PortCmdLstAddrUp_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: read regCLBU=%#010x\n", __FUNCTION__, pAhciPort->regCLBU));
    *pu32Value = pAhciPort->regCLBU;
    return VINF_SUCCESS;
}

/**
 * Read from the port command list base address register.
 */
static VBOXSTRICTRC PortCmdLstAddr_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: read regCLB=%#010x\n", __FUNCTION__, pAhciPort->regCLB));
    *pu32Value = pAhciPort->regCLB;
    return VINF_SUCCESS;
}

/**
 * Write to the port command list base address register.
 */
static VBOXSTRICTRC PortCmdLstAddr_w(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    ahciLog(("%s: write u32Value=%#010x\n", __FUNCTION__, u32Value));

    Assert(!(u32Value & ~AHCI_PORT_CLB_RESERVED));

    pAhciPort->regCLB = (u32Value & AHCI_PORT_CLB_RESERVED);
    pAhciPort->GCPhysAddrClb = AHCI_RTGCPHYS_FROM_U32(pAhciPort->regCLBU, pAhciPort->regCLB);

    return VINF_SUCCESS;
}

/**
 * Read from the global Version register.
 */
static VBOXSTRICTRC HbaVersion_r(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    Log(("%s: read regHbaVs=%#010x\n", __FUNCTION__, pThis->regHbaVs));
    *pu32Value = pThis->regHbaVs;
    return VINF_SUCCESS;
}

/**
 * Read from the global Ports implemented register.
 */
static VBOXSTRICTRC HbaPortsImplemented_r(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    Log(("%s: read regHbaPi=%#010x\n", __FUNCTION__, pThis->regHbaPi));
    *pu32Value = pThis->regHbaPi;
    return VINF_SUCCESS;
}

/**
 * Write to the global interrupt status register.
 */
static VBOXSTRICTRC HbaInterruptStatus_w(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(iReg);
    Log(("%s: write u32Value=%#010x\n", __FUNCTION__, u32Value));

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->lock, VINF_IOM_R3_MMIO_WRITE);
    if (rc != VINF_SUCCESS)
        return rc;

    pThis->regHbaIs &= ~(u32Value);

    /*
     * Update interrupt status register and check for ports who
     * set the interrupt inbetween.
     */
    bool fClear = true;
    pThis->regHbaIs |= ASMAtomicXchgU32(&pThis->u32PortsInterrupted, 0);
    if (!pThis->regHbaIs)
    {
        unsigned i = 0;

        /* Check if the cleared ports have a interrupt status bit set. */
        while ((u32Value > 0) && (i < AHCI_MAX_NR_PORTS_IMPL))
        {
            if (u32Value & 0x01)
            {
                PAHCIPORT pAhciPort = &pThis->aPorts[i];

                if (pAhciPort->regIE & pAhciPort->regIS)
                {
                    Log(("%s: Interrupt status of port %u set -> Set interrupt again\n", __FUNCTION__, i));
                    ASMAtomicOrU32(&pThis->u32PortsInterrupted, 1 << i);
                    fClear = false;
                    break;
                }
            }
            u32Value >>= 1;
            i++;
        }
    }
    else
        fClear = false;

    if (fClear)
        ahciHbaClearInterrupt(pDevIns);
    else
    {
        Log(("%s: Not clearing interrupt: u32PortsInterrupted=%#010x\n", __FUNCTION__, pThis->u32PortsInterrupted));
        /*
         * We need to set the interrupt again because the I/O APIC does not set it again even if the
         * line is still high.
         * We need to clear it first because the PCI bus only calls the interrupt controller if the state changes.
         */
        PDMDevHlpPCISetIrq(pDevIns, 0, 0);
        PDMDevHlpPCISetIrq(pDevIns, 0, 1);
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->lock);
    return VINF_SUCCESS;
}

/**
 * Read from the global interrupt status register.
 */
static VBOXSTRICTRC HbaInterruptStatus_r(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(iReg);

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->lock, VINF_IOM_R3_MMIO_READ);
    if (rc != VINF_SUCCESS)
        return rc;

    uint32_t u32PortsInterrupted = ASMAtomicXchgU32(&pThis->u32PortsInterrupted, 0);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->lock);
    Log(("%s: read regHbaIs=%#010x u32PortsInterrupted=%#010x\n", __FUNCTION__, pThis->regHbaIs, u32PortsInterrupted));

    pThis->regHbaIs |= u32PortsInterrupted;

#ifdef LOG_ENABLED
    Log(("%s:", __FUNCTION__));
    uint32_t const cPortsImpl = RT_MIN(pThis->cPortsImpl, RT_ELEMENTS(pThis->aPorts));
    for (unsigned i = 0; i < cPortsImpl; i++)
    {
        if ((pThis->regHbaIs >> i) & 0x01)
            Log((" P%d", i));
    }
    Log(("\n"));
#endif

    *pu32Value = pThis->regHbaIs;

    return VINF_SUCCESS;
}

/**
 * Write to the global control register.
 */
static VBOXSTRICTRC HbaControl_w(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t iReg, uint32_t u32Value)
{
    Log(("%s: write u32Value=%#010x\n"
         "%s: AE=%d IE=%d HR=%d\n",
         __FUNCTION__, u32Value,
         __FUNCTION__, (u32Value & AHCI_HBA_CTRL_AE) >> 31, (u32Value & AHCI_HBA_CTRL_IE) >> 1,
         (u32Value & AHCI_HBA_CTRL_HR)));
    RT_NOREF(iReg);

#ifndef IN_RING3
    RT_NOREF(pDevIns, pThis, u32Value);
    return VINF_IOM_R3_MMIO_WRITE;
#else
    /*
     * Increase the active thread counter because we might set the host controller
     * reset bit.
     */
    ASMAtomicIncU32(&pThis->cThreadsActive);
    ASMAtomicWriteU32(&pThis->regHbaCtrl, (u32Value & AHCI_HBA_CTRL_RW_MASK) | AHCI_HBA_CTRL_AE);

    /*
     * Do the HBA reset if requested and there is no other active thread at the moment,
     * the work is deferred to the last active thread otherwise.
     */
    uint32_t cThreadsActive = ASMAtomicDecU32(&pThis->cThreadsActive);
    if (   (u32Value & AHCI_HBA_CTRL_HR)
        && !cThreadsActive)
        ahciR3HBAReset(pDevIns, pThis, PDMDEVINS_2_DATA_CC(pDevIns, PAHCICC));

    return VINF_SUCCESS;
#endif
}

/**
 * Read the global control register.
 */
static VBOXSTRICTRC HbaControl_r(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    Log(("%s: read regHbaCtrl=%#010x\n"
         "%s: AE=%d IE=%d HR=%d\n",
         __FUNCTION__, pThis->regHbaCtrl,
         __FUNCTION__, (pThis->regHbaCtrl & AHCI_HBA_CTRL_AE) >> 31, (pThis->regHbaCtrl & AHCI_HBA_CTRL_IE) >> 1,
         (pThis->regHbaCtrl & AHCI_HBA_CTRL_HR)));
    *pu32Value = pThis->regHbaCtrl;
    return VINF_SUCCESS;
}

/**
 * Read the global capabilities register.
 */
static VBOXSTRICTRC HbaCapabilities_r(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    Log(("%s: read regHbaCap=%#010x\n"
         "%s: S64A=%d SNCQ=%d SIS=%d SSS=%d SALP=%d SAL=%d SCLO=%d ISS=%d SNZO=%d SAM=%d SPM=%d PMD=%d SSC=%d PSC=%d NCS=%d NP=%d\n",
          __FUNCTION__, pThis->regHbaCap,
          __FUNCTION__, (pThis->regHbaCap & AHCI_HBA_CAP_S64A) >> 31, (pThis->regHbaCap & AHCI_HBA_CAP_SNCQ) >> 30,
          (pThis->regHbaCap & AHCI_HBA_CAP_SIS) >> 28, (pThis->regHbaCap & AHCI_HBA_CAP_SSS) >> 27,
          (pThis->regHbaCap & AHCI_HBA_CAP_SALP) >> 26, (pThis->regHbaCap & AHCI_HBA_CAP_SAL) >> 25,
          (pThis->regHbaCap & AHCI_HBA_CAP_SCLO) >> 24, (pThis->regHbaCap & AHCI_HBA_CAP_ISS) >> 20,
          (pThis->regHbaCap & AHCI_HBA_CAP_SNZO) >> 19, (pThis->regHbaCap & AHCI_HBA_CAP_SAM) >> 18,
          (pThis->regHbaCap & AHCI_HBA_CAP_SPM) >> 17, (pThis->regHbaCap & AHCI_HBA_CAP_PMD) >> 15,
          (pThis->regHbaCap & AHCI_HBA_CAP_SSC) >> 14, (pThis->regHbaCap & AHCI_HBA_CAP_PSC) >> 13,
          (pThis->regHbaCap & AHCI_HBA_CAP_NCS) >> 8, (pThis->regHbaCap & AHCI_HBA_CAP_NP)));
    *pu32Value = pThis->regHbaCap;
    return VINF_SUCCESS;
}

/**
 * Write to the global command completion coalescing control register.
 */
static VBOXSTRICTRC HbaCccCtl_w(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(iReg);
    Log(("%s: write u32Value=%#010x\n"
         "%s: TV=%d CC=%d INT=%d EN=%d\n",
         __FUNCTION__, u32Value,
         __FUNCTION__, AHCI_HBA_CCC_CTL_TV_GET(u32Value), AHCI_HBA_CCC_CTL_CC_GET(u32Value),
         AHCI_HBA_CCC_CTL_INT_GET(u32Value), (u32Value & AHCI_HBA_CCC_CTL_EN)));

    pThis->regHbaCccCtl = u32Value;
    pThis->uCccTimeout  = AHCI_HBA_CCC_CTL_TV_GET(u32Value);
    pThis->uCccPortNr   = AHCI_HBA_CCC_CTL_INT_GET(u32Value);
    pThis->uCccNr       = AHCI_HBA_CCC_CTL_CC_GET(u32Value);

    if (u32Value & AHCI_HBA_CCC_CTL_EN)
        PDMDevHlpTimerSetMillies(pDevIns, pThis->hHbaCccTimer, pThis->uCccTimeout); /* Arm the timer */
    else
        PDMDevHlpTimerStop(pDevIns, pThis->hHbaCccTimer);

    return VINF_SUCCESS;
}

/**
 * Read the global command completion coalescing control register.
 */
static VBOXSTRICTRC HbaCccCtl_r(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    Log(("%s: read regHbaCccCtl=%#010x\n"
         "%s: TV=%d CC=%d INT=%d EN=%d\n",
         __FUNCTION__, pThis->regHbaCccCtl,
         __FUNCTION__, AHCI_HBA_CCC_CTL_TV_GET(pThis->regHbaCccCtl), AHCI_HBA_CCC_CTL_CC_GET(pThis->regHbaCccCtl),
         AHCI_HBA_CCC_CTL_INT_GET(pThis->regHbaCccCtl), (pThis->regHbaCccCtl & AHCI_HBA_CCC_CTL_EN)));
    *pu32Value = pThis->regHbaCccCtl;
    return VINF_SUCCESS;
}

/**
 * Write to the global command completion coalescing ports register.
 */
static VBOXSTRICTRC HbaCccPorts_w(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, iReg);
    Log(("%s: write u32Value=%#010x\n", __FUNCTION__, u32Value));

    pThis->regHbaCccPorts = u32Value;

    return VINF_SUCCESS;
}

/**
 * Read the global command completion coalescing ports register.
 */
static VBOXSTRICTRC HbaCccPorts_r(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    Log(("%s: read regHbaCccPorts=%#010x\n", __FUNCTION__, pThis->regHbaCccPorts));

#ifdef LOG_ENABLED
    Log(("%s:", __FUNCTION__));
    uint32_t const cPortsImpl = RT_MIN(pThis->cPortsImpl, RT_ELEMENTS(pThis->aPorts));
    for (unsigned i = 0; i < cPortsImpl; i++)
    {
        if ((pThis->regHbaCccPorts >> i) & 0x01)
            Log((" P%d", i));
    }
    Log(("\n"));
#endif

    *pu32Value = pThis->regHbaCccPorts;
    return VINF_SUCCESS;
}

/**
 * Invalid write to global register
 */
static VBOXSTRICTRC HbaInvalid_w(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, pThis, iReg, u32Value);
    Log(("%s: Write denied!!! iReg=%u u32Value=%#010x\n", __FUNCTION__, iReg, u32Value));
    return VINF_SUCCESS;
}

/**
 * Invalid Port write.
 */
static VBOXSTRICTRC PortInvalid_w(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, pThis, pAhciPort, iReg, u32Value);
    ahciLog(("%s: Write denied!!! iReg=%u u32Value=%#010x\n", __FUNCTION__, iReg, u32Value));
    return VINF_SUCCESS;
}

/**
 * Invalid Port read.
 */
static VBOXSTRICTRC PortInvalid_r(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, pAhciPort, iReg, pu32Value);
    ahciLog(("%s: Read denied!!! iReg=%u\n", __FUNCTION__, iReg));
    return VINF_IOM_MMIO_UNUSED_00;
}

/**
 * Register descriptor table for global HBA registers
 */
static const AHCIOPREG g_aOpRegs[] =
{
    {"HbaCapabilites",      HbaCapabilities_r,     HbaInvalid_w}, /* Readonly */
    {"HbaControl"    ,      HbaControl_r,          HbaControl_w},
    {"HbaInterruptStatus",  HbaInterruptStatus_r,  HbaInterruptStatus_w},
    {"HbaPortsImplemented", HbaPortsImplemented_r, HbaInvalid_w}, /* Readonly */
    {"HbaVersion",          HbaVersion_r,          HbaInvalid_w}, /* ReadOnly */
    {"HbaCccCtl",           HbaCccCtl_r,           HbaCccCtl_w},
    {"HbaCccPorts",         HbaCccPorts_r,         HbaCccPorts_w},
};

/**
 * Register descriptor table for port registers
 */
static const AHCIPORTOPREG g_aPortOpRegs[] =
{
    {"PortCmdLstAddr",   PortCmdLstAddr_r,   PortCmdLstAddr_w},
    {"PortCmdLstAddrUp", PortCmdLstAddrUp_r, PortCmdLstAddrUp_w},
    {"PortFisAddr",      PortFisAddr_r,      PortFisAddr_w},
    {"PortFisAddrUp",    PortFisAddrUp_r,    PortFisAddrUp_w},
    {"PortIntrSts",      PortIntrSts_r,      PortIntrSts_w},
    {"PortIntrEnable",   PortIntrEnable_r,   PortIntrEnable_w},
    {"PortCmd",          PortCmd_r,          PortCmd_w},
    {"PortReserved1",    PortInvalid_r,      PortInvalid_w}, /* Not used. */
    {"PortTaskFileData", PortTaskFileData_r, PortInvalid_w}, /* Readonly */
    {"PortSignature",    PortSignature_r,    PortInvalid_w}, /* Readonly */
    {"PortSStatus",      PortSStatus_r,      PortInvalid_w}, /* Readonly */
    {"PortSControl",     PortSControl_r,     PortSControl_w},
    {"PortSError",       PortSError_r,       PortSError_w},
    {"PortSActive",      PortSActive_r,      PortSActive_w},
    {"PortCmdIssue",     PortCmdIssue_r,     PortCmdIssue_w},
    {"PortReserved2",    PortInvalid_r,      PortInvalid_w}, /* Not used. */
};

#ifdef IN_RING3

/**
 * Reset initiated by system software for one port.
 *
 * @param   pAhciPort       The port to reset, shared bits.
 * @param   pAhciPortR3     The port to reset, ring-3 bits.
 */
static void ahciR3PortSwReset(PAHCIPORT pAhciPort, PAHCIPORTR3 pAhciPortR3)
{
    bool fAllTasksCanceled;

    /* Cancel all tasks first. */
    fAllTasksCanceled = ahciR3CancelActiveTasks(pAhciPortR3);
    Assert(fAllTasksCanceled);

    Assert(pAhciPort->cTasksActive == 0);

    pAhciPort->regIS   = 0;
    pAhciPort->regIE   = 0;
    pAhciPort->regCMD  = AHCI_PORT_CMD_CPD  | /* Cold presence detection */
                         AHCI_PORT_CMD_SUD  | /* Device has spun up. */
                         AHCI_PORT_CMD_POD;   /* Port is powered on. */

    /* Hotplugging supported?. */
    if (pAhciPort->fHotpluggable)
        pAhciPort->regCMD |= AHCI_PORT_CMD_HPCP;

    pAhciPort->regTFD  = (1 << 8) | ATA_STAT_SEEK | ATA_STAT_WRERR;
    pAhciPort->regSIG  = UINT32_MAX;
    pAhciPort->regSSTS = 0;
    pAhciPort->regSCTL = 0;
    pAhciPort->regSERR = 0;
    pAhciPort->regSACT = 0;
    pAhciPort->regCI   = 0;

    pAhciPort->fResetDevice      = false;
    pAhciPort->fPoweredOn        = true;
    pAhciPort->fSpunUp           = true;
    pAhciPort->cMultSectors = ATA_MAX_MULT_SECTORS;
    pAhciPort->uATATransferMode = ATA_MODE_UDMA | 6;

    pAhciPort->u32TasksNew = 0;
    pAhciPort->u32TasksRedo = 0;
    pAhciPort->u32TasksFinished = 0;
    pAhciPort->u32QueuedTasksFinished = 0;
    pAhciPort->u32CurrentCommandSlot = 0;

    if (pAhciPort->fPresent)
    {
        pAhciPort->regCMD |= AHCI_PORT_CMD_CPS; /* Indicate that there is a device on that port */

        if (pAhciPort->fPoweredOn)
        {
            /*
             * Set states in the Port Signature and SStatus registers.
             */
            if (pAhciPort->fATAPI)
                pAhciPort->regSIG = AHCI_PORT_SIG_ATAPI;
            else
                pAhciPort->regSIG = AHCI_PORT_SIG_DISK;
            pAhciPort->regSSTS = (0x01 << 8) | /* Interface is active. */
                                 (0x02 << 4) | /* Generation 2 (3.0GBps) speed. */
                                 (0x03 << 0);  /* Device detected and communication established. */
        }
    }
}

/**
 * Hardware reset used for machine power on and reset.
 *
 * @param pAhciPort     The port to reset, shared bits.
 */
static void ahciPortHwReset(PAHCIPORT pAhciPort)
{
    /* Reset the address registers. */
    pAhciPort->regCLB  = 0;
    pAhciPort->regCLBU = 0;
    pAhciPort->regFB   = 0;
    pAhciPort->regFBU  = 0;

    /* Reset calculated addresses. */
    pAhciPort->GCPhysAddrClb = 0;
    pAhciPort->GCPhysAddrFb  = 0;
}

/**
 * Create implemented ports bitmap.
 *
 * @returns 32bit bitmask with a bit set for every implemented port.
 * @param   cPorts    Number of ports.
 */
static uint32_t ahciGetPortsImplemented(unsigned cPorts)
{
    uint32_t uPortsImplemented = 0;

    for (unsigned i = 0; i < cPorts; i++)
        uPortsImplemented |= (1 << i);

    return uPortsImplemented;
}

/**
 * Reset the entire HBA.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AHCI state.
 * @param   pThisCC     The ring-3 AHCI state.
 */
static void ahciR3HBAReset(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIR3 pThisCC)
{
    unsigned i;
    int rc = VINF_SUCCESS;

    LogRel(("AHCI#%u: Reset the HBA\n", pDevIns->iInstance));

    /* Stop the CCC timer. */
    if (pThis->regHbaCccCtl & AHCI_HBA_CCC_CTL_EN)
    {
        rc = PDMDevHlpTimerStop(pDevIns, pThis->hHbaCccTimer);
        if (RT_FAILURE(rc))
            AssertMsgFailed(("%s: Failed to stop timer!\n", __FUNCTION__));
    }

    /* Reset every port */
    uint32_t const cPortsImpl = RT_MIN(pThis->cPortsImpl, RT_ELEMENTS(pThisCC->aPorts));
    for (i = 0; i < cPortsImpl; i++)
    {
        PAHCIPORT   pAhciPort   = &pThis->aPorts[i];
        PAHCIPORTR3 pAhciPortR3 = &pThisCC->aPorts[i];

        pAhciPort->iLUN   = i;
        pAhciPortR3->iLUN = i;
        ahciR3PortSwReset(pAhciPort, pAhciPortR3);
    }

    /* Init Global registers */
    pThis->regHbaCap      = AHCI_HBA_CAP_ISS_SHIFT(AHCI_HBA_CAP_ISS_GEN2)
                          | AHCI_HBA_CAP_S64A /* 64bit addressing supported */
                          | AHCI_HBA_CAP_SAM  /* AHCI mode only */
                          | AHCI_HBA_CAP_SNCQ /* Support native command queuing */
                          | AHCI_HBA_CAP_SSS  /* Staggered spin up */
                          | AHCI_HBA_CAP_CCCS /* Support command completion coalescing */
                          | AHCI_HBA_CAP_NCS_SET(pThis->cCmdSlotsAvail) /* Number of command slots we support */
                          | AHCI_HBA_CAP_NP_SET(pThis->cPortsImpl); /* Number of supported ports */
    pThis->regHbaCtrl     = AHCI_HBA_CTRL_AE;
    pThis->regHbaPi       = ahciGetPortsImplemented(pThis->cPortsImpl);
    pThis->regHbaVs       = AHCI_HBA_VS_MJR | AHCI_HBA_VS_MNR;
    pThis->regHbaCccCtl   = 0;
    pThis->regHbaCccPorts = 0;
    pThis->uCccTimeout    = 0;
    pThis->uCccPortNr     = 0;
    pThis->uCccNr         = 0;

    /* Clear pending interrupts. */
    pThis->regHbaIs            = 0;
    pThis->u32PortsInterrupted = 0;
    ahciHbaClearInterrupt(pDevIns);

    pThis->f64BitAddr = false;
    pThis->u32PortsInterrupted = 0;
    pThis->f8ByteMMIO4BytesWrittenSuccessfully = false;
    /* Clear the HBA Reset bit */
    pThis->regHbaCtrl &= ~AHCI_HBA_CTRL_HR;
}

#endif /* IN_RING3 */

/**
 * Reads from a AHCI controller register.
 *
 * @returns Strict VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AHCI state.
 * @param   uReg        The register to write.
 * @param   pv          Where to store the result.
 * @param   cb          Number of bytes read.
 */
static VBOXSTRICTRC ahciRegisterRead(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t uReg, void *pv, unsigned cb)
{
    VBOXSTRICTRC rc;
    uint32_t iReg;

    /*
     * If the access offset is smaller than AHCI_HBA_GLOBAL_SIZE the guest accesses the global registers.
     * Otherwise it accesses the registers of a port.
     */
    if (uReg < AHCI_HBA_GLOBAL_SIZE)
    {
        iReg = uReg >> 2;
        Log3(("%s: Trying to read from global register %u\n", __FUNCTION__, iReg));
        if (iReg < RT_ELEMENTS(g_aOpRegs))
        {
            const AHCIOPREG *pReg = &g_aOpRegs[iReg];
            rc = pReg->pfnRead(pDevIns, pThis, iReg, (uint32_t *)pv);
        }
        else
        {
            Log3(("%s: Trying to read global register %u/%u!!!\n", __FUNCTION__, iReg, RT_ELEMENTS(g_aOpRegs)));
            *(uint32_t *)pv = 0;
            rc = VINF_SUCCESS;
        }
    }
    else
    {
        uint32_t iRegOffset;
        uint32_t iPort;

        /* Calculate accessed port. */
        uReg -= AHCI_HBA_GLOBAL_SIZE;
        iPort = uReg / AHCI_PORT_REGISTER_SIZE;
        iRegOffset  = (uReg % AHCI_PORT_REGISTER_SIZE);
        iReg = iRegOffset >> 2;

        Log3(("%s: Trying to read from port %u and register %u\n", __FUNCTION__, iPort, iReg));

        if (RT_LIKELY(   iPort < RT_MIN(pThis->cPortsImpl, RT_ELEMENTS(pThis->aPorts))
                      && iReg < RT_ELEMENTS(g_aPortOpRegs)))
        {
            const AHCIPORTOPREG *pPortReg = &g_aPortOpRegs[iReg];
            rc = pPortReg->pfnRead(pDevIns, pThis, &pThis->aPorts[iPort], iReg, (uint32_t *)pv);
        }
        else
        {
            Log3(("%s: Trying to read port %u register %u/%u!!!\n", __FUNCTION__, iPort, iReg, RT_ELEMENTS(g_aPortOpRegs)));
            rc = VINF_IOM_MMIO_UNUSED_00;
        }

        /*
         * Windows Vista tries to read one byte from some registers instead of four.
         * Correct the value according to the read size.
         */
        if (RT_SUCCESS(rc) && cb != sizeof(uint32_t))
        {
            switch (cb)
            {
                case 1:
                {
                    uint8_t uNewValue;
                    uint8_t *p = (uint8_t *)pv;

                    iRegOffset &= 3;
                    Log3(("%s: iRegOffset=%u\n", __FUNCTION__, iRegOffset));
                    uNewValue = p[iRegOffset];
                    /* Clear old value */
                    *(uint32_t *)pv = 0;
                    *(uint8_t *)pv = uNewValue;
                    break;
                }
                default:
                    ASSERT_GUEST_MSG_FAILED(("%s: unsupported access width cb=%d iPort=%x iRegOffset=%x iReg=%x!!!\n",
                                             __FUNCTION__, cb, iPort, iRegOffset, iReg));
            }
        }
    }

    return rc;
}

/**
 * Writes a value to one of the AHCI controller registers.
 *
 * @returns Strict VBox status code.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AHCI state.
 * @param   offReg      The offset of the register to write to.
 * @param   u32Value    The value to write.
 */
static VBOXSTRICTRC ahciRegisterWrite(PPDMDEVINS pDevIns, PAHCI pThis, uint32_t offReg, uint32_t u32Value)
{
    VBOXSTRICTRC rc;
    uint32_t iReg;

    /*
     * If the access offset is smaller than 100h the guest accesses the global registers.
     * Otherwise it accesses the registers of a port.
     */
    if (offReg < AHCI_HBA_GLOBAL_SIZE)
    {
        Log3(("Write global HBA register\n"));
        iReg = offReg >> 2;
        if (iReg < RT_ELEMENTS(g_aOpRegs))
        {
            const AHCIOPREG *pReg = &g_aOpRegs[iReg];
            rc = pReg->pfnWrite(pDevIns, pThis, iReg, u32Value);
        }
        else
        {
            Log3(("%s: Trying to write global register %u/%u!!!\n", __FUNCTION__, iReg, RT_ELEMENTS(g_aOpRegs)));
            rc = VINF_SUCCESS;
        }
    }
    else
    {
        uint32_t iPort;
        Log3(("Write Port register\n"));
        /* Calculate accessed port. */
        offReg -= AHCI_HBA_GLOBAL_SIZE;
        iPort   =  offReg / AHCI_PORT_REGISTER_SIZE;
        iReg    = (offReg % AHCI_PORT_REGISTER_SIZE) >> 2;
        Log3(("%s: Trying to write to port %u and register %u\n", __FUNCTION__, iPort, iReg));
        if (RT_LIKELY(   iPort < RT_MIN(pThis->cPortsImpl, RT_ELEMENTS(pThis->aPorts))
                      && iReg < RT_ELEMENTS(g_aPortOpRegs)))
        {
            const AHCIPORTOPREG *pPortReg = &g_aPortOpRegs[iReg];
            rc = pPortReg->pfnWrite(pDevIns, pThis, &pThis->aPorts[iPort], iReg, u32Value);
        }
        else
        {
            Log3(("%s: Trying to write port %u register %u/%u!!!\n", __FUNCTION__, iPort, iReg, RT_ELEMENTS(g_aPortOpRegs)));
            rc = VINF_SUCCESS;
        }
    }

    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) ahciMMIORead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PAHCI pThis = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    Log2(("#%d ahciMMIORead: pvUser=%p:{%.*Rhxs} cb=%d off=%RGp\n", pDevIns->iInstance, pv, cb, pv, cb, off));
    RT_NOREF(pvUser);

    VBOXSTRICTRC rc = ahciRegisterRead(pDevIns, pThis, off, pv, cb);

    Log2(("#%d ahciMMIORead: return pvUser=%p:{%.*Rhxs} cb=%d off=%RGp rc=%Rrc\n",
          pDevIns->iInstance, pv, cb, pv, cb, off, VBOXSTRICTRC_VAL(rc)));
    return rc;
}

/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) ahciMMIOWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PAHCI pThis = PDMDEVINS_2_DATA(pDevIns, PAHCI);

    Assert(cb == 4 || cb == 8);     /* Assert IOM flags & sanity  */
    Assert(!(off & (cb - 1)));      /* Ditto. */

    /* Break up 64 bits writes into two dword writes. */
    /** @todo Eliminate this code once the IOM/EM starts taking care of these
     *        situations. */
    if (cb == 8)
    {
        /*
         * Only write the first 4 bytes if they weren't already.
         * It is possible that the last write to the register caused a world
         * switch and we entered this function again.
         * Writing the first 4 bytes again could cause indeterminate behavior
         * which can cause errors in the guest.
         */
        VBOXSTRICTRC rc = VINF_SUCCESS;
        if (!pThis->f8ByteMMIO4BytesWrittenSuccessfully)
        {
            rc = ahciMMIOWrite(pDevIns, pvUser, off, pv, 4);
            if (rc != VINF_SUCCESS)
                return rc;

            pThis->f8ByteMMIO4BytesWrittenSuccessfully = true;
        }

        rc = ahciMMIOWrite(pDevIns, pvUser, off + 4, (uint8_t *)pv + 4, 4);
        /*
         * Reset flag again so that the first 4 bytes are written again on the next
         * 8byte MMIO access.
         */
        if (rc == VINF_SUCCESS)
            pThis->f8ByteMMIO4BytesWrittenSuccessfully = false;

        return rc;
    }

    /* Do the access. */
    Log2(("#%d ahciMMIOWrite: pvUser=%p:{%.*Rhxs} cb=%d GCPhysAddr=%RGp\n", pDevIns->iInstance, pv, cb, pv, cb, off));
    return ahciRegisterWrite(pDevIns, pThis, off, *(uint32_t const *)pv);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 * Fake IDE port handler provided to make solaris happy.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
ahciLegacyFakeWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(pDevIns, pvUser, offPort, u32, cb);
    ASSERT_GUEST_MSG_FAILED(("Should not happen\n"));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,
 * Fake IDE port handler provided to make solaris happy.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
ahciLegacyFakeRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    *pu32 = 0;
    RT_NOREF(pDevIns, pvUser, offPort, cb);
    ASSERT_GUEST_MSG_FAILED(("Should not happen\n"));
    return VERR_IOM_IOPORT_UNUSED;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 * I/O port handler for writes to the index/data register pair.}
 */
static DECLCALLBACK(VBOXSTRICTRC) ahciIdxDataWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PAHCI           pThis = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    VBOXSTRICTRC    rc = VINF_SUCCESS;
    RT_NOREF(pvUser, cb);

    if (offPort >= 8)
    {
        ASSERT_GUEST(cb == 4);

        uint32_t const iReg = (offPort - 8) / 4;
        if (iReg == 0)
        {
            /* Write the index register. */
            pThis->regIdx = u32;
        }
        else
        {
            /** @todo range check? */
            ASSERT_GUEST(iReg == 1);
            rc = ahciRegisterWrite(pDevIns, pThis, pThis->regIdx, u32);
            if (rc == VINF_IOM_R3_MMIO_WRITE)
                rc = VINF_IOM_R3_IOPORT_WRITE;
        }
    }
    /* else: ignore */

    Log2(("#%d ahciIdxDataWrite: pu32=%p:{%.*Rhxs} cb=%d offPort=%#x rc=%Rrc\n",
          pDevIns->iInstance, &u32, cb, &u32, cb, offPort, VBOXSTRICTRC_VAL(rc)));
    return rc;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 * I/O port handler for reads from the index/data register pair.}
 */
static DECLCALLBACK(VBOXSTRICTRC) ahciIdxDataRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PAHCI           pThis = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    VBOXSTRICTRC    rc = VINF_SUCCESS;
    RT_NOREF(pvUser);

    if (offPort >= 8)
    {
        ASSERT_GUEST(cb == 4);

        uint32_t const iReg = (offPort - 8) / 4;
        if (iReg == 0)
        {
            /* Read the index register. */
            *pu32 = pThis->regIdx;
        }
        else
        {
            /** @todo range check? */
            ASSERT_GUEST(iReg == 1);
            rc = ahciRegisterRead(pDevIns, pThis, pThis->regIdx, pu32, cb);
            if (rc == VINF_IOM_R3_MMIO_READ)
                rc = VINF_IOM_R3_IOPORT_READ;
            else if (rc == VINF_IOM_MMIO_UNUSED_00)
                rc = VERR_IOM_IOPORT_UNUSED;
        }
    }
    else
        *pu32 = UINT32_MAX;

    Log2(("#%d ahciIdxDataRead: pu32=%p:{%.*Rhxs} cb=%d offPort=%#x rc=%Rrc\n",
          pDevIns->iInstance, pu32, cb, pu32, cb, offPort, VBOXSTRICTRC_VAL(rc)));
    return rc;
}

#ifdef IN_RING3

/* -=-=-=-=-=- PAHCI::ILeds  -=-=-=-=-=- */

/**
 * Gets the pointer to the status LED of a unit.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   iLUN            The unit which status LED we desire.
 * @param   ppLed           Where to store the LED pointer.
 */
static DECLCALLBACK(int) ahciR3Status_QueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    PAHCICC pThisCC = RT_FROM_MEMBER(pInterface, AHCICC, ILeds);
    if (iLUN < AHCI_MAX_NR_PORTS_IMPL)
    {
        PAHCI pThis = PDMDEVINS_2_DATA(pThisCC->pDevIns, PAHCI);
        *ppLed = &pThis->aPorts[iLUN].Led;
        Assert((*ppLed)->u32Magic == PDMLED_MAGIC);
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) ahciR3Status_QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PAHCICC pThisCC = RT_FROM_MEMBER(pInterface, AHCICC, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDPORTS, &pThisCC->ILeds);
    return NULL;
}

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) ahciR3PortQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PAHCIPORTR3 pAhciPortR3 = RT_FROM_MEMBER(pInterface, AHCIPORTR3, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pAhciPortR3->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAPORT, &pAhciPortR3->IPort);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAEXPORT, &pAhciPortR3->IMediaExPort);
    return NULL;
}

/**
 * @interface_method_impl{PDMIMEDIAPORT,pfnQueryDeviceLocation}
 */
static DECLCALLBACK(int) ahciR3PortQueryDeviceLocation(PPDMIMEDIAPORT pInterface, const char **ppcszController,
                                                       uint32_t *piInstance, uint32_t *piLUN)
{
    PAHCIPORTR3 pAhciPortR3 = RT_FROM_MEMBER(pInterface, AHCIPORTR3, IPort);
    PPDMDEVINS  pDevIns     = pAhciPortR3->pDevIns;

    AssertPtrReturn(ppcszController, VERR_INVALID_POINTER);
    AssertPtrReturn(piInstance, VERR_INVALID_POINTER);
    AssertPtrReturn(piLUN, VERR_INVALID_POINTER);

    *ppcszController = pDevIns->pReg->szName;
    *piInstance = pDevIns->iInstance;
    *piLUN = pAhciPortR3->iLUN;

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAPORT,pfnQueryScsiInqStrings}
 */
static DECLCALLBACK(int) ahciR3PortQueryScsiInqStrings(PPDMIMEDIAPORT pInterface, const char **ppszVendorId,
                                                       const char **ppszProductId, const char **ppszRevision)
{
    PAHCIPORTR3 pAhciPortR3 = RT_FROM_MEMBER(pInterface, AHCIPORTR3, IPort);
    PAHCI       pThis       = PDMDEVINS_2_DATA(pAhciPortR3->pDevIns, PAHCI);
    PAHCIPORT   pAhciPort   = &RT_SAFE_SUBSCRIPT(pThis->aPorts, pAhciPortR3->iLUN);

    if (ppszVendorId)
        *ppszVendorId = &pAhciPort->szInquiryVendorId[0];
    if (ppszProductId)
        *ppszProductId = &pAhciPort->szInquiryProductId[0];
    if (ppszRevision)
        *ppszRevision = &pAhciPort->szInquiryRevision[0];
    return VINF_SUCCESS;
}

#ifdef LOG_ENABLED

/**
 * Dump info about the FIS
 *
 * @param   pAhciPort     The port the command FIS was read from (shared bits).
 * @param   cmdFis        The FIS to print info from.
 */
static void ahciDumpFisInfo(PAHCIPORT pAhciPort, uint8_t *cmdFis)
{
    ahciLog(("%s: *** Begin FIS info dump. ***\n", __FUNCTION__));
    /* Print FIS type. */
    switch (cmdFis[AHCI_CMDFIS_TYPE])
    {
        case AHCI_CMDFIS_TYPE_H2D:
        {
            ahciLog(("%s: Command Fis type: H2D\n", __FUNCTION__));
            ahciLog(("%s: Command Fis size: %d bytes\n", __FUNCTION__, AHCI_CMDFIS_TYPE_H2D_SIZE));
            if (cmdFis[AHCI_CMDFIS_BITS] & AHCI_CMDFIS_C)
                ahciLog(("%s: Command register update\n", __FUNCTION__));
            else
                ahciLog(("%s: Control register update\n", __FUNCTION__));
            ahciLog(("%s: CMD=%#04x \"%s\"\n", __FUNCTION__, cmdFis[AHCI_CMDFIS_CMD], ATACmdText(cmdFis[AHCI_CMDFIS_CMD])));
            ahciLog(("%s: FEAT=%#04x\n", __FUNCTION__, cmdFis[AHCI_CMDFIS_FET]));
            ahciLog(("%s: SECTN=%#04x\n", __FUNCTION__, cmdFis[AHCI_CMDFIS_SECTN]));
            ahciLog(("%s: CYLL=%#04x\n", __FUNCTION__, cmdFis[AHCI_CMDFIS_CYLL]));
            ahciLog(("%s: CYLH=%#04x\n", __FUNCTION__, cmdFis[AHCI_CMDFIS_CYLH]));
            ahciLog(("%s: HEAD=%#04x\n", __FUNCTION__, cmdFis[AHCI_CMDFIS_HEAD]));

            ahciLog(("%s: SECTNEXP=%#04x\n", __FUNCTION__, cmdFis[AHCI_CMDFIS_SECTNEXP]));
            ahciLog(("%s: CYLLEXP=%#04x\n", __FUNCTION__, cmdFis[AHCI_CMDFIS_CYLLEXP]));
            ahciLog(("%s: CYLHEXP=%#04x\n", __FUNCTION__, cmdFis[AHCI_CMDFIS_CYLHEXP]));
            ahciLog(("%s: FETEXP=%#04x\n", __FUNCTION__, cmdFis[AHCI_CMDFIS_FETEXP]));

            ahciLog(("%s: SECTC=%#04x\n", __FUNCTION__, cmdFis[AHCI_CMDFIS_SECTC]));
            ahciLog(("%s: SECTCEXP=%#04x\n", __FUNCTION__, cmdFis[AHCI_CMDFIS_SECTCEXP]));
            ahciLog(("%s: CTL=%#04x\n", __FUNCTION__, cmdFis[AHCI_CMDFIS_CTL]));
            if (cmdFis[AHCI_CMDFIS_CTL] & AHCI_CMDFIS_CTL_SRST)
                ahciLog(("%s: Reset bit is set\n", __FUNCTION__));
            break;
        }
        case AHCI_CMDFIS_TYPE_D2H:
        {
            ahciLog(("%s: Command Fis type D2H\n", __FUNCTION__));
            ahciLog(("%s: Command Fis size: %d\n", __FUNCTION__, AHCI_CMDFIS_TYPE_D2H_SIZE));
            break;
        }
        case AHCI_CMDFIS_TYPE_SETDEVBITS:
        {
            ahciLog(("%s: Command Fis type Set Device Bits\n", __FUNCTION__));
            ahciLog(("%s: Command Fis size: %d\n", __FUNCTION__, AHCI_CMDFIS_TYPE_SETDEVBITS_SIZE));
            break;
        }
        case AHCI_CMDFIS_TYPE_DMAACTD2H:
        {
            ahciLog(("%s: Command Fis type DMA Activate H2D\n", __FUNCTION__));
            ahciLog(("%s: Command Fis size: %d\n", __FUNCTION__, AHCI_CMDFIS_TYPE_DMAACTD2H_SIZE));
            break;
        }
        case AHCI_CMDFIS_TYPE_DMASETUP:
        {
            ahciLog(("%s: Command Fis type DMA Setup\n", __FUNCTION__));
            ahciLog(("%s: Command Fis size: %d\n", __FUNCTION__, AHCI_CMDFIS_TYPE_DMASETUP_SIZE));
            break;
        }
        case AHCI_CMDFIS_TYPE_PIOSETUP:
        {
            ahciLog(("%s: Command Fis type PIO Setup\n", __FUNCTION__));
            ahciLog(("%s: Command Fis size: %d\n", __FUNCTION__, AHCI_CMDFIS_TYPE_PIOSETUP_SIZE));
            break;
        }
        case AHCI_CMDFIS_TYPE_DATA:
        {
            ahciLog(("%s: Command Fis type Data\n", __FUNCTION__));
            break;
        }
        default:
            ahciLog(("%s: ERROR Unknown command FIS type\n", __FUNCTION__));
            break;
    }
    ahciLog(("%s: *** End FIS info dump. ***\n", __FUNCTION__));
}

/**
 * Dump info about the command header
 *
 * @param   pAhciPort   Pointer to the port the command header was read from
 *                      (shared bits).
 * @param   pCmdHdr     The command header to print info from.
 */
static void ahciDumpCmdHdrInfo(PAHCIPORT pAhciPort, CmdHdr *pCmdHdr)
{
    ahciLog(("%s: *** Begin command header info dump. ***\n", __FUNCTION__));
    ahciLog(("%s: Number of Scatter/Gatther List entries: %u\n", __FUNCTION__, AHCI_CMDHDR_PRDTL_ENTRIES(pCmdHdr->u32DescInf)));
    if (pCmdHdr->u32DescInf & AHCI_CMDHDR_C)
        ahciLog(("%s: Clear busy upon R_OK\n", __FUNCTION__));
    if (pCmdHdr->u32DescInf & AHCI_CMDHDR_B)
        ahciLog(("%s: BIST Fis\n", __FUNCTION__));
    if (pCmdHdr->u32DescInf & AHCI_CMDHDR_R)
        ahciLog(("%s: Device Reset Fis\n", __FUNCTION__));
    if (pCmdHdr->u32DescInf & AHCI_CMDHDR_P)
        ahciLog(("%s: Command prefetchable\n", __FUNCTION__));
    if (pCmdHdr->u32DescInf & AHCI_CMDHDR_W)
        ahciLog(("%s: Device write\n", __FUNCTION__));
    else
        ahciLog(("%s: Device read\n", __FUNCTION__));
    if (pCmdHdr->u32DescInf & AHCI_CMDHDR_A)
        ahciLog(("%s: ATAPI command\n", __FUNCTION__));
    else
        ahciLog(("%s: ATA command\n", __FUNCTION__));

    ahciLog(("%s: Command FIS length %u DW\n", __FUNCTION__, (pCmdHdr->u32DescInf & AHCI_CMDHDR_CFL_MASK)));
    ahciLog(("%s: *** End command header info dump. ***\n", __FUNCTION__));
}

#endif /* LOG_ENABLED */

/**
 * Post the first D2H FIS from the device into guest memory.
 *
 * @param   pDevIns     The device instance.
 * @param   pAhciPort   Pointer to the port which "receives" the FIS (shared bits).
 */
static void ahciPostFirstD2HFisIntoMemory(PPDMDEVINS pDevIns, PAHCIPORT pAhciPort)
{
    uint8_t d2hFis[AHCI_CMDFIS_TYPE_D2H_SIZE];

    pAhciPort->fFirstD2HFisSent = true;

    ahciLog(("%s: Sending First D2H FIS from FIFO\n", __FUNCTION__));
    memset(&d2hFis[0], 0, sizeof(d2hFis));
    d2hFis[AHCI_CMDFIS_TYPE] = AHCI_CMDFIS_TYPE_D2H;
    d2hFis[AHCI_CMDFIS_ERR]  = 0x01;

    d2hFis[AHCI_CMDFIS_STS]  = 0x00;

    /* Set the signature based on the device type. */
    if (pAhciPort->fATAPI)
    {
        d2hFis[AHCI_CMDFIS_CYLL] = 0x14;
        d2hFis[AHCI_CMDFIS_CYLH] = 0xeb;
    }
    else
    {
        d2hFis[AHCI_CMDFIS_CYLL]  = 0x00;
        d2hFis[AHCI_CMDFIS_CYLH]  = 0x00;
    }

    d2hFis[AHCI_CMDFIS_HEAD]  = 0x00;
    d2hFis[AHCI_CMDFIS_SECTN] = 0x01;
    d2hFis[AHCI_CMDFIS_SECTC] = 0x01;

    pAhciPort->regTFD = (1 << 8) | ATA_STAT_SEEK | ATA_STAT_WRERR;
    if (!pAhciPort->fATAPI)
        pAhciPort->regTFD |= ATA_STAT_READY;

    ahciPostFisIntoMemory(pDevIns, pAhciPort, AHCI_CMDFIS_TYPE_D2H, d2hFis);
}

/**
 * Post the FIS in the memory area allocated by the guest and set interrupt if necessary.
 *
 * @returns VBox status code
 * @param   pDevIns     The device instance.
 * @param   pAhciPort  The port which "receives" the FIS(shared bits).
 * @param   uFisType   The type of the FIS.
 * @param   pCmdFis    Pointer to the FIS which is to be posted into memory.
 */
static int ahciPostFisIntoMemory(PPDMDEVINS pDevIns, PAHCIPORT pAhciPort, unsigned uFisType, uint8_t *pCmdFis)
{
    int         rc = VINF_SUCCESS;
    RTGCPHYS    GCPhysAddrRecFis = pAhciPort->GCPhysAddrFb;
    unsigned    cbFis = 0;

    ahciLog(("%s: pAhciPort=%p uFisType=%u pCmdFis=%p\n", __FUNCTION__, pAhciPort, uFisType, pCmdFis));

    if (pAhciPort->regCMD & AHCI_PORT_CMD_FRE)
    {
        AssertMsg(GCPhysAddrRecFis, ("%s: GCPhysAddrRecFis is 0\n", __FUNCTION__));

        /* Determine the offset and size of the FIS based on uFisType. */
        switch (uFisType)
        {
            case AHCI_CMDFIS_TYPE_D2H:
            {
                GCPhysAddrRecFis += AHCI_RECFIS_RFIS_OFFSET;
                cbFis = AHCI_CMDFIS_TYPE_D2H_SIZE;
                break;
            }
            case AHCI_CMDFIS_TYPE_SETDEVBITS:
            {
                GCPhysAddrRecFis += AHCI_RECFIS_SDBFIS_OFFSET;
                cbFis = AHCI_CMDFIS_TYPE_SETDEVBITS_SIZE;
                break;
            }
            case AHCI_CMDFIS_TYPE_DMASETUP:
            {
                GCPhysAddrRecFis += AHCI_RECFIS_DSFIS_OFFSET;
                cbFis = AHCI_CMDFIS_TYPE_DMASETUP_SIZE;
                break;
            }
            case AHCI_CMDFIS_TYPE_PIOSETUP:
            {
                GCPhysAddrRecFis += AHCI_RECFIS_PSFIS_OFFSET;
                cbFis = AHCI_CMDFIS_TYPE_PIOSETUP_SIZE;
                break;
            }
            default:
                /*
                 * We should post the unknown FIS into memory too but this never happens because
                 * we know which FIS types we generate. ;)
                 */
                AssertMsgFailed(("%s: Unknown FIS type!\n", __FUNCTION__));
        }

        /* Post the FIS into memory. */
        ahciLog(("%s: PDMDevHlpPCIPhysWrite GCPhysAddrRecFis=%RGp cbFis=%u\n", __FUNCTION__, GCPhysAddrRecFis, cbFis));
        PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhysAddrRecFis, pCmdFis, cbFis);
    }

    return rc;
}

DECLINLINE(void) ahciReqSetStatus(PAHCIREQ pAhciReq, uint8_t u8Error, uint8_t u8Status)
{
    pAhciReq->cmdFis[AHCI_CMDFIS_ERR] = u8Error;
    pAhciReq->cmdFis[AHCI_CMDFIS_STS] = u8Status;
}

static void ataPadString(uint8_t *pbDst, const char *pbSrc, uint32_t cbSize)
{
    for (uint32_t i = 0; i < cbSize; i++)
    {
        if (*pbSrc)
            pbDst[i ^ 1] = *pbSrc++;
        else
            pbDst[i ^ 1] = ' ';
    }
}

static uint32_t ataChecksum(void* ptr, size_t count)
{
    uint8_t u8Sum = 0xa5, *p = (uint8_t*)ptr;
    size_t i;

    for (i = 0; i < count; i++)
    {
      u8Sum += *p++;
    }

    return (uint8_t)-(int32_t)u8Sum;
}

static int ahciIdentifySS(PAHCI pThis, PAHCIPORT pAhciPort, PAHCIPORTR3 pAhciPortR3, void *pvBuf)
{
    uint16_t *p = (uint16_t *)pvBuf;
    memset(p, 0, 512);
    p[0] = RT_H2LE_U16(0x0040);
    p[1] = RT_H2LE_U16(RT_MIN(pAhciPort->PCHSGeometry.cCylinders, 16383));
    p[3] = RT_H2LE_U16(pAhciPort->PCHSGeometry.cHeads);
    /* Block size; obsolete, but required for the BIOS. */
    p[5] = RT_H2LE_U16(512);
    p[6] = RT_H2LE_U16(pAhciPort->PCHSGeometry.cSectors);
    ataPadString((uint8_t *)(p + 10), pAhciPort->szSerialNumber, AHCI_SERIAL_NUMBER_LENGTH); /* serial number */
    p[20] = RT_H2LE_U16(3); /* XXX: retired, cache type */
    p[21] = RT_H2LE_U16(512); /* XXX: retired, cache size in sectors */
    p[22] = RT_H2LE_U16(0); /* ECC bytes per sector */
    ataPadString((uint8_t *)(p + 23), pAhciPort->szFirmwareRevision, AHCI_FIRMWARE_REVISION_LENGTH); /* firmware version */
    ataPadString((uint8_t *)(p + 27), pAhciPort->szModelNumber, AHCI_MODEL_NUMBER_LENGTH); /* model */
#if ATA_MAX_MULT_SECTORS > 1
    p[47] = RT_H2LE_U16(0x8000 | ATA_MAX_MULT_SECTORS);
#endif
    p[48] = RT_H2LE_U16(1); /* dword I/O, used by the BIOS */
    p[49] = RT_H2LE_U16(1 << 11 | 1 << 9 | 1 << 8); /* DMA and LBA supported */
    p[50] = RT_H2LE_U16(1 << 14); /* No drive specific standby timer minimum */
    p[51] = RT_H2LE_U16(240); /* PIO transfer cycle */
    p[52] = RT_H2LE_U16(240); /* DMA transfer cycle */
    p[53] = RT_H2LE_U16(1 | 1 << 1 | 1 << 2); /* words 54-58,64-70,88 valid */
    p[54] = RT_H2LE_U16(RT_MIN(pAhciPort->PCHSGeometry.cCylinders, 16383));
    p[55] = RT_H2LE_U16(pAhciPort->PCHSGeometry.cHeads);
    p[56] = RT_H2LE_U16(pAhciPort->PCHSGeometry.cSectors);
    p[57] = RT_H2LE_U16(RT_MIN(pAhciPort->PCHSGeometry.cCylinders, 16383) * pAhciPort->PCHSGeometry.cHeads * pAhciPort->PCHSGeometry.cSectors);
    p[58] = RT_H2LE_U16(RT_MIN(pAhciPort->PCHSGeometry.cCylinders, 16383) * pAhciPort->PCHSGeometry.cHeads * pAhciPort->PCHSGeometry.cSectors >> 16);
    if (pAhciPort->cMultSectors)
        p[59] = RT_H2LE_U16(0x100 | pAhciPort->cMultSectors);
    if (pAhciPort->cTotalSectors <= (1 << 28) - 1)
    {
        p[60] = RT_H2LE_U16(pAhciPort->cTotalSectors);
        p[61] = RT_H2LE_U16(pAhciPort->cTotalSectors >> 16);
    }
    else
    {
        /* Report maximum number of sectors possible with LBA28 */
        p[60] = RT_H2LE_U16(((1 << 28) - 1) & 0xffff);
        p[61] = RT_H2LE_U16(((1 << 28) - 1) >> 16);
    }
    p[63] = RT_H2LE_U16(ATA_TRANSFER_ID(ATA_MODE_MDMA, ATA_MDMA_MODE_MAX, pAhciPort->uATATransferMode)); /* MDMA modes supported / mode enabled */
    p[64] = RT_H2LE_U16(ATA_PIO_MODE_MAX > 2 ? (1 << (ATA_PIO_MODE_MAX - 2)) - 1 : 0); /* PIO modes beyond PIO2 supported */
    p[65] = RT_H2LE_U16(120); /* minimum DMA multiword tx cycle time */
    p[66] = RT_H2LE_U16(120); /* recommended DMA multiword tx cycle time */
    p[67] = RT_H2LE_U16(120); /* minimum PIO cycle time without flow control */
    p[68] = RT_H2LE_U16(120); /* minimum PIO cycle time with IORDY flow control */
    if (   pAhciPort->fTrimEnabled
        || pAhciPort->cbSector != 512
        || pAhciPortR3->pDrvMedia->pfnIsNonRotational(pAhciPortR3->pDrvMedia))
    {
        p[80] = RT_H2LE_U16(0x1f0); /* support everything up to ATA/ATAPI-8 ACS */
        p[81] = RT_H2LE_U16(0x28); /* conforms to ATA/ATAPI-8 ACS */
    }
    else
    {
        p[80] = RT_H2LE_U16(0x7e); /* support everything up to ATA/ATAPI-6 */
        p[81] = RT_H2LE_U16(0x22); /* conforms to ATA/ATAPI-6 */
    }
    p[82] = RT_H2LE_U16(1 << 3 | 1 << 5 | 1 << 6); /* supports power management,  write cache and look-ahead */
    p[83] = RT_H2LE_U16(1 << 14 | 1 << 10 | 1 << 12 | 1 << 13); /* supports LBA48, FLUSH CACHE and FLUSH CACHE EXT */
    p[84] = RT_H2LE_U16(1 << 14);
    p[85] = RT_H2LE_U16(1 << 3 | 1 << 5 | 1 << 6); /* enabled power management,  write cache and look-ahead */
    p[86] = RT_H2LE_U16(1 << 10 | 1 << 12 | 1 << 13); /* enabled LBA48, FLUSH CACHE and FLUSH CACHE EXT */
    p[87] = RT_H2LE_U16(1 << 14);
    p[88] = RT_H2LE_U16(ATA_TRANSFER_ID(ATA_MODE_UDMA, ATA_UDMA_MODE_MAX, pAhciPort->uATATransferMode)); /* UDMA modes supported / mode enabled */
    p[93] = RT_H2LE_U16(0x00);
    p[100] = RT_H2LE_U16(pAhciPort->cTotalSectors);
    p[101] = RT_H2LE_U16(pAhciPort->cTotalSectors >> 16);
    p[102] = RT_H2LE_U16(pAhciPort->cTotalSectors >> 32);
    p[103] = RT_H2LE_U16(pAhciPort->cTotalSectors >> 48);

    /* valid information, more than one logical sector per physical sector, 2^cLogSectorsPerPhysicalExp logical sectors per physical sector */
    if (pAhciPort->cLogSectorsPerPhysicalExp)
        p[106] = RT_H2LE_U16(RT_BIT(14) | RT_BIT(13) | pAhciPort->cLogSectorsPerPhysicalExp);

    if (pAhciPort->cbSector != 512)
    {
        uint32_t cSectorSizeInWords = pAhciPort->cbSector / sizeof(uint16_t);
        /* Enable reporting of logical sector size. */
        p[106] |= RT_H2LE_U16(RT_BIT(12) | RT_BIT(14));
        p[117] = RT_H2LE_U16(cSectorSizeInWords);
        p[118] = RT_H2LE_U16(cSectorSizeInWords >> 16);
    }

    if (pAhciPortR3->pDrvMedia->pfnIsNonRotational(pAhciPortR3->pDrvMedia))
        p[217] = RT_H2LE_U16(1); /* Non-rotational medium */

    if (pAhciPort->fTrimEnabled) /** @todo Set bit 14 in word 69 too? (Deterministic read after TRIM). */
        p[169] = RT_H2LE_U16(1); /* DATA SET MANAGEMENT command supported. */

    /* The following are SATA specific */
    p[75] = RT_H2LE_U16(pThis->cCmdSlotsAvail - 1); /* Number of commands we support, 0's based */
    p[76] = RT_H2LE_U16((1 << 8) | (1 << 2)); /* Native command queuing and Serial ATA Gen2 (3.0 Gbps) speed supported */

    uint32_t uCsum = ataChecksum(p, 510);
    p[255] = RT_H2LE_U16(0xa5 | (uCsum << 8)); /* Integrity word */

    return VINF_SUCCESS;
}

static int ahciR3AtapiIdentify(PPDMDEVINS pDevIns, PAHCIREQ pAhciReq, PAHCIPORT pAhciPort, size_t cbData, size_t *pcbData)
{
    uint16_t p[256];

    memset(p, 0, 512);
    /* Removable CDROM, 50us response, 12 byte packets */
    p[0] = RT_H2LE_U16(2 << 14 | 5 << 8 | 1 << 7 | 2 << 5 | 0 << 0);
    ataPadString((uint8_t *)(p + 10), pAhciPort->szSerialNumber, AHCI_SERIAL_NUMBER_LENGTH); /* serial number */
    p[20] = RT_H2LE_U16(3); /* XXX: retired, cache type */
    p[21] = RT_H2LE_U16(512); /* XXX: retired, cache size in sectors */
    ataPadString((uint8_t *)(p + 23), pAhciPort->szFirmwareRevision, AHCI_FIRMWARE_REVISION_LENGTH); /* firmware version */
    ataPadString((uint8_t *)(p + 27), pAhciPort->szModelNumber, AHCI_MODEL_NUMBER_LENGTH); /* model */
    p[49] = RT_H2LE_U16(1 << 11 | 1 << 9 | 1 << 8); /* DMA and LBA supported */
    p[50] = RT_H2LE_U16(1 << 14);  /* No drive specific standby timer minimum */
    p[51] = RT_H2LE_U16(240); /* PIO transfer cycle */
    p[52] = RT_H2LE_U16(240); /* DMA transfer cycle */
    p[53] = RT_H2LE_U16(1 << 1 | 1 << 2); /* words 64-70,88 are valid */
    p[63] = RT_H2LE_U16(ATA_TRANSFER_ID(ATA_MODE_MDMA, ATA_MDMA_MODE_MAX, pAhciPort->uATATransferMode)); /* MDMA modes supported / mode enabled */
    p[64] = RT_H2LE_U16(ATA_PIO_MODE_MAX > 2 ? (1 << (ATA_PIO_MODE_MAX - 2)) - 1 : 0); /* PIO modes beyond PIO2 supported */
    p[65] = RT_H2LE_U16(120); /* minimum DMA multiword tx cycle time */
    p[66] = RT_H2LE_U16(120); /* recommended DMA multiword tx cycle time */
    p[67] = RT_H2LE_U16(120); /* minimum PIO cycle time without flow control */
    p[68] = RT_H2LE_U16(120); /* minimum PIO cycle time with IORDY flow control */
    p[73] = RT_H2LE_U16(0x003e); /* ATAPI CDROM major */
    p[74] = RT_H2LE_U16(9); /* ATAPI CDROM minor */
    p[80] = RT_H2LE_U16(0x7e); /* support everything up to ATA/ATAPI-6 */
    p[81] = RT_H2LE_U16(0x22); /* conforms to ATA/ATAPI-6 */
    p[82] = RT_H2LE_U16(1 << 4 | 1 << 9); /* supports packet command set and DEVICE RESET */
    p[83] = RT_H2LE_U16(1 << 14);
    p[84] = RT_H2LE_U16(1 << 14);
    p[85] = RT_H2LE_U16(1 << 4 | 1 << 9); /* enabled packet command set and DEVICE RESET */
    p[86] = RT_H2LE_U16(0);
    p[87] = RT_H2LE_U16(1 << 14);
    p[88] = RT_H2LE_U16(ATA_TRANSFER_ID(ATA_MODE_UDMA, ATA_UDMA_MODE_MAX, pAhciPort->uATATransferMode)); /* UDMA modes supported / mode enabled */
    p[93] = RT_H2LE_U16((1 | 1 << 1) << ((pAhciPort->iLUN & 1) == 0 ? 0 : 8) | 1 << 13 | 1 << 14);

    /* The following are SATA specific */
    p[75] = RT_H2LE_U16(31); /* We support 32 commands */
    p[76] = RT_H2LE_U16((1 << 8) | (1 << 2)); /* Native command queuing and Serial ATA Gen2 (3.0 Gbps) speed supported */

    /* Copy the buffer in to the scatter gather list. */
    *pcbData = ahciR3CopyBufferToPrdtl(pDevIns, pAhciReq, (void *)&p[0], RT_MIN(cbData, sizeof(p)), 0 /* cbSkip */);
    return VINF_SUCCESS;
}

/**
 * Reset all values after a reset of the attached storage device.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AHCI state.
 * @param   pAhciPort   The port the device is attached to, shared bits(shared
 *                      bits).
 * @param   pAhciReq    The state to get the tag number from.
 */
static void ahciFinishStorageDeviceReset(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, PAHCIREQ pAhciReq)
{
    int rc;

    /* Send a status good D2H FIS. */
    pAhciPort->fResetDevice = false;
    if (pAhciPort->regCMD & AHCI_PORT_CMD_FRE)
        ahciPostFirstD2HFisIntoMemory(pDevIns, pAhciPort);

    /* As this is the first D2H FIS after the reset update the signature in the SIG register of the port. */
    if (pAhciPort->fATAPI)
        pAhciPort->regSIG = AHCI_PORT_SIG_ATAPI;
    else
        pAhciPort->regSIG = AHCI_PORT_SIG_DISK;
    ASMAtomicOrU32(&pAhciPort->u32TasksFinished, (1 << pAhciReq->uTag));

    rc = ahciHbaSetInterrupt(pDevIns, pThis, pAhciPort->iLUN, VERR_IGNORED);
    AssertRC(rc);
}

/**
 * Initiates a device reset caused by ATA_DEVICE_RESET (ATAPI only).
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AHCI state.
 * @param   pAhciPort   The device to reset(shared bits).
 * @param   pAhciReq    The task state.
 */
static void ahciDeviceReset(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, PAHCIREQ pAhciReq)
{
    ASMAtomicWriteBool(&pAhciPort->fResetDevice, true);

    /*
     * Because this ATAPI only and ATAPI can't have
     * more than one command active at a time the task counter should be 0
     * and it is possible to finish the reset now.
     */
    Assert(ASMAtomicReadU32(&pAhciPort->cTasksActive) == 0);
    ahciFinishStorageDeviceReset(pDevIns, pThis, pAhciPort, pAhciReq);
}

/**
 * Create a PIO setup FIS and post it into the memory area of the guest.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AHCI state.
 * @param   pAhciPort   The port of the SATA controller (shared bits).
 * @param   cbTransfer  Transfer size of the request.
 * @param   pCmdFis     Pointer to the command FIS from the guest.
 * @param   fRead       Flag whether this is a read request.
 * @param   fInterrupt  If an interrupt should be send to the guest.
 */
static void ahciSendPioSetupFis(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort,
                                size_t cbTransfer, uint8_t *pCmdFis, bool fRead, bool fInterrupt)
{
    uint8_t abPioSetupFis[20];
    bool fAssertIntr = false;

    ahciLog(("%s: building PIO setup Fis\n", __FUNCTION__));

    AssertMsg(   cbTransfer > 0
              && cbTransfer <= 65534,
              ("Can't send PIO setup FIS for requests with 0 bytes to transfer or greater than 65534\n"));

    if (pAhciPort->regCMD & AHCI_PORT_CMD_FRE)
    {
        memset(&abPioSetupFis[0], 0, sizeof(abPioSetupFis));
        abPioSetupFis[AHCI_CMDFIS_TYPE]  = AHCI_CMDFIS_TYPE_PIOSETUP;
        abPioSetupFis[AHCI_CMDFIS_BITS]  = (fInterrupt ? AHCI_CMDFIS_I : 0);
        if (fRead)
            abPioSetupFis[AHCI_CMDFIS_BITS] |= AHCI_CMDFIS_D;
        abPioSetupFis[AHCI_CMDFIS_STS]   = pCmdFis[AHCI_CMDFIS_STS];
        abPioSetupFis[AHCI_CMDFIS_ERR]   = pCmdFis[AHCI_CMDFIS_ERR];
        abPioSetupFis[AHCI_CMDFIS_SECTN] = pCmdFis[AHCI_CMDFIS_SECTN];
        abPioSetupFis[AHCI_CMDFIS_CYLL]  = pCmdFis[AHCI_CMDFIS_CYLL];
        abPioSetupFis[AHCI_CMDFIS_CYLH]  = pCmdFis[AHCI_CMDFIS_CYLH];
        abPioSetupFis[AHCI_CMDFIS_HEAD]  = pCmdFis[AHCI_CMDFIS_HEAD];
        abPioSetupFis[AHCI_CMDFIS_SECTNEXP] = pCmdFis[AHCI_CMDFIS_SECTNEXP];
        abPioSetupFis[AHCI_CMDFIS_CYLLEXP]  = pCmdFis[AHCI_CMDFIS_CYLLEXP];
        abPioSetupFis[AHCI_CMDFIS_CYLHEXP]  = pCmdFis[AHCI_CMDFIS_CYLHEXP];
        abPioSetupFis[AHCI_CMDFIS_SECTC]    = pCmdFis[AHCI_CMDFIS_SECTC];
        abPioSetupFis[AHCI_CMDFIS_SECTCEXP] = pCmdFis[AHCI_CMDFIS_SECTCEXP];

        /* Set transfer count. */
        abPioSetupFis[16] = (cbTransfer >> 8) & 0xff;
        abPioSetupFis[17] = cbTransfer & 0xff;

        /* Update registers. */
        pAhciPort->regTFD = (pCmdFis[AHCI_CMDFIS_ERR] << 8) | pCmdFis[AHCI_CMDFIS_STS];

        ahciPostFisIntoMemory(pDevIns, pAhciPort, AHCI_CMDFIS_TYPE_PIOSETUP, abPioSetupFis);

        if (fInterrupt)
        {
            ASMAtomicOrU32(&pAhciPort->regIS, AHCI_PORT_IS_PSS);
            /* Check if we should assert an interrupt */
            if (pAhciPort->regIE & AHCI_PORT_IE_PSE)
                fAssertIntr = true;
        }

        if (fAssertIntr)
        {
            int rc = ahciHbaSetInterrupt(pDevIns, pThis, pAhciPort->iLUN, VERR_IGNORED);
            AssertRC(rc);
        }
    }
}

/**
 * Build a D2H FIS and post into the memory area of the guest.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AHCI state.
 * @param   pAhciPort   The port of the SATA controller (shared bits).
 * @param   uTag        The tag of the request.
 * @param   pCmdFis     Pointer to the command FIS from the guest.
 * @param   fInterrupt  If an interrupt should be send to the guest.
 */
static void ahciSendD2HFis(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, uint32_t uTag, uint8_t *pCmdFis, bool fInterrupt)
{
    uint8_t d2hFis[20];
    bool fAssertIntr = false;

    ahciLog(("%s: building D2H Fis\n", __FUNCTION__));

    if (pAhciPort->regCMD & AHCI_PORT_CMD_FRE)
    {
        memset(&d2hFis[0], 0, sizeof(d2hFis));
        d2hFis[AHCI_CMDFIS_TYPE]  = AHCI_CMDFIS_TYPE_D2H;
        d2hFis[AHCI_CMDFIS_BITS]  = (fInterrupt ? AHCI_CMDFIS_I : 0);
        d2hFis[AHCI_CMDFIS_STS]   = pCmdFis[AHCI_CMDFIS_STS];
        d2hFis[AHCI_CMDFIS_ERR]   = pCmdFis[AHCI_CMDFIS_ERR];
        d2hFis[AHCI_CMDFIS_SECTN] = pCmdFis[AHCI_CMDFIS_SECTN];
        d2hFis[AHCI_CMDFIS_CYLL]  = pCmdFis[AHCI_CMDFIS_CYLL];
        d2hFis[AHCI_CMDFIS_CYLH]  = pCmdFis[AHCI_CMDFIS_CYLH];
        d2hFis[AHCI_CMDFIS_HEAD]  = pCmdFis[AHCI_CMDFIS_HEAD];
        d2hFis[AHCI_CMDFIS_SECTNEXP] = pCmdFis[AHCI_CMDFIS_SECTNEXP];
        d2hFis[AHCI_CMDFIS_CYLLEXP]  = pCmdFis[AHCI_CMDFIS_CYLLEXP];
        d2hFis[AHCI_CMDFIS_CYLHEXP]  = pCmdFis[AHCI_CMDFIS_CYLHEXP];
        d2hFis[AHCI_CMDFIS_SECTC]    = pCmdFis[AHCI_CMDFIS_SECTC];
        d2hFis[AHCI_CMDFIS_SECTCEXP] = pCmdFis[AHCI_CMDFIS_SECTCEXP];

        /* Update registers. */
        pAhciPort->regTFD = (pCmdFis[AHCI_CMDFIS_ERR] << 8) | pCmdFis[AHCI_CMDFIS_STS];

        ahciPostFisIntoMemory(pDevIns, pAhciPort, AHCI_CMDFIS_TYPE_D2H, d2hFis);

        if (pCmdFis[AHCI_CMDFIS_STS] & ATA_STAT_ERR)
        {
            /* Error bit is set. */
            ASMAtomicOrU32(&pAhciPort->regIS, AHCI_PORT_IS_TFES);
            if (pAhciPort->regIE & AHCI_PORT_IE_TFEE)
                fAssertIntr = true;
            /*
             * Don't mark the command slot as completed because the guest
             * needs it to identify the failed command.
             */
        }
        else if (fInterrupt)
        {
            ASMAtomicOrU32(&pAhciPort->regIS, AHCI_PORT_IS_DHRS);
            /* Check if we should assert an interrupt */
            if (pAhciPort->regIE & AHCI_PORT_IE_DHRE)
                fAssertIntr = true;

            /* Mark command as completed. */
            ASMAtomicOrU32(&pAhciPort->u32TasksFinished, RT_BIT_32(uTag));
        }

        if (fAssertIntr)
        {
            int rc = ahciHbaSetInterrupt(pDevIns, pThis, pAhciPort->iLUN, VERR_IGNORED);
            AssertRC(rc);
        }
    }
}

/**
 * Build a SDB Fis and post it into the memory area of the guest.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared AHCI state.
 * @param   pAhciPort       The port for which the SDB Fis is send, shared bits.
 * @param   pAhciPortR3     The port for which the SDB Fis is send, ring-3 bits.
 * @param   uFinishedTasks  Bitmask of finished tasks.
 * @param   fInterrupt      If an interrupt should be asserted.
 */
static void ahciSendSDBFis(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, PAHCIPORTR3 pAhciPortR3,
                           uint32_t uFinishedTasks, bool fInterrupt)
{
    uint32_t sdbFis[2];
    bool fAssertIntr = false;
    PAHCIREQ pTaskErr = ASMAtomicReadPtrT(&pAhciPortR3->pTaskErr, PAHCIREQ);

    ahciLog(("%s: Building SDB FIS\n", __FUNCTION__));

    if (pAhciPort->regCMD & AHCI_PORT_CMD_FRE)
    {
        memset(&sdbFis[0], 0, sizeof(sdbFis));
        sdbFis[0] = AHCI_CMDFIS_TYPE_SETDEVBITS;
        sdbFis[0] |= (fInterrupt ? (1 << 14) : 0);
        if (RT_UNLIKELY(pTaskErr))
        {
            sdbFis[0]  = pTaskErr->cmdFis[AHCI_CMDFIS_ERR];
            sdbFis[0] |= (pTaskErr->cmdFis[AHCI_CMDFIS_STS] & 0x77) << 16; /* Some bits are marked as reserved and thus are masked out. */

            /* Update registers. */
            pAhciPort->regTFD = (pTaskErr->cmdFis[AHCI_CMDFIS_ERR] << 8) | pTaskErr->cmdFis[AHCI_CMDFIS_STS];
        }
        else
        {
            sdbFis[0]  = 0;
            sdbFis[0] |= (ATA_STAT_READY | ATA_STAT_SEEK) << 16;
            pAhciPort->regTFD = ATA_STAT_READY | ATA_STAT_SEEK;
        }

        sdbFis[1] = pAhciPort->u32QueuedTasksFinished | uFinishedTasks;

        ahciPostFisIntoMemory(pDevIns, pAhciPort, AHCI_CMDFIS_TYPE_SETDEVBITS, (uint8_t *)sdbFis);

        if (RT_UNLIKELY(pTaskErr))
        {
            /* Error bit is set. */
            ASMAtomicOrU32(&pAhciPort->regIS, AHCI_PORT_IS_TFES);
            if (pAhciPort->regIE & AHCI_PORT_IE_TFEE)
                fAssertIntr = true;
        }

        if (fInterrupt)
        {
            ASMAtomicOrU32(&pAhciPort->regIS, AHCI_PORT_IS_SDBS);
            /* Check if we should assert an interrupt */
            if (pAhciPort->regIE & AHCI_PORT_IE_SDBE)
                fAssertIntr = true;
        }

        ASMAtomicOrU32(&pAhciPort->u32QueuedTasksFinished, uFinishedTasks);

        if (fAssertIntr)
        {
            int rc = ahciHbaSetInterrupt(pDevIns, pThis, pAhciPort->iLUN, VERR_IGNORED);
            AssertRC(rc);
        }
    }
}

static uint32_t ahciGetNSectors(uint8_t *pCmdFis, bool fLBA48)
{
    /* 0 means either 256 (LBA28) or 65536 (LBA48) sectors. */
    if (fLBA48)
    {
        if (!pCmdFis[AHCI_CMDFIS_SECTC] && !pCmdFis[AHCI_CMDFIS_SECTCEXP])
            return 65536;
        else
            return pCmdFis[AHCI_CMDFIS_SECTCEXP] << 8 | pCmdFis[AHCI_CMDFIS_SECTC];
    }
    else
    {
        if (!pCmdFis[AHCI_CMDFIS_SECTC])
            return 256;
        else
            return pCmdFis[AHCI_CMDFIS_SECTC];
    }
}

static uint64_t ahciGetSector(PAHCIPORT pAhciPort, uint8_t *pCmdFis, bool fLBA48)
{
    uint64_t iLBA;
    if (pCmdFis[AHCI_CMDFIS_HEAD] & 0x40)
    {
        /* any LBA variant */
        if (fLBA48)
        {
            /* LBA48 */
            iLBA = ((uint64_t)pCmdFis[AHCI_CMDFIS_CYLHEXP] << 40) |
                ((uint64_t)pCmdFis[AHCI_CMDFIS_CYLLEXP] << 32) |
                ((uint64_t)pCmdFis[AHCI_CMDFIS_SECTNEXP] << 24) |
                ((uint64_t)pCmdFis[AHCI_CMDFIS_CYLH] << 16) |
                ((uint64_t)pCmdFis[AHCI_CMDFIS_CYLL] << 8) |
                pCmdFis[AHCI_CMDFIS_SECTN];
        }
        else
        {
            /* LBA */
            iLBA = ((pCmdFis[AHCI_CMDFIS_HEAD] & 0x0f) << 24) | (pCmdFis[AHCI_CMDFIS_CYLH] << 16) |
                (pCmdFis[AHCI_CMDFIS_CYLL] << 8) | pCmdFis[AHCI_CMDFIS_SECTN];
        }
    }
    else
    {
        /* CHS */
        iLBA = ((pCmdFis[AHCI_CMDFIS_CYLH] << 8) | pCmdFis[AHCI_CMDFIS_CYLL]) * pAhciPort->PCHSGeometry.cHeads * pAhciPort->PCHSGeometry.cSectors +
            (pCmdFis[AHCI_CMDFIS_HEAD] & 0x0f) * pAhciPort->PCHSGeometry.cSectors +
            (pCmdFis[AHCI_CMDFIS_SECTN] - 1);
    }
    return iLBA;
}

static uint64_t ahciGetSectorQueued(uint8_t *pCmdFis)
{
    uint64_t uLBA;

    uLBA = ((uint64_t)pCmdFis[AHCI_CMDFIS_CYLHEXP] << 40) |
           ((uint64_t)pCmdFis[AHCI_CMDFIS_CYLLEXP] << 32) |
           ((uint64_t)pCmdFis[AHCI_CMDFIS_SECTNEXP] << 24) |
           ((uint64_t)pCmdFis[AHCI_CMDFIS_CYLH] << 16) |
           ((uint64_t)pCmdFis[AHCI_CMDFIS_CYLL] << 8) |
           pCmdFis[AHCI_CMDFIS_SECTN];

    return uLBA;
}

DECLINLINE(uint32_t) ahciGetNSectorsQueued(uint8_t *pCmdFis)
{
    if (!pCmdFis[AHCI_CMDFIS_FETEXP] && !pCmdFis[AHCI_CMDFIS_FET])
        return 65536;
    else
        return pCmdFis[AHCI_CMDFIS_FETEXP] << 8 | pCmdFis[AHCI_CMDFIS_FET];
}

/**
 * Copy from guest to host memory worker.
 *
 * @copydoc FNAHCIR3MEMCOPYCALLBACK
 */
static DECLCALLBACK(void) ahciR3CopyBufferFromGuestWorker(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, PRTSGBUF pSgBuf,
                                                          size_t cbCopy, size_t *pcbSkip)
{
    size_t cbSkipped = RT_MIN(cbCopy, *pcbSkip);
    cbCopy   -= cbSkipped;
    GCPhys   += cbSkipped;
    *pcbSkip -= cbSkipped;

    while (cbCopy)
    {
        size_t cbSeg = cbCopy;
        void *pvSeg = RTSgBufGetNextSegment(pSgBuf, &cbSeg);

        AssertPtr(pvSeg);
        Log5Func(("%RGp LB %#zx\n", GCPhys, cbSeg));
        PDMDevHlpPCIPhysRead(pDevIns, GCPhys, pvSeg, cbSeg);
        Log7Func(("%.*Rhxd\n", cbSeg, pvSeg));
        GCPhys += cbSeg;
        cbCopy -= cbSeg;
    }
}

/**
 * Copy from host to guest memory worker.
 *
 * @copydoc FNAHCIR3MEMCOPYCALLBACK
 */
static DECLCALLBACK(void) ahciR3CopyBufferToGuestWorker(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, PRTSGBUF pSgBuf,
                                                        size_t cbCopy, size_t *pcbSkip)
{
    size_t cbSkipped = RT_MIN(cbCopy, *pcbSkip);
    cbCopy   -= cbSkipped;
    GCPhys   += cbSkipped;
    *pcbSkip -= cbSkipped;

    while (cbCopy)
    {
        size_t cbSeg = cbCopy;
        void *pvSeg = RTSgBufGetNextSegment(pSgBuf, &cbSeg);

        AssertPtr(pvSeg);
        Log5Func(("%RGp LB %#zx\n", GCPhys, cbSeg));
        Log6Func(("%.*Rhxd\n", cbSeg, pvSeg));
        PDMDevHlpPCIPhysWriteUser(pDevIns, GCPhys, pvSeg, cbSeg);
        GCPhys += cbSeg;
        cbCopy -= cbSeg;
    }
}

/**
 * Walks the PRDTL list copying data between the guest and host memory buffers.
 *
 * @returns Amount of bytes copied.
 * @param   pDevIns         The device instance.
 * @param   pAhciReq        AHCI request structure.
 * @param   pfnCopyWorker   The copy method to apply for each guest buffer.
 * @param   pSgBuf          The host S/G buffer.
 * @param   cbSkip          How many bytes to skip in advance before starting to copy.
 * @param   cbCopy          How many bytes to copy.
 */
static size_t ahciR3PrdtlWalk(PPDMDEVINS pDevIns, PAHCIREQ pAhciReq,
                              PFNAHCIR3MEMCOPYCALLBACK pfnCopyWorker,
                              PRTSGBUF pSgBuf, size_t cbSkip, size_t cbCopy)
{
    RTGCPHYS GCPhysPrdtl = pAhciReq->GCPhysPrdtl;
    unsigned cPrdtlEntries = pAhciReq->cPrdtlEntries;
    size_t cbCopied = 0;

    /*
     * Add the amount to skip to the host buffer size to avoid a
     * few conditionals later on.
     */
    cbCopy += cbSkip;

    AssertMsgReturn(cPrdtlEntries > 0, ("Copying 0 bytes is not possible\n"), 0);

    do
    {
        SGLEntry aPrdtlEntries[32];
        uint32_t cPrdtlEntriesRead = cPrdtlEntries < RT_ELEMENTS(aPrdtlEntries)
                                   ? cPrdtlEntries
                                   : RT_ELEMENTS(aPrdtlEntries);

        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysPrdtl, &aPrdtlEntries[0],
                                 cPrdtlEntriesRead * sizeof(SGLEntry));

        for (uint32_t i = 0; (i < cPrdtlEntriesRead) && cbCopy; i++)
        {
            RTGCPHYS GCPhysAddrDataBase = AHCI_RTGCPHYS_FROM_U32(aPrdtlEntries[i].u32DBAUp, aPrdtlEntries[i].u32DBA);
            uint32_t cbThisCopy = (aPrdtlEntries[i].u32DescInf & SGLENTRY_DESCINF_DBC) + 1;

            cbThisCopy = (uint32_t)RT_MIN(cbThisCopy, cbCopy);

            /* Copy into SG entry. */
            pfnCopyWorker(pDevIns, GCPhysAddrDataBase, pSgBuf, cbThisCopy, &cbSkip);

            cbCopy   -= cbThisCopy;
            cbCopied += cbThisCopy;
        }

        GCPhysPrdtl   += cPrdtlEntriesRead * sizeof(SGLEntry);
        cPrdtlEntries -= cPrdtlEntriesRead;
    } while (cPrdtlEntries && cbCopy);

    if (cbCopied < cbCopy)
        pAhciReq->fFlags |= AHCI_REQ_OVERFLOW;

    return cbCopied;
}

/**
 * Copies a data buffer into the S/G buffer set up by the guest.
 *
 * @returns Amount of bytes copied to the PRDTL.
 * @param   pDevIns     The device instance.
 * @param   pAhciReq    AHCI request structure.
 * @param   pSgBuf      The S/G buffer to copy from.
 * @param   cbSkip      How many bytes to skip in advance before starting to copy.
 * @param   cbCopy      How many bytes to copy.
 */
static size_t ahciR3CopySgBufToPrdtl(PPDMDEVINS pDevIns, PAHCIREQ pAhciReq, PRTSGBUF pSgBuf, size_t cbSkip, size_t cbCopy)
{
    return ahciR3PrdtlWalk(pDevIns, pAhciReq, ahciR3CopyBufferToGuestWorker, pSgBuf, cbSkip, cbCopy);
}

/**
 * Copies the S/G buffer into a data buffer.
 *
 * @returns Amount of bytes copied from the PRDTL.
 * @param   pDevIns     The device instance.
 * @param   pAhciReq    AHCI request structure.
 * @param   pSgBuf      The S/G buffer to copy into.
 * @param   cbSkip      How many bytes to skip in advance before starting to copy.
 * @param   cbCopy      How many bytes to copy.
 */
static size_t ahciR3CopySgBufFromPrdtl(PPDMDEVINS pDevIns, PAHCIREQ pAhciReq, PRTSGBUF pSgBuf, size_t cbSkip, size_t cbCopy)
{
    return ahciR3PrdtlWalk(pDevIns, pAhciReq, ahciR3CopyBufferFromGuestWorker, pSgBuf, cbSkip, cbCopy);
}

/**
 * Copy a simple memory buffer to the guest memory buffer.
 *
 * @returns Amount of bytes copied from the PRDTL.
 * @param   pDevIns     The device instance.
 * @param   pAhciReq    AHCI request structure.
 * @param   pvSrc       The buffer to copy from.
 * @param   cbSrc       How many bytes to copy.
 * @param   cbSkip      How many bytes to skip initially.
 */
static size_t ahciR3CopyBufferToPrdtl(PPDMDEVINS pDevIns, PAHCIREQ pAhciReq, const void *pvSrc, size_t cbSrc, size_t cbSkip)
{
    RTSGSEG Seg;
    RTSGBUF SgBuf;
    Seg.pvSeg = (void *)pvSrc;
    Seg.cbSeg = cbSrc;
    RTSgBufInit(&SgBuf, &Seg, 1);
    return ahciR3CopySgBufToPrdtl(pDevIns, pAhciReq, &SgBuf, cbSkip, cbSrc);
}

/**
 * Calculates the size of the guest buffer described by the PRDT.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pAhciReq    AHCI request structure.
 * @param   pcbPrdt     Where to store the size of the guest buffer.
 */
static int ahciR3PrdtQuerySize(PPDMDEVINS pDevIns, PAHCIREQ pAhciReq, size_t *pcbPrdt)
{
    RTGCPHYS GCPhysPrdtl = pAhciReq->GCPhysPrdtl;
    unsigned cPrdtlEntries = pAhciReq->cPrdtlEntries;
    size_t cbPrdt = 0;

    do
    {
        SGLEntry aPrdtlEntries[32];
        uint32_t const cPrdtlEntriesRead = RT_MIN(cPrdtlEntries,  RT_ELEMENTS(aPrdtlEntries));

        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysPrdtl, &aPrdtlEntries[0], cPrdtlEntriesRead * sizeof(SGLEntry));

        for (uint32_t i = 0; i < cPrdtlEntriesRead; i++)
            cbPrdt += (aPrdtlEntries[i].u32DescInf & SGLENTRY_DESCINF_DBC) + 1;

        GCPhysPrdtl   += cPrdtlEntriesRead * sizeof(SGLEntry);
        cPrdtlEntries -= cPrdtlEntriesRead;
    } while (cPrdtlEntries);

    *pcbPrdt = cbPrdt;
    return VINF_SUCCESS;
}

/**
 * Cancels all active tasks on the port.
 *
 * @returns Whether all active tasks were canceled.
 * @param   pAhciPortR3 The AHCI port, ring-3 bits.
 */
static bool ahciR3CancelActiveTasks(PAHCIPORTR3 pAhciPortR3)
{
    if (pAhciPortR3->pDrvMediaEx)
    {
        int rc = pAhciPortR3->pDrvMediaEx->pfnIoReqCancelAll(pAhciPortR3->pDrvMediaEx);
        AssertRC(rc);
    }
    return true; /* always true for now because tasks don't use guest memory as the buffer which makes canceling a task impossible. */
}

/**
 * Creates the array of ranges to trim.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pAhciPort       AHCI port state, shared bits.
 * @param   pAhciReq        The request handling the TRIM request.
 * @param   idxRangeStart   Index of the first range to start copying.
 * @param   paRanges        Where to store the ranges.
 * @param   cRanges         Number of ranges fitting into the array.
 * @param   pcRanges        Where to store the amount of ranges actually copied on success.
 */
static int ahciTrimRangesCreate(PPDMDEVINS pDevIns, PAHCIPORT pAhciPort, PAHCIREQ pAhciReq, uint32_t idxRangeStart,
                                PRTRANGE paRanges, uint32_t cRanges, uint32_t *pcRanges)
{
    SGLEntry aPrdtlEntries[32];
    uint64_t aRanges[64];
    uint32_t cPrdtlEntries = pAhciReq->cPrdtlEntries;
    RTGCPHYS GCPhysPrdtl   = pAhciReq->GCPhysPrdtl;
    int rc = VERR_PDM_MEDIAEX_IOBUF_OVERFLOW;
    uint32_t idxRange = 0;

    LogFlowFunc(("pAhciPort=%#p pAhciReq=%#p\n", pAhciPort, pAhciReq));

    AssertMsgReturn(pAhciReq->enmType == PDMMEDIAEXIOREQTYPE_DISCARD, ("This is not a trim request\n"), VERR_INVALID_PARAMETER);

    if (!cPrdtlEntries)
        pAhciReq->fFlags |= AHCI_REQ_OVERFLOW;

    /* Convert the ranges from ATA to our format. */
    while (   cPrdtlEntries
           && idxRange < cRanges)
    {
        uint32_t cPrdtlEntriesRead = RT_MIN(cPrdtlEntries, RT_ELEMENTS(aPrdtlEntries));

        rc = VINF_SUCCESS;
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysPrdtl, &aPrdtlEntries[0], cPrdtlEntriesRead * sizeof(SGLEntry));

        for (uint32_t i = 0; i < cPrdtlEntriesRead && idxRange < cRanges; i++)
        {
            RTGCPHYS GCPhysAddrDataBase = AHCI_RTGCPHYS_FROM_U32(aPrdtlEntries[i].u32DBAUp, aPrdtlEntries[i].u32DBA);
            uint32_t cbThisCopy = (aPrdtlEntries[i].u32DescInf & SGLENTRY_DESCINF_DBC) + 1;

            cbThisCopy = RT_MIN(cbThisCopy, sizeof(aRanges));

            /* Copy into buffer. */
            PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysAddrDataBase, aRanges, cbThisCopy);

            for (unsigned idxRangeSrc = 0; idxRangeSrc < RT_ELEMENTS(aRanges) && idxRange < cRanges; idxRangeSrc++)
            {
                /* Skip range if told to do so. */
                if (!idxRangeStart)
                {
                    aRanges[idxRangeSrc] = RT_H2LE_U64(aRanges[idxRangeSrc]);
                    if (AHCI_RANGE_LENGTH_GET(aRanges[idxRangeSrc]) != 0)
                    {
                        paRanges[idxRange].offStart = (aRanges[idxRangeSrc] & AHCI_RANGE_LBA_MASK) * pAhciPort->cbSector;
                        paRanges[idxRange].cbRange = AHCI_RANGE_LENGTH_GET(aRanges[idxRangeSrc]) * pAhciPort->cbSector;
                        idxRange++;
                    }
                    else
                        break;
                }
                else
                    idxRangeStart--;
            }
        }

        GCPhysPrdtl   += cPrdtlEntriesRead * sizeof(SGLEntry);
        cPrdtlEntries -= cPrdtlEntriesRead;
    }

    *pcRanges = idxRange;

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Allocates a new AHCI request.
 *
 * @returns A new AHCI request structure or NULL if out of memory.
 * @param   pAhciPortR3 The AHCI port, ring-3 bits.
 * @param   uTag        The tag to assign.
 */
static PAHCIREQ ahciR3ReqAlloc(PAHCIPORTR3 pAhciPortR3, uint32_t uTag)
{
    PAHCIREQ pAhciReq = NULL;
    PDMMEDIAEXIOREQ hIoReq = NULL;

    int rc = pAhciPortR3->pDrvMediaEx->pfnIoReqAlloc(pAhciPortR3->pDrvMediaEx, &hIoReq, (void **)&pAhciReq,
                                                     uTag, PDMIMEDIAEX_F_SUSPEND_ON_RECOVERABLE_ERR);
    if (RT_SUCCESS(rc))
    {
        pAhciReq->hIoReq  = hIoReq;
        pAhciReq->fMapped = false;
    }
    else
        pAhciReq = NULL;
    return pAhciReq;
}

/**
 * Frees a given AHCI request structure.
 *
 * @param   pAhciPortR3 The AHCI port, ring-3 bits.
 * @param   pAhciReq    The request to free.
 */
static void ahciR3ReqFree(PAHCIPORTR3 pAhciPortR3, PAHCIREQ pAhciReq)
{
    if (   pAhciReq
        && !(pAhciReq->fFlags & AHCI_REQ_IS_ON_STACK))
    {
        int rc = pAhciPortR3->pDrvMediaEx->pfnIoReqFree(pAhciPortR3->pDrvMediaEx, pAhciReq->hIoReq);
        AssertRC(rc);
    }
}

/**
 * Complete a data transfer task by freeing all occupied resources
 * and notifying the guest.
 *
 * @returns Flag whether the given request was canceled inbetween;
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AHCI state.
 * @param   pThisCC     The ring-3 AHCI state.
 * @param   pAhciPort   Pointer to the port where to request completed, shared bits.
 * @param   pAhciPortR3 Pointer to the port where to request completed, ring-3 bits.
 * @param   pAhciReq    Pointer to the task which finished.
 * @param   rcReq       IPRT status code of the completed request.
 */
static bool ahciR3TransferComplete(PPDMDEVINS pDevIns, PAHCI pThis, PAHCICC pThisCC,
                                   PAHCIPORT pAhciPort, PAHCIPORTR3 pAhciPortR3, PAHCIREQ pAhciReq, int rcReq)
{
    bool fCanceled = false;

    LogFlowFunc(("pAhciPort=%p pAhciReq=%p rcReq=%d\n",
                 pAhciPort, pAhciReq, rcReq));

    VBOXDD_AHCI_REQ_COMPLETED(pAhciReq, rcReq, pAhciReq->uOffset, pAhciReq->cbTransfer);

    if (pAhciReq->fMapped)
        PDMDevHlpPhysReleasePageMappingLock(pDevIns, &pAhciReq->PgLck);

    if (rcReq != VERR_PDM_MEDIAEX_IOREQ_CANCELED)
    {
        if (pAhciReq->enmType == PDMMEDIAEXIOREQTYPE_READ)
            pAhciPort->Led.Actual.s.fReading = 0;
        else if (pAhciReq->enmType == PDMMEDIAEXIOREQTYPE_WRITE)
            pAhciPort->Led.Actual.s.fWriting = 0;
        else if (pAhciReq->enmType == PDMMEDIAEXIOREQTYPE_DISCARD)
            pAhciPort->Led.Actual.s.fWriting = 0;
        else if (pAhciReq->enmType == PDMMEDIAEXIOREQTYPE_SCSI)
        {
            pAhciPort->Led.Actual.s.fWriting = 0;
            pAhciPort->Led.Actual.s.fReading = 0;
        }

        if (RT_FAILURE(rcReq))
        {
            /* Log the error. */
            if (pAhciPort->cErrors++ < MAX_LOG_REL_ERRORS)
            {
                if (pAhciReq->enmType == PDMMEDIAEXIOREQTYPE_FLUSH)
                    LogRel(("AHCI#%uP%u: Flush returned rc=%Rrc\n",
                            pDevIns->iInstance, pAhciPort->iLUN, rcReq));
                else if (pAhciReq->enmType == PDMMEDIAEXIOREQTYPE_DISCARD)
                    LogRel(("AHCI#%uP%u: Trim returned rc=%Rrc\n",
                            pDevIns->iInstance, pAhciPort->iLUN, rcReq));
                else
                    LogRel(("AHCI#%uP%u: %s at offset %llu (%zu bytes left) returned rc=%Rrc\n",
                            pDevIns->iInstance, pAhciPort->iLUN,
                            pAhciReq->enmType == PDMMEDIAEXIOREQTYPE_READ
                            ? "Read"
                            : "Write",
                            pAhciReq->uOffset,
                            pAhciReq->cbTransfer, rcReq));
            }

            ahciReqSetStatus(pAhciReq, ID_ERR, ATA_STAT_READY | ATA_STAT_ERR);
            /*
             * We have to duplicate the request here as the underlying I/O
             * request will be freed later.
             */
            PAHCIREQ pReqDup = (PAHCIREQ)RTMemDup(pAhciReq, sizeof(AHCIREQ));
            if (   pReqDup
                && !ASMAtomicCmpXchgPtr(&pAhciPortR3->pTaskErr, pReqDup, NULL))
                RTMemFree(pReqDup);
        }
        else
        {
            /* Status will be set already for non I/O requests. */
            if (pAhciReq->enmType == PDMMEDIAEXIOREQTYPE_SCSI)
            {
                if (pAhciReq->u8ScsiSts == SCSI_STATUS_OK)
                {
                    ahciReqSetStatus(pAhciReq, 0, ATA_STAT_READY | ATA_STAT_SEEK);
                    pAhciReq->cmdFis[AHCI_CMDFIS_SECTN] = (pAhciReq->cmdFis[AHCI_CMDFIS_SECTN] & ~7)
                        | ((pAhciReq->fFlags & AHCI_REQ_XFER_2_HOST) ? ATAPI_INT_REASON_IO : 0)
                        | (!pAhciReq->cbTransfer ? ATAPI_INT_REASON_CD : 0);
                }
                else
                {
                    ahciReqSetStatus(pAhciReq, pAhciPort->abATAPISense[2] << 4, ATA_STAT_READY | ATA_STAT_ERR);
                    pAhciReq->cmdFis[AHCI_CMDFIS_SECTN] = (pAhciReq->cmdFis[AHCI_CMDFIS_SECTN] & ~7) |
                                                          ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
                    pAhciReq->cbTransfer = 0;
                    LogFlowFunc(("SCSI request completed with %u status\n", pAhciReq->u8ScsiSts));
                }
            }
            else if (pAhciReq->enmType != PDMMEDIAEXIOREQTYPE_INVALID)
                ahciReqSetStatus(pAhciReq, 0, ATA_STAT_READY | ATA_STAT_SEEK);

            /* Write updated command header into memory of the guest. */
            uint32_t u32PRDBC = 0;
            if (pAhciReq->enmType != PDMMEDIAEXIOREQTYPE_INVALID)
            {
                size_t cbXfer = 0;
                int rc = pAhciPortR3->pDrvMediaEx->pfnIoReqQueryXferSize(pAhciPortR3->pDrvMediaEx, pAhciReq->hIoReq, &cbXfer);
                AssertRC(rc);
                u32PRDBC = (uint32_t)RT_MIN(cbXfer, pAhciReq->cbTransfer);
            }
            else
                u32PRDBC = (uint32_t)pAhciReq->cbTransfer;

            PDMDevHlpPCIPhysWriteMeta(pDevIns, pAhciReq->GCPhysCmdHdrAddr + RT_UOFFSETOF(CmdHdr, u32PRDBC),
                                      &u32PRDBC, sizeof(u32PRDBC));

            if (pAhciReq->fFlags & AHCI_REQ_OVERFLOW)
            {
                /*
                 * The guest tried to transfer more data than there is space in the buffer.
                 * Terminate task and set the overflow bit.
                 */
                /* Notify the guest. */
                ASMAtomicOrU32(&pAhciPort->regIS, AHCI_PORT_IS_OFS);
                if (pAhciPort->regIE & AHCI_PORT_IE_OFE)
                    ahciHbaSetInterrupt(pDevIns, pThis, pAhciPort->iLUN, VERR_IGNORED);
            }
        }

        /*
         * Make a copy of the required data now and free the request. Otherwise the guest
         * might issue a new request with the same tag and we run into a conflict when allocating
         * a new request with the same tag later on.
         */
        uint32_t fFlags = pAhciReq->fFlags;
        uint32_t uTag   = pAhciReq->uTag;
        size_t   cbTransfer = pAhciReq->cbTransfer;
        bool     fRead      = pAhciReq->enmType == PDMMEDIAEXIOREQTYPE_READ;
        uint8_t  cmdFis[AHCI_CMDFIS_TYPE_H2D_SIZE];
        memcpy(&cmdFis[0], &pAhciReq->cmdFis[0], sizeof(cmdFis));

        ahciR3ReqFree(pAhciPortR3, pAhciReq);

        /* Post a PIO setup FIS first if this is a PIO command which transfers data. */
        if (fFlags & AHCI_REQ_PIO_DATA)
            ahciSendPioSetupFis(pDevIns, pThis, pAhciPort, cbTransfer, &cmdFis[0], fRead, false /* fInterrupt */);

        if (fFlags & AHCI_REQ_CLEAR_SACT)
        {
            if (RT_SUCCESS(rcReq) && !ASMAtomicReadPtrT(&pAhciPortR3->pTaskErr, PAHCIREQ))
                ASMAtomicOrU32(&pAhciPort->u32QueuedTasksFinished, RT_BIT_32(uTag));
        }

        if (fFlags & AHCI_REQ_IS_QUEUED)
        {
            /*
             * Always raise an interrupt after task completion; delaying
             * this (interrupt coalescing) increases latency and has a significant
             * impact on performance (see @bugref{5071})
             */
            ahciSendSDBFis(pDevIns, pThis, pAhciPort, pAhciPortR3, 0, true);
        }
        else
            ahciSendD2HFis(pDevIns, pThis, pAhciPort, uTag, &cmdFis[0], true);
    }
    else
    {
        /*
         * Task was canceled, do the cleanup but DO NOT access the guest memory!
         * The guest might use it for other things now because it doesn't know about that task anymore.
         */
        fCanceled = true;

        /* Leave a log message about the canceled request. */
        if (pAhciPort->cErrors++ < MAX_LOG_REL_ERRORS)
        {
            if (pAhciReq->enmType == PDMMEDIAEXIOREQTYPE_FLUSH)
                LogRel(("AHCI#%uP%u: Canceled flush returned rc=%Rrc\n",
                        pDevIns->iInstance, pAhciPort->iLUN, rcReq));
            else if (pAhciReq->enmType == PDMMEDIAEXIOREQTYPE_DISCARD)
                LogRel(("AHCI#%uP%u: Canceled trim returned rc=%Rrc\n",
                        pDevIns->iInstance,pAhciPort->iLUN, rcReq));
            else
                LogRel(("AHCI#%uP%u: Canceled %s at offset %llu (%zu bytes left) returned rc=%Rrc\n",
                        pDevIns->iInstance, pAhciPort->iLUN,
                        pAhciReq->enmType == PDMMEDIAEXIOREQTYPE_READ
                        ? "read"
                        : "write",
                        pAhciReq->uOffset,
                        pAhciReq->cbTransfer, rcReq));
         }

         ahciR3ReqFree(pAhciPortR3, pAhciReq);
    }

    /*
     * Decrement the active task counter as the last step or we might run into a
     * hang during power off otherwise (see @bugref{7859}).
     * Before it could happen that we signal PDM that we are done while we still have to
     * copy the data to the guest but EMT might be busy destroying the driver chains
     * below us while we have to delegate copying data to EMT instead of doing it
     * on this thread.
     */
    ASMAtomicDecU32(&pAhciPort->cTasksActive);

    if (pAhciPort->cTasksActive == 0 && pThisCC->fSignalIdle)
        PDMDevHlpAsyncNotificationCompleted(pDevIns);

    return fCanceled;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCopyFromBuf}
 */
static DECLCALLBACK(int) ahciR3IoReqCopyFromBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                void *pvIoReqAlloc, uint32_t offDst, PRTSGBUF pSgBuf,
                                                size_t cbCopy)
{
    PAHCIPORTR3 pAhciPortR3 = RT_FROM_MEMBER(pInterface, AHCIPORTR3, IMediaExPort);
    int rc = VINF_SUCCESS;
    PAHCIREQ pIoReq = (PAHCIREQ)pvIoReqAlloc;
    RT_NOREF(hIoReq);

    ahciR3CopySgBufToPrdtl(pAhciPortR3->pDevIns, pIoReq, pSgBuf, offDst, cbCopy);

    if (pIoReq->fFlags & AHCI_REQ_OVERFLOW)
        rc = VERR_PDM_MEDIAEX_IOBUF_OVERFLOW;

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCopyToBuf}
 */
static DECLCALLBACK(int) ahciR3IoReqCopyToBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                              void *pvIoReqAlloc, uint32_t offSrc, PRTSGBUF pSgBuf,
                                              size_t cbCopy)
{
    PAHCIPORTR3 pAhciPortR3 = RT_FROM_MEMBER(pInterface, AHCIPORTR3, IMediaExPort);
    int rc = VINF_SUCCESS;
    PAHCIREQ pIoReq = (PAHCIREQ)pvIoReqAlloc;
    RT_NOREF(hIoReq);

    ahciR3CopySgBufFromPrdtl(pAhciPortR3->pDevIns, pIoReq, pSgBuf, offSrc, cbCopy);
    if (pIoReq->fFlags & AHCI_REQ_OVERFLOW)
        rc = VERR_PDM_MEDIAEX_IOBUF_UNDERRUN;

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqQueryBuf}
 */
static DECLCALLBACK(int) ahciR3IoReqQueryBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                             void *pvIoReqAlloc, void **ppvBuf, size_t *pcbBuf)
{
    PAHCIPORTR3 pAhciPortR3 = RT_FROM_MEMBER(pInterface, AHCIPORTR3, IMediaExPort);
    PPDMDEVINS  pDevIns     = pAhciPortR3->pDevIns;
    PAHCIREQ    pIoReq      = (PAHCIREQ)pvIoReqAlloc;
    int         rc          = VERR_NOT_SUPPORTED;
    RT_NOREF(hIoReq);

    /* Only allow single 4KB page aligned buffers at the moment. */
    if (   pIoReq->cPrdtlEntries == 1
        && pIoReq->cbTransfer    == _4K)
    {
        RTGCPHYS GCPhysPrdt = pIoReq->GCPhysPrdtl;
        SGLEntry PrdtEntry;

        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysPrdt, &PrdtEntry, sizeof(SGLEntry));

        RTGCPHYS GCPhysAddrDataBase = AHCI_RTGCPHYS_FROM_U32(PrdtEntry.u32DBAUp, PrdtEntry.u32DBA);
        uint32_t cbData = (PrdtEntry.u32DescInf & SGLENTRY_DESCINF_DBC) + 1;

        if (   cbData >= _4K
            && !(GCPhysAddrDataBase & (_4K - 1)))
        {
            rc = PDMDevHlpPCIPhysGCPhys2CCPtr(pDevIns, NULL /* pPciDev */, GCPhysAddrDataBase, 0, ppvBuf, &pIoReq->PgLck);
            if (RT_SUCCESS(rc))
            {
                pIoReq->fMapped = true;
                *pcbBuf = cbData;
            }
            else
                rc = VERR_NOT_SUPPORTED;
        }
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqQueryDiscardRanges}
 */
static DECLCALLBACK(int) ahciR3IoReqQueryDiscardRanges(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                       void *pvIoReqAlloc, uint32_t idxRangeStart,
                                                       uint32_t cRanges, PRTRANGE paRanges,
                                                       uint32_t *pcRanges)
{
    PAHCIPORTR3 pAhciPortR3 = RT_FROM_MEMBER(pInterface, AHCIPORTR3, IMediaExPort);
    PPDMDEVINS  pDevIns     = pAhciPortR3->pDevIns;
    PAHCI       pThis       = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    PAHCIPORT   pAhciPort   = &RT_SAFE_SUBSCRIPT(pThis->aPorts, pAhciPortR3->iLUN);
    PAHCIREQ    pIoReq      = (PAHCIREQ)pvIoReqAlloc;
    RT_NOREF(hIoReq);

    return ahciTrimRangesCreate(pDevIns, pAhciPort, pIoReq, idxRangeStart, paRanges, cRanges, pcRanges);
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCompleteNotify}
 */
static DECLCALLBACK(int) ahciR3IoReqCompleteNotify(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                   void *pvIoReqAlloc, int rcReq)
{
    PAHCIPORTR3 pAhciPortR3 = RT_FROM_MEMBER(pInterface, AHCIPORTR3, IMediaExPort);
    PPDMDEVINS  pDevIns     = pAhciPortR3->pDevIns;
    PAHCIR3     pThisCC     = PDMDEVINS_2_DATA_CC(pDevIns, PAHCICC);
    PAHCI       pThis       = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    PAHCIPORT   pAhciPort   = &RT_SAFE_SUBSCRIPT(pThis->aPorts, pAhciPortR3->iLUN);
    PAHCIREQ    pIoReq      = (PAHCIREQ)pvIoReqAlloc;
    RT_NOREF(hIoReq);

    ahciR3TransferComplete(pDevIns, pThis, pThisCC, pAhciPort, pAhciPortR3, pIoReq, rcReq);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqStateChanged}
 */
static DECLCALLBACK(void) ahciR3IoReqStateChanged(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                  void *pvIoReqAlloc, PDMMEDIAEXIOREQSTATE enmState)
{
    PAHCIPORTR3 pAhciPortR3 = RT_FROM_MEMBER(pInterface, AHCIPORTR3, IMediaExPort);
    PPDMDEVINS  pDevIns     = pAhciPortR3->pDevIns;
    PAHCIR3     pThisCC     = PDMDEVINS_2_DATA_CC(pDevIns, PAHCICC);
    PAHCI       pThis       = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    PAHCIPORT   pAhciPort   = &RT_SAFE_SUBSCRIPT(pThis->aPorts, pAhciPortR3->iLUN);
    RT_NOREF(hIoReq, pvIoReqAlloc);

    switch (enmState)
    {
        case PDMMEDIAEXIOREQSTATE_SUSPENDED:
        {
            /* Make sure the request is not accounted for so the VM can suspend successfully. */
            uint32_t cTasksActive = ASMAtomicDecU32(&pAhciPort->cTasksActive);
            if (!cTasksActive && pThisCC->fSignalIdle)
                PDMDevHlpAsyncNotificationCompleted(pDevIns);
            break;
        }
        case PDMMEDIAEXIOREQSTATE_ACTIVE:
            /* Make sure the request is accounted for so the VM suspends only when the request is complete. */
            ASMAtomicIncU32(&pAhciPort->cTasksActive);
            break;
        default:
            AssertMsgFailed(("Invalid request state given %u\n", enmState));
    }
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnMediumEjected}
 */
static DECLCALLBACK(void) ahciR3MediumEjected(PPDMIMEDIAEXPORT pInterface)
{
    PAHCIPORTR3 pAhciPortR3 = RT_FROM_MEMBER(pInterface, AHCIPORTR3, IMediaExPort);
    PPDMDEVINS  pDevIns     = pAhciPortR3->pDevIns;
    PAHCIR3     pThisCC     = PDMDEVINS_2_DATA_CC(pDevIns, PAHCICC);
    PAHCI       pThis       = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    PAHCIPORT   pAhciPort   = &RT_SAFE_SUBSCRIPT(pThis->aPorts, pAhciPortR3->iLUN);

    if (pThisCC->pMediaNotify)
    {
        int rc = PDMDevHlpVMReqCallNoWait(pDevIns, VMCPUID_ANY,
                                          (PFNRT)pThisCC->pMediaNotify->pfnEjected, 2,
                                          pThisCC->pMediaNotify, pAhciPort->iLUN);
        AssertRC(rc);
    }
}

/**
 * Process an non read/write ATA command.
 *
 * @returns The direction of the data transfer
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared AHCI state.
 * @param   pAhciPort       The AHCI port of the request, shared bits.
 * @param   pAhciPortR3     The AHCI port of the request, ring-3 bits.
 * @param   pAhciReq        The AHCI request state.
 * @param   pCmdFis         Pointer to the command FIS.
 */
static PDMMEDIAEXIOREQTYPE ahciProcessCmd(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, PAHCIPORTR3 pAhciPortR3,
                                          PAHCIREQ pAhciReq, uint8_t *pCmdFis)
{
    PDMMEDIAEXIOREQTYPE enmType = PDMMEDIAEXIOREQTYPE_INVALID;
    bool fLBA48 = false;

    AssertMsg(pCmdFis[AHCI_CMDFIS_TYPE] == AHCI_CMDFIS_TYPE_H2D, ("FIS is not a host to device Fis!!\n"));

    pAhciReq->cbTransfer = 0;

    switch (pCmdFis[AHCI_CMDFIS_CMD])
    {
        case ATA_IDENTIFY_DEVICE:
        {
            if (pAhciPortR3->pDrvMedia && !pAhciPort->fATAPI)
            {
                uint16_t u16Temp[256];

                /* Fill the buffer. */
                ahciIdentifySS(pThis, pAhciPort, pAhciPortR3, u16Temp);

                /* Copy the buffer. */
                size_t cbCopied = ahciR3CopyBufferToPrdtl(pDevIns, pAhciReq, &u16Temp[0], sizeof(u16Temp), 0 /* cbSkip */);

                pAhciReq->fFlags |= AHCI_REQ_PIO_DATA;
                pAhciReq->cbTransfer = cbCopied;
                ahciReqSetStatus(pAhciReq, 0, ATA_STAT_READY | ATA_STAT_SEEK);
            }
            else
                ahciReqSetStatus(pAhciReq, ABRT_ERR, ATA_STAT_READY | ATA_STAT_SEEK | ATA_STAT_ERR);
            break;
        }
        case ATA_READ_NATIVE_MAX_ADDRESS_EXT:
        case ATA_READ_NATIVE_MAX_ADDRESS:
                break;
        case ATA_SET_FEATURES:
        {
            switch (pCmdFis[AHCI_CMDFIS_FET])
            {
                case 0x02: /* write cache enable */
                case 0xaa: /* read look-ahead enable */
                case 0x55: /* read look-ahead disable */
                case 0xcc: /* reverting to power-on defaults enable */
                case 0x66: /* reverting to power-on defaults disable */
                    ahciReqSetStatus(pAhciReq, 0, ATA_STAT_READY | ATA_STAT_SEEK);
                    break;
                case 0x82: /* write cache disable */
                    enmType = PDMMEDIAEXIOREQTYPE_FLUSH;
                    break;
                case 0x03:
                {
                    /* set transfer mode */
                    Log2(("%s: transfer mode %#04x\n", __FUNCTION__, pCmdFis[AHCI_CMDFIS_SECTC]));
                    switch (pCmdFis[AHCI_CMDFIS_SECTC] & 0xf8)
                    {
                        case 0x00: /* PIO default */
                        case 0x08: /* PIO mode */
                            break;
                        case ATA_MODE_MDMA: /* MDMA mode */
                            pAhciPort->uATATransferMode = (pCmdFis[AHCI_CMDFIS_SECTC] & 0xf8) | RT_MIN(pCmdFis[AHCI_CMDFIS_SECTC] & 0x07, ATA_MDMA_MODE_MAX);
                            break;
                        case ATA_MODE_UDMA: /* UDMA mode */
                            pAhciPort->uATATransferMode = (pCmdFis[AHCI_CMDFIS_SECTC] & 0xf8) | RT_MIN(pCmdFis[AHCI_CMDFIS_SECTC] & 0x07, ATA_UDMA_MODE_MAX);
                            break;
                    }
                    ahciReqSetStatus(pAhciReq, 0, ATA_STAT_READY | ATA_STAT_SEEK);
                    break;
                }
                default:
                    ahciReqSetStatus(pAhciReq, ABRT_ERR, ATA_STAT_READY | ATA_STAT_ERR);
            }
            break;
        }
        case ATA_DEVICE_RESET:
        {
            if (!pAhciPort->fATAPI)
                ahciReqSetStatus(pAhciReq, ABRT_ERR, ATA_STAT_READY | ATA_STAT_ERR);
            else
            {
                /* Reset the device. */
                ahciDeviceReset(pDevIns, pThis, pAhciPort, pAhciReq);
            }
            break;
        }
        case ATA_FLUSH_CACHE_EXT:
        case ATA_FLUSH_CACHE:
            enmType = PDMMEDIAEXIOREQTYPE_FLUSH;
            break;
        case ATA_PACKET:
            if (!pAhciPort->fATAPI)
                ahciReqSetStatus(pAhciReq, ABRT_ERR, ATA_STAT_READY | ATA_STAT_ERR);
            else
                enmType = PDMMEDIAEXIOREQTYPE_SCSI;
            break;
        case ATA_IDENTIFY_PACKET_DEVICE:
            if (!pAhciPort->fATAPI)
                ahciReqSetStatus(pAhciReq, ABRT_ERR, ATA_STAT_READY | ATA_STAT_ERR);
            else
            {
                size_t cbData;
                ahciR3AtapiIdentify(pDevIns, pAhciReq, pAhciPort, 512, &cbData);

                pAhciReq->fFlags |= AHCI_REQ_PIO_DATA;
                pAhciReq->cbTransfer = cbData;
                pAhciReq->cmdFis[AHCI_CMDFIS_SECTN] = (pAhciReq->cmdFis[AHCI_CMDFIS_SECTN] & ~7)
                    | ((pAhciReq->fFlags & AHCI_REQ_XFER_2_HOST) ? ATAPI_INT_REASON_IO : 0)
                    | (!pAhciReq->cbTransfer ? ATAPI_INT_REASON_CD : 0);

                ahciReqSetStatus(pAhciReq, 0, ATA_STAT_READY | ATA_STAT_SEEK);
            }
            break;
        case ATA_SET_MULTIPLE_MODE:
            if (    pCmdFis[AHCI_CMDFIS_SECTC] != 0
                &&  (   pCmdFis[AHCI_CMDFIS_SECTC] > ATA_MAX_MULT_SECTORS
                     || (pCmdFis[AHCI_CMDFIS_SECTC] & (pCmdFis[AHCI_CMDFIS_SECTC] - 1)) != 0))
                ahciReqSetStatus(pAhciReq, ABRT_ERR, ATA_STAT_READY | ATA_STAT_ERR);
            else
            {
                Log2(("%s: set multi sector count to %d\n", __FUNCTION__, pCmdFis[AHCI_CMDFIS_SECTC]));
                pAhciPort->cMultSectors = pCmdFis[AHCI_CMDFIS_SECTC];
                ahciReqSetStatus(pAhciReq, 0, ATA_STAT_READY | ATA_STAT_SEEK);
            }
            break;
        case ATA_STANDBY_IMMEDIATE:
            break; /* Do nothing. */
        case ATA_CHECK_POWER_MODE:
            pAhciReq->cmdFis[AHCI_CMDFIS_SECTC] = 0xff; /* drive active or idle */
            RT_FALL_THRU();
        case ATA_INITIALIZE_DEVICE_PARAMETERS:
        case ATA_IDLE_IMMEDIATE:
        case ATA_RECALIBRATE:
        case ATA_NOP:
        case ATA_READ_VERIFY_SECTORS_EXT:
        case ATA_READ_VERIFY_SECTORS:
        case ATA_READ_VERIFY_SECTORS_WITHOUT_RETRIES:
        case ATA_SLEEP:
            ahciReqSetStatus(pAhciReq, 0, ATA_STAT_READY | ATA_STAT_SEEK);
            break;
        case ATA_READ_DMA_EXT:
            fLBA48 = true;
            RT_FALL_THRU();
        case ATA_READ_DMA:
        {
            pAhciReq->cbTransfer = ahciGetNSectors(pCmdFis, fLBA48) * pAhciPort->cbSector;
            pAhciReq->uOffset = ahciGetSector(pAhciPort, pCmdFis, fLBA48) * pAhciPort->cbSector;
            enmType = PDMMEDIAEXIOREQTYPE_READ;
            break;
        }
        case ATA_WRITE_DMA_EXT:
            fLBA48 = true;
            RT_FALL_THRU();
        case ATA_WRITE_DMA:
        {
            pAhciReq->cbTransfer = ahciGetNSectors(pCmdFis, fLBA48) * pAhciPort->cbSector;
            pAhciReq->uOffset = ahciGetSector(pAhciPort, pCmdFis, fLBA48) * pAhciPort->cbSector;
            enmType = PDMMEDIAEXIOREQTYPE_WRITE;
            break;
        }
        case ATA_READ_FPDMA_QUEUED:
        {
            pAhciReq->cbTransfer = ahciGetNSectorsQueued(pCmdFis) * pAhciPort->cbSector;
            pAhciReq->uOffset = ahciGetSectorQueued(pCmdFis) * pAhciPort->cbSector;
            pAhciReq->fFlags |= AHCI_REQ_IS_QUEUED;
            enmType = PDMMEDIAEXIOREQTYPE_READ;
            break;
        }
        case ATA_WRITE_FPDMA_QUEUED:
        {
            pAhciReq->cbTransfer = ahciGetNSectorsQueued(pCmdFis) * pAhciPort->cbSector;
            pAhciReq->uOffset = ahciGetSectorQueued(pCmdFis) * pAhciPort->cbSector;
            pAhciReq->fFlags |= AHCI_REQ_IS_QUEUED;
            enmType = PDMMEDIAEXIOREQTYPE_WRITE;
            break;
        }
        case ATA_READ_LOG_EXT:
        {
            size_t cbLogRead = ((pCmdFis[AHCI_CMDFIS_SECTCEXP] << 8) | pCmdFis[AHCI_CMDFIS_SECTC]) * 512;
            unsigned offLogRead = ((pCmdFis[AHCI_CMDFIS_CYLLEXP] << 8) | pCmdFis[AHCI_CMDFIS_CYLL]) * 512;
            unsigned iPage = pCmdFis[AHCI_CMDFIS_SECTN];

            LogFlow(("Trying to read %zu bytes starting at offset %u from page %u\n", cbLogRead, offLogRead, iPage));

            uint8_t aBuf[512];

            memset(aBuf, 0, sizeof(aBuf));

            if (offLogRead + cbLogRead <= sizeof(aBuf))
            {
                switch (iPage)
                {
                    case 0x10:
                    {
                        LogFlow(("Reading error page\n"));
                        PAHCIREQ pTaskErr = ASMAtomicXchgPtrT(&pAhciPortR3->pTaskErr, NULL, PAHCIREQ);
                        if (pTaskErr)
                        {
                            aBuf[0] = (pTaskErr->fFlags & AHCI_REQ_IS_QUEUED) ? pTaskErr->uTag : (1 << 7);
                            aBuf[2] = pTaskErr->cmdFis[AHCI_CMDFIS_STS];
                            aBuf[3] = pTaskErr->cmdFis[AHCI_CMDFIS_ERR];
                            aBuf[4] = pTaskErr->cmdFis[AHCI_CMDFIS_SECTN];
                            aBuf[5] = pTaskErr->cmdFis[AHCI_CMDFIS_CYLL];
                            aBuf[6] = pTaskErr->cmdFis[AHCI_CMDFIS_CYLH];
                            aBuf[7] = pTaskErr->cmdFis[AHCI_CMDFIS_HEAD];
                            aBuf[8] = pTaskErr->cmdFis[AHCI_CMDFIS_SECTNEXP];
                            aBuf[9] = pTaskErr->cmdFis[AHCI_CMDFIS_CYLLEXP];
                            aBuf[10] = pTaskErr->cmdFis[AHCI_CMDFIS_CYLHEXP];
                            aBuf[12] = pTaskErr->cmdFis[AHCI_CMDFIS_SECTC];
                            aBuf[13] = pTaskErr->cmdFis[AHCI_CMDFIS_SECTCEXP];

                            /* Calculate checksum */
                            uint8_t uChkSum = 0;
                            for (unsigned i = 0; i < RT_ELEMENTS(aBuf)-1; i++)
                                uChkSum += aBuf[i];

                            aBuf[511] = (uint8_t)-(int8_t)uChkSum;

                            /* Finally free the error task state structure because it is completely unused now. */
                            RTMemFree(pTaskErr);
                        }

                        /*
                         * Reading this log page results in an abort of all outstanding commands
                         * and clearing the SActive register and TaskFile register.
                         *
                         * See SATA2 1.2 spec chapter 4.2.3.4
                         */
                        bool fAbortedAll = ahciR3CancelActiveTasks(pAhciPortR3);
                        Assert(fAbortedAll); NOREF(fAbortedAll);
                        ahciSendSDBFis(pDevIns, pThis, pAhciPort, pAhciPortR3, UINT32_C(0xffffffff), true);

                        break;
                    }
                }

                /* Copy the buffer. */
                size_t cbCopied = ahciR3CopyBufferToPrdtl(pDevIns, pAhciReq, &aBuf[offLogRead], cbLogRead, 0 /* cbSkip */);

                pAhciReq->fFlags |= AHCI_REQ_PIO_DATA;
                pAhciReq->cbTransfer = cbCopied;
            }

            break;
        }
        case ATA_DATA_SET_MANAGEMENT:
        {
            if (pAhciPort->fTrimEnabled)
            {
                /* Check that the trim bit is set and all other bits are 0. */
                if (   !(pAhciReq->cmdFis[AHCI_CMDFIS_FET] & UINT16_C(0x01))
                    || (pAhciReq->cmdFis[AHCI_CMDFIS_FET] & ~UINT16_C(0x1)))
                    ahciReqSetStatus(pAhciReq, ABRT_ERR, ATA_STAT_READY | ATA_STAT_ERR);
                else
                    enmType = PDMMEDIAEXIOREQTYPE_DISCARD;
                break;
            }
            /* else: fall through and report error to the guest. */
        }
        RT_FALL_THRU();
        /* All not implemented commands go below. */
        case ATA_SECURITY_FREEZE_LOCK:
        case ATA_SMART:
        case ATA_NV_CACHE:
        case ATA_IDLE:
        case ATA_TRUSTED_RECEIVE_DMA: /* Windows 8+ */
            ahciReqSetStatus(pAhciReq, ABRT_ERR, ATA_STAT_READY | ATA_STAT_ERR);
            break;
        default: /* For debugging purposes. */
            AssertMsgFailed(("Unknown command issued (%#x)\n", pCmdFis[AHCI_CMDFIS_CMD]));
            ahciReqSetStatus(pAhciReq, ABRT_ERR, ATA_STAT_READY | ATA_STAT_ERR);
    }

    return enmType;
}

/**
 * Retrieve a command FIS from guest memory.
 *
 * @returns whether the H2D FIS was successfully read from the guest memory.
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AHCI state.
 * @param   pAhciPort   The AHCI port of the request, shared bits.
 * @param   pAhciReq    The state of the actual task.
 */
static bool ahciPortTaskGetCommandFis(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, PAHCIREQ pAhciReq)
{
    AssertMsgReturn(pAhciPort->GCPhysAddrClb && pAhciPort->GCPhysAddrFb,
                    ("%s: GCPhysAddrClb and/or GCPhysAddrFb are 0\n", __FUNCTION__),
                    false);

    /*
     * First we are reading the command header pointed to by regCLB.
     * From this we get the address of the command table which we are reading too.
     * We can process the Command FIS afterwards.
     */
    CmdHdr cmdHdr;
    pAhciReq->GCPhysCmdHdrAddr = pAhciPort->GCPhysAddrClb + pAhciReq->uTag * sizeof(CmdHdr);
    LogFlow(("%s: PDMDevHlpPCIPhysReadMeta GCPhysAddrCmdLst=%RGp cbCmdHdr=%u\n", __FUNCTION__,
             pAhciReq->GCPhysCmdHdrAddr, sizeof(CmdHdr)));
    PDMDevHlpPCIPhysReadMeta(pDevIns, pAhciReq->GCPhysCmdHdrAddr, &cmdHdr, sizeof(CmdHdr));

#ifdef LOG_ENABLED
    /* Print some infos about the command header. */
    ahciDumpCmdHdrInfo(pAhciPort, &cmdHdr);
#endif

    RTGCPHYS GCPhysAddrCmdTbl = AHCI_RTGCPHYS_FROM_U32(cmdHdr.u32CmdTblAddrUp, cmdHdr.u32CmdTblAddr);

    AssertMsgReturn((cmdHdr.u32DescInf & AHCI_CMDHDR_CFL_MASK) * sizeof(uint32_t) == AHCI_CMDFIS_TYPE_H2D_SIZE,
                    ("This is not a command FIS!!\n"),
                    false);

    /* Read the command Fis. */
    LogFlow(("%s: PDMDevHlpPCIPhysReadMeta GCPhysAddrCmdTbl=%RGp cbCmdFis=%u\n", __FUNCTION__, GCPhysAddrCmdTbl, AHCI_CMDFIS_TYPE_H2D_SIZE));
    PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysAddrCmdTbl, &pAhciReq->cmdFis[0], AHCI_CMDFIS_TYPE_H2D_SIZE);

    AssertMsgReturn(pAhciReq->cmdFis[AHCI_CMDFIS_TYPE] == AHCI_CMDFIS_TYPE_H2D,
                    ("This is not a command FIS\n"),
                    false);

    /* Set transfer direction. */
    pAhciReq->fFlags |= (cmdHdr.u32DescInf & AHCI_CMDHDR_W) ? 0 : AHCI_REQ_XFER_2_HOST;

    /* If this is an ATAPI command read the atapi command. */
    if (cmdHdr.u32DescInf & AHCI_CMDHDR_A)
    {
        GCPhysAddrCmdTbl += AHCI_CMDHDR_ACMD_OFFSET;
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysAddrCmdTbl, &pAhciReq->aATAPICmd[0], ATAPI_PACKET_SIZE);
    }

    /* We "received" the FIS. Clear the BSY bit in regTFD. */
    if ((cmdHdr.u32DescInf & AHCI_CMDHDR_C) && (pAhciReq->fFlags & AHCI_REQ_CLEAR_SACT))
    {
        /*
         * We need to send a FIS which clears the busy bit if this is a queued command so that the guest can queue other commands.
         * but this FIS does not assert an interrupt
         */
        ahciSendD2HFis(pDevIns, pThis, pAhciPort, pAhciReq->uTag, pAhciReq->cmdFis, false);
        pAhciPort->regTFD &= ~AHCI_PORT_TFD_BSY;
    }

    pAhciReq->GCPhysPrdtl = AHCI_RTGCPHYS_FROM_U32(cmdHdr.u32CmdTblAddrUp, cmdHdr.u32CmdTblAddr) + AHCI_CMDHDR_PRDT_OFFSET;
    pAhciReq->cPrdtlEntries = AHCI_CMDHDR_PRDTL_ENTRIES(cmdHdr.u32DescInf);

#ifdef LOG_ENABLED
    /* Print some infos about the FIS. */
    ahciDumpFisInfo(pAhciPort, &pAhciReq->cmdFis[0]);

    /* Print the PRDT */
    ahciLog(("PRDT address %RGp number of entries %u\n", pAhciReq->GCPhysPrdtl, pAhciReq->cPrdtlEntries));
    RTGCPHYS GCPhysPrdtl = pAhciReq->GCPhysPrdtl;

    for (unsigned i = 0; i < pAhciReq->cPrdtlEntries; i++)
    {
        SGLEntry SGEntry;

        ahciLog(("Entry %u at address %RGp\n", i, GCPhysPrdtl));
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysPrdtl, &SGEntry, sizeof(SGLEntry));

        RTGCPHYS GCPhysDataAddr = AHCI_RTGCPHYS_FROM_U32(SGEntry.u32DBAUp, SGEntry.u32DBA);
        ahciLog(("GCPhysAddr=%RGp Size=%u\n", GCPhysDataAddr, SGEntry.u32DescInf & SGLENTRY_DESCINF_DBC));

        GCPhysPrdtl += sizeof(SGLEntry);
    }
#endif

    return true;
}

/**
 * Submits a given request for execution.
 *
 * @returns Flag whether the request was canceled inbetween.
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AHCI state.
 * @param   pThisCC     The ring-3 AHCI state.
 * @param   pAhciPort   The port the request is for, shared bits.
 * @param   pAhciPortR3 The port the request is for, ring-3 bits.
 * @param   pAhciReq    The request to submit.
 * @param   enmType     The request type.
 */
static bool ahciR3ReqSubmit(PPDMDEVINS pDevIns, PAHCI pThis, PAHCICC pThisCC, PAHCIPORT pAhciPort, PAHCIPORTR3 pAhciPortR3,
                            PAHCIREQ pAhciReq, PDMMEDIAEXIOREQTYPE enmType)
{
    int rc = VINF_SUCCESS;
    bool fReqCanceled = false;

    VBOXDD_AHCI_REQ_SUBMIT(pAhciReq, pAhciReq->enmType, pAhciReq->uOffset, pAhciReq->cbTransfer);

    if (enmType == PDMMEDIAEXIOREQTYPE_FLUSH)
        rc = pAhciPortR3->pDrvMediaEx->pfnIoReqFlush(pAhciPortR3->pDrvMediaEx, pAhciReq->hIoReq);
    else if (enmType == PDMMEDIAEXIOREQTYPE_DISCARD)
    {
        uint32_t cRangesMax;

        /* The data buffer contains LBA range entries. Each range is 8 bytes big. */
        if (!pAhciReq->cmdFis[AHCI_CMDFIS_SECTC] && !pAhciReq->cmdFis[AHCI_CMDFIS_SECTCEXP])
            cRangesMax = 65536 * 512 / 8;
        else
            cRangesMax = pAhciReq->cmdFis[AHCI_CMDFIS_SECTC] * 512 / 8;

        pAhciPort->Led.Asserted.s.fWriting = pAhciPort->Led.Actual.s.fWriting = 1;
        rc = pAhciPortR3->pDrvMediaEx->pfnIoReqDiscard(pAhciPortR3->pDrvMediaEx, pAhciReq->hIoReq,
                                                     cRangesMax);
    }
    else if (enmType == PDMMEDIAEXIOREQTYPE_READ)
    {
        pAhciPort->Led.Asserted.s.fReading = pAhciPort->Led.Actual.s.fReading = 1;
        rc = pAhciPortR3->pDrvMediaEx->pfnIoReqRead(pAhciPortR3->pDrvMediaEx, pAhciReq->hIoReq,
                                                  pAhciReq->uOffset, pAhciReq->cbTransfer);
    }
    else if (enmType == PDMMEDIAEXIOREQTYPE_WRITE)
    {
        pAhciPort->Led.Asserted.s.fWriting = pAhciPort->Led.Actual.s.fWriting = 1;
        rc = pAhciPortR3->pDrvMediaEx->pfnIoReqWrite(pAhciPortR3->pDrvMediaEx, pAhciReq->hIoReq,
                                                   pAhciReq->uOffset, pAhciReq->cbTransfer);
    }
    else if (enmType == PDMMEDIAEXIOREQTYPE_SCSI)
    {
        size_t cbBuf = 0;

        if (pAhciReq->cPrdtlEntries)
            rc = ahciR3PrdtQuerySize(pDevIns, pAhciReq, &cbBuf);
        pAhciReq->cbTransfer = cbBuf;
        if (RT_SUCCESS(rc))
        {
            if (cbBuf && (pAhciReq->fFlags & AHCI_REQ_XFER_2_HOST))
                pAhciPort->Led.Asserted.s.fReading = pAhciPort->Led.Actual.s.fReading = 1;
            else if (cbBuf)
                pAhciPort->Led.Asserted.s.fWriting = pAhciPort->Led.Actual.s.fWriting = 1;
            rc = pAhciPortR3->pDrvMediaEx->pfnIoReqSendScsiCmd(pAhciPortR3->pDrvMediaEx, pAhciReq->hIoReq,
                                                             0, &pAhciReq->aATAPICmd[0], ATAPI_PACKET_SIZE,
                                                             PDMMEDIAEXIOREQSCSITXDIR_UNKNOWN, NULL, cbBuf,
                                                             &pAhciPort->abATAPISense[0], sizeof(pAhciPort->abATAPISense), NULL,
                                                             &pAhciReq->u8ScsiSts, 30 * RT_MS_1SEC);
        }
    }

    if (rc == VINF_SUCCESS)
        fReqCanceled = ahciR3TransferComplete(pDevIns, pThis, pThisCC, pAhciPort, pAhciPortR3, pAhciReq, VINF_SUCCESS);
    else if (rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
        fReqCanceled = ahciR3TransferComplete(pDevIns, pThis, pThisCC, pAhciPort, pAhciPortR3, pAhciReq, rc);

    return fReqCanceled;
}

/**
 * Prepares the command for execution coping it from guest memory and doing a few
 * validation checks on it.
 *
 * @returns Whether the command was successfully fetched from guest memory and
 *          can be continued.
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared AHCI state.
 * @param   pAhciPort   The AHCI port the request is for, shared bits.
 * @param   pAhciReq    Request structure to copy the command to.
 */
static bool ahciR3CmdPrepare(PPDMDEVINS pDevIns, PAHCI pThis, PAHCIPORT pAhciPort, PAHCIREQ pAhciReq)
{
    /* Set current command slot */
    ASMAtomicWriteU32(&pAhciPort->u32CurrentCommandSlot, pAhciReq->uTag);

    bool fContinue = ahciPortTaskGetCommandFis(pDevIns, pThis, pAhciPort, pAhciReq);
    if (fContinue)
    {
        /* Mark the task as processed by the HBA if this is a queued task so that it doesn't occur in the CI register anymore. */
        if (pAhciPort->regSACT & RT_BIT_32(pAhciReq->uTag))
        {
            pAhciReq->fFlags |= AHCI_REQ_CLEAR_SACT;
            ASMAtomicOrU32(&pAhciPort->u32TasksFinished, RT_BIT_32(pAhciReq->uTag));
        }

        if (pAhciReq->cmdFis[AHCI_CMDFIS_BITS] & AHCI_CMDFIS_C)
        {
            /*
             * It is possible that the request counter can get one higher than the maximum because
             * the request counter is decremented after the guest was notified about the completed
             * request (see @bugref{7859}). If the completing thread is preempted in between the
             * guest might already issue another request before the request counter is decremented
             * which would trigger the following assertion incorrectly in the past.
             */
            AssertLogRelMsg(ASMAtomicReadU32(&pAhciPort->cTasksActive) <= AHCI_NR_COMMAND_SLOTS,
                            ("AHCI#%uP%u: There are more than %u (+1) requests active",
                             pDevIns->iInstance, pAhciPort->iLUN,
                             AHCI_NR_COMMAND_SLOTS));
            ASMAtomicIncU32(&pAhciPort->cTasksActive);
        }
        else
        {
            /* If the reset bit is set put the device into reset state. */
            if (pAhciReq->cmdFis[AHCI_CMDFIS_CTL] & AHCI_CMDFIS_CTL_SRST)
            {
                ahciLog(("%s: Setting device into reset state\n", __FUNCTION__));
                pAhciPort->fResetDevice = true;
                ahciSendD2HFis(pDevIns, pThis, pAhciPort, pAhciReq->uTag, pAhciReq->cmdFis, true);
            }
            else if (pAhciPort->fResetDevice) /* The bit is not set and we are in a reset state. */
                ahciFinishStorageDeviceReset(pDevIns, pThis, pAhciPort, pAhciReq);
            else /* We are not in a reset state update the control registers. */
                AssertMsgFailed(("%s: Update the control register\n", __FUNCTION__));

            fContinue = false;
        }
    }
    else
    {
        /*
         * Couldn't find anything in either the AHCI or SATA spec which
         * indicates what should be done if the FIS is not read successfully.
         * The closest thing is in the state machine, stating that the device
         * should go into idle state again (SATA spec 1.0 chapter 8.7.1).
         * Do the same here and ignore any corrupt FIS types, after all
         * the guest messed up everything and this behavior is undefined.
         */
        fContinue = false;
    }

    return fContinue;
}

/**
 * @callback_method_impl{FNPDMTHREADDEV, The async IO thread for one port.}
 */
static DECLCALLBACK(int) ahciAsyncIOLoop(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    PAHCIPORTR3 pAhciPortR3 = (PAHCIPORTR3)pThread->pvUser;
    PAHCI       pThis       = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    PAHCIR3     pThisCC     = PDMDEVINS_2_DATA_CC(pDevIns, PAHCICC);
    PAHCIPORT   pAhciPort   = &RT_SAFE_SUBSCRIPT(pThis->aPorts, pAhciPortR3->iLUN);
    int         rc          = VINF_SUCCESS;

    ahciLog(("%s: Port %d entering async IO loop.\n", __FUNCTION__, pAhciPort->iLUN));

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        unsigned idx = 0;
        uint32_t u32Tasks = 0;
        uint32_t u32RegHbaCtrl = 0;

        ASMAtomicWriteBool(&pAhciPort->fWrkThreadSleeping, true);
        u32Tasks = ASMAtomicXchgU32(&pAhciPort->u32TasksNew, 0);
        if (!u32Tasks)
        {
            Assert(ASMAtomicReadBool(&pAhciPort->fWrkThreadSleeping));
            rc = PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pAhciPort->hEvtProcess, RT_INDEFINITE_WAIT);
            AssertLogRelMsgReturn(RT_SUCCESS(rc) || rc == VERR_INTERRUPTED, ("%Rrc\n", rc), rc);
            if (RT_UNLIKELY(pThread->enmState != PDMTHREADSTATE_RUNNING))
                break;
            LogFlowFunc(("Woken up with rc=%Rrc\n", rc));
            u32Tasks = ASMAtomicXchgU32(&pAhciPort->u32TasksNew, 0);
        }

        ASMAtomicWriteBool(&pAhciPort->fWrkThreadSleeping, false);
        ASMAtomicIncU32(&pThis->cThreadsActive);

        /* Check whether the thread should be suspended. */
        if (pThisCC->fSignalIdle)
        {
            if (!ASMAtomicDecU32(&pThis->cThreadsActive))
                PDMDevHlpAsyncNotificationCompleted(pDevIns);
            continue;
        }

        /*
         * Check whether the global host controller bit is set and go to sleep immediately again
         * if it is set.
         */
        u32RegHbaCtrl = ASMAtomicReadU32(&pThis->regHbaCtrl);
        if (   u32RegHbaCtrl & AHCI_HBA_CTRL_HR
            && !ASMAtomicDecU32(&pThis->cThreadsActive))
        {
            ahciR3HBAReset(pDevIns, pThis, pThisCC);
            if (pThisCC->fSignalIdle)
                PDMDevHlpAsyncNotificationCompleted(pDevIns);
            continue;
        }

        idx = ASMBitFirstSetU32(u32Tasks);
        while (   idx
               && !pAhciPort->fPortReset)
        {
            bool fReqCanceled = false;

            /* Decrement to get the slot number. */
            idx--;
            ahciLog(("%s: Processing command at slot %d\n", __FUNCTION__, idx));

            PAHCIREQ pAhciReq = ahciR3ReqAlloc(pAhciPortR3, idx);
            if (RT_LIKELY(pAhciReq))
            {
                pAhciReq->uTag          = idx;
                pAhciReq->fFlags        = 0;

                bool fContinue = ahciR3CmdPrepare(pDevIns, pThis, pAhciPort, pAhciReq);
                if (fContinue)
                {
                    PDMMEDIAEXIOREQTYPE enmType = ahciProcessCmd(pDevIns, pThis, pAhciPort, pAhciPortR3,
                                                                 pAhciReq, pAhciReq->cmdFis);
                    pAhciReq->enmType = enmType;

                    if (enmType != PDMMEDIAEXIOREQTYPE_INVALID)
                        fReqCanceled = ahciR3ReqSubmit(pDevIns, pThis, pThisCC, pAhciPort, pAhciPortR3, pAhciReq, enmType);
                    else
                        fReqCanceled = ahciR3TransferComplete(pDevIns, pThis, pThisCC, pAhciPort, pAhciPortR3,
                                                              pAhciReq, VINF_SUCCESS);
                } /* Command */
                else
                    ahciR3ReqFree(pAhciPortR3, pAhciReq);
            }
            else /* !Request allocated, use on stack variant to signal the error. */
            {
                AHCIREQ Req;
                Req.uTag       = idx;
                Req.fFlags     = AHCI_REQ_IS_ON_STACK;
                Req.fMapped    = false;
                Req.cbTransfer = 0;
                Req.uOffset    = 0;
                Req.enmType    = PDMMEDIAEXIOREQTYPE_INVALID;

                bool fContinue = ahciR3CmdPrepare(pDevIns, pThis, pAhciPort, &Req);
                if (fContinue)
                    fReqCanceled = ahciR3TransferComplete(pDevIns, pThis, pThisCC, pAhciPort, pAhciPortR3, &Req, VERR_NO_MEMORY);
            }

            /*
             * Don't process other requests if the last one was canceled,
             * the others are not valid anymore.
             */
            if (fReqCanceled)
                break;

            u32Tasks &= ~RT_BIT_32(idx); /* Clear task bit. */
            idx = ASMBitFirstSetU32(u32Tasks);
        } /* while tasks available */

        /* Check whether a port reset was active. */
        if (   ASMAtomicReadBool(&pAhciPort->fPortReset)
            && (pAhciPort->regSCTL & AHCI_PORT_SCTL_DET) == AHCI_PORT_SCTL_DET_NINIT)
            ahciPortResetFinish(pDevIns, pThis, pAhciPort, pAhciPortR3);

        /*
         * Check whether a host controller reset is pending and execute the reset
         * if this is the last active thread.
         */
        u32RegHbaCtrl = ASMAtomicReadU32(&pThis->regHbaCtrl);
        uint32_t cThreadsActive = ASMAtomicDecU32(&pThis->cThreadsActive);
        if (   (u32RegHbaCtrl & AHCI_HBA_CTRL_HR)
            && !cThreadsActive)
            ahciR3HBAReset(pDevIns, pThis, pThisCC);

        if (!cThreadsActive && pThisCC->fSignalIdle)
            PDMDevHlpAsyncNotificationCompleted(pDevIns);
    } /* While running */

    ahciLog(("%s: Port %d async IO thread exiting\n", __FUNCTION__, pAhciPort->iLUN));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNPDMTHREADWAKEUPDEV}
 */
static DECLCALLBACK(int) ahciAsyncIOLoopWakeUp(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    PAHCIPORTR3 pAhciPortR3 = (PAHCIPORTR3)pThread->pvUser;
    PAHCI       pThis       = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    PAHCIPORT   pAhciPort   = &RT_SAFE_SUBSCRIPT(pThis->aPorts, pAhciPortR3->iLUN);
    return PDMDevHlpSUPSemEventSignal(pDevIns, pAhciPort->hEvtProcess);
}

/* -=-=-=-=- DBGF -=-=-=-=- */

/**
 * AHCI status info callback.
 *
 * @param   pDevIns     The device instance.
 * @param   pHlp        The output helpers.
 * @param   pszArgs     The arguments.
 */
static DECLCALLBACK(void) ahciR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PAHCI pThis = PDMDEVINS_2_DATA(pDevIns, PAHCI);

    /*
     * Show info.
     */
    pHlp->pfnPrintf(pHlp,
                    "%s#%d: mmio=%RGp ports=%u GC=%RTbool R0=%RTbool\n",
                    pDevIns->pReg->szName,
                    pDevIns->iInstance,
                    PDMDevHlpMmioGetMappingAddress(pDevIns, pThis->hMmio),
                    pThis->cPortsImpl,
                    pDevIns->fRCEnabled,
                    pDevIns->fR0Enabled);

    /*
     * Show global registers.
     */
    pHlp->pfnPrintf(pHlp, "HbaCap=%#x\n", pThis->regHbaCap);
    pHlp->pfnPrintf(pHlp, "HbaCtrl=%#x\n", pThis->regHbaCtrl);
    pHlp->pfnPrintf(pHlp, "HbaIs=%#x\n", pThis->regHbaIs);
    pHlp->pfnPrintf(pHlp, "HbaPi=%#x\n", pThis->regHbaPi);
    pHlp->pfnPrintf(pHlp, "HbaVs=%#x\n", pThis->regHbaVs);
    pHlp->pfnPrintf(pHlp, "HbaCccCtl=%#x\n", pThis->regHbaCccCtl);
    pHlp->pfnPrintf(pHlp, "HbaCccPorts=%#x\n", pThis->regHbaCccPorts);
    pHlp->pfnPrintf(pHlp, "PortsInterrupted=%#x\n", pThis->u32PortsInterrupted);

    /*
     * Per port data.
     */
    uint32_t const cPortsImpl = RT_MIN(pThis->cPortsImpl, RT_ELEMENTS(pThis->aPorts));
    for (unsigned i = 0; i < cPortsImpl; i++)
    {
        PAHCIPORT pThisPort = &pThis->aPorts[i];

        pHlp->pfnPrintf(pHlp, "Port %d: device-attached=%RTbool\n", pThisPort->iLUN, pThisPort->fPresent);
        pHlp->pfnPrintf(pHlp, "PortClb=%#x\n", pThisPort->regCLB);
        pHlp->pfnPrintf(pHlp, "PortClbU=%#x\n", pThisPort->regCLBU);
        pHlp->pfnPrintf(pHlp, "PortFb=%#x\n", pThisPort->regFB);
        pHlp->pfnPrintf(pHlp, "PortFbU=%#x\n", pThisPort->regFBU);
        pHlp->pfnPrintf(pHlp, "PortIs=%#x\n", pThisPort->regIS);
        pHlp->pfnPrintf(pHlp, "PortIe=%#x\n", pThisPort->regIE);
        pHlp->pfnPrintf(pHlp, "PortCmd=%#x\n", pThisPort->regCMD);
        pHlp->pfnPrintf(pHlp, "PortTfd=%#x\n", pThisPort->regTFD);
        pHlp->pfnPrintf(pHlp, "PortSig=%#x\n", pThisPort->regSIG);
        pHlp->pfnPrintf(pHlp, "PortSSts=%#x\n", pThisPort->regSSTS);
        pHlp->pfnPrintf(pHlp, "PortSCtl=%#x\n", pThisPort->regSCTL);
        pHlp->pfnPrintf(pHlp, "PortSErr=%#x\n", pThisPort->regSERR);
        pHlp->pfnPrintf(pHlp, "PortSAct=%#x\n", pThisPort->regSACT);
        pHlp->pfnPrintf(pHlp, "PortCi=%#x\n", pThisPort->regCI);
        pHlp->pfnPrintf(pHlp, "PortPhysClb=%RGp\n", pThisPort->GCPhysAddrClb);
        pHlp->pfnPrintf(pHlp, "PortPhysFb=%RGp\n", pThisPort->GCPhysAddrFb);
        pHlp->pfnPrintf(pHlp, "PortActTasksActive=%u\n", pThisPort->cTasksActive);
        pHlp->pfnPrintf(pHlp, "PortPoweredOn=%RTbool\n", pThisPort->fPoweredOn);
        pHlp->pfnPrintf(pHlp, "PortSpunUp=%RTbool\n", pThisPort->fSpunUp);
        pHlp->pfnPrintf(pHlp, "PortFirstD2HFisSent=%RTbool\n", pThisPort->fFirstD2HFisSent);
        pHlp->pfnPrintf(pHlp, "PortATAPI=%RTbool\n", pThisPort->fATAPI);
        pHlp->pfnPrintf(pHlp, "PortTasksFinished=%#x\n", pThisPort->u32TasksFinished);
        pHlp->pfnPrintf(pHlp, "PortQueuedTasksFinished=%#x\n", pThisPort->u32QueuedTasksFinished);
        pHlp->pfnPrintf(pHlp, "PortTasksNew=%#x\n", pThisPort->u32TasksNew);
        pHlp->pfnPrintf(pHlp, "\n");
    }
}

/* -=-=-=-=- Helper -=-=-=-=- */

/**
 * Checks if all asynchronous I/O is finished, both AHCI and IDE.
 *
 * Used by ahciR3Reset, ahciR3Suspend and ahciR3PowerOff. ahciR3SavePrep makes
 * use of it in strict builds (which is why it's up here).
 *
 * @returns true if quiesced, false if busy.
 * @param   pDevIns         The device instance.
 */
static bool ahciR3AllAsyncIOIsFinished(PPDMDEVINS pDevIns)
{
    PAHCI pThis = PDMDEVINS_2_DATA(pDevIns, PAHCI);

    if (pThis->cThreadsActive)
        return false;

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aPorts); i++)
    {
        PAHCIPORT pThisPort = &pThis->aPorts[i];
        if (pThisPort->fPresent)
        {
            if (   (pThisPort->cTasksActive != 0)
                || (pThisPort->u32TasksNew != 0))
               return false;
        }
    }
    return true;
}

/* -=-=-=-=- Saved State -=-=-=-=- */

/**
 * @callback_method_impl{FNSSMDEVSAVEPREP}
 */
static DECLCALLBACK(int) ahciR3SavePrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pDevIns, pSSM);
    Assert(ahciR3AllAsyncIOIsFinished(pDevIns));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMDEVLOADPREP}
 */
static DECLCALLBACK(int) ahciR3LoadPrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pDevIns, pSSM);
    Assert(ahciR3AllAsyncIOIsFinished(pDevIns));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC}
 */
static DECLCALLBACK(int) ahciR3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PAHCI           pThis = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    RT_NOREF(uPass);

    /* config. */
    pHlp->pfnSSMPutU32(pSSM, pThis->cPortsImpl);
    for (uint32_t i = 0; i < AHCI_MAX_NR_PORTS_IMPL; i++)
    {
        pHlp->pfnSSMPutBool(pSSM, pThis->aPorts[i].fPresent);
        pHlp->pfnSSMPutBool(pSSM, pThis->aPorts[i].fHotpluggable);
        pHlp->pfnSSMPutStrZ(pSSM, pThis->aPorts[i].szSerialNumber);
        pHlp->pfnSSMPutStrZ(pSSM, pThis->aPorts[i].szFirmwareRevision);
        pHlp->pfnSSMPutStrZ(pSSM, pThis->aPorts[i].szModelNumber);
    }

    static const char *s_apszIdeEmuPortNames[4] = { "PrimaryMaster", "PrimarySlave", "SecondaryMaster", "SecondarySlave" };
    for (uint32_t i = 0; i < RT_ELEMENTS(s_apszIdeEmuPortNames); i++)
    {
        uint32_t iPort;
        int rc = pHlp->pfnCFGMQueryU32Def(pDevIns->pCfg, s_apszIdeEmuPortNames[i], &iPort, i);
        AssertRCReturn(rc, rc);
        pHlp->pfnSSMPutU32(pSSM, iPort);
    }

    return VINF_SSM_DONT_CALL_AGAIN;
}

/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) ahciR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PAHCI           pThis = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    uint32_t i;
    int rc;

    Assert(!pThis->f8ByteMMIO4BytesWrittenSuccessfully);

    /* The config */
    rc = ahciR3LiveExec(pDevIns, pSSM, SSM_PASS_FINAL);
    AssertRCReturn(rc, rc);

    /* The main device structure. */
    pHlp->pfnSSMPutU32(pSSM, pThis->regHbaCap);
    pHlp->pfnSSMPutU32(pSSM, pThis->regHbaCtrl);
    pHlp->pfnSSMPutU32(pSSM, pThis->regHbaIs);
    pHlp->pfnSSMPutU32(pSSM, pThis->regHbaPi);
    pHlp->pfnSSMPutU32(pSSM, pThis->regHbaVs);
    pHlp->pfnSSMPutU32(pSSM, pThis->regHbaCccCtl);
    pHlp->pfnSSMPutU32(pSSM, pThis->regHbaCccPorts);
    pHlp->pfnSSMPutU8(pSSM, pThis->uCccPortNr);
    pHlp->pfnSSMPutU64(pSSM, pThis->uCccTimeout);
    pHlp->pfnSSMPutU32(pSSM, pThis->uCccNr);
    pHlp->pfnSSMPutU32(pSSM, pThis->uCccCurrentNr);
    pHlp->pfnSSMPutU32(pSSM, pThis->u32PortsInterrupted);
    pHlp->pfnSSMPutBool(pSSM, pThis->fReset);
    pHlp->pfnSSMPutBool(pSSM, pThis->f64BitAddr);
    pHlp->pfnSSMPutBool(pSSM, pDevIns->fR0Enabled);
    pHlp->pfnSSMPutBool(pSSM, pDevIns->fRCEnabled);
    pHlp->pfnSSMPutBool(pSSM, pThis->fLegacyPortResetMethod);

    /* Now every port. */
    for (i = 0; i < AHCI_MAX_NR_PORTS_IMPL; i++)
    {
        Assert(pThis->aPorts[i].cTasksActive == 0);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].regCLB);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].regCLBU);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].regFB);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].regFBU);
        pHlp->pfnSSMPutGCPhys(pSSM, pThis->aPorts[i].GCPhysAddrClb);
        pHlp->pfnSSMPutGCPhys(pSSM, pThis->aPorts[i].GCPhysAddrFb);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].regIS);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].regIE);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].regCMD);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].regTFD);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].regSIG);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].regSSTS);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].regSCTL);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].regSERR);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].regSACT);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].regCI);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].PCHSGeometry.cCylinders);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].PCHSGeometry.cHeads);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].PCHSGeometry.cSectors);
        pHlp->pfnSSMPutU64(pSSM, pThis->aPorts[i].cTotalSectors);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].cMultSectors);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPorts[i].uATATransferMode);
        pHlp->pfnSSMPutBool(pSSM, pThis->aPorts[i].fResetDevice);
        pHlp->pfnSSMPutBool(pSSM, pThis->aPorts[i].fPoweredOn);
        pHlp->pfnSSMPutBool(pSSM, pThis->aPorts[i].fSpunUp);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].u32TasksFinished);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].u32QueuedTasksFinished);
        pHlp->pfnSSMPutU32(pSSM, pThis->aPorts[i].u32CurrentCommandSlot);

        /* ATAPI saved state. */
        pHlp->pfnSSMPutBool(pSSM, pThis->aPorts[i].fATAPI);
        pHlp->pfnSSMPutMem(pSSM, &pThis->aPorts[i].abATAPISense[0], sizeof(pThis->aPorts[i].abATAPISense));
    }

    return pHlp->pfnSSMPutU32(pSSM, UINT32_MAX); /* sanity/terminator */
}

/**
 * Loads a saved legacy ATA emulated device state.
 *
 * @returns VBox status code.
 * @param   pHlp    The device helper call table.
 * @param   pSSM    The handle to the saved state.
 */
static int ahciR3LoadLegacyEmulationState(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM)
{
    int             rc;
    uint32_t        u32Version;
    uint32_t        u32;
    uint32_t        u32IOBuffer;

    /* Test for correct version. */
    rc = pHlp->pfnSSMGetU32(pSSM, &u32Version);
    AssertRCReturn(rc, rc);
    LogFlow(("LoadOldSavedStates u32Version = %d\n", u32Version));

    if (   u32Version != ATA_CTL_SAVED_STATE_VERSION
        && u32Version != ATA_CTL_SAVED_STATE_VERSION_WITHOUT_FULL_SENSE
        && u32Version != ATA_CTL_SAVED_STATE_VERSION_WITHOUT_EVENT_STATUS)
    {
        AssertMsgFailed(("u32Version=%d\n", u32Version));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    pHlp->pfnSSMSkip(pSSM, 19 + 5 * sizeof(bool) + 8 /* sizeof(BMDMAState) */);

    for (uint32_t j = 0; j < 2; j++)
    {
        pHlp->pfnSSMSkip(pSSM, 88 + 5 * sizeof(bool) );

        if (u32Version > ATA_CTL_SAVED_STATE_VERSION_WITHOUT_FULL_SENSE)
            pHlp->pfnSSMSkip(pSSM, 64);
        else
            pHlp->pfnSSMSkip(pSSM, 2);
        /** @todo triple-check this hack after passthrough is working */
        pHlp->pfnSSMSkip(pSSM, 1);

        if (u32Version > ATA_CTL_SAVED_STATE_VERSION_WITHOUT_EVENT_STATUS)
            pHlp->pfnSSMSkip(pSSM, 4);

        pHlp->pfnSSMSkip(pSSM, sizeof(PDMLED));
        pHlp->pfnSSMGetU32(pSSM, &u32IOBuffer);
        if (u32IOBuffer)
            pHlp->pfnSSMSkip(pSSM, u32IOBuffer);
    }

    rc = pHlp->pfnSSMGetU32(pSSM, &u32);
    if (RT_FAILURE(rc))
        return rc;
    if (u32 != ~0U)
    {
        AssertMsgFailed(("u32=%#x expected ~0\n", u32));
        rc = VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
        return rc;
    }

    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) ahciR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PAHCI           pThis = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    uint32_t u32;
    int rc;

    if (   uVersion > AHCI_SAVED_STATE_VERSION
        || uVersion < AHCI_SAVED_STATE_VERSION_VBOX_30)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* Deal with the priod after removing the saved IDE bits where the saved
       state version remained unchanged. */
    if (   uVersion == AHCI_SAVED_STATE_VERSION_IDE_EMULATION
        && pHlp->pfnSSMHandleRevision(pSSM) >= 79045
        && pHlp->pfnSSMHandleRevision(pSSM) <  79201)
        uVersion++;

    /*
     * Check whether we have to resort to the legacy port reset method to
     * prevent older BIOS versions from failing after a reset.
     */
    if (uVersion <= AHCI_SAVED_STATE_VERSION_PRE_PORT_RESET_CHANGES)
        pThis->fLegacyPortResetMethod = true;

    /* Verify config. */
    if (uVersion > AHCI_SAVED_STATE_VERSION_VBOX_30)
    {
        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertRCReturn(rc, rc);
        if (u32 != pThis->cPortsImpl)
        {
            LogRel(("AHCI: Config mismatch: cPortsImpl - saved=%u config=%u\n", u32, pThis->cPortsImpl));
            if (    u32 < pThis->cPortsImpl
                ||  u32 > AHCI_MAX_NR_PORTS_IMPL)
                return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: cPortsImpl - saved=%u config=%u"),
                                               u32, pThis->cPortsImpl);
        }

        for (uint32_t i = 0; i < AHCI_MAX_NR_PORTS_IMPL; i++)
        {
            bool fInUse;
            rc = pHlp->pfnSSMGetBool(pSSM, &fInUse);
            AssertRCReturn(rc, rc);
            if (fInUse != pThis->aPorts[i].fPresent)
                return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                               N_("The %s VM is missing a device on port %u. Please make sure the source and target VMs have compatible storage configurations"),
                                               fInUse ? "target" : "source", i);

            if (uVersion > AHCI_SAVED_STATE_VERSION_PRE_HOTPLUG_FLAG)
            {
                bool fHotpluggable;
                rc = pHlp->pfnSSMGetBool(pSSM, &fHotpluggable);
                AssertRCReturn(rc, rc);
                if (fHotpluggable != pThis->aPorts[i].fHotpluggable)
                    return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                                   N_("AHCI: Port %u config mismatch: Hotplug flag - saved=%RTbool config=%RTbool\n"),
                                                   i, fHotpluggable, pThis->aPorts[i].fHotpluggable);
            }
            else
                Assert(pThis->aPorts[i].fHotpluggable);

            char szSerialNumber[AHCI_SERIAL_NUMBER_LENGTH+1];
            rc = pHlp->pfnSSMGetStrZ(pSSM, szSerialNumber,     sizeof(szSerialNumber));
            AssertRCReturn(rc, rc);
            if (strcmp(szSerialNumber, pThis->aPorts[i].szSerialNumber))
                LogRel(("AHCI: Port %u config mismatch: Serial number - saved='%s' config='%s'\n",
                        i, szSerialNumber, pThis->aPorts[i].szSerialNumber));

            char szFirmwareRevision[AHCI_FIRMWARE_REVISION_LENGTH+1];
            rc = pHlp->pfnSSMGetStrZ(pSSM, szFirmwareRevision, sizeof(szFirmwareRevision));
            AssertRCReturn(rc, rc);
            if (strcmp(szFirmwareRevision, pThis->aPorts[i].szFirmwareRevision))
                LogRel(("AHCI: Port %u config mismatch: Firmware revision - saved='%s' config='%s'\n",
                        i, szFirmwareRevision, pThis->aPorts[i].szFirmwareRevision));

            char szModelNumber[AHCI_MODEL_NUMBER_LENGTH+1];
            rc = pHlp->pfnSSMGetStrZ(pSSM, szModelNumber,      sizeof(szModelNumber));
            AssertRCReturn(rc, rc);
            if (strcmp(szModelNumber, pThis->aPorts[i].szModelNumber))
                LogRel(("AHCI: Port %u config mismatch: Model number - saved='%s' config='%s'\n",
                        i, szModelNumber, pThis->aPorts[i].szModelNumber));
        }

        static const char *s_apszIdeEmuPortNames[4] = { "PrimaryMaster", "PrimarySlave", "SecondaryMaster", "SecondarySlave" };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_apszIdeEmuPortNames); i++)
        {
            uint32_t iPort;
            rc = pHlp->pfnCFGMQueryU32Def(pDevIns->pCfg, s_apszIdeEmuPortNames[i], &iPort, i);
            AssertRCReturn(rc, rc);

            uint32_t iPortSaved;
            rc = pHlp->pfnSSMGetU32(pSSM, &iPortSaved);
            AssertRCReturn(rc, rc);

            if (iPortSaved != iPort)
                return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("IDE %s config mismatch: saved=%u config=%u"),
                                               s_apszIdeEmuPortNames[i], iPortSaved, iPort);
        }
    }

    if (uPass == SSM_PASS_FINAL)
    {
        /* Restore data. */

        /* The main device structure. */
        pHlp->pfnSSMGetU32(pSSM, &pThis->regHbaCap);
        pHlp->pfnSSMGetU32(pSSM, &pThis->regHbaCtrl);
        pHlp->pfnSSMGetU32(pSSM, &pThis->regHbaIs);
        pHlp->pfnSSMGetU32(pSSM, &pThis->regHbaPi);
        pHlp->pfnSSMGetU32(pSSM, &pThis->regHbaVs);
        pHlp->pfnSSMGetU32(pSSM, &pThis->regHbaCccCtl);
        pHlp->pfnSSMGetU32(pSSM, &pThis->regHbaCccPorts);
        pHlp->pfnSSMGetU8(pSSM, &pThis->uCccPortNr);
        pHlp->pfnSSMGetU64(pSSM, &pThis->uCccTimeout);
        pHlp->pfnSSMGetU32(pSSM, &pThis->uCccNr);
        pHlp->pfnSSMGetU32(pSSM, &pThis->uCccCurrentNr);

        pHlp->pfnSSMGetU32V(pSSM, &pThis->u32PortsInterrupted);
        pHlp->pfnSSMGetBool(pSSM, &pThis->fReset);
        pHlp->pfnSSMGetBool(pSSM, &pThis->f64BitAddr);
        bool fIgn;
        pHlp->pfnSSMGetBool(pSSM, &fIgn); /* Was fR0Enabled, which should never have been saved! */
        pHlp->pfnSSMGetBool(pSSM, &fIgn); /* Was fGCEnabled, which should never have been saved! */
        if (uVersion > AHCI_SAVED_STATE_VERSION_PRE_PORT_RESET_CHANGES)
            pHlp->pfnSSMGetBool(pSSM, &pThis->fLegacyPortResetMethod);

        /* Now every port. */
        for (uint32_t i = 0; i < AHCI_MAX_NR_PORTS_IMPL; i++)
        {
            PAHCIPORT pAhciPort = &pThis->aPorts[i];

            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].regCLB);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].regCLBU);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].regFB);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].regFBU);
            pHlp->pfnSSMGetGCPhysV(pSSM, &pThis->aPorts[i].GCPhysAddrClb);
            pHlp->pfnSSMGetGCPhysV(pSSM, &pThis->aPorts[i].GCPhysAddrFb);
            pHlp->pfnSSMGetU32V(pSSM, &pThis->aPorts[i].regIS);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].regIE);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].regCMD);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].regTFD);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].regSIG);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].regSSTS);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].regSCTL);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].regSERR);
            pHlp->pfnSSMGetU32V(pSSM, &pThis->aPorts[i].regSACT);
            pHlp->pfnSSMGetU32V(pSSM, &pThis->aPorts[i].regCI);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].PCHSGeometry.cCylinders);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].PCHSGeometry.cHeads);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].PCHSGeometry.cSectors);
            pHlp->pfnSSMGetU64(pSSM, &pThis->aPorts[i].cTotalSectors);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aPorts[i].cMultSectors);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aPorts[i].uATATransferMode);
            pHlp->pfnSSMGetBool(pSSM, &pThis->aPorts[i].fResetDevice);

            if (uVersion <= AHCI_SAVED_STATE_VERSION_VBOX_30)
                pHlp->pfnSSMSkip(pSSM, AHCI_NR_COMMAND_SLOTS * sizeof(uint8_t)); /* no active data here */

            if (uVersion < AHCI_SAVED_STATE_VERSION_IDE_EMULATION)
            {
                /* The old positions in the FIFO, not required. */
                pHlp->pfnSSMSkip(pSSM, 2*sizeof(uint8_t));
            }
            pHlp->pfnSSMGetBool(pSSM, &pThis->aPorts[i].fPoweredOn);
            pHlp->pfnSSMGetBool(pSSM, &pThis->aPorts[i].fSpunUp);
            pHlp->pfnSSMGetU32V(pSSM, &pThis->aPorts[i].u32TasksFinished);
            pHlp->pfnSSMGetU32V(pSSM, &pThis->aPorts[i].u32QueuedTasksFinished);

            if (uVersion >= AHCI_SAVED_STATE_VERSION_IDE_EMULATION)
                pHlp->pfnSSMGetU32V(pSSM, &pThis->aPorts[i].u32CurrentCommandSlot);

            if (uVersion > AHCI_SAVED_STATE_VERSION_PRE_ATAPI)
            {
                pHlp->pfnSSMGetBool(pSSM, &pThis->aPorts[i].fATAPI);
                pHlp->pfnSSMGetMem(pSSM, pThis->aPorts[i].abATAPISense, sizeof(pThis->aPorts[i].abATAPISense));
                if (uVersion <= AHCI_SAVED_STATE_VERSION_PRE_ATAPI_REMOVE)
                {
                    pHlp->pfnSSMSkip(pSSM, 1); /* cNotifiedMediaChange. */
                    pHlp->pfnSSMSkip(pSSM, 4); /* MediaEventStatus */
                }
            }
            else if (pThis->aPorts[i].fATAPI)
                return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: atapi - saved=false config=true"));

            /* Check if we have tasks pending. */
            uint32_t fTasksOutstanding = pAhciPort->regCI & ~pAhciPort->u32TasksFinished;
            uint32_t fQueuedTasksOutstanding = pAhciPort->regSACT & ~pAhciPort->u32QueuedTasksFinished;

            pAhciPort->u32TasksNew = fTasksOutstanding | fQueuedTasksOutstanding;

            if (pAhciPort->u32TasksNew)
            {
                /*
                 * There are tasks pending. The VM was saved after a task failed
                 * because of non-fatal error. Set the redo flag.
                 */
                pAhciPort->fRedo = true;
            }
        }

        if (uVersion <= AHCI_SAVED_STATE_VERSION_IDE_EMULATION)
        {
            for (uint32_t i = 0; i < 2; i++)
            {
                rc = ahciR3LoadLegacyEmulationState(pHlp, pSSM);
                if(RT_FAILURE(rc))
                    return rc;
            }
        }

        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        if (RT_FAILURE(rc))
            return rc;
        AssertMsgReturn(u32 == UINT32_MAX, ("%#x\n", u32), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
    }

    return VINF_SUCCESS;
}

/* -=-=-=-=- device PDM interface -=-=-=-=- */

/**
 * Configure the attached device for a port.
 *
 * Used by ahciR3Construct and ahciR3Attach.
 *
 * @returns VBox status code
 * @param   pDevIns     The device instance data.
 * @param   pAhciPort   The port for which the device is to be configured, shared bits.
 * @param   pAhciPortR3 The port for which the device is to be configured, ring-3 bits.
 */
static int ahciR3ConfigureLUN(PPDMDEVINS pDevIns, PAHCIPORT pAhciPort, PAHCIPORTR3 pAhciPortR3)
{
    /* Query the media interface. */
    pAhciPortR3->pDrvMedia = PDMIBASE_QUERY_INTERFACE(pAhciPortR3->pDrvBase, PDMIMEDIA);
    AssertMsgReturn(RT_VALID_PTR(pAhciPortR3->pDrvMedia),
                    ("AHCI configuration error: LUN#%d misses the basic media interface!\n", pAhciPort->iLUN),
                    VERR_PDM_MISSING_INTERFACE);

    /* Get the extended media interface. */
    pAhciPortR3->pDrvMediaEx = PDMIBASE_QUERY_INTERFACE(pAhciPortR3->pDrvBase, PDMIMEDIAEX);
    AssertMsgReturn(RT_VALID_PTR(pAhciPortR3->pDrvMediaEx),
                    ("AHCI configuration error: LUN#%d misses the extended media interface!\n", pAhciPort->iLUN),
                    VERR_PDM_MISSING_INTERFACE);

    /*
     * Validate type.
     */
    PDMMEDIATYPE enmType = pAhciPortR3->pDrvMedia->pfnGetType(pAhciPortR3->pDrvMedia);
    AssertMsgReturn(enmType == PDMMEDIATYPE_HARD_DISK || enmType == PDMMEDIATYPE_CDROM || enmType == PDMMEDIATYPE_DVD,
                    ("AHCI configuration error: LUN#%d isn't a disk or cd/dvd. enmType=%u\n", pAhciPort->iLUN, enmType),
                    VERR_PDM_UNSUPPORTED_BLOCK_TYPE);

    int rc = pAhciPortR3->pDrvMediaEx->pfnIoReqAllocSizeSet(pAhciPortR3->pDrvMediaEx, sizeof(AHCIREQ));
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("AHCI configuration error: LUN#%u: Failed to set I/O request size!"),
                                   pAhciPort->iLUN);

    uint32_t fFeatures = 0;
    rc = pAhciPortR3->pDrvMediaEx->pfnQueryFeatures(pAhciPortR3->pDrvMediaEx, &fFeatures);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("AHCI configuration error: LUN#%u: Failed to query features of device"),
                                   pAhciPort->iLUN);

    if (fFeatures & PDMIMEDIAEX_FEATURE_F_DISCARD)
        pAhciPort->fTrimEnabled = true;

    pAhciPort->fPresent = true;

    pAhciPort->fATAPI =    (enmType == PDMMEDIATYPE_CDROM || enmType == PDMMEDIATYPE_DVD)
                        && RT_BOOL(fFeatures & PDMIMEDIAEX_FEATURE_F_RAWSCSICMD);
    if (pAhciPort->fATAPI)
    {
        pAhciPort->PCHSGeometry.cCylinders = 0;
        pAhciPort->PCHSGeometry.cHeads     = 0;
        pAhciPort->PCHSGeometry.cSectors   = 0;
        LogRel(("AHCI: LUN#%d: CD/DVD\n", pAhciPort->iLUN));
    }
    else
    {
        pAhciPort->cbSector = pAhciPortR3->pDrvMedia->pfnGetSectorSize(pAhciPortR3->pDrvMedia);
        pAhciPort->cTotalSectors = pAhciPortR3->pDrvMedia->pfnGetSize(pAhciPortR3->pDrvMedia) / pAhciPort->cbSector;
        rc = pAhciPortR3->pDrvMedia->pfnBiosGetPCHSGeometry(pAhciPortR3->pDrvMedia, &pAhciPort->PCHSGeometry);
        if (rc == VERR_PDM_MEDIA_NOT_MOUNTED)
        {
            pAhciPort->PCHSGeometry.cCylinders = 0;
            pAhciPort->PCHSGeometry.cHeads     = 16; /*??*/
            pAhciPort->PCHSGeometry.cSectors   = 63; /*??*/
        }
        else if (rc == VERR_PDM_GEOMETRY_NOT_SET)
        {
            pAhciPort->PCHSGeometry.cCylinders = 0; /* autodetect marker */
            rc = VINF_SUCCESS;
        }
        AssertRC(rc);

        if (   pAhciPort->PCHSGeometry.cCylinders == 0
            || pAhciPort->PCHSGeometry.cHeads == 0
            || pAhciPort->PCHSGeometry.cSectors == 0)
        {
            uint64_t cCylinders = pAhciPort->cTotalSectors / (16 * 63);
            pAhciPort->PCHSGeometry.cCylinders = RT_MAX(RT_MIN(cCylinders, 16383), 1);
            pAhciPort->PCHSGeometry.cHeads = 16;
            pAhciPort->PCHSGeometry.cSectors = 63;
            /* Set the disk geometry information. Ignore errors. */
            pAhciPortR3->pDrvMedia->pfnBiosSetPCHSGeometry(pAhciPortR3->pDrvMedia, &pAhciPort->PCHSGeometry);
            rc = VINF_SUCCESS;
        }
        LogRel(("AHCI: LUN#%d: disk, PCHS=%u/%u/%u, total number of sectors %Ld\n",
                 pAhciPort->iLUN, pAhciPort->PCHSGeometry.cCylinders,
                 pAhciPort->PCHSGeometry.cHeads, pAhciPort->PCHSGeometry.cSectors,
                 pAhciPort->cTotalSectors));
        if (pAhciPort->fTrimEnabled)
            LogRel(("AHCI: LUN#%d: Enabled TRIM support\n", pAhciPort->iLUN));
    }
    return rc;
}

/**
 * Callback employed by ahciR3Suspend and ahciR3PowerOff.
 *
 * @returns true if we've quiesced, false if we're still working.
 * @param   pDevIns     The device instance.
 */
static DECLCALLBACK(bool) ahciR3IsAsyncSuspendOrPowerOffDone(PPDMDEVINS pDevIns)
{
    if (!ahciR3AllAsyncIOIsFinished(pDevIns))
        return false;

    PAHCIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAHCICC);
    ASMAtomicWriteBool(&pThisCC->fSignalIdle, false);
    return true;
}

/**
 * Common worker for ahciR3Suspend and ahciR3PowerOff.
 */
static void ahciR3SuspendOrPowerOff(PPDMDEVINS pDevIns)
{
    PAHCIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAHCICC);

    ASMAtomicWriteBool(&pThisCC->fSignalIdle, true);
    if (!ahciR3AllAsyncIOIsFinished(pDevIns))
        PDMDevHlpSetAsyncNotification(pDevIns, ahciR3IsAsyncSuspendOrPowerOffDone);
    else
        ASMAtomicWriteBool(&pThisCC->fSignalIdle, false);

    for (uint32_t i = 0; i < RT_ELEMENTS(pThisCC->aPorts); i++)
    {
        PAHCIPORTR3 pThisPort = &pThisCC->aPorts[i];
        if (pThisPort->pDrvMediaEx)
            pThisPort->pDrvMediaEx->pfnNotifySuspend(pThisPort->pDrvMediaEx);
    }
}

/**
 * Suspend notification.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(void) ahciR3Suspend(PPDMDEVINS pDevIns)
{
    Log(("ahciR3Suspend\n"));
    ahciR3SuspendOrPowerOff(pDevIns);
}

/**
 * Resume notification.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(void) ahciR3Resume(PPDMDEVINS pDevIns)
{
    PAHCI pThis = PDMDEVINS_2_DATA(pDevIns, PAHCI);

    /*
     * Check if one of the ports has pending tasks.
     * Queue a notification item again in this case.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aPorts); i++)
    {
        PAHCIPORT pAhciPort = &pThis->aPorts[i];

        if (pAhciPort->u32TasksRedo)
        {
            pAhciPort->u32TasksNew |= pAhciPort->u32TasksRedo;
            pAhciPort->u32TasksRedo = 0;

            Assert(pAhciPort->fRedo);
            pAhciPort->fRedo = false;

            /* Notify the async IO thread. */
            int rc = PDMDevHlpSUPSemEventSignal(pDevIns, pAhciPort->hEvtProcess);
            AssertRC(rc);
        }
    }

    Log(("%s:\n", __FUNCTION__));
}

/**
 * Initializes the VPD data of a attached device.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pAhciPort       The attached device, shared bits.
 * @param   pAhciPortR3     The attached device, ring-3 bits.
 * @param   pszName         Name of the port to get the CFGM node.
 */
static int ahciR3VpdInit(PPDMDEVINS pDevIns, PAHCIPORT pAhciPort, PAHCIPORTR3 pAhciPortR3, const char *pszName)
{
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;

    /* Generate a default serial number. */
    char szSerial[AHCI_SERIAL_NUMBER_LENGTH+1];
    RTUUID Uuid;

    int rc = VINF_SUCCESS;
    if (pAhciPortR3->pDrvMedia)
        rc = pAhciPortR3->pDrvMedia->pfnGetUuid(pAhciPortR3->pDrvMedia, &Uuid);
    else
        RTUuidClear(&Uuid);

    if (RT_FAILURE(rc) || RTUuidIsNull(&Uuid))
    {
        /* Generate a predictable serial for drives which don't have a UUID. */
        RTStrPrintf(szSerial, sizeof(szSerial), "VB%x-1a2b3c4d", pAhciPort->iLUN);
    }
    else
        RTStrPrintf(szSerial, sizeof(szSerial), "VB%08x-%08x", Uuid.au32[0], Uuid.au32[3]);

    /* Get user config if present using defaults otherwise. */
    PCFGMNODE pCfgNode = pHlp->pfnCFGMGetChild(pDevIns->pCfg, pszName);
    rc = pHlp->pfnCFGMQueryStringDef(pCfgNode, "SerialNumber", pAhciPort->szSerialNumber,
                                     sizeof(pAhciPort->szSerialNumber), szSerial);
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
            return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                    N_("AHCI configuration error: \"SerialNumber\" is longer than 20 bytes"));
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI configuration error: failed to read \"SerialNumber\" as string"));
    }

    rc = pHlp->pfnCFGMQueryStringDef(pCfgNode, "FirmwareRevision", pAhciPort->szFirmwareRevision,
                                     sizeof(pAhciPort->szFirmwareRevision), "1.0");
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
            return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                    N_("AHCI configuration error: \"FirmwareRevision\" is longer than 8 bytes"));
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI configuration error: failed to read \"FirmwareRevision\" as string"));
    }

    rc = pHlp->pfnCFGMQueryStringDef(pCfgNode, "ModelNumber", pAhciPort->szModelNumber, sizeof(pAhciPort->szModelNumber),
                                     pAhciPort->fATAPI ? "VBOX CD-ROM" : "VBOX HARDDISK");
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
            return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                   N_("AHCI configuration error: \"ModelNumber\" is longer than 40 bytes"));
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI configuration error: failed to read \"ModelNumber\" as string"));
    }

    rc = pHlp->pfnCFGMQueryU8Def(pCfgNode, "LogicalSectorsPerPhysical", &pAhciPort->cLogSectorsPerPhysicalExp, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("AHCI configuration error: failed to read \"LogicalSectorsPerPhysical\" as integer"));
    if (pAhciPort->cLogSectorsPerPhysicalExp >= 16)
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("AHCI configuration error: \"LogicalSectorsPerPhysical\" must be between 0 and 15"));

    /* There are three other identification strings for CD drives used for INQUIRY */
    if (pAhciPort->fATAPI)
    {
        rc = pHlp->pfnCFGMQueryStringDef(pCfgNode, "ATAPIVendorId", pAhciPort->szInquiryVendorId,
                                         sizeof(pAhciPort->szInquiryVendorId), "VBOX");
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
                return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                        N_("AHCI configuration error: \"ATAPIVendorId\" is longer than 16 bytes"));
            return PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI configuration error: failed to read \"ATAPIVendorId\" as string"));
        }

        rc = pHlp->pfnCFGMQueryStringDef(pCfgNode, "ATAPIProductId", pAhciPort->szInquiryProductId,
                                         sizeof(pAhciPort->szInquiryProductId), "CD-ROM");
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
                return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                        N_("AHCI configuration error: \"ATAPIProductId\" is longer than 16 bytes"));
            return PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI configuration error: failed to read \"ATAPIProductId\" as string"));
        }

        rc = pHlp->pfnCFGMQueryStringDef(pCfgNode, "ATAPIRevision", pAhciPort->szInquiryRevision,
                                         sizeof(pAhciPort->szInquiryRevision), "1.0");
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
                return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                        N_("AHCI configuration error: \"ATAPIRevision\" is longer than 4 bytes"));
            return PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI configuration error: failed to read \"ATAPIRevision\" as string"));
        }
    }

    return rc;
}


/**
 * Detach notification.
 *
 * One harddisk at one port has been unplugged.
 * The VM is suspended at this point.
 *
 * @param   pDevIns     The device instance.
 * @param   iLUN        The logical unit which is being detached.
 * @param   fFlags      Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 */
static DECLCALLBACK(void) ahciR3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PAHCI       pThis   = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    PAHCIR3     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAHCICC);
    int         rc = VINF_SUCCESS;

    Log(("%s:\n", __FUNCTION__));

    AssertMsgReturnVoid(iLUN < RT_MIN(pThis->cPortsImpl, RT_ELEMENTS(pThisCC->aPorts)), ("iLUN=%u", iLUN));
    PAHCIPORT   pAhciPort   = &pThis->aPorts[iLUN];
    PAHCIPORTR3 pAhciPortR3 = &pThisCC->aPorts[iLUN];
    AssertMsgReturnVoid(   pAhciPort->fHotpluggable
                        || (fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG),
                        ("AHCI: Port %d is not marked hotpluggable\n", pAhciPort->iLUN));


    if (pAhciPortR3->pAsyncIOThread)
    {
        int rcThread;
        /* Destroy the thread. */
        rc = PDMDevHlpThreadDestroy(pDevIns, pAhciPortR3->pAsyncIOThread, &rcThread);
        if (RT_FAILURE(rc) || RT_FAILURE(rcThread))
            AssertMsgFailed(("%s Failed to destroy async IO thread rc=%Rrc rcThread=%Rrc\n", __FUNCTION__, rc, rcThread));

        pAhciPortR3->pAsyncIOThread = NULL;
        pAhciPort->fWrkThreadSleeping = true;
    }

    if (!(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG))
    {
        /*
         * Inform the guest about the removed device.
         */
        pAhciPort->regSSTS = 0;
        pAhciPort->regSIG = 0;
        /*
         * Clear CR bit too to prevent submission of new commands when CI is written
         * (AHCI Spec 1.2: 7.4 Interaction of the Command List and Port Change Status).
         */
        ASMAtomicAndU32(&pAhciPort->regCMD, ~(AHCI_PORT_CMD_CPS | AHCI_PORT_CMD_CR));
        ASMAtomicOrU32(&pAhciPort->regIS, AHCI_PORT_IS_CPDS | AHCI_PORT_IS_PRCS | AHCI_PORT_IS_PCS);
        ASMAtomicOrU32(&pAhciPort->regSERR, AHCI_PORT_SERR_X | AHCI_PORT_SERR_N);
        if (pAhciPort->regIE & (AHCI_PORT_IE_CPDE | AHCI_PORT_IE_PCE | AHCI_PORT_IE_PRCE))
            ahciHbaSetInterrupt(pDevIns, pThis, pAhciPort->iLUN, VERR_IGNORED);
    }

    /*
     * Zero some important members.
     */
    pAhciPortR3->pDrvBase    = NULL;
    pAhciPortR3->pDrvMedia   = NULL;
    pAhciPortR3->pDrvMediaEx = NULL;
    pAhciPort->fPresent      = false;
}

/**
 * Attach command.
 *
 * This is called when we change block driver for one port.
 * The VM is suspended at this point.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   iLUN        The logical unit which is being detached.
 * @param   fFlags      Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 */
static DECLCALLBACK(int)  ahciR3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PAHCI       pThis     = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    PAHCIR3     pThisCC   = PDMDEVINS_2_DATA_CC(pDevIns, PAHCICC);
    int         rc;

    Log(("%s:\n", __FUNCTION__));

    /* the usual paranoia */
    AssertMsgReturn(iLUN < RT_MIN(pThis->cPortsImpl, RT_ELEMENTS(pThisCC->aPorts)), ("iLUN=%u", iLUN), VERR_PDM_LUN_NOT_FOUND);
    PAHCIPORT   pAhciPort   = &pThis->aPorts[iLUN];
    PAHCIPORTR3 pAhciPortR3 = &pThisCC->aPorts[iLUN];
    AssertRelease(!pAhciPortR3->pDrvBase);
    AssertRelease(!pAhciPortR3->pDrvMedia);
    AssertRelease(!pAhciPortR3->pDrvMediaEx);
    Assert(pAhciPort->iLUN == iLUN);
    Assert(pAhciPortR3->iLUN == iLUN);

    AssertMsgReturn(   pAhciPort->fHotpluggable
                    || (fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG),
                    ("AHCI: Port %d is not marked hotpluggable\n", pAhciPort->iLUN),
                    VERR_INVALID_PARAMETER);

    /*
     * Try attach the block device and get the interfaces,
     * required as well as optional.
     */
    rc = PDMDevHlpDriverAttach(pDevIns, pAhciPort->iLUN, &pAhciPortR3->IBase, &pAhciPortR3->pDrvBase, pAhciPortR3->szDesc);
    if (RT_SUCCESS(rc))
        rc = ahciR3ConfigureLUN(pDevIns, pAhciPort, pAhciPortR3);
    else
        AssertMsgFailed(("Failed to attach LUN#%d. rc=%Rrc\n", pAhciPort->iLUN, rc));

    if (RT_FAILURE(rc))
    {
        pAhciPortR3->pDrvBase    = NULL;
        pAhciPortR3->pDrvMedia   = NULL;
        pAhciPortR3->pDrvMediaEx = NULL;
        pAhciPort->fPresent      = false;
    }
    else
    {
        rc = PDMDevHlpSUPSemEventCreate(pDevIns, &pAhciPort->hEvtProcess);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("AHCI: Failed to create SUP event semaphore"));

        /* Create the async IO thread. */
        rc = PDMDevHlpThreadCreate(pDevIns, &pAhciPortR3->pAsyncIOThread, pAhciPortR3, ahciAsyncIOLoop,
                                   ahciAsyncIOLoopWakeUp, 0, RTTHREADTYPE_IO, pAhciPortR3->szDesc);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Init vendor product data.
         */
        if (RT_SUCCESS(rc))
            rc = ahciR3VpdInit(pDevIns, pAhciPort, pAhciPortR3, pAhciPortR3->szDesc);

        /* Inform the guest about the added device in case of hotplugging. */
        if (   RT_SUCCESS(rc)
            && !(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG))
        {
            AssertMsgReturn(pAhciPort->fHotpluggable,
                            ("AHCI: Port %d is not marked hotpluggable\n", pAhciPort->iLUN),
                            VERR_NOT_SUPPORTED);

            /*
             * Initialize registers
             */
            ASMAtomicOrU32(&pAhciPort->regCMD, AHCI_PORT_CMD_CPS);
            ASMAtomicOrU32(&pAhciPort->regIS, AHCI_PORT_IS_CPDS | AHCI_PORT_IS_PRCS | AHCI_PORT_IS_PCS);
            ASMAtomicOrU32(&pAhciPort->regSERR, AHCI_PORT_SERR_X | AHCI_PORT_SERR_N);

            if (pAhciPort->fATAPI)
                pAhciPort->regSIG = AHCI_PORT_SIG_ATAPI;
            else
                pAhciPort->regSIG = AHCI_PORT_SIG_DISK;
            pAhciPort->regSSTS = (0x01 << 8) | /* Interface is active. */
                                 (0x02 << 4) | /* Generation 2 (3.0GBps) speed. */
                                 (0x03 << 0);  /* Device detected and communication established. */

            if (   (pAhciPort->regIE & AHCI_PORT_IE_CPDE)
                || (pAhciPort->regIE & AHCI_PORT_IE_PCE)
                || (pAhciPort->regIE & AHCI_PORT_IE_PRCE))
                ahciHbaSetInterrupt(pDevIns, pThis, pAhciPort->iLUN, VERR_IGNORED);
        }

    }

    return rc;
}

/**
 * Common reset worker.
 *
 * @param   pDevIns     The device instance data.
 */
static int ahciR3ResetCommon(PPDMDEVINS pDevIns)
{
    PAHCI   pThis   = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    PAHCIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAHCICC);
    ahciR3HBAReset(pDevIns, pThis, pThisCC);

    /* Hardware reset for the ports. */
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aPorts); i++)
        ahciPortHwReset(&pThis->aPorts[i]);
    return VINF_SUCCESS;
}

/**
 * Callback employed by ahciR3Reset.
 *
 * @returns true if we've quiesced, false if we're still working.
 * @param   pDevIns     The device instance.
 */
static DECLCALLBACK(bool) ahciR3IsAsyncResetDone(PPDMDEVINS pDevIns)
{
    PAHCIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAHCICC);

    if (!ahciR3AllAsyncIOIsFinished(pDevIns))
        return false;
    ASMAtomicWriteBool(&pThisCC->fSignalIdle, false);

    ahciR3ResetCommon(pDevIns);
    return true;
}

/**
 * Reset notification.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(void) ahciR3Reset(PPDMDEVINS pDevIns)
{
    PAHCIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAHCICC);

    ASMAtomicWriteBool(&pThisCC->fSignalIdle, true);
    if (!ahciR3AllAsyncIOIsFinished(pDevIns))
        PDMDevHlpSetAsyncNotification(pDevIns, ahciR3IsAsyncResetDone);
    else
    {
        ASMAtomicWriteBool(&pThisCC->fSignalIdle, false);
        ahciR3ResetCommon(pDevIns);
    }
}

/**
 * Poweroff notification.
 *
 * @param   pDevIns Pointer to the device instance
 */
static DECLCALLBACK(void) ahciR3PowerOff(PPDMDEVINS pDevIns)
{
    Log(("achiR3PowerOff\n"));
    ahciR3SuspendOrPowerOff(pDevIns);
}

/**
 * Destroy a driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(int) ahciR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PAHCI   pThis   = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    int     rc      = VINF_SUCCESS;

    /*
     * At this point the async I/O thread is suspended and will not enter
     * this module again. So, no coordination is needed here and PDM
     * will take care of terminating and cleaning up the thread.
     */
    if (PDMDevHlpCritSectIsInitialized(pDevIns, &pThis->lock))
    {
        PDMDevHlpTimerDestroy(pDevIns, pThis->hHbaCccTimer);
        pThis->hHbaCccTimer = NIL_TMTIMERHANDLE;

        Log(("%s: Destruct every port\n", __FUNCTION__));
        uint32_t const cPortsImpl = RT_MIN(pThis->cPortsImpl, RT_ELEMENTS(pThis->aPorts));
        for (unsigned iActPort = 0; iActPort < cPortsImpl; iActPort++)
        {
            PAHCIPORT pAhciPort = &pThis->aPorts[iActPort];

            if (pAhciPort->hEvtProcess != NIL_SUPSEMEVENT)
            {
                PDMDevHlpSUPSemEventClose(pDevIns, pAhciPort->hEvtProcess);
                pAhciPort->hEvtProcess = NIL_SUPSEMEVENT;
            }
        }

        PDMDevHlpCritSectDelete(pDevIns, &pThis->lock);
    }

    return rc;
}

/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) ahciR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PAHCI           pThis   = PDMDEVINS_2_DATA(pDevIns, PAHCI);
    PAHCIR3         pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PAHCICC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    PPDMIBASE       pBase;
    int             rc;
    unsigned        i;
    uint32_t        cbTotalBufferSize = 0; /** @todo r=bird: cbTotalBufferSize isn't ever set. */

    LogFlowFunc(("pThis=%#p\n", pThis));
    /*
     * Initialize the instance data (everything touched by the destructor need
     * to be initialized here!).
     */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    PDMPciDevSetVendorId(pPciDev,          0x8086); /* Intel */
    PDMPciDevSetDeviceId(pPciDev,          0x2829); /* ICH-8M */
    PDMPciDevSetCommand(pPciDev,           0x0000);
#ifdef VBOX_WITH_MSI_DEVICES
    PDMPciDevSetStatus(pPciDev,            VBOX_PCI_STATUS_CAP_LIST);
    PDMPciDevSetCapabilityList(pPciDev,    0x80);
#else
    PDMPciDevSetCapabilityList(pPciDev,    0x70);
#endif
    PDMPciDevSetRevisionId(pPciDev,        0x02);
    PDMPciDevSetClassProg(pPciDev,         0x01);
    PDMPciDevSetClassSub(pPciDev,          0x06);
    PDMPciDevSetClassBase(pPciDev,         0x01);
    PDMPciDevSetBaseAddress(pPciDev, 5, false, false, false, 0x00000000);

    PDMPciDevSetInterruptLine(pPciDev,     0x00);
    PDMPciDevSetInterruptPin(pPciDev,      0x01);

    PDMPciDevSetByte(pPciDev,  0x70,       VBOX_PCI_CAP_ID_PM); /* Capability ID: PCI Power Management Interface */
    PDMPciDevSetByte(pPciDev,  0x71,       0xa8); /* next */
    PDMPciDevSetByte(pPciDev,  0x72,       0x03); /* version ? */

    PDMPciDevSetByte(pPciDev,  0x90,       0x40); /* AHCI mode. */
    PDMPciDevSetByte(pPciDev,  0x92,       0x3f);
    PDMPciDevSetByte(pPciDev,  0x94,       0x80);
    PDMPciDevSetByte(pPciDev,  0x95,       0x01);
    PDMPciDevSetByte(pPciDev,  0x97,       0x78);

    PDMPciDevSetByte(pPciDev,  0xa8,       0x12);              /* SATACR capability */
    PDMPciDevSetByte(pPciDev,  0xa9,       0x00);              /* next */
    PDMPciDevSetWord(pPciDev,  0xaa,       0x0010);      /* Revision */
    PDMPciDevSetDWord(pPciDev, 0xac,       0x00000028); /* SATA Capability Register 1 */

    pThis->cThreadsActive = 0;

    pThisCC->pDevIns                 = pDevIns;
    pThisCC->IBase.pfnQueryInterface = ahciR3Status_QueryInterface;
    pThisCC->ILeds.pfnQueryStatusLed = ahciR3Status_QueryStatusLed;

    /* Initialize port members. */
    for (i = 0; i < AHCI_MAX_NR_PORTS_IMPL; i++)
    {
        PAHCIPORT   pAhciPort           = &pThis->aPorts[i];
        PAHCIPORTR3 pAhciPortR3         = &pThisCC->aPorts[i];
        pAhciPortR3->pDevIns            = pDevIns;
        pAhciPort->iLUN                 = i;
        pAhciPortR3->iLUN               = i;
        pAhciPort->Led.u32Magic         = PDMLED_MAGIC;
        pAhciPortR3->pDrvBase           = NULL;
        pAhciPortR3->pAsyncIOThread     = NULL;
        pAhciPort->hEvtProcess          = NIL_SUPSEMEVENT;
        pAhciPort->fHotpluggable        = true;
    }

    /*
     * Init locks, using explicit locking where necessary.
     */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->lock, RT_SRC_POS, "AHCI#%u", iInstance);
    if (RT_FAILURE(rc))
    {
        Log(("%s: Failed to create critical section.\n", __FUNCTION__));
        return rc;
    }

    /*
     * Validate and read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns,
                                  "PrimaryMaster|PrimarySlave|SecondaryMaster"
                                  "|SecondarySlave|PortCount|Bootable|CmdSlotsAvail|TigerHack",
                                  "Port*");

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "PortCount", &pThis->cPortsImpl, AHCI_MAX_NR_PORTS_IMPL);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI configuration error: failed to read PortCount as integer"));
    Log(("%s: cPortsImpl=%u\n", __FUNCTION__, pThis->cPortsImpl));
    if (pThis->cPortsImpl > AHCI_MAX_NR_PORTS_IMPL)
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("AHCI configuration error: PortCount=%u should not exceed %u"),
                                   pThis->cPortsImpl, AHCI_MAX_NR_PORTS_IMPL);
    if (pThis->cPortsImpl < 1)
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("AHCI configuration error: PortCount=%u should be at least 1"),
                                   pThis->cPortsImpl);

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "Bootable", &pThis->fBootable, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("AHCI configuration error: failed to read Bootable as boolean"));

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "CmdSlotsAvail", &pThis->cCmdSlotsAvail, AHCI_NR_COMMAND_SLOTS);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI configuration error: failed to read CmdSlotsAvail as integer"));
    Log(("%s: cCmdSlotsAvail=%u\n", __FUNCTION__, pThis->cCmdSlotsAvail));
    if (pThis->cCmdSlotsAvail > AHCI_NR_COMMAND_SLOTS)
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("AHCI configuration error: CmdSlotsAvail=%u should not exceed %u"),
                                   pThis->cPortsImpl, AHCI_NR_COMMAND_SLOTS);
    if (pThis->cCmdSlotsAvail < 1)
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("AHCI configuration error: CmdSlotsAvail=%u should be at least 1"),
                                   pThis->cCmdSlotsAvail);
    bool fTigerHack;
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "TigerHack", &fTigerHack, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI configuration error: failed to read TigerHack as boolean"));


    /*
     * Register the PCI device, it's I/O regions.
     */
    rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
    if (RT_FAILURE(rc))
        return rc;

#ifdef VBOX_WITH_MSI_DEVICES
    PDMMSIREG MsiReg;
    RT_ZERO(MsiReg);
    MsiReg.cMsiVectors    = 1;
    MsiReg.iMsiCapOffset  = 0x80;
    MsiReg.iMsiNextOffset = 0x70;
    rc = PDMDevHlpPCIRegisterMsi(pDevIns, &MsiReg);
    if (RT_FAILURE(rc))
    {
        PCIDevSetCapabilityList(pPciDev, 0x70);
        /* That's OK, we can work without MSI */
    }
#endif

    /*
     * Solaris 10 U5 fails to map the AHCI register space when the sets (0..3)
     * for the legacy IDE registers are not available.  We set up "fake" entries
     * in the PCI configuration  register.  That means they are available but
     * read and writes from/to them have no effect.  No guest should access them
     * anyway because the controller is marked as AHCI in the Programming
     * interface and we don't have an option to change to IDE emulation (real
     * hardware provides an option in the BIOS to switch to it which also changes
     * device Id and other things in the PCI configuration space).
     */
    rc = PDMDevHlpPCIIORegionCreateIo(pDevIns, 0 /*iPciRegion*/, 8 /*cPorts*/,
                                      ahciLegacyFakeWrite, ahciLegacyFakeRead, NULL /*pvUser*/,
                                      "AHCI Fake #0", NULL /*paExtDescs*/, &pThis->hIoPortsLegacyFake0);
    AssertRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI cannot register PCI I/O region")));

    rc = PDMDevHlpPCIIORegionCreateIo(pDevIns, 1 /*iPciRegion*/, 1 /*cPorts*/,
                                      ahciLegacyFakeWrite, ahciLegacyFakeRead, NULL /*pvUser*/,
                                      "AHCI Fake #1", NULL /*paExtDescs*/, &pThis->hIoPortsLegacyFake1);
    AssertRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI cannot register PCI I/O region")));

    rc = PDMDevHlpPCIIORegionCreateIo(pDevIns, 2 /*iPciRegion*/, 8 /*cPorts*/,
                                      ahciLegacyFakeWrite, ahciLegacyFakeRead, NULL /*pvUser*/,
                                      "AHCI Fake #2", NULL /*paExtDescs*/, &pThis->hIoPortsLegacyFake2);
    AssertRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI cannot register PCI I/O region")));

    rc = PDMDevHlpPCIIORegionCreateIo(pDevIns, 3 /*iPciRegion*/, 1 /*cPorts*/,
                                      ahciLegacyFakeWrite, ahciLegacyFakeRead, NULL /*pvUser*/,
                                      "AHCI Fake #3", NULL /*paExtDescs*/, &pThis->hIoPortsLegacyFake3);
    AssertRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI cannot register PCI I/O region")));

    /*
     * The non-fake PCI I/O regions:
     * Note! The 4352 byte MMIO region will be rounded up to GUEST_PAGE_SIZE.
     */
    rc = PDMDevHlpPCIIORegionCreateIo(pDevIns, 4 /*iPciRegion*/, 0x10 /*cPorts*/,
                                      ahciIdxDataWrite, ahciIdxDataRead, NULL /*pvUser*/,
                                      "AHCI IDX/DATA", NULL /*paExtDescs*/, &pThis->hIoPortIdxData);
    AssertRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI cannot register PCI I/O region for BMDMA")));


    /** @todo change this to IOMMMIO_FLAGS_WRITE_ONLY_DWORD once EM/IOM starts
     * handling 2nd DWORD failures on split accesses correctly. */
    rc = PDMDevHlpPCIIORegionCreateMmio(pDevIns, 5 /*iPciRegion*/, 4352 /*cbRegion*/, PCI_ADDRESS_SPACE_MEM,
                                        ahciMMIOWrite, ahciMMIORead, NULL /*pvUser*/,
                                        IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_QWORD_READ_MISSING,
                                        "AHCI", &pThis->hMmio);
    AssertRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI cannot register PCI memory region for registers")));

    /*
     * Create the timer for command completion coalescing feature.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, ahciCccTimer, pThis,
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0, "AHCI CCC", &pThis->hHbaCccTimer);
    AssertRCReturn(rc, rc);

    /*
     * Initialize ports.
     */

    /* Initialize static members on every port. */
    for (i = 0; i < AHCI_MAX_NR_PORTS_IMPL; i++)
        ahciPortHwReset(&pThis->aPorts[i]);

    /* Attach drivers to every available port. */
    for (i = 0; i < pThis->cPortsImpl; i++)
    {
        PAHCIPORT   pAhciPort   = &pThis->aPorts[i];
        PAHCIPORTR3 pAhciPortR3 = &pThisCC->aPorts[i];

        RTStrPrintf(pAhciPortR3->szDesc, sizeof(pAhciPortR3->szDesc), "Port%u", i);

        /*
         * Init interfaces.
         */
        pAhciPortR3->IBase.pfnQueryInterface                 = ahciR3PortQueryInterface;
        pAhciPortR3->IMediaExPort.pfnIoReqCompleteNotify     = ahciR3IoReqCompleteNotify;
        pAhciPortR3->IMediaExPort.pfnIoReqCopyFromBuf        = ahciR3IoReqCopyFromBuf;
        pAhciPortR3->IMediaExPort.pfnIoReqCopyToBuf          = ahciR3IoReqCopyToBuf;
        pAhciPortR3->IMediaExPort.pfnIoReqQueryBuf           = ahciR3IoReqQueryBuf;
        pAhciPortR3->IMediaExPort.pfnIoReqQueryDiscardRanges = ahciR3IoReqQueryDiscardRanges;
        pAhciPortR3->IMediaExPort.pfnIoReqStateChanged       = ahciR3IoReqStateChanged;
        pAhciPortR3->IMediaExPort.pfnMediumEjected           = ahciR3MediumEjected;
        pAhciPortR3->IPort.pfnQueryDeviceLocation            = ahciR3PortQueryDeviceLocation;
        pAhciPortR3->IPort.pfnQueryScsiInqStrings            = ahciR3PortQueryScsiInqStrings;
        pAhciPort->fWrkThreadSleeping                        = true;

        /* Query per port configuration options if available. */
        PCFGMNODE pCfgPort = pHlp->pfnCFGMGetChild(pDevIns->pCfg, pAhciPortR3->szDesc);
        if (pCfgPort)
        {
            rc = pHlp->pfnCFGMQueryBoolDef(pCfgPort, "Hotpluggable", &pAhciPort->fHotpluggable, true);
            if (RT_FAILURE(rc))
                return PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI configuration error: failed to read Hotpluggable as boolean"));
        }

        /*
         * Attach the block driver
         */
        rc = PDMDevHlpDriverAttach(pDevIns, pAhciPort->iLUN, &pAhciPortR3->IBase, &pAhciPortR3->pDrvBase, pAhciPortR3->szDesc);
        if (RT_SUCCESS(rc))
        {
            rc = ahciR3ConfigureLUN(pDevIns, pAhciPort, pAhciPortR3);
            if (RT_FAILURE(rc))
            {
                Log(("%s: Failed to configure the %s.\n", __FUNCTION__, pAhciPortR3->szDesc));
                return rc;
            }

            /* Mark that a device is present on that port */
            if (i < 6)
                pPciDev->abConfig[0x93] |= (1 << i);

            /*
             * Init vendor product data.
             */
            rc = ahciR3VpdInit(pDevIns, pAhciPort, pAhciPortR3, pAhciPortR3->szDesc);
            if (RT_FAILURE(rc))
                return rc;

            rc = PDMDevHlpSUPSemEventCreate(pDevIns, &pAhciPort->hEvtProcess);
            if (RT_FAILURE(rc))
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                           N_("AHCI: Failed to create SUP event semaphore"));

            rc = PDMDevHlpThreadCreate(pDevIns, &pAhciPortR3->pAsyncIOThread, pAhciPortR3, ahciAsyncIOLoop,
                                       ahciAsyncIOLoopWakeUp, 0, RTTHREADTYPE_IO, pAhciPortR3->szDesc);
            if (RT_FAILURE(rc))
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                           N_("AHCI: Failed to create worker thread %s"), pAhciPortR3->szDesc);
        }
        else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        {
            pAhciPortR3->pDrvBase = NULL;
            pAhciPort->fPresent   = false;
            rc = VINF_SUCCESS;
            LogRel(("AHCI: %s: No driver attached\n", pAhciPortR3->szDesc));
        }
        else
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("AHCI: Failed to attach drive to %s"), pAhciPortR3->szDesc);
    }

    /*
     * Attach status driver (optional).
     */
    rc = PDMDevHlpDriverAttach(pDevIns, PDM_STATUS_LUN, &pThisCC->IBase, &pBase, "Status Port");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pLedsConnector = PDMIBASE_QUERY_INTERFACE(pBase, PDMILEDCONNECTORS);
        pThisCC->pMediaNotify   = PDMIBASE_QUERY_INTERFACE(pBase, PDMIMEDIANOTIFY);
    }
    else
        AssertMsgReturn(rc == VERR_PDM_NO_ATTACHED_DRIVER, ("Failed to attach to status driver. rc=%Rrc\n", rc),
                        PDMDEV_SET_ERROR(pDevIns, rc, N_("AHCI cannot attach to status driver")));

    /*
     * Saved state.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, AHCI_SAVED_STATE_VERSION, sizeof(*pThis) + cbTotalBufferSize, NULL,
                                NULL,           ahciR3LiveExec, NULL,
                                ahciR3SavePrep, ahciR3SaveExec, NULL,
                                ahciR3LoadPrep, ahciR3LoadExec, NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register the info item.
     */
    char szTmp[128];
    RTStrPrintf(szTmp, sizeof(szTmp), "%s%d", pDevIns->pReg->szName, pDevIns->iInstance);
    PDMDevHlpDBGFInfoRegister(pDevIns, szTmp, "AHCI info", ahciR3Info);

    return ahciR3ResetCommon(pDevIns);
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) ahciRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PAHCI pThis = PDMDEVINS_2_DATA(pDevIns, PAHCI);

    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsLegacyFake0, ahciLegacyFakeWrite, ahciLegacyFakeRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsLegacyFake1, ahciLegacyFakeWrite, ahciLegacyFakeRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsLegacyFake2, ahciLegacyFakeWrite, ahciLegacyFakeRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsLegacyFake3, ahciLegacyFakeWrite, ahciLegacyFakeRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortIdxData, ahciIdxDataWrite, ahciIdxDataRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, ahciMMIOWrite, ahciMMIORead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceAHCI =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "ahci",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE
                                    | PDM_DEVREG_FLAGS_FIRST_SUSPEND_NOTIFICATION | PDM_DEVREG_FLAGS_FIRST_POWEROFF_NOTIFICATION
                                    | PDM_DEVREG_FLAGS_FIRST_RESET_NOTIFICATION,
    /* .fClass = */                 PDM_DEVREG_CLASS_STORAGE,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(AHCI),
    /* .cbInstanceCC = */           sizeof(AHCICC),
    /* .cbInstanceRC = */           sizeof(AHCIRC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Intel AHCI controller.\n",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           ahciR3Construct,
    /* .pfnDestruct = */            ahciR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               ahciR3Reset,
    /* .pfnSuspend = */             ahciR3Suspend,
    /* .pfnResume = */              ahciR3Resume,
    /* .pfnAttach = */              ahciR3Attach,
    /* .pfnDetach = */              ahciR3Detach,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            ahciR3PowerOff,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           ahciRZConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           ahciRZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */
