/* $Id: IEMAll-x86.cpp 112403 2026-01-11 19:29:08Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - x86 target, miscellaneous.
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
#include <VBox/vmm/dbgf.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/param.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/x86.h>

#include "IEMInline-x86.h" /* iemRegFinishClearingRF */


/**
 * Calculates IEM_F_BRK_PENDING_XXX (IEM_F_PENDING_BRK_MASK) flags, slow code
 * path.
 *
 * This will also invalidate TLB entries for any pages with active data
 * breakpoints on them.
 *
 * @returns IEM_F_BRK_PENDING_XXX or zero.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 *
 * @note    Don't call directly, use iemCalcExecDbgFlags instead.
 */
uint32_t iemCalcExecDbgFlagsSlow(PVMCPUCC pVCpu)
{
    uint32_t fExec = 0;

    /*
     * Helper for invalidate the data TLB for breakpoint addresses.
     *
     * This is to make sure any access to the page will always trigger a TLB
     * load for as long as the breakpoint is enabled.
     */
#ifdef IEM_WITH_DATA_TLB_IN_CUR_CTX
# define INVALID_TLB_ENTRY_FOR_BP(a_uValue) do { \
        RTGCPTR uTagNoRev = (a_uValue); \
        uTagNoRev = IEMTLB_CALC_TAG_NO_REV(pVCpu, uTagNoRev); \
        /** @todo do large page accounting */ \
        uintptr_t const idxEven = IEMTLB_TAG_TO_EVEN_INDEX(uTagNoRev); \
        if (ITLBS(pVCpu).Data.aEntries[idxEven].uTag == (uTagNoRev | ITLBS(pVCpu).Data.uTlbRevision)) \
            ITLBS(pVCpu).Data.aEntries[idxEven].uTag = 0; \
        if (ITLBS(pVCpu).Data.aEntries[idxEven + 1].uTag == (uTagNoRev | ITLBS(pVCpu).Data.uTlbRevisionGlobal)) \
            ITLBS(pVCpu).Data.aEntries[idxEven + 1].uTag = 0; \
    } while (0)
#else
# define INVALID_TLB_ENTRY_FOR_BP(a_uValue) do { } while (0)
#endif

    /*
     * Process guest breakpoints.
     */
#define PROCESS_ONE_BP(a_fDr7, a_iBp, a_uValue) do { \
        if (a_fDr7 & X86_DR7_L_G(a_iBp)) \
        { \
            switch (X86_DR7_GET_RW(a_fDr7, a_iBp)) \
            { \
                case X86_DR7_RW_EO: \
                    fExec |= IEM_F_PENDING_BRK_INSTR; \
                    break; \
                case X86_DR7_RW_WO: \
                case X86_DR7_RW_RW: \
                    fExec |= IEM_F_PENDING_BRK_DATA; \
                    INVALID_TLB_ENTRY_FOR_BP(a_uValue); \
                    break; \
                case X86_DR7_RW_IO: \
                    fExec |= IEM_F_PENDING_BRK_X86_IO; \
                    break; \
            } \
        } \
    } while (0)

    uint32_t const fGstDr7 = (uint32_t)pVCpu->cpum.GstCtx.dr[7];
    if (fGstDr7 & X86_DR7_ENABLED_MASK)
    {
/** @todo extract more details here to simplify matching later. */
#ifdef IEM_WITH_DATA_TLB_IN_CUR_CTX
        IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_DR0_DR3);
#endif
        PROCESS_ONE_BP(fGstDr7, 0, pVCpu->cpum.GstCtx.dr[0]);
        PROCESS_ONE_BP(fGstDr7, 1, pVCpu->cpum.GstCtx.dr[1]);
        PROCESS_ONE_BP(fGstDr7, 2, pVCpu->cpum.GstCtx.dr[2]);
        PROCESS_ONE_BP(fGstDr7, 3, pVCpu->cpum.GstCtx.dr[3]);
    }

    /*
     * Process hypervisor breakpoints.
     */
    PVMCC const    pVM       = pVCpu->CTX_SUFF(pVM);
    uint32_t const fHyperDr7 = DBGFBpGetDR7(pVM);
    if (fHyperDr7 & X86_DR7_ENABLED_MASK)
    {
/** @todo extract more details here to simplify matching later. */
        PROCESS_ONE_BP(fHyperDr7, 0, DBGFBpGetDR0(pVM));
        PROCESS_ONE_BP(fHyperDr7, 1, DBGFBpGetDR1(pVM));
        PROCESS_ONE_BP(fHyperDr7, 2, DBGFBpGetDR2(pVM));
        PROCESS_ONE_BP(fHyperDr7, 3, DBGFBpGetDR3(pVM));
    }

    return fExec;
}


/** @name   Register Access.
 * @{
 */

