/* $Id: DevBusLogic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices - BusLogic SCSI host adapter BT-958.
 *
 * Based on the Multi-Master Ultra SCSI Systems Technical Reference Manual.
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
#define LOG_GROUP LOG_GROUP_DEV_BUSLOGIC
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmstorageifs.h>
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/AssertGuest.h>
#include <VBox/scsi.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/log.h>
#ifdef IN_RING3
# include <iprt/alloc.h>
# include <iprt/memcache.h>
# include <iprt/param.h>
# include <iprt/uuid.h>
#endif

#include "VBoxSCSI.h"
#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Maximum number of attached devices the adapter can handle. */
#define BUSLOGIC_MAX_DEVICES 16

/** Maximum number of scatter gather elements this device can handle. */
#define BUSLOGIC_MAX_SCATTER_GATHER_LIST_SIZE 128

/** Size of the command buffer. */
#define BUSLOGIC_COMMAND_SIZE_MAX   53

/** Size of the reply buffer. */
#define BUSLOGIC_REPLY_SIZE_MAX     64

/** Custom fixed I/O ports for BIOS controller access.
 * Note that these should not be in the ISA range (below 400h) to avoid
 * conflicts with ISA device probing. Addresses in the 300h-340h range should be
 * especially avoided.
 */
#define BUSLOGIC_BIOS_IO_PORT   0x430

/** State saved version. */
#define BUSLOGIC_SAVED_STATE_MINOR_VERSION               5
/** Saved state version before VBoxSCSI got removed. */
#define BUSLOGIC_SAVED_STATE_MINOR_PRE_VBOXSCSI_REMOVAL  4
/** Saved state version before command buffer size was raised. */
#define BUSLOGIC_SAVED_STATE_MINOR_PRE_CMDBUF_RESIZE     3
/** Saved state version before 24-bit mailbox support was implemented. */
#define BUSLOGIC_SAVED_STATE_MINOR_PRE_24BIT_MBOX        2
/** Saved state version before the suspend on error feature was implemented. */
#define BUSLOGIC_SAVED_STATE_MINOR_PRE_ERROR_HANDLING    1

/** Command buffer size in old saved states. */
#define BUSLOGIC_COMMAND_SIZE_OLD 5

/** The duration of software-initiated reset (in nano seconds).
 *  Not documented, set to 50 ms. */
#define BUSLOGIC_RESET_DURATION_NS      UINT64_C(50000000)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * State of a device attached to the buslogic host adapter.
 *
 * @implements  PDMIBASE
 * @implements  PDMISCSIPORT
 * @implements  PDMILEDPORTS
 */
typedef struct BUSLOGICDEVICE
{
    /** The ring-3 device instance (for getting our bearings when arriving in an
     * interface method). */
    PPDMDEVINSR3                    pDevIns;

    /** LUN of the device. */
    uint32_t                        iLUN;

    /** Flag whether device is present.
     * @note This is mirrored in BUSLOGIC::afDevicePresent. */
    bool                            fPresent;
    bool                            afAlignment[3];

    /** Our base interface. */
    PDMIBASE                        IBase;
    /** Media port interface. */
    PDMIMEDIAPORT                   IMediaPort;
    /** Extended media port interface. */
    PDMIMEDIAEXPORT                 IMediaExPort;
    /** Led interface. */
    PDMILEDPORTS                    ILed;
    /** Pointer to the attached driver's base interface. */
    R3PTRTYPE(PPDMIBASE)            pDrvBase;
    /** Pointer to the attached driver's media interface. */
    R3PTRTYPE(PPDMIMEDIA)           pDrvMedia;
    /** Pointer to the attached driver's extended media interface. */
    R3PTRTYPE(PPDMIMEDIAEX)         pDrvMediaEx;
    /** The status LED state for this device. */
    PDMLED                          Led;

    /** Number of outstanding tasks on the port. */
    volatile uint32_t               cOutstandingRequests;
    /** The device name. */
    char                            szName[12];
} BUSLOGICDEVICE, *PBUSLOGICDEVICE;

/**
 * Commands the BusLogic adapter supports.
 */
enum BUSLOGICCOMMAND
{
    BUSLOGICCOMMAND_TEST_CMDC_INTERRUPT = 0x00,
    BUSLOGICCOMMAND_INITIALIZE_MAILBOX = 0x01,
    BUSLOGICCOMMAND_EXECUTE_MAILBOX_COMMAND = 0x02,
    BUSLOGICCOMMAND_EXECUTE_BIOS_COMMAND = 0x03,
    BUSLOGICCOMMAND_INQUIRE_BOARD_ID = 0x04,
    BUSLOGICCOMMAND_ENABLE_OUTGOING_MAILBOX_AVAILABLE_INTERRUPT = 0x05,
    BUSLOGICCOMMAND_SET_SCSI_SELECTION_TIMEOUT = 0x06,
    BUSLOGICCOMMAND_SET_PREEMPT_TIME_ON_BUS = 0x07,
    BUSLOGICCOMMAND_SET_TIME_OFF_BUS = 0x08,
    BUSLOGICCOMMAND_SET_BUS_TRANSFER_RATE = 0x09,
    BUSLOGICCOMMAND_INQUIRE_INSTALLED_DEVICES_ID_0_TO_7 = 0x0a,
    BUSLOGICCOMMAND_INQUIRE_CONFIGURATION = 0x0b,
    BUSLOGICCOMMAND_ENABLE_TARGET_MODE = 0x0c,
    BUSLOGICCOMMAND_INQUIRE_SETUP_INFORMATION = 0x0d,
    BUSLOGICCOMMAND_WRITE_ADAPTER_LOCAL_RAM = 0x1a,
    BUSLOGICCOMMAND_READ_ADAPTER_LOCAL_RAM = 0x1b,
    BUSLOGICCOMMAND_WRITE_BUSMASTER_CHIP_FIFO = 0x1c,
    BUSLOGICCOMMAND_READ_BUSMASTER_CHIP_FIFO = 0x1d,
    BUSLOGICCOMMAND_ECHO_COMMAND_DATA = 0x1f,
    BUSLOGICCOMMAND_HOST_ADAPTER_DIAGNOSTIC = 0x20,
    BUSLOGICCOMMAND_SET_ADAPTER_OPTIONS = 0x21,
    BUSLOGICCOMMAND_INQUIRE_INSTALLED_DEVICES_ID_8_TO_15 = 0x23,
    BUSLOGICCOMMAND_INQUIRE_TARGET_DEVICES = 0x24,
    BUSLOGICCOMMAND_DISABLE_HOST_ADAPTER_INTERRUPT = 0x25,
    BUSLOGICCOMMAND_EXT_BIOS_INFO = 0x28,
    BUSLOGICCOMMAND_UNLOCK_MAILBOX = 0x29,
    BUSLOGICCOMMAND_INITIALIZE_EXTENDED_MAILBOX = 0x81,
    BUSLOGICCOMMAND_EXECUTE_SCSI_COMMAND = 0x83,
    BUSLOGICCOMMAND_INQUIRE_FIRMWARE_VERSION_3RD_LETTER = 0x84,
    BUSLOGICCOMMAND_INQUIRE_FIRMWARE_VERSION_LETTER = 0x85,
    BUSLOGICCOMMAND_INQUIRE_PCI_HOST_ADAPTER_INFORMATION = 0x86,
    BUSLOGICCOMMAND_INQUIRE_HOST_ADAPTER_MODEL_NUMBER = 0x8b,
    BUSLOGICCOMMAND_INQUIRE_SYNCHRONOUS_PERIOD = 0x8c,
    BUSLOGICCOMMAND_INQUIRE_EXTENDED_SETUP_INFORMATION = 0x8d,
    BUSLOGICCOMMAND_ENABLE_STRICT_ROUND_ROBIN_MODE = 0x8f,
    BUSLOGICCOMMAND_STORE_HOST_ADAPTER_LOCAL_RAM = 0x90,
    BUSLOGICCOMMAND_FETCH_HOST_ADAPTER_LOCAL_RAM = 0x91,
    BUSLOGICCOMMAND_STORE_LOCAL_DATA_IN_EEPROM = 0x92,
    BUSLOGICCOMMAND_UPLOAD_AUTO_SCSI_CODE = 0x94,
    BUSLOGICCOMMAND_MODIFY_IO_ADDRESS = 0x95,
    BUSLOGICCOMMAND_SET_CCB_FORMAT = 0x96,
    BUSLOGICCOMMAND_WRITE_INQUIRY_BUFFER = 0x9a,
    BUSLOGICCOMMAND_READ_INQUIRY_BUFFER = 0x9b,
    BUSLOGICCOMMAND_FLASH_ROM_UPLOAD_DOWNLOAD = 0xa7,
    BUSLOGICCOMMAND_READ_SCAM_DATA = 0xa8,
    BUSLOGICCOMMAND_WRITE_SCAM_DATA = 0xa9
} BUSLOGICCOMMAND;

#pragma pack(1)
/**
 * Auto SCSI structure which is located
 * in host adapter RAM and contains several
 * configuration parameters.
 */
typedef struct AutoSCSIRam
{
    uint8_t       aInternalSignature[2];
    uint8_t       cbInformation;
    uint8_t       aHostAdaptertype[6];
    uint8_t       uReserved1;
    bool          fFloppyEnabled :                  1;
    bool          fFloppySecondary :                1;
    bool          fLevelSensitiveInterrupt :        1;
    unsigned char uReserved2 :                      2;
    unsigned char uSystemRAMAreForBIOS :            3;
    unsigned char uDMAChannel :                     7;
    bool          fDMAAutoConfiguration :           1;
    unsigned char uIrqChannel :                     7;
    bool          fIrqAutoConfiguration :           1;
    uint8_t       uDMATransferRate;
    uint8_t       uSCSIId;
    bool          fLowByteTerminated :              1;
    bool          fParityCheckingEnabled :          1;
    bool          fHighByteTerminated :             1;
    bool          fNoisyCablingEnvironment :        1;
    bool          fFastSynchronousNeogtiation :     1;
    bool          fBusResetEnabled :                1;
    bool          fReserved3 :                      1;
    bool          fActiveNegotiationEnabled :       1;
    uint8_t       uBusOnDelay;
    uint8_t       uBusOffDelay;
    bool          fHostAdapterBIOSEnabled :         1;
    bool          fBIOSRedirectionOfInt19 :         1;
    bool          fExtendedTranslation :            1;
    bool          fMapRemovableAsFixed :            1;
    bool          fReserved4 :                      1;
    bool          fBIOSSupportsMoreThan2Drives :    1;
    bool          fBIOSInterruptMode :              1;
    bool          fFlopticalSupport :               1;
    uint16_t      u16DeviceEnabledMask;
    uint16_t      u16WidePermittedMask;
    uint16_t      u16FastPermittedMask;
    uint16_t      u16SynchronousPermittedMask;
    uint16_t      u16DisconnectPermittedMask;
    uint16_t      u16SendStartUnitCommandMask;
    uint16_t      u16IgnoreInBIOSScanMask;
    unsigned char uPCIInterruptPin :                2;
    unsigned char uHostAdapterIoPortAddress :       2;
    bool          fStrictRoundRobinMode :           1;
    bool          fVesaBusSpeedGreaterThan33MHz :   1;
    bool          fVesaBurstWrite :                 1;
    bool          fVesaBurstRead :                  1;
    uint16_t      u16UltraPermittedMask;
    uint32_t      uReserved5;
    uint8_t       uReserved6;
    uint8_t       uAutoSCSIMaximumLUN;
    bool          fReserved7 :                      1;
    bool          fSCAMDominant :                   1;
    bool          fSCAMenabled :                    1;
    bool          fSCAMLevel2 :                     1;
    unsigned char uReserved8 :                      4;
    bool          fInt13Extension :                 1;
    bool          fReserved9 :                      1;
    bool          fCDROMBoot :                      1;
    unsigned char uReserved10 :                     5;
    unsigned char uBootTargetId :                   4;
    unsigned char uBootChannel :                    4;
    bool          fForceBusDeviceScanningOrder :    1;
    unsigned char uReserved11 :                     7;
    uint16_t      u16NonTaggedToAlternateLunPermittedMask;
    uint16_t      u16RenegotiateSyncAfterCheckConditionMask;
    uint8_t       aReserved12[10];
    uint8_t       aManufacturingDiagnostic[2];
    uint16_t      u16Checksum;
} AutoSCSIRam, *PAutoSCSIRam;
AssertCompileSize(AutoSCSIRam, 64);
#pragma pack()

/**
 * The local Ram.
 */
typedef union HostAdapterLocalRam
{
    /** Byte view. */
    uint8_t u8View[256];
    /** Structured view. */
    struct
    {
        /** Offset 0 - 63 is for BIOS. */
        uint8_t     u8Bios[64];
        /** Auto SCSI structure. */
        AutoSCSIRam autoSCSIData;
    } structured;
} HostAdapterLocalRam, *PHostAdapterLocalRam;
AssertCompileSize(HostAdapterLocalRam, 256);


/** Ugly 24-bit big-endian addressing. */
typedef struct
{
    uint8_t hi;
    uint8_t mid;
    uint8_t lo;
} Addr24, Len24;
AssertCompileSize(Addr24, 3);

#define ADDR_TO_U32(x)      (((x).hi << 16) | ((x).mid << 8) | (x).lo)
#define LEN_TO_U32          ADDR_TO_U32
#define U32_TO_ADDR(a, x)   do {(a).hi = (x) >> 16; (a).mid = (x) >> 8; (a).lo = (x);} while(0)
#define U32_TO_LEN          U32_TO_ADDR

/** @name Compatible ISA base I/O port addresses. Disabled if zero.
 * @{  */
#define NUM_ISA_BASES       8
#define MAX_ISA_BASE        (NUM_ISA_BASES - 1)
#define ISA_BASE_DISABLED   6

#ifdef IN_RING3
static uint16_t const g_aISABases[NUM_ISA_BASES] =
{
    0x330, 0x334, 0x230, 0x234, 0x130, 0x134, 0, 0
};
#endif
/** @}  */

/**
 * Emulated device types.
 */
enum BL_DEVICE_TYPE
{
    DEV_BT_958D     = 0,    /* BusLogic BT-958D, PCI. */
    DEV_BT_545C     = 1,    /* BusLogic BT-545C, ISA. */
    DEV_AHA_1540B   = 2     /* Adaptec AHA-1540B, ISA. */
};

/** Pointer to a task state structure. */
typedef struct BUSLOGICREQ *PBUSLOGICREQ;

/**
 * The shared BusLogic device emulation state.
 */
typedef struct BUSLOGIC
{
    /** Status register - Readonly. */
    volatile uint8_t                regStatus;
    /** Interrupt register - Readonly. */
    volatile uint8_t                regInterrupt;
    /** Geometry register - Readonly. */
    volatile uint8_t                regGeometry;
    /** Pending (delayed) interrupt. */
    volatile uint8_t                uPendingIntr;

    /** Command code the guest issued. */
    uint8_t                         uOperationCode;
    /** Current position in the command buffer. */
    uint8_t                         iParameter;
    /** Parameters left until the command is complete. */
    uint8_t                         cbCommandParametersLeft;
    /** Buffer for the command parameters the adapter is currently receiving from the guest.
     *  Size of the largest command which is possible.  */
    uint8_t                         aCommandBuffer[BUSLOGIC_COMMAND_SIZE_MAX]; /* Size of the biggest request. */

    /** Only for LOG_ENABLED builds! */
    volatile uint32_t               cInMailboxesReadyIfLogEnabled;

    /** Position in the buffer we are reading next.
     * @note aligned on 64 byte boundrary for cache-line mojo.  Means IOISABase
     *       is at offset 130. */
    uint8_t                         iReply;
    /** Bytes left until the reply buffer is empty. */
    uint8_t                         cbReplyParametersLeft;
    /** Buffer to store reply data from the controller to the guest. */
    uint8_t                         aReplyBuffer[BUSLOGIC_REPLY_SIZE_MAX]; /* Size of the biggest reply. */

    /** ISA I/O port base (disabled if zero). */
    RTIOPORT                        IOISABase;
    /** Default ISA I/O port base in FW-compatible format. */
    uint8_t                         uDefaultISABaseCode;
    /** Emulated device type. */
    uint8_t                         uDevType;

    /** Signature index for Adaptec models. */
    uint8_t                         uAhaSigIdx;

    /** Whether we are using the RAM or reply buffer. */
    bool                            fUseLocalRam;

    /** Flag whether IRQs are enabled. */
    bool                            fIRQEnabled;
    /** Flag whether 24-bit mailboxes are in use (default is 32-bit). */
    bool                            fMbxIs24Bit;
    /** ISA I/O port base (encoded in FW-compatible format). */
    uint8_t                         uISABaseCode;
    /** ISA IRQ, non-zero if in ISA mode. */
    uint8_t                         uIsaIrq;

    /** Number of mailboxes the guest set up. */
    uint32_t                        cMailbox;

    /** Time when HBA reset was last initiated. */
    uint64_t                        u64ResetTime;   /**< @todo does this need to be saved? */
    /** Physical base address of the outgoing mailboxes. */
    RTGCPHYS                        GCPhysAddrMailboxOutgoingBase;
    /** Current outgoing mailbox position. */
    uint32_t                        uMailboxOutgoingPositionCurrent;
    /** Number of mailboxes ready. */
    volatile uint32_t               cMailboxesReady;
    /** Whether a notification to R3 was sent. */
    volatile bool                   fNotificationSent;
    /** Flag whether a BIOS request is pending. */
    volatile bool                   fBiosReqPending;

    /** Whether strict round robin is enabled. */
    bool                            fStrictRoundRobinMode;
    /** Whether the extended LUN CCB format is enabled for 32 possible logical units. */
    bool                            fExtendedLunCCBFormat;
    /** Last completed command, for debugging. */
    uint8_t                         uPrevCmd;

    /** Current incoming mailbox position. */
    uint32_t                        uMailboxIncomingPositionCurrent;
    /** Physical base address of the incoming mailboxes. */
    RTGCPHYS                        GCPhysAddrMailboxIncomingBase;

    /** Critical section protecting access to the interrupt status register and uMailboxIncomingPositionCurrent. */
    PDMCRITSECT                     CritSectIntr;

    /** Device presence indicators.
     * @note Copy of BUSLOGICDEVICE::fPresent accessible from ring-0. */
    bool                            afDevicePresent[BUSLOGIC_MAX_DEVICES];

    /** The event semaphore the processing thread waits on. */
    SUPSEMEVENT                     hEvtProcess;

    /** ISA compatibility I/O ports. */
    IOMIOPORTHANDLE                 hIoPortsIsa;
    /** BIOS I/O ports for booting, optional.   */
    IOMIOPORTHANDLE                 hIoPortsBios;
    /** PCI Region \#0: I/O ports. */
    IOMIOPORTHANDLE                 hIoPortsPci;
    /** PCI Region \#1: MMIO (32 bytes, but probably rounded up to 4KB). */
    IOMMMIOHANDLE                   hMmio;

    /** Local RAM for the fetch hostadapter local RAM request.
     *  I don't know how big the buffer really is but the maximum
     *  seems to be 256 bytes because the offset and count field in the command request
     *  are only one byte big.
     */
    HostAdapterLocalRam             LocalRam;
} BUSLOGIC;
/** Pointer to the shared BusLogic device emulation state. */
typedef BUSLOGIC *PBUSLOGIC;


/**
 * The ring-3 BusLogic device emulation state.
 *
 * @implements  PDMILEDPORTS
 */
typedef struct BUSLOGICR3
{
    /** The device instance - only for getting our bearings in interface methods. */
    PPDMDEVINSR3                    pDevIns;

    /** BusLogic device states. */
    BUSLOGICDEVICE                  aDeviceStates[BUSLOGIC_MAX_DEVICES];

    /** The base interface.
     * @todo use PDMDEVINS::IBase  */
    PDMIBASE                        IBase;
    /** Status Port - Leds interface. */
    PDMILEDPORTS                    ILeds;
    /** Partner of ILeds. */
    R3PTRTYPE(PPDMILEDCONNECTORS)   pLedsConnector;
    /** Status LUN: Media Notifys. */
    R3PTRTYPE(PPDMIMEDIANOTIFY)     pMediaNotify;

    /** Indicates that PDMDevHlpAsyncNotificationCompleted should be called when
     * a port is entering the idle state. */
    bool volatile                   fSignalIdle;
    /** Flag whether the worker thread is sleeping. */
    volatile bool                   fWrkThreadSleeping;

    /** Worker thread. */
    R3PTRTYPE(PPDMTHREAD)           pThreadWrk;

    /** Pointer to the array of addresses to redo. */
    R3PTRTYPE(PRTGCPHYS)            paGCPhysAddrCCBRedo;
    /** Number of addresses the redo array holds. */
    uint32_t                        cReqsRedo;
} BUSLOGICR3;
/** Pointer to the ring-3 BusLogic device emulation state. */
typedef BUSLOGICR3 *PBUSLOGICR3;


/**
 * The ring-0 BusLogic device emulation state.
 */
typedef struct BUSLOGICR0
{
    uint64_t                        uUnused;
} BUSLOGICR0;
/** Pointer to the ring-0 BusLogic device emulation state. */
typedef BUSLOGICR0 *PBUSLOGICR0;


/**
 * The raw-mode BusLogic device emulation state.
 */
typedef struct BUSLOGICRC
{
    uint64_t                        uUnused;
} BUSLOGICRC;
/** Pointer to the raw-mode BusLogic device emulation state. */
typedef BUSLOGICRC *PBUSLOGICRC;


/** The current context BusLogic device emulation state. */
typedef CTX_SUFF(BUSLOGIC) BUSLOGICCC;
/** Pointer to the current context BusLogic device emulation state. */
typedef CTX_SUFF(PBUSLOGIC) PBUSLOGICCC;


/** Register offsets in the I/O port space. */
#define BUSLOGIC_REGISTER_CONTROL   0 /**< Writeonly */
/** Fields for the control register. */
# define BL_CTRL_RSBUS  RT_BIT(4)   /* Reset SCSI Bus. */
# define BL_CTRL_RINT   RT_BIT(5)   /* Reset Interrupt. */
# define BL_CTRL_RSOFT  RT_BIT(6)   /* Soft Reset. */
# define BL_CTRL_RHARD  RT_BIT(7)   /* Hard Reset. */

#define BUSLOGIC_REGISTER_STATUS    0 /**< Readonly */
/** Fields for the status register. */
# define BL_STAT_CMDINV RT_BIT(0)   /* Command Invalid. */
# define BL_STAT_DIRRDY RT_BIT(2)   /* Data In Register Ready. */
# define BL_STAT_CPRBSY RT_BIT(3)   /* Command/Parameter Out Register Busy. */
# define BL_STAT_HARDY  RT_BIT(4)   /* Host Adapter Ready. */
# define BL_STAT_INREQ  RT_BIT(5)   /* Initialization Required. */
# define BL_STAT_DFAIL  RT_BIT(6)   /* Diagnostic Failure. */
# define BL_STAT_DACT   RT_BIT(7)   /* Diagnistic Active. */

#define BUSLOGIC_REGISTER_COMMAND   1 /**< Writeonly */
#define BUSLOGIC_REGISTER_DATAIN    1 /**< Readonly */
#define BUSLOGIC_REGISTER_INTERRUPT 2 /**< Readonly */
/** Fields for the interrupt register. */
# define BL_INTR_IMBL   RT_BIT(0)   /* Incoming Mailbox Loaded. */
# define BL_INTR_OMBR   RT_BIT(1)   /* Outgoing Mailbox Available. */
# define BL_INTR_CMDC   RT_BIT(2)   /* Command Complete. */
# define BL_INTR_RSTS   RT_BIT(3)   /* SCSI Bus Reset State. */
# define BL_INTR_INTV   RT_BIT(7)   /* Interrupt Valid. */

#define BUSLOGIC_REGISTER_GEOMETRY  3 /* Readonly */
# define BL_GEOM_XLATEN  RT_BIT(7)  /* Extended geometry translation enabled. */
# define BL_GEOM_ISA     0x55       /* ISA adapter? Utterly undocumented. */

/** Structure for the INQUIRE_PCI_HOST_ADAPTER_INFORMATION reply. */
typedef struct ReplyInquirePCIHostAdapterInformation
{
    uint8_t       IsaIOPort;
    uint8_t       IRQ;
    unsigned char LowByteTerminated : 1;
    unsigned char HighByteTerminated : 1;
    unsigned char uReserved : 2; /* Reserved. */
    unsigned char JP1 : 1; /* Whatever that means. */
    unsigned char JP2 : 1; /* Whatever that means. */
    unsigned char JP3 : 1; /* Whatever that means. */
    /** Whether the provided info is valid. */
    unsigned char InformationIsValid: 1;
    uint8_t       uReserved2; /* Reserved. */
} ReplyInquirePCIHostAdapterInformation, *PReplyInquirePCIHostAdapterInformation;
AssertCompileSize(ReplyInquirePCIHostAdapterInformation, 4);

/** Structure for the INQUIRE_CONFIGURATION reply. */
typedef struct ReplyInquireConfiguration
{
    unsigned char uReserved1 :     5;
    bool          fDmaChannel5 :   1;
    bool          fDmaChannel6 :   1;
    bool          fDmaChannel7 :   1;
    bool          fIrqChannel9 :   1;
    bool          fIrqChannel10 :  1;
    bool          fIrqChannel11 :  1;
    bool          fIrqChannel12 :  1;
    unsigned char uReserved2 :     1;
    bool          fIrqChannel14 :  1;
    bool          fIrqChannel15 :  1;
    unsigned char uReserved3 :     1;
    unsigned char uHostAdapterId : 4;
    unsigned char uReserved4 :     4;
} ReplyInquireConfiguration, *PReplyInquireConfiguration;
AssertCompileSize(ReplyInquireConfiguration, 3);

