/* $Id: sched-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#define THREAD_LOGGING
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_THREAD
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/resource.h>

#include <iprt/thread.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/err.h>
#include "internal/sched.h"
#include "internal/thread.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** Array scheduler attributes corresponding to each of the thread types. */
typedef struct PROCPRIORITYTYPE
{
    /** For sanity include the array index. */
    RTTHREADTYPE    enmType;
    /** The thread priority or nice delta - depends on which priority type. */
    int             iPriority;
} PROCPRIORITYTYPE;


/**
 * Configuration of one priority.
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


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Thread level priorities based on a 0..31 priority range
 * as specified as the minimum for SCHED_RR/FIFO. FreeBSD
 * seems to be using this (needs more research to be
 * certain).
 */
static const PROCPRIORITYTYPE g_aTypesThread[RTTHREADTYPE_END] =
{
    { RTTHREADTYPE_INVALID,                 -999999999 },
    { RTTHREADTYPE_INFREQUENT_POLLER,        5 },
    { RTTHREADTYPE_MAIN_HEAVY_WORKER,       12 },
    { RTTHREADTYPE_EMULATION,               14 },
    { RTTHREADTYPE_DEFAULT,                 15 },
    { RTTHREADTYPE_GUI,                     16 },
    { RTTHREADTYPE_MAIN_WORKER,             18 },
    { RTTHREADTYPE_VRDP_IO,                 24 },
    { RTTHREADTYPE_DEBUGGER,                28 },
    { RTTHREADTYPE_MSG_PUMP,                29 },
    { RTTHREADTYPE_IO,                      30 },
    { RTTHREADTYPE_TIMER,                   31 }
};

static const PROCPRIORITYTYPE g_aTypesThreadFlat[RTTHREADTYPE_END] =
{
    { RTTHREADTYPE_INVALID,                 ~0 },
    { RTTHREADTYPE_INFREQUENT_POLLER,       15 },
    { RTTHREADTYPE_MAIN_HEAVY_WORKER,       15 },
    { RTTHREADTYPE_EMULATION,               15 },
    { RTTHREADTYPE_DEFAULT,                 15 },
    { RTTHREADTYPE_GUI,                     15 },
    { RTTHREADTYPE_MAIN_WORKER,             15 },
    { RTTHREADTYPE_VRDP_IO,                 15 },
    { RTTHREADTYPE_DEBUGGER,                15 },
    { RTTHREADTYPE_MSG_PUMP,                15 },
    { RTTHREADTYPE_IO,                      15 },
    { RTTHREADTYPE_TIMER,                   15 }
};

/**
 * Process and thread level priority, full access at thread level.
 */
static const PROCPRIORITY   g_aProcessAndThread[] =
{
    { RTPROCPRIORITY_FLAT,      "Flat",      0, 0, g_aTypesThreadFlat },
    { RTPROCPRIORITY_LOW,       "Low",       9, 0, g_aTypesThread },
    { RTPROCPRIORITY_LOW,       "Low",      11, 0, g_aTypesThread },
    { RTPROCPRIORITY_LOW,       "Low",      15, 0, g_aTypesThread },
    { RTPROCPRIORITY_LOW,       "Low",      17, 0, g_aTypesThread },
    { RTPROCPRIORITY_LOW,       "Low",      19, 0, g_aTypesThread },
    { RTPROCPRIORITY_LOW,       "Low",       7, 0, g_aTypesThread },
    { RTPROCPRIORITY_LOW,       "Low",       5, 0, g_aTypesThread },
    { RTPROCPRIORITY_LOW,       "Low",       3, 0, g_aTypesThread },
    { RTPROCPRIORITY_LOW,       "Low",       1, 0, g_aTypesThread },
    { RTPROCPRIORITY_NORMAL,    "Normal",    0, 0, g_aTypesThread },
    { RTPROCPRIORITY_NORMAL,    "Normal",    0, 0, g_aTypesThreadFlat },
    { RTPROCPRIORITY_HIGH,      "High",     -9, 0, g_aTypesThread },
    { RTPROCPRIORITY_HIGH,      "High",     -7, 0, g_aTypesThread },
    { RTPROCPRIORITY_HIGH,      "High",     -5, 0, g_aTypesThread },
    { RTPROCPRIORITY_HIGH,      "High",     -3, 0, g_aTypesThread },
    { RTPROCPRIORITY_HIGH,      "High",     -1, 0, g_aTypesThread },
    { RTPROCPRIORITY_HIGH,      "High",     -9, 0, g_aTypesThreadFlat },
    { RTPROCPRIORITY_HIGH,      "High",     -1, 0, g_aTypesThreadFlat }
};

