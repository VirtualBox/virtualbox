/* $Id: DevPit-i8254.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevPIT-i8254 - Intel 8254 Programmable Interval Timer (PIT) And Dummy Speaker Device.
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
 * QEMU 8253/8254 interval timer emulation
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
#define LOG_GROUP LOG_GROUP_DEV_PIT
#include <VBox/vmm/pdmdev.h>
#include <VBox/log.h>
#include <VBox/vmm/stam.h>
#include <iprt/assert.h>
#include <iprt/asm-math.h>

#ifdef IN_RING3
# ifdef RT_OS_LINUX
#  include <fcntl.h>
#  include <errno.h>
#  include <unistd.h>
#  include <stdio.h>
#  include <linux/kd.h>
#  include <linux/input.h>
#  include <sys/ioctl.h>
# endif
# include <iprt/alloc.h>
# include <iprt/string.h>
# include <iprt/uuid.h>
#endif /* IN_RING3 */

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The PIT frequency. */
#define PIT_FREQ 1193182

#define RW_STATE_LSB 1
#define RW_STATE_MSB 2
#define RW_STATE_WORD0 3
#define RW_STATE_WORD1 4

/** The current saved state version. */
#define PIT_SAVED_STATE_VERSION             4
/** The saved state version used by VirtualBox 3.1 and earlier.
 * This did not include disable by HPET flag. */
#define PIT_SAVED_STATE_VERSION_VBOX_31     3
/** The saved state version used by VirtualBox 3.0 and earlier.
 * This did not include the config part. */
#define PIT_SAVED_STATE_VERSION_VBOX_30     2

/** @def FAKE_REFRESH_CLOCK
 * Define this to flip the 15usec refresh bit on every read.
 * If not defined, it will be flipped correctly. */
/* #define FAKE_REFRESH_CLOCK */
#ifdef DOXYGEN_RUNNING
# define FAKE_REFRESH_CLOCK
#endif

/** The effective counter mode - if bit 1 is set, bit 2 is ignored. */
#define EFFECTIVE_MODE(x)   ((x) & ~(((x) & 2) << 1))


/**
 * Acquires the PIT lock or returns.
 */
#define DEVPIT_LOCK_RETURN(a_pDevIns, a_pThis, a_rcBusy)  \
    do { \
        int const rcLock = PDMDevHlpCritSectEnter((a_pDevIns), &(a_pThis)->CritSect, (a_rcBusy)); \
        if (rcLock == VINF_SUCCESS) { /* likely */ } \
        else return rcLock; \
    } while (0)

/**
 * Releases the PIT lock.
 */
#define DEVPIT_UNLOCK(a_pDevIns, a_pThis) \
    do { PDMDevHlpCritSectLeave((a_pDevIns), &(a_pThis)->CritSect); } while (0)


/**
 * Acquires the TM lock and PIT lock, returns on failure.
 */
#define DEVPIT_LOCK_BOTH_RETURN(a_pDevIns, a_pThis, a_rcBusy)  \
    do { \
        VBOXSTRICTRC rcLock = PDMDevHlpTimerLockClock2((a_pDevIns), (a_pThis)->channels[0].hTimer, \
                                                       &(a_pThis)->CritSect, (a_rcBusy)); \
        if (RT_LIKELY(rcLock == VINF_SUCCESS)) \
        { /* likely */ } \
        else \
            return rcLock; \
    } while (0)

#ifdef IN_RING3
/**
 * Acquires the TM lock and PIT lock, ignores failures.
 */
# define DEVPIT_R3_LOCK_BOTH(a_pDevIns, a_pThis) \
    PDMDevHlpTimerLockClock2((a_pDevIns), (a_pThis)->channels[0].hTimer, &(a_pThis)->CritSect, VERR_IGNORED)
#endif /* IN_RING3 */

/**
 * Releases the PIT lock and TM lock.
 */
#define DEVPIT_UNLOCK_BOTH(a_pDevIns, a_pThis) \
    PDMDevHlpTimerUnlockClock2((a_pDevIns), (a_pThis)->channels[0].hTimer, &(a_pThis)->CritSect)



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The state of one PIT channel.
 */
typedef struct PITCHANNEL
{
    /** The timer.
     * @note Only channel 0 has a timer.  */
    TMTIMERHANDLE                   hTimer;
    /** The virtual time stamp at the last reload (only used in mode 2 for now). */
    uint64_t                        u64ReloadTS;
    /** The actual time of the next tick.
     * As apposed to the next_transition_time which contains the correct time of the next tick. */
    uint64_t                        u64NextTS;

    /** (count_load_time is only set by PDMDevHlpTimerGet() which returns uint64_t) */
    uint64_t count_load_time;
    /* irq handling */
    int64_t next_transition_time;
    int32_t irq;
    /** Number of release log entries. Used to prevent flooding. */
    uint8_t cRelLogEntries;
    /** The channel number. */
    uint8_t iChan;
    uint8_t abAlignment[2];

    uint32_t count; /* can be 65536 */
    uint16_t latched_count;
    uint8_t count_latched;
    uint8_t status_latched;

    uint8_t status;
    uint8_t read_state;
    uint8_t write_state;
    uint8_t write_latch;

    uint8_t rw_mode;
    uint8_t mode;
    uint8_t bcd; /* not supported */
    uint8_t gate; /* timer start */

} PITCHANNEL;
/** Pointer to the state of one PIT channel. */
typedef PITCHANNEL *PPITCHANNEL;

/** Speaker emulation state. */
typedef enum PITSPEAKEREMU
{
    PIT_SPEAKER_EMU_NONE = 0,
    PIT_SPEAKER_EMU_CONSOLE,
    PIT_SPEAKER_EMU_EVDEV,
    PIT_SPEAKER_EMU_TTY
} PITSPEAKEREMU;

/**
 * The shared PIT state.
 */
typedef struct PITSTATE
{
    /** Channel state. Must come first? */
    PITCHANNEL              channels[3];
    /** Speaker data. */
    int32_t                 speaker_data_on;
#ifdef FAKE_REFRESH_CLOCK
    /** Refresh dummy. */
    int32_t                 dummy_refresh_clock;
#else
    uint32_t                Alignment1;
#endif
    /** Config: I/O port base. */
    RTIOPORT                IOPortBaseCfg;
    /** Disconnect PIT from the interrupt controllers if requested by HPET. */
    bool                    fDisabledByHpet;
    /** Number of IRQs that's been raised. */
    STAMCOUNTER             StatPITIrq;
    /** Profiling the timer callback handler. */
    STAMPROFILEADV          StatPITHandler;
    /** Critical section protecting the state. */
    PDMCRITSECT             CritSect;
    /** The primary I/O port range (0x40-0x43). */
    IOMIOPORTHANDLE         hIoPorts;
    /** The speaker I/O port range (0x40-0x43). */
    IOMIOPORTHANDLE         hIoPortSpeaker;
} PITSTATE;
/** Pointer to the shared PIT device state. */
typedef PITSTATE *PPITSTATE;


/**
 * The ring-3 PIT state.
 */
