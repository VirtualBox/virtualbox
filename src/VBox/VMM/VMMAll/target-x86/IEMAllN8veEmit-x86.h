/* $Id: IEMAllN8veEmit-x86.h 110154 2025-07-08 14:57:17Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Native Recompiler, x86 Target - Code Emitters.
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

#ifndef VMM_INCLUDED_SRC_VMMAll_target_x86_IEMAllN8veEmit_x86_h
#define VMM_INCLUDED_SRC_VMMAll_target_x86_IEMAllN8veEmit_x86_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#ifdef RT_ARCH_AMD64

/**
 * Emits an ModR/M instruction with one opcode byte and only register operands.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitAmd64OneByteModRmInstrRREx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t bOpcode8, uint8_t bOpcodeOther,
                                        uint8_t cOpBits, uint8_t idxRegReg, uint8_t idxRegRm)
{
    Assert(idxRegReg < 16); Assert(idxRegRm < 16);
    switch (cOpBits)
    {
        case 16:
            pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
            RT_FALL_THRU();
        case 32:
            if (idxRegReg >= 8 || idxRegRm >= 8)
                pCodeBuf[off++] = (idxRegReg >= 8 ? X86_OP_REX_R : 0) | (idxRegRm >= 8 ? X86_OP_REX_B : 0);
            pCodeBuf[off++] = bOpcodeOther;
            break;

        default: AssertFailed(); RT_FALL_THRU();
        case 64:
            pCodeBuf[off++] = X86_OP_REX_W | (idxRegReg >= 8 ? X86_OP_REX_R : 0) | (idxRegRm >= 8 ? X86_OP_REX_B : 0);
            pCodeBuf[off++] = bOpcodeOther;
            break;

        case 8:
            if (idxRegReg >= 8 || idxRegRm >= 8)
                pCodeBuf[off++] = (idxRegReg >= 8 ? X86_OP_REX_R : 0) | (idxRegRm >= 8 ? X86_OP_REX_B : 0);
            else if (idxRegReg >= 4 || idxRegRm >= 4)
                pCodeBuf[off++] = X86_OP_REX;
            pCodeBuf[off++] = bOpcode8;
            break;
    }
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxRegReg & 7, idxRegRm & 7);
    return off;
}


/**
 * Emits an ModR/M instruction with two opcode bytes and only register operands.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitAmd64TwoByteModRmInstrRREx(PIEMNATIVEINSTR pCodeBuf, uint32_t off,
                                        uint8_t bOpcode0, uint8_t bOpcode8, uint8_t bOpcodeOther,
                                        uint8_t cOpBits, uint8_t idxRegReg, uint8_t idxRegRm)
{
    Assert(idxRegReg < 16); Assert(idxRegRm < 16);
    switch (cOpBits)
    {
        case 16:
            pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
            RT_FALL_THRU();
        case 32:
            if (idxRegReg >= 8 || idxRegRm >= 8)
                pCodeBuf[off++] = (idxRegReg >= 8 ? X86_OP_REX_R : 0) | (idxRegRm >= 8 ? X86_OP_REX_B : 0);
            pCodeBuf[off++] = bOpcode0;
            pCodeBuf[off++] = bOpcodeOther;
            break;

        default: AssertFailed(); RT_FALL_THRU();
        case 64:
            pCodeBuf[off++] = X86_OP_REX_W | (idxRegReg >= 8 ? X86_OP_REX_R : 0) | (idxRegRm >= 8 ? X86_OP_REX_B : 0);
            pCodeBuf[off++] = bOpcode0;
            pCodeBuf[off++] = bOpcodeOther;
            break;

        case 8:
            if (idxRegReg >= 8 || idxRegRm >= 8)
                pCodeBuf[off++] = (idxRegReg >= 8 ? X86_OP_REX_R : 0) | (idxRegRm >= 8 ? X86_OP_REX_B : 0);
            else if (idxRegReg >= 4 || idxRegRm >= 4)
                pCodeBuf[off++] = X86_OP_REX;
            pCodeBuf[off++] = bOpcode0;
            pCodeBuf[off++] = bOpcode8;
            break;
    }
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxRegReg & 7, idxRegRm & 7);
    return off;
}


/**
 * Emits one of three opcodes with an immediate.
 *
 * These are expected to be a /idxRegReg form.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitAmd64OneByteModRmInstrRIEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t bOpcode8, uint8_t bOpcodeOtherImm8,
                                        uint8_t bOpcodeOther, uint8_t cOpBits, uint8_t cImmBits, uint8_t idxRegReg,
                                        uint8_t idxRegRm, uint64_t uImmOp)
{
    Assert(idxRegReg < 8); Assert(idxRegRm < 16);
    if (   cImmBits == 8
        || (uImmOp <= (uint64_t)0x7f && bOpcodeOtherImm8 != 0xcc))
    {
        switch (cOpBits)
        {
            case 16:
                pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
                RT_FALL_THRU();
            case 32:
                if (idxRegRm >= 8)
                    pCodeBuf[off++] = X86_OP_REX_B;
                pCodeBuf[off++] = bOpcodeOtherImm8; Assert(bOpcodeOtherImm8 != 0xcc);
                break;

            default: AssertFailed(); RT_FALL_THRU();
            case 64:
                pCodeBuf[off++] = X86_OP_REX_W | (idxRegRm >= 8 ? X86_OP_REX_B : 0);
                pCodeBuf[off++] = bOpcodeOtherImm8; Assert(bOpcodeOtherImm8 != 0xcc);
                break;

            case 8:
                if (idxRegRm >= 8)
                    pCodeBuf[off++] = X86_OP_REX_B;
                else if (idxRegRm >= 4)
                    pCodeBuf[off++] = X86_OP_REX;
                pCodeBuf[off++] = bOpcode8; Assert(bOpcode8 != 0xcc);
                break;
        }
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxRegReg, idxRegRm & 7);
        pCodeBuf[off++] = (uint8_t)uImmOp;
    }
    else
    {
        switch (cOpBits)
        {
            case 32:
                if (idxRegRm >= 8)
                    pCodeBuf[off++] = X86_OP_REX_B;
                break;

            default: AssertFailed(); RT_FALL_THRU();
            case 64:
                pCodeBuf[off++] = X86_OP_REX_W | (idxRegRm >= 8 ? X86_OP_REX_B : 0);
                break;

            case 16:
                pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
                if (idxRegRm >= 8)
                    pCodeBuf[off++] = X86_OP_REX_B;
                pCodeBuf[off++] = bOpcodeOther;
                pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxRegReg, idxRegRm & 7);
                pCodeBuf[off++] = RT_BYTE1(uImmOp);
                pCodeBuf[off++] = RT_BYTE2(uImmOp);
                Assert(cImmBits == 16);
                return off;
        }
        pCodeBuf[off++] = bOpcodeOther;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxRegReg, idxRegRm & 7);
        pCodeBuf[off++] = RT_BYTE1(uImmOp);
        pCodeBuf[off++] = RT_BYTE2(uImmOp);
        pCodeBuf[off++] = RT_BYTE3(uImmOp);
        pCodeBuf[off++] = RT_BYTE4(uImmOp);
        Assert(cImmBits == 32);
    }
    return off;
}

#endif /* RT_ARCH_AMD64 */



/*********************************************************************************************************************************
*   Guest Register Load & Store Helpers                                                                                          *
*********************************************************************************************************************************/


/**
 * Alternative to iemNativeEmitLoadGprWithGstShadowRegEx() and
 * iemNativeEmitLoadGprWithGstShadowReg() which should be more efficient as it
 * lets the compiler do the equivalent of the g_aGstShadowInfo lookup.
 *
 * @note This does not mark @a idxHstReg as having a shadow copy of @a a_enmGstReg,
 *       that is something the caller needs to do if applicable.
 */
template<IEMNATIVEGSTREG const a_enmGstReg>
DECL_INLINE_THROW(uint32_t) iemNativeEmitLoadGprWithGstRegExT(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t idxHstReg)
{
    /* 64-bit registers: */
    if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_Pc)
        return iemNativeEmitLoadGprFromVCpuU64Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
    else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_Rsp)
        return iemNativeEmitLoadGprFromVCpuU64Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rsp));
    else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_CsBase)
        return iemNativeEmitLoadGprFromVCpuU64Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.cs.u64Base));
    //else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_Cr0)
    //    return iemNativeEmitLoadGprFromVCpuU64Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.cr0));
    //else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_Cr4)
    //    return iemNativeEmitLoadGprFromVCpuU64Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.cr4));
    //else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_Xcr0)
    //    return iemNativeEmitLoadGprFromVCpuU64Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.aXcr[0]));

    /* 32-bit registers:   */
    else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_EFlags)
        return iemNativeEmitLoadGprFromVCpuU32Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.eflags));
    else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_MxCsr)
        return iemNativeEmitLoadGprFromVCpuU32Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.XState.x87.MXCSR));

    /* 16-bit registers */
    else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_FpuFcw)
        return iemNativeEmitLoadGprFromVCpuU16Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.XState.x87.FCW));
    else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_FpuFsw)
        return iemNativeEmitLoadGprFromVCpuU16Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.XState.x87.FSW));
#if RT_CPLUSPLUS_PREREQ(201700) && !defined(__clang_major__)
    else
    {
        AssertCompile(false);
        return off;
    }
#endif
}


/** See iemNativeEmitLoadGprWithGstRegExT(). */
template<IEMNATIVEGSTREG const a_enmGstReg>
DECL_INLINE_THROW(uint32_t) iemNativeEmitLoadGprWithGstRegT(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxHstReg)
{
#ifdef RT_ARCH_AMD64
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 16);
#elif defined(RT_ARCH_ARM64)
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 6);
#else
# error "port me"
#endif
    off = iemNativeEmitLoadGprWithGstRegExT<a_enmGstReg>(pCodeBuf, off, idxHstReg);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Store companion to iemNativeEmitLoadGprWithGstRegExT().
 */
template<IEMNATIVEGSTREG const a_enmGstReg>
DECL_INLINE_THROW(uint32_t) iemNativeEmitStoreGprToGstRegExT(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t idxHstReg,
                                                             uint8_t idxTmpReg = IEMNATIVE_REG_FIXED_TMP0)
{
    /* 64-bit registers: */
    if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_Pc)
        return iemNativeEmitStoreGprToVCpuU64Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip), idxTmpReg);
    else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_Rsp)
        return iemNativeEmitStoreGprToVCpuU64Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rsp), idxTmpReg);
    //else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_Cr0)
    //    return iemNativeEmitStoreGprToVCpuU64Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.cr0), idxTmpReg);
    //else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_Cr4)
    //    return iemNativeEmitStoreGprToVCpuU64Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.cr4), idxTmpReg);
    //else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_Xcr0)
    //    return iemNativeEmitStoreGprToVCpuU64Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.aXcr[0]), idxTmpReg);
    /* 32-bit registers:   */
    else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_EFlags)
        return iemNativeEmitStoreGprToVCpuU32Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.eflags), idxTmpReg);
    else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_MxCsr)
        return iemNativeEmitStoreGprToVCpuU32Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.XState.x87.MXCSR), idxTmpReg);
    /* 16-bit registers */
    else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_FpuFcw)
        return iemNativeEmitStoreGprToVCpuU16Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.XState.x87.FCW), idxTmpReg);
    else if RT_CONSTEXPR_IF(a_enmGstReg == kIemNativeGstReg_FpuFsw)
        return iemNativeEmitStoreGprToVCpuU16Ex(pCodeBuf, off, idxHstReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.XState.x87.FSW), idxTmpReg);
#if RT_CPLUSPLUS_PREREQ(201700) && !defined(__clang_major__)
    else
    {
        AssertCompile(false);
        return off;
    }
#endif
}


/** See iemNativeEmitLoadGprWithGstRegExT(). */
template<IEMNATIVEGSTREG const a_enmGstReg>
DECL_INLINE_THROW(uint32_t) iemNativeEmitStoreGprToGstRegT(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxHstReg)
{
#ifdef RT_ARCH_AMD64
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 16);
#elif defined(RT_ARCH_ARM64)
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 6);
#else
# error "port me"
#endif
    off = iemNativeEmitStoreGprToGstRegExT<a_enmGstReg>(pCodeBuf, off, idxHstReg);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}



/*********************************************************************************************************************************
*   EFLAGS                                                                                                                       *
*********************************************************************************************************************************/

#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING

/** @def IEMNATIVE_POSTPONING_REG_MASK
 * Register suitable for keeping the inputs or result for a postponed EFLAGS
 * calculation.
 *
 * We use non-volatile register here so we don't have to save & restore them
 * accross callouts (i.e. TLB loads).
 *
 * @note  On x86 we cannot use RDI and RSI because these are used by the
 *        opcode checking code.  The usual joy of the x86 instruction set.
 */
# ifdef RT_ARCH_AMD64
#  define IEMNATIVE_POSTPONING_REG_MASK \
    (IEMNATIVE_CALL_NONVOLATILE_GREG_MASK & ~(RT_BIT_32(X86_GREG_xDI) | RT_BIT_32(X86_GREG_xSI)))
# else
#  define IEMNATIVE_POSTPONING_REG_MASK     IEMNATIVE_CALL_NONVOLATILE_GREG_MASK
# endif

/**
 * This is normally invoked via IEMNATIVE_CLEAR_POSTPONED_EFLAGS().
 */
template<uint32_t const a_fEflClobbered>
DECL_FORCE_INLINE(void) iemNativeClearPostponedEFlags(PIEMRECOMPILERSTATE pReNative)
{
    AssertCompile(!(a_fEflClobbered & ~X86_EFL_STATUS_BITS));
    uint32_t fEFlags = pReNative->PostponedEfl.fEFlags;
    if (fEFlags)
    {
        if RT_CONSTEXPR_IF(a_fEflClobbered != X86_EFL_STATUS_BITS)
        {
            fEFlags &= ~a_fEflClobbered;
            if (!fEFlags)
            { /* likely */ }
            else
            {
                Log5(("EFLAGS: Clobbering %#x: %#x -> %#x (op=%d bits=%u) - iemNativeClearPostponedEFlags\n", a_fEflClobbered,
                      pReNative->PostponedEfl.fEFlags, fEFlags, pReNative->PostponedEfl.enmOp, pReNative->PostponedEfl.cOpBits));
                pReNative->PostponedEfl.fEFlags = fEFlags;
                return;
            }
        }

        /* Do cleanup.  */
        Log5(("EFLAGS: Cleanup of op=%u bits=%u efl=%#x upon clobbering %#x - iemNativeClearPostponedEFlags\n",
              pReNative->PostponedEfl.enmOp, pReNative->PostponedEfl.cOpBits, pReNative->PostponedEfl.fEFlags, a_fEflClobbered));
        pReNative->PostponedEfl.fEFlags = 0;
        pReNative->PostponedEfl.enmOp   = kIemNativePostponedEflOp_Invalid;
        pReNative->PostponedEfl.cOpBits = 0;
        iemNativeRegFreeTmp(pReNative, pReNative->PostponedEfl.idxReg1);
        if (pReNative->PostponedEfl.idxReg2 != UINT8_MAX)
            iemNativeRegFreeTmp(pReNative, pReNative->PostponedEfl.idxReg2);
        pReNative->PostponedEfl.idxReg1 = UINT8_MAX;
        pReNative->PostponedEfl.idxReg2 = UINT8_MAX;
# if defined(VBOX_WITH_STATISTICS) || defined(IEMNATIVE_WITH_TB_DEBUG_INFO)
        STAM_PROFILE_ADD_PERIOD(&pReNative->pVCpu->iem.s.StatNativeEflPostponedEmits, pReNative->PostponedEfl.cEmits);
        pReNative->PostponedEfl.cEmits = 0;
# endif
    }
}

#endif /* IEMNATIVE_WITH_EFLAGS_POSTPONING */


template<bool const a_fDoOp>
DECL_INLINE_THROW(uint32_t) iemNativeEmitPostponedEFlagsCalcLogical(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t cOpBits,
                                                                    uint8_t idxRegResult, uint8_t idxRegEfl, uint8_t idxRegTmp)
{
#ifdef RT_ARCH_AMD64
    /* Do TEST idxRegResult, idxRegResult to set flags. */
    if RT_CONSTEXPR_IF(a_fDoOp)
        off = iemNativeEmitAmd64OneByteModRmInstrRREx(pCodeBuf, off, 0x84, 0x85, cOpBits, idxRegResult, idxRegResult);

    /*
     * Collect the EFLAGS status bits.
     * We know that the overflow bit will always be cleared, so LAHF can be used.
     */
    if (idxRegTmp == X86_GREG_xAX)
    {
        /* lahf ; AH = EFLAGS */
        pCodeBuf[off++] = 0x9f;
        if (idxRegEfl <= X86_GREG_xBX)
        {
            /* mov [CDB]L, AH */
            pCodeBuf[off++] = 0x88;
            pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4 /*AH*/, idxRegEfl);
        }
        else
        {
            /* mov   AL, AH */
            pCodeBuf[off++] = 0x88;
            pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4 /*AH*/, 0 /*AL*/);
            /* mov   xxL, AL */
            pCodeBuf[off++] = idxRegEfl >= 8 ? X86_OP_REX_B : X86_OP_REX;
            pCodeBuf[off++] = 0x88;
            pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0 /*AL*/, idxRegEfl & 7);
        }
    }
    else if (idxRegEfl != X86_GREG_xAX)
    {
# if 1 /* This is 1 or 4 bytes larger, but avoids the stack. */
        /* xchg rax, tmp */
        pCodeBuf[off++] = idxRegTmp < 8 ? X86_OP_REX_W : X86_OP_REX_B | X86_OP_REX_W;
        pCodeBuf[off++] = 0x90 + (idxRegTmp & 7);

        /* lahf ; AH = EFLAGS */
        pCodeBuf[off++] = 0x9f;
        if (idxRegEfl <= X86_GREG_xBX)
        {
            /* mov [CDB]L, AH */
            pCodeBuf[off++] = 0x88;
            pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4 /*AH*/, idxRegEfl);
        }
        else
        {
            /* mov   AL, AH */
            pCodeBuf[off++] = 0x88;
            pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4 /*AH*/, 0 /*AL*/);
            /* mov   xxL, AL */
            pCodeBuf[off++] = idxRegEfl >= 8 ? X86_OP_REX_B : X86_OP_REX;
            pCodeBuf[off++] = 0x88;
            pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0 /*AL*/, idxRegEfl & 7);
        }

        /* xchg rax, tmp */
        pCodeBuf[off++] = idxRegTmp < 8 ? X86_OP_REX_W : X86_OP_REX_B | X86_OP_REX_W;
        pCodeBuf[off++] = 0x90 + (idxRegTmp & 7);

# else
        /* pushf */
        pCodeBuf[off++] = 0x9c;
        /* pop  tmp */
        if (idxRegTmp >= 8)
            pCodeBuf[off++] = X86_OP_REX_B;
        pCodeBuf[off++] = 0x58 + (idxRegTmp & 7);
        /* mov   byte(efl), byte(tmp) */
        if (idxRegEfl >= 4 || idxRegTmp >= 4)
            pCodeBuf[off++] = (idxRegEfl >= 8 ? X86_OP_REX_B : X86_OP_REX)
                            | (idxRegTmp >= 8 ? X86_OP_REX_R : 0);
        pCodeBuf[off++] = 0x88;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxRegTmp & 7, idxRegEfl & 7);
# endif
    }
    else
    {
        /* xchg al, ah  */
        pCodeBuf[off++] = 0x86;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4 /*AH*/, 0 /*AL*/);
        /* lahf ; AH = EFLAGS */
        pCodeBuf[off++] = 0x9f;
        /* xchg al, ah  */
        pCodeBuf[off++] = 0x86;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4 /*AH*/, 0 /*AL*/);
    }
    /* BTR idxEfl, 11; Clear OF */
    if (idxRegEfl >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    pCodeBuf[off++] = 0xf;
    pCodeBuf[off++] = 0xba;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 6, idxRegEfl & 7);
    pCodeBuf[off++] = X86_EFL_OF_BIT;

