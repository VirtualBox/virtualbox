/* $Id: DevEHCI.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevEHCI - Enhanced Host Controller Interface for USB.
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

/** @page pg_dev_ehci   EHCI - Enhanced Host Controller Interface Emulation.
 *
 * This component implements an EHCI USB controller. It is split roughly
 * into two main parts, the first part implements the register level
 * specification of USB EHCI and the second part maintains the root hub (which
 * is an integrated component of the device).
 *
 * The EHCI registers are used for the usual stuff like enabling and disabling
 * interrupts. Since the USB time is divided in to 1ms frames and various
 * interrupts may need to be triggered at frame boundary time, a timer-based
 * approach was taken.
 *
 * Note that all processing is currently done on a frame boundary and
 * no attempt is made to emulate events with micro-frame granularity.
 *
 * The actual USB transfers are stored in main memory (along with endpoint and
 * transfer descriptors). The ED's for all the control and bulk endpoints are
 * found by consulting the HcAsyncListAddr register (ASYNCLISTADDR).
 * Interrupt and isochronous ED's are found by looking at the HcPeriodicListBase
 * (PERIODICLISTBASE) register.
 *
 * At the start of every frame (in function ehciR3StartOfFrame) we traverse all
 * enabled ED lists and queue up as many transfers as possible. No attention
 * is paid to control/bulk service ratios or bandwidth requirements since our
 * USB could conceivably contain a dozen high speed busses and this would
 * artificially limit the performance.
 *
 * Once we have a transfer ready to go (in the appropriate ehciServiceXxx function)
 * we allocate an URB on the stack,  fill in all the relevant fields and submit
 * it using the VUSBIRhSubmitUrb function. The roothub device and the virtual
 * USB core code coordinates everything else from this point onwards.
 *
 * When the URB has been successfully handed to the lower level driver, our
 * prepare callback gets called and we can remove the TD from the ED transfer
 * list. This stops us queueing it twice while it completes.
 *  bird: no, we don't remove it because that confuses the guest! (=> crashes)
 *
 * Completed URBs are reaped at the end of every frame (in function
 * ehciR3FrameBoundaryTimer). Our completion routine makes use of the ED and TD
 * fields in the URB to store the physical addresses of the descriptors so
 * that they may be modified in the roothub callbacks. Our completion
 * routine (ehciRhXferCompleteXxx) carries out a number of tasks:
 *      -# Retires the TD associated with the transfer, setting the
 *         relevant error code etc.
 *      -# Updates done-queue interrupt timer and potentially causes
 *         a writeback of the done-queue.
 *      -# If the transfer was device-to-host, we copy the data into
 *         the host memory.
 *
 * As for error handling EHCI allows for 3 retries before failing a transfer,
 * an error count is stored in each transfer descriptor. A halt flag is also
 * stored in the transfer descriptor. That allows for ED's to be disabled
 * without stopping the bus and de-queuing them.
 *
 * When the bus is started and stopped, we call VUSBIDevPowerOn/Off() on our
 * roothub to indicate it's powering up and powering down. Whenever we power
 * down, the USB core makes sure to synchronously complete all outstanding
 * requests so that the EHCI is never seen in an inconsistent state by the
 * guest OS (Transfers are not meant to be unlinked until they've actually
 * completed, but we can't do that unless we work synchronously, so we just
 * have to fake it).
 *  bird: we do work synchronously now, anything causes guest crashes.
 *
 * The number of ports is configurable. The architectural maximum is 15, but
 * some guests (e.g. OS/2) crash if they see more than 12 or so ports. Saved
 * states always include the data for all 15 ports but HCSPARAMS determines
 * the actual number visible to the guest.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_EHCI
#include <VBox/pci.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/mm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include <iprt/param.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/critsect.h>
#ifdef IN_RING3
# include <iprt/thread.h>
# include <iprt/mem.h>
# include <iprt/uuid.h>
#endif
#include <VBox/vusb.h>
#ifdef VBOX_IN_EXTPACK_R3
# include <VBox/version.h>
#elif defined(VBOX_IN_EXTPACK)
# include <VBox/sup.h>
#endif
#ifndef VBOX_IN_EXTPACK
# include "VBoxDD.h"
#endif


/** The saved state version. */
#define EHCI_SAVED_STATE_VERSION                   7
/** The saved state version before the EOF timers were removed. */
#define EHCI_SAVED_STATE_VERSION_PRE_TIMER_REMOVAL 6    /* Introduced in 5.2. */
/** The saved state with support of 8 ports. */
#define EHCI_SAVED_STATE_VERSION_8PORTS            5    /* Introduced in 3.1 or so. */

/** Number of Downstream Ports on the root hub; 15 is the maximum
 * the EHCI specification provides for. */
#define EHCI_NDP_MAX            15

/** The default Number of Downstream Ports reported to guests. */
#define EHCI_NDP_DEFAULT        12

/* Macro to query the number of currently configured ports. */
#define EHCI_NDP_CFG(pehci) ((pehci)->hcs_params & EHCI_HCS_PARAMS_NDP_MASK)
/** Macro to convert a EHCI port index (zero based) to a VUSB roothub port ID (one based). */
#define EHCI_PORT_2_VUSB_PORT(a_uPort) ((a_uPort) + 1)

/** Size of the capability part of the MMIO page.  */
#define EHCI_CAPS_REG_SIZE      0x20


#ifndef VBOX_DEVICE_STRUCT_TESTCASE
/**
 * Host controller Transfer Descriptor data.
 */
typedef struct VUSBURBHCITDINT
{
    /** Type of TD. */
    uint32_t        TdType;
    /** The address of the TD. */
    RTGCPHYS        TdAddr;
    /** A copy of the TD. */
    uint32_t        TdCopy[18];
} VUSBURBHCITDINT;

/**
 * The host controller data associated with each URB.
 */
typedef struct VUSBURBHCIINT
{
    /** The endpoint descriptor address. */
    RTGCPHYS        EdAddr;
    /** Number of Tds in the array. */
    uint32_t        cTds;
    /** When this URB was created.
     * (Used for isochronous frames and for logging.) */
    uint32_t        u32FrameNo;
    /** Flag indicating that the TDs have been unlinked. */
    bool            fUnlinked;
} VUSBURBHCIINT;
#endif

/**
 * An EHCI root hub port, shared.
 */
typedef struct EHCIHUBPORT
{
    /** The port register. */
    uint32_t                fReg;
} EHCIHUBPORT;
/** Pointer to a shared EHCI root hub port. */
typedef EHCIHUBPORT *PEHCIHUBPORT;

/**
 * An EHCI root hub port, ring-3.
 */
typedef struct EHCIHUBPORTR3
{
    /** Flag whether there is a device attached to the port. */
    bool                                fAttached;
} EHCIHUBPORTR3;
/** Pointer to a ring-3 EHCI root hub port. */
typedef EHCIHUBPORTR3 *PEHCIHUBPORTR3;


/**
 * The EHCI root hub, shared.
 */
typedef struct EHCIROOTHUB
{
    /** Per-port state. */
    EHCIHUBPORT                         aPorts[EHCI_NDP_MAX];
    /** Unused, only needed for saved state compatibility. */
    uint32_t                            unused;
} EHCIROOTHUB;
/** Pointer to the EHCI root hub. */
typedef EHCIROOTHUB *PEHCIROOTHUB;


/**
 * The EHCI root hub, ring-3 edition.
 *
 * @implements  PDMIBASE
 * @implements  VUSBIROOTHUBPORT
 * @implements  PDMILEDPORTS
 */
typedef struct EHCIROOTHUBR3
{
    /** Pointer to the base interface of the VUSB RootHub. */
    R3PTRTYPE(PPDMIBASE)                pIBase;
    /** Pointer to the connector interface of the VUSB RootHub. */
    R3PTRTYPE(PVUSBIROOTHUBCONNECTOR)   pIRhConn;
    /** The base interface exposed to the roothub driver. */
    PDMIBASE                            IBase;
    /** The roothub port interface exposed to the roothub driver. */
    VUSBIROOTHUBPORT                    IRhPort;

    /** The LED. */
    PDMLED                              Led;
    /** The LED ports. */
    PDMILEDPORTS                        ILeds;
    /** Partner of ILeds. */
    R3PTRTYPE(PPDMILEDCONNECTORS)       pLedsConnector;

    EHCIHUBPORTR3                       aPorts[EHCI_NDP_MAX];
} EHCIROOTHUBR3;
/** Pointer to the ring-3 EHCI root hub state. */
typedef EHCIROOTHUBR3 *PEHCIROOTHUBR3;


/**
 * EHCI device data, shared.
 */
typedef struct EHCI
{
    /** Async scheduler sleeping; triggered by empty list detection */
    bool                fAsyncTraversalTimerActive;

    bool                afAlignment0[7];

    /** Start of current frame. */
    uint64_t            SofTime;
    /** Root hub device */
    EHCIROOTHUB         RootHub;

    /** @name Host Controller Capability Registers (R/O)
     * @{ */
    /** CAPLENGTH: base + cap_length = operational register start */
    uint32_t            cap_length;
    /** HCIVERSION: host controller interface version */
    uint32_t            hci_version;
    /** HCSPARAMS: Structural parameters */
    uint32_t            hcs_params;
    /** HCCPARAMS: Capability parameters */
    uint32_t            hcc_params;
    /** @} */

    /** @name Host Controller Operational Registers (R/W)
     * @{ */
    /** USB command register */
    uint32_t            cmd;
    /** USB status register */
    uint32_t            intr_status;
    /** USB interrupt enable register */
    uint32_t            intr;
    /** Frame index register; actually it's micro-frame number */
    uint32_t            frame_idx;
    /** Control Data Structure Segment Register */
    uint32_t            ds_segment;
    /** Periodic Frame List Base Address Register */
    uint32_t            periodic_list_base;
    /** Current Asynchronous List Address Register */
    uint32_t            async_list_base;
    /** Configure Flag Register */
    uint32_t            config;
    /** @} */

    /** @name Control partition (registers)
     * @{ */
    /** Interrupt interval; see interrupt threshold in the command register */
    uint32_t            uIrqInterval;
    /** @} */

    /** @name Frame counter partition (registers)
     * @{ */
    /** HcFmNumber.
     * @remark The register size is 16-bit, but for debugging and performance
     *         reasons we maintain a 32-bit counter. */
    uint32_t            HcFmNumber;
    /** Number of micro-frames per timer call */
    uint32_t            uFramesPerTimerCall;
    /** @} */

    /** Flag whether the framer thread should processing frames. */
    volatile bool       fBusStarted;

    bool                afAlignment1[3]; /**< Align CsIrq correctly. */

    /* The following members are not part of saved state. */

    /** Critical section synchronising interrupt handling. */
    PDMCRITSECT         CsIrq;

    /** The MMIO region. */
    IOMMMIOHANDLE       hMmio;
} EHCI;
AssertCompileMemberAlignment(EHCI, CsIrq, 8);
/** Pointer to shared EHCI device data. */
typedef struct EHCI *PEHCI;


/**
 * EHCI device data, ring-3 edition.
 */
typedef struct EHCIR3
{
    /** Root hub device. */
    EHCIROOTHUBR3       RootHub;

    /** The number of virtual time ticks per frame. */
    uint64_t            cTicksPerFrame;
    /** The number of virtual time ticks per USB bus tick. */
    uint64_t            cTicksPerUsbTick;

    /** Pointer to the device instance. */
    PPDMDEVINSR3        pDevIns;

    /** Number of in-flight TDs. */
    unsigned            cInFlight;
    unsigned            Alignment2;    /**< Align aInFlight on a 8 byte boundary. */
    /** Array of in-flight TDs. */
    struct ehci_td_in_flight
    {
        /** Address of the transport descriptor. */
        RTGCPHYS   GCPhysTD;
        /** Pointer to the URB. */
        R3PTRTYPE(PVUSBURB) pUrb;
    } aInFlight[257];

    /** Detected canceled isochronous URBs. */
    STAMCOUNTER         StatCanceledIsocUrbs;
    /** Detected canceled general URBs. */
    STAMCOUNTER         StatCanceledGenUrbs;
    /** Dropped URBs (endpoint halted, or URB canceled). */
    STAMCOUNTER         StatDroppedUrbs;

    /* The following members are not part of saved state. */

    /** VM timer frequency used for frame timer calculations. */
    uint64_t            u64TimerHz;
    /** Number of USB work cycles with no transfers. */
    uint32_t            cIdleCycles;
    /** Current frame timer rate (default 1000). */
    uint32_t            uFrameRate;
    /** Idle detection flag; must be cleared at start of frame */
    bool                fIdle;
    bool                afAlignment4[3];

    /** Default frequency of the frame timer. */
    uint32_t            uFrameRateDefault;
    /** How long to wait until the next frame. */
    uint64_t            nsWait;
    /** The framer thread. */
    R3PTRTYPE(PPDMTHREAD)      hThreadFrame;
    /** Event semaphore to interact with the framer thread. */
    R3PTRTYPE(RTSEMEVENTMULTI) hSemEventFrame;
    /** Event semaphore to release the thread waiting for the framer thread to stop. */
    R3PTRTYPE(RTSEMEVENTMULTI) hSemEventFrameStopped;
    /** Critical section to synchronize the framer and URB completion handler. */
    RTCRITSECT           CritSect;
} EHCIR3;
/** Pointer to ring-3 EHCI device data. */
typedef struct EHCIR3 *PEHCIR3;


/**
 * EHCI device data, ring-0 edition.
 */
typedef struct EHCIR0
{
    uint32_t uUnused;
} EHCIR0;
/** Pointer to ring-0 EHCI device data. */
typedef struct EHCIR0 *PEHCIR0;


/**
 * EHCI device data, raw-mode edition.
 */
typedef struct EHCIRC
{
    uint32_t uUnused;
} EHCIRC;
/** Pointer to raw-mode EHCI device data. */
typedef struct EHCIRC *PEHCIRC;


/** @typedef EHCICC
 * The EHCI device data for the current context. */
typedef CTX_SUFF(EHCI) EHCICC;
/** @typedef PEHCICC
 * Pointer to the EHCI device for the current context. */
typedef CTX_SUFF(PEHCI) PEHCICC;


/** @@name EHCI Transfer Descriptor Types
 * @{ */
/** Isochronous Transfer Descriptor */
#define EHCI_DESCRIPTOR_ITD     0
/** Queue Head */
#define EHCI_DESCRIPTOR_QH      1
/** Split Transaction Isochronous Transfer Descriptor */
#define EHCI_DESCRIPTOR_SITD    2
/** Frame Span Traversal Node */
#define EHCI_DESCRIPTOR_FSTN    3
/** @} */

/** @@name EHCI Transfer service type
 * @{ */
typedef enum
{
    EHCI_SERVICE_PERIODIC = 0,
    EHCI_SERVICE_ASYNC    = 1
} EHCI_SERVICE_TYPE;
/** @} */

/** @@name EHCI Frame List Element Pointer
 * @{ */
#define EHCI_FRAME_LIST_NEXTPTR_SHIFT          5

typedef struct
{
    uint32_t    Terminate   : 1;
    uint32_t    Type        : 2;
    uint32_t    Reserved    : 2;
    uint32_t    FrameAddr   : 27;
} EHCI_FRAME_LIST_PTR;
AssertCompileSize(EHCI_FRAME_LIST_PTR, 4);
/** @} */

/** @@name EHCI Isochronous Transfer Descriptor (iTD)
 * @{ */
#define EHCI_TD_PTR_SHIFT          5

typedef struct
{
    uint32_t    Terminate   : 1;
    uint32_t    Type        : 2;
    uint32_t    Reserved    : 2;
    uint32_t    Pointer     : 27;
} EHCI_TD_PTR;
AssertCompileSize(EHCI_TD_PTR, 4);

typedef struct
{
    uint32_t    Offset          : 12;
    uint32_t    PG              : 3;
    uint32_t    IOC             : 1;
    uint32_t    Length          : 12;
    uint32_t    TransactError   : 1;
    uint32_t    Babble          : 1;
    uint32_t    DataBufError    : 1;
    uint32_t    Active          : 1;
} EHCI_ITD_TRANSACTION;
AssertCompileSize(EHCI_ITD_TRANSACTION, 4);

typedef struct
{
    uint32_t    DeviceAddress   : 7;
    uint32_t    Reserved1       : 1;
    uint32_t    EndPt           : 4;
    uint32_t    Ignore1         : 20;
    uint32_t    MaxPacket       : 11;
    uint32_t    DirectionIn     : 1;
    uint32_t    Ignore2         : 20;
    uint32_t    Multi           : 2;
    uint32_t    Reserved10      : 10;
    uint32_t    Ignore3         : 20;
} EHCI_ITD_MISC;
AssertCompileSize(EHCI_ITD_MISC, 12);

#define EHCI_BUFFER_PTR_SHIFT       12

typedef struct
{
    uint32_t    Reserved        : 12;
    uint32_t    Pointer         : 20;   /* 4k aligned */
} EHCI_BUFFER_PTR;
AssertCompileSize(EHCI_BUFFER_PTR, 4);

#define EHCI_NUM_ITD_TRANSACTIONS           8
#define EHCI_NUM_ITD_PAGES                  7

typedef struct
{
    EHCI_TD_PTR             Next;
    EHCI_ITD_TRANSACTION    Transaction[EHCI_NUM_ITD_TRANSACTIONS];
    union
    {
        EHCI_ITD_MISC       Misc;
        EHCI_BUFFER_PTR     Buffer[EHCI_NUM_ITD_PAGES];
    } Buffer;
} EHCI_ITD, *PEHCI_ITD;
typedef const EHCI_ITD *PEHCI_CITD;
AssertCompileSize(EHCI_ITD, 0x40);
/** @} */

/* ITD with extra padding to add 8th and 9th 'Buffer' entry. The PG member of
 * EHCI_ITD_TRANSACTION can contain values in the 0-7 range, but only values
 * 0-6 are valid. The extra padding is added to avoid cluttering the code
 * with range checks; ehciR3ReadItd() initializes the pad with a safe value.
 * The EHCI 1.0 specification explicitly says using PG value of 7 yields
 * undefined behavior. Two pad entries are needed because initial PG value
 * of 7 (already 'wrong') can cross to the next page (8).
 */
typedef struct
{
    EHCI_TD_PTR             Next;
    EHCI_ITD_TRANSACTION    Transaction[EHCI_NUM_ITD_TRANSACTIONS];
    union
    {
        EHCI_ITD_MISC       Misc;
        EHCI_BUFFER_PTR     Buffer[EHCI_NUM_ITD_PAGES + 2];
    } Buffer;
} EHCI_ITD_PAD, *PEHCI_ITD_PAD;
AssertCompileSize(EHCI_ITD_PAD, 0x48);

/** @name Split Transaction Isochronous Transfer Descriptor (siTD)
 * @{ */
typedef struct
{
    uint32_t                DeviceAddress   : 7;
    uint32_t                Reserved        : 1;
    uint32_t                EndPt           : 4;
    uint32_t                Reserved2       : 4;
    uint32_t                HubAddress      : 7;
    uint32_t                Reserved3       : 1;
    uint32_t                Port            : 7;
    uint32_t                DirectionIn     : 1;
} EHCI_SITD_ADDR;
AssertCompileSize(EHCI_SITD_ADDR, 4);

typedef struct
{
    uint32_t                SMask       : 8;
    uint32_t                CMask       : 8;
    uint32_t                Reserved    : 16;
} EHCI_SITD_SCHEDCTRL;
AssertCompileSize(EHCI_SITD_SCHEDCTRL, 4);

typedef struct
{
    /* 8 Status flags */
    uint32_t                Reserved        : 1;
    uint32_t                SplitXState     : 1;
    uint32_t                MisseduFrame    : 1;
    uint32_t                TransactError   : 1;
    uint32_t                Babble          : 1;
    uint32_t                DataBufError    : 1;
    uint32_t                Error           : 1;
    uint32_t                Active          : 1;
    uint32_t                CPMask          : 8;
    uint32_t                Length          : 10;
    uint32_t                Reserved4       : 4;
    uint32_t                PageSelect      : 1;
    uint32_t                IOC             : 1;
} EHCI_SITD_TRANSFER;
AssertCompileSize(EHCI_SITD_TRANSFER, 4);

typedef struct
{
    uint32_t    Offset          : 12;
    uint32_t    Pointer         : 20;   /**< 4k aligned */
} EHCI_SITD_BUFFER0;
AssertCompileSize(EHCI_SITD_BUFFER0, 4);

typedef struct
{
    uint32_t    TCount          : 3;
    uint32_t    TPosition       : 2;
    uint32_t    Reserved        : 7;
    uint32_t    Pointer         : 20;   /**< 4k aligned */
} EHCI_SITD_BUFFER1;
AssertCompileSize(EHCI_SITD_BUFFER1, 4);

typedef struct
{
    uint32_t    Terminate       : 1;
    uint32_t    Reserved        : 4;
    uint32_t    Pointer         : 27;
} EHCI_SITD_BACKPTR;
AssertCompileSize(EHCI_SITD_BACKPTR, 4);

typedef struct
{
    EHCI_TD_PTR             NextSITD;
    EHCI_SITD_ADDR          Address;
    EHCI_SITD_SCHEDCTRL     ScheduleCtrl;
    EHCI_SITD_TRANSFER      Transfer;
    EHCI_SITD_BUFFER0       Buffer0;
    EHCI_SITD_BUFFER1       Buffer1;
    EHCI_SITD_BACKPTR       BackPtr;
} EHCI_SITD, *PEHCI_SITD;
typedef const EHCI_SITD *PEHCI_CSITD;
AssertCompileSize(EHCI_SITD, 0x1C);
/** @} */


/** @name Queue Element Transfer Descriptor (qTD)
 * @{ */
typedef struct
{
    uint32_t    Terminate       : 1;
    uint32_t    Reserved        : 4;
    uint32_t    Pointer         : 27;
} EHCI_QTD_NEXTPTR;
AssertCompileSize(EHCI_QTD_NEXTPTR, 4);

typedef struct
{
    uint32_t    Terminate       : 1;
    uint32_t    Reserved        : 4;
    uint32_t    Pointer         : 27;
} EHCI_QTD_ALTNEXTPTR;
AssertCompileSize(EHCI_QTD_ALTNEXTPTR, 4);

#define EHCI_QTD_PID_OUT                    0
#define EHCI_QTD_PID_IN                     1
#define EHCI_QTD_PID_SETUP                  2

typedef struct
{
    /* 8 Status flags */
    uint32_t                PingState       : 1;
    uint32_t                SplitXState     : 1;
    uint32_t                MisseduFrame    : 1;
    uint32_t                TransactError   : 1;
    uint32_t                Babble          : 1;
    uint32_t                DataBufError    : 1;
    uint32_t                Halted          : 1;
    uint32_t                Active          : 1;
    uint32_t                PID             : 2;
    uint32_t                ErrorCount      : 2;
    uint32_t                CurrentPage     : 3;
    uint32_t                IOC             : 1;
    uint32_t                Length          : 15;
    uint32_t                DataToggle      : 1;
} EHCI_QTD_TOKEN;
AssertCompileSize(EHCI_QTD_TOKEN, 4);

#define EHCI_QTD_HAS_ERROR(pQtdToken)           (*((uint32_t *)pQtdToken) & 0x7F)

typedef struct
{
    uint32_t    Offset          : 12;
    uint32_t    Reserved        : 20;
    uint32_t    Ignore[4];
} EHCI_QTD_OFFSET;
AssertCompileSize(EHCI_QTD_OFFSET, 20);

typedef struct
{
    EHCI_QTD_NEXTPTR    Next;
    EHCI_QTD_ALTNEXTPTR AltNext;
    union
    {
        EHCI_QTD_TOKEN  Bits;
        uint32_t        u32;
    } Token;
    union
    {
        EHCI_QTD_OFFSET Offset;
        EHCI_BUFFER_PTR Buffer[5];
    } Buffer;
} EHCI_QTD, *PEHCI_QTD;
typedef const EHCI_QTD *PEHCI_CQTD;
AssertCompileSize(EHCI_QTD, 0x20);
/** @} */


/** @name Queue Head Descriptor (QHD)
 * @{ */

#define EHCI_QHD_EPT_SPEED_FULL         0   /**< 12 Mbps  */
#define EHCI_QHD_EPT_SPEED_LOW          1   /**< 1.5 Mbps */
#define EHCI_QHD_EPT_SPEED_HIGH         2   /**< 480 Mbps */
#define EHCI_QHD_EPT_SPEED_RESERVED     3

typedef struct
{
    uint32_t    DeviceAddress       : 7;
    uint32_t    InActiveNext        : 1;
    uint32_t    EndPt               : 4;
    uint32_t    EndPtSpeed          : 2;
    uint32_t    DataToggle          : 1;
    uint32_t    HeadReclamation     : 1;
    uint32_t    MaxLength           : 11;
    uint32_t    ControlEPFlag       : 1;
    uint32_t    NakCountReload      : 4;
} EHCI_QHD_EPCHARS;
AssertCompileSize(EHCI_QHD_EPCHARS, 4);

typedef struct
{
    uint32_t    SMask               : 8;
    uint32_t    CMask               : 8;
    uint32_t    HubAddress          : 7;
    uint32_t    Port                : 7;
    uint32_t    Mult                : 2;
} EHCI_QHD_EPCAPS;
AssertCompileSize(EHCI_QHD_EPCAPS, 4);

typedef struct
{
    uint32_t    Reserved        : 5;
    uint32_t    Pointer         : 27;
} EHCI_QHD_CURRPTR;
AssertCompileSize(EHCI_QHD_CURRPTR, 4);

typedef struct
{
    uint32_t    Terminate       : 1;
    uint32_t    NakCnt          : 4;
    uint32_t    Pointer         : 27;
} EHCI_QHD_ALTNEXT;
AssertCompileSize(EHCI_QHD_ALTNEXT, 4);

typedef struct
{
    uint32_t    CProgMask       : 8;
    uint32_t    Reserved        : 4;
    uint32_t    Pointer         : 20;   /**< 4k aligned */
} EHCI_QHD_BUFFER1;
AssertCompileSize(EHCI_QHD_BUFFER1, 4);

typedef struct
{
    uint32_t    FrameTag        : 5;
    uint32_t    SBytes          : 7;
    uint32_t    Pointer         : 20;   /**< 4k aligned */
} EHCI_QHD_BUFFER2;
AssertCompileSize(EHCI_QHD_BUFFER2, 4);

typedef struct
{
    EHCI_TD_PTR         Next;
    EHCI_QHD_EPCHARS    Characteristics;
    EHCI_QHD_EPCAPS     Caps;
    EHCI_QHD_CURRPTR    CurrQTD;
    union
    {
        EHCI_QTD        OrgQTD;
        struct
        {
            uint32_t            Identical1[2];
            EHCI_QHD_ALTNEXT    AltNextQTD;
            uint32_t            Identical2;
            EHCI_QHD_BUFFER1    Buffer1;
            EHCI_QHD_BUFFER2    Buffer2;
            uint32_t            Identical3[2];
        } Status;
    } Overlay;
} EHCI_QHD, *PEHCI_QHD;
typedef const EHCI_QHD *PEHCI_CQHD;
AssertCompileSize(EHCI_QHD, 0x30);
/** @} */

/** @name Periodic Frame Span Traversal Node (FSTN)
 * @{ */

typedef struct
{
    uint32_t    Terminate       : 1;
    uint32_t    Type            : 2;
    uint32_t    Reserved        : 2;
    uint32_t    Ptr             : 27;
} EHCI_FSTN_PTR;
AssertCompileSize(EHCI_FSTN_PTR, 4);

typedef struct
{
    EHCI_FSTN_PTR   NormalPtr;
    EHCI_FSTN_PTR   BackPtr;
} EHCI_FSTN, *PEHCI_FSTN;
typedef const EHCI_FSTN *PEHCI_CFSTN;
AssertCompileSize(EHCI_FSTN, 8);

/** @} */


/**
 * EHCI register operator.
 */
