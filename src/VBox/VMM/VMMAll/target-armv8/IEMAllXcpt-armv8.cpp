/* $Id: IEMAllXcpt-armv8.cpp 110467 2025-07-30 08:31:12Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - ARM target, exceptions & interrupts.
 */

/*
 * Copyright (C) 2011-2025 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_IEM
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/iem.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/err.h>


VBOXSTRICTRC
iemRaiseUndefined(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    RT_NOREF(pVCpu);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


DECL_NO_RETURN(void)
iemRaiseUndefinedJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
    VBOXSTRICTRC const rcStrict = iemRaiseUndefined(pVCpu);
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
}


VBOXSTRICTRC
iemRaiseDataAbortFromWalk(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t cbMem, uint32_t fAccess, int rc,
                          PCPGMPTWALKFAST pWalkFast) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, GCPtrMem, cbMem, fAccess, rc, pWalkFast);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


DECL_NO_RETURN(void)
iemRaiseDataAbortFromWalkJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t cbMem, uint32_t fAccess, int rc,
                             PCPGMPTWALKFAST pWalkFast) IEM_NOEXCEPT_MAY_LONGJMP
{
    VBOXSTRICTRC const rcStrict = iemRaiseDataAbortFromWalk(pVCpu, GCPtrMem, cbMem, fAccess, rc, pWalkFast);
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
}


VBOXSTRICTRC
iemRaiseDataAbortFromAlignmentCheck(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t cbMem, uint32_t fAccess) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, GCPtrMem, cbMem, fAccess);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


DECL_NO_RETURN(void)
iemRaiseDataAbortFromAlignmentCheckJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint32_t cbMem,
                                       uint32_t fAccess) IEM_NOEXCEPT_MAY_LONGJMP
{
    VBOXSTRICTRC const rcStrict = iemRaiseDataAbortFromAlignmentCheck(pVCpu, GCPtrMem, cbMem, fAccess);
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
}


VBOXSTRICTRC
iemRaiseDebugDataAccessOrInvokeDbgf(PVMCPUCC pVCpu, uint32_t fDataBps, RTGCPTR GCPtrMem, size_t cbMem,
                                    uint32_t fAccess) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, fDataBps, GCPtrMem, cbMem, fAccess);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


VBOXSTRICTRC
iemRaiseInstructionAbortTlbPermision(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t cbMem, PCIEMTLBENTRY pTlbe) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, GCPtrMem, cbMem, pTlbe);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


DECL_NO_RETURN(void)
iemRaiseInstructionAbortTlbPermisionJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t cbMem, PCIEMTLBENTRY pTlbe)
{
    VBOXSTRICTRC const rcStrict = iemRaiseInstructionAbortTlbPermision(pVCpu, GCPtrMem, cbMem, pTlbe);
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
}


VBOXSTRICTRC
iemRaiseInstructionAbortFromWalk(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t cbMem, uint32_t fAccess, int rc,
                                 PCPGMPTWALKFAST pWalkFast) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, GCPtrMem, cbMem, fAccess, rc, pWalkFast);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


DECL_NO_RETURN(void)
iemRaiseInstructionAbortFromWalkJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrMem, uint8_t cbMem, uint32_t fAccess, int rc,
                                    PCPGMPTWALKFAST pWalkFast) IEM_NOEXCEPT_MAY_LONGJMP
{
    VBOXSTRICTRC const rcStrict = iemRaiseInstructionAbortFromWalk(pVCpu, GCPtrMem, cbMem, fAccess, rc, pWalkFast);
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
}


DECL_NO_RETURN(void)
iemRaiseDebugDataAccessOrInvokeDbgfJmp(PVMCPUCC pVCpu, uint32_t fDataBps, RTGCPTR GCPtrMem, size_t cbMem,
                                       uint32_t fAccess) IEM_NOEXCEPT_MAY_LONGJMP
{
    VBOXSTRICTRC const rcStrict = iemRaiseDebugDataAccessOrInvokeDbgf(pVCpu, fDataBps, GCPtrMem, cbMem, fAccess);
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
}


VBOXSTRICTRC
iemRaisePcAlignmentCheck(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    RT_NOREF(pVCpu);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


DECL_NO_RETURN(void)
iemRaisePcAlignmentCheckJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
    VBOXSTRICTRC const rcStrict = iemRaisePcAlignmentCheck(pVCpu);
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
}


VBOXSTRICTRC
iemRaiseSpAlignmentCheck(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    RT_NOREF(pVCpu);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


DECL_NO_RETURN(void)
iemRaiseSpAlignmentCheckJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
    VBOXSTRICTRC const rcStrict = iemRaiseSpAlignmentCheck(pVCpu);
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
}


VBOXSTRICTRC iemRaiseSystemAccessTrap(PVMCPU pVCpu, uint32_t uEl, uint32_t uInstrEssence) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, uEl, uInstrEssence);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


VBOXSTRICTRC iemRaiseSystemAccessTrapSve(PVMCPU pVCpu, uint32_t uEl) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, uEl);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


VBOXSTRICTRC iemRaiseSystemAccessTrapSme(PVMCPU pVCpu, uint32_t uEl) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, uEl);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


VBOXSTRICTRC iemRaiseSystemAccessTrapAdvSimdFpAccessA64(PVMCPU pVCpu, uint32_t uEl) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, uEl);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


VBOXSTRICTRC iemRaiseSystemAccessTrapUnknown(PVMCPU pVCpu, uint32_t uEl) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, uEl);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


VBOXSTRICTRC iemRaiseExlockException(PVMCPU pVCpu) RT_NOEXCEPT
{
    RT_NOREF(pVCpu);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


/** Accessed via IEMOP_RAISE_INVALID_OPCODE. */
IEM_CIMPL_DEF_0(iemCImplRaiseInvalidOpcode)
{
    RT_NOREF(pVCpu);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}

