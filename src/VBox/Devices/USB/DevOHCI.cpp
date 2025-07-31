/* $Id: DevOHCI.cpp 110179 2025-07-09 18:25:13Z michal.necasek@oracle.com $ */
/** @file
 * DevOHCI - Open Host Controller Interface for USB.
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

/** @page pg_dev_ohci   OHCI - Open Host Controller Interface Emulation.
 *
 * This component implements an OHCI USB controller. It is split roughly in
 * to two main parts, the first part implements the register level
 * specification of USB OHCI and the second part maintains the root hub (which
 * is an integrated component of the device).
 *
 * The OHCI registers are used for the usual stuff like enabling and disabling
 * interrupts. Since the USB time is divided in to 1ms frames and various
 * interrupts may need to be triggered at frame boundary time, a timer-based
 * approach was taken. Whenever the bus is enabled ohci->eof_timer will be set.
 *
 * The actual USB transfers are stored in main memory (along with endpoint and
 * transfer descriptors). The ED's for all the control and bulk endpoints are
 * found by consulting the HcControlHeadED and HcBulkHeadED registers
 * respectively. Interrupt ED's are different, they are found by looking
 * in the HCCA (another communication area in main memory).
 *
 * At the start of every frame (in function ohci_sof) we traverse all enabled
 * ED lists and queue up as many transfers as possible. No attention is paid
 * to control/bulk service ratios or bandwidth requirements since our USB
 * could conceivably contain a dozen high speed busses so this would
 * artificially limit the performance.
 *
 * Once we have a transfer ready to go (in function ohciR3ServiceTd) we
 * allocate an URB on the stack,  fill in all the relevant fields and submit
 * it using the VUSBIRhSubmitUrb function. The roothub device and the virtual
 * USB core code (vusb.c) coordinates everything else from this point onwards.
 *
 * When the URB has been successfully handed to the lower level driver, our
 * prepare callback gets called and we can remove the TD from the ED transfer
 * list. This stops us queueing it twice while it completes.
 *  bird: no, we don't remove it because that confuses the guest! (=> crashes)
 *
 * Completed URBs are reaped at the end of every frame (in function
 * ohci_frame_boundary). Our completion routine makes use of the ED and TD
 * fields in the URB to store the physical addresses of the descriptors so
 * that they may be modified in the roothub callbacks. Our completion
 * routine (ohciR3RhXferCompletion) carries out a number of tasks:
 *      -# Retires the TD associated with the transfer, setting the
 *         relevant error code etc.
 *      -# Updates done-queue interrupt timer and potentially causes
 *         a writeback of the done-queue.
 *      -# If the transfer was device-to-host, we copy the data in to
 *         the host memory.
 *
 * The guest can set the SKIP bit in an endpoint to pause USB traffic on
 * that endpoint. If we see the bit set, we need to cancel any URBs that might
 * be queued on that particular endpoint.
 *
 * In some situations, we might miss seeing the SKIP bit. Additionally, the
 * guest is not forced to set the SKIP bit and might rearrange the transfer
 * descriptors at any time, or remove EDs from the list entirely. We must
 * therefore keep track of which endpoints have URBs already in flight, and
 * cancel any and all such URBs if we detect that the guest is no longer
 * attempting to perform any transfers on them.
 *
 * When we submit a URB, we note the corresponding endpoint and transfer
 * descriptor information. URB completion checks if the descriptors in
 * guest memory are still the same; if not, the URB is effectively thrown
 * away (and nothing is placed on the corresponding done queue).
 *
 * For control and interrupt endpoints, we convert each TD into a URB. No
 * attempt is made to pipeline the traffic and submit multiple URBs for an
 * endpoint.
 *
 * For bulk endpoints, we use heuristics to decide when multiple TDs should
 * be coalesced into a single URB. This logic helps among others with MSDs
 * which tend to transfer data in larger chunks, such as 32 or 64 KB.
 *
 * As for error handling OHCI allows for 3 retries before failing a transfer,
 * an error count is stored in each transfer descriptor. A halt flag is also
 * stored in the transfer descriptor. That allows for ED's to be disabled
 * by the HC without stopping the bus and de-queuing them.
 *
 * When the bus is started and stopped we call VUSBIDevPowerOn/Off() on our
 * roothub to indicate it's powering up and powering down. Whenever we power
 * down, the  USB core makes sure to synchronously complete all outstanding
 * requests so  that the OHCI is never seen in an inconsistent state by the
 * guest OS (Transfers are not meant to be unlinked until they've actually
 * completed, but we can't do that unless we work synchronously, so we just
 * have to fake it).
 *  bird: we do work synchronously now, anything causes guest crashes.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_OHCI
#include <VBox/pci.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/mm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/AssertGuest.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/asm.h>
#include <iprt/asm-math.h>
#include <iprt/semaphore.h>
#include <iprt/critsect.h>
#include <iprt/param.h>
#ifdef IN_RING3
# include <iprt/alloca.h>
# include <iprt/mem.h>
# include <iprt/thread.h>
# include <iprt/uuid.h>
#endif
#include <VBox/vusb.h>
#include "VBoxDD.h"


#define VBOX_WITH_OHCI_PHYS_READ_CACHE
//#define VBOX_WITH_OHCI_PHYS_READ_STATS


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** The current saved state version. */
#define OHCI_SAVED_STATE_VERSION                OHCI_SAVED_STATE_VERSION_NO_EOF_TIMER
/** The current saved state version.
 * @since 6.1.0beta3/rc1  */
#define OHCI_SAVED_STATE_VERSION_NO_EOF_TIMER   6
/** The current saved with the start-of-frame timer.
 * @since 4.3.x  */
#define OHCI_SAVED_STATE_VERSION_EOF_TIMER      5
/** The saved state with support of up to 8 ports.
 * @since 3.1 or so  */
#define OHCI_SAVED_STATE_VERSION_8PORTS         4


/** Maximum supported number of Downstream Ports on the root hub. 15 ports
 * is the maximum defined by the OHCI spec. Must match the number of status
 * register words to the 'opreg' array.
 */
#define OHCI_NDP_MAX        15

/** Default NDP, chosen to be compatible with everything. */
#define OHCI_NDP_DEFAULT    12

/* Macro to query the number of currently configured ports. */
#define OHCI_NDP_CFG(pohci) ((pohci)->RootHub.desc_a & OHCI_RHA_NDP)
/** Macro to convert an OHCI port index (zero based) to a VUSB roothub port ID (one based). */
#define OHCI_PORT_2_VUSB_PORT(a_uPort) ((a_uPort) + 1)

/** Pointer to OHCI device data. */
typedef struct OHCI *POHCI;
/** Read-only pointer to the OHCI device data. */
typedef struct OHCI const *PCOHCI;

#ifndef VBOX_DEVICE_STRUCT_TESTCASE
/**
 * Host controller transfer descriptor data.
 */
typedef struct VUSBURBHCITDINT
{
    /** Type of TD. */
    uint32_t        TdType;
    /** The address of the */
    RTGCPHYS32      TdAddr;
    /** A copy of the TD. */
    uint32_t        TdCopy[8];
} VUSBURBHCITDINT;

/**
 * The host controller data associated with each URB.
 */
typedef struct VUSBURBHCIINT
{
    /** The endpoint descriptor address. */
    RTGCPHYS32      EdAddr;
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
 * An OHCI root hub port.
 */
typedef struct OHCIHUBPORT
{
    /** The port register. */
    uint32_t                fReg;
    /** Flag whether there is a device attached to the port. */
    bool                    fAttached;
    bool                    afPadding[3];
} OHCIHUBPORT;
/** Pointer to an OHCI hub port. */
typedef OHCIHUBPORT *POHCIHUBPORT;

/**
 * The OHCI root hub, shared.
 */
typedef struct OHCIROOTHUB
{
    uint32_t                            status;
    uint32_t                            desc_a;
    uint32_t                            desc_b;
#if HC_ARCH_BITS == 64
    uint32_t                            Alignment0; /**< Align aPorts on a 8 byte boundary. */
#endif
    OHCIHUBPORT                         aPorts[OHCI_NDP_MAX];
} OHCIROOTHUB;
/** Pointer to the OHCI root hub. */
typedef OHCIROOTHUB *POHCIROOTHUB;


/**
 * The OHCI root hub, ring-3 data.
 *
 * @implements  PDMIBASE
 * @implements  VUSBIROOTHUBPORT
 * @implements  PDMILEDPORTS
 */
typedef struct OHCIROOTHUBR3
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

    OHCIHUBPORT                         aPorts[OHCI_NDP_MAX];
    R3PTRTYPE(POHCI)                    pOhci;
} OHCIROOTHUBR3;
/** Pointer to the OHCI ring-3 root hub data. */
typedef OHCIROOTHUBR3 *POHCIROOTHUBR3;

#ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE
typedef struct OHCIPAGECACHE
{
    /** Last read physical page address. */
    RTGCPHYS            GCPhysReadCacheAddr;
    /** Copy of last read physical page. */
    uint8_t             abPhysReadCache[GUEST_PAGE_SIZE];
} OHCIPAGECACHE;
typedef OHCIPAGECACHE *POHCIPAGECACHE;
#endif

/**
 * OHCI device data, shared.
 */
typedef struct OHCI
{
    /** Start of current frame. */
    uint64_t            SofTime;
    /** done queue interrupt counter */
    uint32_t            dqic : 3;
    /** frame number overflow. */
    uint32_t            fno : 1;

    /** Align roothub structure on a 8-byte boundary. */
    uint32_t            u32Alignment0;
    /** Root hub device, shared data. */
    OHCIROOTHUB         RootHub;

    /* OHCI registers */

    /** @name Control partition
     * @{ */
    /** HcControl. */
    uint32_t            ctl;
    /** HcCommandStatus. */
    uint32_t            status;
    /** HcInterruptStatus. */
    uint32_t            intr_status;
    /** HcInterruptEnabled. */
    uint32_t            intr;
    /** @} */

    /** @name Memory pointer partition
     * @{ */
    /** HcHCCA. */
    uint32_t            hcca;
    /** HcPeriodCurrentEd. */
    uint32_t            per_cur;
    /** HcControlCurrentED. */
    uint32_t            ctrl_cur;
    /** HcControlHeadED. */
    uint32_t            ctrl_head;
    /** HcBlockCurrendED. */
    uint32_t            bulk_cur;
    /** HcBlockHeadED. */
    uint32_t            bulk_head;
    /** HcDoneHead. */
    uint32_t            done;
    /** @} */

    /** @name Frame counter partition
     * @{ */
    /** HcFmInterval.FSMPS - FSLargestDataPacket */
    uint32_t            fsmps : 15;
    /** HcFmInterval.FIT - FrameItervalToggle */
    uint32_t            fit : 1;
    /** HcFmInterval.FI - FrameInterval */
    uint32_t            fi : 14;
    /** HcFmRemaining.FRT - toggle bit. */
    uint32_t            frt : 1;
    /** HcFmNumber.
     * @remark The register size is 16-bit, but for debugging and performance
     *         reasons we maintain a 32-bit counter. */
    uint32_t            HcFmNumber;
    /** HcPeriodicStart */
    uint32_t            pstart;
    /** @} */

    /** This member and all the following are not part of saved state. */
    uint64_t            SavedStateEnd;

    /** The number of virtual time ticks per frame. */
    uint64_t            cTicksPerFrame;
    /** The number of virtual time ticks per USB bus tick. */
    uint64_t            cTicksPerUsbTick;

    /** Detected canceled isochronous URBs. */
    STAMCOUNTER         StatCanceledIsocUrbs;
    /** Detected canceled general URBs. */
    STAMCOUNTER         StatCanceledGenUrbs;
    /** Dropped URBs (endpoint halted, or URB canceled). */
    STAMCOUNTER         StatDroppedUrbs;

    /** VM timer frequency used for frame timer calculations. */
    uint64_t            u64TimerHz;
    /** Idle detection flag; must be cleared at start of frame */
    bool                fIdle;
    /** A flag indicating that the bulk list may have in-flight URBs. */
    bool                fBulkNeedsCleaning;

    bool                afAlignment3[2];
    uint32_t            Alignment4;     /**< Align size on a 8 byte boundary. */

    /** Critical section synchronising interrupt handling. */
    PDMCRITSECT         CsIrq;

    /** The MMIO region handle. */
    IOMMMIOHANDLE       hMmio;
} OHCI;


/**
 * OHCI device data, ring-3.
 */
typedef struct OHCIR3
{
    /** The root hub, ring-3 portion.   */
    OHCIROOTHUBR3       RootHub;
    /** Pointer to the device instance - R3 ptr. */
    PPDMDEVINSR3        pDevInsR3;

    /** Number of in-flight TDs. */
    unsigned            cInFlight;
    unsigned            Alignment0;    /**< Align aInFlight on a 8 byte boundary. */
    /** Array of in-flight TDs. */
    struct ohci_td_in_flight
    {
        /** Address of the transport descriptor. */
        uint32_t            GCPhysTD;
        /** Flag indicating an inactive (not-linked) URB. */
        bool                fInactive;
        /** Pointer to the URB. */
        R3PTRTYPE(PVUSBURB) pUrb;
    } aInFlight[257];

#if HC_ARCH_BITS == 32
    uint32_t            Alignment1;
#endif

    /** Number of in-done-queue TDs. */
    unsigned            cInDoneQueue;
    /** Array of in-done-queue TDs. */
    struct ohci_td_in_done_queue
    {
        /** Address of the transport descriptor. */
        uint32_t            GCPhysTD;
    } aInDoneQueue[64];
    /** When the tail of the done queue was added.
     * Used to calculate the age of the done queue. */
    uint32_t            u32FmDoneQueueTail;
#if R3_ARCH_BITS == 32
    /** Align pLoad, the stats and the struct size correctly. */
    uint32_t            Alignment2;
#endif

#ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE
    /** Last read physical page for caching ED reads in the framer thread. */
    OHCIPAGECACHE       CacheED;
    /** Last read physical page for caching TD reads in the framer thread. */
    OHCIPAGECACHE       CacheTD;
#endif

    /** Critical section to synchronize the framer and URB completion handler. */
    RTCRITSECT          CritSect;

    /** The restored periodic frame rate. */
    uint32_t             uRestoredPeriodicFrameRate;
} OHCIR3;
/** Pointer to ring-3 OHCI state. */
typedef OHCIR3 *POHCIR3;

/**
 * OHCI device data, ring-0.
 */
typedef struct OHCIR0
{
    uint32_t                    uUnused;
} OHCIR0;
/** Pointer to ring-0 OHCI state. */
typedef OHCIR0 *POHCIR0;


/**
 * OHCI device data, raw-mode.
 */
typedef struct OHCIRC
{
    uint32_t                    uUnused;
} OHCIRC;
/** Pointer to raw-mode OHCI state. */
typedef OHCIRC *POHCIRC;


/** @typedef OHCICC
 * The instance data for the current context. */
typedef CTX_SUFF(OHCI) OHCICC;
/** @typedef POHCICC
 * Pointer to the instance data for the current context. */
typedef CTX_SUFF(POHCI) POHCICC;


/** Standard OHCI bus speed */
#define OHCI_DEFAULT_TIMER_FREQ     1000

/** Host Controller Communications Area
 * @{  */
#define OHCI_HCCA_NUM_INTR  32
#define OHCI_HCCA_OFS       (OHCI_HCCA_NUM_INTR * sizeof(uint32_t))
typedef struct OCHIHCCA
{
    uint16_t frame;
    uint16_t pad;
    uint32_t done;
} OCHIHCCA;
AssertCompileSize(OCHIHCCA, 8);
/** @} */

/** @name OHCI Endpoint Descriptor
 * @{ */

#define ED_PTR_MASK         (~(uint32_t)0xf)
#define ED_HWINFO_MPS       0x07ff0000
#define ED_HWINFO_ISO       RT_BIT(15)
#define ED_HWINFO_SKIP      RT_BIT(14)
#define ED_HWINFO_LOWSPEED  RT_BIT(13)
#define ED_HWINFO_IN        RT_BIT(12)
#define ED_HWINFO_OUT       RT_BIT(11)
#define ED_HWINFO_DIR       (RT_BIT(11) | RT_BIT(12))
#define ED_HWINFO_ENDPOINT  0x780  /* 4 bits */
#define ED_HWINFO_ENDPOINT_SHIFT 7
#define ED_HWINFO_FUNCTION  0x7f /* 7 bits */
#define ED_HEAD_CARRY       RT_BIT(1)
#define ED_HEAD_HALTED      RT_BIT(0)

/**
 * OHCI Endpoint Descriptor.
 */
typedef struct OHCIED
{
    /** Flags and stuff. */
    uint32_t hwinfo;
    /** TailP - TD Queue Tail pointer. Bits 0-3 ignored / preserved. */
    uint32_t TailP;
    /** HeadP - TD Queue head pointer. Bit 0 - Halted, Bit 1 - toggleCarry. Bit 2&3 - 0. */
    uint32_t HeadP;
    /** NextED - Next Endpoint Descriptor. Bits 0-3 ignored / preserved. */
    uint32_t NextED;
} OHCIED, *POHCIED;
typedef const OHCIED *PCOHCIED;
/** @} */
AssertCompileSize(OHCIED, 16);


/** @name Completion Codes
 * @{ */
#define OHCI_CC_NO_ERROR                (UINT32_C(0x00) << 28)
#define OHCI_CC_CRC                     (UINT32_C(0x01) << 28)
#define OHCI_CC_STALL                   (UINT32_C(0x04) << 28)
#define OHCI_CC_DEVICE_NOT_RESPONDING   (UINT32_C(0x05) << 28)
#define OHCI_CC_DNR                     OHCI_CC_DEVICE_NOT_RESPONDING
#define OHCI_CC_PID_CHECK_FAILURE       (UINT32_C(0x06) << 28)
#define OHCI_CC_UNEXPECTED_PID          (UINT32_C(0x07) << 28)
#define OHCI_CC_DATA_OVERRUN            (UINT32_C(0x08) << 28)
#define OHCI_CC_DATA_UNDERRUN           (UINT32_C(0x09) << 28)
/* 0x0a..0x0b - reserved */
#define OHCI_CC_BUFFER_OVERRUN          (UINT32_C(0x0c) << 28)
#define OHCI_CC_BUFFER_UNDERRUN         (UINT32_C(0x0d) << 28)
#define OHCI_CC_NOT_ACCESSED_0          (UINT32_C(0x0e) << 28)
#define OHCI_CC_NOT_ACCESSED_1          (UINT32_C(0x0f) << 28)
/** @} */


/** @name OHCI General transfer descriptor
 * @{ */

/** Error count (EC) shift. */
#define TD_ERRORS_SHIFT         26
/** Error count max. (One greater than what the EC field can hold.) */
#define TD_ERRORS_MAX           4

/** CC - Condition code mask. */
#define TD_HWINFO_CC            (UINT32_C(0xf0000000))
#define TD_HWINFO_CC_SHIFT      28
/** EC - Error count. */
#define TD_HWINFO_ERRORS        (RT_BIT(26) | RT_BIT(27))
/** T  - Data toggle. */
#define TD_HWINFO_TOGGLE        (RT_BIT(24) | RT_BIT(25))
#define TD_HWINFO_TOGGLE_HI     (RT_BIT(25))
#define TD_HWINFO_TOGGLE_LO     (RT_BIT(24))
/** DI - Delay interrupt. */
#define TD_HWINFO_DI            (RT_BIT(21) | RT_BIT(22) | RT_BIT(23))
#define TD_HWINFO_IN            (RT_BIT(20))
#define TD_HWINFO_OUT           (RT_BIT(19))
/** DP - Direction / PID. */
#define TD_HWINFO_DIR           (RT_BIT(19) | RT_BIT(20))
/** R  - Buffer rounding. */
#define TD_HWINFO_ROUNDING      (RT_BIT(18))
/** Bits that are reserved / unknown. */
#define TD_HWINFO_UNKNOWN_MASK  (UINT32_C(0x0003ffff))

/** SETUP - to endpoint. */
#define OHCI_TD_DIR_SETUP       0x0
/** OUT - to endpoint. */
#define OHCI_TD_DIR_OUT         0x1
/** IN - from endpoint. */
#define OHCI_TD_DIR_IN          0x2
/** Reserved. */
#define OHCI_TD_DIR_RESERVED    0x3

/**
 * OHCI general transfer descriptor
 */
typedef struct OHCITD
{
    uint32_t hwinfo;
    /** CBP - Current Buffer Pointer. (32-bit physical address) */
    uint32_t cbp;
    /** NextTD - Link to the next transfer descriptor. (32-bit physical address, dword aligned) */
    uint32_t NextTD;
    /** BE - Buffer End (inclusive). (32-bit physical address) */
    uint32_t be;
} OHCITD, *POHCITD;
typedef const OHCITD *PCOHCITD;
/** @} */
AssertCompileSize(OHCIED, 16);


/** @name OHCI isochronous transfer descriptor.
 * @{ */
/** SF - Start frame number. */
#define ITD_HWINFO_SF       0xffff
/** DI - Delay interrupt. (TD_HWINFO_DI) */
#define ITD_HWINFO_DI       (RT_BIT(21) | RT_BIT(22) | RT_BIT(23))
#define ITD_HWINFO_DI_SHIFT 21
/** FC - Frame count. */
#define ITD_HWINFO_FC       (RT_BIT(24) | RT_BIT(25) | RT_BIT(26))
#define ITD_HWINFO_FC_SHIFT 24
/** CC - Condition code mask. (=TD_HWINFO_CC)  */
#define ITD_HWINFO_CC       UINT32_C(0xf0000000)
#define ITD_HWINFO_CC_SHIFT 28
/** The buffer page 0 mask (lower 12 bits are ignored). */
#define ITD_BP0_MASK        UINT32_C(0xfffff000)

#define ITD_NUM_PSW 8
/** OFFSET - offset of the package into the buffer page.
 * (Only valid when CC set to Not Accessed.)
 *
 * Note that the top bit of the OFFSET field is overlapping with the
 * first bit in the CC field. This is ok because both 0xf and 0xe are
 * defined as "Not Accessed".
 */
#define ITD_PSW_OFFSET      0x1fff
/** SIZE field mask for IN bound transfers.
 * (Only valid when CC isn't Not Accessed.)*/
#define ITD_PSW_SIZE        0x07ff
/** CC field mask.
 * USed to indicate the format of SIZE (Not Accessed -> OFFSET). */
#define ITD_PSW_CC          0xf000
#define ITD_PSW_CC_SHIFT    12

/**
 * OHCI isochronous transfer descriptor.
 */
typedef struct OHCIITD
{
    uint32_t HwInfo;
    /** BP0 - Buffer Page 0. The lower 12 bits are ignored. */
    uint32_t BP0;
    /** NextTD - Link to the next transfer descriptor. (32-bit physical address, dword aligned) */
    uint32_t NextTD;
    /** BE - Buffer End (inclusive). (32-bit physical address) */
    uint32_t BE;
    /** (OffsetN/)PSWN - package status word array (0..7).
     * The format varies depending on whether the package has been completed or not. */
    uint16_t aPSW[ITD_NUM_PSW];
} OHCIITD, *POHCIITD;
typedef const OHCIITD *PCOHCIITD;
/** @} */
AssertCompileSize(OHCIITD, 32);

/**
 * OHCI register operator.
 */
typedef struct OHCIOPREG
{
    const char *pszName;
    VBOXSTRICTRC (*pfnRead )(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value);
    VBOXSTRICTRC (*pfnWrite)(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t u32Value);
} OHCIOPREG;


/* OHCI Local stuff */
#define OHCI_CTL_CBSR       ((1<<0)|(1<<1)) /* Control/Bulk Service Ratio. */
#define OHCI_CTL_PLE        (1<<2)          /* Periodic List Enable. */
#define OHCI_CTL_IE         (1<<3)          /* Isochronous Enable. */
#define OHCI_CTL_CLE        (1<<4)          /* Control List Enable. */
#define OHCI_CTL_BLE        (1<<5)          /* Bulk List Enable. */
#define OHCI_CTL_HCFS       ((1<<6)|(1<<7)) /* Host Controller Functional State. */
#define  OHCI_USB_RESET         0x00
#define  OHCI_USB_RESUME        0x40
#define  OHCI_USB_OPERATIONAL   0x80
#define  OHCI_USB_SUSPEND       0xc0
#define OHCI_CTL_IR         (1<<8)          /* Interrupt Routing (host/SMI). */
#define OHCI_CTL_RWC        (1<<9)          /* Remote Wakeup Connected. */
#define OHCI_CTL_RWE        (1<<10)         /* Remote Wakeup Enabled. */

#define OHCI_STATUS_HCR     (1<<0)          /* Host Controller Reset. */
#define OHCI_STATUS_CLF     (1<<1)          /* Control List Filled. */
#define OHCI_STATUS_BLF     (1<<2)          /* Bulk List Filled. */
#define OHCI_STATUS_OCR     (1<<3)          /* Ownership Change Request. */
#define OHCI_STATUS_SOC     ((1<<6)|(1<<7)) /* Scheduling Overrun Count. */

/** @name Interrupt Status and Enabled/Disabled Flags
 * @{ */
/** SO  - Scheduling overrun. */
#define OHCI_INTR_SCHEDULING_OVERRUN        RT_BIT(0)
/** WDH - HcDoneHead writeback. */
#define OHCI_INTR_WRITE_DONE_HEAD           RT_BIT(1)
/** SF  - Start of frame. */
#define OHCI_INTR_START_OF_FRAME            RT_BIT(2)
/** RD  - Resume detect. */
#define OHCI_INTR_RESUME_DETECT             RT_BIT(3)
/** UE  - Unrecoverable error. */
#define OHCI_INTR_UNRECOVERABLE_ERROR       RT_BIT(4)
/** FNO - Frame number overflow. */
#define OHCI_INTR_FRAMENUMBER_OVERFLOW      RT_BIT(5)
/** RHSC- Root hub status change. */
#define OHCI_INTR_ROOT_HUB_STATUS_CHANGE    RT_BIT(6)
/** OC  - Ownership change. */
#define OHCI_INTR_OWNERSHIP_CHANGE          RT_BIT(30)
/** MIE - Master interrupt enable. */
#define OHCI_INTR_MASTER_INTERRUPT_ENABLED  RT_BIT(31)
/** @} */

#define OHCI_HCCA_SIZE      0x100
#define OHCI_HCCA_MASK      UINT32_C(0xffffff00)

#define OHCI_FMI_FI         UINT32_C(0x00003fff)    /* Frame Interval. */
#define OHCI_FMI_FSMPS      UINT32_C(0x7fff0000)    /* Full-Speed Max Packet Size. */
#define OHCI_FMI_FSMPS_SHIFT 16
#define OHCI_FMI_FIT        UINT32_C(0x80000000)    /* Frame Interval Toggle. */
#define OHCI_FMI_FIT_SHIFT  31

#define OHCI_FR_FRT         RT_BIT_32(31)           /* Frame Remaining Toggle */

#define OHCI_LS_THRESH      0x628                   /* Low-Speed Threshold. */

#define OHCI_RHA_NDP        (0xff)                  /* Number of Downstream Ports. */
#define OHCI_RHA_PSM        RT_BIT_32(8)            /* Power Switching Mode. */
#define OHCI_RHA_NPS        RT_BIT_32(9)            /* No Power Switching. */
#define OHCI_RHA_DT         RT_BIT_32(10)           /* Device Type. */
#define OHCI_RHA_OCPM       RT_BIT_32(11)           /* Over-Current Protection Mode. */
#define OHCI_RHA_NOCP       RT_BIT_32(12)           /* No Over-Current Protection. */
#define OHCI_RHA_POTPGP     UINT32_C(0xff000000)    /* Power On To Power Good Time. */

#define OHCI_RHS_LPS        RT_BIT_32(0)            /* Local Power Status. */
#define OHCI_RHS_OCI        RT_BIT_32(1)            /* Over-Current Indicator. */
#define OHCI_RHS_DRWE       RT_BIT_32(15)           /* Device Remote Wakeup Enable. */
#define OHCI_RHS_LPSC       RT_BIT_32(16)           /* Local Power Status Change. */
#define OHCI_RHS_OCIC       RT_BIT_32(17)           /* Over-Current Indicator Change. */
#define OHCI_RHS_CRWE       RT_BIT_32(31)           /* Clear Remote Wakeup Enable. */

/** @name HcRhPortStatus[n] - RH Port Status register (read).
 * @{ */
/** CCS - CurrentConnectionStatus - 0 = no device, 1 = device. */
#define OHCI_PORT_CCS       RT_BIT(0)
/** ClearPortEnable (when writing CCS). */
#define OHCI_PORT_CLRPE     OHCI_PORT_CCS
/** PES - PortEnableStatus. */
#define OHCI_PORT_PES       RT_BIT(1)
/** PSS - PortSuspendStatus */
#define OHCI_PORT_PSS       RT_BIT(2)
/** POCI- PortOverCurrentIndicator. */
#define OHCI_PORT_POCI      RT_BIT(3)
/** ClearSuspendStatus (when writing POCI). */
#define OHCI_PORT_CLRSS     OHCI_PORT_POCI
/** PRS - PortResetStatus */
#define OHCI_PORT_PRS       RT_BIT(4)
/** PPS - PortPowerStatus */
#define OHCI_PORT_PPS       RT_BIT(8)
/** LSDA - LowSpeedDeviceAttached */
#define OHCI_PORT_LSDA      RT_BIT(9)
/** ClearPortPower (when writing LSDA). */
#define OHCI_PORT_CLRPP     OHCI_PORT_LSDA
/** CSC  - ConnectStatusChange */
#define OHCI_PORT_CSC       RT_BIT(16)
/** PESC - PortEnableStatusChange */
#define OHCI_PORT_PESC      RT_BIT(17)
/** PSSC - PortSuspendStatusChange */
#define OHCI_PORT_PSSC      RT_BIT(18)
/** OCIC - OverCurrentIndicatorChange */
#define OHCI_PORT_OCIC      RT_BIT(19)
/** PRSC - PortResetStatusChange */
#define OHCI_PORT_PRSC      RT_BIT(20)
/** The mask of RW1C bits. */
#define OHCI_PORT_CLEAR_CHANGE_MASK     (OHCI_PORT_CSC | OHCI_PORT_PESC | OHCI_PORT_PSSC | OHCI_PORT_OCIC | OHCI_PORT_PRSC)
/** @} */


#ifndef VBOX_DEVICE_STRUCT_TESTCASE

#ifdef VBOX_WITH_OHCI_PHYS_READ_STATS
/*
 * Explain
 */
typedef struct OHCIDESCREADSTATS
{
    uint32_t cReads;
    uint32_t cPageChange;
    uint32_t cMinReadsPerPage;
    uint32_t cMaxReadsPerPage;

    uint32_t cReadsLastPage;
    uint32_t u32LastPageAddr;
} OHCIDESCREADSTATS;
typedef OHCIDESCREADSTATS *POHCIDESCREADSTATS;

typedef struct OHCIPHYSREADSTATS
{
    OHCIDESCREADSTATS ed;
    OHCIDESCREADSTATS td;
    OHCIDESCREADSTATS all;

    uint32_t cCrossReads;
    uint32_t cCacheReads;
    uint32_t cPageReads;
} OHCIPHYSREADSTATS;
typedef OHCIPHYSREADSTATS *POHCIPHYSREADSTATS;
typedef OHCIPHYSREADSTATS const *PCOHCIPHYSREADSTATS;
#endif /* VBOX_WITH_OHCI_PHYS_READ_STATS */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if defined(VBOX_WITH_OHCI_PHYS_READ_STATS) && defined(IN_RING3)
static OHCIPHYSREADSTATS g_PhysReadState;
#endif

#if defined(LOG_ENABLED) && defined(IN_RING3)
static bool g_fLogBulkEPs = false;
static bool g_fLogControlEPs = false;
static bool g_fLogInterruptEPs = false;
#endif
#ifdef IN_RING3
/**
 * SSM descriptor table for the OHCI structure.
 */
static SSMFIELD const g_aOhciFields[] =
{
    SSMFIELD_ENTRY(         OHCI, SofTime),
    SSMFIELD_ENTRY_CUSTOM(        dpic+fno, RT_OFFSETOF(OHCI, SofTime) + RT_SIZEOFMEMB(OHCI, SofTime), 4),
    SSMFIELD_ENTRY(         OHCI, RootHub.status),
    SSMFIELD_ENTRY(         OHCI, RootHub.desc_a),
    SSMFIELD_ENTRY(         OHCI, RootHub.desc_b),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[0].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[1].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[2].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[3].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[4].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[5].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[6].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[7].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[8].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[9].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[10].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[11].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[12].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[13].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[14].fReg),
    SSMFIELD_ENTRY(         OHCI, ctl),
    SSMFIELD_ENTRY(         OHCI, status),
    SSMFIELD_ENTRY(         OHCI, intr_status),
    SSMFIELD_ENTRY(         OHCI, intr),
    SSMFIELD_ENTRY(         OHCI, hcca),
    SSMFIELD_ENTRY(         OHCI, per_cur),
    SSMFIELD_ENTRY(         OHCI, ctrl_cur),
    SSMFIELD_ENTRY(         OHCI, ctrl_head),
    SSMFIELD_ENTRY(         OHCI, bulk_cur),
    SSMFIELD_ENTRY(         OHCI, bulk_head),
    SSMFIELD_ENTRY(         OHCI, done),
    SSMFIELD_ENTRY_CUSTOM(        fsmps+fit+fi+frt, RT_OFFSETOF(OHCI, done) + RT_SIZEOFMEMB(OHCI, done), 4),
    SSMFIELD_ENTRY(         OHCI, HcFmNumber),
    SSMFIELD_ENTRY(         OHCI, pstart),
    SSMFIELD_ENTRY_TERM()
};