typedef struct EHCIOPREG
{
    const char *pszName;
    VBOXSTRICTRC (*pfnRead )(PPDMDEVINS pDevIns, PEHCI ehci, uint32_t iReg, uint32_t *pu32Value);
    VBOXSTRICTRC (*pfnWrite)(PPDMDEVINS pDevIns, PEHCI ehci, uint32_t iReg, uint32_t u32Value);
} EHCIOPREG;


/* EHCI Local stuff */
#define EHCI_HCS_PARAMS_PORT_ROUTING_RULES          RT_BIT(7)
#define EHCI_HCS_PARAMS_PORT_POWER_CONTROL          RT_BIT(4)
#define EHCI_HCS_PARAMS_NDP_MASK                    (RT_BIT(0) | RT_BIT(1) | RT_BIT(2) | RT_BIT(3))

/* controller may cache an isochronous data structure for an entire frame */
#define EHCI_HCC_PARAMS_ISOCHRONOUS_CACHING         RT_BIT(7)
#define EHCI_HCC_PARAMS_ASYNC_SCHEDULE_PARKING      RT_BIT(2)
#define EHCI_HCC_PARAMS_PROGRAMMABLE_FRAME_LIST     RT_BIT(1)
#define EHCI_HCC_PARAMS_64BITS_ADDRESSING           RT_BIT(0)

/** @name Interrupt Enable Register bits (USBINTR)
 * @{ */
#define EHCI_INTR_ENABLE_THRESHOLD                  RT_BIT(0)
#define EHCI_INTR_ENABLE_ERROR                      RT_BIT(1)
#define EHCI_INTR_ENABLE_PORT_CHANGE                RT_BIT(2)
#define EHCI_INTR_ENABLE_FRAME_LIST_ROLLOVER        RT_BIT(3)
#define EHCI_INTR_ENABLE_HOST_SYSTEM_ERROR          RT_BIT(4)
#define EHCI_INTR_ENABLE_ASYNC_ADVANCE              RT_BIT(5)
#define EHCI_INTR_ENABLE_MASK                       (EHCI_INTR_ENABLE_ASYNC_ADVANCE|EHCI_INTR_ENABLE_HOST_SYSTEM_ERROR|EHCI_INTR_ENABLE_FRAME_LIST_ROLLOVER|EHCI_INTR_ENABLE_PORT_CHANGE|EHCI_INTR_ENABLE_ERROR|EHCI_INTR_ENABLE_THRESHOLD)
/** @} */

/** @name Configure Flag Register (CONFIGFLAG)
 * @{ */
#define EHCI_CONFIGFLAG_ROUTING                     RT_BIT(0)
#define EHCI_CONFIGFLAG_MASK                        EHCI_CONFIGFLAG_ROUTING
/** @} */

/** @name Status Register (USBSTS)
 * @{ */
#define EHCI_STATUS_ASYNC_SCHED                     RT_BIT(15)     /* RO */
#define EHCI_STATUS_PERIOD_SCHED                    RT_BIT(14)     /* RO */
#define EHCI_STATUS_RECLAMATION                     RT_BIT(13)     /* RO */
#define EHCI_STATUS_HCHALTED                        RT_BIT(12)     /* RO */
#define EHCI_STATUS_INT_ON_ASYNC_ADV                RT_BIT(5)
#define EHCI_STATUS_HOST_SYSTEM_ERROR               RT_BIT(4)
#define EHCI_STATUS_FRAME_LIST_ROLLOVER             RT_BIT(3)
#define EHCI_STATUS_PORT_CHANGE_DETECT              RT_BIT(2)
#define EHCI_STATUS_ERROR_INT                       RT_BIT(1)
#define EHCI_STATUS_THRESHOLD_INT                   RT_BIT(0)
#define EHCI_STATUS_INTERRUPT_MASK                  (EHCI_STATUS_THRESHOLD_INT|EHCI_STATUS_ERROR_INT|EHCI_STATUS_PORT_CHANGE_DETECT|EHCI_STATUS_FRAME_LIST_ROLLOVER|EHCI_STATUS_HOST_SYSTEM_ERROR|EHCI_STATUS_INT_ON_ASYNC_ADV)
/** @} */

#define EHCI_PERIODIC_LIST_MASK                     UINT32_C(0xFFFFF000)  /**< 4kb aligned */
#define EHCI_ASYNC_LIST_MASK                        UINT32_C(0xFFFFFFE0)  /**< 32-byte aligned */


/** @name Port Status and Control Register bits (PORTSC)
 * @{ */
#define EHCI_PORT_CURRENT_CONNECT                   RT_BIT(0)                  /**< RO */
#define EHCI_PORT_CONNECT_CHANGE                    RT_BIT(1)
#define EHCI_PORT_PORT_ENABLED                      RT_BIT(2)
#define EHCI_PORT_PORT_CHANGE                       RT_BIT(3)
#define EHCI_PORT_OVER_CURRENT_ACTIVE               RT_BIT(4)                  /**< RO */
#define EHCI_PORT_OVER_CURRENT_CHANGE               RT_BIT(5)
#define EHCI_PORT_FORCE_PORT_RESUME                 RT_BIT(6)
#define EHCI_PORT_SUSPEND                           RT_BIT(7)
#define EHCI_PORT_RESET                             RT_BIT(8)
#define EHCI_PORT_LINE_STATUS_MASK                  (RT_BIT(10) | RT_BIT(11))  /**< RO */
#define EHCI_PORT_LINE_STATUS_SHIFT                 10
#define EHCI_PORT_POWER                             RT_BIT(12)
#define EHCI_PORT_OWNER                             RT_BIT(13)
#define EHCI_PORT_INDICATOR                         (RT_BIT(14) | RT_BIT(15))
#define EHCI_PORT_TEST_CONTROL_MASK                 (RT_BIT(16) | RT_BIT(17) | RT_BIT(18) | RT_BIT(19))
#define EHCI_PORT_TEST_CONTROL_SHIFT                16
#define EHCI_PORT_WAKE_ON_CONNECT_ENABLE            RT_BIT(20)
#define EHCI_PORT_WAKE_ON_DISCONNECT_ENABLE         RT_BIT(21)
#define EHCI_PORT_WAKE_OVER_CURRENT_ENABLE          RT_BIT(22)
#define EHCI_PORT_RESERVED                          (RT_BIT(9)|RT_BIT(23)|RT_BIT(24)|RT_BIT(25)|RT_BIT(26)|RT_BIT(27)|RT_BIT(28)|RT_BIT(29)|RT_BIT(30)|RT_BIT(31))

#define EHCI_PORT_WAKE_MASK                         (EHCI_PORT_WAKE_ON_CONNECT_ENABLE|EHCI_PORT_WAKE_ON_DISCONNECT_ENABLE|EHCI_PORT_WAKE_OVER_CURRENT_ENABLE)
#define EHCI_PORT_CHANGE_MASK                       (EHCI_PORT_CONNECT_CHANGE|EHCI_PORT_PORT_CHANGE|EHCI_PORT_OVER_CURRENT_CHANGE)
/** @} */

/** @name Command Register bits (USBCMD)
 * @{ */
#define EHCI_CMD_RUN                                RT_BIT(0)
#define EHCI_CMD_RESET                              RT_BIT(1)
#define EHCI_CMD_FRAME_LIST_SIZE_MASK               (RT_BIT(2) | RT_BIT(3))
#define EHCI_CMD_FRAME_LIST_SIZE_SHIFT              2
#define EHCI_CMD_PERIODIC_SCHED_ENABLE              RT_BIT(4)
#define EHCI_CMD_ASYNC_SCHED_ENABLE                 RT_BIT(5)
#define EHCI_CMD_INT_ON_ADVANCE_DOORBELL            RT_BIT(6)
#define EHCI_CMD_SOFT_RESET                         RT_BIT(7)               /**< optional */
#define EHCI_CMD_ASYNC_SCHED_PARK_MODE_COUNT_MASK   (RT_BIT(8) | RT_BIT(9)) /**< optional */
#define EHCI_CMD_ASYNC_SCHED_PARK_MODE_COUNT_SHIFT  8
#define EHCI_CMD_RESERVED                           RT_BIT(10)
#define EHCI_CMD_ASYNC_SCHED_PARK_ENABLE            RT_BIT(11)              /**< optional */
#define EHCI_CMD_RESERVED2                          (RT_BIT(12) | RT_BIT(13) | RT_BIT(14) | RT_BIT(15))
#define EHCI_CMD_INTERRUPT_THRESHOLD_MASK           (RT_BIT(16) | RT_BIT(17) | RT_BIT(18) | RT_BIT(19) | RT_BIT(20) | RT_BIT(21) | RT_BIT(22) | RT_BIT(23))
#define EHCI_CMD_INTERRUPT_THRESHOLD_SHIFT          16
#define EHCI_CMD_MASK                               (EHCI_CMD_INTERRUPT_THRESHOLD_MASK|EHCI_CMD_ASYNC_SCHED_PARK_ENABLE|EHCI_CMD_ASYNC_SCHED_PARK_MODE_COUNT_MASK|EHCI_CMD_SOFT_RESET|EHCI_CMD_INT_ON_ADVANCE_DOORBELL|EHCI_CMD_ASYNC_SCHED_ENABLE|EHCI_CMD_PERIODIC_SCHED_ENABLE|EHCI_CMD_FRAME_LIST_SIZE_MASK|EHCI_CMD_RESET|EHCI_CMD_RUN)

#define EHCI_DEFAULT_PERIODIC_LIST_SIZE             1024
#define EHCI_DEFAULT_PERIODIC_LIST_MASK             0x3ff

#define EHCI_FRINDEX_UFRAME_COUNT_MASK              0x7
#define EHCI_FRINDEX_FRAME_INDEX_MASK               EHCI_DEFAULT_PERIODIC_LIST_MASK
#define EHCI_FRINDEX_FRAME_INDEX_SHIFT              3

/** @} */

/* Local EHCI definitions */
#define  EHCI_USB_RESET                             0x00
#define  EHCI_USB_RESUME                            0x40
#define  EHCI_USB_OPERATIONAL                       0x80
#define  EHCI_USB_SUSPEND                           0xc0

#define EHCI_HARDWARE_TIMER_FREQ                    8000        /**< 8000 hz = every 125 usec */
#define EHCI_DEFAULT_TIMER_FREQ                     1000
#define EHCI_UFRAMES_PER_FRAME                      8

#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if defined(VBOX_IN_EXTPACK_R0) && defined(RT_OS_SOLARIS)
/* Dependency information for the native solaris loader. */
extern "C" { char _depends_on[] = "vboxdrv VMMR0.r0"; }
#endif

#if defined(LOG_ENABLED) && defined(IN_RING3)
static bool g_fLogControlEPs = false;
static bool g_fLogInterruptEPs = false;
#endif

#ifdef IN_RING3
/**
 * SSM descriptor table for the EHCI structure.
 */
static SSMFIELD const g_aEhciFields[] =
{
    SSMFIELD_ENTRY(         EHCI, fAsyncTraversalTimerActive),
    SSMFIELD_ENTRY(         EHCI, SofTime),
    SSMFIELD_ENTRY(         EHCI, RootHub.unused),
    SSMFIELD_ENTRY(         EHCI, RootHub.unused),
    SSMFIELD_ENTRY(         EHCI, RootHub.unused),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[0].fReg),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[1].fReg),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[2].fReg),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[3].fReg),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[4].fReg),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[5].fReg),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[6].fReg),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[7].fReg),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[8].fReg),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[9].fReg),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[10].fReg),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[11].fReg),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[12].fReg),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[13].fReg),
    SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[14].fReg),
    SSMFIELD_ENTRY(         EHCI, cap_length),
    SSMFIELD_ENTRY(         EHCI, hci_version),
    SSMFIELD_ENTRY(         EHCI, hcs_params),
    SSMFIELD_ENTRY(         EHCI, hcc_params),
    SSMFIELD_ENTRY(         EHCI, cmd),
    SSMFIELD_ENTRY(         EHCI, intr_status),
    SSMFIELD_ENTRY(         EHCI, intr),
    SSMFIELD_ENTRY(         EHCI, frame_idx),
    SSMFIELD_ENTRY(         EHCI, ds_segment),
    SSMFIELD_ENTRY(         EHCI, periodic_list_base),
    SSMFIELD_ENTRY(         EHCI, async_list_base),
    SSMFIELD_ENTRY(         EHCI, config),
    SSMFIELD_ENTRY(         EHCI, uIrqInterval),
    SSMFIELD_ENTRY(         EHCI, HcFmNumber),
    SSMFIELD_ENTRY(         EHCI, uFramesPerTimerCall),
    SSMFIELD_ENTRY(         EHCI, fBusStarted),
    SSMFIELD_ENTRY_TERM()
};
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
#ifdef IN_RING3
/* Update host controller state to reflect a device attach */
static void ehciR3PortPower(PEHCI pThis, PEHCICC pThisCC, unsigned iPort, bool fPowerUp);

static void ehciR3QHUpdateOverlay(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC,
                                  PEHCI_QHD pQhd, RTGCPHYS GCPhysQHD, PEHCI_QTD pQtd);
static void ehciR3CalcTimerIntervals(PEHCI pThis, PEHCICC pThisCC, uint32_t u32FrameRate);
#endif /* IN_RING3 */
RT_C_DECLS_END

/**
 * Update PCI IRQ levels
 */
static void ehciUpdateInterruptLocked(PPDMDEVINS pDevIns, PEHCI pThis, const char *msg)
{
    int level = 0;

    if (pThis->intr_status & pThis->intr)
        level = 1;

    PDMDevHlpPCISetIrq(pDevIns, 0, level);
    if (level)
    {
        uint32_t val = pThis->intr_status & pThis->intr;

        Log2Func(("Fired off interrupt %#010x - INT=%d ERR=%d PCD=%d FLR=%d HSE=%d IAA=%d - %s\n",
              val,
              !!(val & EHCI_STATUS_THRESHOLD_INT),
              !!(val & EHCI_STATUS_ERROR_INT),
              !!(val & EHCI_STATUS_PORT_CHANGE_DETECT),
              !!(val & EHCI_STATUS_FRAME_LIST_ROLLOVER),
              !!(val & EHCI_STATUS_HOST_SYSTEM_ERROR),
              !!(val & EHCI_STATUS_INT_ON_ASYNC_ADV),
              msg));
        RT_NOREF(val, msg);

        /* host controller must clear the EHCI_CMD_INT_ON_ADVANCE_DOORBELL bit after setting it in the status register */
        if (pThis->intr_status & EHCI_STATUS_INT_ON_ASYNC_ADV)
            ASMAtomicAndU32(&pThis->cmd, ~EHCI_CMD_INT_ON_ADVANCE_DOORBELL);

    }
    else
        Log2Func(("cleared interrupt\n"));
}

/**
 * Set an interrupt, use the wrapper ehciSetInterrupt.
 */
DECLINLINE(int) ehciSetInterruptInt(PPDMDEVINS pDevIns, PEHCI pThis, int rcBusy, uint32_t intr, const char *msg)
{
    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CsIrq, rcBusy);
    if (rc != VINF_SUCCESS)
        return rc;

    if ( (pThis->intr_status & intr) != intr )
    {
        ASMAtomicOrU32(&pThis->intr_status, intr);
        ehciUpdateInterruptLocked(pDevIns, pThis, msg);
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CsIrq);
    return rc;
}

/**
 * Set an interrupt wrapper macro for logging purposes.
 */
#define ehciSetInterrupt(a_pDevIns, a_pEhci, a_rcBusy, a_fIntr)  \
    ehciSetInterruptInt(a_pDevIns, a_pEhci, a_rcBusy,     a_fIntr, #a_fIntr)
#define ehciR3SetInterrupt(a_pDevIns, a_pEhci, a_fIntr) \
    ehciSetInterruptInt(a_pDevIns, a_pEhci, VERR_IGNORED, a_fIntr, #a_fIntr)

#ifdef IN_RING3

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) ehciR3RhQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PEHCICC pThisCC = RT_FROM_MEMBER(pInterface, EHCICC, RootHub.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->RootHub.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, VUSBIROOTHUBPORT, &pThisCC->RootHub.IRhPort);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDPORTS, &pThisCC->RootHub.ILeds);
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
static DECLCALLBACK(int) ehciR3RhQueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    PEHCICC pThisCC = RT_FROM_MEMBER(pInterface, EHCICC, RootHub.ILeds);
    if (iLUN == 0)
    {
        *ppLed = &pThisCC->RootHub.Led;
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}


/**
 * Get the number of avialable ports in the hub.
 *
 * @returns The number of ports available.
 * @param   pInterface      Pointer to this structure.
 * @param   pAvailable      Bitmap indicating the available ports. Set bit == available port.
 */