typedef struct PITSTATER3
{
    /** PIT port interface. */
    PDMIHPETLEGACYNOTIFY    IHpetLegacyNotify;
    /** Pointer to the device instance. */
    PPDMDEVINSR3            pDevIns;
    /** Config: Speaker enabled. */
    bool                    fSpeakerCfg;
    /** Config: What to do with speaker activity. */
    PITSPEAKEREMU           enmSpeakerEmu;
#ifdef RT_OS_LINUX
    /** File handle for host speaker functionality. */
    int                     hHostSpeaker;
    int                     afAlignment2;
#endif
} PITSTATER3;
/** Pointer to the ring-3 PIT device state. */
typedef PITSTATER3 *PPITSTATER3;


#ifndef VBOX_DEVICE_STRUCT_TESTCASE



static int pit_get_count(PPDMDEVINS pDevIns, PPITSTATE pThis, PPITCHANNEL pChan)
{
    uint64_t d;
    TMTIMERHANDLE hTimer = pThis->channels[0].hTimer;
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, hTimer));

    if (EFFECTIVE_MODE(pChan->mode) == 2)
    {
        if (pChan->u64NextTS == UINT64_MAX)
        {
            d = ASMMultU64ByU32DivByU32(PDMDevHlpTimerGet(pDevIns, hTimer) - pChan->count_load_time,
                                        PIT_FREQ, PDMDevHlpTimerGetFreq(pDevIns, hTimer));
            return pChan->count - (d % pChan->count); /** @todo check this value. */
        }
        uint64_t Interval = pChan->u64NextTS - pChan->u64ReloadTS;
        if (!Interval)
            return pChan->count - 1; /** @todo This is WRONG! But I'm too tired to fix it properly and just want to shut up a DIV/0 trap now. */
        d = PDMDevHlpTimerGet(pDevIns, hTimer);
        d = ASMMultU64ByU32DivByU32(d - pChan->u64ReloadTS, pChan->count, Interval);
        if (d >= pChan->count)
            return 1;
        return pChan->count - d;
    }

    d = ASMMultU64ByU32DivByU32(PDMDevHlpTimerGet(pDevIns, hTimer) - pChan->count_load_time,
                                PIT_FREQ, PDMDevHlpTimerGetFreq(pDevIns, hTimer));
    int counter;
    switch (EFFECTIVE_MODE(pChan->mode))
    {
        case 0:
        case 1:
        case 4:
        case 5:
            counter = (pChan->count - d) & 0xffff;
            break;
        case 3:
            /* XXX: may be incorrect for odd counts */
            counter = pChan->count - ((2 * d) % pChan->count);
            break;
        default:
            counter = pChan->count - (d % pChan->count);
            break;
    }
    /** @todo check that we don't return 0, in most modes (all?) the counter shouldn't be zero. */
    return counter;
}


/* get pit output bit */
static int pit_get_out1(PPDMDEVINS pDevIns, PPITSTATE pThis, PPITCHANNEL pChan, int64_t current_time)
{
    TMTIMERHANDLE hTimer = pThis->channels[0].hTimer;
    uint64_t d;
    int out;

    d = ASMMultU64ByU32DivByU32(current_time - pChan->count_load_time, PIT_FREQ, PDMDevHlpTimerGetFreq(pDevIns, hTimer));
    switch (EFFECTIVE_MODE(pChan->mode))
    {
        default:
        case 0:
            out = (d >= pChan->count);
            break;
        case 1:
            out = (d < pChan->count);
            break;
        case 2:
            Log2(("pit_get_out1: d=%llx c=%x %x \n", d, pChan->count, (unsigned)(d % pChan->count)));
            if ((d % pChan->count) == 0 && d != 0)
                out = 1;
            else
                out = 0;
            break;
        case 3:
            out = (d % pChan->count) < ((pChan->count + 1) >> 1);
            break;
        case 4:
        case 5:
            out = (d != pChan->count);
            break;
    }
    return out;
}


static int pit_get_out(PPDMDEVINS pDevIns, PPITSTATE pThis, int channel, int64_t current_time)
{
    PPITCHANNEL pChan = &pThis->channels[channel];
    return pit_get_out1(pDevIns, pThis, pChan, current_time);
}


static int pit_get_gate(PPITSTATE pThis, int channel)
{
    PPITCHANNEL pChan = &pThis->channels[channel];
    return pChan->gate;
}


/* if already latched, do not latch again */
static void pit_latch_count(PPDMDEVINS pDevIns, PPITSTATE pThis, PPITCHANNEL pChan)
{
    if (!pChan->count_latched)
    {
        pChan->latched_count = pit_get_count(pDevIns, pThis, pChan);
        pChan->count_latched = pChan->rw_mode;
        LogFlow(("pit_latch_count: latched_count=%#06x / %10RU64 ns (c=%#06x m=%d)\n",
                 pChan->latched_count, ASMMultU64ByU32DivByU32(pChan->count - pChan->latched_count, 1000000000, PIT_FREQ),
                 pChan->count, pChan->mode));
    }
}

#ifdef IN_RING3

/* return -1 if no transition will occur.  */
static int64_t pitR3GetNextTransitionTime(PPDMDEVINS pDevIns, PPITSTATE pThis, PPITCHANNEL pChan, uint64_t current_time)
{
    TMTIMERHANDLE hTimer = pThis->channels[0].hTimer;
    uint64_t d, next_time, base;
    uint32_t period2;

    d = ASMMultU64ByU32DivByU32(current_time - pChan->count_load_time, PIT_FREQ, PDMDevHlpTimerGetFreq(pDevIns, hTimer));
    switch (EFFECTIVE_MODE(pChan->mode))
    {
        default:
        case 0:
        case 1:
            if (d < pChan->count)
                next_time = pChan->count;
            else
                return -1;
            break;

        /*
         * Mode 2: The period is 'count' PIT ticks.
         * When the counter reaches 1 we set the output low (for channel 0 that
         * means lowering IRQ0). On the next tick, where we should be decrementing
         * from 1 to 0, the count is loaded and the output goes high (channel 0
         * means raising IRQ0 again and triggering timer interrupt).
         *
         * In VirtualBox we compress the pulse and flip-flop the IRQ line at the
         * end of the period, which signals an interrupt at the exact same time.
         */
        case 2:
            base = (d / pChan->count) * pChan->count;
# ifndef VBOX /* see above */
            if ((d - base) == 0 && d != 0)
                next_time = base + pChan->count - 1;
            else
# endif
                next_time = base + pChan->count;
            break;
        case 3:
            base = (d / pChan->count) * pChan->count;
            period2 = ((pChan->count + 1) >> 1);
            if ((d - base) < period2)
                next_time = base + period2;
            else
                next_time = base + pChan->count;
            break;

        /* Modes 4 and 5 generate a short pulse at the end of the time delay. This
         * is similar to mode 2, except modes 4/5 aren't periodic. We use the same
         * optimization - only use one timer callback and pulse the IRQ.
         * Note: Tickless Linux kernels use PIT mode 4 with 'nolapic'.
         */
        case 4:
        case 5:
# ifdef VBOX
            if (d <= pChan->count)
                next_time = pChan->count;
# else
            if (d < pChan->count)
                next_time = pChan->count;
            else if (d == pChan->count)
                next_time = pChan->count + 1;
# endif
            else
                return -1;
            break;
    }

    /* convert to timer units */
    LogFlow(("PIT: next_time=%'14RU64 %'20RU64 mode=%#x count=%#06x\n", next_time,
             ASMMultU64ByU32DivByU32(next_time, PDMDevHlpTimerGetFreq(pDevIns, hTimer), PIT_FREQ), pChan->mode, pChan->count));
    next_time = pChan->count_load_time + ASMMultU64ByU32DivByU32(next_time, PDMDevHlpTimerGetFreq(pDevIns, hTimer), PIT_FREQ);

    /* fix potential rounding problems */
    if (next_time <= current_time)
        next_time = current_time;

    /* Add one to next_time; if we don't, integer truncation will cause
     * the algorithm to think that at the end of each period, it'pChan still
     * within the first one instead of at the beginning of the next one.
     */
    return next_time + 1;
}


