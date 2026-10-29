/* $Id: IEMAllXcpt-x86.cpp 112403 2026-01-11 19:29:08Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - x86 target, exceptions & interrupts.
 */

/*
 * Copyright (C) 2011-2026 Oracle and/or its affiliates.
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
#include <VBox/vmm/em.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/gcm.h>
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
# include <VBox/vmm/hm_svm.h>
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
# include <VBox/vmm/hmvmxinline.h>
#endif
#include <VBox/vmm/tm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/dbgftrace.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/x86.h>

#include "IEMInline.h"
#include "IEMInline-x86.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * CPU exception classes.
 */
typedef enum IEMXCPTCLASS
{
    IEMXCPTCLASS_BENIGN,
    IEMXCPTCLASS_CONTRIBUTORY,
    IEMXCPTCLASS_PAGE_FAULT,
    IEMXCPTCLASS_DOUBLE_FAULT
} IEMXCPTCLASS;



/** @name  Misc Worker Functions.
 * @{
 */

/**
 * Gets the exception class for the specified exception vector.
 *
 * @returns The class of the specified exception.
 * @param   uVector       The exception vector.
 */
static IEMXCPTCLASS iemGetXcptClass(uint8_t uVector) RT_NOEXCEPT
{
    Assert(uVector <= X86_XCPT_LAST);
    switch (uVector)
    {
        case X86_XCPT_DE:
        case X86_XCPT_TS:
        case X86_XCPT_NP:
        case X86_XCPT_SS:
        case X86_XCPT_GP:
        case X86_XCPT_SX:   /* AMD only */
            return IEMXCPTCLASS_CONTRIBUTORY;

        case X86_XCPT_PF:
        case X86_XCPT_VE:   /* Intel only */
            return IEMXCPTCLASS_PAGE_FAULT;

        case X86_XCPT_DF:
            return IEMXCPTCLASS_DOUBLE_FAULT;
    }
    return IEMXCPTCLASS_BENIGN;
}


/**
 * Evaluates how to handle an exception caused during delivery of another event
 * (exception / interrupt).
 *
 * @returns How to handle the recursive exception.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   fPrevFlags          The flags of the previous event.
 * @param   uPrevVector         The vector of the previous event.
 * @param   fCurFlags           The flags of the current exception.
 * @param   uCurVector          The vector of the current exception.
 * @param   pfXcptRaiseInfo     Where to store additional information about the
 *                              exception condition. Optional.
 */
VMM_INT_DECL(IEMXCPTRAISE) IEMEvaluateRecursiveXcpt(PVMCPUCC pVCpu, uint32_t fPrevFlags, uint8_t uPrevVector, uint32_t fCurFlags,
                                                    uint8_t uCurVector, PIEMXCPTRAISEINFO pfXcptRaiseInfo)
{
    /*
     * Only CPU exceptions can be raised while delivering other events, software interrupt
     * (INTn/INT3/INTO/ICEBP) generated exceptions cannot occur as the current (second) exception.
     */
    AssertReturn(fCurFlags & IEM_XCPT_FLAGS_T_CPU_XCPT, IEMXCPTRAISE_INVALID);
    Assert(pVCpu); RT_NOREF(pVCpu);
    Log2(("IEMEvaluateRecursiveXcpt: uPrevVector=%#x uCurVector=%#x\n", uPrevVector, uCurVector));

    IEMXCPTRAISE     enmRaise   = IEMXCPTRAISE_CURRENT_XCPT;
    IEMXCPTRAISEINFO fRaiseInfo = IEMXCPTRAISEINFO_NONE;
    if (fPrevFlags & IEM_XCPT_FLAGS_T_CPU_XCPT)
    {
        IEMXCPTCLASS enmPrevXcptClass = iemGetXcptClass(uPrevVector);
        if (enmPrevXcptClass != IEMXCPTCLASS_BENIGN)
        {
            IEMXCPTCLASS enmCurXcptClass = iemGetXcptClass(uCurVector);
            if (   enmPrevXcptClass == IEMXCPTCLASS_PAGE_FAULT
                && (   enmCurXcptClass == IEMXCPTCLASS_PAGE_FAULT
                    || enmCurXcptClass == IEMXCPTCLASS_CONTRIBUTORY))
            {
                enmRaise = IEMXCPTRAISE_DOUBLE_FAULT;
                fRaiseInfo = enmCurXcptClass == IEMXCPTCLASS_PAGE_FAULT ? IEMXCPTRAISEINFO_PF_PF
                                                                        : IEMXCPTRAISEINFO_PF_CONTRIBUTORY_XCPT;
                Log2(("IEMEvaluateRecursiveXcpt: Vectoring page fault. uPrevVector=%#x uCurVector=%#x uCr2=%#RX64\n", uPrevVector,
                      uCurVector, pVCpu->cpum.GstCtx.cr2));
            }
            else if (   enmPrevXcptClass == IEMXCPTCLASS_CONTRIBUTORY
                     && enmCurXcptClass  == IEMXCPTCLASS_CONTRIBUTORY)
            {
                enmRaise = IEMXCPTRAISE_DOUBLE_FAULT;
                Log2(("IEMEvaluateRecursiveXcpt: uPrevVector=%#x uCurVector=%#x -> #DF\n", uPrevVector, uCurVector));
            }
            else if (   enmPrevXcptClass == IEMXCPTCLASS_DOUBLE_FAULT
                     && (   enmCurXcptClass == IEMXCPTCLASS_CONTRIBUTORY
                         || enmCurXcptClass == IEMXCPTCLASS_PAGE_FAULT))
            {
                enmRaise = IEMXCPTRAISE_TRIPLE_FAULT;
                Log2(("IEMEvaluateRecursiveXcpt: #DF handler raised a %#x exception -> triple fault\n", uCurVector));
            }
        }
        else
        {
            if (uPrevVector == X86_XCPT_NMI)
            {
                fRaiseInfo = IEMXCPTRAISEINFO_NMI_XCPT;
                if (uCurVector == X86_XCPT_PF)
                {
                    fRaiseInfo |= IEMXCPTRAISEINFO_NMI_PF;
                    Log2(("IEMEvaluateRecursiveXcpt: NMI delivery caused a page fault\n"));
                }
            }
            else if (   uPrevVector == X86_XCPT_AC
                     && uCurVector  == X86_XCPT_AC)
            {
                enmRaise   = IEMXCPTRAISE_CPU_HANG;
                fRaiseInfo = IEMXCPTRAISEINFO_AC_AC;
                Log2(("IEMEvaluateRecursiveXcpt: Recursive #AC - Bad guest\n"));
            }
        }
    }
    else if (fPrevFlags & IEM_XCPT_FLAGS_T_EXT_INT)
    {
        fRaiseInfo = IEMXCPTRAISEINFO_EXT_INT_XCPT;
        if (uCurVector == X86_XCPT_PF)
            fRaiseInfo |= IEMXCPTRAISEINFO_EXT_INT_PF;
    }
    else
    {
        Assert(fPrevFlags & IEM_XCPT_FLAGS_T_SOFT_INT);
        fRaiseInfo = IEMXCPTRAISEINFO_SOFT_INT_XCPT;
    }

    if (pfXcptRaiseInfo)
        *pfXcptRaiseInfo = fRaiseInfo;
    return enmRaise;
}


/**
 * Enters the CPU shutdown state initiated by a triple fault or other
 * unrecoverable conditions.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling thread.
 */
static VBOXSTRICTRC iemInitiateCpuShutdown(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
        IEM_VMX_VMEXIT_TRIPLE_FAULT_RET(pVCpu, VMX_EXIT_TRIPLE_FAULT, 0 /* u64ExitQual */);

    if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_SHUTDOWN))
    {
        Log2(("shutdown: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_SHUTDOWN, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    RT_NOREF(pVCpu);
    return VINF_EM_TRIPLE_FAULT;
}


/**
 * Validates a new SS segment.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling thread.
 * @param   NewSS           The new SS selctor.
 * @param   uCpl            The CPL to load the stack for.
 * @param   pDesc           Where to return the descriptor.
 */
static VBOXSTRICTRC iemMiscValidateNewSS(PVMCPUCC pVCpu, RTSEL NewSS, uint8_t uCpl, PIEMSELDESC pDesc) RT_NOEXCEPT
{
    /* Null selectors are not allowed (we're not called for dispatching
       interrupts with SS=0 in long mode). */
    if (!(NewSS & X86_SEL_MASK_OFF_RPL))
    {
        Log(("iemMiscValidateNewSSandRsp: %#x - null selector -> #TS(0)\n", NewSS));
        return iemRaiseTaskSwitchFault0(pVCpu);
    }

    /** @todo testcase: check that the TSS.ssX RPL is checked.  Also check when. */
    if ((NewSS & X86_SEL_RPL) != uCpl)
    {
        Log(("iemMiscValidateNewSSandRsp: %#x - RPL and CPL (%d) differs -> #TS\n", NewSS, uCpl));
        return iemRaiseTaskSwitchFaultBySelector(pVCpu, NewSS);
    }

    /*
     * Read the descriptor.
     */
    VBOXSTRICTRC rcStrict = iemMemFetchSelDesc(pVCpu, pDesc, NewSS, X86_XCPT_TS);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /*
     * Perform the descriptor validation documented for LSS, POP SS and MOV SS.
     */
    if (!pDesc->Legacy.Gen.u1DescType)
    {
        Log(("iemMiscValidateNewSSandRsp: %#x - system selector (%#x) -> #TS\n", NewSS, pDesc->Legacy.Gen.u4Type));
        return iemRaiseTaskSwitchFaultBySelector(pVCpu, NewSS);
    }

    if (    (pDesc->Legacy.Gen.u4Type & X86_SEL_TYPE_CODE)
        || !(pDesc->Legacy.Gen.u4Type & X86_SEL_TYPE_WRITE) )
    {
        Log(("iemMiscValidateNewSSandRsp: %#x - code or read only (%#x) -> #TS\n", NewSS, pDesc->Legacy.Gen.u4Type));
        return iemRaiseTaskSwitchFaultBySelector(pVCpu, NewSS);
    }
    if (pDesc->Legacy.Gen.u2Dpl != uCpl)
    {
        Log(("iemMiscValidateNewSSandRsp: %#x - DPL (%d) and CPL (%d) differs -> #TS\n", NewSS, pDesc->Legacy.Gen.u2Dpl, uCpl));
        return iemRaiseTaskSwitchFaultBySelector(pVCpu, NewSS);
    }

    /* Is it there? */
    /** @todo testcase: Is this checked before the canonical / limit check below? */
    if (!pDesc->Legacy.Gen.u1Present)
    {
        Log(("iemMiscValidateNewSSandRsp: %#x - segment not present -> #NP\n", NewSS));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, NewSS);
    }

    return VINF_SUCCESS;
}

/** @} */


/** @name  Raising Exceptions.
 *
 * @{
 */


/**
 * Loads the specified stack far pointer from the TSS.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   uCpl            The CPL to load the stack for.
 * @param   pSelSS          Where to return the new stack segment.
 * @param   puEsp           Where to return the new stack pointer.
 */
static VBOXSTRICTRC iemRaiseLoadStackFromTss32Or16(PVMCPUCC pVCpu, uint8_t uCpl, PRTSEL pSelSS, uint32_t *puEsp) RT_NOEXCEPT
{
    VBOXSTRICTRC rcStrict;
    Assert(uCpl < 4);

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_TR | CPUMCTX_EXTRN_GDTR | CPUMCTX_EXTRN_LDTR);
    switch (pVCpu->cpum.GstCtx.tr.Attr.n.u4Type)
    {
        /*
         * 16-bit TSS (X86TSS16).
         */
        case X86_SEL_TYPE_SYS_286_TSS_AVAIL: AssertFailed(); RT_FALL_THRU();
        case X86_SEL_TYPE_SYS_286_TSS_BUSY:
        {
            uint32_t off = uCpl * 4 + 2;
            if (off + 4 <= pVCpu->cpum.GstCtx.tr.u32Limit)
            {
                /** @todo check actual access pattern here. */
                uint32_t u32Tmp = 0; /* gcc maybe... */
                rcStrict = iemMemFetchSysU32(pVCpu, &u32Tmp, UINT8_MAX, pVCpu->cpum.GstCtx.tr.u64Base + off);
                if (rcStrict == VINF_SUCCESS)
                {
                    *puEsp  = RT_LOWORD(u32Tmp);
                    *pSelSS = RT_HIWORD(u32Tmp);
                    return VINF_SUCCESS;
                }
            }
            else
            {
                Log(("LoadStackFromTss32Or16: out of bounds! uCpl=%d, u32Limit=%#x TSS16\n", uCpl, pVCpu->cpum.GstCtx.tr.u32Limit));
                rcStrict = iemRaiseTaskSwitchFaultCurrentTSS(pVCpu);
            }
            break;
        }

        /*
         * 32-bit TSS (X86TSS32).
         */
        case X86_SEL_TYPE_SYS_386_TSS_AVAIL: AssertFailed(); RT_FALL_THRU();
        case X86_SEL_TYPE_SYS_386_TSS_BUSY:
        {
            uint32_t off = uCpl * 8 + 4;
            if (off + 7 <= pVCpu->cpum.GstCtx.tr.u32Limit)
            {
/** @todo check actual access pattern here. */
                uint64_t u64Tmp;
                rcStrict = iemMemFetchSysU64(pVCpu, &u64Tmp, UINT8_MAX, pVCpu->cpum.GstCtx.tr.u64Base + off);
                if (rcStrict == VINF_SUCCESS)
                {
                    *puEsp  = u64Tmp & UINT32_MAX;
                    *pSelSS = (RTSEL)(u64Tmp >> 32);
                    return VINF_SUCCESS;
                }
            }
            else
            {
                Log(("LoadStackFromTss32Or16: out of bounds! uCpl=%d, u32Limit=%#x TSS16\n", uCpl, pVCpu->cpum.GstCtx.tr.u32Limit));
                rcStrict = iemRaiseTaskSwitchFaultCurrentTSS(pVCpu);
            }
            break;
        }

        default:
            AssertFailed();
            rcStrict = VERR_IEM_IPE_4;
            break;
    }

    *puEsp  = 0; /* make gcc happy */
    *pSelSS = 0; /* make gcc happy */
    return rcStrict;
}


/**
 * Loads the specified stack pointer from the 64-bit TSS.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   uCpl            The CPL to load the stack for.
 * @param   uIst            The interrupt stack table index, 0 if to use uCpl.
 * @param   puRsp           Where to return the new stack pointer.
 */
static VBOXSTRICTRC iemRaiseLoadStackFromTss64(PVMCPUCC pVCpu, uint8_t uCpl, uint8_t uIst, uint64_t *puRsp) RT_NOEXCEPT
{
    Assert(uCpl < 4);
    Assert(uIst < 8);
    *puRsp  = 0; /* make gcc happy */

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_TR | CPUMCTX_EXTRN_GDTR | CPUMCTX_EXTRN_LDTR);
    AssertReturn(pVCpu->cpum.GstCtx.tr.Attr.n.u4Type == AMD64_SEL_TYPE_SYS_TSS_BUSY, VERR_IEM_IPE_5);

    uint32_t off;
    if (uIst)
        off = (uIst - 1) * sizeof(uint64_t) + RT_UOFFSETOF(X86TSS64, ist1);
    else
        off = uCpl * sizeof(uint64_t) + RT_UOFFSETOF(X86TSS64, rsp0);
    if (off + sizeof(uint64_t) > pVCpu->cpum.GstCtx.tr.u32Limit)
    {
        Log(("iemRaiseLoadStackFromTss64: out of bounds! uCpl=%d uIst=%d, u32Limit=%#x\n", uCpl, uIst, pVCpu->cpum.GstCtx.tr.u32Limit));
        return iemRaiseTaskSwitchFaultCurrentTSS(pVCpu);
    }

    return iemMemFetchSysU64(pVCpu, puRsp, UINT8_MAX, pVCpu->cpum.GstCtx.tr.u64Base + off);
}


/**
 * Adjust the CPU state according to the exception being raised.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   u8Vector        The exception that has been raised.
 */
DECLINLINE(void) iemRaiseXcptAdjustState(PVMCPUCC pVCpu, uint8_t u8Vector)
{
    switch (u8Vector)
    {
        case X86_XCPT_DB:
            IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_DR7);
            pVCpu->cpum.GstCtx.dr[7] &= ~X86_DR7_GD;
            break;
        /** @todo Read the AMD and Intel exception reference... */
    }
}


/**
 * Implements exceptions and interrupts for real mode.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr         The number of bytes to offset rIP by in the return
 *                          address.
 * @param   u8Vector        The interrupt / exception vector number.
 * @param   fFlags          The flags.
 * @param   uErr            The error value if IEM_XCPT_FLAGS_ERR is set.
 * @param   uCr2            The CR2 value if IEM_XCPT_FLAGS_CR2 is set.
 */
