/* $Id: initterm-r0drv-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Initialization & Termination, R0 Driver, Darwin.
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
#include "the-darwin-kernel.h"
#include "internal/iprt.h"

#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/dbg.h>
#include "internal/initterm.h"



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Pointer to the lock group used by IPRT. */
DECL_HIDDEN_DATA(lck_grp_t *)                   g_pDarwinLockGroup = NULL;
/** Pointer to the ast_pending function, if found. */
DECL_HIDDEN_DATA(PFNR0DARWINASTPENDING)         g_pfnR0DarwinAstPending = NULL;
/** Pointer to the cpu_interrupt function, if found. */
DECL_HIDDEN_DATA(PFNR0DARWINCPUINTERRUPT)       g_pfnR0DarwinCpuInterrupt = NULL;
#ifdef DEBUG
/** Pointer to the vm_fault_external function - used once for debugging @bugref{9466}. */
DECL_HIDDEN_DATA(PFNR0DARWINVMFAULTEXTERNAL)    g_pfnR0DarwinVmFaultExternal = NULL;
#endif


DECLHIDDEN(int) rtR0InitNative(void)
{
    IPRT_DARWIN_SAVE_EFL_AC();

    /*
     * Create the lock group.
     */
    g_pDarwinLockGroup = lck_grp_alloc_init("IPRT", LCK_GRP_ATTR_NULL);
    AssertReturn(g_pDarwinLockGroup, VERR_NO_MEMORY);

    /*
     * Initialize the preemption hacks.
     */
    int rc = rtThreadPreemptDarwinInit();
    if (RT_SUCCESS(rc))
    {
        /*
         * Try resolve kernel symbols we need but apple don't wish to give us.
         */
        RTDBGKRNLINFO hKrnlInfo;
        rc = RTR0DbgKrnlInfoOpen(&hKrnlInfo, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, NULL, "ast_pending",   (void **)&g_pfnR0DarwinAstPending);
            printf("ast_pending=%p\n", (void *)(uintptr_t)g_pfnR0DarwinAstPending);
            RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, NULL, "cpu_interrupt", (void **)&g_pfnR0DarwinCpuInterrupt);
            printf("cpu_interrupt=%p\n", (void *)(uintptr_t)g_pfnR0DarwinCpuInterrupt);
#ifdef DEBUG
            RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, NULL, "vm_fault_external", (void **)&g_pfnR0DarwinVmFaultExternal);
            printf("vm_fault_external=%p\n", (void *)(uintptr_t)g_pfnR0DarwinVmFaultExternal);
#endif
            RTR0DbgKrnlInfoRelease(hKrnlInfo);
        }
        if (RT_FAILURE(rc))
        {
            printf("rtR0InitNative: warning! failed to resolve special kernel symbols\n");
            rc = VINF_SUCCESS;
        }
    }
    if (RT_FAILURE(rc))
        rtR0TermNative();

    IPRT_DARWIN_RESTORE_EFL_AC();
    return rc;
}


DECLHIDDEN(void) rtR0TermNative(void)
{
    IPRT_DARWIN_SAVE_EFL_AC();

    /*
     * Preemption hacks before the lock group.
     */
    rtThreadPreemptDarwinTerm();

    /*
     * Free the lock group.
     */
    if (g_pDarwinLockGroup)
    {
        lck_grp_free(g_pDarwinLockGroup);
        g_pDarwinLockGroup = NULL;
    }

    IPRT_DARWIN_RESTORE_EFL_AC();
}

