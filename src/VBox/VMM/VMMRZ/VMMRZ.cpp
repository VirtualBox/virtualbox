/* $Id: VMMRZ.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMM - Virtual Machine Monitor, Raw-mode and ring-0 context code.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_VMM
#include <VBox/vmm/vmm.h>
#include "VMMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <iprt/assert.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/errcore.h>
#include <iprt/string.h>


/**
 * Disables all host calls, except certain fatal ones.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @thread  EMT.
 */
VMMRZDECL(void) VMMRZCallRing3Disable(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);
#if defined(LOG_ENABLED) && defined(IN_RING0)
    RTCCUINTREG fFlags = ASMIntDisableFlags(); /* preemption consistency. */
#endif

    Assert(pVCpu->vmmr0.s.cCallRing3Disabled < 16);
    if (ASMAtomicUoIncU32(&pVCpu->vmmr0.s.cCallRing3Disabled) == 1)
    {
#ifdef IN_RC
        pVCpu->pVMRC->vmm.s.fRCLoggerFlushingDisabled = true;
#else
        pVCpu->vmmr0.s.fLogFlushingDisabled = true;
#endif
    }

#if defined(LOG_ENABLED) && defined(IN_RING0)
    ASMSetFlags(fFlags);
#endif
}


/**
 * Counters VMMRZCallRing3Disable() and re-enables host calls.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @thread  EMT.
 */
VMMRZDECL(void) VMMRZCallRing3Enable(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);
#if defined(LOG_ENABLED) && defined(IN_RING0)
    RTCCUINTREG fFlags = ASMIntDisableFlags(); /* preemption consistency. */
#endif

    Assert(pVCpu->vmmr0.s.cCallRing3Disabled > 0);
    if (ASMAtomicUoDecU32(&pVCpu->vmmr0.s.cCallRing3Disabled) == 0)
    {
#ifdef IN_RC
        pVCpu->pVMRC->vmm.s.fRCLoggerFlushingDisabled = false;
#else
        pVCpu->vmmr0.s.fLogFlushingDisabled = false;
#endif
    }

#if defined(LOG_ENABLED) && defined(IN_RING0)
    ASMSetFlags(fFlags);
#endif
}


/**
 * Checks whether its possible to call host context or not.
 *
 * @returns true if it's safe, false if it isn't.
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 */
VMMRZDECL(bool) VMMRZCallRing3IsEnabled(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pVCpu->vmmr0.s.cCallRing3Disabled <= 16);
    return pVCpu->vmmr0.s.cCallRing3Disabled == 0;
}

