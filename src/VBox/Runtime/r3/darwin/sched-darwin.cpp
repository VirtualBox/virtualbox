/* $Id: sched-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Scheduling, Darwin.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_THREAD
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <mach/thread_info.h>
#include <mach/host_info.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <sched.h>
#include <pthread.h>
#include <limits.h>
#include <errno.h>

#include <iprt/thread.h>
#include <iprt/log.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include "internal/sched.h"
#include "internal/thread.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Configuration of one priority.
 */
typedef struct
{
    /** The priority. */
    RTPROCPRIORITY  enmPriority;
    /** The name of this priority. */
    const char     *pszName;
    /** Array scheduler attributes corresponding to each of the thread types. */
    struct
    {
        /** For sanity include the array index. */
        RTTHREADTYPE    enmType;
        /** The desired mach base_priority value. */
        int             iBasePriority;
        /** The suggested priority value. (Same as iBasePriority seems to do the
         *  trick.) */
        int             iPriority;
    } aTypes[RTTHREADTYPE_END];
} PROCPRIORITY;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Array of static priority configurations.
 *
 * ASSUMES that pthread_setschedparam takes a sched_priority argument in the
 * range 0..127, which is translated into mach base_priority 0..63 and mach
 * importance -31..32 (among other things). We also ASSUMES SCHED_OTHER.
 *
 * The base_priority range can be checked with tstDarwinSched, we're assuming it's
 * 0..63 for user processes.
 *
 * Further we observe that fseventsd and mds both run at (mach) priority 50,
 * while Finder runs at 47. At priority 63 we find the dynamic pager, the login
 * window, UserEventAgent, SystemUIServer and coreaudiod. We do not wish to upset the
 * dynamic pager, UI or audio, but we wish for I/O to not be bothered by spotlight
 * (mds/fseventsd).
 */
static const PROCPRIORITY g_aPriorities[] =
{
    {
        RTPROCPRIORITY_DEFAULT, "Default",
        {
            { RTTHREADTYPE_INVALID,                 INT_MIN, INT_MIN },
            { RTTHREADTYPE_INFREQUENT_POLLER,       29, 29 },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       30, 30 },
            { RTTHREADTYPE_EMULATION,               31, 31 }, /* the default priority */
            { RTTHREADTYPE_DEFAULT,                 32, 32 },
            { RTTHREADTYPE_GUI,                     32, 32 },
            { RTTHREADTYPE_MAIN_WORKER,             32, 32 },
            { RTTHREADTYPE_VRDP_IO,                 39, 39 },
            { RTTHREADTYPE_DEBUGGER,                42, 42 },
            { RTTHREADTYPE_MSG_PUMP,                47, 47 },
            { RTTHREADTYPE_IO,                      52, 52 },
            { RTTHREADTYPE_TIMER,                   55, 55 }
        }
    },
    {
        RTPROCPRIORITY_LOW, "Low",
        {
            { RTTHREADTYPE_INVALID,                 INT_MIN, INT_MIN },
            { RTTHREADTYPE_INFREQUENT_POLLER,       20, 20 },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       22, 22 },
            { RTTHREADTYPE_EMULATION,               24, 24 },
            { RTTHREADTYPE_DEFAULT,                 28, 28 },
            { RTTHREADTYPE_GUI,                     29, 29 },
            { RTTHREADTYPE_MAIN_WORKER,             30, 30 },
            { RTTHREADTYPE_VRDP_IO,                 31, 31 },
            { RTTHREADTYPE_DEBUGGER,                31, 31 },
            { RTTHREADTYPE_MSG_PUMP,                31, 31 },
            { RTTHREADTYPE_IO,                      31, 31 },
            { RTTHREADTYPE_TIMER,                   31, 31 }
        }
    },
    {
        RTPROCPRIORITY_NORMAL, "Normal",
        {
            { RTTHREADTYPE_INVALID,                 INT_MIN, INT_MIN },
            { RTTHREADTYPE_INFREQUENT_POLLER,       29, 29 },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       30, 30 },
            { RTTHREADTYPE_EMULATION,               31, 31 }, /* the default priority */
            { RTTHREADTYPE_DEFAULT,                 32, 32 },
            { RTTHREADTYPE_GUI,                     32, 32 },
            { RTTHREADTYPE_MAIN_WORKER,             32, 32 },
            { RTTHREADTYPE_VRDP_IO,                 39, 39 },
            { RTTHREADTYPE_DEBUGGER,                42, 42 },
            { RTTHREADTYPE_MSG_PUMP,                47, 47 },
            { RTTHREADTYPE_IO,                      52, 52 },
            { RTTHREADTYPE_TIMER,                   55, 55 }
        }
    },
    {
        RTPROCPRIORITY_HIGH, "High",
        {
            { RTTHREADTYPE_INVALID,                 INT_MIN, INT_MIN },
            { RTTHREADTYPE_INFREQUENT_POLLER,       30, 30 },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       31, 31 },
            { RTTHREADTYPE_EMULATION,               32, 32 },
            { RTTHREADTYPE_DEFAULT,                 40, 40 },
            { RTTHREADTYPE_GUI,                     41, 41 },
            { RTTHREADTYPE_MAIN_WORKER,             43, 43 },
            { RTTHREADTYPE_VRDP_IO,                 45, 45 },
            { RTTHREADTYPE_DEBUGGER,                47, 47 },
            { RTTHREADTYPE_MSG_PUMP,                49, 49 },
            { RTTHREADTYPE_IO,                      57, 57 },
            { RTTHREADTYPE_TIMER,                   61, 61 }
        }
    },
    /* last */
    {
        RTPROCPRIORITY_FLAT, "Flat",
        {
            { RTTHREADTYPE_INVALID,                 INT_MIN, INT_MIN },
            { RTTHREADTYPE_INFREQUENT_POLLER,       31, 31 },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       31, 31 },
            { RTTHREADTYPE_EMULATION,               31, 31 },
            { RTTHREADTYPE_DEFAULT,                 31, 31 },
            { RTTHREADTYPE_GUI,                     31, 31 },
            { RTTHREADTYPE_MAIN_WORKER,             31, 31 },
            { RTTHREADTYPE_VRDP_IO,                 31, 31 },
            { RTTHREADTYPE_DEBUGGER,                31, 31 },
            { RTTHREADTYPE_MSG_PUMP,                31, 31 },
            { RTTHREADTYPE_IO,                      31, 31 },
            { RTTHREADTYPE_TIMER,                   31, 31 }
        }
    },
};