/**
 * SSM descriptor table for the older 8-port OHCI structure.
 */
static SSMFIELD const g_aOhciFields8Ports[] =
{
    SSMFIELD_ENTRY(         OHCI, SofTime),
    SSMFIELD_ENTRY_CUSTOM(        dpic+fno, RT_OFFSETOF(OHCI, SofTime) + RT_SIZEOFMEMB(OHCI, SofTime), 4),
    SSMFIELD_ENTRY(         OHCI, RootHub.status),
    SSMFIELD_ENTRY(         OHCI, RootHub.desc_a),
    SSMFIELD_ENTRY(         OHCI, RootHub.desc_b),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[0].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[1].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[2].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[3].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[4].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[5].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[6].fReg),
    SSMFIELD_ENTRY(         OHCI, RootHub.aPorts[7].fReg),
    SSMFIELD_ENTRY(         OHCI, ctl),
    SSMFIELD_ENTRY(         OHCI, status),
    SSMFIELD_ENTRY(         OHCI, intr_status),
    SSMFIELD_ENTRY(         OHCI, intr),
    SSMFIELD_ENTRY(         OHCI, hcca),
    SSMFIELD_ENTRY(         OHCI, per_cur),
    SSMFIELD_ENTRY(         OHCI, ctrl_cur),
    SSMFIELD_ENTRY(         OHCI, ctrl_head),
    SSMFIELD_ENTRY(         OHCI, bulk_cur),
    SSMFIELD_ENTRY(         OHCI, bulk_head),
    SSMFIELD_ENTRY(         OHCI, done),
    SSMFIELD_ENTRY_CUSTOM(        fsmps+fit+fi+frt, RT_OFFSETOF(OHCI, done) + RT_SIZEOFMEMB(OHCI, done), 4),
    SSMFIELD_ENTRY(         OHCI, HcFmNumber),
    SSMFIELD_ENTRY(         OHCI, pstart),
    SSMFIELD_ENTRY_TERM()
};
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
#ifdef IN_RING3
/* Update host controller state to reflect a device attach */
static void                 ohciR3RhPortPower(POHCIROOTHUBR3 pRh, unsigned iPort, bool fPowerUp);
static void                 ohciR3BusResume(PPDMDEVINS pDevIns, POHCI pOhci, POHCICC pThisCC, bool fHardware);
static void                 ohciR3BusStop(POHCICC pThisCC);
#ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE
static void                 ohciR3PhysReadCacheInvalidate(POHCIPAGECACHE pPageCache);
#endif

static DECLCALLBACK(void)   ohciR3RhXferCompletion(PVUSBIROOTHUBPORT pInterface, PVUSBURB pUrb);
static DECLCALLBACK(bool)   ohciR3RhXferError(PVUSBIROOTHUBPORT pInterface, PVUSBURB pUrb);

static bool                 ohciR3IsTdInFlight(POHCICC pThisCC, uint32_t GCPhysTD);
static int                  ohciR3InFlightFind(POHCICC pThisCC, uint32_t GCPhysTD);
# if defined(VBOX_STRICT) || defined(LOG_ENABLED)
static int                  ohciR3InDoneQueueFind(POHCICC pThisCC, uint32_t GCPhysTD);
# endif
#endif /* IN_RING3 */
RT_C_DECLS_END


/**
 * Update PCI IRQ levels
 */
static void ohciUpdateInterruptLocked(PPDMDEVINS pDevIns, POHCI ohci, const char *msg)
{
    int level = 0;

    if (    (ohci->intr & OHCI_INTR_MASTER_INTERRUPT_ENABLED)
        &&  (ohci->intr_status & ohci->intr)
        && !(ohci->ctl & OHCI_CTL_IR))
        level = 1;

    PDMDevHlpPCISetIrq(pDevIns, 0, level);
    if (level)
    {
        uint32_t val = ohci->intr_status & ohci->intr;
        Log2(("ohci: Fired off interrupt %#010x - SO=%d WDH=%d SF=%d RD=%d UE=%d FNO=%d RHSC=%d OC=%d - %s\n",
              val, val & 1, (val >> 1) & 1, (val >> 2) & 1, (val >> 3) & 1, (val >> 4) & 1, (val >> 5) & 1,
              (val >> 6) & 1, (val >> 30) & 1, msg)); NOREF(val); NOREF(msg);
    }
}

#ifdef IN_RING3

/**
 * Set an interrupt, use the wrapper ohciSetInterrupt.
 */
DECLINLINE(int) ohciR3SetInterruptInt(PPDMDEVINS pDevIns, POHCI ohci, int rcBusy, uint32_t intr, const char *msg)
{
    int rc = PDMDevHlpCritSectEnter(pDevIns, &ohci->CsIrq, rcBusy);
    if (rc != VINF_SUCCESS)
        return rc;

    if ( (ohci->intr_status & intr) != intr )
    {
        ohci->intr_status |= intr;
        ohciUpdateInterruptLocked(pDevIns, ohci, msg);
    }

    PDMDevHlpCritSectLeave(pDevIns, &ohci->CsIrq);
    return rc;
}

/**
 * Set an interrupt wrapper macro for logging purposes.
 */
# define ohciR3SetInterrupt(a_pDevIns, a_pOhci, a_fIntr) \
    ohciR3SetInterruptInt(a_pDevIns, a_pOhci, VERR_IGNORED, a_fIntr, #a_fIntr)


/**
 * Sets the HC in the unrecoverable error state and raises the appropriate interrupt.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The OHCI instance.
 * @param   iCode               Diagnostic code.
 */
DECLINLINE(void) ohciR3RaiseUnrecoverableError(PPDMDEVINS pDevIns, POHCI pThis, int iCode)
{
    LogRelMax(10, ("OHCI#%d: Raising unrecoverable error (%d)\n", pDevIns->iInstance, iCode));
    ohciR3SetInterrupt(pDevIns, pThis, OHCI_INTR_UNRECOVERABLE_ERROR);
}


/* Carry out a hardware remote wakeup */
static void ohciR3RemoteWakeup(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC)
{
    if ((pThis->ctl & OHCI_CTL_HCFS) != OHCI_USB_SUSPEND)
        return;
    if (!(pThis->RootHub.status & OHCI_RHS_DRWE))
        return;
    ohciR3BusResume(pDevIns, pThis, pThisCC, true /* hardware */);
}


/**
 * Query interface method for the roothub LUN.
 */
static DECLCALLBACK(void *) ohciR3RhQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    POHCICC pThisCC = RT_FROM_MEMBER(pInterface, OHCICC, RootHub.IBase);
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
static DECLCALLBACK(int) ohciR3RhQueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    POHCICC pThisCC = RT_FROM_MEMBER(pInterface, OHCICC, RootHub.ILeds);
    if (iLUN == 0)
    {
        *ppLed = &pThisCC->RootHub.Led;
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}


/** Converts a OHCI.roothub.IRhPort pointer to a OHCICC one. */
#define VUSBIROOTHUBPORT_2_OHCI(a_pInterface) RT_FROM_MEMBER(a_pInterface, OHCICC, RootHub.IRhPort)

/**
 * Get the number of available ports in the hub.
 *
 * @returns The number of ports available.
 * @param   pInterface      Pointer to this structure.
 * @param   pAvailable      Bitmap indicating the available ports. Set bit == available port.
 */
static DECLCALLBACK(unsigned) ohciR3RhGetAvailablePorts(PVUSBIROOTHUBPORT pInterface, PVUSBPORTBITMAP pAvailable)
{
    POHCICC    pThisCC = VUSBIROOTHUBPORT_2_OHCI(pInterface);
    PPDMDEVINS pDevIns = pThisCC->pDevInsR3;
    POHCI      pThis   = PDMDEVINS_2_DATA(pDevIns, POHCI);
    unsigned   cPorts  = 0;

    memset(pAvailable, 0, sizeof(*pAvailable));

    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);


    for (unsigned iPort = 0; iPort < OHCI_NDP_CFG(pThis); iPort++)
        if (!pThis->RootHub.aPorts[iPort].fAttached)
        {
            cPorts++;
            ASMBitSet(pAvailable, iPort + 1);
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
static DECLCALLBACK(uint32_t) ohciR3RhGetUSBVersions(PVUSBIROOTHUBPORT pInterface)
{
    RT_NOREF(pInterface);
    return VUSB_STDVER_11;
}


/** @interface_method_impl{VUSBIROOTHUBPORT,pfnAttach} */
static DECLCALLBACK(int) ohciR3RhAttach(PVUSBIROOTHUBPORT pInterface, uint32_t uPort, VUSBSPEED enmSpeed)
{
    POHCICC    pThisCC = VUSBIROOTHUBPORT_2_OHCI(pInterface);
    PPDMDEVINS pDevIns = pThisCC->pDevInsR3;
    POHCI      pThis   = PDMDEVINS_2_DATA(pDevIns, POHCI);
    LogFlow(("ohciR3RhAttach: uPort=%u\n", uPort));
    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    /*
     * Validate and adjust input.
     */
    Assert(uPort >= 1 && uPort <= OHCI_NDP_CFG(pThis));
    uPort--;
    Assert(!pThis->RootHub.aPorts[uPort].fAttached);
    /* Only LS/FS devices should end up here. */
    Assert(enmSpeed == VUSB_SPEED_LOW || enmSpeed == VUSB_SPEED_FULL);

    /*
     * Attach it.
     */
    pThis->RootHub.aPorts[uPort].fReg = OHCI_PORT_CCS | OHCI_PORT_CSC;
    if (enmSpeed == VUSB_SPEED_LOW)
        pThis->RootHub.aPorts[uPort].fReg |= OHCI_PORT_LSDA;
    pThis->RootHub.aPorts[uPort].fAttached = true;
    ohciR3RhPortPower(&pThisCC->RootHub, uPort, 1 /* power on */);

    ohciR3RemoteWakeup(pDevIns, pThis, pThisCC);
    ohciR3SetInterrupt(pDevIns, pThis, OHCI_INTR_ROOT_HUB_STATUS_CHANGE);

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return VINF_SUCCESS;
}


/**
 * A device is being detached from a port in the roothub.
 *
 * @param   pInterface      Pointer to this structure.
 * @param   uPort           The port number assigned to the device.
 */
static DECLCALLBACK(void) ohciR3RhDetach(PVUSBIROOTHUBPORT pInterface, uint32_t uPort)
{
    POHCICC    pThisCC = VUSBIROOTHUBPORT_2_OHCI(pInterface);
    PPDMDEVINS pDevIns = pThisCC->pDevInsR3;
    POHCI      pThis   = PDMDEVINS_2_DATA(pDevIns, POHCI);
    LogFlow(("ohciR3RhDetach: uPort=%u\n", uPort));
    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    /*
     * Validate and adjust input.
     */
    Assert(uPort >= 1 && uPort <= OHCI_NDP_CFG(pThis));
    uPort--;
    Assert(pThis->RootHub.aPorts[uPort].fAttached);

    /*
     * Detach it.
     */
    pThis->RootHub.aPorts[uPort].fAttached = false;
    if (pThis->RootHub.aPorts[uPort].fReg & OHCI_PORT_PES)
        pThis->RootHub.aPorts[uPort].fReg = OHCI_PORT_CSC | OHCI_PORT_PESC;
    else
        pThis->RootHub.aPorts[uPort].fReg = OHCI_PORT_CSC;

    ohciR3RemoteWakeup(pDevIns, pThis, pThisCC);
    ohciR3SetInterrupt(pDevIns, pThis, OHCI_INTR_ROOT_HUB_STATUS_CHANGE);

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
 * @param uPort     The port of the device completing the reset.
 * @param rc        The result of the operation.
 * @param pvUser    Pointer to the controller.
 */
static DECLCALLBACK(void) ohciR3RhResetDoneOneDev(PVUSBIDEVICE pDev, uint32_t uPort, int rc, void *pvUser)
{
    LogRel(("OHCI: root hub reset completed with %Rrc\n", rc));
    RT_NOREF(pDev, uPort, rc, pvUser);
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
static DECLCALLBACK(int) ohciR3RhReset(PVUSBIROOTHUBPORT pInterface, bool fResetOnLinux)
{
    POHCICC    pThisCC = VUSBIROOTHUBPORT_2_OHCI(pInterface);
    PPDMDEVINS pDevIns = pThisCC->pDevInsR3;
    POHCI      pThis   = PDMDEVINS_2_DATA(pDevIns, POHCI);
    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    Log(("ohci: root hub reset%s\n", fResetOnLinux ? " (reset on linux)" : ""));

    pThis->RootHub.status = 0;
    pThis->RootHub.desc_a = OHCI_RHA_NPS | OHCI_NDP_CFG(pThis); /* Preserve NDP value. */
    pThis->RootHub.desc_b = 0x0; /* Impl. specific */

    /*
     * We're pending to _reattach_ the device without resetting them.
     * Except, during VM reset where we use the opportunity to do a proper
     * reset before the guest comes along and expect things.
     *
     * However, it's very very likely that we're not doing the right thing
     * here if coming from the guest (USB Reset state). The docs talks about
     * root hub resetting, however what exact behaviour in terms of root hub
     * status and changed bits, and HC interrupts aren't stated clearly. IF we
     * get trouble and see the guest doing "USB Resets" we will have to look
     * into this. For the time being we stick with simple.
     */
    for (unsigned iPort = 0; iPort < OHCI_NDP_CFG(pThis); iPort++)
    {
        if (pThis->RootHub.aPorts[iPort].fAttached)
        {
            pThis->RootHub.aPorts[iPort].fReg = OHCI_PORT_CCS | OHCI_PORT_CSC | OHCI_PORT_PPS;
            if (fResetOnLinux)
            {
                PVM pVM = PDMDevHlpGetVM(pDevIns);
                VUSBIRhDevReset(pThisCC->RootHub.pIRhConn, OHCI_PORT_2_VUSB_PORT(iPort), fResetOnLinux,
                                ohciR3RhResetDoneOneDev, pThis, pVM);
            }
        }
        else
            pThis->RootHub.aPorts[iPort].fReg = 0;
    }
    ohciR3SetInterrupt(pDevIns, pThis, OHCI_INTR_ROOT_HUB_STATUS_CHANGE);

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return VINF_SUCCESS;
}


/**
 * Does a software or hardware reset of the controller.
 *
 * This is called in response to setting HcCommandStatus.HCR, hardware reset,
 * and device construction.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The ohci instance data.
 * @param   pThisCC         The ohci instance data, current context.
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
static void ohciR3DoReset(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC, uint32_t fNewMode, bool fResetOnLinux)
{
    Log(("ohci: %s reset%s\n", fNewMode == OHCI_USB_RESET ? "hardware" : "software",
         fResetOnLinux ? " (reset on linux)" : ""));

    /* Clear list enable bits first, so that any processing currently in progress terminates quickly. */
    pThis->ctl &= ~(OHCI_CTL_BLE | OHCI_CTL_CLE | OHCI_CTL_PLE);

    /* Stop the bus in any case, disabling walking the lists. */
    ohciR3BusStop(pThisCC);

    /*
     * Cancel all outstanding URBs.
     *
     * We can't, and won't, deal with URBs until we're moved out of the
     * suspend/reset state. Also, a real HC isn't going to send anything
     * any more when a reset has been signaled.
     */
    pThisCC->RootHub.pIRhConn->pfnCancelAllUrbs(pThisCC->RootHub.pIRhConn);
    Assert(pThisCC->cInFlight == 0);

    /*
     * Reset the hardware registers.
     */
    if (fNewMode == OHCI_USB_RESET)
        pThis->ctl  = OHCI_CTL_RWC;                     /* We're the firmware, set RemoteWakeupConnected. */
    else
        pThis->ctl &= OHCI_CTL_IR | OHCI_CTL_RWC;       /* IR and RWC are preserved on software reset. */

    /* Clear the HCFS bits first to make setting the new state work. */
    pThis->ctl &= ~OHCI_CTL_HCFS;
    pThis->ctl |= fNewMode;
    pThis->status = 0;
    pThis->intr_status = 0;
    pThis->intr = 0;
    PDMDevHlpPCISetIrq(pDevIns, 0, 0);

    pThis->hcca = 0;
    pThis->per_cur = 0;
    pThis->ctrl_head = pThis->ctrl_cur = 0;
    pThis->bulk_head = pThis->bulk_cur = 0;
    pThis->done = 0;

    pThis->fsmps = 0x2778;                              /* To-Be-Defined, use the value linux sets...*/
    pThis->fit = 0;
    pThis->fi = 11999;                                  /* (12MHz ticks, one frame is 1ms) */
    pThis->frt = 0;
    pThis->HcFmNumber = 0;
    pThis->pstart = 0;

    pThis->dqic = 0x7;
    pThis->fno = 0;

#ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE
    ohciR3PhysReadCacheInvalidate(&pThisCC->CacheED);
    ohciR3PhysReadCacheInvalidate(&pThisCC->CacheTD);
#endif

    /*
     * If this is a hardware reset, we will initialize the root hub too.
     * Software resets doesn't do this according to the specs.
     * (It's not possible to have device connected at the time of the
     * device construction, so nothing to worry about there.)
     */
    if (fNewMode == OHCI_USB_RESET)
        pThisCC->RootHub.pIRhConn->pfnReset(pThisCC->RootHub.pIRhConn, fResetOnLinux);
}


/**
 * Reads physical memory.
 */
DECLINLINE(void) ohciR3PhysRead(PPDMDEVINS pDevIns, uint32_t Addr, void *pvBuf, size_t cbBuf)
{
    if (cbBuf)
        PDMDevHlpPCIPhysReadUser(pDevIns, Addr, pvBuf, cbBuf);
}

/**
 * Reads physical memory - metadata.
 */
DECLINLINE(void) ohciR3PhysReadMeta(PPDMDEVINS pDevIns, uint32_t Addr, void *pvBuf, size_t cbBuf)
{
    if (cbBuf)
        PDMDevHlpPCIPhysReadMeta(pDevIns, Addr, pvBuf, cbBuf);
}

/**
 * Writes physical memory.
 */
DECLINLINE(void) ohciR3PhysWrite(PPDMDEVINS pDevIns, uint32_t Addr, const void *pvBuf, size_t cbBuf)
{
    if (cbBuf)
        PDMDevHlpPCIPhysWriteUser(pDevIns, Addr, pvBuf, cbBuf);
}

/**
 * Writes physical memory - metadata.
 */
DECLINLINE(void) ohciR3PhysWriteMeta(PPDMDEVINS pDevIns, uint32_t Addr, const void *pvBuf, size_t cbBuf)
{
    if (cbBuf)
        PDMDevHlpPCIPhysWriteMeta(pDevIns, Addr, pvBuf, cbBuf);
}

/**
 * Read an array of dwords from physical memory and correct endianness.
 */
DECLINLINE(void) ohciR3GetDWords(PPDMDEVINS pDevIns, uint32_t Addr, uint32_t *pau32s, int c32s)
{
    ohciR3PhysReadMeta(pDevIns, Addr, pau32s, c32s * sizeof(uint32_t));
# ifndef RT_LITTLE_ENDIAN
    for(int i = 0; i < c32s; i++)
        pau32s[i] = RT_H2LE_U32(pau32s[i]);
# endif
}

/**
 * Write an array of dwords from physical memory and correct endianness.
 */
DECLINLINE(void) ohciR3PutDWords(PPDMDEVINS pDevIns, uint32_t Addr, const uint32_t *pau32s, int cu32s)
{
# ifdef RT_LITTLE_ENDIAN
    ohciR3PhysWriteMeta(pDevIns, Addr, pau32s, cu32s << 2);
# else
    for (int i = 0; i < c32s; i++, pau32s++, Addr += sizeof(*pau32s))
    {
        uint32_t u32Tmp = RT_H2LE_U32(*pau32s);
        ohciR3PhysWriteMeta(pDevIns, Addr, (uint8_t *)&u32Tmp, sizeof(u32Tmp));
    }
# endif
}



# ifdef VBOX_WITH_OHCI_PHYS_READ_STATS

static void descReadStatsReset(POHCIDESCREADSTATS p)
{
    p->cReads = 0;
    p->cPageChange = 0;
    p->cMinReadsPerPage = UINT32_MAX;
    p->cMaxReadsPerPage = 0;

    p->cReadsLastPage = 0;
    p->u32LastPageAddr = 0;
}

static void physReadStatsReset(POHCIPHYSREADSTATS p)
{
    descReadStatsReset(&p->ed);
    descReadStatsReset(&p->td);
    descReadStatsReset(&p->all);

    p->cCrossReads = 0;
    p->cCacheReads = 0;
    p->cPageReads = 0;
}

static void physReadStatsUpdateDesc(POHCIDESCREADSTATS p, uint32_t u32Addr)
{
    const uint32_t u32PageAddr = u32Addr & ~UINT32_C(0xFFF);

    ++p->cReads;

    if (p->u32LastPageAddr == 0)
    {
       /* First call. */
       ++p->cReadsLastPage;
       p->u32LastPageAddr = u32PageAddr;
    }
    else if (u32PageAddr != p->u32LastPageAddr)
    {
       /* New page. */
       ++p->cPageChange;

       p->cMinReadsPerPage = RT_MIN(p->cMinReadsPerPage, p->cReadsLastPage);
       p->cMaxReadsPerPage = RT_MAX(p->cMaxReadsPerPage, p->cReadsLastPage);;

       p->cReadsLastPage = 1;
       p->u32LastPageAddr = u32PageAddr;
    }
    else
    {
        /* Read on the same page. */
       ++p->cReadsLastPage;
    }
}

static void physReadStatsPrint(POHCIPHYSREADSTATS p)
{
    p->ed.cMinReadsPerPage = RT_MIN(p->ed.cMinReadsPerPage, p->ed.cReadsLastPage);
    p->ed.cMaxReadsPerPage = RT_MAX(p->ed.cMaxReadsPerPage, p->ed.cReadsLastPage);;

    p->td.cMinReadsPerPage = RT_MIN(p->td.cMinReadsPerPage, p->td.cReadsLastPage);
    p->td.cMaxReadsPerPage = RT_MAX(p->td.cMaxReadsPerPage, p->td.cReadsLastPage);;

    p->all.cMinReadsPerPage = RT_MIN(p->all.cMinReadsPerPage, p->all.cReadsLastPage);
    p->all.cMaxReadsPerPage = RT_MAX(p->all.cMaxReadsPerPage, p->all.cReadsLastPage);;

    LogRel(("PHYSREAD:\n"
            "  ED: %d, %d, %d/%d\n"
            "  TD: %d, %d, %d/%d\n"
            " ALL: %d, %d, %d/%d\n"
            "   C: %d, %d, %d\n"
            "",
            p->ed.cReads, p->ed.cPageChange, p->ed.cMinReadsPerPage, p->ed.cMaxReadsPerPage,
            p->td.cReads, p->td.cPageChange, p->td.cMinReadsPerPage, p->td.cMaxReadsPerPage,
            p->all.cReads, p->all.cPageChange, p->all.cMinReadsPerPage, p->all.cMaxReadsPerPage,
            p->cCrossReads, p->cCacheReads, p->cPageReads
          ));

    physReadStatsReset(p);
}

# endif /* VBOX_WITH_OHCI_PHYS_READ_STATS */
# ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE

static void ohciR3PhysReadCacheInvalidate(POHCIPAGECACHE pPageCache)
{
    pPageCache->GCPhysReadCacheAddr = NIL_RTGCPHYS;
}

static void ohciR3PhysReadCacheRead(PPDMDEVINS pDevIns, POHCIPAGECACHE pPageCache, RTGCPHYS GCPhys, void *pvBuf, size_t cbBuf)
{
    const RTGCPHYS PageAddr = GCPhys & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;

    if (PageAddr == ((GCPhys + cbBuf) & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK))
    {
        if (PageAddr != pPageCache->GCPhysReadCacheAddr)
        {
            PDMDevHlpPCIPhysRead(pDevIns, PageAddr, pPageCache->abPhysReadCache, sizeof(pPageCache->abPhysReadCache));
            pPageCache->GCPhysReadCacheAddr = PageAddr;
#  ifdef VBOX_WITH_OHCI_PHYS_READ_STATS
            ++g_PhysReadState.cPageReads;
#  endif
        }

        memcpy(pvBuf, &pPageCache->abPhysReadCache[GCPhys & GUEST_PAGE_OFFSET_MASK], cbBuf);
#  ifdef VBOX_WITH_OHCI_PHYS_READ_STATS
        ++g_PhysReadState.cCacheReads;
#  endif
    }
    else
    {
        PDMDevHlpPCIPhysRead(pDevIns, GCPhys, pvBuf, cbBuf);
#  ifdef VBOX_WITH_OHCI_PHYS_READ_STATS
        ++g_PhysReadState.cCrossReads;
#  endif
    }
}


/**
 * Updates the data in the given page cache if the given guest physical address is currently contained
 * in the cache.
 *
 * @param   pPageCache  The page cache to update.
 * @param   GCPhys      The guest physical address needing the update.
 * @param   pvBuf       Pointer to the buffer to update the page cache with.
 * @param   cbBuf       Number of bytes to update.
 */
static void ohciR3PhysCacheUpdate(POHCIPAGECACHE pPageCache, RTGCPHYS GCPhys, const void *pvBuf, size_t cbBuf)
{
    const RTGCPHYS GCPhysPage = GCPhys & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;

    if (GCPhysPage == pPageCache->GCPhysReadCacheAddr)
    {
        uint32_t offPage = GCPhys & GUEST_PAGE_OFFSET_MASK;
        memcpy(&pPageCache->abPhysReadCache[offPage], pvBuf, RT_MIN(GUEST_PAGE_SIZE - offPage, cbBuf));
    }
}

/**
 * Update any cached ED data with the given endpoint descriptor at the given address.
 *
 * @param   pThisCC     The OHCI instance data for the current context.
 * @param   EdAddr      Endpoint descriptor address.
 * @param   pEd         The endpoint descriptor which got updated.
 */
DECLINLINE(void) ohciR3CacheEdUpdate(POHCICC pThisCC, RTGCPHYS32 EdAddr, PCOHCIED pEd)
{
    ohciR3PhysCacheUpdate(&pThisCC->CacheED, EdAddr + RT_OFFSETOF(OHCIED, HeadP), &pEd->HeadP, sizeof(uint32_t));
}


/**
 * Update any cached TD data with the given transfer descriptor at the given address.
 *
 * @param   pThisCC     The OHCI instance data, current context.
 * @param   TdAddr      Transfer descriptor address.
 * @param   pTd         The transfer descriptor which got updated.
 */
DECLINLINE(void) ohciR3CacheTdUpdate(POHCICC pThisCC, RTGCPHYS32 TdAddr, PCOHCITD pTd)
{
    ohciR3PhysCacheUpdate(&pThisCC->CacheTD, TdAddr, pTd, sizeof(*pTd));
}

# endif /* VBOX_WITH_OHCI_PHYS_READ_CACHE */

/**
 * Reads an OHCIED.
 */
DECLINLINE(void) ohciR3ReadEd(PPDMDEVINS pDevIns, uint32_t EdAddr, POHCIED pEd)
{
# ifdef VBOX_WITH_OHCI_PHYS_READ_STATS
    physReadStatsUpdateDesc(&g_PhysReadState.ed, EdAddr);
    physReadStatsUpdateDesc(&g_PhysReadState.all, EdAddr);
# endif
#ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE
    POHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCICC);
    ohciR3PhysReadCacheRead(pDevIns, &pThisCC->CacheED, EdAddr, pEd, sizeof(*pEd));
#else
    ohciR3GetDWords(pDevIns, EdAddr, (uint32_t *)pEd, sizeof(*pEd) >> 2);
#endif
}

/**
 * Reads an OHCITD.
 */
DECLINLINE(void) ohciR3ReadTd(PPDMDEVINS pDevIns, uint32_t TdAddr, POHCITD pTd)
{
# ifdef VBOX_WITH_OHCI_PHYS_READ_STATS
    physReadStatsUpdateDesc(&g_PhysReadState.td, TdAddr);
    physReadStatsUpdateDesc(&g_PhysReadState.all, TdAddr);
# endif
#ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE
    POHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCICC);
    ohciR3PhysReadCacheRead(pDevIns, &pThisCC->CacheTD, TdAddr, pTd, sizeof(*pTd));
#else
    ohciR3GetDWords(pDevIns, TdAddr, (uint32_t *)pTd, sizeof(*pTd) >> 2);
#endif
# ifdef LOG_ENABLED
    if (LogIs3Enabled())
    {
        uint32_t hichg;
        hichg = pTd->hwinfo;
        Log3(("ohciR3ReadTd(,%#010x,): R=%d DP=%d DI=%d T=%d EC=%d CC=%#x CBP=%#010x NextTD=%#010x BE=%#010x UNK=%#x\n",
              TdAddr,
              (pTd->hwinfo >> 18) & 1,
              (pTd->hwinfo >> 19) & 3,
              (pTd->hwinfo >> 21) & 7,
              (pTd->hwinfo >> 24) & 3,
              (pTd->hwinfo >> 26) & 3,
              (pTd->hwinfo >> 28) &15,
              pTd->cbp,
              pTd->NextTD,
              pTd->be,
              pTd->hwinfo & TD_HWINFO_UNKNOWN_MASK));
#  if 0
        if (LogIs3Enabled())
        {
            /*
             * usbohci.sys (32-bit XP) allocates 0x80 bytes per TD:
             *  0x00-0x0f is the OHCI TD.
             *  0x10-0x1f for isochronous TDs
             *  0x20 is the physical address of this TD.
             *  0x24 is initialized with 0x64745948, probably a magic.
             *  0x28 is some kind of flags. the first bit begin the allocated / not allocated indicator.
             *  0x30 is a pointer to something. endpoint? interface? device?
             *  0x38 is initialized to 0xdeadface. but is changed into a pointer or something.
             *  0x40 looks like a pointer.
             * The rest is unknown and initialized with zeros.
             */
            uint8_t abXpTd[0x80];
            ohciR3PhysRead(pDevIns, TdAddr, abXpTd, sizeof(abXpTd));
            Log3(("WinXpTd: alloc=%d PhysSelf=%RX32 s2=%RX32 magic=%RX32 s4=%RX32 s5=%RX32\n"
                  "%.*Rhxd\n",
                  abXpTd[28] & RT_BIT(0),
                  *((uint32_t *)&abXpTd[0x20]), *((uint32_t *)&abXpTd[0x30]),
                  *((uint32_t *)&abXpTd[0x24]), *((uint32_t *)&abXpTd[0x38]),
                  *((uint32_t *)&abXpTd[0x40]),
                  sizeof(abXpTd), &abXpTd[0]));
        }
#  endif
    }
# endif
}

/**
 * Reads an OHCIITD.
 */
DECLINLINE(void) ohciR3ReadITd(PPDMDEVINS pDevIns, POHCI pThis, uint32_t ITdAddr, POHCIITD pITd)
{
    ohciR3GetDWords(pDevIns, ITdAddr, (uint32_t *)pITd, sizeof(*pITd) / sizeof(uint32_t));
# ifdef LOG_ENABLED
    if (LogIs3Enabled())
    {
        Log3(("ohciR3ReadITd(,%#010x,): SF=%#06x (%#RX32) DI=%#x FC=%d CC=%#x BP0=%#010x NextTD=%#010x BE=%#010x\n",
              ITdAddr,
              pITd->HwInfo & 0xffff, pThis->HcFmNumber,
              (pITd->HwInfo >> 21) & 7,
              (pITd->HwInfo >> 24) & 7,
              (pITd->HwInfo >> 28) &15,
              pITd->BP0,
              pITd->NextTD,
              pITd->BE));
        Log3(("psw0=%x:%03x psw1=%x:%03x psw2=%x:%03x psw3=%x:%03x psw4=%x:%03x psw5=%x:%03x psw6=%x:%03x psw7=%x:%03x\n",
              pITd->aPSW[0] >> 12, pITd->aPSW[0] & 0xfff,
              pITd->aPSW[1] >> 12, pITd->aPSW[1] & 0xfff,
              pITd->aPSW[2] >> 12, pITd->aPSW[2] & 0xfff,
              pITd->aPSW[3] >> 12, pITd->aPSW[3] & 0xfff,
              pITd->aPSW[4] >> 12, pITd->aPSW[4] & 0xfff,
              pITd->aPSW[5] >> 12, pITd->aPSW[5] & 0xfff,
              pITd->aPSW[6] >> 12, pITd->aPSW[6] & 0xfff,
              pITd->aPSW[7] >> 12, pITd->aPSW[7] & 0xfff));
    }
# else
    RT_NOREF(pThis);
# endif
}


/**
 * Writes an OHCIED.
 */
DECLINLINE(void) ohciR3WriteEd(PPDMDEVINS pDevIns, uint32_t EdAddr, PCOHCIED pEd)
{
# ifdef LOG_ENABLED
    if (LogIs3Enabled())
    {
        OHCIED      EdOld;
        uint32_t    hichg;

        ohciR3GetDWords(pDevIns, EdAddr, (uint32_t *)&EdOld, sizeof(EdOld) >> 2);
        hichg = EdOld.hwinfo ^ pEd->hwinfo;
        Log3(("ohciR3WriteEd(,%#010x,): %sFA=%#x %sEN=%#x %sD=%#x %sS=%d %sK=%d %sF=%d %sMPS=%#x %sTailP=%#010x %sHeadP=%#010x %sH=%d %sC=%d %sNextED=%#010x\n",
              EdAddr,
              (hichg >>  0) & 0x7f ? "*" : "", (pEd->hwinfo >>  0) & 0x7f,
              (hichg >>  7) &  0xf ? "*" : "", (pEd->hwinfo >>  7) &  0xf,
              (hichg >> 11) &    3 ? "*" : "", (pEd->hwinfo >> 11) &    3,
              (hichg >> 13) &    1 ? "*" : "", (pEd->hwinfo >> 13) &    1,
              (hichg >> 14) &    1 ? "*" : "", (pEd->hwinfo >> 14) &    1,
              (hichg >> 15) &    1 ? "*" : "", (pEd->hwinfo >> 15) &    1,
              (hichg >> 24) &0x3ff ? "*" : "", (pEd->hwinfo >> 16) &0x3ff,
              EdOld.TailP != pEd->TailP ? "*" : "", pEd->TailP,
              (EdOld.HeadP & ~3) != (pEd->HeadP & ~3) ? "*" : "", pEd->HeadP & ~3,
              (EdOld.HeadP ^ pEd->HeadP) & 1 ? "*" : "", pEd->HeadP & 1,
              (EdOld.HeadP ^ pEd->HeadP) & 2 ? "*" : "", (pEd->HeadP >> 1) & 1,
              EdOld.NextED != pEd->NextED ? "*" : "", pEd->NextED));
    }
# endif

    ohciR3PutDWords(pDevIns, EdAddr + RT_OFFSETOF(OHCIED, HeadP), &pEd->HeadP, 1);
#ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE
    POHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCICC);
    ohciR3CacheEdUpdate(pThisCC, EdAddr, pEd);
#endif
}


