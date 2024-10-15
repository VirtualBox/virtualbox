/* $Id: sched-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Scheduling, Win32.
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

/** @def WIN32_SCHED_ENABLED
 * Enables the priority scheme. */
#define WIN32_SCHED_ENABLED


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_THREAD
#include <iprt/win/windows.h>

#include <iprt/thread.h>
#include <iprt/log.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
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
    /** The Win32 process priority class. If ANY_PROCESS_PRIORITY_CLASS the
     * process priority class is left unchanged. */
    DWORD dwProcessPriorityClass;
    /** Array scheduler attributes corresponding to each of the thread types. */
    struct
    {
        /** For sanity include the array index. */
        RTTHREADTYPE    enmType;
        /** The Win32 thread priority. */
        int             iThreadPriority;
    } aTypes[RTTHREADTYPE_END];
} PROCPRIORITY;

/** Matches any process priority class. */
#define ANY_PROCESS_PRIORITY_CLASS  (~0U)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Array of static priority configurations.
 */
static const PROCPRIORITY g_aPriorities[] =
{
    {
        RTPROCPRIORITY_FLAT, "Flat", ANY_PROCESS_PRIORITY_CLASS,
        {
            { RTTHREADTYPE_INVALID,                 ~0 },
            { RTTHREADTYPE_INFREQUENT_POLLER,       THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_EMULATION,               THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_DEFAULT,                 THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_GUI,                     THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_MAIN_WORKER,             THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_VRDP_IO,                 THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_DEBUGGER,                THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_MSG_PUMP,                THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_IO,                      THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_TIMER,                   THREAD_PRIORITY_NORMAL }
        }
    },
    {
        RTPROCPRIORITY_LOW, "Low - Below Normal", BELOW_NORMAL_PRIORITY_CLASS,
        {
            { RTTHREADTYPE_INVALID,                 ~0 },
            { RTTHREADTYPE_INFREQUENT_POLLER,       THREAD_PRIORITY_LOWEST },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       THREAD_PRIORITY_BELOW_NORMAL },
            { RTTHREADTYPE_EMULATION,               THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_DEFAULT,                 THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_GUI,                     THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_MAIN_WORKER,             THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_VRDP_IO,                 THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_DEBUGGER,                THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_MSG_PUMP,                THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_IO,                      THREAD_PRIORITY_HIGHEST },
            { RTTHREADTYPE_TIMER,                   THREAD_PRIORITY_HIGHEST }
        }
    },
    {
        RTPROCPRIORITY_LOW, "Low", ANY_PROCESS_PRIORITY_CLASS,
        {
            { RTTHREADTYPE_INVALID,                 ~0 },
            { RTTHREADTYPE_INFREQUENT_POLLER,       THREAD_PRIORITY_LOWEST },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       THREAD_PRIORITY_LOWEST },
            { RTTHREADTYPE_EMULATION,               THREAD_PRIORITY_LOWEST },
            { RTTHREADTYPE_DEFAULT,                 THREAD_PRIORITY_BELOW_NORMAL },
            { RTTHREADTYPE_GUI,                     THREAD_PRIORITY_BELOW_NORMAL },
            { RTTHREADTYPE_MAIN_WORKER,             THREAD_PRIORITY_BELOW_NORMAL },
            { RTTHREADTYPE_VRDP_IO,                 THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_DEBUGGER,                THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_MSG_PUMP,                THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_IO,                      THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_TIMER,                   THREAD_PRIORITY_NORMAL }
        }
    },
    {
        RTPROCPRIORITY_NORMAL, "Normal - Normal", NORMAL_PRIORITY_CLASS,
        {
            { RTTHREADTYPE_INVALID,                 ~0 },
            { RTTHREADTYPE_INFREQUENT_POLLER,       THREAD_PRIORITY_LOWEST },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       THREAD_PRIORITY_LOWEST },
            { RTTHREADTYPE_EMULATION,               THREAD_PRIORITY_BELOW_NORMAL },
            { RTTHREADTYPE_DEFAULT,                 THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_GUI,                     THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_MAIN_WORKER,             THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_VRDP_IO,                 THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_DEBUGGER,                THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_MSG_PUMP,                THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_IO,                      THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_TIMER,                   THREAD_PRIORITY_HIGHEST }
        }
    },
    {
        RTPROCPRIORITY_NORMAL, "Normal", ANY_PROCESS_PRIORITY_CLASS,
        {
            { RTTHREADTYPE_INVALID,                 ~0 },
            { RTTHREADTYPE_INFREQUENT_POLLER,       THREAD_PRIORITY_LOWEST },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       THREAD_PRIORITY_LOWEST },
            { RTTHREADTYPE_EMULATION,               THREAD_PRIORITY_BELOW_NORMAL },
            { RTTHREADTYPE_DEFAULT,                 THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_GUI,                     THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_MAIN_WORKER,             THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_VRDP_IO,                 THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_DEBUGGER,                THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_MSG_PUMP,                THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_IO,                      THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_TIMER,                   THREAD_PRIORITY_HIGHEST }
        }
    },
    {
        RTPROCPRIORITY_HIGH, "High - High", HIGH_PRIORITY_CLASS,
        {
            { RTTHREADTYPE_INVALID,                 ~0 },
            { RTTHREADTYPE_INFREQUENT_POLLER,       THREAD_PRIORITY_LOWEST },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       THREAD_PRIORITY_LOWEST },
            { RTTHREADTYPE_EMULATION,               THREAD_PRIORITY_BELOW_NORMAL },
            { RTTHREADTYPE_DEFAULT,                 THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_GUI,                     THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_MAIN_WORKER,             THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_VRDP_IO,                 THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_DEBUGGER,                THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_MSG_PUMP,                THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_IO,                      THREAD_PRIORITY_HIGHEST },
            { RTTHREADTYPE_TIMER,                   THREAD_PRIORITY_HIGHEST }
        }
    },
    {
        RTPROCPRIORITY_HIGH, "High - Above Normal", ABOVE_NORMAL_PRIORITY_CLASS,
        {
            { RTTHREADTYPE_INVALID,                 ~0 },
            { RTTHREADTYPE_INFREQUENT_POLLER,       THREAD_PRIORITY_LOWEST },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       THREAD_PRIORITY_LOWEST },
            { RTTHREADTYPE_EMULATION,               THREAD_PRIORITY_BELOW_NORMAL },
            { RTTHREADTYPE_DEFAULT,                 THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_GUI,                     THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_MAIN_WORKER,             THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_VRDP_IO,                 THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_DEBUGGER,                THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_MSG_PUMP,                THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_IO,                      THREAD_PRIORITY_HIGHEST },
            { RTTHREADTYPE_TIMER,                   THREAD_PRIORITY_HIGHEST }
        }
    },
    {
        RTPROCPRIORITY_HIGH, "High", ANY_PROCESS_PRIORITY_CLASS,
        {
            { RTTHREADTYPE_INVALID,                 ~0 },
            { RTTHREADTYPE_INFREQUENT_POLLER,       THREAD_PRIORITY_BELOW_NORMAL },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_EMULATION,               THREAD_PRIORITY_NORMAL },
            { RTTHREADTYPE_DEFAULT,                 THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_GUI,                     THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_MAIN_WORKER,             THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_VRDP_IO,                 THREAD_PRIORITY_ABOVE_NORMAL },
            { RTTHREADTYPE_DEBUGGER,                THREAD_PRIORITY_HIGHEST },
            { RTTHREADTYPE_MSG_PUMP,                THREAD_PRIORITY_HIGHEST },
            { RTTHREADTYPE_IO,                      THREAD_PRIORITY_HIGHEST },
            { RTTHREADTYPE_TIMER,                   THREAD_PRIORITY_HIGHEST }
        }
    }
};

