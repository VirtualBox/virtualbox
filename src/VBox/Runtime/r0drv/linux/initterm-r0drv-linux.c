/* $Id: initterm-r0drv-linux.c 114441 2026-06-18 16:45:13Z alexander.eichner@oracle.com $ */
/** @file
 * IPRT - Initialization & Termination, R0 Driver, Linux.
 */

/*
 * Copyright (C) 2006-2025 Oracle and/or its affiliates.
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
#include <iprt/dbg.h>
#include "internal/initterm.h"


#define X86_CPUID_STEXT_FEATURE_EDX_CET_IBT RT_BIT_32(20)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The IPRT work queue. */
#if RTLNX_VER_MIN(2,5,41)
static struct workqueue_struct *g_prtR0LnxWorkQueue;
#else
static DECLARE_TASK_QUEUE(g_rtR0LnxWorkQueue);
#endif

/** Pointer to init_mm, if we have it.
 * This is a special mm structure used to manage the kernel address space. */
struct mm_struct *g_pLnxInitMm = NULL;


/** Whether CET is supported on this CPU. */
bool g_fLnxIsCetSupported = false;
/** Whether CET is enabled. */
bool g_fLnxIsCetEnabled  = false;

#if RTLNX_VER_MIN(6,19,0) && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86))
/** Pointer to __flush_tlb_all kernel symbol. */
__typeof__(__flush_tlb_all) *g_pfnLinuxFlushTlbAll;
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
# if RTLNX_VER_MIN(2,6,13)
    g_prtR0LnxWorkQueue = create_workqueue("iprt-VBoxWQueue");
# else
    g_prtR0LnxWorkQueue = create_workqueue("iprt-VBoxQ");
# endif
    if (!g_prtR0LnxWorkQueue)
        rc = VERR_NO_MEMORY;
#endif

    /*
     * Support for calling dynamic functions on CET-enabled (control flow
     * enforcement technology) kernels.
     */
    g_fLnxIsCetSupported = false;
    g_fLnxIsCetEnabled   = false;
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    {
        uint32_t uLeaves = ASMCpuId_EAX(0);
        if (   uLeaves >= 7
            && RTX86IsValidStdRange(uLeaves)
            && (ASMCpuIdEx_EDX(7, 0) & X86_CPUID_STEXT_FEATURE_EDX_CET_IBT))
        {
            uint64_t const fSupCet = ASMRdMsr(MSR_IA32_S_CET);
            g_fLnxIsCetSupported = true;
            g_fLnxIsCetEnabled   = RT_BOOL(fSupCet & MSR_IA32_CET_ENDBR_EN);
        }
    }
#endif

    /*
     * There are some unexported symbols we want, try get them:
     *
     *      - 'init_mm' so we can protect kernel memory (esp on arm64) in
     *         memobj-r0drv-linux.c (IPRT_USE_APPLY_TO_PAGE_RANGE_FOR_EXEC).
     *         Which means it's needed for 5.10.  The symbol was exported
     *         before 2.6.25.
     *
     *      - __flush_tlb_all is restricted since 6.19 (x86 & amd64).
     */
#if RTLNX_VER_MIN(5,10,0) /* (this works back to 2.30 in theory) */
    {
        RTDBGKRNLINFO hKrnlInfo;
        int rc2 = RTR0DbgKrnlInfoOpen(&hKrnlInfo, 0);
        if (RT_SUCCESS(rc2))
        {
            g_pLnxInitMm = (struct mm_struct *)RTR0DbgKrnlInfoGetSymbol(hKrnlInfo, NULL,  "init_mm");
            //printk("rtR0InitNative: g_pLnxInitMm=%#lx\n", (unsigned long)g_pLnxInitMm);
            printk(KERN_INFO "rtR0InitNative: g_pLnxInitMm=%p\n", g_pLnxInitMm);
# if RTLNX_VER_MIN(6,19,0) && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86))
            g_pfnLinuxFlushTlbAll = (__typeof__(g_pfnLinuxFlushTlbAll))RTR0DbgKrnlInfoGetFunction(hKrnlInfo, NULL,
                                                                                                   "__flush_tlb_all");
# endif
            RTR0DbgKrnlInfoRelease(hKrnlInfo);
        }
        else
            printk(KERN_WARNING "rtR0InitNative: RTR0DbgKrnlInfoOpen failed: %d\n", rc2);

# if RTLNX_VER_MIN(6,19,0) && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86))
        if (!RT_VALID_PTR(g_pfnLinuxFlushTlbAll))
            g_pfnLinuxFlushTlbAll = (__typeof__(g_pfnLinuxFlushTlbAll))__symbol_get("__flush_tlb_all");
        if (!RT_VALID_PTR(g_pfnLinuxFlushTlbAll))
        {
            printk(KERN_ERR "rtR0InitNative: Unable to resolve '__flush_tlb_all'!\n");
            g_pfnLinuxFlushTlbAll = NULL;
        }
# endif
    }
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

#if RTLNX_VER_MIN(6,19,0) && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86))
    /* Don't bother putting __flush_tlb_all, it's in kernel text and we
       probably got it via kallsyms anyway. */
    g_pfnLinuxFlushTlbAll = NULL;
#endif

    IPRT_LINUX_RESTORE_EFL_AC();
}

