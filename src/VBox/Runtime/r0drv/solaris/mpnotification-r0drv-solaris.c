/* $Id: mpnotification-r0drv-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiprocessor Event Notifications, Ring-0 Driver, Solaris.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include "the-solaris-kernel.h"
#include "internal/iprt.h"

#include <iprt/errcore.h>
#include <iprt/mp.h>
#include <iprt/cpuset.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include "r0drv/mp-r0drv.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Whether CPUs are being watched or not. */
static volatile bool g_fSolCpuWatch = false;
/** Set of online cpus that is maintained by the MP callback.
 * This avoids locking issues querying the set from the kernel as well as
 * eliminating any uncertainty regarding the online status during the
 * callback. */
RTCPUSET g_rtMpSolCpuSet;

/**
 * Internal solaris representation for watching CPUs.
 */
typedef struct RTMPSOLWATCHCPUS
{
    /** Function pointer to Mp worker. */
    PFNRTMPWORKER   pfnWorker;
    /** Argument to pass to the Mp worker. */
    void           *pvArg;
} RTMPSOLWATCHCPUS;
typedef RTMPSOLWATCHCPUS *PRTMPSOLWATCHCPUS;


/**
 * Solaris callback function for Mp event notification.
 *
 * @returns Solaris error code.
 * @param    CpuState   The current event/state of the CPU.
 * @param    iCpu       Which CPU is this event for.
 * @param    pvArg      Ignored.
 *
 * @remarks This function assumes index == RTCPUID.
 *          We may -not- be firing on the CPU going online/offline and called
 *          with preemption enabled.
 */
static int rtMpNotificationCpuEvent(cpu_setup_t CpuState, int iCpu, void *pvArg)
{
    RTMPEVENT enmMpEvent;

    /*
     * Update our CPU set structures first regardless of whether we've been
     * scheduled on the right CPU or not, this is just atomic accounting.
     */
    if (CpuState == CPU_ON)
    {
        enmMpEvent = RTMPEVENT_ONLINE;
        RTCpuSetAdd(&g_rtMpSolCpuSet, iCpu);
    }
    else if (CpuState == CPU_OFF)
    {
        enmMpEvent = RTMPEVENT_OFFLINE;
        RTCpuSetDel(&g_rtMpSolCpuSet, iCpu);
    }
    else
        return 0;

    rtMpNotificationDoCallbacks(enmMpEvent, iCpu);
    NOREF(pvArg);
    return 0;
}


DECLHIDDEN(int) rtR0MpNotificationNativeInit(void)
{
    if (ASMAtomicReadBool(&g_fSolCpuWatch) == true)
        return VERR_WRONG_ORDER;

    /*
     * Register the callback building the online cpu set as we do so.
     */
    RTCpuSetEmpty(&g_rtMpSolCpuSet);

    mutex_enter(&cpu_lock);
    register_cpu_setup_func(rtMpNotificationCpuEvent, NULL /* pvArg */);

    for (int i = 0; i < (int)RTMpGetCount(); ++i)
        if (cpu_is_online(cpu[i]))
            rtMpNotificationCpuEvent(CPU_ON, i, NULL /* pvArg */);

    ASMAtomicWriteBool(&g_fSolCpuWatch, true);
    mutex_exit(&cpu_lock);

    return VINF_SUCCESS;
}


DECLHIDDEN(void) rtR0MpNotificationNativeTerm(void)
{
    if (ASMAtomicReadBool(&g_fSolCpuWatch) == true)
    {
        mutex_enter(&cpu_lock);
        unregister_cpu_setup_func(rtMpNotificationCpuEvent, NULL /* pvArg */);
        ASMAtomicWriteBool(&g_fSolCpuWatch, false);
        mutex_exit(&cpu_lock);
    }
}

