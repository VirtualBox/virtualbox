/* $Id: DevHPET.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * HPET virtual device - High Precision Event Timer emulation.
 *
 * This implementation is based on the (generic) Intel IA-PC HPET specification
 * and the Intel ICH9 datasheet.
 *
 * Typical windows 1809 usage (efi, smp) is to do repated one-shots and
 * a variable rate.  The reprogramming sequence is as follows (all accesses
 * are 32-bit):
 *  -# counter register read.
 *  -# timer 0: config register read.
 *  -# timer 0: write 0x134 to config register.
 *  -# timer 0: write comparator register.
 *  -# timer 0: write 0x134 to config register.
 *  -# timer 0: read comparator register.
 *  -# counter register read.
 *
 * Typical linux will configure the timer at Hz but not necessarily enable
 * interrupts (HPET_TN_ENABLE not set).  It would be nice to emulate this
 * mode without using timers.
 *
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_HPET
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/stam.h>
#include <VBox/log.h>
#include <VBox/AssertGuest.h>
#include <iprt/asm-math.h>
#include <iprt/string.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/*
 * Current limitations:
 *   - not entirely correct time of interrupt, i.e. never
 *     schedule interrupt earlier than in 1ms
 *   - statistics not implemented
 *   - level-triggered mode not implemented
 */

/** Base address for MMIO.
 * On ICH9, it is 0xFED0x000 where 'x' is 0-3, default 0. We do not support
 * relocation as the platform firmware is responsible for configuring the
 * HPET base address and the OS isn't expected to move it.
 * WARNING: This has to match the ACPI tables! */
#define HPET_BASE                   0xfed00000

/** HPET reserves a 1K range. */
#define HPET_BAR_SIZE               0x1000

/** The number of timers for PIIX4 / PIIX3. */
#define HPET_NUM_TIMERS_PIIX        3   /* Minimal implementation. */
/** The number of timers for ICH9. */
#define HPET_NUM_TIMERS_ICH9        4

/** HPET clock period for PIIX4 / PIIX3.
 * 10000000 femtoseconds == 10ns.
 */
#define HPET_CLK_PERIOD_PIIX        UINT32_C(10000000)

/** HPET clock period for ICH9.
 * 69841279 femtoseconds == 69.84 ns (1 / 14.31818MHz).
 */
#define HPET_CLK_PERIOD_ICH9        UINT32_C(69841279)

/**
 * Femtosecods in a nanosecond
 */
#define FS_PER_NS                   UINT32_C(1000000)

/** Number of HPET ticks per second (Hz), ICH9 frequency.  */
#define HPET_TICKS_PER_SEC_ICH9     UINT32_C(14318180)
AssertCompile(HPET_TICKS_PER_SEC_ICH9 == (RT_NS_1SEC_64 * FS_PER_NS + HPET_CLK_PERIOD_ICH9 / 2) / HPET_CLK_PERIOD_ICH9);

/** Number of HPET ticks per second (Hz), made-up PIIX frequency.  */
#define HPET_TICKS_PER_SEC_PIIX     UINT32_C(100000000)
AssertCompile(HPET_TICKS_PER_SEC_PIIX == (RT_NS_1SEC_64 * FS_PER_NS + HPET_CLK_PERIOD_PIIX / 2) / HPET_CLK_PERIOD_PIIX);

/** Number of HPET ticks in 100 years (approximate), ICH9 frequency.
 * Value: 45153812448000000 (0x00A06B27'3737B800) */
#define HPET_TICKS_IN_100YR_ICH9    (HPET_TICKS_PER_SEC_ICH9 * RT_SEC_1DAY_64 * 365 * 100)
AssertCompile(HPET_TICKS_IN_100YR_ICH9 >= UINT64_C(45153812448000000));

/**  Number of HPET ticks in 100 years, made-up PIIX frequency.
 * Value: 315360000000000000 (0x0460623F'C85E0000) */
#define HPET_TICKS_IN_100YR_PIIX    (HPET_TICKS_PER_SEC_PIIX * RT_SEC_1DAY_64 * 365 * 100)
AssertCompile(HPET_TICKS_IN_100YR_PIIX >= UINT64_C(315360000000000000));

/** @name Interrupt type
 * @{ */
#define HPET_TIMER_TYPE_LEVEL       (1 << 1)
#define HPET_TIMER_TYPE_EDGE        (0 << 1)
/** @} */

/** @name Delivery mode
 * @{ */
#define HPET_TIMER_DELIVERY_APIC    0   /**< Delivery through APIC. */
#define HPET_TIMER_DELIVERY_FSB     1   /**< Delivery through FSB. */
/** @} */

#define HPET_TIMER_CAP_FSB_INT_DEL (1 << 15)
#define HPET_TIMER_CAP_PER_INT     (1 << 4)

#define HPET_CFG_ENABLE          0x001  /**< ENABLE_CNF */
#define HPET_CFG_LEGACY          0x002  /**< LEG_RT_CNF */

/** @name Register offsets in HPET space.
 * @{ */
#define HPET_ID                  0x000  /**< Device ID. */
#define HPET_PERIOD              0x004  /**< Clock period in femtoseconds. */
#define HPET_CFG                 0x010  /**< Configuration register. */
#define HPET_STATUS              0x020  /**< Status register. */
#define HPET_COUNTER             0x0f0  /**< Main HPET counter. */
/** @} */

/** @name Timer N offsets (within each timer's space).
 * @{ */
#define HPET_TN_CFG              0x000  /**< Timer N configuration. */
#define HPET_TN_CMP              0x008  /**< Timer N comparator. */
#define HPET_TN_ROUTE            0x010  /**< Timer N interrupt route. */
/** @} */

#define HPET_CFG_WRITE_MASK      0x3

#define HPET_TN_INT_TYPE                RT_BIT_64(1)
#define HPET_TN_ENABLE                  RT_BIT_64(2)
#define HPET_TN_PERIODIC                RT_BIT_64(3)
#define HPET_TN_PERIODIC_CAP            RT_BIT_64(4)
#define HPET_TN_SIZE_CAP                RT_BIT_64(5)
#define HPET_TN_SETVAL                  RT_BIT_64(6)    /**< Periodic timers only: Change COMPARATOR as well as ACCUMULATOR. */
#define HPET_TN_32BIT                   RT_BIT_64(8)
#define HPET_TN_INT_ROUTE_MASK          UINT64_C(0x3e00)
#define HPET_TN_CFG_WRITE_MASK          UINT64_C(0x3e46)
#define HPET_TN_INT_ROUTE_SHIFT         9
#define HPET_TN_INT_ROUTE_CAP_SHIFT     32

#define HPET_TN_CFG_BITS_READONLY_OR_RESERVED 0xffff80b1U

/** Extract the timer count from the capabilities. */
#define HPET_CAP_GET_TIMERS(a_u32)              ((((a_u32) >> 8) + 1) & 0x1f)
/** Revision ID. */
#define HPET_CAP_GET_REV_ID(a_u32)              ((a_u32) & 0xff)
/** Counter size. */
#define HPET_CAP_HAS_64BIT_COUNT_SIZE(a_u32)    RT_BOOL((a_u32) & RT_BIT(13))
/** Legacy Replacement Route. */
#define HPET_CAP_HAS_LEG_RT(a_u32)              RT_BOOL((a_u32) & RT_BIT(15))


/** The version of the saved state. */
#define HPET_SAVED_STATE_VERSION                3
/** The version of the saved state prior to the off-by-1 timer count fix. */
#define HPET_SAVED_STATE_VERSION_PRE_TIMER      2
/** Empty saved state */
#define HPET_SAVED_STATE_VERSION_EMPTY          1


/**
 * Acquires the HPET lock or returns.
 */
#define DEVHPET_LOCK_RETURN(a_pDevIns, a_pThis, a_rcBusy)  \
    do { \
        int const rcLock = PDMDevHlpCritSectEnter((a_pDevIns), &(a_pThis)->CritSect, (a_rcBusy)); \
        if (RT_LIKELY(rcLock == VINF_SUCCESS)) \
        { /* likely */ } \
        else \
            return rcLock; \
    } while (0)

/**
 * Releases the HPET lock.
 */
#define DEVHPET_UNLOCK(a_pDevIns, a_pThis) \
    do { PDMDevHlpCritSectLeave((a_pDevIns), &(a_pThis)->CritSect); } while (0)


/**
 * Acquires the TM lock and HPET lock, returns on failure.
 * @todo r=bird: Aren't the timers using the same critsect?!?
 */
#define DEVHPET_LOCK_BOTH_RETURN(a_pDevIns, a_pThis, a_rcBusy)  \
    do { \
        VBOXSTRICTRC rcLock = PDMDevHlpTimerLockClock2((a_pDevIns), (a_pThis)->aTimers[0].hTimer, &(a_pThis)->CritSect, (a_rcBusy)); \
        if (RT_LIKELY(rcLock == VINF_SUCCESS)) \
        { /* likely */ } \
        else \
            return rcLock; \
    } while (0)


/**
 * Releases the HPET lock and TM lock.
 */
#define DEVHPET_UNLOCK_BOTH(a_pDevIns, a_pThis) \
        PDMDevHlpTimerUnlockClock2((a_pDevIns), (a_pThis)->aTimers[0].hTimer, &(a_pThis)->CritSect)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * A HPET timer.
 *
 * @note    To avoid excessive locking, we many of the updates atomically.
 */
