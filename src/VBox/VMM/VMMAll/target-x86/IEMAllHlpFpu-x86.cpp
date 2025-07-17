/* $Id: IEMAllHlpFpu-x86.cpp 108427 2025-02-17 15:24:14Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - x86 target, FPU helpers.
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
#ifdef IN_RING0
# define VBOX_VMM_TARGET_X86
#endif
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/tm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/dbgftrace.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/x86.h>

#include "IEMInline-x86.h"


/** @name   FPU access and helpers.
 *
 * @{
 */

/**
 * Updates the x87.DS and FPUDP registers.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pFpuCtx             The FPU context.
 * @param   iEffSeg             The effective segment register.
 * @param   GCPtrEff            The effective address relative to @a iEffSeg.
 */
DECLINLINE(void) iemFpuUpdateDP(PVMCPUCC pVCpu, PX86FXSTATE pFpuCtx, uint8_t iEffSeg, RTGCPTR GCPtrEff)
{
    RTSEL sel;
    switch (iEffSeg)
    {
        case X86_SREG_DS: sel = pVCpu->cpum.GstCtx.ds.Sel; break;
        case X86_SREG_SS: sel = pVCpu->cpum.GstCtx.ss.Sel; break;
        case X86_SREG_CS: sel = pVCpu->cpum.GstCtx.cs.Sel; break;
        case X86_SREG_ES: sel = pVCpu->cpum.GstCtx.es.Sel; break;
        case X86_SREG_FS: sel = pVCpu->cpum.GstCtx.fs.Sel; break;
        case X86_SREG_GS: sel = pVCpu->cpum.GstCtx.gs.Sel; break;
        default:
            AssertMsgFailed(("%d\n", iEffSeg));
            sel = pVCpu->cpum.GstCtx.ds.Sel;
    }
    /** @todo pFpuCtx->DS and FPUDP needs to be kept separately. */
    if (IEM_IS_REAL_OR_V86_MODE(pVCpu))
    {
        pFpuCtx->DS    = 0;
        pFpuCtx->FPUDP = (uint32_t)GCPtrEff + ((uint32_t)sel << 4);
    }
    else if (!IEM_IS_LONG_MODE(pVCpu)) /** @todo this is weird. explain. */
    {
        pFpuCtx->DS    = sel;
        pFpuCtx->FPUDP = GCPtrEff;
    }
    else
        *(uint64_t *)&pFpuCtx->FPUDP = GCPtrEff;
}


/**
 * Rotates the stack registers in the push direction.
 *
 * @param   pFpuCtx             The FPU context.
 * @remarks This is a complete waste of time, but fxsave stores the registers in
 *          stack order.
 */
DECLINLINE(void) iemFpuRotateStackPush(PX86FXSTATE pFpuCtx)
{
    RTFLOAT80U r80Tmp = pFpuCtx->aRegs[7].r80;
    pFpuCtx->aRegs[7].r80 = pFpuCtx->aRegs[6].r80;
    pFpuCtx->aRegs[6].r80 = pFpuCtx->aRegs[5].r80;
    pFpuCtx->aRegs[5].r80 = pFpuCtx->aRegs[4].r80;
    pFpuCtx->aRegs[4].r80 = pFpuCtx->aRegs[3].r80;
    pFpuCtx->aRegs[3].r80 = pFpuCtx->aRegs[2].r80;
    pFpuCtx->aRegs[2].r80 = pFpuCtx->aRegs[1].r80;
    pFpuCtx->aRegs[1].r80 = pFpuCtx->aRegs[0].r80;
    pFpuCtx->aRegs[0].r80 = r80Tmp;
}


/**
 * Rotates the stack registers in the pop direction.
 *
 * @param   pFpuCtx             The FPU context.
 * @remarks This is a complete waste of time, but fxsave stores the registers in
 *          stack order.
 */
