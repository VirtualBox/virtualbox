/* $Id: sched-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Scheduling, POSIX.
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

/*
 * !WARNING!
 *
 * When talking about lowering and raising priority, we do *NOT* refer to
 * the common direction priority values takes on unix systems (lower means
 * higher). So, when we raise the priority of a linux thread the nice
 * value will decrease, and when we lower the priority the nice value
 * will increase. Confusing, right?
 *
 * !WARNING!
 */



/** @def THREAD_LOGGING
 * Be very careful with enabling this, it may cause deadlocks when combined
 * with the 'thread' logging prefix.
 */
#ifdef DOXYGEN_RUNNING
# define THREAD_LOGGING
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_THREAD
#include <errno.h>
#include <pthread.h>
#include <limits.h>
#include <sched.h>
#include <unistd.h>
#include <sys/resource.h>

#include <iprt/thread.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/errcore.h>
#include "internal/sched.h"
#include "internal/thread.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** Array scheduler attributes corresponding to each of the thread types.
 * @internal */
typedef struct PROCPRIORITYTYPE
{
    /** For sanity include the array index. */
    RTTHREADTYPE    enmType;
    /** The thread priority or nice delta - depends on which priority type. */
    int             iPriority;
} PROCPRIORITYTYPE;


/**
 * Configuration of one priority.
 * @internal
 */
typedef struct
{
    /** The priority. */
    RTPROCPRIORITY  enmPriority;
    /** The name of this priority. */
    const char     *pszName;
    /** The process nice value. */
    int             iNice;
    /** The delta applied to the iPriority value. */
    int             iDelta;
    /** Array scheduler attributes corresponding to each of the thread types. */
    const PROCPRIORITYTYPE *paTypes;
} PROCPRIORITY;


/**
 * Saved priority settings
 * @internal
 */
typedef struct
{
    /** Process priority. */
    int                 iPriority;
    /** Process level. */
    struct sched_param  SchedParam;
    /** Process level. */
    int                 iPolicy;
    /** pthread level. */
    struct sched_param  PthreadSchedParam;
    /** pthread level. */
    int                 iPthreadPolicy;
} SAVEDPRIORITY, *PSAVEDPRIORITY;


/**
 * Priorities for checking by separate thread
 * @internal
 */
typedef struct
{
    /** The current thread priority to assume first. */
    int                 iCurrent;
    /** The thread priority to try set afterwards. */
    int                 iNew;
} VALIDATORPRIORITYPAIR, *PVALIDATORPRIORITYPAIR;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Deltas for a process in which we are not restricted
 * to only be lowering the priority.
 */
static const PROCPRIORITYTYPE g_aTypesLinuxFree[RTTHREADTYPE_END] =
{
    { RTTHREADTYPE_INVALID,                 -999999999 },
    { RTTHREADTYPE_INFREQUENT_POLLER,       +3 },
    { RTTHREADTYPE_MAIN_HEAVY_WORKER,       +2 },
    { RTTHREADTYPE_EMULATION,               +1 },
    { RTTHREADTYPE_DEFAULT,                  0 },
    { RTTHREADTYPE_GUI,                      0 },
    { RTTHREADTYPE_MAIN_WORKER,              0 },
    { RTTHREADTYPE_VRDP_IO,                 -1 },
    { RTTHREADTYPE_DEBUGGER,                -1 },
    { RTTHREADTYPE_MSG_PUMP,                -2 },
    { RTTHREADTYPE_IO,                      -3 },
    { RTTHREADTYPE_TIMER,                   -4 }
};

/**
 * Deltas for a process in which we are restricted and can only lower the priority.
 */
static const PROCPRIORITYTYPE g_aTypesLinuxRestricted[RTTHREADTYPE_END] =
{
    { RTTHREADTYPE_INVALID,                 -999999999 },
    { RTTHREADTYPE_INFREQUENT_POLLER,       +3 },
    { RTTHREADTYPE_MAIN_HEAVY_WORKER,       +2 },
    { RTTHREADTYPE_EMULATION,               +1 },
    { RTTHREADTYPE_DEFAULT,                  0 },
    { RTTHREADTYPE_GUI,                      0 },
    { RTTHREADTYPE_MAIN_WORKER,              0 },
    { RTTHREADTYPE_VRDP_IO,                  0 },
    { RTTHREADTYPE_DEBUGGER,                 0 },
    { RTTHREADTYPE_MSG_PUMP,                 0 },
    { RTTHREADTYPE_IO,                       0 },
    { RTTHREADTYPE_TIMER,                    0 }
};

