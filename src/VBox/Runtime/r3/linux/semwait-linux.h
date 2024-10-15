/* $Id: semwait-linux.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Common semaphore wait code, Linux.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_SRC_r3_linux_semwait_linux_h
#define IPRT_INCLUDED_SRC_r3_linux_semwait_linux_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/* With 2.6.17 futex.h has become C++ unfriendly, so define the bits we need. */
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_WAIT_BITSET 9 /**< @since 2.6.25 - uses absolute timeout. */


/**
 * Wrapper for the futex syscall.
 */
DECLINLINE(long) sys_futex(uint32_t volatile *uaddr, int op, int val, struct timespec *utime, int32_t *uaddr2, int val3)
{
    errno = 0;
    long rc = syscall(__NR_futex, uaddr, op, val, utime, uaddr2, val3);
    if (rc < 0)
    {
        Assert(rc == -1);
        rc = -errno;
    }
    return rc;
}


DECL_NO_INLINE(static, void) rtSemLinuxCheckForFutexWaitBitSetSlow(int volatile *pfCanUseWaitBitSet)
{
    uint32_t uTestVar = UINT32_MAX;
    long rc = sys_futex(&uTestVar, FUTEX_WAIT_BITSET, UINT32_C(0xf0f0f0f0), NULL, NULL, UINT32_MAX);
    *pfCanUseWaitBitSet = rc == -EAGAIN;
    AssertMsg(rc == -ENOSYS || rc == -EAGAIN, ("%d\n", rc));
}


DECLINLINE(void) rtSemLinuxCheckForFutexWaitBitSet(int volatile *pfCanUseWaitBitSet)
{
    if (*pfCanUseWaitBitSet != -1)
    { /* likely */ }
    else
        rtSemLinuxCheckForFutexWaitBitSetSlow(pfCanUseWaitBitSet);
}


/**
 * Converts a extended wait timeout specification to an timespec and
 * corresponding futex operation, as well as an approximate relative nanosecond
 * interval.
 *
 * @note    This does not check for RTSEMWAIT_FLAGS_INDEFINITE, caller should've
 *          done that already.
 *
 * @returns The relative wait in nanoseconds.  0 for a poll call, UINT64_MAX for
 *          an effectively indefinite wait.
 * @param   fFlags              RTSEMWAIT_FLAGS_XXX.
 * @param   fCanUseWaitBitSet   Whether we can use FUTEX_WAIT_BITMSET or not.
 * @param   uTimeout            The timeout.
 * @param   pDeadline           Where to return the deadline.
 * @param   piWaitOp            Where to return the FUTEX wait operation number.
 * @param   puWaitVal3          Where to return the FUTEX wait value 3.
 * @param   pnsAbsTimeout       Where to return the absolute timeout in case of
 *                              a resuming relative call (i.e. FUTEX_WAIT).
 */