static DECLCALLBACK(unsigned) ehciR3RhGetAvailablePorts(PVUSBIROOTHUBPORT pInterface, PVUSBPORTBITMAP pAvailable)
{
    PEHCICC     pThisCC = RT_FROM_MEMBER(pInterface, EHCICC, RootHub.IRhPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PEHCI       pThis   = PDMDEVINS_2_DATA(pDevIns, PEHCI);

    memset(pAvailable, 0, sizeof(*pAvailable));

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    unsigned cPorts  = 0;
    for (unsigned iPort = 0; iPort < EHCI_NDP_CFG(pThis); iPort++)
    {
        if (!pThisCC->RootHub.aPorts[iPort].fAttached)
        {
            cPorts++;
            ASMBitSet(pAvailable, iPort + 1);
        }
    }

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return cPorts;
}


/**
 * Gets the supported USB versions.
 *
 * @returns The mask of supported USB versions.
 * @param   pInterface      Pointer to this structure.
 */
static DECLCALLBACK(uint32_t) ehciR3RhGetUSBVersions(PVUSBIROOTHUBPORT pInterface)
{
    RT_NOREF(pInterface);
    return VUSB_STDVER_20;
}


/** @interface_method_impl{VUSBIROOTHUBPORT,pfnAttach} */
static DECLCALLBACK(int) ehciR3RhAttach(PVUSBIROOTHUBPORT pInterface, uint32_t uPort, VUSBSPEED enmSpeed)
{
    PEHCICC     pThisCC = RT_FROM_MEMBER(pInterface, EHCICC, RootHub.IRhPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PEHCI       pThis   = PDMDEVINS_2_DATA(pDevIns, PEHCI);
    LogFlowFunc(("uPort=%u\n", uPort));
    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    /*
     * Validate and adjust input.
     */
    Assert(uPort >= 1 && uPort <= EHCI_NDP_CFG(pThis));
    uPort--;
    Assert(!pThisCC->RootHub.aPorts[uPort].fAttached);
    Assert(enmSpeed == VUSB_SPEED_HIGH); RT_NOREF(enmSpeed); /* Only HS devices should end up here! */

    /*
     * Attach it.
     */
    ASMAtomicAndU32(&pThis->RootHub.aPorts[uPort].fReg, ~EHCI_PORT_OWNER);  /* not attached to a companion controller */
    ASMAtomicOrU32(&pThis->RootHub.aPorts[uPort].fReg, EHCI_PORT_CURRENT_CONNECT | EHCI_PORT_CONNECT_CHANGE);
    pThisCC->RootHub.aPorts[uPort].fAttached = true;
    ehciR3PortPower(pThis, pThisCC, uPort, 1 /* power on */);

    ehciR3SetInterrupt(pDevIns, pThis, EHCI_STATUS_PORT_CHANGE_DETECT);

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return VINF_SUCCESS;
}


/**
 * A device is being detached from a port in the roothub.
 *
 * @param   pInterface      Pointer to this structure.
 * @param   uPort           The port number assigned to the device.
 */
static DECLCALLBACK(void) ehciR3RhDetach(PVUSBIROOTHUBPORT pInterface, uint32_t uPort)
{
    PEHCICC     pThisCC = RT_FROM_MEMBER(pInterface, EHCICC, RootHub.IRhPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PEHCI       pThis   = PDMDEVINS_2_DATA(pDevIns, PEHCI);
    LogFlowFunc(("uPort=%u\n", uPort));
    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    /*
     * Validate and adjust input.
     */
    Assert(uPort >= 1 && uPort <= EHCI_NDP_CFG(pThis));
    uPort--;
    Assert(pThisCC->RootHub.aPorts[uPort].fAttached);

    /*
     * Detach it.
     */
    pThisCC->RootHub.aPorts[uPort].fAttached = false;
    ASMAtomicAndU32(&pThis->RootHub.aPorts[uPort].fReg, ~EHCI_PORT_CURRENT_CONNECT);
    if (pThis->RootHub.aPorts[uPort].fReg & EHCI_PORT_PORT_ENABLED)
    {
        ASMAtomicAndU32(&pThis->RootHub.aPorts[uPort].fReg, ~EHCI_PORT_PORT_ENABLED);
        ASMAtomicOrU32(&pThis->RootHub.aPorts[uPort].fReg, EHCI_PORT_CONNECT_CHANGE | EHCI_PORT_PORT_CHANGE);
    }
    else
        ASMAtomicOrU32(&pThis->RootHub.aPorts[uPort].fReg, EHCI_PORT_CONNECT_CHANGE);

    ehciR3SetInterrupt(pDevIns, pThis, EHCI_STATUS_PORT_CHANGE_DETECT);

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
}


/**
 * One of the roothub devices has completed its reset operation.
 *
 * Currently, we don't think anything is required to be done here
 * so it's just a stub for forcing async resetting of the devices
 * during a root hub reset.
 *
 * @param pDev      The root hub device.
 * @param uPort     The port number of the device on the roothub being resetted.
 * @param rc        The result of the operation.
 * @param pvUser    Pointer to the controller.
 */
static DECLCALLBACK(void) ehciR3RhResetDoneOneDev(PVUSBIDEVICE pDev, uint32_t uPort, int rc, void *pvUser)
{
    LogRel(("EHCI: root hub reset completed with %Rrc\n", rc));
    RT_NOREF(pDev, uPort, rc, pvUser);
}


/**
 * Does a software or hardware reset of the controller.
 *
 * This is called in response to setting HcCommandStatus.HCR, hardware reset,
 * and device construction.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared EHCI instance data.
 * @param   pThisCC         The ring-3 EHCI instance data.
 * @param   fNewMode        The new mode of operation. This is UsbSuspend if it's a
 *                          software reset, and UsbReset if it's a hardware reset / cold boot.
 * @param   fResetOnLinux   Set if we can do a real reset of the devices attached to the root hub.
 *                          This is really a just a hack for the non-working linux device reset.
 *                          Linux has this feature called 'logical disconnect' if device reset fails
 *                          which prevents us from doing resets when the guest asks for it - the guest
 *                          will get confused when the device seems to be reconnected everytime it tries
 *                          to reset it. But if we're at hardware reset time, we can allow a device to
 *                          be 'reconnected' without upsetting the guest.
 *
 * @remark  This hasn't got anything to do with software setting the mode to UsbReset.
 */
static void ehciR3DoReset(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC, uint32_t fNewMode, bool fResetOnLinux)
{
    LogFunc(("%s reset%s\n", fNewMode == EHCI_USB_RESET ? "hardware" : "software",
         fResetOnLinux ? " (reset on linux)" : ""));

    /*
     * Cancel all outstanding URBs.
     *
     * We can't, and won't, deal with URBs until we're moved out of the
     * suspend/reset state. Also, a real HC isn't going to send anything
     * any more when a reset has been signaled.
     *
     * This must be done on the framer thread to avoid race conditions.
     */
    pThisCC->RootHub.pIRhConn->pfnCancelAllUrbs(pThisCC->RootHub.pIRhConn);

    /*
     * Reset the hardware registers.
     */
    /** @todo other differences between hardware reset and VM reset? */

    if (pThis->hcc_params & EHCI_HCC_PARAMS_ASYNC_SCHEDULE_PARKING)
        pThis->cmd              = 0x80000 | EHCI_CMD_ASYNC_SCHED_PARK_ENABLE | (3 << EHCI_CMD_ASYNC_SCHED_PARK_MODE_COUNT_SHIFT);
    else
        pThis->cmd              = 0x80000;

    pThis->intr_status          = EHCI_STATUS_HCHALTED;
    pThis->intr                 = 0;
    pThis->frame_idx            = 0;
    pThis->ds_segment           = 0;
    pThis->periodic_list_base   = 0;    /* undefined */
    pThis->async_list_base      = 0;    /* undefined */
    pThis->config               = 0;
    pThis->uIrqInterval         = (pThis->intr_status & EHCI_CMD_INTERRUPT_THRESHOLD_MASK) >> EHCI_CMD_INTERRUPT_THRESHOLD_SHIFT;

    /* We have to update interrupts as the IRQ may need to be cleared. */
    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CsIrq, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CsIrq, rcLock);

    ehciUpdateInterruptLocked(pDevIns, pThis, "ehciR3DoReset");

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CsIrq);

    ehciR3CalcTimerIntervals(pThis, pThisCC, pThisCC->uFrameRateDefault);

    if (fNewMode == EHCI_USB_RESET)
    {
        /* Only a hardware reset reinits the port registers */
        for (unsigned i = 0; i < EHCI_NDP_CFG(pThis); i++)
        {
            if (pThis->hcs_params & EHCI_HCS_PARAMS_PORT_POWER_CONTROL)
                pThis->RootHub.aPorts[i].fReg = EHCI_PORT_OWNER;
            else
                pThis->RootHub.aPorts[i].fReg = EHCI_PORT_POWER | EHCI_PORT_OWNER;
        }
    }
/** @todo Shouldn't we stop the SOF timer at this point? */

    /*
     * If this is a hardware reset, we will initialize the root hub too.
     * Software resets doesn't do this according to the specs.
     * (It's not possible to have device connected at the time of the
     * device construction, so nothing to worry about there.)
     */
    if (fNewMode == EHCI_USB_RESET)
    {
        pThisCC->RootHub.pIRhConn->pfnReset(pThisCC->RootHub.pIRhConn, fResetOnLinux);

        /*
         * Reattach the devices.
         */
        for (unsigned i = 0; i < EHCI_NDP_CFG(pThis); i++)
        {
            bool fAttached = pThisCC->RootHub.aPorts[i].fAttached;
            pThisCC->RootHub.aPorts[i].fAttached = false;

            if (fAttached)
                ehciR3RhAttach(&pThisCC->RootHub.IRhPort, i+1, VUSB_SPEED_HIGH);
        }
    }
}

/**
 * Reset the root hub.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to this structure.
 * @param   fResetOnLinux   This is used to indicate whether we're at VM reset time and
 *                          can do real resets or if we're at any other time where that
 *                          isn't such a good idea.
 * @remark  Do NOT call VUSBIDevReset on the root hub in an async fashion!
 * @thread  EMT
 */
static DECLCALLBACK(int) ehciR3RhReset(PVUSBIROOTHUBPORT pInterface, bool fResetOnLinux)
{
    PEHCICC     pThisCC = RT_FROM_MEMBER(pInterface, EHCICC, RootHub.IRhPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PEHCI       pThis   = PDMDEVINS_2_DATA(pDevIns, PEHCI);
    LogFunc(("fResetOnLinux=%d\n", fResetOnLinux));

    /* Soft reset first */
    ehciR3DoReset(pDevIns, pThis, pThisCC, EHCI_USB_SUSPEND, false /* N/A */);

    /*
     * We're pretending to _reattach_ the devices without resetting them.
     * Except, during VM reset where we use the opportunity to do a proper
     * reset before the guest comes along and expects things.
     *
     * However, it's very very likely that we're not doing the right thing
     * here when end up here on request from the guest (USB Reset state).
     * The docs talks about root hub resetting, however what exact behaviour
     * in terms of root hub status and changed bits, and HC interrupts aren't
     * stated clearly. IF we get trouble and see the guest doing "USB Resets"
     * we will have to look into this. For the time being we stick with simple.
     */
    for (unsigned iPort = 0; iPort < EHCI_NDP_CFG(pThis); iPort++)
    {
        if (pThisCC->RootHub.aPorts[iPort].fAttached)
        {
            ASMAtomicOrU32(&pThis->RootHub.aPorts[iPort].fReg, EHCI_PORT_CURRENT_CONNECT | EHCI_PORT_CONNECT_CHANGE);
            if (fResetOnLinux)
            {
                PVM pVM = PDMDevHlpGetVM(pDevIns);
                VUSBIRhDevReset(pThisCC->RootHub.pIRhConn, EHCI_PORT_2_VUSB_PORT(iPort), fResetOnLinux,
                                ehciR3RhResetDoneOneDev, pThis, pVM);
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Reads physical memory.
 */
DECLINLINE(void) ehciPhysRead(PPDMDEVINS pDevIns, RTGCPHYS Addr, void *pvBuf, size_t cbBuf)
{
    PDMDevHlpPCIPhysReadUser(pDevIns, Addr, pvBuf, cbBuf);
}


/**
 * Reads physical memory - metadata.
 */
DECLINLINE(void) ehciPhysReadMeta(PPDMDEVINS pDevIns, RTGCPHYS Addr, void *pvBuf, size_t cbBuf)
{
    PDMDevHlpPCIPhysReadMeta(pDevIns, Addr, pvBuf, cbBuf);
}


/**
 * Writes physical memory.
 */
DECLINLINE(void) ehciPhysWrite(PPDMDEVINS pDevIns, RTGCPHYS Addr, const void *pvBuf, size_t cbBuf)
{
    PDMDevHlpPCIPhysWriteUser(pDevIns, Addr, pvBuf, cbBuf);
}


/**
 * Writes physical memory.
 */
DECLINLINE(void) ehciPhysWriteMeta(PPDMDEVINS pDevIns, RTGCPHYS Addr, const void *pvBuf, size_t cbBuf)
{
    PDMDevHlpPCIPhysWriteMeta(pDevIns, Addr, pvBuf, cbBuf);
}


/**
 * Read an array of dwords from physical memory and correct endianness.
 */
DECLINLINE(void) ehciGetDWords(PPDMDEVINS pDevIns, RTGCPHYS Addr, uint32_t *pau32s, int c32s)
{
    ehciPhysReadMeta(pDevIns, Addr, pau32s, c32s * sizeof(uint32_t));
# ifndef RT_LITTLE_ENDIAN
    for(int i = 0; i < c32s; i++)
        pau32s[i] = RT_H2LE_U32(pau32s[i]);
# endif
}


/**
 * Write an array of dwords from physical memory and correct endianness.
 */
DECLINLINE(void) ehciPutDWords(PPDMDEVINS pDevIns, RTGCPHYS Addr, const uint32_t *pau32s, int cu32s)
{
# ifdef RT_LITTLE_ENDIAN
    ehciPhysWriteMeta(pDevIns, Addr, pau32s, cu32s << 2);
# else
    for (int i = 0; i < c32s; i++, pau32s++, Addr += sizeof(*pau32s))
    {
        uint32_t u32Tmp = RT_H2LE_U32(*pau32s);
        ehciPhysWriteMeta(pDevIns, Addr, (uint8_t *)&u32Tmp, sizeof(u32Tmp));
    }
# endif
}


DECLINLINE(void) ehciR3ReadFrameListPtr(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, EHCI_FRAME_LIST_PTR *pFrameList)
{
    ehciGetDWords(pDevIns, GCPhys, (uint32_t *)pFrameList, sizeof(*pFrameList) >> 2);
}

DECLINLINE(void) ehciR3ReadTDPtr(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, EHCI_TD_PTR *pTD)
{
    ehciGetDWords(pDevIns, GCPhys, (uint32_t *)pTD, sizeof(*pTD) >> 2);
}

DECLINLINE(void) ehciR3ReadItd(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, PEHCI_ITD_PAD pPItd)
{
    ehciGetDWords(pDevIns, GCPhys, (uint32_t *)pPItd, sizeof(EHCI_ITD) >> 2);
    pPItd->Buffer.Buffer[7].Pointer = 0xFFFFF;  /* Direct ill-defined accesses to the last page under 4GB (ROM). */
    pPItd->Buffer.Buffer[8].Pointer = 0xFFFFF;
}

DECLINLINE(void) ehciR3ReadSitd(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, PEHCI_SITD pSitd)
{
    ehciGetDWords(pDevIns, GCPhys, (uint32_t *)pSitd, sizeof(*pSitd) >> 2);
}

DECLINLINE(void) ehciR3WriteItd(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, PEHCI_ITD_PAD pItd)
{
    /** @todo might need to be careful about write order in async io thread */
    /*
     * Only write to the fields the controller is allowed to write to,
     * namely the eight double words coming after the next link pointer.
     */
    uint32_t offWrite = RT_OFFSETOF(EHCI_ITD, Transaction[0]);
    uint32_t offDWordsWrite = offWrite / sizeof(uint32_t);
    Assert(!(offWrite % sizeof(uint32_t)));

    ehciPutDWords(pDevIns, GCPhys + offWrite, (uint32_t *)pItd + offDWordsWrite,  (sizeof(EHCI_ITD) >> 2) - offDWordsWrite);
}

DECLINLINE(void) ehciR3ReadQHD(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, PEHCI_QHD pQHD)
{
    ehciGetDWords(pDevIns, GCPhys, (uint32_t *)pQHD, sizeof(*pQHD) >> 2);
}

DECLINLINE(void) ehciR3ReadQTD(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, PEHCI_QTD pQTD)
{
    ehciGetDWords(pDevIns, GCPhys, (uint32_t *)pQTD, sizeof(*pQTD) >> 2);
}

DECLINLINE(void) ehciR3WriteQTD(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, PEHCI_QTD pQTD)
{
    /** @todo might need to be careful about write order in async io thread */
    /*
     * Only write to the fields the controller is allowed to write to,
     * namely the two double words coming after the alternate next QTD pointer.
     */
    uint32_t offWrite = RT_OFFSETOF(EHCI_QTD, Token.u32);
    uint32_t offDWordsWrite = offWrite / sizeof(uint32_t);
    Assert(!(offWrite % sizeof(uint32_t)));

    ehciPutDWords(pDevIns, GCPhys + offWrite, (uint32_t *)pQTD + offDWordsWrite, (sizeof(*pQTD) >> 2) - offDWordsWrite);
}


/**
 * Updates the QHD in guest memory only updating portions of the QHD the controller
 * is allowed to write to.
 *
 * @param   pDevIns     The device instance.
 * @param   GCPhys      Physical guest address of the QHD.
 * @param   pQHD        The QHD to update the guest memory with.
 */
DECLINLINE(void) ehciR3UpdateQHD(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, PEHCI_QHD pQHD)
{
    /*
     * Only update members starting from the current QTD pointer, everything
     * before is readonly for the controller and the guest might have updated it
     * behind our backs already.
     */
    uint32_t offWrite = RT_OFFSETOF(EHCI_QHD, CurrQTD);
    ehciPhysWriteMeta(pDevIns, GCPhys + offWrite, (uint8_t *)pQHD + offWrite, sizeof(EHCI_QHD) - offWrite);
}

#ifdef LOG_ENABLED

# if 0 /* unused */
/**
 * Dumps a TD queue. LOG_ENABLED builds only.
 */
DECLINLINE(void) ehciR3DumpTdQueue(PEHCI pThis, RTGCPHYS GCPhysHead, const char *pszMsg)
{
    RT_NOREF(pThis, GCPhysHead, pszMsg);
    AssertFailed();
}
# endif /* unused */

/**
 * Dumps an SITD list. LOG_ENABLED builds only.
 */
DECLINLINE(void) ehciR3DumpSITD(PPDMDEVINS pDevIns, RTGCPHYS GCPhysHead, bool fList)
{
    RT_NOREF(pDevIns, GCPhysHead, fList);
    AssertFailed();
}

/**
 * Dumps an FSTN list. LOG_ENABLED builds only.
 */
DECLINLINE(void) ehciR3DumpFSTN(PPDMDEVINS pDevIns, RTGCPHYS GCPhysHead, bool fList)
{
    RT_NOREF(pDevIns, GCPhysHead, fList);
    AssertFailed();
}

#ifdef LOG_ENABLED
static const char *ehciPID2Str(uint32_t PID)
{
    switch(PID)
    {
        case EHCI_QTD_PID_OUT:
            return "OUT";
        case EHCI_QTD_PID_IN:
            return "IN";
        case EHCI_QTD_PID_SETUP:
            return "SETUP";
        default:
            return "Invalid PID!";
    }
}
#endif

DECLINLINE(void) ehciR3DumpSingleQTD(RTGCPHYS GCPhys, PEHCI_QTD pQtd, const char *pszPrefix)
{
    if (pQtd->Token.Bits.Active)
    {
        Log2(("  QTD%s: %RGp={", pszPrefix, GCPhys));
        Log2((" Length=%x IOC=%d DT=%d CErr=%d C_Page=%d Status=%x PID=%s}\n", pQtd->Token.Bits.Length, pQtd->Token.Bits.IOC, pQtd->Token.Bits.DataToggle, pQtd->Token.Bits.ErrorCount, pQtd->Token.Bits.CurrentPage, pQtd->Token.u32 & 0xff, ehciPID2Str(pQtd->Token.Bits.PID)));
        Log2(("  QTD: %RGp={", GCPhys));
        Log2((" Buf0=%x Offset=%x Buf1=%x Buf2=%x Buf3=%x Buf4=%x}\n", pQtd->Buffer.Buffer[0].Pointer, pQtd->Buffer.Offset.Offset, pQtd->Buffer.Buffer[1].Pointer, pQtd->Buffer.Buffer[2].Pointer, pQtd->Buffer.Buffer[3].Pointer, pQtd->Buffer.Buffer[4].Pointer));
        Log2(("  QTD: %RGp={", GCPhys));
        Log2((" Next=%RGp T=%d AltNext=%RGp AltT=%d\n", (RTGCPHYS)pQtd->Next.Pointer << EHCI_TD_PTR_SHIFT, pQtd->Next.Terminate, (RTGCPHYS)pQtd->AltNext.Pointer << EHCI_TD_PTR_SHIFT, pQtd->AltNext.Terminate));
    }
    else
        Log2(("  QTD%s: %RGp={Not Active}\n", pszPrefix, GCPhys));
}

/**
 * Dumps a QTD list. LOG_ENABLED builds only.
 */
DECLINLINE(void) ehciR3DumpQTD(PPDMDEVINS pDevIns, RTGCPHYS GCPhysHead, bool fList)
{
    RTGCPHYS GCPhys = GCPhysHead;
    unsigned iterations = 0;

    for (;;)
    {
        EHCI_QTD qtd;

        /* Read the whole QHD */
        ehciR3ReadQTD(pDevIns, GCPhys, &qtd);
        ehciR3DumpSingleQTD(GCPhys, &qtd, "");

        if (!fList || qtd.Next.Terminate || !qtd.Next.Pointer || qtd.Token.Bits.Halted || !qtd.Token.Bits.Active)
            break;

        /* next */
        if (GCPhys == ((RTGCPHYS)qtd.Next.Pointer << EHCI_TD_PTR_SHIFT))
            break; /* detect if list item is self-cycled. */

        GCPhys = (RTGCPHYS)qtd.Next.Pointer << EHCI_TD_PTR_SHIFT;

        if (GCPhys == GCPhysHead)
            break;

        /* If we ran too many iterations, the list must be looping in on itself.
         * On a real controller loops wouldn't be fatal, as it will eventually
         * run out of time in the micro-frame.
         */
        if (++iterations == 128)
        {
            LogFunc(("Too many iterations, exiting!\n"));
            break;
        }
    }

    /* alternative pointers */
    GCPhys = GCPhysHead;
    iterations = 0;

    for (;;)
    {
        EHCI_QTD qtd;

        /* Read the whole QHD */
        ehciR3ReadQTD(pDevIns, GCPhys, &qtd);
        if (GCPhys != GCPhysHead)
            ehciR3DumpSingleQTD(GCPhys, &qtd, "-A");

        if (!fList || qtd.AltNext.Terminate || !qtd.AltNext.Pointer || qtd.Token.Bits.Halted || !qtd.Token.Bits.Active)
            break;

        /* next */
        if (GCPhys == ((RTGCPHYS)qtd.AltNext.Pointer << EHCI_TD_PTR_SHIFT))
            break; /* detect if list item is self-cycled. */

        GCPhys = (RTGCPHYS)qtd.AltNext.Pointer << EHCI_TD_PTR_SHIFT;

        if (GCPhys == GCPhysHead)
            break;

        /* If we ran too many iterations, the list must be looping in on itself.
         * On a real controller loops wouldn't be fatal, as it will eventually
         * run out of time in the micro-frame.
         */
        if (++iterations == 128)
        {
            LogFunc(("Too many iterations, exiting!\n"));
            break;
        }
    }
}

/**
 * Dumps a QHD list. LOG_ENABLED builds only.
 */
DECLINLINE(void) ehciR3DumpQH(PPDMDEVINS pDevIns, RTGCPHYS GCPhysHead, bool fList)
{
    EHCI_QHD qhd;
    RTGCPHYS GCPhys = GCPhysHead;
    unsigned iterations = 0;

    Log2((" QH: %RGp={", GCPhys));

    /* Read the whole QHD */
    ehciR3ReadQHD(pDevIns, GCPhys, &qhd);

    Log2(("HorzLnk=%RGp Typ=%u T=%u Addr=%x EndPt=%x Speed=%x MaxSize=%x NAK=%d C=%d RH=%d I=%d}\n",
          ((RTGCPHYS)qhd.Next.Pointer << EHCI_TD_PTR_SHIFT), qhd.Next.Type, qhd.Next.Terminate,
          qhd.Characteristics.DeviceAddress, qhd.Characteristics.EndPt, qhd.Characteristics.EndPtSpeed,
          qhd.Characteristics.MaxLength, qhd.Characteristics.NakCountReload, qhd.Characteristics.ControlEPFlag,
          qhd.Characteristics.HeadReclamation, qhd.Characteristics.InActiveNext));
    Log2(("  Caps: Port=%x Hub=%x Multi=%x CMask=%x SMask=%x\n", qhd.Caps.Port, qhd.Caps.HubAddress,
          qhd.Caps.Mult, qhd.Caps.CMask, qhd.Caps.SMask));
    Log2(("  CurrPtr=%RGp Next=%RGp T=%d AltNext=%RGp T=%d\n",
          ((RTGCPHYS)qhd.CurrQTD.Pointer << EHCI_TD_PTR_SHIFT),
          ((RTGCPHYS)qhd.Overlay.OrgQTD.Next.Pointer << EHCI_TD_PTR_SHIFT), qhd.Overlay.OrgQTD.Next.Terminate,
          ((RTGCPHYS)qhd.Overlay.OrgQTD.AltNext.Pointer << EHCI_TD_PTR_SHIFT), qhd.Overlay.OrgQTD.AltNext.Terminate));
    ehciR3DumpSingleQTD((RTGCPHYS)qhd.CurrQTD.Pointer << EHCI_TD_PTR_SHIFT, &qhd.Overlay.OrgQTD, "");
    ehciR3DumpQTD(pDevIns, (RTGCPHYS)qhd.Overlay.OrgQTD.Next.Pointer << EHCI_TD_PTR_SHIFT, true);

    Assert(qhd.Next.Pointer || qhd.Next.Terminate);
    if (    !fList
        ||   qhd.Next.Terminate
        ||  !qhd.Next.Pointer)
        return;

    for (;;)
    {
        /* Read the next pointer */
        EHCI_TD_PTR ptr;
        ehciR3ReadTDPtr(pDevIns, GCPhys, &ptr);

        AssertMsg(ptr.Type == EHCI_DESCRIPTOR_QH, ("Unexpected pointer to type %d\n", ptr.Type));
        Assert(ptr.Pointer || ptr.Terminate);
        if (    ptr.Terminate
            || !ptr.Pointer
            ||  ptr.Type != EHCI_DESCRIPTOR_QH)
            break;

        /* next */
        if (GCPhys == ((RTGCPHYS)ptr.Pointer << EHCI_TD_PTR_SHIFT))
            break;  /* Looping on itself. Bad guest! */

        GCPhys = (RTGCPHYS)ptr.Pointer << EHCI_TD_PTR_SHIFT;
        if (GCPhys == GCPhysHead)
            break;  /* break the loop */

        ehciR3DumpQH(pDevIns, GCPhys, false);

        /* And again, if we ran too many iterations, the list must be looping on itself.
         * Just quit.
         */
        if (++iterations == 64)
        {
            LogFunc(("Too many iterations, exiting!\n"));
            break;
        }
    }
}

/**
 * Dumps an ITD list. LOG_ENABLED builds only.
 */
DECLINLINE(void) ehciR3DumpITD(PPDMDEVINS pDevIns, RTGCPHYS GCPhysHead, bool fList)
{
    RTGCPHYS GCPhys = GCPhysHead;
    unsigned iterations = 0;

    for (;;)
    {
        Log2((" ITD: %RGp={", GCPhys));

        /* Read the whole ITD */
        EHCI_ITD_PAD    PaddedItd;
        PEHCI_ITD_PAD   pItd = &PaddedItd;
        ehciR3ReadItd(pDevIns, GCPhys, &PaddedItd);

        Log2(("Addr=%x EndPt=%x Dir=%s MaxSize=%x Mult=%d}\n", pItd->Buffer.Misc.DeviceAddress, pItd->Buffer.Misc.EndPt, (pItd->Buffer.Misc.DirectionIn) ? "in" : "out", pItd->Buffer.Misc.MaxPacket, pItd->Buffer.Misc.Multi));
        for (unsigned i=0;i<RT_ELEMENTS(pItd->Transaction);i++)
        {
            if (pItd->Transaction[i].Active)
            {
                Log2(("T%d Len=%x Offset=%x PG=%d IOC=%d Buffer=%x\n", i, pItd->Transaction[i].Length, pItd->Transaction[i].Offset, pItd->Transaction[i].PG, pItd->Transaction[i].IOC,
                       (RTGCPHYS)pItd->Buffer.Buffer[pItd->Transaction[i].PG].Pointer << EHCI_BUFFER_PTR_SHIFT));
            }
        }
        Assert(pItd->Next.Pointer || pItd->Next.Terminate);
        if (!fList || pItd->Next.Terminate || !pItd->Next.Pointer)
            break;

        /* And again, if we ran too many iterations, the list must be looping on itself.
         * Just quit.
         */
        if (++iterations == 128)
        {
            LogFunc(("Too many iterations, exiting!\n"));
            break;
        }

        /* next */
        GCPhys = (RTGCPHYS)pItd->Next.Pointer << EHCI_TD_PTR_SHIFT;
    }
}

/**
 * Dumps a periodic list. LOG_ENABLED builds only.
 */
DECLINLINE(void) ehciR3DumpPeriodicList(PPDMDEVINS pDevIns, RTGCPHYS GCPhysHead, const char *pszMsg, bool fTDs)
{
    RT_NOREF(fTDs);
    RTGCPHYS GCPhys = GCPhysHead;
    unsigned iterations = 0;

    if (pszMsg)
        Log2(("%s:", pszMsg));

    for (;;)
    {
        EHCI_FRAME_LIST_PTR FramePtr;

        /* ED */
        Log2((" %RGp={", GCPhys));
        if (!GCPhys)
        {
            Log2(("END}\n"));
            return;
        }

        /* Frame list pointer */
        ehciR3ReadFrameListPtr(pDevIns, GCPhys, &FramePtr);
        if (FramePtr.Terminate)
        {
            Log2(("[Terminate]}\n"));
        }
        else
        {
            RTGCPHYS GCPhys1 = (RTGCPHYS)FramePtr.FrameAddr << EHCI_FRAME_LIST_NEXTPTR_SHIFT;
            switch (FramePtr.Type)
            {
                case EHCI_DESCRIPTOR_ITD:
                    Log2(("[ITD]}\n"));
                    ehciR3DumpITD(pDevIns, GCPhys1, false);
                    break;
                case EHCI_DESCRIPTOR_SITD:
                    Log2(("[SITD]}\n"));
                    ehciR3DumpSITD(pDevIns, GCPhys1, false);
                    break;
                case EHCI_DESCRIPTOR_QH:
                    Log2(("[QH]}\n"));
                    ehciR3DumpQH(pDevIns, GCPhys1, false);
                    break;
                case EHCI_DESCRIPTOR_FSTN:
                    Log2(("[FSTN]}\n"));
                    ehciR3DumpFSTN(pDevIns, GCPhys1, false);
                    break;
            }
        }

        /* Same old. If we ran too many iterations, the list must be looping on itself.
         * Just quit.
         */
        if (++iterations == 128)
        {
            LogFunc(("Too many iterations, exiting!\n"));
            break;
        }

        /* next */
        GCPhys = GCPhys + sizeof(FramePtr);
    }
}

#endif /* LOG_ENABLED */


DECLINLINE(int) ehciR3InFlightFindFree(PEHCICC pThisCC, const int iStart)
{
    unsigned i = iStart;
    while (i < RT_ELEMENTS(pThisCC->aInFlight)) {
        if (pThisCC->aInFlight[i].pUrb == NULL)
            return i;
        i++;
    }
    i = iStart;
    while (i-- > 0) {
        if (pThisCC->aInFlight[i].pUrb == NULL)
            return i;
    }
    return -1;
}


/**
 * Record an in-flight TD.
 *
 * @param   pThis       EHCI instance data, shared edition.
 * @param   pThisCC     EHCI instance data, ring-3 edition.
 * @param   GCPhysTD    Physical address of the TD.
 * @param   pUrb        The URB.
 */
static void ehciR3InFlightAdd(PEHCI pThis, PEHCICC pThisCC, RTGCPHYS GCPhysTD, PVUSBURB pUrb)
{
    int i = ehciR3InFlightFindFree(pThisCC, (GCPhysTD >> 4) % RT_ELEMENTS(pThisCC->aInFlight));
    if (i >= 0)
    {
#ifdef LOG_ENABLED
        pUrb->pHci->u32FrameNo = pThis->HcFmNumber;
#endif
        pThisCC->aInFlight[i].GCPhysTD = GCPhysTD;
        pThisCC->aInFlight[i].pUrb = pUrb;
        pThisCC->cInFlight++;
        return;
    }
    AssertMsgFailed(("Out of space cInFlight=%d!\n", pThisCC->cInFlight));
    RT_NOREF(pThis);
}


/**
 * Record in-flight TDs for an URB.
 *
 * @param   pThis       EHCI instance data, shared edition.
 * @param   pThisCC     EHCI instance data, ring-3 edition.
 * @param   pUrb        The URB.
 */
static void ehciR3InFlightAddUrb(PEHCI pThis, PEHCICC pThisCC, PVUSBURB pUrb)
{
    for (unsigned iTd = 0; iTd < pUrb->pHci->cTds; iTd++)
        ehciR3InFlightAdd(pThis, pThisCC, pUrb->paTds[iTd].TdAddr, pUrb);
}


/**
 * Finds a in-flight TD.
 *
 * @returns Index of the record.
 * @returns -1 if not found.
 * @param   pThisCC     EHCI instance data, ring-3 edition.
 * @param   GCPhysTD    Physical address of the TD.
 * @remark  This has to be fast.
 */
static int ehciR3InFlightFind(PEHCICC pThisCC, RTGCPHYS GCPhysTD)
{
    unsigned cLeft = pThisCC->cInFlight;
    unsigned i = (GCPhysTD >> 4) % RT_ELEMENTS(pThisCC->aInFlight);
    const int iLast = i;
    while (i < RT_ELEMENTS(pThisCC->aInFlight)) {
        if (pThisCC->aInFlight[i].GCPhysTD == GCPhysTD && pThisCC->aInFlight[i].pUrb)
            return i;
        if (pThisCC->aInFlight[i].pUrb)
            if (cLeft-- <= 1)
                return -1;
        i++;
    }
    i = iLast;
    while (i-- > 0) {
        if (pThisCC->aInFlight[i].GCPhysTD == GCPhysTD && pThisCC->aInFlight[i].pUrb)
            return i;
        if (pThisCC->aInFlight[i].pUrb)
            if (cLeft-- <= 1)
                return -1;
    }
    return -1;
}


/**
 * Checks if a TD is in-flight.
 *
 * @returns true if in flight, false if not.
 * @param   pThisCC     EHCI instance data, ring-3 edition.
 * @param   GCPhysTD    Physical address of the TD.
 */
static bool ehciR3IsTdInFlight(PEHCICC pThisCC, RTGCPHYS GCPhysTD)
{
    return ehciR3InFlightFind(pThisCC, GCPhysTD) >= 0;
}


/**
 * Removes a in-flight TD.
 *
 * @returns 0 if found. For logged builds this is the number of frames the TD has been in-flight.
 * @returns -1 if not found.
 * @param   pThis       EHCI instance data, shared edition.
 * @param   pThisCC     EHCI instance data, ring-3 edition.
 * @param   GCPhysTD    Physical address of the TD.
 */
static int ehciR3InFlightRemove(PEHCI pThis, PEHCICC pThisCC, RTGCPHYS GCPhysTD)
{
    int i = ehciR3InFlightFind(pThisCC, GCPhysTD);
    if (i >= 0)
    {
#ifdef LOG_ENABLED
        const int cFramesInFlight = pThis->HcFmNumber - pThisCC->aInFlight[i].pUrb->pHci->u32FrameNo;
#else
        const int cFramesInFlight = 0;
#endif
        Log2Func(("reaping TD=%RGp %d frames (%#010x-%#010x)\n",
              GCPhysTD, cFramesInFlight, pThisCC->aInFlight[i].pUrb->pHci->u32FrameNo, pThis->HcFmNumber));
        pThisCC->aInFlight[i].GCPhysTD = 0;
        pThisCC->aInFlight[i].pUrb = NULL;
        pThisCC->cInFlight--;
        return cFramesInFlight;
    }
    AssertMsgFailed(("TD %RGp is not in flight\n", GCPhysTD));
    RT_NOREF(pThis);
    return -1;
}


/**
 * Removes all TDs associated with a URB from the in-flight tracking.
 *
 * @returns 0 if found. For logged builds this is the number of frames the TD has been in-flight.
 * @returns -1 if not found.
 * @param   pThis       EHCI instance data, shared edition.
 * @param   pThisCC     EHCI instance data, ring-3 edition.
 * @param   pUrb        The URB.
 */
static int ehciR3InFlightRemoveUrb(PEHCI pThis, PEHCICC pThisCC, PVUSBURB pUrb)
{
    int cFramesInFlight = ehciR3InFlightRemove(pThis, pThisCC, pUrb->paTds[0].TdAddr);
    if (pUrb->pHci->cTds > 1)
    {
        for (unsigned iTd = 1; iTd < pUrb->pHci->cTds; iTd++)
            if (ehciR3InFlightRemove(pThis, pThisCC, pUrb->paTds[iTd].TdAddr) < 0)
                cFramesInFlight = -1;
    }
    return cFramesInFlight;
}


/**
 * Checks that the transport descriptors associated with the URB
 * hasn't been changed in any way indicating that they may have been canceled.
 *
 * This rountine also updates the TD copies contained within the URB.
 *
 * @returns true if the URB has been canceled, otherwise false.
 * @param   pThisCC     EHCI instance data, ring-3 edition. (For stats.)
 * @param   pUrb        The URB in question.
 * @param   pItd        The ITD pointer.
 */
static bool ehciR3ItdHasUrbBeenCanceled(PEHCICC pThisCC, PVUSBURB pUrb, PEHCI_ITD_PAD pItd)
{
    RT_NOREF(pThisCC);
    Assert(pItd);
    if (!pUrb)
        return true;

    PEHCI_ITD pItdCopy = (PEHCI_ITD)pUrb->paTds[0].TdCopy;

    /* Check transactions */
    for (unsigned i = 0; i < RT_ELEMENTS(pItd->Transaction); i++)
    {
        if (    pItd->Transaction[i].Length != pItdCopy->Transaction[i].Length
            ||  pItd->Transaction[i].Offset != pItdCopy->Transaction[i].Offset
            ||  pItd->Transaction[i].PG     != pItdCopy->Transaction[i].PG
            ||  pItd->Transaction[i].Active != pItdCopy->Transaction[i].Active)
        {
            Log(("%s: ehciR3ItdHasUrbBeenCanceled: TdAddr=%RGp canceled! [iso]\n",
                 pUrb->pszDesc, pUrb->paTds[0].TdAddr));
            Log2(("   %.*Rhxs (cur)\n"
                  "!= %.*Rhxs (copy)\n",
                  sizeof(*pItd), pItd, sizeof(*pItd), &pUrb->paTds[0].TdCopy[0]));
            STAM_COUNTER_INC(&pThisCC->StatCanceledIsocUrbs);
            return true;
        }
    }

    /* Check misc characteristics */
    if (    pItd->Buffer.Misc.DeviceAddress != pItdCopy->Buffer.Misc.DeviceAddress
        ||  pItd->Buffer.Misc.DirectionIn   != pItdCopy->Buffer.Misc.DirectionIn
        ||  pItd->Buffer.Misc.EndPt         != pItdCopy->Buffer.Misc.EndPt)
    {
        Log(("%s: ehciR3ItdHasUrbBeenCanceled (misc): TdAddr=%RGp canceled! [iso]\n",
             pUrb->pszDesc, pUrb->paTds[0].TdAddr));
        Log2(("   %.*Rhxs (cur)\n"
              "!= %.*Rhxs (copy)\n",
              sizeof(*pItd), pItd, sizeof(*pItd), &pUrb->paTds[0].TdCopy[0]));
        STAM_COUNTER_INC(&pThisCC->StatCanceledIsocUrbs);
        return true;
    }

    /* Check buffer pointers */
    for (unsigned i = 0; i < RT_ELEMENTS(pItd->Buffer.Buffer); i++)
    {
        if (pItd->Buffer.Buffer[i].Pointer  != pItdCopy->Buffer.Buffer[i].Pointer)
        {
            Log(("%s: ehciR3ItdHasUrbBeenCanceled (buf): TdAddr=%RGp canceled! [iso]\n",
                 pUrb->pszDesc, pUrb->paTds[0].TdAddr));
            Log2(("   %.*Rhxs (cur)\n"
                  "!= %.*Rhxs (copy)\n",
                  sizeof(*pItd), pItd, sizeof(*pItd), &pUrb->paTds[0].TdCopy[0]));
            STAM_COUNTER_INC(&pThisCC->StatCanceledIsocUrbs);
            return true;
        }
    }
    return false;
}

/**
 * Checks that the transport descriptors associated with the URB
 * hasn't been changed in any way indicating that they may have been canceled.
 *
 * This rountine also updates the TD copies contained within the URB.
 *
 * @returns true if the URB has been canceled, otherwise false.
 * @param   pThisCC     EHCI instance data, ring-3 edition. (For stats.)
 * @param   pUrb        The URB in question.
 * @param   pQhd        The QHD pointer
 * @param   pQtd        The QTD pointer
 */
static bool ehciR3QhdHasUrbBeenCanceled(PEHCICC pThisCC, PVUSBURB pUrb, PEHCI_QHD pQhd, PEHCI_QTD pQtd)
{
    RT_NOREF(pQhd, pThisCC);
    Assert(pQhd && pQtd);
    if (   !pUrb
        || !ehciR3IsTdInFlight(pThisCC, pUrb->paTds[0].TdAddr))
        return true;

    PEHCI_QTD pQtdCopy = (PEHCI_QTD)pUrb->paTds[0].TdCopy;

    if (   pQtd->Token.Bits.Length      != pQtdCopy->Token.Bits.Length
        || pQtd->Token.Bits.Active      != pQtdCopy->Token.Bits.Active
        || pQtd->Token.Bits.DataToggle  != pQtdCopy->Token.Bits.DataToggle
        || pQtd->Token.Bits.CurrentPage != pQtdCopy->Token.Bits.CurrentPage
        || pQtd->Token.Bits.PID         != pQtdCopy->Token.Bits.PID
        || pQtd->Buffer.Offset.Offset   != pQtdCopy->Buffer.Offset.Offset)
    {
        Log(("%s: ehciQtdHasUrbBeenCanceled: TdAddr=%RGp canceled! [iso]\n",
             pUrb->pszDesc, pUrb->paTds[0].TdAddr));
        Log2(("   %.*Rhxs (cur)\n"
              "!= %.*Rhxs (copy)\n",
              sizeof(*pQtd), pQtd, sizeof(*pQtd), &pUrb->paTds[0].TdCopy[0]));
        STAM_COUNTER_INC(&pThisCC->StatCanceledGenUrbs);
        return true;
    }


    /* Check buffer pointers */
    for (unsigned i = 0; i < RT_ELEMENTS(pQtd->Buffer.Buffer); i++)
    {
        if (pQtd->Buffer.Buffer[i].Pointer  != pQtdCopy->Buffer.Buffer[i].Pointer)
        {
            Log(("%s: ehciQtdHasUrbBeenCanceled (buf): TdAddr=%RGp canceled! [iso]\n",
                 pUrb->pszDesc, pUrb->paTds[0].TdAddr));
            Log2(("   %.*Rhxs (cur)\n"
                  "!= %.*Rhxs (copy)\n",
                  sizeof(*pQtd), pQtd, sizeof(*pQtd), &pUrb->paTds[0].TdCopy[0]));
            STAM_COUNTER_INC(&pThisCC->StatCanceledGenUrbs);
            return true;
        }
    }

    return false;
}

/**
 * Set the ITD status bits acorresponding to the VUSB status code.
 *
 * @param   enmStatus   The VUSB status code.
 * @param   pItdStatus  ITD status pointer
 */
static void ehciR3VUsbStatus2ItdStatus(VUSBSTATUS enmStatus, EHCI_ITD_TRANSACTION *pItdStatus)
{
    switch (enmStatus)
    {
        case VUSBSTATUS_OK:
            pItdStatus->TransactError = 0;
            pItdStatus->DataBufError  = 0;
            break;  /* make sure error bits are cleared */
        case VUSBSTATUS_STALL:
        case VUSBSTATUS_DNR:
        case VUSBSTATUS_CRC:
            pItdStatus->TransactError = 1;
            break;
        case VUSBSTATUS_DATA_UNDERRUN:
        case VUSBSTATUS_DATA_OVERRUN:
            pItdStatus->DataBufError = 1;
            break;
        case VUSBSTATUS_NOT_ACCESSED:
            Log(("pUrb->enmStatus=VUSBSTATUS_NOT_ACCESSED!!!\n"));
            break; /* can't signal this other than setting the length to 0 */
        default:
            Log(("pUrb->enmStatus=%#x!!!\n", enmStatus));
            break;;
    }
}

/**
 * Set the QTD status bits acorresponding to the VUSB status code.
 *
 * @param   enmStatus   The VUSB status code.
 * @param   pQtdStatus  QTD status pointer
 */
static void ehciR3VUsbStatus2QtdStatus(VUSBSTATUS enmStatus, EHCI_QTD_TOKEN *pQtdStatus)
{
    /** @todo CERR */
    switch (enmStatus)
    {
        case VUSBSTATUS_OK:
            break;  /* nothing to do */
        case VUSBSTATUS_STALL:
            pQtdStatus->Halted = 1;
            pQtdStatus->Active = 0;
            break;  /* not an error! */
        case VUSBSTATUS_DNR:
        case VUSBSTATUS_CRC:
            pQtdStatus->TransactError = 1;
            break;
        case VUSBSTATUS_DATA_UNDERRUN:
        case VUSBSTATUS_DATA_OVERRUN:
            pQtdStatus->DataBufError = 1;
            break;
        case VUSBSTATUS_NOT_ACCESSED:
            Log(("pUrb->enmStatus=VUSBSTATUS_NOT_ACCESSED!!!\n"));
            break; /* can't signal this */
        default:
            Log(("pUrb->enmStatus=%#x!!!\n", enmStatus));
            break;;
    }
}


/**
 * Heuristic to determine the transfer type
 *
 * @returns transfer type
 * @param   pQhd        Queue head pointer
 */
static VUSBXFERTYPE ehciR3QueryTransferType(PEHCI_QHD pQhd)
{
    /* If it's EP0, we know what it is. */
    if (!pQhd->Characteristics.EndPt)
        return VUSBXFERTYPE_CTRL;

    /* Non-zero SMask implies interrupt transfer. */
    if (pQhd->Caps.SMask)
        return VUSBXFERTYPE_INTR;

    /* For non-HS EPs, control endpoints are clearly marked. */
    if (    pQhd->Characteristics.ControlEPFlag
        &&  pQhd->Characteristics.EndPtSpeed != EHCI_QHD_EPT_SPEED_HIGH)
        return VUSBXFERTYPE_CTRL;

    /* If we still don't know, it's guesswork from now on. */

    /* 64 likely indicates an interrupt transfer (see @bugref{8314})*/
    if (pQhd->Characteristics.MaxLength == 64)
        return VUSBXFERTYPE_INTR;

    /* At this point we hope it's a bulk transfer with max packet size of 512. */
    Assert(pQhd->Characteristics.MaxLength == 512);
    return VUSBXFERTYPE_BULK;
}

/**
 * Worker for ehciR3RhXferCompletion that handles the completion of
 * a URB made up of isochronous TDs.
 *
 * In general, all URBs should have status OK.
 */
static void ehciR3RhXferCompleteITD(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC, PVUSBURB pUrb)
{
    /* Read the whole ITD */
    EHCI_ITD_PAD  PaddedItd;
    PEHCI_ITD_PAD pItd = &PaddedItd;
    ehciR3ReadItd(pDevIns, pUrb->paTds[0].TdAddr, &PaddedItd);

    /*
     * Check that the URB hasn't been canceled and then try unlink the TDs.
     *
     * We drop the URB if the ED is marked halted/skip ASSUMING that this
     * means the HCD has canceled the URB.
     *
     * If we succeed here (i.e. not dropping the URB), the TdCopy members will
     * be updated but not yet written. We will delay the writing till we're done
     * with the data copying, buffer pointer advancing and error handling.
     */
    bool fHasBeenCanceled = false;
    int  cFmAge = ehciR3InFlightRemoveUrb(pThis, pThisCC, pUrb);
    if (    cFmAge < 0
        ||  (fHasBeenCanceled = ehciR3ItdHasUrbBeenCanceled(pThisCC, pUrb, pItd))
       )
    {
        Log(("%s: ehciR3RhXferCompleteITD: DROPPED {ITD=%RGp cTds=%d TD0=%RGp age %d} because:%s%s!!!\n",
             pUrb->pszDesc, pUrb->pHci->EdAddr, pUrb->pHci->cTds, pUrb->paTds[0].TdAddr, cFmAge,
             cFmAge < 0                                             ? " td not-in-flight" : "",
             fHasBeenCanceled                                       ? " td canceled" : ""));
        NOREF(fHasBeenCanceled);
        STAM_COUNTER_INC(&pThisCC->StatDroppedUrbs);
        return;
    }

    bool fIOC = false, fError = false;

    /*
     * Copy the data back (if IN operation) and update the TDs.
     */
    if (pUrb->enmStatus == VUSBSTATUS_OK)
    {
        for (unsigned i = 0; i < pUrb->cIsocPkts; i++)
        {
            ehciR3VUsbStatus2ItdStatus(pUrb->aIsocPkts[i].enmStatus, &pItd->Transaction[i]);
            if (pItd->Transaction[i].IOC)
                fIOC = true;

            if (    pUrb->enmDir == VUSBDIRECTION_IN
                &&  (   pUrb->aIsocPkts[i].enmStatus == VUSBSTATUS_OK
                     || pUrb->aIsocPkts[i].enmStatus == VUSBSTATUS_DATA_UNDERRUN
                     || pUrb->aIsocPkts[i].enmStatus == VUSBSTATUS_DATA_OVERRUN))
            {
                Assert(pItd->Transaction[i].Active);

                if (pItd->Transaction[i].Active)
                {
                    const unsigned  pg = pItd->Transaction[i].PG;
                    const unsigned  cb = pUrb->aIsocPkts[i].cb;
                    pItd->Transaction[i].Length = cb;    /* Set the actual size. */
                    /* Copy data. */
                    if (cb)
                    {
                        uint8_t *pb = &pUrb->abData[pUrb->aIsocPkts[i].off];

                        RTGCPHYS GCPhysBuf = (RTGCPHYS)pItd->Buffer.Buffer[pg].Pointer << EHCI_BUFFER_PTR_SHIFT;
                        GCPhysBuf += pItd->Transaction[i].Offset;

                        /* If the transfer would cross page boundary, use the next sequential PG pointer
                         * for the second part (section 4.7.1).
                         */
                        if (pItd->Transaction[i].Offset + pItd->Transaction[i].Length > GUEST_PAGE_SIZE)
                        {
                            unsigned    cb1 = GUEST_PAGE_SIZE - pItd->Transaction[i].Offset;
                            unsigned    cb2 = cb - cb1;

                            ehciPhysWrite(pDevIns, GCPhysBuf, pb, cb1);
                            if ((pg + 1) >= EHCI_NUM_ITD_PAGES)
                               LogRelMax(10, ("EHCI: Crossing to nonstandard page %d in iTD at %RGp on completion.\n", pg + 1, pUrb->paTds[0].TdAddr));

                            GCPhysBuf = (RTGCPHYS)pItd->Buffer.Buffer[pg + 1].Pointer << EHCI_BUFFER_PTR_SHIFT;
                            ehciPhysWrite(pDevIns, GCPhysBuf, pb + cb1, cb2);
                        }
                        else
                            ehciPhysWrite(pDevIns, GCPhysBuf, pb, cb);

                        Log5(("packet %d: off=%#x cb=%#x pb=%p (%#x)\n"
                              "%.*Rhxd\n",
                              i, pUrb->aIsocPkts[i].off, cb, pb, pb - &pUrb->abData[0], cb, pb));
                    }
                }
            }
            pItd->Transaction[i].Active = 0; /* transfer is now officially finished */
        } /* for */
    }
    else
    {
        LogFunc(("Taking untested code path at line %d...\n", __LINE__));
        /*
         * Most status codes only apply to the individual packets.
         *
         * If we get a URB level error code of this kind, we'll distribute
         * it to all the packages unless some other status is available for
         * a package. This is a bit fuzzy, and we will get rid of this code
         * before long!
         */
        for (unsigned i = 0; i < pUrb->cIsocPkts; i++)
        {
            if (pItd->Transaction[i].Active)
            {
                ehciR3VUsbStatus2ItdStatus(pUrb->aIsocPkts[i].enmStatus, &pItd->Transaction[i]);
                if (pItd->Transaction[i].IOC)
                    fIOC = true;

                pItd->Transaction[i].Active = 0;   /* transfer is now officially finished */
            }
        }
        fError = true;
    }

    /*
     * Write back the modified TD.
     */

    Log(("%s: ehciR3RhXferCompleteITD: pUrb->paTds[0].TdAddr=%RGp EdAddr=%RGp "
         "psw0=%x:%x psw1=%x:%x psw2=%x:%x psw3=%x:%x psw4=%x:%x psw5=%x:%x psw6=%x:%x psw7=%x:%x\n",
         pUrb->pszDesc, pUrb->paTds[0].TdAddr,
         pUrb->pHci->EdAddr,
         pItd->Buffer.Buffer[pItd->Transaction[0].PG].Pointer << EHCI_BUFFER_PTR_SHIFT, pItd->Transaction[0].Length,
         pItd->Buffer.Buffer[pItd->Transaction[1].PG].Pointer << EHCI_BUFFER_PTR_SHIFT, pItd->Transaction[1].Length,
         pItd->Buffer.Buffer[pItd->Transaction[2].PG].Pointer << EHCI_BUFFER_PTR_SHIFT, pItd->Transaction[2].Length,
         pItd->Buffer.Buffer[pItd->Transaction[3].PG].Pointer << EHCI_BUFFER_PTR_SHIFT, pItd->Transaction[3].Length,
         pItd->Buffer.Buffer[pItd->Transaction[4].PG].Pointer << EHCI_BUFFER_PTR_SHIFT, pItd->Transaction[4].Length,
         pItd->Buffer.Buffer[pItd->Transaction[5].PG].Pointer << EHCI_BUFFER_PTR_SHIFT, pItd->Transaction[5].Length,
         pItd->Buffer.Buffer[pItd->Transaction[6].PG].Pointer << EHCI_BUFFER_PTR_SHIFT, pItd->Transaction[6].Length,
         pItd->Buffer.Buffer[pItd->Transaction[7].PG].Pointer << EHCI_BUFFER_PTR_SHIFT, pItd->Transaction[7].Length
         ));
    ehciR3WriteItd(pDevIns, pUrb->paTds[0].TdAddr, pItd);

    /*
     * Signal an interrupt on the next interrupt threshold when IOC was set for any transaction.
     * Both error and completion interrupts may be signaled at the same time (see Table 2.10).
     */
    if (fError)
        ehciR3SetInterrupt(pDevIns, pThis, EHCI_STATUS_ERROR_INT);
    if (fIOC)
        ehciR3SetInterrupt(pDevIns, pThis, EHCI_STATUS_THRESHOLD_INT);
}


/**
 * Worker for ehciR3RhXferCompletion that handles the completion of
 * a URB made up of queue heads/descriptors
 */
static void ehciR3RhXferCompleteQH(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC, PVUSBURB pUrb)
{
    EHCI_QHD qhd;
    EHCI_QTD qtd;

    /* Read the whole QHD & QTD */
    ehciR3ReadQHD(pDevIns, pUrb->pHci->EdAddr, &qhd);
    AssertMsg(pUrb->paTds[0].TdAddr == ((RTGCPHYS)qhd.CurrQTD.Pointer << EHCI_TD_PTR_SHIFT),
              ("Out of order completion %RGp != %RGp Endpoint=%#x\n", pUrb->paTds[0].TdAddr,
               ((RTGCPHYS)qhd.CurrQTD.Pointer << EHCI_TD_PTR_SHIFT), pUrb->EndPt));
    ehciR3ReadQTD(pDevIns, (RTGCPHYS)qhd.CurrQTD.Pointer << EHCI_TD_PTR_SHIFT, &qtd);

    /*
     * Check that the URB hasn't been canceled and then try unlink the TDs.
     *
     * We drop the URB if the ED is marked halted/skip ASSUMING that this
     * means the HCD has canceled the URB.
     *
     * If we succeed here (i.e. not dropping the URB), the TdCopy members will
     * be updated but not yet written. We will delay the writing till we're done
     * with the data copying, buffer pointer advancing and error handling.
     */
    bool fHasBeenCanceled = false;
    if ((fHasBeenCanceled = ehciR3QhdHasUrbBeenCanceled(pThisCC, pUrb, &qhd, &qtd)))
    {
        Log(("%s: ehciRhXferCompletionQH: DROPPED {qTD=%RGp cTds=%d TD0=%RGp} because:%s%s!!!\n",
             pUrb->pszDesc, pUrb->pHci->EdAddr, pUrb->pHci->cTds, pUrb->paTds[0].TdAddr,
             (pUrb->paTds[0].TdAddr != ((RTGCPHYS)qhd.CurrQTD.Pointer << EHCI_TD_PTR_SHIFT))   ?  " curptr changed" : "",
             fHasBeenCanceled                                       ? " td canceled" : ""));
        NOREF(fHasBeenCanceled);
        STAM_COUNTER_INC(&pThisCC->StatDroppedUrbs);

        ehciR3InFlightRemoveUrb(pThis, pThisCC, pUrb);
        qtd.Token.Bits.Active = 0;
        ehciR3QHUpdateOverlay(pDevIns, pThis, pThisCC, &qhd, pUrb->pHci->EdAddr, &qtd);
        return;
    }
    ehciR3InFlightRemoveUrb(pThis, pThisCC, pUrb);

    /* Update the status/error bits */
    ehciR3VUsbStatus2QtdStatus(pUrb->enmStatus, &qtd.Token.Bits);

    /*
     * Write back IN buffers.
     */
    if (    pUrb->enmDir == VUSBDIRECTION_IN
        &&  pUrb->cbData
        &&  (   pUrb->enmStatus == VUSBSTATUS_OK
             || pUrb->enmStatus == VUSBSTATUS_DATA_OVERRUN
             || pUrb->enmStatus == VUSBSTATUS_DATA_UNDERRUN
            )
       )
    {
        unsigned curOffset = 0;
        unsigned cbLeft = pUrb->cbData;

        for (unsigned i=qtd.Token.Bits.CurrentPage;i<RT_ELEMENTS(qtd.Buffer.Buffer);i++)
        {
            RTGCPHYS GCPhysBuf;
            unsigned cbCurTransfer;

            GCPhysBuf = (RTGCPHYS)qtd.Buffer.Buffer[i].Pointer << EHCI_BUFFER_PTR_SHIFT;
            if (i == 0)
                GCPhysBuf += qtd.Buffer.Offset.Offset;

            cbCurTransfer = GUEST_PAGE_SIZE - (GCPhysBuf & GUEST_PAGE_OFFSET_MASK);
            cbCurTransfer = RT_MIN(cbCurTransfer, cbLeft);

            Log3Func(("packet data for page %d:\n"
                  "%.*Rhxd\n",
                  i,
                  cbCurTransfer, &pUrb->abData[curOffset]));

            ehciPhysWrite(pDevIns, GCPhysBuf, &pUrb->abData[curOffset], cbCurTransfer);
            curOffset  += cbCurTransfer;
            cbLeft     -= cbCurTransfer;

            if (cbLeft == 0)
                break;
            Assert(cbLeft < qtd.Token.Bits.Length);
        }
    }

    if (    pUrb->cbData
        &&  (   pUrb->enmStatus == VUSBSTATUS_OK
             || pUrb->enmStatus == VUSBSTATUS_DATA_OVERRUN
             || pUrb->enmStatus == VUSBSTATUS_DATA_UNDERRUN
            )
       )
    {
        /* 3.5.3:
         * This field specifies the total number of bytes to be moved
         * with this transfer descriptor. This field is decremented by the number of bytes actually
         * moved during the transaction, only on the successful completion of the transaction
         */
        Assert(qtd.Token.Bits.Length >= pUrb->cbData);
        qtd.Token.Bits.Length -= pUrb->cbData;

        /* Data was moved; toggle data toggle bit */
        qtd.Token.Bits.DataToggle ^= 1;
    }

#ifdef LOG_ENABLED
    ehciR3DumpSingleQTD(pUrb->paTds[0].TdAddr, &qtd, "");
#endif
    qtd.Token.Bits.Active = 0;   /* transfer is now officially finished */

    /*
     * Write back the modified TD.
     */
    Log(("%s: ehciR3RhXferCompleteQH: pUrb->paTds[0].TdAddr=%RGp EdAddr=%RGp\n",
         pUrb->pszDesc, pUrb->paTds[0].TdAddr,
         pUrb->pHci->EdAddr));

    ehciR3WriteQTD(pDevIns, pUrb->paTds[0].TdAddr, &qtd);

    ehciR3QHUpdateOverlay(pDevIns, pThis, pThisCC, &qhd, pUrb->pHci->EdAddr, &qtd);

    /*
     * Signal an interrupt on the next interrupt threshold when IOC was set for any transaction.
     * Both error and completion interrupts may be signaled at the same time (see Table 2.10).
     */
    if (EHCI_QTD_HAS_ERROR(&qtd.Token.Bits))
        ehciR3SetInterrupt(pDevIns, pThis, EHCI_STATUS_ERROR_INT);

    bool fIOC = false;
    if (qtd.Token.Bits.IOC) {
        fIOC = true;
        Log2Func(("Interrupting, IOC set\n"));
    } else if (qtd.Token.Bits.Length && (qtd.Token.Bits.PID == EHCI_QTD_PID_IN)) {
        fIOC = true; /* See 4.10.8 */
        Log2Func(("Interrupting, short IN packet\n"));
    }
    if (fIOC)
        ehciR3SetInterrupt(pDevIns, pThis, EHCI_STATUS_THRESHOLD_INT);
}


/**
 * Transfer completion callback routine.
 *
 * VUSB will call this when a transfer have been completed
 * in a one or another way.
 *
 * @param   pInterface      Pointer to EHCI::ROOTHUB::IRhPort.
 * @param   pUrb            Pointer to the URB in question.
 */
static DECLCALLBACK(void) ehciR3RhXferCompletion(PVUSBIROOTHUBPORT pInterface, PVUSBURB pUrb)
{
    PEHCICC     pThisCC = RT_FROM_MEMBER(pInterface, EHCICC, RootHub.IRhPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PEHCI       pThis   = PDMDEVINS_2_DATA(pDevIns, PEHCI);

    LogFlow(("%s: ehciR3RhXferCompletion: EdAddr=%RGp cTds=%d TdAddr0=%RGp\n",
             pUrb->pszDesc, pUrb->pHci->EdAddr, pUrb->pHci->cTds, pUrb->paTds[0].TdAddr));
    LogFlow(("%s: ehciR3RhXferCompletion: cbData=%x status=%x\n", pUrb->pszDesc, pUrb->cbData, pUrb->enmStatus));

    Assert(pUrb->pHci->cTds == 1);

    RTCritSectEnter(&pThisCC->CritSect);
    pThisCC->fIdle = false;   /* Mark as active */

    switch (pUrb->paTds[0].TdType)
    {
        case EHCI_DESCRIPTOR_QH:
            ehciR3RhXferCompleteQH(pDevIns, pThis, pThisCC, pUrb);
            break;

        case EHCI_DESCRIPTOR_ITD:
            ehciR3RhXferCompleteITD(pDevIns, pThis, pThisCC, pUrb);
            break;

        case EHCI_DESCRIPTOR_SITD:
        case EHCI_DESCRIPTOR_FSTN:
            AssertFailed();
            break;
    }

    ehciR3CalcTimerIntervals(pThis, pThisCC, pThisCC->uFrameRateDefault);
    RTCritSectLeave(&pThisCC->CritSect);
    RTSemEventMultiSignal(pThisCC->hSemEventFrame);
}

/**
 * Worker for ehciR3RhXferError that handles the error case of
 * a URB made up of queue heads/descriptors
 *
 * @returns true if the URB should be retired.
 * @returns false if the URB should be retried.
 * @param   pDevIns     The device instance.
 * @param   pThisCC     EHCI instance data, ring-3 edition. (For stats.)
 * @param   pUrb        Pointer to the URB in question.
 */
static bool ehciR3RhXferErrorQH(PPDMDEVINS pDevIns, PEHCICC pThisCC, PVUSBURB pUrb)
{
    EHCI_QHD qhd;
    EHCI_QTD qtd;

    /* Read the whole QHD & QTD */
    ehciR3ReadQHD(pDevIns, pUrb->pHci->EdAddr, &qhd);
    Assert(pUrb->paTds[0].TdAddr == ((RTGCPHYS)qhd.CurrQTD.Pointer << EHCI_TD_PTR_SHIFT));
    ehciR3ReadQTD(pDevIns, (RTGCPHYS)qhd.CurrQTD.Pointer << EHCI_TD_PTR_SHIFT, &qtd);

    /*
     * Check if the TDs still are valid.
     * This will make sure the TdCopy is up to date.
     */
    /** @todo IMPORTANT! we must check if the ED is still valid at this point!!! */
    if (ehciR3QhdHasUrbBeenCanceled(pThisCC, pUrb, &qhd, &qtd))
    {
        Log(("%s: ehciR3RhXferError: TdAddr0=%RGp canceled!\n", pUrb->pszDesc, pUrb->paTds[0].TdAddr));
        return true;
    }
    return true;
}

/**
 * Handle transfer errors.
 *
 * VUSB calls this when a transfer attempt failed. This function will respond
 * indicating whether to retry or complete the URB with failure.
 *
 * @returns true if the URB should be retired.
 * @returns false if the URB should be retried.
 * @param   pInterface      Pointer to EHCI::ROOTHUB::IRhPort.
 * @param   pUrb            Pointer to the URB in question.
 */
static DECLCALLBACK(bool) ehciR3RhXferError(PVUSBIROOTHUBPORT pInterface, PVUSBURB pUrb)
{
    PEHCICC     pThisCC = RT_FROM_MEMBER(pInterface, EHCICC, RootHub.IRhPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    bool        fRetire = false;

    RTCritSectEnter(&pThisCC->CritSect);
    /*
     * Don't retry on stall.
     */
    if (pUrb->enmStatus == VUSBSTATUS_STALL)
    {
        Log2(("%s: ehciR3RhXferError: STALL, giving up.\n", pUrb->pszDesc));
        fRetire = true;
    }
    else
    {
        switch (pUrb->paTds[0].TdType)
        {
            case EHCI_DESCRIPTOR_QH:
            {
                fRetire = ehciR3RhXferErrorQH(pDevIns, pThisCC, pUrb);
                break;
            }

            /*
             * Isochronous URBs can't be retried.
             */
            case EHCI_DESCRIPTOR_ITD:
            case EHCI_DESCRIPTOR_SITD:
            case EHCI_DESCRIPTOR_FSTN:
            default:
                fRetire = true;
                break;
        }
    }

    RTCritSectLeave(&pThisCC->CritSect);
    return fRetire;
}

/**
 * A worker for ehciR3ServiceQTD which submits the specified TD.
 *
 * @returns true on success.
 * @returns false on failure to submit.
 */
static bool ehciR3SubmitQTD(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC, RTGCPHYS GCPhysQHD,
                            PEHCI_QHD pQhd, RTGCPHYS GCPhysQTD, PEHCI_QTD pQtd, const unsigned iFrame)
{
    /*
     * Determine the endpoint direction.
     */
    VUSBDIRECTION enmDir;
    switch(pQtd->Token.Bits.PID)
    {
        case EHCI_QTD_PID_OUT:
            enmDir = VUSBDIRECTION_OUT;
            break;
        case EHCI_QTD_PID_IN:
            enmDir = VUSBDIRECTION_IN;
            break;
        case EHCI_QTD_PID_SETUP:
            enmDir = VUSBDIRECTION_SETUP;
            break;
        default:
            return false;
    }

    VUSBXFERTYPE enmType;

    enmType = ehciR3QueryTransferType(pQhd);

    pThisCC->fIdle = false;   /* Mark as active */

    /*
     * Allocate and initialize the URB.
     */
    PVUSBURB pUrb = VUSBIRhNewUrb(pThisCC->RootHub.pIRhConn, pQhd->Characteristics.DeviceAddress, VUSB_DEVICE_PORT_INVALID,
                                  enmType, enmDir, pQtd->Token.Bits.Length, 1, NULL);
    if (!pUrb)
        /* retry later... */
        return false;

    pUrb->EndPt             = pQhd->Characteristics.EndPt;
    pUrb->fShortNotOk       = (enmDir != VUSBDIRECTION_IN);    /** @todo ??? */
    pUrb->enmStatus         = VUSBSTATUS_OK;
    pUrb->pHci->cTds        = 1;
    pUrb->pHci->EdAddr      = GCPhysQHD;
    pUrb->pHci->fUnlinked   = false;
    pUrb->pHci->u32FrameNo  = iFrame;
    pUrb->paTds[0].TdAddr   = GCPhysQTD;
    pUrb->paTds[0].TdType   = EHCI_DESCRIPTOR_QH;
    AssertCompile(sizeof(pUrb->paTds[0].TdCopy) >= sizeof(*pQtd));
    memcpy(pUrb->paTds[0].TdCopy, pQtd, sizeof(*pQtd));
#if 0 /* color the data */
    memset(pUrb->abData, 0xfe, cbTotal);
#endif

    /* copy the data */
    if (    pQtd->Token.Bits.Length
        &&  enmDir != VUSBDIRECTION_IN)
    {
        unsigned curOffset = 0;
        unsigned cbTransfer = pQtd->Token.Bits.Length;

        for (unsigned i=pQtd->Token.Bits.CurrentPage;i<RT_ELEMENTS(pQtd->Buffer.Buffer);i++)
        {
            RTGCPHYS GCPhysBuf;
            unsigned cbCurTransfer;

            GCPhysBuf = (RTGCPHYS)pQtd->Buffer.Buffer[i].Pointer << EHCI_BUFFER_PTR_SHIFT;
            if (i == 0)
                GCPhysBuf += pQtd->Buffer.Offset.Offset;

            cbCurTransfer = GUEST_PAGE_SIZE - (GCPhysBuf & GUEST_PAGE_OFFSET_MASK);
            cbCurTransfer = RT_MIN(cbCurTransfer, cbTransfer);

            ehciPhysRead(pDevIns, GCPhysBuf, &pUrb->abData[curOffset], cbCurTransfer);

            Log3Func(("packet data:\n"
                  "%.*Rhxd\n",
                  cbCurTransfer, &pUrb->abData[curOffset]));

            curOffset  += cbCurTransfer;
            cbTransfer -= cbCurTransfer;

            if (cbTransfer == 0)
                break;
            Assert(cbTransfer < pQtd->Token.Bits.Length);
        }
    }

    /*
     * Submit the URB.
     */
    ehciR3InFlightAddUrb(pThis, pThisCC, pUrb);
    Log(("%s: ehciSubmitQtd: QtdAddr=%RGp GCPhysQHD=%RGp cbData=%#x\n",
         pUrb->pszDesc, GCPhysQTD, GCPhysQHD, pUrb->cbData));
    RTCritSectLeave(&pThisCC->CritSect);
    int rc = VUSBIRhSubmitUrb(pThisCC->RootHub.pIRhConn, pUrb, &pThisCC->RootHub.Led);
    RTCritSectEnter(&pThisCC->CritSect);
    if (RT_SUCCESS(rc))
        return true;

    /* Failure cleanup. Can happen if we're still resetting the device or out of resources. */
    LogFunc(("failed GCPhysQtd=%RGp GCPhysQHD=%RGp pUrb=%p!!\n",
         GCPhysQTD, GCPhysQHD, pUrb));
    ehciR3InFlightRemove(pThis, pThisCC, GCPhysQTD);

    /* Also mark the QH as halted and inactive and write back the changes. */
    pQhd->Overlay.OrgQTD.Token.Bits.Active = 0;
    pQhd->Overlay.OrgQTD.Token.Bits.Halted = 1;
    ehciR3UpdateQHD(pDevIns, GCPhysQHD, pQhd);
    return false;
}

/**
 * A worker for ehciR3ServiceITD which submits the specified TD.
 *
 * @returns true on success.
 * @returns false on failure to submit.
 */
static bool ehciR3SubmitITD(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC,
                            PEHCI_ITD_PAD pItd, RTGCPHYS ITdAddr, const unsigned iFrame)
{
    /*
     * Determine the endpoint direction.
     */
    VUSBDIRECTION enmDir;
    if(pItd->Buffer.Misc.DirectionIn)
        enmDir = VUSBDIRECTION_IN;
    else
        enmDir = VUSBDIRECTION_OUT;

    /*
     * Extract the packet sizes and calc the total URB size.
     */
    struct
    {
        uint16_t cb;
    } aPkts[EHCI_NUM_ITD_TRANSACTIONS];

    unsigned cPackets = 0;
    uint32_t cbTotal = 0;
    for (unsigned i=0;i<RT_ELEMENTS(pItd->Transaction);i++)
    {
        if (pItd->Transaction[i].Active)
        {
            aPkts[cPackets].cb = pItd->Transaction[i].Length;
            cbTotal += pItd->Transaction[i].Length;
            cPackets++;
        }
    }
    Assert(cbTotal <= 24576);

    pThisCC->fIdle = false;   /* Mark as active */

    /*
     * Allocate and initialize the URB.
     */
    PVUSBURB pUrb = VUSBIRhNewUrb(pThisCC->RootHub.pIRhConn, pItd->Buffer.Misc.DeviceAddress, VUSB_DEVICE_PORT_INVALID,
                                  VUSBXFERTYPE_ISOC, enmDir, cbTotal, 1, NULL);
    if (!pUrb)
        /* retry later... */
        return false;

    pUrb->EndPt             = pItd->Buffer.Misc.EndPt;
    pUrb->fShortNotOk       = false;
    pUrb->enmStatus         = VUSBSTATUS_OK;
    pUrb->pHci->cTds        = 1;
    pUrb->pHci->EdAddr      = ITdAddr;
    pUrb->pHci->fUnlinked   = false;
    pUrb->pHci->u32FrameNo  = iFrame;
    pUrb->paTds[0].TdAddr   = ITdAddr;
    pUrb->paTds[0].TdType   = EHCI_DESCRIPTOR_ITD;
    AssertCompile(sizeof(pUrb->paTds[0].TdCopy) >= sizeof(*pItd));
    memcpy(pUrb->paTds[0].TdCopy, pItd, sizeof(*pItd));
#if 0 /* color the data */
    memset(pUrb->abData, 0xfe, cbTotal);
#endif

    /* copy the data */
    if (    cbTotal
        &&  enmDir != VUSBDIRECTION_IN)
    {
        unsigned curOffset = 0;

        for (unsigned i=0;i<RT_ELEMENTS(pItd->Transaction);i++)
        {
            RTGCPHYS GCPhysBuf;

            if (pItd->Transaction[i].Active)
            {
                const unsigned  pg = pItd->Transaction[i].PG;

                GCPhysBuf = (RTGCPHYS)pItd->Buffer.Buffer[pg].Pointer << EHCI_BUFFER_PTR_SHIFT;
                GCPhysBuf += pItd->Transaction[i].Offset;

                /* If the transfer would cross page boundary, use the next sequential PG pointer
                 * for the second part (section 4.7.1).
                 */
                if (pItd->Transaction[i].Offset + pItd->Transaction[i].Length > GUEST_PAGE_SIZE)
                {
                    unsigned    cb1 = GUEST_PAGE_SIZE - pItd->Transaction[i].Offset;
                    unsigned    cb2 = pItd->Transaction[i].Length - cb1;

                    ehciPhysRead(pDevIns, GCPhysBuf, &pUrb->abData[curOffset], cb1);
                    if ((pg + 1) >= EHCI_NUM_ITD_PAGES)
                       LogRelMax(10, ("EHCI: Crossing to nonstandard page %d in iTD at %RGp on submit.\n", pg + 1, pUrb->paTds[0].TdAddr));

                    GCPhysBuf = (RTGCPHYS)pItd->Buffer.Buffer[pg + 1].Pointer << EHCI_BUFFER_PTR_SHIFT;
                    ehciPhysRead(pDevIns, GCPhysBuf, &pUrb->abData[curOffset + cb1], cb2);
                }
                else
                    ehciPhysRead(pDevIns, GCPhysBuf, &pUrb->abData[curOffset], pItd->Transaction[i].Length);

                curOffset += pItd->Transaction[i].Length;
            }
        }
    }

    /* setup the packets */
    pUrb->cIsocPkts = cPackets;
    unsigned off = 0;
    for (unsigned i = 0; i < pUrb->cIsocPkts; i++)
    {
        pUrb->aIsocPkts[i].enmStatus = VUSBSTATUS_NOT_ACCESSED;
        pUrb->aIsocPkts[i].off = off;
        off += pUrb->aIsocPkts[i].cb = aPkts[i].cb;
    }
    Assert(off == cbTotal);

    /*
     * Submit the URB.
     */
    ehciR3InFlightAddUrb(pThis, pThisCC, pUrb);
    Log(("%s: ehciR3SubmitITD: cbData=%#x cIsocPkts=%d TdAddr=%RGp (%#x)\n",
         pUrb->pszDesc, pUrb->cbData, pUrb->cIsocPkts, ITdAddr, iFrame));
    RTCritSectLeave(&pThisCC->CritSect);
    int rc = VUSBIRhSubmitUrb(pThisCC->RootHub.pIRhConn, pUrb, &pThisCC->RootHub.Led);
    RTCritSectEnter(&pThisCC->CritSect);
    if (RT_SUCCESS(rc))
        return true;

    /* Failure cleanup. Can happen if we're still resetting the device or out of resources. */
    LogFunc(("failed pUrb=%p cbData=%#x cTds=%d ITdAddr0=%RGp - rc=%Rrc\n",
         pUrb, cbTotal, 1, ITdAddr, rc));
    ehciR3InFlightRemove(pThis, pThisCC, ITdAddr);
    return false;
}


/**
 * Services an ITD list  (only for high-speed isochronous endpoints; all others use queues)
 *
 * An ITD can contain up to 8 transactions, which are all processed within a single frame.
 * Note that FRINDEX includes the micro-frame number, but only bits [12:3] are used as an
 * index into the periodic frame list (see 4.7.1).
 */
static void ehciR3ServiceITD(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC,
                             RTGCPHYS GCPhys, EHCI_SERVICE_TYPE enmServiceType, const unsigned iFrame)
{
    RT_NOREF(enmServiceType);
    bool          fAnyActive = false;
    EHCI_ITD_PAD  PaddedItd;
    PEHCI_ITD_PAD pItd = &PaddedItd;

    if (ehciR3IsTdInFlight(pThisCC, GCPhys))
        return;

    /* Read the whole ITD */
    ehciR3ReadItd(pDevIns, GCPhys, &PaddedItd);

    Log2((" ITD: %RGp={Addr=%x EndPt=%x Dir=%s MaxSize=%x Mult=%d}\n", GCPhys, pItd->Buffer.Misc.DeviceAddress, pItd->Buffer.Misc.EndPt, (pItd->Buffer.Misc.DirectionIn) ? "in" : "out", pItd->Buffer.Misc.MaxPacket, pItd->Buffer.Misc.Multi));

    /* Some basic checks */
    for (unsigned i = 0; i < RT_ELEMENTS(pItd->Transaction); i++)
    {
        if (pItd->Transaction[i].Active)
        {
            fAnyActive = true;
            if (pItd->Transaction[i].PG >= EHCI_NUM_ITD_PAGES)
            {
                /* Using out of range PG value (7) yields undefined behavior. We will attempt
                 * the last page below 4GB (which is ROM, not writable).
                 */
                LogRelMax(10, ("EHCI: Nonstandard page value %d in iTD at %RGp.\n", pItd->Transaction[i].PG, (RTGCPHYS)GCPhys));
            }

            Log2(("      T%d Len=%x Offset=%x PG=%d IOC=%d Buffer=%x\n", i, pItd->Transaction[i].Length, pItd->Transaction[i].Offset, pItd->Transaction[i].PG, pItd->Transaction[i].IOC,
                   pItd->Buffer.Buffer[pItd->Transaction[i].PG].Pointer));
        }
    }
    /* We can't service one transaction every 125 usec, so we'll handle all 8 of them at once. */
    if (fAnyActive)
        ehciR3SubmitITD(pDevIns, pThis, pThisCC, pItd, GCPhys, iFrame);
    else
        Log2((" ITD not active, skipping.\n"));
}

/**
 * Services an SITD list
 */
static void ehciR3ServiceSITD(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC,
                              RTGCPHYS GCPhys, EHCI_SERVICE_TYPE enmServiceType, const unsigned iFrame)
{
    RT_NOREF(pThis, pThisCC, enmServiceType, iFrame);

    /* Read the whole SITD */
    EHCI_SITD sitd;
    ehciR3ReadSitd(pDevIns, GCPhys, &sitd);

    Log2((" SITD: %RGp={Addr=%x EndPt=%x Dir=%s MaxSize=%x}\n", GCPhys, sitd.Address.DeviceAddress, sitd.Address.EndPt, (sitd.Address.DirectionIn) ? "in" : "out", sitd.Transfer.Length));

    if (sitd.Transfer.Active)
        AssertMsgFailed(("SITD lists not implemented; active SITD should never occur!\n"));
    else
        Log2((" SITD not active, skipping.\n"));
}

/**
 * Copies the currently active QTD to the QH overlay area
 */
static void ehciR3QHSetupOverlay(PPDMDEVINS pDevIns, PEHCI_QHD pQhd, RTGCPHYS GCPhysQHD, PEHCI_QTD pQtd, RTGCPHYS GCPhysQTD)
{
    bool fDataToggle = pQhd->Overlay.OrgQTD.Token.Bits.DataToggle;

    Assert(pQtd->Token.Bits.Active);

    Log2Func(("current pointer %RGp old %RGp\n", GCPhysQTD, ((RTGCPHYS)pQhd->CurrQTD.Pointer << EHCI_TD_PTR_SHIFT)));
    pQhd->CurrQTD.Pointer  = GCPhysQTD >> EHCI_TD_PTR_SHIFT;
    pQhd->CurrQTD.Reserved = 0;
    pQhd->Overlay.OrgQTD   = *pQtd;
    /* All fields except those below are copied from the QTD; see 4.10.2 */
    if (pQhd->Characteristics.DataToggle)
        pQhd->Overlay.OrgQTD.Token.Bits.DataToggle  = fDataToggle;   /* Preserve data toggle bit in the queue head */

    pQhd->Overlay.Status.Buffer1.CProgMask = 0;
    pQhd->Overlay.Status.Buffer2.FrameTag  = 0;
    pQhd->Overlay.Status.AltNextQTD.NakCnt = pQhd->Characteristics.NakCountReload;
    /* Note: ping state not changed if it's a high-speed device */

    /* Save the current QTD to the overlay area */
    ehciR3UpdateQHD(pDevIns, GCPhysQHD, pQhd);
}

/**
 * Updates the currently active QTD to the QH overlay area
 */
static void ehciR3QHUpdateOverlay(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC,
                                  PEHCI_QHD pQhd, RTGCPHYS GCPhysQHD, PEHCI_QTD pQtd)
{
    Assert(!pQtd->Token.Bits.Active);
    pQhd->Overlay.OrgQTD = *pQtd;
    if (!pQhd->Overlay.OrgQTD.Next.Terminate)
    {
        EHCI_QTD qtdNext;
        RTGCPHYS GCPhysNextQTD = (RTGCPHYS)pQhd->Overlay.OrgQTD.Next.Pointer << EHCI_TD_PTR_SHIFT;

        if (ehciR3IsTdInFlight(pThisCC, GCPhysNextQTD))
        {
            /* Read the whole QTD */
            ehciR3ReadQTD(pDevIns, GCPhysNextQTD, &qtdNext);
            if (qtdNext.Token.Bits.Active)
            {
                ehciR3QHSetupOverlay(pDevIns, pQhd, GCPhysQHD, &qtdNext, GCPhysNextQTD);
                return;
            }
            else
            {
                /* Td has been cancelled! */
                LogFunc(("in-flight qTD %RGp has been cancelled! (active=%d T=%d)\n", GCPhysNextQTD, qtdNext.Token.Bits.Active, pQhd->Overlay.OrgQTD.Next.Terminate));
                /** @todo we don't properly cancel the URB; it will remain active on the host.... */
                ehciR3InFlightRemove(pThis, pThisCC, GCPhysNextQTD);
            }
        }
    }
    /* Save the current QTD to the overlay area. */
    ehciR3UpdateQHD(pDevIns, GCPhysQHD, pQhd);
}

/**
 * Services a QTD list
 */
static RTGCPHYS ehciR3ServiceQTD(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC,  PEHCI_QHD pQhd, RTGCPHYS GCPhysQHD,
                                   RTGCPHYS GCPhysQTD, EHCI_SERVICE_TYPE enmServiceType, const unsigned iFrame)
{
    EHCI_QTD qtd;

    /* Read the whole QTD */
    ehciR3ReadQTD(pDevIns, GCPhysQTD, &qtd);

    if (qtd.Token.Bits.Active)
    {
        if (!ehciR3IsTdInFlight(pThisCC, GCPhysQTD))
        {
            /* Don't queue more than one non-bulk transfer at a time */
            if (    ehciR3QueryTransferType(pQhd) != VUSBXFERTYPE_BULK
                &&  pQhd->Overlay.OrgQTD.Token.Bits.Active)
                return 0;

            Log2((" Length=%x IOC=%d DT=%d PID=%s}\n", qtd.Token.Bits.Length, qtd.Token.Bits.IOC, qtd.Token.Bits.DataToggle, ehciPID2Str(qtd.Token.Bits.PID)));
            if (    !pQhd->Overlay.OrgQTD.Token.Bits.Active
                ||  GCPhysQTD == (RTGCPHYS)pQhd->CurrQTD.Pointer << EHCI_TD_PTR_SHIFT)
                ehciR3QHSetupOverlay(pDevIns, pQhd, GCPhysQHD, &qtd, GCPhysQTD);
            else
                Log2Func(("transfer %RGp in progress -> don't update the overlay\n", (RTGCPHYS)pQhd->CurrQTD.Pointer << EHCI_TD_PTR_SHIFT));

            ehciR3SubmitQTD(pDevIns, pThis, pThisCC, GCPhysQHD, pQhd, GCPhysQTD, &qtd, iFrame);

            /* Set the Reclamation bit in USBSTS (4.10.3) */
            if (enmServiceType == EHCI_SERVICE_ASYNC)
            {
                Log2Func(("activity detected, set EHCI_STATUS_RECLAMATION\n"));
                ASMAtomicOrU32(&pThis->intr_status, EHCI_STATUS_RECLAMATION);
            }

            /* Reread the whole QTD; it might have been completed already and therefore changed */
            ehciR3ReadQTD(pDevIns, GCPhysQTD, &qtd);
        }
        /* Table 4-10: any transfer with zero size: queue only one */
        if (qtd.Token.Bits.Length == 0)
        {
            LogFunc(("queue only one: transfer with zero size\n"));
            return 0;
        }

        /* We can't queue more than one TD if we can't decide here and now which TD we should take next */
        if (    qtd.Token.Bits.Active   /* only check if this urb is in-flight */
            &&  qtd.Token.Bits.PID == EHCI_QTD_PID_IN
            &&  !qtd.AltNext.Terminate
            &&  !qtd.Next.Terminate
            &&  qtd.Next.Pointer != qtd.AltNext.Pointer)
        {
            Log2Func(("Can't decide which pointer to take next; don't queue more than one!\n"));
            return 0;
        }
    }
    else
    {
        Log2((" Not active}\n"));
        return 0;
    }

    /* If the 'Bytes to Transfer' field is not zero and the T-bit in the AltNext pointer is zero, then use this pointer. (4.10.2) */
    if (    !qtd.Token.Bits.Active                      /* only check if no urbs are in-flight */
        &&  qtd.Token.Bits.PID == EHCI_QTD_PID_IN       /* short packets only apply to incoming tds */
        &&  !qtd.AltNext.Terminate
        &&  qtd.Token.Bits.Length)
    {
        Assert(qtd.AltNext.Pointer);
        Log2(("Taking alternate pointer %RGp\n", (RTGCPHYS)(qtd.AltNext.Pointer << EHCI_TD_PTR_SHIFT)));
        return (RTGCPHYS)qtd.AltNext.Pointer << EHCI_TD_PTR_SHIFT;
    }
    else
    {
        Assert(qtd.Next.Pointer || qtd.Next.Terminate);
        if (qtd.Next.Terminate || !qtd.Next.Pointer)
            return 0;
        return (RTGCPHYS)qtd.Next.Pointer << EHCI_TD_PTR_SHIFT;
    }
}

/**
 * Services a QHD list
 */
static bool ehciR3ServiceQHD(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC,
                             RTGCPHYS GCPhys, EHCI_SERVICE_TYPE enmServiceType, const unsigned iFrame)
{
    EHCI_QHD qhd;

    Log2Func(("%RGp={", GCPhys));

    /* Read the whole QHD */ /** @todo reading too much */
    ehciR3ReadQHD(pDevIns, GCPhys, &qhd);

    /* Only interrupt qHDs should be linked from the periodic list; the S-mask field description
     * in table 3-20 clearly says a zero S-mask on the periodic list yields undefined results. In reality,
     * the Windows HCD links dummy qHDs at the start of the interrupt queue and these have an empty S-mask.
     * If we're servicing the periodic list, check the S-mask first; that takes care of the dummy qHDs.
     */
    if (enmServiceType == EHCI_SERVICE_PERIODIC)
    {
        // If iFrame was a micro-frame number, we should check the S-mask against it. But
        // we're processing all micro-frames at once, so we'll look at any qHD with non-zero S-mask
        if (qhd.Caps.SMask == 0) {
            Log2Func(("periodic qHD not scheduled for current frame -> next\n"));
            return true;
        }
        else
            Log2Func(("periodic qHD scheduled for current frame, processing\n"));
    }
    else
    {
        Assert(enmServiceType == EHCI_SERVICE_ASYNC);
        /* Empty schedule detection (4.10.1), for async schedule only */
        if (qhd.Characteristics.HeadReclamation) /* H-bit set but not an interrupt qHD */
        {
            if (pThis->intr_status & EHCI_STATUS_RECLAMATION)
            {
                Log2Func(("clear EHCI_STATUS_RECLAMATION\n"));
                ASMAtomicAndU32(&pThis->intr_status, ~EHCI_STATUS_RECLAMATION);
            }
            else
            {
                Log2Func(("empty schedule -> bail out\n"));
                pThis->fAsyncTraversalTimerActive = true;
                return false; /** stop traversing the list */
            }
        }
    }

    /* no active qTD here or in the next queue element -> skip to next horizontal pointer (Figure 4.14 & 4.10.2) */
    if (    !qhd.Overlay.OrgQTD.Token.Bits.Active
        &&  qhd.Characteristics.InActiveNext)
    {
        Log2Func(("skip to next pointer (active)\n"));
        return true;
    }
    /* we are ignoring the Inactivate on Next Transaction bit; only applies to periodic lists & low or full speed devices (table 3.9) */

    /** We are not allowed to handle multiple TDs unless async park is enabled (and only for high-speed devices), but we can cheat a bit. */
    unsigned PMCount = 1;
    if (    (pThis->cmd & EHCI_CMD_ASYNC_SCHED_PARK_ENABLE)
        &&  qhd.Characteristics.EndPtSpeed == EHCI_QHD_EPT_SPEED_HIGH
        &&  enmServiceType == EHCI_SERVICE_ASYNC)
    {
        PMCount = (pThis->cmd & EHCI_CMD_ASYNC_SCHED_PARK_MODE_COUNT_MASK) >> EHCI_CMD_ASYNC_SCHED_PARK_MODE_COUNT_SHIFT;
        Log2Func(("PM Count=%d\n", PMCount));

        /* We will attempt to queue a bit more if we're allowed to queue more than one TD. */
        if (PMCount != 1)
            PMCount = 16;
    }

    /* Queue as many transfer descriptors as possible */
    RTGCPHYS GCPhysQTD;
    if (qhd.Overlay.OrgQTD.Token.Bits.Active)
    {
        Assert(ehciR3IsTdInFlight(pThisCC, (RTGCPHYS)qhd.CurrQTD.Pointer << EHCI_TD_PTR_SHIFT));
        GCPhysQTD = (RTGCPHYS)qhd.CurrQTD.Pointer << EHCI_TD_PTR_SHIFT;
    }
    else
    {
        /* If the 'Bytes to Transfer' field is not zero and the T-bit in the AltNext pointer is zero, then use this pointer. (4.10.2) */
        if (   !qhd.Overlay.OrgQTD.AltNext.Terminate
            &&  qhd.Overlay.OrgQTD.Token.Bits.Length)
        {
            Assert(qhd.Overlay.OrgQTD.AltNext.Pointer);
            Log2(("Taking alternate pointer %RGp\n", (RTGCPHYS)qhd.Overlay.OrgQTD.AltNext.Pointer << EHCI_TD_PTR_SHIFT));
            GCPhysQTD = (RTGCPHYS)qhd.Overlay.OrgQTD.AltNext.Pointer << EHCI_TD_PTR_SHIFT;
        }
        else
        {
            Assert(qhd.Overlay.OrgQTD.Next.Pointer || qhd.Overlay.OrgQTD.Next.Terminate || qhd.Overlay.OrgQTD.Token.Bits.Halted);
            if (qhd.Overlay.OrgQTD.Next.Terminate || !qhd.Overlay.OrgQTD.Next.Pointer || qhd.Overlay.OrgQTD.Token.Bits.Halted)
                GCPhysQTD = 0;
            else
                GCPhysQTD = (RTGCPHYS)qhd.Overlay.OrgQTD.Next.Pointer << EHCI_TD_PTR_SHIFT;
        }
    }

    while (GCPhysQTD && PMCount--)
    {
        GCPhysQTD = ehciR3ServiceQTD(pDevIns, pThis, pThisCC, &qhd, GCPhys, GCPhysQTD, enmServiceType, iFrame);

        /* Reread the whole QHD; urb submit can call us right back which causes QH changes */ /** @todo reading too much */
        ehciR3ReadQHD(pDevIns, GCPhys, &qhd);
    }
    return true;
}

/**
 * Services a FSTN list
 */
static void ehciR3ServiceFSTN(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC,
                              RTGCPHYS GCPhys, EHCI_SERVICE_TYPE enmServiceType, const unsigned iFrame)
{
    RT_NOREF(pDevIns, pThis, pThisCC, GCPhys, enmServiceType, iFrame);
    AssertMsgFailed(("FSTN lists not implemented; should never occur!\n"));
}

/**
 * Services the async list.
 *
 * The async list has complex URB assembling, but that's taken
 * care of at VUSB level (unlike the other transfer types).
 */
static void ehciR3ServiceAsyncList(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC, const unsigned iFrame)
{
    RTGCPHYS GCPhysHead = pThis->async_list_base;
    RTGCPHYS GCPhys = GCPhysHead;
    EHCI_TD_PTR ptr;
    unsigned cIterations = 0;

    Assert(!(pThis->async_list_base & 0x1f));
    Assert(pThis->cmd & EHCI_CMD_ASYNC_SCHED_ENABLE);
    Assert(pThis->cmd & EHCI_CMD_RUN);

    Log2Func(("%RGp\n", GCPhysHead));
#ifdef LOG_ENABLED
    ehciR3DumpQH(pDevIns, GCPhysHead, true);
#endif

    /* Signal the async advance doorbell interrupt (if required) */
    if (    (pThis->cmd & EHCI_CMD_INT_ON_ADVANCE_DOORBELL))
//        &&  !pThis->cInFlight)
        ehciR3SetInterrupt(pDevIns, pThis, EHCI_STATUS_INT_ON_ASYNC_ADV);

    /* Process the list of qHDs */
    for (;;)
    {
        /* Process the qHD */
        if (!ehciR3ServiceQHD(pDevIns, pThis, pThisCC, GCPhys, EHCI_SERVICE_ASYNC, iFrame))
            break;

        /* Read the next pointer */
        RTGCPHYS GCPhysLast = GCPhys;
        ehciR3ReadTDPtr(pDevIns, GCPhys, &ptr);

        /* Detect obvious loops. */
        if (GCPhys == ((RTGCPHYS)ptr.Pointer << EHCI_TD_PTR_SHIFT))
            break;

        /* Technically a zero address could be valid, but that's extremely unlikely! */
        Assert(ptr.Pointer || ptr.Terminate);
        if (ptr.Terminate || !ptr.Pointer)
            break;

        /* Not clear what we should do if this *is* something other than a qHD. */
        AssertMsg(ptr.Type == EHCI_DESCRIPTOR_QH, ("Unexpected pointer to type %d\n", ptr.Type));
        if (ptr.Type != EHCI_DESCRIPTOR_QH)
            break;

        /* If we ran too many iterations, the list must be looping in on itself.
         * On a real controller loops wouldn't be fatal, as it will eventually
         * run out of time in the micro-frame.
         */
        AssertMsgBreak(++cIterations < 128, ("Too many iterations, exiting\n"));

        /* next */
        GCPhys = (RTGCPHYS)ptr.Pointer << EHCI_TD_PTR_SHIFT;
        Assert(!(GCPhys & 0x1f));
        if (   GCPhys == GCPhysHead
            || GCPhys == GCPhysLast)
            break;  /* break the loop */
    }

#ifdef LOG_ENABLED
    if (g_fLogControlEPs)
        ehciR3DumpQH(pDevIns, GCPhysHead, true);
#endif
}


/**
 * Services the periodic list.
 *
 * On the interrupt portion of the periodic list we must reassemble URBs from multiple
 * TDs using heuristics derived from USB tracing done in the guests and guest source
 * code (when available).
 */
static void ehciR3ServicePeriodicList(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC, const unsigned iFrame)
{
    Assert(pThis->cmd & EHCI_CMD_PERIODIC_SCHED_ENABLE);

#ifdef LOG_ENABLED
    RTGCPHYS pFramePtrHead = 0;
    if (g_fLogInterruptEPs)
    {
        RTGCPHYS pFramePtr = pThis->periodic_list_base + iFrame * sizeof(EHCI_FRAME_LIST_PTR);

        pFramePtrHead = pFramePtr;

        char sz[48];
        RTStrPrintf(sz, sizeof(sz), "Int%02x before", iFrame);
        ehciR3DumpPeriodicList(pDevIns, pFramePtrHead, sz, true);
    }
#endif

    /*
     * Iterate the periodic list.
     */
    EHCI_FRAME_LIST_PTR FramePtr;
    RTGCPHYS            GCPhys = pThis->periodic_list_base + iFrame * sizeof(FramePtr);
    unsigned            iterations = 0;

    ehciR3ReadFrameListPtr(pDevIns, GCPhys, &FramePtr);
    while (!FramePtr.Terminate && (pThis->cmd & EHCI_CMD_RUN))
    {
        GCPhys = (RTGCPHYS)FramePtr.FrameAddr << EHCI_FRAME_LIST_NEXTPTR_SHIFT;
        /* Process the descriptor based on its type. Note that on the periodic
         * list, HCDs may (and do) mix iTDs and qHDs more or less freely.
         */
        switch(FramePtr.Type)
        {
            case EHCI_DESCRIPTOR_ITD:
                ehciR3ServiceITD(pDevIns, pThis, pThisCC, GCPhys, EHCI_SERVICE_PERIODIC, iFrame);
                break;
            case EHCI_DESCRIPTOR_SITD:
                ehciR3ServiceSITD(pDevIns, pThis, pThisCC, GCPhys, EHCI_SERVICE_PERIODIC, iFrame);
                break;
            case EHCI_DESCRIPTOR_QH:
                ehciR3ServiceQHD(pDevIns, pThis, pThisCC, GCPhys, EHCI_SERVICE_PERIODIC, iFrame);
                break;
            case EHCI_DESCRIPTOR_FSTN:
                ehciR3ServiceFSTN(pDevIns, pThis, pThisCC, GCPhys, EHCI_SERVICE_PERIODIC, iFrame);
                break;
        }

        /* If we ran too many iterations, the list must be looping in on itself.
         * On a real controller loops wouldn't be fatal, as it will eventually
         * run out of time in the micro-frame.
         */
        if (++iterations == 2048)
        {
            AssertMsgFailed(("ehciR3ServicePeriodicList: Too many iterations, exiting\n"));
            break;
        }
        /* Read the next link */
        ehciR3ReadFrameListPtr(pDevIns, GCPhys, &FramePtr);

        /* Detect obvious loops. */
        if (GCPhys == ((RTGCPHYS)FramePtr.FrameAddr << EHCI_FRAME_LIST_NEXTPTR_SHIFT))
            break;
    }

#ifdef LOG_ENABLED
    if (g_fLogInterruptEPs)
    {
        char sz[48];
        RTStrPrintf(sz, sizeof(sz), "Int%02x after ", iFrame);
        ehciR3DumpPeriodicList(pDevIns, pFramePtrHead, sz, true);
    }
#endif
}


/**
 * Calculate frame timer variables given a frame rate (1,000 Hz is the full speed).
 */
static void ehciR3CalcTimerIntervals(PEHCI pThis, PEHCICC pThisCC, uint32_t u32FrameRate)
{
    Assert(u32FrameRate <= EHCI_HARDWARE_TIMER_FREQ);

    pThis->uFramesPerTimerCall    = EHCI_HARDWARE_TIMER_FREQ / u32FrameRate;
    pThisCC->nsWait               = RT_NS_1SEC / u32FrameRate;
    pThisCC->cTicksPerFrame       = pThisCC->u64TimerHz / u32FrameRate;
    if (!pThisCC->cTicksPerFrame)
        pThisCC->cTicksPerFrame   = 1;
    pThisCC->uFrameRate           = u32FrameRate;
}

/**
 * Generate a Start-Of-Frame event, and set a timer for End-Of-Frame.
 */
static void ehciR3StartOfFrame(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC)
{
    uint32_t    uNewFrameRate = pThisCC->uFrameRate;
#ifdef LOG_ENABLED
    const uint32_t status_old = pThis->intr_status;
#endif

    pThis->SofTime += pThisCC->cTicksPerFrame;
    unsigned iFrame = (pThis->frame_idx >> EHCI_FRINDEX_FRAME_INDEX_SHIFT) & EHCI_FRINDEX_FRAME_INDEX_MASK;

    if (pThis->uIrqInterval < pThis->uFramesPerTimerCall)
        pThis->uIrqInterval = 0;
    else
        pThis->uIrqInterval -= pThis->uFramesPerTimerCall;

    /* Empty async list detection halted the async schedule */
    if (pThis->fAsyncTraversalTimerActive)
    {
        /* Table 4.7 in 4.8.4.1 */
        Log2Func(("setting STATUS_RECLAMATION after empty list detection\n"));
        ASMAtomicOrU32(&pThis->intr_status, EHCI_STATUS_RECLAMATION);
        pThis->fAsyncTraversalTimerActive = false;
    }

    /*
     * Periodic EPs (Isochronous & Interrupt)
     */
    if (pThis->cmd & EHCI_CMD_PERIODIC_SCHED_ENABLE)
    {
        int num_frames = RT_MAX(1, pThis->uFramesPerTimerCall >> EHCI_FRINDEX_FRAME_INDEX_SHIFT);
        Assert(num_frames > 0 && num_frames < 1024);

        ASMAtomicOrU32(&pThis->intr_status, EHCI_STATUS_PERIOD_SCHED);

        if (pThis->cmd & EHCI_CMD_RUN)
        {
            /* If we're running the frame timer at a reduced rate, we still need to process
             * all frames. Otherwise we risk completely missing newly scheduled periodic transfers.
             */
            for (int i = 0; i < num_frames; ++i)
                ehciR3ServicePeriodicList(pDevIns, pThis, pThisCC, (iFrame + i) & EHCI_FRINDEX_FRAME_INDEX_MASK);
        }
    }
    else
        ASMAtomicAndU32(&pThis->intr_status, ~EHCI_STATUS_PERIOD_SCHED);

    /*
     * Async EPs (Control and Bulk)
     */
    if (pThis->cmd & EHCI_CMD_ASYNC_SCHED_ENABLE)
    {
        ASMAtomicOrU32(&pThis->intr_status, EHCI_STATUS_ASYNC_SCHED);
        if (pThis->cmd & EHCI_CMD_RUN)
            ehciR3ServiceAsyncList(pDevIns, pThis, pThisCC, iFrame);
    }
    else
        ASMAtomicAndU32(&pThis->intr_status, ~EHCI_STATUS_ASYNC_SCHED);

    /*
     * ASSUME the guest won't be very upset if a TD is completed, retired and handed
     * back immediately. The idea is to be able to retire the data and/or status stages
     * of a control transfer together with the setup stage, thus saving a frame. This
     * behaviour is should be perfectly ok, since the setup (and maybe data) stages
     * have already taken at least one frame to complete.
     *
     * But, when implementing the first synchronous virtual USB devices, we'll have to
     * verify that the guest doesn't choke when having a TD returned in the same frame
     * as it was submitted.
     */

#ifdef LOG_ENABLED
    if (pThis->intr_status ^ status_old)
    {
        uint32_t val = pThis->intr_status;
        uint32_t chg = val ^ status_old; NOREF(chg);
        Log2Func(("HcCommandStatus=%#010x: %sHCR=%d %sCLF=%d %sBLF=%d %sOCR=%d %sSOC=%d\n",
              val,
              chg & RT_BIT(0) ? "*" : "", val & 1,
              chg & RT_BIT(1) ? "*" : "", (val >> 1) & 1,
              chg & RT_BIT(2) ? "*" : "", (val >> 2) & 1,
              chg & RT_BIT(3) ? "*" : "", (val >> 3) & 1,
              chg & (3<<16)? "*" : "", (val >> 16) & 3));
    }
#endif

    /*
     * Adjust the frame timer interval based on idle detection.
     */
    if (pThisCC->fIdle)
    {
        pThisCC->cIdleCycles++;

        /*
         * Set the new frame rate based on how long we've been idle.
         * Don't remain more than 2 seconds in each frame rate (except for lowest one).
         */
        /** @todo Experiment with these values. */
        if (pThisCC->cIdleCycles == 2 * pThisCC->uFrameRate)
        {
            if (pThisCC->uFrameRate > 500)
                uNewFrameRate = pThisCC->uFrameRate - 500;
            else
                uNewFrameRate = 50; /* Absolute minimum is 50 Hertz, i.e 20ms interval. */

            pThisCC->cIdleCycles = 1;
        }
    }
    else
    {
        if (pThisCC->cIdleCycles)
        {
            pThisCC->cIdleCycles = 0;
            uNewFrameRate      = pThisCC->uFrameRateDefault;
        }
    }
    if (uNewFrameRate != pThisCC->uFrameRate)
        ehciR3CalcTimerIntervals(pThis, pThisCC, uNewFrameRate);
}

/**
 * Updates the HcFmNumber and frame_index values. HcFmNumber contains the current USB
 * frame number, frame_idx is the current micro-frame. In other words,
 *
 *   HcFmNumber == frame_idx << EHCI_FRAME_INDEX_SHIFT
 */
static void ehciR3BumpFrameNumber(PPDMDEVINS pDevIns, PEHCI pThis)
{
    pThis->HcFmNumber = pThis->frame_idx;

    const uint32_t u32OldFmNumber = pThis->HcFmNumber;

    pThis->HcFmNumber += pThis->uFramesPerTimerCall;

    if ((u32OldFmNumber ^ pThis->HcFmNumber) & ~EHCI_FRINDEX_FRAME_INDEX_MASK)
    {
        Log2Func(("rollover!\n"));
        ehciR3SetInterrupt(pDevIns, pThis, EHCI_STATUS_FRAME_LIST_ROLLOVER);
    }

    pThis->frame_idx = pThis->HcFmNumber;

}

/**
 * @callback_method_impl{PFNPDMTHREADDEV, EHCI Frame Thread}
 */
static DECLCALLBACK(int) ehciR3ThreadFrame(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    PEHCI   pThis   = PDMDEVINS_2_DATA(pDevIns, PEHCI);
    PEHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PEHCICC);

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        int rc = VINF_SUCCESS;
        while (   !ASMAtomicReadBool(&pThis->fBusStarted)
               && pThread->enmState == PDMTHREADSTATE_RUNNING)
        {
            /* Make sure the SCHED status bits are clear. */
            ASMAtomicAndU32(&pThis->intr_status, ~EHCI_STATUS_PERIOD_SCHED);
            ASMAtomicAndU32(&pThis->intr_status, ~EHCI_STATUS_ASYNC_SCHED);

            /* Signal the waiter that we are stopped now. */
            rc = RTSemEventMultiSignal(pThisCC->hSemEventFrameStopped);
            AssertRC(rc);

            rc = RTSemEventMultiWait(pThisCC->hSemEventFrame, RT_INDEFINITE_WAIT);
            RTSemEventMultiReset(pThisCC->hSemEventFrame);
        }

        AssertLogRelMsgReturn(RT_SUCCESS(rc) || rc == VERR_TIMEOUT, ("%Rrc\n", rc), rc);
        if (RT_UNLIKELY(pThread->enmState != PDMTHREADSTATE_RUNNING))
            break;

        uint64_t const tsNanoStart = RTTimeNanoTS();

        RTCritSectEnter(&pThisCC->CritSect);

        /* Reset idle detection flag */
        pThisCC->fIdle = true;

        /* Frame boundary, so do EOF stuff here */
        ehciR3StartOfFrame(pDevIns, pThis, pThisCC);

        /* Start the next frame */
        ehciR3BumpFrameNumber(pDevIns, pThis);

        RTCritSectLeave(&pThisCC->CritSect);

        /* Wait for the next round. */
        uint64_t nsWait = (RTTimeNanoTS() + pThisCC->nsWait) - tsNanoStart;

        rc = RTSemEventMultiWaitEx(pThisCC->hSemEventFrame, RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_UNINTERRUPTIBLE,
                                   nsWait);
        AssertLogRelMsg(RT_SUCCESS(rc) || rc == VERR_TIMEOUT, ("%Rrc\n", rc));
        RTSemEventMultiReset(pThisCC->hSemEventFrame);
    }

    return VINF_SUCCESS;
}

/**
 * Unblock the framer thread so it can respond to a state change.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThread     The send thread.
 */
static DECLCALLBACK(int) ehciR3ThreadFrameWakeup(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    PEHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PEHCICC);
    RT_NOREF(pThread);
    return RTSemEventMultiSignal(pThisCC->hSemEventFrame);
}

/**
 * Start sending SOF tokens across the USB bus, lists are processed in next frame.
 */
static void ehciR3BusStart(PPDMDEVINS pDevIns, PEHCI pThis, PEHCICC pThisCC)
{
    pThisCC->RootHub.pIRhConn->pfnPowerOn(pThisCC->RootHub.pIRhConn);
    ehciR3BumpFrameNumber(pDevIns, pThis);

    LogFunc(("Bus started\n"));

    ASMAtomicAndU32(&pThis->intr_status, ~EHCI_STATUS_HCHALTED);
    pThis->SofTime = PDMDevHlpTMTimeVirtGet(pDevIns) - pThisCC->cTicksPerFrame;
    bool fBusActive = ASMAtomicXchgBool(&pThis->fBusStarted, true);
    if (!fBusActive)
        RTSemEventMultiSignal(pThisCC->hSemEventFrame);
}

/**
 * Stop sending SOF tokens on the bus
 */
static void ehciR3BusStop(PEHCI pThis, PEHCICC pThisCC)
{
    LogFunc(("\n"));
    bool fBusActive = ASMAtomicXchgBool(&pThis->fBusStarted, false);
    if (fBusActive)
    {
        int rc = RTSemEventMultiReset(pThisCC->hSemEventFrameStopped);
        AssertRC(rc);

        /* Signal the frame thread to stop. */
        RTSemEventMultiSignal(pThisCC->hSemEventFrame);

        /* Wait for signal from the thread that it stopped. */
        rc = RTSemEventMultiWait(pThisCC->hSemEventFrameStopped, RT_INDEFINITE_WAIT);
        AssertRC(rc);
    }
    pThisCC->RootHub.pIRhConn->pfnPowerOff(pThisCC->RootHub.pIRhConn);
    ASMAtomicOrU32(&pThis->intr_status, EHCI_STATUS_HCHALTED);
}

/**
 * Power a port up or down
 */
static void ehciR3PortPower(PEHCI pThis, PEHCICC pThisCC, unsigned iPort, bool fPowerUp)
{
    bool fOldPPS = !!(pThis->RootHub.aPorts[iPort].fReg & EHCI_PORT_POWER);
    if (fPowerUp)
    {
        Log2Func(("port %d UP\n", iPort));
        /* power up */
        if (pThisCC->RootHub.aPorts[iPort].fAttached)
            ASMAtomicOrU32(&pThis->RootHub.aPorts[iPort].fReg, EHCI_PORT_CURRENT_CONNECT);
        if (pThis->RootHub.aPorts[iPort].fReg & EHCI_PORT_CURRENT_CONNECT)
            ASMAtomicOrU32(&pThis->RootHub.aPorts[iPort].fReg, EHCI_PORT_POWER);
        if (pThisCC->RootHub.aPorts[iPort].fAttached && !fOldPPS)
            VUSBIRhDevPowerOn(pThisCC->RootHub.pIRhConn, EHCI_PORT_2_VUSB_PORT(iPort));
    }
    else
    {
        Log2(("Func port %d DOWN\n", iPort));
        /* power down */
        ASMAtomicAndU32(&pThis->RootHub.aPorts[iPort].fReg, ~(EHCI_PORT_POWER|EHCI_PORT_CURRENT_CONNECT));
        if (pThisCC->RootHub.aPorts[iPort].fAttached && fOldPPS)
            VUSBIRhDevPowerOff(pThisCC->RootHub.pIRhConn, EHCI_PORT_2_VUSB_PORT(iPort));
    }
}

/**
 * Completion callback for the VUSBIDevReset() operation.
 * @thread EMT.
 */
static void ehciR3PortResetDone(PEHCI pThis, PEHCICC pThisCC, uint32_t uPort, int rc)
{
    Log2Func(("rc=%Rrc\n", rc));
    Assert(uPort >= 1);
    unsigned iPort = uPort - 1;

    if (RT_SUCCESS(rc))
    {
        /*
         * Successful reset.
         */
        Log2Func(("Reset completed.\n"));
        /* Note: XP relies on us clearing EHCI_PORT_CONNECT_CHANGE */
        ASMAtomicAndU32(&pThis->RootHub.aPorts[iPort].fReg, ~(EHCI_PORT_RESET | EHCI_PORT_SUSPEND | EHCI_PORT_CONNECT_CHANGE));
        ASMAtomicOrU32(&pThis->RootHub.aPorts[iPort].fReg, EHCI_PORT_PORT_ENABLED);
    }
    else
    {
        /* desperate measures. */
        if (   pThisCC->RootHub.aPorts[iPort].fAttached
            && VUSBIRhDevGetState(pThisCC->RootHub.pIRhConn, uPort) == VUSB_DEVICE_STATE_ATTACHED)
        {
            /*
             * Damn, something weird happend during reset. We'll pretend the user did an
             * incredible fast reconnect or something. (prolly not gonna work)
             */
            Log2Func(("The reset failed (rc=%Rrc)!!! Pretending reconnect at the speed of light.\n", rc));
            ASMAtomicOrU32(&pThis->RootHub.aPorts[iPort].fReg, EHCI_PORT_CURRENT_CONNECT | EHCI_PORT_CONNECT_CHANGE);
        }
        else
        {
            /*
             * The device has / will be disconnected.
             */
            Log2Func(("Disconnected (rc=%Rrc)!!!\n", rc));
            ASMAtomicAndU32(&pThis->RootHub.aPorts[iPort].fReg, ~(EHCI_PORT_RESET | EHCI_PORT_SUSPEND));
            ASMAtomicOrU32(&pThis->RootHub.aPorts[iPort].fReg, EHCI_PORT_CONNECT_CHANGE);
        }
    }
}

/**
 * Sets a flag in a port status register but only set it if a device is
 * connected, if not set ConnectStatusChange flag to force HCD to reevaluate
 * connect status.
 *
 * @returns true if device was connected and the flag was cleared.
 */
static bool ehciR3RhPortSetIfConnected(PEHCIROOTHUB pRh, int iPort, uint32_t fValue)
{
    /*
     * Writing a 0 has no effect
     */
    if (fValue == 0)
        return false;

    /*
     * The port might be still/already disconnected.
     */
    if (!(pRh->aPorts[iPort].fReg & EHCI_PORT_CURRENT_CONNECT))
        return false;

    bool fRc = !(pRh->aPorts[iPort].fReg & fValue);

    /* set the bit */
    ASMAtomicOrU32(&pRh->aPorts[iPort].fReg, fValue);

    return fRc;
}

#endif /* IN_RING3 */


/**
 * Read the USBCMD register of the host controller.
 */
static VBOXSTRICTRC HcCommand_r(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    /* Signal the async advance doorbell interrupt (if required)
     * XP polls the command register to see when it can queue up more TDs.
     */
    if (    (pThis->cmd & EHCI_CMD_INT_ON_ADVANCE_DOORBELL))
//        &&  !pThis->cInFlight)
    {
        int rc = ehciSetInterrupt(pDevIns, pThis, VINF_IOM_R3_MMIO_READ, EHCI_STATUS_INT_ON_ASYNC_ADV);
        if (rc != VINF_SUCCESS)
            return rc;
    }

    *pu32Value = pThis->cmd;
    RT_NOREF(iReg);
    return VINF_SUCCESS;
}

/**
 * Write to the USBCMD register of the host controller.
 */
static VBOXSTRICTRC HcCommand_w(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t val)
{
#ifdef IN_RING3
    PEHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PEHCICC);
#endif
    RT_NOREF(pDevIns, iReg);

#ifdef LOG_ENABLED
    Log(("HcCommand_w old=%x new=%x\n", pThis->cmd, val));
    if (val & EHCI_CMD_RUN)
        Log(("    CMD_RUN\n"));
    if (val & EHCI_CMD_RESET)
        Log(("    CMD_RESET\n"));
    if (val & EHCI_CMD_PERIODIC_SCHED_ENABLE)
        Log(("    CMD_PERIODIC_SCHED_ENABLE\n"));
    if (val & EHCI_CMD_ASYNC_SCHED_ENABLE)
        Log(("    CMD_ASYNC_SCHED_ENABLE\n"));
    if (val & EHCI_CMD_INT_ON_ADVANCE_DOORBELL)
        Log(("    CMD_INT_ON_ADVANCE_DOORBELL\n"));
    if (val & EHCI_CMD_SOFT_RESET)
        Log(("    CMD_SOFT_RESET\n"));
    if (val & EHCI_CMD_ASYNC_SCHED_PARK_ENABLE)
        Log(("    CMD_ASYNC_SCHED_PARK_ENABLE\n"));

    Log(("    CMD_FRAME_LIST_SIZE              %d\n", (val & EHCI_CMD_FRAME_LIST_SIZE_MASK) >> EHCI_CMD_FRAME_LIST_SIZE_SHIFT));
    Log(("    CMD_ASYNC_SCHED_PARK_MODE_COUNT  %d\n", (val & EHCI_CMD_ASYNC_SCHED_PARK_MODE_COUNT_MASK) >> EHCI_CMD_ASYNC_SCHED_PARK_MODE_COUNT_SHIFT));
    Log(("    CMD_INTERRUPT_THRESHOLD          %d\n", (val & EHCI_CMD_INTERRUPT_THRESHOLD_MASK) >> EHCI_CMD_INTERRUPT_THRESHOLD_SHIFT));
#endif

    Assert(!(pThis->hcc_params & EHCI_HCC_PARAMS_PROGRAMMABLE_FRAME_LIST)); /* hardcoded assumptions about list size */
    if (!(pThis->hcc_params & EHCI_HCC_PARAMS_PROGRAMMABLE_FRAME_LIST))
    {
        if (val & EHCI_CMD_FRAME_LIST_SIZE_MASK)
            Log(("Trying to change the frame list size to %d even though it's hardcoded at 1024 elements!!\n", (val & EHCI_CMD_FRAME_LIST_SIZE_MASK) >> EHCI_CMD_FRAME_LIST_SIZE_SHIFT));

        val &= ~EHCI_CMD_FRAME_LIST_SIZE_MASK;  /* 00 = 1024 */
    }
    if (val & ~EHCI_CMD_MASK)
        Log(("Unknown bits %#x are set!!!\n", val & ~0x0003000f));

    uint32_t old_cmd = pThis->cmd;
#ifdef IN_RING3
    pThis->cmd = val;
#endif

    if (val & EHCI_CMD_RESET)
    {
#ifdef IN_RING3
        LogRel(("EHCI: Hardware reset\n"));
        ehciR3DoReset(pDevIns, pThis, pThisCC, EHCI_USB_RESET, true /* reset devices */);
#else
        return VINF_IOM_R3_MMIO_WRITE;
#endif
    }
    else if (val & EHCI_CMD_SOFT_RESET)
    {
#ifdef IN_RING3
        LogRel(("EHCI: Software reset\n"));
        ehciR3DoReset(pDevIns, pThis, pThisCC, EHCI_USB_SUSPEND, false /* N/A */);
#else
        return VINF_IOM_R3_MMIO_WRITE;
#endif
    }
    else
    {
        /* see what changed and take action on that. */
        uint32_t old_state = old_cmd & EHCI_CMD_RUN;
        uint32_t new_state = val     & EHCI_CMD_RUN;

        if (old_state != new_state)
        {
#ifdef IN_RING3
            switch (new_state)
            {
                case EHCI_CMD_RUN:
                    LogRel(("EHCI: USB Operational\n"));
                    ehciR3BusStart(pDevIns, pThis, pThisCC);
                    break;
                case 0:
                    ehciR3BusStop(pThis, pThisCC);
                    LogRel(("EHCI: USB Suspended\n"));
                    break;
            }
#else
            return VINF_IOM_R3_MMIO_WRITE;
#endif
        }
    }
#ifndef IN_RING3
    pThis->cmd = val;
#endif
    return VINF_SUCCESS;
}

/**
 * Read the USBSTS register of the host controller.
 */
static VBOXSTRICTRC HcStatus_r(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
#ifdef LOG_ENABLED
    Log(("HcStatus_r current value %x\n", pThis->intr_status));
    if (pThis->intr_status & EHCI_STATUS_ASYNC_SCHED)
        Log(("    STATUS_ASYNC_SCHED\n"));
    if (pThis->intr_status & EHCI_STATUS_PERIOD_SCHED)
        Log(("    STATUS_PERIOD_SCHED\n"));
    if (pThis->intr_status & EHCI_STATUS_RECLAMATION)
        Log(("    STATUS_RECLAMATION\n"));
    if (pThis->intr_status & EHCI_STATUS_HCHALTED)
        Log(("    STATUS_HCHALTED\n"));
    if (pThis->intr_status & EHCI_STATUS_INT_ON_ASYNC_ADV)
        Log(("    STATUS_INT_ON_ASYNC_ADV\n"));
    if (pThis->intr_status & EHCI_STATUS_HOST_SYSTEM_ERROR)
        Log(("    STATUS_HOST_SYSTEM_ERROR\n"));
    if (pThis->intr_status & EHCI_STATUS_FRAME_LIST_ROLLOVER)
        Log(("    STATUS_FRAME_LIST_ROLLOVER\n"));
    if (pThis->intr_status & EHCI_STATUS_PORT_CHANGE_DETECT)
        Log(("    STATUS_PORT_CHANGE_DETECT\n"));
    if (pThis->intr_status & EHCI_STATUS_ERROR_INT)
        Log(("    STATUS_ERROR_INT\n"));
    if (pThis->intr_status & EHCI_STATUS_THRESHOLD_INT)
        Log(("    STATUS_THRESHOLD_INT\n"));
#endif
    *pu32Value = pThis->intr_status;
    return VINF_SUCCESS;
}

/**
 * Write to the USBSTS register of the host controller.
 */
static VBOXSTRICTRC HcStatus_w(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(iReg);
    VBOXSTRICTRC rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CsIrq, VINF_IOM_R3_MMIO_WRITE);
    if (rc != VINF_SUCCESS)
        return rc;

#ifdef LOG_ENABLED
    Log(("HcStatus_w current value %x; new %x\n", pThis->intr_status, val));
    if (val & EHCI_STATUS_ASYNC_SCHED)
        Log(("    STATUS_ASYNC_SCHED\n"));
    if (val & EHCI_STATUS_PERIOD_SCHED)
        Log(("    STATUS_PERIOD_SCHED\n"));
    if (val & EHCI_STATUS_RECLAMATION)
        Log(("    STATUS_RECLAMATION\n"));
    if (val & EHCI_STATUS_HCHALTED)
        Log(("    STATUS_HCHALTED\n"));
    if (val & EHCI_STATUS_INT_ON_ASYNC_ADV)
        Log(("    STATUS_INT_ON_ASYNC_ADV\n"));
    if (val & EHCI_STATUS_HOST_SYSTEM_ERROR)
        Log(("    STATUS_HOST_SYSTEM_ERROR\n"));
    if (val & EHCI_STATUS_FRAME_LIST_ROLLOVER)
        Log(("    STATUS_FRAME_LIST_ROLLOVER\n"));
    if (val & EHCI_STATUS_PORT_CHANGE_DETECT)
        Log(("    STATUS_PORT_CHANGE_DETECT\n"));
    if (val & EHCI_STATUS_ERROR_INT)
        Log(("    STATUS_ERROR_INT\n"));
    if (val & EHCI_STATUS_THRESHOLD_INT)
        Log(("    STATUS_THRESHOLD_INT\n"));
#endif
    if (    (val & ~EHCI_STATUS_INTERRUPT_MASK)
        &&  val != 0xffffffff   /* ignore clear-all-like requests from xp. */)
        Log(("Unknown bits %#x are set!!!\n", val & ~EHCI_STATUS_INTERRUPT_MASK));

    /* Some bits are read-only */
    val &= EHCI_STATUS_INTERRUPT_MASK;

    /* "The Host Controller Driver may clear specific bits in this
     * register by writing '1' to bit positions to be cleared"
     */
    ASMAtomicAndU32(&pThis->intr_status, ~val);
    ehciUpdateInterruptLocked(pDevIns, pThis, "HcStatus_w");

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CsIrq);
    return VINF_SUCCESS;
}

/**
 * Read the USBINTR register of the host controller.
 */
static VBOXSTRICTRC HcInterruptEnable_r(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    *pu32Value = pThis->intr;
    return VINF_SUCCESS;
}

/**
 * Write to the USBINTR register of the host controller.
 */
static VBOXSTRICTRC HcInterruptEnable_w(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(iReg);
#ifdef LOG_ENABLED
    Log(("HcInterruptEnable_w -> new value %x\n", val));
    if (val & EHCI_INTR_ENABLE_THRESHOLD)
        Log(("    INTR_ENABLE_THRESHOLD\n"));
    if (val & EHCI_INTR_ENABLE_ERROR)
        Log(("    INTR_ENABLE_ERROR\n"));
    if (val & EHCI_INTR_ENABLE_PORT_CHANGE)
        Log(("    INTR_ENABLE_PORT_CHANGE\n"));
    if (val & EHCI_INTR_ENABLE_FRAME_LIST_ROLLOVER)
        Log(("    INTR_ENABLE_FRAME_LIST_ROLLOVER\n"));
    if (val & EHCI_INTR_ENABLE_HOST_SYSTEM_ERROR)
        Log(("    INTR_ENABLE_HOST_SYSTEM_ERROR\n"));
    if (val & EHCI_INTR_ENABLE_ASYNC_ADVANCE)
        Log(("    INTR_ENABLE_ASYNC_ADVANCE\n"));
    if (val & ~EHCI_INTR_ENABLE_MASK)
        Log(("    Illegal bits set %x!!\n", val & ~EHCI_INTR_ENABLE_MASK));
#endif
    VBOXSTRICTRC rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CsIrq, VINF_IOM_R3_MMIO_WRITE);
    if (rc == VINF_SUCCESS)
    {
        pThis->intr = val & EHCI_INTR_ENABLE_MASK;
        ehciUpdateInterruptLocked(pDevIns, pThis, "HcInterruptEnable_w");
        PDMDevHlpCritSectLeave(pDevIns, &pThis->CsIrq);
    }
    return rc;
}

/**
 * Read the FRINDEX register of the host controller.
 */
static VBOXSTRICTRC HcFrameIndex_r(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    Log2Func(("current frame %x\n", pThis->frame_idx));
    *pu32Value = pThis->frame_idx;
    return VINF_SUCCESS;
}

/**
 * Write to the FRINDEX register of the host controller.
 */
static VBOXSTRICTRC HcFrameIndex_w(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);
    LogFunc(("pThis->frame_idx new index=%x\n", val));
    if (!(pThis->intr_status & EHCI_STATUS_HCHALTED))
        Log(("->>Updating the frame index while the controller is running!!!\n"));

    ASMAtomicXchgU32(&pThis->frame_idx, val);
    return VINF_SUCCESS;
}

