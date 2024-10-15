/* $Id: sleepqueue-r0drv-freebsd.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - FreeBSD Ring-0 Driver Helpers for Abstracting Sleep Queues,
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef IPRT_INCLUDED_SRC_r0drv_freebsd_sleepqueue_r0drv_freebsd_h
#define IPRT_INCLUDED_SRC_r0drv_freebsd_sleepqueue_r0drv_freebsd_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "the-freebsd-kernel.h"

#include <iprt/asm-math.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/time.h>

/**
 * Kernel mode FreeBSD wait state structure.
 */
typedef struct RTR0SEMBSDSLEEP
{
    /** The absolute timeout given as nano seconds since the start of the
     *  monotonic clock. */
    uint64_t        uNsAbsTimeout;
    /** The timeout in ticks. Updated after waiting. */
    int             iTimeout;
    /** Set if it's an indefinite wait. */
    bool            fIndefinite;
    /** Set if we've already timed out.
     * Set by rtR0SemBsdWaitDoIt and read by rtR0SemBsdWaitHasTimedOut. */
    bool            fTimedOut;
    /** Flag whether the wait was interrupted. */
    bool            fInterrupted;
    /** flag whether the wait is interruptible or not. */
    bool            fInterruptible;
    /** Opaque wait channel id. */
    void            *pvWaitChan;
} RTR0SEMBSDSLEEP;
/** Pointer to a FreeBSD wait state. */
typedef RTR0SEMBSDSLEEP *PRTR0SEMBSDSLEEP;


/**
 * Updates the timeout of the FreeBSD wait.
 *
 * @returns RTSEMWAIT_FLAGS_INDEFINITE if the timeout value is too big.
 *          0 otherwise
 * @param   pWait               The wait structure.
 * @param   uTimeout            The relative timeout in nanoseconds.
 */
DECLINLINE(uint32_t) rtR0SemBsdWaitUpdateTimeout(PRTR0SEMBSDSLEEP pWait, uint64_t uTimeout)
{
#if 0
    struct timeval tv;

    tv.tv_sec = uTimeout / UINT64_C(1000000000);
    tv.tv_usec = (uTimeout % UINT64_C(1000000000)) / UINT64_C(1000);

    pWait->iTimeout = tvtohz(&tv);
#else
    uint64_t cTicks = ASMMultU64ByU32DivByU32(uTimeout, hz, UINT32_C(1000000000));
    if (cTicks >= INT_MAX)
        return RTSEMWAIT_FLAGS_INDEFINITE;
    else
        pWait->iTimeout     = (int)cTicks;
#endif

    return 0;
}

/**
 * Initializes a wait.
 *
 * The caller MUST check the wait condition BEFORE calling this function or the
 * timeout logic will be flawed.
 *
 * @returns VINF_SUCCESS or VERR_TIMEOUT.
 * @param   pWait               The wait structure.
 * @param   fFlags              The wait flags.
 * @param   uTimeout            The timeout.
 * @param   pvWaitChan          The opaque wait channel.
 */
DECLINLINE(int) rtR0SemBsdWaitInit(PRTR0SEMBSDSLEEP pWait, uint32_t fFlags, uint64_t uTimeout,
                                   void *pvWaitChan)
{
    pWait->iTimeout = 0;
    pWait->uNsAbsTimeout = 0; /* shut up gcc */

    /*
     * Process the flags and timeout.
     */
    if (!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE))
    {
/** @todo optimize: millisecs -> nanosecs -> millisec -> jiffies */
        if (fFlags & RTSEMWAIT_FLAGS_MILLISECS)
            uTimeout = uTimeout < UINT64_MAX / UINT32_C(1000000) * UINT32_C(1000000)
                     ? uTimeout * UINT32_C(1000000)
                     : UINT64_MAX;
        if (uTimeout == UINT64_MAX)
            fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
        else
        {
            uint64_t u64Now;
            if (fFlags & RTSEMWAIT_FLAGS_RELATIVE)
            {
                if (uTimeout == 0)
                    return VERR_TIMEOUT;

                u64Now = RTTimeSystemNanoTS();
                if (u64Now + uTimeout < u64Now) /* overflow */
                    fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
                else
                    pWait->uNsAbsTimeout = u64Now + uTimeout;
            }
            else
            {
                u64Now = RTTimeSystemNanoTS();
                if (u64Now >= uTimeout)
                    return VERR_TIMEOUT;

                pWait->uNsAbsTimeout = uTimeout;
                uTimeout -= u64Now; /* Get a relative value. */
            }
        }
    }

    if (!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE))
    {
        pWait->fIndefinite      = false;
        fFlags |= rtR0SemBsdWaitUpdateTimeout(pWait, uTimeout);
    }

    if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
    {
        pWait->fIndefinite      = true;
        pWait->iTimeout         = INT_MAX;
        pWait->uNsAbsTimeout    = UINT64_MAX;
    }

    pWait->fTimedOut   = false;

    /*
     * Initialize the wait queue related bits.
     */
    pWait->fInterruptible = fFlags & RTSEMWAIT_FLAGS_INTERRUPTIBLE
                            ? true : false;
    pWait->pvWaitChan     = pvWaitChan;
    pWait->fInterrupted   = false;

    return VINF_SUCCESS;
}

