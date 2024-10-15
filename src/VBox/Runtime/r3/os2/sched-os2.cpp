/* $Id: sched-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Scheduling, OS/2
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

/** @def OS2_SCHED_ENABLED
 * Enables the priority scheme. */
#define OS2_SCHED_ENABLED


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_THREAD
#define INCL_BASE
#define INCL_ERRORS
#include <os2.h>

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
    /** Array scheduler attributes corresponding to each of the thread types. */
    struct
    {
        /** For sanity include the array index. */
        RTTHREADTYPE    enmType;
        /** The OS/2 priority class. */
        ULONG           ulClass;
        /** The OS/2 priority delta. */
        ULONG           ulDelta;
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
        RTPROCPRIORITY_FLAT, "Flat",
        {
            { RTTHREADTYPE_INVALID,                 ~0, ~0 },
            { RTTHREADTYPE_INFREQUENT_POLLER,       PRTYC_REGULAR, 0 },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       PRTYC_REGULAR, 0 },
            { RTTHREADTYPE_EMULATION,               PRTYC_REGULAR, 0 },
            { RTTHREADTYPE_DEFAULT,                 PRTYC_REGULAR, 0 },
            { RTTHREADTYPE_GUI,                     PRTYC_REGULAR, 0 },
            { RTTHREADTYPE_MAIN_WORKER,             PRTYC_REGULAR, 0 },
            { RTTHREADTYPE_VRDP_IO,                 PRTYC_REGULAR, 0 },
            { RTTHREADTYPE_DEBUGGER,                PRTYC_REGULAR, 0 },
            { RTTHREADTYPE_MSG_PUMP,                PRTYC_REGULAR, 0 },
            { RTTHREADTYPE_IO,                      PRTYC_REGULAR, 0 },
            { RTTHREADTYPE_TIMER,                   PRTYC_REGULAR, 0 }
        }
    },
    {
        RTPROCPRIORITY_LOW, "Low",
        {
            { RTTHREADTYPE_INVALID,                 ~0 },
            { RTTHREADTYPE_INFREQUENT_POLLER,       PRTYC_IDLETIME, 0 },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       PRTYC_IDLETIME, 0 },
            { RTTHREADTYPE_EMULATION,               PRTYC_IDLETIME, 0 },
            { RTTHREADTYPE_DEFAULT,                 PRTYC_IDLETIME, 30 },
            { RTTHREADTYPE_GUI,                     PRTYC_IDLETIME, 30 },
            { RTTHREADTYPE_MAIN_WORKER,             PRTYC_IDLETIME, 30 },
            { RTTHREADTYPE_VRDP_IO,                 PRTYC_REGULAR,  0 },
            { RTTHREADTYPE_DEBUGGER,                PRTYC_REGULAR,  0 },
            { RTTHREADTYPE_MSG_PUMP,                PRTYC_REGULAR,  0 },
            { RTTHREADTYPE_IO,                      PRTYC_REGULAR,  0 },
            { RTTHREADTYPE_TIMER,                   PRTYC_REGULAR,  0 }
        }
    },
    {
        RTPROCPRIORITY_NORMAL, "Normal",
        {
            { RTTHREADTYPE_INVALID,                 ~0 },
            { RTTHREADTYPE_INFREQUENT_POLLER,       PRTYC_IDLETIME, 30 },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       PRTYC_IDLETIME, 31 },
            { RTTHREADTYPE_EMULATION,               PRTYC_REGULAR,  0 },
            { RTTHREADTYPE_DEFAULT,                 PRTYC_REGULAR,  5 },
            { RTTHREADTYPE_GUI,                     PRTYC_REGULAR,  10 },
            { RTTHREADTYPE_MAIN_WORKER,             PRTYC_REGULAR,  12 },
            { RTTHREADTYPE_VRDP_IO,                 PRTYC_REGULAR,  15 },
            { RTTHREADTYPE_DEBUGGER,                PRTYC_REGULAR,  20 },
            { RTTHREADTYPE_MSG_PUMP,                PRTYC_REGULAR,  25 },
            { RTTHREADTYPE_IO,                      PRTYC_FOREGROUNDSERVER,  5 },
            { RTTHREADTYPE_TIMER,                   PRTYC_TIMECRITICAL, 0 }
        }
    },
    {
        RTPROCPRIORITY_HIGH, "High",
        {
            { RTTHREADTYPE_INVALID,                 ~0 },
            { RTTHREADTYPE_INFREQUENT_POLLER,       PRTYC_IDLETIME, 30 },
            { RTTHREADTYPE_MAIN_HEAVY_WORKER,       PRTYC_REGULAR,  0 },
            { RTTHREADTYPE_EMULATION,               PRTYC_REGULAR,  0 },
            { RTTHREADTYPE_DEFAULT,                 PRTYC_REGULAR,  15 },
            { RTTHREADTYPE_GUI,                     PRTYC_REGULAR,  20 },
            { RTTHREADTYPE_MAIN_WORKER,             PRTYC_REGULAR,  25 },
            { RTTHREADTYPE_VRDP_IO,                 PRTYC_REGULAR,  30 },
            { RTTHREADTYPE_DEBUGGER,                PRTYC_TIMECRITICAL, 2 },
            { RTTHREADTYPE_MSG_PUMP,                PRTYC_TIMECRITICAL, 3 },
            { RTTHREADTYPE_IO,                      PRTYC_TIMECRITICAL, 4 },
            { RTTHREADTYPE_TIMER,                   PRTYC_TIMECRITICAL, 5 }
        }
    }
};