/** Structure for the INQUIRE_SETUP_INFORMATION reply. */
typedef struct ReplyInquireSetupInformationSynchronousValue
{
    unsigned char uOffset :         4;
    unsigned char uTransferPeriod : 3;
    bool fSynchronous :             1;
}ReplyInquireSetupInformationSynchronousValue, *PReplyInquireSetupInformationSynchronousValue;
AssertCompileSize(ReplyInquireSetupInformationSynchronousValue, 1);

typedef struct ReplyInquireSetupInformation
{
    bool fSynchronousInitiationEnabled : 1;
    bool fParityCheckingEnabled :        1;
    unsigned char uReserved1 :           6;
    uint8_t uBusTransferRate;
    uint8_t uPreemptTimeOnBus;
    uint8_t uTimeOffBus;
    uint8_t cMailbox;
    Addr24  MailboxAddress;
    ReplyInquireSetupInformationSynchronousValue SynchronousValuesId0To7[8];
    uint8_t uDisconnectPermittedId0To7;
    uint8_t uSignature;
    uint8_t uCharacterD;
    uint8_t uHostBusType;
    uint8_t uWideTransferPermittedId0To7;
    uint8_t uWideTransfersActiveId0To7;
    ReplyInquireSetupInformationSynchronousValue SynchronousValuesId8To15[8];
    uint8_t uDisconnectPermittedId8To15;
    uint8_t uReserved2;
    uint8_t uWideTransferPermittedId8To15;
    uint8_t uWideTransfersActiveId8To15;
} ReplyInquireSetupInformation, *PReplyInquireSetupInformation;
AssertCompileSize(ReplyInquireSetupInformation, 34);

/** Structure for the INQUIRE_EXTENDED_SETUP_INFORMATION. */
#pragma pack(1)
typedef struct ReplyInquireExtendedSetupInformation
{
    uint8_t       uBusType;
    uint8_t       uBiosAddress;
    uint16_t      u16ScatterGatherLimit;
    uint8_t       cMailbox;
    uint32_t      uMailboxAddressBase;
    unsigned char uReserved1 : 2;
    bool          fFastEISA : 1;
    unsigned char uReserved2 : 3;
    bool          fLevelSensitiveInterrupt : 1;
    unsigned char uReserved3 : 1;
    unsigned char aFirmwareRevision[3];
    bool          fHostWideSCSI : 1;
    bool          fHostDifferentialSCSI : 1;
    bool          fHostSupportsSCAM : 1;
    bool          fHostUltraSCSI : 1;
    bool          fHostSmartTermination : 1;
    unsigned char uReserved4 : 3;
} ReplyInquireExtendedSetupInformation, *PReplyInquireExtendedSetupInformation;
AssertCompileSize(ReplyInquireExtendedSetupInformation, 14);
#pragma pack()

/** Structure for the INITIALIZE EXTENDED MAILBOX request. */
#pragma pack(1)
typedef struct RequestInitializeExtendedMailbox
{
    /** Number of mailboxes in guest memory. */
    uint8_t  cMailbox;
    /** Physical address of the first mailbox. */
    uint32_t uMailboxBaseAddress;
} RequestInitializeExtendedMailbox, *PRequestInitializeExtendedMailbox;
AssertCompileSize(RequestInitializeExtendedMailbox, 5);
#pragma pack()

/** Structure for the INITIALIZE MAILBOX request. */
typedef struct
{
    /** Number of mailboxes to set up. */
    uint8_t     cMailbox;
    /** Physical address of the first mailbox. */
    Addr24      aMailboxBaseAddr;
} RequestInitMbx, *PRequestInitMbx;
AssertCompileSize(RequestInitMbx, 4);

/**
 * Structure of a mailbox in guest memory.
 * The incoming and outgoing mailbox have the same size
 * but the incoming one has some more fields defined which
 * are marked as reserved in the outgoing one.
 * The last field is also different from the type.
 * For outgoing mailboxes it is the action and
 * for incoming ones the completion status code for the task.
 * We use one structure for both types.
 */
typedef struct Mailbox32
{
    /** Physical address of the CCB structure in the guest memory. */
    uint32_t u32PhysAddrCCB;
    /** Type specific data. */
    union
    {
        /** For outgoing mailboxes. */
        struct
        {
            /** Reserved */
            uint8_t uReserved[3];
            /** Action code. */
            uint8_t uActionCode;
        } out;
        /** For incoming mailboxes. */
        struct
        {
            /** The host adapter status after finishing the request. */
            uint8_t  uHostAdapterStatus;
            /** The status of the device which executed the request after executing it. */
            uint8_t  uTargetDeviceStatus;
            /** Reserved. */
            uint8_t  uReserved;
            /** The completion status code of the request. */
            uint8_t uCompletionCode;
        } in;
    } u;
} Mailbox32, *PMailbox32;
AssertCompileSize(Mailbox32, 8);

/** Old style 24-bit mailbox entry. */
typedef struct Mailbox24
{
    /** Mailbox command (incoming) or state (outgoing). */
    uint8_t     uCmdState;
    /** Physical address of the CCB structure in the guest memory. */
    Addr24      aPhysAddrCCB;
} Mailbox24, *PMailbox24;
AssertCompileSize(Mailbox24, 4);

/**
 * Action codes for outgoing mailboxes.
 */
enum BUSLOGIC_MAILBOX_OUTGOING_ACTION
{
    BUSLOGIC_MAILBOX_OUTGOING_ACTION_FREE = 0x00,
    BUSLOGIC_MAILBOX_OUTGOING_ACTION_START_COMMAND = 0x01,
    BUSLOGIC_MAILBOX_OUTGOING_ACTION_ABORT_COMMAND = 0x02
};

/**
 * Completion codes for incoming mailboxes.
 */
enum BUSLOGIC_MAILBOX_INCOMING_COMPLETION
{
    BUSLOGIC_MAILBOX_INCOMING_COMPLETION_FREE = 0x00,
    BUSLOGIC_MAILBOX_INCOMING_COMPLETION_WITHOUT_ERROR = 0x01,
    BUSLOGIC_MAILBOX_INCOMING_COMPLETION_ABORTED = 0x02,
    BUSLOGIC_MAILBOX_INCOMING_COMPLETION_ABORTED_NOT_FOUND = 0x03,
    BUSLOGIC_MAILBOX_INCOMING_COMPLETION_WITH_ERROR = 0x04,
    BUSLOGIC_MAILBOX_INCOMING_COMPLETION_INVALID_CCB = 0x05
};

/**
 * Host adapter status for incoming mailboxes.
 */
enum BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS
{
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_CMD_COMPLETED = 0x00,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_LINKED_CMD_COMPLETED = 0x0a,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_LINKED_CMD_COMPLETED_WITH_FLAG = 0x0b,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_DATA_UNDERUN = 0x0c,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_SCSI_SELECTION_TIMEOUT = 0x11,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_DATA_OVERRUN = 0x12,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_UNEXPECTED_BUS_FREE = 0x13,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_INVALID_BUS_PHASE_REQUESTED = 0x14,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_INVALID_OUTGOING_MAILBOX_ACTION_CODE = 0x15,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_INVALID_COMMAND_OPERATION_CODE = 0x16,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_LINKED_CCB_HAS_INVALID_LUN = 0x17,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_INVALID_COMMAND_PARAMETER = 0x1a,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_AUTO_REQUEST_SENSE_FAILED = 0x1b,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_TAGGED_QUEUING_MESSAGE_REJECTED = 0x1c,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_UNSUPPORTED_MESSAGE_RECEIVED = 0x1d,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_HOST_ADAPTER_HARDWARE_FAILED = 0x20,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_TARGET_FAILED_RESPONSE_TO_ATN = 0x21,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_HOST_ADAPTER_ASSERTED_RST = 0x22,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_OTHER_DEVICE_ASSERTED_RST = 0x23,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_TARGET_DEVICE_RECONNECTED_IMPROPERLY = 0x24,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_HOST_ADAPTER_ASSERTED_BUS_DEVICE_RESET = 0x25,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_ABORT_QUEUE_GENERATED = 0x26,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_HOST_ADAPTER_SOFTWARE_ERROR = 0x27,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_HOST_ADAPTER_HARDWARE_TIMEOUT_ERROR = 0x30,
    BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_SCSI_PARITY_ERROR_DETECTED = 0x34
};

/**
 * Device status codes for incoming mailboxes.
 */
enum BUSLOGIC_MAILBOX_INCOMING_DEVICE_STATUS
{
    BUSLOGIC_MAILBOX_INCOMING_DEVICE_STATUS_OPERATION_GOOD = 0x00,
    BUSLOGIC_MAILBOX_INCOMING_DEVICE_STATUS_CHECK_CONDITION = 0x02,
    BUSLOGIC_MAILBOX_INCOMING_DEVICE_STATUS_DEVICE_BUSY = 0x08
};

/**
 * Opcode types for CCB.
 */
enum BUSLOGIC_CCB_OPCODE
{
    BUSLOGIC_CCB_OPCODE_INITIATOR_CCB = 0x00,
    BUSLOGIC_CCB_OPCODE_TARGET_CCB = 0x01,
    BUSLOGIC_CCB_OPCODE_INITIATOR_CCB_SCATTER_GATHER = 0x02,
    BUSLOGIC_CCB_OPCODE_INITIATOR_CCB_RESIDUAL_DATA_LENGTH = 0x03,
    BUSLOGIC_CCB_OPCODE_INITIATOR_CCB_RESIDUAL_SCATTER_GATHER = 0x04,
    BUSLOGIC_CCB_OPCODE_BUS_DEVICE_RESET = 0x81
};

/**
 * Data transfer direction.
 */
enum BUSLOGIC_CCB_DIRECTION
{
    BUSLOGIC_CCB_DIRECTION_UNKNOWN = 0x00,
    BUSLOGIC_CCB_DIRECTION_IN      = 0x01,
    BUSLOGIC_CCB_DIRECTION_OUT     = 0x02,
    BUSLOGIC_CCB_DIRECTION_NO_DATA = 0x03
};

/**
 * The command control block for a SCSI request.
 */
typedef struct CCB32
{
    /** Opcode. */
    uint8_t       uOpcode;
    /** Reserved */
    unsigned char uReserved1 :      3;
    /** Data direction for the request. */
    unsigned char uDataDirection :  2;
    /** Whether the request is tag queued. */
    bool          fTagQueued :      1;
    /** Queue tag mode. */
    unsigned char uQueueTag :       2;
    /** Length of the SCSI CDB. */
    uint8_t       cbCDB;
    /** Sense data length. */
    uint8_t       cbSenseData;
    /** Data length. */
    uint32_t      cbData;
    /** Data pointer.
     *  This points to the data region or a scatter gather list based on the opcode.
     */
    uint32_t      u32PhysAddrData;
    /** Reserved. */
    uint8_t       uReserved2[2];
    /** Host adapter status. */
    uint8_t       uHostAdapterStatus;
    /** Device adapter status. */
    uint8_t       uDeviceStatus;
    /** The device the request is sent to. */
    uint8_t       uTargetId;
    /**The LUN in the device. */
    unsigned char uLogicalUnit : 5;
    /** Legacy tag. */
    bool          fLegacyTagEnable : 1;
    /** Legacy queue tag. */
    unsigned char uLegacyQueueTag : 2;
    /** The SCSI CDB.  (A CDB can be 12 bytes long.) */
    uint8_t       abCDB[12];
    /** Reserved. */
    uint8_t       uReserved3[6];
    /** Sense data pointer. */
    uint32_t      u32PhysAddrSenseData;
} CCB32, *PCCB32;
AssertCompileSize(CCB32, 40);


/**
 * The 24-bit command control block.
 */
typedef struct CCB24
{
    /** Opcode. */
    uint8_t         uOpcode;
    /** The LUN in the device. */
    unsigned char   uLogicalUnit : 3;
    /** Data direction for the request. */
    unsigned char   uDataDirection : 2;
    /** The target device ID. */
    unsigned char   uTargetId : 3;
    /** Length of the SCSI CDB. */
    uint8_t         cbCDB;
    /** Sense data length. */
    uint8_t         cbSenseData;
    /** Data length. */
    Len24           acbData;
    /** Data pointer.
     *  This points to the data region or a scatter gather list based on the opc
     */
    Addr24          aPhysAddrData;
    /** Pointer to next CCB for linked commands. */
    Addr24          aPhysAddrLink;
    /** Command linking identifier. */
    uint8_t         uLinkId;
    /** Host adapter status. */
    uint8_t         uHostAdapterStatus;
    /** Device adapter status. */
    uint8_t         uDeviceStatus;
    /** Two unused bytes. */
    uint8_t         aReserved[2];
    /** The SCSI CDB.  (A CDB can be 12 bytes long.)   */
    uint8_t         abCDB[12];
} CCB24, *PCCB24;
AssertCompileSize(CCB24, 30);

/**
 * The common 24-bit/32-bit command control block. The 32-bit CCB is laid out
 * such that many fields are in the same location as in the older 24-bit CCB.
 */
typedef struct CCBC
{
    /** Opcode. */
    uint8_t         uOpcode;
    /** The LUN in the device. */
    unsigned char   uPad1 : 3;
    /** Data direction for the request. */
    unsigned char   uDataDirection : 2;
    /** The target device ID. */
    unsigned char   uPad2 : 3;
    /** Length of the SCSI CDB. */
    uint8_t         cbCDB;
    /** Sense data length. */
    uint8_t         cbSenseData;
    uint8_t         aPad1[10];
    /** Host adapter status. */
    uint8_t         uHostAdapterStatus;
    /** Device adapter status. */
    uint8_t         uDeviceStatus;
    uint8_t         aPad2[2];
    /** The SCSI CDB (up to 12 bytes). */
    uint8_t         abCDB[12];
} CCBC, *PCCBC;
AssertCompileSize(CCBC, 30);

/* Make sure that the 24-bit/32-bit/common CCB offsets match. */
AssertCompileMemberOffset(CCBC,  cbCDB, 2);
AssertCompileMemberOffset(CCB24, cbCDB, 2);
AssertCompileMemberOffset(CCB32, cbCDB, 2);
AssertCompileMemberOffset(CCBC,  uHostAdapterStatus, 14);
AssertCompileMemberOffset(CCB24, uHostAdapterStatus, 14);
AssertCompileMemberOffset(CCB32, uHostAdapterStatus, 14);
AssertCompileMemberOffset(CCBC,  abCDB, 18);
AssertCompileMemberOffset(CCB24, abCDB, 18);
AssertCompileMemberOffset(CCB32, abCDB, 18);

/** A union of all CCB types (24-bit/32-bit/common). */
typedef union CCBU
{
    CCB32    n;     /**< New 32-bit CCB. */
    CCB24    o;     /**< Old 24-bit CCB. */
    CCBC     c;     /**< Common CCB subset. */
} CCBU, *PCCBU;

/** 32-bit scatter-gather list entry. */
typedef struct SGE32
{
    uint32_t   cbSegment;
    uint32_t   u32PhysAddrSegmentBase;
} SGE32, *PSGE32;
AssertCompileSize(SGE32, 8);

/** 24-bit scatter-gather list entry. */
typedef struct SGE24
{
    Len24       acbSegment;
    Addr24      aPhysAddrSegmentBase;
} SGE24, *PSGE24;
AssertCompileSize(SGE24, 6);

/**
 * The structure for the "Execute SCSI Command" command.
 */
typedef struct ESCMD
{
    /** Data length. */
    uint32_t        cbData;
    /** Data pointer. */
    uint32_t        u32PhysAddrData;
    /** The device the request is sent to. */
    uint8_t         uTargetId;
    /** The LUN in the device. */
    uint8_t         uLogicalUnit;
    /** Reserved */
    unsigned char   uReserved1 : 3;
    /** Data direction for the request. */
    unsigned char   uDataDirection : 2;
    /** Reserved */
    unsigned char   uReserved2 : 3;
    /** Length of the SCSI CDB. */
    uint8_t         cbCDB;
    /** The SCSI CDB.  (A CDB from our BIOS can be up to 16 bytes long
     * which works with our emulation even though the original BusLogic HBA
     * supports only 12 byte CDBs). */
    uint8_t         abCDB[16];
} ESCMD, *PESCMD;
AssertCompileSize(ESCMD, 28);

/**
 * Task state for a CCB request.
 */
typedef struct BUSLOGICREQ
{
    /** PDM extended media interface I/O request hande. */
    PDMMEDIAEXIOREQ                hIoReq;
    /** Device this task is assigned to. */
    PBUSLOGICDEVICE                pTargetDevice;
    /** The command control block from the guest. */
    CCBU                           CCBGuest;
    /** Guest physical address of th CCB. */
    RTGCPHYS                       GCPhysAddrCCB;
    /** Pointer to the R3 sense buffer. */
    uint8_t                        *pbSenseBuffer;
    /** Flag whether this is a request from the BIOS. */
    bool                           fBIOS;
    /** 24-bit request flag (default is 32-bit). */
    bool                           fIs24Bit;
    /** SCSI status code. */
    uint8_t                        u8ScsiSts;
} BUSLOGICREQ;

/**
 * S/G buffer copy arguments.
 */
typedef struct BUSLOGICCOPYARGS
{
    /** Pointer to the shared BusLogic instance data. */
    PBUSLOGIC                       pThis;
    /** Pointer to the device instance data. */
    PPDMDEVINS                      pDevIns;
    /** Pointer to the SCSI command buffer. */
    PESCMD                          pCmd;
    /** Number of bytes copied already. */
    size_t                          cbCopied;
} BUSLOGICCOPYARGS;
/** Pointer to BUSLOGICCOPYARGS. */
typedef BUSLOGICCOPYARGS *PBUSLOGICCOPYARGS;

#ifdef IN_RING3
/**
 * Memory buffer callback.
 *
 * @param   pDevIns The device instance.
 * @param   pThis   Pointer to the shared BusLogic instance data.
 * @param   GCPhys  The guest physical address of the memory buffer.
 * @param   pSgBuf  The pointer to the host R3 S/G buffer.
 * @param   cbCopy  How many bytes to copy between the two buffers.
 * @param   pcbSkip Initially contains the amount of bytes to skip
 *                  starting from the guest physical address before
 *                  accessing the S/G buffer and start copying data.
 *                  On return this contains the remaining amount if
 *                  cbCopy < *pcbSkip or 0 otherwise.
 */
typedef DECLCALLBACKTYPE(void, FNBUSLOGICR3MEMCOPYCALLBACK,(PPDMDEVINS pDevIns, PBUSLOGIC pThis, RTGCPHYS GCPhys,
                                                       PRTSGBUF pSgBuf, size_t cbCopy, size_t *pcbSkip));
/** Pointer to a memory copy buffer callback. */
typedef FNBUSLOGICR3MEMCOPYCALLBACK *PFNBUSLOGICR3MEMCOPYCALLBACK;
#endif

#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef IN_RING3
static int buslogicR3RegisterISARange(PPDMDEVINS pDevIns, PBUSLOGIC pThis, uint8_t uBaseCode);
#endif


/**
 * Assert IRQ line of the BusLogic adapter. Rather than using
 * the more modern method of the guest explicitly only clearing
 * the interrupt causes it handled, BusLogic never reports all
 * interrupts at once. Instead, new interrupts are postponed if
 * an interrupt of a different type is still pending.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           Pointer to the shared BusLogic instance data.
 * @param   fSuppressIrq    Flag to suppress IRQ generation regardless of fIRQEnabled
 * @param   uIrqType        Type of interrupt being generated.
 */
static void buslogicSetInterrupt(PPDMDEVINS pDevIns, PBUSLOGIC pThis, bool fSuppressIrq, uint8_t uIrqType)
{
    LogFlowFunc(("pThis=%#p, setting %#02x (current %#02x, pending %#02x)\n",
                 pThis, uIrqType, pThis->regInterrupt, pThis->uPendingIntr));

    /* A CMDC interrupt overrides other pending interrupts. The documentation may claim
     * otherwise, but a real BT-958 replaces a pending IMBL with a CMDC; the IMBL simply
     * vanishes. However, if there's a CMDC already active, another CMDC is latched and
     * reported once the first CMDC is cleared.
     */
    if (uIrqType & BL_INTR_CMDC)
    {
        Assert(uIrqType == BL_INTR_CMDC);
        if ((pThis->regInterrupt & BL_INTR_INTV) && !(pThis->regInterrupt & BL_INTR_CMDC))
            Log(("CMDC overriding pending interrupt! (was %02x)\n", pThis->regInterrupt));
        if (!(pThis->regInterrupt & BL_INTR_CMDC))
            pThis->regInterrupt |= uIrqType | BL_INTR_INTV; /* Report now. */
        else
            pThis->uPendingIntr |= uIrqType;                /* Report later. */
    }
    else if (uIrqType & (BL_INTR_IMBL | BL_INTR_OMBR))
    {
        /* If the CMDC interrupt is pending, store IMBL/OMBR for later. Note that IMBL
         * and OMBR can be reported together even if an interrupt of the other type is
         * already pending.
         */
        if (!(pThis->regInterrupt & BL_INTR_CMDC))
            pThis->regInterrupt |= uIrqType | BL_INTR_INTV; /* Report now. */
        else
            pThis->uPendingIntr |= uIrqType;                /* Report later. */
    }
    else    /* We do not expect to see BL_INTR_RSTS at this point. */
        AssertMsgFailed(("Invalid interrupt state (unknown interrupt cause)!\n"));
    AssertMsg(pThis->regInterrupt, ("Invalid interrupt state (interrupt not set)!\n"));
    AssertMsg(pThis->regInterrupt != BL_INTR_INTV, ("Invalid interrupt state (set but no cause)!\n"));

    if (pThis->fIRQEnabled && !fSuppressIrq)
    {
        if (!pThis->uIsaIrq)
            PDMDevHlpPCISetIrq(pDevIns, 0, 1);
        else
            PDMDevHlpISASetIrq(pDevIns, pThis->uIsaIrq, 1);
    }
}

/**
 * Deasserts the interrupt line of the BusLogic adapter.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the shared BusLogic instance data.
 */
static void buslogicClearInterrupt(PPDMDEVINS pDevIns, PBUSLOGIC pThis)
{
    LogFlowFunc(("pThis=%#p, clearing %#02x (pending %#02x)\n",
                 pThis, pThis->regInterrupt, pThis->uPendingIntr));
    pThis->regInterrupt = 0;
    pThis->regStatus &= ~BL_STAT_CMDINV;
    if (!pThis->uIsaIrq)
        PDMDevHlpPCISetIrq(pDevIns, 0, 0);
    else
        PDMDevHlpISASetIrq(pDevIns, pThis->uIsaIrq, 0);
    /* If there's another pending interrupt, report it now. */
    if (pThis->uPendingIntr)
    {
        buslogicSetInterrupt(pDevIns, pThis, false, pThis->uPendingIntr);
        pThis->uPendingIntr = 0;
    }
}

#if defined(IN_RING3)

/**
 * Advances the mailbox pointer to the next slot.
 *
 * @param   pThis       Pointer to the shared BusLogic instance data.
 */
DECLINLINE(void) buslogicR3OutgoingMailboxAdvance(PBUSLOGIC pThis)
{
    uint32_t    uNextPos = pThis->uMailboxOutgoingPositionCurrent + 1;
    Assert(uNextPos <= pThis->cMailbox);
    pThis->uMailboxOutgoingPositionCurrent = uNextPos >= pThis->cMailbox ? 0 : uNextPos;
}

/**
 * Initialize local RAM of host adapter with default values.
 *
 * @param   pThis       Pointer to the shared BusLogic instance data.
 */
static void buslogicR3InitializeLocalRam(PBUSLOGIC pThis)
{
    /*
     * These values are mostly from what I think is right
     * looking at the dmesg output from a Linux guest inside
     * a VMware server VM.
     *
     * So they don't have to be right :)
     */
    memset(pThis->LocalRam.u8View, 0, sizeof(HostAdapterLocalRam));
    pThis->LocalRam.structured.autoSCSIData.fLevelSensitiveInterrupt = true;
    pThis->LocalRam.structured.autoSCSIData.fParityCheckingEnabled = true;
    pThis->LocalRam.structured.autoSCSIData.fExtendedTranslation = true; /* Same as in geometry register. */
    pThis->LocalRam.structured.autoSCSIData.u16DeviceEnabledMask = UINT16_MAX; /* All enabled. Maybe mask out non present devices? */
    pThis->LocalRam.structured.autoSCSIData.u16WidePermittedMask = UINT16_MAX;
    pThis->LocalRam.structured.autoSCSIData.u16FastPermittedMask = UINT16_MAX;
    pThis->LocalRam.structured.autoSCSIData.u16SynchronousPermittedMask = UINT16_MAX;
    pThis->LocalRam.structured.autoSCSIData.u16DisconnectPermittedMask = UINT16_MAX;
    pThis->LocalRam.structured.autoSCSIData.fStrictRoundRobinMode = pThis->fStrictRoundRobinMode;
    pThis->LocalRam.structured.autoSCSIData.u16UltraPermittedMask = UINT16_MAX;
    pThis->LocalRam.structured.autoSCSIData.uSCSIId = 7;
    pThis->LocalRam.structured.autoSCSIData.uHostAdapterIoPortAddress = pThis->uDefaultISABaseCode == ISA_BASE_DISABLED ? 2 : pThis->uDefaultISABaseCode;
    /** @todo calculate checksum? */
}