#elif defined(RT_ARCH_ARM64)
    /*
     * Calculate flags.
     */
    /* Clear the status bits. ~0x8D5 (or ~0x8FD) can't be AND immediate, so use idxRegTmp for constant. */
    off = iemNativeEmitLoadGpr32ImmExT<~X86_EFL_STATUS_BITS>(pCodeBuf, off, idxRegTmp);
    off = iemNativeEmitAndGpr32ByGpr32Ex(pCodeBuf, off, idxRegEfl, idxRegTmp);

    /* N,Z -> SF,ZF */
    if (cOpBits < 32)
        pCodeBuf[off++] = Armv8A64MkInstrSetF8SetF16(idxRegResult, cOpBits > 8); /* sets NZ */
    else if RT_CONSTEXPR_IF(a_fDoOp)
        pCodeBuf[off++] = Armv8A64MkInstrAnds(ARMV8_A64_REG_XZR, idxRegResult, idxRegResult, cOpBits > 32 /*f64Bit*/);
    pCodeBuf[off++] = Armv8A64MkInstrMrs(idxRegTmp, ARMV8_AARCH64_SYSREG_NZCV); /* Bits: 31=N; 30=Z; 29=C; 28=V; */
    pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegTmp, idxRegTmp, 30);
    pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegEfl, idxRegTmp, X86_EFL_ZF_BIT, 2, false /*f64Bit*/);
    AssertCompile(X86_EFL_ZF_BIT + 1 == X86_EFL_SF_BIT);

    /* Calculate 8-bit parity of the result. */
    pCodeBuf[off++] = Armv8A64MkInstrEor(idxRegTmp, idxRegResult, idxRegResult, false /*f64Bit*/,
                                         4 /*offShift6*/, kArmv8A64InstrShift_Lsr);
    pCodeBuf[off++] = Armv8A64MkInstrEor(idxRegTmp, idxRegTmp,    idxRegTmp,    false /*f64Bit*/,
                                         2 /*offShift6*/, kArmv8A64InstrShift_Lsr);
    pCodeBuf[off++] = Armv8A64MkInstrEor(idxRegTmp, idxRegTmp,    idxRegTmp,    false /*f64Bit*/,
                                         1 /*offShift6*/, kArmv8A64InstrShift_Lsr);
    Assert(Armv8A64ConvertImmRImmS2Mask32(0, 0) == 1);
    pCodeBuf[off++] = Armv8A64MkInstrEorImm(idxRegTmp, idxRegTmp, 0, 0, false /*f64Bit*/);
    pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegEfl, idxRegTmp, X86_EFL_PF_BIT, 1, false /*f64Bit*/);

#else
# error "port me"
#endif
    return off;
}

#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING

template<uint32_t const a_bmInputRegs, bool const a_fTlbMiss = false>
static uint32_t iemNativeDoPostponedEFlagsInternal(PIEMRECOMPILERSTATE pReNative, uint32_t off, PIEMNATIVEINSTR pCodeBuf,
                                                   uint32_t bmExtraTlbMissRegs = 0)
{
# ifdef IEMNATIVE_WITH_TB_DEBUG_INFO
    iemNativeDbgInfoAddPostponedEFlagsCalc(pReNative, off, pReNative->PostponedEfl.enmOp, pReNative->PostponedEfl.cOpBits,
                                           pReNative->PostponedEfl.cEmits);
# endif

    /*
     * In the TB exit code path we cannot do regular register allocation.  Nor
     * can we when we're in the TLB miss code, unless we're skipping the TLB
     * lookup.  Since the latter isn't an important usecase and should get along
     * fine on just volatile registers, we do not need to do anything special
     * for it.
     *
     * So, we do our own register allocating here.  Any register goes in the TB
     * exit path, excluding a_bmInputRegs, fixed and postponed related registers.
     * In the TLB miss we can use any volatile register and temporary registers
     * allocated in the TLB state.
     *
     * Note! On x86 we prefer using RAX as the first TMP register, so we can
     *       make use of LAHF which is typically faster than PUSHF/POP.  This
     *       is why the idxRegTmp allocation is first when there is no EFLAG
     *       shadow, since RAX is represented by bit 0 in the mask.
     */
    uint32_t bmAvailableRegs;
    if RT_CONSTEXPR_IF(!a_fTlbMiss)
    {
        bmAvailableRegs = ~(a_bmInputRegs | IEMNATIVE_REG_FIXED_MASK) & IEMNATIVE_HST_GREG_MASK;
        if (pReNative->PostponedEfl.idxReg2 != UINT8_MAX)
            bmAvailableRegs &= ~(RT_BIT_32(pReNative->PostponedEfl.idxReg1) | RT_BIT_32(pReNative->PostponedEfl.idxReg2));
        else
            bmAvailableRegs &= ~RT_BIT_32(pReNative->PostponedEfl.idxReg1);
    }
    else
    {
        /* Note! a_bmInputRegs takes precedence over bmExtraTlbMissRegs. */
        bmAvailableRegs  = (IEMNATIVE_CALL_VOLATILE_GREG_MASK | bmExtraTlbMissRegs)
                         & ~(a_bmInputRegs | IEMNATIVE_REG_FIXED_MASK)
                         & IEMNATIVE_HST_GREG_MASK;
    }

    /* Use existing EFLAGS shadow if available. For the TLB-miss code path we
       need to weed out volatile registers here, as they will no longer be valid. */
    uint8_t idxRegTmp;
    uint8_t idxRegEfl = pReNative->Core.aidxGstRegShadows[kIemNativeGstReg_EFlags];
    if (   (pReNative->Core.bmGstRegShadows & RT_BIT_64(kIemNativeGstReg_EFlags))
        && (!a_fTlbMiss || !(RT_BIT_32(idxRegEfl) & IEMNATIVE_CALL_VOLATILE_GREG_MASK)))
    {
        Assert(idxRegEfl < IEMNATIVE_HST_GREG_COUNT);
        Assert(!(a_bmInputRegs & RT_BIT_32(idxRegEfl)));
        if RT_CONSTEXPR_IF(!a_fTlbMiss) Assert(bmAvailableRegs & RT_BIT_32(idxRegEfl));
        bmAvailableRegs &= ~RT_BIT_32(idxRegEfl);
# ifdef VBOX_STRICT
        off = iemNativeEmitGuestRegValueCheckEx(pReNative, pCodeBuf, off, idxRegEfl, kIemNativeGstReg_EFlags);
# endif

        idxRegTmp = ASMBitFirstSetU32(bmAvailableRegs) - 1;
        bmAvailableRegs &= ~RT_BIT_32(idxRegTmp);
    }
    else
    {
        idxRegTmp = ASMBitFirstSetU32(bmAvailableRegs) - 1; /* allocate the temp register first to prioritize EAX on x86. */
        bmAvailableRegs &= ~RT_BIT_32(idxRegTmp);

        idxRegEfl = ASMBitFirstSetU32(bmAvailableRegs) - 1;
        bmAvailableRegs &= ~RT_BIT_32(idxRegTmp);
        off = iemNativeEmitLoadGprWithGstRegExT<kIemNativeGstReg_EFlags>(pCodeBuf, off, idxRegEfl);
    }
    Assert(bmAvailableRegs != 0);

    /*
     * Do the actual EFLAGS calculation.
     */
    switch (pReNative->PostponedEfl.enmOp)
    {
        case kIemNativePostponedEflOp_Logical:
            Assert(pReNative->PostponedEfl.idxReg2 == UINT8_MAX);
            off = iemNativeEmitPostponedEFlagsCalcLogical<true>(pCodeBuf, off, pReNative->PostponedEfl.cOpBits,
                                                                pReNative->PostponedEfl.idxReg1, idxRegEfl, idxRegTmp);
            break;

        default:
            AssertFailedBreak();
    }

    /*
     * Store EFLAGS.
     */
# ifdef VBOX_STRICT
    /* check that X86_EFL_1 is set. */
    uint32_t offFixup1;
    off = iemNativeEmitTestBitInGprAndJmpToFixedIfSetEx(pCodeBuf, off, idxRegEfl, X86_EFL_1_BIT, off, &offFixup1);
    off = iemNativeEmitBrkEx(pCodeBuf, off, 0x3330);
    iemNativeFixupFixedJump(pReNative, offFixup1, off);
    /* Check that X86_EFL_RAZ_LO_MASK is zero. */
    off = iemNativeEmitTestAnyBitsInGpr32Ex(pCodeBuf, off, idxRegEfl, X86_EFL_RAZ_LO_MASK, idxRegTmp);
    uint32_t const offFixup2 = off;
    off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off, kIemNativeInstrCond_e);
    off = iemNativeEmitBrkEx(pCodeBuf, off, 0x3331);
    iemNativeFixupFixedJump(pReNative, offFixup2, off);
# endif
    off = iemNativeEmitStoreGprToVCpuU32Ex(pCodeBuf, off, idxRegEfl, RT_UOFFSETOF(VMCPU, cpum.GstCtx.eflags));
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

# if defined(VBOX_WITH_STATISTICS) || defined(IEMNATIVE_WITH_TB_DEBUG_INFO)
    pReNative->PostponedEfl.cEmits++;
# endif
    return off;
}



template<uint32_t const a_bmInputRegs>
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeDoPostponedEFlagsAtTbExit(PIEMRECOMPILERSTATE pReNative, uint32_t off)
{
    if (pReNative->PostponedEfl.fEFlags)
    {
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS);
        return iemNativeDoPostponedEFlagsInternal<a_bmInputRegs>(pReNative, off, pCodeBuf);
    }
    return off;
}


template<uint32_t const a_bmInputRegs>
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeDoPostponedEFlagsAtTbExitEx(PIEMRECOMPILERSTATE pReNative, uint32_t off, PIEMNATIVEINSTR pCodeBuf)
{
    if (pReNative->PostponedEfl.fEFlags)
        return iemNativeDoPostponedEFlagsInternal<a_bmInputRegs>(pReNative, off, pCodeBuf);
    return off;
}


template<uint32_t const a_bmInputRegs>
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeDoPostponedEFlagsAtTlbMiss(PIEMRECOMPILERSTATE pReNative, uint32_t off, const IEMNATIVEEMITTLBSTATE *pTlbState,
                                    uint32_t bmTmpRegs)
{
    if (pReNative->PostponedEfl.fEFlags)
    {
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS);
        return iemNativeDoPostponedEFlagsInternal<a_bmInputRegs, true>(pReNative, off, pCodeBuf,
                                                                       pTlbState->getRegsNotToSave() | bmTmpRegs);
    }
    return off;
}

#endif /* IEMNATIVE_WITH_EFLAGS_POSTPONING */


/**
 * This is an implementation of IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL.
 *
 * It takes liveness stuff into account.
 */
template<bool a_fNeedToSetFlags>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitEFlagsForLogical(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarEfl,
                              uint8_t cOpBits, uint8_t idxRegResult)
{
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeEflTotalLogical);
    IEMNATIVE_CLEAR_POSTPONED_EFLAGS(pReNative, X86_EFL_STATUS_BITS);
    RT_NOREF(cOpBits, idxRegResult);

#ifdef IEMNATIVE_WITH_EFLAGS_SKIPPING
    /*
     * See if we can skip this wholesale.
     */
    PCIEMLIVENESSENTRY const pLivenessEntry = &pReNative->paLivenessEntries[pReNative->idxCurCall];
    uint64_t const           fEflClobbered  = IEMLIVENESS_STATE_GET_WILL_BE_CLOBBERED_SET(pLivenessEntry)
                                            & IEMLIVENESSBIT_STATUS_EFL_MASK;
# ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    uint64_t                 fEflPostponing;
# endif
    if (   fEflClobbered == IEMLIVENESSBIT_STATUS_EFL_MASK
        && !(pReNative->fMc & IEM_MC_F_WITH_FLAGS))
    {
        STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeEflSkippedLogical);
        pReNative->fSkippingEFlags = X86_EFL_STATUS_BITS;
# ifdef IEMNATIVE_STRICT_EFLAGS_SKIPPING
        off = iemNativeEmitOrImmIntoVCpuU32(pReNative, off, X86_EFL_STATUS_BITS, RT_UOFFSETOF(VMCPU, iem.s.fSkippingEFlags));
# endif
        Log5(("EFLAGS: Skipping %#x - iemNativeEmitEFlagsForLogical\n", X86_EFL_STATUS_BITS));
        return off;
    }
# ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    if (      (  (fEflPostponing = IEMLIVENESS_STATE_GET_CAN_BE_POSTPONED_SET(pLivenessEntry) & IEMLIVENESSBIT_STATUS_EFL_MASK)
               | fEflClobbered)
           == IEMLIVENESSBIT_STATUS_EFL_MASK
        && idxRegResult != UINT8_MAX)
    {
        STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeEflPostponedLogical);
        pReNative->PostponedEfl.fEFlags  = X86_EFL_STATUS_BITS;
        pReNative->PostponedEfl.enmOp    = kIemNativePostponedEflOp_Logical;
        pReNative->PostponedEfl.cOpBits  = cOpBits;
        pReNative->PostponedEfl.idxReg1  = iemNativeRegAllocTmpExPreferNonVolatile(pReNative, &off, IEMNATIVE_POSTPONING_REG_MASK);
        /** @todo it would normally be possible to use idxRegResult, iff it is
         *        already a non-volatile register and we can be user the caller
         *        doesn't modify it.  That'll save a register move and allocation. */
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, pReNative->PostponedEfl.idxReg1, idxRegResult);
        Log5(("EFLAGS: Postponing %#x op=%u bits=%u reg1=%u - iemNativeEmitEFlagsForLogical\n", X86_EFL_STATUS_BITS,
              kIemNativePostponedEflOp_Logical, cOpBits, pReNative->PostponedEfl.idxReg1));
    }
# endif
    else
#endif
    {
        uint8_t const         idxRegEfl = iemNativeVarRegisterAcquireInited(pReNative, idxVarEfl, &off);
        uint8_t const         idxRegTmp = iemNativeRegAllocTmp(pReNative, &off);
#ifdef RT_ARCH_AMD64
        PIEMNATIVEINSTR const pCodeBuf  = iemNativeInstrBufEnsure(pReNative, off, 32);
#elif defined(RT_ARCH_ARM64)
        PIEMNATIVEINSTR const pCodeBuf  = iemNativeInstrBufEnsure(pReNative, off, 16);
#else
# error "port me"
#endif
        off = iemNativeEmitPostponedEFlagsCalcLogical<a_fNeedToSetFlags>(pCodeBuf, off, cOpBits, idxRegResult,
                                                                         idxRegEfl, idxRegTmp);
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

        iemNativeVarRegisterRelease(pReNative, idxVarEfl);
        iemNativeRegFreeTmp(pReNative, idxRegTmp);
    }

#ifdef IEMNATIVE_WITH_EFLAGS_SKIPPING
    if (pReNative->fSkippingEFlags)
        Log5(("EFLAGS: fSkippingEFlags %#x -> 0 (iemNativeEmitEFlagsForLogical)\n", pReNative->fSkippingEFlags));
    pReNative->fSkippingEFlags = 0;
# ifdef IEMNATIVE_STRICT_EFLAGS_SKIPPING
    off = iemNativeEmitStoreImmToVCpuU32(pReNative, off, 0, RT_UOFFSETOF(VMCPU, iem.s.fSkippingEFlags));
# endif
#endif
    return off;
}


/**
 * This is an implementation of IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC.
 *
 * It takes liveness stuff into account.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitEFlagsForArithmetic(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarEfl, uint8_t idxRegEflIn
#ifndef RT_ARCH_AMD64
                                 , uint8_t cOpBits, uint8_t idxRegResult, uint8_t idxRegDstIn, uint8_t idxRegSrc
                                 , bool fInvertCarry, uint64_t uImmSrc
#endif
                                 )
{
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeEflTotalArithmetic);
    IEMNATIVE_CLEAR_POSTPONED_EFLAGS(pReNative, X86_EFL_STATUS_BITS);

#ifdef IEMNATIVE_WITH_EFLAGS_SKIPPING
    /*
     * See if we can skip this wholesale.
     */
    PCIEMLIVENESSENTRY const pLivenessEntry = &pReNative->paLivenessEntries[pReNative->idxCurCall];
    if (   IEMLIVENESS_STATE_ARE_STATUS_EFL_TO_BE_CLOBBERED(pLivenessEntry)
        && !(pReNative->fMc & IEM_MC_F_WITH_FLAGS))
    {
        STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeEflSkippedArithmetic);
        pReNative->fSkippingEFlags = X86_EFL_STATUS_BITS;
        Log5(("EFLAGS: Skipping %#x - iemNativeEmitEFlagsForArithmetic\n", X86_EFL_STATUS_BITS));
# ifdef IEMNATIVE_STRICT_EFLAGS_SKIPPING
        off = iemNativeEmitOrImmIntoVCpuU32(pReNative, off, X86_EFL_STATUS_BITS, RT_UOFFSETOF(VMCPU, iem.s.fSkippingEFlags));
# endif
    }
    else
#endif
    {
#ifdef RT_ARCH_AMD64
        /*
         * Collect flags and merge them with eflags.
         */
        PIEMNATIVEINSTR pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        /* pushf - do this before any reg allocations as they may emit instructions too. */
        pCodeBuf[off++] = 0x9c;

        uint8_t const idxRegEfl = idxRegEflIn != UINT8_MAX ? idxRegEflIn
                                : iemNativeVarRegisterAcquireInited(pReNative, idxVarEfl, &off);
        uint8_t const idxTmpReg = iemNativeRegAllocTmp(pReNative, &off);
        pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2 + 7 + 7 + 3);
        /* pop   tmp */
        if (idxTmpReg >= 8)
            pCodeBuf[off++] = X86_OP_REX_B;
        pCodeBuf[off++] = 0x58 + (idxTmpReg & 7);
        /* Isolate the flags we want. */
        off = iemNativeEmitAndGpr32ByImmEx(pCodeBuf, off, idxTmpReg, X86_EFL_STATUS_BITS);
        /* Clear the status bits in EFLs. */
        off = iemNativeEmitAndGpr32ByImmEx(pCodeBuf, off, idxRegEfl, ~X86_EFL_STATUS_BITS);
        /* OR in the flags we collected. */
        off = iemNativeEmitOrGpr32ByGprEx(pCodeBuf, off, idxRegEfl, idxTmpReg);
        if (idxRegEflIn != idxRegEfl)
            iemNativeVarRegisterRelease(pReNative, idxVarEfl);
        iemNativeRegFreeTmp(pReNative, idxTmpReg);