/**
 * Read the CTRLDSSEGMENT register of the host controller.
 */
static VBOXSTRICTRC HcControlDSSeg_r(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    if (pThis->hcc_params & EHCI_HCC_PARAMS_64BITS_ADDRESSING)
        *pu32Value = pThis->ds_segment;
    else
        *pu32Value = 0;

    return VINF_SUCCESS;
}

/**
 * Write to the CTRLDSSEGMENT register of the host controller.
 */
static VBOXSTRICTRC HcControlDSSeg_w(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);
    LogFunc(("new base %x\n", val));
    if (pThis->hcc_params & EHCI_HCC_PARAMS_64BITS_ADDRESSING)
        ASMAtomicXchgU32(&pThis->ds_segment, val);

    return VINF_SUCCESS;
}

/**
 * Read the PERIODICLISTBASE register of the host controller.
 */
static VBOXSTRICTRC HcPeriodicListBase_r(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    Log2Func(("current base %x\n", pThis->periodic_list_base));
    *pu32Value = pThis->periodic_list_base;
    return VINF_SUCCESS;
}

/**
 * Write to the PERIODICLISTBASE register of the host controller.
 */
static VBOXSTRICTRC HcPeriodicListBase_w(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);
    LogFunc(("new base %x\n", val));
    if (val & ~EHCI_PERIODIC_LIST_MASK)
        Log(("->> Base not aligned on a 4kb boundary!!!!\n"));
    if (    !(pThis->intr_status & EHCI_STATUS_HCHALTED)
        &&  (pThis->cmd & EHCI_CMD_PERIODIC_SCHED_ENABLE))
        Log(("->>Updating the periodic list base while the controller is running!!!\n"));

    ASMAtomicXchgU32(&pThis->periodic_list_base, val & EHCI_PERIODIC_LIST_MASK);
    return VINF_SUCCESS;
}