/**
 * All threads have the same priority.
 *
 * This is typically chosen when we find that we can't raise the priority
 * to the process default of a thread created by a low priority thread.
 */
static const PROCPRIORITYTYPE g_aTypesLinuxFlat[RTTHREADTYPE_END] =
{
    { RTTHREADTYPE_INVALID,                 -999999999 },
    { RTTHREADTYPE_INFREQUENT_POLLER,        0 },
    { RTTHREADTYPE_MAIN_HEAVY_WORKER,        0 },
    { RTTHREADTYPE_EMULATION,                0 },
    { RTTHREADTYPE_DEFAULT,                  0 },
    { RTTHREADTYPE_GUI,                      0 },
    { RTTHREADTYPE_MAIN_WORKER,              0 },
    { RTTHREADTYPE_VRDP_IO,                  0 },
    { RTTHREADTYPE_DEBUGGER,                 0 },
    { RTTHREADTYPE_MSG_PUMP,                 0 },
    { RTTHREADTYPE_IO,                       0 },
    { RTTHREADTYPE_TIMER,                    0 }
};

/**
 * Process and thread level priority, full access at thread level.
 */
static const PROCPRIORITY   g_aUnixConfigs[] =
{
    { RTPROCPRIORITY_FLAT,      "Flat",      0,   0, g_aTypesLinuxFlat },
    { RTPROCPRIORITY_LOW,       "Low",       9,   9, g_aTypesLinuxFree },
    { RTPROCPRIORITY_LOW,       "Low",       9,   9, g_aTypesLinuxFlat },
    { RTPROCPRIORITY_LOW,       "Low",      15,  15, g_aTypesLinuxFree },
    { RTPROCPRIORITY_LOW,       "Low",      15,  15, g_aTypesLinuxFlat },
    { RTPROCPRIORITY_LOW,       "Low",      17,  17, g_aTypesLinuxFree },
    { RTPROCPRIORITY_LOW,       "Low",      17,  17, g_aTypesLinuxFlat },
    { RTPROCPRIORITY_LOW,       "Low",      19,  19, g_aTypesLinuxFlat },
    { RTPROCPRIORITY_LOW,       "Low",       9,   9, g_aTypesLinuxRestricted },
    { RTPROCPRIORITY_LOW,       "Low",      15,  15, g_aTypesLinuxRestricted },
    { RTPROCPRIORITY_LOW,       "Low",      17,  17, g_aTypesLinuxRestricted },
    { RTPROCPRIORITY_NORMAL,    "Normal",    0,   0, g_aTypesLinuxFree },
    { RTPROCPRIORITY_NORMAL,    "Normal",    0,   0, g_aTypesLinuxRestricted },
    { RTPROCPRIORITY_NORMAL,    "Normal",    0,   0, g_aTypesLinuxFlat },
    { RTPROCPRIORITY_HIGH,      "High",     -9,  -9, g_aTypesLinuxFree },
    { RTPROCPRIORITY_HIGH,      "High",     -7,  -7, g_aTypesLinuxFree },
    { RTPROCPRIORITY_HIGH,      "High",     -5,  -5, g_aTypesLinuxFree },
    { RTPROCPRIORITY_HIGH,      "High",     -3,  -3, g_aTypesLinuxFree },
    { RTPROCPRIORITY_HIGH,      "High",     -1,  -1, g_aTypesLinuxFree },
    { RTPROCPRIORITY_HIGH,      "High",     -9,  -9, g_aTypesLinuxRestricted },
    { RTPROCPRIORITY_HIGH,      "High",     -7,  -7, g_aTypesLinuxRestricted },
    { RTPROCPRIORITY_HIGH,      "High",     -5,  -5, g_aTypesLinuxRestricted },
    { RTPROCPRIORITY_HIGH,      "High",     -3,  -3, g_aTypesLinuxRestricted },
    { RTPROCPRIORITY_HIGH,      "High",     -1,  -1, g_aTypesLinuxRestricted },
    { RTPROCPRIORITY_HIGH,      "High",     -9,  -9, g_aTypesLinuxFlat },
    { RTPROCPRIORITY_HIGH,      "High",     -7,  -7, g_aTypesLinuxFlat },
    { RTPROCPRIORITY_HIGH,      "High",     -5,  -5, g_aTypesLinuxFlat },
    { RTPROCPRIORITY_HIGH,      "High",     -3,  -3, g_aTypesLinuxFlat },
    { RTPROCPRIORITY_HIGH,      "High",     -1,  -1, g_aTypesLinuxFlat }
};