#elif defined(RT_ARCH_ARM64)
        /*
         * Calculate flags.
         */
        uint8_t const         idxRegEfl  = idxRegEflIn != UINT8_MAX ? idxRegEflIn
                                         : iemNativeVarRegisterAcquireInited(pReNative, idxVarEfl, &off);
        uint8_t const         idxTmpReg  = iemNativeRegAllocTmp(pReNative, &off);
        uint8_t const         idxTmpReg2 = cOpBits >= 32 ? UINT8_MAX : iemNativeRegAllocTmp(pReNative, &off);
        PIEMNATIVEINSTR const pCodeBuf   = iemNativeInstrBufEnsure(pReNative, off, 20);

        /* Invert CF (stored inverted on ARM) and load the flags into the temporary register. */
        if (fInvertCarry)
            pCodeBuf[off++] = ARMV8_A64_INSTR_CFINV;
        pCodeBuf[off++] = Armv8A64MkInstrMrs(idxTmpReg, ARMV8_AARCH64_SYSREG_NZCV); /* Bits: 31=N; 30=Z; 29=C; 28=V; */

        if (cOpBits >= 32)
        {
            /* V -> OF */
            pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxTmpReg, idxTmpReg, 28);
            pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegEfl, idxTmpReg, X86_EFL_OF_BIT, 1, false /*f64Bit*/);

            /* C -> CF */
            pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxTmpReg, idxTmpReg, 1);
            pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegEfl, idxTmpReg, X86_EFL_CF_BIT, 1, false /*f64Bit*/);
        }

        /* N,Z -> SF,ZF */
        pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxTmpReg, idxTmpReg, cOpBits >= 32 ? 1 : 30);
        pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegEfl, idxTmpReg, X86_EFL_ZF_BIT, 2, false /*f64Bit*/);

        /* For ADC and SBB we have to calculate overflow and carry our selves. */
        if (cOpBits < 32)
        {
            /* Since the carry flag is the zero'th flag, we just use BFXIL got copy it over. */
            AssertCompile(X86_EFL_CF_BIT == 0);
            pCodeBuf[off++] = Armv8A64MkInstrBfxil(idxRegEfl, idxRegResult, cOpBits, 1, false /*f64Bit*/);

            /* The overflow flag is more work as we have to compare the signed bits for
               both inputs and the result. See IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC.

               Formula: ~(a_uDst ^ a_uSrcOf) & (a_uResult ^ a_uDst)
               With a_uSrcOf as a_uSrc for additions and ~a_uSrc for subtractions.

               It is a bit simpler when the right (source) side is constant:
               adc: S D R -> OF                     sbb: S D R -> OF
                    0 0 0 -> 0  \                        0 0 0 -> 0  \
                    0 0 1 -> 1   \                       0 0 1 -> 0   \
                    0 1 0 -> 0   / and not(D), R         0 1 0 -> 1   / and D, not(R)
                    0 1 1 -> 0  /                        0 1 1 -> 0  /
                    1 0 0 -> 0  \                        1 0 0 -> 0  \
                    1 0 1 -> 0   \ and D, not(R)         1 0 1 -> 1   \ and not(D), R
                    1 1 0 -> 1   /                       1 1 0 -> 0   /
                    1 1 1 -> 0  /                        1 1 1 -> 0  / */
            if (idxRegSrc != UINT8_MAX)
            {
                if (fInvertCarry) /* sbb:  ~((a_uDst) ^ ~(a_uSrcOf)) ->  (a_uDst) ^  (a_uSrcOf); HACK ALERT: fInvertCarry == sbb */
                    pCodeBuf[off++] = Armv8A64MkInstrEor(idxTmpReg,  idxRegDstIn, idxRegSrc,  false);
                else              /* adc:  ~((a_uDst) ^ (a_uSrcOf))  ->  (a_uDst) ^ ~(a_uSrcOf) */
                    pCodeBuf[off++] = Armv8A64MkInstrEon(idxTmpReg,  idxRegDstIn, idxRegSrc,  false);
                pCodeBuf[off++] = Armv8A64MkInstrEor(idxTmpReg2,   idxRegDstIn, idxRegResult, false); /* (a_uDst) ^ (a_uResult) */
                pCodeBuf[off++] = Armv8A64MkInstrAnd(idxTmpReg,    idxTmpReg,   idxTmpReg2,   false /*f64Bit*/);
            }
            else if (uImmSrc & RT_BIT_64(cOpBits - 1))
            {
                if (fInvertCarry) /* HACK ALERT: fInvertCarry == sbb */
                    pCodeBuf[off++] = Armv8A64MkInstrBic(idxTmpReg, idxRegResult, idxRegDstIn, false);
                else
                    pCodeBuf[off++] = Armv8A64MkInstrBic(idxTmpReg, idxRegDstIn, idxRegResult, false);
            }
            else
            {
                if (fInvertCarry) /* HACK ALERT: fInvertCarry == sbb */
                    pCodeBuf[off++] = Armv8A64MkInstrBic(idxTmpReg, idxRegDstIn, idxRegResult, false);
                else
                    pCodeBuf[off++] = Armv8A64MkInstrBic(idxTmpReg, idxRegResult, idxRegDstIn, false);
            }
            pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxTmpReg, idxTmpReg,   cOpBits - 1,  false /*f64Bit*/);
            pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegEfl,    idxTmpReg,   X86_EFL_OF_BIT, 1);
            iemNativeRegFreeTmp(pReNative, idxTmpReg2);
        }

        /* Calculate 8-bit parity of the result. */
        pCodeBuf[off++] = Armv8A64MkInstrEor(idxTmpReg, idxRegResult, idxRegResult, false /*f64Bit*/,
                                             4 /*offShift6*/, kArmv8A64InstrShift_Lsr);
        pCodeBuf[off++] = Armv8A64MkInstrEor(idxTmpReg, idxTmpReg,    idxTmpReg,    false /*f64Bit*/,
                                             2 /*offShift6*/, kArmv8A64InstrShift_Lsr);
        pCodeBuf[off++] = Armv8A64MkInstrEor(idxTmpReg, idxTmpReg,    idxTmpReg,    false /*f64Bit*/,
                                             1 /*offShift6*/, kArmv8A64InstrShift_Lsr);
        Assert(Armv8A64ConvertImmRImmS2Mask32(0, 0) == 1);
        pCodeBuf[off++] = Armv8A64MkInstrEorImm(idxTmpReg, idxTmpReg, 0, 0, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegEfl, idxTmpReg, X86_EFL_PF_BIT, 1,  false /*f64Bit*/);

        /* Calculate auxilary carry/borrow.  This is related to 8-bit BCD.
           General formula: ((uint32_t)(a_uResult)  ^ (uint32_t)(a_uSrc) ^ (uint32_t)(a_uDst)) & X86_EFL_AF;
               S D R
               0 0 0 -> 0;  \
               0 0 1 -> 1;   \  regular
               0 1 0 -> 1;   /    xor R, D
               0 1 1 -> 0;  /
               1 0 0 -> 1;  \
               1 0 1 -> 0;   \  invert one of the two
               1 1 0 -> 0;   /    xor not(R), D
               1 1 1 -> 1;  /
           a_uSrc[bit 4]=0: ((uint32_t)(a_uResult)  ^ (uint32_t)(a_uDst)) & X86_EFL_AF;
           a_uSrc[bit 4]=1: ((uint32_t)~(a_uResult) ^ (uint32_t)(a_uDst)) & X86_EFL_AF;
           */

        if (idxRegSrc != UINT8_MAX)
        {
            pCodeBuf[off++] = Armv8A64MkInstrEor(idxTmpReg, idxRegDstIn, idxRegSrc, false /*f64Bit*/);
            pCodeBuf[off++] = Armv8A64MkInstrEor(idxTmpReg, idxTmpReg, idxRegResult, false /*f64Bit*/);
        }
        else if (uImmSrc & X86_EFL_AF)
            pCodeBuf[off++] = Armv8A64MkInstrEon(idxTmpReg, idxRegDstIn, idxRegResult, false /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrEor(idxTmpReg, idxRegDstIn, idxRegResult, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxTmpReg, idxTmpReg, X86_EFL_AF_BIT, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegEfl, idxTmpReg, X86_EFL_AF_BIT, 1,  false /*f64Bit*/);

        if (idxRegEflIn != idxRegEfl)
            iemNativeVarRegisterRelease(pReNative, idxVarEfl);
        iemNativeRegFreeTmp(pReNative, idxTmpReg);

#else
# error "port me"
#endif
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#ifdef IEMNATIVE_WITH_EFLAGS_SKIPPING
        if (pReNative->fSkippingEFlags)
            Log5(("EFLAGS: fSkippingEFlags %#x -> 0 (iemNativeEmitEFlagsForArithmetic)\n", pReNative->fSkippingEFlags));
        pReNative->fSkippingEFlags = 0;
# ifdef IEMNATIVE_STRICT_EFLAGS_SKIPPING
        off = iemNativeEmitStoreImmToVCpuU32(pReNative, off, 0, RT_UOFFSETOF(VMCPU, iem.s.fSkippingEFlags));
# endif
#endif
    }
    return off;

}



/*********************************************************************************************************************************
*   Bitwise Logical Operations                                                                                                   *
*********************************************************************************************************************************/

/**
 * The AND instruction will clear OF, CF and AF (latter is undefined) and
 * set the other flags according to the result.
 */
template<uint8_t const a_cOpBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_and_r_r_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint8_t idxVarSrc, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
    uint8_t const idxRegSrc = iemNativeVarRegisterAcquireInited(pReNative, idxVarSrc, &off);
#ifdef RT_ARCH_AMD64
    /* On AMD64 we just use the correctly sized AND instruction harvest the EFLAGS. */
    off = iemNativeEmitAmd64OneByteModRmInstrRREx(iemNativeInstrBufEnsure(pReNative, off, 4), off,
                                                  0x22, 0x23, a_cOpBits, idxRegDst, idxRegSrc);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    iemNativeVarRegisterRelease(pReNative, idxVarSrc);

    off = iemNativeEmitEFlagsForLogical<false>(pReNative, off, idxVarEfl, a_cOpBits, idxRegDst);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we use 32-bit AND for the 8-bit and 16-bit bit ones. */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pCodeBuf[off++] = Armv8A64MkInstrAnds(idxRegDst, idxRegDst, idxRegSrc, a_cOpBits > 32 /*f64Bit*/);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    iemNativeVarRegisterRelease(pReNative, idxVarSrc);

    off = iemNativeEmitEFlagsForLogical<false>(pReNative, off, idxVarEfl, a_cOpBits, idxRegDst);
#else
# error "Port me"
#endif
    iemNativeVarRegisterRelease(pReNative, idxVarDst);
    return off;
}


/**
 * The AND instruction with immediate value as right operand.
 */
template<uint8_t const a_cOpBits, uint8_t const a_cImmBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_and_r_i_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint64_t uImmOp, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
#ifdef RT_ARCH_AMD64
    /* On AMD64 we just use the correctly sized AND instruction harvest the EFLAGS. */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
    off = iemNativeEmitAmd64OneByteModRmInstrRIEx(pCodeBuf, off, 0x80, 0x83, 0x81, a_cOpBits, a_cImmBits, 4, idxRegDst, uImmOp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    off = iemNativeEmitEFlagsForLogical<false>(pReNative, off, idxVarEfl, a_cOpBits, idxRegDst);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we use 32-bit AND for the 8-bit and 16-bit bit ones, and of
       course the immediate variant when possible to save a register load. */
    uint32_t uImmSizeLen, uImmRotations;
    if (  a_cOpBits > 32
        ? Armv8A64ConvertMask64ToImmRImmS(uImmOp, &uImmSizeLen, &uImmRotations)
        : Armv8A64ConvertMask32ToImmRImmS(uImmOp, &uImmSizeLen, &uImmRotations))
    {
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        if (a_cOpBits >= 32)
            pCodeBuf[off++] = Armv8A64MkInstrAndsImm(idxRegDst, idxRegDst, uImmSizeLen, uImmRotations, a_cOpBits > 32 /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAndImm(idxRegDst, idxRegDst, uImmSizeLen, uImmRotations, a_cOpBits > 32 /*f64Bit*/);
    }
    else
    {
        uint8_t const idxRegTmpImm = iemNativeRegAllocTmpImm(pReNative, &off, uImmOp);
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        if RT_CONSTEXPR_IF(a_cOpBits >= 32)
            pCodeBuf[off++] = Armv8A64MkInstrAnds(idxRegDst, idxRegDst, idxRegTmpImm, a_cOpBits > 32 /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAnd(idxRegDst, idxRegDst, idxRegTmpImm, a_cOpBits > 32 /*f64Bit*/);
        iemNativeRegFreeTmpImm(pReNative, idxRegTmpImm);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    off = iemNativeEmitEFlagsForLogical<a_cOpBits < 32>(pReNative, off, idxVarEfl, a_cOpBits, idxRegDst);

#else
# error "Port me"
#endif
    iemNativeVarRegisterRelease(pReNative, idxVarDst);
    return off;
}


/**
 * The TEST instruction will clear OF, CF and AF (latter is undefined) and
 * set the other flags according to the result.
 */
template<uint8_t const a_cOpBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_test_r_r_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint8_t idxVarSrc, uint8_t idxVarEfl)
{
    uint8_t const         idxRegDst    = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
    uint8_t const         idxRegSrc    = idxVarSrc == idxVarDst ? idxRegDst /* special case of 'test samereg,samereg' */
                                       : iemNativeVarRegisterAcquireInited(pReNative, idxVarSrc, &off);
#ifdef RT_ARCH_AMD64
    /* On AMD64 we just use the correctly sized TEST instruction harvest the EFLAGS. */
    off = iemNativeEmitAmd64OneByteModRmInstrRREx(iemNativeInstrBufEnsure(pReNative, off, 4), off,
                                                  0x84, 0x85, a_cOpBits, idxRegSrc, idxRegDst);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we use 32-bit AND for the 8-bit and 16-bit bit ones.  We also
       need to keep the result in order to calculate the flags. */
    uint8_t const         idxRegResult = iemNativeRegAllocTmp(pReNative, &off);
    PIEMNATIVEINSTR const pCodeBuf     = iemNativeInstrBufEnsure(pReNative, off, 1);
    if RT_CONSTEXPR_IF(a_cOpBits >= 32)
        pCodeBuf[off++] = Armv8A64MkInstrAnds(idxRegResult, idxRegDst, idxRegSrc, a_cOpBits > 32 /*f64Bit*/);
    else
        pCodeBuf[off++] = Armv8A64MkInstrAnd(idxRegResult, idxRegDst, idxRegSrc, a_cOpBits > 32 /*f64Bit*/);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#else
# error "Port me"
#endif
    if (idxVarSrc != idxVarDst)
        iemNativeVarRegisterRelease(pReNative, idxVarSrc);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

#ifdef RT_ARCH_AMD64
    off = iemNativeEmitEFlagsForLogical<false>(pReNative, off, idxVarEfl, a_cOpBits, UINT8_MAX);
#else
    if RT_CONSTEXPR_IF(a_cOpBits >= 32)
        off = iemNativeEmitEFlagsForLogical<false>(pReNative, off, idxVarEfl, a_cOpBits, idxRegResult);
    else
        off = iemNativeEmitEFlagsForLogical<true>(pReNative, off, idxVarEfl, a_cOpBits, idxRegResult);
    iemNativeRegFreeTmp(pReNative, idxRegResult);
#endif
    return off;
}


/**
 * The TEST instruction with immediate value as right operand.
 */
template<uint8_t const a_cOpBits, uint8_t const a_cImmBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_test_r_i_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint64_t uImmOp, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
#ifdef RT_ARCH_AMD64
    /* On AMD64 we just use the correctly sized AND instruction harvest the EFLAGS. */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
    off = iemNativeEmitAmd64OneByteModRmInstrRIEx(pCodeBuf, off, 0xf6, 0xcc, 0xf7, a_cOpBits, a_cImmBits, 0, idxRegDst, uImmOp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

    off = iemNativeEmitEFlagsForLogical<false>(pReNative, off, idxVarEfl, a_cOpBits, UINT8_MAX);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we use 32-bit AND for the 8-bit and 16-bit bit ones, and of
       course the immediate variant when possible to save a register load.
       We also need to keep the result in order to calculate the flags. */
    uint8_t const         idxRegResult = iemNativeRegAllocTmp(pReNative, &off);
    uint32_t uImmSizeLen, uImmRotations;
    if (  a_cOpBits > 32
        ? Armv8A64ConvertMask64ToImmRImmS(uImmOp, &uImmSizeLen, &uImmRotations)
        : Armv8A64ConvertMask32ToImmRImmS(uImmOp, &uImmSizeLen, &uImmRotations))
    {
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        if RT_CONSTEXPR_IF(a_cOpBits >= 32)
            pCodeBuf[off++] = Armv8A64MkInstrAndsImm(idxRegResult, idxRegDst, uImmSizeLen, uImmRotations, a_cOpBits > 32 /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAndImm(idxRegResult, idxRegDst, uImmSizeLen, uImmRotations, a_cOpBits > 32 /*f64Bit*/);
    }
    else
    {
        uint8_t const idxRegTmpImm = iemNativeRegAllocTmpImm(pReNative, &off, uImmOp);
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        if RT_CONSTEXPR_IF(a_cOpBits >= 32)
            pCodeBuf[off++] = Armv8A64MkInstrAnds(idxRegResult, idxRegDst, idxRegTmpImm, a_cOpBits > 32 /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAnd(idxRegResult, idxRegDst, idxRegTmpImm, a_cOpBits > 32 /*f64Bit*/);
        iemNativeRegFreeTmpImm(pReNative, idxRegTmpImm);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

    off = iemNativeEmitEFlagsForLogical<a_cOpBits < 32>(pReNative, off, idxVarEfl, a_cOpBits, idxRegResult);

    iemNativeRegFreeTmp(pReNative, idxRegResult);

#else
# error "Port me"
#endif
    return off;
}


/**
 * The OR instruction will clear OF, CF and AF (latter is undefined) and
 * set the other flags according to the result.
 */
template<uint8_t const a_cOpBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_or_r_r_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint8_t idxVarSrc, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
    uint8_t const idxRegSrc = iemNativeVarRegisterAcquireInited(pReNative, idxVarSrc, &off);
#ifdef RT_ARCH_AMD64
    /* On AMD64 we just use the correctly sized OR instruction harvest the EFLAGS. */
    off = iemNativeEmitAmd64OneByteModRmInstrRREx(iemNativeInstrBufEnsure(pReNative, off, 4), off,
                                                  0x0a, 0x0b, a_cOpBits, idxRegDst, idxRegSrc);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    iemNativeVarRegisterRelease(pReNative, idxVarSrc);

    off = iemNativeEmitEFlagsForLogical<false>(pReNative, off, idxVarEfl, a_cOpBits, idxRegDst);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we use 32-bit OR for the 8-bit and 16-bit bit ones. */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pCodeBuf[off++] = Armv8A64MkInstrOrr(idxRegDst, idxRegDst, idxRegSrc, a_cOpBits > 32 /*f64Bit*/);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    iemNativeVarRegisterRelease(pReNative, idxVarSrc);

    off = iemNativeEmitEFlagsForLogical<true>(pReNative, off, idxVarEfl, a_cOpBits, idxRegDst);

#else
# error "Port me"
#endif
    iemNativeVarRegisterRelease(pReNative, idxVarDst);
    return off;
}


/**
 * The OR instruction with immediate value as right operand.
 */
template<uint8_t const a_cOpBits, uint8_t const a_cImmBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_or_r_i_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint64_t uImmOp, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
#ifdef RT_ARCH_AMD64
    /* On AMD64 we just use the correctly sized OR instruction harvest the EFLAGS. */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
    off = iemNativeEmitAmd64OneByteModRmInstrRIEx(pCodeBuf, off, 0x80, 0x83, 0x81, a_cOpBits, a_cImmBits, 1, idxRegDst, uImmOp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    off = iemNativeEmitEFlagsForLogical<false>(pReNative, off, idxVarEfl, a_cOpBits, idxRegDst);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we use 32-bit OR for the 8-bit and 16-bit bit ones, and of
       course the immediate variant when possible to save a register load.  */
    uint32_t uImmSizeLen, uImmRotations;
    if (  a_cOpBits > 32
        ? Armv8A64ConvertMask64ToImmRImmS(uImmOp, &uImmSizeLen, &uImmRotations)
        : Armv8A64ConvertMask32ToImmRImmS(uImmOp, &uImmSizeLen, &uImmRotations))
    {
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pCodeBuf[off++] = Armv8A64MkInstrOrrImm(idxRegDst, idxRegDst, uImmSizeLen, uImmRotations, a_cOpBits > 32 /*f64Bit*/);
    }
    else
    {
        uint8_t const idxRegTmpImm = iemNativeRegAllocTmpImm(pReNative, &off, uImmOp);
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pCodeBuf[off++] = Armv8A64MkInstrOrr(idxRegDst, idxRegDst, idxRegTmpImm, a_cOpBits > 32 /*f64Bit*/);
        iemNativeRegFreeTmpImm(pReNative, idxRegTmpImm);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    off = iemNativeEmitEFlagsForLogical<true>(pReNative, off, idxVarEfl, a_cOpBits, idxRegDst);

#else
# error "Port me"
#endif
    iemNativeVarRegisterRelease(pReNative, idxVarDst);
    return off;
}


/**
 * The XOR instruction will clear OF, CF and AF (latter is undefined) and
 * set the other flags according to the result.
 */
template<uint8_t const a_cOpBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_xor_r_r_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint8_t idxVarSrc, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
    uint8_t const idxRegSrc = iemNativeVarRegisterAcquireInited(pReNative, idxVarSrc, &off);
#ifdef RT_ARCH_AMD64
    /* On AMD64 we just use the correctly sized OR instruction harvest the EFLAGS. */
    off = iemNativeEmitAmd64OneByteModRmInstrRREx(iemNativeInstrBufEnsure(pReNative, off, 4), off,
                                                  0x32, 0x33, a_cOpBits, idxRegDst, idxRegSrc);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    iemNativeVarRegisterRelease(pReNative, idxVarSrc);

    off = iemNativeEmitEFlagsForLogical<false>(pReNative, off, idxVarEfl, a_cOpBits, idxRegDst);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we use 32-bit OR for the 8-bit and 16-bit bit ones. */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pCodeBuf[off++] = Armv8A64MkInstrEor(idxRegDst, idxRegDst, idxRegSrc, a_cOpBits > 32 /*f64Bit*/);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    iemNativeVarRegisterRelease(pReNative, idxVarSrc);

    off = iemNativeEmitEFlagsForLogical<true>(pReNative, off, idxVarEfl, a_cOpBits, idxRegDst);

#else
# error "Port me"
#endif
    iemNativeVarRegisterRelease(pReNative, idxVarDst);
    return off;
}


/**
 * The XOR instruction with immediate value as right operand.
 */
template<uint8_t const a_cOpBits, uint8_t const a_cImmBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_xor_r_i_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint64_t uImmOp, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
#ifdef RT_ARCH_AMD64
    /* On AMD64 we just use the correctly sized XOR instruction harvest the EFLAGS. */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
    off = iemNativeEmitAmd64OneByteModRmInstrRIEx(pCodeBuf, off, 0x80, 0x83, 0x81, a_cOpBits, a_cImmBits, 6, idxRegDst, uImmOp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    off = iemNativeEmitEFlagsForLogical<false>(pReNative, off, idxVarEfl, a_cOpBits, idxRegDst);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we use 32-bit OR for the 8-bit and 16-bit bit ones, and of
       course the immediate variant when possible to save a register load.  */
    uint32_t uImmSizeLen, uImmRotations;
    if (  a_cOpBits > 32
        ? Armv8A64ConvertMask64ToImmRImmS(uImmOp, &uImmSizeLen, &uImmRotations)
        : Armv8A64ConvertMask32ToImmRImmS(uImmOp, &uImmSizeLen, &uImmRotations))
    {
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pCodeBuf[off++] = Armv8A64MkInstrEorImm(idxRegDst, idxRegDst, uImmSizeLen, uImmRotations, a_cOpBits > 32 /*f64Bit*/);
    }
    else
    {
        uint8_t const idxRegTmpImm = iemNativeRegAllocTmpImm(pReNative, &off, uImmOp);
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pCodeBuf[off++] = Armv8A64MkInstrEor(idxRegDst, idxRegDst, idxRegTmpImm, a_cOpBits > 32 /*f64Bit*/);
        iemNativeRegFreeTmpImm(pReNative, idxRegTmpImm);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    off = iemNativeEmitEFlagsForLogical<true>(pReNative, off, idxVarEfl, a_cOpBits, idxRegDst);

#else
# error "Port me"
#endif
    iemNativeVarRegisterRelease(pReNative, idxVarDst);
    return off;
}



/*********************************************************************************************************************************
*   ADD, ADC, SUB, SBB, CMP                                                                                                      *
*********************************************************************************************************************************/

/**
 * The ADD instruction will set all status flags.
 */
template<uint8_t const a_cOpBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_add_r_r_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint8_t idxVarSrc, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
    uint8_t const idxRegSrc = iemNativeVarRegisterAcquireInited(pReNative, idxVarSrc, &off);

#ifdef RT_ARCH_AMD64
    /* On AMD64 we just use the correctly sized ADD instruction to get the right EFLAGS.SF value. */
    off = iemNativeEmitAmd64OneByteModRmInstrRREx(iemNativeInstrBufEnsure(pReNative, off, 4), off,
                                                  0x02, 0x03, a_cOpBits, idxRegDst, idxRegSrc);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    iemNativeVarRegisterRelease(pReNative, idxVarSrc);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, idxVarEfl, UINT8_MAX);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we'll need the two input operands as well as the result in order
       to calculate the right flags, even if we use ADDS and translates NZCV into
       OF, CF, ZF and SF. */
    uint8_t const         idxRegDstIn = iemNativeRegAllocTmp(pReNative, &off);
    PIEMNATIVEINSTR const pCodeBuf    = iemNativeInstrBufEnsure(pReNative, off, 4);
    if RT_CONSTEXPR_IF(a_cOpBits >= 32)
    {
        off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, idxRegDstIn, idxRegDst);
        pCodeBuf[off++] = Armv8A64MkInstrAddReg(idxRegDst, idxRegDst, idxRegSrc, a_cOpBits > 32 /*f64Bit*/, true /*fSetFlags*/);
    }
    else
    {
        /* Shift the operands up so we can perform a 32-bit operation and get all four flags. */
        uint32_t const cShift = 32 - a_cOpBits;
        pCodeBuf[off++] = Armv8A64MkInstrOrr(idxRegDstIn, ARMV8_A64_REG_XZR, idxRegDst, false /*f64Bit*/, cShift);
        pCodeBuf[off++] = Armv8A64MkInstrAddReg(idxRegDst, idxRegDstIn, idxRegSrc, false /*f64Bit*/,
                                                true /*fSetFlags*/, cShift);
        pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegDstIn, idxRegDstIn, cShift, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegDst, idxRegDst, cShift, false /*f64Bit*/);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, idxVarEfl, UINT8_MAX, a_cOpBits > 32 ? a_cOpBits : 32, idxRegDst,
                                           idxRegDstIn, idxRegSrc, false /*fInvertCarry*/, 0);

    iemNativeRegFreeTmp(pReNative, idxRegDstIn);
    iemNativeVarRegisterRelease(pReNative, idxVarSrc);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

#else
# error "port me"
#endif
    return off;
}


/**
 * The ADD instruction with immediate value as right operand.
 */
template<uint8_t const a_cOpBits, uint8_t const a_cImmBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_add_r_i_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint64_t uImmOp, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);