/**
 * Deltas for a process in which we are not restricted
 * to only be lowering the priority.
 */
static const PROCPRIORITYTYPE g_aTypesUnixFree[RTTHREADTYPE_END] =
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
 * Deltas for a process in which we are restricted
 * to only be lowering the priority.
 */
static const PROCPRIORITYTYPE g_aTypesUnixRestricted[RTTHREADTYPE_END] =
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
 * Deltas for a process in which we are restricted
 * to only be lowering the priority.
 */
static const PROCPRIORITYTYPE g_aTypesUnixFlat[RTTHREADTYPE_END] =
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
    { RTPROCPRIORITY_FLAT,      "Flat",      0,   0, g_aTypesUnixFlat },
    { RTPROCPRIORITY_LOW,       "Low",       9,   9, g_aTypesUnixFree },
    { RTPROCPRIORITY_LOW,       "Low",       9,   9, g_aTypesUnixFlat },
    { RTPROCPRIORITY_LOW,       "Low",      15,  15, g_aTypesUnixFree },
    { RTPROCPRIORITY_LOW,       "Low",      15,  15, g_aTypesUnixFlat },
    { RTPROCPRIORITY_LOW,       "Low",      17,  17, g_aTypesUnixFree },
    { RTPROCPRIORITY_LOW,       "Low",      17,  17, g_aTypesUnixFlat },
    { RTPROCPRIORITY_LOW,       "Low",      19,  19, g_aTypesUnixFlat },
    { RTPROCPRIORITY_LOW,       "Low",       9,   9, g_aTypesUnixRestricted },
    { RTPROCPRIORITY_LOW,       "Low",      15,  15, g_aTypesUnixRestricted },
    { RTPROCPRIORITY_LOW,       "Low",      17,  17, g_aTypesUnixRestricted },
    { RTPROCPRIORITY_NORMAL,    "Normal",    0,   0, g_aTypesUnixFree },
    { RTPROCPRIORITY_NORMAL,    "Normal",    0,   0, g_aTypesUnixRestricted },
    { RTPROCPRIORITY_NORMAL,    "Normal",    0,   0, g_aTypesUnixFlat },
    { RTPROCPRIORITY_HIGH,      "High",     -9,  -9, g_aTypesUnixFree },
    { RTPROCPRIORITY_HIGH,      "High",     -7,  -7, g_aTypesUnixFree },
    { RTPROCPRIORITY_HIGH,      "High",     -5,  -5, g_aTypesUnixFree },
    { RTPROCPRIORITY_HIGH,      "High",     -3,  -3, g_aTypesUnixFree },
    { RTPROCPRIORITY_HIGH,      "High",     -1,  -1, g_aTypesUnixFree },
    { RTPROCPRIORITY_HIGH,      "High",     -9,  -9, g_aTypesUnixRestricted },
    { RTPROCPRIORITY_HIGH,      "High",     -7,  -7, g_aTypesUnixRestricted },
    { RTPROCPRIORITY_HIGH,      "High",     -5,  -5, g_aTypesUnixRestricted },
    { RTPROCPRIORITY_HIGH,      "High",     -3,  -3, g_aTypesUnixRestricted },
    { RTPROCPRIORITY_HIGH,      "High",     -1,  -1, g_aTypesUnixRestricted },
    { RTPROCPRIORITY_HIGH,      "High",     -9,  -9, g_aTypesUnixFlat },
    { RTPROCPRIORITY_HIGH,      "High",     -7,  -7, g_aTypesUnixFlat },
    { RTPROCPRIORITY_HIGH,      "High",     -5,  -5, g_aTypesUnixFlat },
    { RTPROCPRIORITY_HIGH,      "High",     -3,  -3, g_aTypesUnixFlat },
    { RTPROCPRIORITY_HIGH,      "High",     -1,  -1, g_aTypesUnixFlat }
};

