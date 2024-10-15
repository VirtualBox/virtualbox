/* $Id: CPUMRZ.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPUM - Raw-mode and ring-0 context.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_CPUM
#include <VBox/vmm/cpum.h>
#include "CPUMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/vmm/hm.h>
#include <iprt/assert.h>
#include <iprt/x86.h>




/**
 * Prepares the host FPU/SSE/AVX stuff for IEM action.
 *
 * This will make sure the FPU/SSE/AVX guest state is _not_ loaded in the CPU.
 * This will make sure the FPU/SSE/AVX host state is saved.
 * Finally, it will make sure the FPU/SSE/AVX host features can be safely
 * accessed.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMRZ_INT_DECL(void)    CPUMRZFpuStatePrepareHostCpuForUse(PVMCPUCC pVCpu)
{
    pVCpu->cpum.s.fChanged |= CPUM_CHANGED_FPU_REM;
    switch (pVCpu->cpum.s.fUseFlags & (CPUM_USED_FPU_GUEST | CPUM_USED_FPU_HOST))
    {
        case 0:
            if (cpumRZSaveHostFPUState(&pVCpu->cpum.s) == VINF_CPUM_HOST_CR0_MODIFIED)
                HMR0NotifyCpumModifiedHostCr0(pVCpu);
            Log6(("CPUMRZFpuStatePrepareHostCpuForUse: #0 - %#x\n", ASMGetCR0()));
            break;

        case CPUM_USED_FPU_HOST:
            Log6(("CPUMRZFpuStatePrepareHostCpuForUse: #1 - %#x\n", ASMGetCR0()));
            break;

        case CPUM_USED_FPU_GUEST | CPUM_USED_FPU_HOST:
            cpumRZSaveGuestFpuState(&pVCpu->cpum.s, true /*fLeaveFpuAccessible*/);
#ifdef IN_RING0
            HMR0NotifyCpumUnloadedGuestFpuState(pVCpu);
#endif
            Log6(("CPUMRZFpuStatePrepareHostCpuForUse: #2 - %#x\n", ASMGetCR0()));
            break;

        default:
            AssertFailed();
    }

}


/**
 * Makes sure the FPU/SSE/AVX guest state is saved in CPUMCPU::Guest and will be
 * reloaded before direct use.
 *
 * No promisses about the FPU/SSE/AVX host features are made.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMRZ_INT_DECL(void)    CPUMRZFpuStateActualizeForChange(PVMCPUCC pVCpu)
{
    CPUMRZFpuStatePrepareHostCpuForUse(pVCpu);
}


/**
 * Makes sure the FPU/SSE/AVX state in CPUMCPU::Guest is up to date.
 *
 * This will not cause CPUM_USED_FPU_GUEST to change.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMRZ_INT_DECL(void)    CPUMRZFpuStateActualizeForRead(PVMCPUCC pVCpu)
{
    if (pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST)
    {
        Assert(pVCpu->cpum.s.Guest.fUsedFpuGuest);
        cpumRZSaveGuestFpuState(&pVCpu->cpum.s, false /*fLeaveFpuAccessible*/);
        pVCpu->cpum.s.fUseFlags |= CPUM_USED_FPU_GUEST;
        pVCpu->cpum.s.Guest.fUsedFpuGuest = true;
        Log7(("CPUMRZFpuStateActualizeForRead\n"));
    }
}


/**
 * Makes sure the XMM0..XMM15 and MXCSR state in CPUMCPU::Guest is up to date.
 *
 * This will not cause CPUM_USED_FPU_GUEST to change.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMRZ_INT_DECL(void)    CPUMRZFpuStateActualizeSseForRead(PVMCPUCC pVCpu)
{
#if defined(VBOX_WITH_KERNEL_USING_XMM) && HC_ARCH_BITS == 64
    NOREF(pVCpu);
#else
    if (pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST)
    {
        Assert(pVCpu->cpum.s.Guest.fUsedFpuGuest);
        cpumRZSaveGuestSseRegisters(&pVCpu->cpum.s);
        Log7(("CPUMRZFpuStateActualizeSseForRead\n"));
    }
#endif
}


/**
 * Makes sure the YMM0..YMM15 and MXCSR state in CPUMCPU::Guest is up to date.
 *
 * This will not cause CPUM_USED_FPU_GUEST to change.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMRZ_INT_DECL(void)    CPUMRZFpuStateActualizeAvxForRead(PVMCPUCC pVCpu)
{
    if (pVCpu->cpum.s.fUseFlags & CPUM_USED_FPU_GUEST)
    {
        Assert(pVCpu->cpum.s.Guest.fUsedFpuGuest);
        cpumRZSaveGuestAvxRegisters(&pVCpu->cpum.s);
        Log7(("CPUMRZFpuStateActualizeAvxForRead\n"));
    }
}