/**
 * The dynamic default priority configuration.
 *
 * This can be recalulated at runtime depending on what the
 * system allow us to do. Presently we don't do this as it's
 * generally not a bit issue on Win32 hosts.
 */
static PROCPRIORITY g_aDefaultPriority =
{
    RTPROCPRIORITY_LOW, "Default", ANY_PROCESS_PRIORITY_CLASS,
    {
        { RTTHREADTYPE_INVALID,                 ~0 },
        { RTTHREADTYPE_INFREQUENT_POLLER,       THREAD_PRIORITY_LOWEST },
        { RTTHREADTYPE_MAIN_HEAVY_WORKER,       THREAD_PRIORITY_BELOW_NORMAL },
        { RTTHREADTYPE_EMULATION,               THREAD_PRIORITY_NORMAL },
        { RTTHREADTYPE_DEFAULT,                 THREAD_PRIORITY_NORMAL },
        { RTTHREADTYPE_GUI,                     THREAD_PRIORITY_NORMAL },
        { RTTHREADTYPE_MAIN_WORKER,             THREAD_PRIORITY_NORMAL },
        { RTTHREADTYPE_VRDP_IO,                 THREAD_PRIORITY_NORMAL },
        { RTTHREADTYPE_DEBUGGER,                THREAD_PRIORITY_ABOVE_NORMAL },
        { RTTHREADTYPE_MSG_PUMP,                THREAD_PRIORITY_ABOVE_NORMAL },
        { RTTHREADTYPE_IO,                      THREAD_PRIORITY_HIGHEST },
        { RTTHREADTYPE_TIMER,                   THREAD_PRIORITY_HIGHEST }
    }
};