static VBOXSTRICTRC
iemRaiseXcptOrIntInRealMode(PVMCPUCC      pVCpu,
                            uint8_t     cbInstr,
                            uint8_t     u8Vector,
                            uint32_t    fFlags,
                            uint16_t    uErr,
                            uint64_t    uCr2) RT_NOEXCEPT
{
    NOREF(uErr); NOREF(uCr2);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_XCPT_MASK);

    /*
     * Read the IDT entry.
     */
    if (pVCpu->cpum.GstCtx.idtr.cbIdt < UINT32_C(4) * u8Vector + 3)
    {
        Log(("RaiseXcptOrIntInRealMode: %#x is out of bounds (%#x)\n", u8Vector, pVCpu->cpum.GstCtx.idtr.cbIdt));
        return iemRaiseGeneralProtectionFault(pVCpu, X86_TRAP_ERR_IDT | ((uint16_t)u8Vector << X86_TRAP_ERR_SEL_SHIFT));
    }
    RTFAR16 Idte;
    VBOXSTRICTRC rcStrict = iemMemFetchDataU32(pVCpu, (uint32_t *)&Idte, UINT8_MAX, pVCpu->cpum.GstCtx.idtr.pIdt + UINT32_C(4) * u8Vector);
    if (RT_UNLIKELY(rcStrict != VINF_SUCCESS))
    {
        Log(("iemRaiseXcptOrIntInRealMode: failed to fetch IDT entry! vec=%#x rc=%Rrc\n", u8Vector, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }

#ifdef LOG_ENABLED
    /* If software interrupt, try decode it if logging is enabled and such. */
    if (   (fFlags & IEM_XCPT_FLAGS_T_SOFT_INT)
        && LogIsItEnabled(RTLOGGRPFLAGS_ENABLED, LOG_GROUP_IEM_SYSCALL))
        iemLogSyscallRealModeInt(pVCpu, u8Vector, cbInstr);
#endif

    /*
     * Push the stack frame.
     */
    uint8_t   bUnmapInfo;
    uint16_t *pu16Frame;
    uint64_t  uNewRsp;
    rcStrict = iemMemStackPushBeginSpecial(pVCpu, 6, 3, (void **)&pu16Frame, &bUnmapInfo, &uNewRsp);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    uint32_t fEfl = IEMMISC_GET_EFL(pVCpu);
#if IEM_CFG_TARGET_CPU == IEMTARGETCPU_DYNAMIC
    AssertCompile(IEMTARGETCPU_8086 <= IEMTARGETCPU_186 && IEMTARGETCPU_V20 <= IEMTARGETCPU_186 && IEMTARGETCPU_286 > IEMTARGETCPU_186);
    if (ICORE(pVCpu).uTargetCpu <= IEMTARGETCPU_186)
        fEfl |= UINT16_C(0xf000);
#endif
    pu16Frame[2] = (uint16_t)fEfl;
    pu16Frame[1] = (uint16_t)pVCpu->cpum.GstCtx.cs.Sel;
    pu16Frame[0] = (fFlags & IEM_XCPT_FLAGS_T_SOFT_INT) ? pVCpu->cpum.GstCtx.ip + cbInstr : pVCpu->cpum.GstCtx.ip;
    rcStrict = iemMemStackPushCommitSpecial(pVCpu, bUnmapInfo, uNewRsp);
    if (RT_UNLIKELY(rcStrict != VINF_SUCCESS))
        return rcStrict;

    /*
     * Load the vector address into cs:ip and make exception specific state
     * adjustments.
     */
    pVCpu->cpum.GstCtx.cs.Sel           = Idte.sel;
    pVCpu->cpum.GstCtx.cs.ValidSel      = Idte.sel;
    pVCpu->cpum.GstCtx.cs.fFlags        = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.cs.u64Base       = (uint32_t)Idte.sel << 4;
    /** @todo do we load attribs and limit as well? Should we check against limit like far jump? */
    pVCpu->cpum.GstCtx.rip              = Idte.off;
    fEfl &= ~(X86_EFL_IF | X86_EFL_TF | X86_EFL_AC);
    IEMMISC_SET_EFL(pVCpu, fEfl);

    /** @todo do we actually do this in real mode? */
    if (fFlags & IEM_XCPT_FLAGS_T_CPU_XCPT)
        iemRaiseXcptAdjustState(pVCpu, u8Vector);

    /*
     * Deal with debug events that follows the exception and clear inhibit flags.
     */
    if (   !(fFlags & IEM_XCPT_FLAGS_T_SOFT_INT)
        || !(pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK))
        pVCpu->cpum.GstCtx.eflags.uBoth &= ~(CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_INHIBIT_SHADOW);
    else
    {
        Log(("iemRaiseXcptOrIntInRealMode: Raising #DB after %#x; pending=%#x\n",
             u8Vector, pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK));
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR6);
        pVCpu->cpum.GstCtx.dr[6] |= (pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK_NONSILENT)
                                  >> CPUMCTX_DBG_HIT_DRX_SHIFT;
        pVCpu->cpum.GstCtx.eflags.uBoth &= ~(CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_INHIBIT_SHADOW);
        return iemRaiseDebugException(pVCpu);
    }

    /* The IEM_F_MODE_XXX and IEM_F_X86_CPL_MASK doesn't really change here,
       so best leave them alone in case we're in a weird kind of real mode... */

    return fFlags & IEM_XCPT_FLAGS_T_CPU_XCPT ? VINF_IEM_RAISED_XCPT : VINF_SUCCESS;
}


/**
 * Loads a NULL data selector into when coming from V8086 mode.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   pSReg           Pointer to the segment register.
 */
DECLINLINE(void) iemHlpLoadNullDataSelectorOnV86Xcpt(PVMCPUCC pVCpu, PCPUMSELREG pSReg)
{
    pSReg->Sel      = 0;
    pSReg->ValidSel = 0;
    if (IEM_IS_GUEST_CPU_INTEL(pVCpu))
    {
        /* VT-x (Intel 3960x) doesn't change the base and limit, clears and sets the following attributes */
        pSReg->Attr.u &= X86DESCATTR_DT | X86DESCATTR_TYPE | X86DESCATTR_DPL | X86DESCATTR_G | X86DESCATTR_D;
        pSReg->Attr.u |= X86DESCATTR_UNUSABLE;
    }
    else
    {
        pSReg->fFlags   = CPUMSELREG_FLAGS_VALID;
        /** @todo check this on AMD-V */
        pSReg->u64Base  = 0;
        pSReg->u32Limit = 0;
    }
}


/**
 * Loads a segment selector during a task switch in V8086 mode.
 *
 * @param   pSReg           Pointer to the segment register.
 * @param   uSel            The selector value to load.
 */
DECLINLINE(void) iemHlpLoadSelectorInV86Mode(PCPUMSELREG pSReg, uint16_t uSel)
{
    /* See Intel spec. 26.3.1.2 "Checks on Guest Segment Registers". */
    pSReg->Sel      = uSel;
    pSReg->ValidSel = uSel;
    pSReg->fFlags   = CPUMSELREG_FLAGS_VALID;
    pSReg->u64Base  = uSel << 4;
    pSReg->u32Limit = 0xffff;
    pSReg->Attr.u   = 0xf3;
}


/**
 * Loads a segment selector during a task switch in protected mode.
 *
 * In this task switch scenario, we would throw \#TS exceptions rather than
 * \#GPs.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   pSReg           Pointer to the segment register.
 * @param   uSel            The new selector value.
 *
 * @remarks This does _not_ handle CS or SS.
 * @remarks This expects IEM_GET_CPL(pVCpu) to return an up to date value.
 */
static VBOXSTRICTRC iemHlpTaskSwitchLoadDataSelectorInProtMode(PVMCPUCC pVCpu, PCPUMSELREG pSReg, uint16_t uSel) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));

    /* Null data selector. */
    if (!(uSel & X86_SEL_MASK_OFF_RPL))
    {
        iemHlpLoadNullDataSelectorProt(pVCpu, pSReg, uSel);
        Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, pSReg));
        CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_HIDDEN_SEL_REGS);
        return VINF_SUCCESS;
    }

    /* Fetch the descriptor. */
    IEMSELDESC Desc;
    VBOXSTRICTRC rcStrict = iemMemFetchSelDesc(pVCpu, &Desc, uSel, X86_XCPT_TS);
    if (rcStrict != VINF_SUCCESS)
    {
        Log(("iemHlpTaskSwitchLoadDataSelectorInProtMode: failed to fetch selector. uSel=%u rc=%Rrc\n", uSel,
             VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }

    /* Must be a data segment or readable code segment. */
    if (   !Desc.Legacy.Gen.u1DescType
        || (Desc.Legacy.Gen.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_READ)) == X86_SEL_TYPE_CODE)
    {
        Log(("iemHlpTaskSwitchLoadDataSelectorInProtMode: invalid segment type. uSel=%u Desc.u4Type=%#x\n", uSel,
             Desc.Legacy.Gen.u4Type));
        return iemRaiseTaskSwitchFaultWithErr(pVCpu, uSel & X86_SEL_MASK_OFF_RPL);
    }

    /* Check privileges for data segments and non-conforming code segments. */
    if (   (Desc.Legacy.Gen.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF))
        != (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF))
    {
        /* The RPL and the new CPL must be less than or equal to the DPL. */
        if (   (unsigned)(uSel & X86_SEL_RPL) > Desc.Legacy.Gen.u2Dpl
            || (IEM_GET_CPL(pVCpu) > Desc.Legacy.Gen.u2Dpl))
        {
            Log(("iemHlpTaskSwitchLoadDataSelectorInProtMode: Invalid priv. uSel=%u uSel.RPL=%u DPL=%u CPL=%u\n",
                 uSel, (uSel & X86_SEL_RPL), Desc.Legacy.Gen.u2Dpl, IEM_GET_CPL(pVCpu)));
            return iemRaiseTaskSwitchFaultWithErr(pVCpu, uSel & X86_SEL_MASK_OFF_RPL);
        }
    }

    /* Is it there? */
    if (!Desc.Legacy.Gen.u1Present)
    {
        Log(("iemHlpTaskSwitchLoadDataSelectorInProtMode: Segment not present. uSel=%u\n", uSel));
        return iemRaiseSelectorNotPresentWithErr(pVCpu, uSel & X86_SEL_MASK_OFF_RPL);
    }

    /* The base and limit. */
    uint32_t cbLimit = X86DESC_LIMIT_G(&Desc.Legacy);
    uint64_t u64Base = X86DESC_BASE(&Desc.Legacy);

    /*
     * Ok, everything checked out fine. Now set the accessed bit before
     * committing the result into the registers.
     */
    if (!(Desc.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
    {
        rcStrict = iemMemMarkSelDescAccessed(pVCpu, uSel);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        Desc.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
    }

    /* Commit */
    pSReg->Sel      = uSel;
    pSReg->Attr.u   = X86DESC_GET_HID_ATTR(&Desc.Legacy);
    pSReg->u32Limit = cbLimit;
    pSReg->u64Base  = u64Base;  /** @todo testcase/investigate: seen claims that the upper half of the base remains unchanged... */
    pSReg->ValidSel = uSel;
    pSReg->fFlags   = CPUMSELREG_FLAGS_VALID;
    if (IEM_IS_GUEST_CPU_INTEL(pVCpu))
        pSReg->Attr.u &= ~X86DESCATTR_UNUSABLE;

    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, pSReg));
    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_HIDDEN_SEL_REGS);
    return VINF_SUCCESS;
}


/**
 * Performs a task switch.
 *
 * If the task switch is the result of a JMP, CALL or IRET instruction, the
 * caller is responsible for performing the necessary checks (like DPL, TSS
 * present etc.) which are specific to JMP/CALL/IRET. See Intel Instruction
 * reference for JMP, CALL, IRET.
 *
 * If the task switch is the due to a software interrupt or hardware exception,
 * the caller is responsible for validating the TSS selector and descriptor. See
 * Intel Instruction reference for INT n.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   enmTaskSwitch   The cause of the task switch.
 * @param   uNextEip        The EIP effective after the task switch.
 * @param   fFlags          The flags, see IEM_XCPT_FLAGS_XXX.
 * @param   uErr            The error value if IEM_XCPT_FLAGS_ERR is set.
 * @param   uCr2            The CR2 value if IEM_XCPT_FLAGS_CR2 is set.
 * @param   SelTss          The TSS selector of the new task.
 * @param   pNewDescTss     Pointer to the new TSS descriptor.
 */
VBOXSTRICTRC
iemTaskSwitch(PVMCPUCC        pVCpu,
              IEMTASKSWITCH   enmTaskSwitch,
              uint32_t        uNextEip,
              uint32_t        fFlags,
              uint16_t        uErr,
              uint64_t        uCr2,
              RTSEL           SelTss,
              PIEMSELDESC     pNewDescTss) RT_NOEXCEPT
{
    Assert(!IEM_IS_REAL_MODE(pVCpu));
    Assert(!IEM_IS_64BIT_CODE(pVCpu));
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_XCPT_MASK);

    uint32_t const uNewTssType = pNewDescTss->Legacy.Gate.u4Type;
    Assert(   uNewTssType == X86_SEL_TYPE_SYS_286_TSS_AVAIL
           || uNewTssType == X86_SEL_TYPE_SYS_286_TSS_BUSY
           || uNewTssType == X86_SEL_TYPE_SYS_386_TSS_AVAIL
           || uNewTssType == X86_SEL_TYPE_SYS_386_TSS_BUSY);

    bool const fIsNewTss386 = (   uNewTssType == X86_SEL_TYPE_SYS_386_TSS_AVAIL
                               || uNewTssType == X86_SEL_TYPE_SYS_386_TSS_BUSY);

    Log(("iemTaskSwitch: enmTaskSwitch=%u NewTss=%#x fIsNewTss386=%RTbool EIP=%#RX32 uNextEip=%#RX32\n", enmTaskSwitch, SelTss,
         fIsNewTss386, pVCpu->cpum.GstCtx.eip, uNextEip));

    /* Update CR2 in case it's a page-fault. */
    /** @todo This should probably be done much earlier in IEM/PGM. See
     *        @bugref{5653#c49}. */
    if (fFlags & IEM_XCPT_FLAGS_CR2)
        pVCpu->cpum.GstCtx.cr2 = uCr2;

    /*
     * Check the new TSS limit. See Intel spec. 6.15 "Exception and Interrupt Reference"
     * subsection "Interrupt 10 - Invalid TSS Exception (#TS)".
     */
    uint32_t const uNewTssLimit    = pNewDescTss->Legacy.Gen.u16LimitLow | (pNewDescTss->Legacy.Gen.u4LimitHigh << 16);
    uint32_t const uNewTssLimitMin = fIsNewTss386 ? X86_SEL_TYPE_SYS_386_TSS_LIMIT_MIN : X86_SEL_TYPE_SYS_286_TSS_LIMIT_MIN;
    if (uNewTssLimit < uNewTssLimitMin)
    {
        Log(("iemTaskSwitch: Invalid new TSS limit. enmTaskSwitch=%u uNewTssLimit=%#x uNewTssLimitMin=%#x -> #TS\n",
             enmTaskSwitch, uNewTssLimit, uNewTssLimitMin));
        return iemRaiseTaskSwitchFaultWithErr(pVCpu, SelTss & X86_SEL_MASK_OFF_RPL);
    }

    /*
     * Task switches in VMX non-root mode always cause task switches.
     * The new TSS must have been read and validated (DPL, limits etc.) before a
     * task-switch VM-exit commences.
     *
     * See Intel spec. 25.4.2 "Treatment of Task Switches".
     */
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        Log(("iemTaskSwitch: Guest intercept (source=%u, sel=%#x) -> VM-exit.\n", enmTaskSwitch, SelTss));
        IEM_VMX_VMEXIT_TASK_SWITCH_RET(pVCpu, enmTaskSwitch, SelTss, uNextEip - pVCpu->cpum.GstCtx.eip);
    }

    /*
     * The SVM nested-guest intercept for task-switch takes priority over all exceptions
     * after validating the incoming (new) TSS, see AMD spec. 15.14.1 "Task Switch Intercept".
     */
    if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_TASK_SWITCH))
    {
        uint64_t const uExitInfo1 = SelTss;
        uint64_t       uExitInfo2 = uErr;
        switch (enmTaskSwitch)
        {
            case IEMTASKSWITCH_JUMP: uExitInfo2 |= SVM_EXIT2_TASK_SWITCH_JUMP; break;
            case IEMTASKSWITCH_IRET: uExitInfo2 |= SVM_EXIT2_TASK_SWITCH_IRET; break;
            default: break;
        }
        if (fFlags & IEM_XCPT_FLAGS_ERR)
            uExitInfo2 |= SVM_EXIT2_TASK_SWITCH_HAS_ERROR_CODE;
        if (pVCpu->cpum.GstCtx.eflags.Bits.u1RF)
            uExitInfo2 |= SVM_EXIT2_TASK_SWITCH_EFLAGS_RF;

        Log(("iemTaskSwitch: Guest intercept -> #VMEXIT. uExitInfo1=%#RX64 uExitInfo2=%#RX64\n", uExitInfo1, uExitInfo2));
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_TASK_SWITCH, uExitInfo1, uExitInfo2);
        RT_NOREF2(uExitInfo1, uExitInfo2);
    }

    /*
     * Check the current TSS limit. The last written byte to the current TSS during the
     * task switch will be 2 bytes at offset 0x5C (32-bit) and 1 byte at offset 0x28 (16-bit).
     * See Intel spec. 7.2.1 "Task-State Segment (TSS)" for static and dynamic fields.
     *
     * The AMD docs doesn't mention anything about limit checks with LTR which suggests you can
     * end up with smaller than "legal" TSS limits.
     */
    uint32_t const uCurTssLimit    = pVCpu->cpum.GstCtx.tr.u32Limit;
    uint32_t const uCurTssLimitMin = fIsNewTss386 ? 0x5F : 0x29;
    if (uCurTssLimit < uCurTssLimitMin)
    {
        Log(("iemTaskSwitch: Invalid current TSS limit. enmTaskSwitch=%u uCurTssLimit=%#x uCurTssLimitMin=%#x -> #TS\n",
             enmTaskSwitch, uCurTssLimit, uCurTssLimitMin));
        return iemRaiseTaskSwitchFaultWithErr(pVCpu, SelTss & X86_SEL_MASK_OFF_RPL);
    }

    /*
     * Verify that the new TSS can be accessed and map it. Map only the required contents
     * and not the entire TSS.
     */
    uint8_t         bUnmapInfoNewTss;
    void           *pvNewTss;
    uint32_t  const cbNewTss    = uNewTssLimitMin + 1;
    RTGCPTR   const GCPtrNewTss = X86DESC_BASE(&pNewDescTss->Legacy);
    AssertCompile(sizeof(X86TSS32) == X86_SEL_TYPE_SYS_386_TSS_LIMIT_MIN + 1);
    /** @todo Handle if the TSS crosses a page boundary. Intel specifies that it may
     *        not perform correct translation if this happens. See Intel spec. 7.2.1
     *        "Task-State Segment". */
    VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvNewTss, &bUnmapInfoNewTss, cbNewTss, UINT8_MAX, GCPtrNewTss, IEM_ACCESS_SYS_RW, 0);