static void pitR3IrqTimerUpdate(PPDMDEVINS pDevIns, PPITSTATE pThis, PPITCHANNEL pChan,
                                uint64_t current_time, uint64_t now, bool in_timer)
{
    int64_t expire_time;
    int irq_level;
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, pThis->channels[0].hTimer));

    if (pChan->hTimer == NIL_TMTIMERHANDLE)
        return;
    expire_time = pitR3GetNextTransitionTime(pDevIns, pThis, pChan, current_time);
    irq_level = pit_get_out1(pDevIns, pThis, pChan, current_time) ? PDM_IRQ_LEVEL_HIGH : PDM_IRQ_LEVEL_LOW;

    /* If PIT is disabled by HPET - simply disconnect ticks from interrupt controllers,
     * but do not modify other aspects of device operation.
     */
    if (!pThis->fDisabledByHpet)
    {
        switch (EFFECTIVE_MODE(pChan->mode))
        {
            case 2:
            case 4:
            case 5:
                /* We just flip-flop the IRQ line to save an extra timer call,
                 * which isn't generally required. However, the pulse is only
                 * generated when running on the timer callback (and thus on
                 * the trailing edge of the output signal pulse).
                 */
                if (in_timer)
                {
                    PDMDevHlpISASetIrq(pDevIns, pChan->irq, PDM_IRQ_LEVEL_FLIP_FLOP);
                    break;
                }
                RT_FALL_THRU();
            default:
                PDMDevHlpISASetIrq(pDevIns, pChan->irq, irq_level);
                break;
        }
    }

    if (irq_level)
    {
        pChan->u64ReloadTS = now;
        STAM_COUNTER_INC(&pThis->StatPITIrq);
    }

    if (expire_time != -1)
    {
        Log3(("pitR3IrqTimerUpdate: next=%'RU64 now=%'RU64\n", expire_time, now));
        pChan->u64NextTS = expire_time;
        PDMDevHlpTimerSet(pDevIns, pChan->hTimer, pChan->u64NextTS);
    }
    else
    {
        LogFlow(("PIT: m=%d count=%#4x irq_level=%#x stopped\n", pChan->mode, pChan->count, irq_level));
        PDMDevHlpTimerStop(pDevIns, pChan->hTimer);
        pChan->u64NextTS = UINT64_MAX;
    }
    pChan->next_transition_time = expire_time;
}


/* val must be 0 or 1 */
static void pitR3SetGate(PPDMDEVINS pDevIns, PPITSTATE pThis, int channel, int val)
{
    PPITCHANNEL     pChan  = &pThis->channels[channel];
    TMTIMERHANDLE   hTimer = pThis->channels[0].hTimer;

    Assert((val & 1) == val);
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, hTimer));

    switch (EFFECTIVE_MODE(pChan->mode))
    {
        default:
        case 0:
        case 4:
            /* XXX: just disable/enable counting */
            break;
        case 1:
        case 5:
            if (pChan->gate < val)
            {
                /* restart counting on rising edge */
                Log(("pitR3SetGate: restarting mode %d\n", pChan->mode));
                pChan->count_load_time = PDMDevHlpTimerGet(pDevIns, hTimer);
                pitR3IrqTimerUpdate(pDevIns, pThis, pChan, pChan->count_load_time, pChan->count_load_time, false);
            }
            break;
        case 2:
        case 3:
            if (pChan->gate < val)
            {
                /* restart counting on rising edge */
                Log(("pitR3SetGate: restarting mode %d\n", pChan->mode));
                pChan->count_load_time = pChan->u64ReloadTS = PDMDevHlpTimerGet(pDevIns, hTimer);
                pitR3IrqTimerUpdate(pDevIns, pThis, pChan, pChan->count_load_time, pChan->count_load_time, false);
            }
            /* XXX: disable/enable counting */
            break;
    }
    pChan->gate = val;
}


static void pitR3LoadCount(PPDMDEVINS pDevIns, PPITSTATE pThis, PPITCHANNEL pChan, int val)
{
    TMTIMERHANDLE hTimer = pThis->channels[0].hTimer;
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, hTimer));

    if (val == 0)
        val = 0x10000;
    pChan->count_load_time = pChan->u64ReloadTS = PDMDevHlpTimerGet(pDevIns, hTimer);
    pChan->count = val;
    pitR3IrqTimerUpdate(pDevIns, pThis, pChan, pChan->count_load_time, pChan->count_load_time, false);

    /* log the new rate (ch 0 only). */
    if (pChan->hTimer != NIL_TMTIMERHANDLE /* ch 0 */)
    {
        if (pChan->cRelLogEntries < 32)
        {
            pChan->cRelLogEntries++;
            LogRel(("PIT: mode=%d count=%#x (%u) - %d.%02d Hz (ch=0)\n",
                    pChan->mode, pChan->count, pChan->count, PIT_FREQ / pChan->count, (PIT_FREQ * 100 / pChan->count) % 100));
        }
        else
            Log(("PIT: mode=%d count=%#x (%u) - %d.%02d Hz (ch=0)\n",
                 pChan->mode, pChan->count, pChan->count, PIT_FREQ / pChan->count, (PIT_FREQ * 100 / pChan->count) % 100));
        PDMDevHlpTimerSetFrequencyHint(pDevIns, hTimer, PIT_FREQ / pChan->count);
    }
    else
        Log(("PIT: mode=%d count=%#x (%u) - %d.%02d Hz (ch=%d)\n",
             pChan->mode, pChan->count, pChan->count, PIT_FREQ / pChan->count, (PIT_FREQ * 100 / pChan->count) % 100,
             pChan - &pThis->channels[0]));
}