/**
 * Read the ASYNCLISTADDR register of the host controller.
 */
static VBOXSTRICTRC HcAsyncListAddr_r(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    Log2Func(("current base %x\n", pThis->async_list_base));
    *pu32Value = pThis->async_list_base;
    return VINF_SUCCESS;
}

/**
 * Write to the ASYNCLISTADDR register of the host controller.
 */
static VBOXSTRICTRC HcAsyncListAddr_w(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);
    LogFunc(("new address %x\n", val));
    if (val & ~EHCI_ASYNC_LIST_MASK)
        Log(("->> Base not aligned on a 32-byte boundary!!!!\n"));
    if (    !(pThis->intr_status & EHCI_STATUS_HCHALTED)
        &&  (pThis->cmd & EHCI_CMD_ASYNC_SCHED_ENABLE))
        Log(("->>Updating the asynchronous list address while the controller is running!!!\n"));

    ASMAtomicXchgU32(&pThis->async_list_base, val & EHCI_ASYNC_LIST_MASK);
    return VINF_SUCCESS;
}

/**
 * Read the CONFIGFLAG register of the host controller.
 */
static VBOXSTRICTRC HcConfigFlag_r(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    Log2Func(("current config=%x\n", pThis->config));
    *pu32Value = pThis->config;
    return VINF_SUCCESS;
}