/**
 * The dynamic default priority configuration.
 *
 * This will be recalulated at runtime depending on what the
 * system allow us to do and what the current priority is.
 */
static PROCPRIORITY g_aDefaultPriority =
{
    RTPROCPRIORITY_LOW, "Default", 0, 0, g_aTypesLinuxRestricted
};

/** Pointer to the current priority configuration. */
static const PROCPRIORITY *g_pProcessPriority = &g_aDefaultPriority;

/** Set if we can raise the priority of a thread beyond the default.
 *
 * It might mean we have the CAP_SYS_NICE capability or that the
 * process's RLIMIT_NICE is higher than the priority of the thread
 * calculating the defaults.
 */
static bool g_fCanRaisePriority = false;

/** Set if we can restore the priority after having temporarily lowered or raised it. */
static bool g_fCanRestorePriority = false;

/** Set if we can NOT raise the priority to the process default in a thread
 * created by a thread running below the process default.
 */
static bool g_fScrewedUpMaxPriorityLimitInheritance = true;

/** The highest priority we can set. */
static int  g_iMaxPriority = 0;

/** The lower priority we can set. */
static int  g_iMinPriority = 19;

/** Set when we've successfully determined the capabilities of the process and kernel. */
static bool g_fInitialized = false;



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Saves all the scheduling attributes we can think of.
 */
static void rtSchedNativeSave(PSAVEDPRIORITY pSave)
{
    memset(pSave, 0xff, sizeof(*pSave));

    errno = 0;
    pSave->iPriority = getpriority(PRIO_PROCESS, 0 /* current process */);
    Assert(errno == 0);

    errno = 0;
    sched_getparam(0 /* current process */, &pSave->SchedParam);
    Assert(errno == 0);

    errno = 0;
    pSave->iPolicy = sched_getscheduler(0 /* current process */);
    Assert(errno == 0);

    int rc = pthread_getschedparam(pthread_self(), &pSave->iPthreadPolicy, &pSave->PthreadSchedParam);
    Assert(rc == 0); NOREF(rc);
}


/**
 * Restores scheduling attributes.
 * Most of this won't work right, but anyway...
 */
static void rtSchedNativeRestore(PSAVEDPRIORITY pSave)
{
    setpriority(PRIO_PROCESS, 0, pSave->iPriority);
    sched_setscheduler(0, pSave->iPolicy, &pSave->SchedParam);
    sched_setparam(0, &pSave->SchedParam);
    pthread_setschedparam(pthread_self(), pSave->iPthreadPolicy, &pSave->PthreadSchedParam);
}


/**
 * Called on the priority proxy thread if requested running, otherwise
 * rtSchedRunThread() calls it directly.
 */
static DECLCALLBACK(int) rtSchedRunThreadCallback(pthread_t *pThread, void *(*pfnThread)(void *pvArg), void *pvArg)
{
    int rc = pthread_create(pThread, NULL, pfnThread, pvArg);
    if (!rc)
        return VINF_SUCCESS;
    return RTErrConvertFromErrno(rc);
}


/**
 * Starts a worker thread and wait for it to complete.
 *
 * We cannot use RTThreadCreate since we're already owner of the RW lock.
 */