DECLINLINE(void) iemFpuRotateStackPop(PX86FXSTATE pFpuCtx)
{
    RTFLOAT80U r80Tmp = pFpuCtx->aRegs[0].r80;
    pFpuCtx->aRegs[0].r80 = pFpuCtx->aRegs[1].r80;
    pFpuCtx->aRegs[1].r80 = pFpuCtx->aRegs[2].r80;
    pFpuCtx->aRegs[2].r80 = pFpuCtx->aRegs[3].r80;
    pFpuCtx->aRegs[3].r80 = pFpuCtx->aRegs[4].r80;
    pFpuCtx->aRegs[4].r80 = pFpuCtx->aRegs[5].r80;
    pFpuCtx->aRegs[5].r80 = pFpuCtx->aRegs[6].r80;
    pFpuCtx->aRegs[6].r80 = pFpuCtx->aRegs[7].r80;
    pFpuCtx->aRegs[7].r80 = r80Tmp;
}


/**
 * Updates FSW and pushes a FPU result onto the FPU stack if no pending
 * exception prevents it.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pResult             The FPU operation result to push.
 * @param   pFpuCtx             The FPU context.
 */
static void iemFpuMaybePushResult(PVMCPU pVCpu, PIEMFPURESULT pResult, PX86FXSTATE pFpuCtx) RT_NOEXCEPT
{
    /* Update FSW and bail if there are pending exceptions afterwards. */
    uint16_t fFsw = pFpuCtx->FSW & ~X86_FSW_C_MASK;
    fFsw |= pResult->FSW & ~X86_FSW_TOP_MASK;
    if (   (fFsw         & (X86_FSW_IE | X86_FSW_ZE | X86_FSW_DE))
        & ~(pFpuCtx->FCW & (X86_FCW_IM | X86_FCW_ZM | X86_FCW_DM)))
    {
        if ((fFsw & X86_FSW_ES) && !(pFpuCtx->FCW & X86_FSW_ES))
            Log11(("iemFpuMaybePushResult: %04x:%08RX64: FSW %#x -> %#x\n",
                   pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pFpuCtx->FSW, fFsw));
        pFpuCtx->FSW = fFsw;
        return;
    }

    uint16_t iNewTop = (X86_FSW_TOP_GET(fFsw) + 7) & X86_FSW_TOP_SMASK;
    if (!(pFpuCtx->FTW & RT_BIT(iNewTop)))
    {
        /* All is fine, push the actual value. */
        pFpuCtx->FTW |= RT_BIT(iNewTop);
        pFpuCtx->aRegs[7].r80 = pResult->r80Result;
    }
    else if (pFpuCtx->FCW & X86_FCW_IM)
    {
        /* Masked stack overflow, push QNaN. */
        fFsw |= X86_FSW_IE | X86_FSW_SF | X86_FSW_C1;
        iemFpuStoreQNan(&pFpuCtx->aRegs[7].r80);
    }
    else
    {
        /* Raise stack overflow, don't push anything. */
        pFpuCtx->FSW |= pResult->FSW & ~X86_FSW_C_MASK;
        pFpuCtx->FSW |= X86_FSW_IE | X86_FSW_SF | X86_FSW_C1 | X86_FSW_B | X86_FSW_ES;
        Log11(("iemFpuMaybePushResult: %04x:%08RX64: stack overflow (FSW=%#x)\n",
               pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pFpuCtx->FSW));
        return;
    }

    fFsw &= ~X86_FSW_TOP_MASK;
    fFsw |= iNewTop << X86_FSW_TOP_SHIFT;
    pFpuCtx->FSW = fFsw;

    iemFpuRotateStackPush(pFpuCtx);
    RT_NOREF(pVCpu);
}


/**
 * Stores a result in a FPU register and updates the FSW and FTW.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pFpuCtx             The FPU context.
 * @param   pResult             The result to store.
 * @param   iStReg              Which FPU register to store it in.
 */