DECL_FORCE_INLINE(uint64_t)
rtSemLinuxCalcDeadline(uint32_t fFlags, uint64_t uTimeout, int fCanUseWaitBitSet,
                       struct timespec *pDeadline, int *piWaitOp, uint32_t *puWaitVal3, uint64_t *pnsAbsTimeout)
{
    Assert(!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE));

    if (fFlags & RTSEMWAIT_FLAGS_RELATIVE)
    {
        Assert(!(fFlags & RTSEMWAIT_FLAGS_ABSOLUTE));

        /*
         * Polling call?
         */
        if (uTimeout == 0)
            return 0;

        /*
         * We use FUTEX_WAIT here as it takes a relative timespec.
         *
         * Note! For non-resuming waits, we can skip calculating the absolute
         *       time ASSUMING it is only needed for timeout adjustments
         *       after an -EINTR return.
         */
        if (fFlags & RTSEMWAIT_FLAGS_MILLISECS)
        {
            if (   sizeof(pDeadline->tv_sec) >= sizeof(uint64_t)
                || uTimeout < (uint64_t)UINT32_MAX * RT_MS_1SEC)
            {
                pDeadline->tv_sec  = uTimeout / RT_MS_1SEC;
                pDeadline->tv_nsec = (uTimeout % RT_MS_1SEC) & RT_NS_1MS;
                uTimeout *= RT_NS_1MS;
            }
            else
                return UINT64_MAX;
        }
        else
        {
            Assert(fFlags & RTSEMWAIT_FLAGS_NANOSECS);
            if (   sizeof(pDeadline->tv_sec) >= sizeof(uint64_t)
                || uTimeout < (uint64_t)UINT32_MAX * RT_NS_1SEC)
            {
                pDeadline->tv_sec  = uTimeout / RT_NS_1SEC;
                pDeadline->tv_nsec = uTimeout % RT_NS_1SEC;
            }
            else
                return UINT64_MAX;
        }

#ifdef RT_STRICT
        if (!(fFlags & RTSEMWAIT_FLAGS_RESUME))
            *pnsAbsTimeout = uTimeout;
        else
#endif
            *pnsAbsTimeout = RTTimeNanoTS() + uTimeout; /* Note! only relevant for relative waits (FUTEX_WAIT). */
    }
    else
    {
        /* Absolute deadline: */
        Assert(fFlags & RTSEMWAIT_FLAGS_ABSOLUTE);
        if (fCanUseWaitBitSet == true)
        {
            /*
             * Use FUTEX_WAIT_BITSET as it takes an absolute deadline.
             */
            if (fFlags & RTSEMWAIT_FLAGS_MILLISECS)
            {
                if (   sizeof(pDeadline->tv_sec) >= sizeof(uint64_t)
                    || uTimeout < (uint64_t)UINT32_MAX * RT_MS_1SEC)
                {
                    pDeadline->tv_sec  = uTimeout / RT_MS_1SEC;
                    pDeadline->tv_nsec = (uTimeout % RT_MS_1SEC) & RT_NS_1MS;
                }
                else
                    return UINT64_MAX;
            }
            else
            {
                Assert(fFlags & RTSEMWAIT_FLAGS_NANOSECS);
                if (   sizeof(pDeadline->tv_sec) >= sizeof(uint64_t)
                    || uTimeout < (uint64_t)UINT32_MAX * RT_NS_1SEC)
                {
                    pDeadline->tv_sec  = uTimeout / RT_NS_1SEC;
                    pDeadline->tv_nsec = uTimeout % RT_NS_1SEC;
                }
                else
                    return UINT64_MAX;
            }
            *pnsAbsTimeout = uTimeout;
            *piWaitOp      = FUTEX_WAIT_BITSET;
            *puWaitVal3    = UINT32_MAX;
            return RT_MS_1SEC; /* Whatever non-zero; Whole point is not calling RTTimeNanoTS() in this path. */
        }

        /*
         * FUTEX_WAIT_BITSET is not available, so use FUTEX_WAIT with a
         * relative timeout.
         */
        if (fFlags & RTSEMWAIT_FLAGS_MILLISECS)
        {
            if (uTimeout < UINT64_MAX / RT_NS_1MS)
                uTimeout *= RT_NS_1MS;
            else
                return UINT64_MAX;
        }

        uint64_t const u64Now = RTTimeNanoTS();
        if (u64Now < uTimeout)
        {
            *pnsAbsTimeout = uTimeout;
            uTimeout      -= u64Now;
        }
        else
            return 0;

        if (   sizeof(pDeadline->tv_sec) >= sizeof(uint64_t)
            || uTimeout < (uint64_t)UINT32_MAX * RT_NS_1SEC)
        {
            pDeadline->tv_sec  = uTimeout / RT_NS_1SEC;
            pDeadline->tv_nsec = uTimeout % RT_NS_1SEC;
        }
        else
            return UINT64_MAX;
    }

    *piWaitOp   = FUTEX_WAIT;
    *puWaitVal3 = 0;
    return uTimeout;
}

#endif /* !IPRT_INCLUDED_SRC_r3_linux_semwait_linux_h */