static int rtSchedRunThread(void *(*pfnThread)(void *pvArg), void *pvArg, bool fUsePriorityProxy)
{
    /*
     * Create the thread.
     */
    pthread_t Thread;
    int rc;
#ifndef RTTHREAD_POSIX_WITH_CREATE_PRIORITY_PROXY
    RT_NOREF(fUsePriorityProxy);
#else
    if (   fUsePriorityProxy
        && rtThreadPosixPriorityProxyStart())
        rc = rtThreadPosixPriorityProxyCall(NULL, (PFNRT)rtSchedRunThreadCallback, 3, &Thread, pfnThread, pvArg);
    else
#endif
        rc = rtSchedRunThreadCallback(&Thread, pfnThread, pvArg);
    if (RT_SUCCESS(rc))
    {
        /*
         * Wait for the thread to finish.
         */
        void *pvRet = (void *)-1;
        do
        {
            rc = pthread_join(Thread, &pvRet);
        } while (rc == EINTR);
        if (rc)
            return RTErrConvertFromErrno(rc);
        return (int)(uintptr_t)pvRet;
    }
    return rc;
}


static void rtSchedDumpPriority(void)
{
#ifdef THREAD_LOGGING
    Log(("Priority: g_fCanRaisePriority=%RTbool g_fCanRestorePriority=%RTbool g_fScrewedUpMaxPriorityLimitInheritance=%RTbool\n",
         g_fCanRaisePriority, g_fCanRestorePriority, g_fScrewedUpMaxPriorityLimitInheritance));
    Log(("Priority: g_iMaxPriority=%d g_iMinPriority=%d\n", g_iMaxPriority, g_iMinPriority));
    Log(("Priority: enmPriority=%d \"%s\" iNice=%d iDelta=%d\n",
         g_pProcessPriority->enmPriority,
         g_pProcessPriority->pszName,
         g_pProcessPriority->iNice,
         g_pProcessPriority->iDelta));
    Log(("Priority:  %2d INFREQUENT_POLLER = %d\n", RTTHREADTYPE_INFREQUENT_POLLER, g_pProcessPriority->paTypes[RTTHREADTYPE_INFREQUENT_POLLER].iPriority));
    Log(("Priority:  %2d MAIN_HEAVY_WORKER = %d\n", RTTHREADTYPE_MAIN_HEAVY_WORKER, g_pProcessPriority->paTypes[RTTHREADTYPE_MAIN_HEAVY_WORKER].iPriority));
    Log(("Priority:  %2d EMULATION         = %d\n", RTTHREADTYPE_EMULATION        , g_pProcessPriority->paTypes[RTTHREADTYPE_EMULATION        ].iPriority));
    Log(("Priority:  %2d DEFAULT           = %d\n", RTTHREADTYPE_DEFAULT          , g_pProcessPriority->paTypes[RTTHREADTYPE_DEFAULT          ].iPriority));
    Log(("Priority:  %2d GUI               = %d\n", RTTHREADTYPE_GUI              , g_pProcessPriority->paTypes[RTTHREADTYPE_GUI              ].iPriority));
    Log(("Priority:  %2d MAIN_WORKER       = %d\n", RTTHREADTYPE_MAIN_WORKER      , g_pProcessPriority->paTypes[RTTHREADTYPE_MAIN_WORKER      ].iPriority));
    Log(("Priority:  %2d VRDP_IO           = %d\n", RTTHREADTYPE_VRDP_IO          , g_pProcessPriority->paTypes[RTTHREADTYPE_VRDP_IO          ].iPriority));
    Log(("Priority:  %2d DEBUGGER          = %d\n", RTTHREADTYPE_DEBUGGER         , g_pProcessPriority->paTypes[RTTHREADTYPE_DEBUGGER         ].iPriority));
    Log(("Priority:  %2d MSG_PUMP          = %d\n", RTTHREADTYPE_MSG_PUMP         , g_pProcessPriority->paTypes[RTTHREADTYPE_MSG_PUMP         ].iPriority));
    Log(("Priority:  %2d IO                = %d\n", RTTHREADTYPE_IO               , g_pProcessPriority->paTypes[RTTHREADTYPE_IO               ].iPriority));
    Log(("Priority:  %2d TIMER             = %d\n", RTTHREADTYPE_TIMER            , g_pProcessPriority->paTypes[RTTHREADTYPE_TIMER            ].iPriority));
#endif
}


