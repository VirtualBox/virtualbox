/* $Id: DevRTC.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Motorola MC146818 RTC/CMOS Device with PIIX4 extensions.
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
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 * QEMU MC146818 RTC emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_RTC
#include <VBox/vmm/pdmdev.h>
#include <VBox/log.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/string.h>

#ifdef IN_RING3
# include <iprt/alloc.h>
# include <iprt/uuid.h>
#endif /* IN_RING3 */

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/*#define DEBUG_CMOS*/
#define RTC_CRC_START   0x10
#define RTC_CRC_LAST    0x2d
#define RTC_CRC_HIGH    0x2e
#define RTC_CRC_LOW     0x2f

#define RTC_SECONDS             0
#define RTC_SECONDS_ALARM       1
#define RTC_MINUTES             2
#define RTC_MINUTES_ALARM       3
#define RTC_HOURS               4
#define RTC_HOURS_ALARM         5
#define RTC_ALARM_DONT_CARE    0xC0

#define RTC_DAY_OF_WEEK         6
#define RTC_DAY_OF_MONTH        7
#define RTC_MONTH               8
#define RTC_YEAR                9

#define RTC_REG_A               10
#define RTC_REG_B               11
#define RTC_REG_C               12
#define RTC_REG_D               13

#define REG_A_UIP 0x80

#define REG_B_SET 0x80
#define REG_B_PIE 0x40
#define REG_B_AIE 0x20
#define REG_B_UIE 0x10

#define REG_C_IRQF  0x80
#define REG_C_PF    0x40
#define REG_C_AF    0x20
#define REG_C_UF    0x10

#define CMOS_BANK_LOWER_LIMIT   0x0E
#define CMOS_BANK_UPPER_LIMIT   0x7F
#define CMOS_BANK2_LOWER_LIMIT  0x80
#define CMOS_BANK2_UPPER_LIMIT  0xFF
#define CMOS_BANK_SIZE          0x80

/** The saved state version. */
#define RTC_SAVED_STATE_VERSION             4
/** The saved state version used by VirtualBox pre-3.2.
 * This does not include the second 128-byte bank. */
#define RTC_SAVED_STATE_VERSION_VBOX_32PRE  3
/** The saved state version used by VirtualBox 3.1 and earlier.
 * This does not include disabled by HPET state. */
#define RTC_SAVED_STATE_VERSION_VBOX_31     2
/** The saved state version used by VirtualBox 3.0 and earlier.
 * This does not include the configuration.  */
#define RTC_SAVED_STATE_VERSION_VBOX_30     1


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** @todo Replace struct my_tm with RTTIME. */
struct my_tm
{
    int32_t tm_sec;
    int32_t tm_min;
    int32_t tm_hour;
    int32_t tm_mday;
    int32_t tm_mon;
    int32_t tm_year;
    int32_t tm_wday;
    int32_t tm_yday;
};


typedef struct RTCSTATE
{
    uint8_t cmos_data[256];
    uint8_t cmos_index[2];
    uint8_t Alignment0[6];
    struct my_tm current_tm;
    /** The configured IRQ. */
    int32_t                 irq;
    /** The configured I/O port base. */
    RTIOPORT                IOPortBase;
    /** Use UTC or local time initially. */
    bool                    fUTC;
    /** Disabled by HPET legacy mode. */
    bool                    fDisabledByHpet;
    /* periodic timer */
    int64_t next_periodic_time;
    /* second update */
    int64_t next_second_time;

    /** The periodic timer (rtcTimerPeriodic). */
    TMTIMERHANDLE           hPeriodicTimer;
    /** The second timer (rtcTimerSecond). */
    TMTIMERHANDLE           hSecondTimer;
    /** The second second timer (rtcTimerSecond2). */
    TMTIMERHANDLE           hSecondTimer2;
    /** The I/O port range handle. */
    IOMIOPORTHANDLE         hIoPorts;

    /** Number of release log entries. Used to prevent flooding. */
    uint32_t                cRelLogEntries;
    /** The current/previous logged timer period. */
    int32_t                 CurLogPeriod;
    /** The current/previous hinted timer period. */
    int32_t                 CurHintPeriod;
    /** How many consecutive times the UIP has been seen. */
    int32_t                 cUipSeen;

    /** Number of IRQs that's been raised. */
    STAMCOUNTER             StatRTCIrq;
    /** Number of times the timer callback handler ran. */
    STAMCOUNTER             StatRTCTimerCB;
    /** Number of times the PIE bit was changed. */
    STAMCOUNTER             StatRTCPieFlip;
    /** Number of times an interrupt was cleared. */
    STAMCOUNTER             StatRTCIrqClear;
    /** How long the periodic interrupt remains active. */
    STAMPROFILEADV          StatPIrqPending;
} RTCSTATE;
/** Pointer to the RTC device state. */
typedef RTCSTATE *PRTCSTATE;


/**
 * RTC ring-3 instance data.
 */
typedef struct RTCSTATER3
{
    /** Pointer to the device instance. */
    PPDMDEVINSR3            pDevInsR3;

    /** The RTC registration structure. */
    PDMRTCREG               RtcReg;
    /** The RTC device helpers. */
    R3PTRTYPE(PCPDMRTCHLP)  pRtcHlpR3;

    /** Pointer to the shared state (for the IHpetLegacyNotify callback). */
    PRTCSTATE               pShared;
    /** HPET legacy mode notification interface. */
    PDMIHPETLEGACYNOTIFY    IHpetLegacyNotify;
} RTCSTATER3;
/** Pointer to the ring-3 RTC device instance data. */
typedef RTCSTATER3 *PRTCSTATER3;


/**
 * RTC ring-0 instance data.
 */
typedef struct RTCSTATER0
{
    uint64_t                uUnused;
} RTCSTATER0;
/** Pointer to the ring-0 RTC device instance data. */
typedef RTCSTATER0 *PRTCSTATER0;


/**
 * RTC raw-mode instance data.
 */
typedef struct RTCSTATERC
{
    uint64_t                uUnused;
} RTCSTATERC;
/** Pointer to the raw-mode RTC device instance data. */
typedef RTCSTATERC *PRTCSTATERC;


