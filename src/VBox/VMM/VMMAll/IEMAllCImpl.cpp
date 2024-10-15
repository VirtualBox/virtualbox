/* $Id: IEMAllCImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Instruction Implementation in C/C++ (code include).
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
#define IEM_WITH_OPAQUE_DECODER_STATE
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/apic.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/gim.h>
#include <VBox/vmm/gcm.h>
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
# include <VBox/vmm/em.h>
# include <VBox/vmm/hm_svm.h>
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
# include <VBox/vmm/hmvmxinline.h>
#endif
#ifndef VBOX_WITHOUT_CPUID_HOST_CALL
# include <VBox/vmm/cpuidcall.h>
#endif
#include <VBox/vmm/tm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/dbgftrace.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/dis.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/x86.h>

#include "IEMInline.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/**
 * Flushes the prefetch buffer, light version.
 * @todo The \#if conditions here must match the ones in iemOpcodeFlushLight().
 */
#ifndef IEM_WITH_CODE_TLB
# define IEM_FLUSH_PREFETCH_LIGHT(a_pVCpu, a_cbInstr) iemOpcodeFlushLight(a_pVCpu, a_cbInstr)
#else
# define IEM_FLUSH_PREFETCH_LIGHT(a_pVCpu, a_cbInstr) do { } while (0)
#endif

/**
 * Flushes the prefetch buffer, heavy version.
 * @todo The \#if conditions here must match the ones in iemOpcodeFlushHeavy().
 */
#if !defined(IEM_WITH_CODE_TLB) || 1
# define IEM_FLUSH_PREFETCH_HEAVY(a_pVCpu, a_cbInstr) iemOpcodeFlushHeavy(a_pVCpu, a_cbInstr)
#else
# define IEM_FLUSH_PREFETCH_HEAVY(a_pVCpu, a_cbInstr) do { } while (0)
#endif



/** @name Misc Helpers
 * @{
 */


/**
 * Worker function for iemHlpCheckPortIOPermission, don't call directly.
 *
 * @returns Strict VBox status code.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   u16Port             The port number.
 * @param   cbOperand           The operand size.
 */
static VBOXSTRICTRC iemHlpCheckPortIOPermissionBitmap(PVMCPUCC pVCpu, uint16_t u16Port, uint8_t cbOperand)
{
    /* The TSS bits we're interested in are the same on 386 and AMD64. */
    AssertCompile(AMD64_SEL_TYPE_SYS_TSS_BUSY  == X86_SEL_TYPE_SYS_386_TSS_BUSY);
    AssertCompile(AMD64_SEL_TYPE_SYS_TSS_AVAIL == X86_SEL_TYPE_SYS_386_TSS_AVAIL);
    AssertCompileMembersAtSameOffset(X86TSS32, offIoBitmap, X86TSS64, offIoBitmap);
    AssertCompile(sizeof(X86TSS32) == sizeof(X86TSS64));

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_TR);

    /*
     * Check the TSS type, 16-bit TSSes doesn't have any I/O permission bitmap.
     */
    Assert(!pVCpu->cpum.GstCtx.tr.Attr.n.u1DescType);
    if (RT_UNLIKELY(   pVCpu->cpum.GstCtx.tr.Attr.n.u4Type != AMD64_SEL_TYPE_SYS_TSS_BUSY
                    && pVCpu->cpum.GstCtx.tr.Attr.n.u4Type != AMD64_SEL_TYPE_SYS_TSS_AVAIL))
    {
        Log(("iemHlpCheckPortIOPermissionBitmap: Port=%#x cb=%d - TSS type %#x (attr=%#x) has no I/O bitmap -> #GP(0)\n",
             u16Port, cbOperand, pVCpu->cpum.GstCtx.tr.Attr.n.u4Type, pVCpu->cpum.GstCtx.tr.Attr.u));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /*
     * Read the bitmap offset (may #PF).
     */
    uint16_t offBitmap;
    VBOXSTRICTRC rcStrict = iemMemFetchSysU16(pVCpu, &offBitmap, UINT8_MAX,
                                              pVCpu->cpum.GstCtx.tr.u64Base + RT_UOFFSETOF(X86TSS64, offIoBitmap));
    if (rcStrict != VINF_SUCCESS)
    {
        Log(("iemHlpCheckPortIOPermissionBitmap: Error reading offIoBitmap (%Rrc)\n", VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }

    /*
     * The bit range from u16Port to (u16Port + cbOperand - 1), however intel
     * describes the CPU actually reading two bytes regardless of whether the
     * bit range crosses a byte boundrary.  Thus the + 1 in the test below.
     */
    uint32_t offFirstBit = (uint32_t)u16Port / 8 + offBitmap;
    /** @todo check if real CPUs ensures that offBitmap has a minimum value of
     *        for instance sizeof(X86TSS32). */
    if (offFirstBit + 1 > pVCpu->cpum.GstCtx.tr.u32Limit) /* the limit is inclusive */
    {
        Log(("iemHlpCheckPortIOPermissionBitmap: offFirstBit=%#x + 1 is beyond u32Limit=%#x -> #GP(0)\n",
             offFirstBit, pVCpu->cpum.GstCtx.tr.u32Limit));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /*
     * Read the necessary bits.
     */
    /** @todo Test the assertion in the intel manual that the CPU reads two
     *        bytes.  The question is how this works wrt to \#PF and \#GP on the
     *        2nd byte when it's not required. */
    uint16_t bmBytes = UINT16_MAX;
    rcStrict = iemMemFetchSysU16(pVCpu, &bmBytes, UINT8_MAX, pVCpu->cpum.GstCtx.tr.u64Base + offFirstBit);
    if (rcStrict != VINF_SUCCESS)
    {
        Log(("iemHlpCheckPortIOPermissionBitmap: Error reading I/O bitmap @%#x (%Rrc)\n", offFirstBit, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }

    /*
     * Perform the check.
     */
    uint16_t fPortMask = (1 << cbOperand) - 1;
    bmBytes >>= (u16Port & 7);
    if (bmBytes & fPortMask)
    {
        Log(("iemHlpCheckPortIOPermissionBitmap: u16Port=%#x LB %u - access denied (bm=%#x mask=%#x) -> #GP(0)\n",
             u16Port, cbOperand, bmBytes, fPortMask));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    return VINF_SUCCESS;
}


/**
 * Checks if we are allowed to access the given I/O port, raising the
 * appropriate exceptions if we aren't (or if the I/O bitmap is not
 * accessible).
 *
 * @returns Strict VBox status code.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   u16Port             The port number.
 * @param   cbOperand           The operand size.
 */
DECLINLINE(VBOXSTRICTRC) iemHlpCheckPortIOPermission(PVMCPUCC pVCpu, uint16_t u16Port, uint8_t cbOperand)
{
    X86EFLAGS Efl;
    Efl.u = IEMMISC_GET_EFL(pVCpu);
    if (   (pVCpu->cpum.GstCtx.cr0 & X86_CR0_PE)
        && (    IEM_GET_CPL(pVCpu) > Efl.Bits.u2IOPL
            ||  Efl.Bits.u1VM) )
        return iemHlpCheckPortIOPermissionBitmap(pVCpu, u16Port, cbOperand);
    return VINF_SUCCESS;
}


#if 0
/**
 * Calculates the parity bit.
 *
 * @returns true if the bit is set, false if not.
 * @param   u8Result            The least significant byte of the result.
 */
static bool iemHlpCalcParityFlag(uint8_t u8Result)
{
    /*
     * Parity is set if the number of bits in the least significant byte of
     * the result is even.
     */
    uint8_t cBits;
    cBits  = u8Result & 1;              /* 0 */
    u8Result >>= 1;
    cBits += u8Result & 1;
    u8Result >>= 1;
    cBits += u8Result & 1;
    u8Result >>= 1;
    cBits += u8Result & 1;
    u8Result >>= 1;
    cBits += u8Result & 1;              /* 4 */
    u8Result >>= 1;
    cBits += u8Result & 1;
    u8Result >>= 1;
    cBits += u8Result & 1;
    u8Result >>= 1;
    cBits += u8Result & 1;
    return !(cBits & 1);
}
#endif /* not used */


/**
 * Updates the specified flags according to a 8-bit result.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   u8Result            The result to set the flags according to.
 * @param   fToUpdate           The flags to update.
 * @param   fUndefined          The flags that are specified as undefined.
 */
static void iemHlpUpdateArithEFlagsU8(PVMCPUCC pVCpu, uint8_t u8Result, uint32_t fToUpdate, uint32_t fUndefined)
{
    uint32_t fEFlags = iemAImpl_test_u8(pVCpu->cpum.GstCtx.eflags.u, &u8Result, u8Result);
    pVCpu->cpum.GstCtx.eflags.u &= ~(fToUpdate | fUndefined);
    pVCpu->cpum.GstCtx.eflags.u |= (fToUpdate | fUndefined) & fEFlags;
}


/**
 * Updates the specified flags according to a 16-bit result.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   u16Result           The result to set the flags according to.
 * @param   fToUpdate           The flags to update.
 * @param   fUndefined          The flags that are specified as undefined.
 */
static void iemHlpUpdateArithEFlagsU16(PVMCPUCC pVCpu, uint16_t u16Result, uint32_t fToUpdate, uint32_t fUndefined)
{
    uint32_t fEFlags = iemAImpl_test_u16(pVCpu->cpum.GstCtx.eflags.u, &u16Result, u16Result);
    pVCpu->cpum.GstCtx.eflags.u &= ~(fToUpdate | fUndefined);
    pVCpu->cpum.GstCtx.eflags.u |= (fToUpdate | fUndefined) & fEFlags;
}


/**
 * Helper used by iret.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   uCpl                The new CPL.
 * @param   pSReg               Pointer to the segment register.
 */
static void iemHlpAdjustSelectorForNewCpl(PVMCPUCC pVCpu, uint8_t uCpl, PCPUMSELREG pSReg)
{
    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, pSReg));
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_SREG_MASK);

    if (   uCpl > pSReg->Attr.n.u2Dpl
        && pSReg->Attr.n.u1DescType /* code or data, not system */
        &&    (pSReg->Attr.n.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF))
           !=                         (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF)) /* not conforming code */
        iemHlpLoadNullDataSelectorProt(pVCpu, pSReg, 0);
}


/**
 * Indicates that we have modified the FPU state.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemHlpUsedFpu(PVMCPUCC pVCpu)
{
    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_FPU_REM);
}

/** @} */

/** @name C Implementations
 * @{
 */


/**
 * Implements a pop [mem16].
 */
IEM_CIMPL_DEF_2(iemCImpl_pop_mem16, uint16_t, iEffSeg, RTGCPTR, GCPtrEffDst)
{
    uint16_t  u16Value;
    RTUINT64U TmpRsp;
    TmpRsp.u = pVCpu->cpum.GstCtx.rsp;
    VBOXSTRICTRC rcStrict = iemMemStackPopU16Ex(pVCpu, &u16Value, &TmpRsp);
    if (rcStrict == VINF_SUCCESS)
    {
        rcStrict = iemMemStoreDataU16(pVCpu, iEffSeg, GCPtrEffDst, u16Value);
        if (rcStrict == VINF_SUCCESS)
        {
            pVCpu->cpum.GstCtx.rsp = TmpRsp.u;
            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }
    }
    return rcStrict;

}


/**
 * Implements a pop [mem32].
 */
IEM_CIMPL_DEF_2(iemCImpl_pop_mem32, uint16_t, iEffSeg, RTGCPTR, GCPtrEffDst)
{
    uint32_t  u32Value;
    RTUINT64U TmpRsp;
    TmpRsp.u = pVCpu->cpum.GstCtx.rsp;
    VBOXSTRICTRC rcStrict = iemMemStackPopU32Ex(pVCpu, &u32Value, &TmpRsp);
    if (rcStrict == VINF_SUCCESS)
    {
        rcStrict = iemMemStoreDataU32(pVCpu, iEffSeg, GCPtrEffDst, u32Value);
        if (rcStrict == VINF_SUCCESS)
        {
            pVCpu->cpum.GstCtx.rsp = TmpRsp.u;
            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }
    }
    return rcStrict;

}


/**
 * Implements a pop [mem64].
 */
IEM_CIMPL_DEF_2(iemCImpl_pop_mem64, uint16_t, iEffSeg, RTGCPTR, GCPtrEffDst)
{
    uint64_t  u64Value;
    RTUINT64U TmpRsp;
    TmpRsp.u = pVCpu->cpum.GstCtx.rsp;
    VBOXSTRICTRC rcStrict = iemMemStackPopU64Ex(pVCpu, &u64Value, &TmpRsp);
    if (rcStrict == VINF_SUCCESS)
    {
        rcStrict = iemMemStoreDataU64(pVCpu, iEffSeg, GCPtrEffDst, u64Value);
        if (rcStrict == VINF_SUCCESS)
        {
            pVCpu->cpum.GstCtx.rsp = TmpRsp.u;
            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }
    }
    return rcStrict;

}


/**
 * Implements a 16-bit popa.
 */
IEM_CIMPL_DEF_0(iemCImpl_popa_16)
{
    RTGCPTR         GCPtrStart  = iemRegGetEffRsp(pVCpu);
    RTGCPTR         GCPtrLast   = GCPtrStart + 15;
    VBOXSTRICTRC    rcStrict;

    /*
     * The docs are a bit hard to comprehend here, but it looks like we wrap
     * around in real mode as long as none of the individual "popa" crosses the
     * end of the stack segment.  In protected mode we check the whole access
     * in one go.  For efficiency, only do the word-by-word thing if we're in
     * danger of wrapping around.
     */
    /** @todo do popa boundary / wrap-around checks.  */
    if (RT_UNLIKELY(   IEM_IS_REAL_OR_V86_MODE(pVCpu)
                    && (pVCpu->cpum.GstCtx.cs.u32Limit < GCPtrLast)) ) /* ASSUMES 64-bit RTGCPTR */
    {
        /* word-by-word */
        RTUINT64U TmpRsp;
        TmpRsp.u = pVCpu->cpum.GstCtx.rsp;
        rcStrict = iemMemStackPopU16Ex(pVCpu, &pVCpu->cpum.GstCtx.di, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPopU16Ex(pVCpu, &pVCpu->cpum.GstCtx.si, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPopU16Ex(pVCpu, &pVCpu->cpum.GstCtx.bp, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
        {
            iemRegAddToRspEx(pVCpu, &TmpRsp, 2); /* sp */
            rcStrict = iemMemStackPopU16Ex(pVCpu, &pVCpu->cpum.GstCtx.bx, &TmpRsp);
        }
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPopU16Ex(pVCpu, &pVCpu->cpum.GstCtx.dx, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPopU16Ex(pVCpu, &pVCpu->cpum.GstCtx.cx, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPopU16Ex(pVCpu, &pVCpu->cpum.GstCtx.ax, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
        {
            pVCpu->cpum.GstCtx.rsp = TmpRsp.u;
            rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }
    }
    else
    {
        uint8_t         bUnmapInfo;
        uint16_t const *pau16Mem = NULL;
        rcStrict = iemMemMap(pVCpu, (void **)&pau16Mem, &bUnmapInfo, 16, X86_SREG_SS, GCPtrStart,
                             IEM_ACCESS_STACK_R, sizeof(*pau16Mem) - 1);
        if (rcStrict == VINF_SUCCESS)
        {
            pVCpu->cpum.GstCtx.di = pau16Mem[7 - X86_GREG_xDI];
            pVCpu->cpum.GstCtx.si = pau16Mem[7 - X86_GREG_xSI];
            pVCpu->cpum.GstCtx.bp = pau16Mem[7 - X86_GREG_xBP];
            /* skip sp */
            pVCpu->cpum.GstCtx.bx = pau16Mem[7 - X86_GREG_xBX];
            pVCpu->cpum.GstCtx.dx = pau16Mem[7 - X86_GREG_xDX];
            pVCpu->cpum.GstCtx.cx = pau16Mem[7 - X86_GREG_xCX];
            pVCpu->cpum.GstCtx.ax = pau16Mem[7 - X86_GREG_xAX];
            rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
            if (rcStrict == VINF_SUCCESS)
            {
                iemRegAddToRsp(pVCpu, 16);
                rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
            }
        }
    }
    return rcStrict;
}


/**
 * Implements a 32-bit popa.
 */
IEM_CIMPL_DEF_0(iemCImpl_popa_32)
{
    RTGCPTR         GCPtrStart  = iemRegGetEffRsp(pVCpu);
    RTGCPTR         GCPtrLast   = GCPtrStart + 31;
    VBOXSTRICTRC    rcStrict;

    /*
     * The docs are a bit hard to comprehend here, but it looks like we wrap
     * around in real mode as long as none of the individual "popa" crosses the
     * end of the stack segment.  In protected mode we check the whole access
     * in one go.  For efficiency, only do the word-by-word thing if we're in
     * danger of wrapping around.
     */
    /** @todo do popa boundary / wrap-around checks.  */
    if (RT_UNLIKELY(   IEM_IS_REAL_OR_V86_MODE(pVCpu)
                    && (pVCpu->cpum.GstCtx.cs.u32Limit < GCPtrLast)) ) /* ASSUMES 64-bit RTGCPTR */
    {
        /* word-by-word */
        RTUINT64U TmpRsp;
        TmpRsp.u = pVCpu->cpum.GstCtx.rsp;
        rcStrict = iemMemStackPopU32Ex(pVCpu, &pVCpu->cpum.GstCtx.edi, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPopU32Ex(pVCpu, &pVCpu->cpum.GstCtx.esi, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPopU32Ex(pVCpu, &pVCpu->cpum.GstCtx.ebp, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
        {
            iemRegAddToRspEx(pVCpu, &TmpRsp, 2); /* sp */
            rcStrict = iemMemStackPopU32Ex(pVCpu, &pVCpu->cpum.GstCtx.ebx, &TmpRsp);
        }
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPopU32Ex(pVCpu, &pVCpu->cpum.GstCtx.edx, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPopU32Ex(pVCpu, &pVCpu->cpum.GstCtx.ecx, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPopU32Ex(pVCpu, &pVCpu->cpum.GstCtx.eax, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
        {
#if 1  /** @todo what actually happens with the high bits when we're in 16-bit mode? */
            pVCpu->cpum.GstCtx.rdi &= UINT32_MAX;
            pVCpu->cpum.GstCtx.rsi &= UINT32_MAX;
            pVCpu->cpum.GstCtx.rbp &= UINT32_MAX;
            pVCpu->cpum.GstCtx.rbx &= UINT32_MAX;
            pVCpu->cpum.GstCtx.rdx &= UINT32_MAX;
            pVCpu->cpum.GstCtx.rcx &= UINT32_MAX;
            pVCpu->cpum.GstCtx.rax &= UINT32_MAX;
#endif
            pVCpu->cpum.GstCtx.rsp = TmpRsp.u;
            rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }
    }
    else
    {
        uint8_t         bUnmapInfo;
        uint32_t const *pau32Mem;
        rcStrict = iemMemMap(pVCpu, (void **)&pau32Mem, &bUnmapInfo, 32, X86_SREG_SS, GCPtrStart,
                             IEM_ACCESS_STACK_R, sizeof(*pau32Mem) - 1);
        if (rcStrict == VINF_SUCCESS)
        {
            pVCpu->cpum.GstCtx.rdi = pau32Mem[7 - X86_GREG_xDI];
            pVCpu->cpum.GstCtx.rsi = pau32Mem[7 - X86_GREG_xSI];
            pVCpu->cpum.GstCtx.rbp = pau32Mem[7 - X86_GREG_xBP];
            /* skip esp */
            pVCpu->cpum.GstCtx.rbx = pau32Mem[7 - X86_GREG_xBX];
            pVCpu->cpum.GstCtx.rdx = pau32Mem[7 - X86_GREG_xDX];
            pVCpu->cpum.GstCtx.rcx = pau32Mem[7 - X86_GREG_xCX];
            pVCpu->cpum.GstCtx.rax = pau32Mem[7 - X86_GREG_xAX];
            rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
            if (rcStrict == VINF_SUCCESS)
            {
                iemRegAddToRsp(pVCpu, 32);
                rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
            }
        }
    }
    return rcStrict;
}


/**
 * Implements a 16-bit pusha.
 */
IEM_CIMPL_DEF_0(iemCImpl_pusha_16)
{
    RTGCPTR         GCPtrTop    = iemRegGetEffRsp(pVCpu);
    RTGCPTR         GCPtrBottom = GCPtrTop - 15;
    VBOXSTRICTRC    rcStrict;

    /*
     * The docs are a bit hard to comprehend here, but it looks like we wrap
     * around in real mode as long as none of the individual "pushd" crosses the
     * end of the stack segment.  In protected mode we check the whole access
     * in one go.  For efficiency, only do the word-by-word thing if we're in
     * danger of wrapping around.
     */
    /** @todo do pusha boundary / wrap-around checks.  */
    if (RT_UNLIKELY(   GCPtrBottom > GCPtrTop
                    && IEM_IS_REAL_OR_V86_MODE(pVCpu) ) )
    {
        /* word-by-word */
        RTUINT64U TmpRsp;
        TmpRsp.u = pVCpu->cpum.GstCtx.rsp;
        rcStrict = iemMemStackPushU16Ex(pVCpu, pVCpu->cpum.GstCtx.ax, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPushU16Ex(pVCpu, pVCpu->cpum.GstCtx.cx, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPushU16Ex(pVCpu, pVCpu->cpum.GstCtx.dx, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPushU16Ex(pVCpu, pVCpu->cpum.GstCtx.bx, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPushU16Ex(pVCpu, pVCpu->cpum.GstCtx.sp, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPushU16Ex(pVCpu, pVCpu->cpum.GstCtx.bp, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPushU16Ex(pVCpu, pVCpu->cpum.GstCtx.si, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPushU16Ex(pVCpu, pVCpu->cpum.GstCtx.di, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
        {
            pVCpu->cpum.GstCtx.rsp = TmpRsp.u;
            rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }
    }
    else
    {
        GCPtrBottom--;
        uint8_t   bUnmapInfo;
        uint16_t *pau16Mem = NULL;
        rcStrict = iemMemMap(pVCpu, (void **)&pau16Mem, &bUnmapInfo, 16, X86_SREG_SS, GCPtrBottom,
                             IEM_ACCESS_STACK_W, sizeof(*pau16Mem) - 1);
        if (rcStrict == VINF_SUCCESS)
        {
            pau16Mem[7 - X86_GREG_xDI] = pVCpu->cpum.GstCtx.di;
            pau16Mem[7 - X86_GREG_xSI] = pVCpu->cpum.GstCtx.si;
            pau16Mem[7 - X86_GREG_xBP] = pVCpu->cpum.GstCtx.bp;
            pau16Mem[7 - X86_GREG_xSP] = pVCpu->cpum.GstCtx.sp;
            pau16Mem[7 - X86_GREG_xBX] = pVCpu->cpum.GstCtx.bx;
            pau16Mem[7 - X86_GREG_xDX] = pVCpu->cpum.GstCtx.dx;
            pau16Mem[7 - X86_GREG_xCX] = pVCpu->cpum.GstCtx.cx;
            pau16Mem[7 - X86_GREG_xAX] = pVCpu->cpum.GstCtx.ax;
            rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
            if (rcStrict == VINF_SUCCESS)
            {
                iemRegSubFromRsp(pVCpu, 16);
                rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
            }
        }
    }
    return rcStrict;
}


/**
 * Implements a 32-bit pusha.
 */
IEM_CIMPL_DEF_0(iemCImpl_pusha_32)
{
    RTGCPTR         GCPtrTop    = iemRegGetEffRsp(pVCpu);
    RTGCPTR         GCPtrBottom = GCPtrTop - 31;
    VBOXSTRICTRC    rcStrict;

    /*
     * The docs are a bit hard to comprehend here, but it looks like we wrap
     * around in real mode as long as none of the individual "pusha" crosses the
     * end of the stack segment.  In protected mode we check the whole access
     * in one go.  For efficiency, only do the word-by-word thing if we're in
     * danger of wrapping around.
     */
    /** @todo do pusha boundary / wrap-around checks.  */
    if (RT_UNLIKELY(   GCPtrBottom > GCPtrTop
                    && IEM_IS_REAL_OR_V86_MODE(pVCpu) ) )
    {
        /* word-by-word */
        RTUINT64U TmpRsp;
        TmpRsp.u = pVCpu->cpum.GstCtx.rsp;
        rcStrict = iemMemStackPushU32Ex(pVCpu, pVCpu->cpum.GstCtx.eax, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPushU32Ex(pVCpu, pVCpu->cpum.GstCtx.ecx, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPushU32Ex(pVCpu, pVCpu->cpum.GstCtx.edx, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPushU32Ex(pVCpu, pVCpu->cpum.GstCtx.ebx, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPushU32Ex(pVCpu, pVCpu->cpum.GstCtx.esp, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPushU32Ex(pVCpu, pVCpu->cpum.GstCtx.ebp, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPushU32Ex(pVCpu, pVCpu->cpum.GstCtx.esi, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemMemStackPushU32Ex(pVCpu, pVCpu->cpum.GstCtx.edi, &TmpRsp);
        if (rcStrict == VINF_SUCCESS)
        {
            pVCpu->cpum.GstCtx.rsp = TmpRsp.u;
            rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }
    }
    else
    {
        GCPtrBottom--;
        uint8_t   bUnmapInfo;
        uint32_t *pau32Mem;
        rcStrict = iemMemMap(pVCpu, (void **)&pau32Mem, &bUnmapInfo, 32, X86_SREG_SS, GCPtrBottom,
                             IEM_ACCESS_STACK_W, sizeof(*pau32Mem) - 1);
        if (rcStrict == VINF_SUCCESS)
        {
            pau32Mem[7 - X86_GREG_xDI] = pVCpu->cpum.GstCtx.edi;
            pau32Mem[7 - X86_GREG_xSI] = pVCpu->cpum.GstCtx.esi;
            pau32Mem[7 - X86_GREG_xBP] = pVCpu->cpum.GstCtx.ebp;
            pau32Mem[7 - X86_GREG_xSP] = pVCpu->cpum.GstCtx.esp;
            pau32Mem[7 - X86_GREG_xBX] = pVCpu->cpum.GstCtx.ebx;
            pau32Mem[7 - X86_GREG_xDX] = pVCpu->cpum.GstCtx.edx;
            pau32Mem[7 - X86_GREG_xCX] = pVCpu->cpum.GstCtx.ecx;
            pau32Mem[7 - X86_GREG_xAX] = pVCpu->cpum.GstCtx.eax;
            rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
            if (rcStrict == VINF_SUCCESS)
            {
                iemRegSubFromRsp(pVCpu, 32);
                rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
            }
        }
    }
    return rcStrict;
}


/**
 * Implements pushf.
 *
 *
 * @param   enmEffOpSize    The effective operand size.
 */
IEM_CIMPL_DEF_1(iemCImpl_pushf, IEMMODE, enmEffOpSize)
{
    VBOXSTRICTRC rcStrict;

    if (!IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_PUSHF))
    { /* probable */ }
    else
    {
        Log2(("pushf: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_PUSHF, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /*
     * If we're in V8086 mode some care is required (which is why we're in
     * doing this in a C implementation).
     */
    uint32_t fEfl = IEMMISC_GET_EFL(pVCpu);
    if (   (fEfl & X86_EFL_VM)
        && X86_EFL_GET_IOPL(fEfl) != 3 )
    {
        Assert(pVCpu->cpum.GstCtx.cr0 & X86_CR0_PE);
        if (   enmEffOpSize != IEMMODE_16BIT
            || !(pVCpu->cpum.GstCtx.cr4 & X86_CR4_VME))
            return iemRaiseGeneralProtectionFault0(pVCpu);
        fEfl &= ~X86_EFL_IF;          /* (RF and VM are out of range) */
        fEfl |= (fEfl & X86_EFL_VIF) >> (19 - 9);
        rcStrict = iemMemStackPushU16(pVCpu, (uint16_t)fEfl);
    }
    else
    {

        /*
         * Ok, clear RF and VM, adjust for ancient CPUs, and push the flags.
         */
        fEfl &= ~(X86_EFL_RF | X86_EFL_VM);

        switch (enmEffOpSize)
        {
            case IEMMODE_16BIT:
                AssertCompile(IEMTARGETCPU_8086 <= IEMTARGETCPU_186 && IEMTARGETCPU_V20 <= IEMTARGETCPU_186 && IEMTARGETCPU_286 > IEMTARGETCPU_186);
                if (IEM_GET_TARGET_CPU(pVCpu) <= IEMTARGETCPU_186)
                    fEfl |= UINT16_C(0xf000);
                rcStrict = iemMemStackPushU16(pVCpu, (uint16_t)fEfl);
                break;
            case IEMMODE_32BIT:
                rcStrict = iemMemStackPushU32(pVCpu, fEfl);
                break;
            case IEMMODE_64BIT:
                rcStrict = iemMemStackPushU64(pVCpu, fEfl);
                break;
            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
    }

    if (rcStrict == VINF_SUCCESS)
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    return rcStrict;
}


/**
 * Implements popf.
 *
 * @param   enmEffOpSize    The effective operand size.
 */
IEM_CIMPL_DEF_1(iemCImpl_popf, IEMMODE, enmEffOpSize)
{
    uint32_t const  fEflOld = IEMMISC_GET_EFL(pVCpu);
    VBOXSTRICTRC    rcStrict;
    uint32_t        fEflNew;

    if (!IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_POPF))
    { /* probable */ }
    else
    {
        Log2(("popf: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_POPF, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /*
     * V8086 is special as usual.
     */
    if (fEflOld & X86_EFL_VM)
    {
        /*
         * Almost anything goes if IOPL is 3.
         */
        if (X86_EFL_GET_IOPL(fEflOld) == 3)
        {
            switch (enmEffOpSize)
            {
                case IEMMODE_16BIT:
                {
                    uint16_t u16Value;
                    rcStrict = iemMemStackPopU16(pVCpu, &u16Value);
                    if (rcStrict != VINF_SUCCESS)
                        return rcStrict;
                    fEflNew = u16Value | (fEflOld & UINT32_C(0xffff0000));
                    break;
                }
                case IEMMODE_32BIT:
                    rcStrict = iemMemStackPopU32(pVCpu, &fEflNew);
                    if (rcStrict != VINF_SUCCESS)
                        return rcStrict;
                    break;
                IEM_NOT_REACHED_DEFAULT_CASE_RET();
            }

            const uint32_t fPopfBits = pVCpu->CTX_SUFF(pVM)->cpum.ro.GuestFeatures.enmMicroarch != kCpumMicroarch_Intel_80386
                                     ? X86_EFL_POPF_BITS : X86_EFL_POPF_BITS_386;
            fEflNew &=   fPopfBits & ~(X86_EFL_IOPL);
            fEflNew |= ~(fPopfBits & ~(X86_EFL_IOPL)) & fEflOld;
        }
        /*
         * Interrupt flag virtualization with CR4.VME=1.
         */
        else if (   enmEffOpSize == IEMMODE_16BIT
                 && (pVCpu->cpum.GstCtx.cr4 & X86_CR4_VME) )
        {
            uint16_t    u16Value;
            RTUINT64U   TmpRsp;
            TmpRsp.u = pVCpu->cpum.GstCtx.rsp;
            rcStrict = iemMemStackPopU16Ex(pVCpu, &u16Value, &TmpRsp);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            if (   (   (u16Value & X86_EFL_IF)
                    && (fEflOld  & X86_EFL_VIP))
                || (u16Value & X86_EFL_TF) )
                return iemRaiseGeneralProtectionFault0(pVCpu);

            fEflNew = X86_EFL_RA1_MASK
                    | (u16Value & ~(X86_EFL_IF | X86_EFL_IOPL | X86_EFL_RAZ_MASK))
                    | (fEflOld & (UINT32_C(0xffff0000) | X86_EFL_IF | X86_EFL_IOPL) & ~(X86_EFL_VIF | X86_EFL_RF))
                    | ((uint32_t)(u16Value & X86_EFL_IF) << (X86_EFL_VIF_BIT - X86_EFL_IF_BIT));

            pVCpu->cpum.GstCtx.rsp = TmpRsp.u;
        }
        else
            return iemRaiseGeneralProtectionFault0(pVCpu);

    }
    /*
     * Not in V8086 mode.
     */
    else
    {
        /* Pop the flags. */
        switch (enmEffOpSize)
        {
            case IEMMODE_16BIT:
            {
                uint16_t u16Value;
                rcStrict = iemMemStackPopU16(pVCpu, &u16Value);
                if (rcStrict != VINF_SUCCESS)
                    return rcStrict;
                fEflNew = u16Value | (fEflOld & UINT32_C(0xffff0000));

                /*
                 * Ancient CPU adjustments:
                 *  - 8086, 80186, V20/30:
                 *    Fixed bits 15:12 bits are not kept correctly internally, mostly for
                 *    practical reasons (masking below).  We add them when pushing flags.
                 *  - 80286:
                 *    The NT and IOPL flags cannot be popped from real mode and are
                 *    therefore always zero (since a 286 can never exit from PM and
                 *    their initial value is zero).  This changed on a 386 and can
                 *    therefore be used to detect 286 or 386 CPU in real mode.
                 */
                if (   IEM_GET_TARGET_CPU(pVCpu) == IEMTARGETCPU_286
                    && !(pVCpu->cpum.GstCtx.cr0 & X86_CR0_PE) )
                    fEflNew &= ~(X86_EFL_NT | X86_EFL_IOPL);
                break;
            }
            case IEMMODE_32BIT:
                rcStrict = iemMemStackPopU32(pVCpu, &fEflNew);
                if (rcStrict != VINF_SUCCESS)
                    return rcStrict;
                break;
            case IEMMODE_64BIT:
            {
                uint64_t u64Value;
                rcStrict = iemMemStackPopU64(pVCpu, &u64Value);
                if (rcStrict != VINF_SUCCESS)
                    return rcStrict;
                fEflNew = u64Value;  /** @todo testcase: Check exactly what happens if high bits are set. */
                break;
            }
            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }

        /* Merge them with the current flags. */
        const uint32_t fPopfBits = pVCpu->CTX_SUFF(pVM)->cpum.ro.GuestFeatures.enmMicroarch != kCpumMicroarch_Intel_80386
                                 ? X86_EFL_POPF_BITS : X86_EFL_POPF_BITS_386;
        if (   (fEflNew & (X86_EFL_IOPL | X86_EFL_IF)) == (fEflOld & (X86_EFL_IOPL | X86_EFL_IF))
            || IEM_GET_CPL(pVCpu) == 0)
        {
            fEflNew &=  fPopfBits;
            fEflNew |= ~fPopfBits & fEflOld;
        }
        else if (IEM_GET_CPL(pVCpu) <= X86_EFL_GET_IOPL(fEflOld))
        {
            fEflNew &=   fPopfBits & ~(X86_EFL_IOPL);
            fEflNew |= ~(fPopfBits & ~(X86_EFL_IOPL)) & fEflOld;
        }
        else
        {
            fEflNew &=   fPopfBits & ~(X86_EFL_IOPL | X86_EFL_IF);
            fEflNew |= ~(fPopfBits & ~(X86_EFL_IOPL | X86_EFL_IF)) & fEflOld;
        }
    }

    /*
     * Commit the flags.
     */
    Assert(fEflNew & RT_BIT_32(1));
    IEMMISC_SET_EFL(pVCpu, fEflNew);
    pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~IEM_F_X86_AC) | iemCalcExecAcFlag(pVCpu);
    return iemRegAddToRipAndFinishingClearingRfEx(pVCpu, cbInstr, fEflOld);
}


/**
 * Implements far jumps and calls thru task segments (TSS).
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling thread.
 * @param   cbInstr         The current instruction length.
 * @param   uSel            The selector.
 * @param   enmBranch       The kind of branching we're performing.
 * @param   enmEffOpSize    The effective operand size.
 * @param   pDesc           The descriptor corresponding to @a uSel. The type is
 *                          task gate.
 */
static VBOXSTRICTRC iemCImpl_BranchTaskSegment(PVMCPUCC pVCpu, uint8_t cbInstr, uint16_t uSel, IEMBRANCH enmBranch,
                                               IEMMODE enmEffOpSize, PIEMSELDESC pDesc)
{
#ifndef IEM_IMPLEMENTS_TASKSWITCH
    IEM_RETURN_ASPECT_NOT_IMPLEMENTED();
#else
    Assert(enmBranch == IEMBRANCH_JUMP || enmBranch == IEMBRANCH_CALL);
    Assert(   pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_286_TSS_AVAIL
           || pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_386_TSS_AVAIL);
    RT_NOREF_PV(enmEffOpSize);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_XCPT_MASK);

    if (   pDesc->Legacy.Gate.u2Dpl < IEM_GET_CPL(pVCpu)
        || pDesc->Legacy.Gate.u2Dpl < (uSel & X86_SEL_RPL))
    {
        Log(("BranchTaskSegment invalid priv. uSel=%04x TSS DPL=%d CPL=%u Sel RPL=%u -> #GP\n", uSel, pDesc->Legacy.Gate.u2Dpl,
             IEM_GET_CPL(pVCpu), (uSel & X86_SEL_RPL)));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel & X86_SEL_MASK_OFF_RPL);
    }

    /** @todo This is checked earlier for far jumps (see iemCImpl_FarJmp) but not
     *        far calls (see iemCImpl_callf). Most likely in both cases it should be
     *        checked here, need testcases. */
    if (!pDesc->Legacy.Gen.u1Present)
    {
        Log(("BranchTaskSegment TSS not present uSel=%04x -> #NP\n", uSel));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, uSel & X86_SEL_MASK_OFF_RPL);
    }

    uint32_t uNextEip = pVCpu->cpum.GstCtx.eip + cbInstr;
    return iemTaskSwitch(pVCpu, enmBranch == IEMBRANCH_JUMP ? IEMTASKSWITCH_JUMP : IEMTASKSWITCH_CALL,
                         uNextEip, 0 /* fFlags */, 0 /* uErr */, 0 /* uCr2 */, uSel, pDesc);
#endif
}


/**
 * Implements far jumps and calls thru task gates.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling thread.
 * @param   cbInstr         The current instruction length.
 * @param   uSel            The selector.
 * @param   enmBranch       The kind of branching we're performing.
 * @param   enmEffOpSize    The effective operand size.
 * @param   pDesc           The descriptor corresponding to @a uSel. The type is
 *                          task gate.
 */
static VBOXSTRICTRC iemCImpl_BranchTaskGate(PVMCPUCC pVCpu, uint8_t cbInstr, uint16_t uSel, IEMBRANCH enmBranch,
                                            IEMMODE enmEffOpSize, PIEMSELDESC pDesc)
{
#ifndef IEM_IMPLEMENTS_TASKSWITCH
    IEM_RETURN_ASPECT_NOT_IMPLEMENTED();
#else
    Assert(enmBranch == IEMBRANCH_JUMP || enmBranch == IEMBRANCH_CALL);
    RT_NOREF_PV(enmEffOpSize);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_XCPT_MASK);

    if (   pDesc->Legacy.Gate.u2Dpl < IEM_GET_CPL(pVCpu)
        || pDesc->Legacy.Gate.u2Dpl < (uSel & X86_SEL_RPL))
    {
        Log(("BranchTaskGate invalid priv. uSel=%04x TSS DPL=%d CPL=%u Sel RPL=%u -> #GP\n", uSel, pDesc->Legacy.Gate.u2Dpl,
             IEM_GET_CPL(pVCpu), (uSel & X86_SEL_RPL)));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel & X86_SEL_MASK_OFF_RPL);
    }

    /** @todo This is checked earlier for far jumps (see iemCImpl_FarJmp) but not
     *        far calls (see iemCImpl_callf). Most likely in both cases it should be
     *        checked here, need testcases. */
    if (!pDesc->Legacy.Gen.u1Present)
    {
        Log(("BranchTaskSegment segment not present uSel=%04x -> #NP\n", uSel));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, uSel & X86_SEL_MASK_OFF_RPL);
    }

    /*
     * Fetch the new TSS descriptor from the GDT.
     */
    RTSEL uSelTss = pDesc->Legacy.Gate.u16Sel;
    if (uSelTss  & X86_SEL_LDT)
    {
        Log(("BranchTaskGate TSS is in LDT. uSel=%04x uSelTss=%04x -> #GP\n", uSel, uSelTss));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel & X86_SEL_MASK_OFF_RPL);
    }

    IEMSELDESC TssDesc;
    VBOXSTRICTRC rcStrict = iemMemFetchSelDesc(pVCpu, &TssDesc, uSelTss, X86_XCPT_GP);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    if (TssDesc.Legacy.Gate.u4Type & X86_SEL_TYPE_SYS_TSS_BUSY_MASK)
    {
        Log(("BranchTaskGate TSS is busy. uSel=%04x uSelTss=%04x DescType=%#x -> #GP\n", uSel, uSelTss,
             TssDesc.Legacy.Gate.u4Type));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel & X86_SEL_MASK_OFF_RPL);
    }

    if (!TssDesc.Legacy.Gate.u1Present)
    {
        Log(("BranchTaskGate TSS is not present. uSel=%04x uSelTss=%04x -> #NP\n", uSel, uSelTss));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, uSelTss & X86_SEL_MASK_OFF_RPL);
    }

    uint32_t uNextEip = pVCpu->cpum.GstCtx.eip + cbInstr;
    return iemTaskSwitch(pVCpu, enmBranch == IEMBRANCH_JUMP ? IEMTASKSWITCH_JUMP : IEMTASKSWITCH_CALL,
                         uNextEip, 0 /* fFlags */, 0 /* uErr */, 0 /* uCr2 */, uSelTss, &TssDesc);
#endif
}


/**
 * Implements far jumps and calls thru call gates.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling thread.
 * @param   cbInstr         The current instruction length.
 * @param   uSel            The selector.
 * @param   enmBranch       The kind of branching we're performing.
 * @param   enmEffOpSize    The effective operand size.
 * @param   pDesc           The descriptor corresponding to @a uSel. The type is
 *                          call gate.
 */
static VBOXSTRICTRC iemCImpl_BranchCallGate(PVMCPUCC pVCpu, uint8_t cbInstr, uint16_t uSel, IEMBRANCH enmBranch,
                                            IEMMODE enmEffOpSize, PIEMSELDESC pDesc)
{
#define IEM_IMPLEMENTS_CALLGATE
#ifndef IEM_IMPLEMENTS_CALLGATE
    IEM_RETURN_ASPECT_NOT_IMPLEMENTED();
#else
    RT_NOREF_PV(enmEffOpSize);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_XCPT_MASK);

    /* NB: Far jumps can only do intra-privilege transfers. Far calls support
     * inter-privilege calls and are much more complex.
     *
     * NB: 64-bit call gate has the same type as a 32-bit call gate! If
     * EFER.LMA=1, the gate must be 64-bit. Conversely if EFER.LMA=0, the gate
     * must be 16-bit or 32-bit.
     */
    /** @todo effective operand size is probably irrelevant here, only the
     *        call gate bitness matters??
     */
    VBOXSTRICTRC    rcStrict;
    RTPTRUNION      uPtrRet;
    uint64_t        uNewRsp;
    uint64_t        uNewRip;
    uint64_t        u64Base;
    uint32_t        cbLimit;
    RTSEL           uNewCS;
    IEMSELDESC      DescCS;

    AssertCompile(X86_SEL_TYPE_SYS_386_CALL_GATE == AMD64_SEL_TYPE_SYS_CALL_GATE);
    Assert(enmBranch == IEMBRANCH_JUMP || enmBranch == IEMBRANCH_CALL);
    Assert(   pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_286_CALL_GATE
           || pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_386_CALL_GATE);

    /* Determine the new instruction pointer from the gate descriptor. */
    uNewRip = pDesc->Legacy.Gate.u16OffsetLow
            | ((uint32_t)pDesc->Legacy.Gate.u16OffsetHigh << 16)
            | ((uint64_t)pDesc->Long.Gate.u32OffsetTop    << 32);

    /* Perform DPL checks on the gate descriptor. */
    if (   pDesc->Legacy.Gate.u2Dpl < IEM_GET_CPL(pVCpu)
        || pDesc->Legacy.Gate.u2Dpl < (uSel & X86_SEL_RPL))
    {
        Log(("BranchCallGate invalid priv. uSel=%04x Gate DPL=%d CPL=%u Sel RPL=%u -> #GP\n", uSel, pDesc->Legacy.Gate.u2Dpl,
             IEM_GET_CPL(pVCpu), (uSel & X86_SEL_RPL)));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
    }

    /** @todo does this catch NULL selectors, too? */
    if (!pDesc->Legacy.Gen.u1Present)
    {
        Log(("BranchCallGate Gate not present uSel=%04x -> #NP\n", uSel));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, uSel);
    }

    /*
     * Fetch the target CS descriptor from the GDT or LDT.
     */
    uNewCS = pDesc->Legacy.Gate.u16Sel;
    rcStrict = iemMemFetchSelDesc(pVCpu, &DescCS, uNewCS, X86_XCPT_GP);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /* Target CS must be a code selector. */
    if (   !DescCS.Legacy.Gen.u1DescType
        || !(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_CODE) )
    {
        Log(("BranchCallGate %04x:%08RX64 -> not a code selector (u1DescType=%u u4Type=%#x).\n",
             uNewCS, uNewRip, DescCS.Legacy.Gen.u1DescType, DescCS.Legacy.Gen.u4Type));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCS);
    }

    /* Privilege checks on target CS. */
    if (enmBranch == IEMBRANCH_JUMP)
    {
        if (DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_CONF)
        {
            if (DescCS.Legacy.Gen.u2Dpl > IEM_GET_CPL(pVCpu))
            {
                Log(("BranchCallGate jump (conforming) bad DPL uNewCS=%04x Gate DPL=%d CPL=%u -> #GP\n",
                     uNewCS, DescCS.Legacy.Gen.u2Dpl, IEM_GET_CPL(pVCpu)));
                return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCS);
            }
        }
        else
        {
            if (DescCS.Legacy.Gen.u2Dpl != IEM_GET_CPL(pVCpu))
            {
                Log(("BranchCallGate jump (non-conforming) bad DPL uNewCS=%04x Gate DPL=%d CPL=%u -> #GP\n",
                     uNewCS, DescCS.Legacy.Gen.u2Dpl, IEM_GET_CPL(pVCpu)));
                return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCS);
            }
        }
    }
    else
    {
        Assert(enmBranch == IEMBRANCH_CALL);
        if (DescCS.Legacy.Gen.u2Dpl > IEM_GET_CPL(pVCpu))
        {
            Log(("BranchCallGate call invalid priv. uNewCS=%04x Gate DPL=%d CPL=%u -> #GP\n",
                 uNewCS, DescCS.Legacy.Gen.u2Dpl, IEM_GET_CPL(pVCpu)));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCS & X86_SEL_MASK_OFF_RPL);
        }
    }

    /* Additional long mode checks. */
    if (IEM_IS_LONG_MODE(pVCpu))
    {
        if (!DescCS.Legacy.Gen.u1Long)
        {
            Log(("BranchCallGate uNewCS %04x -> not a 64-bit code segment.\n", uNewCS));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCS);
        }

        /* L vs D. */
        if (   DescCS.Legacy.Gen.u1Long
            && DescCS.Legacy.Gen.u1DefBig)
        {
            Log(("BranchCallGate uNewCS %04x -> both L and D are set.\n", uNewCS));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCS);
        }
    }

    if (!DescCS.Legacy.Gate.u1Present)
    {
        Log(("BranchCallGate target CS is not present. uSel=%04x uNewCS=%04x -> #NP(CS)\n", uSel, uNewCS));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, uNewCS);
    }

    if (enmBranch == IEMBRANCH_JUMP)
    {
        /** @todo This is very similar to regular far jumps; merge! */
        /* Jumps are fairly simple... */

        /* Chop the high bits off if 16-bit gate (Intel says so). */
        if (pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_286_CALL_GATE)
            uNewRip = (uint16_t)uNewRip;

        /* Limit check for non-long segments. */
        cbLimit = X86DESC_LIMIT_G(&DescCS.Legacy);
        if (DescCS.Legacy.Gen.u1Long)
            u64Base = 0;
        else
        {
            if (uNewRip > cbLimit)
            {
                Log(("BranchCallGate jump %04x:%08RX64 -> out of bounds (%#x) -> #GP(0)\n", uNewCS, uNewRip, cbLimit));
                return iemRaiseGeneralProtectionFaultBySelector(pVCpu, 0);
            }
            u64Base = X86DESC_BASE(&DescCS.Legacy);
        }

        /* Canonical address check. */
        if (!IEM_IS_CANONICAL(uNewRip))
        {
            Log(("BranchCallGate jump %04x:%016RX64 - not canonical -> #GP\n", uNewCS, uNewRip));
            return iemRaiseNotCanonical(pVCpu);
        }

        /*
         * Ok, everything checked out fine.  Now set the accessed bit before
         * committing the result into CS, CSHID and RIP.
         */
        if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
        {
            rcStrict = iemMemMarkSelDescAccessed(pVCpu, uNewCS);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            /** @todo check what VT-x and AMD-V does. */
            DescCS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
        }

        /* commit */
        pVCpu->cpum.GstCtx.rip         = uNewRip;
        pVCpu->cpum.GstCtx.cs.Sel      = uNewCS & X86_SEL_MASK_OFF_RPL;
        pVCpu->cpum.GstCtx.cs.Sel     |= IEM_GET_CPL(pVCpu); /** @todo is this right for conforming segs? or in general? */
        pVCpu->cpum.GstCtx.cs.ValidSel = pVCpu->cpum.GstCtx.cs.Sel;
        pVCpu->cpum.GstCtx.cs.fFlags   = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.cs.Attr.u   = X86DESC_GET_HID_ATTR(&DescCS.Legacy);
        pVCpu->cpum.GstCtx.cs.u32Limit = cbLimit;
        pVCpu->cpum.GstCtx.cs.u64Base  = u64Base;
    }
    else
    {
        Assert(enmBranch == IEMBRANCH_CALL);
        /* Calls are much more complicated. */

        if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_CONF) && (DescCS.Legacy.Gen.u2Dpl < IEM_GET_CPL(pVCpu)))
        {
            /* More privilege. This is the fun part. */
            Assert(!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_CONF));    /* Filtered out above. */

            /*
             * Determine new SS:rSP from the TSS.
             */
            Assert(!pVCpu->cpum.GstCtx.tr.Attr.n.u1DescType);

            /* Figure out where the new stack pointer is stored in the TSS. */
            uint8_t const uNewCSDpl = DescCS.Legacy.Gen.u2Dpl;
            uint16_t      offNewStack;    /* Offset of new stack in TSS. */
            uint16_t      cbNewStack;     /* Number of bytes the stack information takes up in TSS. */
            if (!IEM_IS_LONG_MODE(pVCpu))
            {
                if (pVCpu->cpum.GstCtx.tr.Attr.n.u4Type == X86_SEL_TYPE_SYS_386_TSS_BUSY)
                {
                    offNewStack = RT_UOFFSETOF(X86TSS32, esp0) + uNewCSDpl * 8;
                    cbNewStack  = RT_SIZEOFMEMB(X86TSS32, esp0) + RT_SIZEOFMEMB(X86TSS32, ss0);
                }
                else
                {
                    Assert(pVCpu->cpum.GstCtx.tr.Attr.n.u4Type == X86_SEL_TYPE_SYS_286_TSS_BUSY);
                    offNewStack = RT_UOFFSETOF(X86TSS16, sp0) + uNewCSDpl * 4;
                    cbNewStack  = RT_SIZEOFMEMB(X86TSS16, sp0) + RT_SIZEOFMEMB(X86TSS16, ss0);
                }
            }
            else
            {
                Assert(pVCpu->cpum.GstCtx.tr.Attr.n.u4Type == AMD64_SEL_TYPE_SYS_TSS_BUSY);
                offNewStack = RT_UOFFSETOF(X86TSS64, rsp0) + uNewCSDpl * RT_SIZEOFMEMB(X86TSS64, rsp0);
                cbNewStack  = RT_SIZEOFMEMB(X86TSS64, rsp0);
            }

            /* Check against TSS limit. */
            if ((uint16_t)(offNewStack + cbNewStack - 1) > pVCpu->cpum.GstCtx.tr.u32Limit)
            {
                Log(("BranchCallGate inner stack past TSS limit - %u > %u -> #TS(TSS)\n", offNewStack + cbNewStack - 1, pVCpu->cpum.GstCtx.tr.u32Limit));
                return iemRaiseTaskSwitchFaultBySelector(pVCpu, pVCpu->cpum.GstCtx.tr.Sel);
            }

            uint8_t     bUnmapInfo;
            RTPTRUNION  uPtrTss;
            RTGCPTR     GCPtrTss = pVCpu->cpum.GstCtx.tr.u64Base + offNewStack;
            rcStrict = iemMemMap(pVCpu, &uPtrTss.pv, &bUnmapInfo, cbNewStack, UINT8_MAX, GCPtrTss, IEM_ACCESS_SYS_R, 0);
            if (rcStrict != VINF_SUCCESS)
            {
                Log(("BranchCallGate: TSS mapping failed (%Rrc)\n", VBOXSTRICTRC_VAL(rcStrict)));
                return rcStrict;
            }

            RTSEL       uNewSS;
            if (!IEM_IS_LONG_MODE(pVCpu))
            {
                if (pVCpu->cpum.GstCtx.tr.Attr.n.u4Type == X86_SEL_TYPE_SYS_386_TSS_BUSY)
                {
                    uNewRsp = uPtrTss.pu32[0];
                    uNewSS  = uPtrTss.pu16[2];
                }
                else
                {
                    Assert(pVCpu->cpum.GstCtx.tr.Attr.n.u4Type == X86_SEL_TYPE_SYS_286_TSS_BUSY);
                    uNewRsp = uPtrTss.pu16[0];
                    uNewSS  = uPtrTss.pu16[1];
                }
            }
            else
            {
                Assert(pVCpu->cpum.GstCtx.tr.Attr.n.u4Type == AMD64_SEL_TYPE_SYS_TSS_BUSY);
                /* SS will be a NULL selector, but that's valid. */
                uNewRsp = uPtrTss.pu64[0];
                uNewSS  = uNewCSDpl;
            }

            /* Done with the TSS now. */
            rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
            if (rcStrict != VINF_SUCCESS)
            {
                Log(("BranchCallGate: TSS unmapping failed (%Rrc)\n", VBOXSTRICTRC_VAL(rcStrict)));
                return rcStrict;
            }

            /* Only used outside of long mode. */
            uint8_t const cbWords = pDesc->Legacy.Gate.u5ParmCount;

            /* If EFER.LMA is 0, there's extra work to do. */
            IEMSELDESC DescSS;
            if (!IEM_IS_LONG_MODE(pVCpu))
            {
                if ((uNewSS & X86_SEL_MASK_OFF_RPL) == 0)
                {
                    Log(("BranchCallGate new SS NULL -> #TS(NewSS)\n"));
                    return iemRaiseTaskSwitchFaultBySelector(pVCpu, uNewSS);
                }

                /* Grab the new SS descriptor. */
                rcStrict = iemMemFetchSelDesc(pVCpu, &DescSS, uNewSS, X86_XCPT_SS);
                if (rcStrict != VINF_SUCCESS)
                    return rcStrict;

                /* Ensure that CS.DPL == SS.RPL == SS.DPL. */
                if (   (DescCS.Legacy.Gen.u2Dpl != (uNewSS & X86_SEL_RPL))
                    || (DescCS.Legacy.Gen.u2Dpl != DescSS.Legacy.Gen.u2Dpl))
                {
                    Log(("BranchCallGate call bad RPL/DPL uNewSS=%04x SS DPL=%d CS DPL=%u -> #TS(NewSS)\n",
                         uNewSS, DescCS.Legacy.Gen.u2Dpl, DescCS.Legacy.Gen.u2Dpl));
                    return iemRaiseTaskSwitchFaultBySelector(pVCpu, uNewSS);
                }

                /* Ensure new SS is a writable data segment. */
                if ((DescSS.Legacy.Gen.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_WRITE)) != X86_SEL_TYPE_WRITE)
                {
                    Log(("BranchCallGate call new SS -> not a writable data selector (u4Type=%#x)\n", DescSS.Legacy.Gen.u4Type));
                    return iemRaiseTaskSwitchFaultBySelector(pVCpu, uNewSS);
                }

                if (!DescSS.Legacy.Gen.u1Present)
                {
                    Log(("BranchCallGate New stack not present uSel=%04x -> #SS(NewSS)\n", uNewSS));
                    return iemRaiseStackSelectorNotPresentBySelector(pVCpu, uNewSS);
                }
                if (pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_386_CALL_GATE)
                    cbNewStack = (uint16_t)sizeof(uint32_t) * (4 + cbWords);
                else
                    cbNewStack = (uint16_t)sizeof(uint16_t) * (4 + cbWords);
            }
            else
            {
                /* Just grab the new (NULL) SS descriptor. */
                /** @todo testcase: Check whether the zero GDT entry is actually loaded here
                 *        like we do... */
                rcStrict = iemMemFetchSelDesc(pVCpu, &DescSS, uNewSS, X86_XCPT_SS);
                if (rcStrict != VINF_SUCCESS)
                    return rcStrict;

                cbNewStack = sizeof(uint64_t) * 4;
            }

            /** @todo According to Intel, new stack is checked for enough space first,
             *        then switched. According to AMD, the stack is switched first and
             *        then pushes might fault!
             *        NB: OS/2 Warp 3/4 actively relies on the fact that possible
             *        incoming stack \#PF happens before actual stack switch. AMD is
             *        either lying or implicitly assumes that new state is committed
             *        only if and when an instruction doesn't fault.
             */

            /** @todo According to AMD, CS is loaded first, then SS.
             *        According to Intel, it's the other way around!?
             */

            /** @todo Intel and AMD disagree on when exactly the CPL changes! */

            /* Set the accessed bit before committing new SS. */
            if (!(DescSS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
            {
                rcStrict = iemMemMarkSelDescAccessed(pVCpu, uNewSS);
                if (rcStrict != VINF_SUCCESS)
                    return rcStrict;
                DescSS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
            }

            /* Remember the old SS:rSP and their linear address. */
            RTSEL const    uOldSS  = pVCpu->cpum.GstCtx.ss.Sel;
            uint64_t const uOldRsp = pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig ? pVCpu->cpum.GstCtx.rsp : pVCpu->cpum.GstCtx.sp;

            RTGCPTR const GCPtrParmWds = pVCpu->cpum.GstCtx.ss.u64Base + uOldRsp;

            /* HACK ALERT! Probe if the write to the new stack will succeed. May #SS(NewSS)
                           or #PF, the former is not implemented in this workaround. */
            /** @todo Proper fix callgate target stack exceptions. */
            /** @todo testcase: Cover callgates with partially or fully inaccessible
             *        target stacks. */
            void    *pvNewFrame;
            RTGCPTR  GCPtrNewStack = X86DESC_BASE(&DescSS.Legacy) + uNewRsp - cbNewStack;
            rcStrict = iemMemMap(pVCpu, &pvNewFrame, &bUnmapInfo, cbNewStack, UINT8_MAX, GCPtrNewStack, IEM_ACCESS_SYS_RW, 0);
            if (rcStrict != VINF_SUCCESS)
            {
                Log(("BranchCallGate: Incoming stack (%04x:%08RX64) not accessible, rc=%Rrc\n", uNewSS, uNewRsp, VBOXSTRICTRC_VAL(rcStrict)));
                return rcStrict;
            }
            rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
            if (rcStrict != VINF_SUCCESS)
            {
                Log(("BranchCallGate: New stack probe unmapping failed (%Rrc)\n", VBOXSTRICTRC_VAL(rcStrict)));
                return rcStrict;
            }

            /* Commit new SS:rSP. */
            pVCpu->cpum.GstCtx.ss.Sel      = uNewSS;
            pVCpu->cpum.GstCtx.ss.ValidSel = uNewSS;
            pVCpu->cpum.GstCtx.ss.Attr.u   = X86DESC_GET_HID_ATTR(&DescSS.Legacy);
            pVCpu->cpum.GstCtx.ss.u32Limit = X86DESC_LIMIT_G(&DescSS.Legacy);
            pVCpu->cpum.GstCtx.ss.u64Base  = X86DESC_BASE(&DescSS.Legacy);
            pVCpu->cpum.GstCtx.ss.fFlags   = CPUMSELREG_FLAGS_VALID;
            pVCpu->cpum.GstCtx.rsp         = uNewRsp;
            IEM_SET_CPL(pVCpu, uNewCSDpl); /** @todo Are the parameter words accessed using the new CPL or the old CPL? */
            Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.ss));
            CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_HIDDEN_SEL_REGS);

            /* At this point the stack access must not fail because new state was already committed. */
            /** @todo this can still fail due to SS.LIMIT not check.   */
            uint8_t bUnmapInfoRet;
            rcStrict = iemMemStackPushBeginSpecial(pVCpu, cbNewStack,
                                                   IEM_IS_LONG_MODE(pVCpu) ? 7
                                                   : pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_386_CALL_GATE ? 3 : 1,
                                                   &uPtrRet.pv, &bUnmapInfoRet, &uNewRsp);
            AssertMsgReturn(rcStrict == VINF_SUCCESS, ("BranchCallGate: New stack mapping failed (%Rrc)\n", VBOXSTRICTRC_VAL(rcStrict)),
                            VERR_INTERNAL_ERROR_5);

            if (!IEM_IS_LONG_MODE(pVCpu))
            {
                if (pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_386_CALL_GATE)
                {
                    if (cbWords)
                    {
                        /* Map the relevant chunk of the old stack. */
                        RTPTRUNION uPtrParmWds;
                        rcStrict = iemMemMap(pVCpu, &uPtrParmWds.pv, &bUnmapInfo, cbWords * 4, UINT8_MAX, GCPtrParmWds,
                                             IEM_ACCESS_DATA_R, 0 /** @todo Can uNewCSDpl == 3? Then we need alignment mask here! */);
                        if (rcStrict != VINF_SUCCESS)
                        {
                            Log(("BranchCallGate: Old stack mapping (32-bit) failed (%Rrc)\n", VBOXSTRICTRC_VAL(rcStrict)));
                            return rcStrict;
                        }

                        /* Copy the parameter (d)words. */
                        for (int i = 0; i < cbWords; ++i)
                            uPtrRet.pu32[2 + i] = uPtrParmWds.pu32[i];

                        /* Unmap the old stack. */
                        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
                        if (rcStrict != VINF_SUCCESS)
                        {
                            Log(("BranchCallGate: Old stack unmapping (32-bit) failed (%Rrc)\n", VBOXSTRICTRC_VAL(rcStrict)));
                            return rcStrict;
                        }
                    }

                    /* Push the old CS:rIP. */
                    uPtrRet.pu32[0] = pVCpu->cpum.GstCtx.eip + cbInstr;
                    uPtrRet.pu32[1] = pVCpu->cpum.GstCtx.cs.Sel; /** @todo Testcase: What is written to the high word when pushing CS? */

                    /* Push the old SS:rSP. */
                    uPtrRet.pu32[2 + cbWords + 0] = uOldRsp;
                    uPtrRet.pu32[2 + cbWords + 1] = uOldSS;
                }
                else
                {
                    Assert(pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_286_CALL_GATE);

                    if (cbWords)
                    {
                        /* Map the relevant chunk of the old stack. */
                        RTPTRUNION uPtrParmWds;
                        rcStrict = iemMemMap(pVCpu, &uPtrParmWds.pv, &bUnmapInfo, cbWords * 2, UINT8_MAX, GCPtrParmWds,
                                             IEM_ACCESS_DATA_R, 0 /** @todo Can uNewCSDpl == 3? Then we need alignment mask here! */);
                        if (rcStrict != VINF_SUCCESS)
                        {
                            Log(("BranchCallGate: Old stack mapping (16-bit) failed (%Rrc)\n", VBOXSTRICTRC_VAL(rcStrict)));
                            return rcStrict;
                        }

                        /* Copy the parameter words. */
                        for (int i = 0; i < cbWords; ++i)
                            uPtrRet.pu16[2 + i] = uPtrParmWds.pu16[i];

                        /* Unmap the old stack. */
                        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
                        if (rcStrict != VINF_SUCCESS)
                        {
                            Log(("BranchCallGate: Old stack unmapping (32-bit) failed (%Rrc)\n", VBOXSTRICTRC_VAL(rcStrict)));
                            return rcStrict;
                        }
                    }

                    /* Push the old CS:rIP. */
                    uPtrRet.pu16[0] = pVCpu->cpum.GstCtx.ip + cbInstr;
                    uPtrRet.pu16[1] = pVCpu->cpum.GstCtx.cs.Sel;

                    /* Push the old SS:rSP. */
                    uPtrRet.pu16[2 + cbWords + 0] = uOldRsp;
                    uPtrRet.pu16[2 + cbWords + 1] = uOldSS;
                }
            }
            else
            {
                Assert(pDesc->Legacy.Gate.u4Type == AMD64_SEL_TYPE_SYS_CALL_GATE);

                /* For 64-bit gates, no parameters are copied. Just push old SS:rSP and CS:rIP. */
                uPtrRet.pu64[0] = pVCpu->cpum.GstCtx.rip + cbInstr;
                uPtrRet.pu64[1] = pVCpu->cpum.GstCtx.cs.Sel; /** @todo Testcase: What is written to the high words when pushing CS? */
                uPtrRet.pu64[2] = uOldRsp;
                uPtrRet.pu64[3] = uOldSS;       /** @todo Testcase: What is written to the high words when pushing SS? */
            }

            rcStrict = iemMemStackPushCommitSpecial(pVCpu, bUnmapInfoRet, uNewRsp);
            if (rcStrict != VINF_SUCCESS)
            {
                Log(("BranchCallGate: New stack unmapping failed (%Rrc)\n", VBOXSTRICTRC_VAL(rcStrict)));
                return rcStrict;
            }

            /* Chop the high bits off if 16-bit gate (Intel says so). */
            if (pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_286_CALL_GATE)
                uNewRip = (uint16_t)uNewRip;

            /* Limit / canonical check. */
            cbLimit = X86DESC_LIMIT_G(&DescCS.Legacy);
            if (!IEM_IS_LONG_MODE(pVCpu))
            {
                if (uNewRip > cbLimit)
                {
                    Log(("BranchCallGate %04x:%08RX64 -> out of bounds (%#x)\n", uNewCS, uNewRip, cbLimit));
                    return iemRaiseGeneralProtectionFaultBySelector(pVCpu, 0);
                }
                u64Base = X86DESC_BASE(&DescCS.Legacy);
            }
            else
            {
                Assert(pDesc->Legacy.Gate.u4Type == AMD64_SEL_TYPE_SYS_CALL_GATE);
                if (!IEM_IS_CANONICAL(uNewRip))
                {
                    Log(("BranchCallGate call %04x:%016RX64 - not canonical -> #GP\n", uNewCS, uNewRip));
                    return iemRaiseNotCanonical(pVCpu);
                }
                u64Base = 0;
            }

            /*
             * Now set the accessed bit before
             * writing the return address to the stack and committing the result into
             * CS, CSHID and RIP.
             */
            /** @todo Testcase: Need to check WHEN exactly the accessed bit is set. */
            if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
            {
                rcStrict = iemMemMarkSelDescAccessed(pVCpu, uNewCS);
                if (rcStrict != VINF_SUCCESS)
                    return rcStrict;
                /** @todo check what VT-x and AMD-V does. */
                DescCS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
            }

            /* Commit new CS:rIP. */
            pVCpu->cpum.GstCtx.rip         = uNewRip;
            pVCpu->cpum.GstCtx.cs.Sel      = uNewCS & X86_SEL_MASK_OFF_RPL;
            pVCpu->cpum.GstCtx.cs.Sel     |= IEM_GET_CPL(pVCpu);
            pVCpu->cpum.GstCtx.cs.ValidSel = pVCpu->cpum.GstCtx.cs.Sel;
            pVCpu->cpum.GstCtx.cs.fFlags   = CPUMSELREG_FLAGS_VALID;
            pVCpu->cpum.GstCtx.cs.Attr.u   = X86DESC_GET_HID_ATTR(&DescCS.Legacy);
            pVCpu->cpum.GstCtx.cs.u32Limit = cbLimit;
            pVCpu->cpum.GstCtx.cs.u64Base  = u64Base;
        }
        else
        {
            /* Same privilege. */
            /** @todo This is very similar to regular far calls; merge! */

            /* Check stack first - may #SS(0). */
            /** @todo check how gate size affects pushing of CS! Does callf 16:32 in
             *        16-bit code cause a two or four byte CS to be pushed? */
            uint8_t bUnmapInfoRet;
            rcStrict = iemMemStackPushBeginSpecial(pVCpu,
                                                   IEM_IS_LONG_MODE(pVCpu) ? 8+8
                                                   : pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_386_CALL_GATE ? 4+4 : 2+2,
                                                   IEM_IS_LONG_MODE(pVCpu) ? 7
                                                   : pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_386_CALL_GATE ? 3 : 2,
                                                   &uPtrRet.pv, &bUnmapInfoRet, &uNewRsp);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            /* Chop the high bits off if 16-bit gate (Intel says so). */
            if (pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_286_CALL_GATE)
                uNewRip = (uint16_t)uNewRip;

            /* Limit / canonical check. */
            cbLimit = X86DESC_LIMIT_G(&DescCS.Legacy);
            if (!IEM_IS_LONG_MODE(pVCpu))
            {
                if (uNewRip > cbLimit)
                {
                    Log(("BranchCallGate %04x:%08RX64 -> out of bounds (%#x)\n", uNewCS, uNewRip, cbLimit));
                    return iemRaiseGeneralProtectionFaultBySelector(pVCpu, 0);
                }
                u64Base = X86DESC_BASE(&DescCS.Legacy);
            }
            else
            {
                if (!IEM_IS_CANONICAL(uNewRip))
                {
                    Log(("BranchCallGate call %04x:%016RX64 - not canonical -> #GP\n", uNewCS, uNewRip));
                    return iemRaiseNotCanonical(pVCpu);
                }
                u64Base = 0;
            }

            /*
             * Now set the accessed bit before
             * writing the return address to the stack and committing the result into
             * CS, CSHID and RIP.
             */
            /** @todo Testcase: Need to check WHEN exactly the accessed bit is set. */
            if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
            {
                rcStrict = iemMemMarkSelDescAccessed(pVCpu, uNewCS);
                if (rcStrict != VINF_SUCCESS)
                    return rcStrict;
                /** @todo check what VT-x and AMD-V does. */
                DescCS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
            }

            /* stack */
            if (!IEM_IS_LONG_MODE(pVCpu))
            {
                if (pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_386_CALL_GATE)
                {
                    uPtrRet.pu32[0] = pVCpu->cpum.GstCtx.eip + cbInstr;
                    uPtrRet.pu32[1] = pVCpu->cpum.GstCtx.cs.Sel; /** @todo Testcase: What is written to the high word when pushing CS? */
                }
                else
                {
                    Assert(pDesc->Legacy.Gate.u4Type == X86_SEL_TYPE_SYS_286_CALL_GATE);
                    uPtrRet.pu16[0] = pVCpu->cpum.GstCtx.ip + cbInstr;
                    uPtrRet.pu16[1] = pVCpu->cpum.GstCtx.cs.Sel;
                }
            }
            else
            {
                Assert(pDesc->Legacy.Gate.u4Type == AMD64_SEL_TYPE_SYS_CALL_GATE);
                uPtrRet.pu64[0] = pVCpu->cpum.GstCtx.rip + cbInstr;
                uPtrRet.pu64[1] = pVCpu->cpum.GstCtx.cs.Sel; /** @todo Testcase: What is written to the high words when pushing CS? */
            }

            rcStrict = iemMemStackPushCommitSpecial(pVCpu, bUnmapInfoRet, uNewRsp);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            /* commit */
            pVCpu->cpum.GstCtx.rip         = uNewRip;
            pVCpu->cpum.GstCtx.cs.Sel      = uNewCS & X86_SEL_MASK_OFF_RPL;
            pVCpu->cpum.GstCtx.cs.Sel     |= IEM_GET_CPL(pVCpu);
            pVCpu->cpum.GstCtx.cs.ValidSel = pVCpu->cpum.GstCtx.cs.Sel;
            pVCpu->cpum.GstCtx.cs.fFlags   = CPUMSELREG_FLAGS_VALID;
            pVCpu->cpum.GstCtx.cs.Attr.u   = X86DESC_GET_HID_ATTR(&DescCS.Legacy);
            pVCpu->cpum.GstCtx.cs.u32Limit = cbLimit;
            pVCpu->cpum.GstCtx.cs.u64Base  = u64Base;
        }
    }
    pVCpu->cpum.GstCtx.eflags.Bits.u1RF = 0;

    iemRecalcExecModeAndCplAndAcFlags(pVCpu);

/** @todo single stepping   */

    /* Flush the prefetch buffer. */
    IEM_FLUSH_PREFETCH_HEAVY(pVCpu, cbInstr);
    return VINF_SUCCESS;
#endif /* IEM_IMPLEMENTS_CALLGATE */
}


/**
 * Implements far jumps and calls thru system selectors.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling thread.
 * @param   cbInstr         The current instruction length.
 * @param   uSel            The selector.
 * @param   enmBranch       The kind of branching we're performing.
 * @param   enmEffOpSize    The effective operand size.
 * @param   pDesc           The descriptor corresponding to @a uSel.
 */
static VBOXSTRICTRC iemCImpl_BranchSysSel(PVMCPUCC pVCpu, uint8_t cbInstr, uint16_t uSel, IEMBRANCH enmBranch,
                                          IEMMODE enmEffOpSize, PIEMSELDESC pDesc)
{
    Assert(enmBranch == IEMBRANCH_JUMP || enmBranch == IEMBRANCH_CALL);
    Assert((uSel & X86_SEL_MASK_OFF_RPL));
    IEM_CTX_IMPORT_RET(pVCpu, IEM_CPUMCTX_EXTRN_XCPT_MASK);

    if (IEM_IS_LONG_MODE(pVCpu))
        switch (pDesc->Legacy.Gen.u4Type)
        {
            case AMD64_SEL_TYPE_SYS_CALL_GATE:
                return iemCImpl_BranchCallGate(pVCpu, cbInstr, uSel, enmBranch, enmEffOpSize, pDesc);

            default:
            case AMD64_SEL_TYPE_SYS_LDT:
            case AMD64_SEL_TYPE_SYS_TSS_BUSY:
            case AMD64_SEL_TYPE_SYS_TSS_AVAIL:
            case AMD64_SEL_TYPE_SYS_TRAP_GATE:
            case AMD64_SEL_TYPE_SYS_INT_GATE:
                Log(("branch %04x -> wrong sys selector (64-bit): %d\n", uSel, pDesc->Legacy.Gen.u4Type));
                return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
        }

    switch (pDesc->Legacy.Gen.u4Type)
    {
        case X86_SEL_TYPE_SYS_286_CALL_GATE:
        case X86_SEL_TYPE_SYS_386_CALL_GATE:
            return iemCImpl_BranchCallGate(pVCpu, cbInstr, uSel, enmBranch, enmEffOpSize, pDesc);

        case X86_SEL_TYPE_SYS_TASK_GATE:
            return iemCImpl_BranchTaskGate(pVCpu, cbInstr, uSel, enmBranch, enmEffOpSize, pDesc);

        case X86_SEL_TYPE_SYS_286_TSS_AVAIL:
        case X86_SEL_TYPE_SYS_386_TSS_AVAIL:
            return iemCImpl_BranchTaskSegment(pVCpu, cbInstr, uSel, enmBranch, enmEffOpSize, pDesc);

        case X86_SEL_TYPE_SYS_286_TSS_BUSY:
            Log(("branch %04x -> busy 286 TSS\n", uSel));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);

        case X86_SEL_TYPE_SYS_386_TSS_BUSY:
            Log(("branch %04x -> busy 386 TSS\n", uSel));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);

        default:
        case X86_SEL_TYPE_SYS_LDT:
        case X86_SEL_TYPE_SYS_286_INT_GATE:
        case X86_SEL_TYPE_SYS_286_TRAP_GATE:
        case X86_SEL_TYPE_SYS_386_INT_GATE:
        case X86_SEL_TYPE_SYS_386_TRAP_GATE:
            Log(("branch %04x -> wrong sys selector: %d\n", uSel, pDesc->Legacy.Gen.u4Type));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
    }
}


/**
 * Implements far jumps.
 *
 * @param   uSel            The selector.
 * @param   offSeg          The segment offset.
 * @param   enmEffOpSize    The effective operand size.
 */
IEM_CIMPL_DEF_3(iemCImpl_FarJmp, uint16_t, uSel, uint64_t, offSeg, IEMMODE, enmEffOpSize)
{
    NOREF(cbInstr);
    Assert(offSeg <= UINT32_MAX || (!IEM_IS_GUEST_CPU_AMD(pVCpu) && IEM_IS_64BIT_CODE(pVCpu)));

    /*
     * Real mode and V8086 mode are easy.  The only snag seems to be that
     * CS.limit doesn't change and the limit check is done against the current
     * limit.
     */
    /** @todo Robert Collins claims (The Segment Descriptor Cache, DDJ August
     *        1998) that up to and including the Intel 486, far control
     *        transfers in real mode set default CS attributes (0x93) and also
     *        set a 64K segment limit. Starting with the Pentium, the
     *        attributes and limit are left alone but the access rights are
     *        ignored. We only implement the Pentium+ behavior.
     *  */
    if (IEM_IS_REAL_OR_V86_MODE(pVCpu))
    {
        Assert(enmEffOpSize == IEMMODE_16BIT || enmEffOpSize == IEMMODE_32BIT);
        if (offSeg > pVCpu->cpum.GstCtx.cs.u32Limit)
        {
            Log(("iemCImpl_FarJmp: 16-bit limit\n"));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }

        if (enmEffOpSize == IEMMODE_16BIT) /** @todo WRONG, must pass this. */
            pVCpu->cpum.GstCtx.rip       = offSeg;
        else
            pVCpu->cpum.GstCtx.rip       = offSeg & UINT16_MAX;
        pVCpu->cpum.GstCtx.cs.Sel        = uSel;
        pVCpu->cpum.GstCtx.cs.ValidSel   = uSel;
        pVCpu->cpum.GstCtx.cs.fFlags     = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.cs.u64Base    = (uint32_t)uSel << 4;

        /* Update the FLAT 32-bit mode flag, if we're in 32-bit unreal mode (unlikely): */
        if (RT_LIKELY(!IEM_IS_32BIT_CODE(pVCpu)))
        { /* likely */ }
        else if (uSel != 0)
            pVCpu->iem.s.fExec &= ~IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK;
        else
            pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK)
                               | iemCalc32BitFlatIndicator(pVCpu);

        return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
    }

    /*
     * Protected mode. Need to parse the specified descriptor...
     */
    if (!(uSel & X86_SEL_MASK_OFF_RPL))
    {
        Log(("jmpf %04x:%08RX64 -> invalid selector, #GP(0)\n", uSel, offSeg));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /* Fetch the descriptor. */
    IEMSELDESC Desc;
    VBOXSTRICTRC rcStrict = iemMemFetchSelDesc(pVCpu, &Desc, uSel, X86_XCPT_GP);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /* Is it there? */
    if (!Desc.Legacy.Gen.u1Present) /** @todo this is probably checked too early. Testcase! */
    {
        Log(("jmpf %04x:%08RX64 -> segment not present\n", uSel, offSeg));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, uSel);
    }

    /*
     * Deal with it according to its type.  We do the standard code selectors
     * here and dispatch the system selectors to worker functions.
     */
    if (!Desc.Legacy.Gen.u1DescType)
        return iemCImpl_BranchSysSel(pVCpu, cbInstr, uSel, IEMBRANCH_JUMP, enmEffOpSize, &Desc);

    /* Only code segments. */
    if (!(Desc.Legacy.Gen.u4Type & X86_SEL_TYPE_CODE))
    {
        Log(("jmpf %04x:%08RX64 -> not a code selector (u4Type=%#x).\n", uSel, offSeg, Desc.Legacy.Gen.u4Type));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
    }

    /* L vs D. */
    if (   Desc.Legacy.Gen.u1Long
        && Desc.Legacy.Gen.u1DefBig
        && IEM_IS_LONG_MODE(pVCpu))
    {
        Log(("jmpf %04x:%08RX64 -> both L and D are set.\n", uSel, offSeg));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
    }

    /* DPL/RPL/CPL check, where conforming segments makes a difference. */
    if (Desc.Legacy.Gen.u4Type & X86_SEL_TYPE_CONF)
    {
        if (IEM_GET_CPL(pVCpu) < Desc.Legacy.Gen.u2Dpl)
        {
            Log(("jmpf %04x:%08RX64 -> DPL violation (conforming); DPL=%d CPL=%u\n",
                 uSel, offSeg, Desc.Legacy.Gen.u2Dpl, IEM_GET_CPL(pVCpu)));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
        }
    }
    else
    {
        if (IEM_GET_CPL(pVCpu) != Desc.Legacy.Gen.u2Dpl)
        {
            Log(("jmpf %04x:%08RX64 -> CPL != DPL; DPL=%d CPL=%u\n", uSel, offSeg, Desc.Legacy.Gen.u2Dpl, IEM_GET_CPL(pVCpu)));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
        }
        if ((uSel & X86_SEL_RPL) > IEM_GET_CPL(pVCpu))
        {
            Log(("jmpf %04x:%08RX64 -> RPL > DPL; RPL=%d CPL=%u\n", uSel, offSeg, (uSel & X86_SEL_RPL), IEM_GET_CPL(pVCpu)));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
        }
    }

    /* Chop the high bits if 16-bit (Intel says so). */
    if (enmEffOpSize == IEMMODE_16BIT)
        offSeg &= UINT16_MAX;

    /* Limit check and get the base.  */
    uint64_t u64Base;
    uint32_t cbLimit = X86DESC_LIMIT_G(&Desc.Legacy);
    if (   !Desc.Legacy.Gen.u1Long
        || !IEM_IS_LONG_MODE(pVCpu))
    {
        if (RT_LIKELY(offSeg <= cbLimit))
            u64Base = X86DESC_BASE(&Desc.Legacy);
        else
        {
            Log(("jmpf %04x:%08RX64 -> out of bounds (%#x)\n", uSel, offSeg, cbLimit));
            /** @todo Intel says this is \#GP(0)! */
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
        }
    }
    else
        u64Base = 0;

    /*
     * Ok, everything checked out fine.  Now set the accessed bit before
     * committing the result into CS, CSHID and RIP.
     */
    if (!(Desc.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
    {
        rcStrict = iemMemMarkSelDescAccessed(pVCpu, uSel);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        /** @todo check what VT-x and AMD-V does. */
        Desc.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
    }

    /* commit */
    pVCpu->cpum.GstCtx.rip = offSeg;
    pVCpu->cpum.GstCtx.cs.Sel         = uSel & X86_SEL_MASK_OFF_RPL;
    pVCpu->cpum.GstCtx.cs.Sel        |= IEM_GET_CPL(pVCpu); /** @todo is this right for conforming segs? or in general? */
    pVCpu->cpum.GstCtx.cs.ValidSel    = pVCpu->cpum.GstCtx.cs.Sel;
    pVCpu->cpum.GstCtx.cs.fFlags      = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.cs.Attr.u      = X86DESC_GET_HID_ATTR(&Desc.Legacy);
    pVCpu->cpum.GstCtx.cs.u32Limit    = cbLimit;
    pVCpu->cpum.GstCtx.cs.u64Base     = u64Base;

    /** @todo check if the hidden bits are loaded correctly for 64-bit
     *        mode.  */

    iemRecalcExecModeAndCplAndAcFlags(pVCpu);

    /* Flush the prefetch buffer. */
    IEM_FLUSH_PREFETCH_HEAVY(pVCpu, cbInstr);

    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Implements far calls.
 *
 * This very similar to iemCImpl_FarJmp.
 *
 * @param   uSel            The selector.
 * @param   offSeg          The segment offset.
 * @param   enmEffOpSize    The operand size (in case we need it).
 */
IEM_CIMPL_DEF_3(iemCImpl_callf, uint16_t, uSel, uint64_t, offSeg, IEMMODE, enmEffOpSize)
{
    VBOXSTRICTRC    rcStrict;
    uint64_t        uNewRsp;
    RTPTRUNION      uPtrRet;
    uint8_t         bUnmapInfo;

    /*
     * Real mode and V8086 mode are easy.  The only snag seems to be that
     * CS.limit doesn't change and the limit check is done against the current
     * limit.
     */
    /** @todo See comment for similar code in iemCImpl_FarJmp */
    if (IEM_IS_REAL_OR_V86_MODE(pVCpu))
    {
        Assert(enmEffOpSize == IEMMODE_16BIT || enmEffOpSize == IEMMODE_32BIT);

        /* Check stack first - may #SS(0). */
        rcStrict = iemMemStackPushBeginSpecial(pVCpu, enmEffOpSize == IEMMODE_32BIT ? 4+4 : 2+2,
                                               enmEffOpSize == IEMMODE_32BIT ? 3 : 1,
                                               &uPtrRet.pv, &bUnmapInfo, &uNewRsp);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        /* Check the target address range. */
/** @todo this must be wrong! Write unreal mode tests! */
        if (offSeg > UINT32_MAX)
            return iemRaiseGeneralProtectionFault0(pVCpu);

        /* Everything is fine, push the return address. */
        if (enmEffOpSize == IEMMODE_16BIT)
        {
            uPtrRet.pu16[0] = pVCpu->cpum.GstCtx.ip + cbInstr;
            uPtrRet.pu16[1] = pVCpu->cpum.GstCtx.cs.Sel;
        }
        else
        {
            uPtrRet.pu32[0] = pVCpu->cpum.GstCtx.eip + cbInstr;
            uPtrRet.pu16[2] = pVCpu->cpum.GstCtx.cs.Sel;
        }
        rcStrict = iemMemStackPushCommitSpecial(pVCpu, bUnmapInfo, uNewRsp);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        /* Branch. */
        pVCpu->cpum.GstCtx.rip           = offSeg;
        pVCpu->cpum.GstCtx.cs.Sel        = uSel;
        pVCpu->cpum.GstCtx.cs.ValidSel   = uSel;
        pVCpu->cpum.GstCtx.cs.fFlags     = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.cs.u64Base    = (uint32_t)uSel << 4;

        return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
    }

    /*
     * Protected mode. Need to parse the specified descriptor...
     */
    if (!(uSel & X86_SEL_MASK_OFF_RPL))
    {
        Log(("callf %04x:%08RX64 -> invalid selector, #GP(0)\n", uSel, offSeg));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /* Fetch the descriptor. */
    IEMSELDESC Desc;
    rcStrict = iemMemFetchSelDesc(pVCpu, &Desc, uSel, X86_XCPT_GP);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /*
     * Deal with it according to its type.  We do the standard code selectors
     * here and dispatch the system selectors to worker functions.
     */
    if (!Desc.Legacy.Gen.u1DescType)
        return iemCImpl_BranchSysSel(pVCpu, cbInstr, uSel, IEMBRANCH_CALL, enmEffOpSize, &Desc);

    /* Only code segments. */
    if (!(Desc.Legacy.Gen.u4Type & X86_SEL_TYPE_CODE))
    {
        Log(("callf %04x:%08RX64 -> not a code selector (u4Type=%#x).\n", uSel, offSeg, Desc.Legacy.Gen.u4Type));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
    }

    /* L vs D. */
    if (   Desc.Legacy.Gen.u1Long
        && Desc.Legacy.Gen.u1DefBig
        && IEM_IS_LONG_MODE(pVCpu))
    {
        Log(("callf %04x:%08RX64 -> both L and D are set.\n", uSel, offSeg));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
    }

    /* DPL/RPL/CPL check, where conforming segments makes a difference. */
    if (Desc.Legacy.Gen.u4Type & X86_SEL_TYPE_CONF)
    {
        if (IEM_GET_CPL(pVCpu) < Desc.Legacy.Gen.u2Dpl)
        {
            Log(("callf %04x:%08RX64 -> DPL violation (conforming); DPL=%d CPL=%u\n",
                 uSel, offSeg, Desc.Legacy.Gen.u2Dpl, IEM_GET_CPL(pVCpu)));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
        }
    }
    else
    {
        if (IEM_GET_CPL(pVCpu) != Desc.Legacy.Gen.u2Dpl)
        {
            Log(("callf %04x:%08RX64 -> CPL != DPL; DPL=%d CPL=%u\n", uSel, offSeg, Desc.Legacy.Gen.u2Dpl, IEM_GET_CPL(pVCpu)));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
        }
        if ((uSel & X86_SEL_RPL) > IEM_GET_CPL(pVCpu))
        {
            Log(("callf %04x:%08RX64 -> RPL > DPL; RPL=%d CPL=%u\n", uSel, offSeg, (uSel & X86_SEL_RPL), IEM_GET_CPL(pVCpu)));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
        }
    }

    /* Is it there? */
    if (!Desc.Legacy.Gen.u1Present)
    {
        Log(("callf %04x:%08RX64 -> segment not present\n", uSel, offSeg));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, uSel);
    }

    /* Check stack first - may #SS(0). */
    /** @todo check how operand prefix affects pushing of CS! Does callf 16:32 in
     *        16-bit code cause a two or four byte CS to be pushed? */
    rcStrict = iemMemStackPushBeginSpecial(pVCpu,
                                           enmEffOpSize == IEMMODE_64BIT ? 8+8 : enmEffOpSize == IEMMODE_32BIT ? 4+4 : 2+2,
                                           enmEffOpSize == IEMMODE_64BIT ? 7   : enmEffOpSize == IEMMODE_32BIT ? 3   : 1,
                                           &uPtrRet.pv, &bUnmapInfo, &uNewRsp);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /* Chop the high bits if 16-bit (Intel says so). */
    if (enmEffOpSize == IEMMODE_16BIT)
        offSeg &= UINT16_MAX;

    /* Limit / canonical check. */
    uint64_t u64Base;
    uint32_t cbLimit = X86DESC_LIMIT_G(&Desc.Legacy);
    if (   !Desc.Legacy.Gen.u1Long
        || !IEM_IS_LONG_MODE(pVCpu))
    {
        if (RT_LIKELY(offSeg <= cbLimit))
            u64Base = X86DESC_BASE(&Desc.Legacy);
        else
        {
            Log(("callf %04x:%08RX64 -> out of bounds (%#x)\n", uSel, offSeg, cbLimit));
            /** @todo Intel says this is \#GP(0)! */
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
        }
    }
    else if (IEM_IS_CANONICAL(offSeg))
        u64Base = 0;
    else
    {
        Log(("callf %04x:%016RX64 - not canonical -> #GP\n", uSel, offSeg));
        return iemRaiseNotCanonical(pVCpu);
    }

    /*
     * Now set the accessed bit before
     * writing the return address to the stack and committing the result into
     * CS, CSHID and RIP.
     */
    /** @todo Testcase: Need to check WHEN exactly the accessed bit is set. */
    if (!(Desc.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
    {
        rcStrict = iemMemMarkSelDescAccessed(pVCpu, uSel);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        /** @todo check what VT-x and AMD-V does. */
        Desc.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
    }

    /* stack */
    if (enmEffOpSize == IEMMODE_16BIT)
    {
        uPtrRet.pu16[0] = pVCpu->cpum.GstCtx.ip + cbInstr;
        uPtrRet.pu16[1] = pVCpu->cpum.GstCtx.cs.Sel;
    }
    else if (enmEffOpSize == IEMMODE_32BIT)
    {
        uPtrRet.pu32[0] = pVCpu->cpum.GstCtx.eip + cbInstr;
        uPtrRet.pu32[1] = pVCpu->cpum.GstCtx.cs.Sel; /** @todo Testcase: What is written to the high word when callf is pushing CS? */
    }
    else
    {
        uPtrRet.pu64[0] = pVCpu->cpum.GstCtx.rip + cbInstr;
        uPtrRet.pu64[1] = pVCpu->cpum.GstCtx.cs.Sel; /** @todo Testcase: What is written to the high words when callf is pushing CS? */
    }
    rcStrict = iemMemStackPushCommitSpecial(pVCpu, bUnmapInfo, uNewRsp);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /* commit */
    pVCpu->cpum.GstCtx.rip = offSeg;
    pVCpu->cpum.GstCtx.cs.Sel         = uSel & X86_SEL_MASK_OFF_RPL;
    pVCpu->cpum.GstCtx.cs.Sel        |= IEM_GET_CPL(pVCpu);
    pVCpu->cpum.GstCtx.cs.ValidSel    = pVCpu->cpum.GstCtx.cs.Sel;
    pVCpu->cpum.GstCtx.cs.fFlags      = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.cs.Attr.u      = X86DESC_GET_HID_ATTR(&Desc.Legacy);
    pVCpu->cpum.GstCtx.cs.u32Limit    = cbLimit;
    pVCpu->cpum.GstCtx.cs.u64Base     = u64Base;

    /** @todo check if the hidden bits are loaded correctly for 64-bit
     *        mode.  */

    iemRecalcExecModeAndCplAndAcFlags(pVCpu);

    /* Flush the prefetch buffer. */
    IEM_FLUSH_PREFETCH_HEAVY(pVCpu, cbInstr);

    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Implements retf.
 *
 * @param   enmEffOpSize    The effective operand size.
 * @param   cbPop           The amount of arguments to pop from the stack
 *                          (bytes).
 */
IEM_CIMPL_DEF_2(iemCImpl_retf, IEMMODE, enmEffOpSize, uint16_t, cbPop)
{
    NOREF(cbInstr);

    /*
     * Read the stack values first.
     */
    RTUINT64U       NewRsp;
    uint8_t         bUnmapInfo;
    RTCPTRUNION     uPtrFrame;
    uint32_t        cbRetPtr = enmEffOpSize == IEMMODE_16BIT ? 2+2
                             : enmEffOpSize == IEMMODE_32BIT ? 4+4 : 8+8;
    VBOXSTRICTRC rcStrict = iemMemStackPopBeginSpecial(pVCpu, cbRetPtr,
                                                       enmEffOpSize == IEMMODE_16BIT ? 1 : enmEffOpSize == IEMMODE_32BIT ? 3 : 7,
                                                       &uPtrFrame.pv, &bUnmapInfo, &NewRsp.u);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    uint64_t        uNewRip;
    uint16_t        uNewCs;
    if (enmEffOpSize == IEMMODE_16BIT)
    {
        uNewRip = uPtrFrame.pu16[0];
        uNewCs  = uPtrFrame.pu16[1];
    }
    else if (enmEffOpSize == IEMMODE_32BIT)
    {
        uNewRip = uPtrFrame.pu32[0];
        uNewCs  = uPtrFrame.pu16[2];
    }
    else
    {
        uNewRip = uPtrFrame.pu64[0];
        uNewCs  = uPtrFrame.pu16[4];
    }

    rcStrict = iemMemStackPopDoneSpecial(pVCpu, bUnmapInfo);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    { /* extremely likely */ }
    else
        return rcStrict;

    /*
     * Real mode and V8086 mode are easy.
     */
    /** @todo See comment for similar code in iemCImpl_FarJmp */
    if (IEM_IS_REAL_OR_V86_MODE(pVCpu))
    {
        Assert(enmEffOpSize == IEMMODE_32BIT || enmEffOpSize == IEMMODE_16BIT);
        /** @todo check how this is supposed to work if sp=0xfffe. */

        /* Check the limit of the new EIP. */
        /** @todo Intel pseudo code only does the limit check for 16-bit
         *        operands, AMD does not make any distinction. What is right? */
        if (uNewRip > pVCpu->cpum.GstCtx.cs.u32Limit)
            return iemRaiseSelectorBounds(pVCpu, X86_SREG_CS, IEM_ACCESS_INSTRUCTION);

        /* commit the operation. */
        if (cbPop)
            iemRegAddToRspEx(pVCpu, &NewRsp, cbPop);
        pVCpu->cpum.GstCtx.rsp           = NewRsp.u;
        pVCpu->cpum.GstCtx.rip           = uNewRip;
        pVCpu->cpum.GstCtx.cs.Sel        = uNewCs;
        pVCpu->cpum.GstCtx.cs.ValidSel   = uNewCs;
        pVCpu->cpum.GstCtx.cs.fFlags     = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.cs.u64Base    = (uint32_t)uNewCs << 4;
        return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
    }

    /*
     * Protected mode is complicated, of course.
     */
    if (!(uNewCs & X86_SEL_MASK_OFF_RPL))
    {
        Log(("retf %04x:%08RX64 -> invalid selector, #GP(0)\n", uNewCs, uNewRip));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_SREG_MASK | CPUMCTX_EXTRN_GDTR | CPUMCTX_EXTRN_LDTR);

    /* Fetch the descriptor. */
    IEMSELDESC DescCs;
    rcStrict = iemMemFetchSelDesc(pVCpu, &DescCs, uNewCs, X86_XCPT_GP);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /* Can only return to a code selector. */
    if (   !DescCs.Legacy.Gen.u1DescType
        || !(DescCs.Legacy.Gen.u4Type & X86_SEL_TYPE_CODE) )
    {
        Log(("retf %04x:%08RX64 -> not a code selector (u1DescType=%u u4Type=%#x).\n",
             uNewCs, uNewRip, DescCs.Legacy.Gen.u1DescType, DescCs.Legacy.Gen.u4Type));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
    }

    /* L vs D. */
    if (   DescCs.Legacy.Gen.u1Long /** @todo Testcase: far return to a selector with both L and D set. */
        && DescCs.Legacy.Gen.u1DefBig
        && IEM_IS_LONG_MODE(pVCpu))
    {
        Log(("retf %04x:%08RX64 -> both L & D set.\n", uNewCs, uNewRip));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
    }

    /* DPL/RPL/CPL checks. */
    if ((uNewCs & X86_SEL_RPL) < IEM_GET_CPL(pVCpu))
    {
        Log(("retf %04x:%08RX64 -> RPL < CPL(%d).\n", uNewCs, uNewRip, IEM_GET_CPL(pVCpu)));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
    }

    if (DescCs.Legacy.Gen.u4Type & X86_SEL_TYPE_CONF)
    {
        if ((uNewCs & X86_SEL_RPL) < DescCs.Legacy.Gen.u2Dpl)
        {
            Log(("retf %04x:%08RX64 -> DPL violation (conforming); DPL=%u RPL=%u\n",
                 uNewCs, uNewRip, DescCs.Legacy.Gen.u2Dpl, (uNewCs & X86_SEL_RPL)));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
        }
    }
    else
    {
        if ((uNewCs & X86_SEL_RPL) != DescCs.Legacy.Gen.u2Dpl)
        {
            Log(("retf %04x:%08RX64 -> RPL != DPL; DPL=%u RPL=%u\n",
                 uNewCs, uNewRip, DescCs.Legacy.Gen.u2Dpl, (uNewCs & X86_SEL_RPL)));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
        }
    }

    /* Is it there? */
    if (!DescCs.Legacy.Gen.u1Present)
    {
        Log(("retf %04x:%08RX64 -> segment not present\n", uNewCs, uNewRip));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, uNewCs);
    }

    /*
     * Return to outer privilege? (We'll typically have entered via a call gate.)
     */
    if ((uNewCs & X86_SEL_RPL) != IEM_GET_CPL(pVCpu))
    {
        /* Read the outer stack pointer stored *after* the parameters. */
        rcStrict = iemMemStackPopContinueSpecial(pVCpu, cbPop /*off*/, cbRetPtr, &uPtrFrame.pv, &bUnmapInfo, NewRsp.u);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        uint16_t uNewOuterSs;
        RTUINT64U NewOuterRsp;
        if (enmEffOpSize == IEMMODE_16BIT)
        {
            NewOuterRsp.u = uPtrFrame.pu16[0];
            uNewOuterSs   = uPtrFrame.pu16[1];
        }
        else if (enmEffOpSize == IEMMODE_32BIT)
        {
            NewOuterRsp.u = uPtrFrame.pu32[0];
            uNewOuterSs   = uPtrFrame.pu16[2];
        }
        else
        {
            NewOuterRsp.u = uPtrFrame.pu64[0];
            uNewOuterSs   = uPtrFrame.pu16[4];
        }
        rcStrict = iemMemStackPopDoneSpecial(pVCpu, bUnmapInfo);
        if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        { /* extremely likely */ }
        else
            return rcStrict;

        /* Check for NULL stack selector (invalid in ring-3 and non-long mode)
           and read the selector. */
        IEMSELDESC DescSs;
        if (!(uNewOuterSs & X86_SEL_MASK_OFF_RPL))
        {
            if (   !DescCs.Legacy.Gen.u1Long
                || (uNewOuterSs & X86_SEL_RPL) == 3)
            {
                Log(("retf %04x:%08RX64 %04x:%08RX64 -> invalid stack selector, #GP\n",
                     uNewCs, uNewRip, uNewOuterSs, NewOuterRsp.u));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }
            /** @todo Testcase: Return far to ring-1 or ring-2 with SS=0. */
            iemMemFakeStackSelDesc(&DescSs, (uNewOuterSs & X86_SEL_RPL));
        }
        else
        {
            /* Fetch the descriptor for the new stack segment. */
            rcStrict = iemMemFetchSelDesc(pVCpu, &DescSs, uNewOuterSs, X86_XCPT_GP);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
        }

        /* Check that RPL of stack and code selectors match. */
        if ((uNewCs & X86_SEL_RPL) != (uNewOuterSs & X86_SEL_RPL))
        {
            Log(("retf %04x:%08RX64 %04x:%08RX64 - SS.RPL != CS.RPL -> #GP(SS)\n", uNewCs, uNewRip, uNewOuterSs, NewOuterRsp.u));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewOuterSs);
        }

        /* Must be a writable data segment. */
        if (   !DescSs.Legacy.Gen.u1DescType
            || (DescSs.Legacy.Gen.u4Type & X86_SEL_TYPE_CODE)
            || !(DescSs.Legacy.Gen.u4Type & X86_SEL_TYPE_WRITE) )
        {
            Log(("retf %04x:%08RX64 %04x:%08RX64 - SS not a writable data segment (u1DescType=%u u4Type=%#x) -> #GP(SS).\n",
                 uNewCs, uNewRip, uNewOuterSs, NewOuterRsp.u, DescSs.Legacy.Gen.u1DescType, DescSs.Legacy.Gen.u4Type));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewOuterSs);
        }

        /* L vs D. (Not mentioned by intel.) */
        if (   DescSs.Legacy.Gen.u1Long /** @todo Testcase: far return to a stack selector with both L and D set. */
            && DescSs.Legacy.Gen.u1DefBig
            && IEM_IS_LONG_MODE(pVCpu))
        {
            Log(("retf %04x:%08RX64 %04x:%08RX64 - SS has both L & D set -> #GP(SS).\n",
                 uNewCs, uNewRip, uNewOuterSs, NewOuterRsp.u));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewOuterSs);
        }

        /* DPL/RPL/CPL checks. */
        if (DescSs.Legacy.Gen.u2Dpl != (uNewCs & X86_SEL_RPL))
        {
            Log(("retf %04x:%08RX64 %04x:%08RX64 - SS.DPL(%u) != CS.RPL (%u) -> #GP(SS).\n",
                 uNewCs, uNewRip, uNewOuterSs, NewOuterRsp.u, DescSs.Legacy.Gen.u2Dpl, uNewCs & X86_SEL_RPL));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewOuterSs);
        }

        /* Is it there? */
        if (!DescSs.Legacy.Gen.u1Present)
        {
            Log(("retf %04x:%08RX64 %04x:%08RX64 - SS not present -> #NP(SS).\n", uNewCs, uNewRip, uNewOuterSs, NewOuterRsp.u));
            return iemRaiseSelectorNotPresentBySelector(pVCpu, uNewCs);
        }

        /* Calc SS limit.*/
        uint64_t u64BaseSs;
        uint32_t cbLimitSs = X86DESC_LIMIT_G(&DescSs.Legacy);

        /* Is RIP canonical or within CS.limit? */
        uint64_t u64BaseCs;
        uint32_t cbLimitCs = X86DESC_LIMIT_G(&DescCs.Legacy);

        /** @todo Testcase: Is this correct? */
        if (   DescCs.Legacy.Gen.u1Long
            && IEM_IS_LONG_MODE(pVCpu) )
        {
            if (!IEM_IS_CANONICAL(uNewRip))
            {
                Log(("retf %04x:%08RX64 %04x:%08RX64 - not canonical -> #GP.\n", uNewCs, uNewRip, uNewOuterSs, NewOuterRsp.u));
                return iemRaiseNotCanonical(pVCpu);
            }
            u64BaseCs = 0;
            u64BaseSs = 0;
        }
        else
        {
            if (uNewRip > cbLimitCs)
            {
                Log(("retf %04x:%08RX64 %04x:%08RX64 - out of bounds (%#x)-> #GP(CS).\n",
                     uNewCs, uNewRip, uNewOuterSs, NewOuterRsp.u, cbLimitCs));
                /** @todo Intel says this is \#GP(0)! */
                return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
            }
            u64BaseCs = X86DESC_BASE(&DescCs.Legacy);
            u64BaseSs = X86DESC_BASE(&DescSs.Legacy);
        }

        /*
         * Now set the accessed bit before
         * writing the return address to the stack and committing the result into
         * CS, CSHID and RIP.
         */
        /** @todo Testcase: Need to check WHEN exactly the CS accessed bit is set. */
        if (!(DescCs.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
        {
            rcStrict = iemMemMarkSelDescAccessed(pVCpu, uNewCs);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            /** @todo check what VT-x and AMD-V does. */
            DescCs.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
        }
        /** @todo Testcase: Need to check WHEN exactly the SS accessed bit is set. */
        if (!(DescSs.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
        {
            rcStrict = iemMemMarkSelDescAccessed(pVCpu, uNewOuterSs);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            /** @todo check what VT-x and AMD-V does. */
            DescSs.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
        }

        /* commit */
        if (enmEffOpSize == IEMMODE_16BIT)
            pVCpu->cpum.GstCtx.rip           = uNewRip & UINT16_MAX; /** @todo Testcase: When exactly does this occur? With call it happens prior to the limit check according to Intel... */
        else
            pVCpu->cpum.GstCtx.rip           = uNewRip;
        pVCpu->cpum.GstCtx.cs.Sel            = uNewCs;
        pVCpu->cpum.GstCtx.cs.ValidSel       = uNewCs;
        pVCpu->cpum.GstCtx.cs.fFlags         = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.cs.Attr.u         = X86DESC_GET_HID_ATTR(&DescCs.Legacy);
        pVCpu->cpum.GstCtx.cs.u32Limit       = cbLimitCs;
        pVCpu->cpum.GstCtx.cs.u64Base        = u64BaseCs;
        pVCpu->cpum.GstCtx.ss.Sel            = uNewOuterSs;
        pVCpu->cpum.GstCtx.ss.ValidSel       = uNewOuterSs;
        pVCpu->cpum.GstCtx.ss.fFlags         = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.ss.Attr.u         = X86DESC_GET_HID_ATTR(&DescSs.Legacy);
        pVCpu->cpum.GstCtx.ss.u32Limit       = cbLimitSs;
        pVCpu->cpum.GstCtx.ss.u64Base        = u64BaseSs;

        iemHlpAdjustSelectorForNewCpl(pVCpu, uNewCs & X86_SEL_RPL, &pVCpu->cpum.GstCtx.ds);
        iemHlpAdjustSelectorForNewCpl(pVCpu, uNewCs & X86_SEL_RPL, &pVCpu->cpum.GstCtx.es);
        iemHlpAdjustSelectorForNewCpl(pVCpu, uNewCs & X86_SEL_RPL, &pVCpu->cpum.GstCtx.fs);
        iemHlpAdjustSelectorForNewCpl(pVCpu, uNewCs & X86_SEL_RPL, &pVCpu->cpum.GstCtx.gs);

        iemRecalcExecModeAndCplAndAcFlags(pVCpu); /* Affects iemRegAddToRspEx and the setting of RSP/SP below.  */

        if (cbPop)
            iemRegAddToRspEx(pVCpu, &NewOuterRsp, cbPop);
        if (IEM_IS_64BIT_CODE(pVCpu))
            pVCpu->cpum.GstCtx.rsp           = NewOuterRsp.u;
        else if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
            pVCpu->cpum.GstCtx.rsp           = (uint32_t)NewOuterRsp.u;
        else
            pVCpu->cpum.GstCtx.sp            = (uint16_t)NewOuterRsp.u;

        iemRecalcExecModeAndCplAndAcFlags(pVCpu); /* Affects iemRegAddToRspEx and the setting of RSP/SP below.  */

        /** @todo check if the hidden bits are loaded correctly for 64-bit
         *        mode. */
    }
    /*
     * Return to the same privilege level
     */
    else
    {
        /* Limit / canonical check. */
        uint64_t u64Base;
        uint32_t cbLimitCs = X86DESC_LIMIT_G(&DescCs.Legacy);

        /** @todo Testcase: Is this correct? */
        bool f64BitCs = false;
        if (   DescCs.Legacy.Gen.u1Long
            && IEM_IS_LONG_MODE(pVCpu) )
        {
            if (!IEM_IS_CANONICAL(uNewRip))
            {
                Log(("retf %04x:%08RX64 - not canonical -> #GP\n", uNewCs, uNewRip));
                return iemRaiseNotCanonical(pVCpu);
            }
            u64Base  = 0;
            f64BitCs = true;
            f64BitCs = true;
        }
        else
        {
            if (uNewRip > cbLimitCs)
            {
                Log(("retf %04x:%08RX64 -> out of bounds (%#x)\n", uNewCs, uNewRip, cbLimitCs));
                /** @todo Intel says this is \#GP(0)! */
                return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
            }
            u64Base = X86DESC_BASE(&DescCs.Legacy);
        }

        /*
         * Now set the accessed bit before
         * writing the return address to the stack and committing the result into
         * CS, CSHID and RIP.
         */
        /** @todo Testcase: Need to check WHEN exactly the accessed bit is set. */
        if (!(DescCs.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
        {
            rcStrict = iemMemMarkSelDescAccessed(pVCpu, uNewCs);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            /** @todo check what VT-x and AMD-V does. */
            DescCs.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
        }

        /* commit */
        if (cbPop)
/** @todo This cannot be right. We're using the old CS mode here, and iemRegAddToRspEx checks fExec. */
            iemRegAddToRspEx(pVCpu, &NewRsp, cbPop);
        if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig || f64BitCs)
            pVCpu->cpum.GstCtx.rsp       = NewRsp.u;
        else
            pVCpu->cpum.GstCtx.sp        = (uint16_t)NewRsp.u;
        if (enmEffOpSize == IEMMODE_16BIT)
            pVCpu->cpum.GstCtx.rip       = uNewRip & UINT16_MAX; /** @todo Testcase: When exactly does this occur? With call it happens prior to the limit check according to Intel... */
        else
            pVCpu->cpum.GstCtx.rip       = uNewRip;
        pVCpu->cpum.GstCtx.cs.Sel        = uNewCs;
        pVCpu->cpum.GstCtx.cs.ValidSel   = uNewCs;
        pVCpu->cpum.GstCtx.cs.fFlags     = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.cs.Attr.u     = X86DESC_GET_HID_ATTR(&DescCs.Legacy);
        pVCpu->cpum.GstCtx.cs.u32Limit   = cbLimitCs;
        pVCpu->cpum.GstCtx.cs.u64Base    = u64Base;
        /** @todo check if the hidden bits are loaded correctly for 64-bit
         *        mode.  */

        iemRecalcExecModeAndCplAndAcFlags(pVCpu);
    }

    /* Flush the prefetch buffer. */
    IEM_FLUSH_PREFETCH_HEAVY(pVCpu, cbInstr); /** @todo use light flush for same privilege? */

    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Implements enter.
 *
 * We're doing this in C because the instruction is insane, even for the
 * u8NestingLevel=0 case dealing with the stack is tedious.
 *
 * @param   enmEffOpSize    The effective operand size.
 * @param   cbFrame         Frame size.
 * @param   cParameters     Frame parameter count.
 */
IEM_CIMPL_DEF_3(iemCImpl_enter, IEMMODE, enmEffOpSize, uint16_t, cbFrame, uint8_t, cParameters)
{
    /* Push RBP, saving the old value in TmpRbp. */
    RTUINT64U       NewRsp; NewRsp.u = pVCpu->cpum.GstCtx.rsp;
    RTUINT64U       TmpRbp; TmpRbp.u = pVCpu->cpum.GstCtx.rbp;
    RTUINT64U       NewRbp;
    VBOXSTRICTRC    rcStrict;
    if (enmEffOpSize == IEMMODE_64BIT)
    {
        rcStrict = iemMemStackPushU64Ex(pVCpu, TmpRbp.u, &NewRsp);
        NewRbp = NewRsp;
    }
    else if (enmEffOpSize == IEMMODE_32BIT)
    {
        rcStrict = iemMemStackPushU32Ex(pVCpu, TmpRbp.DWords.dw0, &NewRsp);
        NewRbp = NewRsp;
    }
    else
    {
        rcStrict = iemMemStackPushU16Ex(pVCpu, TmpRbp.Words.w0, &NewRsp);
        NewRbp = TmpRbp;
        NewRbp.Words.w0 = NewRsp.Words.w0;
    }
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /* Copy the parameters (aka nesting levels by Intel). */
    cParameters &= 0x1f;
    if (cParameters > 0)
    {
        switch (enmEffOpSize)
        {
            case IEMMODE_16BIT:
                if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
                    TmpRbp.DWords.dw0 -= 2;
                else
                    TmpRbp.Words.w0   -= 2;
                do
                {
                    uint16_t u16Tmp;
                    rcStrict = iemMemStackPopU16Ex(pVCpu, &u16Tmp, &TmpRbp);
                    if (rcStrict != VINF_SUCCESS)
                        break;
                    rcStrict = iemMemStackPushU16Ex(pVCpu, u16Tmp, &NewRsp);
                } while (--cParameters > 0 && rcStrict == VINF_SUCCESS);
                break;

            case IEMMODE_32BIT:
                if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
                    TmpRbp.DWords.dw0 -= 4;
                else
                    TmpRbp.Words.w0   -= 4;
                do
                {
                    uint32_t u32Tmp;
                    rcStrict = iemMemStackPopU32Ex(pVCpu, &u32Tmp, &TmpRbp);
                    if (rcStrict != VINF_SUCCESS)
                        break;
                    rcStrict = iemMemStackPushU32Ex(pVCpu, u32Tmp, &NewRsp);
                } while (--cParameters > 0 && rcStrict == VINF_SUCCESS);
                break;

            case IEMMODE_64BIT:
                TmpRbp.u -= 8;
                do
                {
                    uint64_t u64Tmp;
                    rcStrict = iemMemStackPopU64Ex(pVCpu, &u64Tmp, &TmpRbp);
                    if (rcStrict != VINF_SUCCESS)
                        break;
                    rcStrict = iemMemStackPushU64Ex(pVCpu, u64Tmp, &NewRsp);
                } while (--cParameters > 0 && rcStrict == VINF_SUCCESS);
                break;

            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
        if (rcStrict != VINF_SUCCESS)
            return VINF_SUCCESS;

        /* Push the new RBP */
        if (enmEffOpSize == IEMMODE_64BIT)
            rcStrict = iemMemStackPushU64Ex(pVCpu, NewRbp.u, &NewRsp);
        else if (enmEffOpSize == IEMMODE_32BIT)
            rcStrict = iemMemStackPushU32Ex(pVCpu, NewRbp.DWords.dw0, &NewRsp);
        else
            rcStrict = iemMemStackPushU16Ex(pVCpu, NewRbp.Words.w0, &NewRsp);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

    }

    /* Recalc RSP. */
    iemRegSubFromRspEx(pVCpu, &NewRsp, cbFrame);

    /** @todo Should probe write access at the new RSP according to AMD. */
    /** @todo Should handle accesses to the VMX APIC-access page. */

    /* Commit it. */
    pVCpu->cpum.GstCtx.rbp = NewRbp.u;
    pVCpu->cpum.GstCtx.rsp = NewRsp.u;
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}



/**
 * Implements leave.
 *
 * We're doing this in C because messing with the stack registers is annoying
 * since they depends on SS attributes.
 *
 * @param   enmEffOpSize    The effective operand size.
 */
IEM_CIMPL_DEF_1(iemCImpl_leave, IEMMODE, enmEffOpSize)
{
    /* Calculate the intermediate RSP from RBP and the stack attributes. */
    RTUINT64U       NewRsp;
    if (IEM_IS_64BIT_CODE(pVCpu))
        NewRsp.u = pVCpu->cpum.GstCtx.rbp;
    else if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
        NewRsp.u = pVCpu->cpum.GstCtx.ebp;
    else
    {
        /** @todo Check that LEAVE actually preserve the high EBP bits. */
        NewRsp.u = pVCpu->cpum.GstCtx.rsp;
        NewRsp.Words.w0 = pVCpu->cpum.GstCtx.bp;
    }

    /* Pop RBP according to the operand size. */
    VBOXSTRICTRC    rcStrict;
    RTUINT64U       NewRbp;
    switch (enmEffOpSize)
    {
        case IEMMODE_16BIT:
            NewRbp.u = pVCpu->cpum.GstCtx.rbp;
            rcStrict = iemMemStackPopU16Ex(pVCpu, &NewRbp.Words.w0, &NewRsp);
            break;
        case IEMMODE_32BIT:
            NewRbp.u = 0;
            rcStrict = iemMemStackPopU32Ex(pVCpu, &NewRbp.DWords.dw0, &NewRsp);
            break;
        case IEMMODE_64BIT:
            rcStrict = iemMemStackPopU64Ex(pVCpu, &NewRbp.u, &NewRsp);
            break;
        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;


    /* Commit it. */
    pVCpu->cpum.GstCtx.rbp = NewRbp.u;
    pVCpu->cpum.GstCtx.rsp = NewRsp.u;
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements int3 and int XX.
 *
 * @param   u8Int       The interrupt vector number.
 * @param   enmInt      The int instruction type.
 */
IEM_CIMPL_DEF_2(iemCImpl_int, uint8_t, u8Int, IEMINT, enmInt)
{
    Assert(pVCpu->iem.s.cXcptRecursions == 0);

    /*
     * We must check if this INT3 might belong to DBGF before raising a #BP.
     */
    if (u8Int == 3)
    {
        PVMCC pVM = pVCpu->CTX_SUFF(pVM);
        if (pVM->dbgf.ro.cEnabledInt3Breakpoints == 0)
        { /* likely: No vbox debugger breakpoints */ }
        else
        {
            VBOXSTRICTRC rcStrict = DBGFTrap03Handler(pVM, pVCpu, &pVCpu->cpum.GstCtx);
            Log(("iemCImpl_int: DBGFTrap03Handler -> %Rrc\n", VBOXSTRICTRC_VAL(rcStrict) ));
            if (rcStrict != VINF_EM_RAW_GUEST_TRAP)
                return iemSetPassUpStatus(pVCpu, rcStrict);
        }
    }
/** @todo single stepping   */
    return iemRaiseXcptOrInt(pVCpu,
                             cbInstr,
                             u8Int,
                             IEM_XCPT_FLAGS_T_SOFT_INT | enmInt,
                             0,
                             0);
}


/**
 * Implements iret for real mode and V8086 mode.
 *
 * @param   enmEffOpSize    The effective operand size.
 */
IEM_CIMPL_DEF_1(iemCImpl_iret_real_v8086, IEMMODE, enmEffOpSize)
{
    X86EFLAGS Efl;
    Efl.u = IEMMISC_GET_EFL(pVCpu);
    NOREF(cbInstr);

    /*
     * iret throws an exception if VME isn't enabled.
     */
    if (   Efl.Bits.u1VM
        && Efl.Bits.u2IOPL != 3
        && !(pVCpu->cpum.GstCtx.cr4 & X86_CR4_VME))
        return iemRaiseGeneralProtectionFault0(pVCpu);

    /*
     * Do the stack bits, but don't commit RSP before everything checks
     * out right.
     */
    Assert(enmEffOpSize == IEMMODE_32BIT || enmEffOpSize == IEMMODE_16BIT);
    VBOXSTRICTRC    rcStrict;
    uint8_t         bUnmapInfo;
    RTCPTRUNION     uFrame;
    uint16_t        uNewCs;
    uint32_t        uNewEip;
    uint32_t        uNewFlags;
    uint64_t        uNewRsp;
    if (enmEffOpSize == IEMMODE_32BIT)
    {
        rcStrict = iemMemStackPopBeginSpecial(pVCpu, 12, 1, &uFrame.pv, &bUnmapInfo, &uNewRsp);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        uNewEip    = uFrame.pu32[0];
        if (uNewEip > UINT16_MAX)
            return iemRaiseGeneralProtectionFault0(pVCpu);

        uNewCs     = (uint16_t)uFrame.pu32[1];
        uNewFlags  = uFrame.pu32[2];
        uNewFlags &= X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF
                   | X86_EFL_TF | X86_EFL_IF | X86_EFL_DF | X86_EFL_OF | X86_EFL_IOPL | X86_EFL_NT
                   | X86_EFL_RF /*| X86_EFL_VM*/ | X86_EFL_AC /*|X86_EFL_VIF*/ /*|X86_EFL_VIP*/
                   | X86_EFL_ID;
        if (IEM_GET_TARGET_CPU(pVCpu) <= IEMTARGETCPU_386)
            uNewFlags &= ~(X86_EFL_AC | X86_EFL_ID | X86_EFL_VIF | X86_EFL_VIP);
        uNewFlags |= Efl.u & (X86_EFL_VM | X86_EFL_VIF | X86_EFL_VIP | X86_EFL_1);
    }
    else
    {
        rcStrict = iemMemStackPopBeginSpecial(pVCpu, 6, 1, &uFrame.pv, &bUnmapInfo, &uNewRsp);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        uNewEip    = uFrame.pu16[0];
        uNewCs     = uFrame.pu16[1];
        uNewFlags  = uFrame.pu16[2];
        uNewFlags &= X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF
                   | X86_EFL_TF | X86_EFL_IF | X86_EFL_DF | X86_EFL_OF | X86_EFL_IOPL | X86_EFL_NT;
        uNewFlags |= Efl.u & ((UINT32_C(0xffff0000) | X86_EFL_1) & ~X86_EFL_RF);
        /** @todo The intel pseudo code does not indicate what happens to
         *        reserved flags. We just ignore them. */
        /* Ancient CPU adjustments: See iemCImpl_popf. */
        if (IEM_GET_TARGET_CPU(pVCpu) == IEMTARGETCPU_286)
            uNewFlags &= ~(X86_EFL_NT | X86_EFL_IOPL);
    }
    rcStrict = iemMemStackPopDoneSpecial(pVCpu, bUnmapInfo);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    { /* extremely likely */ }
    else
        return rcStrict;

    /** @todo Check how this is supposed to work if sp=0xfffe. */
    Log7(("iemCImpl_iret_real_v8086: uNewCs=%#06x uNewRip=%#010x uNewFlags=%#x uNewRsp=%#18llx\n",
          uNewCs, uNewEip, uNewFlags, uNewRsp));

    /*
     * Check the limit of the new EIP.
     */
    /** @todo Only the AMD pseudo code check the limit here, what's
     *        right? */
    if (uNewEip > pVCpu->cpum.GstCtx.cs.u32Limit)
        return iemRaiseSelectorBounds(pVCpu, X86_SREG_CS, IEM_ACCESS_INSTRUCTION);

    /*
     * V8086 checks and flag adjustments
     */
    if (Efl.Bits.u1VM)
    {
        if (Efl.Bits.u2IOPL == 3)
        {
            /* Preserve IOPL and clear RF. */
            uNewFlags &=        ~(X86_EFL_IOPL | X86_EFL_RF);
            uNewFlags |= Efl.u & (X86_EFL_IOPL);
        }
        else if (   enmEffOpSize == IEMMODE_16BIT
                 && (   !(uNewFlags & X86_EFL_IF)
                     || !Efl.Bits.u1VIP )
                 && !(uNewFlags & X86_EFL_TF)   )
        {
            /* Move IF to VIF, clear RF and preserve IF and IOPL.*/
            uNewFlags &= ~X86_EFL_VIF;
            uNewFlags |= (uNewFlags & X86_EFL_IF) << (19 - 9);
            uNewFlags &=        ~(X86_EFL_IF | X86_EFL_IOPL | X86_EFL_RF);
            uNewFlags |= Efl.u & (X86_EFL_IF | X86_EFL_IOPL);
        }
        else
            return iemRaiseGeneralProtectionFault0(pVCpu);
        Log7(("iemCImpl_iret_real_v8086: u1VM=1: adjusted uNewFlags=%#x\n", uNewFlags));
    }

    /*
     * Commit the operation.
     */
    IEMTLBTRACE_IRET(pVCpu, uNewCs, uNewEip, uNewFlags);
#ifdef DBGFTRACE_ENABLED
    RTTraceBufAddMsgF(pVCpu->CTX_SUFF(pVM)->CTX_SUFF(hTraceBuf), "iret/rm %04x:%04x -> %04x:%04x %x %04llx",
                      pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.eip, uNewCs, uNewEip, uNewFlags, uNewRsp);
#endif
    pVCpu->cpum.GstCtx.rsp           = uNewRsp;
    pVCpu->cpum.GstCtx.rip           = uNewEip;
    pVCpu->cpum.GstCtx.cs.Sel        = uNewCs;
    pVCpu->cpum.GstCtx.cs.ValidSel   = uNewCs;
    pVCpu->cpum.GstCtx.cs.fFlags     = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.cs.u64Base    = (uint32_t)uNewCs << 4;
    /** @todo do we load attribs and limit as well? */
    Assert(uNewFlags & X86_EFL_1);
    IEMMISC_SET_EFL(pVCpu, uNewFlags);
    pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~IEM_F_X86_AC) | iemCalcExecAcFlag(pVCpu);

    /* Flush the prefetch buffer. */
    IEM_FLUSH_PREFETCH_HEAVY(pVCpu, cbInstr); /** @todo can do light flush in real mode at least */

/** @todo single stepping   */
    return VINF_SUCCESS;
}


/**
 * Loads a segment register when entering V8086 mode.
 *
 * @param   pSReg           The segment register.
 * @param   uSeg            The segment to load.
 */
static void iemCImplCommonV8086LoadSeg(PCPUMSELREG pSReg, uint16_t uSeg)
{
    pSReg->Sel        = uSeg;
    pSReg->ValidSel   = uSeg;
    pSReg->fFlags     = CPUMSELREG_FLAGS_VALID;
    pSReg->u64Base    = (uint32_t)uSeg << 4;
    pSReg->u32Limit   = 0xffff;
    pSReg->Attr.u     = X86_SEL_TYPE_RW_ACC | RT_BIT(4) /*!sys*/ | RT_BIT(7) /*P*/ | (3 /*DPL*/ << 5); /* VT-x wants 0xf3 */
    /** @todo Testcase: Check if VT-x really needs this and what it does itself when
     *        IRET'ing to V8086. */
}


/**
 * Implements iret for protected mode returning to V8086 mode.
 *
 * @param   uNewEip         The new EIP.
 * @param   uNewCs          The new CS.
 * @param   uNewFlags       The new EFLAGS.
 * @param   uNewRsp         The RSP after the initial IRET frame.
 *
 * @note    This can only be a 32-bit iret du to the X86_EFL_VM position.
 */
IEM_CIMPL_DEF_4(iemCImpl_iret_prot_v8086, uint32_t, uNewEip, uint16_t, uNewCs, uint32_t, uNewFlags, uint64_t, uNewRsp)
{
    RT_NOREF_PV(cbInstr);
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_SREG_MASK);

    /*
     * Pop the V8086 specific frame bits off the stack.
     */
    uint8_t      bUnmapInfo;
    RTCPTRUNION  uFrame;
    VBOXSTRICTRC rcStrict = iemMemStackPopContinueSpecial(pVCpu, 0 /*off*/, 24 /*cbMem*/, &uFrame.pv, &bUnmapInfo, uNewRsp);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;
    uint32_t uNewEsp = uFrame.pu32[0];
    uint16_t uNewSs  = uFrame.pu32[1];
    uint16_t uNewEs  = uFrame.pu32[2];
    uint16_t uNewDs  = uFrame.pu32[3];
    uint16_t uNewFs  = uFrame.pu32[4];
    uint16_t uNewGs  = uFrame.pu32[5];
    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo); /* don't use iemMemStackPopCommitSpecial here. */
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /*
     * Commit the operation.
     */
    uNewFlags &= X86_EFL_LIVE_MASK;
    uNewFlags |= X86_EFL_RA1_MASK;
#ifdef DBGFTRACE_ENABLED
    RTTraceBufAddMsgF(pVCpu->CTX_SUFF(pVM)->CTX_SUFF(hTraceBuf), "iret/p/v %04x:%08x -> %04x:%04x %x %04x:%04x",
                      pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.eip, uNewCs, uNewEip, uNewFlags, uNewSs, uNewEsp);
#endif
    Log7(("iemCImpl_iret_prot_v8086: %04x:%08x -> %04x:%04x %x %04x:%04x\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.eip, uNewCs, uNewEip, uNewFlags, uNewSs, uNewEsp));

    IEMMISC_SET_EFL(pVCpu, uNewFlags);
    iemCImplCommonV8086LoadSeg(&pVCpu->cpum.GstCtx.cs, uNewCs);
    iemCImplCommonV8086LoadSeg(&pVCpu->cpum.GstCtx.ss, uNewSs);
    iemCImplCommonV8086LoadSeg(&pVCpu->cpum.GstCtx.es, uNewEs);
    iemCImplCommonV8086LoadSeg(&pVCpu->cpum.GstCtx.ds, uNewDs);
    iemCImplCommonV8086LoadSeg(&pVCpu->cpum.GstCtx.fs, uNewFs);
    iemCImplCommonV8086LoadSeg(&pVCpu->cpum.GstCtx.gs, uNewGs);
    pVCpu->cpum.GstCtx.rip      = (uint16_t)uNewEip;
    pVCpu->cpum.GstCtx.rsp      = uNewEsp; /** @todo check this out! */
    pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~(IEM_F_MODE_MASK | IEM_F_X86_CPL_MASK | IEM_F_X86_AC))
                       | (3 << IEM_F_X86_CPL_SHIFT)
                       | IEM_F_MODE_X86_16BIT_PROT_V86
                       | iemCalcExecAcFlag(pVCpu);

    /* Flush the prefetch buffer. */
    IEM_FLUSH_PREFETCH_HEAVY(pVCpu, cbInstr);

/** @todo single stepping   */
    return VINF_SUCCESS;
}


/**
 * Implements iret for protected mode returning via a nested task.
 *
 * @param   enmEffOpSize    The effective operand size.
 */
IEM_CIMPL_DEF_1(iemCImpl_iret_prot_NestedTask, IEMMODE, enmEffOpSize)
{
    Log7(("iemCImpl_iret_prot_NestedTask:\n"));
#ifndef IEM_IMPLEMENTS_TASKSWITCH
    IEM_RETURN_ASPECT_NOT_IMPLEMENTED();
#else
    RT_NOREF_PV(enmEffOpSize);

    /*
     * Read the segment selector in the link-field of the current TSS.
     */
    RTSEL        uSelRet;
    VBOXSTRICTRC rcStrict = iemMemFetchSysU16(pVCpu, &uSelRet, UINT8_MAX, pVCpu->cpum.GstCtx.tr.u64Base);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /*
     * Fetch the returning task's TSS descriptor from the GDT.
     */
    if (uSelRet & X86_SEL_LDT)
    {
        Log(("iret_prot_NestedTask TSS not in LDT. uSelRet=%04x -> #TS\n", uSelRet));
        return iemRaiseTaskSwitchFaultBySelector(pVCpu, uSelRet);
    }

    IEMSELDESC TssDesc;
    rcStrict = iemMemFetchSelDesc(pVCpu, &TssDesc, uSelRet, X86_XCPT_GP);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    if (TssDesc.Legacy.Gate.u1DescType)
    {
        Log(("iret_prot_NestedTask Invalid TSS type. uSelRet=%04x -> #TS\n", uSelRet));
        return iemRaiseTaskSwitchFaultBySelector(pVCpu, uSelRet & X86_SEL_MASK_OFF_RPL);
    }

    if (   TssDesc.Legacy.Gate.u4Type != X86_SEL_TYPE_SYS_286_TSS_BUSY
        && TssDesc.Legacy.Gate.u4Type != X86_SEL_TYPE_SYS_386_TSS_BUSY)
    {
        Log(("iret_prot_NestedTask TSS is not busy. uSelRet=%04x DescType=%#x -> #TS\n", uSelRet, TssDesc.Legacy.Gate.u4Type));
        return iemRaiseTaskSwitchFaultBySelector(pVCpu, uSelRet & X86_SEL_MASK_OFF_RPL);
    }

    if (!TssDesc.Legacy.Gate.u1Present)
    {
        Log(("iret_prot_NestedTask TSS is not present. uSelRet=%04x -> #NP\n", uSelRet));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, uSelRet & X86_SEL_MASK_OFF_RPL);
    }

    uint32_t uNextEip = pVCpu->cpum.GstCtx.eip + cbInstr;
    return iemTaskSwitch(pVCpu, IEMTASKSWITCH_IRET, uNextEip, 0 /* fFlags */, 0 /* uErr */,
                         0 /* uCr2 */, uSelRet, &TssDesc);
#endif
}


/**
 * Implements iret for protected mode
 *
 * @param   enmEffOpSize    The effective operand size.
 */
IEM_CIMPL_DEF_1(iemCImpl_iret_prot, IEMMODE, enmEffOpSize)
{
    NOREF(cbInstr);
    Assert(enmEffOpSize == IEMMODE_32BIT || enmEffOpSize == IEMMODE_16BIT);

    /*
     * Nested task return.
     */
    if (pVCpu->cpum.GstCtx.eflags.Bits.u1NT)
        return IEM_CIMPL_CALL_1(iemCImpl_iret_prot_NestedTask, enmEffOpSize);

    /*
     * Normal return.
     *
     * Do the stack bits, but don't commit RSP before everything checks
     * out right.
     */
    Assert(enmEffOpSize == IEMMODE_32BIT || enmEffOpSize == IEMMODE_16BIT);
    uint8_t         bUnmapInfo;
    VBOXSTRICTRC    rcStrict;
    RTCPTRUNION     uFrame;
    uint16_t        uNewCs;
    uint32_t        uNewEip;
    uint32_t        uNewFlags;
    uint64_t        uNewRsp;
    if (enmEffOpSize == IEMMODE_32BIT)
    {
        rcStrict = iemMemStackPopBeginSpecial(pVCpu, 12, 3, &uFrame.pv, &bUnmapInfo, &uNewRsp);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        uNewEip    = uFrame.pu32[0];
        uNewCs     = (uint16_t)uFrame.pu32[1];
        uNewFlags  = uFrame.pu32[2];
    }
    else
    {
        rcStrict = iemMemStackPopBeginSpecial(pVCpu, 6, 1, &uFrame.pv, &bUnmapInfo, &uNewRsp);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        uNewEip    = uFrame.pu16[0];
        uNewCs     = uFrame.pu16[1];
        uNewFlags  = uFrame.pu16[2];
    }
    rcStrict = iemMemStackPopDoneSpecial(pVCpu, bUnmapInfo); /* don't use iemMemStackPopCommitSpecial here. */
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    { /* extremely likely */ }
    else
        return rcStrict;
    Log7(("iemCImpl_iret_prot: uNewCs=%#06x uNewEip=%#010x uNewFlags=%#x uNewRsp=%#18llx uCpl=%u\n", uNewCs, uNewEip, uNewFlags, uNewRsp, IEM_GET_CPL(pVCpu)));

    /*
     * We're hopefully not returning to V8086 mode...
     */
    if (   (uNewFlags & X86_EFL_VM)
        && IEM_GET_CPL(pVCpu) == 0)
    {
        Assert(enmEffOpSize == IEMMODE_32BIT);
        return IEM_CIMPL_CALL_4(iemCImpl_iret_prot_v8086, uNewEip, uNewCs, uNewFlags, uNewRsp);
    }

    /*
     * Protected mode.
     */
    /* Read the CS descriptor. */
    if (!(uNewCs & X86_SEL_MASK_OFF_RPL))
    {
        Log(("iret %04x:%08x -> invalid CS selector, #GP(0)\n", uNewCs, uNewEip));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    IEMSELDESC DescCS;
    rcStrict = iemMemFetchSelDesc(pVCpu, &DescCS, uNewCs, X86_XCPT_GP);
    if (rcStrict != VINF_SUCCESS)
    {
        Log(("iret %04x:%08x - rcStrict=%Rrc when fetching CS\n", uNewCs, uNewEip, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }

    /* Must be a code descriptor. */
    if (!DescCS.Legacy.Gen.u1DescType)
    {
        Log(("iret %04x:%08x - CS is system segment (%#x) -> #GP\n", uNewCs, uNewEip, DescCS.Legacy.Gen.u4Type));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
    }
    if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_CODE))
    {
        Log(("iret %04x:%08x - not code segment (%#x) -> #GP\n", uNewCs, uNewEip, DescCS.Legacy.Gen.u4Type));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
    }

    /* Privilege checks. */
    if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_CONF))
    {
        if ((uNewCs & X86_SEL_RPL) != DescCS.Legacy.Gen.u2Dpl)
        {
            Log(("iret %04x:%08x - RPL != DPL (%d) -> #GP\n", uNewCs, uNewEip, DescCS.Legacy.Gen.u2Dpl));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
        }
    }
    else if ((uNewCs & X86_SEL_RPL) < DescCS.Legacy.Gen.u2Dpl)
    {
        Log(("iret %04x:%08x - RPL < DPL (%d) -> #GP\n", uNewCs, uNewEip, DescCS.Legacy.Gen.u2Dpl));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
    }
    if ((uNewCs & X86_SEL_RPL) < IEM_GET_CPL(pVCpu))
    {
        Log(("iret %04x:%08x - RPL < CPL (%d) -> #GP\n", uNewCs, uNewEip, IEM_GET_CPL(pVCpu)));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
    }

    /* Present? */
    if (!DescCS.Legacy.Gen.u1Present)
    {
        Log(("iret %04x:%08x - CS not present -> #NP\n", uNewCs, uNewEip));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, uNewCs);
    }

    uint32_t cbLimitCS = X86DESC_LIMIT_G(&DescCS.Legacy);

    /*
     * Return to outer level?
     */
    if ((uNewCs & X86_SEL_RPL) != IEM_GET_CPL(pVCpu))
    {
        uint16_t    uNewSS;
        uint32_t    uNewESP;
        if (enmEffOpSize == IEMMODE_32BIT)
        {
            rcStrict = iemMemStackPopContinueSpecial(pVCpu, 0/*off*/, 8 /*cbMem*/, &uFrame.pv, &bUnmapInfo, uNewRsp);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
/** @todo We might be popping a 32-bit ESP from the IRET frame, but whether
 *        16-bit or 32-bit are being loaded into SP depends on the D/B
 *        bit of the popped SS selector it turns out. */
            uNewESP = uFrame.pu32[0];
            uNewSS  = (uint16_t)uFrame.pu32[1];
        }
        else
        {
            rcStrict = iemMemStackPopContinueSpecial(pVCpu, 0 /*off*/, 4 /*cbMem*/, &uFrame.pv, &bUnmapInfo, uNewRsp);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            uNewESP = uFrame.pu16[0];
            uNewSS  = uFrame.pu16[1];
        }
        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        Log7(("iemCImpl_iret_prot: uNewSS=%#06x uNewESP=%#010x\n", uNewSS, uNewESP));

        /* Read the SS descriptor. */
        if (!(uNewSS & X86_SEL_MASK_OFF_RPL))
        {
            Log(("iret %04x:%08x/%04x:%08x -> invalid SS selector, #GP(0)\n", uNewCs, uNewEip, uNewSS, uNewESP));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }

        IEMSELDESC DescSS;
        rcStrict = iemMemFetchSelDesc(pVCpu, &DescSS, uNewSS, X86_XCPT_GP); /** @todo Correct exception? */
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("iret %04x:%08x/%04x:%08x - %Rrc when fetching SS\n",
                 uNewCs, uNewEip, uNewSS, uNewESP, VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }

        /* Privilege checks. */
        if ((uNewSS & X86_SEL_RPL) != (uNewCs & X86_SEL_RPL))
        {
            Log(("iret %04x:%08x/%04x:%08x -> SS.RPL != CS.RPL -> #GP\n", uNewCs, uNewEip, uNewSS, uNewESP));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewSS);
        }
        if (DescSS.Legacy.Gen.u2Dpl != (uNewCs & X86_SEL_RPL))
        {
            Log(("iret %04x:%08x/%04x:%08x -> SS.DPL (%d) != CS.RPL -> #GP\n",
                 uNewCs, uNewEip, uNewSS, uNewESP, DescSS.Legacy.Gen.u2Dpl));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewSS);
        }

        /* Must be a writeable data segment descriptor. */
        if (!DescSS.Legacy.Gen.u1DescType)
        {
            Log(("iret %04x:%08x/%04x:%08x -> SS is system segment (%#x) -> #GP\n",
                 uNewCs, uNewEip, uNewSS, uNewESP, DescSS.Legacy.Gen.u4Type));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewSS);
        }
        if ((DescSS.Legacy.Gen.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_WRITE)) != X86_SEL_TYPE_WRITE)
        {
            Log(("iret %04x:%08x/%04x:%08x - not writable data segment (%#x) -> #GP\n",
                 uNewCs, uNewEip, uNewSS, uNewESP, DescSS.Legacy.Gen.u4Type));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewSS);
        }

        /* Present? */
        if (!DescSS.Legacy.Gen.u1Present)
        {
            Log(("iret %04x:%08x/%04x:%08x -> SS not present -> #SS\n", uNewCs, uNewEip, uNewSS, uNewESP));
            return iemRaiseStackSelectorNotPresentBySelector(pVCpu, uNewSS);
        }

        uint32_t cbLimitSs = X86DESC_LIMIT_G(&DescSS.Legacy);

        /* Check EIP. */
        if (uNewEip > cbLimitCS)
        {
            Log(("iret %04x:%08x/%04x:%08x -> EIP is out of bounds (%#x) -> #GP(0)\n",
                 uNewCs, uNewEip, uNewSS, uNewESP, cbLimitCS));
            /** @todo Which is it, \#GP(0) or \#GP(sel)? */
            return iemRaiseSelectorBoundsBySelector(pVCpu, uNewCs);
        }

        /*
         * Commit the changes, marking CS and SS accessed first since
         * that may fail.
         */
        if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
        {
            rcStrict = iemMemMarkSelDescAccessed(pVCpu, uNewCs);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            DescCS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
        }
        if (!(DescSS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
        {
            rcStrict = iemMemMarkSelDescAccessed(pVCpu, uNewSS);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            DescSS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
        }

        uint32_t fEFlagsMask = X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF
                             | X86_EFL_TF | X86_EFL_DF | X86_EFL_OF | X86_EFL_NT;
        if (enmEffOpSize != IEMMODE_16BIT)
            fEFlagsMask |= X86_EFL_RF | X86_EFL_AC | X86_EFL_ID;
        if (IEM_GET_CPL(pVCpu) == 0)
            fEFlagsMask |= X86_EFL_IF | X86_EFL_IOPL | X86_EFL_VIF | X86_EFL_VIP; /* VM is 0 */
        else if (IEM_GET_CPL(pVCpu) <= pVCpu->cpum.GstCtx.eflags.Bits.u2IOPL)
            fEFlagsMask |= X86_EFL_IF;
        if (IEM_GET_TARGET_CPU(pVCpu) <= IEMTARGETCPU_386)
            fEFlagsMask &= ~(X86_EFL_AC | X86_EFL_ID | X86_EFL_VIF | X86_EFL_VIP);
        uint32_t fEFlagsNew = IEMMISC_GET_EFL(pVCpu);
        fEFlagsNew         &= ~fEFlagsMask;
        fEFlagsNew         |= uNewFlags & fEFlagsMask;
        IEMTLBTRACE_IRET(pVCpu, uNewCs, uNewEip, fEFlagsNew);
#ifdef DBGFTRACE_ENABLED
        RTTraceBufAddMsgF(pVCpu->CTX_SUFF(pVM)->CTX_SUFF(hTraceBuf), "iret/%up%u %04x:%08x -> %04x:%04x %x %04x:%04x",
                          IEM_GET_CPL(pVCpu), uNewCs & X86_SEL_RPL, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.eip,
                          uNewCs, uNewEip, uNewFlags,  uNewSS, uNewESP);
#endif

        IEMMISC_SET_EFL(pVCpu, fEFlagsNew);
        pVCpu->cpum.GstCtx.rip           = uNewEip;
        pVCpu->cpum.GstCtx.cs.Sel        = uNewCs;
        pVCpu->cpum.GstCtx.cs.ValidSel   = uNewCs;
        pVCpu->cpum.GstCtx.cs.fFlags     = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.cs.Attr.u     = X86DESC_GET_HID_ATTR(&DescCS.Legacy);
        pVCpu->cpum.GstCtx.cs.u32Limit   = cbLimitCS;
        pVCpu->cpum.GstCtx.cs.u64Base    = X86DESC_BASE(&DescCS.Legacy);

        pVCpu->cpum.GstCtx.ss.Sel        = uNewSS;
        pVCpu->cpum.GstCtx.ss.ValidSel   = uNewSS;
        pVCpu->cpum.GstCtx.ss.fFlags     = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.ss.Attr.u     = X86DESC_GET_HID_ATTR(&DescSS.Legacy);
        pVCpu->cpum.GstCtx.ss.u32Limit   = cbLimitSs;
        pVCpu->cpum.GstCtx.ss.u64Base    = X86DESC_BASE(&DescSS.Legacy);
        if (!pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
            pVCpu->cpum.GstCtx.sp        = (uint16_t)uNewESP;
        else
            pVCpu->cpum.GstCtx.rsp       = uNewESP;

        iemHlpAdjustSelectorForNewCpl(pVCpu, uNewCs & X86_SEL_RPL, &pVCpu->cpum.GstCtx.ds);
        iemHlpAdjustSelectorForNewCpl(pVCpu, uNewCs & X86_SEL_RPL, &pVCpu->cpum.GstCtx.es);
        iemHlpAdjustSelectorForNewCpl(pVCpu, uNewCs & X86_SEL_RPL, &pVCpu->cpum.GstCtx.fs);
        iemHlpAdjustSelectorForNewCpl(pVCpu, uNewCs & X86_SEL_RPL, &pVCpu->cpum.GstCtx.gs);

        iemRecalcExecModeAndCplAndAcFlags(pVCpu);

        /* Done! */

    }
    /*
     * Return to the same level.
     */
    else
    {
        /* Check EIP. */
        if (uNewEip > cbLimitCS)
        {
            Log(("iret %04x:%08x - EIP is out of bounds (%#x) -> #GP(0)\n", uNewCs, uNewEip, cbLimitCS));
            /** @todo Which is it, \#GP(0) or \#GP(sel)? */
            return iemRaiseSelectorBoundsBySelector(pVCpu, uNewCs);
        }

        /*
         * Commit the changes, marking CS first since it may fail.
         */
        if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
        {
            rcStrict = iemMemMarkSelDescAccessed(pVCpu, uNewCs);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            DescCS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
        }

        X86EFLAGS NewEfl;
        NewEfl.u = IEMMISC_GET_EFL(pVCpu);
        uint32_t fEFlagsMask = X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF
                             | X86_EFL_TF | X86_EFL_DF | X86_EFL_OF | X86_EFL_NT;
        if (enmEffOpSize != IEMMODE_16BIT)
            fEFlagsMask |= X86_EFL_RF | X86_EFL_AC | X86_EFL_ID;
        if (IEM_GET_CPL(pVCpu) == 0)
            fEFlagsMask |= X86_EFL_IF | X86_EFL_IOPL | X86_EFL_VIF | X86_EFL_VIP; /* VM is 0 */
        else if (IEM_GET_CPL(pVCpu) <= NewEfl.Bits.u2IOPL)
            fEFlagsMask |= X86_EFL_IF;
        if (IEM_GET_TARGET_CPU(pVCpu) <= IEMTARGETCPU_386)
            fEFlagsMask &= ~(X86_EFL_AC | X86_EFL_ID | X86_EFL_VIF | X86_EFL_VIP);
        NewEfl.u           &= ~fEFlagsMask;
        NewEfl.u           |= fEFlagsMask & uNewFlags;
        IEMTLBTRACE_IRET(pVCpu, uNewCs, uNewEip, NewEfl.u);
#ifdef DBGFTRACE_ENABLED
        RTTraceBufAddMsgF(pVCpu->CTX_SUFF(pVM)->CTX_SUFF(hTraceBuf), "iret/%up %04x:%08x -> %04x:%04x %x %04x:%04llx",
                          IEM_GET_CPL(pVCpu), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.eip,
                          uNewCs, uNewEip, uNewFlags, pVCpu->cpum.GstCtx.ss.Sel, uNewRsp);
#endif

        IEMMISC_SET_EFL(pVCpu, NewEfl.u);
        pVCpu->cpum.GstCtx.rip           = uNewEip;
        pVCpu->cpum.GstCtx.cs.Sel        = uNewCs;
        pVCpu->cpum.GstCtx.cs.ValidSel   = uNewCs;
        pVCpu->cpum.GstCtx.cs.fFlags     = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.cs.Attr.u     = X86DESC_GET_HID_ATTR(&DescCS.Legacy);
        pVCpu->cpum.GstCtx.cs.u32Limit   = cbLimitCS;
        pVCpu->cpum.GstCtx.cs.u64Base    = X86DESC_BASE(&DescCS.Legacy);
        if (!pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
            pVCpu->cpum.GstCtx.sp        = (uint16_t)uNewRsp;
        else
            pVCpu->cpum.GstCtx.rsp       = uNewRsp;

        iemRecalcExecModeAndCplAndAcFlags(pVCpu);

        /* Done! */
    }

    /* Flush the prefetch buffer. */
    IEM_FLUSH_PREFETCH_HEAVY(pVCpu, cbInstr); /** @todo may light flush if same ring? */

/** @todo single stepping   */
    return VINF_SUCCESS;
}


/**
 * Implements iret for long mode
 *
 * @param   enmEffOpSize    The effective operand size.
 */
IEM_CIMPL_DEF_1(iemCImpl_iret_64bit, IEMMODE, enmEffOpSize)
{
    NOREF(cbInstr);

    /*
     * Nested task return is not supported in long mode.
     */
    if (pVCpu->cpum.GstCtx.eflags.Bits.u1NT)
    {
        Log(("iret/64 with NT=1 (eflags=%#x) -> #GP(0)\n", pVCpu->cpum.GstCtx.eflags.u));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /*
     * Normal return.
     *
     * Do the stack bits, but don't commit RSP before everything checks
     * out right.
     */
    VBOXSTRICTRC    rcStrict;
    uint8_t         bUnmapInfo;
    RTCPTRUNION     uFrame;
    uint64_t        uNewRip;
    uint16_t        uNewCs;
    uint16_t        uNewSs;
    uint32_t        uNewFlags;
    uint64_t        uNewRsp;
    if (enmEffOpSize == IEMMODE_64BIT)
    {
        rcStrict = iemMemStackPopBeginSpecial(pVCpu, 5*8, 7, &uFrame.pv, &bUnmapInfo, &uNewRsp);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        uNewRip    = uFrame.pu64[0];
        uNewCs     = (uint16_t)uFrame.pu64[1];
        uNewFlags  = (uint32_t)uFrame.pu64[2];
        uNewRsp    = uFrame.pu64[3];
        uNewSs     = (uint16_t)uFrame.pu64[4];
    }
    else if (enmEffOpSize == IEMMODE_32BIT)
    {
        rcStrict = iemMemStackPopBeginSpecial(pVCpu, 5*4, 3, &uFrame.pv, &bUnmapInfo, &uNewRsp);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        uNewRip    = uFrame.pu32[0];
        uNewCs     = (uint16_t)uFrame.pu32[1];
        uNewFlags  = uFrame.pu32[2];
        uNewRsp    = uFrame.pu32[3];
        uNewSs     = (uint16_t)uFrame.pu32[4];
    }
    else
    {
        Assert(enmEffOpSize == IEMMODE_16BIT);
        rcStrict = iemMemStackPopBeginSpecial(pVCpu, 5*2, 1, &uFrame.pv, &bUnmapInfo, &uNewRsp);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        uNewRip    = uFrame.pu16[0];
        uNewCs     = uFrame.pu16[1];
        uNewFlags  = uFrame.pu16[2];
        uNewRsp    = uFrame.pu16[3];
        uNewSs     = uFrame.pu16[4];
    }
    rcStrict = iemMemStackPopDoneSpecial(pVCpu, bUnmapInfo); /* don't use iemMemStackPopCommitSpecial here. */
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    { /* extremely like */ }
    else
        return rcStrict;
    Log7(("iret/64 stack: cs:rip=%04x:%016RX64 rflags=%016RX64 ss:rsp=%04x:%016RX64\n", uNewCs, uNewRip, uNewFlags, uNewSs, uNewRsp));

    /*
     * Check stuff.
     */
    /* Read the CS descriptor. */
    if (!(uNewCs & X86_SEL_MASK_OFF_RPL))
    {
        Log(("iret/64 %04x:%016RX64/%04x:%016RX64 -> invalid CS selector, #GP(0)\n", uNewCs, uNewRip, uNewSs, uNewRsp));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    IEMSELDESC DescCS;
    rcStrict = iemMemFetchSelDesc(pVCpu, &DescCS, uNewCs, X86_XCPT_GP);
    if (rcStrict != VINF_SUCCESS)
    {
        Log(("iret/64 %04x:%016RX64/%04x:%016RX64 - rcStrict=%Rrc when fetching CS\n",
             uNewCs, uNewRip, uNewSs, uNewRsp, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }

    /* Must be a code descriptor. */
    if (   !DescCS.Legacy.Gen.u1DescType
        || !(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_CODE))
    {
        Log(("iret/64 %04x:%016RX64/%04x:%016RX64 - CS is not a code segment T=%u T=%#xu -> #GP\n",
             uNewCs, uNewRip, uNewSs, uNewRsp, DescCS.Legacy.Gen.u1DescType, DescCS.Legacy.Gen.u4Type));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
    }

    /* Privilege checks. */
    uint8_t const uNewCpl = uNewCs & X86_SEL_RPL;
    if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_CONF))
    {
        if ((uNewCs & X86_SEL_RPL) != DescCS.Legacy.Gen.u2Dpl)
        {
            Log(("iret/64 %04x:%016RX64 - RPL != DPL (%d) -> #GP\n", uNewCs, uNewRip, DescCS.Legacy.Gen.u2Dpl));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
        }
    }
    else if ((uNewCs & X86_SEL_RPL) < DescCS.Legacy.Gen.u2Dpl)
    {
        Log(("iret/64 %04x:%016RX64 - RPL < DPL (%d) -> #GP\n", uNewCs, uNewRip, DescCS.Legacy.Gen.u2Dpl));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
    }
    if ((uNewCs & X86_SEL_RPL) < IEM_GET_CPL(pVCpu))
    {
        Log(("iret/64 %04x:%016RX64 - RPL < CPL (%d) -> #GP\n", uNewCs, uNewRip, IEM_GET_CPL(pVCpu)));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewCs);
    }

    /* Present? */
    if (!DescCS.Legacy.Gen.u1Present)
    {
        Log(("iret/64 %04x:%016RX64/%04x:%016RX64 - CS not present -> #NP\n", uNewCs, uNewRip, uNewSs, uNewRsp));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, uNewCs);
    }

    uint32_t cbLimitCS = X86DESC_LIMIT_G(&DescCS.Legacy);

    /* Read the SS descriptor. */
    IEMSELDESC DescSS;
    if (!(uNewSs & X86_SEL_MASK_OFF_RPL))
    {
        if (   !DescCS.Legacy.Gen.u1Long
            || DescCS.Legacy.Gen.u1DefBig /** @todo exactly how does iret (and others) behave with u1Long=1 and u1DefBig=1? \#GP(sel)? */
            || uNewCpl > 2) /** @todo verify SS=0 impossible for ring-3. */
        {
            Log(("iret/64 %04x:%016RX64/%04x:%016RX64 -> invalid SS selector, #GP(0)\n", uNewCs, uNewRip, uNewSs, uNewRsp));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }
        /* Make sure SS is sensible, marked as accessed etc. */
        iemMemFakeStackSelDesc(&DescSS, (uNewSs & X86_SEL_RPL));
    }
    else
    {
        rcStrict = iemMemFetchSelDesc(pVCpu, &DescSS, uNewSs, X86_XCPT_GP); /** @todo Correct exception? */
        if (rcStrict != VINF_SUCCESS)
        {
            Log(("iret/64 %04x:%016RX64/%04x:%016RX64 - %Rrc when fetching SS\n",
                 uNewCs, uNewRip, uNewSs, uNewRsp, VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
    }

    /* Privilege checks. */
    if ((uNewSs & X86_SEL_RPL) != (uNewCs & X86_SEL_RPL))
    {
        Log(("iret/64 %04x:%016RX64/%04x:%016RX64 -> SS.RPL != CS.RPL -> #GP\n", uNewCs, uNewRip, uNewSs, uNewRsp));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewSs);
    }

    uint32_t cbLimitSs;
    if (!(uNewSs & X86_SEL_MASK_OFF_RPL))
        cbLimitSs = UINT32_MAX;
    else
    {
        if (DescSS.Legacy.Gen.u2Dpl != (uNewCs & X86_SEL_RPL))
        {
            Log(("iret/64 %04x:%016RX64/%04x:%016RX64 -> SS.DPL (%d) != CS.RPL -> #GP\n",
                 uNewCs, uNewRip, uNewSs, uNewRsp, DescSS.Legacy.Gen.u2Dpl));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewSs);
        }

        /* Must be a writeable data segment descriptor. */
        if (!DescSS.Legacy.Gen.u1DescType)
        {
            Log(("iret/64 %04x:%016RX64/%04x:%016RX64 -> SS is system segment (%#x) -> #GP\n",
                 uNewCs, uNewRip, uNewSs, uNewRsp, DescSS.Legacy.Gen.u4Type));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewSs);
        }
        if ((DescSS.Legacy.Gen.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_WRITE)) != X86_SEL_TYPE_WRITE)
        {
            Log(("iret/64 %04x:%016RX64/%04x:%016RX64 - not writable data segment (%#x) -> #GP\n",
                 uNewCs, uNewRip, uNewSs, uNewRsp, DescSS.Legacy.Gen.u4Type));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewSs);
        }

        /* Present? */
        if (!DescSS.Legacy.Gen.u1Present)
        {
            Log(("iret/64 %04x:%016RX64/%04x:%016RX64 -> SS not present -> #SS\n", uNewCs, uNewRip, uNewSs, uNewRsp));
            return iemRaiseStackSelectorNotPresentBySelector(pVCpu, uNewSs);
        }
        cbLimitSs = X86DESC_LIMIT_G(&DescSS.Legacy);
    }

    /* Check EIP. */
    if (DescCS.Legacy.Gen.u1Long)
    {
        if (!IEM_IS_CANONICAL(uNewRip))
        {
            Log(("iret/64 %04x:%016RX64/%04x:%016RX64 -> RIP is not canonical -> #GP(0)\n", uNewCs, uNewRip, uNewSs, uNewRsp));
            return iemRaiseNotCanonical(pVCpu);
        }
/** @todo check the location of this... Testcase. */
        if (RT_LIKELY(!DescCS.Legacy.Gen.u1DefBig))
        { /* likely */ }
        else
        {
            Log(("iret/64 %04x:%016RX64/%04x:%016RX64 -> both L and D are set -> #GP(0)\n", uNewCs, uNewRip, uNewSs, uNewRsp));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }
    }
    else
    {
        if (uNewRip > cbLimitCS)
        {
            Log(("iret/64 %04x:%016RX64/%04x:%016RX64 -> EIP is out of bounds (%#x) -> #GP(0)\n",
                 uNewCs, uNewRip, uNewSs, uNewRsp, cbLimitCS));
            /** @todo Which is it, \#GP(0) or \#GP(sel)? */
            return iemRaiseSelectorBoundsBySelector(pVCpu, uNewCs);
        }
    }

    /*
     * Commit the changes, marking CS and SS accessed first since
     * that may fail.
     */
    /** @todo where exactly are these actually marked accessed by a real CPU? */
    if (!(DescCS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
    {
        rcStrict = iemMemMarkSelDescAccessed(pVCpu, uNewCs);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        DescCS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
    }
    if (!(DescSS.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
    {
        rcStrict = iemMemMarkSelDescAccessed(pVCpu, uNewSs);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
        DescSS.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
    }

    uint32_t fEFlagsMask = X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF
                         | X86_EFL_TF | X86_EFL_DF | X86_EFL_OF | X86_EFL_NT;
    if (enmEffOpSize != IEMMODE_16BIT)
        fEFlagsMask |= X86_EFL_RF | X86_EFL_AC | X86_EFL_ID;
    if (IEM_GET_CPL(pVCpu) == 0)
        fEFlagsMask |= X86_EFL_IF | X86_EFL_IOPL | X86_EFL_VIF | X86_EFL_VIP; /* VM is ignored */
    else if (IEM_GET_CPL(pVCpu) <= pVCpu->cpum.GstCtx.eflags.Bits.u2IOPL)
        fEFlagsMask |= X86_EFL_IF;
    uint32_t fEFlagsNew = IEMMISC_GET_EFL(pVCpu);
    fEFlagsNew         &= ~fEFlagsMask;
    fEFlagsNew         |= uNewFlags & fEFlagsMask;
    IEMTLBTRACE_IRET(pVCpu, uNewCs, uNewRip, fEFlagsNew);
#ifdef DBGFTRACE_ENABLED
    RTTraceBufAddMsgF(pVCpu->CTX_SUFF(pVM)->CTX_SUFF(hTraceBuf), "iret/64/%ul%u %08llx -> %04x:%04llx %llx %04x:%04llx",
                      IEM_GET_CPL(pVCpu), uNewCpl, pVCpu->cpum.GstCtx.rip, uNewCs, uNewRip, uNewFlags, uNewSs, uNewRsp);
#endif

    IEMMISC_SET_EFL(pVCpu, fEFlagsNew);
    pVCpu->cpum.GstCtx.rip           = uNewRip;
    pVCpu->cpum.GstCtx.cs.Sel        = uNewCs;
    pVCpu->cpum.GstCtx.cs.ValidSel   = uNewCs;
    pVCpu->cpum.GstCtx.cs.fFlags     = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.cs.Attr.u     = X86DESC_GET_HID_ATTR(&DescCS.Legacy);
    pVCpu->cpum.GstCtx.cs.u32Limit   = cbLimitCS;
    pVCpu->cpum.GstCtx.cs.u64Base    = X86DESC_BASE(&DescCS.Legacy);
    if (pVCpu->cpum.GstCtx.cs.Attr.n.u1Long || pVCpu->cpum.GstCtx.cs.Attr.n.u1DefBig)
        pVCpu->cpum.GstCtx.rsp       = uNewRsp;
    else
        pVCpu->cpum.GstCtx.sp        = (uint16_t)uNewRsp;
    pVCpu->cpum.GstCtx.ss.Sel        = uNewSs;
    pVCpu->cpum.GstCtx.ss.ValidSel   = uNewSs;
    if (!(uNewSs & X86_SEL_MASK_OFF_RPL))
    {
        pVCpu->cpum.GstCtx.ss.fFlags     = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.ss.Attr.u     = X86DESCATTR_UNUSABLE | (uNewCpl << X86DESCATTR_DPL_SHIFT);
        pVCpu->cpum.GstCtx.ss.u32Limit   = UINT32_MAX;
        pVCpu->cpum.GstCtx.ss.u64Base    = 0;
        Log2(("iret/64 new SS: NULL\n"));
    }
    else
    {
        pVCpu->cpum.GstCtx.ss.fFlags     = CPUMSELREG_FLAGS_VALID;
        pVCpu->cpum.GstCtx.ss.Attr.u     = X86DESC_GET_HID_ATTR(&DescSS.Legacy);
        pVCpu->cpum.GstCtx.ss.u32Limit   = cbLimitSs;
        pVCpu->cpum.GstCtx.ss.u64Base    = X86DESC_BASE(&DescSS.Legacy);
        Log2(("iret/64 new SS: base=%#RX64 lim=%#x attr=%#x\n", pVCpu->cpum.GstCtx.ss.u64Base, pVCpu->cpum.GstCtx.ss.u32Limit, pVCpu->cpum.GstCtx.ss.Attr.u));
    }

    if (IEM_GET_CPL(pVCpu) != uNewCpl)
    {
        iemHlpAdjustSelectorForNewCpl(pVCpu, uNewCpl, &pVCpu->cpum.GstCtx.ds);
        iemHlpAdjustSelectorForNewCpl(pVCpu, uNewCpl, &pVCpu->cpum.GstCtx.es);
        iemHlpAdjustSelectorForNewCpl(pVCpu, uNewCpl, &pVCpu->cpum.GstCtx.fs);
        iemHlpAdjustSelectorForNewCpl(pVCpu, uNewCpl, &pVCpu->cpum.GstCtx.gs);
    }

    iemRecalcExecModeAndCplAndAcFlags(pVCpu);

    /* Flush the prefetch buffer. */
    IEM_FLUSH_PREFETCH_HEAVY(pVCpu, cbInstr); /** @todo may light flush if the ring + mode doesn't change */

/** @todo single stepping   */
    return VINF_SUCCESS;
}


/**
 * Implements iret.
 *
 * @param   enmEffOpSize    The effective operand size.
 */
IEM_CIMPL_DEF_1(iemCImpl_iret, IEMMODE, enmEffOpSize)
{
    bool fBlockingNmi = CPUMAreInterruptsInhibitedByNmi(&pVCpu->cpum.GstCtx);

    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    else if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        /*
         * Record whether NMI (or virtual-NMI) blocking is in effect during the execution
         * of this IRET instruction. We need to provide this information as part of some
         * VM-exits.
         *
         * See Intel spec. 27.2.2 "Information for VM Exits Due to Vectored Events".
         */
        if (IEM_VMX_IS_PINCTLS_SET(pVCpu, VMX_PIN_CTLS_VIRT_NMI))
            pVCpu->cpum.GstCtx.hwvirt.vmx.fNmiUnblockingIret = pVCpu->cpum.GstCtx.hwvirt.vmx.fVirtNmiBlocking;
        else
            pVCpu->cpum.GstCtx.hwvirt.vmx.fNmiUnblockingIret = fBlockingNmi;

        /*
         * If "NMI exiting" is set, IRET does not affect blocking of NMIs.
         * See Intel Spec. 25.3 "Changes To Instruction Behavior In VMX Non-root Operation".
         */
        if (IEM_VMX_IS_PINCTLS_SET(pVCpu, VMX_PIN_CTLS_NMI_EXIT))
            fBlockingNmi = false;

        /* Clear virtual-NMI blocking, if any, before causing any further exceptions. */
        pVCpu->cpum.GstCtx.hwvirt.vmx.fVirtNmiBlocking = false;
    }
#endif
    /*
     * The SVM nested-guest intercept for IRET takes priority over all exceptions,
     * The NMI is still held pending (which I assume means blocking of further NMIs
     * is in effect).
     *
     * See AMD spec. 15.9 "Instruction Intercepts".
     * See AMD spec. 15.21.9 "NMI Support".
     */
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_IRET))
    {
        Log(("iret: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_IRET, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /*
     * Clear NMI blocking, if any, before causing any further exceptions.
     * See Intel spec. 6.7.1 "Handling Multiple NMIs".
     */
    if (fBlockingNmi)
        CPUMClearInterruptInhibitingByNmi(&pVCpu->cpum.GstCtx);

    /*
     * Call a mode specific worker.
     */
    VBOXSTRICTRC rcStrict;
    if (IEM_IS_REAL_OR_V86_MODE(pVCpu))
        rcStrict = IEM_CIMPL_CALL_1(iemCImpl_iret_real_v8086, enmEffOpSize);
    else
    {
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_SREG_MASK | CPUMCTX_EXTRN_GDTR | CPUMCTX_EXTRN_LDTR);
        if (IEM_IS_64BIT_CODE(pVCpu))
            rcStrict = IEM_CIMPL_CALL_1(iemCImpl_iret_64bit, enmEffOpSize);
        else
            rcStrict = IEM_CIMPL_CALL_1(iemCImpl_iret_prot, enmEffOpSize);
    }

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    /*
     * Clear NMI unblocking IRET state with the completion of IRET.
     */
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
        pVCpu->cpum.GstCtx.hwvirt.vmx.fNmiUnblockingIret = false;
#endif
    return rcStrict;
}


static void iemLoadallSetSelector(PVMCPUCC pVCpu, uint8_t iSegReg, uint16_t uSel)
{
    PCPUMSELREGHID  pHid = iemSRegGetHid(pVCpu, iSegReg);

    pHid->Sel      = uSel;
    pHid->ValidSel = uSel;
    pHid->fFlags   = CPUMSELREG_FLAGS_VALID;
}


static void iemLoadall286SetDescCache(PVMCPUCC pVCpu, uint8_t iSegReg, uint8_t const *pbMem)
{
    PCPUMSELREGHID  pHid = iemSRegGetHid(pVCpu, iSegReg);

    /* The base is in the first three bytes. */
    pHid->u64Base  = pbMem[0] + (pbMem[1] << 8) + (pbMem[2] << 16);
    /* The attributes are in the fourth byte. */
    pHid->Attr.u   = pbMem[3];
    pHid->Attr.u  &= ~(X86DESCATTR_L | X86DESCATTR_D); /* (just to be on the safe side) */
    /* The limit is in the last two bytes. */
    pHid->u32Limit = pbMem[4] + (pbMem[5] << 8);
}


/**
 * Implements 286 LOADALL (286 CPUs only).
 */
IEM_CIMPL_DEF_0(iemCImpl_loadall286)
{
    NOREF(cbInstr);

    /* Data is loaded from a buffer at 800h. No checks are done on the
     * validity of loaded state.
     *
     * LOADALL only loads the internal CPU state, it does not access any
     * GDT, LDT, or similar tables.
     */

    if (IEM_GET_CPL(pVCpu) != 0)
    {
        Log(("loadall286: CPL must be 0 not %u -> #GP(0)\n", IEM_GET_CPL(pVCpu)));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    uint8_t        bUnmapInfo;
    uint8_t const *pbMem      = NULL;
    RTGCPHYS       GCPtrStart = 0x800;    /* Fixed table location. */
    VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, (void **)&pbMem, &bUnmapInfo, 0x66, UINT8_MAX, GCPtrStart, IEM_ACCESS_SYS_R, 0);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /* The MSW is at offset 0x06. */
    uint16_t const *pau16Mem = (uint16_t const *)(pbMem + 0x06);
    /* Even LOADALL can't clear the MSW.PE bit, though it can set it. */
    uint64_t uNewCr0 = pVCpu->cpum.GstCtx.cr0 & ~(X86_CR0_MP | X86_CR0_EM | X86_CR0_TS);
    uNewCr0 |= *pau16Mem & (X86_CR0_PE | X86_CR0_MP | X86_CR0_EM | X86_CR0_TS);
    uint64_t const uOldCr0 = pVCpu->cpum.GstCtx.cr0;

    CPUMSetGuestCR0(pVCpu, uNewCr0);
    Assert(pVCpu->cpum.GstCtx.cr0 == uNewCr0);

    /* Inform PGM if mode changed. */
    if ((uNewCr0 & X86_CR0_PE) != (uOldCr0 & X86_CR0_PE))
    {
        int rc = PGMFlushTLB(pVCpu, pVCpu->cpum.GstCtx.cr3, true /* global */);
        AssertRCReturn(rc, rc);
        /* ignore informational status codes */
    }
    rcStrict = PGMChangeMode(pVCpu, pVCpu->cpum.GstCtx.cr0, pVCpu->cpum.GstCtx.cr4, pVCpu->cpum.GstCtx.msrEFER,
                             false /* fForce */);

    /* TR selector is at offset 0x16. */
    pau16Mem = (uint16_t const *)(pbMem + 0x16);
    pVCpu->cpum.GstCtx.tr.Sel      = pau16Mem[0];
    pVCpu->cpum.GstCtx.tr.ValidSel = pau16Mem[0];
    pVCpu->cpum.GstCtx.tr.fFlags   = CPUMSELREG_FLAGS_VALID;

    /* Followed by FLAGS... */
    pVCpu->cpum.GstCtx.eflags.u = pau16Mem[1] | X86_EFL_1;
    pVCpu->cpum.GstCtx.ip       = pau16Mem[2];   /* ...and IP. */

    /* LDT is at offset 0x1C. */
    pau16Mem = (uint16_t const *)(pbMem + 0x1C);
    pVCpu->cpum.GstCtx.ldtr.Sel      = pau16Mem[0];
    pVCpu->cpum.GstCtx.ldtr.ValidSel = pau16Mem[0];
    pVCpu->cpum.GstCtx.ldtr.fFlags   = CPUMSELREG_FLAGS_VALID;

    /* Segment registers are at offset 0x1E. */
    pau16Mem = (uint16_t const *)(pbMem + 0x1E);
    iemLoadallSetSelector(pVCpu, X86_SREG_DS, pau16Mem[0]);
    iemLoadallSetSelector(pVCpu, X86_SREG_SS, pau16Mem[1]);
    iemLoadallSetSelector(pVCpu, X86_SREG_CS, pau16Mem[2]);
    iemLoadallSetSelector(pVCpu, X86_SREG_ES, pau16Mem[3]);

    /* GPRs are at offset 0x26. */
    pau16Mem = (uint16_t const *)(pbMem + 0x26);
    pVCpu->cpum.GstCtx.di = pau16Mem[0];
    pVCpu->cpum.GstCtx.si = pau16Mem[1];
    pVCpu->cpum.GstCtx.bp = pau16Mem[2];
    pVCpu->cpum.GstCtx.sp = pau16Mem[3];
    pVCpu->cpum.GstCtx.bx = pau16Mem[4];
    pVCpu->cpum.GstCtx.dx = pau16Mem[5];
    pVCpu->cpum.GstCtx.cx = pau16Mem[6];
    pVCpu->cpum.GstCtx.ax = pau16Mem[7];

    /* Descriptor caches are at offset 0x36, 6 bytes per entry. */
    iemLoadall286SetDescCache(pVCpu, X86_SREG_ES, pbMem + 0x36);
    iemLoadall286SetDescCache(pVCpu, X86_SREG_CS, pbMem + 0x3C);
    iemLoadall286SetDescCache(pVCpu, X86_SREG_SS, pbMem + 0x42);
    iemLoadall286SetDescCache(pVCpu, X86_SREG_DS, pbMem + 0x48);

    /* GDTR contents are at offset 0x4E, 6 bytes. */
    uint8_t const *pau8Mem = pbMem + 0x4E;
    /* NB: Fourth byte "should be zero"; we are ignoring it. */
    RTGCPHYS GCPtrBase = pau8Mem[0] + ((uint32_t)pau8Mem[1] << 8) + ((uint32_t)pau8Mem[2] << 16);
    uint16_t cbLimit   = pau8Mem[4] + ((uint32_t)pau8Mem[5] << 8);
    CPUMSetGuestGDTR(pVCpu, GCPtrBase, cbLimit);

    /* IDTR contents are at offset 0x5A, 6 bytes. */
    pau8Mem   = pbMem + 0x5A;
    GCPtrBase = pau8Mem[0] + ((uint32_t)pau8Mem[1] << 8) + ((uint32_t)pau8Mem[2] << 16);
    cbLimit   = pau8Mem[4] + ((uint32_t)pau8Mem[5] << 8);
    CPUMSetGuestIDTR(pVCpu, GCPtrBase, cbLimit);

    Log(("LOADALL: GDTR:%08RX64/%04X, IDTR:%08RX64/%04X\n", pVCpu->cpum.GstCtx.gdtr.pGdt, pVCpu->cpum.GstCtx.gdtr.cbGdt, pVCpu->cpum.GstCtx.idtr.pIdt, pVCpu->cpum.GstCtx.idtr.cbIdt));
    Log(("LOADALL: CS:%04X, CS base:%08X, limit:%04X, attrs:%02X\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.cs.u64Base, pVCpu->cpum.GstCtx.cs.u32Limit, pVCpu->cpum.GstCtx.cs.Attr.u));
    Log(("LOADALL: DS:%04X, DS base:%08X, limit:%04X, attrs:%02X\n", pVCpu->cpum.GstCtx.ds.Sel, pVCpu->cpum.GstCtx.ds.u64Base, pVCpu->cpum.GstCtx.ds.u32Limit, pVCpu->cpum.GstCtx.ds.Attr.u));
    Log(("LOADALL: ES:%04X, ES base:%08X, limit:%04X, attrs:%02X\n", pVCpu->cpum.GstCtx.es.Sel, pVCpu->cpum.GstCtx.es.u64Base, pVCpu->cpum.GstCtx.es.u32Limit, pVCpu->cpum.GstCtx.es.Attr.u));
    Log(("LOADALL: SS:%04X, SS base:%08X, limit:%04X, attrs:%02X\n", pVCpu->cpum.GstCtx.ss.Sel, pVCpu->cpum.GstCtx.ss.u64Base, pVCpu->cpum.GstCtx.ss.u32Limit, pVCpu->cpum.GstCtx.ss.Attr.u));
    Log(("LOADALL: SI:%04X, DI:%04X, AX:%04X, BX:%04X, CX:%04X, DX:%04X\n", pVCpu->cpum.GstCtx.si, pVCpu->cpum.GstCtx.di, pVCpu->cpum.GstCtx.bx, pVCpu->cpum.GstCtx.bx, pVCpu->cpum.GstCtx.cx, pVCpu->cpum.GstCtx.dx));

    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /*
     * The CPL may change and protected mode may change enabled.  It is taken
     * from the "DPL fields of the SS and CS descriptor caches" but there is no
     * word as to what happens if those are not identical (probably bad things).
     */
    iemRecalcExecModeAndCplAndAcFlags(pVCpu);
    Assert(IEM_IS_16BIT_CODE(pVCpu));

    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_HIDDEN_SEL_REGS | CPUM_CHANGED_IDTR | CPUM_CHANGED_GDTR | CPUM_CHANGED_TR | CPUM_CHANGED_LDTR);

    /* Flush the prefetch buffer. */
    IEM_FLUSH_PREFETCH_HEAVY(pVCpu, cbInstr);

/** @todo single stepping   */
    return rcStrict;
}


/**
 * Implements SYSCALL (AMD and Intel64).
 */
IEM_CIMPL_DEF_0(iemCImpl_syscall)
{


    /*
     * Check preconditions.
     *
     * Note that CPUs described in the documentation may load a few odd values
     * into CS and SS than we allow here.  This has yet to be checked on real
     * hardware.
     */
    if (!(pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_SCE))
    {
        Log(("syscall: Not enabled in EFER -> #UD\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }
    if (!(pVCpu->cpum.GstCtx.cr0 & X86_CR0_PE))
    {
        Log(("syscall: Protected mode is required -> #GP(0)\n"));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }
    if (   IEM_IS_GUEST_CPU_INTEL(pVCpu)
        && !IEM_IS_64BIT_CODE(pVCpu)) //&& !CPUMIsGuestInLongModeEx(IEM_GET_CTX(pVCpu)))
    {
        Log(("syscall: Only available in 64-bit mode on intel -> #UD\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_SYSCALL_MSRS);

    /** @todo verify RPL ignoring and CS=0xfff8 (i.e. SS == 0). */
    /** @todo what about LDT selectors? Shouldn't matter, really. */
    uint16_t uNewCs = (pVCpu->cpum.GstCtx.msrSTAR >> MSR_K6_STAR_SYSCALL_CS_SS_SHIFT) & X86_SEL_MASK_OFF_RPL;
    uint16_t uNewSs = uNewCs + 8;
    if (uNewCs == 0 || uNewSs == 0)
    {
        /** @todo Neither Intel nor AMD document this check. */
        Log(("syscall: msrSTAR.CS = 0 or SS = 0 -> #GP(0)\n"));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /*
     * Hack alert! Convert incoming debug events to slient on Intel.
     * See the dbg+inhibit+ringxfer test in bs3-cpu-weird-1.
     */
    if (   !(pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK_NONSILENT)
        || !IEM_IS_GUEST_CPU_INTEL(pVCpu))
    { /* ignore */ }
    else
    {
        Log(("iemCImpl_syscall: Converting pending %#x debug events to a silent one (intel hack)\n",
             pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK));
        pVCpu->cpum.GstCtx.eflags.uBoth = (pVCpu->cpum.GstCtx.eflags.uBoth & ~CPUMCTX_DBG_HIT_DRX_MASK)
                                        | CPUMCTX_DBG_HIT_DRX_SILENT;
    }

    /*
     * Long mode and legacy mode differs.
     */
    if (CPUMIsGuestInLongModeEx(IEM_GET_CTX(pVCpu)))
    {
        uint64_t uNewRip = IEM_IS_64BIT_CODE(pVCpu) ? pVCpu->cpum.GstCtx.msrLSTAR : pVCpu->cpum.GstCtx. msrCSTAR;

        /* This test isn't in the docs, but I'm not trusting the guys writing
           the MSRs to have validated the values as canonical like they should. */
        if (!IEM_IS_CANONICAL(uNewRip))
        {
            /** @todo Intel claims this can't happen because IA32_LSTAR MSR can't be written with non-canonical address. */
            Log(("syscall: New RIP not canonical -> #UD\n"));
            return iemRaiseUndefinedOpcode(pVCpu);
        }

        /*
         * Commit it.
         */
        Log(("syscall: %04x:%016RX64 [efl=%#llx] -> %04x:%016RX64\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.rflags.u, uNewCs, uNewRip));
        pVCpu->cpum.GstCtx.rcx           = pVCpu->cpum.GstCtx.rip + cbInstr;
        pVCpu->cpum.GstCtx.rip           = uNewRip;

        pVCpu->cpum.GstCtx.rflags.u     &= ~X86_EFL_RF;
        pVCpu->cpum.GstCtx.r11           = pVCpu->cpum.GstCtx.rflags.u;
        pVCpu->cpum.GstCtx.rflags.u     &= ~pVCpu->cpum.GstCtx.msrSFMASK;
        pVCpu->cpum.GstCtx.rflags.u     |= X86_EFL_RA1_MASK;

        pVCpu->cpum.GstCtx.cs.Attr.u     = X86DESCATTR_P | X86DESCATTR_G | X86DESCATTR_L | X86DESCATTR_DT | X86_SEL_TYPE_ER_ACC;
        pVCpu->cpum.GstCtx.ss.Attr.u     = X86DESCATTR_P | X86DESCATTR_G | X86DESCATTR_D | X86DESCATTR_DT | X86_SEL_TYPE_RW_ACC;

        pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~(IEM_F_MODE_MASK | IEM_F_X86_CPL_MASK | IEM_F_X86_AC))
                           | IEM_F_MODE_X86_64BIT;
    }
    else
    {
        /*
         * Commit it.
         */
        Log(("syscall: %04x:%08RX32 [efl=%#x] -> %04x:%08RX32\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.eip, pVCpu->cpum.GstCtx.eflags.u, uNewCs, (uint32_t)(pVCpu->cpum.GstCtx.msrSTAR & MSR_K6_STAR_SYSCALL_EIP_MASK)));
        pVCpu->cpum.GstCtx.rcx           = pVCpu->cpum.GstCtx.eip + cbInstr;
        pVCpu->cpum.GstCtx.rip           = pVCpu->cpum.GstCtx.msrSTAR & MSR_K6_STAR_SYSCALL_EIP_MASK;
        pVCpu->cpum.GstCtx.rflags.u     &= ~(X86_EFL_VM | X86_EFL_IF | X86_EFL_RF);

        pVCpu->cpum.GstCtx.cs.Attr.u     = X86DESCATTR_P | X86DESCATTR_G | X86DESCATTR_D | X86DESCATTR_DT | X86_SEL_TYPE_ER_ACC;
        pVCpu->cpum.GstCtx.ss.Attr.u     = X86DESCATTR_P | X86DESCATTR_G | X86DESCATTR_D | X86DESCATTR_DT | X86_SEL_TYPE_RW_ACC;

        pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~(IEM_F_MODE_MASK | IEM_F_X86_CPL_MASK | IEM_F_X86_AC))
                           | IEM_F_MODE_X86_32BIT_PROT
                           | iemCalc32BitFlatIndicatorEsDs(pVCpu);
    }
    pVCpu->cpum.GstCtx.cs.Sel        = uNewCs;
    pVCpu->cpum.GstCtx.cs.ValidSel   = uNewCs;
    pVCpu->cpum.GstCtx.cs.u64Base    = 0;
    pVCpu->cpum.GstCtx.cs.u32Limit   = UINT32_MAX;
    pVCpu->cpum.GstCtx.cs.fFlags     = CPUMSELREG_FLAGS_VALID;

    pVCpu->cpum.GstCtx.ss.Sel        = uNewSs;
    pVCpu->cpum.GstCtx.ss.ValidSel   = uNewSs;
    pVCpu->cpum.GstCtx.ss.u64Base    = 0;
    pVCpu->cpum.GstCtx.ss.u32Limit   = UINT32_MAX;
    pVCpu->cpum.GstCtx.ss.fFlags     = CPUMSELREG_FLAGS_VALID;

    /* Flush the prefetch buffer. */
    IEM_FLUSH_PREFETCH_HEAVY(pVCpu, cbInstr);

    /*
     * Handle debug events.
     * If TF isn't masked, we're supposed to raise a single step #DB.
     */
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Implements SYSRET (AMD and Intel64).
 *
 * @param   enmEffOpSize    The effective operand size.
 */
IEM_CIMPL_DEF_1(iemCImpl_sysret, IEMMODE, enmEffOpSize)

{
    RT_NOREF_PV(cbInstr);

    /*
     * Check preconditions.
     *
     * Note that CPUs described in the documentation may load a few odd values
     * into CS and SS than we allow here.  This has yet to be checked on real
     * hardware.
     */
    if (!(pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_SCE))
    {
        Log(("sysret: Not enabled in EFER -> #UD\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }
    if (IEM_IS_GUEST_CPU_INTEL(pVCpu) && !CPUMIsGuestInLongModeEx(IEM_GET_CTX(pVCpu)))
    {
        Log(("sysret: Only available in long mode on intel -> #UD\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }
    if (!(pVCpu->cpum.GstCtx.cr0 & X86_CR0_PE))
    {
        Log(("sysret: Protected mode is required -> #GP(0)\n"));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }
    if (IEM_GET_CPL(pVCpu) != 0)
    {
        Log(("sysret: CPL must be 0 not %u -> #GP(0)\n", IEM_GET_CPL(pVCpu)));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_SYSCALL_MSRS);

    /** @todo Does SYSRET verify CS != 0 and SS != 0? Neither is valid in ring-3. */
    uint16_t uNewCs = (pVCpu->cpum.GstCtx.msrSTAR >> MSR_K6_STAR_SYSRET_CS_SS_SHIFT) & X86_SEL_MASK_OFF_RPL;
    uint16_t uNewSs = uNewCs + 8;
    if (enmEffOpSize == IEMMODE_64BIT)
        uNewCs += 16;
    if (uNewCs == 0 || uNewSs == 0)
    {
        Log(("sysret: msrSTAR.CS = 0 or SS = 0 -> #GP(0)\n"));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /*
     * Commit it.
     */
    bool f32Bit = true;
    if (CPUMIsGuestInLongModeEx(IEM_GET_CTX(pVCpu)))
    {
        if (enmEffOpSize == IEMMODE_64BIT)
        {
            Log(("sysret: %04x:%016RX64 [efl=%#llx] -> %04x:%016RX64 [r11=%#llx]\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.rflags.u, uNewCs, pVCpu->cpum.GstCtx.rcx, pVCpu->cpum.GstCtx.r11));
            /* Note! We disregard intel manual regarding the RCX canonical
                     check, ask intel+xen why AMD doesn't do it. */
            pVCpu->cpum.GstCtx.rip       = pVCpu->cpum.GstCtx.rcx;
            pVCpu->cpum.GstCtx.cs.Attr.u = X86DESCATTR_P | X86DESCATTR_G | X86DESCATTR_L | X86DESCATTR_DT | X86_SEL_TYPE_ER_ACC
                                         | (3 << X86DESCATTR_DPL_SHIFT);
            f32Bit = false;
        }
        else
        {
            Log(("sysret: %04x:%016RX64 [efl=%#llx] -> %04x:%08RX32 [r11=%#llx]\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.rflags.u, uNewCs, pVCpu->cpum.GstCtx.ecx, pVCpu->cpum.GstCtx.r11));
            pVCpu->cpum.GstCtx.rip       = pVCpu->cpum.GstCtx.ecx;
            pVCpu->cpum.GstCtx.cs.Attr.u = X86DESCATTR_P | X86DESCATTR_G | X86DESCATTR_D | X86DESCATTR_DT | X86_SEL_TYPE_ER_ACC
                                         | (3 << X86DESCATTR_DPL_SHIFT);
        }
        /** @todo testcase: See what kind of flags we can make SYSRET restore and
         *        what it really ignores. RF and VM are hinted at being zero, by AMD.
         *        Intel says:  RFLAGS := (R11 & 3C7FD7H) | 2; */
        pVCpu->cpum.GstCtx.rflags.u      = pVCpu->cpum.GstCtx.r11 & (X86_EFL_POPF_BITS | X86_EFL_VIF | X86_EFL_VIP);
        pVCpu->cpum.GstCtx.rflags.u     |= X86_EFL_RA1_MASK;
    }
    else
    {
        Log(("sysret: %04x:%08RX32 [efl=%#x] -> %04x:%08RX32\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.eip, pVCpu->cpum.GstCtx.eflags.u, uNewCs, pVCpu->cpum.GstCtx.ecx));
        pVCpu->cpum.GstCtx.rip           = pVCpu->cpum.GstCtx.rcx;
        pVCpu->cpum.GstCtx.rflags.u     |= X86_EFL_IF;
        pVCpu->cpum.GstCtx.cs.Attr.u     = X86DESCATTR_P | X86DESCATTR_G | X86DESCATTR_D | X86DESCATTR_DT | X86_SEL_TYPE_ER_ACC
                                         | (3 << X86DESCATTR_DPL_SHIFT);
    }
    pVCpu->cpum.GstCtx.cs.Sel        = uNewCs | 3;
    pVCpu->cpum.GstCtx.cs.ValidSel   = uNewCs | 3;
    pVCpu->cpum.GstCtx.cs.u64Base    = 0;
    pVCpu->cpum.GstCtx.cs.u32Limit   = UINT32_MAX;
    pVCpu->cpum.GstCtx.cs.fFlags     = CPUMSELREG_FLAGS_VALID;

    /* The SS hidden bits remains unchanged says AMD, we presume they set DPL to 3.
       Intel (and presuably VIA) OTOH sets loads valid ring-3 values it seems, see
       X86_BUG_SYSRET_SS_ATTRS in linux 5.3. */
    if (IEM_IS_GUEST_CPU_AMD(pVCpu))
    {
        Log(("sysret: ss:rsp=%04x:%08RX64 attr=%x -> %04x:%08RX64 attr=%#x\n", pVCpu->cpum.GstCtx.ss.Sel, pVCpu->cpum.GstCtx.rsp, pVCpu->cpum.GstCtx.ss.Attr.u, uNewSs | 3, pVCpu->cpum.GstCtx.rsp, pVCpu->cpum.GstCtx.ss.Attr.u | (3 << X86DESCATTR_DPL_SHIFT) ));
        pVCpu->cpum.GstCtx.ss.Attr.u     |= (3 << X86DESCATTR_DPL_SHIFT);
    }
    else
    {
        Log(("sysret: ss:rsp=%04x:%08RX64 attr=%x -> %04x:%08RX64 attr=%#x\n", pVCpu->cpum.GstCtx.ss.Sel, pVCpu->cpum.GstCtx.rsp, pVCpu->cpum.GstCtx.ss.Attr.u, uNewSs | 3, pVCpu->cpum.GstCtx.rsp, X86DESCATTR_P | X86DESCATTR_G | X86DESCATTR_D | X86DESCATTR_DT | X86_SEL_TYPE_RW_ACC | (3 << X86DESCATTR_DPL_SHIFT) ));
        pVCpu->cpum.GstCtx.ss.Attr.u      = X86DESCATTR_P | X86DESCATTR_G | X86DESCATTR_D | X86DESCATTR_DT | X86_SEL_TYPE_RW_ACC
                                          | (3 << X86DESCATTR_DPL_SHIFT);
        pVCpu->cpum.GstCtx.ss.u64Base     = 0;
        pVCpu->cpum.GstCtx.ss.u32Limit    = UINT32_MAX;
    }
    pVCpu->cpum.GstCtx.ss.Sel        = uNewSs | 3;
    pVCpu->cpum.GstCtx.ss.ValidSel   = uNewSs | 3;
    pVCpu->cpum.GstCtx.ss.fFlags     = CPUMSELREG_FLAGS_VALID;
    /** @todo Testcase: verify that SS.u1Long and SS.u1DefBig are left unchanged
     *        on sysret on AMD and not on intel. */

    if (!f32Bit)
        pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~(IEM_F_MODE_MASK | IEM_F_X86_CPL_MASK | IEM_F_X86_AC))
                           | (3 << IEM_F_X86_CPL_SHIFT)
                           | IEM_F_MODE_X86_64BIT
                           | iemCalcExecAcFlag(pVCpu);
    else
        pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~(IEM_F_MODE_MASK | IEM_F_X86_CPL_MASK | IEM_F_X86_AC))
                           | (3 << IEM_F_X86_CPL_SHIFT)
                           | IEM_F_MODE_X86_32BIT_PROT
                           /** @todo sort out the SS.BASE/LIM/ATTR claim by AMD and maybe we can switch to
                            * iemCalc32BitFlatIndicatorDsEs and move this up into the above branch. */
                           | iemCalc32BitFlatIndicator(pVCpu)
                           | iemCalcExecAcFlag(pVCpu);

    /* Flush the prefetch buffer. */
    IEM_FLUSH_PREFETCH_HEAVY(pVCpu, cbInstr);

/** @todo single step   */
    return VINF_SUCCESS;
}


/**
 * Implements SYSENTER (Intel, 32-bit AMD).
 */
IEM_CIMPL_DEF_0(iemCImpl_sysenter)
{
    RT_NOREF(cbInstr);

    /*
     * Check preconditions.
     *
     * Note that CPUs described in the documentation may load a few odd values
     * into CS and SS than we allow here.  This has yet to be checked on real
     * hardware.
     */
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fSysEnter)
    {
        Log(("sysenter: not supported -=> #UD\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }
    if (!(pVCpu->cpum.GstCtx.cr0 & X86_CR0_PE))
    {
        Log(("sysenter: Protected or long mode is required -> #GP(0)\n"));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }
    bool fIsLongMode = CPUMIsGuestInLongModeEx(IEM_GET_CTX(pVCpu));
    if (IEM_IS_GUEST_CPU_AMD(pVCpu) && fIsLongMode)
    {
        Log(("sysenter: Only available in protected mode on AMD -> #UD\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_SYSENTER_MSRS);
    uint16_t uNewCs = pVCpu->cpum.GstCtx.SysEnter.cs;
    if ((uNewCs & X86_SEL_MASK_OFF_RPL) == 0)
    {
        Log(("sysenter: SYSENTER_CS = %#x -> #GP(0)\n", uNewCs));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /* This test isn't in the docs, it's just a safeguard against missing
       canonical checks when writing the registers. */
    if (RT_LIKELY(   !fIsLongMode
                  || (   IEM_IS_CANONICAL(pVCpu->cpum.GstCtx.SysEnter.eip)
                      && IEM_IS_CANONICAL(pVCpu->cpum.GstCtx.SysEnter.esp))))
    { /* likely */ }
    else
    {
        Log(("sysenter: SYSENTER_EIP = %#RX64 or/and SYSENTER_ESP = %#RX64 not canonical -> #GP(0)\n",
             pVCpu->cpum.GstCtx.SysEnter.eip, pVCpu->cpum.GstCtx.SysEnter.esp));
        return iemRaiseUndefinedOpcode(pVCpu);
    }

/** @todo Test: Sysenter from ring-0, ring-1 and ring-2.  */

    /*
     * Update registers and commit.
     */
    if (fIsLongMode)
    {
        Log(("sysenter: %04x:%016RX64 [efl=%#llx] -> %04x:%016RX64\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
             pVCpu->cpum.GstCtx.rflags.u, uNewCs & X86_SEL_MASK_OFF_RPL, pVCpu->cpum.GstCtx.SysEnter.eip));
        pVCpu->cpum.GstCtx.rip          = pVCpu->cpum.GstCtx.SysEnter.eip;
        pVCpu->cpum.GstCtx.rsp          = pVCpu->cpum.GstCtx.SysEnter.esp;
        pVCpu->cpum.GstCtx.cs.Attr.u    = X86DESCATTR_L | X86DESCATTR_G | X86DESCATTR_P | X86DESCATTR_DT
                                        | X86DESCATTR_LIMIT_HIGH | X86_SEL_TYPE_ER_ACC;
        pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~(IEM_F_MODE_MASK | IEM_F_X86_CPL_MASK | IEM_F_X86_AC))
                           | IEM_F_MODE_X86_64BIT;
    }
    else
    {
        Log(("sysenter: %04x:%08RX32 [efl=%#llx] -> %04x:%08RX32\n", pVCpu->cpum.GstCtx.cs.Sel, (uint32_t)pVCpu->cpum.GstCtx.rip,
             pVCpu->cpum.GstCtx.rflags.u, uNewCs & X86_SEL_MASK_OFF_RPL, (uint32_t)pVCpu->cpum.GstCtx.SysEnter.eip));
        pVCpu->cpum.GstCtx.rip          = (uint32_t)pVCpu->cpum.GstCtx.SysEnter.eip;
        pVCpu->cpum.GstCtx.rsp          = (uint32_t)pVCpu->cpum.GstCtx.SysEnter.esp;
        pVCpu->cpum.GstCtx.cs.Attr.u    = X86DESCATTR_D | X86DESCATTR_G | X86DESCATTR_P | X86DESCATTR_DT
                                        | X86DESCATTR_LIMIT_HIGH | X86_SEL_TYPE_ER_ACC;
        pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~(IEM_F_MODE_MASK | IEM_F_X86_CPL_MASK | IEM_F_X86_AC))
                           | IEM_F_MODE_X86_32BIT_PROT
                           | iemCalc32BitFlatIndicatorEsDs(pVCpu);
    }
    pVCpu->cpum.GstCtx.cs.Sel           = uNewCs & X86_SEL_MASK_OFF_RPL;
    pVCpu->cpum.GstCtx.cs.ValidSel      = uNewCs & X86_SEL_MASK_OFF_RPL;
    pVCpu->cpum.GstCtx.cs.u64Base       = 0;
    pVCpu->cpum.GstCtx.cs.u32Limit      = UINT32_MAX;
    pVCpu->cpum.GstCtx.cs.fFlags        = CPUMSELREG_FLAGS_VALID;

    pVCpu->cpum.GstCtx.ss.Sel           = (uNewCs & X86_SEL_MASK_OFF_RPL) + 8;
    pVCpu->cpum.GstCtx.ss.ValidSel      = (uNewCs & X86_SEL_MASK_OFF_RPL) + 8;
    pVCpu->cpum.GstCtx.ss.u64Base       = 0;
    pVCpu->cpum.GstCtx.ss.u32Limit      = UINT32_MAX;
    pVCpu->cpum.GstCtx.ss.Attr.u        = X86DESCATTR_D | X86DESCATTR_G | X86DESCATTR_P | X86DESCATTR_DT
                                        | X86DESCATTR_LIMIT_HIGH | X86_SEL_TYPE_RW_ACC;
    pVCpu->cpum.GstCtx.ss.fFlags        = CPUMSELREG_FLAGS_VALID;

    pVCpu->cpum.GstCtx.rflags.Bits.u1IF = 0;
    pVCpu->cpum.GstCtx.rflags.Bits.u1VM = 0;
    pVCpu->cpum.GstCtx.rflags.Bits.u1RF = 0;

    /* Flush the prefetch buffer. */
    IEM_FLUSH_PREFETCH_HEAVY(pVCpu, cbInstr);

/** @todo single stepping   */
    return VINF_SUCCESS;
}


/**
 * Implements SYSEXIT (Intel, 32-bit AMD).
 *
 * @param   enmEffOpSize    The effective operand size.
 */
IEM_CIMPL_DEF_1(iemCImpl_sysexit, IEMMODE, enmEffOpSize)
{
    RT_NOREF(cbInstr);

    /*
     * Check preconditions.
     *
     * Note that CPUs described in the documentation may load a few odd values
     * into CS and SS than we allow here.  This has yet to be checked on real
     * hardware.
     */
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fSysEnter)
    {
        Log(("sysexit: not supported -=> #UD\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }
    if (!(pVCpu->cpum.GstCtx.cr0 & X86_CR0_PE))
    {
        Log(("sysexit: Protected or long mode is required -> #GP(0)\n"));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }
    bool fIsLongMode = CPUMIsGuestInLongModeEx(IEM_GET_CTX(pVCpu));
    if (IEM_IS_GUEST_CPU_AMD(pVCpu) && fIsLongMode)
    {
        Log(("sysexit: Only available in protected mode on AMD -> #UD\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }
    if (IEM_GET_CPL(pVCpu) != 0)
    {
        Log(("sysexit: CPL(=%u) != 0 -> #GP(0)\n", IEM_GET_CPL(pVCpu)));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_SYSENTER_MSRS);
    uint16_t uNewCs = pVCpu->cpum.GstCtx.SysEnter.cs;
    if ((uNewCs & X86_SEL_MASK_OFF_RPL) == 0)
    {
        Log(("sysexit: SYSENTER_CS = %#x -> #GP(0)\n", uNewCs));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /*
     * Update registers and commit.
     */
    if (enmEffOpSize == IEMMODE_64BIT)
    {
        Log(("sysexit: %04x:%016RX64 [efl=%#llx] -> %04x:%016RX64\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
             pVCpu->cpum.GstCtx.rflags.u, (uNewCs | 3) + 32, pVCpu->cpum.GstCtx.rcx));
        pVCpu->cpum.GstCtx.rip          = pVCpu->cpum.GstCtx.rdx;
        pVCpu->cpum.GstCtx.rsp          = pVCpu->cpum.GstCtx.rcx;
        pVCpu->cpum.GstCtx.cs.Attr.u    = X86DESCATTR_L | X86DESCATTR_G | X86DESCATTR_P | X86DESCATTR_DT
                                        | X86DESCATTR_LIMIT_HIGH | X86_SEL_TYPE_ER_ACC | (3 << X86DESCATTR_DPL_SHIFT);
        pVCpu->cpum.GstCtx.cs.Sel       = (uNewCs | 3) + 32;
        pVCpu->cpum.GstCtx.cs.ValidSel  = (uNewCs | 3) + 32;
        pVCpu->cpum.GstCtx.ss.Sel       = (uNewCs | 3) + 40;
        pVCpu->cpum.GstCtx.ss.ValidSel  = (uNewCs | 3) + 40;

        pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~(IEM_F_MODE_MASK | IEM_F_X86_CPL_MASK))
                           | (3 << IEM_F_X86_CPL_SHIFT)
                           | IEM_F_MODE_X86_64BIT
                           | iemCalcExecAcFlag(pVCpu);
    }
    else
    {
        Log(("sysexit: %04x:%08RX64 [efl=%#llx] -> %04x:%08RX32\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
             pVCpu->cpum.GstCtx.rflags.u, (uNewCs | 3) + 16, (uint32_t)pVCpu->cpum.GstCtx.edx));
        pVCpu->cpum.GstCtx.rip          = pVCpu->cpum.GstCtx.edx;
        pVCpu->cpum.GstCtx.rsp          = pVCpu->cpum.GstCtx.ecx;
        pVCpu->cpum.GstCtx.cs.Attr.u    = X86DESCATTR_D | X86DESCATTR_G | X86DESCATTR_P | X86DESCATTR_DT
                                        | X86DESCATTR_LIMIT_HIGH | X86_SEL_TYPE_ER_ACC | (3 << X86DESCATTR_DPL_SHIFT);
        pVCpu->cpum.GstCtx.cs.Sel       = (uNewCs | 3) + 16;
        pVCpu->cpum.GstCtx.cs.ValidSel  = (uNewCs | 3) + 16;
        pVCpu->cpum.GstCtx.ss.Sel       = (uNewCs | 3) + 24;
        pVCpu->cpum.GstCtx.ss.ValidSel  = (uNewCs | 3) + 24;

        pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~(IEM_F_MODE_MASK | IEM_F_X86_CPL_MASK))
                           | (3 << IEM_F_X86_CPL_SHIFT)
                           | IEM_F_MODE_X86_32BIT_PROT
                           | iemCalc32BitFlatIndicatorEsDs(pVCpu)
                           | iemCalcExecAcFlag(pVCpu);
    }
    pVCpu->cpum.GstCtx.cs.u64Base       = 0;
    pVCpu->cpum.GstCtx.cs.u32Limit      = UINT32_MAX;
    pVCpu->cpum.GstCtx.cs.fFlags        = CPUMSELREG_FLAGS_VALID;

    pVCpu->cpum.GstCtx.ss.u64Base       = 0;
    pVCpu->cpum.GstCtx.ss.u32Limit      = UINT32_MAX;
    pVCpu->cpum.GstCtx.ss.Attr.u        = X86DESCATTR_D | X86DESCATTR_G | X86DESCATTR_P | X86DESCATTR_DT
                                        | X86DESCATTR_LIMIT_HIGH | X86_SEL_TYPE_RW_ACC | (3 << X86DESCATTR_DPL_SHIFT);
    pVCpu->cpum.GstCtx.ss.fFlags        = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.rflags.Bits.u1RF = 0;

/** @todo single stepping */

    /* Flush the prefetch buffer. */
    IEM_FLUSH_PREFETCH_HEAVY(pVCpu, cbInstr);

    return VINF_SUCCESS;
}


/**
 * Completes a MOV SReg,XXX or POP SReg instruction.
 *
 * When not modifying SS or when we're already in an interrupt shadow we
 * can update RIP and finish the instruction the normal way.
 *
 * Otherwise, the MOV/POP SS interrupt shadow that we now enable will block
 * both TF and DBx events.  The TF will be ignored while the DBx ones will
 * be delayed till the next instruction boundrary.  For more details see
 * @sdmv3{077,200,6.8.3,Masking Exceptions and Interrupts When Switching Stacks}.
 */
DECLINLINE(VBOXSTRICTRC) iemCImpl_LoadSRegFinish(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iSegReg)
{
    if (iSegReg != X86_SREG_SS || CPUMIsInInterruptShadow(&pVCpu->cpum.GstCtx))
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);

    iemRegAddToRip(pVCpu, cbInstr);
    pVCpu->cpum.GstCtx.eflags.uBoth &= ~X86_EFL_RF; /* Shadow int isn't set and DRx is delayed, so only clear RF. */
    CPUMSetInInterruptShadowSs(&pVCpu->cpum.GstCtx);

    return VINF_SUCCESS;
}


/**
 * Common worker for 'pop SReg', 'mov SReg, GReg' and 'lXs GReg, reg/mem'.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling
 *                      thread.
 * @param   iSegReg     The segment register number (valid).
 * @param   uSel        The new selector value.
 */
static VBOXSTRICTRC iemCImpl_LoadSRegWorker(PVMCPUCC pVCpu, uint8_t iSegReg, uint16_t uSel)
{
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iSegReg));
    uint16_t       *pSel = iemSRegRef(pVCpu, iSegReg);
    PCPUMSELREGHID  pHid = iemSRegGetHid(pVCpu, iSegReg);

    Assert(iSegReg <= X86_SREG_GS && iSegReg != X86_SREG_CS);

    /*
     * Real mode and V8086 mode are easy.
     */
    if (IEM_IS_REAL_OR_V86_MODE(pVCpu))
    {
        *pSel           = uSel;
        pHid->u64Base   = (uint32_t)uSel << 4;
        pHid->ValidSel  = uSel;
        pHid->fFlags    = CPUMSELREG_FLAGS_VALID;
#if 0 /* AMD Volume 2, chapter 4.1 - "real mode segmentation" - states that limit and attributes are untouched. */
        /** @todo Does the CPU actually load limits and attributes in the
         *        real/V8086 mode segment load case?  It doesn't for CS in far
         *        jumps...  Affects unreal mode.  */
        pHid->u32Limit          = 0xffff;
        pHid->Attr.u = 0;
        pHid->Attr.n.u1Present  = 1;
        pHid->Attr.n.u1DescType = 1;
        pHid->Attr.n.u4Type     = iSegReg != X86_SREG_CS
                                ? X86_SEL_TYPE_RW
                                : X86_SEL_TYPE_READ | X86_SEL_TYPE_CODE;
#endif

        /* Update the FLAT 32-bit mode flag, if we're in 32-bit unreal mode (unlikely): */
        if (RT_LIKELY(!IEM_IS_32BIT_CODE(pVCpu)))
        { /* likely */ }
        else if (uSel != 0)
            pVCpu->iem.s.fExec &= ~IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK;
        else
            pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK)
                               | iemCalc32BitFlatIndicator(pVCpu);
    }
    /*
     * Protected / long mode - null segment.
     *
     * Check if it's a null segment selector value first, that's OK for DS, ES,
     * FS and GS.  If not null, then we have to load and parse the descriptor.
     */
    else if (!(uSel & X86_SEL_MASK_OFF_RPL))
    {
        Assert(iSegReg != X86_SREG_CS); /** @todo testcase for \#UD on MOV CS, ax! */
        if (iSegReg == X86_SREG_SS)
        {
            /* In 64-bit kernel mode, the stack can be 0 because of the way
               interrupts are dispatched. AMD seems to have a slighly more
               relaxed relationship to SS.RPL than intel does. */
            /** @todo We cannot 'mov ss, 3' in 64-bit kernel mode, can we? There is a testcase (bs-cpu-xcpt-1), but double check this! */
            if (   !IEM_IS_64BIT_CODE(pVCpu)
                || IEM_GET_CPL(pVCpu) > 2
                || (   uSel != IEM_GET_CPL(pVCpu)
                    && !IEM_IS_GUEST_CPU_AMD(pVCpu)) )
            {
                Log(("load sreg %#x -> invalid stack selector, #GP(0)\n", uSel));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }
        }

        *pSel = uSel;   /* Not RPL, remember :-) */
        iemHlpLoadNullDataSelectorProt(pVCpu, pHid, uSel);
        if (iSegReg == X86_SREG_SS)
            pHid->Attr.u |= IEM_GET_CPL(pVCpu) << X86DESCATTR_DPL_SHIFT;

        /* This will affect the FLAT 32-bit mode flag: */
        if (   iSegReg < X86_SREG_FS
            && IEM_IS_32BIT_CODE(pVCpu))
            pVCpu->iem.s.fExec &= ~IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK;
    }
    /*
     * Protected / long mode.
     */
    else
    {
        /* Fetch the descriptor. */
        IEMSELDESC Desc;
        VBOXSTRICTRC rcStrict = iemMemFetchSelDesc(pVCpu, &Desc, uSel, X86_XCPT_GP); /** @todo Correct exception? */
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        /* Check GPs first. */
        if (!Desc.Legacy.Gen.u1DescType)
        {
            Log(("load sreg %d (=%#x) - system selector (%#x) -> #GP\n", iSegReg, uSel, Desc.Legacy.Gen.u4Type));
            return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
        }
        if (iSegReg == X86_SREG_SS) /* SS gets different treatment */
        {
            if (    (Desc.Legacy.Gen.u4Type & X86_SEL_TYPE_CODE)
                || !(Desc.Legacy.Gen.u4Type & X86_SEL_TYPE_WRITE) )
            {
                Log(("load sreg SS, %#x - code or read only (%#x) -> #GP\n", uSel, Desc.Legacy.Gen.u4Type));
                return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
            }
            if ((uSel & X86_SEL_RPL) != IEM_GET_CPL(pVCpu))
            {
                Log(("load sreg SS, %#x - RPL and CPL (%d) differs -> #GP\n", uSel, IEM_GET_CPL(pVCpu)));
                return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
            }
            if (Desc.Legacy.Gen.u2Dpl != IEM_GET_CPL(pVCpu))
            {
                Log(("load sreg SS, %#x - DPL (%d) and CPL (%d) differs -> #GP\n", uSel, Desc.Legacy.Gen.u2Dpl, IEM_GET_CPL(pVCpu)));
                return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
            }
        }
        else
        {
            if ((Desc.Legacy.Gen.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_READ)) == X86_SEL_TYPE_CODE)
            {
                Log(("load sreg%u, %#x - execute only segment -> #GP\n", iSegReg, uSel));
                return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
            }
            if (   (Desc.Legacy.Gen.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF))
                != (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF))
            {
#if 0 /* this is what intel says. */
                if (   (uSel & X86_SEL_RPL) > Desc.Legacy.Gen.u2Dpl
                    && IEM_GET_CPL(pVCpu)   > Desc.Legacy.Gen.u2Dpl)
                {
                    Log(("load sreg%u, %#x - both RPL (%d) and CPL (%d) are greater than DPL (%d) -> #GP\n",
                         iSegReg, uSel, (uSel & X86_SEL_RPL), IEM_GET_CPL(pVCpu), Desc.Legacy.Gen.u2Dpl));
                    return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
                }
#else /* this is what makes more sense. */
                if ((unsigned)(uSel & X86_SEL_RPL) > Desc.Legacy.Gen.u2Dpl)
                {
                    Log(("load sreg%u, %#x - RPL (%d) is greater than DPL (%d) -> #GP\n",
                         iSegReg, uSel, (uSel & X86_SEL_RPL), Desc.Legacy.Gen.u2Dpl));
                    return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
                }
                if (IEM_GET_CPL(pVCpu) > Desc.Legacy.Gen.u2Dpl)
                {
                    Log(("load sreg%u, %#x - CPL (%d) is greater than DPL (%d) -> #GP\n",
                         iSegReg, uSel, IEM_GET_CPL(pVCpu), Desc.Legacy.Gen.u2Dpl));
                    return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uSel);
                }
#endif
            }
        }

        /* Is it there? */
        if (!Desc.Legacy.Gen.u1Present)
        {
            Log(("load sreg%d,%#x - segment not present -> #NP\n", iSegReg, uSel));
            return iemRaiseSelectorNotPresentBySelector(pVCpu, uSel);
        }

        /* The base and limit. */
        uint32_t cbLimit = X86DESC_LIMIT_G(&Desc.Legacy);
        uint64_t u64Base = X86DESC_BASE(&Desc.Legacy);

        /*
         * Ok, everything checked out fine.  Now set the accessed bit before
         * committing the result into the registers.
         */
        if (!(Desc.Legacy.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
        {
            rcStrict = iemMemMarkSelDescAccessed(pVCpu, uSel);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
            Desc.Legacy.Gen.u4Type |= X86_SEL_TYPE_ACCESSED;
        }

        /* commit */
        *pSel = uSel;
        pHid->Attr.u   = X86DESC_GET_HID_ATTR(&Desc.Legacy);
        pHid->u32Limit = cbLimit;
        pHid->u64Base  = u64Base;
        pHid->ValidSel = uSel;
        pHid->fFlags   = CPUMSELREG_FLAGS_VALID;

        /** @todo check if the hidden bits are loaded correctly for 64-bit
         *        mode.  */

        /* This will affect the FLAT 32-bit mode flag: */
        if (   iSegReg < X86_SREG_FS
            && IEM_IS_32BIT_CODE(pVCpu))
            pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK)
                               | iemCalc32BitFlatIndicator(pVCpu);
    }

    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, pHid));
    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_HIDDEN_SEL_REGS);
    return VINF_SUCCESS;
}


/**
 * Implements 'mov SReg, r/m'.
 *
 * @param   iSegReg     The segment register number (valid).
 * @param   uSel        The new selector value.
 */
IEM_CIMPL_DEF_2(iemCImpl_load_SReg, uint8_t, iSegReg, uint16_t, uSel)
{
    VBOXSTRICTRC rcStrict = iemCImpl_LoadSRegWorker(pVCpu, iSegReg, uSel);
    if (rcStrict == VINF_SUCCESS)
        rcStrict = iemCImpl_LoadSRegFinish(pVCpu, cbInstr, iSegReg);
    return rcStrict;
}


/**
 * Implements 'pop SReg'.
 *
 * @param   iSegReg         The segment register number (valid).
 * @param   enmEffOpSize    The efficient operand size (valid).
 */
IEM_CIMPL_DEF_2(iemCImpl_pop_Sreg, uint8_t, iSegReg, IEMMODE, enmEffOpSize)
{
    VBOXSTRICTRC rcStrict;

    /*
     * Read the selector off the stack and join paths with mov ss, reg.
     */
    RTUINT64U TmpRsp;
    TmpRsp.u = pVCpu->cpum.GstCtx.rsp;
    switch (enmEffOpSize)
    {
        case IEMMODE_16BIT:
        {
            uint16_t uSel;
            rcStrict = iemMemStackPopU16Ex(pVCpu, &uSel, &TmpRsp);
            if (rcStrict == VINF_SUCCESS)
                rcStrict = iemCImpl_LoadSRegWorker(pVCpu, iSegReg, uSel);
            break;
        }

        case IEMMODE_32BIT:
        {
            /* Modern Intel CPU only does a WORD sized access here, both as
               segmentation and paging is concerned.  So, we have to emulate
               this to make bs3-cpu-weird-1 happy. */
            if (IEM_IS_GUEST_CPU_INTEL(pVCpu))
            {
                /* We don't have flexible enough stack primitives here, so just
                   do a word pop and add two bytes to SP/RSP on success. */
                uint16_t uSel;
                rcStrict = iemMemStackPopU16Ex(pVCpu, &uSel, &TmpRsp);
                if (rcStrict == VINF_SUCCESS)
                {
                    iemRegAddToRspEx(pVCpu, &TmpRsp, sizeof(uint32_t) - sizeof(uint16_t));
                    rcStrict = iemCImpl_LoadSRegWorker(pVCpu, iSegReg, uSel);
                }
            }
            else
            {
                uint32_t u32Value;
                rcStrict = iemMemStackPopU32Ex(pVCpu, &u32Value, &TmpRsp);
                if (rcStrict == VINF_SUCCESS)
                    rcStrict = iemCImpl_LoadSRegWorker(pVCpu, iSegReg, (uint16_t)u32Value);
            }
            break;
        }

        case IEMMODE_64BIT:
        {
            /* Like for the 32-bit case above, intel only does a WORD access. */
            if (IEM_IS_GUEST_CPU_INTEL(pVCpu))
            {
                uint16_t uSel;
                rcStrict = iemMemStackPopU16Ex(pVCpu, &uSel, &TmpRsp);
                if (rcStrict == VINF_SUCCESS)
                {
                    iemRegAddToRspEx(pVCpu, &TmpRsp, sizeof(uint64_t) - sizeof(uint16_t));
                    rcStrict = iemCImpl_LoadSRegWorker(pVCpu, iSegReg, uSel);
                }
            }
            else
            {
                uint64_t u64Value;
                rcStrict = iemMemStackPopU64Ex(pVCpu, &u64Value, &TmpRsp);
                if (rcStrict == VINF_SUCCESS)
                    rcStrict = iemCImpl_LoadSRegWorker(pVCpu, iSegReg, (uint16_t)u64Value);
            }
            break;
        }
        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }

    /*
     * If the load succeeded, commit the stack change and finish the instruction.
     */
    if (rcStrict == VINF_SUCCESS)
    {
        pVCpu->cpum.GstCtx.rsp = TmpRsp.u;
        rcStrict = iemCImpl_LoadSRegFinish(pVCpu, cbInstr, iSegReg);
    }

    return rcStrict;
}


/**
 * Implements lgs, lfs, les, lds & lss.
 */
IEM_CIMPL_DEF_5(iemCImpl_load_SReg_Greg, uint16_t, uSel, uint64_t, offSeg, uint8_t, iSegReg, uint8_t, iGReg, IEMMODE, enmEffOpSize)
{
    /*
     * Use iemCImpl_LoadSRegWorker to do the tricky segment register loading.
     */
    /** @todo verify and test that mov, pop and lXs works the segment
     *        register loading in the exact same way. */
    VBOXSTRICTRC rcStrict = iemCImpl_LoadSRegWorker(pVCpu, iSegReg, uSel);
    if (rcStrict == VINF_SUCCESS)
    {
        switch (enmEffOpSize)
        {
            case IEMMODE_16BIT:
                iemGRegStoreU16(pVCpu, iGReg, offSeg);
                break;
            case IEMMODE_32BIT:
            case IEMMODE_64BIT:
                iemGRegStoreU64(pVCpu, iGReg, offSeg);
                break;
            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }
    return rcStrict;
}


/**
 * Helper for VERR, VERW, LAR, and LSL and loads the descriptor into memory.
 *
 * @retval VINF_SUCCESS on success.
 * @retval VINF_IEM_SELECTOR_NOT_OK if the selector isn't ok.
 * @retval iemMemFetchSysU64 return value.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   uSel                The selector value.
 * @param   fAllowSysDesc       Whether system descriptors are OK or not.
 * @param   pDesc               Where to return the descriptor on success.
 */
static VBOXSTRICTRC iemCImpl_LoadDescHelper(PVMCPUCC pVCpu, uint16_t uSel, bool fAllowSysDesc, PIEMSELDESC pDesc)
{
    pDesc->Long.au64[0] = 0;
    pDesc->Long.au64[1] = 0;

    if (!(uSel & X86_SEL_MASK_OFF_RPL)) /** @todo test this on 64-bit. */
        return VINF_IEM_SELECTOR_NOT_OK;

    /* Within the table limits? */
    RTGCPTR GCPtrBase;
    if (uSel & X86_SEL_LDT)
    {
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_LDTR);
        if (   !pVCpu->cpum.GstCtx.ldtr.Attr.n.u1Present
            || (uSel | X86_SEL_RPL_LDT) > pVCpu->cpum.GstCtx.ldtr.u32Limit )
            return VINF_IEM_SELECTOR_NOT_OK;
        GCPtrBase = pVCpu->cpum.GstCtx.ldtr.u64Base;
    }
    else
    {
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_GDTR);
        if ((uSel | X86_SEL_RPL_LDT) > pVCpu->cpum.GstCtx.gdtr.cbGdt)
            return VINF_IEM_SELECTOR_NOT_OK;
        GCPtrBase = pVCpu->cpum.GstCtx.gdtr.pGdt;
    }

    /* Fetch the descriptor. */
    VBOXSTRICTRC rcStrict = iemMemFetchSysU64(pVCpu, &pDesc->Legacy.u, UINT8_MAX, GCPtrBase + (uSel & X86_SEL_MASK));
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;
    if (!pDesc->Legacy.Gen.u1DescType)
    {
        if (!fAllowSysDesc)
            return VINF_IEM_SELECTOR_NOT_OK;
        if (CPUMIsGuestInLongModeEx(IEM_GET_CTX(pVCpu)))
        {
            rcStrict = iemMemFetchSysU64(pVCpu, &pDesc->Long.au64[1], UINT8_MAX, GCPtrBase + (uSel & X86_SEL_MASK) + 8);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
        }

    }

    return VINF_SUCCESS;
}


/**
 * Implements verr (fWrite = false) and verw (fWrite = true).
 */
IEM_CIMPL_DEF_2(iemCImpl_VerX, uint16_t, uSel, bool, fWrite)
{
    Assert(!IEM_IS_REAL_OR_V86_MODE(pVCpu));

    /** @todo figure whether the accessed bit is set or not. */

    bool         fAccessible = true;
    IEMSELDESC   Desc;
    VBOXSTRICTRC rcStrict = iemCImpl_LoadDescHelper(pVCpu, uSel, false /*fAllowSysDesc*/, &Desc);
    if (rcStrict == VINF_SUCCESS)
    {
        /* Check the descriptor, order doesn't matter much here. */
        if (   !Desc.Legacy.Gen.u1DescType
            || !Desc.Legacy.Gen.u1Present)
            fAccessible = false;
        else
        {
            if (  fWrite
                ? (Desc.Legacy.Gen.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_WRITE)) != X86_SEL_TYPE_WRITE
                : (Desc.Legacy.Gen.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_READ))  == X86_SEL_TYPE_CODE)
                fAccessible = false;

            /** @todo testcase for the conforming behavior. */
            if (   (Desc.Legacy.Gen.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF))
                != (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF))
            {
                if ((unsigned)(uSel & X86_SEL_RPL) > Desc.Legacy.Gen.u2Dpl)
                    fAccessible = false;
                else if (IEM_GET_CPL(pVCpu) > Desc.Legacy.Gen.u2Dpl)
                    fAccessible = false;
            }
        }

    }
    else if (rcStrict == VINF_IEM_SELECTOR_NOT_OK)
        fAccessible = false;
    else
        return rcStrict;

    /* commit */
    pVCpu->cpum.GstCtx.eflags.Bits.u1ZF = fAccessible;

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements LAR and LSL with 64-bit operand size.
 *
 * @returns VINF_SUCCESS.
 * @param   pu64Dst         Pointer to the destination register.
 * @param   uSel            The selector to load details for.
 * @param   fIsLar          true = LAR, false = LSL.
 */
IEM_CIMPL_DEF_3(iemCImpl_LarLsl_u64, uint64_t *, pu64Dst, uint16_t, uSel, bool, fIsLar)
{
    Assert(!IEM_IS_REAL_OR_V86_MODE(pVCpu));

    /** @todo figure whether the accessed bit is set or not. */

    bool         fDescOk = true;
    IEMSELDESC   Desc;
    VBOXSTRICTRC rcStrict = iemCImpl_LoadDescHelper(pVCpu, uSel, true /*fAllowSysDesc*/, &Desc);
    if (rcStrict == VINF_SUCCESS)
    {
        /*
         * Check the descriptor type.
         */
        if (!Desc.Legacy.Gen.u1DescType)
        {
            if (CPUMIsGuestInLongModeEx(IEM_GET_CTX(pVCpu)))
            {
                if (Desc.Long.Gen.u5Zeros)
                    fDescOk = false;
                else
                    switch (Desc.Long.Gen.u4Type)
                    {
                        /** @todo Intel lists 0 as valid for LSL, verify whether that's correct */
                        case AMD64_SEL_TYPE_SYS_TSS_AVAIL:
                        case AMD64_SEL_TYPE_SYS_TSS_BUSY:
                        case AMD64_SEL_TYPE_SYS_LDT: /** @todo Intel lists this as invalid for LAR, AMD and 32-bit does otherwise. */
                            break;
                        case AMD64_SEL_TYPE_SYS_CALL_GATE:
                            fDescOk = fIsLar;
                            break;
                        default:
                            fDescOk = false;
                            break;
                    }
            }
            else
            {
                switch (Desc.Long.Gen.u4Type)
                {
                    case X86_SEL_TYPE_SYS_286_TSS_AVAIL:
                    case X86_SEL_TYPE_SYS_286_TSS_BUSY:
                    case X86_SEL_TYPE_SYS_386_TSS_AVAIL:
                    case X86_SEL_TYPE_SYS_386_TSS_BUSY:
                    case X86_SEL_TYPE_SYS_LDT:
                        break;
                    case X86_SEL_TYPE_SYS_286_CALL_GATE:
                    case X86_SEL_TYPE_SYS_TASK_GATE:
                    case X86_SEL_TYPE_SYS_386_CALL_GATE:
                        fDescOk = fIsLar;
                        break;
                    default:
                        fDescOk = false;
                        break;
                }
            }
        }
        if (fDescOk)
        {
            /*
             * Check the RPL/DPL/CPL interaction..
             */
            /** @todo testcase for the conforming behavior. */
            if (   (Desc.Legacy.Gen.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF)) != (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF)
                || !Desc.Legacy.Gen.u1DescType)
            {
                if ((unsigned)(uSel & X86_SEL_RPL) > Desc.Legacy.Gen.u2Dpl)
                    fDescOk = false;
                else if (IEM_GET_CPL(pVCpu) > Desc.Legacy.Gen.u2Dpl)
                    fDescOk = false;
            }
        }

        if (fDescOk)
        {
            /*
             * All fine, start committing the result.
             */
            if (fIsLar)
                *pu64Dst = Desc.Legacy.au32[1] & UINT32_C(0x00ffff00);
            else
                *pu64Dst = X86DESC_LIMIT_G(&Desc.Legacy);
        }

    }
    else if (rcStrict == VINF_IEM_SELECTOR_NOT_OK)
        fDescOk = false;
    else
        return rcStrict;

    /* commit flags value and advance rip. */
    pVCpu->cpum.GstCtx.eflags.Bits.u1ZF = fDescOk;
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements LAR and LSL with 16-bit operand size.
 *
 * @returns VINF_SUCCESS.
 * @param   pu16Dst         Pointer to the destination register.
 * @param   uSel            The selector to load details for.
 * @param   fIsLar          true = LAR, false = LSL.
 */
IEM_CIMPL_DEF_3(iemCImpl_LarLsl_u16, uint16_t *, pu16Dst, uint16_t, uSel, bool, fIsLar)
{
    uint64_t u64TmpDst = *pu16Dst;
    IEM_CIMPL_CALL_3(iemCImpl_LarLsl_u64, &u64TmpDst, uSel, fIsLar);
    *pu16Dst = u64TmpDst;
    return VINF_SUCCESS;
}


/**
 * Implements lgdt.
 *
 * @param   iEffSeg         The segment of the new gdtr contents
 * @param   GCPtrEffSrc     The address of the new gdtr contents.
 * @param   enmEffOpSize    The effective operand size.
 */
IEM_CIMPL_DEF_3(iemCImpl_lgdt, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc, IEMMODE, enmEffOpSize)
{
    if (IEM_GET_CPL(pVCpu) != 0)
        return iemRaiseGeneralProtectionFault0(pVCpu);
    Assert(!pVCpu->cpum.GstCtx.eflags.Bits.u1VM);

    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
    else if (   IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
             && IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_DESC_TABLE_EXIT))
    {
        Log(("lgdt: Guest intercept -> VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_NEEDS_INFO_RET(pVCpu, VMX_EXIT_GDTR_IDTR_ACCESS, VMXINSTRID_LGDT, cbInstr);
    }
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_GDTR_WRITES))
    {
        Log(("lgdt: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_GDTR_WRITE, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /*
     * Fetch the limit and base address.
     */
    uint16_t cbLimit;
    RTGCPTR  GCPtrBase;
    VBOXSTRICTRC rcStrict = iemMemFetchDataXdtr(pVCpu, &cbLimit, &GCPtrBase, iEffSeg, GCPtrEffSrc, enmEffOpSize);
    if (rcStrict == VINF_SUCCESS)
    {
        if (   !IEM_IS_64BIT_CODE(pVCpu)
            || X86_IS_CANONICAL(GCPtrBase))
        {
            rcStrict = CPUMSetGuestGDTR(pVCpu, GCPtrBase, cbLimit);
            if (rcStrict == VINF_SUCCESS)
                rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }
        else
        {
            Log(("iemCImpl_lgdt: Non-canonical base %04x:%RGv\n", cbLimit, GCPtrBase));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }
    }
    return rcStrict;
}


/**
 * Implements sgdt.
 *
 * @param   iEffSeg         The segment where to store the gdtr content.
 * @param   GCPtrEffDst     The address where to store the gdtr content.
 */
IEM_CIMPL_DEF_2(iemCImpl_sgdt, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst)
{
    /*
     * Join paths with sidt.
     * Note! No CPL or V8086 checks here, it's a really sad story, ask Intel if
     *       you really must know.
     */
    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
    else if (   IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
             && IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_DESC_TABLE_EXIT))
    {
        Log(("sgdt: Guest intercept -> VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_NEEDS_INFO_RET(pVCpu, VMX_EXIT_GDTR_IDTR_ACCESS, VMXINSTRID_SGDT, cbInstr);
    }
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_GDTR_READS))
    {
        Log(("sgdt: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_GDTR_READ, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_GDTR);
    VBOXSTRICTRC rcStrict = iemMemStoreDataXdtr(pVCpu, pVCpu->cpum.GstCtx.gdtr.cbGdt, pVCpu->cpum.GstCtx.gdtr.pGdt, iEffSeg, GCPtrEffDst);
    if (rcStrict == VINF_SUCCESS)
        rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    return rcStrict;
}


/**
 * Implements lidt.
 *
 * @param   iEffSeg         The segment of the new idtr contents
 * @param   GCPtrEffSrc     The address of the new idtr contents.
 * @param   enmEffOpSize    The effective operand size.
 */
IEM_CIMPL_DEF_3(iemCImpl_lidt, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc, IEMMODE, enmEffOpSize)
{
    if (IEM_GET_CPL(pVCpu) != 0)
        return iemRaiseGeneralProtectionFault0(pVCpu);
    Assert(!pVCpu->cpum.GstCtx.eflags.Bits.u1VM);

    if (!IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_IDTR_WRITES))
    { /* probable */ }
    else
    {
        Log(("lidt: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_IDTR_WRITE, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /*
     * Fetch the limit and base address.
     */
    uint16_t cbLimit;
    RTGCPTR  GCPtrBase;
    VBOXSTRICTRC rcStrict = iemMemFetchDataXdtr(pVCpu, &cbLimit, &GCPtrBase, iEffSeg, GCPtrEffSrc, enmEffOpSize);
    if (rcStrict == VINF_SUCCESS)
    {
        if (   !IEM_IS_64BIT_CODE(pVCpu)
            || X86_IS_CANONICAL(GCPtrBase))
        {
            CPUMSetGuestIDTR(pVCpu, GCPtrBase, cbLimit);
            rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }
        else
        {
            Log(("iemCImpl_lidt: Non-canonical base %04x:%RGv\n", cbLimit, GCPtrBase));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }
    }
    return rcStrict;
}


/**
 * Implements sidt.
 *
 * @param   iEffSeg         The segment where to store the idtr content.
 * @param   GCPtrEffDst     The address where to store the idtr content.
 */
IEM_CIMPL_DEF_2(iemCImpl_sidt, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst)
{
    /*
     * Join paths with sgdt.
     * Note! No CPL or V8086 checks here, it's a really sad story, ask Intel if
     *       you really must know.
     */
    if (!IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_IDTR_READS))
    { /* probable */ }
    else
    {
        Log(("sidt: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_IDTR_READ, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_IDTR);
    VBOXSTRICTRC rcStrict = iemMemStoreDataXdtr(pVCpu, pVCpu->cpum.GstCtx.idtr.cbIdt, pVCpu->cpum.GstCtx.idtr.pIdt, iEffSeg, GCPtrEffDst);
    if (rcStrict == VINF_SUCCESS)
        rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    return rcStrict;
}


/**
 * Implements lldt.
 *
 * @param   uNewLdt     The new LDT selector value.
 */
IEM_CIMPL_DEF_1(iemCImpl_lldt, uint16_t, uNewLdt)
{
    /*
     * Check preconditions.
     */
    if (IEM_IS_REAL_OR_V86_MODE(pVCpu))
    {
        Log(("lldt %04x - real or v8086 mode -> #GP(0)\n", uNewLdt));
        return iemRaiseUndefinedOpcode(pVCpu);
    }
    if (IEM_GET_CPL(pVCpu) != 0)
    {
        Log(("lldt %04x - CPL is %d -> #GP(0)\n", uNewLdt, IEM_GET_CPL(pVCpu)));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /* Nested-guest VMX intercept (SVM is after all checks). */
    /** @todo testcase: exit vs check order. */
    if (   !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
        || !IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_DESC_TABLE_EXIT))
    { /* probable */ }
    else
    {
        Log(("lldt: Guest intercept -> VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_NEEDS_INFO_RET(pVCpu, VMX_EXIT_LDTR_TR_ACCESS, VMXINSTRID_LLDT, cbInstr);
    }

    if (uNewLdt & X86_SEL_LDT)
    {
        Log(("lldt %04x - LDT selector -> #GP\n", uNewLdt));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewLdt);
    }

    /*
     * Now, loading a NULL selector is easy.
     */
    if (!(uNewLdt & X86_SEL_MASK_OFF_RPL))
    {
        /* Nested-guest SVM intercept. */
        if (!IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_LDTR_WRITES))
        { /* probable */ }
        else
        {
            Log(("lldt: Guest intercept -> #VMEXIT\n"));
            IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
            IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_LDTR_WRITE, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }

        Log(("lldt %04x: Loading NULL selector.\n", uNewLdt));
        pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_LDTR;
        CPUMSetGuestLDTR(pVCpu, uNewLdt);
        pVCpu->cpum.GstCtx.ldtr.ValidSel = uNewLdt;
        pVCpu->cpum.GstCtx.ldtr.fFlags   = CPUMSELREG_FLAGS_VALID;
        if (IEM_IS_GUEST_CPU_AMD(pVCpu))
        {
            /* AMD-V seems to leave the base and limit alone. */
            pVCpu->cpum.GstCtx.ldtr.Attr.u = X86DESCATTR_UNUSABLE;
        }
        else
        {
            /* VT-x (Intel 3960x) seems to be doing the following. */
            pVCpu->cpum.GstCtx.ldtr.Attr.u   = X86DESCATTR_UNUSABLE | X86DESCATTR_G | X86DESCATTR_D;
            pVCpu->cpum.GstCtx.ldtr.u64Base  = 0;
            pVCpu->cpum.GstCtx.ldtr.u32Limit = UINT32_MAX;
        }

        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /*
     * Read the descriptor.
     */
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_LDTR | CPUMCTX_EXTRN_GDTR);
    IEMSELDESC Desc;
    VBOXSTRICTRC rcStrict = iemMemFetchSelDesc(pVCpu, &Desc, uNewLdt, X86_XCPT_GP); /** @todo Correct exception? */
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /* Check GPs first. */
    if (Desc.Legacy.Gen.u1DescType)
    {
        Log(("lldt %#x - not system selector (type %x) -> #GP\n", uNewLdt, Desc.Legacy.Gen.u4Type));
        return iemRaiseGeneralProtectionFault(pVCpu, uNewLdt & X86_SEL_MASK_OFF_RPL);
    }
    if (Desc.Legacy.Gen.u4Type != X86_SEL_TYPE_SYS_LDT)
    {
        Log(("lldt %#x - not LDT selector (type %x) -> #GP\n", uNewLdt, Desc.Legacy.Gen.u4Type));
        return iemRaiseGeneralProtectionFault(pVCpu, uNewLdt & X86_SEL_MASK_OFF_RPL);
    }
    uint64_t u64Base;
    if (!IEM_IS_LONG_MODE(pVCpu))
        u64Base = X86DESC_BASE(&Desc.Legacy);
    else
    {
        if (Desc.Long.Gen.u5Zeros)
        {
            Log(("lldt %#x - u5Zeros=%#x -> #GP\n", uNewLdt, Desc.Long.Gen.u5Zeros));
            return iemRaiseGeneralProtectionFault(pVCpu, uNewLdt & X86_SEL_MASK_OFF_RPL);
        }

        u64Base = X86DESC64_BASE(&Desc.Long);
        if (!IEM_IS_CANONICAL(u64Base))
        {
            Log(("lldt %#x - non-canonical base address %#llx -> #GP\n", uNewLdt, u64Base));
            return iemRaiseGeneralProtectionFault(pVCpu, uNewLdt & X86_SEL_MASK_OFF_RPL);
        }
    }

    /* NP */
    if (!Desc.Legacy.Gen.u1Present)
    {
        Log(("lldt %#x - segment not present -> #NP\n", uNewLdt));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, uNewLdt);
    }

    /* Nested-guest SVM intercept. */
    if (!IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_LDTR_WRITES))
    { /* probable */ }
    else
    {
        Log(("lldt: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_LDTR_WRITE, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /*
     * It checks out alright, update the registers.
     */
/** @todo check if the actual value is loaded or if the RPL is dropped */
    CPUMSetGuestLDTR(pVCpu, uNewLdt & X86_SEL_MASK_OFF_RPL);
    pVCpu->cpum.GstCtx.ldtr.ValidSel = uNewLdt & X86_SEL_MASK_OFF_RPL;
    pVCpu->cpum.GstCtx.ldtr.fFlags   = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.ldtr.Attr.u   = X86DESC_GET_HID_ATTR(&Desc.Legacy);
    pVCpu->cpum.GstCtx.ldtr.u32Limit = X86DESC_LIMIT_G(&Desc.Legacy);
    pVCpu->cpum.GstCtx.ldtr.u64Base  = u64Base;

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements sldt GReg
 *
 * @param   iGReg           The general register to store the CRx value in.
 * @param   enmEffOpSize    The operand size.
 */
IEM_CIMPL_DEF_2(iemCImpl_sldt_reg, uint8_t, iGReg, uint8_t, enmEffOpSize)
{
    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
    else if (   IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
             && IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_DESC_TABLE_EXIT))
    {
        Log(("sldt: Guest intercept -> VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_NEEDS_INFO_RET(pVCpu, VMX_EXIT_LDTR_TR_ACCESS, VMXINSTRID_SLDT, cbInstr);
    }
    else
        IEM_SVM_CHECK_INSTR_INTERCEPT(pVCpu, SVM_CTRL_INTERCEPT_LDTR_READS, SVM_EXIT_LDTR_READ, 0, 0, cbInstr);

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_LDTR);
    switch (enmEffOpSize)
    {
        case IEMMODE_16BIT:
            iemGRegStoreU16(pVCpu, iGReg, pVCpu->cpum.GstCtx.ldtr.Sel);
            break;
        case IEMMODE_32BIT:
        case IEMMODE_64BIT:
            iemGRegStoreU64(pVCpu, iGReg, pVCpu->cpum.GstCtx.ldtr.Sel);
            break;
        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements sldt mem.
 *
 * @param   iEffSeg         The effective segment register to use with @a GCPtrMem.
 * @param   GCPtrEffDst     Where to store the 16-bit CR0 value.
 */
IEM_CIMPL_DEF_2(iemCImpl_sldt_mem, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst)
{
    IEM_SVM_CHECK_INSTR_INTERCEPT(pVCpu, SVM_CTRL_INTERCEPT_LDTR_READS, SVM_EXIT_LDTR_READ, 0, 0, cbInstr);

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_LDTR);
    VBOXSTRICTRC rcStrict = iemMemStoreDataU16(pVCpu, iEffSeg, GCPtrEffDst, pVCpu->cpum.GstCtx.ldtr.Sel);
    if (rcStrict == VINF_SUCCESS)
        rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    return rcStrict;
}


/**
 * Implements ltr.
 *
 * @param   uNewTr      The new TSS selector value.
 */
IEM_CIMPL_DEF_1(iemCImpl_ltr, uint16_t, uNewTr)
{
    /*
     * Check preconditions.
     */
    if (IEM_IS_REAL_OR_V86_MODE(pVCpu))
    {
        Log(("ltr %04x - real or v8086 mode -> #GP(0)\n", uNewTr));
        return iemRaiseUndefinedOpcode(pVCpu);
    }
    if (IEM_GET_CPL(pVCpu) != 0)
    {
        Log(("ltr %04x - CPL is %d -> #GP(0)\n", uNewTr, IEM_GET_CPL(pVCpu)));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }
    if (   !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
        || !IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_DESC_TABLE_EXIT))
    { /* probable */ }
    else
    {
        Log(("ltr: Guest intercept -> VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_NEEDS_INFO_RET(pVCpu, VMX_EXIT_LDTR_TR_ACCESS, VMXINSTRID_LTR, cbInstr);
    }
    if (uNewTr & X86_SEL_LDT)
    {
        Log(("ltr %04x - LDT selector -> #GP\n", uNewTr));
        return iemRaiseGeneralProtectionFaultBySelector(pVCpu, uNewTr);
    }
    if (!(uNewTr & X86_SEL_MASK_OFF_RPL))
    {
        Log(("ltr %04x - NULL selector -> #GP(0)\n", uNewTr));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }
    if (!IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_TR_WRITES))
    { /* probable */ }
    else
    {
        Log(("ltr: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_TR_WRITE, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /*
     * Read the descriptor.
     */
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_LDTR | CPUMCTX_EXTRN_GDTR | CPUMCTX_EXTRN_TR);
    IEMSELDESC Desc;
    VBOXSTRICTRC rcStrict = iemMemFetchSelDesc(pVCpu, &Desc, uNewTr, X86_XCPT_GP); /** @todo Correct exception? */
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /* Check GPs first. */
    if (Desc.Legacy.Gen.u1DescType)
    {
        Log(("ltr %#x - not system selector (type %x) -> #GP\n", uNewTr, Desc.Legacy.Gen.u4Type));
        return iemRaiseGeneralProtectionFault(pVCpu, uNewTr & X86_SEL_MASK_OFF_RPL);
    }
    if (   Desc.Legacy.Gen.u4Type != X86_SEL_TYPE_SYS_386_TSS_AVAIL /* same as AMD64_SEL_TYPE_SYS_TSS_AVAIL */
        && (   Desc.Legacy.Gen.u4Type != X86_SEL_TYPE_SYS_286_TSS_AVAIL
            || IEM_IS_LONG_MODE(pVCpu)) )
    {
        Log(("ltr %#x - not an available TSS selector (type %x) -> #GP\n", uNewTr, Desc.Legacy.Gen.u4Type));
        return iemRaiseGeneralProtectionFault(pVCpu, uNewTr & X86_SEL_MASK_OFF_RPL);
    }
    uint64_t u64Base;
    if (!IEM_IS_LONG_MODE(pVCpu))
        u64Base = X86DESC_BASE(&Desc.Legacy);
    else
    {
        if (Desc.Long.Gen.u5Zeros)
        {
            Log(("ltr %#x - u5Zeros=%#x -> #GP\n", uNewTr, Desc.Long.Gen.u5Zeros));
            return iemRaiseGeneralProtectionFault(pVCpu, uNewTr & X86_SEL_MASK_OFF_RPL);
        }

        u64Base = X86DESC64_BASE(&Desc.Long);
        if (!IEM_IS_CANONICAL(u64Base))
        {
            Log(("ltr %#x - non-canonical base address %#llx -> #GP\n", uNewTr, u64Base));
            return iemRaiseGeneralProtectionFault(pVCpu, uNewTr & X86_SEL_MASK_OFF_RPL);
        }
    }

    /* NP */
    if (!Desc.Legacy.Gen.u1Present)
    {
        Log(("ltr %#x - segment not present -> #NP\n", uNewTr));
        return iemRaiseSelectorNotPresentBySelector(pVCpu, uNewTr);
    }

    /*
     * Set it busy.
     * Note! Intel says this should lock down the whole descriptor, but we'll
     *       restrict our selves to 32-bit for now due to lack of inline
     *       assembly and such.
     */
    uint8_t bUnmapInfo;
    void   *pvDesc;
    rcStrict = iemMemMap(pVCpu, &pvDesc, &bUnmapInfo, 8, UINT8_MAX,
                         pVCpu->cpum.GstCtx.gdtr.pGdt + (uNewTr & X86_SEL_MASK_OFF_RPL), IEM_ACCESS_DATA_RW, 0);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;
    switch ((uintptr_t)pvDesc & 3)
    {
        case 0: ASMAtomicBitSet(pvDesc, 40 + 1); break;
        case 1: ASMAtomicBitSet((uint8_t *)pvDesc + 3, 40 + 1 - 24); break;
        case 2: ASMAtomicBitSet((uint8_t *)pvDesc + 2, 40 + 1 - 16); break;
        case 3: ASMAtomicBitSet((uint8_t *)pvDesc + 1, 40 + 1 -  8); break;
    }
    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;
    Desc.Legacy.Gen.u4Type |= X86_SEL_TYPE_SYS_TSS_BUSY_MASK;

    /*
     * It checks out alright, update the registers.
     */
/** @todo check if the actual value is loaded or if the RPL is dropped */
    CPUMSetGuestTR(pVCpu, uNewTr & X86_SEL_MASK_OFF_RPL);
    pVCpu->cpum.GstCtx.tr.ValidSel = uNewTr & X86_SEL_MASK_OFF_RPL;
    pVCpu->cpum.GstCtx.tr.fFlags   = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.tr.Attr.u   = X86DESC_GET_HID_ATTR(&Desc.Legacy);
    pVCpu->cpum.GstCtx.tr.u32Limit = X86DESC_LIMIT_G(&Desc.Legacy);
    pVCpu->cpum.GstCtx.tr.u64Base  = u64Base;

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements str GReg
 *
 * @param   iGReg           The general register to store the CRx value in.
 * @param   enmEffOpSize    The operand size.
 */
IEM_CIMPL_DEF_2(iemCImpl_str_reg, uint8_t, iGReg, uint8_t, enmEffOpSize)
{
    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
    else if (   IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
             && IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_DESC_TABLE_EXIT))
    {
        Log(("str_reg: Guest intercept -> VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_NEEDS_INFO_RET(pVCpu, VMX_EXIT_LDTR_TR_ACCESS, VMXINSTRID_STR, cbInstr);
    }
    else
        IEM_SVM_CHECK_INSTR_INTERCEPT(pVCpu, SVM_CTRL_INTERCEPT_TR_READS, SVM_EXIT_TR_READ, 0, 0, cbInstr);

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_TR);
    switch (enmEffOpSize)
    {
        case IEMMODE_16BIT:
            iemGRegStoreU16(pVCpu, iGReg, pVCpu->cpum.GstCtx.tr.Sel);
            break;
        case IEMMODE_32BIT:
        case IEMMODE_64BIT:
            iemGRegStoreU64(pVCpu, iGReg, pVCpu->cpum.GstCtx.tr.Sel);
            break;
        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements str mem.
 *
 * @param   iEffSeg         The effective segment register to use with @a GCPtrMem.
 * @param   GCPtrEffDst     Where to store the 16-bit CR0 value.
 */
IEM_CIMPL_DEF_2(iemCImpl_str_mem, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst)
{
    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
    else if (   IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
             && IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_DESC_TABLE_EXIT))
    {
        Log(("str_mem: Guest intercept -> VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_NEEDS_INFO_RET(pVCpu, VMX_EXIT_LDTR_TR_ACCESS, VMXINSTRID_STR, cbInstr);
    }
    else
        IEM_SVM_CHECK_INSTR_INTERCEPT(pVCpu, SVM_CTRL_INTERCEPT_TR_READS, SVM_EXIT_TR_READ, 0, 0, cbInstr);

    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_TR);
    VBOXSTRICTRC rcStrict = iemMemStoreDataU16(pVCpu, iEffSeg, GCPtrEffDst, pVCpu->cpum.GstCtx.tr.Sel);
    if (rcStrict == VINF_SUCCESS)
        rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    return rcStrict;
}


/**
 * Implements mov GReg,CRx.
 *
 * @param   iGReg           The general register to store the CRx value in.
 * @param   iCrReg          The CRx register to read (valid).
 */
IEM_CIMPL_DEF_2(iemCImpl_mov_Rd_Cd, uint8_t, iGReg, uint8_t, iCrReg)
{
    if (IEM_GET_CPL(pVCpu) != 0)
        return iemRaiseGeneralProtectionFault0(pVCpu);
    Assert(!pVCpu->cpum.GstCtx.eflags.Bits.u1VM);

    if (!IEM_SVM_IS_READ_CR_INTERCEPT_SET(pVCpu, iCrReg))
    { /* probable */ }
    else
    {
        Log(("iemCImpl_mov_Rd_Cd: Guest intercept CR%u -> #VMEXIT\n", iCrReg));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_CRX_VMEXIT_RET(pVCpu, SVM_EXIT_READ_CR0 + iCrReg, IEMACCESSCRX_MOV_CRX, iGReg);
    }

    /* Read it. */
    uint64_t crX;
    switch (iCrReg)
    {
        case 0:
            IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0);
            crX = pVCpu->cpum.GstCtx.cr0;
            if (IEM_GET_TARGET_CPU(pVCpu) <= IEMTARGETCPU_386)
                crX |= UINT32_C(0x7fffffe0); /* All reserved CR0 flags are set on a 386, just like MSW on 286. */
            break;
        case 2:
            IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_CR2);
            crX = pVCpu->cpum.GstCtx.cr2;
            break;
        case 3:
            IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR3);
            crX = pVCpu->cpum.GstCtx.cr3;
            break;
        case 4:
            IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR4);
            crX = pVCpu->cpum.GstCtx.cr4;
            break;
        case 8:
        {
            IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_APIC_TPR);
            if (!IEM_IS_IN_GUEST(pVCpu))
            { /* probable */ }
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
            else if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
            {
                VBOXSTRICTRC rcStrict = iemVmxVmexitInstrMovFromCr8(pVCpu, iGReg, cbInstr);
                if (rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE)
                    return rcStrict;

                /*
                 * If the Mov-from-CR8 doesn't cause a VM-exit, bits 7:4 of the VTPR is copied
                 * to bits 0:3 of the destination operand. Bits 63:4 of the destination operand
                 * are cleared.
                 *
                 * See Intel Spec. 29.3 "Virtualizing CR8-based TPR Accesses"
                 */
                if (IEM_VMX_IS_PROCCTLS_SET(pVCpu, VMX_PROC_CTLS_USE_TPR_SHADOW))
                {
                    uint32_t const uTpr = iemVmxVirtApicReadRaw32(pVCpu, XAPIC_OFF_TPR);
                    crX = (uTpr >> 4) & 0xf;
                    break;
                }
            }
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
            else if (pVCpu->iem.s.fExec & IEM_F_X86_CTX_SVM)
            {
                PCSVMVMCBCTRL pVmcbCtrl = &pVCpu->cpum.GstCtx.hwvirt.svm.Vmcb.ctrl;
                if (CPUMIsGuestSvmVirtIntrMasking(pVCpu, IEM_GET_CTX(pVCpu)))
                {
                    crX = pVmcbCtrl->IntCtrl.n.u8VTPR & 0xf;
                    break;
                }
            }
#endif
            uint8_t uTpr;
            int rc = APICGetTpr(pVCpu, &uTpr, NULL, NULL);
            if (RT_SUCCESS(rc))
                crX = uTpr >> 4;
            else
                crX = 0;
            break;
        }
        IEM_NOT_REACHED_DEFAULT_CASE_RET(); /* call checks */
    }

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    if (!IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    { /* probable */ }
    else
        switch (iCrReg)
        {
            /* CR0/CR4 reads are subject to masking when in VMX non-root mode. */
            case 0: crX = CPUMGetGuestVmxMaskedCr0(&pVCpu->cpum.GstCtx, pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64Cr0Mask.u); break;
            case 4: crX = CPUMGetGuestVmxMaskedCr4(&pVCpu->cpum.GstCtx, pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64Cr4Mask.u); break;
            case 3:
            {
                VBOXSTRICTRC rcStrict = iemVmxVmexitInstrMovFromCr3(pVCpu, iGReg, cbInstr);
                if (rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE)
                    return rcStrict;
                break;
            }
        }
#endif

    /* Store it. */
    if (IEM_IS_64BIT_CODE(pVCpu))
        iemGRegStoreU64(pVCpu, iGReg, crX);
    else
        iemGRegStoreU64(pVCpu, iGReg, (uint32_t)crX);

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements smsw GReg.
 *
 * @param   iGReg           The general register to store the CRx value in.
 * @param   enmEffOpSize    The operand size.
 */
IEM_CIMPL_DEF_2(iemCImpl_smsw_reg, uint8_t, iGReg, uint8_t, enmEffOpSize)
{
    IEM_SVM_CHECK_READ_CR0_INTERCEPT(pVCpu, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */, cbInstr);

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    uint64_t u64MaskedCr0;
    if (!IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
        u64MaskedCr0 = pVCpu->cpum.GstCtx.cr0;
    else
        u64MaskedCr0 = CPUMGetGuestVmxMaskedCr0(&pVCpu->cpum.GstCtx, pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64Cr0Mask.u);
    uint64_t const u64GuestCr0 = u64MaskedCr0;
#else
    uint64_t const u64GuestCr0 = pVCpu->cpum.GstCtx.cr0;
#endif

    switch (enmEffOpSize)
    {
        case IEMMODE_16BIT:
            if (IEM_GET_TARGET_CPU(pVCpu) > IEMTARGETCPU_386)
                iemGRegStoreU16(pVCpu, iGReg, (uint16_t)u64GuestCr0);
            /* Unused bits are set on 386 and older CPU: */
            else if (IEM_GET_TARGET_CPU(pVCpu) >= IEMTARGETCPU_386)
                iemGRegStoreU16(pVCpu, iGReg, (uint16_t)u64GuestCr0 | 0xffe0);
            else
                iemGRegStoreU16(pVCpu, iGReg, (uint16_t)u64GuestCr0 | 0xfff0);
            break;

/** @todo testcase for bits 31:16. We're not doing that correctly. */

        case IEMMODE_32BIT:
            if (IEM_GET_TARGET_CPU(pVCpu) >= IEMTARGETCPU_386)
                iemGRegStoreU32(pVCpu, iGReg, (uint32_t)u64GuestCr0);
            else /** @todo test this! */
                iemGRegStoreU32(pVCpu, iGReg, (uint32_t)u64GuestCr0 | UINT32_C(0x7fffffe0)); /* Unused bits are set on 386. */
            break;

        case IEMMODE_64BIT:
            iemGRegStoreU64(pVCpu, iGReg, u64GuestCr0);
            break;

        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements smsw mem.
 *
 * @param   iEffSeg         The effective segment register to use with @a GCPtrMem.
 * @param   GCPtrEffDst     Where to store the 16-bit CR0 value.
 */
IEM_CIMPL_DEF_2(iemCImpl_smsw_mem, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst)
{
    uint64_t u64GuestCr0 = pVCpu->cpum.GstCtx.cr0;
    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
    else if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
        u64GuestCr0 = CPUMGetGuestVmxMaskedCr0(&pVCpu->cpum.GstCtx, pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64Cr0Mask.u);
    else
        IEM_SVM_CHECK_READ_CR0_INTERCEPT(pVCpu, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */, cbInstr);

    uint16_t u16Value;
    if (IEM_GET_TARGET_CPU(pVCpu) > IEMTARGETCPU_386)
        u16Value = (uint16_t)u64GuestCr0;
    else if (IEM_GET_TARGET_CPU(pVCpu) >= IEMTARGETCPU_386)
        u16Value = (uint16_t)u64GuestCr0 | 0xffe0;
    else
        u16Value = (uint16_t)u64GuestCr0 | 0xfff0;

    VBOXSTRICTRC rcStrict = iemMemStoreDataU16(pVCpu, iEffSeg, GCPtrEffDst, u16Value);
    if (rcStrict == VINF_SUCCESS)
        rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    return rcStrict;
}


/**
 * Helper for mapping CR3 and PAE PDPEs for 'mov CRx,GReg'.
 */
#define IEM_MAP_PAE_PDPES_AT_CR3_RET(a_pVCpu, a_iCrReg, a_uCr3) \
    do \
    { \
        int const rcX = PGMGstMapPaePdpesAtCr3(a_pVCpu, a_uCr3); \
        if (RT_SUCCESS(rcX)) \
        { /* likely */ } \
        else \
        { \
            /* Either invalid PDPTEs or CR3 second-level translation failed. Raise #GP(0) either way. */ \
            Log(("iemCImpl_load_Cr%#x: Trying to load invalid PAE PDPEs\n", a_iCrReg)); \
            return iemRaiseGeneralProtectionFault0(a_pVCpu); \
        } \
    } while (0)


/**
 * Used to implemented 'mov CRx,GReg' and 'lmsw r/m16'.
 *
 * @param   iCrReg          The CRx register to write (valid).
 * @param   uNewCrX         The new value.
 * @param   enmAccessCrX    The instruction that caused the CrX load.
 * @param   iGReg           The general register in case of a 'mov CRx,GReg'
 *                          instruction.
 */
IEM_CIMPL_DEF_4(iemCImpl_load_CrX, uint8_t, iCrReg, uint64_t, uNewCrX, IEMACCESSCRX, enmAccessCrX, uint8_t, iGReg)
{
    VBOXSTRICTRC    rcStrict;
    int             rc;
#ifndef VBOX_WITH_NESTED_HWVIRT_SVM
    RT_NOREF2(iGReg, enmAccessCrX);
#endif

    /*
     * Try store it.
     * Unfortunately, CPUM only does a tiny bit of the work.
     */
    switch (iCrReg)
    {
        case 0:
        {
            /*
             * Perform checks.
             */
            IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0);

            uint64_t const uOldCrX = pVCpu->cpum.GstCtx.cr0;
            uint32_t const fValid  = CPUMGetGuestCR0ValidMask();

            /* ET is hardcoded on 486 and later. */
            if (IEM_GET_TARGET_CPU(pVCpu) > IEMTARGETCPU_486)
                uNewCrX |= X86_CR0_ET;
            /* The 386 and 486 didn't #GP(0) on attempting to set reserved CR0 bits. ET was settable on 386. */
            else if (IEM_GET_TARGET_CPU(pVCpu) == IEMTARGETCPU_486)
            {
                uNewCrX &= fValid;
                uNewCrX |= X86_CR0_ET;
            }
            else
                uNewCrX &= X86_CR0_PE | X86_CR0_MP | X86_CR0_EM | X86_CR0_TS | X86_CR0_PG | X86_CR0_ET;

            /* Check for reserved bits. */
            if (uNewCrX & ~(uint64_t)fValid)
            {
                Log(("Trying to set reserved CR0 bits: NewCR0=%#llx InvalidBits=%#llx\n", uNewCrX, uNewCrX & ~(uint64_t)fValid));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }

            /* Check for invalid combinations. */
            if (    (uNewCrX & X86_CR0_PG)
                && !(uNewCrX & X86_CR0_PE) )
            {
                Log(("Trying to set CR0.PG without CR0.PE\n"));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }

            if (   !(uNewCrX & X86_CR0_CD)
                && (uNewCrX & X86_CR0_NW) )
            {
                Log(("Trying to clear CR0.CD while leaving CR0.NW set\n"));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }

            if (   !(uNewCrX & X86_CR0_PG)
                && (pVCpu->cpum.GstCtx.cr4 & X86_CR4_PCIDE))
            {
                Log(("Trying to clear CR0.PG while leaving CR4.PCID set\n"));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }

            /* Long mode consistency checks. */
            if (    (uNewCrX & X86_CR0_PG)
                && !(uOldCrX & X86_CR0_PG)
                &&  (pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_LME) )
            {
                if (!(pVCpu->cpum.GstCtx.cr4 & X86_CR4_PAE))
                {
                    Log(("Trying to enabled long mode paging without CR4.PAE set\n"));
                    return iemRaiseGeneralProtectionFault0(pVCpu);
                }
                if (pVCpu->cpum.GstCtx.cs.Attr.n.u1Long)
                {
                    Log(("Trying to enabled long mode paging with a long CS descriptor loaded.\n"));
                    return iemRaiseGeneralProtectionFault0(pVCpu);
                }
            }

            /** @todo testcase: what happens if we disable paging while in 64-bit code? */

            if (!IEM_IS_IN_GUEST(pVCpu))
            { /* probable */ }
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
            /* Check for bits that must remain set or cleared in VMX operation,
               see Intel spec. 23.8 "Restrictions on VMX operation". */
            else if (IEM_VMX_IS_ROOT_MODE(pVCpu))
            {
                uint64_t const uCr0Fixed0 = iemVmxGetCr0Fixed0(pVCpu, IEM_VMX_IS_NON_ROOT_MODE(pVCpu));
                if ((uNewCrX & uCr0Fixed0) != uCr0Fixed0)
                {
                    Log(("Trying to clear reserved CR0 bits in VMX operation: NewCr0=%#llx MB1=%#llx\n", uNewCrX, uCr0Fixed0));
                    return iemRaiseGeneralProtectionFault0(pVCpu);
                }

                uint64_t const uCr0Fixed1 = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Cr0Fixed1;
                if (uNewCrX & ~uCr0Fixed1)
                {
                    Log(("Trying to set reserved CR0 bits in VMX operation: NewCr0=%#llx MB0=%#llx\n", uNewCrX, uCr0Fixed1));
                    return iemRaiseGeneralProtectionFault0(pVCpu);
                }
            }
#endif
            /*
             * SVM nested-guest CR0 write intercepts.
             */
            else if (IEM_SVM_IS_WRITE_CR_INTERCEPT_SET(pVCpu, iCrReg))
            {
                Log(("iemCImpl_load_Cr%#x: Guest intercept -> #VMEXIT\n", iCrReg));
                IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
                IEM_SVM_CRX_VMEXIT_RET(pVCpu, SVM_EXIT_WRITE_CR0, enmAccessCrX, iGReg);
            }
            else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_CR0_SEL_WRITE))
            {
                /* 'lmsw' intercepts regardless of whether the TS/MP bits are actually toggled. */
                if (   enmAccessCrX == IEMACCESSCRX_LMSW
                    || (uNewCrX & ~(X86_CR0_TS | X86_CR0_MP)) != (uOldCrX & ~(X86_CR0_TS | X86_CR0_MP)))
                {
                    Assert(enmAccessCrX != IEMACCESSCRX_CLTS);
                    Log(("iemCImpl_load_Cr%#x: lmsw or bits other than TS/MP changed: Guest intercept -> #VMEXIT\n", iCrReg));
                    IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
                    IEM_SVM_CRX_VMEXIT_RET(pVCpu, SVM_EXIT_CR0_SEL_WRITE, enmAccessCrX, iGReg);
                }
            }

            /*
             * Change EFER.LMA if entering or leaving long mode.
             */
            uint64_t NewEFER = pVCpu->cpum.GstCtx.msrEFER;
            if (   (uNewCrX & X86_CR0_PG) != (uOldCrX & X86_CR0_PG)
                && (pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_LME) )
            {
                if (uNewCrX & X86_CR0_PG)
                    NewEFER |= MSR_K6_EFER_LMA;
                else
                    NewEFER &= ~MSR_K6_EFER_LMA;

                CPUMSetGuestEFER(pVCpu, NewEFER);
                Assert(pVCpu->cpum.GstCtx.msrEFER == NewEFER);
            }

            IEMTLBTRACE_LOAD_CR0(pVCpu, uNewCrX, uOldCrX);

            /*
             * Inform PGM.
             */
            if (    (uNewCrX & (X86_CR0_PG | X86_CR0_WP | X86_CR0_PE | X86_CR0_CD | X86_CR0_NW))
                !=  (uOldCrX & (X86_CR0_PG | X86_CR0_WP | X86_CR0_PE | X86_CR0_CD | X86_CR0_NW)) )
            {
                if (    enmAccessCrX != IEMACCESSCRX_MOV_CRX
                    || !CPUMIsPaePagingEnabled(uNewCrX, pVCpu->cpum.GstCtx.cr4, NewEFER)
                    ||  CPUMIsGuestInSvmNestedHwVirtMode(IEM_GET_CTX(pVCpu)))
                { /* likely */ }
                else
                    IEM_MAP_PAE_PDPES_AT_CR3_RET(pVCpu, iCrReg, pVCpu->cpum.GstCtx.cr3);
                rc = PGMFlushTLB(pVCpu, pVCpu->cpum.GstCtx.cr3, true /* global */);
                AssertRCReturn(rc, rc);
                /* ignore informational status codes */
            }

            /*
             * Change CR0.
             */
            CPUMSetGuestCR0(pVCpu, uNewCrX);
            Assert(pVCpu->cpum.GstCtx.cr0 == uNewCrX);

            /* Update the fExec flags if PE changed. */
            if ((uNewCrX ^ uOldCrX) & X86_CR0_PE)
                iemRecalcExecModeAndCplAndAcFlags(pVCpu);

            /*
             * Inform PGM some more...
             */
            rcStrict = PGMChangeMode(pVCpu, pVCpu->cpum.GstCtx.cr0, pVCpu->cpum.GstCtx.cr4, pVCpu->cpum.GstCtx.msrEFER,
                                     false /* fForce */);
            break;
        }

        /*
         * CR2 can be changed without any restrictions.
         */
        case 2:
        {
            if (!IEM_SVM_IS_WRITE_CR_INTERCEPT_SET(pVCpu, /*cr*/ 2))
            { /* probable */ }
            else
            {
                Log(("iemCImpl_load_Cr%#x: Guest intercept -> #VMEXIT\n", iCrReg));
                IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
                IEM_SVM_CRX_VMEXIT_RET(pVCpu, SVM_EXIT_WRITE_CR2, enmAccessCrX, iGReg);
            }
            pVCpu->cpum.GstCtx.cr2 = uNewCrX;
            pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_CR2;
            rcStrict  = VINF_SUCCESS;
            break;
        }

        /*
         * CR3 is relatively simple, although AMD and Intel have different
         * accounts of how setting reserved bits are handled.  We take intel's
         * word for the lower bits and AMD's for the high bits (63:52).  The
         * lower reserved bits are ignored and left alone; OpenBSD 5.8 relies
         * on this.
         */
        /** @todo Testcase: Setting reserved bits in CR3, especially before
         *        enabling paging. */
        case 3:
        {
            IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR3);

            /* Bit 63 being clear in the source operand with PCIDE indicates no invalidations are required. */
            if (   (pVCpu->cpum.GstCtx.cr4 & X86_CR4_PCIDE)
                && (uNewCrX & RT_BIT_64(63)))
            {
                /** @todo r=ramshankar: avoiding a TLB flush altogether here causes Windows 10
                 *        SMP(w/o nested-paging) to hang during bootup on Skylake systems, see
                 *        Intel spec. 4.10.4.1 "Operations that Invalidate TLBs and
                 *        Paging-Structure Caches". */
                uNewCrX &= ~RT_BIT_64(63);
            }

            /* Check / mask the value. */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
            /* See Intel spec. 27.2.2 "EPT Translation Mechanism" footnote. */
            uint64_t const fInvPhysMask = !CPUMIsGuestVmxEptPagingEnabledEx(IEM_GET_CTX(pVCpu))
                                        ? (UINT64_MAX << IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cMaxPhysAddrWidth)
                                        : (~X86_CR3_EPT_PAGE_MASK & X86_PAGE_4K_BASE_MASK);
#else
            uint64_t const fInvPhysMask = UINT64_C(0xfff0000000000000);
#endif
            if (uNewCrX & fInvPhysMask)
            {
                /** @todo Should we raise this only for 64-bit mode like Intel claims? AMD is
                 *        very vague in this area. As mentioned above, need testcase on real
                 *        hardware... Sigh. */
                Log(("Trying to load CR3 with invalid high bits set: %#llx\n", uNewCrX));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }

            uint64_t fValid;
            if (   (pVCpu->cpum.GstCtx.cr4 & X86_CR4_PAE)
                && (pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_LME))
            {
                /** @todo Redundant? This value has already been validated above. */
                fValid = UINT64_C(0x000fffffffffffff);
            }
            else
                fValid = UINT64_C(0xffffffff);
            if (uNewCrX & ~fValid)
            {
                Log(("Automatically clearing reserved MBZ bits in CR3 load: NewCR3=%#llx ClearedBits=%#llx\n",
                     uNewCrX, uNewCrX & ~fValid));
                uNewCrX &= fValid;
            }

            if (!IEM_SVM_IS_WRITE_CR_INTERCEPT_SET(pVCpu, /*cr*/ 3))
            { /* probable */ }
            else
            {
                Log(("iemCImpl_load_Cr%#x: Guest intercept -> #VMEXIT\n", iCrReg));
                IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
                IEM_SVM_CRX_VMEXIT_RET(pVCpu, SVM_EXIT_WRITE_CR3, enmAccessCrX, iGReg);
            }

            IEMTLBTRACE_LOAD_CR3(pVCpu, uNewCrX, pVCpu->cpum.GstCtx.cr3);

            /* Inform PGM. */
            if (pVCpu->cpum.GstCtx.cr0 & X86_CR0_PG)
            {
                if (   !CPUMIsGuestInPAEModeEx(IEM_GET_CTX(pVCpu))
                    ||  CPUMIsGuestInSvmNestedHwVirtMode(IEM_GET_CTX(pVCpu)))
                { /* likely */ }
                else
                {
                    Assert(enmAccessCrX == IEMACCESSCRX_MOV_CRX);
                    IEM_MAP_PAE_PDPES_AT_CR3_RET(pVCpu, iCrReg, uNewCrX);
                }
                rc = PGMFlushTLB(pVCpu, uNewCrX, !(pVCpu->cpum.GstCtx.cr4 & X86_CR4_PGE));
                AssertRCReturn(rc, rc);
                /* ignore informational status codes */
            }

            /* Make the change. */
            rc = CPUMSetGuestCR3(pVCpu, uNewCrX);
            AssertRCSuccessReturn(rc, rc);

            rcStrict = VINF_SUCCESS;
            break;
        }

        /*
         * CR4 is a bit more tedious as there are bits which cannot be cleared
         * under some circumstances and such.
         */
        case 4:
        {
            IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR4);
            uint64_t const uOldCrX = pVCpu->cpum.GstCtx.cr4;

            /* Reserved bits. */
            uint32_t const fValid = CPUMGetGuestCR4ValidMask(pVCpu->CTX_SUFF(pVM));
            if (uNewCrX & ~(uint64_t)fValid)
            {
                Log(("Trying to set reserved CR4 bits: NewCR4=%#llx InvalidBits=%#llx\n", uNewCrX, uNewCrX & ~(uint64_t)fValid));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }

            bool const fPcide    = !(uOldCrX & X86_CR4_PCIDE) && (uNewCrX & X86_CR4_PCIDE);
            bool const fLongMode = CPUMIsGuestInLongModeEx(IEM_GET_CTX(pVCpu));

            /* PCIDE check. */
            if (   fPcide
                && (   !fLongMode
                    || (pVCpu->cpum.GstCtx.cr3 & UINT64_C(0xfff))))
            {
                Log(("Trying to set PCIDE with invalid PCID or outside long mode. Pcid=%#x\n", (pVCpu->cpum.GstCtx.cr3 & UINT64_C(0xfff))));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }

            /* PAE check. */
            if (   fLongMode
                && (uOldCrX & X86_CR4_PAE)
                && !(uNewCrX & X86_CR4_PAE))
            {
                Log(("Trying to set clear CR4.PAE while long mode is active\n"));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }

            if (!IEM_SVM_IS_WRITE_CR_INTERCEPT_SET(pVCpu, /*cr*/ 4))
            { /* probable */ }
            else
            {
                Log(("iemCImpl_load_Cr%#x: Guest intercept -> #VMEXIT\n", iCrReg));
                IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
                IEM_SVM_CRX_VMEXIT_RET(pVCpu, SVM_EXIT_WRITE_CR4, enmAccessCrX, iGReg);
            }

            /* Check for bits that must remain set or cleared in VMX operation,
               see Intel spec. 23.8 "Restrictions on VMX operation". */
            if (!IEM_VMX_IS_ROOT_MODE(pVCpu))
            { /* probable */ }
            else
            {
                uint64_t const uCr4Fixed0 = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Cr4Fixed0;
                if ((uNewCrX & uCr4Fixed0) != uCr4Fixed0)
                {
                    Log(("Trying to clear reserved CR4 bits in VMX operation: NewCr4=%#llx MB1=%#llx\n", uNewCrX, uCr4Fixed0));
                    return iemRaiseGeneralProtectionFault0(pVCpu);
                }

                uint64_t const uCr4Fixed1 = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Cr4Fixed1;
                if (uNewCrX & ~uCr4Fixed1)
                {
                    Log(("Trying to set reserved CR4 bits in VMX operation: NewCr4=%#llx MB0=%#llx\n", uNewCrX, uCr4Fixed1));
                    return iemRaiseGeneralProtectionFault0(pVCpu);
                }
            }

            IEMTLBTRACE_LOAD_CR4(pVCpu, uNewCrX, uOldCrX);

            /*
             * Notify PGM.
             */
            if ((uNewCrX ^ uOldCrX) & (X86_CR4_PSE | X86_CR4_PAE | X86_CR4_PGE | X86_CR4_PCIDE /* | X86_CR4_SMEP */))
            {
                if (   !CPUMIsPaePagingEnabled(pVCpu->cpum.GstCtx.cr0, uNewCrX, pVCpu->cpum.GstCtx.msrEFER)
                    || CPUMIsGuestInSvmNestedHwVirtMode(IEM_GET_CTX(pVCpu)))
                { /* likely */ }
                else
                {
                    Assert(enmAccessCrX == IEMACCESSCRX_MOV_CRX);
                    IEM_MAP_PAE_PDPES_AT_CR3_RET(pVCpu, iCrReg, pVCpu->cpum.GstCtx.cr3);
                }
                rc = PGMFlushTLB(pVCpu, pVCpu->cpum.GstCtx.cr3, true /* global */);
                AssertRCReturn(rc, rc);
                /* ignore informational status codes */
            }

            /*
             * Change it.
             */
            rc = CPUMSetGuestCR4(pVCpu, uNewCrX);
            AssertRCSuccessReturn(rc, rc);
            Assert(pVCpu->cpum.GstCtx.cr4 == uNewCrX);

            rcStrict = PGMChangeMode(pVCpu, pVCpu->cpum.GstCtx.cr0, pVCpu->cpum.GstCtx.cr4, pVCpu->cpum.GstCtx.msrEFER,
                                     false /* fForce */);
            break;
        }

        /*
         * CR8 maps to the APIC TPR.
         */
        case 8:
        {
            IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_APIC_TPR);
            if (uNewCrX & ~(uint64_t)0xf)
            {
                Log(("Trying to set reserved CR8 bits (%#RX64)\n", uNewCrX));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }

            if (!IEM_IS_IN_GUEST(pVCpu))
            { /* probable */ }
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
            else if (   IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
                     && IEM_VMX_IS_PROCCTLS_SET(pVCpu, VMX_PROC_CTLS_USE_TPR_SHADOW))
            {
                /*
                 * If the Mov-to-CR8 doesn't cause a VM-exit, bits 0:3 of the source operand
                 * is copied to bits 7:4 of the VTPR. Bits 0:3 and bits 31:8 of the VTPR are
                 * cleared. Following this the processor performs TPR virtualization.
                 *
                 * However, we should not perform TPR virtualization immediately here but
                 * after this instruction has completed.
                 *
                 * See Intel spec. 29.3 "Virtualizing CR8-based TPR Accesses"
                 * See Intel spec. 27.1 "Architectural State Before A VM-exit"
                 */
                uint32_t const uTpr = (uNewCrX & 0xf) << 4;
                Log(("iemCImpl_load_Cr%#x: Virtualizing TPR (%#x) write\n", iCrReg, uTpr));
                iemVmxVirtApicWriteRaw32(pVCpu, XAPIC_OFF_TPR, uTpr);
                iemVmxVirtApicSetPendingWrite(pVCpu, XAPIC_OFF_TPR);
                rcStrict = VINF_SUCCESS;
                break;
            }
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
            else if (pVCpu->iem.s.fExec & IEM_F_X86_CTX_SVM)
            {
                if (IEM_SVM_IS_WRITE_CR_INTERCEPT_SET(pVCpu, /*cr*/ 8))
                {
                    Log(("iemCImpl_load_Cr%#x: Guest intercept -> #VMEXIT\n", iCrReg));
                    IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
                    IEM_SVM_CRX_VMEXIT_RET(pVCpu, SVM_EXIT_WRITE_CR8, enmAccessCrX, iGReg);
                }

                pVCpu->cpum.GstCtx.hwvirt.svm.Vmcb.ctrl.IntCtrl.n.u8VTPR = uNewCrX;
                if (CPUMIsGuestSvmVirtIntrMasking(pVCpu, IEM_GET_CTX(pVCpu)))
                {
                    rcStrict = VINF_SUCCESS;
                    break;
                }
            }
#endif
            uint8_t const u8Tpr = (uint8_t)uNewCrX << 4;
            APICSetTpr(pVCpu, u8Tpr);
            rcStrict = VINF_SUCCESS;
            break;
        }

        IEM_NOT_REACHED_DEFAULT_CASE_RET(); /* call checks */
    }

    /*
     * Advance the RIP on success.
     */
    if (RT_SUCCESS(rcStrict))
    {
        if (rcStrict != VINF_SUCCESS)
            iemSetPassUpStatus(pVCpu, rcStrict);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    return rcStrict;
}


/**
 * Implements mov CRx,GReg.
 *
 * @param   iCrReg          The CRx register to write (valid).
 * @param   iGReg           The general register to load the CRx value from.
 */
IEM_CIMPL_DEF_2(iemCImpl_mov_Cd_Rd, uint8_t, iCrReg, uint8_t, iGReg)
{
    if (IEM_GET_CPL(pVCpu) != 0)
        return iemRaiseGeneralProtectionFault0(pVCpu);
    Assert(!pVCpu->cpum.GstCtx.eflags.Bits.u1VM);

    /*
     * Read the new value from the source register and call common worker.
     */
    uint64_t uNewCrX;
    if (IEM_IS_64BIT_CODE(pVCpu))
        uNewCrX = iemGRegFetchU64(pVCpu, iGReg);
    else
        uNewCrX = iemGRegFetchU32(pVCpu, iGReg);

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    if (!IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    { /* probable */ }
    else
    {
        VBOXSTRICTRC rcStrict = VINF_VMX_INTERCEPT_NOT_ACTIVE;
        switch (iCrReg)
        {
            case 0:
            case 4: rcStrict = iemVmxVmexitInstrMovToCr0Cr4(pVCpu, iCrReg, &uNewCrX, iGReg, cbInstr);   break;
            case 3: rcStrict = iemVmxVmexitInstrMovToCr3(pVCpu, uNewCrX, iGReg, cbInstr);               break;
            case 8: rcStrict = iemVmxVmexitInstrMovToCr8(pVCpu, iGReg, cbInstr);                        break;
        }
        if (rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE)
            return rcStrict;
    }
#endif

    return IEM_CIMPL_CALL_4(iemCImpl_load_CrX, iCrReg, uNewCrX, IEMACCESSCRX_MOV_CRX, iGReg);
}


/**
 * Implements 'LMSW r/m16'
 *
 * @param   u16NewMsw       The new value.
 * @param   GCPtrEffDst     The guest-linear address of the source operand in case
 *                          of a memory operand. For register operand, pass
 *                          NIL_RTGCPTR.
 */
IEM_CIMPL_DEF_2(iemCImpl_lmsw, uint16_t, u16NewMsw, RTGCPTR, GCPtrEffDst)
{
    if (IEM_GET_CPL(pVCpu) != 0)
        return iemRaiseGeneralProtectionFault0(pVCpu);
    Assert(!pVCpu->cpum.GstCtx.eflags.Bits.u1VM);
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0);

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    /* Check nested-guest VMX intercept and get updated MSW if there's no VM-exit. */
    if (!IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    { /* probable */ }
    else
    {
        VBOXSTRICTRC rcStrict = iemVmxVmexitInstrLmsw(pVCpu, pVCpu->cpum.GstCtx.cr0, &u16NewMsw, GCPtrEffDst, cbInstr);
        if (rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE)
            return rcStrict;
    }
#else
    RT_NOREF_PV(GCPtrEffDst);
#endif

    /*
     * Compose the new CR0 value and call common worker.
     */
    uint64_t uNewCr0 = pVCpu->cpum.GstCtx.cr0  & ~(X86_CR0_MP | X86_CR0_EM | X86_CR0_TS);
    uNewCr0 |= u16NewMsw & (X86_CR0_PE | X86_CR0_MP | X86_CR0_EM | X86_CR0_TS);
    return IEM_CIMPL_CALL_4(iemCImpl_load_CrX, /*cr*/ 0, uNewCr0, IEMACCESSCRX_LMSW, UINT8_MAX /* iGReg */);
}


/**
 * Implements 'CLTS'.
 */
IEM_CIMPL_DEF_0(iemCImpl_clts)
{
    if (IEM_GET_CPL(pVCpu) != 0)
        return iemRaiseGeneralProtectionFault0(pVCpu);

    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0);
    uint64_t uNewCr0 = pVCpu->cpum.GstCtx.cr0;
    uNewCr0 &= ~X86_CR0_TS;

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    if (!IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    { /* probable */ }
    else
    {
        VBOXSTRICTRC rcStrict = iemVmxVmexitInstrClts(pVCpu, cbInstr);
        if (rcStrict == VINF_VMX_MODIFIES_BEHAVIOR)
            uNewCr0 |= (pVCpu->cpum.GstCtx.cr0 & X86_CR0_TS);
        else if (rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE)
            return rcStrict;
    }
#endif

    return IEM_CIMPL_CALL_4(iemCImpl_load_CrX, /*cr*/ 0, uNewCr0, IEMACCESSCRX_CLTS, UINT8_MAX /* iGReg */);
}


/**
 * Implements mov GReg,DRx.
 *
 * @param   iGReg           The general register to store the DRx value in.
 * @param   iDrReg          The DRx register to read (0-7).
 */
IEM_CIMPL_DEF_2(iemCImpl_mov_Rd_Dd, uint8_t, iGReg, uint8_t, iDrReg)
{
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    /*
     * Check nested-guest VMX intercept.
     * Unlike most other intercepts, the Mov DRx intercept takes preceedence
     * over CPL and CR4.DE and even DR4/DR5 checks.
     *
     * See Intel spec. 25.1.3 "Instructions That Cause VM Exits Conditionally".
     */
    if (!IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    { /* probable */ }
    else
    {
        VBOXSTRICTRC rcStrict = iemVmxVmexitInstrMovDrX(pVCpu, VMXINSTRID_MOV_FROM_DRX, iDrReg, iGReg, cbInstr);
        if (rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE)
            return rcStrict;
    }
#endif

    /*
     * Check preconditions.
     */
    /* Raise GPs. */
    if (IEM_GET_CPL(pVCpu) != 0)
        return iemRaiseGeneralProtectionFault0(pVCpu);
    Assert(!pVCpu->cpum.GstCtx.eflags.Bits.u1VM);
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_DR7);

    /** @todo \#UD in outside ring-0 too? */
    if (iDrReg == 4 || iDrReg == 5)
    {
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_CR4);
        if (pVCpu->cpum.GstCtx.cr4 & X86_CR4_DE)
        {
            Log(("mov r%u,dr%u: CR4.DE=1 -> #GP(0)\n", iGReg, iDrReg));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }
        iDrReg += 2;
    }

    /* Raise #DB if general access detect is enabled. */
    if (pVCpu->cpum.GstCtx.dr[7] & X86_DR7_GD)
    {
        Log(("mov r%u,dr%u: DR7.GD=1 -> #DB\n", iGReg, iDrReg));
        return iemRaiseDebugException(pVCpu);
    }

    /*
     * Read the debug register and store it in the specified general register.
     */
    uint64_t drX;
    switch (iDrReg)
    {
        case 0:
            IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR0_DR3);
            drX = pVCpu->cpum.GstCtx.dr[0];
            break;
        case 1:
            IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR0_DR3);
            drX = pVCpu->cpum.GstCtx.dr[1];
            break;
        case 2:
            IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR0_DR3);
            drX = pVCpu->cpum.GstCtx.dr[2];
            break;
        case 3:
            IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR0_DR3);
            drX = pVCpu->cpum.GstCtx.dr[3];
            break;
        case 6:
            IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR6);
            drX = pVCpu->cpum.GstCtx.dr[6];
            drX |= X86_DR6_RA1_MASK;
            drX &= ~X86_DR6_RAZ_MASK;
            break;
        case 7:
            IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_DR7);
            drX = pVCpu->cpum.GstCtx.dr[7];
            drX |=X86_DR7_RA1_MASK;
            drX &= ~X86_DR7_RAZ_MASK;
            break;
        IEM_NOT_REACHED_DEFAULT_CASE_RET(); /* caller checks */
    }

    /** @todo SVM nested-guest intercept for DR8-DR15? */
    /*
     * Check for any SVM nested-guest intercepts for the DRx read.
     */
    if (!IEM_SVM_IS_READ_DR_INTERCEPT_SET(pVCpu, iDrReg))
    { /* probable */ }
    else
    {
        Log(("mov r%u,dr%u: Guest intercept -> #VMEXIT\n", iGReg, iDrReg));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_READ_DR0 + (iDrReg & 0xf),
                           IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fSvmDecodeAssists ? (iGReg & 7) : 0, 0 /* uExitInfo2 */);
    }

    if (IEM_IS_64BIT_CODE(pVCpu))
        iemGRegStoreU64(pVCpu, iGReg, drX);
    else
        iemGRegStoreU32(pVCpu, iGReg, (uint32_t)drX);

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements mov DRx,GReg.
 *
 * @param   iDrReg          The DRx register to write (valid).
 * @param   iGReg           The general register to load the DRx value from.
 */
IEM_CIMPL_DEF_2(iemCImpl_mov_Dd_Rd, uint8_t, iDrReg, uint8_t, iGReg)
{
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    /*
     * Check nested-guest VMX intercept.
     * Unlike most other intercepts, the Mov DRx intercept takes preceedence
     * over CPL and CR4.DE and even DR4/DR5 checks.
     *
     * See Intel spec. 25.1.3 "Instructions That Cause VM Exits Conditionally".
     */
    if (!IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    { /* probable */ }
    else
    {
        VBOXSTRICTRC rcStrict = iemVmxVmexitInstrMovDrX(pVCpu, VMXINSTRID_MOV_TO_DRX, iDrReg, iGReg, cbInstr);
        if (rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE)
            return rcStrict;
    }
#endif

    /*
     * Check preconditions.
     */
    if (IEM_GET_CPL(pVCpu) != 0)
        return iemRaiseGeneralProtectionFault0(pVCpu);
    Assert(!pVCpu->cpum.GstCtx.eflags.Bits.u1VM);
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_DR7);

    if (iDrReg == 4 || iDrReg == 5)
    {
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_CR4);
        if (pVCpu->cpum.GstCtx.cr4 & X86_CR4_DE)
        {
            Log(("mov dr%u,r%u: CR4.DE=1 -> #GP(0)\n", iDrReg, iGReg));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }
        iDrReg += 2;
    }

    /* Raise #DB if general access detect is enabled. */
    /** @todo is \#DB/DR7.GD raised before any reserved high bits in DR7/DR6
     *        \#GP? */
    if (pVCpu->cpum.GstCtx.dr[7] & X86_DR7_GD)
    {
        Log(("mov dr%u,r%u: DR7.GD=1 -> #DB\n", iDrReg, iGReg));
        return iemRaiseDebugException(pVCpu);
    }

    /*
     * Read the new value from the source register.
     */
    uint64_t uNewDrX;
    if (IEM_IS_64BIT_CODE(pVCpu))
        uNewDrX = iemGRegFetchU64(pVCpu, iGReg);
    else
        uNewDrX = iemGRegFetchU32(pVCpu, iGReg);

    /*
     * Adjust it.
     */
    switch (iDrReg)
    {
        case 0:
        case 1:
        case 2:
        case 3:
            /* nothing to adjust */
            break;

        case 6:
            if (uNewDrX & X86_DR6_MBZ_MASK)
            {
                Log(("mov dr%u,%#llx: DR6 high bits are not zero -> #GP(0)\n", iDrReg, uNewDrX));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }
            uNewDrX |= X86_DR6_RA1_MASK;
            uNewDrX &= ~X86_DR6_RAZ_MASK;
            break;

        case 7:
            if (uNewDrX & X86_DR7_MBZ_MASK)
            {
                Log(("mov dr%u,%#llx: DR7 high bits are not zero -> #GP(0)\n", iDrReg, uNewDrX));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }
            uNewDrX |= X86_DR7_RA1_MASK;
            uNewDrX &= ~X86_DR7_RAZ_MASK;
            break;

        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }

    /** @todo SVM nested-guest intercept for DR8-DR15? */
    /*
     * Check for any SVM nested-guest intercepts for the DRx write.
     */
    if (!IEM_SVM_IS_WRITE_DR_INTERCEPT_SET(pVCpu, iDrReg))
    { /* probable */ }
    else
    {
        Log2(("mov dr%u,r%u: Guest intercept -> #VMEXIT\n", iDrReg, iGReg));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_WRITE_DR0 + (iDrReg & 0xf),
                              IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fSvmDecodeAssists ? (iGReg & 7) : 0, 0 /* uExitInfo2 */);
    }

    /*
     * Do the actual setting.
     */
    if (iDrReg < 4)
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR0_DR3);
    else if (iDrReg == 6)
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR6);

    int rc = CPUMSetGuestDRx(pVCpu, iDrReg, uNewDrX);
    AssertRCSuccessReturn(rc, RT_SUCCESS_NP(rc) ? VERR_IEM_IPE_1 : rc);

    /*
     * Re-init hardware breakpoint summary if it was DR7 that got changed.
     *
     * We also do this when an active data breakpoint is updated so that the
     * TLB entry can be correctly invalidated.
     */
    if (   iDrReg == 7
#ifdef IEM_WITH_DATA_TLB
        || (   iDrReg <= 3
            && (X86_DR7_L_G(iDrReg) & pVCpu->cpum.GstCtx.dr[7])
            && X86_DR7_IS_W_CFG(pVCpu->cpum.GstCtx.dr[7], iDrReg) )
#endif
       )
        iemRecalcExecDbgFlags(pVCpu);

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements mov GReg,TRx.
 *
 * @param   iGReg           The general register to store the
 *                          TRx value in.
 * @param   iTrReg          The TRx register to read (6/7).
 */
IEM_CIMPL_DEF_2(iemCImpl_mov_Rd_Td, uint8_t, iGReg, uint8_t, iTrReg)
{
    /*
     * Check preconditions. NB: This instruction is 386/486 only.
     */

    /* Raise GPs. */
    if (IEM_GET_CPL(pVCpu) != 0)
        return iemRaiseGeneralProtectionFault0(pVCpu);
    Assert(!pVCpu->cpum.GstCtx.eflags.Bits.u1VM);

    if (iTrReg < 6 || iTrReg > 7)
    {
        /** @todo Do Intel CPUs reject this or are the TRs aliased? */
        Log(("mov r%u,tr%u: invalid register -> #GP(0)\n", iGReg, iTrReg));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /*
     * Read the test register and store it in the specified general register.
     * This is currently a dummy implementation that only exists to satisfy
     * old debuggers like WDEB386 or OS/2 KDB which unconditionally read the
     * TR6/TR7 registers. Software which actually depends on the TR values
     * (different on 386/486) is exceedingly rare.
     */
    uint32_t trX;
    switch (iTrReg)
    {
        case 6:
            trX = 0;    /* Currently a dummy. */
            break;
        case 7:
            trX = 0;    /* Currently a dummy. */
            break;
        IEM_NOT_REACHED_DEFAULT_CASE_RET(); /* call checks */
    }

    iemGRegStoreU32(pVCpu, iGReg, trX);

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements mov TRx,GReg.
 *
 * @param   iTrReg          The TRx register to write (valid).
 * @param   iGReg           The general register to load the TRx
 *                          value from.
 */
IEM_CIMPL_DEF_2(iemCImpl_mov_Td_Rd, uint8_t, iTrReg, uint8_t, iGReg)
{
    /*
     * Check preconditions. NB: This instruction is 386/486 only.
     */

    /* Raise GPs. */
    if (IEM_GET_CPL(pVCpu) != 0)
        return iemRaiseGeneralProtectionFault0(pVCpu);
    Assert(!pVCpu->cpum.GstCtx.eflags.Bits.u1VM);

    if (iTrReg < 6 || iTrReg > 7)
    {
        /** @todo Do Intel CPUs reject this or are the TRs aliased? */
        Log(("mov r%u,tr%u: invalid register -> #GP(0)\n", iGReg, iTrReg));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /*
     * Read the new value from the source register.
     */
    uint32_t uNewTrX = iemGRegFetchU32(pVCpu, iGReg);

    /*
     * Here we would do the actual setting if this weren't a dummy implementation.
     * This is currently a dummy implementation that only exists to prevent
     * old debuggers like WDEB386 or OS/2 KDB from crashing.
     */
    RT_NOREF(uNewTrX);

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'INVLPG m'.
 *
 * @param   GCPtrPage       The effective address of the page to invalidate.
 * @remarks Updates the RIP.
 */
IEM_CIMPL_DEF_1(iemCImpl_invlpg, RTGCPTR, GCPtrPage)
{
    /* ring-0 only. */
    if (IEM_GET_CPL(pVCpu) != 0)
        return iemRaiseGeneralProtectionFault0(pVCpu);
    Assert(!pVCpu->cpum.GstCtx.eflags.Bits.u1VM);
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_CR4 | CPUMCTX_EXTRN_EFER);

    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    else if (   IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
             && IEM_VMX_IS_PROCCTLS_SET(pVCpu, VMX_PROC_CTLS_INVLPG_EXIT))
    {
        Log(("invlpg: Guest intercept (%RGp) -> VM-exit\n", GCPtrPage));
        return iemVmxVmexitInstrInvlpg(pVCpu, GCPtrPage, cbInstr);
    }
#endif
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_INVLPG))
    {
        Log(("invlpg: Guest intercept (%RGp) -> #VMEXIT\n", GCPtrPage));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_INVLPG,
                              IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fSvmDecodeAssists ? GCPtrPage : 0, 0 /* uExitInfo2 */);
    }

    int rc = PGMInvalidatePage(pVCpu, GCPtrPage);
    if (rc == VINF_SUCCESS)
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    if (rc == VINF_PGM_SYNC_CR3)
    {
        iemSetPassUpStatus(pVCpu, rc);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    AssertMsg(RT_FAILURE_NP(rc), ("%Rrc\n", rc));
    Log(("PGMInvalidatePage(%RGv) -> %Rrc\n", GCPtrPage, rc));
    return rc;
}


/**
 * Implements INVPCID.
 *
 * @param   iEffSeg              The segment of the invpcid descriptor.
 * @param   GCPtrInvpcidDesc     The address of invpcid descriptor.
 * @param   uInvpcidType         The invalidation type.
 * @remarks Updates the RIP.
 */
IEM_CIMPL_DEF_3(iemCImpl_invpcid, uint8_t, iEffSeg, RTGCPTR, GCPtrInvpcidDesc, uint64_t, uInvpcidType)
{
    /*
     * Check preconditions.
     */
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fInvpcid)
        return iemRaiseUndefinedOpcode(pVCpu);

    /* When in VMX non-root mode and INVPCID is not enabled, it results in #UD. */
    if (RT_LIKELY(   !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
                  || IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_INVPCID)))
    { /* likely */ }
    else
    {
        Log(("invpcid: Not enabled for nested-guest execution -> #UD\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }

    if (IEM_GET_CPL(pVCpu) != 0)
    {
        Log(("invpcid: CPL != 0 -> #GP(0)\n"));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    if (IEM_IS_V86_MODE(pVCpu))
    {
        Log(("invpcid: v8086 mode -> #GP(0)\n"));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /*
     * Check nested-guest intercept.
     *
     * INVPCID causes a VM-exit if "enable INVPCID" and "INVLPG exiting" are
     * both set. We have already checked the former earlier in this function.
     *
     * CPL and virtual-8086 mode checks take priority over this VM-exit.
     * See Intel spec. "25.1.1 Relative Priority of Faults and VM Exits".
     */
    if (   !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
        || !IEM_VMX_IS_PROCCTLS_SET(pVCpu, VMX_PROC_CTLS_INVLPG_EXIT))
    { /* probable */ }
    else
    {
        Log(("invpcid: Guest intercept -> #VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_NEEDS_INFO_RET(pVCpu, VMX_EXIT_INVPCID, VMXINSTRID_NONE, cbInstr);
    }

    if (uInvpcidType > X86_INVPCID_TYPE_MAX_VALID)
    {
        Log(("invpcid: invalid/unrecognized invpcid type %#RX64 -> #GP(0)\n", uInvpcidType));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_CR4 | CPUMCTX_EXTRN_EFER);

    /*
     * Fetch the invpcid descriptor from guest memory.
     */
/** @todo Check if the entire 128 bits are always read for all types.  Check for invalid types as well. */
    RTUINT128U uDesc;
    VBOXSTRICTRC rcStrict = iemMemFetchDataU128(pVCpu, &uDesc, iEffSeg, GCPtrInvpcidDesc);
    if (rcStrict == VINF_SUCCESS)
    {
        /*
         * Validate the descriptor.
         */
        if (uDesc.s.Lo > 0xfff)
        {
            Log(("invpcid: reserved bits set in invpcid descriptor %#RX64 -> #GP(0)\n", uDesc.s.Lo));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }

        RTGCUINTPTR64 const GCPtrInvAddr = uDesc.s.Hi;
        uint8_t       const uPcid        = uDesc.s.Lo & UINT64_C(0xfff);
        uint32_t      const uCr4         = pVCpu->cpum.GstCtx.cr4;
        uint64_t      const uCr3         = pVCpu->cpum.GstCtx.cr3;
        switch (uInvpcidType)
        {
            case X86_INVPCID_TYPE_INDV_ADDR:
            {
                if (!IEM_IS_CANONICAL(GCPtrInvAddr))
                {
                    Log(("invpcid: invalidation address %#RGP is not canonical -> #GP(0)\n", GCPtrInvAddr));
                    return iemRaiseGeneralProtectionFault0(pVCpu);
                }
                if (  !(uCr4 & X86_CR4_PCIDE)
                    && uPcid != 0)
                {
                    Log(("invpcid: invalid pcid %#x\n", uPcid));
                    return iemRaiseGeneralProtectionFault0(pVCpu);
                }

                /* Invalidate mappings for the linear address tagged with PCID except global translations. */
/** @todo PGMFlushTLB is overkill for X86_INVPCID_TYPE_INDV_ADDR. Add a fGlobal parameter
 * to PGMInvalidatePage or add a new function to support this variation of invlpg. */
                PGMFlushTLB(pVCpu, uCr3, false /* fGlobal */);
                break;
            }

            case X86_INVPCID_TYPE_SINGLE_CONTEXT:
            {
                if (  !(uCr4 & X86_CR4_PCIDE)
                    && uPcid != 0)
                {
                    Log(("invpcid: invalid pcid %#x\n", uPcid));
                    return iemRaiseGeneralProtectionFault0(pVCpu);
                }
                /* Invalidate all mappings associated with PCID except global translations. */
                PGMFlushTLB(pVCpu, uCr3, false /* fGlobal */);
                break;
            }

            case X86_INVPCID_TYPE_ALL_CONTEXT_INCL_GLOBAL:
            {
                PGMFlushTLB(pVCpu, uCr3, true /* fGlobal */);
                break;
            }

            case X86_INVPCID_TYPE_ALL_CONTEXT_EXCL_GLOBAL:
            {
                PGMFlushTLB(pVCpu, uCr3, false /* fGlobal */);
                break;
            }
            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
        rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }
    return rcStrict;
}


/**
 * Implements INVD.
 */
IEM_CIMPL_DEF_0(iemCImpl_invd)
{
    if (IEM_GET_CPL(pVCpu) != 0)
    {
        Log(("invd: CPL != 0 -> #GP(0)\n"));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
    else if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
        IEM_VMX_VMEXIT_INSTR_RET(pVCpu, VMX_EXIT_INVD, cbInstr);
    else
        IEM_SVM_CHECK_INSTR_INTERCEPT(pVCpu, SVM_CTRL_INTERCEPT_INVD, SVM_EXIT_INVD, 0, 0, cbInstr);

    /* We currently take no action here. */
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements WBINVD.
 */
IEM_CIMPL_DEF_0(iemCImpl_wbinvd)
{
    if (IEM_GET_CPL(pVCpu) != 0)
    {
        Log(("wbinvd: CPL != 0 -> #GP(0)\n"));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
    else if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
        IEM_VMX_VMEXIT_INSTR_RET(pVCpu, VMX_EXIT_WBINVD, cbInstr);
    else
        IEM_SVM_CHECK_INSTR_INTERCEPT(pVCpu, SVM_CTRL_INTERCEPT_WBINVD, SVM_EXIT_WBINVD, 0, 0, cbInstr);

    /* We currently take no action here. */
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/** Opcode 0x0f 0xaa. */
IEM_CIMPL_DEF_0(iemCImpl_rsm)
{
    IEM_SVM_CHECK_INSTR_INTERCEPT(pVCpu, SVM_CTRL_INTERCEPT_RSM, SVM_EXIT_RSM, 0, 0, cbInstr);
    NOREF(cbInstr);
    return iemRaiseUndefinedOpcode(pVCpu);
}


/**
 * Implements RDTSC.
 */
IEM_CIMPL_DEF_0(iemCImpl_rdtsc)
{
    /*
     * Check preconditions.
     */
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fTsc)
        return iemRaiseUndefinedOpcode(pVCpu);

    if (IEM_GET_CPL(pVCpu) != 0)
    {
        IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR4);
        if (pVCpu->cpum.GstCtx.cr4 & X86_CR4_TSD)
        {
            Log(("rdtsc: CR4.TSD and CPL=%u -> #GP(0)\n", IEM_GET_CPL(pVCpu)));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }
    }

    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
    else if (   IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
             && IEM_VMX_IS_PROCCTLS_SET(pVCpu, VMX_PROC_CTLS_RDTSC_EXIT))
    {
        Log(("rdtsc: Guest intercept -> VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_RET(pVCpu, VMX_EXIT_RDTSC, cbInstr);
    }
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_RDTSC))
    {
        Log(("rdtsc: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_RDTSC, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /*
     * Do the job.
     */
    uint64_t uTicks = TMCpuTickGet(pVCpu);
#if defined(VBOX_WITH_NESTED_HWVIRT_SVM) || defined(VBOX_WITH_NESTED_HWVIRT_VMX)
    uTicks = CPUMApplyNestedGuestTscOffset(pVCpu, uTicks);
#endif
    pVCpu->cpum.GstCtx.rax = RT_LO_U32(uTicks);
    pVCpu->cpum.GstCtx.rdx = RT_HI_U32(uTicks);
    pVCpu->cpum.GstCtx.fExtrn &= ~(CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RDX); /* For IEMExecDecodedRdtsc. */
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements RDTSC.
 */
IEM_CIMPL_DEF_0(iemCImpl_rdtscp)
{
    /*
     * Check preconditions.
     */
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fRdTscP)
        return iemRaiseUndefinedOpcode(pVCpu);

    if (RT_LIKELY(    !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
                  || IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_RDTSCP)))
    { /* likely */ }
    else
    {
        Log(("rdtscp: Not enabled for VMX non-root mode -> #UD\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }

    if (IEM_GET_CPL(pVCpu) != 0)
    {
        IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR4);
        if (pVCpu->cpum.GstCtx.cr4 & X86_CR4_TSD)
        {
            Log(("rdtscp: CR4.TSD and CPL=%u -> #GP(0)\n", IEM_GET_CPL(pVCpu)));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }
    }

    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
    else if (   IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
             && IEM_VMX_IS_PROCCTLS_SET(pVCpu, VMX_PROC_CTLS_RDTSC_EXIT))
    {
        Log(("rdtscp: Guest intercept -> VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_RET(pVCpu, VMX_EXIT_RDTSCP, cbInstr);
    }
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_RDTSCP))
    {
        Log(("rdtscp: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_RDTSCP, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /*
     * Do the job.
     * Query the MSR first in case of trips to ring-3.
     */
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_TSC_AUX);
    VBOXSTRICTRC rcStrict = CPUMQueryGuestMsr(pVCpu, MSR_K8_TSC_AUX, &pVCpu->cpum.GstCtx.rcx);
    if (rcStrict == VINF_SUCCESS)
    {
        /* Low dword of the TSC_AUX msr only. */
        pVCpu->cpum.GstCtx.rcx &= UINT32_C(0xffffffff);

        uint64_t uTicks = TMCpuTickGet(pVCpu);
#if defined(VBOX_WITH_NESTED_HWVIRT_SVM) || defined(VBOX_WITH_NESTED_HWVIRT_VMX)
        uTicks = CPUMApplyNestedGuestTscOffset(pVCpu, uTicks);
#endif
        pVCpu->cpum.GstCtx.rax = RT_LO_U32(uTicks);
        pVCpu->cpum.GstCtx.rdx = RT_HI_U32(uTicks);
        pVCpu->cpum.GstCtx.fExtrn &= ~(CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RDX | CPUMCTX_EXTRN_RCX); /* For IEMExecDecodedRdtscp. */
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }
    return rcStrict;
}


/**
 * Implements RDPMC.
 */
IEM_CIMPL_DEF_0(iemCImpl_rdpmc)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR4);

    if (   IEM_GET_CPL(pVCpu) != 0
        && !(pVCpu->cpum.GstCtx.cr4 & X86_CR4_PCE))
        return iemRaiseGeneralProtectionFault0(pVCpu);

    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
    else if (   IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
             && IEM_VMX_IS_PROCCTLS_SET(pVCpu, VMX_PROC_CTLS_RDPMC_EXIT))
    {
        Log(("rdpmc: Guest intercept -> VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_RET(pVCpu, VMX_EXIT_RDPMC, cbInstr);
    }
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_RDPMC))
    {
        Log(("rdpmc: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_RDPMC, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /** @todo Emulate performance counters, for now just return 0. */
    pVCpu->cpum.GstCtx.rax = 0;
    pVCpu->cpum.GstCtx.rdx = 0;
    pVCpu->cpum.GstCtx.fExtrn &= ~(CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RDX);
    /** @todo We should trigger a \#GP here if the CPU doesn't support the index in
     *        ecx but see @bugref{3472}! */

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements RDMSR.
 */
IEM_CIMPL_DEF_0(iemCImpl_rdmsr)
{
    /*
     * Check preconditions.
     */
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fMsr)
        return iemRaiseUndefinedOpcode(pVCpu);
    if (IEM_GET_CPL(pVCpu) != 0)
        return iemRaiseGeneralProtectionFault0(pVCpu);

    /*
     * Check nested-guest intercepts.
     */
    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    else if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        if (iemVmxIsRdmsrWrmsrInterceptSet(pVCpu, VMX_EXIT_RDMSR, pVCpu->cpum.GstCtx.ecx))
            IEM_VMX_VMEXIT_INSTR_RET(pVCpu, VMX_EXIT_RDMSR, cbInstr);
    }
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_MSR_PROT))
    {
        VBOXSTRICTRC rcStrict = iemSvmHandleMsrIntercept(pVCpu, pVCpu->cpum.GstCtx.ecx, false /* fWrite */, cbInstr);
        if (rcStrict == VINF_SVM_VMEXIT)
            return VINF_SUCCESS;
        if (rcStrict != VINF_SVM_INTERCEPT_NOT_ACTIVE)
        {
            Log(("IEM: SVM intercepted rdmsr(%#x) failed. rc=%Rrc\n", pVCpu->cpum.GstCtx.ecx, VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
    }
#endif

    /*
     * Do the job.
     */
    RTUINT64U uValue;
    /** @todo make CPUMAllMsrs.cpp import the necessary MSR state. */
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_ALL_MSRS);

    VBOXSTRICTRC rcStrict = CPUMQueryGuestMsr(pVCpu, pVCpu->cpum.GstCtx.ecx, &uValue.u);
    if (rcStrict == VINF_SUCCESS)
    {
        pVCpu->cpum.GstCtx.rax = uValue.s.Lo;
        pVCpu->cpum.GstCtx.rdx = uValue.s.Hi;
        pVCpu->cpum.GstCtx.fExtrn &= ~(CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RDX);

        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

#ifndef IN_RING3
    /* Deferred to ring-3. */
    if (rcStrict == VINF_CPUM_R3_MSR_READ)
    {
        Log(("IEM: rdmsr(%#x) -> ring-3\n", pVCpu->cpum.GstCtx.ecx));
        return rcStrict;
    }
#endif

    /* Often a unimplemented MSR or MSR bit, so worth logging. */
    if (pVCpu->iem.s.cLogRelRdMsr < 32)
    {
        pVCpu->iem.s.cLogRelRdMsr++;
        LogRel(("IEM: rdmsr(%#x) -> #GP(0)\n", pVCpu->cpum.GstCtx.ecx));
    }
    else
        Log((   "IEM: rdmsr(%#x) -> #GP(0)\n", pVCpu->cpum.GstCtx.ecx));
    AssertMsgReturn(rcStrict == VERR_CPUM_RAISE_GP_0, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)), VERR_IPE_UNEXPECTED_STATUS);
    return iemRaiseGeneralProtectionFault0(pVCpu);
}


/**
 * Implements WRMSR.
 */
IEM_CIMPL_DEF_0(iemCImpl_wrmsr)
{
    /*
     * Check preconditions.
     */
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fMsr)
        return iemRaiseUndefinedOpcode(pVCpu);
    if (IEM_GET_CPL(pVCpu) != 0)
        return iemRaiseGeneralProtectionFault0(pVCpu);

    RTUINT64U uValue;
    uValue.s.Lo = pVCpu->cpum.GstCtx.eax;
    uValue.s.Hi = pVCpu->cpum.GstCtx.edx;

    uint32_t const idMsr = pVCpu->cpum.GstCtx.ecx;

    /** @todo make CPUMAllMsrs.cpp import the necessary MSR state. */
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_ALL_MSRS);

    /*
     * Check nested-guest intercepts.
     */
    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    else if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        if (iemVmxIsRdmsrWrmsrInterceptSet(pVCpu, VMX_EXIT_WRMSR, idMsr))
            IEM_VMX_VMEXIT_INSTR_RET(pVCpu, VMX_EXIT_WRMSR, cbInstr);
    }
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_MSR_PROT))
    {
        VBOXSTRICTRC rcStrict = iemSvmHandleMsrIntercept(pVCpu, idMsr, true /* fWrite */, cbInstr);
        if (rcStrict == VINF_SVM_VMEXIT)
            return VINF_SUCCESS;
        if (rcStrict != VINF_SVM_INTERCEPT_NOT_ACTIVE)
        {
            Log(("IEM: SVM intercepted rdmsr(%#x) failed. rc=%Rrc\n", idMsr, VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
    }
#endif

    if (idMsr == MSR_K6_EFER)
        IEMTLBTRACE_LOAD_EFER(pVCpu, uValue.u, pVCpu->cpum.GstCtx.msrEFER);

    /*
     * Do the job.
     */
    VBOXSTRICTRC rcStrict = CPUMSetGuestMsr(pVCpu, idMsr, uValue.u);
    if (rcStrict == VINF_SUCCESS)
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);

#ifndef IN_RING3
    /* Deferred to ring-3. */
    if (rcStrict == VINF_CPUM_R3_MSR_WRITE)
    {
        Log(("IEM: wrmsr(%#x) -> ring-3\n", idMsr));
        return rcStrict;
    }
#endif

    /* Often a unimplemented MSR or MSR bit, so worth logging. */
    if (pVCpu->iem.s.cLogRelWrMsr < 32)
    {
        pVCpu->iem.s.cLogRelWrMsr++;
        LogRel(("IEM: wrmsr(%#x,%#x`%08x) -> #GP(0)\n", idMsr, uValue.s.Hi, uValue.s.Lo));
    }
    else
        Log((   "IEM: wrmsr(%#x,%#x`%08x) -> #GP(0)\n", idMsr, uValue.s.Hi, uValue.s.Lo));
    AssertMsgReturn(rcStrict == VERR_CPUM_RAISE_GP_0, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)), VERR_IPE_UNEXPECTED_STATUS);
    return iemRaiseGeneralProtectionFault0(pVCpu);
}


/**
 * Implements 'IN eAX, port'.
 *
 * @param   u16Port             The source port.
 * @param   cbReg               The register size.
 * @param   bImmAndEffAddrMode  Bit 7: Whether the port was specified through an
 *                              immediate operand or the implicit DX register.
 *                              Bits 3-0: Effective address mode.
 */
IEM_CIMPL_DEF_3(iemCImpl_in, uint16_t, u16Port, uint8_t, cbReg, uint8_t, bImmAndEffAddrMode)
{
    /*
     * GCM intercept.
     *
     * This must be placed before the IOPL check as the mesa driver intercept
     * would otherwise trigger a #GP(0).
     */
    if (!IEM_IS_IN_GUEST(pVCpu) && GCMIsInterceptingIOPortRead(pVCpu, u16Port, cbReg))
    {
        VBOXSTRICTRC rcStrict = GCMInterceptedIOPortRead(pVCpu, &pVCpu->cpum.GstCtx, u16Port, cbReg);
        if (rcStrict == VINF_GCM_HANDLED_ADVANCE_RIP || rcStrict == VINF_GCM_HANDLED)
        {
            Log(("iemCImpl_in: u16Port=%#x cbReg=%d was handled by GCMIOPortRead (%d)\n", u16Port, cbReg, VBOXSTRICTRC_VAL(rcStrict)));
            if (rcStrict == VINF_GCM_HANDLED_ADVANCE_RIP)
                rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
            else
                rcStrict = VINF_SUCCESS;
            return rcStrict;
        }
        Assert(rcStrict == VERR_GCM_NOT_HANDLED);
    }

    /*
     * CPL check
     */
    VBOXSTRICTRC rcStrict = iemHlpCheckPortIOPermission(pVCpu, u16Port, cbReg);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }

    /*
     * Check VMX nested-guest IO intercept.
     */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    else if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        rcStrict = iemVmxVmexitInstrIo(pVCpu, VMXINSTRID_IO_IN, u16Port, RT_BOOL(bImmAndEffAddrMode & 0x80), cbReg, cbInstr);
        if (rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE)
            return rcStrict;
    }
#endif

    /*
     * Check SVM nested-guest IO intercept.
     */
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_IOIO_PROT))
    {
        uint8_t cAddrSizeBits;
        switch (bImmAndEffAddrMode & 0xf)
        {
            case IEMMODE_16BIT: cAddrSizeBits = 16; break;
            case IEMMODE_32BIT: cAddrSizeBits = 32; break;
            case IEMMODE_64BIT: cAddrSizeBits = 64; break;
            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
        rcStrict = iemSvmHandleIOIntercept(pVCpu, u16Port, SVMIOIOTYPE_IN, cbReg, cAddrSizeBits, 0 /* N/A - iEffSeg */,
                                           false /* fRep */, false /* fStrIo */, cbInstr);
        if (rcStrict == VINF_SVM_VMEXIT)
            return VINF_SUCCESS;
        if (rcStrict != VINF_SVM_INTERCEPT_NOT_ACTIVE)
        {
            Log(("iemCImpl_in: iemSvmHandleIOIntercept failed (u16Port=%#x, cbReg=%u) rc=%Rrc\n", u16Port, cbReg,
                 VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
    }
#endif
#if !defined(VBOX_WITH_NESTED_HWVIRT_VMX) && !defined(VBOX_WITH_NESTED_HWVIRT_SVM)
    RT_NOREF(bImmAndEffAddrMode);
#endif

    /*
     * Perform the I/O.
     */
    PVMCC const pVM      = pVCpu->CTX_SUFF(pVM);
    uint32_t    u32Value = 0;
    rcStrict = IOMIOPortRead(pVM, pVCpu, u16Port, &u32Value, cbReg);
    if (IOM_SUCCESS(rcStrict))
    {
        switch (cbReg)
        {
            case 1: pVCpu->cpum.GstCtx.al  = (uint8_t)u32Value;  break;
            case 2: pVCpu->cpum.GstCtx.ax  = (uint16_t)u32Value; break;
            case 4: pVCpu->cpum.GstCtx.rax = u32Value;           break;
            default: AssertFailedReturn(VERR_IEM_IPE_3);
        }

        pVCpu->iem.s.cPotentialExits++;
        if (rcStrict != VINF_SUCCESS)
            iemSetPassUpStatus(pVCpu, rcStrict);

        /*
         * Check for I/O breakpoints before we complete the instruction.
         */
        uint32_t const fDr7 = pVCpu->cpum.GstCtx.dr[7];
        if (RT_UNLIKELY(   (   (   (fDr7 & X86_DR7_ENABLED_MASK)
                                && X86_DR7_ANY_RW_IO(fDr7)
                                && (pVCpu->cpum.GstCtx.cr4 & X86_CR4_DE))
                            || pVM->dbgf.ro.cEnabledHwIoBreakpoints > 0)
                        && rcStrict == VINF_SUCCESS))
        {
            IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR0_DR3 | CPUMCTX_EXTRN_DR6);
            pVCpu->cpum.GstCtx.eflags.uBoth |= DBGFBpCheckIo2(pVM, pVCpu, u16Port, cbReg);
        }

        rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    return rcStrict;
}


/**
 * Implements 'IN eAX, DX'.
 *
 * @param   cbReg       The register size.
 * @param   enmEffAddrMode  Effective address mode.
 */
IEM_CIMPL_DEF_2(iemCImpl_in_eAX_DX, uint8_t, cbReg, IEMMODE, enmEffAddrMode)
{
    return IEM_CIMPL_CALL_3(iemCImpl_in, pVCpu->cpum.GstCtx.dx, cbReg, 0 /* fImm */ | enmEffAddrMode);
}


/**
 * Implements 'OUT port, eAX'.
 *
 * @param   u16Port         The destination port.
 * @param   cbReg           The register size.
 * @param   bImmAndEffAddrMode  Bit 7: Whether the port was specified through an
 *                              immediate operand or the implicit DX register.
 *                              Bits 3-0: Effective address mode.
 */
IEM_CIMPL_DEF_3(iemCImpl_out, uint16_t, u16Port, uint8_t, cbReg, uint8_t, bImmAndEffAddrMode)
{
    /*
     * CPL check
     */
    VBOXSTRICTRC rcStrict = iemHlpCheckPortIOPermission(pVCpu, u16Port, cbReg);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }

    /*
     * Check VMX nested-guest I/O intercept.
     */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    else if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        rcStrict = iemVmxVmexitInstrIo(pVCpu, VMXINSTRID_IO_OUT, u16Port, RT_BOOL(bImmAndEffAddrMode & 0x80), cbReg, cbInstr);
        if (rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE)
            return rcStrict;
    }
#endif

    /*
     * Check SVM nested-guest I/O intercept.
     */
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_IOIO_PROT))
    {
        uint8_t cAddrSizeBits;
        switch (bImmAndEffAddrMode & 0xf)
        {
            case IEMMODE_16BIT: cAddrSizeBits = 16; break;
            case IEMMODE_32BIT: cAddrSizeBits = 32; break;
            case IEMMODE_64BIT: cAddrSizeBits = 64; break;
            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
        rcStrict = iemSvmHandleIOIntercept(pVCpu, u16Port, SVMIOIOTYPE_OUT, cbReg, cAddrSizeBits, 0 /* N/A - iEffSeg */,
                                           false /* fRep */, false /* fStrIo */, cbInstr);
        if (rcStrict == VINF_SVM_VMEXIT)
            return VINF_SUCCESS;
        if (rcStrict != VINF_SVM_INTERCEPT_NOT_ACTIVE)
        {
            Log(("iemCImpl_out: iemSvmHandleIOIntercept failed (u16Port=%#x, cbReg=%u) rc=%Rrc\n", u16Port, cbReg,
                 VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }
    }
#endif
#if !defined(VBOX_WITH_NESTED_HWVIRT_VMX) && !defined(VBOX_WITH_NESTED_HWVIRT_SVM)
    RT_NOREF(bImmAndEffAddrMode);
#endif

    /*
     * Perform the I/O.
     */
    PVMCC const pVM      = pVCpu->CTX_SUFF(pVM);
    uint32_t u32Value;
    switch (cbReg)
    {
        case 1: u32Value = pVCpu->cpum.GstCtx.al;  break;
        case 2: u32Value = pVCpu->cpum.GstCtx.ax;  break;
        case 4: u32Value = pVCpu->cpum.GstCtx.eax; break;
        default: AssertFailedReturn(VERR_IEM_IPE_4);
    }
    rcStrict = IOMIOPortWrite(pVM, pVCpu, u16Port, u32Value, cbReg);
    if (IOM_SUCCESS(rcStrict))
    {
        pVCpu->iem.s.cPotentialExits++;
        if (rcStrict != VINF_SUCCESS)
            iemSetPassUpStatus(pVCpu, rcStrict);

        /*
         * Check for I/O breakpoints before we complete the instruction.
         */
        uint32_t const fDr7 = pVCpu->cpum.GstCtx.dr[7];
        if (RT_UNLIKELY(   (   (   (fDr7 & X86_DR7_ENABLED_MASK)
                                && X86_DR7_ANY_RW_IO(fDr7)
                                && (pVCpu->cpum.GstCtx.cr4 & X86_CR4_DE))
                            || pVM->dbgf.ro.cEnabledHwIoBreakpoints > 0)
                        && rcStrict == VINF_SUCCESS))
        {
            IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR0_DR3 | CPUMCTX_EXTRN_DR6);
            pVCpu->cpum.GstCtx.eflags.uBoth |= DBGFBpCheckIo2(pVM, pVCpu, u16Port, cbReg);
        }

        rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }
    return rcStrict;
}


/**
 * Implements 'OUT DX, eAX'.
 *
 * @param   cbReg       The register size.
 * @param   enmEffAddrMode  Effective address mode.
 */
IEM_CIMPL_DEF_2(iemCImpl_out_DX_eAX, uint8_t, cbReg, IEMMODE, enmEffAddrMode)
{
    return IEM_CIMPL_CALL_3(iemCImpl_out, pVCpu->cpum.GstCtx.dx, cbReg, 0 /* fImm */ | enmEffAddrMode);
}


/**
 * Implements 'CLI'.
 */
IEM_CIMPL_DEF_0(iemCImpl_cli)
{
    uint32_t        fEfl    = IEMMISC_GET_EFL(pVCpu);
#ifdef LOG_ENABLED
    uint32_t const  fEflOld = fEfl;
#endif

    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR4);
    if (pVCpu->cpum.GstCtx.cr0 & X86_CR0_PE)
    {
        uint8_t const uIopl = X86_EFL_GET_IOPL(fEfl);
        if (!(fEfl & X86_EFL_VM))
        {
            if (IEM_GET_CPL(pVCpu) <= uIopl)
                fEfl &= ~X86_EFL_IF;
            else if (   IEM_GET_CPL(pVCpu) == 3
                     && (pVCpu->cpum.GstCtx.cr4 & X86_CR4_PVI) )
                fEfl &= ~X86_EFL_VIF;
            else
                return iemRaiseGeneralProtectionFault0(pVCpu);
        }
        /* V8086 */
        else if (uIopl == 3)
            fEfl &= ~X86_EFL_IF;
        else if (   uIopl < 3
                 && (pVCpu->cpum.GstCtx.cr4 & X86_CR4_VME) )
            fEfl &= ~X86_EFL_VIF;
        else
            return iemRaiseGeneralProtectionFault0(pVCpu);
    }
    /* real mode */
    else
        fEfl &= ~X86_EFL_IF;

    /* Commit. */
    IEMMISC_SET_EFL(pVCpu, fEfl);
    VBOXSTRICTRC const rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    Log2(("CLI: %#x -> %#x\n", fEflOld, fEfl));
    return rcStrict;
}


/**
 * Implements 'STI'.
 */
IEM_CIMPL_DEF_0(iemCImpl_sti)
{
    uint32_t        fEfl    = IEMMISC_GET_EFL(pVCpu);
    uint32_t const  fEflOld = fEfl;

    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR4);
    if (pVCpu->cpum.GstCtx.cr0 & X86_CR0_PE)
    {
        uint8_t const uIopl = X86_EFL_GET_IOPL(fEfl);
        if (!(fEfl & X86_EFL_VM))
        {
            if (IEM_GET_CPL(pVCpu) <= uIopl)
                fEfl |= X86_EFL_IF;
            else if (   IEM_GET_CPL(pVCpu) == 3
                     && (pVCpu->cpum.GstCtx.cr4 & X86_CR4_PVI)
                     && !(fEfl & X86_EFL_VIP) )
                fEfl |= X86_EFL_VIF;
            else
                return iemRaiseGeneralProtectionFault0(pVCpu);
        }
        /* V8086 */
        else if (uIopl == 3)
            fEfl |= X86_EFL_IF;
        else if (   uIopl < 3
                 && (pVCpu->cpum.GstCtx.cr4 & X86_CR4_VME)
                 && !(fEfl & X86_EFL_VIP) )
            fEfl |= X86_EFL_VIF;
        else
            return iemRaiseGeneralProtectionFault0(pVCpu);
    }
    /* real mode */
    else
        fEfl |= X86_EFL_IF;

    /*
     * Commit.
     *
     * Note! Setting the shadow interrupt flag must be done after RIP updating.
     */
    IEMMISC_SET_EFL(pVCpu, fEfl);
    VBOXSTRICTRC const rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    if (!(fEflOld & X86_EFL_IF) && (fEfl & X86_EFL_IF))
    {
        /** @todo only set it the shadow flag if it was clear before? */
        CPUMSetInInterruptShadowSti(&pVCpu->cpum.GstCtx);
    }
    pVCpu->iem.s.fTbCurInstrIsSti = true;
    Log2(("STI: %#x -> %#x\n", fEflOld, fEfl));
    return rcStrict;
}


/**
 * Implements 'HLT'.
 */
IEM_CIMPL_DEF_0(iemCImpl_hlt)
{
    if (IEM_GET_CPL(pVCpu) != 0)
        return iemRaiseGeneralProtectionFault0(pVCpu);

    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
    else if (   IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
        && IEM_VMX_IS_PROCCTLS_SET(pVCpu, VMX_PROC_CTLS_HLT_EXIT))
    {
        Log2(("hlt: Guest intercept -> VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_RET(pVCpu, VMX_EXIT_HLT, cbInstr);
    }
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_HLT))
    {
        Log2(("hlt: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_HLT, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /** @todo finish: This ASSUMES that iemRegAddToRipAndFinishingClearingRF won't
     * be returning any status codes relating to non-guest events being raised, as
     * we'll mess up the guest HALT otherwise.  */
    VBOXSTRICTRC rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    if (rcStrict == VINF_SUCCESS)
        rcStrict = VINF_EM_HALT;
    return rcStrict;
}


/**
 * Implements 'MONITOR'.
 */
IEM_CIMPL_DEF_1(iemCImpl_monitor, uint8_t, iEffSeg)
{
    /*
     * Permission checks.
     */
    if (IEM_GET_CPL(pVCpu) != 0)
    {
        Log2(("monitor: CPL != 0\n"));
        return iemRaiseUndefinedOpcode(pVCpu); /** @todo MSR[0xC0010015].MonMwaitUserEn if we care. */
    }
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fMonitorMWait)
    {
        Log2(("monitor: Not in CPUID\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }

    /*
     * Check VMX guest-intercept.
     * This should be considered a fault-like VM-exit.
     * See Intel spec. 25.1.1 "Relative Priority of Faults and VM Exits".
     */
    if (   !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
        || !IEM_VMX_IS_PROCCTLS_SET(pVCpu, VMX_PROC_CTLS_MONITOR_EXIT))
    { /* probable */ }
    else
    {
        Log2(("monitor: Guest intercept -> #VMEXIT\n"));
        IEM_VMX_VMEXIT_INSTR_RET(pVCpu, VMX_EXIT_MONITOR, cbInstr);
    }

    /*
     * Gather the operands and validate them.
     */
    RTGCPTR  GCPtrMem   = IEM_IS_64BIT_CODE(pVCpu) ? pVCpu->cpum.GstCtx.rax : pVCpu->cpum.GstCtx.eax;
    uint32_t uEcx       = pVCpu->cpum.GstCtx.ecx;
    uint32_t uEdx       = pVCpu->cpum.GstCtx.edx;
/** @todo Test whether EAX or ECX is processed first, i.e. do we get \#PF or
 *        \#GP first. */
    if (uEcx != 0)
    {
        Log2(("monitor rax=%RX64, ecx=%RX32, edx=%RX32; ECX != 0 -> #GP(0)\n", GCPtrMem, uEcx, uEdx)); NOREF(uEdx);
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    VBOXSTRICTRC rcStrict = iemMemApplySegment(pVCpu, IEM_ACCESS_TYPE_READ | IEM_ACCESS_WHAT_DATA, iEffSeg, 1, &GCPtrMem);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    RTGCPHYS GCPhysMem;
    /** @todo access size   */
    rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, GCPtrMem, 1, IEM_ACCESS_TYPE_READ | IEM_ACCESS_WHAT_DATA, &GCPhysMem);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    else if (   IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
             && IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_VIRT_APIC_ACCESS))
    {
        /*
         * MONITOR does not access the memory, just monitors the address. However,
         * if the address falls in the APIC-access page, the address monitored must
         * instead be the corresponding address in the virtual-APIC page.
         *
         * See Intel spec. 29.4.4 "Instruction-Specific Considerations".
         */
        rcStrict = iemVmxVirtApicAccessUnused(pVCpu, &GCPhysMem, 1, IEM_ACCESS_TYPE_READ | IEM_ACCESS_WHAT_DATA);
        if (   rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE
            && rcStrict != VINF_VMX_MODIFIES_BEHAVIOR)
                return rcStrict;
    }
#endif
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_MONITOR))
    {
        Log2(("monitor: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_MONITOR, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /*
     * Call EM to prepare the monitor/wait.
     */
    rcStrict = EMMonitorWaitPrepare(pVCpu, pVCpu->cpum.GstCtx.rax, pVCpu->cpum.GstCtx.rcx, pVCpu->cpum.GstCtx.rdx, GCPhysMem);
    Assert(rcStrict == VINF_SUCCESS);
    if (rcStrict == VINF_SUCCESS)
        rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    return rcStrict;
}


/**
 * Implements 'MWAIT'.
 */
IEM_CIMPL_DEF_0(iemCImpl_mwait)
{
    /*
     * Permission checks.
     */
    if (IEM_GET_CPL(pVCpu) != 0)
    {
        Log2(("mwait: CPL != 0\n"));
        /** @todo MSR[0xC0010015].MonMwaitUserEn if we care. (Remember to check
         *        EFLAGS.VM then.) */
        return iemRaiseUndefinedOpcode(pVCpu);
    }
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fMonitorMWait)
    {
        Log2(("mwait: Not in CPUID\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }

    /* Check VMX nested-guest intercept. */
    if (   !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
        || !IEM_VMX_IS_PROCCTLS_SET(pVCpu, VMX_PROC_CTLS_MWAIT_EXIT))
    { /* probable */ }
    else
        IEM_VMX_VMEXIT_MWAIT_RET(pVCpu, EMMonitorIsArmed(pVCpu), cbInstr);

    /*
     * Gather the operands and validate them.
     */
    uint32_t const uEax = pVCpu->cpum.GstCtx.eax;
    uint32_t const uEcx = pVCpu->cpum.GstCtx.ecx;
    if (uEcx != 0)
    {
        /* Only supported extension is break on IRQ when IF=0. */
        if (uEcx > 1)
        {
            Log2(("mwait eax=%RX32, ecx=%RX32; ECX > 1 -> #GP(0)\n", uEax, uEcx));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }
        uint32_t fMWaitFeatures = 0;
        uint32_t uIgnore = 0;
        CPUMGetGuestCpuId(pVCpu, 5, 0, -1 /*f64BitMode*/, &uIgnore, &uIgnore, &fMWaitFeatures, &uIgnore);
        if (    (fMWaitFeatures & (X86_CPUID_MWAIT_ECX_EXT | X86_CPUID_MWAIT_ECX_BREAKIRQIF0))
            !=                    (X86_CPUID_MWAIT_ECX_EXT | X86_CPUID_MWAIT_ECX_BREAKIRQIF0))
        {
            Log2(("mwait eax=%RX32, ecx=%RX32; break-on-IRQ-IF=0 extension not enabled -> #GP(0)\n", uEax, uEcx));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
        /*
         * If the interrupt-window exiting control is set or a virtual-interrupt is pending
         * for delivery; and interrupts are disabled the processor does not enter its
         * mwait state but rather passes control to the next instruction.
         *
         * See Intel spec. 25.3 "Changes to Instruction Behavior In VMX Non-root Operation".
         */
        if (   !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
            || pVCpu->cpum.GstCtx.eflags.Bits.u1IF)
        { /* probable */ }
        else if (   IEM_VMX_IS_PROCCTLS_SET(pVCpu, VMX_PROC_CTLS_INT_WINDOW_EXIT)
                 || VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NESTED_GUEST))
            /** @todo finish: check up this out after we move int window stuff out of the
             *        run loop and into the instruction finishing logic here. */
            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
#endif
    }

    /*
     * Check SVM nested-guest mwait intercepts.
     */
    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
    else if (   IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_MWAIT_ARMED)
             && EMMonitorIsArmed(pVCpu))
    {
        Log2(("mwait: Guest intercept (monitor hardware armed) -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_MWAIT_ARMED, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_MWAIT))
    {
        Log2(("mwait: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_MWAIT, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /*
     * Call EM to prepare the monitor/wait.
     *
     * This will return VINF_EM_HALT. If there the trap flag is set, we may
     * override it when executing iemRegAddToRipAndFinishingClearingRF ASSUMING
     * that will only return guest related events.
     */
    VBOXSTRICTRC rcStrict = EMMonitorWaitPerform(pVCpu, uEax, uEcx);

    /** @todo finish: This needs more thinking as we should suppress internal
     * debugger events here, or we'll bugger up the guest state even more than we
     * alread do around VINF_EM_HALT. */
    VBOXSTRICTRC rcStrict2 = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    if (rcStrict2 != VINF_SUCCESS)
    {
        Log2(("mwait: %Rrc (perform) -> %Rrc (finish)!\n", VBOXSTRICTRC_VAL(rcStrict), VBOXSTRICTRC_VAL(rcStrict2) ));
        rcStrict = rcStrict2;
    }

    return rcStrict;
}


/**
 * Implements 'SWAPGS'.
 */
IEM_CIMPL_DEF_0(iemCImpl_swapgs)
{
    Assert(IEM_IS_64BIT_CODE(pVCpu)); /* Caller checks this. */

    /*
     * Permission checks.
     */
    if (IEM_GET_CPL(pVCpu) != 0)
    {
        Log2(("swapgs: CPL != 0\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }

    /*
     * Do the job.
     */
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_KERNEL_GS_BASE | CPUMCTX_EXTRN_GS);
    uint64_t uOtherGsBase = pVCpu->cpum.GstCtx.msrKERNELGSBASE;
    pVCpu->cpum.GstCtx.msrKERNELGSBASE = pVCpu->cpum.GstCtx.gs.u64Base;
    pVCpu->cpum.GstCtx.gs.u64Base = uOtherGsBase;

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


#ifndef VBOX_WITHOUT_CPUID_HOST_CALL
/**
 * Handles a CPUID call.
 */
static VBOXSTRICTRC iemCpuIdVBoxCall(PVMCPUCC pVCpu, uint32_t iFunction,
                                     uint32_t *pEax, uint32_t *pEbx, uint32_t *pEcx, uint32_t *pEdx)
{
    switch (iFunction)
    {
        case VBOX_CPUID_FN_ID:
            LogFlow(("iemCpuIdVBoxCall: VBOX_CPUID_FN_ID\n"));
            *pEax = VBOX_CPUID_RESP_ID_EAX;
            *pEbx = VBOX_CPUID_RESP_ID_EBX;
            *pEcx = VBOX_CPUID_RESP_ID_ECX;
            *pEdx = VBOX_CPUID_RESP_ID_EDX;
            break;

        case VBOX_CPUID_FN_LOG:
        {
            CPUM_IMPORT_EXTRN_RET(pVCpu, CPUMCTX_EXTRN_RDX | CPUMCTX_EXTRN_RBX | CPUMCTX_EXTRN_RSI
                                       | IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK);

            /* Validate input. */
            uint32_t cchToLog = *pEdx;
            if (cchToLog <= _2M)
            {
                uint32_t const uLogPicker = *pEbx;
                if (uLogPicker <= 1)
                {
                    /* Resolve the logger. */
                    PRTLOGGER const pLogger = !uLogPicker
                                            ? RTLogDefaultInstanceEx(UINT32_MAX) : RTLogRelGetDefaultInstanceEx(UINT32_MAX);
                    if (pLogger)
                    {
                        /* Copy over the data: */
                        RTGCPTR GCPtrSrc = pVCpu->cpum.GstCtx.rsi;
                        while (cchToLog > 0)
                        {
                            uint32_t cbToMap = GUEST_PAGE_SIZE - (GCPtrSrc & GUEST_PAGE_OFFSET_MASK);
                            if (cbToMap > cchToLog)
                                cbToMap = cchToLog;
                            /** @todo Extend iemMemMap to allowing page size accessing and avoid 7
                             *        unnecessary calls & iterations per pages. */
                            if (cbToMap > 512)
                                cbToMap = 512;
                            uint8_t      bUnmapInfo;
                            void        *pvSrc    = NULL;
                            VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvSrc, &bUnmapInfo, cbToMap,
                                                              UINT8_MAX, GCPtrSrc, IEM_ACCESS_DATA_R, 0);
                            if (rcStrict == VINF_SUCCESS)
                            {
                                RTLogBulkNestedWrite(pLogger, (const char *)pvSrc, cbToMap, "Gst:");
                                rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
                                AssertRCSuccessReturn(VBOXSTRICTRC_VAL(rcStrict), rcStrict);
                            }
                            else
                            {
                                Log(("iemCpuIdVBoxCall: %Rrc at %RGp LB %#x\n", VBOXSTRICTRC_VAL(rcStrict), GCPtrSrc, cbToMap));
                                return rcStrict;
                            }

                            /* Advance. */
                            pVCpu->cpum.GstCtx.rsi = GCPtrSrc += cbToMap;
                            *pEdx                  = cchToLog -= cbToMap;
                        }
                        *pEax = VINF_SUCCESS;
                    }
                    else
                        *pEax = (uint32_t)VERR_NOT_FOUND;
                }
                else
                    *pEax = (uint32_t)VERR_NOT_FOUND;
            }
            else
                *pEax = (uint32_t)VERR_TOO_MUCH_DATA;
            *pEdx = VBOX_CPUID_RESP_GEN_EDX;
            *pEcx = VBOX_CPUID_RESP_GEN_ECX;
            *pEbx = VBOX_CPUID_RESP_GEN_EBX;
            break;
        }

        default:
            LogFlow(("iemCpuIdVBoxCall: Invalid function %#x (%#x, %#x)\n", iFunction, *pEbx, *pEdx));
            *pEax = (uint32_t)VERR_INVALID_FUNCTION;
            *pEbx = (uint32_t)VERR_INVALID_FUNCTION;
            *pEcx = (uint32_t)VERR_INVALID_FUNCTION;
            *pEdx = (uint32_t)VERR_INVALID_FUNCTION;
            break;
    }
    return VINF_SUCCESS;
}
#endif /* VBOX_WITHOUT_CPUID_HOST_CALL */

/**
 * Implements 'CPUID'.
 */
IEM_CIMPL_DEF_0(iemCImpl_cpuid)
{
    if (!IEM_IS_IN_GUEST(pVCpu))
    { /* probable */ }
    else if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        Log2(("cpuid: Guest intercept -> VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_RET(pVCpu, VMX_EXIT_CPUID, cbInstr);
    }
    else if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_CPUID))
    {
        Log2(("cpuid: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_CPUID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }


    uint32_t const uEax = pVCpu->cpum.GstCtx.eax;
    uint32_t const uEcx = pVCpu->cpum.GstCtx.ecx;

#ifndef VBOX_WITHOUT_CPUID_HOST_CALL
    /*
     * CPUID host call backdoor.
     */
    if (   uEax == VBOX_CPUID_REQ_EAX_FIXED
        && (uEcx & VBOX_CPUID_REQ_ECX_FIXED_MASK) == VBOX_CPUID_REQ_ECX_FIXED
        && pVCpu->CTX_SUFF(pVM)->iem.s.fCpuIdHostCall)
    {
        VBOXSTRICTRC rcStrict = iemCpuIdVBoxCall(pVCpu, uEcx & VBOX_CPUID_REQ_ECX_FN_MASK,
                                                 &pVCpu->cpum.GstCtx.eax, &pVCpu->cpum.GstCtx.ebx,
                                                 &pVCpu->cpum.GstCtx.ecx, &pVCpu->cpum.GstCtx.edx);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }
    /*
     * Regular CPUID.
     */
    else
#endif
        CPUMGetGuestCpuId(pVCpu, uEax, uEcx, pVCpu->cpum.GstCtx.cs.Attr.n.u1Long,
                          &pVCpu->cpum.GstCtx.eax, &pVCpu->cpum.GstCtx.ebx, &pVCpu->cpum.GstCtx.ecx, &pVCpu->cpum.GstCtx.edx);

    pVCpu->cpum.GstCtx.rax &= UINT32_C(0xffffffff);
    pVCpu->cpum.GstCtx.rbx &= UINT32_C(0xffffffff);
    pVCpu->cpum.GstCtx.rcx &= UINT32_C(0xffffffff);
    pVCpu->cpum.GstCtx.rdx &= UINT32_C(0xffffffff);
    pVCpu->cpum.GstCtx.fExtrn &= ~(CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RCX | CPUMCTX_EXTRN_RDX | CPUMCTX_EXTRN_RBX);

    pVCpu->iem.s.cPotentialExits++;
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'AAD'.
 *
 * @param   bImm            The immediate operand.
 */
IEM_CIMPL_DEF_1(iemCImpl_aad, uint8_t, bImm)
{
    uint16_t const ax = pVCpu->cpum.GstCtx.ax;
    uint8_t const  al = (uint8_t)ax + (uint8_t)(ax >> 8) * bImm;
    pVCpu->cpum.GstCtx.ax = al;
    iemHlpUpdateArithEFlagsU8(pVCpu, al,
                              X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF,
                              X86_EFL_OF | X86_EFL_AF | X86_EFL_CF);

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'AAM'.
 *
 * @param   bImm            The immediate operand. Cannot be 0.
 */
IEM_CIMPL_DEF_1(iemCImpl_aam, uint8_t, bImm)
{
    Assert(bImm != 0); /* #DE on 0 is handled in the decoder. */

    uint16_t const ax = pVCpu->cpum.GstCtx.ax;
    uint8_t const  al = (uint8_t)ax % bImm;
    uint8_t const  ah = (uint8_t)ax / bImm;
    pVCpu->cpum.GstCtx.ax = (ah << 8) + al;
    iemHlpUpdateArithEFlagsU8(pVCpu, al,
                              X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF,
                              X86_EFL_OF | X86_EFL_AF | X86_EFL_CF);

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'DAA'.
 */
IEM_CIMPL_DEF_0(iemCImpl_daa)
{
    uint8_t const  al       = pVCpu->cpum.GstCtx.al;
    bool const     fCarry   = pVCpu->cpum.GstCtx.eflags.Bits.u1CF;

    if (   pVCpu->cpum.GstCtx.eflags.Bits.u1AF
        || (al & 0xf) >= 10)
    {
        pVCpu->cpum.GstCtx.al = al + 6;
        pVCpu->cpum.GstCtx.eflags.Bits.u1AF = 1;
    }
    else
        pVCpu->cpum.GstCtx.eflags.Bits.u1AF = 0;

    if (al >= 0x9a || fCarry)
    {
        pVCpu->cpum.GstCtx.al += 0x60;
        pVCpu->cpum.GstCtx.eflags.Bits.u1CF = 1;
    }
    else
        pVCpu->cpum.GstCtx.eflags.Bits.u1CF = 0;

    iemHlpUpdateArithEFlagsU8(pVCpu, pVCpu->cpum.GstCtx.al, X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF, X86_EFL_OF);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'DAS'.
 */
IEM_CIMPL_DEF_0(iemCImpl_das)
{
    uint8_t const  uInputAL = pVCpu->cpum.GstCtx.al;
    bool const     fCarry   = pVCpu->cpum.GstCtx.eflags.Bits.u1CF;

    if (   pVCpu->cpum.GstCtx.eflags.Bits.u1AF
        || (uInputAL & 0xf) >= 10)
    {
        pVCpu->cpum.GstCtx.eflags.Bits.u1AF = 1;
        if (uInputAL < 6)
            pVCpu->cpum.GstCtx.eflags.Bits.u1CF = 1;
        pVCpu->cpum.GstCtx.al = uInputAL - 6;
    }
    else
    {
        pVCpu->cpum.GstCtx.eflags.Bits.u1AF = 0;
        pVCpu->cpum.GstCtx.eflags.Bits.u1CF = 0;
    }

    if (uInputAL >= 0x9a || fCarry)
    {
        pVCpu->cpum.GstCtx.al -= 0x60;
        pVCpu->cpum.GstCtx.eflags.Bits.u1CF = 1;
    }

    iemHlpUpdateArithEFlagsU8(pVCpu, pVCpu->cpum.GstCtx.al, X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF, X86_EFL_OF);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'AAA'.
 */
IEM_CIMPL_DEF_0(iemCImpl_aaa)
{
    if (IEM_IS_GUEST_CPU_AMD(pVCpu))
    {
        if (   pVCpu->cpum.GstCtx.eflags.Bits.u1AF
            || (pVCpu->cpum.GstCtx.ax & 0xf) >= 10)
        {
            pVCpu->cpum.GstCtx.eflags.uBoth = iemAImpl_add_u16(pVCpu->cpum.GstCtx.eflags.uBoth, &pVCpu->cpum.GstCtx.ax, 0x106);
            pVCpu->cpum.GstCtx.eflags.Bits.u1AF = 1;
            pVCpu->cpum.GstCtx.eflags.Bits.u1CF = 1;
        }
        else
        {
            iemHlpUpdateArithEFlagsU16(pVCpu, pVCpu->cpum.GstCtx.ax, X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF, X86_EFL_OF);
            pVCpu->cpum.GstCtx.eflags.Bits.u1AF = 0;
            pVCpu->cpum.GstCtx.eflags.Bits.u1CF = 0;
        }
        pVCpu->cpum.GstCtx.ax &= UINT16_C(0xff0f);
    }
    else
    {
        if (   pVCpu->cpum.GstCtx.eflags.Bits.u1AF
            || (pVCpu->cpum.GstCtx.ax & 0xf) >= 10)
        {
            pVCpu->cpum.GstCtx.ax += UINT16_C(0x106);
            pVCpu->cpum.GstCtx.eflags.Bits.u1AF = 1;
            pVCpu->cpum.GstCtx.eflags.Bits.u1CF = 1;
        }
        else
        {
            pVCpu->cpum.GstCtx.eflags.Bits.u1AF = 0;
            pVCpu->cpum.GstCtx.eflags.Bits.u1CF = 0;
        }
        pVCpu->cpum.GstCtx.ax &= UINT16_C(0xff0f);
        iemHlpUpdateArithEFlagsU8(pVCpu, pVCpu->cpum.GstCtx.al, X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF, X86_EFL_OF);
    }

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'AAS'.
 */
IEM_CIMPL_DEF_0(iemCImpl_aas)
{
    if (IEM_IS_GUEST_CPU_AMD(pVCpu))
    {
        if (   pVCpu->cpum.GstCtx.eflags.Bits.u1AF
            || (pVCpu->cpum.GstCtx.ax & 0xf) >= 10)
        {
            pVCpu->cpum.GstCtx.eflags.uBoth = iemAImpl_sub_u16(pVCpu->cpum.GstCtx.eflags.uBoth, &pVCpu->cpum.GstCtx.ax, 0x106);
            pVCpu->cpum.GstCtx.eflags.Bits.u1AF = 1;
            pVCpu->cpum.GstCtx.eflags.Bits.u1CF = 1;
        }
        else
        {
            iemHlpUpdateArithEFlagsU16(pVCpu, pVCpu->cpum.GstCtx.ax, X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF, X86_EFL_OF);
            pVCpu->cpum.GstCtx.eflags.Bits.u1AF = 0;
            pVCpu->cpum.GstCtx.eflags.Bits.u1CF = 0;
        }
        pVCpu->cpum.GstCtx.ax &= UINT16_C(0xff0f);
    }
    else
    {
        if (   pVCpu->cpum.GstCtx.eflags.Bits.u1AF
            || (pVCpu->cpum.GstCtx.ax & 0xf) >= 10)
        {
            pVCpu->cpum.GstCtx.ax -= UINT16_C(0x106);
            pVCpu->cpum.GstCtx.eflags.Bits.u1AF = 1;
            pVCpu->cpum.GstCtx.eflags.Bits.u1CF = 1;
        }
        else
        {
            pVCpu->cpum.GstCtx.eflags.Bits.u1AF = 0;
            pVCpu->cpum.GstCtx.eflags.Bits.u1CF = 0;
        }
        pVCpu->cpum.GstCtx.ax &= UINT16_C(0xff0f);
        iemHlpUpdateArithEFlagsU8(pVCpu, pVCpu->cpum.GstCtx.al, X86_EFL_SF | X86_EFL_ZF | X86_EFL_PF, X86_EFL_OF);
    }

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements the 16-bit version of 'BOUND'.
 *
 * @note    We have separate 16-bit and 32-bit variants of this function due to
 *          the decoder using unsigned parameters, whereas we want signed one to
 *          do the job.  This is significant for a recompiler.
 */
IEM_CIMPL_DEF_3(iemCImpl_bound_16, int16_t, idxArray, int16_t, idxLowerBound, int16_t, idxUpperBound)
{
    /*
     * Check if the index is inside the bounds, otherwise raise #BR.
     */
    if (   idxArray >= idxLowerBound
        && idxArray <= idxUpperBound)
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    return iemRaiseBoundRangeExceeded(pVCpu);
}


/**
 * Implements the 32-bit version of 'BOUND'.
 */
IEM_CIMPL_DEF_3(iemCImpl_bound_32, int32_t, idxArray, int32_t, idxLowerBound, int32_t, idxUpperBound)
{
    /*
     * Check if the index is inside the bounds, otherwise raise #BR.
     */
    if (   idxArray >= idxLowerBound
        && idxArray <= idxUpperBound)
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    return iemRaiseBoundRangeExceeded(pVCpu);
}



/*
 * Instantiate the various string operation combinations.
 */
#define OP_SIZE     8
#define ADDR_SIZE   16
#include "IEMAllCImplStrInstr.cpp.h"
#define OP_SIZE     8
#define ADDR_SIZE   32
#include "IEMAllCImplStrInstr.cpp.h"
#define OP_SIZE     8
#define ADDR_SIZE   64
#include "IEMAllCImplStrInstr.cpp.h"

#define OP_SIZE     16
#define ADDR_SIZE   16
#include "IEMAllCImplStrInstr.cpp.h"
#define OP_SIZE     16
#define ADDR_SIZE   32
#include "IEMAllCImplStrInstr.cpp.h"
#define OP_SIZE     16
#define ADDR_SIZE   64
#include "IEMAllCImplStrInstr.cpp.h"

#define OP_SIZE     32
#define ADDR_SIZE   16
#include "IEMAllCImplStrInstr.cpp.h"
#define OP_SIZE     32
#define ADDR_SIZE   32
#include "IEMAllCImplStrInstr.cpp.h"
#define OP_SIZE     32
#define ADDR_SIZE   64
#include "IEMAllCImplStrInstr.cpp.h"

#define OP_SIZE     64
#define ADDR_SIZE   32
#include "IEMAllCImplStrInstr.cpp.h"
#define OP_SIZE     64
#define ADDR_SIZE   64
#include "IEMAllCImplStrInstr.cpp.h"


/**
 * Implements 'XGETBV'.
 */
IEM_CIMPL_DEF_0(iemCImpl_xgetbv)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR4);
    if (pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSXSAVE)
    {
        uint32_t uEcx = pVCpu->cpum.GstCtx.ecx;
        switch (uEcx)
        {
            case 0:
                break;

            case 1: /** @todo Implement XCR1 support. */
            default:
                Log(("xgetbv ecx=%RX32 -> #GP(0)\n", uEcx));
                return iemRaiseGeneralProtectionFault0(pVCpu);

        }
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_XCRx);
        pVCpu->cpum.GstCtx.rax = RT_LO_U32(pVCpu->cpum.GstCtx.aXcr[uEcx]);
        pVCpu->cpum.GstCtx.rdx = RT_HI_U32(pVCpu->cpum.GstCtx.aXcr[uEcx]);

        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }
    Log(("xgetbv CR4.OSXSAVE=0 -> UD\n"));
    return iemRaiseUndefinedOpcode(pVCpu);
}


/**
 * Implements 'XSETBV'.
 */
IEM_CIMPL_DEF_0(iemCImpl_xsetbv)
{
    if (pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSXSAVE)
    {
        /** @todo explain why this happens before the CPL check.  */
        if (!IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_XSETBV))
        { /* probable */ }
        else
        {
            Log2(("xsetbv: Guest intercept -> #VMEXIT\n"));
            IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
            IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_XSETBV, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }

        if (IEM_GET_CPL(pVCpu) == 0)
        {
            IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_XCRx);

            if (!IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
            { /* probable */ }
            else
                IEM_VMX_VMEXIT_INSTR_RET(pVCpu, VMX_EXIT_XSETBV, cbInstr);

            uint32_t uEcx = pVCpu->cpum.GstCtx.ecx;
            uint64_t uNewValue = RT_MAKE_U64(pVCpu->cpum.GstCtx.eax, pVCpu->cpum.GstCtx.edx);
            switch (uEcx)
            {
                case 0:
                {
                    int rc = CPUMSetGuestXcr0(pVCpu, uNewValue);
                    if (rc == VINF_SUCCESS)
                        break;
                    Assert(rc == VERR_CPUM_RAISE_GP_0);
                    Log(("xsetbv ecx=%RX32 (newvalue=%RX64) -> #GP(0)\n", uEcx, uNewValue));
                    return iemRaiseGeneralProtectionFault0(pVCpu);
                }

                case 1: /** @todo Implement XCR1 support. */
                default:
                    Log(("xsetbv ecx=%RX32 (newvalue=%RX64) -> #GP(0)\n", uEcx, uNewValue));
                    return iemRaiseGeneralProtectionFault0(pVCpu);

            }

            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }

        Log(("xsetbv cpl=%u -> GP(0)\n", IEM_GET_CPL(pVCpu)));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }
    Log(("xsetbv CR4.OSXSAVE=0 -> UD\n"));
    return iemRaiseUndefinedOpcode(pVCpu);
}

#ifndef RT_ARCH_ARM64
# ifdef IN_RING3

/** Argument package for iemCImpl_cmpxchg16b_fallback_rendezvous_callback. */
struct IEMCIMPLCX16ARGS
{
    PRTUINT128U     pu128Dst;
    PRTUINT128U     pu128RaxRdx;
    PRTUINT128U     pu128RbxRcx;
    uint32_t       *pEFlags;
#  ifdef VBOX_STRICT
    uint32_t        cCalls;
#  endif
};

/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS,
 *                       Worker for iemCImpl_cmpxchg16b_fallback_rendezvous}
 */
static DECLCALLBACK(VBOXSTRICTRC) iemCImpl_cmpxchg16b_fallback_rendezvous_callback(PVM pVM, PVMCPUCC pVCpu, void *pvUser)
{
    RT_NOREF(pVM, pVCpu);
    struct IEMCIMPLCX16ARGS *pArgs = (struct IEMCIMPLCX16ARGS *)pvUser;
#  ifdef VBOX_STRICT
    Assert(pArgs->cCalls == 0);
    pArgs->cCalls++;
#  endif

    iemAImpl_cmpxchg16b_fallback(pArgs->pu128Dst, pArgs->pu128RaxRdx, pArgs->pu128RbxRcx, pArgs->pEFlags);
    return VINF_SUCCESS;
}

# endif /* IN_RING3 */

/**
 * Implements 'CMPXCHG16B' fallback using rendezvous.
 */
IEM_CIMPL_DEF_5(iemCImpl_cmpxchg16b_fallback_rendezvous, PRTUINT128U, pu128Dst, PRTUINT128U, pu128RaxRdx,
                PRTUINT128U, pu128RbxRcx, uint32_t *, pEFlags, uint8_t, bUnmapInfo)
{
# ifdef IN_RING3
    struct IEMCIMPLCX16ARGS Args;
    Args.pu128Dst       = pu128Dst;
    Args.pu128RaxRdx    = pu128RaxRdx;
    Args.pu128RbxRcx    = pu128RbxRcx;
    Args.pEFlags        = pEFlags;
#  ifdef VBOX_STRICT
    Args.cCalls         = 0;
#  endif
    VBOXSTRICTRC rcStrict = VMMR3EmtRendezvous(pVCpu->CTX_SUFF(pVM), VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE,
                                               iemCImpl_cmpxchg16b_fallback_rendezvous_callback, &Args);
    Assert(Args.cCalls == 1);
    if (rcStrict == VINF_SUCCESS)
    {
        /* Duplicated tail code. */
        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        if (rcStrict == VINF_SUCCESS)
        {
            pVCpu->cpum.GstCtx.eflags.u = *pEFlags; /* IEM_MC_COMMIT_EFLAGS */
            if (!(*pEFlags & X86_EFL_ZF))
            {
                pVCpu->cpum.GstCtx.rax = pu128RaxRdx->s.Lo;
                pVCpu->cpum.GstCtx.rdx = pu128RaxRdx->s.Hi;
            }
            rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }
    }
    return rcStrict;
# else
    RT_NOREF(pVCpu, cbInstr, pu128Dst, pu128RaxRdx, pu128RbxRcx, pEFlags, bUnmapInfo);
    return VERR_IEM_ASPECT_NOT_IMPLEMENTED; /* This should get us to ring-3 for now.  Should perhaps be replaced later. */
# endif
}

#endif /* RT_ARCH_ARM64 */

/**
 * Implements 'CLFLUSH' and 'CLFLUSHOPT'.
 *
 * This is implemented in C because it triggers a load like behaviour without
 * actually reading anything.  Since that's not so common, it's implemented
 * here.
 *
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEff        The address of the image.
 */
IEM_CIMPL_DEF_2(iemCImpl_clflush_clflushopt, uint8_t, iEffSeg, RTGCPTR, GCPtrEff)
{
    /*
     * Pretend to do a load w/o reading (see also iemCImpl_monitor and iemMemMap).
     */
    VBOXSTRICTRC rcStrict = iemMemApplySegment(pVCpu, IEM_ACCESS_TYPE_READ | IEM_ACCESS_WHAT_DATA, iEffSeg, 1, &GCPtrEff);
    if (rcStrict == VINF_SUCCESS)
    {
        RTGCPHYS GCPhysMem;
        /** @todo access size.   */
        rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, GCPtrEff, 1, IEM_ACCESS_TYPE_READ | IEM_ACCESS_WHAT_DATA, &GCPhysMem);
        if (rcStrict == VINF_SUCCESS)
        {
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
            if (   !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
                || !IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_VIRT_APIC_ACCESS))
            { /* probable */ }
            else
            {
                /*
                 * CLFLUSH/CLFLUSHOPT does not access the memory, but flushes the cache-line
                 * that contains the address. However, if the address falls in the APIC-access
                 * page, the address flushed must instead be the corresponding address in the
                 * virtual-APIC page.
                 *
                 * See Intel spec. 29.4.4 "Instruction-Specific Considerations".
                 */
                rcStrict = iemVmxVirtApicAccessUnused(pVCpu, &GCPhysMem, 1, IEM_ACCESS_TYPE_READ | IEM_ACCESS_WHAT_DATA);
                if (   rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE
                    && rcStrict != VINF_VMX_MODIFIES_BEHAVIOR)
                    return rcStrict;
            }
#endif
            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }
    }

    return rcStrict;
}


/**
 * Implements 'FINIT' and 'FNINIT'.
 *
 * @param   fCheckXcpts     Whether to check for umasked pending exceptions or
 *                          not.
 */
IEM_CIMPL_DEF_1(iemCImpl_finit, bool, fCheckXcpts)
{
    /*
     * Exceptions.
     */
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0);
    if (pVCpu->cpum.GstCtx.cr0 & (X86_CR0_EM | X86_CR0_TS))
        return iemRaiseDeviceNotAvailable(pVCpu);

    iemFpuActualizeStateForChange(pVCpu);
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_X87);

    /* FINIT: Raise #MF on pending exception(s): */
    if (fCheckXcpts && (pVCpu->cpum.GstCtx.XState.x87.FSW & X86_FSW_ES))
        return iemRaiseMathFault(pVCpu);

    /*
     * Reset the state.
     */
    PX86XSAVEAREA pXState = &pVCpu->cpum.GstCtx.XState;

    /* Rotate the stack to account for changed TOS. */
    iemFpuRotateStackSetTop(&pXState->x87, 0);

    pXState->x87.FCW        = 0x37f;
    pXState->x87.FSW        = 0;
    pXState->x87.FTW        = 0x00;     /* 0 - empty. */
    /** @todo Intel says the instruction and data pointers are not cleared on
     *        387, presume that 8087 and 287 doesn't do so either. */
    /** @todo test this stuff.   */
    if (IEM_GET_TARGET_CPU(pVCpu) > IEMTARGETCPU_386)
    {
        pXState->x87.FPUDP  = 0;
        pXState->x87.DS     = 0; //??
        pXState->x87.Rsrvd2 = 0;
        pXState->x87.FPUIP  = 0;
        pXState->x87.CS     = 0; //??
        pXState->x87.Rsrvd1 = 0;
    }
    pXState->x87.FOP        = 0;

    iemHlpUsedFpu(pVCpu);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'FXSAVE'.
 *
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEff        The address of the image.
 * @param   enmEffOpSize    The operand size (only REX.W really matters).
 */
IEM_CIMPL_DEF_3(iemCImpl_fxsave, uint8_t, iEffSeg, RTGCPTR, GCPtrEff, IEMMODE, enmEffOpSize)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX);

    /** @todo check out bugref{1529} and AMD behaviour  */

    /*
     * Raise exceptions.
     */
    if (pVCpu->cpum.GstCtx.cr0 & (X86_CR0_TS | X86_CR0_EM))
        return iemRaiseDeviceNotAvailable(pVCpu);

    /*
     * Access the memory.
     */
    uint8_t bUnmapInfo;
    void   *pvMem512;
    VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvMem512, &bUnmapInfo, 512,
                                      iEffSeg, GCPtrEff, IEM_ACCESS_DATA_W | IEM_ACCESS_PARTIAL_WRITE,
                                      15 | IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_GP_OR_AC);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;
    PX86FXSTATE  pDst = (PX86FXSTATE)pvMem512;
    PCX86FXSTATE pSrc = &pVCpu->cpum.GstCtx.XState.x87;

    /*
     * Store the registers.
     */
    /** @todo CPU/VM detection possible! If CR4.OSFXSR=0 MXCSR it's
     * implementation specific whether MXCSR and XMM0-XMM7 are saved. */

    /* common for all formats */
    pDst->FCW           = pSrc->FCW;
    pDst->FSW           = pSrc->FSW;
    pDst->FTW           = pSrc->FTW & UINT16_C(0xff);
    pDst->FOP           = pSrc->FOP;
    pDst->MXCSR         = pSrc->MXCSR;
    pDst->MXCSR_MASK    = CPUMGetGuestMxCsrMask(pVCpu->CTX_SUFF(pVM));
    for (uint32_t i = 0; i < RT_ELEMENTS(pDst->aRegs); i++)
    {
        /** @todo Testcase: What actually happens to the 6 reserved bytes? I'm clearing
         *        them for now... */
        pDst->aRegs[i].au32[0] = pSrc->aRegs[i].au32[0];
        pDst->aRegs[i].au32[1] = pSrc->aRegs[i].au32[1];
        pDst->aRegs[i].au32[2] = pSrc->aRegs[i].au32[2] & UINT32_C(0xffff);
        pDst->aRegs[i].au32[3] = 0;
    }

    /* FPU IP, CS, DP and DS. */
    pDst->FPUIP  = pSrc->FPUIP;
    pDst->CS     = pSrc->CS;
    pDst->FPUDP  = pSrc->FPUDP;
    pDst->DS     = pSrc->DS;
    if (enmEffOpSize == IEMMODE_64BIT)
    {
        /* Save upper 16-bits of FPUIP (IP:CS:Rsvd1) and FPUDP (DP:DS:Rsvd2). */
        pDst->Rsrvd1 = pSrc->Rsrvd1;
        pDst->Rsrvd2 = pSrc->Rsrvd2;
    }
    else
    {
        pDst->Rsrvd1 = 0;
        pDst->Rsrvd2 = 0;
    }

    /* XMM registers. Skipped in 64-bit CPL0 if EFER.FFXSR (AMD only) is set. */
    if (   !(pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_FFXSR)
        || !IEM_IS_64BIT_CODE(pVCpu)
        || IEM_GET_CPL(pVCpu) != 0)
    {
        uint32_t cXmmRegs = IEM_IS_64BIT_CODE(pVCpu) ? 16 : 8;
        for (uint32_t i = 0; i < cXmmRegs; i++)
            pDst->aXMM[i] = pSrc->aXMM[i];
        /** @todo Testcase: What happens to the reserved XMM registers? Untouched,
         *        right? */
    }

    /*
     * Commit the memory.
     */
    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'FXRSTOR'.
 *
 * @param   iEffSeg         The effective segment register for @a GCPtrEff.
 * @param   GCPtrEff        The address of the image.
 * @param   enmEffOpSize    The operand size (only REX.W really matters).
 */
IEM_CIMPL_DEF_3(iemCImpl_fxrstor, uint8_t, iEffSeg, RTGCPTR, GCPtrEff, IEMMODE, enmEffOpSize)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX);

    /** @todo check out bugref{1529} and AMD behaviour  */

    /*
     * Raise exceptions.
     */
    if (pVCpu->cpum.GstCtx.cr0 & (X86_CR0_TS | X86_CR0_EM))
        return iemRaiseDeviceNotAvailable(pVCpu);

    /*
     * Access the memory.
     */
    uint8_t bUnmapInfo;
    void   *pvMem512;
    VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvMem512, &bUnmapInfo, 512, iEffSeg, GCPtrEff, IEM_ACCESS_DATA_R,
                                      15 | IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_GP_OR_AC);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;
    PCX86FXSTATE pSrc = (PCX86FXSTATE)pvMem512;
    PX86FXSTATE  pDst = &pVCpu->cpum.GstCtx.XState.x87;

    /*
     * Check the state for stuff which will #GP(0).
     */
    uint32_t const fMXCSR      = pSrc->MXCSR;
    uint32_t const fMXCSR_MASK = CPUMGetGuestMxCsrMask(pVCpu->CTX_SUFF(pVM));
    if (fMXCSR & ~fMXCSR_MASK)
    {
        Log(("fxrstor: MXCSR=%#x (MXCSR_MASK=%#x) -> #GP(0)\n", fMXCSR, fMXCSR_MASK));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /*
     * Load the registers.
     */
    /** @todo CPU/VM detection possible! If CR4.OSFXSR=0 MXCSR it's
     * implementation specific whether MXCSR and XMM0-XMM7 are
     * restored according to Intel.
     * AMD says MXCSR and XMM registers are never loaded if
     * CR4.OSFXSR=0.
     */

    /* common for all formats */
    pDst->FCW       = pSrc->FCW;
    pDst->FSW       = pSrc->FSW;
    pDst->FTW       = pSrc->FTW & UINT16_C(0xff);
    pDst->FOP       = pSrc->FOP;
    pDst->MXCSR     = fMXCSR;
    /* (MXCSR_MASK is read-only) */
    for (uint32_t i = 0; i < RT_ELEMENTS(pSrc->aRegs); i++)
    {
        pDst->aRegs[i].au32[0] = pSrc->aRegs[i].au32[0];
        pDst->aRegs[i].au32[1] = pSrc->aRegs[i].au32[1];
        pDst->aRegs[i].au32[2] = pSrc->aRegs[i].au32[2] & UINT32_C(0xffff);
        pDst->aRegs[i].au32[3] = 0;
    }

    /* FPU IP, CS, DP and DS. */
    /** @todo AMD says this is only done if FSW.ES is set after loading. */
    if (enmEffOpSize == IEMMODE_64BIT)
    {
        pDst->FPUIP  = pSrc->FPUIP;
        pDst->CS     = pSrc->CS;
        pDst->Rsrvd1 = pSrc->Rsrvd1;
        pDst->FPUDP  = pSrc->FPUDP;
        pDst->DS     = pSrc->DS;
        pDst->Rsrvd2 = pSrc->Rsrvd2;
    }
    else
    {
        pDst->FPUIP  = pSrc->FPUIP;
        pDst->CS     = pSrc->CS;
        pDst->Rsrvd1 = 0;
        pDst->FPUDP  = pSrc->FPUDP;
        pDst->DS     = pSrc->DS;
        pDst->Rsrvd2 = 0;
    }

    /* XMM registers. Skipped in 64-bit CPL0 if EFER.FFXSR (AMD only) is set.
     * Does not affect MXCSR, only registers.
     */
    if (   !(pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_FFXSR)
        || !IEM_IS_64BIT_CODE(pVCpu)
        || IEM_GET_CPL(pVCpu) != 0)
    {
        uint32_t cXmmRegs = IEM_IS_64BIT_CODE(pVCpu) ? 16 : 8;
        for (uint32_t i = 0; i < cXmmRegs; i++)
            pDst->aXMM[i] = pSrc->aXMM[i];
    }

    pDst->FCW &= ~X86_FCW_ZERO_MASK | X86_FCW_IC_MASK; /* Intel 10980xe allows setting the IC bit. Win 3.11 CALC.EXE sets it. */
    iemFpuRecalcExceptionStatus(pDst);

    if (pDst->FSW & X86_FSW_ES)
        Log11(("fxrstor: %04x:%08RX64: loading state with pending FPU exception (FSW=%#x)\n",
               pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pSrc->FSW));

    /*
     * Unmap the memory.
     */
    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    iemHlpUsedFpu(pVCpu);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'XSAVE'.
 *
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEff        The address of the image.
 * @param   enmEffOpSize    The operand size (only REX.W really matters).
 */
IEM_CIMPL_DEF_3(iemCImpl_xsave, uint8_t, iEffSeg, RTGCPTR, GCPtrEff, IEMMODE, enmEffOpSize)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE | CPUMCTX_EXTRN_XCRx);

    /*
     * Raise exceptions.
     */
    if (!(pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSXSAVE))
        return iemRaiseUndefinedOpcode(pVCpu);
    /* When in VMX non-root mode and XSAVE/XRSTOR is not enabled, it results in #UD. */
    if (RT_LIKELY(    !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
                  || IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_XSAVES_XRSTORS)))
    { /* likely */ }
    else
    {
        Log(("xrstor: Not enabled for nested-guest execution -> #UD\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }
    if (pVCpu->cpum.GstCtx.cr0 & X86_CR0_TS)
        return iemRaiseDeviceNotAvailable(pVCpu);

    /*
     * Calc the requested mask.
     */
    uint64_t const fReqComponents = RT_MAKE_U64(pVCpu->cpum.GstCtx.eax, pVCpu->cpum.GstCtx.edx) & pVCpu->cpum.GstCtx.aXcr[0];
    AssertLogRelReturn(!(fReqComponents & ~(XSAVE_C_X87 | XSAVE_C_SSE | XSAVE_C_YMM)), VERR_IEM_ASPECT_NOT_IMPLEMENTED);
    uint64_t const fXInUse        = pVCpu->cpum.GstCtx.aXcr[0];

/** @todo figure out the exact protocol for the memory access.  Currently we
 *        just need this crap to work halfways to make it possible to test
 *        AVX instructions. */
/** @todo figure out the XINUSE and XMODIFIED   */

    /*
     * Access the x87 memory state.
     */
    /* The x87+SSE state.  */
    uint8_t bUnmapInfoMem512;
    void   *pvMem512;
    VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvMem512, &bUnmapInfoMem512, 512,
                                      iEffSeg, GCPtrEff, IEM_ACCESS_DATA_W | IEM_ACCESS_PARTIAL_WRITE,
                                      63 | IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_GP_OR_AC);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;
    PX86FXSTATE  pDst = (PX86FXSTATE)pvMem512;
    PCX86FXSTATE pSrc = &pVCpu->cpum.GstCtx.XState.x87;

    /* The header.  */
    uint8_t      bUnmapInfoHdr;
    PX86XSAVEHDR pHdr;
    rcStrict = iemMemMap(pVCpu, (void **)&pHdr, &bUnmapInfoHdr, sizeof(pHdr),
                         iEffSeg, GCPtrEff + 512, IEM_ACCESS_DATA_RW, 0 /* checked above */);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /*
     * Store the X87 state.
     */
    if (fReqComponents & XSAVE_C_X87)
    {
        /* common for all formats */
        pDst->FCW    = pSrc->FCW;
        pDst->FSW    = pSrc->FSW;
        pDst->FTW    = pSrc->FTW & UINT16_C(0xff);
        pDst->FOP    = pSrc->FOP;
        pDst->FPUIP  = pSrc->FPUIP;
        pDst->CS     = pSrc->CS;
        pDst->FPUDP  = pSrc->FPUDP;
        pDst->DS     = pSrc->DS;
        if (enmEffOpSize == IEMMODE_64BIT)
        {
            /* Save upper 16-bits of FPUIP (IP:CS:Rsvd1) and FPUDP (DP:DS:Rsvd2). */
            pDst->Rsrvd1 = pSrc->Rsrvd1;
            pDst->Rsrvd2 = pSrc->Rsrvd2;
        }
        else
        {
            pDst->Rsrvd1 = 0;
            pDst->Rsrvd2 = 0;
        }
        for (uint32_t i = 0; i < RT_ELEMENTS(pDst->aRegs); i++)
        {
            /** @todo Testcase: What actually happens to the 6 reserved bytes? I'm clearing
             *        them for now... */
            pDst->aRegs[i].au32[0] = pSrc->aRegs[i].au32[0];
            pDst->aRegs[i].au32[1] = pSrc->aRegs[i].au32[1];
            pDst->aRegs[i].au32[2] = pSrc->aRegs[i].au32[2] & UINT32_C(0xffff);
            pDst->aRegs[i].au32[3] = 0;
        }

    }

    if (fReqComponents & (XSAVE_C_SSE | XSAVE_C_YMM))
    {
        pDst->MXCSR         = pSrc->MXCSR;
        pDst->MXCSR_MASK    = CPUMGetGuestMxCsrMask(pVCpu->CTX_SUFF(pVM));
    }

    if (fReqComponents & XSAVE_C_SSE)
    {
        /* XMM registers. */
        uint32_t cXmmRegs = enmEffOpSize == IEMMODE_64BIT ? 16 : 8;
        for (uint32_t i = 0; i < cXmmRegs; i++)
            pDst->aXMM[i] = pSrc->aXMM[i];
        /** @todo Testcase: What happens to the reserved XMM registers? Untouched,
         *        right? */
    }

    /* Commit the x87 state bits. (probably wrong) */
    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfoMem512);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /*
     * Store AVX state.
     */
    if (fReqComponents & XSAVE_C_YMM)
    {
        /** @todo testcase: xsave64 vs xsave32 wrt XSAVE_C_YMM. */
        AssertLogRelReturn(pVCpu->cpum.GstCtx.aoffXState[XSAVE_C_YMM_BIT] != UINT16_MAX, VERR_IEM_IPE_9);
        uint8_t         bUnmapInfoComp;
        PCX86XSAVEYMMHI pCompSrc = CPUMCTX_XSAVE_C_PTR(IEM_GET_CTX(pVCpu), XSAVE_C_YMM_BIT, PCX86XSAVEYMMHI);
        PX86XSAVEYMMHI  pCompDst;
        rcStrict = iemMemMap(pVCpu, (void **)&pCompDst, &bUnmapInfoComp, sizeof(*pCompDst), iEffSeg,
                             GCPtrEff + pVCpu->cpum.GstCtx.aoffXState[XSAVE_C_YMM_BIT],
                             IEM_ACCESS_DATA_W | IEM_ACCESS_PARTIAL_WRITE, 0 /* checked above */);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        uint32_t cXmmRegs = enmEffOpSize == IEMMODE_64BIT ? 16 : 8;
        for (uint32_t i = 0; i < cXmmRegs; i++)
            pCompDst->aYmmHi[i] = pCompSrc->aYmmHi[i];

        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfoComp);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }

    /*
     * Update the header.
     */
    pHdr->bmXState = (pHdr->bmXState & ~fReqComponents)
                   | (fReqComponents & fXInUse);

    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfoHdr);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'XRSTOR'.
 *
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEff        The address of the image.
 * @param   enmEffOpSize    The operand size (only REX.W really matters).
 */
IEM_CIMPL_DEF_3(iemCImpl_xrstor, uint8_t, iEffSeg, RTGCPTR, GCPtrEff, IEMMODE, enmEffOpSize)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE | CPUMCTX_EXTRN_XCRx);

    /*
     * Raise exceptions.
     */
    if (!(pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSXSAVE))
        return iemRaiseUndefinedOpcode(pVCpu);
    /* When in VMX non-root mode and XSAVE/XRSTOR is not enabled, it results in #UD. */
    if (RT_LIKELY(   !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
                  || IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_XSAVES_XRSTORS)))
    { /* likely */ }
    else
    {
        Log(("xrstor: Not enabled for nested-guest execution -> #UD\n"));
        return iemRaiseUndefinedOpcode(pVCpu);
    }
    if (pVCpu->cpum.GstCtx.cr0 & X86_CR0_TS)
        return iemRaiseDeviceNotAvailable(pVCpu);
    if (GCPtrEff & 63)
    {
        /** @todo CPU/VM detection possible! \#AC might not be signal for
         * all/any misalignment sizes, intel says its an implementation detail. */
        if (   (pVCpu->cpum.GstCtx.cr0 & X86_CR0_AM)
            && pVCpu->cpum.GstCtx.eflags.Bits.u1AC
            && IEM_GET_CPL(pVCpu) == 3)
            return iemRaiseAlignmentCheckException(pVCpu);
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

/** @todo figure out the exact protocol for the memory access.  Currently we
 *        just need this crap to work halfways to make it possible to test
 *        AVX instructions. */
/** @todo figure out the XINUSE and XMODIFIED   */

    /*
     * Access the x87 memory state.
     */
    /* The x87+SSE state.  */
    uint8_t bUnmapInfoMem512;
    void   *pvMem512;
    VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvMem512, &bUnmapInfoMem512, 512, iEffSeg, GCPtrEff, IEM_ACCESS_DATA_R,
                                      63 | IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_GP_OR_AC);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;
    PCX86FXSTATE pSrc = (PCX86FXSTATE)pvMem512;
    PX86FXSTATE  pDst = &pVCpu->cpum.GstCtx.XState.x87;

    /*
     * Calc the requested mask
     */
    uint8_t       bUnmapInfoHdr;
    PX86XSAVEHDR  pHdrDst = &pVCpu->cpum.GstCtx.XState.Hdr;
    PCX86XSAVEHDR pHdrSrc;
    rcStrict = iemMemMap(pVCpu, (void **)&pHdrSrc, &bUnmapInfoHdr, sizeof(*pHdrSrc), iEffSeg, GCPtrEff + 512,
                         IEM_ACCESS_DATA_R, 0 /* checked above */);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    uint64_t const fReqComponents = RT_MAKE_U64(pVCpu->cpum.GstCtx.eax, pVCpu->cpum.GstCtx.edx) & pVCpu->cpum.GstCtx.aXcr[0];
    AssertLogRelReturn(!(fReqComponents & ~(XSAVE_C_X87 | XSAVE_C_SSE | XSAVE_C_YMM)), VERR_IEM_ASPECT_NOT_IMPLEMENTED);
    //uint64_t const fXInUse        = pVCpu->cpum.GstCtx.aXcr[0];
    uint64_t const fRstorMask     = pHdrSrc->bmXState;
    uint64_t const fCompMask      = pHdrSrc->bmXComp;

    AssertLogRelReturn(!(fCompMask & XSAVE_C_X), VERR_IEM_ASPECT_NOT_IMPLEMENTED);

    uint32_t const cXmmRegs = enmEffOpSize == IEMMODE_64BIT ? 16 : 8;

    /* We won't need this any longer. */
    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfoHdr);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /*
     * Load the X87 state.
     */
    if (fReqComponents & XSAVE_C_X87)
    {
        if (fRstorMask & XSAVE_C_X87)
        {
            pDst->FCW    = pSrc->FCW;
            pDst->FSW    = pSrc->FSW;
            pDst->FTW    = pSrc->FTW & UINT16_C(0xff);
            pDst->FOP    = pSrc->FOP;
            pDst->FPUIP  = pSrc->FPUIP;
            pDst->CS     = pSrc->CS;
            pDst->FPUDP  = pSrc->FPUDP;
            pDst->DS     = pSrc->DS;
            if (enmEffOpSize == IEMMODE_64BIT)
            {
                /* Load upper 16-bits of FPUIP (IP:CS:Rsvd1) and FPUDP (DP:DS:Rsvd2). */
                pDst->Rsrvd1 = pSrc->Rsrvd1;
                pDst->Rsrvd2 = pSrc->Rsrvd2;
            }
            else
            {
                pDst->Rsrvd1 = 0;
                pDst->Rsrvd2 = 0;
            }
            for (uint32_t i = 0; i < RT_ELEMENTS(pDst->aRegs); i++)
            {
                pDst->aRegs[i].au32[0] = pSrc->aRegs[i].au32[0];
                pDst->aRegs[i].au32[1] = pSrc->aRegs[i].au32[1];
                pDst->aRegs[i].au32[2] = pSrc->aRegs[i].au32[2] & UINT32_C(0xffff);
                pDst->aRegs[i].au32[3] = 0;
            }

            pDst->FCW &= ~X86_FCW_ZERO_MASK | X86_FCW_IC_MASK; /* Intel 10980xe allows setting the IC bit. Win 3.11 CALC.EXE sets it. */
            iemFpuRecalcExceptionStatus(pDst);

            if (pDst->FSW & X86_FSW_ES)
                Log11(("xrstor: %04x:%08RX64: loading state with pending FPU exception (FSW=%#x)\n",
                       pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pSrc->FSW));
        }
        else
        {
            pDst->FCW   = 0x37f;
            pDst->FSW   = 0;
            pDst->FTW   = 0x00;         /* 0 - empty. */
            pDst->FPUDP = 0;
            pDst->DS    = 0; //??
            pDst->Rsrvd2= 0;
            pDst->FPUIP = 0;
            pDst->CS    = 0; //??
            pDst->Rsrvd1= 0;
            pDst->FOP   = 0;
            for (uint32_t i = 0; i < RT_ELEMENTS(pSrc->aRegs); i++)
            {
                pDst->aRegs[i].au32[0] = 0;
                pDst->aRegs[i].au32[1] = 0;
                pDst->aRegs[i].au32[2] = 0;
                pDst->aRegs[i].au32[3] = 0;
            }
        }
        pHdrDst->bmXState |= XSAVE_C_X87; /* playing safe for now */
    }

    /* MXCSR */
    if (fReqComponents & (XSAVE_C_SSE | XSAVE_C_YMM))
    {
        if (fRstorMask & (XSAVE_C_SSE | XSAVE_C_YMM))
            pDst->MXCSR = pSrc->MXCSR;
        else
            pDst->MXCSR = 0x1f80;
    }

    /* XMM registers. */
    if (fReqComponents & XSAVE_C_SSE)
    {
        if (fRstorMask & XSAVE_C_SSE)
        {
            for (uint32_t i = 0; i < cXmmRegs; i++)
                pDst->aXMM[i] = pSrc->aXMM[i];
            /** @todo Testcase: What happens to the reserved XMM registers? Untouched,
             *        right? */
        }
        else
        {
            for (uint32_t i = 0; i < cXmmRegs; i++)
            {
                pDst->aXMM[i].au64[0] = 0;
                pDst->aXMM[i].au64[1] = 0;
            }
        }
        pHdrDst->bmXState |= XSAVE_C_SSE; /* playing safe for now */
    }

    /* Unmap the x87 state bits (so we've don't run out of mapping). */
    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfoMem512);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /*
     * Restore AVX state.
     */
    if (fReqComponents & XSAVE_C_YMM)
    {
        AssertLogRelReturn(pVCpu->cpum.GstCtx.aoffXState[XSAVE_C_YMM_BIT] != UINT16_MAX, VERR_IEM_IPE_9);
        PX86XSAVEYMMHI  pCompDst = CPUMCTX_XSAVE_C_PTR(IEM_GET_CTX(pVCpu), XSAVE_C_YMM_BIT, PX86XSAVEYMMHI);

        if (fRstorMask & XSAVE_C_YMM)
        {
            /** @todo testcase: xsave64 vs xsave32 wrt XSAVE_C_YMM. */
            uint8_t         bUnmapInfoComp;
            PCX86XSAVEYMMHI pCompSrc;
            rcStrict = iemMemMap(pVCpu, (void **)&pCompSrc, &bUnmapInfoComp, sizeof(*pCompDst),
                                 iEffSeg, GCPtrEff + pVCpu->cpum.GstCtx.aoffXState[XSAVE_C_YMM_BIT],
                                 IEM_ACCESS_DATA_R, 0 /* checked above */);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            for (uint32_t i = 0; i < cXmmRegs; i++)
            {
                pCompDst->aYmmHi[i].au64[0] = pCompSrc->aYmmHi[i].au64[0];
                pCompDst->aYmmHi[i].au64[1] = pCompSrc->aYmmHi[i].au64[1];
            }

            rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfoComp);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;
        }
        else
        {
            for (uint32_t i = 0; i < cXmmRegs; i++)
            {
                pCompDst->aYmmHi[i].au64[0] = 0;
                pCompDst->aYmmHi[i].au64[1] = 0;
            }
        }
        pHdrDst->bmXState |= XSAVE_C_YMM; /* playing safe for now */
    }

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}




/**
 * Implements 'STMXCSR'.
 *
 * @param   iEffSeg         The effective segment register for @a GCPtrEff.
 * @param   GCPtrEff        The address of the image.
 */
IEM_CIMPL_DEF_2(iemCImpl_stmxcsr, uint8_t, iEffSeg, RTGCPTR, GCPtrEff)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX);

    /*
     * Raise exceptions.
     */
    if (   !(pVCpu->cpum.GstCtx.cr0 & X86_CR0_EM)
        && (pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSFXSR))
    {
        if (!(pVCpu->cpum.GstCtx.cr0 & X86_CR0_TS))
        {
            /*
             * Do the job.
             */
            VBOXSTRICTRC rcStrict = iemMemStoreDataU32(pVCpu, iEffSeg, GCPtrEff, pVCpu->cpum.GstCtx.XState.x87.MXCSR);
            if (rcStrict == VINF_SUCCESS)
                return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
            return rcStrict;
        }
        return iemRaiseDeviceNotAvailable(pVCpu);
    }
    return iemRaiseUndefinedOpcode(pVCpu);
}


/**
 * Implements 'VSTMXCSR'.
 *
 * @param   iEffSeg         The effective segment register for @a GCPtrEff.
 * @param   GCPtrEff        The address of the image.
 */
IEM_CIMPL_DEF_2(iemCImpl_vstmxcsr, uint8_t, iEffSeg, RTGCPTR, GCPtrEff)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_XCRx);

    /*
     * Raise exceptions.
     */
    if (   (   !IEM_IS_GUEST_CPU_AMD(pVCpu)
            ? (pVCpu->cpum.GstCtx.aXcr[0] & (XSAVE_C_SSE | XSAVE_C_YMM)) == (XSAVE_C_SSE | XSAVE_C_YMM)
            : !(pVCpu->cpum.GstCtx.cr0 & X86_CR0_EM)) /* AMD Jaguar CPU (f0x16,m0,s1) behaviour */
        && (pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSXSAVE))
    {
        if (!(pVCpu->cpum.GstCtx.cr0 & X86_CR0_TS))
        {
            /*
             * Do the job.
             */
            VBOXSTRICTRC rcStrict = iemMemStoreDataU32(pVCpu, iEffSeg, GCPtrEff, pVCpu->cpum.GstCtx.XState.x87.MXCSR);
            if (rcStrict == VINF_SUCCESS)
                return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
            return rcStrict;
        }
        return iemRaiseDeviceNotAvailable(pVCpu);
    }
    return iemRaiseUndefinedOpcode(pVCpu);
}


/**
 * Implements 'LDMXCSR'.
 *
 * @param   iEffSeg         The effective segment register for @a GCPtrEff.
 * @param   GCPtrEff        The address of the image.
 */
IEM_CIMPL_DEF_2(iemCImpl_ldmxcsr, uint8_t, iEffSeg, RTGCPTR, GCPtrEff)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX);

    /*
     * Raise exceptions.
     */
    /** @todo testcase - order of LDMXCSR faults.  Does \#PF, \#GP and \#SS
     *        happen after or before \#UD and \#EM? */
    if (   !(pVCpu->cpum.GstCtx.cr0 & X86_CR0_EM)
        && (pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSFXSR))
    {
        if (!(pVCpu->cpum.GstCtx.cr0 & X86_CR0_TS))
        {
            /*
             * Do the job.
             */
            uint32_t fNewMxCsr;
            VBOXSTRICTRC rcStrict = iemMemFetchDataU32(pVCpu, &fNewMxCsr, iEffSeg, GCPtrEff);
            if (rcStrict == VINF_SUCCESS)
            {
                uint32_t const fMxCsrMask = CPUMGetGuestMxCsrMask(pVCpu->CTX_SUFF(pVM));
                if (!(fNewMxCsr & ~fMxCsrMask))
                {
                    pVCpu->cpum.GstCtx.XState.x87.MXCSR = fNewMxCsr;
                    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
                }
                Log(("ldmxcsr: New MXCSR=%#RX32 & ~MASK=%#RX32 = %#RX32 -> #GP(0)\n",
                     fNewMxCsr, fMxCsrMask, fNewMxCsr & ~fMxCsrMask));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }
            return rcStrict;
        }
        return iemRaiseDeviceNotAvailable(pVCpu);
    }
    return iemRaiseUndefinedOpcode(pVCpu);
}


/**
 * Implements 'VSTMXCSR'.
 *
 * @param   iEffSeg         The effective segment register for @a GCPtrEff.
 * @param   GCPtrEff        The address of the image.
 */
IEM_CIMPL_DEF_2(iemCImpl_vldmxcsr, uint8_t, iEffSeg, RTGCPTR, GCPtrEff)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_XCRx);

    /*
     * Raise exceptions.
     */
    if (   (   !IEM_IS_GUEST_CPU_AMD(pVCpu)
            ? (pVCpu->cpum.GstCtx.aXcr[0] & (XSAVE_C_SSE | XSAVE_C_YMM)) == (XSAVE_C_SSE | XSAVE_C_YMM)
            : !(pVCpu->cpum.GstCtx.cr0 & X86_CR0_EM))
        && (pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSXSAVE))
    {
        if (!(pVCpu->cpum.GstCtx.cr0 & X86_CR0_TS))
        {
            /*
             * Do the job.
             */
            uint32_t fNewMxCsr;
            VBOXSTRICTRC rcStrict = iemMemFetchDataU32(pVCpu, &fNewMxCsr, iEffSeg, GCPtrEff);
            if (rcStrict == VINF_SUCCESS)
            {
                uint32_t const fMxCsrMask = CPUMGetGuestMxCsrMask(pVCpu->CTX_SUFF(pVM));
                if (!(fNewMxCsr & ~fMxCsrMask))
                {
                    pVCpu->cpum.GstCtx.XState.x87.MXCSR = fNewMxCsr;
                    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
                }
                Log(("ldmxcsr: New MXCSR=%#RX32 & ~MASK=%#RX32 = %#RX32 -> #GP(0)\n",
                     fNewMxCsr, fMxCsrMask, fNewMxCsr & ~fMxCsrMask));
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }
            return rcStrict;
        }
        return iemRaiseDeviceNotAvailable(pVCpu);
    }
    return iemRaiseUndefinedOpcode(pVCpu);
}


/**
 * Commmon routine for fnstenv and fnsave.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   enmEffOpSize    The effective operand size.
 * @param   uPtr            Where to store the state.
 */
static void iemCImplCommonFpuStoreEnv(PVMCPUCC pVCpu, IEMMODE enmEffOpSize, RTPTRUNION uPtr)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_X87);
    PCX86FXSTATE pSrcX87 = &pVCpu->cpum.GstCtx.XState.x87;
    if (enmEffOpSize == IEMMODE_16BIT)
    {
        uPtr.pu16[0] = pSrcX87->FCW;
        uPtr.pu16[1] = pSrcX87->FSW;
        uPtr.pu16[2] = iemFpuCalcFullFtw(pSrcX87);
        if (IEM_IS_REAL_OR_V86_MODE(pVCpu))
        {
            /** @todo Testcase: How does this work when the FPUIP/CS was saved in
             *        protected mode or long mode and we save it in real mode?  And vice
             *        versa?  And with 32-bit operand size?  I think CPU is storing the
             *        effective address ((CS << 4) + IP) in the offset register and not
             *        doing any address calculations here. */
            uPtr.pu16[3] = (uint16_t)pSrcX87->FPUIP;
            uPtr.pu16[4] = ((pSrcX87->FPUIP >> 4) & UINT16_C(0xf000)) | pSrcX87->FOP;
            uPtr.pu16[5] = (uint16_t)pSrcX87->FPUDP;
            uPtr.pu16[6] = (pSrcX87->FPUDP  >> 4) & UINT16_C(0xf000);
        }
        else
        {
            uPtr.pu16[3] = pSrcX87->FPUIP;
            uPtr.pu16[4] = pSrcX87->CS;
            uPtr.pu16[5] = pSrcX87->FPUDP;
            uPtr.pu16[6] = pSrcX87->DS;
        }
    }
    else
    {
        /** @todo Testcase: what is stored in the "gray" areas? (figure 8-9 and 8-10) */
        uPtr.pu16[0*2]   = pSrcX87->FCW;
        uPtr.pu16[0*2+1] = 0xffff;  /* (0xffff observed on intel skylake.) */
        uPtr.pu16[1*2]   = pSrcX87->FSW;
        uPtr.pu16[1*2+1] = 0xffff;
        uPtr.pu16[2*2]   = iemFpuCalcFullFtw(pSrcX87);
        uPtr.pu16[2*2+1] = 0xffff;
        if (IEM_IS_REAL_OR_V86_MODE(pVCpu))
        {
            uPtr.pu16[3*2]   = (uint16_t)pSrcX87->FPUIP;
            uPtr.pu32[4]     = ((pSrcX87->FPUIP & UINT32_C(0xffff0000)) >> 4) | pSrcX87->FOP;
            uPtr.pu16[5*2]   = (uint16_t)pSrcX87->FPUDP;
            uPtr.pu32[6]     = (pSrcX87->FPUDP  & UINT32_C(0xffff0000)) >> 4;
        }
        else
        {
            uPtr.pu32[3]     = pSrcX87->FPUIP;
            uPtr.pu16[4*2]   = pSrcX87->CS;
            uPtr.pu16[4*2+1] = pSrcX87->FOP;
            uPtr.pu32[5]     = pSrcX87->FPUDP;
            uPtr.pu16[6*2]   = pSrcX87->DS;
            uPtr.pu16[6*2+1] = 0xffff;
        }
    }
}


/**
 * Commmon routine for fldenv and frstor
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   enmEffOpSize    The effective operand size.
 * @param   uPtr                Where to store the state.
 */
static void iemCImplCommonFpuRestoreEnv(PVMCPUCC pVCpu, IEMMODE enmEffOpSize, RTCPTRUNION uPtr)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_X87);
    PX86FXSTATE pDstX87 = &pVCpu->cpum.GstCtx.XState.x87;
    if (enmEffOpSize == IEMMODE_16BIT)
    {
        pDstX87->FCW = uPtr.pu16[0];
        pDstX87->FSW = uPtr.pu16[1];
        pDstX87->FTW = uPtr.pu16[2];
        if (IEM_IS_REAL_OR_V86_MODE(pVCpu))
        {
            pDstX87->FPUIP = uPtr.pu16[3] | ((uint32_t)(uPtr.pu16[4] & UINT16_C(0xf000)) << 4);
            pDstX87->FPUDP = uPtr.pu16[5] | ((uint32_t)(uPtr.pu16[6] & UINT16_C(0xf000)) << 4);
            pDstX87->FOP   = uPtr.pu16[4] & UINT16_C(0x07ff);
            pDstX87->CS    = 0;
            pDstX87->Rsrvd1= 0;
            pDstX87->DS    = 0;
            pDstX87->Rsrvd2= 0;
        }
        else
        {
            pDstX87->FPUIP = uPtr.pu16[3];
            pDstX87->CS    = uPtr.pu16[4];
            pDstX87->Rsrvd1= 0;
            pDstX87->FPUDP = uPtr.pu16[5];
            pDstX87->DS    = uPtr.pu16[6];
            pDstX87->Rsrvd2= 0;
            /** @todo Testcase: Is FOP cleared when doing 16-bit protected mode fldenv? */
        }
    }
    else
    {
        pDstX87->FCW = uPtr.pu16[0*2];
        pDstX87->FSW = uPtr.pu16[1*2];
        pDstX87->FTW = uPtr.pu16[2*2];
        if (IEM_IS_REAL_OR_V86_MODE(pVCpu))
        {
            pDstX87->FPUIP = uPtr.pu16[3*2] | ((uPtr.pu32[4] & UINT32_C(0x0ffff000)) << 4);
            pDstX87->FOP   = uPtr.pu32[4] & UINT16_C(0x07ff);
            pDstX87->FPUDP = uPtr.pu16[5*2] | ((uPtr.pu32[6] & UINT32_C(0x0ffff000)) << 4);
            pDstX87->CS    = 0;
            pDstX87->Rsrvd1= 0;
            pDstX87->DS    = 0;
            pDstX87->Rsrvd2= 0;
        }
        else
        {
            pDstX87->FPUIP = uPtr.pu32[3];
            pDstX87->CS    = uPtr.pu16[4*2];
            pDstX87->Rsrvd1= 0;
            pDstX87->FOP   = uPtr.pu16[4*2+1];
            pDstX87->FPUDP = uPtr.pu32[5];
            pDstX87->DS    = uPtr.pu16[6*2];
            pDstX87->Rsrvd2= 0;
        }
    }

    /* Make adjustments. */
    pDstX87->FTW = iemFpuCompressFtw(pDstX87->FTW);
#ifdef LOG_ENABLED
    uint16_t const fOldFsw = pDstX87->FSW;
#endif
    pDstX87->FCW &= ~X86_FCW_ZERO_MASK | X86_FCW_IC_MASK; /* Intel 10980xe allows setting the IC bit. Win 3.11 CALC.EXE sets it. */
    iemFpuRecalcExceptionStatus(pDstX87);
#ifdef LOG_ENABLED
    if ((pDstX87->FSW & X86_FSW_ES) ^ (fOldFsw & X86_FSW_ES))
        Log11(("iemCImplCommonFpuRestoreEnv: %04x:%08RX64: %s FPU exception (FCW=%#x FSW=%#x -> %#x)\n",
               pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, fOldFsw & X86_FSW_ES ? "Suppressed" : "Raised",
               pDstX87->FCW, fOldFsw, pDstX87->FSW));
#endif

    /** @todo Testcase: Check if ES and/or B are automatically cleared if no
     *        exceptions are pending after loading the saved state? */
}


/**
 * Implements 'FNSTENV'.
 *
 * @param   enmEffOpSize    The operand size (only REX.W really matters).
 * @param   iEffSeg         The effective segment register for @a GCPtrEffDst.
 * @param   GCPtrEffDst     The address of the image.
 */
IEM_CIMPL_DEF_3(iemCImpl_fnstenv, IEMMODE, enmEffOpSize, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst)
{
    uint8_t      bUnmapInfo;
    RTPTRUNION   uPtr;
    VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &uPtr.pv, &bUnmapInfo, enmEffOpSize == IEMMODE_16BIT ? 14 : 28,
                                      iEffSeg, GCPtrEffDst, IEM_ACCESS_DATA_W | IEM_ACCESS_PARTIAL_WRITE,
                                      enmEffOpSize == IEMMODE_16BIT ? 1 : 3 /** @todo ? */);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    iemCImplCommonFpuStoreEnv(pVCpu, enmEffOpSize, uPtr);

    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /* Mask all math exceptions. Any possibly pending exceptions will be cleared. */
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    pFpuCtx->FCW |= X86_FCW_XCPT_MASK;
#ifdef LOG_ENABLED
    uint16_t fOldFsw = pFpuCtx->FSW;
#endif
    iemFpuRecalcExceptionStatus(pFpuCtx);
#ifdef LOG_ENABLED
    if ((pFpuCtx->FSW & X86_FSW_ES) ^ (fOldFsw & X86_FSW_ES))
        Log11(("fnstenv: %04x:%08RX64: %s FPU exception (FCW=%#x, FSW %#x -> %#x)\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
               fOldFsw & X86_FSW_ES ? "Suppressed" : "Raised", pFpuCtx->FCW, fOldFsw, pFpuCtx->FSW));
#endif

    iemHlpUsedFpu(pVCpu);

    /* Note: C0, C1, C2 and C3 are documented as undefined, we leave them untouched! */
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'FNSAVE'.
 *
 * @param   enmEffOpSize    The operand size.
 * @param   iEffSeg         The effective segment register for @a GCPtrEffDst.
 * @param   GCPtrEffDst     The address of the image.
 */
IEM_CIMPL_DEF_3(iemCImpl_fnsave, IEMMODE, enmEffOpSize, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_X87);

    uint8_t      bUnmapInfo;
    RTPTRUNION   uPtr;
    VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &uPtr.pv, &bUnmapInfo, enmEffOpSize == IEMMODE_16BIT ? 94 : 108,
                                      iEffSeg, GCPtrEffDst, IEM_ACCESS_DATA_W | IEM_ACCESS_PARTIAL_WRITE, 3 /** @todo ? */);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemCImplCommonFpuStoreEnv(pVCpu, enmEffOpSize, uPtr);
    PRTFLOAT80U paRegs = (PRTFLOAT80U)(uPtr.pu8 + (enmEffOpSize == IEMMODE_16BIT ? 14 : 28));
    for (uint32_t i = 0; i < RT_ELEMENTS(pFpuCtx->aRegs); i++)
    {
        paRegs[i].au32[0] = pFpuCtx->aRegs[i].au32[0];
        paRegs[i].au32[1] = pFpuCtx->aRegs[i].au32[1];
        paRegs[i].au16[4] = pFpuCtx->aRegs[i].au16[4];
    }

    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /* Rotate the stack to account for changed TOS. */
    iemFpuRotateStackSetTop(pFpuCtx, 0);

    /*
     * Re-initialize the FPU context.
     */
    pFpuCtx->FCW   = 0x37f;
    pFpuCtx->FSW   = 0;
    pFpuCtx->FTW   = 0x00;       /* 0 - empty */
    pFpuCtx->FPUDP = 0;
    pFpuCtx->DS    = 0;
    pFpuCtx->Rsrvd2= 0;
    pFpuCtx->FPUIP = 0;
    pFpuCtx->CS    = 0;
    pFpuCtx->Rsrvd1= 0;
    pFpuCtx->FOP   = 0;

    iemHlpUsedFpu(pVCpu);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}



/**
 * Implements 'FLDENV'.
 *
 * @param   enmEffOpSize    The operand size (only REX.W really matters).
 * @param   iEffSeg         The effective segment register for @a GCPtrEffSrc.
 * @param   GCPtrEffSrc     The address of the image.
 */
IEM_CIMPL_DEF_3(iemCImpl_fldenv, IEMMODE, enmEffOpSize, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc)
{
    uint8_t      bUnmapInfo;
    RTCPTRUNION  uPtr;
    VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, (void **)&uPtr.pv, &bUnmapInfo, enmEffOpSize == IEMMODE_16BIT ? 14 : 28,
                                      iEffSeg, GCPtrEffSrc, IEM_ACCESS_DATA_R,
                                      enmEffOpSize == IEMMODE_16BIT ? 1 : 3 /** @todo ?*/);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    iemCImplCommonFpuRestoreEnv(pVCpu, enmEffOpSize, uPtr);

    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    iemHlpUsedFpu(pVCpu);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'FRSTOR'.
 *
 * @param   enmEffOpSize    The operand size.
 * @param   iEffSeg         The effective segment register for @a GCPtrEffSrc.
 * @param   GCPtrEffSrc     The address of the image.
 */
IEM_CIMPL_DEF_3(iemCImpl_frstor, IEMMODE, enmEffOpSize, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc)
{
    uint8_t      bUnmapInfo;
    RTCPTRUNION  uPtr;
    VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, (void **)&uPtr.pv, &bUnmapInfo, enmEffOpSize == IEMMODE_16BIT ? 94 : 108,
                                      iEffSeg, GCPtrEffSrc, IEM_ACCESS_DATA_R, 3 /** @todo ?*/ );
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    iemCImplCommonFpuRestoreEnv(pVCpu, enmEffOpSize, uPtr);
    PCRTFLOAT80U paRegs = (PCRTFLOAT80U)(uPtr.pu8 + (enmEffOpSize == IEMMODE_16BIT ? 14 : 28));
    for (uint32_t i = 0; i < RT_ELEMENTS(pFpuCtx->aRegs); i++)
    {
        pFpuCtx->aRegs[i].au32[0] = paRegs[i].au32[0];
        pFpuCtx->aRegs[i].au32[1] = paRegs[i].au32[1];
        pFpuCtx->aRegs[i].au32[2] = paRegs[i].au16[4];
        pFpuCtx->aRegs[i].au32[3] = 0;
    }

    rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    iemHlpUsedFpu(pVCpu);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'FLDCW'.
 *
 * @param   u16Fcw          The new FCW.
 */
IEM_CIMPL_DEF_1(iemCImpl_fldcw, uint16_t, u16Fcw)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_X87);

    /** @todo Testcase: Check what happens when trying to load X86_FCW_PC_RSVD. */
    /** @todo Testcase: Try see what happens when trying to set undefined bits
     *        (other than 6 and 7).  Currently ignoring them. */
    /** @todo Testcase: Test that it raises and loweres the FPU exception bits
     *        according to FSW. (This is what is currently implemented.) */
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    pFpuCtx->FCW = u16Fcw & (~X86_FCW_ZERO_MASK | X86_FCW_IC_MASK); /* Intel 10980xe allows setting the IC bit. Win 3.11 CALC.EXE sets it. */
#ifdef LOG_ENABLED
    uint16_t fOldFsw = pFpuCtx->FSW;
#endif
    iemFpuRecalcExceptionStatus(pFpuCtx);
#ifdef LOG_ENABLED
    if ((pFpuCtx->FSW & X86_FSW_ES) ^ (fOldFsw & X86_FSW_ES))
        Log11(("fldcw: %04x:%08RX64: %s FPU exception (FCW=%#x, FSW %#x -> %#x)\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip,
               fOldFsw & X86_FSW_ES ? "Suppressed" : "Raised", pFpuCtx->FCW, fOldFsw, pFpuCtx->FSW));
#endif

    /* Note: C0, C1, C2 and C3 are documented as undefined, we leave them untouched! */
    iemHlpUsedFpu(pVCpu);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}



/**
 * Implements the underflow case of fxch.
 *
 * @param   iStReg              The other stack register.
 * @param   uFpuOpcode          The FPU opcode (for simplicity).
 */
IEM_CIMPL_DEF_2(iemCImpl_fxch_underflow, uint8_t, iStReg, uint16_t, uFpuOpcode)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_X87);

    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    unsigned const iReg1 = X86_FSW_TOP_GET(pFpuCtx->FSW);
    unsigned const iReg2 = (iReg1 + iStReg) & X86_FSW_TOP_SMASK;
    Assert(!(RT_BIT(iReg1) & pFpuCtx->FTW) || !(RT_BIT(iReg2) & pFpuCtx->FTW));

    /** @todo Testcase: fxch underflow. Making assumptions that underflowed
     *        registers are read as QNaN and then exchanged. This could be
     *        wrong... */
    if (pFpuCtx->FCW & X86_FCW_IM)
    {
        if (RT_BIT(iReg1) & pFpuCtx->FTW)
        {
            if (RT_BIT(iReg2) & pFpuCtx->FTW)
                iemFpuStoreQNan(&pFpuCtx->aRegs[0].r80);
            else
                pFpuCtx->aRegs[0].r80 = pFpuCtx->aRegs[iStReg].r80;
            iemFpuStoreQNan(&pFpuCtx->aRegs[iStReg].r80);
        }
        else
        {
            pFpuCtx->aRegs[iStReg].r80 = pFpuCtx->aRegs[0].r80;
            iemFpuStoreQNan(&pFpuCtx->aRegs[0].r80);
        }
        pFpuCtx->FSW &= ~X86_FSW_C_MASK;
        pFpuCtx->FSW |= X86_FSW_C1 | X86_FSW_IE | X86_FSW_SF;
    }
    else
    {
        /* raise underflow exception, don't change anything. */
        pFpuCtx->FSW &= ~(X86_FSW_TOP_MASK | X86_FSW_XCPT_MASK);
        pFpuCtx->FSW |= X86_FSW_C1 | X86_FSW_IE | X86_FSW_SF | X86_FSW_ES | X86_FSW_B;
        Log11(("fxch: %04x:%08RX64: Underflow exception (FSW=%#x)\n",
               pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pFpuCtx->FSW));
    }

    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, uFpuOpcode);
    iemHlpUsedFpu(pVCpu);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'FCOMI', 'FCOMIP', 'FUCOMI', and 'FUCOMIP'.
 *
 * @param   iStReg              The other stack register.
 * @param   fUCmp               true for FUCOMI[P], false for FCOMI[P].
 * @param   uPopAndFpuOpcode    Bits 15-0: The FPU opcode.
 *                              Bit  31: Whether we should pop the stack when
 *                              done or not.
 */
IEM_CIMPL_DEF_3(iemCImpl_fcomi_fucomi, uint8_t, iStReg, bool, fUCmp, uint32_t, uPopAndFpuOpcode)
{
    Assert(iStReg < 8);
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_X87);

    /*
     * Raise exceptions.
     */
    if (pVCpu->cpum.GstCtx.cr0 & (X86_CR0_EM | X86_CR0_TS))
        return iemRaiseDeviceNotAvailable(pVCpu);

    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    uint16_t u16Fsw = pFpuCtx->FSW;
    if (u16Fsw & X86_FSW_ES)
        return iemRaiseMathFault(pVCpu);

    /*
     * Check if any of the register accesses causes #SF + #IA.
     */
    bool           fPop  = RT_BOOL(uPopAndFpuOpcode & RT_BIT_32(31));
    unsigned const iReg1 = X86_FSW_TOP_GET(u16Fsw);
    unsigned const iReg2 = (iReg1 + iStReg) & X86_FSW_TOP_SMASK;
    if ((pFpuCtx->FTW & (RT_BIT(iReg1) | RT_BIT(iReg2))) == (RT_BIT(iReg1) | RT_BIT(iReg2)))
    {
        uint32_t u32Eflags;
        if (!fUCmp)
            u32Eflags = iemAImpl_fcomi_r80_by_r80(pFpuCtx, &u16Fsw, &pFpuCtx->aRegs[0].r80, &pFpuCtx->aRegs[iStReg].r80);
        else
            u32Eflags = iemAImpl_fucomi_r80_by_r80(pFpuCtx, &u16Fsw, &pFpuCtx->aRegs[0].r80, &pFpuCtx->aRegs[iStReg].r80);

        pFpuCtx->FSW &= ~X86_FSW_C1;
        pFpuCtx->FSW |= u16Fsw & ~X86_FSW_TOP_MASK;
        if (   !(u16Fsw & X86_FSW_IE)
            || (pFpuCtx->FCW & X86_FCW_IM) )
        {
            pVCpu->cpum.GstCtx.eflags.u &= ~(X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF);
            pVCpu->cpum.GstCtx.eflags.u |= u32Eflags & (X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF);
        }
    }
    else if (pFpuCtx->FCW & X86_FCW_IM)
    {
        /* Masked underflow. */
        pFpuCtx->FSW &= ~X86_FSW_C1;
        pFpuCtx->FSW |= X86_FSW_IE | X86_FSW_SF;
        pVCpu->cpum.GstCtx.eflags.u &= ~(X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF);
        pVCpu->cpum.GstCtx.eflags.u |= X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF;
    }
    else
    {
        /* Raise underflow - don't touch EFLAGS or TOP. */
        pFpuCtx->FSW &= ~X86_FSW_C1;
        pFpuCtx->FSW |= X86_FSW_IE | X86_FSW_SF | X86_FSW_ES | X86_FSW_B;
        Log11(("fxch: %04x:%08RX64: Raising IE+SF exception (FSW=%#x)\n",
               pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pFpuCtx->FSW));
        fPop = false;
    }

    /*
     * Pop if necessary.
     */
    if (fPop)
    {
        pFpuCtx->FTW &= ~RT_BIT(iReg1);
        iemFpuStackIncTop(pVCpu);
    }

    iemFpuUpdateOpcodeAndIpWorkerEx(pVCpu, pFpuCtx, (uint16_t)uPopAndFpuOpcode);
    iemHlpUsedFpu(pVCpu);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'RDSEED'.
 *
 * @returns VINF_SUCCESS.
 * @param   iReg            The register.
 * @param   enmEffOpSize    The operand size.
 */
IEM_CIMPL_DEF_2(iemCImpl_rdseed, uint8_t, iReg, IEMMODE, enmEffOpSize)
{
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    /* Nested-guest VMX intercept. */
    if (   !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
        || !IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_RDSEED_EXIT))
    { /* probable */ }
    else
    {
        Log(("rdseed: Guest intercept -> VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_NEEDS_INFO_RET(pVCpu, VMX_EXIT_RDSEED, VMXINSTRID_RDSEED, cbInstr);
    }
#endif

    uint32_t *pEFlags = &pVCpu->cpum.GstCtx.eflags.uBoth;
    switch (enmEffOpSize)
    {
        case IEMMODE_16BIT:
        {
            PFNIEMAIMPLRDRANDSEEDU16 pfnImpl = IEM_SELECT_HOST_OR_FALLBACK(fRdSeed,
                                                                           &iemAImpl_rdseed_u16,
                                                                           &iemAImpl_rdseed_u16_fallback);
            uint16_t *pu16Dst = iemGRegRefU16(pVCpu, iReg);
            (pfnImpl)(pu16Dst, pEFlags);
            break;
        }
        case IEMMODE_32BIT:
        {
            PFNIEMAIMPLRDRANDSEEDU32 pfnImpl = IEM_SELECT_HOST_OR_FALLBACK(fRdSeed,
                                                                           &iemAImpl_rdseed_u32,
                                                                           &iemAImpl_rdseed_u32_fallback);
            uint32_t *pu32Dst = iemGRegRefU32(pVCpu, iReg);
            (pfnImpl)(pu32Dst, pEFlags);
            iemGRegStoreU32(pVCpu, iReg, *pu32Dst);
            break;
        }
        case IEMMODE_64BIT:
        {
            PFNIEMAIMPLRDRANDSEEDU64 pfnImpl = IEM_SELECT_HOST_OR_FALLBACK(fRdSeed,
                                                                           &iemAImpl_rdseed_u64,
                                                                           &iemAImpl_rdseed_u64_fallback);
            uint64_t *pu64Dst = iemGRegRefU64(pVCpu, iReg);
            (pfnImpl)(pu64Dst, pEFlags);
            break;
        }
        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'RDRAND'.
 *
 * @returns VINF_SUCCESS.
 * @param   iReg            The register.
 * @param   enmEffOpSize    The operand size.
 */
IEM_CIMPL_DEF_2(iemCImpl_rdrand, uint8_t, iReg, IEMMODE, enmEffOpSize)
{
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    /* Nested-guest VMX intercept. */
    if (   !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
        || !IEM_VMX_IS_PROCCTLS2_SET(pVCpu, VMX_PROC_CTLS2_RDRAND_EXIT))
    { /* probable */ }
    else
    {
        Log(("rdrand: Guest intercept -> VM-exit\n"));
        IEM_VMX_VMEXIT_INSTR_NEEDS_INFO_RET(pVCpu, VMX_EXIT_RDRAND, VMXINSTRID_RDRAND, cbInstr);
    }
#endif

    uint32_t *pEFlags = &pVCpu->cpum.GstCtx.eflags.uBoth;
    switch (enmEffOpSize)
    {
        case IEMMODE_16BIT:
        {
            PFNIEMAIMPLRDRANDSEEDU16 pfnImpl = IEM_SELECT_HOST_OR_FALLBACK(fRdRand, &iemAImpl_rdrand_u16,
                                                                                    &iemAImpl_rdrand_u16_fallback);
            uint16_t *pu16Dst = iemGRegRefU16(pVCpu, iReg);
            (pfnImpl)(pu16Dst, pEFlags);
            break;
        }
        case IEMMODE_32BIT:
        {
            PFNIEMAIMPLRDRANDSEEDU32 pfnImpl = IEM_SELECT_HOST_OR_FALLBACK(fRdRand, &iemAImpl_rdrand_u32,
                                                                                    &iemAImpl_rdrand_u32_fallback);
            uint32_t *pu32Dst = iemGRegRefU32(pVCpu, iReg);
            (pfnImpl)(pu32Dst, pEFlags);
            iemGRegStoreU32(pVCpu, iReg, *pu32Dst);
            break;
        }
        case IEMMODE_64BIT:
        {
            PFNIEMAIMPLRDRANDSEEDU64 pfnImpl = IEM_SELECT_HOST_OR_FALLBACK(fRdRand, &iemAImpl_rdrand_u64,
                                                                                    &iemAImpl_rdrand_u64_fallback);
            uint64_t *pu64Dst = iemGRegRefU64(pVCpu, iReg);
            (pfnImpl)(pu64Dst, pEFlags);
            break;
        }
        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Worker for 'VMASKMOVPS / VPMASKMOVD' 128-bit 32-bit-masked load.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr         The current instruction length.
 * @param   iXRegDst        The destination XMM register index.
 * @param   iXRegMsk        The mask XMM register index.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffSrc     The source memory address.
 */
static VBOXSTRICTRC iemCImpl_maskmov_load_u128_32_worker(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iXRegDst, uint8_t iXRegMsk, uint8_t iEffSeg, RTGCPTR GCPtrEffSrc)
{
    uint32_t fAccessed = 0;

     PRTUINT128U puDst =  (PRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDst];
    PCRTUINT128U puMsk = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegMsk];
    PCRTUINT128U puSrc;

    for (uint32_t i = 0; i < RT_ELEMENTS(puMsk->au32); i++)
    {
        fAccessed |= puMsk->au32[i];
    }

    if (fAccessed & RT_BIT(31)) {
        /*
         * Access the source memory.
         */
        uint8_t bUnmapInfo;
        void   *pvMemSrc;
        VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvMemSrc, &bUnmapInfo, sizeof(*puSrc),
                                          iEffSeg, GCPtrEffSrc, IEM_ACCESS_DATA_R, 0);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        puSrc = (PCRTUINT128U)pvMemSrc;

        for (uint32_t i = 0; i < RT_ELEMENTS(puSrc->au32); i++)
        {
            puDst->au32[i] = (puMsk->au32[i] & RT_BIT(31)) ? puSrc->au32[i] : 0;
        }
        pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iXRegDst].au64[0] = 0;
        pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iXRegDst].au64[1] = 0;

        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
        pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iXRegDst].au64[0] = 0;
        pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iXRegDst].au64[1] = 0;
    }

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}



/**
 * Worker for 'VMASKMOVPS / VPMASKMOVD' 256-bit 32-bit-masked load.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr         The current instruction length.
 * @param   iYRegDst        The destination YMM register index.
 * @param   iYRegMsk        The mask YMM register index.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffSrc     The source memory address.
 */
static VBOXSTRICTRC iemCImpl_maskmov_load_u256_32_worker(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iYRegDst, uint8_t iYRegMsk, uint8_t iEffSeg, RTGCPTR GCPtrEffSrc)
{
    uint32_t fAccessed = 0;

     PRTUINT128U puDstLo =  (PRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDst];
     PRTUINT128U puDstHi =  (PRTUINT128U)&pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDst];
    PCRTUINT128U puMskLo = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegMsk];
    PCRTUINT128U puMskHi = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegMsk];
    PCRTUINT256U puSrc;

    for (uint32_t i = 0; i < RT_ELEMENTS(puMskLo->au32); i++)
    {
        fAccessed |= puMskLo->au32[i] | puMskHi->au32[i];
    }

    if (fAccessed & RT_BIT(31)) {
        /*
         * Access the source memory.
         */
        uint8_t bUnmapInfo;
        void   *pvMemSrc;
        VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvMemSrc, &bUnmapInfo, sizeof(*puSrc),
                                          iEffSeg, GCPtrEffSrc, IEM_ACCESS_DATA_R, 0);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        puSrc   = (PCRTUINT256U)pvMemSrc;

        uint8_t const iHalf = RT_ELEMENTS(puSrc->au32) / 2;

        for (uint32_t i = 0; i < iHalf; i++)
        {
            puDstLo->au32[i] = (puMskLo->au32[i] & RT_BIT(31)) ? puSrc->au32[i] : 0;
        }
        for (uint32_t i = iHalf; i < RT_ELEMENTS(puSrc->au32); i++)
        {
            puDstHi->au32[i - iHalf] = (puMskHi->au32[i - iHalf] & RT_BIT(31)) ? puSrc->au32[i] : 0;
        }

        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }
    else
    {
        puDstLo->au64[0] = 0;
        puDstLo->au64[1] = 0;
        puDstHi->au64[0] = 0;
        puDstHi->au64[1] = 0;
    }

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Worker for 'VMASKMOVPS / VPMASKMOVD' 128-bit 32-bit-masked store.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr         The current instruction length.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffDst     The destination memory address.
 * @param   iXRegMsk        The mask XMM register index.
 * @param   iXRegSrc        The source XMM register index.
 */
static VBOXSTRICTRC iemCImpl_maskmov_store_u128_32_worker(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iEffSeg, RTGCPTR GCPtrEffDst, uint8_t iXRegMsk, uint8_t iXRegSrc)
{
    uint32_t fAccessed = 0;

     PRTUINT128U puDst;
    PCRTUINT128U puMsk = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegMsk];
    PCRTUINT128U puSrc = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegSrc];

    for (uint32_t i = 0; i < RT_ELEMENTS(puMsk->au32); i++)
    {
        fAccessed |= puMsk->au32[i];
    }

    if (fAccessed & RT_BIT(31)) {
        /*
         * Access the destination memory.
         */
        uint8_t bUnmapInfo;
        void   *pvMemDst;
        VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvMemDst, &bUnmapInfo, sizeof(*puDst),
                                          iEffSeg, GCPtrEffDst, IEM_ACCESS_DATA_RW, 0);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        puDst =  (PRTUINT128U)pvMemDst;

        for (uint32_t i = 0; i < RT_ELEMENTS(puDst->au32); i++)
        {
            if (puMsk->au32[i] & RT_BIT(31))
                puDst->au32[i] = puSrc->au32[i];
        }

        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}



/**
 * Worker for 'VMASKMOVPS / VPMASKMOVD' 256-bit 32-bit-masked store.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr         The current instruction length.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffDst     The destination memory address.
 * @param   iYRegMsk        The mask YMM register index.
 * @param   iYRegSrc        The source YMM register index.
 */
static VBOXSTRICTRC iemCImpl_maskmov_store_u256_32_worker(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iEffSeg, RTGCPTR GCPtrEffDst, uint8_t iYRegMsk, uint8_t iYRegSrc)
{
    uint32_t fAccessed = 0;

     PRTUINT256U puDst;
    PCRTUINT128U puMskLo = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegMsk];
    PCRTUINT128U puMskHi = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegMsk];
    PCRTUINT128U puSrcLo = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrc];
    PCRTUINT128U puSrcHi = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegSrc];

    for (uint32_t i = 0; i < RT_ELEMENTS(puMskLo->au32); i++)
    {
        fAccessed |= puMskLo->au32[i] | puMskHi->au32[i];
    }

    if (fAccessed & RT_BIT(31)) {
        /*
         * Access the destination memory.
         */
        uint8_t bUnmapInfo;
        void   *pvMemDst;
        VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvMemDst, &bUnmapInfo, sizeof(*puDst),
                                          iEffSeg, GCPtrEffDst, IEM_ACCESS_DATA_RW, 0);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        puDst   =  (PRTUINT256U)pvMemDst;

        uint8_t const iHalf = RT_ELEMENTS(puDst->au32) / 2;

        for (uint32_t i = 0; i < iHalf; i++)
        {
            if (puMskLo->au32[i] & RT_BIT(31))
                puDst->au32[i] = puSrcLo->au32[i];
        }
        for (uint32_t i = iHalf; i < RT_ELEMENTS(puDst->au32); i++)
        {
            if (puMskHi->au32[i - iHalf] & RT_BIT(31))
                puDst->au32[i] = puSrcHi->au32[i - iHalf];
        }

        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Worker for 'VMASKMOVPD / VPMASKMOVQ' 128-bit 64-bit-masked load.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr         The current instruction length.
 * @param   iXRegDst        The destination XMM register index.
 * @param   iXRegMsk        The mask XMM register index.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffSrc     The source memory address.
 */
static VBOXSTRICTRC iemCImpl_maskmov_load_u128_64_worker(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iXRegDst, uint8_t iXRegMsk, uint8_t iEffSeg, RTGCPTR GCPtrEffSrc)
{
    uint64_t fAccessed = 0;

     PRTUINT128U puDst =  (PRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDst];
    PCRTUINT128U puMsk = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegMsk];
    PCRTUINT128U puSrc;

    for (uint32_t i = 0; i < RT_ELEMENTS(puMsk->au64); i++)
    {
        fAccessed |= puMsk->au64[i];
    }

    if (fAccessed & RT_BIT_64(63)) {
        /*
         * Access the source memory.
         */
        uint8_t bUnmapInfo;
        void   *pvMemSrc;
        VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvMemSrc, &bUnmapInfo, sizeof(*puSrc),
                                          iEffSeg, GCPtrEffSrc, IEM_ACCESS_DATA_R, 0);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        puSrc = (PCRTUINT128U)pvMemSrc;

        for (uint32_t i = 0; i < RT_ELEMENTS(puSrc->au64); i++)
        {
            puDst->au64[i] = (puMsk->au64[i] & RT_BIT_64(63)) ? puSrc->au64[i] : 0;
        }
        pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iXRegDst].au64[0] = 0;
        pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iXRegDst].au64[1] = 0;

        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
        pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iXRegDst].au64[0] = 0;
        pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iXRegDst].au64[1] = 0;
    }

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}



/**
 * Worker for 'VMASKMOVPD / VPMASKMOVQ' 256-bit 64-bit-masked load.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr         The current instruction length.
 * @param   iYRegDst        The destination YMM register index.
 * @param   iYRegMsk        The mask YMM register index.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffSrc     The source memory address.
 */
static VBOXSTRICTRC iemCImpl_maskmov_load_u256_64_worker(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iYRegDst, uint8_t iYRegMsk, uint8_t iEffSeg, RTGCPTR GCPtrEffSrc)
{
    uint64_t fAccessed = 0;

     PRTUINT128U puDstLo =  (PRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDst];
     PRTUINT128U puDstHi =  (PRTUINT128U)&pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDst];
    PCRTUINT128U puMskLo = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegMsk];
    PCRTUINT128U puMskHi = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegMsk];
    PCRTUINT256U puSrc;

    for (uint32_t i = 0; i < RT_ELEMENTS(puMskLo->au64); i++)
    {
        fAccessed |= puMskLo->au64[i] | puMskHi->au64[i];
    }

    if (fAccessed & RT_BIT_64(63)) {
        /*
         * Access the source memory.
         */
        uint8_t bUnmapInfo;
        void   *pvMemSrc;
        VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvMemSrc, &bUnmapInfo, sizeof(*puSrc),
                                          iEffSeg, GCPtrEffSrc, IEM_ACCESS_DATA_R, 0);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        puSrc   = (PCRTUINT256U)pvMemSrc;

        uint8_t const iHalf = RT_ELEMENTS(puSrc->au64) / 2;

        for (uint32_t i = 0; i < iHalf; i++)
        {
            puDstLo->au64[i] = (puMskLo->au64[i] & RT_BIT_64(63)) ? puSrc->au64[i] : 0;
        }
        for (uint32_t i = iHalf; i < RT_ELEMENTS(puSrc->au64); i++)
        {
            puDstHi->au64[i - iHalf] = (puMskHi->au64[i - iHalf] & RT_BIT_64(63)) ? puSrc->au64[i] : 0;
        }

        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }
    else
    {
        puDstLo->au64[0] = 0;
        puDstLo->au64[1] = 0;
        puDstHi->au64[0] = 0;
        puDstHi->au64[1] = 0;
    }

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Worker for 'VMASKMOVPD / VPMASKMOVQ' 128-bit 64-bit-masked store.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr         The current instruction length.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffDst     The destination memory address.
 * @param   iXRegMsk        The mask XMM register index.
 * @param   iXRegSrc        The source XMM register index.
 */
static VBOXSTRICTRC iemCImpl_maskmov_store_u128_64_worker(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iEffSeg, RTGCPTR GCPtrEffDst, uint8_t iXRegMsk, uint8_t iXRegSrc)
{
    uint64_t fAccessed = 0;

     PRTUINT128U puDst;
    PCRTUINT128U puMsk = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegMsk];
    PCRTUINT128U puSrc = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegSrc];

    for (uint32_t i = 0; i < RT_ELEMENTS(puMsk->au64); i++)
    {
        fAccessed |= puMsk->au64[i];
    }

    if (fAccessed & RT_BIT_64(63)) {
        /*
         * Access the destination memory.
         */
        uint8_t bUnmapInfo;
        void   *pvMemDst;
        VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvMemDst, &bUnmapInfo, sizeof(*puDst),
                                          iEffSeg, GCPtrEffDst, IEM_ACCESS_DATA_RW, 0);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        puDst =  (PRTUINT128U)pvMemDst;

        for (uint32_t i = 0; i < RT_ELEMENTS(puDst->au64); i++)
        {
            if (puMsk->au64[i] & RT_BIT_64(63))
                puDst->au64[i] = puSrc->au64[i];
        }

        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}



/**
 * Worker for 'VMASKMOVPD / VPMASKMOVQ' 256-bit 64-bit-masked store.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr         The current instruction length.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffDst     The destination memory address.
 * @param   iYRegMsk        The mask YMM register index.
 * @param   iYRegSrc        The source YMM register index.
 */
static VBOXSTRICTRC iemCImpl_maskmov_store_u256_64_worker(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iEffSeg, RTGCPTR GCPtrEffDst, uint8_t iYRegMsk, uint8_t iYRegSrc)
{
    uint64_t fAccessed = 0;

     PRTUINT256U puDst;
    PCRTUINT128U puMskLo = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegMsk];
    PCRTUINT128U puMskHi = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegMsk];
    PCRTUINT128U puSrcLo = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrc];
    PCRTUINT128U puSrcHi = (PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegSrc];

    for (uint32_t i = 0; i < RT_ELEMENTS(puMskLo->au64); i++)
    {
        fAccessed |= puMskLo->au64[i] | puMskHi->au64[i];
    }

    if (fAccessed & RT_BIT_64(63)) {
        /*
         * Access the destination memory.
         */
        uint8_t bUnmapInfo;
        void   *pvMemDst;
        VBOXSTRICTRC rcStrict = iemMemMap(pVCpu, &pvMemDst, &bUnmapInfo, sizeof(*puDst),
                                          iEffSeg, GCPtrEffDst, IEM_ACCESS_DATA_RW, 0);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        puDst   =  (PRTUINT256U)pvMemDst;

        uint8_t const iHalf = RT_ELEMENTS(puDst->au64) / 2;

        for (uint32_t i = 0; i < iHalf; i++)
        {
            if (puMskLo->au64[i] & RT_BIT_64(63))
                puDst->au64[i] = puSrcLo->au64[i];
        }
        for (uint32_t i = iHalf; i < RT_ELEMENTS(puDst->au64); i++)
        {
            if (puMskHi->au64[i - iHalf] & RT_BIT_64(63))
                puDst->au64[i] = puSrcHi->au64[i - iHalf];
        }

        rcStrict = iemMemCommitAndUnmap(pVCpu, bUnmapInfo);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;
    }

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Implements 'VMASKMOVPS' 128-bit 32-bit-masked load.
 *
 * @param   iXRegDst        The destination XMM register index.
 * @param   iXRegMsk        The mask XMM register index.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffSrc     The source memory address.
 */
IEM_CIMPL_DEF_4(iemCImpl_vmaskmovps_load_u128, uint8_t, iXRegDst, uint8_t, iXRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc)
{
    return iemCImpl_maskmov_load_u128_32_worker(pVCpu, cbInstr, iXRegDst, iXRegMsk, iEffSeg, GCPtrEffSrc);
}


/**
 * Implements 'VMASKMOVPS' 256-bit 32-bit-masked load.
 *
 * @param   iYRegDst        The destination YMM register index.
 * @param   iYRegMsk        The mask YMM register index.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffSrc     The source memory address.
 */
IEM_CIMPL_DEF_4(iemCImpl_vmaskmovps_load_u256, uint8_t, iYRegDst, uint8_t, iYRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc)
{
    return iemCImpl_maskmov_load_u256_32_worker(pVCpu, cbInstr, iYRegDst, iYRegMsk, iEffSeg, GCPtrEffSrc);
}


/**
 * Implements 'VMASKMOVPS' 128-bit 32-bit-masked store.
 *
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffDst     The destination memory address.
 * @param   iXRegMsk        The mask XMM register index.
 * @param   iXRegSrc        The source XMM register index.
 */
IEM_CIMPL_DEF_4(iemCImpl_vmaskmovps_store_u128, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iXRegMsk, uint8_t, iXRegSrc)
{
    return iemCImpl_maskmov_store_u128_32_worker(pVCpu, cbInstr, iEffSeg, GCPtrEffDst, iXRegMsk, iXRegSrc);
}


/**
 * Implements 'VMASKMOVPS' 256-bit 32-bit-masked store.
 *
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffDst     The destination memory address.
 * @param   iYRegMsk        The mask YMM register index.
 * @param   iYRegSrc        The source YMM register index.
 */
IEM_CIMPL_DEF_4(iemCImpl_vmaskmovps_store_u256, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iYRegMsk, uint8_t, iYRegSrc)
{
    return iemCImpl_maskmov_store_u256_32_worker(pVCpu, cbInstr, iEffSeg, GCPtrEffDst, iYRegMsk, iYRegSrc);
}


/**
 * Implements 'VPMASKMOVD' 128-bit 32-bit-masked load.
 *
 * @param   iXRegDst        The destination XMM register index.
 * @param   iXRegMsk        The mask XMM register index.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffSrc     The source memory address.
 */
IEM_CIMPL_DEF_4(iemCImpl_vpmaskmovd_load_u128, uint8_t, iXRegDst, uint8_t, iXRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc)
{
    return iemCImpl_maskmov_load_u128_32_worker(pVCpu, cbInstr, iXRegDst, iXRegMsk, iEffSeg, GCPtrEffSrc);
}


/**
 * Implements 'VPMASKMOVD' 256-bit 32-bit-masked load.
 *
 * @param   iYRegDst        The destination YMM register index.
 * @param   iYRegMsk        The mask YMM register index.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffSrc     The source memory address.
 */
IEM_CIMPL_DEF_4(iemCImpl_vpmaskmovd_load_u256, uint8_t, iYRegDst, uint8_t, iYRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc)
{
    return iemCImpl_maskmov_load_u256_32_worker(pVCpu, cbInstr, iYRegDst, iYRegMsk, iEffSeg, GCPtrEffSrc);
}


/**
 * Implements 'VPMASKMOVD' 128-bit 32-bit-masked store.
 *
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffDst     The destination memory address.
 * @param   iXRegMsk        The mask XMM register index.
 * @param   iXRegSrc        The source XMM register index.
 */
IEM_CIMPL_DEF_4(iemCImpl_vpmaskmovd_store_u128, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iXRegMsk, uint8_t, iXRegSrc)
{
    return iemCImpl_maskmov_store_u128_32_worker(pVCpu, cbInstr, iEffSeg, GCPtrEffDst, iXRegMsk, iXRegSrc);
}


/**
 * Implements 'VPMASKMOVD' 256-bit 32-bit-masked store.
 *
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffDst     The destination memory address.
 * @param   iYRegMsk        The mask YMM register index.
 * @param   iYRegSrc        The source YMM register index.
 */
IEM_CIMPL_DEF_4(iemCImpl_vpmaskmovd_store_u256, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iYRegMsk, uint8_t, iYRegSrc)
{
    return iemCImpl_maskmov_store_u256_32_worker(pVCpu, cbInstr, iEffSeg, GCPtrEffDst, iYRegMsk, iYRegSrc);
}


/**
 * Implements 'VMASKMOVPD' 128-bit 64-bit-masked load.
 *
 * @param   iXRegDst        The destination XMM register index.
 * @param   iXRegMsk        The mask XMM register index.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffSrc     The source memory address.
 */
IEM_CIMPL_DEF_4(iemCImpl_vmaskmovpd_load_u128, uint8_t, iXRegDst, uint8_t, iXRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc)
{
    return iemCImpl_maskmov_load_u128_64_worker(pVCpu, cbInstr, iXRegDst, iXRegMsk, iEffSeg, GCPtrEffSrc);
}


/**
 * Implements 'VMASKMOVPD' 256-bit 64-bit-masked load.
 *
 * @param   iYRegDst        The destination YMM register index.
 * @param   iYRegMsk        The mask YMM register index.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffSrc     The source memory address.
 */
IEM_CIMPL_DEF_4(iemCImpl_vmaskmovpd_load_u256, uint8_t, iYRegDst, uint8_t, iYRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc)
{
    return iemCImpl_maskmov_load_u256_64_worker(pVCpu, cbInstr, iYRegDst, iYRegMsk, iEffSeg, GCPtrEffSrc);
}


/**
 * Implements 'VMASKMOVPD' 128-bit 64-bit-masked store.
 *
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffDst     The destination memory address.
 * @param   iXRegMsk        The mask XMM register index.
 * @param   iXRegSrc        The source XMM register index.
 */
IEM_CIMPL_DEF_4(iemCImpl_vmaskmovpd_store_u128, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iXRegMsk, uint8_t, iXRegSrc)
{
    return iemCImpl_maskmov_store_u128_64_worker(pVCpu, cbInstr, iEffSeg, GCPtrEffDst, iXRegMsk, iXRegSrc);
}


/**
 * Implements 'VMASKMOVPD' 256-bit 64-bit-masked store.
 *
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffDst     The destination memory address.
 * @param   iYRegMsk        The mask YMM register index.
 * @param   iYRegSrc        The source YMM register index.
 */
IEM_CIMPL_DEF_4(iemCImpl_vmaskmovpd_store_u256, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iYRegMsk, uint8_t, iYRegSrc)
{
    return iemCImpl_maskmov_store_u256_64_worker(pVCpu, cbInstr, iEffSeg, GCPtrEffDst, iYRegMsk, iYRegSrc);
}


/**
 * Implements 'VPMASKMOVQ' 128-bit 64-bit-masked load.
 *
 * @param   iXRegDst        The destination XMM register index.
 * @param   iXRegMsk        The mask XMM register index.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffSrc     The source memory address.
 */
IEM_CIMPL_DEF_4(iemCImpl_vpmaskmovq_load_u128, uint8_t, iXRegDst, uint8_t, iXRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc)
{
    return iemCImpl_maskmov_load_u128_64_worker(pVCpu, cbInstr, iXRegDst, iXRegMsk, iEffSeg, GCPtrEffSrc);
}


/**
 * Implements 'VPMASKMOVQ' 256-bit 64-bit-masked load.
 *
 * @param   iYRegDst        The destination YMM register index.
 * @param   iYRegMsk        The mask YMM register index.
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffSrc     The source memory address.
 */
IEM_CIMPL_DEF_4(iemCImpl_vpmaskmovq_load_u256, uint8_t, iYRegDst, uint8_t, iYRegMsk, uint8_t, iEffSeg, RTGCPTR, GCPtrEffSrc)
{
    return iemCImpl_maskmov_load_u256_64_worker(pVCpu, cbInstr, iYRegDst, iYRegMsk, iEffSeg, GCPtrEffSrc);
}


/**
 * Implements 'VPMASKMOVQ' 128-bit 64-bit-masked store.
 *
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffDst     The destination memory address.
 * @param   iXRegMsk        The mask XMM register index.
 * @param   iXRegSrc        The source XMM register index.
 */
IEM_CIMPL_DEF_4(iemCImpl_vpmaskmovq_store_u128, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iXRegMsk, uint8_t, iXRegSrc)
{
    return iemCImpl_maskmov_store_u128_64_worker(pVCpu, cbInstr, iEffSeg, GCPtrEffDst, iXRegMsk, iXRegSrc);
}


/**
 * Implements 'VPMASKMOVQ' 256-bit 64-bit-masked store.
 *
 * @param   iEffSeg         The effective segment.
 * @param   GCPtrEffDst     The destination memory address.
 * @param   iYRegMsk        The mask YMM register index.
 * @param   iYRegSrc        The source YMM register index.
 */
IEM_CIMPL_DEF_4(iemCImpl_vpmaskmovq_store_u256, uint8_t, iEffSeg, RTGCPTR, GCPtrEffDst, uint8_t, iYRegMsk, uint8_t, iYRegSrc)
{
    return iemCImpl_maskmov_store_u256_64_worker(pVCpu, cbInstr, iEffSeg, GCPtrEffDst, iYRegMsk, iYRegSrc);
}


/**
 * Worker for 'VGATHERcxx' / 'VPGATHERxx' masked loads.
 *
 * @param   u32PackedArgs   Arguments packed to the tune of IEMGATHERARGS.
 * @param   u32Disp         The address displacement for the indices.
 */
IEM_CIMPL_DEF_2(iemCImpl_vpgather_worker_xx, uint32_t, u32PackedArgs, uint32_t, u32Disp)
{
    IEMGATHERARGS const PackedArgs = { u32PackedArgs };
    int32_t const       offDisp    = (int32_t)u32Disp;

    if (PackedArgs.s.iYRegDst == PackedArgs.s.iYRegIdc ||
        PackedArgs.s.iYRegIdc == PackedArgs.s.iYRegMsk ||
        PackedArgs.s.iYRegDst == PackedArgs.s.iYRegMsk)   return iemRaiseUndefinedOpcode(pVCpu);

    Assert(PackedArgs.s.enmEffOpSize <= IEMMODE_64BIT);
    Assert(PackedArgs.s.enmEffAddrMode <= IEMMODE_64BIT);

    uint32_t const cbMaxWidth = PackedArgs.s.fVex256  ? 32 : 16; /* Width of widest XMM / YMM register we will use: 32 or 16 */
    uint32_t const cbIdxWidth = PackedArgs.s.fIdxQword ? 8 : 4;  /* Width of one index: 4-byte dword or 8-byte qword */
    uint32_t const cbValWidth = PackedArgs.s.fValQword ? 8 : 4;  /* Width of one value: 4-byte dword or 8-byte qword */
    uint32_t const cMasks     = cbMaxWidth / cbValWidth;         /* Count of masks:   8 or 4 or 2 */
    uint32_t const cIndices   = cbMaxWidth / cbIdxWidth;         /* Count of indices: 8 or 4 or 2 */
    uint32_t const cValues    = RT_MIN(cMasks, cIndices);        /* Count of values to gather: 8 or 4 or 2 */
    Assert(cValues == 2 || cValues == 4 || cValues == 8);
    uint32_t const cbDstWidth = cValues * cbValWidth;   /* Width of the destination & mask XMM / YMM registers: 32 or 16 or 8 */
    Assert(cbDstWidth == 8 || cbDstWidth == 16 || cbDstWidth == 32);

    /*
     * Get the base pointer.
     */
    uint64_t u64Base = iemGRegFetchU64(pVCpu, PackedArgs.s.iGRegBase);
    if (PackedArgs.s.enmEffAddrMode != IEMMODE_64BIT)
        u64Base &= (PackedArgs.s.enmEffAddrMode == IEMMODE_16BIT ? UINT16_MAX : UINT32_MAX);

    PRTUINT128U const apuDst[2] =
    {
        &pVCpu->cpum.GstCtx.XState.x87.aXMM[PackedArgs.s.iYRegDst].uXmm,
        &pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[PackedArgs.s.iYRegDst].uXmm
    };
    PCRTUINT128U const apuIdc[2] =
    {
        &pVCpu->cpum.GstCtx.XState.x87.aXMM[PackedArgs.s.iYRegIdc].uXmm,
        &pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[PackedArgs.s.iYRegIdc].uXmm
    };
    PRTUINT128U const apuMsk[2] =
    {
        &pVCpu->cpum.GstCtx.XState.x87.aXMM[PackedArgs.s.iYRegMsk].uXmm,
        &pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[PackedArgs.s.iYRegMsk].uXmm
    };

    /*
     * Convert the masks to all-0s or all-1s, writing back to the mask
     * register so it will have the correct value if subsequent memory
     * accesses fault.  Note that cMasks can be larger than cValues, in
     * the Qword-index, Dword-value instructions `vgatherqps' and
     * `vpgatherqd'.  Updating the masks for as many masks as *would*
     * have been used if the destination register were wide enough --
     * is the observed behavior of a Core i7-10700.
     */
    if (!PackedArgs.s.fValQword)
        for (uint32_t i = 0; i < cMasks; i++)
            apuMsk[(i >> 2) & 1]->ai32[i & 3] >>= 31; /* Use arithmetic shift right (SAR/ASR) */
    else
        for (uint32_t i = 0; i < cMasks; i++)
            apuMsk[(i >> 1) & 1]->ai64[i & 1] >>= 63; /* Use arithmetic shift right (SAR/ASR) */

    /*
     * Zero upper bits of mask if VEX128.
     */
    if (!PackedArgs.s.fVex256)
    {
        apuMsk[1]->au64[0] = 0;
        apuMsk[1]->au64[1] = 0;
    }

    /*
     * Gather the individual values, as masked.
     */
    for (uint32_t i = 0; i < cValues; i++)
    {
        /*
         * Consult the mask determined above.
         */
        if (  !PackedArgs.s.fValQword
            ? apuMsk[(i >> 2) & 1]->au32[i & 3] != 0
            : apuMsk[(i >> 1) & 1]->au64[i & 1] != 0)
        {
            /*
             * Get the index, scale it, add scaled index + offset to the base pointer.
             */
            int64_t offIndex;
            if (!PackedArgs.s.fIdxQword)
                offIndex = apuIdc[(i >> 2) & 1]->ai32[i & 3];
            else
                offIndex = apuIdc[(i >> 1) & 1]->ai64[i & 1];
            offIndex <<= PackedArgs.s.iScale;
            offIndex  += offDisp;

            uint64_t u64Addr = u64Base + offIndex;
            if (PackedArgs.s.enmEffAddrMode != IEMMODE_64BIT)
                u64Addr &= UINT32_MAX;

            /*
             * Gather it -- fetch this gather-item from guest memory.
             */
            VBOXSTRICTRC rcStrict;
            if (!PackedArgs.s.fValQword)
                rcStrict = iemMemFetchDataU32NoAc(pVCpu, &apuDst[(i >> 2) & 1]->au32[i & 3], PackedArgs.s.iEffSeg, u64Addr);
            else
                rcStrict = iemMemFetchDataU64NoAc(pVCpu, &apuDst[(i >> 1) & 1]->au64[i & 1], PackedArgs.s.iEffSeg, u64Addr);
            if (rcStrict != VINF_SUCCESS)
                return rcStrict;

            /*
             * Now that we *didn't* fault, write all-0s to that part of the mask register.
             */
            if (!PackedArgs.s.fValQword)
                apuMsk[(i >> 2) & 1]->au32[i & 3] = 0;
            else
                apuMsk[(i >> 1) & 1]->au64[i & 1] = 0;
            /** @todo How is data breakpoints handled? The intel docs kind of hints they
             *        may be raised here... */
        }
    }

    /*
     * Zero upper bits of destination and mask.
     */
    if (cbDstWidth != 32)
    {
        apuDst[1]->au64[0] = 0;
        apuDst[1]->au64[1] = 0;
        apuMsk[1]->au64[0] = 0;
        apuMsk[1]->au64[1] = 0;
        if (cbDstWidth == 8)
        {
            apuDst[0]->au64[1] = 0;
            apuMsk[0]->au64[1] = 0;
        }
    }

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}

/** @} */