#endif /* IN_RING3 */

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 */
static DECLCALLBACK(VBOXSTRICTRC) pitIOPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    Log2(("pitIOPortRead: offPort=%#x cb=%x\n", offPort, cb));
    NOREF(pvUser);
    Assert(offPort < 4);
    if (cb != 1 || offPort == 3)
    {
        Log(("pitIOPortRead: offPort=%#x cb=%x *pu32=unused!\n", offPort, cb));
        return VERR_IOM_IOPORT_UNUSED;
    }
    RT_UNTRUSTED_VALIDATED_FENCE(); /* paranoia */

    PPITSTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PPITSTATE);
    PPITCHANNEL pChan = &pThis->channels[offPort];
    int ret;

    DEVPIT_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_READ);
    if (pChan->status_latched)
    {
        pChan->status_latched = 0;
        ret = pChan->status;
        DEVPIT_UNLOCK(pDevIns, pThis);
    }
    else if (pChan->count_latched)
    {
        switch (pChan->count_latched)
        {
            default:
            case RW_STATE_LSB:
                ret = pChan->latched_count & 0xff;
                pChan->count_latched = 0;
                break;
            case RW_STATE_MSB:
                ret = pChan->latched_count >> 8;
                pChan->count_latched = 0;
                break;
            case RW_STATE_WORD0:
                ret = pChan->latched_count & 0xff;
                pChan->count_latched = RW_STATE_MSB;
                break;
        }
        DEVPIT_UNLOCK(pDevIns, pThis);
    }
    else
    {
        DEVPIT_UNLOCK(pDevIns, pThis);
        DEVPIT_LOCK_BOTH_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_READ);
        int count;
        switch (pChan->read_state)
        {
            default:
            case RW_STATE_LSB:
                count = pit_get_count(pDevIns, pThis, pChan);
                ret = count & 0xff;
                break;
            case RW_STATE_MSB:
                count = pit_get_count(pDevIns, pThis, pChan);
                ret = (count >> 8) & 0xff;
                break;
            case RW_STATE_WORD0:
                count = pit_get_count(pDevIns, pThis, pChan);
                ret = count & 0xff;
                pChan->read_state = RW_STATE_WORD1;
                break;
            case RW_STATE_WORD1:
                count = pit_get_count(pDevIns, pThis, pChan);
                ret = (count >> 8) & 0xff;
                pChan->read_state = RW_STATE_WORD0;
                break;
        }
        DEVPIT_UNLOCK_BOTH(pDevIns, pThis);
    }

    *pu32 = ret;
    Log2(("pitIOPortRead: offPort=%#x cb=%x *pu32=%#04x\n", offPort, cb, *pu32));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC) pitIOPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    Log2(("pitIOPortWrite: offPort=%#x cb=%x u32=%#04x\n", offPort, cb, u32));
    NOREF(pvUser);
    Assert(offPort < 4);

    if (cb != 1)
        return VINF_SUCCESS;

    PPITSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPITSTATE);
    if (offPort == 3)
    {
        /*
         * Port 43h - Mode/Command Register.
         *  7 6 5 4 3 2 1 0
         *  * * . . . . . .  Select channel: 0 0 = Channel 0
         *                                   0 1 = Channel 1
         *                                   1 0 = Channel 2
         *                                   1 1 = Read-back command (8254 only)
         *                                                  (Illegal on 8253)
         *                                                  (Illegal on PS/2 {JAM})
         *  . . * * . . . .  Command/Access mode: 0 0 = Latch count value command
         *                                        0 1 = Access mode: lobyte only
         *                                        1 0 = Access mode: hibyte only
         *                                        1 1 = Access mode: lobyte/hibyte
         *  . . . . * * * .  Operating mode: 0 0 0 = Mode 0, 0 0 1 = Mode 1,
         *                                   0 1 0 = Mode 2, 0 1 1 = Mode 3,
         *                                   1 0 0 = Mode 4, 1 0 1 = Mode 5,
         *                                   1 1 0 = Mode 2, 1 1 1 = Mode 3
         *  . . . . . . . *  BCD/Binary mode: 0 = 16-bit binary, 1 = four-digit BCD
         */
        unsigned channel = (u32 >> 6) & 0x3;
        RT_UNTRUSTED_VALIDATED_FENCE(); /* paranoia */
        if (channel == 3)
        {
            /* read-back command */
            DEVPIT_LOCK_BOTH_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_WRITE);
            for (channel = 0; channel < RT_ELEMENTS(pThis->channels); channel++)
            {
                PPITCHANNEL pChan = &pThis->channels[channel];
                if (u32 & (2 << channel))
                {
                    if (!(u32 & 0x20))
                        pit_latch_count(pDevIns, pThis, pChan);
                    if (!(u32 & 0x10) && !pChan->status_latched)
                    {
                        /* status latch */
                        /* XXX: add BCD and null count */
                        pChan->status = (pit_get_out1(pDevIns, pThis, pChan,
                                                      PDMDevHlpTimerGet(pDevIns, pThis->channels[0].hTimer)) << 7)
                                      | (pChan->rw_mode << 4)
                                      | (pChan->mode << 1)
                                      | pChan->bcd;
                        pChan->status_latched = 1;
                    }
                }
            }
            DEVPIT_UNLOCK_BOTH(pDevIns, pThis);
        }
        else
        {
            PPITCHANNEL pChan = &pThis->channels[channel];
            unsigned access = (u32 >> 4) & 3;
            if (access == 0)
            {
                DEVPIT_LOCK_BOTH_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_WRITE);
                pit_latch_count(pDevIns, pThis, pChan);
                DEVPIT_UNLOCK_BOTH(pDevIns, pThis);
            }
            else
            {
                DEVPIT_LOCK_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_WRITE);
                pChan->rw_mode = access;
                pChan->read_state = access;
                pChan->write_state = access;

                pChan->mode = (u32 >> 1) & 7;
                pChan->bcd = u32 & 1;
                /* XXX: update irq timer ? */
                DEVPIT_UNLOCK(pDevIns, pThis);
            }
        }
    }
    else
    {
#ifndef IN_RING3
        /** @todo There is no reason not to do this in all contexts these
         *        days... */
        return VINF_IOM_R3_IOPORT_WRITE;
#else /* IN_RING3 */
        /*
         * Port 40-42h - Channel Data Ports.
         */
        RT_UNTRUSTED_VALIDATED_FENCE(); /* paranoia */
        PPITCHANNEL pChan = &pThis->channels[offPort];
        DEVPIT_LOCK_BOTH_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_WRITE);
        switch (pChan->write_state)
        {
            default:
            case RW_STATE_LSB:
                pitR3LoadCount(pDevIns, pThis, pChan, u32);
                break;
            case RW_STATE_MSB:
                pitR3LoadCount(pDevIns, pThis, pChan, u32 << 8);
                break;
            case RW_STATE_WORD0:
                pChan->write_latch = u32;
                pChan->write_state = RW_STATE_WORD1;
                break;
            case RW_STATE_WORD1:
                pitR3LoadCount(pDevIns, pThis, pChan, pChan->write_latch | (u32 << 8));
                pChan->write_state = RW_STATE_WORD0;
                break;
        }
        DEVPIT_UNLOCK_BOTH(pDevIns, pThis);
#endif /* !IN_RING3 */
    }
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN, Speaker}
 */