/** @todo Not cleaning up bUnmapInfoNewTss mapping in any early exits here.
 * Consider wrapping the remainder into a function for simpler cleanup. */
    if (rcStrict != VINF_SUCCESS)
    {
        Log(("iemTaskSwitch: Failed to read new TSS. enmTaskSwitch=%u cbNewTss=%u uNewTssLimit=%u rc=%Rrc\n", enmTaskSwitch,
             cbNewTss, uNewTssLimit, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }

    /*
     * Clear the busy bit in current task's TSS descriptor if it's a task switch due to JMP/IRET.
     */
    uint32_t fEFlags = pVCpu->cpum.GstCtx.eflags.u;
    if (   enmTaskSwitch == IEMTASKSWITCH_JUMP
        || enmTaskSwitch == IEMTASKSWITCH_IRET)
    {
        uint8_t  bUnmapInfoDescCurTss;
        PX86DESC pDescCurTss;
        rcStrict = iemMemMap(pVCpu, (void **)&pDescCurTss, &bUnmapInfoDescCurTss, sizeof(*pDescCurTss), UINT8_MAX,
                             pVCpu->cpum.GstCtx.gdtr.pGdt + (pVCpu->cpum.GstCtx.tr.Sel & X86_SEL_MASK), IEM_ACCESS_SYS_RW, 0);
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("iemTaskSwitch: Failed to read new TSS descriptor in GDT. enmTaskSwitch=%u pGdt=%#RX64 rc=%Rrc\n",
                 enmTaskSwitch, pVCpu->cpum.GstCtx.gdtr.pGdt, VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }

        pDescCurTss->Gate.u4Type &= ~X86_SEL_TYPE_SYS_TSS_BUSY_MASK;
        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfoDescCurTss);
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("iemTaskSwitch: Failed to commit new TSS descriptor in GDT. enmTaskSwitch=%u pGdt=%#RX64 rc=%Rrc\n",
                 enmTaskSwitch, pVCpu->cpum.GstCtx.gdtr.pGdt, VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }

        /* Clear EFLAGS.NT (Nested Task) in the eflags memory image, if it's a task switch due to an IRET. */
        if (enmTaskSwitch == IEMTASKSWITCH_IRET)
        {
            Assert(   uNewTssType == X86_SEL_TYPE_SYS_286_TSS_BUSY
                   || uNewTssType == X86_SEL_TYPE_SYS_386_TSS_BUSY);
            fEFlags &= ~X86_EFL_NT;
        }
    }

    /*
     * Save the CPU state into the current TSS.
     */
    RTGCPTR const GCPtrCurTss = pVCpu->cpum.GstCtx.tr.u64Base;
    if (GCPtrNewTss == GCPtrCurTss)
    {
        Log(("iemTaskSwitch: Switching to the same TSS! enmTaskSwitch=%u GCPtr[Cur|New]TSS=%#RGv\n", enmTaskSwitch, GCPtrCurTss));
        Log(("uCurCr3=%#x uCurEip=%#x uCurEflags=%#x uCurEax=%#x uCurEsp=%#x uCurEbp=%#x uCurCS=%#04x uCurSS=%#04x uCurLdt=%#x\n",
             pVCpu->cpum.GstCtx.cr3, pVCpu->cpum.GstCtx.eip, pVCpu->cpum.GstCtx.eflags.u, pVCpu->cpum.GstCtx.eax,
             pVCpu->cpum.GstCtx.esp, pVCpu->cpum.GstCtx.ebp, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.ss.Sel,
             pVCpu->cpum.GstCtx.ldtr.Sel));
    }
    if (fIsNewTss386)
    {
        /*
         * Verify that the current TSS (32-bit) can be accessed, only the minimum required size.
         * See Intel spec. 7.2.1 "Task-State Segment (TSS)" for static and dynamic fields.
         */
        uint8_t        bUnmapInfoCurTss32;
        void          *pvCurTss32;
        uint32_t const offCurTss = RT_UOFFSETOF(X86TSS32, eip);
        uint32_t const cbCurTss  = RT_UOFFSETOF(X86TSS32, selLdt) - RT_UOFFSETOF(X86TSS32, eip);
        AssertCompile(RTASSERT_OFFSET_OF(X86TSS32, selLdt) - RTASSERT_OFFSET_OF(X86TSS32, eip) == 64);
        rcStrict = iemMemMap(pVCpu, &pvCurTss32, &bUnmapInfoCurTss32, cbCurTss, UINT8_MAX,
                             GCPtrCurTss + offCurTss, IEM_ACCESS_SYS_RW, 0);
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("iemTaskSwitch: Failed to read current 32-bit TSS. enmTaskSwitch=%u GCPtrCurTss=%#RGv cb=%u rc=%Rrc\n",
                 enmTaskSwitch, GCPtrCurTss, cbCurTss, VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }

        /* !! WARNING !! Access -only- the members (dynamic fields) that are mapped, i.e interval [offCurTss..cbCurTss). */
        PX86TSS32 pCurTss32 = (PX86TSS32)((uintptr_t)pvCurTss32 - offCurTss);
        pCurTss32->eip    = uNextEip;
        pCurTss32->eflags = fEFlags;
        pCurTss32->eax    = pVCpu->cpum.GstCtx.eax;
        pCurTss32->ecx    = pVCpu->cpum.GstCtx.ecx;
        pCurTss32->edx    = pVCpu->cpum.GstCtx.edx;
        pCurTss32->ebx    = pVCpu->cpum.GstCtx.ebx;
        pCurTss32->esp    = pVCpu->cpum.GstCtx.esp;
        pCurTss32->ebp    = pVCpu->cpum.GstCtx.ebp;
        pCurTss32->esi    = pVCpu->cpum.GstCtx.esi;
        pCurTss32->edi    = pVCpu->cpum.GstCtx.edi;
        pCurTss32->es     = pVCpu->cpum.GstCtx.es.Sel;
        pCurTss32->cs     = pVCpu->cpum.GstCtx.cs.Sel;
        pCurTss32->ss     = pVCpu->cpum.GstCtx.ss.Sel;
        pCurTss32->ds     = pVCpu->cpum.GstCtx.ds.Sel;
        pCurTss32->fs     = pVCpu->cpum.GstCtx.fs.Sel;
        pCurTss32->gs     = pVCpu->cpum.GstCtx.gs.Sel;

        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfoCurTss32);
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("iemTaskSwitch: Failed to commit current 32-bit TSS. enmTaskSwitch=%u rc=%Rrc\n", enmTaskSwitch,
                 VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
    }
    else
    {
        /*
         * Verify that the current TSS (16-bit) can be accessed. Again, only the minimum required size.
         */
        uint8_t        bUnmapInfoCurTss16;
        void          *pvCurTss16;
        uint32_t const offCurTss = RT_UOFFSETOF(X86TSS16, ip);
        uint32_t const cbCurTss  = RT_UOFFSETOF(X86TSS16, selLdt) - RT_UOFFSETOF(X86TSS16, ip);
        AssertCompile(RTASSERT_OFFSET_OF(X86TSS16, selLdt) - RTASSERT_OFFSET_OF(X86TSS16, ip) == 28);
        rcStrict = iemMemMap(pVCpu, &pvCurTss16, &bUnmapInfoCurTss16, cbCurTss, UINT8_MAX,
                             GCPtrCurTss + offCurTss, IEM_ACCESS_SYS_RW, 0);
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("iemTaskSwitch: Failed to read current 16-bit TSS. enmTaskSwitch=%u GCPtrCurTss=%#RGv cb=%u rc=%Rrc\n",
                 enmTaskSwitch, GCPtrCurTss, cbCurTss, VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }

        /* !! WARNING !! Access -only- the members (dynamic fields) that are mapped, i.e interval [offCurTss..cbCurTss). */
        PX86TSS16 pCurTss16 = (PX86TSS16)((uintptr_t)pvCurTss16 - offCurTss);
        pCurTss16->ip    = uNextEip;
        pCurTss16->flags = (uint16_t)fEFlags;
        pCurTss16->ax    = pVCpu->cpum.GstCtx.ax;
        pCurTss16->cx    = pVCpu->cpum.GstCtx.cx;
        pCurTss16->dx    = pVCpu->cpum.GstCtx.dx;
        pCurTss16->bx    = pVCpu->cpum.GstCtx.bx;
        pCurTss16->sp    = pVCpu->cpum.GstCtx.sp;
        pCurTss16->bp    = pVCpu->cpum.GstCtx.bp;
        pCurTss16->si    = pVCpu->cpum.GstCtx.si;
        pCurTss16->di    = pVCpu->cpum.GstCtx.di;
        pCurTss16->es    = pVCpu->cpum.GstCtx.es.Sel;
        pCurTss16->cs    = pVCpu->cpum.GstCtx.cs.Sel;
        pCurTss16->ss    = pVCpu->cpum.GstCtx.ss.Sel;
        pCurTss16->ds    = pVCpu->cpum.GstCtx.ds.Sel;

        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfoCurTss16);
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("iemTaskSwitch: Failed to commit current 16-bit TSS. enmTaskSwitch=%u rc=%Rrc\n", enmTaskSwitch,
                 VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
    }

    /*
     * Update the previous task link field for the new TSS, if the task switch is due to a CALL/INT_XCPT.
     */
    if (   enmTaskSwitch == IEMTASKSWITCH_CALL
        || enmTaskSwitch == IEMTASKSWITCH_INT_XCPT)
    {
        /* 16 or 32-bit TSS doesn't matter, we only access the first, common 16-bit field (selPrev) here. */
        PX86TSS32 pNewTSS = (PX86TSS32)pvNewTss;
        pNewTSS->selPrev  = pVCpu->cpum.GstCtx.tr.Sel;
    }

    /*
     * Read the state from the new TSS into temporaries. Setting it immediately as the new CPU state is tricky,
     * it's done further below with error handling (e.g. CR3 changes will go through PGM).
     */
    uint32_t uNewCr3, uNewEip, uNewEflags, uNewEax, uNewEcx, uNewEdx, uNewEbx, uNewEsp, uNewEbp, uNewEsi, uNewEdi;
    uint16_t uNewES,  uNewCS, uNewSS, uNewDS, uNewFS, uNewGS, uNewLdt;
    bool     fNewDebugTrap;
    if (fIsNewTss386)
    {
        PCX86TSS32 pNewTss32 = (PCX86TSS32)pvNewTss;
        uNewCr3       = (pVCpu->cpum.GstCtx.cr0 & X86_CR0_PG) ? pNewTss32->cr3 : 0;
        uNewEip       = pNewTss32->eip;
        uNewEflags    = pNewTss32->eflags;
        uNewEax       = pNewTss32->eax;
        uNewEcx       = pNewTss32->ecx;
        uNewEdx       = pNewTss32->edx;
        uNewEbx       = pNewTss32->ebx;
        uNewEsp       = pNewTss32->esp;
        uNewEbp       = pNewTss32->ebp;
        uNewEsi       = pNewTss32->esi;
        uNewEdi       = pNewTss32->edi;
        uNewES        = pNewTss32->es;
        uNewCS        = pNewTss32->cs;
        uNewSS        = pNewTss32->ss;
        uNewDS        = pNewTss32->ds;
        uNewFS        = pNewTss32->fs;
        uNewGS        = pNewTss32->gs;
        uNewLdt       = pNewTss32->selLdt;
        fNewDebugTrap = RT_BOOL(pNewTss32->fDebugTrap);
    }
    else
    {
        PCX86TSS16 pNewTss16 = (PCX86TSS16)pvNewTss;
        uNewCr3       = 0;
        uNewEip       = pNewTss16->ip;
        uNewEflags    = pNewTss16->flags;
        uNewEax       = UINT32_C(0xffff0000) | pNewTss16->ax;
        uNewEcx       = UINT32_C(0xffff0000) | pNewTss16->cx;
        uNewEdx       = UINT32_C(0xffff0000) | pNewTss16->dx;
        uNewEbx       = UINT32_C(0xffff0000) | pNewTss16->bx;
        uNewEsp       = UINT32_C(0xffff0000) | pNewTss16->sp;
        uNewEbp       = UINT32_C(0xffff0000) | pNewTss16->bp;
        uNewEsi       = UINT32_C(0xffff0000) | pNewTss16->si;
        uNewEdi       = UINT32_C(0xffff0000) | pNewTss16->di;
        uNewES        = pNewTss16->es;
        uNewCS        = pNewTss16->cs;
        uNewSS        = pNewTss16->ss;
        uNewDS        = pNewTss16->ds;
        uNewFS        = 0;
        uNewGS        = 0;
        uNewLdt       = pNewTss16->selLdt;
        fNewDebugTrap = false;
    }

    if (GCPtrNewTss == GCPtrCurTss)
        Log(("uNewCr3=%#x uNewEip=%#x uNewEflags=%#x uNewEax=%#x uNewEsp=%#x uNewEbp=%#x uNewCS=%#04x uNewSS=%#04x uNewLdt=%#x\n",
             uNewCr3, uNewEip, uNewEflags, uNewEax, uNewEsp, uNewEbp, uNewCS, uNewSS, uNewLdt));

    /*
     * We're done accessing the new TSS.
     */
    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfoNewTss);
    if (rcStrict != VINF_SUCCESS)
    {
        Log(("iemTaskSwitch: Failed to commit new TSS. enmTaskSwitch=%u rc=%Rrc\n", enmTaskSwitch, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }

    /*
     * Set the busy bit in the new TSS descriptor, if the task switch is a JMP/CALL/INT_XCPT.
     */
    if (enmTaskSwitch != IEMTASKSWITCH_IRET)
    {
        rcStrict = iemMemMap(pVCpu, (void **)&pNewDescTss, &bUnmapInfoNewTss, sizeof(*pNewDescTss), UINT8_MAX,
                             pVCpu->cpum.GstCtx.gdtr.pGdt + (SelTss & X86_SEL_MASK), IEM_ACCESS_SYS_RW, 0);
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("iemTaskSwitch: Failed to read new TSS descriptor in GDT (2). enmTaskSwitch=%u pGdt=%#RX64 rc=%Rrc\n",
                 enmTaskSwitch, pVCpu->cpum.GstCtx.gdtr.pGdt, VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }

        /* Check that the descriptor indicates the new TSS is available (not busy). */
        AssertMsg(   pNewDescTss->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_286_TSS_AVAIL
                  || pNewDescTss->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_386_TSS_AVAIL,
                  ("Invalid TSS descriptor type=%#x", pNewDescTss->Legacy.Gate.u4Type));

        pNewDescTss->Legacy.Gate.u4Type |= X86_SEL_TYPE_SYS_TSS_BUSY_MASK;
        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfoNewTss);
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("iemTaskSwitch: Failed to commit new TSS descriptor in GDT (2). enmTaskSwitch=%u pGdt=%#RX64 rc=%Rrc\n",
                 enmTaskSwitch, pVCpu->cpum.GstCtx.gdtr.pGdt, VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
    }

    /*
     * From this point on, we're technically in the new task. We will defer exceptions
     * until the completion of the task switch but before executing any instructions in the new task.
     */
    pVCpu->cpum.GstCtx.tr.Sel      = SelTss;
    pVCpu->cpum.GstCtx.tr.ValidSel = SelTss;
    pVCpu->cpum.GstCtx.tr.fFlags   = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.tr.Attr.u   = X86DESC_GET_HID_ATTR(&pNewDescTss->Legacy);
    pVCpu->cpum.GstCtx.tr.u32Limit = X86DESC_LIMIT_G(&pNewDescTss->Legacy);
    pVCpu->cpum.GstCtx.tr.u64Base  = X86DESC_BASE(&pNewDescTss->Legacy);
    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_TR);

    /* Set the busy bit in TR. */
    pVCpu->cpum.GstCtx.tr.Attr.n.u4Type |= X86_SEL_TYPE_SYS_TSS_BUSY_MASK;

    /* Set EFLAGS.NT (Nested Task) in the eflags loaded from the new TSS, if it's a task switch due to a CALL/INT_XCPT. */
    if (   enmTaskSwitch == IEMTASKSWITCH_CALL
        || enmTaskSwitch == IEMTASKSWITCH_INT_XCPT)
    {
        uNewEflags |= X86_EFL_NT;
    }

    pVCpu->cpum.GstCtx.dr[7] &= ~X86_DR7_LE_ALL;     /** @todo Should we clear DR7.LE bit too? */
    pVCpu->cpum.GstCtx.cr0   |= X86_CR0_TS;
    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_CR0);

    pVCpu->cpum.GstCtx.eip    = uNewEip;
    pVCpu->cpum.GstCtx.eax    = uNewEax;
    pVCpu->cpum.GstCtx.ecx    = uNewEcx;
    pVCpu->cpum.GstCtx.edx    = uNewEdx;
    pVCpu->cpum.GstCtx.ebx    = uNewEbx;
    pVCpu->cpum.GstCtx.esp    = uNewEsp;
    pVCpu->cpum.GstCtx.ebp    = uNewEbp;
    pVCpu->cpum.GstCtx.esi    = uNewEsi;
    pVCpu->cpum.GstCtx.edi    = uNewEdi;

    uNewEflags &= X86_EFL_LIVE_MASK;
    uNewEflags |= X86_EFL_RA1_MASK;
    IEMMISC_SET_EFL(pVCpu, uNewEflags);

    /*
     * Switch the selectors here and do the segment checks later. If we throw exceptions, the selectors
     * will be valid in the exception handler. We cannot update the hidden parts until we've switched CR3
     * due to the hidden part data originating from the guest LDT/GDT which is accessed through paging.
     */
    pVCpu->cpum.GstCtx.es.Sel       = uNewES;
    pVCpu->cpum.GstCtx.es.Attr.u   &= ~X86DESCATTR_P;

    pVCpu->cpum.GstCtx.cs.Sel       = uNewCS;
    pVCpu->cpum.GstCtx.cs.Attr.u   &= ~X86DESCATTR_P;

    pVCpu->cpum.GstCtx.ss.Sel       = uNewSS;
    pVCpu->cpum.GstCtx.ss.Attr.u   &= ~X86DESCATTR_P;

    pVCpu->cpum.GstCtx.ds.Sel       = uNewDS;
    pVCpu->cpum.GstCtx.ds.Attr.u   &= ~X86DESCATTR_P;

    pVCpu->cpum.GstCtx.fs.Sel       = uNewFS;
    pVCpu->cpum.GstCtx.fs.Attr.u   &= ~X86DESCATTR_P;

    pVCpu->cpum.GstCtx.gs.Sel       = uNewGS;
    pVCpu->cpum.GstCtx.gs.Attr.u   &= ~X86DESCATTR_P;
    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_HIDDEN_SEL_REGS);

    pVCpu->cpum.GstCtx.ldtr.Sel     = uNewLdt;
    pVCpu->cpum.GstCtx.ldtr.fFlags  = CPUMSELREG_FLAGS_STALE;
    pVCpu->cpum.GstCtx.ldtr.Attr.u &= ~X86DESCATTR_P;
    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_LDTR);

    if (IEM_IS_GUEST_CPU_INTEL(pVCpu))
    {
        pVCpu->cpum.GstCtx.es.Attr.u   |= X86DESCATTR_UNUSABLE;
        pVCpu->cpum.GstCtx.cs.Attr.u   |= X86DESCATTR_UNUSABLE;
        pVCpu->cpum.GstCtx.ss.Attr.u   |= X86DESCATTR_UNUSABLE;
        pVCpu->cpum.GstCtx.ds.Attr.u   |= X86DESCATTR_UNUSABLE;
        pVCpu->cpum.GstCtx.fs.Attr.u   |= X86DESCATTR_UNUSABLE;
        pVCpu->cpum.GstCtx.gs.Attr.u   |= X86DESCATTR_UNUSABLE;
        pVCpu->cpum.GstCtx.ldtr.Attr.u |= X86DESCATTR_UNUSABLE;
    }

    /*
     * Switch CR3 for the new task.
     */
    if (   fIsNewTss386
        && (pVCpu->cpum.GstCtx.cr0 & X86_CR0_PG))
    {
        /** @todo Should we update and flush TLBs only if CR3 value actually changes? */
        int rc = CPUMSetGuestCR3(pVCpu, uNewCr3);
        AssertRCSuccessReturn(rc, rc);

        /* Inform PGM. */
        /** @todo Should we raise \#GP(0) here when PAE PDPEs are invalid? */
        rc = PGMFlushTLB(pVCpu, pVCpu->cpum.GstCtx.cr3, !(pVCpu->cpum.GstCtx.cr4 & X86_CR4_PGE));
        AssertRCReturn(rc, rc);
        /* ignore informational status codes */

        CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_CR3);
    }

    /*
     * Switch LDTR for the new task.
     */
    if (!(uNewLdt & X86_SEL_MASK_OFF_RPL))
        iemHlpLoadNullDataSelectorProt(pVCpu, &pVCpu->cpum.GstCtx.ldtr, uNewLdt);
    else
    {
        Assert(!pVCpu->cpum.GstCtx.ldtr.Attr.n.u1Present);   /* Ensures that LDT.TI check passes in iemMemFetchSelDesc() below. */

        IEMSELDESC DescNewLdt;
        rcStrict = iemMemFetchSelDesc(pVCpu, &DescNewLdt, uNewLdt, X86_XCPT_TS);
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("iemTaskSwitch: fetching LDT failed. enmTaskSwitch=%u uNewLdt=%u cbGdt=%u rc=%Rrc\n", enmTaskSwitch,
                 uNewLdt, pVCpu->cpum.GstCtx.gdtr.cbGdt, VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
        if (   !DescNewLdt.Legacy.Gen.u1Present
            ||  DescNewLdt.Legacy.Gen.u1DescType
            ||  DescNewLdt.Legacy.Gen.u4Type != X86_SEL_TYPE_SYS_LDT)
        {
            Log(("iemTaskSwitch: Invalid LDT. enmTaskSwitch=%u uNewLdt=%u DescNewLdt.Legacy.u=%#RX64 -> #TS\n", enmTaskSwitch,
                 uNewLdt, DescNewLdt.Legacy.u));
            return iemRaiseTaskSwitchFaultWithErr(pVCpu, uNewLdt & X86_SEL_MASK_OFF_RPL);
        }

        pVCpu->cpum.GstCtx.ldtr.ValidSel = uNewLdt;
        pVCpu->cpum.GstCtx.ldtr.fFlags   = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.ldtr.u64Base  = X86DESC_BASE(&DescNewLdt.Legacy);
        pVCpu->cpum.GstCtx.ldtr.u32Limit = X86DESC_LIMIT_G(&DescNewLdt.Legacy);
        pVCpu->cpum.GstCtx.ldtr.Attr.u   = X86DESC_GET_HID_ATTR(&DescNewLdt.Legacy);
        if (IEM_IS_GUEST_CPU_INTEL(pVCpu))
            pVCpu->cpum.GstCtx.ldtr.Attr.u &= ~X86DESCATTR_UNUSABLE;
        Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.ldtr));
    }

    IEMSELDESC DescSS;
    if (IEM_IS_V86_MODE(pVCpu))
    {
        IEM_SET_CPL(pVCpu, 3);
        iemHlpLoadSelectorInV86Mode(&pVCpu->cpum.GstCtx.es, uNewES);
        iemHlpLoadSelectorInV86Mode(&pVCpu->cpum.GstCtx.cs, uNewCS);
        iemHlpLoadSelectorInV86Mode(&pVCpu->cpum.GstCtx.ss, uNewSS);
        iemHlpLoadSelectorInV86Mode(&pVCpu->cpum.GstCtx.ds, uNewDS);
        iemHlpLoadSelectorInV86Mode(&pVCpu->cpum.GstCtx.fs, uNewFS);
        iemHlpLoadSelectorInV86Mode(&pVCpu->cpum.GstCtx.gs, uNewGS);

        /* Quick fix: fake DescSS. */ /** @todo fix the code further down? */
        DescSS.Legacy.u = 0;
        DescSS.Legacy.Gen.u16LimitLow = (uint16_t)pVCpu->cpum.GstCtx.ss.u32Limit;
        DescSS.Legacy.Gen.u4LimitHigh = pVCpu->cpum.GstCtx.ss.u32Limit >> 16;
        DescSS.Legacy.Gen.u16BaseLow  = (uint16_t)pVCpu->cpum.GstCtx.ss.u64Base;
        DescSS.Legacy.Gen.u8BaseHigh1 = (uint8_t)(pVCpu->cpum.GstCtx.ss.u64Base >> 16);
        DescSS.Legacy.Gen.u8BaseHigh2 = (uint8_t)(pVCpu->cpum.GstCtx.ss.u64Base >> 24);
        DescSS.Legacy.Gen.u4Type      = X86_SEL_TYPE_RW_ACC;
        DescSS.Legacy.Gen.u2Dpl       = 3;
    }
    else
    {
        uint8_t const uNewCpl = (uNewCS & X86_SEL_RPL);

        /*
         * Load the stack segment for the new task.
         */
        if (!(uNewSS & X86_SEL_MASK_OFF_RPL))
        {
            Log(("iemTaskSwitch: Null stack segment. enmTaskSwitch=%u uNewSS=%#x -> #TS\n", enmTaskSwitch, uNewSS));
            return iemRaiseTaskSwitchFaultWithErr(pVCpu, uNewSS & X86_SEL_MASK_OFF_RPL);
        }

        /* Fetch the descriptor. */
        rcStrict = iemMemFetchSelDesc(pVCpu, &DescSS, uNewSS, X86_XCPT_TS);
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("iemTaskSwitch: failed to fetch SS. uNewSS=%#x rc=%Rrc\n", uNewSS,
                 VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }

        /* SS must be a data segment and writable. */
        if (    !DescSS.Legacy.Gen.u1DescType
            ||  (DescSS.Legacy.Gen.u4Type & X86_SEL_TYPE_CODE)
            || !(DescSS.Legacy.Gen.u4Type & X86_SEL_TYPE_WRITE))
        {
            Log(("iemTaskSwitch: SS invalid descriptor type. uNewSS=%#x u1DescType=%u u4Type=%#x\n",
                 uNewSS, DescSS.Legacy.Gen.u1DescType, DescSS.Legacy.Gen.u4Type));
            return iemRaiseTaskSwitchFaultWithErr(pVCpu, uNewSS & X86_SEL_MASK_OFF_RPL);
        }

        /* The SS.RPL, SS.DPL, CS.RPL (CPL) must be equal. */
        if (   (uNewSS & X86_SEL_RPL) != uNewCpl
            || DescSS.Legacy.Gen.u2Dpl != uNewCpl)
        {
            Log(("iemTaskSwitch: Invalid priv. for SS. uNewSS=%#x SS.DPL=%u uNewCpl=%u -> #TS\n", uNewSS, DescSS.Legacy.Gen.u2Dpl,
                 uNewCpl));
            return iemRaiseTaskSwitchFaultWithErr(pVCpu, uNewSS & X86_SEL_MASK_OFF_RPL);
        }

        /* Is it there? */
        if (!DescSS.Legacy.Gen.u1Present)
        {
            Log(("iemTaskSwitch: SS not present. uNewSS=%#x -> #NP\n", uNewSS));
            return iemRaiseSelectorNotPresentWithErr(pVCpu, uNewSS & X86_SEL_MASK_OFF_RPL);
        }

        uint32_t cbLimit = X86DESC_LIMIT_G(&DescSS.Legacy);
        uint64_t u64Base = X86DESC_BASE(&DescSS.Legacy);

        /* Set the accessed bit before committing the result into SS. */
        if (!(DescSS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
        {
            rcStrict = iemMemMarkSelDescAccessed(pVCpu, uNewSS);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            DescSS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
        }

        /* Commit SS. */
        pVCpu->cpum.GstCtx.ss.Sel      = uNewSS;
        pVCpu->cpum.GstCtx.ss.ValidSel = uNewSS;
        pVCpu->cpum.GstCtx.ss.Attr.u   = X86DESC_GET_HID_ATTR(&DescSS.Legacy);
        pVCpu->cpum.GstCtx.ss.u32Limit = cbLimit;
        pVCpu->cpum.GstCtx.ss.u64Base  = u64Base;
        pVCpu->cpum.GstCtx.ss.fFlags   = CPUMSELREG_FLAGS_VALID;
        Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.ss));

        /* CPL has changed, update IEM before loading rest of segments. */
        IEM_SET_CPL(pVCpu, uNewCpl);

        /*
         * Load the data segments for the new task.
         */
        rcStrict = iemHlpTaskSwitchLoadDataSelectorInProtMode(pVCpu, &pVCpu->cpum.GstCtx.es, uNewES);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        rcStrict = iemHlpTaskSwitchLoadDataSelectorInProtMode(pVCpu, &pVCpu->cpum.GstCtx.ds, uNewDS);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        rcStrict = iemHlpTaskSwitchLoadDataSelectorInProtMode(pVCpu, &pVCpu->cpum.GstCtx.fs, uNewFS);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        rcStrict = iemHlpTaskSwitchLoadDataSelectorInProtMode(pVCpu, &pVCpu->cpum.GstCtx.gs, uNewGS);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        /*
         * Load the code segment for the new task.
         */
        if (!(uNewCS & X86_SEL_MASK_OFF_RPL))
        {
            Log(("iemTaskSwitch #TS: Null code segment. enmTaskSwitch=%u uNewCS=%#x\n", enmTaskSwitch, uNewCS));
            return iemRaiseTaskSwitchFaultWithErr(pVCpu, uNewCS & X86_SEL_MASK_OFF_RPL);
        }

        /* Fetch the descriptor. */
        IEMSELDESC DescCS;
        rcStrict = iemMemFetchSelDesc(pVCpu, &DescCS, uNewCS, X86_XCPT_TS);
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("iemTaskSwitch: failed to fetch CS. uNewCS=%u rc=%Rrc\n", uNewCS, VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }

        /* CS must be a code segment. */
        if (   !DescCS.Legacy.Gen.u1DescType
            || !(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_CODE))
        {
            Log(("iemTaskSwitch: CS invalid descriptor type. uNewCS=%#x u1DescType=%u u4Type=%#x -> #TS\n", uNewCS,
                 DescCS.Legacy.Gen.u1DescType, DescCS.Legacy.Gen.u4Type));
            return iemRaiseTaskSwitchFaultWithErr(pVCpu, uNewCS & X86_SEL_MASK_OFF_RPL);
        }

        /* For conforming CS, DPL must be less than or equal to the RPL. */
        if (   (DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_CONF)
            && DescCS.Legacy.Gen.u2Dpl > (uNewCS & X86_SEL_RPL))
        {
            Log(("iemTaskSwitch: confirming CS DPL > RPL. uNewCS=%#x u4Type=%#x DPL=%u -> #TS\n", uNewCS, DescCS.Legacy.Gen.u4Type,
                 DescCS.Legacy.Gen.u2Dpl));
            return iemRaiseTaskSwitchFaultWithErr(pVCpu, uNewCS & X86_SEL_MASK_OFF_RPL);
        }

        /* For non-conforming CS, DPL must match RPL. */
        if (   !(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_CONF)
            && DescCS.Legacy.Gen.u2Dpl != (uNewCS & X86_SEL_RPL))
        {
            Log(("iemTaskSwitch: non-confirming CS DPL RPL mismatch. uNewCS=%#x u4Type=%#x DPL=%u -> #TS\n", uNewCS,
                 DescCS.Legacy.Gen.u4Type, DescCS.Legacy.Gen.u2Dpl));
            return iemRaiseTaskSwitchFaultWithErr(pVCpu, uNewCS & X86_SEL_MASK_OFF_RPL);
        }

        /* Is it there? */
        if (!DescCS.Legacy.Gen.u1Present)
        {
            Log(("iemTaskSwitch: CS not present. uNewCS=%#x -> #NP\n", uNewCS));
            return iemRaiseSelectorNotPresentWithErr(pVCpu, uNewCS & X86_SEL_MASK_OFF_RPL);
        }

        cbLimit = X86DESC_LIMIT_G(&DescCS.Legacy);
        u64Base = X86DESC_BASE(&DescCS.Legacy);

        /* Set the accessed bit before committing the result into CS. */
        if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
        {
            rcStrict = iemMemMarkSelDescAccessed(pVCpu, uNewCS);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            DescCS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
        }

        /* Commit CS. */
        pVCpu->cpum.GstCtx.cs.Sel      = uNewCS;
        pVCpu->cpum.GstCtx.cs.ValidSel = uNewCS;
        pVCpu->cpum.GstCtx.cs.Attr.u   = X86DESC_GET_HID_ATTR(&DescCS.Legacy);
        pVCpu->cpum.GstCtx.cs.u32Limit = cbLimit;
        pVCpu->cpum.GstCtx.cs.u64Base  = u64Base;
        pVCpu->cpum.GstCtx.cs.fFlags   = CPUMSELREG_FLAGS_VALID;
        Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.cs));
    }

    /* Make sure the CPU mode is correct. */
    uint32_t const fExecNew = iemCalcExecFlags(pVCpu) | (ICORE(pVCpu).fExec & IEM_F_USER_OPTS);
    if (fExecNew != ICORE(pVCpu).fExec)
        Log(("iemTaskSwitch: fExec %#x -> %#x (xor %#x)\n", ICORE(pVCpu).fExec, fExecNew, ICORE(pVCpu).fExec ^ fExecNew));
    ICORE(pVCpu).fExec = fExecNew;

    /** @todo Debug trap. */
    if (fIsNewTss386 && fNewDebugTrap)
        Log(("iemTaskSwitch: Debug Trap set in new TSS. Not implemented!\n"));

    /*
     * Construct the error code masks based on what caused this task switch.
     * See Intel Instruction reference for INT.
     */
    uint16_t uExt;
    if (   enmTaskSwitch == IEMTASKSWITCH_INT_XCPT
        && (   !(fFlags & IEM_XCPT_FLAGS_T_SOFT_INT)
            ||  (fFlags & IEM_XCPT_FLAGS_ICEBP_INSTR)))
        uExt = 1;
    else
        uExt = 0;

    /*
     * Push any error code on to the new stack.
     */
    if (fFlags & IEM_XCPT_FLAGS_ERR)
    {
        Assert(enmTaskSwitch == IEMTASKSWITCH_INT_XCPT);
        uint32_t      cbLimitSS    = X86DESC_LIMIT_G(&DescSS.Legacy);
        uint8_t const cbStackFrame = fIsNewTss386 ? 4 : 2;

        /* Check that there is sufficient space on the stack. */
        /** @todo Factor out segment limit checking for normal/expand down segments
         *        into a separate function. */
        if (!(DescSS.Legacy.Gen.u4Type & X86_SEL_TYPE_DOWN))
        {
            if (   pVCpu->cpum.GstCtx.esp - 1 > cbLimitSS
                || pVCpu->cpum.GstCtx.esp < cbStackFrame)
            {
                /** @todo Intel says \#SS(EXT) for INT/XCPT, I couldn't figure out AMD yet. */
                Log(("iemTaskSwitch: SS=%#x ESP=%#x cbStackFrame=%#x is out of bounds -> #SS\n",
                     pVCpu->cpum.GstCtx.ss.Sel, pVCpu->cpum.GstCtx.esp, cbStackFrame));
                return iemRaiseStackSelectorNotPresentWithErr(pVCpu, uExt);
            }
        }
        else
        {
            if (   pVCpu->cpum.GstCtx.esp - 1 > (DescSS.Legacy.Gen.u1DefBig ? UINT32_MAX : UINT32_C(0xffff))
                || pVCpu->cpum.GstCtx.esp - cbStackFrame < cbLimitSS + UINT32_C(1))
            {
                Log(("iemTaskSwitch: SS=%#x ESP=%#x cbStackFrame=%#x (expand down) is out of bounds -> #SS\n",
                     pVCpu->cpum.GstCtx.ss.Sel, pVCpu->cpum.GstCtx.esp, cbStackFrame));
                return iemRaiseStackSelectorNotPresentWithErr(pVCpu, uExt);
            }
        }


        if (fIsNewTss386)
            rcStrict = iemMemStackPushU32(pVCpu, uErr);
        else
            rcStrict = iemMemStackPushU16(pVCpu, uErr);
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("iemTaskSwitch: Can't push error code to new task's stack. %s-bit TSS. rc=%Rrc\n",
                 fIsNewTss386 ? "32" : "16", VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
    }

    /* Check the new EIP against the new CS limit. */
    if (pVCpu->cpum.GstCtx.eip > pVCpu->cpum.GstCtx.cs.u32Limit)
    {
        Log(("iemHlpTaskSwitchLoadDataSelectorInProtMode: New EIP exceeds CS limit. uNewEIP=%#RX32 CS limit=%u -> #GP(0)\n",
             pVCpu->cpum.GstCtx.eip, pVCpu->cpum.GstCtx.cs.u32Limit));
        /** @todo Intel says \#GP(EXT) for INT/XCPT, I couldn't figure out AMD yet. */
        return iemRaiseGeneralProtectionFault(pVCpu, uExt);
    }

    Log(("iemTaskSwitch: Success! New CS:EIP=%#04x:%#x SS=%#04x\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.eip,
         pVCpu->cpum.GstCtx.ss.Sel));
    return fFlags & IEM_XCPT_FLAGS_T_CPU_XCPT ? VINF_IEM_RAISED_XCPT : VINF_SUCCESS;
}


