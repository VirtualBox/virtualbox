/* $Id: initterm-r0drv-linux.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Initialization & Termination, R0 Driver, Linux.
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
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include "internal/initterm.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The IPRT work queue. */
#if RTLNX_VER_MIN(2,5,41)
static struct workqueue_struct *g_prtR0LnxWorkQueue;
#else
static DECLARE_TASK_QUEUE(g_rtR0LnxWorkQueue);
#endif


/**
 * Pushes an item onto the IPRT work queue.
 *
 * @param   pWork               The work item.
 * @param   pfnWorker           The callback function.  It will be called back
 *                              with @a pWork as argument.
 */
DECLHIDDEN(void) rtR0LnxWorkqueuePush(RTR0LNXWORKQUEUEITEM *pWork, void (*pfnWorker)(RTR0LNXWORKQUEUEITEM *))
{
    IPRT_LINUX_SAVE_EFL_AC();

#if RTLNX_VER_MIN(2,5,41)
# if RTLNX_VER_MIN(2,6,20)
    INIT_WORK(pWork, pfnWorker);
# else
    INIT_WORK(pWork, (void (*)(void *))pfnWorker, pWork);
# endif
    queue_work(g_prtR0LnxWorkQueue, pWork);
#else
    INIT_TQUEUE(pWork, (void (*)(void *))pfnWorker, pWork);
    queue_task(pWork, &g_rtR0LnxWorkQueue);
#endif

    IPRT_LINUX_RESTORE_EFL_AC();
}


/**
 * Flushes all items in the IPRT work queue.
 *
 * @remarks This is mostly for 2.4.x compatability.  Must not be called from
 *          atomic contexts or with unncessary locks held.
 */
DECLHIDDEN(void) rtR0LnxWorkqueueFlush(void)
{
    IPRT_LINUX_SAVE_EFL_AC();

#if RTLNX_VER_MIN(2,5,41)
    flush_workqueue(g_prtR0LnxWorkQueue);
#else
    run_task_queue(&g_rtR0LnxWorkQueue);
#endif

    IPRT_LINUX_RESTORE_EFL_AC();
}


DECLHIDDEN(int) rtR0InitNative(void)
{
    int rc = VINF_SUCCESS;
    IPRT_LINUX_SAVE_EFL_AC();

#if RTLNX_VER_MIN(2,5,41)
 #if RTLNX_VER_MIN(2,6,13)
    g_prtR0LnxWorkQueue = create_workqueue("iprt-VBoxWQueue");
 #else
    g_prtR0LnxWorkQueue = create_workqueue("iprt-VBoxQ");
 #endif
    if (!g_prtR0LnxWorkQueue)
        rc = VERR_NO_MEMORY;
#endif

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}


DECLHIDDEN(void) rtR0TermNative(void)
{
    IPRT_LINUX_SAVE_EFL_AC();

    rtR0LnxWorkqueueFlush();
#if RTLNX_VER_MIN(2,5,41)
    destroy_workqueue(g_prtR0LnxWorkQueue);
    g_prtR0LnxWorkQueue = NULL;
#endif

    IPRT_LINUX_RESTORE_EFL_AC();
}