typedef struct HPETTIMER
{
    /** The HPET timer. */
    TMTIMERHANDLE               hTimer;

    /** Timer index. */
    uint8_t                     idxTimer;
    /** Wrap. */
    uint8_t                     u8Wrap;
    /** Explicit padding. */
    uint8_t                     abPadding[6];

    /** @name Memory-mapped, software visible timer registers.
     * @{ */
    /** Configuration/capabilities. */
    uint64_t                    u64Config;
    /** Comparator. */
    uint64_t                    u64Cmp;
    /** FSB route, not supported now. */
    uint64_t                    u64Fsb;
    /** @} */

    /** @name Hidden register state.
     * @{ */
    /** Accumulator / Last value written to comparator. */
    uint64_t                    u64Period;
    /** @} */

    STAMCOUNTER                 StatSetIrq;
    STAMCOUNTER                 StatSetTimer;
} HPETTIMER;
AssertCompileMemberAlignment(HPETTIMER, u64Config, sizeof(uint64_t));
AssertCompileSizeAlignment(HPETTIMER, 64);
/** Pointer to the shared state of an HPET timer. */
typedef HPETTIMER *PHPETTIMER;
/** Const pointer to the shared state of an HPET timer. */
typedef HPETTIMER const *PCHPETTIMER;


/**
 * The shared HPET device state.
 */
typedef struct HPET
{
    /** Timer structures. */
    HPETTIMER                   aTimers[RT_MAX(HPET_NUM_TIMERS_PIIX, HPET_NUM_TIMERS_ICH9)];

    /** Offset realtive to the virtual sync clock. */
    uint64_t                    u64HpetOffset;

    /** @name Memory-mapped, software visible registers
     * @{ */
    /** Capabilities. */
    uint32_t                    u32Capabilities;
    /** Used to be u32Period.  We only implement two period values depending on
     * fIch9, and since we usually would have to RT_MIN(u32Period,1) we could
     * just as well select between HPET_CLK_PERIOD_ICH9 and HPET_CLK_PERIOD_PIIX. */
    uint32_t                    u32Padding;
    /** Configuration. */
    uint64_t                    u64HpetConfig;
    /** Interrupt status register. */
    uint64_t                    u64Isr;
    /** Main counter. */
    uint64_t                    u64HpetCounter;
    /** @}  */

    /** Whether we emulate ICH9 HPET (different frequency & timer count). */
    bool                        fIch9;
    /** Size alignment padding. */
    uint8_t                     abPadding0[7+8];

    /** The handle of the MMIO region. */
    IOMMMIOHANDLE               hMmio;

    /** Global device lock. */
    PDMCRITSECT                 CritSect;

    STAMCOUNTER                 StatCounterRead4Byte;
    STAMCOUNTER                 StatCounterRead8Byte;
    STAMCOUNTER                 StatCounterWriteLow;
    STAMCOUNTER                 StatCounterWriteHigh;
    STAMCOUNTER                 StatZeroDeltaHack;
} HPET;
AssertCompileMemberAlignment(HPET, aTimers, 64);
AssertCompileMemberAlignment(HPET, CritSect, 64);
/** Pointer to the shared HPET device state. */
typedef HPET *PHPET;
/** Const pointer to the shared HPET device state. */
typedef const HPET *PCHPET;


/**
 * The ring-3 specific HPET device state.
 */
typedef struct HPETR3
{
    /** The HPET helpers. */
    PCPDMHPETHLPR3              pHpetHlp;
} HPETR3;
/** Pointer to the ring-3 specific HPET device state. */
typedef HPETR3 *PHPETR3;


/**
 * The ring-0 specific HPET device state.
 */
typedef struct HPETR0
{
    /** The HPET helpers. */
    PCPDMHPETHLPR0              pHpetHlp;
} HPETR0;
/** Pointer to the ring-0 specific HPET device state. */
typedef HPETR0 *PHPETR0;


/**
 * The raw-mode specific HPET device state.
 */
typedef struct HPETRC
{
    /** The HPET helpers. */
    PCPDMHPETHLPRC              pHpetHlp;
} HPETRC;
/** Pointer to the raw-mode specific HPET device state. */
typedef HPETRC *PHPETRC;


/** The HPET device state specific to the current context. */
typedef CTX_SUFF(HPET) HPETCC;
/** Pointer to the HPET device state specific to the current context. */
typedef CTX_SUFF(PHPET) PHPETCC;


#ifndef VBOX_DEVICE_STRUCT_TESTCASE

DECLINLINE(bool) hpet32bitTimerEx(uint64_t fConfig)
{
    return !(fConfig & HPET_TN_SIZE_CAP)
        ||  (fConfig & HPET_TN_32BIT);
}


DECLINLINE(bool) hpet32bitTimer(PHPETTIMER pHpetTimer)
{
    return hpet32bitTimerEx(ASMAtomicUoReadU64(&pHpetTimer->u64Config));
}


DECLINLINE(uint64_t) hpetInvalidValue(PHPETTIMER pHpetTimer)
{
    return hpet32bitTimer(pHpetTimer) ? UINT32_MAX : UINT64_MAX;
}


/**
 * @note The caller shall do overflow checks! See @bugref{10301}.
 */
DECLINLINE(uint64_t) hpetTicksToNs(PHPET pThis, uint64_t value)
{
    return ASMMultU64ByU32DivByU32(value, pThis->fIch9 ? HPET_CLK_PERIOD_ICH9 : HPET_CLK_PERIOD_PIIX, FS_PER_NS);
}

DECLINLINE(uint64_t) nsToHpetTicks(PCHPET pThis, uint64_t u64Value)
{
    return ASMMultU64ByU32DivByU32(u64Value, FS_PER_NS, pThis->fIch9 ? HPET_CLK_PERIOD_ICH9 : HPET_CLK_PERIOD_PIIX);
}

DECLINLINE(uint64_t) hpetGetTicksEx(PCHPET pThis, uint64_t tsNow)
{
    return nsToHpetTicks(pThis, tsNow + pThis->u64HpetOffset);
}

DECLINLINE(uint64_t) hpetUpdateMasked(uint64_t u64NewValue, uint64_t u64OldValue, uint64_t u64Mask)
{
    u64NewValue &= u64Mask;
    u64NewValue |= (u64OldValue & ~u64Mask);
    return u64NewValue;
}

DECLINLINE(bool) hpetBitJustSet(uint64_t u64OldValue, uint64_t u64NewValue, uint64_t u64Mask)
{
    return !(u64OldValue & u64Mask)
        && !!(u64NewValue & u64Mask);
}

DECLINLINE(bool) hpetBitJustCleared(uint64_t u64OldValue, uint64_t u64NewValue, uint64_t u64Mask)
{
    return !!(u64OldValue & u64Mask)
        && !(u64NewValue & u64Mask);
}

DECLINLINE(uint64_t) hpetComputeDiff(uint64_t fConfig, uint64_t uCmp, uint64_t uHpetNow)
{
    if (hpet32bitTimerEx(fConfig))
    {
        uint32_t u32Diff = (uint32_t)uCmp - (uint32_t)uHpetNow;
        if ((int32_t)u32Diff > 0)
            return u32Diff;
    }
    else
    {
        uint64_t u64Diff = uCmp - uHpetNow;
        if ((int64_t)u64Diff > 0)
            return u64Diff;
    }
    return 0;
}


DECLINLINE(uint64_t) hpetAdjustComparator(PHPETTIMER pHpetTimer, uint64_t fConfig, uint64_t uCmp,
                                          uint64_t uPeriod, uint64_t uHpetNow)
{
    if (fConfig & HPET_TN_PERIODIC)
    {
        if (uPeriod)
        {
            uint64_t cPeriods = (uHpetNow - uCmp) / uPeriod;
            uCmp += (cPeriods + 1) * uPeriod;
            ASMAtomicWriteU64(&pHpetTimer->u64Cmp, uCmp);
        }
    }
    return uCmp;
}


/**
 * Sets the frequency hint if it's a periodic timer.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared HPET state.
 * @param   pHpetTimer  The timer.
 * @param   fConfig     Already read config value.
 * @param   uPeriod     Already read period value.
 */
DECLINLINE(void) hpetTimerSetFrequencyHint(PPDMDEVINS pDevIns, PHPET pThis, PHPETTIMER pHpetTimer,
                                           uint64_t fConfig, uint64_t uPeriod)
{
    if (   (fConfig & HPET_TN_PERIODIC)
        && uPeriod > 0
        && uPeriod < (pThis->fIch9 ? HPET_TICKS_PER_SEC_ICH9 : HPET_TICKS_PER_SEC_PIIX) / 10 /* 100 ns */)
    {
        uint64_t const nsPeriod = hpetTicksToNs(pThis, uPeriod);
        PDMDevHlpTimerSetFrequencyHint(pDevIns, pHpetTimer->hTimer, RT_NS_1SEC / (uint32_t)nsPeriod);
    }
}


/**
 * Programs an HPET timer, arming hTimer for the next IRQ.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The HPET instance data.
 * @param   pHpetTimer  The HPET timer to program.  The wrap-around indicator is
 *                      updates, and for periodic timer the comparator.
 * @param   tsNow       The current virtual sync clock time.
 * @note    Caller must both the virtual sync (timer) and HPET locks.
 */