/** The instance data for the current context. */
typedef CTX_SUFF(RTCSTATE)  RTCSTATECC;
/** Pointer to the instance data for the current context. */
typedef CTX_SUFF(PRTCSTATE) PRTCSTATECC;


#ifndef VBOX_DEVICE_STRUCT_TESTCASE

static void rtc_timer_update(PPDMDEVINS pDevIns, PRTCSTATE pThis, int64_t current_time)
{
    int period_code, period;
    uint64_t cur_clock, next_irq_clock;
    uint32_t freq;

    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, pThis->hPeriodicTimer));
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));

    period_code = pThis->cmos_data[RTC_REG_A] & 0x0f;
    if (   period_code != 0
        && (pThis->cmos_data[RTC_REG_B] & REG_B_PIE))
    {
        if (period_code <= 2)
            period_code += 7;
        /* period in 32 kHz cycles */
        period = 1 << (period_code - 1);
        /* compute 32 kHz clock */
        freq = PDMDevHlpTimerGetFreq(pDevIns, pThis->hPeriodicTimer);

        cur_clock = ASMMultU64ByU32DivByU32(current_time, 32768, freq);
        next_irq_clock = (cur_clock & ~(uint64_t)(period - 1)) + period;
        pThis->next_periodic_time = ASMMultU64ByU32DivByU32(next_irq_clock, freq, 32768) + 1;
        PDMDevHlpTimerSet(pDevIns, pThis->hPeriodicTimer, pThis->next_periodic_time);

#ifdef IN_RING3
        if (RT_LIKELY(period == pThis->CurLogPeriod))
        { /* likely */ }
        else
        {
            if (pThis->cRelLogEntries++ < 64)
                LogRel(("RTC: period=%#x (%d) %u Hz\n", period, period, _32K / period));
            pThis->CurLogPeriod = period;
        }
#endif
        if (RT_LIKELY(period == pThis->CurHintPeriod))
        { /* likely */ }
        else
        {
            pThis->CurHintPeriod = period;
            PDMDevHlpTimerSetFrequencyHint(pDevIns, pThis->hPeriodicTimer, _32K / period);
        }
    }
    else
    {
#ifdef IN_RING3
        if (PDMDevHlpTimerIsActive(pDevIns, pThis->hPeriodicTimer) && pThis->cRelLogEntries++ < 64)
            LogRel(("RTC: Stopped the periodic timer\n"));
#endif
        PDMDevHlpTimerStop(pDevIns, pThis->hPeriodicTimer);
        pThis->CurHintPeriod = 0;
        pThis->CurLogPeriod = 0;
    }
    RT_NOREF(pDevIns);
}


static void rtc_raise_irq(PPDMDEVINS pDevIns, PRTCSTATE pThis, uint32_t iLevel)
{
    if (!pThis->fDisabledByHpet)
    {
        PDMDevHlpISASetIrq(pDevIns, pThis->irq, iLevel);
        if (iLevel)
            STAM_REL_COUNTER_INC(&pThis->StatRTCIrq);
    }
}


#ifdef IN_RING3
DECLINLINE(int) to_bcd(PRTCSTATE pThis, int a)
{
    if (pThis->cmos_data[RTC_REG_B] & 0x04)
        return a;
    return ((a / 10) << 4) | (a % 10);
}
#endif


DECLINLINE(int) from_bcd(PRTCSTATE pThis, int a)
{
    if (pThis->cmos_data[RTC_REG_B] & 0x04)
        return a;
    return ((a >> 4) * 10) + (a & 0x0f);
}


static void rtc_set_time(PRTCSTATE pThis)
{
    struct my_tm *tm = &pThis->current_tm;

    tm->tm_sec  = from_bcd(pThis, pThis->cmos_data[RTC_SECONDS]);
    tm->tm_min  = from_bcd(pThis, pThis->cmos_data[RTC_MINUTES]);
    tm->tm_hour = from_bcd(pThis, pThis->cmos_data[RTC_HOURS] & 0x7f);
    if (!(pThis->cmos_data[RTC_REG_B] & 0x02))
    {
        tm->tm_hour %= 12;
        if (pThis->cmos_data[RTC_HOURS] & 0x80)
            tm->tm_hour += 12;
    }
    tm->tm_wday = from_bcd(pThis, pThis->cmos_data[RTC_DAY_OF_WEEK]);
    tm->tm_mday = from_bcd(pThis, pThis->cmos_data[RTC_DAY_OF_MONTH]);
    tm->tm_mon  = from_bcd(pThis, pThis->cmos_data[RTC_MONTH]) - 1;
    tm->tm_year = from_bcd(pThis, pThis->cmos_data[RTC_YEAR]) + 100;
}