/**
 * Do a hardware reset of the buslogic adapter.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the shared BusLogic instance data.
 * @param   fResetIO    Flag determining whether ISA I/O should be reset.
 */
static int buslogicR3HwReset(PPDMDEVINS pDevIns, PBUSLOGIC pThis, bool fResetIO)
{
    LogFlowFunc(("pThis=%#p\n", pThis));

    /* Reset registers to default values. */
    pThis->regStatus = BL_STAT_HARDY | BL_STAT_INREQ;
    pThis->regGeometry = BL_GEOM_XLATEN;

    /* BusLogic's BTDOSM.SYS version 4.20 DOS ASPI driver (but not e.g.
     * version 3.00) insists that bit 6 in the completely undocumented
     * "geometry register" must be set when scanning for ISA devices.
     * This bit may be a convenient way of avoiding a "double scan" of
     * PCI HBAs (which do not have bit 6 set) in ISA compatibility mode.
     *
     * On a BT-542B and BT-545C, the geometry register contains the value
     * 55h after power up/reset. In addition, bit 7 reflects the state
     * of the >1GB disk setting (DIP switch or EEPROM).
     */
    if (pThis->uDevType == DEV_BT_545C)
        pThis->regGeometry |= BL_GEOM_ISA;

    pThis->uOperationCode = 0xff; /* No command executing. */
    pThis->uPrevCmd = 0xff;
    pThis->iParameter = 0;
    pThis->cbCommandParametersLeft = 0;
    pThis->fIRQEnabled = true;
    pThis->fStrictRoundRobinMode = false;
    pThis->fExtendedLunCCBFormat = false;
    pThis->uMailboxOutgoingPositionCurrent = 0;
    pThis->uMailboxIncomingPositionCurrent = 0;
    pThis->uAhaSigIdx = 0;
    pThis->cMailbox = 0;
    pThis->GCPhysAddrMailboxIncomingBase = 0;
    pThis->GCPhysAddrMailboxOutgoingBase = 0;

    /* Clear any active/pending interrupts. */
    pThis->uPendingIntr = 0;
    buslogicClearInterrupt(pDevIns, pThis);

    /* Guest-initiated HBA reset does not affect ISA port I/O. */
    if (fResetIO)
        buslogicR3RegisterISARange(pDevIns, pThis, pThis->uDefaultISABaseCode);
    buslogicR3InitializeLocalRam(pThis);

    return VINF_SUCCESS;
}

#endif /* IN_RING3 */

/**
 * Resets the command state machine for the next command and notifies the guest.
 * Note that suppressing CMDC also suppresses the interrupt, but not vice versa.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           Pointer to the shared BusLogic instance data.
 * @param   fSuppressIrq    Flag to suppress IRQ generation regardless of current state
 * @param   fSuppressCMDC   Flag to suppress command completion status as well
 */
static void buslogicCommandComplete(PPDMDEVINS pDevIns, PBUSLOGIC pThis, bool fSuppressIrq, bool fSuppressCMDC)
{
    LogFlowFunc(("pThis=%#p\n", pThis));
    Assert(pThis->uOperationCode != BUSLOGICCOMMAND_EXECUTE_MAILBOX_COMMAND);

    pThis->fUseLocalRam = false;
    pThis->regStatus |= BL_STAT_HARDY;
    pThis->regStatus &= ~BL_STAT_DIRRDY;
    pThis->iReply = 0;

    /* Some commands do not set CMDC when successful. */
    if (!fSuppressCMDC)
    {
        /* Notify that the command is complete. */
        buslogicSetInterrupt(pDevIns, pThis, fSuppressIrq, BL_INTR_CMDC);
    }

    pThis->uPrevCmd = pThis->uOperationCode;
    pThis->uOperationCode = 0xff;
    pThis->iParameter = 0;
}

/**
 * Memory write helper to handle PCI/ISA differences - metadata writes.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the shared BusLogic instance data.
 * @param   GCPhys      Guest physical memory address
 * @param   pvBuf       Host side buffer address
 * @param   cbWrite     Number of bytes to write
 */
static void blPhysWriteMeta(PPDMDEVINS pDevIns, PBUSLOGIC pThis, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite)
{
    if (!pThis->uIsaIrq)
        PDMDevHlpPCIPhysWriteMeta(pDevIns, GCPhys, pvBuf, cbWrite);
    else
        PDMDevHlpPhysWriteMeta(pDevIns, GCPhys, pvBuf, cbWrite);
}

/**
 * Memory read helper to handle PCI/ISA differences - metadata reads.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the shared BusLogic instance data.
 * @param   GCPhys      Guest physical memory address.
 * @param   pvBuf       Host side buffer address.
 * @param   cbRead      Number of bytes to read.
 */
static void blPhysReadMeta(PPDMDEVINS pDevIns, PBUSLOGIC pThis, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead)
{
    if (!pThis->uIsaIrq)
        PDMDevHlpPCIPhysReadMeta(pDevIns, GCPhys, pvBuf, cbRead);
    else
        PDMDevHlpPhysReadMeta(pDevIns, GCPhys, pvBuf, cbRead);
}

#ifdef IN_RING3

/**
 * Memory write helper to handle PCI/ISA differences - userdata writes.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the shared BusLogic instance data.
 * @param   GCPhys      Guest physical memory address
 * @param   pvBuf       Host side buffer address
 * @param   cbWrite     Number of bytes to write
 */
static void blPhysWriteUser(PPDMDEVINS pDevIns, PBUSLOGIC pThis, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite)
{
    if (!pThis->uIsaIrq)
        PDMDevHlpPCIPhysWriteUser(pDevIns, GCPhys, pvBuf, cbWrite);
    else
        PDMDevHlpPhysWriteUser(pDevIns, GCPhys, pvBuf, cbWrite);
}

/**
 * Memory read helper to handle PCI/ISA differences - userdata reads.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the shared BusLogic instance data.
 * @param   GCPhys      Guest physical memory address.
 * @param   pvBuf       Host side buffer address.
 * @param   cbRead      Number of bytes to read.
 */
static void blPhysReadUser(PPDMDEVINS pDevIns, PBUSLOGIC pThis, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead)
{
    if (!pThis->uIsaIrq)
        PDMDevHlpPCIPhysReadUser(pDevIns, GCPhys, pvBuf, cbRead);
    else
        PDMDevHlpPhysReadUser(pDevIns, GCPhys, pvBuf, cbRead);
}

/**
 * Initiates a hard reset which was issued from the guest.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the shared BusLogic instance data.
 * @param   fHardReset  Flag initiating a hard (vs. soft) reset.
 */
static void buslogicR3InitiateReset(PPDMDEVINS pDevIns, PBUSLOGIC pThis, bool fHardReset)
{
    LogFlowFunc(("pThis=%#p fHardReset=%d\n", pThis, fHardReset));

    buslogicR3HwReset(pDevIns, pThis, false);

    if (fHardReset)
    {
        /* Set the diagnostic active bit in the status register and clear the ready state. */
        pThis->regStatus |=  BL_STAT_DACT;
        pThis->regStatus &= ~BL_STAT_HARDY;

        /* Remember when the guest initiated a reset (after we're done resetting). */
        pThis->u64ResetTime = PDMDevHlpTMTimeVirtGetNano(pDevIns);
    }
}


/**
 * Send a mailbox with set status codes to the guest.
 *
 * @param   pDevIns                 The device instance.
 * @param   pThis                   Pointer to the shared BusLogic instance data.
 * @param   GCPhysAddrCCB           The physical guest address of the CCB the mailbox is for.
 * @param   pCCBGuest               The command control block.
 * @param   uHostAdapterStatus      The host adapter status code to set.
 * @param   uDeviceStatus           The target device status to set.
 * @param   uMailboxCompletionCode  Completion status code to set in the mailbox.
 */
static void buslogicR3SendIncomingMailbox(PPDMDEVINS pDevIns, PBUSLOGIC pThis, RTGCPHYS GCPhysAddrCCB,
                                          PCCBU pCCBGuest, uint8_t uHostAdapterStatus,
                                          uint8_t uDeviceStatus, uint8_t uMailboxCompletionCode)
{
    Mailbox32 MbxIn;

    MbxIn.u32PhysAddrCCB           = (uint32_t)GCPhysAddrCCB;
    MbxIn.u.in.uHostAdapterStatus  = uHostAdapterStatus;
    MbxIn.u.in.uTargetDeviceStatus = uDeviceStatus;
    MbxIn.u.in.uReserved           = 0;
    MbxIn.u.in.uCompletionCode     = uMailboxCompletionCode;

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSectIntr, VINF_SUCCESS);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSectIntr, rc);

    RTGCPHYS GCPhysAddrMailboxIncoming = pThis->GCPhysAddrMailboxIncomingBase
                                       + (   pThis->uMailboxIncomingPositionCurrent
                                          * (pThis->fMbxIs24Bit ? sizeof(Mailbox24) : sizeof(Mailbox32)) );

    /* Advance to next mailbox position. */
    pThis->uMailboxIncomingPositionCurrent++;
    if (pThis->uMailboxIncomingPositionCurrent >= pThis->cMailbox)
        pThis->uMailboxIncomingPositionCurrent = 0;

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSectIntr);

    if (uMailboxCompletionCode != BUSLOGIC_MAILBOX_INCOMING_COMPLETION_ABORTED_NOT_FOUND)
    {
        LogFlowFunc(("Completing CCB %RGp hstat=%u, dstat=%u, incoming mailbox at %RGp\n", GCPhysAddrCCB,
                     uHostAdapterStatus, uDeviceStatus, GCPhysAddrMailboxIncoming));

        /* Update CCB. */
        pCCBGuest->c.uHostAdapterStatus = uHostAdapterStatus;
        pCCBGuest->c.uDeviceStatus      = uDeviceStatus;
        /* Rewrite CCB up to the CDB; perhaps more than necessary. */
        blPhysWriteMeta(pDevIns, pThis, GCPhysAddrCCB, pCCBGuest, RT_UOFFSETOF(CCBC, abCDB));
    }

# ifdef RT_STRICT
    uint8_t     uCode;
    unsigned    uCodeOffs = pThis->fMbxIs24Bit ? RT_OFFSETOF(Mailbox24, uCmdState) : RT_OFFSETOF(Mailbox32, u.out.uActionCode);
    blPhysReadMeta(pDevIns, pThis, GCPhysAddrMailboxIncoming + uCodeOffs, &uCode, sizeof(uCode));
    Assert(uCode == BUSLOGIC_MAILBOX_INCOMING_COMPLETION_FREE);
# endif

    /* Update mailbox. */
    if (pThis->fMbxIs24Bit)
    {
        Mailbox24   Mbx24;

        Mbx24.uCmdState = MbxIn.u.in.uCompletionCode;
        U32_TO_ADDR(Mbx24.aPhysAddrCCB, MbxIn.u32PhysAddrCCB);
        Log(("24-bit mailbox: completion code=%u, CCB at %RGp\n", Mbx24.uCmdState, (RTGCPHYS)ADDR_TO_U32(Mbx24.aPhysAddrCCB)));
        blPhysWriteMeta(pDevIns, pThis, GCPhysAddrMailboxIncoming, &Mbx24, sizeof(Mailbox24));
    }
    else
    {
        Log(("32-bit mailbox: completion code=%u, CCB at %RGp\n", MbxIn.u.in.uCompletionCode, GCPhysAddrCCB));
        blPhysWriteMeta(pDevIns, pThis, GCPhysAddrMailboxIncoming, &MbxIn, sizeof(Mailbox32));
    }

    rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSectIntr, VINF_SUCCESS);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSectIntr, rc);

# ifdef LOG_ENABLED
    ASMAtomicIncU32(&pThis->cInMailboxesReadyIfLogEnabled);
# endif

    buslogicSetInterrupt(pDevIns, pThis, false, BL_INTR_IMBL);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSectIntr);
}

# ifdef LOG_ENABLED

/**
 * Dumps the content of a mailbox for debugging purposes.
 *
 * @return nothing
 * @param  pMailbox   The mailbox to dump.
 * @param  fOutgoing  true if dumping the outgoing state.
 *                    false if dumping the incoming state.
 */
static void buslogicR3DumpMailboxInfo(PMailbox32 pMailbox, bool fOutgoing)
{
    Log(("%s: Dump for %s mailbox:\n", __FUNCTION__, fOutgoing ? "outgoing" : "incoming"));
    Log(("%s: u32PhysAddrCCB=%#x\n", __FUNCTION__, pMailbox->u32PhysAddrCCB));
    if (fOutgoing)
    {
        Log(("%s: uActionCode=%u\n", __FUNCTION__, pMailbox->u.out.uActionCode));
    }
    else
    {
        Log(("%s: uHostAdapterStatus=%u\n", __FUNCTION__, pMailbox->u.in.uHostAdapterStatus));
        Log(("%s: uTargetDeviceStatus=%u\n", __FUNCTION__, pMailbox->u.in.uTargetDeviceStatus));
        Log(("%s: uCompletionCode=%u\n", __FUNCTION__, pMailbox->u.in.uCompletionCode));
    }
}

/**
 * Dumps the content of a command control block for debugging purposes.
 *
 * @param   pCCB            Pointer to the command control block to dump.
 * @param   fIs24BitCCB     Flag to determine CCB format.
 */
static void buslogicR3DumpCCBInfo(PCCBU pCCB, bool fIs24BitCCB)
{
    Log(("%s: Dump for %s Command Control Block:\n", __FUNCTION__, fIs24BitCCB ? "24-bit" : "32-bit"));
    Log(("%s: uOpCode=%#x\n", __FUNCTION__, pCCB->c.uOpcode));
    Log(("%s: uDataDirection=%u\n", __FUNCTION__, pCCB->c.uDataDirection));
    Log(("%s: cbCDB=%u\n", __FUNCTION__, pCCB->c.cbCDB));
    Log(("%s: cbSenseData=%u\n", __FUNCTION__, pCCB->c.cbSenseData));
    Log(("%s: uHostAdapterStatus=%u\n", __FUNCTION__, pCCB->c.uHostAdapterStatus));
    Log(("%s: uDeviceStatus=%u\n", __FUNCTION__, pCCB->c.uDeviceStatus));
    if (fIs24BitCCB)
    {
        Log(("%s: cbData=%u\n", __FUNCTION__, LEN_TO_U32(pCCB->o.acbData)));
        Log(("%s: PhysAddrData=%#x\n", __FUNCTION__, ADDR_TO_U32(pCCB->o.aPhysAddrData)));
        Log(("%s: uTargetId=%u\n", __FUNCTION__, pCCB->o.uTargetId));
        Log(("%s: uLogicalUnit=%u\n", __FUNCTION__, pCCB->o.uLogicalUnit));
    }
    else
    {
        Log(("%s: cbData=%u\n", __FUNCTION__, pCCB->n.cbData));
        Log(("%s: PhysAddrData=%#x\n", __FUNCTION__, pCCB->n.u32PhysAddrData));
        Log(("%s: uTargetId=%u\n", __FUNCTION__, pCCB->n.uTargetId));
        Log(("%s: uLogicalUnit=%u\n", __FUNCTION__, pCCB->n.uLogicalUnit));
        Log(("%s: fTagQueued=%d\n", __FUNCTION__, pCCB->n.fTagQueued));
        Log(("%s: uQueueTag=%u\n", __FUNCTION__, pCCB->n.uQueueTag));
        Log(("%s: fLegacyTagEnable=%u\n", __FUNCTION__, pCCB->n.fLegacyTagEnable));
        Log(("%s: uLegacyQueueTag=%u\n", __FUNCTION__, pCCB->n.uLegacyQueueTag));
        Log(("%s: PhysAddrSenseData=%#x\n", __FUNCTION__, pCCB->n.u32PhysAddrSenseData));
    }
    Log(("%s: uCDB[0]=%#x\n", __FUNCTION__, pCCB->c.abCDB[0]));
    for (int i = 1; i < pCCB->c.cbCDB; i++)
        Log(("%s: uCDB[%d]=%u\n", __FUNCTION__, i, pCCB->c.abCDB[i]));
}

# endif /* LOG_ENABLED */

/**
 * Allocate data buffer.
 *
 * @param   pDevIns       PDM device instance.
 * @param   fIs24Bit      Flag whether the 24bit SG format is used.
 * @param   GCSGList      Guest physical address of S/G list.
 * @param   cEntries      Number of list entries to read.
 * @param   pSGEList      Pointer to 32-bit S/G list storage.
 */
static void buslogicR3ReadSGEntries(PPDMDEVINS pDevIns, bool fIs24Bit, RTGCPHYS GCSGList,
                                    uint32_t cEntries, SGE32 *pSGEList)
{
    /* Read the S/G entries. Convert 24-bit entries to 32-bit format. */
    PBUSLOGIC pThis = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    if (fIs24Bit)
    {
        SGE24 aSGE24[32];
        Assert(cEntries <= RT_ELEMENTS(aSGE24));

        Log2(("Converting %u 24-bit S/G entries to 32-bit\n", cEntries));
        blPhysReadMeta(pDevIns, pThis, GCSGList, &aSGE24, cEntries * sizeof(SGE24));
        for (uint32_t i = 0; i < cEntries; ++i)
        {
            pSGEList[i].cbSegment              = LEN_TO_U32(aSGE24[i].acbSegment);
            pSGEList[i].u32PhysAddrSegmentBase = ADDR_TO_U32(aSGE24[i].aPhysAddrSegmentBase);
        }
    }
    else
        blPhysReadMeta(pDevIns, pThis, GCSGList, pSGEList, cEntries * sizeof(SGE32));
}

/**
 * Determines the size of th guest data buffer.
 *
 * @returns VBox status code.
 * @param   pDevIns       PDM device instance.
 * @param   pCCBGuest     The CCB of the guest.
 * @param   fIs24Bit      Flag whether the 24bit SG format is used.
 * @param   pcbBuf        Where to store the size of the guest data buffer on success.
 */
static int buslogicR3QueryDataBufferSize(PPDMDEVINS pDevIns, PCCBU pCCBGuest, bool fIs24Bit, size_t *pcbBuf)
{
    int rc = VINF_SUCCESS;
    uint32_t cbDataCCB;
    uint32_t u32PhysAddrCCB;
    size_t cbBuf = 0;

    /* Extract the data length and physical address from the CCB. */
    if (fIs24Bit)
    {
        u32PhysAddrCCB  = ADDR_TO_U32(pCCBGuest->o.aPhysAddrData);
        cbDataCCB       = LEN_TO_U32(pCCBGuest->o.acbData);
    }
    else
    {
        u32PhysAddrCCB  = pCCBGuest->n.u32PhysAddrData;
        cbDataCCB       = pCCBGuest->n.cbData;
    }

#if 1
    /* Hack for NT 10/91: A CCB describes a 2K buffer, but TEST UNIT READY is executed. This command
     * returns no data, hence the buffer must be left alone!
     */
    if (pCCBGuest->c.abCDB[0] == 0)
        cbDataCCB = 0;
#endif

    if (   (pCCBGuest->c.uDataDirection != BUSLOGIC_CCB_DIRECTION_NO_DATA)
        && cbDataCCB)
    {
        /*
         * The BusLogic adapter can handle two different data buffer formats.
         * The first one is that the data pointer entry in the CCB points to
         * the buffer directly. In second mode the data pointer points to a
         * scatter gather list which describes the buffer.
         */
        if (   (pCCBGuest->c.uOpcode == BUSLOGIC_CCB_OPCODE_INITIATOR_CCB_SCATTER_GATHER)
            || (pCCBGuest->c.uOpcode == BUSLOGIC_CCB_OPCODE_INITIATOR_CCB_RESIDUAL_SCATTER_GATHER))
        {
            uint32_t cScatterGatherGCRead;
            uint32_t iScatterGatherEntry;
            SGE32    aScatterGatherReadGC[32]; /* A buffer for scatter gather list entries read from guest memory. */
            uint32_t cScatterGatherGCLeft = cbDataCCB / (fIs24Bit ? sizeof(SGE24) : sizeof(SGE32));
            RTGCPHYS GCPhysAddrScatterGatherCurrent = u32PhysAddrCCB;

            /* Count number of bytes to transfer. */
            do
            {
                cScatterGatherGCRead =   (cScatterGatherGCLeft < RT_ELEMENTS(aScatterGatherReadGC))
                                        ? cScatterGatherGCLeft
                                        : RT_ELEMENTS(aScatterGatherReadGC);
                cScatterGatherGCLeft -= cScatterGatherGCRead;

                buslogicR3ReadSGEntries(pDevIns, fIs24Bit, GCPhysAddrScatterGatherCurrent, cScatterGatherGCRead, aScatterGatherReadGC);

                for (iScatterGatherEntry = 0; iScatterGatherEntry < cScatterGatherGCRead; iScatterGatherEntry++)
                    cbBuf += aScatterGatherReadGC[iScatterGatherEntry].cbSegment;

                /* Set address to the next entries to read. */
                GCPhysAddrScatterGatherCurrent += cScatterGatherGCRead * (fIs24Bit ? sizeof(SGE24) : sizeof(SGE32));
            } while (cScatterGatherGCLeft > 0);

            Log(("%s: cbBuf=%d\n", __FUNCTION__, cbBuf));
        }
        else if (   pCCBGuest->c.uOpcode == BUSLOGIC_CCB_OPCODE_INITIATOR_CCB
                 || pCCBGuest->c.uOpcode == BUSLOGIC_CCB_OPCODE_INITIATOR_CCB_RESIDUAL_DATA_LENGTH)
            cbBuf = cbDataCCB;
    }

    if (RT_SUCCESS(rc))
        *pcbBuf = cbBuf;

    return rc;
}

/**
 * Copy from guest to host memory worker.
 *
 * @copydoc FNBUSLOGICR3MEMCOPYCALLBACK
 */
static DECLCALLBACK(void) buslogicR3CopyBufferFromGuestWorker(PPDMDEVINS pDevIns, PBUSLOGIC pThis, RTGCPHYS GCPhys,
                                                              PRTSGBUF pSgBuf, size_t cbCopy, size_t *pcbSkip)
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
        blPhysReadUser(pDevIns, pThis, GCPhys, pvSeg, cbSeg);
        GCPhys += cbSeg;
        cbCopy -= cbSeg;
    }
}

/**
 * Copy from host to guest memory worker.
 *
 * @copydoc FNBUSLOGICR3MEMCOPYCALLBACK
 */
static DECLCALLBACK(void) buslogicR3CopyBufferToGuestWorker(PPDMDEVINS pDevIns, PBUSLOGIC pThis, RTGCPHYS GCPhys,
                                                            PRTSGBUF pSgBuf, size_t cbCopy, size_t *pcbSkip)
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
        blPhysWriteUser(pDevIns, pThis, GCPhys, pvSeg, cbSeg);
        GCPhys += cbSeg;
        cbCopy -= cbSeg;
    }
}

/**
 * Walks the guest S/G buffer calling the given copy worker for every buffer.
 *
 * @returns The amout of bytes actually copied.
 * @param   pDevIns         The device instance.
 * @param   pThis           Pointer to the Buslogic device state.
 * @param   pReq            Pointer to the request state.
 * @param   pfnCopyWorker   The copy method to apply for each guest buffer.
 * @param   pSgBuf          The host S/G buffer.
 * @param   cbSkip          How many bytes to skip in advance before starting to copy.
 * @param   cbCopy          How many bytes to copy.
 */