static void hpetProgramTimer(PPDMDEVINS pDevIns, PHPET pThis, PHPETTIMER pHpetTimer, uint64_t const tsNow)
{
    /*
     * Calculate the number of HPET ticks to the next timer IRQ, but
     * first updating comparator if periodic timer.
     */
    uint64_t const fConfig   = pHpetTimer->u64Config;
    uint64_t const uPeriod   = pHpetTimer->u64Period;
    uint64_t       uCmp      = pHpetTimer->u64Cmp;
    uint64_t const uHpetNow  = hpetGetTicksEx(pThis, tsNow);
    uCmp = hpetAdjustComparator(pHpetTimer, fConfig, uCmp, uPeriod, uHpetNow);
    uint64_t       uHpetDelta = hpetComputeDiff(fConfig, uCmp, uHpetNow);

    /*
     * HPET spec says in one-shot 32-bit mode, generate an interrupt when
     * counter wraps in addition to an interrupt with comparator match.
     */
    bool fWrap = false;
    if (    hpet32bitTimerEx(fConfig)
        && !(fConfig & HPET_TN_PERIODIC))
    {
        uint32_t cHpetTicksTillWrap = UINT32_MAX - (uint32_t)uHpetNow + 1;
        if (cHpetTicksTillWrap < (uint32_t)uHpetDelta)
        {
            Log(("HPET[%u]: wrap: till=%u ticks=%lld diff64=%lld\n",
                 pHpetTimer->idxTimer, cHpetTicksTillWrap, uHpetNow, uHpetDelta));
            uHpetDelta = cHpetTicksTillWrap;
            fWrap      = true;
        }
    }
    pHpetTimer->u8Wrap = fWrap;

    /*
     * HACK ALERT! Avoid killing VM with interrupts.
     */
#if 1 /** @todo HACK, rethink, may have negative impact on the guest */
    if (uHpetDelta != 0)
    { /* likely? */ }
    else
    {
        Log(("HPET[%u]: Applying zero delta hack!\n", pHpetTimer->idxTimer));
        STAM_REL_COUNTER_INC(&pThis->StatZeroDeltaHack);
/** @todo lower this.   */
        uHpetDelta = pThis->fIch9 ? 14318 : 100000; /* 1 millisecond */
    }
#endif

    /*
     * Arm the timer.
     */
    uint64_t u64TickLimit = pThis->fIch9 ? HPET_TICKS_IN_100YR_ICH9 : HPET_TICKS_IN_100YR_PIIX;
    if (uHpetDelta <= u64TickLimit)
    {
        uint64_t const cTicksDelta = hpetTicksToNs(pThis, uHpetDelta);
        uint64_t const tsDeadline  = tsNow + cTicksDelta;
        Log4(("HPET[%u]: next IRQ in %lld hpet ticks (TM %lld ticks, at %llu)\n",
              pHpetTimer->idxTimer, uHpetDelta, cTicksDelta, tsDeadline));
        PDMDevHlpTimerSet(pDevIns, pHpetTimer->hTimer, tsDeadline);
        hpetTimerSetFrequencyHint(pDevIns, pThis, pHpetTimer, fConfig, uPeriod);
        STAM_REL_COUNTER_INC(&pHpetTimer->StatSetTimer);
    }
    else
        LogRelMax(10, ("HPET[%u]: Not scheduling an interrupt more than 100 years in the future.\n", pHpetTimer->idxTimer));
}


/* -=-=-=-=-=- Timer register accesses -=-=-=-=-=- */


/**
 * Reads a HPET timer register.
 *
 * @returns The register value.
 * @param   pThis               The HPET instance.
 * @param   iTimerNo            The timer index.
 * @param   iTimerReg           The index of the timer register to read.
 *
 * @note    No locking required.
 */
static uint32_t hpetTimerRegRead32(PHPET pThis, uint32_t iTimerNo, uint32_t iTimerReg)
{
    uint32_t u32Value;
    if (   iTimerNo < HPET_CAP_GET_TIMERS(pThis->u32Capabilities)
        && iTimerNo < RT_ELEMENTS(pThis->aTimers) )
    {
        PHPETTIMER pHpetTimer = &pThis->aTimers[iTimerNo];
        switch (iTimerReg)
        {
            case HPET_TN_CFG:
                u32Value = (uint32_t)ASMAtomicReadU64(&pHpetTimer->u64Config);
                Log(("HPET[%u]: read32 HPET_TN_CFG: %#x\n", iTimerNo, u32Value));
                break;

            case HPET_TN_CFG + 4:
                u32Value = (uint32_t)(ASMAtomicReadU64(&pHpetTimer->u64Config) >> 32);
                Log(("HPET[%u]: read32 HPET_TN_CFG+4: %#x\n", iTimerNo, u32Value));
                break;

            case HPET_TN_CMP:
            {
                uint64_t uCmp = ASMAtomicReadU64(&pHpetTimer->u64Cmp);
                u32Value = (uint32_t)uCmp;
                Log(("HPET[%u]: read32 HPET_TN_CMP: %#x (%#RX64)\n", pHpetTimer->idxTimer, u32Value, uCmp));
                break;
            }

            case HPET_TN_CMP + 4:
            {
                uint64_t uCmp = ASMAtomicReadU64(&pHpetTimer->u64Cmp);
                u32Value = (uint32_t)(uCmp >> 32);
                Log(("HPET[%u]: read32 HPET_TN_CMP+4: %#x (%#RX64)\n", pHpetTimer->idxTimer, u32Value, uCmp));
                break;
            }

            case HPET_TN_ROUTE:
                u32Value = (uint32_t)(pHpetTimer->u64Fsb >> 32); /** @todo Looks wrong, but since it's not supported, who cares. */
                Log(("HPET[%u]: read32 HPET_TN_ROUTE: %#x\n", iTimerNo, u32Value));
                break;

            default:
                LogRelMax(10, ("HPET[%u]: Invalid HPET register read: %d\n", iTimerNo, iTimerReg));
                u32Value = 0;
                break;
        }
    }
    else
    {
        LogRelMax(10, ("HPET: Using timer above configured range: %d\n", iTimerNo));
        u32Value = 0;
    }
    return u32Value;
}


/**
 * Reads a HPET timer register, 64-bit access.
 *
 * @returns The register value.
 * @param   pThis               The HPET instance.
 * @param   iTimerNo            The timer index.
 * @param   iTimerReg           The index of the timer register to read.
 */
static uint64_t hpetTimerRegRead64(PHPET pThis, uint32_t iTimerNo, uint32_t iTimerReg)
{
    uint64_t u64Value;
    if (   iTimerNo < HPET_CAP_GET_TIMERS(pThis->u32Capabilities)
        && iTimerNo < RT_ELEMENTS(pThis->aTimers) )
    {
        PHPETTIMER pHpetTimer = &pThis->aTimers[iTimerNo];
        switch (iTimerReg)
        {
            case HPET_TN_CFG:
                u64Value = ASMAtomicReadU64(&pHpetTimer->u64Config);
                Log(("HPET[%u]: read64 HPET_TN_CFG: %#RX64\n", iTimerNo, u64Value));
                break;

            case HPET_TN_CMP:
                u64Value = ASMAtomicReadU64(&pHpetTimer->u64Cmp);
                Log(("HPET[%u]: read64 HPET_TN_CMP: %#RX64\n", iTimerNo, u64Value));
                break;

            case HPET_TN_ROUTE:
                u64Value = (uint32_t)(pHpetTimer->u64Fsb >> 32); /** @todo Looks wrong, but since it's not supported, who cares. */
                Log(("HPET[%u]: read64 HPET_TN_ROUTE: %#RX64\n", iTimerNo, u64Value));
                break;

            default:
                LogRelMax(10, ("HPET[%u]: Invalid 64-bit HPET register read64: %d\n", iTimerNo, iTimerReg));
                u64Value = 0;
                break;
        }
    }
    else
    {
        LogRelMax(10, ("HPET: Using timer above configured range: %d\n", iTimerNo));
        u64Value = 0;
    }
    return u64Value;
}


/**
 * 32-bit write to a HPET timer register.
 *
 * @returns Strict VBox status code.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared HPET state.
 * @param   iTimerNo        The timer being written to.
 * @param   iTimerReg       The register being written to.
 * @param   u32NewValue     The value being written.
 *
 * @remarks The caller should not hold any locks.
 */