static DECLCALLBACK(VBOXSTRICTRC)
pitIOPortSpeakerRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(pvUser, offPort);
    if (cb == 1)
    {
        PPITSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPITSTATE);
        DEVPIT_LOCK_BOTH_RETURN(pDevIns, pThis, VINF_IOM_R3_IOPORT_READ);

        const uint64_t u64Now = PDMDevHlpTimerGet(pDevIns, pThis->channels[0].hTimer);
        Assert(PDMDevHlpTimerGetFreq(pDevIns, pThis->channels[0].hTimer) == 1000000000); /* lazy bird. */

        /* bit 6,7 Parity error stuff. */
        /* bit 5 - mirrors timer 2 output condition. */
        const int fOut = pit_get_out(pDevIns, pThis, 2, u64Now);
        /* bit 4 - toggled with each (DRAM?) refresh request, every 15.085 u-op Chan.
                   ASSUMES ns timer freq, see assertion above. */
#ifndef FAKE_REFRESH_CLOCK
        const int fRefresh = (u64Now / 15085) & 1;
#else
        pThis->dummy_refresh_clock ^= 1;
        const int fRefresh = pThis->dummy_refresh_clock;
#endif
        /* bit 2,3 NMI / parity status stuff. */
        /* bit 1 - speaker data status */
        const int fSpeakerStatus = pThis->speaker_data_on;
        /* bit 0 - timer 2 clock gate to speaker status. */
        const int fTimer2GateStatus = pit_get_gate(pThis, 2);

        DEVPIT_UNLOCK_BOTH(pDevIns, pThis);

        *pu32 = fTimer2GateStatus
              | (fSpeakerStatus << 1)
              | (fRefresh << 4)
              | (fOut << 5);
        Log(("pitIOPortSpeakerRead: offPort=%#x cb=%x *pu32=%#x\n", offPort, cb, *pu32));
        return VINF_SUCCESS;
    }
    Log(("pitIOPortSpeakerRead: offPort=%#x cb=%x *pu32=unused!\n", offPort, cb));
    return VERR_IOM_IOPORT_UNUSED;
}

#ifdef IN_RING3

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, Speaker}
 */
static DECLCALLBACK(VBOXSTRICTRC)
pitR3IOPortSpeakerWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(pvUser, offPort);
    if (cb == 1)
    {
        PPITSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPITSTATE);
        DEVPIT_LOCK_BOTH_RETURN(pDevIns, pThis, VERR_IGNORED);

        pThis->speaker_data_on = (u32 >> 1) & 1;
        pitR3SetGate(pDevIns, pThis, 2, u32 & 1);

        /** @todo r=klaus move this to a (system-specific) driver, which can
         * abstract the details, and if necessary create a thread to minimize
         * impact on VM execution. */
# ifdef RT_OS_LINUX
        PPITSTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPITSTATER3);

        if (pThisCC->enmSpeakerEmu != PIT_SPEAKER_EMU_NONE)
        {
            PPITCHANNEL pChan = &pThis->channels[2];
            if (pThis->speaker_data_on)
            {
                Log2Func(("starting beep freq=%d\n", PIT_FREQ / pChan->count));
                switch (pThisCC->enmSpeakerEmu)
                {
                    case PIT_SPEAKER_EMU_CONSOLE:
                    {
                        int res;
                        res = ioctl(pThisCC->hHostSpeaker, KIOCSOUND, pChan->count);
                        if (res == -1)
                        {
                            LogRel(("PIT: speaker: ioctl failed errno=%d, disabling emulation\n", errno));
                            pThisCC->enmSpeakerEmu = PIT_SPEAKER_EMU_NONE;
                        }
                        break;
                    }
                    case PIT_SPEAKER_EMU_EVDEV:
                    {
                        struct input_event e;
                        e.type = EV_SND;
                        e.code = SND_TONE;
                        e.value = PIT_FREQ / pChan->count;
                        int res = write(pThisCC->hHostSpeaker, &e, sizeof(struct input_event));
                        NOREF(res);
                        break;
                    }
                    case PIT_SPEAKER_EMU_TTY:
                    {
                        int res = write(pThisCC->hHostSpeaker, "\a", 1);
                        NOREF(res);
                        break;
                    }
                    case PIT_SPEAKER_EMU_NONE:
                        break;
                    default:
                        Log2Func(("unknown speaker emulation %d, disabling emulation\n", pThisCC->enmSpeakerEmu));
                        pThisCC->enmSpeakerEmu = PIT_SPEAKER_EMU_NONE;
                }
            }
            else
            {
                Log2Func(("stopping beep\n"));
                switch (pThisCC->enmSpeakerEmu)
                {
                    case PIT_SPEAKER_EMU_CONSOLE:
                        /* No error checking here. The Linux device driver
                         * implementation considers it an error (errno=22,
                         * EINVAL) to stop sound if it hasn't been started.
                         * Of course we could detect this by checking only
                         * for enabled->disabled transitions and ignoring
                         * disabled->disabled ones, but it's not worth the
                         * effort. */
                        ioctl(pThisCC->hHostSpeaker, KIOCSOUND, 0);
                        break;
                    case PIT_SPEAKER_EMU_EVDEV:
                    {
                        struct input_event e;
                        e.type = EV_SND;
                        e.code = SND_TONE;
                        e.value = 0;
                        int res = write(pThisCC->hHostSpeaker, &e, sizeof(struct input_event));
                        NOREF(res);
                        break;
                    }
                    case PIT_SPEAKER_EMU_TTY:
                        break;
                    case PIT_SPEAKER_EMU_NONE:
                        break;
                    default:
                        Log2Func(("unknown speaker emulation %d, disabling emulation\n", pThisCC->enmSpeakerEmu));
                        pThisCC->enmSpeakerEmu = PIT_SPEAKER_EMU_NONE;
                }
            }
        }
# endif /* RT_OS_LINUX */

        DEVPIT_UNLOCK_BOTH(pDevIns, pThis);
    }
    Log(("pitR3IOPortSpeakerWrite: offPort=%#x cb=%x u32=%#x\n", offPort, cb, u32));
    return VINF_SUCCESS;
}


/* -=-=-=-=-=- Saved state -=-=-=-=-=- */

/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC}
 */