/* -=-=-=-=-=- I/O Port Handlers -=-=-=-=-=- */


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 */
static DECLCALLBACK(VBOXSTRICTRC) rtcIOPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    NOREF(pvUser);
    Assert(offPort < 4);

    if (cb != 1)
        return VERR_IOM_IOPORT_UNUSED;

    PRTCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);
    if ((offPort & 1) == 0)
        *pu32 = 0xff;
    else
    {
        unsigned bank = (offPort >> 1) & 1;
        switch (pThis->cmos_index[bank])
        {
            case RTC_SECONDS:
            case RTC_MINUTES:
            case RTC_HOURS:
            case RTC_DAY_OF_WEEK:
            case RTC_DAY_OF_MONTH:
            case RTC_MONTH:
            case RTC_YEAR:
                *pu32 = pThis->cmos_data[pThis->cmos_index[0]];
                break;

            case RTC_REG_A:
                if (pThis->cmos_data[RTC_REG_A] & REG_A_UIP)
                    ++pThis->cUipSeen;
                else
                    pThis->cUipSeen = 0;
                if (pThis->cUipSeen >= 250)
                {
                    pThis->cmos_data[pThis->cmos_index[0]] &= ~REG_A_UIP;
                    pThis->cUipSeen = 0;
                }
                *pu32 = pThis->cmos_data[pThis->cmos_index[0]];
                break;

            case RTC_REG_C:
                *pu32 = pThis->cmos_data[pThis->cmos_index[0]];
                if (*pu32)  /* If any bits were set, reading will clear them. */
                {
                    STAM_REL_COUNTER_INC(&pThis->StatRTCIrqClear);
                    if (pThis->cmos_data[RTC_REG_C] & REG_C_PF)
                        STAM_REL_PROFILE_ADV_STOP(&pThis->StatPIrqPending, dummy);
                }
                rtc_raise_irq(pDevIns, pThis, 0);
                pThis->cmos_data[RTC_REG_C] = 0x00;
                break;

            default:
                *pu32 = pThis->cmos_data[pThis->cmos_index[bank]];
                break;
        }

        Log(("CMOS: Read bank %d idx %#04x: %#04x\n", bank, pThis->cmos_index[bank], *pu32));
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC) rtcIOPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    NOREF(pvUser);
    Assert(offPort < 4);

    if (cb != 1)
        return VINF_SUCCESS;

    PRTCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);
    uint32_t bank = (offPort >> 1) & 1;
    if ((offPort & 1) == 0)
    {
        pThis->cmos_index[bank] = (u32 & 0x7f) + (bank * CMOS_BANK_SIZE);

        /* HACK ALERT! Attempt to trigger VM_FF_TIMER and/or VM_FF_TM_VIRTUAL_SYNC
           for forcing the pSecondTimer2 timer to run be run and clear UIP in
           a timely fashion. */
        if (u32 == RTC_REG_A)
            PDMDevHlpTimerGet(pDevIns, pThis->hSecondTimer);
    }
    else
    {
        Log(("CMOS: Write bank %d idx %#04x: %#04x (old %#04x)\n", bank,
             pThis->cmos_index[bank], u32, pThis->cmos_data[pThis->cmos_index[bank]]));

        int const idx = pThis->cmos_index[bank];
        switch (idx)
        {
            case RTC_SECONDS_ALARM:
            case RTC_MINUTES_ALARM:
            case RTC_HOURS_ALARM:
                pThis->cmos_data[pThis->cmos_index[0]] = u32;
                break;

            case RTC_SECONDS:
            case RTC_MINUTES:
            case RTC_HOURS:
            case RTC_DAY_OF_WEEK:
            case RTC_DAY_OF_MONTH:
            case RTC_MONTH:
            case RTC_YEAR:
                pThis->cmos_data[pThis->cmos_index[0]] = u32;
                /* if in set mode, do not update the time */
                if (!(pThis->cmos_data[RTC_REG_B] & REG_B_SET))
                    rtc_set_time(pThis);
                break;

            case RTC_REG_A:
            case RTC_REG_B:
            {
                /* We need to acquire the clock lock, because of lock ordering
                   issues this means having to release the device lock.  Since
                   we're letting IOM do the locking, we must not return without
                   holding the device lock.*/
                PDMDevHlpCritSectLeave(pDevIns, pDevIns->CTX_SUFF(pCritSectRo));
                VBOXSTRICTRC rc1 = PDMDevHlpTimerLockClock2(pDevIns, pThis->hPeriodicTimer, pDevIns->CTX_SUFF(pCritSectRo),
                                                            VINF_SUCCESS /* must get it */);
                AssertRCReturn(VBOXSTRICTRC_VAL(rc1), rc1);

                if (idx == RTC_REG_A)
                {
                    /* UIP bit is read only */
                    pThis->cmos_data[RTC_REG_A] = (u32                        & ~REG_A_UIP)
                                                | (pThis->cmos_data[RTC_REG_A] & REG_A_UIP);
                }
                else
                {
                    if (u32 & REG_B_SET)
                    {
                        /* set mode: reset UIP mode */
                        pThis->cmos_data[RTC_REG_A] &= ~REG_A_UIP;
#if 0 /* This is probably wrong as it breaks changing the time/date in OS/2. */
                        u32 &= ~REG_B_UIE;
#endif
                    }
                    else
                    {
                        /* if disabling set mode, update the time */
                        if (pThis->cmos_data[RTC_REG_B] & REG_B_SET)
                            rtc_set_time(pThis);
                    }

                    if (((uint8_t)u32 & REG_B_PIE) != (pThis->cmos_data[RTC_REG_B] & REG_B_PIE))
                        STAM_REL_COUNTER_INC(&pThis->StatRTCPieFlip);

                    pThis->cmos_data[RTC_REG_B] = u32;
                }

                rtc_timer_update(pDevIns, pThis, PDMDevHlpTimerGet(pDevIns, pThis->hPeriodicTimer));

                PDMDevHlpTimerUnlockClock(pDevIns, pThis->hPeriodicTimer);
                /* the caller leaves the other lock. */
                break;
            }

            case RTC_REG_C:
            case RTC_REG_D:
                /* cannot write to them */
                break;

            default:
                pThis->cmos_data[pThis->cmos_index[bank]] = u32;
                break;
        }
    }

    return VINF_SUCCESS;
}

#ifdef IN_RING3

/* -=-=-=-=-=- Debug Info Handlers  -=-=-=-=-=- */

/**
 * @callback_method_impl{FNDBGFHANDLERDEV,
 *      Dumps the cmos Bank Info.}
 */
static DECLCALLBACK(void) rtcCmosBankInfo(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF1(pszArgs);
    PRTCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);

    pHlp->pfnPrintf(pHlp,
                    "First CMOS bank, offsets 0x0E - 0x7F\n"
                    "Offset %02x : --- use 'info rtc' to show CMOS clock ---", 0);
    for (unsigned iCmos = CMOS_BANK_LOWER_LIMIT; iCmos <= CMOS_BANK_UPPER_LIMIT; iCmos++)
    {
        if ((iCmos & 15) == 0)
            pHlp->pfnPrintf(pHlp, "Offset %02x : %02x", iCmos, pThis->cmos_data[iCmos]);
        else if ((iCmos & 15) == 8)
            pHlp->pfnPrintf(pHlp, "-%02x", pThis->cmos_data[iCmos]);
        else if ((iCmos & 15) == 15)
            pHlp->pfnPrintf(pHlp, " %02x\n", pThis->cmos_data[iCmos]);
        else
            pHlp->pfnPrintf(pHlp, " %02x", pThis->cmos_data[iCmos]);
    }
}