static size_t buslogicR3SgBufWalker(PPDMDEVINS pDevIns, PBUSLOGIC pThis, PBUSLOGICREQ pReq,
                                    PFNBUSLOGICR3MEMCOPYCALLBACK pfnCopyWorker,
                                    PRTSGBUF pSgBuf, size_t cbSkip, size_t cbCopy)
{
    uint32_t   cbDataCCB;
    uint32_t   u32PhysAddrCCB;
    size_t     cbCopied = 0;

    /*
     * Add the amount to skip to the host buffer size to avoid a
     * few conditionals later on.
     */
    cbCopy += cbSkip;

    /* Extract the data length and physical address from the CCB. */
    if (pReq->fIs24Bit)
    {
        u32PhysAddrCCB  = ADDR_TO_U32(pReq->CCBGuest.o.aPhysAddrData);
        cbDataCCB       = LEN_TO_U32(pReq->CCBGuest.o.acbData);
    }
    else
    {
        u32PhysAddrCCB  = pReq->CCBGuest.n.u32PhysAddrData;
        cbDataCCB       = pReq->CCBGuest.n.cbData;
    }

#if 1
    /* Hack for NT 10/91: A CCB describes a 2K buffer, but TEST UNIT READY is executed. This command
     * returns no data, hence the buffer must be left alone!
     */
    if (pReq->CCBGuest.c.abCDB[0] == 0)
        cbDataCCB = 0;
#endif

    LogFlowFunc(("pReq=%#p cbDataCCB=%u direction=%u cbCopy=%zu\n", pReq, cbDataCCB,
                 pReq->CCBGuest.c.uDataDirection, cbCopy));

    if (   (cbDataCCB > 0)
        && (   pReq->CCBGuest.c.uDataDirection == BUSLOGIC_CCB_DIRECTION_IN
            || pReq->CCBGuest.c.uDataDirection == BUSLOGIC_CCB_DIRECTION_OUT
            || pReq->CCBGuest.c.uDataDirection == BUSLOGIC_CCB_DIRECTION_UNKNOWN))
    {
        if (   (pReq->CCBGuest.c.uOpcode == BUSLOGIC_CCB_OPCODE_INITIATOR_CCB_SCATTER_GATHER)
            || (pReq->CCBGuest.c.uOpcode == BUSLOGIC_CCB_OPCODE_INITIATOR_CCB_RESIDUAL_SCATTER_GATHER))
        {
            uint32_t cScatterGatherGCRead;
            uint32_t iScatterGatherEntry;
            SGE32    aScatterGatherReadGC[32]; /* Number of scatter gather list entries read from guest memory. */
            uint32_t cScatterGatherGCLeft = cbDataCCB / (pReq->fIs24Bit ? sizeof(SGE24) : sizeof(SGE32));
            RTGCPHYS GCPhysAddrScatterGatherCurrent = u32PhysAddrCCB;

            do
            {
                cScatterGatherGCRead = (cScatterGatherGCLeft < RT_ELEMENTS(aScatterGatherReadGC))
                                     ? cScatterGatherGCLeft
                                     : RT_ELEMENTS(aScatterGatherReadGC);
                cScatterGatherGCLeft -= cScatterGatherGCRead;

                buslogicR3ReadSGEntries(pDevIns, pReq->fIs24Bit, GCPhysAddrScatterGatherCurrent,
                                        cScatterGatherGCRead, aScatterGatherReadGC);

                for (iScatterGatherEntry = 0; iScatterGatherEntry < cScatterGatherGCRead && cbCopy > 0; iScatterGatherEntry++)
                {
                    RTGCPHYS GCPhysAddrDataBase;
                    size_t   cbCopyThis;

                    Log(("%s: iScatterGatherEntry=%u\n", __FUNCTION__, iScatterGatherEntry));

                    GCPhysAddrDataBase = (RTGCPHYS)aScatterGatherReadGC[iScatterGatherEntry].u32PhysAddrSegmentBase;
                    cbCopyThis = RT_MIN(cbCopy, aScatterGatherReadGC[iScatterGatherEntry].cbSegment);

                    Log(("%s: GCPhysAddrDataBase=%RGp cbCopyThis=%zu\n", __FUNCTION__, GCPhysAddrDataBase, cbCopyThis));

                    pfnCopyWorker(pDevIns, pThis, GCPhysAddrDataBase, pSgBuf, cbCopyThis, &cbSkip);
                    cbCopied += cbCopyThis;
                    cbCopy   -= cbCopyThis;
                }

                /* Set address to the next entries to read. */
                GCPhysAddrScatterGatherCurrent += cScatterGatherGCRead * (pReq->fIs24Bit ? sizeof(SGE24) : sizeof(SGE32));
            } while (   cScatterGatherGCLeft > 0
                     && cbCopy > 0);

        }
        else if (   pReq->CCBGuest.c.uOpcode == BUSLOGIC_CCB_OPCODE_INITIATOR_CCB
                 || pReq->CCBGuest.c.uOpcode == BUSLOGIC_CCB_OPCODE_INITIATOR_CCB_RESIDUAL_DATA_LENGTH)
        {
            /* The buffer is not scattered. */
            RTGCPHYS GCPhysAddrDataBase = u32PhysAddrCCB;

            AssertMsg(GCPhysAddrDataBase != 0, ("Physical address is 0\n"));

            Log(("Non-scattered buffer:\n"));
            Log(("u32PhysAddrData=%#x\n", u32PhysAddrCCB));
            Log(("cbData=%u\n", cbDataCCB));
            Log(("GCPhysAddrDataBase=0x%RGp\n", GCPhysAddrDataBase));

            /* Copy the data into the guest memory. */
            pfnCopyWorker(pDevIns, pThis, GCPhysAddrDataBase, pSgBuf, RT_MIN(cbDataCCB, cbCopy), &cbSkip);
            cbCopied += RT_MIN(cbDataCCB, cbCopy);
        }
    }

    return cbCopied - RT_MIN(cbSkip, cbCopied);
}

/**
 * Copies a data buffer into the S/G buffer set up by the guest.
 *
 * @returns Amount of bytes copied to the guest.
 * @param   pDevIns         The device instance.
 * @param   pThis           Pointer to the shared BusLogic instance data.
 * @param   pReq            Request structure.
 * @param   pSgBuf          The S/G buffer to copy from.
 * @param   cbSkip          How many bytes to skip in advance before starting to copy.
 * @param   cbCopy          How many bytes to copy.
 */
static size_t buslogicR3CopySgBufToGuest(PPDMDEVINS pDevIns, PBUSLOGIC pThis, PBUSLOGICREQ pReq,
                                         PRTSGBUF pSgBuf, size_t cbSkip, size_t cbCopy)
{
    return buslogicR3SgBufWalker(pDevIns, pThis, pReq, buslogicR3CopyBufferToGuestWorker, pSgBuf, cbSkip, cbCopy);
}

/**
 * Copies the guest S/G buffer into a host data buffer.
 *
 * @returns Amount of bytes copied from the guest.
 * @param   pDevIns         The device instance.
 * @param   pThis           Pointer to the shared BusLogic instance data.
 * @param   pReq            Request structure.
 * @param   pSgBuf          The S/G buffer to copy into.
 * @param   cbSkip          How many bytes to skip in advance before starting to copy.
 * @param   cbCopy          How many bytes to copy.
 */
static size_t buslogicR3CopySgBufFromGuest(PPDMDEVINS pDevIns, PBUSLOGIC pThis, PBUSLOGICREQ pReq,
                                           PRTSGBUF pSgBuf, size_t cbSkip, size_t cbCopy)
{
    return buslogicR3SgBufWalker(pDevIns, pThis, pReq, buslogicR3CopyBufferFromGuestWorker, pSgBuf, cbSkip, cbCopy);
}

/** Convert sense buffer length taking into account shortcut values. */
static uint32_t buslogicR3ConvertSenseBufferLength(uint32_t cbSense)
{
    /* Convert special sense buffer length values. */
    if (cbSense == 0)
        cbSense = 14;   /* 0 means standard 14-byte buffer. */
    else if (cbSense == 1)
        cbSense = 0;    /* 1 means no sense data. */
    else if (cbSense < 8)
        AssertMsgFailed(("Reserved cbSense value of %d used!\n", cbSense));

    return cbSense;
}

/**
 * Free the sense buffer.
 *
 * @param   pReq         Pointer to the request state.
 * @param   fCopy        If sense data should be copied to guest memory.
 */
static void buslogicR3SenseBufferFree(PBUSLOGICREQ pReq, bool fCopy)
{
    uint32_t    cbSenseBuffer;

    cbSenseBuffer = buslogicR3ConvertSenseBufferLength(pReq->CCBGuest.c.cbSenseData);

    /* Copy the sense buffer into guest memory if requested. */
    if (fCopy && cbSenseBuffer)
    {
        PPDMDEVINS  pDevIns = pReq->pTargetDevice->pDevIns;
        PBUSLOGIC   pThis   = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
        RTGCPHYS    GCPhysAddrSenseBuffer;

        /* With 32-bit CCBs, the (optional) sense buffer physical address is provided separately.
         * On the other hand, with 24-bit CCBs, the sense buffer is simply located at the end of
         * the CCB, right after the variable-length CDB.
         */
        if (pReq->fIs24Bit)
        {
            GCPhysAddrSenseBuffer  = pReq->GCPhysAddrCCB;
            GCPhysAddrSenseBuffer += pReq->CCBGuest.c.cbCDB + RT_OFFSETOF(CCB24, abCDB);
        }
        else
            GCPhysAddrSenseBuffer = pReq->CCBGuest.n.u32PhysAddrSenseData;

        Log3(("%s: sense buffer: %.*Rhxs\n", __FUNCTION__, cbSenseBuffer, pReq->pbSenseBuffer));
        blPhysWriteMeta(pDevIns, pThis, GCPhysAddrSenseBuffer, pReq->pbSenseBuffer, cbSenseBuffer);
    }

    RTMemFree(pReq->pbSenseBuffer);
    pReq->pbSenseBuffer = NULL;
}

/**
 * Alloc the sense buffer.
 *
 * @returns VBox status code.
 * @param   pReq    Pointer to the task state.
 */
static int buslogicR3SenseBufferAlloc(PBUSLOGICREQ pReq)
{
    pReq->pbSenseBuffer = NULL;

    uint32_t cbSenseBuffer = buslogicR3ConvertSenseBufferLength(pReq->CCBGuest.c.cbSenseData);
    if (cbSenseBuffer)
    {
        pReq->pbSenseBuffer = (uint8_t *)RTMemAllocZ(cbSenseBuffer);
        if (!pReq->pbSenseBuffer)
            return VERR_NO_MEMORY;
    }

    return VINF_SUCCESS;
}

#endif /* IN_RING3 */

/**
 * Parses the command buffer and executes it.
 *
 * @returns VBox status code.
 * @param   pDevIns     The PDM device instance.
 * @param   pThis       Pointer to the shared BusLogic instance data.
 */
static int buslogicProcessCommand(PPDMDEVINS pDevIns, PBUSLOGIC pThis)
{
    int rc = VINF_SUCCESS;
    bool fSuppressIrq = false;
    bool fSuppressCMDC = false;
    bool fCmdComplete  = true;

    LogFlowFunc(("pThis=%#p\n", pThis));
    AssertMsg(pThis->uOperationCode != 0xff, ("There is no command to execute\n"));

    switch (pThis->uOperationCode)
    {
        case BUSLOGICCOMMAND_TEST_CMDC_INTERRUPT:
            /* Valid command, no reply. */
            pThis->cbReplyParametersLeft = 0;
            break;
        case BUSLOGICCOMMAND_INQUIRE_PCI_HOST_ADAPTER_INFORMATION:
        {
            /* Only supported on PCI BusLogic HBAs. */
            if (pThis->uDevType != DEV_BT_958D)
            {
                Log(("Command %#x not valid for this adapter\n", pThis->uOperationCode));
                pThis->cbReplyParametersLeft = 0;
                pThis->regStatus |= BL_STAT_CMDINV;
                break;
            }

            PReplyInquirePCIHostAdapterInformation pReply = (PReplyInquirePCIHostAdapterInformation)pThis->aReplyBuffer;
            memset(pReply, 0, sizeof(ReplyInquirePCIHostAdapterInformation));

            /* Modeled after a real BT-958(D) */
            pReply->HighByteTerminated = 1;
            pReply->LowByteTerminated = 1;
            pReply->JP1 = 1;    /* Closed; "Factory configured - do not alter" */
            pReply->InformationIsValid = 1;
            pReply->IsaIOPort = pThis->uISABaseCode < 6 ? pThis->uISABaseCode : 0xff;
            pReply->IRQ = PCIDevGetInterruptLine(pDevIns->apPciDevs[0]);
            pThis->cbReplyParametersLeft = sizeof(ReplyInquirePCIHostAdapterInformation);
            break;
        }
        case BUSLOGICCOMMAND_SET_SCSI_SELECTION_TIMEOUT:
        {
            /* no-op */
            pThis->cbReplyParametersLeft = 0;
            break;
        }
        case BUSLOGICCOMMAND_MODIFY_IO_ADDRESS:
        {

            /* Modify the ISA-compatible I/O port base. Note that this technically
             * violates the PCI spec, as this address is not reported through PCI.
             * However, it is required for compatibility with old drivers.
             */
#ifdef IN_RING3 /* We can do this from ring-0 now, but we'd like to see the LogRel, so we keep going back to ring-3 anyway. */
            uint8_t baseCode = pThis->aCommandBuffer[0];

            Log(("ISA I/O for PCI (code %x)\n", baseCode));
            pThis->cbReplyParametersLeft = 0;
            if (baseCode < 8) {
                buslogicR3RegisterISARange(pDevIns, pThis, baseCode);
                fSuppressIrq = true;
                fSuppressCMDC = true;
            }
            else
            {
                Log(("ISA base %#x not valid for this adapter\n", baseCode));
                pThis->regStatus |= BL_STAT_CMDINV;
            }
            break;
#else
            AssertMsgFailed(("Must never get here!\n"));
            break;
#endif
        }
        case BUSLOGICCOMMAND_INQUIRE_BOARD_ID:
        {
            /* The special option byte is important: If it is '0' or 'B', Windows NT drivers
             * for Adaptec AHA-154x may claim the adapter. The BusLogic drivers will claim
             * the adapter only when the byte is *not* '0' or 'B'.
             */
            if (pThis->uDevType == DEV_AHA_1540B)
            {
                pThis->aReplyBuffer[0] = 'A'; /* Firmware option bytes */
                pThis->aReplyBuffer[1] = '0'; /* Special option byte */
            }
            else
            {
                pThis->aReplyBuffer[0] = 'A'; /* Firmware option bytes */
                pThis->aReplyBuffer[1] = 'A'; /* Special option byte */
            }

            /* FW version 5.07B for BT-958, version 4.25J for BT-545. */
            if (pThis->uDevType == DEV_BT_958D) {
                pThis->aReplyBuffer[2] = '5'; /* Major version 5 */
                pThis->aReplyBuffer[3] = '0'; /* Minor version 0 */
            }
            else
            {
                pThis->aReplyBuffer[2] = '4';
                pThis->aReplyBuffer[3] = '2';
            }
            pThis->cbReplyParametersLeft = 4; /* Reply is 4 bytes long */
            break;
        }
        case BUSLOGICCOMMAND_INQUIRE_FIRMWARE_VERSION_3RD_LETTER:
        {
            if (pThis->uDevType == DEV_AHA_1540B)
            {
                /* Newer ASPI4DOS.SYS versions expect this command to fail. */
                Log(("Command %#x not valid for this adapter\n", pThis->uOperationCode));
                pThis->cbReplyParametersLeft = 0;
                pThis->regStatus |= BL_STAT_CMDINV;
                break;
            }

            /* FW version 5.07B for BT-958, version 4.25J for BT-545. */
            if (pThis->uDevType == DEV_BT_958D)
                pThis->aReplyBuffer[0] = '7';
            else
                pThis->aReplyBuffer[0] = '5';
            pThis->cbReplyParametersLeft = 1;
            break;
        }
        case BUSLOGICCOMMAND_INQUIRE_FIRMWARE_VERSION_LETTER:
        {
            /* FW version 5.07B for BT-958, version 4.25J for BT-545. */
            if (pThis->uDevType == DEV_BT_958D)
                pThis->aReplyBuffer[0] = 'B';
            else
                pThis->aReplyBuffer[0] = 'J';
            pThis->cbReplyParametersLeft = 1;
            break;
        }
        case BUSLOGICCOMMAND_SET_ADAPTER_OPTIONS:
            /* The parameter list length is determined by the first byte of the command buffer. */
            if (pThis->iParameter == 1)
            {
                /* First pass - set the number of following parameter bytes. */
                pThis->cbCommandParametersLeft = RT_MIN(pThis->aCommandBuffer[0], sizeof(pThis->aCommandBuffer) - 1);
                Log(("Set HA options: %u bytes follow\n", pThis->cbCommandParametersLeft));
            }
            else
            {
                /* Second pass - process received data. */
                Log(("Set HA options: received %u bytes\n", pThis->aCommandBuffer[0]));
                /* We ignore the data - it only concerns the SCSI hardware protocol. */
            }
            pThis->cbReplyParametersLeft = 0;
            break;

        case BUSLOGICCOMMAND_EXECUTE_SCSI_COMMAND:
            /* The parameter list length is at least 12 bytes; the 12th byte determines
             * the number of additional CDB bytes that will follow.
             */
            if (pThis->iParameter == 12)
            {
                /* First pass - set the number of following CDB bytes. */
                pThis->cbCommandParametersLeft = RT_MIN(pThis->aCommandBuffer[11], sizeof(pThis->aCommandBuffer) - 12);
                Log(("Execute SCSI cmd: %u more bytes follow\n", pThis->cbCommandParametersLeft));
            }
            else
            {
                PESCMD      pCmd;

                /* Second pass - process received data. */
                Log(("Execute SCSI cmd: received %u bytes\n", pThis->aCommandBuffer[0]));
                pCmd = (PESCMD)pThis->aCommandBuffer;
                Log(("Addr %08X, cbData %08X, cbCDB=%u\n", pCmd->u32PhysAddrData, pCmd->cbData, pCmd->cbCDB));

                if (!ASMAtomicXchgBool(&pThis->fBiosReqPending, true))
                {
                    /* Wake up the worker thread. */
                    int rc2 = PDMDevHlpSUPSemEventSignal(pDevIns, pThis->hEvtProcess);
                    AssertRC(rc2);
                }

                fCmdComplete = false;
            }
            break;

        case BUSLOGICCOMMAND_INQUIRE_HOST_ADAPTER_MODEL_NUMBER:
        {
            /* Not supported on AHA-154x. */
            if (pThis->uDevType == DEV_AHA_1540B)
            {
                Log(("Command %#x not valid for this adapter\n", pThis->uOperationCode));
                pThis->cbReplyParametersLeft = 0;
                pThis->regStatus |= BL_STAT_CMDINV;
                break;
            }

            /* The reply length is set by the guest and is found in the first byte of the command buffer. */
            if (pThis->aCommandBuffer[0] > sizeof(pThis->aReplyBuffer))
            {
                Log(("Requested too much adapter model number data (%u)!\n", pThis->aCommandBuffer[0]));
                pThis->regStatus |= BL_STAT_CMDINV;
                break;
            }
            pThis->cbReplyParametersLeft = pThis->aCommandBuffer[0];
            memset(pThis->aReplyBuffer, 0, sizeof(pThis->aReplyBuffer));
            const char aModelName958[] = "958D ";   /* Trailing \0 is fine, that's the filler anyway. */
            const char aModelName545[] = "54xC ";
            AssertCompile(sizeof(aModelName958) == sizeof(aModelName545));
            const char *pModelName = pThis->uDevType == DEV_BT_958D ? aModelName958 : aModelName545;
            int cCharsToTransfer =   pThis->cbReplyParametersLeft <= sizeof(aModelName958)
                                   ? pThis->cbReplyParametersLeft
                                   : sizeof(aModelName958);

            for (int i = 0; i < cCharsToTransfer; i++)
                pThis->aReplyBuffer[i] = pModelName[i];

            break;
        }
        case BUSLOGICCOMMAND_INQUIRE_CONFIGURATION:
        {
            uint8_t uIrq;

            if (pThis->uIsaIrq)
                uIrq = pThis->uIsaIrq;
            else
                uIrq = PCIDevGetInterruptLine(pDevIns->apPciDevs[0]);

            pThis->cbReplyParametersLeft = sizeof(ReplyInquireConfiguration);
            PReplyInquireConfiguration pReply = (PReplyInquireConfiguration)pThis->aReplyBuffer;
            memset(pReply, 0, sizeof(ReplyInquireConfiguration));

            pReply->uHostAdapterId = 7; /* The controller has always 7 as ID. */
            pReply->fDmaChannel6  = 1;  /* DMA channel 6 is a good default. */

            /* The PCI IRQ is not necessarily representable in this structure.
             * If that is the case, the guest likely won't function correctly,
             * therefore we log a warning. Note that for ISA configurations, we
             * can only allow IRQs that can be supported; for PCI, the HBA
             * has no control over IRQ assignment.
             */
            switch (uIrq)
            {
                case 9:     pReply->fIrqChannel9  = 1; break;
                case 10:    pReply->fIrqChannel10 = 1; break;
                case 11:    pReply->fIrqChannel11 = 1; break;
                case 12:    pReply->fIrqChannel12 = 1; break;
                case 14:    pReply->fIrqChannel14 = 1; break;
                case 15:    pReply->fIrqChannel15 = 1; break;
                default:
                    LogRel(("Warning: PCI IRQ %d cannot be represented as ISA!\n", uIrq));
                    break;
            }
            break;
        }
        case BUSLOGICCOMMAND_INQUIRE_EXTENDED_SETUP_INFORMATION:
        {
            /* Some Adaptec AHA-154x drivers (e.g. OS/2) execute this command and expect
             * it to fail. If it succeeds, the drivers refuse to load. However, some newer
             * Adaptec 154x models supposedly support it too??
             */
            if (pThis->uDevType == DEV_AHA_1540B)
            {
                Log(("Command %#x not valid for this adapter\n", pThis->uOperationCode));
                pThis->cbReplyParametersLeft = 0;
                pThis->regStatus |= BL_STAT_CMDINV;
                break;
            }

            /* The reply length is set by the guest and is found in the first byte of the command buffer. */
            pThis->cbReplyParametersLeft = pThis->aCommandBuffer[0];
            PReplyInquireExtendedSetupInformation pReply = (PReplyInquireExtendedSetupInformation)pThis->aReplyBuffer;
            memset(pReply, 0, sizeof(ReplyInquireExtendedSetupInformation));

            /** @todo should this reflect the RAM contents (AutoSCSIRam)? */
            if (pThis->uDevType == DEV_BT_958D)
                pReply->uBusType = 'E';         /* EISA style */
            else
                pReply->uBusType = 'A';         /* ISA */

            pReply->u16ScatterGatherLimit = 8192;
            pReply->cMailbox = pThis->cMailbox;
            pReply->uMailboxAddressBase = (uint32_t)pThis->GCPhysAddrMailboxOutgoingBase;
            pReply->fLevelSensitiveInterrupt = true;
            pReply->fHostWideSCSI = true;
            pReply->fHostUltraSCSI = true;
            /* FW version 5.07B for BT-958, version 4.25J for BT-545. */
            if (pThis->uDevType == DEV_BT_958D)
                memcpy(pReply->aFirmwareRevision, "07B", sizeof(pReply->aFirmwareRevision));
            else
                memcpy(pReply->aFirmwareRevision, "25J", sizeof(pReply->aFirmwareRevision));

            break;
        }
        case BUSLOGICCOMMAND_INQUIRE_SETUP_INFORMATION:
        {
            /* The reply length is set by the guest and is found in the first byte of the command buffer. */
            pThis->cbReplyParametersLeft = pThis->aCommandBuffer[0];
            PReplyInquireSetupInformation pReply = (PReplyInquireSetupInformation)pThis->aReplyBuffer;
            memset(pReply, 0, sizeof(ReplyInquireSetupInformation));
            pReply->fSynchronousInitiationEnabled = true;
            pReply->fParityCheckingEnabled = true;
            pReply->cMailbox = pThis->cMailbox;
            U32_TO_ADDR(pReply->MailboxAddress, pThis->GCPhysAddrMailboxOutgoingBase);
            /* The 'D' signature (actually 'SD' for Storage Dimensions, and 'BD' for BusLogic)
             * prevents Adaptec's OS/2 drivers from getting too friendly with BusLogic hardware
             * and upsetting the HBA state.
             */
            if (pThis->uDevType == DEV_AHA_1540B)
            {
                pReply->uSignature  = 0;    /* Zeros for Adaptec. */
                pReply->uCharacterD = 0;
            }
            else
            {
                pReply->uSignature  = 'B';
                pReply->uCharacterD = 'D';      /* BusLogic model. */
            }
            if (pThis->uDevType == DEV_BT_958D)
                pReply->uHostBusType = 'F';     /* PCI bus. */
            else
                pReply->uHostBusType = 'A';     /* ISA bus. */
            break;
        }
        case BUSLOGICCOMMAND_FETCH_HOST_ADAPTER_LOCAL_RAM:
        {
            /*
             * First element in the command buffer contains start offset to read from
             * and second one the number of bytes to read.
             */
            uint8_t uOffset = pThis->aCommandBuffer[0];
            pThis->cbReplyParametersLeft  = pThis->aCommandBuffer[1];

            pThis->fUseLocalRam = true;
            pThis->iReply = uOffset;
            break;
        }
        case BUSLOGICCOMMAND_INITIALIZE_MAILBOX:
        {
            PRequestInitMbx pRequest = (PRequestInitMbx)pThis->aCommandBuffer;

            pThis->cbReplyParametersLeft = 0;
            if (!pRequest->cMailbox)
            {
                Log(("cMailboxes=%u (24-bit mode), fail!\n", pThis->cMailbox));
                pThis->regStatus |= BL_STAT_CMDINV;
                break;
            }
            pThis->fMbxIs24Bit = true;
            pThis->cMailbox = pRequest->cMailbox;
            pThis->uMailboxOutgoingPositionCurrent = pThis->uMailboxIncomingPositionCurrent = 0;
            pThis->GCPhysAddrMailboxOutgoingBase = (RTGCPHYS)ADDR_TO_U32(pRequest->aMailboxBaseAddr);
            /* The area for incoming mailboxes is right after the last entry of outgoing mailboxes. */
            pThis->GCPhysAddrMailboxIncomingBase = pThis->GCPhysAddrMailboxOutgoingBase + (pThis->cMailbox * sizeof(Mailbox24));

            Log(("GCPhysAddrMailboxOutgoingBase=%RGp\n", pThis->GCPhysAddrMailboxOutgoingBase));
            Log(("GCPhysAddrMailboxIncomingBase=%RGp\n", pThis->GCPhysAddrMailboxIncomingBase));
            Log(("cMailboxes=%u (24-bit mode)\n", pThis->cMailbox));
            LogRel(("Initialized 24-bit mailbox, %d entries at %08x\n", pRequest->cMailbox, ADDR_TO_U32(pRequest->aMailboxBaseAddr)));

            pThis->regStatus &= ~BL_STAT_INREQ;
            break;
        }
        case BUSLOGICCOMMAND_INITIALIZE_EXTENDED_MAILBOX:
        {
            if (pThis->uDevType == DEV_AHA_1540B)
            {
                Log(("Command %#x not valid for this adapter\n", pThis->uOperationCode));
                pThis->cbReplyParametersLeft = 0;
                pThis->regStatus |= BL_STAT_CMDINV;
                break;
            }

            PRequestInitializeExtendedMailbox pRequest = (PRequestInitializeExtendedMailbox)pThis->aCommandBuffer;

            pThis->cbReplyParametersLeft = 0;
            if (!pRequest->cMailbox)
            {
                Log(("cMailboxes=%u (32-bit mode), fail!\n", pThis->cMailbox));
                pThis->regStatus |= BL_STAT_CMDINV;
                break;
            }
            pThis->fMbxIs24Bit = false;
            pThis->cMailbox = pRequest->cMailbox;
            pThis->uMailboxOutgoingPositionCurrent = pThis->uMailboxIncomingPositionCurrent = 0;
            pThis->GCPhysAddrMailboxOutgoingBase = (RTGCPHYS)pRequest->uMailboxBaseAddress;
            /* The area for incoming mailboxes is right after the last entry of outgoing mailboxes. */
            pThis->GCPhysAddrMailboxIncomingBase = (RTGCPHYS)pRequest->uMailboxBaseAddress + (pThis->cMailbox * sizeof(Mailbox32));

            Log(("GCPhysAddrMailboxOutgoingBase=%RGp\n", pThis->GCPhysAddrMailboxOutgoingBase));
            Log(("GCPhysAddrMailboxIncomingBase=%RGp\n", pThis->GCPhysAddrMailboxIncomingBase));
            Log(("cMailboxes=%u (32-bit mode)\n", pThis->cMailbox));
            LogRel(("Initialized 32-bit mailbox, %d entries at %08x\n", pRequest->cMailbox, pRequest->uMailboxBaseAddress));

            pThis->regStatus &= ~BL_STAT_INREQ;
            break;
        }
        case BUSLOGICCOMMAND_ENABLE_STRICT_ROUND_ROBIN_MODE:
        {
            if (pThis->aCommandBuffer[0] == 0)
                pThis->fStrictRoundRobinMode = false;
            else if (pThis->aCommandBuffer[0] == 1)
                pThis->fStrictRoundRobinMode = true;
            else
                AssertMsgFailed(("Invalid round robin mode %d\n", pThis->aCommandBuffer[0]));

            pThis->cbReplyParametersLeft = 0;
            break;
        }
        case BUSLOGICCOMMAND_SET_CCB_FORMAT:
        {
            if (pThis->aCommandBuffer[0] == 0)
                pThis->fExtendedLunCCBFormat = false;
            else if (pThis->aCommandBuffer[0] == 1)
                pThis->fExtendedLunCCBFormat = true;
            else
                AssertMsgFailed(("Invalid CCB format %d\n", pThis->aCommandBuffer[0]));

            pThis->cbReplyParametersLeft = 0;
            break;
        }
        case BUSLOGICCOMMAND_INQUIRE_INSTALLED_DEVICES_ID_0_TO_7:
            /* This is supposed to send TEST UNIT READY to each target/LUN.
             * We cheat and skip that, since we already know what's attached
             */
            memset(pThis->aReplyBuffer, 0, 8);
            for (int i = 0; i < 8; ++i)
            {
                if (pThis->afDevicePresent[i])
                    pThis->aReplyBuffer[i] = 1;
            }
            pThis->aReplyBuffer[7] = 0;     /* HA hardcoded at ID 7. */
            pThis->cbReplyParametersLeft = 8;
            break;
        case BUSLOGICCOMMAND_INQUIRE_INSTALLED_DEVICES_ID_8_TO_15:
            /* See note about cheating above. */
            memset(pThis->aReplyBuffer, 0, 8);
            for (int i = 0; i < 8; ++i)
            {
                if (pThis->afDevicePresent[i + 8])
                    pThis->aReplyBuffer[i] = 1;
            }
            pThis->cbReplyParametersLeft = 8;
            break;
        case BUSLOGICCOMMAND_INQUIRE_TARGET_DEVICES:
        {
            /* Each bit which is set in the 16bit wide variable means a present device. */
            uint16_t u16TargetsPresentMask = 0;

            for (uint8_t i = 0; i < RT_ELEMENTS(pThis->afDevicePresent); i++)
            {
                if (pThis->afDevicePresent[i])
                    u16TargetsPresentMask |= (1 << i);
            }
            pThis->aReplyBuffer[0] = (uint8_t)u16TargetsPresentMask;
            pThis->aReplyBuffer[1] = (uint8_t)(u16TargetsPresentMask >> 8);
            pThis->cbReplyParametersLeft = 2;
            break;
        }
        case BUSLOGICCOMMAND_INQUIRE_SYNCHRONOUS_PERIOD:
        {
            if (pThis->aCommandBuffer[0] > sizeof(pThis->aReplyBuffer))
            {
                Log(("Requested too much synch period inquiry (%u)!\n", pThis->aCommandBuffer[0]));
                pThis->regStatus |= BL_STAT_CMDINV;
                break;
            }
            pThis->cbReplyParametersLeft = pThis->aCommandBuffer[0];
            for (uint8_t i = 0; i < pThis->cbReplyParametersLeft; i++)
                pThis->aReplyBuffer[i] = 0; /** @todo Figure if we need something other here. It's not needed for the linux driver */

            break;
        }
        case BUSLOGICCOMMAND_DISABLE_HOST_ADAPTER_INTERRUPT:
        {
            /* Not supported on AHA-154x HBAs. */
            if (pThis->uDevType == DEV_AHA_1540B)
            {
                Log(("Command %#x not valid for this adapter\n", pThis->uOperationCode));
                pThis->cbReplyParametersLeft = 0;
                pThis->regStatus |= BL_STAT_CMDINV;
                break;
            }

            pThis->cbReplyParametersLeft = 0;
            if (pThis->aCommandBuffer[0] == 0)
                pThis->fIRQEnabled = false;
            else
                pThis->fIRQEnabled = true;
            /* No interrupt signaled regardless of enable/disable. NB: CMDC is still signaled! */
            fSuppressIrq = true;
            break;
        }
        case BUSLOGICCOMMAND_ECHO_COMMAND_DATA:
        {
            pThis->aReplyBuffer[0] = pThis->aCommandBuffer[0];
            pThis->cbReplyParametersLeft = 1;
            break;
        }
        case BUSLOGICCOMMAND_ENABLE_OUTGOING_MAILBOX_AVAILABLE_INTERRUPT:
        {
            uint8_t     uEnable = pThis->aCommandBuffer[0];

            pThis->cbReplyParametersLeft = 0;
            Log(("Enable OMBR: %u\n", uEnable));
            /* Only 0/1 are accepted. */
            if (uEnable > 1)
                pThis->regStatus |= BL_STAT_CMDINV;
            else
            {
                pThis->LocalRam.structured.autoSCSIData.uReserved6 = uEnable;
                fSuppressIrq = true;
                fSuppressCMDC = true;
            }
            break;
        }
        case BUSLOGICCOMMAND_SET_PREEMPT_TIME_ON_BUS:
        {
            pThis->cbReplyParametersLeft = 0;
            pThis->LocalRam.structured.autoSCSIData.uBusOnDelay = pThis->aCommandBuffer[0];
            Log(("Bus-on time: %d\n", pThis->aCommandBuffer[0]));
            break;
        }
        case BUSLOGICCOMMAND_SET_TIME_OFF_BUS:
        {
            pThis->cbReplyParametersLeft = 0;
            pThis->LocalRam.structured.autoSCSIData.uBusOffDelay = pThis->aCommandBuffer[0];
            Log(("Bus-off time: %d\n", pThis->aCommandBuffer[0]));
            break;
        }
        case BUSLOGICCOMMAND_SET_BUS_TRANSFER_RATE:
        {
            pThis->cbReplyParametersLeft = 0;
            pThis->LocalRam.structured.autoSCSIData.uDMATransferRate = pThis->aCommandBuffer[0];
            Log(("Bus transfer rate: %02X\n", pThis->aCommandBuffer[0]));
            break;
        }
        case BUSLOGICCOMMAND_WRITE_BUSMASTER_CHIP_FIFO:
        {
            RTGCPHYS GCPhysFifoBuf;
            Addr24   addr;

            pThis->cbReplyParametersLeft = 0;
            addr.hi  = pThis->aCommandBuffer[0];
            addr.mid = pThis->aCommandBuffer[1];
            addr.lo  = pThis->aCommandBuffer[2];
            GCPhysFifoBuf = (RTGCPHYS)ADDR_TO_U32(addr);
            Log(("Write busmaster FIFO at: %04X\n", ADDR_TO_U32(addr)));
            blPhysReadMeta(pDevIns, pThis, GCPhysFifoBuf, &pThis->LocalRam.u8View[64], 64);
            break;
        }
        case BUSLOGICCOMMAND_READ_BUSMASTER_CHIP_FIFO:
        {
            RTGCPHYS GCPhysFifoBuf;
            Addr24   addr;

            pThis->cbReplyParametersLeft = 0;
            addr.hi  = pThis->aCommandBuffer[0];
            addr.mid = pThis->aCommandBuffer[1];
            addr.lo  = pThis->aCommandBuffer[2];
            GCPhysFifoBuf = (RTGCPHYS)ADDR_TO_U32(addr);
            Log(("Read busmaster FIFO at: %04X\n", ADDR_TO_U32(addr)));
            blPhysWriteMeta(pDevIns, pThis, GCPhysFifoBuf, &pThis->LocalRam.u8View[64], 64);
            break;
        }
        default:
            AssertMsgFailed(("Invalid command %#x\n", pThis->uOperationCode));
            RT_FALL_THRU();
        case BUSLOGICCOMMAND_EXT_BIOS_INFO:
        case BUSLOGICCOMMAND_UNLOCK_MAILBOX:
            /* Commands valid for Adaptec 154xC which we don't handle since
             * we pretend being 154xB compatible. Just mark the command as invalid.
             */
            Log(("Command %#x not valid for this adapter\n", pThis->uOperationCode));
            pThis->cbReplyParametersLeft = 0;
            pThis->regStatus |= BL_STAT_CMDINV;
            break;
        case BUSLOGICCOMMAND_EXECUTE_MAILBOX_COMMAND: /* Should be handled already. */
            AssertMsgFailed(("Invalid mailbox execute state!\n"));
    }

    Log(("uOperationCode=%#x, cbReplyParametersLeft=%d\n", pThis->uOperationCode, pThis->cbReplyParametersLeft));

    /* Fail command if too much parameter data requested. */
    if ((pThis->cbCommandParametersLeft + pThis->iParameter) > sizeof(pThis->aCommandBuffer))
    {
        Log(("Invalid command parameter length (%u)\n", pThis->cbCommandParametersLeft));
        pThis->cbReplyParametersLeft   = 0;
        pThis->cbCommandParametersLeft = 0;
        pThis->regStatus |= BL_STAT_CMDINV;
    }

    if (fCmdComplete)
    {
        /* Set the data in ready bit in the status register in case the command has a reply. */
        if (pThis->cbReplyParametersLeft)
            pThis->regStatus |= BL_STAT_DIRRDY;
        else if (!pThis->cbCommandParametersLeft)
            buslogicCommandComplete(pDevIns, pThis, fSuppressIrq, fSuppressCMDC);
    }

    return rc;
}