static void iemFpuStoreResultOnly(PVMCPU pVCpu, PX86FXSTATE pFpuCtx, PIEMFPURESULT pResult, uint8_t iStReg) RT_NOEXCEPT
{
    Assert(iStReg < 8);
    uint16_t       fNewFsw = pFpuCtx->FSW;
    uint16_t const iReg    = (X86_FSW_TOP_GET(fNewFsw) + iStReg) & X86_FSW_TOP_SMASK;
    fNewFsw &= ~X86_FSW_C_MASK;
    fNewFsw |= pResult->FSW & ~X86_FSW_TOP_MASK;
    if ((fNewFsw & X86_FSW_ES) && !(pFpuCtx->FSW & X86_FSW_ES))
        Log11(("iemFpuStoreResultOnly: %04x:%08RX64: FSW %#x -> %#x\n",
               pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pFpuCtx->FSW, fNewFsw));
    pFpuCtx->FSW  = fNewFsw;
    pFpuCtx->FTW |= RT_BIT(iReg);
    pFpuCtx->aRegs[iStReg].r80 = pResult->r80Result;
    RT_NOREF(pVCpu);
}


/**
 * Only updates the FPU status word (FSW) with the result of the current
 * instruction.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pFpuCtx             The FPU context.
 * @param   u16FSW              The FSW output of the current instruction.
 */
static void iemFpuUpdateFSWOnly(PVMCPU pVCpu, PX86FXSTATE pFpuCtx, uint16_t u16FSW) RT_NOEXCEPT
{
    uint16_t fNewFsw = pFpuCtx->FSW;
    fNewFsw &= ~X86_FSW_C_MASK;
    fNewFsw |= u16FSW & ~X86_FSW_TOP_MASK;
    if ((fNewFsw & X86_FSW_ES) && !(pFpuCtx->FSW & X86_FSW_ES))
        Log11(("iemFpuStoreResultOnly: %04x:%08RX64: FSW %#x -> %#x\n",
               pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pFpuCtx->FSW, fNewFsw));
    pFpuCtx->FSW = fNewFsw;
    RT_NOREF(pVCpu);
}


/**
 * Pops one item off the FPU stack if no pending exception prevents it.
 *
 * @param   pFpuCtx             The FPU context.
 */
static void iemFpuMaybePopOne(PX86FXSTATE pFpuCtx) RT_NOEXCEPT
{
    /* Check pending exceptions. */
    uint16_t uFSW = pFpuCtx->FSW;
    if (   (pFpuCtx->FSW & (X86_FSW_IE | X86_FSW_ZE | X86_FSW_DE))
        & ~(pFpuCtx->FCW & (X86_FCW_IM | X86_FCW_ZM | X86_FCW_DM)))
        return;

    /* TOP--. */
    uint16_t iOldTop = uFSW & X86_FSW_TOP_MASK;
    uFSW &= ~X86_FSW_TOP_MASK;
    uFSW |= (iOldTop + (UINT16_C(9) << X86_FSW_TOP_SHIFT)) & X86_FSW_TOP_MASK;
    pFpuCtx->FSW = uFSW;

    /* Mark the previous ST0 as empty. */
    iOldTop >>= X86_FSW_TOP_SHIFT;
    pFpuCtx->FTW &= ~RT_BIT(iOldTop);

    /* Rotate the registers. */
    iemFpuRotateStackPop(pFpuCtx);
}