/**
 * @callback_method_impl{FNDBGFHANDLERDEV,
 *      Dumps the cmos Bank2 Info.}
 */
static DECLCALLBACK(void) rtcCmosBank2Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF1(pszArgs);
    PRTCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);

    pHlp->pfnPrintf(pHlp, "Second CMOS bank, offsets 0x80 - 0xFF\n");
    for (uint16_t iCmos = CMOS_BANK2_LOWER_LIMIT; iCmos <= CMOS_BANK2_UPPER_LIMIT; iCmos++)
    {
        if ((iCmos & 15) == 0)
            pHlp->pfnPrintf(pHlp, "Offset %02x : %02x", iCmos, pThis->cmos_data[iCmos]);
        else if ((iCmos & 15) == 8)
            pHlp->pfnPrintf(pHlp, "-%02x", pThis->cmos_data[iCmos]);
        else if ((iCmos & 15) == 15)
            pHlp->pfnPrintf(pHlp, " %02x\n", pThis->cmos_data[iCmos]);
        else
            pHlp->pfnPrintf(pHlp, " %02x", pThis->cmos_data[iCmos]);
    }
}

/**
 * @callback_method_impl{FNDBGFHANDLERDEV,
 *      Dumps the cmos RTC Info.}
 */
static DECLCALLBACK(void) rtcCmosClockInfo(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF1(pszArgs);
    PRTCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);
    uint8_t u8Sec   = from_bcd(pThis, pThis->cmos_data[RTC_SECONDS]);
    uint8_t u8Min   = from_bcd(pThis, pThis->cmos_data[RTC_MINUTES]);
    uint8_t u8Hr    = from_bcd(pThis, pThis->cmos_data[RTC_HOURS] & 0x7f);
    if (   !(pThis->cmos_data[RTC_REG_B] & 0x02)
        && (pThis->cmos_data[RTC_HOURS] & 0x80))
        u8Hr += 12;
    uint8_t u8Day   = from_bcd(pThis, pThis->cmos_data[RTC_DAY_OF_MONTH]);
    uint8_t u8Month = from_bcd(pThis, pThis->cmos_data[RTC_MONTH]) ;
    uint8_t u8Year  = from_bcd(pThis, pThis->cmos_data[RTC_YEAR]);
    pHlp->pfnPrintf(pHlp, "Time: %02u:%02u:%02u  Date: %02u-%02u-%02u\n",
                    u8Hr, u8Min, u8Sec, u8Year, u8Month, u8Day);
    pHlp->pfnPrintf(pHlp, "REG A=%02x B=%02x C=%02x D=%02x\n",
                    pThis->cmos_data[RTC_REG_A], pThis->cmos_data[RTC_REG_B],
                    pThis->cmos_data[RTC_REG_C], pThis->cmos_data[RTC_REG_D]);

    if (pThis->cmos_data[RTC_REG_B] & REG_B_PIE)
    {
        if (pThis->CurHintPeriod)
            pHlp->pfnPrintf(pHlp, "Periodic Interrupt Enabled: %d Hz\n", _32K / pThis->CurHintPeriod);
    }
}



/* -=-=-=-=-=- Timers and their support code  -=-=-=-=-=- */


/**
 * @callback_method_impl{FNTMTIMERDEV, periodic}
 */
static DECLCALLBACK(void) rtcTimerPeriodic(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PRTCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);
    Assert(hTimer == pThis->hPeriodicTimer);
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, hTimer));
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));
    RT_NOREF2(hTimer, pvUser);

    rtc_timer_update(pDevIns, pThis, pThis->next_periodic_time);
    STAM_REL_COUNTER_INC(&pThis->StatRTCTimerCB);

    if (!(pThis->cmos_data[RTC_REG_C] & REG_C_PF))
        STAM_REL_PROFILE_ADV_START(&pThis->StatPIrqPending, dummy);

    pThis->cmos_data[RTC_REG_C] |= REG_C_IRQF | REG_C_PF;

    rtc_raise_irq(pDevIns, pThis, 1);
}


/* month is between 0 and 11. */
static int get_days_in_month(int month, int year)
{
    static const int days_tab[12] =
    {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    int d;

    if ((unsigned )month >= 12)
        return 31;

    d = days_tab[month];
    if (month == 1)
    {
        if ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0))
            d++;
    }
    return d;
}


/* update 'tm' to the next second */
static void rtc_next_second(struct my_tm *tm)
{
    int days_in_month;

    tm->tm_sec++;
    if ((unsigned)tm->tm_sec >= 60)
    {
        tm->tm_sec = 0;
        tm->tm_min++;
        if ((unsigned)tm->tm_min >= 60)
        {
            tm->tm_min = 0;
            tm->tm_hour++;
            if ((unsigned)tm->tm_hour >= 24)
            {
                tm->tm_hour = 0;
                /* next day */
                tm->tm_wday++;
                if ((unsigned)tm->tm_wday >= 7)
                    tm->tm_wday = 0;
                days_in_month = get_days_in_month(tm->tm_mon,
                                                  tm->tm_year + 1900);
                tm->tm_mday++;
                if (tm->tm_mday < 1)
                    tm->tm_mday = 1;
                else if (tm->tm_mday > days_in_month)
                {
                    tm->tm_mday = 1;
                    tm->tm_mon++;
                    if (tm->tm_mon >= 12)
                    {
                        tm->tm_mon = 0;
                        tm->tm_year++;
                    }
                }
            }
        }
    }
}


/**
 * @callback_method_impl{FNTMTIMERDEV, Second timer.}
 */
