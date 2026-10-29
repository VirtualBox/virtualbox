/* $Id: IEMAllExec-x86.cpp 112403 2026-01-11 19:29:08Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - x86 target, decoded instruction execution.
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
#include <VBox/vmm/iom.h>
#include <VBox/vmm/gcm.h>
#include <VBox/vmm/gim.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/x86.h>

#include "IEMInline.h"
#include "IEMInline-x86.h"
#include "IEMInlineExec.h"
#include "IEMInlineExec-x86.h"


/**
 * Interface for HM and EM for executing string I/O OUT (write) instructions.
 *
 * This API ASSUMES that the caller has already verified that the guest code is
 * allowed to access the I/O port.  (The I/O port is in the DX register in the
 * guest state.)
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbValue             The size of the I/O port access (1, 2, or 4).
 * @param   enmAddrMode         The addressing mode.
 * @param   fRepPrefix          Indicates whether a repeat prefix is used
 *                              (doesn't matter which for this instruction).
 * @param   cbInstr             The instruction length in bytes.
 * @param   iEffSeg             The effective segment address.
 * @param   fIoChecked          Whether the access to the I/O port has been
 *                              checked or not.  It's typically checked in the
 *                              HM scenario.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecStringIoWrite(PVMCPUCC pVCpu, uint8_t cbValue, IEMMODE enmAddrMode,
                                                bool fRepPrefix, uint8_t cbInstr, uint8_t iEffSeg, bool fIoChecked)
{
    AssertMsgReturn(iEffSeg < X86_SREG_COUNT, ("%#x\n", iEffSeg), VERR_IEM_INVALID_EFF_SEG);
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 1);

    /*
     * State init.
     */
    iemInitExec(pVCpu, 0 /*fExecOpts*/);

    /*
     * Switch orgy for getting to the right handler.
     */
    VBOXSTRICTRC rcStrict;
    if (fRepPrefix)
    {
        switch (enmAddrMode)
        {
            case IEMMODE_16BIT:
                switch (cbValue)
                {
                    case 1: rcStrict = iemCImpl_rep_outs_op8_addr16(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    case 2: rcStrict = iemCImpl_rep_outs_op16_addr16(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    case 4: rcStrict = iemCImpl_rep_outs_op32_addr16(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    default:
                        AssertMsgFailedReturn(("cbValue=%#x\n", cbValue), VERR_IEM_INVALID_OPERAND_SIZE);
                }
                break;

            case IEMMODE_32BIT:
                switch (cbValue)
                {
                    case 1: rcStrict = iemCImpl_rep_outs_op8_addr32(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    case 2: rcStrict = iemCImpl_rep_outs_op16_addr32(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    case 4: rcStrict = iemCImpl_rep_outs_op32_addr32(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    default:
                        AssertMsgFailedReturn(("cbValue=%#x\n", cbValue), VERR_IEM_INVALID_OPERAND_SIZE);
                }
                break;

            case IEMMODE_64BIT:
                switch (cbValue)
                {
                    case 1: rcStrict = iemCImpl_rep_outs_op8_addr64(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    case 2: rcStrict = iemCImpl_rep_outs_op16_addr64(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    case 4: rcStrict = iemCImpl_rep_outs_op32_addr64(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    default:
                        AssertMsgFailedReturn(("cbValue=%#x\n", cbValue), VERR_IEM_INVALID_OPERAND_SIZE);
                }
                break;

            default:
                AssertMsgFailedReturn(("enmAddrMode=%d\n", enmAddrMode), VERR_IEM_INVALID_ADDRESS_MODE);
        }
    }
    else
    {
        switch (enmAddrMode)
        {
            case IEMMODE_16BIT:
                switch (cbValue)
                {
                    case 1: rcStrict = iemCImpl_outs_op8_addr16(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    case 2: rcStrict = iemCImpl_outs_op16_addr16(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    case 4: rcStrict = iemCImpl_outs_op32_addr16(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    default:
                        AssertMsgFailedReturn(("cbValue=%#x\n", cbValue), VERR_IEM_INVALID_OPERAND_SIZE);
                }
                break;

            case IEMMODE_32BIT:
                switch (cbValue)
                {
                    case 1: rcStrict = iemCImpl_outs_op8_addr32(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    case 2: rcStrict = iemCImpl_outs_op16_addr32(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    case 4: rcStrict = iemCImpl_outs_op32_addr32(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    default:
                        AssertMsgFailedReturn(("cbValue=%#x\n", cbValue), VERR_IEM_INVALID_OPERAND_SIZE);
                }
                break;

            case IEMMODE_64BIT:
                switch (cbValue)
                {
                    case 1: rcStrict = iemCImpl_outs_op8_addr64(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    case 2: rcStrict = iemCImpl_outs_op16_addr64(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    case 4: rcStrict = iemCImpl_outs_op32_addr64(pVCpu, cbInstr, iEffSeg, fIoChecked); break;
                    default:
                        AssertMsgFailedReturn(("cbValue=%#x\n", cbValue), VERR_IEM_INVALID_OPERAND_SIZE);
                }
                break;

            default:
                AssertMsgFailedReturn(("enmAddrMode=%d\n", enmAddrMode), VERR_IEM_INVALID_ADDRESS_MODE);
        }
    }

    if (ICORE(pVCpu).cActiveMappings)
        iemMemRollback(pVCpu);

    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM for executing string I/O IN (read) instructions.
 *
 * This API ASSUMES that the caller has already verified that the guest code is
 * allowed to access the I/O port.  (The I/O port is in the DX register in the
 * guest state.)
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbValue             The size of the I/O port access (1, 2, or 4).
 * @param   enmAddrMode         The addressing mode.
 * @param   fRepPrefix          Indicates whether a repeat prefix is used
 *                              (doesn't matter which for this instruction).
 * @param   cbInstr             The instruction length in bytes.
 * @param   fIoChecked          Whether the access to the I/O port has been
 *                              checked or not.  It's typically checked in the
 *                              HM scenario.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecStringIoRead(PVMCPUCC pVCpu, uint8_t cbValue, IEMMODE enmAddrMode,
                                               bool fRepPrefix, uint8_t cbInstr, bool fIoChecked)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 1);

    /*
     * State init.
     */
    iemInitExec(pVCpu, 0 /*fExecOpts*/);

    /*
     * Switch orgy for getting to the right handler.
     */
    VBOXSTRICTRC rcStrict;
    if (fRepPrefix)
    {
        switch (enmAddrMode)
        {
            case IEMMODE_16BIT:
                switch (cbValue)
                {
                    case 1: rcStrict = iemCImpl_rep_ins_op8_addr16(pVCpu, cbInstr, fIoChecked); break;
                    case 2: rcStrict = iemCImpl_rep_ins_op16_addr16(pVCpu, cbInstr, fIoChecked); break;
                    case 4: rcStrict = iemCImpl_rep_ins_op32_addr16(pVCpu, cbInstr, fIoChecked); break;
                    default:
                        AssertMsgFailedReturn(("cbValue=%#x\n", cbValue), VERR_IEM_INVALID_OPERAND_SIZE);
                }
                break;

            case IEMMODE_32BIT:
                switch (cbValue)
                {
                    case 1: rcStrict = iemCImpl_rep_ins_op8_addr32(pVCpu, cbInstr, fIoChecked); break;
                    case 2: rcStrict = iemCImpl_rep_ins_op16_addr32(pVCpu, cbInstr, fIoChecked); break;
                    case 4: rcStrict = iemCImpl_rep_ins_op32_addr32(pVCpu, cbInstr, fIoChecked); break;
                    default:
                        AssertMsgFailedReturn(("cbValue=%#x\n", cbValue), VERR_IEM_INVALID_OPERAND_SIZE);
                }
                break;

            case IEMMODE_64BIT:
                switch (cbValue)
                {
                    case 1: rcStrict = iemCImpl_rep_ins_op8_addr64(pVCpu, cbInstr, fIoChecked); break;
                    case 2: rcStrict = iemCImpl_rep_ins_op16_addr64(pVCpu, cbInstr, fIoChecked); break;
                    case 4: rcStrict = iemCImpl_rep_ins_op32_addr64(pVCpu, cbInstr, fIoChecked); break;
                    default:
                        AssertMsgFailedReturn(("cbValue=%#x\n", cbValue), VERR_IEM_INVALID_OPERAND_SIZE);
                }
                break;

            default:
                AssertMsgFailedReturn(("enmAddrMode=%d\n", enmAddrMode), VERR_IEM_INVALID_ADDRESS_MODE);
        }
    }
    else
    {
        switch (enmAddrMode)
        {
            case IEMMODE_16BIT:
                switch (cbValue)
                {
                    case 1: rcStrict = iemCImpl_ins_op8_addr16(pVCpu, cbInstr, fIoChecked); break;
                    case 2: rcStrict = iemCImpl_ins_op16_addr16(pVCpu, cbInstr, fIoChecked); break;
                    case 4: rcStrict = iemCImpl_ins_op32_addr16(pVCpu, cbInstr, fIoChecked); break;
                    default:
                        AssertMsgFailedReturn(("cbValue=%#x\n", cbValue), VERR_IEM_INVALID_OPERAND_SIZE);
                }
                break;

            case IEMMODE_32BIT:
                switch (cbValue)
                {
                    case 1: rcStrict = iemCImpl_ins_op8_addr32(pVCpu, cbInstr, fIoChecked); break;
                    case 2: rcStrict = iemCImpl_ins_op16_addr32(pVCpu, cbInstr, fIoChecked); break;
                    case 4: rcStrict = iemCImpl_ins_op32_addr32(pVCpu, cbInstr, fIoChecked); break;
                    default:
                        AssertMsgFailedReturn(("cbValue=%#x\n", cbValue), VERR_IEM_INVALID_OPERAND_SIZE);
                }
                break;

            case IEMMODE_64BIT:
                switch (cbValue)
                {
                    case 1: rcStrict = iemCImpl_ins_op8_addr64(pVCpu, cbInstr, fIoChecked); break;
                    case 2: rcStrict = iemCImpl_ins_op16_addr64(pVCpu, cbInstr, fIoChecked); break;
                    case 4: rcStrict = iemCImpl_ins_op32_addr64(pVCpu, cbInstr, fIoChecked); break;
                    default:
                        AssertMsgFailedReturn(("cbValue=%#x\n", cbValue), VERR_IEM_INVALID_OPERAND_SIZE);
                }
                break;

            default:
                AssertMsgFailedReturn(("enmAddrMode=%d\n", enmAddrMode), VERR_IEM_INVALID_ADDRESS_MODE);
        }
    }

    if (   ICORE(pVCpu).cActiveMappings == 0
        || VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_IEM))
    { /* likely */ }
    else
    {
        AssertMsg(!IOM_SUCCESS(rcStrict), ("%#x\n", VBOXSTRICTRC_VAL(rcStrict)));
        iemMemRollback(pVCpu);
    }
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for rawmode to write execute an OUT instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 * @param   u16Port     The port to read.
 * @param   fImm        Whether the port is specified using an immediate operand or
 *                      using the implicit DX register.
 * @param   cbReg       The register size.
 *
 * @remarks In ring-0 not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedOut(PVMCPUCC pVCpu, uint8_t cbInstr, uint16_t u16Port, bool fImm, uint8_t cbReg)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 1);
    Assert(cbReg <= 4 && cbReg != 3);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_3(iemCImpl_out, u16Port, cbReg,
                                             ((uint8_t)fImm << 7) | 0xf /** @todo never worked with intercepts */);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for rawmode to write execute an IN instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 * @param   u16Port     The port to read.
 * @param   fImm        Whether the port is specified using an immediate operand or
 *                      using the implicit DX.
 * @param   cbReg       The register size.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedIn(PVMCPUCC pVCpu, uint8_t cbInstr, uint16_t u16Port, bool fImm, uint8_t cbReg)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 1);
    Assert(cbReg <= 4 && cbReg != 3);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_3(iemCImpl_in, u16Port, cbReg,
                                             ((uint8_t)fImm << 7) | 0xf /** @todo never worked with intercepts */);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to write to a CRx register.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 * @param   iCrReg      The control register number (destination).
 * @param   iGReg       The general purpose register number (source).
 *
 * @remarks In ring-0 not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedMovCRxWrite(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iCrReg, uint8_t iGReg)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 2);
    Assert(iCrReg < 16);
    Assert(iGReg < 16);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_2(iemCImpl_mov_Cd_Rd, iCrReg, iGReg);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to read from a CRx register.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 * @param   iGReg       The general purpose register number (destination).
 * @param   iCrReg      The control register number (source).
 *
 * @remarks In ring-0 not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedMovCRxRead(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iGReg, uint8_t iCrReg)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 2);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_CR4
                        | CPUMCTX_EXTRN_APIC_TPR);
    Assert(iCrReg < 16);
    Assert(iGReg < 16);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_2(iemCImpl_mov_Rd_Cd, iGReg, iCrReg);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to write to a DRx register.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 * @param   iDrReg      The debug register number (destination).
 * @param   iGReg       The general purpose register number (source).
 *
 * @remarks In ring-0 not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedMovDRxWrite(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iDrReg, uint8_t iGReg)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 2);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_DR7);
    Assert(iDrReg < 8);
    Assert(iGReg < 16);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_2(iemCImpl_mov_Dd_Rd, iDrReg, iGReg);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to read from a DRx register.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 * @param   iGReg       The general purpose register number (destination).
 * @param   iDrReg      The debug register number (source).
 *
 * @remarks In ring-0 not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedMovDRxRead(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iGReg, uint8_t iDrReg)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 2);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_DR7);
    Assert(iDrReg < 8);
    Assert(iGReg < 16);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_2(iemCImpl_mov_Rd_Dd, iGReg, iDrReg);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to clear the CR0[TS] bit.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 *
 * @remarks In ring-0 not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedClts(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 2);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_clts);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the LMSW instruction (loads CR0).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   cbInstr         The instruction length in bytes.
 * @param   uValue          The value to load into CR0.
 * @param   GCPtrEffDst     The guest-linear address if the LMSW instruction has a
 *                          memory operand. Otherwise pass NIL_RTGCPTR.
 *
 * @remarks In ring-0 not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedLmsw(PVMCPUCC pVCpu, uint8_t cbInstr, uint16_t uValue, RTGCPTR GCPtrEffDst)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 3);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_2(iemCImpl_lmsw, uValue, GCPtrEffDst);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the XSETBV instruction (loads XCRx).
 *
 * Takes input values in ecx and edx:eax of the CPU context of the calling EMT.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   cbInstr     The instruction length in bytes.
 * @remarks In ring-0 not all of the state needs to be synced in.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedXsetbv(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 3);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_xsetbv);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the WBINVD instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 *
 * @remarks In ring-0 not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedWbinvd(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 2);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_wbinvd);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the INVD instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 *
 * @remarks In ring-0 not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedInvd(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 2);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_invd);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the INVLPG instruction.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_PGM_SYNC_CR3
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 * @param   GCPtrPage   The effective address of the page to invalidate.
 *
 * @remarks In ring-0 not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedInvlpg(PVMCPUCC pVCpu, uint8_t cbInstr, RTGCPTR GCPtrPage)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 3);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_1(iemCImpl_invlpg, GCPtrPage);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the INVPCID instruction.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_PGM_SYNC_CR3
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 * @param   iEffSeg     The effective segment register.
 * @param   GCPtrDesc   The effective address of the INVPCID descriptor.
 * @param   uType       The invalidation type.
 *
 * @remarks In ring-0 not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedInvpcid(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iEffSeg, RTGCPTR GCPtrDesc,
                                                 uint64_t uType)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 4);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_3(iemCImpl_invpcid, iEffSeg, GCPtrDesc, uType);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the CPUID instruction.
 *
 * @returns Strict VBox status code.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbInstr             The instruction length in bytes.
 *
 * @remarks Not all of the state needs to be synced in, the usual pluss RAX and RCX.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedCpuid(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 2);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RCX);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_cpuid);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the RDPMC instruction.
 *
 * @returns Strict VBox status code.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbInstr             The instruction length in bytes.
 *
 * @remarks Not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedRdpmc(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 2);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_CR4);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_rdpmc);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the RDTSC instruction.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_IEM_RAISED_XCPT (VINF_EM_RESCHEDULE) if exception is raised.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbInstr             The instruction length in bytes.
 *
 * @remarks Not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedRdtsc(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 2);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_CR4);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_rdtsc);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the RDTSCP instruction.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_IEM_RAISED_XCPT (VINF_EM_RESCHEDULE) if exception is raised.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbInstr             The instruction length in bytes.
 *
 * @remarks Not all of the state needs to be synced in.  Recommended
 *          to include CPUMCTX_EXTRN_TSC_AUX, to avoid extra fetch call.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedRdtscp(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 3);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_CR4 | CPUMCTX_EXTRN_TSC_AUX);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_rdtscp);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the RDMSR instruction.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_IEM_RAISED_XCPT (VINF_EM_RESCHEDULE) if exception is raised.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbInstr             The instruction length in bytes.
 *
 * @remarks Not all of the state needs to be synced in.  Requires RCX and
 *          (currently) all MSRs.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedRdmsr(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 2);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_RCX | CPUMCTX_EXTRN_ALL_MSRS);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_rdmsr);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the WRMSR instruction.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_IEM_RAISED_XCPT (VINF_EM_RESCHEDULE) if exception is raised.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbInstr             The instruction length in bytes.
 *
 * @remarks Not all of the state needs to be synced in.  Requires RCX, RAX, RDX,
 *          and (currently) all MSRs.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedWrmsr(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 2);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK
                        | CPUMCTX_EXTRN_RCX | CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RDX | CPUMCTX_EXTRN_ALL_MSRS);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_wrmsr);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the MONITOR instruction.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_IEM_RAISED_XCPT (VINF_EM_RESCHEDULE) if exception is raised.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbInstr             The instruction length in bytes.
 *
 * @remarks Not all of the state needs to be synced in.
 * @remarks ASSUMES the default segment of DS and no segment override prefixes
 *          are used.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedMonitor(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 3);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK | CPUMCTX_EXTRN_DS);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_1(iemCImpl_monitor, X86_SREG_DS);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the MWAIT instruction.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_IEM_RAISED_XCPT (VINF_EM_RESCHEDULE) if exception is raised.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbInstr             The instruction length in bytes.
 *
 * @remarks Not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedMwait(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 3);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_RCX | CPUMCTX_EXTRN_RAX);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_mwait);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate the HLT instruction.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_IEM_RAISED_XCPT (VINF_EM_RESCHEDULE) if exception is raised.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbInstr             The instruction length in bytes.
 *
 * @remarks Not all of the state needs to be synced in.
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedHlt(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 1);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_hlt);
    Assert(!ICORE(pVCpu).cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}