#ifdef RT_ARCH_AMD64
    /* On AMD64 we just use the correctly sized ADD instruction to get the right EFLAGS.SF value. */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
    off = iemNativeEmitAmd64OneByteModRmInstrRIEx(pCodeBuf, off, 0x80, 0x83, 0x81, a_cOpBits, a_cImmBits, 0, idxRegDst, uImmOp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    iemNativeVarRegisterRelease(pReNative, idxVarDst);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, idxVarEfl, UINT8_MAX);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we'll need the two input operands as well as the result in order
       to calculate the right flags, even if we use ADDS and translates NZCV into
       OF, CF, ZF and SF. */
    uint8_t const   idxRegDstIn  = iemNativeRegAllocTmp(pReNative, &off);
    PIEMNATIVEINSTR pCodeBuf     = iemNativeInstrBufEnsure(pReNative, off, 8);
    off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, idxRegDstIn, idxRegDst);
    if RT_CONSTEXPR_IF(a_cOpBits >= 32)
    {
        if (uImmOp <= 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(idxRegDst, idxRegDst, uImmOp, a_cOpBits > 32 /*f64Bit*/,
                                                       true /*fSetFlags*/);
        else if (uImmOp <= 0xfff000U && !(uImmOp & 0xfff))
            pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(idxRegDst, idxRegDst, uImmOp >> 12, a_cOpBits > 32 /*f64Bit*/,
                                                       true /*fSetFlags*/, true /*fShift12*/);
        else
        {
            uint8_t const idxRegTmpImm = iemNativeRegAllocTmpImm(pReNative, &off, uImmOp);
            pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
            pCodeBuf[off++] = Armv8A64MkInstrAddReg(idxRegDst, idxRegDst, idxRegTmpImm, a_cOpBits > 32 /*f64Bit*/,
                                                    true /*fSetFlags*/);
            iemNativeRegFreeTmpImm(pReNative, idxRegTmpImm);
        }
    }
    else
    {
        /* Shift the operands up so we can perform a 32-bit operation and get all four flags. */
        uint32_t const cShift = 32 - a_cOpBits;
        uint8_t const idxRegTmpImm = iemNativeRegAllocTmpImm(pReNative, &off, uImmOp << cShift);
        pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);
        pCodeBuf[off++] = Armv8A64MkInstrAddReg(idxRegDst, idxRegTmpImm, idxRegDstIn, false /*f64Bit*/, true /*fSetFlags*/, cShift);
        pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegDst, idxRegDst, cShift, false /*f64Bit*/);
        iemNativeRegFreeTmpImm(pReNative, idxRegTmpImm);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, idxVarEfl, UINT8_MAX, a_cOpBits > 32 ? a_cOpBits : 32, idxRegDst,
                                           idxRegDstIn, UINT8_MAX, false /*fInvertCarry*/, uImmOp);

    iemNativeRegFreeTmp(pReNative, idxRegDstIn);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

#else
# error "port me"
#endif
    return off;
}


/**
 * The ADC instruction takes CF as input and will set all status flags.
 */
template<uint8_t const a_cOpBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_adc_r_r_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint8_t idxVarSrc, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
    uint8_t const idxRegSrc = iemNativeVarRegisterAcquireInited(pReNative, idxVarSrc, &off);
    uint8_t const idxRegEfl = iemNativeVarRegisterAcquireInited(pReNative, idxVarEfl, &off);

#ifdef RT_ARCH_AMD64
    /* On AMD64 we use BT to set EFLAGS.CF and then issue an ADC instruction
       with matching size to get the correct flags. */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 9);

    /* Use the BT instruction to set CF according to idxRegEfl. */
    off = iemNativeEmitAmd64TwoByteModRmInstrRREx(pCodeBuf, off, 0x0f, 0x0b, 0xba, 32 /*cOpBits*/, 4, idxRegEfl);
    pCodeBuf[off++] = X86_EFL_CF_BIT;

    off = iemNativeEmitAmd64OneByteModRmInstrRREx(pCodeBuf, off, 0x12, 0x13, a_cOpBits, idxRegDst, idxRegSrc);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    iemNativeVarRegisterRelease(pReNative, idxVarSrc);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, UINT8_MAX, idxRegEfl);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we use the RMIF instruction to load PSTATE.CF from idxRegEfl and
       then ADCS for the calculation.  We need all inputs and result for the two
       flags (AF,PF) that can't be directly derived from PSTATE.NZCV. */
    uint8_t const         idxRegDstIn = iemNativeRegAllocTmp(pReNative, &off);
    PIEMNATIVEINSTR const pCodeBuf    = iemNativeInstrBufEnsure(pReNative, off, 7);

    pCodeBuf[off++] = Armv8A64MkInstrRmif(idxRegEfl, (X86_EFL_CF_BIT - 1) & 63, RT_BIT_32(1) /*fMask=C*/);
    off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, idxRegDstIn, idxRegDst);
    if RT_CONSTEXPR_IF(a_cOpBits >= 32)
        pCodeBuf[off++] = Armv8A64MkInstrAdcs(idxRegDst, idxRegDst, idxRegSrc, a_cOpBits > 32 /*f64Bit*/);
    else
    {
        /* Since we're also adding in the carry flag here, shifting operands up
           doesn't work. So, we have to calculate carry & overflow manually. */
        pCodeBuf[off++] = Armv8A64MkInstrAdc(idxRegDst, idxRegDst, idxRegSrc, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrSetF8SetF16(idxRegDst, a_cOpBits > 8); /* NZ are okay, CV aren't.*/
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, UINT8_MAX, idxRegEfl, a_cOpBits, idxRegDst,
                                           idxRegDstIn, idxRegSrc, false /*fInvertCarry*/, 0);

    iemNativeRegFreeTmp(pReNative, idxRegDstIn);
    iemNativeVarRegisterRelease(pReNative, idxVarSrc);
    if RT_CONSTEXPR_IF(a_cOpBits < 32)
        off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxRegDst, RT_BIT_32(a_cOpBits) - 1U);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

#else
# error "port me"
#endif
    iemNativeVarRegisterRelease(pReNative, idxVarEfl);
    return off;
}


/**
 * The ADC instruction with immediate value as right operand.
 */
template<uint8_t const a_cOpBits, uint8_t const a_cImmBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_adc_r_i_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint64_t uImmOp, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
    uint8_t const idxRegEfl = iemNativeVarRegisterAcquireInited(pReNative, idxVarEfl, &off);

#ifdef RT_ARCH_AMD64
    /* On AMD64 we use BT to set EFLAGS.CF and then issue an ADC instruction
       with matching size to get the correct flags. */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 12);

    off = iemNativeEmitAmd64TwoByteModRmInstrRREx(pCodeBuf, off, 0x0f, 0x0b, 0xba, 32 /*cOpBits*/, 4, idxRegEfl);
    pCodeBuf[off++] = X86_EFL_CF_BIT;

    off = iemNativeEmitAmd64OneByteModRmInstrRIEx(pCodeBuf, off, 0x80, 0x83, 0x81, a_cOpBits, a_cImmBits, 2, idxRegDst, uImmOp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    iemNativeVarRegisterRelease(pReNative, idxVarDst);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, UINT8_MAX, idxRegEfl);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we use the RMIF instructions to load PSTATE.CF from idxRegEfl
       and then ADCS for the calculation.  We need all inputs and result for
       the two flags (AF,PF) that can't be directly derived from PSTATE.NZCV. */
    uint8_t const         idxRegDstIn = iemNativeRegAllocTmp(pReNative, &off);
    uint8_t const         idxRegImm   = iemNativeRegAllocTmpImm(pReNative, &off, uImmOp);
    PIEMNATIVEINSTR const pCodeBuf    = iemNativeInstrBufEnsure(pReNative, off, 4);

    pCodeBuf[off++] = Armv8A64MkInstrRmif(idxRegEfl, (X86_EFL_CF_BIT - 1) & 63, RT_BIT_32(1) /*fMask=C*/);
    off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, idxRegDstIn, idxRegDst);
    if RT_CONSTEXPR_IF(a_cOpBits >= 32)
        pCodeBuf[off++] = Armv8A64MkInstrAdcs(idxRegDst, idxRegDst, idxRegImm, a_cOpBits > 32 /*f64Bit*/);
    else
    {
        /* Since we're also adding in the carry flag here, shifting operands up
           doesn't work. So, we have to calculate carry & overflow manually. */
        pCodeBuf[off++] = Armv8A64MkInstrAdc(idxRegDst, idxRegDst, idxRegImm, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrSetF8SetF16(idxRegDst, a_cOpBits > 8); /* NZ are okay, CV aren't.*/
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    iemNativeRegFreeTmp(pReNative, idxRegImm);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, UINT8_MAX, idxRegEfl, a_cOpBits, idxRegDst,
                                           idxRegDstIn, UINT8_MAX, false /*fInvertCarry*/, uImmOp);

    iemNativeRegFreeTmp(pReNative, idxRegDstIn);
    if RT_CONSTEXPR_IF(a_cOpBits < 32)
        off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxRegDst, RT_BIT_32(a_cOpBits) - 1U);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

#else
# error "port me"
#endif
    iemNativeVarRegisterRelease(pReNative, idxVarEfl);
    return off;
}


/**
 * The SUB instruction will set all status flags.
 */
template<uint8_t const a_cOpBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_sub_r_r_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint8_t idxVarSrc, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
    uint8_t const idxRegSrc = iemNativeVarRegisterAcquireInited(pReNative, idxVarSrc, &off);

#ifdef RT_ARCH_AMD64
    /* On AMD64 we just use the correctly sized SUB instruction to get the right EFLAGS.SF value. */
    off = iemNativeEmitAmd64OneByteModRmInstrRREx(iemNativeInstrBufEnsure(pReNative, off, 4), off,
                                                  0x2a, 0x2b, a_cOpBits, idxRegDst, idxRegSrc);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    iemNativeVarRegisterRelease(pReNative, idxVarSrc);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, idxVarEfl, UINT8_MAX);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we'll need the two input operands as well as the result in order
       to calculate the right flags, even if we use SUBS and translates NZCV into
       OF, CF, ZF and SF. */
    uint8_t const         idxRegDstIn = iemNativeRegAllocTmp(pReNative, &off);
    PIEMNATIVEINSTR const pCodeBuf    = iemNativeInstrBufEnsure(pReNative, off, 4);
    if RT_CONSTEXPR_IF(a_cOpBits >= 32)
    {
        off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, idxRegDstIn, idxRegDst);
        pCodeBuf[off++] = Armv8A64MkInstrSubReg(idxRegDst, idxRegDst, idxRegSrc, a_cOpBits > 32 /*f64Bit*/, true /*fSetFlags*/);
    }
    else
    {
        /* Shift the operands up so we can perform a 32-bit operation and get all four flags. */
        uint32_t const cShift = 32 - a_cOpBits;
        pCodeBuf[off++] = Armv8A64MkInstrOrr(idxRegDstIn, ARMV8_A64_REG_XZR, idxRegDst, false /*f64Bit*/, cShift);
        pCodeBuf[off++] = Armv8A64MkInstrSubReg(idxRegDst, idxRegDstIn, idxRegSrc, false /*f64Bit*/,
                                                true /*fSetFlags*/, cShift);
        pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegDstIn, idxRegDstIn, cShift, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegDst, idxRegDst, cShift, false /*f64Bit*/);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, idxVarEfl, UINT8_MAX, a_cOpBits > 32 ? a_cOpBits : 32, idxRegDst,
                                           idxRegDstIn, idxRegSrc, true /*fInvertCarry*/, 0);

    iemNativeRegFreeTmp(pReNative, idxRegDstIn);
    iemNativeVarRegisterRelease(pReNative, idxVarSrc);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

#else
# error "port me"
#endif
    return off;
}


/**
 * The SUB instruction with immediate value as right operand.
 */
template<uint8_t const a_cOpBits, uint8_t const a_cImmBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_sub_r_i_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint64_t uImmOp, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);

#ifdef RT_ARCH_AMD64
    /* On AMD64 we just use the correctly sized SUB instruction to get the right EFLAGS.SF value. */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
    off = iemNativeEmitAmd64OneByteModRmInstrRIEx(pCodeBuf, off, 0x80, 0x83, 0x81, a_cOpBits, a_cImmBits, 5, idxRegDst, uImmOp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    iemNativeVarRegisterRelease(pReNative, idxVarDst);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, idxVarEfl, UINT8_MAX);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we'll need the two input operands as well as the result in order
       to calculate the right flags, even if we use SUBS and translates NZCV into
       OF, CF, ZF and SF. */
    uint8_t const   idxRegDstIn  = iemNativeRegAllocTmp(pReNative, &off);
    PIEMNATIVEINSTR pCodeBuf     = iemNativeInstrBufEnsure(pReNative, off, 8);
    off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, idxRegDstIn, idxRegDst);
    if RT_CONSTEXPR_IF(a_cOpBits >= 32)
    {
        if (uImmOp <= 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrSubUImm12(idxRegDst, idxRegDst, uImmOp, a_cOpBits > 32 /*f64Bit*/,
                                                       true /*fSetFlags*/);
        else if (uImmOp <= 0xfff000U && !(uImmOp & 0xfff))
            pCodeBuf[off++] = Armv8A64MkInstrSubUImm12(idxRegDst, idxRegDst, uImmOp >> 12, a_cOpBits > 32 /*f64Bit*/,
                                                       true /*fSetFlags*/, true /*fShift12*/);
        else
        {
            uint8_t const idxRegTmpImm = iemNativeRegAllocTmpImm(pReNative, &off, uImmOp);
            pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
            pCodeBuf[off++] = Armv8A64MkInstrSubReg(idxRegDst, idxRegDst, idxRegTmpImm, a_cOpBits > 32 /*f64Bit*/,
                                                    true /*fSetFlags*/);
            iemNativeRegFreeTmpImm(pReNative, idxRegTmpImm);
        }
    }
    else
    {
        /* Shift the operands up so we can perform a 32-bit operation and get all four flags. */
        uint32_t const cShift       = 32 - a_cOpBits;
        uint8_t const  idxRegTmpImm = iemNativeRegAllocTmpImm(pReNative, &off, uImmOp);
        pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 4);
        pCodeBuf[off++] = Armv8A64MkInstrLslImm(idxRegDstIn, idxRegDstIn, cShift, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrSubReg(idxRegDst,   idxRegDstIn, idxRegTmpImm, false /*f64Bit*/, true /*fSetFlags*/, cShift);
        pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegDstIn, idxRegDstIn, cShift, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegDst,   idxRegDst,   cShift, false /*f64Bit*/);
        iemNativeRegFreeTmpImm(pReNative, idxRegTmpImm);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, idxVarEfl, UINT8_MAX, a_cOpBits > 32 ? a_cOpBits : 32, idxRegDst,
                                           idxRegDstIn, UINT8_MAX, true /*fInvertCarry*/, uImmOp);

    iemNativeRegFreeTmp(pReNative, idxRegDstIn);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