/**
 * Read a register from the BusLogic adapter.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the shared BusLogic instance data.
 * @param   iRegister   The index of the register to read.
 * @param   pu32        Where to store the register content.
 */
static int buslogicRegisterRead(PPDMDEVINS pDevIns, PBUSLOGIC pThis, unsigned iRegister, uint32_t *pu32)
{
    static const char s_szAhaSig[] = "ADAP";
    int rc = VINF_SUCCESS;

    switch (iRegister)
    {
        case BUSLOGIC_REGISTER_STATUS:
        {
            *pu32 = pThis->regStatus;

            /* If the diagnostic active bit is set, we are in a guest-initiated
             * hard reset. If the guest reads the status register and waits for
             * the host adapter ready bit to be set, we terminate the reset right
             * away. However, guests may also expect the reset condition to clear
             * automatically after a period of time, in which case we can't show
             * the DIAG bit at all.
             */
            if (pThis->regStatus & BL_STAT_DACT)
            {
                uint64_t    u64AccessTime = PDMDevHlpTMTimeVirtGetNano(pDevIns);

                pThis->regStatus &= ~BL_STAT_DACT;
                pThis->regStatus |= BL_STAT_HARDY;

                if (u64AccessTime - pThis->u64ResetTime > BUSLOGIC_RESET_DURATION_NS)
                {
                    /* If reset already expired, let the guest see that right away. */
                    *pu32 = pThis->regStatus;
                    pThis->u64ResetTime = 0;
                }
            }
            break;
        }
        case BUSLOGIC_REGISTER_DATAIN:
        {
            AssertCompileSize(pThis->LocalRam, 256);
            AssertCompileSize(pThis->iReply, sizeof(uint8_t));
            AssertCompileSize(pThis->cbReplyParametersLeft, sizeof(uint8_t));

            if (pThis->fUseLocalRam)
                *pu32 = pThis->LocalRam.u8View[pThis->iReply];
            else
            {
                /*
                 * Real adapters seem to pad the reply with zeroes and allow up to 255 bytes even
                 * if the real reply is shorter.
                 */
                if (pThis->iReply >= sizeof(pThis->aReplyBuffer))
                    *pu32 = 0;
                else
                    *pu32 = pThis->aReplyBuffer[pThis->iReply];
            }

            /* Careful about underflow - guest can read data register even if
             * no data is available.
             */
            if (pThis->cbReplyParametersLeft)
            {
                pThis->iReply++;
                pThis->cbReplyParametersLeft--;
                if (!pThis->cbReplyParametersLeft)
                {
                    /*
                     * Reply finished, set command complete bit, unset data-in ready bit and
                     * interrupt the guest if enabled.
                     * NB: Some commands do not set the CMDC bit / raise completion interrupt.
                     */
                    if (pThis->uOperationCode == BUSLOGICCOMMAND_FETCH_HOST_ADAPTER_LOCAL_RAM)
                        buslogicCommandComplete(pDevIns, pThis, true /* fSuppressIrq */, true /* fSuppressCMDC */);
                    else
                        buslogicCommandComplete(pDevIns, pThis, false, false);
                }
            }
            LogFlowFunc(("data=%02x, iReply=%d, cbReplyParametersLeft=%u\n", *pu32,
                         pThis->iReply, pThis->cbReplyParametersLeft));
            break;
        }
        case BUSLOGIC_REGISTER_INTERRUPT:
        {
            *pu32 = pThis->regInterrupt;
            break;
        }
        case BUSLOGIC_REGISTER_GEOMETRY:
        {
            if (pThis->uDevType == DEV_AHA_1540B)
            {
                /* NB: The AHA-154xB actually does not respond on this I/O port and
                 * returns 0xFF. However, Adaptec's last ASP4DOS.SYS version (3.36)
                 * assumes the AHA-154xC behavior and fails to load if the 'ADAP'
                 * signature is not present.
                 */
                uint8_t off = pThis->uAhaSigIdx & 3;
                *pu32 = s_szAhaSig[off];
                pThis->uAhaSigIdx = (off + 1) & 3;
            }
            else
                *pu32 = pThis->regGeometry;
            break;
        }
        default:
            *pu32 = UINT32_C(0xffffffff);
    }

    Log2(("%s: pu32=%p:{%.*Rhxs} iRegister=%d rc=%Rrc\n",
          __FUNCTION__, pu32, 1, pu32, iRegister, rc));

    return rc;
}

/**
 * Write a value to a register.
 *
 * @returns VBox status code.
 * @param   pDevIns     The PDM device instance.
 * @param   pThis       Pointer to the shared BusLogic instance data.
 * @param   iRegister   The index of the register to read.
 * @param   uVal        The value to write.
 */
static int buslogicRegisterWrite(PPDMDEVINS pDevIns, PBUSLOGIC pThis, unsigned iRegister, uint8_t uVal)
{
    int rc = VINF_SUCCESS;

    switch (iRegister)
    {
        case BUSLOGIC_REGISTER_CONTROL:
        {
            if ((uVal & BL_CTRL_RHARD) || (uVal & BL_CTRL_RSOFT))
            {
#ifdef IN_RING3
                bool    fHardReset = !!(uVal & BL_CTRL_RHARD);

                LogRel(("BusLogic: %s reset\n", fHardReset ? "hard" : "soft"));
                buslogicR3InitiateReset(pDevIns, pThis, fHardReset);
#else
                rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                break;
            }

            rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSectIntr, VINF_IOM_R3_IOPORT_WRITE);
            if (rc != VINF_SUCCESS)
                return rc;

#ifdef LOG_ENABLED
            uint32_t cMailboxesReady = ASMAtomicXchgU32(&pThis->cInMailboxesReadyIfLogEnabled, 0);
            Log(("%u incoming mailboxes were ready when this interrupt was cleared\n", cMailboxesReady));
#endif

            if (uVal & BL_CTRL_RINT)
                buslogicClearInterrupt(pDevIns, pThis);

            PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSectIntr);

            break;
        }
        case BUSLOGIC_REGISTER_COMMAND:
        {
            /* Fast path for mailbox execution command. */
            if ((uVal == BUSLOGICCOMMAND_EXECUTE_MAILBOX_COMMAND) && (pThis->uOperationCode == 0xff))
            {
                /// @todo Should fail if BL_STAT_INREQ is set
                /* If there are no mailboxes configured, don't even try to do anything. */
                if (pThis->cMailbox)
                {
                    ASMAtomicIncU32(&pThis->cMailboxesReady);
                    if (!ASMAtomicXchgBool(&pThis->fNotificationSent, true))
                    {
                        /* Wake up the worker thread. */
                        int rc2 = PDMDevHlpSUPSemEventSignal(pDevIns, pThis->hEvtProcess);
                        AssertRC(rc2);
                    }
                }

                return rc;
            }

            /*
             * Check if we are already fetch command parameters from the guest.
             * If not we initialize executing a new command.
             */
            if (pThis->uOperationCode == 0xff)
            {
                pThis->uOperationCode = uVal;
                pThis->iParameter = 0;

                /* Mark host adapter as busy and clear the invalid status bit. */
                pThis->regStatus &= ~(BL_STAT_HARDY | BL_STAT_CMDINV);

                /* Get the number of bytes for parameters from the command code. */
                switch (pThis->uOperationCode)
                {
                    case BUSLOGICCOMMAND_TEST_CMDC_INTERRUPT:
                    case BUSLOGICCOMMAND_INQUIRE_FIRMWARE_VERSION_LETTER:
                    case BUSLOGICCOMMAND_INQUIRE_BOARD_ID:
                    case BUSLOGICCOMMAND_INQUIRE_FIRMWARE_VERSION_3RD_LETTER:
                    case BUSLOGICCOMMAND_INQUIRE_PCI_HOST_ADAPTER_INFORMATION:
                    case BUSLOGICCOMMAND_INQUIRE_CONFIGURATION:
                    case BUSLOGICCOMMAND_INQUIRE_INSTALLED_DEVICES_ID_0_TO_7:
                    case BUSLOGICCOMMAND_INQUIRE_INSTALLED_DEVICES_ID_8_TO_15:
                    case BUSLOGICCOMMAND_INQUIRE_TARGET_DEVICES:
                        pThis->cbCommandParametersLeft = 0;
                        break;
                    case BUSLOGICCOMMAND_MODIFY_IO_ADDRESS:
                    case BUSLOGICCOMMAND_INQUIRE_EXTENDED_SETUP_INFORMATION:
                    case BUSLOGICCOMMAND_DISABLE_HOST_ADAPTER_INTERRUPT:
                    case BUSLOGICCOMMAND_INQUIRE_HOST_ADAPTER_MODEL_NUMBER:
                        /* These commands are not on AHA-154x, some Adaptec drivers (ASPI4DOS.SYS) test them. */
                        if (pThis->uDevType == DEV_AHA_1540B)
                        {
                            pThis->cbCommandParametersLeft = 0;
                            break;
                        }
                        RT_FALL_THRU();
                    case BUSLOGICCOMMAND_INQUIRE_SETUP_INFORMATION:
                    case BUSLOGICCOMMAND_ENABLE_STRICT_ROUND_ROBIN_MODE:
                    case BUSLOGICCOMMAND_SET_CCB_FORMAT:
                    case BUSLOGICCOMMAND_INQUIRE_SYNCHRONOUS_PERIOD:
                    case BUSLOGICCOMMAND_ECHO_COMMAND_DATA:
                    case BUSLOGICCOMMAND_ENABLE_OUTGOING_MAILBOX_AVAILABLE_INTERRUPT:
                    case BUSLOGICCOMMAND_SET_PREEMPT_TIME_ON_BUS:
                    case BUSLOGICCOMMAND_SET_TIME_OFF_BUS:
                    case BUSLOGICCOMMAND_SET_BUS_TRANSFER_RATE:
                        pThis->cbCommandParametersLeft = 1;
                        break;
                    case BUSLOGICCOMMAND_FETCH_HOST_ADAPTER_LOCAL_RAM:
                        pThis->cbCommandParametersLeft = 2;
                        break;
                    case BUSLOGICCOMMAND_READ_BUSMASTER_CHIP_FIFO:
                    case BUSLOGICCOMMAND_WRITE_BUSMASTER_CHIP_FIFO:
                        pThis->cbCommandParametersLeft = 3;
                        break;
                    case BUSLOGICCOMMAND_SET_SCSI_SELECTION_TIMEOUT:
                        pThis->cbCommandParametersLeft = 4;
                        break;
                    case BUSLOGICCOMMAND_INITIALIZE_MAILBOX:
                        pThis->cbCommandParametersLeft = sizeof(RequestInitMbx);
                        break;
                    case BUSLOGICCOMMAND_INITIALIZE_EXTENDED_MAILBOX:
                        /* Some Adaptec drivers (ASPI4DOS.SYS) test this command. */
                        if (pThis->uDevType == DEV_AHA_1540B)
                        {
                            pThis->cbCommandParametersLeft = 0;
                            break;
                        }
                        pThis->cbCommandParametersLeft = sizeof(RequestInitializeExtendedMailbox);
                        break;
                    case BUSLOGICCOMMAND_SET_ADAPTER_OPTIONS:
                        /* There must be at least one byte following this command. */
                        pThis->cbCommandParametersLeft = 1;
                        break;
                    case BUSLOGICCOMMAND_EXECUTE_SCSI_COMMAND:
                        /* 12 bytes + variable-length CDB. */
                        pThis->cbCommandParametersLeft = 12;
                        break;
                    case BUSLOGICCOMMAND_EXT_BIOS_INFO:
                    case BUSLOGICCOMMAND_UNLOCK_MAILBOX:
                        /* Invalid commands. */
                        pThis->cbCommandParametersLeft = 0;
                        break;
                    case BUSLOGICCOMMAND_EXECUTE_MAILBOX_COMMAND: /* Should not come here anymore. */
                    default:
                        AssertMsgFailed(("Invalid operation code %#x\n", uVal));
                }
            }
            else if (pThis->cbCommandParametersLeft)
            {
#ifndef IN_RING3
                /* This command must be executed in R3 as it rehooks the ISA I/O port. */
                if (pThis->uOperationCode == BUSLOGICCOMMAND_MODIFY_IO_ADDRESS)
                {
                    rc = VINF_IOM_R3_IOPORT_WRITE;
                    break;
                }
#endif
                /*
                 * The real adapter would set the Command register busy bit in the status register.
                 * The guest has to wait until it is unset.
                 * We don't need to do it because the guest does not continue execution while we are in this
                 * function.
                 */
                pThis->aCommandBuffer[pThis->iParameter] = uVal;
                pThis->iParameter++;
                pThis->cbCommandParametersLeft--;
            }

            /* Start execution of command if there are no parameters left. */
            if (!pThis->cbCommandParametersLeft)
            {
                rc = buslogicProcessCommand(pDevIns, pThis);
                AssertMsgRC(rc, ("Processing command failed rc=%Rrc\n", rc));
            }
            break;
        }

        /* On BusLogic adapters, the interrupt and geometry registers are R/W.
         * That is different from Adaptec 154x where those are read only.
         */
        case BUSLOGIC_REGISTER_INTERRUPT:
            if (pThis->uDevType == DEV_AHA_1540B)
                break;
            pThis->regInterrupt = uVal;
            break;

        case BUSLOGIC_REGISTER_GEOMETRY:
            if (pThis->uDevType == DEV_AHA_1540B)
                break;
            pThis->regGeometry = uVal;
            break;

        default:
            AssertMsgFailed(("Register not available\n"));
            rc = VERR_IOM_IOPORT_UNUSED;
    }

    return rc;
}

/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) buslogicMMIORead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    RT_NOREF(pDevIns, pvUser, off, pv, cb);

    /* the linux driver does not make use of the MMIO area. */
    ASSERT_GUEST_MSG_FAILED(("MMIO Read: %RGp LB %u\n", off, cb));
    return VINF_IOM_MMIO_UNUSED_FF;
}

/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) buslogicMMIOWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    RT_NOREF(pDevIns, pvUser, off, pv, cb);

    /* the linux driver does not make use of the MMIO area. */
    ASSERT_GUEST_MSG_FAILED(("MMIO Write: %RGp LB %u: %.*Rhxs\n", off, cb, cb, pv));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 */