/**
 * Implements exceptions and interrupts for protected mode.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr         The number of bytes to offset rIP by in the return
 *                          address.
 * @param   u8Vector        The interrupt / exception vector number.
 * @param   fFlags          The flags.
 * @param   uErr            The error value if IEM_XCPT_FLAGS_ERR is set.
 * @param   uCr2            The CR2 value if IEM_XCPT_FLAGS_CR2 is set.
 */
static VBOXSTRICTRC
iemRaiseXcptOrIntInProtMode(PVMCPUCC    pVCpu,
                            uint8_t     cbInstr,
                            uint8_t     u8Vector,
                            uint32_t    fFlags,
                            uint16_t    uErr,
                            uint64_t    uCr2) RT_NOEXCEPT
{
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_XCPT_MASK);

    /*
     * Read the IDT entry.
     */
    if (pVCpu->cpum.GstCtx.idtr.cbIdt < UINT32_C(8) * u8Vector + 7)
    {
        Log(("RaiseXcptOrIntInProtMode: %#x is out of bounds (%#x)\n", u8Vector, pVCpu->cpum.GstCtx.idtr.cbIdt));
        return iemRaiseGeneralProtectionFault(pVCpu, X86_TRAP_ERR_IDT | ((uint16_t)u8Vector << X86_TRAP_ERR_SEL_SHIFT));
    }
    X86DESC Idte;
    VBOXSTRICTRC rcStrict = iemMemFetchSysU64(pVCpu, &Idte.u, UINT8_MAX,
                                              pVCpu->cpum.GstCtx.idtr.pIdt + UINT32_C(8) * u8Vector);
    if (RT_UNLIKELY(rcStrict != VINF_SUCCESS))
    {
        Log(("iemRaiseXcptOrIntInProtMode: failed to fetch IDT entry! vec=%#x rc=%Rrc\n", u8Vector, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }
    Log(("iemRaiseXcptOrIntInProtMode: vec=%#x P=%u DPL=%u DT=%u:%u A=%u %04x:%04x%04x - from %04x:%08RX64 efl=%#x depth=%d\n",
         u8Vector, Idte.Gate.u1Present, Idte.Gate.u2Dpl, Idte.Gate.u1DescType, Idte.Gate.u4Type,
         Idte.Gate.u5ParmCount, Idte.Gate.u16Sel, Idte.Gate.u16OffsetHigh, Idte.Gate.u16OffsetLow,
         pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.eflags.u, ICORE(pVCpu).cXcptRecursions));

    /*
     * Check the descriptor type, DPL and such.
     * ASSUMES this is done in the same order as described for call-gate calls.
     */
    if (Idte.Gate.u1DescType)
    {
        Log(("RaiseXcptOrIntInProtMode %#x - not system selector (%#x) -> #GP\n", u8Vector, Idte.Gate.u4Type));
        return iemRaiseGeneralProtectionFault(pVCpu, X86_TRAP_ERR_IDT | ((uint16_t)u8Vector << X86_TRAP_ERR_SEL_SHIFT));
    }
    bool     fTaskGate   = false;
    uint8_t  f32BitGate  = true;
    uint32_t fEflToClear = X86_EFL_TF | X86_EFL_NT | X86_EFL_RF | X86_EFL_VM;
    switch (Idte.Gate.u4Type)
    {
        case X86_SEL_TYPE_SYS_UNDEFINED:
        case X86_SEL_TYPE_SYS_286_TSS_AVAIL:
        case X86_SEL_TYPE_SYS_LDT:
        case X86_SEL_TYPE_SYS_286_TSS_BUSY:
        case X86_SEL_TYPE_SYS_286_CALL_GATE:
        case X86_SEL_TYPE_SYS_UNDEFINED2:
        case X86_SEL_TYPE_SYS_386_TSS_AVAIL:
        case X86_SEL_TYPE_SYS_UNDEFINED3:
        case X86_SEL_TYPE_SYS_386_TSS_BUSY:
        case X86_SEL_TYPE_SYS_386_CALL_GATE:
        case X86_SEL_TYPE_SYS_UNDEFINED4:
        {
            /** @todo check what actually happens when the type is wrong...
             *        esp. call gates. */
            Log(("RaiseXcptOrIntInProtMode %#x - invalid type (%#x) -> #GP\n", u8Vector, Idte.Gate.u4Type));
            return iemRaiseGeneralProtectionFault(pVCpu, X86_TRAP_ERR_IDT | ((uint16_t)u8Vector << X86_TRAP_ERR_SEL_SHIFT));
        }

        case X86_SEL_TYPE_SYS_286_INT_GATE:
            f32BitGate = false;
            RT_FALL_THRU();
        case X86_SEL_TYPE_SYS_386_INT_GATE:
            fEflToClear |= X86_EFL_IF;
            break;

        case X86_SEL_TYPE_SYS_TASK_GATE:
            fTaskGate = true;
#ifndef IEM_IMPLEMENTS_TASKSWITCH
            IEM_RETURN_ASPECT_NOT_IMPLEMENTED_LOG(("Task gates\n"));
#endif
            break;

        case X86_SEL_TYPE_SYS_286_TRAP_GATE:
            f32BitGate = false;
            break;
        case X86_SEL_TYPE_SYS_386_TRAP_GATE:
            break;

        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }

    /* Check DPL against CPL if applicable. */
    if ((fFlags & (IEM_XCPT_FLAGS_T_SOFT_INT | IEM_XCPT_FLAGS_ICEBP_INSTR)) == IEM_XCPT_FLAGS_T_SOFT_INT)
    {
        if (IEM_GET_CPL(pVCpu) > Idte.Gate.u2Dpl)
        {
            Log(("RaiseXcptOrIntInProtMode %#x - CPL (%d) > DPL (%d) -> #GP\n", u8Vector, IEM_GET_CPL(pVCpu), Idte.Gate.u2Dpl));
            return iemRaiseGeneralProtectionFault(pVCpu, X86_TRAP_ERR_IDT | ((uint16_t)u8Vector << X86_TRAP_ERR_SEL_SHIFT));
        }
    }

    /* Is it there? */
    if (!Idte.Gate.u1Present)
    {
        Log(("RaiseXcptOrIntInProtMode %#x - not present -> #NP\n", u8Vector));
        return iemRaiseSelectorNotPresentWithErr(pVCpu, X86_TRAP_ERR_IDT | ((uint16_t)u8Vector << X86_TRAP_ERR_SEL_SHIFT));
    }

    /* Is it a task-gate? */
    if (fTaskGate)
    {
        /*
         * Construct the error code masks based on what caused this task switch.
         * See Intel Instruction reference for INT.
         */
        uint16_t const uExt     = (    (fFlags & IEM_XCPT_FLAGS_T_SOFT_INT)
                                   && !(fFlags & IEM_XCPT_FLAGS_ICEBP_INSTR)) ? 0 : 1;
        uint16_t const uSelMask = X86_SEL_MASK_OFF_RPL;
        RTSEL          SelTss   = Idte.Gate.u16Sel;

        /*
         * Fetch the TSS descriptor in the GDT.
         */
        IEMSELDESC DescTSS;
        rcStrict = iemMemFetchSelDescWithErr(pVCpu, &DescTSS, SelTss, X86_XCPT_GP, (SelTss & uSelMask) | uExt);
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("RaiseXcptOrIntInProtMode %#x - failed to fetch TSS selector %#x, rc=%Rrc\n", u8Vector, SelTss,
                 VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }

        /* The TSS descriptor must be a system segment and be available (not busy). */
        if (   DescTSS.Legacy.Gen.u1DescType
            || (   DescTSS.Legacy.Gen.u4Type != X86_SEL_TYPE_SYS_286_TSS_AVAIL
                && DescTSS.Legacy.Gen.u4Type != X86_SEL_TYPE_SYS_386_TSS_AVAIL))
        {
            Log(("RaiseXcptOrIntInProtMode %#x - TSS selector %#x of task gate not a system descriptor or not available %#RX64\n",
                 u8Vector, SelTss, DescTSS.Legacy.au64));
            return iemRaiseGeneralProtectionFault(pVCpu, (SelTss & uSelMask) | uExt);
        }

        /* The TSS must be present. */
        if (!DescTSS.Legacy.Gen.u1Present)
        {
            Log(("RaiseXcptOrIntInProtMode %#x - TSS selector %#x not present %#RX64\n", u8Vector, SelTss, DescTSS.Legacy.au64));
            return iemRaiseSelectorNotPresentWithErr(pVCpu, (SelTss & uSelMask) | uExt);
        }

        /* Do the actual task switch. */
        return iemTaskSwitch(pVCpu, IEMTASKSWITCH_INT_XCPT,
                             (fFlags & IEM_XCPT_FLAGS_T_SOFT_INT) ? pVCpu->cpum.GstCtx.eip + cbInstr : pVCpu->cpum.GstCtx.eip,
                             fFlags, uErr, uCr2, SelTss, &DescTSS);
    }

    /* A null CS is bad. */
    RTSEL NewCS = Idte.Gate.u16Sel;
    if (!(NewCS & X86_SEL_MASK_OFF_RPL))
    {
        Log(("RaiseXcptOrIntInProtMode %#x - CS=%#x -> #GP\n", u8Vector, NewCS));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /* Fetch the descriptor for the new CS. */
    IEMSELDESC DescCS;
    rcStrict = iemMemFetchSelDesc(pVCpu, &DescCS, NewCS, X86_XCPT_GP); /** @todo correct exception? */
    if (rcStrict != VINF_SUCCESS)
    {
        Log(("RaiseXcptOrIntInProtMode %#x - CS=%#x - rc=%Rrc\n", u8Vector, NewCS, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }

    /* Must be a code segment. */
    if (!DescCS.Legacy.Gen.u1DescType)
    {
        Log(("RaiseXcptOrIntInProtMode %#x - CS=%#x - system selector (%#x) -> #GP\n", u8Vector, NewCS, DescCS.Legacy.Gen.u4Type));
        return iemRaiseGeneralProtectionFault(pVCpu, NewCS & X86_SEL_MASK_OFF_RPL);
    }
    if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_CODE))
    {
        Log(("RaiseXcptOrIntInProtMode %#x - CS=%#x - data selector (%#x) -> #GP\n", u8Vector, NewCS, DescCS.Legacy.Gen.u4Type));
        return iemRaiseGeneralProtectionFault(pVCpu, NewCS & X86_SEL_MASK_OFF_RPL);
    }

    /* Don't allow lowering the privilege level. */
    /** @todo Does the lowering of privileges apply to software interrupts
     *        only?  This has bearings on the more-privileged or
     *        same-privilege stack behavior further down.  A testcase would
     *        be nice. */
    if (DescCS.Legacy.Gen.u2Dpl > IEM_GET_CPL(pVCpu))
    {
        Log(("RaiseXcptOrIntInProtMode %#x - CS=%#x - DPL (%d) > CPL (%d) -> #GP\n",
             u8Vector, NewCS, DescCS.Legacy.Gen.u2Dpl, IEM_GET_CPL(pVCpu)));
        return iemRaiseGeneralProtectionFault(pVCpu, NewCS & X86_SEL_MASK_OFF_RPL);
    }

    /* Make sure the selector is present. */
    if (!DescCS.Legacy.Gen.u1Present)
    {
        Log(("RaiseXcptOrIntInProtMode %#x - CS=%#x - segment not present -> #NP\n", u8Vector, NewCS));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, NewCS);
    }

#ifdef LOG_ENABLED
    /* If software interrupt, try decode it if logging is enabled and such. */
    if (   (fFlags & IEM_XCPT_FLAGS_T_SOFT_INT)
        && LogIsItEnabled(RTLOGGRPFLAGS_ENABLED, LOG_GROUP_IEM_SYSCALL))
        iemLogSyscallProtModeInt(pVCpu, u8Vector, cbInstr);
#endif

    /* Check the new EIP against the new CS limit. */
    uint32_t const uNewEip =    Idte.Gate.u4Type == X86_SEL_TYPE_SYS_286_INT_GATE
                             || Idte.Gate.u4Type == X86_SEL_TYPE_SYS_286_TRAP_GATE
                           ? Idte.Gate.u16OffsetLow
                           : Idte.Gate.u16OffsetLow | ((uint32_t)Idte.Gate.u16OffsetHigh << 16);
    uint32_t cbLimitCS = X86DESC_LIMIT_G(&DescCS.Legacy);
    if (uNewEip > cbLimitCS)
    {
        Log(("RaiseXcptOrIntInProtMode %#x - EIP=%#x > cbLimitCS=%#x (CS=%#x) -> #GP(0)\n",
             u8Vector, uNewEip, cbLimitCS, NewCS));
        return iemRaiseGeneralProtectionFault(pVCpu, 0);
    }
    Log7(("iemRaiseXcptOrIntInProtMode: new EIP=%#x CS=%#x\n", uNewEip, NewCS));

    /* Calc the flag image to push. */
    uint32_t        fEfl    = IEMMISC_GET_EFL(pVCpu);
    if (fFlags & (IEM_XCPT_FLAGS_DRx_INSTR_BP | IEM_XCPT_FLAGS_T_SOFT_INT))
        fEfl &= ~X86_EFL_RF;
    else
        fEfl |= X86_EFL_RF; /* Vagueness is all I've found on this so far... */ /** @todo Automatically pushing EFLAGS.RF. */

    /* From V8086 mode only go to CPL 0. */
    uint8_t const   uNewCpl = DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_CONF
                            ? IEM_GET_CPL(pVCpu) : DescCS.Legacy.Gen.u2Dpl;
    if ((fEfl & X86_EFL_VM) && uNewCpl != 0) /** @todo When exactly is this raised? */
    {
        Log(("RaiseXcptOrIntInProtMode %#x - CS=%#x - New CPL (%d) != 0 w/ VM=1 -> #GP\n", u8Vector, NewCS, uNewCpl));
        return iemRaiseGeneralProtectionFault(pVCpu, 0);
    }

    /*
     * If the privilege level changes, we need to get a new stack from the TSS.
     * This in turns means validating the new SS and ESP...
     */
    if (uNewCpl != IEM_GET_CPL(pVCpu))
    {
        RTSEL    NewSS;
        uint32_t uNewEsp;
        rcStrict = iemRaiseLoadStackFromTss32Or16(pVCpu, uNewCpl, &NewSS, &uNewEsp);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        IEMSELDESC DescSS;
        rcStrict = iemMiscValidateNewSS(pVCpu, NewSS, uNewCpl, &DescSS);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        /* If the new SS is 16-bit, we are only going to use SP, not ESP. */
        if (!DescSS.Legacy.Gen.u1DefBig)
        {
            Log(("iemRaiseXcptOrIntInProtMode: Forcing ESP=%#x to 16 bits\n", uNewEsp));
            uNewEsp = (uint16_t)uNewEsp;
        }

        Log7(("iemRaiseXcptOrIntInProtMode: New SS=%#x ESP=%#x (from TSS); current SS=%#x ESP=%#x\n", NewSS, uNewEsp, pVCpu->cpum.GstCtx.ss.Sel, pVCpu->cpum.GstCtx.esp));

        /* Check that there is sufficient space for the stack frame. */
        uint32_t cbLimitSS = X86DESC_LIMIT_G(&DescSS.Legacy);
        uint8_t const cbStackFrame = !(fEfl & X86_EFL_VM)
                                   ? (fFlags & IEM_XCPT_FLAGS_ERR ? 12 : 10) << f32BitGate
                                   : (fFlags & IEM_XCPT_FLAGS_ERR ? 20 : 18) << f32BitGate;

        if (!(DescSS.Legacy.Gen.u4Type & X86_SEL_TYPE_DOWN))
        {
            if (   uNewEsp - 1 > cbLimitSS
                || uNewEsp < cbStackFrame)
            {
                Log(("RaiseXcptOrIntInProtMode: %#x - SS=%#x ESP=%#x cbStackFrame=%#x is out of bounds -> #GP\n",
                     u8Vector, NewSS, uNewEsp, cbStackFrame));
                return iemRaiseSelectorBoundsBySelector(pVCpu, NewSS);
            }
        }
        else
        {
            if (   uNewEsp - 1 > (DescSS.Legacy.Gen.u1DefBig ? UINT32_MAX : UINT16_MAX)
                || uNewEsp - cbStackFrame < cbLimitSS + UINT32_C(1))
            {
                Log(("RaiseXcptOrIntInProtMode: %#x - SS=%#x ESP=%#x cbStackFrame=%#x (expand down) is out of bounds -> #GP\n",
                     u8Vector, NewSS, uNewEsp, cbStackFrame));
                return iemRaiseSelectorBoundsBySelector(pVCpu, NewSS);
            }
        }

        /*
         * Start making changes.
         */

        /* Set the new CPL so that stack accesses use it. */
        uint8_t const uOldCpl = IEM_GET_CPL(pVCpu);
        IEM_SET_CPL(pVCpu, uNewCpl);

        /* Create the stack frame. */
        uint8_t    bUnmapInfoStackFrame;
        RTPTRUNION uStackFrame;
        rcStrict = iemMemMap(pVCpu, &uStackFrame.pv, &bUnmapInfoStackFrame, cbStackFrame, UINT8_MAX,
                             uNewEsp - cbStackFrame + X86DESC_BASE(&DescSS.Legacy),
                             IEM_ACCESS_STACK_W | IEM_ACCESS_WHAT_SYS, 0); /* _SYS is a hack ... */
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        if (f32BitGate)
        {
            if (fFlags & IEM_XCPT_FLAGS_ERR)
                *uStackFrame.pu32++ = uErr;
            uStackFrame.pu32[0] = (fFlags & IEM_XCPT_FLAGS_T_SOFT_INT) ? pVCpu->cpum.GstCtx.eip + cbInstr : pVCpu->cpum.GstCtx.eip;
            uStackFrame.pu32[1] = (pVCpu->cpum.GstCtx.cs.Sel & ~X86_SEL_RPL) | uOldCpl;
            uStackFrame.pu32[2] = fEfl;
            uStackFrame.pu32[3] = pVCpu->cpum.GstCtx.esp;
            uStackFrame.pu32[4] = pVCpu->cpum.GstCtx.ss.Sel;
            Log7(("iemRaiseXcptOrIntInProtMode: 32-bit push SS=%#x ESP=%#x\n", pVCpu->cpum.GstCtx.ss.Sel, pVCpu->cpum.GstCtx.esp));
            if (fEfl & X86_EFL_VM)
            {
                uStackFrame.pu32[1] = pVCpu->cpum.GstCtx.cs.Sel;
                uStackFrame.pu32[5] = pVCpu->cpum.GstCtx.es.Sel;
                uStackFrame.pu32[6] = pVCpu->cpum.GstCtx.ds.Sel;
                uStackFrame.pu32[7] = pVCpu->cpum.GstCtx.fs.Sel;
                uStackFrame.pu32[8] = pVCpu->cpum.GstCtx.gs.Sel;
            }
        }
        else
        {
            if (fFlags & IEM_XCPT_FLAGS_ERR)
                *uStackFrame.pu16++ = uErr;
            uStackFrame.pu16[0] = (fFlags & IEM_XCPT_FLAGS_T_SOFT_INT) ? pVCpu->cpum.GstCtx.ip + cbInstr : pVCpu->cpum.GstCtx.ip;
            uStackFrame.pu16[1] = (pVCpu->cpum.GstCtx.cs.Sel & ~X86_SEL_RPL) | uOldCpl;
            uStackFrame.pu16[2] = fEfl;
            uStackFrame.pu16[3] = pVCpu->cpum.GstCtx.sp;
            uStackFrame.pu16[4] = pVCpu->cpum.GstCtx.ss.Sel;
            Log7(("iemRaiseXcptOrIntInProtMode: 16-bit push SS=%#x SP=%#x\n", pVCpu->cpum.GstCtx.ss.Sel, pVCpu->cpum.GstCtx.sp));
            if (fEfl & X86_EFL_VM)
            {
                uStackFrame.pu16[1] = pVCpu->cpum.GstCtx.cs.Sel;
                uStackFrame.pu16[5] = pVCpu->cpum.GstCtx.es.Sel;
                uStackFrame.pu16[6] = pVCpu->cpum.GstCtx.ds.Sel;
                uStackFrame.pu16[7] = pVCpu->cpum.GstCtx.fs.Sel;
                uStackFrame.pu16[8] = pVCpu->cpum.GstCtx.gs.Sel;
            }
        }
        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfoStackFrame);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        /* Mark the selectors 'accessed' (hope this is the correct time). */
        /** @todo testcase: excatly _when_ are the accessed bits set - before or
         *        after pushing the stack frame? (Write protect the gdt + stack to
         *        find out.) */
        if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
        {
            rcStrict = iemMemMarkSelDescAccessed(pVCpu, NewCS);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            DescCS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
        }

        if (!(DescSS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
        {
            rcStrict = iemMemMarkSelDescAccessed(pVCpu, NewSS);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            DescSS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
        }

        /*
         * Start comitting the register changes (joins with the DPL=CPL branch).
         */
        pVCpu->cpum.GstCtx.ss.Sel            = NewSS;
        pVCpu->cpum.GstCtx.ss.ValidSel       = NewSS;
        pVCpu->cpum.GstCtx.ss.fFlags         = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.ss.u32Limit       = cbLimitSS;
        pVCpu->cpum.GstCtx.ss.u64Base        = X86DESC_BASE(&DescSS.Legacy);
        pVCpu->cpum.GstCtx.ss.Attr.u         = X86DESC_GET_HID_ATTR(&DescSS.Legacy);
        /** @todo When coming from 32-bit code and operating with a 16-bit TSS and
         *        16-bit handler, the high word of ESP remains unchanged (i.e. only
         *        SP is loaded).
         *  Need to check the other combinations too:
         *      - 16-bit TSS, 32-bit handler
         *      - 32-bit TSS, 16-bit handler */
        if (!pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
            pVCpu->cpum.GstCtx.sp            = (uint16_t)(uNewEsp - cbStackFrame);
        else
            pVCpu->cpum.GstCtx.rsp           = uNewEsp - cbStackFrame;

        if (fEfl & X86_EFL_VM)
        {
            iemHlpLoadNullDataSelectorOnV86Xcpt(pVCpu, &pVCpu->cpum.GstCtx.gs);
            iemHlpLoadNullDataSelectorOnV86Xcpt(pVCpu, &pVCpu->cpum.GstCtx.fs);
            iemHlpLoadNullDataSelectorOnV86Xcpt(pVCpu, &pVCpu->cpum.GstCtx.es);
            iemHlpLoadNullDataSelectorOnV86Xcpt(pVCpu, &pVCpu->cpum.GstCtx.ds);
        }
    }
    /*
     * Same privilege, no stack change and smaller stack frame.
     */
    else
    {
        uint64_t        uNewRsp;
        uint8_t         bUnmapInfoStackFrame;
        RTPTRUNION      uStackFrame;
        uint8_t const   cbStackFrame = (fFlags & IEM_XCPT_FLAGS_ERR ? 8 : 6) << f32BitGate;
        rcStrict = iemMemStackPushBeginSpecial(pVCpu, cbStackFrame, f32BitGate ? 3 : 1,
                                               &uStackFrame.pv, &bUnmapInfoStackFrame, &uNewRsp);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        if (f32BitGate)
        {
            if (fFlags & IEM_XCPT_FLAGS_ERR)
                *uStackFrame.pu32++ = uErr;
            uStackFrame.pu32[0] = fFlags & IEM_XCPT_FLAGS_T_SOFT_INT ? pVCpu->cpum.GstCtx.eip + cbInstr : pVCpu->cpum.GstCtx.eip;
            uStackFrame.pu32[1] = (pVCpu->cpum.GstCtx.cs.Sel & ~X86_SEL_RPL) | IEM_GET_CPL(pVCpu);
            uStackFrame.pu32[2] = fEfl;
        }
        else
        {
            if (fFlags & IEM_XCPT_FLAGS_ERR)
                *uStackFrame.pu16++ = uErr;
            uStackFrame.pu16[0] = fFlags & IEM_XCPT_FLAGS_T_SOFT_INT ? pVCpu->cpum.GstCtx.eip + cbInstr : pVCpu->cpum.GstCtx.eip;
            uStackFrame.pu16[1] = (pVCpu->cpum.GstCtx.cs.Sel & ~X86_SEL_RPL) | IEM_GET_CPL(pVCpu);
            uStackFrame.pu16[2] = fEfl;
        }
        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfoStackFrame); /* don't use the commit here */
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        /* Mark the CS selector as 'accessed'. */
        if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
        {
            rcStrict = iemMemMarkSelDescAccessed(pVCpu, NewCS);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            DescCS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
        }

        /*
         * Start committing the register changes (joins with the other branch).
         */
        pVCpu->cpum.GstCtx.rsp = uNewRsp;
    }

    /* ... register committing continues. */
    pVCpu->cpum.GstCtx.cs.Sel            = (NewCS & ~X86_SEL_RPL) | uNewCpl;
    pVCpu->cpum.GstCtx.cs.ValidSel       = (NewCS & ~X86_SEL_RPL) | uNewCpl;
    pVCpu->cpum.GstCtx.cs.fFlags         = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.cs.u32Limit       = cbLimitCS;
    pVCpu->cpum.GstCtx.cs.u64Base        = X86DESC_BASE(&DescCS.Legacy);
    pVCpu->cpum.GstCtx.cs.Attr.u         = X86DESC_GET_HID_ATTR(&DescCS.Legacy);

    pVCpu->cpum.GstCtx.rip               = uNewEip;  /* (The entire register is modified, see pe16_32 bs3kit tests.) */
    fEfl &= ~fEflToClear;
    IEMMISC_SET_EFL(pVCpu, fEfl);

    if (fFlags & IEM_XCPT_FLAGS_CR2)
        pVCpu->cpum.GstCtx.cr2 = uCr2;

    if (fFlags & IEM_XCPT_FLAGS_T_CPU_XCPT)
        iemRaiseXcptAdjustState(pVCpu, u8Vector);

    /* Make sure the execution flags are correct. */
    uint32_t const fExecNew = iemCalcExecFlags(pVCpu) | (ICORE(pVCpu).fExec & IEM_F_USER_OPTS);
    if (fExecNew != ICORE(pVCpu).fExec)
        Log(("iemRaiseXcptOrIntInProtMode: fExec %#x -> %#x (xor %#x)\n",
             ICORE(pVCpu).fExec, fExecNew, ICORE(pVCpu).fExec ^ fExecNew));
    ICORE(pVCpu).fExec = fExecNew;
    Assert(IEM_GET_CPL(pVCpu) == uNewCpl);

    /*
     * Deal with debug events that follows the exception and clear inhibit flags.
     */
    if (   !(fFlags & IEM_XCPT_FLAGS_T_SOFT_INT)
        || !(pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK))
        pVCpu->cpum.GstCtx.eflags.uBoth &= ~(CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_INHIBIT_SHADOW);
    else
    {
        Log(("iemRaiseXcptOrIntInProtMode: Raising #DB after %#x; pending=%#x\n",
             u8Vector, pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK));
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR6);
        pVCpu->cpum.GstCtx.dr[6] |= (pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK_NONSILENT)
                                  >> CPUMCTX_DBG_HIT_DRX_SHIFT;
        pVCpu->cpum.GstCtx.eflags.uBoth &= ~(CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_INHIBIT_SHADOW);
        return iemRaiseDebugException(pVCpu);
    }

    return fFlags & IEM_XCPT_FLAGS_T_CPU_XCPT ? VINF_IEM_RAISED_XCPT : VINF_SUCCESS;
}


/**
 * Implements exceptions and interrupts for long mode.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr         The number of bytes to offset rIP by in the return
 *                          address.
 * @param   u8Vector        The interrupt / exception vector number.
 * @param   fFlags          The flags.
 * @param   uErr            The error value if IEM_XCPT_FLAGS_ERR is set.
 * @param   uCr2            The CR2 value if IEM_XCPT_FLAGS_CR2 is set.
 */
static VBOXSTRICTRC
iemRaiseXcptOrIntInLongMode(PVMCPUCC    pVCpu,
                            uint8_t     cbInstr,
                            uint8_t     u8Vector,
                            uint32_t    fFlags,
                            uint16_t    uErr,
                            uint64_t    uCr2) RT_NOEXCEPT
{
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_XCPT_MASK);

    /*
     * Read the IDT entry.
     */
    uint16_t offIdt = (uint16_t)u8Vector << 4;
    if (pVCpu->cpum.GstCtx.idtr.cbIdt < offIdt + 7)
    {
        Log(("iemRaiseXcptOrIntInLongMode: %#x is out of bounds (%#x)\n", u8Vector, pVCpu->cpum.GstCtx.idtr.cbIdt));
        return iemRaiseGeneralProtectionFault(pVCpu, X86_TRAP_ERR_IDT | ((uint16_t)u8Vector << X86_TRAP_ERR_SEL_SHIFT));
    }
    X86DESC64 Idte;
#ifdef _MSC_VER /* Shut up silly compiler warning. */
    Idte.au64[0] = 0;
    Idte.au64[1] = 0;
#endif
    VBOXSTRICTRC rcStrict = iemMemFetchSysU64(pVCpu, &Idte.au64[0], UINT8_MAX, pVCpu->cpum.GstCtx.idtr.pIdt + offIdt);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        rcStrict = iemMemFetchSysU64(pVCpu, &Idte.au64[1], UINT8_MAX, pVCpu->cpum.GstCtx.idtr.pIdt + offIdt + 8);
    if (RT_UNLIKELY(rcStrict != VINF_SUCCESS))
    {
        Log(("iemRaiseXcptOrIntInLongMode: failed to fetch IDT entry! vec=%#x rc=%Rrc\n", u8Vector, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }
    Log(("iemRaiseXcptOrIntInLongMode: vec=%#x P=%u DPL=%u DT=%u:%u IST=%u %04x:%08x%04x%04x\n",
         u8Vector, Idte.Gate.u1Present, Idte.Gate.u2Dpl, Idte.Gate.u1DescType, Idte.Gate.u4Type,
         Idte.Gate.u3IST, Idte.Gate.u16Sel, Idte.Gate.u32OffsetTop, Idte.Gate.u16OffsetHigh, Idte.Gate.u16OffsetLow));

    /*
     * Check the descriptor type, DPL and such.
     * ASSUMES this is done in the same order as described for call-gate calls.
     */
    if (Idte.Gate.u1DescType)
    {
        Log(("iemRaiseXcptOrIntInLongMode %#x - not system selector (%#x) -> #GP\n", u8Vector, Idte.Gate.u4Type));
        return iemRaiseGeneralProtectionFault(pVCpu, X86_TRAP_ERR_IDT | ((uint16_t)u8Vector << X86_TRAP_ERR_SEL_SHIFT));
    }
    uint32_t fEflToClear = X86_EFL_TF | X86_EFL_NT | X86_EFL_RF | X86_EFL_VM;
    switch (Idte.Gate.u4Type)
    {
        case AMD64_SEL_TYPE_SYS_INT_GATE:
            fEflToClear |= X86_EFL_IF;
            break;
        case AMD64_SEL_TYPE_SYS_TRAP_GATE:
            break;

        default:
            Log(("iemRaiseXcptOrIntInLongMode %#x - invalid type (%#x) -> #GP\n", u8Vector, Idte.Gate.u4Type));
            return iemRaiseGeneralProtectionFault(pVCpu, X86_TRAP_ERR_IDT | ((uint16_t)u8Vector << X86_TRAP_ERR_SEL_SHIFT));
    }

    /* Check DPL against CPL if applicable. */
    if ((fFlags & (IEM_XCPT_FLAGS_T_SOFT_INT | IEM_XCPT_FLAGS_ICEBP_INSTR)) == IEM_XCPT_FLAGS_T_SOFT_INT)
    {
        if (IEM_GET_CPL(pVCpu) > Idte.Gate.u2Dpl)
        {
            Log(("iemRaiseXcptOrIntInLongMode %#x - CPL (%d) > DPL (%d) -> #GP\n", u8Vector, IEM_GET_CPL(pVCpu), Idte.Gate.u2Dpl));
            return iemRaiseGeneralProtectionFault(pVCpu, X86_TRAP_ERR_IDT | ((uint16_t)u8Vector << X86_TRAP_ERR_SEL_SHIFT));
        }
    }

    /* Is it there? */
    if (!Idte.Gate.u1Present)
    {
        Log(("iemRaiseXcptOrIntInLongMode %#x - not present -> #NP\n", u8Vector));
        return iemRaiseSelectorNotPresentWithErr(pVCpu, X86_TRAP_ERR_IDT | ((uint16_t)u8Vector << X86_TRAP_ERR_SEL_SHIFT));
    }

    /* A null CS is bad. */
    RTSEL NewCS = Idte.Gate.u16Sel;
    if (!(NewCS & X86_SEL_MASK_OFF_RPL))
    {
        Log(("iemRaiseXcptOrIntInLongMode %#x - CS=%#x -> #GP\n", u8Vector, NewCS));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /* Fetch the descriptor for the new CS. */
    IEMSELDESC DescCS;
    rcStrict = iemMemFetchSelDesc(pVCpu, &DescCS, NewCS, X86_XCPT_GP);
    if (rcStrict != VINF_SUCCESS)
    {
        Log(("iemRaiseXcptOrIntInLongMode %#x - CS=%#x - rc=%Rrc\n", u8Vector, NewCS, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }

    /* Must be a 64-bit code segment. */
    if (!DescCS.Long.Gen.u1DescType)
    {
        Log(("iemRaiseXcptOrIntInLongMode %#x - CS=%#x - system selector (%#x) -> #GP\n", u8Vector, NewCS, DescCS.Legacy.Gen.u4Type));
        return iemRaiseGeneralProtectionFault(pVCpu, NewCS & X86_SEL_MASK_OFF_RPL);
    }
    if (   !DescCS.Long.Gen.u1Long
        || DescCS.Long.Gen.u1DefBig
        || !(DescCS.Long.Gen.u4Type & X86_SEL_TYPE_CODE) )
    {
        Log(("iemRaiseXcptOrIntInLongMode %#x - CS=%#x - not 64-bit code selector (%#x, L=%u, D=%u) -> #GP\n",
             u8Vector, NewCS, DescCS.Legacy.Gen.u4Type, DescCS.Long.Gen.u1Long, DescCS.Long.Gen.u1DefBig));
        return iemRaiseGeneralProtectionFault(pVCpu, NewCS & X86_SEL_MASK_OFF_RPL);
    }

    /* Don't allow lowering the privilege level.  For non-conforming CS
       selectors, the CS.DPL sets the privilege level the trap/interrupt
       handler runs at.  For conforming CS selectors, the CPL remains
       unchanged, but the CS.DPL must be <= CPL. */
    /** @todo Testcase: Interrupt handler with CS.DPL=1, interrupt dispatched
     *        when CPU in Ring-0. Result \#GP?  */
    if (DescCS.Legacy.Gen.u2Dpl > IEM_GET_CPL(pVCpu))
    {
        Log(("iemRaiseXcptOrIntInLongMode %#x - CS=%#x - DPL (%d) > CPL (%d) -> #GP\n",
             u8Vector, NewCS, DescCS.Legacy.Gen.u2Dpl, IEM_GET_CPL(pVCpu)));
        return iemRaiseGeneralProtectionFault(pVCpu, NewCS & X86_SEL_MASK_OFF_RPL);
    }


    /* Make sure the selector is present. */
    if (!DescCS.Legacy.Gen.u1Present)
    {
        Log(("iemRaiseXcptOrIntInLongMode %#x - CS=%#x - segment not present -> #NP\n", u8Vector, NewCS));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, NewCS);
    }

    /* Check that the new RIP is canonical. */
    uint64_t const uNewRip = Idte.Gate.u16OffsetLow
                           | ((uint32_t)Idte.Gate.u16OffsetHigh << 16)
                           | ((uint64_t)Idte.Gate.u32OffsetTop  << 32);
    if (!IEM_IS_CANONICAL(uNewRip))
    {
        Log(("iemRaiseXcptOrIntInLongMode %#x - RIP=%#RX64 - Not canonical -> #GP(0)\n", u8Vector, uNewRip));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /*
     * If the privilege level changes or if the IST isn't zero, we need to get
     * a new stack from the TSS.
     */
    uint64_t        uNewRsp;
    uint8_t const   uNewCpl = DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_CONF
                            ? IEM_GET_CPL(pVCpu) : DescCS.Legacy.Gen.u2Dpl;
    if (   uNewCpl != IEM_GET_CPL(pVCpu)
        || Idte.Gate.u3IST != 0)
    {
        rcStrict = iemRaiseLoadStackFromTss64(pVCpu, uNewCpl, Idte.Gate.u3IST, &uNewRsp);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }
    else
        uNewRsp = pVCpu->cpum.GstCtx.rsp;
    uNewRsp &= ~(uint64_t)0xf;

    /*
     * Calc the flag image to push.
     */
    uint32_t        fEfl    = IEMMISC_GET_EFL(pVCpu);
    if (fFlags & (IEM_XCPT_FLAGS_DRx_INSTR_BP | IEM_XCPT_FLAGS_T_SOFT_INT))
        fEfl &= ~X86_EFL_RF;
    else
        fEfl |= X86_EFL_RF; /* Vagueness is all I've found on this so far... */ /** @todo Automatically pushing EFLAGS.RF. */

    /*
     * Start making changes.
     */
    /* Set the new CPL so that stack accesses use it. */
    uint8_t const uOldCpl = IEM_GET_CPL(pVCpu);
    IEM_SET_CPL(pVCpu, uNewCpl);
/** @todo Setting CPL this early seems wrong as it would affect and errors we
 *        raise accessing the stack and (?) GDT/LDT... */

    /* Create the stack frame. */
    uint8_t    bUnmapInfoStackFrame;
    uint32_t   cbStackFrame = sizeof(uint64_t) * (5 + !!(fFlags & IEM_XCPT_FLAGS_ERR));
    RTPTRUNION uStackFrame;
    rcStrict = iemMemMap(pVCpu, &uStackFrame.pv, &bUnmapInfoStackFrame, cbStackFrame, UINT8_MAX,
                         uNewRsp - cbStackFrame, IEM_ACCESS_STACK_W | IEM_ACCESS_WHAT_SYS, 0); /* _SYS is a hack ... */
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    if (fFlags & IEM_XCPT_FLAGS_ERR)
        *uStackFrame.pu64++ = uErr;
    uStackFrame.pu64[0] = fFlags & IEM_XCPT_FLAGS_T_SOFT_INT ? pVCpu->cpum.GstCtx.rip + cbInstr : pVCpu->cpum.GstCtx.rip;
    uStackFrame.pu64[1] = (pVCpu->cpum.GstCtx.cs.Sel & ~X86_SEL_RPL) | uOldCpl; /* CPL paranoia */
    uStackFrame.pu64[2] = fEfl;
    uStackFrame.pu64[3] = pVCpu->cpum.GstCtx.rsp;
    uStackFrame.pu64[4] = pVCpu->cpum.GstCtx.ss.Sel;
    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfoStackFrame);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /* Mark the CS selectors 'accessed' (hope this is the correct time). */
    /** @todo testcase: excatly _when_ are the accessed bits set - before or
     *        after pushing the stack frame? (Write protect the gdt + stack to
     *        find out.) */
    if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
    {
        rcStrict = iemMemMarkSelDescAccessed(pVCpu, NewCS);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        DescCS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
    }

    /*
     * Start comitting the register changes.
     */
    /** @todo research/testcase: Figure out what VT-x and AMD-V loads into the
     *        hidden registers when interrupting 32-bit or 16-bit code! */
    if (uNewCpl != uOldCpl)
    {
        pVCpu->cpum.GstCtx.ss.Sel        = 0 | uNewCpl;
        pVCpu->cpum.GstCtx.ss.ValidSel   = 0 | uNewCpl;
        pVCpu->cpum.GstCtx.ss.fFlags     = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.ss.u32Limit   = UINT32_MAX;
        pVCpu->cpum.GstCtx.ss.u64Base    = 0;
        pVCpu->cpum.GstCtx.ss.Attr.u     = (uNewCpl << X86DESCATTR_DPL_SHIFT) | X86DESCATTR_UNUSABLE;
    }
    pVCpu->cpum.GstCtx.rsp           = uNewRsp - cbStackFrame;
    pVCpu->cpum.GstCtx.cs.Sel        = (NewCS & ~X86_SEL_RPL) | uNewCpl;
    pVCpu->cpum.GstCtx.cs.ValidSel   = (NewCS & ~X86_SEL_RPL) | uNewCpl;
    pVCpu->cpum.GstCtx.cs.fFlags     = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.cs.u32Limit   = X86DESC_LIMIT_G(&DescCS.Legacy);
    pVCpu->cpum.GstCtx.cs.u64Base    = X86DESC_BASE(&DescCS.Legacy);
    pVCpu->cpum.GstCtx.cs.Attr.u     = X86DESC_GET_HID_ATTR(&DescCS.Legacy);
    pVCpu->cpum.GstCtx.rip           = uNewRip;

    fEfl &= ~fEflToClear;
    IEMMISC_SET_EFL(pVCpu, fEfl);

    if (fFlags & IEM_XCPT_FLAGS_CR2)
        pVCpu->cpum.GstCtx.cr2 = uCr2;

    if (fFlags & IEM_XCPT_FLAGS_T_CPU_XCPT)
        iemRaiseXcptAdjustState(pVCpu, u8Vector);

    iemRecalcExecModeAndCplAndAcFlags(pVCpu);

    /*
     * Deal with debug events that follows the exception and clear inhibit flags.
     */
    if (   !(fFlags & IEM_XCPT_FLAGS_T_SOFT_INT)
        || !(pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK))
        pVCpu->cpum.GstCtx.eflags.uBoth &= ~(CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_INHIBIT_SHADOW);
    else
    {
        Log(("iemRaiseXcptOrIntInLongMode: Raising #DB after %#x; pending=%#x\n",
             u8Vector, pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK));
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR6);
        pVCpu->cpum.GstCtx.dr[6] |= (pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK_NONSILENT)
                                  >> CPUMCTX_DBG_HIT_DRX_SHIFT;
        pVCpu->cpum.GstCtx.eflags.uBoth &= ~(CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_INHIBIT_SHADOW);
        return iemRaiseDebugException(pVCpu);
    }

    return fFlags & IEM_XCPT_FLAGS_T_CPU_XCPT ? VINF_IEM_RAISED_XCPT : VINF_SUCCESS;
}


/**
 * Implements exceptions and interrupts.
 *
 * All exceptions and interrupts goes thru this function!
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr         The number of bytes to offset rIP by in the return
 *                          address.
 * @param   u8Vector        The interrupt / exception vector number.
 * @param   fFlags          The flags.
 * @param   uErr            The error value if IEM_XCPT_FLAGS_ERR is set.
 * @param   uCr2            The CR2 value if IEM_XCPT_FLAGS_CR2 is set.
 */
VBOXSTRICTRC
iemRaiseXcptOrInt(PVMCPUCC    pVCpu,
                  uint8_t     cbInstr,
                  uint8_t     u8Vector,
                  uint32_t    fFlags,
                  uint16_t    uErr,
                  uint64_t    uCr2) RT_NOEXCEPT
{
    /*
     * Get all the state that we might need here.
     */
    IEM_CTX_IMPORT_RET(pVCpu, IEM_CPUMCTX_EXTRN_XCPT_MASK);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_XCPT_MASK);

#ifndef IEM_WITH_CODE_TLB_IN_CUR_CTX /** @todo we're doing it afterwards too, that should suffice... */
    /*
     * Flush prefetch buffer
     */
    ICORE(pVCpu).cbOpcode = ICORE(pVCpu).offOpcode;
#endif

    /*
     * Perform the V8086 IOPL check and upgrade the fault without nesting.
     */
    if (   pVCpu->cpum.GstCtx.eflags.Bits.u1VM
        && pVCpu->cpum.GstCtx.eflags.Bits.u2IOPL != 3
        && (fFlags & (  IEM_XCPT_FLAGS_T_SOFT_INT
                      | IEM_XCPT_FLAGS_BP_INSTR
                      | IEM_XCPT_FLAGS_ICEBP_INSTR
                      | IEM_XCPT_FLAGS_OF_INSTR)) == IEM_XCPT_FLAGS_T_SOFT_INT
        && (pVCpu->cpum.GstCtx.cr0 & X86_CR0_PE) )
    {
        Log(("iemRaiseXcptOrInt: V8086 IOPL check failed for int %#x -> #GP(0)\n", u8Vector));
        fFlags   = IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR;
        u8Vector = X86_XCPT_GP;
        uErr     = 0;
    }

    PVMCC const pVM = pVCpu->CTX_SUFF(pVM);
#ifdef DBGFTRACE_ENABLED
    RTTraceBufAddMsgF(pVM->CTX_SUFF(hTraceBuf), "Xcpt/%u: %02x %u %x %x %llx %04x:%04llx %04x:%04llx",
                      ICORE(pVCpu).cXcptRecursions, u8Vector, cbInstr, fFlags, uErr, uCr2,
                      pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.ss.Sel, pVCpu->cpum.GstCtx.rsp);
#endif

    /*
     * Check if DBGF wants to intercept the exception.
     */
    if (   (fFlags & (IEM_XCPT_FLAGS_T_EXT_INT | IEM_XCPT_FLAGS_T_SOFT_INT))
        || !DBGF_IS_EVENT_ENABLED(pVM, (DBGFEVENTTYPE)(DBGFEVENT_XCPT_FIRST + u8Vector)) )
    { /* likely */ }
    else
    {
        VBOXSTRICTRC rcStrict = DBGFEventGenericWithArgs(pVM, pVCpu, (DBGFEVENTTYPE)(DBGFEVENT_XCPT_FIRST + u8Vector),
                                                         DBGFEVENTCTX_INVALID, 1, (uint64_t)uErr);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }

    /*
     * Evaluate whether NMI blocking should be in effect.
     * Normally, NMI blocking is in effect whenever we inject an NMI.
     */
    bool fBlockNmi = u8Vector == X86_XCPT_NMI
                  && (fFlags & IEM_XCPT_FLAGS_T_CPU_XCPT);

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        VBOXSTRICTRC rcStrict0 = iemVmxVmexitEvent(pVCpu, u8Vector, fFlags, uErr, uCr2, cbInstr);
        if (rcStrict0 != VINF_VMX_INTERCEPT_NOT_ACTIVE)
            return rcStrict0;

        /* If virtual-NMI blocking is in effect for the nested-guest, guest NMIs are not blocked. */
        if (pVCpu->cpum.GstCtx.hwvirt.vmx.fVirtNmiBlocking)
        {
            Assert(CPUMIsGuestVmxPinCtlsSet(&pVCpu->cpum.GstCtx, VMX_PIN_CTLS_VIRT_NMI));
            fBlockNmi = false;
        }
    }
#endif

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    if (CPUMIsGuestInSvmNestedHwVirtMode(IEM_GET_CTX(pVCpu)))
    {
        /*
         * If the event is being injected as part of VMRUN, it isn't subject to event
         * intercepts in the nested-guest. However, secondary exceptions that occur
         * during injection of any event -are- subject to exception intercepts.
         *
         * See AMD spec. 15.20 "Event Injection".
         */
        if (!pVCpu->cpum.GstCtx.hwvirt.svm.fInterceptEvents)
            pVCpu->cpum.GstCtx.hwvirt.svm.fInterceptEvents = true;
        else
        {
            /*
             * Check and handle if the event being raised is intercepted.
             */
            VBOXSTRICTRC rcStrict0 = iemHandleSvmEventIntercept(pVCpu, cbInstr, u8Vector, fFlags, uErr, uCr2);
            if (rcStrict0 != VINF_SVM_INTERCEPT_NOT_ACTIVE)
                return rcStrict0;
        }
    }
#endif

    /*
     * Set NMI blocking if necessary.
     */
    if (fBlockNmi)
        CPUMSetInterruptInhibitingByNmi(&pVCpu->cpum.GstCtx);

    /*
     * Do recursion accounting.
     */
    uint8_t const  uPrevXcpt = ICORE(pVCpu).uCurXcpt;
    uint32_t const fPrevXcpt = ICORE(pVCpu).fCurXcpt;
    if (ICORE(pVCpu).cXcptRecursions == 0)
        Log(("iemRaiseXcptOrInt: %#x at %04x:%RGv cbInstr=%#x fFlags=%#x uErr=%#x uCr2=%llx\n",
             u8Vector, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, cbInstr, fFlags, uErr, uCr2));
    else
    {
        Log(("iemRaiseXcptOrInt: %#x at %04x:%RGv cbInstr=%#x fFlags=%#x uErr=%#x uCr2=%llx; prev=%#x depth=%d flags=%#x\n",
             u8Vector, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, cbInstr, fFlags, uErr, uCr2, ICORE(pVCpu).uCurXcpt,
             ICORE(pVCpu).cXcptRecursions + 1, fPrevXcpt));

        if (ICORE(pVCpu).cXcptRecursions >= 4)
        {
#ifdef DEBUG_bird
            AssertFailed();
#endif
            IEM_RETURN_ASPECT_NOT_IMPLEMENTED_LOG(("Too many fault nestings.\n"));
        }

        /*
         * Evaluate the sequence of recurring events.
         */
        IEMXCPTRAISE enmRaise = IEMEvaluateRecursiveXcpt(pVCpu, fPrevXcpt, uPrevXcpt, fFlags, u8Vector,
                                                         NULL /* pXcptRaiseInfo */);
        if (enmRaise == IEMXCPTRAISE_CURRENT_XCPT)
        { /* likely */ }
        else if (enmRaise == IEMXCPTRAISE_DOUBLE_FAULT)
        {
            Log2(("iemRaiseXcptOrInt: Raising double fault. uPrevXcpt=%#x\n", uPrevXcpt));
            fFlags   = IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR;
            u8Vector = X86_XCPT_DF;
            uErr     = 0;
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
            /* VMX nested-guest #DF intercept needs to be checked here. */
            if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
            {
                VBOXSTRICTRC rcStrict0 = iemVmxVmexitEventDoubleFault(pVCpu);
                if (rcStrict0 != VINF_VMX_INTERCEPT_NOT_ACTIVE)
                    return rcStrict0;
            }
#endif
            /* SVM nested-guest #DF intercepts need to be checked now. See AMD spec. 15.12 "Exception Intercepts". */
            if (IEM_SVM_IS_XCPT_INTERCEPT_SET(pVCpu, X86_XCPT_DF))
                IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_XCPT_DF, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }
        else if (enmRaise == IEMXCPTRAISE_TRIPLE_FAULT)
        {
            Log2(("iemRaiseXcptOrInt: Raising triple fault. uPrevXcpt=%#x\n", uPrevXcpt));
            return iemInitiateCpuShutdown(pVCpu);
        }
        else if (enmRaise == IEMXCPTRAISE_CPU_HANG)
        {
            /* If a nested-guest enters an endless CPU loop condition, we'll emulate it; otherwise guru. */
            Log2(("iemRaiseXcptOrInt: CPU hang condition detected\n"));
            if (   !CPUMIsGuestInSvmNestedHwVirtMode(IEM_GET_CTX(pVCpu))
                && !CPUMIsGuestInVmxNonRootMode(IEM_GET_CTX(pVCpu)))
                return VERR_EM_GUEST_CPU_HANG;
        }
        else
        {
            AssertMsgFailed(("Unexpected condition! enmRaise=%#x uPrevXcpt=%#x fPrevXcpt=%#x, u8Vector=%#x fFlags=%#x\n",
                             enmRaise, uPrevXcpt, fPrevXcpt, u8Vector, fFlags));
            return VERR_IEM_IPE_9;
        }

        /*
         * The 'EXT' bit is set when an exception occurs during deliver of an external
         * event (such as an interrupt or earlier exception)[1]. Privileged software
         * exception (INT1) also sets the EXT bit[2]. Exceptions generated by software
         * interrupts and INTO, INT3 instructions, the 'EXT' bit will not be set.
         *
         * [1] - Intel spec. 6.13 "Error Code"
         * [2] - Intel spec. 26.5.1.1 "Details of Vectored-Event Injection".
         * [3] - Intel Instruction reference for INT n.
         */
        if (   (fPrevXcpt & (IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_T_EXT_INT | IEM_XCPT_FLAGS_ICEBP_INSTR))
            && (fFlags & IEM_XCPT_FLAGS_ERR)
            && u8Vector != X86_XCPT_PF
            && u8Vector != X86_XCPT_DF)
        {
            uErr |= X86_TRAP_ERR_EXTERNAL;
        }
    }

    ICORE(pVCpu).cXcptRecursions++;
    ICORE(pVCpu).uCurXcpt    = u8Vector;
    ICORE(pVCpu).fCurXcpt    = fFlags;
    ICORE(pVCpu).uCurXcptErr = uErr;
    ICORE(pVCpu).uCurXcptCr2 = uCr2;

    /*
     * Extensive logging.
     */
#if defined(LOG_ENABLED) && defined(IN_RING3)
    if (LogIs3Enabled())
    {
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR_MASK);
        char    szRegs[4096];
        DBGFR3RegPrintf(pVM->pUVM, pVCpu->idCpu, &szRegs[0], sizeof(szRegs),
                        "rax=%016VR{rax} rbx=%016VR{rbx} rcx=%016VR{rcx} rdx=%016VR{rdx}\n"
                        "rsi=%016VR{rsi} rdi=%016VR{rdi} r8 =%016VR{r8} r9 =%016VR{r9}\n"
                        "r10=%016VR{r10} r11=%016VR{r11} r12=%016VR{r12} r13=%016VR{r13}\n"
                        "r14=%016VR{r14} r15=%016VR{r15} %VRF{rflags}\n"
                        "rip=%016VR{rip} rsp=%016VR{rsp} rbp=%016VR{rbp}\n"
                        "cs={%04VR{cs} base=%016VR{cs_base} limit=%08VR{cs_lim} flags=%04VR{cs_attr}} cr0=%016VR{cr0}\n"
                        "ds={%04VR{ds} base=%016VR{ds_base} limit=%08VR{ds_lim} flags=%04VR{ds_attr}} cr2=%016VR{cr2}\n"
                        "es={%04VR{es} base=%016VR{es_base} limit=%08VR{es_lim} flags=%04VR{es_attr}} cr3=%016VR{cr3}\n"
                        "fs={%04VR{fs} base=%016VR{fs_base} limit=%08VR{fs_lim} flags=%04VR{fs_attr}} cr4=%016VR{cr4}\n"
                        "gs={%04VR{gs} base=%016VR{gs_base} limit=%08VR{gs_lim} flags=%04VR{gs_attr}} cr8=%016VR{cr8}\n"
                        "ss={%04VR{ss} base=%016VR{ss_base} limit=%08VR{ss_lim} flags=%04VR{ss_attr}}\n"
                        "dr0=%016VR{dr0} dr1=%016VR{dr1} dr2=%016VR{dr2} dr3=%016VR{dr3}\n"
                        "dr6=%016VR{dr6} dr7=%016VR{dr7}\n"
                        "gdtr=%016VR{gdtr_base}:%04VR{gdtr_lim}  idtr=%016VR{idtr_base}:%04VR{idtr_lim}  rflags=%08VR{rflags}\n"
                        "ldtr={%04VR{ldtr} base=%016VR{ldtr_base} limit=%08VR{ldtr_lim} flags=%08VR{ldtr_attr}}\n"
                        "tr  ={%04VR{tr} base=%016VR{tr_base} limit=%08VR{tr_lim} flags=%08VR{tr_attr}}\n"
                        "    sysenter={cs=%04VR{sysenter_cs} eip=%08VR{sysenter_eip} esp=%08VR{sysenter_esp}}\n"
                        "        efer=%016VR{efer}\n"
                        "         pat=%016VR{pat}\n"
                        "     sf_mask=%016VR{sf_mask}\n"
                        "krnl_gs_base=%016VR{krnl_gs_base}\n"
                        "       lstar=%016VR{lstar}\n"
                        "        star=%016VR{star} cstar=%016VR{cstar}\n"
                        "fcw=%04VR{fcw} fsw=%04VR{fsw} ftw=%04VR{ftw} mxcsr=%04VR{mxcsr} mxcsr_mask=%04VR{mxcsr_mask}\n"
                        );

        char szInstr[256];
        DBGFR3DisasInstrEx(pVM->pUVM, pVCpu->idCpu, 0, 0,
                           DBGF_DISAS_FLAGS_CURRENT_GUEST | DBGF_DISAS_FLAGS_DEFAULT_MODE,
                           szInstr, sizeof(szInstr), NULL);
        Log3(("%s%s\n", szRegs, szInstr));
    }
#endif /* LOG_ENABLED */

    /*
     * Stats.
     */
    uint64_t const uTimestamp = ASMReadTSC();
    if (!(fFlags & IEM_XCPT_FLAGS_T_CPU_XCPT))
    {
        STAM_REL_STATS({ pVCpu->iem.s.aStatInts[u8Vector] += 1; });
        EMHistoryAddExit(pVCpu,
                           fFlags & IEM_XCPT_FLAGS_T_EXT_INT
                         ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_IEM, u8Vector)
                         : EMEXIT_MAKE_FT(EMEXIT_F_KIND_IEM, u8Vector | 0x100),
                         pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base, uTimestamp);
        IEMTLBTRACE_IRQ(pVCpu, u8Vector, fFlags, pVCpu->cpum.GstCtx.rflags.uBoth);
    }
    else
    {
        if (u8Vector < RT_ELEMENTS(pVCpu->iem.s.aStatXcpts))
            STAM_REL_COUNTER_INC(&pVCpu->iem.s.aStatXcpts[u8Vector]);
        EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT_EX(EMEXIT_F_KIND_XCPT, u8Vector,
                                                  fFlags & IEM_XCPT_FLAGS_ERR ? uErr | RT_BIT_32(31) : 0),
                         pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base, uTimestamp);
        if (fFlags & IEM_XCPT_FLAGS_CR2)
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_XCPT, u8Vector | EMEXIT_F_XCPT_CR2), uCr2, uTimestamp);
        IEMTLBTRACE_XCPT(pVCpu, u8Vector, fFlags & IEM_XCPT_FLAGS_ERR ? uErr : 0, fFlags & IEM_XCPT_FLAGS_CR2 ? uCr2 : 0, fFlags);
    }

    /*
     * Hack alert! Convert incoming debug events to slient on Intel.
     * See the dbg+inhibit+ringxfer test in bs3-cpu-weird-1.
     */
    if (   !(fFlags & IEM_XCPT_FLAGS_T_SOFT_INT)
        || !(pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK_NONSILENT)
        || !IEM_IS_GUEST_CPU_INTEL(pVCpu))
    { /* ignore */ }
    else
    {
        Log(("iemRaiseXcptOrInt: Converting pending %#x debug events to a silent one (intel hack); vec=%#x\n",
             pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK, u8Vector));
        pVCpu->cpum.GstCtx.eflags.uBoth = (pVCpu->cpum.GstCtx.eflags.uBoth & ~CPUMCTX_DBG_HIT_DRX_MASK)
                                        | CPUMCTX_DBG_HIT_DRX_SILENT;
    }

    /*
     * #PF's implies a INVLPG for the CR2 value (see 4.10.1.1 in Intel SDM Vol 3)
     * to ensure that a stale TLB or paging cache entry will only cause one
     * spurious #PF.
     */
    if (    u8Vector == X86_XCPT_PF
        && (fFlags & (IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_CR2)) == (IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_CR2))
        IEMTlbInvalidatePage(pVCpu, uCr2);

    /*
     * Call the mode specific worker function.
     */
    VBOXSTRICTRC    rcStrict;
    if (!(pVCpu->cpum.GstCtx.cr0 & X86_CR0_PE))
        rcStrict = iemRaiseXcptOrIntInRealMode(pVCpu, cbInstr, u8Vector, fFlags, uErr, uCr2);
    else if (pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_LMA)
        rcStrict = iemRaiseXcptOrIntInLongMode(pVCpu, cbInstr, u8Vector, fFlags, uErr, uCr2);
    else
        rcStrict = iemRaiseXcptOrIntInProtMode(pVCpu, cbInstr, u8Vector, fFlags, uErr, uCr2);

    /* Flush the prefetch buffer. */
    iemOpcodeFlushHeavy(pVCpu, IEM_GET_INSTR_LEN(pVCpu));

    /*
     * Unwind.
     */
    ICORE(pVCpu).cXcptRecursions--;
    ICORE(pVCpu).uCurXcpt = uPrevXcpt;
    ICORE(pVCpu).fCurXcpt = fPrevXcpt;
    Log(("iemRaiseXcptOrInt: returns %Rrc (vec=%#x); cs:rip=%04x:%RGv ss:rsp=%04x:%RGv cpl=%u depth=%d\n",
         VBOXSTRICTRC_VAL(rcStrict), u8Vector, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.ss.Sel,
         pVCpu->cpum.GstCtx.esp, IEM_GET_CPL(pVCpu), ICORE(pVCpu).cXcptRecursions + 1));
    return rcStrict;
}