/**
 * The dynamic default priority configuration.
 *
 * This can be recalulated at runtime depending on what the
 * system allow us to do. Presently we don't do this as it seems
 * Darwin generally lets us do whatever we want.
 *
 * @remarks this is the same as "Normal" above.
 */
static PROCPRIORITY g_aDefaultPriority =
{
    RTPROCPRIORITY_DEFAULT, "Default",
    {
        { RTTHREADTYPE_INVALID,                 INT_MIN, INT_MIN },
        { RTTHREADTYPE_INFREQUENT_POLLER,       29, 29 },
        { RTTHREADTYPE_MAIN_HEAVY_WORKER,       30, 30 },
        { RTTHREADTYPE_EMULATION,               31, 31 }, /* the default priority */
        { RTTHREADTYPE_DEFAULT,                 32, 32 },
        { RTTHREADTYPE_GUI,                     32, 32 },
        { RTTHREADTYPE_MAIN_WORKER,             32, 32 },
        { RTTHREADTYPE_VRDP_IO,                 39, 39 },
        { RTTHREADTYPE_DEBUGGER,                42, 42 },
        { RTTHREADTYPE_MSG_PUMP,                47, 47 },
        { RTTHREADTYPE_IO,                      52, 52 },
        { RTTHREADTYPE_TIMER,                   55, 55 }
    }
};


/** Pointer to the current priority configuration. */
static const PROCPRIORITY *g_pProcessPriority = &g_aDefaultPriority;


/**
 * Get's the priority information for the current thread.
 *
 * @returns The base priority
 * @param   pThread     The thread to get it for.  NULL for current.
 */
static int rtSchedDarwinGetBasePriority(PRTTHREADINT pThread)
{
    /* the base_priority. */
    mach_msg_type_number_t       Count  = POLICY_TIMESHARE_INFO_COUNT;
    struct policy_timeshare_info TSInfo = {0,0,0,0,0};
    kern_return_t krc = thread_info(!pThread ? mach_thread_self() : pthread_mach_thread_np((pthread_t)pThread->Core.Key),
                                    THREAD_SCHED_TIMESHARE_INFO, (thread_info_t)&TSInfo, &Count);
    Assert(krc == KERN_SUCCESS); NOREF(krc);

    return TSInfo.base_priority;
}