static DECLCALLBACK(void) rtcR3TimerSecond(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PRTCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);

    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, pThis->hPeriodicTimer));
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));
    RT_NOREF(pvUser, hTimer);

    /* if the oscillator is not in normal operation, we do not update */
    if ((pThis->cmos_data[RTC_REG_A] & 0x70) != 0x20)
    {
        pThis->next_second_time += PDMDevHlpTimerGetFreq(pDevIns, pThis->hSecondTimer);
        PDMDevHlpTimerSet(pDevIns, pThis->hSecondTimer, pThis->next_second_time);
    }
    else
    {
        rtc_next_second(&pThis->current_tm);

        if (!(pThis->cmos_data[RTC_REG_B] & REG_B_SET))
        {
            /* update in progress bit */
            Log2(("RTC: UIP %x -> 1\n", !!(pThis->cmos_data[RTC_REG_A] & REG_A_UIP)));
            pThis->cmos_data[RTC_REG_A] |= REG_A_UIP;
        }

        /* 244140 ns = 8 / 32768 seconds */
        uint64_t delay = PDMDevHlpTimerFromNano(pDevIns, pThis->hSecondTimer2, 244140);
        PDMDevHlpTimerSet(pDevIns, pThis->hSecondTimer2, pThis->next_second_time + delay);
    }
}


/* Used by rtc_set_date and rtcR3TimerSecond2. */
static void rtc_copy_date(PRTCSTATE pThis)
{
    const struct my_tm *tm = &pThis->current_tm;

    pThis->cmos_data[RTC_SECONDS] = to_bcd(pThis, tm->tm_sec);
    pThis->cmos_data[RTC_MINUTES] = to_bcd(pThis, tm->tm_min);
    if (pThis->cmos_data[RTC_REG_B] & 0x02)
    {
        /* 24 hour format */
        pThis->cmos_data[RTC_HOURS] = to_bcd(pThis, tm->tm_hour);
    }
    else
    {
        /* 12 hour format */
        int h = tm->tm_hour % 12;
        pThis->cmos_data[RTC_HOURS] = to_bcd(pThis, h ? h : 12);
        if (tm->tm_hour >= 12)
            pThis->cmos_data[RTC_HOURS] |= 0x80;
    }
    pThis->cmos_data[RTC_DAY_OF_WEEK] = to_bcd(pThis, tm->tm_wday);
    pThis->cmos_data[RTC_DAY_OF_MONTH] = to_bcd(pThis, tm->tm_mday);
    pThis->cmos_data[RTC_MONTH] = to_bcd(pThis, tm->tm_mon + 1);
    pThis->cmos_data[RTC_YEAR] = to_bcd(pThis, tm->tm_year % 100);
}


/**
 * @callback_method_impl{FNTMTIMERDEV, Second2 timer.}
 */
static DECLCALLBACK(void) rtcR3TimerSecond2(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PRTCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);

    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, pThis->hPeriodicTimer));
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));
    RT_NOREF2(hTimer, pvUser);

    if (!(pThis->cmos_data[RTC_REG_B] & REG_B_SET))
        rtc_copy_date(pThis);

    /* check alarm */
    if (pThis->cmos_data[RTC_REG_B] & REG_B_AIE)
    {
        if (   (   (pThis->cmos_data[RTC_SECONDS_ALARM] & 0xc0) == 0xc0
                || from_bcd(pThis, pThis->cmos_data[RTC_SECONDS_ALARM]) == pThis->current_tm.tm_sec)
            && (   (pThis->cmos_data[RTC_MINUTES_ALARM] & 0xc0) == 0xc0
                || from_bcd(pThis, pThis->cmos_data[RTC_MINUTES_ALARM]) == pThis->current_tm.tm_min)
            && (   (pThis->cmos_data[RTC_HOURS_ALARM  ] & 0xc0) == 0xc0
                || from_bcd(pThis, pThis->cmos_data[RTC_HOURS_ALARM  ]) == pThis->current_tm.tm_hour)
            )
        {
            pThis->cmos_data[RTC_REG_C] |= REG_C_IRQF | REG_C_AF;
            rtc_raise_irq(pDevIns, pThis, 1);
        }
    }

    /* update ended interrupt */
    if (pThis->cmos_data[RTC_REG_B] & REG_B_UIE)
    {
        pThis->cmos_data[RTC_REG_C] |= REG_C_IRQF | REG_C_UF;
        rtc_raise_irq(pDevIns, pThis, 1);
    }

    /* clear update in progress bit */
    Log2(("RTC: UIP %x -> 0\n", !!(pThis->cmos_data[RTC_REG_A] & REG_A_UIP)));
    pThis->cmos_data[RTC_REG_A] &= ~REG_A_UIP;

    pThis->next_second_time += PDMDevHlpTimerGetFreq(pDevIns, pThis->hSecondTimer);
    PDMDevHlpTimerSet(pDevIns, pThis->hSecondTimer, pThis->next_second_time);
}


/* -=-=-=-=-=- Saved State -=-=-=-=-=- */


/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC}
 */