static DECLCALLBACK(int) pitR3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PPITSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PPITSTATE);
    PPITSTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPITSTATER3);

    PCPDMDEVHLPR3 pHlp  = pDevIns->pHlpR3;
    RT_NOREF(uPass);
    pHlp->pfnSSMPutIOPort(pSSM, pThis->IOPortBaseCfg);
    pHlp->pfnSSMPutU8(    pSSM, pThis->channels[0].irq);
    pHlp->pfnSSMPutBool(  pSSM, pThisCC->fSpeakerCfg);
    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) pitR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PPITSTATE     pThis = PDMDEVINS_2_DATA(pDevIns, PPITSTATE);
    PCPDMDEVHLPR3 pHlp  = pDevIns->pHlpR3;
    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rc, rc);

    /* The config. */
    pitR3LiveExec(pDevIns, pSSM, SSM_PASS_FINAL);

    /* The state. */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->channels); i++)
    {
        PPITCHANNEL pChan = &pThis->channels[i];
        pHlp->pfnSSMPutU32(pSSM, pChan->count);
        pHlp->pfnSSMPutU16(pSSM, pChan->latched_count);
        pHlp->pfnSSMPutU8(pSSM, pChan->count_latched);
        pHlp->pfnSSMPutU8(pSSM, pChan->status_latched);
        pHlp->pfnSSMPutU8(pSSM, pChan->status);
        pHlp->pfnSSMPutU8(pSSM, pChan->read_state);
        pHlp->pfnSSMPutU8(pSSM, pChan->write_state);
        pHlp->pfnSSMPutU8(pSSM, pChan->write_latch);
        pHlp->pfnSSMPutU8(pSSM, pChan->rw_mode);
        pHlp->pfnSSMPutU8(pSSM, pChan->mode);
        pHlp->pfnSSMPutU8(pSSM, pChan->bcd);
        pHlp->pfnSSMPutU8(pSSM, pChan->gate);
        pHlp->pfnSSMPutU64(pSSM, pChan->count_load_time);
        pHlp->pfnSSMPutU64(pSSM, pChan->u64NextTS);
        pHlp->pfnSSMPutU64(pSSM, pChan->u64ReloadTS);
        pHlp->pfnSSMPutS64(pSSM, pChan->next_transition_time);
        if (pChan->hTimer != NIL_TMTIMERHANDLE)
            PDMDevHlpTimerSave(pDevIns, pChan->hTimer, pSSM);
    }

    pHlp->pfnSSMPutS32(pSSM, pThis->speaker_data_on);
# ifdef FAKE_REFRESH_CLOCK
    pHlp->pfnSSMPutS32(pSSM, pThis->dummy_refresh_clock);
# else
    pHlp->pfnSSMPutS32(pSSM, 0);
# endif

    pHlp->pfnSSMPutBool(pSSM, pThis->fDisabledByHpet);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) pitR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PPITSTATE     pThis   = PDMDEVINS_2_DATA(pDevIns, PPITSTATE);
    PPITSTATER3   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPITSTATER3);
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;
    int           rc;

    if (    uVersion != PIT_SAVED_STATE_VERSION
        &&  uVersion != PIT_SAVED_STATE_VERSION_VBOX_30
        &&  uVersion != PIT_SAVED_STATE_VERSION_VBOX_31)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* The config. */
    if (uVersion > PIT_SAVED_STATE_VERSION_VBOX_30)
    {
        RTIOPORT IOPortBaseCfg;
        rc = pHlp->pfnSSMGetIOPort(pSSM, &IOPortBaseCfg); AssertRCReturn(rc, rc);
        if (IOPortBaseCfg != pThis->IOPortBaseCfg)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - IOPortBaseCfg: saved=%RTiop config=%RTiop"),
                                    IOPortBaseCfg, pThis->IOPortBaseCfg);

        uint8_t u8Irq;
        rc = pHlp->pfnSSMGetU8(pSSM, &u8Irq); AssertRCReturn(rc, rc);
        if (u8Irq != pThis->channels[0].irq)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - u8Irq: saved=%#x config=%#x"),
                                    u8Irq, pThis->channels[0].irq);

        bool fSpeakerCfg;
        rc = pHlp->pfnSSMGetBool(pSSM, &fSpeakerCfg); AssertRCReturn(rc, rc);
        if (fSpeakerCfg != pThisCC->fSpeakerCfg)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - fSpeakerCfg: saved=%RTbool config=%RTbool"),
                                    fSpeakerCfg, pThisCC->fSpeakerCfg);
    }

    if (uPass != SSM_PASS_FINAL)
        return VINF_SUCCESS;

    /* The state. */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->channels); i++)
    {
        PPITCHANNEL pChan = &pThis->channels[i];
        pHlp->pfnSSMGetU32(pSSM, &pChan->count);
        pHlp->pfnSSMGetU16(pSSM, &pChan->latched_count);
        pHlp->pfnSSMGetU8(pSSM, &pChan->count_latched);
        pHlp->pfnSSMGetU8(pSSM, &pChan->status_latched);
        pHlp->pfnSSMGetU8(pSSM, &pChan->status);
        pHlp->pfnSSMGetU8(pSSM, &pChan->read_state);
        pHlp->pfnSSMGetU8(pSSM, &pChan->write_state);
        pHlp->pfnSSMGetU8(pSSM, &pChan->write_latch);
        pHlp->pfnSSMGetU8(pSSM, &pChan->rw_mode);
        pHlp->pfnSSMGetU8(pSSM, &pChan->mode);
        pHlp->pfnSSMGetU8(pSSM, &pChan->bcd);
        pHlp->pfnSSMGetU8(pSSM, &pChan->gate);
        pHlp->pfnSSMGetU64(pSSM, &pChan->count_load_time);
        pHlp->pfnSSMGetU64(pSSM, &pChan->u64NextTS);
        pHlp->pfnSSMGetU64(pSSM, &pChan->u64ReloadTS);
        pHlp->pfnSSMGetS64(pSSM, &pChan->next_transition_time);
        if (pChan->hTimer != NIL_TMTIMERHANDLE)
        {
            rc = PDMDevHlpTimerLoad(pDevIns, pChan->hTimer, pSSM);
            AssertRCReturn(rc, rc);
            LogRel(("PIT: mode=%d count=%#x (%u) - %d.%02d Hz (ch=%d) (restore)\n",
                    pChan->mode, pChan->count, pChan->count, PIT_FREQ / pChan->count, (PIT_FREQ * 100 / pChan->count) % 100, i));
            rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
            AssertRCReturn(rc, rc);
            PDMDevHlpTimerSetFrequencyHint(pDevIns, pChan->hTimer, PIT_FREQ / pChan->count);
            PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
        }
        pThis->channels[i].cRelLogEntries = 0;
    }

    pHlp->pfnSSMGetS32(pSSM, &pThis->speaker_data_on);
# ifdef FAKE_REFRESH_CLOCK
    pHlp->pfnSSMGetS32(pSSM, &pThis->dummy_refresh_clock);
# else
    int32_t u32Dummy;
    pHlp->pfnSSMGetS32(pSSM, &u32Dummy);
# endif
    if (uVersion > PIT_SAVED_STATE_VERSION_VBOX_31)
        rc = pHlp->pfnSSMGetBool(pSSM, &pThis->fDisabledByHpet);

    return VINF_SUCCESS;
}


/* -=-=-=-=-=- Timer -=-=-=-=-=- */

/**
 * @callback_method_impl{FNTMTIMERDEV, User argument points to the PIT channel state.}
 */
static DECLCALLBACK(void) pitR3Timer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PPITSTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PPITSTATE);
    PPITCHANNEL pChan = (PPITCHANNEL)pvUser;
    STAM_PROFILE_ADV_START(&pThis->StatPITHandler, a);
    Assert(hTimer == pChan->hTimer);

    Log(("pitR3Timer\n"));
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));
    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, hTimer));

    pitR3IrqTimerUpdate(pDevIns, pThis, pChan, pChan->next_transition_time, PDMDevHlpTimerGet(pDevIns, hTimer), true);

    STAM_PROFILE_ADV_STOP(&pThis->StatPITHandler, a);
}