/**
 * Writes an OHCITD.
 */
DECLINLINE(void) ohciR3WriteTd(PPDMDEVINS pDevIns, uint32_t TdAddr, PCOHCITD pTd, const char *pszLogMsg)
{
# ifdef LOG_ENABLED
    if (LogIs3Enabled())
    {
        OHCITD TdOld;
        ohciR3GetDWords(pDevIns, TdAddr, (uint32_t *)&TdOld, sizeof(TdOld) >> 2);
        uint32_t hichg = TdOld.hwinfo ^ pTd->hwinfo;
        Log3(("ohciR3WriteTd(,%#010x,): %sR=%d %sDP=%d %sDI=%#x %sT=%d %sEC=%d %sCC=%#x %sCBP=%#010x %sNextTD=%#010x %sBE=%#010x (%s)\n",
              TdAddr,
              (hichg >> 18) & 1 ? "*" : "", (pTd->hwinfo >> 18) & 1,
              (hichg >> 19) & 3 ? "*" : "", (pTd->hwinfo >> 19) & 3,
              (hichg >> 21) & 7 ? "*" : "", (pTd->hwinfo >> 21) & 7,
              (hichg >> 24) & 3 ? "*" : "", (pTd->hwinfo >> 24) & 3,
              (hichg >> 26) & 3 ? "*" : "", (pTd->hwinfo >> 26) & 3,
              (hichg >> 28) &15 ? "*" : "", (pTd->hwinfo >> 28) &15,
              TdOld.cbp  != pTd->cbp  ? "*" : "", pTd->cbp,
              TdOld.NextTD != pTd->NextTD ? "*" : "", pTd->NextTD,
              TdOld.be   != pTd->be   ? "*" : "", pTd->be,
              pszLogMsg));
    }
# else
    RT_NOREF(pszLogMsg);
# endif
    ohciR3PutDWords(pDevIns, TdAddr, (uint32_t *)pTd, sizeof(*pTd) >> 2);
#ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE
    POHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCICC);
    ohciR3CacheTdUpdate(pThisCC, TdAddr, pTd);
#endif
}

/**
 * Writes an OHCIITD.
 */
DECLINLINE(void) ohciR3WriteITd(PPDMDEVINS pDevIns, POHCI pThis, uint32_t ITdAddr, PCOHCIITD pITd, const char *pszLogMsg)
{
# ifdef LOG_ENABLED
    if (LogIs3Enabled())
    {
        OHCIITD ITdOld;
        ohciR3GetDWords(pDevIns, ITdAddr, (uint32_t *)&ITdOld, sizeof(ITdOld) / sizeof(uint32_t));
        uint32_t HIChg = ITdOld.HwInfo ^ pITd->HwInfo;
        Log3(("ohciR3WriteITd(,%#010x,): %sSF=%#x (now=%#RX32) %sDI=%#x %sFC=%d %sCC=%#x %sBP0=%#010x %sNextTD=%#010x %sBE=%#010x (%s)\n",
              ITdAddr,
              (HIChg & 0xffff) & 1 ? "*" : "", pITd->HwInfo & 0xffff, pThis->HcFmNumber,
              (HIChg >> 21)    & 7 ? "*" : "", (pITd->HwInfo >> 21) & 7,
              (HIChg >> 24)    & 7 ? "*" : "", (pITd->HwInfo >> 24) & 7,
              (HIChg >> 28)    &15 ? "*" : "", (pITd->HwInfo >> 28) &15,
              ITdOld.BP0    != pITd->BP0    ? "*" : "", pITd->BP0,
              ITdOld.NextTD != pITd->NextTD ? "*" : "", pITd->NextTD,
              ITdOld.BE     != pITd->BE     ? "*" : "", pITd->BE,
              pszLogMsg));
        Log3(("psw0=%s%x:%s%03x psw1=%s%x:%s%03x psw2=%s%x:%s%03x psw3=%s%x:%s%03x psw4=%s%x:%s%03x psw5=%s%x:%s%03x psw6=%s%x:%s%03x psw7=%s%x:%s%03x\n",
              (ITdOld.aPSW[0] >> 12) != (pITd->aPSW[0] >> 12) ? "*" : "", pITd->aPSW[0] >> 12,  (ITdOld.aPSW[0] & 0xfff) != (pITd->aPSW[0] & 0xfff) ? "*" : "", pITd->aPSW[0] & 0xfff,
              (ITdOld.aPSW[1] >> 12) != (pITd->aPSW[1] >> 12) ? "*" : "", pITd->aPSW[1] >> 12,  (ITdOld.aPSW[1] & 0xfff) != (pITd->aPSW[1] & 0xfff) ? "*" : "", pITd->aPSW[1] & 0xfff,
              (ITdOld.aPSW[2] >> 12) != (pITd->aPSW[2] >> 12) ? "*" : "", pITd->aPSW[2] >> 12,  (ITdOld.aPSW[2] & 0xfff) != (pITd->aPSW[2] & 0xfff) ? "*" : "", pITd->aPSW[2] & 0xfff,
              (ITdOld.aPSW[3] >> 12) != (pITd->aPSW[3] >> 12) ? "*" : "", pITd->aPSW[3] >> 12,  (ITdOld.aPSW[3] & 0xfff) != (pITd->aPSW[3] & 0xfff) ? "*" : "", pITd->aPSW[3] & 0xfff,
              (ITdOld.aPSW[4] >> 12) != (pITd->aPSW[4] >> 12) ? "*" : "", pITd->aPSW[4] >> 12,  (ITdOld.aPSW[4] & 0xfff) != (pITd->aPSW[4] & 0xfff) ? "*" : "", pITd->aPSW[4] & 0xfff,
              (ITdOld.aPSW[5] >> 12) != (pITd->aPSW[5] >> 12) ? "*" : "", pITd->aPSW[5] >> 12,  (ITdOld.aPSW[5] & 0xfff) != (pITd->aPSW[5] & 0xfff) ? "*" : "", pITd->aPSW[5] & 0xfff,
              (ITdOld.aPSW[6] >> 12) != (pITd->aPSW[6] >> 12) ? "*" : "", pITd->aPSW[6] >> 12,  (ITdOld.aPSW[6] & 0xfff) != (pITd->aPSW[6] & 0xfff) ? "*" : "", pITd->aPSW[6] & 0xfff,
              (ITdOld.aPSW[7] >> 12) != (pITd->aPSW[7] >> 12) ? "*" : "", pITd->aPSW[7] >> 12,  (ITdOld.aPSW[7] & 0xfff) != (pITd->aPSW[7] & 0xfff) ? "*" : "", pITd->aPSW[7] & 0xfff));
    }
# else
    RT_NOREF(pThis, pszLogMsg);
# endif
    ohciR3PutDWords(pDevIns, ITdAddr, (uint32_t *)pITd, sizeof(*pITd) / sizeof(uint32_t));
}


# ifdef LOG_ENABLED

/**
 * Core TD queue dumper. LOG_ENABLED builds only.
 */
DECLINLINE(void) ohciR3DumpTdQueueCore(PPDMDEVINS pDevIns, POHCICC pThisCC, uint32_t GCPhysHead, uint32_t GCPhysTail, bool fFull)
{
    uint32_t GCPhys = GCPhysHead;
    int cIterations = 128;
    for (;;)
    {
        OHCITD Td;
        Log4(("%#010x%s%s", GCPhys,
              GCPhys && ohciR3InFlightFind(pThisCC, GCPhys) >= 0 ? "~" : "",
              GCPhys && ohciR3InDoneQueueFind(pThisCC, GCPhys) >= 0 ? "^" : ""));
        if (GCPhys == 0 || GCPhys == GCPhysTail)
            break;

        /* can't use ohciR3ReadTd() because of Log4. */
        ohciR3GetDWords(pDevIns, GCPhys, (uint32_t *)&Td, sizeof(Td) >> 2);
        if (fFull)
            Log4((" [R=%d DP=%d DI=%d T=%d EC=%d CC=%#x CBP=%#010x NextTD=%#010x BE=%#010x] -> ",
                  (Td.hwinfo >> 18) & 1,
                  (Td.hwinfo >> 19) & 3,
                  (Td.hwinfo >> 21) & 7,
                  (Td.hwinfo >> 24) & 3,
                  (Td.hwinfo >> 26) & 3,
                  (Td.hwinfo >> 28) &15,
                  Td.cbp,
                  Td.NextTD,
                  Td.be));
        else
            Log4((" -> "));
        GCPhys = Td.NextTD & ED_PTR_MASK;
        Assert(GCPhys != GCPhysHead);
        if (!--cIterations)
            break;
    }
}

/**
 * Dumps a TD queue. LOG_ENABLED builds only.
 */
DECLINLINE(void) ohciR3DumpTdQueue(PPDMDEVINS pDevIns, POHCICC pThisCC, uint32_t GCPhysHead, const char *pszMsg)
{
    if (pszMsg)
        Log4(("%s: ", pszMsg));
    ohciR3DumpTdQueueCore(pDevIns, pThisCC, GCPhysHead, 0, true);
    Log4(("\n"));
}

/**
 * Core ITD queue dumper. LOG_ENABLED builds only.
 */
DECLINLINE(void) ohciR3DumpITdQueueCore(PPDMDEVINS pDevIns, POHCICC pThisCC, uint32_t GCPhysHead, uint32_t GCPhysTail, bool fFull)
{
    RT_NOREF(fFull);
    uint32_t GCPhys = GCPhysHead;
    int cIterations = 100;
    for (;;)
    {
        OHCIITD ITd;
        Log4(("%#010x%s%s", GCPhys,
              GCPhys && ohciR3InFlightFind(pThisCC, GCPhys) >= 0 ? "~" : "",
              GCPhys && ohciR3InDoneQueueFind(pThisCC, GCPhys) >= 0 ? "^" : ""));
        if (GCPhys == 0 || GCPhys == GCPhysTail)
            break;

        /* can't use ohciR3ReadTd() because of Log4. */
        ohciR3GetDWords(pDevIns, GCPhys, (uint32_t *)&ITd, sizeof(ITd) / sizeof(uint32_t));
        /*if (fFull)
            Log4((" [R=%d DP=%d DI=%d T=%d EC=%d CC=%#x CBP=%#010x NextTD=%#010x BE=%#010x] -> ",
                  (Td.hwinfo >> 18) & 1,
                  (Td.hwinfo >> 19) & 3,
                  (Td.hwinfo >> 21) & 7,
                  (Td.hwinfo >> 24) & 3,
                  (Td.hwinfo >> 26) & 3,
                  (Td.hwinfo >> 28) &15,
                  Td.cbp,
                  Td.NextTD,
                  Td.be));
        else*/
            Log4((" -> "));
        GCPhys = ITd.NextTD & ED_PTR_MASK;
        Assert(GCPhys != GCPhysHead);
        if (!--cIterations)
            break;
    }
}

/**
 * Dumps a ED list. LOG_ENABLED builds only.
 */
DECLINLINE(void) ohciR3DumpEdList(PPDMDEVINS pDevIns, POHCICC pThisCC, uint32_t GCPhysHead, const char *pszMsg, bool fTDs)
{
    RT_NOREF(fTDs);
    uint32_t GCPhys = GCPhysHead;
    if (pszMsg)
        Log4(("%s:", pszMsg));
    for (;;)
    {
        OHCIED Ed;

        /* ED */
        Log4((" %#010x={", GCPhys));
        if (!GCPhys)
        {
            Log4(("END}\n"));
            return;
        }

        /* TDs */
        ohciR3ReadEd(pDevIns, GCPhys, &Ed);
        if (Ed.hwinfo & ED_HWINFO_ISO)
            Log4(("[I]"));
        if ((Ed.HeadP & ED_HEAD_HALTED) || (Ed.hwinfo & ED_HWINFO_SKIP))
        {
            if ((Ed.HeadP & ED_HEAD_HALTED) && (Ed.hwinfo & ED_HWINFO_SKIP))
                Log4(("SH}"));
            else if (Ed.hwinfo & ED_HWINFO_SKIP)
                Log4(("S-}"));
            else
                Log4(("-H}"));
        }
        else
        {
            if (Ed.hwinfo & ED_HWINFO_ISO)
                ohciR3DumpITdQueueCore(pDevIns, pThisCC, Ed.HeadP & ED_PTR_MASK, Ed.TailP & ED_PTR_MASK, false);
            else
                ohciR3DumpTdQueueCore(pDevIns, pThisCC, Ed.HeadP & ED_PTR_MASK, Ed.TailP & ED_PTR_MASK, false);
            Log4(("}"));
        }

        /* next */
        GCPhys = Ed.NextED & ED_PTR_MASK;
        Assert(GCPhys != GCPhysHead);
    }
    /* not reached */
}

# endif /* LOG_ENABLED */


DECLINLINE(int) ohciR3InFlightFindFree(POHCICC pThisCC, const int iStart)
{
    unsigned i = iStart;
    while (i < RT_ELEMENTS(pThisCC->aInFlight))
    {
        if (pThisCC->aInFlight[i].pUrb == NULL)
            return i;
        i++;
    }
    i = iStart;
    while (i-- > 0)
    {
        if (pThisCC->aInFlight[i].pUrb == NULL)
            return i;
    }
    return -1;
}


/**
 * Record an in-flight TD.
 *
 * @param   pThis       OHCI instance data, shared edition.
 * @param   pThisCC     OHCI instance data, ring-3 edition.
 * @param   GCPhysTD    Physical address of the TD.
 * @param   pUrb        The URB.
 */