#else
# error "port me"
#endif
    return off;
}


/**
 * The CMP instruction will set all status flags, but modifies no registers.
 */
template<uint8_t const a_cOpBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_cmp_r_r_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint8_t idxVarSrc, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
    uint8_t const idxRegSrc = iemNativeVarRegisterAcquireInited(pReNative, idxVarSrc, &off);

#ifdef RT_ARCH_AMD64
    /* On AMD64 we just use the correctly sized CMP instruction to get the right EFLAGS.SF value. */
    off = iemNativeEmitAmd64OneByteModRmInstrRREx(iemNativeInstrBufEnsure(pReNative, off, 4), off,
                                                  0x3a, 0x3b, a_cOpBits, idxRegDst, idxRegSrc);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    iemNativeVarRegisterRelease(pReNative, idxVarSrc);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, idxVarEfl, UINT8_MAX);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we'll need the actual result as well as both input operands in order
       to calculate the right flags, even if we use SUBS and translates NZCV into
       OF, CF, ZF and SF. */
    uint8_t const         idxRegResult = iemNativeRegAllocTmp(pReNative, &off);
    PIEMNATIVEINSTR const pCodeBuf     = iemNativeInstrBufEnsure(pReNative, off, 3);
    if RT_CONSTEXPR_IF(a_cOpBits >= 32)
        pCodeBuf[off++] = Armv8A64MkInstrSubReg(idxRegResult, idxRegDst, idxRegSrc, a_cOpBits > 32 /*f64Bit*/, true /*fSetFlags*/);
    else
    {
        /* Shift the operands up so we can perform a 32-bit operation and get all four flags. */
        uint32_t const cShift = 32 - a_cOpBits;
        pCodeBuf[off++] = Armv8A64MkInstrOrr(idxRegResult, ARMV8_A64_REG_XZR, idxRegDst, false /*f64Bit*/, cShift);
        pCodeBuf[off++] = Armv8A64MkInstrSubReg(idxRegResult, idxRegResult, idxRegSrc, false /*f64Bit*/,
                                                true /*fSetFlags*/, cShift);
        pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegResult, idxRegResult, cShift, false /*f64Bit*/);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, idxVarEfl, UINT8_MAX, a_cOpBits > 32 ? a_cOpBits : 32, idxRegResult,
                                           idxRegDst, idxRegSrc, true /*fInvertCarry*/, 0);

    iemNativeRegFreeTmp(pReNative, idxRegResult);
    iemNativeVarRegisterRelease(pReNative, idxVarSrc);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

#else
# error "port me"
#endif
    return off;
}


/**
 * The CMP instruction with immediate value as right operand.
 */
template<uint8_t const a_cOpBits, uint8_t const a_cImmBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_cmp_r_i_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint64_t uImmOp, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);

#ifdef RT_ARCH_AMD64
    /* On AMD64 we just use the correctly sized CMP instruction to get the right EFLAGS.SF value. */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
    off = iemNativeEmitAmd64OneByteModRmInstrRIEx(pCodeBuf, off, 0x80, 0x83, 0x81, a_cOpBits, a_cImmBits, 7, idxRegDst, uImmOp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    iemNativeVarRegisterRelease(pReNative, idxVarDst);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, idxVarEfl, UINT8_MAX);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we'll need the actual result as well as both input operands in order
       to calculate the right flags, even if we use SUBS and translates NZCV into
       OF, CF, ZF and SF. */
    uint8_t const   idxRegResult = iemNativeRegAllocTmp(pReNative, &off);
    PIEMNATIVEINSTR pCodeBuf     = iemNativeInstrBufEnsure(pReNative, off, 8);
    if RT_CONSTEXPR_IF(a_cOpBits >= 32)
    {
        if (uImmOp <= 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrSubUImm12(idxRegResult, idxRegDst, uImmOp, a_cOpBits > 32 /*f64Bit*/,
                                                       true /*fSetFlags*/);
        else if (uImmOp <= 0xfff000U && !(uImmOp & 0xfff))
            pCodeBuf[off++] = Armv8A64MkInstrSubUImm12(idxRegResult, idxRegDst, uImmOp >> 12, a_cOpBits > 32 /*f64Bit*/,
                                                       true /*fSetFlags*/, true /*fShift12*/);
        else
        {
            uint8_t const idxRegTmpImm = iemNativeRegAllocTmpImm(pReNative, &off, uImmOp);
            pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
            pCodeBuf[off++] = Armv8A64MkInstrSubReg(idxRegResult, idxRegDst, idxRegTmpImm, a_cOpBits > 32 /*f64Bit*/,
                                                    true /*fSetFlags*/);
            iemNativeRegFreeTmpImm(pReNative, idxRegTmpImm);
        }
    }
    else
    {
        /* Shift the operands up so we can perform a 32-bit operation and get all four flags. */
        uint32_t const cShift       = 32 - a_cOpBits;
        uint8_t const  idxRegTmpImm = iemNativeRegAllocTmpImm(pReNative, &off, uImmOp);
        pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3);
        pCodeBuf[off++] = Armv8A64MkInstrLslImm(idxRegResult, idxRegDst,    cShift, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrSubReg(idxRegResult, idxRegResult, idxRegTmpImm, false /*f64Bit*/, true /*fSetFlags*/, cShift);
        pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegResult, idxRegResult, cShift, false /*f64Bit*/);
        iemNativeRegFreeTmpImm(pReNative, idxRegTmpImm);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, idxVarEfl, UINT8_MAX, a_cOpBits > 32 ? a_cOpBits : 32, idxRegResult,
                                           idxRegDst, UINT8_MAX, true /*fInvertCarry*/, uImmOp);

    iemNativeRegFreeTmp(pReNative, idxRegResult);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

#else
# error "port me"
#endif
    return off;
}


/**
 * The SBB instruction takes CF as input and will set all status flags.
 */
template<uint8_t const a_cOpBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_sbb_r_r_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint8_t idxVarSrc, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
    uint8_t const idxRegSrc = iemNativeVarRegisterAcquireInited(pReNative, idxVarSrc, &off);
    uint8_t const idxRegEfl = iemNativeVarRegisterAcquireInited(pReNative, idxVarEfl, &off);

#ifdef RT_ARCH_AMD64
    /* On AMD64 we use BT to set EFLAGS.CF and then issue an SBB instruction
       with matching size to get the correct flags. */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 9);

    off = iemNativeEmitAmd64TwoByteModRmInstrRREx(pCodeBuf, off, 0x0f, 0x0b, 0xba, 32 /*cOpBits*/, 4, idxRegEfl);
    pCodeBuf[off++] = X86_EFL_CF_BIT;

    off = iemNativeEmitAmd64OneByteModRmInstrRREx(pCodeBuf, off, 0x1a, 0x1b, a_cOpBits, idxRegDst, idxRegSrc);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    iemNativeVarRegisterRelease(pReNative, idxVarSrc);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, UINT8_MAX, idxRegEfl);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we use the RMIF+CFINV instructions to load PSTATE.CF from
       idxRegEfl and then SBCS for the calculation.  We need all inputs and
       result for the two flags (AF,PF) that can't be directly derived from
       PSTATE.NZCV. */
    uint8_t const         idxRegDstIn = iemNativeRegAllocTmp(pReNative, &off);
    PIEMNATIVEINSTR const pCodeBuf    = iemNativeInstrBufEnsure(pReNative, off, 5);

    pCodeBuf[off++] = Armv8A64MkInstrRmif(idxRegEfl, (X86_EFL_CF_BIT - 1) & 63, RT_BIT_32(1) /*fMask=C*/);
    pCodeBuf[off++] = ARMV8_A64_INSTR_CFINV;
    off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, idxRegDstIn, idxRegDst);
    if RT_CONSTEXPR_IF(a_cOpBits >= 32)
        pCodeBuf[off++] = Armv8A64MkInstrSbcs(idxRegDst, idxRegDst, idxRegSrc, a_cOpBits > 32 /*f64Bit*/);
    else
    {
        /* Since we're also adding in the carry flag here, shifting operands up
           doesn't work. So, we have to calculate carry & overflow manually. */
        pCodeBuf[off++] = Armv8A64MkInstrSbc(idxRegDst, idxRegDst, idxRegSrc, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrSetF8SetF16(idxRegDst, a_cOpBits > 8); /* NZ are okay, CV aren't.*/
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, UINT8_MAX, idxRegEfl, a_cOpBits, idxRegDst,
                                           idxRegDstIn, idxRegSrc, true /*fInvertCarry*/, 0);

    iemNativeRegFreeTmp(pReNative, idxRegDstIn);
    iemNativeVarRegisterRelease(pReNative, idxVarSrc);
    if RT_CONSTEXPR_IF(a_cOpBits < 32)
        off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxRegDst, RT_BIT_32(a_cOpBits) - 1U);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

#else
# error "port me"
#endif
    iemNativeVarRegisterRelease(pReNative, idxVarEfl);
    return off;
}


/**
 * The SBB instruction with immediate value as right operand.
 */
template<uint8_t const a_cOpBits, uint8_t const a_cImmBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_sbb_r_i_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint64_t uImmOp, uint8_t idxVarEfl)
{
    uint8_t const idxRegDst = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
    uint8_t const idxRegEfl = iemNativeVarRegisterAcquireInited(pReNative, idxVarEfl, &off);

#ifdef RT_ARCH_AMD64
    /* On AMD64 we use BT to set EFLAGS.CF and then issue an SBB instruction
       with matching size to get the correct flags. */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 12);

    off = iemNativeEmitAmd64TwoByteModRmInstrRREx(pCodeBuf, off, 0x0f, 0x0b, 0xba, 32 /*cOpBits*/, 4, idxRegEfl);
    pCodeBuf[off++] = X86_EFL_CF_BIT;

    off = iemNativeEmitAmd64OneByteModRmInstrRIEx(pCodeBuf, off, 0x80, 0x83, 0x81, a_cOpBits, a_cImmBits, 3, idxRegDst, uImmOp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    iemNativeVarRegisterRelease(pReNative, idxVarDst);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, UINT8_MAX, idxRegEfl);

#elif defined(RT_ARCH_ARM64)
    /* On ARM64 we use the RMIF+CFINV instructions to load PSTATE.CF from
       idxRegEfl and then SBCS for the calculation.  We need all inputs and
       result for the two flags (AF,PF) that can't be directly derived from
       PSTATE.NZCV. */
    uint8_t const         idxRegDstIn = iemNativeRegAllocTmp(pReNative, &off);
    uint8_t const         idxRegImm   = iemNativeRegAllocTmpImm(pReNative, &off, uImmOp);
    PIEMNATIVEINSTR const pCodeBuf    = iemNativeInstrBufEnsure(pReNative, off, 5);

    pCodeBuf[off++] = Armv8A64MkInstrRmif(idxRegEfl, (X86_EFL_CF_BIT - 1) & 63, RT_BIT_32(1) /*fMask=C*/);
    pCodeBuf[off++] = ARMV8_A64_INSTR_CFINV;
    off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, idxRegDstIn, idxRegDst);
    if RT_CONSTEXPR_IF(a_cOpBits >= 32)
        pCodeBuf[off++] = Armv8A64MkInstrSbcs(idxRegDst, idxRegDst, idxRegImm, a_cOpBits > 32 /*f64Bit*/);
    else
    {
        /* Since we're also adding in the carry flag here, shifting operands up
           doesn't work. So, we have to calculate carry & overflow manually. */
        pCodeBuf[off++] = Armv8A64MkInstrSbc(idxRegDst, idxRegDst, idxRegImm, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrSetF8SetF16(idxRegDst, a_cOpBits > 8); /* NZ are okay, CV aren't.*/
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    iemNativeRegFreeTmp(pReNative, idxRegImm);

    off = iemNativeEmitEFlagsForArithmetic(pReNative, off, UINT8_MAX, idxRegEfl, a_cOpBits, idxRegDst,
                                           idxRegDstIn, UINT8_MAX, true /*fInvertCarry*/, uImmOp);

    iemNativeRegFreeTmp(pReNative, idxRegDstIn);
    if RT_CONSTEXPR_IF(a_cOpBits < 32)
        off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxRegDst, RT_BIT_32(a_cOpBits) - 1U);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);

#else
# error "port me"
#endif
    iemNativeVarRegisterRelease(pReNative, idxVarEfl);
    return off;
}


template<uint8_t const a_cOpBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_imul_r_r_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint8_t idxVarSrc, uint8_t idxVarEfl)
{
    RT_NOREF(idxVarDst, idxVarSrc, idxVarEfl);
    AssertFailed();
    return iemNativeEmitBrk(pReNative, off, 0x666);
}


template<uint8_t const a_cOpBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_popcnt_r_r_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint8_t idxVarSrc, uint8_t idxVarEfl)
{
    RT_NOREF(idxVarDst, idxVarSrc, idxVarEfl);
    AssertFailed();
    return iemNativeEmitBrk(pReNative, off, 0x666);
}


template<uint8_t const a_cOpBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_tzcnt_r_r_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint8_t idxVarSrc, uint8_t idxVarEfl)
{
    RT_NOREF(idxVarDst, idxVarSrc, idxVarEfl);
    AssertFailed();
    return iemNativeEmitBrk(pReNative, off, 0x666);
}


template<uint8_t const a_cOpBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_lzcnt_r_r_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint8_t idxVarSrc, uint8_t idxVarEfl)
{
    RT_NOREF(idxVarDst, idxVarSrc, idxVarEfl);
    AssertFailed();
    return iemNativeEmitBrk(pReNative, off, 0x666);
}



/*********************************************************************************************************************************
*   Shifting and Rotating.                                                                                                       *
*********************************************************************************************************************************/


typedef enum
{
    kIemNativeEmitEFlagsForShiftType_Left,
    kIemNativeEmitEFlagsForShiftType_Right,
    kIemNativeEmitEFlagsForShiftType_SignedRight
} IEMNATIVEEMITEFLAGSFORSHIFTTYPE;

/**
 * This is used by SHL, SHR and SAR emulation.
 *
 * It takes liveness stuff into account.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitEFlagsForShift(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxRegEfl, uint8_t idxRegResult,
                            uint8_t idxRegSrc, uint8_t idxRegCount, uint8_t cOpBits, IEMNATIVEEMITEFLAGSFORSHIFTTYPE enmType,
                            uint8_t idxRegTmp)
{
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeEflTotalShift);

RT_NOREF(pReNative, off, idxRegEfl, idxRegResult, idxRegSrc, idxRegCount, cOpBits, enmType);
#if 0 //def IEMNATIVE_WITH_EFLAGS_SKIPPING
    /*
     * See if we can skip this wholesale.
     */
    PCIEMLIVENESSENTRY const pLivenessEntry = &pReNative->paLivenessEntries[pReNative->idxCurCall];
    if (   IEMLIVENESS_STATE_ARE_STATUS_EFL_TO_BE_CLOBBERED(pLivenessEntry)
        && !(pReNative->fMc & IEM_MC_F_WITH_FLAGS))
    {
        STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeEflSkippedShift);
        pReNative->fSkippingEFlags |= X86_EFL_STATUS_BITS;
# ifdef IEMNATIVE_STRICT_EFLAGS_SKIPPING
        off = iemNativeEmitOrImmIntoVCpuU32(pReNative, off, X86_EFL_STATUS_BITS, RT_UOFFSETOF(VMCPU, iem.s.fSkippingEFlags));
# endif
    }
    else
#endif
    {
        /*
         * The difference between Intel and AMD flags for SHL are:
         *  - Intel always clears AF while AMD always sets it.
         *  - Intel sets OF for the first shift, while AMD for the last shift.
         *
         */

#ifdef RT_ARCH_AMD64
        /*
         * We capture flags and does the additional OF and AF calculations as needed.
         */
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 64);
        /** @todo kIemNativeEmitEFlagsForShiftType_SignedRight: we could alternatively
         *        use LAHF here when host rax is free since, OF is cleared. */
        /* pushf */
        pCodeBuf[off++] = 0x9c;
        /* pop   tmp */
        if (idxRegTmp >= 8)
            pCodeBuf[off++] = X86_OP_REX_B;
        pCodeBuf[off++] = 0x58 + (idxRegTmp & 7);
        /* Clear the status bits in EFLs. */
        off = iemNativeEmitAndGpr32ByImmEx(pCodeBuf, off, idxRegEfl, ~X86_EFL_STATUS_BITS);
        uint8_t const idxTargetCpuEflFlavour = pReNative->pVCpu->iem.s.aidxTargetCpuEflFlavour[1];
        if (idxTargetCpuEflFlavour == IEMTARGETCPU_EFL_BEHAVIOR_NATIVE)
            off = iemNativeEmitAndGpr32ByImmEx(pCodeBuf, off, idxRegTmp, X86_EFL_STATUS_BITS);
        else
        {
            /* and  tmp, X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_CF */
            off = iemNativeEmitAndGpr32ByImmEx(pCodeBuf, off, idxRegTmp, X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_CF);
            if (idxTargetCpuEflFlavour == IEMTARGETCPU_EFL_BEHAVIOR_AMD)
                off = iemNativeEmitOrGpr32ByImmEx(pCodeBuf, off, idxRegTmp, X86_EFL_AF);
            /* OR in the flags we collected. */
            off = iemNativeEmitOrGpr32ByGprEx(pCodeBuf, off, idxRegEfl, idxRegTmp);

            /* Calculate OF */
            if (idxTargetCpuEflFlavour == IEMTARGETCPU_EFL_BEHAVIOR_AMD)
            {
                /* AMD last bit shifted: fEfl |= ((uResult >> (cOpBits - 1)) ^ fCarry) << X86_EFL_OF_BIT; */
                /* bt   idxRegResult, (cOpBits - 1) => CF=result-sign-bit */
                off = iemNativeEmitAmd64TwoByteModRmInstrRREx(pCodeBuf, off, 0x0f, 0x0b /*ud2*/, 0xba,
                                                              RT_MAX(cOpBits, 16), 4, idxRegResult);
                pCodeBuf[off++] = cOpBits - 1;
                /* setc idxRegTmp */
                off = iemNativeEmitAmd64TwoByteModRmInstrRREx(pCodeBuf, off, 0x0f, 0x92, 0x0b /*ud2*/, 8, 0, idxRegTmp);
                /* xor  idxRegTmp, idxRegEfl */
                off = iemNativeEmitXorGpr32ByGpr32Ex(pCodeBuf, off, idxRegTmp, idxRegEfl);
                /* and  idxRegTmp, 1 */
                off = iemNativeEmitAndGpr32ByImmEx(pCodeBuf, off, idxRegTmp, 1);
                /* shl  idxRegTmp, X86_EFL_OF_BIT */
                off = iemNativeEmitShiftGpr32LeftEx(pCodeBuf, off, idxRegTmp, X86_EFL_OF_BIT);
            }
            else
            {
                /* Intel first bit shifted: fEfl |= X86_EFL_GET_OF_ ## cOpBits(uDst ^ (uDst << 1)); */
                if (cOpBits <= 32)
                {
                    /* mov  idxRegTmp, idxRegSrc */
                    off = iemNativeEmitLoadGprFromGpr32Ex(pCodeBuf, off, idxRegTmp, idxRegSrc);
                    /* shl  idxRegTmp, 1 */
                    off = iemNativeEmitShiftGpr32LeftEx(pCodeBuf, off, idxRegTmp, 1);
                    /* xor  idxRegTmp, idxRegSrc */
                    off = iemNativeEmitXorGprByGprEx(pCodeBuf, off, idxRegTmp, idxRegSrc);
                    /* shr  idxRegTmp, cOpBits - X86_EFL_OF_BIT - 1  or  shl idxRegTmp, X86_EFL_OF_BIT - cOpBits + 1 */
                    if (cOpBits >= X86_EFL_OF_BIT)
                        off = iemNativeEmitShiftGpr32RightEx(pCodeBuf, off, idxRegTmp, cOpBits - X86_EFL_OF_BIT - 1);
                    else
                        off = iemNativeEmitShiftGpr32LeftEx(pCodeBuf, off, idxRegTmp, X86_EFL_OF_BIT - cOpBits + 1);
                }
                else
                {
                    /* same as above but with 64-bit grps*/
                    off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, idxRegTmp, idxRegSrc);
                    off = iemNativeEmitShiftGprLeftEx(pCodeBuf, off, idxRegTmp, 1);
                    off = iemNativeEmitXorGprByGprEx(pCodeBuf, off, idxRegTmp, idxRegSrc);
                    off = iemNativeEmitShiftGprRightEx(pCodeBuf, off, idxRegTmp, cOpBits - X86_EFL_OF_BIT - 1);
                }
                /* and  idxRegTmp, X86_EFL_OF */
                off = iemNativeEmitAndGpr32ByImmEx(pCodeBuf, off, idxRegTmp, X86_EFL_OF);
            }
        }
        /* Or in the collected flag(s) */
        off = iemNativeEmitOrGpr32ByGprEx(pCodeBuf, off, idxRegEfl, idxRegTmp);