/**
 * Adds a 8-bit signed jump offset to RIP/EIP/IP.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   enmEffOpSize        Effective operand size.
 */
VBOXSTRICTRC iemRegRipRelativeJumpS8AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int8_t offNextInstr,
                                                        IEMMODE enmEffOpSize) RT_NOEXCEPT
{
    switch (enmEffOpSize)
    {
        case IEMMODE_16BIT:
        {
            uint16_t const uNewIp = pVCpu->cpum.GstCtx.ip + cbInstr + (int16_t)offNextInstr;
            if (RT_LIKELY(   uNewIp <= pVCpu->cpum.GstCtx.cs.u32Limit
                          || IEM_IS_64BIT_CODE(pVCpu) /* no CS limit checks in 64-bit mode */))
                pVCpu->cpum.GstCtx.rip = uNewIp;
            else
                return iemRaiseGeneralProtectionFault0(pVCpu);
            break;
        }

        case IEMMODE_32BIT:
        {
            Assert(!IEM_IS_64BIT_CODE(pVCpu));
            Assert(pVCpu->cpum.GstCtx.rip <= UINT32_MAX);

            uint32_t const uNewEip = pVCpu->cpum.GstCtx.eip + cbInstr + (int32_t)offNextInstr;
            if (RT_LIKELY(uNewEip <= pVCpu->cpum.GstCtx.cs.u32Limit))
                pVCpu->cpum.GstCtx.rip = uNewEip;
            else
                return iemRaiseGeneralProtectionFault0(pVCpu);
            break;
        }

        case IEMMODE_64BIT:
        {
            Assert(IEM_IS_64BIT_CODE(pVCpu));

            uint64_t const uNewRip = pVCpu->cpum.GstCtx.rip + cbInstr + (int64_t)offNextInstr;
            if (RT_LIKELY(IEM_IS_CANONICAL(uNewRip)))
                pVCpu->cpum.GstCtx.rip = uNewRip;
            else
                return iemRaiseGeneralProtectionFault0(pVCpu);
            break;
        }

        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }

#ifndef IEM_WITH_CODE_TLB_IN_CUR_CTX
    /* Flush the prefetch buffer. */
    ICORE(pVCpu).cbOpcode = cbInstr;
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Adds a 16-bit signed jump offset to RIP/EIP/IP.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 */
VBOXSTRICTRC iemRegRipRelativeJumpS16AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int16_t offNextInstr) RT_NOEXCEPT
{
    Assert(ICORE(pVCpu).enmEffOpSize == IEMMODE_16BIT);

    uint16_t const uNewIp = pVCpu->cpum.GstCtx.ip + cbInstr + offNextInstr;
    if (RT_LIKELY(   uNewIp <= pVCpu->cpum.GstCtx.cs.u32Limit
                  || IEM_IS_64BIT_CODE(pVCpu) /* no limit checking in 64-bit mode */))
        pVCpu->cpum.GstCtx.rip = uNewIp;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

#ifndef IEM_WITH_CODE_TLB_IN_CUR_CTX
    /* Flush the prefetch buffer. */
    ICORE(pVCpu).cbOpcode = IEM_GET_INSTR_LEN(pVCpu);
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Adds a 32-bit signed jump offset to RIP/EIP/IP.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   enmEffOpSize        Effective operand size.
 */
VBOXSTRICTRC iemRegRipRelativeJumpS32AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int32_t offNextInstr,
                                                         IEMMODE enmEffOpSize) RT_NOEXCEPT
{
    if (enmEffOpSize == IEMMODE_32BIT)
    {
        Assert(pVCpu->cpum.GstCtx.rip <= UINT32_MAX); Assert(!IEM_IS_64BIT_CODE(pVCpu));

        uint32_t const uNewEip = pVCpu->cpum.GstCtx.eip + cbInstr + offNextInstr;
        if (RT_LIKELY(uNewEip <= pVCpu->cpum.GstCtx.cs.u32Limit))
            pVCpu->cpum.GstCtx.rip = uNewEip;
        else
            return iemRaiseGeneralProtectionFault0(pVCpu);
    }
    else
    {
        Assert(enmEffOpSize == IEMMODE_64BIT);

        uint64_t const uNewRip = pVCpu->cpum.GstCtx.rip + cbInstr + (int64_t)offNextInstr;
        if (RT_LIKELY(IEM_IS_CANONICAL(uNewRip)))
            pVCpu->cpum.GstCtx.rip = uNewRip;
        else
            return iemRaiseGeneralProtectionFault0(pVCpu);
    }

#ifndef IEM_WITH_CODE_TLB_IN_CUR_CTX
    /* Flush the prefetch buffer. */
    ICORE(pVCpu).cbOpcode = IEM_GET_INSTR_LEN(pVCpu);
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}

/** @}  */

