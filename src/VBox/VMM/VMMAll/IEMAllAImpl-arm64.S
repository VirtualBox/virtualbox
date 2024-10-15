/* $Id: IEMAllAImpl-arm64.S 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Instruction Implementation in Assembly, ARM64 variant.
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


/*********************************************************************************************************************************
*       Header Files                                                                                                             *
*********************************************************************************************************************************/
#include <iprt/asmdefs-arm.h>
#include <iprt/x86.h>

#define IEM_AIMPL_FUNCTION_ALIGNMENT    0x20


#if RT_CLANG_PREREQ(15, 0)
        .arch_extension flagm   /* not necessary */
#else
        /* clang 12.0.x defaults to apple-a12. M1 is more similar to A14, I guess.
           For some reason the +crc make cfinv work (with clang 12). 'flagm' isn't
           recognized, nor is the 'fmi' in the error message for cfinv.  'flagm'
           work for v15 and is enabled by default it seems. */
# ifdef RT_OS_DARWIN
        .cpu            apple-a14+crc
# else
        .cpu            cortex-a53+flagm
# endif
#endif


.macro CALC_EFLAGS_PARITY, regEfl, regResult, regTmp
        /*
         * Parity calculation for low byte of the result (sucks that there is no popcount for gprs).
         */
        eor     \regTmp, \regResult, \regResult, LSR #4
        eor     \regTmp, \regTmp, \regTmp, LSR #2
        eor     \regTmp, \regTmp, \regTmp, LSR #1
        eor     \regTmp, \regTmp, #1
        bfi     \regEfl, \regTmp, #X86_EFL_PF_BIT, #1   /* PF(2) = popcount(w9 & 0xff) & 1 ^ 1 */
.endm


.macro CALC_EFLAGS_AUX_CARRY, regEfl, regResult, regLeft, regRight, regTmp
        /*
         * Auxilary carry / borrow flag.  This is related to 8-bit BCD.
         */
        eor     \regTmp, \regLeft, \regRight
        eor     \regTmp, \regTmp, \regResult
        lsr     \regTmp, \regTmp, #X86_EFL_AF_BIT
        bfi     \regEfl, \regTmp, #X86_EFL_AF_BIT, #1   /* AF(4) = (w8 ^ w1 ^ w9 & X86_EFL_AF) >> X86_EFL_AF_BIT */
.endm

.macro CALC_EFLAGS, regEfl, regResult, regLeft, regRight, regTmp, fSkipFlags=0
        /*
         * Translate the arm NZCV bits into corresponding EFLAGS bits.
         */
 .if \fSkipFlags == 0 || \fSkipFlags == X86_EFL_OF
#if 0
        /* Maybe just a tiny bit slow than the next one. */
        mrs     \regTmp, NZCV                           /* [31] = N; [30] = Z; [29] = C; [29] = V */
  .ifeq \fSkipFlags & X86_EFL_OF
        lsr     \regTmp, \regTmp, #28
        bfi     \regEfl, \regTmp, #X86_EFL_OF_BIT, #1
        lsr     \regTmp, \regTmp, #1
  .else
        lsr     \regTmp, \regTmp, #29
  .endif
        eor     \regTmp, \regTmp, #1                    /* inverts the carry flag to x86 style. */
        bfi     \regEfl, \regTmp, #X86_EFL_CF_BIT, #1   /* CF(0) = C */
        lsr     \regTmp, \regTmp, #1
        bfi     \regEfl, \regTmp, #X86_EFL_ZF_BIT, #2   /* SF(7),ZF(6) = NZ */
#else
        /* This seems to be the faster one... */
        cfinv
        mrs     \regTmp, NZCV                           /* [31] = N; [30] = Z; [29] = C; [29] = V */
  .ifeq (\fSkipFlags & X86_EFL_OF)
        lsr     \regTmp, \regTmp, #28
        bfi     \regEfl, \regTmp, #X86_EFL_OF_BIT, #1
        lsr     \regTmp, \regTmp, #1
  .else
        lsr     \regTmp, \regTmp, #29
  .endif
        bfi     \regEfl, \regTmp, #X86_EFL_CF_BIT, #1   /* CF(0) = C */
        lsr     \regTmp, \regTmp, #1
        bfi     \regEfl, \regTmp, #X86_EFL_ZF_BIT, #2   /* SF(7),ZF(6) = NZ */
#endif
 .else
        /* Definitely slower than the above two, but easier to handle wrt skipping parts. */
  .ifeq \fSkipFlags & X86_EFL_ZF
        cset    \regTmp, eq
        bfi     \regEfl, \regTmp, #X86_EFL_ZF_BIT, #1
  .endif
  .ifeq \fSkipFlags & X86_EFL_CF
        cset    \regTmp, cc
        bfi     \regEfl, \regTmp, #X86_EFL_CF_BIT, #1
  .endif
  .ifeq \fSkipFlags & X86_EFL_OF
        cset    \regTmp, vs
        bfi     \regEfl, \regTmp, #X86_EFL_OF_BIT, #1
  .endif
  .ifeq \fSkipFlags & X86_EFL_SF
        cset    \regTmp, mi
        bfi     \regEfl, \regTmp, #X86_EFL_SF_BIT, #1
  .endif
 .endif


        /*
         * Parity calculation for low byte of the result (sucks that there is no popcount for gprs).
         */
        eor     \regTmp, \regResult, \regResult, LSR #4
        eor     \regTmp, \regTmp, \regTmp, LSR #2
        eor     \regTmp, \regTmp, \regTmp, LSR #1
        eor     \regTmp, \regTmp, #1
        bfi     \regEfl, \regTmp, #X86_EFL_PF_BIT, #1   /* PF(2) = popcount(w9 & 0xff) & 1 ^ 1 */

        /*
         * Auxilary carry / borrow flag.  This is related to 8-bit BCD.
         */
        eor     \regTmp, \regLeft, \regRight
        eor     \regTmp, \regTmp, \regResult
        lsr     \regTmp, \regTmp, #X86_EFL_AF_BIT
        bfi     \regEfl, \regTmp, #X86_EFL_AF_BIT, #1   /* AF(4) = (w8 ^ w1 ^ w9 & X86_EFL_AF) >> X86_EFL_AF_BIT */

        /* done */
.endm


BEGINCODE