/**
 * The dynamic default priority configuration.
 *
 * This will be recalulated at runtime depending on what the
 * system allow us to do and what the current priority is.
 */
static PROCPRIORITY g_aDefaultPriority =
{
    RTPROCPRIORITY_LOW, "Default", 0, 0, g_aTypesUnixRestricted
};

/** Pointer to the current priority configuration. */
static const PROCPRIORITY *g_pProcessPriority = &g_aDefaultPriority;


/** Set to what kind of scheduling priority support the host
 * OS seems to be offering. Determined at runtime.
 */
static enum
{
    OSPRIOSUP_UNDETERMINED = 0,
    /** An excellent combination of process and thread level
     * I.e. setpriority() works on process level, one have to be supervisor
     * to raise priority as is the custom in unix. While pthread_setschedparam()
     * works on thread level and we can raise the priority just like we want.
     *
     * I think this is what FreeBSD offers. (It is certainly analogous to what
     * NT offers if you wondered.) Linux on the other hand doesn't provide this
     * for processes with SCHED_OTHER policy, and I'm not sure if we want to
     * play around with using the real-time SCHED_RR and SCHED_FIFO which would
     * require special privileges anyway.
     */
    OSPRIOSUP_PROCESS_AND_THREAD_LEVEL,
    /** A rough thread level priority only.
     * setpriority() is the only real game in town, and it works on thread level.
     */
    OSPRIOSUP_THREAD_LEVEL
}   volatile g_enmOsPrioSup = OSPRIOSUP_UNDETERMINED;

/** Set if we figure we have nice capability, meaning we can use setpriority
 * to raise the priority. */
static bool g_fCanNice = false;


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
 * Starts a worker thread and wait for it to complete.
 * We cannot use RTThreadCreate since we're already owner of the RW lock.
 */