static DECLCALLBACK(int) rtcLiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    RT_NOREF1(uPass);
    PCPDMDEVHLPR3 pHlp  = pDevIns->pHlpR3;
    PRTCSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);

    pHlp->pfnSSMPutU8(    pSSM, pThis->irq);
    pHlp->pfnSSMPutIOPort(pSSM, pThis->IOPortBase);
    pHlp->pfnSSMPutBool(  pSSM, pThis->fUTC);

    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) rtcSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PCPDMDEVHLPR3 pHlp  = pDevIns->pHlpR3;
    PRTCSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);

    /* The config. */
    rtcLiveExec(pDevIns, pSSM, SSM_PASS_FINAL);

    /* The state. */
    pHlp->pfnSSMPutMem(pSSM, pThis->cmos_data, CMOS_BANK_SIZE);
    pHlp->pfnSSMPutU8(pSSM, pThis->cmos_index[0]);

    pHlp->pfnSSMPutS32(pSSM, pThis->current_tm.tm_sec);
    pHlp->pfnSSMPutS32(pSSM, pThis->current_tm.tm_min);
    pHlp->pfnSSMPutS32(pSSM, pThis->current_tm.tm_hour);
    pHlp->pfnSSMPutS32(pSSM, pThis->current_tm.tm_wday);
    pHlp->pfnSSMPutS32(pSSM, pThis->current_tm.tm_mday);
    pHlp->pfnSSMPutS32(pSSM, pThis->current_tm.tm_mon);
    pHlp->pfnSSMPutS32(pSSM, pThis->current_tm.tm_year);

    PDMDevHlpTimerSave(pDevIns, pThis->hPeriodicTimer, pSSM);

    pHlp->pfnSSMPutS64(pSSM, pThis->next_periodic_time);

    pHlp->pfnSSMPutS64(pSSM, pThis->next_second_time);
    PDMDevHlpTimerSave(pDevIns, pThis->hSecondTimer, pSSM);
    PDMDevHlpTimerSave(pDevIns, pThis->hSecondTimer2, pSSM);

    pHlp->pfnSSMPutBool(pSSM, pThis->fDisabledByHpet);

    pHlp->pfnSSMPutMem(pSSM, &pThis->cmos_data[CMOS_BANK_SIZE], CMOS_BANK_SIZE);
    return pHlp->pfnSSMPutU8(pSSM, pThis->cmos_index[1]);
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) rtcLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PCPDMDEVHLPR3 pHlp  = pDevIns->pHlpR3;
    PRTCSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);
    int           rc;

    if (    uVersion != RTC_SAVED_STATE_VERSION
        &&  uVersion != RTC_SAVED_STATE_VERSION_VBOX_32PRE
        &&  uVersion != RTC_SAVED_STATE_VERSION_VBOX_31
        &&  uVersion != RTC_SAVED_STATE_VERSION_VBOX_30)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* The config. */
    if (uVersion > RTC_SAVED_STATE_VERSION_VBOX_30)
    {
        uint8_t u8Irq;
        rc = pHlp->pfnSSMGetU8(pSSM, &u8Irq);
        AssertRCReturn(rc, rc);
        if (u8Irq != pThis->irq)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - u8Irq: saved=%#x config=%#x"), u8Irq, pThis->irq);

        RTIOPORT IOPortBase;
        rc = pHlp->pfnSSMGetIOPort(pSSM, &IOPortBase);
        AssertRCReturn(rc, rc);
        if (IOPortBase != pThis->IOPortBase)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - IOPortBase: saved=%RTiop config=%RTiop"), IOPortBase, pThis->IOPortBase);

        bool fUTC;
        rc = pHlp->pfnSSMGetBool(pSSM, &fUTC);
        AssertRCReturn(rc, rc);
        if (fUTC != pThis->fUTC)
            LogRel(("RTC: Config mismatch - fUTC: saved=%RTbool config=%RTbool\n", fUTC, pThis->fUTC));
    }

    if (uPass != SSM_PASS_FINAL)
        return VINF_SUCCESS;

    /* The state. */
    pHlp->pfnSSMGetMem(pSSM, pThis->cmos_data, CMOS_BANK_SIZE);
    pHlp->pfnSSMGetU8(pSSM, &pThis->cmos_index[0]);

    pHlp->pfnSSMGetS32(pSSM, &pThis->current_tm.tm_sec);
    pHlp->pfnSSMGetS32(pSSM, &pThis->current_tm.tm_min);
    pHlp->pfnSSMGetS32(pSSM, &pThis->current_tm.tm_hour);
    pHlp->pfnSSMGetS32(pSSM, &pThis->current_tm.tm_wday);
    pHlp->pfnSSMGetS32(pSSM, &pThis->current_tm.tm_mday);
    pHlp->pfnSSMGetS32(pSSM, &pThis->current_tm.tm_mon);
    pHlp->pfnSSMGetS32(pSSM, &pThis->current_tm.tm_year);

    PDMDevHlpTimerLoad(pDevIns, pThis->hPeriodicTimer, pSSM);

    pHlp->pfnSSMGetS64(pSSM, &pThis->next_periodic_time);

    pHlp->pfnSSMGetS64(pSSM, &pThis->next_second_time);
    PDMDevHlpTimerLoad(pDevIns, pThis->hSecondTimer, pSSM);
    rc = PDMDevHlpTimerLoad(pDevIns, pThis->hSecondTimer2, pSSM);
    AssertRCReturn(rc, rc);

    if (uVersion > RTC_SAVED_STATE_VERSION_VBOX_31)
    {
        rc = pHlp->pfnSSMGetBool(pSSM, &pThis->fDisabledByHpet);
        AssertRCReturn(rc, rc);
    }

    if (uVersion > RTC_SAVED_STATE_VERSION_VBOX_32PRE)
    {
        /* Second CMOS bank. */
        pHlp->pfnSSMGetMem(pSSM, &pThis->cmos_data[CMOS_BANK_SIZE], CMOS_BANK_SIZE);
        rc = pHlp->pfnSSMGetU8(pSSM, &pThis->cmos_index[1]);
        AssertRCReturn(rc, rc);
    }

    int period_code = pThis->cmos_data[RTC_REG_A] & 0x0f;
    if (    period_code != 0
        &&  (pThis->cmos_data[RTC_REG_B] & REG_B_PIE))
    {
        if (period_code <= 2)
            period_code += 7;
        int period = 1 << (period_code - 1);
        LogRel(("RTC: period=%#x (%d) %u Hz (restore)\n", period, period, _32K / period));
        rc = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VINF_SUCCESS);
        AssertRCReturn(rc, rc);
        PDMDevHlpTimerSetFrequencyHint(pDevIns, pThis->hPeriodicTimer, _32K / period);
        PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
        pThis->CurLogPeriod  = period;
        pThis->CurHintPeriod = period;
    }
    else
    {
        LogRel(("RTC: Stopped the periodic timer (restore)\n"));
        pThis->CurLogPeriod  = 0;
        pThis->CurHintPeriod = 0;
    }
    pThis->cRelLogEntries = 0;

    return VINF_SUCCESS;
}


/* -=-=-=-=-=- PDM Interface provided by the RTC device  -=-=-=-=-=- */

/**
 * Calculate and update the standard CMOS checksum.
 *
 * @param   pThis       Pointer to the RTC state data.
 */
static void rtcCalcCRC(PRTCSTATE pThis)
{
    uint16_t u16 = 0;
    for (unsigned i = RTC_CRC_START; i <= RTC_CRC_LAST; i++)
        u16 += pThis->cmos_data[i];

    pThis->cmos_data[RTC_CRC_LOW]  = u16 & 0xff;
    pThis->cmos_data[RTC_CRC_HIGH] = (u16 >> 8) & 0xff;
}