static void ohciR3InFlightAdd(POHCI pThis, POHCICC pThisCC, uint32_t GCPhysTD, PVUSBURB pUrb)
{
    if (ohciR3IsTdInFlight(pThisCC, GCPhysTD))
    {
        PPDMDEVINS pDevIns = pThisCC->pDevInsR3;
        ohciR3RaiseUnrecoverableError(pDevIns, pThis, 10);
        return;
    }

    int i = ohciR3InFlightFindFree(pThisCC, (GCPhysTD >> 4) % RT_ELEMENTS(pThisCC->aInFlight));
    if (i >= 0)
    {
# ifdef LOG_ENABLED
        pUrb->pHci->u32FrameNo = pThis->HcFmNumber;
# endif
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
 * @param   pThis       OHCI instance data, shared edition.
 * @param   pThisCC     OHCI instance data, ring-3 edition.
 * @param   pUrb        The URB.
 */
static void ohciR3InFlightAddUrb(POHCI pThis, POHCICC pThisCC, PVUSBURB pUrb)
{
    for (unsigned iTd = 0; iTd < pUrb->pHci->cTds; iTd++)
        ohciR3InFlightAdd(pThis, pThisCC, pUrb->paTds[iTd].TdAddr, pUrb);
}


/**
 * Finds a in-flight TD.
 *
 * @returns Index of the record.
 * @returns -1 if not found.
 * @param   pThisCC     OHCI instance data, ring-3 edition.
 * @param   GCPhysTD    Physical address of the TD.
 * @remark  This has to be fast.
 */
static int ohciR3InFlightFind(POHCICC pThisCC, uint32_t GCPhysTD)
{
    unsigned cLeft = pThisCC->cInFlight;
    unsigned i = (GCPhysTD >> 4) % RT_ELEMENTS(pThisCC->aInFlight);
    const int iLast = i;
    while (i < RT_ELEMENTS(pThisCC->aInFlight))
    {
        if (pThisCC->aInFlight[i].GCPhysTD == GCPhysTD && pThisCC->aInFlight[i].pUrb)
            return i;
        if (pThisCC->aInFlight[i].pUrb)
            if (cLeft-- <= 1)
                return -1;
        i++;
    }
    i = iLast;
    while (i-- > 0)
    {
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
 * @param   pThisCC     OHCI instance data, ring-3 edition.
 * @param   GCPhysTD    Physical address of the TD.
 */
static bool ohciR3IsTdInFlight(POHCICC pThisCC, uint32_t GCPhysTD)
{
    return ohciR3InFlightFind(pThisCC, GCPhysTD) >= 0;
}

#if 0
/**
 * Returns a URB associated with an in-flight TD, if any.
 *
 * @returns pointer to URB if TD is in flight.
 * @returns NULL if not in flight.
 * @param   pThisCC     OHCI instance data, ring-3 edition.
 * @param   GCPhysTD    Physical address of the TD.
 */
static PVUSBURB ohciR3TdInFlightUrb(POHCICC pThisCC, uint32_t GCPhysTD)
{
    int i;

    i = ohciR3InFlightFind(pThisCC, GCPhysTD);
    if ( i >= 0 )
        return pThisCC->aInFlight[i].pUrb;
    return NULL;
}
#endif

/**
 * Removes a in-flight TD.
 *
 * @returns 0 if found. For logged builds this is the number of frames the TD has been in-flight.
 * @returns -1 if not found.
 * @param   pThis       OHCI instance data, shared edition (for logging).
 * @param   pThisCC     OHCI instance data, ring-3 edition.
 * @param   GCPhysTD    Physical address of the TD.
 */
static int ohciR3InFlightRemove(POHCI pThis, POHCICC pThisCC, uint32_t GCPhysTD)
{
    int i = ohciR3InFlightFind(pThisCC, GCPhysTD);
    if (i >= 0)
    {
# ifdef LOG_ENABLED
        const int cFramesInFlight = pThis->HcFmNumber - pThisCC->aInFlight[i].pUrb->pHci->u32FrameNo;
# else
        const int cFramesInFlight = 0; RT_NOREF(pThis);
# endif
        Log2(("ohciR3InFlightRemove: reaping TD=%#010x %d frames (%#010x-%#010x)\n",
              GCPhysTD, cFramesInFlight, pThisCC->aInFlight[i].pUrb->pHci->u32FrameNo, pThis->HcFmNumber));
        pThisCC->aInFlight[i].GCPhysTD = 0;
        pThisCC->aInFlight[i].pUrb = NULL;
        pThisCC->cInFlight--;
        return cFramesInFlight;
    }
    AssertMsgFailed(("TD %#010x is not in flight\n", GCPhysTD));
    return -1;
}


/**
 * Clear any possible leftover traces of a URB from the in-flight tracking.
 * Useful if broken guests confuse the tracking logic by using the same TD
 * for multiple URBs. See @bugref{10410}.
 *
 * @param   pThisCC     OHCI instance data, ring-3 edition.
 * @param   pUrb        The URB.
 */
static void ohciR3InFlightClearUrb(POHCICC pThisCC, PVUSBURB pUrb)
{
    unsigned i = 0;
    while (i < RT_ELEMENTS(pThisCC->aInFlight))
    {
        if (pThisCC->aInFlight[i].pUrb == pUrb)
        {
            Log2(("ohciR3InFlightClearUrb: clearing leftover URB!!\n"));
            pThisCC->aInFlight[i].GCPhysTD = 0;
            pThisCC->aInFlight[i].pUrb = NULL;
            pThisCC->cInFlight--;
        }
        i++;
    }
}


/**
 * Removes all TDs associated with a URB from the in-flight tracking.
 *
 * @returns 0 if found. For logged builds this is the number of frames the TD has been in-flight.
 * @returns -1 if not found.
 * @param   pThis       OHCI instance data, shared edition (for logging).
 * @param   pThisCC     OHCI instance data, ring-3 edition.
 * @param   pUrb        The URB.
 */
static int ohciR3InFlightRemoveUrb(POHCI pThis, POHCICC pThisCC, PVUSBURB pUrb)
{
    int cFramesInFlight = ohciR3InFlightRemove(pThis, pThisCC, pUrb->paTds[0].TdAddr);
    if (pUrb->pHci->cTds > 1)
    {
        for (unsigned iTd = 1; iTd < pUrb->pHci->cTds; iTd++)
            if (ohciR3InFlightRemove(pThis, pThisCC, pUrb->paTds[iTd].TdAddr) < 0)
                cFramesInFlight = -1;
    }
    ohciR3InFlightClearUrb(pThisCC, pUrb);
    return cFramesInFlight;
}


# if defined(VBOX_STRICT) || defined(LOG_ENABLED)

/**
 * Empties the in-done-queue.
 * @param   pThisCC     OHCI instance data, ring-3 edition.
 */
static void ohciR3InDoneQueueZap(POHCICC pThisCC)
{
    pThisCC->cInDoneQueue = 0;
}

/**
 * Finds a TD in the in-done-queue.
 * @returns >= 0 on success.
 * @returns -1 if not found.
 * @param   pThisCC     OHCI instance data, ring-3 edition.
 * @param   GCPhysTD    Physical address of the TD.
 */
static int ohciR3InDoneQueueFind(POHCICC pThisCC, uint32_t GCPhysTD)
{
    unsigned i = pThisCC->cInDoneQueue;
    while (i-- > 0)
        if (pThisCC->aInDoneQueue[i].GCPhysTD == GCPhysTD)
            return i;
    return -1;
}

/**
 * Checks that the specified TD is not in the done queue.
 * @param   pThisCC     OHCI instance data, ring-3 edition.
 * @param   GCPhysTD    Physical address of the TD.
 */
static bool ohciR3InDoneQueueCheck(POHCICC pThisCC, uint32_t GCPhysTD)
{
    int i = ohciR3InDoneQueueFind(pThisCC, GCPhysTD);
#  if 0
    /* This condition has been observed with the USB tablet emulation or with
     * a real USB mouse and an SMP XP guest.  I am also not sure if this is
     * really a problem for us.  The assertion checks that the guest doesn't
     * re-submit a TD which is still in the done queue.  It seems to me that
     * this should only be a problem if we either keep track of TDs in the done
     * queue somewhere else as well (in which case we should also free those
     * references in time, and I can't see any code doing that) or if we
     * manipulate TDs in the done queue in some way that might fail if they are
     * re-submitted (can't see anything like that either).
     */
    AssertMsg(i < 0, ("TD %#010x (i=%d)\n", GCPhysTD, i));
#  endif
    return i < 0;
}


#  if defined(VBOX_STRICT) && defined(LOG_ENABLED)
/**
 * Adds a TD to the in-done-queue tracking, checking that it's not there already.
 * @param   pThisCC     OHCI instance data, ring-3 edition.
 * @param   GCPhysTD    Physical address of the TD.
 */
static void ohciR3InDoneQueueAdd(POHCICC pThisCC, uint32_t GCPhysTD)
{
    Assert(pThisCC->cInDoneQueue + 1 <= RT_ELEMENTS(pThisCC->aInDoneQueue));
    if (ohciR3InDoneQueueCheck(pThisCC, GCPhysTD))
        pThisCC->aInDoneQueue[pThisCC->cInDoneQueue++].GCPhysTD = GCPhysTD;
}
#  endif /* VBOX_STRICT */
# endif /* defined(VBOX_STRICT) || defined(LOG_ENABLED) */


/**
 * OHCI Transport Buffer - represents a OHCI Transport Descriptor (TD).
 * A TD may be split over max 2 pages.
 */
typedef struct OHCIBUF
{
    /** Pages involved. */
    struct OHCIBUFVEC
    {
        /** The 32-bit physical address of this part. */
        uint32_t Addr;
        /** The length. */
        uint32_t cb;
    } aVecs[2];
    /** Number of valid entries in aVecs. */
    uint32_t    cVecs;
    /** The total length. */
    uint32_t    cbTotal;
} OHCIBUF, *POHCIBUF;


/**
 * Sets up a OHCI transport buffer.
 *
 * @param   pBuf    OHCI buffer.
 * @param   cbp     Current buffer pointer. 32-bit physical address.
 * @param   be      Last byte in buffer (BufferEnd). 32-bit physical address.
 */
static void ohciR3BufInit(POHCIBUF pBuf, uint32_t cbp, uint32_t be)
{
    if (!cbp || !be)
    {
        pBuf->cVecs = 0;
        pBuf->cbTotal = 0;
        Log2(("ohci: cbp=%#010x be=%#010x cbTotal=0 EMPTY\n", cbp, be));
    }
    else if ((cbp & ~0xfff) == (be & ~0xfff) && (cbp <= be))
    {
        pBuf->aVecs[0].Addr = cbp;
        pBuf->aVecs[0].cb = (be - cbp) + 1;
        pBuf->cVecs   = 1;
        pBuf->cbTotal = pBuf->aVecs[0].cb;
        Log2(("ohci: cbp=%#010x be=%#010x cbTotal=%u\n", cbp, be, pBuf->cbTotal));
    }
    else
    {
        pBuf->aVecs[0].Addr = cbp;
        pBuf->aVecs[0].cb   = 0x1000 - (cbp & 0xfff);
        pBuf->aVecs[1].Addr = be & ~0xfff;
        pBuf->aVecs[1].cb   = (be & 0xfff) + 1;
        pBuf->cVecs   = 2;
        pBuf->cbTotal = pBuf->aVecs[0].cb + pBuf->aVecs[1].cb;
        Log2(("ohci: cbp=%#010x be=%#010x cbTotal=%u PAGE FLIP\n", cbp, be, pBuf->cbTotal));
    }
}

/**
 * Updates a OHCI transport buffer.
 *
 * This is called upon completion to adjust the sector lengths if
 * the total length has changed. (received less then we had space for
 * or a partial transfer.)
 *
 * @param   pBuf        The buffer to update. cbTotal contains the new total on input.
 *                      While the aVecs[*].cb members is updated upon return.
 */
static void ohciR3BufUpdate(POHCIBUF pBuf)
{
    for (uint32_t i = 0, cbCur = 0; i < pBuf->cVecs; i++)
    {
        if (cbCur + pBuf->aVecs[i].cb > pBuf->cbTotal)
        {
            pBuf->aVecs[i].cb = pBuf->cbTotal - cbCur;
            pBuf->cVecs = i + 1;
            return;
        }
        cbCur += pBuf->aVecs[i].cb;
    }
}


/** A worker for ohciR3UnlinkTds(). */
static bool ohciR3UnlinkIsochronousTdInList(PPDMDEVINS pDevIns, POHCI pThis, uint32_t TdAddr, POHCIITD pITd, POHCIED pEd)
{
    const uint32_t  LastTdAddr = pEd->TailP & ED_PTR_MASK;
    Log(("ohciUnlinkIsocTdInList: Unlinking non-head ITD! TdAddr=%#010RX32 HeadTdAddr=%#010RX32 LastEdAddr=%#010RX32\n",
         TdAddr, pEd->HeadP & ED_PTR_MASK, LastTdAddr));
    AssertMsgReturn(LastTdAddr != TdAddr, ("TdAddr=%#010RX32\n", TdAddr), false);

    uint32_t cIterations = 256;
    uint32_t CurTdAddr = pEd->HeadP & ED_PTR_MASK;
    while (     CurTdAddr != LastTdAddr
           &&   cIterations-- > 0)
    {
        OHCIITD ITd;
        ohciR3ReadITd(pDevIns, pThis, CurTdAddr, &ITd);
        if ((ITd.NextTD & ED_PTR_MASK) == TdAddr)
        {
            ITd.NextTD = (pITd->NextTD & ED_PTR_MASK) | (ITd.NextTD & ~ED_PTR_MASK);
            ohciR3WriteITd(pDevIns, pThis, CurTdAddr, &ITd, "ohciUnlinkIsocTdInList");
            pITd->NextTD &= ~ED_PTR_MASK;
            return true;
        }

        /* next */
        CurTdAddr = ITd.NextTD & ED_PTR_MASK;
    }

    Log(("ohciUnlinkIsocTdInList: TdAddr=%#010RX32 wasn't found in the list!!! (cIterations=%d)\n", TdAddr, cIterations));
    return false;
}


/** A worker for ohciR3UnlinkTds(). */
static bool ohciR3UnlinkGeneralTdInList(PPDMDEVINS pDevIns, uint32_t TdAddr, POHCITD pTd, POHCIED pEd)
{
    const uint32_t  LastTdAddr = pEd->TailP & ED_PTR_MASK;
    Log(("ohciR3UnlinkGeneralTdInList: Unlinking non-head TD! TdAddr=%#010RX32 HeadTdAddr=%#010RX32 LastEdAddr=%#010RX32\n",
         TdAddr, pEd->HeadP & ED_PTR_MASK, LastTdAddr));
    AssertMsgReturn(LastTdAddr != TdAddr, ("TdAddr=%#010RX32\n", TdAddr), false);

    uint32_t cIterations = 256;
    uint32_t CurTdAddr = pEd->HeadP & ED_PTR_MASK;
    while (     CurTdAddr != LastTdAddr
           &&   cIterations-- > 0)
    {
        OHCITD Td;
        ohciR3ReadTd(pDevIns, CurTdAddr, &Td);
        if ((Td.NextTD & ED_PTR_MASK) == TdAddr)
        {
            Td.NextTD = (pTd->NextTD & ED_PTR_MASK) | (Td.NextTD & ~ED_PTR_MASK);
            ohciR3WriteTd(pDevIns, CurTdAddr, &Td, "ohciR3UnlinkGeneralTdInList");
            pTd->NextTD &= ~ED_PTR_MASK;
            return true;
        }

        /* next */
        CurTdAddr = Td.NextTD & ED_PTR_MASK;
    }

    Log(("ohciR3UnlinkGeneralTdInList: TdAddr=%#010RX32 wasn't found in the list!!! (cIterations=%d)\n", TdAddr, cIterations));
    return false;
}


/**
 * Unlinks the TDs that makes up the URB from the ED.
 *
 * @returns success indicator. true if successfully unlinked.
 * @returns false if the TD was not found in the list.
 */
static bool ohciR3UnlinkTds(PPDMDEVINS pDevIns, POHCI pThis, PVUSBURB pUrb, POHCIED pEd)
{
    /*
     * Don't unlink more than once.
     */
    if (pUrb->pHci->fUnlinked)
        return true;
    pUrb->pHci->fUnlinked = true;

    if (pUrb->enmType == VUSBXFERTYPE_ISOC)
    {
        for (unsigned iTd = 0; iTd < pUrb->pHci->cTds; iTd++)
        {
            POHCIITD pITd = (POHCIITD)&pUrb->paTds[iTd].TdCopy[0];
            const uint32_t ITdAddr = pUrb->paTds[iTd].TdAddr;

            /*
             * Unlink the TD from the ED list.
             * The normal case is that it's at the head of the list.
             */
            Assert((ITdAddr & ED_PTR_MASK) == ITdAddr);
            if ((pEd->HeadP & ED_PTR_MASK) == ITdAddr)
            {
                pEd->HeadP = (pITd->NextTD & ED_PTR_MASK) | (pEd->HeadP & ~ED_PTR_MASK);
                pITd->NextTD &= ~ED_PTR_MASK;
            }
            else
            {
                /*
                 * It's probably somewhere in the list, not a unlikely situation with
                 * the current isochronous code.
                 */
                if (!ohciR3UnlinkIsochronousTdInList(pDevIns, pThis, ITdAddr, pITd, pEd))
                    return false;
            }
        }
    }
    else
    {
        for (unsigned iTd = 0; iTd < pUrb->pHci->cTds; iTd++)
        {
            POHCITD pTd = (POHCITD)&pUrb->paTds[iTd].TdCopy[0];
            const uint32_t TdAddr = pUrb->paTds[iTd].TdAddr;

            /** @todo r=bird: Messing with the toggle flag in prepare is probably not correct
             * when we encounter a STALL error, 4.3.1.3.7.2: ''If an endpoint returns a STALL
             * PID, the  Host Controller retires the General TD with the ConditionCode set
             * to STALL and halts the endpoint. The CurrentBufferPointer, ErrorCount, and
             * dataToggle fields retain the values that they had at the start of the
             * transaction.'' */

            /* update toggle and set data toggle carry */
            pTd->hwinfo &= ~TD_HWINFO_TOGGLE;
            if ( pTd->hwinfo & TD_HWINFO_TOGGLE_HI )
            {
                if ( !!(pTd->hwinfo & TD_HWINFO_TOGGLE_LO) ) /** @todo r=bird: is it just me or doesn't this make sense at all? */
                    pTd->hwinfo |= TD_HWINFO_TOGGLE_LO;
                else
                    pTd->hwinfo &= ~TD_HWINFO_TOGGLE_LO;
            }
            else
            {
                if ( !!(pEd->HeadP & ED_HEAD_CARRY) )        /** @todo r=bird: is it just me or doesn't this make sense at all? */
                    pEd->HeadP |= ED_HEAD_CARRY;
                else
                    pEd->HeadP &= ~ED_HEAD_CARRY;
            }

            /*
             * Unlink the TD from the ED list.
             * The normal case is that it's at the head of the list.
             */
            Assert((TdAddr & ED_PTR_MASK) == TdAddr);
            if ((pEd->HeadP & ED_PTR_MASK) == TdAddr)
            {
                pEd->HeadP = (pTd->NextTD & ED_PTR_MASK) | (pEd->HeadP & ~ED_PTR_MASK);
                pTd->NextTD &= ~ED_PTR_MASK;
            }
            else
            {
                /*
                 * The TD is probably somewhere in the list.
                 *
                 * This shouldn't ever happen unless there was a failure! Even on failure,
                 * we can screw up the HCD state by picking out a TD from within the list
                 * like this! If this turns out to be a problem, we have to find a better
                 * solution. For now we'll hope the HCD handles it...
                 */
                if (!ohciR3UnlinkGeneralTdInList(pDevIns, TdAddr, pTd, pEd))
                    return false;
            }

            /*
             * Only unlink the first TD on error.
             * See comment in ohciR3RhXferCompleteGeneralURB().
             */
            if (pUrb->enmStatus != VUSBSTATUS_OK)
                break;
        }
    }

    return true;
}


/**
 * Checks that the transport descriptors associated with the URB
 * hasn't been changed in any way indicating that they may have been canceled.
 *
 * This rountine also updates the TD copies contained within the URB.
 *
 * @returns true if the URB has been canceled, otherwise false.
 * @param   pDevIns     The device instance.
 * @param   pThis       The OHCI instance.
 * @param   pUrb        The URB in question.
 * @param   pEd         The ED pointer (optional).
 */
static bool ohciR3HasUrbBeenCanceled(PPDMDEVINS pDevIns, POHCI pThis, PVUSBURB pUrb, PCOHCIED pEd)
{
    if (!pUrb)
        return true;

    /*
     * Make sure we've got an endpoint descriptor so we can
     * check for tail TDs.
     */
    OHCIED Ed;
    if (!pEd)
    {
        ohciR3ReadEd(pDevIns, pUrb->pHci->EdAddr, &Ed);
        pEd = &Ed;
    }

    if (pUrb->enmType == VUSBXFERTYPE_ISOC)
    {
        for (unsigned iTd = 0; iTd < pUrb->pHci->cTds; iTd++)
        {
            union
            {
                OHCIITD     ITd;
                uint32_t    au32[8];
            } u;
            if (    (pUrb->paTds[iTd].TdAddr & ED_PTR_MASK)
                ==  (pEd->TailP & ED_PTR_MASK))
            {
                Log(("%s: ohciR3HasUrbBeenCanceled: iTd=%d cTds=%d TdAddr=%#010RX32 canceled (tail)! [iso]\n",
                     pUrb->pszDesc, iTd, pUrb->pHci->cTds, pUrb->paTds[iTd].TdAddr));
                STAM_COUNTER_INC(&pThis->StatCanceledIsocUrbs);
                return true;
            }
            ohciR3ReadITd(pDevIns, pThis, pUrb->paTds[iTd].TdAddr, &u.ITd);
            if (    u.au32[0] != pUrb->paTds[iTd].TdCopy[0]     /* hwinfo */
                ||  u.au32[1] != pUrb->paTds[iTd].TdCopy[1]     /* bp0 */
                ||  u.au32[3] != pUrb->paTds[iTd].TdCopy[3]     /* be */
                ||  (   u.au32[2] != pUrb->paTds[iTd].TdCopy[2] /* NextTD */
                     && iTd + 1 < pUrb->pHci->cTds /* ignore the last one */)
                ||  u.au32[4] != pUrb->paTds[iTd].TdCopy[4]     /* psw0&1 */
                ||  u.au32[5] != pUrb->paTds[iTd].TdCopy[5]     /* psw2&3 */
                ||  u.au32[6] != pUrb->paTds[iTd].TdCopy[6]     /* psw4&5 */
                ||  u.au32[7] != pUrb->paTds[iTd].TdCopy[7]     /* psw6&7 */
               )
            {
                Log(("%s: ohciR3HasUrbBeenCanceled: iTd=%d cTds=%d TdAddr=%#010RX32 canceled! [iso]\n",
                     pUrb->pszDesc, iTd, pUrb->pHci->cTds, pUrb->paTds[iTd].TdAddr));
                Log2(("   %.*Rhxs (cur)\n"
                      "!= %.*Rhxs (copy)\n",
                      sizeof(u.ITd), &u.ITd, sizeof(u.ITd), &pUrb->paTds[iTd].TdCopy[0]));
                STAM_COUNTER_INC(&pThis->StatCanceledIsocUrbs);
                return true;
            }
            pUrb->paTds[iTd].TdCopy[2] = u.au32[2];
        }
    }
    else
    {
        for (unsigned iTd = 0; iTd < pUrb->pHci->cTds; iTd++)
        {
            union
            {
                OHCITD      Td;
                uint32_t    au32[4];
            } u;
            if (    (pUrb->paTds[iTd].TdAddr & ED_PTR_MASK)
                ==  (pEd->TailP & ED_PTR_MASK))
            {
                Log(("%s: ohciR3HasUrbBeenCanceled: iTd=%d cTds=%d TdAddr=%#010RX32 canceled (tail)!\n",
                     pUrb->pszDesc, iTd, pUrb->pHci->cTds, pUrb->paTds[iTd].TdAddr));
                STAM_COUNTER_INC(&pThis->StatCanceledGenUrbs);
                return true;
            }
            ohciR3ReadTd(pDevIns, pUrb->paTds[iTd].TdAddr, &u.Td);
            if (    u.au32[0] != pUrb->paTds[iTd].TdCopy[0]     /* hwinfo */
                ||  u.au32[1] != pUrb->paTds[iTd].TdCopy[1]     /* cbp */
                ||  u.au32[3] != pUrb->paTds[iTd].TdCopy[3]     /* be */
                ||  (   u.au32[2] != pUrb->paTds[iTd].TdCopy[2] /* NextTD */
                     && iTd + 1 < pUrb->pHci->cTds /* ignore the last one */)
               )
            {
                Log(("%s: ohciR3HasUrbBeenCanceled: iTd=%d cTds=%d TdAddr=%#010RX32 canceled!\n",
                     pUrb->pszDesc, iTd, pUrb->pHci->cTds, pUrb->paTds[iTd].TdAddr));
                Log2(("   %.*Rhxs (cur)\n"
                      "!= %.*Rhxs (copy)\n",
                      sizeof(u.Td), &u.Td, sizeof(u.Td), &pUrb->paTds[iTd].TdCopy[0]));
                STAM_COUNTER_INC(&pThis->StatCanceledGenUrbs);
                return true;
            }
            pUrb->paTds[iTd].TdCopy[2] = u.au32[2];
        }
    }
    return false;
}


/**
 * Returns the OHCI_CC_* corresponding to the VUSB status code.
 *
 * @returns OHCI_CC_* value.
 * @param   enmStatus   The VUSB status code.
 */
static uint32_t ohciR3VUsbStatus2OhciStatus(VUSBSTATUS enmStatus)
{
    switch (enmStatus)
    {
        case VUSBSTATUS_OK:             return OHCI_CC_NO_ERROR;
        case VUSBSTATUS_STALL:          return OHCI_CC_STALL;
        case VUSBSTATUS_CRC:            return OHCI_CC_CRC;
        case VUSBSTATUS_DATA_UNDERRUN:  return OHCI_CC_DATA_UNDERRUN;
        case VUSBSTATUS_DATA_OVERRUN:   return OHCI_CC_DATA_OVERRUN;
        case VUSBSTATUS_DNR:            return OHCI_CC_DNR;
        case VUSBSTATUS_NOT_ACCESSED:   return OHCI_CC_NOT_ACCESSED_1;
        default:
            Log(("pUrb->enmStatus=%#x!!!\n", enmStatus));
            return OHCI_CC_DNR;
    }
}


/**
 * Lock the given OHCI controller instance.
 *
 * @param   pThisCC     The OHCI controller instance to lock, ring-3 edition.
 */
DECLINLINE(void) ohciR3Lock(POHCICC pThisCC)
{
    RTCritSectEnter(&pThisCC->CritSect);

# ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE
    /* Clear all caches here to avoid reading stale data from previous lock holders. */
    ohciR3PhysReadCacheInvalidate(&pThisCC->CacheED);
    ohciR3PhysReadCacheInvalidate(&pThisCC->CacheTD);
# endif
}


/**
 * Unlocks the given OHCI controller instance.
 *
 * @param   pThisCC     The OHCI controller instance to unlock, ring-3 edition.
 */
DECLINLINE(void) ohciR3Unlock(POHCICC pThisCC)
{
# ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE
    /*
     * Clear all caches here to avoid leaving stale data behind (paranoia^2,
     * already done in ohciR3Lock).
     */
    ohciR3PhysReadCacheInvalidate(&pThisCC->CacheED);
    ohciR3PhysReadCacheInvalidate(&pThisCC->CacheTD);
# endif

    RTCritSectLeave(&pThisCC->CritSect);
}


/**
 * Worker for ohciR3RhXferCompletion that handles the completion of
 * a URB made up of isochronous TDs.
 *
 * In general, all URBs should have status OK.
 */
static void ohciR3RhXferCompleteIsochronousURB(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC, PVUSBURB pUrb
                                               /*, POHCIED pEd , int cFmAge*/)
{
    /*
     * Copy the data back (if IN operation) and update the TDs.
     */
    for (unsigned iTd = 0; iTd < pUrb->pHci->cTds; iTd++)
    {
        POHCIITD pITd = (POHCIITD)&pUrb->paTds[iTd].TdCopy[0];
        const uint32_t ITdAddr = pUrb->paTds[iTd].TdAddr;
        const unsigned cFrames = ((pITd->HwInfo & ITD_HWINFO_FC) >> ITD_HWINFO_FC_SHIFT) + 1;
        unsigned       R = (pUrb->pHci->u32FrameNo & ITD_HWINFO_SF) - (pITd->HwInfo & ITD_HWINFO_SF);
        if (R >= 8)
            R = 0; /* submitted ahead of time. */

        /*
         * Only one case of TD level condition code is document, so
         * just set NO_ERROR here to reduce number duplicate code.
         */
        pITd->HwInfo &= ~TD_HWINFO_CC;
        AssertCompile(OHCI_CC_NO_ERROR == 0);

        if (pUrb->enmStatus == VUSBSTATUS_OK)
        {
            /*
             * Update the frames and copy back the data.
             * We assume that we don't get incorrect lengths here.
             */
            for (unsigned i = 0; i < cFrames; i++)
            {
                if (   i < R
                    || pUrb->aIsocPkts[i - R].enmStatus == VUSBSTATUS_NOT_ACCESSED)
                {
                    /* It should already be NotAccessed. */
                    pITd->aPSW[i] |= 0xe000; /* (Don't touch the 12th bit.) */
                    continue;
                }

                /* Update the PSW (save the offset first in case of a IN). */
                uint32_t off = pITd->aPSW[i] & ITD_PSW_OFFSET;
                pITd->aPSW[i] = ohciR3VUsbStatus2OhciStatus(pUrb->aIsocPkts[i - R].enmStatus)
                              >> (TD_HWINFO_CC_SHIFT - ITD_PSW_CC_SHIFT);

                if (    pUrb->enmDir == VUSBDIRECTION_IN
                    &&  (   pUrb->aIsocPkts[i - R].enmStatus == VUSBSTATUS_OK
                         || pUrb->aIsocPkts[i - R].enmStatus == VUSBSTATUS_DATA_UNDERRUN
                         || pUrb->aIsocPkts[i - R].enmStatus == VUSBSTATUS_DATA_OVERRUN))
                {
                    /* Set the size. */
                    const unsigned   cb = pUrb->aIsocPkts[i - R].cb;
                    pITd->aPSW[i] |= cb & ITD_PSW_SIZE;
                    /* Copy data. */
                    if (cb)
                    {
                        uint8_t *pb = &pUrb->abData[pUrb->aIsocPkts[i - R].off];
                        if (off + cb > 0x1000)
                        {
                            if (off < 0x1000)
                            {
                                /* both */
                                const unsigned cb0 = 0x1000 - off;
                                ohciR3PhysWrite(pDevIns, (pITd->BP0 & ITD_BP0_MASK) + off, pb, cb0);
                                ohciR3PhysWrite(pDevIns, pITd->BE & ITD_BP0_MASK, pb + cb0, cb - cb0);
                            }
                            else /* only in the 2nd page */
                                ohciR3PhysWrite(pDevIns, (pITd->BE & ITD_BP0_MASK) + (off & ITD_BP0_MASK), pb, cb);
                        }
                        else /* only in the 1st page */
                            ohciR3PhysWrite(pDevIns, (pITd->BP0 & ITD_BP0_MASK) + off, pb, cb);
                        Log5(("packet %d: off=%#x cb=%#x pb=%p (%#x)\n"
                              "%.*Rhxd\n",
                              i + R, off, cb, pb, pb - &pUrb->abData[0], cb, pb));
                        //off += cb;
                    }
                }
            }

            /*
             * If the last package ended with a NotAccessed status, set ITD CC
             * to DataOverrun to indicate scheduling overrun.
             */
            if (pUrb->aIsocPkts[pUrb->cIsocPkts - 1].enmStatus == VUSBSTATUS_NOT_ACCESSED)
                pITd->HwInfo |= OHCI_CC_DATA_OVERRUN;
        }
        else
        {
            Log(("DevOHCI: Taking untested code path at line %d...\n", __LINE__));
            /*
             * Most status codes only applies to the individual packets.
             *
             * If we get a URB level error code of this kind, we'll distribute
             * it to all the packages unless some other status is available for
             * a package. This is a bit fuzzy, and we will get rid of this code
             * before long!
             */
            //if (pUrb->enmStatus != VUSBSTATUS_DATA_OVERRUN)
            {
                const unsigned uCC = ohciR3VUsbStatus2OhciStatus(pUrb->enmStatus)
                                   >> (TD_HWINFO_CC_SHIFT - ITD_PSW_CC_SHIFT);
                for (unsigned i = 0; i < cFrames; i++)
                    pITd->aPSW[i] = uCC;
            }
            //else
            //    pITd->HwInfo |= ohciR3VUsbStatus2OhciStatus(pUrb->enmStatus);
        }

        /*
         * Update the done queue interrupt timer.
         */
        uint32_t DoneInt = (pITd->HwInfo & ITD_HWINFO_DI) >> ITD_HWINFO_DI_SHIFT;
        if ((pITd->HwInfo & TD_HWINFO_CC) != OHCI_CC_NO_ERROR)
            DoneInt = 0; /* It's cleared on error. */
        if (    DoneInt != 0x7
            &&  DoneInt < pThis->dqic)
            pThis->dqic = DoneInt;

        /*
         * Move on to the done list and write back the modified TD.
         */
# ifdef LOG_ENABLED
        if (!pThis->done)
            pThisCC->u32FmDoneQueueTail = pThis->HcFmNumber;
#  ifdef VBOX_STRICT
        ohciR3InDoneQueueAdd(pThisCC, ITdAddr);
#  endif
# endif
        pITd->NextTD = pThis->done;
        pThis->done = ITdAddr;

        Log(("%s: ohciR3RhXferCompleteIsochronousURB: ITdAddr=%#010x EdAddr=%#010x SF=%#x (%#x) CC=%#x FC=%d "
             "psw0=%x:%x psw1=%x:%x psw2=%x:%x psw3=%x:%x psw4=%x:%x psw5=%x:%x psw6=%x:%x psw7=%x:%x R=%d\n",
             pUrb->pszDesc, ITdAddr,
             pUrb->pHci->EdAddr,
             pITd->HwInfo & ITD_HWINFO_SF, pThis->HcFmNumber,
             (pITd->HwInfo & ITD_HWINFO_CC) >> ITD_HWINFO_CC_SHIFT,
             (pITd->HwInfo & ITD_HWINFO_FC) >> ITD_HWINFO_FC_SHIFT,
             pITd->aPSW[0] >> ITD_PSW_CC_SHIFT, pITd->aPSW[0] & ITD_PSW_SIZE,
             pITd->aPSW[1] >> ITD_PSW_CC_SHIFT, pITd->aPSW[1] & ITD_PSW_SIZE,
             pITd->aPSW[2] >> ITD_PSW_CC_SHIFT, pITd->aPSW[2] & ITD_PSW_SIZE,
             pITd->aPSW[3] >> ITD_PSW_CC_SHIFT, pITd->aPSW[3] & ITD_PSW_SIZE,
             pITd->aPSW[4] >> ITD_PSW_CC_SHIFT, pITd->aPSW[4] & ITD_PSW_SIZE,
             pITd->aPSW[5] >> ITD_PSW_CC_SHIFT, pITd->aPSW[5] & ITD_PSW_SIZE,
             pITd->aPSW[6] >> ITD_PSW_CC_SHIFT, pITd->aPSW[6] & ITD_PSW_SIZE,
             pITd->aPSW[7] >> ITD_PSW_CC_SHIFT, pITd->aPSW[7] & ITD_PSW_SIZE,
             R));
        ohciR3WriteITd(pDevIns, pThis, ITdAddr, pITd, "retired");
    }
    RT_NOREF(pThisCC);
}


/**
 * Worker for ohciR3RhXferCompletion that handles the completion of
 * a URB made up of general TDs.
 */
static void ohciR3RhXferCompleteGeneralURB(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC, PVUSBURB pUrb,
                                           POHCIED pEd, int cFmAge)
{
    RT_NOREF(cFmAge);

    /*
     * Copy the data back (if IN operation) and update the TDs.
     */
    unsigned cbLeft = pUrb->cbData;
    uint8_t *pb     = &pUrb->abData[0];
    for (unsigned iTd = 0; iTd < pUrb->pHci->cTds; iTd++)
    {
        POHCITD pTd = (POHCITD)&pUrb->paTds[iTd].TdCopy[0];
        const uint32_t TdAddr = pUrb->paTds[iTd].TdAddr;

        /*
         * Setup a ohci transfer buffer and calc the new cbp value.
         */
        OHCIBUF Buf;
        ohciR3BufInit(&Buf, pTd->cbp, pTd->be);
        uint32_t NewCbp;
        if (cbLeft >= Buf.cbTotal)
            NewCbp = 0;
        else
        {
            /* (len may have changed for short transfers) */
            Buf.cbTotal = cbLeft;
            ohciR3BufUpdate(&Buf);
            Assert(Buf.cVecs >= 1);
            NewCbp = Buf.aVecs[Buf.cVecs-1].Addr + Buf.aVecs[Buf.cVecs-1].cb;
        }

        /*
         * Write back IN buffers.
         */
        if (    pUrb->enmDir == VUSBDIRECTION_IN
            &&  (   pUrb->enmStatus == VUSBSTATUS_OK
                 || pUrb->enmStatus == VUSBSTATUS_DATA_OVERRUN
                 || pUrb->enmStatus == VUSBSTATUS_DATA_UNDERRUN)
            &&  Buf.cbTotal > 0)
        {
            Assert(Buf.cVecs > 0);

            /* Be paranoid */
            if (   Buf.aVecs[0].cb > cbLeft
                || (   Buf.cVecs > 1
                    && Buf.aVecs[1].cb > (cbLeft - Buf.aVecs[0].cb)))
            {
                ohciR3RaiseUnrecoverableError(pDevIns, pThis, 1);
                return;
            }

            ohciR3PhysWrite(pDevIns, Buf.aVecs[0].Addr, pb, Buf.aVecs[0].cb);
            if (Buf.cVecs > 1)
                ohciR3PhysWrite(pDevIns, Buf.aVecs[1].Addr, pb + Buf.aVecs[0].cb, Buf.aVecs[1].cb);
        }

        /* advance the data buffer. */
        cbLeft -= Buf.cbTotal;
        pb += Buf.cbTotal;

        /*
         * Set writeback field.
         */
        /* zero out writeback fields for retirement */
        pTd->hwinfo &= ~TD_HWINFO_CC;
        /* always update the CurrentBufferPointer; essential for underrun/overrun errors */
        pTd->cbp = NewCbp;

        if (pUrb->enmStatus == VUSBSTATUS_OK)
        {
            pTd->hwinfo &= ~TD_HWINFO_ERRORS;

            /* update done queue interrupt timer */
            uint32_t DoneInt = (pTd->hwinfo & TD_HWINFO_DI) >> 21;
            if (    DoneInt != 0x7
                &&  DoneInt < pThis->dqic)
                pThis->dqic = DoneInt;
            Log(("%s: ohciR3RhXferCompleteGeneralURB: ED=%#010x TD=%#010x Age=%d enmStatus=%d cbTotal=%#x NewCbp=%#010RX32 dqic=%d\n",
                 pUrb->pszDesc, pUrb->pHci->EdAddr, TdAddr, cFmAge, pUrb->enmStatus, Buf.cbTotal, NewCbp, pThis->dqic));
        }
        else
        {
            Log(("%s: ohciR3RhXferCompleteGeneralURB: HALTED ED=%#010x TD=%#010x (age %d) pUrb->enmStatus=%d\n",
                 pUrb->pszDesc, pUrb->pHci->EdAddr, TdAddr, cFmAge, pUrb->enmStatus));
            pEd->HeadP |= ED_HEAD_HALTED;
            pThis->dqic = 0; /* "If the Transfer Descriptor is being retired with an error,
                             *  then the Done Queue Interrupt Counter is cleared as if the
                             *  InterruptDelay field were zero."
                             */
            switch (pUrb->enmStatus)
            {
                case VUSBSTATUS_STALL:
                    pTd->hwinfo |= OHCI_CC_STALL;
                    break;
                case VUSBSTATUS_CRC:
                    pTd->hwinfo |= OHCI_CC_CRC;
                    break;
                case VUSBSTATUS_DATA_UNDERRUN:
                    pTd->hwinfo |= OHCI_CC_DATA_UNDERRUN;
                    break;
                case VUSBSTATUS_DATA_OVERRUN:
                    pTd->hwinfo |= OHCI_CC_DATA_OVERRUN;
                    break;
                default: /* what the hell */
                    Log(("pUrb->enmStatus=%#x!!!\n", pUrb->enmStatus));
                    RT_FALL_THRU();
                case VUSBSTATUS_DNR:
                    pTd->hwinfo |= OHCI_CC_DNR;
                    break;
            }
        }

        /*
         * Move on to the done list and write back the modified TD.
         */
# ifdef LOG_ENABLED
        if (!pThis->done)
            pThisCC->u32FmDoneQueueTail = pThis->HcFmNumber;
#  ifdef VBOX_STRICT
        ohciR3InDoneQueueAdd(pThisCC, TdAddr);
#  endif
# endif
        pTd->NextTD = pThis->done;
        pThis->done = TdAddr;

        ohciR3WriteTd(pDevIns, TdAddr, pTd, "retired");

        /*
         * If we've halted the endpoint, we stop here.
         * ohciR3UnlinkTds() will make sure we've only unliked the first TD.
         *
         * The reason for this is that while we can have more than one TD in a URB, real
         * OHCI hardware will only deal with one TD at the time and it's therefore incorrect
         * to retire TDs after the endpoint has been halted. Win2k will crash or enter infinite
         * kernel loop if we don't behave correctly. (See @bugref{1646}.)
         */
        if (pEd->HeadP & ED_HEAD_HALTED)
            break;
    }
    RT_NOREF(pThisCC);
}


/**
 * Transfer completion callback routine.
 *
 * VUSB will call this when a transfer have been completed
 * in a one or another way.
 *
 * @param   pInterface      Pointer to OHCI::ROOTHUB::IRhPort.
 * @param   pUrb            Pointer to the URB in question.
 */
static DECLCALLBACK(void) ohciR3RhXferCompletion(PVUSBIROOTHUBPORT pInterface, PVUSBURB pUrb)
{
    POHCICC    pThisCC = VUSBIROOTHUBPORT_2_OHCI(pInterface);
    PPDMDEVINS pDevIns = pThisCC->pDevInsR3;
    POHCI      pThis   = PDMDEVINS_2_DATA(pDevIns, POHCI);
    LogFlow(("%s: ohciR3RhXferCompletion: EdAddr=%#010RX32 cTds=%d TdAddr0=%#010RX32\n",
             pUrb->pszDesc, pUrb->pHci->EdAddr, pUrb->pHci->cTds, pUrb->paTds[0].TdAddr));

    ohciR3Lock(pThisCC);

    int cFmAge = ohciR3InFlightRemoveUrb(pThis, pThisCC, pUrb);

    /* Do nothing requiring memory access if the HC encountered an unrecoverable error. */
    if (!(pThis->intr_status & OHCI_INTR_UNRECOVERABLE_ERROR))
    {
        pThis->fIdle = false;   /* Mark as active */

        /* get the current end point descriptor. */
        OHCIED Ed;
        ohciR3ReadEd(pDevIns, pUrb->pHci->EdAddr, &Ed);

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
        if (pUrb->enmStatus == VUSBSTATUS_UNDO)
        {
            /* Leave the TD alone - the HCD doesn't want us talking to the device. */
            Log(("%s: ohciR3RhXferCompletion: CANCELED {ED=%#010x cTds=%d TD0=%#010x age %d}\n",
                 pUrb->pszDesc, pUrb->pHci->EdAddr, pUrb->pHci->cTds, pUrb->paTds[0].TdAddr, cFmAge));
            STAM_COUNTER_INC(&pThis->StatDroppedUrbs);
            ohciR3Unlock(pThisCC);
            return;
        }
        bool fHasBeenCanceled = false;
        if (    (Ed.HeadP & ED_HEAD_HALTED)
            ||  (Ed.hwinfo & ED_HWINFO_SKIP)
            ||  cFmAge < 0
            ||  (fHasBeenCanceled = ohciR3HasUrbBeenCanceled(pDevIns, pThis, pUrb, &Ed))
            ||  !ohciR3UnlinkTds(pDevIns, pThis, pUrb, &Ed)
           )
        {
            Log(("%s: ohciR3RhXferCompletion: DROPPED {ED=%#010x cTds=%d TD0=%#010x age %d} because:%s%s%s%s%s!!!\n",
                 pUrb->pszDesc, pUrb->pHci->EdAddr, pUrb->pHci->cTds, pUrb->paTds[0].TdAddr, cFmAge,
                 (Ed.HeadP & ED_HEAD_HALTED)                            ? " ep halted" : "",
                 (Ed.hwinfo & ED_HWINFO_SKIP)                           ? " ep skip" : "",
                 (Ed.HeadP & ED_PTR_MASK) != pUrb->paTds[0].TdAddr      ? " ep head-changed" : "",
                 cFmAge < 0                                             ? " td not-in-flight" : "",
                 fHasBeenCanceled                                       ? " td canceled" : ""));
            NOREF(fHasBeenCanceled);
            STAM_COUNTER_INC(&pThis->StatDroppedUrbs);
            ohciR3Unlock(pThisCC);
            return;
        }

        /*
         * Complete the TD updating and write the back.
         * When appropriate also copy data back to the guest memory.
         */
        if (pUrb->enmType == VUSBXFERTYPE_ISOC)
            ohciR3RhXferCompleteIsochronousURB(pDevIns, pThis, pThisCC, pUrb /*, &Ed , cFmAge*/);
        else
            ohciR3RhXferCompleteGeneralURB(pDevIns, pThis, pThisCC, pUrb, &Ed, cFmAge);

        /* finally write back the endpoint descriptor. */
        ohciR3WriteEd(pDevIns, pUrb->pHci->EdAddr, &Ed);
    }

    ohciR3Unlock(pThisCC);
}


/**
 * Handle transfer errors.
 *
 * VUSB calls this when a transfer attempt failed. This function will respond
 * indicating whether to retry or complete the URB with failure.
 *
 * @returns true if the URB should be retired.
 * @returns false if the URB should be retried.
 * @param   pInterface      Pointer to OHCI::ROOTHUB::IRhPort.
 * @param   pUrb            Pointer to the URB in question.
 */
static DECLCALLBACK(bool) ohciR3RhXferError(PVUSBIROOTHUBPORT pInterface, PVUSBURB pUrb)
{
    POHCICC    pThisCC = VUSBIROOTHUBPORT_2_OHCI(pInterface);
    PPDMDEVINS pDevIns = pThisCC->pDevInsR3;
    POHCI      pThis   = PDMDEVINS_2_DATA(pDevIns, POHCI);

    /*
     * Isochronous URBs can't be retried.
     */
    if (pUrb->enmType == VUSBXFERTYPE_ISOC)
        return true;

    /*
     * Don't retry on stall.
     */
    if (pUrb->enmStatus == VUSBSTATUS_STALL)
    {
        Log2(("%s: ohciR3RhXferError: STALL, giving up.\n", pUrb->pszDesc));
        return true;
    }

    ohciR3Lock(pThisCC);
    bool fRetire = false;
    /*
     * Check if the TDs still are valid.
     * This will make sure the TdCopy is up to date.
     */
    const uint32_t  TdAddr = pUrb->paTds[0].TdAddr;
/** @todo IMPORTANT! we must check if the ED is still valid at this point!!! */
    if (ohciR3HasUrbBeenCanceled(pDevIns, pThis, pUrb, NULL))
    {
        Log(("%s: ohciR3RhXferError: TdAddr0=%#x canceled!\n", pUrb->pszDesc, TdAddr));
        fRetire = true;
    }
    else
    {
        /*
         * Get and update the error counter.
         */
        POHCITD     pTd = (POHCITD)&pUrb->paTds[0].TdCopy[0];
        unsigned    cErrs = (pTd->hwinfo & TD_HWINFO_ERRORS) >> TD_ERRORS_SHIFT;
        pTd->hwinfo &= ~TD_HWINFO_ERRORS;
        cErrs++;
        pTd->hwinfo |= (cErrs % TD_ERRORS_MAX) << TD_ERRORS_SHIFT;
        ohciR3WriteTd(pDevIns, TdAddr, pTd, "ohciR3RhXferError");

        if (cErrs >= TD_ERRORS_MAX - 1)
        {
            Log2(("%s: ohciR3RhXferError: too many errors, giving up!\n", pUrb->pszDesc));
            fRetire = true;
        }
        else
            Log2(("%s: ohciR3RhXferError: cErrs=%d: retrying...\n", pUrb->pszDesc, cErrs));
    }

    ohciR3Unlock(pThisCC);
    return fRetire;
}


/**
 * Determine transfer direction from an endpoint descriptor.
 * NB: This may fail if the direction is not valid. If it does fail,
 * we do not raise an unrecoverable error but the caller may wish to.
 */
static VUSBDIRECTION ohciR3GetDirection(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC, PCOHCIED pEd)
{
    RT_NOREF(pThisCC);
    RT_NOREF(pThis);
    VUSBDIRECTION   enmDir = VUSBDIRECTION_INVALID;

    if (pEd->hwinfo & ED_HWINFO_ISO)
    {
        switch (pEd->hwinfo & ED_HWINFO_DIR)
        {
            case ED_HWINFO_OUT: enmDir = VUSBDIRECTION_OUT; break;
            case ED_HWINFO_IN:  enmDir = VUSBDIRECTION_IN;  break;
            default:
                Log(("ohciR3GetDirection: Invalid direction!!!! Ed.hwinfo=%#x\n", pEd->hwinfo));
        }
    }
    else
    {
        switch (pEd->hwinfo & ED_HWINFO_DIR)
        {
            case ED_HWINFO_OUT: enmDir = VUSBDIRECTION_OUT; break;
            case ED_HWINFO_IN:  enmDir = VUSBDIRECTION_IN;  break;
            default:
                /* We must read the TD to determine direction. */
                uint32_t    TdAddr = pEd->HeadP & ED_PTR_MASK;
                OHCITD      Td;
                ohciR3ReadTd(pDevIns, TdAddr, &Td);
                switch (Td.hwinfo & TD_HWINFO_DIR)
                {
                    case TD_HWINFO_OUT: enmDir = VUSBDIRECTION_OUT; break;
                    case TD_HWINFO_IN:  enmDir = VUSBDIRECTION_IN; break;
                    case 0:             enmDir = VUSBDIRECTION_SETUP; break;
                    default:
                        Log(("ohciR3GetDirection: Invalid direction!!!! Td.hwinfo=%#x Ed.hwinfo=%#x\n", Td.hwinfo, pEd->hwinfo));
                }
        }
   }

    return enmDir;
}


/**
 * Service a general transport descriptor.
 */
static bool ohciR3ServiceTd(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC, VUSBXFERTYPE enmType,
                            PCOHCIED pEd, uint32_t EdAddr, uint32_t TdAddr, uint32_t *pNextTdAddr, const char *pszListName)
{
    RT_NOREF(pszListName);

    /*
     * Read the TD and setup the buffer data.
     */
    OHCITD Td;
    ohciR3ReadTd(pDevIns, TdAddr, &Td);
    OHCIBUF Buf;
    ohciR3BufInit(&Buf, Td.cbp, Td.be);

    *pNextTdAddr = Td.NextTD & ED_PTR_MASK;

    /*
     * Determine the direction.
     */
    VUSBDIRECTION enmDir;
    switch (pEd->hwinfo & ED_HWINFO_DIR)
    {
        case ED_HWINFO_OUT: enmDir = VUSBDIRECTION_OUT; break;
        case ED_HWINFO_IN:  enmDir = VUSBDIRECTION_IN;  break;
        default:
            switch (Td.hwinfo & TD_HWINFO_DIR)
            {
                case TD_HWINFO_OUT: enmDir = VUSBDIRECTION_OUT; break;
                case TD_HWINFO_IN:  enmDir = VUSBDIRECTION_IN; break;
                case 0:             enmDir = VUSBDIRECTION_SETUP; break;
                default:
                    Log(("ohciR3ServiceTd: Invalid direction!!!! Td.hwinfo=%#x Ed.hwdinfo=%#x\n", Td.hwinfo, pEd->hwinfo));
                    ohciR3RaiseUnrecoverableError(pDevIns, pThis, 2);
                    return false;
            }
            break;
    }

    pThis->fIdle = false;   /* Mark as active */

    /*
     * Allocate and initialize a new URB.
     */
    PVUSBURB pUrb = VUSBIRhNewUrb(pThisCC->RootHub.pIRhConn, pEd->hwinfo & ED_HWINFO_FUNCTION, VUSB_DEVICE_PORT_INVALID,
                                  enmType, enmDir, Buf.cbTotal, 1, NULL);
    if (!pUrb)
        return false;                   /* retry later... */

    pUrb->EndPt = (pEd->hwinfo & ED_HWINFO_ENDPOINT) >> ED_HWINFO_ENDPOINT_SHIFT;
    pUrb->fShortNotOk = !(Td.hwinfo & TD_HWINFO_ROUNDING);
    pUrb->enmStatus = VUSBSTATUS_OK;
    pUrb->pHci->EdAddr = EdAddr;
    pUrb->pHci->fUnlinked = false;
    pUrb->pHci->cTds = 1;
    pUrb->paTds[0].TdAddr = TdAddr;
    pUrb->pHci->u32FrameNo = pThis->HcFmNumber;
    AssertCompile(sizeof(pUrb->paTds[0].TdCopy) >= sizeof(Td));
    memcpy(pUrb->paTds[0].TdCopy, &Td, sizeof(Td));

    /* copy data if out bound transfer. */
    pUrb->cbData = Buf.cbTotal;
    if (    Buf.cbTotal
        &&  Buf.cVecs > 0
        &&  enmDir != VUSBDIRECTION_IN)
    {
        /* Be paranoid. */
        if (   Buf.aVecs[0].cb > pUrb->cbData
            || (   Buf.cVecs > 1
                && Buf.aVecs[1].cb > (pUrb->cbData - Buf.aVecs[0].cb)))
        {
            ohciR3RaiseUnrecoverableError(pDevIns, pThis, 3);
            VUSBIRhFreeUrb(pThisCC->RootHub.pIRhConn, pUrb);
            return false;
        }

        ohciR3PhysRead(pDevIns, Buf.aVecs[0].Addr, pUrb->abData, Buf.aVecs[0].cb);
        if (Buf.cVecs > 1)
            ohciR3PhysRead(pDevIns, Buf.aVecs[1].Addr, &pUrb->abData[Buf.aVecs[0].cb], Buf.aVecs[1].cb);
    }

    /*
     * Submit the URB.
     */
    ohciR3InFlightAdd(pThis, pThisCC, TdAddr, pUrb);
    Log(("%s: ohciR3ServiceTd: submitting TdAddr=%#010x EdAddr=%#010x cbData=%#x\n",
         pUrb->pszDesc, TdAddr, EdAddr, pUrb->cbData));

    ohciR3Unlock(pThisCC);
    int rc = VUSBIRhSubmitUrb(pThisCC->RootHub.pIRhConn, pUrb, &pThisCC->RootHub.Led);
    ohciR3Lock(pThisCC);
    if (RT_SUCCESS(rc))
        return true;

    /* Failure cleanup. Can happen if we're still resetting the device or out of resources. */
    Log(("ohciR3ServiceTd: failed submitting TdAddr=%#010x EdAddr=%#010x pUrb=%p!!\n",
         TdAddr, EdAddr, pUrb));
    ohciR3InFlightRemove(pThis, pThisCC, TdAddr);
    return false;
}


/**
 * Service a the head TD of an endpoint.
 */
static bool ohciR3ServiceHeadTd(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC, VUSBXFERTYPE enmType,
                                PCOHCIED pEd, uint32_t EdAddr, const char *pszListName)
{
    /*
     * Read the TD, after first checking if it's already in-flight.
     */
    uint32_t TdAddr = pEd->HeadP & ED_PTR_MASK;
    if (ohciR3IsTdInFlight(pThisCC, TdAddr))
        return false;
# if defined(VBOX_STRICT) || defined(LOG_ENABLED)
    ohciR3InDoneQueueCheck(pThisCC, TdAddr);
# endif
    return ohciR3ServiceTd(pDevIns, pThis, pThisCC, enmType, pEd, EdAddr, TdAddr, &TdAddr, pszListName);
}


/**
 * Service one or more general transport descriptors (bulk or interrupt).
 */
static bool ohciR3ServiceTdMultiple(PPDMDEVINS pDevIns, POHCI pThis, VUSBXFERTYPE enmType, PCOHCIED pEd, uint32_t EdAddr,
                                    uint32_t TdAddr, uint32_t *pNextTdAddr, const char *pszListName)
{
    RT_NOREF(pszListName);

    /*
     * Read the TDs involved in this URB.
     */
    struct OHCITDENTRY
    {
        /** The TD. */
        OHCITD      Td;
        /** The associated OHCI buffer tracker. */
        OHCIBUF     Buf;
        /** The TD address. */
        uint32_t    TdAddr;
        /** Pointer to the next element in the chain (stack). */
        struct OHCITDENTRY *pNext;
    }   Head;

    POHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCICC);
# ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE
    ohciR3PhysReadCacheInvalidate(&pThisCC->CacheTD);
# endif

    /* read the head */
    ohciR3ReadTd(pDevIns, TdAddr, &Head.Td);
    ohciR3BufInit(&Head.Buf, Head.Td.cbp, Head.Td.be);
    Head.TdAddr = TdAddr;
    Head.pNext = NULL;

    /* combine with more TDs. */
    struct OHCITDENTRY *pTail   = &Head;
    unsigned            cbTotal = pTail->Buf.cbTotal;
    unsigned            cTds    = 1;
    while (     (pTail->Buf.cbTotal == 0x1000 || pTail->Buf.cbTotal == 0x2000)
           &&   !(pTail->Td.hwinfo & TD_HWINFO_ROUNDING) /* This isn't right for *BSD, but let's not . */
           &&   (pTail->Td.NextTD & ED_PTR_MASK) != (pEd->TailP & ED_PTR_MASK)
           &&   cTds < 128)
    {
        struct OHCITDENTRY *pCur = (struct OHCITDENTRY *)alloca(sizeof(*pCur));

        pCur->pNext = NULL;
        pCur->TdAddr = pTail->Td.NextTD & ED_PTR_MASK;
        ohciR3ReadTd(pDevIns, pCur->TdAddr, &pCur->Td);
        ohciR3BufInit(&pCur->Buf, pCur->Td.cbp, pCur->Td.be);

        /* Don't combine if the direction doesn't match up. There can't actually be
         * a mismatch for bulk/interrupt EPs unless the guest is buggy.
         */
        if (    (pCur->Td.hwinfo & (TD_HWINFO_DIR))
            !=  (Head.Td.hwinfo & (TD_HWINFO_DIR)))
            break;

        pTail->pNext = pCur;
        pTail = pCur;
        cbTotal += pCur->Buf.cbTotal;
        cTds++;
    }

    /* calc next TD address */
    *pNextTdAddr = pTail->Td.NextTD & ED_PTR_MASK;

    /*
     * Determine the direction.
     */
    VUSBDIRECTION enmDir;
    switch (pEd->hwinfo & ED_HWINFO_DIR)
    {
        case ED_HWINFO_OUT: enmDir = VUSBDIRECTION_OUT; break;
        case ED_HWINFO_IN:  enmDir = VUSBDIRECTION_IN;  break;
        default:
            Log(("ohciR3ServiceTdMultiple: WARNING! Ed.hwdinfo=%#x bulk or interrupt EP shouldn't rely on the TD for direction...\n", pEd->hwinfo));
            switch (Head.Td.hwinfo & TD_HWINFO_DIR)
            {
                case TD_HWINFO_OUT: enmDir = VUSBDIRECTION_OUT; break;
                case TD_HWINFO_IN:  enmDir = VUSBDIRECTION_IN; break;
                default:
                    Log(("ohciR3ServiceTdMultiple: Invalid direction!!!! Head.Td.hwinfo=%#x Ed.hwdinfo=%#x\n", Head.Td.hwinfo, pEd->hwinfo));
                    ohciR3RaiseUnrecoverableError(pDevIns, pThis, 4);
                    return false;
            }
            break;
    }

    pThis->fIdle = false;   /* Mark as active */

    /*
     * Allocate and initialize a new URB.
     */
    PVUSBURB pUrb = VUSBIRhNewUrb(pThisCC->RootHub.pIRhConn, pEd->hwinfo & ED_HWINFO_FUNCTION, VUSB_DEVICE_PORT_INVALID,
                                  enmType, enmDir, cbTotal, cTds, "ohciR3ServiceTdMultiple");
    if (!pUrb)
        /* retry later... */
        return false;
    Assert(pUrb->cbData == cbTotal);

    pUrb->enmType = enmType;
    pUrb->EndPt = (pEd->hwinfo & ED_HWINFO_ENDPOINT) >> ED_HWINFO_ENDPOINT_SHIFT;
    pUrb->enmDir = enmDir;
    pUrb->fShortNotOk = !(pTail->Td.hwinfo & TD_HWINFO_ROUNDING);
    pUrb->enmStatus = VUSBSTATUS_OK;
    pUrb->pHci->cTds = cTds;
    pUrb->pHci->EdAddr = EdAddr;
    pUrb->pHci->fUnlinked = false;
    pUrb->pHci->u32FrameNo = pThis->HcFmNumber;

    /* Copy data and TD information. */
    unsigned iTd = 0;
    uint8_t *pb = &pUrb->abData[0];
    for (struct OHCITDENTRY *pCur = &Head; pCur; pCur = pCur->pNext, iTd++)
    {
        /* data */
        if (    cbTotal
            &&  enmDir != VUSBDIRECTION_IN
            &&  pCur->Buf.cVecs > 0)
        {
            ohciR3PhysRead(pDevIns, pCur->Buf.aVecs[0].Addr, pb, pCur->Buf.aVecs[0].cb);
            if (pCur->Buf.cVecs > 1)
                ohciR3PhysRead(pDevIns, pCur->Buf.aVecs[1].Addr, pb + pCur->Buf.aVecs[0].cb, pCur->Buf.aVecs[1].cb);
        }
        pb += pCur->Buf.cbTotal;

        /* TD info */
        pUrb->paTds[iTd].TdAddr = pCur->TdAddr;
        AssertCompile(sizeof(pUrb->paTds[iTd].TdCopy) >= sizeof(pCur->Td));
        memcpy(pUrb->paTds[iTd].TdCopy, &pCur->Td, sizeof(pCur->Td));
    }

    /*
     * Submit the URB.
     */
    ohciR3InFlightAddUrb(pThis, pThisCC, pUrb);
    Log(("%s: ohciR3ServiceTdMultiple: submitting cbData=%#x EdAddr=%#010x cTds=%d TdAddr0=%#010x\n",
         pUrb->pszDesc, pUrb->cbData, EdAddr, cTds, TdAddr));
    ohciR3Unlock(pThisCC);
    int rc = VUSBIRhSubmitUrb(pThisCC->RootHub.pIRhConn, pUrb, &pThisCC->RootHub.Led);
    ohciR3Lock(pThisCC);
    if (RT_SUCCESS(rc))
        return true;

    /* Failure cleanup. Can happen if we're still resetting the device or out of resources. */
    Log(("ohciR3ServiceTdMultiple: failed submitting pUrb=%p cbData=%#x EdAddr=%#010x cTds=%d TdAddr0=%#010x - rc=%Rrc\n",
         pUrb, cbTotal, EdAddr, cTds, TdAddr, rc));
    /* NB: We cannot call ohciR3InFlightRemoveUrb() because the URB is already gone! */
    for (struct OHCITDENTRY *pCur = &Head; pCur; pCur = pCur->pNext, iTd++)
        ohciR3InFlightRemove(pThis, pThisCC, pCur->TdAddr);
    return false;
}


/**
 * Service the head TD of an endpoint.
 */
static bool ohciR3ServiceHeadTdMultiple(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC, VUSBXFERTYPE enmType,
                                        PCOHCIED pEd, uint32_t EdAddr, const char *pszListName)
{
    /*
     * First, check that it's not already in-flight.
     */
    uint32_t TdAddr = pEd->HeadP & ED_PTR_MASK;
    if (ohciR3IsTdInFlight(pThisCC, TdAddr))
        return false;
# if defined(VBOX_STRICT) || defined(LOG_ENABLED)
    ohciR3InDoneQueueCheck(pThisCC, TdAddr);
# endif
    return ohciR3ServiceTdMultiple(pDevIns, pThis, enmType, pEd, EdAddr, TdAddr, &TdAddr, pszListName);
}


/**
 * A worker for ohciR3ServiceIsochronousEndpoint which unlinks a ITD
 * that belongs to the past.
 */
static bool ohciR3ServiceIsochronousTdUnlink(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC, POHCIITD pITd, uint32_t ITdAddr,
                                             uint32_t ITdAddrPrev, PVUSBURB pUrb, POHCIED pEd, uint32_t EdAddr)
{
    LogFlow(("%s%sohciR3ServiceIsochronousTdUnlink: Unlinking ITD: ITdAddr=%#010x EdAddr=%#010x ITdAddrPrev=%#010x\n",
             pUrb ? pUrb->pszDesc : "", pUrb ? ": " : "", ITdAddr, EdAddr, ITdAddrPrev));

    /*
     * Do the unlinking.
     */
    const uint32_t ITdAddrNext = pITd->NextTD & ED_PTR_MASK;
    if (ITdAddrPrev)
    {
        /* Get validate the previous TD */
        int iInFlightPrev = ohciR3InFlightFind(pThisCC, ITdAddrPrev);
        AssertMsgReturn(iInFlightPrev >= 0, ("ITdAddr=%#RX32\n", ITdAddrPrev), false);
        PVUSBURB pUrbPrev = pThisCC->aInFlight[iInFlightPrev].pUrb;
        if (ohciR3HasUrbBeenCanceled(pDevIns, pThis, pUrbPrev, pEd)) /* ensures the copy is correct. */
            return false;

        /* Update the copy and write it back. */
        POHCIITD pITdPrev = ((POHCIITD)pUrbPrev->paTds[0].TdCopy);
        pITdPrev->NextTD = (pITdPrev->NextTD & ~ED_PTR_MASK) | ITdAddrNext;
        ohciR3WriteITd(pDevIns, pThis, ITdAddrPrev, pITdPrev, "ohciR3ServiceIsochronousEndpoint");
    }
    else
    {
        /* It's the head node. update the copy from the caller and write it back. */
        pEd->HeadP = (pEd->HeadP & ~ED_PTR_MASK) | ITdAddrNext;
        ohciR3WriteEd(pDevIns, EdAddr, pEd);
    }

    /*
     * If it's in flight, just mark the URB as unlinked (there is only one ITD per URB atm).
     * Otherwise, retire it to the done queue with an error and cause a done line interrupt (?).
     */
    if (pUrb)
    {
        pUrb->pHci->fUnlinked = true;
        if (ohciR3HasUrbBeenCanceled(pDevIns, pThis, pUrb, pEd)) /* ensures the copy is correct (paranoia). */
            return false;

        POHCIITD pITdCopy = ((POHCIITD)pUrb->paTds[0].TdCopy);
        pITd->NextTD = pITdCopy->NextTD &= ~ED_PTR_MASK;
    }
    else
    {
        pITd->HwInfo &= ~ITD_HWINFO_CC;
        pITd->HwInfo |= OHCI_CC_DATA_OVERRUN;

        pITd->NextTD = pThis->done;
        pThis->done = ITdAddr;

        pThis->dqic = 0;
    }

    ohciR3WriteITd(pDevIns, pThis, ITdAddr, pITd, "ohciR3ServiceIsochronousTdUnlink");
    return true;
}


/**
 * A worker for ohciR3ServiceIsochronousEndpoint which submits the specified TD.
 *
 * @returns true on success.
 * @returns false on failure to submit.
 * @param   pDevIns The device instance.
 * @param   pThis   The OHCI controller instance data, shared edition.
 * @param   pThisCC The OHCI controller instance data, ring-3 edition.
 * @param   pITd    The transfer descriptor to service.
 * @param   ITdAddr The address of the transfer descriptor in gues memory.
 * @param   R       The start packet (frame) relative to the start of frame in HwInfo.
 * @param   pEd     The OHCI endpoint descriptor.
 * @param   EdAddr  The endpoint descriptor address in guest memory.
 */
static bool ohciR3ServiceIsochronousTd(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC,
                                       POHCIITD pITd, uint32_t ITdAddr, const unsigned R, PCOHCIED pEd, uint32_t EdAddr)
{
    /*
     * Determine the endpoint direction.
     */
    VUSBDIRECTION enmDir;
    switch (pEd->hwinfo & ED_HWINFO_DIR)
    {
        case ED_HWINFO_OUT: enmDir = VUSBDIRECTION_OUT; break;
        case ED_HWINFO_IN:  enmDir = VUSBDIRECTION_IN;  break;
        default:
            Log(("ohciR3ServiceIsochronousTd: Invalid direction!!!! Ed.hwdinfo=%#x\n", pEd->hwinfo));
            ohciR3RaiseUnrecoverableError(pDevIns, pThis, 5);
            return false;
    }

    /*
     * Extract the packet sizes and calc the total URB size.
     */
    struct
    {
        uint16_t cb;
        uint16_t off;
    } aPkts[ITD_NUM_PSW];

    /* first entry (R) */
    uint32_t cbTotal = 0;
    if (((uint32_t)pITd->aPSW[R] >> ITD_PSW_CC_SHIFT) < (OHCI_CC_NOT_ACCESSED_0 >> TD_HWINFO_CC_SHIFT))
    {
        Log(("ITdAddr=%RX32 PSW%d.CC=%#x < 'Not Accessed'!\n", ITdAddr, R, pITd->aPSW[R] >> ITD_PSW_CC_SHIFT)); /* => Unrecoverable Error*/
        ohciR3RaiseUnrecoverableError(pDevIns, pThis, 9);
        return false;
    }
    uint16_t offPrev = aPkts[0].off = (pITd->aPSW[R] & ITD_PSW_OFFSET);

    /* R+1..cFrames */
    const unsigned cFrames = ((pITd->HwInfo & ITD_HWINFO_FC) >> ITD_HWINFO_FC_SHIFT) + 1;
    for (unsigned iR = R + 1; iR < cFrames; iR++)
    {
        const uint16_t PSW = pITd->aPSW[iR];
        const uint16_t off = aPkts[iR - R].off = (PSW & ITD_PSW_OFFSET);
        cbTotal += aPkts[iR - R - 1].cb = off - offPrev;
        if (off < offPrev)
        {
            Log(("ITdAddr=%RX32 PSW%d.offset=%#x < offPrev=%#x!\n", ITdAddr, iR, off, offPrev)); /* => Unrecoverable Error*/
            ohciR3RaiseUnrecoverableError(pDevIns, pThis, 6);
            return false;
        }
        if (((uint32_t)PSW >> ITD_PSW_CC_SHIFT) < (OHCI_CC_NOT_ACCESSED_0 >> TD_HWINFO_CC_SHIFT))
        {
            Log(("ITdAddr=%RX32 PSW%d.CC=%#x < 'Not Accessed'!\n", ITdAddr, iR, PSW >> ITD_PSW_CC_SHIFT)); /* => Unrecoverable Error*/
            ohciR3RaiseUnrecoverableError(pDevIns, pThis, 7);
            return false;
        }
        offPrev = off;
    }

    /* calc offEnd and figure out the size of the last packet. */
    const uint32_t offEnd = (pITd->BE & 0xfff)
                          + (((pITd->BE & ITD_BP0_MASK) != (pITd->BP0 & ITD_BP0_MASK)) << 12)
                          + 1 /* BE is inclusive */;
    if (offEnd < offPrev)
    {
        Log(("ITdAddr=%RX32 offEnd=%#x < offPrev=%#x!\n", ITdAddr, offEnd, offPrev)); /* => Unrecoverable Error*/
        ohciR3RaiseUnrecoverableError(pDevIns, pThis, 8);
        return false;
    }
    cbTotal += aPkts[cFrames - 1 - R].cb = offEnd - offPrev;
    Assert(cbTotal <= 0x2000);

    pThis->fIdle = false;   /* Mark as active */

    /*
     * Allocate and initialize a new URB.
     */
    PVUSBURB pUrb = VUSBIRhNewUrb(pThisCC->RootHub.pIRhConn, pEd->hwinfo & ED_HWINFO_FUNCTION, VUSB_DEVICE_PORT_INVALID,
                                  VUSBXFERTYPE_ISOC, enmDir, cbTotal, 1, NULL);
    if (!pUrb)
        /* retry later... */
        return false;

    pUrb->EndPt           = (pEd->hwinfo & ED_HWINFO_ENDPOINT) >> ED_HWINFO_ENDPOINT_SHIFT;
    pUrb->fShortNotOk     = false;
    pUrb->enmStatus       = VUSBSTATUS_OK;
    pUrb->pHci->EdAddr    = EdAddr;
    pUrb->pHci->cTds      = 1;
    pUrb->pHci->fUnlinked = false;
    pUrb->pHci->u32FrameNo = pThis->HcFmNumber;
    pUrb->paTds[0].TdAddr = ITdAddr;
    AssertCompile(sizeof(pUrb->paTds[0].TdCopy) >= sizeof(*pITd));
    memcpy(pUrb->paTds[0].TdCopy, pITd, sizeof(*pITd));
# if 0 /* color the data */
    memset(pUrb->abData, 0xfe, cbTotal);
# endif

    /* copy the data */
    if (    cbTotal
        &&  enmDir != VUSBDIRECTION_IN)
    {
        const uint32_t off0 = pITd->aPSW[R] & ITD_PSW_OFFSET;
        if (off0 < 0x1000)
        {
            if (offEnd > 0x1000)
            {
                /* both pages. */
                const unsigned cb0 = 0x1000 - off0;
                ohciR3PhysRead(pDevIns, (pITd->BP0 & ITD_BP0_MASK) + off0, &pUrb->abData[0], cb0);
                ohciR3PhysRead(pDevIns, pITd->BE & ITD_BP0_MASK, &pUrb->abData[cb0], offEnd & 0xfff);
            }
            else /* a portion of the 1st page. */
                ohciR3PhysRead(pDevIns, (pITd->BP0 & ITD_BP0_MASK) + off0, pUrb->abData, offEnd - off0);
        }
        else /* a portion of the 2nd page. */
            ohciR3PhysRead(pDevIns, (pITd->BE & UINT32_C(0xfffff000)) + (off0 & 0xfff), pUrb->abData, cbTotal);
    }

    /* setup the packets */
    pUrb->cIsocPkts = cFrames - R;
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
    ohciR3InFlightAdd(pThis, pThisCC, ITdAddr, pUrb);
    Log(("%s: ohciR3ServiceIsochronousTd: submitting cbData=%#x cIsocPkts=%d EdAddr=%#010x TdAddr=%#010x SF=%#x (%#x)\n",
         pUrb->pszDesc, pUrb->cbData, pUrb->cIsocPkts, EdAddr, ITdAddr, pITd->HwInfo & ITD_HWINFO_SF, pThis->HcFmNumber));
    ohciR3Unlock(pThisCC);
    int rc = VUSBIRhSubmitUrb(pThisCC->RootHub.pIRhConn, pUrb, &pThisCC->RootHub.Led);
    ohciR3Lock(pThisCC);
    if (RT_SUCCESS(rc))
        return true;

    /* Failure cleanup. Can happen if we're still resetting the device or out of resources. */
    Log(("ohciR3ServiceIsochronousTd: failed submitting pUrb=%p cbData=%#x EdAddr=%#010x cTds=%d ITdAddr0=%#010x - rc=%Rrc\n",
         pUrb, cbTotal, EdAddr, 1, ITdAddr, rc));
    ohciR3InFlightRemove(pThis, pThisCC, ITdAddr);
    return false;
}


/**
 * Service an isochronous endpoint.
 */
static void ohciR3ServiceIsochronousEndpoint(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC, POHCIED pEd, uint32_t EdAddr)
{
    /*
     * We currently process this as if the guest follows the interrupt end point chaining
     * hierarchy described in the documenation. This means that for an isochronous endpoint
     * with a 1 ms interval we expect to find in-flight TDs at the head of the list. We will
     * skip over all in-flight TDs whose timeframe has been exceeded. Those which aren't in
     * flight but which are too late will be retired (possibly out of order, but, we don't
     * care right now).
     *
     * When we reach a TD which still has a buffer which is due for take off, we will
     * stop iterating TDs. If it's in-flight, there isn't anything to be done. Otherwise
     * we will push it onto the runway for immediate take off. In this process we
     * might have to complete buffers which didn't make it on time, something which
     * complicates the kind of status info we need to keep around for the TD.
     *
     * Note: We're currently not making any attempt at reassembling ITDs into URBs.
     *       However, this will become necessary because of EMT scheduling and guest
     *       like linux using one TD for each frame (simple but inefficient for us).
     */
    OHCIITD ITd;
    uint32_t ITdAddr = pEd->HeadP & ED_PTR_MASK;
    uint32_t ITdAddrPrev = 0;
    uint32_t u32NextFrame = UINT32_MAX;
    const uint16_t u16CurFrame = pThis->HcFmNumber;
    for (;;)
    {
        /* check for end-of-chain. */
        if (    ITdAddr == (pEd->TailP & ED_PTR_MASK)
            ||  !ITdAddr)
            break;

        /*
         * If isochronous endpoints are around, don't slow down the timer. Getting the timing right
         * is difficult enough as it is.
         */
        pThis->fIdle = false;

        /*
         * Read the current ITD and check what we're supposed to do about it.
         */
        ohciR3ReadITd(pDevIns, pThis, ITdAddr, &ITd);
        const uint32_t  ITdAddrNext = ITd.NextTD & ED_PTR_MASK;
        const int16_t   R = u16CurFrame - (uint16_t)(ITd.HwInfo & ITD_HWINFO_SF); /* 4.3.2.3 */
        const int16_t   cFrames = ((ITd.HwInfo & ITD_HWINFO_FC) >> ITD_HWINFO_FC_SHIFT) + 1;

        if (R < cFrames)
        {
            /*
             * It's inside the current or a future launch window.
             *
             * We will try maximize the TD in flight here to deal with EMT scheduling
             * issues and similar stuff which will screw up the time. So, we will only
             * stop submitting TD when we reach a gap (in time) or end of the list.
             */
            if (    R < 0   /* (a future frame) */
                &&  (uint16_t)u32NextFrame != (uint16_t)(ITd.HwInfo & ITD_HWINFO_SF))
                break;
            if (ohciR3InFlightFind(pThisCC, ITdAddr) < 0)
                if (!ohciR3ServiceIsochronousTd(pDevIns, pThis, pThisCC, &ITd, ITdAddr, R < 0 ? 0 : R, pEd, EdAddr))
                    break;

            ITdAddrPrev = ITdAddr;
        }
        else
        {
# if 1
            /*
             * Ok, the launch window for this TD has passed.
             * If it's not in flight it should be retired with a DataOverrun status (TD).
             *
             * Don't remove in-flight TDs before they complete.
             * Windows will, upon the completion of another ITD it seems, check for if
             * any other TDs has been unlinked. If we unlink them before they really
             * complete all the packet status codes will be NotAccessed and Windows
             * will fail the URB with status USBD_STATUS_ISOCH_REQUEST_FAILED.
             *
             * I don't know if unlinking TDs out of order could cause similar problems,
             * time will show.
             */
            int iInFlight = ohciR3InFlightFind(pThisCC, ITdAddr);
            if (iInFlight >= 0)
                ITdAddrPrev = ITdAddr;
            else if (!ohciR3ServiceIsochronousTdUnlink(pDevIns, pThis, pThisCC, &ITd, ITdAddr, ITdAddrPrev, NULL, pEd, EdAddr))
            {
                Log(("ohciR3ServiceIsochronousEndpoint: Failed unlinking old ITD.\n"));
                break;
            }
# else /* BAD IDEA: */
            /*
             * Ok, the launch window for this TD has passed.
             * If it's not in flight it should be retired with a DataOverrun status (TD).
             *
             * If it's in flight we will try unlink it from the list prematurely to
             * help the guest to move on and shorten the list we have to walk. We currently
             * are successful with the first URB but then it goes too slowly...
             */
            int iInFlight = ohciR3InFlightFind(pThis, ITdAddr);
            if (!ohciR3ServiceIsochronousTdUnlink(pThis, &ITd, ITdAddr, ITdAddrPrev,
                                                  iInFlight < 0 ? NULL : pThis->aInFlight[iInFlight].pUrb,
                                                  pEd, EdAddr))
            {
                Log(("ohciR3ServiceIsochronousEndpoint: Failed unlinking old ITD.\n"));
                break;
            }
# endif
        }

        /* advance to the next ITD */
        ITdAddr = ITdAddrNext;
        u32NextFrame = (ITd.HwInfo & ITD_HWINFO_SF) + cFrames;
    }
}


/**
 * Checks if a endpoints has TDs queued and is ready to have them processed.
 *
 * @returns true if it's ok to process TDs.
 * @param   pEd     The endpoint data.
 */
DECLINLINE(bool) ohciR3IsEdReady(PCOHCIED pEd)
{
    return (pEd->HeadP & ED_PTR_MASK) != (pEd->TailP & ED_PTR_MASK)
         && !(pEd->HeadP & ED_HEAD_HALTED)
         && !(pEd->hwinfo & ED_HWINFO_SKIP);
}


/**
 * Checks if an endpoint has TDs queued (not necessarily ready to have them processed).
 *
 * @returns true if endpoint may have TDs queued.
 * @param   pEd     The endpoint data.
 */
DECLINLINE(bool) ohciR3IsEdPresent(PCOHCIED pEd)
{
    return (pEd->HeadP & ED_PTR_MASK) != (pEd->TailP & ED_PTR_MASK)
         && !(pEd->HeadP & ED_HEAD_HALTED);
}


/**
 * Services the bulk list.
 *
 * On the bulk list we must reassemble URBs from multiple TDs using heuristics
 * derived from USB tracing done in the guests and guest source code (when available).
 */
static void ohciR3ServiceBulkList(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC)
{
# ifdef LOG_ENABLED
    if (g_fLogBulkEPs)
        ohciR3DumpEdList(pDevIns, pThisCC, pThis->bulk_head, "Bulk before", true);
    if (pThis->bulk_cur)
        Log(("ohciR3ServiceBulkList: bulk_cur=%#010x before listprocessing!!! HCD have positioned us!!!\n", pThis->bulk_cur));
# endif

    /*
     * ", HC will start processing the Bulk list and will set BF [BulkListFilled] to 0"
     * - We've simplified and are always starting at the head of the list and working
     *   our way thru to the end each time.
     */
    pThis->status &= ~OHCI_STATUS_BLF;
    pThis->fBulkNeedsCleaning = false;
    pThis->bulk_cur = 0;

    uint32_t EdAddr = pThis->bulk_head;
    uint32_t cIterations = 256;
    while (EdAddr
        && (pThis->ctl & OHCI_CTL_BLE)
        && (cIterations-- > 0))
    {
        OHCIED Ed;

        /* Bail if previous processing ended up in the unrecoverable error state. */
        if (pThis->intr_status & OHCI_INTR_UNRECOVERABLE_ERROR)
            break;

        ohciR3ReadEd(pDevIns, EdAddr, &Ed);
        Assert(!(Ed.hwinfo & ED_HWINFO_ISO)); /* the guest is screwing us */
        if (ohciR3IsEdReady(&Ed))
        {
            pThis->status |= OHCI_STATUS_BLF;
            pThis->fBulkNeedsCleaning = true;

# if 1
            /*
             * After we figured out that all the TDs submitted for dealing with MSD
             * read/write data really makes up on single URB, and that we must
             * reassemble these TDs into an URB before submitting it, there is no
             * longer any need for servicing anything other than the head *URB*
             * on a bulk endpoint.
             */
            ohciR3ServiceHeadTdMultiple(pDevIns, pThis, pThisCC, VUSBXFERTYPE_BULK, &Ed, EdAddr, "Bulk");
# else
            /*
             * This alternative code was used before we started reassembling URBs from
             * multiple TDs. We keep it handy for debugging.
             */
            uint32_t TdAddr = Ed.HeadP & ED_PTR_MASK;
            if (!ohciR3IsTdInFlight(pThis, TdAddr))
            {
                do
                {
                    if (!ohciR3ServiceTdMultiple(pThis, VUSBXFERTYPE_BULK, &Ed, EdAddr, TdAddr, &TdAddr, "Bulk"))
                    {
                        LogFlow(("ohciR3ServiceBulkList: ohciR3ServiceTdMultiple -> false\n"));
                        break;
                    }
                    if (    (TdAddr & ED_PTR_MASK) == (Ed.TailP & ED_PTR_MASK)
                        ||  !TdAddr /* paranoia */)
                    {
                        LogFlow(("ohciR3ServiceBulkList: TdAddr=%#010RX32 Ed.TailP=%#010RX32\n", TdAddr, Ed.TailP));
                        break;
                    }

                    ohciR3ReadEd(pDevIns, EdAddr, &Ed); /* It might have been updated on URB completion. */
                } while (ohciR3IsEdReady(&Ed));
            }
# endif
        }
        else
        {
            if (Ed.hwinfo & ED_HWINFO_SKIP)
            {
                LogFlow(("ohciR3ServiceBulkList: Ed=%#010RX32 Ed.TailP=%#010RX32 SKIP\n", EdAddr, Ed.TailP));
                /* If the ED is in 'skip' state, no transactions on it are allowed and we must
                 * cancel outstanding URBs, if any.
                 */
                uint8_t uAddr  = Ed.hwinfo & ED_HWINFO_FUNCTION;
                uint8_t uEndPt = (Ed.hwinfo & ED_HWINFO_ENDPOINT) >> ED_HWINFO_ENDPOINT_SHIFT;
                VUSBDIRECTION enmDir = ohciR3GetDirection(pDevIns, pThis, pThisCC, &Ed);
                if (enmDir != VUSBDIRECTION_INVALID)
                {
                    /* Must leave the lock here, see ohciR3ServicePeriodicList for details. */
                    ohciR3Unlock(pThisCC);
                    pThisCC->RootHub.pIRhConn->pfnAbortEpByAddr(pThisCC->RootHub.pIRhConn, uAddr, uEndPt, enmDir);
                    ohciR3Lock(pThisCC);
                    /** @todo should we re-read Ed here? */
                }
            }
        }

        /* Trivial loop detection. */
        if (EdAddr == (Ed.NextED & ED_PTR_MASK))
            break;
        /* Proceed to the next endpoint. */
        EdAddr = Ed.NextED & ED_PTR_MASK;
    }

# ifdef LOG_ENABLED
    if (g_fLogBulkEPs)
        ohciR3DumpEdList(pDevIns, pThisCC, pThis->bulk_head, "Bulk after ", true);
# endif
}


/**
 * Abort outstanding transfers on the bulk list.
 *
 * If the guest disabled bulk list processing, we must abort any outstanding transfers
 * (that is, cancel in-flight URBs associated with the list). This is required because
 * there may be outstanding read URBs that will never get a response from the device
 * and would block further communication.
 */
static void ohciR3UndoBulkList(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC)
{
# ifdef LOG_ENABLED
    if (g_fLogBulkEPs)
        ohciR3DumpEdList(pDevIns, pThisCC, pThis->bulk_head, "Bulk before", true);
    if (pThis->bulk_cur)
        Log(("ohciR3UndoBulkList: bulk_cur=%#010x before list processing!!! HCD has positioned us!!!\n", pThis->bulk_cur));
# endif

    /* This flag follows OHCI_STATUS_BLF, but BLF doesn't change when list processing is disabled. */
    pThis->fBulkNeedsCleaning = false;

    uint32_t EdAddr = pThis->bulk_head;
    uint32_t cIterations = 256;
    while (EdAddr
        && (cIterations-- > 0))
    {
        OHCIED Ed;

        ohciR3ReadEd(pDevIns, EdAddr, &Ed);
        Assert(!(Ed.hwinfo & ED_HWINFO_ISO)); /* the guest is screwing us */
        if (ohciR3IsEdPresent(&Ed))
        {
            uint32_t TdAddr = Ed.HeadP & ED_PTR_MASK;
            if (ohciR3IsTdInFlight(pThisCC, TdAddr))
            {
                LogFlow(("ohciR3UndoBulkList: Ed=%#010RX32 Ed.TailP=%#010RX32 UNDO\n", EdAddr, Ed.TailP));
               /* First we need to determine the transfer direction, which may fail(!). */
               uint8_t uAddr  = Ed.hwinfo & ED_HWINFO_FUNCTION;
               uint8_t uEndPt = (Ed.hwinfo & ED_HWINFO_ENDPOINT) >> ED_HWINFO_ENDPOINT_SHIFT;
               VUSBDIRECTION enmDir = ohciR3GetDirection(pDevIns, pThis, pThisCC, &Ed);
               if (enmDir != VUSBDIRECTION_INVALID)
               {
                   /* Must leave the lock here, see ohciR3ServicePeriodicList for details. */
                   ohciR3Unlock(pThisCC);
                   pThisCC->RootHub.pIRhConn->pfnAbortEpByAddr(pThisCC->RootHub.pIRhConn, uAddr, uEndPt, enmDir);
                   ohciR3Lock(pThisCC);
                   /** @todo should we re-read Ed here? */
               }
            }
        }

        /* Trivial loop detection. */
        if (EdAddr == (Ed.NextED & ED_PTR_MASK))
            break;
        /* Proceed to the next endpoint. */
        EdAddr = Ed.NextED & ED_PTR_MASK;
    }
}


/**
 * Services the control list.
 *
 * The control list has complex URB assembling, but that's taken
 * care of at VUSB level (unlike the other transfer types).
 */
static void ohciR3ServiceCtrlList(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC)
{
# ifdef LOG_ENABLED
    if (g_fLogControlEPs)
        ohciR3DumpEdList(pDevIns, pThisCC, pThis->ctrl_head, "Ctrl before", true);
    if (pThis->ctrl_cur)
        Log(("ohciR3ServiceCtrlList: ctrl_cur=%010x before list processing!!! HCD have positioned us!!!\n", pThis->ctrl_cur));
# endif

    /*
     * ", HC will start processing the list and will set ControlListFilled to 0"
     * - We've simplified and are always starting at the head of the list and working
     *   our way thru to the end each time.
     */
    pThis->status &= ~OHCI_STATUS_CLF;
    pThis->ctrl_cur = 0;

    uint32_t EdAddr = pThis->ctrl_head;
    uint32_t cIterations = 256;
    while ( EdAddr
        && (pThis->ctl & OHCI_CTL_CLE)
        && (cIterations-- > 0))
    {
        OHCIED Ed;

        /* Bail if previous processing ended up in the unrecoverable error state. */
        if (pThis->intr_status & OHCI_INTR_UNRECOVERABLE_ERROR)
            break;

        ohciR3ReadEd(pDevIns, EdAddr, &Ed);
        Assert(!(Ed.hwinfo & ED_HWINFO_ISO)); /* the guest is screwing us */
        if (ohciR3IsEdReady(&Ed))
        {
# if 1
            /*
             * Control TDs depends on order and stage. Only one can be in-flight
             * at any given time. OTOH, some stages are completed immediately,
             * so we process the list until we've got a head which is in-flight
             * or reach the end of the list.
             */
            do
            {
                if (    !ohciR3ServiceHeadTd(pDevIns, pThis, pThisCC, VUSBXFERTYPE_CTRL, &Ed, EdAddr, "Control")
                    ||  ohciR3IsTdInFlight(pThisCC, Ed.HeadP & ED_PTR_MASK))
                {
                    pThis->status |= OHCI_STATUS_CLF;
                    break;
                }
                ohciR3ReadEd(pDevIns, EdAddr, &Ed); /* It might have been updated on URB completion. */
            } while (ohciR3IsEdReady(&Ed));
# else
            /* Simplistic, for debugging. */
            ohciR3ServiceHeadTd(pThis, VUSBXFERTYPE_CTRL, &Ed, EdAddr, "Control");
            pThis->status |= OHCI_STATUS_CLF;
# endif
        }

        /* Trivial loop detection. */
        if (EdAddr == (Ed.NextED & ED_PTR_MASK))
            break;
        /* Proceed to the next endpoint. */
        EdAddr = Ed.NextED & ED_PTR_MASK;
    }

# ifdef LOG_ENABLED
    if (g_fLogControlEPs)
        ohciR3DumpEdList(pDevIns, pThisCC, pThis->ctrl_head, "Ctrl after ", true);
# endif
}


/**
 * Services the periodic list.
 *
 * On the interrupt portion of the periodic list we must reassemble URBs from multiple
 * TDs using heuristics derived from USB tracing done in the guests and guest source
 * code (when available).
 */
static void ohciR3ServicePeriodicList(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC)
{
    /*
     * Read the list head from the HCCA.
     */
    const unsigned  iList = pThis->HcFmNumber % OHCI_HCCA_NUM_INTR;
    uint32_t        EdAddr;
    ohciR3GetDWords(pDevIns, pThis->hcca + iList * sizeof(EdAddr), &EdAddr, 1);

# ifdef LOG_ENABLED
    const uint32_t EdAddrHead = EdAddr;
    if (g_fLogInterruptEPs)
    {
        char sz[48];
        RTStrPrintf(sz, sizeof(sz), "Int%02x before", iList);
        ohciR3DumpEdList(pDevIns, pThisCC, EdAddrHead, sz, true);
    }
# endif

    /*
     * Iterate the endpoint list.
     */
    unsigned cIterations = 128;
    while (EdAddr
        && (pThis->ctl & OHCI_CTL_PLE)
        && (cIterations-- > 0))
    {
        OHCIED Ed;

        /* Bail if previous processing ended up in the unrecoverable error state. */
        if (pThis->intr_status & OHCI_INTR_UNRECOVERABLE_ERROR)
            break;

        ohciR3ReadEd(pDevIns, EdAddr, &Ed);
        if (ohciR3IsEdReady(&Ed))
        {
            /*
             * "There is no separate head pointer of isochronous transfers. The first
             * isochronous Endpoint Descriptor simply links to the last interrupt
             * Endpoint Descriptor."
             */
            if (!(Ed.hwinfo & ED_HWINFO_ISO))
            {
                /*
                 * Presently we will only process the head URB on an interrupt endpoint.
                 */
                ohciR3ServiceHeadTdMultiple(pDevIns, pThis, pThisCC, VUSBXFERTYPE_INTR, &Ed, EdAddr, "Periodic");
            }
            else if (pThis->ctl & OHCI_CTL_IE)
            {
                /*
                 * Presently only the head ITD.
                 */
                ohciR3ServiceIsochronousEndpoint(pDevIns, pThis, pThisCC, &Ed, EdAddr);
            }
            else
                break;
        }
        else
        {
            if (Ed.hwinfo & ED_HWINFO_SKIP)
            {
                Log3(("ohciR3ServicePeriodicList: Ed=%#010RX32 Ed.TailP=%#010RX32 SKIP\n", EdAddr, Ed.TailP));
                /* If the ED is in 'skip' state, no transactions on it are allowed and we must
                 * cancel outstanding URBs, if any.
                 * First we need to determine the transfer direction, which may fail(!).
                 *
                 * Note! We *must* leave the critsect before calling the abort function as
                 *       it typically wants to send synchronous requests to the URB IO thread
                 *       which will enter the critsect when responding.  ohciR3StartFrame
                 *       entered the critsect and I (bird) can't immediately spot any cached
                 *       state info that could get obsoleted while we're doing this (except
                 *       some logging stuff).
                 */
                uint8_t uAddr  = Ed.hwinfo & ED_HWINFO_FUNCTION;
                uint8_t uEndPt = (Ed.hwinfo & ED_HWINFO_ENDPOINT) >> ED_HWINFO_ENDPOINT_SHIFT;
                VUSBDIRECTION enmDir = ohciR3GetDirection(pDevIns, pThis, pThisCC, &Ed);
                if (enmDir != VUSBDIRECTION_INVALID)
                {
                    ohciR3Unlock(pThisCC);
                    pThisCC->RootHub.pIRhConn->pfnAbortEpByAddr(pThisCC->RootHub.pIRhConn, uAddr, uEndPt, enmDir);
                    ohciR3Lock(pThisCC);
                    /** @todo should we re-read Ed here? */
                }
            }
        }
        /* Trivial loop detection. */
        if (EdAddr == (Ed.NextED & ED_PTR_MASK))
            break;
        /* Proceed to the next endpoint. */
        EdAddr = Ed.NextED & ED_PTR_MASK;
    }

# ifdef LOG_ENABLED
    if (g_fLogInterruptEPs)
    {
        char sz[48];
        RTStrPrintf(sz, sizeof(sz), "Int%02x after ", iList);
        ohciR3DumpEdList(pDevIns, pThisCC, EdAddrHead, sz, true);
    }
# endif
}


/**
 * Update the HCCA.
 *
 * @param   pDevIns The device instance.
 * @param   pThis   The OHCI controller instance data, shared edition.
 * @param   pThisCC The OHCI controller instance data, ring-3 edition.
 */
static void ohciR3UpdateHCCA(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC)
{
    OCHIHCCA hcca;
    ohciR3PhysRead(pDevIns, pThis->hcca + OHCI_HCCA_OFS, &hcca, sizeof(hcca));

    hcca.frame = RT_H2LE_U16((uint16_t)pThis->HcFmNumber);
    hcca.pad = 0;

    bool fWriteDoneHeadInterrupt = false;
    if (    pThis->dqic == 0
        &&  (pThis->intr_status & OHCI_INTR_WRITE_DONE_HEAD) == 0)
    {
        uint32_t done = pThis->done;

        if (pThis->intr_status & ~(  OHCI_INTR_MASTER_INTERRUPT_ENABLED | OHCI_INTR_OWNERSHIP_CHANGE
                                   | OHCI_INTR_WRITE_DONE_HEAD) )
            done |= 0x1;

        hcca.done = RT_H2LE_U32(done);
        pThis->done = 0;
        pThis->dqic = 0x7;

        Log(("ohci: Writeback Done (%#010x) on frame %#x (age %#x)\n", hcca.done,
             pThis->HcFmNumber, pThis->HcFmNumber - pThisCC->u32FmDoneQueueTail));
# ifdef LOG_ENABLED
        ohciR3DumpTdQueue(pDevIns, pThisCC, hcca.done & ED_PTR_MASK, "DoneQueue");
# endif
        Assert(RT_OFFSETOF(OCHIHCCA, done) == 4);
# if defined(VBOX_STRICT) || defined(LOG_ENABLED)
        ohciR3InDoneQueueZap(pThisCC);
# endif
        fWriteDoneHeadInterrupt = true;
    }

    Log3(("ohci: Updating HCCA on frame %#x\n", pThis->HcFmNumber));
    ohciR3PhysWriteMeta(pDevIns, pThis->hcca + OHCI_HCCA_OFS, (uint8_t *)&hcca, sizeof(hcca));
    if (fWriteDoneHeadInterrupt)
        ohciR3SetInterrupt(pDevIns, pThis, OHCI_INTR_WRITE_DONE_HEAD);
    RT_NOREF(pThisCC);
}


/**
 * Go over the in-flight URB list and cancel any URBs that are no longer in use.
 * This occurs when the host removes EDs or TDs from the lists and we don't notice
 * the sKip bit. Such URBs must be promptly canceled, otherwise there is a risk
 * they might "steal" data destined for another URB.
 */
static void ohciR3CancelOrphanedURBs(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC)
{
    bool fValidHCCA = !(    pThis->hcca >= OHCI_HCCA_MASK
                        ||  pThis->hcca < ~OHCI_HCCA_MASK);
    unsigned    i, cLeft;
    int         j;
    uint32_t    EdAddr;
    PVUSBURB    pUrb;

    /* If the HCCA is not currently valid, or there are no in-flight URBs,
     * there's nothing to do.
     */
    if (!fValidHCCA || !pThisCC->cInFlight)
        return;

    /* Initially mark all in-flight URBs as inactive. */
    for (i = 0, cLeft = pThisCC->cInFlight; cLeft && i < RT_ELEMENTS(pThisCC->aInFlight); i++)
    {
        if (pThisCC->aInFlight[i].pUrb)
        {
            pThisCC->aInFlight[i].fInactive = true;
            cLeft--;
        }
    }
    Assert(cLeft == 0);

# ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE
    /* Get hcca data to minimize calls to ohciR3GetDWords/PDMDevHlpPCIPhysRead. */
    uint32_t au32HCCA[OHCI_HCCA_NUM_INTR];
    ohciR3GetDWords(pDevIns, pThis->hcca, au32HCCA, OHCI_HCCA_NUM_INTR);
# endif

    /* Go over all bulk/control/interrupt endpoint lists; any URB found in these lists
     * is marked as active again.
     */
    for (i = 0; i < OHCI_HCCA_NUM_INTR + 2; i++)
    {
        switch (i)
        {
        case OHCI_HCCA_NUM_INTR:
            EdAddr = pThis->bulk_head;
            break;
        case OHCI_HCCA_NUM_INTR + 1:
            EdAddr = pThis->ctrl_head;
            break;
        default:
# ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE
            EdAddr = au32HCCA[i];
# else
            ohciR3GetDWords(pDevIns, pThis->hcca + i * sizeof(EdAddr), &EdAddr, 1);
# endif
            break;
        }

        unsigned cIterED = 128;
        while ( EdAddr
            && (cIterED-- > 0))
        {
            OHCIED Ed;
            OHCITD Td;

            ohciR3ReadEd(pDevIns, EdAddr, &Ed);
            uint32_t TdAddr = Ed.HeadP & ED_PTR_MASK;
            uint32_t TailP  = Ed.TailP & ED_PTR_MASK;
            unsigned cIterTD = 0;
            if (  !(Ed.hwinfo & ED_HWINFO_SKIP)
                && (TdAddr != TailP))
            {
# ifdef VBOX_WITH_OHCI_PHYS_READ_CACHE
                ohciR3PhysReadCacheInvalidate(&pThisCC->CacheTD);
# endif
                do
                {
                    ohciR3ReadTd(pDevIns, TdAddr, &Td);
                    j = ohciR3InFlightFind(pThisCC, TdAddr);
                    if (j > -1)
                        pThisCC->aInFlight[j].fInactive = false;
                    TdAddr = Td.NextTD & ED_PTR_MASK;
                    /* See @bugref{8125}.
                     * Sometimes the ED is changed by the guest between ohciR3ReadEd above and here.
                     * Then the code reads TD pointed by the new TailP, which is not allowed.
                     * Luckily Windows guests have Td.NextTD = 0 in the tail TD.
                     * Also having a real TD at 0 is very unlikely.
                     * So do not continue.
                     */
                    if (TdAddr == 0)
                        break;
                    /* Failsafe for temporarily looped lists. */
                    if (++cIterTD == 128)
                        break;
                } while (TdAddr != (Ed.TailP & ED_PTR_MASK));
            }
            /* Trivial loop detection. */
            if (EdAddr == (Ed.NextED & ED_PTR_MASK))
                break;
            /* Proceed to the next endpoint. */
            EdAddr = Ed.NextED & ED_PTR_MASK;
        }
    }

    /* In-flight URBs still marked as inactive are not used anymore and need
     * to be canceled.
     * On most hosts we only have the option to abort endpoint/pipe, and can't cancel
     * a single URB. So we first collect information about which endpoints need to be
     * aborted and then abort every such endpoint only once.
     * The endpoint information for each device/address is packed into a 32-bit integer.
     */
    uint32_t    uDevEps[256] = { 0 };
    AssertCompile(sizeof(pUrb->DstAddress) == 1);

    for (i = 0, cLeft = pThisCC->cInFlight; cLeft && i < RT_ELEMENTS(pThisCC->aInFlight); i++)
    {
        if (pThisCC->aInFlight[i].pUrb)
        {
            cLeft--;
            pUrb = pThisCC->aInFlight[i].pUrb;
            if (   pThisCC->aInFlight[i].fInactive
                && pUrb->enmState == VUSBURBSTATE_IN_FLIGHT
                && pUrb->enmType != VUSBXFERTYPE_CTRL)
            {
                if (pUrb->enmDir == VUSBDIRECTION_OUT || pUrb->enmDir == VUSBDIRECTION_IN)
                    uDevEps[pUrb->DstAddress] |= (pUrb->enmDir == VUSBDIRECTION_OUT ? 1 : 2) << (pUrb->EndPt * 2);
            }
        }
    }
    Assert(cLeft == 0);

    ohciR3Unlock(pThisCC);
    for (unsigned uAddr = 0; uAddr < 256; ++uAddr)
    {
        if (uDevEps[uAddr])
        {
            for (unsigned uEp = 0; uEp < 16; ++uEp)
            {
                unsigned uDirs = (uDevEps[uAddr] >> (uEp * 2)) & 3;
                if (uDirs)
                {
                    if (uDirs & 1)
                        pThisCC->RootHub.pIRhConn->pfnAbortEpByAddr(pThisCC->RootHub.pIRhConn, uAddr, uEp, VUSBDIRECTION_OUT);
                    if (uDirs & 2)
                        pThisCC->RootHub.pIRhConn->pfnAbortEpByAddr(pThisCC->RootHub.pIRhConn, uAddr, uEp, VUSBDIRECTION_IN);
                }
            }
        }
    }
    ohciR3Lock(pThisCC);
}


/**
 * Generate a Start-Of-Frame event, and set a timer for End-Of-Frame.
 */
static void ohciR3StartOfFrame(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC)
{
# ifdef LOG_ENABLED
    const uint32_t status_old = pThis->status;
# endif

    /*
     * Update HcFmRemaining.FRT and update start of frame time.
     */
    pThis->frt = pThis->fit;
    pThis->SofTime += pThis->cTicksPerFrame;

    /*
     * Check that the HCCA address isn't bogus. Linux 2.4.x is known to start
     * the bus with a hcca of 0 to work around problem with a specific controller.
     */
    bool fValidHCCA = !(    pThis->hcca >= OHCI_HCCA_MASK
                        ||  pThis->hcca < ~OHCI_HCCA_MASK);

# if 1
    /*
     * Update the HCCA.
     * Should be done after SOF but before HC read first ED in this frame.
     */
    if (fValidHCCA)
        ohciR3UpdateHCCA(pDevIns, pThis, pThisCC);
# endif

    /* "After writing to HCCA, HC will set SF in HcInterruptStatus" - guest isn't executing, so ignore the order! */
    ohciR3SetInterrupt(pDevIns, pThis, OHCI_INTR_START_OF_FRAME);

    if (pThis->fno)
    {
        ohciR3SetInterrupt(pDevIns, pThis, OHCI_INTR_FRAMENUMBER_OVERFLOW);
        pThis->fno = 0;
    }

    /* If the HCCA address is invalid, we're quitting here to avoid doing something which cannot be reported to the HCD. */
    if (!fValidHCCA)
    {
        Log(("ohciR3StartOfFrame: skipping hcca part because hcca=%RX32 (our 'valid' range: %RX32-%RX32)\n",
             pThis->hcca, ~OHCI_HCCA_MASK, OHCI_HCCA_MASK));
        return;
    }

    /*
     * Periodic EPs.
     */
    if (pThis->ctl & OHCI_CTL_PLE)
        ohciR3ServicePeriodicList(pDevIns, pThis, pThisCC);

    /*
     * Control EPs.
     */
    if (    (pThis->ctl & OHCI_CTL_CLE)
        &&  (pThis->status & OHCI_STATUS_CLF) )
        ohciR3ServiceCtrlList(pDevIns, pThis, pThisCC);

    /*
     * Bulk EPs.
     */
    if (    (pThis->ctl & OHCI_CTL_BLE)
        &&  (pThis->status & OHCI_STATUS_BLF))
        ohciR3ServiceBulkList(pDevIns, pThis, pThisCC);
    else if ((pThis->status & OHCI_STATUS_BLF)
        &&    pThis->fBulkNeedsCleaning)
        ohciR3UndoBulkList(pDevIns, pThis, pThisCC);    /* If list disabled but not empty, abort endpoints. */

# if 0
    /*
     * Update the HCCA after processing the lists and everything. A bit experimental.
     *
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
    ohciR3UpdateHCCA(pThis);
# endif

# ifdef LOG_ENABLED
    if (pThis->status ^ status_old)
    {
        uint32_t val = pThis->status;
        uint32_t chg = val ^ status_old; NOREF(chg);
        Log2(("ohciR3StartOfFrame: HcCommandStatus=%#010x: %sHCR=%d %sCLF=%d %sBLF=%d %sOCR=%d %sSOC=%d\n",
              val,
              chg & RT_BIT(0) ? "*" : "", val & 1,
              chg & RT_BIT(1) ? "*" : "", (val >> 1) & 1,
              chg & RT_BIT(2) ? "*" : "", (val >> 2) & 1,
              chg & RT_BIT(3) ? "*" : "", (val >> 3) & 1,
              chg & (3<<16)? "*" : "", (val >> 16) & 3));
    }
# endif
}


/**
 * Updates the HcFmNumber and FNO registers.
 */
static void ohciR3BumpFrameNumber(POHCI pThis)
{
    const uint16_t u16OldFmNumber = pThis->HcFmNumber++;
    if ((u16OldFmNumber ^ pThis->HcFmNumber) & RT_BIT(15))
        pThis->fno = 1;
}


/**
 * Callback for periodic frame processing.
 */
static DECLCALLBACK(bool) ohciR3StartFrame(PVUSBIROOTHUBPORT pInterface, uint32_t u32FrameNo)
{
    RT_NOREF(u32FrameNo);
    POHCICC    pThisCC = VUSBIROOTHUBPORT_2_OHCI(pInterface);
    PPDMDEVINS pDevIns = pThisCC->pDevInsR3;
    POHCI      pThis   = PDMDEVINS_2_DATA(pDevIns, POHCI);

    ohciR3Lock(pThisCC);

    /* Reset idle detection flag */
    pThis->fIdle = true;

# ifdef VBOX_WITH_OHCI_PHYS_READ_STATS
    physReadStatsReset(&g_PhysReadState);
# endif

    if (!(pThis->intr_status & OHCI_INTR_UNRECOVERABLE_ERROR))
    {
        /* Frame boundary, so do EOF stuff here. */
        ohciR3BumpFrameNumber(pThis);
        if ( (pThis->dqic != 0x7) && (pThis->dqic != 0))
            pThis->dqic--;

        /* Clean up any URBs that have been removed. */
        ohciR3CancelOrphanedURBs(pDevIns, pThis, pThisCC);

        /* Start the next frame. */
        ohciR3StartOfFrame(pDevIns, pThis, pThisCC);
    }

# ifdef VBOX_WITH_OHCI_PHYS_READ_STATS
    physReadStatsPrint(&g_PhysReadState);
# endif

    ohciR3Unlock(pThisCC);
    return pThis->fIdle;
}


/**
 * @interface_method_impl{VUSBIROOTHUBPORT,pfnFrameRateChanged}
 */
static DECLCALLBACK(void) ohciR3FrameRateChanged(PVUSBIROOTHUBPORT pInterface, uint32_t u32FrameRate)
{
    POHCICC    pThisCC = VUSBIROOTHUBPORT_2_OHCI(pInterface);
    PPDMDEVINS pDevIns = pThisCC->pDevInsR3;
    POHCI      pThis   = PDMDEVINS_2_DATA(pDevIns, POHCI);

    Assert(u32FrameRate <= OHCI_DEFAULT_TIMER_FREQ);

    pThis->cTicksPerFrame = pThis->u64TimerHz / u32FrameRate;
    if (!pThis->cTicksPerFrame)
        pThis->cTicksPerFrame = 1;
    pThis->cTicksPerUsbTick = pThis->u64TimerHz >= VUSB_BUS_HZ ? pThis->u64TimerHz / VUSB_BUS_HZ : 1;
}


/**
 * Start sending SOF tokens across the USB bus, lists are processed in
 * next frame
 */
static void ohciR3BusStart(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC)
{
    pThisCC->RootHub.pIRhConn->pfnPowerOn(pThisCC->RootHub.pIRhConn);
    pThis->dqic = 0x7;

    Log(("ohci: Bus started\n"));

    pThis->SofTime = PDMDevHlpTMTimeVirtGet(pDevIns);
    int rc = pThisCC->RootHub.pIRhConn->pfnSetPeriodicFrameProcessing(pThisCC->RootHub.pIRhConn, OHCI_DEFAULT_TIMER_FREQ);
    AssertRC(rc);
}


/**
 * Stop sending SOF tokens on the bus
 */
static void ohciR3BusStop(POHCICC pThisCC)
{
    int rc = pThisCC->RootHub.pIRhConn->pfnSetPeriodicFrameProcessing(pThisCC->RootHub.pIRhConn, 0);
    AssertRC(rc);
    pThisCC->RootHub.pIRhConn->pfnPowerOff(pThisCC->RootHub.pIRhConn);
}


/**
 * Move in to resume state
 */
static void ohciR3BusResume(PPDMDEVINS pDevIns, POHCI pThis, POHCICC pThisCC, bool fHardware)
{
    pThis->ctl &= ~OHCI_CTL_HCFS;
    pThis->ctl |= OHCI_USB_RESUME;

    LogFunc(("fHardware=%RTbool RWE=%s\n",
         fHardware, (pThis->ctl & OHCI_CTL_RWE) ? "on" : "off"));

    if (fHardware && (pThis->ctl & OHCI_CTL_RWE))
        ohciR3SetInterrupt(pDevIns, pThis, OHCI_INTR_RESUME_DETECT);

    ohciR3BusStart(pDevIns, pThis, pThisCC);
}


/* Power a port up or down */
static void ohciR3RhPortPower(POHCIROOTHUBR3 pRh, unsigned iPort, bool fPowerUp)
{
    POHCIHUBPORT pPort = &pRh->aPorts[iPort];
    bool fOldPPS = !!(pPort->fReg & OHCI_PORT_PPS);

    LogFlowFunc(("iPort=%u fPowerUp=%RTbool\n", iPort, fPowerUp));

    if (fPowerUp)
    {
        /* power up */
        if (pPort->fAttached)
            pPort->fReg |= OHCI_PORT_CCS;
        if (pPort->fReg & OHCI_PORT_CCS)
            pPort->fReg |= OHCI_PORT_PPS;
        if (pPort->fAttached && !fOldPPS)
            VUSBIRhDevPowerOn(pRh->pIRhConn, OHCI_PORT_2_VUSB_PORT(iPort));
    }
    else
    {
        /* power down */
        pPort->fReg &= ~(OHCI_PORT_PPS | OHCI_PORT_CCS | OHCI_PORT_PSS | OHCI_PORT_PRS);
        if (pPort->fAttached && fOldPPS)
            VUSBIRhDevPowerOff(pRh->pIRhConn, OHCI_PORT_2_VUSB_PORT(iPort));
    }
}

#endif /* IN_RING3 */

/**
 * Read the HcRevision register.
 */
static VBOXSTRICTRC HcRevision_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    Log2(("HcRevision_r() -> 0x10\n"));
    *pu32Value = 0x10; /* OHCI revision 1.0, no emulation. */
    return VINF_SUCCESS;
}

/**
 * Write to the HcRevision register.
 */
static VBOXSTRICTRC HcRevision_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t u32Value)
{
    RT_NOREF(pDevIns, pThis, iReg, u32Value);
    Log2(("HcRevision_w(%#010x) - denied\n", u32Value));
    ASSERT_GUEST_MSG_FAILED(("Invalid operation!!! u32Value=%#010x\n", u32Value));
    return VINF_SUCCESS;
}

/**
 * Read the HcControl register.
 */
static VBOXSTRICTRC HcControl_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    uint32_t ctl = pThis->ctl;
    Log2(("HcControl_r -> %#010x - CBSR=%d PLE=%d IE=%d CLE=%d BLE=%d HCFS=%#x IR=%d RWC=%d RWE=%d\n",
          ctl, ctl & 3, (ctl >> 2) & 1, (ctl >> 3) & 1, (ctl >> 4) & 1, (ctl >> 5) & 1, (ctl >> 6) & 3, (ctl >> 8) & 1,
          (ctl >> 9) & 1, (ctl >> 10) & 1));
    *pu32Value = ctl;
    return VINF_SUCCESS;
}

/**
 * Write the HcControl register.
 */
static VBOXSTRICTRC HcControl_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(iReg);

    /* log it. */
    uint32_t chg = pThis->ctl ^ val; NOREF(chg);
    Log2(("HcControl_w(%#010x) => %sCBSR=%d %sPLE=%d %sIE=%d %sCLE=%d %sBLE=%d %sHCFS=%#x %sIR=%d %sRWC=%d %sRWE=%d\n",
          val,
          chg & 3       ? "*" : "",  val        & 3,
          chg & RT_BIT(2)  ? "*" : "", (val >>  2) & 1,
          chg & RT_BIT(3)  ? "*" : "", (val >>  3) & 1,
          chg & RT_BIT(4)  ? "*" : "", (val >>  4) & 1,
          chg & RT_BIT(5)  ? "*" : "", (val >>  5) & 1,
          chg & (3 << 6)? "*" : "", (val >>  6) & 3,
          chg & RT_BIT(8)  ? "*" : "", (val >>  8) & 1,
          chg & RT_BIT(9)  ? "*" : "", (val >>  9) & 1,
          chg & RT_BIT(10) ? "*" : "", (val >> 10) & 1));
    if (val & ~0x07ff)
        Log2(("Unknown bits %#x are set!!!\n", val & ~0x07ff));

    /* see what changed and take action on that. */
    uint32_t old_state = pThis->ctl & OHCI_CTL_HCFS;
    uint32_t new_state = val & OHCI_CTL_HCFS;

#ifdef IN_RING3
    pThis->ctl = val;
    if (new_state != old_state)
    {
        POHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCICC);
        switch (new_state)
        {
            case OHCI_USB_OPERATIONAL:
                LogRel(("OHCI: USB Operational\n"));
                ohciR3BusStart(pDevIns, pThis, pThisCC);
                break;
            case OHCI_USB_SUSPEND:
                ohciR3BusStop(pThisCC);
                LogRel(("OHCI: USB Suspended\n"));
                break;
            case OHCI_USB_RESUME:
                LogRel(("OHCI: USB Resume\n"));
                ohciR3BusResume(pDevIns, pThis, pThisCC, false /* not hardware */);
                break;
            case OHCI_USB_RESET:
            {
                LogRel(("OHCI: USB Reset\n"));
                ohciR3BusStop(pThisCC);
                /** @todo This should probably do a real reset, but we don't implement
                 * that correctly in the roothub reset callback yet. check it's
                 * comments and argument for more details. */
                pThisCC->RootHub.pIRhConn->pfnReset(pThisCC->RootHub.pIRhConn, false /* don't do a real reset */);
                break;
            }
        }
    }
