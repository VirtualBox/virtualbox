/* $Id: IEMN8veRecompilerEmit.h 107185 2024-11-26 22:43:27Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - Native Recompiler Inlined Emitters.
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

#ifndef VMM_INCLUDED_SRC_include_IEMN8veRecompilerEmit_h
#define VMM_INCLUDED_SRC_include_IEMN8veRecompilerEmit_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "IEMN8veRecompiler.h"


/** @defgroup grp_iem_n8ve_re_inline    Native Recompiler Inlined Emitters
 * @ingroup grp_iem_n8ve_re
 * @{
 */

/**
 * Emit a simple marker instruction to more easily tell where something starts
 * in the disassembly.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitMarker(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t uInfo)
{
#ifdef RT_ARCH_AMD64
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
    if (uInfo == 0)
    {
        /* nop */
        pbCodeBuf[off++] = 0x90;
    }
    else
    {
        /* nop [disp32] */
        pbCodeBuf[off++] = 0x0f;
        pbCodeBuf[off++] = 0x1f;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM0, 0, 5);
        pbCodeBuf[off++] = RT_BYTE1(uInfo);
        pbCodeBuf[off++] = RT_BYTE2(uInfo);
        pbCodeBuf[off++] = RT_BYTE3(uInfo);
        pbCodeBuf[off++] = RT_BYTE4(uInfo);
    }
#elif defined(RT_ARCH_ARM64)
    /* nop */
    uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    if (uInfo == 0)
        pu32CodeBuf[off++] = ARMV8_A64_INSTR_NOP;
    else
        pu32CodeBuf[off++] = Armv8A64MkInstrMovZ(ARMV8_A64_REG_XZR, (uint16_t)uInfo);

    RT_NOREF(uInfo);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emit a breakpoint instruction.
 */
DECL_FORCE_INLINE(uint32_t) iemNativeEmitBrkEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint32_t uInfo)
{
#ifdef RT_ARCH_AMD64
    pCodeBuf[off++] = 0xcc;
    RT_NOREF(uInfo);   /** @todo use multibyte nop for info? */

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrBrk(uInfo & UINT32_C(0xffff));

#else
# error "error"
#endif
    return off;
}


/**
 * Emit a breakpoint instruction.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitBrk(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t uInfo)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitBrkEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, uInfo);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitBrkEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, uInfo);
#else
# error "error"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/*********************************************************************************************************************************
*   Loads, Stores and Related Stuff.                                                                                             *
*********************************************************************************************************************************/

#ifdef RT_ARCH_AMD64
/**
 * Common bit of iemNativeEmitLoadGprByGpr and friends.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitGprByGprDisp(uint8_t *pbCodeBuf, uint32_t off, uint8_t iGprReg, uint8_t iGprBase, int32_t offDisp)
{
    if (offDisp == 0 && (iGprBase & 7) != X86_GREG_xBP) /* Can use encoding w/o displacement field. */
    {
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM0, iGprReg & 7, iGprBase & 7);
        if ((iGprBase & 7) == X86_GREG_xSP) /* for RSP/R12 relative addressing we have to use a SIB byte. */
            pbCodeBuf[off++] = X86_SIB_MAKE(X86_GREG_xSP, X86_GREG_xSP, 0); /* -> [RSP/R12] */
    }
    else if (offDisp == (int8_t)offDisp)
    {
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM1, iGprReg & 7, iGprBase & 7);
        if ((iGprBase & 7) == X86_GREG_xSP) /* for RSP/R12 relative addressing we have to use a SIB byte. */
            pbCodeBuf[off++] = X86_SIB_MAKE(X86_GREG_xSP, X86_GREG_xSP, 0); /* -> [RSP/R12] */
        pbCodeBuf[off++] = (uint8_t)offDisp;
    }
    else
    {
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM4, iGprReg & 7, iGprBase & 7);
        if ((iGprBase & 7) == X86_GREG_xSP) /* for RSP/R12 relative addressing we have to use a SIB byte. */
            pbCodeBuf[off++] = X86_SIB_MAKE(X86_GREG_xSP, X86_GREG_xSP, 0); /* -> [RSP/R12] */
        pbCodeBuf[off++] = RT_BYTE1((uint32_t)offDisp);
        pbCodeBuf[off++] = RT_BYTE2((uint32_t)offDisp);
        pbCodeBuf[off++] = RT_BYTE3((uint32_t)offDisp);
        pbCodeBuf[off++] = RT_BYTE4((uint32_t)offDisp);
    }
    return off;
}
#endif /* RT_ARCH_AMD64 */

/**
 * Emits setting a GPR to zero.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitGprZero(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr)
{
#ifdef RT_ARCH_AMD64
    /* xor gpr32, gpr32 */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3);
    if (iGpr >= 8)
        pbCodeBuf[off++] = X86_OP_REX_R | X86_OP_REX_B;
    pbCodeBuf[off++] = 0x33;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGpr & 7, iGpr & 7);

#elif defined(RT_ARCH_ARM64)
    /* mov gpr, #0x0 */
    uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = UINT32_C(0xd2800000) | iGpr;

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Variant of iemNativeEmitLoadGpr32Imm where the caller ensures sufficent
 * buffer space.
 *
 * Max buffer consumption:
 *      - AMD64: 6 instruction bytes.
 *      - ARM64: 2 instruction words (8 bytes).
 *
 * @note The top 32 bits will be cleared.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitLoadGpr32ImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGpr, uint32_t uImm32)
{
#ifdef RT_ARCH_AMD64
    if (uImm32 == 0)
    {
        /* xor gpr, gpr */
        if (iGpr >= 8)
            pCodeBuf[off++] = X86_OP_REX_R | X86_OP_REX_B;
        pCodeBuf[off++] = 0x33;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGpr & 7, iGpr & 7);
    }
    else
    {
        /* mov gpr, imm32 */
        if (iGpr >= 8)
            pCodeBuf[off++] = X86_OP_REX_B;
        pCodeBuf[off++] = 0xb8 + (iGpr & 7);
        pCodeBuf[off++] = RT_BYTE1(uImm32);
        pCodeBuf[off++] = RT_BYTE2(uImm32);
        pCodeBuf[off++] = RT_BYTE3(uImm32);
        pCodeBuf[off++] = RT_BYTE4(uImm32);
    }

#elif defined(RT_ARCH_ARM64)
    if ((uImm32 >> 16) == 0)
        /* movz gpr, imm16 */
        pCodeBuf[off++] = Armv8A64MkInstrMovZ(iGpr, uImm32,                    0, false /*f64Bit*/);
    else if ((uImm32 & UINT32_C(0xffff)) == 0)
        /* movz gpr, imm16, lsl #16 */
        pCodeBuf[off++] = Armv8A64MkInstrMovZ(iGpr, uImm32 >> 16,              1, false /*f64Bit*/);
    else if ((uImm32 & UINT32_C(0xffff)) == UINT32_C(0xffff))
        /* movn gpr, imm16, lsl #16 */
        pCodeBuf[off++] = Armv8A64MkInstrMovN(iGpr, ~uImm32 >> 16,             1, false /*f64Bit*/);
    else if ((uImm32 >> 16) == UINT32_C(0xffff))
        /* movn gpr, imm16 */
        pCodeBuf[off++] = Armv8A64MkInstrMovN(iGpr, ~uImm32,                   0, false /*f64Bit*/);
    else
    {
        pCodeBuf[off++] = Armv8A64MkInstrMovZ(iGpr, uImm32 & UINT32_C(0xffff), 0, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrMovK(iGpr, uImm32 >> 16,              1, false /*f64Bit*/);
    }

#else
# error "port me"
#endif
    return off;
}


/**
 * Variant of iemNativeEmitLoadGpr32Imm where the caller ensures sufficent
 * buffer space.
 *
 * Max buffer consumption:
 *      - AMD64: 6 instruction bytes.
 *      - ARM64: 2 instruction words (8 bytes).
 *
 * @note The top 32 bits will be cleared.
 */
template<uint32_t const a_uImm32>
DECL_FORCE_INLINE(uint32_t) iemNativeEmitLoadGpr32ImmExT(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGpr)
{
#ifdef RT_ARCH_AMD64
    if (a_uImm32 == 0)
    {
        /* xor gpr, gpr */
        if (iGpr >= 8)
            pCodeBuf[off++] = X86_OP_REX_R | X86_OP_REX_B;
        pCodeBuf[off++] = 0x33;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGpr & 7, iGpr & 7);
    }
    else
    {
        /* mov gpr, imm32 */
        if (iGpr >= 8)
            pCodeBuf[off++] = X86_OP_REX_B;
        pCodeBuf[off++] = 0xb8 + (iGpr & 7);
        pCodeBuf[off++] = RT_BYTE1(a_uImm32);
        pCodeBuf[off++] = RT_BYTE2(a_uImm32);
        pCodeBuf[off++] = RT_BYTE3(a_uImm32);
        pCodeBuf[off++] = RT_BYTE4(a_uImm32);
    }

#elif defined(RT_ARCH_ARM64)
    if RT_CONSTEXPR_IF((a_uImm32 >> 16) == 0)
        /* movz gpr, imm16 */
        pCodeBuf[off++] = Armv8A64MkInstrMovZ(iGpr, a_uImm32,                    0, false /*f64Bit*/);
    else if RT_CONSTEXPR_IF((a_uImm32 & UINT32_C(0xffff)) == 0)
        /* movz gpr, imm16, lsl #16 */
        pCodeBuf[off++] = Armv8A64MkInstrMovZ(iGpr, a_uImm32 >> 16,              1, false /*f64Bit*/);
    else if RT_CONSTEXPR_IF((a_uImm32 & UINT32_C(0xffff)) == UINT32_C(0xffff))
        /* movn gpr, imm16, lsl #16 */
        pCodeBuf[off++] = Armv8A64MkInstrMovN(iGpr, ~a_uImm32 >> 16,             1, false /*f64Bit*/);
    else if RT_CONSTEXPR_IF((a_uImm32 >> 16) == UINT32_C(0xffff))
        /* movn gpr, imm16 */
        pCodeBuf[off++] = Armv8A64MkInstrMovN(iGpr, ~a_uImm32,                   0, false /*f64Bit*/);
    else
    {
        pCodeBuf[off++] = Armv8A64MkInstrMovZ(iGpr, a_uImm32 & UINT32_C(0xffff), 0, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrMovK(iGpr, a_uImm32 >> 16,              1, false /*f64Bit*/);
    }

#else
# error "port me"
#endif
    return off;
}


/**
 * Variant of iemNativeEmitLoadGprImm64 where the caller ensures sufficent
 * buffer space.
 *
 * Max buffer consumption:
 *      - AMD64: 10 instruction bytes.
 *      - ARM64: 4 instruction words (16 bytes).
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitLoadGprImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGpr, uint64_t uImm64)
{
#ifdef RT_ARCH_AMD64
    if (uImm64 == 0)
    {
        /* xor gpr, gpr */
        if (iGpr >= 8)
            pCodeBuf[off++] = X86_OP_REX_R | X86_OP_REX_B;
        pCodeBuf[off++] = 0x33;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGpr & 7, iGpr & 7);
    }
    else if (uImm64 <= UINT32_MAX)
    {
        /* mov gpr, imm32 */
        if (iGpr >= 8)
            pCodeBuf[off++] = X86_OP_REX_B;
        pCodeBuf[off++] = 0xb8 + (iGpr & 7);
        pCodeBuf[off++] = RT_BYTE1(uImm64);
        pCodeBuf[off++] = RT_BYTE2(uImm64);
        pCodeBuf[off++] = RT_BYTE3(uImm64);
        pCodeBuf[off++] = RT_BYTE4(uImm64);
    }
    else if (uImm64 == (uint64_t)(int32_t)uImm64)
    {
        /* mov gpr, sx(imm32) */
        if (iGpr < 8)
            pCodeBuf[off++] = X86_OP_REX_W;
        else
            pCodeBuf[off++] = X86_OP_REX_W | X86_OP_REX_B;
        pCodeBuf[off++] = 0xc7;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGpr & 7);
        pCodeBuf[off++] = RT_BYTE1(uImm64);
        pCodeBuf[off++] = RT_BYTE2(uImm64);
        pCodeBuf[off++] = RT_BYTE3(uImm64);
        pCodeBuf[off++] = RT_BYTE4(uImm64);
    }
    else
    {
        /* mov gpr, imm64 */
        if (iGpr < 8)
            pCodeBuf[off++] = X86_OP_REX_W;
        else
            pCodeBuf[off++] = X86_OP_REX_W | X86_OP_REX_B;
        pCodeBuf[off++] = 0xb8 + (iGpr & 7);
        pCodeBuf[off++] = RT_BYTE1(uImm64);
        pCodeBuf[off++] = RT_BYTE2(uImm64);
        pCodeBuf[off++] = RT_BYTE3(uImm64);
        pCodeBuf[off++] = RT_BYTE4(uImm64);
        pCodeBuf[off++] = RT_BYTE5(uImm64);
        pCodeBuf[off++] = RT_BYTE6(uImm64);
        pCodeBuf[off++] = RT_BYTE7(uImm64);
        pCodeBuf[off++] = RT_BYTE8(uImm64);
    }

#elif defined(RT_ARCH_ARM64)
    /*
     * Quick simplification: Do 32-bit load if top half is zero.
     */
    if (uImm64 <= UINT32_MAX)
        return iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off, iGpr, (uint32_t)uImm64);

    /*
     * We need to start this sequence with a 'mov grp, imm16, lsl #x' and
     * supply remaining bits using 'movk grp, imm16, lsl #x'.
     *
     * The mov instruction is encoded 0xd2800000 + shift + imm16 + grp,
     * while the movk is 0xf2800000 + shift + imm16 + grp, meaning the diff
     * is 0x20000000 (bit 29). So, we keep this bit in a variable and set it
     * after the first non-zero immediate component so we switch to movk for
     * the remainder.
     */
    unsigned cZeroHalfWords = !( uImm64        & UINT16_MAX)
                            + !((uImm64 >> 16) & UINT16_MAX)
                            + !((uImm64 >> 32) & UINT16_MAX)
                            + !((uImm64 >> 48) & UINT16_MAX);
    unsigned cFfffHalfWords = cZeroHalfWords >= 2 ? 0 /* skip */
                            : ( (uImm64        & UINT16_MAX) == UINT16_MAX)
                            + (((uImm64 >> 16) & UINT16_MAX) == UINT16_MAX)
                            + (((uImm64 >> 32) & UINT16_MAX) == UINT16_MAX)
                            + (((uImm64 >> 48) & UINT16_MAX) == UINT16_MAX);
    if (cFfffHalfWords <= cZeroHalfWords)
    {
        uint32_t fMovBase = UINT32_C(0xd2800000) | iGpr;

        /* movz gpr, imm16 */
        uint32_t uImmPart = (uint32_t)((uImm64 >>  0) & UINT32_C(0xffff));
        if (uImmPart || cZeroHalfWords == 4)
        {
            pCodeBuf[off++] = fMovBase | (UINT32_C(0) << 21) | (uImmPart << 5);
            fMovBase |= RT_BIT_32(29);
        }
        /* mov[z/k] gpr, imm16, lsl #16 */
        uImmPart = (uint32_t)((uImm64 >> 16) & UINT32_C(0xffff));
        if (uImmPart)
        {
            pCodeBuf[off++] = fMovBase | (UINT32_C(1) << 21) | (uImmPart << 5);
            fMovBase |= RT_BIT_32(29);
        }
        /* mov[z/k] gpr, imm16, lsl #32 */
        uImmPart = (uint32_t)((uImm64 >> 32) & UINT32_C(0xffff));
        if (uImmPart)
        {
            pCodeBuf[off++] = fMovBase | (UINT32_C(2) << 21) | (uImmPart << 5);
            fMovBase |= RT_BIT_32(29);
        }
        /* mov[z/k] gpr, imm16, lsl #48 */
        uImmPart = (uint32_t)((uImm64 >> 48) & UINT32_C(0xffff));
        if (uImmPart)
            pCodeBuf[off++] = fMovBase | (UINT32_C(3) << 21) | (uImmPart << 5);
    }
    else
    {
        uint32_t fMovBase = UINT32_C(0x92800000) | iGpr;

        /* find the first half-word that isn't UINT16_MAX. */
        uint32_t const iHwNotFfff =  (uImm64        & UINT16_MAX) != UINT16_MAX ? 0
                                  : ((uImm64 >> 16) & UINT16_MAX) != UINT16_MAX ? 1
                                  : ((uImm64 >> 32) & UINT16_MAX) != UINT16_MAX ? 2 : 3;

        /* movn gpr, imm16, lsl #iHwNotFfff*16 */
        uint32_t uImmPart = (uint32_t)(~(uImm64 >> (iHwNotFfff * 16)) & UINT32_C(0xffff)) << 5;
        pCodeBuf[off++] = fMovBase | (iHwNotFfff << 21) | uImmPart;
        fMovBase |= RT_BIT_32(30) | RT_BIT_32(29); /* -> movk */
        /* movk gpr, imm16 */
        if (iHwNotFfff != 0)
        {
            uImmPart = (uint32_t)((uImm64 >>  0) & UINT32_C(0xffff));
            if (uImmPart != UINT32_C(0xffff))
                pCodeBuf[off++] = fMovBase | (UINT32_C(0) << 21) | (uImmPart << 5);
        }
        /* movk gpr, imm16, lsl #16 */
        if (iHwNotFfff != 1)
        {
            uImmPart = (uint32_t)((uImm64 >> 16) & UINT32_C(0xffff));
            if (uImmPart != UINT32_C(0xffff))
                pCodeBuf[off++] = fMovBase | (UINT32_C(1) << 21) | (uImmPart << 5);
        }
        /* movk gpr, imm16, lsl #32 */
        if (iHwNotFfff != 2)
        {
            uImmPart = (uint32_t)((uImm64 >> 32) & UINT32_C(0xffff));
            if (uImmPart != UINT32_C(0xffff))
                pCodeBuf[off++] = fMovBase | (UINT32_C(2) << 21) | (uImmPart << 5);
        }
        /* movk gpr, imm16, lsl #48 */
        if (iHwNotFfff != 3)
        {
            uImmPart = (uint32_t)((uImm64 >> 48) & UINT32_C(0xffff));
            if (uImmPart != UINT32_C(0xffff))
                pCodeBuf[off++] = fMovBase | (UINT32_C(3) << 21) | (uImmPart << 5);
        }
    }

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits loading a constant into a 64-bit GPR
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprImm64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr, uint64_t uImm64)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGprImmEx(iemNativeInstrBufEnsure(pReNative, off, 10), off, iGpr, uImm64);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitLoadGprImmEx(iemNativeInstrBufEnsure(pReNative, off, 4), off, iGpr, uImm64);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits loading a constant into a 32-bit GPR.
 * @note The top 32 bits will be cleared.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprImm32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr, uint32_t uImm32)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGpr32ImmEx(iemNativeInstrBufEnsure(pReNative, off, 6), off, iGpr, uImm32);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitLoadGpr32ImmEx(iemNativeInstrBufEnsure(pReNative, off, 2), off, iGpr, uImm32);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits loading a constant into a 8-bit GPR
 * @note The AMD64 version does *NOT* clear any bits in the 8..63 range,
 *       only the ARM64 version does that.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGpr8Imm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr, uint8_t uImm8)
{
#ifdef RT_ARCH_AMD64
    /* mov gpr, imm8 */
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3);
    if (iGpr >= 8)
        pbCodeBuf[off++] = X86_OP_REX_B;
    else if (iGpr >= 4)
        pbCodeBuf[off++] = X86_OP_REX;
    pbCodeBuf[off++] = 0xb0 + (iGpr & 7);
    pbCodeBuf[off++] = RT_BYTE1(uImm8);

#elif defined(RT_ARCH_ARM64)
    /* movz gpr, imm16, lsl #0 */
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = UINT32_C(0xd2800000) | (UINT32_C(0) << 21) | ((uint32_t)uImm8 << 5) | iGpr;

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


#ifdef RT_ARCH_AMD64
/**
 * Common bit of iemNativeEmitLoadGprFromVCpuU64 and friends.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitGprByVCpuDisp(uint8_t *pbCodeBuf, uint32_t off, uint8_t iGprReg, uint32_t offVCpu)
{
    if (offVCpu < 128)
    {
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM1, iGprReg & 7, IEMNATIVE_REG_FIXED_PVMCPU);
        pbCodeBuf[off++] = (uint8_t)(int8_t)offVCpu;
    }
    else
    {
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM4, iGprReg & 7, IEMNATIVE_REG_FIXED_PVMCPU);
        pbCodeBuf[off++] = RT_BYTE1((uint32_t)offVCpu);
        pbCodeBuf[off++] = RT_BYTE2((uint32_t)offVCpu);
        pbCodeBuf[off++] = RT_BYTE3((uint32_t)offVCpu);
        pbCodeBuf[off++] = RT_BYTE4((uint32_t)offVCpu);
    }
    return off;
}

/**
 * Special variant of iemNativeEmitGprByVCpuDisp for accessing the VM structure.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitGprByVCpuSignedDisp(uint8_t *pbCodeBuf, uint32_t off, uint8_t iGprReg, int32_t offVCpu)
{
    if (offVCpu < 128 && offVCpu >= -128)
    {
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM1, iGprReg & 7, IEMNATIVE_REG_FIXED_PVMCPU);
        pbCodeBuf[off++] = (uint8_t)(int8_t)offVCpu;
    }
    else
    {
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM4, iGprReg & 7, IEMNATIVE_REG_FIXED_PVMCPU);
        pbCodeBuf[off++] = RT_BYTE1((uint32_t)offVCpu);
        pbCodeBuf[off++] = RT_BYTE2((uint32_t)offVCpu);
        pbCodeBuf[off++] = RT_BYTE3((uint32_t)offVCpu);
        pbCodeBuf[off++] = RT_BYTE4((uint32_t)offVCpu);
    }
    return off;
}

#elif defined(RT_ARCH_ARM64)

/**
 * Common bit of iemNativeEmitLoadGprFromVCpuU64Ex and friends.
 *
 * @note Loads can use @a iGprReg for large offsets, stores requires a temporary
 *       registers (@a iGprTmp).
 * @note DON'T try this with prefetch.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitGprByVCpuLdStEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprReg, uint32_t offVCpu,
                             ARMV8A64INSTRLDSTTYPE enmOperation, unsigned cbData, uint8_t iGprTmp = UINT8_MAX)
{
    /*
     * There are a couple of ldr variants that takes an immediate offset, so
     * try use those if we can, otherwise we have to use the temporary register
     * help with the addressing.
     */
    if (offVCpu < _4K * cbData && !(offVCpu & (cbData - 1)))
        /* Use the unsigned variant of ldr Wt, [<Xn|SP>, #off]. */
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(enmOperation, iGprReg, IEMNATIVE_REG_FIXED_PVMCPU, offVCpu / cbData);
    else if (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx) < (unsigned)(_4K * cbData) && !(offVCpu & (cbData - 1)))
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(enmOperation, iGprReg, IEMNATIVE_REG_FIXED_PCPUMCTX,
                                                   (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx)) / cbData);
    else if (!ARMV8A64INSTRLDSTTYPE_IS_STORE(enmOperation) || iGprTmp != UINT8_MAX)
    {
        /* The offset is too large, so we must load it into a register and use
           ldr Wt, [<Xn|SP>, (<Wm>|<Xm>)]. */
        /** @todo reduce by offVCpu by >> 3 or >> 2? if it saves instructions? */
        if (iGprTmp == UINT8_MAX)
            iGprTmp = iGprReg;
        off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, iGprTmp, offVCpu);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(enmOperation, iGprReg, IEMNATIVE_REG_FIXED_PVMCPU, iGprTmp);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

    return off;
}

/**
 * Common bit of iemNativeEmitLoadGprFromVCpuU64 and friends.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitGprByVCpuLdSt(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprReg,
                           uint32_t offVCpu, ARMV8A64INSTRLDSTTYPE enmOperation, unsigned cbData)
{
    /*
     * There are a couple of ldr variants that takes an immediate offset, so
     * try use those if we can, otherwise we have to use the temporary register
     * help with the addressing.
     */
    if (offVCpu < _4K * cbData && !(offVCpu & (cbData - 1)))
    {
        /* Use the unsigned variant of ldr Wt, [<Xn|SP>, #off]. */
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRUOff(enmOperation, iGprReg, IEMNATIVE_REG_FIXED_PVMCPU, offVCpu / cbData);
    }
    else if (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx) < (unsigned)(_4K * cbData) && !(offVCpu & (cbData - 1)))
    {
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRUOff(enmOperation, iGprReg, IEMNATIVE_REG_FIXED_PCPUMCTX,
                                                      (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx)) / cbData);
    }
    else
    {
        /* The offset is too large, so we must load it into a register and use
           ldr Wt, [<Xn|SP>, (<Wm>|<Xm>)]. */
        /** @todo reduce by offVCpu by >> 3 or >> 2? if it saves instructions? */
        off = iemNativeEmitLoadGprImm64(pReNative, off, IEMNATIVE_REG_FIXED_TMP0, offVCpu);
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(enmOperation, iGprReg, IEMNATIVE_REG_FIXED_PVMCPU,
                                                       IEMNATIVE_REG_FIXED_TMP0);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Special variant of iemNativeEmitGprByVCpuLdStEx for accessing the VM
 * structure.
 *
 * @note Loads can use @a iGprReg for large offsets, stores requires a temporary
 *       registers (@a iGprTmp).
 * @note DON'T try this with prefetch.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitGprBySignedVCpuLdStEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprReg, int32_t offVCpu,
                                   ARMV8A64INSTRLDSTTYPE enmOperation, unsigned cbData, uint8_t iGprTmp = UINT8_MAX)
{
    Assert((uint32_t)RT_ABS(offVCpu) < RT_BIT_32(28)); /* we should be way out of range for problematic sign extending issues. */
    Assert(!((uint32_t)RT_ABS(offVCpu) & (cbData - 1)));

   /*
     * For negative offsets we need to use put the displacement in a register
     * as the two variants with signed immediates will either post or pre
     * increment the base address register.
     */
    if (!ARMV8A64INSTRLDSTTYPE_IS_STORE(enmOperation) || iGprTmp != UINT8_MAX)
    {
        uint8_t const idxIndexReg = !ARMV8A64INSTRLDSTTYPE_IS_STORE(enmOperation) ? iGprReg : IEMNATIVE_REG_FIXED_TMP0;
        off = iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off, idxIndexReg, offVCpu / (int32_t)cbData);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(enmOperation, iGprReg, IEMNATIVE_REG_FIXED_PVMCPU, idxIndexReg,
                                                    kArmv8A64InstrLdStExtend_Sxtw, cbData > 1 /*fShifted*/);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

    return off;
}

/**
 * Special variant of iemNativeEmitGprByVCpuLdSt for accessing the VM structure.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitGprBySignedVCpuLdSt(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprReg,
                                 int32_t offVCpu, ARMV8A64INSTRLDSTTYPE enmOperation, unsigned cbData)
{
    off = iemNativeEmitGprBySignedVCpuLdStEx(iemNativeInstrBufEnsure(pReNative, off, 2 + 1), off, iGprReg,
                                             offVCpu, enmOperation, cbData, IEMNATIVE_REG_FIXED_TMP0);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}

#endif /* RT_ARCH_ARM64 */


/**
 * Emits a 64-bit GPR load of a VCpu value.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromVCpuU64Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGpr, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* mov reg64, mem64 */
    if (iGpr < 8)
        pCodeBuf[off++] = X86_OP_REX_W;
    else
        pCodeBuf[off++] = X86_OP_REX_W | X86_OP_REX_R;
    pCodeBuf[off++] = 0x8b;
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, iGpr, offVCpu);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdStEx(pCodeBuf, off, iGpr, offVCpu, kArmv8A64InstrLdStType_Ld_Dword, sizeof(uint64_t));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 64-bit GPR load of a VCpu value.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromVCpuU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGprFromVCpuU64Ex(iemNativeInstrBufEnsure(pReNative, off, 7), off, iGpr, offVCpu);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdSt(pReNative, off, iGpr, offVCpu, kArmv8A64InstrLdStType_Ld_Dword, sizeof(uint64_t));

#else
# error "port me"
#endif
    return off;
}

/**
 * Emits a 32-bit GPR load of a VCpu value.
 * @note Bits 32 thru 63 in the GPR will be zero after the operation.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromVCpuU32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGpr, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* mov reg32, mem32 */
    if (iGpr >= 8)
        pCodeBuf[off++] = X86_OP_REX_R;
    pCodeBuf[off++] = 0x8b;
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, iGpr, offVCpu);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdStEx(pCodeBuf, off, iGpr, offVCpu, kArmv8A64InstrLdStType_Ld_Word, sizeof(uint32_t));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 32-bit GPR load of a VCpu value.
 * @note Bits 32 thru 63 in the GPR will be zero after the operation.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromVCpuU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGprFromVCpuU32Ex(iemNativeInstrBufEnsure(pReNative, off, 7), off, iGpr, offVCpu);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdSt(pReNative, off, iGpr, offVCpu, kArmv8A64InstrLdStType_Ld_Word, sizeof(uint32_t));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 16-bit GPR load of a VCpu value.
 * @note Bits 16 thru 63 in the GPR will be zero after the operation.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromVCpuU16Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGpr, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* movzx reg32, mem16 */
    if (iGpr >= 8)
        pCodeBuf[off++] = X86_OP_REX_R;
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xb7;
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, iGpr, offVCpu);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdStEx(pCodeBuf, off, iGpr, offVCpu, kArmv8A64InstrLdStType_Ld_Half, sizeof(uint16_t));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 16-bit GPR load of a VCpu value.
 * @note Bits 16 thru 63 in the GPR will be zero after the operation.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromVCpuU16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGprFromVCpuU16Ex(iemNativeInstrBufEnsure(pReNative, off, 8), off, iGpr, offVCpu);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdSt(pReNative, off, iGpr, offVCpu, kArmv8A64InstrLdStType_Ld_Half, sizeof(uint16_t));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 8-bit GPR load of a VCpu value.
 * @note Bits 8 thru 63 in the GPR will be zero after the operation.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromVCpuU8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* movzx reg32, mem8 */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
    if (iGpr >= 8)
        pbCodeBuf[off++] = X86_OP_REX_R;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xb6;
    off = iemNativeEmitGprByVCpuDisp(pbCodeBuf, off, iGpr, offVCpu);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdSt(pReNative, off, iGpr, offVCpu, kArmv8A64InstrLdStType_Ld_Byte, sizeof(uint8_t));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a store of a GPR value to a 64-bit VCpu field.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStoreGprToVCpuU64Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGpr, uint32_t offVCpu,
                                 uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* mov mem64, reg64 */
    if (iGpr < 8)
        pCodeBuf[off++] = X86_OP_REX_W;
    else
        pCodeBuf[off++] = X86_OP_REX_W | X86_OP_REX_R;
    pCodeBuf[off++] = 0x89;
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, iGpr, offVCpu);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdStEx(pCodeBuf, off, iGpr, offVCpu, kArmv8A64InstrLdStType_St_Dword, sizeof(uint64_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a store of a GPR value to a 64-bit VCpu field.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGprToVCpuU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitStoreGprToVCpuU64Ex(iemNativeInstrBufEnsure(pReNative, off, 7), off, iGpr, offVCpu);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitStoreGprToVCpuU64Ex(iemNativeInstrBufEnsure(pReNative, off, 5), off, iGpr, offVCpu,
                                           IEMNATIVE_REG_FIXED_TMP0);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a store of a GPR value to a 32-bit VCpu field.
 *
 * @note Limited range on ARM64.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGprToVCpuU32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGpr, uint32_t offVCpu,
                                 uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* mov mem32, reg32 */
    if (iGpr >= 8)
        pCodeBuf[off++] = X86_OP_REX_R;
    pCodeBuf[off++] = 0x89;
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, iGpr, offVCpu);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdStEx(pCodeBuf, off, iGpr, offVCpu, kArmv8A64InstrLdStType_St_Word, sizeof(uint32_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a store of a GPR value to a 32-bit VCpu field.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGprToVCpuU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* mov mem32, reg32 */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
    if (iGpr >= 8)
        pbCodeBuf[off++] = X86_OP_REX_R;
    pbCodeBuf[off++] = 0x89;
    off = iemNativeEmitGprByVCpuDisp(pbCodeBuf, off, iGpr, offVCpu);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdSt(pReNative, off, iGpr, offVCpu, kArmv8A64InstrLdStType_St_Word, sizeof(uint32_t));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a store of a GPR value to a 16-bit VCpu field.
 *
 * @note Limited range on ARM64.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGprToVCpuU16Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGpr, uint32_t offVCpu,
                                 uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* mov mem16, reg16 */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iGpr >= 8)
        pCodeBuf[off++] = X86_OP_REX_R;
    pCodeBuf[off++] = 0x89;
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, iGpr, offVCpu);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdStEx(pCodeBuf, off, iGpr, offVCpu, kArmv8A64InstrLdStType_St_Half, sizeof(uint16_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a store of a GPR value to a 16-bit VCpu field.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGprToVCpuU16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* mov mem16, reg16 */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
    pbCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iGpr >= 8)
        pbCodeBuf[off++] = X86_OP_REX_R;
    pbCodeBuf[off++] = 0x89;
    off = iemNativeEmitGprByVCpuDisp(pbCodeBuf, off, iGpr, offVCpu);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdSt(pReNative, off, iGpr, offVCpu, kArmv8A64InstrLdStType_St_Half, sizeof(uint16_t));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a store of a GPR value to a 8-bit VCpu field.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGprToVCpuU8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* mov mem8, reg8 */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
    if (iGpr >= 8)
        pbCodeBuf[off++] = X86_OP_REX_R;
    pbCodeBuf[off++] = 0x88;
    off = iemNativeEmitGprByVCpuDisp(pbCodeBuf, off, iGpr, offVCpu);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdSt(pReNative, off, iGpr, offVCpu, kArmv8A64InstrLdStType_St_Byte, sizeof(uint8_t));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a store of an immediate value to a 64-bit VCpu field.
 *
 * @note Will allocate temporary registers on both ARM64 and AMD64.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStoreImmToVCpuU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint64_t uImm, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* mov mem32, imm32 */
    uint8_t const idxRegImm = iemNativeRegAllocTmpImm(pReNative, &off, uImm);
    off = iemNativeEmitStoreGprToVCpuU64Ex(iemNativeInstrBufEnsure(pReNative, off, 7), off, idxRegImm, offVCpu);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    iemNativeRegFreeTmpImm(pReNative, idxRegImm);

#elif defined(RT_ARCH_ARM64)
    uint8_t const idxRegImm = uImm == 0 ? ARMV8_A64_REG_XZR : iemNativeRegAllocTmpImm(pReNative, &off, uImm);
    off = iemNativeEmitGprByVCpuLdSt(pReNative, off, idxRegImm, offVCpu, kArmv8A64InstrLdStType_St_Dword, sizeof(uint64_t));
    if (idxRegImm != ARMV8_A64_REG_XZR)
        iemNativeRegFreeTmpImm(pReNative, idxRegImm);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a store of an immediate value to a 32-bit VCpu field.
 *
 * @note ARM64: Will allocate temporary registers.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStoreImmToVCpuU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t uImm, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* mov mem32, imm32 */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 10);
    pCodeBuf[off++] = 0xc7;
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, 0, offVCpu);
    pCodeBuf[off++] = RT_BYTE1(uImm);
    pCodeBuf[off++] = RT_BYTE2(uImm);
    pCodeBuf[off++] = RT_BYTE3(uImm);
    pCodeBuf[off++] = RT_BYTE4(uImm);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    uint8_t const idxRegImm = uImm == 0 ? ARMV8_A64_REG_XZR : iemNativeRegAllocTmpImm(pReNative, &off, uImm);
    off = iemNativeEmitGprByVCpuLdSt(pReNative, off, idxRegImm, offVCpu, kArmv8A64InstrLdStType_St_Word, sizeof(uint32_t));
    if (idxRegImm != ARMV8_A64_REG_XZR)
        iemNativeRegFreeTmpImm(pReNative, idxRegImm);

#else
# error "port me"
#endif
    return off;
}



/**
 * Emits a store of an immediate value to a 16-bit VCpu field.
 *
 * @note ARM64: A idxTmp1 is always required! The idxTmp2 depends on whehter the
 *       offset can be encoded as an immediate or not.  The @a offVCpu immediate
 *       range is 0..8190 bytes from VMCPU and the same from CPUMCPU.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStoreImmToVCpuU16Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint16_t uImm, uint32_t offVCpu,
                                 uint8_t idxTmp1 = UINT8_MAX, uint8_t idxTmp2 = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* mov mem16, imm16 */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    pCodeBuf[off++] = 0xc7;
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, 0, offVCpu);
    pCodeBuf[off++] = RT_BYTE1(uImm);
    pCodeBuf[off++] = RT_BYTE2(uImm);
    RT_NOREF(idxTmp1, idxTmp2);

#elif defined(RT_ARCH_ARM64)
    if (idxTmp1 != UINT8_MAX)
    {
        pCodeBuf[off++] = Armv8A64MkInstrMovZ(idxTmp1, uImm);
        off = iemNativeEmitGprByVCpuLdStEx(pCodeBuf, off, idxTmp1, offVCpu, kArmv8A64InstrLdStType_St_Half,
                                           sizeof(uint16_t), idxTmp2);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a store of an immediate value to a 8-bit VCpu field.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreImmToVCpuU8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t bImm, uint32_t offVCpu,
                              uint8_t idxRegTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* mov mem8, imm8 */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
    pbCodeBuf[off++] = 0xc6;
    off = iemNativeEmitGprByVCpuDisp(pbCodeBuf, off, 0, offVCpu);
    pbCodeBuf[off++] = bImm;
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    RT_NOREF(idxRegTmp);

#elif defined(RT_ARCH_ARM64)
    /* Cannot use IEMNATIVE_REG_FIXED_TMP0 for the immediate as that's used by iemNativeEmitGprByVCpuLdSt. */
    if (idxRegTmp != UINT8_MAX)
    {
        Assert(idxRegTmp != IEMNATIVE_REG_FIXED_TMP0);
        off = iemNativeEmitLoadGprImm32(pReNative, off, idxRegTmp, bImm);
        off = iemNativeEmitGprByVCpuLdSt(pReNative, off, idxRegTmp, offVCpu, kArmv8A64InstrLdStType_St_Byte, sizeof(uint8_t));
    }
    else
    {
        uint8_t const idxRegImm = iemNativeRegAllocTmpImm(pReNative, &off, bImm);
        off = iemNativeEmitGprByVCpuLdSt(pReNative, off, idxRegImm, offVCpu, kArmv8A64InstrLdStType_St_Byte, sizeof(uint8_t));
        iemNativeRegFreeTmpImm(pReNative, idxRegImm);
    }

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a load effective address to a GRP of a VCpu field.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLeaGprByVCpu(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* lea gprdst, [rbx + offDisp] */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
    if (iGprDst < 8)
        pbCodeBuf[off++] = X86_OP_REX_W;
    else
        pbCodeBuf[off++] = X86_OP_REX_W | X86_OP_REX_R;
    pbCodeBuf[off++] = 0x8d;
    off = iemNativeEmitGprByVCpuDisp(pbCodeBuf, off, iGprDst, offVCpu);

#elif defined(RT_ARCH_ARM64)
    if (offVCpu < (unsigned)_4K)
    {
        uint32_t * const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(iGprDst, IEMNATIVE_REG_FIXED_PVMCPU, offVCpu);
    }
    else if (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx) < (unsigned)_4K)
    {
        uint32_t * const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(iGprDst, IEMNATIVE_REG_FIXED_PCPUMCTX,
                                                   offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx));
    }
    else if (offVCpu <= 0xffffffU)
    {
        uint32_t * const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);
        pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(iGprDst, IEMNATIVE_REG_FIXED_PVMCPU, offVCpu >> 12,
                                                   true /*f64Bit*/, false /*fSetFlags*/, true /*fShift12*/);
        if (offVCpu & 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(iGprDst, iGprDst, offVCpu & 0xfff);
    }
    else
    {
        Assert(iGprDst != IEMNATIVE_REG_FIXED_PVMCPU);
        off = iemNativeEmitLoadGprImm64(pReNative, off, iGprDst, offVCpu);
        uint32_t * const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pCodeBuf[off++] = Armv8A64MkInstrAddSubReg(false /*fSub*/, iGprDst, IEMNATIVE_REG_FIXED_PCPUMCTX, iGprDst);
    }

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/** This is just as a typesafe alternative to RT_UOFFSETOF. */
DECL_FORCE_INLINE(uint32_t) iemNativeVCpuOffsetFromStamCounterPtr(PVMCPU pVCpu, PSTAMCOUNTER pStamCounter)
{
    uintptr_t const off = (uintptr_t)pStamCounter - (uintptr_t)pVCpu;
    Assert(off < sizeof(VMCPU));
    return off;
}


/** This is just as a typesafe alternative to RT_UOFFSETOF. */
DECL_FORCE_INLINE(uint32_t) iemNativeVCpuOffsetFromU64Ptr(PVMCPU pVCpu, uint64_t *pu64)
{
    uintptr_t const off = (uintptr_t)pu64 - (uintptr_t)pVCpu;
    Assert(off < sizeof(VMCPU));
    return off;
}


/**
 * Emits code for incrementing a statistics counter (STAMCOUNTER/uint64_t) in VMCPU.
 *
 * @note The two temp registers are not required for AMD64.  ARM64 always
 *       requires the first, and the 2nd is needed if the offset cannot be
 *       encoded as an immediate.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitIncStamCounterInVCpuEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t idxTmp1, uint8_t idxTmp2, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* inc qword [pVCpu + off] */
    pCodeBuf[off++] = X86_OP_REX_W;
    pCodeBuf[off++] = 0xff;
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, 0, offVCpu);
    RT_NOREF(idxTmp1, idxTmp2);

#elif defined(RT_ARCH_ARM64)
    /* Determine how we're to access pVCpu first. */
    uint32_t const cbData = sizeof(STAMCOUNTER);
    if (offVCpu < _4K * cbData && !(offVCpu & (cbData - 1)))
    {
        /* Use the unsigned variant of ldr Wt, [<Xn|SP>, #off]. */
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_Ld_Dword, idxTmp1,
                                                   IEMNATIVE_REG_FIXED_PVMCPU, offVCpu / cbData);
        pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(idxTmp1, idxTmp1, 1);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_St_Dword, idxTmp1,
                                                   IEMNATIVE_REG_FIXED_PVMCPU, offVCpu / cbData);
    }
    else if (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx) < (unsigned)(_4K * cbData) && !(offVCpu & (cbData - 1)))
    {
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_Ld_Dword, idxTmp1, IEMNATIVE_REG_FIXED_PCPUMCTX,
                                                   (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx)) / cbData);
        pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(idxTmp1, idxTmp1, 1);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_St_Dword, idxTmp1, IEMNATIVE_REG_FIXED_PCPUMCTX,
                                                   (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx)) / cbData);
    }
    else
    {
        /* The offset is too large, so we must load it into a register and use
           ldr Wt, [<Xn|SP>, (<Wm>|<Xm>)]. */
        off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, idxTmp2, offVCpu);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(kArmv8A64InstrLdStType_Ld_Dword, idxTmp1, IEMNATIVE_REG_FIXED_PVMCPU, idxTmp2);
        pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(idxTmp1, idxTmp1, 1);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(kArmv8A64InstrLdStType_St_Dword, idxTmp1, IEMNATIVE_REG_FIXED_PVMCPU, idxTmp2);
    }

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits code for incrementing a statistics counter (STAMCOUNTER/uint64_t) in VMCPU.
 *
 * @note The two temp registers are not required for AMD64.  ARM64 always
 *       requires the first, and the 2nd is needed if the offset cannot be
 *       encoded as an immediate.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitIncStamCounterInVCpu(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxTmp1, uint8_t idxTmp2, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitIncStamCounterInVCpuEx(iemNativeInstrBufEnsure(pReNative, off, 7), off, idxTmp1, idxTmp2, offVCpu);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitIncStamCounterInVCpuEx(iemNativeInstrBufEnsure(pReNative, off, 4+3), off, idxTmp1, idxTmp2, offVCpu);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for incrementing an unsigned 32-bit statistics counter in VMCPU.
 *
 * @note The two temp registers are not required for AMD64.  ARM64 always
 *       requires the first, and the 2nd is needed if the offset cannot be
 *       encoded as an immediate.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitIncU32CounterInVCpuEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t idxTmp1, uint8_t idxTmp2, uint32_t offVCpu)
{
    Assert(!(offVCpu & 3)); /* ASSUME correctly aligned member. */
#ifdef RT_ARCH_AMD64
    /* inc dword [pVCpu + offVCpu] */
    pCodeBuf[off++] = 0xff;
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, 0, offVCpu);
    RT_NOREF(idxTmp1, idxTmp2);

#elif defined(RT_ARCH_ARM64)
    /* Determine how we're to access pVCpu first. */
    uint32_t const cbData = sizeof(uint32_t);
    if (offVCpu < (unsigned)(_4K * cbData))
    {
        /* Use the unsigned variant of ldr Wt, [<Xn|SP>, #off]. */
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_Ld_Word, idxTmp1,
                                                   IEMNATIVE_REG_FIXED_PVMCPU, offVCpu / cbData);
        pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(idxTmp1, idxTmp1, 1);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_St_Word, idxTmp1,
                                                   IEMNATIVE_REG_FIXED_PVMCPU, offVCpu / cbData);
    }
    else if (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx) < (unsigned)(_4K * cbData))
    {
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_Ld_Word, idxTmp1, IEMNATIVE_REG_FIXED_PCPUMCTX,
                                                   (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx)) / cbData);
        pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(idxTmp1, idxTmp1, 1);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_St_Word, idxTmp1, IEMNATIVE_REG_FIXED_PCPUMCTX,
                                                   (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx)) / cbData);
    }
    else
    {
        /* The offset is too large, so we must load it into a register and use
           ldr Wt, [<Xn|SP>, (<Wm>|<Xm>)].  We'll try use the 'LSL, #2' feature
           of the instruction if that'll reduce the constant to 16-bits. */
        if (offVCpu / cbData < (unsigned)UINT16_MAX)
        {
            pCodeBuf[off++] = Armv8A64MkInstrMovZ(idxTmp2, offVCpu / cbData);
            pCodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(kArmv8A64InstrLdStType_Ld_Word, idxTmp1, IEMNATIVE_REG_FIXED_PVMCPU,
                                                        idxTmp2, kArmv8A64InstrLdStExtend_Lsl, true /*fShifted(2)*/);
            pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(idxTmp1, idxTmp1, 1);
            pCodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(kArmv8A64InstrLdStType_St_Word, idxTmp1, IEMNATIVE_REG_FIXED_PVMCPU,
                                                        idxTmp2, kArmv8A64InstrLdStExtend_Lsl, true /*fShifted(2)*/);
        }
        else
        {
            off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, idxTmp2, offVCpu);
            pCodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(kArmv8A64InstrLdStType_Ld_Word, idxTmp1, IEMNATIVE_REG_FIXED_PVMCPU, idxTmp2);
            pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(idxTmp1, idxTmp1, 1);
            pCodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(kArmv8A64InstrLdStType_St_Word, idxTmp1, IEMNATIVE_REG_FIXED_PVMCPU, idxTmp2);
        }
    }

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits code for incrementing an unsigned 32-bit statistics counter in VMCPU.
 *
 * @note The two temp registers are not required for AMD64.  ARM64 always
 *       requires the first, and the 2nd is needed if the offset cannot be
 *       encoded as an immediate.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitIncU32CounterInVCpu(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxTmp1, uint8_t idxTmp2, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitIncU32CounterInVCpuEx(iemNativeInstrBufEnsure(pReNative, off, 6), off, idxTmp1, idxTmp2, offVCpu);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitIncU32CounterInVCpuEx(iemNativeInstrBufEnsure(pReNative, off, 4+3), off, idxTmp1, idxTmp2, offVCpu);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for OR'ing a bitmask into a 32-bit VMCPU member.
 *
 * @note  May allocate temporary registers (not AMD64).
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitOrImmIntoVCpuU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t fMask, uint32_t offVCpu)
{
    Assert(!(offVCpu & 3)); /* ASSUME correctly aligned member. */
#ifdef RT_ARCH_AMD64
    /* or dword [pVCpu + offVCpu], imm8/32 */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 10);
    if (fMask < 0x80)
    {
        pCodeBuf[off++] = 0x83;
        off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, 1, offVCpu);
        pCodeBuf[off++] = (uint8_t)fMask;
    }
    else
    {
        pCodeBuf[off++] = 0x81;
        off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, 1, offVCpu);
        pCodeBuf[off++] = RT_BYTE1(fMask);
        pCodeBuf[off++] = RT_BYTE2(fMask);
        pCodeBuf[off++] = RT_BYTE3(fMask);
        pCodeBuf[off++] = RT_BYTE4(fMask);
    }

#elif defined(RT_ARCH_ARM64)
    /* If the constant is unwieldy we'll need a register to hold it as well. */
    uint32_t uImmSizeLen, uImmRotate;
    uint8_t const idxTmpMask  = Armv8A64ConvertMask32ToImmRImmS(fMask, &uImmSizeLen, &uImmRotate) ? UINT8_MAX
                              : iemNativeRegAllocTmpImm(pReNative, &off, fMask);

    /* We need a temp register for holding the member value we're modifying. */
    uint8_t const idxTmpValue = iemNativeRegAllocTmp(pReNative, &off);

    /* Determine how we're to access pVCpu first. */
    uint32_t const cbData = sizeof(uint32_t);
    if (offVCpu < (unsigned)(_4K * cbData))
    {
        /* Use the unsigned variant of ldr Wt, [<Xn|SP>, #off]. */
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_Ld_Word, idxTmpValue,
                                                   IEMNATIVE_REG_FIXED_PVMCPU, offVCpu / cbData);
        if (idxTmpMask == UINT8_MAX)
            pCodeBuf[off++] = Armv8A64MkInstrOrrImm(idxTmpValue, idxTmpValue, uImmSizeLen, uImmRotate, false /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrOrr(idxTmpValue, idxTmpValue, idxTmpMask, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_St_Word, idxTmpValue,
                                                   IEMNATIVE_REG_FIXED_PVMCPU, offVCpu / cbData);
    }
    else if (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx) < (unsigned)(_4K * cbData))
    {
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_Ld_Word, idxTmpValue, IEMNATIVE_REG_FIXED_PCPUMCTX,
                                                   (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx)) / cbData);
        if (idxTmpMask == UINT8_MAX)
            pCodeBuf[off++] = Armv8A64MkInstrOrrImm(idxTmpValue, idxTmpValue, uImmSizeLen, uImmRotate, false /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrOrr(idxTmpValue, idxTmpValue, idxTmpMask, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_St_Word, idxTmpValue, IEMNATIVE_REG_FIXED_PCPUMCTX,
                                                   (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx)) / cbData);
    }
    else
    {
        /* The offset is too large, so we must load it into a register and use
           ldr Wt, [<Xn|SP>, (<Wm>|<Xm>)].  We'll try use the 'LSL, #2' feature
           of the instruction if that'll reduce the constant to 16-bits. */
        uint8_t const         idxTmpIndex = iemNativeRegAllocTmp(pReNative, &off);
        PIEMNATIVEINSTR const pCodeBuf    = iemNativeInstrBufEnsure(pReNative, off, 5);
        bool const            fShifted    = offVCpu / cbData < (unsigned)UINT16_MAX;
        if (fShifted)
            pCodeBuf[off++] = Armv8A64MkInstrMovZ(idxTmpIndex, offVCpu / cbData);
        else
            off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, idxTmpIndex, offVCpu);

        pCodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(kArmv8A64InstrLdStType_Ld_Word, idxTmpValue, IEMNATIVE_REG_FIXED_PVMCPU,
                                                    idxTmpIndex, kArmv8A64InstrLdStExtend_Lsl, fShifted /*fShifted(2)*/);

        if (idxTmpMask == UINT8_MAX)
            pCodeBuf[off++] = Armv8A64MkInstrOrrImm(idxTmpValue, idxTmpValue, uImmSizeLen, uImmRotate, false /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrOrr(idxTmpValue, idxTmpValue, idxTmpMask, false /*f64Bit*/);

        pCodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(kArmv8A64InstrLdStType_St_Word, idxTmpValue, IEMNATIVE_REG_FIXED_PVMCPU,
                                                    idxTmpIndex, kArmv8A64InstrLdStExtend_Lsl, fShifted /*fShifted(2)*/);
        iemNativeRegFreeTmp(pReNative, idxTmpIndex);
    }
    iemNativeRegFreeTmp(pReNative, idxTmpValue);
    if (idxTmpMask != UINT8_MAX)
        iemNativeRegFreeTmp(pReNative, idxTmpMask);

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for AND'ing a bitmask into a 32-bit VMCPU member.
 *
 * @note  May allocate temporary registers (not AMD64).
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitAndImmIntoVCpuU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t fMask, uint32_t offVCpu)
{
    Assert(!(offVCpu & 3)); /* ASSUME correctly aligned member. */
#ifdef RT_ARCH_AMD64
    /* and dword [pVCpu + offVCpu], imm8/32 */
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 10);
    if (fMask < 0x80)
    {
        pCodeBuf[off++] = 0x83;
        off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, 4, offVCpu);
        pCodeBuf[off++] = (uint8_t)fMask;
    }
    else
    {
        pCodeBuf[off++] = 0x81;
        off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, 4, offVCpu);
        pCodeBuf[off++] = RT_BYTE1(fMask);
        pCodeBuf[off++] = RT_BYTE2(fMask);
        pCodeBuf[off++] = RT_BYTE3(fMask);
        pCodeBuf[off++] = RT_BYTE4(fMask);
    }

#elif defined(RT_ARCH_ARM64)
    /* If the constant is unwieldy we'll need a register to hold it as well. */
    uint32_t uImmSizeLen, uImmRotate;
    uint8_t const idxTmpMask  = Armv8A64ConvertMask32ToImmRImmS(fMask, &uImmSizeLen, &uImmRotate) ? UINT8_MAX
                              : iemNativeRegAllocTmpImm(pReNative, &off, fMask);

    /* We need a temp register for holding the member value we're modifying. */
    uint8_t const idxTmpValue = iemNativeRegAllocTmp(pReNative, &off);

    /* Determine how we're to access pVCpu first. */
    uint32_t const cbData = sizeof(uint32_t);
    if (offVCpu < (unsigned)(_4K * cbData))
    {
        /* Use the unsigned variant of ldr Wt, [<Xn|SP>, #off]. */
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_Ld_Word, idxTmpValue,
                                                   IEMNATIVE_REG_FIXED_PVMCPU, offVCpu / cbData);
        if (idxTmpMask == UINT8_MAX)
            pCodeBuf[off++] = Armv8A64MkInstrAndImm(idxTmpValue, idxTmpValue, uImmSizeLen, uImmRotate, false /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAnd(idxTmpValue, idxTmpValue, idxTmpMask, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_St_Word, idxTmpValue,
                                                   IEMNATIVE_REG_FIXED_PVMCPU, offVCpu / cbData);
    }
    else if (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx) < (unsigned)(_4K * cbData))
    {
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_Ld_Word, idxTmpValue, IEMNATIVE_REG_FIXED_PCPUMCTX,
                                                   (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx)) / cbData);
        if (idxTmpMask == UINT8_MAX)
            pCodeBuf[off++] = Armv8A64MkInstrAndImm(idxTmpValue, idxTmpValue, uImmSizeLen, uImmRotate, false /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAnd(idxTmpValue, idxTmpValue, idxTmpMask, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_St_Word, idxTmpValue, IEMNATIVE_REG_FIXED_PCPUMCTX,
                                                   (offVCpu - RT_UOFFSETOF(VMCPU, cpum.GstCtx)) / cbData);
    }
    else
    {
        /* The offset is too large, so we must load it into a register and use
           ldr Wt, [<Xn|SP>, (<Wm>|<Xm>)].  We'll try use the 'LSL, #2' feature
           of the instruction if that'll reduce the constant to 16-bits. */
        uint8_t const         idxTmpIndex = iemNativeRegAllocTmp(pReNative, &off);
        PIEMNATIVEINSTR const pCodeBuf    = iemNativeInstrBufEnsure(pReNative, off, 5);
        bool const            fShifted    = offVCpu / cbData < (unsigned)UINT16_MAX;
        if (fShifted)
            pCodeBuf[off++] = Armv8A64MkInstrMovZ(idxTmpIndex, offVCpu / cbData);
        else
            off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, idxTmpIndex, offVCpu);

        pCodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(kArmv8A64InstrLdStType_Ld_Word, idxTmpValue, IEMNATIVE_REG_FIXED_PVMCPU,
                                                    idxTmpIndex, kArmv8A64InstrLdStExtend_Lsl, fShifted /*fShifted(2)*/);

        if (idxTmpMask == UINT8_MAX)
            pCodeBuf[off++] = Armv8A64MkInstrAndImm(idxTmpValue, idxTmpValue, uImmSizeLen, uImmRotate, false /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAnd(idxTmpValue, idxTmpValue, idxTmpMask, false /*f64Bit*/);

        pCodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(kArmv8A64InstrLdStType_St_Word, idxTmpValue, IEMNATIVE_REG_FIXED_PVMCPU,
                                                    idxTmpIndex, kArmv8A64InstrLdStExtend_Lsl, fShifted /*fShifted(2)*/);
        iemNativeRegFreeTmp(pReNative, idxTmpIndex);
    }
    iemNativeRegFreeTmp(pReNative, idxTmpValue);
    if (idxTmpMask != UINT8_MAX)
        iemNativeRegFreeTmp(pReNative, idxTmpMask);

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a gprdst = gprsrc load.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitLoadGprFromGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    /* mov gprdst, gprsrc */
    if ((iGprDst | iGprSrc) >= 8)
        pCodeBuf[off++] = iGprDst < 8  ? X86_OP_REX_W | X86_OP_REX_B
                        : iGprSrc >= 8 ? X86_OP_REX_W | X86_OP_REX_R | X86_OP_REX_B
                        :                X86_OP_REX_W | X86_OP_REX_R;
    else
        pCodeBuf[off++] = X86_OP_REX_W;
    pCodeBuf[off++] = 0x8b;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

#elif defined(RT_ARCH_ARM64)
    /* mov dst, src;   alias for: orr dst, xzr, src */
    pCodeBuf[off++] = Armv8A64MkInstrOrr(iGprDst, ARMV8_A64_REG_XZR, iGprSrc);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a gprdst = gprsrc load.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGprFromGprEx(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst, iGprSrc);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitLoadGprFromGprEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iGprSrc);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a gprdst = gprsrc[31:0] load.
 * @note Bits 63 thru 32 are cleared.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitLoadGprFromGpr32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    /* mov gprdst, gprsrc */
    if ((iGprDst | iGprSrc) >= 8)
        pCodeBuf[off++] = iGprDst < 8  ? X86_OP_REX_B
                        : iGprSrc >= 8 ? X86_OP_REX_R | X86_OP_REX_B
                        :                X86_OP_REX_R;
    pCodeBuf[off++] = 0x8b;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

#elif defined(RT_ARCH_ARM64)
    /* mov dst32, src32;   alias for: orr dst32, wzr, src32 */
    pCodeBuf[off++] = Armv8A64MkInstrOrr(iGprDst, ARMV8_A64_REG_WZR, iGprSrc, false /*f64bit*/);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a gprdst = gprsrc[31:0] load.
 * @note Bits 63 thru 32 are cleared.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromGpr32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGprFromGpr32Ex(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst, iGprSrc);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitLoadGprFromGpr32Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iGprSrc);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a gprdst = gprsrc[15:0] load.
 * @note Bits 63 thru 15 are cleared.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromGpr16Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    /* movzx Gv,Ew */
    if ((iGprDst | iGprSrc) >= 8)
        pCodeBuf[off++] = iGprDst < 8  ? X86_OP_REX_B
                        : iGprSrc >= 8 ? X86_OP_REX_R | X86_OP_REX_B
                        :                X86_OP_REX_R;
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xb7;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

#elif defined(RT_ARCH_ARM64)
    /* and gprdst, gprsrc, #0xffff */
# if 1
    Assert(Armv8A64ConvertImmRImmS2Mask32(0x0f, 0) == UINT16_MAX);
    pCodeBuf[off++] = Armv8A64MkInstrAndImm(iGprDst, iGprSrc, 0x0f, 0, false /*f64Bit*/);
# else
    Assert(Armv8A64ConvertImmRImmS2Mask64(0x4f, 0) == UINT16_MAX);
    pCodeBuf[off++] = Armv8A64MkInstrAndImm(iGprDst, iGprSrc, 0x4f, 0);
# endif

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a gprdst = gprsrc[15:0] load.
 * @note Bits 63 thru 15 are cleared.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromGpr16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGprFromGpr16Ex(iemNativeInstrBufEnsure(pReNative, off, 4), off, iGprDst, iGprSrc);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitLoadGprFromGpr16Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iGprSrc);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a gprdst = gprsrc[7:0] load.
 * @note Bits 63 thru 8 are cleared.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitLoadGprFromGpr8Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    /* movzx Gv,Eb */
    if (iGprDst >= 8 || iGprSrc >= 8)
        pCodeBuf[off++] = iGprDst < 8  ? X86_OP_REX_B
                        : iGprSrc >= 8 ? X86_OP_REX_R | X86_OP_REX_B
                        :                X86_OP_REX_R;
    else if (iGprSrc >= 4)
        pCodeBuf[off++] = X86_OP_REX;
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xb6;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

#elif defined(RT_ARCH_ARM64)
    /* and gprdst, gprsrc, #0xff */
    Assert(Armv8A64ConvertImmRImmS2Mask32(0x07, 0) == UINT8_MAX);
    pCodeBuf[off++] = Armv8A64MkInstrAndImm(iGprDst, iGprSrc, 0x07, 0, false /*f64Bit*/);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a gprdst = gprsrc[7:0] load.
 * @note Bits 63 thru 8 are cleared.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromGpr8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGprFromGpr8Ex(iemNativeInstrBufEnsure(pReNative, off, 4), off, iGprDst, iGprSrc);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitLoadGprFromGpr8Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iGprSrc);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a gprdst = gprsrc[15:8] load (ah, ch, dh, bh).
 * @note Bits 63 thru 8 are cleared.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromGpr8Hi(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);

    /* movzx Gv,Ew */
    if ((iGprDst | iGprSrc) >= 8)
        pbCodeBuf[off++] = iGprDst < 8  ? X86_OP_REX_B
                         : iGprSrc >= 8 ? X86_OP_REX_R | X86_OP_REX_B
                         :                X86_OP_REX_R;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xb7;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

    /* shr Ev,8 */
    if (iGprDst >= 8)
        pbCodeBuf[off++] = X86_OP_REX_B;
    pbCodeBuf[off++] = 0xc1;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 5, iGprDst & 7);
    pbCodeBuf[off++] = 8;

#elif defined(RT_ARCH_ARM64)
    /* ubfx gprdst, gprsrc, #8, #8 - gprdst = gprsrc[15:8] */
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = Armv8A64MkInstrUbfx(iGprDst, iGprSrc, 8, 8, false /*f64Bit*/);

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Sign-extends 32-bit value in @a iGprSrc into a 64-bit value in @a iGprDst.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprSignExtendedFromGpr32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    /* movsxd r64, r/m32 */
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3);
    pbCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pbCodeBuf[off++] = 0x63;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

#elif defined(RT_ARCH_ARM64)
    /* sxtw dst, src */
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = Armv8A64MkInstrSxtw(iGprDst, iGprSrc);

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Sign-extends 16-bit value in @a iGprSrc into a 64-bit value in @a iGprDst.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprSignExtendedFromGpr16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    /* movsx r64, r/m16 */
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 4);
    pbCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xbf;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

#elif defined(RT_ARCH_ARM64)
    /* sxth dst, src */
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = Armv8A64MkInstrSxth(iGprDst, iGprSrc);

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Sign-extends 16-bit value in @a iGprSrc into a 32-bit value in @a iGprDst.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGpr32SignExtendedFromGpr16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    /* movsx r64, r/m16 */
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 4);
    if (iGprDst >= 8 || iGprSrc >= 8)
        pbCodeBuf[off++] = (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xbf;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

#elif defined(RT_ARCH_ARM64)
    /* sxth dst32, src */
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = Armv8A64MkInstrSxth(iGprDst, iGprSrc, false /*f64Bit*/);

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Sign-extends 8-bit value in @a iGprSrc into a 64-bit value in @a iGprDst.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprSignExtendedFromGpr8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    /* movsx r64, r/m8 */
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 4);
    pbCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xbe;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

#elif defined(RT_ARCH_ARM64)
    /* sxtb dst, src */
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = Armv8A64MkInstrSxtb(iGprDst, iGprSrc);

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Sign-extends 8-bit value in @a iGprSrc into a 32-bit value in @a iGprDst.
 * @note Bits 63 thru 32 are cleared.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGpr32SignExtendedFromGpr8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    /* movsx r32, r/m8 */
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 4);
    if (iGprDst >= 8 || iGprSrc >= 8)
        pbCodeBuf[off++] = (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    else if (iGprSrc >= 4)
        pbCodeBuf[off++] = X86_OP_REX;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xbe;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

#elif defined(RT_ARCH_ARM64)
    /* sxtb dst32, src32 */
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = Armv8A64MkInstrSxtb(iGprDst, iGprSrc, false /*f64Bit*/);

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Sign-extends 8-bit value in @a iGprSrc into a 16-bit value in @a iGprDst.
 * @note Bits 63 thru 16 are cleared.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGpr16SignExtendedFromGpr8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    /* movsx r16, r/m8 */
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 9);
    pbCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iGprDst >= 8 || iGprSrc >= 8)
        pbCodeBuf[off++] = (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    else if (iGprSrc >= 4)
        pbCodeBuf[off++] = X86_OP_REX;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xbe;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

    /* movzx r32, r/m16 */
    if (iGprDst >= 8)
        pbCodeBuf[off++] = X86_OP_REX_R | X86_OP_REX_B;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xb7;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprDst & 7);

#elif defined(RT_ARCH_ARM64)
    /* sxtb dst32, src32;  and dst32, dst32, #0xffff */
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);
    pu32CodeBuf[off++] = Armv8A64MkInstrSxtb(iGprDst, iGprSrc, false /*f64Bit*/);
    Assert(Armv8A64ConvertImmRImmS2Mask32(15, 0) == 0xffff);
    pu32CodeBuf[off++] = Armv8A64MkInstrAndImm(iGprDst, iGprDst, 15, 0, false /*f64Bit*/);

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a gprdst = gprsrc + addend load.
 * @note The addend is 32-bit for AMD64 and 64-bit for ARM64.
 */
#ifdef RT_ARCH_AMD64
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromGprWithAddend(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                      uint8_t iGprDst, uint8_t iGprSrc, int32_t iAddend)
{
    Assert(iAddend != 0);

    /* lea gprdst, [gprsrc + iAddend] */
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
    pbCodeBuf[off++] = X86_OP_REX_W | (iGprDst >= 8 ? X86_OP_REX_R : 0) | (iGprSrc >= 8 ? X86_OP_REX_B : 0);
    pbCodeBuf[off++] = 0x8d;
    off = iemNativeEmitGprByGprDisp(pbCodeBuf, off, iGprDst, iGprSrc, iAddend);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}

#elif defined(RT_ARCH_ARM64)
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromGprWithAddend(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                      uint8_t iGprDst, uint8_t iGprSrc, int64_t iAddend)
{
    if ((uint32_t)iAddend < 4096)
    {
        /* add dst, src, uimm12 */
        uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(false /*fSub*/, iGprDst, iGprSrc, (uint32_t)iAddend);
    }
    else if ((uint32_t)-iAddend < 4096)
    {
        /* sub dst, src, uimm12 */
        uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, iGprDst, iGprSrc, (uint32_t)-iAddend);
    }
    else
    {
        Assert(iGprSrc != iGprDst);
        off = iemNativeEmitLoadGprImm64(pReNative, off, iGprDst, iAddend);
        uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAddSubReg(false /*fSub*/, iGprDst, iGprSrc, iGprDst);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}
#else
# error "port me"
#endif

/**
 * Emits a gprdst = gprsrc + addend load, accepting iAddend == 0.
 * @note The added is 32-bit for AMD64 and 64-bit for ARM64.
 */
#ifdef RT_ARCH_AMD64
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromGprWithAddendMaybeZero(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                               uint8_t iGprDst, uint8_t iGprSrc, int32_t iAddend)
#else
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromGprWithAddendMaybeZero(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                               uint8_t iGprDst, uint8_t iGprSrc, int64_t iAddend)
#endif
{
    if (iAddend != 0)
        return iemNativeEmitLoadGprFromGprWithAddend(pReNative, off, iGprDst, iGprSrc, iAddend);
    return iemNativeEmitLoadGprFromGpr(pReNative, off, iGprDst, iGprSrc);
}


/**
 * Emits a gprdst = gprsrc32 + addend load.
 * @note Bits 63 thru 32 are cleared.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromGpr32WithAddend(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                        uint8_t iGprDst, uint8_t iGprSrc, int32_t iAddend)
{
    Assert(iAddend != 0);

#ifdef RT_ARCH_AMD64
    /* a32 o32 lea gprdst, [gprsrc + iAddend] */
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 9);
    pbCodeBuf[off++] = X86_OP_PRF_SIZE_ADDR;
    if ((iGprDst | iGprSrc) >= 8)
        pbCodeBuf[off++] = (iGprDst >= 8 ? X86_OP_REX_R : 0) | (iGprSrc >= 8 ? X86_OP_REX_B : 0);
    pbCodeBuf[off++] = 0x8d;
    off = iemNativeEmitGprByGprDisp(pbCodeBuf, off, iGprDst, iGprSrc, iAddend);

#elif defined(RT_ARCH_ARM64)
    if ((uint32_t)iAddend < 4096)
    {
        /* add dst, src, uimm12 */
        uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(false /*fSub*/, iGprDst, iGprSrc, (uint32_t)iAddend, false /*f64Bit*/);
    }
    else if ((uint32_t)-iAddend < 4096)
    {
        /* sub dst, src, uimm12 */
        uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, iGprDst, iGprSrc, (uint32_t)-iAddend, false /*f64Bit*/);
    }
    else
    {
        Assert(iGprSrc != iGprDst);
        off = iemNativeEmitLoadGprImm64(pReNative, off, iGprDst, (int64_t)iAddend);
        uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAddSubReg(false /*fSub*/, iGprDst, iGprSrc, iGprDst, false /*f64Bit*/);
    }

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a gprdst = gprsrc32 + addend load, accepting iAddend == 0.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprFromGpr32WithAddendMaybeZero(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                 uint8_t iGprDst, uint8_t iGprSrc, int32_t iAddend)
{
    if (iAddend != 0)
        return iemNativeEmitLoadGprFromGpr32WithAddend(pReNative, off, iGprDst, iGprSrc, iAddend);
    return iemNativeEmitLoadGprFromGpr32(pReNative, off, iGprDst, iGprSrc);
}


/**
 * Emits a gprdst[15:0] = gprsrc[15:0], preserving all other bits in the
 * destination.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitGprMergeInGpr16Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t idxDst, uint8_t idxSrc)
{
#ifdef RT_ARCH_AMD64
    /* mov reg16, r/m16 */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (idxDst >= 8 || idxSrc >= 8)
        pCodeBuf[off++] = (idxDst < 8 ? 0 : X86_OP_REX_R) | (idxSrc < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x8b;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxDst & 7, idxSrc & 7);

#elif defined(RT_ARCH_ARM64)
    /* bfi w1, w2, 0, 16 - moves bits 15:0 from idxSrc to idxDst bits 15:0. */
    pCodeBuf[off++] = Armv8A64MkInstrBfi(idxDst, idxSrc, 0, 16);

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits a gprdst[15:0] = gprsrc[15:0], preserving all other bits in the
 * destination.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitGprMergeInGpr16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDst, uint8_t idxSrc)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitGprMergeInGpr16Ex(iemNativeInstrBufEnsure(pReNative, off, 4), off, idxDst, idxSrc);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprMergeInGpr16Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, idxDst, idxSrc);
#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


#ifdef RT_ARCH_AMD64
/**
 * Common bit of iemNativeEmitLoadGprByBp and friends.
 */
DECL_FORCE_INLINE(uint32_t) iemNativeEmitGprByBpDisp(uint8_t *pbCodeBuf, uint32_t off, uint8_t iGprReg, int32_t offDisp,
                                                     PIEMRECOMPILERSTATE pReNativeAssert)
{
    if (offDisp < 128 && offDisp >= -128)
    {
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM1, iGprReg & 7, X86_GREG_xBP);
        pbCodeBuf[off++] = (uint8_t)(int8_t)offDisp;
    }
    else
    {
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM4, iGprReg & 7, X86_GREG_xBP);
        pbCodeBuf[off++] = RT_BYTE1((uint32_t)offDisp);
        pbCodeBuf[off++] = RT_BYTE2((uint32_t)offDisp);
        pbCodeBuf[off++] = RT_BYTE3((uint32_t)offDisp);
        pbCodeBuf[off++] = RT_BYTE4((uint32_t)offDisp);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNativeAssert, off); RT_NOREF(pReNativeAssert);
    return off;
}
#elif defined(RT_ARCH_ARM64)
/**
 * Common bit of iemNativeEmitLoadGprByBp and friends.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitGprByBpLdSt(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprReg,
                         int32_t offDisp, ARMV8A64INSTRLDSTTYPE enmOperation, unsigned cbData)
{
    if ((uint32_t)offDisp < 4096U * cbData && !((uint32_t)offDisp & (cbData - 1)))
    {
        /* str w/ unsigned imm12 (scaled) */
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRUOff(enmOperation, iGprReg, ARMV8_A64_REG_BP, (uint32_t)offDisp / cbData);
    }
    else if (offDisp >= -256 && offDisp <= 256)
    {
        /* stur w/ signed imm9 (unscaled) */
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrSturLdur(enmOperation, iGprReg, ARMV8_A64_REG_BP, offDisp);
    }
    else
    {
        /* Use temporary indexing register. */
        off = iemNativeEmitLoadGprImm64(pReNative, off, IEMNATIVE_REG_FIXED_TMP0, (uint32_t)offDisp);
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(enmOperation, iGprReg, ARMV8_A64_REG_BP,
                                                       IEMNATIVE_REG_FIXED_TMP0, kArmv8A64InstrLdStExtend_Sxtw);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}
#endif


/**
 * Emits a 64-bit GRP load instruction with an BP relative source address.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByBp(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, int32_t offDisp)
{
#ifdef RT_ARCH_AMD64
    /* mov gprdst, qword [rbp + offDisp]  */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
    if (iGprDst < 8)
        pbCodeBuf[off++] = X86_OP_REX_W;
    else
        pbCodeBuf[off++] = X86_OP_REX_W | X86_OP_REX_R;
    pbCodeBuf[off++] = 0x8b;
    return iemNativeEmitGprByBpDisp(pbCodeBuf, off, iGprDst, offDisp, pReNative);

#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitGprByBpLdSt(pReNative, off, iGprDst, offDisp, kArmv8A64InstrLdStType_Ld_Dword, sizeof(uint64_t));

#else
# error "port me"
#endif
}


/**
 * Emits a 32-bit GRP load instruction with an BP relative source address.
 * @note Bits 63 thru 32 of the GPR will be cleared.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByBpU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, int32_t offDisp)
{
#ifdef RT_ARCH_AMD64
    /* mov gprdst, dword [rbp + offDisp]  */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
    if (iGprDst >= 8)
        pbCodeBuf[off++] = X86_OP_REX_R;
    pbCodeBuf[off++] = 0x8b;
    return iemNativeEmitGprByBpDisp(pbCodeBuf, off, iGprDst, offDisp, pReNative);

#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitGprByBpLdSt(pReNative, off, iGprDst, offDisp, kArmv8A64InstrLdStType_Ld_Word, sizeof(uint32_t));

#else
# error "port me"
#endif
}


/**
 * Emits a 16-bit GRP load instruction with an BP relative source address.
 * @note Bits 63 thru 16 of the GPR will be cleared.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByBpU16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, int32_t offDisp)
{
#ifdef RT_ARCH_AMD64
    /* movzx gprdst, word [rbp + offDisp]  */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
    if (iGprDst >= 8)
        pbCodeBuf[off++] = X86_OP_REX_R;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xb7;
    return iemNativeEmitGprByBpDisp(pbCodeBuf, off, iGprDst, offDisp, pReNative);

#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitGprByBpLdSt(pReNative, off, iGprDst, offDisp, kArmv8A64InstrLdStType_Ld_Half, sizeof(uint32_t));

#else
# error "port me"
#endif
}


/**
 * Emits a 8-bit GRP load instruction with an BP relative source address.
 * @note Bits 63 thru 8 of the GPR will be cleared.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByBpU8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, int32_t offDisp)
{
#ifdef RT_ARCH_AMD64
    /* movzx gprdst, byte [rbp + offDisp]  */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
    if (iGprDst >= 8)
        pbCodeBuf[off++] = X86_OP_REX_R;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xb6;
    return iemNativeEmitGprByBpDisp(pbCodeBuf, off, iGprDst, offDisp, pReNative);

#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitGprByBpLdSt(pReNative, off, iGprDst, offDisp, kArmv8A64InstrLdStType_Ld_Byte, sizeof(uint32_t));

#else
# error "port me"
#endif
}


/**
 * Emits a 128-bit vector register load instruction with an BP relative source address.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadVecRegByBpU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, int32_t offDisp)
{
#ifdef RT_ARCH_AMD64
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 9);

    /* movdqu reg128, mem128 */
    pbCodeBuf[off++] = 0xf3;
    if (iVecRegDst >= 8)
        pbCodeBuf[off++] = X86_OP_REX_R;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0x6f;
    return iemNativeEmitGprByBpDisp(pbCodeBuf, off, iVecRegDst, offDisp, pReNative);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitGprByBpLdSt(pReNative, off, iVecRegDst, offDisp, kArmv8A64InstrLdStType_Ld_Vr_128, sizeof(RTUINT128U));
#else
# error "port me"
#endif
}


/**
 * Emits a 256-bit vector register load instruction with an BP relative source address.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadVecRegByBpU256(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, int32_t offDisp)
{
#ifdef RT_ARCH_AMD64
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);

    /* vmovdqu reg256, mem256 */
    pbCodeBuf[off++] = X86_OP_VEX2;
    pbCodeBuf[off++] = X86_OP_VEX2_BYTE1_MAKE_NO_VVVV(iVecRegDst >= 8, true /*f256BitAvx*/, X86_OP_VEX2_BYTE1_P_0F3H);
    pbCodeBuf[off++] = 0x6f;
    return iemNativeEmitGprByBpDisp(pbCodeBuf, off, iVecRegDst, offDisp, pReNative);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES two consecutive vector registers for the 256-bit value. */
    Assert(!(iVecRegDst & 0x1));
    off = iemNativeEmitGprByBpLdSt(pReNative, off, iVecRegDst,     offDisp,                      kArmv8A64InstrLdStType_Ld_Vr_128, sizeof(RTUINT128U));
    return iemNativeEmitGprByBpLdSt(pReNative, off, iVecRegDst + 1, offDisp + sizeof(RTUINT128U), kArmv8A64InstrLdStType_Ld_Vr_128, sizeof(RTUINT128U));
#else
# error "port me"
#endif
}


/**
 * Emits a load effective address to a GRP with an BP relative source address.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLeaGprByBp(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, int32_t offDisp)
{
#ifdef RT_ARCH_AMD64
    /* lea gprdst, [rbp + offDisp] */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
    if (iGprDst < 8)
        pbCodeBuf[off++] = X86_OP_REX_W;
    else
        pbCodeBuf[off++] = X86_OP_REX_W | X86_OP_REX_R;
    pbCodeBuf[off++] = 0x8d;
    off = iemNativeEmitGprByBpDisp(pbCodeBuf, off, iGprDst, offDisp, pReNative);

#elif defined(RT_ARCH_ARM64)
    bool const     fSub       = offDisp < 0;
    uint32_t const offAbsDisp = (uint32_t)RT_ABS(offDisp);
    if (offAbsDisp <= 0xffffffU)
    {
        uint32_t * const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);
        if (offAbsDisp <= 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, ARMV8_A64_REG_BP, offAbsDisp);
        else
        {
            pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, ARMV8_A64_REG_BP, offAbsDisp >> 12,
                                                          true /*f64Bit*/, false /*fSetFlags*/, true /*fShift12*/);
            if (offAbsDisp & 0xfffU)
                pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprDst, offAbsDisp & 0xfff);
        }
    }
    else
    {
        Assert(iGprDst != IEMNATIVE_REG_FIXED_PVMCPU);
        off = iemNativeEmitLoadGprImm64(pReNative, off, iGprDst, offAbsDisp);
        uint32_t * const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pCodeBuf[off++] = Armv8A64MkInstrAddSubReg(fSub, iGprDst, ARMV8_A64_REG_BP, iGprDst);
    }

#else
# error "port me"
#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a 64-bit GPR store with an BP relative destination address.
 *
 * @note May trash IEMNATIVE_REG_FIXED_TMP0.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGprByBp(PIEMRECOMPILERSTATE pReNative, uint32_t off, int32_t offDisp, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    /* mov qword [rbp + offDisp], gprdst */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
    if (iGprSrc < 8)
        pbCodeBuf[off++] = X86_OP_REX_W;
    else
        pbCodeBuf[off++] = X86_OP_REX_W | X86_OP_REX_R;
    pbCodeBuf[off++] = 0x89;
    return iemNativeEmitGprByBpDisp(pbCodeBuf, off, iGprSrc, offDisp, pReNative);

#elif defined(RT_ARCH_ARM64)
    if (offDisp >= 0 && offDisp < 4096 * 8 && !((uint32_t)offDisp & 7))
    {
        /* str w/ unsigned imm12 (scaled) */
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_St_Dword, iGprSrc,
                                                      ARMV8_A64_REG_BP, (uint32_t)offDisp / 8);
    }
    else if (offDisp >= -256 && offDisp <= 256)
    {
        /* stur w/ signed imm9 (unscaled) */
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrSturLdur(kArmv8A64InstrLdStType_St_Dword, iGprSrc, ARMV8_A64_REG_BP, offDisp);
    }
    else if ((uint32_t)-offDisp < (unsigned)_4K)
    {
        /* Use temporary indexing register w/ sub uimm12. */
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);
        pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, IEMNATIVE_REG_FIXED_TMP0,
                                                         ARMV8_A64_REG_BP, (uint32_t)-offDisp);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_St_Dword, iGprSrc, IEMNATIVE_REG_FIXED_TMP0, 0);
    }
    else
    {
        /* Use temporary indexing register. */
        off = iemNativeEmitLoadGprImm64(pReNative, off, IEMNATIVE_REG_FIXED_TMP0, (uint32_t)offDisp);
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(kArmv8A64InstrLdStType_St_Dword, iGprSrc, ARMV8_A64_REG_BP,
                                                       IEMNATIVE_REG_FIXED_TMP0, kArmv8A64InstrLdStExtend_Sxtw);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;

#else
# error "Port me!"
#endif
}


/**
 * Emits a 64-bit immediate store with an BP relative destination address.
 *
 * @note May trash IEMNATIVE_REG_FIXED_TMP0.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreImm64ByBp(PIEMRECOMPILERSTATE pReNative, uint32_t off, int32_t offDisp, uint64_t uImm64)
{
#ifdef RT_ARCH_AMD64
    if ((int64_t)uImm64 == (int32_t)uImm64)
    {
        /* mov qword [rbp + offDisp], imm32 - sign extended */
        uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 11);
        pbCodeBuf[off++] = X86_OP_REX_W;
        pbCodeBuf[off++] = 0xc7;
        if (offDisp < 128 && offDisp >= -128)
        {
            pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM1, 0, X86_GREG_xBP);
            pbCodeBuf[off++] = (uint8_t)offDisp;
        }
        else
        {
            pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM4, 0, X86_GREG_xBP);
            pbCodeBuf[off++] = RT_BYTE1((uint32_t)offDisp);
            pbCodeBuf[off++] = RT_BYTE2((uint32_t)offDisp);
            pbCodeBuf[off++] = RT_BYTE3((uint32_t)offDisp);
            pbCodeBuf[off++] = RT_BYTE4((uint32_t)offDisp);
        }
        pbCodeBuf[off++] = RT_BYTE1(uImm64);
        pbCodeBuf[off++] = RT_BYTE2(uImm64);
        pbCodeBuf[off++] = RT_BYTE3(uImm64);
        pbCodeBuf[off++] = RT_BYTE4(uImm64);
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
        return off;
    }
#endif

    /* Load tmp0, imm64; Store tmp to bp+disp. */
    off = iemNativeEmitLoadGprImm64(pReNative, off, IEMNATIVE_REG_FIXED_TMP0, uImm64);
    return iemNativeEmitStoreGprByBp(pReNative, off, offDisp, IEMNATIVE_REG_FIXED_TMP0);
}


/**
 * Emits a 128-bit vector register store with an BP relative destination address.
 *
 * @note May trash IEMNATIVE_REG_FIXED_TMP0.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreVecRegByBpU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, int32_t offDisp, uint8_t iVecRegSrc)
{
#ifdef RT_ARCH_AMD64
    /* movdqu [rbp + offDisp], vecsrc */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
    pbCodeBuf[off++] = 0xf3;
    if (iVecRegSrc >= 8)
        pbCodeBuf[off++] =  X86_OP_REX_R;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0x7f;
    return iemNativeEmitGprByBpDisp(pbCodeBuf, off, iVecRegSrc, offDisp, pReNative);

#elif defined(RT_ARCH_ARM64)
    if (offDisp >= 0 && offDisp < 4096 * 8 && !((uint32_t)offDisp & 7))
    {
        /* str w/ unsigned imm12 (scaled) */
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_St_Vr_128, iVecRegSrc,
                                                      ARMV8_A64_REG_BP, (uint32_t)offDisp / 8);
    }
    else if (offDisp >= -256 && offDisp <= 256)
    {
        /* stur w/ signed imm9 (unscaled) */
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrSturLdur(kArmv8A64InstrLdStType_St_Vr_128, iVecRegSrc, ARMV8_A64_REG_BP, offDisp);
    }
    else if ((uint32_t)-offDisp < (unsigned)_4K)
    {
        /* Use temporary indexing register w/ sub uimm12. */
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);
        pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, IEMNATIVE_REG_FIXED_TMP0,
                                                         ARMV8_A64_REG_BP, (uint32_t)-offDisp);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRUOff(kArmv8A64InstrLdStType_St_Vr_128, iVecRegSrc, IEMNATIVE_REG_FIXED_TMP0, 0);
    }
    else
    {
        /* Use temporary indexing register. */
        off = iemNativeEmitLoadGprImm64(pReNative, off, IEMNATIVE_REG_FIXED_TMP0, (uint32_t)offDisp);
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(kArmv8A64InstrLdStType_St_Vr_128, iVecRegSrc, ARMV8_A64_REG_BP,
                                                       IEMNATIVE_REG_FIXED_TMP0, kArmv8A64InstrLdStExtend_Sxtw);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;

#else
# error "Port me!"
#endif
}


/**
 * Emits a 256-bit vector register store with an BP relative destination address.
 *
 * @note May trash IEMNATIVE_REG_FIXED_TMP0.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreVecRegByBpU256(PIEMRECOMPILERSTATE pReNative, uint32_t off, int32_t offDisp, uint8_t iVecRegSrc)
{
#ifdef RT_ARCH_AMD64
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);

    /* vmovdqu mem256, reg256 */
    pbCodeBuf[off++] = X86_OP_VEX2;
    pbCodeBuf[off++] = X86_OP_VEX2_BYTE1_MAKE_NO_VVVV(iVecRegSrc >= 8, true /*f256BitAvx*/, X86_OP_VEX2_BYTE1_P_0F3H);
    pbCodeBuf[off++] = 0x7f;
    return iemNativeEmitGprByBpDisp(pbCodeBuf, off, iVecRegSrc, offDisp, pReNative);
#elif defined(RT_ARCH_ARM64)
    Assert(!(iVecRegSrc & 0x1));
    off =  iemNativeEmitStoreVecRegByBpU128(pReNative, off, offDisp,                      iVecRegSrc);
    return iemNativeEmitStoreVecRegByBpU128(pReNative, off, offDisp + sizeof(RTUINT128U), iVecRegSrc + 1);
#else
# error "Port me!"
#endif
}

#if defined(RT_ARCH_ARM64)

/**
 * Common bit of iemNativeEmitLoadGprFromVCpuU64 and friends.
 *
 * @note Odd and large @a offDisp values requires a temporary, unless it's a
 *       load and @a iGprReg differs from @a iGprBase.  Will assert / throw if
 *       caller does not heed this.
 *
 * @note DON'T try this with prefetch.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitGprByGprLdStEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprReg, uint8_t iGprBase, int32_t offDisp,
                            ARMV8A64INSTRLDSTTYPE enmOperation, unsigned cbData, uint8_t iGprTmp = UINT8_MAX)
{
    if ((uint32_t)offDisp < _4K * cbData && !((uint32_t)offDisp & (cbData - 1)))
    {
        /* Use the unsigned variant of ldr Wt, [<Xn|SP>, #off]. */
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(enmOperation, iGprReg, iGprBase, (uint32_t)offDisp / cbData);
    }
    else if (   (   !ARMV8A64INSTRLDSTTYPE_IS_STORE(enmOperation)
                 && iGprReg != iGprBase)
             || iGprTmp != UINT8_MAX)
    {
        /* The offset is too large, so we must load it into a register and use
           ldr Wt, [<Xn|SP>, (<Wm>|<Xm>)]. */
        /** @todo reduce by offVCpu by >> 3 or >> 2? if it saves instructions? */
        if (iGprTmp == UINT8_MAX)
            iGprTmp = iGprReg;
        off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, iGprTmp, (int64_t)offDisp);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(enmOperation, iGprReg, iGprBase, iGprTmp);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif
    return off;
}

/**
 * Common bit of iemNativeEmitLoadGprFromVCpuU64 and friends.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitGprByGprLdSt(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprReg,
                          uint8_t iGprBase, int32_t offDisp, ARMV8A64INSTRLDSTTYPE enmOperation, unsigned cbData)
{
    /*
     * There are a couple of ldr variants that takes an immediate offset, so
     * try use those if we can, otherwise we have to use the temporary register
     * help with the addressing.
     */
    if ((uint32_t)offDisp < _4K * cbData && !((uint32_t)offDisp & (cbData - 1)))
    {
        /* Use the unsigned variant of ldr Wt, [<Xn|SP>, #off]. */
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRUOff(enmOperation, iGprReg, iGprBase, (uint32_t)offDisp / cbData);
    }
    else
    {
        /* The offset is too large, so we must load it into a register and use
           ldr Wt, [<Xn|SP>, (<Wm>|<Xm>)]. */
        /** @todo reduce by offVCpu by >> 3 or >> 2? if it saves instructions? */
        uint8_t const idxTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, (int64_t)offDisp);

        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(enmOperation, iGprReg, iGprBase, idxTmpReg);

        iemNativeRegFreeTmpImm(pReNative, idxTmpReg);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}

/**
 * Common bit of iemNativeEmitLoadVecRegByGprU128 and friends.
 *
 * @note Odd and large @a offDisp values requires a temporary, unless it's a
 *       load and @a iGprReg differs from @a iGprBase.  Will assert / throw if
 *       caller does not heed this.
 *
 * @note DON'T try this with prefetch.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitVecRegByGprLdStEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecReg, uint8_t iGprBase, int32_t offDisp,
                               ARMV8A64INSTRLDSTTYPE enmOperation, unsigned cbData, uint8_t iGprTmp = UINT8_MAX)
{
    if ((uint32_t)offDisp < _4K * cbData && !((uint32_t)offDisp & (cbData - 1)))
    {
        /* Use the unsigned variant of ldr Wt, [<Xn|SP>, #off]. */
        pCodeBuf[off++] = Armv8A64MkInstrStLdRUOff(enmOperation, iVecReg, iGprBase, (uint32_t)offDisp / cbData);
    }
    else if (   !ARMV8A64INSTRLDSTTYPE_IS_STORE(enmOperation)
             || iGprTmp != UINT8_MAX)
    {
        /* The offset is too large, so we must load it into a register and use
           ldr Wt, [<Xn|SP>, (<Wm>|<Xm>)]. */
        /** @todo reduce by offVCpu by >> 3 or >> 2? if it saves instructions? */
        off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, iGprTmp, (int64_t)offDisp);
        pCodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(enmOperation, iVecReg, iGprBase, iGprTmp);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif
    return off;
}


/**
 * Common bit of iemNativeEmitLoadVecRegByGprU128 and friends.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitVecRegByGprLdSt(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecReg,
                             uint8_t iGprBase, int32_t offDisp, ARMV8A64INSTRLDSTTYPE enmOperation, unsigned cbData)
{
    /*
     * There are a couple of ldr variants that takes an immediate offset, so
     * try use those if we can, otherwise we have to use the temporary register
     * help with the addressing.
     */
    if ((uint32_t)offDisp < _4K * cbData && !((uint32_t)offDisp & (cbData - 1)))
    {
        /* Use the unsigned variant of ldr Wt, [<Xn|SP>, #off]. */
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRUOff(enmOperation, iVecReg, iGprBase, (uint32_t)offDisp / cbData);
    }
    else
    {
        /* The offset is too large, so we must load it into a register and use
           ldr Wt, [<Xn|SP>, (<Wm>|<Xm>)]. */
        /** @todo reduce by offVCpu by >> 3 or >> 2? if it saves instructions? */
        uint8_t const idxTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, (int64_t)offDisp);

        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrStLdRegIdx(enmOperation, iVecReg, iGprBase, idxTmpReg);

        iemNativeRegFreeTmpImm(pReNative, idxTmpReg);
    }
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}
#endif /* RT_ARCH_ARM64 */

/**
 * Emits a 64-bit GPR load via a GPR base address with a displacement.
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       -0x7ff8...0x7ff8 range will require a temporary register (@a iGprTmp) if
 *       @a iGprReg and @a iGprBase are the same. Will assert / throw if caller
 *       does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByGprU64Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprBase,
                               int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* mov reg64, mem64 */
    pCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x8b;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iGprDst, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprDst, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_Ld_Dword, sizeof(uint64_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 64-bit GPR load via a GPR base address with a displacement.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByGprU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprBase, int32_t offDisp)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGprByGprU64Ex(iemNativeInstrBufEnsure(pReNative, off, 8), off, iGprDst, iGprBase, offDisp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdSt(pReNative, off, iGprDst, iGprBase, offDisp, kArmv8A64InstrLdStType_Ld_Dword, sizeof(uint64_t));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 32-bit GPR load via a GPR base address with a displacement.
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       -0x3ffc...0x3ffc range will require a temporary register (@a iGprTmp)
 *       if @a iGprReg and @a iGprBase are the same. Will assert / throw if
 *       caller does not heed this.
 *
 * @note Bits 63 thru 32 in @a iGprDst will be cleared.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByGprU32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprBase,
                               int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* mov reg32, mem32 */
    if (iGprDst >= 8 || iGprBase >= 8)
        pCodeBuf[off++] = (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x8b;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iGprDst, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprDst, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_Ld_Word, sizeof(uint32_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 32-bit GPR load via a GPR base address with a displacement.
 * @note Bits 63 thru 32 in @a iGprDst will be cleared.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByGprU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprBase, int32_t offDisp)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGprByGprU32Ex(iemNativeInstrBufEnsure(pReNative, off, 8), off, iGprDst, iGprBase, offDisp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdSt(pReNative, off, iGprDst, iGprBase, offDisp, kArmv8A64InstrLdStType_Ld_Word, sizeof(uint32_t));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 32-bit GPR load via a GPR base address with a displacement,
 * sign-extending the value to 64 bits.
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       -0x3ffc...0x3ffc range will require a temporary register (@a iGprTmp)
 *       if @a iGprReg and @a iGprBase are the same. Will assert / throw if
 *       caller does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByGprU64SignExtendedFromS32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprBase,
                                                  int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* movsxd reg64, mem32 */
    pCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x63;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iGprDst, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprDst, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_Ld_SignWord64, sizeof(uint32_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 16-bit GPR load via a GPR base address with a displacement.
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       -0x1ffe...0x1ffe range will require a temporary register (@a iGprTmp)
 *       if @a iGprReg and @a iGprBase are the same. Will assert / throw if
 *       caller does not heed this.
 *
 * @note Bits 63 thru 16 in @a iGprDst will be cleared.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByGprU16Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprBase,
                               int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* movzx reg32, mem16 */
    if (iGprDst >= 8 || iGprBase >= 8)
        pCodeBuf[off++] = (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xb7;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iGprDst, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprDst, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_Ld_Half, sizeof(uint16_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 16-bit GPR load via a GPR base address with a displacement,
 * sign-extending the value to 64 bits.
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       -0x1ffe...0x1ffe range will require a temporary register (@a iGprTmp)
 *       if @a iGprReg and @a iGprBase are the same. Will assert / throw if
 *       caller does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByGprU64SignExtendedFromS16Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprBase,
                                                  int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* movsx reg64, mem16 */
    pCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xbf;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iGprDst, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprDst, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_Ld_SignHalf64, sizeof(uint16_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 16-bit GPR load via a GPR base address with a displacement,
 * sign-extending the value to 32 bits.
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       -0x1ffe...0x1ffe range will require a temporary register (@a iGprTmp)
 *       if @a iGprReg and @a iGprBase are the same. Will assert / throw if
 *       caller does not heed this.
 *
 * @note Bits 63 thru 32 in @a iGprDst will be cleared.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByGprU32SignExtendedFromS16Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprBase,
                                                  int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* movsx reg32, mem16 */
    if (iGprDst >= 8 || iGprBase >= 8)
        pCodeBuf[off++] = (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xbf;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iGprDst, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprDst, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_Ld_SignHalf32, sizeof(uint16_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 8-bit GPR load via a GPR base address with a displacement.
 *
 * @note ARM64: @a offDisp values not in the 0xfff...0xfff range will require a
 *       temporary register (@a iGprTmp) if @a iGprReg and @a iGprBase are the
 *       same. Will assert / throw if caller does not heed this.
 *
 * @note Bits 63 thru 8 in @a iGprDst will be cleared.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByGprU8Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprBase,
                              int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* movzx reg32, mem8 */
    if (iGprDst >= 8 || iGprBase >= 8)
        pCodeBuf[off++] = (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xb6;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iGprDst, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprDst, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_Ld_Byte, sizeof(uint8_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 8-bit GPR load via a GPR base address with a displacement,
 * sign-extending the value to 64 bits.
 *
 * @note ARM64: @a offDisp values not in the 0xfff...0xfff range will require a
 *       temporary register (@a iGprTmp) if @a iGprReg and @a iGprBase are the
 *       same. Will assert / throw if caller does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByGprU64SignExtendedFromS8Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprBase,
                                                 int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* movsx reg64, mem8 */
    pCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xbe;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iGprDst, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprDst, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_Ld_SignByte64, sizeof(uint8_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 8-bit GPR load via a GPR base address with a displacement,
 * sign-extending the value to 32 bits.
 *
 * @note ARM64: @a offDisp values not in the 0xfff...0xfff range will require a
 *       temporary register (@a iGprTmp) if @a iGprReg and @a iGprBase are the
 *       same. Will assert / throw if caller does not heed this.
 *
 * @note Bits 63 thru 32 in @a iGprDst will be cleared.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByGprU32SignExtendedFromS8Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprBase,
                                                 int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* movsx reg32, mem8 */
    if (iGprDst >= 8 || iGprBase >= 8)
        pCodeBuf[off++] = (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xbe;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iGprDst, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprDst, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_Ld_SignByte32, sizeof(uint8_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 8-bit GPR load via a GPR base address with a displacement,
 * sign-extending the value to 16 bits.
 *
 * @note ARM64: @a offDisp values not in the 0xfff...0xfff range will require a
 *       temporary register (@a iGprTmp) if @a iGprReg and @a iGprBase are the
 *       same. Will assert / throw if caller does not heed this.
 *
 * @note Bits 63 thru 16 in @a iGprDst will be cleared.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadGprByGprU16SignExtendedFromS8Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprBase,
                                                 int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* movsx reg32, mem8 */
    if (iGprDst >= 8 || iGprBase >= 8)
        pCodeBuf[off++] = (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xbe;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iGprDst, iGprBase, offDisp);
# if 1 /** @todo use 'movzx reg32, reg16' instead of 'and reg32, 0ffffh' ? */
    /* and reg32, 0xffffh */
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    pCodeBuf[off++] = 0x81;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4, iGprDst & 7);
    pCodeBuf[off++] = 0xff;
    pCodeBuf[off++] = 0xff;
    pCodeBuf[off++] = 0;
    pCodeBuf[off++] = 0;
# else
    /* movzx reg32, reg16 */
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B | X86_OP_REX_R;
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xb7;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprDst & 7);
# endif
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprDst, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_Ld_SignByte32, sizeof(uint8_t), iGprTmp);
    Assert(Armv8A64ConvertImmRImmS2Mask32(15, 0) == 0xffff);
    pCodeBuf[off++] = Armv8A64MkInstrAndImm(iGprDst, iGprDst, 15, 0, false /*64Bit*/);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 128-bit vector register load via a GPR base address with a displacement.
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       -0x7ff8...0x7ff8 range will require a temporary register (@a iGprTmp) if
 *       @a iGprReg and @a iGprBase are the same. Will assert / throw if caller
 *       does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadVecRegByGprU128Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iGprBase,
                                   int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* movdqu reg128, mem128 */
    pCodeBuf[off++] = 0xf3;
    if (iVecRegDst >= 8 || iGprBase >= 8)
        pCodeBuf[off++] = (iVecRegDst < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0x6f;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iVecRegDst, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitVecRegByGprLdStEx(pCodeBuf, off, iVecRegDst, iGprBase, offDisp,
                                         kArmv8A64InstrLdStType_Ld_Vr_128, sizeof(RTUINT128U), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 128-bit GPR load via a GPR base address with a displacement.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadVecRegByGprU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iGprBase, int32_t offDisp)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadVecRegByGprU128Ex(iemNativeInstrBufEnsure(pReNative, off, 8), off, iVecRegDst, iGprBase, offDisp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitVecRegByGprLdSt(pReNative, off, iVecRegDst, iGprBase, offDisp, kArmv8A64InstrLdStType_Ld_Vr_128, sizeof(RTUINT128U));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 256-bit vector register load via a GPR base address with a displacement.
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       -0x7ff8...0x7ff8 range will require a temporary register (@a iGprTmp) if
 *       @a iGprReg and @a iGprBase are the same. Will assert / throw if caller
 *       does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadVecRegByGprU256Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iGprBase,
                                   int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* vmovdqu reg256, mem256 */
    pCodeBuf[off++] = X86_OP_VEX3;
    pCodeBuf[off++] =   (iVecRegDst < 8 ? X86_OP_VEX3_BYTE1_R : 0)
                      | X86_OP_VEX3_BYTE1_X
                      | (iGprBase < 8 ? X86_OP_VEX3_BYTE1_B : 0)
                      | UINT8_C(0x01);
    pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE_NO_VVVV(false /*f64BitOpSz*/, true /*f256BitAvx*/, X86_OP_VEX2_BYTE1_P_0F3H);
    pCodeBuf[off++] = 0x6f;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iVecRegDst, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    Assert(!(iVecRegDst & 0x1));
    off = iemNativeEmitVecRegByGprLdStEx(pCodeBuf, off, iVecRegDst, iGprBase, offDisp,
                                         kArmv8A64InstrLdStType_Ld_Vr_128, sizeof(RTUINT128U), iGprTmp);
    off = iemNativeEmitVecRegByGprLdStEx(pCodeBuf, off, iVecRegDst + 1, iGprBase, offDisp + sizeof(RTUINT128U),
                                         kArmv8A64InstrLdStType_Ld_Vr_128, sizeof(RTUINT128U), iGprTmp);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 256-bit GPR load via a GPR base address with a displacement.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitLoadVecRegByGprU256(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iGprBase, int32_t offDisp)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadVecRegByGprU128Ex(iemNativeInstrBufEnsure(pReNative, off, 8), off, iVecRegDst, iGprBase, offDisp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    Assert(!(iVecRegDst & 0x1));
    off = iemNativeEmitVecRegByGprLdSt(pReNative, off, iVecRegDst, iGprBase, offDisp,
                                       kArmv8A64InstrLdStType_Ld_Vr_128, sizeof(RTUINT128U));
    off = iemNativeEmitVecRegByGprLdSt(pReNative, off, iVecRegDst + 1, iGprBase, offDisp + sizeof(RTUINT128U),
                                       kArmv8A64InstrLdStType_Ld_Vr_128, sizeof(RTUINT128U));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 64-bit GPR store via a GPR base address with a displacement.
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       0x7ff8...0x7ff8 range will require a temporary register (@a iGprTmp) if
 *       @a iGprReg and @a iGprBase are the same. Will assert / throw if caller
 *       does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStoreGpr64ByGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprSrc, uint8_t iGprBase,
                               int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* mov mem64, reg64 */
    pCodeBuf[off++] = X86_OP_REX_W | (iGprSrc < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x89;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iGprSrc, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprSrc, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_St_Dword, sizeof(uint64_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 32-bit GPR store via a GPR base address with a displacement.
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       0x3ffc...0x3ffc range will require a temporary register (@a iGprTmp) if
 *       @a iGprReg and @a iGprBase are the same. Will assert / throw if caller
 *       does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStoreGpr32ByGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprSrc, uint8_t iGprBase,
                               int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* mov mem32, reg32 */
    if (iGprSrc >= 8 || iGprBase >= 8)
        pCodeBuf[off++] = (iGprSrc < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x89;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iGprSrc, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprSrc, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_St_Word, sizeof(uint32_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 16-bit GPR store via a GPR base address with a displacement.
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       0x1ffe...0x1ffe range will require a temporary register (@a iGprTmp) if
 *       @a iGprReg and @a iGprBase are the same. Will assert / throw if caller
 *       does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStoreGpr16ByGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprSrc, uint8_t iGprBase,
                               int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* mov mem16, reg16 */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iGprSrc >= 8 || iGprBase >= 8)
        pCodeBuf[off++] = (iGprSrc < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x89;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iGprSrc, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprSrc, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_St_Half, sizeof(uint16_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 8-bit GPR store via a GPR base address with a displacement.
 *
 * @note ARM64: @a offDisp values not in the 0xfff...0xfff range will require a
 *       temporary register (@a iGprTmp) if @a iGprReg and @a iGprBase are the
 *       same. Will assert / throw if caller does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStoreGpr8ByGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprSrc, uint8_t iGprBase,
                              int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* mov mem8, reg8 */
    if (iGprSrc >= 8 || iGprBase >= 8)
        pCodeBuf[off++] = (iGprSrc < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    else if (iGprSrc >= 4)
        pCodeBuf[off++] = X86_OP_REX;
    pCodeBuf[off++] = 0x88;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iGprSrc, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprSrc, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_St_Byte, sizeof(uint8_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 64-bit immediate store via a GPR base address with a displacement.
 *
 * @note This will always require @a iGprTmpImm on ARM (except for uImm=0), on
 *       AMD64 it depends on the immediate value.
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       0x7ff8...0x7ff8 range will require a temporary register (@a iGprTmp) if
 *       @a iGprReg and @a iGprBase are the same. Will assert / throw if caller
 *       does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStoreImm64ByGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint64_t uImm, uint8_t iGprBase,
                               uint8_t iGprImmTmp = UINT8_MAX, int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    if ((int32_t)uImm == (int64_t)uImm)
    {
        /* mov mem64, imm32 (sign-extended) */
        pCodeBuf[off++] = X86_OP_REX_W | (iGprBase < 8 ? 0 : X86_OP_REX_B);
        pCodeBuf[off++] = 0xc7;
        off = iemNativeEmitGprByGprDisp(pCodeBuf, off, 0, iGprBase, offDisp);
        pCodeBuf[off++] = RT_BYTE1(uImm);
        pCodeBuf[off++] = RT_BYTE2(uImm);
        pCodeBuf[off++] = RT_BYTE3(uImm);
        pCodeBuf[off++] = RT_BYTE4(uImm);
    }
    else if (iGprImmTmp != UINT8_MAX || iGprTmp != UINT8_MAX)
    {
        /* require temporary register. */
        if (iGprImmTmp == UINT8_MAX)
            iGprImmTmp = iGprTmp;
        off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, iGprImmTmp, uImm);
        off = iemNativeEmitStoreGpr64ByGprEx(pCodeBuf, off, iGprImmTmp, iGprBase, offDisp);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#elif defined(RT_ARCH_ARM64)
    if (uImm == 0)
        iGprImmTmp = ARMV8_A64_REG_XZR;
    else
    {
        Assert(iGprImmTmp < 31);
        off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, iGprImmTmp, uImm);
    }
    off = iemNativeEmitStoreGpr64ByGprEx(pCodeBuf, off, iGprImmTmp, iGprBase, offDisp, iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 32-bit GPR store via a GPR base address with a displacement.
 *
 * @note This will always require @a iGprTmpImm on ARM64 (except for uImm=0).
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       0x3ffc...0x3ffc range will require a temporary register (@a iGprTmp) if
 *       @a iGprReg and @a iGprBase are the same. Will assert / throw if caller
 *       does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStoreImm32ByGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint32_t uImm, uint8_t iGprBase,
                               uint8_t iGprImmTmp = UINT8_MAX, int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* mov mem32, imm32 */
    if (iGprBase >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    pCodeBuf[off++] = 0xc7;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, 0, iGprBase, offDisp);
    pCodeBuf[off++] = RT_BYTE1(uImm);
    pCodeBuf[off++] = RT_BYTE2(uImm);
    pCodeBuf[off++] = RT_BYTE3(uImm);
    pCodeBuf[off++] = RT_BYTE4(uImm);
    RT_NOREF(iGprImmTmp, iGprTmp);

#elif defined(RT_ARCH_ARM64)
    Assert(iGprImmTmp < 31);
    if (uImm == 0)
        iGprImmTmp = ARMV8_A64_REG_XZR;
    else
    {
        Assert(iGprImmTmp < 31);
        off = iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off, iGprImmTmp, uImm);
    }
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprImmTmp, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_St_Word, sizeof(uint32_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 16-bit GPR store via a GPR base address with a displacement.
 *
 * @note This will always require @a iGprTmpImm on ARM64 (except for uImm=0).
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       0x1ffe...0x1ffe range will require a temporary register (@a iGprTmp) if
 *       @a iGprReg and @a iGprBase are the same. Will assert / throw if caller
 *       does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStoreImm16ByGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint16_t uImm, uint8_t iGprBase,
                               uint8_t iGprImmTmp = UINT8_MAX, int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* mov mem16, imm16 */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iGprBase >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    pCodeBuf[off++] = 0xc7;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, 0, iGprBase, offDisp);
    pCodeBuf[off++] = RT_BYTE1(uImm);
    pCodeBuf[off++] = RT_BYTE2(uImm);
    RT_NOREF(iGprImmTmp, iGprTmp);

#elif defined(RT_ARCH_ARM64)
    if (uImm == 0)
        iGprImmTmp = ARMV8_A64_REG_XZR;
    else
    {
        Assert(iGprImmTmp < 31);
        pCodeBuf[off++] = Armv8A64MkInstrMovZ(iGprImmTmp, uImm);
    }
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprImmTmp, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_St_Half, sizeof(uint16_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 8-bit GPR store via a GPR base address with a displacement.
 *
 * @note This will always require @a iGprTmpImm on ARM64 (except for uImm=0).
 *
 * @note ARM64: @a offDisp values not in the 0xfff...0xfff range will require a
 *       temporary register (@a iGprTmp) if @a iGprReg and @a iGprBase are the
 *       same. Will assert / throw if caller does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStoreImm8ByGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t uImm, uint8_t iGprBase,
                              uint8_t iGprImmTmp = UINT8_MAX, int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* mov mem8, imm8 */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iGprBase >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    pCodeBuf[off++] = 0xc6;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, 0, iGprBase, offDisp);
    pCodeBuf[off++] = uImm;
    RT_NOREF(iGprImmTmp, iGprTmp);

#elif defined(RT_ARCH_ARM64)
    if (uImm == 0)
        iGprImmTmp = ARMV8_A64_REG_XZR;
    else
    {
        Assert(iGprImmTmp < 31);
        pCodeBuf[off++] = Armv8A64MkInstrMovZ(iGprImmTmp, uImm);
    }
    off = iemNativeEmitGprByGprLdStEx(pCodeBuf, off, iGprImmTmp, iGprBase, offDisp,
                                      kArmv8A64InstrLdStType_St_Byte, sizeof(uint8_t), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 128-bit vector register store via a GPR base address with a displacement.
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       -0x7ff8...0x7ff8 range will require a temporary register (@a iGprTmp) if
 *       @a iGprReg and @a iGprBase are the same. Will assert / throw if caller
 *       does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStoreVecRegByGprU128Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iGprBase,
                                   int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* movdqu mem128, reg128 */
    pCodeBuf[off++] = 0xf3;
    if (iVecRegDst >= 8 || iGprBase >= 8)
        pCodeBuf[off++] = (iVecRegDst < 8 ? 0 : X86_OP_REX_R) | (iGprBase < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0x7f;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iVecRegDst, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitVecRegByGprLdStEx(pCodeBuf, off, iVecRegDst, iGprBase, offDisp,
                                         kArmv8A64InstrLdStType_St_Vr_128, sizeof(RTUINT128U), iGprTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 128-bit vector register store via a GPR base address with a displacement.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreVecRegByGprU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iGprBase, int32_t offDisp)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitStoreVecRegByGprU128Ex(iemNativeInstrBufEnsure(pReNative, off, 8), off, iVecRegDst, iGprBase, offDisp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitVecRegByGprLdSt(pReNative, off, iVecRegDst, iGprBase, offDisp, kArmv8A64InstrLdStType_St_Vr_128, sizeof(RTUINT128U));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 256-bit vector register store via a GPR base address with a displacement.
 *
 * @note ARM64: Misaligned @a offDisp values and values not in the
 *       -0x7ff8...0x7ff8 range will require a temporary register (@a iGprTmp) if
 *       @a iGprReg and @a iGprBase are the same. Will assert / throw if caller
 *       does not heed this.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStoreVecRegByGprU256Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iGprBase,
                                    int32_t offDisp = 0, uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    /* vmovdqu mem256, reg256 */
    pCodeBuf[off++] = X86_OP_VEX3;
    pCodeBuf[off++] =   (iVecRegDst < 8 ? X86_OP_VEX3_BYTE1_R : 0)
                      | X86_OP_VEX3_BYTE1_X
                      | (iGprBase < 8 ? X86_OP_VEX3_BYTE1_B : 0)
                      | UINT8_C(0x01);
    pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE_NO_VVVV(false /*f64BitOpSz*/, true /*f256BitAvx*/, X86_OP_VEX2_BYTE1_P_0F3H);
    pCodeBuf[off++] = 0x7f;
    off = iemNativeEmitGprByGprDisp(pCodeBuf, off, iVecRegDst, iGprBase, offDisp);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    Assert(!(iVecRegDst & 0x1));
    off = iemNativeEmitVecRegByGprLdStEx(pCodeBuf, off, iVecRegDst, iGprBase, offDisp,
                                         kArmv8A64InstrLdStType_St_Vr_128, sizeof(RTUINT128U), iGprTmp);
    off = iemNativeEmitVecRegByGprLdStEx(pCodeBuf, off, iVecRegDst + 1, iGprBase, offDisp + sizeof(RTUINT128U),
                                         kArmv8A64InstrLdStType_St_Vr_128, sizeof(RTUINT128U), iGprTmp);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 256-bit GPR load via a GPR base address with a displacement.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreVecRegByGprU256(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iGprBase, int32_t offDisp)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitStoreVecRegByGprU256Ex(iemNativeInstrBufEnsure(pReNative, off, 8), off, iVecRegDst, iGprBase, offDisp);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#elif defined(RT_ARCH_ARM64)
    Assert(!(iVecRegDst & 0x1));
    off = iemNativeEmitVecRegByGprLdSt(pReNative, off, iVecRegDst, iGprBase, offDisp,
                                       kArmv8A64InstrLdStType_St_Vr_128, sizeof(RTUINT128U));
    off = iemNativeEmitVecRegByGprLdSt(pReNative, off, iVecRegDst + 1, iGprBase, offDisp + sizeof(RTUINT128U),
                                       kArmv8A64InstrLdStType_St_Vr_128, sizeof(RTUINT128U));

#else
# error "port me"
#endif
    return off;
}



/*********************************************************************************************************************************
*   Subtraction and Additions                                                                                                    *
*********************************************************************************************************************************/

/**
 * Emits subtracting a 64-bit GPR from another, storing the result in the first.
 * @note The AMD64 version sets flags.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSubTwoGprs(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSubtrahend)
{
#if defined(RT_ARCH_AMD64)
    /* sub Gv,Ev */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3);
    pbCodeBuf[off++] = (iGprDst < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_R)
                     | (iGprSubtrahend < 8 ? 0 : X86_OP_REX_B);
    pbCodeBuf[off++] = 0x2b;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSubtrahend & 7);

#elif defined(RT_ARCH_ARM64)
    uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = Armv8A64MkInstrSubReg(iGprDst, iGprDst, iGprSubtrahend);

#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits subtracting a 32-bit GPR from another, storing the result in the first.
 * @note The AMD64 version sets flags.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSubTwoGprs32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSubtrahend)
{
#if defined(RT_ARCH_AMD64)
    /* sub Gv,Ev */
    if (iGprDst >= 8 || iGprSubtrahend >= 8)
        pCodeBuf[off++] = (iGprDst        < 8 ? 0 : X86_OP_REX_R)
                        | (iGprSubtrahend < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x2b;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSubtrahend & 7);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrSubReg(iGprDst, iGprDst, iGprSubtrahend, false /*f64Bit*/);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits subtracting a 32-bit GPR from another, storing the result in the first.
 * @note The AMD64 version sets flags.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSubTwoGprs32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSubtrahend)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitSubTwoGprs32Ex(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst, iGprSubtrahend);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSubTwoGprs32Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iGprSubtrahend);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a 64-bit GPR subtract with a signed immediate subtrahend.
 *
 * This will optimize using DEC/INC/whatever, so try avoid flag dependencies.
 *
 * @note Larger constants will require a temporary register.  Failing to specify
 *       one when needed will trigger fatal assertion / throw.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitSubGprImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, int64_t iSubtrahend,
                         uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    pCodeBuf[off++] = iGprDst >= 8 ? X86_OP_REX_W | X86_OP_REX_B : X86_OP_REX_W;
    if (iSubtrahend == 1)
    {
        /* dec r/m64 */
        pCodeBuf[off++] = 0xff;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 1, iGprDst & 7);
    }
    else if (iSubtrahend == -1)
    {
        /* inc r/m64 */
        pCodeBuf[off++] = 0xff;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
    }
    else if ((int8_t)iSubtrahend == iSubtrahend)
    {
        /* sub r/m64, imm8 */
        pCodeBuf[off++] = 0x83;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 5, iGprDst & 7);
        pCodeBuf[off++] = (uint8_t)iSubtrahend;
    }
    else if ((int32_t)iSubtrahend == iSubtrahend)
    {
        /* sub r/m64, imm32 */
        pCodeBuf[off++] = 0x81;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 5, iGprDst & 7);
        pCodeBuf[off++] = RT_BYTE1((uint64_t)iSubtrahend);
        pCodeBuf[off++] = RT_BYTE2((uint64_t)iSubtrahend);
        pCodeBuf[off++] = RT_BYTE3((uint64_t)iSubtrahend);
        pCodeBuf[off++] = RT_BYTE4((uint64_t)iSubtrahend);
    }
    else if (iGprTmp != UINT8_MAX)
    {
        off = iemNativeEmitLoadGprImmEx(pCodeBuf, off - 1, iGprTmp, (uint64_t)iSubtrahend);
        /* sub r/m64, r64 */
        pCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_B) | (iGprTmp < 8 ? 0 : X86_OP_REX_R);
        pCodeBuf[off++] = 0x29;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprTmp & 7, iGprDst & 7);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#elif defined(RT_ARCH_ARM64)
    uint32_t uAbsSubtrahend = RT_ABS(iSubtrahend);
    if (uAbsSubtrahend < 4096)
    {
        if (iSubtrahend >= 0)
            pCodeBuf[off++] = Armv8A64MkInstrSubUImm12(iGprDst, iGprDst, uAbsSubtrahend);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(iGprDst, iGprDst, uAbsSubtrahend);
    }
    else if (uAbsSubtrahend <= 0xfff000 && !(uAbsSubtrahend & 0xfff))
    {
        if (iSubtrahend >= 0)
            pCodeBuf[off++] = Armv8A64MkInstrSubUImm12(iGprDst, iGprDst, uAbsSubtrahend >> 12,
                                                       true /*f64Bit*/, false /*fSetFlags*/, true /*fShift*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(iGprDst, iGprDst, uAbsSubtrahend >> 12,
                                                       true /*f64Bit*/, false /*fSetFlags*/, true /*fShift*/);
    }
    else if (iGprTmp != UINT8_MAX)
    {
        off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, iGprTmp, (uint64_t)iSubtrahend);
        pCodeBuf[off++] = Armv8A64MkInstrSubReg(iGprDst, iGprDst, iGprTmp);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits a 64-bit GPR subtract with a signed immediate subtrahend.
 *
 * @note Larger constants will require a temporary register.  Failing to specify
 *       one when needed will trigger fatal assertion / throw.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSubGprImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, int64_t iSubtrahend,
                       uint8_t iGprTmp = UINT8_MAX)

{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSubGprImmEx(iemNativeInstrBufEnsure(pReNative, off, 13), off, iGprDst, iSubtrahend, iGprTmp);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSubGprImmEx(iemNativeInstrBufEnsure(pReNative, off, 5), off, iGprDst, iSubtrahend, iGprTmp);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a 32-bit GPR subtract with a signed immediate subtrahend.
 *
 * This will optimize using DEC/INC/whatever, so try avoid flag dependencies.
 *
 * @note ARM64: Larger constants will require a temporary register.  Failing to
 *       specify one when needed will trigger fatal assertion / throw.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitSubGpr32ImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, int32_t iSubtrahend,
                           uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    if (iSubtrahend == 1)
    {
        /* dec r/m32 */
        pCodeBuf[off++] = 0xff;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 1, iGprDst & 7);
    }
    else if (iSubtrahend == -1)
    {
        /* inc r/m32 */
        pCodeBuf[off++] = 0xff;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
    }
    else if (iSubtrahend < 128 && iSubtrahend >= -128)
    {
        /* sub r/m32, imm8 */
        pCodeBuf[off++] = 0x83;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 5, iGprDst & 7);
        pCodeBuf[off++] = (uint8_t)iSubtrahend;
    }
    else
    {
        /* sub r/m32, imm32 */
        pCodeBuf[off++] = 0x81;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 5, iGprDst & 7);
        pCodeBuf[off++] = RT_BYTE1(iSubtrahend);
        pCodeBuf[off++] = RT_BYTE2(iSubtrahend);
        pCodeBuf[off++] = RT_BYTE3(iSubtrahend);
        pCodeBuf[off++] = RT_BYTE4(iSubtrahend);
    }
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    uint32_t uAbsSubtrahend = RT_ABS(iSubtrahend);
    if (uAbsSubtrahend < 4096)
    {
        if (iSubtrahend >= 0)
            pCodeBuf[off++] = Armv8A64MkInstrSubUImm12(iGprDst, iGprDst, uAbsSubtrahend, false /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(iGprDst, iGprDst, uAbsSubtrahend, false /*f64Bit*/);
    }
    else if (uAbsSubtrahend <= 0xfff000 && !(uAbsSubtrahend & 0xfff))
    {
        if (iSubtrahend >= 0)
            pCodeBuf[off++] = Armv8A64MkInstrSubUImm12(iGprDst, iGprDst, uAbsSubtrahend >> 12,
                                                       false /*f64Bit*/, false /*fSetFlags*/, true /*fShift*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(iGprDst, iGprDst, uAbsSubtrahend >> 12,
                                                       false /*f64Bit*/, false /*fSetFlags*/, true /*fShift*/);
    }
    else if (iGprTmp != UINT8_MAX)
    {
        off = iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off, iGprTmp, (uint32_t)iSubtrahend);
        pCodeBuf[off++] = Armv8A64MkInstrSubReg(iGprDst, iGprDst, iGprTmp, false /*f64Bit*/);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits a 32-bit GPR subtract with a signed immediate subtrahend.
 *
 * @note ARM64: Larger constants will require a temporary register.  Failing to
 *       specify one when needed will trigger fatal assertion / throw.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSubGpr32Imm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, int32_t iSubtrahend,
                         uint8_t iGprTmp = UINT8_MAX)

{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSubGprImmEx(iemNativeInstrBufEnsure(pReNative, off, 7), off, iGprDst, iSubtrahend, iGprTmp);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSubGprImmEx(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst, iSubtrahend, iGprTmp);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a 16-bit GPR subtract with a signed immediate subtrahend.
 *
 * This will optimize using DEC/INC/whatever and ARM64 will not set flags,
 * so not suitable as a base for conditional jumps.
 *
 * @note AMD64: Will only update the lower 16 bits of the register.
 * @note ARM64: Will update the entire register.
 * @note ARM64: Larger constants will require a temporary register.  Failing to
 *       specify one when needed will trigger fatal assertion / throw.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitSubGpr16ImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, int16_t iSubtrahend,
                           uint8_t iGprTmp = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    if (iSubtrahend == 1)
    {
        /* dec r/m16 */
        pCodeBuf[off++] = 0xff;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 1, iGprDst & 7);
    }
    else if (iSubtrahend == -1)
    {
        /* inc r/m16 */
        pCodeBuf[off++] = 0xff;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
    }
    else if ((int8_t)iSubtrahend == iSubtrahend)
    {
        /* sub r/m16, imm8 */
        pCodeBuf[off++] = 0x83;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 5, iGprDst & 7);
        pCodeBuf[off++] = (uint8_t)iSubtrahend;
    }
    else
    {
        /* sub r/m16, imm16 */
        pCodeBuf[off++] = 0x81;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 5, iGprDst & 7);
        pCodeBuf[off++] = RT_BYTE1((uint16_t)iSubtrahend);
        pCodeBuf[off++] = RT_BYTE2((uint16_t)iSubtrahend);
    }
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    uint32_t uAbsSubtrahend = RT_ABS(iSubtrahend);
    if (uAbsSubtrahend < 4096)
    {
        if (iSubtrahend >= 0)
            pCodeBuf[off++] = Armv8A64MkInstrSubUImm12(iGprDst, iGprDst, uAbsSubtrahend, false /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(iGprDst, iGprDst, uAbsSubtrahend, false /*f64Bit*/);
    }
    else if (uAbsSubtrahend <= 0xfff000 && !(uAbsSubtrahend & 0xfff))
    {
        if (iSubtrahend >= 0)
            pCodeBuf[off++] = Armv8A64MkInstrSubUImm12(iGprDst, iGprDst, uAbsSubtrahend >> 12,
                                                       false /*f64Bit*/, false /*fSetFlags*/, true /*fShift*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(iGprDst, iGprDst, uAbsSubtrahend >> 12,
                                                       false /*f64Bit*/, false /*fSetFlags*/, true /*fShift*/);
    }
    else if (iGprTmp != UINT8_MAX)
    {
        off = iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off, iGprTmp, (uint32_t)iSubtrahend);
        pCodeBuf[off++] = Armv8A64MkInstrSubReg(iGprDst, iGprDst, iGprTmp, false /*f64Bit*/);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif
    pCodeBuf[off++] = Armv8A64MkInstrAndImm(iGprDst, iGprDst, 15, 0, false /*f64Bit*/);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits adding a 64-bit GPR to another, storing the result in the first.
 * @note The AMD64 version sets flags.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitAddTwoGprsEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprAddend)
{
#if defined(RT_ARCH_AMD64)
    /* add Gv,Ev */
    pCodeBuf[off++] = (iGprDst < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_R)
                    | (iGprAddend < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x03;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprAddend & 7);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrAddSubReg(false /*fSub*/, iGprDst, iGprDst, iGprAddend);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits adding a 64-bit GPR to another, storing the result in the first.
 * @note The AMD64 version sets flags.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAddTwoGprs(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprAddend)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitAddTwoGprsEx(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst, iGprAddend);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitAddTwoGprsEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iGprAddend);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits adding a 64-bit GPR to another, storing the result in the first.
 * @note The AMD64 version sets flags.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitAddTwoGprs32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprAddend)
{
#if defined(RT_ARCH_AMD64)
    /* add Gv,Ev */
    if (iGprDst >= 8 || iGprAddend >= 8)
        pCodeBuf[off++] = (iGprDst    >= 8 ? X86_OP_REX_R : 0)
                        | (iGprAddend >= 8 ? X86_OP_REX_B : 0);
    pCodeBuf[off++] = 0x03;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprAddend & 7);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrAddSubReg(false /*fSub*/, iGprDst, iGprDst, iGprAddend, false /*f64Bit*/);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits adding a 64-bit GPR to another, storing the result in the first.
 * @note The AMD64 version sets flags.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAddTwoGprs32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprAddend)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitAddTwoGprs32Ex(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst, iGprAddend);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitAddTwoGprs32Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iGprAddend);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a 64-bit GPR additions with a 8-bit signed immediate.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAddGprImm8Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, int8_t iImm8)
{
#if defined(RT_ARCH_AMD64)
    /* add or inc */
    pCodeBuf[off++] = iGprDst < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_B;
    if (iImm8 != 1)
    {
        pCodeBuf[off++] = 0x83;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
        pCodeBuf[off++] = (uint8_t)iImm8;
    }
    else
    {
        pCodeBuf[off++] = 0xff;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
    }

#elif defined(RT_ARCH_ARM64)
    if (iImm8 >= 0)
        pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(iGprDst, iGprDst, (uint8_t)iImm8);
    else
        pCodeBuf[off++] = Armv8A64MkInstrSubUImm12(iGprDst, iGprDst, (uint8_t)-iImm8);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits a 64-bit GPR additions with a 8-bit signed immediate.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAddGprImm8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, int8_t iImm8)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitAddGprImm8Ex(iemNativeInstrBufEnsure(pReNative, off, 4), off, iGprDst, iImm8);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitAddGprImm8Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iImm8);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a 32-bit GPR additions with a 8-bit signed immediate.
 * @note Bits 32 thru 63 in the GPR will be zero after the operation.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitAddGpr32Imm8Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, int8_t iImm8)
{
#if defined(RT_ARCH_AMD64)
    /* add or inc */
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    if (iImm8 != 1)
    {
        pCodeBuf[off++] = 0x83;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
        pCodeBuf[off++] = (uint8_t)iImm8;
    }
    else
    {
        pCodeBuf[off++] = 0xff;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
    }

#elif defined(RT_ARCH_ARM64)
    if (iImm8 >= 0)
        pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(false /*fSub*/, iGprDst, iGprDst, (uint8_t)iImm8, false /*f64Bit*/);
    else
        pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, iGprDst, iGprDst, (uint8_t)-iImm8, false /*f64Bit*/);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits a 32-bit GPR additions with a 8-bit signed immediate.
 * @note Bits 32 thru 63 in the GPR will be zero after the operation.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAddGpr32Imm8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, int8_t iImm8)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitAddGpr32Imm8Ex(iemNativeInstrBufEnsure(pReNative, off, 4), off, iGprDst, iImm8);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitAddGpr32Imm8Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iImm8);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a 64-bit GPR additions with a 64-bit signed addend.
 *
 * @note Will assert / throw if @a iGprTmp is not specified when needed.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitAddGprImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, int64_t iAddend, uint8_t iGprTmp = UINT8_MAX)
{
#if defined(RT_ARCH_AMD64)
    if ((int8_t)iAddend == iAddend)
        return iemNativeEmitAddGprImm8Ex(pCodeBuf, off, iGprDst, (int8_t)iAddend);

    if ((int32_t)iAddend == iAddend)
    {
        /* add grp, imm32 */
        pCodeBuf[off++] = iGprDst < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_B;
        pCodeBuf[off++] = 0x81;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
        pCodeBuf[off++] = RT_BYTE1((uint32_t)iAddend);
        pCodeBuf[off++] = RT_BYTE2((uint32_t)iAddend);
        pCodeBuf[off++] = RT_BYTE3((uint32_t)iAddend);
        pCodeBuf[off++] = RT_BYTE4((uint32_t)iAddend);
    }
    else if (iGprTmp != UINT8_MAX)
    {
        off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, iGprTmp, iAddend);

        /* add dst, tmpreg  */
        pCodeBuf[off++] = (iGprDst < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_R)
                        | (iGprTmp < 8 ? 0 : X86_OP_REX_B);
        pCodeBuf[off++] = 0x03;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprTmp & 7);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#elif defined(RT_ARCH_ARM64)
    uint64_t const uAbsAddend = (uint64_t)RT_ABS(iAddend);
    if (uAbsAddend <= 0xffffffU)
    {
        bool const fSub = iAddend < 0;
        if (uAbsAddend > 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprDst, uAbsAddend >> 12, true /*f64Bit*/,
                                                          false /*fSetFlags*/, true /*fShift12*/);
        if (uAbsAddend & 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprDst, uAbsAddend & UINT32_C(0xfff));
    }
    else if (iGprTmp != UINT8_MAX)
    {
        off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, iGprTmp, iAddend);
        pCodeBuf[off++] = Armv8A64MkInstrAddReg(iGprDst, iGprDst, iGprTmp);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits a 64-bit GPR additions with a 64-bit signed addend.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAddGprImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, int64_t iAddend)
{
#if defined(RT_ARCH_AMD64)
    if (iAddend <= INT8_MAX && iAddend >= INT8_MIN)
        return iemNativeEmitAddGprImm8(pReNative, off, iGprDst, (int8_t)iAddend);

    if (iAddend <= INT32_MAX && iAddend >= INT32_MIN)
    {
        /* add grp, imm32 */
        uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
        pbCodeBuf[off++] = iGprDst < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_B;
        pbCodeBuf[off++] = 0x81;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
        pbCodeBuf[off++] = RT_BYTE1((uint32_t)iAddend);
        pbCodeBuf[off++] = RT_BYTE2((uint32_t)iAddend);
        pbCodeBuf[off++] = RT_BYTE3((uint32_t)iAddend);
        pbCodeBuf[off++] = RT_BYTE4((uint32_t)iAddend);
    }
    else
    {
        /* Best to use a temporary register to deal with this in the simplest way: */
        uint8_t iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, (uint64_t)iAddend);

        /* add dst, tmpreg  */
        uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3);
        pbCodeBuf[off++] = (iGprDst < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_R)
                         | (iTmpReg < 8 ? 0 : X86_OP_REX_B);
        pbCodeBuf[off++] = 0x03;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iTmpReg & 7);

        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }

#elif defined(RT_ARCH_ARM64)
    bool const     fSub       = iAddend < 0;
    uint64_t const uAbsAddend = (uint64_t)RT_ABS(iAddend);
    if (uAbsAddend <= 0xffffffU)
    {
        uint32_t * const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);
        if (uAbsAddend > 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprDst, uAbsAddend >> 12, true /*f64Bit*/,
                                                          false /*fSetFlags*/, true /*fShift12*/);
        if (uAbsAddend & 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprDst, uAbsAddend & UINT32_C(0xfff));
    }
    else
    {
        /* Use temporary register for the immediate. */
        uint8_t iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, uAbsAddend);

        /* add gprdst, gprdst, tmpreg */
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAddSubReg(fSub, iGprDst, iGprDst, iTmpReg);

        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }

#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a 32-bit GPR additions with a 32-bit signed immediate.
 * @note Bits 32 thru 63 in the GPR will be zero after the operation.
 * @note For ARM64 the iAddend value must be in the range 0x000000..0xffffff.
 *       The negative ranges are also allowed, making it behave like a
 *       subtraction.  If the constant does not conform, bad stuff will happen.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitAddGpr32ImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, int32_t iAddend, uint8_t iGprTmp = UINT8_MAX)
{
#if defined(RT_ARCH_AMD64)
    if (iAddend <= INT8_MAX && iAddend >= INT8_MIN)
        return iemNativeEmitAddGpr32Imm8Ex(pCodeBuf, off, iGprDst, (int8_t)iAddend);

    /* add grp, imm32 */
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    pCodeBuf[off++] = 0x81;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
    pCodeBuf[off++] = RT_BYTE1((uint32_t)iAddend);
    pCodeBuf[off++] = RT_BYTE2((uint32_t)iAddend);
    pCodeBuf[off++] = RT_BYTE3((uint32_t)iAddend);
    pCodeBuf[off++] = RT_BYTE4((uint32_t)iAddend);
    RT_NOREF(iGprTmp);

#elif defined(RT_ARCH_ARM64)
    uint32_t const uAbsAddend = (uint32_t)RT_ABS(iAddend);
    if (uAbsAddend <= 0xffffffU)
    {
        bool const fSub = iAddend < 0;
        if (uAbsAddend > 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprDst, uAbsAddend >> 12, false /*f64Bit*/,
                                                          false /*fSetFlags*/, true /*fShift12*/);
        if (uAbsAddend & 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprDst, uAbsAddend & 0xfff, false /*f64Bit*/);
    }
    else if (iGprTmp != UINT8_MAX)
    {
        off = iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off, iGprTmp, iAddend);
        pCodeBuf[off++] = Armv8A64MkInstrAddReg(iGprDst, iGprDst, iGprTmp, false /*f64Bit*/);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits a 32-bit GPR additions with a 32-bit signed immediate.
 * @note Bits 32 thru 63 in the GPR will be zero after the operation.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAddGpr32Imm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, int32_t iAddend)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitAddGpr32ImmEx(iemNativeInstrBufEnsure(pReNative, off, 7), off, iGprDst, iAddend);

#elif defined(RT_ARCH_ARM64)
    bool const     fSub       = iAddend < 0;
    uint32_t const uAbsAddend = (uint32_t)RT_ABS(iAddend);
    if (uAbsAddend <= 0xffffffU)
    {
        uint32_t * const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);
        if (uAbsAddend > 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprDst, uAbsAddend >> 12, false /*f64Bit*/,
                                                          false /*fSetFlags*/, true /*fShift12*/);
        if (uAbsAddend & 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprDst, uAbsAddend & 0xfff, false /*f64Bit*/);
    }
    else
    {
        /* Use temporary register for the immediate. */
        uint8_t iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, uAbsAddend);

        /* add gprdst, gprdst, tmpreg */
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAddSubReg(fSub, iGprDst, iGprDst, iTmpReg, false /*f64Bit*/);

        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }

#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a 16-bit GPR add with a signed immediate addend.
 *
 * This will optimize using INC/DEC/whatever and ARM64 will not set flags,
 * so not suitable as a base for conditional jumps.
 *
 * @note AMD64: Will only update the lower 16 bits of the register.
 * @note ARM64: Will update the entire register.
 * @sa   iemNativeEmitSubGpr16ImmEx
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitAddGpr16ImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, int16_t iAddend)
{
#ifdef RT_ARCH_AMD64
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    if (iAddend == 1)
    {
        /* inc r/m16 */
        pCodeBuf[off++] = 0xff;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
    }
    else if (iAddend == -1)
    {
        /* dec r/m16 */
        pCodeBuf[off++] = 0xff;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 1, iGprDst & 7);
    }
    else if ((int8_t)iAddend == iAddend)
    {
        /* add r/m16, imm8 */
        pCodeBuf[off++] = 0x83;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
        pCodeBuf[off++] = (uint8_t)iAddend;
    }
    else
    {
        /* add r/m16, imm16 */
        pCodeBuf[off++] = 0x81;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
        pCodeBuf[off++] = RT_BYTE1((uint16_t)iAddend);
        pCodeBuf[off++] = RT_BYTE2((uint16_t)iAddend);
    }

#elif defined(RT_ARCH_ARM64)
    bool const     fSub       = iAddend < 0;
    uint32_t const uAbsAddend = (uint32_t)RT_ABS(iAddend);
    if (uAbsAddend > 0xfffU)
        pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprDst, uAbsAddend >> 12, false /*f64Bit*/,
                                                      false /*fSetFlags*/, true /*fShift12*/);
    if (uAbsAddend & 0xfffU)
        pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprDst, uAbsAddend & 0xfff, false /*f64Bit*/);
    pCodeBuf[off++] = Armv8A64MkInstrAndImm(iGprDst, iGprDst, 15, 0, false /*f64Bit*/);

#else
# error "Port me"
#endif
    return off;
}



/**
 * Adds two 64-bit GPRs together, storing the result in a third register.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitGprEqGprPlusGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprAddend1, uint8_t iGprAddend2)
{
#ifdef RT_ARCH_AMD64
    if (iGprDst != iGprAddend1 && iGprDst != iGprAddend2)
    {
        /** @todo consider LEA */
        off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, iGprDst, iGprAddend1);
        off = iemNativeEmitAddTwoGprsEx(pCodeBuf, off, iGprDst, iGprAddend2);
    }
    else
        off = iemNativeEmitAddTwoGprsEx(pCodeBuf, off, iGprDst, iGprDst != iGprAddend1 ? iGprAddend1 : iGprAddend2);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrAddReg(iGprDst, iGprAddend1, iGprAddend2);

#else
# error "Port me!"
#endif
    return off;
}



/**
 * Adds two 32-bit GPRs together, storing the result in a third register.
 * @note Bits 32 thru 63 in @a iGprDst will be zero after the operation.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitGpr32EqGprPlusGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprAddend1, uint8_t iGprAddend2)
{
#ifdef RT_ARCH_AMD64
    if (iGprDst != iGprAddend1 && iGprDst != iGprAddend2)
    {
        /** @todo consider LEA */
        off = iemNativeEmitLoadGprFromGpr32Ex(pCodeBuf, off, iGprDst, iGprAddend1);
        off = iemNativeEmitAddTwoGprs32Ex(pCodeBuf, off, iGprDst, iGprAddend2);
    }
    else
        off = iemNativeEmitAddTwoGprs32Ex(pCodeBuf, off, iGprDst, iGprDst != iGprAddend1 ? iGprAddend1 : iGprAddend2);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrAddReg(iGprDst, iGprAddend1, iGprAddend2, false /*f64Bit*/);

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Adds a 64-bit GPR and a 64-bit unsigned constant, storing the result in a
 * third register.
 *
 * @note The ARM64 version does not work for non-trivial constants if the
 *       two registers are the same.  Will assert / throw exception.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitGprEqGprPlusImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprAddend, int64_t iImmAddend)
{
#ifdef RT_ARCH_AMD64
    /** @todo consider LEA */
    if ((int8_t)iImmAddend == iImmAddend)
    {
        off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, iGprDst, iGprAddend);
        off = iemNativeEmitAddGprImm8Ex(pCodeBuf, off, iGprDst, (int8_t)iImmAddend);
    }
    else
    {
        off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, iGprDst, iImmAddend);
        off = iemNativeEmitAddTwoGprsEx(pCodeBuf, off, iGprDst, iGprAddend);
    }

#elif defined(RT_ARCH_ARM64)
    bool     const fSub          = iImmAddend < 0;
    uint64_t const uAbsImmAddend = RT_ABS(iImmAddend);
    if (uAbsImmAddend <= 0xfffU)
        pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprAddend, uAbsImmAddend);
    else if (uAbsImmAddend <= 0xffffffU)
    {
        pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprAddend, uAbsImmAddend >> 12,
                                                      true /*f64Bit*/, false /*fSetFlags*/, true /*fShift12*/);
        if (uAbsImmAddend & 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprDst, uAbsImmAddend & UINT32_C(0xfff));
    }
    else if (iGprDst != iGprAddend)
    {
        off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, iGprDst, (uint64_t)iImmAddend);
        off = iemNativeEmitAddTwoGprsEx(pCodeBuf, off, iGprDst, iGprAddend);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Adds a 32-bit GPR and a 32-bit unsigned constant, storing the result in a
 * third register.
 *
 * @note Bits 32 thru 63 in @a iGprDst will be zero after the operation.
 *
 * @note The ARM64 version does not work for non-trivial constants if the
 *       two registers are the same.  Will assert / throw exception.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitGpr32EqGprPlusImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprAddend, int32_t iImmAddend)
{
#ifdef RT_ARCH_AMD64
    /** @todo consider LEA */
    if ((int8_t)iImmAddend == iImmAddend)
    {
        off = iemNativeEmitLoadGprFromGpr32Ex(pCodeBuf, off, iGprDst, iGprAddend);
        off = iemNativeEmitAddGpr32Imm8Ex(pCodeBuf, off, iGprDst, (int8_t)iImmAddend);
    }
    else
    {
        off = iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off, iGprDst, iImmAddend);
        off = iemNativeEmitAddTwoGprsEx(pCodeBuf, off, iGprDst, iGprAddend);
    }

#elif defined(RT_ARCH_ARM64)
    bool     const fSub          = iImmAddend < 0;
    uint32_t const uAbsImmAddend = RT_ABS(iImmAddend);
    if (uAbsImmAddend <= 0xfffU)
        pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprAddend, uAbsImmAddend, false /*f64Bit*/);
    else if (uAbsImmAddend <= 0xffffffU)
    {
        pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprAddend, uAbsImmAddend >> 12,
                                                      false /*f64Bit*/, false /*fSetFlags*/, true /*fShift12*/);
        if (uAbsImmAddend & 0xfffU)
            pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(fSub, iGprDst, iGprDst, uAbsImmAddend & 0xfff, false /*f64Bit*/);
    }
    else if (iGprDst != iGprAddend)
    {
        off = iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off, iGprDst, (uint32_t)iImmAddend);
        off = iemNativeEmitAddTwoGprs32Ex(pCodeBuf, off, iGprDst, iGprAddend);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "Port me!"
#endif
    return off;
}


/*********************************************************************************************************************************
*   Unary Operations                                                                                                             *
*********************************************************************************************************************************/

/**
 * Emits code for two complement negation of a 64-bit GPR.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitNegGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst)
{
#if defined(RT_ARCH_AMD64)
    /* neg Ev */
    pCodeBuf[off++] = iGprDst < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_B;
    pCodeBuf[off++] = 0xf7;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 3, iGprDst & 7);

#elif defined(RT_ARCH_ARM64)
    /* sub dst, xzr, dst */
    pCodeBuf[off++] = Armv8A64MkInstrNeg(iGprDst);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for two complement negation of a 64-bit GPR.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitNegGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitNegGprEx(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitNegGprEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for two complement negation of a 32-bit GPR.
 * @note bit 32 thru 63 are set to zero.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitNegGpr32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst)
{
#if defined(RT_ARCH_AMD64)
    /* neg Ev */
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    pCodeBuf[off++] = 0xf7;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 3, iGprDst & 7);

#elif defined(RT_ARCH_ARM64)
    /* sub dst, xzr, dst */
    pCodeBuf[off++] = Armv8A64MkInstrNeg(iGprDst, false /*f64Bit*/);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for two complement negation of a 32-bit GPR.
 * @note bit 32 thru 63 are set to zero.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitNegGpr32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitNegGpr32Ex(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitNegGpr32Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}



/*********************************************************************************************************************************
*   Bit Operations                                                                                                               *
*********************************************************************************************************************************/

/**
 * Emits code for clearing bits 16 thru 63 in the GPR.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitClear16UpGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst)
{
#if defined(RT_ARCH_AMD64)
    /* movzx Gv,Ew */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 4);
    if (iGprDst >= 8)
        pbCodeBuf[off++] = X86_OP_REX_B | X86_OP_REX_R;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xb7;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprDst & 7);

#elif defined(RT_ARCH_ARM64)
    uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
# if 1
    pu32CodeBuf[off++] = Armv8A64MkInstrUxth(iGprDst, iGprDst);
# else
    ///* This produces 0xffff; 0x4f: N=1 imms=001111 (immr=0) => size=64 length=15 */
    //pu32CodeBuf[off++] = Armv8A64MkInstrAndImm(iGprDst, iGprDst, 0x4f);
# endif
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for AND'ing two 64-bit GPRs.
 *
 * @note When fSetFlags=true, JZ/JNZ jumps can be used afterwards on both AMD64
 *       and ARM64 hosts.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitAndGprByGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc, bool fSetFlags = false)
{
#if defined(RT_ARCH_AMD64)
    /* and Gv, Ev */
    pCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x23;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);
    RT_NOREF(fSetFlags);

#elif defined(RT_ARCH_ARM64)
    if (!fSetFlags)
        pCodeBuf[off++] = Armv8A64MkInstrAnd(iGprDst, iGprDst, iGprSrc);
    else
        pCodeBuf[off++] = Armv8A64MkInstrAnds(iGprDst, iGprDst, iGprSrc);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for AND'ing two 64-bit GPRs.
 *
 * @note When fSetFlags=true, JZ/JNZ jumps can be used afterwards on both AMD64
 *       and ARM64 hosts.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAndGprByGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc, bool fSetFlags = false)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitAndGprByGprEx(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst, iGprSrc, fSetFlags);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitAndGprByGprEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iGprSrc, fSetFlags);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for AND'ing two 32-bit GPRs.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitAndGpr32ByGpr32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc, bool fSetFlags = false)
{
#if defined(RT_ARCH_AMD64)
    /* and Gv, Ev */
    if (iGprDst >= 8 || iGprSrc >= 8)
        pCodeBuf[off++] = (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x23;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);
    RT_NOREF(fSetFlags);

#elif defined(RT_ARCH_ARM64)
    if (!fSetFlags)
        pCodeBuf[off++] = Armv8A64MkInstrAnd(iGprDst, iGprDst, iGprSrc, false /*f64Bit*/);
    else
        pCodeBuf[off++] = Armv8A64MkInstrAnds(iGprDst, iGprDst, iGprSrc, false /*f64Bit*/);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for AND'ing two 32-bit GPRs.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAndGpr32ByGpr32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc, bool fSetFlags = false)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitAndGpr32ByGpr32Ex(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst, iGprSrc, fSetFlags);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitAndGpr32ByGpr32Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iGprSrc, fSetFlags);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for AND'ing a 64-bit GPRs with a constant.
 *
 * @note When fSetFlags=true, JZ/JNZ jumps can be used afterwards on both AMD64
 *       and ARM64 hosts.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAndGprByImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint64_t uImm, bool fSetFlags = false)
{
#if defined(RT_ARCH_AMD64)
    if ((int64_t)uImm == (int8_t)uImm)
    {
        /* and Ev, imm8 */
        uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 4);
        pbCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_B);
        pbCodeBuf[off++] = 0x83;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4, iGprDst & 7);
        pbCodeBuf[off++] = (uint8_t)uImm;
    }
    else if ((int64_t)uImm == (int32_t)uImm)
    {
        /* and Ev, imm32 */
        uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
        pbCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_B);
        pbCodeBuf[off++] = 0x81;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4, iGprDst & 7);
        pbCodeBuf[off++] = RT_BYTE1(uImm);
        pbCodeBuf[off++] = RT_BYTE2(uImm);
        pbCodeBuf[off++] = RT_BYTE3(uImm);
        pbCodeBuf[off++] = RT_BYTE4(uImm);
    }
    else
    {
        /* Use temporary register for the 64-bit immediate. */
        uint8_t iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, uImm);
        off = iemNativeEmitAndGprByGpr(pReNative, off, iGprDst, iTmpReg);
        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }
    RT_NOREF(fSetFlags);

#elif defined(RT_ARCH_ARM64)
    uint32_t uImmR     = 0;
    uint32_t uImmNandS = 0;
    if (Armv8A64ConvertMask64ToImmRImmS(uImm, &uImmNandS, &uImmR))
    {
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        if (!fSetFlags)
            pu32CodeBuf[off++] = Armv8A64MkInstrAndImm(iGprDst, iGprDst, uImmNandS, uImmR);
        else
            pu32CodeBuf[off++] = Armv8A64MkInstrAndsImm(iGprDst, iGprDst, uImmNandS, uImmR);
    }
    else
    {
        /* Use temporary register for the 64-bit immediate. */
        uint8_t iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, uImm);
        off = iemNativeEmitAndGprByGpr(pReNative, off, iGprDst, iTmpReg, fSetFlags);
        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }

#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for AND'ing an 32-bit GPRs with a constant.
 * @note Bits 32 thru 63 in the destination will be zero after the operation.
 * @note For ARM64 this only supports @a uImm values that can be expressed using
 *       the two 6-bit immediates of the AND/ANDS instructions.  The caller must
 *       make sure this is possible!
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitAndGpr32ByImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint32_t uImm, bool fSetFlags = false)
{
#if defined(RT_ARCH_AMD64)
    /* and Ev, imm */
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    if ((int32_t)uImm == (int8_t)uImm)
    {
        pCodeBuf[off++] = 0x83;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4, iGprDst & 7);
        pCodeBuf[off++] = (uint8_t)uImm;
    }
    else
    {
        pCodeBuf[off++] = 0x81;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4, iGprDst & 7);
        pCodeBuf[off++] = RT_BYTE1(uImm);
        pCodeBuf[off++] = RT_BYTE2(uImm);
        pCodeBuf[off++] = RT_BYTE3(uImm);
        pCodeBuf[off++] = RT_BYTE4(uImm);
    }
    RT_NOREF(fSetFlags);

#elif defined(RT_ARCH_ARM64)
    uint32_t uImmR     = 0;
    uint32_t uImmNandS = 0;
    if (Armv8A64ConvertMask32ToImmRImmS(uImm, &uImmNandS, &uImmR))
    {
        if (!fSetFlags)
            pCodeBuf[off++] = Armv8A64MkInstrAndImm(iGprDst, iGprDst, uImmNandS, uImmR, false /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAndsImm(iGprDst, iGprDst, uImmNandS, uImmR, false /*f64Bit*/);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for AND'ing an 32-bit GPRs with a constant.
 *
 * @note Bits 32 thru 63 in the destination will be zero after the operation.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAndGpr32ByImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint32_t uImm, bool fSetFlags = false)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitAndGpr32ByImmEx(iemNativeInstrBufEnsure(pReNative, off, 7), off, iGprDst, uImm, fSetFlags);

#elif defined(RT_ARCH_ARM64)
    uint32_t uImmR     = 0;
    uint32_t uImmNandS = 0;
    if (Armv8A64ConvertMask32ToImmRImmS(uImm, &uImmNandS, &uImmR))
    {
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        if (!fSetFlags)
            pu32CodeBuf[off++] = Armv8A64MkInstrAndImm(iGprDst, iGprDst, uImmNandS, uImmR, false /*f64Bit*/);
        else
            pu32CodeBuf[off++] = Armv8A64MkInstrAndsImm(iGprDst, iGprDst, uImmNandS, uImmR, false /*f64Bit*/);
    }
    else
    {
        /* Use temporary register for the 64-bit immediate. */
        uint8_t iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, uImm);
        off = iemNativeEmitAndGpr32ByGpr32(pReNative, off, iGprDst, iTmpReg, fSetFlags);
        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }

#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for AND'ing an 64-bit GPRs with a constant.
 *
 * @note For ARM64 any complicated immediates w/o a AND/ANDS compatible
 *       encoding will assert / throw exception if @a iGprDst and @a iGprSrc are
 *       the same.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitGprEqGprAndImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc, uint64_t uImm,
                              bool fSetFlags = false)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, iGprDst, uImm);
    off = iemNativeEmitAndGprByGprEx(pCodeBuf, off, iGprDst, iGprSrc);
    RT_NOREF(fSetFlags);

#elif defined(RT_ARCH_ARM64)
    uint32_t uImmR     = 0;
    uint32_t uImmNandS = 0;
    if (Armv8A64ConvertMask64ToImmRImmS(uImm, &uImmNandS, &uImmR))
    {
        if (!fSetFlags)
            pCodeBuf[off++] = Armv8A64MkInstrAndImm(iGprDst, iGprSrc, uImmNandS, uImmR);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAndsImm(iGprDst, iGprSrc, uImmNandS, uImmR);
    }
    else if (iGprDst != iGprSrc)
    {
        off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, iGprDst, uImm);
        off = iemNativeEmitAndGprByGprEx(pCodeBuf, off, iGprDst, iGprSrc, fSetFlags);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "Port me"
#endif
    return off;
}

/**
 * Emits code for AND'ing an 32-bit GPRs with a constant.
 *
 * @note For ARM64 any complicated immediates w/o a AND/ANDS compatible
 *       encoding will assert / throw exception if @a iGprDst and @a iGprSrc are
 *       the same.
 *
 * @note Bits 32 thru 63 in the destination will be zero after the operation.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitGpr32EqGprAndImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc, uint32_t uImm,
                                bool fSetFlags = false)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off, iGprDst, uImm);
    off = iemNativeEmitAndGpr32ByGpr32Ex(pCodeBuf, off, iGprDst, iGprSrc);
    RT_NOREF(fSetFlags);

#elif defined(RT_ARCH_ARM64)
    uint32_t uImmR     = 0;
    uint32_t uImmNandS = 0;
    if (Armv8A64ConvertMask32ToImmRImmS(uImm, &uImmNandS, &uImmR))
    {
        if (!fSetFlags)
            pCodeBuf[off++] = Armv8A64MkInstrAndImm(iGprDst, iGprSrc, uImmNandS, uImmR, false /*f64Bit*/);
        else
            pCodeBuf[off++] = Armv8A64MkInstrAndsImm(iGprDst, iGprSrc, uImmNandS, uImmR, false /*f64Bit*/);
    }
    else if (iGprDst != iGprSrc)
    {
        /* If a value greater or equal than 64K isn't more than 16 bits wide,
           we can use shifting to save an instruction.  We prefer the builtin ctz
           here to our own, since the compiler can process uImm at compile time
           if it is a constant value (which is often the case).  This is useful
           for the TLB looup code. */
        if (uImm > 0xffffU)
        {
#  if defined(__GNUC__)
            unsigned cTrailingZeros = __builtin_ctz(uImm);
#  else
            unsigned cTrailingZeros = ASMBitFirstSetU32(uImm) - 1;
#  endif
            if ((uImm >> cTrailingZeros) <= 0xffffU)
            {
                pCodeBuf[off++] = Armv8A64MkInstrMovZ(iGprDst, uImm >> cTrailingZeros);
                pCodeBuf[off++] = Armv8A64MkInstrAnd(iGprDst, iGprSrc,
                                                     iGprDst, true /*f64Bit*/, cTrailingZeros, kArmv8A64InstrShift_Lsl);
                return off;
            }
        }
        off = iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off, iGprDst, uImm);
        off = iemNativeEmitAndGpr32ByGpr32Ex(pCodeBuf, off, iGprDst, iGprSrc, fSetFlags);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for OR'ing two 64-bit GPRs.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitOrGprByGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#if defined(RT_ARCH_AMD64)
    /* or Gv, Ev */
    pCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0b;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrOrr(iGprDst, iGprDst, iGprSrc);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for OR'ing two 64-bit GPRs.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitOrGprByGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitOrGprByGprEx(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst, iGprSrc);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitOrGprByGprEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iGprSrc);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for OR'ing two 32-bit GPRs.
 * @note Bits 63:32 of the destination GPR will be cleared.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitOrGpr32ByGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#if defined(RT_ARCH_AMD64)
    /* or Gv, Ev */
    if (iGprDst >= 8 || iGprSrc >= 8)
        pCodeBuf[off++] = (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0b;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrOrr(iGprDst, iGprDst, iGprSrc, false /*f64Bit*/);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for OR'ing two 32-bit GPRs.
 * @note Bits 63:32 of the destination GPR will be cleared.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitOrGpr32ByGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitOrGpr32ByGprEx(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst, iGprSrc);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitOrGpr32ByGprEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iGprSrc);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for OR'ing a 64-bit GPRs with a constant.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitOrGprByImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint64_t uImm)
{
#if defined(RT_ARCH_AMD64)
    if ((int64_t)uImm == (int8_t)uImm)
    {
        /* or Ev, imm8 */
        uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 4);
        pbCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_B);
        pbCodeBuf[off++] = 0x83;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 1, iGprDst & 7);
        pbCodeBuf[off++] = (uint8_t)uImm;
    }
    else if ((int64_t)uImm == (int32_t)uImm)
    {
        /* or Ev, imm32 */
        uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
        pbCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_B);
        pbCodeBuf[off++] = 0x81;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 1, iGprDst & 7);
        pbCodeBuf[off++] = RT_BYTE1(uImm);
        pbCodeBuf[off++] = RT_BYTE2(uImm);
        pbCodeBuf[off++] = RT_BYTE3(uImm);
        pbCodeBuf[off++] = RT_BYTE4(uImm);
    }
    else
    {
        /* Use temporary register for the 64-bit immediate. */
        uint8_t iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, uImm);
        off = iemNativeEmitOrGprByGprEx(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst, iTmpReg);
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }

#elif defined(RT_ARCH_ARM64)
    uint32_t uImmR     = 0;
    uint32_t uImmNandS = 0;
    if (Armv8A64ConvertMask64ToImmRImmS(uImm, &uImmNandS, &uImmR))
    {
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrOrrImm(iGprDst, iGprDst, uImmNandS, uImmR);
    }
    else
    {
        /* Use temporary register for the 64-bit immediate. */
        uint8_t iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, uImm);
        off = iemNativeEmitOrGprByGprEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iTmpReg);
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }

#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for OR'ing an 32-bit GPRs with a constant.
 * @note Bits 32 thru 63 in the destination will be zero after the operation.
 * @note For ARM64 this only supports @a uImm values that can be expressed using
 *       the two 6-bit immediates of the OR instructions.  The caller must make
 *       sure this is possible!
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitOrGpr32ByImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint32_t uImm)
{
#if defined(RT_ARCH_AMD64)
    /* or Ev, imm */
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    if ((int32_t)uImm == (int8_t)uImm)
    {
        pCodeBuf[off++] = 0x83;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 1, iGprDst & 7);
        pCodeBuf[off++] = (uint8_t)uImm;
    }
    else
    {
        pCodeBuf[off++] = 0x81;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 1, iGprDst & 7);
        pCodeBuf[off++] = RT_BYTE1(uImm);
        pCodeBuf[off++] = RT_BYTE2(uImm);
        pCodeBuf[off++] = RT_BYTE3(uImm);
        pCodeBuf[off++] = RT_BYTE4(uImm);
    }

#elif defined(RT_ARCH_ARM64)
    uint32_t uImmR     = 0;
    uint32_t uImmNandS = 0;
    if (Armv8A64ConvertMask32ToImmRImmS(uImm, &uImmNandS, &uImmR))
        pCodeBuf[off++] = Armv8A64MkInstrOrrImm(iGprDst, iGprDst, uImmNandS, uImmR, false /*f64Bit*/);
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for OR'ing an 32-bit GPRs with a constant.
 *
 * @note Bits 32 thru 63 in the destination will be zero after the operation.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitOrGpr32ByImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint32_t uImm)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitOrGpr32ByImmEx(iemNativeInstrBufEnsure(pReNative, off, 7), off, iGprDst, uImm);

#elif defined(RT_ARCH_ARM64)
    uint32_t uImmR     = 0;
    uint32_t uImmNandS = 0;
    if (Armv8A64ConvertMask32ToImmRImmS(uImm, &uImmNandS, &uImmR))
    {
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrOrrImm(iGprDst, iGprDst, uImmNandS, uImmR, false /*f64Bit*/);
    }
    else
    {
        /* Use temporary register for the 64-bit immediate. */
        uint8_t iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, uImm);
        off = iemNativeEmitOrGpr32ByGpr(pReNative, off, iGprDst, iTmpReg);
        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }

#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}



/**
 * ORs two 64-bit GPRs together, storing the result in a third register.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitGprEqGprOrGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc1, uint8_t iGprSrc2)
{
#ifdef RT_ARCH_AMD64
    if (iGprDst != iGprSrc1 && iGprDst != iGprSrc2)
    {
        /** @todo consider LEA */
        off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, iGprDst, iGprSrc1);
        off = iemNativeEmitOrGprByGprEx(pCodeBuf, off, iGprDst, iGprSrc2);
    }
    else
        off = iemNativeEmitOrGprByGprEx(pCodeBuf, off, iGprDst, iGprDst != iGprSrc1 ? iGprSrc1 : iGprSrc2);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrOrr(iGprDst, iGprSrc1, iGprSrc2);

#else
# error "Port me!"
#endif
    return off;
}



/**
 * Ors two 32-bit GPRs together, storing the result in a third register.
 * @note Bits 32 thru 63 in @a iGprDst will be zero after the operation.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitGpr32EqGprOrGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc1, uint8_t iGprSrc2)
{
#ifdef RT_ARCH_AMD64
    if (iGprDst != iGprSrc1 && iGprDst != iGprSrc2)
    {
        off = iemNativeEmitLoadGprFromGpr32Ex(pCodeBuf, off, iGprDst, iGprSrc1);
        off = iemNativeEmitOrGpr32ByGprEx(pCodeBuf, off, iGprDst, iGprSrc2);
    }
    else
        off = iemNativeEmitOrGpr32ByGprEx(pCodeBuf, off, iGprDst, iGprDst != iGprSrc1 ? iGprSrc1 : iGprSrc2);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrOrr(iGprDst, iGprSrc1, iGprSrc2, false /*f64Bit*/);

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits code for XOR'ing two 64-bit GPRs.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitXorGprByGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#if defined(RT_ARCH_AMD64)
    /* and Gv, Ev */
    pCodeBuf[off++] = X86_OP_REX_W | (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x33;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrEor(iGprDst, iGprDst, iGprSrc);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for XOR'ing two 64-bit GPRs.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitXorGprByGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitXorGprByGprEx(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst, iGprSrc);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitXorGprByGprEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iGprSrc);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for XOR'ing two 32-bit GPRs.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitXorGpr32ByGpr32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#if defined(RT_ARCH_AMD64)
    /* and Gv, Ev */
    if (iGprDst >= 8 || iGprSrc >= 8)
        pCodeBuf[off++] = (iGprDst < 8 ? 0 : X86_OP_REX_R) | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x33;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iGprSrc & 7);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrEor(iGprDst, iGprDst, iGprSrc, false /*f64Bit*/);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for XOR'ing two 32-bit GPRs.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitXorGpr32ByGpr32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitXorGpr32ByGpr32Ex(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprDst, iGprSrc);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitXorGpr32ByGpr32Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iGprSrc);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for XOR'ing an 32-bit GPRs with a constant.
 * @note Bits 32 thru 63 in the destination will be zero after the operation.
 * @note For ARM64 this only supports @a uImm values that can be expressed using
 *       the two 6-bit immediates of the EOR instructions.  The caller must make
 *       sure this is possible!
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitXorGpr32ByImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint32_t uImm)
{
#if defined(RT_ARCH_AMD64)
    /* xor Ev, imm */
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    if ((int32_t)uImm == (int8_t)uImm)
    {
        pCodeBuf[off++] = 0x83;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 6, iGprDst & 7);
        pCodeBuf[off++] = (uint8_t)uImm;
    }
    else
    {
        pCodeBuf[off++] = 0x81;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 6, iGprDst & 7);
        pCodeBuf[off++] = RT_BYTE1(uImm);
        pCodeBuf[off++] = RT_BYTE2(uImm);
        pCodeBuf[off++] = RT_BYTE3(uImm);
        pCodeBuf[off++] = RT_BYTE4(uImm);
    }

#elif defined(RT_ARCH_ARM64)
    uint32_t uImmR     = 0;
    uint32_t uImmNandS = 0;
    if (Armv8A64ConvertMask32ToImmRImmS(uImm, &uImmNandS, &uImmR))
        pCodeBuf[off++] = Armv8A64MkInstrEorImm(iGprDst, iGprDst, uImmNandS, uImmR, false /*f64Bit*/);
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for XOR'ing two 32-bit GPRs.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitXorGpr32ByImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint32_t uImm)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitXorGpr32ByImmEx(iemNativeInstrBufEnsure(pReNative, off, 7), off, iGprDst, uImm);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitXorGpr32ByImmEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, uImm);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/*********************************************************************************************************************************
*   Shifting                                                                                                                     *
*********************************************************************************************************************************/

/**
 * Emits code for shifting a GPR a fixed number of bits to the left.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitShiftGprLeftEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t cShift)
{
    Assert(cShift > 0 && cShift < 64);

#if defined(RT_ARCH_AMD64)
    /* shl dst, cShift */
    pCodeBuf[off++] = iGprDst < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_B;
    if (cShift != 1)
    {
        pCodeBuf[off++] = 0xc1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4, iGprDst & 7);
        pCodeBuf[off++] = cShift;
    }
    else
    {
        pCodeBuf[off++] = 0xd1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4, iGprDst & 7);
    }

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrLslImm(iGprDst, iGprDst, cShift);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for shifting a GPR a fixed number of bits to the left.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitShiftGprLeft(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t cShift)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitShiftGprLeftEx(iemNativeInstrBufEnsure(pReNative, off, 4), off, iGprDst, cShift);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitShiftGprLeftEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, cShift);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for shifting a 32-bit GPR a fixed number of bits to the left.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitShiftGpr32LeftEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t cShift)
{
    Assert(cShift > 0 && cShift < 32);

#if defined(RT_ARCH_AMD64)
    /* shl dst, cShift */
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    if (cShift != 1)
    {
        pCodeBuf[off++] = 0xc1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4, iGprDst & 7);
        pCodeBuf[off++] = cShift;
    }
    else
    {
        pCodeBuf[off++] = 0xd1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4, iGprDst & 7);
    }

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrLslImm(iGprDst, iGprDst, cShift, false /*64Bit*/);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for shifting a 32-bit GPR a fixed number of bits to the left.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitShiftGpr32Left(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t cShift)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitShiftGpr32LeftEx(iemNativeInstrBufEnsure(pReNative, off, 4), off, iGprDst, cShift);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitShiftGpr32LeftEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, cShift);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for (unsigned) shifting a GPR a fixed number of bits to the right.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitShiftGprRightEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t cShift)
{
    Assert(cShift > 0 && cShift < 64);

#if defined(RT_ARCH_AMD64)
    /* shr dst, cShift */
    pCodeBuf[off++] = iGprDst < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_B;
    if (cShift != 1)
    {
        pCodeBuf[off++] = 0xc1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 5, iGprDst & 7);
        pCodeBuf[off++] = cShift;
    }
    else
    {
        pCodeBuf[off++] = 0xd1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 5, iGprDst & 7);
    }

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrLsrImm(iGprDst, iGprDst, cShift);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for (unsigned) shifting a GPR a fixed number of bits to the right.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitShiftGprRight(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t cShift)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitShiftGprRightEx(iemNativeInstrBufEnsure(pReNative, off, 4), off, iGprDst, cShift);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitShiftGprRightEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, cShift);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for (unsigned) shifting a 32-bit GPR a fixed number of bits to the
 * right.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitShiftGpr32RightEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t cShift)
{
    Assert(cShift > 0 && cShift < 32);

#if defined(RT_ARCH_AMD64)
    /* shr dst, cShift */
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    if (cShift != 1)
    {
        pCodeBuf[off++] = 0xc1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 5, iGprDst & 7);
        pCodeBuf[off++] = cShift;
    }
    else
    {
        pCodeBuf[off++] = 0xd1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 5, iGprDst & 7);
    }

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrLsrImm(iGprDst, iGprDst, cShift, false /*64Bit*/);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for (unsigned) shifting a 32-bit GPR a fixed number of bits to the
 * right.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitShiftGpr32Right(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t cShift)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitShiftGpr32RightEx(iemNativeInstrBufEnsure(pReNative, off, 4), off, iGprDst, cShift);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitShiftGpr32RightEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, cShift);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for (unsigned) shifting a 32-bit GPR a fixed number of bits to the
 * right and assigning it to a different GPR.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitGpr32EqGprShiftRightImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc, uint8_t cShift)
{
    Assert(cShift > 0); Assert(cShift < 32);
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitLoadGprFromGpr32Ex(pCodeBuf, off, iGprDst, iGprSrc);
    off = iemNativeEmitShiftGpr32RightEx(pCodeBuf, off, iGprDst, cShift);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrLsrImm(iGprDst, iGprSrc, cShift, false /*64Bit*/);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for (signed) shifting a GPR a fixed number of bits to the right.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitArithShiftGprRightEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t cShift)
{
    Assert(cShift > 0 && cShift < 64);

#if defined(RT_ARCH_AMD64)
    /* sar dst, cShift */
    pCodeBuf[off++] = iGprDst < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_B;
    if (cShift != 1)
    {
        pCodeBuf[off++] = 0xc1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 7, iGprDst & 7);
        pCodeBuf[off++] = cShift;
    }
    else
    {
        pCodeBuf[off++] = 0xd1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 7, iGprDst & 7);
    }

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrAsrImm(iGprDst, iGprDst, cShift);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for (signed) shifting a GPR a fixed number of bits to the right.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitArithShiftGprRight(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t cShift)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitArithShiftGprRightEx(iemNativeInstrBufEnsure(pReNative, off, 4), off, iGprDst, cShift);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitArithShiftGprRightEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, cShift);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for (signed) shifting a 32-bit GPR a fixed number of bits to the right.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitArithShiftGpr32RightEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t cShift)
{
    Assert(cShift > 0 && cShift < 64);

#if defined(RT_ARCH_AMD64)
    /* sar dst, cShift */
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    if (cShift != 1)
    {
        pCodeBuf[off++] = 0xc1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 7, iGprDst & 7);
        pCodeBuf[off++] = cShift;
    }
    else
    {
        pCodeBuf[off++] = 0xd1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 7, iGprDst & 7);
    }

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrAsrImm(iGprDst, iGprDst, cShift, false /*f64Bit*/);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code for (signed) shifting a GPR a fixed number of bits to the right.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitArithShiftGpr32Right(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t cShift)
{
#if defined(RT_ARCH_AMD64)
    off = iemNativeEmitArithShiftGpr32RightEx(iemNativeInstrBufEnsure(pReNative, off, 4), off, iGprDst, cShift);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitArithShiftGpr32RightEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, cShift);
#else
# error "Port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for rotating a GPR a fixed number of bits to the left.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitRotateGprLeftEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t cShift)
{
    Assert(cShift > 0 && cShift < 64);

#if defined(RT_ARCH_AMD64)
    /* rol dst, cShift */
    pCodeBuf[off++] = iGprDst < 8 ? X86_OP_REX_W : X86_OP_REX_W | X86_OP_REX_B;
    if (cShift != 1)
    {
        pCodeBuf[off++] = 0xc1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
        pCodeBuf[off++] = cShift;
    }
    else
    {
        pCodeBuf[off++] = 0xd1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprDst & 7);
    }

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrRorImm(iGprDst, iGprDst, cShift);

#else
# error "Port me"
#endif
    return off;
}


#if defined(RT_ARCH_AMD64)
/**
 * Emits code for rotating a 32-bit GPR a fixed number of bits to the left via carry.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitAmd64RotateGpr32LeftViaCarryEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t cShift)
{
    Assert(cShift > 0 && cShift < 32);

    /* rcl dst, cShift */
    if (iGprDst >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    if (cShift != 1)
    {
        pCodeBuf[off++] = 0xc1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 2, iGprDst & 7);
        pCodeBuf[off++] = cShift;
    }
    else
    {
        pCodeBuf[off++] = 0xd1;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 2, iGprDst & 7);
    }

    return off;
}
#endif /* RT_ARCH_AMD64 */



/**
 * Emits code for reversing the byte order for a 16-bit value in a 32-bit GPR.
 * @note Bits 63:32 of the destination GPR will be cleared.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitBswapGpr16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr)
{
#if defined(RT_ARCH_AMD64)
    /*
     * There is no bswap r16 on x86 (the encoding exists but does not work).
     * So just use a rol (gcc -O2 is doing that).
     *
     *    rol r16, 0x8
     */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5);
    pbCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iGpr >= 8)
        pbCodeBuf[off++] = X86_OP_REX_B;
    pbCodeBuf[off++] = 0xc1;
    pbCodeBuf[off++] = 0xc0 | (iGpr & 7);
    pbCodeBuf[off++] = 0x08;
#elif defined(RT_ARCH_ARM64)
    uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);

    pu32CodeBuf[off++] = Armv8A64MkInstrRev16(iGpr, iGpr, false /*f64Bit*/);
#else
# error "Port me"
#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for reversing the byte order in a 32-bit GPR.
 * @note Bits 63:32 of the destination GPR will be cleared.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitBswapGpr32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr)
{
#if defined(RT_ARCH_AMD64)
    /* bswap r32 */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3);

    if (iGpr >= 8)
        pbCodeBuf[off++] = X86_OP_REX_B;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xc8 | (iGpr & 7);
#elif defined(RT_ARCH_ARM64)
    uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);

    pu32CodeBuf[off++] = Armv8A64MkInstrRev(iGpr, iGpr, false /*f64Bit*/);
#else
# error "Port me"
#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code for reversing the byte order in a 64-bit GPR.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitBswapGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGpr)
{
#if defined(RT_ARCH_AMD64)
    /* bswap r64 */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3);

    if (iGpr >= 8)
        pbCodeBuf[off++] = X86_OP_REX_W | X86_OP_REX_B;
    else
        pbCodeBuf[off++] = X86_OP_REX_W;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xc8 | (iGpr & 7);
#elif defined(RT_ARCH_ARM64)
    uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);

    pu32CodeBuf[off++] = Armv8A64MkInstrRev(iGpr, iGpr, true /*f64Bit*/);
#else
# error "Port me"
#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/*********************************************************************************************************************************
*   Bitfield manipulation                                                                                                        *
*********************************************************************************************************************************/

/**
 * Emits code for clearing.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitBitClearInGpr32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t const iGpr, uint8_t iBit)
{
    Assert(iBit < 32);

#if defined(RT_ARCH_AMD64)
    /* btr r32, imm8 */
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5);

    if (iGpr >= 8)
        pbCodeBuf[off++] = X86_OP_REX_B;
    pbCodeBuf[off++] = 0x0f;
    pbCodeBuf[off++] = 0xba;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 6, iGpr & 7);
    pbCodeBuf[off++] = iBit;
#elif defined(RT_ARCH_ARM64)
    uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);

    pu32CodeBuf[off++] = Armv8A64MkInstrBfc(iGpr, iBit /*offFirstBit*/, 1 /*cBits*/, true /*f64Bit*/);
#else
# error "Port me"
#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/*********************************************************************************************************************************
*   Compare and Testing                                                                                                          *
*********************************************************************************************************************************/


#ifdef RT_ARCH_ARM64
/**
 * Emits an ARM64 compare instruction.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCmpArm64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprLeft, uint8_t iGprRight,
                      bool f64Bit = true, uint32_t cShift = 0, ARMV8A64INSTRSHIFT enmShift = kArmv8A64InstrShift_Lsr)
{
    uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = Armv8A64MkInstrAddSubReg(true /*fSub*/, ARMV8_A64_REG_XZR /*iRegResult*/, iGprLeft, iGprRight,
                                                  f64Bit, true /*fSetFlags*/, cShift, enmShift);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}
#endif


/**
 * Emits a compare of two 64-bit GPRs, settings status flags/whatever for use
 * with conditional instruction.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitCmpGprWithGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprLeft, uint8_t iGprRight)
{
#ifdef RT_ARCH_AMD64
    /* cmp Gv, Ev */
    pCodeBuf[off++] = X86_OP_REX_W | (iGprLeft >= 8 ? X86_OP_REX_R : 0) | (iGprRight >= 8 ? X86_OP_REX_B : 0);
    pCodeBuf[off++] = 0x3b;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprLeft & 7, iGprRight & 7);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrCmpReg(iGprLeft, iGprRight);

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits a compare of two 64-bit GPRs, settings status flags/whatever for use
 * with conditional instruction.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCmpGprWithGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprLeft, uint8_t iGprRight)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitCmpGprWithGprEx(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprLeft, iGprRight);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitCmpGprWithGprEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprLeft, iGprRight);
#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a compare of two 32-bit GPRs, settings status flags/whatever for use
 * with conditional instruction.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitCmpGpr32WithGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprLeft, uint8_t iGprRight)
{
#ifdef RT_ARCH_AMD64
    /* cmp Gv, Ev */
    if (iGprLeft >= 8 || iGprRight >= 8)
        pCodeBuf[off++] = (iGprLeft >= 8 ? X86_OP_REX_R : 0) | (iGprRight >= 8 ? X86_OP_REX_B : 0);
    pCodeBuf[off++] = 0x3b;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprLeft & 7, iGprRight & 7);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrCmpReg(iGprLeft, iGprRight, false /*f64Bit*/);

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits a compare of two 32-bit GPRs, settings status flags/whatever for use
 * with conditional instruction.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCmpGpr32WithGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprLeft, uint8_t iGprRight)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitCmpGpr32WithGprEx(iemNativeInstrBufEnsure(pReNative, off, 3), off, iGprLeft, iGprRight);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitCmpGpr32WithGprEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprLeft, iGprRight);
#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a compare of a 64-bit GPR with a constant value, settings status
 * flags/whatever for use with conditional instruction.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCmpGprWithImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprLeft,
                             uint64_t uImm, uint8_t idxTmpReg = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    if ((int8_t)uImm == (int64_t)uImm)
    {
        /* cmp Ev, Ib */
        pCodeBuf[off++] = X86_OP_REX_W | (iGprLeft >= 8 ? X86_OP_REX_B : 0);
        pCodeBuf[off++] = 0x83;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 7, iGprLeft & 7);
        pCodeBuf[off++] = (uint8_t)uImm;
        return off;
    }
    if ((int32_t)uImm == (int64_t)uImm)
    {
        /* cmp Ev, imm */
        pCodeBuf[off++] = X86_OP_REX_W | (iGprLeft >= 8 ? X86_OP_REX_B : 0);
        pCodeBuf[off++] = 0x81;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 7, iGprLeft & 7);
        pCodeBuf[off++] = RT_BYTE1(uImm);
        pCodeBuf[off++] = RT_BYTE2(uImm);
        pCodeBuf[off++] = RT_BYTE3(uImm);
        pCodeBuf[off++] = RT_BYTE4(uImm);
        return off;
    }

#elif defined(RT_ARCH_ARM64)
    if (uImm < _4K)
    {
        pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, ARMV8_A64_REG_XZR, iGprLeft, (uint32_t)uImm,
                                                      true /*64Bit*/, true /*fSetFlags*/);
        return off;
    }
    if ((uImm & ~(uint64_t)0xfff000) == 0)
    {
        pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, ARMV8_A64_REG_XZR, iGprLeft, (uint32_t)uImm >> 12,
                                                      true /*64Bit*/, true /*fSetFlags*/, true /*fShift12*/);
        return off;
    }

#else
# error "Port me!"
#endif

    if (idxTmpReg != UINT8_MAX)
    {
        /* Use temporary register for the immediate. */
        off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, idxTmpReg, uImm);
        off = iemNativeEmitCmpGprWithGprEx(pCodeBuf, off, iGprLeft, idxTmpReg);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

    return off;
}


/**
 * Emits a compare of a 64-bit GPR with a constant value, settings status
 * flags/whatever for use with conditional instruction.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCmpGprWithImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprLeft, uint64_t uImm)
{
#ifdef RT_ARCH_AMD64
    if ((int8_t)uImm == (int64_t)uImm)
    {
        /* cmp Ev, Ib */
        uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 4);
        pbCodeBuf[off++] = X86_OP_REX_W | (iGprLeft >= 8 ? X86_OP_REX_B : 0);
        pbCodeBuf[off++] = 0x83;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 7, iGprLeft & 7);
        pbCodeBuf[off++] = (uint8_t)uImm;
    }
    else if ((int32_t)uImm == (int64_t)uImm)
    {
        /* cmp Ev, imm */
        uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
        pbCodeBuf[off++] = X86_OP_REX_W | (iGprLeft >= 8 ? X86_OP_REX_B : 0);
        pbCodeBuf[off++] = 0x81;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 7, iGprLeft & 7);
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
        pbCodeBuf[off++] = RT_BYTE1(uImm);
        pbCodeBuf[off++] = RT_BYTE2(uImm);
        pbCodeBuf[off++] = RT_BYTE3(uImm);
        pbCodeBuf[off++] = RT_BYTE4(uImm);
    }
    else
    {
        /* Use temporary register for the immediate. */
        uint8_t const iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, uImm);
        off = iemNativeEmitCmpGprWithGpr(pReNative, off, iGprLeft, iTmpReg);
        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }

#elif defined(RT_ARCH_ARM64)
    /** @todo guess there are clevere things we can do here...   */
    if (uImm < _4K)
    {
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, ARMV8_A64_REG_XZR, iGprLeft, (uint32_t)uImm,
                                                         true /*64Bit*/, true /*fSetFlags*/);
    }
    else if ((uImm & ~(uint64_t)0xfff000) == 0)
    {
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, ARMV8_A64_REG_XZR, iGprLeft, (uint32_t)uImm >> 12,
                                                         true /*64Bit*/, true /*fSetFlags*/, true /*fShift12*/);
    }
    else
    {
        /* Use temporary register for the immediate. */
        uint8_t iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, uImm);
        off = iemNativeEmitCmpGprWithGpr(pReNative, off, iGprLeft, iTmpReg);
        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }

#else
# error "Port me!"
#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a compare of a 32-bit GPR with a constant value, settings status
 * flags/whatever for use with conditional instruction.
 *
 * @note On ARM64 the @a uImm value must be in the range 0x000..0xfff or that
 *       shifted 12 bits to the left (e.g. 0x1000..0xfff0000 with the lower 12
 *       bits all zero).  Will release assert or throw exception if the caller
 *       violates this restriction.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitCmpGpr32WithImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprLeft, uint32_t uImm)
{
#ifdef RT_ARCH_AMD64
    if (iGprLeft >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    if (uImm <= UINT32_C(0x7f))
    {
        /* cmp Ev, Ib */
        pCodeBuf[off++] = 0x83;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 7, iGprLeft & 7);
        pCodeBuf[off++] = (uint8_t)uImm;
    }
    else
    {
        /* cmp Ev, imm */
        pCodeBuf[off++] = 0x81;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 7, iGprLeft & 7);
        pCodeBuf[off++] = RT_BYTE1(uImm);
        pCodeBuf[off++] = RT_BYTE2(uImm);
        pCodeBuf[off++] = RT_BYTE3(uImm);
        pCodeBuf[off++] = RT_BYTE4(uImm);
    }

#elif defined(RT_ARCH_ARM64)
    /** @todo guess there are clevere things we can do here...   */
    if (uImm < _4K)
        pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, ARMV8_A64_REG_XZR, iGprLeft, (uint32_t)uImm,
                                                      false /*64Bit*/, true /*fSetFlags*/);
    else if ((uImm & ~(uint32_t)0xfff000) == 0)
        pCodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, ARMV8_A64_REG_XZR, iGprLeft, (uint32_t)uImm,
                                                      false /*64Bit*/, true /*fSetFlags*/, true /*fShift12*/);
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits a compare of a 32-bit GPR with a constant value, settings status
 * flags/whatever for use with conditional instruction.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCmpGpr32WithImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprLeft, uint32_t uImm)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitCmpGpr32WithImmEx(iemNativeInstrBufEnsure(pReNative, off, 7), off, iGprLeft, uImm);

#elif defined(RT_ARCH_ARM64)
    /** @todo guess there are clevere things we can do here...   */
    if (uImm < _4K)
    {
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, ARMV8_A64_REG_XZR, iGprLeft, (uint32_t)uImm,
                                                         false /*64Bit*/, true /*fSetFlags*/);
    }
    else if ((uImm & ~(uint32_t)0xfff000) == 0)
    {
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, ARMV8_A64_REG_XZR, iGprLeft, (uint32_t)uImm,
                                                         false /*64Bit*/, true /*fSetFlags*/, true /*fShift12*/);
    }
    else
    {
        /* Use temporary register for the immediate. */
        uint8_t iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, uImm);
        off = iemNativeEmitCmpGpr32WithGpr(pReNative, off, iGprLeft, iTmpReg);
        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }

#else
# error "Port me!"
#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a compare of a 32-bit GPR with a constant value, settings status
 * flags/whatever for use with conditional instruction.
 *
 * @note ARM64: Helper register is required (@a idxTmpReg) for isolating the
 *       16-bit value from @a iGrpLeft.
 * @note On ARM64 the @a uImm value must be in the range 0x000..0xfff or that
 *       shifted 12 bits to the left (e.g. 0x1000..0xfff0000 with the lower 12
 *       bits all zero).  Will release assert or throw exception if the caller
 *       violates this restriction.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitCmpGpr16WithImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprLeft, uint16_t uImm,
                               uint8_t idxTmpReg = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iGprLeft >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    if (uImm <= UINT32_C(0x7f))
    {
        /* cmp Ev, Ib */
        pCodeBuf[off++] = 0x83;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 7, iGprLeft & 7);
        pCodeBuf[off++] = (uint8_t)uImm;
    }
    else
    {
        /* cmp Ev, imm */
        pCodeBuf[off++] = 0x81;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 7, iGprLeft & 7);
        pCodeBuf[off++] = RT_BYTE1(uImm);
        pCodeBuf[off++] = RT_BYTE2(uImm);
    }
    RT_NOREF(idxTmpReg);

#elif defined(RT_ARCH_ARM64)
# ifdef IEM_WITH_THROW_CATCH
    AssertStmt(idxTmpReg < 32, IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
    AssertReleaseStmt(idxTmpReg < 32, off = UINT32_MAX);
# endif
    Assert(Armv8A64ConvertImmRImmS2Mask32(15, 0) == 0xffff);
    pCodeBuf[off++] = Armv8A64MkInstrAndImm(idxTmpReg, iGprLeft, 15, 0, false /*f64Bit*/);
    off = iemNativeEmitCmpGpr32WithImmEx(pCodeBuf, off, idxTmpReg, uImm);

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits a compare of a 16-bit GPR with a constant value, settings status
 * flags/whatever for use with conditional instruction.
 *
 * @note ARM64: Helper register is required (idxTmpReg).
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCmpGpr16WithImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprLeft, uint16_t uImm,
                             uint8_t idxTmpReg = UINT8_MAX)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitCmpGpr16WithImmEx(iemNativeInstrBufEnsure(pReNative, off, 7), off, iGprLeft, uImm, idxTmpReg);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitCmpGpr16WithImmEx(iemNativeInstrBufEnsure(pReNative, off, 2), off, iGprLeft, uImm, idxTmpReg);
#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}



/*********************************************************************************************************************************
*   Branching                                                                                                                    *
*********************************************************************************************************************************/

/**
 * Emits a JMP rel32 / B imm19 to the given label.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitJmpToLabelEx(PIEMRECOMPILERSTATE pReNative, PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint32_t idxLabel)
{
    Assert(idxLabel < pReNative->cLabels);

#ifdef RT_ARCH_AMD64
    if (pReNative->paLabels[idxLabel].off != UINT32_MAX)
    {
        uint32_t offRel = pReNative->paLabels[idxLabel].off - (off + 2);
        if ((int32_t)offRel < 128 && (int32_t)offRel >= -128)
        {
            pCodeBuf[off++] = 0xeb;                 /* jmp rel8 */
            pCodeBuf[off++] = (uint8_t)offRel;
        }
        else
        {
            offRel -= 3;
            pCodeBuf[off++] = 0xe9;                 /* jmp rel32 */
            pCodeBuf[off++] = RT_BYTE1(offRel);
            pCodeBuf[off++] = RT_BYTE2(offRel);
            pCodeBuf[off++] = RT_BYTE3(offRel);
            pCodeBuf[off++] = RT_BYTE4(offRel);
        }
    }
    else
    {
        pCodeBuf[off++] = 0xe9;                     /* jmp rel32 */
        iemNativeAddFixup(pReNative, off, idxLabel, kIemNativeFixupType_Rel32, -4);
        pCodeBuf[off++] = 0xfe;
        pCodeBuf[off++] = 0xff;
        pCodeBuf[off++] = 0xff;
        pCodeBuf[off++] = 0xff;
    }
    pCodeBuf[off++] = 0xcc;                         /* int3 poison */

#elif defined(RT_ARCH_ARM64)
    if (pReNative->paLabels[idxLabel].off != UINT32_MAX)
    {
        pCodeBuf[off] = Armv8A64MkInstrB(pReNative->paLabels[idxLabel].off - off);
        off++;
    }
    else
    {
        iemNativeAddFixup(pReNative, off, idxLabel, kIemNativeFixupType_RelImm26At0);
        pCodeBuf[off++] = Armv8A64MkInstrB(-1);
    }

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits a JMP rel32 / B imm19 to the given label.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitJmpToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t idxLabel)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitJmpToLabelEx(pReNative, iemNativeInstrBufEnsure(pReNative, off, 6), off, idxLabel);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitJmpToLabelEx(pReNative, iemNativeInstrBufEnsure(pReNative, off, 1), off, idxLabel);
#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a JMP rel32 / B imm19 to a new undefined label.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitJmpToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off, IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0)
{
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, enmLabelType, UINT32_MAX /*offWhere*/, uData);
    return iemNativeEmitJmpToLabel(pReNative, off, idxLabel);
}

/** Condition type. */
#ifdef RT_ARCH_AMD64
typedef enum IEMNATIVEINSTRCOND : uint8_t
{
    kIemNativeInstrCond_o = 0,
    kIemNativeInstrCond_no,
    kIemNativeInstrCond_c,
    kIemNativeInstrCond_nc,
    kIemNativeInstrCond_e,
    kIemNativeInstrCond_z = kIemNativeInstrCond_e,
    kIemNativeInstrCond_ne,
    kIemNativeInstrCond_nz = kIemNativeInstrCond_ne,
    kIemNativeInstrCond_be,
    kIemNativeInstrCond_nbe,
    kIemNativeInstrCond_s,
    kIemNativeInstrCond_ns,
    kIemNativeInstrCond_p,
    kIemNativeInstrCond_np,
    kIemNativeInstrCond_l,
    kIemNativeInstrCond_nl,
    kIemNativeInstrCond_le,
    kIemNativeInstrCond_nle
} IEMNATIVEINSTRCOND;
#elif defined(RT_ARCH_ARM64)
typedef ARMV8INSTRCOND  IEMNATIVEINSTRCOND;
# define kIemNativeInstrCond_o      todo_conditional_codes
# define kIemNativeInstrCond_no     todo_conditional_codes
# define kIemNativeInstrCond_c      todo_conditional_codes
# define kIemNativeInstrCond_nc     todo_conditional_codes
# define kIemNativeInstrCond_e      kArmv8InstrCond_Eq
# define kIemNativeInstrCond_ne     kArmv8InstrCond_Ne
# define kIemNativeInstrCond_be     kArmv8InstrCond_Ls
# define kIemNativeInstrCond_nbe    kArmv8InstrCond_Hi
# define kIemNativeInstrCond_s      todo_conditional_codes
# define kIemNativeInstrCond_ns     todo_conditional_codes
# define kIemNativeInstrCond_p      todo_conditional_codes
# define kIemNativeInstrCond_np     todo_conditional_codes
# define kIemNativeInstrCond_l      kArmv8InstrCond_Lt
# define kIemNativeInstrCond_nl     kArmv8InstrCond_Ge
# define kIemNativeInstrCond_le     kArmv8InstrCond_Le
# define kIemNativeInstrCond_nle    kArmv8InstrCond_Gt
#else
# error "Port me!"
#endif


/**
 * Emits a Jcc rel32 / B.cc imm19 to the given label (ASSUMED requiring fixup).
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitJccToLabelEx(PIEMRECOMPILERSTATE pReNative, PIEMNATIVEINSTR pCodeBuf, uint32_t off,
                          uint32_t idxLabel, IEMNATIVEINSTRCOND enmCond)
{
    Assert(idxLabel < pReNative->cLabels);

    uint32_t const offLabel = pReNative->paLabels[idxLabel].off;
#ifdef RT_ARCH_AMD64
    if (offLabel >= off)
    {
        /* jcc rel32 */
        pCodeBuf[off++] = 0x0f;
        pCodeBuf[off++] = (uint8_t)enmCond | 0x80;
        iemNativeAddFixup(pReNative, off, idxLabel, kIemNativeFixupType_Rel32, -4);
        pCodeBuf[off++] = 0x00;
        pCodeBuf[off++] = 0x00;
        pCodeBuf[off++] = 0x00;
        pCodeBuf[off++] = 0x00;
    }
    else
    {
        int32_t offDisp = offLabel - (off + 2);
        if ((int8_t)offDisp == offDisp)
        {
            /* jcc rel8 */
            pCodeBuf[off++] = (uint8_t)enmCond | 0x70;
            pCodeBuf[off++] = RT_BYTE1((uint32_t)offDisp);
        }
        else
        {
            /* jcc rel32 */
            offDisp -= 4;
            pCodeBuf[off++] = 0x0f;
            pCodeBuf[off++] = (uint8_t)enmCond | 0x80;
            pCodeBuf[off++] = RT_BYTE1((uint32_t)offDisp);
            pCodeBuf[off++] = RT_BYTE2((uint32_t)offDisp);
            pCodeBuf[off++] = RT_BYTE3((uint32_t)offDisp);
            pCodeBuf[off++] = RT_BYTE4((uint32_t)offDisp);
        }
    }

#elif defined(RT_ARCH_ARM64)
    if (offLabel >= off)
    {
        iemNativeAddFixup(pReNative, off, idxLabel, kIemNativeFixupType_RelImm19At5);
        pCodeBuf[off++] = Armv8A64MkInstrBCond(enmCond, -1);
    }
    else
    {
        Assert(off - offLabel <= 0x3ffffU);
        pCodeBuf[off] = Armv8A64MkInstrBCond(enmCond, offLabel - off);
        off++;
    }

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits a Jcc rel32 / B.cc imm19 to the given label (ASSUMED requiring fixup).
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitJccToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t idxLabel, IEMNATIVEINSTRCOND enmCond)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitJccToLabelEx(pReNative, iemNativeInstrBufEnsure(pReNative, off, 6), off, idxLabel, enmCond);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitJccToLabelEx(pReNative, iemNativeInstrBufEnsure(pReNative, off, 1), off, idxLabel, enmCond);
#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a Jcc rel32 / B.cc imm19 to a new label.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitJccToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                           IEMNATIVELABELTYPE enmLabelType, uint16_t uData, IEMNATIVEINSTRCOND enmCond)
{
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, enmLabelType, UINT32_MAX /*offWhere*/, uData);
    return iemNativeEmitJccToLabel(pReNative, off, idxLabel, enmCond);
}


/**
 * Emits a JZ/JE rel32 / B.EQ imm19 to the given label.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJzToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t idxLabel)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitJccToLabel(pReNative, off, idxLabel, kIemNativeInstrCond_e);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitJccToLabel(pReNative, off, idxLabel, kArmv8InstrCond_Eq);
#else
# error "Port me!"
#endif
}

/**
 * Emits a JZ/JE rel32 / B.EQ imm19 to a new label.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJzToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                      IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitJccToNewLabel(pReNative, off, enmLabelType, uData, kIemNativeInstrCond_e);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitJccToNewLabel(pReNative, off, enmLabelType, uData, kArmv8InstrCond_Eq);
#else
# error "Port me!"
#endif
}


/**
 * Emits a JNZ/JNE rel32 / B.NE imm19 to the given label.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJnzToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t idxLabel)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitJccToLabel(pReNative, off, idxLabel, kIemNativeInstrCond_ne);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitJccToLabel(pReNative, off, idxLabel, kArmv8InstrCond_Ne);
#else
# error "Port me!"
#endif
}

/**
 * Emits a JNZ/JNE rel32 / B.NE imm19 to a new label.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJnzToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                       IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitJccToNewLabel(pReNative, off, enmLabelType, uData, kIemNativeInstrCond_ne);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitJccToNewLabel(pReNative, off, enmLabelType, uData, kArmv8InstrCond_Ne);
#else
# error "Port me!"
#endif
}


/**
 * Emits a JBE/JNA rel32 / B.LS imm19 to the given label.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJbeToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t idxLabel)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitJccToLabel(pReNative, off, idxLabel, kIemNativeInstrCond_be);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitJccToLabel(pReNative, off, idxLabel, kArmv8InstrCond_Ls);
#else
# error "Port me!"
#endif
}

/**
 * Emits a JBE/JNA rel32 / B.LS imm19 to a new label.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJbeToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                       IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitJccToNewLabel(pReNative, off, enmLabelType, uData, kIemNativeInstrCond_be);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitJccToNewLabel(pReNative, off, enmLabelType, uData, kArmv8InstrCond_Ls);
#else
# error "Port me!"
#endif
}


/**
 * Emits a JA/JNBE rel32 / B.HI imm19 to the given label.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJaToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t idxLabel)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitJccToLabel(pReNative, off, idxLabel, kIemNativeInstrCond_nbe);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitJccToLabel(pReNative, off, idxLabel, kArmv8InstrCond_Hi);
#else
# error "Port me!"
#endif
}

/**
 * Emits a JA/JNBE rel32 / B.HI imm19 to a new label.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJaToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                      IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitJccToNewLabel(pReNative, off, enmLabelType, uData, kIemNativeInstrCond_nbe);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitJccToNewLabel(pReNative, off, enmLabelType, uData, kArmv8InstrCond_Hi);
#else
# error "Port me!"
#endif
}


/**
 * Emits a JL/JNGE rel32 / B.LT imm19 to the given label.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJlToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t idxLabel)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitJccToLabel(pReNative, off, idxLabel, kIemNativeInstrCond_l);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitJccToLabel(pReNative, off, idxLabel, kArmv8InstrCond_Lt);
#else
# error "Port me!"
#endif
}

/**
 * Emits a JA/JNGE rel32 / B.HI imm19 to a new label.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJlToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                      IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitJccToNewLabel(pReNative, off, enmLabelType, uData, kIemNativeInstrCond_l);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitJccToNewLabel(pReNative, off, enmLabelType, uData, kArmv8InstrCond_Lt);
#else
# error "Port me!"
#endif
}


/**
 * Emits a Jcc rel32 / B.cc imm19 with a fixed displacement.
 *
 * @note The @a offTarget is the absolute jump target (unit is IEMNATIVEINSTR).
 *
 *       Only use hardcoded jumps forward when emitting for exactly one
 *       platform, otherwise apply iemNativeFixupFixedJump() to ensure hitting
 *       the right target address on all platforms!
 *
 *       Please also note that on x86 it is necessary pass off + 256 or higher
 *       for @a offTarget one believe the intervening code is more than 127
 *       bytes long.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitJccToFixedEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint32_t offTarget, IEMNATIVEINSTRCOND enmCond)
{
#ifdef RT_ARCH_AMD64
    /* jcc rel8 / rel32 */
    int32_t offDisp = (int32_t)(offTarget - (off + 2));
    if (offDisp < 128 && offDisp >= -128)
    {
        pCodeBuf[off++] = (uint8_t)enmCond | 0x70;
        pCodeBuf[off++] = RT_BYTE1((uint32_t)offDisp);
    }
    else
    {
        offDisp -= 4;
        pCodeBuf[off++] = 0x0f;
        pCodeBuf[off++] = (uint8_t)enmCond | 0x80;
        pCodeBuf[off++] = RT_BYTE1((uint32_t)offDisp);
        pCodeBuf[off++] = RT_BYTE2((uint32_t)offDisp);
        pCodeBuf[off++] = RT_BYTE3((uint32_t)offDisp);
        pCodeBuf[off++] = RT_BYTE4((uint32_t)offDisp);
    }

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off] = Armv8A64MkInstrBCond(enmCond, (int32_t)(offTarget - off));
    off++;
#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits a Jcc rel32 / B.cc imm19 with a fixed displacement.
 *
 * @note The @a offTarget is the absolute jump target (unit is IEMNATIVEINSTR).
 *
 *       Only use hardcoded jumps forward when emitting for exactly one
 *       platform, otherwise apply iemNativeFixupFixedJump() to ensure hitting
 *       the right target address on all platforms!
 *
 *       Please also note that on x86 it is necessary pass off + 256 or higher
 *       for @a offTarget if one believe the intervening code is more than 127
 *       bytes long.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitJccToFixed(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t offTarget, IEMNATIVEINSTRCOND enmCond)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitJccToFixedEx(iemNativeInstrBufEnsure(pReNative, off, 6), off, offTarget, enmCond);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitJccToFixedEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, offTarget, enmCond);
#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a JZ/JE rel32 / B.EQ imm19 with a fixed displacement.
 *
 * See notes on @a offTarget in the iemNativeEmitJccToFixed() documentation.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJzToFixed(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t offTarget)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitJccToFixed(pReNative, off, offTarget, kIemNativeInstrCond_e);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitJccToFixed(pReNative, off, offTarget, kArmv8InstrCond_Eq);
#else
# error "Port me!"
#endif
}


/**
 * Emits a JNZ/JNE rel32 / B.NE imm19 with a fixed displacement.
 *
 * See notes on @a offTarget in the iemNativeEmitJccToFixed() documentation.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJnzToFixed(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t offTarget)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitJccToFixed(pReNative, off, offTarget, kIemNativeInstrCond_ne);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitJccToFixed(pReNative, off, offTarget, kArmv8InstrCond_Ne);
#else
# error "Port me!"
#endif
}


/**
 * Emits a JBE/JNA rel32 / B.LS imm19 with a fixed displacement.
 *
 * See notes on @a offTarget in the iemNativeEmitJccToFixed() documentation.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJbeToFixed(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t offTarget)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitJccToFixed(pReNative, off, offTarget, kIemNativeInstrCond_be);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitJccToFixed(pReNative, off, offTarget, kArmv8InstrCond_Ls);
#else
# error "Port me!"
#endif
}


/**
 * Emits a JA/JNBE rel32 / B.HI imm19 with a fixed displacement.
 *
 * See notes on @a offTarget in the iemNativeEmitJccToFixed() documentation.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJaToFixed(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t offTarget)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitJccToFixed(pReNative, off, offTarget, kIemNativeInstrCond_nbe);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitJccToFixed(pReNative, off, offTarget, kArmv8InstrCond_Hi);
#else
# error "Port me!"
#endif
}


/**
 * Emits a JMP rel32/rel8 / B imm26 with a fixed displacement.
 *
 * See notes on @a offTarget in the iemNativeEmitJccToFixed() documentation.
 */
DECL_FORCE_INLINE(uint32_t) iemNativeEmitJmpToFixedEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint32_t offTarget)
{
#ifdef RT_ARCH_AMD64
    /* jmp rel8 or rel32 */
    int32_t offDisp = offTarget - (off + 2);
    if (offDisp < 128 && offDisp >= -128)
    {
        pCodeBuf[off++] = 0xeb;
        pCodeBuf[off++] = RT_BYTE1((uint32_t)offDisp);
    }
    else
    {
        offDisp -= 3;
        pCodeBuf[off++] = 0xe9;
        pCodeBuf[off++] = RT_BYTE1((uint32_t)offDisp);
        pCodeBuf[off++] = RT_BYTE2((uint32_t)offDisp);
        pCodeBuf[off++] = RT_BYTE3((uint32_t)offDisp);
        pCodeBuf[off++] = RT_BYTE4((uint32_t)offDisp);
    }

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off] = Armv8A64MkInstrB((int32_t)(offTarget - off));
    off++;

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits a JMP rel32/rel8 / B imm26 with a fixed displacement.
 *
 * See notes on @a offTarget in the iemNativeEmitJccToFixed() documentation.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJmpToFixed(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t offTarget)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitJmpToFixedEx(iemNativeInstrBufEnsure(pReNative, off, 5), off, offTarget);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitJmpToFixedEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, offTarget);
#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Fixes up a conditional jump to a fixed label.
 * @see  iemNativeEmitJmpToFixed, iemNativeEmitJnzToFixed,
 *       iemNativeEmitJzToFixed, ...
 */
DECL_INLINE_THROW(void) iemNativeFixupFixedJump(PIEMRECOMPILERSTATE pReNative, uint32_t offFixup, uint32_t offTarget)
{
#ifdef RT_ARCH_AMD64
    uint8_t * const pbCodeBuf = pReNative->pInstrBuf;
    uint8_t const   bOpcode   = pbCodeBuf[offFixup];
    if ((uint8_t)(bOpcode - 0x70) < (uint8_t)0x10 || bOpcode == 0xeb)
    {
        pbCodeBuf[offFixup + 1] = (uint8_t)(offTarget - (offFixup + 2));
        AssertStmt((int8_t)pbCodeBuf[offFixup + 1] == (int32_t)(offTarget - (offFixup + 2)),
                   IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_EMIT_FIXED_JUMP_OUT_OF_RANGE));
    }
    else
    {
        if (bOpcode != 0x0f)
            Assert(bOpcode == 0xe9);
        else
        {
            offFixup += 1;
            Assert((uint8_t)(pbCodeBuf[offFixup] - 0x80) <= 0x10);
        }
        uint32_t const offRel32 = offTarget - (offFixup + 5);
        pbCodeBuf[offFixup + 1] = RT_BYTE1(offRel32);
        pbCodeBuf[offFixup + 2] = RT_BYTE2(offRel32);
        pbCodeBuf[offFixup + 3] = RT_BYTE3(offRel32);
        pbCodeBuf[offFixup + 4] = RT_BYTE4(offRel32);
    }

#elif defined(RT_ARCH_ARM64)
    int32_t const    offDisp     = offTarget - offFixup;
    uint32_t * const pu32CodeBuf = pReNative->pInstrBuf;
    if ((pu32CodeBuf[offFixup] & UINT32_C(0xff000000)) == UINT32_C(0x54000000))
    {
        /* B.COND + BC.COND */
        Assert(offDisp >= -262144 && offDisp < 262144);
        pu32CodeBuf[offFixup] = (pu32CodeBuf[offFixup] & UINT32_C(0xff00001f))
                              | (((uint32_t)offDisp    & UINT32_C(0x0007ffff)) << 5);
    }
    else if ((pu32CodeBuf[offFixup] & UINT32_C(0xfc000000)) == UINT32_C(0x14000000))
    {
        /* B imm26 */
        Assert(offDisp >= -33554432 && offDisp < 33554432);
        pu32CodeBuf[offFixup] = (pu32CodeBuf[offFixup] & UINT32_C(0xfc000000))
                              | ((uint32_t)offDisp     & UINT32_C(0x03ffffff));
    }
    else if ((pu32CodeBuf[offFixup] & UINT32_C(0x7e000000)) == UINT32_C(0x34000000))
    {
        /* CBZ / CBNZ reg, imm19 */
        Assert((pu32CodeBuf[offFixup] & UINT32_C(0x7e000000)) == UINT32_C(0x34000000));
        Assert(offDisp >= -1048576 && offDisp < 1048576);
        pu32CodeBuf[offFixup] = (pu32CodeBuf[offFixup]    & UINT32_C(0xff00001f))
                              | (((uint32_t)offDisp << 5) & UINT32_C(0x00ffffe0));
    }
    else
    {
        /* TBZ / TBNZ reg, bit5, imm14 */
        Assert((pu32CodeBuf[offFixup] & UINT32_C(0x7e000000)) == UINT32_C(0x36000000));
        Assert(offDisp >= -8192 && offDisp < 8192);
        pu32CodeBuf[offFixup] = (pu32CodeBuf[offFixup]    & UINT32_C(0xfff8001f))
                              | (((uint32_t)offDisp << 5) & UINT32_C(0x0007ffe0));
    }

#else
# error "Port me!"
#endif
}


#ifdef RT_ARCH_AMD64
/**
 * For doing bt on a register.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAmd64TestBitInGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprSrc, uint8_t iBitNo)
{
    Assert(iBitNo < 64);
    /* bt Ev, imm8 */
    if (iBitNo >= 32)
        pCodeBuf[off++] = X86_OP_REX_W | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    else if (iGprSrc >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xba;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4, iGprSrc & 7);
    pCodeBuf[off++] = iBitNo;
    return off;
}
#endif /* RT_ARCH_AMD64 */


/**
 * Internal helper, don't call directly.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestBitInGprAndJmpToFixedIfCcEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprSrc, uint8_t iBitNo,
                                             uint32_t offTarget, uint32_t *poffFixup, bool fJmpIfSet)
{
    Assert(iBitNo < 64);
#ifdef RT_ARCH_AMD64
    if (iBitNo < 8)
    {
        /* test Eb, imm8 */
        if (iGprSrc >= 4)
            pCodeBuf[off++] = iGprSrc >= 8 ? X86_OP_REX_B : X86_OP_REX;
        pCodeBuf[off++] = 0xf6;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprSrc & 7);
        pCodeBuf[off++] = (uint8_t)1 << iBitNo;
        if (poffFixup)
            *poffFixup = off;
        off = iemNativeEmitJccToFixedEx(pCodeBuf, off, offTarget, fJmpIfSet ? kIemNativeInstrCond_ne : kIemNativeInstrCond_e);
    }
    else
    {
        /* bt Ev, imm8 */
        if (iBitNo >= 32)
            pCodeBuf[off++] = X86_OP_REX_W | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
        else if (iGprSrc >= 8)
            pCodeBuf[off++] = X86_OP_REX_B;
        pCodeBuf[off++] = 0x0f;
        pCodeBuf[off++] = 0xba;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4, iGprSrc & 7);
        pCodeBuf[off++] = iBitNo;
        if (poffFixup)
            *poffFixup = off;
        off = iemNativeEmitJccToFixedEx(pCodeBuf, off, offTarget, fJmpIfSet ? kIemNativeInstrCond_c : kIemNativeInstrCond_nc);
    }

#elif defined(RT_ARCH_ARM64)
    /* Just use the TBNZ instruction here. */
    if (poffFixup)
        *poffFixup = off;
    pCodeBuf[off++] = Armv8A64MkInstrTbzTbnz(fJmpIfSet, off - offTarget, iGprSrc, iBitNo);

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits a jump to @a idxTarget on the condition that bit @a iBitNo _is_ _set_
 * in @a iGprSrc.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestBitInGprAndJmpToFixedIfSetEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprSrc, uint8_t iBitNo,
                                              uint32_t offTarget, uint32_t *poffFixup)
{
    return iemNativeEmitTestBitInGprAndJmpToFixedIfCcEx(pCodeBuf, off, iGprSrc, iBitNo, offTarget, poffFixup, true /*fJmpIfSet*/);
}


/**
 * Emits a jump to @a idxTarget on the condition that bit @a iBitNo _is_ _not_
 * _set_ in @a iGprSrc.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestBitInGprAndJmpToLabelIfNotSetEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprSrc, uint8_t iBitNo,
                                                 uint32_t offTarget, uint32_t *poffFixup)
{
    return iemNativeEmitTestBitInGprAndJmpToFixedIfCcEx(pCodeBuf, off, iGprSrc, iBitNo, offTarget, poffFixup, false /*fJmpIfSet*/);
}



/**
 * Internal helper, don't call directly.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestBitInGprAndJmpToLabelIfCcEx(PIEMRECOMPILERSTATE pReNative, PIEMNATIVEINSTR pCodeBuf, uint32_t off,
                                             uint8_t iGprSrc, uint8_t iBitNo, uint32_t idxLabel, bool fJmpIfSet)
{
    Assert(iBitNo < 64);
#ifdef RT_ARCH_AMD64
    if (iBitNo < 8)
    {
        /* test Eb, imm8 */
        if (iGprSrc >= 4)
            pCodeBuf[off++] = iGprSrc >= 8 ? X86_OP_REX_B : X86_OP_REX;
        pCodeBuf[off++] = 0xf6;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprSrc & 7);
        pCodeBuf[off++] = (uint8_t)1 << iBitNo;
        off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabel,
                                        fJmpIfSet ? kIemNativeInstrCond_ne : kIemNativeInstrCond_e);
    }
    else
    {
        /* bt Ev, imm8 */
        if (iBitNo >= 32)
            pCodeBuf[off++] = X86_OP_REX_W | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
        else if (iGprSrc >= 8)
            pCodeBuf[off++] = X86_OP_REX_B;
        pCodeBuf[off++] = 0x0f;
        pCodeBuf[off++] = 0xba;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4, iGprSrc & 7);
        pCodeBuf[off++] = iBitNo;
        off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabel,
                                        fJmpIfSet ? kIemNativeInstrCond_c : kIemNativeInstrCond_nc);
    }

#elif defined(RT_ARCH_ARM64)
    /* Use the TBNZ instruction here. */
    if (pReNative->paLabels[idxLabel].enmType > kIemNativeLabelType_LastWholeTbBranch)
    {
        AssertMsg(pReNative->paLabels[idxLabel].off == UINT32_MAX,
                  ("TODO: Please enable & test commented out code for jumping back to a predefined label.\n"));
        //uint32_t offLabel = pReNative->paLabels[idxLabel].off;
        //if (offLabel == UINT32_MAX)
        {
            iemNativeAddFixup(pReNative, off, idxLabel, kIemNativeFixupType_RelImm14At5);
            pCodeBuf[off++] = Armv8A64MkInstrTbzTbnz(fJmpIfSet, 0, iGprSrc, iBitNo);
        }
        //else
        //{
        //    RT_BREAKPOINT();
        //    Assert(off - offLabel <= 0x1fffU);
        //    pCodeBuf[off++] = Armv8A64MkInstrTbzTbnz(fJmpIfSet, offLabel - off, iGprSrc, iBitNo);
        //
        //}
    }
    else
    {
        Assert(Armv8A64ConvertImmRImmS2Mask64(0x40, (64U - iBitNo) & 63U) == RT_BIT_64(iBitNo));
        pCodeBuf[off++] = Armv8A64MkInstrTstImm(iGprSrc, 0x40, (64U - iBitNo) & 63U);
        iemNativeAddFixup(pReNative, off, idxLabel, kIemNativeFixupType_RelImm19At5);
        pCodeBuf[off++] = Armv8A64MkInstrBCond(fJmpIfSet ? kArmv8InstrCond_Ne : kArmv8InstrCond_Eq, 0);
    }

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits a jump to @a idxLabel on the condition that bit @a iBitNo _is_ _set_ in
 * @a iGprSrc.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestBitInGprAndJmpToLabelIfSetEx(PIEMRECOMPILERSTATE pReNative, PIEMNATIVEINSTR pCodeBuf, uint32_t off,
                                              uint8_t iGprSrc, uint8_t iBitNo, uint32_t idxLabel)
{
    return iemNativeEmitTestBitInGprAndJmpToLabelIfCcEx(pReNative, pCodeBuf, off, iGprSrc, iBitNo, idxLabel, true /*fJmpIfSet*/);
}


/**
 * Emits a jump to @a idxLabel on the condition that bit @a iBitNo _is_ _not_
 * _set_ in @a iGprSrc.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestBitInGprAndJmpToLabelIfNotSetEx(PIEMRECOMPILERSTATE pReNative, PIEMNATIVEINSTR pCodeBuf, uint32_t off,
                                                 uint8_t iGprSrc, uint8_t iBitNo, uint32_t idxLabel)
{
    return iemNativeEmitTestBitInGprAndJmpToLabelIfCcEx(pReNative, pCodeBuf, off, iGprSrc, iBitNo, idxLabel, false /*fJmpIfSet*/);
}


/**
 * Internal helper, don't call directly.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestBitInGprAndJmpToLabelIfCc(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc,
                                           uint8_t iBitNo, uint32_t idxLabel, bool fJmpIfSet)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitTestBitInGprAndJmpToLabelIfCcEx(pReNative, iemNativeInstrBufEnsure(pReNative, off, 5+6), off,
                                                       iGprSrc, iBitNo, idxLabel, fJmpIfSet);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitTestBitInGprAndJmpToLabelIfCcEx(pReNative, iemNativeInstrBufEnsure(pReNative, off, 2), off,
                                                       iGprSrc, iBitNo, idxLabel, fJmpIfSet);
#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a jump to @a idxLabel on the condition that bit @a iBitNo _is_ _set_ in
 * @a iGprSrc.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitTestBitInGprAndJmpToLabelIfSet(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                        uint8_t iGprSrc, uint8_t iBitNo, uint32_t idxLabel)
{
    return iemNativeEmitTestBitInGprAndJmpToLabelIfCc(pReNative, off, iGprSrc, iBitNo, idxLabel, true /*fJmpIfSet*/);
}


/**
 * Emits a jump to @a idxLabel on the condition that bit @a iBitNo _is_ _not_
 * _set_ in @a iGprSrc.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitTestBitInGprAndJmpToLabelIfNotSet(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                           uint8_t iGprSrc, uint8_t iBitNo, uint32_t idxLabel)
{
    return iemNativeEmitTestBitInGprAndJmpToLabelIfCc(pReNative, off, iGprSrc, iBitNo, idxLabel, false /*fJmpIfSet*/);
}


/**
 * Emits a test for any of the bits from @a fBits in @a iGprSrc, setting CPU
 * flags accordingly.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestAnyBitsInGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, uint64_t fBits)
{
    Assert(fBits != 0);
#ifdef RT_ARCH_AMD64

    if (fBits >= UINT32_MAX)
    {
        uint8_t iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, fBits);

        /* test Ev,Gv */
        uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5);
        pbCodeBuf[off++] = X86_OP_REX_W | (iGprSrc < 8 ? 0 : X86_OP_REX_R) | (iTmpReg < 8 ? 0 : X86_OP_REX_B);
        pbCodeBuf[off++] = 0x85;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprSrc & 8, iTmpReg & 7);

        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }
    else if (fBits <= UINT32_MAX)
    {
        /* test Eb, imm8 or test Ev, imm32 */
        uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
        if (fBits <= UINT8_MAX)
        {
            if (iGprSrc >= 4)
                pbCodeBuf[off++] = iGprSrc >= 8 ? X86_OP_REX_B : X86_OP_REX;
            pbCodeBuf[off++] = 0xf6;
            pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprSrc & 7);
            pbCodeBuf[off++] = (uint8_t)fBits;
        }
        else
        {
            if (iGprSrc >= 8)
                pbCodeBuf[off++] = X86_OP_REX_B;
            pbCodeBuf[off++] = 0xf7;
            pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprSrc & 7);
            pbCodeBuf[off++] = RT_BYTE1(fBits);
            pbCodeBuf[off++] = RT_BYTE2(fBits);
            pbCodeBuf[off++] = RT_BYTE3(fBits);
            pbCodeBuf[off++] = RT_BYTE4(fBits);
        }
    }
    /** @todo implement me. */
    else
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_EMIT_CASE_NOT_IMPLEMENTED_1));

#elif defined(RT_ARCH_ARM64)
    uint32_t uImmR     = 0;
    uint32_t uImmNandS = 0;
    if (Armv8A64ConvertMask64ToImmRImmS(fBits, &uImmNandS, &uImmR))
    {
        /* ands xzr, iGprSrc, #fBits */
        uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAndsImm(ARMV8_A64_REG_XZR, iGprSrc, uImmNandS, uImmR);
    }
    else
    {
        /* ands xzr, iGprSrc, iTmpReg */
        uint8_t const iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, fBits);
        uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAnds(ARMV8_A64_REG_XZR, iGprSrc, iTmpReg);
        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }

#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a test for any of the bits from @a fBits in the lower 32 bits of
 * @a iGprSrc, setting CPU flags accordingly.
 *
 * @note For ARM64 this only supports @a fBits values that can be expressed
 *       using the two 6-bit immediates of the ANDS instruction.  The caller
 *       must make sure this is possible!
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitTestAnyBitsInGpr32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprSrc, uint32_t fBits,
                                  uint8_t iTmpReg = UINT8_MAX)
{
    Assert(fBits != 0);

#ifdef RT_ARCH_AMD64
    if (fBits <= UINT8_MAX)
    {
        /* test Eb, imm8 */
        if (iGprSrc >= 4)
            pCodeBuf[off++] = iGprSrc >= 8 ? X86_OP_REX_B : X86_OP_REX;
        pCodeBuf[off++] = 0xf6;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprSrc & 7);
        pCodeBuf[off++] = (uint8_t)fBits;
    }
    else
    {
        /* test Ev, imm32 */
        if (iGprSrc >= 8)
            pCodeBuf[off++] = X86_OP_REX_B;
        pCodeBuf[off++] = 0xf7;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprSrc & 7);
        pCodeBuf[off++] = RT_BYTE1(fBits);
        pCodeBuf[off++] = RT_BYTE2(fBits);
        pCodeBuf[off++] = RT_BYTE3(fBits);
        pCodeBuf[off++] = RT_BYTE4(fBits);
    }
    RT_NOREF(iTmpReg);

#elif defined(RT_ARCH_ARM64)
    /* ands xzr, src, #fBits */
    uint32_t uImmR     = 0;
    uint32_t uImmNandS = 0;
    if (Armv8A64ConvertMask32ToImmRImmS(fBits, &uImmNandS, &uImmR))
        pCodeBuf[off++] = Armv8A64MkInstrAndsImm(ARMV8_A64_REG_XZR, iGprSrc, uImmNandS, uImmR, false /*f64Bit*/);
    else if (iTmpReg != UINT8_MAX)
    {
        off = iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off, iTmpReg, fBits);
        pCodeBuf[off++] = Armv8A64MkInstrAnds(ARMV8_A64_REG_XZR, iGprSrc, iTmpReg, false /*f64Bit*/);
    }
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "Port me!"
#endif
    return off;
}



/**
 * Emits a test for any of the bits from @a fBits in the lower 8 bits of
 * @a iGprSrc, setting CPU flags accordingly.
 *
 * @note For ARM64 this only supports @a fBits values that can be expressed
 *       using the two 6-bit immediates of the ANDS instruction.  The caller
 *       must make sure this is possible!
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitTestAnyBitsInGpr8Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprSrc, uint8_t fBits)
{
    Assert(fBits != 0);

#ifdef RT_ARCH_AMD64
    /* test Eb, imm8 */
    if (iGprSrc >= 4)
        pCodeBuf[off++] = iGprSrc >= 8 ? X86_OP_REX_B : X86_OP_REX;
    pCodeBuf[off++] = 0xf6;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprSrc & 7);
    pCodeBuf[off++] = fBits;

#elif defined(RT_ARCH_ARM64)
    /* ands xzr, src, #fBits */
    uint32_t uImmR     = 0;
    uint32_t uImmNandS = 0;
    if (Armv8A64ConvertMask32ToImmRImmS(fBits, &uImmNandS, &uImmR))
        pCodeBuf[off++] = Armv8A64MkInstrAndsImm(ARMV8_A64_REG_XZR, iGprSrc, uImmNandS, uImmR, false /*f64Bit*/);
    else
# ifdef IEM_WITH_THROW_CATCH
        AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
        AssertReleaseFailedStmt(off = UINT32_MAX);
# endif

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits a test for any of the bits from @a fBits in the lower 8 bits of
 * @a iGprSrc, setting CPU flags accordingly.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestAnyBitsInGpr8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, uint8_t fBits)
{
    Assert(fBits != 0);

#ifdef RT_ARCH_AMD64
    off = iemNativeEmitTestAnyBitsInGpr8Ex(iemNativeInstrBufEnsure(pReNative, off, 4), off, iGprSrc, fBits);

#elif defined(RT_ARCH_ARM64)
    /* ands xzr, src, [tmp|#imm] */
    uint32_t uImmR     = 0;
    uint32_t uImmNandS = 0;
    if (Armv8A64ConvertMask32ToImmRImmS(fBits, &uImmNandS, &uImmR))
    {
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAndsImm(ARMV8_A64_REG_XZR, iGprSrc, uImmNandS, uImmR, false /*f64Bit*/);
    }
    else
    {
        /* Use temporary register for the 64-bit immediate. */
        uint8_t iTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, fBits);
        uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrAnds(ARMV8_A64_REG_XZR, iGprSrc, iTmpReg, false /*f64Bit*/);
        iemNativeRegFreeTmpImm(pReNative, iTmpReg);
    }

#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a jump to @a idxLabel on the condition _any_ of the bits in @a fBits
 * are set in @a iGprSrc.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestAnyBitsInGprAndJmpToLabelIfAnySet(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                   uint8_t iGprSrc, uint64_t fBits, uint32_t idxLabel)
{
    Assert(fBits); Assert(!RT_IS_POWER_OF_TWO(fBits));

    off = iemNativeEmitTestAnyBitsInGpr(pReNative, off, iGprSrc, fBits);
    off = iemNativeEmitJnzToLabel(pReNative, off, idxLabel);

    return off;
}


/**
 * Emits a jump to @a idxLabel on the condition _none_ of the bits in @a fBits
 * are set in @a iGprSrc.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestAnyBitsInGprAndJmpToLabelIfNoneSet(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                    uint8_t iGprSrc, uint64_t fBits, uint32_t idxLabel)
{
    Assert(fBits); Assert(!RT_IS_POWER_OF_TWO(fBits));

    off = iemNativeEmitTestAnyBitsInGpr(pReNative, off, iGprSrc, fBits);
    off = iemNativeEmitJzToLabel(pReNative, off, idxLabel);

    return off;
}


/**
 * Emits code that jumps to @a idxLabel if @a iGprSrc is not zero.
 *
 * The operand size is given by @a f64Bit.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToLabelEx(PIEMRECOMPILERSTATE pReNative, PIEMNATIVEINSTR pCodeBuf, uint32_t off,
                                                     uint8_t iGprSrc, bool f64Bit, bool fJmpIfNotZero, uint32_t idxLabel)
{
    Assert(idxLabel < pReNative->cLabels);

#ifdef RT_ARCH_AMD64
    /* test reg32,reg32  / test reg64,reg64 */
    if (f64Bit)
        pCodeBuf[off++] = X86_OP_REX_W | (iGprSrc < 8 ? 0 : X86_OP_REX_R | X86_OP_REX_B);
    else if (iGprSrc >= 8)
        pCodeBuf[off++] = X86_OP_REX_R | X86_OP_REX_B;
    pCodeBuf[off++] = 0x85;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprSrc & 7, iGprSrc & 7);

    /* jnz idxLabel  */
    off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabel,
                                    fJmpIfNotZero ? kIemNativeInstrCond_ne : kIemNativeInstrCond_e);

#elif defined(RT_ARCH_ARM64)
    if (pReNative->paLabels[idxLabel].off != UINT32_MAX)
    {
        pCodeBuf[off] = Armv8A64MkInstrCbzCbnz(fJmpIfNotZero, (int32_t)(pReNative->paLabels[idxLabel].off - off),
                                               iGprSrc, f64Bit);
        off++;
    }
    else
    {
        iemNativeAddFixup(pReNative, off, idxLabel, kIemNativeFixupType_RelImm19At5);
        pCodeBuf[off++] = Armv8A64MkInstrCbzCbnz(fJmpIfNotZero, 0, iGprSrc, f64Bit);
    }

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits code that jumps to @a idxLabel if @a iGprSrc is not zero.
 *
 * The operand size is given by @a f64Bit.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc,
                                                   bool f64Bit, bool fJmpIfNotZero, uint32_t idxLabel)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToLabelEx(pReNative, iemNativeInstrBufEnsure(pReNative, off, 3 + 6),
                                                               off, iGprSrc, f64Bit, fJmpIfNotZero, idxLabel);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToLabelEx(pReNative, iemNativeInstrBufEnsure(pReNative, off, 1),
                                                               off, iGprSrc, f64Bit, fJmpIfNotZero, idxLabel);
#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code that jumps to @a offTarget if @a iGprSrc is not zero.
 *
 * The operand size is given by @a f64Bit.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToFixedEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off,
                                                     uint8_t iGprSrc, bool f64Bit, bool fJmpIfNotZero, uint32_t offTarget)
{
#ifdef RT_ARCH_AMD64
    /* test reg32,reg32  / test reg64,reg64 */
    if (f64Bit)
        pCodeBuf[off++] = X86_OP_REX_W | (iGprSrc < 8 ? 0 : X86_OP_REX_R | X86_OP_REX_B);
    else if (iGprSrc >= 8)
        pCodeBuf[off++] = X86_OP_REX_R | X86_OP_REX_B;
    pCodeBuf[off++] = 0x85;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprSrc & 7, iGprSrc & 7);

    /* jnz idxLabel  */
    off = iemNativeEmitJccToFixedEx(pCodeBuf, off, offTarget,
                                    fJmpIfNotZero ? kIemNativeInstrCond_ne : kIemNativeInstrCond_e);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off] = Armv8A64MkInstrCbzCbnz(fJmpIfNotZero, (int32_t)(offTarget - off), iGprSrc, f64Bit);
    off++;

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Emits code that jumps to @a offTarget if @a iGprSrc is not zero.
 *
 * The operand size is given by @a f64Bit.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToFixed(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc,
                                                   bool f64Bit, bool fJmpIfNotZero, uint32_t offTarget)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToFixedEx(iemNativeInstrBufEnsure(pReNative, off, 3 + 6),
                                                               off, iGprSrc, f64Bit, fJmpIfNotZero, offTarget);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToFixedEx(iemNativeInstrBufEnsure(pReNative, off, 1),
                                                               off, iGprSrc, f64Bit, fJmpIfNotZero, offTarget);
#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/* if (Grp1 == 0) Jmp idxLabel; */

/**
 * Emits code that jumps to @a idxLabel if @a iGprSrc is zero.
 *
 * The operand size is given by @a f64Bit.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGprIsZeroAndJmpToLabelEx(PIEMRECOMPILERSTATE pReNative, PIEMNATIVEINSTR pCodeBuf, uint32_t off,
                                            uint8_t iGprSrc, bool f64Bit, uint32_t idxLabel)
{
    return iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToLabelEx(pReNative, pCodeBuf, off, iGprSrc,
                                                                f64Bit, false /*fJmpIfNotZero*/, idxLabel);
}


/**
 * Emits code that jumps to @a idxLabel if @a iGprSrc is zero.
 *
 * The operand size is given by @a f64Bit.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitTestIfGprIsZeroAndJmpToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                      uint8_t iGprSrc, bool f64Bit, uint32_t idxLabel)
{
    return iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToLabel(pReNative, off, iGprSrc, f64Bit, false /*fJmpIfNotZero*/, idxLabel);
}


/**
 * Emits code that jumps to a new label if @a iGprSrc is zero.
 *
 * The operand size is given by @a f64Bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGprIsZeroAndJmpToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, bool f64Bit,
                                             IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0)
{
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, enmLabelType, UINT32_MAX /*offWhere*/, uData);
    return iemNativeEmitTestIfGprIsZeroAndJmpToLabel(pReNative, off, iGprSrc, f64Bit, idxLabel);
}


/**
 * Emits code that jumps to @a offTarget if @a iGprSrc is zero.
 *
 * The operand size is given by @a f64Bit.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitTestIfGprIsZeroAndJmpToFixed(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                      uint8_t iGprSrc, bool f64Bit, uint32_t offTarget)
{
    return iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToFixed(pReNative, off, iGprSrc, f64Bit, false /*fJmpIfNotZero*/, offTarget);
}


/* if (Grp1 != 0) Jmp idxLabel; */

/**
 * Emits code that jumps to @a idxLabel if @a iGprSrc is not zero.
 *
 * The operand size is given by @a f64Bit.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGprIsNotZeroAndJmpToLabelEx(PIEMRECOMPILERSTATE pReNative, PIEMNATIVEINSTR pCodeBuf, uint32_t off,
                                               uint8_t iGprSrc, bool f64Bit, uint32_t idxLabel)
{
    return iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToLabelEx(pReNative, pCodeBuf, off, iGprSrc,
                                                                f64Bit, true /*fJmpIfNotZero*/, idxLabel);
}


/**
 * Emits code that jumps to @a idxLabel if @a iGprSrc is not zero.
 *
 * The operand size is given by @a f64Bit.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitTestIfGprIsNotZeroAndJmpToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                         uint8_t iGprSrc, bool f64Bit, uint32_t idxLabel)
{
    return iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToLabel(pReNative, off, iGprSrc, f64Bit, true /*fJmpIfNotZero*/, idxLabel);
}


/**
 * Emits code that jumps to a new label if @a iGprSrc is not zero.
 *
 * The operand size is given by @a f64Bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGprIsNotZeroAndJmpToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, bool f64Bit,
                                               IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0)
{
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, enmLabelType, UINT32_MAX /*offWhere*/, uData);
    return iemNativeEmitTestIfGprIsNotZeroAndJmpToLabel(pReNative, off, iGprSrc, f64Bit, idxLabel);
}


/* if (Grp1 != Gpr2) Jmp idxLabel; */

/**
 * Emits code that jumps to the given label if @a iGprLeft and @a iGprRight
 * differs.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGprNotEqualGprAndJmpToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                               uint8_t iGprLeft, uint8_t iGprRight, uint32_t idxLabel)
{
    off = iemNativeEmitCmpGprWithGpr(pReNative, off, iGprLeft, iGprRight);
    off = iemNativeEmitJnzToLabel(pReNative, off, idxLabel);
    return off;
}


/**
 * Emits code that jumps to a new label if @a iGprLeft and @a iGprRight differs.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGprNotEqualGprAndJmpToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                  uint8_t iGprLeft, uint8_t iGprRight,
                                                  IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0)
{
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, enmLabelType, UINT32_MAX /*offWhere*/, uData);
    return iemNativeEmitTestIfGprNotEqualGprAndJmpToLabel(pReNative, off, iGprLeft, iGprRight, idxLabel);
}


/* if (Grp != Imm) Jmp idxLabel; */

/**
 * Emits code that jumps to the given label if @a iGprSrc differs from @a uImm.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGprNotEqualImmAndJmpToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                               uint8_t iGprSrc, uint64_t uImm, uint32_t idxLabel)
{
    off = iemNativeEmitCmpGprWithImm(pReNative, off, iGprSrc, uImm);
    off = iemNativeEmitJnzToLabel(pReNative, off, idxLabel);
    return off;
}


/**
 * Emits code that jumps to a new label if @a iGprSrc differs from @a uImm.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGprNotEqualImmAndJmpToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                  uint8_t iGprSrc, uint64_t uImm,
                                                  IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0)
{
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, enmLabelType, UINT32_MAX /*offWhere*/, uData);
    return iemNativeEmitTestIfGprNotEqualImmAndJmpToLabel(pReNative, off, iGprSrc, uImm, idxLabel);
}


/**
 * Emits code that jumps to the given label if 32-bit @a iGprSrc differs from
 * @a uImm.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitTestIfGpr32NotEqualImmAndJmpToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                             uint8_t iGprSrc, uint32_t uImm, uint32_t idxLabel)
{
    off = iemNativeEmitCmpGpr32WithImm(pReNative, off, iGprSrc, uImm);
    off = iemNativeEmitJnzToLabel(pReNative, off, idxLabel);
    return off;
}


/**
 * Emits code that jumps to a new label if 32-bit @a iGprSrc differs from
 * @a uImm.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGpr32NotEqualImmAndJmpToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                    uint8_t iGprSrc, uint32_t uImm,
                                                    IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0)
{
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, enmLabelType, UINT32_MAX /*offWhere*/, uData);
    return iemNativeEmitTestIfGpr32NotEqualImmAndJmpToLabel(pReNative, off, iGprSrc, uImm, idxLabel);
}


/**
 * Emits code that jumps to the given label if 16-bit @a iGprSrc differs from
 * @a uImm.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitTestIfGpr16NotEqualImmAndJmpToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                             uint8_t iGprSrc, uint16_t uImm, uint32_t idxLabel)
{
    off = iemNativeEmitCmpGpr16WithImm(pReNative, off, iGprSrc, uImm);
    off = iemNativeEmitJnzToLabel(pReNative, off, idxLabel);
    return off;
}


/**
 * Emits code that jumps to a new label if 16-bit @a iGprSrc differs from
 * @a uImm.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGpr16NotEqualImmAndJmpToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                    uint8_t iGprSrc, uint16_t uImm,
                                                    IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0)
{
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, enmLabelType, UINT32_MAX /*offWhere*/, uData);
    return iemNativeEmitTestIfGpr16NotEqualImmAndJmpToLabel(pReNative, off, iGprSrc, uImm, idxLabel);
}


/* if (Grp == Imm) Jmp idxLabel; */

/**
 * Emits code that jumps to the given label if @a iGprSrc equals @a uImm.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGprEqualsImmAndJmpToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                             uint8_t iGprSrc, uint64_t uImm, uint32_t idxLabel)
{
    off = iemNativeEmitCmpGprWithImm(pReNative, off, iGprSrc, uImm);
    off = iemNativeEmitJzToLabel(pReNative, off, idxLabel);
    return off;
}


/**
 * Emits code that jumps to a new label if @a iGprSrc equals from @a uImm.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGprEqualsImmAndJmpToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, uint64_t uImm,
                                                IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0)
{
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, enmLabelType, UINT32_MAX /*offWhere*/, uData);
    return iemNativeEmitTestIfGprEqualsImmAndJmpToLabel(pReNative, off, iGprSrc, uImm, idxLabel);
}


/**
 * Emits code that jumps to the given label if 32-bit @a iGprSrc equals @a uImm.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitTestIfGpr32EqualsImmAndJmpToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                           uint8_t iGprSrc, uint32_t uImm, uint32_t idxLabel)
{
    off = iemNativeEmitCmpGpr32WithImm(pReNative, off, iGprSrc, uImm);
    off = iemNativeEmitJzToLabel(pReNative, off, idxLabel);
    return off;
}


/**
 * Emits code that jumps to a new label if 32-bit @a iGprSrc equals @a uImm.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGpr32EqualsImmAndJmpToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, uint32_t uImm,
                                                  IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0)
{
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, enmLabelType, UINT32_MAX /*offWhere*/, uData);
    return iemNativeEmitTestIfGpr32EqualsImmAndJmpToLabel(pReNative, off, iGprSrc, uImm, idxLabel);
}


/**
 * Emits code that jumps to the given label if 16-bit @a iGprSrc equals @a uImm.
 *
 * @note ARM64: Helper register is required (idxTmpReg).
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitTestIfGpr16EqualsImmAndJmpToLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                                           uint8_t iGprSrc, uint16_t uImm, uint32_t idxLabel,
                                                                           uint8_t idxTmpReg = UINT8_MAX)
{
    off = iemNativeEmitCmpGpr16WithImm(pReNative, off, iGprSrc, uImm, idxTmpReg);
    off = iemNativeEmitJzToLabel(pReNative, off, idxLabel);
    return off;
}


/**
 * Emits code that jumps to a new label if 16-bit @a iGprSrc equals @a uImm.
 *
 * @note ARM64: Helper register is required (idxTmpReg).
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTestIfGpr16EqualsImmAndJmpToNewLabel(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, uint16_t uImm,
                                                  IEMNATIVELABELTYPE enmLabelType, uint16_t uData = 0,
                                                  uint8_t idxTmpReg = UINT8_MAX)
{
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, enmLabelType, UINT32_MAX /*offWhere*/, uData);
    return iemNativeEmitTestIfGpr16EqualsImmAndJmpToLabel(pReNative, off, iGprSrc, uImm, idxLabel, idxTmpReg);
}



/*********************************************************************************************************************************
*   Indirect Jumps.                                                                                                              *
*********************************************************************************************************************************/

/**
 * Emits an indirect jump a 64-bit address in a GPR.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJmpViaGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc)
{
#ifdef RT_ARCH_AMD64
    uint8_t * const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3);
    if (iGprSrc >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    pCodeBuf[off++] = 0xff;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4, iGprSrc & 7);

#elif defined(RT_ARCH_ARM64)
    uint32_t * const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pCodeBuf[off++] = Armv8A64MkInstrBr(iGprSrc);

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits an indirect jump to an immediate 64-bit address (uses the temporary GPR).
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitJmpImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uintptr_t uPfn)
{
    off = iemNativeEmitLoadGprImm64(pReNative, off, IEMNATIVE_REG_FIXED_TMP0, uPfn);
    return iemNativeEmitJmpViaGpr(pReNative, off, IEMNATIVE_REG_FIXED_TMP0);
}


/*********************************************************************************************************************************
*   Calls.                                                                                                                       *
*********************************************************************************************************************************/

/**
 * Emits a call to a 64-bit address.
 */
DECL_FORCE_INLINE(uint32_t) iemNativeEmitCallImmEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uintptr_t uPfn,
#ifdef RT_ARCH_AMD64
                                                   uint8_t idxRegTmp = X86_GREG_xAX
#elif defined(RT_ARCH_ARM64)
                                                   uint8_t idxRegTmp = IEMNATIVE_REG_FIXED_TMP0
#else
# error "Port me"
#endif
                                                   )
{
    off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, idxRegTmp, uPfn);

#ifdef RT_ARCH_AMD64
    /* call idxRegTmp */
    if (idxRegTmp >= 8)
        pCodeBuf[off++] = X86_OP_REX_B;
    pCodeBuf[off++] = 0xff;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 2, idxRegTmp & 7);

#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrBlr(idxRegTmp);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a call to a 64-bit address.
 */
template<bool const a_fSkipEflChecks = false>
DECL_INLINE_THROW(uint32_t) iemNativeEmitCallImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uintptr_t uPfn)
{
    if RT_CONSTEXPR_IF(!a_fSkipEflChecks)
    {
        IEMNATIVE_ASSERT_EFLAGS_POSTPONING_ONLY(pReNative, X86_EFL_STATUS_BITS);
        IEMNATIVE_ASSERT_EFLAGS_SKIPPING_ONLY(  pReNative, X86_EFL_STATUS_BITS);
    }

#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGprImm64(pReNative, off, X86_GREG_xAX, uPfn);

    /* call rax */
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);
    pbCodeBuf[off++] = 0xff;
    pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 2, X86_GREG_xAX);

#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitLoadGprImm64(pReNative, off, IEMNATIVE_REG_FIXED_TMP0, uPfn);

    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = Armv8A64MkInstrBlr(IEMNATIVE_REG_FIXED_TMP0);

#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code to load a stack variable into an argument GPR.
 * @throws VERR_IEM_VAR_NOT_INITIALIZED, VERR_IEM_VAR_UNEXPECTED_KIND
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadArgGregFromStackVar(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxRegArg, uint8_t idxVar,
                                     int32_t offAddend = 0, uint32_t fHstVolatileRegsAllowed = UINT32_MAX,
                                     bool fSpilledVarsInVolatileRegs = false)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    PIEMNATIVEVAR const pVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)];
    AssertStmt(pVar->enmKind == kIemNativeVarKind_Stack, IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));

    uint8_t const idxRegVar = pVar->idxReg;
    if (   idxRegVar < RT_ELEMENTS(pReNative->Core.aHstRegs)
        && (   (RT_BIT_32(idxRegVar) & (~IEMNATIVE_CALL_VOLATILE_GREG_MASK | fHstVolatileRegsAllowed))
            || !fSpilledVarsInVolatileRegs ))
    {
        AssertStmt(   !(RT_BIT_32(idxRegVar) & IEMNATIVE_CALL_VOLATILE_GREG_MASK)
                   || (RT_BIT_32(idxRegVar) & fHstVolatileRegsAllowed),
                   IEMNATIVE_DO_LONGJMP(pReNative,  VERR_IEM_REG_IPE_13));
        if (!offAddend)
        {
            if (idxRegArg != idxRegVar)
                off = iemNativeEmitLoadGprFromGpr(pReNative, off, idxRegArg, idxRegVar);
        }
        else
            off = iemNativeEmitLoadGprFromGprWithAddend(pReNative, off, idxRegArg, idxRegVar, offAddend);
    }
    else
    {
        uint8_t const idxStackSlot = pVar->idxStackSlot;
        AssertStmt(idxStackSlot != UINT8_MAX, IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_NOT_INITIALIZED));
        off = iemNativeEmitLoadGprByBp(pReNative, off, idxRegArg, iemNativeStackCalcBpDisp(idxStackSlot));
        if (offAddend)
            off = iemNativeEmitAddGprImm(pReNative, off, idxRegArg, offAddend);
    }
    return off;
}


/**
 * Emits code to load a stack or immediate variable value into an argument GPR,
 * optional with a addend.
 * @throws VERR_IEM_VAR_NOT_INITIALIZED, VERR_IEM_VAR_UNEXPECTED_KIND
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadArgGregFromImmOrStackVar(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxRegArg, uint8_t idxVar,
                                          int32_t offAddend = 0, uint32_t fHstVolatileRegsAllowed = 0,
                                          bool fSpilledVarsInVolatileRegs = false)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    PIEMNATIVEVAR const pVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)];
    if (pVar->enmKind == kIemNativeVarKind_Immediate)
        off = iemNativeEmitLoadGprImm64(pReNative, off, idxRegArg, pVar->u.uValue + offAddend);
    else
        off = iemNativeEmitLoadArgGregFromStackVar(pReNative, off, idxRegArg, idxVar, offAddend,
                                                   fHstVolatileRegsAllowed, fSpilledVarsInVolatileRegs);
    return off;
}


/**
 * Emits code to load the variable address into an argument GPR.
 *
 * This only works for uninitialized and stack variables.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadArgGregWithVarAddr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxRegArg, uint8_t idxVar,
                                    bool fFlushShadows)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    PIEMNATIVEVAR const pVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)];
    AssertStmt(   pVar->enmKind == kIemNativeVarKind_Invalid
               || pVar->enmKind == kIemNativeVarKind_Stack,
               IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));
    AssertStmt(!pVar->fSimdReg,
               IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));

    uint8_t const idxStackSlot   = iemNativeVarGetStackSlot(pReNative, idxVar);
    int32_t const offBpDisp      = iemNativeStackCalcBpDisp(idxStackSlot);

    uint8_t const idxRegVar      = pVar->idxReg;
    if (idxRegVar < RT_ELEMENTS(pReNative->Core.aHstRegs))
    {
        off = iemNativeEmitStoreGprByBp(pReNative, off, offBpDisp, idxRegVar);
        iemNativeRegFreeVar(pReNative, idxRegVar, fFlushShadows);
        Assert(pVar->idxReg == UINT8_MAX);
    }
    Assert(   pVar->idxStackSlot != UINT8_MAX
           && pVar->idxReg       == UINT8_MAX);

    return iemNativeEmitLeaGprByBp(pReNative, off, idxRegArg, offBpDisp);
}



/*********************************************************************************************************************************
*   TB exiting helpers.                                                                                                          *
*********************************************************************************************************************************/

#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
/* IEMAllN8veEmit-x86.h: */
template<uint32_t const a_bmInputRegs>
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeDoPostponedEFlagsAtTbExitEx(PIEMRECOMPILERSTATE pReNative, uint32_t off, PIEMNATIVEINSTR pCodeBuf);

template<uint32_t const a_bmInputRegs>
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeDoPostponedEFlagsAtTbExit(PIEMRECOMPILERSTATE pReNative, uint32_t off);
#endif


/**
 * Helper for marking the current conditional branch as exiting the TB.
 *
 * This simplifies the state consolidation later when we reach the IEM_MC_ENDIF.
 */
DECL_FORCE_INLINE(void) iemNativeMarkCurCondBranchAsExiting(PIEMRECOMPILERSTATE pReNative)
{
    uint8_t idxCondDepth = pReNative->cCondDepth;
    if (idxCondDepth)
    {
        idxCondDepth--;
        pReNative->aCondStack[idxCondDepth].afExitTb[pReNative->aCondStack[idxCondDepth].fInElse] = true;
    }
}


/**
 * Unconditionally exits the translation block via a branch instructions.
 *
 * @note In case a delayed EFLAGS calculation is pending, this may emit an
 *       additional IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS instructions.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fActuallyExitingTb = true, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t) iemNativeEmitTbExitEx(PIEMRECOMPILERSTATE pReNative, PIEMNATIVEINSTR pCodeBuf, uint32_t off)
{
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_ONLY(pReNative, X86_EFL_STATUS_BITS);
    AssertCompile(IEMNATIVELABELTYPE_IS_EXIT_REASON(a_enmExitReason));

    if RT_CONSTEXPR_IF(a_fActuallyExitingTb)
        iemNativeMarkCurCondBranchAsExiting(pReNative);

#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    if RT_CONSTEXPR_IF(a_fPostponedEfl)
        off = iemNativeDoPostponedEFlagsAtTbExitEx<IEMNATIVELABELTYPE_GET_INPUT_REG_MASK(a_enmExitReason)>(pReNative, off,
                                                                                                           pCodeBuf);
#endif

#ifdef RT_ARCH_AMD64
    /* jmp rel32 */
    pCodeBuf[off++] = 0xe9;
    iemNativeAddTbExitFixup(pReNative, off, a_enmExitReason);
    pCodeBuf[off++] = 0xfe;
    pCodeBuf[off++] = 0xff;
    pCodeBuf[off++] = 0xff;
    pCodeBuf[off++] = 0xff;

#elif defined(RT_ARCH_ARM64)
    iemNativeAddTbExitFixup(pReNative, off, a_enmExitReason);
    pCodeBuf[off++] = Armv8A64MkInstrB(-1);

#else
# error "Port me!"
#endif
    return off;
}


/**
 * Unconditionally exits the translation block via a branch instructions.
 *
 * @note In case a delayed EFLAGS calculation is pending, this may emit an
 *       additional IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS instructions.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fActuallyExitingTb = true, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t) iemNativeEmitTbExit(PIEMRECOMPILERSTATE pReNative, uint32_t off)
{
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_ONLY(pReNative, X86_EFL_STATUS_BITS);
    AssertCompile(IEMNATIVELABELTYPE_IS_EXIT_REASON(a_enmExitReason));

    if RT_CONSTEXPR_IF(a_fActuallyExitingTb)
        iemNativeMarkCurCondBranchAsExiting(pReNative);

#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    if RT_CONSTEXPR_IF(a_fPostponedEfl)
        off = iemNativeDoPostponedEFlagsAtTbExit<IEMNATIVELABELTYPE_GET_INPUT_REG_MASK(a_enmExitReason)>(pReNative, off);
#endif

#ifdef RT_ARCH_AMD64
    PIEMNATIVEINSTR pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 6);

    /* jmp rel32 */
    pCodeBuf[off++] = 0xe9;
    iemNativeAddTbExitFixup(pReNative, off, a_enmExitReason);
    pCodeBuf[off++] = 0xfe;
    pCodeBuf[off++] = 0xff;
    pCodeBuf[off++] = 0xff;
    pCodeBuf[off++] = 0xff;

#elif defined(RT_ARCH_ARM64)
    PIEMNATIVEINSTR pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    iemNativeAddTbExitFixup(pReNative, off, a_enmExitReason);
    pCodeBuf[off++] = Armv8A64MkInstrB(-1);

#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a Jcc rel32 / B.cc imm19 to the given label (ASSUMED requiring fixup).
 *
 * @note In case a delayed EFLAGS calculation is pending, this may emit an
 *       additional IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS instructions.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitTbExitJccEx(PIEMRECOMPILERSTATE pReNative, PIEMNATIVEINSTR pCodeBuf, uint32_t off, IEMNATIVEINSTRCOND enmCond)
{
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_ONLY(pReNative, X86_EFL_STATUS_BITS);
    AssertCompile(IEMNATIVELABELTYPE_IS_EXIT_REASON(a_enmExitReason));

#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    if RT_CONSTEXPR_IF(a_fPostponedEfl)
        if (pReNative->PostponedEfl.fEFlags)
        {
            /* Jcc l_NonPrimaryCodeStreamTarget */
            uint32_t const offFixup1 = off;
            off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off + 1, enmCond);

            /* JMP l_PrimaryCodeStreamResume */
            uint32_t const offFixup2 = off;
            off = iemNativeEmitJmpToFixedEx(pCodeBuf, off, off + IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS);

            /* l_NonPrimaryCodeStreamTarget: */
            iemNativeFixupFixedJump(pReNative, offFixup1, off);
            off = iemNativeEmitTbExitEx<a_enmExitReason, false /*a_fActuallyExitingTb*/, true>(pReNative, pCodeBuf, off);

            /* l_PrimaryCodeStreamResume: */
            iemNativeFixupFixedJump(pReNative, offFixup2, off);
            IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
            return off;
        }
#endif

#if defined(RT_ARCH_AMD64)
    /* jcc rel32 */
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = (uint8_t)enmCond | 0x80;
    iemNativeAddTbExitFixup(pReNative, off, a_enmExitReason);
    pCodeBuf[off++] = 0x00;
    pCodeBuf[off++] = 0x00;
    pCodeBuf[off++] = 0x00;
    pCodeBuf[off++] = 0x00;

#else
    /* ARM64 doesn't have the necessary jump range, so we jump via local label
       just like when we keep everything local. */
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, a_enmExitReason, UINT32_MAX /*offWhere*/, 0 /*uData*/);
    off = iemNativeEmitJccToLabelEx(pReNative, pCodeBuf, off, idxLabel, enmCond);
#endif
    return off;
}


/**
 * Emits a Jcc rel32 / B.cc imm19 to the epilog.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t) iemNativeEmitTbExitJcc(PIEMRECOMPILERSTATE pReNative, uint32_t off, IEMNATIVEINSTRCOND enmCond)
{
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_ONLY(pReNative, X86_EFL_STATUS_BITS);
    AssertCompile(IEMNATIVELABELTYPE_IS_EXIT_REASON(a_enmExitReason));

#ifdef RT_ARCH_AMD64
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 6 + IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS + 5);
#elif defined(RT_ARCH_ARM64)
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2 + IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS + 1);
#else
# error "Port me!"
#endif
    off = iemNativeEmitTbExitJccEx<a_enmExitReason, a_fPostponedEfl>(pReNative, pCodeBuf, off, enmCond);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a JNZ/JNE rel32 / B.NE imm19 to the TB exit routine with the given reason.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t) iemNativeEmitTbExitJnz(PIEMRECOMPILERSTATE pReNative, uint32_t off)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitTbExitJcc<a_enmExitReason, a_fPostponedEfl>(pReNative, off, kIemNativeInstrCond_ne);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitTbExitJcc<a_enmExitReason, a_fPostponedEfl>(pReNative, off, kArmv8InstrCond_Ne);
#else
# error "Port me!"
#endif
}


/**
 * Emits a JZ/JE rel32 / B.EQ imm19 to the TB exit routine with the given reason.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t) iemNativeEmitTbExitJz(PIEMRECOMPILERSTATE pReNative, uint32_t off)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitTbExitJcc<a_enmExitReason, a_fPostponedEfl>(pReNative, off, kIemNativeInstrCond_e);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitTbExitJcc<a_enmExitReason, a_fPostponedEfl>(pReNative, off, kArmv8InstrCond_Eq);
#else
# error "Port me!"
#endif
}


/**
 * Emits a JA/JNBE rel32 / B.HI imm19 to the TB exit.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t) iemNativeEmitTbExitJa(PIEMRECOMPILERSTATE pReNative, uint32_t off)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitTbExitJcc<a_enmExitReason, a_fPostponedEfl>(pReNative, off, kIemNativeInstrCond_nbe);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitTbExitJcc<a_enmExitReason, a_fPostponedEfl>(pReNative, off, kArmv8InstrCond_Hi);
#else
# error "Port me!"
#endif
}


/**
 * Emits a JL/JNGE rel32 / B.LT imm19 to the TB exit with the given reason.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t) iemNativeEmitTbExitJl(PIEMRECOMPILERSTATE pReNative, uint32_t off)
{
#ifdef RT_ARCH_AMD64
    return iemNativeEmitTbExitJcc<a_enmExitReason, a_fPostponedEfl>(pReNative, off, kIemNativeInstrCond_l);
#elif defined(RT_ARCH_ARM64)
    return iemNativeEmitTbExitJcc<a_enmExitReason, a_fPostponedEfl>(pReNative, off, kArmv8InstrCond_Lt);
#else
# error "Port me!"
#endif
}


/**
 * Emits a jump to the TB exit with @a a_enmExitReason on the condition _any_ of
 * the bits in @a fBits are set in @a iGprSrc.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTbExitIfAnyBitsSetInGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, uint64_t fBits)
{
    Assert(fBits); Assert(!RT_IS_POWER_OF_TWO(fBits));

    off = iemNativeEmitTestAnyBitsInGpr(pReNative, off, iGprSrc, fBits);
    return iemNativeEmitTbExitJnz<a_enmExitReason, a_fPostponedEfl>(pReNative, off);
}


#if 0 /* unused */
/**
 * Emits a jump to @a idxLabel on the condition _none_ of the bits in @a fBits
 * are set in @a iGprSrc.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTbExitIfNoBitsSetInGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, uint64_t fBits)
{
    Assert(fBits); Assert(!RT_IS_POWER_OF_TWO(fBits));

    off = iemNativeEmitTestAnyBitsInGpr(pReNative, off, iGprSrc, fBits);
    return iemNativeEmitJzTbExit<a_enmExitReason, a_fPostponedEfl>(pReNative, off);
}
#endif


#if 0 /* unused */
/**
 * Emits code that exits the TB with the given reason if @a iGprLeft and @a iGprRight
 * differs.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTbExitIfGprNotEqualGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprLeft, uint8_t iGprRight)
{
    off = iemNativeEmitCmpGprWithGpr(pReNative, off, iGprLeft, iGprRight);
    off = iemNativeEmitJnzTbExit<a_enmExitReason, a_fPostponedEfl>(pReNative, off);
    return off;
}
#endif


/**
 * Emits code that jumps to the given label if 32-bit @a iGprSrc differs from
 * @a uImm.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTbExitIfGpr32NotEqualImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, uint32_t uImm)
{
    off = iemNativeEmitCmpGpr32WithImm(pReNative, off, iGprSrc, uImm);
    off = iemNativeEmitTbExitJnz<a_enmExitReason, a_fPostponedEfl>(pReNative, off);
    return off;
}


/**
 * Emits code that exits the current TB if @a iGprSrc differs from @a uImm.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTbExitIfGprNotEqualImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, uint64_t uImm)
{
    off = iemNativeEmitCmpGprWithImm(pReNative, off, iGprSrc, uImm);
    off = iemNativeEmitTbExitJnz<a_enmExitReason, a_fPostponedEfl>(pReNative, off);
    return off;
}


/**
 * Emits code that exits the current TB with the given reason if 32-bit @a iGprSrc equals @a uImm.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTbExitIfGpr32EqualsImm(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, uint32_t uImm)
{
    off = iemNativeEmitCmpGpr32WithImm(pReNative, off, iGprSrc, uImm);
    off = iemNativeEmitTbExitJz<a_enmExitReason, a_fPostponedEfl>(pReNative, off);
    return off;
}


/**
 * Emits code to exit the current TB with the reason @a a_enmExitReason on the
 * condition that bit @a iBitNo _is_ _set_ in @a iGprSrc.
 *
 * @note On ARM64 the range is only +/-8191 instructions.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTbExitIfBitSetInGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, uint8_t iBitNo)
{
    AssertCompile(IEMNATIVELABELTYPE_IS_EXIT_REASON(a_enmExitReason));

#if defined(RT_ARCH_AMD64)
    Assert(iBitNo < 64);
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5);
    if (iBitNo < 8)
    {
        /* test Eb, imm8 */
        if (iGprSrc >= 4)
            pbCodeBuf[off++] = iGprSrc >= 8 ? X86_OP_REX_B : X86_OP_REX;
        pbCodeBuf[off++] = 0xf6;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, iGprSrc & 7);
        pbCodeBuf[off++] = (uint8_t)1 << iBitNo;
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
        off = iemNativeEmitTbExitJcc<a_enmExitReason, a_fPostponedEfl>(pReNative, off, kIemNativeInstrCond_ne);
    }
    else
    {
        /* bt Ev, imm8 */
        if (iBitNo >= 32)
            pbCodeBuf[off++] = X86_OP_REX_W | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
        else if (iGprSrc >= 8)
            pbCodeBuf[off++] = X86_OP_REX_B;
        pbCodeBuf[off++] = 0x0f;
        pbCodeBuf[off++] = 0xba;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 4, iGprSrc & 7);
        pbCodeBuf[off++] = iBitNo;
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
        off = iemNativeEmitTbExitJcc<a_enmExitReason, a_fPostponedEfl>(pReNative, off, kIemNativeInstrCond_c);
    }
    return off;

#elif defined(RT_ARCH_ARM64)
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_ONLY(pReNative, X86_EFL_STATUS_BITS);
    /** @todo Perhaps we should always apply the PostponedEfl code pattern here,
     *        it's the same number of instructions as the TST + B.CC stuff? */
# ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    if RT_CONSTEXPR_IF(a_fPostponedEfl)
        if (pReNative->PostponedEfl.fEFlags)
        {
            PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off,
                                                                     3 + IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS);
            pCodeBuf[off++] = Armv8A64MkInstrTbnz(1 /*l_NonPrimaryCodeStreamTarget*/, iGprSrc, iBitNo);
            uint32_t const offFixup = off;
            pCodeBuf[off++] = Armv8A64MkInstrB(0 /*l_PrimaryCodeStreamResume*/);
            /* l_NonPrimaryCodeStreamTarget: */
            off = iemNativeEmitTbExitEx<a_enmExitReason, false /*a_fActuallyExitingTb*/, true>(pReNative, pCodeBuf, off);
            /* l_PrimaryCodeStreamResume: */
            iemNativeFixupFixedJump(pReNative, offFixup, off);
            IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
            return off;
        }
# endif
    /* ARM64 doesn't have the necessary range to reach the per-chunk code, so
       we go via a local trampoline. */
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, a_enmExitReason, UINT32_MAX /*offWhere*/, 0 /*uData*/);
    return iemNativeEmitTestBitInGprAndJmpToLabelIfCc(pReNative, off, iGprSrc, iBitNo, idxLabel, true /*fJmpIfSet*/);
#else
# error "port me"
#endif
}


/**
 * Emits code that exits the current TB with @a a_enmExitReason if @a iGprSrc is
 * not zero.
 *
 * The operand size is given by @a f64Bit.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitTbExitIfGprIsNotZeroEx(PIEMRECOMPILERSTATE pReNative, PIEMNATIVEINSTR pCodeBuf, uint32_t off,
                                    uint8_t iGprSrc, bool f64Bit)
{
    AssertCompile(IEMNATIVELABELTYPE_IS_EXIT_REASON(a_enmExitReason));

#if defined(RT_ARCH_AMD64)
    /* test reg32,reg32  / test reg64,reg64 */
    if (f64Bit)
        pCodeBuf[off++] = X86_OP_REX_W | (iGprSrc < 8 ? 0 : X86_OP_REX_R | X86_OP_REX_B);
    else if (iGprSrc >= 8)
        pCodeBuf[off++] = X86_OP_REX_R | X86_OP_REX_B;
    pCodeBuf[off++] = 0x85;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprSrc & 7, iGprSrc & 7);

    /* jnz idxLabel  */
    return iemNativeEmitTbExitJccEx<a_enmExitReason, a_fPostponedEfl>(pReNative, pCodeBuf, off, kIemNativeInstrCond_ne);

#elif defined(RT_ARCH_ARM64)
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_ONLY(pReNative, X86_EFL_STATUS_BITS);
# ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    if RT_CONSTEXPR_IF(a_fPostponedEfl)
        if (pReNative->PostponedEfl.fEFlags)
        {
            pCodeBuf[off++] = Armv8A64MkInstrCbnz(1 /*l_NonPrimaryCodeStreamTarget*/, iGprSrc, f64Bit);
            uint32_t const offFixup = off;
            pCodeBuf[off++] = Armv8A64MkInstrB(0 /*l_PrimaryCodeStreamResume*/);
            /* l_NonPrimaryCodeStreamTarget: */
            off = iemNativeEmitTbExitEx<a_enmExitReason, false /*a_fActuallyExitingTb*/, true>(pReNative, pCodeBuf, off);
            /* l_PrimaryCodeStreamResume: */
            iemNativeFixupFixedJump(pReNative, offFixup, off);
            IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
            return off;
        }
# endif
    /* ARM64 doesn't have the necessary range to reach the per-chunk code, so
       we go via a local trampoline. */
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, a_enmExitReason, UINT32_MAX /*offWhere*/, 0 /*uData*/);
    return iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToLabelEx(pReNative, pCodeBuf, off, iGprSrc,
                                                                f64Bit, true /*fJmpIfNotZero*/, idxLabel);
#else
# error "port me"
#endif
}


/**
 * Emits code to exit the current TB with the given reason @a a_enmExitReason if
 * @a iGprSrc is not zero.
 *
 * The operand size is given by @a f64Bit.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTbExitIfGprIsNotZero(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, bool f64Bit)
{
#if defined(RT_ARCH_AMD64)
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3 + 6 + IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS);

#else
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3 + IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS);
#endif
    off = iemNativeEmitTbExitIfGprIsNotZeroEx<a_enmExitReason, a_fPostponedEfl>(pReNative, pCodeBuf, off, iGprSrc, f64Bit);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits code that exits the current TB with @a a_enmExitReason if @a iGprSrc is
 * zero.
 *
 * The operand size is given by @a f64Bit.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitTbExitIfGprIsZeroEx(PIEMRECOMPILERSTATE pReNative, PIEMNATIVEINSTR pCodeBuf, uint32_t off,
                                 uint8_t iGprSrc, bool f64Bit)
{
    AssertCompile(IEMNATIVELABELTYPE_IS_EXIT_REASON(a_enmExitReason));

#if defined(RT_ARCH_AMD64)
    /* test reg32,reg32  / test reg64,reg64 */
    if (f64Bit)
        pCodeBuf[off++] = X86_OP_REX_W | (iGprSrc < 8 ? 0 : X86_OP_REX_R | X86_OP_REX_B);
    else if (iGprSrc >= 8)
        pCodeBuf[off++] = X86_OP_REX_R | X86_OP_REX_B;
    pCodeBuf[off++] = 0x85;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprSrc & 7, iGprSrc & 7);

    /* jnz idxLabel  */
    return iemNativeEmitTbExitJccEx<a_enmExitReason, a_fPostponedEfl>(pReNative, pCodeBuf, off, kIemNativeInstrCond_e);

#elif defined(RT_ARCH_ARM64)
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_ONLY(pReNative, X86_EFL_STATUS_BITS);
# ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    if RT_CONSTEXPR_IF(a_fPostponedEfl)
        if (pReNative->PostponedEfl.fEFlags)
        {
            pCodeBuf[off++] = Armv8A64MkInstrCbz(1 /*l_NonPrimaryCodeStreamTarget*/, iGprSrc, f64Bit);
            uint32_t const offFixup = off;
            pCodeBuf[off++] = Armv8A64MkInstrB(0 /*l_PrimaryCodeStreamResume*/);
            /* l_NonPrimaryCodeStreamTarget: */
            off = iemNativeEmitTbExitEx<a_enmExitReason, false /*a_fActuallyExitingTb*/, true>(pReNative, pCodeBuf, off);
            /* l_PrimaryCodeStreamResume: */
            iemNativeFixupFixedJump(pReNative, offFixup, off);
            IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
            return off;
        }
# endif
    /* ARM64 doesn't have the necessary range to reach the per-chunk code, so
       we go via a local trampoline. */
    uint32_t const idxLabel = iemNativeLabelCreate(pReNative, a_enmExitReason, UINT32_MAX /*offWhere*/, 0 /*uData*/);
    return iemNativeEmitTestIfGprIsZeroOrNotZeroAndJmpToLabelEx(pReNative, pCodeBuf, off, iGprSrc,
                                                                f64Bit, false /*fJmpIfNotZero*/, idxLabel);
#else
# error "port me"
#endif
}


/**
 * Emits code to exit the current TB with the given reason @a a_enmExitReason if @a iGprSrc is zero.
 *
 * The operand size is given by @a f64Bit.
 */
template<IEMNATIVELABELTYPE const a_enmExitReason, bool const a_fPostponedEfl = true>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitTbExitIfGprIsZero(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprSrc, bool f64Bit)
{
#if defined(RT_ARCH_AMD64)
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3 + 6 + IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS);

#else
    PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3 + IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS);
#endif
    off = iemNativeEmitTbExitIfGprIsZeroEx<a_enmExitReason, a_fPostponedEfl>(pReNative, pCodeBuf, off, iGprSrc, f64Bit);
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}



/*********************************************************************************************************************************
*   SIMD helpers.                                                                                                                *
*********************************************************************************************************************************/

/**
 * Emits code to load the variable address into an argument GPR.
 *
 * This is a special variant intended for SIMD variables only and only called
 * by the TLB miss path in the memory fetch/store code because there we pass
 * the value by reference and need both the register and stack depending on which
 * path is taken (TLB hit vs. miss).
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitLoadArgGregWithSimdVarAddrForMemAccess(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxRegArg, uint8_t idxVar,
                                                    bool fSyncRegWithStack = true)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    PIEMNATIVEVAR const pVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)];
    AssertStmt(   pVar->enmKind == kIemNativeVarKind_Invalid
               || pVar->enmKind == kIemNativeVarKind_Stack,
               IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));
    AssertStmt(pVar->fSimdReg,
               IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));
    Assert(   pVar->idxStackSlot != UINT8_MAX
           && pVar->idxReg != UINT8_MAX);

    uint8_t const idxStackSlot   = iemNativeVarGetStackSlot(pReNative, idxVar);
    int32_t const offBpDisp      = iemNativeStackCalcBpDisp(idxStackSlot);

    uint8_t const idxRegVar      = pVar->idxReg;
    Assert(idxRegVar < RT_ELEMENTS(pReNative->Core.aHstSimdRegs));
    Assert(pVar->cbVar == sizeof(RTUINT128U) || pVar->cbVar == sizeof(RTUINT256U));

    if (fSyncRegWithStack)
    {
        if (pVar->cbVar == sizeof(RTUINT128U))
            off = iemNativeEmitStoreVecRegByBpU128(pReNative, off, offBpDisp, idxRegVar);
        else
            off = iemNativeEmitStoreVecRegByBpU256(pReNative, off, offBpDisp, idxRegVar);
    }

    return iemNativeEmitLeaGprByBp(pReNative, off, idxRegArg, offBpDisp);
}


/**
 * Emits code to sync the host SIMD register assigned to the given SIMD variable.
 *
 * This is a special helper and only called
 * by the TLB miss path in the memory fetch/store code because there we pass
 * the value by reference and need to sync the value on the stack with the assigned host register
 * after a TLB miss where the value ends up on the stack.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitSimdVarSyncStackToRegister(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    PIEMNATIVEVAR const pVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)];
    AssertStmt(   pVar->enmKind == kIemNativeVarKind_Invalid
               || pVar->enmKind == kIemNativeVarKind_Stack,
               IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));
    AssertStmt(pVar->fSimdReg,
               IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));
    Assert(   pVar->idxStackSlot != UINT8_MAX
           && pVar->idxReg != UINT8_MAX);

    uint8_t const idxStackSlot   = iemNativeVarGetStackSlot(pReNative, idxVar);
    int32_t const offBpDisp      = iemNativeStackCalcBpDisp(idxStackSlot);

    uint8_t const idxRegVar      = pVar->idxReg;
    Assert(idxRegVar < RT_ELEMENTS(pReNative->Core.aHstSimdRegs));
    Assert(pVar->cbVar == sizeof(RTUINT128U) || pVar->cbVar == sizeof(RTUINT256U));

    if (pVar->cbVar == sizeof(RTUINT128U))
        off = iemNativeEmitLoadVecRegByBpU128(pReNative, off, idxRegVar, offBpDisp);
    else
        off = iemNativeEmitLoadVecRegByBpU256(pReNative, off, idxRegVar, offBpDisp);

    return off;
}


/**
 * Emits a gprdst = ~gprsrc store.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitInvBitsGprEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc, bool f64Bit = true)
{
#ifdef RT_ARCH_AMD64
    if (iGprDst != iGprSrc)
    {
        /* mov gprdst, gprsrc. */
        if (f64Bit)
            off = iemNativeEmitLoadGprFromGprEx(pCodeBuf, off, iGprDst, iGprSrc);
        else
            off = iemNativeEmitLoadGprFromGpr32Ex(pCodeBuf, off, iGprDst, iGprSrc); /* Bits 32:63 are cleared. */
    }

    /* not gprdst */
    if (f64Bit || iGprDst >= 8)
        pCodeBuf[off++] =    (f64Bit ? X86_OP_REX_W : 0)
                           | (iGprDst >= 8 ? X86_OP_REX_B : 0);
    pCodeBuf[off++] = 0xf7;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 2, iGprDst & 7);
#elif defined(RT_ARCH_ARM64)
    pCodeBuf[off++] = Armv8A64MkInstrOrn(iGprDst, ARMV8_A64_REG_XZR, iGprSrc, f64Bit);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a gprdst = ~gprsrc store.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitInvBitsGpr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iGprSrc, bool f64Bit = true)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitInvBitsGprEx(iemNativeInstrBufEnsure(pReNative, off, 9), off, iGprDst, iGprSrc, f64Bit);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitInvBitsGprEx(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iGprSrc, f64Bit);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a 128-bit vector register store to a VCpu value.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreVecRegToVCpuLowU128Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecReg, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* movdqa mem128, reg128 */ /* ASSUMING an aligned location here. */
    pCodeBuf[off++] = 0x66;
    if (iVecReg >= 8)
        pCodeBuf[off++] = X86_OP_REX_R;
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0x7f;
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, iVecReg, offVCpu);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdStEx(pCodeBuf, off, iVecReg, offVCpu, kArmv8A64InstrLdStType_St_Vr_128, sizeof(RTUINT128U));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 128-bit vector register load of a VCpu value.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreVecRegToVCpuLowU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecReg, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdStoreVecRegToVCpuLowU128Ex(iemNativeInstrBufEnsure(pReNative, off, 9), off, iVecReg, offVCpu);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdStoreVecRegToVCpuLowU128Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecReg, offVCpu);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a high 128-bit vector register store to a VCpu value.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreVecRegToVCpuHighU128Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecReg, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* vextracti128 mem128, reg128, 1 */ /* ASSUMES AVX2 support. */
    pCodeBuf[off++] = X86_OP_VEX3;
    if (iVecReg >= 8)
        pCodeBuf[off++] = 0x63;
    else
        pCodeBuf[off++] = 0xe3;
    pCodeBuf[off++] = 0x7d;
    pCodeBuf[off++] = 0x39;
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, iVecReg, offVCpu);
    pCodeBuf[off++] = 0x01; /* Immediate */
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdStEx(pCodeBuf, off, iVecReg, offVCpu, kArmv8A64InstrLdStType_St_Vr_128, sizeof(RTUINT128U));
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a high 128-bit vector register load of a VCpu value.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreVecRegToVCpuHighU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecReg, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdStoreVecRegToVCpuHighU128Ex(iemNativeInstrBufEnsure(pReNative, off, 10), off, iVecReg, offVCpu);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecReg & 0x1));
    off = iemNativeEmitSimdStoreVecRegToVCpuHighU128Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecReg + 1, offVCpu);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a 128-bit vector register load of a VCpu value.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadVecRegFromVCpuLowU128Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecReg, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* movdqa reg128, mem128 */ /* ASSUMING an aligned location here. */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iVecReg >= 8)
        pCodeBuf[off++] = X86_OP_REX_R;
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0x6f;
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, iVecReg, offVCpu);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdStEx(pCodeBuf, off, iVecReg, offVCpu, kArmv8A64InstrLdStType_Ld_Vr_128, sizeof(RTUINT128U));

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 128-bit vector register load of a VCpu value.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadVecRegFromVCpuLowU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecReg, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdLoadVecRegFromVCpuLowU128Ex(iemNativeInstrBufEnsure(pReNative, off, 9), off, iVecReg, offVCpu);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdLoadVecRegFromVCpuLowU128Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecReg, offVCpu);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a 128-bit vector register load of a VCpu value.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadVecRegFromVCpuHighU128Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecReg, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    /* vinserti128 ymm, ymm, mem128, 1. */ /* ASSUMES AVX2 support */
    pCodeBuf[off++] = X86_OP_VEX3;
    if (iVecReg >= 8)
        pCodeBuf[off++] = 0x63;
    else
        pCodeBuf[off++] = 0xe3;
    pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE(false, iVecReg, true, X86_OP_VEX3_BYTE2_P_066H);
    pCodeBuf[off++] = 0x38;
    off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, iVecReg, offVCpu);
    pCodeBuf[off++] = 0x01; /* Immediate */
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitGprByVCpuLdStEx(pCodeBuf, off, iVecReg, offVCpu, kArmv8A64InstrLdStType_Ld_Vr_128, sizeof(RTUINT128U));
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a 128-bit vector register load of a VCpu value.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadVecRegFromVCpuHighU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecReg, uint32_t offVCpu)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdLoadVecRegFromVCpuHighU128Ex(iemNativeInstrBufEnsure(pReNative, off, 10), off, iVecReg, offVCpu);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecReg & 0x1));
    off = iemNativeEmitSimdLoadVecRegFromVCpuHighU128Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecReg + 1, offVCpu);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst = vecsrc load.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdLoadVecRegFromVecRegU128Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iVecRegSrc)
{
#ifdef RT_ARCH_AMD64
    /* movdqu vecdst, vecsrc */
    pCodeBuf[off++] = 0xf3;

    if ((iVecRegDst | iVecRegSrc) >= 8)
        pCodeBuf[off++] = iVecRegDst < 8  ? X86_OP_REX_B
                        : iVecRegSrc >= 8 ? X86_OP_REX_R | X86_OP_REX_B
                        :                   X86_OP_REX_R;
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0x6f;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iVecRegSrc & 7);

#elif defined(RT_ARCH_ARM64)
    /* mov dst, src;   alias for: orr dst, src, src */
    pCodeBuf[off++] = Armv8A64MkVecInstrOrr(iVecRegDst, iVecRegSrc, iVecRegSrc);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst = vecsrc load, 128-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadVecRegFromVecRegU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iVecRegSrc)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdLoadVecRegFromVecRegU128Ex(iemNativeInstrBufEnsure(pReNative, off, 5), off, iVecRegDst, iVecRegSrc);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdLoadVecRegFromVecRegU128Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecRegDst, iVecRegSrc);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst[128:255] = vecsrc[128:255] load.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadVecRegHighU128FromVecRegHighU128Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iVecRegSrc)
{
#ifdef RT_ARCH_AMD64
    /* vperm2i128 dst, dst, src, 0x30. */ /* ASSUMES AVX2 support */
    pCodeBuf[off++] = X86_OP_VEX3;
    pCodeBuf[off++] = X86_OP_VEX3_BYTE1_MAKE(0x3, iVecRegSrc >= 8, false, iVecRegDst >= 8);
    pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE(false, iVecRegDst, true, X86_OP_VEX3_BYTE2_P_066H);
    pCodeBuf[off++] = 0x46;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iVecRegSrc & 7);
    pCodeBuf[off++] = 0x30; /* Immediate, this will leave the low 128 bits of dst untouched and move the high 128 bits from src to dst. */

#elif defined(RT_ARCH_ARM64)
    RT_NOREF(pCodeBuf, iVecRegDst, iVecRegSrc);

    /* Should never be called because we can just use iemNativeEmitSimdLoadVecRegFromVecRegU128(). */
# ifdef IEM_WITH_THROW_CATCH
    AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
    AssertReleaseFailedStmt(off = UINT32_MAX);
# endif
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[128:255] = vecsrc[128:255] load, high 128-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadVecRegHighU128FromVecRegHighU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iVecRegSrc)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdLoadVecRegHighU128FromVecRegHighU128Ex(iemNativeInstrBufEnsure(pReNative, off, 5), off, iVecRegDst, iVecRegSrc);
#elif defined(RT_ARCH_ARM64)
    Assert(!(iVecRegDst & 0x1) && !(iVecRegSrc & 0x1));
    off = iemNativeEmitSimdLoadVecRegFromVecRegU128Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecRegDst + 1, iVecRegSrc + 1);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst[0:127] = vecsrc[128:255] load.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadVecRegLowU128FromVecRegHighU128Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iVecRegSrc)
{
#ifdef RT_ARCH_AMD64
    /* vextracti128 dst, src, 1. */ /* ASSUMES AVX2 support */
    pCodeBuf[off++] = X86_OP_VEX3;
    pCodeBuf[off++] = X86_OP_VEX3_BYTE1_MAKE(0x3, iVecRegDst >= 8, false, iVecRegSrc >= 8);
    pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE_NO_VVVV(false, true, X86_OP_VEX3_BYTE2_P_066H);
    pCodeBuf[off++] = 0x39;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegSrc & 7, iVecRegDst & 7);
    pCodeBuf[off++] = 0x1;

#elif defined(RT_ARCH_ARM64)
    RT_NOREF(pCodeBuf, iVecRegDst, iVecRegSrc);

    /* Should never be called because we can just use iemNativeEmitSimdLoadVecRegFromVecRegU128Ex(). */
# ifdef IEM_WITH_THROW_CATCH
    AssertFailedStmt(IEMNATIVE_DO_LONGJMP(NULL, VERR_IEM_IPE_9));
# else
    AssertReleaseFailedStmt(off = UINT32_MAX);
# endif
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[0:127] = vecsrc[128:255] load, high 128-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadVecRegLowU128FromVecRegHighU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iVecRegSrc)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdLoadVecRegLowU128FromVecRegHighU128Ex(iemNativeInstrBufEnsure(pReNative, off, 6), off, iVecRegDst, iVecRegSrc);
#elif defined(RT_ARCH_ARM64)
    Assert(!(iVecRegDst & 0x1) && !(iVecRegSrc & 0x1));
    off = iemNativeEmitSimdLoadVecRegFromVecRegU128Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecRegDst, iVecRegSrc + 1);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst = vecsrc load, 256-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadVecRegFromVecRegU256(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iVecRegSrc)
{
#ifdef RT_ARCH_AMD64
    /* vmovdqa ymm, ymm */
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5);
    if (iVecRegDst >= 8 && iVecRegSrc >= 8)
    {
        pbCodeBuf[off++] = X86_OP_VEX3;
        pbCodeBuf[off++] = 0x41;
        pbCodeBuf[off++] = 0x7d;
        pbCodeBuf[off++] = 0x6f;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iVecRegSrc & 7);
    }
    else
    {
        pbCodeBuf[off++] = X86_OP_VEX2;
        pbCodeBuf[off++] = (iVecRegSrc >= 8 || iVecRegDst >= 8) ? 0x7d : 0xfd;
        pbCodeBuf[off++] = iVecRegSrc >= 8 ? 0x7f : 0x6f;
        pbCodeBuf[off++] =   iVecRegSrc >= 8
                           ? X86_MODRM_MAKE(X86_MOD_REG, iVecRegSrc & 7, iVecRegDst & 7)
                           : X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iVecRegSrc & 7);
    }
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecRegDst & 0x1)); Assert(!(iVecRegSrc & 0x1));
    off = iemNativeEmitSimdLoadVecRegFromVecRegU128(pReNative, off, iVecRegDst,     iVecRegSrc    );
    off = iemNativeEmitSimdLoadVecRegFromVecRegU128(pReNative, off, iVecRegDst + 1, iVecRegSrc + 1);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst = vecsrc load.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdLoadVecRegHighU128FromVecRegLowU128Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iVecRegSrc)
{
#ifdef RT_ARCH_AMD64
    /* vinserti128 dst, dst, src, 1. */ /* ASSUMES AVX2 support */
    pCodeBuf[off++] = X86_OP_VEX3;
    pCodeBuf[off++] = X86_OP_VEX3_BYTE1_MAKE(0x3, iVecRegSrc >= 8, false, iVecRegDst >= 8);
    pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE(false, iVecRegDst, true, X86_OP_VEX3_BYTE2_P_066H);
    pCodeBuf[off++] = 0x38;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iVecRegSrc & 7);
    pCodeBuf[off++] = 0x01; /* Immediate */

#elif defined(RT_ARCH_ARM64)
    Assert(!(iVecRegDst & 0x1) && !(iVecRegSrc & 0x1));
    /* mov dst, src;   alias for: orr dst, src, src */
    pCodeBuf[off++] = Armv8A64MkVecInstrOrr(iVecRegDst + 1, iVecRegSrc, iVecRegSrc);

#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[128:255] = vecsrc[0:127] load, 128-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadVecRegHighU128FromVecRegLowU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iVecRegSrc)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdLoadVecRegHighU128FromVecRegLowU128Ex(iemNativeInstrBufEnsure(pReNative, off, 6), off, iVecRegDst, iVecRegSrc);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdLoadVecRegHighU128FromVecRegLowU128Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecRegDst, iVecRegSrc);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a gprdst = vecsrc[x] load, 64-bit.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdLoadGprFromVecRegU64Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iVecRegSrc, uint8_t iQWord)
{
#ifdef RT_ARCH_AMD64
    if (iQWord >= 2)
    {
        /*
         * vpextrq doesn't work on the upper 128-bits.
         * So we use the following sequence:
         *     vextracti128 vectmp0, vecsrc, 1
         *     pextrq       gpr, vectmp0, #(iQWord - 2)
         */
        /* vextracti128 */
        pCodeBuf[off++] = X86_OP_VEX3;
        pCodeBuf[off++] = X86_OP_VEX3_BYTE1_MAKE(0x3, IEMNATIVE_SIMD_REG_FIXED_TMP0 >= 8, false, iVecRegSrc >= 8);
        pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE_NO_VVVV(false, true, X86_OP_VEX3_BYTE2_P_066H);
        pCodeBuf[off++] = 0x39;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegSrc & 7, IEMNATIVE_SIMD_REG_FIXED_TMP0 & 7);
        pCodeBuf[off++] = 0x1;

        /* pextrq */
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
        pCodeBuf[off++] =   X86_OP_REX_W
                          | (IEMNATIVE_SIMD_REG_FIXED_TMP0 < 8 ? 0 : X86_OP_REX_R)
                          | (iGprDst < 8 ? 0 : X86_OP_REX_B);
        pCodeBuf[off++] = 0x0f;
        pCodeBuf[off++] = 0x3a;
        pCodeBuf[off++] = 0x16;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, IEMNATIVE_SIMD_REG_FIXED_TMP0 & 7, iGprDst & 7);
        pCodeBuf[off++] = iQWord - 2;
    }
    else
    {
        /* pextrq gpr, vecsrc, #iQWord (ASSUMES SSE4.1). */
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
        pCodeBuf[off++] =   X86_OP_REX_W
                          | (iVecRegSrc < 8 ? 0 : X86_OP_REX_R)
                          | (iGprDst < 8 ? 0 : X86_OP_REX_B);
        pCodeBuf[off++] = 0x0f;
        pCodeBuf[off++] = 0x3a;
        pCodeBuf[off++] = 0x16;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegSrc & 7, iGprDst & 7);
        pCodeBuf[off++] = iQWord;
    }
#elif defined(RT_ARCH_ARM64)
    /* umov gprdst, vecsrc[iQWord] */
    pCodeBuf[off++] = Armv8A64MkVecInstrUmov(iGprDst, iVecRegSrc, iQWord, kArmv8InstrUmovInsSz_U64);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a gprdst = vecsrc[x] load, 64-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadGprFromVecRegU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iVecRegSrc, uint8_t iQWord)
{
    Assert(iQWord <= 3);

#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdLoadGprFromVecRegU64Ex(iemNativeInstrBufEnsure(pReNative, off, 13), off, iGprDst, iVecRegSrc, iQWord);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecRegSrc & 0x1));
    /* Need to access the "high" 128-bit vector register. */
    if (iQWord >= 2)
        off = iemNativeEmitSimdLoadGprFromVecRegU64Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iVecRegSrc + 1, iQWord - 2);
    else
        off = iemNativeEmitSimdLoadGprFromVecRegU64Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iVecRegSrc,     iQWord);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a gprdst = vecsrc[x] load, 32-bit.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdLoadGprFromVecRegU32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iVecRegSrc, uint8_t iDWord)
{
#ifdef RT_ARCH_AMD64
    if (iDWord >= 4)
    {
        /*
         * vpextrd doesn't work on the upper 128-bits.
         * So we use the following sequence:
         *     vextracti128 vectmp0, vecsrc, 1
         *     pextrd       gpr, vectmp0, #(iDWord - 4)
         */
        /* vextracti128 */
        pCodeBuf[off++] = X86_OP_VEX3;
        pCodeBuf[off++] = X86_OP_VEX3_BYTE1_MAKE(0x3, IEMNATIVE_SIMD_REG_FIXED_TMP0 >= 8, false, iVecRegSrc >= 8);
        pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE_NO_VVVV(false, true, X86_OP_VEX3_BYTE2_P_066H);
        pCodeBuf[off++] = 0x39;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegSrc & 7, IEMNATIVE_SIMD_REG_FIXED_TMP0 & 7);
        pCodeBuf[off++] = 0x1;

        /* pextrd gpr, vecsrc, #iDWord (ASSUMES SSE4.1). */
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
        if (iGprDst >= 8 || IEMNATIVE_SIMD_REG_FIXED_TMP0 >= 8)
            pCodeBuf[off++] =   (IEMNATIVE_SIMD_REG_FIXED_TMP0 < 8 ? 0 : X86_OP_REX_R)
                              | (iGprDst < 8 ? 0 : X86_OP_REX_B);
        pCodeBuf[off++] = 0x0f;
        pCodeBuf[off++] = 0x3a;
        pCodeBuf[off++] = 0x16;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, IEMNATIVE_SIMD_REG_FIXED_TMP0 & 7, iGprDst & 7);
        pCodeBuf[off++] = iDWord - 4;
    }
    else
    {
        /* pextrd gpr, vecsrc, #iDWord (ASSUMES SSE4.1). */
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
        if (iGprDst >= 8 || iVecRegSrc >= 8)
            pCodeBuf[off++] =   (iVecRegSrc < 8 ? 0 : X86_OP_REX_R)
                              | (iGprDst < 8 ? 0 : X86_OP_REX_B);
        pCodeBuf[off++] = 0x0f;
        pCodeBuf[off++] = 0x3a;
        pCodeBuf[off++] = 0x16;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegSrc & 7, iGprDst & 7);
        pCodeBuf[off++] = iDWord;
    }
#elif defined(RT_ARCH_ARM64)
    Assert(iDWord < 4);

    /* umov gprdst, vecsrc[iDWord] */
    pCodeBuf[off++] = Armv8A64MkVecInstrUmov(iGprDst, iVecRegSrc, iDWord, kArmv8InstrUmovInsSz_U32, false /*fDst64Bit*/);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a gprdst = vecsrc[x] load, 32-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadGprFromVecRegU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iVecRegSrc, uint8_t iDWord)
{
    Assert(iDWord <= 7);

#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdLoadGprFromVecRegU32Ex(iemNativeInstrBufEnsure(pReNative, off, 15), off, iGprDst, iVecRegSrc, iDWord);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecRegSrc & 0x1));
    /* Need to access the "high" 128-bit vector register. */
    if (iDWord >= 4)
        off = iemNativeEmitSimdLoadGprFromVecRegU32Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iVecRegSrc + 1, iDWord - 4);
    else
        off = iemNativeEmitSimdLoadGprFromVecRegU32Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iVecRegSrc, iDWord);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a gprdst = vecsrc[x] load, 16-bit.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdLoadGprFromVecRegU16Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iVecRegSrc, uint8_t iWord)
{
#ifdef RT_ARCH_AMD64
    if (iWord >= 8)
    {
        /** @todo Currently not used. */
        AssertReleaseFailed();
    }
    else
    {
        /* pextrw gpr, vecsrc, #iWord */
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
        if (iGprDst >= 8 || iVecRegSrc >= 8)
            pCodeBuf[off++] =   (iGprDst < 8 ? 0 : X86_OP_REX_R)
                              | (iVecRegSrc < 8 ? 0 : X86_OP_REX_B);
        pCodeBuf[off++] = 0x0f;
        pCodeBuf[off++] = 0xc5;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iGprDst & 7, iVecRegSrc & 7);
        pCodeBuf[off++] = iWord;
    }
#elif defined(RT_ARCH_ARM64)
    /* umov gprdst, vecsrc[iWord] */
    pCodeBuf[off++] = Armv8A64MkVecInstrUmov(iGprDst, iVecRegSrc, iWord, kArmv8InstrUmovInsSz_U16, false /*fDst64Bit*/);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a gprdst = vecsrc[x] load, 16-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadGprFromVecRegU16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iVecRegSrc, uint8_t iWord)
{
    Assert(iWord <= 16);

#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdLoadGprFromVecRegU16Ex(iemNativeInstrBufEnsure(pReNative, off, 6), off, iGprDst, iVecRegSrc, iWord);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecRegSrc & 0x1));
    /* Need to access the "high" 128-bit vector register. */
    if (iWord >= 8)
        off = iemNativeEmitSimdLoadGprFromVecRegU16Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iVecRegSrc + 1, iWord - 8);
    else
        off = iemNativeEmitSimdLoadGprFromVecRegU16Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iVecRegSrc, iWord);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a gprdst = vecsrc[x] load, 8-bit.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdLoadGprFromVecRegU8Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iGprDst, uint8_t iVecRegSrc, uint8_t iByte)
{
#ifdef RT_ARCH_AMD64
    if (iByte >= 16)
    {
        /** @todo Currently not used. */
        AssertReleaseFailed();
    }
    else
    {
        /* pextrb gpr, vecsrc, #iByte */
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
        if (iGprDst >= 8 || iVecRegSrc >= 8)
            pCodeBuf[off++] =   (iVecRegSrc < 8 ? 0 : X86_OP_REX_R)
                              | (iGprDst < 8 ? 0 : X86_OP_REX_B);
        pCodeBuf[off++] = 0x0f;
        pCodeBuf[off++] = 0x3a;
        pCodeBuf[off++] = 0x14;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegSrc & 7, iGprDst & 7);
        pCodeBuf[off++] = iByte;
    }
#elif defined(RT_ARCH_ARM64)
    /* umov gprdst, vecsrc[iByte] */
    pCodeBuf[off++] = Armv8A64MkVecInstrUmov(iGprDst, iVecRegSrc, iByte, kArmv8InstrUmovInsSz_U8, false /*fDst64Bit*/);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a gprdst = vecsrc[x] load, 8-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdLoadGprFromVecRegU8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGprDst, uint8_t iVecRegSrc, uint8_t iByte)
{
    Assert(iByte <= 32);

#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdLoadGprFromVecRegU8Ex(iemNativeInstrBufEnsure(pReNative, off, 7), off, iGprDst, iVecRegSrc, iByte);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecRegSrc & 0x1));
    /* Need to access the "high" 128-bit vector register. */
    if (iByte >= 16)
        off = iemNativeEmitSimdLoadGprFromVecRegU8Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iVecRegSrc + 1, iByte - 16);
    else
        off = iemNativeEmitSimdLoadGprFromVecRegU8Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iGprDst, iVecRegSrc, iByte);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst[x] = gprsrc store, 64-bit.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdStoreGprToVecRegU64Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, uint8_t iQWord)
{
#ifdef RT_ARCH_AMD64
    if (iQWord >= 2)
    {
        /*
         * vpinsrq doesn't work on the upper 128-bits.
         * So we use the following sequence:
         *     vextracti128 vectmp0, vecdst, 1
         *     pinsrq       vectmp0, gpr, #(iQWord - 2)
         *     vinserti128  vecdst, vectmp0, 1
         */
        /* vextracti128 */
        pCodeBuf[off++] = X86_OP_VEX3;
        pCodeBuf[off++] = X86_OP_VEX3_BYTE1_MAKE(0x3, IEMNATIVE_SIMD_REG_FIXED_TMP0 >= 8, false, iVecRegDst >= 8);
        pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE_NO_VVVV(false, true, X86_OP_VEX3_BYTE2_P_066H);
        pCodeBuf[off++] = 0x39;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, IEMNATIVE_SIMD_REG_FIXED_TMP0 & 7);
        pCodeBuf[off++] = 0x1;

        /* pinsrq */
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
        pCodeBuf[off++] =   X86_OP_REX_W
                          | (IEMNATIVE_SIMD_REG_FIXED_TMP0 < 8 ? 0 : X86_OP_REX_R)
                          | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
        pCodeBuf[off++] = 0x0f;
        pCodeBuf[off++] = 0x3a;
        pCodeBuf[off++] = 0x22;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, IEMNATIVE_SIMD_REG_FIXED_TMP0 & 7, iGprSrc & 7);
        pCodeBuf[off++] = iQWord - 2;

        /* vinserti128 */
        pCodeBuf[off++] = X86_OP_VEX3;
        pCodeBuf[off++] = X86_OP_VEX3_BYTE1_MAKE(0x3, IEMNATIVE_SIMD_REG_FIXED_TMP0 >= 8, false, iVecRegDst >= 8);
        pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE(false, iVecRegDst, true, X86_OP_VEX3_BYTE2_P_066H);
        pCodeBuf[off++] = 0x38;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, IEMNATIVE_SIMD_REG_FIXED_TMP0 & 7);
        pCodeBuf[off++] = 0x01; /* Immediate */
    }
    else
    {
        /* pinsrq vecsrc, gpr, #iQWord (ASSUMES SSE4.1). */
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
        pCodeBuf[off++] =   X86_OP_REX_W
                          | (iVecRegDst < 8 ? 0 : X86_OP_REX_R)
                          | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
        pCodeBuf[off++] = 0x0f;
        pCodeBuf[off++] = 0x3a;
        pCodeBuf[off++] = 0x22;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iGprSrc & 7);
        pCodeBuf[off++] = iQWord;
    }
#elif defined(RT_ARCH_ARM64)
    /* ins vecsrc[iQWord], gpr */
    pCodeBuf[off++] = Armv8A64MkVecInstrIns(iVecRegDst, iGprSrc, iQWord, kArmv8InstrUmovInsSz_U64);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[x] = gprsrc store, 64-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreGprToVecRegU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, uint8_t iQWord)
{
    Assert(iQWord <= 3);

#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdStoreGprToVecRegU64Ex(iemNativeInstrBufEnsure(pReNative, off, 19), off, iVecRegDst, iGprSrc, iQWord);
#elif defined(RT_ARCH_ARM64)
    Assert(!(iVecRegDst & 0x1));
    if (iQWord >= 2)
        off = iemNativeEmitSimdStoreGprToVecRegU64Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecRegDst + 1, iGprSrc, iQWord - 2);
    else
        off = iemNativeEmitSimdStoreGprToVecRegU64Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecRegDst,     iGprSrc, iQWord);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst[x] = gprsrc store, 32-bit.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdStoreGprToVecRegU32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, uint8_t iDWord)
{
#ifdef RT_ARCH_AMD64
    if (iDWord >= 4)
    {
        /*
         * vpinsrq doesn't work on the upper 128-bits.
         * So we use the following sequence:
         *     vextracti128 vectmp0, vecdst, 1
         *     pinsrd       vectmp0, gpr, #(iDword - 4)
         *     vinserti128  vecdst, vectmp0, 1
         */
        /* vextracti128 */
        pCodeBuf[off++] = X86_OP_VEX3;
        pCodeBuf[off++] = X86_OP_VEX3_BYTE1_MAKE(0x3, IEMNATIVE_SIMD_REG_FIXED_TMP0 >= 8, false, iVecRegDst >= 8);
        pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE_NO_VVVV(false, true, X86_OP_VEX3_BYTE2_P_066H);
        pCodeBuf[off++] = 0x39;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, IEMNATIVE_SIMD_REG_FIXED_TMP0 & 7);
        pCodeBuf[off++] = 0x1;

        /* pinsrd */
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
        if (IEMNATIVE_SIMD_REG_FIXED_TMP0 >= 8 || iGprSrc >= 8)
            pCodeBuf[off++] =   (IEMNATIVE_SIMD_REG_FIXED_TMP0 < 8 ? 0 : X86_OP_REX_R)
                              | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
        pCodeBuf[off++] = 0x0f;
        pCodeBuf[off++] = 0x3a;
        pCodeBuf[off++] = 0x22;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, IEMNATIVE_SIMD_REG_FIXED_TMP0 & 7, iGprSrc & 7);
        pCodeBuf[off++] = iDWord - 4;

        /* vinserti128 */
        pCodeBuf[off++] = X86_OP_VEX3;
        pCodeBuf[off++] = X86_OP_VEX3_BYTE1_MAKE(0x3, IEMNATIVE_SIMD_REG_FIXED_TMP0 >= 8, false, iVecRegDst >= 8);
        pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE(false, iVecRegDst, true, X86_OP_VEX3_BYTE2_P_066H);
        pCodeBuf[off++] = 0x38;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, IEMNATIVE_SIMD_REG_FIXED_TMP0 & 7);
        pCodeBuf[off++] = 0x01; /* Immediate */
    }
    else
    {
        /* pinsrd vecsrc, gpr, #iDWord (ASSUMES SSE4.1). */
        pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
        if (iVecRegDst >= 8 || iGprSrc >= 8)
            pCodeBuf[off++] =   (iVecRegDst < 8 ? 0 : X86_OP_REX_R)
                              | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
        pCodeBuf[off++] = 0x0f;
        pCodeBuf[off++] = 0x3a;
        pCodeBuf[off++] = 0x22;
        pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iGprSrc & 7);
        pCodeBuf[off++] = iDWord;
    }
#elif defined(RT_ARCH_ARM64)
    /* ins vecsrc[iDWord], gpr */
    pCodeBuf[off++] = Armv8A64MkVecInstrIns(iVecRegDst, iGprSrc, iDWord, kArmv8InstrUmovInsSz_U32);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[x] = gprsrc store, 64-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreGprToVecRegU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, uint8_t iDWord)
{
    Assert(iDWord <= 7);

#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdStoreGprToVecRegU32Ex(iemNativeInstrBufEnsure(pReNative, off, 19), off, iVecRegDst, iGprSrc, iDWord);
#elif defined(RT_ARCH_ARM64)
    Assert(!(iVecRegDst & 0x1));
    if (iDWord >= 4)
        off = iemNativeEmitSimdStoreGprToVecRegU32Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecRegDst + 1, iGprSrc, iDWord - 4);
    else
        off = iemNativeEmitSimdStoreGprToVecRegU32Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecRegDst,     iGprSrc, iDWord);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst[x] = gprsrc store, 16-bit.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdStoreGprToVecRegU16Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, uint8_t iWord)
{
#ifdef RT_ARCH_AMD64
    /* pinsrw vecsrc, gpr, #iWord. */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iVecRegDst >= 8 || iGprSrc >= 8)
        pCodeBuf[off++] =   (iVecRegDst < 8 ? 0 : X86_OP_REX_R)
                          | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xc4;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iGprSrc & 7);
    pCodeBuf[off++] = iWord;
#elif defined(RT_ARCH_ARM64)
    /* ins vecsrc[iWord], gpr */
    pCodeBuf[off++] = Armv8A64MkVecInstrIns(iVecRegDst, iGprSrc, iWord, kArmv8InstrUmovInsSz_U16);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[x] = gprsrc store, 16-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreGprToVecRegU16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, uint8_t iWord)
{
    Assert(iWord <= 15);

#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdStoreGprToVecRegU16Ex(iemNativeInstrBufEnsure(pReNative, off, 6), off, iVecRegDst, iGprSrc, iWord);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdStoreGprToVecRegU16Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecRegDst, iGprSrc, iWord);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst[x] = gprsrc store, 8-bit.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdStoreGprToVecRegU8Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, uint8_t iByte)
{
#ifdef RT_ARCH_AMD64
    /* pinsrb vecsrc, gpr, #iByte (ASSUMES SSE4.1). */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iVecRegDst >= 8 || iGprSrc >= 8)
        pCodeBuf[off++] =   (iVecRegDst < 8 ? 0 : X86_OP_REX_R)
                          | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0x3a;
    pCodeBuf[off++] = 0x20;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iGprSrc & 7);
    pCodeBuf[off++] = iByte;
#elif defined(RT_ARCH_ARM64)
    /* ins vecsrc[iByte], gpr */
    pCodeBuf[off++] = Armv8A64MkVecInstrIns(iVecRegDst, iGprSrc, iByte, kArmv8InstrUmovInsSz_U8);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[x] = gprsrc store, 8-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreGprToVecRegU8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, uint8_t iByte)
{
    Assert(iByte <= 15);

#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdStoreGprToVecRegU8Ex(iemNativeInstrBufEnsure(pReNative, off, 7), off, iVecRegDst, iGprSrc, iByte);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdStoreGprToVecRegU8Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecRegDst, iGprSrc, iByte);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst.au32[iDWord] = 0 store.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdZeroVecRegElemU32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecReg, uint8_t iDWord)
{
    Assert(iDWord <= 7);

#ifdef RT_ARCH_AMD64
    /*
     * xor tmp0, tmp0
     * pinsrd xmm, tmp0, iDword
     */
    if (IEMNATIVE_REG_FIXED_TMP0 >= 8)
        pCodeBuf[off++] = X86_OP_REX_R | X86_OP_REX_B;
    pCodeBuf[off++] = 0x33;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, IEMNATIVE_REG_FIXED_TMP0 & 7, IEMNATIVE_REG_FIXED_TMP0 & 7);
    off = iemNativeEmitSimdStoreGprToVecRegU32Ex(pCodeBuf, off, iVecReg, IEMNATIVE_REG_FIXED_TMP0, iDWord);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecReg & 0x1));
    /* ins vecsrc[iDWord], wzr */
    if (iDWord >= 4)
        pCodeBuf[off++] = Armv8A64MkVecInstrIns(iVecReg + 1, ARMV8_A64_REG_WZR, iDWord - 4, kArmv8InstrUmovInsSz_U32);
    else
        pCodeBuf[off++] = Armv8A64MkVecInstrIns(iVecReg, ARMV8_A64_REG_WZR, iDWord, kArmv8InstrUmovInsSz_U32);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst.au32[iDWord] = 0 store.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdZeroVecRegElemU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecReg, uint8_t iDWord)
{

#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdZeroVecRegElemU32Ex(iemNativeInstrBufEnsure(pReNative, off, 10), off, iVecReg, iDWord);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdZeroVecRegElemU32Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecReg, iDWord);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst[0:127] = 0 store.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdZeroVecRegLowU128Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecReg)
{
#ifdef RT_ARCH_AMD64
    /* pxor xmm, xmm */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iVecReg >= 8)
        pCodeBuf[off++] = X86_OP_REX_B | X86_OP_REX_R;
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xef;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecReg & 7, iVecReg & 7);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecReg & 0x1));
    /* eor vecreg, vecreg, vecreg */
    pCodeBuf[off++] = Armv8A64MkVecInstrEor(iVecReg, iVecReg, iVecReg);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[0:127] = 0 store.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdZeroVecRegLowU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecReg)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdZeroVecRegLowU128Ex(iemNativeInstrBufEnsure(pReNative, off, 5), off, iVecReg);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdZeroVecRegLowU128Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecReg);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst[128:255] = 0 store.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdZeroVecRegHighU128Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecReg)
{
#ifdef RT_ARCH_AMD64
    /* vmovdqa xmm, xmm. This will clear the upper half of ymm */
    if (iVecReg < 8)
    {
        pCodeBuf[off++] = X86_OP_VEX2;
        pCodeBuf[off++] = 0xf9;
    }
    else
    {
        pCodeBuf[off++] = X86_OP_VEX3;
        pCodeBuf[off++] = 0x41;
        pCodeBuf[off++] = 0x79;
    }
    pCodeBuf[off++] = 0x6f;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecReg & 7, iVecReg & 7);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecReg & 0x1));
    /* eor vecreg, vecreg, vecreg */
    pCodeBuf[off++] = Armv8A64MkVecInstrEor(iVecReg + 1, iVecReg + 1, iVecReg + 1);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[128:255] = 0 store.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdZeroVecRegHighU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecReg)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdZeroVecRegHighU128Ex(iemNativeInstrBufEnsure(pReNative, off, 7), off, iVecReg);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdZeroVecRegHighU128Ex(iemNativeInstrBufEnsure(pReNative, off, 1), off, iVecReg);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst[0:255] = 0 store.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdZeroVecRegU256Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecReg)
{
#ifdef RT_ARCH_AMD64
    /* vpxor ymm, ymm, ymm */
    if (iVecReg < 8)
    {
        pCodeBuf[off++] = X86_OP_VEX2;
        pCodeBuf[off++] = X86_OP_VEX2_BYTE1_MAKE(false, iVecReg, true, X86_OP_VEX3_BYTE2_P_066H);
    }
    else
    {
        pCodeBuf[off++] = X86_OP_VEX3;
        pCodeBuf[off++] = X86_OP_VEX3_BYTE1_X | 0x01;
        pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE(false, iVecReg, true, X86_OP_VEX3_BYTE2_P_066H);
    }
    pCodeBuf[off++] = 0xef;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecReg & 7, iVecReg & 7);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecReg & 0x1));
    /* eor vecreg, vecreg, vecreg */
    pCodeBuf[off++] = Armv8A64MkVecInstrEor(iVecReg,     iVecReg,     iVecReg);
    pCodeBuf[off++] = Armv8A64MkVecInstrEor(iVecReg + 1, iVecReg + 1, iVecReg + 1);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[0:255] = 0 store.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdZeroVecRegU256(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecReg)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdZeroVecRegU256Ex(iemNativeInstrBufEnsure(pReNative, off, 5), off, iVecReg);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdZeroVecRegU256Ex(iemNativeInstrBufEnsure(pReNative, off, 2), off, iVecReg);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst = gprsrc broadcast, 8-bit.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdBroadcastGprToVecRegU8Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, bool f256Bit = false)
{
#ifdef RT_ARCH_AMD64
    /* pinsrb vecdst, gpr, #0 (ASSUMES SSE 4.1) */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iVecRegDst >= 8 || iGprSrc >= 8)
        pCodeBuf[off++] =   (iVecRegDst < 8 ? 0 : X86_OP_REX_R)
                          | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0x3a;
    pCodeBuf[off++] = 0x20;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iGprSrc & 7);
    pCodeBuf[off++] = 0x00;

    /* vpbroadcastb {y,x}mm, xmm (ASSUMES AVX2). */
    pCodeBuf[off++] = X86_OP_VEX3;
    pCodeBuf[off++] =   X86_OP_VEX3_BYTE1_X
                      | 0x02                 /* opcode map. */
                      | (  iVecRegDst >= 8
                         ? 0
                         : X86_OP_VEX3_BYTE1_B | X86_OP_VEX3_BYTE1_R);
    pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE_NO_VVVV(false, f256Bit, X86_OP_VEX3_BYTE2_P_066H);
    pCodeBuf[off++] = 0x78;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iVecRegDst & 7);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecRegDst & 0x1) || !f256Bit);

    /* dup vecsrc, gpr */
    pCodeBuf[off++] = Armv8A64MkVecInstrDup(iVecRegDst, iGprSrc, kArmv8InstrUmovInsSz_U8);
    if (f256Bit)
        pCodeBuf[off++] = Armv8A64MkVecInstrDup(iVecRegDst + 1, iGprSrc, kArmv8InstrUmovInsSz_U8);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[x] = gprsrc broadcast, 8-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdBroadcastGprToVecRegU8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, bool f256Bit = false)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdBroadcastGprToVecRegU8Ex(iemNativeInstrBufEnsure(pReNative, off, 12), off, iVecRegDst, iGprSrc, f256Bit);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdBroadcastGprToVecRegU8Ex(iemNativeInstrBufEnsure(pReNative, off, f256Bit ? 2 : 1), off, iVecRegDst, iGprSrc, f256Bit);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst = gprsrc broadcast, 16-bit.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdBroadcastGprToVecRegU16Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, bool f256Bit = false)
{
#ifdef RT_ARCH_AMD64
    /* pinsrw vecdst, gpr, #0 */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iVecRegDst >= 8 || iGprSrc >= 8)
        pCodeBuf[off++] =   (iVecRegDst < 8 ? 0 : X86_OP_REX_R)
                          | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0xc4;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iGprSrc & 7);
    pCodeBuf[off++] = 0x00;

    /* vpbroadcastd {y,x}mm, xmm (ASSUMES AVX2). */
    pCodeBuf[off++] = X86_OP_VEX3;
    pCodeBuf[off++] =   X86_OP_VEX3_BYTE1_X
                      | 0x02                 /* opcode map. */
                      | (  iVecRegDst >= 8
                         ? 0
                         : X86_OP_VEX3_BYTE1_B | X86_OP_VEX3_BYTE1_R);
    pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE_NO_VVVV(false, f256Bit, X86_OP_VEX3_BYTE2_P_066H);
    pCodeBuf[off++] = 0x79;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iVecRegDst & 7);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecRegDst & 0x1) || !f256Bit);

    /* dup vecsrc, gpr */
    pCodeBuf[off++] = Armv8A64MkVecInstrDup(iVecRegDst, iGprSrc, kArmv8InstrUmovInsSz_U16);
    if (f256Bit)
        pCodeBuf[off++] = Armv8A64MkVecInstrDup(iVecRegDst + 1, iGprSrc, kArmv8InstrUmovInsSz_U16);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[x] = gprsrc broadcast, 16-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdBroadcastGprToVecRegU16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, bool f256Bit = false)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdBroadcastGprToVecRegU16Ex(iemNativeInstrBufEnsure(pReNative, off, 12), off, iVecRegDst, iGprSrc, f256Bit);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdBroadcastGprToVecRegU16Ex(iemNativeInstrBufEnsure(pReNative, off, f256Bit ? 2 : 1), off, iVecRegDst, iGprSrc, f256Bit);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst = gprsrc broadcast, 32-bit.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdBroadcastGprToVecRegU32Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, bool f256Bit = false)
{
#ifdef RT_ARCH_AMD64
    /** @todo If anyone has a better idea on how to do this more efficiently I'm all ears,
     *        vbroadcast needs a memory operand or another xmm register to work... */

    /* pinsrd vecdst, gpr, #0 (ASSUMES SSE4.1). */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (iVecRegDst >= 8 || iGprSrc >= 8)
        pCodeBuf[off++] =   (iVecRegDst < 8 ? 0 : X86_OP_REX_R)
                          | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0x3a;
    pCodeBuf[off++] = 0x22;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iGprSrc & 7);
    pCodeBuf[off++] = 0x00;

    /* vpbroadcastd {y,x}mm, xmm (ASSUMES AVX2). */
    pCodeBuf[off++] = X86_OP_VEX3;
    pCodeBuf[off++] =   X86_OP_VEX3_BYTE1_X
                      | 0x02                 /* opcode map. */
                      | (  iVecRegDst >= 8
                         ? 0
                         : X86_OP_VEX3_BYTE1_B | X86_OP_VEX3_BYTE1_R);
    pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE_NO_VVVV(false, f256Bit, X86_OP_VEX3_BYTE2_P_066H);
    pCodeBuf[off++] = 0x58;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iVecRegDst & 7);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecRegDst & 0x1) || !f256Bit);

    /* dup vecsrc, gpr */
    pCodeBuf[off++] = Armv8A64MkVecInstrDup(iVecRegDst, iGprSrc, kArmv8InstrUmovInsSz_U32);
    if (f256Bit)
        pCodeBuf[off++] = Armv8A64MkVecInstrDup(iVecRegDst + 1, iGprSrc, kArmv8InstrUmovInsSz_U32);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[x] = gprsrc broadcast, 32-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdBroadcastGprToVecRegU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, bool f256Bit = false)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdBroadcastGprToVecRegU32Ex(iemNativeInstrBufEnsure(pReNative, off, 12), off, iVecRegDst, iGprSrc, f256Bit);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdBroadcastGprToVecRegU32Ex(iemNativeInstrBufEnsure(pReNative, off, f256Bit ? 2 : 1), off, iVecRegDst, iGprSrc, f256Bit);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst = gprsrc broadcast, 64-bit.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdBroadcastGprToVecRegU64Ex(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, bool f256Bit = false)
{
#ifdef RT_ARCH_AMD64
    /** @todo If anyone has a better idea on how to do this more efficiently I'm all ears,
     *        vbroadcast needs a memory operand or another xmm register to work... */

    /* pinsrq vecdst, gpr, #0 (ASSUMES SSE4.1). */
    pCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    pCodeBuf[off++] =   X86_OP_REX_W
                      | (iVecRegDst < 8 ? 0 : X86_OP_REX_R)
                      | (iGprSrc < 8 ? 0 : X86_OP_REX_B);
    pCodeBuf[off++] = 0x0f;
    pCodeBuf[off++] = 0x3a;
    pCodeBuf[off++] = 0x22;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iGprSrc & 7);
    pCodeBuf[off++] = 0x00;

    /* vpbroadcastq {y,x}mm, xmm (ASSUMES AVX2). */
    pCodeBuf[off++] = X86_OP_VEX3;
    pCodeBuf[off++] =   X86_OP_VEX3_BYTE1_X
                      | 0x02                 /* opcode map. */
                      | (  iVecRegDst >= 8
                         ? 0
                         : X86_OP_VEX3_BYTE1_B | X86_OP_VEX3_BYTE1_R);
    pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE_NO_VVVV(false, f256Bit, X86_OP_VEX3_BYTE2_P_066H);
    pCodeBuf[off++] = 0x59;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iVecRegDst & 7);
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecRegDst & 0x1) || !f256Bit);

    /* dup vecsrc, gpr */
    pCodeBuf[off++] = Armv8A64MkVecInstrDup(iVecRegDst, iGprSrc, kArmv8InstrUmovInsSz_U64);
    if (f256Bit)
        pCodeBuf[off++] = Armv8A64MkVecInstrDup(iVecRegDst + 1, iGprSrc, kArmv8InstrUmovInsSz_U64);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[x] = gprsrc broadcast, 64-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdBroadcastGprToVecRegU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iGprSrc, bool f256Bit = false)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdBroadcastGprToVecRegU64Ex(iemNativeInstrBufEnsure(pReNative, off, 14), off, iVecRegDst, iGprSrc, f256Bit);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdBroadcastGprToVecRegU64Ex(iemNativeInstrBufEnsure(pReNative, off, f256Bit ? 2 : 1), off, iVecRegDst, iGprSrc, f256Bit);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/**
 * Emits a vecdst[0:127] = vecdst[128:255] = vecsrc[0:127] broadcast, 128-bit.
 */
DECL_FORCE_INLINE(uint32_t)
iemNativeEmitSimdBroadcastVecRegU128ToVecRegEx(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t iVecRegDst, uint8_t iVecRegSrc)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdLoadVecRegFromVecRegU128Ex(pCodeBuf, off, iVecRegDst, iVecRegSrc);

    /* vinserti128 ymm, ymm, xmm, 1. */ /* ASSUMES AVX2 support */
    pCodeBuf[off++] = X86_OP_VEX3;
    pCodeBuf[off++] = X86_OP_VEX3_BYTE1_MAKE(0x3, iVecRegSrc >= 8, false, iVecRegDst >= 8);
    pCodeBuf[off++] = X86_OP_VEX3_BYTE2_MAKE(false, iVecRegDst, true, X86_OP_VEX3_BYTE2_P_066H);
    pCodeBuf[off++] = 0x38;
    pCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, iVecRegDst & 7, iVecRegSrc & 7);
    pCodeBuf[off++] = 0x01; /* Immediate */
#elif defined(RT_ARCH_ARM64)
    /* ASSUMES that there are two adjacent 128-bit registers available for the 256-bit value. */
    Assert(!(iVecRegDst & 0x1));

    /* mov dst, src;   alias for: orr dst, src, src */
    pCodeBuf[off++] = Armv8A64MkVecInstrOrr(iVecRegDst,     iVecRegSrc, iVecRegSrc);
    pCodeBuf[off++] = Armv8A64MkVecInstrOrr(iVecRegDst + 1, iVecRegSrc, iVecRegSrc);
#else
# error "port me"
#endif
    return off;
}


/**
 * Emits a vecdst[0:127] = vecdst[128:255] = vecsrc[0:127] broadcast, 128-bit.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdBroadcastVecRegU128ToVecReg(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iVecRegDst, uint8_t iVecRegSrc)
{
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSimdBroadcastVecRegU128ToVecRegEx(iemNativeInstrBufEnsure(pReNative, off, 11), off, iVecRegDst, iVecRegSrc);
#elif defined(RT_ARCH_ARM64)
    off = iemNativeEmitSimdBroadcastVecRegU128ToVecRegEx(iemNativeInstrBufEnsure(pReNative, off, 2), off, iVecRegDst, iVecRegSrc);
#else
# error "port me"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    return off;
}


/** @} */

#endif /* !VMM_INCLUDED_SRC_include_IEMN8veRecompilerEmit_h */