/**
 * Write to the CONFIGFLAG register of the host controller.
 */
static VBOXSTRICTRC HcConfigFlag_w(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);
    LogFunc(("new configuration routing %x\n", val & EHCI_CONFIGFLAG_ROUTING));
    pThis->config = val & EHCI_CONFIGFLAG_MASK;
    return VINF_SUCCESS;
}

/**
 * Read the PORTSC register of a port
 */
static VBOXSTRICTRC HcPortStatusCtrl_r(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    const unsigned  i = iReg - 1;
    PEHCIHUBPORT    p = &pThis->RootHub.aPorts[i];
    RT_NOREF(pDevIns);

    Assert(!(pThis->hcs_params & EHCI_HCS_PARAMS_PORT_POWER_CONTROL));

    if (p->fReg & EHCI_PORT_RESET)
    {
#ifdef IN_RING3
        Log2Func(("port %u: Impatient guest!\n", i));
        RTThreadYield();
#else
        Log2Func(("yield -> VINF_IOM_R3_MMIO_READ\n"));
        return VINF_IOM_R3_MMIO_READ;
#endif
    }

    *pu32Value = p->fReg;
    return VINF_SUCCESS;
}

/**
 * Write to the PORTSC register of a port
 */
static VBOXSTRICTRC HcPortStatusCtrl_w(PPDMDEVINS pDevIns, PEHCI pThis, uint32_t iReg, uint32_t val)
{
    const unsigned  i = iReg - 1;
    PEHCIHUBPORT    pPort = &pThis->RootHub.aPorts[i];

    if (    pPort->fReg == val
        &&  !(val & EHCI_PORT_CHANGE_MASK))
        return VINF_SUCCESS;

#ifdef IN_RING3
    PEHCICC         pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PEHCICC);

    LogFunc(("port %d: old=%x new=%x\n", i, pPort->fReg, val));
    Assert(!(pThis->hcs_params & EHCI_HCS_PARAMS_PORT_POWER_CONTROL));
    Assert(pPort->fReg & EHCI_PORT_POWER);

    if (val & EHCI_PORT_RESERVED)
        Log(("Invalid bits set %x!!!\n", val & EHCI_PORT_RESERVED));

    /* Write to clear any of the change bits: EHCI_PORT_CONNECT_CHANGE, EHCI_PORT_PORT_CHANGE and EHCI_PORT_OVER_CURRENT_CHANGE */
    if (val & EHCI_PORT_CHANGE_MASK)
    {
        ASMAtomicAndU32(&pPort->fReg, ~(val & EHCI_PORT_CHANGE_MASK));
        /* XP seems to need this after device detach */
        if (!(pPort->fReg & EHCI_PORT_CURRENT_CONNECT))
            ASMAtomicAndU32(&pPort->fReg, ~EHCI_PORT_CONNECT_CHANGE);
    }

    /* Writing the Port Enable/Disable bit as 1 has no effect; software cannot enable
     * the port that way. Writing the bit as zero does disable the port, but does not
     * set the corresponding 'changed' bit or trigger an interrupt.
     */
    if (!(val & EHCI_PORT_PORT_ENABLED) && (pPort->fReg & EHCI_PORT_PORT_ENABLED))
    {
        ASMAtomicAndU32(&pPort->fReg, ~EHCI_PORT_PORT_ENABLED);
        LogFunc(("port %u: DISABLE\n", i));
    }

    if (val & EHCI_PORT_SUSPEND)
        LogFunc(("port %u: SUSPEND - not implemented correctly!!!\n", i));

    if (val & EHCI_PORT_RESET)
    {
        Log2Func(("Reset port\n"));
        if ( ehciR3RhPortSetIfConnected(&pThis->RootHub, i, val & EHCI_PORT_RESET) )
        {
            PVM pVM = PDMDevHlpGetVM(pDevIns);
            VUSBIRhDevReset(pThisCC->RootHub.pIRhConn, EHCI_PORT_2_VUSB_PORT(i), false /* don't reset on linux */, NULL /* sync */, pThis, pVM);
            ehciR3PortResetDone(pThis, pThisCC, EHCI_PORT_2_VUSB_PORT(i), VINF_SUCCESS);
        }
        else if (pPort->fReg & EHCI_PORT_RESET)
        {
            /* the guest is getting impatient. */
            Log2Func(("port %u: Impatient guest!\n", i));
            RTThreadYield();
        }
    }

    /* EHCI_PORT_POWER ignored as we don't support this in HCS_PARAMS */
    /* EHCI_PORT_INDICATOR ignored as we don't support this in HCS_PARAMS */
    /* EHCI_PORT_TEST_CONTROL_MASK ignored */
    ASMAtomicAndU32(&pPort->fReg, ~EHCI_PORT_WAKE_MASK);
    ASMAtomicOrU32(&pPort->fReg, (val & EHCI_PORT_WAKE_MASK));
    return VINF_SUCCESS;