/**
 * The dynamic default priority configuration.
 *
 * This can be recalulated at runtime depending on what the
 * system allow us to do. Presently we don't do this as it's
 * generally not a bit issue on OS/2 hosts.
 */
static PROCPRIORITY g_aDefaultPriority =
{
    RTPROCPRIORITY_LOW, "Default",
    {
        { RTTHREADTYPE_INVALID,                 ~0 },
        { RTTHREADTYPE_INFREQUENT_POLLER,       PRTYC_IDLETIME, 30 },
        { RTTHREADTYPE_MAIN_HEAVY_WORKER,       PRTYC_IDLETIME, 31 },
        { RTTHREADTYPE_EMULATION,               PRTYC_REGULAR,  0 },
        { RTTHREADTYPE_DEFAULT,                 PRTYC_REGULAR,  5 },
        { RTTHREADTYPE_GUI,                     PRTYC_REGULAR,  10 },
        { RTTHREADTYPE_MAIN_WORKER,             PRTYC_REGULAR,  12 },
        { RTTHREADTYPE_VRDP_IO,                 PRTYC_REGULAR,  15 },
        { RTTHREADTYPE_DEBUGGER,                PRTYC_REGULAR,  20 },
        { RTTHREADTYPE_MSG_PUMP,                PRTYC_REGULAR,  25 },
        { RTTHREADTYPE_IO,                      PRTYC_FOREGROUNDSERVER, 5 },
        { RTTHREADTYPE_TIMER,                   PRTYC_TIMECRITICAL, 0 }
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
    Assert(enmType > RTTHREADTYPE_INVALID && enmType < RTTHREADTYPE_END);
    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtProcNativeSetPriority(RTPROCPRIORITY enmPriority)
{
    Assert(enmPriority > RTPROCPRIORITY_INVALID && enmPriority < RTPROCPRIORITY_LAST);

    if (enmPriority == RTPROCPRIORITY_DEFAULT)
    {
        g_pProcessPriority = &g_aDefaultPriority;
        return VINF_SUCCESS;
    }

    for (size_t i = 0; i < RT_ELEMENTS(g_aPriorities); i++)
        if (g_aPriorities[i].enmPriority == enmPriority)
        {
            g_pProcessPriority = &g_aPriorities[i];
            return VINF_SUCCESS;
        }

    AssertFailedReturn(VERR_INTERNAL_ERROR);
}


DECLHIDDEN(int) rtThreadNativeSetPriority(PRTTHREADINT pThread, RTTHREADTYPE enmType)
{
    Assert(enmType > RTTHREADTYPE_INVALID && enmType < RTTHREADTYPE_END);
    AssertMsg(g_pProcessPriority && g_pProcessPriority->aTypes[enmType].enmType == enmType,
              ("enmType=%d entry=%d\n", enmType, g_pProcessPriority->aTypes[enmType].enmType));

#ifdef OS2_SCHED_ENABLED
    APIRET rc = DosSetPriority(PRTYS_THREAD, g_pProcessPriority->aTypes[enmType].ulClass, g_pProcessPriority->aTypes[enmType].ulDelta, (ULONG)pThread->Core.Key & 0xffff /*tid*/);
    AssertMsg(rc == NO_ERROR, ("%d\n", rc));
    return RTErrConvertFromOS2(rc);
#else
    return VINF_SUCCESS;
#endif
}