static VBOXSTRICTRC hpetTimerRegWrite32(PPDMDEVINS pDevIns, PHPET pThis, uint32_t iTimerNo,
                                        uint32_t iTimerReg, uint32_t u32NewValue)
{
    Assert(!PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    Assert(!PDMDevHlpTimerIsLockOwner(pDevIns, pThis->aTimers[0].hTimer));

    if (   iTimerNo < HPET_CAP_GET_TIMERS(pThis->u32Capabilities)
        && iTimerNo < RT_ELEMENTS(pThis->aTimers) )    /* Parfait - see above. */
    {
        PHPETTIMER pHpetTimer = &pThis->aTimers[iTimerNo];

        switch (iTimerReg)
        {
            case HPET_TN_CFG:
            {
                /*
                 * Calculate the writable mask and see if anything actually changed
                 * before doing any locking.  Windows 10 (1809) does two CFG writes
                 * with the same value (0x134) when reprogramming the HPET#0 timer.
                 */
                uint64_t       fConfig = ASMAtomicUoReadU64(&pHpetTimer->u64Config);
                uint64_t const fMask   = HPET_TN_CFG_WRITE_MASK
                                       | (fConfig & HPET_TN_PERIODIC_CAP ? HPET_TN_PERIODIC : 0)
                                       | (fConfig & HPET_TN_SIZE_CAP     ? HPET_TN_32BIT    : 0);
                if ((u32NewValue & fMask) == (fConfig & fMask))
                    Log(("HPET[%u]: write32 HPET_TN_CFG: %#x - no change (%#RX64)\n", iTimerNo, u32NewValue, fConfig));
                else
                {
#ifndef IN_RING3
                    /* Return to ring-3 (where LogRel works) to complain about level-triggered interrupts. */
                    if ((u32NewValue & HPET_TN_INT_TYPE) == HPET_TIMER_TYPE_LEVEL)
                        return VINF_IOM_R3_MMIO_WRITE;
#endif
                    DEVHPET_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE);

                    fConfig = ASMAtomicUoReadU64(&pHpetTimer->u64Config);
                    uint64_t const fConfigNew = hpetUpdateMasked(u32NewValue, fConfig, fMask);
                    Log(("HPET[%u]: write HPET_TN_CFG: %#RX64 -> %#RX64\n", iTimerNo, fConfig, fConfigNew));

                    if ((fConfigNew & HPET_TN_32BIT) == (fConfig & HPET_TN_32BIT))
                    { /* likely it stays the same */ }
                    else if (fConfigNew & HPET_TN_32BIT)
                    {
                        Log(("HPET[%u]: Changing timer to 32-bit mode.\n", iTimerNo));
                        /* Clear the top bits of the comparator and period to be on the safe side. */
                        ASMAtomicUoWriteU64(&pHpetTimer->u64Cmp,    (uint32_t)pHpetTimer->u64Cmp);
                        ASMAtomicUoWriteU64(&pHpetTimer->u64Period, (uint32_t)pHpetTimer->u64Period);
                    }
                    else
                        Log(("HPET[%u]: Changing timer to 64-bit mode.\n", iTimerNo));
                    ASMAtomicWriteU64(&pHpetTimer->u64Config, fConfigNew);

                    DEVHPET_UNLOCK(pDevIns, pThis);

                    if (RT_LIKELY((fConfigNew & HPET_TN_INT_TYPE) != HPET_TIMER_TYPE_LEVEL))
                    { /* likely */ }
                    else
                    {
                        LogRelMax(10, ("HPET[%u]: Level-triggered config not yet supported\n", iTimerNo));
                        ASSERT_GUEST_MSG_FAILED(("Level-triggered config not yet supported"));
                    }
                }
                break;
            }

            case HPET_TN_CFG + 4: /* Interrupt capabilities - read only. */
                Log(("HPET[%u]: write32 HPET_TN_CFG + 4 (ignored)\n", iTimerNo));
                break;

            case HPET_TN_CMP: /* lower bits of comparator register */
            {
                DEVHPET_LOCK_BOTH_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE);
                uint64_t fConfig = ASMAtomicUoReadU64(&pHpetTimer->u64Config);
                Log(("HPET[%u]: write32 HPET_TN_CMP: %#x (fCfg=%#RX32)\n", iTimerNo, u32NewValue, (uint32_t)fConfig));

                if (fConfig & HPET_TN_PERIODIC)
                    ASMAtomicUoWriteU64(&pHpetTimer->u64Period, RT_MAKE_U64(u32NewValue, RT_HI_U32(pHpetTimer->u64Period)));

                if (!(fConfig & HPET_TN_PERIODIC) || (fConfig & HPET_TN_SETVAL))
                    ASMAtomicUoWriteU64(&pHpetTimer->u64Cmp, RT_MAKE_U64(u32NewValue, RT_HI_U32(pHpetTimer->u64Cmp)));

                ASMAtomicAndU64(&pHpetTimer->u64Config, ~HPET_TN_SETVAL);
                Log2(("HPET[%u]: after32 HPET_TN_CMP cmp=%#llx per=%#llx\n", iTimerNo, pHpetTimer->u64Cmp, pHpetTimer->u64Period));

                if (pThis->u64HpetConfig & HPET_CFG_ENABLE)
                    hpetProgramTimer(pDevIns, pThis, pHpetTimer, PDMDevHlpTimerGet(pDevIns, pHpetTimer->hTimer));
                DEVHPET_UNLOCK_BOTH(pDevIns, pThis);
                break;
            }

            /** @todo figure out how exactly it behaves wrt to HPET_TN_SETVAL   */
            case HPET_TN_CMP + 4: /* upper bits of comparator register */
            {
                DEVHPET_LOCK_BOTH_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE);
                uint64_t fConfig = ASMAtomicUoReadU64(&pHpetTimer->u64Config);

                if (!hpet32bitTimerEx(fConfig))
                {
                    Log(("HPET[%u]: write32 HPET_TN_CMP + 4: %#x (fCfg=%#RX32)\n", iTimerNo, u32NewValue, (uint32_t)fConfig));
                    if (fConfig & HPET_TN_PERIODIC)
                        ASMAtomicUoWriteU64(&pHpetTimer->u64Period, RT_MAKE_U64(RT_LO_U32(pHpetTimer->u64Period), u32NewValue));

                    if (!(fConfig & HPET_TN_PERIODIC) || (fConfig & HPET_TN_SETVAL))
                        ASMAtomicUoWriteU64(&pHpetTimer->u64Cmp, RT_MAKE_U64(RT_LO_U32(pHpetTimer->u64Cmp), u32NewValue));

                    ASMAtomicAndU64(&pHpetTimer->u64Config, ~HPET_TN_SETVAL);
                    Log2(("HPET[%u]: after32 HPET_TN_CMP+4: cmp=%#llx per=%#llx\n", iTimerNo, pHpetTimer->u64Cmp, pHpetTimer->u64Period));

                    if (pThis->u64HpetConfig & HPET_CFG_ENABLE)
                        hpetProgramTimer(pDevIns, pThis, pHpetTimer, PDMDevHlpTimerGet(pDevIns, pHpetTimer->hTimer));
                }
                else
                    Log(("HPET[%u]: write32 HPET_TN_CMP + 4: %#x - but timer is 32-bit!! (fCfg=%#RX32)\n", iTimerNo, u32NewValue, (uint32_t)fConfig));
                DEVHPET_UNLOCK_BOTH(pDevIns, pThis);
                break;
            }

            case HPET_TN_ROUTE:
                Log(("HPET[%u]: write32 HPET_TN_ROUTE (ignored)\n", iTimerNo));
                break;

            case HPET_TN_ROUTE + 4:
                Log(("HPET[%u]: write32 HPET_TN_ROUTE + 4 (ignored)\n", iTimerNo));
                break;

            default:
                LogRelMax(10, ("HPET[%u]: Invalid timer register write: %d\n", iTimerNo, iTimerReg));
                break;
        }
    }
    else
        LogRelMax(10, ("HPET: Using timer above configured range: %d (reg %#x)\n", iTimerNo, iTimerReg));
    return VINF_SUCCESS;
}


/**
 * 64-bit write to a HPET timer register.
 *
 * @returns Strict VBox status code.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared HPET state.
 * @param   iTimerNo        The timer being written to.
 * @param   iTimerReg       The register being written to.
 * @param   u64NewValue     The value being written.
 *
 * @remarks The caller should not hold any locks.
 */
static VBOXSTRICTRC hpetTimerRegWrite64(PPDMDEVINS pDevIns, PHPET pThis, uint32_t iTimerNo,
                                        uint32_t iTimerReg, uint64_t u64NewValue)
{
    Assert(!PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    Assert(!PDMDevHlpTimerIsLockOwner(pDevIns, pThis->aTimers[0].hTimer));
    Assert(!(iTimerReg & 7));

    if (   iTimerNo < HPET_CAP_GET_TIMERS(pThis->u32Capabilities)
        && iTimerNo < RT_ELEMENTS(pThis->aTimers) )    /* Parfait - see above. */
    {
        PHPETTIMER pHpetTimer = &pThis->aTimers[iTimerNo];

        switch (iTimerReg)
        {
            case HPET_TN_CFG:
                /* The upper 32 bits are not writable, so join paths with the 32-bit version. */
                return hpetTimerRegWrite32(pDevIns, pThis, iTimerNo, iTimerReg, (uint32_t)u64NewValue);

            case HPET_TN_CMP:
            {
                DEVHPET_LOCK_BOTH_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE);
                uint64_t fConfig = ASMAtomicUoReadU64(&pHpetTimer->u64Config);
                Log(("HPET[%u]: write64 HPET_TN_CMP: %#RX64 (fCfg=%#RX64)\n", iTimerNo, u64NewValue, (uint32_t)fConfig));

                /** @todo not sure if this is right, but it is consistent with the 32-bit config
                 *        change behaviour and defensive wrt mixups. */
                if (!hpet32bitTimerEx(fConfig))
                { /* likely */ }
                else
                    u64NewValue = (uint32_t)u64NewValue;

                if (fConfig & HPET_TN_PERIODIC)
                    ASMAtomicUoWriteU64(&pHpetTimer->u64Period, u64NewValue);

                if (!(fConfig & HPET_TN_PERIODIC) || (fConfig & HPET_TN_SETVAL))
                    ASMAtomicUoWriteU64(&pHpetTimer->u64Cmp, u64NewValue);

                ASMAtomicAndU64(&pHpetTimer->u64Config, ~HPET_TN_SETVAL);
                Log2(("HPET[%u]: after64 HPET_TN_CMP cmp=%#llx per=%#llx\n", iTimerNo, pHpetTimer->u64Cmp, pHpetTimer->u64Period));

                if (pThis->u64HpetConfig & HPET_CFG_ENABLE)
                    hpetProgramTimer(pDevIns, pThis, pHpetTimer, PDMDevHlpTimerGet(pDevIns, pHpetTimer->hTimer));
                DEVHPET_UNLOCK_BOTH(pDevIns, pThis);
                break;
            }

            case HPET_TN_ROUTE:
                Log(("HPET[%u]: write64 HPET_TN_ROUTE (ignored)\n", iTimerNo));
                break;

            default:
                LogRelMax(10, ("HPET[%u]: Invalid timer register write: %d\n", iTimerNo, iTimerReg));
                break;
        }
    }
    else
        LogRelMax(10, ("HPET: Using timer above configured range: %d (reg %#x)\n", iTimerNo, iTimerReg));
    return VINF_SUCCESS;
}


/* -=-=-=-=-=- Non-timer register accesses -=-=-=-=-=- */


/**
 * Read a 32-bit HPET register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared HPET state.
 * @param   idxReg              The register to read.
 * @param   pu32Value           Where to return the register value.
 *
 * @remarks The caller must not own the device lock if HPET_COUNTER is read.
 */