/* -=-=-=-=-=- Debug Info -=-=-=-=-=- */

/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) pitR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PPITSTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PPITSTATE);
    unsigned    i;
    for (i = 0; i < RT_ELEMENTS(pThis->channels); i++)
    {
        const PITCHANNEL *pChan = &pThis->channels[i];

        pHlp->pfnPrintf(pHlp,
                        "PIT (i8254) channel %d status: irq=%#x\n"
                        "      count=%08x"      "  latched_count=%04x  count_latched=%02x\n"
                        "           status=%02x   status_latched=%02x     read_state=%02x\n"
                        "      write_state=%02x      write_latch=%02x        rw_mode=%02x\n"
                        "             mode=%02x              bcd=%02x           gate=%02x\n"
                        "  count_load_time=%016RX64 next_transition_time=%016RX64\n"
                        "      u64ReloadTS=%016RX64            u64NextTS=%016RX64\n"
                        ,
                        i, pChan->irq,
                        pChan->count,         pChan->latched_count,     pChan->count_latched,
                        pChan->status,        pChan->status_latched,    pChan->read_state,
                        pChan->write_state,   pChan->write_latch,       pChan->rw_mode,
                        pChan->mode,          pChan->bcd,               pChan->gate,
                        pChan->count_load_time,   pChan->next_transition_time,
                        pChan->u64ReloadTS,       pChan->u64NextTS);
    }
# ifdef FAKE_REFRESH_CLOCK
    pHlp->pfnPrintf(pHlp, "speaker_data_on=%#x dummy_refresh_clock=%#x\n",
                    pThis->speaker_data_on, pThis->dummy_refresh_clock);
# else
    pHlp->pfnPrintf(pHlp, "speaker_data_on=%#x\n", pThis->speaker_data_on);
# endif
    if (pThis->fDisabledByHpet)
        pHlp->pfnPrintf(pHlp, "Disabled by HPET\n");
}


/* -=-=-=-=-=- IHpetLegacyNotify -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIHPETLEGACYNOTIFY,pfnModeChanged}
 */
static DECLCALLBACK(void) pitR3NotifyHpetLegacyNotify_ModeChanged(PPDMIHPETLEGACYNOTIFY pInterface, bool fActivated)
{
    PPITSTATER3  pThisCC = RT_FROM_MEMBER(pInterface, PITSTATER3, IHpetLegacyNotify);
    PPDMDEVINS   pDevIns = pThisCC->pDevIns;
    PPITSTATE    pThis   = PDMDEVINS_2_DATA(pDevIns, PPITSTATE);
    int const    rcLock  = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

    pThis->fDisabledByHpet = fActivated;

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
}


/* -=-=-=-=-=- PDMDEVINS::IBase -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) pitR3QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDEVINS  pDevIns = RT_FROM_MEMBER(pInterface, PDMDEVINS, IBase);
    PPITSTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPITSTATER3);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE,    &pDevIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHPETLEGACYNOTIFY, &pThisCC->IHpetLegacyNotify);
    return NULL;
}


/* -=-=-=-=-=- PDMDEVREG -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) pitR3Reset(PPDMDEVINS pDevIns)
{
    PPITSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPITSTATE);
    LogFlow(("pitR3Reset: \n"));

    DEVPIT_R3_LOCK_BOTH(pDevIns, pThis);

    pThis->fDisabledByHpet = false;

    for (unsigned i = 0; i < RT_ELEMENTS(pThis->channels); i++)
    {
        PPITCHANNEL pChan = &pThis->channels[i];

# if 1 /* Set everything back to virgin state. (might not be strictly correct) */
        pChan->latched_count = 0;
        pChan->count_latched = 0;
        pChan->status_latched = 0;
        pChan->status = 0;
        pChan->read_state = 0;
        pChan->write_state = 0;
        pChan->write_latch = 0;
        pChan->rw_mode = 0;
        pChan->bcd = 0;
# endif
        pChan->u64NextTS = UINT64_MAX;
        pChan->cRelLogEntries = 0;
        pChan->mode = 3;
        pChan->gate = (i != 2);
        pitR3LoadCount(pDevIns, pThis, pChan, 0);
    }

    DEVPIT_UNLOCK_BOTH(pDevIns, pThis);
}

# ifdef RT_OS_LINUX

static int pitR3TryDeviceOpen(const char *pszPath, int flags)
{
    int fd = open(pszPath, flags);
    if (fd == -1)
        LogRel(("PIT: speaker: cannot open \"%s\", errno=%d\n", pszPath, errno));
    else
        LogRel(("PIT: speaker: opened \"%s\"\n", pszPath));
    return fd;
}


static int pitR3TryDeviceOpenSanitizeIoctl(const char *pszPath, int flags)
{
    int fd = open(pszPath, flags);
    if (fd == -1)
        LogRel(("PIT: speaker: cannot open \"%s\", errno=%d\n", pszPath, errno));
    else
    {
        int errno_eviocgsnd0 = 0;
        int errno_kiocsound = 0;
        if (ioctl(fd, EVIOCGSND(0)) == -1)
        {
            errno_eviocgsnd0 = errno;
            if (ioctl(fd, KIOCSOUND, 1) == -1)
                errno_kiocsound = errno;
            else
                ioctl(fd, KIOCSOUND, 0);
        }
        if (errno_eviocgsnd0 && errno_kiocsound)
        {
            LogRel(("PIT: speaker: cannot use \"%s\", ioctl failed errno=%d/errno=%d\n", pszPath, errno_eviocgsnd0, errno_kiocsound));
            close(fd);
            fd = -1;
        }
        else
            LogRel(("PIT: speaker: opened \"%s\"\n", pszPath));
    }
    return fd;
}

# endif /* RT_OS_LINUX */

/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int)  pitR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);

#ifdef RT_OS_LINUX
    PPITSTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPITSTATER3);

    if (pThisCC->enmSpeakerEmu != PIT_SPEAKER_EMU_NONE)
    {
        Assert(pThisCC->hHostSpeaker != -1);
        close(pThisCC->hHostSpeaker);
        pThisCC->hHostSpeaker = -1;
    }