/**
 * @interface_method_impl{PDMRTCREG,pfnWrite}
 */
static DECLCALLBACK(int) rtcCMOSWrite(PPDMDEVINS pDevIns, unsigned iReg, uint8_t u8Value)
{
    PRTCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->pCritSectRoR3));
    if (iReg < RT_ELEMENTS(pThis->cmos_data))
    {
        pThis->cmos_data[iReg] = u8Value;

        /* does it require checksum update? */
        if (    iReg >= RTC_CRC_START
            &&  iReg <= RTC_CRC_LAST)
            rtcCalcCRC(pThis);

        return VINF_SUCCESS;
    }

    AssertMsgFailed(("iReg=%d\n", iReg));
    return VERR_INVALID_PARAMETER;
}


/**
 * @interface_method_impl{PDMRTCREG,pfnRead}
 */
static DECLCALLBACK(int) rtcCMOSRead(PPDMDEVINS pDevIns, unsigned iReg, uint8_t *pu8Value)
{
    PRTCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->pCritSectRoR3));

    if (iReg < RT_ELEMENTS(pThis->cmos_data))
    {
        *pu8Value = pThis->cmos_data[iReg];
        return VINF_SUCCESS;
    }
    AssertMsgFailed(("iReg=%d\n", iReg));
    return VERR_INVALID_PARAMETER;
}


/**
 * @interface_method_impl{PDMIHPETLEGACYNOTIFY,pfnModeChanged}
 */
static DECLCALLBACK(void) rtcHpetLegacyNotify_ModeChanged(PPDMIHPETLEGACYNOTIFY pInterface, bool fActivated)
{
    PRTCSTATECC pThisCC = RT_FROM_MEMBER(pInterface, RTCSTATER3, IHpetLegacyNotify);
    PPDMDEVINS  pDevIns = pThisCC->pDevInsR3;
    int const   rcLock  = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    pThisCC->pShared->fDisabledByHpet = fActivated;

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
}



/* -=-=-=-=-=- IBase -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) rtcQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDEVINS  pDevIns = RT_FROM_MEMBER(pInterface, PDMDEVINS, IBase);
    PRTCSTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PRTCSTATECC);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE,             &pDevIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHPETLEGACYNOTIFY, &pThisCC->IHpetLegacyNotify);
    return NULL;
}


/* -=-=-=-=-=- PDMDEVREG -=-=-=-=-=- */

static void rtc_set_memory(PRTCSTATE pThis, int addr, int val)
{
    if (addr >= 0 && addr <= 127)
        pThis->cmos_data[addr] = val;
}


static void rtc_set_date(PRTCSTATE pThis, const struct my_tm *tm)
{
    pThis->current_tm = *tm;
    rtc_copy_date(pThis);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnInitComplete}
 *
 * Used to set the clock.
 */