#elif defined(RT_ARCH_ARM64)
        /*
         * Calculate flags.
         */
        PIEMNATIVEINSTR const pCodeBuf  = iemNativeInstrBufEnsure(pReNative, off, 20);

        /* Clear the status bits. ~0x8D5 (or ~0x8FD) can't be AND immediate, so use idxRegTmp for constant. */
        off = iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off, idxRegTmp, ~X86_EFL_STATUS_BITS);
        off = iemNativeEmitAndGpr32ByGpr32Ex(pCodeBuf, off, idxRegEfl, idxRegTmp);

        /* N,Z -> SF,ZF */
        if (cOpBits < 32)
            pCodeBuf[off++] = Armv8A64MkInstrSetF8SetF16(idxRegResult, cOpBits > 8); /* sets NZ */
        else
            pCodeBuf[off++] = Armv8A64MkInstrAnds(ARMV8_A64_REG_XZR, idxRegResult, idxRegResult, cOpBits > 32 /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrMrs(idxRegTmp, ARMV8_AARCH64_SYSREG_NZCV); /* Bits: 31=N; 30=Z; 29=C; 28=V; */
        pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegTmp, idxRegTmp, 30);
        pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegEfl, idxRegTmp, X86_EFL_ZF_BIT, 2, false /*f64Bit*/);
        AssertCompile(X86_EFL_ZF_BIT + 1 == X86_EFL_SF_BIT);

        /* Calculate 8-bit parity of the result. */
        pCodeBuf[off++] = Armv8A64MkInstrEor(idxRegTmp, idxRegResult, idxRegResult, false /*f64Bit*/,
                                             4 /*offShift6*/, kArmv8A64InstrShift_Lsr);
        pCodeBuf[off++] = Armv8A64MkInstrEor(idxRegTmp, idxRegTmp,    idxRegTmp,    false /*f64Bit*/,
                                             2 /*offShift6*/, kArmv8A64InstrShift_Lsr);
        pCodeBuf[off++] = Armv8A64MkInstrEor(idxRegTmp, idxRegTmp,    idxRegTmp,    false /*f64Bit*/,
                                             1 /*offShift6*/, kArmv8A64InstrShift_Lsr);
        Assert(Armv8A64ConvertImmRImmS2Mask32(0, 0) == 1);
        pCodeBuf[off++] = Armv8A64MkInstrEorImm(idxRegTmp, idxRegTmp, 0, 0, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegEfl, idxRegTmp, X86_EFL_PF_BIT, 1,  false /*f64Bit*/);

        /* Calculate carry - the last bit shifted out of the input value. */
        if (enmType == kIemNativeEmitEFlagsForShiftType_Left)
        {
            /* CF = (idxRegSrc >> (cOpBits - idxRegCount))) & 1 */
            pCodeBuf[off++] = Armv8A64MkInstrMovZ(idxRegTmp, cOpBits);
            pCodeBuf[off++] = Armv8A64MkInstrSubReg(idxRegTmp, idxRegTmp, idxRegCount, false /*f64Bit*/, cOpBits < 32 /*fSetFlags*/);
            if (cOpBits < 32)
                pCodeBuf[off++] = Armv8A64MkInstrBCond(kArmv8InstrCond_Cc, 3); /* 16 or 8 bit: CF is clear if all shifted out */
            pCodeBuf[off++] = Armv8A64MkInstrLsrv(idxRegTmp, idxRegSrc, idxRegTmp, cOpBits > 32);
        }
        else
        {
            /* CF = (idxRegSrc >> (idxRegCount - 1)) & 1 */
            pCodeBuf[off++] = Armv8A64MkInstrSubUImm12(idxRegTmp, idxRegCount, 1, false /*f64Bit*/);
            pCodeBuf[off++] = Armv8A64MkInstrLsrv(idxRegTmp, idxRegSrc, idxRegTmp, cOpBits > 32);
        }
        pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegEfl, idxRegTmp, X86_EFL_CF_BIT, 1, false /*f64Bit*/);

        uint8_t const idxTargetCpuEflFlavour = pReNative->pVCpu->iem.s.aidxTargetCpuEflFlavour[0];
        if (idxTargetCpuEflFlavour != IEMTARGETCPU_EFL_BEHAVIOR_AMD)
        {
            /* Intel: OF = first bit shifted: fEfl |= X86_EFL_GET_OF_ ## cOpBits(uDst ^ (uDst << 1)); */
            pCodeBuf[off++] = Armv8A64MkInstrEor(idxRegTmp, idxRegSrc, idxRegSrc, cOpBits > 32, 1 /*left shift count*/);
            pCodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegTmp, idxRegTmp, cOpBits - 1, cOpBits > 32);
            pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegEfl, idxRegTmp, X86_EFL_OF_BIT, 1, false /*f64Bit*/);
        }
        else
        {
            /* AMD: OF = last bit shifted: fEfl |= ((uResult >> (cOpBits - 1)) ^ fCarry) << X86_EFL_OF_BIT; */
            AssertCompile(X86_EFL_CF_BIT == 0);
            pCodeBuf[off++] = Armv8A64MkInstrEor(idxRegTmp, idxRegEfl, idxRegResult, cOpBits > 32, /* ASSUMES CF calculated! */
                                                 cOpBits - 1, kArmv8A64InstrShift_Lsr);
            pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegEfl, idxRegTmp, X86_EFL_OF_BIT, 1, false /*f64Bit*/);

            /* AMD unconditionally clears AF. */
            Assert(Armv8A64ConvertImmRImmS2Mask32(0, 32 - X86_EFL_AF_BIT) == X86_EFL_AF);
            pCodeBuf[off++] = Armv8A64MkInstrOrrImm(idxRegEfl, idxRegEfl, 0, 32 - X86_EFL_AF_BIT, false /*f64Bit*/);
        }
#else
# error "port me"
#endif
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#ifdef IEMNATIVE_WITH_EFLAGS_SKIPPING
        if (pReNative->fSkippingEFlags)
            Log5(("EFLAGS: fSkippingEFlags %#x -> 0 (iemNativeEmitEFlagsForShift)\n", pReNative->fSkippingEFlags));
        pReNative->fSkippingEFlags = 0;
# ifdef IEMNATIVE_STRICT_EFLAGS_SKIPPING
        off = iemNativeEmitStoreImmToVCpuU32(pReNative, off, 0, RT_UOFFSETOF(VMCPU, iem.s.fSkippingEFlags));
# endif
#endif
    }
    return off;
}


DECL_INLINE_THROW(uint32_t)
iemNativeEmit_shl_r_CL_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                           uint8_t idxVarDst, uint8_t idxVarCount, uint8_t idxVarEfl, uint8_t cOpBits)
{
    /* Note! Since we're doing some branching here, we need to allocate all
             registers we need before the jump or we may end up with invalid
             register state if the branch is taken. */
    uint8_t const idxRegTmp   = iemNativeRegAllocTmp(pReNative, &off); /* Do this first in hope we'll get EAX. */
    uint8_t const idxRegCount = iemNativeVarRegisterAcquireInited(pReNative, idxVarCount, &off); /* modified on arm64 */
    uint8_t const idxRegDst   = iemNativeVarRegisterAcquireInited(pReNative, idxVarDst, &off);
    uint8_t const idxRegEfl   = iemNativeVarRegisterAcquireInited(pReNative, idxVarEfl, &off);

#ifdef RT_ARCH_AMD64
    /* Make sure IEM_MC_NATIVE_AMD64_HOST_REG_FOR_LOCAL was used. */
    AssertStmt(idxRegCount == X86_GREG_xCX, IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_EMIT_UNEXPECTED_VAR_REGISTER));

    /* We only need a copy of the input value if the target CPU differs from the host CPU. */
    uint8_t const         idxRegDstIn = pReNative->pVCpu->iem.s.aidxTargetCpuEflFlavour[1] == IEMTARGETCPU_EFL_BEHAVIOR_NATIVE
                                      ? UINT8_MAX : iemNativeRegAllocTmp(pReNative, &off);
    PIEMNATIVEINSTR const pCodeBuf    = iemNativeInstrBufEnsure(pReNative, off, 4+2+3+4);

    /* Check if it's NOP before we do anything. */
    off = iemNativeEmitTestAnyBitsInGpr8Ex(pCodeBuf, off, idxRegCount, cOpBits <= 32 ? 0x1f : 0x3f);
    uint32_t const offFixup = off;
    off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off /*8-bit should be enough */, kIemNativeInstrCond_z);

    if (idxRegDstIn != UINT8_MAX)
        off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, idxRegDstIn, idxRegDst);
    off = iemNativeEmitAmd64OneByteModRmInstrRREx(pCodeBuf, off, 0xd2, 0xd3, cOpBits, 4, idxRegDst);

#elif defined(RT_ARCH_ARM64)
    /* We always (except we can skip EFLAGS calcs) a copy of the input value. */
    uint8_t const         idxRegDstIn = iemNativeRegAllocTmp(pReNative, &off);
    PIEMNATIVEINSTR const pCodeBuf    = iemNativeInstrBufEnsure(pReNative, off, 6);

    /* Check if it's NOP before we do anything. We MODIFY idxRegCount here! */
    Assert(Armv8A64ConvertImmRImmS2Mask32(4, 0) == 0x1f);
    Assert(Armv8A64ConvertImmRImmS2Mask32(5, 0) == 0x3f);
    pCodeBuf[off++] = Armv8A64MkInstrAndsImm(idxRegCount, idxRegCount, cOpBits > 32 ? 5 : 4, 0, false /*f64Bit*/);
    uint32_t const offFixup = off;
    off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off, kArmv8InstrCond_Eq);

    pCodeBuf[off++] = Armv8A64MkInstrMov(idxRegDstIn, idxRegDst);
    pCodeBuf[off++] = Armv8A64MkInstrLslv(idxRegDst, idxRegDst, idxRegCount, cOpBits > 32 /*f64Bit*/);
    if (cOpBits < 32)
    {
        Assert(Armv8A64ConvertImmRImmS2Mask32(7, 0) == 0xff);
        Assert(Armv8A64ConvertImmRImmS2Mask32(15, 0) == 0xffff);
        pCodeBuf[off++] = Armv8A64MkInstrAndImm(idxRegDst, idxRegDst, cOpBits - 1, 0, false /*f64Bit*/);
    }

#else
# error "port me"
#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    off = iemNativeEmitEFlagsForShift(pReNative, off, idxRegEfl, idxRegDst, idxRegDstIn, idxRegCount,
                                      cOpBits, kIemNativeEmitEFlagsForShiftType_Left, idxRegTmp);

    /* fixup the jump */
    iemNativeFixupFixedJump(pReNative, offFixup, off);

#ifdef RT_ARCH_AMD64
    if (idxRegDstIn != UINT8_MAX)
#endif
        iemNativeRegFreeTmp(pReNative, idxRegDstIn);
    iemNativeVarRegisterRelease(pReNative, idxVarEfl);
    iemNativeVarRegisterRelease(pReNative, idxVarDst);
    iemNativeVarRegisterRelease(pReNative, idxVarCount);
    iemNativeRegFreeTmp(pReNative, idxRegTmp);
    return off;
}


DECL_INLINE_THROW(uint32_t)
iemNativeEmit_shr_r_CL_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                           uint8_t idxVarDst, uint8_t idxVarCount, uint8_t idxVarEfl, uint8_t cOpBits)
{
    RT_NOREF(idxVarDst, idxVarCount, idxVarEfl, cOpBits);
    AssertFailed();
    return iemNativeEmitBrk(pReNative, off, 0x666);
}


DECL_INLINE_THROW(uint32_t)
iemNativeEmit_sar_r_CL_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                           uint8_t idxVarDst, uint8_t idxVarCount, uint8_t idxVarEfl, uint8_t cOpBits)
{
    RT_NOREF(idxVarDst, idxVarCount, idxVarEfl, cOpBits);
    AssertFailed();
    return iemNativeEmitBrk(pReNative, off, 0x666);
}


DECL_INLINE_THROW(uint32_t)
iemNativeEmit_rol_r_CL_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                           uint8_t idxVarDst, uint8_t idxVarCount, uint8_t idxVarEfl, uint8_t cOpBits)
{
    RT_NOREF(idxVarDst, idxVarCount, idxVarEfl, cOpBits);
    AssertFailed();
    return iemNativeEmitBrk(pReNative, off, 0x666);
}


DECL_INLINE_THROW(uint32_t)
iemNativeEmit_ror_r_CL_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                           uint8_t idxVarDst, uint8_t idxVarCount, uint8_t idxVarEfl, uint8_t cOpBits)
{
    RT_NOREF(idxVarDst, idxVarCount, idxVarEfl, cOpBits);
    AssertFailed();
    return iemNativeEmitBrk(pReNative, off, 0x666);
}


DECL_INLINE_THROW(uint32_t)
iemNativeEmit_rcl_r_CL_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                           uint8_t idxVarDst, uint8_t idxVarCount, uint8_t idxVarEfl, uint8_t cOpBits)
{
    RT_NOREF(idxVarDst, idxVarCount, idxVarEfl, cOpBits);
    AssertFailed();
    return iemNativeEmitBrk(pReNative, off, 0x666);
}


DECL_INLINE_THROW(uint32_t)
iemNativeEmit_rcr_r_CL_efl(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                           uint8_t idxVarDst, uint8_t idxVarCount, uint8_t idxVarEfl, uint8_t cOpBits)
{
    RT_NOREF(idxVarDst, idxVarCount, idxVarEfl, cOpBits);
    AssertFailed();
    return iemNativeEmitBrk(pReNative, off, 0x666);
}



/*********************************************************************************************************************************
*   SIMD emitters.                                                                                                               *
*********************************************************************************************************************************/

/**
 * Common emitter for packed arithmetic instructions.
 */
#ifdef RT_ARCH_AMD64
# define IEMNATIVE_NATIVE_EMIT_LOGICAL_OP_U128(a_Instr, a_enmArmOp, a_bOpcX86) \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rr_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxSimdGstRegSrc) \
    { \
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
        uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegSrc), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ReadOnly); \
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5); \
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP; \
        if (idxSimdRegDst >= 8 || idxSimdRegSrc >= 8) \
            pCodeBuf[off++] =   (idxSimdRegSrc >= 8 ? X86_OP_REX_B : 0) \
                              | (idxSimdRegDst >= 8 ? X86_OP_REX_R : 0); \
        pCodeBuf[off++] = 0x0f; \
        pCodeBuf[off++] = (a_bOpcX86); \
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxSimdRegDst & 7, idxSimdRegSrc & 7); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc); \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rv_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxVarSrc) \
    { \
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
        uint8_t const idxSimdRegSrc = iemNativeVarSimdRegisterAcquire(pReNative, idxVarSrc, &off, true /*fInitialized*/); \
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5); \
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP; \
        if (idxSimdRegDst >= 8 || idxSimdRegSrc >= 8) \
            pCodeBuf[off++] =   (idxSimdRegSrc >= 8 ? X86_OP_REX_B : 0) \
                              | (idxSimdRegDst >= 8 ? X86_OP_REX_R : 0); \
        pCodeBuf[off++] = 0x0f; \
        pCodeBuf[off++] = (a_bOpcX86); \
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxSimdRegDst & 7, idxSimdRegSrc & 7); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
        iemNativeVarRegisterRelease(pReNative, idxVarSrc); \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    typedef int ignore_semicolon
#elif defined(RT_ARCH_ARM64)
# define IEMNATIVE_NATIVE_EMIT_LOGICAL_OP_U128(a_Instr, a_enmArmOp, a_bOpcX86) \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rr_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxSimdGstRegSrc) \
    { \
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
        uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegSrc), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ReadOnly); \
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1); \
        pCodeBuf[off++] = Armv8A64MkVecInstrLogical((a_enmArmOp), idxSimdRegDst, idxSimdRegDst, idxSimdRegSrc); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc); \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rv_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxVarSrc) \
    { \
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
        uint8_t const idxSimdRegSrc = iemNativeVarSimdRegisterAcquire(pReNative, idxVarSrc, &off, true /*fInitialized*/); \
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1); \
        pCodeBuf[off++] = Armv8A64MkVecInstrLogical((a_enmArmOp), idxSimdRegDst, idxSimdRegDst, idxSimdRegSrc); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
        iemNativeVarRegisterRelease(pReNative, idxVarSrc); \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    typedef int ignore_semicolon
#else
# error "Port me"
#endif

/* POR, ORPS, ORPD. */
IEMNATIVE_NATIVE_EMIT_LOGICAL_OP_U128(por, kArmv8VecInstrLogicOp_Orr, 0xeb);
/* PXOR, XORPS, XORPD. */
IEMNATIVE_NATIVE_EMIT_LOGICAL_OP_U128(pxor, kArmv8VecInstrLogicOp_Eor, 0xef);
/* PAND, ANDPS, ANDPD. */
IEMNATIVE_NATIVE_EMIT_LOGICAL_OP_U128(pand, kArmv8VecInstrLogicOp_And, 0xdb);


/**
 * Common emitter for the shift right with immediate instructions.
 */
#ifdef RT_ARCH_AMD64
# define IEMNATIVE_NATIVE_EMIT_SHIFT_RIGHT_IMM_U128(a_Instr, a_cShiftMax, a_ArmElemSz, a_bOpcX86) \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_ri_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const bImm) \
    { \
        if (bImm) \
        { \
            uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                                  kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
            PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 6); \
            pCodeBuf[off++] = X86_OP_PRF_SIZE_OP; \
            if (idxSimdRegDst >= 8) \
                pCodeBuf[off++] = X86_OP_REX_B; \
            pCodeBuf[off++] = 0x0f; \
            pCodeBuf[off++] = (a_bOpcX86); \
            pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 2, idxSimdRegDst & 7); \
            pCodeBuf[off++] = bImm; \
            iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
            IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        } \
        /* Immediate 0 is a nop. */ \
        return off; \
    } \
    typedef int ignore_semicolon
#elif defined(RT_ARCH_ARM64)
# define IEMNATIVE_NATIVE_EMIT_SHIFT_RIGHT_IMM_U128(a_Instr, a_cShiftMax, a_ArmElemSz, a_bOpcX86) \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_ri_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const bImm) \
    { \
        if (bImm) \
        { \
            uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                                  kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
            PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1); \
            pCodeBuf[off++] = Armv8A64MkVecInstrShrImm(idxSimdRegDst, idxSimdRegDst, RT_MIN(bImm, (a_cShiftMax)), (a_ArmElemSz)); \
            iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
            IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        } \
        /* Immediate 0 is a nop. */ \
        return off; \
    } \
    typedef int ignore_semicolon
#else
# error "Port me"
#endif

IEMNATIVE_NATIVE_EMIT_SHIFT_RIGHT_IMM_U128(psrlw, 16, kArmv8InstrShiftSz_U16, 0x71);
IEMNATIVE_NATIVE_EMIT_SHIFT_RIGHT_IMM_U128(psrld, 32, kArmv8InstrShiftSz_U32, 0x72);
IEMNATIVE_NATIVE_EMIT_SHIFT_RIGHT_IMM_U128(psrlq, 64, kArmv8InstrShiftSz_U64, 0x73);


/**
 * Common emitter for the shift left with immediate instructions.
 */
