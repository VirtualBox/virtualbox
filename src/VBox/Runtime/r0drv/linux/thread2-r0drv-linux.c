/* $Id: thread2-r0drv-linux.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Threads (Part 2), Ring-0 Driver, Linux.
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
#include "the-linux-kernel.h"
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/thread.h>
#include <iprt/errcore.h>
#include "internal/thread.h"

#if RTLNX_VER_MIN(4,11,0)
    #include <uapi/linux/sched/types.h>
#endif /* >= KERNEL_VERSION(4, 11, 0) */

RTDECL(RTTHREAD) RTThreadSelf(void)
{
    return rtThreadGetByNative((RTNATIVETHREAD)current);
}


DECLHIDDEN(int) rtThreadNativeInit(void)
{
    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtThreadNativeSetPriority(PRTTHREADINT pThread, RTTHREADTYPE enmType)
{
#if RTLNX_VER_MIN(2,5,2)
    /*
     * Assignments are partially based on g_aTypesLinuxFree but
     * scaled up in the high priority end.
     *
     * 5.9.0 - :
     *      The sched_set_normal interfaces does not really check the input,
     *      whereas sched_set_fifo & sched_set_fifo_low have fixed assignments.
     * 2.6.11 - 5.9.0:
     *      Use sched_setscheduler to try effect FIFO scheduling
     *      for IO and TIMER threads, otherwise use set_user_nice.
     * 2.5.2 - 5.9.0:
     *      Use set_user_nice to renice the thread.
     */
    int                 iNice       = 0;
# if RTLNX_VER_MAX(5,9,0)
    int                 rc;
#  if RTLNX_VER_MIN(2,6,11)
    int                 iSchedClass = SCHED_NORMAL;
    struct sched_param  Param       = { .sched_priority = 0 };
#  endif
# endif
    switch (enmType)
    {
        case RTTHREADTYPE_INFREQUENT_POLLER:
            iNice = +3;
            break;

        case RTTHREADTYPE_MAIN_HEAVY_WORKER:
            iNice = +2;
            break;

        case RTTHREADTYPE_EMULATION:
            iNice = +1;
            break;

        case RTTHREADTYPE_DEFAULT:
        case RTTHREADTYPE_GUI:
        case RTTHREADTYPE_MAIN_WORKER:
            iNice =  0;
            break;

        case RTTHREADTYPE_VRDP_IO:
        case RTTHREADTYPE_DEBUGGER:
            iNice = -1;
            break;

        case RTTHREADTYPE_MSG_PUMP:
            iNice = -2;
            break;

        case RTTHREADTYPE_IO:
# if RTLNX_VER_MIN(5,9,0)
            sched_set_fifo_low(current);
            return VINF_SUCCESS;
# else
            iNice = -12;
#  if RTLNX_VER_MIN(2,6,11)
            iSchedClass = SCHED_FIFO;
            Param.sched_priority = 1; /* => prio=98; */
#  endif
            break;
# endif

        case RTTHREADTYPE_TIMER:
# if RTLNX_VER_MIN(5,9,0)
            sched_set_fifo(current);
            return VINF_SUCCESS;
# else
            iNice = -19;
#  if RTLNX_VER_MIN(2,6,11)
            iSchedClass = SCHED_FIFO;
            Param.sched_priority = MAX_RT_PRIO / 2; /* => prio=49 */
#  endif
            break;
# endif
        default:
            AssertMsgFailedReturn(("enmType=%d\n", enmType), VERR_INVALID_PARAMETER);
    }

# if RTLNX_VER_MIN(5,9,0)
    /*
     * We only get here for renice work.
     */
    sched_set_normal(current, iNice);

# else  /* < 5.9.0 */
#  if RTLNX_VER_MIN(2,6,11)
    /*
     * Try set scheduler parameters.
     * Fall back on normal + nice if this fails for FIFO policy.*
     */
    rc = sched_setscheduler(current, iSchedClass, &Param);
    if (rc)
    {
        Param.sched_priority = 0;
        iSchedClass = SCHED_NORMAL;
        rc = sched_setscheduler(current, iSchedClass, &Param);
    }

    /*
     * Renice if using normal scheduling class.
     */
    if (iSchedClass == SCHED_NORMAL)
#  endif /* >= 2.6.11 */
        set_user_nice(current, iNice);

# endif /* < 5.9.0 */
#else  /* < 2.5.2 */
    RT_NOREF_PV(enmType);
#endif /* < 2.5.2 */
    RT_NOREF_PV(pThread);
    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtThreadNativeAdopt(PRTTHREADINT pThread)
{
    RT_NOREF_PV(pThread);
    return VERR_NOT_IMPLEMENTED;
}


DECLHIDDEN(void) rtThreadNativeWaitKludge(PRTTHREADINT pThread)
{
    /** @todo fix RTThreadWait/RTR0Term race on linux. */
    RTThreadSleep(1); NOREF(pThread);
}


DECLHIDDEN(void) rtThreadNativeDestroy(PRTTHREADINT pThread)
{
    NOREF(pThread);
}


#if RTLNX_VER_MIN(2,6,4)
/**
 * Native kernel thread wrapper function.
 *
 * This will forward to rtThreadMain and do termination upon return.
 *
 * @param pvArg         Pointer to the argument package.
 */
static int rtThreadNativeMain(void *pvArg)
{
    PRTTHREADINT pThread = (PRTTHREADINT)pvArg;

    rtThreadMain(pThread, (RTNATIVETHREAD)current, &pThread->szName[0]);
    return 0;
}
#endif


DECLHIDDEN(int) rtThreadNativeCreate(PRTTHREADINT pThreadInt, PRTNATIVETHREAD pNativeThread)
{
#if RTLNX_VER_MIN(2,6,4)
    struct task_struct *NativeThread;
    IPRT_LINUX_SAVE_EFL_AC();

    RT_ASSERT_PREEMPTIBLE();

    NativeThread = kthread_run(rtThreadNativeMain, pThreadInt, "iprt-%s", pThreadInt->szName);

    if (!IS_ERR(NativeThread))
    {
        *pNativeThread = (RTNATIVETHREAD)NativeThread;
        IPRT_LINUX_RESTORE_EFL_AC();
        return VINF_SUCCESS;
    }
    IPRT_LINUX_RESTORE_EFL_AC();
    return VERR_GENERAL_FAILURE;
#else
    return VERR_NOT_IMPLEMENTED;
#endif
}