#else  /* !IN_RING3 */
    RT_NOREF(pDevIns);
    if ( new_state != old_state )
    {
        Log2(("HcControl_w: state changed -> VINF_IOM_R3_MMIO_WRITE\n"));
        return VINF_IOM_R3_MMIO_WRITE;
    }
    pThis->ctl = val;
#endif /* !IN_RING3 */

    return VINF_SUCCESS;
}

/**
 * Read the HcCommandStatus register.
 */
static VBOXSTRICTRC HcCommandStatus_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    uint32_t status = pThis->status;
    Log2(("HcCommandStatus_r() -> %#010x - HCR=%d CLF=%d BLF=%d OCR=%d SOC=%d\n",
          status, status & 1, (status >> 1) & 1, (status >> 2) & 1, (status >> 3) & 1, (status >> 16) & 3));
    *pu32Value = status;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write to the HcCommandStatus register.
 */
static VBOXSTRICTRC HcCommandStatus_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);

    /* log */
    uint32_t chg = pThis->status ^ val; NOREF(chg);
    Log2(("HcCommandStatus_w(%#010x) => %sHCR=%d %sCLF=%d %sBLF=%d %sOCR=%d %sSOC=%d\n",
          val,
          chg & RT_BIT(0) ? "*" : "", val & 1,
          chg & RT_BIT(1) ? "*" : "", (val >> 1) & 1,
          chg & RT_BIT(2) ? "*" : "", (val >> 2) & 1,
          chg & RT_BIT(3) ? "*" : "", (val >> 3) & 1,
          chg & (3<<16)? "!!!":"", (pThis->status >> 16) & 3));
    if (val & ~0x0003000f)
        Log2(("Unknown bits %#x are set!!!\n", val & ~0x0003000f));

    /* SOC is read-only */
    val = (val & ~OHCI_STATUS_SOC);