static DECLCALLBACK(VBOXSTRICTRC)
buslogicIOPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PBUSLOGIC pThis = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    unsigned iRegister = offPort % 4;
    RT_NOREF(pvUser, cb);

    ASSERT_GUEST(cb == 1);

    return buslogicRegisterRead(pDevIns, pThis, iRegister, pu32);
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC)
buslogicIOPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PBUSLOGIC pThis = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    unsigned iRegister = offPort % 4;
    RT_NOREF(pvUser, cb);

    ASSERT_GUEST(cb == 1);

    int rc = buslogicRegisterWrite(pDevIns, pThis, iRegister, (uint8_t)u32);

    Log2(("#%d %s: pvUser=%#p cb=%d u32=%#x offPort=%#x rc=%Rrc\n",
          pDevIns->iInstance, __FUNCTION__, pvUser, cb, u32, offPort, rc));

    return rc;
}

#ifdef IN_RING3

/**
 * Update the ISA I/O range.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pThis           Pointer to the shared BusLogic instance data.
 * @param   uBaseCode       Encoded ISA I/O base; only low 3 bits are used.
 */
static int buslogicR3RegisterISARange(PPDMDEVINS pDevIns, PBUSLOGIC pThis, uint8_t uBaseCode)
{
    uint8_t     uCode = uBaseCode & MAX_ISA_BASE;
    uint16_t    uNewBase = g_aISABases[uCode];
    int         rc = VINF_SUCCESS;

    LogFlowFunc(("ISA I/O code %02X, new base %X\n", uBaseCode, uNewBase));

    /* Check if the same port range actually changed. */
    if (uNewBase != pThis->IOISABase)
    {
        /* Unmap the old range, if necessary. */
        if (pThis->IOISABase)
        {
            rc = PDMDevHlpIoPortUnmap(pDevIns, pThis->hIoPortsIsa);
            AssertRC(rc);
        }
        if (RT_SUCCESS(rc))
        {
            pThis->IOISABase = 0;   /* First mark as unregistered. */
            pThis->uISABaseCode = ISA_BASE_DISABLED;

            if (uNewBase)
            {
                /* Register the new range if requested. */
                rc = PDMDevHlpIoPortMap(pDevIns, pThis->hIoPortsIsa, uNewBase);
                if (RT_SUCCESS(rc))
                {
                    pThis->IOISABase = uNewBase;
                    pThis->uISABaseCode = uCode;
                }
            }
        }
        if (RT_SUCCESS(rc))
        {
            if (uNewBase)
            {
                Log(("ISA I/O base: %x\n", uNewBase));
                LogRel(("BusLogic: ISA I/O base: %x\n", uNewBase));
            }
            else
            {
                Log(("Disabling ISA I/O ports.\n"));
                LogRel(("BusLogic: ISA I/O disabled\n"));
            }
        }

    }
    return rc;
}

/**
 * Completes a request initiated by the BIOS through the BUSLOGICCOMMAND_EXECUTE_SCSI_COMMAND command.
 *
 * @param   pThis       Pointer to the shared BusLogic instance data.
 * @param   u8ScsiSts   The SCSI status code.
 */
static void buslogicR3ReqCompleteBios(PBUSLOGIC pThis, uint8_t u8ScsiSts)
{
    pThis->cbReplyParametersLeft = 4;
    pThis->aReplyBuffer[0] = pThis->aReplyBuffer[1] = 0;
    pThis->aReplyBuffer[2] = u8ScsiSts;
    pThis->aReplyBuffer[3] = 0;

    pThis->regStatus |= BL_STAT_DIRRDY;
}

static int buslogicR3ReqComplete(PPDMDEVINS pDevIns, PBUSLOGIC pThis, PBUSLOGICCC pThisCC, PBUSLOGICREQ pReq, int rcReq)
{
    RT_NOREF(rcReq);
    PBUSLOGICDEVICE pTgtDev = pReq->pTargetDevice;

    LogFlowFunc(("before decrement %u\n", pTgtDev->cOutstandingRequests));
    ASMAtomicDecU32(&pTgtDev->cOutstandingRequests);
    LogFlowFunc(("after decrement %u\n", pTgtDev->cOutstandingRequests));

    if (pReq->fBIOS)
    {
        uint8_t u8ScsiSts = pReq->u8ScsiSts;
        pTgtDev->pDrvMediaEx->pfnIoReqFree(pTgtDev->pDrvMediaEx, pReq->hIoReq);
        buslogicR3ReqCompleteBios(pThis, u8ScsiSts);
    }
    else
    {
        if (pReq->pbSenseBuffer)
            buslogicR3SenseBufferFree(pReq, (pReq->u8ScsiSts != SCSI_STATUS_OK));

        /* Update residual data length. */
        if (   (pReq->CCBGuest.c.uOpcode == BUSLOGIC_CCB_OPCODE_INITIATOR_CCB_RESIDUAL_DATA_LENGTH)
            || (pReq->CCBGuest.c.uOpcode == BUSLOGIC_CCB_OPCODE_INITIATOR_CCB_RESIDUAL_SCATTER_GATHER))
        {
            size_t cbResidual = 0;
            int rc = pTgtDev->pDrvMediaEx->pfnIoReqQueryResidual(pTgtDev->pDrvMediaEx, pReq->hIoReq, &cbResidual);
            AssertRC(rc); Assert(cbResidual == (uint32_t)cbResidual);

            if (pReq->fIs24Bit)
                U32_TO_LEN(pReq->CCBGuest.o.acbData, (uint32_t)cbResidual);
            else
                pReq->CCBGuest.n.cbData = (uint32_t)cbResidual;
        }

        /*
         * Save vital things from the request and free it before posting completion
         * to avoid that the guest submits a new request with the same ID as the still
         * allocated one.
         */
#ifdef LOG_ENABLED
        bool fIs24Bit = pReq->fIs24Bit;
#endif
        uint8_t u8ScsiSts = pReq->u8ScsiSts;
        RTGCPHYS GCPhysAddrCCB = pReq->GCPhysAddrCCB;
        CCBU CCBGuest;
        memcpy(&CCBGuest, &pReq->CCBGuest, sizeof(CCBU));

        pTgtDev->pDrvMediaEx->pfnIoReqFree(pTgtDev->pDrvMediaEx, pReq->hIoReq);
        if (u8ScsiSts == SCSI_STATUS_OK)
            buslogicR3SendIncomingMailbox(pDevIns, pThis, GCPhysAddrCCB, &CCBGuest,
                                          BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_CMD_COMPLETED,
                                          BUSLOGIC_MAILBOX_INCOMING_DEVICE_STATUS_OPERATION_GOOD,
                                          BUSLOGIC_MAILBOX_INCOMING_COMPLETION_WITHOUT_ERROR);
        else if (u8ScsiSts == SCSI_STATUS_CHECK_CONDITION)
            buslogicR3SendIncomingMailbox(pDevIns, pThis, GCPhysAddrCCB, &CCBGuest,
                                          BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_CMD_COMPLETED,
                                          BUSLOGIC_MAILBOX_INCOMING_DEVICE_STATUS_CHECK_CONDITION,
                                          BUSLOGIC_MAILBOX_INCOMING_COMPLETION_WITH_ERROR);
        else
            AssertMsgFailed(("invalid completion status %u\n", u8ScsiSts));

#ifdef LOG_ENABLED
        buslogicR3DumpCCBInfo(&CCBGuest, fIs24Bit);
#endif
    }

    if (pTgtDev->cOutstandingRequests == 0 && pThisCC->fSignalIdle)
        PDMDevHlpAsyncNotificationCompleted(pDevIns);

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAPORT,pfnQueryDeviceLocation}
 */
static DECLCALLBACK(int) buslogicR3QueryDeviceLocation(PPDMIMEDIAPORT pInterface, const char **ppcszController,
                                                       uint32_t *piInstance, uint32_t *piLUN)
{
    PBUSLOGICDEVICE pTgtDev = RT_FROM_MEMBER(pInterface, BUSLOGICDEVICE, IMediaPort);
    PPDMDEVINS      pDevIns = pTgtDev->pDevIns;

    AssertPtrReturn(ppcszController, VERR_INVALID_POINTER);
    AssertPtrReturn(piInstance, VERR_INVALID_POINTER);
    AssertPtrReturn(piLUN, VERR_INVALID_POINTER);

    *ppcszController = pDevIns->pReg->szName;
    *piInstance = pDevIns->iInstance;
    *piLUN = pTgtDev->iLUN;

    return VINF_SUCCESS;
}

static DECLCALLBACK(size_t) buslogicR3CopySgToGuestBios(PCRTSGBUF pSgBuf, const void *pvSrc, size_t cbSrc, void *pvUser)
{
    PBUSLOGICCOPYARGS pArgs = (PBUSLOGICCOPYARGS)pvUser;
    size_t cbThisCopy = RT_MIN(cbSrc, pArgs->pCmd->cbData - pArgs->cbCopied);
    RT_NOREF(pSgBuf);

    blPhysWriteUser(pArgs->pDevIns, pArgs->pThis, pArgs->pCmd->u32PhysAddrData + pArgs->cbCopied, pvSrc, cbThisCopy);
    pArgs->cbCopied += cbThisCopy;
    return cbThisCopy;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCopyFromBuf}
 */
static DECLCALLBACK(int) buslogicR3IoReqCopyFromBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                    void *pvIoReqAlloc, uint32_t offDst, PRTSGBUF pSgBuf,
                                                    size_t cbCopy)
{
    PBUSLOGICDEVICE pTgtDev = RT_FROM_MEMBER(pInterface, BUSLOGICDEVICE, IMediaExPort);
    PPDMDEVINS      pDevIns = pTgtDev->pDevIns;
    PBUSLOGICREQ    pReq    = (PBUSLOGICREQ)pvIoReqAlloc;
    RT_NOREF(hIoReq);

    size_t cbCopied = 0;
    if (RT_LIKELY(!pReq->fBIOS))
        cbCopied = buslogicR3CopySgBufToGuest(pDevIns, PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC), pReq, pSgBuf, offDst, cbCopy);
    else
    {
        BUSLOGICCOPYARGS Args;
        PBUSLOGIC pThis = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
        PESCMD pCmd = (PESCMD)pThis->aCommandBuffer;

        Args.pCmd     = pCmd;
        Args.pThis    = pThis;
        Args.pDevIns  = pDevIns;
        Args.cbCopied = 0;
        cbCopied = RTSgBufCopyToFn(pSgBuf, RT_MIN(pCmd->cbData, cbCopy), buslogicR3CopySgToGuestBios, &Args);
    }
    return cbCopied == cbCopy ? VINF_SUCCESS : VERR_PDM_MEDIAEX_IOBUF_OVERFLOW;
}

static DECLCALLBACK(size_t) buslogicR3CopySgFromGuestBios(PCRTSGBUF pSgBuf, void *pvDst, size_t cbDst, void *pvUser)
{
    PBUSLOGICCOPYARGS pArgs = (PBUSLOGICCOPYARGS)pvUser;
    size_t cbThisCopy = RT_MIN(cbDst, pArgs->pCmd->cbData - pArgs->cbCopied);
    RT_NOREF(pSgBuf);

    blPhysReadUser(pArgs->pDevIns, pArgs->pThis, pArgs->pCmd->u32PhysAddrData + pArgs->cbCopied, pvDst, cbThisCopy);
    pArgs->cbCopied += cbThisCopy;
    return cbThisCopy;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCopyToBuf}
 */
static DECLCALLBACK(int) buslogicR3IoReqCopyToBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                  void *pvIoReqAlloc, uint32_t offSrc, PRTSGBUF pSgBuf,
                                                  size_t cbCopy)
{
    RT_NOREF(hIoReq);
    PBUSLOGICDEVICE pTgtDev = RT_FROM_MEMBER(pInterface, BUSLOGICDEVICE, IMediaExPort);
    PPDMDEVINS      pDevIns = pTgtDev->pDevIns;
    PBUSLOGICREQ    pReq    = (PBUSLOGICREQ)pvIoReqAlloc;

    size_t cbCopied = 0;
    if (RT_LIKELY(!pReq->fBIOS))
        cbCopied = buslogicR3CopySgBufFromGuest(pDevIns, PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC), pReq, pSgBuf, offSrc, cbCopy);
    else
    {
        BUSLOGICCOPYARGS Args;
        PBUSLOGIC pThis = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
        PESCMD pCmd = (PESCMD)pThis->aCommandBuffer;

        Args.pCmd     = pCmd;
        Args.pThis    = pThis;
        Args.pDevIns  = pDevIns;
        Args.cbCopied = 0;
        cbCopied = RTSgBufCopyFromFn(pSgBuf, RT_MIN(pCmd->cbData, cbCopy), buslogicR3CopySgFromGuestBios, &Args);
    }

    return cbCopied == cbCopy ? VINF_SUCCESS : VERR_PDM_MEDIAEX_IOBUF_UNDERRUN;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCompleteNotify}
 */
static DECLCALLBACK(int) buslogicR3IoReqCompleteNotify(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                       void *pvIoReqAlloc, int rcReq)
{
    RT_NOREF(hIoReq);
    PBUSLOGICDEVICE pTgtDev = RT_FROM_MEMBER(pInterface, BUSLOGICDEVICE, IMediaExPort);
    PPDMDEVINS      pDevIns = pTgtDev->pDevIns;
    buslogicR3ReqComplete(pDevIns, PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC), PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC),
                          (PBUSLOGICREQ)pvIoReqAlloc, rcReq);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqStateChanged}
 */
static DECLCALLBACK(void) buslogicR3IoReqStateChanged(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                      void *pvIoReqAlloc, PDMMEDIAEXIOREQSTATE enmState)
{
    RT_NOREF(hIoReq, pvIoReqAlloc, enmState);
    PBUSLOGICDEVICE pTgtDev = RT_FROM_MEMBER(pInterface, BUSLOGICDEVICE, IMediaExPort);

    switch (enmState)
    {
        case PDMMEDIAEXIOREQSTATE_SUSPENDED:
        {
            PPDMDEVINS  pDevIns = pTgtDev->pDevIns;
            PBUSLOGICCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);

            /* Make sure the request is not accounted for so the VM can suspend successfully. */
            uint32_t cTasksActive = ASMAtomicDecU32(&pTgtDev->cOutstandingRequests);
            if (!cTasksActive && pThisCC->fSignalIdle)
                PDMDevHlpAsyncNotificationCompleted(pDevIns);
            break;
        }
        case PDMMEDIAEXIOREQSTATE_ACTIVE:
            /* Make sure the request is accounted for so the VM suspends only when the request is complete. */
            ASMAtomicIncU32(&pTgtDev->cOutstandingRequests);
            break;
        default:
            AssertMsgFailed(("Invalid request state given %u\n", enmState));
    }
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnMediumEjected}
 */
static DECLCALLBACK(void) buslogicR3MediumEjected(PPDMIMEDIAEXPORT pInterface)
{
    PBUSLOGICDEVICE pTgtDev = RT_FROM_MEMBER(pInterface, BUSLOGICDEVICE, IMediaExPort);
    PPDMDEVINS      pDevIns = pTgtDev->pDevIns;
    PBUSLOGICCC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);

    if (pThisCC->pMediaNotify)
    {
        int rc = PDMDevHlpVMReqCallNoWait(pDevIns, VMCPUID_ANY,
                                          (PFNRT)pThisCC->pMediaNotify->pfnEjected, 2,
                                          pThisCC->pMediaNotify, pTgtDev->iLUN);
        AssertRC(rc);
    }
}