#else  /* !IN_RING3 */
    RT_NOREF(pDevIns);
    return VINF_IOM_R3_MMIO_WRITE;
#endif /* !IN_RING3 */
}

/**
 * Register descriptor table
 */
static const EHCIOPREG g_aOpRegs[] =
{
    {"HcCommand" ,          HcCommand_r,            HcCommand_w},
    {"HcStatus",            HcStatus_r,             HcStatus_w},
    {"HcInterruptEnable",   HcInterruptEnable_r,    HcInterruptEnable_w},
    {"HcFrameIndex",        HcFrameIndex_r,         HcFrameIndex_w},
    {"HcControlDSSeg",      HcControlDSSeg_r,       HcControlDSSeg_w},
    {"HcPeriodicListBase",  HcPeriodicListBase_r,   HcPeriodicListBase_w},
    {"HcAsyncListAddr",     HcAsyncListAddr_r,      HcAsyncListAddr_w}
};

/**
 * Register descriptor table 2
 * (Starting at offset 0x40)
 */
static const EHCIOPREG g_aOpRegs2[] =
{
    {"HcConfigFlag",        HcConfigFlag_r,         HcConfigFlag_w},

    /* The number of port status register depends on the definition
     * of EHCI_NDP_MAX macro
     */
    {"HcPortStatusCtrl[0]",  HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
    {"HcPortStatusCtrl[1]",  HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
    {"HcPortStatusCtrl[2]",  HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
    {"HcPortStatusCtrl[3]",  HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
    {"HcPortStatusCtrl[4]",  HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
    {"HcPortStatusCtrl[5]",  HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
    {"HcPortStatusCtrl[6]",  HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
    {"HcPortStatusCtrl[7]",  HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
    {"HcPortStatusCtrl[8]",  HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
    {"HcPortStatusCtrl[9]",  HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
    {"HcPortStatusCtrl[10]", HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
    {"HcPortStatusCtrl[11]", HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
    {"HcPortStatusCtrl[12]", HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
    {"HcPortStatusCtrl[13]", HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
    {"HcPortStatusCtrl[14]", HcPortStatusCtrl_r,     HcPortStatusCtrl_w},
};

/* Quick way to determine how many op regs are valid. Since at least one port must
 * be configured (and no more than 15), there will be between 2 and 16 registers.
 */
#define NUM_OP_REGS2(pehci) (1 + EHCI_NDP_CFG(pehci))

AssertCompile(RT_ELEMENTS(g_aOpRegs2) > 1);
AssertCompile(RT_ELEMENTS(g_aOpRegs2) <= 16);


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE, Write to a MMIO register. }
 *
 * @note We only accept 32-bit reads that are 32-bit aligned.
 */
static DECLCALLBACK(VBOXSTRICTRC) ehciMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PEHCI pThis = PDMDEVINS_2_DATA(pDevIns, PEHCI);
    RT_NOREF(pvUser);

    Log2Func(("%RGp size=%d\n", off, cb));

    if (off < EHCI_CAPS_REG_SIZE)
    {
        switch (off)
        {
            case 0x0:   /* CAPLENGTH */
                /* read CAPLENGTH + HCIVERSION in one go */
                if (cb == 4)
                {
                    *(uint32_t *)pv = (pThis->hci_version << 16) | pThis->cap_length;
                    return VINF_SUCCESS;
                }

                AssertReturn(cb == 1, VINF_IOM_MMIO_UNUSED_FF);
                *(uint8_t *)pv = pThis->cap_length;
                break;

            case 0x2:   /* HCIVERSION */
                AssertReturn(cb == 2, VINF_IOM_MMIO_UNUSED_FF);
                *(uint16_t *)pv = pThis->hci_version;
                break;

            case 0x4:   /* HCSPARAMS (structural) */
                AssertReturn(cb == 4, VINF_IOM_MMIO_UNUSED_FF);
                *(uint32_t *)pv = pThis->hcs_params;
                break;

            case 0x8:   /* HCCPARAMS (caps) */
                AssertReturn(cb == 4, VINF_IOM_MMIO_UNUSED_FF);
                *(uint32_t *)pv = pThis->hcc_params;
                break;

            case 0x9:   /* one byte HCIPARAMS read (XP; EHCI extended capability offset) */
                AssertReturn(cb == 1, VINF_IOM_MMIO_UNUSED_FF);
                *(uint8_t *)pv = (uint8_t)(pThis->hcc_params >> 8);
                break;

            case 0xC:   /* HCSP-PORTROUTE (60 bits) */
            case 0x10:
                AssertReturn(cb == 4, VINF_IOM_MMIO_UNUSED_FF);
                *(uint32_t *)pv = 0;
                break;

            default:
                LogFunc(("Trying to read register %#x!!!\n", off));
                return VINF_IOM_MMIO_UNUSED_FF;
        }
        Log2Func(("%RGp size=%d -> val=%x\n", off, cb, *(uint32_t *)pv));
        return VINF_SUCCESS;
    }

    /*
     * Validate the access.
     */
    if (cb != sizeof(uint32_t))
    {
        Log2Func(("Bad read size!!! off=%RGp cb=%d\n", off, cb));
        return VINF_IOM_MMIO_UNUSED_FF; /* No idea what really would happen... */
    }
    if (off & 0x3)
    {
        Log2Func(("Unaligned read!!! off=%RGp cb=%d\n", off, cb));
        return VINF_IOM_MMIO_UNUSED_FF;
    }

    /*
     * Validate the register and call the read operator.
     */
    VBOXSTRICTRC rc;
    uint32_t iReg = (off - pThis->cap_length) >> 2;
    if (iReg < RT_ELEMENTS(g_aOpRegs))
    {
        const EHCIOPREG *pReg = &g_aOpRegs[iReg];
        rc = pReg->pfnRead(pDevIns, pThis, iReg, (uint32_t *)pv);
        Log2Func(("%RGp size=%d -> val=%x (rc=%d)\n", off, cb, *(uint32_t *)pv, VBOXSTRICTRC_VAL(rc)));
    }
    else if (iReg >= 0x10) /* 0x40 */
    {
        iReg -= 0x10;
        if (iReg < NUM_OP_REGS2(pThis))
        {
            const EHCIOPREG *pReg = &g_aOpRegs2[iReg];
            rc = pReg->pfnRead(pDevIns, pThis, iReg, (uint32_t *)pv);
            Log2Func(("%RGp size=%d -> val=%x (rc=%d)*\n", off, cb, *(uint32_t *)pv, VBOXSTRICTRC_VAL(rc)));
        }
        else
        {
            LogFunc(("Trying to read register %u/%u!!!\n", iReg, NUM_OP_REGS2(pThis)));
            rc = VINF_IOM_MMIO_UNUSED_FF;
        }
    }
    else
    {
        LogFunc(("Trying to read register %u/%u (2)!!!\n", iReg, RT_ELEMENTS(g_aOpRegs)));
        rc = VINF_IOM_MMIO_UNUSED_FF;
    }
    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE, Write to a MMIO register. }
 *
 * @note We only accept 32-bit writes that are 32-bit aligned.
 */
static DECLCALLBACK(VBOXSTRICTRC) ehciMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PEHCI pThis = PDMDEVINS_2_DATA(pDevIns, PEHCI);
    RT_NOREF(pvUser);

    Log2Func(("%RGp %x size=%d\n", off, *(uint32_t *)pv, cb));

    if (off < EHCI_CAPS_REG_SIZE)
    {
        /* These are read-only */
        LogFunc(("Trying to write to register %#x!!!\n", off));
        return VINF_SUCCESS;
    }

    /*
     * Validate the access.
     */
    if (cb != sizeof(uint32_t))
    {
        Log2Func(("Bad write size!!! off=%RGp cb=%d\n", off, cb));
        return VINF_SUCCESS;
    }
    if (off & 0x3)
    {
        Log2Func(("Unaligned write!!! off=%RGp cb=%d\n", off, cb));
        return VINF_SUCCESS;
    }

    /*
     * Validate the register and call the read operator.
     */
    VBOXSTRICTRC rc;
    uint32_t iReg = (off - pThis->cap_length) >> 2;
    if (iReg < RT_ELEMENTS(g_aOpRegs))
    {
        const EHCIOPREG *pReg = &g_aOpRegs[iReg];
        rc = pReg->pfnWrite(pDevIns, pThis, iReg, *(uint32_t *)pv);
    }
    else if (iReg >= 0x10) /* 0x40 */
    {
        iReg -= 0x10;
        if (iReg < NUM_OP_REGS2(pThis))
        {
            const EHCIOPREG *pReg = &g_aOpRegs2[iReg];
            rc = pReg->pfnWrite(pDevIns, pThis, iReg, *(uint32_t *)pv);
        }
        else
        {
            LogFunc(("Trying to write to register %u/%u!!!\n", iReg, NUM_OP_REGS2(pThis)));
            rc = VINF_SUCCESS;  /* ignore the invalid write */
        }
    }
    else
    {
        LogFunc(("Trying to write to register %u/%u!!! (2)\n", iReg, RT_ELEMENTS(g_aOpRegs)));
        rc = VINF_SUCCESS;  /* ignore the invalid write */
    }
    return rc;
}

#ifdef IN_RING3

/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) ehciR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PEHCI pThis = PDMDEVINS_2_DATA(pDevIns, PEHCI);
    LogFlowFunc(("\n"));
    return pDevIns->pHlpR3->pfnSSMPutStructEx(pSSM, pThis, sizeof(*pThis), 0 /*fFlags*/, &g_aEhciFields[0], NULL);
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) ehciLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PEHCI           pThis = PDMDEVINS_2_DATA(pDevIns, PEHCI);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    int             rc;
    LogFlowFunc(("\n"));
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    if (uVersion == EHCI_SAVED_STATE_VERSION)
    {
        rc = pHlp->pfnSSMGetStructEx(pSSM, pThis, sizeof(*pThis), 0 /*fFlags*/, &g_aEhciFields[0], NULL);
        if (RT_FAILURE(rc))
            return rc;
    }
    else if (uVersion == EHCI_SAVED_STATE_VERSION_PRE_TIMER_REMOVAL)
    {
        static SSMFIELD const g_aEhciFieldsPreTimerRemoval[] =
        {
            SSMFIELD_ENTRY(         EHCI, fAsyncTraversalTimerActive),
            SSMFIELD_ENTRY(         EHCI, SofTime),
            SSMFIELD_ENTRY(         EHCI, RootHub.unused),
            SSMFIELD_ENTRY(         EHCI, RootHub.unused),
            SSMFIELD_ENTRY(         EHCI, RootHub.unused),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[0].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[1].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[2].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[3].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[4].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[5].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[6].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[7].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[8].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[9].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[10].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[11].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[12].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[13].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[14].fReg),
            SSMFIELD_ENTRY(         EHCI, cap_length),
            SSMFIELD_ENTRY(         EHCI, hci_version),
            SSMFIELD_ENTRY(         EHCI, hcs_params),
            SSMFIELD_ENTRY(         EHCI, hcc_params),
            SSMFIELD_ENTRY(         EHCI, cmd),
            SSMFIELD_ENTRY(         EHCI, intr_status),
            SSMFIELD_ENTRY(         EHCI, intr),
            SSMFIELD_ENTRY(         EHCI, frame_idx),
            SSMFIELD_ENTRY(         EHCI, ds_segment),
            SSMFIELD_ENTRY(         EHCI, periodic_list_base),
            SSMFIELD_ENTRY(         EHCI, async_list_base),
            SSMFIELD_ENTRY(         EHCI, config),
            SSMFIELD_ENTRY(         EHCI, uIrqInterval),
            SSMFIELD_ENTRY(         EHCI, HcFmNumber),
            SSMFIELD_ENTRY(         EHCI, uFramesPerTimerCall),
            SSMFIELD_ENTRY_TERM()
        };

        rc = pHlp->pfnSSMGetStructEx(pSSM, pThis, sizeof(*pThis), 0 /*fFlags*/, &g_aEhciFieldsPreTimerRemoval[0], NULL);
        if (RT_FAILURE(rc))
            return rc;
        AssertReturn(EHCI_NDP_CFG(pThis) <= EHCI_NDP_MAX, VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
    }
    else if (uVersion == EHCI_SAVED_STATE_VERSION_8PORTS)
    {
        static SSMFIELD const s_aEhciFields8Ports[] =
        {
            SSMFIELD_ENTRY(         EHCI, fAsyncTraversalTimerActive),
            SSMFIELD_ENTRY(         EHCI, SofTime),
            SSMFIELD_ENTRY(         EHCI, RootHub.unused),
            SSMFIELD_ENTRY(         EHCI, RootHub.unused),
            SSMFIELD_ENTRY(         EHCI, RootHub.unused),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[0].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[1].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[2].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[3].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[4].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[5].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[6].fReg),
            SSMFIELD_ENTRY(         EHCI, RootHub.aPorts[7].fReg),
            SSMFIELD_ENTRY(         EHCI, cap_length),
            SSMFIELD_ENTRY(         EHCI, hci_version),
            SSMFIELD_ENTRY(         EHCI, hcs_params),
            SSMFIELD_ENTRY(         EHCI, hcc_params),
            SSMFIELD_ENTRY(         EHCI, cmd),
            SSMFIELD_ENTRY(         EHCI, intr_status),
            SSMFIELD_ENTRY(         EHCI, intr),
            SSMFIELD_ENTRY(         EHCI, frame_idx),
            SSMFIELD_ENTRY(         EHCI, ds_segment),
            SSMFIELD_ENTRY(         EHCI, periodic_list_base),
            SSMFIELD_ENTRY(         EHCI, async_list_base),
            SSMFIELD_ENTRY(         EHCI, config),
            SSMFIELD_ENTRY(         EHCI, uIrqInterval),
            SSMFIELD_ENTRY(         EHCI, HcFmNumber),
            SSMFIELD_ENTRY(         EHCI, uFramesPerTimerCall),
            SSMFIELD_ENTRY_TERM()
        };

        rc = pHlp->pfnSSMGetStructEx(pSSM, pThis, sizeof(*pThis), 0 /*fFlags*/, &s_aEhciFields8Ports[0], NULL);
        if (RT_FAILURE(rc))
            return rc;
        AssertReturn(EHCI_NDP_CFG(pThis) == 8, VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
    }
    else
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /*
     * The EOF timer changed from one to two in version 4 of the saved state,
     * then was dropped entirely in version 7.
     *
     * Note! Looks like someone remove the code that dealt with versions 1 thru 4,
     *       without adjust the above comment.
     */
    if (uVersion <= EHCI_SAVED_STATE_VERSION_PRE_TIMER_REMOVAL)
    {
        bool fActive1 = false;
        pHlp->pfnTimerSkipLoad(pSSM, &fActive1);
        bool fActive2 = false;
        pHlp->pfnTimerSkipLoad(pSSM, &fActive2);
        bool fNoSync = false;
        rc = pHlp->pfnSSMGetBool(pSSM, &fNoSync);
        if (   RT_SUCCESS(rc)
            && (fActive1 || fActive2))
            pThis->fBusStarted = true;
        else
            pThis->fBusStarted = false;
    }
    return rc;
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV, Dumps EHCI control registers.}
 */
static DECLCALLBACK(void) ehciR3InfoRegs(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PEHCI pThis = PDMDEVINS_2_DATA(pDevIns, PEHCI);
    unsigned uPort;

    /* Command register */
    pHlp->pfnPrintf(pHlp, "USBCMD: %x\n", pThis->cmd);
    if (pThis->cmd & EHCI_CMD_RUN)
        pHlp->pfnPrintf(pHlp, "    CMD_RUN\n");
    if (pThis->cmd & EHCI_CMD_RESET)
        pHlp->pfnPrintf(pHlp, "    CMD_RESET\n");
    if (pThis->cmd & EHCI_CMD_PERIODIC_SCHED_ENABLE)
        pHlp->pfnPrintf(pHlp, "    CMD_PERIODIC_SCHED_ENABLE\n");
    if (pThis->cmd & EHCI_CMD_ASYNC_SCHED_ENABLE)
        pHlp->pfnPrintf(pHlp, "    CMD_ASYNC_SCHED_ENABLE\n");
    if (pThis->cmd & EHCI_CMD_INT_ON_ADVANCE_DOORBELL)
        pHlp->pfnPrintf(pHlp, "    CMD_INT_ON_ADVANCE_DOORBELL\n");
    if (pThis->cmd & EHCI_CMD_SOFT_RESET)
        pHlp->pfnPrintf(pHlp, "    CMD_SOFT_RESET\n");
    if (pThis->cmd & EHCI_CMD_ASYNC_SCHED_PARK_ENABLE)
        pHlp->pfnPrintf(pHlp, "    CMD_ASYNC_SCHED_PARK_ENABLE\n");

    pHlp->pfnPrintf(pHlp, "    CMD_FRAME_LIST_SIZE              %d\n", (pThis->cmd & EHCI_CMD_FRAME_LIST_SIZE_MASK) >> EHCI_CMD_FRAME_LIST_SIZE_SHIFT);
    pHlp->pfnPrintf(pHlp, "    CMD_ASYNC_SCHED_PARK_MODE_COUNT  %d\n", (pThis->cmd & EHCI_CMD_ASYNC_SCHED_PARK_MODE_COUNT_MASK) >> EHCI_CMD_ASYNC_SCHED_PARK_MODE_COUNT_SHIFT);
    pHlp->pfnPrintf(pHlp, "    CMD_INTERRUPT_THRESHOLD          %d\n", (pThis->cmd & EHCI_CMD_INTERRUPT_THRESHOLD_MASK) >> EHCI_CMD_INTERRUPT_THRESHOLD_SHIFT);

    /* Status register */
    pHlp->pfnPrintf(pHlp, "USBSTS: %x\n", pThis->intr_status);
    if (pThis->intr_status & EHCI_STATUS_ASYNC_SCHED)
        pHlp->pfnPrintf(pHlp, "    STATUS_ASYNC_SCHED\n");
    if (pThis->intr_status & EHCI_STATUS_PERIOD_SCHED)
        pHlp->pfnPrintf(pHlp, "    STATUS_PERIOD_SCHED\n");
    if (pThis->intr_status & EHCI_STATUS_RECLAMATION)
        pHlp->pfnPrintf(pHlp, "    STATUS_RECLAMATION\n");
    if (pThis->intr_status & EHCI_STATUS_HCHALTED)
        pHlp->pfnPrintf(pHlp, "    STATUS_HCHALTED\n");
    if (pThis->intr_status & EHCI_STATUS_INT_ON_ASYNC_ADV)
        pHlp->pfnPrintf(pHlp, "    STATUS_INT_ON_ASYNC_ADV\n");
    if (pThis->intr_status & EHCI_STATUS_HOST_SYSTEM_ERROR)
        pHlp->pfnPrintf(pHlp, "    STATUS_HOST_SYSTEM_ERROR\n");
    if (pThis->intr_status & EHCI_STATUS_FRAME_LIST_ROLLOVER)
        pHlp->pfnPrintf(pHlp, "    STATUS_FRAME_LIST_ROLLOVER\n");
    if (pThis->intr_status & EHCI_STATUS_PORT_CHANGE_DETECT)
        pHlp->pfnPrintf(pHlp, "    STATUS_PORT_CHANGE_DETECT\n");
    if (pThis->intr_status & EHCI_STATUS_ERROR_INT)
        pHlp->pfnPrintf(pHlp, "    STATUS_ERROR_INT\n");
    if (pThis->intr_status & EHCI_STATUS_THRESHOLD_INT)
        pHlp->pfnPrintf(pHlp, "    STATUS_THRESHOLD_INT\n");

    /* Interrupt enable register */
    pHlp->pfnPrintf(pHlp, "USBINTR: %x\n", pThis->intr);
    if (pThis->intr & EHCI_INTR_ENABLE_THRESHOLD)
        pHlp->pfnPrintf(pHlp, "    INTR_ENABLE_THRESHOLD\n");
    if (pThis->intr & EHCI_INTR_ENABLE_ERROR)
        pHlp->pfnPrintf(pHlp, "    INTR_ENABLE_ERROR\n");
    if (pThis->intr & EHCI_INTR_ENABLE_PORT_CHANGE)
        pHlp->pfnPrintf(pHlp, "    INTR_ENABLE_PORT_CHANGE\n");
    if (pThis->intr & EHCI_INTR_ENABLE_FRAME_LIST_ROLLOVER)
        pHlp->pfnPrintf(pHlp, "    INTR_ENABLE_FRAME_LIST_ROLLOVER\n");
    if (pThis->intr & EHCI_INTR_ENABLE_HOST_SYSTEM_ERROR)
        pHlp->pfnPrintf(pHlp, "    INTR_ENABLE_HOST_SYSTEM_ERROR\n");
    if (pThis->intr & EHCI_INTR_ENABLE_ASYNC_ADVANCE)
        pHlp->pfnPrintf(pHlp, "    INTR_ENABLE_ASYNC_ADVANCE\n");
    if (pThis->intr & ~EHCI_INTR_ENABLE_MASK)
        pHlp->pfnPrintf(pHlp, "    Illegal bits set %x!!\n", pThis->intr & ~EHCI_INTR_ENABLE_MASK);

    /* Frame index register */
    pHlp->pfnPrintf(pHlp, "FRINDEX: %x\n", pThis->frame_idx);

    /* Control data structure segment */
    pHlp->pfnPrintf(pHlp, "CTRLDSSEGMENT:    %RX32\n", pThis->ds_segment);

    /* Periodic frame list base address register */
    pHlp->pfnPrintf(pHlp, "PERIODICLISTBASE: %RX32\n", pThis->periodic_list_base);

    /* Current asynchronous list address register */
    pHlp->pfnPrintf(pHlp, "ASYNCLISTADDR:    %RX32\n", pThis->async_list_base);

    pHlp->pfnPrintf(pHlp, "\n");

    for (uPort = 0; uPort < EHCI_NDP_CFG(pThis); ++uPort)
    {
        PEHCIHUBPORT pPort = &pThis->RootHub.aPorts[uPort];
        pHlp->pfnPrintf(pHlp, "PORTSC for port %u:\n", uPort);
        if (pPort->fReg & EHCI_PORT_CURRENT_CONNECT)
            pHlp->pfnPrintf(pHlp, "    PORT_CURRENT_CONNECT\n");
        if (pPort->fReg & EHCI_PORT_CONNECT_CHANGE)
            pHlp->pfnPrintf(pHlp, "    PORT_CONNECT_CHANGE\n");
        if (pPort->fReg & EHCI_PORT_PORT_ENABLED)
            pHlp->pfnPrintf(pHlp, "    PORT_PORT_ENABLED\n");
        if (pPort->fReg & EHCI_PORT_PORT_CHANGE)
            pHlp->pfnPrintf(pHlp, "    PORT_PORT_CHANGE\n");
        if (pPort->fReg & EHCI_PORT_OVER_CURRENT_ACTIVE)
            pHlp->pfnPrintf(pHlp, "    PORT_OVER_CURRENT_ACTIVE\n");
        if (pPort->fReg & EHCI_PORT_OVER_CURRENT_CHANGE)
            pHlp->pfnPrintf(pHlp, "    PORT_OVER_CURRENT_CHANGE\n");
        if (pPort->fReg & EHCI_PORT_FORCE_PORT_RESUME)
            pHlp->pfnPrintf(pHlp, "    PORT_FORCE_PORT_RESUME\n");
        if (pPort->fReg & EHCI_PORT_SUSPEND)
            pHlp->pfnPrintf(pHlp, "    PORT_SUSPEND\n");
        if (pPort->fReg & EHCI_PORT_RESET)
            pHlp->pfnPrintf(pHlp, "    PORT_RESET\n");
        pHlp->pfnPrintf(pHlp, "    LINE_STATUS: ");
        switch ((pPort->fReg & EHCI_PORT_LINE_STATUS_MASK) >> EHCI_PORT_LINE_STATUS_SHIFT)
        {
        case 0:
            pHlp->pfnPrintf(pHlp, "    SE0 (0), not low-speed\n");
            break;
        case 1:
            pHlp->pfnPrintf(pHlp, "    K-state (1), low-speed device\n");
            break;
        case 2:
            pHlp->pfnPrintf(pHlp, "    J-state (2), not low-speed\n");
            break;
        default:
        case 3:
            pHlp->pfnPrintf(pHlp, "    Undefined (3)\n");
            break;
        }
        if (pPort->fReg & EHCI_PORT_POWER)
            pHlp->pfnPrintf(pHlp, "    PORT_POWER\n");
        if (pPort->fReg & EHCI_PORT_OWNER)
            pHlp->pfnPrintf(pHlp, "    PORT_OWNER (1 = owned by companion HC)\n");
        if (pPort->fReg & EHCI_PORT_WAKE_ON_CONNECT_ENABLE)
            pHlp->pfnPrintf(pHlp, "    PORT_WAKE_ON_CONNECT_ENABLE\n");
        if (pPort->fReg & EHCI_PORT_WAKE_ON_DISCONNECT_ENABLE)
            pHlp->pfnPrintf(pHlp, "    PORT_WAKE_ON_DISCONNECT_ENABLE\n");
        if (pPort->fReg & EHCI_PORT_WAKE_OVER_CURRENT_ENABLE)
            pHlp->pfnPrintf(pHlp, "    PORT_WAKE_OVER_CURRENT_ENABLE\n");
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) ehciR3Reset(PPDMDEVINS pDevIns)
{
    PEHCI   pThis   = PDMDEVINS_2_DATA(pDevIns, PEHCI);
    PEHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PEHCICC);
    LogFlowFunc(("\n"));

    /*
     * There is no distinction between cold boot, warm reboot and software reboots,
     * all of these are treated as cold boots. We are also doing the initialization
     * job of a BIOS or SMM driver.
     *
     * Important: Don't confuse UsbReset with hardware reset. Hardware reset is
     *            just one way of getting into the UsbReset state.
     */
    ehciR3BusStop(pThis, pThisCC);
    ehciR3DoReset(pDevIns, pThis, pThisCC, EHCI_USB_RESET, true /* reset devices */);
}


/**
 * Reset notification.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(void) ehciR3Resume(PPDMDEVINS pDevIns)
{
    PEHCI   pThis   = PDMDEVINS_2_DATA(pDevIns, PEHCI);
    PEHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PEHCICC);
    LogFlowFunc(("\n"));

    /* Restart the frame thread if the timer is active. */
    if (pThis->fBusStarted)
    {
        LogFlowFunc(("Bus was active, restart frame thread\n"));
        RTSemEventMultiSignal(pThisCC->hSemEventFrame);
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) ehciR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PEHCI   pThis   = PDMDEVINS_2_DATA(pDevIns, PEHCI);
    PEHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PEHCICC);
    LogFlowFunc(("\n"));

    if (pThisCC->hSemEventFrame != NIL_RTSEMEVENTMULTI)
    {
        RTSemEventMultiDestroy(pThisCC->hSemEventFrame);
        pThisCC->hSemEventFrame = NIL_RTSEMEVENTMULTI;
    }

    if (pThisCC->hSemEventFrameStopped != NIL_RTSEMEVENTMULTI)
    {
        RTSemEventMultiDestroy(pThisCC->hSemEventFrameStopped);
        pThisCC->hSemEventFrameStopped = NIL_RTSEMEVENTMULTI;
    }

    if (RTCritSectIsInitialized(&pThisCC->CritSect))
        RTCritSectDelete(&pThisCC->CritSect);
    PDMDevHlpCritSectDelete(pDevIns, &pThis->CsIrq);

    /*
     * Tear down the per endpoint in-flight tracking...
     */

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct,EHCI constructor}
 */
static DECLCALLBACK(int) ehciR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PEHCI           pThis   = PDMDEVINS_2_DATA(pDevIns, PEHCI);
    PEHCICC         pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PEHCICC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    LogFlowFunc(("\n"));

    /*
     * Read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "DefaultFrameRateKHz|Ports", "");

    /* Frame rate option. */
    int rc = pHlp->pfnCFGMQueryU32Def(pCfg, "DefaultFrameRateKHz", &pThisCC->uFrameRateDefault, EHCI_DEFAULT_TIMER_FREQ / 1000);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("EHCI configuration error: failed to read DefaultFrameRateKHz as integer"));

    if (   pThisCC->uFrameRateDefault > EHCI_HARDWARE_TIMER_FREQ / 1000
        || pThisCC->uFrameRateDefault == 0)
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("EHCI configuration error: DefaultFrameRateKHz must be in range [%u,%u]"),
                                   1, EHCI_HARDWARE_TIMER_FREQ / 1000);

    /* Convert to Hertz. */
    pThisCC->uFrameRateDefault *= 1000;

    /* Number of ports option. */
    uint32_t cPorts;
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "Ports", &cPorts, EHCI_NDP_DEFAULT);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("EHCI configuration error: failed to read Ports as integer"));

    if (cPorts == 0 || cPorts > EHCI_NDP_MAX)
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("EHCI configuration error: Ports must be in range [%u,%u]"),
                                   1, EHCI_NDP_MAX);

    /*
     * Init instance data.
     */
    pThisCC->pDevIns = pDevIns;

    /* Intel 82801FB/FBM USB2 controller */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    PDMPciDevSetVendorId(pPciDev,       0x8086);
    PDMPciDevSetDeviceId(pPciDev,       0x265C);
    PDMPciDevSetClassProg(pPciDev,      0x20); /* EHCI */
    PDMPciDevSetClassSub(pPciDev,       0x03);
    PDMPciDevSetClassBase(pPciDev,      0x0c);
    PDMPciDevSetInterruptPin(pPciDev,   0x01);
#ifdef VBOX_WITH_MSI_DEVICES
    PDMPciDevSetStatus(pPciDev,         VBOX_PCI_STATUS_CAP_LIST);
    PDMPciDevSetCapabilityList(pPciDev, 0x80);
#endif
    PDMPciDevSetByte(pPciDev, 0x60,     0x20); /* serial bus release number register; 0x20 = USB 2.0 */
    /** @todo USBLEGSUP & USBLEGCTLSTS? Legacy interface for the BIOS (0xEECP+0 & 0xEECP+4) */

    pThisCC->RootHub.IBase.pfnQueryInterface       = ehciR3RhQueryInterface;
    pThisCC->RootHub.IRhPort.pfnGetAvailablePorts  = ehciR3RhGetAvailablePorts;
    pThisCC->RootHub.IRhPort.pfnGetUSBVersions     = ehciR3RhGetUSBVersions;
    pThisCC->RootHub.IRhPort.pfnAttach             = ehciR3RhAttach;
    pThisCC->RootHub.IRhPort.pfnDetach             = ehciR3RhDetach;
    pThisCC->RootHub.IRhPort.pfnReset              = ehciR3RhReset;
    pThisCC->RootHub.IRhPort.pfnXferCompletion     = ehciR3RhXferCompletion;
    pThisCC->RootHub.IRhPort.pfnXferError          = ehciR3RhXferError;

    /* USB LED */
    pThisCC->RootHub.Led.u32Magic                  = PDMLED_MAGIC;
    pThisCC->RootHub.ILeds.pfnQueryStatusLed       = ehciR3RhQueryStatusLed;

    /*
     * Register PCI device and I/O region.
     */
    rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
    if (RT_FAILURE(rc))
        return rc;

#ifdef VBOX_WITH_MSI_DEVICES
    PDMMSIREG MsiReg;
    RT_ZERO(MsiReg);
    MsiReg.cMsiVectors    = 1;
    MsiReg.iMsiCapOffset  = 0x80;
    MsiReg.iMsiNextOffset = 0x00;
    rc = PDMDevHlpPCIRegisterMsi(pDevIns, &MsiReg);
    if (RT_FAILURE(rc))
    {
        PDMPciDevSetCapabilityList(pPciDev, 0x0);
        /* That's OK, we can work without MSI */
    }
#endif

    rc = PDMDevHlpPCIIORegionCreateMmio(pDevIns, 0 /*iPciRegion*/, 4096 /*cbRegion*/, PCI_ADDRESS_SPACE_MEM,
                                        ehciMmioWrite, ehciMmioRead, NULL /*pvUser*/,
                                        IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED
                                        | IOMMMIO_FLAGS_DBGSTOP_ON_COMPLICATED_WRITE,
                                        "USB EHCI", &pThis->hMmio);
    AssertRCReturn(rc, rc);

    /* Initialize capability registers */
    pThis->cap_length           = EHCI_CAPS_REG_SIZE;
    pThis->hci_version          = 0x100;
    /*  31:24   Reserved
     *  23:20   Debug Port Number
     *  19:17   Reserved
     *  16      Port indicators (P_INDICATOR) enabled/disabled
     *  15:12   Number of companion controllers (N_CC)
     *  11:8    Number of ports per companion controller (N_PCC)
     *  7       Port routing controls enabled/disabled
     *  6:5     Reserved
     *  4       Port power control enabled/disabled                     -> disabled to simplify matters!
     *  3:0     N_PORTS; number of ports
     */
    /* Currently only number of ports specified */
    pThis->hcs_params           = cPorts;

    /*  31:16   Reserved
     *  15:8    EHCI extended capabilities pointer (EECP) (0x40 or greater)
     *  7:4     Isochronous scheduling threshold
     *  3       Reserved
     *  2       Asynchronous schedule park capability (allow several TDs to be handled per async queue head)
     *  1       Programmable frame list flag (0=1024 frames fixed)
     *  0       64 bits addressability
     */
    pThis->hcc_params           = EHCI_HCC_PARAMS_ISOCHRONOUS_CACHING | EHCI_HCC_PARAMS_ASYNC_SCHEDULE_PARKING;

    /*
     * Register the saved state data unit.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, EHCI_SAVED_STATE_VERSION, sizeof(*pThis), NULL,
                                NULL, NULL, NULL,
                                NULL, ehciR3SaveExec, NULL,
                                NULL, ehciLoadExec,   NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Attach to the VBox USB RootHub Driver on LUN #0.
     */
    rc = PDMDevHlpDriverAttach(pDevIns, 0, &pThisCC->RootHub.IBase, &pThisCC->RootHub.pIBase, "RootHub");
    if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: No roothub driver attached to LUN #0!\n"));
        return rc;
    }
    pThisCC->RootHub.pIRhConn = PDMIBASE_QUERY_INTERFACE(pThisCC->RootHub.pIBase, VUSBIROOTHUBCONNECTOR);
    AssertMsgReturn(pThisCC->RootHub.pIRhConn,
                    ("Configuration error: The driver doesn't provide the VUSBIROOTHUBCONNECTOR interface!\n"),
                    VERR_PDM_MISSING_INTERFACE);

    /*
     * Attach status driver (optional).
     */
    PPDMIBASE pBase;
    rc = PDMDevHlpDriverAttach(pDevIns, PDM_STATUS_LUN, &pThisCC->RootHub.IBase, &pBase, "Status Port");
    if (RT_SUCCESS(rc))
        pThisCC->RootHub.pLedsConnector = PDMIBASE_QUERY_INTERFACE(pBase, PDMILEDCONNECTORS);
    else
        AssertLogRelMsgReturn(rc == VERR_PDM_NO_ATTACHED_DRIVER, ("Failed to attach to status driver. rc=%Rrc\n", rc), rc);

    /* Set URB parameters. */
    rc = VUSBIRhSetUrbParams(pThisCC->RootHub.pIRhConn, sizeof(VUSBURBHCIINT), sizeof(VUSBURBHCITDINT));
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS, N_("EHCI: Failed to set URB parameters"));

    /*
     * Calculate the timer intervals.
     * This ASSUMES that the VM timer doesn't change frequency during the run.
     */
    pThisCC->u64TimerHz = PDMDevHlpTMTimeVirtGetFreq(pDevIns);
    ehciR3CalcTimerIntervals(pThis, pThisCC, pThisCC->uFrameRateDefault);
    LogFunc(("cTicksPerFrame=%RU64 cTicksPerUsbTick=%RU64\n", pThisCC->cTicksPerFrame, pThisCC->cTicksPerUsbTick));

    pThis->fBusStarted = false;

    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CsIrq, RT_SRC_POS, "EHCI#%uIrq", iInstance);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("EHCI: Failed to create critical section"));

    rc = RTSemEventMultiCreate(&pThisCC->hSemEventFrame);
    AssertRCReturn(rc, rc);

    rc = RTSemEventMultiCreate(&pThisCC->hSemEventFrameStopped);
    AssertRCReturn(rc, rc);

    rc = RTCritSectInit(&pThisCC->CritSect);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS, N_("EHCI: Failed to create critical section"));

    rc = PDMDevHlpThreadCreate(pDevIns, &pThisCC->hThreadFrame, pThisCC, ehciR3ThreadFrame,
                               ehciR3ThreadFrameWakeup, 0, RTTHREADTYPE_IO, "EhciFramer");
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS, N_("EHCI: Failed to create worker thread"));

    /*
     * Do a hardware reset.
     */
    ehciR3DoReset(pDevIns, pThis, pThisCC, EHCI_USB_RESET, false /* don't reset devices */);