/** Pointer to the current priority configuration. */
static const PROCPRIORITY *g_pProcessPriority = &g_aDefaultPriority;


/**
 * Calculate the scheduling properties for all the threads in the default
 * process priority, assuming the current thread have the type enmType.
 *
 * @returns iprt status code.
 * @param   enmType     The thread type to be assumed for the current thread.
 */
DECLHIDDEN(int) rtSchedNativeCalcDefaultPriority(RTTHREADTYPE enmType)
{
    Assert(enmType > RTTHREADTYPE_INVALID && enmType < RTTHREADTYPE_END); RT_NOREF_PV(enmType);
    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtProcNativeSetPriority(RTPROCPRIORITY enmPriority)
{
    Assert(enmPriority > RTPROCPRIORITY_INVALID && enmPriority < RTPROCPRIORITY_LAST); RT_NOREF_PV(enmPriority);

    if (enmPriority == RTPROCPRIORITY_DEFAULT)
    {
        g_pProcessPriority = &g_aDefaultPriority;
        return VINF_SUCCESS;
    }

    for (size_t i = 0; i < RT_ELEMENTS(g_aPriorities); i++)
        if (   g_aPriorities[i].enmPriority == enmPriority
            && g_aPriorities[i].dwProcessPriorityClass == ANY_PROCESS_PRIORITY_CLASS)
        {
            g_pProcessPriority = &g_aPriorities[i];
            return VINF_SUCCESS;
        }

    AssertFailedReturn(VERR_INTERNAL_ERROR);
}


/**
 * Gets the win32 thread handle.
 *
 * @returns Valid win32 handle for the specified thread.
 * @param   pThread     The thread.
 */
DECLINLINE(HANDLE) rtThreadNativeGetHandle(PRTTHREADINT pThread)
{
    if ((uintptr_t)pThread->Core.Key == GetCurrentThreadId())
        return GetCurrentThread();
    return (HANDLE)pThread->hThread;
}


DECLHIDDEN(int) rtThreadNativeSetPriority(PRTTHREADINT pThread, RTTHREADTYPE enmType)
{
    Assert(enmType > RTTHREADTYPE_INVALID && enmType < RTTHREADTYPE_END);
    AssertMsg(g_pProcessPriority && g_pProcessPriority->aTypes[enmType].enmType == enmType,
              ("enmType=%d entry=%d\n", enmType, g_pProcessPriority->aTypes[enmType].enmType));

#ifdef WIN32_SCHED_ENABLED
    HANDLE hThread = rtThreadNativeGetHandle(pThread);
    if (   hThread == NULL /* No handle for alien threads. */
        || SetThreadPriority(hThread, g_pProcessPriority->aTypes[enmType].iThreadPriority))
        return VINF_SUCCESS;

    DWORD dwLastError = GetLastError();
    int rc = RTErrConvertFromWin32(dwLastError);
    AssertMsgFailed(("SetThreadPriority(%p, %d) failed, dwLastError=%d rc=%Rrc\n",
                     rtThreadNativeGetHandle(pThread), g_pProcessPriority->aTypes[enmType].iThreadPriority, dwLastError, rc));
    return rc;
#else
    return VINF_SUCCESS;
#endif
}