/* Some sketches.

// IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u8_locked, (uint8_t  *pu8Mem,  uint8_t  *pu8Reg));
BEGINPROC_HIDDEN iemAImpl_xchg_u8_locked
        ldrb    w2, [x1]
        swpalb  w2, w2, [x0]
        strb    w2, [x1]
        ret

// IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u16_locked,(uint16_t *pu16Mem, uint16_t *pu16Reg));
BEGINPROC_HIDDEN iemAImpl_xchg_u16_locked
        ldrh    w2, [x1]
        swpalh  w2, w2, [x0]
        strh    w2, [x1]
        ret

// IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u32_locked,(uint32_t *pu32Mem, uint32_t *pu32Reg));
// IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u64_locked,(uint64_t *pu64Mem, uint64_t *pu64Reg));

*/


/* IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u8_locked, (uint8_t  *pu8Mem,  uint8_t  *pu8Reg)); */

/*
 * The CMP instruction.
 */

/* uint32_t iemAImpl_cmp_u8(uint32_t fEFlags, uint8_t const *puDst, uint8_t uSrc); */
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN iemAImpl_sub_u8
        .cfi_startproc
        /* Do the subtraction. */
        ldrb    w8, [x1]
        /*and     w2, w2, #0xff - should not be necessary. */
        subs    w9, w8, w2                      /* w9 = w8 (*puDst) - w2 (uSrc)  */
        strb    w9, [x1]
        setf8   w9

        /* Calculate EFLAGS (passed in and returned via x0). */
        and     w9, w9, #0xffff
        CALC_EFLAGS x0, x9, x8, x2, x11, X86_EFL_OF

        /* The overflow flag calc done by setf16 isn't correct for subtraction, so we have to
           figure it out ourselves. (See IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC for details.) */
        eor     w11, w8, w2                     /* input dst ^ source (simplified from ~(dst ^ (source ^ 0x8000)) ). */
        eor     w12, w8, w9
        and     w11, w12, w11
        lsr     w11, w11, #7
        bfi     w0, w11, #X86_EFL_OF_BIT, #1

        ret
        .cfi_endproc


/* uint32_t iemAImpl_cmp_u16(uint32_t fEFlags, uint16_t const *puDst, uint16_t uSrc); */
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN iemAImpl_sub_u16
        .cfi_startproc
        /* Do the subtraction. */
        ldrh    w8, [x1]
        /*and     w2, w2, #0xffff - should not be necessary. */
        subs    w9, w8, w2                      /* w9 = w8 (*puDst) - w2 (uSrc)  */
        setf16  w9
        strh    w9, [x1]

        /* Calculate EFLAGS (passed in and returned via x0). */
        and     w9, w9, #0xffff
        CALC_EFLAGS x0, x9, x8, x2, x11, X86_EFL_OF

        /* The overflow flag calc done by setf16 isn't correct for subtraction, so we have to
           figure it out ourselves. (See IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC for details.) */
        eor     w11, w8, w2                     /* input dst ^ source (simplified from ~(dst ^ (source ^ 0x8000)) ). */
        eor     w12, w8, w9
        and     w11, w12, w11
        lsr     w11, w11, #15
        bfi     w0, w11, #X86_EFL_OF_BIT, #1

        ret
        .cfi_endproc


/* uint32_t iemAImpl_cmp_u32(uint32_t fEFlags, uint32_t const *puDst, uint32_t uSrc); */
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN iemAImpl_sub_u32
        .cfi_startproc
        /* Do the subtraction. */
        ldr     w8, [x1]
        subs    w9, w8, w2                      /* w9 = w8 (*puDst) - w2 (uSrc)  */
        str     w9, [x1]

        /* Calculate EFLAGS (passed in and returned via x0). */

#if 0
        /* Translate the arm NZCV bits into corresponding EFLAGS bits. */
#if 0   /* maybe just a tiny bit slow than the next one. */
        mrs     x11, NZCV                       /* w11[31] = N; w11[30] = Z; w11[29] = C; w11[29] = V */
        lsr     w11, w11, #28
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
        lsr     w11, w11, #1
        eor     w11, w11, #1                    /* inverts the carry flag to x86 style. */
        bfi     w0, w11, #X86_EFL_CF_BIT, #1    /* CF(0) = C */
        lsr     w11, w11, #1
        bfi     w0, w11, #X86_EFL_ZF_BIT, #2    /* SF(7),ZF(6) = NZ */
#elif 1 /* seems the faster one... */
        cfinv
        mrs     x11, NZCV                       /* w11[31] = N; w11[30] = Z; w11[29] = C; w11[29] = V */
        lsr     w11, w11, #28
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
        lsr     w11, w11, #1
        bfi     w0, w11, #X86_EFL_CF_BIT, #1    /* CF(0) = C */
        lsr     w11, w11, #1
        bfi     w0, w11, #X86_EFL_ZF_BIT, #2    /* SF(7),ZF(6) = NZ */
#else
        cset    w11, eq
        bfi     w0, w11, #X86_EFL_ZF_BIT, #1
        cset    w11, cc
        bfi     w0, w11, #X86_EFL_CF_BIT, #1
        cset    w11, vs
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
        cset    w11, mi
        bfi     w0, w11, #X86_EFL_SF_BIT, #1
#endif

        /* Parity calculation for low byte of the result (sucks that there is no popcount for gprs). */
        eor     w11, w9, w9, LSR #4
        eor     w11, w11, w11, LSR #2
        eor     w11, w11, w11, LSR #1
        eor     w11, w11, #1
        bfi     w0, w11, #X86_EFL_PF_BIT, #1    /* PF(2) = popcount(w9 & 0xff) & 1 ^ 1 */

        /* Auxilary carry / borrow flag.  This is related to 8-bit BCD. */
        eor     w11, w8, w2
        eor     w11, w11, w9
        lsr     w11, w11, #X86_EFL_AF_BIT
        bfi     w0, w11, #X86_EFL_AF_BIT, #1    /* AF(4) = (w8 ^ w2 ^ w9 & X86_EFL_AF) >> X86_EFL_AF_BIT */
#else
        CALC_EFLAGS x0, x9, x8, x2, x11
#endif

        ret
        .cfi_endproc


/* uint32_t iemAImpl_cmp_u64(uint32_t fEFlags, uint64_t const *puDst, uint64_t uSrc); */
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN iemAImpl_sub_u64
        .cfi_startproc
        /* Do the subtraction. */
        ldr     x8, [x1]
        subs    x9, x8, x2                      /* x9 = x8 (*puDst) - x2 (uSrc)  */
        str     x9, [x1]

        /* Calculate EFLAGS (passed in and returned via x0). */
        CALC_EFLAGS x0, x9, x8, x2, x11

        ret
        .cfi_endproc