/**
 * Prepares the next wait.
 *
 * This must be called before rtR0SemBsdWaitDoIt, and the caller should check
 * the exit conditions inbetween the two calls.
 *
 * @param   pWait               The wait structure.
 */
DECLINLINE(void) rtR0SemBsdWaitPrepare(PRTR0SEMBSDSLEEP pWait)
{
    /* Lock the queues. */
    sleepq_lock(pWait->pvWaitChan);
}

/**
 * Do the actual wait.
 *
 * @param   pWait               The wait structure.
 */
DECLINLINE(void) rtR0SemBsdWaitDoIt(PRTR0SEMBSDSLEEP pWait)
{
    int rcBsd;
    int fSleepqFlags = SLEEPQ_CONDVAR;

    if (pWait->fInterruptible)
        fSleepqFlags |= SLEEPQ_INTERRUPTIBLE;

    sleepq_add(pWait->pvWaitChan, NULL, "VBoxIS", fSleepqFlags, 0);

    if (!pWait->fIndefinite)
    {
        sleepq_set_timeout(pWait->pvWaitChan, pWait->iTimeout);

        if (pWait->fInterruptible)
            rcBsd = SLEEPQ_TIMEDWAIT_SIG(pWait->pvWaitChan);
        else
            rcBsd = SLEEPQ_TIMEDWAIT(pWait->pvWaitChan);
    }
    else
    {
        if (pWait->fInterruptible)
            rcBsd = SLEEPQ_WAIT_SIG(pWait->pvWaitChan);
        else
        {
            rcBsd = 0;
            SLEEPQ_WAIT(pWait->pvWaitChan);
        }
    }

    switch (rcBsd)
    {
        case 0:
            break;
        case ERESTART:
        {
            if (!pWait->fIndefinite)
            {
                /* Recalc timeout. */
                uint64_t u64Now = RTTimeSystemNanoTS();
                if (u64Now >= pWait->uNsAbsTimeout)
                    pWait->fTimedOut = true;
                else
                {
                    u64Now = pWait->uNsAbsTimeout - u64Now;
                    rtR0SemBsdWaitUpdateTimeout(pWait, u64Now);
                }
            }
            break;
        }
        case EWOULDBLOCK:
            pWait->fTimedOut = true;
            break;
        case EINTR:
            Assert(pWait->fInterruptible);
            pWait->fInterrupted = true;
            break;
        default:
            AssertMsgFailed(("sleepq_* -> %d\n", rcBsd));
            break;
    }
}


/**
 * Checks if a FreeBSD wait was interrupted.
 *
 * @returns true / false
 * @param   pWait               The wait structure.
 * @remarks This shall be called before the first rtR0SemBsdWaitDoIt().
 */
DECLINLINE(bool) rtR0SemBsdWaitWasInterrupted(PRTR0SEMBSDSLEEP pWait)
{
    return pWait->fInterrupted;
}


/**
 * Checks if a FreeBSD wait has timed out.
 *
 * @returns true / false
 * @param   pWait               The wait structure.
 */
DECLINLINE(bool) rtR0SemBsdWaitHasTimedOut(PRTR0SEMBSDSLEEP pWait)
{
    return pWait->fTimedOut;
}


/**
 * Deletes a FreeBSD wait.
 *
 * @param   pWait               The wait structure.
 */
DECLINLINE(void) rtR0SemBsdWaitDelete(PRTR0SEMBSDSLEEP pWait)
{
    sleepq_release(pWait->pvWaitChan);
}


/**
 * Signals the wait channel.
 *
 * @param  pvWaitChan           The opaque wait channel handle.
 */
DECLINLINE(void) rtR0SemBsdSignal(void *pvWaitChan)
{
    sleepq_lock(pvWaitChan);
    int fWakeupSwapProc = sleepq_signal(pvWaitChan, SLEEPQ_CONDVAR, 0, 0);
    sleepq_release(pvWaitChan);
    if (fWakeupSwapProc)
        kick_proc0();
}

/**
 * Wakes up all waiters on the wait channel.
 *
 * @param  pvWaitChan           The opaque wait channel handle.
 */
DECLINLINE(void) rtR0SemBsdBroadcast(void *pvWaitChan)
{
    sleepq_lock(pvWaitChan);
    sleepq_broadcast(pvWaitChan, SLEEPQ_CONDVAR, 0, 0);
#if __FreeBSD_version >= 800000 /* Broadcast releases the sleep queue lock on FreeBSD 7.x */
    sleepq_release(pvWaitChan);
#endif
}

/**
 * Gets the max resolution of the timeout machinery.
 *
 * @returns Resolution specified in nanoseconds.
 */
DECLINLINE(uint32_t) rtR0SemBsdWaitGetResolution(void)
{
    return 1000000000 / hz; /* ns */
}

#endif /* !IPRT_INCLUDED_SRC_r0drv_freebsd_sleepqueue_r0drv_freebsd_h */

