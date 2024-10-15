/* $Id: DevATA.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices: ATA/ATAPI controller device (disk and cdrom).
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_IDE
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmstorageifs.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#ifdef IN_RING3
# include <iprt/mem.h>
# include <iprt/mp.h>
# include <iprt/semaphore.h>
# include <iprt/thread.h>
# include <iprt/time.h>
# include <iprt/uuid.h>
#endif /* IN_RING3 */
#include <iprt/critsect.h>
#include <iprt/asm.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pgm.h>

#include <VBox/sup.h>
#include <VBox/AssertGuest.h>
#include <VBox/scsi.h>
#include <VBox/scsiinline.h>
#include <VBox/ata.h>

#include "ATAPIPassthrough.h"
#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Temporary instrumentation for tracking down potential virtual disk
 * write performance issues. */
#undef VBOX_INSTRUMENT_DMA_WRITES

/** @name The SSM saved state versions.
 * @{
 */
/** The current saved state version. */
#define ATA_SAVED_STATE_VERSION                         21
/** Saved state version without iCurLBA for ATA commands. */
#define ATA_SAVED_STATE_VERSION_WITHOUT_ATA_ILBA        20
/** The saved state version used by VirtualBox 3.0.
 * This lacks the config part and has the type at the and.  */
#define ATA_SAVED_STATE_VERSION_VBOX_30                 19
#define ATA_SAVED_STATE_VERSION_WITH_BOOL_TYPE          18
#define ATA_SAVED_STATE_VERSION_WITHOUT_FULL_SENSE      16
#define ATA_SAVED_STATE_VERSION_WITHOUT_EVENT_STATUS    17
/** @} */

/** Values read from an empty (with no devices attached) ATA bus. */
#define ATA_EMPTY_BUS_DATA      0x7F
#define ATA_EMPTY_BUS_DATA_32   0x7F7F7F7F

/**
 * Maximum number of sectors to transfer in a READ/WRITE MULTIPLE request.
 * Set to 1 to disable multi-sector read support. According to the ATA
 * specification this must be a power of 2 and it must fit in an 8 bit
 * value. Thus the only valid values are 1, 2, 4, 8, 16, 32, 64 and 128.
 */
#define ATA_MAX_MULT_SECTORS 128

/** The maxium I/O buffer size (for sanity). */
#define ATA_MAX_SECTOR_SIZE         _4K
/** The maxium I/O buffer size (for sanity). */
#define ATA_MAX_IO_BUFFER_SIZE      (ATA_MAX_MULT_SECTORS * ATA_MAX_SECTOR_SIZE)

/** Mask to be applied to all indexing into ATACONTROLLER::aIfs. */
#define ATA_SELECTED_IF_MASK        1

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

/** ATAPI sense info size. */
#define ATAPI_SENSE_SIZE 64

/** The maximum number of release log entries per device. */
#define MAX_LOG_REL_ERRORS  1024

/* MediaEventStatus */
#define ATA_EVENT_STATUS_UNCHANGED              0    /**< medium event status not changed */
#define ATA_EVENT_STATUS_MEDIA_EJECT_REQUESTED  1    /**< medium eject requested (eject button pressed) */
#define ATA_EVENT_STATUS_MEDIA_NEW              2    /**< new medium inserted */
#define ATA_EVENT_STATUS_MEDIA_REMOVED          3    /**< medium removed */
#define ATA_EVENT_STATUS_MEDIA_CHANGED          4    /**< medium was removed + new medium was inserted */

/* Media track type */
#define ATA_MEDIA_TYPE_UNKNOWN                  0    /**< unknown CD type */
#define ATA_MEDIA_NO_DISC                    0x70    /**< Door closed, no medium */

/** @defgroup grp_piix3atabmdma     PIIX3 ATA Bus Master DMA
 * @{
 */

/** @name BM_STATUS
 * @{
 */
/** Currently performing a DMA operation. */
#define BM_STATUS_DMAING 0x01
/** An error occurred during the DMA operation. */
#define BM_STATUS_ERROR  0x02
/** The DMA unit has raised the IDE interrupt line. */
#define BM_STATUS_INT    0x04
/** User-defined bit 0, commonly used to signal that drive 0 supports DMA. */
#define BM_STATUS_D0DMA  0x20
/** User-defined bit 1, commonly used to signal that drive 1 supports DMA. */
#define BM_STATUS_D1DMA  0x40
/** @} */

/** @name BM_CMD
 * @{
 */
/** Start the DMA operation. */
#define BM_CMD_START     0x01
/** Data transfer direction: from device to memory if set. */
#define BM_CMD_WRITE     0x08
/** @} */

/** Number of I/O ports per bus-master DMA controller. */
#define BM_DMA_CTL_IOPORTS          8
/** Mask corresponding to BM_DMA_CTL_IOPORTS. */
#define BM_DMA_CTL_IOPORTS_MASK     7
/** Shift count corresponding to BM_DMA_CTL_IOPORTS. */
#define BM_DMA_CTL_IOPORTS_SHIFT    3

/** @} */

#define ATADEVSTATE_2_DEVINS(pIf)              ( (pIf)->CTX_SUFF(pDevIns) )
#define CONTROLLER_2_DEVINS(pController)       ( (pController)->CTX_SUFF(pDevIns) )


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** @defgroup grp_piix3atabmdma     PIIX3 ATA Bus Master DMA
 * @{
 */
/** PIIX3 Bus Master DMA unit state. */
typedef struct BMDMAState
{
    /** Command register. */
    uint8_t     u8Cmd;
    /** Status register. */
    uint8_t     u8Status;
    /** Explicit alignment padding.   */
    uint8_t     abAlignment[2];
    /** Address of the MMIO region in the guest's memory space. */
    RTGCPHYS32  GCPhysAddr;
} BMDMAState;

/** PIIX3 Bus Master DMA descriptor entry. */
typedef struct BMDMADesc
{
    /** Address of the DMA source/target buffer. */
    RTGCPHYS32 GCPhysBuffer;
    /** Size of the DMA source/target buffer. */
    uint32_t   cbBuffer;
} BMDMADesc;
/** @} */


/**
 * The shared state of an ATA device.
 */
typedef struct ATADEVSTATE
{
    /** The I/O buffer.
     * @note Page aligned in case it helps.  */
    uint8_t                             abIOBuffer[ATA_MAX_IO_BUFFER_SIZE];

    /** Flag indicating whether the current command uses LBA48 mode. */
    bool                                fLBA48;
    /** Flag indicating whether this drive implements the ATAPI command set. */
    bool                                fATAPI;
    /** Set if this interface has asserted the IRQ. */
    bool                                fIrqPending;
    /** Currently configured number of sectors in a multi-sector transfer. */
    uint8_t                             cMultSectors;
    /** Physical CHS disk geometry (static). */
    PDMMEDIAGEOMETRY                    PCHSGeometry;
    /** Translated CHS disk geometry (variable). */
    PDMMEDIAGEOMETRY                    XCHSGeometry;
    /** Total number of sectors on this disk. */
    uint64_t                            cTotalSectors;
    /** Sector size of the medium. */
    uint32_t                            cbSector;
    /** Number of sectors to transfer per IRQ. */
    uint32_t                            cSectorsPerIRQ;

    /** ATA/ATAPI register 1: feature (write-only). */
    uint8_t                             uATARegFeature;
    /** ATA/ATAPI register 1: feature, high order byte. */
    uint8_t                             uATARegFeatureHOB;
    /** ATA/ATAPI register 1: error (read-only). */
    uint8_t                             uATARegError;
    /** ATA/ATAPI register 2: sector count (read/write). */
    uint8_t                             uATARegNSector;
    /** ATA/ATAPI register 2: sector count, high order byte. */
    uint8_t                             uATARegNSectorHOB;
    /** ATA/ATAPI register 3: sector (read/write). */
    uint8_t                             uATARegSector;
    /** ATA/ATAPI register 3: sector, high order byte. */
    uint8_t                             uATARegSectorHOB;
    /** ATA/ATAPI register 4: cylinder low (read/write). */
    uint8_t                             uATARegLCyl;
    /** ATA/ATAPI register 4: cylinder low, high order byte. */
    uint8_t                             uATARegLCylHOB;
    /** ATA/ATAPI register 5: cylinder high (read/write). */
    uint8_t                             uATARegHCyl;
    /** ATA/ATAPI register 5: cylinder high, high order byte. */
    uint8_t                             uATARegHCylHOB;
    /** ATA/ATAPI register 6: select drive/head (read/write). */
    uint8_t                             uATARegSelect;
    /** ATA/ATAPI register 7: status (read-only). */
    uint8_t                             uATARegStatus;
    /** ATA/ATAPI register 7: command (write-only). */
    uint8_t                             uATARegCommand;
    /** ATA/ATAPI drive control register (write-only). */
    uint8_t                             uATARegDevCtl;

    /** Currently active transfer mode (MDMA/UDMA) and speed. */
    uint8_t                             uATATransferMode;
    /** Current transfer direction. */
    uint8_t                             uTxDir;
    /** Index of callback for begin transfer. */
    uint8_t                             iBeginTransfer;
    /** Index of callback for source/sink of data. */
    uint8_t                             iSourceSink;
    /** Flag indicating whether the current command transfers data in DMA mode. */
    bool                                fDMA;
    /** Set to indicate that ATAPI transfer semantics must be used. */
    bool                                fATAPITransfer;

    /** Total ATA/ATAPI transfer size, shared PIO/DMA. */
    uint32_t                            cbTotalTransfer;
    /** Elementary ATA/ATAPI transfer size, shared PIO/DMA. */
    uint32_t                            cbElementaryTransfer;
    /** Maximum ATAPI elementary transfer size, PIO only. */
    uint32_t                            cbPIOTransferLimit;
    /** ATAPI passthrough transfer size, shared PIO/DMA */
    uint32_t                            cbAtapiPassthroughTransfer;
    /** Current read/write buffer position, shared PIO/DMA. */
    uint32_t                            iIOBufferCur;
    /** First element beyond end of valid buffer content, shared PIO/DMA. */
    uint32_t                            iIOBufferEnd;
    /** Align the following fields correctly. */
    uint32_t                            Alignment0;

    /** ATA/ATAPI current PIO read/write transfer position. Not shared with DMA for safety reasons. */
    uint32_t                            iIOBufferPIODataStart;
    /** ATA/ATAPI current PIO read/write transfer end. Not shared with DMA for safety reasons. */
    uint32_t                            iIOBufferPIODataEnd;

    /** Current LBA position (both ATA/ATAPI). */
    uint32_t                            iCurLBA;
    /** ATAPI current sector size. */
    uint32_t                            cbATAPISector;
    /** ATAPI current command. */
    uint8_t                             abATAPICmd[ATAPI_PACKET_SIZE];
    /** ATAPI sense data. */
    uint8_t                             abATAPISense[ATAPI_SENSE_SIZE];
    /** HACK: Countdown till we report a newly unmounted drive as mounted. */
    uint8_t                             cNotifiedMediaChange;
    /** The same for GET_EVENT_STATUS for mechanism */
    volatile uint32_t                   MediaEventStatus;

    /** Media type if known. */
    volatile uint32_t                   MediaTrackType;

    /** The status LED state for this drive. */
    PDMLED                              Led;

    /** Size of I/O buffer. */
    uint32_t                            cbIOBuffer;

    /*
     * No data that is part of the saved state after this point!!!!!
     */

    /** Counter for number of busy status seen in R3 in a row. */
    uint8_t                             cBusyStatusHackR3;
    /** Counter for number of busy status seen in GC/R0 in a row. */
    uint8_t                             cBusyStatusHackRZ;
    /** Defines the R3 yield rate by a mask (power of 2 minus one).
     * Lower is more agressive. */
    uint8_t                             cBusyStatusHackR3Rate;
    /** Defines the R0/RC yield rate by a mask (power of 2 minus one).
     * Lower is more agressive. */
    uint8_t                             cBusyStatusHackRZRate;

    /** Release statistics: number of ATA DMA commands. */
    STAMCOUNTER                         StatATADMA;
    /** Release statistics: number of ATA PIO commands. */
    STAMCOUNTER                         StatATAPIO;
    /** Release statistics: number of ATAPI PIO commands. */
    STAMCOUNTER                         StatATAPIDMA;
    /** Release statistics: number of ATAPI PIO commands. */
    STAMCOUNTER                         StatATAPIPIO;
#ifdef VBOX_INSTRUMENT_DMA_WRITES
    /** Release statistics: number of DMA sector writes and the time spent. */
    STAMPROFILEADV                      StatInstrVDWrites;
#endif
    /** Release statistics: Profiling RTThreadYield calls during status polling. */
    STAMPROFILEADV                      StatStatusYields;

    /** Statistics: number of read operations and the time spent reading. */
    STAMPROFILEADV                      StatReads;
    /** Statistics: number of bytes read. */
    STAMCOUNTER                         StatBytesRead;
    /** Statistics: number of write operations and the time spent writing. */
    STAMPROFILEADV                      StatWrites;
    /** Statistics: number of bytes written. */
    STAMCOUNTER                         StatBytesWritten;
    /** Statistics: number of flush operations and the time spend flushing. */
    STAMPROFILE                         StatFlushes;

    /** Enable passing through commands directly to the ATAPI drive. */
    bool                                fATAPIPassthrough;
    /** Flag whether to overwrite inquiry data in passthrough mode. */
    bool                                fOverwriteInquiry;
    /** Number of errors we've reported to the release log.
     * This is to prevent flooding caused by something going horribly wrong.
     * this value against MAX_LOG_REL_ERRORS in places likely to cause floods
     * like the ones we currently seeing on the linux smoke tests (2006-11-10). */
    uint32_t                            cErrors;
    /** Timestamp of last started command. 0 if no command pending. */
    uint64_t                            u64CmdTS;

    /** The LUN number. */
    uint32_t                            iLUN;
    /** The controller number. */
    uint8_t                             iCtl;
    /** The device number. */
    uint8_t                             iDev;
    /** Set if the device is present. */
    bool                                fPresent;
    /** Explicit alignment. */
    uint8_t                             bAlignment2;

    /** The serial number to use for IDENTIFY DEVICE commands. */
    char                                szSerialNumber[ATA_SERIAL_NUMBER_LENGTH+1];
    /** The firmware revision to use for IDENTIFY DEVICE commands. */
    char                                szFirmwareRevision[ATA_FIRMWARE_REVISION_LENGTH+1];
    /** The model number to use for IDENTIFY DEVICE commands. */
    char                                szModelNumber[ATA_MODEL_NUMBER_LENGTH+1];
    /** The vendor identification string for SCSI INQUIRY commands. */
    char                                szInquiryVendorId[SCSI_INQUIRY_VENDOR_ID_LENGTH+1];
    /** The product identification string for SCSI INQUIRY commands. */
    char                                szInquiryProductId[SCSI_INQUIRY_PRODUCT_ID_LENGTH+1];
    /** The revision string for SCSI INQUIRY commands. */
    char                                szInquiryRevision[SCSI_INQUIRY_REVISION_LENGTH+1];

    /** Padding the structure to a multiple of 4096 for better I/O buffer alignment. */
    uint8_t                             abAlignment4[7 + 3528];
} ATADEVSTATE;
AssertCompileMemberAlignment(ATADEVSTATE, cTotalSectors, 8);
AssertCompileMemberAlignment(ATADEVSTATE, StatATADMA, 8);
AssertCompileMemberAlignment(ATADEVSTATE, u64CmdTS, 8);
AssertCompileMemberAlignment(ATADEVSTATE, szSerialNumber, 8);
AssertCompileSizeAlignment(ATADEVSTATE, 4096); /* To align the buffer on a page boundrary. */
/** Pointer to the shared state of an ATA device. */
typedef ATADEVSTATE *PATADEVSTATE;


/**
 * The ring-3 state of an ATA device.
 *
 * @implements PDMIBASE
 * @implements PDMIBLOCKPORT
 * @implements PDMIMOUNTNOTIFY
 */
typedef struct ATADEVSTATER3
{
    /** Pointer to the attached driver's base interface. */
    R3PTRTYPE(PPDMIBASE)                pDrvBase;
    /** Pointer to the attached driver's block interface. */
    R3PTRTYPE(PPDMIMEDIA)               pDrvMedia;
    /** Pointer to the attached driver's mount interface.
     * This is NULL if the driver isn't a removable unit. */
    R3PTRTYPE(PPDMIMOUNT)               pDrvMount;
    /** The base interface. */
    PDMIBASE                            IBase;
    /** The block port interface. */
    PDMIMEDIAPORT                       IPort;
    /** The mount notify interface. */
    PDMIMOUNTNOTIFY                     IMountNotify;

    /** The LUN number. */
    uint32_t                            iLUN;
    /** The controller number. */
    uint8_t                             iCtl;
    /** The device number. */
    uint8_t                             iDev;
    /** Explicit alignment. */
    uint8_t                             abAlignment2[2];
    /** The device instance so we can get our bearings from an interface method. */
    PPDMDEVINSR3                        pDevIns;

    /** The current tracklist of the loaded medium if passthrough is used. */
    R3PTRTYPE(PTRACKLIST)               pTrackList;
} ATADEVSTATER3;
/** Pointer to the ring-3 state of an ATA device. */
typedef ATADEVSTATER3 *PATADEVSTATER3;


/**
 * Transfer request forwarded to the async I/O thread.
 */
typedef struct ATATransferRequest
{
    /** The interface index the request is for. */
    uint8_t  iIf;
    /** The index of the begin transfer callback to call. */
    uint8_t  iBeginTransfer;
    /** The index of the source sink callback to call for doing the transfer. */
    uint8_t  iSourceSink;
    /** Transfer direction. */
    uint8_t  uTxDir;
    /** How many bytes to transfer. */
    uint32_t cbTotalTransfer;
} ATATransferRequest;


/**
 * Abort request forwarded to the async I/O thread.
 */
typedef struct ATAAbortRequest
{
    /** The interface index the request is for. */
    uint8_t iIf;
    /** Flag whether to reset the drive. */
    bool    fResetDrive;
} ATAAbortRequest;


/**
 * Request type indicator.
 */
typedef enum
{
    /** Begin a new transfer. */
    ATA_AIO_NEW = 0,
    /** Continue a DMA transfer. */
    ATA_AIO_DMA,
    /** Continue a PIO transfer. */
    ATA_AIO_PIO,
    /** Reset the drives on current controller, stop all transfer activity. */
    ATA_AIO_RESET_ASSERTED,
    /** Reset the drives on current controller, resume operation. */
    ATA_AIO_RESET_CLEARED,
    /** Abort the current transfer of a particular drive. */
    ATA_AIO_ABORT
} ATAAIO;


/**
 * Combining structure for an ATA request to the async I/O thread
 * started with the request type insicator.
 */
typedef struct ATARequest
{
    /** Request type. */
    ATAAIO                 ReqType;
    /** Request type dependent data. */
    union
    {
        /** Transfer request specific data. */
        ATATransferRequest t;
        /** Abort request specific data. */
        ATAAbortRequest    a;
    } u;
} ATARequest;


/**
 * The shared state of an ATA controller.
 *
 * Has two devices, the master (0) and the slave (1).
 */
typedef struct ATACONTROLLER
{
    /** The ATA/ATAPI interfaces of this controller. */
    ATADEVSTATE         aIfs[2];

    /** The base of the first I/O Port range. */
    RTIOPORT            IOPortBase1;
    /** The base of the second I/O Port range. (0 if none) */
    RTIOPORT            IOPortBase2;
    /** The assigned IRQ. */
    uint32_t            irq;
    /** Access critical section */
    PDMCRITSECT         lock;

    /** Selected drive. */
    uint8_t             iSelectedIf;
    /** The interface on which to handle async I/O. */
    uint8_t             iAIOIf;
    /** The state of the async I/O thread. */
    uint8_t             uAsyncIOState;
    /** Flag indicating whether the next transfer is part of the current command. */
    bool                fChainedTransfer;
    /** Set when the reset processing is currently active on this controller. */
    bool                fReset;
    /** Flag whether the current transfer needs to be redone. */
    bool                fRedo;
    /** Flag whether the redo suspend has been finished. */
    bool                fRedoIdle;
    /** Flag whether the DMA operation to be redone is the final transfer. */
    bool                fRedoDMALastDesc;
    /** The BusMaster DMA state. */
    BMDMAState          BmDma;
    /** Pointer to first DMA descriptor. */
    RTGCPHYS32          GCPhysFirstDMADesc;
    /** Pointer to last DMA descriptor. */
    RTGCPHYS32          GCPhysLastDMADesc;
    /** Pointer to current DMA buffer (for redo operations). */
    RTGCPHYS32          GCPhysRedoDMABuffer;
    /** Size of current DMA buffer (for redo operations). */
    uint32_t            cbRedoDMABuffer;

    /** The event semaphore the thread is waiting on for requests. */
    SUPSEMEVENT         hAsyncIOSem;
    /** The request queue for the AIO thread. One element is always unused. */
    ATARequest          aAsyncIORequests[4];
    /** The position at which to insert a new request for the AIO thread. */
    volatile uint8_t    AsyncIOReqHead;
    /** The position at which to get a new request for the AIO thread. */
    volatile uint8_t    AsyncIOReqTail;
    /** The controller number. */
    uint8_t             iCtl;
    /** Magic delay before triggering interrupts in DMA mode. */
    uint32_t            msDelayIRQ;
    /** The lock protecting the request queue. */
    PDMCRITSECT         AsyncIORequestLock;

    /** Timestamp we started the reset. */
    uint64_t            u64ResetTime;

    /** The first port in the first I/O port range, regular operation. */
    IOMIOPORTHANDLE     hIoPorts1First;
    /** The other ports in the first I/O port range, regular operation. */
    IOMIOPORTHANDLE     hIoPorts1Other;
    /** The second I/O port range, regular operation. */
    IOMIOPORTHANDLE     hIoPorts2;
    /** The first I/O port range, empty controller operation. */
    IOMIOPORTHANDLE     hIoPortsEmpty1;
    /** The second I/O port range, empty controller operation. */
    IOMIOPORTHANDLE     hIoPortsEmpty2;

    /* Statistics */
    STAMCOUNTER         StatAsyncOps;
    uint64_t            StatAsyncMinWait;
    uint64_t            StatAsyncMaxWait;
    STAMCOUNTER         StatAsyncTimeUS;
    STAMPROFILEADV      StatAsyncTime;
    STAMPROFILE         StatLockWait;
    uint8_t             abAlignment4[3328];
} ATACONTROLLER;
AssertCompileMemberAlignment(ATACONTROLLER, lock, 8);
AssertCompileMemberAlignment(ATACONTROLLER, aIfs, 8);
AssertCompileMemberAlignment(ATACONTROLLER, u64ResetTime, 8);
AssertCompileMemberAlignment(ATACONTROLLER, StatAsyncOps, 8);
AssertCompileMemberAlignment(ATACONTROLLER, AsyncIORequestLock, 8);
AssertCompileSizeAlignment(ATACONTROLLER, 4096); /* To align the controllers, devices and I/O buffers on page boundaries. */
/** Pointer to the shared state of an ATA controller. */
typedef ATACONTROLLER *PATACONTROLLER;


/**
 * The ring-3 state of an ATA controller.
 */
typedef struct ATACONTROLLERR3
{
    /** The ATA/ATAPI interfaces of this controller. */
    ATADEVSTATER3       aIfs[2];

    /** Pointer to device instance. */
    PPDMDEVINSR3        pDevIns;

    /** The async I/O thread handle. NIL_RTTHREAD if no thread. */
    RTTHREAD            hAsyncIOThread;
    /** The event semaphore the thread is waiting on during suspended I/O. */
    RTSEMEVENT          hSuspendIOSem;
    /** Set when the destroying the device instance and the thread must exit. */
    uint32_t volatile   fShutdown;
    /** Whether to call PDMDevHlpAsyncNotificationCompleted when idle. */
    bool volatile       fSignalIdle;

    /** The controller number. */
    uint8_t             iCtl;

    uint8_t             abAlignment[3];
} ATACONTROLLERR3;
/** Pointer to the ring-3 state of an ATA controller. */
typedef ATACONTROLLERR3 *PATACONTROLLERR3;


/** ATA chipset type.   */
typedef enum CHIPSET
{
    /** PIIX3 chipset, must be 0 for saved state compatibility */
    CHIPSET_PIIX3 = 0,
    /** PIIX4 chipset, must be 1 for saved state compatibility */
    CHIPSET_PIIX4,
    /** ICH6 chipset */
    CHIPSET_ICH6,
    CHIPSET_32BIT_HACK=0x7fffffff
} CHIPSET;
AssertCompileSize(CHIPSET, 4);

/**
 * The shared state of a ATA PCI device.
 */
typedef struct ATASTATE
{
    /** The controllers. */
    ATACONTROLLER                   aCts[2];
    /** Flag indicating chipset being emulated. */
    CHIPSET                         enmChipset;
    /** Explicit alignment padding. */
    uint8_t                         abAlignment1[7];
    /** PCI region \#4: Bus-master DMA I/O ports. */
    IOMIOPORTHANDLE                 hIoPortsBmDma;
} ATASTATE;
/** Pointer to the shared state of an ATA PCI device. */
typedef ATASTATE *PATASTATE;


/**
 * The ring-3 state of a ATA PCI device.
 *
 * @implements  PDMILEDPORTS
 */
typedef struct ATASTATER3
{
    /** The controllers. */
    ATACONTROLLERR3                 aCts[2];
    /** Status LUN: Base interface. */
    PDMIBASE                        IBase;
    /** Status LUN: Leds interface. */
    PDMILEDPORTS                    ILeds;
    /** Status LUN: Partner of ILeds. */
    R3PTRTYPE(PPDMILEDCONNECTORS)   pLedsConnector;
    /** Status LUN: Media Notify. */
    R3PTRTYPE(PPDMIMEDIANOTIFY)     pMediaNotify;
    /** Pointer to device instance (for getting our bearings in interface methods). */
    PPDMDEVINSR3                    pDevIns;
} ATASTATER3;
/** Pointer to the ring-3 state of an ATA PCI device. */
typedef ATASTATER3 *PATASTATER3;


/**
 * The ring-0 state of the ATA PCI device.
 */
typedef struct ATASTATER0
{
    uint64_t                        uUnused;
} ATASTATER0;
/** Pointer to the ring-0 state of an ATA PCI device. */
typedef ATASTATER0 *PATASTATER0;


/**
 * The raw-mode state of the ATA PCI device.
 */
typedef struct ATASTATERC
{
    uint64_t                        uUnused;
} ATASTATERC;
/** Pointer to the raw-mode state of an ATA PCI device. */
typedef ATASTATERC *PATASTATERC;


/** The current context state of an ATA PCI device. */
typedef CTX_SUFF(ATASTATE) ATASTATECC;
/** Pointer to the current context state of an ATA PCI device. */
typedef CTX_SUFF(PATASTATE) PATASTATECC;


#ifndef VBOX_DEVICE_STRUCT_TESTCASE


#ifdef IN_RING3
DECLINLINE(void) ataSetStatusValue(PATACONTROLLER pCtl, PATADEVSTATE s, uint8_t stat)
{
    /* Freeze status register contents while processing RESET. */
    if (!pCtl->fReset)
    {
        s->uATARegStatus = stat;
        Log2(("%s: LUN#%d status %#04x\n", __FUNCTION__, s->iLUN, s->uATARegStatus));
    }
}
#endif /* IN_RING3 */


DECLINLINE(void) ataSetStatus(PATACONTROLLER pCtl, PATADEVSTATE s, uint8_t stat)
{
    /* Freeze status register contents while processing RESET. */
    if (!pCtl->fReset)
    {
        s->uATARegStatus |= stat;
        Log2(("%s: LUN#%d status %#04x\n", __FUNCTION__, s->iLUN, s->uATARegStatus));
    }
}


DECLINLINE(void) ataUnsetStatus(PATACONTROLLER pCtl, PATADEVSTATE s, uint8_t stat)
{
    /* Freeze status register contents while processing RESET. */
    if (!pCtl->fReset)
    {
        s->uATARegStatus &= ~stat;
        Log2(("%s: LUN#%d status %#04x\n", __FUNCTION__, s->iLUN, s->uATARegStatus));
    }
}

#if defined(IN_RING3) || defined(IN_RING0)

# ifdef IN_RING3
typedef void FNBEGINTRANSFER(PATACONTROLLER pCtl, PATADEVSTATE s);
typedef FNBEGINTRANSFER *PFNBEGINTRANSFER;
typedef bool FNSOURCESINK(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3);
typedef FNSOURCESINK *PFNSOURCESINK;

static FNBEGINTRANSFER ataR3ReadWriteSectorsBT;
static FNBEGINTRANSFER ataR3PacketBT;
static FNBEGINTRANSFER atapiR3CmdBT;
static FNBEGINTRANSFER atapiR3PassthroughCmdBT;

static FNSOURCESINK ataR3IdentifySS;
static FNSOURCESINK ataR3FlushSS;
static FNSOURCESINK ataR3ReadSectorsSS;
static FNSOURCESINK ataR3WriteSectorsSS;
static FNSOURCESINK ataR3ExecuteDeviceDiagnosticSS;
static FNSOURCESINK ataR3TrimSS;
static FNSOURCESINK ataR3PacketSS;
static FNSOURCESINK ataR3InitDevParmSS;
static FNSOURCESINK ataR3RecalibrateSS;
static FNSOURCESINK atapiR3GetConfigurationSS;
static FNSOURCESINK atapiR3GetEventStatusNotificationSS;
static FNSOURCESINK atapiR3IdentifySS;
static FNSOURCESINK atapiR3InquirySS;
static FNSOURCESINK atapiR3MechanismStatusSS;
static FNSOURCESINK atapiR3ModeSenseErrorRecoverySS;
static FNSOURCESINK atapiR3ModeSenseCDStatusSS;
static FNSOURCESINK atapiR3ReadSS;
static FNSOURCESINK atapiR3ReadCapacitySS;
static FNSOURCESINK atapiR3ReadDiscInformationSS;
static FNSOURCESINK atapiR3ReadTOCNormalSS;
static FNSOURCESINK atapiR3ReadTOCMultiSS;
static FNSOURCESINK atapiR3ReadTOCRawSS;
static FNSOURCESINK atapiR3ReadTrackInformationSS;
static FNSOURCESINK atapiR3RequestSenseSS;
static FNSOURCESINK atapiR3PassthroughSS;
static FNSOURCESINK atapiR3ReadDVDStructureSS;
# endif /* IN_RING3 */

/**
 * Begin of transfer function indexes for g_apfnBeginTransFuncs.
 */
typedef enum ATAFNBT
{
    ATAFN_BT_NULL = 0,
    ATAFN_BT_READ_WRITE_SECTORS,
    ATAFN_BT_PACKET,
    ATAFN_BT_ATAPI_CMD,
    ATAFN_BT_ATAPI_PASSTHROUGH_CMD,
    ATAFN_BT_MAX
} ATAFNBT;

# ifdef IN_RING3
/**
 * Array of end transfer functions, the index is ATAFNET.
 * Make sure ATAFNET and this array match!
 */
static const PFNBEGINTRANSFER g_apfnBeginTransFuncs[ATAFN_BT_MAX] =
{
    NULL,
    ataR3ReadWriteSectorsBT,
    ataR3PacketBT,
    atapiR3CmdBT,
    atapiR3PassthroughCmdBT,
};
# endif /* IN_RING3 */

/**
 * Source/sink function indexes for g_apfnSourceSinkFuncs.
 */
typedef enum ATAFNSS
{
    ATAFN_SS_NULL = 0,
    ATAFN_SS_IDENTIFY,
    ATAFN_SS_FLUSH,
    ATAFN_SS_READ_SECTORS,
    ATAFN_SS_WRITE_SECTORS,
    ATAFN_SS_EXECUTE_DEVICE_DIAGNOSTIC,
    ATAFN_SS_TRIM,
    ATAFN_SS_PACKET,
    ATAFN_SS_INITIALIZE_DEVICE_PARAMETERS,
    ATAFN_SS_RECALIBRATE,
    ATAFN_SS_ATAPI_GET_CONFIGURATION,
    ATAFN_SS_ATAPI_GET_EVENT_STATUS_NOTIFICATION,
    ATAFN_SS_ATAPI_IDENTIFY,
    ATAFN_SS_ATAPI_INQUIRY,
    ATAFN_SS_ATAPI_MECHANISM_STATUS,
    ATAFN_SS_ATAPI_MODE_SENSE_ERROR_RECOVERY,
    ATAFN_SS_ATAPI_MODE_SENSE_CD_STATUS,
    ATAFN_SS_ATAPI_READ,
    ATAFN_SS_ATAPI_READ_CAPACITY,
    ATAFN_SS_ATAPI_READ_DISC_INFORMATION,
    ATAFN_SS_ATAPI_READ_TOC_NORMAL,
    ATAFN_SS_ATAPI_READ_TOC_MULTI,
    ATAFN_SS_ATAPI_READ_TOC_RAW,
    ATAFN_SS_ATAPI_READ_TRACK_INFORMATION,
    ATAFN_SS_ATAPI_REQUEST_SENSE,
    ATAFN_SS_ATAPI_PASSTHROUGH,
    ATAFN_SS_ATAPI_READ_DVD_STRUCTURE,
    ATAFN_SS_MAX
} ATAFNSS;

# ifdef IN_RING3
/**
 * Array of source/sink functions, the index is ATAFNSS.
 * Make sure ATAFNSS and this array match!
 */
static const PFNSOURCESINK g_apfnSourceSinkFuncs[ATAFN_SS_MAX] =
{
    NULL,
    ataR3IdentifySS,
    ataR3FlushSS,
    ataR3ReadSectorsSS,
    ataR3WriteSectorsSS,
    ataR3ExecuteDeviceDiagnosticSS,
    ataR3TrimSS,
    ataR3PacketSS,
    ataR3InitDevParmSS,
    ataR3RecalibrateSS,
    atapiR3GetConfigurationSS,
    atapiR3GetEventStatusNotificationSS,
    atapiR3IdentifySS,
    atapiR3InquirySS,
    atapiR3MechanismStatusSS,
    atapiR3ModeSenseErrorRecoverySS,
    atapiR3ModeSenseCDStatusSS,
    atapiR3ReadSS,
    atapiR3ReadCapacitySS,
    atapiR3ReadDiscInformationSS,
    atapiR3ReadTOCNormalSS,
    atapiR3ReadTOCMultiSS,
    atapiR3ReadTOCRawSS,
    atapiR3ReadTrackInformationSS,
    atapiR3RequestSenseSS,
    atapiR3PassthroughSS,
    atapiR3ReadDVDStructureSS
};
# endif /* IN_RING3 */


static const ATARequest g_ataDMARequest    = { ATA_AIO_DMA,            { { 0, 0, 0, 0, 0 } } };
static const ATARequest g_ataPIORequest    = { ATA_AIO_PIO,            { { 0, 0, 0, 0, 0 } } };
# ifdef IN_RING3
static const ATARequest g_ataResetARequest = { ATA_AIO_RESET_ASSERTED, { { 0, 0, 0, 0, 0 } } };
static const ATARequest g_ataResetCRequest = { ATA_AIO_RESET_CLEARED,  { { 0, 0, 0, 0, 0 } } };
# endif

# ifdef IN_RING3
static void ataR3AsyncIOClearRequests(PPDMDEVINS pDevIns, PATACONTROLLER pCtl)
{
    int rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->AsyncIORequestLock, VINF_SUCCESS);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pCtl->AsyncIORequestLock, rc);

    pCtl->AsyncIOReqHead = 0;
    pCtl->AsyncIOReqTail = 0;

    rc = PDMDevHlpCritSectLeave(pDevIns, &pCtl->AsyncIORequestLock);
    AssertRC(rc);
}
# endif /* IN_RING3 */

static void ataHCAsyncIOPutRequest(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, const ATARequest *pReq)
{
    int rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->AsyncIORequestLock, VINF_SUCCESS);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pCtl->AsyncIORequestLock, rc);

    uint8_t const iAsyncIORequest = pCtl->AsyncIOReqHead % RT_ELEMENTS(pCtl->aAsyncIORequests);
    Assert((iAsyncIORequest + 1) % RT_ELEMENTS(pCtl->aAsyncIORequests) != pCtl->AsyncIOReqTail);
    memcpy(&pCtl->aAsyncIORequests[iAsyncIORequest], pReq, sizeof(*pReq));
    pCtl->AsyncIOReqHead = (iAsyncIORequest + 1) % RT_ELEMENTS(pCtl->aAsyncIORequests);

    rc = PDMDevHlpCritSectLeave(pDevIns, &pCtl->AsyncIORequestLock);
    AssertRC(rc);

    rc = PDMDevHlpCritSectScheduleExitEvent(pDevIns, &pCtl->lock, pCtl->hAsyncIOSem);
    if (RT_FAILURE(rc))
    {
        rc = PDMDevHlpSUPSemEventSignal(pDevIns, pCtl->hAsyncIOSem);
        AssertRC(rc);
    }
}

# ifdef IN_RING3

static const ATARequest *ataR3AsyncIOGetCurrentRequest(PPDMDEVINS pDevIns, PATACONTROLLER pCtl)
{
    const ATARequest *pReq;

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->AsyncIORequestLock, VINF_SUCCESS);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pCtl->AsyncIORequestLock, rc);

    if (pCtl->AsyncIOReqHead != pCtl->AsyncIOReqTail)
        pReq = &pCtl->aAsyncIORequests[pCtl->AsyncIOReqTail];
    else
        pReq = NULL;

    rc = PDMDevHlpCritSectLeave(pDevIns, &pCtl->AsyncIORequestLock);
    AssertRC(rc);
    return pReq;
}


/**
 * Remove the request with the given type, as it's finished. The request
 * is not removed blindly, as this could mean a RESET request that is not
 * yet processed (but has cleared the request queue) is lost.
 *
 * @param pDevIns   The device instance.
 * @param pCtl      Controller for which to remove the request.
 * @param ReqType   Type of the request to remove.
 */
static void ataR3AsyncIORemoveCurrentRequest(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, ATAAIO ReqType)
{
    int rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->AsyncIORequestLock, VINF_SUCCESS);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pCtl->AsyncIORequestLock, rc);

    if (pCtl->AsyncIOReqHead != pCtl->AsyncIOReqTail && pCtl->aAsyncIORequests[pCtl->AsyncIOReqTail].ReqType == ReqType)
    {
        pCtl->AsyncIOReqTail++;
        pCtl->AsyncIOReqTail %= RT_ELEMENTS(pCtl->aAsyncIORequests);
    }

    rc = PDMDevHlpCritSectLeave(pDevIns, &pCtl->AsyncIORequestLock);
    AssertRC(rc);
}


/**
 * Dump the request queue for a particular controller. First dump the queue
 * contents, then the already processed entries, as long as they haven't been
 * overwritten.
 *
 * @param pDevIns   The device instance.
 * @param pCtl      Controller for which to dump the queue.
 */
static void ataR3AsyncIODumpRequests(PPDMDEVINS pDevIns, PATACONTROLLER pCtl)
{
    int rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->AsyncIORequestLock, VINF_SUCCESS);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pCtl->AsyncIORequestLock, rc);

    LogRel(("PIIX3 ATA: Ctl#%d: request queue dump (topmost is current):\n", pCtl->iCtl));
    uint8_t curr = pCtl->AsyncIOReqTail;
    do
    {
        if (curr == pCtl->AsyncIOReqHead)
            LogRel(("PIIX3 ATA: Ctl#%d: processed requests (topmost is oldest):\n", pCtl->iCtl));
        switch (pCtl->aAsyncIORequests[curr].ReqType)
        {
            case ATA_AIO_NEW:
                LogRel(("new transfer request, iIf=%d iBeginTransfer=%d iSourceSink=%d cbTotalTransfer=%d uTxDir=%d\n",
                        pCtl->aAsyncIORequests[curr].u.t.iIf, pCtl->aAsyncIORequests[curr].u.t.iBeginTransfer,
                        pCtl->aAsyncIORequests[curr].u.t.iSourceSink, pCtl->aAsyncIORequests[curr].u.t.cbTotalTransfer,
                        pCtl->aAsyncIORequests[curr].u.t.uTxDir));
                break;
            case ATA_AIO_DMA:
                LogRel(("dma transfer continuation\n"));
                break;
            case ATA_AIO_PIO:
                LogRel(("pio transfer continuation\n"));
                break;
            case ATA_AIO_RESET_ASSERTED:
                LogRel(("reset asserted request\n"));
                break;
            case ATA_AIO_RESET_CLEARED:
                LogRel(("reset cleared request\n"));
                break;
            case ATA_AIO_ABORT:
                LogRel(("abort request, iIf=%d fResetDrive=%d\n", pCtl->aAsyncIORequests[curr].u.a.iIf,
                                                                  pCtl->aAsyncIORequests[curr].u.a.fResetDrive));
                break;
            default:
                LogRel(("unknown request %d\n", pCtl->aAsyncIORequests[curr].ReqType));
        }
        curr = (curr + 1) % RT_ELEMENTS(pCtl->aAsyncIORequests);
    } while (curr != pCtl->AsyncIOReqTail);

    rc = PDMDevHlpCritSectLeave(pDevIns, &pCtl->AsyncIORequestLock);
    AssertRC(rc);
}


/**
 * Checks whether the request queue for a particular controller is empty
 * or whether a particular controller is idle.
 *
 * @param pDevIns   The device instance.
 * @param pCtl      Controller for which to check the queue.
 * @param fStrict   If set then the controller is checked to be idle.
 */
static bool ataR3AsyncIOIsIdle(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, bool fStrict)
{
    int rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->AsyncIORequestLock, VINF_SUCCESS);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pCtl->AsyncIORequestLock, rc);

    bool fIdle = pCtl->fRedoIdle;
    if (!fIdle)
        fIdle = (pCtl->AsyncIOReqHead == pCtl->AsyncIOReqTail);
    if (fStrict)
        fIdle &= (pCtl->uAsyncIOState == ATA_AIO_NEW);

    rc = PDMDevHlpCritSectLeave(pDevIns, &pCtl->AsyncIORequestLock);
    AssertRC(rc);
    return fIdle;
}


/**
 * Send a transfer request to the async I/O thread.
 *
 * @param   pDevIns             The device instance.
 * @param   pCtl                The ATA controller.
 * @param   s                   Pointer to the ATA device state data.
 * @param   cbTotalTransfer     Data transfer size.
 * @param   uTxDir              Data transfer direction.
 * @param   iBeginTransfer      Index of BeginTransfer callback.
 * @param   iSourceSink         Index of SourceSink callback.
 * @param   fChainedTransfer    Whether this is a transfer that is part of the previous command/transfer.
 */
static void ataR3StartTransfer(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s,
                               uint32_t cbTotalTransfer, uint8_t uTxDir, ATAFNBT iBeginTransfer,
                               ATAFNSS iSourceSink, bool fChainedTransfer)
{
    ATARequest Req;

    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pCtl->lock));

    /* Do not issue new requests while the RESET line is asserted. */
    if (pCtl->fReset)
    {
        Log2(("%s: Ctl#%d: suppressed new request as RESET is active\n", __FUNCTION__, pCtl->iCtl));
        return;
    }

    /* If the controller is already doing something else right now, ignore
     * the command that is being submitted. Some broken guests issue commands
     * twice (e.g. the Linux kernel that comes with Acronis True Image 8). */
    if (!fChainedTransfer && !ataR3AsyncIOIsIdle(pDevIns, pCtl, true /*fStrict*/))
    {
        Log(("%s: Ctl#%d: ignored command %#04x, controller state %d\n", __FUNCTION__, pCtl->iCtl, s->uATARegCommand, pCtl->uAsyncIOState));
        LogRel(("PIIX3 IDE: guest issued command %#04x while controller busy\n", s->uATARegCommand));
        return;
    }

    Req.ReqType = ATA_AIO_NEW;
    if (fChainedTransfer)
        Req.u.t.iIf = pCtl->iAIOIf;
    else
        Req.u.t.iIf = pCtl->iSelectedIf;
    Req.u.t.cbTotalTransfer = cbTotalTransfer;
    Req.u.t.uTxDir = uTxDir;
    Req.u.t.iBeginTransfer = iBeginTransfer;
    Req.u.t.iSourceSink = iSourceSink;
    ataSetStatusValue(pCtl, s, ATA_STAT_BUSY);
    pCtl->fChainedTransfer = fChainedTransfer;

    /*
     * Kick the worker thread into action.
     */
    Log2(("%s: Ctl#%d: message to async I/O thread, new request\n", __FUNCTION__, pCtl->iCtl));
    ataHCAsyncIOPutRequest(pDevIns, pCtl, &Req);
}


/**
 * Send an abort command request to the async I/O thread.
 *
 * @param   pDevIns     The device instance.
 * @param   pCtl        The ATA controller.
 * @param   s           Pointer to the ATA device state data.
 * @param   fResetDrive Whether to reset the drive or just abort a command.
 */
static void ataR3AbortCurrentCommand(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, bool fResetDrive)
{
    ATARequest Req;

    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pCtl->lock));

    /* Do not issue new requests while the RESET line is asserted. */
    if (pCtl->fReset)
    {
        Log2(("%s: Ctl#%d: suppressed aborting command as RESET is active\n", __FUNCTION__, pCtl->iCtl));
        return;
    }

    Req.ReqType = ATA_AIO_ABORT;
    Req.u.a.iIf = pCtl->iSelectedIf;
    Req.u.a.fResetDrive = fResetDrive;
    ataSetStatus(pCtl, s, ATA_STAT_BUSY);
    Log2(("%s: Ctl#%d: message to async I/O thread, abort command on LUN#%d\n", __FUNCTION__, pCtl->iCtl, s->iLUN));
    ataHCAsyncIOPutRequest(pDevIns, pCtl, &Req);
}

# endif /* IN_RING3 */

/**
 * Set the internal interrupt pending status, update INTREQ as appropriate.
 *
 * @param   pDevIns     The device instance.
 * @param   pCtl        The ATA controller.
 * @param   s           Pointer to the ATA device state data.
 */
static void ataHCSetIRQ(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s)
{
    if (!s->fIrqPending)
    {
        if (!(s->uATARegDevCtl & ATA_DEVCTL_DISABLE_IRQ))
        {
            Log2(("%s: LUN#%d asserting IRQ\n", __FUNCTION__, s->iLUN));
            /* The BMDMA unit unconditionally sets BM_STATUS_INT if the interrupt
             * line is asserted. It monitors the line for a rising edge. */
            pCtl->BmDma.u8Status |= BM_STATUS_INT;
            /* Only actually set the IRQ line if updating the currently selected drive. */
            if (s == &pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK])
            {
                /** @todo experiment with adaptive IRQ delivery: for reads it is
                 * better to wait for IRQ delivery, as it reduces latency. */
                if (pCtl->irq == 16)
                    PDMDevHlpPCISetIrq(pDevIns, 0, 1);
                else
                    PDMDevHlpISASetIrq(pDevIns, pCtl->irq, 1);
            }
        }
        s->fIrqPending = true;
    }
}

#endif /* IN_RING0 || IN_RING3 */

/**
 * Clear the internal interrupt pending status, update INTREQ as appropriate.
 *
 * @param   pDevIns     The device instance.
 * @param   pCtl        The ATA controller.
 * @param   s           Pointer to the ATA device state data.
 */
static void ataUnsetIRQ(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s)
{
    if (s->fIrqPending)
    {
        if (!(s->uATARegDevCtl & ATA_DEVCTL_DISABLE_IRQ))
        {
            Log2(("%s: LUN#%d deasserting IRQ\n", __FUNCTION__, s->iLUN));
            /* Only actually unset the IRQ line if updating the currently selected drive. */
            if (s == &pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK])
            {
                if (pCtl->irq == 16)
                    PDMDevHlpPCISetIrq(pDevIns, 0, 0);
                else
                    PDMDevHlpISASetIrq(pDevIns, pCtl->irq, 0);
            }
        }
        s->fIrqPending = false;
    }
}

#if defined(IN_RING0) || defined(IN_RING3)

static void ataHCPIOTransferStart(PATACONTROLLER pCtl, PATADEVSTATE s, uint32_t start, uint32_t size)
{
    Log2(("%s: LUN#%d start %d size %d\n", __FUNCTION__, s->iLUN, start, size));
    s->iIOBufferPIODataStart = start;
    s->iIOBufferPIODataEnd = start + size;
    ataSetStatus(pCtl, s, ATA_STAT_DRQ | ATA_STAT_SEEK);
    ataUnsetStatus(pCtl, s, ATA_STAT_BUSY);
}


static void ataHCPIOTransferStop(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s)
{
    Log2(("%s: LUN#%d\n", __FUNCTION__, s->iLUN));
    if (s->fATAPITransfer)
    {
        s->uATARegNSector = (s->uATARegNSector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
        Log2(("%s: interrupt reason %#04x\n", __FUNCTION__, s->uATARegNSector));
        ataHCSetIRQ(pDevIns, pCtl, s);
        s->fATAPITransfer = false;
    }
    s->cbTotalTransfer = 0;
    s->cbElementaryTransfer = 0;
    s->iIOBufferPIODataStart = 0;
    s->iIOBufferPIODataEnd = 0;
    s->iBeginTransfer = ATAFN_BT_NULL;
    s->iSourceSink = ATAFN_SS_NULL;
}


static void ataHCPIOTransferLimitATAPI(PATADEVSTATE s)
{
    uint32_t cbLimit, cbTransfer;

    cbLimit = s->cbPIOTransferLimit;
    /* Use maximum transfer size if the guest requested 0. Avoids a hang. */
    if (cbLimit == 0)
        cbLimit = 0xfffe;
    Log2(("%s: byte count limit=%d\n", __FUNCTION__, cbLimit));
    if (cbLimit == 0xffff)
        cbLimit--;
    cbTransfer = RT_MIN(s->cbTotalTransfer, s->iIOBufferEnd - s->iIOBufferCur);
    if (cbTransfer > cbLimit)
    {
        /* Byte count limit for clipping must be even in this case */
        if (cbLimit & 1)
            cbLimit--;
        cbTransfer = cbLimit;
    }
    s->uATARegLCyl = cbTransfer;
    s->uATARegHCyl = cbTransfer >> 8;
    s->cbElementaryTransfer = cbTransfer;
}

# ifdef IN_RING3

/**
 * Enters the lock protecting the controller data against concurrent access.
 *
 * @param   pDevIns     The device instance.
 * @param   pCtl        The controller to lock.
 */
DECLINLINE(void) ataR3LockEnter(PPDMDEVINS pDevIns, PATACONTROLLER pCtl)
{
    STAM_PROFILE_START(&pCtl->StatLockWait, a);
    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pCtl->lock, VINF_SUCCESS);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pCtl->lock, rcLock);
    STAM_PROFILE_STOP(&pCtl->StatLockWait, a);
}

/**
 * Leaves the lock protecting the controller against concurrent data access.
 *
 * @param   pDevIns     The device instance.
 * @param   pCtl        The controller to unlock.
 */
DECLINLINE(void) ataR3LockLeave(PPDMDEVINS pDevIns, PATACONTROLLER pCtl)
{
    PDMDevHlpCritSectLeave(pDevIns, &pCtl->lock);
}

static uint32_t ataR3GetNSectors(PATADEVSTATE s)
{
    /* 0 means either 256 (LBA28) or 65536 (LBA48) sectors. */
    if (s->fLBA48)
    {
        if (!s->uATARegNSector && !s->uATARegNSectorHOB)
            return 65536;
        else
            return s->uATARegNSectorHOB << 8 | s->uATARegNSector;
    }
    else
    {
        if (!s->uATARegNSector)
            return 256;
        else
            return s->uATARegNSector;
    }
}


static void ataR3PadString(uint8_t *pbDst, const char *pbSrc, uint32_t cbSize)
{
    for (uint32_t i = 0; i < cbSize; i++)
    {
        if (*pbSrc)
            pbDst[i ^ 1] = *pbSrc++;
        else
            pbDst[i ^ 1] = ' ';
    }
}


#if 0 /* unused */
/**
 * Compares two MSF values.
 *
 * @returns 1  if the first value is greater than the second value.
 *          0  if both are equal
 *          -1 if the first value is smaller than the second value.
 */
DECLINLINE(int) atapiCmpMSF(const uint8_t *pbMSF1, const uint8_t *pbMSF2)
{
    int iRes = 0;

    for (unsigned i = 0; i < 3; i++)
    {
        if (pbMSF1[i] < pbMSF2[i])
        {
            iRes = -1;
            break;
        }
        else if (pbMSF1[i] > pbMSF2[i])
        {
            iRes = 1;
            break;
        }
    }

    return iRes;
}
#endif /* unused */

static void ataR3CmdOK(PATACONTROLLER pCtl, PATADEVSTATE s, uint8_t status)
{
    s->uATARegError = 0; /* Not needed by ATA spec, but cannot hurt. */
    ataSetStatusValue(pCtl, s, ATA_STAT_READY | status);
}


static void ataR3CmdError(PATACONTROLLER pCtl, PATADEVSTATE s, uint8_t uErrorCode)
{
    Log(("%s: code=%#x\n", __FUNCTION__, uErrorCode));
    Assert(uErrorCode);
    s->uATARegError = uErrorCode;
    ataSetStatusValue(pCtl, s, ATA_STAT_READY | ATA_STAT_SEEK | ATA_STAT_ERR);
    s->cbTotalTransfer = 0;
    s->cbElementaryTransfer = 0;
    s->iIOBufferCur = 0;
    s->iIOBufferEnd = 0;
    s->uTxDir = PDMMEDIATXDIR_NONE;
    s->iBeginTransfer = ATAFN_BT_NULL;
    s->iSourceSink = ATAFN_SS_NULL;
}

static uint32_t ataR3Checksum(void* ptr, size_t count)
{
    uint8_t u8Sum = 0xa5, *p = (uint8_t*)ptr;
    size_t i;

    for (i = 0; i < count; i++)
    {
      u8Sum += *p++;
    }

    return (uint8_t)-(int32_t)u8Sum;
}

/**
 * Sink/Source: IDENTIFY
 */
static bool ataR3IdentifySS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint16_t *p;
    RT_NOREF(pDevIns);

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    Assert(s->cbElementaryTransfer == 512);

    p = (uint16_t *)&s->abIOBuffer[0];
    memset(p, 0, 512);
    p[0] = RT_H2LE_U16(0x0040);
    p[1] = RT_H2LE_U16(RT_MIN(s->PCHSGeometry.cCylinders, 16383));
    p[3] = RT_H2LE_U16(s->PCHSGeometry.cHeads);
    /* Block size; obsolete, but required for the BIOS. */
    p[5] = RT_H2LE_U16(s->cbSector);
    p[6] = RT_H2LE_U16(s->PCHSGeometry.cSectors);
    ataR3PadString((uint8_t *)(p + 10), s->szSerialNumber, ATA_SERIAL_NUMBER_LENGTH); /* serial number */
    p[20] = RT_H2LE_U16(3); /* XXX: retired, cache type */
    p[21] = RT_H2LE_U16(512); /* XXX: retired, cache size in sectors */
    p[22] = RT_H2LE_U16(0); /* ECC bytes per sector */
    ataR3PadString((uint8_t *)(p + 23), s->szFirmwareRevision, ATA_FIRMWARE_REVISION_LENGTH); /* firmware version */
    ataR3PadString((uint8_t *)(p + 27), s->szModelNumber, ATA_MODEL_NUMBER_LENGTH); /* model */
# if ATA_MAX_MULT_SECTORS > 1
    p[47] = RT_H2LE_U16(0x8000 | ATA_MAX_MULT_SECTORS);
# endif
    p[48] = RT_H2LE_U16(1); /* dword I/O, used by the BIOS */
    p[49] = RT_H2LE_U16(1 << 11 | 1 << 9 | 1 << 8); /* DMA and LBA supported */
    p[50] = RT_H2LE_U16(1 << 14); /* No drive specific standby timer minimum */
    p[51] = RT_H2LE_U16(240); /* PIO transfer cycle */
    p[52] = RT_H2LE_U16(240); /* DMA transfer cycle */
    p[53] = RT_H2LE_U16(1 | 1 << 1 | 1 << 2); /* words 54-58,64-70,88 valid */
    p[54] = RT_H2LE_U16(RT_MIN(s->XCHSGeometry.cCylinders, 16383));
    p[55] = RT_H2LE_U16(s->XCHSGeometry.cHeads);
    p[56] = RT_H2LE_U16(s->XCHSGeometry.cSectors);
    p[57] = RT_H2LE_U16(  RT_MIN(s->XCHSGeometry.cCylinders, 16383)
                        * s->XCHSGeometry.cHeads
                        * s->XCHSGeometry.cSectors);
    p[58] = RT_H2LE_U16(  RT_MIN(s->XCHSGeometry.cCylinders, 16383)
                        * s->XCHSGeometry.cHeads
                        * s->XCHSGeometry.cSectors >> 16);
    if (s->cMultSectors)
        p[59] = RT_H2LE_U16(0x100 | s->cMultSectors);
    if (s->cTotalSectors <= (1 << 28) - 1)
    {
        p[60] = RT_H2LE_U16(s->cTotalSectors);
        p[61] = RT_H2LE_U16(s->cTotalSectors >> 16);
    }
    else
    {
        /* Report maximum number of sectors possible with LBA28 */
        p[60] = RT_H2LE_U16(((1 << 28) - 1) & 0xffff);
        p[61] = RT_H2LE_U16(((1 << 28) - 1) >> 16);
    }
    p[63] = RT_H2LE_U16(ATA_TRANSFER_ID(ATA_MODE_MDMA, ATA_MDMA_MODE_MAX, s->uATATransferMode)); /* MDMA modes supported / mode enabled */
    p[64] = RT_H2LE_U16(ATA_PIO_MODE_MAX > 2 ? (1 << (ATA_PIO_MODE_MAX - 2)) - 1 : 0); /* PIO modes beyond PIO2 supported */
    p[65] = RT_H2LE_U16(120); /* minimum DMA multiword tx cycle time */
    p[66] = RT_H2LE_U16(120); /* recommended DMA multiword tx cycle time */
    p[67] = RT_H2LE_U16(120); /* minimum PIO cycle time without flow control */
    p[68] = RT_H2LE_U16(120); /* minimum PIO cycle time with IORDY flow control */
    if (   pDevR3->pDrvMedia->pfnDiscard
        || s->cbSector != 512
        || pDevR3->pDrvMedia->pfnIsNonRotational(pDevR3->pDrvMedia))
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
    if (s->cTotalSectors <= (1 << 28) - 1)
        p[83] = RT_H2LE_U16(1 << 14 | 1 << 12); /* supports FLUSH CACHE */
    else
        p[83] = RT_H2LE_U16(1 << 14 | 1 << 10 | 1 << 12 | 1 << 13); /* supports LBA48, FLUSH CACHE and FLUSH CACHE EXT */
    p[84] = RT_H2LE_U16(1 << 14);
    p[85] = RT_H2LE_U16(1 << 3 | 1 << 5 | 1 << 6); /* enabled power management,  write cache and look-ahead */
    if (s->cTotalSectors <= (1 << 28) - 1)
        p[86] = RT_H2LE_U16(1 << 12); /* enabled FLUSH CACHE */
    else
        p[86] = RT_H2LE_U16(1 << 10 | 1 << 12 | 1 << 13); /* enabled LBA48, FLUSH CACHE and FLUSH CACHE EXT */
    p[87] = RT_H2LE_U16(1 << 14);
    p[88] = RT_H2LE_U16(ATA_TRANSFER_ID(ATA_MODE_UDMA, ATA_UDMA_MODE_MAX, s->uATATransferMode)); /* UDMA modes supported / mode enabled */
    p[93] = RT_H2LE_U16((1 | 1 << 1) << ((s->iLUN & 1) == 0 ? 0 : 8) | 1 << 13 | 1 << 14);
    if (s->cTotalSectors > (1 << 28) - 1)
    {
        p[100] = RT_H2LE_U16(s->cTotalSectors);
        p[101] = RT_H2LE_U16(s->cTotalSectors >> 16);
        p[102] = RT_H2LE_U16(s->cTotalSectors >> 32);
        p[103] = RT_H2LE_U16(s->cTotalSectors >> 48);
    }

    if (s->cbSector != 512)
    {
        uint32_t cSectorSizeInWords = s->cbSector / sizeof(uint16_t);
        /* Enable reporting of logical sector size. */
        p[106] |= RT_H2LE_U16(RT_BIT(12) | RT_BIT(14));
        p[117] = RT_H2LE_U16(cSectorSizeInWords);
        p[118] = RT_H2LE_U16(cSectorSizeInWords >> 16);
    }

    if (pDevR3->pDrvMedia->pfnDiscard) /** @todo Set bit 14 in word 69 too? (Deterministic read after TRIM). */
        p[169] = RT_H2LE_U16(1); /* DATA SET MANAGEMENT command supported. */
    if (pDevR3->pDrvMedia->pfnIsNonRotational(pDevR3->pDrvMedia))
        p[217] = RT_H2LE_U16(1); /* Non-rotational medium */
    uint32_t uCsum = ataR3Checksum(p, 510);
    p[255] = RT_H2LE_U16(0xa5 | (uCsum << 8)); /* Integrity word */
    s->iSourceSink = ATAFN_SS_NULL;
    ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
    return false;
}


/**
 * Sink/Source: FLUSH
 */
static bool ataR3FlushSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    int rc;

    Assert(s->uTxDir == PDMMEDIATXDIR_NONE);
    Assert(!s->cbElementaryTransfer);

    ataR3LockLeave(pDevIns, pCtl);

    STAM_PROFILE_START(&s->StatFlushes, f);
    rc = pDevR3->pDrvMedia->pfnFlush(pDevR3->pDrvMedia);
    AssertRC(rc);
    STAM_PROFILE_STOP(&s->StatFlushes, f);

    ataR3LockEnter(pDevIns, pCtl);
    ataR3CmdOK(pCtl, s, 0);
    return false;
}

/**
 * Sink/Source: ATAPI IDENTIFY
 */
static bool atapiR3IdentifySS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint16_t *p;
    RT_NOREF(pDevIns, pDevR3);

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    Assert(s->cbElementaryTransfer == 512);

    p = (uint16_t *)&s->abIOBuffer[0];
    memset(p, 0, 512);
    /* Removable CDROM, 3ms response, 12 byte packets */
    p[0] = RT_H2LE_U16(2 << 14 | 5 << 8 | 1 << 7 | 0 << 5 | 0 << 0);
    ataR3PadString((uint8_t *)(p + 10), s->szSerialNumber, ATA_SERIAL_NUMBER_LENGTH); /* serial number */
    p[20] = RT_H2LE_U16(3); /* XXX: retired, cache type */
    p[21] = RT_H2LE_U16(512); /* XXX: retired, cache size in sectors */
    ataR3PadString((uint8_t *)(p + 23), s->szFirmwareRevision, ATA_FIRMWARE_REVISION_LENGTH); /* firmware version */
    ataR3PadString((uint8_t *)(p + 27), s->szModelNumber, ATA_MODEL_NUMBER_LENGTH); /* model */
    p[49] = RT_H2LE_U16(1 << 11 | 1 << 9 | 1 << 8); /* DMA and LBA supported */
    p[50] = RT_H2LE_U16(1 << 14);  /* No drive specific standby timer minimum */
    p[51] = RT_H2LE_U16(240); /* PIO transfer cycle */
    p[52] = RT_H2LE_U16(240); /* DMA transfer cycle */
    p[53] = RT_H2LE_U16(1 << 1 | 1 << 2); /* words 64-70,88 are valid */
    p[63] = RT_H2LE_U16(ATA_TRANSFER_ID(ATA_MODE_MDMA, ATA_MDMA_MODE_MAX, s->uATATransferMode)); /* MDMA modes supported / mode enabled */
    p[64] = RT_H2LE_U16(ATA_PIO_MODE_MAX > 2 ? (1 << (ATA_PIO_MODE_MAX - 2)) - 1 : 0); /* PIO modes beyond PIO2 supported */
    p[65] = RT_H2LE_U16(120); /* minimum DMA multiword tx cycle time */
    p[66] = RT_H2LE_U16(120); /* recommended DMA multiword tx cycle time */
    p[67] = RT_H2LE_U16(120); /* minimum PIO cycle time without flow control */
    p[68] = RT_H2LE_U16(120); /* minimum PIO cycle time with IORDY flow control */
    p[73] = RT_H2LE_U16(0x003e); /* ATAPI CDROM major */
    p[74] = RT_H2LE_U16(9); /* ATAPI CDROM minor */
    p[75] = RT_H2LE_U16(1); /* queue depth 1 */
    p[80] = RT_H2LE_U16(0x7e); /* support everything up to ATA/ATAPI-6 */
    p[81] = RT_H2LE_U16(0x22); /* conforms to ATA/ATAPI-6 */
    p[82] = RT_H2LE_U16(1 << 4 | 1 << 9); /* supports packet command set and DEVICE RESET */
    p[83] = RT_H2LE_U16(1 << 14);
    p[84] = RT_H2LE_U16(1 << 14);
    p[85] = RT_H2LE_U16(1 << 4 | 1 << 9); /* enabled packet command set and DEVICE RESET */
    p[86] = RT_H2LE_U16(0);
    p[87] = RT_H2LE_U16(1 << 14);
    p[88] = RT_H2LE_U16(ATA_TRANSFER_ID(ATA_MODE_UDMA, ATA_UDMA_MODE_MAX, s->uATATransferMode)); /* UDMA modes supported / mode enabled */
    p[93] = RT_H2LE_U16((1 | 1 << 1) << ((s->iLUN & 1) == 0 ? 0 : 8) | 1 << 13 | 1 << 14);
    /* According to ATAPI-5 spec:
     *
     * The use of this word is optional.
     * If bits 7:0 of this word contain the signature A5h, bits 15:8
     * contain the data
     * structure checksum.
     * The data structure checksum is the twos complement of the sum of
     * all bytes in words 0 through 254 and the byte consisting of
     * bits 7:0 in word 255.
     * Each byte shall be added with unsigned arithmetic,
     * and overflow shall be ignored.
     * The sum of all 512 bytes is zero when the checksum is correct.
     */
    uint32_t uCsum = ataR3Checksum(p, 510);
    p[255] = RT_H2LE_U16(0xa5 | (uCsum << 8)); /* Integrity word */

    s->iSourceSink = ATAFN_SS_NULL;
    ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
    return false;
}


static void ataR3SetSignature(PATADEVSTATE s)
{
    s->uATARegSelect &= 0xf0; /* clear head */
    /* put signature */
    s->uATARegNSector = 1;
    s->uATARegSector = 1;
    if (s->fATAPI)
    {
        s->uATARegLCyl = 0x14;
        s->uATARegHCyl = 0xeb;
    }
    else
    {
        s->uATARegLCyl = 0;
        s->uATARegHCyl = 0;
    }
}


static uint64_t ataR3GetSector(PATADEVSTATE s)
{
    uint64_t iLBA;
    if (s->uATARegSelect & 0x40)
    {
        /* any LBA variant */
        if (s->fLBA48)
        {
            /* LBA48 */
            iLBA = ((uint64_t)s->uATARegHCylHOB << 40)
                 | ((uint64_t)s->uATARegLCylHOB << 32)
                 | ((uint64_t)s->uATARegSectorHOB << 24)
                 | ((uint64_t)s->uATARegHCyl << 16)
                 | ((uint64_t)s->uATARegLCyl << 8)
                 | s->uATARegSector;
        }
        else
        {
            /* LBA */
            iLBA = ((uint32_t)(s->uATARegSelect & 0x0f) << 24)
                 | ((uint32_t)s->uATARegHCyl << 16)
                 | ((uint32_t)s->uATARegLCyl << 8)
                 | s->uATARegSector;
        }
    }
    else
    {
        /* CHS */
        iLBA = (((uint32_t)s->uATARegHCyl << 8) | s->uATARegLCyl) * s->XCHSGeometry.cHeads * s->XCHSGeometry.cSectors
             + (s->uATARegSelect & 0x0f) * s->XCHSGeometry.cSectors
             + (s->uATARegSector - 1);
        LogFlowFunc(("CHS %u/%u/%u -> LBA %llu\n", ((uint32_t)s->uATARegHCyl << 8) | s->uATARegLCyl, s->uATARegSelect & 0x0f, s->uATARegSector, iLBA));
    }
    return iLBA;
}

static void ataR3SetSector(PATADEVSTATE s, uint64_t iLBA)
{
    uint32_t cyl, r;
    if (s->uATARegSelect & 0x40)
    {
        /* any LBA variant */
        if (s->fLBA48)
        {
            /* LBA48 */
            s->uATARegHCylHOB = iLBA >> 40;
            s->uATARegLCylHOB = iLBA >> 32;
            s->uATARegSectorHOB = iLBA >> 24;
            s->uATARegHCyl = iLBA >> 16;
            s->uATARegLCyl = iLBA >> 8;
            s->uATARegSector = iLBA;
        }
        else
        {
            /* LBA */
            s->uATARegSelect = (s->uATARegSelect & 0xf0) | (iLBA >> 24);
            s->uATARegHCyl = (iLBA >> 16);
            s->uATARegLCyl = (iLBA >> 8);
            s->uATARegSector = (iLBA);
        }
    }
    else
    {
        /* CHS */
        AssertMsgReturnVoid(s->XCHSGeometry.cHeads && s->XCHSGeometry.cSectors, ("Device geometry not set!\n"));
        cyl = iLBA / (s->XCHSGeometry.cHeads * s->XCHSGeometry.cSectors);
        r = iLBA % (s->XCHSGeometry.cHeads * s->XCHSGeometry.cSectors);
        s->uATARegHCyl = cyl >> 8;
        s->uATARegLCyl = cyl;
        s->uATARegSelect = (s->uATARegSelect & 0xf0) | ((r / s->XCHSGeometry.cSectors) & 0x0f);
        s->uATARegSector = (r % s->XCHSGeometry.cSectors) + 1;
        LogFlowFunc(("LBA %llu -> CHS %u/%u/%u\n", iLBA, cyl, s->uATARegSelect & 0x0f, s->uATARegSector));
    }
}


static void ataR3WarningDiskFull(PPDMDEVINS pDevIns)
{
    int rc;
    LogRel(("PIIX3 ATA: Host disk full\n"));
    rc = PDMDevHlpVMSetRuntimeError(pDevIns, VMSETRTERR_FLAGS_SUSPEND | VMSETRTERR_FLAGS_NO_WAIT, "DevATA_DISKFULL",
                                    N_("Host system reported disk full. VM execution is suspended. You can resume after freeing some space"));
    AssertRC(rc);
}

static void ataR3WarningFileTooBig(PPDMDEVINS pDevIns)
{
    int rc;
    LogRel(("PIIX3 ATA: File too big\n"));
    rc = PDMDevHlpVMSetRuntimeError(pDevIns, VMSETRTERR_FLAGS_SUSPEND | VMSETRTERR_FLAGS_NO_WAIT, "DevATA_FILETOOBIG",
                                    N_("Host system reported that the file size limit of the host file system has been exceeded. VM execution is suspended. You need to move your virtual hard disk to a filesystem which allows bigger files"));
    AssertRC(rc);
}

static void ataR3WarningISCSI(PPDMDEVINS pDevIns)
{
    int rc;
    LogRel(("PIIX3 ATA: iSCSI target unavailable\n"));
    rc = PDMDevHlpVMSetRuntimeError(pDevIns, VMSETRTERR_FLAGS_SUSPEND | VMSETRTERR_FLAGS_NO_WAIT, "DevATA_ISCSIDOWN",
                                    N_("The iSCSI target has stopped responding. VM execution is suspended. You can resume when it is available again"));
    AssertRC(rc);
}

static void ataR3WarningFileStale(PPDMDEVINS pDevIns)
{
    int rc;
    LogRel(("PIIX3 ATA: File handle became stale\n"));
    rc = PDMDevHlpVMSetRuntimeError(pDevIns, VMSETRTERR_FLAGS_SUSPEND | VMSETRTERR_FLAGS_NO_WAIT, "DevATA_FILESTALE",
                                    N_("The file became stale (often due to a restarted NFS server). VM execution is suspended. You can resume when it is available again"));
    AssertRC(rc);
}


static bool ataR3IsRedoSetWarning(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, int rc)
{
    Assert(!PDMDevHlpCritSectIsOwner(pDevIns, &pCtl->lock));
    if (rc == VERR_DISK_FULL)
    {
        pCtl->fRedoIdle = true;
        ataR3WarningDiskFull(pDevIns);
        return true;
    }
    if (rc == VERR_FILE_TOO_BIG)
    {
        pCtl->fRedoIdle = true;
        ataR3WarningFileTooBig(pDevIns);
        return true;
    }
    if (rc == VERR_BROKEN_PIPE || rc == VERR_NET_CONNECTION_REFUSED)
    {
        pCtl->fRedoIdle = true;
        /* iSCSI connection abort (first error) or failure to reestablish
         * connection (second error). Pause VM. On resume we'll retry. */
        ataR3WarningISCSI(pDevIns);
        return true;
    }
    if (rc == VERR_STALE_FILE_HANDLE)
    {
        pCtl->fRedoIdle = true;
        ataR3WarningFileStale(pDevIns);
        return true;
    }
    if (rc == VERR_VD_DEK_MISSING)
    {
        /* Error message already set. */
        pCtl->fRedoIdle = true;
        return true;
    }

    return false;
}


static int ataR3ReadSectors(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3,
                            uint64_t u64Sector, void *pvBuf, uint32_t cSectors, bool *pfRedo)
{
    int rc;
    uint32_t const cbSector = s->cbSector;
    uint32_t cbToRead = cSectors * cbSector;
    Assert(pvBuf == &s->abIOBuffer[0]);
    AssertReturnStmt(cbToRead <= sizeof(s->abIOBuffer), *pfRedo = false, VERR_BUFFER_OVERFLOW);

    ataR3LockLeave(pDevIns, pCtl);

    STAM_PROFILE_ADV_START(&s->StatReads, r);
    s->Led.Asserted.s.fReading = s->Led.Actual.s.fReading = 1;
    rc = pDevR3->pDrvMedia->pfnRead(pDevR3->pDrvMedia, u64Sector * cbSector, pvBuf, cbToRead);
    s->Led.Actual.s.fReading = 0;
    STAM_PROFILE_ADV_STOP(&s->StatReads, r);
    Log4(("ataR3ReadSectors: rc=%Rrc cSectors=%#x u64Sector=%llu\n%.*Rhxd\n",
          rc, cSectors, u64Sector, cbToRead, pvBuf));

    STAM_REL_COUNTER_ADD(&s->StatBytesRead, cbToRead);

    if (RT_SUCCESS(rc))
        *pfRedo = false;
    else
        *pfRedo = ataR3IsRedoSetWarning(pDevIns, pCtl, rc);

    ataR3LockEnter(pDevIns, pCtl);
    return rc;
}


static int ataR3WriteSectors(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3,
                             uint64_t u64Sector, const void *pvBuf, uint32_t cSectors, bool *pfRedo)
{
    int rc;
    uint32_t const cbSector = s->cbSector;
    uint32_t cbToWrite = cSectors * cbSector;
    Assert(pvBuf == &s->abIOBuffer[0]);
    AssertReturnStmt(cbToWrite <= sizeof(s->abIOBuffer), *pfRedo = false, VERR_BUFFER_OVERFLOW);

    ataR3LockLeave(pDevIns, pCtl);

    STAM_PROFILE_ADV_START(&s->StatWrites, w);
    s->Led.Asserted.s.fWriting = s->Led.Actual.s.fWriting = 1;
# ifdef VBOX_INSTRUMENT_DMA_WRITES
    if (s->fDMA)
        STAM_PROFILE_ADV_START(&s->StatInstrVDWrites, vw);
# endif
    rc = pDevR3->pDrvMedia->pfnWrite(pDevR3->pDrvMedia, u64Sector * cbSector, pvBuf, cbToWrite);
# ifdef VBOX_INSTRUMENT_DMA_WRITES
    if (s->fDMA)
        STAM_PROFILE_ADV_STOP(&s->StatInstrVDWrites, vw);
# endif
    s->Led.Actual.s.fWriting = 0;
    STAM_PROFILE_ADV_STOP(&s->StatWrites, w);
    Log4(("ataR3WriteSectors: rc=%Rrc cSectors=%#x u64Sector=%llu\n%.*Rhxd\n",
          rc, cSectors, u64Sector, cbToWrite, pvBuf));

    STAM_REL_COUNTER_ADD(&s->StatBytesWritten, cbToWrite);

    if (RT_SUCCESS(rc))
        *pfRedo = false;
    else
        *pfRedo = ataR3IsRedoSetWarning(pDevIns, pCtl, rc);

    ataR3LockEnter(pDevIns, pCtl);
    return rc;
}


/**
 * Begin Transfer: READ/WRITE SECTORS
 */
static void ataR3ReadWriteSectorsBT(PATACONTROLLER pCtl, PATADEVSTATE s)
{
    uint32_t const cbSector = RT_MAX(s->cbSector, 1);
    uint32_t cSectors;

    cSectors = s->cbTotalTransfer / cbSector;
    if (cSectors > s->cSectorsPerIRQ)
        s->cbElementaryTransfer = s->cSectorsPerIRQ * cbSector;
    else
        s->cbElementaryTransfer = cSectors * cbSector;
    if (s->uTxDir == PDMMEDIATXDIR_TO_DEVICE)
        ataR3CmdOK(pCtl, s, 0);
}


/**
 * Sink/Source: READ SECTORS
 */
static bool ataR3ReadSectorsSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint32_t const cbSector = RT_MAX(s->cbSector, 1);
    uint32_t cSectors;
    uint64_t iLBA;
    bool fRedo;
    int rc;

    cSectors = s->cbElementaryTransfer / cbSector;
    Assert(cSectors);
    iLBA = s->iCurLBA;
    Log(("%s: %d sectors at LBA %d\n", __FUNCTION__, cSectors, iLBA));
    rc = ataR3ReadSectors(pDevIns, pCtl, s, pDevR3, iLBA, s->abIOBuffer, cSectors, &fRedo);
    if (RT_SUCCESS(rc))
    {
        /* When READ SECTORS etc. finishes, the address in the task
         * file register points at the last sector read, not at the next
         * sector that would be read. This ensures the registers always
         * contain a valid sector address.
         */
        if (s->cbElementaryTransfer == s->cbTotalTransfer)
        {
            s->iSourceSink = ATAFN_SS_NULL;
            ataR3SetSector(s, iLBA + cSectors - 1);
        }
        else
            ataR3SetSector(s, iLBA + cSectors);
        s->uATARegNSector -= cSectors;
        s->iCurLBA += cSectors;
        ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
    }
    else
    {
        if (fRedo)
            return fRedo;
        if (s->cErrors++ < MAX_LOG_REL_ERRORS)
            LogRel(("PIIX3 ATA: LUN#%d: disk read error (rc=%Rrc iSector=%#RX64 cSectors=%#RX32)\n",
                    s->iLUN, rc, iLBA, cSectors));

        /*
         * Check if we got interrupted. We don't need to set status variables
         * because the request was aborted.
         */
        if (rc != VERR_INTERRUPTED)
            ataR3CmdError(pCtl, s, ID_ERR);
    }
    return false;
}


/**
 * Sink/Source: WRITE SECTOR
 */
static bool ataR3WriteSectorsSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint32_t const cbSector = RT_MAX(s->cbSector, 1);
    uint64_t iLBA;
    uint32_t cSectors;
    bool fRedo;
    int rc;

    cSectors = s->cbElementaryTransfer / cbSector;
    Assert(cSectors);
    iLBA = s->iCurLBA;
    Log(("%s: %d sectors at LBA %d\n", __FUNCTION__, cSectors, iLBA));
    rc = ataR3WriteSectors(pDevIns, pCtl, s, pDevR3, iLBA, s->abIOBuffer, cSectors, &fRedo);
    if (RT_SUCCESS(rc))
    {
        ataR3SetSector(s, iLBA + cSectors);
        s->iCurLBA = iLBA + cSectors;
        if (!s->cbTotalTransfer)
            s->iSourceSink = ATAFN_SS_NULL;
        ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
    }
    else
    {
        if (fRedo)
            return fRedo;
        if (s->cErrors++ < MAX_LOG_REL_ERRORS)
            LogRel(("PIIX3 ATA: LUN#%d: disk write error (rc=%Rrc iSector=%#RX64 cSectors=%#RX32)\n",
                    s->iLUN, rc, iLBA, cSectors));

        /*
         * Check if we got interrupted. We don't need to set status variables
         * because the request was aborted.
         */
        if (rc != VERR_INTERRUPTED)
            ataR3CmdError(pCtl, s, ID_ERR);
    }
    return false;
}


static void atapiR3CmdOK(PATACONTROLLER pCtl, PATADEVSTATE s)
{
    s->uATARegError = 0;
    ataSetStatusValue(pCtl, s, ATA_STAT_READY);
    s->uATARegNSector = (s->uATARegNSector & ~7)
        | ((s->uTxDir != PDMMEDIATXDIR_TO_DEVICE) ? ATAPI_INT_REASON_IO : 0)
        | (!s->cbTotalTransfer ? ATAPI_INT_REASON_CD : 0);
    Log2(("%s: interrupt reason %#04x\n", __FUNCTION__, s->uATARegNSector));

    memset(s->abATAPISense, '\0', sizeof(s->abATAPISense));
    s->abATAPISense[0] = 0x70 | (1 << 7);
    s->abATAPISense[7] = 10;
}


static void atapiR3CmdError(PATACONTROLLER pCtl, PATADEVSTATE s, const uint8_t *pabATAPISense, size_t cbATAPISense)
{
    Log(("%s: sense=%#x (%s) asc=%#x ascq=%#x (%s)\n", __FUNCTION__, pabATAPISense[2] & 0x0f, SCSISenseText(pabATAPISense[2] & 0x0f),
         pabATAPISense[12], pabATAPISense[13], SCSISenseExtText(pabATAPISense[12], pabATAPISense[13])));
    s->uATARegError = pabATAPISense[2] << 4;
    ataSetStatusValue(pCtl, s, ATA_STAT_READY | ATA_STAT_ERR);
    s->uATARegNSector = (s->uATARegNSector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
    Log2(("%s: interrupt reason %#04x\n", __FUNCTION__, s->uATARegNSector));
    memset(s->abATAPISense, '\0', sizeof(s->abATAPISense));
    memcpy(s->abATAPISense, pabATAPISense, RT_MIN(cbATAPISense, sizeof(s->abATAPISense)));
    s->cbTotalTransfer = 0;
    s->cbElementaryTransfer = 0;
    s->cbAtapiPassthroughTransfer = 0;
    s->iIOBufferCur = 0;
    s->iIOBufferEnd = 0;
    s->uTxDir = PDMMEDIATXDIR_NONE;
    s->iBeginTransfer = ATAFN_BT_NULL;
    s->iSourceSink = ATAFN_SS_NULL;
}


/** @todo deprecated function - doesn't provide enough info. Replace by direct
 * calls to atapiR3CmdError()  with full data. */
static void atapiR3CmdErrorSimple(PATACONTROLLER pCtl, PATADEVSTATE s, uint8_t uATAPISenseKey, uint8_t uATAPIASC)
{
    uint8_t abATAPISense[ATAPI_SENSE_SIZE];
    memset(abATAPISense, '\0', sizeof(abATAPISense));
    abATAPISense[0] = 0x70 | (1 << 7);
    abATAPISense[2] = uATAPISenseKey & 0x0f;
    abATAPISense[7] = 10;
    abATAPISense[12] = uATAPIASC;
    atapiR3CmdError(pCtl, s, abATAPISense, sizeof(abATAPISense));
}


/**
 * Begin Transfer: ATAPI command
 */
static void atapiR3CmdBT(PATACONTROLLER pCtl, PATADEVSTATE s)
{
    s->fATAPITransfer = true;
    s->cbElementaryTransfer = s->cbTotalTransfer;
    s->cbAtapiPassthroughTransfer = s->cbTotalTransfer;
    s->cbPIOTransferLimit = s->uATARegLCyl | (s->uATARegHCyl << 8);
    if (s->uTxDir == PDMMEDIATXDIR_TO_DEVICE)
        atapiR3CmdOK(pCtl, s);
}


/**
 * Begin Transfer: ATAPI Passthrough command
 */
static void atapiR3PassthroughCmdBT(PATACONTROLLER pCtl, PATADEVSTATE s)
{
    atapiR3CmdBT(pCtl, s);
}


/**
 * Sink/Source: READ
 */
static bool atapiR3ReadSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    int rc;
    uint64_t cbBlockRegion = 0;
    VDREGIONDATAFORM enmDataForm;

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    uint32_t const iATAPILBA     = s->iCurLBA;
    uint32_t const cbTransfer    = RT_MIN(s->cbTotalTransfer, RT_MIN(s->cbIOBuffer, ATA_MAX_IO_BUFFER_SIZE));
    uint32_t const cbATAPISector = s->cbATAPISector;
    uint32_t const cSectors      = cbTransfer / cbATAPISector;
    Assert(cSectors * cbATAPISector <= cbTransfer);
    Log(("%s: %d sectors at LBA %d\n", __FUNCTION__, cSectors, iATAPILBA));
    AssertLogRelReturn(cSectors * cbATAPISector <= sizeof(s->abIOBuffer), false);

    ataR3LockLeave(pDevIns, pCtl);

    rc = pDevR3->pDrvMedia->pfnQueryRegionPropertiesForLba(pDevR3->pDrvMedia, iATAPILBA, NULL, NULL,
                                                      &cbBlockRegion, &enmDataForm);
    if (RT_SUCCESS(rc))
    {
        STAM_PROFILE_ADV_START(&s->StatReads, r);
        s->Led.Asserted.s.fReading = s->Led.Actual.s.fReading = 1;

        /* If the region block size and requested sector matches we can just pass the request through. */
        if (cbBlockRegion == cbATAPISector)
            rc = pDevR3->pDrvMedia->pfnRead(pDevR3->pDrvMedia, (uint64_t)iATAPILBA * cbATAPISector,
                                            s->abIOBuffer, cbATAPISector * cSectors);
        else
        {
            uint32_t const iEndSector = iATAPILBA + cSectors;
            ASSERT_GUEST(iEndSector >= iATAPILBA);
            if (cbBlockRegion == 2048 && cbATAPISector == 2352)
            {
                /* Generate the sync bytes. */
                uint8_t *pbBuf = s->abIOBuffer;

                for (uint32_t i = iATAPILBA; i < iEndSector; i++)
                {
                    /* Sync bytes, see 4.2.3.8 CD Main Channel Block Formats */
                    *pbBuf++ = 0x00;
                    memset(pbBuf, 0xff, 10);
                    pbBuf += 10;
                    *pbBuf++ = 0x00;
                    /* MSF */
                    scsiLBA2MSF(pbBuf, i);
                    pbBuf += 3;
                    *pbBuf++ = 0x01; /* mode 1 data */
                    /* data */
                    rc = pDevR3->pDrvMedia->pfnRead(pDevR3->pDrvMedia, (uint64_t)i * 2048, pbBuf, 2048);
                    if (RT_FAILURE(rc))
                        break;
                    pbBuf += 2048;
                    /**
                     * @todo maybe compute ECC and parity, layout is:
                     * 2072 4   EDC
                     * 2076 172 P parity symbols
                     * 2248 104 Q parity symbols
                     */
                    memset(pbBuf, 0, 280);
                    pbBuf += 280;
                }
            }
            else if (cbBlockRegion == 2352 && cbATAPISector == 2048)
            {
                /* Read only the user data portion. */
                uint8_t *pbBuf = s->abIOBuffer;

                for (uint32_t i = iATAPILBA; i < iEndSector; i++)
                {
                    uint8_t abTmp[2352];
                    uint8_t cbSkip;

                    rc = pDevR3->pDrvMedia->pfnRead(pDevR3->pDrvMedia, (uint64_t)i * 2352, &abTmp[0], 2352);
                    if (RT_FAILURE(rc))
                        break;

                    /* Mode 2 has an additional subheader before user data; we need to
                     * skip 16 bytes for Mode 1 (sync + header) and 20 bytes for Mode 2       +
                     * (sync + header + subheader).
                     */
                    switch (enmDataForm) {
                    case VDREGIONDATAFORM_MODE2_2352:
                    case VDREGIONDATAFORM_XA_2352:
                        cbSkip = 24;
                        break;
                    case VDREGIONDATAFORM_MODE1_2352:
                        cbSkip = 16;
                        break;
                    default:
                        AssertMsgFailed(("Unexpected region form (%#u), using default skip value\n", enmDataForm));
                        cbSkip = 16;
                    }
                    memcpy(pbBuf, &abTmp[cbSkip], 2048);
                    pbBuf += 2048;
                }
            }
            else
                ASSERT_GUEST_MSG_FAILED(("Unsupported: cbBlockRegion=%u cbATAPISector=%u\n", cbBlockRegion, cbATAPISector));
        }
        s->Led.Actual.s.fReading = 0;
        STAM_PROFILE_ADV_STOP(&s->StatReads, r);
    }

    ataR3LockEnter(pDevIns, pCtl);

    if (RT_SUCCESS(rc))
    {
        STAM_REL_COUNTER_ADD(&s->StatBytesRead, cbATAPISector * cSectors);

        /* The initial buffer end value has been set up based on the total
         * transfer size. But the I/O buffer size limits what can actually be
         * done in one transfer, so set the actual value of the buffer end. */
        s->cbElementaryTransfer = cbTransfer;
        if (cbTransfer >= s->cbTotalTransfer)
            s->iSourceSink = ATAFN_SS_NULL;
        atapiR3CmdOK(pCtl, s);
        s->iCurLBA = iATAPILBA + cSectors;
    }
    else
    {
        if (s->cErrors++ < MAX_LOG_REL_ERRORS)
            LogRel(("PIIX3 ATA: LUN#%d: CD-ROM read error, %d sectors at LBA %d\n", s->iLUN, cSectors, iATAPILBA));

        /*
         * Check if we got interrupted. We don't need to set status variables
         * because the request was aborted.
         */
        if (rc != VERR_INTERRUPTED)
            atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_MEDIUM_ERROR, SCSI_ASC_READ_ERROR);
    }
    return false;
}

/**
 * Sets the given media track type.
 */
static uint32_t ataR3MediumTypeSet(PATADEVSTATE s, uint32_t MediaTrackType)
{
    return ASMAtomicXchgU32(&s->MediaTrackType, MediaTrackType);
}


/**
 * Sink/Source: Passthrough
 */
static bool atapiR3PassthroughSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    int rc = VINF_SUCCESS;
    uint8_t abATAPISense[ATAPI_SENSE_SIZE];
    uint32_t cbTransfer;
    PSTAMPROFILEADV pProf = NULL;

    cbTransfer = RT_MIN(s->cbAtapiPassthroughTransfer, RT_MIN(s->cbIOBuffer, ATA_MAX_IO_BUFFER_SIZE));

    if (s->uTxDir == PDMMEDIATXDIR_TO_DEVICE)
        Log3(("ATAPI PT data write (%d): %.*Rhxs\n", cbTransfer, cbTransfer, s->abIOBuffer));

    /* Simple heuristics: if there is at least one sector of data
     * to transfer, it's worth updating the LEDs. */
    if (cbTransfer >= 2048)
    {
        if (s->uTxDir != PDMMEDIATXDIR_TO_DEVICE)
        {
            s->Led.Asserted.s.fReading = s->Led.Actual.s.fReading = 1;
            pProf = &s->StatReads;
        }
        else
        {
            s->Led.Asserted.s.fWriting = s->Led.Actual.s.fWriting = 1;
            pProf = &s->StatWrites;
        }
    }

    ataR3LockLeave(pDevIns, pCtl);

# if defined(LOG_ENABLED)
    char szBuf[1024];

    memset(szBuf, 0, sizeof(szBuf));

    switch (s->abATAPICmd[0])
    {
        case SCSI_MODE_SELECT_10:
        {
            size_t cbBlkDescLength = scsiBE2H_U16(&s->abIOBuffer[6]);

            SCSILogModePage(szBuf, sizeof(szBuf) - 1,
                            s->abIOBuffer + 8 + cbBlkDescLength,
                            cbTransfer - 8 - cbBlkDescLength);
            break;
        }
        case SCSI_SEND_CUE_SHEET:
        {
            SCSILogCueSheet(szBuf, sizeof(szBuf) - 1,
                            s->abIOBuffer, cbTransfer);
            break;
        }
        default:
            break;
    }

    Log2(("%s\n", szBuf));
# endif

    if (pProf) { STAM_PROFILE_ADV_START(pProf, b); }

    Assert(s->cbATAPISector);
    const uint32_t cbATAPISector = RT_MAX(s->cbATAPISector, 1);                     /* paranoia */
    const uint32_t cbIOBuffer    = RT_MIN(s->cbIOBuffer, ATA_MAX_IO_BUFFER_SIZE);   /* ditto */

    if (   cbTransfer > SCSI_MAX_BUFFER_SIZE
        || s->cbElementaryTransfer > cbIOBuffer)
    {
        /* Linux accepts commands with up to 100KB of data, but expects
         * us to handle commands with up to 128KB of data. The usual
         * imbalance of powers. */
        uint8_t abATAPICmd[ATAPI_PACKET_SIZE];
        uint32_t iATAPILBA, cSectors, cReqSectors, cbCurrTX;
        uint8_t *pbBuf = s->abIOBuffer;
        uint32_t cSectorsMax; /**< Maximum amount of sectors to read without exceeding the I/O buffer. */

        cSectorsMax = cbTransfer / cbATAPISector;
        AssertStmt(cSectorsMax * s->cbATAPISector <= cbIOBuffer, cSectorsMax = cbIOBuffer / cbATAPISector);

        switch (s->abATAPICmd[0])
        {
            case SCSI_READ_10:
            case SCSI_WRITE_10:
            case SCSI_WRITE_AND_VERIFY_10:
                iATAPILBA = scsiBE2H_U32(s->abATAPICmd + 2);
                cSectors = scsiBE2H_U16(s->abATAPICmd + 7);
                break;
            case SCSI_READ_12:
            case SCSI_WRITE_12:
                iATAPILBA = scsiBE2H_U32(s->abATAPICmd + 2);
                cSectors = scsiBE2H_U32(s->abATAPICmd + 6);
                break;
            case SCSI_READ_CD:
                iATAPILBA = scsiBE2H_U32(s->abATAPICmd + 2);
                cSectors = scsiBE2H_U24(s->abATAPICmd + 6);
                break;
            case SCSI_READ_CD_MSF:
                iATAPILBA = scsiMSF2LBA(s->abATAPICmd + 3);
                cSectors = scsiMSF2LBA(s->abATAPICmd + 6) - iATAPILBA;
                break;
            default:
                AssertMsgFailed(("Don't know how to split command %#04x\n", s->abATAPICmd[0]));
                if (s->cErrors++ < MAX_LOG_REL_ERRORS)
                    LogRel(("PIIX3 ATA: LUN#%d: CD-ROM passthrough split error\n", s->iLUN));
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_ILLEGAL_OPCODE);
                ataR3LockEnter(pDevIns, pCtl);
                return false;
        }
        cSectorsMax = RT_MIN(cSectorsMax, cSectors);
        memcpy(abATAPICmd, s->abATAPICmd, ATAPI_PACKET_SIZE);
        cReqSectors = 0;
        for (uint32_t i = cSectorsMax; i > 0; i -= cReqSectors)
        {
            if (i * cbATAPISector > SCSI_MAX_BUFFER_SIZE)
                cReqSectors = SCSI_MAX_BUFFER_SIZE / cbATAPISector;
            else
                cReqSectors = i;
            cbCurrTX = cbATAPISector * cReqSectors;
            switch (s->abATAPICmd[0])
            {
                case SCSI_READ_10:
                case SCSI_WRITE_10:
                case SCSI_WRITE_AND_VERIFY_10:
                    scsiH2BE_U32(abATAPICmd + 2, iATAPILBA);
                    scsiH2BE_U16(abATAPICmd + 7, cReqSectors);
                    break;
                case SCSI_READ_12:
                case SCSI_WRITE_12:
                    scsiH2BE_U32(abATAPICmd + 2, iATAPILBA);
                    scsiH2BE_U32(abATAPICmd + 6, cReqSectors);
                    break;
                case SCSI_READ_CD:
                    scsiH2BE_U32(abATAPICmd + 2, iATAPILBA);
                    scsiH2BE_U24(abATAPICmd + 6, cReqSectors);
                    break;
                case SCSI_READ_CD_MSF:
                    scsiLBA2MSF(abATAPICmd + 3, iATAPILBA);
                    scsiLBA2MSF(abATAPICmd + 6, iATAPILBA + cReqSectors);
                    break;
            }
            AssertLogRelReturn((uintptr_t)(pbBuf - &s->abIOBuffer[0]) + cbCurrTX <= sizeof(s->abIOBuffer), false);
            rc = pDevR3->pDrvMedia->pfnSendCmd(pDevR3->pDrvMedia, abATAPICmd, ATAPI_PACKET_SIZE, (PDMMEDIATXDIR)s->uTxDir,
                                               pbBuf, &cbCurrTX, abATAPISense, sizeof(abATAPISense), 30000 /**< @todo timeout */);
            if (rc != VINF_SUCCESS)
                break;
            iATAPILBA += cReqSectors;
            pbBuf += cbATAPISector * cReqSectors;
        }

        if (RT_SUCCESS(rc))
        {
            /* Adjust ATAPI command for the next call. */
            switch (s->abATAPICmd[0])
            {
                case SCSI_READ_10:
                case SCSI_WRITE_10:
                case SCSI_WRITE_AND_VERIFY_10:
                    scsiH2BE_U32(s->abATAPICmd + 2, iATAPILBA);
                    scsiH2BE_U16(s->abATAPICmd + 7, cSectors - cSectorsMax);
                    break;
                case SCSI_READ_12:
                case SCSI_WRITE_12:
                    scsiH2BE_U32(s->abATAPICmd + 2, iATAPILBA);
                    scsiH2BE_U32(s->abATAPICmd + 6, cSectors - cSectorsMax);
                    break;
                case SCSI_READ_CD:
                    scsiH2BE_U32(s->abATAPICmd + 2, iATAPILBA);
                    scsiH2BE_U24(s->abATAPICmd + 6, cSectors - cSectorsMax);
                    break;
                case SCSI_READ_CD_MSF:
                    scsiLBA2MSF(s->abATAPICmd + 3, iATAPILBA);
                    scsiLBA2MSF(s->abATAPICmd + 6, iATAPILBA + cSectors - cSectorsMax);
                    break;
                default:
                    AssertMsgFailed(("Don't know how to split command %#04x\n", s->abATAPICmd[0]));
                    if (s->cErrors++ < MAX_LOG_REL_ERRORS)
                        LogRel(("PIIX3 ATA: LUN#%d: CD-ROM passthrough split error\n", s->iLUN));
                    atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_ILLEGAL_OPCODE);
                    return false;
            }
        }
    }
    else
    {
        AssertLogRelReturn(cbTransfer <= sizeof(s->abIOBuffer), false);
        rc = pDevR3->pDrvMedia->pfnSendCmd(pDevR3->pDrvMedia, s->abATAPICmd, ATAPI_PACKET_SIZE, (PDMMEDIATXDIR)s->uTxDir,
                                           s->abIOBuffer, &cbTransfer, abATAPISense, sizeof(abATAPISense), 30000 /**< @todo timeout */);
    }
    if (pProf) { STAM_PROFILE_ADV_STOP(pProf, b); }

    ataR3LockEnter(pDevIns, pCtl);

    /* Update the LEDs and the read/write statistics. */
    if (cbTransfer >= 2048)
    {
        if (s->uTxDir != PDMMEDIATXDIR_TO_DEVICE)
        {
            s->Led.Actual.s.fReading = 0;
            STAM_REL_COUNTER_ADD(&s->StatBytesRead, cbTransfer);
        }
        else
        {
            s->Led.Actual.s.fWriting = 0;
            STAM_REL_COUNTER_ADD(&s->StatBytesWritten, cbTransfer);
        }
    }

    if (RT_SUCCESS(rc))
    {
        /* Do post processing for certain commands. */
        switch (s->abATAPICmd[0])
        {
            case SCSI_SEND_CUE_SHEET:
            case SCSI_READ_TOC_PMA_ATIP:
            {
                if (!pDevR3->pTrackList)
                    rc = ATAPIPassthroughTrackListCreateEmpty(&pDevR3->pTrackList);

                if (RT_SUCCESS(rc))
                    rc = ATAPIPassthroughTrackListUpdate(pDevR3->pTrackList, s->abATAPICmd, s->abIOBuffer, sizeof(s->abIOBuffer));

                if (   RT_FAILURE(rc)
                    && s->cErrors++ < MAX_LOG_REL_ERRORS)
                    LogRel(("ATA: Error (%Rrc) while updating the tracklist during %s, burning the disc might fail\n",
                            rc, s->abATAPICmd[0] == SCSI_SEND_CUE_SHEET ? "SEND CUE SHEET" : "READ TOC/PMA/ATIP"));
                break;
            }
            case SCSI_SYNCHRONIZE_CACHE:
            {
                if (pDevR3->pTrackList)
                    ATAPIPassthroughTrackListClear(pDevR3->pTrackList);
                break;
            }
        }

        if (s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE)
        {
            /*
             * Reply with the same amount of data as the real drive
             * but only if the command wasn't split.
             */
            if (s->cbAtapiPassthroughTransfer < cbIOBuffer)
                s->cbTotalTransfer = cbTransfer;

            if (   s->abATAPICmd[0] == SCSI_INQUIRY
                && s->fOverwriteInquiry)
            {
                /* Make sure that the real drive cannot be identified.
                 * Motivation: changing the VM configuration should be as
                 *             invisible as possible to the guest. */
                Log3(("ATAPI PT inquiry data before (%d): %.*Rhxs\n", cbTransfer, cbTransfer, s->abIOBuffer));
                scsiPadStr(&s->abIOBuffer[8], "VBOX", 8);
                scsiPadStr(&s->abIOBuffer[16], "CD-ROM", 16);
                scsiPadStr(&s->abIOBuffer[32], "1.0", 4);
            }

            if (cbTransfer)
                Log3(("ATAPI PT data read (%d):\n%.*Rhxd\n", cbTransfer, cbTransfer, s->abIOBuffer));
        }

        /* The initial buffer end value has been set up based on the total
         * transfer size. But the I/O buffer size limits what can actually be
         * done in one transfer, so set the actual value of the buffer end. */
        Assert(cbTransfer <= s->cbAtapiPassthroughTransfer);
        s->cbElementaryTransfer        = cbTransfer;
        s->cbAtapiPassthroughTransfer -= cbTransfer;
        if (!s->cbAtapiPassthroughTransfer)
        {
            s->iSourceSink = ATAFN_SS_NULL;
            atapiR3CmdOK(pCtl, s);
        }
    }
    else
    {
        if (s->cErrors < MAX_LOG_REL_ERRORS)
        {
            uint8_t u8Cmd = s->abATAPICmd[0];
            do
            {
                /* don't log superfluous errors */
                if (    rc == VERR_DEV_IO_ERROR
                    && (   u8Cmd == SCSI_TEST_UNIT_READY
                        || u8Cmd == SCSI_READ_CAPACITY
                        || u8Cmd == SCSI_READ_DVD_STRUCTURE
                        || u8Cmd == SCSI_READ_TOC_PMA_ATIP))
                    break;
                s->cErrors++;
                LogRel(("PIIX3 ATA: LUN#%d: CD-ROM passthrough cmd=%#04x sense=%d ASC=%#02x ASCQ=%#02x %Rrc\n",
                            s->iLUN, u8Cmd, abATAPISense[2] & 0x0f, abATAPISense[12], abATAPISense[13], rc));
            } while (0);
        }
        atapiR3CmdError(pCtl, s, abATAPISense, sizeof(abATAPISense));
    }
    return false;
}


/**
 * Begin Transfer: Read DVD structures
 */
static bool atapiR3ReadDVDStructureSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint8_t *buf = s->abIOBuffer;
    int media = s->abATAPICmd[1];
    int format = s->abATAPICmd[7];
    RT_NOREF(pDevIns, pDevR3);

    AssertCompile(sizeof(s->abIOBuffer) > UINT16_MAX /* want a RT_MIN() below, but clang takes offence at always false stuff */);
    uint16_t max_len = scsiBE2H_U16(&s->abATAPICmd[8]);
    memset(buf, 0, max_len);

    switch (format) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
        case 0x10:
        case 0x11:
        case 0x30:
        case 0x31:
        case 0xff:
            if (media == 0)
            {
                int uASC = SCSI_ASC_NONE;

                switch (format)
                {
                    case 0x0: /* Physical format information */
                    {
                        int layer = s->abATAPICmd[6];
                        uint64_t total_sectors;

                        if (layer != 0)
                        {
                            uASC = -SCSI_ASC_INV_FIELD_IN_CMD_PACKET;
                            break;
                        }

                        total_sectors = s->cTotalSectors;
                        total_sectors >>= 2;
                        if (total_sectors == 0)
                        {
                            uASC = -SCSI_ASC_MEDIUM_NOT_PRESENT;
                            break;
                        }

                        buf[4] = 1;   /* DVD-ROM, part version 1 */
                        buf[5] = 0xf; /* 120mm disc, minimum rate unspecified */
                        buf[6] = 1;   /* one layer, read-only (per MMC-2 spec) */
                        buf[7] = 0;   /* default densities */

                        /* FIXME: 0x30000 per spec? */
                        scsiH2BE_U32(buf + 8, 0); /* start sector */
                        scsiH2BE_U32(buf + 12, total_sectors - 1); /* end sector */
                        scsiH2BE_U32(buf + 16, total_sectors - 1); /* l0 end sector */

                        /* Size of buffer, not including 2 byte size field */
                        scsiH2BE_U32(&buf[0], 2048 + 2);

                        /* 2k data + 4 byte header */
                        uASC = (2048 + 4);
                        break;
                    }
                    case 0x01: /* DVD copyright information */
                        buf[4] = 0; /* no copyright data */
                        buf[5] = 0; /* no region restrictions */

                        /* Size of buffer, not including 2 byte size field */
                        scsiH2BE_U16(buf, 4 + 2);

                        /* 4 byte header + 4 byte data */
                        uASC = (4 + 4);
                        break;

                    case 0x03: /* BCA information - invalid field for no BCA info */
                        uASC = -SCSI_ASC_INV_FIELD_IN_CMD_PACKET;
                        break;

                    case 0x04: /* DVD disc manufacturing information */
                        /* Size of buffer, not including 2 byte size field */
                        scsiH2BE_U16(buf, 2048 + 2);

                        /* 2k data + 4 byte header */
                        uASC = (2048 + 4);
                        break;
                    case 0xff:
                        /*
                         * This lists all the command capabilities above.  Add new ones
                         * in order and update the length and buffer return values.
                         */

                        buf[4] = 0x00; /* Physical format */
                        buf[5] = 0x40; /* Not writable, is readable */
                        scsiH2BE_U16((buf + 6), 2048 + 4);

                        buf[8] = 0x01; /* Copyright info */
                        buf[9] = 0x40; /* Not writable, is readable */
                        scsiH2BE_U16((buf + 10), 4 + 4);

                        buf[12] = 0x03; /* BCA info */
                        buf[13] = 0x40; /* Not writable, is readable */
                        scsiH2BE_U16((buf + 14), 188 + 4);

                        buf[16] = 0x04; /* Manufacturing info */
                        buf[17] = 0x40; /* Not writable, is readable */
                        scsiH2BE_U16((buf + 18), 2048 + 4);

                        /* Size of buffer, not including 2 byte size field */
                        scsiH2BE_U16(buf, 16 + 2);

                        /* data written + 4 byte header */
                        uASC = (16 + 4);
                        break;
                    default: /** @todo formats beyond DVD-ROM requires */
                        uASC = -SCSI_ASC_INV_FIELD_IN_CMD_PACKET;
                }

                if (uASC < 0)
                {
                    s->iSourceSink = ATAFN_SS_NULL;
                    atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, -uASC);
                    return false;
                }
                break;
            }
            /** @todo BD support, fall through for now */
            RT_FALL_THRU();

        /* Generic disk structures */
        case 0x80: /** @todo AACS volume identifier */
        case 0x81: /** @todo AACS media serial number */
        case 0x82: /** @todo AACS media identifier */
        case 0x83: /** @todo AACS media key block */
        case 0x90: /** @todo List of recognized format layers */
        case 0xc0: /** @todo Write protection status */
        default:
            s->iSourceSink = ATAFN_SS_NULL;
            atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET);
            return false;
    }

    s->iSourceSink = ATAFN_SS_NULL;
    atapiR3CmdOK(pCtl, s);
    return false;
}


static bool atapiR3ReadSectors(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s,
                               uint32_t iATAPILBA, uint32_t cSectors, uint32_t cbSector)
{
    Assert(cSectors > 0);
    s->iCurLBA = iATAPILBA;
    s->cbATAPISector = cbSector;
    ataR3StartTransfer(pDevIns, pCtl, s, cSectors * cbSector,
                       PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_READ, true);
    return false;
}


/**
 * Sink/Source: ATAPI READ CAPACITY
 */
static bool atapiR3ReadCapacitySS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint8_t *pbBuf = s->abIOBuffer;
    RT_NOREF(pDevIns, pDevR3);

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    Assert(s->cbElementaryTransfer <= 8);
    scsiH2BE_U32(pbBuf, s->cTotalSectors - 1);
    scsiH2BE_U32(pbBuf + 4, 2048);
    s->iSourceSink = ATAFN_SS_NULL;
    atapiR3CmdOK(pCtl, s);
    return false;
}


/**
 * Sink/Source: ATAPI READ DISCK INFORMATION
 */
static bool atapiR3ReadDiscInformationSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint8_t *pbBuf = s->abIOBuffer;
    RT_NOREF(pDevIns, pDevR3);

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    Assert(s->cbElementaryTransfer <= 34);
    memset(pbBuf, '\0', 34);
    scsiH2BE_U16(pbBuf, 32);
    pbBuf[2] = (0 << 4) | (3 << 2) | (2 << 0); /* not erasable, complete session, complete disc */
    pbBuf[3] = 1; /* number of first track */
    pbBuf[4] = 1; /* number of sessions (LSB) */
    pbBuf[5] = 1; /* first track number in last session (LSB) */
    pbBuf[6] = (uint8_t)pDevR3->pDrvMedia->pfnGetRegionCount(pDevR3->pDrvMedia); /* last track number in last session (LSB) */
    pbBuf[7] = (0 << 7) | (0 << 6) | (1 << 5) | (0 << 2) | (0 << 0); /* disc id not valid, disc bar code not valid, unrestricted use, not dirty, not RW medium */
    pbBuf[8] = 0; /* disc type = CD-ROM */
    pbBuf[9] = 0; /* number of sessions (MSB) */
    pbBuf[10] = 0; /* number of sessions (MSB) */
    pbBuf[11] = 0; /* number of sessions (MSB) */
    scsiH2BE_U32(pbBuf + 16, 0xffffffff); /* last session lead-in start time is not available */
    scsiH2BE_U32(pbBuf + 20, 0xffffffff); /* last possible start time for lead-out is not available */
    s->iSourceSink = ATAFN_SS_NULL;
    atapiR3CmdOK(pCtl, s);
    return false;
}


/**
 * Sink/Source: ATAPI READ TRACK INFORMATION
 */
static bool atapiR3ReadTrackInformationSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint8_t *pbBuf = s->abIOBuffer;
    uint32_t u32LogAddr = scsiBE2H_U32(&s->abATAPICmd[2]);
    uint8_t u8LogAddrType = s->abATAPICmd[1] & 0x03;
    RT_NOREF(pDevIns);

    int rc;
    uint64_t u64LbaStart = 0;
    uint32_t uRegion = 0;
    uint64_t cBlocks = 0;
    uint64_t cbBlock = 0;
    uint8_t u8DataMode = 0xf; /* Unknown data mode. */
    uint8_t u8TrackMode = 0;
    VDREGIONDATAFORM enmDataForm = VDREGIONDATAFORM_INVALID;

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    Assert(s->cbElementaryTransfer <= 36);

    switch (u8LogAddrType)
    {
        case 0x00:
            rc = pDevR3->pDrvMedia->pfnQueryRegionPropertiesForLba(pDevR3->pDrvMedia, u32LogAddr, &uRegion,
                                                                   NULL, NULL, NULL);
            if (RT_SUCCESS(rc))
                rc = pDevR3->pDrvMedia->pfnQueryRegionProperties(pDevR3->pDrvMedia, uRegion, &u64LbaStart,
                                                                 &cBlocks, &cbBlock, &enmDataForm);
            break;
        case 0x01:
        {
            if (u32LogAddr >= 1)
            {
                uRegion = u32LogAddr - 1;
                rc = pDevR3->pDrvMedia->pfnQueryRegionProperties(pDevR3->pDrvMedia, uRegion, &u64LbaStart,
                                                                 &cBlocks, &cbBlock, &enmDataForm);
            }
            else
                rc = VERR_NOT_FOUND; /** @todo Return lead-in information. */
            break;
        }
        case 0x02:
        default:
            atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET);
            return false;
    }

    if (RT_FAILURE(rc))
    {
        atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET);
        return false;
    }

    switch (enmDataForm)
    {
        case VDREGIONDATAFORM_MODE1_2048:
        case VDREGIONDATAFORM_MODE1_2352:
        case VDREGIONDATAFORM_MODE1_0:
            u8DataMode = 1;
            break;
        case VDREGIONDATAFORM_XA_2336:
        case VDREGIONDATAFORM_XA_2352:
        case VDREGIONDATAFORM_XA_0:
        case VDREGIONDATAFORM_MODE2_2336:
        case VDREGIONDATAFORM_MODE2_2352:
        case VDREGIONDATAFORM_MODE2_0:
            u8DataMode = 2;
            break;
        default:
            u8DataMode = 0xf;
    }

    if (enmDataForm == VDREGIONDATAFORM_CDDA)
        u8TrackMode = 0x0;
    else
        u8TrackMode = 0x4;

    memset(pbBuf, '\0', 36);
    scsiH2BE_U16(pbBuf, 34);
    pbBuf[2] = uRegion + 1;                                            /* track number (LSB) */
    pbBuf[3] = 1;                                                      /* session number (LSB) */
    pbBuf[5] = (0 << 5) | (0 << 4) | u8TrackMode;                      /* not damaged, primary copy, data track */
    pbBuf[6] = (0 << 7) | (0 << 6) | (0 << 5) | (0 << 6) | u8DataMode; /* not reserved track, not blank, not packet writing, not fixed packet */
    pbBuf[7] = (0 << 1) | (0 << 0);                                    /* last recorded address not valid, next recordable address not valid */
    scsiH2BE_U32(pbBuf + 8, (uint32_t)u64LbaStart);                    /* track start address is 0 */
    scsiH2BE_U32(pbBuf + 24, (uint32_t)cBlocks);                       /* track size */
    pbBuf[32] = 0;                                                     /* track number (MSB) */
    pbBuf[33] = 0;                                                     /* session number (MSB) */
    s->iSourceSink = ATAFN_SS_NULL;
    atapiR3CmdOK(pCtl, s);
    return false;
}

static DECLCALLBACK(uint32_t) atapiR3GetConfigurationFillFeatureListProfiles(PATADEVSTATE s, uint8_t *pbBuf, size_t cbBuf)
{
    RT_NOREF(s);
    if (cbBuf < 3*4)
        return 0;

    scsiH2BE_U16(pbBuf, 0x0); /* feature 0: list of profiles supported */
    pbBuf[2] = (0 << 2) | (1 << 1) | (1 << 0); /* version 0, persistent, current */
    pbBuf[3] = 8; /* additional bytes for profiles */
    /* The MMC-3 spec says that DVD-ROM read capability should be reported
     * before CD-ROM read capability. */
    scsiH2BE_U16(pbBuf + 4, 0x10); /* profile: read-only DVD */
    pbBuf[6] = (0 << 0); /* NOT current profile */
    scsiH2BE_U16(pbBuf + 8, 0x08); /* profile: read only CD */
    pbBuf[10] = (1 << 0); /* current profile */

    return 3*4; /* Header + 2 profiles entries */
}

static DECLCALLBACK(uint32_t) atapiR3GetConfigurationFillFeatureCore(PATADEVSTATE s, uint8_t *pbBuf, size_t cbBuf)
{
    RT_NOREF(s);
    if (cbBuf < 12)
        return 0;

    scsiH2BE_U16(pbBuf, 0x1); /* feature 0001h: Core Feature */
    pbBuf[2] = (0x2 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 8; /* Additional length */
    scsiH2BE_U16(pbBuf + 4, 0x00000002); /* Physical interface ATAPI. */
    pbBuf[8] = RT_BIT(0); /* DBE */
    /* Rest is reserved. */

    return 12;
}

static DECLCALLBACK(uint32_t) atapiR3GetConfigurationFillFeatureMorphing(PATADEVSTATE s, uint8_t *pbBuf, size_t cbBuf)
{
    RT_NOREF(s);
    if (cbBuf < 8)
        return 0;

    scsiH2BE_U16(pbBuf, 0x2); /* feature 0002h: Morphing Feature */
    pbBuf[2] = (0x1 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 4; /* Additional length */
    pbBuf[4] = RT_BIT(1) | 0x0; /* OCEvent | !ASYNC */
    /* Rest is reserved. */

    return 8;
}

static DECLCALLBACK(uint32_t) atapiR3GetConfigurationFillFeatureRemovableMedium(PATADEVSTATE s, uint8_t *pbBuf, size_t cbBuf)
{
    RT_NOREF(s);
    if (cbBuf < 8)
        return 0;

    scsiH2BE_U16(pbBuf, 0x3); /* feature 0003h: Removable Medium Feature */
    pbBuf[2] = (0x2 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 4; /* Additional length */
    /* Tray type loading | Load | Eject | !Pvnt Jmpr | !DBML | Lock */
    pbBuf[4] = (0x2 << 5) | RT_BIT(4) | RT_BIT(3) | (0x0 << 2) | (0x0 << 1) | RT_BIT(0);
    /* Rest is reserved. */

    return 8;
}

static DECLCALLBACK(uint32_t) atapiR3GetConfigurationFillFeatureRandomReadable (PATADEVSTATE s, uint8_t *pbBuf, size_t cbBuf)
{
    RT_NOREF(s);
    if (cbBuf < 12)
        return 0;

    scsiH2BE_U16(pbBuf, 0x10); /* feature 0010h: Random Readable Feature */
    pbBuf[2] = (0x0 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 8; /* Additional length */
    scsiH2BE_U32(pbBuf + 4, 2048); /* Logical block size. */
    scsiH2BE_U16(pbBuf + 8, 0x10); /* Blocking (0x10 for DVD, CD is not defined). */
    pbBuf[10] = 0; /* PP not present */
    /* Rest is reserved. */

    return 12;
}

static DECLCALLBACK(uint32_t) atapiR3GetConfigurationFillFeatureCDRead(PATADEVSTATE s, uint8_t *pbBuf, size_t cbBuf)
{
    RT_NOREF(s);
    if (cbBuf < 8)
        return 0;

    scsiH2BE_U16(pbBuf, 0x1e); /* feature 001Eh: CD Read Feature */
    pbBuf[2] = (0x2 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 0; /* Additional length */
    pbBuf[4] = (0x0 << 7) | (0x0 << 1) | 0x0; /* !DAP | !C2-Flags | !CD-Text. */
    /* Rest is reserved. */

    return 8;
}

static DECLCALLBACK(uint32_t) atapiR3GetConfigurationFillFeaturePowerManagement(PATADEVSTATE s, uint8_t *pbBuf, size_t cbBuf)
{
    RT_NOREF(s);
    if (cbBuf < 4)
        return 0;

    scsiH2BE_U16(pbBuf, 0x100); /* feature 0100h: Power Management Feature */
    pbBuf[2] = (0x0 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 0; /* Additional length */

    return 4;
}

static DECLCALLBACK(uint32_t) atapiR3GetConfigurationFillFeatureTimeout(PATADEVSTATE s, uint8_t *pbBuf, size_t cbBuf)
{
    RT_NOREF(s);
    if (cbBuf < 8)
        return 0;

    scsiH2BE_U16(pbBuf, 0x105); /* feature 0105h: Timeout Feature */
    pbBuf[2] = (0x0 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 4; /* Additional length */
    pbBuf[4] = 0x0; /* !Group3 */

    return 8;
}

/**
 * Callback to fill in the correct data for a feature.
 *
 * @returns Number of bytes written into the buffer.
 * @param   s       The ATA device state.
 * @param   pbBuf   The buffer to fill the data with.
 * @param   cbBuf   Size of the buffer.
 */
typedef DECLCALLBACKTYPE(uint32_t, FNATAPIR3FEATUREFILL,(PATADEVSTATE s, uint8_t *pbBuf, size_t cbBuf));
/** Pointer to a feature fill callback. */
typedef FNATAPIR3FEATUREFILL *PFNATAPIR3FEATUREFILL;

/**
 * ATAPI feature descriptor.
 */
typedef struct ATAPIR3FEATDESC
{
    /** The feature number. */
    uint16_t u16Feat;
    /** The callback to fill in the correct data. */
    PFNATAPIR3FEATUREFILL pfnFeatureFill;
} ATAPIR3FEATDESC;

/**
 * Array of known ATAPI feature descriptors.
 */
static const ATAPIR3FEATDESC s_aAtapiR3Features[] =
{
    { 0x0000, atapiR3GetConfigurationFillFeatureListProfiles},
    { 0x0001, atapiR3GetConfigurationFillFeatureCore},
    { 0x0002, atapiR3GetConfigurationFillFeatureMorphing},
    { 0x0003, atapiR3GetConfigurationFillFeatureRemovableMedium},
    { 0x0010, atapiR3GetConfigurationFillFeatureRandomReadable},
    { 0x001e, atapiR3GetConfigurationFillFeatureCDRead},
    { 0x0100, atapiR3GetConfigurationFillFeaturePowerManagement},
    { 0x0105, atapiR3GetConfigurationFillFeatureTimeout}
};

/**
 * Sink/Source: ATAPI GET CONFIGURATION
 */
static bool atapiR3GetConfigurationSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint32_t const cbIOBuffer = RT_MIN(s->cbIOBuffer, ATA_MAX_IO_BUFFER_SIZE);
    uint8_t *pbBuf = s->abIOBuffer;
    uint32_t cbBuf = cbIOBuffer;
    uint32_t cbCopied = 0;
    uint16_t u16Sfn = scsiBE2H_U16(&s->abATAPICmd[2]);
    uint8_t u8Rt = s->abATAPICmd[1] & 0x03;
    RT_NOREF(pDevIns, pDevR3);

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    Assert(s->cbElementaryTransfer <= 80);
    /* Accept valid request types only. */
    if (u8Rt == 3)
    {
        atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET);
        return false;
    }
    memset(pbBuf, '\0', cbBuf);
    /** @todo implement switching between CD-ROM and DVD-ROM profile (the only
     * way to differentiate them right now is based on the image size). */
    if (s->cTotalSectors)
        scsiH2BE_U16(pbBuf + 6, 0x08); /* current profile: read-only CD */
    else
        scsiH2BE_U16(pbBuf + 6, 0x00); /* current profile: none -> no media */
    cbBuf    -= 8;
    pbBuf    += 8;

    if (u8Rt == 0x2)
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aAtapiR3Features); i++)
        {
            if (s_aAtapiR3Features[i].u16Feat == u16Sfn)
            {
                cbCopied = s_aAtapiR3Features[i].pfnFeatureFill(s, pbBuf, cbBuf);
                cbBuf -= cbCopied;
                pbBuf += cbCopied;
                break;
            }
        }
    }
    else
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aAtapiR3Features); i++)
        {
            if (s_aAtapiR3Features[i].u16Feat > u16Sfn)
            {
                cbCopied = s_aAtapiR3Features[i].pfnFeatureFill(s, pbBuf, cbBuf);
                cbBuf -= cbCopied;
                pbBuf += cbCopied;
            }
        }
    }

    /* Set data length now - the field is not included in the final length. */
    scsiH2BE_U32(s->abIOBuffer, cbIOBuffer - cbBuf - 4);

    /* Other profiles we might want to add in the future: 0x40 (BD-ROM) and 0x50 (HDDVD-ROM) */
    s->iSourceSink = ATAFN_SS_NULL;
    atapiR3CmdOK(pCtl, s);
    return false;
}


/**
 * Sink/Source: ATAPI GET EVENT STATUS NOTIFICATION
 */
static bool atapiR3GetEventStatusNotificationSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint8_t *pbBuf = s->abIOBuffer;
    RT_NOREF(pDevIns, pDevR3);

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    Assert(s->cbElementaryTransfer <= 8);

    if (!(s->abATAPICmd[1] & 1))
    {
        /* no asynchronous operation supported */
        atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET);
        return false;
    }

    uint32_t OldStatus, NewStatus;
    do
    {
        OldStatus = ASMAtomicReadU32(&s->MediaEventStatus);
        NewStatus = ATA_EVENT_STATUS_UNCHANGED;
        switch (OldStatus)
        {
            case ATA_EVENT_STATUS_MEDIA_NEW:
                /* mount */
                scsiH2BE_U16(pbBuf + 0, 6);
                pbBuf[2] = 0x04; /* media */
                pbBuf[3] = 0x5e; /* supported = busy|media|external|power|operational */
                pbBuf[4] = 0x02; /* new medium */
                pbBuf[5] = 0x02; /* medium present / door closed */
                pbBuf[6] = 0x00;
                pbBuf[7] = 0x00;
                break;

            case ATA_EVENT_STATUS_MEDIA_CHANGED:
            case ATA_EVENT_STATUS_MEDIA_REMOVED:
                /* umount */
                scsiH2BE_U16(pbBuf + 0, 6);
                pbBuf[2] = 0x04; /* media */
                pbBuf[3] = 0x5e; /* supported = busy|media|external|power|operational */
                pbBuf[4] = OldStatus == ATA_EVENT_STATUS_MEDIA_CHANGED ? 0x04 /* media changed */ : 0x03; /* media removed */
                pbBuf[5] = 0x00; /* medium absent / door closed */
                pbBuf[6] = 0x00;
                pbBuf[7] = 0x00;
                if (OldStatus == ATA_EVENT_STATUS_MEDIA_CHANGED)
                    NewStatus = ATA_EVENT_STATUS_MEDIA_NEW;
                break;

            case ATA_EVENT_STATUS_MEDIA_EJECT_REQUESTED: /* currently unused */
                scsiH2BE_U16(pbBuf + 0, 6);
                pbBuf[2] = 0x04; /* media */
                pbBuf[3] = 0x5e; /* supported = busy|media|external|power|operational */
                pbBuf[4] = 0x01; /* eject requested (eject button pressed) */
                pbBuf[5] = 0x02; /* medium present / door closed */
                pbBuf[6] = 0x00;
                pbBuf[7] = 0x00;
                break;

            case ATA_EVENT_STATUS_UNCHANGED:
            default:
                scsiH2BE_U16(pbBuf + 0, 6);
                pbBuf[2] = 0x01; /* operational change request / notification */
                pbBuf[3] = 0x5e; /* supported = busy|media|external|power|operational */
                pbBuf[4] = 0x00;
                pbBuf[5] = 0x00;
                pbBuf[6] = 0x00;
                pbBuf[7] = 0x00;
                break;
        }
    } while (!ASMAtomicCmpXchgU32(&s->MediaEventStatus, NewStatus, OldStatus));

    s->iSourceSink = ATAFN_SS_NULL;
    atapiR3CmdOK(pCtl, s);
    return false;
}


/**
 * Sink/Source: ATAPI INQUIRY
 */
static bool atapiR3InquirySS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint8_t *pbBuf = s->abIOBuffer;
    RT_NOREF(pDevIns, pDevR3);

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    Assert(s->cbElementaryTransfer <= 36);
    pbBuf[0] = 0x05; /* CD-ROM */
    pbBuf[1] = 0x80; /* removable */
# if 1/*ndef VBOX*/  /** @todo implement MESN + AENC. (async notification on removal and stuff.) */
    pbBuf[2] = 0x00; /* ISO */
    pbBuf[3] = 0x21; /* ATAPI-2 (XXX: put ATAPI-4 ?) */
# else
    pbBuf[2] = 0x00; /* ISO */
    pbBuf[3] = 0x91; /* format 1, MESN=1, AENC=9 ??? */
# endif
    pbBuf[4] = 31; /* additional length */
    pbBuf[5] = 0; /* reserved */
    pbBuf[6] = 0; /* reserved */
    pbBuf[7] = 0; /* reserved */
    scsiPadStr(pbBuf + 8, s->szInquiryVendorId, 8);
    scsiPadStr(pbBuf + 16, s->szInquiryProductId, 16);
    scsiPadStr(pbBuf + 32, s->szInquiryRevision, 4);
    s->iSourceSink = ATAFN_SS_NULL;
    atapiR3CmdOK(pCtl, s);
    return false;
}


/**
 * Sink/Source: ATAPI MODE SENSE ERROR RECOVERY
 */
static bool atapiR3ModeSenseErrorRecoverySS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint8_t *pbBuf = s->abIOBuffer;
    RT_NOREF(pDevIns, pDevR3);

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    Assert(s->cbElementaryTransfer <= 16);
    scsiH2BE_U16(&pbBuf[0], 16 + 6);
    pbBuf[2] = (uint8_t)s->MediaTrackType;
    pbBuf[3] = 0;
    pbBuf[4] = 0;
    pbBuf[5] = 0;
    pbBuf[6] = 0;
    pbBuf[7] = 0;

    pbBuf[8] = 0x01;
    pbBuf[9] = 0x06;
    pbBuf[10] = 0x00;   /* Maximum error recovery */
    pbBuf[11] = 0x05;   /* 5 retries */
    pbBuf[12] = 0x00;
    pbBuf[13] = 0x00;
    pbBuf[14] = 0x00;
    pbBuf[15] = 0x00;
    s->iSourceSink = ATAFN_SS_NULL;
    atapiR3CmdOK(pCtl, s);
    return false;
}


/**
 * Sink/Source: ATAPI MODE SENSE CD STATUS
 */
static bool atapiR3ModeSenseCDStatusSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint8_t *pbBuf = s->abIOBuffer;
    RT_NOREF(pDevIns);

    /* 28 bytes of total returned data corresponds to ATAPI 2.6. Note that at least some versions
     * of NEC_IDE.SYS DOS driver (possibly other Oak Technology OTI-011 drivers) do not correctly
     * handle cases where more than 28 bytes are returned due to bugs. See @bugref{5869}.
     */
    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    Assert(s->cbElementaryTransfer <= 28);
    scsiH2BE_U16(&pbBuf[0], 26);
    pbBuf[2] = (uint8_t)s->MediaTrackType;
    pbBuf[3] = 0;
    pbBuf[4] = 0;
    pbBuf[5] = 0;
    pbBuf[6] = 0;
    pbBuf[7] = 0;

    pbBuf[8] = 0x2a;
    pbBuf[9] = 18; /* page length */
    pbBuf[10] = 0x08; /* DVD-ROM read support */
    pbBuf[11] = 0x00; /* no write support */
    /* The following claims we support audio play. This is obviously false,
     * but the Linux generic CDROM support makes many features depend on this
     * capability. If it's not set, this causes many things to be disabled. */
    pbBuf[12] = 0x71; /* multisession support, mode 2 form 1/2 support, audio play */
    pbBuf[13] = 0x00; /* no subchannel reads supported */
    pbBuf[14] = (1 << 0) | (1 << 3) | (1 << 5); /* lock supported, eject supported, tray type loading mechanism */
    if (pDevR3->pDrvMount && pDevR3->pDrvMount->pfnIsLocked(pDevR3->pDrvMount))
        pbBuf[14] |= 1 << 1; /* report lock state */
    pbBuf[15] = 0; /* no subchannel reads supported, no separate audio volume control, no changer etc. */
    scsiH2BE_U16(&pbBuf[16], 5632); /* (obsolete) claim 32x speed support */
    scsiH2BE_U16(&pbBuf[18], 2); /* number of audio volume levels */
    scsiH2BE_U16(&pbBuf[20], RT_MIN(s->cbIOBuffer, ATA_MAX_IO_BUFFER_SIZE) / _1K); /* buffer size supported in Kbyte */
    scsiH2BE_U16(&pbBuf[22], 5632); /* (obsolete) current read speed 32x */
    pbBuf[24] = 0; /* reserved */
    pbBuf[25] = 0; /* reserved for digital audio (see idx 15) */
    pbBuf[26] = 0; /* reserved */
    pbBuf[27] = 0; /* reserved */
    s->iSourceSink = ATAFN_SS_NULL;
    atapiR3CmdOK(pCtl, s);
    return false;
}


/**
 * Sink/Source: ATAPI REQUEST SENSE
 */
static bool atapiR3RequestSenseSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint8_t *pbBuf = s->abIOBuffer;
    RT_NOREF(pDevIns, pDevR3);

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    memset(pbBuf, '\0', RT_MIN(s->cbElementaryTransfer, sizeof(s->abIOBuffer)));
    AssertCompile(sizeof(s->abIOBuffer) >= sizeof(s->abATAPISense));
    memcpy(pbBuf, s->abATAPISense, RT_MIN(s->cbElementaryTransfer, sizeof(s->abATAPISense)));
    s->iSourceSink = ATAFN_SS_NULL;
    atapiR3CmdOK(pCtl, s);
    return false;
}


/**
 * Sink/Source: ATAPI MECHANISM STATUS
 */
static bool atapiR3MechanismStatusSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint8_t *pbBuf = s->abIOBuffer;
    RT_NOREF(pDevIns, pDevR3);

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    Assert(s->cbElementaryTransfer <= 8);
    scsiH2BE_U16(pbBuf, 0);
    /* no current LBA */
    pbBuf[2] = 0;
    pbBuf[3] = 0;
    pbBuf[4] = 0;
    pbBuf[5] = 1;
    scsiH2BE_U16(pbBuf + 6, 0);
    s->iSourceSink = ATAFN_SS_NULL;
    atapiR3CmdOK(pCtl, s);
    return false;
}


/**
 * Sink/Source: ATAPI READ TOC NORMAL
 */
static bool atapiR3ReadTOCNormalSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint8_t *pbBuf = s->abIOBuffer;
    uint8_t *q;
    uint8_t iStartTrack;
    bool fMSF;
    uint32_t cbSize;
    RT_NOREF(pDevIns);

    /* Track fields are 8-bit and 1-based, so cut the track count at 255,
       avoiding any potential buffer overflow issues below. */
    uint32_t cTracks = pDevR3->pDrvMedia->pfnGetRegionCount(pDevR3->pDrvMedia);
    AssertStmt(cTracks <= UINT8_MAX, cTracks = UINT8_MAX);
    AssertCompile(sizeof(s->abIOBuffer) >= 2 + 256 + 8);

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    fMSF = (s->abATAPICmd[1] >> 1) & 1;
    iStartTrack = s->abATAPICmd[6];
    if (iStartTrack == 0)
        iStartTrack = 1;

    if (iStartTrack > cTracks && iStartTrack != 0xaa)
    {
        atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET);
        return false;
    }
    q = pbBuf + 2;
    *q++ = iStartTrack; /* first track number */
    *q++ = cTracks;     /* last track number */
    for (uint32_t iTrack = iStartTrack; iTrack <= cTracks; iTrack++)
    {
        uint64_t uLbaStart = 0;
        VDREGIONDATAFORM enmDataForm = VDREGIONDATAFORM_MODE1_2048;

        int rc = pDevR3->pDrvMedia->pfnQueryRegionProperties(pDevR3->pDrvMedia, iTrack - 1, &uLbaStart,
                                                             NULL, NULL, &enmDataForm);
        AssertRC(rc);

        *q++ = 0;                  /* reserved */

        if (enmDataForm == VDREGIONDATAFORM_CDDA)
            *q++ = 0x10;           /* ADR, control */
        else
            *q++ = 0x14;           /* ADR, control */

        *q++ = (uint8_t)iTrack;    /* track number */
        *q++ = 0;                  /* reserved */
        if (fMSF)
        {
            *q++ = 0; /* reserved */
            scsiLBA2MSF(q, (uint32_t)uLbaStart);
            q += 3;
        }
        else
        {
            /* sector 0 */
            scsiH2BE_U32(q, (uint32_t)uLbaStart);
            q += 4;
        }
    }
    /* lead out track */
    *q++ = 0; /* reserved */
    *q++ = 0x14; /* ADR, control */
    *q++ = 0xaa; /* track number */
    *q++ = 0; /* reserved */

    /* Query start and length of last track to get the start of the lead out track. */
    uint64_t uLbaStart = 0;
    uint64_t cBlocks = 0;

    int rc = pDevR3->pDrvMedia->pfnQueryRegionProperties(pDevR3->pDrvMedia, cTracks - 1, &uLbaStart,
                                                    &cBlocks, NULL, NULL);
    AssertRC(rc);

    uLbaStart += cBlocks;
    if (fMSF)
    {
        *q++ = 0; /* reserved */
        scsiLBA2MSF(q, (uint32_t)uLbaStart);
        q += 3;
    }
    else
    {
        scsiH2BE_U32(q, (uint32_t)uLbaStart);
        q += 4;
    }
    cbSize = q - pbBuf;
    scsiH2BE_U16(pbBuf, cbSize - 2);
    if (cbSize < s->cbTotalTransfer)
        s->cbTotalTransfer = cbSize;
    s->iSourceSink = ATAFN_SS_NULL;
    atapiR3CmdOK(pCtl, s);
    return false;
}


/**
 * Sink/Source: ATAPI READ TOC MULTI
 */
static bool atapiR3ReadTOCMultiSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint8_t *pbBuf = s->abIOBuffer;
    bool fMSF;
    RT_NOREF(pDevIns);

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    Assert(s->cbElementaryTransfer <= 12);
    fMSF = (s->abATAPICmd[1] >> 1) & 1;
    /* multi session: only a single session defined */
    /** @todo double-check this stuff against what a real drive says for a CD-ROM (not a CD-R)
     * with only a single data session. Maybe solve the problem with "cdrdao read-toc" not being
     * able to figure out whether numbers are in BCD or hex. */
    memset(pbBuf, 0, 12);
    pbBuf[1] = 0x0a;
    pbBuf[2] = 0x01;
    pbBuf[3] = 0x01;

    VDREGIONDATAFORM enmDataForm = VDREGIONDATAFORM_MODE1_2048;
    int rc = pDevR3->pDrvMedia->pfnQueryRegionProperties(pDevR3->pDrvMedia, 0, NULL, NULL, NULL, &enmDataForm);
    AssertRC(rc);

    if (enmDataForm == VDREGIONDATAFORM_CDDA)
        pbBuf[5] = 0x10;           /* ADR, control */
    else
        pbBuf[5] = 0x14;           /* ADR, control */

    pbBuf[6] = 1; /* first track in last complete session */
    if (fMSF)
    {
        pbBuf[8] = 0; /* reserved */
        scsiLBA2MSF(&pbBuf[9], 0);
    }
    else
    {
        /* sector 0 */
        scsiH2BE_U32(pbBuf + 8, 0);
    }
    s->iSourceSink = ATAFN_SS_NULL;
    atapiR3CmdOK(pCtl, s);
    return false;
}


/**
 * Sink/Source: ATAPI READ TOC RAW
 */
static bool atapiR3ReadTOCRawSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    uint8_t *pbBuf = s->abIOBuffer;
    uint8_t *q;
    uint8_t iStartTrack;
    bool fMSF;
    uint32_t cbSize;
    RT_NOREF(pDevIns, pDevR3);

    Assert(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE);
    fMSF = (s->abATAPICmd[1] >> 1) & 1;
    iStartTrack = s->abATAPICmd[6];

    q = pbBuf + 2;
    *q++ = 1; /* first session */
    *q++ = 1; /* last session */

    *q++ = 1; /* session number */
    *q++ = 0x14; /* data track */
    *q++ = 0; /* track number */
    *q++ = 0xa0; /* first track in program area */
    *q++ = 0; /* min */
    *q++ = 0; /* sec */
    *q++ = 0; /* frame */
    *q++ = 0;
    *q++ = 1; /* first track */
    *q++ = 0x00; /* disk type CD-DA or CD data */
    *q++ = 0;

    *q++ = 1; /* session number */
    *q++ = 0x14; /* data track */
    *q++ = 0; /* track number */
    *q++ = 0xa1; /* last track in program area */
    *q++ = 0; /* min */
    *q++ = 0; /* sec */
    *q++ = 0; /* frame */
    *q++ = 0;
    *q++ = 1; /* last track */
    *q++ = 0;
    *q++ = 0;

    *q++ = 1; /* session number */
    *q++ = 0x14; /* data track */
    *q++ = 0; /* track number */
    *q++ = 0xa2; /* lead-out */
    *q++ = 0; /* min */
    *q++ = 0; /* sec */
    *q++ = 0; /* frame */
    if (fMSF)
    {
        *q++ = 0; /* reserved */
        scsiLBA2MSF(q, s->cTotalSectors);
        q += 3;
    }
    else
    {
        scsiH2BE_U32(q, s->cTotalSectors);
        q += 4;
    }

    *q++ = 1; /* session number */
    *q++ = 0x14; /* ADR, control */
    *q++ = 0;    /* track number */
    *q++ = 1;    /* point */
    *q++ = 0; /* min */
    *q++ = 0; /* sec */
    *q++ = 0; /* frame */
    if (fMSF)
    {
        *q++ = 0; /* reserved */
        scsiLBA2MSF(q, 0);
        q += 3;
    }
    else
    {
        /* sector 0 */
        scsiH2BE_U32(q, 0);
        q += 4;
    }

    cbSize = q - pbBuf;
    scsiH2BE_U16(pbBuf, cbSize - 2);
    if (cbSize < s->cbTotalTransfer)
        s->cbTotalTransfer = cbSize;
    s->iSourceSink = ATAFN_SS_NULL;
    atapiR3CmdOK(pCtl, s);
    return false;
}


static void atapiR3ParseCmdVirtualATAPI(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    const uint8_t *pbPacket = s->abATAPICmd;
    uint32_t cbMax;
    uint32_t cSectors, iATAPILBA;

    switch (pbPacket[0])
    {
        case SCSI_TEST_UNIT_READY:
            if (s->cNotifiedMediaChange > 0)
            {
                if (s->cNotifiedMediaChange-- > 2)
                    atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT);
                else
                    atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_UNIT_ATTENTION, SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED); /* media changed */
            }
            else
            {
                PPDMIMOUNT const pDrvMount = pDevR3->pDrvMount;
                if (pDrvMount && pDrvMount->pfnIsMounted(pDrvMount))
                    atapiR3CmdOK(pCtl, s);
                else
                    atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT);
            }
            break;
        case SCSI_GET_EVENT_STATUS_NOTIFICATION:
            cbMax = scsiBE2H_U16(pbPacket + 7);
            ataR3StartTransfer(pDevIns, pCtl, s, RT_MIN(cbMax, 8), PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_GET_EVENT_STATUS_NOTIFICATION, true);
            break;
        case SCSI_MODE_SENSE_10:
        {
            uint8_t uPageControl, uPageCode;
            cbMax = scsiBE2H_U16(pbPacket + 7);
            uPageControl = pbPacket[2] >> 6;
            uPageCode = pbPacket[2] & 0x3f;
            switch (uPageControl)
            {
                case SCSI_PAGECONTROL_CURRENT:
                    switch (uPageCode)
                    {
                        case SCSI_MODEPAGE_ERROR_RECOVERY:
                            ataR3StartTransfer(pDevIns, pCtl, s, RT_MIN(cbMax, 16), PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_MODE_SENSE_ERROR_RECOVERY, true);
                            break;
                        case SCSI_MODEPAGE_CD_STATUS:
                            ataR3StartTransfer(pDevIns, pCtl, s, RT_MIN(cbMax, 28), PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_MODE_SENSE_CD_STATUS, true);
                            break;
                        default:
                            goto error_cmd;
                    }
                    break;
                case SCSI_PAGECONTROL_CHANGEABLE:
                    goto error_cmd;
                case SCSI_PAGECONTROL_DEFAULT:
                    goto error_cmd;
                default:
                case SCSI_PAGECONTROL_SAVED:
                    atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_SAVING_PARAMETERS_NOT_SUPPORTED);
                    break;
            }
            break;
        }
        case SCSI_REQUEST_SENSE:
            cbMax = pbPacket[4];
            ataR3StartTransfer(pDevIns, pCtl, s, RT_MIN(cbMax, 18), PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_REQUEST_SENSE, true);
            break;
        case SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL:
        {
            PPDMIMOUNT const pDrvMount = pDevR3->pDrvMount;
            if (pDrvMount && pDrvMount->pfnIsMounted(pDrvMount))
            {
                if (pbPacket[4] & 1)
                    pDrvMount->pfnLock(pDrvMount);
                else
                    pDrvMount->pfnUnlock(pDrvMount);
                atapiR3CmdOK(pCtl, s);
            }
            else
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT);
            break;
        }
        case SCSI_READ_10:
        case SCSI_READ_12:
        {
            if (s->cNotifiedMediaChange > 0)
            {
                s->cNotifiedMediaChange-- ;
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_UNIT_ATTENTION, SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED); /* media changed */
                break;
            }
            if (!pDevR3->pDrvMount || !pDevR3->pDrvMount->pfnIsMounted(pDevR3->pDrvMount))
            {
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT);
                break;
            }
            if (pbPacket[0] == SCSI_READ_10)
                cSectors = scsiBE2H_U16(pbPacket + 7);
            else
                cSectors = scsiBE2H_U32(pbPacket + 6);
            iATAPILBA = scsiBE2H_U32(pbPacket + 2);

            if (cSectors == 0)
            {
                atapiR3CmdOK(pCtl, s);
                break;
            }

            /* Check that the sector size is valid. */
            VDREGIONDATAFORM enmDataForm = VDREGIONDATAFORM_INVALID;
            int rc = pDevR3->pDrvMedia->pfnQueryRegionPropertiesForLba(pDevR3->pDrvMedia, iATAPILBA,
                                                                       NULL, NULL, NULL, &enmDataForm);
            if (RT_UNLIKELY(   rc == VERR_NOT_FOUND
                            || ((uint64_t)iATAPILBA + cSectors > s->cTotalSectors)))
            {
                /* Rate limited logging, one log line per second. For
                 * guests that insist on reading from places outside the
                 * valid area this often generates too many release log
                 * entries otherwise. */
                static uint64_t uLastLogTS = 0;
                if (RTTimeMilliTS() >= uLastLogTS + 1000)
                {
                    LogRel(("PIIX3 ATA: LUN#%d: CD-ROM block number %Ld invalid (READ)\n", s->iLUN, (uint64_t)iATAPILBA + cSectors));
                    uLastLogTS = RTTimeMilliTS();
                }
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_LOGICAL_BLOCK_OOR);
                break;
            }
            else if (   enmDataForm != VDREGIONDATAFORM_MODE1_2048
                     && enmDataForm != VDREGIONDATAFORM_MODE1_2352
                     && enmDataForm != VDREGIONDATAFORM_MODE2_2336
                     && enmDataForm != VDREGIONDATAFORM_MODE2_2352
                     && enmDataForm != VDREGIONDATAFORM_RAW)
            {
                uint8_t abATAPISense[ATAPI_SENSE_SIZE];
                RT_ZERO(abATAPISense);

                abATAPISense[0] = 0x70 | (1 << 7);
                abATAPISense[2] = (SCSI_SENSE_ILLEGAL_REQUEST & 0x0f) | SCSI_SENSE_FLAG_ILI;
                scsiH2BE_U32(&abATAPISense[3], iATAPILBA);
                abATAPISense[7] = 10;
                abATAPISense[12] = SCSI_ASC_ILLEGAL_MODE_FOR_THIS_TRACK;
                atapiR3CmdError(pCtl, s, &abATAPISense[0], sizeof(abATAPISense));
                break;
            }
            atapiR3ReadSectors(pDevIns, pCtl, s, iATAPILBA, cSectors, 2048);
            break;
        }
        case SCSI_READ_CD_MSF:
        case SCSI_READ_CD:
        {
            if (s->cNotifiedMediaChange > 0)
            {
                s->cNotifiedMediaChange-- ;
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_UNIT_ATTENTION, SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED); /* media changed */
                break;
            }
            if (!pDevR3->pDrvMount || !pDevR3->pDrvMount->pfnIsMounted(pDevR3->pDrvMount))
            {
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT);
                break;
            }
            if ((pbPacket[10] & 0x7) != 0)
            {
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET);
                break;
            }
            if (pbPacket[0] == SCSI_READ_CD)
            {
                cSectors = (pbPacket[6] << 16) | (pbPacket[7] << 8) | pbPacket[8];
                iATAPILBA = scsiBE2H_U32(pbPacket + 2);
            }
            else    /* READ CD MSF */
            {
                iATAPILBA = scsiMSF2LBA(pbPacket + 3);
                if (iATAPILBA > scsiMSF2LBA(pbPacket + 6))
                {
                    Log2(("Start MSF %02u:%02u:%02u > end MSF  %02u:%02u:%02u!\n", *(pbPacket + 3), *(pbPacket + 4), *(pbPacket + 5),
                          *(pbPacket + 6), *(pbPacket + 7), *(pbPacket + 8)));
                    atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET);
                    break;
                }
                cSectors = scsiMSF2LBA(pbPacket + 6) - iATAPILBA;
                Log2(("Start MSF %02u:%02u:%02u -> LBA %u\n", *(pbPacket + 3), *(pbPacket + 4), *(pbPacket + 5), iATAPILBA));
                Log2(("End   MSF %02u:%02u:%02u -> %u sectors\n", *(pbPacket + 6), *(pbPacket + 7), *(pbPacket + 8), cSectors));
            }
            if (cSectors == 0)
            {
                atapiR3CmdOK(pCtl, s);
                break;
            }
            if ((uint64_t)iATAPILBA + cSectors > s->cTotalSectors)
            {
                /* Rate limited logging, one log line per second. For
                 * guests that insist on reading from places outside the
                 * valid area this often generates too many release log
                 * entries otherwise. */
                static uint64_t uLastLogTS = 0;
                if (RTTimeMilliTS() >= uLastLogTS + 1000)
                {
                    LogRel(("PIIX3 ATA: LUN#%d: CD-ROM block number %Ld invalid (READ CD)\n", s->iLUN, (uint64_t)iATAPILBA + cSectors));
                    uLastLogTS = RTTimeMilliTS();
                }
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_LOGICAL_BLOCK_OOR);
                break;
            }
            /*
             * If the LBA is in an audio track we are required to ignore pretty much all
             * of the channel selection values (except 0x00) and map everything to 0x10
             * which means read user data with a sector size of 2352 bytes.
             *
             * (MMC-6 chapter 6.19.2.6)
             */
            uint8_t uChnSel = pbPacket[9] & 0xf8;
            VDREGIONDATAFORM enmDataForm;
            int rc = pDevR3->pDrvMedia->pfnQueryRegionPropertiesForLba(pDevR3->pDrvMedia, iATAPILBA,
                                                                       NULL, NULL, NULL, &enmDataForm);
            AssertRC(rc);

            if (enmDataForm == VDREGIONDATAFORM_CDDA)
            {
                if (uChnSel == 0)
                {
                    /* nothing */
                    atapiR3CmdOK(pCtl, s);
                }
                else
                    atapiR3ReadSectors(pDevIns, pCtl, s, iATAPILBA, cSectors, 2352);
            }
            else
            {
                switch (uChnSel)
                {
                    case 0x00:
                        /* nothing */
                        atapiR3CmdOK(pCtl, s);
                        break;
                    case 0x10:
                        /* normal read */
                        atapiR3ReadSectors(pDevIns, pCtl, s, iATAPILBA, cSectors, 2048);
                        break;
                    case 0xf8:
                        /* read all data */
                        atapiR3ReadSectors(pDevIns, pCtl, s, iATAPILBA, cSectors, 2352);
                        break;
                    default:
                        LogRel(("PIIX3 ATA: LUN#%d: CD-ROM sector format not supported (%#x)\n", s->iLUN, pbPacket[9] & 0xf8));
                        atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET);
                        break;
                }
            }
            break;
        }
        case SCSI_SEEK_10:
        {
            if (s->cNotifiedMediaChange > 0)
            {
                s->cNotifiedMediaChange-- ;
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_UNIT_ATTENTION, SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED); /* media changed */
                break;
            }
            if (!pDevR3->pDrvMount || !pDevR3->pDrvMount->pfnIsMounted(pDevR3->pDrvMount))
            {
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT);
                break;
            }
            iATAPILBA = scsiBE2H_U32(pbPacket + 2);
            if (iATAPILBA > s->cTotalSectors)
            {
                /* Rate limited logging, one log line per second. For
                 * guests that insist on seeking to places outside the
                 * valid area this often generates too many release log
                 * entries otherwise. */
                static uint64_t uLastLogTS = 0;
                if (RTTimeMilliTS() >= uLastLogTS + 1000)
                {
                    LogRel(("PIIX3 ATA: LUN#%d: CD-ROM block number %Ld invalid (SEEK)\n", s->iLUN, (uint64_t)iATAPILBA));
                    uLastLogTS = RTTimeMilliTS();
                }
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_LOGICAL_BLOCK_OOR);
                break;
            }
            atapiR3CmdOK(pCtl, s);
            ataSetStatus(pCtl, s, ATA_STAT_SEEK); /* Linux expects this. Required by ATAPI 2.x when seek completes. */
            break;
        }
        case SCSI_START_STOP_UNIT:
        {
            int rc = VINF_SUCCESS;
            switch (pbPacket[4] & 3)
            {
                case 0: /* 00 - Stop motor */
                case 1: /* 01 - Start motor */
                    break;
                case 2: /* 10 - Eject media */
                {
                    /* This must be done from EMT. */
                    PATASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PATASTATER3);
                    PPDMIMOUNT  pDrvMount = pDevR3->pDrvMount;
                    if (pDrvMount)
                    {
                        ataR3LockLeave(pDevIns, pCtl);

                        rc = PDMDevHlpVMReqPriorityCallWait(pDevIns, VMCPUID_ANY,
                                                            (PFNRT)pDrvMount->pfnUnmount, 3,
                                                            pDrvMount, false /*=fForce*/, true /*=fEject*/);
                        Assert(RT_SUCCESS(rc) || rc == VERR_PDM_MEDIA_LOCKED || rc == VERR_PDM_MEDIA_NOT_MOUNTED);
                        if (RT_SUCCESS(rc) && pThisCC->pMediaNotify)
                        {
                            rc = PDMDevHlpVMReqCallNoWait(pDevIns, VMCPUID_ANY,
                                                          (PFNRT)pThisCC->pMediaNotify->pfnEjected, 2,
                                                          pThisCC->pMediaNotify, s->iLUN);
                            AssertRC(rc);
                        }

                        ataR3LockEnter(pDevIns, pCtl);
                    }
                    else
                        rc = VINF_SUCCESS;
                    break;
                }
                case 3: /* 11 - Load media */
                    /** @todo rc = pDevR3->pDrvMount->pfnLoadMedia(pDevR3->pDrvMount) */
                    break;
            }
            if (RT_SUCCESS(rc))
            {
                atapiR3CmdOK(pCtl, s);
                ataSetStatus(pCtl, s, ATA_STAT_SEEK);   /* Needed by NT 3.51/4.0, see @bugref{5869}. */
            }
            else
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIA_LOAD_OR_EJECT_FAILED);
            break;
        }
        case SCSI_MECHANISM_STATUS:
        {
            cbMax = scsiBE2H_U16(pbPacket + 8);
            ataR3StartTransfer(pDevIns, pCtl, s, RT_MIN(cbMax, 8), PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_MECHANISM_STATUS, true);
            break;
        }
        case SCSI_READ_TOC_PMA_ATIP:
        {
            uint8_t format;

            if (s->cNotifiedMediaChange > 0)
            {
                s->cNotifiedMediaChange-- ;
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_UNIT_ATTENTION, SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED); /* media changed */
                break;
            }
            if (!pDevR3->pDrvMount || !pDevR3->pDrvMount->pfnIsMounted(pDevR3->pDrvMount))
            {
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT);
                break;
            }
            cbMax = scsiBE2H_U16(pbPacket + 7);
            /* SCSI MMC-3 spec says format is at offset 2 (lower 4 bits),
             * but Linux kernel uses offset 9 (topmost 2 bits). Hope that
             * the other field is clear... */
            format = (pbPacket[2] & 0xf) | (pbPacket[9] >> 6);
            switch (format)
            {
                case 0:
                    ataR3StartTransfer(pDevIns, pCtl, s, cbMax, PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_READ_TOC_NORMAL, true);
                    break;
                case 1:
                    ataR3StartTransfer(pDevIns, pCtl, s, RT_MIN(cbMax, 12), PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_READ_TOC_MULTI, true);
                    break;
                case 2:
                    ataR3StartTransfer(pDevIns, pCtl, s, cbMax, PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_READ_TOC_RAW, true);
                    break;
                default:
                  error_cmd:
                    atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET);
                    break;
            }
            break;
        }
        case SCSI_READ_CAPACITY:
            if (s->cNotifiedMediaChange > 0)
            {
                s->cNotifiedMediaChange-- ;
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_UNIT_ATTENTION, SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED); /* media changed */
                break;
            }
            if (!pDevR3->pDrvMount || !pDevR3->pDrvMount->pfnIsMounted(pDevR3->pDrvMount))
            {
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT);
                break;
            }
            ataR3StartTransfer(pDevIns, pCtl, s, 8, PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_READ_CAPACITY, true);
            break;
        case SCSI_READ_DISC_INFORMATION:
            if (s->cNotifiedMediaChange > 0)
            {
                s->cNotifiedMediaChange-- ;
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_UNIT_ATTENTION, SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED); /* media changed */
                break;
            }
            if (!pDevR3->pDrvMount || !pDevR3->pDrvMount->pfnIsMounted(pDevR3->pDrvMount))
            {
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT);
                break;
            }
            cbMax = scsiBE2H_U16(pbPacket + 7);
            ataR3StartTransfer(pDevIns, pCtl, s, RT_MIN(cbMax, 34), PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_READ_DISC_INFORMATION, true);
            break;
        case SCSI_READ_TRACK_INFORMATION:
            if (s->cNotifiedMediaChange > 0)
            {
                s->cNotifiedMediaChange-- ;
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_UNIT_ATTENTION, SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED); /* media changed */
                break;
            }
            if (!pDevR3->pDrvMount || !pDevR3->pDrvMount->pfnIsMounted(pDevR3->pDrvMount))
            {
                atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT);
                break;
            }
            cbMax = scsiBE2H_U16(pbPacket + 7);
            ataR3StartTransfer(pDevIns, pCtl, s, RT_MIN(cbMax, 36), PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_READ_TRACK_INFORMATION, true);
            break;
        case SCSI_GET_CONFIGURATION:
            /* No media change stuff here, it can confuse Linux guests. */
            cbMax = scsiBE2H_U16(pbPacket + 7);
            ataR3StartTransfer(pDevIns, pCtl, s, RT_MIN(cbMax, 80), PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_GET_CONFIGURATION, true);
            break;
        case SCSI_INQUIRY:
            cbMax = scsiBE2H_U16(pbPacket + 3);
            ataR3StartTransfer(pDevIns, pCtl, s, RT_MIN(cbMax, 36), PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_INQUIRY, true);
            break;
        case SCSI_READ_DVD_STRUCTURE:
            cbMax = scsiBE2H_U16(pbPacket + 8);
            ataR3StartTransfer(pDevIns, pCtl, s, RT_MIN(cbMax, 4), PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_READ_DVD_STRUCTURE, true);
            break;
        default:
            atapiR3CmdErrorSimple(pCtl, s, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_ILLEGAL_OPCODE);
            break;
    }
}


/*
 * Parse ATAPI commands, passing them directly to the CD/DVD drive.
 */
static void atapiR3ParseCmdPassthrough(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    const uint8_t *pbPacket = &s->abATAPICmd[0];

    /* Some cases we have to handle here. */
    if (   pbPacket[0] == SCSI_GET_EVENT_STATUS_NOTIFICATION
        && ASMAtomicReadU32(&s->MediaEventStatus) != ATA_EVENT_STATUS_UNCHANGED)
    {
        uint32_t cbTransfer = scsiBE2H_U16(pbPacket + 7);
        ataR3StartTransfer(pDevIns, pCtl, s, RT_MIN(cbTransfer, 8), PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_GET_EVENT_STATUS_NOTIFICATION, true);
    }
    else if (   pbPacket[0] == SCSI_REQUEST_SENSE
             && (s->abATAPISense[2] & 0x0f) != SCSI_SENSE_NONE)
        ataR3StartTransfer(pDevIns, pCtl, s, RT_MIN(pbPacket[4], 18), PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_ATAPI_CMD, ATAFN_SS_ATAPI_REQUEST_SENSE, true);
    else
    {
        size_t cbBuf = 0;
        size_t cbATAPISector = 0;
        size_t cbTransfer = 0;
        PDMMEDIATXDIR uTxDir = PDMMEDIATXDIR_NONE;
        uint8_t u8ScsiSts = SCSI_STATUS_OK;

        if (pbPacket[0] == SCSI_FORMAT_UNIT || pbPacket[0] == SCSI_GET_PERFORMANCE)
            cbBuf = s->uATARegLCyl | (s->uATARegHCyl << 8); /* use ATAPI transfer length */

        bool fPassthrough = ATAPIPassthroughParseCdb(pbPacket, sizeof(s->abATAPICmd), cbBuf, pDevR3->pTrackList,
                                                     &s->abATAPISense[0], sizeof(s->abATAPISense), &uTxDir, &cbTransfer,
                                                     &cbATAPISector, &u8ScsiSts);
        if (fPassthrough)
        {
            s->cbATAPISector = (uint32_t)cbATAPISector;
            Assert(s->cbATAPISector == (uint32_t)cbATAPISector);
            Assert(cbTransfer == (uint32_t)cbTransfer);

            /*
             * Send a command to the drive, passing data in/out as required.
             * Commands which exceed the I/O buffer size are split below
             * or aborted if splitting is not implemented.
             */
            Log2(("ATAPI PT: max size %d\n", cbTransfer));
            if (cbTransfer == 0)
                uTxDir = PDMMEDIATXDIR_NONE;
            ataR3StartTransfer(pDevIns, pCtl, s, (uint32_t)cbTransfer, uTxDir, ATAFN_BT_ATAPI_PASSTHROUGH_CMD, ATAFN_SS_ATAPI_PASSTHROUGH, true);
        }
        else if (u8ScsiSts == SCSI_STATUS_CHECK_CONDITION)
        {
            /* Sense data is already set, end the request and notify the guest. */
            Log(("%s: sense=%#x (%s) asc=%#x ascq=%#x (%s)\n", __FUNCTION__, s->abATAPISense[2] & 0x0f, SCSISenseText(s->abATAPISense[2] & 0x0f),
                     s->abATAPISense[12], s->abATAPISense[13], SCSISenseExtText(s->abATAPISense[12], s->abATAPISense[13])));
            s->uATARegError = s->abATAPISense[2] << 4;
            ataSetStatusValue(pCtl, s, ATA_STAT_READY | ATA_STAT_ERR);
            s->uATARegNSector = (s->uATARegNSector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
            Log2(("%s: interrupt reason %#04x\n", __FUNCTION__, s->uATARegNSector));
            s->cbTotalTransfer = 0;
            s->cbElementaryTransfer = 0;
            s->cbAtapiPassthroughTransfer = 0;
            s->iIOBufferCur = 0;
            s->iIOBufferEnd = 0;
            s->uTxDir = PDMMEDIATXDIR_NONE;
            s->iBeginTransfer = ATAFN_BT_NULL;
            s->iSourceSink = ATAFN_SS_NULL;
        }
        else if (u8ScsiSts == SCSI_STATUS_OK)
            atapiR3CmdOK(pCtl, s);
    }
}


static void atapiR3ParseCmd(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    const uint8_t *pbPacket;

    pbPacket = s->abATAPICmd;
# ifdef DEBUG
    Log(("%s: LUN#%d DMA=%d CMD=%#04x \"%s\"\n", __FUNCTION__, s->iLUN, s->fDMA, pbPacket[0], SCSICmdText(pbPacket[0])));
# else /* !DEBUG */
    Log(("%s: LUN#%d DMA=%d CMD=%#04x\n", __FUNCTION__, s->iLUN, s->fDMA, pbPacket[0]));
# endif /* !DEBUG */
    Log2(("%s: limit=%#x packet: %.*Rhxs\n", __FUNCTION__, s->uATARegLCyl | (s->uATARegHCyl << 8), ATAPI_PACKET_SIZE, pbPacket));

    if (s->fATAPIPassthrough)
        atapiR3ParseCmdPassthrough(pDevIns, pCtl, s, pDevR3);
    else
        atapiR3ParseCmdVirtualATAPI(pDevIns, pCtl, s, pDevR3);
}


/**
 * Sink/Source: PACKET
 */
static bool ataR3PacketSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    s->fDMA = !!(s->uATARegFeature & 1);
    memcpy(s->abATAPICmd, s->abIOBuffer, ATAPI_PACKET_SIZE);
    s->uTxDir = PDMMEDIATXDIR_NONE;
    s->cbTotalTransfer = 0;
    s->cbElementaryTransfer = 0;
    s->cbAtapiPassthroughTransfer = 0;
    atapiR3ParseCmd(pDevIns, pCtl, s, pDevR3);
    return false;
}


/**
 * SCSI_GET_EVENT_STATUS_NOTIFICATION should return "medium removed" event
 * from now on, regardless if there was a medium inserted or not.
 */
static void ataR3MediumRemoved(PATADEVSTATE s)
{
    ASMAtomicWriteU32(&s->MediaEventStatus, ATA_EVENT_STATUS_MEDIA_REMOVED);
}


/**
 * SCSI_GET_EVENT_STATUS_NOTIFICATION should return "medium inserted". If
 * there was already a medium inserted, don't forget to send the "medium
 * removed" event first.
 */
static void ataR3MediumInserted(PATADEVSTATE s)
{
    uint32_t OldStatus, NewStatus;
    do
    {
        OldStatus = ASMAtomicReadU32(&s->MediaEventStatus);
        switch (OldStatus)
        {
            case ATA_EVENT_STATUS_MEDIA_CHANGED:
            case ATA_EVENT_STATUS_MEDIA_REMOVED:
                /* no change, we will send "medium removed" + "medium inserted" */
                NewStatus = ATA_EVENT_STATUS_MEDIA_CHANGED;
                break;
            default:
                NewStatus = ATA_EVENT_STATUS_MEDIA_NEW;
                break;
        }
    } while (!ASMAtomicCmpXchgU32(&s->MediaEventStatus, NewStatus, OldStatus));
}


/**
 * @interface_method_impl{PDMIMOUNTNOTIFY,pfnMountNotify}
 */
static DECLCALLBACK(void) ataR3MountNotify(PPDMIMOUNTNOTIFY pInterface)
{
    PATADEVSTATER3  pIfR3 = RT_FROM_MEMBER(pInterface, ATADEVSTATER3, IMountNotify);
    PATASTATE       pThis = PDMDEVINS_2_DATA(pIfR3->pDevIns, PATASTATE);
    PATADEVSTATE    pIf   = &RT_SAFE_SUBSCRIPT(RT_SAFE_SUBSCRIPT(pThis->aCts, pIfR3->iCtl).aIfs, pIfR3->iDev);
    Log(("%s: changing LUN#%d\n", __FUNCTION__, pIfR3->iLUN));

    /* Ignore the call if we're called while being attached. */
    if (!pIfR3->pDrvMedia)
        return;

    uint32_t cRegions = pIfR3->pDrvMedia->pfnGetRegionCount(pIfR3->pDrvMedia);
    for (uint32_t i = 0; i < cRegions; i++)
    {
        uint64_t cBlocks = 0;
        int rc = pIfR3->pDrvMedia->pfnQueryRegionProperties(pIfR3->pDrvMedia, i, NULL, &cBlocks, NULL, NULL);
        AssertRC(rc);
        pIf->cTotalSectors += cBlocks;
    }

    LogRel(("PIIX3 ATA: LUN#%d: CD/DVD, total number of sectors %Ld, passthrough unchanged\n", pIf->iLUN, pIf->cTotalSectors));

    /* Report media changed in TEST UNIT and other (probably incorrect) places. */
    if (pIf->cNotifiedMediaChange < 2)
        pIf->cNotifiedMediaChange = 1;
    ataR3MediumInserted(pIf);
    ataR3MediumTypeSet(pIf, ATA_MEDIA_TYPE_UNKNOWN);
}

/**
 * @interface_method_impl{PDMIMOUNTNOTIFY,pfnUnmountNotify}
 */
static DECLCALLBACK(void) ataR3UnmountNotify(PPDMIMOUNTNOTIFY pInterface)
{
    PATADEVSTATER3  pIfR3 = RT_FROM_MEMBER(pInterface, ATADEVSTATER3, IMountNotify);
    PATASTATE       pThis = PDMDEVINS_2_DATA(pIfR3->pDevIns, PATASTATE);
    PATADEVSTATE    pIf   = &RT_SAFE_SUBSCRIPT(RT_SAFE_SUBSCRIPT(pThis->aCts, pIfR3->iCtl).aIfs, pIfR3->iDev);
    Log(("%s:\n", __FUNCTION__));
    pIf->cTotalSectors = 0;

    /*
     * Whatever I do, XP will not use the GET MEDIA STATUS nor the EVENT stuff.
     * However, it will respond to TEST UNIT with a 0x6 0x28 (media changed) sense code.
     * So, we'll give it 4 TEST UNIT command to catch up, two which the media is not
     * present and 2 in which it is changed.
     */
    pIf->cNotifiedMediaChange = 1;
    ataR3MediumRemoved(pIf);
    ataR3MediumTypeSet(pIf, ATA_MEDIA_NO_DISC);
}

/**
 * Begin Transfer: PACKET
 */
static void ataR3PacketBT(PATACONTROLLER pCtl, PATADEVSTATE s)
{
    s->cbElementaryTransfer = s->cbTotalTransfer;
    s->cbAtapiPassthroughTransfer = s->cbTotalTransfer;
    s->uATARegNSector = (s->uATARegNSector & ~7) | ATAPI_INT_REASON_CD;
    Log2(("%s: interrupt reason %#04x\n", __FUNCTION__, s->uATARegNSector));
    ataSetStatusValue(pCtl, s, ATA_STAT_READY);
}


static void ataR3ResetDevice(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s)
{
    LogFlowFunc(("\n"));
    s->cMultSectors = ATA_MAX_MULT_SECTORS;
    s->cNotifiedMediaChange = 0;
    ASMAtomicWriteU32(&s->MediaEventStatus, ATA_EVENT_STATUS_UNCHANGED);
    ASMAtomicWriteU32(&s->MediaTrackType, ATA_MEDIA_TYPE_UNKNOWN);
    ataUnsetIRQ(pDevIns, pCtl, s);

    s->uATARegSelect = 0x20;
    ataSetStatusValue(pCtl, s, ATA_STAT_READY | ATA_STAT_SEEK);
    ataR3SetSignature(s);
    s->cbTotalTransfer = 0;
    s->cbElementaryTransfer = 0;
    s->cbAtapiPassthroughTransfer = 0;
    s->iIOBufferPIODataStart = 0;
    s->iIOBufferPIODataEnd = 0;
    s->iBeginTransfer = ATAFN_BT_NULL;
    s->iSourceSink = ATAFN_SS_NULL;
    s->fDMA = false;
    s->fATAPITransfer = false;
    s->uATATransferMode = ATA_MODE_UDMA | 2; /* PIIX3 supports only up to UDMA2 */

    s->XCHSGeometry = s->PCHSGeometry;  /* Restore default CHS translation. */

    s->uATARegFeature = 0;
}


static void ataR3DeviceDiag(PATACONTROLLER pCtl, PATADEVSTATE s)
{
    ataR3SetSignature(s);
    if (s->fATAPI)
        ataSetStatusValue(pCtl, s, 0); /* NOTE: READY is _not_ set */
    else
        ataSetStatusValue(pCtl, s, ATA_STAT_READY | ATA_STAT_SEEK);
    s->uATARegError = 0x01;
}


/**
 * Sink/Source: EXECUTE DEVICE DIAGNOTIC
 */
static bool ataR3ExecuteDeviceDiagnosticSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    RT_NOREF(pDevIns, s, pDevR3);

    /* EXECUTE DEVICE DIAGNOSTIC is a very special command which always
     * gets executed, regardless of which device is selected. As a side
     * effect, it always completes with device 0 selected.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(pCtl->aIfs); i++)
        ataR3DeviceDiag(pCtl, &pCtl->aIfs[i]);

    LogRel(("ATA: LUN#%d: EXECUTE DEVICE DIAGNOSTIC, status %02X\n", s->iLUN, s->uATARegStatus));
    pCtl->iSelectedIf = 0;

    return false;
}


/**
 * Sink/Source: INITIALIZE DEVICE PARAMETERS
 */
static bool ataR3InitDevParmSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    RT_NOREF(pDevR3);
    LogFlowFunc(("\n"));

    /* Technical Note:
     * On ST506 type drives with a separate controller, the INITIALIZE DRIVE PARAMETERS command was
     * required to inform the controller of drive geometry. The controller needed to know the
     * number of heads and sectors per track so that it could correctly advance to the next track
     * or cylinder when executing multi-sector commands. Setting a geometry that didn't match the
     * drive made very little sense because sectors had fixed CHS addresses. It was at best
     * possible to reduce the drive's capacity by limiting the number of heads and/or sectors
     * per track.
     *
     * IDE drives inherently have to know their true geometry, but most of them also support
     * programmable translation that can be set through the INITIALIZE DEVICE PARAMETERS command.
     * In fact most older IDE drives typically weren't operated using their default (native) geometry,
     * and with newer IDE drives that's not even an option.
     *
     * Up to and including ATA-5, the standard defined a CHS to LBA translation (since ATA-6, CHS
     * support is optional):
     *
     * LBA = (((cyl_num * heads_per_cyl) + head_num) * sectors_per_track) + sector_num - 1
     *
     * The INITIALIZE DEVICE PARAMETERS command sets the heads_per_cyl and sectors_per_track
     * values used in the above formula.
     *
     * Drives must obviously support an INITIALIZE DRIVE PARAMETERS command matching the drive's
     * default CHS translation. Everything else is optional.
     *
     * We support any geometry with non-zero sectors per track because there's no reason not to;
     * this behavior is common in many if not most IDE drives.
     */

    PDMMEDIAGEOMETRY    Geom = { 0 };

    Geom.cHeads   = (s->uATARegSelect & 0x0f) + 1;  /* Effective range 1-16. */
    Geom.cSectors = s->uATARegNSector;              /* Range 0-255, zero is not valid. */

    if (Geom.cSectors)
    {
        uint64_t cCylinders = s->cTotalSectors / (Geom.cHeads * Geom.cSectors);
        Geom.cCylinders = RT_MAX(RT_MIN(cCylinders, 16383), 1);

        s->XCHSGeometry = Geom;

        ataR3LockLeave(pDevIns, pCtl);
        LogRel(("ATA: LUN#%d: INITIALIZE DEVICE PARAMETERS: %u sectors per track, %u heads\n",
                s->iLUN, s->uATARegNSector, (s->uATARegSelect & 0x0f) + 1));
        RTThreadSleep(pCtl->msDelayIRQ);
        ataR3LockEnter(pDevIns, pCtl);
        ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
    }
    else
    {
        ataR3LockLeave(pDevIns, pCtl);
        LogRel(("ATA: LUN#%d: INITIALIZE DEVICE PARAMETERS error (zero sectors per track)!\n", s->iLUN));
        RTThreadSleep(pCtl->msDelayIRQ);
        ataR3LockEnter(pDevIns, pCtl);
        ataR3CmdError(pCtl, s, ABRT_ERR);
    }
    return false;
}


/**
 * Sink/Source: RECALIBRATE
 */
static bool ataR3RecalibrateSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    RT_NOREF(pDevR3);
    LogFlowFunc(("\n"));
    ataR3LockLeave(pDevIns, pCtl);
    RTThreadSleep(pCtl->msDelayIRQ);
    ataR3LockEnter(pDevIns, pCtl);
    ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
    return false;
}


static int ataR3TrimSectors(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3,
                            uint64_t u64Sector, uint32_t cSectors, bool *pfRedo)
{
    RTRANGE TrimRange;
    int rc;

    ataR3LockLeave(pDevIns, pCtl);

    TrimRange.offStart = u64Sector * s->cbSector;
    TrimRange.cbRange  = cSectors * s->cbSector;

    s->Led.Asserted.s.fWriting = s->Led.Actual.s.fWriting = 1;
    rc = pDevR3->pDrvMedia->pfnDiscard(pDevR3->pDrvMedia, &TrimRange, 1);
    s->Led.Actual.s.fWriting = 0;

    if (RT_SUCCESS(rc))
        *pfRedo = false;
    else
        *pfRedo = ataR3IsRedoSetWarning(pDevIns, pCtl, rc);

    ataR3LockEnter(pDevIns, pCtl);
    return rc;
}


/**
 * Sink/Source: TRIM
 */
static bool ataR3TrimSS(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3)
{
    int rc = VERR_GENERAL_FAILURE;
    uint32_t cRangesMax;
    uint64_t *pu64Range = (uint64_t *)&s->abIOBuffer[0];
    bool fRedo = false;

    cRangesMax = RT_MIN(s->cbElementaryTransfer, sizeof(s->abIOBuffer)) / sizeof(uint64_t);
    Assert(cRangesMax);

    while (cRangesMax-- > 0)
    {
        if (ATA_RANGE_LENGTH_GET(*pu64Range) == 0)
            break;

        rc = ataR3TrimSectors(pDevIns, pCtl, s, pDevR3, *pu64Range & ATA_RANGE_LBA_MASK,
                              ATA_RANGE_LENGTH_GET(*pu64Range), &fRedo);
        if (RT_FAILURE(rc))
            break;

        pu64Range++;
    }

    if (RT_SUCCESS(rc))
    {
        s->iSourceSink = ATAFN_SS_NULL;
        ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
    }
    else
    {
        if (fRedo)
            return fRedo;
        if (s->cErrors++ < MAX_LOG_REL_ERRORS)
            LogRel(("PIIX3 ATA: LUN#%d: disk trim error (rc=%Rrc iSector=%#RX64 cSectors=%#RX32)\n",
                    s->iLUN, rc, *pu64Range & ATA_RANGE_LBA_MASK, ATA_RANGE_LENGTH_GET(*pu64Range)));

        /*
         * Check if we got interrupted. We don't need to set status variables
         * because the request was aborted.
         */
        if (rc != VERR_INTERRUPTED)
            ataR3CmdError(pCtl, s, ID_ERR);
    }

    return false;
}


static void ataR3ParseCmd(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s, PATADEVSTATER3 pDevR3, uint8_t cmd)
{
# ifdef DEBUG
    Log(("%s: LUN#%d CMD=%#04x \"%s\"\n", __FUNCTION__, s->iLUN, cmd, ATACmdText(cmd)));
# else /* !DEBUG */
    Log(("%s: LUN#%d CMD=%#04x\n", __FUNCTION__, s->iLUN, cmd));
# endif /* !DEBUG */
    s->fLBA48 = false;
    s->fDMA = false;
    if (cmd == ATA_IDLE_IMMEDIATE)
    {
        /* Detect Linux timeout recovery, first tries IDLE IMMEDIATE (which
         * would overwrite the failing command unfortunately), then RESET. */
        int32_t uCmdWait = -1;
        uint64_t uNow = RTTimeNanoTS();
        if (s->u64CmdTS)
            uCmdWait = (uNow - s->u64CmdTS) / 1000;
        LogRel(("PIIX3 ATA: LUN#%d: IDLE IMMEDIATE, CmdIf=%#04x (%d usec ago)\n",
                s->iLUN, s->uATARegCommand, uCmdWait));
    }
    s->uATARegCommand = cmd;
    switch (cmd)
    {
        case ATA_IDENTIFY_DEVICE:
            if (pDevR3->pDrvMedia && !s->fATAPI)
                ataR3StartTransfer(pDevIns, pCtl, s, 512, PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_NULL, ATAFN_SS_IDENTIFY, false);
            else
            {
                if (s->fATAPI)
                    ataR3SetSignature(s);
                ataR3CmdError(pCtl, s, ABRT_ERR);
                ataUnsetStatus(pCtl, s, ATA_STAT_READY);
                ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
            }
            break;
        case ATA_RECALIBRATE:
            if (s->fATAPI)
                goto abort_cmd;
            ataR3StartTransfer(pDevIns, pCtl, s, 0, PDMMEDIATXDIR_NONE, ATAFN_BT_NULL, ATAFN_SS_RECALIBRATE, false);
            break;
        case ATA_INITIALIZE_DEVICE_PARAMETERS:
            if (s->fATAPI)
                goto abort_cmd;
            ataR3StartTransfer(pDevIns, pCtl, s, 0, PDMMEDIATXDIR_NONE, ATAFN_BT_NULL, ATAFN_SS_INITIALIZE_DEVICE_PARAMETERS, false);
            break;
        case ATA_SET_MULTIPLE_MODE:
            if (    s->uATARegNSector != 0
                &&  (   s->uATARegNSector > ATA_MAX_MULT_SECTORS
                     || (s->uATARegNSector & (s->uATARegNSector - 1)) != 0))
            {
                ataR3CmdError(pCtl, s, ABRT_ERR);
            }
            else
            {
                Log2(("%s: set multi sector count to %d\n", __FUNCTION__, s->uATARegNSector));
                s->cMultSectors = s->uATARegNSector;
                ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
            }
            ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
            break;
        case ATA_READ_VERIFY_SECTORS_EXT:
            s->fLBA48 = true;
            RT_FALL_THRU();
        case ATA_READ_VERIFY_SECTORS:
        case ATA_READ_VERIFY_SECTORS_WITHOUT_RETRIES:
            /* do sector number check ? */
            ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
            ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
            break;
        case ATA_READ_SECTORS_EXT:
            s->fLBA48 = true;
            RT_FALL_THRU();
        case ATA_READ_SECTORS:
        case ATA_READ_SECTORS_WITHOUT_RETRIES:
            if (!pDevR3->pDrvMedia || s->fATAPI)
                goto abort_cmd;
            s->cSectorsPerIRQ = 1;
            s->iCurLBA = ataR3GetSector(s);
            ataR3StartTransfer(pDevIns, pCtl, s, ataR3GetNSectors(s) * s->cbSector, PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_READ_WRITE_SECTORS, ATAFN_SS_READ_SECTORS, false);
            break;
        case ATA_WRITE_SECTORS_EXT:
            s->fLBA48 = true;
            RT_FALL_THRU();
        case ATA_WRITE_SECTORS:
        case ATA_WRITE_SECTORS_WITHOUT_RETRIES:
            if (!pDevR3->pDrvMedia || s->fATAPI)
                goto abort_cmd;
            s->cSectorsPerIRQ = 1;
            s->iCurLBA = ataR3GetSector(s);
            ataR3StartTransfer(pDevIns, pCtl, s, ataR3GetNSectors(s) * s->cbSector, PDMMEDIATXDIR_TO_DEVICE, ATAFN_BT_READ_WRITE_SECTORS, ATAFN_SS_WRITE_SECTORS, false);
            break;
        case ATA_READ_MULTIPLE_EXT:
            s->fLBA48 = true;
            RT_FALL_THRU();
        case ATA_READ_MULTIPLE:
            if (!pDevR3->pDrvMedia || !s->cMultSectors || s->fATAPI)
                goto abort_cmd;
            s->cSectorsPerIRQ = s->cMultSectors;
            s->iCurLBA = ataR3GetSector(s);
            ataR3StartTransfer(pDevIns, pCtl, s, ataR3GetNSectors(s) * s->cbSector, PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_READ_WRITE_SECTORS, ATAFN_SS_READ_SECTORS, false);
            break;
        case ATA_WRITE_MULTIPLE_EXT:
            s->fLBA48 = true;
            RT_FALL_THRU();
        case ATA_WRITE_MULTIPLE:
            if (!pDevR3->pDrvMedia || !s->cMultSectors || s->fATAPI)
                goto abort_cmd;
            s->cSectorsPerIRQ = s->cMultSectors;
            s->iCurLBA = ataR3GetSector(s);
            ataR3StartTransfer(pDevIns, pCtl, s, ataR3GetNSectors(s) * s->cbSector, PDMMEDIATXDIR_TO_DEVICE, ATAFN_BT_READ_WRITE_SECTORS, ATAFN_SS_WRITE_SECTORS, false);
            break;
        case ATA_READ_DMA_EXT:
            s->fLBA48 = true;
            RT_FALL_THRU();
        case ATA_READ_DMA:
        case ATA_READ_DMA_WITHOUT_RETRIES:
            if (!pDevR3->pDrvMedia || s->fATAPI)
                goto abort_cmd;
            s->cSectorsPerIRQ = ATA_MAX_MULT_SECTORS;
            s->iCurLBA = ataR3GetSector(s);
            s->fDMA = true;
            ataR3StartTransfer(pDevIns, pCtl, s, ataR3GetNSectors(s) * s->cbSector, PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_READ_WRITE_SECTORS, ATAFN_SS_READ_SECTORS, false);
            break;
        case ATA_WRITE_DMA_EXT:
            s->fLBA48 = true;
            RT_FALL_THRU();
        case ATA_WRITE_DMA:
        case ATA_WRITE_DMA_WITHOUT_RETRIES:
            if (!pDevR3->pDrvMedia || s->fATAPI)
                goto abort_cmd;
            s->cSectorsPerIRQ = ATA_MAX_MULT_SECTORS;
            s->iCurLBA = ataR3GetSector(s);
            s->fDMA = true;
            ataR3StartTransfer(pDevIns, pCtl, s, ataR3GetNSectors(s) * s->cbSector, PDMMEDIATXDIR_TO_DEVICE, ATAFN_BT_READ_WRITE_SECTORS, ATAFN_SS_WRITE_SECTORS, false);
            break;
        case ATA_READ_NATIVE_MAX_ADDRESS_EXT:
            if (!pDevR3->pDrvMedia || s->fATAPI)
                goto abort_cmd;
            s->fLBA48 = true;
            ataR3SetSector(s, s->cTotalSectors - 1);
            ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
            ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
            break;
        case ATA_SEEK: /* Used by the SCO OpenServer. Command is marked as obsolete */
            ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
            ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
            break;
        case ATA_READ_NATIVE_MAX_ADDRESS:
            if (!pDevR3->pDrvMedia || s->fATAPI)
                goto abort_cmd;
            ataR3SetSector(s, RT_MIN(s->cTotalSectors, 1 << 28) - 1);
            ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
            ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
            break;
        case ATA_CHECK_POWER_MODE:
            s->uATARegNSector = 0xff; /* drive active or idle */
            ataR3CmdOK(pCtl, s, 0);
            ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
            break;
        case ATA_SET_FEATURES:
            Log2(("%s: feature=%#x\n", __FUNCTION__, s->uATARegFeature));
            if (!pDevR3->pDrvMedia)
                goto abort_cmd;
            switch (s->uATARegFeature)
            {
                case 0x02: /* write cache enable */
                    Log2(("%s: write cache enable\n", __FUNCTION__));
                    ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
                    ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
                    break;
                case 0xaa: /* read look-ahead enable */
                    Log2(("%s: read look-ahead enable\n", __FUNCTION__));
                    ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
                    ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
                    break;
                case 0x55: /* read look-ahead disable */
                    Log2(("%s: read look-ahead disable\n", __FUNCTION__));
                    ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
                    ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
                    break;
                case 0xcc: /* reverting to power-on defaults enable */
                    Log2(("%s: revert to power-on defaults enable\n", __FUNCTION__));
                    ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
                    ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
                    break;
                case 0x66: /* reverting to power-on defaults disable */
                    Log2(("%s: revert to power-on defaults disable\n", __FUNCTION__));
                    ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
                    ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
                    break;
                case 0x82: /* write cache disable */
                    Log2(("%s: write cache disable\n", __FUNCTION__));
                    /* As per the ATA/ATAPI-6 specs, a write cache disable
                     * command MUST flush the write buffers to disc. */
                    ataR3StartTransfer(pDevIns, pCtl, s, 0, PDMMEDIATXDIR_NONE, ATAFN_BT_NULL, ATAFN_SS_FLUSH, false);
                    break;
                case 0x03: { /* set transfer mode */
                    Log2(("%s: transfer mode %#04x\n", __FUNCTION__, s->uATARegNSector));
                    switch (s->uATARegNSector & 0xf8)
                    {
                        case 0x00: /* PIO default */
                        case 0x08: /* PIO mode */
                            break;
                        case ATA_MODE_MDMA: /* MDMA mode */
                            s->uATATransferMode = (s->uATARegNSector & 0xf8) | RT_MIN(s->uATARegNSector & 0x07, ATA_MDMA_MODE_MAX);
                            break;
                        case ATA_MODE_UDMA: /* UDMA mode */
                            s->uATATransferMode = (s->uATARegNSector & 0xf8) | RT_MIN(s->uATARegNSector & 0x07, ATA_UDMA_MODE_MAX);
                            break;
                        default:
                            goto abort_cmd;
                    }
                    ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
                    ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
                    break;
                }
                default:
                    goto abort_cmd;
            }
            /*
             * OS/2 workarond:
             * The OS/2 IDE driver from MCP2 appears to rely on the feature register being
             * reset here. According to the specification, this is a driver bug as the register
             * contents are undefined after the call. This means we can just as well reset it.
             */
            s->uATARegFeature = 0;
            break;
        case ATA_FLUSH_CACHE_EXT:
        case ATA_FLUSH_CACHE:
            if (!pDevR3->pDrvMedia || s->fATAPI)
                goto abort_cmd;
            ataR3StartTransfer(pDevIns, pCtl, s, 0, PDMMEDIATXDIR_NONE, ATAFN_BT_NULL, ATAFN_SS_FLUSH, false);
            break;
        case ATA_STANDBY_IMMEDIATE:
            ataR3CmdOK(pCtl, s, 0);
            ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
            break;
        case ATA_IDLE_IMMEDIATE:
            LogRel(("PIIX3 ATA: LUN#%d: aborting current command\n", s->iLUN));
            ataR3AbortCurrentCommand(pDevIns, pCtl, s, false);
            break;
        case ATA_SLEEP:
            ataR3CmdOK(pCtl, s, 0);
            ataHCSetIRQ(pDevIns, pCtl, s);
            break;
            /* ATAPI commands */
        case ATA_IDENTIFY_PACKET_DEVICE:
            if (s->fATAPI)
                ataR3StartTransfer(pDevIns, pCtl, s, 512, PDMMEDIATXDIR_FROM_DEVICE, ATAFN_BT_NULL, ATAFN_SS_ATAPI_IDENTIFY, false);
            else
            {
                ataR3CmdError(pCtl, s, ABRT_ERR);
                ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
            }
            break;
        case ATA_EXECUTE_DEVICE_DIAGNOSTIC:
            ataR3StartTransfer(pDevIns, pCtl, s, 0, PDMMEDIATXDIR_NONE, ATAFN_BT_NULL, ATAFN_SS_EXECUTE_DEVICE_DIAGNOSTIC, false);
            break;
        case ATA_DEVICE_RESET:
            if (!s->fATAPI)
                goto abort_cmd;
            LogRel(("PIIX3 ATA: LUN#%d: performing device RESET\n", s->iLUN));
            ataR3AbortCurrentCommand(pDevIns, pCtl, s, true);
            break;
        case ATA_PACKET:
            if (!s->fATAPI)
                goto abort_cmd;
            /* overlapping commands not supported */
            if (s->uATARegFeature & 0x02)
                goto abort_cmd;
            ataR3StartTransfer(pDevIns, pCtl, s, ATAPI_PACKET_SIZE, PDMMEDIATXDIR_TO_DEVICE, ATAFN_BT_PACKET, ATAFN_SS_PACKET, false);
            break;
        case ATA_DATA_SET_MANAGEMENT:
            if (!pDevR3->pDrvMedia || !pDevR3->pDrvMedia->pfnDiscard)
                goto abort_cmd;
            if (   !(s->uATARegFeature & UINT8_C(0x01))
                || (s->uATARegFeature & ~UINT8_C(0x01)))
                goto abort_cmd;
            s->fDMA = true;
            ataR3StartTransfer(pDevIns, pCtl, s, (s->uATARegNSectorHOB << 8 | s->uATARegNSector) * s->cbSector, PDMMEDIATXDIR_TO_DEVICE, ATAFN_BT_NULL, ATAFN_SS_TRIM, false);
            break;
        default:
        abort_cmd:
            ataR3CmdError(pCtl, s, ABRT_ERR);
            if (s->fATAPI)
                ataUnsetStatus(pCtl, s, ATA_STAT_READY);
            ataHCSetIRQ(pDevIns, pCtl, s); /* Shortcut, do not use AIO thread. */
            break;
    }
}

# endif /* IN_RING3 */
#endif /* IN_RING0 || IN_RING3 */

/*
 * Note: There are four distinct cases of port I/O handling depending on
 * which devices (if any) are attached to an IDE channel:
 *
 *  1) No device attached. No response to writes or reads (i.e. reads return
 *     all bits set).
 *
 *  2) Both devices attached. Reads and writes are processed normally.
 *
 *  3) Device 0 only. If device 0 is selected, normal behavior applies. But
 *     if Device 1 is selected, writes are still directed to Device 0 (except
 *     commands are not executed), reads from control/command registers are
 *     directed to Device 0, but status/alt status reads return 0. If Device 1
 *     is a PACKET device, all reads return 0. See ATAPI-6 clause 9.16.1 and
 *     Table 18 in clause 7.1.
 *
 *  4) Device 1 only - non-standard(!). Device 1 can't tell if Device 0 is
 *     present or not and behaves the same. That means if Device 0 is selected,
 *     Device 1 responds to writes (except commands are not executed) but does
 *     not respond to reads. If Device 1 selected, normal behavior applies.
 *     See ATAPI-6 clause 9.16.2 and Table 15 in clause 7.1.
 */

static VBOXSTRICTRC ataIOPortWriteU8(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, uint32_t addr, uint32_t val, uintptr_t iCtl)
{
    RT_NOREF(iCtl);
    Log2(("%s: LUN#%d write addr=%#x val=%#04x\n", __FUNCTION__, pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK].iLUN, addr, val));
    addr &= 7;
    switch (addr)
    {
        case 0:
            break;
        case 1: /* feature register */
            /* NOTE: data is written to the two drives */
            pCtl->aIfs[0].uATARegDevCtl &= ~ATA_DEVCTL_HOB;
            pCtl->aIfs[1].uATARegDevCtl &= ~ATA_DEVCTL_HOB;
            pCtl->aIfs[0].uATARegFeatureHOB = pCtl->aIfs[0].uATARegFeature;
            pCtl->aIfs[1].uATARegFeatureHOB = pCtl->aIfs[1].uATARegFeature;
            pCtl->aIfs[0].uATARegFeature = val;
            pCtl->aIfs[1].uATARegFeature = val;
            break;
        case 2: /* sector count */
            pCtl->aIfs[0].uATARegDevCtl &= ~ATA_DEVCTL_HOB;
            pCtl->aIfs[1].uATARegDevCtl &= ~ATA_DEVCTL_HOB;
            pCtl->aIfs[0].uATARegNSectorHOB = pCtl->aIfs[0].uATARegNSector;
            pCtl->aIfs[1].uATARegNSectorHOB = pCtl->aIfs[1].uATARegNSector;
            pCtl->aIfs[0].uATARegNSector = val;
            pCtl->aIfs[1].uATARegNSector = val;
            break;
        case 3: /* sector number */
            pCtl->aIfs[0].uATARegDevCtl &= ~ATA_DEVCTL_HOB;
            pCtl->aIfs[1].uATARegDevCtl &= ~ATA_DEVCTL_HOB;
            pCtl->aIfs[0].uATARegSectorHOB = pCtl->aIfs[0].uATARegSector;
            pCtl->aIfs[1].uATARegSectorHOB = pCtl->aIfs[1].uATARegSector;
            pCtl->aIfs[0].uATARegSector = val;
            pCtl->aIfs[1].uATARegSector = val;
            break;
        case 4: /* cylinder low */
            pCtl->aIfs[0].uATARegDevCtl &= ~ATA_DEVCTL_HOB;
            pCtl->aIfs[1].uATARegDevCtl &= ~ATA_DEVCTL_HOB;
            pCtl->aIfs[0].uATARegLCylHOB = pCtl->aIfs[0].uATARegLCyl;
            pCtl->aIfs[1].uATARegLCylHOB = pCtl->aIfs[1].uATARegLCyl;
            pCtl->aIfs[0].uATARegLCyl = val;
            pCtl->aIfs[1].uATARegLCyl = val;
            break;
        case 5: /* cylinder high */
            pCtl->aIfs[0].uATARegDevCtl &= ~ATA_DEVCTL_HOB;
            pCtl->aIfs[1].uATARegDevCtl &= ~ATA_DEVCTL_HOB;
            pCtl->aIfs[0].uATARegHCylHOB = pCtl->aIfs[0].uATARegHCyl;
            pCtl->aIfs[1].uATARegHCylHOB = pCtl->aIfs[1].uATARegHCyl;
            pCtl->aIfs[0].uATARegHCyl = val;
            pCtl->aIfs[1].uATARegHCyl = val;
            break;
        case 6: /* drive/head */
            pCtl->aIfs[0].uATARegSelect = val & ~0x10;
            pCtl->aIfs[1].uATARegSelect = val | 0x10;
            if (((val >> 4) & ATA_SELECTED_IF_MASK) != pCtl->iSelectedIf)
            {
                /* select another drive */
                uintptr_t const iSelectedIf = (val >> 4) & ATA_SELECTED_IF_MASK;
                pCtl->iSelectedIf = (uint8_t)iSelectedIf;
                /* The IRQ line is multiplexed between the two drives, so
                 * update the state when switching to another drive. Only need
                 * to update interrupt line if it is enabled and there is a
                 * state change. */
                if (    !(pCtl->aIfs[iSelectedIf].uATARegDevCtl & ATA_DEVCTL_DISABLE_IRQ)
                    &&  pCtl->aIfs[iSelectedIf].fIrqPending != pCtl->aIfs[iSelectedIf ^ 1].fIrqPending)
                {
                    if (pCtl->aIfs[iSelectedIf].fIrqPending)
                    {
                        Log2(("%s: LUN#%d asserting IRQ (drive select change)\n", __FUNCTION__, pCtl->aIfs[iSelectedIf].iLUN));
                        /* The BMDMA unit unconditionally sets BM_STATUS_INT if
                         * the interrupt line is asserted. It monitors the line
                         * for a rising edge. */
                        pCtl->BmDma.u8Status |= BM_STATUS_INT;
                        if (pCtl->irq == 16)
                            PDMDevHlpPCISetIrq(pDevIns, 0, 1);
                        else
                            PDMDevHlpISASetIrq(pDevIns, pCtl->irq, 1);
                    }
                    else
                    {
                        Log2(("%s: LUN#%d deasserting IRQ (drive select change)\n", __FUNCTION__, pCtl->aIfs[iSelectedIf].iLUN));
                        if (pCtl->irq == 16)
                            PDMDevHlpPCISetIrq(pDevIns, 0, 0);
                        else
                            PDMDevHlpISASetIrq(pDevIns, pCtl->irq, 0);
                    }
                }
            }
            break;
        default:
        case 7: /* command */
        {
            /* ignore commands to non-existent device */
            uintptr_t iSelectedIf = pCtl->iSelectedIf & ATA_SELECTED_IF_MASK;
            PATADEVSTATE pDev = &pCtl->aIfs[iSelectedIf];
            if (iSelectedIf && !pDev->fPresent) /** @todo r=bird the iSelectedIf test here looks bogus... explain. */
                break;
#ifndef IN_RING3
            /* Don't do anything complicated in GC */
            return VINF_IOM_R3_IOPORT_WRITE;
#else /* IN_RING3 */
            PATASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PATASTATER3);
            ataUnsetIRQ(pDevIns, pCtl, &pCtl->aIfs[iSelectedIf]);
            ataR3ParseCmd(pDevIns, pCtl, &pCtl->aIfs[iSelectedIf], &pThisCC->aCts[iCtl].aIfs[iSelectedIf], val);
            break;
#endif /* !IN_RING3 */
        }
    }
    return VINF_SUCCESS;
}


static VBOXSTRICTRC ataIOPortReadU8(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, uint32_t addr, uint32_t *pu32)
{
    PATADEVSTATE s = &pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK];
    uint32_t    val;
    bool        fHOB;

    /* Check if the guest is reading from a non-existent device. */
    if (RT_LIKELY(s->fPresent))
    { /* likely */ }
    else
    {
        if (pCtl->iSelectedIf)  /* Device 1 selected, Device 0 responding for it. */
        {
            Assert(pCtl->aIfs[0].fPresent);

            /* When an ATAPI device 0 responds for non-present device 1, it generally
             * returns zeros on reads. The Error register is an exception. See clause 7.1,
             * table 16 in ATA-6 specification.
             */
            if (((addr & 7) != 1) && pCtl->aIfs[0].fATAPI)
            {
                Log2(("%s: addr=%#x, val=0: LUN#%d not attached/LUN#%d ATAPI\n", __FUNCTION__, addr, s->iLUN, pCtl->aIfs[0].iLUN));
                *pu32 = 0;
                return VINF_SUCCESS;
            }
            /* Else handle normally. */
        }
        else                    /* Device 0 selected (but not present). */
        {
            /* Because device 1 has no way to tell if there is device 0, the behavior is the same
             * as for an empty bus; see comments in ataIOPortReadEmptyBus(). Note that EFI (TianoCore)
             * relies on this behavior when detecting devices.
             */
            *pu32 = ATA_EMPTY_BUS_DATA;
            Log2(("%s: addr=%#x: LUN#%d not attached, val=%#02x\n", __FUNCTION__, addr, s->iLUN, *pu32));
            return VINF_SUCCESS;
        }
    }

    fHOB = !!(s->uATARegDevCtl & (1 << 7));
    switch (addr & 7)
    {
        case 0: /* data register */
            val = 0xff;
            break;
        case 1: /* error register */
            /* The ATA specification is very terse when it comes to specifying
             * the precise effects of reading back the error/feature register.
             * The error register (read-only) shares the register number with
             * the feature register (write-only), so it seems that it's not
             * necessary to support the usual HOB readback here. */
            if (!s->fPresent)
                val = 0;
            else
                val = s->uATARegError;
            break;
        case 2: /* sector count */
            if (fHOB)
                val = s->uATARegNSectorHOB;
            else
                val = s->uATARegNSector;
            break;
        case 3: /* sector number */
            if (fHOB)
                val = s->uATARegSectorHOB;
            else
                val = s->uATARegSector;
            break;
        case 4: /* cylinder low */
            if (fHOB)
                val = s->uATARegLCylHOB;
            else
                val = s->uATARegLCyl;
            break;
        case 5: /* cylinder high */
            if (fHOB)
                val = s->uATARegHCylHOB;
            else
                val = s->uATARegHCyl;
            break;
        case 6: /* drive/head */
            /* This register must always work as long as there is at least
             * one drive attached to the controller. It is common between
             * both drives anyway (completely identical content). */
            if (!pCtl->aIfs[0].fPresent && !pCtl->aIfs[1].fPresent)
                val = 0;
            else
                val = s->uATARegSelect;
            break;
        default:
        case 7: /* primary status */
        {
            if (!s->fPresent)
                val = 0;
            else
                val = s->uATARegStatus;

            /* Give the async I/O thread an opportunity to make progress,
             * don't let it starve by guests polling frequently. EMT has a
             * lower priority than the async I/O thread, but sometimes the
             * host OS doesn't care. With some guests we are only allowed to
             * be busy for about 5 milliseconds in some situations. Note that
             * this is no guarantee for any other VBox thread getting
             * scheduled, so this just lowers the CPU load a bit when drives
             * are busy. It cannot help with timing problems. */
            if (val & ATA_STAT_BUSY)
            {
#ifdef IN_RING3
                /* @bugref{1960}: Don't yield all the time, unless it's a reset (can be tricky). */
                bool fYield = (s->cBusyStatusHackR3++ & s->cBusyStatusHackR3Rate) == 0
                            || pCtl->fReset;

                ataR3LockLeave(pDevIns, pCtl);

                /*
                 * The thread might be stuck in an I/O operation due to a high I/O
                 * load on the host (see @bugref{3301}).  To perform the reset
                 * successfully we interrupt the operation by sending a signal to
                 * the thread if the thread didn't responded in 10ms.
                 *
                 * This works only on POSIX hosts (Windows has a CancelSynchronousIo
                 * function which does the same but it was introduced with Vista) but
                 * so far this hang was only observed on Linux and Mac OS X.
                 *
                 * This is a workaround and needs to be solved properly.
                 */
                if (pCtl->fReset)
                {
                    uint64_t u64ResetTimeStop = RTTimeMilliTS();
                    if (u64ResetTimeStop - pCtl->u64ResetTime >= 10)
                    {
                        LogRel(("PIIX3 ATA LUN#%d: Async I/O thread probably stuck in operation, interrupting\n", s->iLUN));
                        pCtl->u64ResetTime = u64ResetTimeStop;
# ifndef RT_OS_WINDOWS /* We've got this API on windows, but it doesn't necessarily interrupt I/O. */
                        PATASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PATASTATER3);
                        PATACONTROLLERR3 pCtlR3 = &RT_SAFE_SUBSCRIPT(pThisCC->aCts, pCtl->iCtl);
                        RTThreadPoke(pCtlR3->hAsyncIOThread);
# endif
                        Assert(fYield);
                    }
                }

                if (fYield)
                {
                    STAM_REL_PROFILE_ADV_START(&s->StatStatusYields, a);
                    RTThreadYield();
                    STAM_REL_PROFILE_ADV_STOP(&s->StatStatusYields, a);
                }
                ASMNopPause();

                ataR3LockEnter(pDevIns, pCtl);

                val = s->uATARegStatus;
#else /* !IN_RING3 */
                /* Cannot yield CPU in raw-mode and ring-0 context.  And switching
                 * to host context for each and every busy status is too costly,
                 * especially on SMP systems where we don't gain much by
                 * yielding the CPU to someone else. */
                if ((s->cBusyStatusHackRZ++ & s->cBusyStatusHackRZRate) == 1)
                {
                    s->cBusyStatusHackR3 = 0; /* Forces a yield. */
                    return VINF_IOM_R3_IOPORT_READ;
                }
#endif /* !IN_RING3 */
            }
            else
            {
                s->cBusyStatusHackRZ = 0;
                s->cBusyStatusHackR3 = 0;
            }
            ataUnsetIRQ(pDevIns, pCtl, s);
            break;
        }
    }
    Log2(("%s: LUN#%d addr=%#x val=%#04x\n", __FUNCTION__, s->iLUN, addr, val));
    *pu32 = val;
    return VINF_SUCCESS;
}


/*
 * Read the Alternate status register. Does not affect interrupts.
 */
static uint32_t ataStatusRead(PATACONTROLLER pCtl, uint32_t uIoPortForLog)
{
    PATADEVSTATE s = &pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK];
    uint32_t val;
    RT_NOREF(uIoPortForLog);

    Assert(pCtl->aIfs[0].fPresent || pCtl->aIfs[1].fPresent); /* Channel must not be empty. */
    if (pCtl->iSelectedIf == 1 && !s->fPresent)
        val = 0;    /* Device 1 selected, Device 0 responding for it. */
    else
        val = s->uATARegStatus;
    Log2(("%s: LUN#%d read addr=%#x val=%#04x\n", __FUNCTION__, pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK].iLUN, uIoPortForLog, val));
    return val;
}

static int ataControlWrite(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, uint32_t val, uint32_t uIoPortForLog)
{
    RT_NOREF(uIoPortForLog);
#ifndef IN_RING3
    if ((val ^ pCtl->aIfs[0].uATARegDevCtl) & ATA_DEVCTL_RESET)
        return VINF_IOM_R3_IOPORT_WRITE; /* The RESET stuff is too complicated for RC+R0. */
#endif /* !IN_RING3 */

    Log2(("%s: LUN#%d write addr=%#x val=%#04x\n", __FUNCTION__, pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK].iLUN, uIoPortForLog, val));
    /* RESET is common for both drives attached to a controller. */
    if (   !(pCtl->aIfs[0].uATARegDevCtl & ATA_DEVCTL_RESET)
        && (val & ATA_DEVCTL_RESET))
    {
#ifdef IN_RING3
        /* Software RESET low to high */
        int32_t uCmdWait0 = -1;
        int32_t uCmdWait1 = -1;
        uint64_t uNow = RTTimeNanoTS();
        if (pCtl->aIfs[0].u64CmdTS)
            uCmdWait0 = (uNow - pCtl->aIfs[0].u64CmdTS) / 1000;
        if (pCtl->aIfs[1].u64CmdTS)
            uCmdWait1 = (uNow - pCtl->aIfs[1].u64CmdTS) / 1000;
        LogRel(("PIIX3 ATA: Ctl#%d: RESET, DevSel=%d AIOIf=%d CmdIf0=%#04x (%d usec ago) CmdIf1=%#04x (%d usec ago)\n",
                pCtl->iCtl, pCtl->iSelectedIf, pCtl->iAIOIf,
                pCtl->aIfs[0].uATARegCommand, uCmdWait0,
                pCtl->aIfs[1].uATARegCommand, uCmdWait1));
        pCtl->fReset = true;
        /* Everything must be done after the reset flag is set, otherwise
         * there are unavoidable races with the currently executing request
         * (which might just finish in the mean time). */
        pCtl->fChainedTransfer = false;
        for (uint32_t i = 0; i < RT_ELEMENTS(pCtl->aIfs); i++)
        {
            ataR3ResetDevice(pDevIns, pCtl, &pCtl->aIfs[i]);
            /* The following cannot be done using ataSetStatusValue() since the
             * reset flag is already set, which suppresses all status changes. */
            pCtl->aIfs[i].uATARegStatus = ATA_STAT_BUSY | ATA_STAT_SEEK;
            Log2(("%s: LUN#%d status %#04x\n", __FUNCTION__, pCtl->aIfs[i].iLUN, pCtl->aIfs[i].uATARegStatus));
            pCtl->aIfs[i].uATARegError = 0x01;
        }
        pCtl->iSelectedIf = 0;
        ataR3AsyncIOClearRequests(pDevIns, pCtl);
        Log2(("%s: Ctl#%d: message to async I/O thread, resetA\n", __FUNCTION__, pCtl->iCtl));
        if (val & ATA_DEVCTL_HOB)
        {
            val &= ~ATA_DEVCTL_HOB;
            Log2(("%s: ignored setting HOB\n", __FUNCTION__));
        }

        /* Save the timestamp we started the reset. */
        pCtl->u64ResetTime = RTTimeMilliTS();

        /* Issue the reset request now. */
        ataHCAsyncIOPutRequest(pDevIns, pCtl, &g_ataResetARequest);
#else /* !IN_RING3 */
        AssertMsgFailed(("RESET handling is too complicated for GC\n"));
#endif /* IN_RING3 */
    }
    else if (   (pCtl->aIfs[0].uATARegDevCtl & ATA_DEVCTL_RESET)
             && !(val & ATA_DEVCTL_RESET))
    {
#ifdef IN_RING3
        /* Software RESET high to low */
        Log(("%s: deasserting RESET\n", __FUNCTION__));
        Log2(("%s: Ctl#%d: message to async I/O thread, resetC\n", __FUNCTION__, pCtl->iCtl));
        if (val & ATA_DEVCTL_HOB)
        {
            val &= ~ATA_DEVCTL_HOB;
            Log2(("%s: ignored setting HOB\n", __FUNCTION__));
        }
        ataHCAsyncIOPutRequest(pDevIns, pCtl, &g_ataResetCRequest);
#else /* !IN_RING3 */
        AssertMsgFailed(("RESET handling is too complicated for GC\n"));
#endif /* IN_RING3 */
    }

    /* Change of interrupt disable flag. Update interrupt line if interrupt
     * is pending on the current interface. */
    if (   ((val ^ pCtl->aIfs[0].uATARegDevCtl) & ATA_DEVCTL_DISABLE_IRQ)
        && pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK].fIrqPending)
    {
        if (!(val & ATA_DEVCTL_DISABLE_IRQ))
        {
            Log2(("%s: LUN#%d asserting IRQ (interrupt disable change)\n", __FUNCTION__, pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK].iLUN));
            /* The BMDMA unit unconditionally sets BM_STATUS_INT if the
             * interrupt line is asserted. It monitors the line for a rising
             * edge. */
            pCtl->BmDma.u8Status |= BM_STATUS_INT;
            if (pCtl->irq == 16)
                PDMDevHlpPCISetIrq(pDevIns, 0, 1);
            else
                PDMDevHlpISASetIrq(pDevIns, pCtl->irq, 1);
        }
        else
        {
            Log2(("%s: LUN#%d deasserting IRQ (interrupt disable change)\n", __FUNCTION__, pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK].iLUN));
            if (pCtl->irq == 16)
                PDMDevHlpPCISetIrq(pDevIns, 0, 0);
            else
                PDMDevHlpISASetIrq(pDevIns, pCtl->irq, 0);
        }
    }

    if (val & ATA_DEVCTL_HOB)
        Log2(("%s: set HOB\n", __FUNCTION__));

    pCtl->aIfs[0].uATARegDevCtl = val;
    pCtl->aIfs[1].uATARegDevCtl = val;

    return VINF_SUCCESS;
}

#if defined(IN_RING0) || defined(IN_RING3)

static void ataHCPIOTransfer(PPDMDEVINS pDevIns, PATACONTROLLER pCtl)
{
    PATADEVSTATE s;

    s = &pCtl->aIfs[pCtl->iAIOIf & ATA_SELECTED_IF_MASK];
    Log3(("%s: if=%p\n", __FUNCTION__, s));

    if (s->cbTotalTransfer && s->iIOBufferCur > s->iIOBufferEnd)
    {
# ifdef IN_RING3
        LogRel(("PIIX3 ATA: LUN#%d: %s data in the middle of a PIO transfer - VERY SLOW\n",
                s->iLUN, s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE ? "loading" : "storing"));
        /* Any guest OS that triggers this case has a pathetic ATA driver.
         * In a real system it would block the CPU via IORDY, here we do it
         * very similarly by not continuing with the current instruction
         * until the transfer to/from the storage medium is completed. */
        uint8_t const iSourceSink = s->iSourceSink;
        if (   iSourceSink != ATAFN_SS_NULL
            && iSourceSink < RT_ELEMENTS(g_apfnSourceSinkFuncs))
        {
            bool fRedo;
            uint8_t        status  = s->uATARegStatus;
            PATASTATER3    pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PATASTATER3);
            PATADEVSTATER3 pDevR3  = &RT_SAFE_SUBSCRIPT(RT_SAFE_SUBSCRIPT(pThisCC->aCts, pCtl->iCtl).aIfs, s->iDev);

            ataSetStatusValue(pCtl, s, ATA_STAT_BUSY);
            Log2(("%s: calling source/sink function\n", __FUNCTION__));
            fRedo = g_apfnSourceSinkFuncs[iSourceSink](pDevIns, pCtl, s, pDevR3);
            pCtl->fRedo = fRedo;
            if (RT_UNLIKELY(fRedo))
                return;
            ataSetStatusValue(pCtl, s, status);
            s->iIOBufferCur = 0;
            s->iIOBufferEnd = s->cbElementaryTransfer;
        }
        else
            Assert(iSourceSink == ATAFN_SS_NULL);
# else
        AssertReleaseFailed();
# endif
    }
    if (s->cbTotalTransfer)
    {
        if (s->fATAPITransfer)
            ataHCPIOTransferLimitATAPI(s);

        if (s->uTxDir == PDMMEDIATXDIR_TO_DEVICE && s->cbElementaryTransfer > s->cbTotalTransfer)
            s->cbElementaryTransfer = s->cbTotalTransfer;

        Log2(("%s: %s tx_size=%d elem_tx_size=%d index=%d end=%d\n",
              __FUNCTION__, s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE ? "T2I" : "I2T",
              s->cbTotalTransfer, s->cbElementaryTransfer,
              s->iIOBufferCur, s->iIOBufferEnd));
        ataHCPIOTransferStart(pCtl, s, s->iIOBufferCur, s->cbElementaryTransfer);
        s->cbTotalTransfer -= s->cbElementaryTransfer;
        s->iIOBufferCur += s->cbElementaryTransfer;

        if (s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE && s->cbElementaryTransfer > s->cbTotalTransfer)
            s->cbElementaryTransfer = s->cbTotalTransfer;
    }
    else
        ataHCPIOTransferStop(pDevIns, pCtl, s);
}


DECLINLINE(void) ataHCPIOTransferFinish(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATADEVSTATE s)
{
    /* Do not interfere with RESET processing if the PIO transfer finishes
     * while the RESET line is asserted. */
    if (pCtl->fReset)
    {
        Log2(("%s: Ctl#%d: suppressed continuing PIO transfer as RESET is active\n", __FUNCTION__, pCtl->iCtl));
        return;
    }

    if (   s->uTxDir == PDMMEDIATXDIR_TO_DEVICE
        || (   s->iSourceSink != ATAFN_SS_NULL
            && s->iIOBufferCur >= s->iIOBufferEnd))
    {
        /* Need to continue the transfer in the async I/O thread. This is
         * the case for write operations or generally for not yet finished
         * transfers (some data might need to be read). */
        ataSetStatus(pCtl, s, ATA_STAT_BUSY);
        ataUnsetStatus(pCtl, s, ATA_STAT_READY | ATA_STAT_DRQ);

        Log2(("%s: Ctl#%d: message to async I/O thread, continuing PIO transfer\n", __FUNCTION__, pCtl->iCtl));
        ataHCAsyncIOPutRequest(pDevIns, pCtl, &g_ataPIORequest);
    }
    else
    {
        /* Either everything finished (though some data might still be pending)
         * or some data is pending before the next read is due. */

        /* Continue a previously started transfer. */
        ataUnsetStatus(pCtl, s, ATA_STAT_DRQ);
        ataSetStatus(pCtl, s, ATA_STAT_READY);

        if (s->cbTotalTransfer)
        {
            /* There is more to transfer, happens usually for large ATAPI
             * reads - the protocol limits the chunk size to 65534 bytes. */
            ataHCPIOTransfer(pDevIns, pCtl);
            ataHCSetIRQ(pDevIns, pCtl, s);
        }
        else
        {
            Log2(("%s: Ctl#%d: skipping message to async I/O thread, ending PIO transfer\n", __FUNCTION__, pCtl->iCtl));
            /* Finish PIO transfer. */
            ataHCPIOTransfer(pDevIns, pCtl);
            Assert(!pCtl->fRedo);
        }
    }
}

#endif /* IN_RING0 || IN_RING3 */

/**
 * Fallback for ataCopyPioData124 that handles unaligned and out of bounds cases.
 *
 * @param   pIf         The device interface to work with.
 * @param   pbDst       The destination buffer.
 * @param   pbSrc       The source buffer.
 * @param   offStart    The start offset (iIOBufferPIODataStart).
 * @param   cbCopy      The number of bytes to copy, either 1, 2 or 4 bytes.
 */
DECL_NO_INLINE(static, void) ataCopyPioData124Slow(PATADEVSTATE pIf, uint8_t *pbDst, const uint8_t *pbSrc,
                                                   uint32_t offStart, uint32_t cbCopy)
{
    uint32_t const offNext    = offStart + cbCopy;
    uint32_t const cbIOBuffer = RT_MIN(pIf->cbIOBuffer, ATA_MAX_IO_BUFFER_SIZE);

    if (offStart + cbCopy > cbIOBuffer)
    {
        Log(("%s: cbCopy=%#x offStart=%#x cbIOBuffer=%#x offNext=%#x (iIOBufferPIODataEnd=%#x)\n",
             __FUNCTION__, cbCopy, offStart, cbIOBuffer, offNext, pIf->iIOBufferPIODataEnd));
        if (offStart < cbIOBuffer)
            cbCopy = cbIOBuffer - offStart;
        else
            cbCopy = 0;
    }

    switch (cbCopy)
    {
        case 4: pbDst[3] = pbSrc[3]; RT_FALL_THRU();
        case 3: pbDst[2] = pbSrc[2]; RT_FALL_THRU();
        case 2: pbDst[1] = pbSrc[1]; RT_FALL_THRU();
        case 1: pbDst[0] = pbSrc[0]; RT_FALL_THRU();
        case 0: break;
        default: AssertFailed(); /* impossible */
    }

    pIf->iIOBufferPIODataStart = offNext;

}


/**
 * Work for ataDataWrite & ataDataRead that copies data without using memcpy.
 *
 * This also updates pIf->iIOBufferPIODataStart.
 *
 * The two buffers are either stack (32-bit aligned) or somewhere within
 * pIf->abIOBuffer.
 *
 * @param   pIf         The device interface to work with.
 * @param   pbDst       The destination buffer.
 * @param   pbSrc       The source buffer.
 * @param   offStart    The start offset (iIOBufferPIODataStart).
 * @param   cbCopy      The number of bytes to copy, either 1, 2 or 4 bytes.
 */
DECLINLINE(void) ataCopyPioData124(PATADEVSTATE pIf, uint8_t *pbDst, const uint8_t *pbSrc, uint32_t offStart, uint32_t cbCopy)
{
    /*
     * Quick bounds checking can be done by checking that the abIOBuffer offset
     * (iIOBufferPIODataStart) is aligned at the transfer size (which is ASSUMED
     * to be 1, 2 or 4).  However, since we're paranoid and don't currently
     * trust iIOBufferPIODataEnd to be within bounds, we current check against the
     * IO buffer size too.
     */
    Assert(cbCopy == 1 || cbCopy == 2 || cbCopy == 4);
    if (RT_LIKELY(   !(offStart & (cbCopy - 1))
                  && offStart + cbCopy <= RT_MIN(pIf->cbIOBuffer, ATA_MAX_IO_BUFFER_SIZE)))
    {
        switch (cbCopy)
        {
            case 4: *(uint32_t *)pbDst = *(uint32_t const *)pbSrc; break;
            case 2: *(uint16_t *)pbDst = *(uint16_t const *)pbSrc; break;
            case 1: *pbDst = *pbSrc; break;
        }
        pIf->iIOBufferPIODataStart = offStart + cbCopy;
    }
    else
        ataCopyPioData124Slow(pIf, pbDst, pbSrc, offStart, cbCopy);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 * Port I/O Handler for primary port range OUT operations.}
 * @note    offPort is an absolute port number!
 */
static DECLCALLBACK(VBOXSTRICTRC)
ataIOPortWrite1Data(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PATASTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATACONTROLLER pCtl  = &RT_SAFE_SUBSCRIPT(pThis->aCts, (uintptr_t)pvUser);
    RT_NOREF(offPort);

    Assert((uintptr_t)pvUser < 2);
    Assert(offPort == pCtl->IOPortBase1);
    Assert(cb == 2 || cb == 4); /* Writes to the data port may be 16-bit or 32-bit. */

    VBOXSTRICTRC rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->lock, VINF_IOM_R3_IOPORT_WRITE);
    if (rc == VINF_SUCCESS)
    {
        PATADEVSTATE s = &pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK];
        uint32_t const iIOBufferPIODataStart = RT_MIN(s->iIOBufferPIODataStart, sizeof(s->abIOBuffer));
        uint32_t const iIOBufferPIODataEnd   = RT_MIN(s->iIOBufferPIODataEnd,   sizeof(s->abIOBuffer));

        if (iIOBufferPIODataStart < iIOBufferPIODataEnd)
        {
            Assert(s->uTxDir == PDMMEDIATXDIR_TO_DEVICE);
            uint8_t       *pbDst = &s->abIOBuffer[iIOBufferPIODataStart];
            uint8_t const *pbSrc = (uint8_t const *)&u32;

#ifdef IN_RC
            /* Raw-mode: The ataHCPIOTransfer following the last transfer unit
               requires I/O thread signalling, we must go to ring-3 for that. */
            if (iIOBufferPIODataStart + cb < iIOBufferPIODataEnd)
                ataCopyPioData124(s, pbDst, pbSrc, iIOBufferPIODataStart, cb);
            else
                rc = VINF_IOM_R3_IOPORT_WRITE;

#elif defined(IN_RING0)
            /* Ring-0: We can do I/O thread signalling here, however for paranoid reasons
               triggered by a special case in ataHCPIOTransferFinish, we take extra care here. */
            if (iIOBufferPIODataStart + cb < iIOBufferPIODataEnd)
                ataCopyPioData124(s, pbDst, pbSrc, iIOBufferPIODataStart, cb);
            else if (s->uTxDir == PDMMEDIATXDIR_TO_DEVICE) /* paranoia */
            {
                ataCopyPioData124(s, pbDst, pbSrc, iIOBufferPIODataStart, cb);
                ataHCPIOTransferFinish(pDevIns, pCtl, s);
            }
            else
            {
                Log(("%s: Unexpected\n", __FUNCTION__));
                rc = VINF_IOM_R3_IOPORT_WRITE;
            }

#else  /* IN_RING 3*/
            ataCopyPioData124(s, pbDst, pbSrc, iIOBufferPIODataStart, cb);
            if (s->iIOBufferPIODataStart >= iIOBufferPIODataEnd)
                ataHCPIOTransferFinish(pDevIns, pCtl, s);
#endif /* IN_RING 3*/
        }
        else
            Log2(("%s: DUMMY data\n", __FUNCTION__));

        Log3(("%s: addr=%#x val=%.*Rhxs rc=%d\n", __FUNCTION__, offPort, cb, &u32, VBOXSTRICTRC_VAL(rc)));
        PDMDevHlpCritSectLeave(pDevIns, &pCtl->lock);
    }
    else
        Log3(("%s: addr=%#x -> %d\n", __FUNCTION__, offPort, VBOXSTRICTRC_VAL(rc)));
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,
 * Port I/O Handler for primary port range IN operations.}
 * @note    offPort is an absolute port number!
 */
static DECLCALLBACK(VBOXSTRICTRC)
ataIOPortRead1Data(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PATASTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATACONTROLLER pCtl  = &RT_SAFE_SUBSCRIPT(pThis->aCts, (uintptr_t)pvUser);
    RT_NOREF(offPort);

    Assert((uintptr_t)pvUser < 2);
    Assert(offPort == pCtl->IOPortBase1);

    /* Reads from the data register may be 16-bit or 32-bit. Byte accesses are
       upgraded to word. */
    Assert(cb == 1 || cb == 2 || cb == 4);
    uint32_t cbActual = cb != 1 ? cb : 2;
    *pu32 = 0;

    VBOXSTRICTRC rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->lock, VINF_IOM_R3_IOPORT_READ);
    if (rc == VINF_SUCCESS)
    {
        PATADEVSTATE s = &pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK];

        if (s->iIOBufferPIODataStart < s->iIOBufferPIODataEnd)
        {
            AssertMsg(s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE, ("%#x\n", s->uTxDir));
            uint32_t const iIOBufferPIODataStart = RT_MIN(s->iIOBufferPIODataStart, sizeof(s->abIOBuffer));
            uint32_t const iIOBufferPIODataEnd   = RT_MIN(s->iIOBufferPIODataEnd,   sizeof(s->abIOBuffer));
            uint8_t const *pbSrc = &s->abIOBuffer[iIOBufferPIODataStart];
            uint8_t       *pbDst = (uint8_t *)pu32;

#ifdef IN_RC
            /* All but the last transfer unit is simple enough for RC, but
             * sending a request to the async IO thread is too complicated. */
            if (iIOBufferPIODataStart + cbActual < iIOBufferPIODataEnd)
                ataCopyPioData124(s, pbDst, pbSrc, iIOBufferPIODataStart, cbActual);
            else
                rc = VINF_IOM_R3_IOPORT_READ;

#elif defined(IN_RING0)
            /* Ring-0: We can do I/O thread signalling here.  However there is one
               case in ataHCPIOTransfer that does a LogRel and would (but not from
               here) call directly into the driver code.  We detect that odd case
               here cand return to ring-3 to handle it. */
            if (iIOBufferPIODataStart + cbActual < iIOBufferPIODataEnd)
                ataCopyPioData124(s, pbDst, pbSrc, iIOBufferPIODataStart, cbActual);
            else if (   s->cbTotalTransfer == 0
                     || s->iSourceSink != ATAFN_SS_NULL
                     || s->iIOBufferCur <= s->iIOBufferEnd)
            {
                ataCopyPioData124(s, pbDst, pbSrc, iIOBufferPIODataStart, cbActual);
                ataHCPIOTransferFinish(pDevIns, pCtl, s);
            }
            else
            {
                Log(("%s: Unexpected\n",__FUNCTION__));
                rc = VINF_IOM_R3_IOPORT_READ;
            }

#else  /* IN_RING3 */
            ataCopyPioData124(s, pbDst, pbSrc, iIOBufferPIODataStart, cbActual);
            if (s->iIOBufferPIODataStart >= iIOBufferPIODataEnd)
                ataHCPIOTransferFinish(pDevIns, pCtl, s);
#endif /* IN_RING3 */

            /* Just to be on the safe side (caller takes care of this, really). */
            if (cb == 1)
                *pu32 &= 0xff;
        }
        else
        {
            Log2(("%s: DUMMY data\n", __FUNCTION__));
            memset(pu32, 0xff, cb);
        }
        Log3(("%s: addr=%#x val=%.*Rhxs rc=%d\n", __FUNCTION__, offPort, cb, pu32, VBOXSTRICTRC_VAL(rc)));

        PDMDevHlpCritSectLeave(pDevIns, &pCtl->lock);
    }
    else
        Log3(("%s: addr=%#x -> %d\n", __FUNCTION__, offPort, VBOXSTRICTRC_VAL(rc)));

    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWINSTRING,
 * Port I/O Handler for primary port range IN string operations.}
 * @note    offPort is an absolute port number!
 */
static DECLCALLBACK(VBOXSTRICTRC)
ataIOPortReadStr1Data(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint8_t *pbDst, uint32_t *pcTransfers, unsigned cb)
{
    PATASTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATACONTROLLER pCtl  = &RT_SAFE_SUBSCRIPT(pThis->aCts, (uintptr_t)pvUser);
    RT_NOREF(offPort);

    Assert((uintptr_t)pvUser < 2);
    Assert(offPort == pCtl->IOPortBase1);
    Assert(*pcTransfers > 0);

    VBOXSTRICTRC rc;
    if (cb == 2 || cb == 4)
    {
        rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->lock, VINF_IOM_R3_IOPORT_READ);
        if (rc == VINF_SUCCESS)
        {
            PATADEVSTATE s = &pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK];

            uint32_t const offStart = s->iIOBufferPIODataStart;
            uint32_t const offEnd   = s->iIOBufferPIODataEnd;
            if (offStart < offEnd)
            {
                /*
                 * Figure how much we can copy.  Usually it's the same as the request.
                 * The last transfer unit cannot be handled in RC, as it involves
                 * thread communication.  In R0 we let the non-string callback handle it,
                 * and ditto for overflows/dummy data.
                 */
                uint32_t cAvailable = (offEnd - offStart) / cb;
#ifndef IN_RING3
                if (cAvailable > 0)
                    cAvailable--;
#endif
                uint32_t const cRequested = *pcTransfers;
                if (cAvailable > cRequested)
                    cAvailable = cRequested;
                uint32_t const cbTransfer = cAvailable * cb;
                uint32_t const offEndThisXfer = offStart + cbTransfer;
                if (   offEndThisXfer <= RT_MIN(s->cbIOBuffer, ATA_MAX_IO_BUFFER_SIZE)
                    && offStart       <  RT_MIN(s->cbIOBuffer, ATA_MAX_IO_BUFFER_SIZE) /* paranoia */
                    && cbTransfer     >  0)
                {
                    /*
                     * Do the transfer.
                     */
                    uint8_t const *pbSrc = &s->abIOBuffer[offStart];
                    memcpy(pbDst, pbSrc, cbTransfer);
                    Log3(("%s: addr=%#x cb=%#x cbTransfer=%#x val=%.*Rhxd\n", __FUNCTION__, offPort, cb, cbTransfer, cbTransfer, pbSrc));
                    s->iIOBufferPIODataStart = offEndThisXfer;
#ifdef IN_RING3
                    if (offEndThisXfer >= offEnd)
                        ataHCPIOTransferFinish(pDevIns, pCtl, s);
#endif
                    *pcTransfers = cRequested - cAvailable;
                }
                else
                    Log2(("ataIOPortReadStr1Data: DUMMY/Overflow!\n"));
            }
            else
            {
                /*
                 * Dummy read (shouldn't happen) return 0xff like the non-string handler.
                 */
                Log2(("ataIOPortReadStr1Data: DUMMY data (%#x bytes)\n", *pcTransfers * cb));
                memset(pbDst, 0xff, *pcTransfers * cb);
                *pcTransfers = 0;
            }

            PDMDevHlpCritSectLeave(pDevIns, &pCtl->lock);
        }
    }
    /*
     * Let the non-string I/O callback handle 1 byte reads.
     */
    else
    {
        Log2(("ataIOPortReadStr1Data: 1 byte read (%#x transfers)\n", *pcTransfers));
        AssertFailed();
        rc = VINF_SUCCESS;
    }
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUTSTRING,
 * Port I/O Handler for primary port range OUT string operations.}
 * @note    offPort is an absolute port number!
 */
static DECLCALLBACK(VBOXSTRICTRC)
ataIOPortWriteStr1Data(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint8_t const *pbSrc, uint32_t *pcTransfers, unsigned cb)
{
    PATASTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATACONTROLLER pCtl  = &RT_SAFE_SUBSCRIPT(pThis->aCts, (uintptr_t)pvUser);
    RT_NOREF(offPort);

    Assert((uintptr_t)pvUser < 2);
    Assert(offPort == pCtl->IOPortBase1);
    Assert(*pcTransfers > 0);

    VBOXSTRICTRC rc;
    if (cb == 2 || cb == 4)
    {
        rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->lock, VINF_IOM_R3_IOPORT_WRITE);
        if (rc == VINF_SUCCESS)
        {
            PATADEVSTATE s = &pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK];

            uint32_t const offStart = s->iIOBufferPIODataStart;
            uint32_t const offEnd   = s->iIOBufferPIODataEnd;
            Log3Func(("offStart=%#x offEnd=%#x *pcTransfers=%d cb=%d\n", offStart, offEnd, *pcTransfers, cb));
            if (offStart < offEnd)
            {
                /*
                 * Figure how much we can copy.  Usually it's the same as the request.
                 * The last transfer unit cannot be handled in RC, as it involves
                 * thread communication.  In R0 we let the non-string callback handle it,
                 * and ditto for overflows/dummy data.
                 */
                uint32_t cAvailable = (offEnd - offStart) / cb;
#ifndef IN_RING3
                if (cAvailable)
                    cAvailable--;
#endif
                uint32_t const cRequested = *pcTransfers;
                if (cAvailable > cRequested)
                    cAvailable = cRequested;
                uint32_t const cbTransfer = cAvailable * cb;
                uint32_t const offEndThisXfer = offStart + cbTransfer;
                if (   offEndThisXfer <= RT_MIN(s->cbIOBuffer, ATA_MAX_IO_BUFFER_SIZE)
                    && offStart       <  RT_MIN(s->cbIOBuffer, ATA_MAX_IO_BUFFER_SIZE) /* paranoia */
                    && cbTransfer     >  0)
                {
                    /*
                     * Do the transfer.
                     */
                    void *pvDst = &s->abIOBuffer[offStart];
                    memcpy(pvDst, pbSrc, cbTransfer);
                    Log3(("%s: addr=%#x val=%.*Rhxs\n", __FUNCTION__, offPort, cbTransfer, pvDst));
                    s->iIOBufferPIODataStart = offEndThisXfer;
#ifdef IN_RING3
                    if (offEndThisXfer >= offEnd)
                        ataHCPIOTransferFinish(pDevIns, pCtl, s);
#endif
                    *pcTransfers = cRequested - cAvailable;
                }
                else
                    Log2(("ataIOPortWriteStr1Data: DUMMY/Overflow!\n"));
            }
            else
            {
                Log2(("ataIOPortWriteStr1Data: DUMMY data (%#x bytes)\n", *pcTransfers * cb));
                *pcTransfers = 0;
            }

            PDMDevHlpCritSectLeave(pDevIns, &pCtl->lock);
        }
    }
    /*
     * Let the non-string I/O callback handle 1 byte reads.
     */
    else
    {
        Log2(("ataIOPortWriteStr1Data: 1 byte write (%#x transfers)\n", *pcTransfers));
        AssertFailed();
        rc = VINF_SUCCESS;
    }

    return rc;
}


#ifdef IN_RING3

static void ataR3DMATransferStop(PATADEVSTATE s)
{
    s->cbTotalTransfer = 0;
    s->cbElementaryTransfer = 0;
    s->iBeginTransfer = ATAFN_BT_NULL;
    s->iSourceSink = ATAFN_SS_NULL;
}


/**
 * Perform the entire DMA transfer in one go (unless a source/sink operation
 * has to be redone or a RESET comes in between). Unlike the PIO counterpart
 * this function cannot handle empty transfers.
 *
 * @param pDevIns   The device instance.
 * @param pCtl      Controller for which to perform the transfer, shared bits.
 * @param pCtlR3    The ring-3 controller state.
 */
static void ataR3DMATransfer(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATACONTROLLERR3 pCtlR3)
{
    uint8_t const   iAIOIf = pCtl->iAIOIf & ATA_SELECTED_IF_MASK;
    PATADEVSTATE    s      = &pCtl->aIfs[iAIOIf];
    PATADEVSTATER3  pDevR3 = &pCtlR3->aIfs[iAIOIf];
    bool fRedo;
    RTGCPHYS32 GCPhysDesc;
    uint32_t cbTotalTransfer, cbElementaryTransfer;
    uint32_t iIOBufferCur, iIOBufferEnd;
    PDMMEDIATXDIR uTxDir;
    bool fLastDesc = false;

    Assert(sizeof(BMDMADesc) == 8);

    fRedo = pCtl->fRedo;
    if (RT_LIKELY(!fRedo))
        Assert(s->cbTotalTransfer);
    uTxDir = (PDMMEDIATXDIR)s->uTxDir;
    cbTotalTransfer = s->cbTotalTransfer;
    cbElementaryTransfer = RT_MIN(s->cbElementaryTransfer, sizeof(s->abIOBuffer));
    iIOBufferEnd = RT_MIN(s->iIOBufferEnd, sizeof(s->abIOBuffer));
    iIOBufferCur = RT_MIN(RT_MIN(s->iIOBufferCur, sizeof(s->abIOBuffer)), iIOBufferEnd);

    /* The DMA loop is designed to hold the lock only when absolutely
     * necessary. This avoids long freezes should the guest access the
     * ATA registers etc. for some reason. */
    ataR3LockLeave(pDevIns, pCtl);

    Log2(("%s: %s tx_size=%d elem_tx_size=%d index=%d end=%d\n",
         __FUNCTION__, uTxDir == PDMMEDIATXDIR_FROM_DEVICE ? "T2I" : "I2T",
         cbTotalTransfer, cbElementaryTransfer,
         iIOBufferCur, iIOBufferEnd));
    for (GCPhysDesc = pCtl->GCPhysFirstDMADesc;
         GCPhysDesc <= pCtl->GCPhysLastDMADesc;
         GCPhysDesc += sizeof(BMDMADesc))
    {
        BMDMADesc DMADesc;
        RTGCPHYS32 GCPhysBuffer;
        uint32_t cbBuffer;

        if (RT_UNLIKELY(fRedo))
        {
            GCPhysBuffer = pCtl->GCPhysRedoDMABuffer;
            cbBuffer = pCtl->cbRedoDMABuffer;
            fLastDesc = pCtl->fRedoDMALastDesc;
            DMADesc.GCPhysBuffer = DMADesc.cbBuffer = 0; /* Shut up MSC. */
        }
        else
        {
            PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhysDesc, &DMADesc, sizeof(BMDMADesc));
            GCPhysBuffer = RT_LE2H_U32(DMADesc.GCPhysBuffer);
            cbBuffer = RT_LE2H_U32(DMADesc.cbBuffer);
            fLastDesc = RT_BOOL(cbBuffer & UINT32_C(0x80000000));
            cbBuffer &= 0xfffe;
            if (cbBuffer == 0)
                cbBuffer = 0x10000;
            if (cbBuffer > cbTotalTransfer)
                cbBuffer = cbTotalTransfer;
        }

        while (RT_UNLIKELY(fRedo) || (cbBuffer && cbTotalTransfer))
        {
            if (RT_LIKELY(!fRedo))
            {
                uint32_t cbXfer = RT_MIN(RT_MIN(cbBuffer, iIOBufferEnd - iIOBufferCur),
                                         sizeof(s->abIOBuffer) - RT_MIN(iIOBufferCur, sizeof(s->abIOBuffer)));
                Log2(("%s: DMA desc %#010x: addr=%#010x size=%#010x orig_size=%#010x\n", __FUNCTION__,
                      (int)GCPhysDesc, GCPhysBuffer, cbBuffer, RT_LE2H_U32(DMADesc.cbBuffer) & 0xfffe));

                if (uTxDir == PDMMEDIATXDIR_FROM_DEVICE)
                    PDMDevHlpPCIPhysWriteUser(pDevIns, GCPhysBuffer, &s->abIOBuffer[iIOBufferCur], cbXfer);
                else
                    PDMDevHlpPCIPhysReadUser(pDevIns, GCPhysBuffer, &s->abIOBuffer[iIOBufferCur], cbXfer);

                iIOBufferCur    += cbXfer;
                cbTotalTransfer -= cbXfer;
                cbBuffer        -= cbXfer;
                GCPhysBuffer    += cbXfer;
            }
            if (    iIOBufferCur == iIOBufferEnd
                &&  (uTxDir == PDMMEDIATXDIR_TO_DEVICE || cbTotalTransfer))
            {
                if (uTxDir == PDMMEDIATXDIR_FROM_DEVICE && cbElementaryTransfer > cbTotalTransfer)
                    cbElementaryTransfer = cbTotalTransfer;

                ataR3LockEnter(pDevIns, pCtl);

                /* The RESET handler could have cleared the DMA transfer
                 * state (since we didn't hold the lock until just now
                 * the guest can continue in parallel). If so, the state
                 * is already set up so the loop is exited immediately. */
                uint8_t const iSourceSink = s->iSourceSink;
                if (   iSourceSink != ATAFN_SS_NULL
                    && iSourceSink < RT_ELEMENTS(g_apfnSourceSinkFuncs))
                {
                    s->iIOBufferCur = iIOBufferCur;
                    s->iIOBufferEnd = iIOBufferEnd;
                    s->cbElementaryTransfer = cbElementaryTransfer;
                    s->cbTotalTransfer = cbTotalTransfer;
                    Log2(("%s: calling source/sink function\n", __FUNCTION__));
                    fRedo = g_apfnSourceSinkFuncs[iSourceSink](pDevIns, pCtl, s, pDevR3);
                    if (RT_UNLIKELY(fRedo))
                    {
                        pCtl->GCPhysFirstDMADesc = GCPhysDesc;
                        pCtl->GCPhysRedoDMABuffer = GCPhysBuffer;
                        pCtl->cbRedoDMABuffer = cbBuffer;
                        pCtl->fRedoDMALastDesc = fLastDesc;
                    }
                    else
                    {
                        cbTotalTransfer = s->cbTotalTransfer;
                        cbElementaryTransfer = s->cbElementaryTransfer;

                        if (uTxDir == PDMMEDIATXDIR_TO_DEVICE && cbElementaryTransfer > cbTotalTransfer)
                            cbElementaryTransfer = cbTotalTransfer;
                        iIOBufferCur = 0;
                        iIOBufferEnd = RT_MIN(cbElementaryTransfer, sizeof(s->abIOBuffer));
                    }
                    pCtl->fRedo = fRedo;
                }
                else
                {
                    /* This forces the loop to exit immediately. */
                    Assert(iSourceSink == ATAFN_SS_NULL);
                    GCPhysDesc = pCtl->GCPhysLastDMADesc + 1;
                }

                ataR3LockLeave(pDevIns, pCtl);
                if (RT_UNLIKELY(fRedo))
                    break;
            }
        }

        if (RT_UNLIKELY(fRedo))
            break;

        /* end of transfer */
        if (!cbTotalTransfer || fLastDesc)
            break;

        ataR3LockEnter(pDevIns, pCtl);

        if (!(pCtl->BmDma.u8Cmd & BM_CMD_START) || pCtl->fReset)
        {
            LogRel(("PIIX3 ATA: Ctl#%d: ABORT DMA%s\n", pCtl->iCtl, pCtl->fReset ? " due to RESET" : ""));
            if (!pCtl->fReset)
                ataR3DMATransferStop(s);
            /* This forces the loop to exit immediately. */
            GCPhysDesc = pCtl->GCPhysLastDMADesc + 1;
        }

        ataR3LockLeave(pDevIns, pCtl);
    }

    ataR3LockEnter(pDevIns, pCtl);
    if (RT_UNLIKELY(fRedo))
        return;

    if (fLastDesc)
        pCtl->BmDma.u8Status &= ~BM_STATUS_DMAING;
    s->cbTotalTransfer = cbTotalTransfer;
    s->cbElementaryTransfer = cbElementaryTransfer;
    s->iIOBufferCur = iIOBufferCur;
    s->iIOBufferEnd = iIOBufferEnd;
}

/**
 * Signal PDM that we're idle (if we actually are).
 *
 * @param   pDevIns     The device instance.
 * @param   pCtl        The shared controller state.
 * @param   pCtlR3      The ring-3 controller state.
 */
static void ataR3AsyncSignalIdle(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, PATACONTROLLERR3 pCtlR3)
{
    /*
     * Take the lock here and recheck the idle indicator to avoid
     * unnecessary work and racing ataR3WaitForAsyncIOIsIdle.
     */
    int rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->AsyncIORequestLock, VINF_SUCCESS);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pCtl->AsyncIORequestLock, rc);

    if (    pCtlR3->fSignalIdle
        &&  ataR3AsyncIOIsIdle(pDevIns, pCtl, false /*fStrict*/))
    {
        PDMDevHlpAsyncNotificationCompleted(pDevIns);
        RTThreadUserSignal(pCtlR3->hAsyncIOThread); /* for ataR3Construct/ataR3ResetCommon. */
    }

    rc = PDMDevHlpCritSectLeave(pDevIns, &pCtl->AsyncIORequestLock);
    AssertRC(rc);
}

/**
 * Async I/O thread for an interface.
 *
 * Once upon a time this was readable code with several loops and a different
 * semaphore for each purpose. But then came the "how can one save the state in
 * the middle of a PIO transfer" question.  The solution was to use an ASM,
 * which is what's there now.
 */
static DECLCALLBACK(int) ataR3AsyncIOThread(RTTHREAD hThreadSelf, void *pvUser)
{
    PATACONTROLLERR3 const  pCtlR3  = (PATACONTROLLERR3)pvUser;
    PPDMDEVINSR3 const      pDevIns = pCtlR3->pDevIns;
    PATASTATE const         pThis   = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATASTATER3 const       pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PATASTATER3);
    uintptr_t const         iCtl    = pCtlR3 - &pThisCC->aCts[0];
    PATACONTROLLER const    pCtl    = &RT_SAFE_SUBSCRIPT(pThis->aCts, iCtl);
    int                     rc      = VINF_SUCCESS;
    uint64_t                u64TS   = 0; /* shut up gcc */
    uint64_t                uWait;
    const ATARequest       *pReq;
    RT_NOREF(hThreadSelf);
    Assert(pCtl->iCtl == pCtlR3->iCtl);

    pReq = NULL;
    pCtl->fChainedTransfer = false;
    while (!pCtlR3->fShutdown)
    {
        /* Keep this thread from doing anything as long as EMT is suspended. */
        while (pCtl->fRedoIdle)
        {
            if (pCtlR3->fSignalIdle)
                ataR3AsyncSignalIdle(pDevIns, pCtl, pCtlR3);
            rc = RTSemEventWait(pCtlR3->hSuspendIOSem, RT_INDEFINITE_WAIT);
            /* Continue if we got a signal by RTThreadPoke().
             * We will get notified if there is a request to process.
             */
            if (RT_UNLIKELY(rc == VERR_INTERRUPTED))
                continue;
            if (RT_FAILURE(rc) || pCtlR3->fShutdown)
                break;

            pCtl->fRedoIdle = false;
        }

        /* Wait for work.  */
        while (pReq == NULL)
        {
            if (pCtlR3->fSignalIdle)
                ataR3AsyncSignalIdle(pDevIns, pCtl, pCtlR3);
            rc = PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pCtl->hAsyncIOSem, RT_INDEFINITE_WAIT);
            /* Continue if we got a signal by RTThreadPoke().
             * We will get notified if there is a request to process.
             */
            if (RT_UNLIKELY(rc == VERR_INTERRUPTED))
                continue;
            if (RT_FAILURE(rc) || RT_UNLIKELY(pCtlR3->fShutdown))
                break;

            pReq = ataR3AsyncIOGetCurrentRequest(pDevIns, pCtl);
        }

        if (RT_FAILURE(rc) || pCtlR3->fShutdown)
            break;

        if (pReq == NULL)
            continue;

        ATAAIO ReqType = pReq->ReqType;

        Log2(("%s: Ctl#%d: state=%d, req=%d\n", __FUNCTION__, pCtl->iCtl, pCtl->uAsyncIOState, ReqType));
        if (pCtl->uAsyncIOState != ReqType)
        {
            /* The new state is not the state that was expected by the normal
             * state changes. This is either a RESET/ABORT or there's something
             * really strange going on. */
            if (    (pCtl->uAsyncIOState == ATA_AIO_PIO || pCtl->uAsyncIOState == ATA_AIO_DMA)
                &&  (ReqType == ATA_AIO_PIO || ReqType == ATA_AIO_DMA))
            {
                /* Incorrect sequence of PIO/DMA states. Dump request queue. */
                ataR3AsyncIODumpRequests(pDevIns, pCtl);
            }
            AssertReleaseMsg(   ReqType == ATA_AIO_RESET_ASSERTED
                             || ReqType == ATA_AIO_RESET_CLEARED
                             || ReqType == ATA_AIO_ABORT
                             || pCtl->uAsyncIOState == ReqType,
                             ("I/O state inconsistent: state=%d request=%d\n", pCtl->uAsyncIOState, ReqType));
        }

        /* Do our work.  */
        ataR3LockEnter(pDevIns, pCtl);

        if (pCtl->uAsyncIOState == ATA_AIO_NEW && !pCtl->fChainedTransfer)
        {
            u64TS = RTTimeNanoTS();
#if defined(DEBUG) || defined(VBOX_WITH_STATISTICS)
            STAM_PROFILE_ADV_START(&pCtl->StatAsyncTime, a);
#endif
        }

        switch (ReqType)
        {
            case ATA_AIO_NEW:
            {
                uint8_t const iIf = pReq->u.t.iIf & ATA_SELECTED_IF_MASK;
                pCtl->iAIOIf = iIf;
                PATADEVSTATE   s      = &pCtl->aIfs[iIf];
                PATADEVSTATER3 pDevR3 = &pCtlR3->aIfs[iIf];

                s->cbTotalTransfer = pReq->u.t.cbTotalTransfer;
                s->uTxDir = pReq->u.t.uTxDir;
                s->iBeginTransfer = pReq->u.t.iBeginTransfer;
                s->iSourceSink = pReq->u.t.iSourceSink;
                s->iIOBufferEnd = 0;
                s->u64CmdTS = u64TS;

                if (s->fATAPI)
                {
                    if (pCtl->fChainedTransfer)
                    {
                        /* Only count the actual transfers, not the PIO
                         * transfer of the ATAPI command bytes. */
                        if (s->fDMA)
                            STAM_REL_COUNTER_INC(&s->StatATAPIDMA);
                        else
                            STAM_REL_COUNTER_INC(&s->StatATAPIPIO);
                    }
                }
                else
                {
                    if (s->fDMA)
                        STAM_REL_COUNTER_INC(&s->StatATADMA);
                    else
                        STAM_REL_COUNTER_INC(&s->StatATAPIO);
                }

                pCtl->fChainedTransfer = false;

                uint8_t const iBeginTransfer = s->iBeginTransfer;
                if (   iBeginTransfer != ATAFN_BT_NULL
                    && iBeginTransfer < RT_ELEMENTS(g_apfnBeginTransFuncs))
                {
                    Log2(("%s: Ctl#%d: calling begin transfer function\n", __FUNCTION__, pCtl->iCtl));
                    g_apfnBeginTransFuncs[iBeginTransfer](pCtl, s);
                    s->iBeginTransfer = ATAFN_BT_NULL;
                    if (s->uTxDir != PDMMEDIATXDIR_FROM_DEVICE)
                        s->iIOBufferEnd = s->cbElementaryTransfer;
                }
                else
                {
                    Assert(iBeginTransfer == ATAFN_BT_NULL);
                    s->cbElementaryTransfer = s->cbTotalTransfer;
                    s->iIOBufferEnd = s->cbTotalTransfer;
                }
                s->iIOBufferCur = 0;

                if (s->uTxDir != PDMMEDIATXDIR_TO_DEVICE)
                {
                    uint8_t const iSourceSink = s->iSourceSink;
                    if (   iSourceSink != ATAFN_SS_NULL
                        && iSourceSink < RT_ELEMENTS(g_apfnSourceSinkFuncs))
                    {
                        bool fRedo;
                        Log2(("%s: Ctl#%d: calling source/sink function\n", __FUNCTION__, pCtl->iCtl));
                        fRedo = g_apfnSourceSinkFuncs[iSourceSink](pDevIns, pCtl, s, pDevR3);
                        pCtl->fRedo = fRedo;
                        if (RT_UNLIKELY(fRedo && !pCtl->fReset))
                        {
                            /* Operation failed at the initial transfer, restart
                             * everything from scratch by resending the current
                             * request. Occurs very rarely, not worth optimizing. */
                            LogRel(("%s: Ctl#%d: redo entire operation\n", __FUNCTION__, pCtl->iCtl));
                            ataHCAsyncIOPutRequest(pDevIns, pCtl, pReq);
                            break;
                        }
                    }
                    else
                    {
                        Assert(iSourceSink == ATAFN_SS_NULL);
                        ataR3CmdOK(pCtl, s, ATA_STAT_SEEK);
                    }
                    s->iIOBufferEnd = s->cbElementaryTransfer;

                }

                /* Do not go into the transfer phase if RESET is asserted.
                 * The CritSect is released while waiting for the host OS
                 * to finish the I/O, thus RESET is possible here. Most
                 * important: do not change uAsyncIOState. */
                if (pCtl->fReset)
                    break;

                if (s->fDMA)
                {
                    if (s->cbTotalTransfer)
                    {
                        ataSetStatus(pCtl, s, ATA_STAT_DRQ);

                        pCtl->uAsyncIOState = ATA_AIO_DMA;
                        /* If BMDMA is already started, do the transfer now. */
                        if (pCtl->BmDma.u8Cmd & BM_CMD_START)
                        {
                            Log2(("%s: Ctl#%d: message to async I/O thread, continuing DMA transfer immediately\n", __FUNCTION__, pCtl->iCtl));
                            ataHCAsyncIOPutRequest(pDevIns, pCtl, &g_ataDMARequest);
                        }
                    }
                    else
                    {
                        Assert(s->uTxDir == PDMMEDIATXDIR_NONE); /* Any transfer which has an initial transfer size of 0 must be marked as such. */
                        /* Finish DMA transfer. */
                        ataR3DMATransferStop(s);
                        ataHCSetIRQ(pDevIns, pCtl, s);
                        pCtl->uAsyncIOState = ATA_AIO_NEW;
                    }
                }
                else
                {
                    if (s->cbTotalTransfer)
                    {
                        ataHCPIOTransfer(pDevIns, pCtl);
                        Assert(!pCtl->fRedo);
                        if (s->fATAPITransfer || s->uTxDir != PDMMEDIATXDIR_TO_DEVICE)
                            ataHCSetIRQ(pDevIns, pCtl, s);

                        if (s->uTxDir == PDMMEDIATXDIR_TO_DEVICE || s->iSourceSink != ATAFN_SS_NULL)
                        {
                            /* Write operations and not yet finished transfers
                             * must be completed in the async I/O thread. */
                            pCtl->uAsyncIOState = ATA_AIO_PIO;
                        }
                        else
                        {
                            /* Finished read operation can be handled inline
                             * in the end of PIO transfer handling code. Linux
                             * depends on this, as it waits only briefly for
                             * devices to become ready after incoming data
                             * transfer. Cannot find anything in the ATA spec
                             * that backs this assumption, but as all kernels
                             * are affected (though most of the time it does
                             * not cause any harm) this must work. */
                            pCtl->uAsyncIOState = ATA_AIO_NEW;
                        }
                    }
                    else
                    {
                        Assert(s->uTxDir == PDMMEDIATXDIR_NONE); /* Any transfer which has an initial transfer size of 0 must be marked as such. */
                        /* Finish PIO transfer. */
                        ataHCPIOTransfer(pDevIns, pCtl);
                        Assert(!pCtl->fRedo);
                        if (!s->fATAPITransfer)
                            ataHCSetIRQ(pDevIns, pCtl, s);
                        pCtl->uAsyncIOState = ATA_AIO_NEW;
                    }
                }
                break;
            }

            case ATA_AIO_DMA:
            {
                BMDMAState   *bm = &pCtl->BmDma;
                PATADEVSTATE  s  = &pCtl->aIfs[pCtl->iAIOIf & ATA_SELECTED_IF_MASK];
                ATAFNSS iOriginalSourceSink = (ATAFNSS)s->iSourceSink; /* Used by the hack below, but gets reset by then. */

                if (s->uTxDir == PDMMEDIATXDIR_FROM_DEVICE)
                    AssertRelease(bm->u8Cmd & BM_CMD_WRITE);
                else
                    AssertRelease(!(bm->u8Cmd & BM_CMD_WRITE));

                if (RT_LIKELY(!pCtl->fRedo))
                {
                    /* The specs say that the descriptor table must not cross a
                     * 4K boundary. */
                    pCtl->GCPhysFirstDMADesc = bm->GCPhysAddr;
                    pCtl->GCPhysLastDMADesc = RT_ALIGN_32(bm->GCPhysAddr + 1, _4K) - sizeof(BMDMADesc);
                }
                ataR3DMATransfer(pDevIns, pCtl, pCtlR3);

                if (RT_UNLIKELY(pCtl->fRedo && !pCtl->fReset))
                {
                    LogRel(("PIIX3 ATA: Ctl#%d: redo DMA operation\n", pCtl->iCtl));
                    ataHCAsyncIOPutRequest(pDevIns, pCtl, &g_ataDMARequest);
                    break;
                }

                /* The infamous delay IRQ hack. */
                if (   iOriginalSourceSink == ATAFN_SS_WRITE_SECTORS
                    && s->cbTotalTransfer == 0
                    && pCtl->msDelayIRQ)
                {
                    /* Delay IRQ for writing. Required to get the Win2K
                     * installation work reliably (otherwise it crashes,
                     * usually during component install). So far no better
                     * solution has been found. */
                    Log(("%s: delay IRQ hack\n", __FUNCTION__));
                    ataR3LockLeave(pDevIns, pCtl);
                    RTThreadSleep(pCtl->msDelayIRQ);
                    ataR3LockEnter(pDevIns, pCtl);
                }

                ataUnsetStatus(pCtl, s, ATA_STAT_DRQ);
                Assert(!pCtl->fChainedTransfer);
                Assert(s->iSourceSink == ATAFN_SS_NULL);
                if (s->fATAPITransfer)
                {
                    s->uATARegNSector = (s->uATARegNSector & ~7) | ATAPI_INT_REASON_IO | ATAPI_INT_REASON_CD;
                    Log2(("%s: Ctl#%d: interrupt reason %#04x\n", __FUNCTION__, pCtl->iCtl, s->uATARegNSector));
                    s->fATAPITransfer = false;
                }
                ataHCSetIRQ(pDevIns, pCtl, s);
                pCtl->uAsyncIOState = ATA_AIO_NEW;
                break;
            }

            case ATA_AIO_PIO:
            {
                uint8_t const iIf = pCtl->iAIOIf & ATA_SELECTED_IF_MASK;
                pCtl->iAIOIf = iIf;
                PATADEVSTATE   s      = &pCtl->aIfs[iIf];
                PATADEVSTATER3 pDevR3 = &pCtlR3->aIfs[iIf];

                uint8_t const iSourceSink = s->iSourceSink;
                if (   iSourceSink != ATAFN_SS_NULL
                    && iSourceSink < RT_ELEMENTS(g_apfnSourceSinkFuncs))
                {
                    bool fRedo;
                    Log2(("%s: Ctl#%d: calling source/sink function\n", __FUNCTION__, pCtl->iCtl));
                    fRedo = g_apfnSourceSinkFuncs[iSourceSink](pDevIns, pCtl, s, pDevR3);
                    pCtl->fRedo = fRedo;
                    if (RT_UNLIKELY(fRedo && !pCtl->fReset))
                    {
                        LogRel(("PIIX3 ATA: Ctl#%d: redo PIO operation\n", pCtl->iCtl));
                        ataHCAsyncIOPutRequest(pDevIns, pCtl, &g_ataPIORequest);
                        break;
                    }
                    s->iIOBufferCur = 0;
                    s->iIOBufferEnd = s->cbElementaryTransfer;
                }
                else
                {
                    /* Continue a previously started transfer. */
                    Assert(iSourceSink == ATAFN_SS_NULL);
                    ataUnsetStatus(pCtl, s, ATA_STAT_BUSY);
                    ataSetStatus(pCtl, s, ATA_STAT_READY);
                }

                /* It is possible that the drives on this controller get RESET
                 * during the above call to the source/sink function. If that's
                 * the case, don't restart the transfer and don't finish it the
                 * usual way. RESET handling took care of all that already.
                 * Most important: do not change uAsyncIOState. */
                if (pCtl->fReset)
                    break;

                if (s->cbTotalTransfer)
                {
                    ataHCPIOTransfer(pDevIns, pCtl);
                    ataHCSetIRQ(pDevIns, pCtl, s);

                    if (s->uTxDir == PDMMEDIATXDIR_TO_DEVICE || s->iSourceSink != ATAFN_SS_NULL)
                    {
                        /* Write operations and not yet finished transfers
                         * must be completed in the async I/O thread. */
                        pCtl->uAsyncIOState = ATA_AIO_PIO;
                    }
                    else
                    {
                        /* Finished read operation can be handled inline
                         * in the end of PIO transfer handling code. Linux
                         * depends on this, as it waits only briefly for
                         * devices to become ready after incoming data
                         * transfer. Cannot find anything in the ATA spec
                         * that backs this assumption, but as all kernels
                         * are affected (though most of the time it does
                         * not cause any harm) this must work. */
                        pCtl->uAsyncIOState = ATA_AIO_NEW;
                    }
                }
                else
                {
                    /* The infamous delay IRQ hack. */
                    if (RT_UNLIKELY(pCtl->msDelayIRQ))
                    {
                        /* Various antique guests have buggy disk drivers silently
                         * assuming that disk operations take a relatively long time.
                         * Work around such bugs by holding off interrupts a bit.
                         */
                        Log(("%s: delay IRQ hack (PIO)\n", __FUNCTION__));
                        ataR3LockLeave(pDevIns, pCtl);
                        RTThreadSleep(pCtl->msDelayIRQ);
                        ataR3LockEnter(pDevIns, pCtl);
                    }

                    /* Finish PIO transfer. */
                    ataHCPIOTransfer(pDevIns, pCtl);
                    if (    !pCtl->fChainedTransfer
                        &&  !s->fATAPITransfer
                        &&  s->uTxDir != PDMMEDIATXDIR_FROM_DEVICE)
                    {
                            ataHCSetIRQ(pDevIns, pCtl, s);
                    }
                    pCtl->uAsyncIOState = ATA_AIO_NEW;
                }
                break;
            }

            case ATA_AIO_RESET_ASSERTED:
                pCtl->uAsyncIOState = ATA_AIO_RESET_CLEARED;
                ataHCPIOTransferStop(pDevIns, pCtl, &pCtl->aIfs[0]);
                ataHCPIOTransferStop(pDevIns, pCtl, &pCtl->aIfs[1]);
                /* Do not change the DMA registers, they are not affected by the
                 * ATA controller reset logic. It should be sufficient to issue a
                 * new command, which is now possible as the state is cleared. */
                break;

            case ATA_AIO_RESET_CLEARED:
                pCtl->uAsyncIOState = ATA_AIO_NEW;
                pCtl->fReset = false;
                /* Ensure that half-completed transfers are not redone. A reset
                 * cancels the entire transfer, so continuing is wrong. */
                pCtl->fRedo = false;
                pCtl->fRedoDMALastDesc = false;
                LogRel(("PIIX3 ATA: Ctl#%d: finished processing RESET\n", pCtl->iCtl));
                for (uint32_t i = 0; i < RT_ELEMENTS(pCtl->aIfs); i++)
                {
                    ataR3SetSignature(&pCtl->aIfs[i]);
                    if (pCtl->aIfs[i].fATAPI)
                        ataSetStatusValue(pCtl, &pCtl->aIfs[i], 0); /* NOTE: READY is _not_ set */
                    else
                        ataSetStatusValue(pCtl, &pCtl->aIfs[i], ATA_STAT_READY | ATA_STAT_SEEK);
                }
                break;

            case ATA_AIO_ABORT:
            {
                /* Abort the current command no matter what. There cannot be
                 * any command activity on the other drive otherwise using
                 * one thread per controller wouldn't work at all. */
                PATADEVSTATE s = &pCtl->aIfs[pReq->u.a.iIf & ATA_SELECTED_IF_MASK];

                pCtl->uAsyncIOState = ATA_AIO_NEW;
                /* Do not change the DMA registers, they are not affected by the
                 * ATA controller reset logic. It should be sufficient to issue a
                 * new command, which is now possible as the state is cleared. */
                if (pReq->u.a.fResetDrive)
                {
                    ataR3ResetDevice(pDevIns, pCtl, s);
                    ataR3DeviceDiag(pCtl, s);
                }
                else
                {
                    /* Stop any pending DMA transfer. */
                    s->fDMA = false;
                    ataHCPIOTransferStop(pDevIns, pCtl, s);
                    ataUnsetStatus(pCtl, s, ATA_STAT_BUSY | ATA_STAT_DRQ | ATA_STAT_SEEK | ATA_STAT_ERR);
                    ataSetStatus(pCtl, s, ATA_STAT_READY);
                    ataHCSetIRQ(pDevIns, pCtl, s);
                }
                break;
            }

            default:
                AssertMsgFailed(("Undefined async I/O state %d\n", pCtl->uAsyncIOState));
        }

        ataR3AsyncIORemoveCurrentRequest(pDevIns, pCtl, ReqType);
        pReq = ataR3AsyncIOGetCurrentRequest(pDevIns, pCtl);

        if (pCtl->uAsyncIOState == ATA_AIO_NEW && !pCtl->fChainedTransfer)
        {
# if defined(DEBUG) || defined(VBOX_WITH_STATISTICS)
            STAM_PROFILE_ADV_STOP(&pCtl->StatAsyncTime, a);
# endif

            u64TS = RTTimeNanoTS() - u64TS;
            uWait = u64TS / 1000;
            uintptr_t const iAIOIf = pCtl->iAIOIf & ATA_SELECTED_IF_MASK;
            Log(("%s: Ctl#%d: LUN#%d finished I/O transaction in %d microseconds\n",
                 __FUNCTION__, pCtl->iCtl, pCtl->aIfs[iAIOIf].iLUN, (uint32_t)(uWait)));
            /* Mark command as finished. */
            pCtl->aIfs[iAIOIf].u64CmdTS = 0;

            /*
             * Release logging of command execution times depends on the
             * command type. ATAPI commands often take longer (due to CD/DVD
             * spin up time etc.) so the threshold is different.
             */
            if (pCtl->aIfs[iAIOIf].uATARegCommand != ATA_PACKET)
            {
                if (uWait > 8 * 1000 * 1000)
                {
                    /*
                     * Command took longer than 8 seconds. This is close
                     * enough or over the guest's command timeout, so place
                     * an entry in the release log to allow tracking such
                     * timing errors (which are often caused by the host).
                     */
                    LogRel(("PIIX3 ATA: execution time for ATA command %#04x was %d seconds\n",
                            pCtl->aIfs[iAIOIf].uATARegCommand, uWait / (1000 * 1000)));
                }
            }
            else
            {
                if (uWait > 20 * 1000 * 1000)
                {
                    /*
                     * Command took longer than 20 seconds. This is close
                     * enough or over the guest's command timeout, so place
                     * an entry in the release log to allow tracking such
                     * timing errors (which are often caused by the host).
                     */
                    LogRel(("PIIX3 ATA: execution time for ATAPI command %#04x was %d seconds\n",
                            pCtl->aIfs[iAIOIf].abATAPICmd[0], uWait / (1000 * 1000)));
                }
            }

# if defined(DEBUG) || defined(VBOX_WITH_STATISTICS)
            if (uWait < pCtl->StatAsyncMinWait || !pCtl->StatAsyncMinWait)
                pCtl->StatAsyncMinWait = uWait;
            if (uWait > pCtl->StatAsyncMaxWait)
                pCtl->StatAsyncMaxWait = uWait;

            STAM_COUNTER_ADD(&pCtl->StatAsyncTimeUS, uWait);
            STAM_COUNTER_INC(&pCtl->StatAsyncOps);
# endif /* DEBUG || VBOX_WITH_STATISTICS */
        }

        ataR3LockLeave(pDevIns, pCtl);
    }

    /* Signal the ultimate idleness. */
    RTThreadUserSignal(pCtlR3->hAsyncIOThread);
    if (pCtlR3->fSignalIdle)
        PDMDevHlpAsyncNotificationCompleted(pDevIns);

    /* Cleanup the state.  */
    /* Do not destroy request lock yet, still needed for proper shutdown. */
    pCtlR3->fShutdown = false;

    Log2(("%s: Ctl#%d: return %Rrc\n", __FUNCTION__, pCtl->iCtl, rc));
    return rc;
}

#endif /* IN_RING3 */

static uint32_t ataBMDMACmdReadB(PATACONTROLLER pCtl, uint32_t addr)
{
    uint32_t val = pCtl->BmDma.u8Cmd;
    RT_NOREF(addr);
    Log2(("%s: addr=%#06x val=%#04x\n", __FUNCTION__, addr, val));
    return val;
}


static void ataBMDMACmdWriteB(PPDMDEVINS pDevIns, PATACONTROLLER pCtl, uint32_t addr, uint32_t val)
{
    RT_NOREF(pDevIns, addr);
    Log2(("%s: addr=%#06x val=%#04x\n", __FUNCTION__, addr, val));
    if (!(val & BM_CMD_START))
    {
        pCtl->BmDma.u8Status &= ~BM_STATUS_DMAING;
        pCtl->BmDma.u8Cmd = val & (BM_CMD_START | BM_CMD_WRITE);
    }
    else
    {
#ifndef IN_RC
        /* Check whether the guest OS wants to change DMA direction in
         * mid-flight. Not allowed, according to the PIIX3 specs. */
        Assert(!(pCtl->BmDma.u8Status & BM_STATUS_DMAING) || !((val ^ pCtl->BmDma.u8Cmd) & 0x04));
        uint8_t uOldBmDmaStatus = pCtl->BmDma.u8Status;
        pCtl->BmDma.u8Status |= BM_STATUS_DMAING;
        pCtl->BmDma.u8Cmd = val & (BM_CMD_START | BM_CMD_WRITE);

        /* Do not continue DMA transfers while the RESET line is asserted. */
        if (pCtl->fReset)
        {
            Log2(("%s: Ctl#%d: suppressed continuing DMA transfer as RESET is active\n", __FUNCTION__, pCtl->iCtl));
            return;
        }

        /* Do not start DMA transfers if there's a PIO transfer going on,
         * or if there is already a transfer started on this controller. */
        if (   !pCtl->aIfs[pCtl->iSelectedIf & ATA_SELECTED_IF_MASK].fDMA
            || (uOldBmDmaStatus & BM_STATUS_DMAING))
            return;

        if (pCtl->aIfs[pCtl->iAIOIf & ATA_SELECTED_IF_MASK].uATARegStatus & ATA_STAT_DRQ)
        {
            Log2(("%s: Ctl#%d: message to async I/O thread, continuing DMA transfer\n", __FUNCTION__, pCtl->iCtl));
            ataHCAsyncIOPutRequest(pDevIns, pCtl, &g_ataDMARequest);
        }
#else /* !IN_RING3 */
        AssertMsgFailed(("DMA START handling is too complicated for RC\n"));
#endif /* IN_RING3 */
    }
}

static uint32_t ataBMDMAStatusReadB(PATACONTROLLER pCtl, uint32_t addr)
{
    uint32_t val = pCtl->BmDma.u8Status;
    RT_NOREF(addr);
    Log2(("%s: addr=%#06x val=%#04x\n", __FUNCTION__, addr, val));
    return val;
}

static void ataBMDMAStatusWriteB(PATACONTROLLER pCtl, uint32_t addr, uint32_t val)
{
    RT_NOREF(addr);
    Log2(("%s: addr=%#06x val=%#04x\n", __FUNCTION__, addr, val));
    pCtl->BmDma.u8Status =    (val & (BM_STATUS_D0DMA | BM_STATUS_D1DMA))
                           |  (pCtl->BmDma.u8Status & BM_STATUS_DMAING)
                           |  (pCtl->BmDma.u8Status & ~val & (BM_STATUS_ERROR | BM_STATUS_INT));
}

static uint32_t ataBMDMAAddrReadL(PATACONTROLLER pCtl, uint32_t addr)
{
    uint32_t val = (uint32_t)pCtl->BmDma.GCPhysAddr;
    RT_NOREF(addr);
    Log2(("%s: addr=%#06x val=%#010x\n", __FUNCTION__, addr, val));
    return val;
}

static void ataBMDMAAddrWriteL(PATACONTROLLER pCtl, uint32_t addr, uint32_t val)
{
    RT_NOREF(addr);
    Log2(("%s: addr=%#06x val=%#010x\n", __FUNCTION__, addr, val));
    pCtl->BmDma.GCPhysAddr = val & ~3;
}

static void ataBMDMAAddrWriteLowWord(PATACONTROLLER pCtl, uint32_t addr, uint32_t val)
{
    RT_NOREF(addr);
    Log2(("%s: addr=%#06x val=%#010x\n", __FUNCTION__, addr, val));
    pCtl->BmDma.GCPhysAddr = (pCtl->BmDma.GCPhysAddr & 0xFFFF0000) | RT_LOWORD(val & ~3);

}

static void ataBMDMAAddrWriteHighWord(PATACONTROLLER pCtl, uint32_t addr, uint32_t val)
{
    Log2(("%s: addr=%#06x val=%#010x\n", __FUNCTION__, addr, val));
    RT_NOREF(addr);
    pCtl->BmDma.GCPhysAddr = (RT_LOWORD(val) << 16) | RT_LOWORD(pCtl->BmDma.GCPhysAddr);
}

/** Helper for ataBMDMAIOPortRead and ataBMDMAIOPortWrite.  */
#define VAL(port, size)   ( ((port) & BM_DMA_CTL_IOPORTS_MASK) | ((size) << BM_DMA_CTL_IOPORTS_SHIFT) )

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 * Port I/O Handler for bus-master DMA IN operations - both controllers.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
ataBMDMAIOPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PATASTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATACONTROLLER pCtl  = &RT_SAFE_SUBSCRIPT(pThis->aCts, (offPort >> BM_DMA_CTL_IOPORTS_SHIFT));
    RT_NOREF(pvUser);

    VBOXSTRICTRC rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->lock, VINF_IOM_R3_IOPORT_READ);
    if (rc == VINF_SUCCESS)
    {
        switch (VAL(offPort, cb))
        {
            case VAL(0, 1): *pu32 = ataBMDMACmdReadB(pCtl, offPort); break;
            case VAL(0, 2): *pu32 = ataBMDMACmdReadB(pCtl, offPort); break;
            case VAL(2, 1): *pu32 = ataBMDMAStatusReadB(pCtl, offPort); break;
            case VAL(2, 2): *pu32 = ataBMDMAStatusReadB(pCtl, offPort); break;
            case VAL(4, 4): *pu32 = ataBMDMAAddrReadL(pCtl, offPort); break;
            case VAL(0, 4):
                /* The SCO OpenServer tries to read 4 bytes starting from offset 0. */
                *pu32 = ataBMDMACmdReadB(pCtl, offPort) | (ataBMDMAStatusReadB(pCtl, offPort) << 16);
                break;
            default:
                ASSERT_GUEST_MSG_FAILED(("Unsupported read from port %x size=%d\n", offPort, cb));
                rc = VERR_IOM_IOPORT_UNUSED;
                break;
        }
        PDMDevHlpCritSectLeave(pDevIns, &pCtl->lock);
    }
    return rc;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 * Port I/O Handler for bus-master DMA OUT operations - both controllers.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
ataBMDMAIOPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PATASTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATACONTROLLER pCtl  = &RT_SAFE_SUBSCRIPT(pThis->aCts, (offPort >> BM_DMA_CTL_IOPORTS_SHIFT));
    RT_NOREF(pvUser);

    VBOXSTRICTRC rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->lock, VINF_IOM_R3_IOPORT_WRITE);
    if (rc == VINF_SUCCESS)
    {
        switch (VAL(offPort, cb))
        {
            case VAL(0, 1):
#ifdef IN_RC
                if (u32 & BM_CMD_START)
                {
                    rc = VINF_IOM_R3_IOPORT_WRITE;
                    break;
                }
#endif
                ataBMDMACmdWriteB(pDevIns, pCtl, offPort, u32);
                break;
            case VAL(2, 1): ataBMDMAStatusWriteB(pCtl, offPort, u32); break;
            case VAL(4, 4): ataBMDMAAddrWriteL(pCtl, offPort, u32); break;
            case VAL(4, 2): ataBMDMAAddrWriteLowWord(pCtl, offPort, u32); break;
            case VAL(6, 2): ataBMDMAAddrWriteHighWord(pCtl, offPort, u32); break;
            default:
                ASSERT_GUEST_MSG_FAILED(("Unsupported write to port %x size=%d val=%x\n", offPort, cb, u32));
                break;
        }
        PDMDevHlpCritSectLeave(pDevIns, &pCtl->lock);
    }
    return rc;
}

#undef VAL

#ifdef IN_RING3

/* -=-=-=-=-=- ATASTATE::IBase  -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) ataR3Status_QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PATASTATER3 pThisCC = RT_FROM_MEMBER(pInterface, ATASTATER3, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDPORTS, &pThisCC->ILeds);
    return NULL;
}


/* -=-=-=-=-=- ATASTATE::ILeds  -=-=-=-=-=- */

/**
 * Gets the pointer to the status LED of a unit.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   iLUN            The unit which status LED we desire.
 * @param   ppLed           Where to store the LED pointer.
 */
static DECLCALLBACK(int) ataR3Status_QueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    if (iLUN < 4)
    {
        PATASTATER3 pThisCC = RT_FROM_MEMBER(pInterface, ATASTATER3, ILeds);
        PATASTATE   pThis   = PDMDEVINS_2_DATA(pThisCC->pDevIns, PATASTATE);
        switch (iLUN)
        {
            case 0: *ppLed = &pThis->aCts[0].aIfs[0].Led; break;
            case 1: *ppLed = &pThis->aCts[0].aIfs[1].Led; break;
            case 2: *ppLed = &pThis->aCts[1].aIfs[0].Led; break;
            case 3: *ppLed = &pThis->aCts[1].aIfs[1].Led; break;
        }
        Assert((*ppLed)->u32Magic == PDMLED_MAGIC);
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}


/* -=-=-=-=-=- ATADEVSTATE::IBase   -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) ataR3QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PATADEVSTATER3 pIfR3 = RT_FROM_MEMBER(pInterface, ATADEVSTATER3, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pIfR3->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAPORT, &pIfR3->IPort);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMOUNTNOTIFY, &pIfR3->IMountNotify);
    return NULL;
}


/* -=-=-=-=-=- ATADEVSTATE::IPort  -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIMEDIAPORT,pfnQueryDeviceLocation}
 */
static DECLCALLBACK(int) ataR3QueryDeviceLocation(PPDMIMEDIAPORT pInterface, const char **ppcszController,
                                                  uint32_t *piInstance, uint32_t *piLUN)
{
    PATADEVSTATER3 pIfR3   = RT_FROM_MEMBER(pInterface, ATADEVSTATER3, IPort);
    PPDMDEVINS     pDevIns = pIfR3->pDevIns;

    AssertPtrReturn(ppcszController, VERR_INVALID_POINTER);
    AssertPtrReturn(piInstance, VERR_INVALID_POINTER);
    AssertPtrReturn(piLUN, VERR_INVALID_POINTER);

    *ppcszController = pDevIns->pReg->szName;
    *piInstance = pDevIns->iInstance;
    *piLUN = pIfR3->iLUN;

    return VINF_SUCCESS;
}

#endif /* IN_RING3 */

/* -=-=-=-=-=- Wrappers  -=-=-=-=-=- */


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 * Port I/O Handler for OUT operations on unpopulated IDE channels.}
 * @note    offPort is an absolute port number!
 */
static DECLCALLBACK(VBOXSTRICTRC)
ataIOPortWriteEmptyBus(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(pDevIns, pvUser, offPort, u32, cb);

#ifdef VBOX_STRICT
    PATASTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATACONTROLLER pCtl  = &RT_SAFE_SUBSCRIPT(pThis->aCts, (uintptr_t)pvUser);
    Assert((uintptr_t)pvUser < 2);
    Assert(!pCtl->aIfs[0].fPresent && !pCtl->aIfs[1].fPresent);
#endif

    /* This is simply a black hole, writes on unpopulated IDE channels elicit no response. */
    LogFunc(("Empty bus: Ignoring write to port %x val=%x size=%d\n", offPort, u32, cb));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,
 * Port I/O Handler for IN operations on unpopulated IDE channels.}
 * @note    offPort is an absolute port number!
 */
static DECLCALLBACK(VBOXSTRICTRC)
ataIOPortReadEmptyBus(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(pDevIns, offPort, pvUser);

#ifdef VBOX_STRICT
    PATASTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATACONTROLLER pCtl = &RT_SAFE_SUBSCRIPT(pThis->aCts, (uintptr_t)pvUser);
    Assert((uintptr_t)pvUser < 2);
    Assert(cb <= 4);
    Assert(!pCtl->aIfs[0].fPresent && !pCtl->aIfs[1].fPresent);
#endif

    /*
     * Reads on unpopulated IDE channels behave in a unique way. Newer ATA specifications
     * mandate that the host must have a pull-down resistor on signal DD7. As a consequence,
     * bit 7 is always read as zero. This greatly aids in ATA device detection because
     * the empty bus does not look to the host like a permanently busy drive, and no long
     * timeouts (on the order of 30 seconds) are needed.
     *
     * The response is entirely static and does not require any locking or other fancy
     * stuff. Breaking it out simplifies the I/O handling for non-empty IDE channels which
     * is quite complicated enough already.
     */
    *pu32 = ATA_EMPTY_BUS_DATA_32 >> ((4 - cb) * 8);
    LogFunc(("Empty bus: port %x val=%x size=%d\n", offPort, *pu32, cb));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 * Port I/O Handler for primary port range OUT operations.}
 * @note    offPort is an absolute port number!
 */
static DECLCALLBACK(VBOXSTRICTRC)
ataIOPortWrite1Other(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PATASTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    uintptr_t      iCtl = (uintptr_t)pvUser % RT_ELEMENTS(pThis->aCts);
    PATACONTROLLER pCtl = &pThis->aCts[iCtl];

    Assert((uintptr_t)pvUser < 2);

    VBOXSTRICTRC rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->lock, VINF_IOM_R3_IOPORT_WRITE);
    if (rc == VINF_SUCCESS)
    {
        /* Writes to the other command block ports should be 8-bit only. If they
         * are not, the high bits are simply discarded. Undocumented, but observed
         * on a real PIIX4 system.
         */
        if (cb > 1)
            Log(("ataIOPortWrite1: suspect write to port %x val=%x size=%d\n", offPort, u32, cb));

        rc = ataIOPortWriteU8(pDevIns, pCtl, offPort, u32, iCtl);

        PDMDevHlpCritSectLeave(pDevIns, &pCtl->lock);
    }
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,
 * Port I/O Handler for primary port range IN operations.}
 * @note    offPort is an absolute port number!
 */
static DECLCALLBACK(VBOXSTRICTRC)
ataIOPortRead1Other(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PATASTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATACONTROLLER pCtl = &RT_SAFE_SUBSCRIPT(pThis->aCts, (uintptr_t)pvUser);

    Assert((uintptr_t)pvUser < 2);

    VBOXSTRICTRC rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->lock, VINF_IOM_R3_IOPORT_READ);
    if (rc == VINF_SUCCESS)
    {
        /* Reads from the other command block registers should be 8-bit only.
         * If they are not, the low byte is propagated to the high bits.
         * Undocumented, but observed on a real PIIX4 system.
         */
        rc = ataIOPortReadU8(pDevIns, pCtl, offPort, pu32);
        if (cb > 1)
        {
            uint32_t    pad;

            /* Replicate the 8-bit result into the upper three bytes. */
            pad = *pu32 & 0xff;
            pad = pad | (pad << 8);
            pad = pad | (pad << 16);
            *pu32 = pad;
            Log(("ataIOPortRead1: suspect read from port %x size=%d\n", offPort, cb));
        }
        PDMDevHlpCritSectLeave(pDevIns, &pCtl->lock);
    }
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 * Port I/O Handler for secondary port range OUT operations.}
 * @note    offPort is an absolute port number!
 */
static DECLCALLBACK(VBOXSTRICTRC)
ataIOPortWrite2(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PATASTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATACONTROLLER pCtl = &RT_SAFE_SUBSCRIPT(pThis->aCts, (uintptr_t)pvUser);
    int rc;

    Assert((uintptr_t)pvUser < 2);

    if (cb == 1)
    {
        rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->lock, VINF_IOM_R3_IOPORT_WRITE);
        if (rc == VINF_SUCCESS)
        {
            rc = ataControlWrite(pDevIns, pCtl, u32, offPort);
            PDMDevHlpCritSectLeave(pDevIns, &pCtl->lock);
        }
    }
    else
    {
        Log(("ataIOPortWrite2: ignoring write to port %x+%x size=%d!\n", offPort, pCtl->IOPortBase2, cb));
        rc = VINF_SUCCESS;
    }
    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,
 * Port I/O Handler for secondary port range IN operations.}
 * @note    offPort is an absolute port number!
 */
static DECLCALLBACK(VBOXSTRICTRC)
ataIOPortRead2(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PATASTATE      pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATACONTROLLER pCtl = &RT_SAFE_SUBSCRIPT(pThis->aCts, (uintptr_t)pvUser);
    int            rc;

    Assert((uintptr_t)pvUser < 2);

    if (cb == 1)
    {
        rc = PDMDevHlpCritSectEnter(pDevIns, &pCtl->lock, VINF_IOM_R3_IOPORT_READ);
        if (rc == VINF_SUCCESS)
        {
            *pu32 = ataStatusRead(pCtl, offPort);
            PDMDevHlpCritSectLeave(pDevIns, &pCtl->lock);
        }
    }
    else
    {
        Log(("ataIOPortRead2: ignoring read from port %x+%x size=%d!\n", offPort, pCtl->IOPortBase2, cb));
        rc = VERR_IOM_IOPORT_UNUSED;
    }
    return rc;
}

#ifdef IN_RING3

/**
 * Detach notification.
 *
 * The DVD drive has been unplugged.
 *
 * @param   pDevIns     The device instance.
 * @param   iLUN        The logical unit which is being detached.
 * @param   fFlags      Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 */
static DECLCALLBACK(void) ataR3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PATASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PATASTATECC);
    AssertMsg(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
              ("PIIX3IDE: Device does not support hotplugging\n")); RT_NOREF(fFlags);

    /*
     * Locate the controller and stuff.
     */
    unsigned iController = iLUN / RT_ELEMENTS(pThis->aCts[0].aIfs);
    AssertReleaseMsg(iController < RT_ELEMENTS(pThis->aCts), ("iController=%d iLUN=%d\n", iController, iLUN));
    PATACONTROLLER   pCtl   = &pThis->aCts[iController];
    PATACONTROLLERR3 pCtlR3 = &pThisCC->aCts[iController];

    unsigned iInterface  = iLUN % RT_ELEMENTS(pThis->aCts[0].aIfs);
    PATADEVSTATE   pIf   = &pCtl->aIfs[iInterface];
    PATADEVSTATER3 pIfR3 = &pCtlR3->aIfs[iInterface];

    /*
     * Zero some important members.
     */
    pIfR3->pDrvBase = NULL;
    pIfR3->pDrvMedia = NULL;
    pIfR3->pDrvMount = NULL;
    pIf->fPresent    = false;

    /*
     * In case there was a medium inserted.
     */
    ataR3MediumRemoved(pIf);
}


/**
 * Configure a LUN.
 *
 * @returns VBox status code.
 * @param   pIf         The ATA unit state, shared bits.
 * @param   pIfR3       The ATA unit state, ring-3 bits.
 */
static int ataR3ConfigLun(PATADEVSTATE pIf, PATADEVSTATER3 pIfR3)
{
    /*
     * Query Block, Bios and Mount interfaces.
     */
    pIfR3->pDrvMedia = PDMIBASE_QUERY_INTERFACE(pIfR3->pDrvBase, PDMIMEDIA);
    if (!pIfR3->pDrvMedia)
    {
        AssertMsgFailed(("Configuration error: LUN#%d hasn't a block interface!\n", pIf->iLUN));
        return VERR_PDM_MISSING_INTERFACE;
    }

    pIfR3->pDrvMount = PDMIBASE_QUERY_INTERFACE(pIfR3->pDrvBase, PDMIMOUNT);
    pIf->fPresent = true;

    /*
     * Validate type.
     */
    PDMMEDIATYPE enmType = pIfR3->pDrvMedia->pfnGetType(pIfR3->pDrvMedia);
    if (    enmType != PDMMEDIATYPE_CDROM
        &&  enmType != PDMMEDIATYPE_DVD
        &&  enmType != PDMMEDIATYPE_HARD_DISK)
    {
        AssertMsgFailed(("Configuration error: LUN#%d isn't a disk or cd/dvd-rom. enmType=%d\n", pIf->iLUN, enmType));
        return VERR_PDM_UNSUPPORTED_BLOCK_TYPE;
    }
    if (    (   enmType == PDMMEDIATYPE_DVD
             || enmType == PDMMEDIATYPE_CDROM)
        &&  !pIfR3->pDrvMount)
    {
        AssertMsgFailed(("Internal error: cdrom without a mountable interface, WTF???!\n"));
        return VERR_INTERNAL_ERROR;
    }
    pIf->fATAPI = enmType == PDMMEDIATYPE_DVD || enmType == PDMMEDIATYPE_CDROM;
    pIf->fATAPIPassthrough = pIf->fATAPI && pIfR3->pDrvMedia->pfnSendCmd != NULL;

    /*
     * Allocate I/O buffer.
     */
    if (pIf->fATAPI)
        pIf->cbSector = 2048; /* Not required for ATAPI, one medium can have multiple sector sizes. */
    else
    {
        pIf->cbSector = pIfR3->pDrvMedia->pfnGetSectorSize(pIfR3->pDrvMedia);
        AssertLogRelMsgReturn(pIf->cbSector > 0 && pIf->cbSector <= ATA_MAX_SECTOR_SIZE,
                              ("Unsupported sector size on LUN#%u: %#x (%d)\n", pIf->iLUN, pIf->cbSector, pIf->cbSector),
                              VERR_OUT_OF_RANGE);
    }

    if (pIf->cbIOBuffer)
    {
        /* Buffer is (probably) already allocated. Validate the fields,
         * because memory corruption can also overwrite pIf->cbIOBuffer. */
        if (pIf->fATAPI)
            AssertLogRelReturn(pIf->cbIOBuffer == _128K, VERR_BUFFER_OVERFLOW);
        else
            AssertLogRelReturn(pIf->cbIOBuffer == ATA_MAX_MULT_SECTORS * pIf->cbSector, VERR_BUFFER_OVERFLOW);
    }
    else
    {
        if (pIf->fATAPI)
            pIf->cbIOBuffer = _128K;
        else
            pIf->cbIOBuffer = ATA_MAX_MULT_SECTORS * pIf->cbSector;
    }
    AssertCompile(_128K <= ATA_MAX_IO_BUFFER_SIZE);
    AssertCompileSize(pIf->abIOBuffer, ATA_MAX_IO_BUFFER_SIZE);
    AssertLogRelMsgReturn(pIf->cbIOBuffer <= ATA_MAX_IO_BUFFER_SIZE,
                          ("LUN#%u: cbIOBuffer=%#x (%u)\n", pIf->iLUN, pIf->cbIOBuffer, pIf->cbIOBuffer),
                          VERR_BUFFER_OVERFLOW);

    /*
     * Init geometry (only for non-CD/DVD media).
     */
    int rc = VINF_SUCCESS;
    uint32_t cRegions = pIfR3->pDrvMedia->pfnGetRegionCount(pIfR3->pDrvMedia);
    pIf->cTotalSectors = 0;
    for (uint32_t i = 0; i < cRegions; i++)
    {
        uint64_t cBlocks = 0;
        rc = pIfR3->pDrvMedia->pfnQueryRegionProperties(pIfR3->pDrvMedia, i, NULL, &cBlocks, NULL, NULL);
        AssertRC(rc);
        pIf->cTotalSectors += cBlocks;
    }

    if (pIf->fATAPI)
    {
        pIf->PCHSGeometry.cCylinders = 0; /* dummy */
        pIf->PCHSGeometry.cHeads     = 0; /* dummy */
        pIf->PCHSGeometry.cSectors   = 0; /* dummy */
        LogRel(("PIIX3 ATA: LUN#%d: CD/DVD, total number of sectors %Ld, passthrough %s\n",
                pIf->iLUN, pIf->cTotalSectors, (pIf->fATAPIPassthrough ? "enabled" : "disabled")));
    }
    else
    {
        rc = pIfR3->pDrvMedia->pfnBiosGetPCHSGeometry(pIfR3->pDrvMedia, &pIf->PCHSGeometry);
        if (rc == VERR_PDM_MEDIA_NOT_MOUNTED)
        {
            pIf->PCHSGeometry.cCylinders = 0;
            pIf->PCHSGeometry.cHeads     = 16; /*??*/
            pIf->PCHSGeometry.cSectors   = 63; /*??*/
        }
        else if (rc == VERR_PDM_GEOMETRY_NOT_SET)
        {
            pIf->PCHSGeometry.cCylinders = 0; /* autodetect marker */
            rc = VINF_SUCCESS;
        }
        AssertRC(rc);

        if (   pIf->PCHSGeometry.cCylinders == 0
            || pIf->PCHSGeometry.cHeads == 0
            || pIf->PCHSGeometry.cSectors == 0
           )
        {
            uint64_t cCylinders = pIf->cTotalSectors / (16 * 63);
            pIf->PCHSGeometry.cCylinders = RT_MAX(RT_MIN(cCylinders, 16383), 1);
            pIf->PCHSGeometry.cHeads = 16;
            pIf->PCHSGeometry.cSectors = 63;
            /* Set the disk geometry information. Ignore errors. */
            pIfR3->pDrvMedia->pfnBiosSetPCHSGeometry(pIfR3->pDrvMedia, &pIf->PCHSGeometry);
            rc = VINF_SUCCESS;
        }
        LogRel(("PIIX3 ATA: LUN#%d: disk, PCHS=%u/%u/%u, total number of sectors %Ld\n",
                pIf->iLUN, pIf->PCHSGeometry.cCylinders, pIf->PCHSGeometry.cHeads, pIf->PCHSGeometry.cSectors,
                pIf->cTotalSectors));

        if (pIfR3->pDrvMedia->pfnDiscard)
            LogRel(("PIIX3 ATA: LUN#%d: TRIM enabled\n", pIf->iLUN));
    }
    /* Initialize the translated geometry. */
    pIf->XCHSGeometry = pIf->PCHSGeometry;

    /*
     * Check if SMP system to adjust the agressiveness of the busy yield hack (@bugref{1960}).
     *
     * The hack is an ancient (2006?) one for dealing with UNI CPU systems where EMT
     * would potentially monopolise the CPU and starve I/O threads.  It causes the EMT to
     * yield it's timeslice if the guest polls the status register during I/O.  On modern
     * multicore and multithreaded systems, yielding EMT too often may have adverse
     * effects (slow grub) so we aim at avoiding repeating the yield there too often.
     */
    RTCPUID cCpus = RTMpGetOnlineCount();
    if (cCpus <= 1)
    {
        pIf->cBusyStatusHackR3Rate = 1;
        pIf->cBusyStatusHackRZRate = 7;
    }
    else if (cCpus <= 2)
    {
        pIf->cBusyStatusHackR3Rate = 3;
        pIf->cBusyStatusHackRZRate = 15;
    }
    else if (cCpus <= 4)
    {
        pIf->cBusyStatusHackR3Rate = 15;
        pIf->cBusyStatusHackRZRate = 31;
    }
    else
    {
        pIf->cBusyStatusHackR3Rate = 127;
        pIf->cBusyStatusHackRZRate = 127;
    }

    return rc;
}


/**
 * Attach command.
 *
 * This is called when we change block driver for the DVD drive.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   iLUN        The logical unit which is being detached.
 * @param   fFlags      Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 */
static DECLCALLBACK(int)  ataR3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PATASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PATASTATECC);

    AssertMsgReturn(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
                    ("PIIX3IDE: Device does not support hotplugging\n"),
                    VERR_INVALID_PARAMETER);

    /*
     * Locate the controller and stuff.
     */
    unsigned const iController = iLUN / RT_ELEMENTS(pThis->aCts[0].aIfs);
    AssertReleaseMsg(iController < RT_ELEMENTS(pThis->aCts), ("iController=%d iLUN=%d\n", iController, iLUN));
    PATACONTROLLER   pCtl   = &pThis->aCts[iController];
    PATACONTROLLERR3 pCtlR3 = &pThisCC->aCts[iController];

    unsigned const iInterface = iLUN % RT_ELEMENTS(pThis->aCts[0].aIfs);
    PATADEVSTATE   pIf   = &pCtl->aIfs[iInterface];
    PATADEVSTATER3 pIfR3 = &pCtlR3->aIfs[iInterface];

    /* the usual paranoia */
    AssertRelease(!pIfR3->pDrvBase);
    AssertRelease(!pIfR3->pDrvMedia);
    Assert(pIf->iLUN == iLUN);

    /*
     * Try attach the block device and get the interfaces,
     * required as well as optional.
     */
    int rc = PDMDevHlpDriverAttach(pDevIns, pIf->iLUN, &pIfR3->IBase, &pIfR3->pDrvBase, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = ataR3ConfigLun(pIf, pIfR3);
        /*
         * In case there is a medium inserted.
         */
        ataR3MediumInserted(pIf);
        ataR3MediumTypeSet(pIf, ATA_MEDIA_TYPE_UNKNOWN);
    }
    else
        AssertMsgFailed(("Failed to attach LUN#%d. rc=%Rrc\n", pIf->iLUN, rc));

    if (RT_FAILURE(rc))
    {
        pIfR3->pDrvBase  = NULL;
        pIfR3->pDrvMedia = NULL;
        pIfR3->pDrvMount = NULL;
        pIf->fPresent    = false;
    }
    return rc;
}


/**
 * Resume notification.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(void) ataR3Resume(PPDMDEVINS pDevIns)
{
    PATASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PATASTATECC);

    Log(("%s:\n", __FUNCTION__));
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        if (pThis->aCts[i].fRedo && pThis->aCts[i].fRedoIdle)
        {
            int rc = RTSemEventSignal(pThisCC->aCts[i].hSuspendIOSem);
            AssertRC(rc);
        }
    }
    return;
}


/**
 * Checks if all (both) the async I/O threads have quiesced.
 *
 * @returns true on success.
 * @returns false when one or more threads is still processing.
 * @param   pDevIns               Pointer to the PDM device instance.
 */
static bool ataR3AllAsyncIOIsIdle(PPDMDEVINS pDevIns)
{
    PATASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PATASTATECC);

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
        if (pThisCC->aCts[i].hAsyncIOThread != NIL_RTTHREAD)
        {
            bool fRc = ataR3AsyncIOIsIdle(pDevIns, &pThis->aCts[i], false /*fStrict*/);
            if (!fRc)
            {
                /* Make it signal PDM & itself when its done */
                int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->aCts[i].AsyncIORequestLock, VERR_IGNORED);
                PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->aCts[i].AsyncIORequestLock, rcLock);

                ASMAtomicWriteBool(&pThisCC->aCts[i].fSignalIdle, true);

                PDMDevHlpCritSectLeave(pDevIns, &pThis->aCts[i].AsyncIORequestLock);

                fRc = ataR3AsyncIOIsIdle(pDevIns, &pThis->aCts[i], false /*fStrict*/);
                if (!fRc)
                {
#if 0  /** @todo Need to do some time tracking here... */
                    LogRel(("PIIX3 ATA: Ctl#%u is still executing, DevSel=%d AIOIf=%d CmdIf0=%#04x CmdIf1=%#04x\n",
                            i, pThis->aCts[i].iSelectedIf, pThis->aCts[i].iAIOIf,
                            pThis->aCts[i].aIfs[0].uATARegCommand, pThis->aCts[i].aIfs[1].uATARegCommand));
#endif
                    return false;
                }
            }
            ASMAtomicWriteBool(&pThisCC->aCts[i].fSignalIdle, false);
        }
    return true;
}

/**
 * Prepare state save and load operation.
 *
 * @returns VBox status code.
 * @param   pDevIns         Device instance of the device which registered the data unit.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) ataR3SaveLoadPrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PATASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    RT_NOREF(pSSM);

    /* sanity - the suspend notification will wait on the async stuff. */
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
        AssertLogRelMsgReturn(ataR3AsyncIOIsIdle(pDevIns, &pThis->aCts[i], false /*fStrict*/),
                              ("i=%u\n", i),
                              VERR_SSM_IDE_ASYNC_TIMEOUT);
    return VINF_SUCCESS;
}

/**
 * @copydoc FNSSMDEVLIVEEXEC
 */
static DECLCALLBACK(int) ataR3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PATASTATE       pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATASTATER3     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PATASTATECC);
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;
    RT_NOREF(uPass);

    pHlp->pfnSSMPutU8(pSSM, (uint8_t)pThis->enmChipset);
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        pHlp->pfnSSMPutBool(pSSM, true);       /* For controller enabled / disabled. */
        for (uint32_t j = 0; j < RT_ELEMENTS(pThis->aCts[i].aIfs); j++)
        {
            pHlp->pfnSSMPutBool(pSSM, pThisCC->aCts[i].aIfs[j].pDrvBase != NULL);
            pHlp->pfnSSMPutStrZ(pSSM, pThis->aCts[i].aIfs[j].szSerialNumber);
            pHlp->pfnSSMPutStrZ(pSSM, pThis->aCts[i].aIfs[j].szFirmwareRevision);
            pHlp->pfnSSMPutStrZ(pSSM, pThis->aCts[i].aIfs[j].szModelNumber);
        }
    }

    return VINF_SSM_DONT_CALL_AGAIN;
}

/**
 * @copydoc FNSSMDEVSAVEEXEC
 */
static DECLCALLBACK(int) ataR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PATASTATE       pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;

    ataR3LiveExec(pDevIns, pSSM, SSM_PASS_FINAL);

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].iSelectedIf);
        pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].iAIOIf);
        pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].uAsyncIOState);
        pHlp->pfnSSMPutBool(pSSM, pThis->aCts[i].fChainedTransfer);
        pHlp->pfnSSMPutBool(pSSM, pThis->aCts[i].fReset);
        pHlp->pfnSSMPutBool(pSSM, pThis->aCts[i].fRedo);
        pHlp->pfnSSMPutBool(pSSM, pThis->aCts[i].fRedoIdle);
        pHlp->pfnSSMPutBool(pSSM, pThis->aCts[i].fRedoDMALastDesc);
        pHlp->pfnSSMPutMem(pSSM, &pThis->aCts[i].BmDma, sizeof(pThis->aCts[i].BmDma));
        pHlp->pfnSSMPutGCPhys32(pSSM, pThis->aCts[i].GCPhysFirstDMADesc);
        pHlp->pfnSSMPutGCPhys32(pSSM, pThis->aCts[i].GCPhysLastDMADesc);
        pHlp->pfnSSMPutGCPhys32(pSSM, pThis->aCts[i].GCPhysRedoDMABuffer);
        pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].cbRedoDMABuffer);

        for (uint32_t j = 0; j < RT_ELEMENTS(pThis->aCts[i].aIfs); j++)
        {
            pHlp->pfnSSMPutBool(pSSM, pThis->aCts[i].aIfs[j].fLBA48);
            pHlp->pfnSSMPutBool(pSSM, pThis->aCts[i].aIfs[j].fATAPI);
            pHlp->pfnSSMPutBool(pSSM, pThis->aCts[i].aIfs[j].fIrqPending);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].cMultSectors);
            pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].aIfs[j].XCHSGeometry.cCylinders);
            pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].aIfs[j].XCHSGeometry.cHeads);
            pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].aIfs[j].XCHSGeometry.cSectors);
            pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].aIfs[j].cSectorsPerIRQ);
            pHlp->pfnSSMPutU64(pSSM, pThis->aCts[i].aIfs[j].cTotalSectors);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegFeature);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegFeatureHOB);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegError);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegNSector);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegNSectorHOB);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegSector);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegSectorHOB);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegLCyl);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegLCylHOB);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegHCyl);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegHCylHOB);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegSelect);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegStatus);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegCommand);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATARegDevCtl);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uATATransferMode);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].uTxDir);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].iBeginTransfer);
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].iSourceSink);
            pHlp->pfnSSMPutBool(pSSM, pThis->aCts[i].aIfs[j].fDMA);
            pHlp->pfnSSMPutBool(pSSM, pThis->aCts[i].aIfs[j].fATAPITransfer);
            pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].aIfs[j].cbTotalTransfer);
            pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].aIfs[j].cbElementaryTransfer);
            pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].aIfs[j].iIOBufferCur);
            pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].aIfs[j].iIOBufferEnd);
            pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].aIfs[j].iIOBufferPIODataStart);
            pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].aIfs[j].iIOBufferPIODataEnd);
            pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].aIfs[j].iCurLBA);
            pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].aIfs[j].cbATAPISector);
            pHlp->pfnSSMPutMem(pSSM, &pThis->aCts[i].aIfs[j].abATAPICmd, sizeof(pThis->aCts[i].aIfs[j].abATAPICmd));
            pHlp->pfnSSMPutMem(pSSM, &pThis->aCts[i].aIfs[j].abATAPISense, sizeof(pThis->aCts[i].aIfs[j].abATAPISense));
            pHlp->pfnSSMPutU8(pSSM, pThis->aCts[i].aIfs[j].cNotifiedMediaChange);
            pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].aIfs[j].MediaEventStatus);
            pHlp->pfnSSMPutMem(pSSM, &pThis->aCts[i].aIfs[j].Led, sizeof(pThis->aCts[i].aIfs[j].Led));
            pHlp->pfnSSMPutU32(pSSM, pThis->aCts[i].aIfs[j].cbIOBuffer);
            if (pThis->aCts[i].aIfs[j].cbIOBuffer)
                pHlp->pfnSSMPutMem(pSSM, pThis->aCts[i].aIfs[j].abIOBuffer, pThis->aCts[i].aIfs[j].cbIOBuffer);
        }
    }

    return pHlp->pfnSSMPutU32(pSSM, UINT32_MAX); /* sanity/terminator */
}

/**
 * Converts the LUN number into a message string.
 */
static const char *ataR3StringifyLun(unsigned iLun)
{
    switch (iLun)
    {
        case 0:  return "primary master";
        case 1:  return "primary slave";
        case 2:  return "secondary master";
        case 3:  return "secondary slave";
        default: AssertFailedReturn("unknown lun");
    }
}

/**
 * FNSSMDEVLOADEXEC
 */
static DECLCALLBACK(int) ataR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PATASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATASTATER3     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PATASTATECC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;
    uint32_t        u32;

    if (   uVersion != ATA_SAVED_STATE_VERSION
        && uVersion != ATA_SAVED_STATE_VERSION_WITHOUT_ATA_ILBA
        && uVersion != ATA_SAVED_STATE_VERSION_VBOX_30
        && uVersion != ATA_SAVED_STATE_VERSION_WITHOUT_FULL_SENSE
        && uVersion != ATA_SAVED_STATE_VERSION_WITHOUT_EVENT_STATUS
        && uVersion != ATA_SAVED_STATE_VERSION_WITH_BOOL_TYPE)
    {
        AssertMsgFailed(("uVersion=%d\n", uVersion));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    /*
     * Verify the configuration.
     */
    if (uVersion > ATA_SAVED_STATE_VERSION_VBOX_30)
    {
        uint8_t u8Type;
        rc = pHlp->pfnSSMGetU8(pSSM, &u8Type);
        AssertRCReturn(rc, rc);
        if ((CHIPSET)u8Type != pThis->enmChipset)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: enmChipset - saved=%u config=%u"), u8Type, pThis->enmChipset);

        for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
        {
            bool fEnabled;
            rc = pHlp->pfnSSMGetBool(pSSM, &fEnabled);
            AssertRCReturn(rc, rc);
            if (!fEnabled)
                return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Ctr#%u onfig mismatch: fEnabled != true"), i);

            for (uint32_t j = 0; j < RT_ELEMENTS(pThis->aCts[i].aIfs); j++)
            {
                ATADEVSTATE const   *pIf   = &pThis->aCts[i].aIfs[j];
                ATADEVSTATER3 const *pIfR3 = &pThisCC->aCts[i].aIfs[j];

                bool fInUse;
                rc = pHlp->pfnSSMGetBool(pSSM, &fInUse);
                AssertRCReturn(rc, rc);
                if (fInUse != (pIfR3->pDrvBase != NULL))
                    return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                                   N_("The %s VM is missing a %s device. Please make sure the source and target VMs have compatible storage configurations"),
                                                   fInUse ? "target" : "source", ataR3StringifyLun(pIf->iLUN) );

                char szSerialNumber[ATA_SERIAL_NUMBER_LENGTH+1];
                rc = pHlp->pfnSSMGetStrZ(pSSM, szSerialNumber,     sizeof(szSerialNumber));
                AssertRCReturn(rc, rc);
                if (strcmp(szSerialNumber, pIf->szSerialNumber))
                    LogRel(("PIIX3 ATA: LUN#%u config mismatch: Serial number - saved='%s' config='%s'\n",
                            pIf->iLUN, szSerialNumber, pIf->szSerialNumber));

                char szFirmwareRevision[ATA_FIRMWARE_REVISION_LENGTH+1];
                rc = pHlp->pfnSSMGetStrZ(pSSM, szFirmwareRevision, sizeof(szFirmwareRevision));
                AssertRCReturn(rc, rc);
                if (strcmp(szFirmwareRevision, pIf->szFirmwareRevision))
                    LogRel(("PIIX3 ATA: LUN#%u config mismatch: Firmware revision - saved='%s' config='%s'\n",
                            pIf->iLUN, szFirmwareRevision, pIf->szFirmwareRevision));

                char szModelNumber[ATA_MODEL_NUMBER_LENGTH+1];
                rc = pHlp->pfnSSMGetStrZ(pSSM, szModelNumber,      sizeof(szModelNumber));
                AssertRCReturn(rc, rc);
                if (strcmp(szModelNumber, pIf->szModelNumber))
                    LogRel(("PIIX3 ATA: LUN#%u config mismatch: Model number - saved='%s' config='%s'\n",
                            pIf->iLUN, szModelNumber, pIf->szModelNumber));
            }
        }
    }
    if (uPass != SSM_PASS_FINAL)
        return VINF_SUCCESS;

    /*
     * Restore valid parts of the ATASTATE structure
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        /* integrity check */
        if (!ataR3AsyncIOIsIdle(pDevIns, &pThis->aCts[i], false))
        {
            AssertMsgFailed(("Async I/O for controller %d is active\n", i));
            return VERR_INTERNAL_ERROR_4;
        }

        rc = pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].iSelectedIf);
        AssertRCReturn(rc, rc);
        AssertLogRelMsgStmt(pThis->aCts[i].iSelectedIf == (pThis->aCts[i].iSelectedIf & ATA_SELECTED_IF_MASK),
                            ("iSelectedIf = %d\n", pThis->aCts[i].iSelectedIf),
                            pThis->aCts[i].iSelectedIf &= ATA_SELECTED_IF_MASK);
        rc = pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].iAIOIf);
        AssertRCReturn(rc, rc);
        AssertLogRelMsgStmt(pThis->aCts[i].iAIOIf == (pThis->aCts[i].iAIOIf & ATA_SELECTED_IF_MASK),
                            ("iAIOIf = %d\n", pThis->aCts[i].iAIOIf),
                            pThis->aCts[i].iAIOIf &= ATA_SELECTED_IF_MASK);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].uAsyncIOState);
        pHlp->pfnSSMGetBool(pSSM, &pThis->aCts[i].fChainedTransfer);
        pHlp->pfnSSMGetBool(pSSM, &pThis->aCts[i].fReset);
        pHlp->pfnSSMGetBool(pSSM, &pThis->aCts[i].fRedo);
        pHlp->pfnSSMGetBool(pSSM, &pThis->aCts[i].fRedoIdle);
        pHlp->pfnSSMGetBool(pSSM, &pThis->aCts[i].fRedoDMALastDesc);
        pHlp->pfnSSMGetMem(pSSM, &pThis->aCts[i].BmDma, sizeof(pThis->aCts[i].BmDma));
        pHlp->pfnSSMGetGCPhys32(pSSM, &pThis->aCts[i].GCPhysFirstDMADesc);
        pHlp->pfnSSMGetGCPhys32(pSSM, &pThis->aCts[i].GCPhysLastDMADesc);
        pHlp->pfnSSMGetGCPhys32(pSSM, &pThis->aCts[i].GCPhysRedoDMABuffer);
        pHlp->pfnSSMGetU32(pSSM, &pThis->aCts[i].cbRedoDMABuffer);

        for (uint32_t j = 0; j < RT_ELEMENTS(pThis->aCts[i].aIfs); j++)
        {
            pHlp->pfnSSMGetBool(pSSM, &pThis->aCts[i].aIfs[j].fLBA48);
            pHlp->pfnSSMGetBool(pSSM, &pThis->aCts[i].aIfs[j].fATAPI);
            pHlp->pfnSSMGetBool(pSSM, &pThis->aCts[i].aIfs[j].fIrqPending);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].cMultSectors);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aCts[i].aIfs[j].XCHSGeometry.cCylinders);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aCts[i].aIfs[j].XCHSGeometry.cHeads);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aCts[i].aIfs[j].XCHSGeometry.cSectors);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aCts[i].aIfs[j].cSectorsPerIRQ);
            pHlp->pfnSSMGetU64(pSSM, &pThis->aCts[i].aIfs[j].cTotalSectors);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegFeature);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegFeatureHOB);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegError);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegNSector);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegNSectorHOB);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegSector);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegSectorHOB);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegLCyl);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegLCylHOB);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegHCyl);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegHCylHOB);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegSelect);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegStatus);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegCommand);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATARegDevCtl);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uATATransferMode);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].uTxDir);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].iBeginTransfer);
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].iSourceSink);
            pHlp->pfnSSMGetBool(pSSM, &pThis->aCts[i].aIfs[j].fDMA);
            pHlp->pfnSSMGetBool(pSSM, &pThis->aCts[i].aIfs[j].fATAPITransfer);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aCts[i].aIfs[j].cbTotalTransfer);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aCts[i].aIfs[j].cbElementaryTransfer);
            /* NB: cbPIOTransferLimit could be saved/restored but it's sufficient
             * to re-calculate it here, with a tiny risk that it could be
             * unnecessarily low for the current transfer only. Could be changed
             * when changing the saved state in the future.
             */
            pThis->aCts[i].aIfs[j].cbPIOTransferLimit = (pThis->aCts[i].aIfs[j].uATARegHCyl << 8) | pThis->aCts[i].aIfs[j].uATARegLCyl;
            pHlp->pfnSSMGetU32(pSSM, &pThis->aCts[i].aIfs[j].iIOBufferCur);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aCts[i].aIfs[j].iIOBufferEnd);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aCts[i].aIfs[j].iIOBufferPIODataStart);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aCts[i].aIfs[j].iIOBufferPIODataEnd);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aCts[i].aIfs[j].iCurLBA);
            pHlp->pfnSSMGetU32(pSSM, &pThis->aCts[i].aIfs[j].cbATAPISector);
            pHlp->pfnSSMGetMem(pSSM, &pThis->aCts[i].aIfs[j].abATAPICmd, sizeof(pThis->aCts[i].aIfs[j].abATAPICmd));
            if (uVersion > ATA_SAVED_STATE_VERSION_WITHOUT_FULL_SENSE)
                pHlp->pfnSSMGetMem(pSSM, pThis->aCts[i].aIfs[j].abATAPISense, sizeof(pThis->aCts[i].aIfs[j].abATAPISense));
            else
            {
                uint8_t uATAPISenseKey, uATAPIASC;
                memset(pThis->aCts[i].aIfs[j].abATAPISense, '\0', sizeof(pThis->aCts[i].aIfs[j].abATAPISense));
                pThis->aCts[i].aIfs[j].abATAPISense[0] = 0x70 | (1 << 7);
                pThis->aCts[i].aIfs[j].abATAPISense[7] = 10;
                pHlp->pfnSSMGetU8(pSSM, &uATAPISenseKey);
                pHlp->pfnSSMGetU8(pSSM, &uATAPIASC);
                pThis->aCts[i].aIfs[j].abATAPISense[2] = uATAPISenseKey & 0x0f;
                pThis->aCts[i].aIfs[j].abATAPISense[12] = uATAPIASC;
            }
            /** @todo triple-check this hack after passthrough is working */
            pHlp->pfnSSMGetU8(pSSM, &pThis->aCts[i].aIfs[j].cNotifiedMediaChange);
            if (uVersion > ATA_SAVED_STATE_VERSION_WITHOUT_EVENT_STATUS)
                pHlp->pfnSSMGetU32V(pSSM, &pThis->aCts[i].aIfs[j].MediaEventStatus);
            else
                pThis->aCts[i].aIfs[j].MediaEventStatus = ATA_EVENT_STATUS_UNCHANGED;
            pHlp->pfnSSMGetMem(pSSM, &pThis->aCts[i].aIfs[j].Led, sizeof(pThis->aCts[i].aIfs[j].Led));

            uint32_t cbIOBuffer = 0;
            rc = pHlp->pfnSSMGetU32(pSSM, &cbIOBuffer);
            AssertRCReturn(rc, rc);

            if (   (uVersion <= ATA_SAVED_STATE_VERSION_WITHOUT_ATA_ILBA)
                && !pThis->aCts[i].aIfs[j].fATAPI)
            {
                pThis->aCts[i].aIfs[j].iCurLBA = ataR3GetSector(&pThis->aCts[i].aIfs[j]);
            }

            if (cbIOBuffer)
            {
                if (cbIOBuffer <= sizeof(pThis->aCts[i].aIfs[j].abIOBuffer))
                {
                    if (pThis->aCts[i].aIfs[j].cbIOBuffer != cbIOBuffer)
                        LogRel(("ATA: %u/%u: Restoring cbIOBuffer=%u; constructor set up %u!\n", i, j, cbIOBuffer, pThis->aCts[i].aIfs[j].cbIOBuffer));
                    pThis->aCts[i].aIfs[j].cbIOBuffer = cbIOBuffer;
                    pHlp->pfnSSMGetMem(pSSM, pThis->aCts[i].aIfs[j].abIOBuffer, cbIOBuffer);
                }
                else
                {
                    LogRel(("ATA: %u/%u: Restoring cbIOBuffer=%u, only prepared %u!\n", i, j, cbIOBuffer, pThis->aCts[i].aIfs[j].cbIOBuffer));
                    if (pHlp->pfnSSMHandleGetAfter(pSSM) != SSMAFTER_DEBUG_IT)
                        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                                       N_("ATA: %u/%u: Restoring cbIOBuffer=%u, only prepared %u"),
                                                       i, j, cbIOBuffer, pThis->aCts[i].aIfs[j].cbIOBuffer);

                    /* skip the buffer if we're loading for the debugger / animator. */
                    pHlp->pfnSSMSkip(pSSM, cbIOBuffer);
                }
            }
            else
                AssertLogRelMsgStmt(pThis->aCts[i].aIfs[j].cbIOBuffer == 0,
                                    ("ATA: %u/%u: cbIOBuffer=%u restoring zero!\n", i, j, pThis->aCts[i].aIfs[j].cbIOBuffer),
                                    pThis->aCts[i].aIfs[j].cbIOBuffer = 0);
        }
    }
    if (uVersion <= ATA_SAVED_STATE_VERSION_VBOX_30)
        PDMDEVHLP_SSM_GET_ENUM8_RET(pHlp, pSSM, pThis->enmChipset, CHIPSET);

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
 * Callback employed by ataSuspend and ataR3PowerOff.
 *
 * @returns true if we've quiesced, false if we're still working.
 * @param   pDevIns     The device instance.
 */
static DECLCALLBACK(bool) ataR3IsAsyncSuspendOrPowerOffDone(PPDMDEVINS pDevIns)
{
    return ataR3AllAsyncIOIsIdle(pDevIns);
}


/**
 * Common worker for ataSuspend and ataR3PowerOff.
 */
static void ataR3SuspendOrPowerOff(PPDMDEVINS pDevIns)
{
    if (!ataR3AllAsyncIOIsIdle(pDevIns))
        PDMDevHlpSetAsyncNotification(pDevIns, ataR3IsAsyncSuspendOrPowerOffDone);
}


/**
 * Power Off notification.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(void) ataR3PowerOff(PPDMDEVINS pDevIns)
{
    Log(("%s:\n", __FUNCTION__));
    ataR3SuspendOrPowerOff(pDevIns);
}


/**
 * Suspend notification.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(void) ataR3Suspend(PPDMDEVINS pDevIns)
{
    Log(("%s:\n", __FUNCTION__));
    ataR3SuspendOrPowerOff(pDevIns);
}


/**
 * Callback employed by ataR3Reset.
 *
 * @returns true if we've quiesced, false if we're still working.
 * @param   pDevIns     The device instance.
 */
static DECLCALLBACK(bool) ataR3IsAsyncResetDone(PPDMDEVINS pDevIns)
{
    PATASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);

    if (!ataR3AllAsyncIOIsIdle(pDevIns))
        return false;

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->aCts[i].lock, VERR_INTERNAL_ERROR);
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->aCts[i].lock, rcLock);

        for (uint32_t j = 0; j < RT_ELEMENTS(pThis->aCts[i].aIfs); j++)
            ataR3ResetDevice(pDevIns, &pThis->aCts[i], &pThis->aCts[i].aIfs[j]);

        PDMDevHlpCritSectLeave(pDevIns, &pThis->aCts[i].lock);
    }
    return true;
}


/**
 * Common reset worker for ataR3Reset and ataR3Construct.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance data.
 * @param   fConstruct  Indicates who is calling.
 */
static int ataR3ResetCommon(PPDMDEVINS pDevIns, bool fConstruct)
{
    PATASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PATASTATECC);

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->aCts[i].lock, VERR_INTERNAL_ERROR);
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->aCts[i].lock, rcLock);

        pThis->aCts[i].iSelectedIf = 0;
        pThis->aCts[i].iAIOIf = 0;
        pThis->aCts[i].BmDma.u8Cmd = 0;
        /* Report that both drives present on the bus are in DMA mode. This
         * pretends that there is a BIOS that has set it up. Normal reset
         * default is 0x00. */
        pThis->aCts[i].BmDma.u8Status = (pThisCC->aCts[i].aIfs[0].pDrvBase != NULL ? BM_STATUS_D0DMA : 0)
                                      | (pThisCC->aCts[i].aIfs[1].pDrvBase != NULL ? BM_STATUS_D1DMA : 0);
        pThis->aCts[i].BmDma.GCPhysAddr = 0;

        pThis->aCts[i].fReset = true;
        pThis->aCts[i].fRedo = false;
        pThis->aCts[i].fRedoIdle = false;
        ataR3AsyncIOClearRequests(pDevIns, &pThis->aCts[i]);
        Log2(("%s: Ctl#%d: message to async I/O thread, reset controller\n", __FUNCTION__, i));
        ataHCAsyncIOPutRequest(pDevIns, &pThis->aCts[i], &g_ataResetARequest);
        ataHCAsyncIOPutRequest(pDevIns, &pThis->aCts[i], &g_ataResetCRequest);

        PDMDevHlpCritSectLeave(pDevIns, &pThis->aCts[i].lock);
    }

    int rcRet = VINF_SUCCESS;
    if (!fConstruct)
    {
        /*
         * Setup asynchronous notification completion if the requests haven't
         * completed yet.
         */
        if (!ataR3IsAsyncResetDone(pDevIns))
            PDMDevHlpSetAsyncNotification(pDevIns, ataR3IsAsyncResetDone);
    }
    else
    {
        /*
         * Wait for the requests for complete.
         *
         * Would be real nice if we could do it all from EMT(0) and not
         * involve the worker threads, then we could dispense with all the
         * waiting and semaphore ping-pong here...
         */
        for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
        {
            if (pThisCC->aCts[i].hAsyncIOThread != NIL_RTTHREAD)
            {
                int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->aCts[i].AsyncIORequestLock, VERR_IGNORED);
                PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->aCts[i].AsyncIORequestLock, rc);

                ASMAtomicWriteBool(&pThisCC->aCts[i].fSignalIdle, true);
                rc = RTThreadUserReset(pThisCC->aCts[i].hAsyncIOThread);
                AssertRC(rc);

                rc = PDMDevHlpCritSectLeave(pDevIns, &pThis->aCts[i].AsyncIORequestLock);
                AssertRC(rc);

                if (!ataR3AsyncIOIsIdle(pDevIns, &pThis->aCts[i], false /*fStrict*/))
                {
                    rc = RTThreadUserWait(pThisCC->aCts[i].hAsyncIOThread,  30*1000 /*ms*/);
                    if (RT_FAILURE(rc))
                        rc = RTThreadUserWait(pThisCC->aCts[i].hAsyncIOThread, 1000 /*ms*/);
                    if (RT_FAILURE(rc))
                    {
                        AssertRC(rc);
                        rcRet = rc;
                    }
                }
            }
            ASMAtomicWriteBool(&pThisCC->aCts[i].fSignalIdle, false);
        }
        if (RT_SUCCESS(rcRet))
        {
            rcRet = ataR3IsAsyncResetDone(pDevIns) ? VINF_SUCCESS : VERR_INTERNAL_ERROR;
            AssertRC(rcRet);
        }
    }
    return rcRet;
}

/**
 * Reset notification.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(void)  ataR3Reset(PPDMDEVINS pDevIns)
{
    ataR3ResetCommon(pDevIns, false /*fConstruct*/);
}

/**
 * Destroy a driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(int) ataR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PATASTATE       pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATASTATER3     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PATASTATECC);
    int             rc;

    Log(("ataR3Destruct\n"));

    /*
     * Tell the async I/O threads to terminate.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        if (pThisCC->aCts[i].hAsyncIOThread != NIL_RTTHREAD)
        {
            ASMAtomicWriteU32(&pThisCC->aCts[i].fShutdown, true);
            rc = PDMDevHlpSUPSemEventSignal(pDevIns, pThis->aCts[i].hAsyncIOSem);
            AssertRC(rc);
            rc = RTSemEventSignal(pThisCC->aCts[i].hSuspendIOSem);
            AssertRC(rc);
        }
    }

    /*
     * Wait for the threads to terminate before destroying their resources.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        if (pThisCC->aCts[i].hAsyncIOThread != NIL_RTTHREAD)
        {
            rc = RTThreadWait(pThisCC->aCts[i].hAsyncIOThread, 30000 /* 30 s*/, NULL);
            if (RT_SUCCESS(rc))
                pThisCC->aCts[i].hAsyncIOThread = NIL_RTTHREAD;
            else
                LogRel(("PIIX3 ATA Dtor: Ctl#%u is still executing, DevSel=%d AIOIf=%d CmdIf0=%#04x CmdIf1=%#04x rc=%Rrc\n",
                        i, pThis->aCts[i].iSelectedIf, pThis->aCts[i].iAIOIf,
                        pThis->aCts[i].aIfs[0].uATARegCommand, pThis->aCts[i].aIfs[1].uATARegCommand, rc));
        }
    }

    /*
     * Free resources.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        if (PDMDevHlpCritSectIsInitialized(pDevIns, &pThis->aCts[i].AsyncIORequestLock))
            PDMDevHlpCritSectDelete(pDevIns, &pThis->aCts[i].AsyncIORequestLock);
        if (pThis->aCts[i].hAsyncIOSem != NIL_SUPSEMEVENT)
        {
            PDMDevHlpSUPSemEventClose(pDevIns, pThis->aCts[i].hAsyncIOSem);
            pThis->aCts[i].hAsyncIOSem = NIL_SUPSEMEVENT;
        }
        if (pThisCC->aCts[i].hSuspendIOSem != NIL_RTSEMEVENT)
        {
            RTSemEventDestroy(pThisCC->aCts[i].hSuspendIOSem);
            pThisCC->aCts[i].hSuspendIOSem = NIL_RTSEMEVENT;
        }

        /* try one final time */
        if (pThisCC->aCts[i].hAsyncIOThread != NIL_RTTHREAD)
        {
            rc = RTThreadWait(pThisCC->aCts[i].hAsyncIOThread, 1 /*ms*/, NULL);
            if (RT_SUCCESS(rc))
            {
                pThisCC->aCts[i].hAsyncIOThread = NIL_RTTHREAD;
                LogRel(("PIIX3 ATA Dtor: Ctl#%u actually completed.\n", i));
            }
        }

        for (uint32_t iIf = 0; iIf < RT_ELEMENTS(pThis->aCts[i].aIfs); iIf++)
        {
            if (pThisCC->aCts[i].aIfs[iIf].pTrackList)
            {
                ATAPIPassthroughTrackListDestroy(pThisCC->aCts[i].aIfs[iIf].pTrackList);
                pThisCC->aCts[i].aIfs[iIf].pTrackList = NULL;
            }
        }
    }

    return VINF_SUCCESS;
}

/**
 * Convert config value to DEVPCBIOSBOOT.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance data.
 * @param   pCfg        Configuration handle.
 * @param   penmChipset Where to store the chipset type.
 */
static int ataR3ControllerFromCfg(PPDMDEVINS pDevIns, PCFGMNODE pCfg, CHIPSET *penmChipset)
{
    char szType[20];

    int rc = pDevIns->pHlpR3->pfnCFGMQueryStringDef(pCfg, "Type", &szType[0], sizeof(szType), "PIIX4");
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Querying \"Type\" as a string failed"));
    if (!strcmp(szType, "PIIX3"))
        *penmChipset = CHIPSET_PIIX3;
    else if (!strcmp(szType, "PIIX4"))
        *penmChipset = CHIPSET_PIIX4;
    else if (!strcmp(szType, "ICH6"))
        *penmChipset = CHIPSET_ICH6;
    else
    {
        PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                            N_("Configuration error: The \"Type\" value \"%s\" is unknown"),
                            szType);
        rc = VERR_INTERNAL_ERROR;
    }
    return rc;
}

/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) ataR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PATASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PATASTATE);
    PATASTATER3     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PATASTATER3);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    PPDMIBASE       pBase;
    int             rc;
    uint32_t        msDelayIRQ;

    Assert(iInstance == 0);

    /*
     * Initialize NIL handle values (for the destructor).
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        pThis->aCts[i].iCtl = i;
        pThis->aCts[i].hAsyncIOSem = NIL_SUPSEMEVENT;
        pThis->aCts[i].hIoPorts1First = NIL_IOMIOPORTHANDLE;
        pThis->aCts[i].hIoPorts1Other = NIL_IOMIOPORTHANDLE;
        pThis->aCts[i].hIoPorts2 = NIL_IOMIOPORTHANDLE;
        pThis->aCts[i].hIoPortsEmpty1 = NIL_IOMIOPORTHANDLE;
        pThis->aCts[i].hIoPortsEmpty2 = NIL_IOMIOPORTHANDLE;

        pThisCC->aCts[i].iCtl = i;
        pThisCC->aCts[i].hSuspendIOSem = NIL_RTSEMEVENT;
        pThisCC->aCts[i].hAsyncIOThread = NIL_RTTHREAD;
    }

    /*
     * Validate and read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "IRQDelay|Type",  "PrimaryMaster|PrimarySlave|SecondaryMaster|SecondarySlave");

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "IRQDelay", &msDelayIRQ, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("PIIX3 configuration error: failed to read IRQDelay as integer"));
    Log(("%s: msDelayIRQ=%d\n", __FUNCTION__, msDelayIRQ));
    Assert(msDelayIRQ < 50);

    CHIPSET enmChipset = CHIPSET_PIIX3;
    rc = ataR3ControllerFromCfg(pDevIns, pCfg, &enmChipset);
    if (RT_FAILURE(rc))
        return rc;
    pThis->enmChipset = enmChipset;

    /*
     * Initialize data (most of it anyway).
     */
    /* Status LUN. */
    pThisCC->IBase.pfnQueryInterface = ataR3Status_QueryInterface;
    pThisCC->ILeds.pfnQueryStatusLed = ataR3Status_QueryStatusLed;

    /* PCI configuration space. */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);
    PDMPciDevSetVendorId(pPciDev, 0x8086); /* Intel */

    /*
     * When adding more IDE chipsets, don't forget to update pci_bios_init_device()
     * as it explicitly checks for PCI id for IDE controllers.
     */
    switch (enmChipset)
    {
        case CHIPSET_ICH6:
            PDMPciDevSetDeviceId(pPciDev, 0x269e); /* ICH6 IDE */
            /** @todo do we need it? Do we need anything else? */
            PDMPciDevSetByte(pPciDev, 0x48, 0x00); /* UDMACTL */
            PDMPciDevSetByte(pPciDev, 0x4A, 0x00); /* UDMATIM */
            PDMPciDevSetByte(pPciDev, 0x4B, 0x00);
            {
                /*
                 * See www.intel.com/Assets/PDF/manual/298600.pdf p. 30
                 * Report
                 *   WR_Ping-Pong_EN: must be set
                 *   PCR0, PCR1: 80-pin primary cable reporting for both disks
                 *   SCR0, SCR1: 80-pin secondary cable reporting for both disks
                 */
                uint16_t u16Config = (1<<10) | (1<<7)  | (1<<6) | (1<<5) | (1<<4);
                PDMPciDevSetByte(pPciDev, 0x54, u16Config & 0xff);
                PDMPciDevSetByte(pPciDev, 0x55, u16Config >> 8);
            }
            break;
        case CHIPSET_PIIX4:
            PDMPciDevSetDeviceId(pPciDev, 0x7111); /* PIIX4 IDE */
            PDMPciDevSetRevisionId(pPciDev, 0x01); /* PIIX4E */
            PDMPciDevSetByte(pPciDev, 0x48, 0x00); /* UDMACTL */
            PDMPciDevSetByte(pPciDev, 0x4A, 0x00); /* UDMATIM */
            PDMPciDevSetByte(pPciDev, 0x4B, 0x00);
            break;
        case CHIPSET_PIIX3:
            PDMPciDevSetDeviceId(pPciDev, 0x7010); /* PIIX3 IDE */
            break;
        default:
            AssertMsgFailed(("Unsupported IDE chipset type: %d\n", enmChipset));
    }

    /** @todo
     * This is the job of the BIOS / EFI!
     *
     * The same is done in DevPCI.cpp / pci_bios_init_device() but there is no
     * corresponding function in DevPciIch9.cpp. The EFI has corresponding code
     * in OvmfPkg/Library/PlatformBdsLib/BdsPlatform.c: NotifyDev() but this
     * function assumes that the IDE controller is located at PCI 00:01.1 which
     * is not true if the ICH9 chipset is used.
     */
    PDMPciDevSetWord(pPciDev, 0x40, 0x8000); /* enable IDE0 */
    PDMPciDevSetWord(pPciDev, 0x42, 0x8000); /* enable IDE1 */

    PDMPciDevSetCommand(   pPciDev, PCI_COMMAND_IOACCESS | PCI_COMMAND_MEMACCESS | PCI_COMMAND_BUSMASTER);
    PDMPciDevSetClassProg( pPciDev, 0x8a); /* programming interface = PCI_IDE bus-master is supported */
    PDMPciDevSetClassSub(  pPciDev, 0x01); /* class_sub = PCI_IDE */
    PDMPciDevSetClassBase( pPciDev, 0x01); /* class_base = PCI_mass_storage */
    PDMPciDevSetHeaderType(pPciDev, 0x00);

    pThisCC->pDevIns        = pDevIns;
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        pThisCC->aCts[i].pDevIns    = pDevIns;
        pThisCC->aCts[i].iCtl       = i;
        pThis->aCts[i].iCtl         = i;
        pThis->aCts[i].msDelayIRQ   = msDelayIRQ;
        for (uint32_t j = 0; j < RT_ELEMENTS(pThis->aCts[i].aIfs); j++)
        {
            PATADEVSTATE   pIf   = &pThis->aCts[i].aIfs[j];
            PATADEVSTATER3 pIfR3 = &pThisCC->aCts[i].aIfs[j];

            pIfR3->iLUN                             = pIf->iLUN = i * RT_ELEMENTS(pThis->aCts) + j;
            pIfR3->iCtl                             = pIf->iCtl = i;
            pIfR3->iDev                             = pIf->iDev = j;
            pIfR3->pDevIns                          = pDevIns;
            pIfR3->IBase.pfnQueryInterface          = ataR3QueryInterface;
            pIfR3->IMountNotify.pfnMountNotify      = ataR3MountNotify;
            pIfR3->IMountNotify.pfnUnmountNotify    = ataR3UnmountNotify;
            pIfR3->IPort.pfnQueryDeviceLocation     = ataR3QueryDeviceLocation;
            pIf->Led.u32Magic                       = PDMLED_MAGIC;
        }
    }

    Assert(RT_ELEMENTS(pThis->aCts) == 2);
    pThis->aCts[0].irq          = 14;
    pThis->aCts[0].IOPortBase1  = 0x1f0;
    pThis->aCts[0].IOPortBase2  = 0x3f6;
    pThis->aCts[1].irq          = 15;
    pThis->aCts[1].IOPortBase1  = 0x170;
    pThis->aCts[1].IOPortBase2  = 0x376;

    /*
     * Set the default critical section to NOP as we lock on controller level.
     */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Register the PCI device.
     */
    rc = PDMDevHlpPCIRegisterEx(pDevIns, pPciDev, PDMPCIDEVREG_F_NOT_MANDATORY_NO, 1 /*uPciDevNo*/, 1 /*uPciDevFn*/, "piix3ide");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("PIIX3 cannot register PCI device"));

    /* Region #4: I/O ports for the two bus-master DMA controllers. */
    rc = PDMDevHlpPCIIORegionCreateIo(pDevIns, 4 /*iPciRegion*/, 0x10 /*cPorts*/,
                                      ataBMDMAIOPortWrite, ataBMDMAIOPortRead, NULL /*pvUser*/, "ATA Bus Master DMA",
                                      NULL /*paExtDescs*/, &pThis->hIoPortsBmDma);
    AssertRCReturn(rc, rc);

    /*
     * Register stats, create critical sections.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        for (uint32_t j = 0; j < RT_ELEMENTS(pThis->aCts[i].aIfs); j++)
        {
            PATADEVSTATE pIf = &pThis->aCts[i].aIfs[j];
            PDMDevHlpSTAMRegisterF(pDevIns, &pIf->StatATADMA,       STAMTYPE_COUNTER,    STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                                   "Number of ATA DMA transfers.",              "/Devices/IDE%d/ATA%d/Unit%d/DMA", iInstance, i, j);
            PDMDevHlpSTAMRegisterF(pDevIns, &pIf->StatATAPIO,       STAMTYPE_COUNTER,    STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                                   "Number of ATA PIO transfers.",              "/Devices/IDE%d/ATA%d/Unit%d/PIO", iInstance, i, j);
            PDMDevHlpSTAMRegisterF(pDevIns, &pIf->StatATAPIDMA,     STAMTYPE_COUNTER,    STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                                   "Number of ATAPI DMA transfers.",            "/Devices/IDE%d/ATA%d/Unit%d/AtapiDMA", iInstance, i, j);
            PDMDevHlpSTAMRegisterF(pDevIns, &pIf->StatATAPIPIO,     STAMTYPE_COUNTER,    STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                                   "Number of ATAPI PIO transfers.",            "/Devices/IDE%d/ATA%d/Unit%d/AtapiPIO", iInstance, i, j);
#ifdef VBOX_WITH_STATISTICS /** @todo release too. */
            PDMDevHlpSTAMRegisterF(pDevIns, &pIf->StatReads,        STAMTYPE_PROFILE_ADV, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                                   "Profiling of the read operations.",         "/Devices/IDE%d/ATA%d/Unit%d/Reads", iInstance, i, j);
#endif
            PDMDevHlpSTAMRegisterF(pDevIns, &pIf->StatBytesRead,    STAMTYPE_COUNTER,     STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                                   "Amount of data read.",                      "/Devices/IDE%d/ATA%d/Unit%d/ReadBytes", iInstance, i, j);
#ifdef VBOX_INSTRUMENT_DMA_WRITES
            PDMDevHlpSTAMRegisterF(pDevIns, &pIf->StatInstrVDWrites,STAMTYPE_PROFILE_ADV, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                                   "Profiling of the VD DMA write operations.", "/Devices/IDE%d/ATA%d/Unit%d/InstrVDWrites", iInstance, i, j);
#endif
#ifdef VBOX_WITH_STATISTICS
            PDMDevHlpSTAMRegisterF(pDevIns, &pIf->StatWrites,       STAMTYPE_PROFILE_ADV, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                                   "Profiling of the write operations.",        "/Devices/IDE%d/ATA%d/Unit%d/Writes", iInstance, i, j);
#endif
            PDMDevHlpSTAMRegisterF(pDevIns, &pIf->StatBytesWritten, STAMTYPE_COUNTER,     STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                                   "Amount of data written.",                   "/Devices/IDE%d/ATA%d/Unit%d/WrittenBytes", iInstance, i, j);
#ifdef VBOX_WITH_STATISTICS
            PDMDevHlpSTAMRegisterF(pDevIns, &pIf->StatFlushes,      STAMTYPE_PROFILE,     STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                                   "Profiling of the flush operations.",        "/Devices/IDE%d/ATA%d/Unit%d/Flushes", iInstance, i, j);
#endif
            PDMDevHlpSTAMRegisterF(pDevIns, &pIf->StatStatusYields, STAMTYPE_PROFILE,     STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                                   "Profiling of status polling yields.",        "/Devices/IDE%d/ATA%d/Unit%d/StatusYields", iInstance, i, j);
        }
#ifdef VBOX_WITH_STATISTICS /** @todo release too. */
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aCts[i].StatAsyncOps,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                                   "The number of async operations.",   "/Devices/IDE%d/ATA%d/Async/Operations", iInstance, i);
        /** @todo STAMUNIT_MICROSECS */
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aCts[i].StatAsyncMinWait, STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                                   "Minimum wait in microseconds.",     "/Devices/IDE%d/ATA%d/Async/MinWait", iInstance, i);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aCts[i].StatAsyncMaxWait, STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                                   "Maximum wait in microseconds.",     "/Devices/IDE%d/ATA%d/Async/MaxWait", iInstance, i);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aCts[i].StatAsyncTimeUS,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                                   "Total time spent in microseconds.", "/Devices/IDE%d/ATA%d/Async/TotalTimeUS", iInstance, i);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aCts[i].StatAsyncTime,    STAMTYPE_PROFILE_ADV, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                                   "Profiling of async operations.",    "/Devices/IDE%d/ATA%d/Async/Time", iInstance, i);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aCts[i].StatLockWait,     STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                                   "Profiling of locks.",               "/Devices/IDE%d/ATA%d/Async/LockWait", iInstance, i);
#endif /* VBOX_WITH_STATISTICS */

        /* Initialize per-controller critical section. */
        rc = PDMDevHlpCritSectInit(pDevIns, &pThis->aCts[i].lock,               RT_SRC_POS, "ATA#%u-Ctl", i);
        AssertLogRelRCReturn(rc, rc);

        /* Initialize per-controller async I/O request critical section. */
        rc = PDMDevHlpCritSectInit(pDevIns, &pThis->aCts[i].AsyncIORequestLock, RT_SRC_POS, "ATA#%u-Req", i);
        AssertLogRelRCReturn(rc, rc);
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
    else if (rc != VERR_PDM_NO_ATTACHED_DRIVER)
    {
        AssertMsgFailed(("Failed to attach to status driver. rc=%Rrc\n", rc));
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("PIIX3 cannot attach to status driver"));
    }

    /*
     * Attach the units.
     */
    uint32_t cbTotalBuffer = 0;
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        PATACONTROLLER   pCtl   = &pThis->aCts[i];
        PATACONTROLLERR3 pCtlR3 = &pThisCC->aCts[i];

        /*
         * Start the worker thread.
         */
        pCtl->uAsyncIOState = ATA_AIO_NEW;
        rc = PDMDevHlpSUPSemEventCreate(pDevIns, &pCtl->hAsyncIOSem);
        AssertLogRelRCReturn(rc, rc);
        rc = RTSemEventCreate(&pCtlR3->hSuspendIOSem);
        AssertLogRelRCReturn(rc, rc);

        ataR3AsyncIOClearRequests(pDevIns, pCtl);
        rc = RTThreadCreateF(&pCtlR3->hAsyncIOThread, ataR3AsyncIOThread, pCtlR3, 0,
                             RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, "ATA-%u", i);
        AssertLogRelRCReturn(rc, rc);
        Assert(   pCtlR3->hAsyncIOThread != NIL_RTTHREAD && pCtl->hAsyncIOSem != NIL_SUPSEMEVENT
               && pCtlR3->hSuspendIOSem  != NIL_RTSEMEVENT && PDMDevHlpCritSectIsInitialized(pDevIns, &pCtl->AsyncIORequestLock));
        Log(("%s: controller %d AIO thread id %#x; sem %p susp_sem %p\n", __FUNCTION__, i, pCtlR3->hAsyncIOThread, pCtl->hAsyncIOSem, pCtlR3->hSuspendIOSem));

        for (uint32_t j = 0; j < RT_ELEMENTS(pCtl->aIfs); j++)
        {
            static const char *s_apszDescs[RT_ELEMENTS(pThis->aCts)][RT_ELEMENTS(pCtl->aIfs)] =
            {
                { "Primary Master", "Primary Slave" },
                { "Secondary Master", "Secondary Slave" }
            };

            /*
             * Try attach the block device and get the interfaces,
             * required as well as optional.
             */
            PATADEVSTATE   pIf   = &pCtl->aIfs[j];
            PATADEVSTATER3 pIfR3 = &pCtlR3->aIfs[j];

            rc = PDMDevHlpDriverAttach(pDevIns, pIf->iLUN, &pIfR3->IBase, &pIfR3->pDrvBase, s_apszDescs[i][j]);
            if (RT_SUCCESS(rc))
            {
                rc = ataR3ConfigLun(pIf, pIfR3);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Init vendor product data.
                     */
                    static const char *s_apszCFGMKeys[RT_ELEMENTS(pThis->aCts)][RT_ELEMENTS(pCtl->aIfs)] =
                    {
                        { "PrimaryMaster", "PrimarySlave" },
                        { "SecondaryMaster", "SecondarySlave" }
                    };

                    /* Generate a default serial number. */
                    char szSerial[ATA_SERIAL_NUMBER_LENGTH+1];
                    RTUUID Uuid;
                    if (pIfR3->pDrvMedia)
                        rc = pIfR3->pDrvMedia->pfnGetUuid(pIfR3->pDrvMedia, &Uuid);
                    else
                        RTUuidClear(&Uuid);

                    if (RT_FAILURE(rc) || RTUuidIsNull(&Uuid))
                    {
                        /* Generate a predictable serial for drives which don't have a UUID. */
                        RTStrPrintf(szSerial, sizeof(szSerial), "VB%x-%04x%04x",
                                    pIf->iLUN + pDevIns->iInstance * 32,
                                    pThis->aCts[i].IOPortBase1, pThis->aCts[i].IOPortBase2);
                    }
                    else
                        RTStrPrintf(szSerial, sizeof(szSerial), "VB%08x-%08x", Uuid.au32[0], Uuid.au32[3]);

                    /* Get user config if present using defaults otherwise. */
                    PCFGMNODE pCfgNode = pHlp->pfnCFGMGetChild(pCfg, s_apszCFGMKeys[i][j]);
                    rc = pHlp->pfnCFGMQueryStringDef(pCfgNode, "SerialNumber", pIf->szSerialNumber, sizeof(pIf->szSerialNumber),
                                                     szSerial);
                    if (RT_FAILURE(rc))
                    {
                        if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
                            return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                                    N_("PIIX3 configuration error: \"SerialNumber\" is longer than 20 bytes"));
                        return PDMDEV_SET_ERROR(pDevIns, rc,
                                                N_("PIIX3 configuration error: failed to read \"SerialNumber\" as string"));
                    }

                    rc = pHlp->pfnCFGMQueryStringDef(pCfgNode, "FirmwareRevision", pIf->szFirmwareRevision,
                                                     sizeof(pIf->szFirmwareRevision), "1.0");
                    if (RT_FAILURE(rc))
                    {
                        if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
                            return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                                    N_("PIIX3 configuration error: \"FirmwareRevision\" is longer than 8 bytes"));
                        return PDMDEV_SET_ERROR(pDevIns, rc,
                                                N_("PIIX3 configuration error: failed to read \"FirmwareRevision\" as string"));
                    }

                    rc = pHlp->pfnCFGMQueryStringDef(pCfgNode, "ModelNumber", pIf->szModelNumber, sizeof(pIf->szModelNumber),
                                                     pIf->fATAPI ? "VBOX CD-ROM" : "VBOX HARDDISK");
                    if (RT_FAILURE(rc))
                    {
                        if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
                            return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                                    N_("PIIX3 configuration error: \"ModelNumber\" is longer than 40 bytes"));
                        return PDMDEV_SET_ERROR(pDevIns, rc,
                                                N_("PIIX3 configuration error: failed to read \"ModelNumber\" as string"));
                    }

                    /* There are three other identification strings for CD drives used for INQUIRY */
                    if (pIf->fATAPI)
                    {
                        rc = pHlp->pfnCFGMQueryStringDef(pCfgNode, "ATAPIVendorId", pIf->szInquiryVendorId,
                                                         sizeof(pIf->szInquiryVendorId), "VBOX");
                        if (RT_FAILURE(rc))
                        {
                            if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
                                return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                                        N_("PIIX3 configuration error: \"ATAPIVendorId\" is longer than 16 bytes"));
                            return PDMDEV_SET_ERROR(pDevIns, rc,
                                                    N_("PIIX3 configuration error: failed to read \"ATAPIVendorId\" as string"));
                        }

                        rc = pHlp->pfnCFGMQueryStringDef(pCfgNode, "ATAPIProductId", pIf->szInquiryProductId,
                                                         sizeof(pIf->szInquiryProductId), "CD-ROM");
                        if (RT_FAILURE(rc))
                        {
                            if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
                                return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                                        N_("PIIX3 configuration error: \"ATAPIProductId\" is longer than 16 bytes"));
                            return PDMDEV_SET_ERROR(pDevIns, rc,
                                                    N_("PIIX3 configuration error: failed to read \"ATAPIProductId\" as string"));
                        }

                        rc = pHlp->pfnCFGMQueryStringDef(pCfgNode, "ATAPIRevision", pIf->szInquiryRevision,
                                                         sizeof(pIf->szInquiryRevision), "1.0");
                        if (RT_FAILURE(rc))
                        {
                            if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
                                return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                                        N_("PIIX3 configuration error: \"ATAPIRevision\" is longer than 4 bytes"));
                            return PDMDEV_SET_ERROR(pDevIns, rc,
                                                    N_("PIIX3 configuration error: failed to read \"ATAPIRevision\" as string"));
                        }

                        rc = pHlp->pfnCFGMQueryBoolDef(pCfgNode, "OverwriteInquiry", &pIf->fOverwriteInquiry, true);
                        if (RT_FAILURE(rc))
                            return PDMDEV_SET_ERROR(pDevIns, rc,
                                                    N_("PIIX3 configuration error: failed to read \"OverwriteInquiry\" as boolean"));
                    }
                }
            }
            else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
            {
                pIfR3->pDrvBase  = NULL;
                pIfR3->pDrvMedia = NULL;
                pIf->cbIOBuffer  = 0;
                pIf->fPresent    = false;
                LogRel(("PIIX3 ATA: LUN#%d: no unit\n", pIf->iLUN));
            }
            else
            {
                switch (rc)
                {
                    case VERR_ACCESS_DENIED:
                        /* Error already cached by DrvHostBase */
                        return rc;
                    default:
                        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                                   N_("PIIX3 cannot attach drive to the %s"),
                                                   s_apszDescs[i][j]);
                }
            }
            cbTotalBuffer += pIf->cbIOBuffer;
        }
    }

    /*
     * Register the I/O ports.
     * The ports are all hardcoded and enforced by the PIIX3 host bridge controller.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        Assert(pThis->aCts[i].aIfs[0].fPresent == (pThisCC->aCts[i].aIfs[0].pDrvMedia != NULL));
        Assert(pThis->aCts[i].aIfs[1].fPresent == (pThisCC->aCts[i].aIfs[1].pDrvMedia != NULL));

        if (!pThisCC->aCts[i].aIfs[0].pDrvMedia && !pThisCC->aCts[i].aIfs[1].pDrvMedia)
        {
            /* No device present on this ATA bus; requires special handling. */
            rc = PDMDevHlpIoPortCreateExAndMap(pDevIns, pThis->aCts[i].IOPortBase1, 8 /*cPorts*/, IOM_IOPORT_F_ABS,
                                               ataIOPortWriteEmptyBus, ataIOPortReadEmptyBus, NULL, NULL, (RTHCPTR)(uintptr_t)i,
                                               "ATA I/O Base 1 - Empty Bus", NULL /*paExtDescs*/, &pThis->aCts[i].hIoPortsEmpty1);
            AssertLogRelRCReturn(rc, rc);
            rc = PDMDevHlpIoPortCreateExAndMap(pDevIns, pThis->aCts[i].IOPortBase2, 1 /*cPorts*/, IOM_IOPORT_F_ABS,
                                               ataIOPortWriteEmptyBus, ataIOPortReadEmptyBus, NULL, NULL, (RTHCPTR)(uintptr_t)i,
                                                "ATA I/O Base 2 - Empty Bus", NULL /*paExtDescs*/, &pThis->aCts[i].hIoPortsEmpty2);
            AssertLogRelRCReturn(rc, rc);
        }
        else
        {
            /* At least one device present, register regular handlers. */
            rc = PDMDevHlpIoPortCreateExAndMap(pDevIns, pThis->aCts[i].IOPortBase1, 1 /*cPorts*/, IOM_IOPORT_F_ABS,
                                               ataIOPortWrite1Data, ataIOPortRead1Data,
                                               ataIOPortWriteStr1Data, ataIOPortReadStr1Data, (RTHCPTR)(uintptr_t)i,
                                               "ATA I/O Base 1 - Data", NULL /*paExtDescs*/, &pThis->aCts[i].hIoPorts1First);
            AssertLogRelRCReturn(rc, rc);
            rc = PDMDevHlpIoPortCreateExAndMap(pDevIns, pThis->aCts[i].IOPortBase1 + 1, 7 /*cPorts*/, IOM_IOPORT_F_ABS,
                                               ataIOPortWrite1Other, ataIOPortRead1Other, NULL, NULL, (RTHCPTR)(uintptr_t)i,
                                               "ATA I/O Base 1 - Other", NULL /*paExtDescs*/, &pThis->aCts[i].hIoPorts1Other);
            AssertLogRelRCReturn(rc, rc);


            rc = PDMDevHlpIoPortCreateExAndMap(pDevIns, pThis->aCts[i].IOPortBase2, 1 /*cPorts*/, IOM_IOPORT_F_ABS,
                                               ataIOPortWrite2, ataIOPortRead2, NULL, NULL, (RTHCPTR)(uintptr_t)i,
                                               "ATA I/O Base 2", NULL /*paExtDescs*/, &pThis->aCts[i].hIoPorts2);
            AssertLogRelRCReturn(rc, rc);
        }
    }

    rc = PDMDevHlpSSMRegisterEx(pDevIns, ATA_SAVED_STATE_VERSION, sizeof(*pThis) + cbTotalBuffer, NULL,
                                NULL,              ataR3LiveExec, NULL,
                                ataR3SaveLoadPrep, ataR3SaveExec, NULL,
                                ataR3SaveLoadPrep, ataR3LoadExec, NULL);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("PIIX3 cannot register save state handlers"));

    /*
     * Initialize the device state.
     */
    return ataR3ResetCommon(pDevIns, true /*fConstruct*/);
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) ataRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PATASTATE pThis = PDMDEVINS_2_DATA(pDevIns, PATASTATE);

    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsBmDma, ataBMDMAIOPortWrite, ataBMDMAIOPortRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aCts); i++)
    {
        if (pThis->aCts[i].hIoPorts1First != NIL_IOMIOPORTHANDLE)
        {
            rc = PDMDevHlpIoPortSetUpContextEx(pDevIns, pThis->aCts[i].hIoPorts1First,
                                               ataIOPortWrite1Data, ataIOPortRead1Data,
                                               ataIOPortWriteStr1Data, ataIOPortReadStr1Data, (RTHCPTR)(uintptr_t)i);
            AssertLogRelRCReturn(rc, rc);
            rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->aCts[i].hIoPorts1Other,
                                             ataIOPortWrite1Other, ataIOPortRead1Other, (RTHCPTR)(uintptr_t)i);
            AssertLogRelRCReturn(rc, rc);
            rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->aCts[i].hIoPorts2,
                                             ataIOPortWrite2, ataIOPortRead2, (RTHCPTR)(uintptr_t)i);
            AssertLogRelRCReturn(rc, rc);
        }
        else
        {
            rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->aCts[i].hIoPortsEmpty1,
                                             ataIOPortWriteEmptyBus, ataIOPortReadEmptyBus, (void *)(uintptr_t)i /*pvUser*/);
            AssertRCReturn(rc, rc);

            rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->aCts[i].hIoPortsEmpty2,
                                             ataIOPortWriteEmptyBus, ataIOPortReadEmptyBus, (void *)(uintptr_t)i /*pvUser*/);
            AssertRCReturn(rc, rc);
        }
    }

    return VINF_SUCCESS;
}


#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DevicePIIX3IDE =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "piix3ide",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE
                                    | PDM_DEVREG_FLAGS_FIRST_SUSPEND_NOTIFICATION | PDM_DEVREG_FLAGS_FIRST_POWEROFF_NOTIFICATION
                                    | PDM_DEVREG_FLAGS_FIRST_RESET_NOTIFICATION,
    /* .fClass = */                 PDM_DEVREG_CLASS_STORAGE,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(ATASTATE),
    /* .cbInstanceCC = */           sizeof(ATASTATECC),
    /* .cbInstanceRC = */           sizeof(ATASTATERC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Intel PIIX3 ATA controller.\n"
                                    "  LUN #0 is primary master.\n"
                                    "  LUN #1 is primary slave.\n"
                                    "  LUN #2 is secondary master.\n"
                                    "  LUN #3 is secondary slave.\n"
                                    "  LUN #999 is the LED/Status connector.",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           ataR3Construct,
    /* .pfnDestruct = */            ataR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               ataR3Reset,
    /* .pfnSuspend = */             ataR3Suspend,
    /* .pfnResume = */              ataR3Resume,
    /* .pfnAttach = */              ataR3Attach,
    /* .pfnDetach = */              ataR3Detach,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            ataR3PowerOff,
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
    /* .pfnConstruct = */           ataRZConstruct,
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
    /* .pfnConstruct = */           ataRZConstruct,
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