/**
 * Pushes a FPU result onto the FPU stack if no pending exception prevents it.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pResult             The FPU operation result to push.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuPushResult(PVMCPUCC pVCpu, PIEMFPURESULT pResult, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuMaybePushResult(pVCpu, pResult, pFpuCtx);
}


/**
 * Pushes a FPU result onto the FPU stack if no pending exception prevents it,
 * and sets FPUDP and FPUDS.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pResult             The FPU operation result to push.
 * @param   iEffSeg             The effective segment register.
 * @param   GCPtrEff            The effective address relative to @a iEffSeg.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuPushResultWithMemOp(PVMCPUCC pVCpu, PIEMFPURESULT pResult, uint8_t iEffSeg, RTGCPTR GCPtrEff,
                               uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateDP(pVCpu, pFpuCtx, iEffSeg, GCPtrEff);
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuMaybePushResult(pVCpu, pResult, pFpuCtx);
}


/**
 * Replace ST0 with the first value and push the second onto the FPU stack,
 * unless a pending exception prevents it.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pResult             The FPU operation result to store and push.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuPushResultTwo(PVMCPUCC pVCpu, PIEMFPURESULTTWO pResult, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);

    /* Update FSW and bail if there are pending exceptions afterwards. */
    uint16_t fFsw = pFpuCtx->FSW & ~X86_FSW_C_MASK;
    fFsw |= pResult->FSW & ~X86_FSW_TOP_MASK;
    if (   (fFsw         & (X86_FSW_IE | X86_FSW_ZE | X86_FSW_DE))
        & ~(pFpuCtx->FCW & (X86_FCW_IM | X86_FCW_ZM | X86_FCW_DM)))
    {
        if ((fFsw & X86_FSW_ES) && !(pFpuCtx->FSW & X86_FSW_ES))
            Log11(("iemFpuPushResultTwo: %04x:%08RX64: FSW %#x -> %#x\n",
                   pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pFpuCtx->FSW, fFsw));
        pFpuCtx->FSW = fFsw;
        return;
    }

    uint16_t iNewTop = (X86_FSW_TOP_GET(fFsw) + 7) & X86_FSW_TOP_SMASK;
    if (!(pFpuCtx->FTW & RT_BIT(iNewTop)))
    {
        /* All is fine, push the actual value. */
        pFpuCtx->FTW |= RT_BIT(iNewTop);
        pFpuCtx->aRegs[0].r80 = pResult->r80Result1;
        pFpuCtx->aRegs[7].r80 = pResult->r80Result2;
    }
    else if (pFpuCtx->FCW & X86_FCW_IM)
    {
        /* Masked stack overflow, push QNaN. */
        fFsw |= X86_FSW_IE | X86_FSW_SF | X86_FSW_C1;
        iemFpuStoreQNan(&pFpuCtx->aRegs[0].r80);
        iemFpuStoreQNan(&pFpuCtx->aRegs[7].r80);
    }
    else
    {
        /* Raise stack overflow, don't push anything. */
        pFpuCtx->FSW |= pResult->FSW & ~X86_FSW_C_MASK;
        pFpuCtx->FSW |= X86_FSW_IE | X86_FSW_SF | X86_FSW_C1 | X86_FSW_B | X86_FSW_ES;
        Log11(("iemFpuPushResultTwo: %04x:%08RX64: stack overflow (FSW=%#x)\n",
               pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pFpuCtx->FSW));
        return;
    }

    fFsw &= ~X86_FSW_TOP_MASK;
    fFsw |= iNewTop << X86_FSW_TOP_SHIFT;
    pFpuCtx->FSW = fFsw;

    iemFpuRotateStackPush(pFpuCtx);
}