/**
 * See iemRaiseXcptOrInt.  Will not return.
 */
DECL_NO_RETURN(void)
iemRaiseXcptOrIntJmp(PVMCPUCC    pVCpu,
                     uint8_t     cbInstr,
                     uint8_t     u8Vector,
                     uint32_t    fFlags,
                     uint16_t    uErr,
                     uint64_t    uCr2) IEM_NOEXCEPT_MAY_LONGJMP
{
    VBOXSTRICTRC rcStrict = iemRaiseXcptOrInt(pVCpu, cbInstr, u8Vector, fFlags, uErr, uCr2);
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
}


/** \#DE - 00.  */
VBOXSTRICTRC iemRaiseDivideError(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    if (GCMIsInterceptingXcptDE(pVCpu))
    {
        int rc = GCMXcptDE(pVCpu, &pVCpu->cpum.GstCtx);
        if (rc == VINF_SUCCESS)
        {
            Log(("iemRaiseDivideError: Restarting instruction because of GCMXcptDE\n"));
            return VINF_IEM_RAISED_XCPT; /* must return non-zero status here to cause a instruction restart */
        }
    }
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_DE, IEM_XCPT_FLAGS_T_CPU_XCPT, 0, 0);
}


/** \#DE - 00.  */
DECL_NO_RETURN(void) iemRaiseDivideErrorJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
    iemRaiseXcptOrIntJmp(pVCpu, 0, X86_XCPT_DE, IEM_XCPT_FLAGS_T_CPU_XCPT, 0, 0);
}