static VBOXSTRICTRC hpetConfigRegRead32(PPDMDEVINS pDevIns, PHPET pThis, uint32_t idxReg, uint32_t *pu32Value)
{
    Assert(!PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect) || (idxReg != HPET_COUNTER && idxReg != HPET_COUNTER + 4));

    uint32_t u32Value;
    switch (idxReg)
    {
        case HPET_ID:
            DEVHPET_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_READ);
            u32Value = pThis->u32Capabilities;
            DEVHPET_UNLOCK(pDevIns, pThis);
            Log(("read HPET_ID: %#x\n", u32Value));
            break;

        case HPET_PERIOD:
            u32Value = pThis->fIch9 ? HPET_CLK_PERIOD_ICH9 : HPET_CLK_PERIOD_PIIX;
            Log(("read HPET_PERIOD: %#x\n", u32Value));
            break;

        case HPET_CFG:
            DEVHPET_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_READ);
            u32Value = (uint32_t)pThis->u64HpetConfig;
            DEVHPET_UNLOCK(pDevIns, pThis);
            Log(("read HPET_CFG: %#x\n", u32Value));
            break;

        case HPET_CFG + 4:
            DEVHPET_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_READ);
            u32Value = (uint32_t)(pThis->u64HpetConfig >> 32);
            DEVHPET_UNLOCK(pDevIns, pThis);
            Log(("read of HPET_CFG + 4: %#x\n", u32Value));
            break;

        case HPET_COUNTER:
        case HPET_COUNTER + 4:
        {
            /** @todo We don't technically need to sit on the virtualsync lock here to
             *        read it, but it helps wrt quality... */
            DEVHPET_LOCK_BOTH_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_READ);

            uint64_t u64Ticks;
            if (pThis->u64HpetConfig & HPET_CFG_ENABLE)
            {
                uint64_t const tsNow = PDMDevHlpTimerGet(pDevIns, pThis->aTimers[0].hTimer);
                PDMDevHlpTimerUnlockClock(pDevIns, pThis->aTimers[0].hTimer);
                u64Ticks = hpetGetTicksEx(pThis, tsNow);
            }
            else
            {
                PDMDevHlpTimerUnlockClock(pDevIns, pThis->aTimers[0].hTimer);
                u64Ticks = pThis->u64HpetCounter;
            }

            STAM_REL_COUNTER_INC(&pThis->StatCounterRead4Byte);
            DEVHPET_UNLOCK(pDevIns, pThis);

            /** @todo is it correct? */
            u32Value = idxReg == HPET_COUNTER ? (uint32_t)u64Ticks : (uint32_t)(u64Ticks >> 32);
            Log(("read HPET_COUNTER: %s part value %x (%#llx)\n", (idxReg == HPET_COUNTER) ? "low" : "high", u32Value, u64Ticks));
            break;
        }

        case HPET_STATUS:
            DEVHPET_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_READ);
            u32Value = (uint32_t)pThis->u64Isr;
            DEVHPET_UNLOCK(pDevIns, pThis);
            Log(("read HPET_STATUS: %#x\n", u32Value));
            break;

        default:
            Log(("invalid HPET register read: %x\n", idxReg));
            u32Value = 0;
            break;
    }

    *pu32Value = u32Value;
    return VINF_SUCCESS;
}


/**
 * 32-bit write to a config register.
 *
 * @returns Strict VBox status code.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared HPET state.
 * @param   idxReg          The register being written to.
 * @param   u32NewValue     The value being written.
 *
 * @remarks The caller should not hold the device lock, unless it also holds
 *          the TM lock.
 */
static VBOXSTRICTRC hpetConfigRegWrite32(PPDMDEVINS pDevIns, PHPET pThis, uint32_t idxReg, uint32_t u32NewValue)
{
    Assert(!PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect) || PDMDevHlpTimerIsLockOwner(pDevIns, pThis->aTimers[0].hTimer));

    VBOXSTRICTRC rc = VINF_SUCCESS;
    switch (idxReg)
    {
        case HPET_ID:
        case HPET_ID + 4:
        {
            Log(("write HPET_ID, useless\n"));
            break;
        }

        case HPET_CFG:
        {
            DEVHPET_LOCK_BOTH_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE);
            uint32_t const iOldValue = (uint32_t)(pThis->u64HpetConfig);
            Log(("write HPET_CFG: %x (old %x)\n", u32NewValue, iOldValue));

            /*
             * This check must be here, before actual update, as hpetLegacyMode
             * may request retry in R3 - so we must keep state intact.
             */
            if ((iOldValue ^ u32NewValue) & HPET_CFG_LEGACY)
            {
#ifdef IN_RING3
                PHPETCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHPETCC);
                if (pThisCC->pHpetHlp != NULL)
                {
                    rc = pThisCC->pHpetHlp->pfnSetLegacyMode(pDevIns, RT_BOOL(u32NewValue & HPET_CFG_LEGACY));
                    if (rc != VINF_SUCCESS)
                    {
                        DEVHPET_UNLOCK_BOTH(pDevIns, pThis);
                        break;
                    }
                }
#else
                rc = VINF_IOM_R3_MMIO_WRITE;
                DEVHPET_UNLOCK_BOTH(pDevIns, pThis);
                break;
#endif
            }

            /* Updating it using an atomic write just to be on the safe side. */
            ASMAtomicWriteU64(&pThis->u64HpetConfig, hpetUpdateMasked(u32NewValue, iOldValue, HPET_CFG_WRITE_MASK));

            uint32_t const cTimers = RT_MIN(HPET_CAP_GET_TIMERS(pThis->u32Capabilities), RT_ELEMENTS(pThis->aTimers));
            if (hpetBitJustSet(iOldValue, u32NewValue, HPET_CFG_ENABLE))
            {
                /*
                 * Enable main counter and interrupt generation.
                 */
                uint64_t u64TickLimit = pThis->fIch9 ? HPET_TICKS_IN_100YR_ICH9 : HPET_TICKS_IN_100YR_PIIX;
                if (pThis->u64HpetCounter <= u64TickLimit)
                    pThis->u64HpetOffset = hpetTicksToNs(pThis, pThis->u64HpetCounter);
                else
                {
                    LogRelMax(10, ("HPET: Counter set more than 100 years in the future, reducing.\n"));
                    pThis->u64HpetOffset = 1000000LL * 60 * 60 * 24 * 365 * 100;
                }

                uint64_t const tsNow = PDMDevHlpTimerGet(pDevIns, pThis->aTimers[0].hTimer);
                pThis->u64HpetOffset -= tsNow;

                for (uint32_t i = 0; i < cTimers; i++)
                    if (pThis->aTimers[i].u64Cmp != hpetInvalidValue(&pThis->aTimers[i]))
                        hpetProgramTimer(pDevIns, pThis, &pThis->aTimers[i], tsNow);
            }
            else if (hpetBitJustCleared(iOldValue, u32NewValue, HPET_CFG_ENABLE))
            {
                /*
                 * Halt main counter and disable interrupt generation.
                 */
                pThis->u64HpetCounter = hpetGetTicksEx(pThis, PDMDevHlpTimerGet(pDevIns, pThis->aTimers[0].hTimer));
                for (uint32_t i = 0; i < cTimers; i++)
                    PDMDevHlpTimerStop(pDevIns, pThis->aTimers[i].hTimer);
            }

            DEVHPET_UNLOCK_BOTH(pDevIns, pThis);
            break;
        }

        case HPET_CFG + 4:
        {
/** @todo r=bird: Is the whole upper part of the config register really
 * writable?  Only 2 bits are writable in the lower part... */
            DEVHPET_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE);
            pThis->u64HpetConfig = hpetUpdateMasked((uint64_t)u32NewValue << 32,
                                                    pThis->u64HpetConfig,
                                                    UINT64_C(0xffffffff00000000));
            Log(("write HPET_CFG + 4: %x -> %#llx\n", u32NewValue, pThis->u64HpetConfig));
            DEVHPET_UNLOCK(pDevIns, pThis);
            break;
        }

        case HPET_STATUS:
        {
            DEVHPET_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE);
            /* Clear ISR for all set bits in u32NewValue, see p. 14 of the HPET spec. */
            pThis->u64Isr &= ~((uint64_t)u32NewValue);
            Log(("write HPET_STATUS: %x -> ISR=%#llx\n", u32NewValue, pThis->u64Isr));
            DEVHPET_UNLOCK(pDevIns, pThis);
            break;
        }

        case HPET_STATUS + 4:
        {
            Log(("write HPET_STATUS + 4: %x\n", u32NewValue));
            if (u32NewValue != 0)
                LogRelMax(10, ("HPET: Writing HPET_STATUS + 4 with non-zero, ignored\n"));
            break;
        }

        case HPET_COUNTER:
        {
            STAM_REL_COUNTER_INC(&pThis->StatCounterWriteLow);
            DEVHPET_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE);
            pThis->u64HpetCounter = RT_MAKE_U64(u32NewValue, RT_HI_U32(pThis->u64HpetCounter));
/** @todo how is this supposed to work if the HPET is enabled? */
            Log(("write HPET_COUNTER: %#x -> %llx\n", u32NewValue, pThis->u64HpetCounter));
            DEVHPET_UNLOCK(pDevIns, pThis);
            break;
        }

        case HPET_COUNTER + 4:
        {
            STAM_REL_COUNTER_INC(&pThis->StatCounterWriteHigh);
            DEVHPET_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE);
            pThis->u64HpetCounter = RT_MAKE_U64(RT_LO_U32(pThis->u64HpetCounter), u32NewValue);
            Log(("write HPET_COUNTER + 4: %#x -> %llx\n", u32NewValue, pThis->u64HpetCounter));
            DEVHPET_UNLOCK(pDevIns, pThis);
            break;
        }

        default:
            LogRelMax(10, ("HPET: Invalid HPET config write: %x\n", idxReg));
            break;
    }

    return rc;
}