/**
 * Stores a result in a FPU register, updates the FSW, FTW, FPUIP, FPUCS, and
 * FOP.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pResult             The result to store.
 * @param   iStReg              Which FPU register to store it in.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuStoreResult(PVMCPUCC pVCpu, PIEMFPURESULT pResult, uint8_t iStReg, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuStoreResultOnly(pVCpu, pFpuCtx, pResult, iStReg);
}


/**
 * Stores a result in a FPU register, updates the FSW, FTW, FPUIP, FPUCS, and
 * FOP, and then pops the stack.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pResult             The result to store.
 * @param   iStReg              Which FPU register to store it in.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuStoreResultThenPop(PVMCPUCC pVCpu, PIEMFPURESULT pResult, uint8_t iStReg, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuStoreResultOnly(pVCpu, pFpuCtx, pResult, iStReg);
    iemFpuMaybePopOne(pFpuCtx);
}


/**
 * Stores a result in a FPU register, updates the FSW, FTW, FPUIP, FPUCS, FOP,
 * FPUDP, and FPUDS.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pResult             The result to store.
 * @param   iStReg              Which FPU register to store it in.
 * @param   iEffSeg             The effective memory operand selector register.
 * @param   GCPtrEff            The effective memory operand offset.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuStoreResultWithMemOp(PVMCPUCC pVCpu, PIEMFPURESULT pResult, uint8_t iStReg,
                                uint8_t iEffSeg, RTGCPTR GCPtrEff, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateDP(pVCpu, pFpuCtx, iEffSeg, GCPtrEff);
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuStoreResultOnly(pVCpu, pFpuCtx, pResult, iStReg);
}


/**
 * Stores a result in a FPU register, updates the FSW, FTW, FPUIP, FPUCS, FOP,
 * FPUDP, and FPUDS, and then pops the stack.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pResult             The result to store.
 * @param   iStReg              Which FPU register to store it in.
 * @param   iEffSeg             The effective memory operand selector register.
 * @param   GCPtrEff            The effective memory operand offset.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuStoreResultWithMemOpThenPop(PVMCPUCC pVCpu, PIEMFPURESULT pResult,
                                       uint8_t iStReg, uint8_t iEffSeg, RTGCPTR GCPtrEff, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateDP(pVCpu, pFpuCtx, iEffSeg, GCPtrEff);
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuStoreResultOnly(pVCpu, pFpuCtx, pResult, iStReg);
    iemFpuMaybePopOne(pFpuCtx);
}


/**
 * Updates the FOP, FPUIP, and FPUCS.  For FNOP.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuUpdateOpcodeAndIp(PVMCPUCC pVCpu, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
}


/**
 * Updates the FSW, FOP, FPUIP, and FPUCS.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   u16FSW              The FSW from the current instruction.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuUpdateFSW(PVMCPUCC pVCpu, uint16_t u16FSW, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuUpdateFSWOnly(pVCpu, pFpuCtx, u16FSW);
}


/**
 * Updates the FSW, FOP, FPUIP, and FPUCS, then pops the stack.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   u16FSW              The FSW from the current instruction.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuUpdateFSWThenPop(PVMCPUCC pVCpu, uint16_t u16FSW, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuUpdateFSWOnly(pVCpu, pFpuCtx, u16FSW);
    iemFpuMaybePopOne(pFpuCtx);
}


/**
 * Updates the FSW, FOP, FPUIP, FPUCS, FPUDP, and FPUDS.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   u16FSW              The FSW from the current instruction.
 * @param   iEffSeg             The effective memory operand selector register.
 * @param   GCPtrEff            The effective memory operand offset.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuUpdateFSWWithMemOp(PVMCPUCC pVCpu, uint16_t u16FSW, uint8_t iEffSeg, RTGCPTR GCPtrEff, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateDP(pVCpu, pFpuCtx, iEffSeg, GCPtrEff);
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuUpdateFSWOnly(pVCpu, pFpuCtx, u16FSW);
}


/**
 * Updates the FSW, FOP, FPUIP, and FPUCS, then pops the stack twice.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   u16FSW              The FSW from the current instruction.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuUpdateFSWThenPopPop(PVMCPUCC pVCpu, uint16_t u16FSW, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuUpdateFSWOnly(pVCpu, pFpuCtx, u16FSW);
    iemFpuMaybePopOne(pFpuCtx);
    iemFpuMaybePopOne(pFpuCtx);
}


/**
 * Updates the FSW, FOP, FPUIP, FPUCS, FPUDP, and FPUDS, then pops the stack.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   u16FSW              The FSW from the current instruction.
 * @param   iEffSeg             The effective memory operand selector register.
 * @param   GCPtrEff            The effective memory operand offset.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuUpdateFSWWithMemOpThenPop(PVMCPUCC pVCpu, uint16_t u16FSW, uint8_t iEffSeg, RTGCPTR GCPtrEff,
                                     uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateDP(pVCpu, pFpuCtx, iEffSeg, GCPtrEff);
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuUpdateFSWOnly(pVCpu, pFpuCtx, u16FSW);
    iemFpuMaybePopOne(pFpuCtx);
}


/**
 * Worker routine for raising an FPU stack underflow exception.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pFpuCtx             The FPU context.
 * @param   iStReg              The stack register being accessed.
 */