#ifdef VBOX_WITH_STATISTICS
    /*
     * Register statistics.
     */
    PDMDevHlpSTAMRegister(pDevIns, &pThisCC->StatCanceledIsocUrbs, STAMTYPE_COUNTER, "CanceledIsocUrbs", STAMUNIT_OCCURENCES,     "Detected canceled isochronous URBs.");
    PDMDevHlpSTAMRegister(pDevIns, &pThisCC->StatCanceledGenUrbs,  STAMTYPE_COUNTER, "CanceledGenUrbs",  STAMUNIT_OCCURENCES,     "Detected canceled general URBs.");
    PDMDevHlpSTAMRegister(pDevIns, &pThisCC->StatDroppedUrbs,      STAMTYPE_COUNTER, "DroppedUrbs",      STAMUNIT_OCCURENCES,     "Dropped URBs (endpoint halted, or URB canceled).");
#endif

    /*
     * Register debugger info callbacks.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "ehci", "EHCI control registers.", ehciR3InfoRegs);

#ifdef DEBUG_sandervl
//  g_fLogInterruptEPs = true;
    g_fLogControlEPs = true;
#endif

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) ehciRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PEHCI pThis = PDMDEVINS_2_DATA(pDevIns, PEHCI);

    int rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, ehciMmioWrite, ehciMmioRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

const PDMDEVREG g_DeviceEHCI =
{
    /* .u32version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "usb-ehci",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_BUS_USB,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(EHCI),
    /* .cbInstanceCC = */           sizeof(EHCICC),
    /* .cbInstanceRC = */           sizeof(EHCIRC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "EHCI USB controller.\n",
#if defined(IN_RING3)
# ifdef VBOX_IN_EXTPACK
    /* .pszRCMod = */               "VBoxEhciRC.rc",
    /* .pszR0Mod = */               "VBoxEhciR0.r0",
# else
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
# endif
    /* .pfnConstruct = */           ehciR3Construct,
    /* .pfnDestruct = */            ehciR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               ehciR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              ehciR3Resume,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            NULL,
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
    /* .pfnConstruct = */           ehciRZConstruct,
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
    /* .pfnConstruct = */           ehciRZConstruct,
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

#ifdef VBOX_IN_EXTPACK
extern "C" const PDMDEVREG g_DeviceXHCI;

# ifdef VBOX_IN_EXTPACK_R3

/**
 * @callback_method_impl{FNPDMVBOXDEVICESREGISTER}
 */
extern "C" DECLEXPORT(int) VBoxDevicesRegister(PPDMDEVREGCB pCallbacks, uint32_t u32Version)
{
    AssertLogRelMsgReturn(u32Version >= VBOX_VERSION,
                          ("u32Version=%#x VBOX_VERSION=%#x\n", u32Version, VBOX_VERSION),
                          VERR_EXTPACK_VBOX_VERSION_MISMATCH);
    AssertLogRelMsgReturn(pCallbacks->u32Version == PDM_DEVREG_CB_VERSION,
                          ("pCallbacks->u32Version=%#x PDM_DEVREG_CB_VERSION=%#x\n", pCallbacks->u32Version, PDM_DEVREG_CB_VERSION),
                          VERR_VERSION_MISMATCH);

    int rc = pCallbacks->pfnRegister(pCallbacks, &g_DeviceEHCI);

    /* EHCI and xHCI devices live in the same module. */
    extern const PDMDEVREG g_DeviceXHCI;
    if (RT_SUCCESS(rc))
        rc = pCallbacks->pfnRegister(pCallbacks, &g_DeviceXHCI);

    return rc;
}

# else

/** Pointer to the ring-0 device registrations for VBoxEhciR0/RC. */
static PCPDMDEVREGR0 g_apDevRegs[] =
{
    &g_DeviceEHCI,
    &g_DeviceXHCI,
};

/** Module device registration record for VBoxEhciR0/RC. */
static PDMDEVMODREGR0 g_ModDevReg =
{
    /* .u32Version = */ PDM_DEVMODREGR0_VERSION,
    /* .cDevRegs = */   RT_ELEMENTS(g_apDevRegs),
    /* .papDevRegs = */ &g_apDevRegs[0],
    /* .hMod = */       NULL,
    /* .ListEntry = */  { NULL, NULL },
};


DECLEXPORT(int)  ModuleInit(void *hMod)
{
    LogFlow(("VBoxEhciRZ/ModuleInit: %p\n", hMod));
    return PDMR0DeviceRegisterModule(hMod, &g_ModDevReg);
}


DECLEXPORT(void) ModuleTerm(void *hMod)
{
    LogFlow(("VBoxEhciRZ/ModuleTerm: %p\n", hMod));
    PDMR0DeviceDeregisterModule(hMod, &g_ModDevReg);
}

# endif
#endif /* VBOX_IN_EXTPACK */

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