#ifdef IN_RING3
    /* "bits written as '0' remain unchanged in the register" */
    pThis->status |= val;
    if (pThis->status & OHCI_STATUS_HCR)
    {
        LogRel(("OHCI: Software reset\n"));
        ohciR3DoReset(pDevIns, pThis, PDMDEVINS_2_DATA_CC(pDevIns, POHCICC), OHCI_USB_SUSPEND, false /* N/A */);
    }
#else
    if ((pThis->status | val) & OHCI_STATUS_HCR)
    {
        LogFlow(("HcCommandStatus_w: reset -> VINF_IOM_R3_MMIO_WRITE\n"));
        return VINF_IOM_R3_MMIO_WRITE;
    }
    pThis->status |= val;
#endif
    return VINF_SUCCESS;
}

/**
 * Read the HcInterruptStatus register.
 */
static VBOXSTRICTRC HcInterruptStatus_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    uint32_t val = pThis->intr_status;
    Log2(("HcInterruptStatus_r() -> %#010x - SO=%d WDH=%d SF=%d RD=%d UE=%d FNO=%d RHSC=%d OC=%d\n",
          val, val & 1, (val >> 1) & 1, (val >> 2) & 1, (val >> 3) & 1, (val >> 4) & 1, (val >> 5) & 1,
          (val >> 6) & 1, (val >> 30) & 1));
    *pu32Value = val;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write to the HcInterruptStatus register.
 */