#ifdef RT_ARCH_AMD64
# define IEMNATIVE_NATIVE_EMIT_SHIFT_LEFT_IMM_U128(a_Instr, a_cShiftMax, a_ArmElemSz, a_bOpcX86) \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_ri_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const bImm) \
    { \
        if (bImm) \
        { \
            uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                                  kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
            PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 6); \
            pCodeBuf[off++] = X86_OP_PRF_SIZE_OP; \
            if (idxSimdRegDst >= 8) \
                pCodeBuf[off++] = X86_OP_REX_B; \
            pCodeBuf[off++] = 0x0f; \
            pCodeBuf[off++] = (a_bOpcX86); \
            pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 6, idxSimdRegDst & 7); \
            pCodeBuf[off++] = bImm; \
            iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
            IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        } \
        /* Immediate 0 is a nop. */ \
        return off; \
    } \
    typedef int ignore_semicolon
#elif defined(RT_ARCH_ARM64)
# define IEMNATIVE_NATIVE_EMIT_SHIFT_LEFT_IMM_U128(a_Instr, a_cShiftMax, a_ArmElemSz, a_bOpcX86) \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_ri_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const bImm) \
    { \
        if (bImm) /* bImm == 0 is a nop */ \
        { \
            uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                                  kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
            PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1); \
            if (bImm < (a_cShiftMax)) \
                pCodeBuf[off++] = Armv8A64MkVecInstrShlImm(idxSimdRegDst, idxSimdRegDst, bImm, (a_ArmElemSz)); \
            else /* Everything >= a_cShiftMax sets the register to zero. */ \
                pCodeBuf[off++] = Armv8A64MkVecInstrEor(idxSimdRegDst, idxSimdRegDst, idxSimdRegDst); \
            iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
            IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        } \
        return off; \
    } \
    typedef int ignore_semicolon
#else
# error "Port me"
#endif

IEMNATIVE_NATIVE_EMIT_SHIFT_LEFT_IMM_U128(psllw, 16, kArmv8InstrShiftSz_U16, 0x71);
IEMNATIVE_NATIVE_EMIT_SHIFT_LEFT_IMM_U128(pslld, 32, kArmv8InstrShiftSz_U32, 0x72);
IEMNATIVE_NATIVE_EMIT_SHIFT_LEFT_IMM_U128(psllq, 64, kArmv8InstrShiftSz_U64, 0x73);


/**
 * Common emitter for packed arithmetic instructions.
 */
#ifdef RT_ARCH_AMD64
# define IEMNATIVE_NATIVE_EMIT_ARITH_OP_U128(a_Instr, a_enmArmOp, a_ArmElemSz, a_bOpcX86) \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rr_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxSimdGstRegSrc) \
    { \
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
        uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegSrc), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ReadOnly); \
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5); \
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP; \
        if (idxSimdRegDst >= 8 || idxSimdRegSrc >= 8) \
            pCodeBuf[off++] =   (idxSimdRegSrc >= 8 ? X86_OP_REX_B : 0) \
                              | (idxSimdRegDst >= 8 ? X86_OP_REX_R : 0); \
        pCodeBuf[off++] = 0x0f; \
        pCodeBuf[off++] = (a_bOpcX86); \
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxSimdRegDst & 7, idxSimdRegSrc & 7); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc); \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rv_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxVarSrc) \
    { \
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
        uint8_t const idxSimdRegSrc = iemNativeVarSimdRegisterAcquire(pReNative, idxVarSrc, &off, true /*fInitialized*/); \
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5); \
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP; \
        if (idxSimdRegDst >= 8 || idxSimdRegSrc >= 8) \
            pCodeBuf[off++] =   (idxSimdRegSrc >= 8 ? X86_OP_REX_B : 0) \
                              | (idxSimdRegDst >= 8 ? X86_OP_REX_R : 0); \
        pCodeBuf[off++] = 0x0f; \
        pCodeBuf[off++] = (a_bOpcX86); \
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxSimdRegDst & 7, idxSimdRegSrc & 7); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
        iemNativeVarRegisterRelease(pReNative, idxVarSrc); \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    typedef int ignore_semicolon
#elif defined(RT_ARCH_ARM64)
# define IEMNATIVE_NATIVE_EMIT_ARITH_OP_U128(a_Instr, a_enmArmOp, a_ArmElemSz, a_bOpcX86) \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rr_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxSimdGstRegSrc) \
    { \
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
        uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegSrc), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ReadOnly); \
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1); \
        pCodeBuf[off++] = Armv8A64MkVecInstrArithOp((a_enmArmOp), idxSimdRegDst, idxSimdRegDst, idxSimdRegSrc, (a_ArmElemSz)); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc); \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rv_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxVarSrc) \
    { \
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
        uint8_t const idxSimdRegSrc = iemNativeVarSimdRegisterAcquire(pReNative, idxVarSrc, &off, true /*fInitialized*/); \
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1); \
        pCodeBuf[off++] = Armv8A64MkVecInstrArithOp((a_enmArmOp), idxSimdRegDst, idxSimdRegDst, idxSimdRegSrc, (a_ArmElemSz)); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
        iemNativeVarRegisterRelease(pReNative, idxVarSrc); \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    typedef int ignore_semicolon
#else
# error "Port me"
#endif

/*
 * PADDx.
 */
IEMNATIVE_NATIVE_EMIT_ARITH_OP_U128(paddb, kArmv8VecInstrArithOp_Add, kArmv8VecInstrArithSz_8,  0xfc);
IEMNATIVE_NATIVE_EMIT_ARITH_OP_U128(paddw, kArmv8VecInstrArithOp_Add, kArmv8VecInstrArithSz_16, 0xfd);
IEMNATIVE_NATIVE_EMIT_ARITH_OP_U128(paddd, kArmv8VecInstrArithOp_Add, kArmv8VecInstrArithSz_32, 0xfe);
IEMNATIVE_NATIVE_EMIT_ARITH_OP_U128(paddq, kArmv8VecInstrArithOp_Add, kArmv8VecInstrArithSz_64, 0xd4);

/*
 * PSUBx.
 */
IEMNATIVE_NATIVE_EMIT_ARITH_OP_U128(psubb, kArmv8VecInstrArithOp_Sub, kArmv8VecInstrArithSz_8,  0xf8);
IEMNATIVE_NATIVE_EMIT_ARITH_OP_U128(psubw, kArmv8VecInstrArithOp_Sub, kArmv8VecInstrArithSz_16, 0xf9);
IEMNATIVE_NATIVE_EMIT_ARITH_OP_U128(psubd, kArmv8VecInstrArithOp_Sub, kArmv8VecInstrArithSz_32, 0xfa);
IEMNATIVE_NATIVE_EMIT_ARITH_OP_U128(psubq, kArmv8VecInstrArithOp_Sub, kArmv8VecInstrArithSz_64, 0xfb);

/*
 * PADDUSx.
 */
IEMNATIVE_NATIVE_EMIT_ARITH_OP_U128(paddusb, kArmv8VecInstrArithOp_UnsignSat_Add, kArmv8VecInstrArithSz_8,  0xdc);
IEMNATIVE_NATIVE_EMIT_ARITH_OP_U128(paddusw, kArmv8VecInstrArithOp_UnsignSat_Add, kArmv8VecInstrArithSz_16, 0xdd);

/*
 * PMULLx.
 */
IEMNATIVE_NATIVE_EMIT_ARITH_OP_U128(pmullw,  kArmv8VecInstrArithOp_Mul, kArmv8VecInstrArithSz_16, 0xd5);


/**
 * Common emitter for the pcmpeqb/pcmpeqw/pcmpeqd instructions.
 */
#ifdef RT_ARCH_AMD64
# define IEMNATIVE_NATIVE_EMIT_PCMP_U128(a_Instr, a_enmOp, a_ArmElemSz, a_bOpcX86) \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rr_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxSimdGstRegSrc) \
    { \
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
        uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegSrc), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ReadOnly); \
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5); \
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP; \
        if (idxSimdRegDst >= 8 || idxSimdRegSrc >= 8) \
            pCodeBuf[off++] =   (idxSimdRegSrc >= 8 ? X86_OP_REX_B : 0) \
                              | (idxSimdRegDst >= 8 ? X86_OP_REX_R : 0); \
        pCodeBuf[off++] = 0x0f; \
        pCodeBuf[off++] = (a_bOpcX86); \
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxSimdRegDst & 7, idxSimdRegSrc & 7); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc); \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rv_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxVarSrc) \
    { \
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
        uint8_t const idxSimdRegSrc = iemNativeVarSimdRegisterAcquire(pReNative, idxVarSrc, &off, true /*fInitialized*/); \
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5); \
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP; \
        if (idxSimdRegDst >= 8 || idxSimdRegSrc >= 8) \
            pCodeBuf[off++] =   (idxSimdRegSrc >= 8 ? X86_OP_REX_B : 0) \
                              | (idxSimdRegDst >= 8 ? X86_OP_REX_R : 0); \
        pCodeBuf[off++] = 0x0f; \
        pCodeBuf[off++] = (a_bOpcX86); \
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxSimdRegDst & 7, idxSimdRegSrc & 7); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
        iemNativeVarRegisterRelease(pReNative, idxVarSrc); \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    typedef int ignore_semicolon
#elif defined(RT_ARCH_ARM64)
# define IEMNATIVE_NATIVE_EMIT_PCMP_U128(a_Instr, a_enmOp, a_ArmElemSz, a_bOpcX86) \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rr_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxSimdGstRegSrc) \
    { \
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
        uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegSrc), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ReadOnly); \
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1); \
        pCodeBuf[off++] = Armv8A64MkVecInstrCmp((a_enmOp), idxSimdRegDst, idxSimdRegDst, idxSimdRegSrc, (a_ArmElemSz)); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc); \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rv_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxVarSrc) \
    { \
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
        uint8_t const idxSimdRegSrc = iemNativeVarSimdRegisterAcquire(pReNative, idxVarSrc, &off, true /*fInitialized*/); \
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1); \
        pCodeBuf[off++] = Armv8A64MkVecInstrCmp((a_enmOp), idxSimdRegDst, idxSimdRegDst, idxSimdRegSrc, (a_ArmElemSz)); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
        iemNativeVarRegisterRelease(pReNative, idxVarSrc); \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    typedef int ignore_semicolon
#else
# error "Port me"
#endif

IEMNATIVE_NATIVE_EMIT_PCMP_U128(pcmpeqb, kArmv8VecInstrCmpOp_Eq, kArmv8VecInstrArithSz_8,  0x74);
IEMNATIVE_NATIVE_EMIT_PCMP_U128(pcmpeqw, kArmv8VecInstrCmpOp_Eq, kArmv8VecInstrArithSz_16, 0x75);
IEMNATIVE_NATIVE_EMIT_PCMP_U128(pcmpeqd, kArmv8VecInstrCmpOp_Eq, kArmv8VecInstrArithSz_32, 0x76);


/**
 * Emitter for pmovmskb
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_pmovmskb_rr_u128(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                               uint8_t const idxGstRegDst, uint8_t const idxSimdGstRegSrc)
{
#ifdef RT_ARCH_AMD64
    uint8_t const idxRegDst = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(idxGstRegDst),
                                                              kIemNativeGstRegUse_ForFullWrite);
    uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off,
                                                                          IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegSrc),
                                                                          kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_ReadOnly);
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5);

    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (idxRegDst >= 8 || idxSimdRegSrc >= 8)
        pCodeBuf[off++] = (idxSimdRegSrc >= 8 ? X86_OP_REX_B : 0)
                        | (idxRegDst     >= 8 ? X86_OP_REX_R : 0);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xd7;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxRegDst & 7, idxSimdRegSrc & 7);

    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    iemNativeRegFreeTmp(pReNative, idxRegDst);

#elif defined(RT_ARCH_ARM64)
    uint8_t const idxRegDst = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(idxGstRegDst),
                                                              kIemNativeGstRegUse_ForFullWrite);
    uint8_t const idxRegTmp = iemNativeRegAllocTmp(pReNative, &off);
    uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off,
                                                                          IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegSrc),
                                                                          kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_Calculation);
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);

    /*
     * See https://community.arm.com/arm-community-blogs/b/infrastructure-solutions-blog/posts/porting-x86-vector-bitmask-optimizations-to-arm-neon
     * for different approaches as NEON doesn't has an instruction equivalent for pmovmskb, so we have to emulate that.
     *
     * As there is no way around emulating the exact semantics of pmovmskb we will use the same algorithm
     * as the sse2neon implementation because there we can get away with loading any constants and the
     * base algorithm is only 4 NEON instructions (+ 3 for extracting the result to a general register).
     *
     * The following illustrates the algorithm:
     *
     *     Byte vector Element ->   15       14       13       12       11       10        9        8        7        6       5        4         3        2        1        0
     *     Instruction
     *          |
     *          V
     *                           Axxxxxxx Bxxxxxxx Cxxxxxxx Dxxxxxxx Exxxxxxx Fxxxxxxx Gxxxxxxx Hxxxxxxx Ixxxxxxx Jxxxxxxx Kxxxxxxx Lxxxxxxx Mxxxxxxx Nxxxxxxx Oxxxxxxx Pxxxxxxx
     *     USHR v.16B, v.16B, #7 0000000A 0000000B 0000000C 0000000D 0000000E 0000000F 0000000G 0000000H 0000000I 0000000J 0000000K 0000000L 0000000M 0000000N 0000000O 0000000P
     *     USRA v.8H,  v.8H,  #7 00000000 000000AB 00000000 000000CD 00000000 000000EF 00000000 000000GH 00000000 000000IJ 00000000 000000KL 00000000 000000MN 00000000 000000OP
     *     USRA v.4S,  v.4S, #14 00000000 00000000 00000000 0000ABCD 00000000 00000000 00000000 0000EFGH 00000000 00000000 00000000 0000IJKL 00000000 00000000 00000000 0000MNOP
     *     USRA v.2D,  v.2D, #28 00000000 00000000 00000000 00000000 00000000 00000000 00000000 ABCDEFGH 00000000 00000000 00000000 00000000 00000000 00000000 00000000 IJKLMNOP
     *
     * The extraction process
     *     UMOV wTMP,  v.16B[8]             00000000 00000000 00000000 00000000 00000000 00000000 00000000 ABCDEFGH
     *     UMOV wRES,  v.16B[0]             00000000 00000000 00000000 00000000 00000000 00000000 00000000 IJKLMNOP
     *     ORR  xRES, xRES, xTMP, LSL #8    00000000 00000000 00000000 00000000 00000000 00000000 ABCDEFGH IJKLMNOP
     */
    pCodeBuf[off++] = Armv8A64MkVecInstrShrImm(idxSimdRegSrc, idxSimdRegSrc,  7, kArmv8InstrShiftSz_U8);
    pCodeBuf[off++] = Armv8A64MkVecInstrShrImm(idxSimdRegSrc, idxSimdRegSrc,  7, kArmv8InstrShiftSz_U16, true /*fUnsigned*/, false /*fRound*/, true /*fAccum*/);
    pCodeBuf[off++] = Armv8A64MkVecInstrShrImm(idxSimdRegSrc, idxSimdRegSrc, 14, kArmv8InstrShiftSz_U32, true /*fUnsigned*/, false /*fRound*/, true /*fAccum*/);
    pCodeBuf[off++] = Armv8A64MkVecInstrShrImm(idxSimdRegSrc, idxSimdRegSrc, 28, kArmv8InstrShiftSz_U64, true /*fUnsigned*/, false /*fRound*/, true /*fAccum*/);
    pCodeBuf[off++] = Armv8A64MkVecInstrUmov(idxRegTmp, idxSimdRegSrc, 8, kArmv8InstrUmovInsSz_U8, false /*fDst64Bit*/);
    pCodeBuf[off++] = Armv8A64MkVecInstrUmov(idxRegDst, idxSimdRegSrc, 0, kArmv8InstrUmovInsSz_U8, false /*fDst64Bit*/);
    pCodeBuf[off++] = Armv8A64MkInstrOrr(idxRegDst, idxRegDst, idxRegTmp, true /*f64Bit*/, 8 /*offShift6*/);

    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    iemNativeRegFreeTmp(pReNative, idxRegTmp);
    iemNativeRegFreeTmp(pReNative, idxRegDst);

#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Common emitter for the PACKUSWB instructions - guest register / guest register variant.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_packuswb_rr_u128(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                               uint8_t const idxSimdGstRegDst, uint8_t const idxSimdGstRegSrc)
{
    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst),
                                                                          kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate);
    uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegSrc),
                                                                          kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ReadOnly);

#ifdef RT_ARCH_AMD64
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5);

    /* packuswb xmm, xmm */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (idxSimdRegDst >= 8 || idxSimdRegSrc >= 8)
        pCodeBuf[off++] =   (idxSimdRegSrc >= 8 ? X86_OP_REX_B : 0)
                          | (idxSimdRegDst >= 8 ? X86_OP_REX_R : 0);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0x67;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxSimdRegDst & 7, idxSimdRegSrc & 7);

#elif defined(RT_ARCH_ARM64)
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);

    pCodeBuf[off++] = Armv8A64MkVecInstrQxtn(kArmv8VecInstrQxtnOp_Sqxtun, false /*fUpper*/, idxSimdRegDst, idxSimdRegDst, kArmv8VecInstrArithSz_8);
    pCodeBuf[off++] = Armv8A64MkVecInstrQxtn(kArmv8VecInstrQxtnOp_Sqxtun, true  /*fUpper*/, idxSimdRegDst, idxSimdRegSrc, kArmv8VecInstrArithSz_8);

#else
# error "port me"
#endif

    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Common emitter for the PACKUSWB instructions - guest register / recompiler variable variant.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmit_packuswb_rv_u128(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                               uint8_t const idxSimdGstRegDst, uint8_t const idxVarSrc)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarSrc);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVarSrc, sizeof(RTUINT128U));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst),
                                                                          kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate);
    uint8_t const idxSimdRegSrc = iemNativeVarSimdRegisterAcquire(pReNative, idxVarSrc, &off, true /*fInitialized*/);


#ifdef RT_ARCH_AMD64
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5);

    /* packuswb xmm, xmm */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (idxSimdRegDst >= 8 || idxSimdRegSrc >= 8)
        pCodeBuf[off++] =   (idxSimdRegSrc >= 8 ? X86_OP_REX_B : 0)
                          | (idxSimdRegDst >= 8 ? X86_OP_REX_R : 0);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0x67;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxSimdRegDst & 7, idxSimdRegSrc & 7);

#elif defined(RT_ARCH_ARM64)
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);

    pCodeBuf[off++] = Armv8A64MkVecInstrQxtn(kArmv8VecInstrQxtnOp_Sqxtun, false /*fUpper*/, idxSimdRegDst, idxSimdRegDst, kArmv8VecInstrArithSz_8);
    pCodeBuf[off++] = Armv8A64MkVecInstrQxtn(kArmv8VecInstrQxtnOp_Sqxtun, true  /*fUpper*/, idxSimdRegDst, idxSimdRegSrc, kArmv8VecInstrArithSz_8);

#else
# error "port me"
#endif

    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxVarSrc);

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Common emitter for the pmov{s,z}x* instructions.
 */