static int rtSchedCreateThread(void *(*pfnThread)(void *pvArg), void *pvArg)
{
    /*
     * Setup thread attributes.
     */
    pthread_attr_t  ThreadAttr;
    int rc = pthread_attr_init(&ThreadAttr);
    if (!rc)
    {
        rc = pthread_attr_setdetachstate(&ThreadAttr, PTHREAD_CREATE_JOINABLE);
        if (!rc)
        {
            rc = pthread_attr_setstacksize(&ThreadAttr, 128*1024);
            if (!rc)
            {
                /*
                 * Create the thread.
                 */
                pthread_t Thread;
                rc = pthread_create(&Thread, &ThreadAttr, pfnThread, pvArg);
                if (!rc)
                {
                    pthread_attr_destroy(&ThreadAttr);
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
            }
        }
        pthread_attr_destroy(&ThreadAttr);
    }
    return RTErrConvertFromErrno(rc);
}


static void rtSchedDumpPriority(void)
{
#ifdef THREAD_LOGGING
    Log(("Priority: g_fCanNice=%d g_enmOsPrioSup=%d\n", g_fCanNice, g_enmOsPrioSup));
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
 * The prober thread.
 * We don't want to mess with the priority of the calling thread.
 *
 * @remark  This is pretty presumptive stuff, but if it works on Linux and
 *          FreeBSD it does what I want.
 */
static void *rtSchedNativeProberThread(void *pvUser)
{
    SAVEDPRIORITY SavedPriority;
    rtSchedNativeSave(&SavedPriority);

    /*
     * Let's first try and see what we get on a thread level.
     */
    int iMax = sched_get_priority_max(SavedPriority.iPthreadPolicy);
    int iMin = sched_get_priority_min(SavedPriority.iPthreadPolicy);
    if (iMax - iMin >= 32)
    {
        pthread_t Self = pthread_self();
        int i = iMin;
        while (i <= iMax)
        {
            struct sched_param SchedParam = SavedPriority.PthreadSchedParam;
            SchedParam.sched_priority = i;
            if (pthread_setschedparam(Self, SavedPriority.iPthreadPolicy, &SchedParam))
                break;
            i++;
        }
        if (i == iMax)
            g_enmOsPrioSup = OSPRIOSUP_PROCESS_AND_THREAD_LEVEL;
    }

    /*
     * Ok, we didn't have the good stuff, so let's fall back on the unix stuff.
     */
    if (g_enmOsPrioSup == OSPRIOSUP_UNDETERMINED)
        g_enmOsPrioSup = OSPRIOSUP_THREAD_LEVEL;

    /*
     * Check if we can get higher priority (typically only root can do this).
     * (Won't work right if our priority is -19 to start with, but what the heck.)
     *
     * We assume that the unix priority is -19 to 19. I know there are defines
     * for this, but I don't remember which and if I'm awake enough to make sense
     * of them from any SuS spec.
     */
    int iStart = getpriority(PRIO_PROCESS, 0);
    int i = iStart;
    while (i-- > -19)
    {
        if (setpriority(PRIO_PROCESS, 0, i))
            break;
    }
    if (getpriority(PRIO_PROCESS, 0) != iStart)
        g_fCanNice = true;
    else
        g_fCanNice = false;

    /* done */
    rtSchedNativeRestore(&SavedPriority);
    RT_NOREF(pvUser);
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
     * First figure out what's supported by the OS.
     */
    if (g_enmOsPrioSup == OSPRIOSUP_UNDETERMINED)
    {
        int iPriority = getpriority(PRIO_PROCESS, 0);
        int rc = rtSchedCreateThread(rtSchedNativeProberThread, NULL);
        if (RT_FAILURE(rc))
            return rc;
        if (g_enmOsPrioSup == OSPRIOSUP_UNDETERMINED)
            g_enmOsPrioSup = OSPRIOSUP_THREAD_LEVEL;
        Assert(getpriority(PRIO_PROCESS, 0) == iPriority); NOREF(iPriority);
    }

    /*
     * Now let's see what we can do...
     */
    int iPriority = getpriority(PRIO_PROCESS, 0);
    switch (g_enmOsPrioSup)
    {
        case OSPRIOSUP_PROCESS_AND_THREAD_LEVEL:
        {
            g_aDefaultPriority.iNice = iPriority;
            g_aDefaultPriority.iDelta = 0;
            g_aDefaultPriority.paTypes = g_aTypesThread;
            Assert(enmType == g_aDefaultPriority.paTypes[enmType].enmType);
            break;
        }

        case OSPRIOSUP_THREAD_LEVEL:
        {
            if (g_fCanNice)
                g_aDefaultPriority.paTypes = g_aTypesUnixFree;
            else
                g_aDefaultPriority.paTypes = g_aTypesUnixRestricted;
            Assert(enmType == g_aDefaultPriority.paTypes[enmType].enmType);
            g_aDefaultPriority.iNice = iPriority - g_aDefaultPriority.paTypes[enmType].iPriority;
            g_aDefaultPriority.iDelta = g_aDefaultPriority.iNice;
            break;
        }

        default:
            AssertFailed();
            break;
    }
    rtSchedDumpPriority();
    return VINF_SUCCESS;
}


/**
 * The validator thread.
 * We don't want to mess with the priority of the calling thread.
 *
 * @remark  This is pretty presumptive stuff, but if it works on Linux and
 *          FreeBSD it does what I want.
 */
static void *rtSchedNativeValidatorThread(void *pvUser)
{
    const PROCPRIORITY *pCfg = (const PROCPRIORITY *)pvUser;
    SAVEDPRIORITY SavedPriority;
    rtSchedNativeSave(&SavedPriority);

    int rc = VINF_SUCCESS;
    switch (g_enmOsPrioSup)
    {
        /*
         * Try set the specified process priority and then try
         * out all the thread priorities which are used.
         */
        case OSPRIOSUP_PROCESS_AND_THREAD_LEVEL:
        {
            if (!setpriority(PRIO_PROCESS, 0, pCfg->iNice))
            {
                int iMin = sched_get_priority_min(SavedPriority.iPolicy);
                pthread_t Self = pthread_self();
                for (int i = RTTHREADTYPE_INVALID + 1; i < RTTHREADTYPE_END; i++)
                {
                    struct sched_param SchedParam = SavedPriority.PthreadSchedParam;
                    SchedParam.sched_priority = pCfg->paTypes[i].iPriority
                        + pCfg->iDelta + iMin;
                    rc = pthread_setschedparam(Self, SavedPriority.iPthreadPolicy, &SchedParam);
                    if (rc)
                    {
                        rc = RTErrConvertFromErrno(rc);
                        break;
                    }
                }
            }
            else
                rc = RTErrConvertFromErrno(errno);
            break;
        }

        /*
         * Try out the priorities from the top and down.
         */
        case OSPRIOSUP_THREAD_LEVEL:
        {
            int i = RTTHREADTYPE_END;
            while (--i > RTTHREADTYPE_INVALID)
            {
                int iPriority = pCfg->paTypes[i].iPriority + pCfg->iDelta;
                if (setpriority(PRIO_PROCESS, 0, iPriority))
                {
                    rc = RTErrConvertFromErrno(errno);
                    break;
                }
            }
            break;
        }

        default:
            AssertFailed();
            break;
    }

    /* done */
    rtSchedNativeRestore(&SavedPriority);
    return (void *)(intptr_t)rc;
}


DECLHIDDEN(int) rtProcNativeSetPriority(RTPROCPRIORITY enmPriority)
{
    Assert(enmPriority > RTPROCPRIORITY_INVALID && enmPriority < RTPROCPRIORITY_LAST);

#ifdef RTTHREAD_POSIX_WITH_CREATE_PRIORITY_PROXY
    /*
     * Make sure the proxy creation thread is started so we don't 'lose' our
     * initial priority if it's lowered.
     */
    rtThreadPosixPriorityProxyStart();
#endif

    /*
     * Nothing to validate for the default priority (assuming no external renice).
     */
    int rc = VINF_SUCCESS;
    if (enmPriority == RTPROCPRIORITY_DEFAULT)
        g_pProcessPriority = &g_aDefaultPriority;
    else
    {
        /*
         * Select the array to search.
         */
        const PROCPRIORITY *pa;
        unsigned            c;
        switch (g_enmOsPrioSup)
        {
            case OSPRIOSUP_PROCESS_AND_THREAD_LEVEL:
                pa = g_aProcessAndThread;
                c = RT_ELEMENTS(g_aProcessAndThread);
                break;
            case OSPRIOSUP_THREAD_LEVEL:
                pa = g_aUnixConfigs;
                c = RT_ELEMENTS(g_aUnixConfigs);
                break;
            default:
                pa = NULL;
                c = 0;
                break;
        }

        /*
         * Search the array.
         */
        rc = VERR_FILE_NOT_FOUND;
        unsigned i;
        for (i = 0; i < c; i++)
        {
            if (pa[i].enmPriority == enmPriority)
            {
                /*
                 * Validate it.
                 */
                int iPriority = getpriority(PRIO_PROCESS, 0);
                int rc3 = rtSchedCreateThread(rtSchedNativeValidatorThread, (void *)&pa[i]);
                Assert(getpriority(PRIO_PROCESS, 0) == iPriority); NOREF(iPriority);
                if (RT_SUCCESS(rc))
                    rc = rc3;
                if (RT_SUCCESS(rc))
                    break;
            }
        }

        /*
         * Did we get lucky?
         * If so update process priority and globals.
         */
        if (RT_SUCCESS(rc))
        {
            switch (g_enmOsPrioSup)
            {
                case OSPRIOSUP_PROCESS_AND_THREAD_LEVEL:
                    if (setpriority(PRIO_PROCESS, 0, pa[i].iNice))
                    {
                        rc = RTErrConvertFromErrno(errno);
                        AssertMsgFailed(("setpriority(,,%d) -> errno=%d rc=%Rrc\n", pa[i].iNice, errno, rc));
                    }
                    break;

                default:
                    break;
            }

            if (RT_SUCCESS(rc))
                g_pProcessPriority = &pa[i];
        }
    }

#ifdef THREAD_LOGGING
    LogFlow(("rtProcNativeSetPriority: returns %Rrc enmPriority=%d\n", rc, enmPriority));
    rtSchedDumpPriority();
#endif
    return rc;
}


/**
 * Worker for rtThreadNativeSetPriority/OSPRIOSUP_PROCESS_AND_THREAD_LEVEL
 * that's either called on the priority proxy thread or directly if no proxy.
 */
static DECLCALLBACK(int) rtThreadPosixSetPriorityOnProcAndThrdCallback(PRTTHREADINT pThread, RTTHREADTYPE enmType)
{
    struct sched_param  SchedParam = {-9999999};
    int                 iPolicy = -7777777;
    int rc = pthread_getschedparam((pthread_t)pThread->Core.Key, &iPolicy, &SchedParam);
    if (!rc)
    {
        SchedParam.sched_priority = g_pProcessPriority->paTypes[enmType].iPriority
            + g_pProcessPriority->iDelta
            + sched_get_priority_min(iPolicy);

        rc = pthread_setschedparam((pthread_t)pThread->Core.Key, iPolicy, &SchedParam);
        if (!rc)
        {
#ifdef THREAD_LOGGING
            Log(("rtThreadNativeSetPriority: Thread=%p enmType=%d iPolicy=%d sched_priority=%d pid=%d\n",
                 pThread->Core.Key, enmType, iPolicy, SchedParam.sched_priority, getpid()));
#endif
            return VINF_SUCCESS;
        }
    }

    int rcNative = rc;
    rc = RTErrConvertFromErrno(rc);
    AssertMsgFailed(("pthread_[gs]etschedparam(%p, %d, {%d}) -> rcNative=%d rc=%Rrc\n",
                     (void *)pThread->Core.Key, iPolicy, SchedParam.sched_priority, rcNative, rc)); NOREF(rcNative);
    return rc;
}


DECLHIDDEN(int) rtThreadNativeSetPriority(PRTTHREADINT pThread, RTTHREADTYPE enmType)
{
    Assert(enmType > RTTHREADTYPE_INVALID && enmType < RTTHREADTYPE_END);
    Assert(enmType == g_pProcessPriority->paTypes[enmType].enmType);

    int rc = VINF_SUCCESS;
    switch (g_enmOsPrioSup)
    {
        case OSPRIOSUP_PROCESS_AND_THREAD_LEVEL:
        {
#ifdef RTTHREAD_POSIX_WITH_CREATE_PRIORITY_PROXY
            if (rtThreadPosixPriorityProxyStart())
                rc = rtThreadPosixPriorityProxyCall(pThread, (PFNRT)rtThreadPosixSetPriorityOnProcAndThrdCallback,
                                                    2, pThread, enmType);
            else
#endif
                rc = rtThreadPosixSetPriorityOnProcAndThrdCallback(pThread, enmType);
            break;
        }

        case OSPRIOSUP_THREAD_LEVEL:
        {
            /* No cross platform way of getting the 'who' parameter value for
               arbitrary threads, so this is restricted to the calling thread only. */
            AssertReturn((pthread_t)pThread->Core.Key == pthread_self(), VERR_NOT_SUPPORTED);

            int iPriority = g_pProcessPriority->paTypes[enmType].iPriority + g_pProcessPriority->iDelta;
            if (!setpriority(PRIO_PROCESS, 0, iPriority))
            {
                AssertMsg(iPriority == getpriority(PRIO_PROCESS, 0), ("iPriority=%d getpriority()=%d\n", iPriority, getpriority(PRIO_PROCESS, 0)));
#ifdef THREAD_LOGGING
                Log(("rtThreadNativeSetPriority: Thread=%p enmType=%d iPriority=%d pid=%d\n", pThread->Core.Key, enmType, iPriority, getpid()));
#endif
            }
            else
            {
#if 0
                rc = RTErrConvertFromErrno(errno);
                AssertMsgFailed(("setpriority(,, %d) -> errno=%d rc=%Rrc\n", iPriority, errno, rc));
#else
                /** @todo
                 * Just keep quiet about failures now - we'll fail here because we're not
                 * allowed to raise our own priority. This is a problem when starting the
                 * threads with higher priority from EMT (i.e. most threads it starts).
                 * This is apparently inherited from the parent in some cases and not
                 * in other cases. I guess this would come down to which kind of pthread
                 * implementation is actually in use, and how many sensible patches which
                 * are installed.
                 * I need to find a system where this problem shows up in order to come up
                 * with a proper fix. There's an pthread_create attribute for not inheriting
                 * scheduler stuff I think...
                 */
                rc = VINF_SUCCESS;
#endif
            }
            break;
        }

        /*
         * Any thread created before we determine the default config, remains unchanged!
         * The prober thread above is one of those.
         */
        default:
            break;
    }

    return rc;
}