static VBOXSTRICTRC HcInterruptStatus_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(iReg);

    uint32_t res = pThis->intr_status & ~val;
    uint32_t chg = pThis->intr_status ^ res; NOREF(chg);

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CsIrq, VINF_IOM_R3_MMIO_WRITE);
    if (rc != VINF_SUCCESS)
        return rc;

    Log2(("HcInterruptStatus_w(%#010x) => %sSO=%d %sWDH=%d %sSF=%d %sRD=%d %sUE=%d %sFNO=%d %sRHSC=%d %sOC=%d\n",
          val,
          chg & RT_BIT(0) ? "*" : "",  res       & 1,
          chg & RT_BIT(1) ? "*" : "", (res >> 1) & 1,
          chg & RT_BIT(2) ? "*" : "", (res >> 2) & 1,
          chg & RT_BIT(3) ? "*" : "", (res >> 3) & 1,
          chg & RT_BIT(4) ? "*" : "", (res >> 4) & 1,
          chg & RT_BIT(5) ? "*" : "", (res >> 5) & 1,
          chg & RT_BIT(6) ? "*" : "", (res >> 6) & 1,
          chg & RT_BIT(30)? "*" : "", (res >> 30) & 1));
    if (    (val & ~0xc000007f)
        &&  val != 0xffffffff /* ignore clear-all-like requests from xp. */)
        Log2(("Unknown bits %#x are set!!!\n", val & ~0xc000007f));

    /* "The Host Controller Driver may clear specific bits in this
     * register by writing '1' to bit positions to be cleared"
     */
    pThis->intr_status &= ~val;
    ohciUpdateInterruptLocked(pDevIns, pThis, "HcInterruptStatus_w");
    PDMDevHlpCritSectLeave(pDevIns, &pThis->CsIrq);
    return VINF_SUCCESS;
}

/**
 * Read the HcInterruptEnable register
 */
static VBOXSTRICTRC HcInterruptEnable_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    uint32_t val = pThis->intr;
    Log2(("HcInterruptEnable_r() -> %#010x - SO=%d WDH=%d SF=%d RD=%d UE=%d FNO=%d RHSC=%d OC=%d MIE=%d\n",
          val, val & 1, (val >> 1) & 1, (val >> 2) & 1, (val >> 3) & 1, (val >> 4) & 1, (val >> 5) & 1,
          (val >> 6) & 1, (val >> 30) & 1, (val >> 31) & 1));
    *pu32Value = val;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Writes to the HcInterruptEnable register.
 */
static VBOXSTRICTRC HcInterruptEnable_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(iReg);
    uint32_t res = pThis->intr | val;
    uint32_t chg = pThis->intr ^ res; NOREF(chg);

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CsIrq, VINF_IOM_R3_MMIO_WRITE);
    if (rc != VINF_SUCCESS)
        return rc;

    Log2(("HcInterruptEnable_w(%#010x) => %sSO=%d %sWDH=%d %sSF=%d %sRD=%d %sUE=%d %sFNO=%d %sRHSC=%d %sOC=%d %sMIE=%d\n",
          val,
          chg & RT_BIT(0)  ? "*" : "",  res        & 1,
          chg & RT_BIT(1)  ? "*" : "", (res >>  1) & 1,
          chg & RT_BIT(2)  ? "*" : "", (res >>  2) & 1,
          chg & RT_BIT(3)  ? "*" : "", (res >>  3) & 1,
          chg & RT_BIT(4)  ? "*" : "", (res >>  4) & 1,
          chg & RT_BIT(5)  ? "*" : "", (res >>  5) & 1,
          chg & RT_BIT(6)  ? "*" : "", (res >>  6) & 1,
          chg & RT_BIT(30) ? "*" : "", (res >> 30) & 1,
          chg & RT_BIT(31) ? "*" : "", (res >> 31) & 1));
    if (val & ~0xc000007f)
        Log2(("Uknown bits %#x are set!!!\n", val & ~0xc000007f));

    pThis->intr |= val;
    ohciUpdateInterruptLocked(pDevIns, pThis, "HcInterruptEnable_w");
    PDMDevHlpCritSectLeave(pDevIns, &pThis->CsIrq);
    return VINF_SUCCESS;
}

/**
 * Reads the HcInterruptDisable register.
 */
static VBOXSTRICTRC HcInterruptDisable_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
#if 1 /** @todo r=bird: "On read, the current value of the HcInterruptEnable register is returned." */
    uint32_t val = pThis->intr;
#else /* old code. */
    uint32_t val = ~pThis->intr;
#endif
    Log2(("HcInterruptDisable_r() -> %#010x - SO=%d WDH=%d SF=%d RD=%d UE=%d FNO=%d RHSC=%d OC=%d MIE=%d\n",
          val, val & 1, (val >> 1) & 1, (val >> 2) & 1, (val >> 3) & 1, (val >> 4) & 1, (val >> 5) & 1,
          (val >> 6) & 1, (val >> 30) & 1, (val >> 31) & 1));

    *pu32Value = val;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Writes to the HcInterruptDisable register.
 */
static VBOXSTRICTRC HcInterruptDisable_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(iReg);
    uint32_t res = pThis->intr & ~val;
    uint32_t chg = pThis->intr ^ res; NOREF(chg);

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CsIrq, VINF_IOM_R3_MMIO_WRITE);
    if (rc != VINF_SUCCESS)
        return rc;

    Log2(("HcInterruptDisable_w(%#010x) => %sSO=%d %sWDH=%d %sSF=%d %sRD=%d %sUE=%d %sFNO=%d %sRHSC=%d %sOC=%d %sMIE=%d\n",
          val,
          chg & RT_BIT(0)  ? "*" : "",  res        & 1,
          chg & RT_BIT(1)  ? "*" : "", (res >>  1) & 1,
          chg & RT_BIT(2)  ? "*" : "", (res >>  2) & 1,
          chg & RT_BIT(3)  ? "*" : "", (res >>  3) & 1,
          chg & RT_BIT(4)  ? "*" : "", (res >>  4) & 1,
          chg & RT_BIT(5)  ? "*" : "", (res >>  5) & 1,
          chg & RT_BIT(6)  ? "*" : "", (res >>  6) & 1,
          chg & RT_BIT(30) ? "*" : "", (res >> 30) & 1,
          chg & RT_BIT(31) ? "*" : "", (res >> 31) & 1));
    /* Don't bitch about invalid bits here since it makes sense to disable
     * interrupts you don't know about. */

    pThis->intr &= ~val;
    ohciUpdateInterruptLocked(pDevIns, pThis, "HcInterruptDisable_w");
    PDMDevHlpCritSectLeave(pDevIns, &pThis->CsIrq);
    return VINF_SUCCESS;
}

/**
 * Read the HcHCCA register (Host Controller Communications Area physical address).
 */
static VBOXSTRICTRC HcHCCA_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    Log2(("HcHCCA_r() -> %#010x\n", pThis->hcca));
    *pu32Value = pThis->hcca;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write to the HcHCCA register (Host Controller Communications Area physical address).
 */
static VBOXSTRICTRC HcHCCA_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t Value)
{
    Log2(("HcHCCA_w(%#010x) - old=%#010x new=%#010x\n", Value, pThis->hcca, Value & OHCI_HCCA_MASK));
    pThis->hcca = Value & OHCI_HCCA_MASK;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Read the HcPeriodCurrentED register.
 */
static VBOXSTRICTRC HcPeriodCurrentED_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    Log2(("HcPeriodCurrentED_r() -> %#010x\n", pThis->per_cur));
    *pu32Value = pThis->per_cur;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write to the HcPeriodCurrentED register.
 */
static VBOXSTRICTRC HcPeriodCurrentED_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    Log(("HcPeriodCurrentED_w(%#010x) - old=%#010x new=%#010x (This is a read only register, only the linux guys don't respect that!)\n",
         val, pThis->per_cur, val & ~7));
    //AssertMsgFailed(("HCD (Host Controller Driver) should not write to HcPeriodCurrentED! val=%#010x (old=%#010x)\n", val, pThis->per_cur));
    AssertMsg(!(val & 7), ("Invalid alignment, val=%#010x\n", val));
    pThis->per_cur = val & ~7;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Read the HcControlHeadED register.
 */
static VBOXSTRICTRC HcControlHeadED_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    Log2(("HcControlHeadED_r() -> %#010x\n", pThis->ctrl_head));
    *pu32Value = pThis->ctrl_head;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write to the HcControlHeadED register.
 */
static VBOXSTRICTRC HcControlHeadED_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    Log2(("HcControlHeadED_w(%#010x) - old=%#010x new=%#010x\n", val, pThis->ctrl_head, val & ~7));
    AssertMsg(!(val & 7), ("Invalid alignment, val=%#010x\n", val));
    pThis->ctrl_head = val & ~7;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Read the HcControlCurrentED register.
 */
static VBOXSTRICTRC HcControlCurrentED_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    Log2(("HcControlCurrentED_r() -> %#010x\n", pThis->ctrl_cur));
    *pu32Value = pThis->ctrl_cur;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write to the HcControlCurrentED register.
 */
static VBOXSTRICTRC HcControlCurrentED_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    Log2(("HcControlCurrentED_w(%#010x) - old=%#010x new=%#010x\n", val, pThis->ctrl_cur, val & ~7));
    AssertMsg(!(pThis->ctl & OHCI_CTL_CLE), ("Illegal write! HcControl.ControlListEnabled is set! val=%#010x\n", val));
    AssertMsg(!(val & 7), ("Invalid alignment, val=%#010x\n", val));
    pThis->ctrl_cur = val & ~7;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Read the HcBulkHeadED register.
 */
static VBOXSTRICTRC HcBulkHeadED_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    Log2(("HcBulkHeadED_r() -> %#010x\n", pThis->bulk_head));
    *pu32Value = pThis->bulk_head;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write to the HcBulkHeadED register.
 */
static VBOXSTRICTRC HcBulkHeadED_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    Log2(("HcBulkHeadED_w(%#010x) - old=%#010x new=%#010x\n", val, pThis->bulk_head, val & ~7));
    AssertMsg(!(val & 7), ("Invalid alignment, val=%#010x\n", val));
    pThis->bulk_head = val & ~7; /** @todo The ATI OHCI controller on my machine enforces 16-byte address alignment. */
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Read the HcBulkCurrentED register.
 */
static VBOXSTRICTRC HcBulkCurrentED_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    Log2(("HcBulkCurrentED_r() -> %#010x\n", pThis->bulk_cur));
    *pu32Value = pThis->bulk_cur;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write to the HcBulkCurrentED register.
 */
static VBOXSTRICTRC HcBulkCurrentED_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    Log2(("HcBulkCurrentED_w(%#010x) - old=%#010x new=%#010x\n", val, pThis->bulk_cur, val & ~7));
    AssertMsg(!(pThis->ctl & OHCI_CTL_BLE), ("Illegal write! HcControl.BulkListEnabled is set! val=%#010x\n", val));
    AssertMsg(!(val & 7), ("Invalid alignment, val=%#010x\n", val));
    pThis->bulk_cur = val & ~7;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}


/**
 * Read the HcDoneHead register.
 */
static VBOXSTRICTRC HcDoneHead_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    Log2(("HcDoneHead_r() -> 0x%#08x\n", pThis->done));
    *pu32Value = pThis->done;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write to the HcDoneHead register.
 */
static VBOXSTRICTRC HcDoneHead_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, pThis, iReg, val);
    Log2(("HcDoneHead_w(0x%#08x) - denied!!!\n", val));
    /*AssertMsgFailed(("Illegal operation!!! val=%#010x\n", val)); - OS/2 does this */
    return VINF_SUCCESS;
}


/**
 * Read the HcFmInterval (Fm=Frame) register.
 */
static VBOXSTRICTRC HcFmInterval_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    uint32_t val = (pThis->fit << 31) | (pThis->fsmps << 16) | (pThis->fi);
    Log2(("HcFmInterval_r() -> 0x%#08x - FI=%d FSMPS=%d FIT=%d\n",
          val, val & 0x3fff, (val >> 16) & 0x7fff, val >> 31));
    *pu32Value = val;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write to the HcFmInterval (Fm = Frame) register.
 */
static VBOXSTRICTRC HcFmInterval_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);

    /* log */
    uint32_t chg = val ^ ((pThis->fit << 31) | (pThis->fsmps << 16) | pThis->fi); NOREF(chg);
    Log2(("HcFmInterval_w(%#010x) => %sFI=%d %sFSMPS=%d %sFIT=%d\n",
          val,
          chg & 0x00003fff ? "*" : "",  val        & 0x3fff,
          chg & 0x7fff0000 ? "*" : "", (val >> 16) & 0x7fff,
          chg >> 31        ? "*" : "", (val >> 31) & 1));
    if (pThis->fi != (val & OHCI_FMI_FI))
    {
        Log(("ohci: FrameInterval: %#010x -> %#010x\n", pThis->fi, val & OHCI_FMI_FI));
        AssertMsg(pThis->fit != ((val >> OHCI_FMI_FIT_SHIFT) & 1), ("HCD didn't toggle the FIT bit!!!\n"));
    }

    /* update */
    pThis->fi = val & OHCI_FMI_FI;
    pThis->fit = (val & OHCI_FMI_FIT) >> OHCI_FMI_FIT_SHIFT;
    pThis->fsmps = (val & OHCI_FMI_FSMPS) >> OHCI_FMI_FSMPS_SHIFT;
    return VINF_SUCCESS;
}

/**
 * Read the HcFmRemaining (Fm = Frame) register.
 */
static VBOXSTRICTRC HcFmRemaining_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(iReg);
    uint32_t Value = pThis->frt << 31;
    if ((pThis->ctl & OHCI_CTL_HCFS) == OHCI_USB_OPERATIONAL)
    {
        /*
         * Being in USB operational state guarantees SofTime was set already.
         */
        uint64_t tks = PDMDevHlpTMTimeVirtGet(pDevIns) - pThis->SofTime;
        if (tks < pThis->cTicksPerFrame)  /* avoid muldiv if possible */
        {
            uint16_t fr;
            tks = ASMMultU64ByU32DivByU32(1, tks, pThis->cTicksPerUsbTick);
            fr = (uint16_t)(pThis->fi - tks);
            Value |= fr;
        }
    }

    Log2(("HcFmRemaining_r() -> %#010x - FR=%d FRT=%d\n", Value, Value & 0x3fff, Value >> 31));
    *pu32Value = Value;
    return VINF_SUCCESS;
}

/**
 * Write to the HcFmRemaining (Fm = Frame) register.
 */
static VBOXSTRICTRC HcFmRemaining_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, pThis, iReg, val);
    Log2(("HcFmRemaining_w(%#010x) - denied\n", val));
    AssertMsgFailed(("Invalid operation!!! val=%#010x\n", val));
    return VINF_SUCCESS;
}

/**
 * Read the HcFmNumber (Fm = Frame) register.
 */
static VBOXSTRICTRC HcFmNumber_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    uint32_t val = (uint16_t)pThis->HcFmNumber;
    Log2(("HcFmNumber_r() -> %#010x - FN=%#x(%d) (32-bit=%#x(%d))\n", val, val, val, pThis->HcFmNumber, pThis->HcFmNumber));
    *pu32Value = val;
    return VINF_SUCCESS;
}

/**
 * Write to the HcFmNumber (Fm = Frame) register.
 */
static VBOXSTRICTRC HcFmNumber_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, pThis, iReg, val);
    Log2(("HcFmNumber_w(%#010x) - denied\n", val));
    AssertMsgFailed(("Invalid operation!!! val=%#010x\n", val));
    return VINF_SUCCESS;
}

/**
 * Read the HcPeriodicStart register.
 * The register determines when in a frame to switch from control&bulk to periodic lists.
 */
static VBOXSTRICTRC HcPeriodicStart_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    Log2(("HcPeriodicStart_r() -> %#010x - PS=%d\n", pThis->pstart, pThis->pstart & 0x3fff));
    *pu32Value = pThis->pstart;
    return VINF_SUCCESS;
}

/**
 * Write to the HcPeriodicStart register.
 * The register determines when in a frame to switch from control&bulk to periodic lists.
 */
static VBOXSTRICTRC HcPeriodicStart_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);
    Log2(("HcPeriodicStart_w(%#010x) => PS=%d\n", val, val & 0x3fff));
    if (val & ~0x3fff)
        Log2(("Unknown bits %#x are set!!!\n", val & ~0x3fff));
    pThis->pstart = val; /** @todo r=bird: should we support setting the other bits? */
    return VINF_SUCCESS;
}

/**
 * Read the HcLSThreshold register.
 */
static VBOXSTRICTRC HcLSThreshold_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    Log2(("HcLSThreshold_r() -> %#010x\n", OHCI_LS_THRESH));
    *pu32Value = OHCI_LS_THRESH;
    return VINF_SUCCESS;
}

/**
 * Write to the HcLSThreshold register.
 *
 * Docs are inconsistent here:
 *
 *      "Neither the Host Controller nor the Host Controller Driver are allowed to change this value."
 *
 *      "This value is calculated by HCD with the consideration of transmission and setup overhead."
 *
 *      The register is marked "R/W" the HCD column.
 *
 */
static VBOXSTRICTRC HcLSThreshold_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, pThis, iReg, val);
    Log2(("HcLSThreshold_w(%#010x) => LST=0x%03x(%d)\n", val, val & 0x0fff, val & 0x0fff));
    AssertMsg(val == OHCI_LS_THRESH,
              ("HCD tried to write bad LS threshold: 0x%x (see function header)\n", val));
    /** @todo the HCD can change this. */
    return VINF_SUCCESS;
}

/**
 * Read the HcRhDescriptorA register.
 */
static VBOXSTRICTRC HcRhDescriptorA_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    RT_NOREF(pDevIns, iReg);
    uint32_t val = pThis->RootHub.desc_a;
#if 0 /* annoying */
    Log2(("HcRhDescriptorA_r() -> %#010x - NDP=%d PSM=%d NPS=%d DT=%d OCPM=%d NOCP=%d POTGT=%#x\n",
          val, val & 0xff, (val >> 8) & 1, (val >> 9) & 1, (val >> 10) & 1, (val >> 11) & 1,
          (val >> 12) & 1, (val >> 24) & 0xff));
#endif
    *pu32Value = val;
    return VINF_SUCCESS;
}

/**
 * Write to the HcRhDescriptorA register.
 */
static VBOXSTRICTRC HcRhDescriptorA_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);
    uint32_t chg = val ^ pThis->RootHub.desc_a; NOREF(chg);
    Log2(("HcRhDescriptorA_w(%#010x) => %sNDP=%d %sPSM=%d %sNPS=%d %sDT=%d %sOCPM=%d %sNOCP=%d %sPOTGT=%#x - %sPowerSwitching Set%sPower\n",
          val,
          chg & 0xff      ?"!!!": "", val & 0xff,
          (chg >>  8) & 1 ? "*" : "", (val >>  8) & 1,
          (chg >>  9) & 1 ? "*" : "", (val >>  9) & 1,
          (chg >> 10) & 1 ?"!!!": "", 0,
          (chg >> 11) & 1 ? "*" : "", (val >> 11) & 1,
          (chg >> 12) & 1 ? "*" : "", (val >> 12) & 1,
          (chg >> 24)&0xff? "*" : "", (val >> 24) & 0xff,
          val & OHCI_RHA_NPS ? "No"   : "",
          val & OHCI_RHA_PSM ? "Port" : "Global"));
    if (val & ~0xff001fff)
        Log2(("Unknown bits %#x are set!!!\n", val & ~0xff001fff));


    if ((val & (OHCI_RHA_NDP | OHCI_RHA_DT)) != OHCI_NDP_CFG(pThis))
    {
        Log(("ohci: invalid write to NDP or DT in roothub descriptor A!!! val=0x%.8x\n", val));
        val &= ~(OHCI_RHA_NDP | OHCI_RHA_DT);
        val |= OHCI_NDP_CFG(pThis);
    }

    pThis->RootHub.desc_a = val;
    return VINF_SUCCESS;
}

/**
 * Read the HcRhDescriptorB register.
 */
static VBOXSTRICTRC HcRhDescriptorB_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    uint32_t val = pThis->RootHub.desc_b;
    Log2(("HcRhDescriptorB_r() -> %#010x - DR=0x%04x PPCM=0x%04x\n",
          val, val & 0xffff, val >> 16));
    *pu32Value = val;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write to the HcRhDescriptorB register.
 */
static VBOXSTRICTRC HcRhDescriptorB_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
    RT_NOREF(pDevIns, iReg);
    uint32_t chg = pThis->RootHub.desc_b ^ val; NOREF(chg);
    Log2(("HcRhDescriptorB_w(%#010x) => %sDR=0x%04x %sPPCM=0x%04x\n",
          val,
          chg & 0xffff ? "!!!" : "", val & 0xffff,
          chg >> 16    ? "!!!" : "", val >> 16));

    if ( pThis->RootHub.desc_b != val )
        Log(("ohci: unsupported write to root descriptor B!!! 0x%.8x -> 0x%.8x\n", pThis->RootHub.desc_b, val));
    pThis->RootHub.desc_b = val;
    return VINF_SUCCESS;
}

/**
 * Read the HcRhStatus (Rh = Root Hub) register.
 */
static VBOXSTRICTRC HcRhStatus_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    uint32_t val = pThis->RootHub.status;
    if (val & (OHCI_RHS_LPSC | OHCI_RHS_OCIC))
        Log2(("HcRhStatus_r() -> %#010x - LPS=%d OCI=%d DRWE=%d LPSC=%d OCIC=%d CRWE=%d\n",
              val, val & 1, (val >> 1) & 1, (val >> 15) & 1, (val >> 16) & 1, (val >> 17) & 1, (val >> 31) & 1));
    *pu32Value = val;
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write to the HcRhStatus (Rh = Root Hub) register.
 */
static VBOXSTRICTRC HcRhStatus_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
#ifdef IN_RING3
    POHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCICC);

#ifdef LOG_ENABLED
    /* log */
    uint32_t old = pThis->RootHub.status;
    if (val & ~0x80038003)
        Log2(("HcRhStatus_w: Unknown bits %#x are set!!!\n", val & ~0x80038003));
    if ( (val & OHCI_RHS_LPSC) && (val & OHCI_RHS_LPS) )
        Log2(("HcRhStatus_w: Warning both CGP and SGP are set! (Clear/Set Global Power)\n"));
    if ( (val & OHCI_RHS_DRWE) && (val & OHCI_RHS_CRWE) )
        Log2(("HcRhStatus_w: Warning both CRWE and SRWE are set! (Clear/Set Remote Wakeup Enable)\n"));
#endif

    /* write 1 to clear OCIC */
    if ( val & OHCI_RHS_OCIC )
        pThis->RootHub.status &= ~OHCI_RHS_OCIC;

    /* SetGlobalPower */
    if ( val & OHCI_RHS_LPSC )
    {
        unsigned i;
        Log2(("ohci: global power up\n"));
        for (i = 0; i < OHCI_NDP_CFG(pThis); i++)
            ohciR3RhPortPower(&pThisCC->RootHub, i, true /* power up */);
    }

    /* ClearGlobalPower */
    if ( val & OHCI_RHS_LPS )
    {
        unsigned i;
        Log2(("ohci: global power down\n"));
        for (i = 0; i < OHCI_NDP_CFG(pThis); i++)
            ohciR3RhPortPower(&pThisCC->RootHub, i, false /* power down */);
    }

    if ( val & OHCI_RHS_DRWE )
        pThis->RootHub.status |= OHCI_RHS_DRWE;

    if ( val & OHCI_RHS_CRWE )
        pThis->RootHub.status &= ~OHCI_RHS_DRWE;

#ifdef LOG_ENABLED
    uint32_t chg = pThis->RootHub.status ^ old;
    Log2(("HcRhStatus_w(%#010x) => %sCGP=%d %sOCI=%d %sSRWE=%d %sSGP=%d %sOCIC=%d %sCRWE=%d\n",
          val,
           chg        & 1 ? "*" : "", val        & 1,
          (chg >>  1) & 1 ?"!!!": "", (val >>  1) & 1,
          (chg >> 15) & 1 ? "*" : "", (val >> 15) & 1,
          (chg >> 16) & 1 ? "*" : "", (val >> 16) & 1,
          (chg >> 17) & 1 ? "*" : "", (val >> 17) & 1,
          (chg >> 31) & 1 ? "*" : "", (val >> 31) & 1));
#endif
    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
#else  /* !IN_RING3 */
    RT_NOREF(pDevIns, pThis, iReg, val);
    return VINF_IOM_R3_MMIO_WRITE;
#endif /* !IN_RING3 */
}

/**
 * Read the HcRhPortStatus register of a port.
 */
static VBOXSTRICTRC HcRhPortStatus_r(PPDMDEVINS pDevIns, PCOHCI pThis, uint32_t iReg, uint32_t *pu32Value)
{
    const unsigned i = iReg - 21;
    uint32_t val = pThis->RootHub.aPorts[i].fReg | OHCI_PORT_PPS; /* PortPowerStatus: see todo on power in _w function. */
    if (val & OHCI_PORT_PRS)
    {
#ifdef IN_RING3
        RTThreadYield();
#else
        Log2(("HcRhPortStatus_r: yield -> VINF_IOM_R3_MMIO_READ\n"));
        return VINF_IOM_R3_MMIO_READ;
#endif
    }
    if (val & (OHCI_PORT_PRS | OHCI_PORT_CLEAR_CHANGE_MASK))
        Log2(("HcRhPortStatus_r(): port %u: -> %#010x - CCS=%d PES=%d PSS=%d POCI=%d RRS=%d PPS=%d LSDA=%d CSC=%d PESC=%d PSSC=%d OCIC=%d PRSC=%d\n",
              i, val, val & 1, (val >> 1) & 1, (val >> 2) & 1, (val >> 3) & 1, (val >> 4) & 1, (val >> 8) & 1, (val >> 9) & 1,
              (val >> 16) & 1, (val >> 17) & 1, (val >> 18) & 1, (val >> 19) & 1, (val >> 20) & 1));
    *pu32Value = val;
    RT_NOREF(pDevIns);
    return VINF_SUCCESS;
}