/** \#DB - 01.
 * @note This automatically clear DR7.GD.  */
VBOXSTRICTRC iemRaiseDebugException(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /* This always clears RF (via IEM_XCPT_FLAGS_DRx_INSTR_BP). */
    pVCpu->cpum.GstCtx.dr[7] &= ~X86_DR7_GD;
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_DB, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_DRx_INSTR_BP, 0, 0);
}


/** \#BR - 05.  */
VBOXSTRICTRC iemRaiseBoundRangeExceeded(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_BR, IEM_XCPT_FLAGS_T_CPU_XCPT, 0, 0);
}


/** \#UD - 06.  */
VBOXSTRICTRC iemRaiseUndefinedOpcode(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_UD, IEM_XCPT_FLAGS_T_CPU_XCPT, 0, 0);
}


/** \#UD - 06.  */
DECL_NO_RETURN(void) iemRaiseUndefinedOpcodeJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
    iemRaiseXcptOrIntJmp(pVCpu, 0, X86_XCPT_UD, IEM_XCPT_FLAGS_T_CPU_XCPT, 0, 0);
}


/** \#NM - 07.  */
VBOXSTRICTRC iemRaiseDeviceNotAvailable(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_NM, IEM_XCPT_FLAGS_T_CPU_XCPT, 0, 0);
}