#ifdef RT_ARCH_AMD64
# define IEMNATIVE_NATIVE_EMIT_PMOV_S_Z_U128(a_Instr, a_fArmUnsigned, a_ArmElemSz, a_bOpcX86) \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rr_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxSimdGstRegSrc) \
    { \
        if (idxSimdGstRegDst == idxSimdGstRegSrc) \
        { \
            uint8_t const idxSimdReg = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegSrc), \
                                                                                  kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
            PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 6); \
            pCodeBuf[off++] = X86_OP_PRF_SIZE_OP; \
            if (idxSimdReg >= 8) \
                pCodeBuf[off++] = (idxSimdReg >= 8 ? X86_OP_REX_B | X86_OP_REX_R : 0); \
            pCodeBuf[off++] = 0x0f; \
            pCodeBuf[off++] = 0x38; \
            pCodeBuf[off++] = (a_bOpcX86); \
            pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxSimdReg & 7, idxSimdReg & 7); \
            iemNativeSimdRegFreeTmp(pReNative, idxSimdReg); \
        } \
        else \
        { \
            uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegSrc), \
                                                                                  kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ReadOnly); \
            uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                                  kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForFullWrite); \
            PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 6); \
            pCodeBuf[off++] = X86_OP_PRF_SIZE_OP; \
            if (idxSimdRegDst >= 8 || idxSimdRegSrc >= 8) \
                pCodeBuf[off++] =   (idxSimdRegSrc >= 8 ? X86_OP_REX_B : 0) \
                                  | (idxSimdRegDst >= 8 ? X86_OP_REX_R : 0); \
            pCodeBuf[off++] = 0x0f; \
            pCodeBuf[off++] = 0x38; \
            pCodeBuf[off++] = (a_bOpcX86); \
            pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxSimdRegDst & 7, idxSimdRegSrc & 7); \
            iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
            iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc); \
        } \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rv_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxVarSrc) \
    { \
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForFullWrite); \
        uint8_t const idxRegSrc = iemNativeVarRegisterAcquireInited(pReNative, idxVarSrc, &off); \
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7 + 6); \
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP; /* Transfer value from GPR to temporary vector register using pinsrq. */ \
        pCodeBuf[off++] =   X86_OP_REX_W \
                          | (IEMNATIVE_SIMD_REG_FIXED_TMP0 < 8 ? 0 : X86_OP_REX_R) \
                          | (idxRegSrc < 8 ? 0 : X86_OP_REX_B); \
        pCodeBuf[off++] = 0x0f; \
        pCodeBuf[off++] = 0x3a; \
        pCodeBuf[off++] = 0x22; \
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, IEMNATIVE_SIMD_REG_FIXED_TMP0 & 7, idxRegSrc & 7); \
        pCodeBuf[off++] = 0; /* QWord */\
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP; \
        if (idxSimdRegDst >= 8 || IEMNATIVE_SIMD_REG_FIXED_TMP0 >= 8) \
            pCodeBuf[off++] =   (IEMNATIVE_SIMD_REG_FIXED_TMP0 >= 8 ? X86_OP_REX_B : 0) \
                              | (idxSimdRegDst >= 8 ? X86_OP_REX_R : 0); \
        pCodeBuf[off++] = 0x0f; \
        pCodeBuf[off++] = 0x38; \
        pCodeBuf[off++] = (a_bOpcX86); \
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxSimdRegDst & 7, IEMNATIVE_SIMD_REG_FIXED_TMP0 & 7); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
        iemNativeVarRegisterRelease(pReNative, idxVarSrc); \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    typedef int ignore_semicolon
#elif defined(RT_ARCH_ARM64)
# define IEMNATIVE_NATIVE_EMIT_PMOV_S_Z_U128(a_Instr, a_fArmUnsigned, a_ArmElemSz, a_bOpcX86) \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rr_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxSimdGstRegSrc) \
    { \
        if (idxSimdGstRegDst == idxSimdGstRegSrc) \
        { \
            uint8_t const idxSimdReg = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegSrc), \
                                                                               kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForUpdate); \
            PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1); \
            pCodeBuf[off++] = Armv8A64MkVecInstrUShll(idxSimdReg, idxSimdReg, 0, (a_ArmElemSz), (a_fArmUnsigned)); \
            iemNativeSimdRegFreeTmp(pReNative, idxSimdReg); \
        } \
        else \
        { \
            uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegSrc), \
                                                                                  kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ReadOnly); \
            uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                                  kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForFullWrite); \
            PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1); \
            pCodeBuf[off++] = Armv8A64MkVecInstrUShll(idxSimdRegDst, idxSimdRegSrc, 0, (a_ArmElemSz), (a_fArmUnsigned)); \
            iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
            iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc); \
        } \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    DECL_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rv_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxVarSrc) \
    { \
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst), \
                                                                              kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForFullWrite); \
        uint8_t const idxRegSrc = iemNativeVarRegisterAcquireInited(pReNative, idxVarSrc, &off); \
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2); \
        pCodeBuf[off++] = Armv8A64MkVecInstrIns(IEMNATIVE_SIMD_REG_FIXED_TMP0, idxRegSrc, 0 /*idxElem*/); /* Transfer value from GPR to temporary vector register. */ \
        pCodeBuf[off++] = Armv8A64MkVecInstrUShll(idxSimdRegDst, IEMNATIVE_SIMD_REG_FIXED_TMP0, 0, (a_ArmElemSz), (a_fArmUnsigned)); \
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst); \
        iemNativeVarRegisterRelease(pReNative, idxVarSrc); \
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off); \
        return off; \
    } \
    typedef int ignore_semicolon
#else
# error "Port me"
#endif

IEMNATIVE_NATIVE_EMIT_PMOV_S_Z_U128(pmovzxbw, true,  kArmv8InstrShiftSz_U8,  0x30);
IEMNATIVE_NATIVE_EMIT_PMOV_S_Z_U128(pmovzxwd, true,  kArmv8InstrShiftSz_U16, 0x33);
IEMNATIVE_NATIVE_EMIT_PMOV_S_Z_U128(pmovzxdq, true,  kArmv8InstrShiftSz_U32, 0x35);

IEMNATIVE_NATIVE_EMIT_PMOV_S_Z_U128(pmovsxbw, false, kArmv8InstrShiftSz_U8,  0x20);
IEMNATIVE_NATIVE_EMIT_PMOV_S_Z_U128(pmovsxwd, false, kArmv8InstrShiftSz_U16, 0x23);
IEMNATIVE_NATIVE_EMIT_PMOV_S_Z_U128(pmovsxdq, false, kArmv8InstrShiftSz_U32, 0x25);


/**
 * Updates the MXCSR exception flags, raising any unmasked exceptions.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitMxcsrUpdate(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t const idxInstr, uint8_t const idxSimdGstRegDst, uint8_t const idxSimdRegRes)
{
    uint8_t const idxRegMxCsr = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_MxCsr, kIemNativeGstRegUse_ForUpdate);
    uint8_t const idxRegMxCsrXcptFlags = iemNativeRegAllocTmp(pReNative, &off);
    uint8_t const idxRegTmp = iemNativeRegAllocTmp(pReNative, &off);

#ifdef RT_ARCH_AMD64
    PIEMNATIVEINSTR pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);

    /* stmxcsr */
    if (IEMNATIVE_REG_FIXED_PVMCPU >= 8)
        pbCodeBuf[off++] = X86_OP_REX_B;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xae;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM4, 3, IEMNATIVE_REG_FIXED_PVMCPU & 7);
    pbCodeBuf[off++] = RT_BYTE1(RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));
    pbCodeBuf[off++] = RT_BYTE2(RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));
    pbCodeBuf[off++] = RT_BYTE3(RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));
    pbCodeBuf[off++] = RT_BYTE4(RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));

    /* Load MXCSR, mask everything except status flags and or into guest MXCSR. */
    off = iemNativeEmitLoadGprFromVCpuU32(pReNative, off, idxRegTmp, RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));

    /* Store the flags in the MXCSR xcpt flags register. */
    off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxRegMxCsrXcptFlags, idxRegTmp);
    off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxRegMxCsrXcptFlags, X86_MXCSR_XCPT_FLAGS);

    /* Clear the status flags in the temporary copy and write it back to MXCSR. */
    off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxRegTmp, ~X86_MXCSR_XCPT_FLAGS);
    off = iemNativeEmitStoreGprToVCpuU32(pReNative, off, idxRegTmp, RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));

    pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);

    /* ldmxcsr */
    if (IEMNATIVE_REG_FIXED_PVMCPU >= 8)
        pbCodeBuf[off++] = X86_OP_REX_B;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xae;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM4, 2, IEMNATIVE_REG_FIXED_PVMCPU & 7);
    pbCodeBuf[off++] = RT_BYTE1(RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));
    pbCodeBuf[off++] = RT_BYTE2(RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));
    pbCodeBuf[off++] = RT_BYTE3(RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));
    pbCodeBuf[off++] = RT_BYTE4(RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));

#elif defined(RT_ARCH_ARM64)
    PIEMNATIVEINSTR pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
    pu32CodeBuf[off++] = Armv8A64MkInstrMrs(idxRegMxCsrXcptFlags, ARMV8_AARCH64_SYSREG_FPSR);
    pu32CodeBuf[off++] = Armv8A64MkInstrMsr(ARMV8_A64_REG_XZR, ARMV8_AARCH64_SYSREG_FPSR);      /* Clear FPSR for next instruction. */
    pu32CodeBuf[off++] = Armv8A64MkInstrUxtb(idxRegMxCsrXcptFlags, idxRegMxCsrXcptFlags);       /* Ensure there are only the exception flags set (clears QC, and any possible NZCV flags). */

    /*
     * The exception flags layout differs between MXCSR and FPSR of course:
     *
     * Bit  FPSR        MXCSR
     *  0   IOC  ------> IE
     *
     *  1   DZC  ----    DE <-+
     *               \        |
     *  2   OFC  ---  -> ZE   |
     *              \         |
     *  3   UFC  --  --> OE   |
     *             \          |
     *  4   IXC  -  ---> UE   |
     *            \           |
     *  5          ----> PE   |
     *  6                     |
     *  7   IDC --------------+
     */
    pu32CodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegTmp, idxRegMxCsrXcptFlags, 1);    /* Shift the block of flags starting at DZC to the least significant bits. */
    pu32CodeBuf[off++] = Armv8A64MkInstrBfi(idxRegMxCsrXcptFlags, idxRegTmp, 2, 4);    /* Insert DZC, OFC, UFC and IXC into the MXCSR positions. */
    pu32CodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegTmp, idxRegMxCsrXcptFlags, 6);    /* Shift IDC (now at 6) into the LSB. */
    pu32CodeBuf[off++] = Armv8A64MkInstrBfi(idxRegMxCsrXcptFlags, idxRegTmp, 1, 1);    /* Insert IDC into the MXCSR positions. */
#else
# error "Port me"
#endif

    /*
     * If PE is set together with OE/UE and neither are masked
     * PE needs to be cleared, because on real hardware
     * an exception is generated with only OE/UE being set,
     * but because we mask all exceptions PE will get set as well.
     */
    /** @todo On ARM we can combine the load+and into one and instruction. */
    /** @todo r=aeichner Can this be done more optimal? */
    uint8_t const idxRegTmp2 = iemNativeRegAllocTmp(pReNative, &off);
    off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxRegTmp, idxRegMxCsrXcptFlags);
    off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxRegTmp, X86_MXCSR_OE | X86_MXCSR_UE);
    off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxRegTmp2, idxRegMxCsr);
    off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxRegTmp2, X86_MXCSR_OM | X86_MXCSR_UM);
    off = iemNativeEmitShiftGprRight(pReNative, off, idxRegTmp2, X86_MXCSR_XCPT_MASK_SHIFT);
    off = iemNativeEmitInvBitsGpr(pReNative, off, idxRegTmp2, idxRegTmp2, false /*f64Bit*/);
    off = iemNativeEmitAndGpr32ByGpr32(pReNative, off, idxRegTmp2, idxRegTmp);
    off = iemNativeEmitTestAnyBitsInGpr(pReNative, off, idxRegTmp2, X86_MXCSR_OE | X86_MXCSR_UE);

    uint32_t offFixup = off;
    off = iemNativeEmitJzToFixed(pReNative, off, off);
    off = iemNativeEmitBitClearInGpr32(pReNative, off, idxRegMxCsrXcptFlags, X86_MXCSR_PE_BIT);
    iemNativeFixupFixedJump(pReNative, offFixup, off);
    iemNativeRegFreeTmp(pReNative, idxRegTmp2);


    /* Set the MXCSR flags now. */
    off = iemNativeEmitOrGpr32ByGpr(pReNative, off, idxRegMxCsr, idxRegMxCsrXcptFlags);

    /*
     * Make sure we don't have any outstanding guest register writes as we may
     * raise an \#UD or \#XF and all guest register must be up to date in CPUMCTX.
     */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

    /* Check whether an exception is pending and only update the guest SIMD register if it isn't. */
    /* mov tmp, varmxcsr */
    off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxRegTmp, idxRegMxCsr);
    /* tmp >>= X86_MXCSR_XCPT_MASK_SHIFT */
    off = iemNativeEmitShiftGprRight(pReNative, off, idxRegTmp, X86_MXCSR_XCPT_MASK_SHIFT);
    /* tmp = ~tmp */
    off = iemNativeEmitInvBitsGpr(pReNative, off, idxRegTmp, idxRegTmp, false /*f64Bit*/);
    /* tmp &= mxcsr */
    off = iemNativeEmitAndGpr32ByGpr32(pReNative, off, idxRegMxCsrXcptFlags, idxRegTmp);
    off = iemNativeEmitTbExitIfAnyBitsSetInGpr<kIemNativeLabelType_RaiseSseAvxFpRelated>(pReNative, off, idxRegMxCsrXcptFlags,
                                                                                         X86_MXCSR_XCPT_FLAGS);

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst),
                                                                          kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ForFullWrite);

    /* Move result to guest SIMD register (at this point there is no exception being raised). */
    off = iemNativeEmitSimdLoadVecRegFromVecRegU128(pReNative, off, idxSimdRegDst, idxSimdRegRes);

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeRegFreeTmp(pReNative, idxRegTmp);
    iemNativeRegFreeTmp(pReNative, idxRegMxCsrXcptFlags);
    iemNativeRegFreeTmp(pReNative, idxRegMxCsr);
    return off;
}


/**
 * Common emitter for packed floating point instructions with 3 operands - register, register variant.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitSimdFp3OpCommon_rr_u128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t const idxInstr,
                                                                 uint8_t const idxSimdGstRegDst, uint8_t const idxSimdGstRegSrc,
#ifdef RT_ARCH_AMD64
                                                                 uint8_t const bPrefixX86, uint8_t const bOpcX86
#elif defined(RT_ARCH_ARM64)
                                                                 ARMV8INSTRVECFPOP const enmFpOp, ARMV8INSTRVECFPSZ const enmFpSz
#endif
                                                                 )
{
    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst),
                                                                         kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ReadOnly);
    uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegSrc),
                                                                         kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ReadOnly);

#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdLoadVecRegFromVecRegU128(pReNative, off, IEMNATIVE_SIMD_REG_FIXED_TMP0, idxSimdRegDst);
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5);
    if (bPrefixX86 != 0)
        pCodeBuf[off++] = bPrefixX86;
    if (IEMNATIVE_SIMD_REG_FIXED_TMP0 >= 8 || idxSimdRegSrc >= 8)
        pCodeBuf[off++] =   (idxSimdRegSrc >= 8 ? X86_OP_REX_B : 0)
                          | (IEMNATIVE_SIMD_REG_FIXED_TMP0 >= 8 ? X86_OP_REX_R : 0);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = bOpcX86;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, IEMNATIVE_SIMD_REG_FIXED_TMP0 & 7, idxSimdRegSrc & 7);
#elif defined(RT_ARCH_ARM64)
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pCodeBuf[off++] = Armv8A64MkVecInstrFp3Op(enmFpOp, enmFpSz, IEMNATIVE_SIMD_REG_FIXED_TMP0, idxSimdRegDst, idxSimdRegSrc);
#else
# error "Port me"
#endif
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return iemNativeEmitMxcsrUpdate(pReNative, off, idxInstr, idxSimdGstRegDst, IEMNATIVE_SIMD_REG_FIXED_TMP0);
}


/**
 * Common emitter for packed floating point instructions with 3 operands - register, local variable variant.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitSimdFp3OpCommon_rv_u128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t const idxInstr,
                                                                 uint8_t const idxSimdGstRegDst, uint8_t const idxVarSrc,
#ifdef RT_ARCH_AMD64
                                                                 uint8_t const bPrefixX86, uint8_t const bOpcX86
#elif defined(RT_ARCH_ARM64)
                                                                 ARMV8INSTRVECFPOP const enmFpOp, ARMV8INSTRVECFPSZ const enmFpSz
#endif
                                                                 )
{
    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(idxSimdGstRegDst),
                                                                         kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ReadOnly);
    uint8_t const idxSimdRegSrc = iemNativeVarSimdRegisterAcquire(pReNative, idxVarSrc, &off, true /*fInitialized*/);

#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdLoadVecRegFromVecRegU128(pReNative, off, IEMNATIVE_SIMD_REG_FIXED_TMP0, idxSimdRegDst);
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5);
    if (bPrefixX86 != 0)
        pCodeBuf[off++] = bPrefixX86;
    if (IEMNATIVE_SIMD_REG_FIXED_TMP0 >= 8 || idxSimdRegSrc >= 8)
        pCodeBuf[off++] =   (idxSimdRegSrc >= 8 ? X86_OP_REX_B : 0)
                          | (IEMNATIVE_SIMD_REG_FIXED_TMP0 >= 8 ? X86_OP_REX_R : 0);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = bOpcX86;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, IEMNATIVE_SIMD_REG_FIXED_TMP0 & 7, idxSimdRegSrc & 7);
#elif defined(RT_ARCH_ARM64)
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pCodeBuf[off++] = Armv8A64MkVecInstrFp3Op(enmFpOp, enmFpSz, IEMNATIVE_SIMD_REG_FIXED_TMP0, idxSimdRegDst, idxSimdRegSrc);
#else
# error "Port me"
#endif
    iemNativeVarRegisterRelease(pReNative, idxVarSrc);
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return iemNativeEmitMxcsrUpdate(pReNative, off, idxInstr, idxSimdGstRegDst, IEMNATIVE_SIMD_REG_FIXED_TMP0);
}


/**
 * Common emitter for packed floating point instructions with 3 operands.
 */
#ifdef RT_ARCH_AMD64
# define IEMNATIVE_NATIVE_EMIT_FP_3OP_U128(a_Instr, a_enmArmOp, a_ArmElemSz, a_bPrefixX86, a_bOpcX86) \
    DECL_FORCE_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rr_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t const idxInstr, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxSimdGstRegSrc) \
    { \
        return iemNativeEmitSimdFp3OpCommon_rr_u128(pReNative, off, idxInstr, idxSimdGstRegDst, idxSimdGstRegSrc, \
                                                    a_bPrefixX86, a_bOpcX86); \
    } \
    DECL_FORCE_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rv_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t const idxInstr, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxVarSrc) \
    { \
        return iemNativeEmitSimdFp3OpCommon_rv_u128(pReNative, off, idxInstr, idxSimdGstRegDst, idxVarSrc, \
                                                    a_bPrefixX86, a_bOpcX86); \
    } \
    typedef int ignore_semicolon
#elif defined(RT_ARCH_ARM64)
# define IEMNATIVE_NATIVE_EMIT_FP_3OP_U128(a_Instr, a_enmArmOp, a_ArmElemSz, a_bPrefixX86, a_bOpcX86) \
    DECL_FORCE_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rr_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t const idxInstr, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxSimdGstRegSrc) \
    { \
        return iemNativeEmitSimdFp3OpCommon_rr_u128(pReNative, off, idxInstr, idxSimdGstRegDst, idxSimdGstRegSrc, \
                                                    a_enmArmOp, a_ArmElemSz); \
    } \
    DECL_FORCE_INLINE_THROW(uint32_t) \
    RT_CONCAT3(iemNativeEmit_,a_Instr,_rv_u128)(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t const idxInstr, \
                                                uint8_t const idxSimdGstRegDst, uint8_t const idxVarSrc) \
    { \
        return iemNativeEmitSimdFp3OpCommon_rv_u128(pReNative, off, idxInstr, idxSimdGstRegDst, idxVarSrc, \
                                                    a_enmArmOp, a_ArmElemSz); \
    } \
    typedef int ignore_semicolon
#else
# error "Port me"
#endif


IEMNATIVE_NATIVE_EMIT_FP_3OP_U128(mulps, kArmv8VecInstrFpOp_Mul, kArmv8VecInstrFpSz_4x_Single, 0, 0x59);
IEMNATIVE_NATIVE_EMIT_FP_3OP_U128(addps, kArmv8VecInstrFpOp_Add, kArmv8VecInstrFpSz_4x_Single, 0, 0x58);
IEMNATIVE_NATIVE_EMIT_FP_3OP_U128(addpd, kArmv8VecInstrFpOp_Add, kArmv8VecInstrFpSz_2x_Double, X86_OP_PRF_SIZE_OP, 0x58);
IEMNATIVE_NATIVE_EMIT_FP_3OP_U128(subps, kArmv8VecInstrFpOp_Sub, kArmv8VecInstrFpSz_4x_Single, 0, 0x5c);


#endif /* !VMM_INCLUDED_SRC_VMMAll_target_x86_IEMAllN8veEmit_x86_h */
