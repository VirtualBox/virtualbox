/* $Id: IEMAll-armv8.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager, ARMv8 specific things.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_EM
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/mm.h>
#include "IEMInternal-armv8.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/vmapi.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/getopt.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

VMMDECL(VBOXSTRICTRC) IEMExecForExits(PVMCPUCC pVCpu, uint32_t fWillExit, uint32_t cMinInstructions, uint32_t cMaxInstructions,
                                      uint32_t cMaxInstructionsWithoutExits, PIEMEXECFOREXITSTATS pStats)
{
    AssertReleaseFailed();
    RT_NOREF(pVCpu, fWillExit, cMinInstructions, cMaxInstructions, cMaxInstructionsWithoutExits);
    RT_ZERO(*pStats);
    return VERR_NOT_IMPLEMENTED;
}


VMMDECL(VBOXSTRICTRC) IEMExecLots(PVMCPUCC pVCpu, uint32_t cMaxInstructions, uint32_t cPollRate, uint32_t *pcInstructions)
{
    AssertReleaseFailed();
    RT_NOREF(pVCpu, cMaxInstructions, cPollRate, pcInstructions);
    return VERR_NOT_IMPLEMENTED;
}


VMMDECL(VBOXSTRICTRC) IEMExecOne(PVMCPUCC pVCpu)
{
    AssertReleaseFailed();
    RT_NOREF(pVCpu);
    return VERR_NOT_IMPLEMENTED;
}


VMMDECL(VBOXSTRICTRC) IEMExecOneWithPrefetchedByPC(PVMCPUCC pVCpu, uint64_t OpcodeBytesPC,
                                                   const void *pvOpcodeBytes, size_t cbOpcodeBytes)
{
    AssertReleaseFailed();
    RT_NOREF(pVCpu, OpcodeBytesPC, pvOpcodeBytes, cbOpcodeBytes);
    return VERR_NOT_IMPLEMENTED;
}


VMMDECL(VBOXSTRICTRC) IEMExecOneBypassEx(PVMCPUCC pVCpu, uint32_t *pcbWritten)
{
    AssertReleaseFailed();
    RT_NOREF(pVCpu, pcbWritten);
    return VERR_NOT_IMPLEMENTED;
}


VMMDECL(VBOXSTRICTRC) IEMExecOneBypassWithPrefetchedByPC(PVMCPUCC pVCpu, uint64_t OpcodeBytesPC,
                                                         const void *pvOpcodeBytes, size_t cbOpcodeBytes)
{
    AssertReleaseFailed();
    RT_NOREF(pVCpu, OpcodeBytesPC, pvOpcodeBytes, cbOpcodeBytes);
    return VERR_NOT_IMPLEMENTED;
}


VMMDECL(VBOXSTRICTRC) IEMInjectTrpmEvent(PVMCPUCC pVCpu)
{
    AssertReleaseFailed();
    RT_NOREF(pVCpu);
    return VERR_NOT_IMPLEMENTED;
}


VMM_INT_DECL(void) IEMTlbInvalidateAll(PVMCPUCC pVCpu)
{
    RT_NOREF(pVCpu);
}


VMM_INT_DECL(void) IEMTlbInvalidateAllGlobal(PVMCPUCC pVCpu)
{
    RT_NOREF(pVCpu);
}


VMM_INT_DECL(void) IEMTlbInvalidateAllPhysicalAllCpus(PVMCC pVM, VMCPUID idCpuCaller, IEMTLBPHYSFLUSHREASON enmReason)
{
    RT_NOREF(pVM, idCpuCaller, enmReason);
}


VMMR3_INT_DECL(VBOXSTRICTRC) IEMR3ProcessForceFlag(PVM pVM, PVMCPUCC pVCpu, VBOXSTRICTRC rcStrict)
{
    AssertReleaseFailed();
    RT_NOREF(pVM, pVCpu, rcStrict);
    return VERR_NOT_IMPLEMENTED;
}