/** \#NM - 07.  */
DECL_NO_RETURN(void) iemRaiseDeviceNotAvailableJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
    iemRaiseXcptOrIntJmp(pVCpu, 0, X86_XCPT_NM, IEM_XCPT_FLAGS_T_CPU_XCPT, 0, 0);
}


/** \#TS(err) - 0a.  */
VBOXSTRICTRC iemRaiseTaskSwitchFaultWithErr(PVMCPUCC pVCpu, uint16_t uErr) RT_NOEXCEPT
{
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_TS, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, uErr, 0);
}


/** \#TS(tr) - 0a.  */
VBOXSTRICTRC iemRaiseTaskSwitchFaultCurrentTSS(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_TS, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR,
                              pVCpu->cpum.GstCtx.tr.Sel, 0);
}


/** \#TS(0) - 0a.  */
VBOXSTRICTRC iemRaiseTaskSwitchFault0(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_TS, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR,
                             0, 0);
}


/** \#TS(err) - 0a.  */
VBOXSTRICTRC iemRaiseTaskSwitchFaultBySelector(PVMCPUCC pVCpu, uint16_t uSel) RT_NOEXCEPT
{
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_TS, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR,
                             uSel & X86_SEL_MASK_OFF_RPL, 0);
}


/** \#NP(err) - 0b.  */
VBOXSTRICTRC iemRaiseSelectorNotPresentWithErr(PVMCPUCC pVCpu, uint16_t uErr) RT_NOEXCEPT
{
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_NP, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, uErr, 0);
}