DECLHIDDEN(int) rtSchedNativeCalcDefaultPriority(RTTHREADTYPE enmType)
{
    Assert(enmType > RTTHREADTYPE_INVALID && enmType < RTTHREADTYPE_END);

    /*
     * Get the current priority.
     */
    int iBasePriority = rtSchedDarwinGetBasePriority(NULL);
    Assert(iBasePriority >= 0 && iBasePriority <= 63);

    /*
     * If it doesn't match the default, select the closest one from the table.
     */
    int offBest = RT_ABS(g_pProcessPriority->aTypes[enmType].iBasePriority - iBasePriority);
    if (offBest)
    {
        for (unsigned i = 0; i < RT_ELEMENTS(g_aPriorities); i++)
        {
            int off = RT_ABS(g_aPriorities[i].aTypes[enmType].iBasePriority - iBasePriority);
            if (off < offBest)
            {
                g_pProcessPriority = &g_aPriorities[i];
                if (!off)
                    break;
                offBest = off;
            }
        }
    }

    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtProcNativeSetPriority(RTPROCPRIORITY enmPriority)
{
    Assert(enmPriority > RTPROCPRIORITY_INVALID && enmPriority < RTPROCPRIORITY_LAST);

    /*
     * No checks necessary, we assume we can set any priority in the user process range.
     */
    const PROCPRIORITY *pProcessPriority = &g_aDefaultPriority;
    for (unsigned i = 0; i < RT_ELEMENTS(g_aPriorities); i++)
        if (g_aPriorities[i].enmPriority == enmPriority)
        {
            pProcessPriority = &g_aPriorities[i];
            break;
        }
    Assert(pProcessPriority != &g_aDefaultPriority);
    ASMAtomicUoWritePtr(&g_pProcessPriority, pProcessPriority);

    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtThreadNativeSetPriority(PRTTHREADINT pThread, RTTHREADTYPE enmType)
{
    Assert(enmType > RTTHREADTYPE_INVALID && enmType < RTTHREADTYPE_END);
    AssertMsg(g_pProcessPriority && g_pProcessPriority->aTypes[enmType].enmType == enmType,
              ("enmType=%d entry=%d\n", enmType, g_pProcessPriority->aTypes[enmType].enmType));

    /*
     * Get the current policy and params first since there are
     * opaque members in the param structure and we don't wish to
     * change the policy.
     */
    int iSchedPolicy = SCHED_OTHER;
    struct sched_param SchedParam = {0, {0,0,0,0} };
    int err = pthread_getschedparam((pthread_t)pThread->Core.Key, &iSchedPolicy, &SchedParam);
    if (!err)
    {
        int const iDesiredBasePriority = g_pProcessPriority->aTypes[enmType].iBasePriority;
        int       iPriority            = g_pProcessPriority->aTypes[enmType].iPriority;

        /*
         * First try with the given pthread priority number.
         * Then make adjustments in case we missed the desired base priority (interface
         * changed or whatever - its using an obsolete mach api).
         */
        SchedParam.sched_priority = iPriority;
        err = pthread_setschedparam((pthread_t)pThread->Core.Key, iSchedPolicy, &SchedParam);
        if (!err)
        {
            int i = 0;
            int iBasePriority = rtSchedDarwinGetBasePriority(pThread);

            while (   !err
                   && iBasePriority < iDesiredBasePriority
                   && i++ < 256)
            {
                SchedParam.sched_priority = ++iPriority;
                err = pthread_setschedparam((pthread_t)pThread->Core.Key, iSchedPolicy, &SchedParam);
                iBasePriority = rtSchedDarwinGetBasePriority(pThread);
            }

            while (   !err
                   && iPriority > 0
                   && iBasePriority > iDesiredBasePriority
                   && i++ < 256)
            {
                SchedParam.sched_priority = --iPriority;
                err = pthread_setschedparam((pthread_t)pThread->Core.Key, iSchedPolicy, &SchedParam);
                iBasePriority = rtSchedDarwinGetBasePriority(pThread);
            }

            return VINF_SUCCESS;
        }
    }
    int rc = RTErrConvertFromErrno(err);
    AssertMsgRC(rc, ("rc=%Rrc err=%d iSchedPolicy=%d sched_priority=%d\n",
                     rc, err, iSchedPolicy, SchedParam.sched_priority));
    return rc;
}