/* -=-=-=-=-=- MMIO callbacks -=-=-=-=-=- */


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) hpetMMIORead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    HPET      *pThis  = PDMDEVINS_2_DATA(pDevIns, HPET*);
    NOREF(pvUser);
    Assert(cb == 4 || cb == 8);
    Assert(!(off & (cb - 1)));

    LogFlow(("hpetMMIORead (%d): %RGp\n", cb, off));

    VBOXSTRICTRC rc;
    if (cb == 4)
    {
        /*
         * 4-byte access.
         */
        if (off >= 0x100 && off < 0x400)
        {
            *(uint32_t *)pv = hpetTimerRegRead32(pThis,
                                                 (uint32_t)(off - 0x100) / 0x20,
                                                 (uint32_t)(off - 0x100) % 0x20);
            rc = VINF_SUCCESS;
        }
        else
            rc = hpetConfigRegRead32(pDevIns, pThis, off, (uint32_t *)pv);
    }
    else
    {
        /*
         * 8-byte access - Split the access except for timing sensitive registers.
         * The others assume the protection of the lock.
         */
        PRTUINT64U pValue = (PRTUINT64U)pv;
        if (off == HPET_COUNTER)
        {
            /** @todo We don't technically need to sit on the virtualsync lock here to
             *        read it, but it helps wrt quality... */
            /* When reading HPET counter we must read it in a single read,
               to avoid unexpected time jumps on 32-bit overflow. */
            DEVHPET_LOCK_BOTH_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_READ);

            if (pThis->u64HpetConfig & HPET_CFG_ENABLE)
            {
                uint64_t const tsNow = PDMDevHlpTimerGet(pDevIns, pThis->aTimers[0].hTimer);
                PDMDevHlpTimerUnlockClock(pDevIns, pThis->aTimers[0].hTimer);
                pValue->u = hpetGetTicksEx(pThis, tsNow);
            }
            else
            {
                PDMDevHlpTimerUnlockClock(pDevIns, pThis->aTimers[0].hTimer);
                pValue->u = pThis->u64HpetCounter;
            }

            STAM_REL_COUNTER_INC(&pThis->StatCounterRead8Byte);
            DEVHPET_UNLOCK(pDevIns, pThis);
            rc = VINF_SUCCESS;
        }
        else
        {
            if (off >= 0x100 && off < 0x400)
            {
                uint32_t iTimer    = (uint32_t)(off - 0x100) / 0x20;
                uint32_t iTimerReg = (uint32_t)(off - 0x100) % 0x20;
                Assert(!(iTimerReg & 7));
                pValue->u = hpetTimerRegRead64(pThis, iTimer, iTimerReg);
                rc = VINF_SUCCESS;
            }
            else
            {
                /* for most 8-byte accesses we just split them, happens under lock anyway. */
                DEVHPET_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_READ);
                rc = hpetConfigRegRead32(pDevIns, pThis, off, &pValue->s.Lo);
                if (rc == VINF_SUCCESS)
                    rc = hpetConfigRegRead32(pDevIns, pThis, off + 4, &pValue->s.Hi);
                DEVHPET_UNLOCK(pDevIns, pThis);
            }
        }
    }
    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) hpetMMIOWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    HPET  *pThis  = PDMDEVINS_2_DATA(pDevIns, HPET*);
    LogFlow(("hpetMMIOWrite: cb=%u reg=%RGp val=%llx\n",
             cb, off, cb == 4 ? *(uint32_t *)pv : cb == 8 ? *(uint64_t *)pv : 0xdeadbeef));
    NOREF(pvUser);
    Assert(cb == 4 || cb == 8);
    Assert(!(off & (cb - 1)));

    VBOXSTRICTRC rc;
    if (cb == 4)
    {
        if (off >= 0x100 && off < 0x400)
            rc = hpetTimerRegWrite32(pDevIns, pThis,
                                     (uint32_t)(off - 0x100) / 0x20,
                                     (uint32_t)(off - 0x100) % 0x20,
                                     *(uint32_t const *)pv);
        else
            rc = hpetConfigRegWrite32(pDevIns, pThis, off, *(uint32_t const *)pv);
    }
    else
    {
        /*
         * 8-byte access.
         */
        if (off >= 0x100 && off < 0x400)
            rc = hpetTimerRegWrite64(pDevIns, pThis,
                                     (uint32_t)(off - 0x100) / 0x20,
                                     (uint32_t)(off - 0x100) % 0x20,
                                     *(uint64_t const *)pv);
        else
        {
            /* Split the access and rely on the locking to prevent trouble. */
            RTUINT64U uValue;
            uValue.u = *(uint64_t const *)pv;
            DEVHPET_LOCK_BOTH_RETURN(pDevIns, pThis, VINF_IOM_R3_MMIO_WRITE);
            rc = hpetConfigRegWrite32(pDevIns, pThis, off, uValue.s.Lo);
            if (RT_LIKELY(rc == VINF_SUCCESS))
                rc = hpetConfigRegWrite32(pDevIns, pThis, off + 4, uValue.s.Hi);
            DEVHPET_UNLOCK_BOTH(pDevIns, pThis);
        }
    }

    return rc;
}

#ifdef IN_RING3

/* -=-=-=-=-=- Timer Callback Processing -=-=-=-=-=- */

/**
 * Gets the IRQ of an HPET timer.
 *
 * @returns IRQ number.
 * @param   pThis               The shared HPET state.
 * @param   pHpetTimer          The HPET timer.
 * @param   fConfig             The HPET timer config value.
 */
DECLINLINE(uint32_t) hpetR3TimerGetIrq(PHPET pThis, PCHPETTIMER pHpetTimer, uint64_t fConfig)
{
    /*
     * Per spec, in legacy mode the HPET timers are wired as follows:
     *   timer 0: IRQ0 for PIC and IRQ2 for APIC
     *   timer 1: IRQ8 for both PIC and APIC
     *
     * ISA IRQ delivery logic will take care of correct delivery
     * to the different ICs.
     */
    if (   pHpetTimer->idxTimer <= 1
        && (pThis->u64HpetConfig & HPET_CFG_LEGACY))
        return pHpetTimer->idxTimer == 0 ? 0 : 8;

    return (fConfig & HPET_TN_INT_ROUTE_MASK) >> HPET_TN_INT_ROUTE_SHIFT;
}


/**
 * @callback_method_impl{FNTMTIMERDEV, Device timer callback function.}
 *
 * @note    Only the virtual sync lock is held when called.
 */
static DECLCALLBACK(void) hpetR3Timer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PHPET           pThis      = PDMDEVINS_2_DATA(pDevIns, PHPET);
    PHPETTIMER      pHpetTimer = (HPETTIMER *)pvUser;

    /*
     * Read the timer configuration values we need first.
     *
     * The comparator and period are only written while owning the virtual sync
     * lock, so we don't run any risk there.  The configuration register is
     * written with only the device lock, so must be a bit more careful with it.
     */
    uint64_t        uCmp       = ASMAtomicUoReadU64(&pHpetTimer->u64Cmp);
    uint64_t const  uPeriod    = ASMAtomicUoReadU64(&pHpetTimer->u64Period);
    uint64_t const  fConfig    = ASMAtomicUoReadU64(&pHpetTimer->u64Config);
    Assert(hTimer == pHpetTimer->hTimer);

    if (fConfig & HPET_TN_PERIODIC)
    {
        if (uPeriod)
        {
            uint64_t const tsNow        = PDMDevHlpTimerGet(pDevIns, pHpetTimer->hTimer);
            uint64_t const uHpetNow     = hpetGetTicksEx(pThis, tsNow);
            uCmp = hpetAdjustComparator(pHpetTimer, fConfig, uCmp, uPeriod, uHpetNow);
            uint64_t const cTicksDiff   = hpetComputeDiff(fConfig, uCmp, uHpetNow);
            uint64_t const u64TickLimit = pThis->fIch9 ? HPET_TICKS_IN_100YR_ICH9 : HPET_TICKS_IN_100YR_PIIX;
            if (cTicksDiff <= u64TickLimit)
            {
                uint64_t const tsDeadline = tsNow + hpetTicksToNs(pThis, cTicksDiff);
                Log4(("HPET[%u]: periodic: next in %llu\n", pHpetTimer->idxTimer, tsDeadline));
                PDMDevHlpTimerSet(pDevIns, hTimer, tsDeadline);
                STAM_REL_COUNTER_INC(&pHpetTimer->StatSetTimer);
            }
            else
                LogRelMax(10, ("HPET[%u]: Not scheduling periodic interrupt more than 100 years in the future.\n",
                               pHpetTimer->idxTimer));
        }
    }
    /* For 32-bit non-periodic timers, generate wrap-around interrupts. */
    else if (pHpetTimer->u8Wrap && hpet32bitTimerEx(fConfig))
    {
        pHpetTimer->u8Wrap = 0;         /* (only modified while owning the virtual sync lock) */
        uint64_t const tsNow      = PDMDevHlpTimerGet(pDevIns, hTimer);
        uint64_t const uHpetNow   = nsToHpetTicks(pThis, tsNow + pThis->u64HpetOffset);
        uint64_t const cTicksDiff = hpetComputeDiff(fConfig, uCmp, uHpetNow);
        uint64_t const tsDeadline = tsNow + hpetTicksToNs(pThis, cTicksDiff);
        Log4(("HPET[%u]: post-wrap deadline: %llu\n", pHpetTimer->idxTimer, tsDeadline));
        PDMDevHlpTimerSet(pDevIns, pHpetTimer->hTimer, tsDeadline);
    }

    /*
     * IRQ update.
     */
    if (   (fConfig & HPET_TN_ENABLE)
        && (pThis->u64HpetConfig & HPET_CFG_ENABLE))
    {
        AssertCompile(HPET_TN_INT_TYPE == 2);

        /* We trigger flip/flop in edge-triggered mode and do nothing in
           level-triggered mode yet. */
        if ((fConfig & HPET_TN_INT_TYPE) == HPET_TIMER_TYPE_EDGE)
        {
            PHPETCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHPETCC);
            AssertReturnVoid(pThisCC);

            uint32_t const uIrq = hpetR3TimerGetIrq(pThis, pHpetTimer, fConfig);
            Log4(("HPET[%u]: raising IRQ %u\n", pHpetTimer->idxTimer, uIrq));

            pThisCC->pHpetHlp->pfnSetIrq(pDevIns, uIrq, PDM_IRQ_LEVEL_FLIP_FLOP);
            STAM_REL_COUNTER_INC(&pHpetTimer->StatSetIrq);
        }
        /* ISR bits are only set in level-triggered mode. */
        else
        {
            Assert((fConfig & HPET_TN_INT_TYPE) == HPET_TIMER_TYPE_LEVEL);
            ASMAtomicOrU64(&pThis->u64Isr, RT_BIT_64(pHpetTimer->idxTimer));
            /** @todo implement IRQs in level-triggered mode */
        }
    }

}