/**
 * This just checks if it can raise the priority after having been
 * created by a thread with a low priority.
 *
 * @returns zero on success, non-zero on failure.
 * @param   pvUser  The priority of the parent before it was lowered (cast to int).
 */
static void *rtSchedNativeSubProberThread(void *pvUser)
{
    int iPriority = getpriority(PRIO_PROCESS, 0);
    Assert(iPriority == g_iMinPriority);

    if (setpriority(PRIO_PROCESS, 0, iPriority + 1))
        return (void *)-1;
    if (setpriority(PRIO_PROCESS, 0, (int)(intptr_t)pvUser))
        return (void *)-1;
    return (void *)0;
}


/**
 * The prober thread.
 * We don't want to mess with the priority of the calling thread.
 *
 * @remark  This is pretty presumptive stuff, but if it works on Linux and
 *          FreeBSD it does what I want.
 */
static void *rtSchedNativeProberThread(void *pvUser)
{
    NOREF(pvUser);
    SAVEDPRIORITY SavedPriority;
    rtSchedNativeSave(&SavedPriority);

    /*
     * Check if we can get higher priority (typically only root can do this).
     * (Won't work right if our priority is -19 to start with, but what the heck.)
     *
     * We assume that the priority range is -19 to 19. Should probably find the right
     * define for this.
     */
    int iStart = getpriority(PRIO_PROCESS, 0);
    int i = iStart;
    while (i-- > -20)
        if (setpriority(PRIO_PROCESS, 0, i))
            break;
    g_iMaxPriority = getpriority(PRIO_PROCESS, 0);
    g_fCanRaisePriority = g_iMaxPriority < iStart;
    g_fCanRestorePriority = setpriority(PRIO_PROCESS, 0, iStart) == 0;

    /*
     * Check if we temporarily lower the thread priority.
     * Again, we assume we're not at the extreme end of the priority scale.
     */
    iStart = getpriority(PRIO_PROCESS, 0);
    i = iStart;
    while (i++ < 19)
        if (setpriority(PRIO_PROCESS, 0, i))
            break;
    g_iMinPriority = getpriority(PRIO_PROCESS, 0);
    if (    setpriority(PRIO_PROCESS, 0, iStart)
        ||  getpriority(PRIO_PROCESS, 0) != iStart)
        g_fCanRestorePriority = false;
    if (g_iMinPriority == g_iMaxPriority)
        g_fCanRestorePriority = g_fCanRaisePriority = false;

    /*
     * Check what happens to child threads when the parent lowers the
     * priority when it's being created.
     */
    iStart = getpriority(PRIO_PROCESS, 0);
    g_fScrewedUpMaxPriorityLimitInheritance = true;
    if (    g_fCanRestorePriority
        &&  !setpriority(PRIO_PROCESS, 0, g_iMinPriority)
        &&  iStart != g_iMinPriority)
    {
        if (rtSchedRunThread(rtSchedNativeSubProberThread, (void *)(intptr_t)iStart, false /*fUsePriorityProxy*/) == 0)
            g_fScrewedUpMaxPriorityLimitInheritance = false;
    }

    /* done */
    rtSchedNativeRestore(&SavedPriority);
    return (void *)VINF_SUCCESS;
}


/**
 * Calculate the scheduling properties for all the threads in the default
 * process priority, assuming the current thread have the type enmType.
 *
 * @returns iprt status code.
 * @param   enmType     The thread type to be assumed for the current thread.
 */