/** \#NP(sel) - 0b.  */
VBOXSTRICTRC iemRaiseSelectorNotPresentBySelector(PVMCPUCC pVCpu, uint16_t uSel) RT_NOEXCEPT
{
    Log(("iemRaiseSelectorNotPresentBySelector: cs:rip=%04x:%RX64 uSel=%#x\n",
         pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, uSel));
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_NP, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR,
                             uSel & ~X86_SEL_RPL, 0);
}


/** \#SS(seg) - 0c.  */
VBOXSTRICTRC iemRaiseStackSelectorNotPresentBySelector(PVMCPUCC pVCpu, uint16_t uSel) RT_NOEXCEPT
{
    Log(("iemRaiseStackSelectorNotPresentBySelector: cs:rip=%04x:%RX64 uSel=%#x\n",
         pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, uSel));
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_SS, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR,
                             uSel & ~X86_SEL_RPL, 0);
}


/** \#SS(err) - 0c.  */
VBOXSTRICTRC iemRaiseStackSelectorNotPresentWithErr(PVMCPUCC pVCpu, uint16_t uErr) RT_NOEXCEPT
{
    Log(("iemRaiseStackSelectorNotPresentWithErr: cs:rip=%04x:%RX64 uErr=%#x\n",
         pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, uErr));
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_SS, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, uErr, 0);
}


/** \#GP(n) - 0d.  */
VBOXSTRICTRC iemRaiseGeneralProtectionFault(PVMCPUCC pVCpu, uint16_t uErr) RT_NOEXCEPT
{
    Log(("iemRaiseGeneralProtectionFault: cs:rip=%04x:%RX64 uErr=%#x\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, uErr));
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_GP, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, uErr, 0);
}


/** \#GP(0) - 0d.  */
VBOXSTRICTRC iemRaiseGeneralProtectionFault0(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    Log(("iemRaiseGeneralProtectionFault0: cs:rip=%04x:%RX64\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_GP, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, 0, 0);
}


/** \#GP(0) - 0d.  */
DECL_NO_RETURN(void) iemRaiseGeneralProtectionFault0Jmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
    Log(("iemRaiseGeneralProtectionFault0Jmp: cs:rip=%04x:%RX64\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
    iemRaiseXcptOrIntJmp(pVCpu, 0, X86_XCPT_GP, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, 0, 0);
}


/** \#GP(sel) - 0d.  */
VBOXSTRICTRC iemRaiseGeneralProtectionFaultBySelector(PVMCPUCC pVCpu, RTSEL Sel) RT_NOEXCEPT
{
    Log(("iemRaiseGeneralProtectionFaultBySelector: cs:rip=%04x:%RX64 Sel=%#x\n",
         pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, Sel));
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_GP, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR,
                             Sel & ~X86_SEL_RPL, 0);
}


/** \#GP(0) - 0d.  */
VBOXSTRICTRC iemRaiseNotCanonical(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    Log(("iemRaiseNotCanonical: cs:rip=%04x:%RX64\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_GP, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, 0, 0);
}


/** \#GP(sel) - 0d.  */
VBOXSTRICTRC iemRaiseSelectorBounds(PVMCPUCC pVCpu, uint32_t iSegReg, uint32_t fAccess) RT_NOEXCEPT
{
    Log(("iemRaiseSelectorBounds: cs:rip=%04x:%RX64 iSegReg=%d fAccess=%#x\n",
         pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, iSegReg, fAccess));
    NOREF(iSegReg); NOREF(fAccess);
    return iemRaiseXcptOrInt(pVCpu, 0, iSegReg == X86_SREG_SS ? X86_XCPT_SS : X86_XCPT_GP,
                             IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, 0, 0);
}


/** \#GP(sel) - 0d, longjmp.  */
DECL_NO_RETURN(void) iemRaiseSelectorBoundsJmp(PVMCPUCC pVCpu, uint32_t iSegReg, uint32_t fAccess) IEM_NOEXCEPT_MAY_LONGJMP
{
    Log(("iemRaiseSelectorBoundsJmp: cs:rip=%04x:%RX64 iSegReg=%d fAccess=%#x\n",
         pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, iSegReg, fAccess));
    NOREF(iSegReg); NOREF(fAccess);
    iemRaiseXcptOrIntJmp(pVCpu, 0, iSegReg == X86_SREG_SS ? X86_XCPT_SS : X86_XCPT_GP,
                         IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, 0, 0);
}


/** \#GP(sel) - 0d.  */
VBOXSTRICTRC iemRaiseSelectorBoundsBySelector(PVMCPUCC pVCpu, RTSEL Sel) RT_NOEXCEPT
{
    Log(("iemRaiseSelectorBoundsBySelector: cs:rip=%04x:%RX64 Sel=%#x\n",
         pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, Sel));
    NOREF(Sel);
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_GP, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, 0, 0);
}


/** \#GP(sel) - 0d, longjmp.  */
DECL_NO_RETURN(void) iemRaiseSelectorBoundsBySelectorJmp(PVMCPUCC pVCpu, RTSEL Sel) IEM_NOEXCEPT_MAY_LONGJMP
{
    Log(("iemRaiseSelectorBoundsBySelectorJmp: cs:rip=%04x:%RX64 Sel=%#x\n",
         pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, Sel));
    NOREF(Sel);
    iemRaiseXcptOrIntJmp(pVCpu, 0, X86_XCPT_GP, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, 0, 0);
}


/** \#GP(sel) - 0d.  */
VBOXSTRICTRC iemRaiseSelectorInvalidAccess(PVMCPUCC pVCpu, uint32_t iSegReg, uint32_t fAccess) RT_NOEXCEPT
{
    Log(("iemRaiseSelectorInvalidAccess: cs:rip=%04x:%RX64 iSegReg=%d fAccess=%#x\n",
         pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, iSegReg, fAccess));
    NOREF(iSegReg); NOREF(fAccess);
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_GP, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, 0, 0);
}


/** \#GP(sel) - 0d, longjmp.  */
DECL_NO_RETURN(void) iemRaiseSelectorInvalidAccessJmp(PVMCPUCC pVCpu, uint32_t iSegReg, uint32_t fAccess) IEM_NOEXCEPT_MAY_LONGJMP
{
    NOREF(iSegReg); NOREF(fAccess);
    iemRaiseXcptOrIntJmp(pVCpu, 0, X86_XCPT_GP, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, 0, 0);
}


/** \#PF(n) - 0e.  */
VBOXSTRICTRC iemRaisePageFault(PVMCPUCC pVCpu, RTGCPTR GCPtrWhere, uint32_t cbAccess, uint32_t fAccess, int rc) RT_NOEXCEPT
{
    uint16_t uErr;
    switch (rc)
    {
        case VERR_PAGE_NOT_PRESENT:
        case VERR_PAGE_TABLE_NOT_PRESENT:
        case VERR_PAGE_DIRECTORY_PTR_NOT_PRESENT:
        case VERR_PAGE_MAP_LEVEL4_NOT_PRESENT:
            uErr = 0;
            break;

        case VERR_RESERVED_PAGE_TABLE_BITS:
            uErr = X86_TRAP_PF_P | X86_TRAP_PF_RSVD;
            break;

        default:
            AssertMsgFailed(("%Rrc\n", rc));
            RT_FALL_THRU();
        case VERR_ACCESS_DENIED:
            uErr = X86_TRAP_PF_P;
            break;
    }

    if (IEM_GET_CPL(pVCpu) == 3)
        uErr |= X86_TRAP_PF_US;

    if (   (fAccess & IEM_ACCESS_WHAT_MASK) == IEM_ACCESS_WHAT_CODE
        && (   (pVCpu->cpum.GstCtx.cr4 & X86_CR4_PAE)
            && (pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_NXE) ) )
        uErr |= X86_TRAP_PF_ID;

#if 0 /* This is so much non-sense, really.  Why was it done like that? */
    /* Note! RW access callers reporting a WRITE protection fault, will clear
             the READ flag before calling.  So, read-modify-write accesses (RW)
             can safely be reported as READ faults. */
    if ((fAccess & (IEM_ACCESS_TYPE_WRITE | IEM_ACCESS_TYPE_READ)) == IEM_ACCESS_TYPE_WRITE)
        uErr |= X86_TRAP_PF_RW;
#else
    if (fAccess & IEM_ACCESS_TYPE_WRITE)
    {
        /// @todo r=bird: bs3-cpu-basic-2 wants X86_TRAP_PF_RW for xchg and cmpxchg
        /// (regardless of outcome of the comparison in the latter case).
        //if (!(fAccess & IEM_ACCESS_TYPE_READ))
            uErr |= X86_TRAP_PF_RW;
    }
#endif

    /* For FXSAVE and FRSTOR the #PF is typically reported at the max address
       of the memory operand rather than at the start of it. (Not sure what
       happens if it crosses a page boundrary.)  The current heuristics for
       this is to report the #PF for the last byte if the access is more than
       64 bytes. This is probably not correct, but we can work that out later,
       main objective now is to get FXSAVE to work like for real hardware and
       make bs3-cpu-basic2 work. */
    if (cbAccess <= 64)
    { /* likely*/ }
    else
        GCPtrWhere += cbAccess - 1;

    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_PF, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR | IEM_XCPT_FLAGS_CR2,
                             uErr, GCPtrWhere);
}


/** \#PF(n) - 0e, longjmp.  */
DECL_NO_RETURN(void) iemRaisePageFaultJmp(PVMCPUCC pVCpu, RTGCPTR GCPtrWhere, uint32_t cbAccess,
                                          uint32_t fAccess, int rc) IEM_NOEXCEPT_MAY_LONGJMP
{
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(iemRaisePageFault(pVCpu, GCPtrWhere, cbAccess, fAccess, rc)));
}


/** \#MF(0) - 10.  */
VBOXSTRICTRC iemRaiseMathFault(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    if (pVCpu->cpum.GstCtx.cr0 & X86_CR0_NE)
        return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_MF, IEM_XCPT_FLAGS_T_CPU_XCPT, 0, 0);

    /* Convert a #MF into a FERR -> IRQ 13. See @bugref{6117}. */
    PDMIsaSetIrq(pVCpu->CTX_SUFF(pVM), 13 /* u8Irq */, 1 /* u8Level */, 0 /* uTagSrc */);
    return iemRegUpdateRipAndFinishClearingRF(pVCpu);
}


/** \#MF(0) - 10, longjmp.  */
DECL_NO_RETURN(void) iemRaiseMathFaultJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(iemRaiseMathFault(pVCpu)));
}


/** \#AC(0) - 11.  */
VBOXSTRICTRC iemRaiseAlignmentCheckException(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_AC, IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_ERR, 0, 0);
}


/** \#AC(0) - 11, longjmp.  */
DECL_NO_RETURN(void) iemRaiseAlignmentCheckExceptionJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(iemRaiseAlignmentCheckException(pVCpu)));
}


/** \#XF(0)/\#XM(0) - 19.   */
VBOXSTRICTRC iemRaiseSimdFpException(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_XF, IEM_XCPT_FLAGS_T_CPU_XCPT, 0, 0);
}


/** \#XF(0)/\#XM(0) - 19s, longjmp.  */
DECL_NO_RETURN(void) iemRaiseSimdFpExceptionJmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
    IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(iemRaiseSimdFpException(pVCpu)));
}


/** Accessed via IEMOP_RAISE_DIVIDE_ERROR.   */
IEM_CIMPL_DEF_0(iemCImplRaiseDivideError)
{
    NOREF(cbInstr);
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_DE, IEM_XCPT_FLAGS_T_CPU_XCPT, 0, 0);
}


/** Accessed via IEMOP_RAISE_INVALID_LOCK_PREFIX. */
IEM_CIMPL_DEF_0(iemCImplRaiseInvalidLockPrefix)
{
    NOREF(cbInstr);
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_UD, IEM_XCPT_FLAGS_T_CPU_XCPT, 0, 0);
}


/** Accessed via IEMOP_RAISE_INVALID_OPCODE. */
IEM_CIMPL_DEF_0(iemCImplRaiseInvalidOpcode)
{
    NOREF(cbInstr);
    return iemRaiseXcptOrInt(pVCpu, 0, X86_XCPT_UD, IEM_XCPT_FLAGS_T_CPU_XCPT, 0, 0);
}


/**
 * Checks if IEM is in the process of delivering an event (interrupt or
 * exception).
 *
 * @returns true if we're in the process of raising an interrupt or exception,
 *          false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   puVector        Where to store the vector associated with the
 *                          currently delivered event, optional.
 * @param   pfFlags         Where to store th event delivery flags (see
 *                          IEM_XCPT_FLAGS_XXX), optional.
 * @param   puErr           Where to store the error code associated with the
 *                          event, optional.
 * @param   puCr2           Where to store the CR2 associated with the event,
 *                          optional.
 * @remarks The caller should check the flags to determine if the error code and
 *          CR2 are valid for the event.
 */
VMM_INT_DECL(bool) IEMGetCurrentXcpt(PVMCPUCC pVCpu, uint8_t *puVector, uint32_t *pfFlags, uint32_t *puErr, uint64_t *puCr2)
{
    bool const fRaisingXcpt = ICORE(pVCpu).cXcptRecursions > 0;
    if (fRaisingXcpt)
    {
        if (puVector)
            *puVector = ICORE(pVCpu).uCurXcpt;
        if (pfFlags)
            *pfFlags = ICORE(pVCpu).fCurXcpt;
        if (puErr)
            *puErr = ICORE(pVCpu).uCurXcptErr;
        if (puCr2)
            *puCr2 = ICORE(pVCpu).uCurXcptCr2;
    }
    return fRaisingXcpt;
}

/** @}  */