/*
 * Shift Left.
 */

/* uint32_t iemAImpl_shl_u8( uint32_t fEFlagsIn, uint8_t *pu8Dst, uint8_t cShift); */
/* uint32_t iemAImpl_shl_u16(uint32_t fEFlagsIn, uint16_t *pu16Dst, uint8_t cShift); */
/* uint32_t iemAImpl_shl_u32(uint32_t fEFlagsIn, uint16_t *pu32Dst, uint8_t cShift); */
.macro SHL_8_16_32, a_Name, a_cBits, a_fIntelFlags, a_LdStSuff
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN \a_Name
        .cfi_startproc

        /* Do we need to shift anything at all? */
        and     w2, w2, #0x1f
        cbz     w2, 99f

        /*
         * Do the shifting
         */
        ldr\a_LdStSuff  w8, [x1]
.ifne \a_cBits < 32
        lslv    w9, w8, w2
.else
        lslv    x9, x8, x2                      /* use 64-bit registers here so we get CF for free. We know x1 != 0. */
.endif
        str\a_LdStSuff  w9, [x1]

        /*
         * Calculate EFLAGS.
         */
        CALC_EFLAGS_PARITY w0, w9, w12

.ifne \a_cBits < 32
        setf\a_cBits w9                         /* Sets NZ */
.else
        ands    wzr, w9, w9                     /* Sets NZ */
.endif
#if 1
        mrs     x11, NZCV
        lsr     w11, w11, #30                   /* N=1; Z=0 */
        bfi     w0, w11, X86_EFL_ZF_BIT, 2      /* EFLAGS.ZF and EFLAGS.SF */
#else
        cset    x11, eq
        bfi     w0, w11, X86_EFL_ZF_BIT, 1
        cset    x12, pl
        bfi     w0, w12, X86_EFL_SF_BIT, 1
#endif

.ifne \a_cBits < 32
        bfxil   w0, w9, #\a_cBits, #1           /* w9 bit 8/16 contains carry. (X86_EFL_CF_BIT == 0) */
.else
        bfxil   x0, x9, #\a_cBits, #1           /* x9 bit 32 contains carry. (X86_EFL_CF_BIT == 0) */
.endif

.ifne \a_fIntelFlags
        /* Intel: OF = first bit shifted: fEfl |= X86_EFL_GET_OF_ ## cOpBits(uDst ^ (uDst << 1)); */
        eor     w11, w8, w8, LSL #1
        lsr     w11, w11, #(\a_cBits - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1

        and     w0, w0, ~X86_EFL_AF            /* AF is cleared */
.else
        /* AMD: OF = last bit shifted: fEfl |= ((uResult >> (cOpBits - 1)) ^ fCarry) << X86_EFL_OF_BIT;  */
 .ifne \a_cBits < 32
        eor     w11, w9, w9, LSR #1
        lsr     w11, w11, #(\a_cBits - 1)
 .else
        eor     x11, x9, x9, LSR #1
        lsr     x11, x11, #(\a_cBits - 1)
 .endif
        bfi     w0, w11, #X86_EFL_OF_BIT, #1

        orr     w0, w0, X86_EFL_AF             /* AF is set  */
.endif

99:
        ret
        .cfi_endproc
.endm

SHL_8_16_32 iemAImpl_shl_u8,         8, 1, b
SHL_8_16_32 iemAImpl_shl_u8_intel,   8, 1, b
SHL_8_16_32 iemAImpl_shl_u8_amd,     8, 0, b

SHL_8_16_32 iemAImpl_shl_u16,       16, 1, h
SHL_8_16_32 iemAImpl_shl_u16_intel, 16, 1, h
SHL_8_16_32 iemAImpl_shl_u16_amd,   16, 0, h

SHL_8_16_32 iemAImpl_shl_u32,       32, 1,
SHL_8_16_32 iemAImpl_shl_u32_intel, 32, 1,
SHL_8_16_32 iemAImpl_shl_u32_amd,   32, 0,

/** @todo this is slightly slower than the C version (release) on an M2. Investigate why. */
/* uint32_t iemAImpl_shl_u64(uint32_t fEFlagsIn, uint16_t *pu64Dst, uint8_t cShift); */
.macro SHL_64, a_Name, a_fIntelFlags
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN \a_Name
        .cfi_startproc

        /* Do we need to shift anything at all? */
        and     w2, w2, #0x3f
        cbz     w2, 99f

        /*
         * Do the shifting
         */
        ldr     x8, [x1]
        lslv    x9, x8, x2
        str     x9, [x1]

        /*
         * Calculate EFLAGS.
         */
        CALC_EFLAGS_PARITY w0, w9, w11

        ands    xzr, x9, x9                     /* Sets NZ */
        mrs     x11, NZCV
        lsr     w11, w11, #30                   /* N=1; Z=0 */
        bfi     w0, w11, X86_EFL_ZF_BIT, 2      /* EFLAGS.ZF and EFLAGS.SF */

        neg     w11, w2                         /* the shift count is MODed by the data size, so this is safe. */
        lsrv    x11, x8, x11
        bfi     w0, w11, X86_EFL_CF_BIT, 1

.ifne \a_fIntelFlags
        /* Intel: OF = first bit shifted: fEfl |= X86_EFL_GET_OF_ ## cOpBits(uDst ^ (uDst << 1)); */
        eor     x11, x8, x8, LSL #1
        lsr     x11, x11, #63
        bfi     w0, w11, #X86_EFL_OF_BIT, #1

        and     w0, w0, ~X86_EFL_AF             /* AF is cleared */
.else
        /* AMD: OF = last bit shifted: fEfl |= ((uResult >> (cOpBits - 1)) ^ fCarry) << X86_EFL_OF_BIT;  */
        eor     x11, x11, x9, LSR #63           /* w11[0]=CF from above */
        bfi     w0, w11, #X86_EFL_OF_BIT, #1

        orr     w0, w0, X86_EFL_AF              /* AF is set  */
.endif
99:
        ret
        .cfi_endproc
.endm

SHL_64 iemAImpl_shl_u64,       1
SHL_64 iemAImpl_shl_u64_intel, 1
SHL_64 iemAImpl_shl_u64_amd,   0


/*
 * Shift Right, Unsigned.
 */

/* uint32_t iemAImpl_shr_u8( uint32_t fEFlagsIn, uint8_t *pu8Dst, uint8_t cShift); */
/* uint32_t iemAImpl_shr_u16(uint32_t fEFlagsIn, uint16_t *pu16Dst, uint8_t cShift); */
/* uint32_t iemAImpl_shr_u32(uint32_t fEFlagsIn, uint16_t *pu32Dst, uint8_t cShift); */
.macro shr_8_16_32, a_Name, a_cBits, a_fIntelFlags, a_LdStSuff
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN \a_Name
        .cfi_startproc

        /* Do we need to shift anything at all? */
        and     w2, w2, #0x1f
        cbz     w2, 99f

        /*
         * Do the shifting.
         */
        ldr\a_LdStSuff  w8, [x1]
        lsrv    w9, w8, w2
        str\a_LdStSuff  w9, [x1]

        /*
         * Calculate EFLAGS.
         */
        sub     w11, w2, #1
        lsrv    w11, w8, w11
        bfxil   w0, w11, #X86_EFL_CF_BIT, #1

.ifne \a_fIntelFlags
        and     w0, w0, ~X86_EFL_AF             /* AF is cleared */
        /* Intel: OF = one bit shift: fEfl |= X86_EFL_GET_OF_ ## cOpBits(uDstIn); */
        lsr     w11, w8, #(\a_cBits - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.else
        orr     w0, w0, X86_EFL_AF              /* AF is set  */
        /* AMD: OF = last bits shifted: fEfl |= (uResult >> (cOpBits - 2)) << X86_EFL_OF_BIT;  */
        lsr     w11, w9, #(\a_cBits - 2)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.endif

        CALC_EFLAGS_PARITY w0, w9, w11

.ifne \a_cBits < 32
        setf\a_cBits w9                         /* Sets NZ */
.else
        ands    wzr, w9, w9                     /* Sets NZ */
.endif
        mrs     x11, NZCV
        lsr     w11, w11, #30                   /* N=1; Z=0 */
        bfi     w0, w11, X86_EFL_ZF_BIT, 2      /* EFLAGS.ZF and EFLAGS.SF */

99:
        ret
        .cfi_endproc
.endm

shr_8_16_32 iemAImpl_shr_u8,         8, 1, b
shr_8_16_32 iemAImpl_shr_u8_intel,   8, 1, b
shr_8_16_32 iemAImpl_shr_u8_amd,     8, 0, b

shr_8_16_32 iemAImpl_shr_u16,       16, 1, h
shr_8_16_32 iemAImpl_shr_u16_intel, 16, 1, h
shr_8_16_32 iemAImpl_shr_u16_amd,   16, 0, h

shr_8_16_32 iemAImpl_shr_u32,       32, 1,
shr_8_16_32 iemAImpl_shr_u32_intel, 32, 1,
shr_8_16_32 iemAImpl_shr_u32_amd,   32, 0,

/** @todo this is slightly slower than the C version (release) on an M2. Investigate why. */
/* void iemAImpl_shr_u64(uint16_t *pu64Dst, uint8_t cShift, uint32_t *pEFlags); */
.macro shr_64, a_Name, a_fIntelFlags
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN \a_Name
        .cfi_startproc

        /* Do we need to shift anything at all? */
        ands    w2, w2, #0x3f
        b.eq    99f

        /*
         * Do the shifting
         */
        ldr     x8, [x1]
        lsrv    x9, x8, x2
        str     x9, [x1]

        /*
         * Calculate EFLAGS.
         */
        sub     w11, w2, #1
        lsrv    x11, x8, x11
        bfxil   w0, w11, #X86_EFL_CF_BIT, #1

.ifne \a_fIntelFlags
        and     w0, w0, ~X86_EFL_AF             /* AF is cleared */
        /* Intel: OF = one bit shift: fEfl |= X86_EFL_GET_OF_ ## cOpBits(uDstIn); */
        lsr     x11, x8, #63
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.else
        orr     w0, w0, X86_EFL_AF              /* AF is set  */
        /* AMD: OF = last bits shifted: fEfl |= (uResult >> (cOpBits - 2)) << X86_EFL_OF_BIT;  */
        lsr     x11, x9, #62
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.endif

        CALC_EFLAGS_PARITY w0, w9, w11

        ands    xzr, x9, x9                     /* Sets NZ */
        mrs     x11, NZCV
        lsr     w11, w11, #30                   /* N=1; Z=0 */
        bfi     w0, w11, X86_EFL_ZF_BIT, 2      /* EFLAGS.ZF and EFLAGS.SF */

99:
        ret
        .cfi_endproc
.endm

shr_64 iemAImpl_shr_u64,       1
shr_64 iemAImpl_shr_u64_intel, 1
shr_64 iemAImpl_shr_u64_amd,   0


/*
 * Shift Right, Signed
 */

/* uint32_t iemAImpl_sar_u8( uint32_t fEFlagsIn, uint8_t *pu8Dst, uint8_t cShift); */
/* uint32_t iemAImpl_sar_u16(uint32_t fEFlagsIn, uint16_t *pu16Dst, uint8_t cShift); */
/* uint32_t iemAImpl_sar_u32(uint32_t fEFlagsIn, uint16_t *pu32Dst, uint8_t cShift); */
.macro sar_8_16_32, a_Name, a_cBits, a_fIntelFlags, a_LdSuff, a_StSuff
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN \a_Name
        .cfi_startproc

        /* Do we need to shift anything at all? */
        and     w2, w2, #0x1f
        cbz     w2, 99f

        /*
         * Do the shifting.
         */
        ldr\a_LdSuff  w8, [x1]                  /* Sign-extending for 8 and 16 bits! */
        asrv    w9, w8, w2
        str\a_StSuff  w9, [x1]

        /*
         * Calculate EFLAGS.
         */
        sub     w11, w2, #1
        lsrv    w11, w8, w11
        bfxil   w0, w11, #X86_EFL_CF_BIT, #1

.ifne \a_fIntelFlags
        mov     w11, ~(X86_EFL_AF | X86_EFL_OF)
        and     w0, w0, w11                     /* AF and OF are cleared */
.else
        orr     w0, w0, X86_EFL_AF              /* AF is set  */
        and     w0, w0, ~X86_EFL_OF             /* OF is cleared */
.endif

        CALC_EFLAGS_PARITY w0, w9, w11

.ifne \a_cBits < 32
        setf\a_cBits w9                         /* Sets NZ */
.else
        ands    wzr, w9, w9                     /* Sets NZ */
.endif
        mrs     x11, NZCV
        lsr     w11, w11, #30                   /* N=1; Z=0 */
        bfi     w0, w11, X86_EFL_ZF_BIT, 2      /* EFLAGS.ZF and EFLAGS.SF */

99:
        ret
        .cfi_endproc
.endm

sar_8_16_32 iemAImpl_sar_u8,         8, 1, sb, b
sar_8_16_32 iemAImpl_sar_u8_intel,   8, 1, sb, b
sar_8_16_32 iemAImpl_sar_u8_amd,     8, 0, sb, b

sar_8_16_32 iemAImpl_sar_u16,       16, 1, sh, h
sar_8_16_32 iemAImpl_sar_u16_intel, 16, 1, sh, h
sar_8_16_32 iemAImpl_sar_u16_amd,   16, 0, sh, h

sar_8_16_32 iemAImpl_sar_u32,       32, 1, ,
sar_8_16_32 iemAImpl_sar_u32_intel, 32, 1, ,
sar_8_16_32 iemAImpl_sar_u32_amd,   32, 0, ,

/** @todo this is slightly slower than the C version (release) on an M2. Investigate why. */
/* uint32_t iemAImpl_sar_u64(uint32_t fEFlagsIn, uint16_t *pu64Dst, uint8_t cShift); */
.macro sar_64, a_Name, a_fIntelFlags
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN \a_Name
        .cfi_startproc

        /* Do we need to shift anything at all? */
        ands    w2, w2, #0x3f
        b.eq    99f

        /*
         * Do the shifting
         */
        ldr     x8, [x1]
        asrv    x9, x8, x2
        str     x9, [x1]

        /*
         * Calculate EFLAGS.
         */
        sub     w11, w2, #1
        lsrv    x11, x8, x11
        bfxil   w0, w11, #X86_EFL_CF_BIT, #1

.ifne \a_fIntelFlags
        mov     w11, ~(X86_EFL_AF | X86_EFL_OF)
        and     w0, w0, w11                     /* AF and OF are cleared */
.else
        orr     w0, w0, X86_EFL_AF              /* AF is set  */
        and     w0, w0, ~X86_EFL_OF             /* OF is cleared */
.endif

        CALC_EFLAGS_PARITY w0, w9, w11

        ands    xzr, x9, x9                     /* Sets NZ */
        mrs     x11, NZCV
        lsr     w11, w11, #30                   /* N=1; Z=0 */
        bfi     w0, w11, X86_EFL_ZF_BIT, 2      /* EFLAGS.ZF and EFLAGS.SF */

99:
        ret
        .cfi_endproc
.endm

sar_64 iemAImpl_sar_u64,       1
sar_64 iemAImpl_sar_u64_intel, 1
sar_64 iemAImpl_sar_u64_amd,   0


/*
 * Rotate Left.
 */

/* uint32_t iemAImpl_rol_u8( uint32_t fEFlagsIn, uint8_t *pu8Dst, uint8_t cShift); */
/* uint32_t iemAImpl_rol_u16(uint32_t fEFlagsIn, uint16_t *pu16Dst, uint8_t cShift); */
/* uint32_t iemAImpl_rol_u32(uint32_t fEFlagsIn, uint16_t *pu32Dst, uint8_t cShift); */
.macro ROL_8_16_32, a_Name, a_cBits, a_fIntelFlags, a_LdStSuff
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN \a_Name
        .cfi_startproc

        /* Do we need to rotate anything at all? */
        and     w2, w2, #0x1f
        cbz     w2, 99f

        /*
         * Do the shifting
         */
.ifne \a_cBits < 32
        and     w2, w2, #(\a_cBits - 1)
        neg     w3, w2                          /* the count is MODed by the data size, so this is safe. */
        ldr\a_LdStSuff  w8, [x1]
        orr     w8, w8, w8, LSL #(32 - \a_cBits) /* place a copy of the value at the top of the register, ready to be roated in */
        rorv    w9, w8, w3
        str\a_LdStSuff  w9, [x1]
.else
        neg     w3, w2                          /* the count is MODed by the data size, so this is safe. */
        ldr\a_LdStSuff  w8, [x1]
        rorv    w9, w8, w3
        str\a_LdStSuff  w9, [x1]
.endif

        /*
         * Calculate EFLAGS - only CF and OF.
         */
        bfi     w0, w9, #0, #1                  /* CF = last bit rotated around (new bottom bit) */

.ifne \a_fIntelFlags
        /* Intel: OF = first rotate step: fEfl |= X86_EFL_GET_OF_ ## cOpBits(uDst ^ (uDst << 1)); */
        eor     w11, w8, w8, LSL #1
        lsr     w11, w11, #(\a_cBits - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.else
        /* AMD: OF = last rotate step: fEfl |= ((uResult >> (cOpBits - 1)) ^ fCarry) << X86_EFL_OF_BIT;  */
        eor     w11, w0, w9, LSR #(\a_cBits - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.endif

99:
        ret
        .cfi_endproc
.endm

ROL_8_16_32 iemAImpl_rol_u8,         8, 1, b
ROL_8_16_32 iemAImpl_rol_u8_intel,   8, 1, b
ROL_8_16_32 iemAImpl_rol_u8_amd,     8, 0, b

ROL_8_16_32 iemAImpl_rol_u16,       16, 1, h
ROL_8_16_32 iemAImpl_rol_u16_intel, 16, 1, h
ROL_8_16_32 iemAImpl_rol_u16_amd,   16, 0, h

ROL_8_16_32 iemAImpl_rol_u32,       32, 1,
ROL_8_16_32 iemAImpl_rol_u32_intel, 32, 1,
ROL_8_16_32 iemAImpl_rol_u32_amd,   32, 0,

/** @todo this is slightly slower than the C version (release) on an M2. Investigate why. */
/* uint32_t iemAImpl_rol_u64(uint32_t fEFlagsIn, uint16_t *pu64Dst, uint8_t cShift); */
.macro ROL_64, a_Name, a_fIntelFlags
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN \a_Name
        .cfi_startproc

        /* Do we need to shift anything at all? */
        and     w2, w2, #0x3f
        cbz     w2, 99f

        /*
         * Do the shifting
         */
        neg     w3, w2
        ldr     x8, [x1]
        rorv    x9, x8, x3
        str     x9, [x1]

        /*
         * Calculate EFLAGS - only CF and OF.
         */
        bfi     w0, w9, #0, #1                  /* CF = last bit rotated around */

.ifne \a_fIntelFlags
        /* Intel: OF = first rotate step: fEfl |= X86_EFL_GET_OF_ ## cOpBits(uDst ^ (uDst << 1)); */
        eor     x11, x8, x8, LSL #1
        lsr     x11, x11, #(64 - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.else
        /* AMD: OF = last rotate step: fEfl |= ((uResult >> (cOpBits - 1)) ^ fCarry) << X86_EFL_OF_BIT;  */
        eor     x11, x0, x9, LSR #(64 - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.endif

99:
        ret
        .cfi_endproc
.endm

ROL_64 iemAImpl_rol_u64,       1
ROL_64 iemAImpl_rol_u64_intel, 1
ROL_64 iemAImpl_rol_u64_amd,   0


/*
 * Rotate Right.
 */

/* uint32_t iemAImpl_ror_u8( uint32_t fEFlagsIn, uint8_t *pu8Dst, uint8_t cShift); */
/* uint32_t iemAImpl_ror_u16(uint32_t fEFlagsIn, uint16_t *pu16Dst, uint8_t cShift); */
/* uint32_t iemAImpl_ror_u32(uint32_t fEFlagsIn, uint16_t *pu32Dst, uint8_t cShift); */
.macro ROR_8_16_32, a_Name, a_cBits, a_fIntelFlags, a_LdStSuff
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN \a_Name
        .cfi_startproc

        /* Do we need to rotate anything at all? */
        and     w2, w2, #0x1f
        cbz     w2, 99f

        /*
         * Do the shifting
         */
.ifne \a_cBits < 32
        and     w2, w2, #(\a_cBits - 1)
        ldr\a_LdStSuff  w8, [x1]
        orr     w8, w8, w8, LSL #(\a_cBits)  /* duplicate value above, so it is ready to be shifted in. */
        lsrv    w9, w8, w2
        str\a_LdStSuff  w9, [x1]
.else
        ldr\a_LdStSuff  w8, [x1]
        rorv    w9, w8, w2
        str\a_LdStSuff  w9, [x1]
.endif

        /*
         * Calculate EFLAGS - only CF and OF.
         */
        bfxil   w0, w9, #(\a_cBits - 1), #1     /* CF = last bit rotated around (new top bit) */

.ifne \a_fIntelFlags
        /* Intel: OF = first rotate step: X86_EFL_GET_OF_ ## a_cBitsWidth(uDst ^ (uDst << (a_cBitsWidth - 1))); */
        eor     w11, w8, w8, LSR #(\a_cBits - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.else
        /* AMD: OF = last rotate step: fEFlags |= (((uResult >> ((a_cBitsWidth) - 2)) ^ fCarry) & 1) << X86_EFL_OF_BIT; */
        eor     w11, w0, w9, LSR #(\a_cBits - 2)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.endif

99:
        ret
        .cfi_endproc
.endm

ROR_8_16_32 iemAImpl_ror_u8,         8, 1, b
ROR_8_16_32 iemAImpl_ror_u8_intel,   8, 1, b
ROR_8_16_32 iemAImpl_ror_u8_amd,     8, 0, b

ROR_8_16_32 iemAImpl_ror_u16,       16, 1, h
ROR_8_16_32 iemAImpl_ror_u16_intel, 16, 1, h
ROR_8_16_32 iemAImpl_ror_u16_amd,   16, 0, h

ROR_8_16_32 iemAImpl_ror_u32,       32, 1,
ROR_8_16_32 iemAImpl_ror_u32_intel, 32, 1,
ROR_8_16_32 iemAImpl_ror_u32_amd,   32, 0,

/** @todo this is slightly slower than the C version (release) on an M2. Investigate why. */
/* uint32_t iemAImpl_ror_u64(uint32_t fEFlagsIn, uint16_t *pu64Dst, uint8_t cShift); */
.macro ROR_64, a_Name, a_fIntelFlags
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN \a_Name
        .cfi_startproc

        /* Do we need to shift anything at all? */
        and     w2, w2, #0x3f
        cbz     w2, 99f

        /*
         * Do the shifting
         */
        ldr     x8, [x1]
        rorv    x9, x8, x2
        str     x9, [x1]

        /*
         * Calculate EFLAGS - only CF and OF.
         */
        bfxil   x0, x9, #(64 - 1), #1           /* CF = last bit rotated around (new top bit) */

.ifne \a_fIntelFlags
        /* Intel: OF = first rotate step: X86_EFL_GET_OF_ ## a_cBitsWidth(uDst ^ (uDst << (a_cBitsWidth - 1))); */
        eor     x11, x8, x8, LSR #(64 - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.else
        /* AMD: OF = last rotate step: fEFlags |= (((uResult >> ((a_cBitsWidth) - 2)) ^ fCarry) & 1) << X86_EFL_OF_BIT; */
        eor     x11, x0, x9, LSR #(64 - 2)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.endif

99:
        ret
        .cfi_endproc
.endm

ROR_64 iemAImpl_ror_u64,       1
ROR_64 iemAImpl_ror_u64_intel, 1
ROR_64 iemAImpl_ror_u64_amd,   0


/*
 * Rotate Left thru Carry.
 */

/* uint32_t iemAImpl_rcl_u8( uint32_t fEFlagsIn, uint8_t *pu8Dst, uint8_t cShift); */
/* uint32_t iemAImpl_rcl_u16(uint32_t fEFlagsIn, uint16_t *pu16Dst, uint8_t cShift); */
/* uint32_t iemAImpl_rcl_u32(uint32_t fEFlagsIn, uint16_t *pu32Dst, uint8_t cShift); */
.macro RCL_8_16_32, a_Name, a_cBits, a_fIntelFlags, a_LdStSuff
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN \a_Name
        .cfi_startproc

        /* Do we need to rotate anything at all? */
        and     w2, w2, #0x1f
.ifne \a_cBits >= 32
        cbz     w2, 99f