static void iemFpuStackUnderflowOnly(PVMCPU pVCpu, PX86FXSTATE pFpuCtx, uint8_t iStReg)
{
    Assert(iStReg < 8 || iStReg == UINT8_MAX);
    if (pFpuCtx->FCW & X86_FCW_IM)
    {
        /* Masked underflow. */
        pFpuCtx->FSW &= ~X86_FSW_C_MASK;
        pFpuCtx->FSW |= X86_FSW_IE | X86_FSW_SF;
        uint16_t iReg = (X86_FSW_TOP_GET(pFpuCtx->FSW) + iStReg) & X86_FSW_TOP_SMASK;
        if (iStReg != UINT8_MAX)
        {
            pFpuCtx->FTW |= RT_BIT(iReg);
            iemFpuStoreQNan(&pFpuCtx->aRegs[iStReg].r80);
        }
    }
    else
    {
        pFpuCtx->FSW &= ~X86_FSW_C_MASK;
        pFpuCtx->FSW |= X86_FSW_IE | X86_FSW_SF | X86_FSW_ES | X86_FSW_B;
        Log11(("iemFpuStackUnderflowOnly: %04x:%08RX64: underflow (FSW=%#x)\n",
               pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pFpuCtx->FSW));
    }
    RT_NOREF(pVCpu);
}


/**
 * Raises a FPU stack underflow exception.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iStReg              The destination register that should be loaded
 *                              with QNaN if \#IS is not masked. Specify
 *                              UINT8_MAX if none (like for fcom).
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuStackUnderflow(PVMCPUCC pVCpu, uint8_t iStReg, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuStackUnderflowOnly(pVCpu, pFpuCtx, iStReg);
}


void iemFpuStackUnderflowWithMemOp(PVMCPUCC pVCpu, uint8_t iStReg, uint8_t iEffSeg, RTGCPTR GCPtrEff, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateDP(pVCpu, pFpuCtx, iEffSeg, GCPtrEff);
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuStackUnderflowOnly(pVCpu, pFpuCtx, iStReg);
}


void iemFpuStackUnderflowThenPop(PVMCPUCC pVCpu, uint8_t iStReg, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuStackUnderflowOnly(pVCpu, pFpuCtx, iStReg);
    iemFpuMaybePopOne(pFpuCtx);
}


void iemFpuStackUnderflowWithMemOpThenPop(PVMCPUCC pVCpu, uint8_t iStReg, uint8_t iEffSeg, RTGCPTR GCPtrEff,
                                          uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateDP(pVCpu, pFpuCtx, iEffSeg, GCPtrEff);
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuStackUnderflowOnly(pVCpu, pFpuCtx, iStReg);
    iemFpuMaybePopOne(pFpuCtx);
}


void iemFpuStackUnderflowThenPopPop(PVMCPUCC pVCpu, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuStackUnderflowOnly(pVCpu, pFpuCtx, UINT8_MAX);
    iemFpuMaybePopOne(pFpuCtx);
    iemFpuMaybePopOne(pFpuCtx);
}


void iemFpuStackPushUnderflow(PVMCPUCC pVCpu, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);

    if (pFpuCtx->FCW & X86_FCW_IM)
    {
        /* Masked overflow - Push QNaN. */
        uint16_t iNewTop = (X86_FSW_TOP_GET(pFpuCtx->FSW) + 7) & X86_FSW_TOP_SMASK;
        pFpuCtx->FSW &= ~(X86_FSW_TOP_MASK | X86_FSW_C_MASK);
        pFpuCtx->FSW |= X86_FSW_IE | X86_FSW_SF;
        pFpuCtx->FSW |= iNewTop << X86_FSW_TOP_SHIFT;
        pFpuCtx->FTW |= RT_BIT(iNewTop);
        iemFpuStoreQNan(&pFpuCtx->aRegs[7].r80);
        iemFpuRotateStackPush(pFpuCtx);
    }
    else
    {
        /* Exception pending - don't change TOP or the register stack. */
        pFpuCtx->FSW &= ~X86_FSW_C_MASK;
        pFpuCtx->FSW |= X86_FSW_IE | X86_FSW_SF | X86_FSW_ES | X86_FSW_B;
        Log11(("iemFpuStackPushUnderflow: %04x:%08RX64: underflow (FSW=%#x)\n",
               pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pFpuCtx->FSW));
    }
}