static DECLCALLBACK(int)  rtcInitComplete(PPDMDEVINS pDevIns)
{
    /** @todo this should be (re)done at power on if we didn't load a state... */
    PRTCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);

    /*
     * Set the CMOS date/time.
     */
    RTTIMESPEC  Now;
    PDMDevHlpTMUtcNow(pDevIns, &Now);
    RTTIME Time;
    if (pThis->fUTC)
        RTTimeExplode(&Time, &Now);
    else
        RTTimeLocalExplode(&Time, &Now);

    struct my_tm Tm;
    memset(&Tm, 0, sizeof(Tm));
    Tm.tm_year = Time.i32Year - 1900;
    Tm.tm_mon  = Time.u8Month - 1;
    Tm.tm_mday = Time.u8MonthDay;
    Tm.tm_wday = (Time.u8WeekDay + 1 + 7) % 7; /* 0 = Monday -> Sunday */
    Tm.tm_yday = Time.u16YearDay - 1;
    Tm.tm_hour = Time.u8Hour;
    Tm.tm_min  = Time.u8Minute;
    Tm.tm_sec  = Time.u8Second;

    rtc_set_date(pThis, &Tm);

    int iYear = to_bcd(pThis, (Tm.tm_year / 100) + 19); /* tm_year is 1900 based */
    rtc_set_memory(pThis, 0x32, iYear);                 /* 32h - Century Byte (BCD value for the century */
    rtc_set_memory(pThis, 0x37, iYear);                 /* 37h - (IBM PS/2) Date Century Byte */

    /*
     * Recalculate the checksum just in case.
     */
    rtcCalcCRC(pThis);

    Log(("CMOS bank 0: \n%16.128Rhxd\n", &pThis->cmos_data[0]));
    Log(("CMOS bank 1: \n%16.128Rhxd\n", &pThis->cmos_data[CMOS_BANK_SIZE]));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) rtcReset(PPDMDEVINS pDevIns)
{
    PRTCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);

    /* Reset index values (important for second bank). */
    pThis->cmos_index[0] = 0;
    pThis->cmos_index[1] = CMOS_BANK_SIZE;   /* Point to start of second bank. */
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int)  rtcConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;
    PRTCSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);
    PRTCSTATECC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PRTCSTATECC);
    int           rc;
    Assert(iInstance == 0); RT_NOREF(iInstance);

    /*
     * Validate configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "Irq|Base|UseUTC", "");

    /*
     * Init the data.
     */
    uint8_t u8Irq;
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "Irq", &u8Irq, 8);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"Irq\" as a uint8_t failed"));
    pThis->irq = u8Irq;

    rc = pHlp->pfnCFGMQueryPortDef(pCfg, "Base", &pThis->IOPortBase, 0x70);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"Base\" as a RTIOPORT failed"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "UseUTC", &pThis->fUTC, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"UseUTC\" as a bool failed"));

    Log(("RTC: Irq=%#x Base=%#x fR0Enabled=%RTbool fRCEnabled=%RTbool\n",
         u8Irq, pThis->IOPortBase, pDevIns->fR0Enabled, pDevIns->fRCEnabled));


    pThis->cmos_data[RTC_REG_A] = 0x26;
    pThis->cmos_data[RTC_REG_B] = 0x02;
    pThis->cmos_data[RTC_REG_C] = 0x00;
    pThis->cmos_data[RTC_REG_D] = 0x80;
    pThis->fDisabledByHpet      = false;
    pThis->cmos_index[1]        = CMOS_BANK_SIZE;   /* Point to start of second bank. */

    pThisCC->pDevInsR3                          = pDevIns;
    pThisCC->RtcReg.u32Version                  = PDM_RTCREG_VERSION;
    pThisCC->RtcReg.pfnRead                     = rtcCMOSRead;
    pThisCC->RtcReg.pfnWrite                    = rtcCMOSWrite;
    pThisCC->IHpetLegacyNotify.pfnModeChanged   = rtcHpetLegacyNotify_ModeChanged;

    /* IBase */
    pDevIns->IBase.pfnQueryInterface            = rtcQueryInterface;

    /*
     * Create timers.
     */
    /* Periodic timer. */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, rtcTimerPeriodic, pThis,
                              TMTIMER_FLAGS_DEFAULT_CRIT_SECT | TMTIMER_FLAGS_RING0,
                              "MC146818 RTC Periodic", &pThis->hPeriodicTimer);
    AssertRCReturn(rc, rc);

    /* Seconds timer. */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, rtcR3TimerSecond, pThis,
                              TMTIMER_FLAGS_DEFAULT_CRIT_SECT | TMTIMER_FLAGS_RING0,
                              "MC146818 RTC Second", &pThis->hSecondTimer);
    AssertRCReturn(rc, rc);

    /* The second2 timer, this is always active. */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, rtcR3TimerSecond2, pThis,
                              TMTIMER_FLAGS_DEFAULT_CRIT_SECT | TMTIMER_FLAGS_NO_RING0,
                              "MC146818 RTC Second2", &pThis->hSecondTimer2);
    AssertRCReturn(rc, rc);

    pThis->next_second_time = PDMDevHlpTimerGet(pDevIns, pThis->hSecondTimer2)
                            + (PDMDevHlpTimerGetFreq(pDevIns, pThis->hSecondTimer2) * 99) / 100;
    PDMDevHlpTimerLockClock(pDevIns, pThis->hSecondTimer2, VERR_IGNORED);
    rc = PDMDevHlpTimerSet(pDevIns, pThis->hSecondTimer2, pThis->next_second_time);
    PDMDevHlpTimerUnlockClock(pDevIns, pThis->hSecondTimer2);
    AssertRCReturn(rc, rc);

    /*
     * Register I/O ports.
     */
    static const IOMIOPORTDESC g_aIoPortDescs[] =
    {
        { NULL, "ADDR - CMOS Bank #1", NULL, NULL },
        { "DATA - CMOS Bank #1", "DATA - CMOS Bank #1", NULL, NULL },
        { NULL, "ADDR - CMOS Bank #2", NULL, NULL },
        { "DATA - CMOS Bank #2", "DATA - CMOS Bank #2", NULL, NULL },
        { NULL, NULL, NULL, NULL }
    };
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, pThis->IOPortBase, 4, rtcIOPortWrite, rtcIOPortRead,
                                     "MC146818 RTC/CMOS", g_aIoPortDescs, &pThis->hIoPorts);
    AssertRCReturn(rc, rc);

    /*
     * Register the saved state.
     */
    rc = PDMDevHlpSSMRegister3(pDevIns, RTC_SAVED_STATE_VERSION, sizeof(*pThis), rtcLiveExec, rtcSaveExec, rtcLoadExec);
    AssertRCReturn(rc, rc);

    /*
     * Register ourselves as the RTC/CMOS with PDM.
     */
    rc = PDMDevHlpRTCRegister(pDevIns, &pThisCC->RtcReg, &pThisCC->pRtcHlpR3);
    AssertRCReturn(rc, rc);

    /*
     * Register debugger info callback.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "cmos1", "Display CMOS Bank 1 Info (0x0e-0x7f). No arguments. See also rtc.", rtcCmosBankInfo);
    PDMDevHlpDBGFInfoRegister(pDevIns, "cmos2", "Display CMOS Bank 2 Info (0x0e-0x7f). No arguments.", rtcCmosBank2Info);
    PDMDevHlpDBGFInfoRegister(pDevIns, "rtc",   "Display CMOS RTC (0x00-0x0d). No arguments. See also cmos1 & cmos2", rtcCmosClockInfo);

    /*
     * Register statistics.
     */
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRTCIrq,      STAMTYPE_COUNTER, "Irq",      STAMUNIT_OCCURENCES,      "The number of times a RTC interrupt was triggered.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRTCTimerCB,  STAMTYPE_COUNTER, "TimerCB",  STAMUNIT_OCCURENCES,      "The number of times the RTC timer callback ran.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRTCPieFlip,  STAMTYPE_COUNTER, "PieFlip",  STAMUNIT_OCCURENCES,      "The number of times Periodic Interrupt Enable changed.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRTCIrqClear, STAMTYPE_COUNTER, "IrqClear", STAMUNIT_OCCURENCES,      "The number of times an active interrupt was cleared.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatPIrqPending, STAMTYPE_PROFILE, "PiActive", STAMUNIT_TICKS_PER_CALL,  "How long periodic interrupt stays active (pending).");

    return VINF_SUCCESS;
}

#else /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int)  rtcRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PRTCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PRTCSTATE);

    int rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPorts, rtcIOPortWrite, rtcIOPortRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceMC146818 =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "mc146818",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_RTC,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         1,
    /* .cbInstanceShared = */       sizeof(RTCSTATE),
    /* .cbInstanceCC = */           sizeof(RTCSTATECC),
    /* .cbInstanceRC = */           sizeof(RTCSTATERC),
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Motorola MC146818 RTC/CMOS Device.",
#ifdef IN_RING3
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           rtcConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               rtcReset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        rtcInitComplete,
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
    /* .pfnConstruct = */           rtcRZConstruct,
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
    /* .pfnConstruct = */           rtcRZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not IN_RING3, IN_RING0 nor IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */
