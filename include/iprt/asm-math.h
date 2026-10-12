/** @file
 * IPRT - Assembly Routines for Optimizing some Integers Math Operations.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef IPRT_INCLUDED_asm_math_h
#define IPRT_INCLUDED_asm_math_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

#if defined(_MSC_VER) && RT_INLINE_ASM_USES_INTRIN
/* Emit the intrinsics at all optimization levels. */
# include <iprt/sanitized/intrin.h>
# if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
#  pragma intrinsic(__emul)
#  pragma intrinsic(__emulu)
#  ifdef RT_ARCH_AMD64
#   pragma intrinsic(_mul128)
#   pragma intrinsic(_umul128)
#  endif
# endif
#endif


/*
 * Undefine all symbols we have Watcom C/C++ #pragma aux'es for.
 */
#if defined(__WATCOMC__) && ARCH_BITS == 16 && defined(RT_ARCH_X86)
/*# include "asm-math-watcom-x86-16.h"*/
#elif defined(__WATCOMC__) && ARCH_BITS == 32 && defined(RT_ARCH_X86)
# include "asm-math-watcom-x86-32.h"
#endif


/** @defgroup grp_rt_asm_math   Interger Math Optimizations
 * @ingroup grp_rt_asm
 * @{ */

/**
 * Multiplies two unsigned 32-bit values returning an unsigned 64-bit result.
 *
 * @returns u32F1 * u32F2.
 */

#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN && defined(RT_ARCH_X86)
RT_ASM_DECL_PRAGMA_WATCOM(uint64_t) ASMMult2xU32RetU64(uint32_t u32F1, uint32_t u32F2);
#else
DECLINLINE(uint64_t) ASMMult2xU32RetU64(uint32_t u32F1, uint32_t u32F2)
{
# ifdef RT_ARCH_X86
    uint64_t u64;
#  if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("mull %%edx"
                         : "=A" (u64)
                         : "a" (u32F2), "d" (u32F1));
#  elif RT_INLINE_ASM_USES_INTRIN
    u64 = __emulu(u32F1, u32F2);
#  else
    __asm
    {
        mov     edx, [u32F1]
        mov     eax, [u32F2]
        mul     edx
        mov     dword ptr [u64], eax
        mov     dword ptr [u64 + 4], edx
    }
#  endif
    return u64;
# else  /* generic: */
    return (uint64_t)u32F1 * u32F2;
# endif
}
#endif


/**
 * Multiplies two signed 32-bit values returning a signed 64-bit result.
 *
 * @returns u32F1 * u32F2.
 */
#if RT_INLINE_ASM_EXTERNAL && !RT_INLINE_ASM_USES_INTRIN && defined(RT_ARCH_X86)
RT_ASM_DECL_PRAGMA_WATCOM(int64_t) ASMMult2xS32RetS64(int32_t i32F1, int32_t i32F2);
#else
DECLINLINE(int64_t) ASMMult2xS32RetS64(int32_t i32F1, int32_t i32F2)
{
# ifdef RT_ARCH_X86
    int64_t i64;
#  if RT_INLINE_ASM_GNU_STYLE
    __asm__ __volatile__("imull %%edx"
                         : "=A" (i64)
                         : "a" (i32F2), "d" (i32F1));
#  elif RT_INLINE_ASM_USES_INTRIN
    i64 = __emul(i32F1, i32F2);
#  else
    __asm
    {
        mov     edx, [i32F1]
        mov     eax, [i32F2]
        imul    edx
        mov     dword ptr [i64], eax
        mov     dword ptr [i64 + 4], edx
    }
#  endif
    return i64;
# else  /* generic: */
    return (int64_t)i32F1 * i32F2;
# endif
}
#endif


DECLINLINE(uint64_t) ASMMult2xU64Ret2xU64(uint64_t u64F1, uint64_t u64F2, uint64_t *pu64ProdHi)
{
#if defined(RT_ARCH_AMD64) && (RT_INLINE_ASM_GNU_STYLE || RT_INLINE_ASM_USES_INTRIN)
# if RT_INLINE_ASM_GNU_STYLE
    uint64_t u64Low, u64High;
    __asm__ __volatile__("mulq %%rdx"
                         : "=a" (u64Low), "=d" (u64High)
                         : "0" (u64F1), "1" (u64F2));
    *pu64ProdHi = u64High;
    return u64Low;
# elif RT_INLINE_ASM_USES_INTRIN
    return _umul128(u64F1, u64F2, pu64ProdHi);
# else
#  error "hmm"
# endif
#else  /* generic: */
    /*
     *   F1 * F2 = Prod
     *   --   --
     *   ab * cd =  b*d + a*d*10  +  b*c*10 + a*c*100
     *
     * Where a, b, c and d are 'digits', and 10 is max digit + 1.
     *
     * Our digits are 32-bit wide, so instead of 10 we multiply by 4G.
     *  Prod = F1.s.Lo*F2.s.Lo    + F1.s.Hi*F2.s.Lo*4G
     *       + F1.s.Lo*F2.s.Hi*4G + F1.s.Hi*F2.s.Hi*4G*4G
     */
    RTUINT128U Prod;
    RTUINT64U  Tmp1;
    uint64_t   u64Tmp;
    RTUINT64U  F1, F2;
    F1.u = u64F1;
    F2.u = u64F2;

    Prod.s.Lo = ASMMult2xU32RetU64(F1.s.Lo, F2.s.Lo);

    Tmp1.u = ASMMult2xU32RetU64(F1.s.Hi, F2.s.Lo);
    u64Tmp = (uint64_t)Prod.DWords.dw1 + Tmp1.s.Lo;
    Prod.DWords.dw1 = (uint32_t)u64Tmp;
    Prod.s.Hi = Tmp1.s.Hi;
    Prod.s.Hi += u64Tmp >> 32; /* carry */

    Tmp1.u = ASMMult2xU32RetU64(F1.s.Lo, F2.s.Hi);
    u64Tmp = (uint64_t)Prod.DWords.dw1 + Tmp1.s.Lo;
    Prod.DWords.dw1 = (uint32_t)u64Tmp;
    u64Tmp >>= 32;      /* carry */
    u64Tmp += Prod.DWords.dw2;
    u64Tmp += Tmp1.s.Hi;
    Prod.DWords.dw2 = (uint32_t)u64Tmp;
    Prod.DWords.dw3 += u64Tmp >> 32; /* carry */

    Prod.s.Hi += ASMMult2xU32RetU64(F1.s.Hi, F2.s.Hi);
    *pu64ProdHi  = Prod.s.Hi;
    return Prod.s.Lo;
#endif
}


DECLINLINE(int64_t) ASMMult2xS64Ret2xS64(int64_t i64F1, int64_t i64F2, int64_t *pi64ProdHi)
{
#if defined(RT_ARCH_AMD64) && (RT_INLINE_ASM_GNU_STYLE || RT_INLINE_ASM_USES_INTRIN)
# if RT_INLINE_ASM_GNU_STYLE
    int64_t i64Low, i64High;
    __asm__ __volatile__("imulq %%rdx"
                         : "=a" (i64Low), "=d" (i64High)
                         : "0" (i64F1), "1" (i64F2));
    *pi64ProdHi = i64High;
    return i64Low;
# elif RT_INLINE_ASM_USES_INTRIN
    return _mul128(i64F1, i64F2, pi64ProdHi);
# else
#  error "hmm"
# endif
#else  /* generic: */
    /*
     * Signed multiplication is done by performing unsigned multiplication of
     * the absolute factors and applying the right sign to the product.
     */
    uint64_t uProdHi;
    uint64_t uProdLo;
    if (i64F1 >= 0)
    {
        /* If both positive, do unsigned multiplication. */
        if (i64F2 >= 0)
            return (int64_t)ASMMult2xU64Ret2xU64((uint64_t)i64F1, (uint64_t)i64F2, (uint64_t *)pi64ProdHi);
        i64F2 = -i64F2;
    }
    /* If both negative, negate the factors and do unsigned multiplication. */
    else if (i64F2 < 0)
        return (int64_t)ASMMult2xU64Ret2xU64((uint64_t)-i64F1, (uint64_t)-i64F2, (uint64_t *)pi64ProdHi);
    else
        i64F1 = -i64F1;
    uProdLo = ASMMult2xU64Ret2xU64((uint64_t)i64F1, (uint64_t)i64F2, &uProdHi);

    /* Negate the result */
    if (uProdLo != 0)
    {
        *pi64ProdHi = (int64_t)(UINT64_MAX - uProdHi);
        return (int64_t)(UINT64_C(0) - uProdLo);
    }
    *pi64ProdHi = (int64_t)(UINT64_C(0) - uProdHi);
    return 0;
#endif
}


/**
 * Divides a 64-bit unsigned by a 32-bit unsigned returning an unsigned 32-bit result.
 *
 * @returns u64 / u32.
 */
#if RT_INLINE_ASM_EXTERNAL && defined(RT_ARCH_X86)
RT_ASM_DECL_PRAGMA_WATCOM(uint32_t) ASMDivU64ByU32RetU32(uint64_t u64, uint32_t u32);
#else
DECLINLINE(uint32_t) ASMDivU64ByU32RetU32(uint64_t u64, uint32_t u32)
{
# ifdef RT_ARCH_X86
#  if RT_INLINE_ASM_GNU_STYLE
    RTCCUINTREG uDummy;
    __asm__ __volatile__("divl %3"
                         : "=a" (u32), "=d"(uDummy)
                         : "A" (u64), "r" (u32));
#  else
    __asm
    {
        mov     eax, dword ptr [u64]
        mov     edx, dword ptr [u64 + 4]
        mov     ecx, [u32]
        div     ecx
        mov     [u32], eax
    }
#  endif
    return u32;
# else   /* generic: */
    return (uint32_t)(u64 / u32);
# endif
}
#endif


/**
 * Divides a 64-bit signed by a 32-bit signed returning a signed 32-bit result.
 *
 * @returns u64 / u32.
 */
#if RT_INLINE_ASM_EXTERNAL && defined(RT_ARCH_X86)
RT_ASM_DECL_PRAGMA_WATCOM(int32_t) ASMDivS64ByS32RetS32(int64_t i64, int32_t i32);
#else
DECLINLINE(int32_t) ASMDivS64ByS32RetS32(int64_t i64, int32_t i32)
{
# ifdef RT_ARCH_X86
#  if RT_INLINE_ASM_GNU_STYLE
    RTCCUINTREG iDummy;
    __asm__ __volatile__("idivl %3"
                         : "=a" (i32), "=d"(iDummy)
                         : "A" (i64), "r" (i32));
#  else
    __asm
    {
        mov     eax, dword ptr [i64]
        mov     edx, dword ptr [i64 + 4]
        mov     ecx, [i32]
        idiv    ecx
        mov     [i32], eax
    }
#  endif
    return i32;
# else  /* generic: */
    return (int32_t)(i64 / i32);
# endif
}
#endif


/**
 * Performs 64-bit unsigned by a 32-bit unsigned division with a 32-bit unsigned result,
 * returning the rest.
 *
 * @returns u64 % u32.
 *
 * @remarks It is important that the result is <= UINT32_MAX or we'll overflow and crash.
 */
#if RT_INLINE_ASM_EXTERNAL && defined(RT_ARCH_X86)
RT_ASM_DECL_PRAGMA_WATCOM(uint32_t) ASMModU64ByU32RetU32(uint64_t u64, uint32_t u32);
#else
DECLINLINE(uint32_t) ASMModU64ByU32RetU32(uint64_t u64, uint32_t u32)
{
# ifdef RT_ARCH_X86
#  if RT_INLINE_ASM_GNU_STYLE
    RTCCUINTREG uDummy;
    __asm__ __volatile__("divl %3"
                         : "=a" (uDummy), "=d"(u32)
                         : "A" (u64), "r" (u32));
#  else
    __asm
    {
        mov     eax, dword ptr [u64]
        mov     edx, dword ptr [u64 + 4]
        mov     ecx, [u32]
        div     ecx
        mov     [u32], edx
    }
#  endif
    return u32;
# else  /* generic: */
    return (uint32_t)(u64 % u32);
# endif
}
#endif


/**
 * Performs 64-bit signed by a 32-bit signed division with a 32-bit signed result,
 * returning the rest.
 *
 * @returns u64 % u32.
 *
 * @remarks It is important that the result is <= UINT32_MAX or we'll overflow and crash.
 */
#if RT_INLINE_ASM_EXTERNAL && defined(RT_ARCH_X86)
RT_ASM_DECL_PRAGMA_WATCOM(int32_t) ASMModS64ByS32RetS32(int64_t i64, int32_t i32);
#else
DECLINLINE(int32_t) ASMModS64ByS32RetS32(int64_t i64, int32_t i32)
{
# ifdef RT_ARCH_X86
#  if RT_INLINE_ASM_GNU_STYLE
    RTCCUINTREG iDummy;
    __asm__ __volatile__("idivl %3"
                         : "=a" (iDummy), "=d"(i32)
                         : "A" (i64), "r" (i32));
#  else
    __asm
    {
        mov     eax, dword ptr [i64]
        mov     edx, dword ptr [i64 + 4]
        mov     ecx, [i32]
        idiv    ecx
        mov     [i32], edx
    }
#  endif
    return i32;
# else  /* generic: */
    return (int32_t)(i64 % i32);
# endif
}
#endif


/**
 * Multiple a 32-bit by a 32-bit integer and divide the result by a 32-bit integer
 * using a 64 bit intermediate result.
 *
 * @returns (u32A * u32B) / u32C.
 * @param   u32A    The 32-bit value (A).
 * @param   u32B    The 32-bit value to multiple by A.
 * @param   u32C    The 32-bit value to divide A*B by.
 *
 * @remarks Architecture specific.
 * @remarks Make sure the result won't ever exceed 32-bit, because hardware
 *          exception may be raised if it does.
 * @remarks On x86 this may be used to avoid dragging in 64-bit builtin
 *          arithmetics functions.
 */
#if RT_INLINE_ASM_EXTERNAL && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86))
RT_ASM_DECL_PRAGMA_WATCOM(uint32_t) ASMMultU32ByU32DivByU32(uint32_t u32A, uint32_t u32B, uint32_t u32C);
#else
DECLINLINE(uint32_t) ASMMultU32ByU32DivByU32(uint32_t u32A, uint32_t u32B, uint32_t u32C)
{
# if RT_INLINE_ASM_GNU_STYLE && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86))
    uint32_t u32Result, u32Spill;
    __asm__ __volatile__("mull %2\n\t"
                         "divl %3\n\t"
                         : "=&a" (u32Result),
                           "=&d" (u32Spill)
                         : "r" (u32B),
                           "r" (u32C),
                           "0" (u32A));
    return u32Result;
# else
    return (uint32_t)(((uint64_t)u32A * u32B) / u32C);
# endif
}
#endif


/**
 * Multiple a 64-bit by a 32-bit integer and divide the result by a 32-bit integer
 * using a 96 bit intermediate result.
 *
 * @returns (u64A * u32B) / u32C.
 * @param   u64A    The 64-bit value.
 * @param   u32B    The 32-bit value to multiple by A.
 * @param   u32C    The 32-bit value to divide A*B by.
 *
 * @remarks Architecture specific.
 * @remarks Make sure the result won't ever exceed 64-bit, because hardware
 *          exception may be raised if it does.
 * @remarks On x86 this may be used to avoid dragging in 64-bit builtin
 *          arithmetics function.
 */
#if RT_INLINE_ASM_EXTERNAL || !defined(__GNUC__) || (!defined(RT_ARCH_AMD64) && !defined(RT_ARCH_X86))
RT_DECL_ASM(uint64_t) ASMMultU64ByU32DivByU32(uint64_t u64A, uint32_t u32B, uint32_t u32C);
#else
DECLINLINE(uint64_t) ASMMultU64ByU32DivByU32(uint64_t u64A, uint32_t u32B, uint32_t u32C)
{
# if RT_INLINE_ASM_GNU_STYLE
#  ifdef RT_ARCH_AMD64
    uint64_t u64Result, u64Spill;
    __asm__ __volatile__("mulq %2\n\t"
                         "divq %3\n\t"
                         : "=&a" (u64Result),
                           "=&d" (u64Spill)
                         : "r" ((uint64_t)u32B),
                           "r" ((uint64_t)u32C),
                           "0" (u64A));
    return u64Result;
#  else
    uint32_t u32Dummy;
    uint64_t u64Result;
    __asm__ __volatile__("mull %%ecx       \n\t" /* eax = u64Lo.lo = (u64A.lo * u32B).lo
                                                    edx = u64Lo.hi = (u64A.lo * u32B).hi */
                         "xchg %%eax,%%esi \n\t" /* esi = u64Lo.lo
                                                    eax = u64A.hi */
                         "xchg %%edx,%%edi \n\t" /* edi = u64Low.hi
                                                    edx = u32C */
                         "xchg %%edx,%%ecx \n\t" /* ecx = u32C
                                                    edx = u32B */
                         "mull %%edx       \n\t" /* eax = u64Hi.lo = (u64A.hi * u32B).lo
                                                    edx = u64Hi.hi = (u64A.hi * u32B).hi */
                         "addl %%edi,%%eax \n\t" /* u64Hi.lo += u64Lo.hi */
                         "adcl $0,%%edx    \n\t" /* u64Hi.hi += carry */
                         "divl %%ecx       \n\t" /* eax = u64Hi / u32C
                                                    edx = u64Hi % u32C */
                         "movl %%eax,%%edi \n\t" /* edi = u64Result.hi = u64Hi / u32C */
                         "movl %%esi,%%eax \n\t" /* eax = u64Lo.lo */
                         "divl %%ecx       \n\t" /* u64Result.lo */
                         "movl %%edi,%%edx \n\t" /* u64Result.hi */
                         : "=A"(u64Result), "=c"(u32Dummy),
                           "=S"(u32Dummy), "=D"(u32Dummy)
                         : "a"((uint32_t)u64A),
                           "S"((uint32_t)(u64A >> 32)),
                           "c"(u32B),
                           "D"(u32C));
    return u64Result;
#  endif
# else
    RTUINT64U   u;
    uint64_t    u64Lo = (uint64_t)(u64A & 0xffffffff) * u32B;
    uint64_t    u64Hi = (uint64_t)(u64A >> 32)        * u32B;
    u64Hi  += (u64Lo >> 32);
    u.s.Hi = (uint32_t)(u64Hi / u32C);
    u.s.Lo = (uint32_t)((((u64Hi % u32C) << 32) + (u64Lo & 0xffffffff)) / u32C);
    return u.u;
# endif
}
#endif


/**
 * Multiple a 64-bit by a 32-bit integer and divide the result by a 64-bit integer
 * using a 96 bit intermediate result.
 *
 * @returns (u64A * u32B) / u64C.
 * @param   u64A    The 64-bit value.
 * @param   u32B    The 32-bit value to multiple by A.
 * @param   u64C    The 64-bit value to divide A*B by.
 *
 * @remarks Architecture specific.
 * @remarks Make sure the result won't ever exceed 64-bit, because hardware
 *          exception may be raised if it does.
 * @remarks On x86 this may be used to avoid dragging in 64-bit builtin
 *          arithmetics function.
 */
#if (defined(RT_ARCH_AMD64) && RT_INLINE_ASM_EXTERNAL) || (!defined(RT_ARCH_AMD64) && !defined(RT_ARCH_X86))
RT_DECL_ASM(uint64_t) ASMMultU64ByU32DivByU64(uint64_t u64A, uint32_t u32B, uint64_t u64C);
#else
DECLINLINE(uint64_t) ASMMultU64ByU32DivByU64(uint64_t u64A, uint32_t u32B, uint64_t u64C)
{
# if RT_INLINE_ASM_GNU_STYLE && defined(RT_ARCH_AMD64)
    uint64_t u64Result, u64Spill;
    __asm__ __volatile__("mulq %2\n\t"
                         "divq %3\n\t"
                         : "=&a" (u64Result),
                           "=&d" (u64Spill)
                         : "r" ((uint64_t)u32B),
                           "r" (u64C),
                           "0" (u64A));
    return u64Result;
# else
    uint64_t u64Rem;
    uint64_t u64Dividend = ASMMult2xU64Ret2xU64(u64A, u32B, &u64Rem);
    uint64_t u64Result   = 0;

     /* Not sure if we should assert here because ASMMultU64ByU32DivByU32() has the same requirement for divisor. */
#if 0
    Assert(u64C);
    Assert(u64Rem < u64C); /* Result must fit in 64 bits. */
#endif

    /* Binary long division. */
    for (unsigned i = 0; i < 64; i++)
    {
        /* Reminder * 2 + next-divident-bit can require 65-bits, fCarry holds the 65th bit here. */
        bool const fCarry = RT_BOOL(u64Rem & RT_BIT_64(63));
        u64Rem = (u64Rem << 1) | (u64Dividend >> 63);
        u64Dividend <<= 1;
        u64Result   <<= 1;
        if (   fCarry
            || u64Rem >= u64C)
        {
            u64Rem    -= u64C;
            u64Result |= 1;
        }
    }
    return u64Result;
# endif
}
#endif

/** @} */

/*
 * Include #pragma aux definitions for Watcom C/C++.
 */
#if defined(__WATCOMC__) && ARCH_BITS == 16 && defined(RT_ARCH_X86)
# define IPRT_ASM_WATCOM_X86_16_WITH_PRAGMAS
# undef IPRT_INCLUDED_asm_math_watcom_x86_16_h
/*# include "asm-math-watcom-x86-16.h"*/
#elif defined(__WATCOMC__) && ARCH_BITS == 32 && defined(RT_ARCH_X86)
# define IPRT_ASM_WATCOM_X86_32_WITH_PRAGMAS
# undef IPRT_INCLUDED_asm_math_watcom_x86_32_h
# include "asm-math-watcom-x86-32.h"
#endif

#endif /* !IPRT_INCLUDED_asm_math_h */

