/* $Id: IEMAllCImpl-armv8.cpp 110476 2025-07-30 11:37:14Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - ARMv8 target, miscellaneous.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP   LOG_GROUP_IEM
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/dbgf.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/armv8.h>

#include "IEMInline-armv8.h"


/*********************************************************************************************************************************
*   System Register Access Fallbacks and Helpers                                                                                 *
*********************************************************************************************************************************/

DECLHIDDEN(VBOXSTRICTRC) iemCImplA64_mrs_novar(PVMCPU pVCpu, uint32_t idSysReg, const char *pszRegName,
                                               uint64_t *puDst, uint32_t idxGprDst) RT_NOEXCEPT

{
    RT_NOREF(pVCpu, idSysReg, pszRegName, idxGprDst);
    *puDst = 0;
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


DECLHIDDEN(VBOXSTRICTRC) iemCImplA64_msr_novar(PVMCPU pVCpu, uint32_t idSysReg, const char *pszRegName,
                                               uint64_t uValue, uint32_t idxGprSrc) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, idSysReg, pszRegName, uValue, idxGprSrc);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


DECLHIDDEN(VBOXSTRICTRC) iemCImplA64_mrs_fallback(PVMCPU pVCpu, uint32_t idxGprDst, uint32_t idSysReg) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, idxGprDst, idSysReg);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


DECLHIDDEN(VBOXSTRICTRC)
iemCImplA64_msr_fallback(PVMCPU pVCpu, uint32_t idSysReg, uint64_t uValue, uint32_t idxGprSrc) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, idSysReg, uValue, idxGprSrc);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


/**
 * Recalculates fExec after updating a (relevant) system register.
 */
DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpRecalcFlags(PVMCPU pVCpu, VBOXSTRICTRC rcStrict)
{
    uint32_t const fExecOld = pVCpu->iem.s.fExec;
    uint32_t const fExecNew = iemCalcExecFlags(pVCpu) | (fExecOld & IEM_F_USER_OPTS);
    if (fExecNew != fExecOld)
        Log(("IEM: sysreg: fExec %#x -> %#x (changed %#x)\\n", fExecOld, fExecNew, pVCpu->iem.s.fExec ^ fExecNew));
    pVCpu->iem.s.fExec = fExecNew;
    return rcStrict;
}


/**
 * Recalculates fExec and EL1 PGM mode after updating a (relevant) system register.
 */
DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpRecalcFlagsAndPgmModeEl1(PVMCPU pVCpu, VBOXSTRICTRC rcStrict)
{
    int rc = PGMChangeMode(pVCpu, 1, pVCpu->cpum.GstCtx.Sctlr.u64, pVCpu->cpum.GstCtx.Tcr.u64);
    AssertRCReturn(rc, rc);
    return iemCImplHlpRecalcFlags(pVCpu, rcStrict);
}


/**
 * Recalculates fExec and EL2 PGM mode after updating a (relevant) system register.
 */
DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpRecalcFlagsAndPgmModeEl2(PVMCPU pVCpu, VBOXSTRICTRC rcStrict)
{
    /** @todo does this influence EL1?   */
    int rc = PGMChangeMode(pVCpu, 2, pVCpu->cpum.GstCtx.SctlrEl2.u64, pVCpu->cpum.GstCtx.TcrEl2.u64);
    AssertRCReturn(rc, rc);
    return iemCImplHlpRecalcFlags(pVCpu, rcStrict);
}


/**
 * Recalculates fExec and EL3 PGM mode after updating a (relevant) system register.
 */
DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpRecalcFlagsAndPgmModeEl3(PVMCPU pVCpu, VBOXSTRICTRC rcStrict)
{
#if 0 /** @todo EL3 */
    int rc = PGMChangeMode(pVCpu, 3, pVCpu->cpum.GstCtx.SctlrEl3.u64, pVCpu->cpum.GstCtx.TcrEl3.u64);
    AssertRCReturn(rc, rc);
#else
    AssertFailed();
#endif
    return iemCImplHlpRecalcFlags(pVCpu, rcStrict);
}


DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpNVMemReadU64(PVMCPU pVCpu, uint32_t off, uint64_t *puDst) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, off, puDst);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpNVMemWriteU64(PVMCPU pVCpu, uint32_t off, uint64_t uValue) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, off, uValue);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpReadDbgDtrEl0U64(PVMCPU pVCpu, uint64_t *puDst) RT_NOEXCEPT
{
    RT_NOREF(pVCpu);
    *puDst = 0;
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


DECLHIDDEN(VBOXSTRICTRC) iemCImplHlpReadDbgDtrEl0U32(PVMCPU pVCpu, uint64_t *puDst) RT_NOEXCEPT
{
    RT_NOREF(pVCpu);
    *puDst = 0;
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
}


DECLHIDDEN(uint64_t)     iemCImplHlpGetIdSysReg(PVMCPU pVCpu, uint32_t idSysReg) RT_NOEXCEPT
{
    RT_NOREF(pVCpu, idSysReg);
    AssertFailedReturn(0);
}


DECLHIDDEN(uint64_t)     iemCImplHlpGetAmUserEnrEl0(PVMCPU pVCpu) RT_NOEXCEPT
{
    RT_NOREF(pVCpu);
    AssertFailedReturn(0);
}


DECLHIDDEN(uint64_t)     iemCImplHlpGetPmUserEnrEl0(PVMCPU pVCpu) RT_NOEXCEPT
{
    RT_NOREF(pVCpu);
    AssertFailedReturn(0);
}


DECLHIDDEN(uint64_t)     iemCImplHlpGetPmSelrEl0(PVMCPU pVCpu) RT_NOEXCEPT
{
    RT_NOREF(pVCpu);
    AssertFailedReturn(0);
}



DECLHIDDEN(uint64_t)     iemCImplHlpGetPmUacrEl1(PVMCPU pVCpu) RT_NOEXCEPT
{
    RT_NOREF(pVCpu);
    AssertFailedReturn(0);
}


DECLHIDDEN(uint64_t)     iemCImplHlpGetPhysicalSystemTimerCount(PVMCPU pVCpu) RT_NOEXCEPT
{
    RT_NOREF(pVCpu);
    AssertFailedReturn(0);
}