DECLHIDDEN(int) rtSchedNativeCalcDefaultPriority(RTTHREADTYPE enmType)
{
    Assert(enmType > RTTHREADTYPE_INVALID && enmType < RTTHREADTYPE_END);

    /*
     * First figure out what's we're allowed to do in this process.
     */
    if (!g_fInitialized)
    {
        int iPriority = getpriority(PRIO_PROCESS, 0);
#ifdef RLIMIT_RTPRIO
        /** @todo */
#endif
        int rc = rtSchedRunThread(rtSchedNativeProberThread, NULL, false /*fUsePriorityProxy*/);
        if (RT_FAILURE(rc))
            return rc;
        Assert(getpriority(PRIO_PROCESS, 0) == iPriority); NOREF(iPriority);
        g_fInitialized = true;
    }

    /*
     * Select the right priority type table and update the default
     * process priority structure.
     */
    if (g_fCanRaisePriority && g_fCanRestorePriority && !g_fScrewedUpMaxPriorityLimitInheritance)
        g_aDefaultPriority.paTypes = &g_aTypesLinuxFree[0];
    else if (!g_fCanRaisePriority && g_fCanRestorePriority && !g_fScrewedUpMaxPriorityLimitInheritance)
        g_aDefaultPriority.paTypes = &g_aTypesLinuxRestricted[0];
    else
        g_aDefaultPriority.paTypes = &g_aTypesLinuxFlat[0];
    Assert(enmType == g_aDefaultPriority.paTypes[enmType].enmType);

    int iPriority = getpriority(PRIO_PROCESS, 0 /* current process */);
    g_aDefaultPriority.iNice = iPriority - g_aDefaultPriority.paTypes[enmType].iPriority;
    g_aDefaultPriority.iDelta = g_aDefaultPriority.iNice;

    rtSchedDumpPriority();
    return VINF_SUCCESS;
}


/**
 * The process priority validator thread.
 * (We don't want to mess with the priority of the calling thread.)
 */
static void *rtSchedNativeValidatorThread(void *pvUser)
{
    PVALIDATORPRIORITYPAIR pPrioPair = (PVALIDATORPRIORITYPAIR)pvUser;
    SAVEDPRIORITY SavedPriority;
    rtSchedNativeSave(&SavedPriority);

    int rc = VINF_SUCCESS;

    /*
     * Set the priority to the current value for specified thread type, but
     * only if we have any threads of this type (caller checked - INT_MAX).
     */
    if (pPrioPair->iCurrent != INT_MAX)
        if (setpriority(PRIO_PROCESS, 0, pPrioPair->iCurrent))
            rc = RTErrConvertFromErrno(errno);

    /*
     * Try set the new priority.
     */
    if (RT_SUCCESS(rc) && setpriority(PRIO_PROCESS, 0, pPrioPair->iNew))
        rc = RTErrConvertFromErrno(errno);

    /* done */
    rtSchedNativeRestore(&SavedPriority);
    return (void *)(intptr_t)rc;
}


/**
 * Validates the ability to apply suggested priority scheme.
 *
 * The function checks that we're able to apply all the thread types in the
 * suggested priority scheme.
 *
 * @returns iprt status code.
 * @param   pCfg                The priority scheme to validate.
 * @param   fHavePriorityProxy  Set if we've got a priority proxy thread,
 *                              otherwise clear.
 */