.else
 .ifeq \a_fIntelFlags
        cbz     w2, 99f                     /* AMD */
 .endif

        /*
         * 8 and 16 bit: w2 = w2 % (a_cBits + 1).
         *
         * Given that the w2 range is 0 thru 31, the 16-bit case can be reduced
         * to:
         *      w2 = w2 >= 17 ? w2 - 17 : w2
         *
         * In the 8-bit scenario we're modding with 9, so we need to do it in
         * two steps:
         *      w2 = w2 >= 18 ? w2 - 18 : w2
         *      w2 = w2 >= 9  ? w2 - 9  : w2
         *
         * For comparison clang generates the following for 16-bit:
         *      mov     w9, #0xf0f0f0f1
         *      umull   x9, w2, w9
         *      lsr     x9, x9, #36
         *      bfi     w9, w9, #4, #1
         *      sub     w2, w2, w9
         *
         * The 8-bit variant is differs only in the constants used:
         *      mov     w9, #0x38e38e39
         *      umull   x9, w2, w9
         *      lsr     x9, x9, #33
         *      bfi     w9, w9, #3, #2
         *      subs    w8, w2, w9
         */
 .ifne \a_cBits == 16
        subs    w3, w2, #17
        csel    w2, w3, w2, hs
 .else
        subs    w3, w2, #18
        csel    w2, w3, w2, hs
        subs    w3, w2, #9
        csel    w2, w3, w2, hs
 .endif
 .ifne \a_fIntelFlags
        cbz     w2, 99f                     /* Intel: Skip everything if the modded rotate count is zero. */
 .endif
.endif

        /*
         * Do the rotating: x9 = RORV(w8[0:a_cBits-1] | (CF << 63) | (w8[1:a_cBits-1] << (64-a_cBits-1)) | (CF << a_cBits)), -w2)
         */
        neg     w2, w2                      /* w3 = rorv count - this will be masked by 0x3f so it's the same as 64-w2. */

        ldr\a_LdStSuff  w8, [x1]
 .ifne \a_cBits < 32
        orr     x8, x8, x8, LSL #(64 - \a_cBits - 1)
  .ifeq \a_fIntelFlags
        bfi     x8, x0, #(\a_cBits), #1     /* AMD: w8[a_cBits] = CF; Avoids conditional branch for CF calc to cover cShift==0. */
  .endif
 .else
        lsr     w9, w8, #1
        orr     x8, x8, x9, LSL #(64 - \a_cBits)
 .endif
        bfi     x8, x0, #63, #1             /* w8[63] = CF */
        rorv    x9, x8, x2
        str\a_LdStSuff w9, [x1]

        /*
         * Calculate EFLAGS - only CF and OF.
         */
        bfxil   x0, x9, #(\a_cBits), #1     /* CF = last bit rotated 'out' */

.ifne \a_fIntelFlags
        /* Intel: OF = first rotate step: fEfl |= X86_EFL_GET_OF_ ## cOpBits(uDst ^ (uDst << 1)); */
        eor     w11, w8, w8, LSL #1
        lsr     w11, w11, #(\a_cBits - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.else
        /* AMD: OF = last rotate step: fEfl |= ((uResult >> (cOpBits - 1)) ^ fCarry) << X86_EFL_OF_BIT;  */
        eor     w11, w0, w9, LSR #(\a_cBits - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.endif

99:
        ret
        .cfi_endproc
.endm

RCL_8_16_32 iemAImpl_rcl_u8,         8, 1, b
RCL_8_16_32 iemAImpl_rcl_u8_intel,   8, 1, b
RCL_8_16_32 iemAImpl_rcl_u8_amd,     8, 0, b

RCL_8_16_32 iemAImpl_rcl_u16,       16, 1, h
RCL_8_16_32 iemAImpl_rcl_u16_intel, 16, 1, h
RCL_8_16_32 iemAImpl_rcl_u16_amd,   16, 0, h

RCL_8_16_32 iemAImpl_rcl_u32,       32, 1,
RCL_8_16_32 iemAImpl_rcl_u32_intel, 32, 1,
RCL_8_16_32 iemAImpl_rcl_u32_amd,   32, 0,

/** @todo this is slightly slower than the C version (release) on an M2. Investigate why. */
/* uint32_t iemAImpl_rcl_u64(uint32_t fEFlagsIn, uint16_t *pu64Dst, uint8_t cShift); */
.macro RCL_64, a_Name, a_fIntelFlags
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN \a_Name
        .cfi_startproc

        /* Do we need to shift anything at all? */
        and     w2, w2, #0x3f
        cbz     w2, 99f /** @todo eliminate this for < 32 shift with intel flags */

        /*
         * Do the rotating: (w8 << w2) | (CF << (w2 - 1)) | (w2 > 1 ? (w8 >> (64 - w2 + 1)) : 0)
         */
        and     w3, w0, #X86_EFL_CF
        subs    w4, w2, #1                  /* Also: prep for 'w2 > 1' (w2 can't be zero, btw) - think: cmp w2, #1 */
        lslv    x3, x3, x4                  /* x3 = CF << (w2 - 1) */

        mov     w4, #(64 + 1)
        sub     w4, w4, w2                  /* w4 = 64 - w2 + 1 */

        ldr     x8, [x1]
        lslv    x9, x8, x2
        lsrv    x10, x8, x4
        csel    x10, xzr, x10, eq           /* if w2 == 1: x10 = 0; else: x10 = x8 >> (64 - w2 + 1); */
        orr     x9, x9, x3                  /* shifted CF */
        orr     x9, x9, x10
        str     x9, [x1]

        /*
         * Calculate EFLAGS - only CF and OF.
         */
        neg     x11, x2
        lsr     x11, x8, x11
        bfi     w0, w11, #0, #1            /* CF = last bit rotated out. */

.ifne \a_fIntelFlags
        /* Intel: OF = first rotate step: fEfl |= X86_EFL_GET_OF_ ## cOpBits(uDst ^ (uDst << 1)); */
        eor     x11, x8, x8, LSL #1
        lsr     x11, x11, #(64 - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.else
        /* AMD: OF = last rotate step: fEfl |= ((uResult >> (cOpBits - 1)) ^ fCarry) << X86_EFL_OF_BIT;  */
        eor     x11, x0, x9, LSR #(64 - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.endif

99:
        ret
        .cfi_endproc
.endm

RCL_64 iemAImpl_rcl_u64,       1
RCL_64 iemAImpl_rcl_u64_intel, 1
RCL_64 iemAImpl_rcl_u64_amd,   0


/*
 * Rotate Right thru Carry.
 */