/* -=-=-=-=-=- DBGF Info Handlers -=-=-=-=-=- */


/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) hpetR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PHPET pThis = PDMDEVINS_2_DATA(pDevIns, PHPET);
    NOREF(pszArgs);

    pHlp->pfnPrintf(pHlp,
                    "HPET status:\n"
                    " config=%016RX64     isr=%016RX64\n"
                    " offset=%016RX64 counter=%016RX64 frequency=%u fs\n"
                    " legacy-mode=%s  timer-count=%u\n",
                    pThis->u64HpetConfig, pThis->u64Isr,
                    pThis->u64HpetOffset, pThis->u64HpetCounter, pThis->fIch9 ? HPET_CLK_PERIOD_ICH9 : HPET_CLK_PERIOD_PIIX,
                    !!(pThis->u64HpetConfig & HPET_CFG_LEGACY) ? "on " : "off",
                    HPET_CAP_GET_TIMERS(pThis->u32Capabilities));
    pHlp->pfnPrintf(pHlp,
                    "Timers:\n");
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aTimers); i++)
    {
        static const struct
        {
            const char *psz;
            uint32_t cch;
            uint32_t fFlags;
        } s_aFlags[] =
        {
            { RT_STR_TUPLE(" lvl"),     HPET_TN_INT_TYPE },
            { RT_STR_TUPLE(" en"),      HPET_TN_ENABLE },
            { RT_STR_TUPLE(" per"),     HPET_TN_PERIODIC },
            { RT_STR_TUPLE(" cap_per"), HPET_TN_PERIODIC_CAP },
            { RT_STR_TUPLE(" cap_64"),  HPET_TN_SIZE_CAP },
            { RT_STR_TUPLE(" setval"),  HPET_TN_SETVAL },
            { RT_STR_TUPLE(" 32b"),     HPET_TN_32BIT },
        };
        char        szTmp[64];
        uint64_t    fCfg = pThis->aTimers[i].u64Config;
        size_t      off  = 0;
        for (unsigned j = 0; j < RT_ELEMENTS(s_aFlags); j++)
            if (fCfg & s_aFlags[j].fFlags)
            {
                memcpy(&szTmp[off], s_aFlags[j].psz, s_aFlags[j].cch);
                off  += s_aFlags[j].cch;
                fCfg &= ~(uint64_t)s_aFlags[j].fFlags;
            }
        szTmp[off] = '\0';
        Assert(off < sizeof(szTmp));

        pHlp->pfnPrintf(pHlp,
                        " %d: comparator=%016RX64 accumulator=%016RX64 (%RU64 ns)\n"
                         "        config=%016RX64 irq=%d%s\n",
                        pThis->aTimers[i].idxTimer,
                        pThis->aTimers[i].u64Cmp,
                        pThis->aTimers[i].u64Period,
                        pThis->aTimers[i].u64Period < (pThis->fIch9 ? HPET_TICKS_IN_100YR_ICH9 : HPET_TICKS_IN_100YR_PIIX)
                        ? hpetTicksToNs(pThis, pThis->aTimers[i].u64Period) : UINT64_MAX,
                        pThis->aTimers[i].u64Config,
                        hpetR3TimerGetIrq(pThis, &pThis->aTimers[i], pThis->aTimers[i].u64Config),
                        szTmp);
    }
}


/* -=-=-=-=-=- Saved State -=-=-=-=-=- */


/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC}
 */
static DECLCALLBACK(int) hpetR3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PHPET           pThis   = PDMDEVINS_2_DATA(pDevIns, PHPET);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    NOREF(uPass);

    pHlp->pfnSSMPutU8(pSSM, HPET_CAP_GET_TIMERS(pThis->u32Capabilities));

    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) hpetR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PHPET           pThis   = PDMDEVINS_2_DATA(pDevIns, PHPET);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;

    /*
     * The config.
     */
    hpetR3LiveExec(pDevIns, pSSM, SSM_PASS_FINAL);

    /*
     * The state.
     */
    uint32_t const cTimers = HPET_CAP_GET_TIMERS(pThis->u32Capabilities);
    AssertReturn(cTimers <= RT_ELEMENTS(pThis->aTimers), VERR_OUT_OF_RANGE);
    for (uint32_t iTimer = 0; iTimer < cTimers; iTimer++)
    {
        PHPETTIMER pHpetTimer = &pThis->aTimers[iTimer];
        PDMDevHlpTimerSave(pDevIns, pHpetTimer->hTimer, pSSM);
        pHlp->pfnSSMPutU8(pSSM,  pHpetTimer->u8Wrap);
        pHlp->pfnSSMPutU64(pSSM, pHpetTimer->u64Config);
        pHlp->pfnSSMPutU64(pSSM, pHpetTimer->u64Cmp);
        pHlp->pfnSSMPutU64(pSSM, pHpetTimer->u64Fsb);
        pHlp->pfnSSMPutU64(pSSM, pHpetTimer->u64Period);
    }

    pHlp->pfnSSMPutU64(pSSM, pThis->u64HpetOffset);
    uint64_t u64CapPer = RT_MAKE_U64(pThis->u32Capabilities, pThis->fIch9 ? HPET_CLK_PERIOD_ICH9 : HPET_CLK_PERIOD_PIIX);
    pHlp->pfnSSMPutU64(pSSM, u64CapPer);
    pHlp->pfnSSMPutU64(pSSM, pThis->u64HpetConfig);
    pHlp->pfnSSMPutU64(pSSM, pThis->u64Isr);
    return pHlp->pfnSSMPutU64(pSSM, pThis->u64HpetCounter);
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) hpetR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PHPET           pThis   = PDMDEVINS_2_DATA(pDevIns, PHPET);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;

    /*
     * Version checks.
     */
    if (uVersion == HPET_SAVED_STATE_VERSION_EMPTY)
        return VINF_SUCCESS;
    if (   uVersion != HPET_SAVED_STATE_VERSION
        && uVersion != HPET_SAVED_STATE_VERSION_PRE_TIMER)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /*
     * The config.
     */
    uint8_t cTimers;
    int rc = pHlp->pfnSSMGetU8(pSSM, &cTimers);
    AssertRCReturn(rc, rc);
    if (cTimers > RT_ELEMENTS(pThis->aTimers))
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - too many timers: saved=%#x config=%#x"),
                                       cTimers, RT_ELEMENTS(pThis->aTimers));

    if (uPass != SSM_PASS_FINAL)
        return VINF_SUCCESS;

    /*
     * The state.
     */
    for (uint32_t iTimer = 0; iTimer < cTimers; iTimer++)
    {
        PHPETTIMER pHpetTimer = &pThis->aTimers[iTimer];
        PDMDevHlpTimerLoad(pDevIns, pHpetTimer->hTimer, pSSM);
        pHlp->pfnSSMGetU8(pSSM,  &pHpetTimer->u8Wrap);
        pHlp->pfnSSMGetU64(pSSM, &pHpetTimer->u64Config);
        pHlp->pfnSSMGetU64(pSSM, &pHpetTimer->u64Cmp);
        pHlp->pfnSSMGetU64(pSSM, &pHpetTimer->u64Fsb);
        pHlp->pfnSSMGetU64(pSSM, &pHpetTimer->u64Period);
    }

    pHlp->pfnSSMGetU64(pSSM, &pThis->u64HpetOffset);
    uint64_t u64CapPer;
    pHlp->pfnSSMGetU64(pSSM, &u64CapPer);
    pHlp->pfnSSMGetU64(pSSM, &pThis->u64HpetConfig);
    pHlp->pfnSSMGetU64(pSSM, &pThis->u64Isr);
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->u64HpetCounter);
    if (RT_FAILURE(rc))
        return rc;

    /* Older saved state have an off-by-1 timer count bug. */
    uint8_t cCapTimers = HPET_CAP_GET_TIMERS(RT_LO_U32(u64CapPer));
    if (   uVersion <= HPET_SAVED_STATE_VERSION_PRE_TIMER
        && cCapTimers > 0 /* Paranoia */)
        --cCapTimers;

    /* Verify capability reported timer count matches timer count in the saved state field. */
    if (cCapTimers != cTimers)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Capabilities does not match timer count: cTimers=%#x caps=%#x"),
                                       cTimers, cCapTimers);
    if (HPET_CAP_GET_TIMERS(RT_LO_U32(u64CapPer)) > RT_ELEMENTS(pThis->aTimers))
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - too many timers in capability register: CAP=%#x => %u times, max %u"),
                                       RT_LO_U32(u64CapPer), (unsigned)HPET_CAP_GET_TIMERS(RT_LO_U32(u64CapPer)), RT_ELEMENTS(pThis->aTimers));

    pThis->u32Capabilities  = RT_LO_U32(u64CapPer);
    uint32_t const uExpectedPeriod = pThis->fIch9 ? HPET_CLK_PERIOD_ICH9 : HPET_CLK_PERIOD_PIIX;
    if (RT_HI_U32(u64CapPer) != uExpectedPeriod)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - Expected period %RU32 fs, loaded %RU32 fs"),
                                       uExpectedPeriod, RT_HI_U32(u64CapPer));

    /*
     * Set the timer frequency hints.
     */
    rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rc, rc);

    for (uint32_t iTimer = 0; iTimer < cTimers; iTimer++)
    {
        PHPETTIMER pHpetTimer = &pThis->aTimers[iTimer];
        if (PDMDevHlpTimerIsActive(pDevIns, pHpetTimer->hTimer))
            hpetTimerSetFrequencyHint(pDevIns, pThis, pHpetTimer, pHpetTimer->u64Config, pHpetTimer->u64Period);
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}