static int rtSchedNativeCheckThreadTypes(const PROCPRIORITY *pCfg, bool fHavePriorityProxy)
{
    int i = RTTHREADTYPE_END;
    while (--i > RTTHREADTYPE_INVALID)
    {
        VALIDATORPRIORITYPAIR PrioPair;
        PrioPair.iCurrent = g_pProcessPriority->paTypes[i].iPriority + g_pProcessPriority->iDelta;
        PrioPair.iNew     = pCfg->paTypes[i].iPriority               + pCfg->iDelta;
        if (g_acRTThreadTypeStats[i] == 0)
            PrioPair.iCurrent = INT_MAX;

#ifdef RT_STRICT
        int const iPriority = getpriority(PRIO_PROCESS, 0);
#endif
        int rc = rtSchedRunThread(rtSchedNativeValidatorThread, &PrioPair, fHavePriorityProxy /*fUsePriorityProxy*/);
        Assert(getpriority(PRIO_PROCESS, 0) == iPriority);

        if (RT_FAILURE(rc))
            return rc;
    }
    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtProcNativeSetPriority(RTPROCPRIORITY enmPriority)
{
    Assert(enmPriority > RTPROCPRIORITY_INVALID && enmPriority < RTPROCPRIORITY_LAST);

#ifdef RTTHREAD_POSIX_WITH_CREATE_PRIORITY_PROXY
    /*
     * Make sure the proxy creation thread is started so we don't 'lose' our
     * initial priority if it's lowered.
     */
    bool const fHavePriorityProxy = rtThreadPosixPriorityProxyStart();
#else
    bool const fHavePriorityProxy = false;
#endif

    int rc;
    if (enmPriority == RTPROCPRIORITY_DEFAULT)
    {
        /*
         * If we've lowered priority since the process started, it may be impossible
         * to raise it again for existing thread (new threads will work fine).
         */
        rc = rtSchedNativeCheckThreadTypes(&g_aDefaultPriority, fHavePriorityProxy);
        if (RT_SUCCESS(rc))
            g_pProcessPriority = &g_aDefaultPriority;
    }
    else
    {
        /*
         * Find a configuration which matches and can be applied.
         */
        rc = VERR_NOT_FOUND;
        for (unsigned i = 0; i < RT_ELEMENTS(g_aUnixConfigs); i++)
            if (g_aUnixConfigs[i].enmPriority == enmPriority)
            {
                int rc2 = rtSchedNativeCheckThreadTypes(&g_aUnixConfigs[i], fHavePriorityProxy);
                if (RT_SUCCESS(rc2))
                {
                    g_pProcessPriority = &g_aUnixConfigs[i];
                    rc = VINF_SUCCESS;
                    break;
                }
                if (rc == VERR_NOT_FOUND || rc == VERR_ACCESS_DENIED)
                    rc = rc2;
            }
    }

#ifdef THREAD_LOGGING
    LogFlow(("rtProcNativeSetPriority: returns %Rrc enmPriority=%d\n", rc, enmPriority));
    rtSchedDumpPriority();
#endif
    return rc;
}


/**
 * Called on the priority proxy thread if it's running, otherwise
 * rtThreadNativeSetPriority calls it directly.
 */
static DECLCALLBACK(int) rtThreadLinuxSetPriorityCallback(PRTTHREADINT pThread, int iPriority)
{
    if (!setpriority(PRIO_PROCESS, pThread->tid, iPriority))
    {
        AssertMsg(iPriority == getpriority(PRIO_PROCESS, pThread->tid),
                  ("iPriority=%d getpriority()=%d\n", iPriority, getpriority(PRIO_PROCESS, pThread->tid)));
#ifdef THREAD_LOGGING
        Log(("rtThreadNativeSetPriority: Thread=%p enmType=%d iPriority=%d pid=%d tid=%d\n",
             pThread->Core.Key, enmType, iPriority, getpid(), pThread->tid));
#endif
        return VINF_SUCCESS;
    }
    AssertMsgFailed(("setpriority(,, %d) -> errno=%d rc=%Rrc\n", iPriority, errno, RTErrConvertFromErrno(errno)));
    return VINF_SUCCESS; //non-fatal for now.
}


DECLHIDDEN(int) rtThreadNativeSetPriority(PRTTHREADINT pThread, RTTHREADTYPE enmType)
{
    /* sanity */
    Assert(enmType > RTTHREADTYPE_INVALID && enmType < RTTHREADTYPE_END);
    Assert(enmType == g_pProcessPriority->paTypes[enmType].enmType);

    /*
     * The thread ID is zero for alien threads, so skip these or we'd risk
     * modifying our own priority.
     */
    if (!pThread->tid)
        return VINF_SUCCESS;

    /*
     * Calculate the thread priority and apply it, preferrably via the priority proxy thread.
     */
    int const iPriority = g_pProcessPriority->paTypes[enmType].iPriority + g_pProcessPriority->iDelta;
#ifdef RTTHREAD_POSIX_WITH_CREATE_PRIORITY_PROXY
    if (rtThreadPosixPriorityProxyStart())
        return rtThreadPosixPriorityProxyCall(pThread, (PFNRT)rtThreadLinuxSetPriorityCallback, 2, pThread, iPriority);
#endif
    return rtThreadLinuxSetPriorityCallback(pThread, iPriority);
}