/* uint32_t iemAImpl_rcr_u8( uint32_t fEFlagsIn, uint8_t *pu8Dst, uint8_t cShift); */
/* uint32_t iemAImpl_rcr_u16(uint32_t fEFlagsIn, uint16_t *pu16Dst, uint8_t cShift); */
/* uint32_t iemAImpl_rcr_u32(uint32_t fEFlagsIn, uint16_t *pu32Dst, uint8_t cShift); */
.macro RCR_8_16_32, a_Name, a_cBits, a_fIntelFlags, a_LdStSuff
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN \a_Name
        .cfi_startproc

        /* Do we need to rotate anything at all? */
        and     w2, w2, #0x1f
.ifne \a_cBits >= 32
        cbz     w2, 99f
.else
 .ifeq \a_fIntelFlags
        cbz     w2, 99f                     /* AMD */
 .endif

        /*
         * 8 and 16 bit: w2 = w2 % (a_cBits + 1). (See RCL for details.)
         */
 .ifne \a_cBits == 16
        subs    w3, w2, #17
        csel    w2, w3, w2, hs
 .else
        subs    w3, w2, #18
        csel    w2, w3, w2, hs
        subs    w3, w2, #9
        csel    w2, w3, w2, hs
 .endif
 .ifne \a_fIntelFlags
        cbz     w2, 99f                     /* Intel: Skip everything if the modded rotate count is zero. */
 .endif
.endif

        /*
         * Do the rotating: x9 = RORV(x8[0:a_cBits-1] | (CF << a_cBits) | ((x8 << (a_cBits + 2)) >> 1) | (CF << 63), x2)
         */
        add     w3, w2, #1                  /* w3 = w2 + 1 */

        subs    w4, w2, #1
        mov     w5, #(\a_cBits)
        csel    w4, w5, w5, lo              /* w4 = w2 >= 1 ? w2 - 1 : a_cBits - for CF extraction */

        ldr\a_LdStSuff  w8, [x1]
        bfi     x8, x0, #(\a_cBits), #1     /* Put CF above the input. */
        bfi     x8, x8, #(\a_cBits + 1), #(64 - \a_cBits - 1) /* Put repeat the register content above that again. */
.ifne \a_cBits < 32
 .ifeq \a_fIntelFlags
        bfi     x8, x0, #63, #1             /* AMD 8- and 16-bit: Put CF at the very top so w2 == 0 works w/o branching. */
 .endif
.endif
        rorv    x9, x8, x2
        str\a_LdStSuff w9, [x1]

        /*
         * Calculate EFLAGS - only CF and OF.
         */
        bfxil   x0, x9, #63, #1             /* CF = last bit rotated 'out'  */

.ifne \a_fIntelFlags
        /* Intel: OF = first rotate step: fEFlags |= (fInCarry ^ (uint32_t)(uDst >> (a_cBits - 1))) << X86_EFL_OF_BIT; */
        eor     x11, x8, x8, LSR #1         /* We've got CF in bit #a_cBits in x8 */
        lsr     w11, w11, #(\a_cBits - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.else
        /* AMD: OF = last rotate step: fEFlags |= X86_EFL_GET_OF_ ## a_cBits(uResult ^ (uResult << 1)); */
        eor     w11, w9, w9, LSL #1
        lsr     w11, w11, #(\a_cBits - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.endif

99:
        ret
        .cfi_endproc
.endm

RCR_8_16_32 iemAImpl_rcr_u8,         8, 1, b
RCR_8_16_32 iemAImpl_rcr_u8_intel,   8, 1, b
RCR_8_16_32 iemAImpl_rcr_u8_amd,     8, 0, b

RCR_8_16_32 iemAImpl_rcr_u16,       16, 1, h
RCR_8_16_32 iemAImpl_rcr_u16_intel, 16, 1, h
RCR_8_16_32 iemAImpl_rcr_u16_amd,   16, 0, h

RCR_8_16_32 iemAImpl_rcr_u32,       32, 1,
RCR_8_16_32 iemAImpl_rcr_u32_intel, 32, 1,
RCR_8_16_32 iemAImpl_rcr_u32_amd,   32, 0,

/** @todo this is slightly slower than the C version (release) on an M2. Investigate why. */
/* uint32_t iemAImpl_rcr_u64(uint32_t fEFlagsIn, uint16_t *pu64Dst, uint8_t cShift); */
.macro RCR_64, a_Name, a_fIntelFlags
ALIGNCODE(IEM_AIMPL_FUNCTION_ALIGNMENT)
BEGINPROC_HIDDEN \a_Name
        .cfi_startproc

        /* Do we need to shift anything at all? */
        and     w2, w2, #0x3f
        cbz     w2, 99f

        /*
         * Do the rotating: (w8 >> w2) | (CF << (64 - w2)) | (w2 > 1 ? (w8 << (64 - w2 + 1)) : 0)
         */
        and     w5, w0, #X86_EFL_CF         /* x5 = input CF - for intel OF calc */
        neg     w4, w2
        lslv    x3, x5, x4                  /* x3 = CF << (64 - w2) */

        cmp     w2, #1                      /* prep for w2 > 1 */
        add     w4, w4, #1                  /* w4 = -w2 + 1; which when & 0x3f =^= 64 - 2 + 1 */

        ldr     x8, [x1]
        lsrv    x9, x8, x2
        lslv    x10, x8, x4
        csel    x10, xzr, x10, eq           /* if w2 == 1: x10 = 0; else: x10 = x8 << (64 - w2 + 1); */
        orr     x9, x9, x3                  /* shifted CF */
        orr     x9, x9, x10
        str     x9, [x1]

        /*
         * Calculate EFLAGS - only CF and OF.
         */
        sub     x11, x2, #1
        lsr     x11, x8, x11
        bfi     w0, w11, #0, #1            /* CF = last bit rotated out. */

.ifne \a_fIntelFlags
        /* Intel: OF = first rotate step: fEFlags |= (fInCarry ^ (uint32_t)(uDst >> (a_cBits - 1))) << X86_EFL_OF_BIT; */
        eor     x11, x5, x8, LSR #63
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.else
        /* AMD: OF = last rotate step: fEFlags |= X86_EFL_GET_OF_ ## a_cBits(uResult ^ (uResult << 1)); */
        eor     x11, x9, x9, LSL #1
        lsr     x11, x11, #(64 - 1)
        bfi     w0, w11, #X86_EFL_OF_BIT, #1
.endif

99:
        ret
        .cfi_endproc
.endm

RCR_64 iemAImpl_rcr_u64,       1
RCR_64 iemAImpl_rcr_u64_intel, 1
RCR_64 iemAImpl_rcr_u64_amd,   0