static int buslogicR3DeviceSCSIRequestSetup(PPDMDEVINS pDevIns, PBUSLOGIC pThis, PBUSLOGICCC pThisCC, RTGCPHYS GCPhysAddrCCB)
{
    int rc = VINF_SUCCESS;
    uint8_t uTargetIdCCB;
    CCBU CCBGuest;

    /* Fetch the CCB from guest memory. */
    /** @todo How much do we really have to read? */
    blPhysReadMeta(pDevIns, pThis, GCPhysAddrCCB, &CCBGuest, sizeof(CCB32));

    uTargetIdCCB = pThis->fMbxIs24Bit ? CCBGuest.o.uTargetId : CCBGuest.n.uTargetId;
    if (   RT_LIKELY(uTargetIdCCB < RT_ELEMENTS(pThisCC->aDeviceStates))
        && CCBGuest.c.cbCDB <= RT_ELEMENTS(CCBGuest.c.abCDB))
    {
        PBUSLOGICDEVICE pTgtDev = &pThisCC->aDeviceStates[uTargetIdCCB];

#ifdef LOG_ENABLED
        buslogicR3DumpCCBInfo(&CCBGuest, pThis->fMbxIs24Bit);
#endif

        /* Check if device is present on bus. If not return error immediately and don't process this further. */
        if (RT_LIKELY(pTgtDev->fPresent))
        {
            PDMMEDIAEXIOREQ hIoReq;
            PBUSLOGICREQ pReq;
            rc = pTgtDev->pDrvMediaEx->pfnIoReqAlloc(pTgtDev->pDrvMediaEx, &hIoReq, (void **)&pReq,
                                                     GCPhysAddrCCB, PDMIMEDIAEX_F_SUSPEND_ON_RECOVERABLE_ERR);
            if (RT_SUCCESS(rc))
            {
                pReq->pTargetDevice = pTgtDev;
                pReq->GCPhysAddrCCB = GCPhysAddrCCB;
                pReq->fBIOS         = false;
                pReq->hIoReq        = hIoReq;
                pReq->fIs24Bit      = pThis->fMbxIs24Bit;

                /* Make a copy of the CCB */
                memcpy(&pReq->CCBGuest, &CCBGuest, sizeof(CCBGuest));

                /* Alloc required buffers. */
                rc = buslogicR3SenseBufferAlloc(pReq);
                AssertMsgRC(rc, ("Mapping sense buffer failed rc=%Rrc\n", rc));

                size_t cbBuf = 0;
                rc = buslogicR3QueryDataBufferSize(pDevIns, &pReq->CCBGuest, pReq->fIs24Bit, &cbBuf);
                AssertRC(rc);

                uint32_t uLun = pReq->fIs24Bit ? pReq->CCBGuest.o.uLogicalUnit
                                               : pReq->CCBGuest.n.uLogicalUnit;

                PDMMEDIAEXIOREQSCSITXDIR enmXferDir = PDMMEDIAEXIOREQSCSITXDIR_UNKNOWN;
                size_t cbSense = buslogicR3ConvertSenseBufferLength(CCBGuest.c.cbSenseData);

                if (CCBGuest.c.uDataDirection == BUSLOGIC_CCB_DIRECTION_NO_DATA)
                    enmXferDir = PDMMEDIAEXIOREQSCSITXDIR_NONE;
                else if (CCBGuest.c.uDataDirection == BUSLOGIC_CCB_DIRECTION_OUT)
                    enmXferDir = PDMMEDIAEXIOREQSCSITXDIR_TO_DEVICE;
                else if (CCBGuest.c.uDataDirection == BUSLOGIC_CCB_DIRECTION_IN)
                    enmXferDir = PDMMEDIAEXIOREQSCSITXDIR_FROM_DEVICE;

                ASMAtomicIncU32(&pTgtDev->cOutstandingRequests);
                rc = pTgtDev->pDrvMediaEx->pfnIoReqSendScsiCmd(pTgtDev->pDrvMediaEx, pReq->hIoReq, uLun,
                                                               &pReq->CCBGuest.c.abCDB[0], pReq->CCBGuest.c.cbCDB,
                                                               enmXferDir, NULL, cbBuf, pReq->pbSenseBuffer, cbSense, NULL,
                                                               &pReq->u8ScsiSts, 30 * RT_MS_1SEC);
                if (rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
                    buslogicR3ReqComplete(pDevIns, pThis, pThisCC, pReq, rc);
            }
            else
                buslogicR3SendIncomingMailbox(pDevIns, pThis, GCPhysAddrCCB, &CCBGuest,
                                              BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_SCSI_SELECTION_TIMEOUT,
                                              BUSLOGIC_MAILBOX_INCOMING_DEVICE_STATUS_OPERATION_GOOD,
                                              BUSLOGIC_MAILBOX_INCOMING_COMPLETION_WITH_ERROR);
        }
        else
            buslogicR3SendIncomingMailbox(pDevIns, pThis, GCPhysAddrCCB, &CCBGuest,
                                          BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_SCSI_SELECTION_TIMEOUT,
                                          BUSLOGIC_MAILBOX_INCOMING_DEVICE_STATUS_OPERATION_GOOD,
                                          BUSLOGIC_MAILBOX_INCOMING_COMPLETION_WITH_ERROR);
    }
    else
        buslogicR3SendIncomingMailbox(pDevIns, pThis, GCPhysAddrCCB, &CCBGuest,
                                      BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_INVALID_COMMAND_PARAMETER,
                                      BUSLOGIC_MAILBOX_INCOMING_DEVICE_STATUS_OPERATION_GOOD,
                                      BUSLOGIC_MAILBOX_INCOMING_COMPLETION_WITH_ERROR);

    return rc;
}

static int buslogicR3DeviceSCSIRequestAbort(PPDMDEVINS pDevIns, PBUSLOGIC pThis, RTGCPHYS GCPhysAddrCCB)
{
    uint8_t  uTargetIdCCB;
    CCBU     CCBGuest;

    blPhysReadMeta(pDevIns, pThis, GCPhysAddrCCB, &CCBGuest, sizeof(CCB32));

    uTargetIdCCB = pThis->fMbxIs24Bit ? CCBGuest.o.uTargetId : CCBGuest.n.uTargetId;
    if (RT_LIKELY(uTargetIdCCB < RT_ELEMENTS(pThis->afDevicePresent)))
        buslogicR3SendIncomingMailbox(pDevIns, pThis, GCPhysAddrCCB, &CCBGuest,
                                      BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_ABORT_QUEUE_GENERATED,
                                      BUSLOGIC_MAILBOX_INCOMING_DEVICE_STATUS_OPERATION_GOOD,
                                      BUSLOGIC_MAILBOX_INCOMING_COMPLETION_ABORTED_NOT_FOUND);
    else
        buslogicR3SendIncomingMailbox(pDevIns, pThis, GCPhysAddrCCB, &CCBGuest,
                                      BUSLOGIC_MAILBOX_INCOMING_ADAPTER_STATUS_INVALID_COMMAND_PARAMETER,
                                      BUSLOGIC_MAILBOX_INCOMING_DEVICE_STATUS_OPERATION_GOOD,
                                      BUSLOGIC_MAILBOX_INCOMING_COMPLETION_WITH_ERROR);

    return VINF_SUCCESS;
}

/**
 * Read a mailbox from guest memory. Convert 24-bit mailboxes to
 * 32-bit format.
 *
 * @returns Mailbox guest physical address.
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the shared BusLogic instance data.
 * @param   pMbx        Pointer to the mailbox to read into.
 */
static RTGCPHYS buslogicR3ReadOutgoingMailbox(PPDMDEVINS pDevIns, PBUSLOGIC pThis, PMailbox32 pMbx)
{
    RTGCPHYS    GCMailbox;

    if (pThis->fMbxIs24Bit)
    {
        Mailbox24   Mbx24;

        GCMailbox = pThis->GCPhysAddrMailboxOutgoingBase + (pThis->uMailboxOutgoingPositionCurrent * sizeof(Mailbox24));
        blPhysReadMeta(pDevIns, pThis, GCMailbox, &Mbx24, sizeof(Mailbox24));
        pMbx->u32PhysAddrCCB    = ADDR_TO_U32(Mbx24.aPhysAddrCCB);
        pMbx->u.out.uActionCode = Mbx24.uCmdState;
    }
    else
    {
        GCMailbox = pThis->GCPhysAddrMailboxOutgoingBase + (pThis->uMailboxOutgoingPositionCurrent * sizeof(Mailbox32));
        blPhysReadMeta(pDevIns, pThis, GCMailbox, pMbx, sizeof(Mailbox32));
    }

    return GCMailbox;
}

/**
 * Read mailbox from the guest and execute command.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the shared BusLogic instance data.
 * @param   pThisCC     Pointer to the ring-3 BusLogic instance data.
 */
static int buslogicR3ProcessMailboxNext(PPDMDEVINS pDevIns, PBUSLOGIC pThis, PBUSLOGICCC pThisCC)
{
    RTGCPHYS     GCPhysAddrMailboxCurrent;
    Mailbox32    MailboxGuest;
    int rc = VINF_SUCCESS;

    if (!pThis->fStrictRoundRobinMode)
    {
        /* Search for a filled mailbox - stop if we have scanned all mailboxes. */
        uint8_t uMailboxPosCur = pThis->uMailboxOutgoingPositionCurrent;

        do
        {
            /* Fetch mailbox from guest memory. */
            GCPhysAddrMailboxCurrent = buslogicR3ReadOutgoingMailbox(pDevIns, pThis, &MailboxGuest);

            /* Check the next mailbox. */
            buslogicR3OutgoingMailboxAdvance(pThis);
        } while (   MailboxGuest.u.out.uActionCode == BUSLOGIC_MAILBOX_OUTGOING_ACTION_FREE
                 && uMailboxPosCur != pThis->uMailboxOutgoingPositionCurrent);
    }
    else
    {
        /* Fetch mailbox from guest memory. */
        GCPhysAddrMailboxCurrent = buslogicR3ReadOutgoingMailbox(pDevIns, pThis, &MailboxGuest);
    }

    /*
     * Check if the mailbox is actually loaded.
     * It might be possible that the guest notified us without
     * a loaded mailbox. Do nothing in that case but leave a
     * log entry.
     */
    if (MailboxGuest.u.out.uActionCode == BUSLOGIC_MAILBOX_OUTGOING_ACTION_FREE)
    {
        Log(("No loaded mailbox left\n"));
        return VERR_NO_DATA;
    }

    LogFlow(("Got loaded mailbox at slot %u, CCB phys %RGp\n", pThis->uMailboxOutgoingPositionCurrent, (RTGCPHYS)MailboxGuest.u32PhysAddrCCB));
#ifdef LOG_ENABLED
    buslogicR3DumpMailboxInfo(&MailboxGuest, true);
#endif

    /* We got the mailbox, mark it as free in the guest. */
    uint8_t uActionCode = BUSLOGIC_MAILBOX_OUTGOING_ACTION_FREE;
    unsigned uCodeOffs = pThis->fMbxIs24Bit ? RT_OFFSETOF(Mailbox24, uCmdState) : RT_OFFSETOF(Mailbox32, u.out.uActionCode);
    blPhysWriteMeta(pDevIns, pThis, GCPhysAddrMailboxCurrent + uCodeOffs, &uActionCode, sizeof(uActionCode));

    if (MailboxGuest.u.out.uActionCode == BUSLOGIC_MAILBOX_OUTGOING_ACTION_START_COMMAND)
        rc = buslogicR3DeviceSCSIRequestSetup(pDevIns, pThis, pThisCC, (RTGCPHYS)MailboxGuest.u32PhysAddrCCB);
    else if (MailboxGuest.u.out.uActionCode == BUSLOGIC_MAILBOX_OUTGOING_ACTION_ABORT_COMMAND)
    {
        LogFlow(("Aborting mailbox\n"));
        rc = buslogicR3DeviceSCSIRequestAbort(pDevIns, pThis, (RTGCPHYS)MailboxGuest.u32PhysAddrCCB);
    }
    else
    {
        AssertMsgFailed(("Invalid outgoing mailbox action code %u\n", MailboxGuest.u.out.uActionCode));
        /** @todo We ought to report an error in the incoming mailbox here */
        rc = VINF_NOT_SUPPORTED;    /* Not immediately an error, keep going. */
    }

    AssertRC(rc);

    /* Advance to the next mailbox. */
    if (pThis->fStrictRoundRobinMode)
        buslogicR3OutgoingMailboxAdvance(pThis);

    return rc;
}

/**
 * Processes a SCSI request issued by the BIOS with the BUSLOGICCOMMAND_EXECUTE_SCSI_COMMAND command.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       Pointer to the shared BusLogic instance data.
 * @param   pThisCC     Pointer to the ring-3 BusLogic instance data.
 */
static void buslogicR3ProcessBiosReq(PPDMDEVINS pDevIns, PBUSLOGIC pThis, PBUSLOGICCC pThisCC)
{
    PESCMD pCmd = (PESCMD)pThis->aCommandBuffer;

    if (RT_LIKELY(   pCmd->uTargetId < RT_ELEMENTS(pThisCC->aDeviceStates)
                  && pCmd->cbCDB <= RT_ELEMENTS(pCmd->abCDB)))
    {
        PBUSLOGICDEVICE pTgtDev = &pThisCC->aDeviceStates[pCmd->uTargetId];

        /* Check if device is present on bus. If not return error immediately and don't process this further. */
        if (RT_LIKELY(pTgtDev->fPresent))
        {
            PDMMEDIAEXIOREQ hIoReq;
            PBUSLOGICREQ pReq;
            int rc = pTgtDev->pDrvMediaEx->pfnIoReqAlloc(pTgtDev->pDrvMediaEx, &hIoReq, (void **)&pReq,
                                                         0, PDMIMEDIAEX_F_SUSPEND_ON_RECOVERABLE_ERR);
            if (RT_SUCCESS(rc))
            {
                pReq->pTargetDevice = pTgtDev;
                pReq->GCPhysAddrCCB = 0;
                pReq->fBIOS         = true;
                pReq->hIoReq        = hIoReq;
                pReq->fIs24Bit      = false;

                uint32_t uLun = pCmd->uLogicalUnit;

                PDMMEDIAEXIOREQSCSITXDIR enmXferDir = PDMMEDIAEXIOREQSCSITXDIR_UNKNOWN;

                if (pCmd->uDataDirection == 2)
                    enmXferDir = PDMMEDIAEXIOREQSCSITXDIR_TO_DEVICE;
                else if (pCmd->uDataDirection == 1)
                    enmXferDir = PDMMEDIAEXIOREQSCSITXDIR_FROM_DEVICE;

                ASMAtomicIncU32(&pTgtDev->cOutstandingRequests);
                rc = pTgtDev->pDrvMediaEx->pfnIoReqSendScsiCmd(pTgtDev->pDrvMediaEx, pReq->hIoReq, uLun,
                                                               &pCmd->abCDB[0], pCmd->cbCDB,
                                                               enmXferDir, NULL, pCmd->cbData, NULL, 0 /*cbSense*/, NULL,
                                                               &pReq->u8ScsiSts, 30 * RT_MS_1SEC);
                if (rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
                    buslogicR3ReqComplete(pDevIns, pThis, pThisCC, pReq, rc);
            }
            else
                buslogicR3ReqCompleteBios(pThis, SCSI_STATUS_CHECK_CONDITION);
        }
        else
            buslogicR3ReqCompleteBios(pThis, SCSI_STATUS_CHECK_CONDITION);
    }
    else
        buslogicR3ReqCompleteBios(pThis, SCSI_STATUS_CHECK_CONDITION);
}


/** @callback_method_impl{FNSSMDEVLIVEEXEC}  */
static DECLCALLBACK(int) buslogicR3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PBUSLOGICCC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    RT_NOREF(uPass);

    /* Save the device config. */
    for (unsigned i = 0; i < RT_ELEMENTS(pThisCC->aDeviceStates); i++)
        pHlp->pfnSSMPutBool(pSSM, pThisCC->aDeviceStates[i].fPresent);

    return VINF_SSM_DONT_CALL_AGAIN;
}

/** @callback_method_impl{FNSSMDEVSAVEEXEC}  */
static DECLCALLBACK(int) buslogicR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PBUSLOGIC       pThis      = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    PBUSLOGICCC     pThisCC        = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);
    PCPDMDEVHLPR3   pHlp           = pDevIns->pHlpR3;
    uint32_t        cReqsSuspended = 0;

    /* Every device first. */
    for (unsigned i = 0; i < RT_ELEMENTS(pThisCC->aDeviceStates); i++)
    {
        PBUSLOGICDEVICE pDevice = &pThisCC->aDeviceStates[i];

        AssertMsg(!pDevice->cOutstandingRequests,
                  ("There are still outstanding requests on this device\n"));
        pHlp->pfnSSMPutBool(pSSM, pDevice->fPresent);
        pHlp->pfnSSMPutU32(pSSM, pDevice->cOutstandingRequests);

        if (pDevice->fPresent)
            cReqsSuspended += pDevice->pDrvMediaEx->pfnIoReqGetSuspendedCount(pDevice->pDrvMediaEx);
    }
    /* Now the main device state. */
    pHlp->pfnSSMPutU8    (pSSM, pThis->regStatus);
    pHlp->pfnSSMPutU8    (pSSM, pThis->regInterrupt);
    pHlp->pfnSSMPutU8    (pSSM, pThis->regGeometry);
    pHlp->pfnSSMPutMem   (pSSM, &pThis->LocalRam, sizeof(pThis->LocalRam));
    pHlp->pfnSSMPutU8    (pSSM, pThis->uOperationCode);
    pHlp->pfnSSMPutMem   (pSSM, &pThis->aCommandBuffer, sizeof(pThis->aCommandBuffer));
    pHlp->pfnSSMPutU8    (pSSM, pThis->iParameter);
    pHlp->pfnSSMPutU8    (pSSM, pThis->cbCommandParametersLeft);
    pHlp->pfnSSMPutBool  (pSSM, pThis->fUseLocalRam);
    pHlp->pfnSSMPutMem   (pSSM, pThis->aReplyBuffer, sizeof(pThis->aReplyBuffer));
    pHlp->pfnSSMPutU8    (pSSM, pThis->iReply);
    pHlp->pfnSSMPutU8    (pSSM, pThis->cbReplyParametersLeft);
    pHlp->pfnSSMPutBool  (pSSM, pThis->fIRQEnabled);
    pHlp->pfnSSMPutU8    (pSSM, pThis->uISABaseCode);
    pHlp->pfnSSMPutU32   (pSSM, pThis->cMailbox);
    pHlp->pfnSSMPutBool  (pSSM, pThis->fMbxIs24Bit);
    pHlp->pfnSSMPutGCPhys(pSSM, pThis->GCPhysAddrMailboxOutgoingBase);
    pHlp->pfnSSMPutU32   (pSSM, pThis->uMailboxOutgoingPositionCurrent);
    pHlp->pfnSSMPutU32   (pSSM, pThis->cMailboxesReady);
    pHlp->pfnSSMPutBool  (pSSM, pThis->fNotificationSent);
    pHlp->pfnSSMPutGCPhys(pSSM, pThis->GCPhysAddrMailboxIncomingBase);
    pHlp->pfnSSMPutU32   (pSSM, pThis->uMailboxIncomingPositionCurrent);
    pHlp->pfnSSMPutBool  (pSSM, pThis->fStrictRoundRobinMode);
    pHlp->pfnSSMPutBool  (pSSM, pThis->fExtendedLunCCBFormat);

    pHlp->pfnSSMPutU32(pSSM, cReqsSuspended);

    /* Save the physical CCB address of all suspended requests. */
    for (unsigned i = 0; i < RT_ELEMENTS(pThisCC->aDeviceStates) && cReqsSuspended; i++)
    {
        PBUSLOGICDEVICE pDevice = &pThisCC->aDeviceStates[i];
        if (pDevice->fPresent)
        {
            uint32_t cThisReqsSuspended = pDevice->pDrvMediaEx->pfnIoReqGetSuspendedCount(pDevice->pDrvMediaEx);

            cReqsSuspended -= cThisReqsSuspended;
            if (cThisReqsSuspended)
            {
                PDMMEDIAEXIOREQ hIoReq;
                PBUSLOGICREQ pReq;
                int rc = pDevice->pDrvMediaEx->pfnIoReqQuerySuspendedStart(pDevice->pDrvMediaEx, &hIoReq,
                                                                           (void **)&pReq);
                AssertRCBreak(rc);

                for (;;)
                {
                    pHlp->pfnSSMPutU32(pSSM, (uint32_t)pReq->GCPhysAddrCCB);

                    cThisReqsSuspended--;
                    if (!cThisReqsSuspended)
                        break;

                    rc = pDevice->pDrvMediaEx->pfnIoReqQuerySuspendedNext(pDevice->pDrvMediaEx, hIoReq,
                                                                          &hIoReq, (void **)&pReq);
                    AssertRCBreak(rc);
                }
            }
        }
    }

    return pHlp->pfnSSMPutU32(pSSM, UINT32_MAX);
}

/** @callback_method_impl{FNSSMDEVLOADDONE}  */
static DECLCALLBACK(int) buslogicR3LoadDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PBUSLOGIC   pThis   = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    PBUSLOGICCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);
    RT_NOREF(pSSM);

    buslogicR3RegisterISARange(pDevIns, pThis, pThis->uISABaseCode);

    /* Kick of any requests we might need to redo. */
    if (pThisCC->cReqsRedo)
    {
        for (unsigned i = 0; i < pThisCC->cReqsRedo; i++)
        {
            int rc = buslogicR3DeviceSCSIRequestSetup(pDevIns, pThis, pThisCC, pThisCC->paGCPhysAddrCCBRedo[i]);
            AssertRC(rc);
        }

        RTMemFree(pThisCC->paGCPhysAddrCCBRedo);
        pThisCC->paGCPhysAddrCCBRedo = NULL;
        pThisCC->cReqsRedo = 0;
    }

    return VINF_SUCCESS;
}

/** @callback_method_impl{FNSSMDEVLOADEXEC}  */
static DECLCALLBACK(int) buslogicR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PBUSLOGIC       pThis   = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    PBUSLOGICCC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc      = VINF_SUCCESS;

    /* We support saved states only from this and older versions. */
    if (uVersion > BUSLOGIC_SAVED_STATE_MINOR_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* Every device first. */
    for (unsigned i = 0; i < RT_ELEMENTS(pThisCC->aDeviceStates); i++)
    {
        PBUSLOGICDEVICE pDevice = &pThisCC->aDeviceStates[i];

        AssertMsg(!pDevice->cOutstandingRequests,
                  ("There are still outstanding requests on this device\n"));
        bool fPresent;
        rc = pHlp->pfnSSMGetBool(pSSM, &fPresent);
        AssertRCReturn(rc, rc);
        if (pDevice->fPresent != fPresent)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Target %u config mismatch: config=%RTbool state=%RTbool"), i, pDevice->fPresent, fPresent);

        if (uPass == SSM_PASS_FINAL)
            pHlp->pfnSSMGetU32V(pSSM, &pDevice->cOutstandingRequests);
    }

    if (uPass != SSM_PASS_FINAL)
        return VINF_SUCCESS;

    /* Now the main device state. */
    pHlp->pfnSSMGetU8V   (pSSM, &pThis->regStatus);
    pHlp->pfnSSMGetU8V   (pSSM, &pThis->regInterrupt);
    pHlp->pfnSSMGetU8V   (pSSM, &pThis->regGeometry);
    pHlp->pfnSSMGetMem   (pSSM, &pThis->LocalRam, sizeof(pThis->LocalRam));
    pHlp->pfnSSMGetU8    (pSSM, &pThis->uOperationCode);
    if (uVersion > BUSLOGIC_SAVED_STATE_MINOR_PRE_CMDBUF_RESIZE)
        pHlp->pfnSSMGetMem(pSSM, &pThis->aCommandBuffer, sizeof(pThis->aCommandBuffer));
    else
        pHlp->pfnSSMGetMem(pSSM, &pThis->aCommandBuffer, BUSLOGIC_COMMAND_SIZE_OLD);
    pHlp->pfnSSMGetU8    (pSSM, &pThis->iParameter);
    pHlp->pfnSSMGetU8    (pSSM, &pThis->cbCommandParametersLeft);
    pHlp->pfnSSMGetBool  (pSSM, &pThis->fUseLocalRam);
    pHlp->pfnSSMGetMem   (pSSM, pThis->aReplyBuffer, sizeof(pThis->aReplyBuffer));
    pHlp->pfnSSMGetU8    (pSSM, &pThis->iReply);
    pHlp->pfnSSMGetU8    (pSSM, &pThis->cbReplyParametersLeft);
    pHlp->pfnSSMGetBool  (pSSM, &pThis->fIRQEnabled);
    pHlp->pfnSSMGetU8    (pSSM, &pThis->uISABaseCode);
    pHlp->pfnSSMGetU32   (pSSM, &pThis->cMailbox);
    if (uVersion > BUSLOGIC_SAVED_STATE_MINOR_PRE_24BIT_MBOX)
        pHlp->pfnSSMGetBool(pSSM, &pThis->fMbxIs24Bit);
    pHlp->pfnSSMGetGCPhys(pSSM, &pThis->GCPhysAddrMailboxOutgoingBase);
    pHlp->pfnSSMGetU32   (pSSM, &pThis->uMailboxOutgoingPositionCurrent);
    pHlp->pfnSSMGetU32V  (pSSM, &pThis->cMailboxesReady);
    pHlp->pfnSSMGetBoolV (pSSM, &pThis->fNotificationSent);
    pHlp->pfnSSMGetGCPhys(pSSM, &pThis->GCPhysAddrMailboxIncomingBase);
    pHlp->pfnSSMGetU32   (pSSM, &pThis->uMailboxIncomingPositionCurrent);
    pHlp->pfnSSMGetBool  (pSSM, &pThis->fStrictRoundRobinMode);
    pHlp->pfnSSMGetBool  (pSSM, &pThis->fExtendedLunCCBFormat);

    if (uVersion <= BUSLOGIC_SAVED_STATE_MINOR_PRE_VBOXSCSI_REMOVAL)
    {
        rc = vboxscsiR3LoadExecLegacy(pDevIns->pHlpR3, pSSM);
        if (RT_FAILURE(rc))
        {
            LogRel(("BusLogic: Failed to restore BIOS state: %Rrc.\n", rc));
            return PDMDEV_SET_ERROR(pDevIns, rc, N_("BusLogic: Failed to restore BIOS state\n"));
        }
    }

    if (uVersion > BUSLOGIC_SAVED_STATE_MINOR_PRE_ERROR_HANDLING)
    {
        /* Check if there are pending tasks saved. */
        uint32_t cTasks = 0;

        pHlp->pfnSSMGetU32(pSSM, &cTasks);

        if (cTasks)
        {
            pThisCC->paGCPhysAddrCCBRedo = (PRTGCPHYS)RTMemAllocZ(cTasks * sizeof(RTGCPHYS));
            if (RT_LIKELY(pThisCC->paGCPhysAddrCCBRedo))
            {
                pThisCC->cReqsRedo = cTasks;

                for (uint32_t i = 0; i < cTasks; i++)
                {
                    uint32_t u32PhysAddrCCB;

                    rc = pHlp->pfnSSMGetU32(pSSM, &u32PhysAddrCCB);
                    AssertRCBreak(rc);

                    pThisCC->paGCPhysAddrCCBRedo[i] = u32PhysAddrCCB;
                }
            }
            else
                rc = VERR_NO_MEMORY;
        }
    }

    if (RT_SUCCESS(rc))
    {
        uint32_t u32;
        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        if (RT_SUCCESS(rc))
            AssertMsgReturn(u32 == UINT32_MAX, ("%#x\n", u32), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
    }

    return rc;
}

/**
 * Gets the pointer to the status LED of a device - called from the SCSI driver.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   iLUN            The unit which status LED we desire. Always 0 here as the driver
 *                          doesn't know about other LUN's.
 * @param   ppLed           Where to store the LED pointer.
 */
static DECLCALLBACK(int) buslogicR3DeviceQueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    PBUSLOGICDEVICE pDevice = RT_FROM_MEMBER(pInterface, BUSLOGICDEVICE, ILed);
    if (iLUN == 0)
    {
        *ppLed = &pDevice->Led;
        Assert((*ppLed)->u32Magic == PDMLED_MAGIC);
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) buslogicR3DeviceQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PBUSLOGICDEVICE pDevice = RT_FROM_MEMBER(pInterface, BUSLOGICDEVICE, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDevice->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAPORT, &pDevice->IMediaPort);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAEXPORT, &pDevice->IMediaExPort);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDPORTS, &pDevice->ILed);
    return NULL;
}

/**
 * Gets the pointer to the status LED of a unit.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   iLUN            The unit which status LED we desire.
 * @param   ppLed           Where to store the LED pointer.
 */
static DECLCALLBACK(int) buslogicR3StatusQueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    PBUSLOGICCC pThisCC = RT_FROM_MEMBER(pInterface, BUSLOGICCC, ILeds);
    if (iLUN < BUSLOGIC_MAX_DEVICES)
    {
        *ppLed = &pThisCC->aDeviceStates[iLUN].Led;
        Assert((*ppLed)->u32Magic == PDMLED_MAGIC);
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) buslogicR3StatusQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PBUSLOGICCC pThisCC = RT_FROM_MEMBER(pInterface, BUSLOGICCC, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDPORTS, &pThisCC->ILeds);
    return NULL;
}

/**
 * @callback_method_impl{FNPDMTHREADWAKEUPDEV}
 */
static DECLCALLBACK(int) buslogicR3Worker(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    PBUSLOGIC   pThis   = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    PBUSLOGICCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        int rc;

        ASMAtomicWriteBool(&pThisCC->fWrkThreadSleeping, true);
        bool fNotificationSent = ASMAtomicXchgBool(&pThis->fNotificationSent, false);
        if (!fNotificationSent)
        {
            Assert(ASMAtomicReadBool(&pThisCC->fWrkThreadSleeping));
            rc = PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pThis->hEvtProcess, RT_INDEFINITE_WAIT);
            AssertLogRelMsgReturn(RT_SUCCESS(rc) || rc == VERR_INTERRUPTED, ("%Rrc\n", rc), rc);
            if (RT_UNLIKELY(pThread->enmState != PDMTHREADSTATE_RUNNING))
                break;
            LogFlowFunc(("Woken up with rc=%Rrc\n", rc));
            ASMAtomicWriteBool(&pThis->fNotificationSent, false);
        }

        ASMAtomicWriteBool(&pThisCC->fWrkThreadSleeping, false);

        if (ASMAtomicXchgBool(&pThis->fBiosReqPending, false))
            buslogicR3ProcessBiosReq(pDevIns, pThis, pThisCC);

        if (ASMAtomicXchgU32(&pThis->cMailboxesReady, 0))
        {
            /* Process mailboxes as long as there are new entries. The loop can potentially
             * keep going for a while if the guest keeps supplying new entries.
             * If there are too many invalid mailbox entries, abort processing because
             * we're just wasting time. This might happen if the guest keeps supplying
             * new invalid entries (extremely unlikely), or if the mailbox memory is not
             * writable for whatever reason.
             */
            uint32_t cMaxInvalid = pThis->cMailbox * 2; /* NB: cMailbox can't be more than 255. */
            do
            {
                rc = buslogicR3ProcessMailboxNext(pDevIns, pThis, pThisCC);
                if (RT_UNLIKELY(rc == VINF_NOT_SUPPORTED))
                {
                    if (cMaxInvalid-- == 0)
                    {
                        LogRelMax(10, ("BusLogic: Too many invalid entries, aborting maibox processing!\n"));
                        break;
                    }
                }
                AssertMsg(RT_SUCCESS(rc) || rc == VERR_NO_DATA, ("Processing mailbox failed rc=%Rrc\n", rc));
            } while (RT_SUCCESS(rc));
        }
    } /* While running */

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNPDMTHREADWAKEUPDEV}
 */
static DECLCALLBACK(int) buslogicR3WorkerWakeUp(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    PBUSLOGIC pThis = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    return PDMDevHlpSUPSemEventSignal(pDevIns, pThis->hEvtProcess);
}

/**
 * BusLogic debugger info callback.
 *
 * @param   pDevIns     The device instance.
 * @param   pHlp        The output helpers.
 * @param   pszArgs     The arguments.
 */
static DECLCALLBACK(void) buslogicR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    static const char *apszModels[] = { "BusLogic BT-958D", "BusLogic BT-545C", "Adaptec AHA-1540B" };
    PBUSLOGIC   pThis = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    unsigned    i;
    bool        fVerbose = false;

    /* Parse arguments. */
    if (pszArgs)
        fVerbose = strstr(pszArgs, "verbose") != NULL;

    /* Show basic information. */
    pHlp->pfnPrintf(pHlp, "%s#%d: %s ",
                    pDevIns->pReg->szName,
                    pDevIns->iInstance,
                    pThis->uDevType >= RT_ELEMENTS(apszModels) ? "Unknown model" : apszModels[pThis->uDevType]);
    if (pThis->uIsaIrq)
        pHlp->pfnPrintf(pHlp, "ISA I/O=%RTiop IRQ=%u ",
                        pThis->IOISABase,
                        pThis->uIsaIrq);
    else
        pHlp->pfnPrintf(pHlp, "PCI I/O=%04x ISA I/O=%RTiop MMIO=%RGp IRQ=%u ",
                        PDMDevHlpIoPortGetMappingAddress(pDevIns, pThis->hIoPortsPci), pThis->IOISABase,
                        PDMDevHlpMmioGetMappingAddress(pDevIns, pThis->hMmio),
                        PCIDevGetInterruptLine(pDevIns->apPciDevs[0]));
    pHlp->pfnPrintf(pHlp, "RC=%RTbool R0=%RTbool\n", pDevIns->fRCEnabled, pDevIns->fR0Enabled);

    /* Print mailbox state. */
    if (pThis->regStatus & BL_STAT_INREQ)
        pHlp->pfnPrintf(pHlp, "Mailbox not initialized\n");
    else
        pHlp->pfnPrintf(pHlp, "%u-bit mailbox with %u entries at %RGp (%d LUN CCBs)\n",
                        pThis->fMbxIs24Bit ? 24 : 32, pThis->cMailbox,
                        pThis->GCPhysAddrMailboxOutgoingBase,
                        pThis->fMbxIs24Bit ? 8 : pThis->fExtendedLunCCBFormat ? 64 : 8);

    /* Print register contents. */
    pHlp->pfnPrintf(pHlp, "Registers: STAT=%02x INTR=%02x GEOM=%02x\n",
                    pThis->regStatus, pThis->regInterrupt, pThis->regGeometry);

    /* Print miscellaneous state. */
    pHlp->pfnPrintf(pHlp, "HAC interrupts: %s\n",
                    pThis->fIRQEnabled ? "on" : "off");

    /* Print the current command, if any. */
    if (pThis->uOperationCode != 0xff )
        pHlp->pfnPrintf(pHlp, "Current command: %02X\n", pThis->uOperationCode);

    /* Print the previous command, if any. */
    if (pThis->uPrevCmd != 0xff )
        pHlp->pfnPrintf(pHlp, "Last completed command: %02X\n", pThis->uPrevCmd);

    if (fVerbose && (pThis->regStatus & BL_STAT_INREQ) == 0)
    {
        RTGCPHYS    GCMailbox;

        /* Dump the mailbox contents. */
        if (pThis->fMbxIs24Bit)
        {
            Mailbox24   Mbx24;

            /* Outgoing mailbox, 24-bit format. */
            GCMailbox = pThis->GCPhysAddrMailboxOutgoingBase;
            pHlp->pfnPrintf(pHlp, " Outgoing mailbox entries (24-bit) at %06X:\n", GCMailbox);
            for (i = 0; i < pThis->cMailbox; ++i)
            {
                blPhysReadMeta(pDevIns, pThis, GCMailbox, &Mbx24, sizeof(Mailbox24));
                pHlp->pfnPrintf(pHlp, "  slot %03d: CCB at %06X action code %02X", i, ADDR_TO_U32(Mbx24.aPhysAddrCCB), Mbx24.uCmdState);
                pHlp->pfnPrintf(pHlp, "%s\n", pThis->uMailboxOutgoingPositionCurrent == i ? " *" : "");
                GCMailbox += sizeof(Mailbox24);
            }

            /* Incoming mailbox, 24-bit format. */
            GCMailbox = pThis->GCPhysAddrMailboxOutgoingBase + (pThis->cMailbox * sizeof(Mailbox24));
            pHlp->pfnPrintf(pHlp, " Incoming mailbox entries (24-bit) at %06X:\n", GCMailbox);
            for (i = 0; i < pThis->cMailbox; ++i)
            {
                blPhysReadMeta(pDevIns, pThis, GCMailbox, &Mbx24, sizeof(Mailbox24));
                pHlp->pfnPrintf(pHlp, "  slot %03d: CCB at %06X completion code %02X", i, ADDR_TO_U32(Mbx24.aPhysAddrCCB), Mbx24.uCmdState);
                pHlp->pfnPrintf(pHlp, "%s\n", pThis->uMailboxIncomingPositionCurrent == i ? " *" : "");
                GCMailbox += sizeof(Mailbox24);
            }

        }
        else
        {
            Mailbox32   Mbx32;

            /* Outgoing mailbox, 32-bit format. */
            GCMailbox = pThis->GCPhysAddrMailboxOutgoingBase;
            pHlp->pfnPrintf(pHlp, " Outgoing mailbox entries (32-bit) at %08X:\n", (uint32_t)GCMailbox);
            for (i = 0; i < pThis->cMailbox; ++i)
            {
                blPhysReadMeta(pDevIns, pThis, GCMailbox, &Mbx32, sizeof(Mailbox32));
                pHlp->pfnPrintf(pHlp, "  slot %03d: CCB at %08X action code %02X", i, Mbx32.u32PhysAddrCCB, Mbx32.u.out.uActionCode);
                pHlp->pfnPrintf(pHlp, "%s\n", pThis->uMailboxOutgoingPositionCurrent == i ? " *" : "");
                GCMailbox += sizeof(Mailbox32);
            }

            /* Incoming mailbox, 32-bit format. */
            GCMailbox = pThis->GCPhysAddrMailboxOutgoingBase + (pThis->cMailbox * sizeof(Mailbox32));
            pHlp->pfnPrintf(pHlp, " Incoming mailbox entries (32-bit) at %08X:\n", (uint32_t)GCMailbox);
            for (i = 0; i < pThis->cMailbox; ++i)
            {
                blPhysReadMeta(pDevIns, pThis, GCMailbox, &Mbx32, sizeof(Mailbox32));
                pHlp->pfnPrintf(pHlp, "  slot %03d: CCB at %08X completion code %02X BTSTAT %02X SDSTAT %02X", i,
                                Mbx32.u32PhysAddrCCB, Mbx32.u.in.uCompletionCode, Mbx32.u.in.uHostAdapterStatus, Mbx32.u.in.uTargetDeviceStatus);
                pHlp->pfnPrintf(pHlp, "%s\n", pThis->uMailboxIncomingPositionCurrent == i ? " *" : "");
                GCMailbox += sizeof(Mailbox32);
            }

        }
    }
}