/* -=-=-=-=-=- PDMDEVREG -=-=-=-=-=- */


/**
 * @interface_method_impl{PDMDEVREG,pfnRelocate}
 */
static DECLCALLBACK(void) hpetR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    PHPETRC pThisRC = PDMINS_2_DATA_RC(pDevIns, PHPETRC);
    LogFlow(("hpetR3Relocate:\n"));

    pThisRC->pHpetHlp += offDelta;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) hpetR3Reset(PPDMDEVINS pDevIns)
{
    PHPET   pThis   = PDMDEVINS_2_DATA(pDevIns, PHPET);
    PHPETCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHPETCC);
    LogFlow(("hpetR3Reset:\n"));

    /*
     * The timers first.
     */
    PDMDevHlpTimerLockClock(pDevIns, pThis->aTimers[0].hTimer, VERR_IGNORED);
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aTimers); i++)
    {
        PHPETTIMER pHpetTimer = &pThis->aTimers[i];
        Assert(pHpetTimer->idxTimer == i);
        PDMDevHlpTimerStop(pDevIns, pHpetTimer->hTimer);

        /* capable of periodic operations and 64-bits */
        uint64_t fConfig;
        if (pThis->fIch9)
            fConfig = i == 0 ? HPET_TN_PERIODIC_CAP | HPET_TN_SIZE_CAP : 0;
        else
            fConfig = HPET_TN_PERIODIC_CAP | HPET_TN_SIZE_CAP;

        /* We can do all IRQs */
        uint32_t u32RoutingCap = 0xffffffff;
        fConfig |= ((uint64_t)u32RoutingCap) << HPET_TN_INT_ROUTE_CAP_SHIFT;
        ASMAtomicWriteU64(&pHpetTimer->u64Config, fConfig);
        pHpetTimer->u64Period  = 0;
        pHpetTimer->u8Wrap     = 0;
        pHpetTimer->u64Cmp     = hpetInvalidValue(pHpetTimer);
    }
    PDMDevHlpTimerUnlockClock(pDevIns, pThis->aTimers[0].hTimer);

    /*
     * The shared HPET state.
     */
    pThis->u64HpetConfig  = 0;
    pThis->u64HpetCounter = 0;
    pThis->u64HpetOffset  = 0;

    /* 64-bit main counter; 3 timers supported; LegacyReplacementRoute. */
    pThis->u32Capabilities = (1 << 15)        /* LEG_RT_CAP       - LegacyReplacementRoute capable. */
                           | (1 << 13)        /* COUNTER_SIZE_CAP - Main counter is 64-bit capable. */
                           | 1;               /* REV_ID           - Revision, must not be 0 */
    if (pThis->fIch9)                         /* NUM_TIM_CAP      - Number of timers -1. */
        pThis->u32Capabilities |= (HPET_NUM_TIMERS_ICH9 - 1) << 8;
    else
        pThis->u32Capabilities |= (HPET_NUM_TIMERS_PIIX - 1) << 8;
    pThis->u32Capabilities |= UINT32_C(0x80860000); /* VENDOR */
    AssertCompile(HPET_NUM_TIMERS_ICH9 <= RT_ELEMENTS(pThis->aTimers));
    AssertCompile(HPET_NUM_TIMERS_PIIX <= RT_ELEMENTS(pThis->aTimers));


    /*
     * Notify the PIT/RTC devices.
     */
    if (pThisCC->pHpetHlp)
        pThisCC->pHpetHlp->pfnSetLegacyMode(pDevIns, false /*fActive*/);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) hpetR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PHPET           pThis   = PDMDEVINS_2_DATA(pDevIns, PHPET);
    PHPETCC         pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHPETCC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;

    /* Only one HPET device now, as we use fixed MMIO region. */
    Assert(iInstance == 0); RT_NOREF(iInstance);

    /*
     * Initialize the device state.
     */

    /* Init the HPET timers (init all regardless of how many we expose). */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aTimers); i++)
    {
        PHPETTIMER pHpetTimer = &pThis->aTimers[i];
        pHpetTimer->idxTimer = i;
        pHpetTimer->hTimer   = NIL_TMTIMERHANDLE;
    }

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "ICH9", "");

    int rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "ICH9", &pThis->fIch9, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: failed to read ICH9 as boolean"));


    /*
     * Create critsect and timers.
     * Note! We don't use the default critical section of the device, but our own.
     */
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSect, RT_SRC_POS, "HPET");
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /* Init the HPET timers (init all regardless of how many we expose). */
    static const char * const s_apszTimerNames[] =
    { "HPET Timer 0", "HPET Timer 1", "HPET Timer 2", "HPET Timer 3" };
    AssertCompile(RT_ELEMENTS(pThis->aTimers) == RT_ELEMENTS(s_apszTimerNames));
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aTimers); i++)
    {
        PHPETTIMER pHpetTimer = &pThis->aTimers[i];
        rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, hpetR3Timer, pHpetTimer,
                                  TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0,
                                  s_apszTimerNames[i], &pThis->aTimers[i].hTimer);
        AssertRCReturn(rc, rc);
        uint64_t const cTicksPerSec = PDMDevHlpTimerGetFreq(pDevIns, pThis->aTimers[i].hTimer);
        if (cTicksPerSec != RT_NS_1SEC)
            return PDMDevHlpVMSetError(pDevIns, VERR_INTERNAL_ERROR_2, RT_SRC_POS,
                                       "Unexpected timer resolution %RU64, code assumes nanonsecond resolution!", cTicksPerSec);
    }

    /*
     * This must be done prior to registering the HPET, right?
     */
    hpetR3Reset(pDevIns);

    uint32_t const fCaps = pThis->u32Capabilities;
    LogRel(("HPET: Capabilities=%#RX32 (LegacyRt=%RTbool CounterSize=%s Timers=%u Revision=%#x)\n",
            fCaps, HPET_CAP_HAS_LEG_RT(fCaps), HPET_CAP_HAS_64BIT_COUNT_SIZE(fCaps) ? "64-bit" : "32-bit",
            HPET_CAP_GET_TIMERS(fCaps), HPET_CAP_GET_REV_ID(fCaps)));

    /*
     * Register the HPET and get helpers.
     */
    PDMHPETREG HpetReg;
    HpetReg.u32Version = PDM_HPETREG_VERSION;
    rc = PDMDevHlpHpetRegister(pDevIns, &HpetReg, &pThisCC->pHpetHlp);
    AssertRCReturn(rc, rc);

    /*
     * Register the MMIO range, PDM API requests page aligned
     * addresses and sizes.
     */
    rc = PDMDevHlpMmioCreateAndMap(pDevIns, HPET_BASE, HPET_BAR_SIZE, hpetMMIOWrite, hpetMMIORead,
                                   IOMMMIO_FLAGS_READ_DWORD_QWORD | IOMMMIO_FLAGS_WRITE_ONLY_DWORD_QWORD,
                                   "HPET Memory", &pThis->hMmio);
    AssertRCReturn(rc, rc);

    /*
     * Register SSM state, info item and statistics.
     */
    rc = PDMDevHlpSSMRegister3(pDevIns, HPET_SAVED_STATE_VERSION, sizeof(*pThis), hpetR3LiveExec, hpetR3SaveExec, hpetR3LoadExec);
    AssertRCReturn(rc, rc);

    PDMDevHlpDBGFInfoRegister(pDevIns, "hpet", "Display HPET status. (no arguments)", hpetR3Info);

    /* Statistics: */
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCounterRead4Byte, STAMTYPE_COUNTER,
                          "ReadCounter32bit", STAMUNIT_OCCURENCES, "HPET_COUNTER 32-bit reads");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCounterRead8Byte, STAMTYPE_COUNTER,
                          "ReadCounter64bit", STAMUNIT_OCCURENCES, "HPET_COUNTER 64-bit reads");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCounterWriteLow,  STAMTYPE_COUNTER,
                          "WriteCounterLow",  STAMUNIT_OCCURENCES, "Low HPET_COUNTER writes");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCounterWriteHigh, STAMTYPE_COUNTER,
                          "WriteCounterHigh", STAMUNIT_OCCURENCES, "High HPET_COUNTER writes");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatZeroDeltaHack, STAMTYPE_COUNTER,
                          "ZeroDeltaHacks", STAMUNIT_OCCURENCES, "High HPET_COUNTER writes");

    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aTimers); i++)
    {
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aTimers[i].StatSetIrq, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                               STAMUNIT_OCCURENCES, "Number of times the IRQ has been set.",  "timer%u/SetIrq", i);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThis->aTimers[i].StatSetTimer, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                               STAMUNIT_OCCURENCES, "Number of times the timer has be programmed.",  "timer%u/SetTimer", i);
    }

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) hpetRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PHPET   pThis   = PDMDEVINS_2_DATA(pDevIns, PHPET);
    PHPETCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PHPETCC);

    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    PDMHPETREG HpetReg;
    HpetReg.u32Version = PDM_HPETREG_VERSION;
    rc = PDMDevHlpHpetSetUpContext(pDevIns, &HpetReg, &pThisCC->pHpetHlp);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, hpetMMIOWrite, hpetMMIORead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceHPET =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "hpet",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_PIT,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(HPET),
    /* .cbInstanceCC = */           sizeof(HPETCC),
    /* .cbInstanceRC = */           sizeof(HPETRC),
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "High Precision Event Timer (HPET) Device",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           hpetR3Construct,
    /* .pfnDestruct = */            NULL,
    /* .pfnRelocate = */            hpetR3Relocate,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               hpetR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
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
    /* .pfnConstruct = */           hpetRZConstruct,
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
    /* .pfnConstruct = */           hpetRZConstruct,
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