void iemFpuStackPushUnderflowTwo(PVMCPUCC pVCpu, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);

    if (pFpuCtx->FCW & X86_FCW_IM)
    {
        /* Masked overflow - Push QNaN. */
        uint16_t iNewTop = (X86_FSW_TOP_GET(pFpuCtx->FSW) + 7) & X86_FSW_TOP_SMASK;
        pFpuCtx->FSW &= ~(X86_FSW_TOP_MASK | X86_FSW_C_MASK);
        pFpuCtx->FSW |= X86_FSW_IE | X86_FSW_SF;
        pFpuCtx->FSW |= iNewTop << X86_FSW_TOP_SHIFT;
        pFpuCtx->FTW |= RT_BIT(iNewTop);
        iemFpuStoreQNan(&pFpuCtx->aRegs[0].r80);
        iemFpuStoreQNan(&pFpuCtx->aRegs[7].r80);
        iemFpuRotateStackPush(pFpuCtx);
    }
    else
    {
        /* Exception pending - don't change TOP or the register stack. */
        pFpuCtx->FSW &= ~X86_FSW_C_MASK;
        pFpuCtx->FSW |= X86_FSW_IE | X86_FSW_SF | X86_FSW_ES | X86_FSW_B;
        Log11(("iemFpuStackPushUnderflowTwo: %04x:%08RX64: underflow (FSW=%#x)\n",
               pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pFpuCtx->FSW));
    }
}


/**
 * Worker routine for raising an FPU stack overflow exception on a push.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pFpuCtx             The FPU context.
 */
static void iemFpuStackPushOverflowOnly(PVMCPU pVCpu, PX86FXSTATE pFpuCtx) RT_NOEXCEPT
{
    if (pFpuCtx->FCW & X86_FCW_IM)
    {
        /* Masked overflow. */
        uint16_t iNewTop = (X86_FSW_TOP_GET(pFpuCtx->FSW) + 7) & X86_FSW_TOP_SMASK;
        pFpuCtx->FSW &= ~(X86_FSW_TOP_MASK | X86_FSW_C_MASK);
        pFpuCtx->FSW |= X86_FSW_C1 | X86_FSW_IE | X86_FSW_SF;
        pFpuCtx->FSW |= iNewTop << X86_FSW_TOP_SHIFT;
        pFpuCtx->FTW |= RT_BIT(iNewTop);
        iemFpuStoreQNan(&pFpuCtx->aRegs[7].r80);
        iemFpuRotateStackPush(pFpuCtx);
    }
    else
    {
        /* Exception pending - don't change TOP or the register stack. */
        pFpuCtx->FSW &= ~X86_FSW_C_MASK;
        pFpuCtx->FSW |= X86_FSW_C1 | X86_FSW_IE | X86_FSW_SF | X86_FSW_ES | X86_FSW_B;
        Log11(("iemFpuStackPushOverflowOnly: %04x:%08RX64: overflow (FSW=%#x)\n",
               pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pFpuCtx->FSW));
    }
    RT_NOREF(pVCpu);
}


/**
 * Raises a FPU stack overflow exception on a push.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuStackPushOverflow(PVMCPUCC pVCpu, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuStackPushOverflowOnly(pVCpu, pFpuCtx);
}


/**
 * Raises a FPU stack overflow exception on a push with a memory operand.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iEffSeg             The effective memory operand selector register.
 * @param   GCPtrEff            The effective memory operand offset.
 * @param   uFpuOpcode          The FPU opcode value.
 */
void iemFpuStackPushOverflowWithMemOp(PVMCPUCC pVCpu, uint8_t iEffSeg, RTGCPTR GCPtrEff, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemFpuUpdateDP(pVCpu, pFpuCtx, iEffSeg, GCPtrEff);
    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemFpuStackPushOverflowOnly(pVCpu, pFpuCtx);
}

/** @}  */