#endif

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int)  pitR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PPITSTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PPITSTATE);
    PPITSTATER3     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPITSTATER3);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;
    uint8_t         u8Irq;
    uint16_t        u16Base;
    bool            fSpeaker;
    unsigned        i;
    Assert(iInstance == 0);

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "Irq|Base|SpeakerEnabled|PassthroughSpeaker|PassthroughSpeakerDevice", "");

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "Irq", &u8Irq, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"Irq\" as a uint8_t failed"));

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "Base", &u16Base, 0x40);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"Base\" as a uint16_t failed"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "SpeakerEnabled", &fSpeaker, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Querying \"SpeakerEnabled\" as a bool failed"));

    uint8_t uPassthroughSpeaker;
    char *pszPassthroughSpeakerDevice = NULL;
    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "PassthroughSpeaker", &uPassthroughSpeaker, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: failed to read PassthroughSpeaker as uint8_t"));
    if (uPassthroughSpeaker)
    {
        rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "PassthroughSpeakerDevice", &pszPassthroughSpeakerDevice, NULL);
        if (RT_FAILURE(rc))
            return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: failed to read PassthroughSpeakerDevice as string"));
    }

    /*
     * Init the data.
     */
    pThis->IOPortBaseCfg   = u16Base;
    pThis->channels[0].irq = u8Irq;
    for (i = 0; i < RT_ELEMENTS(pThis->channels); i++)
    {
        pThis->channels[i].hTimer = NIL_TMTIMERHANDLE;
        pThis->channels[i].iChan  = i;
    }
    pThisCC->fSpeakerCfg   = fSpeaker;
    pThisCC->enmSpeakerEmu = PIT_SPEAKER_EMU_NONE;
    if (uPassthroughSpeaker)
    {
        /** @todo r=klaus move this to a (system-specific) driver */
#ifdef RT_OS_LINUX
        /** @todo r=andy Use defines / enums(?) for all those uPassthroughSpeaker below. */
        int fd = -1;
        if (uPassthroughSpeaker == 1 || uPassthroughSpeaker == 100)
            fd = pitR3TryDeviceOpenSanitizeIoctl("/dev/input/by-path/platform-pcspkr-event-spkr", O_WRONLY);

        if (fd == -1 && (uPassthroughSpeaker == 2 || uPassthroughSpeaker == 100))
            fd = pitR3TryDeviceOpenSanitizeIoctl("/dev/tty", O_WRONLY);

        if (fd == -1 && (uPassthroughSpeaker == 3 || uPassthroughSpeaker == 100))
        {
            fd = pitR3TryDeviceOpenSanitizeIoctl("/dev/tty0", O_WRONLY);
            if (fd == -1)
                fd = pitR3TryDeviceOpenSanitizeIoctl("/dev/vc/0", O_WRONLY);
        }

        if (fd == -1 && (uPassthroughSpeaker == 9 || uPassthroughSpeaker == 100) && pszPassthroughSpeakerDevice)
            fd = pitR3TryDeviceOpenSanitizeIoctl(pszPassthroughSpeakerDevice, O_WRONLY);

        if (fd != -1)
        {
            if (ioctl(fd, EVIOCGSND(0)) != -1)
            {
                pThisCC->enmSpeakerEmu = PIT_SPEAKER_EMU_EVDEV;
                LogRel(("PIT: speaker: emulation mode evdev\n"));
            }
            else
            {
                pThisCC->enmSpeakerEmu = PIT_SPEAKER_EMU_CONSOLE;
                LogRel(("PIT: speaker: emulation mode console\n"));
            }
            pThisCC->hHostSpeaker = fd;
        }
        else
        {
            if (uPassthroughSpeaker == 70 || uPassthroughSpeaker == 100)
                fd = pitR3TryDeviceOpen("/dev/tty", O_WRONLY);
            if (fd == -1 && (uPassthroughSpeaker == 79 || uPassthroughSpeaker == 100) && pszPassthroughSpeakerDevice)
                fd = pitR3TryDeviceOpen(pszPassthroughSpeakerDevice, O_WRONLY);
            if (fd != -1)
            {
                pThisCC->enmSpeakerEmu = PIT_SPEAKER_EMU_TTY;
                pThisCC->hHostSpeaker  = fd;
                LogRel(("PIT: speaker: emulation mode tty\n"));
            }
            else
                LogRel(("PIT: speaker: no emulation possible\n"));
        }
#else  /* !RT_OS_LINUX */
        LogRel(("PIT: speaker: emulation deactivated\n"));
#endif /* !RT_OS_LINUX */
        if (pszPassthroughSpeakerDevice)
        {
            PDMDevHlpMMHeapFree(pDevIns, pszPassthroughSpeakerDevice);
            pszPassthroughSpeakerDevice = NULL;
        }
    }

    /*
     * Interfaces
     */
    /* IBase */
    pDevIns->IBase.pfnQueryInterface          = pitR3QueryInterface;
    /* IHpetLegacyNotify */
    pThisCC->IHpetLegacyNotify.pfnModeChanged = pitR3NotifyHpetLegacyNotify_ModeChanged;
    pThisCC->pDevIns                          = pDevIns;

    /*
     * We do our own locking.  This must be done before creating timers.
     */
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSect, RT_SRC_POS, "pit#%u", iInstance);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Create the timer, make it take our critsect.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, pitR3Timer, &pThis->channels[0],
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0, "i8254 PIT", &pThis->channels[0].hTimer);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpTimerSetCritSect(pDevIns, pThis->channels[0].hTimer, &pThis->CritSect);
    AssertRCReturn(rc, rc);

    /*
     * Register I/O ports.
     */
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, u16Base, 4 /*cPorts*/, pitIOPortWrite, pitIOPortRead,
                                     "i8254 Programmable Interval Timer", NULL /*paExtDescs*/, &pThis->hIoPorts);
    AssertRCReturn(rc, rc);

    if (fSpeaker)
    {
        rc = PDMDevHlpIoPortCreateAndMap(pDevIns, 0x61, 1 /*cPorts*/, pitR3IOPortSpeakerWrite, pitIOPortSpeakerRead,
                                         "PC Speaker", NULL /*paExtDescs*/, &pThis->hIoPortSpeaker);
        AssertRCReturn(rc, rc);
    }

    /*
     * Saved state.
     */
    rc = PDMDevHlpSSMRegister3(pDevIns, PIT_SAVED_STATE_VERSION, sizeof(*pThis), pitR3LiveExec, pitR3SaveExec, pitR3LoadExec);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Initialize the device state.
     */
    pitR3Reset(pDevIns);

    /*
     * Register statistics and debug info.
     */
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatPITIrq,      STAMTYPE_COUNTER, "/TM/PIT/Irq",      STAMUNIT_OCCURENCES,     "The number of times a timer interrupt was triggered.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatPITHandler,  STAMTYPE_PROFILE, "/TM/PIT/Handler",  STAMUNIT_TICKS_PER_CALL, "Profiling timer callback handler.");

    PDMDevHlpDBGFInfoRegister(pDevIns, "pit", "Display PIT (i8254) status. (no arguments)", pitR3Info);

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) picRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PPITSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PPITSTATE);

    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPorts, pitIOPortWrite, pitIOPortRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortSpeaker, NULL /*pfnWrite*/, pitIOPortSpeakerRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceI8254 =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "i8254",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_PIT,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(PITSTATE),
    /* .cbInstanceCC = */           CTX_EXPR(sizeof(PITSTATER3), 0, 0),
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Intel 8254 Programmable Interval Timer (PIT) And Dummy Speaker Device",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           pitR3Construct,
    /* .pfnDestruct = */            pitR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               pitR3Reset,
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
    /* .pfnConstruct = */           picRZConstruct,
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
    /* .pfnConstruct = */           picRZConstruct,
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