/* -=-=-=-=- Helper -=-=-=-=- */

 /**
 * Checks if all asynchronous I/O is finished.
 *
 * Used by buslogicR3Reset, buslogicR3Suspend and buslogicR3PowerOff.
 *
 * @returns true if quiesced, false if busy.
 * @param   pDevIns         The device instance.
 */
static bool buslogicR3AllAsyncIOIsFinished(PPDMDEVINS pDevIns)
{
    PBUSLOGICCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);

    for (uint32_t i = 0; i < RT_ELEMENTS(pThisCC->aDeviceStates); i++)
    {
        PBUSLOGICDEVICE pThisDevice = &pThisCC->aDeviceStates[i];
        if (pThisDevice->pDrvBase)
        {
            if (pThisDevice->cOutstandingRequests != 0)
                return false;
        }
    }

    return true;
}

/**
 * Callback employed by buslogicR3Suspend and buslogicR3PowerOff.
 *
 * @returns true if we've quiesced, false if we're still working.
 * @param   pDevIns     The device instance.
 */
static DECLCALLBACK(bool) buslogicR3IsAsyncSuspendOrPowerOffDone(PPDMDEVINS pDevIns)
{
    if (!buslogicR3AllAsyncIOIsFinished(pDevIns))
        return false;

    PBUSLOGICCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);
    ASMAtomicWriteBool(&pThisCC->fSignalIdle, false);
    return true;
}

/**
 * Common worker for buslogicR3Suspend and buslogicR3PowerOff.
 */
static void buslogicR3SuspendOrPowerOff(PPDMDEVINS pDevIns)
{
    PBUSLOGIC   pThis   = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    PBUSLOGICCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);

    ASMAtomicWriteBool(&pThisCC->fSignalIdle, true);
    if (!buslogicR3AllAsyncIOIsFinished(pDevIns))
        PDMDevHlpSetAsyncNotification(pDevIns, buslogicR3IsAsyncSuspendOrPowerOffDone);
    else
    {
        ASMAtomicWriteBool(&pThisCC->fSignalIdle, false);
        AssertMsg(!pThis->fNotificationSent, ("The PDM Queue should be empty at this point\n"));
        RT_NOREF(pThis);
    }

    for (uint32_t i = 0; i < RT_ELEMENTS(pThisCC->aDeviceStates); i++)
    {
        PBUSLOGICDEVICE pThisDevice = &pThisCC->aDeviceStates[i];
        if (pThisDevice->pDrvMediaEx)
            pThisDevice->pDrvMediaEx->pfnNotifySuspend(pThisDevice->pDrvMediaEx);
    }
}

/**
 * Suspend notification.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(void) buslogicR3Suspend(PPDMDEVINS pDevIns)
{
    Log(("buslogicR3Suspend\n"));
    buslogicR3SuspendOrPowerOff(pDevIns);
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
static DECLCALLBACK(void) buslogicR3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PBUSLOGIC       pThis   = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    PBUSLOGICCC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);
    PBUSLOGICDEVICE pDevice = &pThisCC->aDeviceStates[iLUN];
    Log(("%s:\n", __FUNCTION__));
    RT_NOREF(fFlags);


    AssertMsg(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
              ("BusLogic: Device does not support hotplugging\n"));

    /*
     * Zero some important members.
     */
    pThis->afDevicePresent[iLUN] = false;
    pDevice->fPresent    = false;
    pDevice->pDrvBase    = NULL;
    pDevice->pDrvMedia   = NULL;
    pDevice->pDrvMediaEx = NULL;
}

/**
 * Attach command.
 *
 * This is called when we change block driver.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   iLUN        The logical unit which is being detached.
 * @param   fFlags      Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 */
static DECLCALLBACK(int)  buslogicR3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PBUSLOGIC       pThis   = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    PBUSLOGICCC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);
    PBUSLOGICDEVICE pDevice = &pThisCC->aDeviceStates[iLUN];
    int rc;

    AssertMsgReturn(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
                    ("BusLogic: Device does not support hotplugging\n"),
                    VERR_INVALID_PARAMETER);

    /* the usual paranoia */
    AssertRelease(!pDevice->pDrvBase);
    AssertRelease(!pDevice->pDrvMedia);
    AssertRelease(!pDevice->pDrvMediaEx);
    Assert(pDevice->iLUN == iLUN);

    /*
     * Try attach the SCSI driver and get the interfaces,
     * required as well as optional.
     */
    rc = PDMDevHlpDriverAttach(pDevIns, pDevice->iLUN, &pDevice->IBase, &pDevice->pDrvBase, NULL);
    if (RT_SUCCESS(rc))
    {
        /* Query the media interface. */
        pDevice->pDrvMedia = PDMIBASE_QUERY_INTERFACE(pDevice->pDrvBase, PDMIMEDIA);
        AssertMsgReturn(RT_VALID_PTR(pDevice->pDrvMedia),
                        ("BusLogic configuration error: LUN#%d misses the basic media interface!\n", pDevice->iLUN),
                        VERR_PDM_MISSING_INTERFACE);

        /* Get the extended media interface. */
        pDevice->pDrvMediaEx = PDMIBASE_QUERY_INTERFACE(pDevice->pDrvBase, PDMIMEDIAEX);
        AssertMsgReturn(RT_VALID_PTR(pDevice->pDrvMediaEx),
                        ("BusLogic configuration error: LUN#%d misses the extended media interface!\n", pDevice->iLUN),
                        VERR_PDM_MISSING_INTERFACE);

        rc = pDevice->pDrvMediaEx->pfnIoReqAllocSizeSet(pDevice->pDrvMediaEx, sizeof(BUSLOGICREQ));
        AssertMsgRCReturn(rc, ("BusLogic configuration error: LUN#%u: Failed to set I/O request size!", pDevice->iLUN),
                          rc);

        pThis->afDevicePresent[iLUN] = true;
        pDevice->fPresent = true;
    }
    else
        AssertMsgFailed(("Failed to attach LUN#%d. rc=%Rrc\n", pDevice->iLUN, rc));

    if (RT_FAILURE(rc))
    {
        pThis->afDevicePresent[iLUN] = false;
        pDevice->fPresent    = false;
        pDevice->pDrvBase    = NULL;
        pDevice->pDrvMedia   = NULL;
        pDevice->pDrvMediaEx = NULL;
    }
    return rc;
}

/**
 * Callback employed by buslogicR3Reset.
 *
 * @returns true if we've quiesced, false if we're still working.
 * @param   pDevIns     The device instance.
 */
static DECLCALLBACK(bool) buslogicR3IsAsyncResetDone(PPDMDEVINS pDevIns)
{
    PBUSLOGIC   pThis   = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    PBUSLOGICCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);

    if (!buslogicR3AllAsyncIOIsFinished(pDevIns))
        return false;
    ASMAtomicWriteBool(&pThisCC->fSignalIdle, false);

    buslogicR3HwReset(pDevIns, pThis, true);
    return true;
}

/**
 * @copydoc FNPDMDEVRESET
 */
static DECLCALLBACK(void) buslogicR3Reset(PPDMDEVINS pDevIns)
{
    PBUSLOGIC   pThis   = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    PBUSLOGICCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);

    ASMAtomicWriteBool(&pThisCC->fSignalIdle, true);
    if (!buslogicR3AllAsyncIOIsFinished(pDevIns))
        PDMDevHlpSetAsyncNotification(pDevIns, buslogicR3IsAsyncResetDone);
    else
    {
        ASMAtomicWriteBool(&pThisCC->fSignalIdle, false);
        buslogicR3HwReset(pDevIns, pThis, true);
    }
}

/**
 * Poweroff notification.
 *
 * @param   pDevIns Pointer to the device instance
 */
static DECLCALLBACK(void) buslogicR3PowerOff(PPDMDEVINS pDevIns)
{
    Log(("buslogicR3PowerOff\n"));
    buslogicR3SuspendOrPowerOff(pDevIns);
}

/**
 * Destroy a driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(int) buslogicR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PBUSLOGIC pThis = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);

    PDMDevHlpCritSectDelete(pDevIns, &pThis->CritSectIntr);

    if (pThis->hEvtProcess != NIL_SUPSEMEVENT)
    {
        PDMDevHlpSUPSemEventClose(pDevIns, pThis->hEvtProcess);
        pThis->hEvtProcess = NIL_SUPSEMEVENT;
    }

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) buslogicR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PBUSLOGIC       pThis   = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);
    PBUSLOGICCC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBUSLOGICCC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;

    /*
     * Init instance data (do early because of constructor).
     */
    pThis->hMmio       = NIL_IOMMMIOHANDLE;
    pThis->hIoPortsIsa = NIL_IOMIOPORTHANDLE;
    pThis->hIoPortsPci = NIL_IOMIOPORTHANDLE;
    pThisCC->pDevIns = pDevIns;
    pThisCC->IBase.pfnQueryInterface = buslogicR3StatusQueryInterface;
    pThisCC->ILeds.pfnQueryStatusLed = buslogicR3StatusQueryStatusLed;

    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    PDMPciDevSetVendorId(pPciDev,           0x104b); /* BusLogic */
    PDMPciDevSetDeviceId(pPciDev,           0x1040); /* BT-958 */
    PDMPciDevSetCommand(pPciDev,            PCI_COMMAND_IOACCESS | PCI_COMMAND_MEMACCESS);
    PDMPciDevSetRevisionId(pPciDev,         0x01);
    PDMPciDevSetClassProg(pPciDev,          0x00); /* SCSI */
    PDMPciDevSetClassSub(pPciDev,           0x00); /* SCSI */
    PDMPciDevSetClassBase(pPciDev,          0x01); /* Mass storage */
    PDMPciDevSetBaseAddress(pPciDev,        0, true  /*IO*/, false /*Pref*/, false /*64-bit*/, 0x00000000);
    PDMPciDevSetBaseAddress(pPciDev,        1, false /*IO*/, false /*Pref*/, false /*64-bit*/, 0x00000000);
    PDMPciDevSetSubSystemVendorId(pPciDev,  0x104b);
    PDMPciDevSetSubSystemId(pPciDev,        0x1040);
    PDMPciDevSetInterruptLine(pPciDev,      0x00);
    PDMPciDevSetInterruptPin(pPciDev,       0x01);

    /*
     * Validate and read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "Bootable|"     /* Keep it for legacy configs, even though it doesn't do anything anymore, see @bugref{4841}. */
                                           "AdapterType|"
                                           "ISACompat",
                                           "");

    /* Figure out the emulated device type. */
    char szCfgStr[16];
    int rc = pHlp->pfnCFGMQueryStringDef(pCfg, "AdapterType", szCfgStr, sizeof(szCfgStr), "BT-958D");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("BusLogic configuration error: failed to read AdapterType as string"));
    Log(("%s: AdapterType=%s\n", __FUNCTION__, szCfgStr));

    /* Grok the AdapterType setting. */
    if (!strcmp(szCfgStr, "BT-958D"))          /* Default PCI device, 32-bit and 24-bit addressing. */
    {
        pThis->uDevType = DEV_BT_958D;
        pThis->uDefaultISABaseCode = ISA_BASE_DISABLED;
    }
    else if (!strcmp(szCfgStr, "BT-545C"))     /* ISA device, 24-bit addressing only. */
    {
        pThis->uDevType = DEV_BT_545C;
        pThis->uIsaIrq = 11;
    }
    else if (!strcmp(szCfgStr, "AHA-1540B"))   /* Competitor ISA device. */
    {
        pThis->uDevType = DEV_AHA_1540B;
        pThis->uIsaIrq = 11;
    }
    else
        return PDMDEV_SET_ERROR(pDevIns, VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES,
                                N_("BusLogic configuration error: invalid AdapterType setting"));

    /* Only the first instance defaults to having the ISA compatibility ports enabled. */
    if (iInstance == 0)
        rc = pHlp->pfnCFGMQueryStringDef(pCfg, "ISACompat", szCfgStr, sizeof(szCfgStr), "Alternate");
    else
        rc = pHlp->pfnCFGMQueryStringDef(pCfg, "ISACompat", szCfgStr, sizeof(szCfgStr), "Disabled");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("BusLogic configuration error: failed to read ISACompat as string"));
    Log(("%s: ISACompat=%s\n", __FUNCTION__, szCfgStr));

    /* Grok the ISACompat setting. */
    if (!strcmp(szCfgStr, "Disabled"))
        pThis->uDefaultISABaseCode = ISA_BASE_DISABLED;
    else if (!strcmp(szCfgStr, "Primary"))
        pThis->uDefaultISABaseCode = 0;     /* I/O base at 330h. */
    else if (!strcmp(szCfgStr, "Alternate"))
        pThis->uDefaultISABaseCode = 1;     /* I/O base at 334h. */
    else
        return PDMDEV_SET_ERROR(pDevIns, VERR_PDM_DEVINS_UNKNOWN_CFG_VALUES,
                                N_("BusLogic configuration error: invalid ISACompat setting"));

    /*
     * Register the PCI device and its I/O regions if applicable.
     */
    if (!pThis->uIsaIrq)
    {
        rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
        AssertRCReturn(rc, rc);

        rc = PDMDevHlpPCIIORegionCreateIo(pDevIns, 0 /*iPciRegion*/, 32 /*cPorts*/,
                                          buslogicIOPortWrite, buslogicIOPortRead, NULL /*pvUser*/,
                                          "BusLogic PCI", NULL /*paExtDescs*/, &pThis->hIoPortsPci);
        AssertRCReturn(rc, rc);

        rc = PDMDevHlpPCIIORegionCreateMmio(pDevIns, 1 /*iPciRegion*/, 32 /*cbRegion*/, PCI_ADDRESS_SPACE_MEM,
                                            buslogicMMIOWrite, buslogicMMIORead, NULL /*pvUser*/,
                                            IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED,
                                            "BusLogic MMIO", &pThis->hMmio);
        AssertRCReturn(rc, rc);
    }

    /* Set up the compatibility I/O range. */
    rc = PDMDevHlpIoPortCreate(pDevIns, 4 /*cPorts*/, NULL /*pPciDev*/, UINT32_MAX /*iPciRegion*/,
                               buslogicIOPortWrite, buslogicIOPortRead, NULL /*pvUser*/,
                               "BusLogic ISA", NULL /*paExtDescs*/, &pThis->hIoPortsIsa);
    AssertRCReturn(rc, rc);

    rc = buslogicR3RegisterISARange(pDevIns, pThis, pThis->uDefaultISABaseCode);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("BusLogic cannot register ISA I/O handlers"));


    /* Init the interrupt critsect. */
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSectIntr, RT_SRC_POS, "BusLogic-Intr#%u", pDevIns->iInstance);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("BusLogic: cannot create critical section"));

    /*
     * Create event semaphore and worker thread.
     */
    rc = PDMDevHlpSUPSemEventCreate(pDevIns, &pThis->hEvtProcess);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("BusLogic: Failed to create SUP event semaphore"));

    char szDevTag[20];
    RTStrPrintf(szDevTag, sizeof(szDevTag), "BUSLOGIC-%u", iInstance);

    rc = PDMDevHlpThreadCreate(pDevIns, &pThisCC->pThreadWrk, pThis, buslogicR3Worker,
                               buslogicR3WorkerWakeUp, 0, RTTHREADTYPE_IO, szDevTag);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("BusLogic: Failed to create worker thread %s"), szDevTag);

    /* Initialize per device state. */
    for (unsigned i = 0; i < RT_ELEMENTS(pThisCC->aDeviceStates); i++)
    {
        PBUSLOGICDEVICE pDevice = &pThisCC->aDeviceStates[i];

        /* Initialize static parts of the device. */
        pDevice->iLUN = i;
        pDevice->pDevIns = pDevIns;
        pDevice->Led.u32Magic = PDMLED_MAGIC;
        pDevice->IBase.pfnQueryInterface                 = buslogicR3DeviceQueryInterface;
        pDevice->IMediaPort.pfnQueryDeviceLocation       = buslogicR3QueryDeviceLocation;
        pDevice->IMediaExPort.pfnIoReqCompleteNotify     = buslogicR3IoReqCompleteNotify;
        pDevice->IMediaExPort.pfnIoReqCopyFromBuf        = buslogicR3IoReqCopyFromBuf;
        pDevice->IMediaExPort.pfnIoReqCopyToBuf          = buslogicR3IoReqCopyToBuf;
        pDevice->IMediaExPort.pfnIoReqQueryBuf           = NULL;
        pDevice->IMediaExPort.pfnIoReqQueryDiscardRanges = NULL;
        pDevice->IMediaExPort.pfnIoReqStateChanged       = buslogicR3IoReqStateChanged;
        pDevice->IMediaExPort.pfnMediumEjected           = buslogicR3MediumEjected;
        pDevice->ILed.pfnQueryStatusLed                  = buslogicR3DeviceQueryStatusLed;
        RTStrPrintf(pDevice->szName, sizeof(pDevice->szName), "Device%u", i);

        /* Attach SCSI driver. */
        rc = PDMDevHlpDriverAttach(pDevIns, pDevice->iLUN, &pDevice->IBase, &pDevice->pDrvBase, pDevice->szName);
        if (RT_SUCCESS(rc))
        {
            /* Query the media interface. */
            pDevice->pDrvMedia = PDMIBASE_QUERY_INTERFACE(pDevice->pDrvBase, PDMIMEDIA);
            AssertMsgReturn(RT_VALID_PTR(pDevice->pDrvMedia),
                            ("Buslogic configuration error: LUN#%d misses the basic media interface!\n", pDevice->iLUN),
                            VERR_PDM_MISSING_INTERFACE);

            /* Get the extended media interface. */
            pDevice->pDrvMediaEx = PDMIBASE_QUERY_INTERFACE(pDevice->pDrvBase, PDMIMEDIAEX);
            AssertMsgReturn(RT_VALID_PTR(pDevice->pDrvMediaEx),
                            ("Buslogic configuration error: LUN#%d misses the extended media interface!\n", pDevice->iLUN),
                            VERR_PDM_MISSING_INTERFACE);

            rc = pDevice->pDrvMediaEx->pfnIoReqAllocSizeSet(pDevice->pDrvMediaEx, sizeof(BUSLOGICREQ));
            if (RT_FAILURE(rc))
                return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                           N_("Buslogic configuration error: LUN#%u: Failed to set I/O request size!"),
                                           pDevice->iLUN);

            pThis->afDevicePresent[i] = true;
            pDevice->fPresent = true;
        }
        else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        {
            pThis->afDevicePresent[i] = false;
            pDevice->fPresent    = false;
            pDevice->pDrvBase    = NULL;
            pDevice->pDrvMedia   = NULL;
            pDevice->pDrvMediaEx = NULL;
            rc = VINF_SUCCESS;
            Log(("BusLogic: no driver attached to device %s\n", pDevice->szName));
        }
        else
        {
            AssertLogRelMsgFailed(("BusLogic: Failed to attach %s\n", pDevice->szName));
            return rc;
        }
    }

    /*
     * Attach status driver (optional).
     */
    PPDMIBASE pBase;
    rc = PDMDevHlpDriverAttach(pDevIns, PDM_STATUS_LUN, &pThisCC->IBase, &pBase, "Status Port");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pLedsConnector = PDMIBASE_QUERY_INTERFACE(pBase, PDMILEDCONNECTORS);
        pThisCC->pMediaNotify = PDMIBASE_QUERY_INTERFACE(pBase, PDMIMEDIANOTIFY);
    }
    else
        AssertMsgReturn(rc == VERR_PDM_NO_ATTACHED_DRIVER, ("Failed to attach to status driver. rc=%Rrc\n", rc),
                        PDMDEV_SET_ERROR(pDevIns, rc, N_("BusLogic cannot attach to status driver")));

    rc = PDMDevHlpSSMRegisterEx(pDevIns, BUSLOGIC_SAVED_STATE_MINOR_VERSION, sizeof(*pThis), NULL,
                                NULL, buslogicR3LiveExec, NULL,
                                NULL, buslogicR3SaveExec, NULL,
                                NULL, buslogicR3LoadExec, buslogicR3LoadDone);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("BusLogic cannot register save state handlers"));

    /*
     * Register the debugger info callback.
     */
    char szTmp[128];
    RTStrPrintf(szTmp, sizeof(szTmp), "%s%d", pDevIns->pReg->szName, pDevIns->iInstance);
    PDMDevHlpDBGFInfoRegister(pDevIns, szTmp, "BusLogic HBA info", buslogicR3Info);

    rc = buslogicR3HwReset(pDevIns, pThis, true);
    AssertMsgRC(rc, ("hardware reset of BusLogic host adapter failed rc=%Rrc\n", rc));

    return rc;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) buslogicRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PBUSLOGIC pThis = PDMDEVINS_2_DATA(pDevIns, PBUSLOGIC);

    if (!pThis->uIsaIrq)
    {
        int rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsPci, buslogicIOPortWrite, buslogicIOPortRead, NULL /*pvUser*/);
        AssertRCReturn(rc, rc);

        rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, buslogicMMIOWrite, buslogicMMIORead, NULL /*pvUser*/);
        AssertRCReturn(rc, rc);
    }

    int rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsIsa, buslogicIOPortWrite, buslogicIOPortRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}


#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceBusLogic =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "buslogic",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE
                                    | PDM_DEVREG_FLAGS_FIRST_SUSPEND_NOTIFICATION | PDM_DEVREG_FLAGS_FIRST_POWEROFF_NOTIFICATION
                                    | PDM_DEVREG_FLAGS_FIRST_RESET_NOTIFICATION,
    /* .fClass = */                 PDM_DEVREG_CLASS_STORAGE,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(BUSLOGIC),
    /* .cbInstanceCC = */           sizeof(BUSLOGICCC),
    /* .cbInstanceRC = */           sizeof(BUSLOGICRC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "BusLogic BT-958 SCSI host adapter.\n",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           buslogicR3Construct,
    /* .pfnDestruct = */            buslogicR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               buslogicR3Reset,
    /* .pfnSuspend = */             buslogicR3Suspend,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              buslogicR3Attach,
    /* .pfnDetach = */              buslogicR3Detach,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            buslogicR3PowerOff,
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
    /* .pfnConstruct = */           buslogicRZConstruct,
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
    /* .pfnConstruct = */           buslogicRZConstruct,
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