#ifdef IN_RING3
/**
 * Completion callback for the vusb_dev_reset() operation.
 * @thread EMT.
 */
static DECLCALLBACK(void) ohciR3PortResetDone(PVUSBIDEVICE pDev, uint32_t uPort, int rc, void *pvUser)
{
    RT_NOREF(pDev);

    Assert(uPort >= 1);
    PPDMDEVINS      pDevIns = (PPDMDEVINS)pvUser;
    POHCI           pThis   = PDMDEVINS_2_DATA(pDevIns, POHCI);
    POHCICC         pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCICC);
    POHCIHUBPORT    pPort   = &pThis->RootHub.aPorts[uPort - 1];

    if (RT_SUCCESS(rc))
    {
        /*
         * Successful reset.
         */
        Log2(("ohciR3PortResetDone: Reset completed.\n"));
        pPort->fReg &= ~(OHCI_PORT_PRS | OHCI_PORT_PSS | OHCI_PORT_PSSC);
        pPort->fReg |= OHCI_PORT_PES | OHCI_PORT_PRSC;
    }
    else
    {
        /* desperate measures. */
        if (    pPort->fAttached
            &&  VUSBIRhDevGetState(pThisCC->RootHub.pIRhConn, uPort) == VUSB_DEVICE_STATE_ATTACHED)
        {
            /*
             * Damn, something weird happened during reset. We'll pretend the user did an
             * incredible fast reconnect or something. (probably not gonna work)
             */
            Log2(("ohciR3PortResetDone: The reset failed (rc=%Rrc)!!! Pretending reconnect at the speed of light.\n", rc));
            pPort->fReg = OHCI_PORT_CCS | OHCI_PORT_CSC;
        }
        else
        {
            /*
             * The device have / will be disconnected.
             */
            Log2(("ohciR3PortResetDone: Disconnected (rc=%Rrc)!!!\n", rc));
            pPort->fReg &= ~(OHCI_PORT_PRS | OHCI_PORT_PSS | OHCI_PORT_PSSC | OHCI_PORT_PRSC);
            pPort->fReg |= OHCI_PORT_CSC;
        }
    }

    /* Raise roothub status change interrupt. */
    ohciR3SetInterrupt(pDevIns, pThis, OHCI_INTR_ROOT_HUB_STATUS_CHANGE);
}

/**
 * Sets a flag in a port status register but only set it if a device is
 * connected, if not set ConnectStatusChange flag to force HCD to reevaluate
 * connect status.
 *
 * @returns true if device was connected and the flag was cleared.
 */
static bool ohciR3RhPortSetIfConnected(PPDMDEVINS pDevIns, POHCI pThis, int iPort, uint32_t fValue)
{
    /*
     * Writing a 0 has no effect
     */
    if (fValue == 0)
        return false;

    /*
     * If CurrentConnectStatus is cleared we set ConnectStatusChange.
     */
    if (!(pThis->RootHub.aPorts[iPort].fReg & OHCI_PORT_CCS))
    {
        pThis->RootHub.aPorts[iPort].fReg |= OHCI_PORT_CSC;
        ohciR3SetInterrupt(pDevIns, pThis, OHCI_INTR_ROOT_HUB_STATUS_CHANGE);
        return false;
    }

    bool fRc = !(pThis->RootHub.aPorts[iPort].fReg & fValue);

    /* set the bit */
    pThis->RootHub.aPorts[iPort].fReg |= fValue;

    return fRc;
}
#endif /* IN_RING3 */

/**
 * Write to the HcRhPortStatus register of a port.
 */
static VBOXSTRICTRC HcRhPortStatus_w(PPDMDEVINS pDevIns, POHCI pThis, uint32_t iReg, uint32_t val)
{
#ifdef IN_RING3
    const unsigned  i = iReg - 21;
    POHCICC         pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCICC);
    POHCIHUBPORT    p = &pThis->RootHub.aPorts[i];
    uint32_t        old_state = p->fReg;

# ifdef LOG_ENABLED
    /*
     * Log it.
     */
    static const char *apszCmdNames[32] =
    {
        "ClearPortEnable",      "SetPortEnable",    "SetPortSuspend",   "!!!ClearSuspendStatus",
        "SetPortReset",         "!!!5",             "!!!6",             "!!!7",
        "SetPortPower",         "ClearPortPower",   "!!!10",            "!!!11",
        "!!!12",                "!!!13",            "!!!14",            "!!!15",
        "ClearCSC",             "ClearPESC",        "ClearPSSC",        "ClearOCIC",
        "ClearPRSC",            "!!!21",            "!!!22",            "!!!23",
        "!!!24",                "!!!25",            "!!!26",            "!!!27",
        "!!!28",                "!!!29",            "!!!30",            "!!!31"
    };
    Log2(("HcRhPortStatus_w(%#010x): port %u:", val, i));
    for (unsigned j = 0; j < RT_ELEMENTS(apszCmdNames); j++)
        if (val & (1 << j))
            Log2((" %s", apszCmdNames[j]));
    Log2(("\n"));
# endif

    /* Write to clear any of the change bits: CSC, PESC, PSSC, OCIC and PRSC */
    if (val & OHCI_PORT_CLEAR_CHANGE_MASK)
        p->fReg &= ~(val & OHCI_PORT_CLEAR_CHANGE_MASK);

    if (val & OHCI_PORT_CLRPE)
    {
        p->fReg &= ~OHCI_PORT_PES;
        Log2(("HcRhPortStatus_w(): port %u: DISABLE\n", i));
    }

    if (ohciR3RhPortSetIfConnected(pDevIns, pThis, i, val & OHCI_PORT_PES))
        Log2(("HcRhPortStatus_w(): port %u: ENABLE\n", i));

    if (ohciR3RhPortSetIfConnected(pDevIns, pThis, i, val & OHCI_PORT_PSS))
        Log2(("HcRhPortStatus_w(): port %u: SUSPEND - not implemented correctly!!!\n", i));

    if (val & OHCI_PORT_PRS)
    {
        if (ohciR3RhPortSetIfConnected(pDevIns, pThis, i, val & OHCI_PORT_PRS))
        {
            PVM pVM = PDMDevHlpGetVM(pDevIns);
            p->fReg &= ~OHCI_PORT_PRSC;
            VUSBIRhDevReset(pThisCC->RootHub.pIRhConn, OHCI_PORT_2_VUSB_PORT(i), false /* don't reset on linux */,
                            ohciR3PortResetDone, pDevIns, pVM);
        }
        else if (p->fReg & OHCI_PORT_PRS)
        {
            /* the guest is getting impatient. */
            Log2(("HcRhPortStatus_w(): port %u: Impatient guest!\n", i));
            RTThreadYield();
        }
    }

    if (!(pThis->RootHub.desc_a & OHCI_RHA_NPS))
    {
        /** @todo To implement per-device power-switching
         * we need to check PortPowerControlMask to make
         * sure it isn't gang powered
         */
        if (val & OHCI_PORT_CLRPP)
            ohciR3RhPortPower(&pThisCC->RootHub, i, false /* power down */);
        if (val & OHCI_PORT_PPS)
            ohciR3RhPortPower(&pThisCC->RootHub, i, true /* power up */);
    }

    /** @todo r=frank:  ClearSuspendStatus. Timing? */
    if (val & OHCI_PORT_CLRSS)
    {
        ohciR3RhPortPower(&pThisCC->RootHub, i, true /* power up */);
        pThis->RootHub.aPorts[i].fReg &= ~OHCI_PORT_PSS;
        pThis->RootHub.aPorts[i].fReg |= OHCI_PORT_PSSC;
        ohciR3SetInterrupt(pDevIns, pThis, OHCI_INTR_ROOT_HUB_STATUS_CHANGE);
    }

    if (p->fReg != old_state)
    {
        uint32_t res = p->fReg;
        uint32_t chg = res ^ old_state; NOREF(chg);
        Log2(("HcRhPortStatus_w(%#010x): port %u: => %sCCS=%d %sPES=%d %sPSS=%d %sPOCI=%d %sRRS=%d %sPPS=%d %sLSDA=%d %sCSC=%d %sPESC=%d %sPSSC=%d %sOCIC=%d %sPRSC=%d\n",
              val, i,
              chg         & 1 ? "*" : "",  res        & 1,
              (chg >>  1) & 1 ? "*" : "", (res >>  1) & 1,
              (chg >>  2) & 1 ? "*" : "", (res >>  2) & 1,
              (chg >>  3) & 1 ? "*" : "", (res >>  3) & 1,
              (chg >>  4) & 1 ? "*" : "", (res >>  4) & 1,
              (chg >>  8) & 1 ? "*" : "", (res >>  8) & 1,
              (chg >>  9) & 1 ? "*" : "", (res >>  9) & 1,
              (chg >> 16) & 1 ? "*" : "", (res >> 16) & 1,
              (chg >> 17) & 1 ? "*" : "", (res >> 17) & 1,
              (chg >> 18) & 1 ? "*" : "", (res >> 18) & 1,
              (chg >> 19) & 1 ? "*" : "", (res >> 19) & 1,
              (chg >> 20) & 1 ? "*" : "", (res >> 20) & 1));
    }
    RT_NOREF(pDevIns);
    return VINF_SUCCESS;
#else /* !IN_RING3 */
    RT_NOREF(pDevIns, pThis, iReg, val);
    return VINF_IOM_R3_MMIO_WRITE;
#endif /* !IN_RING3 */
}

/**
 * Register descriptor table
 */
static const OHCIOPREG g_aOpRegs[] =
{
    { "HcRevision",          HcRevision_r,           HcRevision_w },            /*  0 */
    { "HcControl",           HcControl_r,            HcControl_w },             /*  1 */
    { "HcCommandStatus",     HcCommandStatus_r,      HcCommandStatus_w },       /*  2 */
    { "HcInterruptStatus",   HcInterruptStatus_r,    HcInterruptStatus_w },     /*  3 */
    { "HcInterruptEnable",   HcInterruptEnable_r,    HcInterruptEnable_w },     /*  4 */
    { "HcInterruptDisable",  HcInterruptDisable_r,   HcInterruptDisable_w },    /*  5 */
    { "HcHCCA",              HcHCCA_r,               HcHCCA_w },                /*  6 */
    { "HcPeriodCurrentED",   HcPeriodCurrentED_r,    HcPeriodCurrentED_w },     /*  7 */
    { "HcControlHeadED",     HcControlHeadED_r,      HcControlHeadED_w },       /*  8 */
    { "HcControlCurrentED",  HcControlCurrentED_r,   HcControlCurrentED_w },    /*  9 */
    { "HcBulkHeadED",        HcBulkHeadED_r,         HcBulkHeadED_w },          /* 10 */
    { "HcBulkCurrentED",     HcBulkCurrentED_r,      HcBulkCurrentED_w },       /* 11 */
    { "HcDoneHead",          HcDoneHead_r,           HcDoneHead_w },            /* 12 */
    { "HcFmInterval",        HcFmInterval_r,         HcFmInterval_w },          /* 13 */
    { "HcFmRemaining",       HcFmRemaining_r,        HcFmRemaining_w },         /* 14 */
    { "HcFmNumber",          HcFmNumber_r,           HcFmNumber_w },            /* 15 */
    { "HcPeriodicStart",     HcPeriodicStart_r,      HcPeriodicStart_w },       /* 16 */
    { "HcLSThreshold",       HcLSThreshold_r,        HcLSThreshold_w },         /* 17 */
    { "HcRhDescriptorA",     HcRhDescriptorA_r,      HcRhDescriptorA_w },       /* 18 */
    { "HcRhDescriptorB",     HcRhDescriptorB_r,      HcRhDescriptorB_w },       /* 19 */
    { "HcRhStatus",          HcRhStatus_r,           HcRhStatus_w },            /* 20 */

    /* The number of port status register depends on the definition
     * of OHCI_NDP_MAX macro
     */
    { "HcRhPortStatus[0]",   HcRhPortStatus_r,       HcRhPortStatus_w },        /* 21 */
    { "HcRhPortStatus[1]",   HcRhPortStatus_r,       HcRhPortStatus_w },        /* 22 */
    { "HcRhPortStatus[2]",   HcRhPortStatus_r,       HcRhPortStatus_w },        /* 23 */
    { "HcRhPortStatus[3]",   HcRhPortStatus_r,       HcRhPortStatus_w },        /* 24 */
    { "HcRhPortStatus[4]",   HcRhPortStatus_r,       HcRhPortStatus_w },        /* 25 */
    { "HcRhPortStatus[5]",   HcRhPortStatus_r,       HcRhPortStatus_w },        /* 26 */
    { "HcRhPortStatus[6]",   HcRhPortStatus_r,       HcRhPortStatus_w },        /* 27 */
    { "HcRhPortStatus[7]",   HcRhPortStatus_r,       HcRhPortStatus_w },        /* 28 */
    { "HcRhPortStatus[8]",   HcRhPortStatus_r,       HcRhPortStatus_w },        /* 29 */
    { "HcRhPortStatus[9]",   HcRhPortStatus_r,       HcRhPortStatus_w },        /* 30 */
    { "HcRhPortStatus[10]",  HcRhPortStatus_r,       HcRhPortStatus_w },        /* 31 */
    { "HcRhPortStatus[11]",  HcRhPortStatus_r,       HcRhPortStatus_w },        /* 32 */
    { "HcRhPortStatus[12]",  HcRhPortStatus_r,       HcRhPortStatus_w },        /* 33 */
    { "HcRhPortStatus[13]",  HcRhPortStatus_r,       HcRhPortStatus_w },        /* 34 */
    { "HcRhPortStatus[14]",  HcRhPortStatus_r,       HcRhPortStatus_w },        /* 35 */
};

/* Quick way to determine how many op regs are valid. Since at least one port must
 * be configured (and no more than 15), there will be between 22 and 36 registers.
 */
#define NUM_OP_REGS(pohci)  (21 + OHCI_NDP_CFG(pohci))

AssertCompile(RT_ELEMENTS(g_aOpRegs) > 21);
AssertCompile(RT_ELEMENTS(g_aOpRegs) <= 36);

/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) ohciMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    POHCI pThis = PDMDEVINS_2_DATA(pDevIns, POHCI);
    RT_NOREF(pvUser);

    /* Paranoia: Assert that IOMMMIO_FLAGS_READ_DWORD works. */
    AssertReturn(cb == sizeof(uint32_t), VERR_INTERNAL_ERROR_3);
    AssertReturn(!(off & 0x3), VERR_INTERNAL_ERROR_4);

    /*
     * Validate the register and call the read operator.
     */
    VBOXSTRICTRC   rc;
    const uint32_t iReg = off >> 2;
    if (iReg < NUM_OP_REGS(pThis))
        rc = g_aOpRegs[iReg].pfnRead(pDevIns, pThis, iReg, (uint32_t *)pv);
    else
    {
        Log(("ohci: Trying to read register %u/%u!!!\n", iReg, NUM_OP_REGS(pThis)));
        rc = VINF_IOM_MMIO_UNUSED_FF;
    }
    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) ohciMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    POHCI pThis = PDMDEVINS_2_DATA(pDevIns, POHCI);
    RT_NOREF(pvUser);

    /* Paranoia: Assert that IOMMMIO_FLAGS_WRITE_DWORD_ZEROED works. */
    AssertReturn(cb == sizeof(uint32_t), VERR_INTERNAL_ERROR_3);
    AssertReturn(!(off & 0x3), VERR_INTERNAL_ERROR_4);

    /*
     * Validate the register and call the read operator.
     */
    VBOXSTRICTRC   rc;
    const uint32_t iReg = off >> 2;
    if (iReg < NUM_OP_REGS(pThis))
        rc = g_aOpRegs[iReg].pfnWrite(pDevIns, pThis, iReg, *(uint32_t const *)pv);
    else
    {
        Log(("ohci: Trying to write to register %u/%u!!!\n", iReg, NUM_OP_REGS(pThis)));
        rc = VINF_SUCCESS;
    }
    return rc;
}

#ifdef IN_RING3

/**
 * Saves the state of the OHCI device.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pSSM        The handle to save the state to.
 */
static DECLCALLBACK(int) ohciR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    POHCI   pThis   = PDMDEVINS_2_DATA(pDevIns, POHCI);
    POHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCICC);
    LogFlow(("ohciR3SaveExec:\n"));

    int rc = pDevIns->pHlpR3->pfnSSMPutStructEx(pSSM, pThis, sizeof(*pThis), 0 /*fFlags*/, &g_aOhciFields[0], NULL);
    AssertRCReturn(rc, rc);

    /* Save the periodic frame rate so we can we can tell if the bus was started or not when restoring. */
    return pDevIns->pHlpR3->pfnSSMPutU32(pSSM, VUSBIRhGetPeriodicFrameRate(pThisCC->RootHub.pIRhConn));
}


/**
 * Loads the state of the OHCI device.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pSSM        The handle to the saved state.
 * @param   uVersion    The data unit version number.
 * @param   uPass       The data pass.
 */
static DECLCALLBACK(int) ohciR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    POHCI           pThis   = PDMDEVINS_2_DATA(pDevIns, POHCI);
    POHCICC         pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCICC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;
    LogFlow(("ohciR3LoadExec:\n"));

    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    if (uVersion >= OHCI_SAVED_STATE_VERSION_EOF_TIMER)
        rc = pHlp->pfnSSMGetStructEx(pSSM, pThis, sizeof(*pThis), 0 /*fFlags*/, &g_aOhciFields[0], NULL);
    else if (uVersion == OHCI_SAVED_STATE_VERSION_8PORTS)
        rc = pHlp->pfnSSMGetStructEx(pSSM, pThis, sizeof(*pThis), 0 /*fFlags*/, &g_aOhciFields8Ports[0], NULL);
    else
        AssertMsgFailedReturn(("%d\n", uVersion), VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);
    AssertRCReturn(rc, rc);

    /*
     * Get the frame rate / started indicator.
     *
     * For older versions there is a timer saved here.  We'll skip it and deduce
     * the periodic frame rate from the host controller functional state.
     */
    if (uVersion > OHCI_SAVED_STATE_VERSION_EOF_TIMER)
    {
        rc = pHlp->pfnSSMGetU32(pSSM, &pThisCC->uRestoredPeriodicFrameRate);
        AssertRCReturn(rc, rc);
    }
    else
    {
        rc = pHlp->pfnSSMSkipToEndOfUnit(pSSM);
        AssertRCReturn(rc, rc);

        uint32_t fHcfs = pThis->ctl & OHCI_CTL_HCFS;
        switch (fHcfs)
        {
            case OHCI_USB_OPERATIONAL:
            case OHCI_USB_RESUME:
                pThisCC->uRestoredPeriodicFrameRate = OHCI_DEFAULT_TIMER_FREQ;
                break;
            default:
                pThisCC->uRestoredPeriodicFrameRate = 0;
                break;
        }
    }

    /** @todo could we restore the frame rate here instead of in ohciR3Resume? */
    return VINF_SUCCESS;
}


/**
 * Reset notification.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(void) ohciR3Reset(PPDMDEVINS pDevIns)
{
    POHCI   pThis   = PDMDEVINS_2_DATA(pDevIns, POHCI);
    POHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCICC);
    LogFlow(("ohciR3Reset:\n"));

    /*
     * There is no distinction between cold boot, warm reboot and software reboots,
     * all of these are treated as cold boots. We are also doing the initialization
     * job of a BIOS or SMM driver.
     *
     * Important: Don't confuse UsbReset with hardware reset. Hardware reset is
     *            just one way of getting into the UsbReset state.
     */
    ohciR3DoReset(pDevIns, pThis, pThisCC, OHCI_USB_RESET, true /* reset devices */);
}


/**
 * Resume notification.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(void) ohciR3Resume(PPDMDEVINS pDevIns)
{
    POHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCICC);
    LogFlowFunc(("\n"));

    /* Restart the frame thread if it was active when the loaded state was saved. */
    uint32_t uRestoredPeriodicFR = pThisCC->uRestoredPeriodicFrameRate;
    pThisCC->uRestoredPeriodicFrameRate = 0;
    if (uRestoredPeriodicFR)
    {
        LogFlowFunc(("Bus was active, enable periodic frame processing (rate: %u)\n", uRestoredPeriodicFR));
        int rc = pThisCC->RootHub.pIRhConn->pfnSetPeriodicFrameProcessing(pThisCC->RootHub.pIRhConn, uRestoredPeriodicFR);
        AssertRC(rc);
    }
}


/**
 * Info handler, device version. Dumps OHCI control registers.
 *
 * @param   pDevIns     Device instance which registered the info.
 * @param   pHlp        Callback functions for doing output.
 * @param   pszArgs     Argument string. Optional and specific to the handler.
 */
static DECLCALLBACK(void) ohciR3InfoRegs(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    POHCI pThis = PDMDEVINS_2_DATA(pDevIns, POHCI);
    uint32_t val, ctl, status;

    /* Control register */
    ctl = pThis->ctl;
    pHlp->pfnPrintf(pHlp, "HcControl:          %08x - CBSR=%d PLE=%d IE=%d CLE=%d BLE=%d HCFS=%#x IR=%d RWC=%d RWE=%d\n",
          ctl, ctl & 3, (ctl >> 2) & 1, (ctl >> 3) & 1, (ctl >> 4) & 1, (ctl >> 5) & 1, (ctl >> 6) & 3, (ctl >> 8) & 1,
          (ctl >> 9) & 1, (ctl >> 10) & 1);

    /* Command status register */
    status = pThis->status;
    pHlp->pfnPrintf(pHlp, "HcCommandStatus:    %08x - HCR=%d CLF=%d BLF=%d OCR=%d SOC=%d\n",
          status, status & 1, (status >> 1) & 1, (status >> 2) & 1, (status >> 3) & 1, (status >> 16) & 3);

    /* Interrupt status register */
    val = pThis->intr_status;
    pHlp->pfnPrintf(pHlp, "HcInterruptStatus:  %08x - SO=%d WDH=%d SF=%d RD=%d UE=%d FNO=%d RHSC=%d OC=%d\n",
          val, val & 1, (val >> 1) & 1, (val >> 2) & 1, (val >> 3) & 1, (val >> 4) & 1, (val >> 5) & 1,
          (val >> 6) & 1, (val >> 30) & 1);

    /* Interrupt enable register */
    val = pThis->intr;
    pHlp->pfnPrintf(pHlp, "HcInterruptEnable:  %08x - SO=%d WDH=%d SF=%d RD=%d UE=%d FNO=%d RHSC=%d OC=%d MIE=%d\n",
          val, val & 1, (val >> 1) & 1, (val >> 2) & 1, (val >> 3) & 1, (val >> 4) & 1, (val >> 5) & 1,
          (val >> 6) & 1, (val >> 30) & 1, (val >> 31) & 1);

    /* HCCA address register */
    pHlp->pfnPrintf(pHlp, "HcHCCA:             %08x\n", pThis->hcca);

    /* Current periodic ED register */
    pHlp->pfnPrintf(pHlp, "HcPeriodCurrentED:  %08x\n", pThis->per_cur);

    /* Control ED registers */
    pHlp->pfnPrintf(pHlp, "HcControlHeadED:    %08x\n", pThis->ctrl_head);
    pHlp->pfnPrintf(pHlp, "HcControlCurrentED: %08x\n", pThis->ctrl_cur);

    /* Bulk ED registers */
    pHlp->pfnPrintf(pHlp, "HcBulkHeadED:       %08x\n", pThis->bulk_head);
    pHlp->pfnPrintf(pHlp, "HcBulkCurrentED:    %08x\n", pThis->bulk_cur);

    /* Done head register */
    pHlp->pfnPrintf(pHlp, "HcDoneHead:         %08x\n", pThis->done);

    /* Done head register */
    pHlp->pfnPrintf(pHlp, "HcDoneHead:         %08x\n", pThis->done);

    /* Root hub descriptor A */
    val = pThis->RootHub.desc_a;
    pHlp->pfnPrintf(pHlp, "HcRhDescriptorA:    %08x - NDP=%d PSM=%d NPS=%d DT=%d OCPM=%d NOCP=%d POTPGT=%d\n",
          val, (uint8_t)val, (val >> 8) & 1, (val >> 9) & 1, (val >> 10) & 1, (val >> 11) & 1, (val >> 12) & 1, (uint8_t)(val >> 24));

    /* Root hub descriptor B */
    val = pThis->RootHub.desc_b;
    pHlp->pfnPrintf(pHlp, "HcRhDescriptorB:    %08x - DR=%#04x PPCM=%#04x\n", val, (uint16_t)val, (uint16_t)(val >> 16));

    /* Root hub status register */
    val = pThis->RootHub.status;
    pHlp->pfnPrintf(pHlp, "HcRhStatus:         %08x - LPS=%d OCI=%d DRWE=%d  LPSC=%d OCIC=%d CRWE=%d\n\n",
          val, val & 1, (val >> 1) & 1, (val >> 15) & 1, (val >> 16) & 1, (val >> 17) & 1, (val >> 31) & 1);

    /* Port status registers */
    for (unsigned i = 0; i < OHCI_NDP_CFG(pThis); ++i)
    {
        val = pThis->RootHub.aPorts[i].fReg;
        pHlp->pfnPrintf(pHlp, "HcRhPortStatus%02d: CCS=%d PES =%d PSS =%d POCI=%d PRS =%d  PPS=%d LSDA=%d\n"
                              "      %08x -  CSC=%d PESC=%d PSSC=%d OCIC=%d PRSC=%d\n",
              i, val & 1, (val >> 1) & 1, (val >> 2) & 1,(val >> 3) & 1, (val >> 4) & 1, (val >> 8) & 1, (val >> 9) & 1,
              val, (val >> 16) & 1, (val >> 17) & 1, (val >> 18) & 1, (val >> 19) & 1, (val >> 20) & 1);
    }
}


/**
 * Destruct a device instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(int) ohciR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    POHCI   pThis   = PDMDEVINS_2_DATA(pDevIns, POHCI);
    POHCICC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCICC);

    if (RTCritSectIsInitialized(&pThisCC->CritSect))
        RTCritSectDelete(&pThisCC->CritSect);
    PDMDevHlpCritSectDelete(pDevIns, &pThis->CsIrq);

    /*
     * Tear down the per endpoint in-flight tracking...
     */

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct,OHCI constructor}
 */
static DECLCALLBACK(int) ohciR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    POHCI   pThis   = PDMDEVINS_2_DATA(pDevIns, POHCI);
    POHCIR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, POHCIR3);

    /*
     * Init instance data.
     */
    pThisCC->pDevInsR3 = pDevIns;

    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    PDMPciDevSetVendorId(pPciDev,       0x106b);
    PDMPciDevSetDeviceId(pPciDev,       0x003f);
    PDMPciDevSetClassProg(pPciDev,      0x10); /* OHCI */
    PDMPciDevSetClassSub(pPciDev,       0x03);
    PDMPciDevSetClassBase(pPciDev,      0x0c);
    PDMPciDevSetInterruptPin(pPciDev,   0x01);
#ifdef VBOX_WITH_MSI_DEVICES
    PDMPciDevSetStatus(pPciDev,         VBOX_PCI_STATUS_CAP_LIST);
    PDMPciDevSetCapabilityList(pPciDev, 0x80);
#endif

    pThisCC->RootHub.pOhci                         = pThis;
    pThisCC->RootHub.IBase.pfnQueryInterface       = ohciR3RhQueryInterface;
    pThisCC->RootHub.IRhPort.pfnGetAvailablePorts  = ohciR3RhGetAvailablePorts;
    pThisCC->RootHub.IRhPort.pfnGetUSBVersions     = ohciR3RhGetUSBVersions;
    pThisCC->RootHub.IRhPort.pfnAttach             = ohciR3RhAttach;
    pThisCC->RootHub.IRhPort.pfnDetach             = ohciR3RhDetach;
    pThisCC->RootHub.IRhPort.pfnReset              = ohciR3RhReset;
    pThisCC->RootHub.IRhPort.pfnXferCompletion     = ohciR3RhXferCompletion;
    pThisCC->RootHub.IRhPort.pfnXferError          = ohciR3RhXferError;
    pThisCC->RootHub.IRhPort.pfnStartFrame         = ohciR3StartFrame;
    pThisCC->RootHub.IRhPort.pfnFrameRateChanged   = ohciR3FrameRateChanged;

    /* USB LED */
    pThisCC->RootHub.Led.u32Magic                  = PDMLED_MAGIC;
    pThisCC->RootHub.ILeds.pfnQueryStatusLed       = ohciR3RhQueryStatusLed;


    /*
     * Read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "Ports", "");

    /* Number of ports option. */
    uint32_t cPorts;
    int rc = pDevIns->pHlpR3->pfnCFGMQueryU32Def(pCfg, "Ports", &cPorts, OHCI_NDP_DEFAULT);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("OHCI configuration error: failed to read Ports as integer"));
    if (cPorts == 0 || cPorts > OHCI_NDP_MAX)
        return PDMDevHlpVMSetError(pDevIns, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                   N_("OHCI configuration error: Ports must be in range [%u,%u]"),
                                   1, OHCI_NDP_MAX);

    /* Store the configured NDP; it will be used everywhere else from now on. */
    pThis->RootHub.desc_a = cPorts;

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

    rc = PDMDevHlpPCIIORegionCreateMmio(pDevIns, 0, 4096, PCI_ADDRESS_SPACE_MEM, ohciMmioWrite, ohciMmioRead, NULL /*pvUser*/,
                                        IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED
                                        | IOMMMIO_FLAGS_DBGSTOP_ON_COMPLICATED_WRITE, "USB OHCI", &pThis->hMmio);
    AssertRCReturn(rc, rc);

    /*
     * Register the saved state data unit.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, OHCI_SAVED_STATE_VERSION, sizeof(*pThis), NULL,
                                NULL, NULL, NULL,
                                NULL, ohciR3SaveExec, NULL,
                                NULL, ohciR3LoadExec, NULL);
    AssertRCReturn(rc, rc);

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
    else if (rc != VERR_PDM_NO_ATTACHED_DRIVER)
    {
        AssertMsgFailed(("Failed to attach to status driver. rc=%Rrc\n", rc));
        return rc;
    }

    /* Set URB parameters. */
    rc = VUSBIRhSetUrbParams(pThisCC->RootHub.pIRhConn, sizeof(VUSBURBHCIINT), sizeof(VUSBURBHCITDINT));
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS, N_("OHCI: Failed to set URB parameters"));

    /*
     * Take down the virtual clock frequence for use in ohciR3FrameRateChanged().
     * (Used to be a timer, thus the name.)
     */
    pThis->u64TimerHz = PDMDevHlpTMTimeVirtGetFreq(pDevIns);

    /*
     * Critical sections: explain
     */
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CsIrq, RT_SRC_POS, "OHCI#%uIrq", iInstance);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS, N_("OHCI: Failed to create critical section"));

    rc = RTCritSectInit(&pThisCC->CritSect);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS, N_("OHCI: Failed to create critical section"));

    /*
     * Do a hardware reset.
     */
    ohciR3DoReset(pDevIns, pThis, pThisCC, OHCI_USB_RESET, false /* don't reset devices */);

# ifdef VBOX_WITH_STATISTICS
    /*
     * Register statistics.
     */
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCanceledIsocUrbs, STAMTYPE_COUNTER, "CanceledIsocUrbs", STAMUNIT_OCCURENCES, "Detected canceled isochronous URBs.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCanceledGenUrbs,  STAMTYPE_COUNTER, "CanceledGenUrbs",  STAMUNIT_OCCURENCES, "Detected canceled general URBs.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatDroppedUrbs,      STAMTYPE_COUNTER, "DroppedUrbs",      STAMUNIT_OCCURENCES, "Dropped URBs (endpoint halted, or URB canceled).");
# endif

    /*
     * Register debugger info callbacks.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "ohci", "OHCI control registers.", ohciR3InfoRegs);

# if 0/*def DEBUG_bird*/
//  g_fLogInterruptEPs = true;
    g_fLogControlEPs = true;
    g_fLogBulkEPs = true;
# endif

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) ohciRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    POHCI pThis = PDMDEVINS_2_DATA(pDevIns, POHCI);

    int rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, ohciMmioWrite, ohciMmioRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

const PDMDEVREG g_DeviceOHCI =
{
    /* .u32version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "usb-ohci",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_BUS_USB,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(OHCI),
    /* .cbInstanceCC = */           sizeof(OHCICC),
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "OHCI USB controller.\n",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           ohciR3Construct,
    /* .pfnDestruct = */            ohciR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               ohciR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              ohciR3Resume,
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
    /* .pfnConstruct = */           ohciRZConstruct,
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
    /* .pfnConstruct = */           ohciRZConstruct,
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
