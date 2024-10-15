/* $Id: IEMAllAImplC.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Instruction Implementation in Assembly, portable C variant.
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
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <iprt/errcore.h>
#include <iprt/x86.h>
#include <iprt/uint128.h>
#include <iprt/uint256.h>
#include <iprt/crc.h>
#include <iprt/string.h>

RT_C_DECLS_BEGIN
#include <softfloat.h>
RT_C_DECLS_END


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def IEM_WITHOUT_ASSEMBLY
 * Enables all the code in this file.
 */
#if !defined(IEM_WITHOUT_ASSEMBLY)
# if defined(RT_ARCH_ARM32) || defined(RT_ARCH_ARM64) || defined(DOXYGEN_RUNNING)
#  define IEM_WITHOUT_ASSEMBLY
# endif
#endif
/* IEM_WITH_ASSEMBLY trumps IEM_WITHOUT_ASSEMBLY for tstIEMAImplAsm purposes. */
#ifdef IEM_WITH_ASSEMBLY
# undef IEM_WITHOUT_ASSEMBLY
#endif

/**
 * Calculates the signed flag value given a result and it's bit width.
 *
 * The signed flag (SF) is a duplication of the most significant bit in the
 * result.
 *
 * @returns X86_EFL_SF or 0.
 * @param   a_uResult       Unsigned result value.
 * @param   a_cBitsWidth    The width of the result (8, 16, 32, 64).
 */
#define X86_EFL_CALC_SF(a_uResult, a_cBitsWidth) \
    ( (uint32_t)((a_uResult) >> ((a_cBitsWidth) - X86_EFL_SF_BIT - 1)) & X86_EFL_SF )

/**
 * Calculates the zero flag value given a result.
 *
 * The zero flag (ZF) indicates whether the result is zero or not.
 *
 * @returns X86_EFL_ZF or 0.
 * @param   a_uResult       Unsigned result value.
 */
#define X86_EFL_CALC_ZF(a_uResult) \
    ( (uint32_t)((a_uResult) == 0) << X86_EFL_ZF_BIT )

/**
 * Calculates the parity flag.
 *
 * @returns X86_EFL_PF or 0.
 * @param   a_uResult       Unsigned result value.
 */
#if !defined(RT_ARCH_ARM64) || 1 /** @todo profile this... micro benching in tstIEMAImpl indicates no gain, but it may be skewed. */
# define IEM_EFL_CALC_PARITY(a_uResult) (g_afParity[(a_uResult) & 0xff])
#else
# define IEM_EFL_CALC_PARITY(a_uResult) iemAImplCalcParity(a_uResult)
DECL_FORCE_INLINE(uint32_t) iemAImplCalcParity(uint32_t uResult)
{
    /* Emulate 8-bit pop count.  This translates to 4 EOR instructions on
       ARM64 as they can shift the 2nd source operand. */
    uint8_t bPf = uResult ^ (uResult >> 4);
    bPf ^= bPf >> 2;
    bPf ^= bPf >> 1;
    bPf ^= 1;
    return (bPf & 1) << X86_EFL_PF_BIT;
}
#endif

/**
 * Extracts the OF flag from a OF calculation result.
 *
 * These are typically used by concating with a bitcount.  The problem is that
 * 8-bit values needs shifting in the other direction than the others.
 */
#define X86_EFL_GET_OF_8(a_uValue)  (((uint32_t)(a_uValue) << (X86_EFL_OF_BIT - 8 + 1))  & X86_EFL_OF)
#define X86_EFL_GET_OF_16(a_uValue) ((uint32_t)((a_uValue) >> (16 - X86_EFL_OF_BIT - 1)) & X86_EFL_OF)
#define X86_EFL_GET_OF_32(a_uValue) ((uint32_t)((a_uValue) >> (32 - X86_EFL_OF_BIT - 1)) & X86_EFL_OF)
#define X86_EFL_GET_OF_64(a_uValue) ((uint32_t)((a_uValue) >> (64 - X86_EFL_OF_BIT - 1)) & X86_EFL_OF)

/**
 * Updates the status bits (CF, PF, AF, ZF, SF, and OF) after arithmetic op.
 *
 * @returns Status bits.
 * @param   a_fEFlagsVar    The 32-bit EFLAGS variable to update.
 * @param   a_uResult       Unsigned result value.
 * @param   a_uSrc          The source value (for AF calc).
 * @param   a_uDst          The original destination value (for AF+OF calc).
 * @param   a_cBitsWidth    The width of the result (8, 16, 32, 64).
 * @param   a_CfExpr        Bool expression for the carry flag (CF).
 * @param   a_uSrcOf        The a_uSrc value to use for overflow calculation.
 */
#define IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(a_fEFlagsVar, a_uResult, a_uDst, a_uSrc, a_cBitsWidth, a_CfExpr, a_uSrcOf) \
    do { \
        a_fEFlagsVar &= ~X86_EFL_STATUS_BITS; \
        a_fEFlagsVar |= (a_CfExpr) << X86_EFL_CF_BIT; \
        a_fEFlagsVar |= IEM_EFL_CALC_PARITY(a_uResult); \
        a_fEFlagsVar |= ((uint32_t)(a_uResult) ^ (uint32_t)(a_uSrc) ^ (uint32_t)(a_uDst)) & X86_EFL_AF; \
        a_fEFlagsVar |= X86_EFL_CALC_ZF(a_uResult); \
        a_fEFlagsVar |= X86_EFL_CALC_SF(a_uResult, a_cBitsWidth); \
        \
        /* Overflow during ADDition happens when both inputs have the same signed \
           bit value and the result has a different sign bit value. \
           \
           Since subtraction can be rewritten as addition: 2 - 1 == 2 + -1, it \
           follows that for SUBtraction the signed bit value must differ between \
           the two inputs and the result's signed bit diff from the first input. \
           Note! Must xor with sign bit to convert, not do (0 - a_uSrc). \
           \
           See also: http://teaching.idallen.com/dat2343/10f/notes/040_overflow.txt */ \
        a_fEFlagsVar |= X86_EFL_GET_OF_ ## a_cBitsWidth(  (  ((uint ## a_cBitsWidth ## _t)~((a_uDst) ^ (a_uSrcOf))) \
                                                           & RT_BIT_64(a_cBitsWidth - 1)) \
                                                        & ((a_uResult) ^ (a_uDst)) ); \
    } while (0)

/**
 * Updates the status bits (CF, PF, AF, ZF, SF, and OF) after a logical op.
 *
 * CF and OF are defined to be 0 by logical operations.  AF on the other hand is
 * undefined.  We clear AF, as that seems to make the most sense and also seems
 * to be the correct behavior on current CPUs.
 *
 * @returns Status bits.
 * @param   a_fEFlagsVar    The 32-bit EFLAGS variable to update.
 * @param   a_uResult       Unsigned result value.
 * @param   a_cBitsWidth    The width of the result (8, 16, 32, 64).
 * @param   a_fExtra        Additional bits to set.
 */
#define IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(a_fEFlagsVar, a_uResult, a_cBitsWidth, a_fExtra) \
    do { \
        a_fEFlagsVar &= ~X86_EFL_STATUS_BITS; \
        a_fEFlagsVar |= IEM_EFL_CALC_PARITY(a_uResult); \
        a_fEFlagsVar |= X86_EFL_CALC_ZF(a_uResult); \
        a_fEFlagsVar |= X86_EFL_CALC_SF(a_uResult, a_cBitsWidth); \
        a_fEFlagsVar |= (a_fExtra); \
    } while (0)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Parity calculation table.
 *
 * This is also used by iemAllAImpl.asm.
 *
 * The generator code:
 * @code
 * #include <stdio.h>
 *
 * int main()
 * {
 *     unsigned b;
 *     for (b = 0; b < 256; b++)
 *     {
 *         int cOnes = ( b       & 1)
 *                   + ((b >> 1) & 1)
 *                   + ((b >> 2) & 1)
 *                   + ((b >> 3) & 1)
 *                   + ((b >> 4) & 1)
 *                   + ((b >> 5) & 1)
 *                   + ((b >> 6) & 1)
 *                   + ((b >> 7) & 1);
 *         printf("    /" "* %#04x = %u%u%u%u%u%u%u%ub *" "/ %s,\n",
 *                b,
 *                (b >> 7) & 1,
 *                (b >> 6) & 1,
 *                (b >> 5) & 1,
 *                (b >> 4) & 1,
 *                (b >> 3) & 1,
 *                (b >> 2) & 1,
 *                (b >> 1) & 1,
 *                 b       & 1,
 *                cOnes & 1 ? "0" : "X86_EFL_PF");
 *     }
 *     return 0;
 * }
 * @endcode
 */
uint8_t const g_afParity[256] =
{
    /* 0000 = 00000000b */ X86_EFL_PF,
    /* 0x01 = 00000001b */ 0,
    /* 0x02 = 00000010b */ 0,
    /* 0x03 = 00000011b */ X86_EFL_PF,
    /* 0x04 = 00000100b */ 0,
    /* 0x05 = 00000101b */ X86_EFL_PF,
    /* 0x06 = 00000110b */ X86_EFL_PF,
    /* 0x07 = 00000111b */ 0,
    /* 0x08 = 00001000b */ 0,
    /* 0x09 = 00001001b */ X86_EFL_PF,
    /* 0x0a = 00001010b */ X86_EFL_PF,
    /* 0x0b = 00001011b */ 0,
    /* 0x0c = 00001100b */ X86_EFL_PF,
    /* 0x0d = 00001101b */ 0,
    /* 0x0e = 00001110b */ 0,
    /* 0x0f = 00001111b */ X86_EFL_PF,
    /* 0x10 = 00010000b */ 0,
    /* 0x11 = 00010001b */ X86_EFL_PF,
    /* 0x12 = 00010010b */ X86_EFL_PF,
    /* 0x13 = 00010011b */ 0,
    /* 0x14 = 00010100b */ X86_EFL_PF,
    /* 0x15 = 00010101b */ 0,
    /* 0x16 = 00010110b */ 0,
    /* 0x17 = 00010111b */ X86_EFL_PF,
    /* 0x18 = 00011000b */ X86_EFL_PF,
    /* 0x19 = 00011001b */ 0,
    /* 0x1a = 00011010b */ 0,
    /* 0x1b = 00011011b */ X86_EFL_PF,
    /* 0x1c = 00011100b */ 0,
    /* 0x1d = 00011101b */ X86_EFL_PF,
    /* 0x1e = 00011110b */ X86_EFL_PF,
    /* 0x1f = 00011111b */ 0,
    /* 0x20 = 00100000b */ 0,
    /* 0x21 = 00100001b */ X86_EFL_PF,
    /* 0x22 = 00100010b */ X86_EFL_PF,
    /* 0x23 = 00100011b */ 0,
    /* 0x24 = 00100100b */ X86_EFL_PF,
    /* 0x25 = 00100101b */ 0,
    /* 0x26 = 00100110b */ 0,
    /* 0x27 = 00100111b */ X86_EFL_PF,
    /* 0x28 = 00101000b */ X86_EFL_PF,
    /* 0x29 = 00101001b */ 0,
    /* 0x2a = 00101010b */ 0,
    /* 0x2b = 00101011b */ X86_EFL_PF,
    /* 0x2c = 00101100b */ 0,
    /* 0x2d = 00101101b */ X86_EFL_PF,
    /* 0x2e = 00101110b */ X86_EFL_PF,
    /* 0x2f = 00101111b */ 0,
    /* 0x30 = 00110000b */ X86_EFL_PF,
    /* 0x31 = 00110001b */ 0,
    /* 0x32 = 00110010b */ 0,
    /* 0x33 = 00110011b */ X86_EFL_PF,
    /* 0x34 = 00110100b */ 0,
    /* 0x35 = 00110101b */ X86_EFL_PF,
    /* 0x36 = 00110110b */ X86_EFL_PF,
    /* 0x37 = 00110111b */ 0,
    /* 0x38 = 00111000b */ 0,
    /* 0x39 = 00111001b */ X86_EFL_PF,
    /* 0x3a = 00111010b */ X86_EFL_PF,
    /* 0x3b = 00111011b */ 0,
    /* 0x3c = 00111100b */ X86_EFL_PF,
    /* 0x3d = 00111101b */ 0,
    /* 0x3e = 00111110b */ 0,
    /* 0x3f = 00111111b */ X86_EFL_PF,
    /* 0x40 = 01000000b */ 0,
    /* 0x41 = 01000001b */ X86_EFL_PF,
    /* 0x42 = 01000010b */ X86_EFL_PF,
    /* 0x43 = 01000011b */ 0,
    /* 0x44 = 01000100b */ X86_EFL_PF,
    /* 0x45 = 01000101b */ 0,
    /* 0x46 = 01000110b */ 0,
    /* 0x47 = 01000111b */ X86_EFL_PF,
    /* 0x48 = 01001000b */ X86_EFL_PF,
    /* 0x49 = 01001001b */ 0,
    /* 0x4a = 01001010b */ 0,
    /* 0x4b = 01001011b */ X86_EFL_PF,
    /* 0x4c = 01001100b */ 0,
    /* 0x4d = 01001101b */ X86_EFL_PF,
    /* 0x4e = 01001110b */ X86_EFL_PF,
    /* 0x4f = 01001111b */ 0,
    /* 0x50 = 01010000b */ X86_EFL_PF,
    /* 0x51 = 01010001b */ 0,
    /* 0x52 = 01010010b */ 0,
    /* 0x53 = 01010011b */ X86_EFL_PF,
    /* 0x54 = 01010100b */ 0,
    /* 0x55 = 01010101b */ X86_EFL_PF,
    /* 0x56 = 01010110b */ X86_EFL_PF,
    /* 0x57 = 01010111b */ 0,
    /* 0x58 = 01011000b */ 0,
    /* 0x59 = 01011001b */ X86_EFL_PF,
    /* 0x5a = 01011010b */ X86_EFL_PF,
    /* 0x5b = 01011011b */ 0,
    /* 0x5c = 01011100b */ X86_EFL_PF,
    /* 0x5d = 01011101b */ 0,
    /* 0x5e = 01011110b */ 0,
    /* 0x5f = 01011111b */ X86_EFL_PF,
    /* 0x60 = 01100000b */ X86_EFL_PF,
    /* 0x61 = 01100001b */ 0,
    /* 0x62 = 01100010b */ 0,
    /* 0x63 = 01100011b */ X86_EFL_PF,
    /* 0x64 = 01100100b */ 0,
    /* 0x65 = 01100101b */ X86_EFL_PF,
    /* 0x66 = 01100110b */ X86_EFL_PF,
    /* 0x67 = 01100111b */ 0,
    /* 0x68 = 01101000b */ 0,
    /* 0x69 = 01101001b */ X86_EFL_PF,
    /* 0x6a = 01101010b */ X86_EFL_PF,
    /* 0x6b = 01101011b */ 0,
    /* 0x6c = 01101100b */ X86_EFL_PF,
    /* 0x6d = 01101101b */ 0,
    /* 0x6e = 01101110b */ 0,
    /* 0x6f = 01101111b */ X86_EFL_PF,
    /* 0x70 = 01110000b */ 0,
    /* 0x71 = 01110001b */ X86_EFL_PF,
    /* 0x72 = 01110010b */ X86_EFL_PF,
    /* 0x73 = 01110011b */ 0,
    /* 0x74 = 01110100b */ X86_EFL_PF,
    /* 0x75 = 01110101b */ 0,
    /* 0x76 = 01110110b */ 0,
    /* 0x77 = 01110111b */ X86_EFL_PF,
    /* 0x78 = 01111000b */ X86_EFL_PF,
    /* 0x79 = 01111001b */ 0,
    /* 0x7a = 01111010b */ 0,
    /* 0x7b = 01111011b */ X86_EFL_PF,
    /* 0x7c = 01111100b */ 0,
    /* 0x7d = 01111101b */ X86_EFL_PF,
    /* 0x7e = 01111110b */ X86_EFL_PF,
    /* 0x7f = 01111111b */ 0,
    /* 0x80 = 10000000b */ 0,
    /* 0x81 = 10000001b */ X86_EFL_PF,
    /* 0x82 = 10000010b */ X86_EFL_PF,
    /* 0x83 = 10000011b */ 0,
    /* 0x84 = 10000100b */ X86_EFL_PF,
    /* 0x85 = 10000101b */ 0,
    /* 0x86 = 10000110b */ 0,
    /* 0x87 = 10000111b */ X86_EFL_PF,
    /* 0x88 = 10001000b */ X86_EFL_PF,
    /* 0x89 = 10001001b */ 0,
    /* 0x8a = 10001010b */ 0,
    /* 0x8b = 10001011b */ X86_EFL_PF,
    /* 0x8c = 10001100b */ 0,
    /* 0x8d = 10001101b */ X86_EFL_PF,
    /* 0x8e = 10001110b */ X86_EFL_PF,
    /* 0x8f = 10001111b */ 0,
    /* 0x90 = 10010000b */ X86_EFL_PF,
    /* 0x91 = 10010001b */ 0,
    /* 0x92 = 10010010b */ 0,
    /* 0x93 = 10010011b */ X86_EFL_PF,
    /* 0x94 = 10010100b */ 0,
    /* 0x95 = 10010101b */ X86_EFL_PF,
    /* 0x96 = 10010110b */ X86_EFL_PF,
    /* 0x97 = 10010111b */ 0,
    /* 0x98 = 10011000b */ 0,
    /* 0x99 = 10011001b */ X86_EFL_PF,
    /* 0x9a = 10011010b */ X86_EFL_PF,
    /* 0x9b = 10011011b */ 0,
    /* 0x9c = 10011100b */ X86_EFL_PF,
    /* 0x9d = 10011101b */ 0,
    /* 0x9e = 10011110b */ 0,
    /* 0x9f = 10011111b */ X86_EFL_PF,
    /* 0xa0 = 10100000b */ X86_EFL_PF,
    /* 0xa1 = 10100001b */ 0,
    /* 0xa2 = 10100010b */ 0,
    /* 0xa3 = 10100011b */ X86_EFL_PF,
    /* 0xa4 = 10100100b */ 0,
    /* 0xa5 = 10100101b */ X86_EFL_PF,
    /* 0xa6 = 10100110b */ X86_EFL_PF,
    /* 0xa7 = 10100111b */ 0,
    /* 0xa8 = 10101000b */ 0,
    /* 0xa9 = 10101001b */ X86_EFL_PF,
    /* 0xaa = 10101010b */ X86_EFL_PF,
    /* 0xab = 10101011b */ 0,
    /* 0xac = 10101100b */ X86_EFL_PF,
    /* 0xad = 10101101b */ 0,
    /* 0xae = 10101110b */ 0,
    /* 0xaf = 10101111b */ X86_EFL_PF,
    /* 0xb0 = 10110000b */ 0,
    /* 0xb1 = 10110001b */ X86_EFL_PF,
    /* 0xb2 = 10110010b */ X86_EFL_PF,
    /* 0xb3 = 10110011b */ 0,
    /* 0xb4 = 10110100b */ X86_EFL_PF,
    /* 0xb5 = 10110101b */ 0,
    /* 0xb6 = 10110110b */ 0,
    /* 0xb7 = 10110111b */ X86_EFL_PF,
    /* 0xb8 = 10111000b */ X86_EFL_PF,
    /* 0xb9 = 10111001b */ 0,
    /* 0xba = 10111010b */ 0,
    /* 0xbb = 10111011b */ X86_EFL_PF,
    /* 0xbc = 10111100b */ 0,
    /* 0xbd = 10111101b */ X86_EFL_PF,
    /* 0xbe = 10111110b */ X86_EFL_PF,
    /* 0xbf = 10111111b */ 0,
    /* 0xc0 = 11000000b */ X86_EFL_PF,
    /* 0xc1 = 11000001b */ 0,
    /* 0xc2 = 11000010b */ 0,
    /* 0xc3 = 11000011b */ X86_EFL_PF,
    /* 0xc4 = 11000100b */ 0,
    /* 0xc5 = 11000101b */ X86_EFL_PF,
    /* 0xc6 = 11000110b */ X86_EFL_PF,
    /* 0xc7 = 11000111b */ 0,
    /* 0xc8 = 11001000b */ 0,
    /* 0xc9 = 11001001b */ X86_EFL_PF,
    /* 0xca = 11001010b */ X86_EFL_PF,
    /* 0xcb = 11001011b */ 0,
    /* 0xcc = 11001100b */ X86_EFL_PF,
    /* 0xcd = 11001101b */ 0,
    /* 0xce = 11001110b */ 0,
    /* 0xcf = 11001111b */ X86_EFL_PF,
    /* 0xd0 = 11010000b */ 0,
    /* 0xd1 = 11010001b */ X86_EFL_PF,
    /* 0xd2 = 11010010b */ X86_EFL_PF,
    /* 0xd3 = 11010011b */ 0,
    /* 0xd4 = 11010100b */ X86_EFL_PF,
    /* 0xd5 = 11010101b */ 0,
    /* 0xd6 = 11010110b */ 0,
    /* 0xd7 = 11010111b */ X86_EFL_PF,
    /* 0xd8 = 11011000b */ X86_EFL_PF,
    /* 0xd9 = 11011001b */ 0,
    /* 0xda = 11011010b */ 0,
    /* 0xdb = 11011011b */ X86_EFL_PF,
    /* 0xdc = 11011100b */ 0,
    /* 0xdd = 11011101b */ X86_EFL_PF,
    /* 0xde = 11011110b */ X86_EFL_PF,
    /* 0xdf = 11011111b */ 0,
    /* 0xe0 = 11100000b */ 0,
    /* 0xe1 = 11100001b */ X86_EFL_PF,
    /* 0xe2 = 11100010b */ X86_EFL_PF,
    /* 0xe3 = 11100011b */ 0,
    /* 0xe4 = 11100100b */ X86_EFL_PF,
    /* 0xe5 = 11100101b */ 0,
    /* 0xe6 = 11100110b */ 0,
    /* 0xe7 = 11100111b */ X86_EFL_PF,
    /* 0xe8 = 11101000b */ X86_EFL_PF,
    /* 0xe9 = 11101001b */ 0,
    /* 0xea = 11101010b */ 0,
    /* 0xeb = 11101011b */ X86_EFL_PF,
    /* 0xec = 11101100b */ 0,
    /* 0xed = 11101101b */ X86_EFL_PF,
    /* 0xee = 11101110b */ X86_EFL_PF,
    /* 0xef = 11101111b */ 0,
    /* 0xf0 = 11110000b */ X86_EFL_PF,
    /* 0xf1 = 11110001b */ 0,
    /* 0xf2 = 11110010b */ 0,
    /* 0xf3 = 11110011b */ X86_EFL_PF,
    /* 0xf4 = 11110100b */ 0,
    /* 0xf5 = 11110101b */ X86_EFL_PF,
    /* 0xf6 = 11110110b */ X86_EFL_PF,
    /* 0xf7 = 11110111b */ 0,
    /* 0xf8 = 11111000b */ 0,
    /* 0xf9 = 11111001b */ X86_EFL_PF,
    /* 0xfa = 11111010b */ X86_EFL_PF,
    /* 0xfb = 11111011b */ 0,
    /* 0xfc = 11111100b */ X86_EFL_PF,
    /* 0xfd = 11111101b */ 0,
    /* 0xfe = 11111110b */ 0,
    /* 0xff = 11111111b */ X86_EFL_PF,
};

/* for clang: */
extern const RTFLOAT32U  g_ar32Zero[];
extern const RTFLOAT64U  g_ar64Zero[];
extern const RTFLOAT80U  g_ar80Zero[];
extern const RTFLOAT32U  g_ar32One[];
extern const RTFLOAT80U  g_ar80One[];
extern const RTFLOAT80U  g_r80Indefinite;
extern const RTFLOAT32U  g_ar32Infinity[];
extern const RTFLOAT64U  g_ar64Infinity[];
extern const RTFLOAT80U  g_ar80Infinity[];
extern const RTFLOAT128U g_r128Ln2;
extern const RTUINT128U  g_u128Ln2Mantissa;
extern const RTUINT128U  g_u128Ln2MantissaIntel;
extern const RTFLOAT128U g_ar128F2xm1HornerConsts[];
extern const RTFLOAT32U  g_ar32QNaN[];
extern const RTFLOAT64U  g_ar64QNaN[];

/** Zero values (indexed by fSign). */
RTFLOAT32U const g_ar32Zero[] = { RTFLOAT32U_INIT_ZERO(0), RTFLOAT32U_INIT_ZERO(1) };
RTFLOAT64U const g_ar64Zero[] = { RTFLOAT64U_INIT_ZERO(0), RTFLOAT64U_INIT_ZERO(1) };
RTFLOAT80U const g_ar80Zero[] = { RTFLOAT80U_INIT_ZERO(0), RTFLOAT80U_INIT_ZERO(1) };

/** One values (indexed by fSign). */
RTFLOAT32U const g_ar32One[] =
{ RTFLOAT32U_INIT(0, 0, RTFLOAT32U_EXP_BIAS), RTFLOAT32U_INIT(1, 0, RTFLOAT32U_EXP_BIAS) };
RTFLOAT80U const g_ar80One[] =
{ RTFLOAT80U_INIT(0, RT_BIT_64(63), RTFLOAT80U_EXP_BIAS), RTFLOAT80U_INIT(1, RT_BIT_64(63), RTFLOAT80U_EXP_BIAS) };

/** Indefinite (negative). */
RTFLOAT80U const g_r80Indefinite = RTFLOAT80U_INIT_INDEFINITE(1);

/** Infinities (indexed by fSign). */
RTFLOAT32U const g_ar32Infinity[] = { RTFLOAT32U_INIT_INF(0), RTFLOAT32U_INIT_INF(1) };
RTFLOAT64U const g_ar64Infinity[] = { RTFLOAT64U_INIT_INF(0), RTFLOAT64U_INIT_INF(1) };
RTFLOAT80U const g_ar80Infinity[] = { RTFLOAT80U_INIT_INF(0), RTFLOAT80U_INIT_INF(1) };

/** Default QNaNs (indexed by fSign). */
RTFLOAT32U const g_ar32QNaN[] = { RTFLOAT32U_INIT_QNAN(0), RTFLOAT32U_INIT_QNAN(1) };
RTFLOAT64U const g_ar64QNaN[] = { RTFLOAT64U_INIT_QNAN(0), RTFLOAT64U_INIT_QNAN(1) };


#if 0
/** 128-bit floating point constant: 2.0 */
const RTFLOAT128U g_r128Two = RTFLOAT128U_INIT_C(0, 0, 0, RTFLOAT128U_EXP_BIAS + 1);
#endif


/* The next section is generated by tools/IEMGenFpuConstants: */

/** The ln2 constant as 128-bit floating point value.
 * base-10: 6.93147180559945309417232121458176575e-1
 * base-16: b.17217f7d1cf79abc9e3b39803f30@-1
 * base-2 : 1.0110001011100100001011111110111110100011100111101111001101010111100100111100011101100111001100000000011111100110e-1 */
//const RTFLOAT128U g_r128Ln2 = RTFLOAT128U_INIT_C(0, 0x62e42fefa39e, 0xf35793c7673007e6, 0x3ffe);
const RTFLOAT128U g_r128Ln2 = RTFLOAT128U_INIT_C(0, 0x62e42fefa39e, 0xf357900000000000, 0x3ffe);
/** High precision ln2 value.
 * base-10: 6.931471805599453094172321214581765680747e-1
 * base-16: b.17217f7d1cf79abc9e3b39803f2f6af0@-1
 * base-2 : 1.0110001011100100001011111110111110100011100111101111001101010111100100111100011101100111001100000000011111100101111011010101111e-1 */
const RTUINT128U g_u128Ln2Mantissa = RTUINT128_INIT_C(0xb17217f7d1cf79ab, 0xc9e3b39803f2f6af);
/** High precision ln2 value, compatible with f2xm1 results on intel 10980XE.
 * base-10: 6.931471805599453094151379470289064954613e-1
 * base-16: b.17217f7d1cf79abc0000000000000000@-1
 * base-2 : 1.0110001011100100001011111110111110100011100111101111001101010111100000000000000000000000000000000000000000000000000000000000000e-1 */
const RTUINT128U g_u128Ln2MantissaIntel = RTUINT128_INIT_C(0xb17217f7d1cf79ab, 0xc000000000000000);

/** Horner constants for f2xm1 */
const RTFLOAT128U g_ar128F2xm1HornerConsts[] =
{
    /* a0
     * base-10: 1.00000000000000000000000000000000000e0
     * base-16: 1.0000000000000000000000000000@0
     * base-2 : 1.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000e0 */
    RTFLOAT128U_INIT_C(0, 0x000000000000, 0x0000000000000000, 0x3fff),
    /* a1
     * base-10: 5.00000000000000000000000000000000000e-1
     * base-16: 8.0000000000000000000000000000@-1
     * base-2 : 1.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000e-1 */
    RTFLOAT128U_INIT_C(0, 0x000000000000, 0x0000000000000000, 0x3ffe),
    /* a2
     * base-10: 1.66666666666666666666666666666666658e-1
     * base-16: 2.aaaaaaaaaaaaaaaaaaaaaaaaaaaa@-1
     * base-2 : 1.0101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101e-3 */
    RTFLOAT128U_INIT_C(0, 0x555555555555, 0x5555555555555555, 0x3ffc),
    /* a3
     * base-10: 4.16666666666666666666666666666666646e-2
     * base-16: a.aaaaaaaaaaaaaaaaaaaaaaaaaaa8@-2
     * base-2 : 1.0101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101010101e-5 */
    RTFLOAT128U_INIT_C(0, 0x555555555555, 0x5555555555555555, 0x3ffa),
    /* a4
     * base-10: 8.33333333333333333333333333333333323e-3
     * base-16: 2.2222222222222222222222222222@-2
     * base-2 : 1.0001000100010001000100010001000100010001000100010001000100010001000100010001000100010001000100010001000100010001e-7 */
    RTFLOAT128U_INIT_C(0, 0x111111111111, 0x1111111111111111, 0x3ff8),
    /* a5
     * base-10: 1.38888888888888888888888888888888874e-3
     * base-16: 5.b05b05b05b05b05b05b05b05b058@-3
     * base-2 : 1.0110110000010110110000010110110000010110110000010110110000010110110000010110110000010110110000010110110000010110e-10 */
    RTFLOAT128U_INIT_C(0, 0x6c16c16c16c1, 0x6c16c16c16c16c16, 0x3ff5),
    /* a6
     * base-10: 1.98412698412698412698412698412698412e-4
     * base-16: d.00d00d00d00d00d00d00d00d00d0@-4
     * base-2 : 1.1010000000011010000000011010000000011010000000011010000000011010000000011010000000011010000000011010000000011010e-13 */
    RTFLOAT128U_INIT_C(0, 0xa01a01a01a01, 0xa01a01a01a01a01a, 0x3ff2),
    /* a7
     * base-10: 2.48015873015873015873015873015873015e-5
     * base-16: 1.a01a01a01a01a01a01a01a01a01a@-4
     * base-2 : 1.1010000000011010000000011010000000011010000000011010000000011010000000011010000000011010000000011010000000011010e-16 */
    RTFLOAT128U_INIT_C(0, 0xa01a01a01a01, 0xa01a01a01a01a01a, 0x3fef),
    /* a8
     * base-10: 2.75573192239858906525573192239858902e-6
     * base-16: 2.e3bc74aad8e671f5583911ca002e@-5
     * base-2 : 1.0111000111011110001110100101010101101100011100110011100011111010101011000001110010001000111001010000000000010111e-19 */
    RTFLOAT128U_INIT_C(0, 0x71de3a556c73, 0x38faac1c88e50017, 0x3fec),
    /* a9
     * base-10: 2.75573192239858906525573192239858865e-7
     * base-16: 4.9f93edde27d71cbbc05b4fa999e0@-6
     * base-2 : 1.0010011111100100111110110111011110001001111101011100011100101110111100000001011011010011111010100110011001111000e-22 */
    RTFLOAT128U_INIT_C(0, 0x27e4fb7789f5, 0xc72ef016d3ea6678, 0x3fe9),
    /* a10
     * base-10: 2.50521083854417187750521083854417184e-8
     * base-16: 6.b99159fd5138e3f9d1f92e0df71c@-7
     * base-2 : 1.1010111001100100010101100111111101010100010011100011100011111110011101000111111001001011100000110111110111000111e-26 */
    RTFLOAT128U_INIT_C(0, 0xae64567f544e, 0x38fe747e4b837dc7, 0x3fe5),
    /* a11
     * base-10: 2.08767569878680989792100903212014296e-9
     * base-16: 8.f76c77fc6c4bdaa26d4c3d67f420@-8
     * base-2 : 1.0001111011101101100011101111111110001101100010010111101101010100010011011010100110000111101011001111111010000100e-29 */
    RTFLOAT128U_INIT_C(0, 0x1eed8eff8d89, 0x7b544da987acfe84, 0x3fe2),
    /* a12
     * base-10: 1.60590438368216145993923771701549472e-10
     * base-16: b.092309d43684be51c198e91d7b40@-9
     * base-2 : 1.0110000100100100011000010011101010000110110100001001011111001010001110000011001100011101001000111010111101101000e-33 */
    RTFLOAT128U_INIT_C(0, 0x6124613a86d0, 0x97ca38331d23af68, 0x3fde),
    /* a13
     * base-10: 1.14707455977297247138516979786821043e-11
     * base-16: c.9cba54603e4e905d6f8a2efd1f20@-10
     * base-2 : 1.1001001110010111010010101000110000000111110010011101001000001011101011011111000101000101110111111010001111100100e-37 */
    RTFLOAT128U_INIT_C(0, 0x93974a8c07c9, 0xd20badf145dfa3e4, 0x3fda),
    /* a14
     * base-10: 7.64716373181981647590113198578806964e-13
     * base-16: d.73f9f399dc0f88ec32b587746578@-11
     * base-2 : 1.1010111001111111001111100111001100111011100000011111000100011101100001100101011010110000111011101000110010101111e-41 */
    RTFLOAT128U_INIT_C(0, 0xae7f3e733b81, 0xf11d8656b0ee8caf, 0x3fd6),
    /* a15
     * base-10: 4.77947733238738529743820749111754352e-14
     * base-16: d.73f9f399dc0f88ec32b587746578@-12
     * base-2 : 1.1010111001111111001111100111001100111011100000011111000100011101100001100101011010110000111011101000110010101111e-45 */
    RTFLOAT128U_INIT_C(0, 0xae7f3e733b81, 0xf11d8656b0ee8caf, 0x3fd2),
    /* a16
     * base-10: 2.81145725434552076319894558301031970e-15
     * base-16: c.a963b81856a53593028cbbb8d7f8@-13
     * base-2 : 1.1001010100101100011101110000001100001010110101001010011010110010011000000101000110010111011101110001101011111111e-49 */
    RTFLOAT128U_INIT_C(0, 0x952c77030ad4, 0xa6b2605197771aff, 0x3fce),
    /* a17
     * base-10: 1.56192069685862264622163643500573321e-16
     * base-16: b.413c31dcbecbbdd8024435161550@-14
     * base-2 : 1.0110100000100111100001100011101110010111110110010111011110111011000000000100100010000110101000101100001010101010e-53 */
    RTFLOAT128U_INIT_C(0, 0x6827863b97d9, 0x77bb004886a2c2aa, 0x3fca),
    /* a18
     * base-10: 8.22063524662432971695598123687227980e-18
     * base-16: 9.7a4da340a0ab92650f61dbdcb3a0@-15
     * base-2 : 1.0010111101001001101101000110100000010100000101010111001001001100101000011110110000111011011110111001011001110100e-57 */
    RTFLOAT128U_INIT_C(0, 0x2f49b4681415, 0x724ca1ec3b7b9674, 0x3fc6),
    /* a19
     * base-10: 4.11031762331216485847799061843614006e-19
     * base-16: 7.950ae900808941ea72b4afe3c2e8@-16
     * base-2 : 1.1110010101000010101110100100000000100000001000100101000001111010100111001010110100101011111110001111000010111010e-62 */
    RTFLOAT128U_INIT_C(0, 0xe542ba402022, 0x507a9cad2bf8f0ba, 0x3fc1),
    /* a20
     * base-10: 1.95729410633912612308475743735054143e-20
     * base-16: 5.c6e3bdb73d5c62fbc51bf3b9b8fc@-17
     * base-2 : 1.0111000110111000111011110110110111001111010101110001100010111110111100010100011011111100111011100110111000111111e-66 */
    RTFLOAT128U_INIT_C(0, 0x71b8ef6dcf57, 0x18bef146fcee6e3f, 0x3fbd),
    /* a21
     * base-10: 8.89679139245057328674889744250246106e-22
     * base-16: 4.338e5b6dfe14a5143242dfcce3a0@-18
     * base-2 : 1.0000110011100011100101101101101101111111100001010010100101000101000011001001000010110111111100110011100011101000e-70 */
    RTFLOAT128U_INIT_C(0, 0x0ce396db7f85, 0x29450c90b7f338e8, 0x3fb9),
};


/*
 * There are a few 64-bit on 32-bit things we'd rather do in C.  Actually, doing
 * it all in C is probably safer atm., optimize what's necessary later, maybe.
 */
#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)


/*********************************************************************************************************************************
*   Binary Operations                                                                                                            *
*********************************************************************************************************************************/

/*
 * ADD
 */

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_add_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    uint64_t uDst    = *puDst;
    uint64_t uResult = uDst + uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 64, uResult < uDst, uSrc);
    return fEFlags;
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_add_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    uint32_t uDst    = *puDst;
    uint32_t uResult = uDst + uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 32, uResult < uDst, uSrc);
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_add_u16,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    uint16_t uDst    = *puDst;
    uint16_t uResult = uDst + uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 16, uResult < uDst, uSrc);
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_add_u8,(uint32_t fEFlags, uint8_t *puDst, uint8_t uSrc))
{
    uint8_t uDst    = *puDst;
    uint8_t uResult = uDst + uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 8, uResult < uDst, uSrc);
    return fEFlags;
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */

/*
 * ADC
 */

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_adc_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    if (!(fEFlags & X86_EFL_CF))
        fEFlags = iemAImpl_add_u64(fEFlags, puDst, uSrc);
    else
    {
        uint64_t uDst    = *puDst;
        uint64_t uResult = uDst + uSrc + 1;
        *puDst = uResult;
        IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 64, uResult <= uDst, uSrc);
    }
    return fEFlags;
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_adc_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    if (!(fEFlags & X86_EFL_CF))
        fEFlags = iemAImpl_add_u32(fEFlags, puDst, uSrc);
    else
    {
        uint32_t uDst    = *puDst;
        uint32_t uResult = uDst + uSrc + 1;
        *puDst = uResult;
        IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 32, uResult <= uDst, uSrc);
    }
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_adc_u16,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    if (!(fEFlags & X86_EFL_CF))
        fEFlags = iemAImpl_add_u16(fEFlags, puDst, uSrc);
    else
    {
        uint16_t uDst    = *puDst;
        uint16_t uResult = uDst + uSrc + 1;
        *puDst = uResult;
        IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 16, uResult <= uDst, uSrc);
    }
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_adc_u8,(uint32_t fEFlags, uint8_t *puDst, uint8_t uSrc))
{
    if (!(fEFlags & X86_EFL_CF))
        fEFlags = iemAImpl_add_u8(fEFlags, puDst, uSrc);
    else
    {
        uint8_t uDst    = *puDst;
        uint8_t uResult = uDst + uSrc + 1;
        *puDst = uResult;
        IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 8, uResult <= uDst, uSrc);
    }
    return fEFlags;
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */

/*
 * SUB
 */
# if !defined(RT_ARCH_ARM64)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_sub_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    uint64_t uDst    = *puDst;
    uint64_t uResult = uDst - uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 64, uDst < uSrc, uSrc ^ RT_BIT_64(63));
    return fEFlags;
}

#  if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_sub_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    uint32_t uDst    = *puDst;
    uint32_t uResult = uDst - uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 32, uDst < uSrc, uSrc ^ RT_BIT_32(31));
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_sub_u16,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    uint16_t uDst    = *puDst;
    uint16_t uResult = uDst - uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 16, uDst < uSrc, uSrc ^ (uint16_t)0x8000);
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_sub_u8,(uint32_t fEFlags, uint8_t *puDst, uint8_t uSrc))
{
    uint8_t uDst    = *puDst;
    uint8_t uResult = uDst - uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 8, uDst < uSrc, uSrc ^ (uint8_t)0x80);
    return fEFlags;
}

#  endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */
# endif /* !RT_ARCH_ARM64 */

/*
 * SBB
 */

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_sbb_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    if (!(fEFlags & X86_EFL_CF))
        fEFlags = iemAImpl_sub_u64(fEFlags, puDst, uSrc);
    else
    {
        uint64_t uDst    = *puDst;
        uint64_t uResult = uDst - uSrc - 1;
        *puDst = uResult;
        IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 64, uDst <= uSrc, uSrc ^ RT_BIT_64(63));
    }
    return fEFlags;
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_sbb_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    if (!(fEFlags & X86_EFL_CF))
        fEFlags = iemAImpl_sub_u32(fEFlags, puDst, uSrc);
    else
    {
        uint32_t uDst    = *puDst;
        uint32_t uResult = uDst - uSrc - 1;
        *puDst = uResult;
        IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 32, uDst <= uSrc, uSrc ^ RT_BIT_32(31));
    }
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_sbb_u16,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    if (!(fEFlags & X86_EFL_CF))
        fEFlags = iemAImpl_sub_u16(fEFlags, puDst, uSrc);
    else
    {
        uint16_t uDst    = *puDst;
        uint16_t uResult = uDst - uSrc - 1;
        *puDst = uResult;
        IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 16, uDst <= uSrc, uSrc ^ (uint16_t)0x8000);
    }
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_sbb_u8,(uint32_t fEFlags, uint8_t *puDst, uint8_t uSrc))
{
    if (!(fEFlags & X86_EFL_CF))
        fEFlags = iemAImpl_sub_u8(fEFlags, puDst, uSrc);
    else
    {
        uint8_t uDst    = *puDst;
        uint8_t uResult = uDst - uSrc - 1;
        *puDst = uResult;
        IEM_EFL_UPDATE_STATUS_BITS_FOR_ARITHMETIC(fEFlags, uResult, uDst, uSrc, 8, uDst <= uSrc, uSrc ^ (uint8_t)0x80);
    }
    return fEFlags;
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */


/*
 * OR
 */

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_or_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    uint64_t uResult = *puDst | uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 64, 0);
    return fEFlags;
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_or_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    uint32_t uResult = *puDst | uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 32, 0);
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_or_u16,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    uint16_t uResult = *puDst | uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 16, 0);
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_or_u8,(uint32_t fEFlags, uint8_t *puDst, uint8_t uSrc))
{
    uint8_t uResult = *puDst | uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 8, 0);
    return fEFlags;
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */

/*
 * XOR
 */

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_xor_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    uint64_t uResult = *puDst ^ uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 64, 0);
    return fEFlags;
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_xor_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    uint32_t uResult = *puDst ^ uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 32, 0);
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_xor_u16,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    uint16_t uResult = *puDst ^ uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 16, 0);
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_xor_u8,(uint32_t fEFlags, uint8_t *puDst, uint8_t uSrc))
{
    uint8_t uResult = *puDst ^ uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 8, 0);
    return fEFlags;
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */

/*
 * AND
 */

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_and_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    uint64_t const uResult = *puDst & uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 64, 0);
    return fEFlags;
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_and_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    uint32_t const uResult = *puDst & uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 32, 0);
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_and_u16,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    uint16_t const uResult = *puDst & uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 16, 0);
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_and_u8,(uint32_t fEFlags, uint8_t *puDst, uint8_t uSrc))
{
    uint8_t const uResult = *puDst & uSrc;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 8, 0);
    return fEFlags;
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */
#endif /* !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY) */

/*
 * ANDN (BMI1 instruction)
 */

IEM_DECL_IMPL_DEF(void, iemAImpl_andn_u64_fallback,(uint64_t *puDst, uint64_t uSrc1, uint64_t uSrc2, uint32_t *pfEFlags))
{
    uint64_t const uResult = ~uSrc1 & uSrc2;
    *puDst = uResult;
    uint32_t fEFlags = *pfEFlags;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 64, 0);
    *pfEFlags = fEFlags;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_andn_u32_fallback,(uint32_t *puDst, uint32_t uSrc1, uint32_t uSrc2, uint32_t *pfEFlags))
{
    uint32_t const uResult = ~uSrc1 & uSrc2;
    *puDst = uResult;
    uint32_t fEFlags = *pfEFlags;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 32, 0);
    *pfEFlags = fEFlags;
}


#if defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
IEM_DECL_IMPL_DEF(void, iemAImpl_andn_u64,(uint64_t *puDst, uint64_t uSrc1, uint64_t uSrc2, uint32_t *pfEFlags))
{
    iemAImpl_andn_u64_fallback(puDst, uSrc1, uSrc2, pfEFlags);
}
#endif


#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
IEM_DECL_IMPL_DEF(void, iemAImpl_andn_u32,(uint32_t *puDst, uint32_t uSrc1, uint32_t uSrc2, uint32_t *pfEFlags))
{
    iemAImpl_andn_u32_fallback(puDst, uSrc1, uSrc2, pfEFlags);
}
#endif

#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)

/*
 * CMP
 */

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cmp_u64,(uint32_t fEFlags, uint64_t const *puDst, uint64_t uSrc))
{
    uint64_t uDstTmp = *puDst;
    return iemAImpl_sub_u64(fEFlags, &uDstTmp, uSrc);
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cmp_u32,(uint32_t fEFlags, uint32_t const *puDst, uint32_t uSrc))
{
    uint32_t uDstTmp = *puDst;
    return iemAImpl_sub_u32(fEFlags, &uDstTmp, uSrc);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cmp_u16,(uint32_t fEFlags, uint16_t const *puDst, uint16_t uSrc))
{
    uint16_t uDstTmp = *puDst;
    return iemAImpl_sub_u16(fEFlags, &uDstTmp, uSrc);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cmp_u8,(uint32_t fEFlags, uint8_t const *puDst, uint8_t uSrc))
{
    uint8_t uDstTmp = *puDst;
    return iemAImpl_sub_u8(fEFlags, &uDstTmp, uSrc);
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */

/*
 * TEST
 */

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_test_u64,(uint32_t fEFlags, uint64_t const *puDst, uint64_t uSrc))
{
    uint64_t uResult = *puDst & uSrc;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 64, 0);
    return fEFlags;
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_test_u32,(uint32_t fEFlags, uint32_t const *puDst, uint32_t uSrc))
{
    uint32_t uResult = *puDst & uSrc;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 32, 0);
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_test_u16,(uint32_t fEFlags, uint16_t const *puDst, uint16_t uSrc))
{
    uint16_t uResult = *puDst & uSrc;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 16, 0);
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_test_u8,(uint32_t fEFlags, uint8_t const *puDst, uint8_t uSrc))
{
    uint8_t uResult = *puDst & uSrc;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_LOGICAL(fEFlags, uResult, 8, 0);
    return fEFlags;
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */


/*
 * LOCK prefixed variants of the above
 */

/** 64-bit locked binary operand operation. */
# define DO_LOCKED_BIN_OP(a_Mnemonic, a_cBitsWidth) \
    do { \
        uint ## a_cBitsWidth ## _t uOld = ASMAtomicUoReadU ## a_cBitsWidth(puDst); \
        uint ## a_cBitsWidth ## _t uTmp; \
        uint32_t fEflTmp; \
        do \
        { \
            uTmp = uOld; \
            fEflTmp = iemAImpl_ ## a_Mnemonic ## _u ## a_cBitsWidth(fEFlagsIn, &uTmp, uSrc); \
        } while (!ASMAtomicCmpXchgExU ## a_cBitsWidth(puDst, uTmp, uOld, &uOld)); \
        return fEflTmp; \
    } while (0)


#define EMIT_LOCKED_BIN_OP(a_Mnemonic, a_cBitsWidth) \
    IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_ ## a_Mnemonic ## _u ## a_cBitsWidth ##  _locked,(uint32_t fEFlagsIn, \
                                                                                           uint ## a_cBitsWidth ## _t *puDst, \
                                                                                           uint ## a_cBitsWidth ## _t uSrc)) \
    { \
        DO_LOCKED_BIN_OP(a_Mnemonic, a_cBitsWidth); \
    }

EMIT_LOCKED_BIN_OP(add, 64)
EMIT_LOCKED_BIN_OP(adc, 64)
EMIT_LOCKED_BIN_OP(sub, 64)
EMIT_LOCKED_BIN_OP(sbb, 64)
EMIT_LOCKED_BIN_OP(or,  64)
EMIT_LOCKED_BIN_OP(xor, 64)
EMIT_LOCKED_BIN_OP(and, 64)
# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_LOCKED_BIN_OP(add, 32)
EMIT_LOCKED_BIN_OP(adc, 32)
EMIT_LOCKED_BIN_OP(sub, 32)
EMIT_LOCKED_BIN_OP(sbb, 32)
EMIT_LOCKED_BIN_OP(or,  32)
EMIT_LOCKED_BIN_OP(xor, 32)
EMIT_LOCKED_BIN_OP(and, 32)

EMIT_LOCKED_BIN_OP(add, 16)
EMIT_LOCKED_BIN_OP(adc, 16)
EMIT_LOCKED_BIN_OP(sub, 16)
EMIT_LOCKED_BIN_OP(sbb, 16)
EMIT_LOCKED_BIN_OP(or,  16)
EMIT_LOCKED_BIN_OP(xor, 16)
EMIT_LOCKED_BIN_OP(and, 16)

EMIT_LOCKED_BIN_OP(add, 8)
EMIT_LOCKED_BIN_OP(adc, 8)
EMIT_LOCKED_BIN_OP(sub, 8)
EMIT_LOCKED_BIN_OP(sbb, 8)
EMIT_LOCKED_BIN_OP(or,  8)
EMIT_LOCKED_BIN_OP(xor, 8)
EMIT_LOCKED_BIN_OP(and, 8)
# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */


/*
 * Bit operations (same signature as above).
 */

/*
 * BT
 */

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bt_u64,(uint32_t fEFlags, uint64_t const *puDst, uint64_t uSrc))
{
    /* Note! "undefined" flags: OF, SF, ZF, AF, PF.  However, it seems they're
             not modified by either AMD (3990x) or Intel (i9-9980HK). */
    Assert(uSrc < 64);
    uint64_t uDst = *puDst;
    if (uDst & RT_BIT_64(uSrc))
        fEFlags |= X86_EFL_CF;
    else
        fEFlags &= ~X86_EFL_CF;
    return fEFlags;
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bt_u32,(uint32_t fEFlags, uint32_t const *puDst, uint32_t uSrc))
{
    /* Note! "undefined" flags: OF, SF, ZF, AF, PF.  However, it seems they're
             not modified by either AMD (3990x) or Intel (i9-9980HK). */
    Assert(uSrc < 32);
    uint32_t uDst = *puDst;
    if (uDst & RT_BIT_32(uSrc))
        fEFlags |= X86_EFL_CF;
    else
        fEFlags &= ~X86_EFL_CF;
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bt_u16,(uint32_t fEFlags, uint16_t const *puDst, uint16_t uSrc))
{
    /* Note! "undefined" flags: OF, SF, ZF, AF, PF.  However, it seems they're
             not modified by either AMD (3990x) or Intel (i9-9980HK). */
    Assert(uSrc < 16);
    uint16_t uDst = *puDst;
    if (uDst & RT_BIT_32(uSrc))
        fEFlags |= X86_EFL_CF;
    else
        fEFlags &= ~X86_EFL_CF;
    return fEFlags;
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */

/*
 * BTC
 */

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_btc_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    /* Note! "undefined" flags: OF, SF, ZF, AF, PF.  However, it seems they're
             not modified by either AMD (3990x) or Intel (i9-9980HK). */
    Assert(uSrc < 64);
    uint64_t fMask = RT_BIT_64(uSrc);
    uint64_t uDst = *puDst;
    if (uDst & fMask)
    {
        uDst &= ~fMask;
        *puDst = uDst;
        fEFlags |= X86_EFL_CF;
    }
    else
    {
        uDst |= fMask;
        *puDst = uDst;
        fEFlags &= ~X86_EFL_CF;
    }
    return fEFlags;
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_btc_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    /* Note! "undefined" flags: OF, SF, ZF, AF, PF.  However, it seems they're
             not modified by either AMD (3990x) or Intel (i9-9980HK). */
    Assert(uSrc < 32);
    uint32_t fMask = RT_BIT_32(uSrc);
    uint32_t uDst = *puDst;
    if (uDst & fMask)
    {
        uDst &= ~fMask;
        *puDst = uDst;
        fEFlags |= X86_EFL_CF;
    }
    else
    {
        uDst |= fMask;
        *puDst = uDst;
        fEFlags &= ~X86_EFL_CF;
    }
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_btc_u16,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    /* Note! "undefined" flags: OF, SF, ZF, AF, PF.  However, it seems they're
             not modified by either AMD (3990x) or Intel (i9-9980HK). */
    Assert(uSrc < 16);
    uint16_t fMask = RT_BIT_32(uSrc);
    uint16_t uDst = *puDst;
    if (uDst & fMask)
    {
        uDst &= ~fMask;
        *puDst = uDst;
        fEFlags |= X86_EFL_CF;
    }
    else
    {
        uDst |= fMask;
        *puDst = uDst;
        fEFlags &= ~X86_EFL_CF;
    }
    return fEFlags;
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */

/*
 * BTR
 */

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_btr_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    /* Note! "undefined" flags: OF, SF, ZF, AF, PF.  We set them as after an
       logical operation (AND/OR/whatever). */
    Assert(uSrc < 64);
    uint64_t fMask = RT_BIT_64(uSrc);
    uint64_t uDst = *puDst;
    if (uDst & fMask)
    {
        uDst &= ~fMask;
        *puDst = uDst;
        fEFlags |= X86_EFL_CF;
    }
    else
        fEFlags &= ~X86_EFL_CF;
    return fEFlags;
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_btr_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    /* Note! "undefined" flags: OF, SF, ZF, AF, PF.  We set them as after an
       logical operation (AND/OR/whatever). */
    Assert(uSrc < 32);
    uint32_t fMask = RT_BIT_32(uSrc);
    uint32_t uDst = *puDst;
    if (uDst & fMask)
    {
        uDst &= ~fMask;
        *puDst = uDst;
        fEFlags |= X86_EFL_CF;
    }
    else
        fEFlags &= ~X86_EFL_CF;
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_btr_u16,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    /* Note! "undefined" flags: OF, SF, ZF, AF, PF.  We set them as after an
       logical operation (AND/OR/whatever). */
    Assert(uSrc < 16);
    uint16_t fMask = RT_BIT_32(uSrc);
    uint16_t uDst = *puDst;
    if (uDst & fMask)
    {
        uDst &= ~fMask;
        *puDst = uDst;
        fEFlags |= X86_EFL_CF;
    }
    else
        fEFlags &= ~X86_EFL_CF;
    return fEFlags;
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */

/*
 * BTS
 */

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bts_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    /* Note! "undefined" flags: OF, SF, ZF, AF, PF.  We set them as after an
       logical operation (AND/OR/whatever). */
    Assert(uSrc < 64);
    uint64_t fMask = RT_BIT_64(uSrc);
    uint64_t uDst = *puDst;
    if (uDst & fMask)
        fEFlags |= X86_EFL_CF;
    else
    {
        uDst |= fMask;
        *puDst = uDst;
        fEFlags &= ~X86_EFL_CF;
    }
    return fEFlags;
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bts_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    /* Note! "undefined" flags: OF, SF, ZF, AF, PF.  We set them as after an
       logical operation (AND/OR/whatever). */
    Assert(uSrc < 32);
    uint32_t fMask = RT_BIT_32(uSrc);
    uint32_t uDst = *puDst;
    if (uDst & fMask)
        fEFlags |= X86_EFL_CF;
    else
    {
        uDst |= fMask;
        *puDst = uDst;
        fEFlags &= ~X86_EFL_CF;
    }
    return fEFlags;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bts_u16,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    /* Note! "undefined" flags: OF, SF, ZF, AF, PF.  We set them as after an
       logical operation (AND/OR/whatever). */
    Assert(uSrc < 16);
    uint16_t fMask = RT_BIT_32(uSrc);
    uint32_t uDst = *puDst;
    if (uDst & fMask)
        fEFlags |= X86_EFL_CF;
    else
    {
        uDst |= fMask;
        *puDst = uDst;
        fEFlags &= ~X86_EFL_CF;
    }
    return fEFlags;
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */

EMIT_LOCKED_BIN_OP(btc, 64)
EMIT_LOCKED_BIN_OP(btr, 64)
EMIT_LOCKED_BIN_OP(bts, 64)
# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_LOCKED_BIN_OP(btc, 32)
EMIT_LOCKED_BIN_OP(btr, 32)
EMIT_LOCKED_BIN_OP(bts, 32)

EMIT_LOCKED_BIN_OP(btc, 16)
EMIT_LOCKED_BIN_OP(btr, 16)
EMIT_LOCKED_BIN_OP(bts, 16)
# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */

#endif /* !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY) */

/*
 * Helpers for BSR and BSF.
 *
 * Note! "undefined" flags: OF, SF, AF, PF, CF.
 *       Intel behavior modelled on 10980xe, AMD on 3990X.  Other marchs may
 *       produce different result (see https://www.sandpile.org/x86/flags.htm),
 *       but we restrict ourselves to emulating these recent marchs.
 */
#define SET_BIT_SEARCH_RESULT_INTEL(a_puDst, a_fEFlagsVar, a_iBit) do { \
        unsigned iBit = (a_iBit); \
        a_fEFlagsVar &= ~(X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF); \
        if (iBit) \
        { \
            *(a_puDst)    = --iBit; \
            a_fEFlagsVar |= IEM_EFL_CALC_PARITY(iBit); \
        } \
        else \
            a_fEFlagsVar |= X86_EFL_ZF | X86_EFL_PF; \
    } while (0)
#define SET_BIT_SEARCH_RESULT_AMD(a_puDst, a_fEFlagsVar, a_iBit) do { \
        unsigned const iBit = (a_iBit); \
        if (iBit) \
        { \
            *(a_puDst)    = iBit - 1; \
            a_fEFlagsVar &= ~X86_EFL_ZF; \
        } \
        else \
            a_fEFlagsVar |= X86_EFL_ZF; \
    } while (0)

/*
 * BSF - first (least significant) bit set
 */
#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsf_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    SET_BIT_SEARCH_RESULT_INTEL(puDst, fEFlags, ASMBitFirstSetU64(uSrc));
    return fEFlags;
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsf_u64_intel,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    SET_BIT_SEARCH_RESULT_INTEL(puDst, fEFlags, ASMBitFirstSetU64(uSrc));
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsf_u64_amd,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    SET_BIT_SEARCH_RESULT_AMD(puDst, fEFlags, ASMBitFirstSetU64(uSrc));
    return fEFlags;
}

#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsf_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    SET_BIT_SEARCH_RESULT_INTEL(puDst, fEFlags, ASMBitFirstSetU32(uSrc));
    return fEFlags;
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsf_u32_intel,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    SET_BIT_SEARCH_RESULT_INTEL(puDst, fEFlags, ASMBitFirstSetU32(uSrc));
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsf_u32_amd,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    SET_BIT_SEARCH_RESULT_AMD(puDst, fEFlags, ASMBitFirstSetU32(uSrc));
    return fEFlags;
}


#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsf_u16,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    SET_BIT_SEARCH_RESULT_INTEL(puDst, fEFlags, ASMBitFirstSetU16(uSrc));
    return fEFlags;
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsf_u16_intel,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    SET_BIT_SEARCH_RESULT_INTEL(puDst, fEFlags, ASMBitFirstSetU16(uSrc));
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsf_u16_amd,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    SET_BIT_SEARCH_RESULT_AMD(puDst, fEFlags, ASMBitFirstSetU16(uSrc));
    return fEFlags;
}



/*
 * BSR - last (most significant) bit set
 */
#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsr_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    SET_BIT_SEARCH_RESULT_INTEL(puDst, fEFlags, ASMBitLastSetU64(uSrc));
    return fEFlags;
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsr_u64_intel,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    SET_BIT_SEARCH_RESULT_INTEL(puDst, fEFlags, ASMBitLastSetU64(uSrc));
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsr_u64_amd,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    SET_BIT_SEARCH_RESULT_AMD(puDst, fEFlags, ASMBitLastSetU64(uSrc));
    return fEFlags;
}


#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsr_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    SET_BIT_SEARCH_RESULT_INTEL(puDst, fEFlags, ASMBitLastSetU32(uSrc));
    return fEFlags;
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsr_u32_intel,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    SET_BIT_SEARCH_RESULT_INTEL(puDst, fEFlags, ASMBitLastSetU32(uSrc));
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsr_u32_amd,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    SET_BIT_SEARCH_RESULT_AMD(puDst, fEFlags, ASMBitLastSetU32(uSrc));
    return fEFlags;
}


#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsr_u16,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    SET_BIT_SEARCH_RESULT_INTEL(puDst, fEFlags, ASMBitLastSetU16(uSrc));
    return fEFlags;
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsr_u16_intel,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    SET_BIT_SEARCH_RESULT_INTEL(puDst, fEFlags, ASMBitLastSetU16(uSrc));
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_bsr_u16_amd,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    SET_BIT_SEARCH_RESULT_AMD(puDst, fEFlags, ASMBitLastSetU16(uSrc));
    return fEFlags;
}


/*
 * Helpers for LZCNT and TZCNT.
 */
#define SET_BIT_CNT_SEARCH_RESULT_INTEL(a_puDst, a_uSrc, a_fEFlagsVar, a_uResult) do { \
        unsigned const uResult = (a_uResult); \
        *(a_puDst) = uResult; \
        a_fEFlagsVar &= ~(X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF); \
        if (uResult) \
            a_fEFlagsVar |= IEM_EFL_CALC_PARITY(uResult); \
        else \
            a_fEFlagsVar |= X86_EFL_ZF | X86_EFL_PF; \
        if (!a_uSrc) \
            a_fEFlagsVar |= X86_EFL_CF; \
    } while (0)
#define SET_BIT_CNT_SEARCH_RESULT_AMD(a_puDst, a_uSrc, a_fEFlagsVar, a_uResult) do { \
        unsigned const uResult = (a_uResult); \
        *(a_puDst) = uResult; \
        a_fEFlagsVar &= ~(X86_EFL_ZF | X86_EFL_CF); \
        if (!uResult) \
            a_fEFlagsVar |= X86_EFL_ZF; \
        if (!a_uSrc) \
            a_fEFlagsVar |= X86_EFL_CF; \
    } while (0)


/*
 * LZCNT - count leading zero bits.
 */
#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_lzcnt_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    return iemAImpl_lzcnt_u64_intel(fEFlags, puDst, uSrc);
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_lzcnt_u64_intel,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    SET_BIT_CNT_SEARCH_RESULT_INTEL(puDst, uSrc, fEFlags, ASMCountLeadingZerosU64(uSrc));
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_lzcnt_u64_amd,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    SET_BIT_CNT_SEARCH_RESULT_AMD(puDst, uSrc, fEFlags, ASMCountLeadingZerosU64(uSrc));
    return fEFlags;
}


#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_lzcnt_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    return iemAImpl_lzcnt_u32_intel(fEFlags, puDst, uSrc);
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_lzcnt_u32_intel,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    SET_BIT_CNT_SEARCH_RESULT_INTEL(puDst, uSrc, fEFlags, ASMCountLeadingZerosU32(uSrc));
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_lzcnt_u32_amd,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    SET_BIT_CNT_SEARCH_RESULT_AMD(puDst, uSrc, fEFlags, ASMCountLeadingZerosU32(uSrc));
    return fEFlags;
}


#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_lzcnt_u16,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    return iemAImpl_lzcnt_u16_intel(fEFlags, puDst, uSrc);
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_lzcnt_u16_intel,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    SET_BIT_CNT_SEARCH_RESULT_INTEL(puDst, uSrc, fEFlags, ASMCountLeadingZerosU16(uSrc));
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_lzcnt_u16_amd,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    SET_BIT_CNT_SEARCH_RESULT_AMD(puDst, uSrc, fEFlags, ASMCountLeadingZerosU16(uSrc));
    return fEFlags;
}


/*
 * TZCNT - count leading zero bits.
 */
#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_tzcnt_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    return iemAImpl_tzcnt_u64_intel(fEFlags, puDst, uSrc);
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_tzcnt_u64_intel,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    SET_BIT_CNT_SEARCH_RESULT_INTEL(puDst, uSrc, fEFlags, ASMCountTrailingZerosU64(uSrc));
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_tzcnt_u64_amd,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    SET_BIT_CNT_SEARCH_RESULT_AMD(puDst, uSrc, fEFlags, ASMCountTrailingZerosU64(uSrc));
    return fEFlags;
}


#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_tzcnt_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    return iemAImpl_tzcnt_u32_intel(fEFlags, puDst, uSrc);
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_tzcnt_u32_intel,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    SET_BIT_CNT_SEARCH_RESULT_INTEL(puDst, uSrc, fEFlags, ASMCountTrailingZerosU32(uSrc));
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_tzcnt_u32_amd,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    SET_BIT_CNT_SEARCH_RESULT_AMD(puDst, uSrc, fEFlags, ASMCountTrailingZerosU32(uSrc));
    return fEFlags;
}


#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_tzcnt_u16,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    return iemAImpl_tzcnt_u16_intel(fEFlags, puDst, uSrc);
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_tzcnt_u16_intel,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    SET_BIT_CNT_SEARCH_RESULT_INTEL(puDst, uSrc, fEFlags, ASMCountTrailingZerosU16(uSrc));
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_tzcnt_u16_amd,(uint32_t fEFlags, uint16_t *puDst, uint16_t uSrc))
{
    SET_BIT_CNT_SEARCH_RESULT_AMD(puDst, uSrc, fEFlags, ASMCountTrailingZerosU16(uSrc));
    return fEFlags;
}



/*
 * BEXTR (BMI1 instruction)
 */
#define EMIT_BEXTR(a_cBits, a_Type, a_Suffix) \
IEM_DECL_IMPL_DEF(void, RT_CONCAT3(iemAImpl_bextr_u,a_cBits,a_Suffix),(a_Type *puDst, a_Type uSrc1, \
                                                                       a_Type uSrc2, uint32_t *pfEFlags)) \
{ \
    /* uSrc1 is considered virtually zero extended to 512 bits width. */ \
    uint32_t      fEfl      = *pfEFlags & ~(X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF); \
    a_Type        uResult; \
    uint8_t const iFirstBit = (uint8_t)uSrc2; \
    if (iFirstBit < a_cBits) \
    { \
        uResult = uSrc1 >> iFirstBit; \
        uint8_t const cBits = (uint8_t)(uSrc2 >> 8); \
        if (cBits < a_cBits) \
            uResult &= RT_CONCAT(RT_BIT_,a_cBits)(cBits) - 1; \
        *puDst    = uResult; \
        if (!uResult) \
            fEfl |= X86_EFL_ZF; \
    } \
    else \
    { \
        *puDst  = uResult = 0; \
        fEfl   |= X86_EFL_ZF; \
    } \
    /** @todo complete flag calculations. */ \
    *pfEFlags = fEfl; \
}

EMIT_BEXTR(64, uint64_t, _fallback)
EMIT_BEXTR(32, uint32_t, _fallback)
#if defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_BEXTR(64, uint64_t, RT_NOTHING)
#endif
#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_BEXTR(32, uint32_t, RT_NOTHING)
#endif

/*
 * BLSR (BMI1 instruction)
 */
#define EMIT_BLSR(a_cBits, a_Type, a_Suffix) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_blsr_u,a_cBits,a_Suffix),(uint32_t fEFlags, a_Type *puDst, a_Type uSrc)) \
{ \
    *puDst = uSrc; \
    uint32_t fEfl1 = iemAImpl_sub_u ## a_cBits(fEFlags, &uSrc, 1); \
    uint32_t fEfl2 = iemAImpl_and_u ## a_cBits(fEFlags, puDst, uSrc); \
    \
    /* AMD: The carry flag is from the SUB operation. */ \
    /* 10890xe: PF always cleared? */ \
    fEfl2 &= ~(X86_EFL_CF | X86_EFL_PF); \
    fEfl2 |= fEfl1 & X86_EFL_CF; \
    return fEfl2; \
}

EMIT_BLSR(64, uint64_t, _fallback)
EMIT_BLSR(32, uint32_t, _fallback)
#if defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_BLSR(64, uint64_t, RT_NOTHING)
#endif
#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_BLSR(32, uint32_t, RT_NOTHING)
#endif

/*
 * BLSMSK (BMI1 instruction)
 */
#define EMIT_BLSMSK(a_cBits, a_Type, a_Suffix) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_blsmsk_u,a_cBits,a_Suffix),(uint32_t fEFlags, a_Type *puDst, a_Type uSrc)) \
{ \
    *puDst = uSrc; \
    uint32_t fEfl1 = iemAImpl_sub_u ## a_cBits(fEFlags, &uSrc, 1); \
    uint32_t fEfl2 = iemAImpl_xor_u ## a_cBits(fEFlags, puDst, uSrc); \
    \
    /* AMD: The carry flag is from the SUB operation. */ \
    /* 10890xe: PF always cleared? */ \
    fEfl2 &= ~(X86_EFL_CF | X86_EFL_PF); \
    fEfl2 |= fEfl1 & X86_EFL_CF; \
    return fEfl2; \
}

EMIT_BLSMSK(64, uint64_t, _fallback)
EMIT_BLSMSK(32, uint32_t, _fallback)
#if defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_BLSMSK(64, uint64_t, RT_NOTHING)
#endif
#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_BLSMSK(32, uint32_t, RT_NOTHING)
#endif

/*
 * BLSI (BMI1 instruction)
 */
#define EMIT_BLSI(a_cBits, a_Type, a_Suffix) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_blsi_u,a_cBits,a_Suffix),(uint32_t fEFlags, a_Type *puDst, a_Type uSrc)) \
{ \
    uint32_t fEfl1 = fEFlags; \
    *puDst = uSrc; \
    iemAImpl_neg_u ## a_cBits(&uSrc, &fEfl1); \
    uint32_t fEfl2 = iemAImpl_and_u ## a_cBits(fEFlags, puDst, uSrc); \
    \
    /* AMD: The carry flag is from the SUB operation. */ \
    /* 10890xe: PF always cleared? */ \
    fEfl2 &= ~(X86_EFL_CF | X86_EFL_PF); \
    fEfl2 |= fEfl1 & X86_EFL_CF; \
    return fEfl2; \
}

EMIT_BLSI(64, uint64_t, _fallback)
EMIT_BLSI(32, uint32_t, _fallback)
#if defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_BLSI(64, uint64_t, RT_NOTHING)
#endif
#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_BLSI(32, uint32_t, RT_NOTHING)
#endif

/*
 * BZHI (BMI2 instruction)
 */
#define EMIT_BZHI(a_cBits, a_Type, a_Suffix) \
IEM_DECL_IMPL_DEF(void, RT_CONCAT3(iemAImpl_bzhi_u,a_cBits,a_Suffix),(a_Type *puDst, a_Type uSrc1, \
                                                                      a_Type uSrc2, uint32_t *pfEFlags)) \
{ \
    uint32_t      fEfl      = *pfEFlags & ~(X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF); \
    a_Type        uResult; \
    uint8_t const iFirstBit = (uint8_t)uSrc2; \
    if (iFirstBit < a_cBits) \
        uResult = uSrc1 & (((a_Type)1 << iFirstBit) - 1); \
    else \
    { \
        uResult = uSrc1; \
        fEfl   |= X86_EFL_CF; \
    } \
    *puDst = uResult; \
    fEfl |= X86_EFL_CALC_ZF(uResult); \
    fEfl |= X86_EFL_CALC_SF(uResult, a_cBits); \
    *pfEFlags = fEfl; \
}

EMIT_BZHI(64, uint64_t, _fallback)
EMIT_BZHI(32, uint32_t, _fallback)
#if defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_BZHI(64, uint64_t, RT_NOTHING)
#endif
#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_BZHI(32, uint32_t, RT_NOTHING)
#endif

/*
 * POPCNT
 */
RT_ALIGNAS_VAR(64) static uint8_t const g_abBitCounts6[64] =
{
    0, 1, 1, 2,  1, 2, 2, 3,  1, 2, 2, 3,  2, 3, 3, 4,
    1, 2, 2, 3,  2, 3, 3, 4,  2, 3, 3, 4,  3, 4, 4, 5,
    1, 2, 2, 3,  2, 3, 3, 4,  2, 3, 3, 4,  3, 4, 4, 5,
    2, 3, 3, 4,  3, 4, 4, 5,  3, 4, 4, 5,  4, 5, 5, 6,
};

/** @todo Use native popcount where possible and employ some more efficient
 *        algorithm here (or in asm.h fallback)! */

DECLINLINE(uint8_t) iemPopCountU16(uint16_t u16)
{
    return g_abBitCounts6[ u16        & 0x3f]
        +  g_abBitCounts6[(u16 >> 6)  & 0x3f]
        +  g_abBitCounts6[(u16 >> 12) & 0x3f];
}

DECLINLINE(uint8_t) iemPopCountU32(uint32_t u32)
{
    return g_abBitCounts6[ u32        & 0x3f]
        +  g_abBitCounts6[(u32 >> 6)  & 0x3f]
        +  g_abBitCounts6[(u32 >> 12) & 0x3f]
        +  g_abBitCounts6[(u32 >> 18) & 0x3f]
        +  g_abBitCounts6[(u32 >> 24) & 0x3f]
        +  g_abBitCounts6[(u32 >> 30) & 0x3f];
}

DECLINLINE(uint8_t) iemPopCountU64(uint64_t u64)
{
    return g_abBitCounts6[ u64        & 0x3f]
        +  g_abBitCounts6[(u64 >> 6)  & 0x3f]
        +  g_abBitCounts6[(u64 >> 12) & 0x3f]
        +  g_abBitCounts6[(u64 >> 18) & 0x3f]
        +  g_abBitCounts6[(u64 >> 24) & 0x3f]
        +  g_abBitCounts6[(u64 >> 30) & 0x3f]
        +  g_abBitCounts6[(u64 >> 36) & 0x3f]
        +  g_abBitCounts6[(u64 >> 42) & 0x3f]
        +  g_abBitCounts6[(u64 >> 48) & 0x3f]
        +  g_abBitCounts6[(u64 >> 54) & 0x3f]
        +  g_abBitCounts6[(u64 >> 60) & 0x3f];
}

#define EMIT_POPCNT(a_cBits, a_Type, a_Suffix) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_popcnt_u,a_cBits,a_Suffix),(uint32_t fEFlags, a_Type *puDst, a_Type uSrc)) \
{ \
    fEFlags &= ~(X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF); \
    a_Type      uResult; \
    if (uSrc) \
        uResult = iemPopCountU ## a_cBits(uSrc); \
    else \
    { \
        fEFlags |= X86_EFL_ZF; \
        uResult  = 0; \
    } \
    *puDst = uResult; \
    return fEFlags; \
}

EMIT_POPCNT(64, uint64_t, _fallback)
EMIT_POPCNT(32, uint32_t, _fallback)
EMIT_POPCNT(16, uint16_t, _fallback)
#if defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_POPCNT(64, uint64_t, RT_NOTHING)
#endif
#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_POPCNT(32, uint32_t, RT_NOTHING)
EMIT_POPCNT(16, uint16_t, RT_NOTHING)
#endif


#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)

/*
 * XCHG
 */

IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u64_locked,(uint64_t *puMem, uint64_t *puReg))
{
#if ARCH_BITS >= 64
    *puReg = ASMAtomicXchgU64(puMem, *puReg);
#else
    uint64_t uOldMem = *puMem;
    while (!ASMAtomicCmpXchgExU64(puMem, *puReg, uOldMem, &uOldMem))
        ASMNopPause();
    *puReg = uOldMem;
#endif
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u32_locked,(uint32_t *puMem, uint32_t *puReg))
{
    *puReg = ASMAtomicXchgU32(puMem, *puReg);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u16_locked,(uint16_t *puMem, uint16_t *puReg))
{
    *puReg = ASMAtomicXchgU16(puMem, *puReg);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u8_locked,(uint8_t *puMem, uint8_t *puReg))
{
    *puReg = ASMAtomicXchgU8(puMem, *puReg);
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */


/* Unlocked variants for fDisregardLock mode: */

IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u64_unlocked,(uint64_t *puMem, uint64_t *puReg))
{
    uint64_t const uOld = *puMem;
    *puMem = *puReg;
    *puReg = uOld;
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u32_unlocked,(uint32_t *puMem, uint32_t *puReg))
{
    uint32_t const uOld = *puMem;
    *puMem = *puReg;
    *puReg = uOld;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u16_unlocked,(uint16_t *puMem, uint16_t *puReg))
{
    uint16_t const uOld = *puMem;
    *puMem = *puReg;
    *puReg = uOld;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_xchg_u8_unlocked,(uint8_t *puMem, uint8_t *puReg))
{
    uint8_t const uOld = *puMem;
    *puMem = *puReg;
    *puReg = uOld;
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */


/*
 * XADD and LOCK XADD.
 */
#define EMIT_XADD(a_cBitsWidth, a_Type) \
IEM_DECL_IMPL_DEF(void, iemAImpl_xadd_u ## a_cBitsWidth,(a_Type *puDst, a_Type *puReg, uint32_t *pfEFlags)) \
{ \
    a_Type uDst    = *puDst; \
    a_Type uResult = uDst; \
    *pfEFlags = iemAImpl_add_u ## a_cBitsWidth(*pfEFlags, &uResult, *puReg); \
    *puDst = uResult; \
    *puReg = uDst; \
} \
\
IEM_DECL_IMPL_DEF(void, iemAImpl_xadd_u ## a_cBitsWidth ## _locked,(a_Type *puDst, a_Type *puReg, uint32_t *pfEFlags)) \
{ \
    a_Type uOld = ASMAtomicUoReadU ## a_cBitsWidth(puDst); \
    a_Type uResult; \
    uint32_t fEflTmp; \
    do \
    { \
        uResult = uOld; \
        fEflTmp = iemAImpl_add_u ## a_cBitsWidth(*pfEFlags, &uResult, *puReg); \
    } while (!ASMAtomicCmpXchgExU ## a_cBitsWidth(puDst, uResult, uOld, &uOld)); \
    *puReg    = uOld; \
    *pfEFlags = fEflTmp; \
}
EMIT_XADD(64, uint64_t)
# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_XADD(32, uint32_t)
EMIT_XADD(16, uint16_t)
EMIT_XADD(8, uint8_t)
# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */

#endif

/*
 * CMPXCHG, CMPXCHG8B, CMPXCHG16B
 *
 * Note! We don't have non-locking/atomic cmpxchg primitives, so all cmpxchg
 *       instructions are emulated as locked.
 */
#if defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u8_locked, (uint8_t  *pu8Dst,  uint8_t  *puAl,  uint8_t  uSrcReg, uint32_t *pEFlags))
{
    uint8_t uOld = *puAl;
    if (ASMAtomicCmpXchgExU8(pu8Dst, uSrcReg, uOld, puAl))
        Assert(*puAl == uOld);
    *pEFlags = iemAImpl_cmp_u8(*pEFlags, &uOld, *puAl);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u16_locked,(uint16_t *pu16Dst, uint16_t *puAx,  uint16_t uSrcReg, uint32_t *pEFlags))
{
    uint16_t uOld = *puAx;
    if (ASMAtomicCmpXchgExU16(pu16Dst, uSrcReg, uOld, puAx))
        Assert(*puAx == uOld);
    *pEFlags = iemAImpl_cmp_u16(*pEFlags, &uOld, *puAx);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u32_locked,(uint32_t *pu32Dst, uint32_t *puEax, uint32_t uSrcReg, uint32_t *pEFlags))
{
    uint32_t uOld = *puEax;
    if (ASMAtomicCmpXchgExU32(pu32Dst, uSrcReg, uOld, puEax))
        Assert(*puEax == uOld);
    *pEFlags = iemAImpl_cmp_u32(*pEFlags, &uOld, *puEax);
}


# if ARCH_BITS == 32
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u64_locked,(uint64_t *pu64Dst, uint64_t *puRax, uint64_t *puSrcReg, uint32_t *pEFlags))
# else
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u64_locked,(uint64_t *pu64Dst, uint64_t *puRax, uint64_t uSrcReg, uint32_t *pEFlags))
# endif
{
# if ARCH_BITS == 32
    uint64_t const uSrcReg = *puSrcReg;
# endif
    uint64_t uOld = *puRax;
    if (ASMAtomicCmpXchgExU64(pu64Dst, uSrcReg, uOld, puRax))
        Assert(*puRax == uOld);
    *pEFlags = iemAImpl_cmp_u64(*pEFlags, &uOld, *puRax);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg8b_locked,(uint64_t *pu64Dst, PRTUINT64U pu64EaxEdx, PRTUINT64U pu64EbxEcx,
                                                   uint32_t *pEFlags))
{
    uint64_t const uNew = pu64EbxEcx->u;
    uint64_t const uOld = pu64EaxEdx->u;
    if (ASMAtomicCmpXchgExU64(pu64Dst, uNew, uOld, &pu64EaxEdx->u))
    {
        Assert(pu64EaxEdx->u == uOld);
        *pEFlags |= X86_EFL_ZF;
    }
    else
        *pEFlags &= ~X86_EFL_ZF;
}


# if defined(RT_ARCH_AMD64) || defined(RT_ARCH_ARM64)
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg16b_locked,(PRTUINT128U pu128Dst, PRTUINT128U pu128RaxRdx, PRTUINT128U pu128RbxRcx,
                                                    uint32_t *pEFlags))
{
#  ifdef VBOX_STRICT
    RTUINT128U const uOld = *pu128RaxRdx;
#  endif
#  if defined(RT_ARCH_AMD64)
    if (ASMAtomicCmpXchgU128v2(&pu128Dst->u, pu128RbxRcx->s.Hi, pu128RbxRcx->s.Lo, pu128RaxRdx->s.Hi, pu128RaxRdx->s.Lo,
                               &pu128RaxRdx->u))
#  else
    if (ASMAtomicCmpXchgU128(&pu128Dst->u, pu128RbxRcx->u, pu128RaxRdx->u, &pu128RaxRdx->u))
#  endif
    {
        Assert(pu128RaxRdx->s.Lo == uOld.s.Lo && pu128RaxRdx->s.Hi == uOld.s.Hi);
        *pEFlags |= X86_EFL_ZF;
    }
    else
        *pEFlags &= ~X86_EFL_ZF;
}
# endif

#endif /* defined(IEM_WITHOUT_ASSEMBLY) */

# if !defined(RT_ARCH_ARM64) /** @todo may need this for unaligned accesses... */
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg16b_fallback,(PRTUINT128U pu128Dst, PRTUINT128U pu128RaxRdx,
                                                      PRTUINT128U pu128RbxRcx, uint32_t *pEFlags))
{
    RTUINT128U u128Tmp = *pu128Dst;
    if (   u128Tmp.s.Lo == pu128RaxRdx->s.Lo
        && u128Tmp.s.Hi == pu128RaxRdx->s.Hi)
    {
        *pu128Dst = *pu128RbxRcx;
        *pEFlags |= X86_EFL_ZF;
    }
    else
    {
        *pu128RaxRdx = u128Tmp;
        *pEFlags &= ~X86_EFL_ZF;
    }
}
#endif /* !RT_ARCH_ARM64 */

#if defined(IEM_WITHOUT_ASSEMBLY)

/* Unlocked versions mapped to the locked ones: */

IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u8,        (uint8_t  *pu8Dst,  uint8_t  *puAl,  uint8_t  uSrcReg, uint32_t *pEFlags))
{
    iemAImpl_cmpxchg_u8_locked(pu8Dst, puAl, uSrcReg, pEFlags);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u16,       (uint16_t *pu16Dst, uint16_t *puAx,  uint16_t uSrcReg, uint32_t *pEFlags))
{
# if 0
    /* If correctly aligned, used the locked variation. */
    if (!((uintptr_t)pu16Dst & 1))
        iemAImpl_cmpxchg_u16_locked(pu16Dst, puAx, uSrcReg, pEFlags);
    else
# endif
    {
        /* Otherwise emulate it as best as we can. */
        uint16_t const uOld = *puAx;
        uint16_t const uDst = *pu16Dst;
        if (uOld == uDst)
        {
            *pu16Dst = uSrcReg;
            *pEFlags = iemAImpl_cmp_u16(*pEFlags, &uOld, uOld);
        }
        else
        {
            *puAx = uDst;
            *pEFlags = iemAImpl_cmp_u16(*pEFlags, &uOld, uDst);
        }
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u32,       (uint32_t *pu32Dst, uint32_t *puEax, uint32_t uSrcReg, uint32_t *pEFlags))
{
# if 0
    /* If correctly aligned, used the locked variation. */
    if (!((uintptr_t)pu32Dst & 3))
        iemAImpl_cmpxchg_u32_locked(pu32Dst, puEax, uSrcReg, pEFlags);
    else
# endif
    {
        /* Otherwise emulate it as best as we can. */
        uint32_t const uOld = *puEax;
        uint32_t const uDst = *pu32Dst;
        if (uOld == uDst)
        {
            *pu32Dst = uSrcReg;
            *pEFlags = iemAImpl_cmp_u32(*pEFlags, &uOld, uOld);
        }
        else
        {
            *puEax = uDst;
            *pEFlags = iemAImpl_cmp_u32(*pEFlags, &uOld, uDst);
        }
    }
}


# if ARCH_BITS == 32
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u64,       (uint64_t *pu64Dst, uint64_t *puRax, uint64_t *puSrcReg, uint32_t *pEFlags))
{
#  if 0
    /* If correctly aligned, used the locked variation. */
    if (!((uintptr_t)pu32Dst & 7))
        iemAImpl_cmpxchg_u64_locked(pu64Dst, puRax, puSrcReg, pEFlags);
    else
#  endif
    {
        /* Otherwise emulate it as best as we can. */
        uint64_t const uOld = *puRax;
        uint64_t const uSrc = *puSrcReg;
        uint64_t const uDst = *pu64Dst;
        if (uOld == uDst)
        {
            *pu64Dst = uSrc;
            *pEFlags = iemAImpl_cmp_u64(*pEFlags, &uOld, uOld);
        }
        else
        {
            *puRax = uDst;
            *pEFlags = iemAImpl_cmp_u64(*pEFlags, &uOld, uDst);
        }
    }
}
# else  /* ARCH_BITS != 32 */
IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg_u64,       (uint64_t *pu64Dst, uint64_t *puRax, uint64_t uSrcReg, uint32_t *pEFlags))
{
#  if 0
    /* If correctly aligned, used the locked variation. */
    if (!((uintptr_t)pu64Dst & 7))
        iemAImpl_cmpxchg_u64_locked(pu64Dst, puRax, uSrcReg, pEFlags);
    else
#  endif
    {
        /* Otherwise emulate it as best as we can. */
        uint64_t const uOld = *puRax;
        uint64_t const uDst = *pu64Dst;
        if (uOld == uDst)
        {
            *pu64Dst = uSrcReg;
            *pEFlags = iemAImpl_cmp_u64(*pEFlags, &uOld, uOld);
        }
        else
        {
            *puRax = uDst;
            *pEFlags = iemAImpl_cmp_u64(*pEFlags, &uOld, uDst);
        }
    }
}
# endif /* ARCH_BITS != 32 */


IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg8b,(uint64_t *pu64Dst, PRTUINT64U pu64EaxEdx, PRTUINT64U pu64EbxEcx, uint32_t *pEFlags))
{
# if 0
    /* If correctly aligned, used the locked variation. */
    if (!((uintptr_t)pu64Dst & 7))
        iemAImpl_cmpxchg8b_locked(pu64Dst, pu64EaxEdx, pu64EbxEcx, pEFlags);
    else
# endif
    {
        /* Otherwise emulate it as best as we can. */
        uint64_t const uNew = pu64EbxEcx->u;
        uint64_t const uOld = pu64EaxEdx->u;
        uint64_t const uDst = *pu64Dst;
        if (uDst == uOld)
        {
            *pu64Dst = uNew;
            *pEFlags |= X86_EFL_ZF;
        }
        else
        {
            pu64EaxEdx->u = uDst;
            *pEFlags &= ~X86_EFL_ZF;
        }
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_cmpxchg16b,(PRTUINT128U pu128Dst, PRTUINT128U pu128RaxRdx, PRTUINT128U pu128RbxRcx,
                                             uint32_t *pEFlags))
{
# if 0
    /* If correctly aligned, used the locked variation. */
    if (!((uintptr_t)pu64Dst & 15))
        iemAImpl_cmpxchg16b_locked(pu128Dst, pu128RaxRdx, pu128RbxRcx, pEFlags);
    else
# endif
    {
        /* Otherwise emulate it as best as we can. */
# ifdef RT_COMPILER_WITH_128BIT_INT_TYPES
        uint128_t const uNew = pu128RbxRcx->u;
        uint128_t const uOld = pu128RaxRdx->u;
        uint128_t const uDst = pu128Dst->u;
        if (uDst == uOld)
        {
            pu128Dst->u = uNew;
            *pEFlags |= X86_EFL_ZF;
        }
        else
        {
            pu128RaxRdx->u = uDst;
            *pEFlags &= ~X86_EFL_ZF;
        }
# else
        RTUINT128U const uNew = *pu128RbxRcx;
        RTUINT128U const uOld = *pu128RaxRdx;
        RTUINT128U const uDst = *pu128Dst;
        if (   uDst.s.Lo == uOld.s.Lo
            && uDst.s.Hi == uOld.s.Hi)
        {
            *pu128Dst = uNew;
            *pEFlags |= X86_EFL_ZF;
        }
        else
        {
            *pu128RaxRdx = uDst;
            *pEFlags &= ~X86_EFL_ZF;
        }
# endif
    }
}

#endif /* defined(IEM_WITHOUT_ASSEMBLY) */

#if (!defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)) \
 && !defined(DOXYGEN_RUNNING) /* Doxygen has some groking issues here and ends up mixing up input. Not worth tracking down now. */

/*
 * MUL, IMUL, DIV and IDIV helpers.
 *
 * - The U64 versions must use 128-bit intermediates, so we need to abstract the
 *   division step so we can select between using C operators and
 *   RTUInt128DivRem/RTUInt128MulU64ByU64.
 *
 * - The U8 versions work returns output in AL + AH instead of xDX + xAX, with the
 *   IDIV/DIV taking all the input in AX too.  This means we have to abstract some
 *   input loads and the result storing.
 */

DECLINLINE(void) RTUInt128DivRemByU64(PRTUINT128U pQuotient, PRTUINT128U pRemainder, PCRTUINT128U pDividend, uint64_t u64Divisor)
{
# ifdef __GNUC__ /* GCC maybe really annoying in function. */
    pQuotient->s.Lo = 0;
    pQuotient->s.Hi = 0;
# endif
    RTUINT128U Divisor;
    Divisor.s.Lo = u64Divisor;
    Divisor.s.Hi = 0;
    RTUInt128DivRem(pQuotient, pRemainder, pDividend, &Divisor);
}

# define DIV_LOAD(a_Dividend)  \
    a_Dividend.s.Lo = *puA, a_Dividend.s.Hi = *puD
# define DIV_LOAD_U8(a_Dividend) \
    a_Dividend.u = *puAX

# define DIV_STORE(a_Quotient, a_uReminder)    *puA  = (a_Quotient),    *puD = (a_uReminder)
# define DIV_STORE_U8(a_Quotient, a_uReminder) *puAX = (uint8_t)(a_Quotient) | ((uint16_t)(a_uReminder) << 8)

# define MUL_LOAD_F1()                         *puA
# define MUL_LOAD_F1_U8()                      ((uint8_t)*puAX)

# define MUL_STORE(a_Result)                   *puA  = (a_Result).s.Lo, *puD = (a_Result).s.Hi
# define MUL_STORE_U8(a_Result)                *puAX = a_Result.u

# define MULDIV_NEG(a_Value, a_cBitsWidth2x) \
    (a_Value).u = UINT ## a_cBitsWidth2x ## _C(0) - (a_Value).u
# define MULDIV_NEG_U128(a_Value, a_cBitsWidth2x) \
    RTUInt128AssignNeg(&(a_Value))

# define MULDIV_MUL(a_Result, a_Factor1, a_Factor2, a_cBitsWidth2x) \
    (a_Result).u = (uint ## a_cBitsWidth2x ## _t)(a_Factor1) * (a_Factor2)
# define MULDIV_MUL_U128(a_Result, a_Factor1, a_Factor2, a_cBitsWidth2x) \
    RTUInt128MulU64ByU64(&(a_Result), a_Factor1, a_Factor2);

# define MULDIV_MODDIV(a_Quotient, a_Remainder, a_Dividend, a_uDivisor) \
    a_Quotient.u = (a_Dividend).u / (a_uDivisor), \
    a_Remainder.u = (a_Dividend).u % (a_uDivisor)
# define MULDIV_MODDIV_U128(a_Quotient, a_Remainder, a_Dividend, a_uDivisor) \
    RTUInt128DivRemByU64(&a_Quotient, &a_Remainder, &a_Dividend, a_uDivisor)


/*
 * MUL
 */
# define EMIT_MUL_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoadF1, a_fnStore, a_fnMul, a_Suffix, a_fIntelFlags) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_mul_u,a_cBitsWidth,a_Suffix), a_Args) \
{ \
    RTUINT ## a_cBitsWidth2x ## U Result; \
    a_fnMul(Result, a_fnLoadF1(), uFactor, a_cBitsWidth2x); \
    a_fnStore(Result); \
    \
    /* Calc EFLAGS: */ \
    if (a_fIntelFlags) \
    { /* Intel: 6700K and 10980XE behavior */ \
        fEFlags &= ~(X86_EFL_SF | X86_EFL_CF | X86_EFL_OF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_PF); \
        if (Result.s.Lo & RT_BIT_64(a_cBitsWidth - 1)) \
            fEFlags |= X86_EFL_SF; \
        fEFlags |= IEM_EFL_CALC_PARITY(Result.s.Lo); \
        if (Result.s.Hi != 0) \
            fEFlags |= X86_EFL_CF | X86_EFL_OF; \
    } \
    else \
    { /* AMD: 3990X */ \
        if (Result.s.Hi != 0) \
            fEFlags |= X86_EFL_CF | X86_EFL_OF; \
        else \
            fEFlags &= ~(X86_EFL_CF | X86_EFL_OF); \
    } \
    return fEFlags; \
} \

# define EMIT_MUL(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoadF1, a_fnStore, a_fnMul) \
    EMIT_MUL_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoadF1, a_fnStore, a_fnMul, RT_NOTHING, 1) \
    EMIT_MUL_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoadF1, a_fnStore, a_fnMul, _intel,     1) \
    EMIT_MUL_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoadF1, a_fnStore, a_fnMul, _amd,       0) \

# ifndef DOXYGEN_RUNNING /* this totally confuses doxygen for some reason */
EMIT_MUL(64, 128, (uint64_t *puA, uint64_t *puD, uint64_t uFactor, uint32_t fEFlags), (puA, puD, uFactor, pfEFlags),
         MUL_LOAD_F1, MUL_STORE, MULDIV_MUL_U128)
#  if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_MUL(32, 64, (uint32_t *puA, uint32_t *puD, uint32_t uFactor, uint32_t fEFlags),  (puA, puD, uFactor, pfEFlags),
         MUL_LOAD_F1, MUL_STORE, MULDIV_MUL)
EMIT_MUL(16, 32, (uint16_t *puA, uint16_t *puD, uint16_t uFactor, uint32_t fEFlags),  (puA, puD, uFactor, pfEFlags),
         MUL_LOAD_F1, MUL_STORE, MULDIV_MUL)
EMIT_MUL(8, 16, (uint16_t *puAX, uint8_t uFactor, uint32_t fEFlags),                  (puAX,     uFactor, pfEFlags),
         MUL_LOAD_F1_U8, MUL_STORE_U8, MULDIV_MUL)
#  endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */
# endif /* !DOXYGEN_RUNNING */

/*
 * MULX
 */
# define EMIT_MULX(a_cBitsWidth, a_cBitsWidth2x, a_uType, a_fnMul, a_Suffix) \
IEM_DECL_IMPL_DEF(void, RT_CONCAT3(iemAImpl_mulx_u,a_cBitsWidth,a_Suffix), \
    (a_uType *puDst1, a_uType *puDst2, a_uType uSrc1, a_uType uSrc2)) \
{ \
    RTUINT ## a_cBitsWidth2x ## U Result; \
    a_fnMul(Result, uSrc1, uSrc2, a_cBitsWidth2x); \
    *puDst2 = Result.s.Lo; /* Lower part first, as we should return the high part when puDst2 == puDst1. */ \
    *puDst1 = Result.s.Hi; \
} \

# ifndef DOXYGEN_RUNNING /* this totally confuses doxygen for some reason */
EMIT_MULX(64, 128, uint64_t, MULDIV_MUL_U128, RT_NOTHING)
EMIT_MULX(64, 128, uint64_t, MULDIV_MUL_U128, _fallback)
#  if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_MULX(32, 64,  uint32_t, MULDIV_MUL, RT_NOTHING)
EMIT_MULX(32, 64,  uint32_t, MULDIV_MUL, _fallback)
#  endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */
# endif /* !DOXYGEN_RUNNING */


/*
 * IMUL
 *
 * The SF, ZF, AF and PF flags are "undefined". AMD (3990x) leaves these
 * flags as is.  Whereas Intel skylake (6700K and 10980X (Cascade Lake)) always
 * clear AF and ZF and calculates SF and PF as per the lower half of the result.
 */
# define EMIT_IMUL_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoadF1, a_fnStore, a_fnNeg, a_fnMul, \
                         a_Suffix, a_fIntelFlags) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_imul_u,a_cBitsWidth,a_Suffix),a_Args) \
{ \
    RTUINT ## a_cBitsWidth2x ## U Result; \
    fEFlags &= ~(X86_EFL_CF | X86_EFL_OF); \
    \
    uint ## a_cBitsWidth ## _t const uFactor1 = a_fnLoadF1(); \
    if (!(uFactor1 & RT_BIT_64(a_cBitsWidth - 1))) \
    { \
        if (!(uFactor2 & RT_BIT_64(a_cBitsWidth - 1))) \
        { \
            a_fnMul(Result, uFactor1, uFactor2, a_cBitsWidth2x); \
            if (Result.s.Hi != 0 || Result.s.Lo >= RT_BIT_64(a_cBitsWidth - 1)) \
                fEFlags |= X86_EFL_CF | X86_EFL_OF; \
        } \
        else \
        { \
            uint ## a_cBitsWidth ## _t const uPositiveFactor2 = UINT ## a_cBitsWidth ## _C(0) - uFactor2; \
            a_fnMul(Result, uFactor1, uPositiveFactor2, a_cBitsWidth2x); \
            if (Result.s.Hi != 0 || Result.s.Lo > RT_BIT_64(a_cBitsWidth - 1)) \
                fEFlags |= X86_EFL_CF | X86_EFL_OF; \
            a_fnNeg(Result, a_cBitsWidth2x); \
        } \
    } \
    else \
    { \
        if (!(uFactor2 & RT_BIT_64(a_cBitsWidth - 1))) \
        { \
            uint ## a_cBitsWidth ## _t const uPositiveFactor1 = UINT ## a_cBitsWidth ## _C(0) - uFactor1; \
            a_fnMul(Result, uPositiveFactor1, uFactor2, a_cBitsWidth2x); \
            if (Result.s.Hi != 0 || Result.s.Lo > RT_BIT_64(a_cBitsWidth - 1)) \
                fEFlags |= X86_EFL_CF | X86_EFL_OF; \
            a_fnNeg(Result, a_cBitsWidth2x); \
        } \
        else \
        { \
            uint ## a_cBitsWidth ## _t const uPositiveFactor1 = UINT ## a_cBitsWidth ## _C(0) - uFactor1; \
            uint ## a_cBitsWidth ## _t const uPositiveFactor2 = UINT ## a_cBitsWidth ## _C(0) - uFactor2; \
            a_fnMul(Result, uPositiveFactor1, uPositiveFactor2, a_cBitsWidth2x); \
            if (Result.s.Hi != 0 || Result.s.Lo >= RT_BIT_64(a_cBitsWidth - 1)) \
                fEFlags |= X86_EFL_CF | X86_EFL_OF; \
        } \
    } \
    a_fnStore(Result); \
    \
    if (a_fIntelFlags) \
    { \
        fEFlags &= ~(X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_PF); \
        if (Result.s.Lo & RT_BIT_64(a_cBitsWidth - 1)) \
            fEFlags |= X86_EFL_SF;  \
        fEFlags |= IEM_EFL_CALC_PARITY(Result.s.Lo & 0xff); \
    } \
    return fEFlags; \
}
# define EMIT_IMUL(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoadF1, a_fnStore, a_fnNeg, a_fnMul) \
    EMIT_IMUL_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoadF1, a_fnStore, a_fnNeg, a_fnMul, RT_NOTHING, 1) \
    EMIT_IMUL_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoadF1, a_fnStore, a_fnNeg, a_fnMul, _intel,     1) \
    EMIT_IMUL_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoadF1, a_fnStore, a_fnNeg, a_fnMul, _amd,       0)

# ifndef DOXYGEN_RUNNING /* this totally confuses doxygen for some reason */
EMIT_IMUL(64, 128, (uint64_t *puA, uint64_t *puD, uint64_t uFactor2, uint32_t fEFlags), (puA, puD, uFactor2, pfEFlags),
          MUL_LOAD_F1, MUL_STORE, MULDIV_NEG_U128, MULDIV_MUL_U128)
#  if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_IMUL(32, 64, (uint32_t *puA, uint32_t *puD, uint32_t uFactor2, uint32_t fEFlags),  (puA, puD, uFactor2, pfEFlags),
          MUL_LOAD_F1, MUL_STORE, MULDIV_NEG, MULDIV_MUL)
EMIT_IMUL(16, 32, (uint16_t *puA, uint16_t *puD, uint16_t uFactor2, uint32_t fEFlags),  (puA, puD, uFactor2, pfEFlags),
          MUL_LOAD_F1, MUL_STORE, MULDIV_NEG, MULDIV_MUL)
EMIT_IMUL(8, 16, (uint16_t *puAX, uint8_t uFactor2, uint32_t fEFlags),                  (puAX,     uFactor2, pfEFlags),
          MUL_LOAD_F1_U8, MUL_STORE_U8, MULDIV_NEG, MULDIV_MUL)
#  endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */
# endif /* !DOXYGEN_RUNNING */


/*
 * IMUL with two operands are mapped onto the three operand variant, ignoring
 * the high part of the product.
 */
# define EMIT_IMUL_TWO(a_cBits, a_uType) \
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_imul_two_u ## a_cBits,(uint32_t fEFlags, a_uType *puDst, a_uType uSrc)) \
{ \
    a_uType uIgn; \
    return iemAImpl_imul_u ## a_cBits(puDst, &uIgn, uSrc, fEFlags); \
} \
\
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_imul_two_u ## a_cBits ## _intel,(uint32_t fEFlags, a_uType *puDst, a_uType uSrc)) \
{ \
    a_uType uIgn; \
    return iemAImpl_imul_u ## a_cBits ## _intel(puDst, &uIgn, uSrc, fEFlags); \
} \
\
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_imul_two_u ## a_cBits ## _amd,(uint32_t fEFlags, a_uType *puDst, a_uType uSrc)) \
{ \
    a_uType uIgn; \
    return iemAImpl_imul_u ## a_cBits ## _amd(puDst, &uIgn, uSrc, fEFlags); \
}

EMIT_IMUL_TWO(64, uint64_t)
# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_IMUL_TWO(32, uint32_t)
EMIT_IMUL_TWO(16, uint16_t)
# endif


/*
 * DIV
 */
# define EMIT_DIV_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoad, a_fnStore, a_fnDivRem, \
                        a_Suffix, a_fIntelFlags) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_div_u,a_cBitsWidth,a_Suffix),a_Args) \
{ \
    RTUINT ## a_cBitsWidth2x ## U Dividend; \
    a_fnLoad(Dividend); \
    if (   uDivisor != 0 \
        && Dividend.s.Hi < uDivisor) \
    { \
        RTUINT ## a_cBitsWidth2x ## U Remainder, Quotient; \
        a_fnDivRem(Quotient, Remainder, Dividend, uDivisor); \
        a_fnStore(Quotient.s.Lo, Remainder.s.Lo); \
        \
        /* Calc EFLAGS: Intel 6700K and 10980XE leaves them alone.  AMD 3990X sets AF and clears PF, ZF and SF. */ \
        if (!a_fIntelFlags) \
            fEFlags = (fEFlags & ~(X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF)) | X86_EFL_AF; \
        return fEFlags; \
    } \
    /* #DE */ \
    return 0; \
}
# define EMIT_DIV(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoad, a_fnStore, a_fnDivRem) \
    EMIT_DIV_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoad, a_fnStore, a_fnDivRem, RT_NOTHING, 1) \
    EMIT_DIV_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoad, a_fnStore, a_fnDivRem, _intel,     1) \
    EMIT_DIV_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoad, a_fnStore, a_fnDivRem, _amd,       0)

# ifndef DOXYGEN_RUNNING /* this totally confuses doxygen for some reason */
EMIT_DIV(64,128,(uint64_t *puA, uint64_t *puD, uint64_t uDivisor, uint32_t fEFlags), (puA, puD, uDivisor, pfEFlags),
         DIV_LOAD, DIV_STORE, MULDIV_MODDIV_U128)
#  if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_DIV(32,64, (uint32_t *puA, uint32_t *puD, uint32_t uDivisor, uint32_t fEFlags), (puA, puD, uDivisor, pfEFlags),
         DIV_LOAD, DIV_STORE, MULDIV_MODDIV)
EMIT_DIV(16,32, (uint16_t *puA, uint16_t *puD, uint16_t uDivisor, uint32_t fEFlags), (puA, puD, uDivisor, pfEFlags),
         DIV_LOAD, DIV_STORE, MULDIV_MODDIV)
EMIT_DIV(8,16,  (uint16_t *puAX, uint8_t uDivisor, uint32_t fEFlags),                (puAX,     uDivisor, pfEFlags),
         DIV_LOAD_U8, DIV_STORE_U8, MULDIV_MODDIV)
#  endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */
# endif /* !DOXYGEN_RUNNING */


/*
 * IDIV
 *
 * EFLAGS are ignored and left as-is by Intel 6700K and 10980XE.  AMD 3990X will
 * set AF and clear PF, ZF and SF just like it does for DIV.
 *
 */
# define EMIT_IDIV_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoad, a_fnStore, a_fnNeg, a_fnDivRem, \
                         a_Suffix, a_fIntelFlags) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_idiv_u,a_cBitsWidth,a_Suffix),a_Args) \
{ \
    /* Note! Skylake leaves all flags alone. */ \
    \
    /** @todo overflow checks */ \
    if (uDivisor != 0) \
    { \
        /* \
         * Convert to unsigned division. \
         */ \
        RTUINT ## a_cBitsWidth2x ## U Dividend; \
        a_fnLoad(Dividend); \
        bool const fSignedDividend = RT_BOOL(Dividend.s.Hi & RT_BIT_64(a_cBitsWidth - 1)); \
        if (fSignedDividend) \
            a_fnNeg(Dividend, a_cBitsWidth2x); \
        \
        uint ## a_cBitsWidth ## _t uDivisorPositive; \
        if (!(uDivisor & RT_BIT_64(a_cBitsWidth - 1))) \
            uDivisorPositive = uDivisor; \
        else \
            uDivisorPositive = UINT ## a_cBitsWidth ## _C(0) - uDivisor; \
        \
        RTUINT ## a_cBitsWidth2x ## U Remainder, Quotient; \
        a_fnDivRem(Quotient, Remainder, Dividend, uDivisorPositive); \
        \
        /* \
         * Setup the result, checking for overflows. \
         */ \
        if (!(uDivisor & RT_BIT_64(a_cBitsWidth - 1))) \
        { \
            if (!fSignedDividend) \
            { \
                /* Positive divisor, positive dividend => result positive. */ \
                if (Quotient.s.Hi == 0 && Quotient.s.Lo <= (uint ## a_cBitsWidth ## _t)INT ## a_cBitsWidth ## _MAX) \
                { \
                    a_fnStore(Quotient.s.Lo, Remainder.s.Lo); \
                    if (!a_fIntelFlags) \
                        fEFlags = (fEFlags & ~(X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF)) | X86_EFL_AF; \
                    return fEFlags; \
                } \
            } \
            else \
            { \
                /* Positive divisor, negative dividend => result negative. */ \
                if (Quotient.s.Hi == 0 && Quotient.s.Lo <= RT_BIT_64(a_cBitsWidth - 1)) \
                { \
                    a_fnStore(UINT ## a_cBitsWidth ## _C(0) - Quotient.s.Lo, UINT ## a_cBitsWidth ## _C(0) - Remainder.s.Lo); \
                    if (!a_fIntelFlags) \
                        fEFlags = (fEFlags & ~(X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF)) | X86_EFL_AF; \
                    return fEFlags; \
                } \
            } \
        } \
        else \
        { \
            if (!fSignedDividend) \
            { \
                /* Negative divisor, positive dividend => negative quotient, positive remainder. */ \
                if (Quotient.s.Hi == 0 && Quotient.s.Lo <= RT_BIT_64(a_cBitsWidth - 1)) \
                { \
                    a_fnStore(UINT ## a_cBitsWidth ## _C(0) - Quotient.s.Lo, Remainder.s.Lo); \
                    if (!a_fIntelFlags) \
                        fEFlags = (fEFlags & ~(X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF)) | X86_EFL_AF; \
                    return fEFlags; \
                } \
            } \
            else \
            { \
                /* Negative divisor, negative dividend => positive quotient, negative remainder. */ \
                if (Quotient.s.Hi == 0 && Quotient.s.Lo <= (uint ## a_cBitsWidth ## _t)INT ## a_cBitsWidth ## _MAX) \
                { \
                    a_fnStore(Quotient.s.Lo, UINT ## a_cBitsWidth ## _C(0) - Remainder.s.Lo); \
                    if (!a_fIntelFlags) \
                        fEFlags = (fEFlags & ~(X86_EFL_PF | X86_EFL_ZF | X86_EFL_SF)) | X86_EFL_AF; \
                    return fEFlags; \
                } \
            } \
        } \
    } \
    /* #DE */ \
    return 0; \
}
# define EMIT_IDIV(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoad, a_fnStore, a_fnNeg, a_fnDivRem) \
     EMIT_IDIV_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoad, a_fnStore, a_fnNeg, a_fnDivRem, RT_NOTHING, 1) \
     EMIT_IDIV_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoad, a_fnStore, a_fnNeg, a_fnDivRem, _intel,     1) \
     EMIT_IDIV_INNER(a_cBitsWidth, a_cBitsWidth2x, a_Args, a_CallArgs, a_fnLoad, a_fnStore, a_fnNeg, a_fnDivRem, _amd,       0)

# ifndef DOXYGEN_RUNNING /* this totally confuses doxygen for some reason */
EMIT_IDIV(64,128,(uint64_t *puA, uint64_t *puD, uint64_t uDivisor, uint32_t fEFlags), (puA, puD, uDivisor, pfEFlags),
          DIV_LOAD, DIV_STORE, MULDIV_NEG_U128, MULDIV_MODDIV_U128)
#  if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_IDIV(32,64,(uint32_t *puA, uint32_t *puD, uint32_t uDivisor, uint32_t fEFlags),  (puA, puD, uDivisor, pfEFlags),
          DIV_LOAD, DIV_STORE, MULDIV_NEG, MULDIV_MODDIV)
EMIT_IDIV(16,32,(uint16_t *puA, uint16_t *puD, uint16_t uDivisor, uint32_t fEFlags),  (puA, puD, uDivisor, pfEFlags),
          DIV_LOAD, DIV_STORE, MULDIV_NEG, MULDIV_MODDIV)
EMIT_IDIV(8,16,(uint16_t *puAX, uint8_t uDivisor, uint32_t fEFlags),                  (puAX,     uDivisor, pfEFlags),
          DIV_LOAD_U8, DIV_STORE_U8, MULDIV_NEG, MULDIV_MODDIV)
#  endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */
# endif /* !DOXYGEN_RUNNING */

#endif /* (!defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)) && !defined(DOXYGEN_RUNNING) */


/*********************************************************************************************************************************
*   Unary operations.                                                                                                            *
*********************************************************************************************************************************/
#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)

/** @def IEM_EFL_UPDATE_STATUS_BITS_FOR_INC_DEC
 * Updates the status bits (CF, PF, AF, ZF, SF, and OF) for an INC or DEC instruction.
 *
 * CF is NOT modified for hysterical raisins (allegedly for carrying and
 * borrowing in arithmetic loops on intel 8008).
 *
 * @returns Status bits.
 * @param   a_pfEFlags      Pointer to the 32-bit EFLAGS value to update.
 * @param   a_uResult       Unsigned result value.
 * @param   a_uDst          The original destination value (for AF calc).
 * @param   a_cBitsWidth    The width of the result (8, 16, 32, 64).
 * @param   a_OfMethod      0 for INC-style, 1 for DEC-style.
 */
#define IEM_EFL_UPDATE_STATUS_BITS_FOR_INC_DEC(a_pfEFlags, a_uResult, a_uDst, a_cBitsWidth, a_OfMethod) \
    do { \
        uint32_t fEflTmp = *(a_pfEFlags); \
        fEflTmp &= ~X86_EFL_STATUS_BITS | X86_EFL_CF; \
        fEflTmp |= IEM_EFL_CALC_PARITY(a_uResult); \
        fEflTmp |= ((uint32_t)(a_uResult) ^ (uint32_t)(a_uDst)) & X86_EFL_AF; \
        fEflTmp |= X86_EFL_CALC_ZF(a_uResult); \
        fEflTmp |= X86_EFL_CALC_SF(a_uResult, a_cBitsWidth); \
        fEflTmp |= X86_EFL_GET_OF_ ## a_cBitsWidth(a_OfMethod == 0 ? (((a_uDst) ^ RT_BIT_64(a_cBitsWidth - 1)) & (a_uResult)) \
                                                                   : ((a_uDst) & ((a_uResult) ^ RT_BIT_64(a_cBitsWidth - 1))) ); \
        *(a_pfEFlags) = fEflTmp; \
    } while (0)

/*
 * INC
 */

IEM_DECL_IMPL_DEF(void, iemAImpl_inc_u64,(uint64_t  *puDst,  uint32_t *pfEFlags))
{
    uint64_t uDst    = *puDst;
    uint64_t uResult = uDst + 1;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_INC_DEC(pfEFlags, uResult, uDst, 64, 0 /*INC*/);
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(void, iemAImpl_inc_u32,(uint32_t  *puDst,  uint32_t *pfEFlags))
{
    uint32_t uDst    = *puDst;
    uint32_t uResult = uDst + 1;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_INC_DEC(pfEFlags, uResult, uDst, 32, 0 /*INC*/);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_inc_u16,(uint16_t  *puDst,  uint32_t *pfEFlags))
{
    uint16_t uDst    = *puDst;
    uint16_t uResult = uDst + 1;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_INC_DEC(pfEFlags, uResult, uDst, 16, 0 /*INC*/);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_inc_u8,(uint8_t  *puDst,  uint32_t *pfEFlags))
{
    uint8_t uDst    = *puDst;
    uint8_t uResult = uDst + 1;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_INC_DEC(pfEFlags, uResult, uDst, 8, 0 /*INC*/);
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */


/*
 * DEC
 */

IEM_DECL_IMPL_DEF(void, iemAImpl_dec_u64,(uint64_t  *puDst,  uint32_t *pfEFlags))
{
    uint64_t uDst    = *puDst;
    uint64_t uResult = uDst - 1;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_INC_DEC(pfEFlags, uResult, uDst, 64, 1 /*INC*/);
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(void, iemAImpl_dec_u32,(uint32_t  *puDst,  uint32_t *pfEFlags))
{
    uint32_t uDst    = *puDst;
    uint32_t uResult = uDst - 1;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_INC_DEC(pfEFlags, uResult, uDst, 32, 1 /*INC*/);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_dec_u16,(uint16_t  *puDst,  uint32_t *pfEFlags))
{
    uint16_t uDst    = *puDst;
    uint16_t uResult = uDst - 1;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_INC_DEC(pfEFlags, uResult, uDst, 16, 1 /*INC*/);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_dec_u8,(uint8_t  *puDst,  uint32_t *pfEFlags))
{
    uint8_t uDst    = *puDst;
    uint8_t uResult = uDst - 1;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_INC_DEC(pfEFlags, uResult, uDst, 8, 1 /*INC*/);
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */


/*
 * NOT
 */

IEM_DECL_IMPL_DEF(void, iemAImpl_not_u64,(uint64_t  *puDst,  uint32_t *pfEFlags))
{
    uint64_t uDst    = *puDst;
    uint64_t uResult = ~uDst;
    *puDst = uResult;
    /* EFLAGS are not modified. */
    RT_NOREF_PV(pfEFlags);
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(void, iemAImpl_not_u32,(uint32_t  *puDst,  uint32_t *pfEFlags))
{
    uint32_t uDst    = *puDst;
    uint32_t uResult = ~uDst;
    *puDst = uResult;
    /* EFLAGS are not modified. */
    RT_NOREF_PV(pfEFlags);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_not_u16,(uint16_t  *puDst,  uint32_t *pfEFlags))
{
    uint16_t uDst    = *puDst;
    uint16_t uResult = ~uDst;
    *puDst = uResult;
    /* EFLAGS are not modified. */
    RT_NOREF_PV(pfEFlags);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_not_u8,(uint8_t  *puDst,  uint32_t *pfEFlags))
{
    uint8_t uDst    = *puDst;
    uint8_t uResult = ~uDst;
    *puDst = uResult;
    /* EFLAGS are not modified. */
    RT_NOREF_PV(pfEFlags);
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */


/*
 * NEG
 */

/**
 * Updates the status bits (CF, PF, AF, ZF, SF, and OF) for an NEG instruction.
 *
 * @returns Status bits.
 * @param   a_pfEFlags      Pointer to the 32-bit EFLAGS value to update.
 * @param   a_uResult       Unsigned result value.
 * @param   a_uDst          The original destination value (for AF calc).
 * @param   a_cBitsWidth    The width of the result (8, 16, 32, 64).
 */
#define IEM_EFL_UPDATE_STATUS_BITS_FOR_NEG(a_pfEFlags, a_uResult, a_uDst, a_cBitsWidth) \
    do { \
        uint32_t fEflTmp = *(a_pfEFlags); \
        fEflTmp &= ~X86_EFL_STATUS_BITS & ~X86_EFL_CF; \
        fEflTmp |= ((a_uDst) != 0) << X86_EFL_CF_BIT; \
        fEflTmp |= IEM_EFL_CALC_PARITY(a_uResult); \
        fEflTmp |= ((uint32_t)(a_uResult) ^ (uint32_t)(a_uDst)) & X86_EFL_AF; \
        fEflTmp |= X86_EFL_CALC_ZF(a_uResult); \
        fEflTmp |= X86_EFL_CALC_SF(a_uResult, a_cBitsWidth); \
        fEflTmp |= X86_EFL_GET_OF_ ## a_cBitsWidth((a_uDst) & (a_uResult)); \
        *(a_pfEFlags) = fEflTmp; \
    } while (0)

IEM_DECL_IMPL_DEF(void, iemAImpl_neg_u64,(uint64_t *puDst, uint32_t *pfEFlags))
{
    uint64_t uDst    = *puDst;
    uint64_t uResult = (uint64_t)0 - uDst;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_NEG(pfEFlags, uResult, uDst, 64);
}

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(void, iemAImpl_neg_u32,(uint32_t *puDst, uint32_t *pfEFlags))
{
    uint32_t uDst    = *puDst;
    uint32_t uResult = (uint32_t)0 - uDst;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_NEG(pfEFlags, uResult, uDst, 32);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_neg_u16,(uint16_t *puDst, uint32_t *pfEFlags))
{
    uint16_t uDst    = *puDst;
    uint16_t uResult = (uint16_t)0 - uDst;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_NEG(pfEFlags, uResult, uDst, 16);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_neg_u8,(uint8_t *puDst, uint32_t *pfEFlags))
{
    uint8_t uDst    = *puDst;
    uint8_t uResult = (uint8_t)0 - uDst;
    *puDst = uResult;
    IEM_EFL_UPDATE_STATUS_BITS_FOR_NEG(pfEFlags, uResult, uDst, 8);
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */

/*
 * Locked variants.
 */

/** Emit a function for doing a locked unary operand operation. */
# define EMIT_LOCKED_UNARY_OP(a_Mnemonic, a_cBitsWidth) \
    IEM_DECL_IMPL_DEF(void, iemAImpl_ ## a_Mnemonic ## _u ## a_cBitsWidth ## _locked,(uint ## a_cBitsWidth ## _t *puDst, \
                                                                                      uint32_t *pfEFlags)) \
    { \
        uint ## a_cBitsWidth ## _t uOld = ASMAtomicUoReadU ## a_cBitsWidth(puDst); \
        uint ## a_cBitsWidth ## _t uTmp; \
        uint32_t fEflTmp; \
        do \
        { \
            uTmp = uOld; \
            fEflTmp = *pfEFlags; \
            iemAImpl_ ## a_Mnemonic ## _u ## a_cBitsWidth(&uTmp, &fEflTmp); \
        } while (!ASMAtomicCmpXchgExU ## a_cBitsWidth(puDst, uTmp, uOld, &uOld)); \
        *pfEFlags = fEflTmp; \
    }

EMIT_LOCKED_UNARY_OP(inc, 64)
EMIT_LOCKED_UNARY_OP(dec, 64)
EMIT_LOCKED_UNARY_OP(not, 64)
EMIT_LOCKED_UNARY_OP(neg, 64)
# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_LOCKED_UNARY_OP(inc, 32)
EMIT_LOCKED_UNARY_OP(dec, 32)
EMIT_LOCKED_UNARY_OP(not, 32)
EMIT_LOCKED_UNARY_OP(neg, 32)

EMIT_LOCKED_UNARY_OP(inc, 16)
EMIT_LOCKED_UNARY_OP(dec, 16)
EMIT_LOCKED_UNARY_OP(not, 16)
EMIT_LOCKED_UNARY_OP(neg, 16)

EMIT_LOCKED_UNARY_OP(inc, 8)
EMIT_LOCKED_UNARY_OP(dec, 8)
EMIT_LOCKED_UNARY_OP(not, 8)
EMIT_LOCKED_UNARY_OP(neg, 8)
# endif

#endif /* !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY) */


/*********************************************************************************************************************************
*   Shifting and Rotating                                                                                                        *
*********************************************************************************************************************************/

/*
 * ROL
 */
#define EMIT_ROL(a_cBitsWidth, a_uType, a_Suffix, a_fIntelFlags, a_fnHlp) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_rol_u,a_cBitsWidth,a_Suffix),(uint32_t fEFlags, a_uType *puDst, uint8_t cShift)) \
{ \
    cShift &= a_cBitsWidth >= 32 ? a_cBitsWidth - 1 : 31; \
    if (cShift) \
    { \
        if (a_cBitsWidth < 32) \
            cShift &= a_cBitsWidth - 1; \
        a_uType const uDst    = *puDst; \
        a_uType const uResult = a_fnHlp(uDst, cShift); \
        *puDst = uResult; \
        \
        /* Calc EFLAGS.  The OF bit is undefined if cShift > 1, we implement \
           it the same way as for 1 bit shifts. */ \
        AssertCompile(X86_EFL_CF_BIT == 0); \
        fEFlags &= ~(X86_EFL_CF | X86_EFL_OF); \
        uint32_t const fCarry = (uResult & X86_EFL_CF); \
        fEFlags |= fCarry; \
        if (!a_fIntelFlags) /* AMD 3990X: According to the last sub-shift: */ \
            fEFlags |= ((uResult >> (a_cBitsWidth - 1)) ^ fCarry) << X86_EFL_OF_BIT; \
        else                /* Intel 10980XE: According to the first sub-shift: */ \
            fEFlags |= X86_EFL_GET_OF_ ## a_cBitsWidth(uDst ^ (uDst << 1)); \
    } \
    return fEFlags; \
}

#ifndef RT_ARCH_ARM64

# if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_ROL(64, uint64_t, RT_NOTHING, 1, ASMRotateLeftU64)
# endif
EMIT_ROL(64, uint64_t, _intel,     1, ASMRotateLeftU64)
EMIT_ROL(64, uint64_t, _amd,       0, ASMRotateLeftU64)

# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_ROL(32, uint32_t, RT_NOTHING, 1, ASMRotateLeftU32)
# endif
EMIT_ROL(32, uint32_t, _intel,     1, ASMRotateLeftU32)
EMIT_ROL(32, uint32_t, _amd,       0, ASMRotateLeftU32)

DECL_FORCE_INLINE(uint16_t) iemAImpl_rol_u16_hlp(uint16_t uValue, uint8_t cShift)
{
    return (uValue << cShift) | (uValue >> (16 - cShift));
}
# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_ROL(16, uint16_t, RT_NOTHING, 1, iemAImpl_rol_u16_hlp)
# endif
EMIT_ROL(16, uint16_t, _intel,     1, iemAImpl_rol_u16_hlp)
EMIT_ROL(16, uint16_t, _amd,       0, iemAImpl_rol_u16_hlp)

DECL_FORCE_INLINE(uint8_t) iemAImpl_rol_u8_hlp(uint8_t uValue, uint8_t cShift)
{
    return (uValue << cShift) | (uValue >> (8 - cShift));
}
# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_ROL(8,  uint8_t,  RT_NOTHING, 1, iemAImpl_rol_u8_hlp)
# endif
EMIT_ROL(8,  uint8_t,  _intel,     1, iemAImpl_rol_u8_hlp)
EMIT_ROL(8,  uint8_t,  _amd,       0, iemAImpl_rol_u8_hlp)

#endif /* !RT_ARCH_ARM64 */

/*
 * ROR
 */
#define EMIT_ROR(a_cBitsWidth, a_uType, a_Suffix, a_fIntelFlags, a_fnHlp) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_ror_u,a_cBitsWidth,a_Suffix),(uint32_t fEFlags, a_uType *puDst, uint8_t cShift)) \
{ \
    cShift &= a_cBitsWidth >= 32 ? a_cBitsWidth - 1 : 31; \
    if (cShift) \
    { \
        if (a_cBitsWidth < 32) \
            cShift &= a_cBitsWidth - 1; \
        a_uType const uDst    = *puDst; \
        a_uType const uResult = a_fnHlp(uDst, cShift); \
        *puDst = uResult; \
        \
        /* Calc EFLAGS:  */ \
        AssertCompile(X86_EFL_CF_BIT == 0); \
        fEFlags &= ~(X86_EFL_CF | X86_EFL_OF); \
        uint32_t const fCarry = (uResult >> ((a_cBitsWidth) - 1)) & X86_EFL_CF; \
        fEFlags |= fCarry; \
        if (!a_fIntelFlags) /* AMD 3990X: According to the last sub-shift: */ \
            fEFlags |= (((uResult >> ((a_cBitsWidth) - 2)) ^ fCarry) & 1) << X86_EFL_OF_BIT; \
        else                /* Intel 10980XE: According to the first sub-shift: */ \
            fEFlags |= X86_EFL_GET_OF_ ## a_cBitsWidth(uDst ^ (uDst << (a_cBitsWidth - 1))); \
    } \
    return fEFlags; \
}

#ifndef RT_ARCH_ARM64

# if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_ROR(64, uint64_t, RT_NOTHING, 1, ASMRotateRightU64)
# endif
EMIT_ROR(64, uint64_t, _intel,     1, ASMRotateRightU64)
EMIT_ROR(64, uint64_t, _amd,       0, ASMRotateRightU64)

# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_ROR(32, uint32_t, RT_NOTHING, 1, ASMRotateRightU32)
# endif
EMIT_ROR(32, uint32_t, _intel,     1, ASMRotateRightU32)
EMIT_ROR(32, uint32_t, _amd,       0, ASMRotateRightU32)

DECL_FORCE_INLINE(uint16_t) iemAImpl_ror_u16_hlp(uint16_t uValue, uint8_t cShift)
{
    return (uValue >> cShift) | (uValue << (16 - cShift));
}
# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_ROR(16, uint16_t, RT_NOTHING, 1, iemAImpl_ror_u16_hlp)
# endif
EMIT_ROR(16, uint16_t, _intel,     1, iemAImpl_ror_u16_hlp)
EMIT_ROR(16, uint16_t, _amd,       0, iemAImpl_ror_u16_hlp)

DECL_FORCE_INLINE(uint8_t) iemAImpl_ror_u8_hlp(uint8_t uValue, uint8_t cShift)
{
    return (uValue >> cShift) | (uValue << (8 - cShift));
}
# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_ROR(8,  uint8_t,  RT_NOTHING, 1, iemAImpl_ror_u8_hlp)
# endif
EMIT_ROR(8,  uint8_t,  _intel,     1, iemAImpl_ror_u8_hlp)
EMIT_ROR(8,  uint8_t,  _amd,       0, iemAImpl_ror_u8_hlp)

#endif /* !RT_ARCH_ARM64 */

/*
 * RCL
 */
#define EMIT_RCL(a_cBitsWidth, a_uType, a_Suffix, a_fIntelFlags) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_rcl_u,a_cBitsWidth,a_Suffix),(uint32_t fEFlags, a_uType *puDst, uint8_t cShift)) \
{ \
    cShift &= a_cBitsWidth >= 32 ? a_cBitsWidth - 1 : 31; \
    if (a_cBitsWidth < 32 && a_fIntelFlags) \
        cShift %= a_cBitsWidth + 1; \
    if (cShift) \
    { \
        if (a_cBitsWidth < 32 && !a_fIntelFlags) \
            cShift %= a_cBitsWidth + 1; \
        a_uType const uDst    = *puDst; \
        a_uType       uResult = uDst << cShift; \
        if (cShift > 1) \
            uResult |= uDst >> (a_cBitsWidth + 1 - cShift); \
        \
        AssertCompile(X86_EFL_CF_BIT == 0); \
        uint32_t fInCarry = fEFlags & X86_EFL_CF; \
        uResult |= (a_uType)fInCarry << (cShift - 1); \
        \
        *puDst = uResult; \
        \
        /* Calc EFLAGS. */ \
        fEFlags &= ~(X86_EFL_CF | X86_EFL_OF); \
        uint32_t const fOutCarry = a_cBitsWidth >= 32 || a_fIntelFlags || cShift \
                                 ? (uDst >> (a_cBitsWidth - cShift)) & X86_EFL_CF : fInCarry; \
        fEFlags |= fOutCarry; \
        if (!a_fIntelFlags) /* AMD 3990X: According to the last sub-shift: */ \
            fEFlags |= ((uResult >> (a_cBitsWidth - 1)) ^ fOutCarry) << X86_EFL_OF_BIT; \
        else                /* Intel 10980XE: According to the first sub-shift: */ \
            fEFlags |= X86_EFL_GET_OF_ ## a_cBitsWidth(uDst ^ (uDst << 1)); \
    } \
    return fEFlags; \
}

#ifndef RT_ARCH_ARM64

# if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_RCL(64, uint64_t, RT_NOTHING, 1)
# endif
EMIT_RCL(64, uint64_t, _intel,     1)
EMIT_RCL(64, uint64_t, _amd,       0)

# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_RCL(32, uint32_t, RT_NOTHING, 1)
# endif
EMIT_RCL(32, uint32_t, _intel,     1)
EMIT_RCL(32, uint32_t, _amd,       0)

# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_RCL(16, uint16_t, RT_NOTHING, 1)
# endif
EMIT_RCL(16, uint16_t, _intel,     1)
EMIT_RCL(16, uint16_t, _amd,       0)

# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_RCL(8,  uint8_t,  RT_NOTHING, 1)
# endif
EMIT_RCL(8,  uint8_t,  _intel,     1)
EMIT_RCL(8,  uint8_t,  _amd,       0)

#endif /* !RT_ARCH_ARM64 */


/*
 * RCR
 */
#define EMIT_RCR(a_cBitsWidth, a_uType, a_Suffix, a_fIntelFlags) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_rcr_u,a_cBitsWidth,a_Suffix),(uint32_t fEFlags, a_uType *puDst, uint8_t cShift)) \
{ \
    cShift &= a_cBitsWidth >= 32 ? a_cBitsWidth - 1 : 31; \
    if (a_cBitsWidth < 32 && a_fIntelFlags) \
        cShift %= a_cBitsWidth + 1; \
    if (cShift) \
    { \
        if (a_cBitsWidth < 32 && !a_fIntelFlags) \
            cShift %= a_cBitsWidth + 1; \
        a_uType const uDst    = *puDst; \
        a_uType       uResult = uDst >> cShift; \
        if (cShift > 1) \
            uResult |= uDst << (a_cBitsWidth + 1 - cShift); \
        \
        AssertCompile(X86_EFL_CF_BIT == 0); \
        uint32_t fInCarry = fEFlags & X86_EFL_CF; \
        uResult |= (a_uType)fInCarry << (a_cBitsWidth - cShift); \
        *puDst = uResult; \
        \
        /* Calc EFLAGS.  The OF bit is undefined if cShift > 1, we implement \
           it the same way as for 1 bit shifts. */ \
        fEFlags &= ~(X86_EFL_CF | X86_EFL_OF); \
        uint32_t const fOutCarry = a_cBitsWidth >= 32 || a_fIntelFlags || cShift \
                                 ? (uDst >> (cShift - 1)) & X86_EFL_CF : fInCarry; \
        fEFlags |= fOutCarry; \
        if (!a_fIntelFlags) /* AMD 3990X: XOR two most signficant bits of the result: */ \
            fEFlags |= X86_EFL_GET_OF_ ## a_cBitsWidth(uResult ^ (uResult << 1));  \
        else                /* Intel 10980XE: same as AMD, but only for the first sub-shift: */ \
            fEFlags |= (fInCarry ^ (uint32_t)(uDst >> (a_cBitsWidth - 1))) << X86_EFL_OF_BIT; \
    } \
    return fEFlags; \
}

#ifndef RT_ARCH_ARM64

#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_RCR(64, uint64_t, RT_NOTHING, 1)
#endif
EMIT_RCR(64, uint64_t, _intel,     1)
EMIT_RCR(64, uint64_t, _amd,       0)

# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_RCR(32, uint32_t, RT_NOTHING, 1)
# endif
EMIT_RCR(32, uint32_t, _intel,     1)
EMIT_RCR(32, uint32_t, _amd,       0)

# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_RCR(16, uint16_t, RT_NOTHING, 1)
# endif
EMIT_RCR(16, uint16_t, _intel,     1)
EMIT_RCR(16, uint16_t, _amd,       0)

# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_RCR(8,  uint8_t,  RT_NOTHING, 1)
# endif
EMIT_RCR(8,  uint8_t,  _intel,     1)
EMIT_RCR(8,  uint8_t,  _amd,       0)

#endif /* !RT_ARCH_ARM64 */


/*
 * SHL
 */
#define EMIT_SHL(a_cBitsWidth, a_uType, a_Suffix, a_fIntelFlags) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_shl_u,a_cBitsWidth,a_Suffix),(uint32_t fEFlags, a_uType *puDst, uint8_t cShift)) \
{ \
    cShift &= a_cBitsWidth >= 32 ? a_cBitsWidth - 1 : 31; \
    if (cShift) \
    { \
        a_uType const uDst  = *puDst; \
        a_uType       uResult = uDst << cShift; \
        *puDst = uResult; \
        \
        /* Calc EFLAGS. */ \
        AssertCompile(X86_EFL_CF_BIT == 0); \
        fEFlags &= ~X86_EFL_STATUS_BITS; \
        uint32_t const fCarry = (uDst >> (a_cBitsWidth - cShift)) & X86_EFL_CF; \
        fEFlags |= fCarry; \
        if (!a_fIntelFlags) \
            fEFlags |= ((uResult >> (a_cBitsWidth - 1)) ^ fCarry) << X86_EFL_OF_BIT; /* AMD 3990X: Last shift result. */ \
        else \
            fEFlags |= X86_EFL_GET_OF_ ## a_cBitsWidth(uDst ^ (uDst << 1)); /* Intel 10980XE: First shift result. */ \
        fEFlags |= X86_EFL_CALC_SF(uResult, a_cBitsWidth); \
        fEFlags |= X86_EFL_CALC_ZF(uResult); \
        fEFlags |= IEM_EFL_CALC_PARITY(uResult); \
        if (!a_fIntelFlags) \
            fEFlags |= X86_EFL_AF; /* AMD 3990x sets it unconditionally, Intel 10980XE does the oposite */ \
    } \
    return fEFlags; \
}

#if !defined(RT_ARCH_ARM64)

# if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHL(64, uint64_t, RT_NOTHING, 1)
# endif
EMIT_SHL(64, uint64_t, _intel,     1)
EMIT_SHL(64, uint64_t, _amd,       0)

# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHL(32, uint32_t, RT_NOTHING, 1)
# endif
EMIT_SHL(32, uint32_t, _intel,     1)
EMIT_SHL(32, uint32_t, _amd,       0)

# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHL(16, uint16_t, RT_NOTHING, 1)
# endif
EMIT_SHL(16, uint16_t, _intel,     1)
EMIT_SHL(16, uint16_t, _amd,       0)

# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHL(8,  uint8_t,  RT_NOTHING, 1)
# endif
EMIT_SHL(8,  uint8_t,  _intel,     1)
EMIT_SHL(8,  uint8_t,  _amd,       0)

#endif /* !RT_ARCH_ARM64 */


/*
 * SHR
 */
#define EMIT_SHR(a_cBitsWidth, a_uType, a_Suffix, a_fIntelFlags) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_shr_u,a_cBitsWidth,a_Suffix),(uint32_t fEFlags, a_uType *puDst, uint8_t cShift)) \
{ \
    cShift &= a_cBitsWidth >= 32 ? a_cBitsWidth - 1 : 31; \
    if (cShift) \
    { \
        a_uType const uDst    = *puDst; \
        a_uType       uResult = uDst >> cShift; \
        *puDst = uResult; \
        \
        /* Calc EFLAGS. */ \
        AssertCompile(X86_EFL_CF_BIT == 0); \
        fEFlags &= ~X86_EFL_STATUS_BITS; \
        fEFlags |= (uDst >> (cShift - 1)) & X86_EFL_CF; \
        if (a_fIntelFlags || cShift == 1) /* AMD 3990x does what intel documents; Intel 10980XE does this for all shift counts. */ \
            fEFlags |= (uDst >> (a_cBitsWidth - 1)) << X86_EFL_OF_BIT; \
        fEFlags |= X86_EFL_CALC_SF(uResult, a_cBitsWidth); \
        fEFlags |= X86_EFL_CALC_ZF(uResult); \
        fEFlags |= IEM_EFL_CALC_PARITY(uResult); \
        if (!a_fIntelFlags) \
            fEFlags |= X86_EFL_AF; /* AMD 3990x sets it unconditionally, Intel 10980XE does the oposite */ \
    } \
    return fEFlags; \
}

#if !defined(RT_ARCH_ARM64)

# if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHR(64, uint64_t, RT_NOTHING, 1)
# endif
EMIT_SHR(64, uint64_t, _intel,     1)
EMIT_SHR(64, uint64_t, _amd,       0)

# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHR(32, uint32_t, RT_NOTHING, 1)
# endif
EMIT_SHR(32, uint32_t, _intel,     1)
EMIT_SHR(32, uint32_t, _amd,       0)

# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHR(16, uint16_t, RT_NOTHING, 1)
# endif
EMIT_SHR(16, uint16_t, _intel,     1)
EMIT_SHR(16, uint16_t, _amd,       0)

# if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHR(8,  uint8_t,  RT_NOTHING, 1)
# endif
EMIT_SHR(8,  uint8_t,  _intel,     1)
EMIT_SHR(8,  uint8_t,  _amd,       0)

#endif /* !RT_ARCH_ARM64 */


/*
 * SAR
 */
#define EMIT_SAR(a_cBitsWidth, a_uType, a_iType, a_Suffix, a_fIntelFlags) \
IEM_DECL_IMPL_DEF(uint32_t, RT_CONCAT3(iemAImpl_sar_u,a_cBitsWidth,a_Suffix),(uint32_t fEFlags, a_uType *puDst, uint8_t cShift)) \
{ \
    cShift &= a_cBitsWidth >= 32 ? a_cBitsWidth - 1 : 31; \
    if (cShift) \
    { \
        a_iType const iDst    = (a_iType)*puDst; \
        a_uType       uResult = iDst >> cShift; \
        *puDst = uResult; \
        \
        /* Calc EFLAGS. \
           Note! The OF flag is always zero because the result never differs from the input. */ \
        AssertCompile(X86_EFL_CF_BIT == 0); \
        fEFlags &= ~X86_EFL_STATUS_BITS; \
        fEFlags |= (iDst >> (cShift - 1)) & X86_EFL_CF; \
        fEFlags |= X86_EFL_CALC_SF(uResult, a_cBitsWidth); \
        fEFlags |= X86_EFL_CALC_ZF(uResult); \
        fEFlags |= IEM_EFL_CALC_PARITY(uResult); \
        if (!a_fIntelFlags) \
            fEFlags |= X86_EFL_AF; /* AMD 3990x sets it unconditionally, Intel 10980XE does the oposite */ \
    } \
    return fEFlags; \
}

#if !defined(RT_ARCH_ARM64)

# if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SAR(64, uint64_t, int64_t, RT_NOTHING, 1)
# endif
EMIT_SAR(64, uint64_t, int64_t, _intel,     1)
EMIT_SAR(64, uint64_t, int64_t, _amd,       0)

# if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SAR(32, uint32_t, int32_t, RT_NOTHING, 1)
# endif
EMIT_SAR(32, uint32_t, int32_t, _intel,     1)
EMIT_SAR(32, uint32_t, int32_t, _amd,       0)

# if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SAR(16, uint16_t, int16_t, RT_NOTHING, 1)
# endif
EMIT_SAR(16, uint16_t, int16_t, _intel,     1)
EMIT_SAR(16, uint16_t, int16_t, _amd,       0)

# if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SAR(8,  uint8_t,  int8_t,  RT_NOTHING, 1)
# endif
EMIT_SAR(8,  uint8_t,  int8_t,  _intel,     1)
EMIT_SAR(8,  uint8_t,  int8_t,  _amd,       0)

#endif /* !RT_ARCH_ARM64 */


/*
 * SHLD
 *
 *  - CF is the last bit shifted out of puDst.
 *  - AF is always cleared by Intel 10980XE.
 *  - AF is always set by AMD 3990X.
 *  - OF is set according to the first shift on Intel 10980XE, it seems.
 *  - OF is set according to the last sub-shift on AMD 3990X.
 *  - ZF, SF and PF are calculated according to the result by both vendors.
 *
 * For 16-bit shifts the count mask isn't 15, but 31, and the CPU will
 * pick either the source register or the destination register for input bits
 * when going beyond 16.  According to https://www.sandpile.org/x86/flags.htm
 * intel has changed behaviour here several times.  We implement what current
 * skylake based does for now, we can extend this later as needed.
 */
#define EMIT_SHLD(a_cBitsWidth, a_uType, a_Suffix, a_fIntelFlags) \
IEM_DECL_IMPL_DEF(void, RT_CONCAT3(iemAImpl_shld_u,a_cBitsWidth,a_Suffix),(a_uType *puDst, a_uType uSrc, uint8_t cShift, \
                                                                           uint32_t *pfEFlags)) \
{ \
    cShift &= a_cBitsWidth - 1; \
    if (cShift) \
    { \
        a_uType const uDst    = *puDst; \
        a_uType       uResult = uDst << cShift; \
        uResult |= uSrc >> (a_cBitsWidth - cShift); \
        *puDst = uResult; \
        \
        /* CALC EFLAGS: */ \
        uint32_t fEfl = *pfEFlags & ~X86_EFL_STATUS_BITS; \
        if (a_fIntelFlags) \
            /* Intel 6700K & 10980XE: Set according to the first shift. AF always cleared. */ \
            fEfl |= X86_EFL_GET_OF_ ## a_cBitsWidth(uDst ^ (uDst << 1));  \
        else \
        {   /* AMD 3990X: Set according to last shift. AF always set. */ \
            fEfl |= X86_EFL_GET_OF_ ## a_cBitsWidth((uDst << (cShift - 1)) ^ uResult); \
            fEfl |= X86_EFL_AF; \
        } \
        AssertCompile(X86_EFL_CF_BIT == 0); \
        fEfl |= (uDst >> (a_cBitsWidth - cShift)) & X86_EFL_CF; /* CF = last bit shifted out */ \
        fEfl |= IEM_EFL_CALC_PARITY(uResult); \
        fEfl |= X86_EFL_CALC_SF(uResult, a_cBitsWidth); \
        fEfl |= X86_EFL_CALC_ZF(uResult); \
        *pfEFlags = fEfl; \
    } \
}

#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHLD(64, uint64_t, RT_NOTHING, 1)
#endif
EMIT_SHLD(64, uint64_t, _intel,     1)
EMIT_SHLD(64, uint64_t, _amd,       0)

#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHLD(32, uint32_t, RT_NOTHING, 1)
#endif
EMIT_SHLD(32, uint32_t, _intel,     1)
EMIT_SHLD(32, uint32_t, _amd,       0)

#define EMIT_SHLD_16(a_Suffix, a_fIntelFlags) \
IEM_DECL_IMPL_DEF(void, RT_CONCAT(iemAImpl_shld_u16,a_Suffix),(uint16_t *puDst, uint16_t uSrc, uint8_t cShift, uint32_t *pfEFlags)) \
{ \
    cShift &= 31; \
    if (cShift) \
    { \
        uint16_t const uDst    = *puDst; \
        uint64_t const uTmp    = a_fIntelFlags \
                               ? ((uint64_t)uDst << 32) | ((uint32_t)uSrc << 16) | uDst \
                               : ((uint64_t)uDst << 32) | ((uint32_t)uSrc << 16) | uSrc; \
        uint16_t const uResult = (uint16_t)((uTmp << cShift) >> 32); \
        *puDst = uResult; \
        \
        /* CALC EFLAGS: */ \
        uint32_t fEfl = *pfEFlags & ~X86_EFL_STATUS_BITS; \
        AssertCompile(X86_EFL_CF_BIT == 0); \
        if (a_fIntelFlags) \
        { \
            fEfl |= (uTmp >> (48 - cShift)) & X86_EFL_CF; /* CF = last bit shifted out of the combined operand */ \
            /* Intel 6700K & 10980XE: OF is et according to the first shift. AF always cleared. */ \
            fEfl |= X86_EFL_GET_OF_16(uDst ^ (uDst << 1));  \
        } \
        else \
        { \
            /* AMD 3990X: OF is set according to last shift, with some weirdness. AF always set. CF = last bit shifted out of uDst. */ \
            if (cShift < 16) \
            { \
                fEfl |= (uDst >> (16 - cShift)) & X86_EFL_CF;  \
                fEfl |= X86_EFL_GET_OF_16((uDst << (cShift - 1)) ^ uResult); \
            } \
            else \
            { \
                if (cShift == 16) \
                    fEfl |= uDst & X86_EFL_CF; \
                fEfl |= X86_EFL_GET_OF_16((uDst << (cShift - 1)) ^ 0); \
            } \
            fEfl |= X86_EFL_AF; \
        } \
        fEfl |= IEM_EFL_CALC_PARITY(uResult); \
        fEfl |= X86_EFL_CALC_SF(uResult, 16); \
        fEfl |= X86_EFL_CALC_ZF(uResult); \
        *pfEFlags = fEfl; \
    } \
}

#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHLD_16(RT_NOTHING, 1)
#endif
EMIT_SHLD_16(_intel,     1)
EMIT_SHLD_16(_amd,       0)


/*
 * SHRD
 *
 * EFLAGS behaviour seems to be the same as with SHLD:
 *  - CF is the last bit shifted out of puDst.
 *  - AF is always cleared by Intel 10980XE.
 *  - AF is always set by AMD 3990X.
 *  - OF is set according to the first shift on Intel 10980XE, it seems.
 *  - OF is set according to the last sub-shift on AMD 3990X.
 *  - ZF, SF and PF are calculated according to the result by both vendors.
 *
 * For 16-bit shifts the count mask isn't 15, but 31, and the CPU will
 * pick either the source register or the destination register for input bits
 * when going beyond 16.  According to https://www.sandpile.org/x86/flags.htm
 * intel has changed behaviour here several times.  We implement what current
 * skylake based does for now, we can extend this later as needed.
 */
#define EMIT_SHRD(a_cBitsWidth, a_uType, a_Suffix, a_fIntelFlags) \
IEM_DECL_IMPL_DEF(void, RT_CONCAT3(iemAImpl_shrd_u,a_cBitsWidth,a_Suffix),(a_uType *puDst, a_uType uSrc, uint8_t cShift, uint32_t *pfEFlags)) \
{ \
    cShift &= a_cBitsWidth - 1; \
    if (cShift) \
    { \
        a_uType const uDst    = *puDst; \
        a_uType       uResult = uDst >> cShift; \
        uResult |= uSrc << (a_cBitsWidth - cShift); \
        *puDst = uResult; \
        \
        uint32_t fEfl = *pfEFlags & ~X86_EFL_STATUS_BITS; \
        AssertCompile(X86_EFL_CF_BIT == 0); \
        fEfl |= (uDst >> (cShift - 1)) & X86_EFL_CF; \
        if (a_fIntelFlags) \
            /* Intel 6700K & 10980XE: Set according to the first shift. AF always cleared. */ \
            fEfl |= X86_EFL_GET_OF_ ## a_cBitsWidth(uDst ^ (uSrc << (a_cBitsWidth - 1))); \
        else \
        {   /* AMD 3990X: Set according to last shift. AF always set. */ \
            if (cShift > 1) /* Set according to last shift. */ \
                fEfl |= X86_EFL_GET_OF_ ## a_cBitsWidth((uSrc << (a_cBitsWidth - cShift + 1)) ^ uResult); \
            else \
                fEfl |= X86_EFL_GET_OF_ ## a_cBitsWidth(uDst ^ uResult); \
            fEfl |= X86_EFL_AF; \
        } \
        fEfl |= X86_EFL_CALC_SF(uResult, a_cBitsWidth); \
        fEfl |= X86_EFL_CALC_ZF(uResult); \
        fEfl |= IEM_EFL_CALC_PARITY(uResult); \
        *pfEFlags = fEfl; \
    } \
}

#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHRD(64, uint64_t, RT_NOTHING, 1)
#endif
EMIT_SHRD(64, uint64_t, _intel,     1)
EMIT_SHRD(64, uint64_t, _amd,       0)

#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHRD(32, uint32_t, RT_NOTHING, 1)
#endif
EMIT_SHRD(32, uint32_t, _intel,     1)
EMIT_SHRD(32, uint32_t, _amd,       0)

#define EMIT_SHRD_16(a_Suffix, a_fIntelFlags) \
IEM_DECL_IMPL_DEF(void, RT_CONCAT(iemAImpl_shrd_u16,a_Suffix),(uint16_t *puDst, uint16_t uSrc, uint8_t cShift, uint32_t *pfEFlags)) \
{ \
    cShift &= 31; \
    if (cShift) \
    { \
        uint16_t const uDst    = *puDst; \
        uint64_t const uTmp    = a_fIntelFlags \
                               ? uDst | ((uint32_t)uSrc << 16) | ((uint64_t)uDst << 32) \
                               : uDst | ((uint32_t)uSrc << 16) | ((uint64_t)uSrc << 32); \
        uint16_t const uResult = (uint16_t)(uTmp >> cShift); \
        *puDst = uResult; \
        \
        uint32_t fEfl = *pfEFlags & ~X86_EFL_STATUS_BITS; \
        AssertCompile(X86_EFL_CF_BIT == 0); \
        if (a_fIntelFlags) \
        { \
            /* Intel 10980XE: The CF is the last shifted out of the combined uTmp operand. */ \
            fEfl |= (uTmp >> (cShift - 1)) & X86_EFL_CF; \
            /* Intel 6700K & 10980XE: Set according to the first shift. AF always cleared. */ \
            fEfl |= X86_EFL_GET_OF_16(uDst ^ (uSrc << 15)); \
        } \
        else \
        { \
            /* AMD 3990X: CF flag seems to be last bit shifted out of uDst, not the combined uSrc:uSrc:uDst operand. */ \
            fEfl |= (uDst >> (cShift - 1)) & X86_EFL_CF; \
            /* AMD 3990X: Set according to last shift. AF always set. */ \
            if (cShift > 1) /* Set according to last shift. */ \
                fEfl |= X86_EFL_GET_OF_16((uint16_t)(uTmp >> (cShift - 1)) ^ uResult); \
            else \
                fEfl |= X86_EFL_GET_OF_16(uDst ^ uResult); \
            fEfl |= X86_EFL_AF; \
        } \
        fEfl |= X86_EFL_CALC_SF(uResult, 16); \
        fEfl |= X86_EFL_CALC_ZF(uResult); \
        fEfl |= IEM_EFL_CALC_PARITY(uResult); \
        *pfEFlags = fEfl; \
    } \
}

#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHRD_16(RT_NOTHING, 1)
#endif
EMIT_SHRD_16(_intel,     1)
EMIT_SHRD_16(_amd,       0)


/*
 * RORX (BMI2)
 */
#define EMIT_RORX(a_cBitsWidth, a_uType, a_fnHlp) \
IEM_DECL_IMPL_DEF(void, RT_CONCAT(iemAImpl_rorx_u,a_cBitsWidth),(a_uType *puDst, a_uType uSrc, a_uType cShift)) \
{ \
    *puDst = a_fnHlp(uSrc, cShift & (a_cBitsWidth - 1)); \
}

#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_RORX(64, uint64_t, ASMRotateRightU64)
#endif
#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_RORX(32, uint32_t, ASMRotateRightU32)
#endif


/*
 * SHLX (BMI2)
 */
#define EMIT_SHLX(a_cBitsWidth, a_uType, a_Suffix) \
IEM_DECL_IMPL_DEF(void, RT_CONCAT3(iemAImpl_shlx_u,a_cBitsWidth,a_Suffix),(a_uType *puDst, a_uType uSrc, a_uType cShift)) \
{ \
    cShift &= a_cBitsWidth - 1; \
    *puDst = uSrc << cShift; \
}

#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHLX(64, uint64_t, RT_NOTHING)
EMIT_SHLX(64, uint64_t, _fallback)
#endif
#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHLX(32, uint32_t, RT_NOTHING)
EMIT_SHLX(32, uint32_t, _fallback)
#endif


/*
 * SHRX (BMI2)
 */
#define EMIT_SHRX(a_cBitsWidth, a_uType, a_Suffix) \
IEM_DECL_IMPL_DEF(void, RT_CONCAT3(iemAImpl_shrx_u,a_cBitsWidth,a_Suffix),(a_uType *puDst, a_uType uSrc, a_uType cShift)) \
{ \
    cShift &= a_cBitsWidth - 1; \
    *puDst = uSrc >> cShift; \
}

#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHRX(64, uint64_t, RT_NOTHING)
EMIT_SHRX(64, uint64_t, _fallback)
#endif
#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SHRX(32, uint32_t, RT_NOTHING)
EMIT_SHRX(32, uint32_t, _fallback)
#endif


/*
 * SARX (BMI2)
 */
#define EMIT_SARX(a_cBitsWidth, a_uType, a_iType, a_Suffix) \
IEM_DECL_IMPL_DEF(void, RT_CONCAT3(iemAImpl_sarx_u,a_cBitsWidth,a_Suffix),(a_uType *puDst, a_uType uSrc, a_uType cShift)) \
{ \
    cShift &= a_cBitsWidth - 1; \
    *puDst = (a_iType)uSrc >> cShift; \
}

#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SARX(64, uint64_t, int64_t, RT_NOTHING)
EMIT_SARX(64, uint64_t, int64_t, _fallback)
#endif
#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_SARX(32, uint32_t, int32_t, RT_NOTHING)
EMIT_SARX(32, uint32_t, int32_t, _fallback)
#endif


/*
 * PDEP (BMI2)
 */
#define EMIT_PDEP(a_cBitsWidth, a_uType, a_Suffix) \
IEM_DECL_IMPL_DEF(void, RT_CONCAT3(iemAImpl_pdep_u,a_cBitsWidth,a_Suffix),(a_uType *puDst, a_uType uSrc, a_uType fMask)) \
{ \
    a_uType uResult = 0; \
    for (unsigned iMaskBit = 0, iBit = 0; iMaskBit < a_cBitsWidth; iMaskBit++) \
        if (fMask & ((a_uType)1 << iMaskBit)) \
        { \
            uResult |= ((uSrc >> iBit) & 1) << iMaskBit; \
            iBit++; \
        } \
    *puDst = uResult; \
}

#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_PDEP(64, uint64_t, RT_NOTHING)
#endif
EMIT_PDEP(64, uint64_t, _fallback)
#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_PDEP(32, uint32_t, RT_NOTHING)
#endif
EMIT_PDEP(32, uint32_t, _fallback)

/*
 * PEXT (BMI2)
 */
#define EMIT_PEXT(a_cBitsWidth, a_uType, a_Suffix) \
IEM_DECL_IMPL_DEF(void, RT_CONCAT3(iemAImpl_pext_u,a_cBitsWidth,a_Suffix),(a_uType *puDst, a_uType uSrc, a_uType fMask)) \
{ \
    a_uType uResult = 0; \
    for (unsigned iMaskBit = 0, iBit = 0; iMaskBit < a_cBitsWidth; iMaskBit++) \
        if (fMask & ((a_uType)1 << iMaskBit)) \
        { \
            uResult |= ((uSrc >> iMaskBit) & 1) << iBit; \
            iBit++; \
        } \
    *puDst = uResult; \
}

#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_PEXT(64, uint64_t, RT_NOTHING)
#endif
EMIT_PEXT(64, uint64_t, _fallback)
#if (!defined(RT_ARCH_X86) && !defined(RT_ARCH_AMD64)) || defined(IEM_WITHOUT_ASSEMBLY)
EMIT_PEXT(32, uint32_t, RT_NOTHING)
#endif
EMIT_PEXT(32, uint32_t, _fallback)


#if !defined(RT_ARCH_AMD64) || defined(IEM_WITHOUT_ASSEMBLY)

# if !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY)
/*
 * BSWAP
 */

IEM_DECL_IMPL_DEF(void, iemAImpl_bswap_u64,(uint64_t *puDst))
{
    *puDst = ASMByteSwapU64(*puDst);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_bswap_u32,(uint32_t *puDst))
{
    *puDst = ASMByteSwapU32(*puDst);
}


/* Note! undocument, so 32-bit arg */
IEM_DECL_IMPL_DEF(void, iemAImpl_bswap_u16,(uint32_t *puDst))
{
#if 0
    *(uint16_t *)puDst = ASMByteSwapU16(*(uint16_t *)puDst);
#else
    /* This is the behaviour AMD 3990x (64-bit mode): */
    *(uint16_t *)puDst = 0;
#endif
}

# endif /* !defined(RT_ARCH_X86) || defined(IEM_WITHOUT_ASSEMBLY) */



# if defined(IEM_WITHOUT_ASSEMBLY)

/*
 * LFENCE, SFENCE & MFENCE.
 */

IEM_DECL_IMPL_DEF(void, iemAImpl_lfence,(void))
{
    ASMReadFence();
}


IEM_DECL_IMPL_DEF(void, iemAImpl_sfence,(void))
{
    ASMWriteFence();
}


IEM_DECL_IMPL_DEF(void, iemAImpl_mfence,(void))
{
    ASMMemoryFence();
}


#  ifndef RT_ARCH_ARM64
IEM_DECL_IMPL_DEF(void, iemAImpl_alt_mem_fence,(void))
{
    ASMMemoryFence();
}
#  endif

# endif

#endif /* !RT_ARCH_AMD64 || IEM_WITHOUT_ASSEMBLY */


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_arpl,(uint32_t fEFlags, uint16_t *pu16Dst, uint16_t u16Src))
{
    uint16_t u16Dst = *pu16Dst;
    if ((u16Dst & X86_SEL_RPL) < (u16Src & X86_SEL_RPL))
    {
        u16Dst   &= X86_SEL_MASK_OFF_RPL;
        u16Dst   |= u16Src & X86_SEL_RPL;
        *pu16Dst = u16Dst;

        fEFlags  |= X86_EFL_ZF;
    }
    else
        fEFlags  &= ~X86_EFL_ZF;
    return fEFlags;
}


#if defined(IEM_WITHOUT_ASSEMBLY)

/*********************************************************************************************************************************
*   x87 FPU Loads                                                                                                                *
*********************************************************************************************************************************/

IEM_DECL_IMPL_DEF(void, iemAImpl_fld_r80_from_r32,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT32U pr32Val))
{
    pFpuRes->FSW = (7 << X86_FSW_TOP_SHIFT) | (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)); /* see iemAImpl_fld1 */
    if (RTFLOAT32U_IS_NORMAL(pr32Val))
    {
        pFpuRes->r80Result.sj64.fSign     = pr32Val->s.fSign;
        pFpuRes->r80Result.sj64.fInteger  = 1;
        pFpuRes->r80Result.sj64.uFraction = (uint64_t)pr32Val->s.uFraction
                                         << (RTFLOAT80U_FRACTION_BITS - RTFLOAT32U_FRACTION_BITS);
        pFpuRes->r80Result.sj64.uExponent = pr32Val->s.uExponent - RTFLOAT32U_EXP_BIAS + RTFLOAT80U_EXP_BIAS;
        Assert(RTFLOAT80U_IS_NORMAL(&pFpuRes->r80Result));
    }
    else if (RTFLOAT32U_IS_ZERO(pr32Val))
    {
        pFpuRes->r80Result.s.fSign     = pr32Val->s.fSign;
        pFpuRes->r80Result.s.uExponent = 0;
        pFpuRes->r80Result.s.uMantissa = 0;
        Assert(RTFLOAT80U_IS_ZERO(&pFpuRes->r80Result));
    }
    else if (RTFLOAT32U_IS_SUBNORMAL(pr32Val))
    {
        /* Subnormal values gets normalized. */
        pFpuRes->r80Result.sj64.fSign     = pr32Val->s.fSign;
        pFpuRes->r80Result.sj64.fInteger  = 1;
        unsigned const cExtraShift = RTFLOAT32U_FRACTION_BITS - ASMBitLastSetU32(pr32Val->s.uFraction);
        pFpuRes->r80Result.sj64.uFraction = (uint64_t)pr32Val->s.uFraction
                                         << (RTFLOAT80U_FRACTION_BITS - RTFLOAT32U_FRACTION_BITS + cExtraShift + 1);
        pFpuRes->r80Result.sj64.uExponent = pr32Val->s.uExponent - RTFLOAT32U_EXP_BIAS + RTFLOAT80U_EXP_BIAS - cExtraShift;
        pFpuRes->FSW |= X86_FSW_DE;
        if (!(pFpuState->FCW & X86_FCW_DM))
            pFpuRes->FSW |= X86_FSW_ES | X86_FSW_B; /* The value is still pushed. */
    }
    else if (RTFLOAT32U_IS_INF(pr32Val))
    {
        pFpuRes->r80Result.s.fSign     = pr32Val->s.fSign;
        pFpuRes->r80Result.s.uExponent = RTFLOAT80U_EXP_MAX;
        pFpuRes->r80Result.s.uMantissa = RT_BIT_64(63);
        Assert(RTFLOAT80U_IS_INF(&pFpuRes->r80Result));
    }
    else
    {
        /* Signalling and quiet NaNs, both turn into quiet ones when loaded (weird). */
        Assert(RTFLOAT32U_IS_NAN(pr32Val));
        pFpuRes->r80Result.sj64.fSign     = pr32Val->s.fSign;
        pFpuRes->r80Result.sj64.uExponent = RTFLOAT80U_EXP_MAX;
        pFpuRes->r80Result.sj64.fInteger  = 1;
        pFpuRes->r80Result.sj64.uFraction = (uint64_t)pr32Val->s.uFraction
                                         << (RTFLOAT80U_FRACTION_BITS - RTFLOAT32U_FRACTION_BITS);
        if (RTFLOAT32U_IS_SIGNALLING_NAN(pr32Val))
        {
            pFpuRes->r80Result.sj64.uFraction |= RT_BIT_64(62); /* make quiet */
            Assert(RTFLOAT80U_IS_QUIET_NAN(&pFpuRes->r80Result));
            pFpuRes->FSW |= X86_FSW_IE;

            if (!(pFpuState->FCW & X86_FCW_IM))
            {
                /* The value is not pushed. */
                pFpuRes->FSW &= ~X86_FSW_TOP_MASK;
                pFpuRes->FSW |= X86_FSW_ES | X86_FSW_B;
                pFpuRes->r80Result.au64[0] = 0;
                pFpuRes->r80Result.au16[4] = 0;
            }
        }
        else
            Assert(RTFLOAT80U_IS_QUIET_NAN(&pFpuRes->r80Result));
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fld_r80_from_r64,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT64U pr64Val))
{
    pFpuRes->FSW = (7 << X86_FSW_TOP_SHIFT) | (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)); /* see iemAImpl_fld1 */
    if (RTFLOAT64U_IS_NORMAL(pr64Val))
    {
        pFpuRes->r80Result.sj64.fSign     = pr64Val->s.fSign;
        pFpuRes->r80Result.sj64.fInteger  = 1;
        pFpuRes->r80Result.sj64.uFraction = pr64Val->s64.uFraction << (RTFLOAT80U_FRACTION_BITS - RTFLOAT64U_FRACTION_BITS);
        pFpuRes->r80Result.sj64.uExponent = pr64Val->s.uExponent - RTFLOAT64U_EXP_BIAS + RTFLOAT80U_EXP_BIAS;
        Assert(RTFLOAT80U_IS_NORMAL(&pFpuRes->r80Result));
    }
    else if (RTFLOAT64U_IS_ZERO(pr64Val))
    {
        pFpuRes->r80Result.s.fSign     = pr64Val->s.fSign;
        pFpuRes->r80Result.s.uExponent = 0;
        pFpuRes->r80Result.s.uMantissa = 0;
        Assert(RTFLOAT80U_IS_ZERO(&pFpuRes->r80Result));
    }
    else if (RTFLOAT64U_IS_SUBNORMAL(pr64Val))
    {
        /* Subnormal values gets normalized. */
        pFpuRes->r80Result.sj64.fSign     = pr64Val->s.fSign;
        pFpuRes->r80Result.sj64.fInteger  = 1;
        unsigned const cExtraShift = RTFLOAT64U_FRACTION_BITS - ASMBitLastSetU64(pr64Val->s64.uFraction);
        pFpuRes->r80Result.sj64.uFraction = pr64Val->s64.uFraction
                                         << (RTFLOAT80U_FRACTION_BITS - RTFLOAT64U_FRACTION_BITS + cExtraShift + 1);
        pFpuRes->r80Result.sj64.uExponent = pr64Val->s.uExponent - RTFLOAT64U_EXP_BIAS + RTFLOAT80U_EXP_BIAS - cExtraShift;
        pFpuRes->FSW |= X86_FSW_DE;
        if (!(pFpuState->FCW & X86_FCW_DM))
            pFpuRes->FSW |= X86_FSW_ES | X86_FSW_B; /* The value is still pushed. */
    }
    else if (RTFLOAT64U_IS_INF(pr64Val))
    {
        pFpuRes->r80Result.s.fSign     = pr64Val->s.fSign;
        pFpuRes->r80Result.s.uExponent = RTFLOAT80U_EXP_MAX;
        pFpuRes->r80Result.s.uMantissa = RT_BIT_64(63);
        Assert(RTFLOAT80U_IS_INF(&pFpuRes->r80Result));
    }
    else
    {
        /* Signalling and quiet NaNs, both turn into quiet ones when loaded (weird). */
        Assert(RTFLOAT64U_IS_NAN(pr64Val));
        pFpuRes->r80Result.sj64.fSign     = pr64Val->s.fSign;
        pFpuRes->r80Result.sj64.uExponent = RTFLOAT80U_EXP_MAX;
        pFpuRes->r80Result.sj64.fInteger  = 1;
        pFpuRes->r80Result.sj64.uFraction = pr64Val->s64.uFraction << (RTFLOAT80U_FRACTION_BITS - RTFLOAT64U_FRACTION_BITS);
        if (RTFLOAT64U_IS_SIGNALLING_NAN(pr64Val))
        {
            pFpuRes->r80Result.sj64.uFraction |= RT_BIT_64(62); /* make quiet */
            Assert(RTFLOAT80U_IS_QUIET_NAN(&pFpuRes->r80Result));
            pFpuRes->FSW |= X86_FSW_IE;

            if (!(pFpuState->FCW & X86_FCW_IM))
            {
                /* The value is not pushed. */
                pFpuRes->FSW &= ~X86_FSW_TOP_MASK;
                pFpuRes->FSW |= X86_FSW_ES | X86_FSW_B;
                pFpuRes->r80Result.au64[0] = 0;
                pFpuRes->r80Result.au16[4] = 0;
            }
        }
        else
            Assert(RTFLOAT80U_IS_QUIET_NAN(&pFpuRes->r80Result));
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fld_r80_from_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val))
{
    pFpuRes->r80Result.au64[0] = pr80Val->au64[0];
    pFpuRes->r80Result.au16[4] = pr80Val->au16[4];
    /* Raises no exceptions. */
    pFpuRes->FSW = (7 << X86_FSW_TOP_SHIFT) | (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)); /* see iemAImpl_fld1 */
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fld1,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes))
{
    pFpuRes->r80Result.sj64.fSign       = 0;
    pFpuRes->r80Result.sj64.uExponent   = 0 + 16383;
    pFpuRes->r80Result.sj64.fInteger    = 1;
    pFpuRes->r80Result.sj64.uFraction   = 0;

    /*
     * FPU status word:
     *      - TOP is irrelevant, but we must match x86 assembly version.
     *      - C1 is always cleared as we don't have any stack overflows.
     *      - C0, C2, and C3 are undefined and Intel 10980XE does not touch them.
     */
    pFpuRes->FSW = (7 << X86_FSW_TOP_SHIFT) | (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3));
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fldl2e,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes))
{
    pFpuRes->r80Result.sj64.fSign       = 0;
    pFpuRes->r80Result.sj64.uExponent   = 0 + 16383;
    pFpuRes->r80Result.sj64.fInteger    = 1;
    pFpuRes->r80Result.sj64.uFraction   =    (pFpuState->FCW & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST
                                          || (pFpuState->FCW & X86_FCW_RC_MASK) == X86_FCW_RC_UP
                                        ? UINT64_C(0x38aa3b295c17f0bc) : UINT64_C(0x38aa3b295c17f0bb);
    pFpuRes->FSW = (7 << X86_FSW_TOP_SHIFT) | (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)); /* see iemAImpl_fld1 */
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fldl2t,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes))
{
    pFpuRes->r80Result.sj64.fSign       = 0;
    pFpuRes->r80Result.sj64.uExponent   = 1 + 16383;
    pFpuRes->r80Result.sj64.fInteger    = 1;
    pFpuRes->r80Result.sj64.uFraction   = (pFpuState->FCW & X86_FCW_RC_MASK) != X86_FCW_RC_UP
                                        ? UINT64_C(0x549a784bcd1b8afe) : UINT64_C(0x549a784bcd1b8aff);
    pFpuRes->FSW = (7 << X86_FSW_TOP_SHIFT) | (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)); /* see iemAImpl_fld1 */
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fldlg2,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes))
{
    pFpuRes->r80Result.sj64.fSign       = 0;
    pFpuRes->r80Result.sj64.uExponent   = -2 + 16383;
    pFpuRes->r80Result.sj64.fInteger    = 1;
    pFpuRes->r80Result.sj64.uFraction   =    (pFpuState->FCW & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST
                                          || (pFpuState->FCW & X86_FCW_RC_MASK) == X86_FCW_RC_UP
                                        ? UINT64_C(0x1a209a84fbcff799) : UINT64_C(0x1a209a84fbcff798);
    pFpuRes->FSW = (7 << X86_FSW_TOP_SHIFT) | (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)); /* see iemAImpl_fld1 */
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fldln2,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes))
{
    pFpuRes->r80Result.sj64.fSign       = 0;
    pFpuRes->r80Result.sj64.uExponent   = -1 + 16383;
    pFpuRes->r80Result.sj64.fInteger    = 1;
    pFpuRes->r80Result.sj64.uFraction   =    (pFpuState->FCW & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST
                                          || (pFpuState->FCW & X86_FCW_RC_MASK) == X86_FCW_RC_UP
                                        ? UINT64_C(0x317217f7d1cf79ac) : UINT64_C(0x317217f7d1cf79ab);
    pFpuRes->FSW = (7 << X86_FSW_TOP_SHIFT) | (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)); /* see iemAImpl_fld1 */
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fldpi,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes))
{
    pFpuRes->r80Result.sj64.fSign       = 0;
    pFpuRes->r80Result.sj64.uExponent   = 1 + 16383;
    pFpuRes->r80Result.sj64.fInteger    = 1;
    pFpuRes->r80Result.sj64.uFraction   =    (pFpuState->FCW & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST
                                          || (pFpuState->FCW & X86_FCW_RC_MASK) == X86_FCW_RC_UP
                                        ? UINT64_C(0x490fdaa22168c235) : UINT64_C(0x490fdaa22168c234);
    pFpuRes->FSW = (7 << X86_FSW_TOP_SHIFT) | (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)); /* see iemAImpl_fld1 */
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fldz,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes))
{
    pFpuRes->r80Result.s.fSign      = 0;
    pFpuRes->r80Result.s.uExponent  = 0;
    pFpuRes->r80Result.s.uMantissa  = 0;
    pFpuRes->FSW = (7 << X86_FSW_TOP_SHIFT) | (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)); /* see iemAImpl_fld1 */
}

#define EMIT_FILD(a_cBits) \
IEM_DECL_IMPL_DEF(void, iemAImpl_fild_r80_from_i ## a_cBits,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, \
                                                             int ## a_cBits ## _t const *piVal)) \
{ \
    int  ## a_cBits ## _t iVal = *piVal; \
    if (iVal == 0) \
    { \
        pFpuRes->r80Result.s.fSign      = 0; \
        pFpuRes->r80Result.s.uExponent  = 0; \
        pFpuRes->r80Result.s.uMantissa  = 0; \
    } \
    else \
    { \
        if (iVal > 0) \
            pFpuRes->r80Result.s.fSign  = 0; \
        else \
        { \
            pFpuRes->r80Result.s.fSign  = 1; \
            iVal = -iVal; \
        } \
        unsigned const cBits = ASMBitLastSetU ## a_cBits((uint ## a_cBits ## _t)iVal); \
        pFpuRes->r80Result.s.uExponent  = cBits - 1 + RTFLOAT80U_EXP_BIAS; \
        pFpuRes->r80Result.s.uMantissa  = (uint64_t)iVal << (RTFLOAT80U_FRACTION_BITS + 1 - cBits); \
    } \
    pFpuRes->FSW = (7 << X86_FSW_TOP_SHIFT) | (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)); /* see iemAImpl_fld1 */ \
}
EMIT_FILD(16)
EMIT_FILD(32)
EMIT_FILD(64)


IEM_DECL_IMPL_DEF(void, iemAImpl_fld_r80_from_d80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTPBCD80U pd80Val))
{
    pFpuRes->FSW = (7 << X86_FSW_TOP_SHIFT) | (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)); /* see iemAImpl_fld1 */
    if (   pd80Val->s.abPairs[0] == 0
        && pd80Val->s.abPairs[1] == 0
        && pd80Val->s.abPairs[2] == 0
        && pd80Val->s.abPairs[3] == 0
        && pd80Val->s.abPairs[4] == 0
        && pd80Val->s.abPairs[5] == 0
        && pd80Val->s.abPairs[6] == 0
        && pd80Val->s.abPairs[7] == 0
        && pd80Val->s.abPairs[8] == 0)
    {
        pFpuRes->r80Result.s.fSign      = pd80Val->s.fSign;
        pFpuRes->r80Result.s.uExponent  = 0;
        pFpuRes->r80Result.s.uMantissa  = 0;
    }
    else
    {
        pFpuRes->r80Result.s.fSign      = pd80Val->s.fSign;

        size_t   cPairs  = RT_ELEMENTS(pd80Val->s.abPairs);
        while (cPairs > 0 && pd80Val->s.abPairs[cPairs - 1] == 0)
            cPairs--;

        uint64_t uVal    = 0;
        uint64_t uFactor = 1;
        for (size_t iPair = 0; iPair < cPairs; iPair++, uFactor *= 100)
            uVal += RTPBCD80U_LO_DIGIT(pd80Val->s.abPairs[iPair]) * uFactor
                  + RTPBCD80U_HI_DIGIT(pd80Val->s.abPairs[iPair]) * uFactor * 10;

        unsigned const cBits = ASMBitLastSetU64(uVal);
        pFpuRes->r80Result.s.uExponent  = cBits - 1 + RTFLOAT80U_EXP_BIAS;
        pFpuRes->r80Result.s.uMantissa  = uVal << (RTFLOAT80U_FRACTION_BITS + 1 - cBits);
    }
}


/*********************************************************************************************************************************
*   x87 FPU Stores                                                                                                               *
*********************************************************************************************************************************/

/**
 * Helper for storing a deconstructed and normal R80 value as a 64-bit one.
 *
 * This uses the rounding rules indicated by fFcw and returns updated fFsw.
 *
 * @returns Updated FPU status word value.
 * @param   fSignIn     Incoming sign indicator.
 * @param   uMantissaIn Incoming mantissa (dot between bit 63 and 62).
 * @param   iExponentIn Unbiased exponent.
 * @param   fFcw        The FPU control word.
 * @param   fFsw        Prepped FPU status word, i.e. exceptions and C1 clear.
 * @param   pr32Dst     Where to return the output value, if one should be
 *                      returned.
 *
 * @note    Tailored as a helper for iemAImpl_fst_r80_to_r32 right now.
 * @note    Exact same logic as iemAImpl_StoreNormalR80AsR64.
 */
static uint16_t iemAImpl_StoreNormalR80AsR32(bool fSignIn, uint64_t uMantissaIn, int32_t iExponentIn,
                                             uint16_t fFcw, uint16_t fFsw, PRTFLOAT32U pr32Dst)
{
    uint64_t const fRoundingOffMask = RT_BIT_64(RTFLOAT80U_FRACTION_BITS - RTFLOAT32U_FRACTION_BITS) - 1; /* 0x7ff */
    uint64_t const uRoundingAdd     = (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST
                                    ? RT_BIT_64(RTFLOAT80U_FRACTION_BITS - RTFLOAT32U_FRACTION_BITS - 1)  /* 0x400 */
                                    : (fFcw & X86_FCW_RC_MASK) == (fSignIn ? X86_FCW_RC_DOWN : X86_FCW_RC_UP)
                                    ? fRoundingOffMask
                                    : 0;
    uint64_t       fRoundedOff      = uMantissaIn & fRoundingOffMask;

    /*
     * Deal with potential overflows/underflows first, optimizing for none.
     * 0 and MAX are used for special values; MAX-1 may be rounded up to MAX.
     */
    int32_t iExponentOut = (int32_t)iExponentIn + RTFLOAT32U_EXP_BIAS;
    if ((uint32_t)iExponentOut - 1 < (uint32_t)(RTFLOAT32U_EXP_MAX - 3))
    { /* likely? */ }
    /*
     * Underflow if the exponent zero or negative.  This is attempted mapped
     * to a subnormal number when possible, with some additional trickery ofc.
     */
    else if (iExponentOut <= 0)
    {
        bool const fIsTiny = iExponentOut < 0
                          || UINT64_MAX - uMantissaIn > uRoundingAdd;
        if (!(fFcw & X86_FCW_UM) && fIsTiny)
            /* Note! 754-1985 sec 7.4 has something about bias adjust of 192 here, not in 2008 & 2019. Perhaps only 8087 & 287? */
            return fFsw | X86_FSW_UE | X86_FSW_ES | X86_FSW_B;

        if (iExponentOut <= 0)
        {
            uMantissaIn = iExponentOut <= -63
                        ? uMantissaIn != 0
                        : (uMantissaIn >> (-iExponentOut + 1)) | ((uMantissaIn & (RT_BIT_64(-iExponentOut + 1) - 1)) != 0);
            fRoundedOff = uMantissaIn & fRoundingOffMask;
            if (fRoundedOff && fIsTiny)
                fFsw |= X86_FSW_UE;
            iExponentOut   = 0;
        }
    }
    /*
     * Overflow if at or above max exponent value or if we will reach max
     * when rounding.  Will return +/-zero or +/-max value depending on
     * whether we're rounding or not.
     */
    else if (   iExponentOut >= RTFLOAT32U_EXP_MAX
             || (   iExponentOut == RTFLOAT32U_EXP_MAX - 1
                 && UINT64_MAX - uMantissaIn <= uRoundingAdd))
    {
        fFsw |= X86_FSW_OE;
        if (!(fFcw & X86_FCW_OM))
            return fFsw | X86_FSW_ES | X86_FSW_B;
        fFsw |= X86_FSW_PE;
        if (uRoundingAdd)
            fFsw |= X86_FSW_C1;
        if (!(fFcw & X86_FCW_PM))
            fFsw |= X86_FSW_ES | X86_FSW_B;

        pr32Dst->s.fSign         = fSignIn;
        if (uRoundingAdd)
        {   /* Zero */
            pr32Dst->s.uExponent = RTFLOAT32U_EXP_MAX;
            pr32Dst->s.uFraction = 0;
        }
        else
        {   /* Max */
            pr32Dst->s.uExponent = RTFLOAT32U_EXP_MAX - 1;
            pr32Dst->s.uFraction = RT_BIT_32(RTFLOAT32U_FRACTION_BITS) - 1;
        }
        return fFsw;
    }

    /*
     * Normal or subnormal number.
     */
    /* Do rounding - just truncate in near mode when midway on an even outcome. */
    uint64_t uMantissaOut = uMantissaIn;
    if (   (fFcw & X86_FCW_RC_MASK) != X86_FCW_RC_NEAREST
        || (uMantissaIn & RT_BIT_64(RTFLOAT80U_FRACTION_BITS - RTFLOAT32U_FRACTION_BITS))
        || fRoundedOff != uRoundingAdd)
    {
        uMantissaOut = uMantissaIn + uRoundingAdd;
        if (uMantissaOut >= uMantissaIn)
        { /* likely */ }
        else
        {
            uMantissaOut >>= 1; /* (We don't need to add bit 63 here (the integer bit), as it will be chopped off below.) */
            iExponentOut++;
            Assert(iExponentOut < RTFLOAT32U_EXP_MAX);  /* checked above */
            fFsw |= X86_FSW_C1;
        }
    }
    else
        uMantissaOut = uMantissaIn;

    /* Truncate the mantissa and set the return value. */
    uMantissaOut >>= RTFLOAT80U_FRACTION_BITS - RTFLOAT32U_FRACTION_BITS;

    pr32Dst->s.uFraction = (uint32_t)uMantissaOut; /* Note! too big for bitfield if normal. */
    pr32Dst->s.uExponent = iExponentOut;
    pr32Dst->s.fSign     = fSignIn;

    /* Set status flags realted to rounding. */
    if (fRoundedOff)
    {
        fFsw |= X86_FSW_PE;
        if (uMantissaOut > (uMantissaIn >> (RTFLOAT80U_FRACTION_BITS - RTFLOAT32U_FRACTION_BITS)))
            fFsw |= X86_FSW_C1;
        if (!(fFcw & X86_FCW_PM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }

    return fFsw;
}


/**
 * @note Exact same logic as iemAImpl_fst_r80_to_r64.
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_fst_r80_to_r32,(PCX86FXSTATE pFpuState, uint16_t *pu16FSW,
                                                 PRTFLOAT32U pr32Dst, PCRTFLOAT80U pr80Src))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t       fFsw = (7 << X86_FSW_TOP_SHIFT) | (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3));
    if (RTFLOAT80U_IS_NORMAL(pr80Src))
        fFsw = iemAImpl_StoreNormalR80AsR32(pr80Src->s.fSign, pr80Src->s.uMantissa,
                                            (int32_t)pr80Src->s.uExponent - RTFLOAT80U_EXP_BIAS, fFcw, fFsw, pr32Dst);
    else if (RTFLOAT80U_IS_ZERO(pr80Src))
    {
        pr32Dst->s.fSign      = pr80Src->s.fSign;
        pr32Dst->s.uExponent  = 0;
        pr32Dst->s.uFraction  = 0;
        Assert(RTFLOAT32U_IS_ZERO(pr32Dst));
    }
    else if (RTFLOAT80U_IS_INF(pr80Src))
    {
        pr32Dst->s.fSign      = pr80Src->s.fSign;
        pr32Dst->s.uExponent  = RTFLOAT32U_EXP_MAX;
        pr32Dst->s.uFraction  = 0;
        Assert(RTFLOAT32U_IS_INF(pr32Dst));
    }
    else if (RTFLOAT80U_IS_INDEFINITE(pr80Src))
    {
        /* Mapped to +/-QNaN */
        pr32Dst->s.fSign      = pr80Src->s.fSign;
        pr32Dst->s.uExponent  = RTFLOAT32U_EXP_MAX;
        pr32Dst->s.uFraction  = RT_BIT_32(RTFLOAT32U_FRACTION_BITS - 1);
    }
    else if (RTFLOAT80U_IS_PSEUDO_INF(pr80Src) || RTFLOAT80U_IS_UNNORMAL(pr80Src) || RTFLOAT80U_IS_PSEUDO_NAN(pr80Src))
    {
        /* Pseudo-Inf / Pseudo-Nan / Unnormal -> QNaN (during load, probably) */
        if (fFcw & X86_FCW_IM)
        {
            pr32Dst->s.fSign      = 1;
            pr32Dst->s.uExponent  = RTFLOAT32U_EXP_MAX;
            pr32Dst->s.uFraction  = RT_BIT_32(RTFLOAT32U_FRACTION_BITS - 1);
            fFsw |= X86_FSW_IE;
        }
        else
            fFsw |= X86_FSW_IE | X86_FSW_ES | X86_FSW_B;;
    }
    else if (RTFLOAT80U_IS_NAN(pr80Src))
    {
        /* IM applies to signalled NaN input only. Everything is converted to quiet NaN. */
        if ((fFcw & X86_FCW_IM) || !RTFLOAT80U_IS_SIGNALLING_NAN(pr80Src))
        {
            pr32Dst->s.fSign      = pr80Src->s.fSign;
            pr32Dst->s.uExponent  = RTFLOAT32U_EXP_MAX;
            pr32Dst->s.uFraction  = (uint32_t)(pr80Src->sj64.uFraction >> (RTFLOAT80U_FRACTION_BITS - RTFLOAT32U_FRACTION_BITS));
            pr32Dst->s.uFraction |= RT_BIT_32(RTFLOAT32U_FRACTION_BITS - 1);
            if (RTFLOAT80U_IS_SIGNALLING_NAN(pr80Src))
                fFsw |= X86_FSW_IE;
        }
        else
            fFsw |= X86_FSW_IE | X86_FSW_ES | X86_FSW_B;
    }
    else
    {
        /* Denormal values causes both an underflow and precision exception. */
        Assert(RTFLOAT80U_IS_DENORMAL(pr80Src) || RTFLOAT80U_IS_PSEUDO_DENORMAL(pr80Src));
        if (fFcw & X86_FCW_UM)
        {
            pr32Dst->s.fSign     = pr80Src->s.fSign;
            pr32Dst->s.uExponent = 0;
            if ((fFcw & X86_FCW_RC_MASK) == (!pr80Src->s.fSign ? X86_FCW_RC_UP : X86_FCW_RC_DOWN))
            {
                pr32Dst->s.uFraction = 1;
                fFsw |= X86_FSW_UE | X86_FSW_PE | X86_FSW_C1;
                if (!(fFcw & X86_FCW_PM))
                    fFsw |= X86_FSW_ES | X86_FSW_B;
            }
            else
            {
                pr32Dst->s.uFraction = 0;
                fFsw |= X86_FSW_UE | X86_FSW_PE;
                if (!(fFcw & X86_FCW_PM))
                    fFsw |= X86_FSW_ES | X86_FSW_B;
            }
        }
        else
            fFsw |= X86_FSW_UE | X86_FSW_ES | X86_FSW_B;
    }
    *pu16FSW = fFsw;
}


/**
 * Helper for storing a deconstructed and normal R80 value as a 64-bit one.
 *
 * This uses the rounding rules indicated by fFcw and returns updated fFsw.
 *
 * @returns Updated FPU status word value.
 * @param   fSignIn     Incoming sign indicator.
 * @param   uMantissaIn Incoming mantissa (dot between bit 63 and 62).
 * @param   iExponentIn Unbiased exponent.
 * @param   fFcw        The FPU control word.
 * @param   fFsw        Prepped FPU status word, i.e. exceptions and C1 clear.
 * @param   pr64Dst     Where to return the output value, if one should be
 *                      returned.
 *
 * @note    Tailored as a helper for iemAImpl_fst_r80_to_r64 right now.
 * @note    Exact same logic as iemAImpl_StoreNormalR80AsR32.
 */
static uint16_t iemAImpl_StoreNormalR80AsR64(bool fSignIn, uint64_t uMantissaIn, int32_t iExponentIn,
                                             uint16_t fFcw, uint16_t fFsw, PRTFLOAT64U pr64Dst)
{
    uint64_t const fRoundingOffMask = RT_BIT_64(RTFLOAT80U_FRACTION_BITS - RTFLOAT64U_FRACTION_BITS) - 1; /* 0x7ff */
    uint32_t const uRoundingAdd     = (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST
                                    ? RT_BIT_64(RTFLOAT80U_FRACTION_BITS - RTFLOAT64U_FRACTION_BITS - 1)  /* 0x400 */
                                    : (fFcw & X86_FCW_RC_MASK) == (fSignIn ? X86_FCW_RC_DOWN : X86_FCW_RC_UP)
                                    ? fRoundingOffMask
                                    : 0;
    uint32_t       fRoundedOff      = uMantissaIn & fRoundingOffMask;

    /*
     * Deal with potential overflows/underflows first, optimizing for none.
     * 0 and MAX are used for special values; MAX-1 may be rounded up to MAX.
     */
    int32_t iExponentOut = (int32_t)iExponentIn + RTFLOAT64U_EXP_BIAS;
    if ((uint32_t)iExponentOut - 1 < (uint32_t)(RTFLOAT64U_EXP_MAX - 3))
    { /* likely? */ }
    /*
     * Underflow if the exponent zero or negative.  This is attempted mapped
     * to a subnormal number when possible, with some additional trickery ofc.
     */
    else if (iExponentOut <= 0)
    {
        bool const fIsTiny = iExponentOut < 0
                          || UINT64_MAX - uMantissaIn > uRoundingAdd;
        if (!(fFcw & X86_FCW_UM) && fIsTiny)
            /* Note! 754-1985 sec 7.4 has something about bias adjust of 1536 here, not in 2008 & 2019. Perhaps only 8087 & 287? */
            return fFsw | X86_FSW_UE | X86_FSW_ES | X86_FSW_B;

        if (iExponentOut <= 0)
        {
            uMantissaIn  = iExponentOut <= -63
                         ? uMantissaIn != 0
                         : (uMantissaIn >> (-iExponentOut + 1)) | ((uMantissaIn & (RT_BIT_64(-iExponentOut + 1) - 1)) != 0);
            fRoundedOff  = uMantissaIn & fRoundingOffMask;
            if (fRoundedOff && fIsTiny)
                fFsw    |= X86_FSW_UE;
            iExponentOut = 0;
        }
    }
    /*
     * Overflow if at or above max exponent value or if we will reach max
     * when rounding.  Will return +/-zero or +/-max value depending on
     * whether we're rounding or not.
     */
    else if (   iExponentOut >= RTFLOAT64U_EXP_MAX
             || (   iExponentOut == RTFLOAT64U_EXP_MAX - 1
                 && UINT64_MAX - uMantissaIn <= uRoundingAdd))
    {
        fFsw |= X86_FSW_OE;
        if (!(fFcw & X86_FCW_OM))
            return fFsw | X86_FSW_ES | X86_FSW_B;
        fFsw |= X86_FSW_PE;
        if (uRoundingAdd)
            fFsw |= X86_FSW_C1;
        if (!(fFcw & X86_FCW_PM))
            fFsw |= X86_FSW_ES | X86_FSW_B;

        pr64Dst->s64.fSign         = fSignIn;
        if (uRoundingAdd)
        {   /* Zero */
            pr64Dst->s64.uExponent = RTFLOAT64U_EXP_MAX;
            pr64Dst->s64.uFraction = 0;
        }
        else
        {   /* Max */
            pr64Dst->s64.uExponent = RTFLOAT64U_EXP_MAX - 1;
            pr64Dst->s64.uFraction = RT_BIT_64(RTFLOAT64U_FRACTION_BITS) - 1;
        }
        return fFsw;
    }

    /*
     * Normal or subnormal number.
     */
    /* Do rounding - just truncate in near mode when midway on an even outcome. */
    uint64_t uMantissaOut = uMantissaIn;
    if (   (fFcw & X86_FCW_RC_MASK) != X86_FCW_RC_NEAREST
        || (uMantissaIn & RT_BIT_32(RTFLOAT80U_FRACTION_BITS - RTFLOAT64U_FRACTION_BITS))
        || fRoundedOff != uRoundingAdd)
    {
        uMantissaOut = uMantissaIn + uRoundingAdd;
        if (uMantissaOut >= uMantissaIn)
        { /* likely */ }
        else
        {
            uMantissaOut >>= 1; /* (We don't need to add bit 63 here (the integer bit), as it will be chopped off below.) */
            iExponentOut++;
            Assert(iExponentOut < RTFLOAT64U_EXP_MAX);  /* checked above */
            fFsw |= X86_FSW_C1;
        }
    }
    else
        uMantissaOut = uMantissaIn;

    /* Truncate the mantissa and set the return value. */
    uMantissaOut >>= RTFLOAT80U_FRACTION_BITS - RTFLOAT64U_FRACTION_BITS;

    pr64Dst->s64.uFraction = uMantissaOut; /* Note! too big for bitfield if normal. */
    pr64Dst->s64.uExponent = iExponentOut;
    pr64Dst->s64.fSign     = fSignIn;

    /* Set status flags realted to rounding. */
    if (fRoundedOff)
    {
        fFsw |= X86_FSW_PE;
        if (uMantissaOut > (uMantissaIn >> (RTFLOAT80U_FRACTION_BITS - RTFLOAT64U_FRACTION_BITS)))
            fFsw |= X86_FSW_C1;
        if (!(fFcw & X86_FCW_PM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }

    return fFsw;
}


/**
 * @note Exact same logic as iemAImpl_fst_r80_to_r32.
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_fst_r80_to_r64,(PCX86FXSTATE pFpuState, uint16_t *pu16FSW,
                                                 PRTFLOAT64U pr64Dst, PCRTFLOAT80U pr80Src))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t       fFsw = (7 << X86_FSW_TOP_SHIFT) | (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3));
    if (RTFLOAT80U_IS_NORMAL(pr80Src))
        fFsw = iemAImpl_StoreNormalR80AsR64(pr80Src->s.fSign, pr80Src->s.uMantissa,
                                            (int32_t)pr80Src->s.uExponent - RTFLOAT80U_EXP_BIAS, fFcw, fFsw, pr64Dst);
    else if (RTFLOAT80U_IS_ZERO(pr80Src))
    {
        pr64Dst->s64.fSign      = pr80Src->s.fSign;
        pr64Dst->s64.uExponent  = 0;
        pr64Dst->s64.uFraction  = 0;
        Assert(RTFLOAT64U_IS_ZERO(pr64Dst));
    }
    else if (RTFLOAT80U_IS_INF(pr80Src))
    {
        pr64Dst->s64.fSign      = pr80Src->s.fSign;
        pr64Dst->s64.uExponent  = RTFLOAT64U_EXP_MAX;
        pr64Dst->s64.uFraction  = 0;
        Assert(RTFLOAT64U_IS_INF(pr64Dst));
    }
    else if (RTFLOAT80U_IS_INDEFINITE(pr80Src))
    {
        /* Mapped to +/-QNaN */
        pr64Dst->s64.fSign      = pr80Src->s.fSign;
        pr64Dst->s64.uExponent  = RTFLOAT64U_EXP_MAX;
        pr64Dst->s64.uFraction  = RT_BIT_64(RTFLOAT64U_FRACTION_BITS - 1);
    }
    else if (RTFLOAT80U_IS_PSEUDO_INF(pr80Src) || RTFLOAT80U_IS_UNNORMAL(pr80Src) || RTFLOAT80U_IS_PSEUDO_NAN(pr80Src))
    {
        /* Pseudo-Inf / Pseudo-Nan / Unnormal -> QNaN (during load, probably) */
        if (fFcw & X86_FCW_IM)
        {
            pr64Dst->s64.fSign      = 1;
            pr64Dst->s64.uExponent  = RTFLOAT64U_EXP_MAX;
            pr64Dst->s64.uFraction  = RT_BIT_64(RTFLOAT64U_FRACTION_BITS - 1);
            fFsw |= X86_FSW_IE;
        }
        else
            fFsw |= X86_FSW_IE | X86_FSW_ES | X86_FSW_B;;
    }
    else if (RTFLOAT80U_IS_NAN(pr80Src))
    {
        /* IM applies to signalled NaN input only. Everything is converted to quiet NaN. */
        if ((fFcw & X86_FCW_IM) || !RTFLOAT80U_IS_SIGNALLING_NAN(pr80Src))
        {
            pr64Dst->s64.fSign      = pr80Src->s.fSign;
            pr64Dst->s64.uExponent  = RTFLOAT64U_EXP_MAX;
            pr64Dst->s64.uFraction  = pr80Src->sj64.uFraction >> (RTFLOAT80U_FRACTION_BITS - RTFLOAT64U_FRACTION_BITS);
            pr64Dst->s64.uFraction |= RT_BIT_64(RTFLOAT64U_FRACTION_BITS - 1);
            if (RTFLOAT80U_IS_SIGNALLING_NAN(pr80Src))
                fFsw |= X86_FSW_IE;
        }
        else
            fFsw |= X86_FSW_IE | X86_FSW_ES | X86_FSW_B;
    }
    else
    {
        /* Denormal values causes both an underflow and precision exception. */
        Assert(RTFLOAT80U_IS_DENORMAL(pr80Src) || RTFLOAT80U_IS_PSEUDO_DENORMAL(pr80Src));
        if (fFcw & X86_FCW_UM)
        {
            pr64Dst->s64.fSign     = pr80Src->s.fSign;
            pr64Dst->s64.uExponent = 0;
            if ((fFcw & X86_FCW_RC_MASK) == (!pr80Src->s.fSign ? X86_FCW_RC_UP : X86_FCW_RC_DOWN))
            {
                pr64Dst->s64.uFraction = 1;
                fFsw |= X86_FSW_UE | X86_FSW_PE | X86_FSW_C1;
                if (!(fFcw & X86_FCW_PM))
                    fFsw |= X86_FSW_ES | X86_FSW_B;
            }
            else
            {
                pr64Dst->s64.uFraction = 0;
                fFsw |= X86_FSW_UE | X86_FSW_PE;
                if (!(fFcw & X86_FCW_PM))
                    fFsw |= X86_FSW_ES | X86_FSW_B;
            }
        }
        else
            fFsw |= X86_FSW_UE | X86_FSW_ES | X86_FSW_B;
    }
    *pu16FSW = fFsw;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fst_r80_to_r80,(PCX86FXSTATE pFpuState, uint16_t *pu16FSW,
                                                 PRTFLOAT80U pr80Dst, PCRTFLOAT80U pr80Src))
{
    /*
     * FPU status word:
     *      - TOP is irrelevant, but we must match x86 assembly version (0).
     *      - C1 is always cleared as we don't have any stack overflows.
     *      - C0, C2, and C3 are undefined and Intel 10980XE does not touch them.
     */
    *pu16FSW = pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3); /* see iemAImpl_fld1 */
    *pr80Dst = *pr80Src;
}


/*
 *
 * Mantissa:
 *  63        56        48        40        32        24        16         8         0
 *  v          v         v         v         v         v         v         v         v
 *  1[.]111 0000 1111 0000 1111 0000 1111 0000 1111 0000 1111 0000 1111 0000 1111 0000
 *      \    \    \    \    \    \    \    \    \    \    \    \    \    \    \    \
 * Exp: 0    4    8    12   16   20   24   28   32   36   40   44   48   52   56   60
 *
 * int64_t has the same width, only bit 63 is the sign bit.  So, the max we can map over
 * are bits 1 thru 63, dropping off bit 0, with an exponent of 62. The number of bits we
 * drop off from the mantissa increases with decreasing exponent, till an exponent of 0
 * where we'll drop off all but bit 63.
 */
#define EMIT_FIST(a_cBits, a_iType, a_iTypeMin, a_iTypeIndefinite) \
IEM_DECL_IMPL_DEF(void, iemAImpl_fist_r80_to_i ## a_cBits,(PCX86FXSTATE pFpuState, uint16_t *pu16FSW, \
                                                           a_iType *piDst, PCRTFLOAT80U pr80Val)) \
{ \
    uint16_t const fFcw    = pFpuState->FCW; \
    uint16_t       fFsw    = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)); \
    bool const     fSignIn = pr80Val->s.fSign; \
    \
    /* \
     * Deal with normal numbers first. \
     */ \
    if (RTFLOAT80U_IS_NORMAL(pr80Val)) \
    { \
        uint64_t       uMantissa = pr80Val->s.uMantissa; \
        int32_t        iExponent = (int32_t)pr80Val->s.uExponent - RTFLOAT80U_EXP_BIAS; \
        \
        if ((uint32_t)iExponent <= a_cBits - 2) \
        { \
            unsigned const cShiftOff        = 63 - iExponent; \
            uint64_t const fRoundingOffMask = RT_BIT_64(cShiftOff) - 1; \
            uint64_t const uRoundingAdd     = (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST \
                                            ? RT_BIT_64(cShiftOff - 1) \
                                            : (fFcw & X86_FCW_RC_MASK) == (fSignIn ? X86_FCW_RC_DOWN : X86_FCW_RC_UP) \
                                            ? fRoundingOffMask \
                                            : 0; \
            uint64_t       fRoundedOff      = uMantissa & fRoundingOffMask; \
            \
            uMantissa >>= cShiftOff; \
            uint64_t const uRounding = (fRoundedOff + uRoundingAdd) >> cShiftOff; \
            uMantissa += uRounding; \
            if (!(uMantissa & RT_BIT_64(a_cBits - 1))) \
            { \
                if (fRoundedOff) \
                { \
                    if ((uMantissa & 1) && (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST && fRoundedOff == uRoundingAdd) \
                        uMantissa &= ~(uint64_t)1; /* round to even number if equal distance between up/down. */ \
                    else if (uRounding) \
                        fFsw |= X86_FSW_C1; \
                    fFsw |= X86_FSW_PE; \
                    if (!(fFcw & X86_FCW_PM)) \
                        fFsw |= X86_FSW_ES | X86_FSW_B; \
                } \
                \
                if (!fSignIn) \
                    *piDst = (a_iType)uMantissa; \
                else \
                    *piDst = -(a_iType)uMantissa; \
            } \
            else \
            { \
                /* overflowed after rounding. */ \
                AssertMsg(iExponent == a_cBits - 2 && uMantissa == RT_BIT_64(a_cBits - 1), \
                          ("e=%d m=%#RX64 (org %#RX64) s=%d; shift=%d ro=%#RX64 rm=%#RX64 ra=%#RX64\n", iExponent, uMantissa, \
                          pr80Val->s.uMantissa, fSignIn, cShiftOff, fRoundedOff, fRoundingOffMask, uRoundingAdd)); \
                \
                /* Special case for the integer minimum value. */ \
                if (fSignIn) \
                { \
                    *piDst = a_iTypeMin; \
                    fFsw |= X86_FSW_PE | X86_FSW_C1; \
                    if (!(fFcw & X86_FCW_PM)) \
                        fFsw |= X86_FSW_ES | X86_FSW_B; \
                } \
                else \
                { \
                    fFsw |= X86_FSW_IE; \
                    if (fFcw & X86_FCW_IM) \
                        *piDst = a_iTypeMin; \
                    else \
                        fFsw |= X86_FSW_ES | X86_FSW_B | (7 << X86_FSW_TOP_SHIFT); \
                } \
            } \
        } \
        /* \
         * Tiny sub-zero numbers. \
         */ \
        else if (iExponent < 0) \
        { \
            if (!fSignIn) \
            { \
                if (   (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_UP \
                    || (iExponent == -1 && (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST)) \
                { \
                    *piDst = 1; \
                    fFsw |= X86_FSW_C1; \
                } \
                else \
                    *piDst = 0; \
            } \
            else \
            { \
                if (   (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_UP \
                    || (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_ZERO \
                    || (iExponent < -1 && (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST)) \
                    *piDst = 0; \
                else \
                { \
                    *piDst = -1; \
                    fFsw |= X86_FSW_C1; \
                } \
            } \
            fFsw |= X86_FSW_PE; \
            if (!(fFcw & X86_FCW_PM)) \
                fFsw |= X86_FSW_ES | X86_FSW_B; \
        } \
        /* \
         * Special MIN case. \
         */ \
        else if (   fSignIn && iExponent == a_cBits - 1 \
                 && ( a_cBits < 64 && (fFcw & X86_FCW_RC_MASK) != X86_FCW_RC_DOWN \
                     ? uMantissa < (RT_BIT_64(63) | RT_BIT_64(65 - a_cBits)) \
                     : uMantissa == RT_BIT_64(63))) \
        { \
            *piDst = a_iTypeMin;  \
            if (uMantissa & (RT_BIT_64(64 - a_cBits + 1) - 1)) \
            { \
                fFsw |= X86_FSW_PE; \
                if (!(fFcw & X86_FCW_PM)) \
                    fFsw |= X86_FSW_ES | X86_FSW_B; \
            } \
        } \
        /* \
         * Too large/small number outside the target integer range. \
         */ \
        else \
        { \
            fFsw |= X86_FSW_IE; \
            if (fFcw & X86_FCW_IM) \
                *piDst = a_iTypeIndefinite; \
            else \
                fFsw |= X86_FSW_ES | X86_FSW_B | (7 << X86_FSW_TOP_SHIFT); \
        } \
    } \
    /* \
     * Map both +0 and -0 to integer zero (signless/+). \
     */ \
    else if (RTFLOAT80U_IS_ZERO(pr80Val)) \
        *piDst = 0; \
    /* \
     * Denormals are just really tiny sub-zero numbers that are either rounded \
     * to zero, 1 or -1 depending on sign and rounding control. \
     */ \
    else if (RTFLOAT80U_IS_PSEUDO_DENORMAL(pr80Val) || RTFLOAT80U_IS_DENORMAL(pr80Val)) \
    { \
        if ((fFcw & X86_FCW_RC_MASK) != (fSignIn ? X86_FCW_RC_DOWN : X86_FCW_RC_UP)) \
            *piDst = 0; \
        else \
        { \
            *piDst = fSignIn ? -1 : 1; \
            fFsw |= X86_FSW_C1; \
        } \
        fFsw |= X86_FSW_PE; \
        if (!(fFcw & X86_FCW_PM)) \
            fFsw |= X86_FSW_ES | X86_FSW_B; \
    } \
    /* \
     * All other special values are considered invalid arguments and result \
     * in an IE exception and indefinite value if masked. \
     */ \
    else \
    { \
        fFsw |= X86_FSW_IE; \
        if (fFcw & X86_FCW_IM) \
            *piDst = a_iTypeIndefinite; \
        else \
            fFsw |= X86_FSW_ES | X86_FSW_B | (7 << X86_FSW_TOP_SHIFT); \
    } \
    *pu16FSW = fFsw; \
}
EMIT_FIST(64, int64_t, INT64_MIN, X86_FPU_INT64_INDEFINITE)
EMIT_FIST(32, int32_t, INT32_MIN, X86_FPU_INT32_INDEFINITE)
EMIT_FIST(16, int16_t, INT16_MIN, X86_FPU_INT16_INDEFINITE)

#endif /*IEM_WITHOUT_ASSEMBLY */


/*
 * The FISTT instruction was added with SSE3 and are a lot simpler than FIST.
 *
 * The 16-bit version is a bit peculiar, though, as it seems to be raising IE
 * as if it was the 32-bit version (i.e. starting with exp 31 instead of 15),
 * thus the @a a_cBitsIn.
 */
#define EMIT_FISTT(a_cBits, a_cBitsIn, a_iType, a_iTypeMin, a_iTypeMax, a_iTypeIndefinite, a_Suffix, a_fIntelVersion) \
IEM_DECL_IMPL_DEF(void, RT_CONCAT3(iemAImpl_fistt_r80_to_i,a_cBits,a_Suffix),(PCX86FXSTATE pFpuState, uint16_t *pu16FSW, \
                                                                              a_iType *piDst, PCRTFLOAT80U pr80Val)) \
{ \
    uint16_t const fFcw    = pFpuState->FCW; \
    uint16_t       fFsw    = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)); \
    bool const     fSignIn = pr80Val->s.fSign; \
    \
    /* \
     * Deal with normal numbers first. \
     */ \
    if (RTFLOAT80U_IS_NORMAL(pr80Val)) \
    { \
        uint64_t       uMantissa = pr80Val->s.uMantissa; \
        int32_t        iExponent = (int32_t)pr80Val->s.uExponent - RTFLOAT80U_EXP_BIAS; \
        \
        if ((uint32_t)iExponent <= a_cBitsIn - 2) \
        { \
            unsigned const cShiftOff        = 63 - iExponent; \
            uint64_t const fRoundingOffMask = RT_BIT_64(cShiftOff) - 1; \
            uint64_t const fRoundedOff      = uMantissa & fRoundingOffMask; \
            uMantissa >>= cShiftOff; \
            /*Assert(!(uMantissa & RT_BIT_64(a_cBits - 1)));*/ \
            if (!fSignIn) \
                *piDst = (a_iType)uMantissa; \
            else \
                *piDst = -(a_iType)uMantissa; \
            \
            if (fRoundedOff) \
            { \
                fFsw |= X86_FSW_PE; \
                if (!(fFcw & X86_FCW_PM)) \
                    fFsw |= X86_FSW_ES | X86_FSW_B; \
            } \
        } \
        /* \
         * Tiny sub-zero numbers. \
         */ \
        else if (iExponent < 0) \
        { \
            *piDst = 0; \
            fFsw |= X86_FSW_PE; \
            if (!(fFcw & X86_FCW_PM)) \
                fFsw |= X86_FSW_ES | X86_FSW_B; \
        } \
        /* \
         * Special MIN case. \
         */ \
        else if (   fSignIn && iExponent == a_cBits - 1 \
                 && (a_cBits < 64 \
                     ? uMantissa < (RT_BIT_64(63) | RT_BIT_64(65 - a_cBits)) \
                     : uMantissa == RT_BIT_64(63)) ) \
        { \
            *piDst = a_iTypeMin;  \
            if (uMantissa & (RT_BIT_64(64 - a_cBits + 1) - 1)) \
            { \
                fFsw |= X86_FSW_PE; \
                if (!(fFcw & X86_FCW_PM)) \
                    fFsw |= X86_FSW_ES | X86_FSW_B; \
            } \
        } \
        /* \
         * Figure this weirdness. \
         */ \
        else if (0 /* huh? gone? */ && a_cBits == 16 && fSignIn && iExponent == 31 && uMantissa < UINT64_C(0x8000100000000000) ) \
        { \
            *piDst = 0;  \
            if (uMantissa & (RT_BIT_64(64 - a_cBits + 1) - 1)) \
            { \
                fFsw |= X86_FSW_PE; \
                if (!(fFcw & X86_FCW_PM)) \
                    fFsw |= X86_FSW_ES | X86_FSW_B; \
            } \
        } \
        /* \
         * Too large/small number outside the target integer range. \
         */ \
        else \
        { \
            fFsw |= X86_FSW_IE; \
            if (fFcw & X86_FCW_IM) \
                *piDst = a_iTypeIndefinite; \
            else \
                fFsw |= X86_FSW_ES | X86_FSW_B | (7 << X86_FSW_TOP_SHIFT); \
        } \
    } \
    /* \
     * Map both +0 and -0 to integer zero (signless/+). \
     */ \
    else if (RTFLOAT80U_IS_ZERO(pr80Val)) \
        *piDst = 0; \
    /* \
     * Denormals are just really tiny sub-zero numbers that are trucated to zero. \
     */ \
    else if (RTFLOAT80U_IS_PSEUDO_DENORMAL(pr80Val) || RTFLOAT80U_IS_DENORMAL(pr80Val)) \
    { \
        *piDst = 0; \
        fFsw |= X86_FSW_PE; \
        if (!(fFcw & X86_FCW_PM)) \
            fFsw |= X86_FSW_ES | X86_FSW_B; \
    } \
    /* \
     * All other special values are considered invalid arguments and result \
     * in an IE exception and indefinite value if masked. \
     */ \
    else \
    { \
        fFsw |= X86_FSW_IE; \
        if (fFcw & X86_FCW_IM) \
            *piDst = a_iTypeIndefinite; \
        else \
            fFsw |= X86_FSW_ES | X86_FSW_B | (7 << X86_FSW_TOP_SHIFT); \
    } \
    *pu16FSW = fFsw; \
}
#if defined(IEM_WITHOUT_ASSEMBLY)
EMIT_FISTT(64, 64, int64_t, INT64_MIN, INT64_MAX, X86_FPU_INT64_INDEFINITE, RT_NOTHING, 1)
EMIT_FISTT(32, 32, int32_t, INT32_MIN, INT32_MAX, X86_FPU_INT32_INDEFINITE, RT_NOTHING, 1)
EMIT_FISTT(16, 16, int16_t, INT16_MIN, INT16_MAX, X86_FPU_INT16_INDEFINITE, RT_NOTHING, 1)
#endif
EMIT_FISTT(16, 16, int16_t, INT16_MIN, INT16_MAX, X86_FPU_INT16_INDEFINITE, _intel,     1)
EMIT_FISTT(16, 16, int16_t, INT16_MIN, INT16_MAX, X86_FPU_INT16_INDEFINITE, _amd,       0)


#if defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(void, iemAImpl_fst_r80_to_d80,(PCX86FXSTATE pFpuState, uint16_t *pu16FSW,
                                                 PRTPBCD80U pd80Dst, PCRTFLOAT80U pr80Src))
{
    /*static RTPBCD80U const s_ad80MaxMin[2] = { RTPBCD80U_INIT_MAX(),   RTPBCD80U_INIT_MIN() };*/
    static RTPBCD80U const s_ad80Zeros[2]  = { RTPBCD80U_INIT_ZERO(0), RTPBCD80U_INIT_ZERO(1) };
    static RTPBCD80U const s_ad80One[2]    = { RTPBCD80U_INIT_C(0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,1),
                                               RTPBCD80U_INIT_C(1, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,1) };
    static RTPBCD80U const s_d80Indefinite = RTPBCD80U_INIT_INDEFINITE();

    uint16_t const fFcw    = pFpuState->FCW;
    uint16_t       fFsw    = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3));
    bool const     fSignIn = pr80Src->s.fSign;

    /*
     * Deal with normal numbers first.
     */
    if (RTFLOAT80U_IS_NORMAL(pr80Src))
    {
        uint64_t       uMantissa = pr80Src->s.uMantissa;
        int32_t        iExponent = (int32_t)pr80Src->s.uExponent - RTFLOAT80U_EXP_BIAS;
        if (   (uint32_t)iExponent <= 58
            || ((uint32_t)iExponent == 59 && uMantissa <= UINT64_C(0xde0b6b3a763fffff)) )
        {
            unsigned const cShiftOff        = 63 - iExponent;
            uint64_t const fRoundingOffMask = RT_BIT_64(cShiftOff) - 1;
            uint64_t const uRoundingAdd     = (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST
                                            ? RT_BIT_64(cShiftOff - 1)
                                            : (fFcw & X86_FCW_RC_MASK) == (fSignIn ? X86_FCW_RC_DOWN : X86_FCW_RC_UP)
                                            ? fRoundingOffMask
                                            : 0;
            uint64_t       fRoundedOff      = uMantissa & fRoundingOffMask;

            uMantissa >>= cShiftOff;
            uint64_t const uRounding = (fRoundedOff + uRoundingAdd) >> cShiftOff;
            uMantissa += uRounding;
            if (uMantissa <= (uint64_t)RTPBCD80U_MAX)
            {
                if (fRoundedOff)
                {
                    if ((uMantissa & 1) && (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST && fRoundedOff == uRoundingAdd)
                        uMantissa &= ~(uint64_t)1; /* round to even number if equal distance between up/down. */
                    else if (uRounding)
                        fFsw |= X86_FSW_C1;
                    fFsw |= X86_FSW_PE;
                    if (!(fFcw & X86_FCW_PM))
                        fFsw |= X86_FSW_ES | X86_FSW_B;
                }

                pd80Dst->s.fSign = fSignIn;
                pd80Dst->s.uPad  = 0;
                for (size_t iPair = 0; iPair < RT_ELEMENTS(pd80Dst->s.abPairs); iPair++)
                {
                    unsigned const uDigits = uMantissa % 100;
                    uMantissa /= 100;
                    uint8_t const bLo = uDigits % 10;
                    uint8_t const bHi = uDigits / 10;
                    pd80Dst->s.abPairs[iPair] = RTPBCD80U_MAKE_PAIR(bHi, bLo);
                }
            }
            else
            {
                /* overflowed after rounding. */
                fFsw |= X86_FSW_IE;
                if (fFcw & X86_FCW_IM)
                    *pd80Dst = s_d80Indefinite;
                else
                    fFsw |= X86_FSW_ES | X86_FSW_B | (7 << X86_FSW_TOP_SHIFT);
            }
        }
        /*
         * Tiny sub-zero numbers.
         */
        else if (iExponent < 0)
        {
            if (!fSignIn)
            {
                if (   (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_UP
                    || (iExponent == -1 && (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST))
                {
                    *pd80Dst = s_ad80One[fSignIn];
                    fFsw |= X86_FSW_C1;
                }
                else
                    *pd80Dst = s_ad80Zeros[fSignIn];
            }
            else
            {
                if (   (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_UP
                    || (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_ZERO
                    || (iExponent < -1 && (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST))
                    *pd80Dst = s_ad80Zeros[fSignIn];
                else
                {
                    *pd80Dst = s_ad80One[fSignIn];
                    fFsw |= X86_FSW_C1;
                }
            }
            fFsw |= X86_FSW_PE;
            if (!(fFcw & X86_FCW_PM))
                fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        /*
         * Too large/small number outside the target integer range.
         */
        else
        {
            fFsw |= X86_FSW_IE;
            if (fFcw & X86_FCW_IM)
                *pd80Dst = s_d80Indefinite;
            else
                fFsw |= X86_FSW_ES | X86_FSW_B | (7 << X86_FSW_TOP_SHIFT);
        }
    }
    /*
     * Map both +0 and -0 to integer zero (signless/+).
     */
    else if (RTFLOAT80U_IS_ZERO(pr80Src))
        *pd80Dst = s_ad80Zeros[fSignIn];
    /*
     * Denormals are just really tiny sub-zero numbers that are either rounded
     * to zero, 1 or -1 depending on sign and rounding control.
     */
    else if (RTFLOAT80U_IS_PSEUDO_DENORMAL(pr80Src) || RTFLOAT80U_IS_DENORMAL(pr80Src))
    {
        if ((fFcw & X86_FCW_RC_MASK) != (fSignIn ? X86_FCW_RC_DOWN : X86_FCW_RC_UP))
            *pd80Dst = s_ad80Zeros[fSignIn];
        else
        {
            *pd80Dst = s_ad80One[fSignIn];
            fFsw |= X86_FSW_C1;
        }
        fFsw |= X86_FSW_PE;
        if (!(fFcw & X86_FCW_PM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }
    /*
     * All other special values are considered invalid arguments and result
     * in an IE exception and indefinite value if masked.
     */
    else
    {
        fFsw |= X86_FSW_IE;
        if (fFcw & X86_FCW_IM)
            *pd80Dst = s_d80Indefinite;
        else
            fFsw |= X86_FSW_ES | X86_FSW_B | (7 << X86_FSW_TOP_SHIFT);
    }
    *pu16FSW = fFsw;
}


/*********************************************************************************************************************************
*   FPU Helpers                                                                                                                  *
*********************************************************************************************************************************/
AssertCompileSize(RTFLOAT128U, 16);
AssertCompileSize(RTFLOAT80U,  10);
AssertCompileSize(RTFLOAT64U,   8);
AssertCompileSize(RTFLOAT32U,   4);

/**
 * Normalizes a possible pseudo-normal value.
 *
 * Psuedo-normal values are some oddities from the 8087 & 287 days.  They are
 * denormals with the J-bit set, so they can simply be rewritten as 2**-16382,
 * i.e. changing uExponent from 0 to 1.
 *
 * This macro will declare a RTFLOAT80U with the name given by
 * @a a_r80ValNormalized and update the @a a_pr80Val variable to point to it if
 * a normalization was performed.
 *
 * @note This must be applied before calling SoftFloat with a value that couldbe
 *       a pseudo-denormal, as SoftFloat doesn't handle pseudo-denormals
 *       correctly.
 */
#define IEM_NORMALIZE_PSEUDO_DENORMAL(a_pr80Val, a_r80ValNormalized) \
    RTFLOAT80U a_r80ValNormalized; \
    if (RTFLOAT80U_IS_PSEUDO_DENORMAL(a_pr80Val)) \
    { \
        a_r80ValNormalized = *a_pr80Val; \
        a_r80ValNormalized.s.uExponent = 1; \
        a_pr80Val = &a_r80ValNormalized; \
    } else do {} while (0)

#ifdef IEM_WITH_FLOAT128_FOR_FPU

DECLINLINE(int) iemFpuF128SetRounding(uint16_t fFcw)
{
    int fNew;
    switch (fFcw & X86_FCW_RC_MASK)
    {
        default:
        case X86_FCW_RC_NEAREST:    fNew = FE_TONEAREST; break;
        case X86_FCW_RC_ZERO:       fNew = FE_TOWARDZERO; break;
        case X86_FCW_RC_UP:         fNew = FE_UPWARD; break;
        case X86_FCW_RC_DOWN:       fNew = FE_DOWNWARD; break;
    }
    int fOld = fegetround();
    fesetround(fNew);
    return fOld;
}


DECLINLINE(void) iemFpuF128RestoreRounding(int fOld)
{
    fesetround(fOld);
}

DECLINLINE(_Float128) iemFpuF128FromFloat80(PCRTFLOAT80U pr80Val, uint16_t fFcw)
{
    RT_NOREF(fFcw);
    RTFLOAT128U Tmp;
    Tmp.s2.uSignAndExponent = pr80Val->s2.uSignAndExponent;
    Tmp.s2.uFractionHigh    = (uint16_t)((pr80Val->s2.uMantissa & (RT_BIT_64(63) - 1)) >> 48);
    Tmp.s2.uFractionMid     = (uint32_t)((pr80Val->s2.uMantissa & UINT32_MAX) >> 16);
    Tmp.s2.uFractionLow     = pr80Val->s2.uMantissa << 48;
    if (RTFLOAT80U_IS_PSEUDO_DENORMAL(pr80Val))
    {
        Assert(Tmp.s.uExponent == 0);
        Tmp.s2.uSignAndExponent++;
    }
    return *(_Float128 *)&Tmp;
}


DECLINLINE(uint16_t) iemFpuF128ToFloat80(PRTFLOAT80U pr80Dst, _Float128 rd128ValSrc, uint16_t fFcw, uint16_t fFsw)
{
    RT_NOREF(fFcw);
    RTFLOAT128U Tmp;
    *(_Float128 *)&Tmp = rd128ValSrc;
    ASMCompilerBarrier();
    if (RTFLOAT128U_IS_NORMAL(&Tmp))
    {
        pr80Dst->s.fSign     = Tmp.s64.fSign;
        pr80Dst->s.uExponent = Tmp.s64.uExponent;
        uint64_t uFraction   = Tmp.s64.uFractionHi << (63 - 48)
                             | Tmp.s64.uFractionLo >> (64 - 15);

        /* Do rounding - just truncate in near mode when midway on an even outcome. */
        unsigned const cShiftOff        = 64 - 15;
        uint64_t const fRoundingOffMask = RT_BIT_64(cShiftOff) - 1;
        uint64_t const uRoundedOff      = Tmp.s64.uFractionLo & fRoundingOffMask;
        if (uRoundedOff)
        {
            uint64_t const uRoundingAdd = (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST
                                        ? RT_BIT_64(cShiftOff - 1)
                                        : (fFcw & X86_FCW_RC_MASK) == (Tmp.s64.fSign ? X86_FCW_RC_DOWN : X86_FCW_RC_UP)
                                        ? fRoundingOffMask
                                        : 0;
            if (   (fFcw & X86_FCW_RC_MASK) != X86_FCW_RC_NEAREST
                || (Tmp.s64.uFractionLo & RT_BIT_64(cShiftOff))
                || uRoundedOff != uRoundingAdd)
            {
                if ((uRoundedOff + uRoundingAdd) >> cShiftOff)
                {
                    uFraction += 1;
                    if (!(uFraction & RT_BIT_64(63)))
                    { /* likely */ }
                    else
                    {
                        uFraction >>= 1;
                        pr80Dst->s.uExponent++;
                        if (pr80Dst->s.uExponent == RTFLOAT64U_EXP_MAX)
                            return fFsw;
                    }
                    fFsw |= X86_FSW_C1;
                }
            }
            fFsw |= X86_FSW_PE;
            if (!(fFcw & X86_FCW_PM))
                fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        pr80Dst->s.uMantissa = RT_BIT_64(63) | uFraction;
    }
    else if (RTFLOAT128U_IS_ZERO(&Tmp))
    {
        pr80Dst->s.fSign     = Tmp.s64.fSign;
        pr80Dst->s.uExponent = 0;
        pr80Dst->s.uMantissa = 0;
    }
    else if (RTFLOAT128U_IS_INF(&Tmp))
    {
        pr80Dst->s.fSign     = Tmp.s64.fSign;
        pr80Dst->s.uExponent = 0;
        pr80Dst->s.uMantissa = 0;
    }
    return fFsw;
}


#else  /* !IEM_WITH_FLOAT128_FOR_FPU - SoftFloat */

/** Initializer for the SoftFloat state structure. */
# define IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_FCW(a_fFcw) \
    { \
        softfloat_tininess_afterRounding, \
          ((a_fFcw) & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST ? (uint8_t)softfloat_round_near_even \
        : ((a_fFcw) & X86_FCW_RC_MASK) == X86_FCW_RC_UP      ? (uint8_t)softfloat_round_max \
        : ((a_fFcw) & X86_FCW_RC_MASK) == X86_FCW_RC_DOWN    ? (uint8_t)softfloat_round_min \
        :                                                      (uint8_t)softfloat_round_minMag, \
        0, \
        (uint8_t)((a_fFcw) & X86_FCW_XCPT_MASK), \
          ((a_fFcw) & X86_FCW_PC_MASK) == X86_FCW_PC_53      ? (uint8_t)64 \
        : ((a_fFcw) & X86_FCW_PC_MASK) == X86_FCW_PC_24      ? (uint8_t)32 : (uint8_t)80 \
    }

/** Returns updated FSW from a SoftFloat state and exception mask (FCW). */
# define IEM_SOFTFLOAT_STATE_TO_FSW(a_fFsw, a_pSoftState, a_fFcw) \
    (  (a_fFsw) \
     | (uint16_t)(((a_pSoftState)->exceptionFlags & softfloat_flag_c1) << 2) \
     | ((a_pSoftState)->exceptionFlags & X86_FSW_XCPT_MASK) \
     | (  ((a_pSoftState)->exceptionFlags & X86_FSW_XCPT_MASK) & (~(a_fFcw) & X86_FSW_XCPT_MASK) \
        ? X86_FSW_ES | X86_FSW_B : 0) )


DECLINLINE(float128_t) iemFpuSoftF128Precision(float128_t r128, unsigned cBits, uint16_t fFcw = X86_FCW_RC_NEAREST)
{
    RT_NOREF(fFcw);
    Assert(cBits > 64);
# if 0 /* rounding does not seem to help */
    uint64_t off = r128.v[0] & (RT_BIT_64(1 + 112 - cBits) - 1);
    r128.v[0] &= ~(RT_BIT_64(1 + 112 - cBits) - 1);
    if (off >= RT_BIT_64(1 + 112 - cBits - 1)
        && (r128.v[0] & RT_BIT_64(1 + 112 - cBits)))
    {
        uint64_t uOld = r128.v[0];
        r128.v[0] += RT_BIT_64(1 + 112 - cBits);
        if (r128.v[0] < uOld)
            r128.v[1] += 1;
    }
# else
    r128.v[0] &= ~(RT_BIT_64(1 + 112 - cBits) - 1);
# endif
    return r128;
}


DECLINLINE(float128_t) iemFpuSoftF128PrecisionIprt(PCRTFLOAT128U pr128, unsigned cBits, uint16_t fFcw = X86_FCW_RC_NEAREST)
{
    RT_NOREF(fFcw);
    Assert(cBits > 64);
# if 0 /* rounding does not seem to help, not even on constants */
    float128_t r128 = { pr128->au64[0], pr128->au64[1] };
    uint64_t off = r128.v[0] & (RT_BIT_64(1 + 112 - cBits) - 1);
    r128.v[0] &= ~(RT_BIT_64(1 + 112 - cBits) - 1);
    if (off >= RT_BIT_64(1 + 112 - cBits - 1)
        && (r128.v[0] & RT_BIT_64(1 + 112 - cBits)))
    {
        uint64_t uOld = r128.v[0];
        r128.v[0] += RT_BIT_64(1 + 112 - cBits);
        if (r128.v[0] < uOld)
            r128.v[1] += 1;
    }
    return r128;
# else
    float128_t r128 = { { pr128->au64[0] & ~(RT_BIT_64(1 + 112 - cBits) - 1), pr128->au64[1] } };
    return r128;
# endif
}


# if 0  /*  unused */
DECLINLINE(float128_t) iemFpuSoftF128FromIprt(PCRTFLOAT128U pr128)
{
    float128_t r128 = { { pr128->au64[0], pr128->au64[1] } };
    return r128;
}
# endif


/** Converts a 80-bit floating point value to SoftFloat 128-bit floating point. */
DECLINLINE(float128_t) iemFpuSoftF128FromFloat80(PCRTFLOAT80U pr80Val)
{
    extFloat80_t Tmp;
    Tmp.signExp = pr80Val->s2.uSignAndExponent;
    Tmp.signif  = pr80Val->s2.uMantissa;
    softfloat_state_t Ignored = SOFTFLOAT_STATE_INIT_DEFAULTS();
    return extF80_to_f128(Tmp, &Ignored);
}


/**
 * Converts from the packed IPRT 80-bit floating point (RTFLOAT80U) format to
 * the SoftFloat extended 80-bit floating point format (extFloat80_t).
 *
 * This is only a structure format conversion, nothing else.
 */
DECLINLINE(extFloat80_t) iemFpuSoftF80FromIprt(PCRTFLOAT80U pr80Val)
{
    extFloat80_t Tmp;
    Tmp.signExp = pr80Val->s2.uSignAndExponent;
    Tmp.signif  = pr80Val->s2.uMantissa;
    return Tmp;
}


/**
 * Converts from SoftFloat extended 80-bit floating point format (extFloat80_t)
 * to the packed IPRT 80-bit floating point (RTFLOAT80U) format.
 *
 * This is only a structure format conversion, nothing else.
 */
DECLINLINE(PRTFLOAT80U) iemFpuSoftF80ToIprt(PRTFLOAT80U pr80Dst, extFloat80_t const r80XSrc)
{
    pr80Dst->s2.uSignAndExponent = r80XSrc.signExp;
    pr80Dst->s2.uMantissa        = r80XSrc.signif;
    return pr80Dst;
}


DECLINLINE(uint16_t) iemFpuSoftF128ToFloat80(PRTFLOAT80U pr80Dst, float128_t r128Src, uint16_t fFcw, uint16_t fFsw)
{
    RT_NOREF(fFcw);
    RTFLOAT128U Tmp;
    *(float128_t *)&Tmp = r128Src;
    ASMCompilerBarrier();

    if (RTFLOAT128U_IS_NORMAL(&Tmp))
    {
        pr80Dst->s.fSign     = Tmp.s64.fSign;
        pr80Dst->s.uExponent = Tmp.s64.uExponent;
        uint64_t uFraction   = Tmp.s64.uFractionHi << (63 - 48)
                             | Tmp.s64.uFractionLo >> (64 - 15);

        /* Do rounding - just truncate in near mode when midway on an even outcome. */
        unsigned const cShiftOff        = 64 - 15;
        uint64_t const fRoundingOffMask = RT_BIT_64(cShiftOff) - 1;
        uint64_t const uRoundedOff      = Tmp.s64.uFractionLo & fRoundingOffMask;
        if (uRoundedOff)
        {
            uint64_t const uRoundingAdd = (fFcw & X86_FCW_RC_MASK) == X86_FCW_RC_NEAREST
                                        ? RT_BIT_64(cShiftOff - 1)
                                        : (fFcw & X86_FCW_RC_MASK) == (Tmp.s64.fSign ? X86_FCW_RC_DOWN : X86_FCW_RC_UP)
                                        ? fRoundingOffMask
                                        : 0;
            if (   (fFcw & X86_FCW_RC_MASK) != X86_FCW_RC_NEAREST
                || (Tmp.s64.uFractionLo & RT_BIT_64(cShiftOff))
                || uRoundedOff != uRoundingAdd)
            {
                if ((uRoundedOff + uRoundingAdd) >> cShiftOff)
                {
                    uFraction += 1;
                    if (!(uFraction & RT_BIT_64(63)))
                    { /* likely */ }
                    else
                    {
                        uFraction >>= 1;
                        pr80Dst->s.uExponent++;
                        if (pr80Dst->s.uExponent == RTFLOAT64U_EXP_MAX)
                            return fFsw;
                    }
                    fFsw |= X86_FSW_C1;
                }
            }
            fFsw |= X86_FSW_PE;
            if (!(fFcw & X86_FCW_PM))
                fFsw |= X86_FSW_ES | X86_FSW_B;
        }

        pr80Dst->s.uMantissa = RT_BIT_64(63) | uFraction;
    }
    else if (RTFLOAT128U_IS_ZERO(&Tmp))
    {
        pr80Dst->s.fSign     = Tmp.s64.fSign;
        pr80Dst->s.uExponent = 0;
        pr80Dst->s.uMantissa = 0;
    }
    else if (RTFLOAT128U_IS_INF(&Tmp))
    {
        pr80Dst->s.fSign     = Tmp.s64.fSign;
        pr80Dst->s.uExponent = 0x7fff;
        pr80Dst->s.uMantissa = 0;
    }
    return fFsw;
}


/**
 * Helper for transfering exception and C1 to FSW and setting the result value
 * accordingly.
 *
 * @returns Updated FSW.
 * @param   pSoftState      The SoftFloat state following the operation.
 * @param   r80XResult      The result of the SoftFloat operation.
 * @param   pr80Result      Where to store the result for IEM.
 * @param   fFcw            The FPU control word.
 * @param   fFsw            The FSW before the operation, with necessary bits
 *                          cleared and such.
 * @param   pr80XcptResult  Alternative return value for use an unmasked \#IE is
 *                          raised.
 */
DECLINLINE(uint16_t) iemFpuSoftStateAndF80ToFswAndIprtResult(softfloat_state_t const *pSoftState, extFloat80_t r80XResult,
                                                             PRTFLOAT80U pr80Result, uint16_t fFcw, uint16_t fFsw,
                                                             PCRTFLOAT80U pr80XcptResult)
{
    fFsw |= (pSoftState->exceptionFlags & X86_FSW_XCPT_MASK)
         | (uint16_t)((pSoftState->exceptionFlags & softfloat_flag_c1) << 2);
    if (fFsw & ~fFcw & X86_FSW_XCPT_MASK)
        fFsw |= X86_FSW_ES | X86_FSW_B;

    if (!(fFsw & ~fFcw & (X86_FSW_IE | X86_FSW_DE)))
        iemFpuSoftF80ToIprt(pr80Result, r80XResult);
    else
    {
        fFsw &= ~(X86_FSW_OE | X86_FSW_UE | X86_FSW_PE | X86_FSW_ZE | X86_FSW_C1);
        *pr80Result = *pr80XcptResult;
    }
    return fFsw;
}


/**
 * Helper doing polynomial evaluation using Horner's method.
 *
 * See https://en.wikipedia.org/wiki/Horner%27s_method for details.
 */
float128_t iemFpuSoftF128HornerPoly(float128_t z, PCRTFLOAT128U g_par128HornerConsts, size_t cHornerConsts,
                                    unsigned cPrecision, softfloat_state_t *pSoftState)
{
    Assert(cHornerConsts > 1);
    size_t     i          = cHornerConsts - 1;
    float128_t r128Result = iemFpuSoftF128PrecisionIprt(&g_par128HornerConsts[i], cPrecision);
    while (i-- > 0)
    {
        r128Result = iemFpuSoftF128Precision(f128_mul(r128Result, z, pSoftState), cPrecision);
        r128Result = f128_add(r128Result, iemFpuSoftF128PrecisionIprt(&g_par128HornerConsts[i], cPrecision), pSoftState);
        r128Result = iemFpuSoftF128Precision(r128Result, cPrecision);
    }
    return r128Result;
}

#endif /* !IEM_WITH_FLOAT128_FOR_FPU - SoftFloat */


/**
 * Composes a normalized and rounded RTFLOAT80U result from a 192 bit wide
 * mantissa, exponent and sign.
 *
 * @returns Updated FSW.
 * @param   pr80Dst     Where to return the composed value.
 * @param   fSign       The sign.
 * @param   puMantissa  The mantissa, 256-bit type but the to 64-bits are
 *                      ignored and should be zero.  This will probably be
 *                      modified during normalization and rounding.
 * @param   iExponent   Unbiased exponent.
 * @param   fFcw        The FPU control word.
 * @param   fFsw        The FPU status word.
 */
static uint16_t iemFpuFloat80RoundAndComposeFrom192(PRTFLOAT80U pr80Dst, bool fSign, PRTUINT256U puMantissa,
                                                    int32_t iExponent, uint16_t fFcw, uint16_t fFsw)
{
    AssertStmt(puMantissa->QWords.qw3 == 0, puMantissa->QWords.qw3 = 0);

    iExponent += RTFLOAT80U_EXP_BIAS;

    /* Do normalization if necessary and possible. */
    if (!(puMantissa->QWords.qw2 & RT_BIT_64(63)))
    {
        int cShift = 192 - RTUInt256BitCount(puMantissa);
        if (iExponent > cShift)
            iExponent -= cShift;
        else
        {
            if (fFcw & X86_FCW_UM)
            {
                if (iExponent > 0)
                    cShift = --iExponent;
                else
                    cShift = 0;
            }
            iExponent -= cShift;
        }
        RTUInt256AssignShiftLeft(puMantissa, cShift);
    }

    /* Do rounding. */
    uint64_t uMantissa = puMantissa->QWords.qw2;
    if (puMantissa->QWords.qw1 || puMantissa->QWords.qw0)
    {
        bool fAdd;
        switch (fFcw & X86_FCW_RC_MASK)
        {
            default: /* (for the simple-minded MSC which otherwise things fAdd would be used uninitialized) */
            case X86_FCW_RC_NEAREST:
                if (puMantissa->QWords.qw1 & RT_BIT_64(63))
                {
                    if (   (uMantissa & 1)
                        || puMantissa->QWords.qw0 != 0
                        || puMantissa->QWords.qw1 != RT_BIT_64(63))
                    {
                        fAdd = true;
                        break;
                    }
                    uMantissa &= ~(uint64_t)1;
                }
                fAdd = false;
                break;
            case X86_FCW_RC_ZERO:
                fAdd = false;
                break;
            case X86_FCW_RC_UP:
                fAdd = !fSign;
                break;
            case X86_FCW_RC_DOWN:
                fAdd = fSign;
                break;
        }
        if (fAdd)
        {
            uint64_t const uTmp = uMantissa;
            uMantissa = uTmp + 1;
            if (uMantissa < uTmp)
            {
                uMantissa >>= 1;
                uMantissa |= RT_BIT_64(63);
                iExponent++;
            }
            fFsw |= X86_FSW_C1;
        }
        fFsw |= X86_FSW_PE;
        if (!(fFcw & X86_FCW_PM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }

    /* Check for underflow (denormals). */
    if (iExponent <= 0)
    {
        if (fFcw & X86_FCW_UM)
        {
            if (uMantissa & RT_BIT_64(63))
                uMantissa >>= 1;
            iExponent = 0;
        }
        else
        {
            iExponent += RTFLOAT80U_EXP_BIAS_ADJUST;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_UE;
    }
    /* Check for overflow */
    else if (iExponent >= RTFLOAT80U_EXP_MAX)
    {
        Assert(iExponent < RTFLOAT80U_EXP_MAX);
    }

    /* Compose the result. */
    pr80Dst->s.uMantissa = uMantissa;
    pr80Dst->s.uExponent = iExponent;
    pr80Dst->s.fSign     = fSign;
    return fFsw;
}


/**
 * See also iemAImpl_fld_r80_from_r32
 */
static uint16_t iemAImplConvertR32ToR80(PCRTFLOAT32U pr32Val, PRTFLOAT80U pr80Dst)
{
    uint16_t fFsw = 0;
    if (RTFLOAT32U_IS_NORMAL(pr32Val))
    {
        pr80Dst->sj64.fSign     = pr32Val->s.fSign;
        pr80Dst->sj64.fInteger  = 1;
        pr80Dst->sj64.uFraction = (uint64_t)pr32Val->s.uFraction
                               << (RTFLOAT80U_FRACTION_BITS - RTFLOAT32U_FRACTION_BITS);
        pr80Dst->sj64.uExponent = pr32Val->s.uExponent - RTFLOAT32U_EXP_BIAS + RTFLOAT80U_EXP_BIAS;
        Assert(RTFLOAT80U_IS_NORMAL(pr80Dst));
    }
    else if (RTFLOAT32U_IS_ZERO(pr32Val))
    {
        pr80Dst->s.fSign     = pr32Val->s.fSign;
        pr80Dst->s.uExponent = 0;
        pr80Dst->s.uMantissa = 0;
        Assert(RTFLOAT80U_IS_ZERO(pr80Dst));
    }
    else if (RTFLOAT32U_IS_SUBNORMAL(pr32Val))
    {
        /* Subnormal -> normalized + X86_FSW_DE return. */
        pr80Dst->sj64.fSign     = pr32Val->s.fSign;
        pr80Dst->sj64.fInteger  = 1;
        unsigned const cExtraShift = RTFLOAT32U_FRACTION_BITS - ASMBitLastSetU32(pr32Val->s.uFraction);
        pr80Dst->sj64.uFraction = (uint64_t)pr32Val->s.uFraction
                               << (RTFLOAT80U_FRACTION_BITS - RTFLOAT32U_FRACTION_BITS + cExtraShift + 1);
        pr80Dst->sj64.uExponent = pr32Val->s.uExponent - RTFLOAT32U_EXP_BIAS + RTFLOAT80U_EXP_BIAS - cExtraShift;
        fFsw = X86_FSW_DE;
    }
    else if (RTFLOAT32U_IS_INF(pr32Val))
    {
        pr80Dst->s.fSign     = pr32Val->s.fSign;
        pr80Dst->s.uExponent = RTFLOAT80U_EXP_MAX;
        pr80Dst->s.uMantissa = RT_BIT_64(63);
        Assert(RTFLOAT80U_IS_INF(pr80Dst));
    }
    else
    {
        Assert(RTFLOAT32U_IS_NAN(pr32Val));
        pr80Dst->sj64.fSign     = pr32Val->s.fSign;
        pr80Dst->sj64.uExponent = RTFLOAT80U_EXP_MAX;
        pr80Dst->sj64.fInteger  = 1;
        pr80Dst->sj64.uFraction = (uint64_t)pr32Val->s.uFraction
                               << (RTFLOAT80U_FRACTION_BITS - RTFLOAT32U_FRACTION_BITS);
        Assert(RTFLOAT80U_IS_NAN(pr80Dst));
        Assert(RTFLOAT80U_IS_SIGNALLING_NAN(pr80Dst) == RTFLOAT32U_IS_SIGNALLING_NAN(pr32Val));
    }
    return fFsw;
}


/**
 * See also iemAImpl_fld_r80_from_r64
 */
static uint16_t iemAImplConvertR64ToR80(PCRTFLOAT64U pr64Val, PRTFLOAT80U pr80Dst)
{
    uint16_t fFsw = 0;
    if (RTFLOAT64U_IS_NORMAL(pr64Val))
    {
        pr80Dst->sj64.fSign     = pr64Val->s.fSign;
        pr80Dst->sj64.fInteger  = 1;
        pr80Dst->sj64.uFraction = pr64Val->s64.uFraction << (RTFLOAT80U_FRACTION_BITS - RTFLOAT64U_FRACTION_BITS);
        pr80Dst->sj64.uExponent = pr64Val->s.uExponent - RTFLOAT64U_EXP_BIAS + RTFLOAT80U_EXP_BIAS;
        Assert(RTFLOAT80U_IS_NORMAL(pr80Dst));
    }
    else if (RTFLOAT64U_IS_ZERO(pr64Val))
    {
        pr80Dst->s.fSign     = pr64Val->s.fSign;
        pr80Dst->s.uExponent = 0;
        pr80Dst->s.uMantissa = 0;
        Assert(RTFLOAT80U_IS_ZERO(pr80Dst));
    }
    else if (RTFLOAT64U_IS_SUBNORMAL(pr64Val))
    {
        /* Subnormal values gets normalized. */
        pr80Dst->sj64.fSign     = pr64Val->s.fSign;
        pr80Dst->sj64.fInteger  = 1;
        unsigned const cExtraShift = RTFLOAT64U_FRACTION_BITS - ASMBitLastSetU64(pr64Val->s64.uFraction);
        pr80Dst->sj64.uFraction = pr64Val->s64.uFraction
                               << (RTFLOAT80U_FRACTION_BITS - RTFLOAT64U_FRACTION_BITS + cExtraShift + 1);
        pr80Dst->sj64.uExponent = pr64Val->s.uExponent - RTFLOAT64U_EXP_BIAS + RTFLOAT80U_EXP_BIAS - cExtraShift;
        fFsw = X86_FSW_DE;
    }
    else if (RTFLOAT64U_IS_INF(pr64Val))
    {
        pr80Dst->s.fSign     = pr64Val->s.fSign;
        pr80Dst->s.uExponent = RTFLOAT80U_EXP_MAX;
        pr80Dst->s.uMantissa = RT_BIT_64(63);
        Assert(RTFLOAT80U_IS_INF(pr80Dst));
    }
    else
    {
        /* Signalling and quiet NaNs, both turn into quiet ones when loaded (weird). */
        Assert(RTFLOAT64U_IS_NAN(pr64Val));
        pr80Dst->sj64.fSign     = pr64Val->s.fSign;
        pr80Dst->sj64.uExponent = RTFLOAT80U_EXP_MAX;
        pr80Dst->sj64.fInteger  = 1;
        pr80Dst->sj64.uFraction = pr64Val->s64.uFraction << (RTFLOAT80U_FRACTION_BITS - RTFLOAT64U_FRACTION_BITS);
        Assert(RTFLOAT80U_IS_NAN(pr80Dst));
        Assert(RTFLOAT80U_IS_SIGNALLING_NAN(pr80Dst) == RTFLOAT64U_IS_SIGNALLING_NAN(pr64Val));
    }
    return fFsw;
}


/**
 * See also EMIT_FILD.
 */
#define EMIT_CONVERT_IXX_TO_R80(a_cBits) \
static PRTFLOAT80U iemAImplConvertI ## a_cBits ## ToR80(int ## a_cBits ## _t iVal, PRTFLOAT80U pr80Dst) \
{ \
    if (iVal == 0) \
    { \
        pr80Dst->s.fSign      = 0; \
        pr80Dst->s.uExponent  = 0; \
        pr80Dst->s.uMantissa  = 0; \
    } \
    else \
    { \
        if (iVal > 0) \
            pr80Dst->s.fSign  = 0; \
        else \
        { \
            pr80Dst->s.fSign  = 1; \
            iVal = -iVal; \
        } \
        unsigned const cBits = ASMBitLastSetU ## a_cBits((uint ## a_cBits ## _t)iVal); \
        pr80Dst->s.uExponent  = cBits - 1 + RTFLOAT80U_EXP_BIAS; \
        pr80Dst->s.uMantissa  = (uint64_t)iVal << (RTFLOAT80U_FRACTION_BITS + 1 - cBits); \
    } \
    return pr80Dst; \
}
EMIT_CONVERT_IXX_TO_R80(16)
EMIT_CONVERT_IXX_TO_R80(32)
//EMIT_CONVERT_IXX_TO_R80(64)

/** For implementing iemAImpl_fmul_r80_by_r64 and such. */
#define EMIT_R80_BY_R64(a_Name, a_fnR80ByR80, a_DenormalException) \
IEM_DECL_IMPL_DEF(void, a_Name,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val1, PCRTFLOAT64U pr64Val2)) \
{ \
    RTFLOAT80U r80Val2; \
    uint16_t   fFsw = iemAImplConvertR64ToR80(pr64Val2, &r80Val2); \
    Assert(!fFsw || fFsw == X86_FSW_DE); \
    if (fFsw) \
    { \
        if (RTFLOAT80U_IS_387_INVALID(pr80Val1) || RTFLOAT80U_IS_NAN(pr80Val1) || (a_DenormalException)) \
            fFsw = 0; \
        else if (!(pFpuState->FCW & X86_FCW_DM)) \
        { \
            pFpuRes->r80Result = *pr80Val1; \
            pFpuRes->FSW       = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (7 << X86_FSW_TOP_SHIFT) \
                               | X86_FSW_DE | X86_FSW_ES | X86_FSW_B; \
            return; \
        } \
    } \
    a_fnR80ByR80(pFpuState, pFpuRes, pr80Val1, &r80Val2); \
    pFpuRes->FSW = (pFpuRes->FSW & ~X86_FSW_TOP_MASK) | (7 << X86_FSW_TOP_SHIFT) | fFsw; \
}

/** For implementing iemAImpl_fmul_r80_by_r32 and such. */
#define EMIT_R80_BY_R32(a_Name, a_fnR80ByR80, a_DenormalException) \
IEM_DECL_IMPL_DEF(void, a_Name,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val1, PCRTFLOAT32U pr32Val2)) \
{ \
    RTFLOAT80U r80Val2; \
    uint16_t   fFsw = iemAImplConvertR32ToR80(pr32Val2, &r80Val2); \
    Assert(!fFsw || fFsw == X86_FSW_DE); \
    if (fFsw) \
    { \
        if (RTFLOAT80U_IS_387_INVALID(pr80Val1) || RTFLOAT80U_IS_NAN(pr80Val1) || (a_DenormalException)) \
            fFsw = 0; \
        else if (!(pFpuState->FCW & X86_FCW_DM)) \
        { \
            pFpuRes->r80Result = *pr80Val1; \
            pFpuRes->FSW       = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (7 << X86_FSW_TOP_SHIFT) \
                               | X86_FSW_DE | X86_FSW_ES | X86_FSW_B; \
            return; \
        } \
    } \
    a_fnR80ByR80(pFpuState, pFpuRes, pr80Val1, &r80Val2); \
    pFpuRes->FSW = (pFpuRes->FSW & ~X86_FSW_TOP_MASK) | (7 << X86_FSW_TOP_SHIFT) | fFsw; \
}

/** For implementing iemAImpl_fimul_r80_by_i32 and such. */
#define EMIT_R80_BY_I32(a_Name, a_fnR80ByR80) \
IEM_DECL_IMPL_DEF(void, a_Name,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val1, int32_t const *pi32Val2)) \
{ \
    RTFLOAT80U r80Val2; \
    a_fnR80ByR80(pFpuState, pFpuRes, pr80Val1, iemAImplConvertI32ToR80(*pi32Val2, &r80Val2)); \
    pFpuRes->FSW = (pFpuRes->FSW & ~X86_FSW_TOP_MASK) | (7 << X86_FSW_TOP_SHIFT); \
}

/** For implementing iemAImpl_fimul_r80_by_i16 and such. */
#define EMIT_R80_BY_I16(a_Name, a_fnR80ByR80) \
IEM_DECL_IMPL_DEF(void, a_Name,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val1, int16_t const *pi16Val2)) \
{ \
    RTFLOAT80U r80Val2; \
    a_fnR80ByR80(pFpuState, pFpuRes, pr80Val1, iemAImplConvertI16ToR80(*pi16Val2, &r80Val2)); \
    pFpuRes->FSW = (pFpuRes->FSW & ~X86_FSW_TOP_MASK) | (7 << X86_FSW_TOP_SHIFT);  \
}



/*********************************************************************************************************************************
*   x86 FPU Division Operations                                                                                                  *
*********************************************************************************************************************************/

/** Worker for iemAImpl_fdiv_r80_by_r80 & iemAImpl_fdivr_r80_by_r80. */
static uint16_t iemAImpl_fdiv_f80_r80_worker(PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2, PRTFLOAT80U pr80Result,
                                             uint16_t fFcw, uint16_t fFsw, PCRTFLOAT80U pr80Val1Org)
{
    if (!RTFLOAT80U_IS_ZERO(pr80Val2) || RTFLOAT80U_IS_NAN(pr80Val1) || RTFLOAT80U_IS_INF(pr80Val1))
    {
        softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_FCW(fFcw);
        extFloat80_t r80XResult = extF80_div(iemFpuSoftF80FromIprt(pr80Val1), iemFpuSoftF80FromIprt(pr80Val2), &SoftState);
        return iemFpuSoftStateAndF80ToFswAndIprtResult(&SoftState, r80XResult, pr80Result, fFcw, fFsw, pr80Val1Org);
    }
    if (!RTFLOAT80U_IS_ZERO(pr80Val1))
    {   /* Div by zero. */
        if (fFcw & X86_FCW_ZM)
            *pr80Result = g_ar80Infinity[pr80Val1->s.fSign != pr80Val2->s.fSign];
        else
        {
            *pr80Result = *pr80Val1Org;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_ZE;
    }
    else
    {   /* Invalid operand */
        if (fFcw & X86_FCW_IM)
            *pr80Result = g_r80Indefinite;
        else
        {
            *pr80Result = *pr80Val1Org;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_IE;
    }
    return fFsw;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fdiv_r80_by_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                  PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (6 << X86_FSW_TOP_SHIFT);

    /* SoftFloat does not check for Pseudo-Infinity, Pseudo-Nan and Unnormals. */
    if (RTFLOAT80U_IS_387_INVALID(pr80Val1) || RTFLOAT80U_IS_387_INVALID(pr80Val2))
    {
        if (fFcw & X86_FCW_IM)
            pFpuRes->r80Result = g_r80Indefinite;
        else
        {
            pFpuRes->r80Result = *pr80Val1;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_IE;
    }
    /* SoftFloat does not check for denormals and certainly not report them to us. NaNs & /0 trumps denormals. */
    else if (   (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val1) && !RTFLOAT80U_IS_NAN(pr80Val2) && !RTFLOAT80U_IS_ZERO(pr80Val2))
             || (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val2) && !RTFLOAT80U_IS_NAN(pr80Val1)) )
    {
        if (fFcw & X86_FCW_DM)
        {
            PCRTFLOAT80U const pr80Val1Org = pr80Val1;
            IEM_NORMALIZE_PSEUDO_DENORMAL(pr80Val1, r80Val1Normalized);
            IEM_NORMALIZE_PSEUDO_DENORMAL(pr80Val2, r80Val2Normalized);
            fFsw = iemAImpl_fdiv_f80_r80_worker(pr80Val1, pr80Val2, &pFpuRes->r80Result, fFcw, fFsw, pr80Val1Org);
        }
        else
        {
            pFpuRes->r80Result = *pr80Val1;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_DE;
    }
    /* SoftFloat can handle the rest: */
    else
        fFsw = iemAImpl_fdiv_f80_r80_worker(pr80Val1, pr80Val2, &pFpuRes->r80Result, fFcw, fFsw, pr80Val1);

    pFpuRes->FSW = fFsw;
}


EMIT_R80_BY_R64(iemAImpl_fdiv_r80_by_r64,  iemAImpl_fdiv_r80_by_r80, 0)
EMIT_R80_BY_R32(iemAImpl_fdiv_r80_by_r32,  iemAImpl_fdiv_r80_by_r80, 0)
EMIT_R80_BY_I32(iemAImpl_fidiv_r80_by_i32, iemAImpl_fdiv_r80_by_r80)
EMIT_R80_BY_I16(iemAImpl_fidiv_r80_by_i16, iemAImpl_fdiv_r80_by_r80)


IEM_DECL_IMPL_DEF(void, iemAImpl_fdivr_r80_by_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                   PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (6 << X86_FSW_TOP_SHIFT);

    /* SoftFloat does not check for Pseudo-Infinity, Pseudo-Nan and Unnormals. */
    if (RTFLOAT80U_IS_387_INVALID(pr80Val1) || RTFLOAT80U_IS_387_INVALID(pr80Val2))
    {
        if (fFcw & X86_FCW_IM)
            pFpuRes->r80Result = g_r80Indefinite;
        else
        {
            pFpuRes->r80Result = *pr80Val1;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_IE;
    }
    /* SoftFloat does not check for denormals and certainly not report them to us. NaNs & /0 trumps denormals. */
    else if (   (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val1) && !RTFLOAT80U_IS_NAN(pr80Val2))
             || (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val2) && !RTFLOAT80U_IS_NAN(pr80Val1) && !RTFLOAT80U_IS_ZERO(pr80Val1)) )
    {
        if (fFcw & X86_FCW_DM)
        {
            PCRTFLOAT80U const pr80Val1Org = pr80Val1;
            IEM_NORMALIZE_PSEUDO_DENORMAL(pr80Val1, r80Val1Normalized);
            IEM_NORMALIZE_PSEUDO_DENORMAL(pr80Val2, r80Val2Normalized);
            fFsw = iemAImpl_fdiv_f80_r80_worker(pr80Val2, pr80Val1, &pFpuRes->r80Result, fFcw, fFsw, pr80Val1Org);
        }
        else
        {
            pFpuRes->r80Result = *pr80Val1;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_DE;
    }
    /* SoftFloat can handle the rest: */
    else
        fFsw = iemAImpl_fdiv_f80_r80_worker(pr80Val2, pr80Val1, &pFpuRes->r80Result, fFcw, fFsw, pr80Val1);

    pFpuRes->FSW = fFsw;
}


EMIT_R80_BY_R64(iemAImpl_fdivr_r80_by_r64,  iemAImpl_fdivr_r80_by_r80, RTFLOAT80U_IS_ZERO(pr80Val1))
EMIT_R80_BY_R32(iemAImpl_fdivr_r80_by_r32,  iemAImpl_fdivr_r80_by_r80, RTFLOAT80U_IS_ZERO(pr80Val1))
EMIT_R80_BY_I32(iemAImpl_fidivr_r80_by_i32, iemAImpl_fdivr_r80_by_r80)
EMIT_R80_BY_I16(iemAImpl_fidivr_r80_by_i16, iemAImpl_fdivr_r80_by_r80)


/** Worker for iemAImpl_fprem_r80_by_r80 & iemAImpl_fprem1_r80_by_r80. */
static uint16_t iemAImpl_fprem_fprem1_r80_by_r80_worker(PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2, PRTFLOAT80U pr80Result,
                                                        uint16_t fFcw, uint16_t fFsw, PCRTFLOAT80U pr80Val1Org, bool fLegacyInstr)
{
    if (!RTFLOAT80U_IS_ZERO(pr80Val2) || RTFLOAT80U_IS_NAN(pr80Val1) || RTFLOAT80U_IS_INF(pr80Val1))
    {
        softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_FCW(fFcw);
        uint16_t          fCxFlags  = 0;
        extFloat80_t r80XResult = extF80_partialRem(iemFpuSoftF80FromIprt(pr80Val1), iemFpuSoftF80FromIprt(pr80Val2),
                                                    fLegacyInstr ? softfloat_round_minMag : softfloat_round_near_even,
                                                    &fCxFlags, &SoftState);
        Assert(!(fCxFlags & ~X86_FSW_C_MASK));
        fFsw = iemFpuSoftStateAndF80ToFswAndIprtResult(&SoftState, r80XResult, pr80Result, fFcw, fFsw, pr80Val1Org);
        if (   !(fFsw & X86_FSW_IE)
            && !RTFLOAT80U_IS_NAN(pr80Result)
            && !RTFLOAT80U_IS_INDEFINITE(pr80Result))
        {
            fFsw &= ~(uint16_t)X86_FSW_C_MASK;
            fFsw |= fCxFlags & X86_FSW_C_MASK;
        }
        return fFsw;
    }

    /* Invalid operand */
    if (fFcw & X86_FCW_IM)
        *pr80Result = g_r80Indefinite;
    else
    {
        *pr80Result = *pr80Val1Org;
        fFsw |= X86_FSW_ES | X86_FSW_B;
    }
    return fFsw | X86_FSW_IE;
}


static void iemAImpl_fprem_fprem1_r80_by_r80(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                             PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2, bool fLegacyInstr)
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 /*| X86_FSW_C2*/ | X86_FSW_C3)) | (6 << X86_FSW_TOP_SHIFT);

    /* SoftFloat does not check for Pseudo-Infinity, Pseudo-Nan and Unnormals.
       In addition, we'd like to handle zero ST(1) now as SoftFloat returns Inf instead
       of Indefinite.  (Note! There is no #Z like the footnotes to tables 3-31 and 3-32
       for the FPREM1 & FPREM1 instructions in the intel reference manual claims!) */
    if (   RTFLOAT80U_IS_387_INVALID(pr80Val1) || RTFLOAT80U_IS_387_INVALID(pr80Val2)
        || (RTFLOAT80U_IS_ZERO(pr80Val2) && !RTFLOAT80U_IS_NAN(pr80Val1) && !RTFLOAT80U_IS_INDEFINITE(pr80Val1)))
    {
        if (fFcw & X86_FCW_IM)
            pFpuRes->r80Result = g_r80Indefinite;
        else
        {
            pFpuRes->r80Result = *pr80Val1;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_IE;
    }
    /* SoftFloat does not check for denormals and certainly not report them to us. NaNs & /0 trumps denormals. */
    else if (   (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val1) && !RTFLOAT80U_IS_NAN(pr80Val2) && !RTFLOAT80U_IS_ZERO(pr80Val2))
             || (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val2) && !RTFLOAT80U_IS_NAN(pr80Val1) && !RTFLOAT80U_IS_INF(pr80Val1)) )
    {
        if (fFcw & X86_FCW_DM)
        {
            PCRTFLOAT80U const pr80Val1Org = pr80Val1;
            IEM_NORMALIZE_PSEUDO_DENORMAL(pr80Val1, r80Val1Normalized);
            IEM_NORMALIZE_PSEUDO_DENORMAL(pr80Val2, r80Val2Normalized);
            fFsw = iemAImpl_fprem_fprem1_r80_by_r80_worker(pr80Val1, pr80Val2, &pFpuRes->r80Result, fFcw, fFsw,
                                                           pr80Val1Org, fLegacyInstr);
        }
        else
        {
            pFpuRes->r80Result = *pr80Val1;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_DE;
    }
    /* SoftFloat can handle the rest: */
    else
        fFsw = iemAImpl_fprem_fprem1_r80_by_r80_worker(pr80Val1, pr80Val2, &pFpuRes->r80Result, fFcw, fFsw,
                                                       pr80Val1, fLegacyInstr);

    pFpuRes->FSW = fFsw;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fprem_r80_by_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                   PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    iemAImpl_fprem_fprem1_r80_by_r80(pFpuState, pFpuRes, pr80Val1, pr80Val2, true /*fLegacyInstr*/);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fprem1_r80_by_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                    PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    iemAImpl_fprem_fprem1_r80_by_r80(pFpuState, pFpuRes, pr80Val1, pr80Val2, false /*fLegacyInstr*/);
}


/*********************************************************************************************************************************
*   x87 FPU Multiplication Operations                                                                                            *
*********************************************************************************************************************************/

/** Worker for iemAImpl_fmul_r80_by_r80. */
static uint16_t iemAImpl_fmul_f80_r80_worker(PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2, PRTFLOAT80U pr80Result,
                                             uint16_t fFcw, uint16_t fFsw, PCRTFLOAT80U pr80Val1Org)
{
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_FCW(fFcw);
    extFloat80_t r80XResult = extF80_mul(iemFpuSoftF80FromIprt(pr80Val1), iemFpuSoftF80FromIprt(pr80Val2), &SoftState);
    return iemFpuSoftStateAndF80ToFswAndIprtResult(&SoftState, r80XResult, pr80Result, fFcw, fFsw, pr80Val1Org);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fmul_r80_by_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                  PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (6 << X86_FSW_TOP_SHIFT);

    /* SoftFloat does not check for Pseudo-Infinity, Pseudo-Nan and Unnormals. */
    if (RTFLOAT80U_IS_387_INVALID(pr80Val1) || RTFLOAT80U_IS_387_INVALID(pr80Val2))
    {
        if (fFcw & X86_FCW_IM)
            pFpuRes->r80Result = g_r80Indefinite;
        else
        {
            pFpuRes->r80Result = *pr80Val1;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_IE;
    }
    /* SoftFloat does not check for denormals and certainly not report them to us. NaNs trumps denormals. */
    else if (   (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val1) && !RTFLOAT80U_IS_NAN(pr80Val2))
             || (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val2) && !RTFLOAT80U_IS_NAN(pr80Val1)) )
    {
        if (fFcw & X86_FCW_DM)
        {
            PCRTFLOAT80U const pr80Val1Org = pr80Val1;
            IEM_NORMALIZE_PSEUDO_DENORMAL(pr80Val1, r80Val1Normalized);
            IEM_NORMALIZE_PSEUDO_DENORMAL(pr80Val2, r80Val2Normalized);
            fFsw = iemAImpl_fmul_f80_r80_worker(pr80Val1, pr80Val2, &pFpuRes->r80Result, fFcw, fFsw, pr80Val1Org);
        }
        else
        {
            pFpuRes->r80Result = *pr80Val1;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_DE;
    }
    /* SoftFloat can handle the rest: */
    else
        fFsw = iemAImpl_fmul_f80_r80_worker(pr80Val1, pr80Val2, &pFpuRes->r80Result, fFcw, fFsw, pr80Val1);

    pFpuRes->FSW = fFsw;
}


EMIT_R80_BY_R64(iemAImpl_fmul_r80_by_r64,  iemAImpl_fmul_r80_by_r80, 0)
EMIT_R80_BY_R32(iemAImpl_fmul_r80_by_r32,  iemAImpl_fmul_r80_by_r80, 0)
EMIT_R80_BY_I32(iemAImpl_fimul_r80_by_i32, iemAImpl_fmul_r80_by_r80)
EMIT_R80_BY_I16(iemAImpl_fimul_r80_by_i16, iemAImpl_fmul_r80_by_r80)


/*********************************************************************************************************************************
*   x87 FPU Addition                                                                                                             *
*********************************************************************************************************************************/

/** Worker for iemAImpl_fadd_r80_by_r80. */
static uint16_t iemAImpl_fadd_f80_r80_worker(PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2, PRTFLOAT80U pr80Result,
                                             uint16_t fFcw, uint16_t fFsw, PCRTFLOAT80U pr80Val1Org)
{
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_FCW(fFcw);
    extFloat80_t r80XResult = extF80_add(iemFpuSoftF80FromIprt(pr80Val1), iemFpuSoftF80FromIprt(pr80Val2), &SoftState);
    return iemFpuSoftStateAndF80ToFswAndIprtResult(&SoftState, r80XResult, pr80Result, fFcw, fFsw, pr80Val1Org);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fadd_r80_by_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                  PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (6 << X86_FSW_TOP_SHIFT);

    /* SoftFloat does not check for Pseudo-Infinity, Pseudo-Nan and Unnormals. */
    if (RTFLOAT80U_IS_387_INVALID(pr80Val1) || RTFLOAT80U_IS_387_INVALID(pr80Val2))
    {
        if (fFcw & X86_FCW_IM)
            pFpuRes->r80Result = g_r80Indefinite;
        else
        {
            pFpuRes->r80Result = *pr80Val1;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_IE;
    }
    /* SoftFloat does not check for denormals and certainly not report them to us. NaNs trumps denormals. */
    else if (   (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val1) && !RTFLOAT80U_IS_NAN(pr80Val2))
             || (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val2) && !RTFLOAT80U_IS_NAN(pr80Val1)) )
    {
        if (fFcw & X86_FCW_DM)
        {
            PCRTFLOAT80U const pr80Val1Org = pr80Val1;
            IEM_NORMALIZE_PSEUDO_DENORMAL(pr80Val1, r80Val1Normalized);
            IEM_NORMALIZE_PSEUDO_DENORMAL(pr80Val2, r80Val2Normalized);
            fFsw = iemAImpl_fadd_f80_r80_worker(pr80Val1, pr80Val2, &pFpuRes->r80Result, fFcw, fFsw, pr80Val1Org);
        }
        else
        {
            pFpuRes->r80Result = *pr80Val1;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_DE;
    }
    /* SoftFloat can handle the rest: */
    else
        fFsw = iemAImpl_fadd_f80_r80_worker(pr80Val1, pr80Val2, &pFpuRes->r80Result, fFcw, fFsw, pr80Val1);

    pFpuRes->FSW = fFsw;
}


EMIT_R80_BY_R64(iemAImpl_fadd_r80_by_r64,  iemAImpl_fadd_r80_by_r80, 0)
EMIT_R80_BY_R32(iemAImpl_fadd_r80_by_r32,  iemAImpl_fadd_r80_by_r80, 0)
EMIT_R80_BY_I32(iemAImpl_fiadd_r80_by_i32, iemAImpl_fadd_r80_by_r80)
EMIT_R80_BY_I16(iemAImpl_fiadd_r80_by_i16, iemAImpl_fadd_r80_by_r80)


/*********************************************************************************************************************************
*   x87 FPU Subtraction                                                                                                          *
*********************************************************************************************************************************/

/** Worker for iemAImpl_fsub_r80_by_r80 and iemAImpl_fsubr_r80_by_r80. */
static uint16_t iemAImpl_fsub_f80_r80_worker(PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2, PRTFLOAT80U pr80Result,
                                             uint16_t fFcw, uint16_t fFsw, PCRTFLOAT80U pr80Val1Org)
{
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_FCW(fFcw);
    extFloat80_t r80XResult = extF80_sub(iemFpuSoftF80FromIprt(pr80Val1), iemFpuSoftF80FromIprt(pr80Val2), &SoftState);
    return iemFpuSoftStateAndF80ToFswAndIprtResult(&SoftState, r80XResult, pr80Result, fFcw, fFsw, pr80Val1Org);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fsub_r80_by_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                  PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (6 << X86_FSW_TOP_SHIFT);

    /* SoftFloat does not check for Pseudo-Infinity, Pseudo-Nan and Unnormals. */
    if (RTFLOAT80U_IS_387_INVALID(pr80Val1) || RTFLOAT80U_IS_387_INVALID(pr80Val2))
    {
        if (fFcw & X86_FCW_IM)
            pFpuRes->r80Result = g_r80Indefinite;
        else
        {
            pFpuRes->r80Result = *pr80Val1;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_IE;
    }
    /* SoftFloat does not check for denormals and certainly not report them to us. NaNs trumps denormals. */
    else if (   (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val1) && !RTFLOAT80U_IS_NAN(pr80Val2))
             || (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val2) && !RTFLOAT80U_IS_NAN(pr80Val1)) )
    {
        if (fFcw & X86_FCW_DM)
        {
            PCRTFLOAT80U const pr80Val1Org = pr80Val1;
            IEM_NORMALIZE_PSEUDO_DENORMAL(pr80Val1, r80Val1Normalized);
            IEM_NORMALIZE_PSEUDO_DENORMAL(pr80Val2, r80Val2Normalized);
            fFsw = iemAImpl_fsub_f80_r80_worker(pr80Val1, pr80Val2, &pFpuRes->r80Result, fFcw, fFsw, pr80Val1Org);
        }
        else
        {
            pFpuRes->r80Result = *pr80Val1;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_DE;
    }
    /* SoftFloat can handle the rest: */
    else
        fFsw = iemAImpl_fsub_f80_r80_worker(pr80Val1, pr80Val2, &pFpuRes->r80Result, fFcw, fFsw, pr80Val1);

    pFpuRes->FSW = fFsw;
}


EMIT_R80_BY_R64(iemAImpl_fsub_r80_by_r64,  iemAImpl_fsub_r80_by_r80, 0)
EMIT_R80_BY_R32(iemAImpl_fsub_r80_by_r32,  iemAImpl_fsub_r80_by_r80, 0)
EMIT_R80_BY_I32(iemAImpl_fisub_r80_by_i32, iemAImpl_fsub_r80_by_r80)
EMIT_R80_BY_I16(iemAImpl_fisub_r80_by_i16, iemAImpl_fsub_r80_by_r80)


/* Same as iemAImpl_fsub_r80_by_r80, but with input operands switched.  */
IEM_DECL_IMPL_DEF(void, iemAImpl_fsubr_r80_by_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                   PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (6 << X86_FSW_TOP_SHIFT);

    /* SoftFloat does not check for Pseudo-Infinity, Pseudo-Nan and Unnormals. */
    if (RTFLOAT80U_IS_387_INVALID(pr80Val1) || RTFLOAT80U_IS_387_INVALID(pr80Val2))
    {
        if (fFcw & X86_FCW_IM)
            pFpuRes->r80Result = g_r80Indefinite;
        else
        {
            pFpuRes->r80Result = *pr80Val1;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_IE;
    }
    /* SoftFloat does not check for denormals and certainly not report them to us. NaNs trumps denormals. */
    else if (   (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val1) && !RTFLOAT80U_IS_NAN(pr80Val2))
             || (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val2) && !RTFLOAT80U_IS_NAN(pr80Val1)) )
    {
        if (fFcw & X86_FCW_DM)
        {
            PCRTFLOAT80U const pr80Val1Org = pr80Val1;
            IEM_NORMALIZE_PSEUDO_DENORMAL(pr80Val1, r80Val1Normalized);
            IEM_NORMALIZE_PSEUDO_DENORMAL(pr80Val2, r80Val2Normalized);
            fFsw = iemAImpl_fsub_f80_r80_worker(pr80Val2, pr80Val1, &pFpuRes->r80Result, fFcw, fFsw, pr80Val1Org);
        }
        else
        {
            pFpuRes->r80Result = *pr80Val1;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_DE;
    }
    /* SoftFloat can handle the rest: */
    else
        fFsw = iemAImpl_fsub_f80_r80_worker(pr80Val2, pr80Val1, &pFpuRes->r80Result, fFcw, fFsw, pr80Val1);

    pFpuRes->FSW = fFsw;
}


EMIT_R80_BY_R64(iemAImpl_fsubr_r80_by_r64,  iemAImpl_fsubr_r80_by_r80, 0)
EMIT_R80_BY_R32(iemAImpl_fsubr_r80_by_r32,  iemAImpl_fsubr_r80_by_r80, 0)
EMIT_R80_BY_I32(iemAImpl_fisubr_r80_by_i32, iemAImpl_fsubr_r80_by_r80)
EMIT_R80_BY_I16(iemAImpl_fisubr_r80_by_i16, iemAImpl_fsubr_r80_by_r80)


/*********************************************************************************************************************************
*   x87 FPU Trigometric Operations                                                                                               *
*********************************************************************************************************************************/
static uint16_t iemAImpl_fpatan_r80_by_r80_normal(PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2, PIEMFPURESULT pFpuRes, uint16_t fFcw, uint16_t fFsw)
{
    softfloat_state_t SoftState = SOFTFLOAT_STATE_INIT_DEFAULTS();
    extFloat80_t y = iemFpuSoftF80FromIprt(pr80Val1);
    extFloat80_t x = iemFpuSoftF80FromIprt(pr80Val2);
    extFloat80_t v;
    (void)fFcw;

    v = extF80_atan2(y, x, &SoftState);

    iemFpuSoftF80ToIprt(&pFpuRes->r80Result, v);
    return fFsw;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_fpatan_r80_by_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                    PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3);

    if (RTFLOAT80U_IS_NORMAL(pr80Val1) && RTFLOAT80U_IS_NORMAL(pr80Val2))
    {
        fFsw = iemAImpl_fpatan_r80_by_r80_normal(pr80Val1, pr80Val2, pFpuRes, fFcw, fFsw);

        fFsw |= X86_FSW_PE | (7 << X86_FSW_TOP_SHIFT);
        if (!(fFcw & X86_FCW_PM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }
    else
    {
        fFsw |= X86_FSW_IE;
        if (!(fFcw & X86_FCW_IM))
        {
            pFpuRes->r80Result = *pr80Val2;
            fFsw |= X86_FSW_ES | X86_FSW_B | (6 << X86_FSW_TOP_SHIFT);
        }
        else
        {
            pFpuRes->r80Result = g_r80Indefinite;
            fFsw |= (7 << X86_FSW_TOP_SHIFT);
        }
    }

    pFpuRes->FSW = fFsw;
}
#endif /* IEM_WITHOUT_ASSEMBLY */

IEM_DECL_IMPL_DEF(void, iemAImpl_fpatan_r80_by_r80_intel,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                          PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    iemAImpl_fpatan_r80_by_r80(pFpuState, pFpuRes, pr80Val1, pr80Val2);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_fpatan_r80_by_r80_amd,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                        PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    iemAImpl_fpatan_r80_by_r80(pFpuState, pFpuRes, pr80Val1, pr80Val2);
}


#if defined(IEM_WITHOUT_ASSEMBLY)
static uint16_t iemAImpl_fptan_r80_r80_normal(PIEMFPURESULTTWO pFpuResTwo, PCRTFLOAT80U pr80Val, uint16_t fFcw, uint16_t fFsw)
{
    softfloat_state_t SoftState = SOFTFLOAT_STATE_INIT_DEFAULTS();
    extFloat80_t x = iemFpuSoftF80FromIprt(pr80Val);
    extFloat80_t v;
    (void)fFcw;

    v = extF80_tan(x, &SoftState);

    iemFpuSoftF80ToIprt(&pFpuResTwo->r80Result1, v);
    return fFsw;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_fptan_r80_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULTTWO pFpuResTwo, PCRTFLOAT80U pr80Val))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | /*X86_FSW_C2 |*/ X86_FSW_C3)) | (6 << X86_FSW_TOP_SHIFT);

    if (RTFLOAT80U_IS_ZERO(pr80Val))
    {
        pFpuResTwo->r80Result1 = *pr80Val;
        pFpuResTwo->r80Result2 = g_ar80One[0];
    }
    else if (RTFLOAT80U_IS_NORMAL(pr80Val))
    {
        if (pr80Val->s.uExponent >= RTFLOAT80U_EXP_BIAS + 63)
        {
            fFsw |= X86_FSW_C2 | (7 << X86_FSW_TOP_SHIFT);
            pFpuResTwo->r80Result1 = *pr80Val;
        }
        else
        {
            if (pr80Val->s.uExponent <= RTFLOAT80U_EXP_BIAS - 63)
            {
                pFpuResTwo->r80Result1 = *pr80Val;
            }
            else
            {
                fFsw = iemAImpl_fptan_r80_r80_normal(pFpuResTwo, pr80Val, fFcw, fFsw);
            }

            pFpuResTwo->r80Result2 = g_ar80One[0];

            fFsw |= X86_FSW_PE;
            if (!(fFcw & X86_FCW_PM))
                fFsw |= X86_FSW_ES | X86_FSW_B;
        }
    }
    else
    {
        fFsw |= X86_FSW_IE;
        if (!(fFcw & X86_FCW_IM))
            fFsw |= X86_FSW_ES | X86_FSW_B | (7 << X86_FSW_TOP_SHIFT);
    }

    pFpuResTwo->FSW = fFsw;
}
#endif /* IEM_WITHOUT_ASSEMBLY */

IEM_DECL_IMPL_DEF(void, iemAImpl_fptan_r80_r80_amd,(PCX86FXSTATE pFpuState, PIEMFPURESULTTWO pFpuResTwo, PCRTFLOAT80U pr80Val))
{
    iemAImpl_fptan_r80_r80(pFpuState, pFpuResTwo, pr80Val);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_fptan_r80_r80_intel,(PCX86FXSTATE pFpuState, PIEMFPURESULTTWO pFpuResTwo, PCRTFLOAT80U pr80Val))
{
    iemAImpl_fptan_r80_r80(pFpuState, pFpuResTwo, pr80Val);
}

#ifdef IEM_WITHOUT_ASSEMBLY

static uint16_t iemAImpl_fsin_r80_normal(PCRTFLOAT80U pr80Val, PRTFLOAT80U pr80Result, uint16_t fFcw, uint16_t fFsw)
{
    softfloat_state_t SoftState = SOFTFLOAT_STATE_INIT_DEFAULTS();
    extFloat80_t x = iemFpuSoftF80FromIprt(pr80Val);
    extFloat80_t v;
    (void)fFcw;

    v = extF80_sin(x, &SoftState);

    iemFpuSoftF80ToIprt(pr80Result, v);

    return fFsw;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_fsin_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | /*X86_FSW_C2 |*/ X86_FSW_C3)) | (7 << X86_FSW_TOP_SHIFT);

    if (RTFLOAT80U_IS_ZERO(pr80Val))
    {
        pFpuRes->r80Result = *pr80Val;
    }
    else if (RTFLOAT80U_IS_NORMAL(pr80Val))
    {
        if (pr80Val->s.uExponent >= RTFLOAT80U_EXP_BIAS + 63)
        {
            fFsw |= X86_FSW_C2;
            pFpuRes->r80Result = *pr80Val;
        }
        else
        {
            if (pr80Val->s.uExponent <= RTFLOAT80U_EXP_BIAS - 63)
            {
                pFpuRes->r80Result = *pr80Val;
            }
            else
            {
                fFsw = iemAImpl_fsin_r80_normal(pr80Val, &pFpuRes->r80Result, fFcw, fFsw);
            }
            fFsw |= X86_FSW_PE;
            if (!(fFcw & X86_FCW_PM))
                fFsw |= X86_FSW_ES | X86_FSW_B;
        }
    }
    else if (RTFLOAT80U_IS_INF(pr80Val))
    {
        fFsw |= X86_FSW_IE;
        if (!(fFcw & X86_FCW_IM))
        {
            fFsw |= X86_FSW_ES | X86_FSW_B;
            pFpuRes->r80Result = *pr80Val;
        }
        else
        {
            pFpuRes->r80Result = g_r80Indefinite;
        }
    }
    else if (RTFLOAT80U_IS_DENORMAL(pr80Val))
    {
        fFsw |= X86_FSW_DE;

        if (fFcw & X86_FCW_DM)
        {
            if (fFcw & X86_FCW_UM)
            {
                pFpuRes->r80Result = *pr80Val;
            }
            else
            {
                /* Underflow signalling as described at 7.4 section of 1985 IEEE 754*/
                uint64_t uMantissa = pr80Val->s.uMantissa;
                uint32_t uExponent = ASMBitLastSetU64(uMantissa);

                uExponent = 64 - uExponent;
                uMantissa <<= uExponent;
                uExponent = RTFLOAT128U_EXP_BIAS_ADJUST - uExponent + 1;

                pFpuRes->r80Result.s.fSign = pr80Val->s.fSign;
                pFpuRes->r80Result.s.uMantissa = uMantissa;
                pFpuRes->r80Result.s.uExponent = uExponent;
            }

            fFsw |= X86_FSW_UE | X86_FSW_PE;

            if ((fFcw & X86_FCW_UM) && (fFcw & X86_FCW_PM))
            {
                /* All the exceptions are masked. */
            }
            else
            {
                fFsw |= X86_FSW_ES | X86_FSW_B;
            }
        }
        else
        {
            pFpuRes->r80Result = *pr80Val;

            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
    }
    else if (RTFLOAT80U_IS_PSEUDO_DENORMAL(pr80Val))
    {
        pFpuRes->r80Result = *pr80Val;
        fFsw |= X86_FSW_DE;

        if (fFcw & X86_FCW_DM)
        {
            if (fFcw & X86_FCW_PM)
            {
                fFsw |= X86_FSW_PE;
            }
            else
            {
                fFsw |= X86_FSW_ES | X86_FSW_B | X86_FSW_PE;
            }

            pFpuRes->r80Result.sj64.uExponent = 1;
        }
        else
        {
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
    } else if (   RTFLOAT80U_IS_QUIET_NAN(pr80Val)
             || RTFLOAT80U_IS_INDEFINITE(pr80Val))
    {
        pFpuRes->r80Result = *pr80Val;
    } else {
        if (   (   RTFLOAT80U_IS_UNNORMAL(pr80Val)
                || RTFLOAT80U_IS_PSEUDO_NAN(pr80Val))
            && (fFcw & X86_FCW_IM))
            pFpuRes->r80Result = g_r80Indefinite;
        else
        {
            pFpuRes->r80Result = *pr80Val;
            if (RTFLOAT80U_IS_SIGNALLING_NAN(pr80Val) && (fFcw & X86_FCW_IM))
                pFpuRes->r80Result.s.uMantissa |= RT_BIT_64(62); /* make it quiet */
        }

        fFsw |= X86_FSW_IE;
        if (!(fFcw & X86_FCW_IM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }

    pFpuRes->FSW = fFsw;
}
#endif /* IEM_WITHOUT_ASSEMBLY */

IEM_DECL_IMPL_DEF(void, iemAImpl_fsin_r80_amd,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val))
{
    iemAImpl_fsin_r80(pFpuState, pFpuRes, pr80Val);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_fsin_r80_intel,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val))
{
    iemAImpl_fsin_r80(pFpuState, pFpuRes, pr80Val);
}

#ifdef IEM_WITHOUT_ASSEMBLY

static uint16_t iemAImpl_fcos_r80_normal(PCRTFLOAT80U pr80Val, PRTFLOAT80U pr80Result, uint16_t fFcw, uint16_t fFsw)
{
    softfloat_state_t SoftState = SOFTFLOAT_STATE_INIT_DEFAULTS();
    extFloat80_t x = iemFpuSoftF80FromIprt(pr80Val);
    extFloat80_t v;
    (void)fFcw;

    v = extF80_cos(x, &SoftState);

    iemFpuSoftF80ToIprt(pr80Result, v);

    return fFsw;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_fcos_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | /*X86_FSW_C2 |*/ X86_FSW_C3)) | (7 << X86_FSW_TOP_SHIFT);

    if (RTFLOAT80U_IS_ZERO(pr80Val))
    {
        pFpuRes->r80Result = g_ar80One[0];
    }
    else if (RTFLOAT80U_IS_NORMAL(pr80Val))
    {
        if (pr80Val->s.uExponent >= RTFLOAT80U_EXP_BIAS + 63)
        {
            fFsw |= X86_FSW_C2;
            pFpuRes->r80Result = *pr80Val;
        }
        else
        {
            if (pr80Val->s.uExponent <= RTFLOAT80U_EXP_BIAS - 63)
            {
                pFpuRes->r80Result = g_ar80One[0];

            }
            else
            {
                fFsw = iemAImpl_fcos_r80_normal(pr80Val, &pFpuRes->r80Result, fFcw, fFsw);
                fFsw |= X86_FSW_C1; // TBD: If the inexact result was rounded up (C1 is set) or “not rounded up” (C1 is cleared).
            }
            fFsw |= X86_FSW_PE;
            if (!(fFcw & X86_FCW_PM))
                fFsw |= X86_FSW_ES | X86_FSW_B;
        }
    }
    else if (RTFLOAT80U_IS_INF(pr80Val))
    {
        fFsw |= X86_FSW_IE;
        if (!(fFcw & X86_FCW_IM))
        {
            fFsw |= X86_FSW_ES | X86_FSW_B;
            pFpuRes->r80Result = *pr80Val;
        }
        else
        {
            pFpuRes->r80Result = g_r80Indefinite;
        }
    }
    else if (RTFLOAT80U_IS_DENORMAL(pr80Val) || RTFLOAT80U_IS_PSEUDO_DENORMAL(pr80Val))
    {
        fFsw |= X86_FSW_DE;

        if (fFcw & X86_FCW_DM)
        {
            pFpuRes->r80Result = g_ar80One[0];

            if (fFcw & X86_FCW_PM)
            {
                fFsw |= X86_FSW_PE;
            }
            else
            {
                fFsw |= X86_FSW_PE | X86_FSW_ES | X86_FSW_B;
            }
        }
        else
        {
            pFpuRes->r80Result = *pr80Val;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
    } else if (   RTFLOAT80U_IS_QUIET_NAN(pr80Val)
             || RTFLOAT80U_IS_INDEFINITE(pr80Val))
    {
        pFpuRes->r80Result = *pr80Val;
    } else {
        if (   (   RTFLOAT80U_IS_UNNORMAL(pr80Val)
                || RTFLOAT80U_IS_PSEUDO_NAN(pr80Val))
            && (fFcw & X86_FCW_IM))
            pFpuRes->r80Result = g_r80Indefinite;
        else
        {
            pFpuRes->r80Result = *pr80Val;
            if (RTFLOAT80U_IS_SIGNALLING_NAN(pr80Val) && (fFcw & X86_FCW_IM))
                pFpuRes->r80Result.s.uMantissa |= RT_BIT_64(62); /* make it quiet */
        }

        fFsw |= X86_FSW_IE;
        if (!(fFcw & X86_FCW_IM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }

    pFpuRes->FSW = fFsw;
}
#endif /* IEM_WITHOUT_ASSEMBLY */

IEM_DECL_IMPL_DEF(void, iemAImpl_fcos_r80_amd,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val))
{
    iemAImpl_fcos_r80(pFpuState, pFpuRes, pr80Val);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_fcos_r80_intel,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val))
{
    iemAImpl_fcos_r80(pFpuState, pFpuRes, pr80Val);
}

#ifdef IEM_WITHOUT_ASSEMBLY

static uint16_t iemAImpl_fsincos_r80_r80_normal(PIEMFPURESULTTWO pFpuResTwo, PCRTFLOAT80U pr80Val, uint16_t fFcw, uint16_t fFsw)
{
    softfloat_state_t SoftState = SOFTFLOAT_STATE_INIT_DEFAULTS();
    extFloat80_t x = iemFpuSoftF80FromIprt(pr80Val);
    extFloat80_t r80Sin, r80Cos;
    (void)fFcw;

    extF80_sincos(x, &r80Sin, &r80Cos, &SoftState);

    iemFpuSoftF80ToIprt(&pFpuResTwo->r80Result1, r80Sin);
    iemFpuSoftF80ToIprt(&pFpuResTwo->r80Result2, r80Cos);

    return fFsw;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_fsincos_r80_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULTTWO pFpuResTwo, PCRTFLOAT80U pr80Val))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | /*X86_FSW_C2 |*/ X86_FSW_C3)) | (7 << X86_FSW_TOP_SHIFT);

    if (RTFLOAT80U_IS_ZERO(pr80Val))
    {
        pFpuResTwo->r80Result1 = *pr80Val;
        pFpuResTwo->r80Result2 = g_ar80One[0];
        fFsw &= ~X86_FSW_TOP_MASK | (6 << X86_FSW_TOP_SHIFT);
    }
    else if (RTFLOAT80U_IS_NORMAL(pr80Val))
    {
        if (pr80Val->s.uExponent >= RTFLOAT80U_EXP_BIAS + 63)
        {
            fFsw |= X86_FSW_C2;

            if (fFcw & X86_FCW_IM)
            {
                pFpuResTwo->r80Result1 = g_r80Indefinite;
            }
            else
            {
                pFpuResTwo->r80Result1 = g_ar80Zero[0];
            }

            pFpuResTwo->r80Result2 = *pr80Val;
        }
        else
        {
            fFsw &= ~X86_FSW_TOP_MASK | (6 << X86_FSW_TOP_SHIFT);

            if (pr80Val->s.uExponent <= RTFLOAT80U_EXP_BIAS - 63)
            {
                pFpuResTwo->r80Result1 = *pr80Val;
                pFpuResTwo->r80Result2 = g_ar80One[0];
            }
            else
            {
                fFsw = iemAImpl_fsincos_r80_r80_normal(pFpuResTwo, pr80Val, fFcw, fFsw);
                fFsw |= X86_FSW_C1; // TBD: If the inexact result was rounded up (C1 is set) or “not rounded up” (C1 is cleared).
            }
            fFsw |= X86_FSW_PE;
            if (!(fFcw & X86_FCW_PM))
                fFsw |= X86_FSW_ES | X86_FSW_B;
        }
    }
    else if (RTFLOAT80U_IS_PSEUDO_DENORMAL(pr80Val))
    {
        fFsw |= X86_FSW_DE;

        if (fFcw & X86_FCW_DM)
        {
            pFpuResTwo->r80Result1 = *pr80Val;
            pFpuResTwo->r80Result2 = g_ar80One[0];
            fFsw &= ~X86_FSW_TOP_MASK | (6 << X86_FSW_TOP_SHIFT);

            if (fFcw & X86_FCW_PM)
            {
                fFsw |= X86_FSW_PE;
            }
            else
            {
                fFsw |= X86_FSW_PE | X86_FSW_ES | X86_FSW_B;
            }

            pFpuResTwo->r80Result1.sj64.uExponent = 1;
        }
        else
        {
            pFpuResTwo->r80Result1 = g_ar80Zero[0];
            pFpuResTwo->r80Result2 = *pr80Val;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
    }
    else if (RTFLOAT80U_IS_DENORMAL(pr80Val))
    {
        fFsw |= X86_FSW_DE;

        if (fFcw & X86_FCW_DM)
        {
            pFpuResTwo->r80Result2 = g_ar80One[0];

            if (fFcw & X86_FCW_UM)
            {
                pFpuResTwo->r80Result1 = *pr80Val;
            }
            else
            {
                /* Underflow signalling as described at 7.4 section of 1985 IEEE 754*/
                uint64_t uMantissa = pr80Val->s.uMantissa;
                uint32_t uExponent = ASMBitLastSetU64(uMantissa);

                uExponent = 64 - uExponent;
                uMantissa <<= uExponent;
                uExponent = RTFLOAT128U_EXP_BIAS_ADJUST - uExponent + 1;

                pFpuResTwo->r80Result1.s.fSign = pr80Val->s.fSign;
                pFpuResTwo->r80Result1.s.uMantissa = uMantissa;
                pFpuResTwo->r80Result1.s.uExponent = uExponent;
            }

            fFsw &= ~X86_FSW_TOP_MASK | (6 << X86_FSW_TOP_SHIFT);
            fFsw |= X86_FSW_UE | X86_FSW_PE;

            if ((fFcw & X86_FCW_UM) && (fFcw & X86_FCW_PM))
            {
                /* All the exceptions are masked. */
            }
            else
            {
                fFsw |= X86_FSW_ES | X86_FSW_B;
            }
        }
        else
        {
            pFpuResTwo->r80Result1 = g_ar80Zero[0];
            pFpuResTwo->r80Result2 = *pr80Val;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
    }
    else if (RTFLOAT80U_IS_QUIET_NAN(pr80Val) || RTFLOAT80U_IS_INDEFINITE(pr80Val))
    {
        pFpuResTwo->r80Result1 = *pr80Val;
        pFpuResTwo->r80Result2 = *pr80Val;
        fFsw &= ~X86_FSW_TOP_MASK | (6 << X86_FSW_TOP_SHIFT);
    }
    else if (RTFLOAT80U_IS_UNNORMAL(pr80Val) || RTFLOAT80U_IS_PSEUDO_NAN(pr80Val))
    {
        if (fFcw & X86_FCW_IM)
        {
            pFpuResTwo->r80Result1 = g_r80Indefinite;
            pFpuResTwo->r80Result2 = g_r80Indefinite;
            fFsw &= ~X86_FSW_TOP_MASK | (6 << X86_FSW_TOP_SHIFT);
        }
        else
        {
            pFpuResTwo->r80Result1 = g_ar80Zero[0];
            pFpuResTwo->r80Result2 = *pr80Val;
        }

        fFsw |= X86_FSW_IE;
        if (!(fFcw & X86_FCW_IM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }
    else if (RTFLOAT80U_IS_SIGNALLING_NAN(pr80Val))
    {
        pFpuResTwo->r80Result1 = *pr80Val;
        pFpuResTwo->r80Result2 = *pr80Val;

        if (fFcw & X86_FCW_IM)
        {
            pFpuResTwo->r80Result1.s.uMantissa |= RT_BIT_64(62); /* make it quiet */
            pFpuResTwo->r80Result2.s.uMantissa |= RT_BIT_64(62);
            fFsw &= ~X86_FSW_TOP_MASK | (6 << X86_FSW_TOP_SHIFT);
        }
        else
        {
            pFpuResTwo->r80Result1 = g_ar80Zero[0];
            pFpuResTwo->r80Result2 = *pr80Val;
        }

        fFsw |= X86_FSW_IE;
        if (!(fFcw & X86_FCW_IM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }
    else if (RTFLOAT80U_IS_INF(pr80Val))
    {
        if (fFcw & X86_FCW_IM)
        {
            pFpuResTwo->r80Result1 = g_r80Indefinite;
            pFpuResTwo->r80Result2 = g_r80Indefinite;
            fFsw &= ~X86_FSW_TOP_MASK | (6 << X86_FSW_TOP_SHIFT);
        }
        else
        {
            pFpuResTwo->r80Result1 = g_ar80Zero[0];
            pFpuResTwo->r80Result2 = *pr80Val;
        }

        fFsw |= X86_FSW_IE;
        if (!(fFcw & X86_FCW_IM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }

    pFpuResTwo->FSW = fFsw;
}
#endif /* IEM_WITHOUT_ASSEMBLY */

IEM_DECL_IMPL_DEF(void, iemAImpl_fsincos_r80_r80_amd,(PCX86FXSTATE pFpuState, PIEMFPURESULTTWO pFpuResTwo, PCRTFLOAT80U pr80Val))
{
    iemAImpl_fsincos_r80_r80(pFpuState, pFpuResTwo, pr80Val);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_fsincos_r80_r80_intel,(PCX86FXSTATE pFpuState, PIEMFPURESULTTWO pFpuResTwo, PCRTFLOAT80U pr80Val))
{
    iemAImpl_fsincos_r80_r80(pFpuState, pFpuResTwo, pr80Val);
}

#ifdef IEM_WITHOUT_ASSEMBLY


/*********************************************************************************************************************************
*   x87 FPU Compare and Testing Operations                                                                                       *
*********************************************************************************************************************************/

IEM_DECL_IMPL_DEF(void, iemAImpl_ftst_r80,(PCX86FXSTATE pFpuState, uint16_t *pu16Fsw, PCRTFLOAT80U pr80Val))
{
    uint16_t fFsw = (7 << X86_FSW_TOP_SHIFT);

    if (RTFLOAT80U_IS_ZERO(pr80Val))
        fFsw |= X86_FSW_C3;
    else if (RTFLOAT80U_IS_NORMAL(pr80Val) || RTFLOAT80U_IS_INF(pr80Val))
        fFsw |= pr80Val->s.fSign ? X86_FSW_C0 : 0;
    else if (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val))
    {
        fFsw |= pr80Val->s.fSign ? X86_FSW_C0 | X86_FSW_DE : X86_FSW_DE;
        if (!(pFpuState->FCW & X86_FCW_DM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }
    else
    {
        fFsw |= X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3 | X86_FSW_IE;
        if (!(pFpuState->FCW & X86_FCW_IM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }

    *pu16Fsw = fFsw;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fxam_r80,(PCX86FXSTATE pFpuState, uint16_t *pu16Fsw, PCRTFLOAT80U pr80Val))
{
    RT_NOREF(pFpuState);
    uint16_t fFsw = (7 << X86_FSW_TOP_SHIFT);

    /* C1 = sign bit (always, even if empty Intel says). */
    if (pr80Val->s.fSign)
        fFsw |= X86_FSW_C1;

    /* Classify the value in C0, C2, C3. */
    if (!(pFpuState->FTW & RT_BIT_32(X86_FSW_TOP_GET(pFpuState->FSW))))
        fFsw |= X86_FSW_C0 | X86_FSW_C3; /* empty */
    else if (RTFLOAT80U_IS_NORMAL(pr80Val))
        fFsw |= X86_FSW_C2;
    else if (RTFLOAT80U_IS_ZERO(pr80Val))
        fFsw |= X86_FSW_C3;
    else if (RTFLOAT80U_IS_QUIET_OR_SIGNALLING_NAN(pr80Val))
        fFsw |= X86_FSW_C0;
    else if (RTFLOAT80U_IS_INF(pr80Val))
        fFsw |= X86_FSW_C0 | X86_FSW_C2;
    else if (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val))
        fFsw |= X86_FSW_C2 | X86_FSW_C3;
    /* whatever else: 0 */

    *pu16Fsw = fFsw;
}


/**
 * Worker for fcom, fucom, and friends.
 */
static uint16_t iemAImpl_fcom_r80_by_r80_worker(PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2,
                                                uint16_t fFcw, uint16_t fFsw, bool fIeOnAllNaNs)
{
    /*
     * Unpack the values.
     */
    bool const fSign1      = pr80Val1->s.fSign;
    int32_t    iExponent1  = pr80Val1->s.uExponent;
    uint64_t   uMantissa1  = pr80Val1->s.uMantissa;

    bool const fSign2      = pr80Val2->s.fSign;
    int32_t    iExponent2  = pr80Val2->s.uExponent;
    uint64_t   uMantissa2  = pr80Val2->s.uMantissa;

    /*
     * Check for invalid inputs.
     */
    if (   RTFLOAT80U_IS_387_INVALID_EX(uMantissa1, iExponent1)
        || RTFLOAT80U_IS_387_INVALID_EX(uMantissa2, iExponent2))
    {
         if (!(fFcw & X86_FCW_IM))
             fFsw |= X86_FSW_ES | X86_FSW_B;
         return fFsw | X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3 | X86_FSW_IE;
    }

    /*
     * Check for NaNs and indefinites, they are all unordered and trumps #DE.
     */
    if (   RTFLOAT80U_IS_INDEFINITE_OR_QUIET_OR_SIGNALLING_NAN_EX(uMantissa1, iExponent1)
        || RTFLOAT80U_IS_INDEFINITE_OR_QUIET_OR_SIGNALLING_NAN_EX(uMantissa2, iExponent2))
    {
        if (   fIeOnAllNaNs
            || RTFLOAT80U_IS_SIGNALLING_NAN_EX(uMantissa1, iExponent1)
            || RTFLOAT80U_IS_SIGNALLING_NAN_EX(uMantissa2, iExponent2))
        {
            fFsw |= X86_FSW_IE;
            if (!(fFcw & X86_FCW_IM))
                fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        return fFsw | X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3;
    }

    /*
     * Normalize the values.
     */
    if (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL_EX(uMantissa1, iExponent1))
    {
        if (RTFLOAT80U_IS_PSEUDO_DENORMAL_EX(uMantissa1, iExponent1))
            iExponent1 = 1;
        else
        {
            iExponent1 = 64 - ASMBitLastSetU64(uMantissa1);
            uMantissa1 <<= iExponent1;
            iExponent1 = 1 - iExponent1;
        }
        fFsw |= X86_FSW_DE;
        if (!(fFcw & X86_FCW_DM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }

    if (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL_EX(uMantissa2, iExponent2))
    {
        if (RTFLOAT80U_IS_PSEUDO_DENORMAL_EX(uMantissa2, iExponent2))
            iExponent2 = 1;
        else
        {
            iExponent2 = 64 - ASMBitLastSetU64(uMantissa2);
            uMantissa2 <<= iExponent2;
            iExponent2 = 1 - iExponent2;
        }
        fFsw |= X86_FSW_DE;
        if (!(fFcw & X86_FCW_DM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }

    /*
     * Test if equal (val1 == val2):
     */
    if (   uMantissa1 == uMantissa2
        && iExponent1 == iExponent2
        && (   fSign1 == fSign2
            || (uMantissa1 == 0 && iExponent1 == 0) /* ignore sign for zero */ ) )
        fFsw |= X86_FSW_C3;
    /*
     * Test if less than (val1 < val2):
     */
    else if (fSign1 && !fSign2)
        fFsw |= X86_FSW_C0;
    else if (fSign1 == fSign2)
    {
        /* Zeros are problematic, however at the most one can be zero here. */
        if (RTFLOAT80U_IS_ZERO_EX(uMantissa1, iExponent1))
            return !fSign1 ? fFsw | X86_FSW_C0 : fFsw;
        if (RTFLOAT80U_IS_ZERO_EX(uMantissa2, iExponent2))
            return fSign1  ? fFsw | X86_FSW_C0 : fFsw;

        if (  fSign1
            ^ (   iExponent1 < iExponent2
               || (   iExponent1 == iExponent2
                   && uMantissa1 < uMantissa2 ) ) )
        fFsw |= X86_FSW_C0;
    }
    /* else: No flags set if greater. */

    return fFsw;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fcom_r80_by_r80,(PCX86FXSTATE pFpuState, uint16_t *pfFsw,
                                                  PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    *pfFsw = iemAImpl_fcom_r80_by_r80_worker(pr80Val1, pr80Val2, pFpuState->FCW, 6 << X86_FSW_TOP_SHIFT, true /*fIeOnAllNaNs*/);
}




IEM_DECL_IMPL_DEF(void, iemAImpl_fucom_r80_by_r80,(PCX86FXSTATE pFpuState, uint16_t *pfFsw,
                                                   PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    *pfFsw = iemAImpl_fcom_r80_by_r80_worker(pr80Val1, pr80Val2, pFpuState->FCW, 6 << X86_FSW_TOP_SHIFT, false /*fIeOnAllNaNs*/);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fcom_r80_by_r64,(PCX86FXSTATE pFpuState, uint16_t *pfFsw,
                                                  PCRTFLOAT80U pr80Val1, PCRTFLOAT64U pr64Val2))
{
    RTFLOAT80U r80Val2;
    uint16_t   fFsw = iemAImplConvertR64ToR80(pr64Val2, &r80Val2);
    Assert(!fFsw || fFsw == X86_FSW_DE);
    *pfFsw = iemAImpl_fcom_r80_by_r80_worker(pr80Val1, &r80Val2, pFpuState->FCW, 7 << X86_FSW_TOP_SHIFT, true /*fIeOnAllNaNs*/);
    if (fFsw != 0 && !(*pfFsw & X86_FSW_IE))
    {
        if (!(pFpuState->FCW & X86_FCW_DM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
        *pfFsw |= fFsw;
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fcom_r80_by_r32,(PCX86FXSTATE pFpuState, uint16_t *pfFsw,
                                                  PCRTFLOAT80U pr80Val1, PCRTFLOAT32U pr32Val2))
{
    RTFLOAT80U r80Val2;
    uint16_t   fFsw = iemAImplConvertR32ToR80(pr32Val2, &r80Val2);
    Assert(!fFsw || fFsw == X86_FSW_DE);
    *pfFsw = iemAImpl_fcom_r80_by_r80_worker(pr80Val1, &r80Val2, pFpuState->FCW, 7 << X86_FSW_TOP_SHIFT, true /*fIeOnAllNaNs*/);
    if (fFsw != 0 && !(*pfFsw & X86_FSW_IE))
    {
        if (!(pFpuState->FCW & X86_FCW_DM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
        *pfFsw |= fFsw;
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_ficom_r80_by_i32,(PCX86FXSTATE pFpuState, uint16_t *pfFsw,
                                                   PCRTFLOAT80U pr80Val1, int32_t const *pi32Val2))
{
    RTFLOAT80U r80Val2;
    iemAImpl_fcom_r80_by_r80(pFpuState, pfFsw, pr80Val1, iemAImplConvertI32ToR80(*pi32Val2, &r80Val2));
    *pfFsw = (*pfFsw & ~X86_FSW_TOP_MASK) | (7 << X86_FSW_TOP_SHIFT);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_ficom_r80_by_i16,(PCX86FXSTATE pFpuState, uint16_t *pfFsw,
                                                   PCRTFLOAT80U pr80Val1, int16_t const *pi16Val2))
{
    RTFLOAT80U r80Val2;
    iemAImpl_fcom_r80_by_r80(pFpuState, pfFsw, pr80Val1, iemAImplConvertI16ToR80(*pi16Val2, &r80Val2));
    *pfFsw = (*pfFsw & ~X86_FSW_TOP_MASK) | (7 << X86_FSW_TOP_SHIFT);
}


/**
 * Worker for fcomi & fucomi.
 */
static uint32_t iemAImpl_fcomi_r80_by_r80_worker(PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2,
                                                 uint16_t fFcw, uint16_t fFswIn, bool fIeOnAllNaNs, uint16_t *pfFsw)
{
    uint16_t fFsw    = iemAImpl_fcom_r80_by_r80_worker(pr80Val1, pr80Val2, fFcw, 6 << X86_FSW_TOP_SHIFT, fIeOnAllNaNs);
    uint32_t fEflags = ((fFsw & X86_FSW_C3) >> (X86_FSW_C3_BIT - X86_EFL_ZF_BIT))
                     | ((fFsw & X86_FSW_C2) >> (X86_FSW_C2_BIT - X86_EFL_PF_BIT))
                     | ((fFsw & X86_FSW_C0) >> (X86_FSW_C0_BIT - X86_EFL_CF_BIT));

    /* Note! C1 is not cleared as per docs! Everything is preserved. */
    *pfFsw = (fFsw & ~X86_FSW_C_MASK) | (fFswIn & X86_FSW_C_MASK);
    return fEflags | X86_EFL_IF | X86_EFL_RA1_MASK;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_fcomi_r80_by_r80,(PCX86FXSTATE pFpuState, uint16_t *pfFsw,
                                                       PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    return iemAImpl_fcomi_r80_by_r80_worker(pr80Val1, pr80Val2, pFpuState->FCW, pFpuState->FSW, true /*fIeOnAllNaNs*/, pfFsw);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_fucomi_r80_by_r80,(PCX86FXSTATE pFpuState, uint16_t *pfFsw,
                                                        PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    return iemAImpl_fcomi_r80_by_r80_worker(pr80Val1, pr80Val2, pFpuState->FCW, pFpuState->FSW, false /*fIeOnAllNaNs*/, pfFsw);
}


/*********************************************************************************************************************************
*   x87 FPU Other Operations                                                                                                     *
*********************************************************************************************************************************/

/**
 * Helper for iemAImpl_frndint_r80, called both on normal and denormal numbers.
 */
static uint16_t iemAImpl_frndint_r80_normal(PCRTFLOAT80U pr80Val, PRTFLOAT80U pr80Result, uint16_t fFcw, uint16_t fFsw)
{
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_FCW(fFcw);
    iemFpuSoftF80ToIprt(pr80Result, extF80_roundToInt(iemFpuSoftF80FromIprt(pr80Val), SoftState.roundingMode,
                                                      true /*exact / generate #PE */, &SoftState));
    return IEM_SOFTFLOAT_STATE_TO_FSW(fFsw, &SoftState, fFcw);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_frndint_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (7 << X86_FSW_TOP_SHIFT);

    if (RTFLOAT80U_IS_NORMAL(pr80Val))
        fFsw = iemAImpl_frndint_r80_normal(pr80Val, &pFpuRes->r80Result, fFcw, fFsw);
    else if (   RTFLOAT80U_IS_ZERO(pr80Val)
             || RTFLOAT80U_IS_QUIET_NAN(pr80Val)
             || RTFLOAT80U_IS_INDEFINITE(pr80Val)
             || RTFLOAT80U_IS_INF(pr80Val))
        pFpuRes->r80Result = *pr80Val;
    else if (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val))
    {
        fFsw |= X86_FSW_DE;
        if (fFcw & X86_FCW_DM)
            fFsw = iemAImpl_frndint_r80_normal(pr80Val, &pFpuRes->r80Result, fFcw, fFsw);
        else
        {
            pFpuRes->r80Result = *pr80Val;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
    }
    else
    {
        if (fFcw & X86_FCW_IM)
        {
            if (!RTFLOAT80U_IS_SIGNALLING_NAN(pr80Val))
                pFpuRes->r80Result = g_r80Indefinite;
            else
            {
                pFpuRes->r80Result = *pr80Val;
                pFpuRes->r80Result.s.uMantissa |= RT_BIT_64(62); /* make it quiet */
            }
        }
        else
        {
            pFpuRes->r80Result = *pr80Val;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_IE;
    }
    pFpuRes->FSW = fFsw;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fscale_r80_by_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                    PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    /* The SoftFloat worker function extF80_scale_extF80 is of our creation, so
       it does everything we need it to do. */
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (6 << X86_FSW_TOP_SHIFT);
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_FCW(fFcw);
    extFloat80_t r80XResult = extF80_scale_extF80(iemFpuSoftF80FromIprt(pr80Val1), iemFpuSoftF80FromIprt(pr80Val2), &SoftState);
    pFpuRes->FSW = iemFpuSoftStateAndF80ToFswAndIprtResult(&SoftState, r80XResult, &pFpuRes->r80Result, fFcw, fFsw, pr80Val1);
}


/**
 * Helper for iemAImpl_fsqrt_r80, called both on normal and denormal numbers.
 */
static uint16_t iemAImpl_fsqrt_r80_normal(PCRTFLOAT80U pr80Val, PRTFLOAT80U pr80Result, uint16_t fFcw, uint16_t fFsw)
{
    Assert(!pr80Val->s.fSign);
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_FCW(fFcw);
    iemFpuSoftF80ToIprt(pr80Result, extF80_sqrt(iemFpuSoftF80FromIprt(pr80Val), &SoftState));
    return IEM_SOFTFLOAT_STATE_TO_FSW(fFsw, &SoftState, fFcw);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fsqrt_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (7 << X86_FSW_TOP_SHIFT);

    if (RTFLOAT80U_IS_NORMAL(pr80Val) && !pr80Val->s.fSign)
        fFsw = iemAImpl_fsqrt_r80_normal(pr80Val, &pFpuRes->r80Result, fFcw, fFsw);
    else if (   RTFLOAT80U_IS_ZERO(pr80Val)
             || RTFLOAT80U_IS_QUIET_NAN(pr80Val)
             || RTFLOAT80U_IS_INDEFINITE(pr80Val)
             || (RTFLOAT80U_IS_INF(pr80Val) && !pr80Val->s.fSign))
        pFpuRes->r80Result = *pr80Val;
    else if (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val) && !pr80Val->s.fSign) /* Negative denormals only generate #IE! */
    {
        fFsw |= X86_FSW_DE;
        if (fFcw & X86_FCW_DM)
            fFsw = iemAImpl_fsqrt_r80_normal(pr80Val, &pFpuRes->r80Result, fFcw, fFsw);
        else
        {
            pFpuRes->r80Result = *pr80Val;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
    }
    else
    {
        if (fFcw & X86_FCW_IM)
        {
            if (!RTFLOAT80U_IS_SIGNALLING_NAN(pr80Val))
                pFpuRes->r80Result = g_r80Indefinite;
            else
            {
                pFpuRes->r80Result = *pr80Val;
                pFpuRes->r80Result.s.uMantissa |= RT_BIT_64(62); /* make it quiet */
            }
        }
        else
        {
            pFpuRes->r80Result = *pr80Val;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
        fFsw |= X86_FSW_IE;
    }
    pFpuRes->FSW = fFsw;
}


/**
 * @code{.unparsed}
 *          x          x * ln2
 * f(x) = 2   - 1  =  e         - 1
 *
 * @endcode
 *
 * We can approximate e^x by a Taylor/Maclaurin series (see
 * https://en.wikipedia.org/wiki/Taylor_series#Exponential_function):
 * @code{.unparsed}
 *        n        0     1     2     3     4
 *  inf  x        x     x     x     x     x
 *  SUM ----- =  --- + --- + --- + --- + --- + ...
 *  n=0   n!      0!    1!    2!    3!    4!
 *
 *                              2     3     4
 *                             x     x     x
 *            =   1  +  x  +  --- + --- + --- + ...
 *                             2!    3!    4!
 * @endcode
 *
 * Given z = x * ln2, we get:
 * @code{.unparsed}
 *                      2     3     4           n
 *    z                z     z     z           z
 *  e   - 1  =  z  +  --- + --- + --- + ... + ---
 *                     2!    3!    4!          n!
 * @endcode
 *
 * Wanting to use Horner's method, we move one z outside and get:
 * @code{.unparsed}
 *                            2     3           (n-1)
 *                     z     z     z           z
 *       =  z ( 1  +  --- + --- + --- + ... + -------  )
 *                     2!    3!    4!          n!
 * @endcode
 *
 * The constants we need for using Horner's methods are 1 and 1 / n!.
 *
 * For very tiny x values, we can get away with f(x) = x * ln 2, because
 * because we don't have the necessary precision to represent 1.0 + z/3 + ...
 * and can approximate it to be 1.0.  For a visual demonstration of this
 * check out https://www.desmos.com/calculator/vidcdxizd9 (for as long
 * as it valid), plotting f(x) = 2^x - 1 and f(x) = x * ln2.
 *
 *
 * As constant accuracy goes, figure 0.1 "80387 Block Diagram" in the "80387
 * Data Sheet" (order 231920-002; Appendix E in 80387 PRM 231917-001; Military
 * i387SX 271166-002), indicates that constants are 67-bit (constant rom block)
 * and the internal mantissa size is 68-bit (mantissa adder & barrel shifter
 * blocks).  (The one bit difference is probably an implicit one missing from
 * the constant ROM.)  A paper on division and sqrt on the AMD-K7 by Stuart F.
 * Oberman states that it internally used a 68 bit mantissa with a 18-bit
 * exponent.
 *
 * However, even when sticking to 67 constants / 68 mantissas, I have not yet
 * successfully reproduced the exact results from an Intel 10980XE, there is
 * always a portition of rounding differences.  Not going to spend too much time
 * on getting this 100% the same, at least not now.
 *
 * P.S. If someone are really curious about 8087 and its contstants:
 * http://www.righto.com/2020/05/extracting-rom-constants-from-8087-math.html
 *
 *
 * @param   pr80Val     The exponent value (x), less than 1.0, greater than
 *                      -1.0 and not zero. This can be a normal, denormal
 *                      or pseudo-denormal value.
 * @param   pr80Result  Where to return the result.
 * @param   fFcw        FPU control word.
 * @param   fFsw        FPU status word.
 */
static uint16_t iemAImpl_f2xm1_r80_normal(PCRTFLOAT80U pr80Val, PRTFLOAT80U pr80Result, uint16_t fFcw, uint16_t fFsw)
{
    /* As mentioned above, we can skip the expensive polynomial calculation
       as it will be close enough to 1.0 that it makes no difference.

       The cutoff point for intel 10980XE is exponents >= -69.  Intel
       also seems to be using a 67-bit or 68-bit constant value, and we get
       a smattering of rounding differences if we go for higher precision. */
    if (pr80Val->s.uExponent <= RTFLOAT80U_EXP_BIAS - 69)
    {
        RTUINT256U u256;
        RTUInt128MulByU64Ex(&u256, &g_u128Ln2MantissaIntel, pr80Val->s.uMantissa);
        u256.QWords.qw0 |= 1; /* force #PE */
        fFsw = iemFpuFloat80RoundAndComposeFrom192(pr80Result, pr80Val->s.fSign, &u256,
                                                   !RTFLOAT80U_IS_PSEUDO_DENORMAL(pr80Val) && !RTFLOAT80U_IS_DENORMAL(pr80Val)
                                                   ? (int32_t)pr80Val->s.uExponent - RTFLOAT80U_EXP_BIAS
                                                   : 1 - RTFLOAT80U_EXP_BIAS,
                                                   fFcw, fFsw);
    }
    else
    {
#ifdef IEM_WITH_FLOAT128_FOR_FPU
        /* This approach is not good enough for small values, we end up with zero. */
        int const fOldRounding = iemFpuF128SetRounding(fFcw);
        _Float128 rd128Val     = iemFpuF128FromFloat80(pr80Val, fFcw);
        _Float128 rd128Result  = powf128(2.0L, rd128Val);
        rd128Result -= 1.0L;
        fFsw = iemFpuF128ToFloat80(pr80Result, rd128Result, fFcw, fFsw);
        iemFpuF128RestoreRounding(fOldRounding);

# else
        softfloat_state_t SoftState = SOFTFLOAT_STATE_INIT_DEFAULTS();
        float128_t const x = iemFpuSoftF128FromFloat80(pr80Val);

        /* As mentioned above, enforce 68-bit internal mantissa width to better
           match the Intel 10980XE results. */
        unsigned const cPrecision = 68;

        /* first calculate z = x * ln2 */
        float128_t z = iemFpuSoftF128Precision(f128_mul(x, iemFpuSoftF128PrecisionIprt(&g_r128Ln2, cPrecision), &SoftState),
                                               cPrecision);

        /* Then do the polynomial evaluation. */
        float128_t r = iemFpuSoftF128HornerPoly(z, g_ar128F2xm1HornerConsts, RT_ELEMENTS(g_ar128F2xm1HornerConsts),
                                                cPrecision, &SoftState);
        r = f128_mul(z, r, &SoftState);

        /* Output the result. */
        fFsw = iemFpuSoftF128ToFloat80(pr80Result, r, fFcw, fFsw);
# endif
    }
    return fFsw;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_f2xm1_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (7 << X86_FSW_TOP_SHIFT);

    if (RTFLOAT80U_IS_NORMAL(pr80Val))
    {
        if (pr80Val->s.uExponent < RTFLOAT80U_EXP_BIAS)
            fFsw = iemAImpl_f2xm1_r80_normal(pr80Val, &pFpuRes->r80Result, fFcw, fFsw);
        else
        {
            /* Special case:
                   2^+1.0 - 1.0 = 1.0
                   2^-1.0 - 1.0 = -0.5 */
            if (   pr80Val->s.uExponent == RTFLOAT80U_EXP_BIAS
                && pr80Val->s.uMantissa == RT_BIT_64(63))
            {
                pFpuRes->r80Result.s.uMantissa = RT_BIT_64(63);
                pFpuRes->r80Result.s.uExponent = RTFLOAT80U_EXP_BIAS - pr80Val->s.fSign;
                pFpuRes->r80Result.s.fSign     = pr80Val->s.fSign;
            }
            /* ST(0) > 1.0 || ST(0) < -1.0: undefined behavior */
            /** @todo 287 is documented to only accept values 0 <= ST(0) <= 0.5. */
            else
                pFpuRes->r80Result = *pr80Val;
            fFsw |= X86_FSW_PE;
            if (!(fFcw & X86_FCW_PM))
                fFsw |= X86_FSW_ES | X86_FSW_B;
        }
    }
    else if (   RTFLOAT80U_IS_ZERO(pr80Val)
             || RTFLOAT80U_IS_QUIET_NAN(pr80Val)
             || RTFLOAT80U_IS_INDEFINITE(pr80Val))
        pFpuRes->r80Result = *pr80Val;
    else if (RTFLOAT80U_IS_INF(pr80Val))
        pFpuRes->r80Result = pr80Val->s.fSign ? g_ar80One[1] : *pr80Val;
    else if (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val))
    {
        fFsw |= X86_FSW_DE;
        if (fFcw & X86_FCW_DM)
            fFsw = iemAImpl_f2xm1_r80_normal(pr80Val, &pFpuRes->r80Result, fFcw, fFsw);
        else
        {
            pFpuRes->r80Result = *pr80Val;
            fFsw |= X86_FSW_ES | X86_FSW_B;
        }
    }
    else
    {
        if (   (   RTFLOAT80U_IS_UNNORMAL(pr80Val)
                || RTFLOAT80U_IS_PSEUDO_NAN(pr80Val))
            && (fFcw & X86_FCW_IM))
            pFpuRes->r80Result = g_r80Indefinite;
        else
        {
            pFpuRes->r80Result = *pr80Val;
            if (RTFLOAT80U_IS_SIGNALLING_NAN(pr80Val) && (fFcw & X86_FCW_IM))
                pFpuRes->r80Result.s.uMantissa |= RT_BIT_64(62); /* make it quiet */
        }
        fFsw |= X86_FSW_IE;
        if (!(fFcw & X86_FCW_IM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }
    pFpuRes->FSW = fFsw;
}

#endif /* IEM_WITHOUT_ASSEMBLY */

IEM_DECL_IMPL_DEF(void, iemAImpl_f2xm1_r80_amd,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val))
{
    iemAImpl_f2xm1_r80(pFpuState, pFpuRes, pr80Val);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_f2xm1_r80_intel,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val))
{
    iemAImpl_f2xm1_r80(pFpuState, pFpuRes, pr80Val);
}

#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_fabs_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val))
{
    pFpuRes->FSW = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (7 << X86_FSW_TOP_SHIFT);
    pFpuRes->r80Result         = *pr80Val;
    pFpuRes->r80Result.s.fSign = 0;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fchs_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes, PCRTFLOAT80U pr80Val))
{
    pFpuRes->FSW = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (7 << X86_FSW_TOP_SHIFT);
    pFpuRes->r80Result         = *pr80Val;
    pFpuRes->r80Result.s.fSign = !pr80Val->s.fSign;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_fxtract_r80_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULTTWO pFpuResTwo, PCRTFLOAT80U pr80Val))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = (pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3)) | (6 << X86_FSW_TOP_SHIFT);

    if (RTFLOAT80U_IS_NORMAL(pr80Val))
    {
        softfloat_state_t Ignored = SOFTFLOAT_STATE_INIT_DEFAULTS();
        iemFpuSoftF80ToIprt(&pFpuResTwo->r80Result1, i32_to_extF80((int32_t)pr80Val->s.uExponent - RTFLOAT80U_EXP_BIAS, &Ignored));

        pFpuResTwo->r80Result2.s.fSign     = pr80Val->s.fSign;
        pFpuResTwo->r80Result2.s.uExponent = RTFLOAT80U_EXP_BIAS;
        pFpuResTwo->r80Result2.s.uMantissa = pr80Val->s.uMantissa;
    }
    else if (RTFLOAT80U_IS_ZERO(pr80Val))
    {
        fFsw |= X86_FSW_ZE;
        if (fFcw & X86_FCW_ZM)
        {
            pFpuResTwo->r80Result1 = g_ar80Infinity[1];
            pFpuResTwo->r80Result2 = *pr80Val;
        }
        else
        {
            pFpuResTwo->r80Result2 = *pr80Val;
            fFsw = X86_FSW_ES | X86_FSW_B | (fFsw & ~X86_FSW_TOP_MASK) | (7 << X86_FSW_TOP_SHIFT);
        }
    }
    else if (RTFLOAT80U_IS_DENORMAL_OR_PSEUDO_DENORMAL(pr80Val))
    {
        fFsw |= X86_FSW_DE;
        if (fFcw & X86_FCW_DM)
        {
            pFpuResTwo->r80Result2.s.fSign     = pr80Val->s.fSign;
            pFpuResTwo->r80Result2.s.uExponent = RTFLOAT80U_EXP_BIAS;
            pFpuResTwo->r80Result2.s.uMantissa = pr80Val->s.uMantissa;
            int32_t iExponent = -16382;
            while (!(pFpuResTwo->r80Result2.s.uMantissa & RT_BIT_64(63)))
            {
                pFpuResTwo->r80Result2.s.uMantissa <<= 1;
                iExponent--;
            }

            softfloat_state_t Ignored = SOFTFLOAT_STATE_INIT_DEFAULTS();
            iemFpuSoftF80ToIprt(&pFpuResTwo->r80Result1, i32_to_extF80(iExponent, &Ignored));
        }
        else
        {
            pFpuResTwo->r80Result2 = *pr80Val;
            fFsw = X86_FSW_ES | X86_FSW_B | (fFsw & ~X86_FSW_TOP_MASK) | (7 << X86_FSW_TOP_SHIFT);
        }
    }
    else if (   RTFLOAT80U_IS_QUIET_NAN(pr80Val)
             || RTFLOAT80U_IS_INDEFINITE(pr80Val))
    {
        pFpuResTwo->r80Result1 = *pr80Val;
        pFpuResTwo->r80Result2 = *pr80Val;
    }
    else if (RTFLOAT80U_IS_INF(pr80Val))
    {
        pFpuResTwo->r80Result1 = g_ar80Infinity[0];
        pFpuResTwo->r80Result2 = *pr80Val;
    }
    else
    {
        if (fFcw & X86_FCW_IM)
        {
            if (!RTFLOAT80U_IS_SIGNALLING_NAN(pr80Val))
                pFpuResTwo->r80Result1 = g_r80Indefinite;
            else
            {
                pFpuResTwo->r80Result1 = *pr80Val;
                pFpuResTwo->r80Result1.s.uMantissa |= RT_BIT_64(62); /* make it quiet */
            }
            pFpuResTwo->r80Result2 = pFpuResTwo->r80Result1;
        }
        else
        {
            pFpuResTwo->r80Result2 = *pr80Val;
            fFsw = X86_FSW_ES | X86_FSW_B | (fFsw & ~X86_FSW_TOP_MASK) | (7 << X86_FSW_TOP_SHIFT);
        }
        fFsw |= X86_FSW_IE;
    }
    pFpuResTwo->FSW = fFsw;
}
#endif /* IEM_WITHOUT_ASSEMBLY */

#if defined(IEM_WITHOUT_ASSEMBLY)

static uint16_t iemAImpl_fyl2x_r80_by_r80_normal(PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2, PRTFLOAT80U pr80Result, uint16_t fFcw, uint16_t fFsw)
{
    softfloat_state_t SoftState = SOFTFLOAT_STATE_INIT_DEFAULTS();
    extFloat80_t y = iemFpuSoftF80FromIprt(pr80Val1);
    extFloat80_t x = iemFpuSoftF80FromIprt(pr80Val2);
    extFloat80_t v;
    (void)fFcw;

    v = extF80_ylog2x(y, x, &SoftState);
    iemFpuSoftF80ToIprt(pr80Result, v);

    return fFsw;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_fyl2x_r80_by_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                   PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3);

    if (RTFLOAT80U_IS_NORMAL(pr80Val1) && RTFLOAT80U_IS_NORMAL(pr80Val2) && !pr80Val2->s.fSign)
    {
        fFsw |= iemAImpl_fyl2x_r80_by_r80_normal(pr80Val1, pr80Val2, &pFpuRes->r80Result, fFcw, fFsw);

        fFsw |= X86_FSW_PE | (7 << X86_FSW_TOP_SHIFT);
        if (!(fFcw & X86_FCW_PM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }
    else
    {
        fFsw |= X86_FSW_IE;

        if (!(fFcw & X86_FCW_IM))
        {
            pFpuRes->r80Result = *pr80Val2;
            fFsw |= X86_FSW_ES | X86_FSW_B | (6 << X86_FSW_TOP_SHIFT);
        }
        else
        {
            pFpuRes->r80Result = g_r80Indefinite;
            fFsw |= (7 << X86_FSW_TOP_SHIFT);
        }
    }

    pFpuRes->FSW = fFsw;
}
#endif /* IEM_WITHOUT_ASSEMBLY */

IEM_DECL_IMPL_DEF(void, iemAImpl_fyl2x_r80_by_r80_intel,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                         PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    iemAImpl_fyl2x_r80_by_r80(pFpuState, pFpuRes, pr80Val1, pr80Val2);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_fyl2x_r80_by_r80_amd,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                       PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    iemAImpl_fyl2x_r80_by_r80(pFpuState, pFpuRes, pr80Val1, pr80Val2);
}

#if defined(IEM_WITHOUT_ASSEMBLY)

static uint16_t iemAImpl_fyl2xp1_r80_by_r80_normal(PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2, PRTFLOAT80U pr80Result, uint16_t fFcw, uint16_t fFsw)
{
    softfloat_state_t SoftState = SOFTFLOAT_STATE_INIT_DEFAULTS();
    extFloat80_t y = iemFpuSoftF80FromIprt(pr80Val1);
    extFloat80_t x = iemFpuSoftF80FromIprt(pr80Val2);
    extFloat80_t v;
    (void)fFcw;

    v = extF80_ylog2xp1(y, x, &SoftState);
    iemFpuSoftF80ToIprt(pr80Result, v);

    return fFsw;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_fyl2xp1_r80_by_r80,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                     PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    uint16_t const fFcw = pFpuState->FCW;
    uint16_t fFsw       = pFpuState->FSW & (X86_FSW_C0 | X86_FSW_C2 | X86_FSW_C3);

    if (RTFLOAT80U_IS_NORMAL(pr80Val1) && RTFLOAT80U_IS_NORMAL(pr80Val2) && pr80Val2->s.uExponent < RTFLOAT80U_EXP_BIAS)
    {
        fFsw = iemAImpl_fyl2xp1_r80_by_r80_normal(pr80Val1, pr80Val2, &pFpuRes->r80Result, fFcw, fFsw);

        fFsw |= X86_FSW_PE | (7 << X86_FSW_TOP_SHIFT);
        if (!(fFcw & X86_FCW_PM))
            fFsw |= X86_FSW_ES | X86_FSW_B;
    }
    else
    {
        fFsw |= X86_FSW_IE;

        if (!(fFcw & X86_FCW_IM))
        {
            pFpuRes->r80Result = *pr80Val2;
            fFsw |= X86_FSW_ES | X86_FSW_B | (6 << X86_FSW_TOP_SHIFT);
        }
        else
        {
            pFpuRes->r80Result = g_r80Indefinite;
            fFsw |= (7 << X86_FSW_TOP_SHIFT);
        }
    }

    pFpuRes->FSW = fFsw;
}

#endif /* IEM_WITHOUT_ASSEMBLY */

IEM_DECL_IMPL_DEF(void, iemAImpl_fyl2xp1_r80_by_r80_intel,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                           PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    iemAImpl_fyl2xp1_r80_by_r80(pFpuState, pFpuRes, pr80Val1, pr80Val2);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_fyl2xp1_r80_by_r80_amd,(PCX86FXSTATE pFpuState, PIEMFPURESULT pFpuRes,
                                                         PCRTFLOAT80U pr80Val1, PCRTFLOAT80U pr80Val2))
{
    iemAImpl_fyl2xp1_r80_by_r80(pFpuState, pFpuRes, pr80Val1, pr80Val2);
}


/*********************************************************************************************************************************
*   MMX, SSE & AVX                                                                                                               *
*********************************************************************************************************************************/

/*
 * PAND / VPAND / PANDPS / VPANDPS / PANDPD / VPANDPD
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pand_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    *puDst &= *puSrc;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pand_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    puDst->au64[0] &= puSrc->au64[0];
    puDst->au64[1] &= puSrc->au64[1];
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpand_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au64[0] = puSrc1->au64[0] & puSrc2->au64[0];
    puDst->au64[1] = puSrc1->au64[1] & puSrc2->au64[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpand_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au64[0] = puSrc1->au64[0] & puSrc2->au64[0];
    puDst->au64[1] = puSrc1->au64[1] & puSrc2->au64[1];
    puDst->au64[2] = puSrc1->au64[2] & puSrc2->au64[2];
    puDst->au64[3] = puSrc1->au64[3] & puSrc2->au64[3];
}


/*
 * PANDN / VPANDN / PANDNPS / VPANDNPS / PANDNPD / VPANDNPD
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pandn_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    *puDst = ~*puDst & *puSrc;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pandn_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    puDst->au64[0] = ~puDst->au64[0] & puSrc->au64[0];
    puDst->au64[1] = ~puDst->au64[1] & puSrc->au64[1];
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpandn_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au64[0] = ~puSrc1->au64[0] & puSrc2->au64[0];
    puDst->au64[1] = ~puSrc1->au64[1] & puSrc2->au64[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpandn_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au64[0] = ~puSrc1->au64[0] & puSrc2->au64[0];
    puDst->au64[1] = ~puSrc1->au64[1] & puSrc2->au64[1];
    puDst->au64[2] = ~puSrc1->au64[2] & puSrc2->au64[2];
    puDst->au64[3] = ~puSrc1->au64[3] & puSrc2->au64[3];
}


/*
 * POR / VPOR / PORPS / VPORPS / PORPD / VPORPD
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_por_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    *puDst |= *puSrc;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_por_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    puDst->au64[0] |= puSrc->au64[0];
    puDst->au64[1] |= puSrc->au64[1];
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpor_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au64[0] = puSrc1->au64[0] | puSrc2->au64[0];
    puDst->au64[1] = puSrc1->au64[1] | puSrc2->au64[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpor_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au64[0] = puSrc1->au64[0] | puSrc2->au64[0];
    puDst->au64[1] = puSrc1->au64[1] | puSrc2->au64[1];
    puDst->au64[2] = puSrc1->au64[2] | puSrc2->au64[2];
    puDst->au64[3] = puSrc1->au64[3] | puSrc2->au64[3];
}


/*
 * PXOR / VPXOR / PXORPS / VPXORPS / PXORPD / VPXORPD
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pxor_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    *puDst ^= *puSrc;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pxor_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    puDst->au64[0] ^= puSrc->au64[0];
    puDst->au64[1] ^= puSrc->au64[1];
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpxor_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au64[0] = puSrc1->au64[0] ^ puSrc2->au64[0];
    puDst->au64[1] = puSrc1->au64[1] ^ puSrc2->au64[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpxor_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au64[0] = puSrc1->au64[0] ^ puSrc2->au64[0];
    puDst->au64[1] = puSrc1->au64[1] ^ puSrc2->au64[1];
    puDst->au64[2] = puSrc1->au64[2] ^ puSrc2->au64[2];
    puDst->au64[3] = puSrc1->au64[3] ^ puSrc2->au64[3];
}


/*
 * PCMPEQB / VPCMPEQB
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpeqb_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au8[0] = uSrc1.au8[0] == uSrc2.au8[0] ? 0xff : 0;
    uDst.au8[1] = uSrc1.au8[1] == uSrc2.au8[1] ? 0xff : 0;
    uDst.au8[2] = uSrc1.au8[2] == uSrc2.au8[2] ? 0xff : 0;
    uDst.au8[3] = uSrc1.au8[3] == uSrc2.au8[3] ? 0xff : 0;
    uDst.au8[4] = uSrc1.au8[4] == uSrc2.au8[4] ? 0xff : 0;
    uDst.au8[5] = uSrc1.au8[5] == uSrc2.au8[5] ? 0xff : 0;
    uDst.au8[6] = uSrc1.au8[6] == uSrc2.au8[6] ? 0xff : 0;
    uDst.au8[7] = uSrc1.au8[7] == uSrc2.au8[7] ? 0xff : 0;
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpeqb_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au8[0]  = uSrc1.au8[0]  == puSrc->au8[0]  ? UINT8_MAX : 0;
    puDst->au8[1]  = uSrc1.au8[1]  == puSrc->au8[1]  ? UINT8_MAX : 0;
    puDst->au8[2]  = uSrc1.au8[2]  == puSrc->au8[2]  ? UINT8_MAX : 0;
    puDst->au8[3]  = uSrc1.au8[3]  == puSrc->au8[3]  ? UINT8_MAX : 0;
    puDst->au8[4]  = uSrc1.au8[4]  == puSrc->au8[4]  ? UINT8_MAX : 0;
    puDst->au8[5]  = uSrc1.au8[5]  == puSrc->au8[5]  ? UINT8_MAX : 0;
    puDst->au8[6]  = uSrc1.au8[6]  == puSrc->au8[6]  ? UINT8_MAX : 0;
    puDst->au8[7]  = uSrc1.au8[7]  == puSrc->au8[7]  ? UINT8_MAX : 0;
    puDst->au8[8]  = uSrc1.au8[8]  == puSrc->au8[8]  ? UINT8_MAX : 0;
    puDst->au8[9]  = uSrc1.au8[9]  == puSrc->au8[9]  ? UINT8_MAX : 0;
    puDst->au8[10] = uSrc1.au8[10] == puSrc->au8[10] ? UINT8_MAX : 0;
    puDst->au8[11] = uSrc1.au8[11] == puSrc->au8[11] ? UINT8_MAX : 0;
    puDst->au8[12] = uSrc1.au8[12] == puSrc->au8[12] ? UINT8_MAX : 0;
    puDst->au8[13] = uSrc1.au8[13] == puSrc->au8[13] ? UINT8_MAX : 0;
    puDst->au8[14] = uSrc1.au8[14] == puSrc->au8[14] ? UINT8_MAX : 0;
    puDst->au8[15] = uSrc1.au8[15] == puSrc->au8[15] ? UINT8_MAX : 0;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpeqb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au8[0]  = puSrc1->au8[0]  == puSrc2->au8[0]  ? UINT8_MAX : 0;
    puDst->au8[1]  = puSrc1->au8[1]  == puSrc2->au8[1]  ? UINT8_MAX : 0;
    puDst->au8[2]  = puSrc1->au8[2]  == puSrc2->au8[2]  ? UINT8_MAX : 0;
    puDst->au8[3]  = puSrc1->au8[3]  == puSrc2->au8[3]  ? UINT8_MAX : 0;
    puDst->au8[4]  = puSrc1->au8[4]  == puSrc2->au8[4]  ? UINT8_MAX : 0;
    puDst->au8[5]  = puSrc1->au8[5]  == puSrc2->au8[5]  ? UINT8_MAX : 0;
    puDst->au8[6]  = puSrc1->au8[6]  == puSrc2->au8[6]  ? UINT8_MAX : 0;
    puDst->au8[7]  = puSrc1->au8[7]  == puSrc2->au8[7]  ? UINT8_MAX : 0;
    puDst->au8[8]  = puSrc1->au8[8]  == puSrc2->au8[8]  ? UINT8_MAX : 0;
    puDst->au8[9]  = puSrc1->au8[9]  == puSrc2->au8[9]  ? UINT8_MAX : 0;
    puDst->au8[10] = puSrc1->au8[10] == puSrc2->au8[10] ? UINT8_MAX : 0;
    puDst->au8[11] = puSrc1->au8[11] == puSrc2->au8[11] ? UINT8_MAX : 0;
    puDst->au8[12] = puSrc1->au8[12] == puSrc2->au8[12] ? UINT8_MAX : 0;
    puDst->au8[13] = puSrc1->au8[13] == puSrc2->au8[13] ? UINT8_MAX : 0;
    puDst->au8[14] = puSrc1->au8[14] == puSrc2->au8[14] ? UINT8_MAX : 0;
    puDst->au8[15] = puSrc1->au8[15] == puSrc2->au8[15] ? UINT8_MAX : 0;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpeqb_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au8[0]  = puSrc1->au8[0]  == puSrc2->au8[0]  ? UINT8_MAX : 0;
    puDst->au8[1]  = puSrc1->au8[1]  == puSrc2->au8[1]  ? UINT8_MAX : 0;
    puDst->au8[2]  = puSrc1->au8[2]  == puSrc2->au8[2]  ? UINT8_MAX : 0;
    puDst->au8[3]  = puSrc1->au8[3]  == puSrc2->au8[3]  ? UINT8_MAX : 0;
    puDst->au8[4]  = puSrc1->au8[4]  == puSrc2->au8[4]  ? UINT8_MAX : 0;
    puDst->au8[5]  = puSrc1->au8[5]  == puSrc2->au8[5]  ? UINT8_MAX : 0;
    puDst->au8[6]  = puSrc1->au8[6]  == puSrc2->au8[6]  ? UINT8_MAX : 0;
    puDst->au8[7]  = puSrc1->au8[7]  == puSrc2->au8[7]  ? UINT8_MAX : 0;
    puDst->au8[8]  = puSrc1->au8[8]  == puSrc2->au8[8]  ? UINT8_MAX : 0;
    puDst->au8[9]  = puSrc1->au8[9]  == puSrc2->au8[9]  ? UINT8_MAX : 0;
    puDst->au8[10] = puSrc1->au8[10] == puSrc2->au8[10] ? UINT8_MAX : 0;
    puDst->au8[11] = puSrc1->au8[11] == puSrc2->au8[11] ? UINT8_MAX : 0;
    puDst->au8[12] = puSrc1->au8[12] == puSrc2->au8[12] ? UINT8_MAX : 0;
    puDst->au8[13] = puSrc1->au8[13] == puSrc2->au8[13] ? UINT8_MAX : 0;
    puDst->au8[14] = puSrc1->au8[14] == puSrc2->au8[14] ? UINT8_MAX : 0;
    puDst->au8[15] = puSrc1->au8[15] == puSrc2->au8[15] ? UINT8_MAX : 0;
    puDst->au8[16] = puSrc1->au8[16] == puSrc2->au8[16] ? UINT8_MAX : 0;
    puDst->au8[17] = puSrc1->au8[17] == puSrc2->au8[17] ? UINT8_MAX : 0;
    puDst->au8[18] = puSrc1->au8[18] == puSrc2->au8[18] ? UINT8_MAX : 0;
    puDst->au8[19] = puSrc1->au8[19] == puSrc2->au8[19] ? UINT8_MAX : 0;
    puDst->au8[20] = puSrc1->au8[20] == puSrc2->au8[20] ? UINT8_MAX : 0;
    puDst->au8[21] = puSrc1->au8[21] == puSrc2->au8[21] ? UINT8_MAX : 0;
    puDst->au8[22] = puSrc1->au8[22] == puSrc2->au8[22] ? UINT8_MAX : 0;
    puDst->au8[23] = puSrc1->au8[23] == puSrc2->au8[23] ? UINT8_MAX : 0;
    puDst->au8[24] = puSrc1->au8[24] == puSrc2->au8[24] ? UINT8_MAX : 0;
    puDst->au8[25] = puSrc1->au8[25] == puSrc2->au8[25] ? UINT8_MAX : 0;
    puDst->au8[26] = puSrc1->au8[26] == puSrc2->au8[26] ? UINT8_MAX : 0;
    puDst->au8[27] = puSrc1->au8[27] == puSrc2->au8[27] ? UINT8_MAX : 0;
    puDst->au8[28] = puSrc1->au8[28] == puSrc2->au8[28] ? UINT8_MAX : 0;
    puDst->au8[29] = puSrc1->au8[29] == puSrc2->au8[29] ? UINT8_MAX : 0;
    puDst->au8[30] = puSrc1->au8[30] == puSrc2->au8[30] ? UINT8_MAX : 0;
    puDst->au8[31] = puSrc1->au8[31] == puSrc2->au8[31] ? UINT8_MAX : 0;
}


/*
 * PCMPEQW / VPCMPEQW
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpeqw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au16[0] = uSrc1.au16[0] == uSrc2.au16[0] ? UINT16_MAX : 0;
    uDst.au16[1] = uSrc1.au16[1] == uSrc2.au16[1] ? UINT16_MAX : 0;
    uDst.au16[2] = uSrc1.au16[2] == uSrc2.au16[2] ? UINT16_MAX : 0;
    uDst.au16[3] = uSrc1.au16[3] == uSrc2.au16[3] ? UINT16_MAX : 0;
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpeqw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au16[0] = uSrc1.au16[0] == puSrc->au16[0] ? UINT16_MAX : 0;
    puDst->au16[1] = uSrc1.au16[1] == puSrc->au16[1] ? UINT16_MAX : 0;
    puDst->au16[2] = uSrc1.au16[2] == puSrc->au16[2] ? UINT16_MAX : 0;
    puDst->au16[3] = uSrc1.au16[3] == puSrc->au16[3] ? UINT16_MAX : 0;
    puDst->au16[4] = uSrc1.au16[4] == puSrc->au16[4] ? UINT16_MAX : 0;
    puDst->au16[5] = uSrc1.au16[5] == puSrc->au16[5] ? UINT16_MAX : 0;
    puDst->au16[6] = uSrc1.au16[6] == puSrc->au16[6] ? UINT16_MAX : 0;
    puDst->au16[7] = uSrc1.au16[7] == puSrc->au16[7] ? UINT16_MAX : 0;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpeqw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au16[0] = puSrc1->au16[0] == puSrc2->au16[0] ? UINT16_MAX : 0;
    puDst->au16[1] = puSrc1->au16[1] == puSrc2->au16[1] ? UINT16_MAX : 0;
    puDst->au16[2] = puSrc1->au16[2] == puSrc2->au16[2] ? UINT16_MAX : 0;
    puDst->au16[3] = puSrc1->au16[3] == puSrc2->au16[3] ? UINT16_MAX : 0;
    puDst->au16[4] = puSrc1->au16[4] == puSrc2->au16[4] ? UINT16_MAX : 0;
    puDst->au16[5] = puSrc1->au16[5] == puSrc2->au16[5] ? UINT16_MAX : 0;
    puDst->au16[6] = puSrc1->au16[6] == puSrc2->au16[6] ? UINT16_MAX : 0;
    puDst->au16[7] = puSrc1->au16[7] == puSrc2->au16[7] ? UINT16_MAX : 0;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpeqw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au16[0]  = puSrc1->au16[0]  == puSrc2->au16[0]  ? UINT16_MAX : 0;
    puDst->au16[1]  = puSrc1->au16[1]  == puSrc2->au16[1]  ? UINT16_MAX : 0;
    puDst->au16[2]  = puSrc1->au16[2]  == puSrc2->au16[2]  ? UINT16_MAX : 0;
    puDst->au16[3]  = puSrc1->au16[3]  == puSrc2->au16[3]  ? UINT16_MAX : 0;
    puDst->au16[4]  = puSrc1->au16[4]  == puSrc2->au16[4]  ? UINT16_MAX : 0;
    puDst->au16[5]  = puSrc1->au16[5]  == puSrc2->au16[5]  ? UINT16_MAX : 0;
    puDst->au16[6]  = puSrc1->au16[6]  == puSrc2->au16[6]  ? UINT16_MAX : 0;
    puDst->au16[7]  = puSrc1->au16[7]  == puSrc2->au16[7]  ? UINT16_MAX : 0;
    puDst->au16[8]  = puSrc1->au16[8]  == puSrc2->au16[8]  ? UINT16_MAX : 0;
    puDst->au16[9]  = puSrc1->au16[9]  == puSrc2->au16[9]  ? UINT16_MAX : 0;
    puDst->au16[10] = puSrc1->au16[10] == puSrc2->au16[10] ? UINT16_MAX : 0;
    puDst->au16[11] = puSrc1->au16[11] == puSrc2->au16[11] ? UINT16_MAX : 0;
    puDst->au16[12] = puSrc1->au16[12] == puSrc2->au16[12] ? UINT16_MAX : 0;
    puDst->au16[13] = puSrc1->au16[13] == puSrc2->au16[13] ? UINT16_MAX : 0;
    puDst->au16[14] = puSrc1->au16[14] == puSrc2->au16[14] ? UINT16_MAX : 0;
    puDst->au16[15] = puSrc1->au16[15] == puSrc2->au16[15] ? UINT16_MAX : 0;
}


/*
 * PCMPEQD / VPCMPEQD.
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpeqd_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au32[0] = uSrc1.au32[0] == uSrc2.au32[0] ? UINT32_MAX : 0;
    uDst.au32[1] = uSrc1.au32[1] == uSrc2.au32[1] ? UINT32_MAX : 0;
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpeqd_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au32[0] = uSrc1.au32[0] == puSrc->au32[0] ? UINT32_MAX : 0;
    puDst->au32[1] = uSrc1.au32[1] == puSrc->au32[1] ? UINT32_MAX : 0;
    puDst->au32[2] = uSrc1.au32[2] == puSrc->au32[2] ? UINT32_MAX : 0;
    puDst->au32[3] = uSrc1.au32[3] == puSrc->au32[3] ? UINT32_MAX : 0;
}

#endif /* IEM_WITHOUT_ASSEMBLY */

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpeqd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au32[0] = puSrc1->au32[0] == puSrc2->au32[0] ? UINT32_MAX : 0;
    puDst->au32[1] = puSrc1->au32[1] == puSrc2->au32[1] ? UINT32_MAX : 0;
    puDst->au32[2] = puSrc1->au32[2] == puSrc2->au32[2] ? UINT32_MAX : 0;
    puDst->au32[3] = puSrc1->au32[3] == puSrc2->au32[3] ? UINT32_MAX : 0;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpeqd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au32[0] = puSrc1->au32[0] == puSrc2->au32[0] ? UINT32_MAX : 0;
    puDst->au32[1] = puSrc1->au32[1] == puSrc2->au32[1] ? UINT32_MAX : 0;
    puDst->au32[2] = puSrc1->au32[2] == puSrc2->au32[2] ? UINT32_MAX : 0;
    puDst->au32[3] = puSrc1->au32[3] == puSrc2->au32[3] ? UINT32_MAX : 0;
    puDst->au32[4] = puSrc1->au32[4] == puSrc2->au32[4] ? UINT32_MAX : 0;
    puDst->au32[5] = puSrc1->au32[5] == puSrc2->au32[5] ? UINT32_MAX : 0;
    puDst->au32[6] = puSrc1->au32[6] == puSrc2->au32[6] ? UINT32_MAX : 0;
    puDst->au32[7] = puSrc1->au32[7] == puSrc2->au32[7] ? UINT32_MAX : 0;
}


/*
 * PCMPEQQ / VPCMPEQQ.
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpeqq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au64[0] = uSrc1.au64[0] == puSrc->au64[0] ? UINT64_MAX : 0;
    puDst->au64[1] = uSrc1.au64[1] == puSrc->au64[1] ? UINT64_MAX : 0;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpeqq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au64[0] = puSrc1->au64[0] == puSrc2->au64[0] ? UINT64_MAX : 0;
    puDst->au64[1] = puSrc1->au64[1] == puSrc2->au64[1] ? UINT64_MAX : 0;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpeqq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au64[0] = puSrc1->au64[0] == puSrc2->au64[0] ? UINT64_MAX : 0;
    puDst->au64[1] = puSrc1->au64[1] == puSrc2->au64[1] ? UINT64_MAX : 0;
    puDst->au64[2] = puSrc1->au64[2] == puSrc2->au64[2] ? UINT64_MAX : 0;
    puDst->au64[3] = puSrc1->au64[3] == puSrc2->au64[3] ? UINT64_MAX : 0;
}


/*
 * PCMPGTB / VPCMPGTB
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpgtb_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au8[0] = uSrc1.ai8[0] > uSrc2.ai8[0] ? UINT8_MAX : 0;
    uDst.au8[1] = uSrc1.ai8[1] > uSrc2.ai8[1] ? UINT8_MAX : 0;
    uDst.au8[2] = uSrc1.ai8[2] > uSrc2.ai8[2] ? UINT8_MAX : 0;
    uDst.au8[3] = uSrc1.ai8[3] > uSrc2.ai8[3] ? UINT8_MAX : 0;
    uDst.au8[4] = uSrc1.ai8[4] > uSrc2.ai8[4] ? UINT8_MAX : 0;
    uDst.au8[5] = uSrc1.ai8[5] > uSrc2.ai8[5] ? UINT8_MAX : 0;
    uDst.au8[6] = uSrc1.ai8[6] > uSrc2.ai8[6] ? UINT8_MAX : 0;
    uDst.au8[7] = uSrc1.ai8[7] > uSrc2.ai8[7] ? UINT8_MAX : 0;
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpgtb_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au8[0]  = uSrc1.ai8[0]  > puSrc->ai8[0]  ? UINT8_MAX : 0;
    puDst->au8[1]  = uSrc1.ai8[1]  > puSrc->ai8[1]  ? UINT8_MAX : 0;
    puDst->au8[2]  = uSrc1.ai8[2]  > puSrc->ai8[2]  ? UINT8_MAX : 0;
    puDst->au8[3]  = uSrc1.ai8[3]  > puSrc->ai8[3]  ? UINT8_MAX : 0;
    puDst->au8[4]  = uSrc1.ai8[4]  > puSrc->ai8[4]  ? UINT8_MAX : 0;
    puDst->au8[5]  = uSrc1.ai8[5]  > puSrc->ai8[5]  ? UINT8_MAX : 0;
    puDst->au8[6]  = uSrc1.ai8[6]  > puSrc->ai8[6]  ? UINT8_MAX : 0;
    puDst->au8[7]  = uSrc1.ai8[7]  > puSrc->ai8[7]  ? UINT8_MAX : 0;
    puDst->au8[8]  = uSrc1.ai8[8]  > puSrc->ai8[8]  ? UINT8_MAX : 0;
    puDst->au8[9]  = uSrc1.ai8[9]  > puSrc->ai8[9]  ? UINT8_MAX : 0;
    puDst->au8[10] = uSrc1.ai8[10] > puSrc->ai8[10] ? UINT8_MAX : 0;
    puDst->au8[11] = uSrc1.ai8[11] > puSrc->ai8[11] ? UINT8_MAX : 0;
    puDst->au8[12] = uSrc1.ai8[12] > puSrc->ai8[12] ? UINT8_MAX : 0;
    puDst->au8[13] = uSrc1.ai8[13] > puSrc->ai8[13] ? UINT8_MAX : 0;
    puDst->au8[14] = uSrc1.ai8[14] > puSrc->ai8[14] ? UINT8_MAX : 0;
    puDst->au8[15] = uSrc1.ai8[15] > puSrc->ai8[15] ? UINT8_MAX : 0;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpgtb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au8[0]  = puSrc1->ai8[0]  > puSrc2->ai8[0]  ? UINT8_MAX : 0;
    puDst->au8[1]  = puSrc1->ai8[1]  > puSrc2->ai8[1]  ? UINT8_MAX : 0;
    puDst->au8[2]  = puSrc1->ai8[2]  > puSrc2->ai8[2]  ? UINT8_MAX : 0;
    puDst->au8[3]  = puSrc1->ai8[3]  > puSrc2->ai8[3]  ? UINT8_MAX : 0;
    puDst->au8[4]  = puSrc1->ai8[4]  > puSrc2->ai8[4]  ? UINT8_MAX : 0;
    puDst->au8[5]  = puSrc1->ai8[5]  > puSrc2->ai8[5]  ? UINT8_MAX : 0;
    puDst->au8[6]  = puSrc1->ai8[6]  > puSrc2->ai8[6]  ? UINT8_MAX : 0;
    puDst->au8[7]  = puSrc1->ai8[7]  > puSrc2->ai8[7]  ? UINT8_MAX : 0;
    puDst->au8[8]  = puSrc1->ai8[8]  > puSrc2->ai8[8]  ? UINT8_MAX : 0;
    puDst->au8[9]  = puSrc1->ai8[9]  > puSrc2->ai8[9]  ? UINT8_MAX : 0;
    puDst->au8[10] = puSrc1->ai8[10] > puSrc2->ai8[10] ? UINT8_MAX : 0;
    puDst->au8[11] = puSrc1->ai8[11] > puSrc2->ai8[11] ? UINT8_MAX : 0;
    puDst->au8[12] = puSrc1->ai8[12] > puSrc2->ai8[12] ? UINT8_MAX : 0;
    puDst->au8[13] = puSrc1->ai8[13] > puSrc2->ai8[13] ? UINT8_MAX : 0;
    puDst->au8[14] = puSrc1->ai8[14] > puSrc2->ai8[14] ? UINT8_MAX : 0;
    puDst->au8[15] = puSrc1->ai8[15] > puSrc2->ai8[15] ? UINT8_MAX : 0;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpgtb_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au8[0]  = puSrc1->ai8[0]  > puSrc2->ai8[0]  ? UINT8_MAX : 0;
    puDst->au8[1]  = puSrc1->ai8[1]  > puSrc2->ai8[1]  ? UINT8_MAX : 0;
    puDst->au8[2]  = puSrc1->ai8[2]  > puSrc2->ai8[2]  ? UINT8_MAX : 0;
    puDst->au8[3]  = puSrc1->ai8[3]  > puSrc2->ai8[3]  ? UINT8_MAX : 0;
    puDst->au8[4]  = puSrc1->ai8[4]  > puSrc2->ai8[4]  ? UINT8_MAX : 0;
    puDst->au8[5]  = puSrc1->ai8[5]  > puSrc2->ai8[5]  ? UINT8_MAX : 0;
    puDst->au8[6]  = puSrc1->ai8[6]  > puSrc2->ai8[6]  ? UINT8_MAX : 0;
    puDst->au8[7]  = puSrc1->ai8[7]  > puSrc2->ai8[7]  ? UINT8_MAX : 0;
    puDst->au8[8]  = puSrc1->ai8[8]  > puSrc2->ai8[8]  ? UINT8_MAX : 0;
    puDst->au8[9]  = puSrc1->ai8[9]  > puSrc2->ai8[9]  ? UINT8_MAX : 0;
    puDst->au8[10] = puSrc1->ai8[10] > puSrc2->ai8[10] ? UINT8_MAX : 0;
    puDst->au8[11] = puSrc1->ai8[11] > puSrc2->ai8[11] ? UINT8_MAX : 0;
    puDst->au8[12] = puSrc1->ai8[12] > puSrc2->ai8[12] ? UINT8_MAX : 0;
    puDst->au8[13] = puSrc1->ai8[13] > puSrc2->ai8[13] ? UINT8_MAX : 0;
    puDst->au8[14] = puSrc1->ai8[14] > puSrc2->ai8[14] ? UINT8_MAX : 0;
    puDst->au8[15] = puSrc1->ai8[15] > puSrc2->ai8[15] ? UINT8_MAX : 0;
    puDst->au8[16] = puSrc1->ai8[16] > puSrc2->ai8[16] ? UINT8_MAX : 0;
    puDst->au8[17] = puSrc1->ai8[17] > puSrc2->ai8[17] ? UINT8_MAX : 0;
    puDst->au8[18] = puSrc1->ai8[18] > puSrc2->ai8[18] ? UINT8_MAX : 0;
    puDst->au8[19] = puSrc1->ai8[19] > puSrc2->ai8[19] ? UINT8_MAX : 0;
    puDst->au8[20] = puSrc1->ai8[20] > puSrc2->ai8[20] ? UINT8_MAX : 0;
    puDst->au8[21] = puSrc1->ai8[21] > puSrc2->ai8[21] ? UINT8_MAX : 0;
    puDst->au8[22] = puSrc1->ai8[22] > puSrc2->ai8[22] ? UINT8_MAX : 0;
    puDst->au8[23] = puSrc1->ai8[23] > puSrc2->ai8[23] ? UINT8_MAX : 0;
    puDst->au8[24] = puSrc1->ai8[24] > puSrc2->ai8[24] ? UINT8_MAX : 0;
    puDst->au8[25] = puSrc1->ai8[25] > puSrc2->ai8[25] ? UINT8_MAX : 0;
    puDst->au8[26] = puSrc1->ai8[26] > puSrc2->ai8[26] ? UINT8_MAX : 0;
    puDst->au8[27] = puSrc1->ai8[27] > puSrc2->ai8[27] ? UINT8_MAX : 0;
    puDst->au8[28] = puSrc1->ai8[28] > puSrc2->ai8[28] ? UINT8_MAX : 0;
    puDst->au8[29] = puSrc1->ai8[29] > puSrc2->ai8[29] ? UINT8_MAX : 0;
    puDst->au8[30] = puSrc1->ai8[30] > puSrc2->ai8[30] ? UINT8_MAX : 0;
    puDst->au8[31] = puSrc1->ai8[31] > puSrc2->ai8[31] ? UINT8_MAX : 0;
}


/*
 * PCMPGTW / VPCMPGTW
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpgtw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au16[0] = uSrc1.ai16[0] > uSrc2.ai16[0] ? UINT16_MAX : 0;
    uDst.au16[1] = uSrc1.ai16[1] > uSrc2.ai16[1] ? UINT16_MAX : 0;
    uDst.au16[2] = uSrc1.ai16[2] > uSrc2.ai16[2] ? UINT16_MAX : 0;
    uDst.au16[3] = uSrc1.ai16[3] > uSrc2.ai16[3] ? UINT16_MAX : 0;
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpgtw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au16[0] = uSrc1.ai16[0] > puSrc->ai16[0] ? UINT16_MAX : 0;
    puDst->au16[1] = uSrc1.ai16[1] > puSrc->ai16[1] ? UINT16_MAX : 0;
    puDst->au16[2] = uSrc1.ai16[2] > puSrc->ai16[2] ? UINT16_MAX : 0;
    puDst->au16[3] = uSrc1.ai16[3] > puSrc->ai16[3] ? UINT16_MAX : 0;
    puDst->au16[4] = uSrc1.ai16[4] > puSrc->ai16[4] ? UINT16_MAX : 0;
    puDst->au16[5] = uSrc1.ai16[5] > puSrc->ai16[5] ? UINT16_MAX : 0;
    puDst->au16[6] = uSrc1.ai16[6] > puSrc->ai16[6] ? UINT16_MAX : 0;
    puDst->au16[7] = uSrc1.ai16[7] > puSrc->ai16[7] ? UINT16_MAX : 0;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpgtw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au16[0] = puSrc1->ai16[0] > puSrc2->ai16[0] ? UINT16_MAX : 0;
    puDst->au16[1] = puSrc1->ai16[1] > puSrc2->ai16[1] ? UINT16_MAX : 0;
    puDst->au16[2] = puSrc1->ai16[2] > puSrc2->ai16[2] ? UINT16_MAX : 0;
    puDst->au16[3] = puSrc1->ai16[3] > puSrc2->ai16[3] ? UINT16_MAX : 0;
    puDst->au16[4] = puSrc1->ai16[4] > puSrc2->ai16[4] ? UINT16_MAX : 0;
    puDst->au16[5] = puSrc1->ai16[5] > puSrc2->ai16[5] ? UINT16_MAX : 0;
    puDst->au16[6] = puSrc1->ai16[6] > puSrc2->ai16[6] ? UINT16_MAX : 0;
    puDst->au16[7] = puSrc1->ai16[7] > puSrc2->ai16[7] ? UINT16_MAX : 0;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpgtw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au16[0]  = puSrc1->ai16[0]  > puSrc2->ai16[0]  ? UINT16_MAX : 0;
    puDst->au16[1]  = puSrc1->ai16[1]  > puSrc2->ai16[1]  ? UINT16_MAX : 0;
    puDst->au16[2]  = puSrc1->ai16[2]  > puSrc2->ai16[2]  ? UINT16_MAX : 0;
    puDst->au16[3]  = puSrc1->ai16[3]  > puSrc2->ai16[3]  ? UINT16_MAX : 0;
    puDst->au16[4]  = puSrc1->ai16[4]  > puSrc2->ai16[4]  ? UINT16_MAX : 0;
    puDst->au16[5]  = puSrc1->ai16[5]  > puSrc2->ai16[5]  ? UINT16_MAX : 0;
    puDst->au16[6]  = puSrc1->ai16[6]  > puSrc2->ai16[6]  ? UINT16_MAX : 0;
    puDst->au16[7]  = puSrc1->ai16[7]  > puSrc2->ai16[7]  ? UINT16_MAX : 0;
    puDst->au16[8]  = puSrc1->ai16[8]  > puSrc2->ai16[8]  ? UINT16_MAX : 0;
    puDst->au16[9]  = puSrc1->ai16[9]  > puSrc2->ai16[9]  ? UINT16_MAX : 0;
    puDst->au16[10] = puSrc1->ai16[10] > puSrc2->ai16[10] ? UINT16_MAX : 0;
    puDst->au16[11] = puSrc1->ai16[11] > puSrc2->ai16[11] ? UINT16_MAX : 0;
    puDst->au16[12] = puSrc1->ai16[12] > puSrc2->ai16[12] ? UINT16_MAX : 0;
    puDst->au16[13] = puSrc1->ai16[13] > puSrc2->ai16[13] ? UINT16_MAX : 0;
    puDst->au16[14] = puSrc1->ai16[14] > puSrc2->ai16[14] ? UINT16_MAX : 0;
    puDst->au16[15] = puSrc1->ai16[15] > puSrc2->ai16[15] ? UINT16_MAX : 0;
}


/*
 * PCMPGTD / VPCMPGTD.
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpgtd_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au32[0] = uSrc1.ai32[0] > uSrc2.ai32[0] ? UINT32_MAX : 0;
    uDst.au32[1] = uSrc1.ai32[1] > uSrc2.ai32[1] ? UINT32_MAX : 0;
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpgtd_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au32[0] = uSrc1.ai32[0] > puSrc->ai32[0] ? UINT32_MAX : 0;
    puDst->au32[1] = uSrc1.ai32[1] > puSrc->ai32[1] ? UINT32_MAX : 0;
    puDst->au32[2] = uSrc1.ai32[2] > puSrc->ai32[2] ? UINT32_MAX : 0;
    puDst->au32[3] = uSrc1.ai32[3] > puSrc->ai32[3] ? UINT32_MAX : 0;
}

#endif /* IEM_WITHOUT_ASSEMBLY */

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpgtd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au32[0] = puSrc1->ai32[0] > puSrc2->ai32[0] ? UINT32_MAX : 0;
    puDst->au32[1] = puSrc1->ai32[1] > puSrc2->ai32[1] ? UINT32_MAX : 0;
    puDst->au32[2] = puSrc1->ai32[2] > puSrc2->ai32[2] ? UINT32_MAX : 0;
    puDst->au32[3] = puSrc1->ai32[3] > puSrc2->ai32[3] ? UINT32_MAX : 0;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpgtd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au32[0] = puSrc1->ai32[0] > puSrc2->ai32[0] ? UINT32_MAX : 0;
    puDst->au32[1] = puSrc1->ai32[1] > puSrc2->ai32[1] ? UINT32_MAX : 0;
    puDst->au32[2] = puSrc1->ai32[2] > puSrc2->ai32[2] ? UINT32_MAX : 0;
    puDst->au32[3] = puSrc1->ai32[3] > puSrc2->ai32[3] ? UINT32_MAX : 0;
    puDst->au32[4] = puSrc1->ai32[4] > puSrc2->ai32[4] ? UINT32_MAX : 0;
    puDst->au32[5] = puSrc1->ai32[5] > puSrc2->ai32[5] ? UINT32_MAX : 0;
    puDst->au32[6] = puSrc1->ai32[6] > puSrc2->ai32[6] ? UINT32_MAX : 0;
    puDst->au32[7] = puSrc1->ai32[7] > puSrc2->ai32[7] ? UINT32_MAX : 0;
}


/*
 * PCMPGTQ / VPCMPGTQ.
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpgtq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au64[0] = uSrc1.ai64[0] > puSrc->ai64[0] ? UINT64_MAX : 0;
    puDst->au64[1] = uSrc1.ai64[1] > puSrc->ai64[1] ? UINT64_MAX : 0;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpgtq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au64[0] = puSrc1->ai64[0] > puSrc2->ai64[0] ? UINT64_MAX : 0;
    puDst->au64[1] = puSrc1->ai64[1] > puSrc2->ai64[1] ? UINT64_MAX : 0;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpgtq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au64[0] = puSrc1->ai64[0] > puSrc2->ai64[0] ? UINT64_MAX : 0;
    puDst->au64[1] = puSrc1->ai64[1] > puSrc2->ai64[1] ? UINT64_MAX : 0;
    puDst->au64[2] = puSrc1->ai64[2] > puSrc2->ai64[2] ? UINT64_MAX : 0;
    puDst->au64[3] = puSrc1->ai64[3] > puSrc2->ai64[3] ? UINT64_MAX : 0;
}


/*
 * PADDB / VPADDB
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_paddb_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au8[0] = uSrc1.au8[0] + uSrc2.au8[0];
    uDst.au8[1] = uSrc1.au8[1] + uSrc2.au8[1];
    uDst.au8[2] = uSrc1.au8[2] + uSrc2.au8[2];
    uDst.au8[3] = uSrc1.au8[3] + uSrc2.au8[3];
    uDst.au8[4] = uSrc1.au8[4] + uSrc2.au8[4];
    uDst.au8[5] = uSrc1.au8[5] + uSrc2.au8[5];
    uDst.au8[6] = uSrc1.au8[6] + uSrc2.au8[6];
    uDst.au8[7] = uSrc1.au8[7] + uSrc2.au8[7];
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_paddb_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au8[0]  = uSrc1.au8[0]  + puSrc->au8[0];
    puDst->au8[1]  = uSrc1.au8[1]  + puSrc->au8[1];
    puDst->au8[2]  = uSrc1.au8[2]  + puSrc->au8[2];
    puDst->au8[3]  = uSrc1.au8[3]  + puSrc->au8[3];
    puDst->au8[4]  = uSrc1.au8[4]  + puSrc->au8[4];
    puDst->au8[5]  = uSrc1.au8[5]  + puSrc->au8[5];
    puDst->au8[6]  = uSrc1.au8[6]  + puSrc->au8[6];
    puDst->au8[7]  = uSrc1.au8[7]  + puSrc->au8[7];
    puDst->au8[8]  = uSrc1.au8[8]  + puSrc->au8[8];
    puDst->au8[9]  = uSrc1.au8[9]  + puSrc->au8[9];
    puDst->au8[10] = uSrc1.au8[10] + puSrc->au8[10];
    puDst->au8[11] = uSrc1.au8[11] + puSrc->au8[11];
    puDst->au8[12] = uSrc1.au8[12] + puSrc->au8[12];
    puDst->au8[13] = uSrc1.au8[13] + puSrc->au8[13];
    puDst->au8[14] = uSrc1.au8[14] + puSrc->au8[14];
    puDst->au8[15] = uSrc1.au8[15] + puSrc->au8[15];
}

#endif


IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au8[0]  = puSrc1->au8[0]  + puSrc2->au8[0];
    puDst->au8[1]  = puSrc1->au8[1]  + puSrc2->au8[1];
    puDst->au8[2]  = puSrc1->au8[2]  + puSrc2->au8[2];
    puDst->au8[3]  = puSrc1->au8[3]  + puSrc2->au8[3];
    puDst->au8[4]  = puSrc1->au8[4]  + puSrc2->au8[4];
    puDst->au8[5]  = puSrc1->au8[5]  + puSrc2->au8[5];
    puDst->au8[6]  = puSrc1->au8[6]  + puSrc2->au8[6];
    puDst->au8[7]  = puSrc1->au8[7]  + puSrc2->au8[7];
    puDst->au8[8]  = puSrc1->au8[8]  + puSrc2->au8[8];
    puDst->au8[9]  = puSrc1->au8[9]  + puSrc2->au8[9];
    puDst->au8[10] = puSrc1->au8[10] + puSrc2->au8[10];
    puDst->au8[11] = puSrc1->au8[11] + puSrc2->au8[11];
    puDst->au8[12] = puSrc1->au8[12] + puSrc2->au8[12];
    puDst->au8[13] = puSrc1->au8[13] + puSrc2->au8[13];
    puDst->au8[14] = puSrc1->au8[14] + puSrc2->au8[14];
    puDst->au8[15] = puSrc1->au8[15] + puSrc2->au8[15];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddb_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au8[0]  = puSrc1->au8[0]  + puSrc2->au8[0];
    puDst->au8[1]  = puSrc1->au8[1]  + puSrc2->au8[1];
    puDst->au8[2]  = puSrc1->au8[2]  + puSrc2->au8[2];
    puDst->au8[3]  = puSrc1->au8[3]  + puSrc2->au8[3];
    puDst->au8[4]  = puSrc1->au8[4]  + puSrc2->au8[4];
    puDst->au8[5]  = puSrc1->au8[5]  + puSrc2->au8[5];
    puDst->au8[6]  = puSrc1->au8[6]  + puSrc2->au8[6];
    puDst->au8[7]  = puSrc1->au8[7]  + puSrc2->au8[7];
    puDst->au8[8]  = puSrc1->au8[8]  + puSrc2->au8[8];
    puDst->au8[9]  = puSrc1->au8[9]  + puSrc2->au8[9];
    puDst->au8[10] = puSrc1->au8[10] + puSrc2->au8[10];
    puDst->au8[11] = puSrc1->au8[11] + puSrc2->au8[11];
    puDst->au8[12] = puSrc1->au8[12] + puSrc2->au8[12];
    puDst->au8[13] = puSrc1->au8[13] + puSrc2->au8[13];
    puDst->au8[14] = puSrc1->au8[14] + puSrc2->au8[14];
    puDst->au8[15] = puSrc1->au8[15] + puSrc2->au8[15];
    puDst->au8[16] = puSrc1->au8[16] + puSrc2->au8[16];
    puDst->au8[17] = puSrc1->au8[17] + puSrc2->au8[17];
    puDst->au8[18] = puSrc1->au8[18] + puSrc2->au8[18];
    puDst->au8[19] = puSrc1->au8[19] + puSrc2->au8[19];
    puDst->au8[20] = puSrc1->au8[20] + puSrc2->au8[20];
    puDst->au8[21] = puSrc1->au8[21] + puSrc2->au8[21];
    puDst->au8[22] = puSrc1->au8[22] + puSrc2->au8[22];
    puDst->au8[23] = puSrc1->au8[23] + puSrc2->au8[23];
    puDst->au8[24] = puSrc1->au8[24] + puSrc2->au8[24];
    puDst->au8[25] = puSrc1->au8[25] + puSrc2->au8[25];
    puDst->au8[26] = puSrc1->au8[26] + puSrc2->au8[26];
    puDst->au8[27] = puSrc1->au8[27] + puSrc2->au8[27];
    puDst->au8[28] = puSrc1->au8[28] + puSrc2->au8[28];
    puDst->au8[29] = puSrc1->au8[29] + puSrc2->au8[29];
    puDst->au8[30] = puSrc1->au8[30] + puSrc2->au8[30];
    puDst->au8[31] = puSrc1->au8[31] + puSrc2->au8[31];
}


/*
 * PADDSB / VPADDSB
 */
#define SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(a_iWord) \
        ( (uint16_t)((a_iWord) + 0x80) <= (uint16_t)0xff  \
          ? (uint8_t)(a_iWord) \
          : (uint8_t)0x7f + (uint8_t)(((a_iWord) >> 15) & 1) ) /* 0x7f = INT8_MAX; 0x80 = INT8_MIN; source bit 15 = sign */

#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_paddsb_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au8[0] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[0] + uSrc2.ai8[0]);
    uDst.au8[1] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[1] + uSrc2.ai8[1]);
    uDst.au8[2] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[2] + uSrc2.ai8[2]);
    uDst.au8[3] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[3] + uSrc2.ai8[3]);
    uDst.au8[4] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[4] + uSrc2.ai8[4]);
    uDst.au8[5] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[5] + uSrc2.ai8[5]);
    uDst.au8[6] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[6] + uSrc2.ai8[6]);
    uDst.au8[7] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[7] + uSrc2.ai8[7]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_paddsb_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au8[0]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[0]  + puSrc->ai8[0]);
    puDst->au8[1]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[1]  + puSrc->ai8[1]);
    puDst->au8[2]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[2]  + puSrc->ai8[2]);
    puDst->au8[3]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[3]  + puSrc->ai8[3]);
    puDst->au8[4]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[4]  + puSrc->ai8[4]);
    puDst->au8[5]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[5]  + puSrc->ai8[5]);
    puDst->au8[6]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[6]  + puSrc->ai8[6]);
    puDst->au8[7]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[7]  + puSrc->ai8[7]);
    puDst->au8[8]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[8]  + puSrc->ai8[8]);
    puDst->au8[9]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[9]  + puSrc->ai8[9]);
    puDst->au8[10] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[10] + puSrc->ai8[10]);
    puDst->au8[11] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[11] + puSrc->ai8[11]);
    puDst->au8[12] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[12] + puSrc->ai8[12]);
    puDst->au8[13] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[13] + puSrc->ai8[13]);
    puDst->au8[14] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[14] + puSrc->ai8[14]);
    puDst->au8[15] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[15] + puSrc->ai8[15]);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddsb_u128_fallback,(PRTUINT128U puDst,
                                                        PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au8[0]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[0]  + puSrc2->ai8[0]);
    puDst->au8[1]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[1]  + puSrc2->ai8[1]);
    puDst->au8[2]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[2]  + puSrc2->ai8[2]);
    puDst->au8[3]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[3]  + puSrc2->ai8[3]);
    puDst->au8[4]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[4]  + puSrc2->ai8[4]);
    puDst->au8[5]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[5]  + puSrc2->ai8[5]);
    puDst->au8[6]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[6]  + puSrc2->ai8[6]);
    puDst->au8[7]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[7]  + puSrc2->ai8[7]);
    puDst->au8[8]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[8]  + puSrc2->ai8[8]);
    puDst->au8[9]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[9]  + puSrc2->ai8[9]);
    puDst->au8[10] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[10] + puSrc2->ai8[10]);
    puDst->au8[11] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[11] + puSrc2->ai8[11]);
    puDst->au8[12] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[12] + puSrc2->ai8[12]);
    puDst->au8[13] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[13] + puSrc2->ai8[13]);
    puDst->au8[14] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[14] + puSrc2->ai8[14]);
    puDst->au8[15] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[15] + puSrc2->ai8[15]);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddsb_u256_fallback,(PRTUINT256U puDst,
                                                        PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au8[0]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[0]  + puSrc2->ai8[0]);
    puDst->au8[1]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[1]  + puSrc2->ai8[1]);
    puDst->au8[2]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[2]  + puSrc2->ai8[2]);
    puDst->au8[3]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[3]  + puSrc2->ai8[3]);
    puDst->au8[4]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[4]  + puSrc2->ai8[4]);
    puDst->au8[5]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[5]  + puSrc2->ai8[5]);
    puDst->au8[6]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[6]  + puSrc2->ai8[6]);
    puDst->au8[7]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[7]  + puSrc2->ai8[7]);
    puDst->au8[8]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[8]  + puSrc2->ai8[8]);
    puDst->au8[9]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[9]  + puSrc2->ai8[9]);
    puDst->au8[10] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[10] + puSrc2->ai8[10]);
    puDst->au8[11] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[11] + puSrc2->ai8[11]);
    puDst->au8[12] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[12] + puSrc2->ai8[12]);
    puDst->au8[13] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[13] + puSrc2->ai8[13]);
    puDst->au8[14] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[14] + puSrc2->ai8[14]);
    puDst->au8[15] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[15] + puSrc2->ai8[15]);
    puDst->au8[16] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[16] + puSrc2->ai8[16]);
    puDst->au8[17] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[17] + puSrc2->ai8[17]);
    puDst->au8[18] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[18] + puSrc2->ai8[18]);
    puDst->au8[19] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[19] + puSrc2->ai8[19]);
    puDst->au8[20] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[20] + puSrc2->ai8[20]);
    puDst->au8[21] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[21] + puSrc2->ai8[21]);
    puDst->au8[22] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[22] + puSrc2->ai8[22]);
    puDst->au8[23] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[23] + puSrc2->ai8[23]);
    puDst->au8[24] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[24] + puSrc2->ai8[24]);
    puDst->au8[25] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[25] + puSrc2->ai8[25]);
    puDst->au8[26] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[26] + puSrc2->ai8[26]);
    puDst->au8[27] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[27] + puSrc2->ai8[27]);
    puDst->au8[28] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[28] + puSrc2->ai8[28]);
    puDst->au8[29] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[29] + puSrc2->ai8[29]);
    puDst->au8[30] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[30] + puSrc2->ai8[30]);
    puDst->au8[31] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[31] + puSrc2->ai8[31]);
}


/*
 * PADDUSB / VPADDUSB
 */
#define SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(a_uWord) \
        ( (uint16_t)(a_uWord) <= (uint16_t)0xff  \
          ? (uint8_t)(a_uWord) \
          : (uint8_t)0xff ) /* 0xff = UINT8_MAX */

#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_paddusb_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au8[0] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[0] + uSrc2.au8[0]);
    uDst.au8[1] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[1] + uSrc2.au8[1]);
    uDst.au8[2] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[2] + uSrc2.au8[2]);
    uDst.au8[3] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[3] + uSrc2.au8[3]);
    uDst.au8[4] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[4] + uSrc2.au8[4]);
    uDst.au8[5] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[5] + uSrc2.au8[5]);
    uDst.au8[6] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[6] + uSrc2.au8[6]);
    uDst.au8[7] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[7] + uSrc2.au8[7]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_paddusb_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au8[0]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[0]  + puSrc->au8[0]);
    puDst->au8[1]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[1]  + puSrc->au8[1]);
    puDst->au8[2]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[2]  + puSrc->au8[2]);
    puDst->au8[3]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[3]  + puSrc->au8[3]);
    puDst->au8[4]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[4]  + puSrc->au8[4]);
    puDst->au8[5]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[5]  + puSrc->au8[5]);
    puDst->au8[6]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[6]  + puSrc->au8[6]);
    puDst->au8[7]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[7]  + puSrc->au8[7]);
    puDst->au8[8]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[8]  + puSrc->au8[8]);
    puDst->au8[9]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[9]  + puSrc->au8[9]);
    puDst->au8[10] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[10] + puSrc->au8[10]);
    puDst->au8[11] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[11] + puSrc->au8[11]);
    puDst->au8[12] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[12] + puSrc->au8[12]);
    puDst->au8[13] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[13] + puSrc->au8[13]);
    puDst->au8[14] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[14] + puSrc->au8[14]);
    puDst->au8[15] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au8[15] + puSrc->au8[15]);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddusb_u128_fallback,(PRTUINT128U puDst,
                                                         PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au8[0]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[0]  + puSrc2->au8[0]);
    puDst->au8[1]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[1]  + puSrc2->au8[1]);
    puDst->au8[2]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[2]  + puSrc2->au8[2]);
    puDst->au8[3]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[3]  + puSrc2->au8[3]);
    puDst->au8[4]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[4]  + puSrc2->au8[4]);
    puDst->au8[5]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[5]  + puSrc2->au8[5]);
    puDst->au8[6]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[6]  + puSrc2->au8[6]);
    puDst->au8[7]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[7]  + puSrc2->au8[7]);
    puDst->au8[8]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[8]  + puSrc2->au8[8]);
    puDst->au8[9]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[9]  + puSrc2->au8[9]);
    puDst->au8[10] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[10] + puSrc2->au8[10]);
    puDst->au8[11] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[11] + puSrc2->au8[11]);
    puDst->au8[12] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[12] + puSrc2->au8[12]);
    puDst->au8[13] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[13] + puSrc2->au8[13]);
    puDst->au8[14] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[14] + puSrc2->au8[14]);
    puDst->au8[15] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[15] + puSrc2->au8[15]);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddusb_u256_fallback,(PRTUINT256U puDst,
                                                         PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au8[0]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[0]  + puSrc2->au8[0]);
    puDst->au8[1]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[1]  + puSrc2->au8[1]);
    puDst->au8[2]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[2]  + puSrc2->au8[2]);
    puDst->au8[3]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[3]  + puSrc2->au8[3]);
    puDst->au8[4]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[4]  + puSrc2->au8[4]);
    puDst->au8[5]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[5]  + puSrc2->au8[5]);
    puDst->au8[6]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[6]  + puSrc2->au8[6]);
    puDst->au8[7]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[7]  + puSrc2->au8[7]);
    puDst->au8[8]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[8]  + puSrc2->au8[8]);
    puDst->au8[9]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[9]  + puSrc2->au8[9]);
    puDst->au8[10] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[10] + puSrc2->au8[10]);
    puDst->au8[11] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[11] + puSrc2->au8[11]);
    puDst->au8[12] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[12] + puSrc2->au8[12]);
    puDst->au8[13] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[13] + puSrc2->au8[13]);
    puDst->au8[14] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[14] + puSrc2->au8[14]);
    puDst->au8[15] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[15] + puSrc2->au8[15]);
    puDst->au8[16] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[16] + puSrc2->au8[16]);
    puDst->au8[17] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[17] + puSrc2->au8[17]);
    puDst->au8[18] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[18] + puSrc2->au8[18]);
    puDst->au8[19] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[19] + puSrc2->au8[19]);
    puDst->au8[20] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[20] + puSrc2->au8[20]);
    puDst->au8[21] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[21] + puSrc2->au8[21]);
    puDst->au8[22] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[22] + puSrc2->au8[22]);
    puDst->au8[23] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[23] + puSrc2->au8[23]);
    puDst->au8[24] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[24] + puSrc2->au8[24]);
    puDst->au8[25] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[25] + puSrc2->au8[25]);
    puDst->au8[26] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[26] + puSrc2->au8[26]);
    puDst->au8[27] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[27] + puSrc2->au8[27]);
    puDst->au8[28] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[28] + puSrc2->au8[28]);
    puDst->au8[29] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[29] + puSrc2->au8[29]);
    puDst->au8[30] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[30] + puSrc2->au8[30]);
    puDst->au8[31] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE(puSrc1->au8[31] + puSrc2->au8[31]);
}


/*
 * PADDW / VPADDW
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_paddw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au16[0] = uSrc1.au16[0] + uSrc2.au16[0];
    uDst.au16[1] = uSrc1.au16[1] + uSrc2.au16[1];
    uDst.au16[2] = uSrc1.au16[2] + uSrc2.au16[2];
    uDst.au16[3] = uSrc1.au16[3] + uSrc2.au16[3];
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_paddw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au16[0] = uSrc1.au16[0] + puSrc->au16[0];
    puDst->au16[1] = uSrc1.au16[1] + puSrc->au16[1];
    puDst->au16[2] = uSrc1.au16[2] + puSrc->au16[2];
    puDst->au16[3] = uSrc1.au16[3] + puSrc->au16[3];
    puDst->au16[4] = uSrc1.au16[4] + puSrc->au16[4];
    puDst->au16[5] = uSrc1.au16[5] + puSrc->au16[5];
    puDst->au16[6] = uSrc1.au16[6] + puSrc->au16[6];
    puDst->au16[7] = uSrc1.au16[7] + puSrc->au16[7];
}

#endif


IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au16[0] = puSrc1->au16[0] + puSrc2->au16[0];
    puDst->au16[1] = puSrc1->au16[1] + puSrc2->au16[1];
    puDst->au16[2] = puSrc1->au16[2] + puSrc2->au16[2];
    puDst->au16[3] = puSrc1->au16[3] + puSrc2->au16[3];
    puDst->au16[4] = puSrc1->au16[4] + puSrc2->au16[4];
    puDst->au16[5] = puSrc1->au16[5] + puSrc2->au16[5];
    puDst->au16[6] = puSrc1->au16[6] + puSrc2->au16[6];
    puDst->au16[7] = puSrc1->au16[7] + puSrc2->au16[7];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au16[0]  = puSrc1->au16[0]  + puSrc2->au16[0];
    puDst->au16[1]  = puSrc1->au16[1]  + puSrc2->au16[1];
    puDst->au16[2]  = puSrc1->au16[2]  + puSrc2->au16[2];
    puDst->au16[3]  = puSrc1->au16[3]  + puSrc2->au16[3];
    puDst->au16[4]  = puSrc1->au16[4]  + puSrc2->au16[4];
    puDst->au16[5]  = puSrc1->au16[5]  + puSrc2->au16[5];
    puDst->au16[6]  = puSrc1->au16[6]  + puSrc2->au16[6];
    puDst->au16[7]  = puSrc1->au16[7]  + puSrc2->au16[7];
    puDst->au16[8]  = puSrc1->au16[8]  + puSrc2->au16[8];
    puDst->au16[9]  = puSrc1->au16[9]  + puSrc2->au16[9];
    puDst->au16[10] = puSrc1->au16[10] + puSrc2->au16[10];
    puDst->au16[11] = puSrc1->au16[11] + puSrc2->au16[11];
    puDst->au16[12] = puSrc1->au16[12] + puSrc2->au16[12];
    puDst->au16[13] = puSrc1->au16[13] + puSrc2->au16[13];
    puDst->au16[14] = puSrc1->au16[14] + puSrc2->au16[14];
    puDst->au16[15] = puSrc1->au16[15] + puSrc2->au16[15];
}


/*
 * PADDSW / VPADDSW
 */
#define SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(a_iDword) \
        ( (uint32_t)((a_iDword) + 0x8000) <= (uint16_t)0xffff  \
          ? (uint16_t)(a_iDword) \
          : (uint16_t)0x7fff + (uint16_t)(((a_iDword) >> 31) & 1) ) /* 0x7fff = INT16_MAX; 0x8000 = INT16_MIN; source bit 31 = sign */

#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_paddsw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[0] + uSrc2.ai16[0]);
    uDst.au16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[1] + uSrc2.ai16[1]);
    uDst.au16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[2] + uSrc2.ai16[2]);
    uDst.au16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[3] + uSrc2.ai16[3]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_paddsw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[0] + puSrc->ai16[0]);
    puDst->au16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[1] + puSrc->ai16[1]);
    puDst->au16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[2] + puSrc->ai16[2]);
    puDst->au16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[3] + puSrc->ai16[3]);
    puDst->au16[4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[4] + puSrc->ai16[4]);
    puDst->au16[5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[5] + puSrc->ai16[5]);
    puDst->au16[6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[6] + puSrc->ai16[6]);
    puDst->au16[7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[7] + puSrc->ai16[7]);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddsw_u128_fallback,(PRTUINT128U puDst,
                                                        PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[0] + puSrc2->ai16[0]);
    puDst->au16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[1] + puSrc2->ai16[1]);
    puDst->au16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[2] + puSrc2->ai16[2]);
    puDst->au16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[3] + puSrc2->ai16[3]);
    puDst->au16[4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[4] + puSrc2->ai16[4]);
    puDst->au16[5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[5] + puSrc2->ai16[5]);
    puDst->au16[6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[6] + puSrc2->ai16[6]);
    puDst->au16[7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[7] + puSrc2->ai16[7]);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddsw_u256_fallback,(PRTUINT256U puDst,
                                                        PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au16[0]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[0]  + puSrc2->ai16[0]);
    puDst->au16[1]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[1]  + puSrc2->ai16[1]);
    puDst->au16[2]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[2]  + puSrc2->ai16[2]);
    puDst->au16[3]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[3]  + puSrc2->ai16[3]);
    puDst->au16[4]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[4]  + puSrc2->ai16[4]);
    puDst->au16[5]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[5]  + puSrc2->ai16[5]);
    puDst->au16[6]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[6]  + puSrc2->ai16[6]);
    puDst->au16[7]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[7]  + puSrc2->ai16[7]);
    puDst->au16[8]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[8]  + puSrc2->ai16[8]);
    puDst->au16[9]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[9]  + puSrc2->ai16[9]);
    puDst->au16[10] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[10] + puSrc2->ai16[10]);
    puDst->au16[11] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[11] + puSrc2->ai16[11]);
    puDst->au16[12] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[12] + puSrc2->ai16[12]);
    puDst->au16[13] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[13] + puSrc2->ai16[13]);
    puDst->au16[14] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[14] + puSrc2->ai16[14]);
    puDst->au16[15] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[15] + puSrc2->ai16[15]);
}


/*
 * PADDUSW / VPADDUSW
 */
#define SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(a_uDword) \
        ( (uint32_t)(a_uDword) <= (uint16_t)0xffff  \
          ? (uint16_t)(a_uDword) \
          : (uint16_t)0xffff ) /* 0xffff = UINT16_MAX */

#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_paddusw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au16[0] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au16[0] + uSrc2.au16[0]);
    uDst.au16[1] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au16[1] + uSrc2.au16[1]);
    uDst.au16[2] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au16[2] + uSrc2.au16[2]);
    uDst.au16[3] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au16[3] + uSrc2.au16[3]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_paddusw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au16[0] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au16[0] + puSrc->au16[0]);
    puDst->au16[1] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au16[1] + puSrc->au16[1]);
    puDst->au16[2] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au16[2] + puSrc->au16[2]);
    puDst->au16[3] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au16[3] + puSrc->au16[3]);
    puDst->au16[4] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au16[4] + puSrc->au16[4]);
    puDst->au16[5] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au16[5] + puSrc->au16[5]);
    puDst->au16[6] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au16[6] + puSrc->au16[6]);
    puDst->au16[7] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au16[7] + puSrc->au16[7]);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddusw_u128_fallback,(PRTUINT128U puDst,
                                                         PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au16[0] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[0] + puSrc2->au16[0]);
    puDst->au16[1] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[1] + puSrc2->au16[1]);
    puDst->au16[2] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[2] + puSrc2->au16[2]);
    puDst->au16[3] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[3] + puSrc2->au16[3]);
    puDst->au16[4] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[4] + puSrc2->au16[4]);
    puDst->au16[5] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[5] + puSrc2->au16[5]);
    puDst->au16[6] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[6] + puSrc2->au16[6]);
    puDst->au16[7] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[7] + puSrc2->au16[7]);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddusw_u256_fallback,(PRTUINT256U puDst,
                                                         PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au16[0]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[0]  + puSrc2->au16[0]);
    puDst->au16[1]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[1]  + puSrc2->au16[1]);
    puDst->au16[2]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[2]  + puSrc2->au16[2]);
    puDst->au16[3]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[3]  + puSrc2->au16[3]);
    puDst->au16[4]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[4]  + puSrc2->au16[4]);
    puDst->au16[5]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[5]  + puSrc2->au16[5]);
    puDst->au16[6]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[6]  + puSrc2->au16[6]);
    puDst->au16[7]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[7]  + puSrc2->au16[7]);
    puDst->au16[8]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[8]  + puSrc2->au16[8]);
    puDst->au16[9]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[9]  + puSrc2->au16[9]);
    puDst->au16[10] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[10] + puSrc2->au16[10]);
    puDst->au16[11] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[11] + puSrc2->au16[11]);
    puDst->au16[12] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[12] + puSrc2->au16[12]);
    puDst->au16[13] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[13] + puSrc2->au16[13]);
    puDst->au16[14] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[14] + puSrc2->au16[14]);
    puDst->au16[15] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD(puSrc1->au16[15] + puSrc2->au16[15]);
}


/*
 * PADDD / VPADDD.
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_paddd_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au32[0] = uSrc1.au32[0] + uSrc2.au32[0];
    uDst.au32[1] = uSrc1.au32[1] + uSrc2.au32[1];
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_paddd_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au32[0] = uSrc1.au32[0] + puSrc->au32[0];
    puDst->au32[1] = uSrc1.au32[1] + puSrc->au32[1];
    puDst->au32[2] = uSrc1.au32[2] + puSrc->au32[2];
    puDst->au32[3] = uSrc1.au32[3] + puSrc->au32[3];
}

#endif /* IEM_WITHOUT_ASSEMBLY */

IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au32[0] = puSrc1->au32[0] + puSrc2->au32[0];
    puDst->au32[1] = puSrc1->au32[1] + puSrc2->au32[1];
    puDst->au32[2] = puSrc1->au32[2] + puSrc2->au32[2];
    puDst->au32[3] = puSrc1->au32[3] + puSrc2->au32[3];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au32[0] = puSrc1->au32[0] + puSrc2->au32[0];
    puDst->au32[1] = puSrc1->au32[1] + puSrc2->au32[1];
    puDst->au32[2] = puSrc1->au32[2] + puSrc2->au32[2];
    puDst->au32[3] = puSrc1->au32[3] + puSrc2->au32[3];
    puDst->au32[4] = puSrc1->au32[4] + puSrc2->au32[4];
    puDst->au32[5] = puSrc1->au32[5] + puSrc2->au32[5];
    puDst->au32[6] = puSrc1->au32[6] + puSrc2->au32[6];
    puDst->au32[7] = puSrc1->au32[7] + puSrc2->au32[7];
}


/*
 * PADDQ / VPADDQ.
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_paddq_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    *puDst = *puDst + *puSrc;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_paddq_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au64[0] = uSrc1.au64[0] + puSrc->au64[0];
    puDst->au64[1] = uSrc1.au64[1] + puSrc->au64[1];
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au64[0] = puSrc1->au64[0] + puSrc2->au64[0];
    puDst->au64[1] = puSrc1->au64[1] + puSrc2->au64[1];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpaddq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au64[0] = puSrc1->au64[0] + puSrc2->au64[0];
    puDst->au64[1] = puSrc1->au64[1] + puSrc2->au64[1];
    puDst->au64[2] = puSrc1->au64[2] + puSrc2->au64[2];
    puDst->au64[3] = puSrc1->au64[3] + puSrc2->au64[3];
}


/*
 * PSUBB / VPSUBB
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psubb_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au8[0] = uSrc1.au8[0] - uSrc2.au8[0];
    uDst.au8[1] = uSrc1.au8[1] - uSrc2.au8[1];
    uDst.au8[2] = uSrc1.au8[2] - uSrc2.au8[2];
    uDst.au8[3] = uSrc1.au8[3] - uSrc2.au8[3];
    uDst.au8[4] = uSrc1.au8[4] - uSrc2.au8[4];
    uDst.au8[5] = uSrc1.au8[5] - uSrc2.au8[5];
    uDst.au8[6] = uSrc1.au8[6] - uSrc2.au8[6];
    uDst.au8[7] = uSrc1.au8[7] - uSrc2.au8[7];
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psubb_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au8[0]  = uSrc1.au8[0]  - puSrc->au8[0];
    puDst->au8[1]  = uSrc1.au8[1]  - puSrc->au8[1];
    puDst->au8[2]  = uSrc1.au8[2]  - puSrc->au8[2];
    puDst->au8[3]  = uSrc1.au8[3]  - puSrc->au8[3];
    puDst->au8[4]  = uSrc1.au8[4]  - puSrc->au8[4];
    puDst->au8[5]  = uSrc1.au8[5]  - puSrc->au8[5];
    puDst->au8[6]  = uSrc1.au8[6]  - puSrc->au8[6];
    puDst->au8[7]  = uSrc1.au8[7]  - puSrc->au8[7];
    puDst->au8[8]  = uSrc1.au8[8]  - puSrc->au8[8];
    puDst->au8[9]  = uSrc1.au8[9]  - puSrc->au8[9];
    puDst->au8[10] = uSrc1.au8[10] - puSrc->au8[10];
    puDst->au8[11] = uSrc1.au8[11] - puSrc->au8[11];
    puDst->au8[12] = uSrc1.au8[12] - puSrc->au8[12];
    puDst->au8[13] = uSrc1.au8[13] - puSrc->au8[13];
    puDst->au8[14] = uSrc1.au8[14] - puSrc->au8[14];
    puDst->au8[15] = uSrc1.au8[15] - puSrc->au8[15];
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au8[0]  = puSrc1->au8[0]  - puSrc2->au8[0];
    puDst->au8[1]  = puSrc1->au8[1]  - puSrc2->au8[1];
    puDst->au8[2]  = puSrc1->au8[2]  - puSrc2->au8[2];
    puDst->au8[3]  = puSrc1->au8[3]  - puSrc2->au8[3];
    puDst->au8[4]  = puSrc1->au8[4]  - puSrc2->au8[4];
    puDst->au8[5]  = puSrc1->au8[5]  - puSrc2->au8[5];
    puDst->au8[6]  = puSrc1->au8[6]  - puSrc2->au8[6];
    puDst->au8[7]  = puSrc1->au8[7]  - puSrc2->au8[7];
    puDst->au8[8]  = puSrc1->au8[8]  - puSrc2->au8[8];
    puDst->au8[9]  = puSrc1->au8[9]  - puSrc2->au8[9];
    puDst->au8[10] = puSrc1->au8[10] - puSrc2->au8[10];
    puDst->au8[11] = puSrc1->au8[11] - puSrc2->au8[11];
    puDst->au8[12] = puSrc1->au8[12] - puSrc2->au8[12];
    puDst->au8[13] = puSrc1->au8[13] - puSrc2->au8[13];
    puDst->au8[14] = puSrc1->au8[14] - puSrc2->au8[14];
    puDst->au8[15] = puSrc1->au8[15] - puSrc2->au8[15];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubb_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au8[0]  = puSrc1->au8[0]  - puSrc2->au8[0];
    puDst->au8[1]  = puSrc1->au8[1]  - puSrc2->au8[1];
    puDst->au8[2]  = puSrc1->au8[2]  - puSrc2->au8[2];
    puDst->au8[3]  = puSrc1->au8[3]  - puSrc2->au8[3];
    puDst->au8[4]  = puSrc1->au8[4]  - puSrc2->au8[4];
    puDst->au8[5]  = puSrc1->au8[5]  - puSrc2->au8[5];
    puDst->au8[6]  = puSrc1->au8[6]  - puSrc2->au8[6];
    puDst->au8[7]  = puSrc1->au8[7]  - puSrc2->au8[7];
    puDst->au8[8]  = puSrc1->au8[8]  - puSrc2->au8[8];
    puDst->au8[9]  = puSrc1->au8[9]  - puSrc2->au8[9];
    puDst->au8[10] = puSrc1->au8[10] - puSrc2->au8[10];
    puDst->au8[11] = puSrc1->au8[11] - puSrc2->au8[11];
    puDst->au8[12] = puSrc1->au8[12] - puSrc2->au8[12];
    puDst->au8[13] = puSrc1->au8[13] - puSrc2->au8[13];
    puDst->au8[14] = puSrc1->au8[14] - puSrc2->au8[14];
    puDst->au8[15] = puSrc1->au8[15] - puSrc2->au8[15];
    puDst->au8[16] = puSrc1->au8[16] - puSrc2->au8[16];
    puDst->au8[17] = puSrc1->au8[17] - puSrc2->au8[17];
    puDst->au8[18] = puSrc1->au8[18] - puSrc2->au8[18];
    puDst->au8[19] = puSrc1->au8[19] - puSrc2->au8[19];
    puDst->au8[20] = puSrc1->au8[20] - puSrc2->au8[20];
    puDst->au8[21] = puSrc1->au8[21] - puSrc2->au8[21];
    puDst->au8[22] = puSrc1->au8[22] - puSrc2->au8[22];
    puDst->au8[23] = puSrc1->au8[23] - puSrc2->au8[23];
    puDst->au8[24] = puSrc1->au8[24] - puSrc2->au8[24];
    puDst->au8[25] = puSrc1->au8[25] - puSrc2->au8[25];
    puDst->au8[26] = puSrc1->au8[26] - puSrc2->au8[26];
    puDst->au8[27] = puSrc1->au8[27] - puSrc2->au8[27];
    puDst->au8[28] = puSrc1->au8[28] - puSrc2->au8[28];
    puDst->au8[29] = puSrc1->au8[29] - puSrc2->au8[29];
    puDst->au8[30] = puSrc1->au8[30] - puSrc2->au8[30];
    puDst->au8[31] = puSrc1->au8[31] - puSrc2->au8[31];
}


/*
 * PSUBSB / VSUBSB
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psubsb_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au8[0] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[0] - uSrc2.ai8[0]);
    uDst.au8[1] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[1] - uSrc2.ai8[1]);
    uDst.au8[2] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[2] - uSrc2.ai8[2]);
    uDst.au8[3] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[3] - uSrc2.ai8[3]);
    uDst.au8[4] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[4] - uSrc2.ai8[4]);
    uDst.au8[5] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[5] - uSrc2.ai8[5]);
    uDst.au8[6] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[6] - uSrc2.ai8[6]);
    uDst.au8[7] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[7] - uSrc2.ai8[7]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psubsb_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au8[0]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[0]  - puSrc->ai8[0]);
    puDst->au8[1]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[1]  - puSrc->ai8[1]);
    puDst->au8[2]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[2]  - puSrc->ai8[2]);
    puDst->au8[3]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[3]  - puSrc->ai8[3]);
    puDst->au8[4]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[4]  - puSrc->ai8[4]);
    puDst->au8[5]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[5]  - puSrc->ai8[5]);
    puDst->au8[6]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[6]  - puSrc->ai8[6]);
    puDst->au8[7]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[7]  - puSrc->ai8[7]);
    puDst->au8[8]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[8]  - puSrc->ai8[8]);
    puDst->au8[9]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[9]  - puSrc->ai8[9]);
    puDst->au8[10] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[10] - puSrc->ai8[10]);
    puDst->au8[11] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[11] - puSrc->ai8[11]);
    puDst->au8[12] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[12] - puSrc->ai8[12]);
    puDst->au8[13] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[13] - puSrc->ai8[13]);
    puDst->au8[14] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[14] - puSrc->ai8[14]);
    puDst->au8[15] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.ai8[15] - puSrc->ai8[15]);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubsb_u128_fallback,(PRTUINT128U puDst,
                                                        PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au8[0]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[0]  - puSrc2->ai8[0]);
    puDst->au8[1]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[1]  - puSrc2->ai8[1]);
    puDst->au8[2]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[2]  - puSrc2->ai8[2]);
    puDst->au8[3]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[3]  - puSrc2->ai8[3]);
    puDst->au8[4]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[4]  - puSrc2->ai8[4]);
    puDst->au8[5]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[5]  - puSrc2->ai8[5]);
    puDst->au8[6]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[6]  - puSrc2->ai8[6]);
    puDst->au8[7]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[7]  - puSrc2->ai8[7]);
    puDst->au8[8]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[8]  - puSrc2->ai8[8]);
    puDst->au8[9]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[9]  - puSrc2->ai8[9]);
    puDst->au8[10] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[10] - puSrc2->ai8[10]);
    puDst->au8[11] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[11] - puSrc2->ai8[11]);
    puDst->au8[12] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[12] - puSrc2->ai8[12]);
    puDst->au8[13] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[13] - puSrc2->ai8[13]);
    puDst->au8[14] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[14] - puSrc2->ai8[14]);
    puDst->au8[15] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[15] - puSrc2->ai8[15]);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubsb_u256_fallback,(PRTUINT256U puDst,
                                                        PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au8[0]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[0]  - puSrc2->ai8[0]);
    puDst->au8[1]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[1]  - puSrc2->ai8[1]);
    puDst->au8[2]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[2]  - puSrc2->ai8[2]);
    puDst->au8[3]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[3]  - puSrc2->ai8[3]);
    puDst->au8[4]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[4]  - puSrc2->ai8[4]);
    puDst->au8[5]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[5]  - puSrc2->ai8[5]);
    puDst->au8[6]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[6]  - puSrc2->ai8[6]);
    puDst->au8[7]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[7]  - puSrc2->ai8[7]);
    puDst->au8[8]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[8]  - puSrc2->ai8[8]);
    puDst->au8[9]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[9]  - puSrc2->ai8[9]);
    puDst->au8[10] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[10] - puSrc2->ai8[10]);
    puDst->au8[11] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[11] - puSrc2->ai8[11]);
    puDst->au8[12] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[12] - puSrc2->ai8[12]);
    puDst->au8[13] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[13] - puSrc2->ai8[13]);
    puDst->au8[14] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[14] - puSrc2->ai8[14]);
    puDst->au8[15] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[15] - puSrc2->ai8[15]);
    puDst->au8[16] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[16] - puSrc2->ai8[16]);
    puDst->au8[17] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[17] - puSrc2->ai8[17]);
    puDst->au8[18] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[18] - puSrc2->ai8[18]);
    puDst->au8[19] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[19] - puSrc2->ai8[19]);
    puDst->au8[20] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[20] - puSrc2->ai8[20]);
    puDst->au8[21] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[21] - puSrc2->ai8[21]);
    puDst->au8[22] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[22] - puSrc2->ai8[22]);
    puDst->au8[23] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[23] - puSrc2->ai8[23]);
    puDst->au8[24] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[24] - puSrc2->ai8[24]);
    puDst->au8[25] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[25] - puSrc2->ai8[25]);
    puDst->au8[26] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[26] - puSrc2->ai8[26]);
    puDst->au8[27] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[27] - puSrc2->ai8[27]);
    puDst->au8[28] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[28] - puSrc2->ai8[28]);
    puDst->au8[29] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[29] - puSrc2->ai8[29]);
    puDst->au8[30] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[30] - puSrc2->ai8[30]);
    puDst->au8[31] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(puSrc1->ai8[31] - puSrc2->ai8[31]);
}


/*
 * PSUBUSB / VPSUBUSW
 */
#define SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(a_uWord) \
        ( (uint16_t)(a_uWord) <= (uint16_t)0xff  \
          ? (uint8_t)(a_uWord) \
          : (uint8_t)0 )

#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psubusb_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au8[0] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[0] - uSrc2.au8[0]);
    uDst.au8[1] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[1] - uSrc2.au8[1]);
    uDst.au8[2] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[2] - uSrc2.au8[2]);
    uDst.au8[3] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[3] - uSrc2.au8[3]);
    uDst.au8[4] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[4] - uSrc2.au8[4]);
    uDst.au8[5] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[5] - uSrc2.au8[5]);
    uDst.au8[6] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[6] - uSrc2.au8[6]);
    uDst.au8[7] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[7] - uSrc2.au8[7]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psubusb_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au8[0]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[0]  - puSrc->au8[0]);
    puDst->au8[1]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[1]  - puSrc->au8[1]);
    puDst->au8[2]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[2]  - puSrc->au8[2]);
    puDst->au8[3]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[3]  - puSrc->au8[3]);
    puDst->au8[4]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[4]  - puSrc->au8[4]);
    puDst->au8[5]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[5]  - puSrc->au8[5]);
    puDst->au8[6]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[6]  - puSrc->au8[6]);
    puDst->au8[7]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[7]  - puSrc->au8[7]);
    puDst->au8[8]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[8]  - puSrc->au8[8]);
    puDst->au8[9]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[9]  - puSrc->au8[9]);
    puDst->au8[10] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[10] - puSrc->au8[10]);
    puDst->au8[11] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[11] - puSrc->au8[11]);
    puDst->au8[12] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[12] - puSrc->au8[12]);
    puDst->au8[13] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[13] - puSrc->au8[13]);
    puDst->au8[14] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[14] - puSrc->au8[14]);
    puDst->au8[15] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(uSrc1.au8[15] - puSrc->au8[15]);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubusb_u128_fallback,(PRTUINT128U puDst,
                                                         PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au8[0]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[0]  - puSrc2->au8[0]);
    puDst->au8[1]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[1]  - puSrc2->au8[1]);
    puDst->au8[2]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[2]  - puSrc2->au8[2]);
    puDst->au8[3]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[3]  - puSrc2->au8[3]);
    puDst->au8[4]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[4]  - puSrc2->au8[4]);
    puDst->au8[5]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[5]  - puSrc2->au8[5]);
    puDst->au8[6]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[6]  - puSrc2->au8[6]);
    puDst->au8[7]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[7]  - puSrc2->au8[7]);
    puDst->au8[8]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[8]  - puSrc2->au8[8]);
    puDst->au8[9]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[9]  - puSrc2->au8[9]);
    puDst->au8[10] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[10] - puSrc2->au8[10]);
    puDst->au8[11] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[11] - puSrc2->au8[11]);
    puDst->au8[12] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[12] - puSrc2->au8[12]);
    puDst->au8[13] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[13] - puSrc2->au8[13]);
    puDst->au8[14] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[14] - puSrc2->au8[14]);
    puDst->au8[15] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[15] - puSrc2->au8[15]);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubusb_u256_fallback,(PRTUINT256U puDst,
                                                         PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au8[0]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[0]  - puSrc2->au8[0]);
    puDst->au8[1]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[1]  - puSrc2->au8[1]);
    puDst->au8[2]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[2]  - puSrc2->au8[2]);
    puDst->au8[3]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[3]  - puSrc2->au8[3]);
    puDst->au8[4]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[4]  - puSrc2->au8[4]);
    puDst->au8[5]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[5]  - puSrc2->au8[5]);
    puDst->au8[6]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[6]  - puSrc2->au8[6]);
    puDst->au8[7]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[7]  - puSrc2->au8[7]);
    puDst->au8[8]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[8]  - puSrc2->au8[8]);
    puDst->au8[9]  = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[9]  - puSrc2->au8[9]);
    puDst->au8[10] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[10] - puSrc2->au8[10]);
    puDst->au8[11] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[11] - puSrc2->au8[11]);
    puDst->au8[12] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[12] - puSrc2->au8[12]);
    puDst->au8[13] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[13] - puSrc2->au8[13]);
    puDst->au8[14] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[14] - puSrc2->au8[14]);
    puDst->au8[15] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[15] - puSrc2->au8[15]);
    puDst->au8[16] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[16] - puSrc2->au8[16]);
    puDst->au8[17] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[17] - puSrc2->au8[17]);
    puDst->au8[18] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[18] - puSrc2->au8[18]);
    puDst->au8[19] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[19] - puSrc2->au8[19]);
    puDst->au8[20] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[20] - puSrc2->au8[20]);
    puDst->au8[21] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[21] - puSrc2->au8[21]);
    puDst->au8[22] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[22] - puSrc2->au8[22]);
    puDst->au8[23] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[23] - puSrc2->au8[23]);
    puDst->au8[24] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[24] - puSrc2->au8[24]);
    puDst->au8[25] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[25] - puSrc2->au8[25]);
    puDst->au8[26] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[26] - puSrc2->au8[26]);
    puDst->au8[27] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[27] - puSrc2->au8[27]);
    puDst->au8[28] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[28] - puSrc2->au8[28]);
    puDst->au8[29] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[29] - puSrc2->au8[29]);
    puDst->au8[30] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[30] - puSrc2->au8[30]);
    puDst->au8[31] = SATURATED_UNSIGNED_WORD_TO_UNSIGNED_BYTE_SUB(puSrc1->au8[31] - puSrc2->au8[31]);
}


/*
 * PSUBW / VPSUBW
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psubw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au16[0] = uSrc1.au16[0] - uSrc2.au16[0];
    uDst.au16[1] = uSrc1.au16[1] - uSrc2.au16[1];
    uDst.au16[2] = uSrc1.au16[2] - uSrc2.au16[2];
    uDst.au16[3] = uSrc1.au16[3] - uSrc2.au16[3];
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psubw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au16[0] = uSrc1.au16[0] - puSrc->au16[0];
    puDst->au16[1] = uSrc1.au16[1] - puSrc->au16[1];
    puDst->au16[2] = uSrc1.au16[2] - puSrc->au16[2];
    puDst->au16[3] = uSrc1.au16[3] - puSrc->au16[3];
    puDst->au16[4] = uSrc1.au16[4] - puSrc->au16[4];
    puDst->au16[5] = uSrc1.au16[5] - puSrc->au16[5];
    puDst->au16[6] = uSrc1.au16[6] - puSrc->au16[6];
    puDst->au16[7] = uSrc1.au16[7] - puSrc->au16[7];
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au16[0] = puSrc1->au16[0] - puSrc2->au16[0];
    puDst->au16[1] = puSrc1->au16[1] - puSrc2->au16[1];
    puDst->au16[2] = puSrc1->au16[2] - puSrc2->au16[2];
    puDst->au16[3] = puSrc1->au16[3] - puSrc2->au16[3];
    puDst->au16[4] = puSrc1->au16[4] - puSrc2->au16[4];
    puDst->au16[5] = puSrc1->au16[5] - puSrc2->au16[5];
    puDst->au16[6] = puSrc1->au16[6] - puSrc2->au16[6];
    puDst->au16[7] = puSrc1->au16[7] - puSrc2->au16[7];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au16[0]  = puSrc1->au16[0]  - puSrc2->au16[0];
    puDst->au16[1]  = puSrc1->au16[1]  - puSrc2->au16[1];
    puDst->au16[2]  = puSrc1->au16[2]  - puSrc2->au16[2];
    puDst->au16[3]  = puSrc1->au16[3]  - puSrc2->au16[3];
    puDst->au16[4]  = puSrc1->au16[4]  - puSrc2->au16[4];
    puDst->au16[5]  = puSrc1->au16[5]  - puSrc2->au16[5];
    puDst->au16[6]  = puSrc1->au16[6]  - puSrc2->au16[6];
    puDst->au16[7]  = puSrc1->au16[7]  - puSrc2->au16[7];
    puDst->au16[8]  = puSrc1->au16[8]  - puSrc2->au16[8];
    puDst->au16[9]  = puSrc1->au16[9]  - puSrc2->au16[9];
    puDst->au16[10] = puSrc1->au16[10] - puSrc2->au16[10];
    puDst->au16[11] = puSrc1->au16[11] - puSrc2->au16[11];
    puDst->au16[12] = puSrc1->au16[12] - puSrc2->au16[12];
    puDst->au16[13] = puSrc1->au16[13] - puSrc2->au16[13];
    puDst->au16[14] = puSrc1->au16[14] - puSrc2->au16[14];
    puDst->au16[15] = puSrc1->au16[15] - puSrc2->au16[15];
}


/*
 * PSUBSW / VPSUBSW
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psubsw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[0] - uSrc2.ai16[0]);
    uDst.au16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[1] - uSrc2.ai16[1]);
    uDst.au16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[2] - uSrc2.ai16[2]);
    uDst.au16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[3] - uSrc2.ai16[3]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psubsw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[0] - puSrc->ai16[0]);
    puDst->au16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[1] - puSrc->ai16[1]);
    puDst->au16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[2] - puSrc->ai16[2]);
    puDst->au16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[3] - puSrc->ai16[3]);
    puDst->au16[4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[4] - puSrc->ai16[4]);
    puDst->au16[5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[5] - puSrc->ai16[5]);
    puDst->au16[6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[6] - puSrc->ai16[6]);
    puDst->au16[7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[7] - puSrc->ai16[7]);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubsw_u128_fallback,(PRTUINT128U puDst,
                                                        PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[0] - puSrc2->ai16[0]);
    puDst->au16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[1] - puSrc2->ai16[1]);
    puDst->au16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[2] - puSrc2->ai16[2]);
    puDst->au16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[3] - puSrc2->ai16[3]);
    puDst->au16[4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[4] - puSrc2->ai16[4]);
    puDst->au16[5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[5] - puSrc2->ai16[5]);
    puDst->au16[6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[6] - puSrc2->ai16[6]);
    puDst->au16[7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[7] - puSrc2->ai16[7]);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubsw_u256_fallback,(PRTUINT256U puDst,
                                                        PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au16[0]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[0]  - puSrc2->ai16[0]);
    puDst->au16[1]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[1]  - puSrc2->ai16[1]);
    puDst->au16[2]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[2]  - puSrc2->ai16[2]);
    puDst->au16[3]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[3]  - puSrc2->ai16[3]);
    puDst->au16[4]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[4]  - puSrc2->ai16[4]);
    puDst->au16[5]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[5]  - puSrc2->ai16[5]);
    puDst->au16[6]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[6]  - puSrc2->ai16[6]);
    puDst->au16[7]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[7]  - puSrc2->ai16[7]);
    puDst->au16[8]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[8]  - puSrc2->ai16[8]);
    puDst->au16[9]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[9]  - puSrc2->ai16[9]);
    puDst->au16[10] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[10] - puSrc2->ai16[10]);
    puDst->au16[11] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[11] - puSrc2->ai16[11]);
    puDst->au16[12] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[12] - puSrc2->ai16[12]);
    puDst->au16[13] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[13] - puSrc2->ai16[13]);
    puDst->au16[14] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[14] - puSrc2->ai16[14]);
    puDst->au16[15] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[15] - puSrc2->ai16[15]);
}


/*
 * PSUBUSW / VPSUBUSW
 */
#define SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(a_uDword) \
        ( (uint32_t)(a_uDword) <= (uint16_t)0xffff  \
          ? (uint16_t)(a_uDword) \
          : (uint16_t)0 )

#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psubusw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au16[0] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(uSrc1.au16[0] - uSrc2.au16[0]);
    uDst.au16[1] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(uSrc1.au16[1] - uSrc2.au16[1]);
    uDst.au16[2] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(uSrc1.au16[2] - uSrc2.au16[2]);
    uDst.au16[3] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(uSrc1.au16[3] - uSrc2.au16[3]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psubusw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au16[0] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(uSrc1.au16[0] - puSrc->au16[0]);
    puDst->au16[1] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(uSrc1.au16[1] - puSrc->au16[1]);
    puDst->au16[2] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(uSrc1.au16[2] - puSrc->au16[2]);
    puDst->au16[3] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(uSrc1.au16[3] - puSrc->au16[3]);
    puDst->au16[4] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(uSrc1.au16[4] - puSrc->au16[4]);
    puDst->au16[5] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(uSrc1.au16[5] - puSrc->au16[5]);
    puDst->au16[6] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(uSrc1.au16[6] - puSrc->au16[6]);
    puDst->au16[7] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(uSrc1.au16[7] - puSrc->au16[7]);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubusw_u128_fallback,(PRTUINT128U puDst,
                                                         PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au16[0] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[0] - puSrc2->au16[0]);
    puDst->au16[1] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[1] - puSrc2->au16[1]);
    puDst->au16[2] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[2] - puSrc2->au16[2]);
    puDst->au16[3] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[3] - puSrc2->au16[3]);
    puDst->au16[4] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[4] - puSrc2->au16[4]);
    puDst->au16[5] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[5] - puSrc2->au16[5]);
    puDst->au16[6] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[6] - puSrc2->au16[6]);
    puDst->au16[7] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[7] - puSrc2->au16[7]);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubusw_u256_fallback,(PRTUINT256U puDst,
                                                         PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au16[0]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[0]  - puSrc2->au16[0]);
    puDst->au16[1]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[1]  - puSrc2->au16[1]);
    puDst->au16[2]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[2]  - puSrc2->au16[2]);
    puDst->au16[3]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[3]  - puSrc2->au16[3]);
    puDst->au16[4]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[4]  - puSrc2->au16[4]);
    puDst->au16[5]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[5]  - puSrc2->au16[5]);
    puDst->au16[6]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[6]  - puSrc2->au16[6]);
    puDst->au16[7]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[7]  - puSrc2->au16[7]);
    puDst->au16[8]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[8]  - puSrc2->au16[8]);
    puDst->au16[9]  = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[9]  - puSrc2->au16[9]);
    puDst->au16[10] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[10] - puSrc2->au16[10]);
    puDst->au16[11] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[11] - puSrc2->au16[11]);
    puDst->au16[12] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[12] - puSrc2->au16[12]);
    puDst->au16[13] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[13] - puSrc2->au16[13]);
    puDst->au16[14] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[14] - puSrc2->au16[14]);
    puDst->au16[15] = SATURATED_UNSIGNED_DWORD_TO_UNSIGNED_WORD_SUB(puSrc1->au16[15] - puSrc2->au16[15]);
}



/*
 * PSUBD / VPSUBD.
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psubd_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au32[0] = uSrc1.au32[0] - uSrc2.au32[0];
    uDst.au32[1] = uSrc1.au32[1] - uSrc2.au32[1];
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psubd_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au32[0] = uSrc1.au32[0] - puSrc->au32[0];
    puDst->au32[1] = uSrc1.au32[1] - puSrc->au32[1];
    puDst->au32[2] = uSrc1.au32[2] - puSrc->au32[2];
    puDst->au32[3] = uSrc1.au32[3] - puSrc->au32[3];
}

#endif /* IEM_WITHOUT_ASSEMBLY */

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au32[0] = puSrc1->au32[0] - puSrc2->au32[0];
    puDst->au32[1] = puSrc1->au32[1] - puSrc2->au32[1];
    puDst->au32[2] = puSrc1->au32[2] - puSrc2->au32[2];
    puDst->au32[3] = puSrc1->au32[3] - puSrc2->au32[3];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au32[0] = puSrc1->au32[0] - puSrc2->au32[0];
    puDst->au32[1] = puSrc1->au32[1] - puSrc2->au32[1];
    puDst->au32[2] = puSrc1->au32[2] - puSrc2->au32[2];
    puDst->au32[3] = puSrc1->au32[3] - puSrc2->au32[3];
    puDst->au32[4] = puSrc1->au32[4] - puSrc2->au32[4];
    puDst->au32[5] = puSrc1->au32[5] - puSrc2->au32[5];
    puDst->au32[6] = puSrc1->au32[6] - puSrc2->au32[6];
    puDst->au32[7] = puSrc1->au32[7] - puSrc2->au32[7];
}


/*
 * PSUBQ / VPSUBQ.
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psubq_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    *puDst = *puDst - *puSrc;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_psubq_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au64[0] = uSrc1.au64[0] - puSrc->au64[0];
    puDst->au64[1] = uSrc1.au64[1] - puSrc->au64[1];
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au64[0] = puSrc1->au64[0] - puSrc2->au64[0];
    puDst->au64[1] = puSrc1->au64[1] - puSrc2->au64[1];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsubq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au64[0] = puSrc1->au64[0] - puSrc2->au64[0];
    puDst->au64[1] = puSrc1->au64[1] - puSrc2->au64[1];
    puDst->au64[2] = puSrc1->au64[2] - puSrc2->au64[2];
    puDst->au64[3] = puSrc1->au64[3] - puSrc2->au64[3];
}



/*
 * PMULLW / VPMULLW / PMULLD / VPMULLD
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pmullw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.ai16[0] = uSrc1.ai16[0] * uSrc2.ai16[0];
    uDst.ai16[1] = uSrc1.ai16[1] * uSrc2.ai16[1];
    uDst.ai16[2] = uSrc1.ai16[2] * uSrc2.ai16[2];
    uDst.ai16[3] = uSrc1.ai16[3] * uSrc2.ai16[3];
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pmullw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->ai16[0] = uSrc1.ai16[0] * puSrc->ai16[0];
    puDst->ai16[1] = uSrc1.ai16[1] * puSrc->ai16[1];
    puDst->ai16[2] = uSrc1.ai16[2] * puSrc->ai16[2];
    puDst->ai16[3] = uSrc1.ai16[3] * puSrc->ai16[3];
    puDst->ai16[4] = uSrc1.ai16[4] * puSrc->ai16[4];
    puDst->ai16[5] = uSrc1.ai16[5] * puSrc->ai16[5];
    puDst->ai16[6] = uSrc1.ai16[6] * puSrc->ai16[6];
    puDst->ai16[7] = uSrc1.ai16[7] * puSrc->ai16[7];
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_pmulld_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai32[0] = uSrc1.ai32[0] * puSrc->ai32[0];
    puDst->ai32[1] = uSrc1.ai32[1] * puSrc->ai32[1];
    puDst->ai32[2] = uSrc1.ai32[2] * puSrc->ai32[2];
    puDst->ai32[3] = uSrc1.ai32[3] * puSrc->ai32[3];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmullw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->ai16[0] = puSrc1->ai16[0] * puSrc2->ai16[0];
    puDst->ai16[1] = puSrc1->ai16[1] * puSrc2->ai16[1];
    puDst->ai16[2] = puSrc1->ai16[2] * puSrc2->ai16[2];
    puDst->ai16[3] = puSrc1->ai16[3] * puSrc2->ai16[3];
    puDst->ai16[4] = puSrc1->ai16[4] * puSrc2->ai16[4];
    puDst->ai16[5] = puSrc1->ai16[5] * puSrc2->ai16[5];
    puDst->ai16[6] = puSrc1->ai16[6] * puSrc2->ai16[6];
    puDst->ai16[7] = puSrc1->ai16[7] * puSrc2->ai16[7];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmullw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->ai16[ 0] = puSrc1->ai16[ 0] * puSrc2->ai16[ 0];
    puDst->ai16[ 1] = puSrc1->ai16[ 1] * puSrc2->ai16[ 1];
    puDst->ai16[ 2] = puSrc1->ai16[ 2] * puSrc2->ai16[ 2];
    puDst->ai16[ 3] = puSrc1->ai16[ 3] * puSrc2->ai16[ 3];
    puDst->ai16[ 4] = puSrc1->ai16[ 4] * puSrc2->ai16[ 4];
    puDst->ai16[ 5] = puSrc1->ai16[ 5] * puSrc2->ai16[ 5];
    puDst->ai16[ 6] = puSrc1->ai16[ 6] * puSrc2->ai16[ 6];
    puDst->ai16[ 7] = puSrc1->ai16[ 7] * puSrc2->ai16[ 7];
    puDst->ai16[ 8] = puSrc1->ai16[ 8] * puSrc2->ai16[ 8];
    puDst->ai16[ 9] = puSrc1->ai16[ 9] * puSrc2->ai16[ 9];
    puDst->ai16[10] = puSrc1->ai16[10] * puSrc2->ai16[10];
    puDst->ai16[11] = puSrc1->ai16[11] * puSrc2->ai16[11];
    puDst->ai16[12] = puSrc1->ai16[12] * puSrc2->ai16[12];
    puDst->ai16[13] = puSrc1->ai16[13] * puSrc2->ai16[13];
    puDst->ai16[14] = puSrc1->ai16[14] * puSrc2->ai16[14];
    puDst->ai16[15] = puSrc1->ai16[15] * puSrc2->ai16[15];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmulld_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->ai32[0] = puSrc1->ai32[0] * puSrc2->ai32[0];
    puDst->ai32[1] = puSrc1->ai32[1] * puSrc2->ai32[1];
    puDst->ai32[2] = puSrc1->ai32[2] * puSrc2->ai32[2];
    puDst->ai32[3] = puSrc1->ai32[3] * puSrc2->ai32[3];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmulld_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->ai32[0] = puSrc1->ai32[0] * puSrc2->ai32[0];
    puDst->ai32[1] = puSrc1->ai32[1] * puSrc2->ai32[1];
    puDst->ai32[2] = puSrc1->ai32[2] * puSrc2->ai32[2];
    puDst->ai32[3] = puSrc1->ai32[3] * puSrc2->ai32[3];
    puDst->ai32[4] = puSrc1->ai32[4] * puSrc2->ai32[4];
    puDst->ai32[5] = puSrc1->ai32[5] * puSrc2->ai32[5];
    puDst->ai32[6] = puSrc1->ai32[6] * puSrc2->ai32[6];
    puDst->ai32[7] = puSrc1->ai32[7] * puSrc2->ai32[7];
}


/*
 * PMULHW / VPMULHW
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pmulhw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.ai16[0] = RT_HIWORD(uSrc1.ai16[0] * uSrc2.ai16[0]);
    uDst.ai16[1] = RT_HIWORD(uSrc1.ai16[1] * uSrc2.ai16[1]);
    uDst.ai16[2] = RT_HIWORD(uSrc1.ai16[2] * uSrc2.ai16[2]);
    uDst.ai16[3] = RT_HIWORD(uSrc1.ai16[3] * uSrc2.ai16[3]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pmulhw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->ai16[0] = RT_HIWORD(uSrc1.ai16[0] * puSrc->ai16[0]);
    puDst->ai16[1] = RT_HIWORD(uSrc1.ai16[1] * puSrc->ai16[1]);
    puDst->ai16[2] = RT_HIWORD(uSrc1.ai16[2] * puSrc->ai16[2]);
    puDst->ai16[3] = RT_HIWORD(uSrc1.ai16[3] * puSrc->ai16[3]);
    puDst->ai16[4] = RT_HIWORD(uSrc1.ai16[4] * puSrc->ai16[4]);
    puDst->ai16[5] = RT_HIWORD(uSrc1.ai16[5] * puSrc->ai16[5]);
    puDst->ai16[6] = RT_HIWORD(uSrc1.ai16[6] * puSrc->ai16[6]);
    puDst->ai16[7] = RT_HIWORD(uSrc1.ai16[7] * puSrc->ai16[7]);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpmulhw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->ai16[0] = RT_HIWORD(puSrc1->ai16[0] * puSrc2->ai16[0]);
    puDst->ai16[1] = RT_HIWORD(puSrc1->ai16[1] * puSrc2->ai16[1]);
    puDst->ai16[2] = RT_HIWORD(puSrc1->ai16[2] * puSrc2->ai16[2]);
    puDst->ai16[3] = RT_HIWORD(puSrc1->ai16[3] * puSrc2->ai16[3]);
    puDst->ai16[4] = RT_HIWORD(puSrc1->ai16[4] * puSrc2->ai16[4]);
    puDst->ai16[5] = RT_HIWORD(puSrc1->ai16[5] * puSrc2->ai16[5]);
    puDst->ai16[6] = RT_HIWORD(puSrc1->ai16[6] * puSrc2->ai16[6]);
    puDst->ai16[7] = RT_HIWORD(puSrc1->ai16[7] * puSrc2->ai16[7]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmulhw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->ai16[ 0] = RT_HIWORD(puSrc1->ai16[ 0] * puSrc2->ai16[ 0]);
    puDst->ai16[ 1] = RT_HIWORD(puSrc1->ai16[ 1] * puSrc2->ai16[ 1]);
    puDst->ai16[ 2] = RT_HIWORD(puSrc1->ai16[ 2] * puSrc2->ai16[ 2]);
    puDst->ai16[ 3] = RT_HIWORD(puSrc1->ai16[ 3] * puSrc2->ai16[ 3]);
    puDst->ai16[ 4] = RT_HIWORD(puSrc1->ai16[ 4] * puSrc2->ai16[ 4]);
    puDst->ai16[ 5] = RT_HIWORD(puSrc1->ai16[ 5] * puSrc2->ai16[ 5]);
    puDst->ai16[ 6] = RT_HIWORD(puSrc1->ai16[ 6] * puSrc2->ai16[ 6]);
    puDst->ai16[ 7] = RT_HIWORD(puSrc1->ai16[ 7] * puSrc2->ai16[ 7]);
    puDst->ai16[ 8] = RT_HIWORD(puSrc1->ai16[ 8] * puSrc2->ai16[ 8]);
    puDst->ai16[ 9] = RT_HIWORD(puSrc1->ai16[ 9] * puSrc2->ai16[ 9]);
    puDst->ai16[10] = RT_HIWORD(puSrc1->ai16[10] * puSrc2->ai16[10]);
    puDst->ai16[11] = RT_HIWORD(puSrc1->ai16[11] * puSrc2->ai16[11]);
    puDst->ai16[12] = RT_HIWORD(puSrc1->ai16[12] * puSrc2->ai16[12]);
    puDst->ai16[13] = RT_HIWORD(puSrc1->ai16[13] * puSrc2->ai16[13]);
    puDst->ai16[14] = RT_HIWORD(puSrc1->ai16[14] * puSrc2->ai16[14]);
    puDst->ai16[15] = RT_HIWORD(puSrc1->ai16[15] * puSrc2->ai16[15]);
}


/*
 * PMULHUW / VPMULHUW
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pmulhuw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uDst.au16[0] = RT_HIWORD(uSrc1.au16[0] * uSrc2.au16[0]);
    uDst.au16[1] = RT_HIWORD(uSrc1.au16[1] * uSrc2.au16[1]);
    uDst.au16[2] = RT_HIWORD(uSrc1.au16[2] * uSrc2.au16[2]);
    uDst.au16[3] = RT_HIWORD(uSrc1.au16[3] * uSrc2.au16[3]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pmulhuw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    puDst->au16[0] = RT_HIWORD(uSrc1.au16[0] * puSrc->au16[0]);
    puDst->au16[1] = RT_HIWORD(uSrc1.au16[1] * puSrc->au16[1]);
    puDst->au16[2] = RT_HIWORD(uSrc1.au16[2] * puSrc->au16[2]);
    puDst->au16[3] = RT_HIWORD(uSrc1.au16[3] * puSrc->au16[3]);
    puDst->au16[4] = RT_HIWORD(uSrc1.au16[4] * puSrc->au16[4]);
    puDst->au16[5] = RT_HIWORD(uSrc1.au16[5] * puSrc->au16[5]);
    puDst->au16[6] = RT_HIWORD(uSrc1.au16[6] * puSrc->au16[6]);
    puDst->au16[7] = RT_HIWORD(uSrc1.au16[7] * puSrc->au16[7]);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpmulhuw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au16[0] = RT_HIWORD(puSrc1->au16[0] * puSrc2->au16[0]);
    puDst->au16[1] = RT_HIWORD(puSrc1->au16[1] * puSrc2->au16[1]);
    puDst->au16[2] = RT_HIWORD(puSrc1->au16[2] * puSrc2->au16[2]);
    puDst->au16[3] = RT_HIWORD(puSrc1->au16[3] * puSrc2->au16[3]);
    puDst->au16[4] = RT_HIWORD(puSrc1->au16[4] * puSrc2->au16[4]);
    puDst->au16[5] = RT_HIWORD(puSrc1->au16[5] * puSrc2->au16[5]);
    puDst->au16[6] = RT_HIWORD(puSrc1->au16[6] * puSrc2->au16[6]);
    puDst->au16[7] = RT_HIWORD(puSrc1->au16[7] * puSrc2->au16[7]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmulhuw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au16[ 0] = RT_HIWORD(puSrc1->au16[ 0] * puSrc2->au16[ 0]);
    puDst->au16[ 1] = RT_HIWORD(puSrc1->au16[ 1] * puSrc2->au16[ 1]);
    puDst->au16[ 2] = RT_HIWORD(puSrc1->au16[ 2] * puSrc2->au16[ 2]);
    puDst->au16[ 3] = RT_HIWORD(puSrc1->au16[ 3] * puSrc2->au16[ 3]);
    puDst->au16[ 4] = RT_HIWORD(puSrc1->au16[ 4] * puSrc2->au16[ 4]);
    puDst->au16[ 5] = RT_HIWORD(puSrc1->au16[ 5] * puSrc2->au16[ 5]);
    puDst->au16[ 6] = RT_HIWORD(puSrc1->au16[ 6] * puSrc2->au16[ 6]);
    puDst->au16[ 7] = RT_HIWORD(puSrc1->au16[ 7] * puSrc2->au16[ 7]);
    puDst->au16[ 8] = RT_HIWORD(puSrc1->au16[ 8] * puSrc2->au16[ 8]);
    puDst->au16[ 9] = RT_HIWORD(puSrc1->au16[ 9] * puSrc2->au16[ 9]);
    puDst->au16[10] = RT_HIWORD(puSrc1->au16[10] * puSrc2->au16[10]);
    puDst->au16[11] = RT_HIWORD(puSrc1->au16[11] * puSrc2->au16[11]);
    puDst->au16[12] = RT_HIWORD(puSrc1->au16[12] * puSrc2->au16[12]);
    puDst->au16[13] = RT_HIWORD(puSrc1->au16[13] * puSrc2->au16[13]);
    puDst->au16[14] = RT_HIWORD(puSrc1->au16[14] * puSrc2->au16[14]);
    puDst->au16[15] = RT_HIWORD(puSrc1->au16[15] * puSrc2->au16[15]);
}


/*
 * PSRLW / VPSRLW
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psrlw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    if (uSrc2.au64[0] <= 15)
    {
        uDst.au16[0] = uSrc1.au16[0] >> uSrc2.au8[0];
        uDst.au16[1] = uSrc1.au16[1] >> uSrc2.au8[0];
        uDst.au16[2] = uSrc1.au16[2] >> uSrc2.au8[0];
        uDst.au16[3] = uSrc1.au16[3] >> uSrc2.au8[0];
    }
    else
    {
        uDst.au64[0] = 0;
    }
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psrlw_imm_u64,(uint64_t *puDst, uint8_t uShift))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uDst;

    if (uShift <= 15)
    {
        uDst.au16[0] = uSrc1.au16[0] >> uShift;
        uDst.au16[1] = uSrc1.au16[1] >> uShift;
        uDst.au16[2] = uSrc1.au16[2] >> uShift;
        uDst.au16[3] = uSrc1.au16[3] >> uShift;
    }
    else
    {
        uDst.au64[0] = 0;
    }
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psrlw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    if (puSrc->au64[0] <= 15)
    {
        puDst->au16[0] = uSrc1.au16[0] >> puSrc->au8[0];
        puDst->au16[1] = uSrc1.au16[1] >> puSrc->au8[0];
        puDst->au16[2] = uSrc1.au16[2] >> puSrc->au8[0];
        puDst->au16[3] = uSrc1.au16[3] >> puSrc->au8[0];
        puDst->au16[4] = uSrc1.au16[4] >> puSrc->au8[0];
        puDst->au16[5] = uSrc1.au16[5] >> puSrc->au8[0];
        puDst->au16[6] = uSrc1.au16[6] >> puSrc->au8[0];
        puDst->au16[7] = uSrc1.au16[7] >> puSrc->au8[0];
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_psrlw_imm_u128,(PRTUINT128U puDst, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puDst;

    if (uShift <= 15)
    {
        puDst->au16[0] = uSrc1.au16[0] >> uShift;
        puDst->au16[1] = uSrc1.au16[1] >> uShift;
        puDst->au16[2] = uSrc1.au16[2] >> uShift;
        puDst->au16[3] = uSrc1.au16[3] >> uShift;
        puDst->au16[4] = uSrc1.au16[4] >> uShift;
        puDst->au16[5] = uSrc1.au16[5] >> uShift;
        puDst->au16[6] = uSrc1.au16[6] >> uShift;
        puDst->au16[7] = uSrc1.au16[7] >> uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlw_imm_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puSrc1;

    if (uShift <= 15)
    {
        puDst->au16[0] = uSrc1.au16[0] >> uShift;
        puDst->au16[1] = uSrc1.au16[1] >> uShift;
        puDst->au16[2] = uSrc1.au16[2] >> uShift;
        puDst->au16[3] = uSrc1.au16[3] >> uShift;
        puDst->au16[4] = uSrc1.au16[4] >> uShift;
        puDst->au16[5] = uSrc1.au16[5] >> uShift;
        puDst->au16[6] = uSrc1.au16[6] >> uShift;
        puDst->au16[7] = uSrc1.au16[7] >> uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    iemAImpl_vpsrlw_imm_u128_fallback(puDst, puSrc1, RT_MIN(16, puSrc2->au64[0]));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlw_imm_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
     iemAImpl_vpsrlw_imm_u128_fallback(puDst, puSrc1, uShift);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlw_imm_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    RTUINT256U uSrc1 = *puSrc1;

    if (uShift <= 15)
    {
        puDst->au16[0] = uSrc1.au16[0] >> uShift;
        puDst->au16[1] = uSrc1.au16[1] >> uShift;
        puDst->au16[2] = uSrc1.au16[2] >> uShift;
        puDst->au16[3] = uSrc1.au16[3] >> uShift;
        puDst->au16[4] = uSrc1.au16[4] >> uShift;
        puDst->au16[5] = uSrc1.au16[5] >> uShift;
        puDst->au16[6] = uSrc1.au16[6] >> uShift;
        puDst->au16[7] = uSrc1.au16[7] >> uShift;
        puDst->au16[8] = uSrc1.au16[8] >> uShift;
        puDst->au16[9] = uSrc1.au16[9] >> uShift;
        puDst->au16[10] = uSrc1.au16[10] >> uShift;
        puDst->au16[11] = uSrc1.au16[11] >> uShift;
        puDst->au16[12] = uSrc1.au16[12] >> uShift;
        puDst->au16[13] = uSrc1.au16[13] >> uShift;
        puDst->au16[14] = uSrc1.au16[14] >> uShift;
        puDst->au16[15] = uSrc1.au16[15] >> uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
        puDst->au64[2] = 0;
        puDst->au64[3] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlw_imm_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    iemAImpl_vpsrlw_imm_u256_fallback(puDst, puSrc1, uShift);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    iemAImpl_vpsrlw_imm_u256_fallback(puDst, puSrc1, RT_MIN(16, puSrc2->au64[0]));
}


/*
 * PSRAW / VPSRAW
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psraw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uint8_t uShift;

    uShift = RT_MIN(15, uSrc2.au64[0]);

    uDst.ai16[0] = uSrc1.ai16[0] >> uShift;
    uDst.ai16[1] = uSrc1.ai16[1] >> uShift;
    uDst.ai16[2] = uSrc1.ai16[2] >> uShift;
    uDst.ai16[3] = uSrc1.ai16[3] >> uShift;

    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psraw_imm_u64,(uint64_t *puDst, uint8_t uShift))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uDst;

    uShift = RT_MIN(15, uShift);

    uDst.ai16[0] = uSrc1.ai16[0] >> uShift;
    uDst.ai16[1] = uSrc1.ai16[1] >> uShift;
    uDst.ai16[2] = uSrc1.ai16[2] >> uShift;
    uDst.ai16[3] = uSrc1.ai16[3] >> uShift;

    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psraw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    uint8_t uShift;

    uShift = RT_MIN(15, puSrc->au64[0]);

    puDst->ai16[0] = uSrc1.ai16[0] >> uShift;
    puDst->ai16[1] = uSrc1.ai16[1] >> uShift;
    puDst->ai16[2] = uSrc1.ai16[2] >> uShift;
    puDst->ai16[3] = uSrc1.ai16[3] >> uShift;
    puDst->ai16[4] = uSrc1.ai16[4] >> uShift;
    puDst->ai16[5] = uSrc1.ai16[5] >> uShift;
    puDst->ai16[6] = uSrc1.ai16[6] >> uShift;
    puDst->ai16[7] = uSrc1.ai16[7] >> uShift;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_psraw_imm_u128,(PRTUINT128U puDst, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puDst;

    uShift = RT_MIN(15, uShift);

    puDst->ai16[0] = uSrc1.ai16[0] >> uShift;
    puDst->ai16[1] = uSrc1.ai16[1] >> uShift;
    puDst->ai16[2] = uSrc1.ai16[2] >> uShift;
    puDst->ai16[3] = uSrc1.ai16[3] >> uShift;
    puDst->ai16[4] = uSrc1.ai16[4] >> uShift;
    puDst->ai16[5] = uSrc1.ai16[5] >> uShift;
    puDst->ai16[6] = uSrc1.ai16[6] >> uShift;
    puDst->ai16[7] = uSrc1.ai16[7] >> uShift;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsraw_imm_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puSrc1;

    uShift = RT_MIN(15, uShift);

    puDst->ai16[0] = uSrc1.ai16[0] >> uShift;
    puDst->ai16[1] = uSrc1.ai16[1] >> uShift;
    puDst->ai16[2] = uSrc1.ai16[2] >> uShift;
    puDst->ai16[3] = uSrc1.ai16[3] >> uShift;
    puDst->ai16[4] = uSrc1.ai16[4] >> uShift;
    puDst->ai16[5] = uSrc1.ai16[5] >> uShift;
    puDst->ai16[6] = uSrc1.ai16[6] >> uShift;
    puDst->ai16[7] = uSrc1.ai16[7] >> uShift;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsraw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    iemAImpl_vpsraw_imm_u128_fallback(puDst, puSrc1, RT_MIN(15, puSrc2->au64[0]));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsraw_imm_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
     iemAImpl_vpsraw_imm_u128_fallback(puDst, puSrc1, uShift);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsraw_imm_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    RTUINT256U uSrc1 = *puSrc1;

    uShift = RT_MIN(15, uShift);

    puDst->ai16[0] = uSrc1.ai16[0] >> uShift;
    puDst->ai16[1] = uSrc1.ai16[1] >> uShift;
    puDst->ai16[2] = uSrc1.ai16[2] >> uShift;
    puDst->ai16[3] = uSrc1.ai16[3] >> uShift;
    puDst->ai16[4] = uSrc1.ai16[4] >> uShift;
    puDst->ai16[5] = uSrc1.ai16[5] >> uShift;
    puDst->ai16[6] = uSrc1.ai16[6] >> uShift;
    puDst->ai16[7] = uSrc1.ai16[7] >> uShift;
    puDst->ai16[8] = uSrc1.ai16[8] >> uShift;
    puDst->ai16[9] = uSrc1.ai16[9] >> uShift;
    puDst->ai16[10] = uSrc1.ai16[10] >> uShift;
    puDst->ai16[11] = uSrc1.ai16[11] >> uShift;
    puDst->ai16[12] = uSrc1.ai16[12] >> uShift;
    puDst->ai16[13] = uSrc1.ai16[13] >> uShift;
    puDst->ai16[14] = uSrc1.ai16[14] >> uShift;
    puDst->ai16[15] = uSrc1.ai16[15] >> uShift;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsraw_imm_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    iemAImpl_vpsraw_imm_u256_fallback(puDst, puSrc1, uShift);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsraw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    iemAImpl_vpsraw_imm_u256_fallback(puDst, puSrc1, RT_MIN(15, puSrc2->au64[0]));
}


/*
 * PSLLW / VPSLLW
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psllw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    if (uSrc2.au64[0] <= 15)
    {
        uDst.au16[0] = uSrc1.au16[0] << uSrc2.au8[0];
        uDst.au16[1] = uSrc1.au16[1] << uSrc2.au8[0];
        uDst.au16[2] = uSrc1.au16[2] << uSrc2.au8[0];
        uDst.au16[3] = uSrc1.au16[3] << uSrc2.au8[0];
    }
    else
    {
        uDst.au64[0] = 0;
    }
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psllw_imm_u64,(uint64_t *puDst, uint8_t uShift))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uDst;

    if (uShift <= 15)
    {
        uDst.au16[0] = uSrc1.au16[0] << uShift;
        uDst.au16[1] = uSrc1.au16[1] << uShift;
        uDst.au16[2] = uSrc1.au16[2] << uShift;
        uDst.au16[3] = uSrc1.au16[3] << uShift;
    }
    else
    {
        uDst.au64[0] = 0;
    }
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psllw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    if (puSrc->au64[0] <= 15)
    {
        puDst->au16[0] = uSrc1.au16[0] << puSrc->au8[0];
        puDst->au16[1] = uSrc1.au16[1] << puSrc->au8[0];
        puDst->au16[2] = uSrc1.au16[2] << puSrc->au8[0];
        puDst->au16[3] = uSrc1.au16[3] << puSrc->au8[0];
        puDst->au16[4] = uSrc1.au16[4] << puSrc->au8[0];
        puDst->au16[5] = uSrc1.au16[5] << puSrc->au8[0];
        puDst->au16[6] = uSrc1.au16[6] << puSrc->au8[0];
        puDst->au16[7] = uSrc1.au16[7] << puSrc->au8[0];
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_psllw_imm_u128,(PRTUINT128U puDst, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puDst;

    if (uShift <= 15)
    {
        puDst->au16[0] = uSrc1.au16[0] << uShift;
        puDst->au16[1] = uSrc1.au16[1] << uShift;
        puDst->au16[2] = uSrc1.au16[2] << uShift;
        puDst->au16[3] = uSrc1.au16[3] << uShift;
        puDst->au16[4] = uSrc1.au16[4] << uShift;
        puDst->au16[5] = uSrc1.au16[5] << uShift;
        puDst->au16[6] = uSrc1.au16[6] << uShift;
        puDst->au16[7] = uSrc1.au16[7] << uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllw_imm_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puSrc1;

    if (uShift <= 15)
    {
        puDst->au16[0] = uSrc1.au16[0] << uShift;
        puDst->au16[1] = uSrc1.au16[1] << uShift;
        puDst->au16[2] = uSrc1.au16[2] << uShift;
        puDst->au16[3] = uSrc1.au16[3] << uShift;
        puDst->au16[4] = uSrc1.au16[4] << uShift;
        puDst->au16[5] = uSrc1.au16[5] << uShift;
        puDst->au16[6] = uSrc1.au16[6] << uShift;
        puDst->au16[7] = uSrc1.au16[7] << uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    iemAImpl_vpsllw_imm_u128_fallback(puDst, puSrc1, RT_MIN(16, puSrc2->au64[0]));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllw_imm_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
    iemAImpl_vpsllw_imm_u128_fallback(puDst, puSrc1, uShift);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllw_imm_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    RTUINT256U uSrc1 = *puSrc1;

    if (uShift <= 15)
    {
        puDst->au16[0] = uSrc1.au16[0] << uShift;
        puDst->au16[1] = uSrc1.au16[1] << uShift;
        puDst->au16[2] = uSrc1.au16[2] << uShift;
        puDst->au16[3] = uSrc1.au16[3] << uShift;
        puDst->au16[4] = uSrc1.au16[4] << uShift;
        puDst->au16[5] = uSrc1.au16[5] << uShift;
        puDst->au16[6] = uSrc1.au16[6] << uShift;
        puDst->au16[7] = uSrc1.au16[7] << uShift;
        puDst->au16[8] = uSrc1.au16[8] << uShift;
        puDst->au16[9] = uSrc1.au16[9] << uShift;
        puDst->au16[10] = uSrc1.au16[10] << uShift;
        puDst->au16[11] = uSrc1.au16[11] << uShift;
        puDst->au16[12] = uSrc1.au16[12] << uShift;
        puDst->au16[13] = uSrc1.au16[13] << uShift;
        puDst->au16[14] = uSrc1.au16[14] << uShift;
        puDst->au16[15] = uSrc1.au16[15] << uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
        puDst->au64[2] = 0;
        puDst->au64[3] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    iemAImpl_vpsllw_imm_u256_fallback(puDst, puSrc1, RT_MIN(16, puSrc2->au64[0]));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllw_imm_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    iemAImpl_vpsllw_imm_u256_fallback(puDst, puSrc1, uShift);
}

/*
 * PSRLD / VPSRLD
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psrld_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    if (uSrc2.au64[0] <= 31)
    {
        uDst.au32[0] = uSrc1.au32[0] >> uSrc2.au8[0];
        uDst.au32[1] = uSrc1.au32[1] >> uSrc2.au8[0];
    }
    else
    {
        uDst.au64[0] = 0;
    }
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psrld_imm_u64,(uint64_t *puDst, uint8_t uShift))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uDst;

    if (uShift <= 31)
    {
        uDst.au32[0] = uSrc1.au32[0] >> uShift;
        uDst.au32[1] = uSrc1.au32[1] >> uShift;
    }
    else
    {
        uDst.au64[0] = 0;
    }
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psrld_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    if (puSrc->au64[0] <= 31)
    {
        puDst->au32[0] = uSrc1.au32[0] >> puSrc->au8[0];
        puDst->au32[1] = uSrc1.au32[1] >> puSrc->au8[0];
        puDst->au32[2] = uSrc1.au32[2] >> puSrc->au8[0];
        puDst->au32[3] = uSrc1.au32[3] >> puSrc->au8[0];
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_psrld_imm_u128,(PRTUINT128U puDst, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puDst;

    if (uShift <= 31)
    {
        puDst->au32[0] = uSrc1.au32[0] >> uShift;
        puDst->au32[1] = uSrc1.au32[1] >> uShift;
        puDst->au32[2] = uSrc1.au32[2] >> uShift;
        puDst->au32[3] = uSrc1.au32[3] >> uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrld_imm_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puSrc1;

    if (uShift <= 31)
    {
        puDst->au32[0] = uSrc1.au32[0] >> uShift;
        puDst->au32[1] = uSrc1.au32[1] >> uShift;
        puDst->au32[2] = uSrc1.au32[2] >> uShift;
        puDst->au32[3] = uSrc1.au32[3] >> uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrld_imm_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
    iemAImpl_vpsrld_imm_u128_fallback(puDst, puSrc1, uShift);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrld_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    iemAImpl_vpsrld_imm_u128_fallback(puDst, puSrc1, RT_MIN(32, puSrc2->au64[0]));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrld_imm_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    RTUINT256U uSrc1 = *puSrc1;

    if (uShift <= 31)
    {
       puDst->au32[0] = uSrc1.au32[0] >> uShift;
       puDst->au32[1] = uSrc1.au32[1] >> uShift;
       puDst->au32[2] = uSrc1.au32[2] >> uShift;
       puDst->au32[3] = uSrc1.au32[3] >> uShift;
       puDst->au32[4] = uSrc1.au32[4] >> uShift;
       puDst->au32[5] = uSrc1.au32[5] >> uShift;
       puDst->au32[6] = uSrc1.au32[6] >> uShift;
       puDst->au32[7] = uSrc1.au32[7] >> uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
        puDst->au64[2] = 0;
        puDst->au64[3] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrld_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    iemAImpl_vpsrld_imm_u256_fallback(puDst, puSrc1, RT_MIN(32, puSrc2->au64[0]));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrld_imm_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    iemAImpl_vpsrld_imm_u256_fallback(puDst, puSrc1, uShift);
}


/*
 * PSRAD / VPSRAD
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psrad_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uint8_t uShift;

    uShift = RT_MIN(31, uSrc2.au64[0]);

    uDst.ai32[0] = uSrc1.ai32[0] >> uShift;
    uDst.ai32[1] = uSrc1.ai32[1] >> uShift;

    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psrad_imm_u64,(uint64_t *puDst, uint8_t uShift))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uDst;

    uShift = RT_MIN(31, uShift);

    uDst.ai32[0] = uSrc1.ai32[0] >> uShift;
    uDst.ai32[1] = uSrc1.ai32[1] >> uShift;

    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psrad_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    uint8_t uShift;

    uShift = RT_MIN(31, puSrc->au64[0]);

    puDst->ai32[0] = uSrc1.ai32[0] >> uShift;
    puDst->ai32[1] = uSrc1.ai32[1] >> uShift;
    puDst->ai32[2] = uSrc1.ai32[2] >> uShift;
    puDst->ai32[3] = uSrc1.ai32[3] >> uShift;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_psrad_imm_u128,(PRTUINT128U puDst, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puDst;

    uShift = RT_MIN(31, uShift);

    puDst->ai32[0] = uSrc1.ai32[0] >> uShift;
    puDst->ai32[1] = uSrc1.ai32[1] >> uShift;
    puDst->ai32[2] = uSrc1.ai32[2] >> uShift;
    puDst->ai32[3] = uSrc1.ai32[3] >> uShift;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrad_imm_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puSrc1;

    uShift = RT_MIN(31, uShift);

    puDst->ai32[0] = uSrc1.ai32[0] >> uShift;
    puDst->ai32[1] = uSrc1.ai32[1] >> uShift;
    puDst->ai32[2] = uSrc1.ai32[2] >> uShift;
    puDst->ai32[3] = uSrc1.ai32[3] >> uShift;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrad_imm_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
    iemAImpl_vpsrad_imm_u128_fallback(puDst, puSrc1, uShift);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrad_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    iemAImpl_vpsrad_imm_u128_fallback(puDst, puSrc1, RT_MIN(31, puSrc2->au64[0]));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrad_imm_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    RTUINT256U uSrc1 = *puSrc1;

    uShift = RT_MIN(31, uShift);

    puDst->ai32[0] = uSrc1.ai32[0] >> uShift;
    puDst->ai32[1] = uSrc1.ai32[1] >> uShift;
    puDst->ai32[2] = uSrc1.ai32[2] >> uShift;
    puDst->ai32[3] = uSrc1.ai32[3] >> uShift;
    puDst->ai32[4] = uSrc1.ai32[4] >> uShift;
    puDst->ai32[5] = uSrc1.ai32[5] >> uShift;
    puDst->ai32[6] = uSrc1.ai32[6] >> uShift;
    puDst->ai32[7] = uSrc1.ai32[7] >> uShift;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrad_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    iemAImpl_vpsrad_imm_u256_fallback(puDst, puSrc1, RT_MIN(31, puSrc2->au64[0]));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrad_imm_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    iemAImpl_vpsrad_imm_u256_fallback(puDst, puSrc1, uShift);
}


/*
 * PSLLD / VPSLLD
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pslld_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    if (uSrc2.au64[0] <= 31)
    {
        uDst.au32[0] = uSrc1.au32[0] << uSrc2.au8[0];
        uDst.au32[1] = uSrc1.au32[1] << uSrc2.au8[0];
    }
    else
    {
        uDst.au64[0] = 0;
    }
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pslld_imm_u64,(uint64_t *puDst, uint8_t uShift))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uDst;

    if (uShift <= 31)
    {
        uDst.au32[0] = uSrc1.au32[0] << uShift;
        uDst.au32[1] = uSrc1.au32[1] << uShift;
    }
    else
    {
        uDst.au64[0] = 0;
    }
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pslld_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    if (puSrc->au64[0] <= 31)
    {
        puDst->au32[0] = uSrc1.au32[0] << puSrc->au8[0];
        puDst->au32[1] = uSrc1.au32[1] << puSrc->au8[0];
        puDst->au32[2] = uSrc1.au32[2] << puSrc->au8[0];
        puDst->au32[3] = uSrc1.au32[3] << puSrc->au8[0];
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_pslld_imm_u128,(PRTUINT128U puDst, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puDst;

    if (uShift <= 31)
    {
        puDst->au32[0] = uSrc1.au32[0] << uShift;
        puDst->au32[1] = uSrc1.au32[1] << uShift;
        puDst->au32[2] = uSrc1.au32[2] << uShift;
        puDst->au32[3] = uSrc1.au32[3] << uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpslld_imm_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puSrc1;

    if (uShift <= 31)
    {
        puDst->au32[0] = uSrc1.au32[0] << uShift;
        puDst->au32[1] = uSrc1.au32[1] << uShift;
        puDst->au32[2] = uSrc1.au32[2] << uShift;
        puDst->au32[3] = uSrc1.au32[3] << uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpslld_imm_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
    iemAImpl_vpslld_imm_u128_fallback(puDst, puSrc1, uShift);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpslld_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    iemAImpl_vpslld_imm_u128_fallback(puDst, puSrc1, RT_MIN(32, puSrc2->au64[0]));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpslld_imm_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    RTUINT256U uSrc1 = *puSrc1;

    if (uShift <= 31)
    {
        puDst->au32[0] = uSrc1.au32[0] << uShift;
        puDst->au32[1] = uSrc1.au32[1] << uShift;
        puDst->au32[2] = uSrc1.au32[2] << uShift;
        puDst->au32[3] = uSrc1.au32[3] << uShift;
        puDst->au32[4] = uSrc1.au32[4] << uShift;
        puDst->au32[5] = uSrc1.au32[5] << uShift;
        puDst->au32[6] = uSrc1.au32[6] << uShift;
        puDst->au32[7] = uSrc1.au32[7] << uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
        puDst->au64[2] = 0;
        puDst->au64[3] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpslld_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    iemAImpl_vpslld_imm_u256_fallback(puDst, puSrc1, RT_MIN(32, puSrc2->au64[0]));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpslld_imm_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    iemAImpl_vpslld_imm_u256_fallback(puDst, puSrc1, uShift);
}


/*
 * PSRLQ / VPSRLQ
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psrlq_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    if (uSrc2.au64[0] <= 63)
    {
        uDst.au64[0] = uSrc1.au64[0] >> uSrc2.au8[0];
    }
    else
    {
        uDst.au64[0] = 0;
    }
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psrlq_imm_u64,(uint64_t *puDst, uint8_t uShift))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uDst;

    if (uShift <= 63)
    {
        uDst.au64[0] = uSrc1.au64[0] >> uShift;
    }
    else
    {
        uDst.au64[0] = 0;
    }
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psrlq_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    if (puSrc->au64[0] <= 63)
    {
        puDst->au64[0] = uSrc1.au64[0] >> puSrc->au8[0];
        puDst->au64[1] = uSrc1.au64[1] >> puSrc->au8[0];
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_psrlq_imm_u128,(PRTUINT128U puDst, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puDst;

    if (uShift <= 63)
    {
        puDst->au64[0] = uSrc1.au64[0] >> uShift;
        puDst->au64[1] = uSrc1.au64[1] >> uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlq_imm_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puSrc1;

    if (uShift <= 63)
    {
        puDst->au64[0] = uSrc1.au64[0] >> uShift;
        puDst->au64[1] = uSrc1.au64[1] >> uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlq_imm_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
    iemAImpl_vpsrlq_imm_u128_fallback(puDst, puSrc1, uShift);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    iemAImpl_vpsrlq_imm_u128_fallback(puDst, puSrc1, RT_MIN(64, puSrc2->au64[0]));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlq_imm_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    RTUINT256U uSrc1 = *puSrc1;

    if (uShift <= 63)
    {
        puDst->au64[0] = uSrc1.au64[0] >> uShift;
        puDst->au64[1] = uSrc1.au64[1] >> uShift;
        puDst->au64[2] = uSrc1.au64[2] >> uShift;
        puDst->au64[3] = uSrc1.au64[3] >> uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
        puDst->au64[2] = 0;
        puDst->au64[3] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    iemAImpl_vpsrlq_imm_u256_fallback(puDst, puSrc1, RT_MIN(64, puSrc2->au64[0]));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlq_imm_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    iemAImpl_vpsrlq_imm_u256_fallback(puDst, puSrc1, uShift);
}


/*
 * PSLLQ / VPSLLQ
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psllq_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    if (uSrc2.au64[0] <= 63)
    {
        uDst.au64[0] = uSrc1.au64[0] << uSrc2.au8[0];
    }
    else
    {
        uDst.au64[0] = 0;
    }
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psllq_imm_u64,(uint64_t *puDst, uint8_t uShift))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uDst;

    if (uShift <= 63)
    {
        uDst.au64[0] = uSrc1.au64[0] << uShift;
    }
    else
    {
        uDst.au64[0] = 0;
    }
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psllq_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    if (puSrc->au64[0] <= 63)
    {
        puDst->au64[0] = uSrc1.au64[0] << puSrc->au8[0];
        puDst->au64[1] = uSrc1.au64[1] << puSrc->au8[0];
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_psllq_imm_u128,(PRTUINT128U puDst, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puDst;

    if (uShift <= 63)
    {
        puDst->au64[0] = uSrc1.au64[0] << uShift;
        puDst->au64[1] = uSrc1.au64[1] << uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllq_imm_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
    RTUINT128U uSrc1 = *puSrc1;

    if (uShift <= 63)
    {
        puDst->au64[0] = uSrc1.au64[0] << uShift;
        puDst->au64[1] = uSrc1.au64[1] << uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    iemAImpl_vpsllq_imm_u128_fallback(puDst, puSrc1, RT_MIN(64, puSrc2->au64[0]));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllq_imm_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc1, uint8_t uShift))
{
    iemAImpl_vpsllq_imm_u128_fallback(puDst, puSrc1, uShift);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllq_imm_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    RTUINT256U uSrc1 = *puSrc1;

    if (uShift <= 63)
    {
        puDst->au64[0] = uSrc1.au64[0] << uShift;
        puDst->au64[1] = uSrc1.au64[1] << uShift;
        puDst->au64[2] = uSrc1.au64[2] << uShift;
        puDst->au64[3] = uSrc1.au64[3] << uShift;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
        puDst->au64[2] = 0;
        puDst->au64[3] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    iemAImpl_vpsllq_imm_u256_fallback(puDst, puSrc1, RT_MIN(64, puSrc2->au64[0]));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllq_imm_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc1, uint8_t uShift))
{
    iemAImpl_vpsllq_imm_u256_fallback(puDst, puSrc1, uShift);
}


/*
 * PSRLDQ / VPSRLDQ
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psrldq_imm_u128,(PRTUINT128U puDst, uint8_t uShift))
{
    if (uShift < 16)
    {
        RTUINT128U uSrc1 = *puDst;
        int i;

        for (i = 0; i < 16 - uShift; ++i)
            puDst->au8[i] = uSrc1.au8[i + uShift];
        for (i = 16 - uShift; i < 16; ++i)
            puDst->au8[i] = 0;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrldq_imm_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t uShift))
{
    if (uShift < 16)
    {
        RTUINT128U uSrc1 = *puSrc;
        int i;

        for (i = 0; i < 16 - uShift; ++i)
            puDst->au8[i] = uSrc1.au8[i + uShift];
        for (i = 16 - uShift; i < 16; ++i)
            puDst->au8[i] = 0;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrldq_imm_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t uShift))
{
    iemAImpl_vpsrldq_imm_u128(&puDst->au128[0], &puSrc->au128[0], uShift);
    iemAImpl_vpsrldq_imm_u128(&puDst->au128[1], &puSrc->au128[1], uShift);
}
#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrldq_imm_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t uShift))
{
    if (uShift < 16)
    {
        RTUINT128U uSrc1 = *puSrc;
        int i;

        for (i = 0; i < 16 - uShift; ++i)
            puDst->au8[i] = uSrc1.au8[i + uShift];
        for (i = 16 - uShift; i < 16; ++i)
            puDst->au8[i] = 0;
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrldq_imm_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t uShift))
{
    iemAImpl_vpsrldq_imm_u128_fallback(&puDst->au128[0], &puSrc->au128[0], uShift);
    iemAImpl_vpsrldq_imm_u128_fallback(&puDst->au128[1], &puSrc->au128[1], uShift);
}


/*
 * PSLLDQ / VPSLLDQ
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pslldq_imm_u128,(PRTUINT128U puDst, uint8_t uShift))
{
    if (uShift < 16)
    {
        RTUINT128U uSrc1 = *puDst;
        int i;

        for (i = 0; i < uShift; ++i)
            puDst->au8[i] = 0;
        for (i = uShift; i < 16; ++i)
            puDst->au8[i] = uSrc1.au8[i - uShift];
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpslldq_imm_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t uShift))
{
    if (uShift < 16)
    {
        RTUINT128U uSrc1 = *puSrc;
        int i;

        for (i = 0; i < uShift; ++i)
            puDst->au8[i] = 0;
        for (i = uShift; i < 16; ++i)
            puDst->au8[i] = uSrc1.au8[i - uShift];
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpslldq_imm_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t uShift))
{
    iemAImpl_vpslldq_imm_u128(&puDst->au128[0], &puSrc->au128[0], uShift);
    iemAImpl_vpslldq_imm_u128(&puDst->au128[1], &puSrc->au128[1], uShift);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpslldq_imm_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t uShift))
{
    if (uShift < 16)
    {
        RTUINT128U uSrc1 = *puSrc;
        int i;

        for (i = 0; i < uShift; ++i)
            puDst->au8[i] = 0;
        for (i = uShift; i < 16; ++i)
            puDst->au8[i] = uSrc1.au8[i - uShift];
    }
    else
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpslldq_imm_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t uShift))
{
    iemAImpl_vpslldq_imm_u128_fallback(&puDst->au128[0], &puSrc->au128[0], uShift);
    iemAImpl_vpslldq_imm_u128_fallback(&puDst->au128[1], &puSrc->au128[1], uShift);
}


/*
 * VPSRLVD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlvd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    for (uint8_t uU32 = 0; uU32 < RT_ELEMENTS(puDst->au32); ++uU32)
    {
        puDst->au32[uU32] = (puSrc2->au32[uU32] > 31) ? 0 : puSrc1->au32[uU32] >> puSrc2->au8[uU32 << 2];
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlvd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    for (uint8_t uU32 = 0; uU32 < RT_ELEMENTS(puDst->au32); ++uU32)
    {
        puDst->au32[uU32] = (puSrc2->au32[uU32] > 31) ? 0 : puSrc1->au32[uU32] >> puSrc2->au8[uU32 << 2];
    }
}


/*
 * VPSRAVD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpsravd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    for (uint8_t uI32 = 0; uI32 < RT_ELEMENTS(puDst->ai32); ++uI32)
    {
        puDst->ai32[uI32] = (puSrc2->au32[uI32] > 31) ? 0 : puSrc1->ai32[uI32] >> puSrc2->au8[uI32 << 2];
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsravd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    for (uint8_t uI32 = 0; uI32 < RT_ELEMENTS(puDst->ai32); ++uI32)
    {
        puDst->ai32[uI32] = (puSrc2->au32[uI32] > 31) ? 0 : puSrc1->ai32[uI32] >> puSrc2->au8[uI32 << 2];
    }
}


/*
 * VPSLLVD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllvd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    for (uint8_t uU32 = 0; uU32 < RT_ELEMENTS(puDst->au32); ++uU32)
    {
        puDst->au32[uU32] = (puSrc2->au32[uU32] > 31) ? 0 : puSrc1->au32[uU32] << puSrc2->au8[uU32 << 2];
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllvd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    for (uint8_t uU32 = 0; uU32 < RT_ELEMENTS(puDst->au32); ++uU32)
    {
        puDst->au32[uU32] = (puSrc2->au32[uU32] > 31) ? 0 : puSrc1->au32[uU32] << puSrc2->au8[uU32 << 2];
    }
}


/*
 * VPSRLVQ
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlvq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    for (uint8_t uU64 = 0; uU64 < RT_ELEMENTS(puDst->au64); ++uU64)
    {
        puDst->au64[uU64] = (puSrc2->au64[uU64] > 63) ? 0 : puSrc1->au64[uU64] >> puSrc2->au8[uU64 << 3];
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsrlvq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    for (uint8_t uU64 = 0; uU64 < RT_ELEMENTS(puDst->au64); ++uU64)
    {
        puDst->au64[uU64] = (puSrc2->au64[uU64] > 63) ? 0 : puSrc1->au64[uU64] >> puSrc2->au8[uU64 << 3];
    }
}


/*
 * VPSLLVQ
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllvq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    for (uint8_t uU64 = 0; uU64 < RT_ELEMENTS(puDst->au64); ++uU64)
    {
        puDst->au64[uU64] = (puSrc2->au64[uU64] > 63) ? 0 : puSrc1->au64[uU64] << puSrc2->au8[uU64 << 3];
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsllvq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    for (uint8_t uU64 = 0; uU64 < RT_ELEMENTS(puDst->au64); ++uU64)
    {
        puDst->au64[uU64] = (puSrc2->au64[uU64] > 63) ? 0 : puSrc1->au64[uU64] << puSrc2->au8[uU64 << 3];
    }
}


/*
 * PMADDWD / VPMADDWD
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pmaddwd_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    uDst.ai32[0] = (int32_t)uSrc1.ai16[0] * uSrc2.ai16[0] + (int32_t)uSrc1.ai16[1] * uSrc2.ai16[1];
    uDst.ai32[1] = (int32_t)uSrc1.ai16[2] * uSrc2.ai16[2] + (int32_t)uSrc1.ai16[3] * uSrc2.ai16[3];
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pmaddwd_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai32[0] = (int32_t)uSrc1.ai16[0] * puSrc->ai16[0] + (int32_t)uSrc1.ai16[1] * puSrc->ai16[1];
    puDst->ai32[1] = (int32_t)uSrc1.ai16[2] * puSrc->ai16[2] + (int32_t)uSrc1.ai16[3] * puSrc->ai16[3];
    puDst->ai32[2] = (int32_t)uSrc1.ai16[4] * puSrc->ai16[4] + (int32_t)uSrc1.ai16[5] * puSrc->ai16[5];
    puDst->ai32[3] = (int32_t)uSrc1.ai16[6] * puSrc->ai16[6] + (int32_t)uSrc1.ai16[7] * puSrc->ai16[7];
}

#endif


IEM_DECL_IMPL_DEF(void, iemAImpl_pmaddwd_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    uDst.ai32[0] = (int32_t)uSrc1.ai16[0] * uSrc2.ai16[0] + (int32_t)uSrc1.ai16[1] * uSrc2.ai16[1];
    uDst.ai32[1] = (int32_t)uSrc1.ai16[2] * uSrc2.ai16[2] + (int32_t)uSrc1.ai16[3] * uSrc2.ai16[3];
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pmaddwd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai32[0] = (int32_t)uSrc1.ai16[0] * puSrc->ai16[0] + (int32_t)uSrc1.ai16[1] * puSrc->ai16[1];
    puDst->ai32[1] = (int32_t)uSrc1.ai16[2] * puSrc->ai16[2] + (int32_t)uSrc1.ai16[3] * puSrc->ai16[3];
    puDst->ai32[2] = (int32_t)uSrc1.ai16[4] * puSrc->ai16[4] + (int32_t)uSrc1.ai16[5] * puSrc->ai16[5];
    puDst->ai32[3] = (int32_t)uSrc1.ai16[6] * puSrc->ai16[6] + (int32_t)uSrc1.ai16[7] * puSrc->ai16[7];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaddwd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->ai32[0] = (int32_t)puSrc1->ai16[0] * puSrc2->ai16[0] + (int32_t)puSrc1->ai16[1] * puSrc2->ai16[1];
    puDst->ai32[1] = (int32_t)puSrc1->ai16[2] * puSrc2->ai16[2] + (int32_t)puSrc1->ai16[3] * puSrc2->ai16[3];
    puDst->ai32[2] = (int32_t)puSrc1->ai16[4] * puSrc2->ai16[4] + (int32_t)puSrc1->ai16[5] * puSrc2->ai16[5];
    puDst->ai32[3] = (int32_t)puSrc1->ai16[6] * puSrc2->ai16[6] + (int32_t)puSrc1->ai16[7] * puSrc2->ai16[7];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaddwd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->ai32[0] = (int32_t)puSrc1->ai16[0] * puSrc2->ai16[0] + (int32_t)puSrc1->ai16[1] * puSrc2->ai16[1];
    puDst->ai32[1] = (int32_t)puSrc1->ai16[2] * puSrc2->ai16[2] + (int32_t)puSrc1->ai16[3] * puSrc2->ai16[3];
    puDst->ai32[2] = (int32_t)puSrc1->ai16[4] * puSrc2->ai16[4] + (int32_t)puSrc1->ai16[5] * puSrc2->ai16[5];
    puDst->ai32[3] = (int32_t)puSrc1->ai16[6] * puSrc2->ai16[6] + (int32_t)puSrc1->ai16[7] * puSrc2->ai16[7];
    puDst->ai32[4] = (int32_t)puSrc1->ai16[8] * puSrc2->ai16[8] + (int32_t)puSrc1->ai16[9] * puSrc2->ai16[9];
    puDst->ai32[5] = (int32_t)puSrc1->ai16[10] * puSrc2->ai16[10] + (int32_t)puSrc1->ai16[11] * puSrc2->ai16[11];
    puDst->ai32[6] = (int32_t)puSrc1->ai16[12] * puSrc2->ai16[12] + (int32_t)puSrc1->ai16[13] * puSrc2->ai16[13];
    puDst->ai32[7] = (int32_t)puSrc1->ai16[14] * puSrc2->ai16[14] + (int32_t)puSrc1->ai16[15] * puSrc2->ai16[15];
}


/*
 * PMAXUB / VPMAXUB / PMAXUW / VPMAXUW / PMAXUD / VPMAXUD
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pmaxub_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    uDst.au8[0] = RT_MAX(uSrc1.au8[0], uSrc2.au8[0]);
    uDst.au8[1] = RT_MAX(uSrc1.au8[1], uSrc2.au8[1]);
    uDst.au8[2] = RT_MAX(uSrc1.au8[2], uSrc2.au8[2]);
    uDst.au8[3] = RT_MAX(uSrc1.au8[3], uSrc2.au8[3]);
    uDst.au8[4] = RT_MAX(uSrc1.au8[4], uSrc2.au8[4]);
    uDst.au8[5] = RT_MAX(uSrc1.au8[5], uSrc2.au8[5]);
    uDst.au8[6] = RT_MAX(uSrc1.au8[6], uSrc2.au8[6]);
    uDst.au8[7] = RT_MAX(uSrc1.au8[7], uSrc2.au8[7]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pmaxub_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->au8[ 0] = RT_MAX(uSrc1.au8[ 0], puSrc->au8[ 0]);
    puDst->au8[ 1] = RT_MAX(uSrc1.au8[ 1], puSrc->au8[ 1]);
    puDst->au8[ 2] = RT_MAX(uSrc1.au8[ 2], puSrc->au8[ 2]);
    puDst->au8[ 3] = RT_MAX(uSrc1.au8[ 3], puSrc->au8[ 3]);
    puDst->au8[ 4] = RT_MAX(uSrc1.au8[ 4], puSrc->au8[ 4]);
    puDst->au8[ 5] = RT_MAX(uSrc1.au8[ 5], puSrc->au8[ 5]);
    puDst->au8[ 6] = RT_MAX(uSrc1.au8[ 6], puSrc->au8[ 6]);
    puDst->au8[ 7] = RT_MAX(uSrc1.au8[ 7], puSrc->au8[ 7]);
    puDst->au8[ 8] = RT_MAX(uSrc1.au8[ 8], puSrc->au8[ 8]);
    puDst->au8[ 9] = RT_MAX(uSrc1.au8[ 9], puSrc->au8[ 9]);
    puDst->au8[10] = RT_MAX(uSrc1.au8[10], puSrc->au8[10]);
    puDst->au8[11] = RT_MAX(uSrc1.au8[11], puSrc->au8[11]);
    puDst->au8[12] = RT_MAX(uSrc1.au8[12], puSrc->au8[12]);
    puDst->au8[13] = RT_MAX(uSrc1.au8[13], puSrc->au8[13]);
    puDst->au8[14] = RT_MAX(uSrc1.au8[14], puSrc->au8[14]);
    puDst->au8[15] = RT_MAX(uSrc1.au8[15], puSrc->au8[15]);
}

#endif


IEM_DECL_IMPL_DEF(void, iemAImpl_pmaxuw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->au16[ 0] = RT_MAX(uSrc1.au16[ 0], puSrc->au16[ 0]);
    puDst->au16[ 1] = RT_MAX(uSrc1.au16[ 1], puSrc->au16[ 1]);
    puDst->au16[ 2] = RT_MAX(uSrc1.au16[ 2], puSrc->au16[ 2]);
    puDst->au16[ 3] = RT_MAX(uSrc1.au16[ 3], puSrc->au16[ 3]);
    puDst->au16[ 4] = RT_MAX(uSrc1.au16[ 4], puSrc->au16[ 4]);
    puDst->au16[ 5] = RT_MAX(uSrc1.au16[ 5], puSrc->au16[ 5]);
    puDst->au16[ 6] = RT_MAX(uSrc1.au16[ 6], puSrc->au16[ 6]);
    puDst->au16[ 7] = RT_MAX(uSrc1.au16[ 7], puSrc->au16[ 7]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pmaxud_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->au32[ 0] = RT_MAX(uSrc1.au32[ 0], puSrc->au32[ 0]);
    puDst->au32[ 1] = RT_MAX(uSrc1.au32[ 1], puSrc->au32[ 1]);
    puDst->au32[ 2] = RT_MAX(uSrc1.au32[ 2], puSrc->au32[ 2]);
    puDst->au32[ 3] = RT_MAX(uSrc1.au32[ 3], puSrc->au32[ 3]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaxub_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au8[ 0] = RT_MAX(puSrc1->au8[ 0], puSrc2->au8[ 0]);
    puDst->au8[ 1] = RT_MAX(puSrc1->au8[ 1], puSrc2->au8[ 1]);
    puDst->au8[ 2] = RT_MAX(puSrc1->au8[ 2], puSrc2->au8[ 2]);
    puDst->au8[ 3] = RT_MAX(puSrc1->au8[ 3], puSrc2->au8[ 3]);
    puDst->au8[ 4] = RT_MAX(puSrc1->au8[ 4], puSrc2->au8[ 4]);
    puDst->au8[ 5] = RT_MAX(puSrc1->au8[ 5], puSrc2->au8[ 5]);
    puDst->au8[ 6] = RT_MAX(puSrc1->au8[ 6], puSrc2->au8[ 6]);
    puDst->au8[ 7] = RT_MAX(puSrc1->au8[ 7], puSrc2->au8[ 7]);
    puDst->au8[ 8] = RT_MAX(puSrc1->au8[ 8], puSrc2->au8[ 8]);
    puDst->au8[ 9] = RT_MAX(puSrc1->au8[ 9], puSrc2->au8[ 9]);
    puDst->au8[10] = RT_MAX(puSrc1->au8[10], puSrc2->au8[10]);
    puDst->au8[11] = RT_MAX(puSrc1->au8[11], puSrc2->au8[11]);
    puDst->au8[12] = RT_MAX(puSrc1->au8[12], puSrc2->au8[12]);
    puDst->au8[13] = RT_MAX(puSrc1->au8[13], puSrc2->au8[13]);
    puDst->au8[14] = RT_MAX(puSrc1->au8[14], puSrc2->au8[14]);
    puDst->au8[15] = RT_MAX(puSrc1->au8[15], puSrc2->au8[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaxub_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au8[ 0] = RT_MAX(puSrc1->au8[ 0], puSrc2->au8[ 0]);
    puDst->au8[ 1] = RT_MAX(puSrc1->au8[ 1], puSrc2->au8[ 1]);
    puDst->au8[ 2] = RT_MAX(puSrc1->au8[ 2], puSrc2->au8[ 2]);
    puDst->au8[ 3] = RT_MAX(puSrc1->au8[ 3], puSrc2->au8[ 3]);
    puDst->au8[ 4] = RT_MAX(puSrc1->au8[ 4], puSrc2->au8[ 4]);
    puDst->au8[ 5] = RT_MAX(puSrc1->au8[ 5], puSrc2->au8[ 5]);
    puDst->au8[ 6] = RT_MAX(puSrc1->au8[ 6], puSrc2->au8[ 6]);
    puDst->au8[ 7] = RT_MAX(puSrc1->au8[ 7], puSrc2->au8[ 7]);
    puDst->au8[ 8] = RT_MAX(puSrc1->au8[ 8], puSrc2->au8[ 8]);
    puDst->au8[ 9] = RT_MAX(puSrc1->au8[ 9], puSrc2->au8[ 9]);
    puDst->au8[10] = RT_MAX(puSrc1->au8[10], puSrc2->au8[10]);
    puDst->au8[11] = RT_MAX(puSrc1->au8[11], puSrc2->au8[11]);
    puDst->au8[12] = RT_MAX(puSrc1->au8[12], puSrc2->au8[12]);
    puDst->au8[13] = RT_MAX(puSrc1->au8[13], puSrc2->au8[13]);
    puDst->au8[14] = RT_MAX(puSrc1->au8[14], puSrc2->au8[14]);
    puDst->au8[15] = RT_MAX(puSrc1->au8[15], puSrc2->au8[15]);
    puDst->au8[16] = RT_MAX(puSrc1->au8[16], puSrc2->au8[16]);
    puDst->au8[17] = RT_MAX(puSrc1->au8[17], puSrc2->au8[17]);
    puDst->au8[18] = RT_MAX(puSrc1->au8[18], puSrc2->au8[18]);
    puDst->au8[19] = RT_MAX(puSrc1->au8[19], puSrc2->au8[19]);
    puDst->au8[20] = RT_MAX(puSrc1->au8[20], puSrc2->au8[20]);
    puDst->au8[21] = RT_MAX(puSrc1->au8[21], puSrc2->au8[21]);
    puDst->au8[22] = RT_MAX(puSrc1->au8[22], puSrc2->au8[22]);
    puDst->au8[23] = RT_MAX(puSrc1->au8[23], puSrc2->au8[23]);
    puDst->au8[24] = RT_MAX(puSrc1->au8[24], puSrc2->au8[24]);
    puDst->au8[25] = RT_MAX(puSrc1->au8[25], puSrc2->au8[25]);
    puDst->au8[26] = RT_MAX(puSrc1->au8[26], puSrc2->au8[26]);
    puDst->au8[27] = RT_MAX(puSrc1->au8[27], puSrc2->au8[27]);
    puDst->au8[28] = RT_MAX(puSrc1->au8[28], puSrc2->au8[28]);
    puDst->au8[29] = RT_MAX(puSrc1->au8[29], puSrc2->au8[29]);
    puDst->au8[30] = RT_MAX(puSrc1->au8[30], puSrc2->au8[30]);
    puDst->au8[31] = RT_MAX(puSrc1->au8[31], puSrc2->au8[31]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaxuw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au16[ 0] = RT_MAX(puSrc1->au16[ 0], puSrc2->au16[ 0]);
    puDst->au16[ 1] = RT_MAX(puSrc1->au16[ 1], puSrc2->au16[ 1]);
    puDst->au16[ 2] = RT_MAX(puSrc1->au16[ 2], puSrc2->au16[ 2]);
    puDst->au16[ 3] = RT_MAX(puSrc1->au16[ 3], puSrc2->au16[ 3]);
    puDst->au16[ 4] = RT_MAX(puSrc1->au16[ 4], puSrc2->au16[ 4]);
    puDst->au16[ 5] = RT_MAX(puSrc1->au16[ 5], puSrc2->au16[ 5]);
    puDst->au16[ 6] = RT_MAX(puSrc1->au16[ 6], puSrc2->au16[ 6]);
    puDst->au16[ 7] = RT_MAX(puSrc1->au16[ 7], puSrc2->au16[ 7]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaxuw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au16[ 0] = RT_MAX(puSrc1->au16[ 0], puSrc2->au16[ 0]);
    puDst->au16[ 1] = RT_MAX(puSrc1->au16[ 1], puSrc2->au16[ 1]);
    puDst->au16[ 2] = RT_MAX(puSrc1->au16[ 2], puSrc2->au16[ 2]);
    puDst->au16[ 3] = RT_MAX(puSrc1->au16[ 3], puSrc2->au16[ 3]);
    puDst->au16[ 4] = RT_MAX(puSrc1->au16[ 4], puSrc2->au16[ 4]);
    puDst->au16[ 5] = RT_MAX(puSrc1->au16[ 5], puSrc2->au16[ 5]);
    puDst->au16[ 6] = RT_MAX(puSrc1->au16[ 6], puSrc2->au16[ 6]);
    puDst->au16[ 7] = RT_MAX(puSrc1->au16[ 7], puSrc2->au16[ 7]);
    puDst->au16[ 8] = RT_MAX(puSrc1->au16[ 8], puSrc2->au16[ 8]);
    puDst->au16[ 9] = RT_MAX(puSrc1->au16[ 9], puSrc2->au16[ 9]);
    puDst->au16[10] = RT_MAX(puSrc1->au16[10], puSrc2->au16[10]);
    puDst->au16[11] = RT_MAX(puSrc1->au16[11], puSrc2->au16[11]);
    puDst->au16[12] = RT_MAX(puSrc1->au16[12], puSrc2->au16[12]);
    puDst->au16[13] = RT_MAX(puSrc1->au16[13], puSrc2->au16[13]);
    puDst->au16[14] = RT_MAX(puSrc1->au16[14], puSrc2->au16[14]);
    puDst->au16[15] = RT_MAX(puSrc1->au16[15], puSrc2->au16[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaxud_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au32[ 0] = RT_MAX(puSrc1->au32[ 0], puSrc2->au32[ 0]);
    puDst->au32[ 1] = RT_MAX(puSrc1->au32[ 1], puSrc2->au32[ 1]);
    puDst->au32[ 2] = RT_MAX(puSrc1->au32[ 2], puSrc2->au32[ 2]);
    puDst->au32[ 3] = RT_MAX(puSrc1->au32[ 3], puSrc2->au32[ 3]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaxud_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au32[ 0] = RT_MAX(puSrc1->au32[ 0], puSrc2->au32[ 0]);
    puDst->au32[ 1] = RT_MAX(puSrc1->au32[ 1], puSrc2->au32[ 1]);
    puDst->au32[ 2] = RT_MAX(puSrc1->au32[ 2], puSrc2->au32[ 2]);
    puDst->au32[ 3] = RT_MAX(puSrc1->au32[ 3], puSrc2->au32[ 3]);
    puDst->au32[ 4] = RT_MAX(puSrc1->au32[ 4], puSrc2->au32[ 4]);
    puDst->au32[ 5] = RT_MAX(puSrc1->au32[ 5], puSrc2->au32[ 5]);
    puDst->au32[ 6] = RT_MAX(puSrc1->au32[ 6], puSrc2->au32[ 6]);
    puDst->au32[ 7] = RT_MAX(puSrc1->au32[ 7], puSrc2->au32[ 7]);
}


/*
 * PMAXSB / VPMAXSB / PMAXSW / VPMAXSW / PMAXSD / VPMAXSD
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pmaxsw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    uDst.ai16[0] = RT_MAX(uSrc1.ai16[0], uSrc2.ai16[0]);
    uDst.ai16[1] = RT_MAX(uSrc1.ai16[1], uSrc2.ai16[1]);
    uDst.ai16[2] = RT_MAX(uSrc1.ai16[2], uSrc2.ai16[2]);
    uDst.ai16[3] = RT_MAX(uSrc1.ai16[3], uSrc2.ai16[3]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pmaxsw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai16[ 0] = RT_MAX(uSrc1.ai16[ 0], puSrc->ai16[ 0]);
    puDst->ai16[ 1] = RT_MAX(uSrc1.ai16[ 1], puSrc->ai16[ 1]);
    puDst->ai16[ 2] = RT_MAX(uSrc1.ai16[ 2], puSrc->ai16[ 2]);
    puDst->ai16[ 3] = RT_MAX(uSrc1.ai16[ 3], puSrc->ai16[ 3]);
    puDst->ai16[ 4] = RT_MAX(uSrc1.ai16[ 4], puSrc->ai16[ 4]);
    puDst->ai16[ 5] = RT_MAX(uSrc1.ai16[ 5], puSrc->ai16[ 5]);
    puDst->ai16[ 6] = RT_MAX(uSrc1.ai16[ 6], puSrc->ai16[ 6]);
    puDst->ai16[ 7] = RT_MAX(uSrc1.ai16[ 7], puSrc->ai16[ 7]);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_pmaxsb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai8[ 0] = RT_MAX(uSrc1.ai8[ 0], puSrc->ai8[ 0]);
    puDst->ai8[ 1] = RT_MAX(uSrc1.ai8[ 1], puSrc->ai8[ 1]);
    puDst->ai8[ 2] = RT_MAX(uSrc1.ai8[ 2], puSrc->ai8[ 2]);
    puDst->ai8[ 3] = RT_MAX(uSrc1.ai8[ 3], puSrc->ai8[ 3]);
    puDst->ai8[ 4] = RT_MAX(uSrc1.ai8[ 4], puSrc->ai8[ 4]);
    puDst->ai8[ 5] = RT_MAX(uSrc1.ai8[ 5], puSrc->ai8[ 5]);
    puDst->ai8[ 6] = RT_MAX(uSrc1.ai8[ 6], puSrc->ai8[ 6]);
    puDst->ai8[ 7] = RT_MAX(uSrc1.ai8[ 7], puSrc->ai8[ 7]);
    puDst->ai8[ 8] = RT_MAX(uSrc1.ai8[ 8], puSrc->ai8[ 8]);
    puDst->ai8[ 9] = RT_MAX(uSrc1.ai8[ 9], puSrc->ai8[ 9]);
    puDst->ai8[10] = RT_MAX(uSrc1.ai8[10], puSrc->ai8[10]);
    puDst->ai8[11] = RT_MAX(uSrc1.ai8[11], puSrc->ai8[11]);
    puDst->ai8[12] = RT_MAX(uSrc1.ai8[12], puSrc->ai8[12]);
    puDst->ai8[13] = RT_MAX(uSrc1.ai8[13], puSrc->ai8[13]);
    puDst->ai8[14] = RT_MAX(uSrc1.ai8[14], puSrc->ai8[14]);
    puDst->ai8[15] = RT_MAX(uSrc1.ai8[15], puSrc->ai8[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pmaxsd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai32[ 0] = RT_MAX(uSrc1.ai32[ 0], puSrc->ai32[ 0]);
    puDst->ai32[ 1] = RT_MAX(uSrc1.ai32[ 1], puSrc->ai32[ 1]);
    puDst->ai32[ 2] = RT_MAX(uSrc1.ai32[ 2], puSrc->ai32[ 2]);
    puDst->ai32[ 3] = RT_MAX(uSrc1.ai32[ 3], puSrc->ai32[ 3]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaxsb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->ai8[ 0] = RT_MAX(puSrc1->ai8[ 0], puSrc2->ai8[ 0]);
    puDst->ai8[ 1] = RT_MAX(puSrc1->ai8[ 1], puSrc2->ai8[ 1]);
    puDst->ai8[ 2] = RT_MAX(puSrc1->ai8[ 2], puSrc2->ai8[ 2]);
    puDst->ai8[ 3] = RT_MAX(puSrc1->ai8[ 3], puSrc2->ai8[ 3]);
    puDst->ai8[ 4] = RT_MAX(puSrc1->ai8[ 4], puSrc2->ai8[ 4]);
    puDst->ai8[ 5] = RT_MAX(puSrc1->ai8[ 5], puSrc2->ai8[ 5]);
    puDst->ai8[ 6] = RT_MAX(puSrc1->ai8[ 6], puSrc2->ai8[ 6]);
    puDst->ai8[ 7] = RT_MAX(puSrc1->ai8[ 7], puSrc2->ai8[ 7]);
    puDst->ai8[ 8] = RT_MAX(puSrc1->ai8[ 8], puSrc2->ai8[ 8]);
    puDst->ai8[ 9] = RT_MAX(puSrc1->ai8[ 9], puSrc2->ai8[ 9]);
    puDst->ai8[10] = RT_MAX(puSrc1->ai8[10], puSrc2->ai8[10]);
    puDst->ai8[11] = RT_MAX(puSrc1->ai8[11], puSrc2->ai8[11]);
    puDst->ai8[12] = RT_MAX(puSrc1->ai8[12], puSrc2->ai8[12]);
    puDst->ai8[13] = RT_MAX(puSrc1->ai8[13], puSrc2->ai8[13]);
    puDst->ai8[14] = RT_MAX(puSrc1->ai8[14], puSrc2->ai8[14]);
    puDst->ai8[15] = RT_MAX(puSrc1->ai8[15], puSrc2->ai8[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaxsb_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->ai8[ 0] = RT_MAX(puSrc1->ai8[ 0], puSrc2->ai8[ 0]);
    puDst->ai8[ 1] = RT_MAX(puSrc1->ai8[ 1], puSrc2->ai8[ 1]);
    puDst->ai8[ 2] = RT_MAX(puSrc1->ai8[ 2], puSrc2->ai8[ 2]);
    puDst->ai8[ 3] = RT_MAX(puSrc1->ai8[ 3], puSrc2->ai8[ 3]);
    puDst->ai8[ 4] = RT_MAX(puSrc1->ai8[ 4], puSrc2->ai8[ 4]);
    puDst->ai8[ 5] = RT_MAX(puSrc1->ai8[ 5], puSrc2->ai8[ 5]);
    puDst->ai8[ 6] = RT_MAX(puSrc1->ai8[ 6], puSrc2->ai8[ 6]);
    puDst->ai8[ 7] = RT_MAX(puSrc1->ai8[ 7], puSrc2->ai8[ 7]);
    puDst->ai8[ 8] = RT_MAX(puSrc1->ai8[ 8], puSrc2->ai8[ 8]);
    puDst->ai8[ 9] = RT_MAX(puSrc1->ai8[ 9], puSrc2->ai8[ 9]);
    puDst->ai8[10] = RT_MAX(puSrc1->ai8[10], puSrc2->ai8[10]);
    puDst->ai8[11] = RT_MAX(puSrc1->ai8[11], puSrc2->ai8[11]);
    puDst->ai8[12] = RT_MAX(puSrc1->ai8[12], puSrc2->ai8[12]);
    puDst->ai8[13] = RT_MAX(puSrc1->ai8[13], puSrc2->ai8[13]);
    puDst->ai8[14] = RT_MAX(puSrc1->ai8[14], puSrc2->ai8[14]);
    puDst->ai8[15] = RT_MAX(puSrc1->ai8[15], puSrc2->ai8[15]);
    puDst->ai8[16] = RT_MAX(puSrc1->ai8[16], puSrc2->ai8[16]);
    puDst->ai8[17] = RT_MAX(puSrc1->ai8[17], puSrc2->ai8[17]);
    puDst->ai8[18] = RT_MAX(puSrc1->ai8[18], puSrc2->ai8[18]);
    puDst->ai8[19] = RT_MAX(puSrc1->ai8[19], puSrc2->ai8[19]);
    puDst->ai8[20] = RT_MAX(puSrc1->ai8[20], puSrc2->ai8[20]);
    puDst->ai8[21] = RT_MAX(puSrc1->ai8[21], puSrc2->ai8[21]);
    puDst->ai8[22] = RT_MAX(puSrc1->ai8[22], puSrc2->ai8[22]);
    puDst->ai8[23] = RT_MAX(puSrc1->ai8[23], puSrc2->ai8[23]);
    puDst->ai8[24] = RT_MAX(puSrc1->ai8[24], puSrc2->ai8[24]);
    puDst->ai8[25] = RT_MAX(puSrc1->ai8[25], puSrc2->ai8[25]);
    puDst->ai8[26] = RT_MAX(puSrc1->ai8[26], puSrc2->ai8[26]);
    puDst->ai8[27] = RT_MAX(puSrc1->ai8[27], puSrc2->ai8[27]);
    puDst->ai8[28] = RT_MAX(puSrc1->ai8[28], puSrc2->ai8[28]);
    puDst->ai8[29] = RT_MAX(puSrc1->ai8[29], puSrc2->ai8[29]);
    puDst->ai8[30] = RT_MAX(puSrc1->ai8[30], puSrc2->ai8[30]);
    puDst->ai8[31] = RT_MAX(puSrc1->ai8[31], puSrc2->ai8[31]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaxsw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->ai16[ 0] = RT_MAX(puSrc1->ai16[ 0], puSrc2->ai16[ 0]);
    puDst->ai16[ 1] = RT_MAX(puSrc1->ai16[ 1], puSrc2->ai16[ 1]);
    puDst->ai16[ 2] = RT_MAX(puSrc1->ai16[ 2], puSrc2->ai16[ 2]);
    puDst->ai16[ 3] = RT_MAX(puSrc1->ai16[ 3], puSrc2->ai16[ 3]);
    puDst->ai16[ 4] = RT_MAX(puSrc1->ai16[ 4], puSrc2->ai16[ 4]);
    puDst->ai16[ 5] = RT_MAX(puSrc1->ai16[ 5], puSrc2->ai16[ 5]);
    puDst->ai16[ 6] = RT_MAX(puSrc1->ai16[ 6], puSrc2->ai16[ 6]);
    puDst->ai16[ 7] = RT_MAX(puSrc1->ai16[ 7], puSrc2->ai16[ 7]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaxsw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->ai16[ 0] = RT_MAX(puSrc1->ai16[ 0], puSrc2->ai16[ 0]);
    puDst->ai16[ 1] = RT_MAX(puSrc1->ai16[ 1], puSrc2->ai16[ 1]);
    puDst->ai16[ 2] = RT_MAX(puSrc1->ai16[ 2], puSrc2->ai16[ 2]);
    puDst->ai16[ 3] = RT_MAX(puSrc1->ai16[ 3], puSrc2->ai16[ 3]);
    puDst->ai16[ 4] = RT_MAX(puSrc1->ai16[ 4], puSrc2->ai16[ 4]);
    puDst->ai16[ 5] = RT_MAX(puSrc1->ai16[ 5], puSrc2->ai16[ 5]);
    puDst->ai16[ 6] = RT_MAX(puSrc1->ai16[ 6], puSrc2->ai16[ 6]);
    puDst->ai16[ 7] = RT_MAX(puSrc1->ai16[ 7], puSrc2->ai16[ 7]);
    puDst->ai16[ 8] = RT_MAX(puSrc1->ai16[ 8], puSrc2->ai16[ 8]);
    puDst->ai16[ 9] = RT_MAX(puSrc1->ai16[ 9], puSrc2->ai16[ 9]);
    puDst->ai16[10] = RT_MAX(puSrc1->ai16[10], puSrc2->ai16[10]);
    puDst->ai16[11] = RT_MAX(puSrc1->ai16[11], puSrc2->ai16[11]);
    puDst->ai16[12] = RT_MAX(puSrc1->ai16[12], puSrc2->ai16[12]);
    puDst->ai16[13] = RT_MAX(puSrc1->ai16[13], puSrc2->ai16[13]);
    puDst->ai16[14] = RT_MAX(puSrc1->ai16[14], puSrc2->ai16[14]);
    puDst->ai16[15] = RT_MAX(puSrc1->ai16[15], puSrc2->ai16[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaxsd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->ai32[ 0] = RT_MAX(puSrc1->ai32[ 0], puSrc2->ai32[ 0]);
    puDst->ai32[ 1] = RT_MAX(puSrc1->ai32[ 1], puSrc2->ai32[ 1]);
    puDst->ai32[ 2] = RT_MAX(puSrc1->ai32[ 2], puSrc2->ai32[ 2]);
    puDst->ai32[ 3] = RT_MAX(puSrc1->ai32[ 3], puSrc2->ai32[ 3]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaxsd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->ai32[ 0] = RT_MAX(puSrc1->ai32[ 0], puSrc2->ai32[ 0]);
    puDst->ai32[ 1] = RT_MAX(puSrc1->ai32[ 1], puSrc2->ai32[ 1]);
    puDst->ai32[ 2] = RT_MAX(puSrc1->ai32[ 2], puSrc2->ai32[ 2]);
    puDst->ai32[ 3] = RT_MAX(puSrc1->ai32[ 3], puSrc2->ai32[ 3]);
    puDst->ai32[ 4] = RT_MAX(puSrc1->ai32[ 4], puSrc2->ai32[ 4]);
    puDst->ai32[ 5] = RT_MAX(puSrc1->ai32[ 5], puSrc2->ai32[ 5]);
    puDst->ai32[ 6] = RT_MAX(puSrc1->ai32[ 6], puSrc2->ai32[ 6]);
    puDst->ai32[ 7] = RT_MAX(puSrc1->ai32[ 7], puSrc2->ai32[ 7]);
}


/*
 * PMINUB / VPMINUB / PMINUW / VPMINUW / PMINUD / VPMINUD
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pminub_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    uDst.au8[0] = RT_MIN(uSrc1.au8[0], uSrc2.au8[0]);
    uDst.au8[1] = RT_MIN(uSrc1.au8[1], uSrc2.au8[1]);
    uDst.au8[2] = RT_MIN(uSrc1.au8[2], uSrc2.au8[2]);
    uDst.au8[3] = RT_MIN(uSrc1.au8[3], uSrc2.au8[3]);
    uDst.au8[4] = RT_MIN(uSrc1.au8[4], uSrc2.au8[4]);
    uDst.au8[5] = RT_MIN(uSrc1.au8[5], uSrc2.au8[5]);
    uDst.au8[6] = RT_MIN(uSrc1.au8[6], uSrc2.au8[6]);
    uDst.au8[7] = RT_MIN(uSrc1.au8[7], uSrc2.au8[7]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pminub_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->au8[ 0] = RT_MIN(uSrc1.au8[ 0], puSrc->au8[ 0]);
    puDst->au8[ 1] = RT_MIN(uSrc1.au8[ 1], puSrc->au8[ 1]);
    puDst->au8[ 2] = RT_MIN(uSrc1.au8[ 2], puSrc->au8[ 2]);
    puDst->au8[ 3] = RT_MIN(uSrc1.au8[ 3], puSrc->au8[ 3]);
    puDst->au8[ 4] = RT_MIN(uSrc1.au8[ 4], puSrc->au8[ 4]);
    puDst->au8[ 5] = RT_MIN(uSrc1.au8[ 5], puSrc->au8[ 5]);
    puDst->au8[ 6] = RT_MIN(uSrc1.au8[ 6], puSrc->au8[ 6]);
    puDst->au8[ 7] = RT_MIN(uSrc1.au8[ 7], puSrc->au8[ 7]);
    puDst->au8[ 8] = RT_MIN(uSrc1.au8[ 8], puSrc->au8[ 8]);
    puDst->au8[ 9] = RT_MIN(uSrc1.au8[ 9], puSrc->au8[ 9]);
    puDst->au8[10] = RT_MIN(uSrc1.au8[10], puSrc->au8[10]);
    puDst->au8[11] = RT_MIN(uSrc1.au8[11], puSrc->au8[11]);
    puDst->au8[12] = RT_MIN(uSrc1.au8[12], puSrc->au8[12]);
    puDst->au8[13] = RT_MIN(uSrc1.au8[13], puSrc->au8[13]);
    puDst->au8[14] = RT_MIN(uSrc1.au8[14], puSrc->au8[14]);
    puDst->au8[15] = RT_MIN(uSrc1.au8[15], puSrc->au8[15]);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_pminuw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->au16[ 0] = RT_MIN(uSrc1.au16[ 0], puSrc->au16[ 0]);
    puDst->au16[ 1] = RT_MIN(uSrc1.au16[ 1], puSrc->au16[ 1]);
    puDst->au16[ 2] = RT_MIN(uSrc1.au16[ 2], puSrc->au16[ 2]);
    puDst->au16[ 3] = RT_MIN(uSrc1.au16[ 3], puSrc->au16[ 3]);
    puDst->au16[ 4] = RT_MIN(uSrc1.au16[ 4], puSrc->au16[ 4]);
    puDst->au16[ 5] = RT_MIN(uSrc1.au16[ 5], puSrc->au16[ 5]);
    puDst->au16[ 6] = RT_MIN(uSrc1.au16[ 6], puSrc->au16[ 6]);
    puDst->au16[ 7] = RT_MIN(uSrc1.au16[ 7], puSrc->au16[ 7]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pminud_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->au32[ 0] = RT_MIN(uSrc1.au32[ 0], puSrc->au32[ 0]);
    puDst->au32[ 1] = RT_MIN(uSrc1.au32[ 1], puSrc->au32[ 1]);
    puDst->au32[ 2] = RT_MIN(uSrc1.au32[ 2], puSrc->au32[ 2]);
    puDst->au32[ 3] = RT_MIN(uSrc1.au32[ 3], puSrc->au32[ 3]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpminub_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au8[ 0] = RT_MIN(puSrc1->au8[ 0], puSrc2->au8[ 0]);
    puDst->au8[ 1] = RT_MIN(puSrc1->au8[ 1], puSrc2->au8[ 1]);
    puDst->au8[ 2] = RT_MIN(puSrc1->au8[ 2], puSrc2->au8[ 2]);
    puDst->au8[ 3] = RT_MIN(puSrc1->au8[ 3], puSrc2->au8[ 3]);
    puDst->au8[ 4] = RT_MIN(puSrc1->au8[ 4], puSrc2->au8[ 4]);
    puDst->au8[ 5] = RT_MIN(puSrc1->au8[ 5], puSrc2->au8[ 5]);
    puDst->au8[ 6] = RT_MIN(puSrc1->au8[ 6], puSrc2->au8[ 6]);
    puDst->au8[ 7] = RT_MIN(puSrc1->au8[ 7], puSrc2->au8[ 7]);
    puDst->au8[ 8] = RT_MIN(puSrc1->au8[ 8], puSrc2->au8[ 8]);
    puDst->au8[ 9] = RT_MIN(puSrc1->au8[ 9], puSrc2->au8[ 9]);
    puDst->au8[10] = RT_MIN(puSrc1->au8[10], puSrc2->au8[10]);
    puDst->au8[11] = RT_MIN(puSrc1->au8[11], puSrc2->au8[11]);
    puDst->au8[12] = RT_MIN(puSrc1->au8[12], puSrc2->au8[12]);
    puDst->au8[13] = RT_MIN(puSrc1->au8[13], puSrc2->au8[13]);
    puDst->au8[14] = RT_MIN(puSrc1->au8[14], puSrc2->au8[14]);
    puDst->au8[15] = RT_MIN(puSrc1->au8[15], puSrc2->au8[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpminub_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au8[ 0] = RT_MIN(puSrc1->au8[ 0], puSrc2->au8[ 0]);
    puDst->au8[ 1] = RT_MIN(puSrc1->au8[ 1], puSrc2->au8[ 1]);
    puDst->au8[ 2] = RT_MIN(puSrc1->au8[ 2], puSrc2->au8[ 2]);
    puDst->au8[ 3] = RT_MIN(puSrc1->au8[ 3], puSrc2->au8[ 3]);
    puDst->au8[ 4] = RT_MIN(puSrc1->au8[ 4], puSrc2->au8[ 4]);
    puDst->au8[ 5] = RT_MIN(puSrc1->au8[ 5], puSrc2->au8[ 5]);
    puDst->au8[ 6] = RT_MIN(puSrc1->au8[ 6], puSrc2->au8[ 6]);
    puDst->au8[ 7] = RT_MIN(puSrc1->au8[ 7], puSrc2->au8[ 7]);
    puDst->au8[ 8] = RT_MIN(puSrc1->au8[ 8], puSrc2->au8[ 8]);
    puDst->au8[ 9] = RT_MIN(puSrc1->au8[ 9], puSrc2->au8[ 9]);
    puDst->au8[10] = RT_MIN(puSrc1->au8[10], puSrc2->au8[10]);
    puDst->au8[11] = RT_MIN(puSrc1->au8[11], puSrc2->au8[11]);
    puDst->au8[12] = RT_MIN(puSrc1->au8[12], puSrc2->au8[12]);
    puDst->au8[13] = RT_MIN(puSrc1->au8[13], puSrc2->au8[13]);
    puDst->au8[14] = RT_MIN(puSrc1->au8[14], puSrc2->au8[14]);
    puDst->au8[15] = RT_MIN(puSrc1->au8[15], puSrc2->au8[15]);
    puDst->au8[16] = RT_MIN(puSrc1->au8[16], puSrc2->au8[16]);
    puDst->au8[17] = RT_MIN(puSrc1->au8[17], puSrc2->au8[17]);
    puDst->au8[18] = RT_MIN(puSrc1->au8[18], puSrc2->au8[18]);
    puDst->au8[19] = RT_MIN(puSrc1->au8[19], puSrc2->au8[19]);
    puDst->au8[20] = RT_MIN(puSrc1->au8[20], puSrc2->au8[20]);
    puDst->au8[21] = RT_MIN(puSrc1->au8[21], puSrc2->au8[21]);
    puDst->au8[22] = RT_MIN(puSrc1->au8[22], puSrc2->au8[22]);
    puDst->au8[23] = RT_MIN(puSrc1->au8[23], puSrc2->au8[23]);
    puDst->au8[24] = RT_MIN(puSrc1->au8[24], puSrc2->au8[24]);
    puDst->au8[25] = RT_MIN(puSrc1->au8[25], puSrc2->au8[25]);
    puDst->au8[26] = RT_MIN(puSrc1->au8[26], puSrc2->au8[26]);
    puDst->au8[27] = RT_MIN(puSrc1->au8[27], puSrc2->au8[27]);
    puDst->au8[28] = RT_MIN(puSrc1->au8[28], puSrc2->au8[28]);
    puDst->au8[29] = RT_MIN(puSrc1->au8[29], puSrc2->au8[29]);
    puDst->au8[30] = RT_MIN(puSrc1->au8[30], puSrc2->au8[30]);
    puDst->au8[31] = RT_MIN(puSrc1->au8[31], puSrc2->au8[31]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpminuw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au16[ 0] = RT_MIN(puSrc1->au16[ 0], puSrc2->au16[ 0]);
    puDst->au16[ 1] = RT_MIN(puSrc1->au16[ 1], puSrc2->au16[ 1]);
    puDst->au16[ 2] = RT_MIN(puSrc1->au16[ 2], puSrc2->au16[ 2]);
    puDst->au16[ 3] = RT_MIN(puSrc1->au16[ 3], puSrc2->au16[ 3]);
    puDst->au16[ 4] = RT_MIN(puSrc1->au16[ 4], puSrc2->au16[ 4]);
    puDst->au16[ 5] = RT_MIN(puSrc1->au16[ 5], puSrc2->au16[ 5]);
    puDst->au16[ 6] = RT_MIN(puSrc1->au16[ 6], puSrc2->au16[ 6]);
    puDst->au16[ 7] = RT_MIN(puSrc1->au16[ 7], puSrc2->au16[ 7]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpminuw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au16[ 0] = RT_MIN(puSrc1->au16[ 0], puSrc2->au16[ 0]);
    puDst->au16[ 1] = RT_MIN(puSrc1->au16[ 1], puSrc2->au16[ 1]);
    puDst->au16[ 2] = RT_MIN(puSrc1->au16[ 2], puSrc2->au16[ 2]);
    puDst->au16[ 3] = RT_MIN(puSrc1->au16[ 3], puSrc2->au16[ 3]);
    puDst->au16[ 4] = RT_MIN(puSrc1->au16[ 4], puSrc2->au16[ 4]);
    puDst->au16[ 5] = RT_MIN(puSrc1->au16[ 5], puSrc2->au16[ 5]);
    puDst->au16[ 6] = RT_MIN(puSrc1->au16[ 6], puSrc2->au16[ 6]);
    puDst->au16[ 7] = RT_MIN(puSrc1->au16[ 7], puSrc2->au16[ 7]);
    puDst->au16[ 8] = RT_MIN(puSrc1->au16[ 8], puSrc2->au16[ 8]);
    puDst->au16[ 9] = RT_MIN(puSrc1->au16[ 9], puSrc2->au16[ 9]);
    puDst->au16[10] = RT_MIN(puSrc1->au16[10], puSrc2->au16[10]);
    puDst->au16[11] = RT_MIN(puSrc1->au16[11], puSrc2->au16[11]);
    puDst->au16[12] = RT_MIN(puSrc1->au16[12], puSrc2->au16[12]);
    puDst->au16[13] = RT_MIN(puSrc1->au16[13], puSrc2->au16[13]);
    puDst->au16[14] = RT_MIN(puSrc1->au16[14], puSrc2->au16[14]);
    puDst->au16[15] = RT_MIN(puSrc1->au16[15], puSrc2->au16[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpminud_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au32[ 0] = RT_MIN(puSrc1->au32[ 0], puSrc2->au32[ 0]);
    puDst->au32[ 1] = RT_MIN(puSrc1->au32[ 1], puSrc2->au32[ 1]);
    puDst->au32[ 2] = RT_MIN(puSrc1->au32[ 2], puSrc2->au32[ 2]);
    puDst->au32[ 3] = RT_MIN(puSrc1->au32[ 3], puSrc2->au32[ 3]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpminud_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au32[ 0] = RT_MIN(puSrc1->au32[ 0], puSrc2->au32[ 0]);
    puDst->au32[ 1] = RT_MIN(puSrc1->au32[ 1], puSrc2->au32[ 1]);
    puDst->au32[ 2] = RT_MIN(puSrc1->au32[ 2], puSrc2->au32[ 2]);
    puDst->au32[ 3] = RT_MIN(puSrc1->au32[ 3], puSrc2->au32[ 3]);
    puDst->au32[ 4] = RT_MIN(puSrc1->au32[ 4], puSrc2->au32[ 4]);
    puDst->au32[ 5] = RT_MIN(puSrc1->au32[ 5], puSrc2->au32[ 5]);
    puDst->au32[ 6] = RT_MIN(puSrc1->au32[ 6], puSrc2->au32[ 6]);
    puDst->au32[ 7] = RT_MIN(puSrc1->au32[ 7], puSrc2->au32[ 7]);
}


/*
 * PMINSB / VPMINSB / PMINSW / VPMINSW / PMINSD / VPMINSD
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pminsw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    uDst.ai16[0] = RT_MIN(uSrc1.ai16[0], uSrc2.ai16[0]);
    uDst.ai16[1] = RT_MIN(uSrc1.ai16[1], uSrc2.ai16[1]);
    uDst.ai16[2] = RT_MIN(uSrc1.ai16[2], uSrc2.ai16[2]);
    uDst.ai16[3] = RT_MIN(uSrc1.ai16[3], uSrc2.ai16[3]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pminsw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai16[ 0] = RT_MIN(uSrc1.ai16[ 0], puSrc->ai16[ 0]);
    puDst->ai16[ 1] = RT_MIN(uSrc1.ai16[ 1], puSrc->ai16[ 1]);
    puDst->ai16[ 2] = RT_MIN(uSrc1.ai16[ 2], puSrc->ai16[ 2]);
    puDst->ai16[ 3] = RT_MIN(uSrc1.ai16[ 3], puSrc->ai16[ 3]);
    puDst->ai16[ 4] = RT_MIN(uSrc1.ai16[ 4], puSrc->ai16[ 4]);
    puDst->ai16[ 5] = RT_MIN(uSrc1.ai16[ 5], puSrc->ai16[ 5]);
    puDst->ai16[ 6] = RT_MIN(uSrc1.ai16[ 6], puSrc->ai16[ 6]);
    puDst->ai16[ 7] = RT_MIN(uSrc1.ai16[ 7], puSrc->ai16[ 7]);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_pminsb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai8[ 0] = RT_MIN(uSrc1.ai8[ 0], puSrc->ai8[ 0]);
    puDst->ai8[ 1] = RT_MIN(uSrc1.ai8[ 1], puSrc->ai8[ 1]);
    puDst->ai8[ 2] = RT_MIN(uSrc1.ai8[ 2], puSrc->ai8[ 2]);
    puDst->ai8[ 3] = RT_MIN(uSrc1.ai8[ 3], puSrc->ai8[ 3]);
    puDst->ai8[ 4] = RT_MIN(uSrc1.ai8[ 4], puSrc->ai8[ 4]);
    puDst->ai8[ 5] = RT_MIN(uSrc1.ai8[ 5], puSrc->ai8[ 5]);
    puDst->ai8[ 6] = RT_MIN(uSrc1.ai8[ 6], puSrc->ai8[ 6]);
    puDst->ai8[ 7] = RT_MIN(uSrc1.ai8[ 7], puSrc->ai8[ 7]);
    puDst->ai8[ 8] = RT_MIN(uSrc1.ai8[ 8], puSrc->ai8[ 8]);
    puDst->ai8[ 9] = RT_MIN(uSrc1.ai8[ 9], puSrc->ai8[ 9]);
    puDst->ai8[10] = RT_MIN(uSrc1.ai8[10], puSrc->ai8[10]);
    puDst->ai8[11] = RT_MIN(uSrc1.ai8[11], puSrc->ai8[11]);
    puDst->ai8[12] = RT_MIN(uSrc1.ai8[12], puSrc->ai8[12]);
    puDst->ai8[13] = RT_MIN(uSrc1.ai8[13], puSrc->ai8[13]);
    puDst->ai8[14] = RT_MIN(uSrc1.ai8[14], puSrc->ai8[14]);
    puDst->ai8[15] = RT_MIN(uSrc1.ai8[15], puSrc->ai8[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pminsd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai32[ 0] = RT_MIN(uSrc1.ai32[ 0], puSrc->ai32[ 0]);
    puDst->ai32[ 1] = RT_MIN(uSrc1.ai32[ 1], puSrc->ai32[ 1]);
    puDst->ai32[ 2] = RT_MIN(uSrc1.ai32[ 2], puSrc->ai32[ 2]);
    puDst->ai32[ 3] = RT_MIN(uSrc1.ai32[ 3], puSrc->ai32[ 3]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpminsb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->ai8[ 0] = RT_MIN(puSrc1->ai8[ 0], puSrc2->ai8[ 0]);
    puDst->ai8[ 1] = RT_MIN(puSrc1->ai8[ 1], puSrc2->ai8[ 1]);
    puDst->ai8[ 2] = RT_MIN(puSrc1->ai8[ 2], puSrc2->ai8[ 2]);
    puDst->ai8[ 3] = RT_MIN(puSrc1->ai8[ 3], puSrc2->ai8[ 3]);
    puDst->ai8[ 4] = RT_MIN(puSrc1->ai8[ 4], puSrc2->ai8[ 4]);
    puDst->ai8[ 5] = RT_MIN(puSrc1->ai8[ 5], puSrc2->ai8[ 5]);
    puDst->ai8[ 6] = RT_MIN(puSrc1->ai8[ 6], puSrc2->ai8[ 6]);
    puDst->ai8[ 7] = RT_MIN(puSrc1->ai8[ 7], puSrc2->ai8[ 7]);
    puDst->ai8[ 8] = RT_MIN(puSrc1->ai8[ 8], puSrc2->ai8[ 8]);
    puDst->ai8[ 9] = RT_MIN(puSrc1->ai8[ 9], puSrc2->ai8[ 9]);
    puDst->ai8[10] = RT_MIN(puSrc1->ai8[10], puSrc2->ai8[10]);
    puDst->ai8[11] = RT_MIN(puSrc1->ai8[11], puSrc2->ai8[11]);
    puDst->ai8[12] = RT_MIN(puSrc1->ai8[12], puSrc2->ai8[12]);
    puDst->ai8[13] = RT_MIN(puSrc1->ai8[13], puSrc2->ai8[13]);
    puDst->ai8[14] = RT_MIN(puSrc1->ai8[14], puSrc2->ai8[14]);
    puDst->ai8[15] = RT_MIN(puSrc1->ai8[15], puSrc2->ai8[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpminsb_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->ai8[ 0] = RT_MIN(puSrc1->ai8[ 0], puSrc2->ai8[ 0]);
    puDst->ai8[ 1] = RT_MIN(puSrc1->ai8[ 1], puSrc2->ai8[ 1]);
    puDst->ai8[ 2] = RT_MIN(puSrc1->ai8[ 2], puSrc2->ai8[ 2]);
    puDst->ai8[ 3] = RT_MIN(puSrc1->ai8[ 3], puSrc2->ai8[ 3]);
    puDst->ai8[ 4] = RT_MIN(puSrc1->ai8[ 4], puSrc2->ai8[ 4]);
    puDst->ai8[ 5] = RT_MIN(puSrc1->ai8[ 5], puSrc2->ai8[ 5]);
    puDst->ai8[ 6] = RT_MIN(puSrc1->ai8[ 6], puSrc2->ai8[ 6]);
    puDst->ai8[ 7] = RT_MIN(puSrc1->ai8[ 7], puSrc2->ai8[ 7]);
    puDst->ai8[ 8] = RT_MIN(puSrc1->ai8[ 8], puSrc2->ai8[ 8]);
    puDst->ai8[ 9] = RT_MIN(puSrc1->ai8[ 9], puSrc2->ai8[ 9]);
    puDst->ai8[10] = RT_MIN(puSrc1->ai8[10], puSrc2->ai8[10]);
    puDst->ai8[11] = RT_MIN(puSrc1->ai8[11], puSrc2->ai8[11]);
    puDst->ai8[12] = RT_MIN(puSrc1->ai8[12], puSrc2->ai8[12]);
    puDst->ai8[13] = RT_MIN(puSrc1->ai8[13], puSrc2->ai8[13]);
    puDst->ai8[14] = RT_MIN(puSrc1->ai8[14], puSrc2->ai8[14]);
    puDst->ai8[15] = RT_MIN(puSrc1->ai8[15], puSrc2->ai8[15]);
    puDst->ai8[16] = RT_MIN(puSrc1->ai8[16], puSrc2->ai8[16]);
    puDst->ai8[17] = RT_MIN(puSrc1->ai8[17], puSrc2->ai8[17]);
    puDst->ai8[18] = RT_MIN(puSrc1->ai8[18], puSrc2->ai8[18]);
    puDst->ai8[19] = RT_MIN(puSrc1->ai8[19], puSrc2->ai8[19]);
    puDst->ai8[20] = RT_MIN(puSrc1->ai8[20], puSrc2->ai8[20]);
    puDst->ai8[21] = RT_MIN(puSrc1->ai8[21], puSrc2->ai8[21]);
    puDst->ai8[22] = RT_MIN(puSrc1->ai8[22], puSrc2->ai8[22]);
    puDst->ai8[23] = RT_MIN(puSrc1->ai8[23], puSrc2->ai8[23]);
    puDst->ai8[24] = RT_MIN(puSrc1->ai8[24], puSrc2->ai8[24]);
    puDst->ai8[25] = RT_MIN(puSrc1->ai8[25], puSrc2->ai8[25]);
    puDst->ai8[26] = RT_MIN(puSrc1->ai8[26], puSrc2->ai8[26]);
    puDst->ai8[27] = RT_MIN(puSrc1->ai8[27], puSrc2->ai8[27]);
    puDst->ai8[28] = RT_MIN(puSrc1->ai8[28], puSrc2->ai8[28]);
    puDst->ai8[29] = RT_MIN(puSrc1->ai8[29], puSrc2->ai8[29]);
    puDst->ai8[30] = RT_MIN(puSrc1->ai8[30], puSrc2->ai8[30]);
    puDst->ai8[31] = RT_MIN(puSrc1->ai8[31], puSrc2->ai8[31]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpminsw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->ai16[ 0] = RT_MIN(puSrc1->ai16[ 0], puSrc2->ai16[ 0]);
    puDst->ai16[ 1] = RT_MIN(puSrc1->ai16[ 1], puSrc2->ai16[ 1]);
    puDst->ai16[ 2] = RT_MIN(puSrc1->ai16[ 2], puSrc2->ai16[ 2]);
    puDst->ai16[ 3] = RT_MIN(puSrc1->ai16[ 3], puSrc2->ai16[ 3]);
    puDst->ai16[ 4] = RT_MIN(puSrc1->ai16[ 4], puSrc2->ai16[ 4]);
    puDst->ai16[ 5] = RT_MIN(puSrc1->ai16[ 5], puSrc2->ai16[ 5]);
    puDst->ai16[ 6] = RT_MIN(puSrc1->ai16[ 6], puSrc2->ai16[ 6]);
    puDst->ai16[ 7] = RT_MIN(puSrc1->ai16[ 7], puSrc2->ai16[ 7]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpminsw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->ai16[ 0] = RT_MIN(puSrc1->ai16[ 0], puSrc2->ai16[ 0]);
    puDst->ai16[ 1] = RT_MIN(puSrc1->ai16[ 1], puSrc2->ai16[ 1]);
    puDst->ai16[ 2] = RT_MIN(puSrc1->ai16[ 2], puSrc2->ai16[ 2]);
    puDst->ai16[ 3] = RT_MIN(puSrc1->ai16[ 3], puSrc2->ai16[ 3]);
    puDst->ai16[ 4] = RT_MIN(puSrc1->ai16[ 4], puSrc2->ai16[ 4]);
    puDst->ai16[ 5] = RT_MIN(puSrc1->ai16[ 5], puSrc2->ai16[ 5]);
    puDst->ai16[ 6] = RT_MIN(puSrc1->ai16[ 6], puSrc2->ai16[ 6]);
    puDst->ai16[ 7] = RT_MIN(puSrc1->ai16[ 7], puSrc2->ai16[ 7]);
    puDst->ai16[ 8] = RT_MIN(puSrc1->ai16[ 8], puSrc2->ai16[ 8]);
    puDst->ai16[ 9] = RT_MIN(puSrc1->ai16[ 9], puSrc2->ai16[ 9]);
    puDst->ai16[10] = RT_MIN(puSrc1->ai16[10], puSrc2->ai16[10]);
    puDst->ai16[11] = RT_MIN(puSrc1->ai16[11], puSrc2->ai16[11]);
    puDst->ai16[12] = RT_MIN(puSrc1->ai16[12], puSrc2->ai16[12]);
    puDst->ai16[13] = RT_MIN(puSrc1->ai16[13], puSrc2->ai16[13]);
    puDst->ai16[14] = RT_MIN(puSrc1->ai16[14], puSrc2->ai16[14]);
    puDst->ai16[15] = RT_MIN(puSrc1->ai16[15], puSrc2->ai16[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpminsd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->ai32[ 0] = RT_MIN(puSrc1->ai32[ 0], puSrc2->ai32[ 0]);
    puDst->ai32[ 1] = RT_MIN(puSrc1->ai32[ 1], puSrc2->ai32[ 1]);
    puDst->ai32[ 2] = RT_MIN(puSrc1->ai32[ 2], puSrc2->ai32[ 2]);
    puDst->ai32[ 3] = RT_MIN(puSrc1->ai32[ 3], puSrc2->ai32[ 3]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpminsd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->ai32[ 0] = RT_MIN(puSrc1->ai32[ 0], puSrc2->ai32[ 0]);
    puDst->ai32[ 1] = RT_MIN(puSrc1->ai32[ 1], puSrc2->ai32[ 1]);
    puDst->ai32[ 2] = RT_MIN(puSrc1->ai32[ 2], puSrc2->ai32[ 2]);
    puDst->ai32[ 3] = RT_MIN(puSrc1->ai32[ 3], puSrc2->ai32[ 3]);
    puDst->ai32[ 4] = RT_MIN(puSrc1->ai32[ 4], puSrc2->ai32[ 4]);
    puDst->ai32[ 5] = RT_MIN(puSrc1->ai32[ 5], puSrc2->ai32[ 5]);
    puDst->ai32[ 6] = RT_MIN(puSrc1->ai32[ 6], puSrc2->ai32[ 6]);
    puDst->ai32[ 7] = RT_MIN(puSrc1->ai32[ 7], puSrc2->ai32[ 7]);
}


/*
 * PAVGB / VPAVGB / PAVGW / VPAVGW
 */
#define PAVGB_EXEC(a_Src1, a_Src2) ((uint8_t)(((uint16_t)(a_Src1) + (a_Src2) + 1) >> 1))
#define PAVGW_EXEC(a_Src1, a_Src2) ((uint16_t)(((uint32_t)(a_Src1) + (a_Src2) + 1) >> 1))

#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pavgb_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    uDst.au8[0] = PAVGB_EXEC(uSrc1.au8[0], uSrc2.au8[0]);
    uDst.au8[1] = PAVGB_EXEC(uSrc1.au8[1], uSrc2.au8[1]);
    uDst.au8[2] = PAVGB_EXEC(uSrc1.au8[2], uSrc2.au8[2]);
    uDst.au8[3] = PAVGB_EXEC(uSrc1.au8[3], uSrc2.au8[3]);
    uDst.au8[4] = PAVGB_EXEC(uSrc1.au8[4], uSrc2.au8[4]);
    uDst.au8[5] = PAVGB_EXEC(uSrc1.au8[5], uSrc2.au8[5]);
    uDst.au8[6] = PAVGB_EXEC(uSrc1.au8[6], uSrc2.au8[6]);
    uDst.au8[7] = PAVGB_EXEC(uSrc1.au8[7], uSrc2.au8[7]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pavgb_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->au8[ 0] = PAVGB_EXEC(uSrc1.au8[ 0], puSrc->au8[ 0]);
    puDst->au8[ 1] = PAVGB_EXEC(uSrc1.au8[ 1], puSrc->au8[ 1]);
    puDst->au8[ 2] = PAVGB_EXEC(uSrc1.au8[ 2], puSrc->au8[ 2]);
    puDst->au8[ 3] = PAVGB_EXEC(uSrc1.au8[ 3], puSrc->au8[ 3]);
    puDst->au8[ 4] = PAVGB_EXEC(uSrc1.au8[ 4], puSrc->au8[ 4]);
    puDst->au8[ 5] = PAVGB_EXEC(uSrc1.au8[ 5], puSrc->au8[ 5]);
    puDst->au8[ 6] = PAVGB_EXEC(uSrc1.au8[ 6], puSrc->au8[ 6]);
    puDst->au8[ 7] = PAVGB_EXEC(uSrc1.au8[ 7], puSrc->au8[ 7]);
    puDst->au8[ 8] = PAVGB_EXEC(uSrc1.au8[ 8], puSrc->au8[ 8]);
    puDst->au8[ 9] = PAVGB_EXEC(uSrc1.au8[ 9], puSrc->au8[ 9]);
    puDst->au8[10] = PAVGB_EXEC(uSrc1.au8[10], puSrc->au8[10]);
    puDst->au8[11] = PAVGB_EXEC(uSrc1.au8[11], puSrc->au8[11]);
    puDst->au8[12] = PAVGB_EXEC(uSrc1.au8[12], puSrc->au8[12]);
    puDst->au8[13] = PAVGB_EXEC(uSrc1.au8[13], puSrc->au8[13]);
    puDst->au8[14] = PAVGB_EXEC(uSrc1.au8[14], puSrc->au8[14]);
    puDst->au8[15] = PAVGB_EXEC(uSrc1.au8[15], puSrc->au8[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pavgw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    uDst.au16[0] = PAVGW_EXEC(uSrc1.au16[0], uSrc2.au16[0]);
    uDst.au16[1] = PAVGW_EXEC(uSrc1.au16[1], uSrc2.au16[1]);
    uDst.au16[2] = PAVGW_EXEC(uSrc1.au16[2], uSrc2.au16[2]);
    uDst.au16[3] = PAVGW_EXEC(uSrc1.au16[3], uSrc2.au16[3]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pavgw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->au16[0] = PAVGW_EXEC(uSrc1.au16[0], puSrc->au16[0]);
    puDst->au16[1] = PAVGW_EXEC(uSrc1.au16[1], puSrc->au16[1]);
    puDst->au16[2] = PAVGW_EXEC(uSrc1.au16[2], puSrc->au16[2]);
    puDst->au16[3] = PAVGW_EXEC(uSrc1.au16[3], puSrc->au16[3]);
    puDst->au16[4] = PAVGW_EXEC(uSrc1.au16[4], puSrc->au16[4]);
    puDst->au16[5] = PAVGW_EXEC(uSrc1.au16[5], puSrc->au16[5]);
    puDst->au16[6] = PAVGW_EXEC(uSrc1.au16[6], puSrc->au16[6]);
    puDst->au16[7] = PAVGW_EXEC(uSrc1.au16[7], puSrc->au16[7]);
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_pavgb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->au8[ 0] = PAVGB_EXEC(uSrc1.au8[ 0], puSrc->au8[ 0]);
    puDst->au8[ 1] = PAVGB_EXEC(uSrc1.au8[ 1], puSrc->au8[ 1]);
    puDst->au8[ 2] = PAVGB_EXEC(uSrc1.au8[ 2], puSrc->au8[ 2]);
    puDst->au8[ 3] = PAVGB_EXEC(uSrc1.au8[ 3], puSrc->au8[ 3]);
    puDst->au8[ 4] = PAVGB_EXEC(uSrc1.au8[ 4], puSrc->au8[ 4]);
    puDst->au8[ 5] = PAVGB_EXEC(uSrc1.au8[ 5], puSrc->au8[ 5]);
    puDst->au8[ 6] = PAVGB_EXEC(uSrc1.au8[ 6], puSrc->au8[ 6]);
    puDst->au8[ 7] = PAVGB_EXEC(uSrc1.au8[ 7], puSrc->au8[ 7]);
    puDst->au8[ 8] = PAVGB_EXEC(uSrc1.au8[ 8], puSrc->au8[ 8]);
    puDst->au8[ 9] = PAVGB_EXEC(uSrc1.au8[ 9], puSrc->au8[ 9]);
    puDst->au8[10] = PAVGB_EXEC(uSrc1.au8[10], puSrc->au8[10]);
    puDst->au8[11] = PAVGB_EXEC(uSrc1.au8[11], puSrc->au8[11]);
    puDst->au8[12] = PAVGB_EXEC(uSrc1.au8[12], puSrc->au8[12]);
    puDst->au8[13] = PAVGB_EXEC(uSrc1.au8[13], puSrc->au8[13]);
    puDst->au8[14] = PAVGB_EXEC(uSrc1.au8[14], puSrc->au8[14]);
    puDst->au8[15] = PAVGB_EXEC(uSrc1.au8[15], puSrc->au8[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pavgw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->au8[ 0] = PAVGW_EXEC(uSrc1.au8[ 0], puSrc->au8[ 0]);
    puDst->au8[ 1] = PAVGW_EXEC(uSrc1.au8[ 1], puSrc->au8[ 1]);
    puDst->au8[ 2] = PAVGW_EXEC(uSrc1.au8[ 2], puSrc->au8[ 2]);
    puDst->au8[ 3] = PAVGW_EXEC(uSrc1.au8[ 3], puSrc->au8[ 3]);
    puDst->au8[ 4] = PAVGW_EXEC(uSrc1.au8[ 4], puSrc->au8[ 4]);
    puDst->au8[ 5] = PAVGW_EXEC(uSrc1.au8[ 5], puSrc->au8[ 5]);
    puDst->au8[ 6] = PAVGW_EXEC(uSrc1.au8[ 6], puSrc->au8[ 6]);
    puDst->au8[ 7] = PAVGW_EXEC(uSrc1.au8[ 7], puSrc->au8[ 7]);
    puDst->au8[ 8] = PAVGW_EXEC(uSrc1.au8[ 8], puSrc->au8[ 8]);
    puDst->au8[ 9] = PAVGW_EXEC(uSrc1.au8[ 9], puSrc->au8[ 9]);
    puDst->au8[10] = PAVGW_EXEC(uSrc1.au8[10], puSrc->au8[10]);
    puDst->au8[11] = PAVGW_EXEC(uSrc1.au8[11], puSrc->au8[11]);
    puDst->au8[12] = PAVGW_EXEC(uSrc1.au8[12], puSrc->au8[12]);
    puDst->au8[13] = PAVGW_EXEC(uSrc1.au8[13], puSrc->au8[13]);
    puDst->au8[14] = PAVGW_EXEC(uSrc1.au8[14], puSrc->au8[14]);
    puDst->au8[15] = PAVGW_EXEC(uSrc1.au8[15], puSrc->au8[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpavgb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au8[ 0] = PAVGB_EXEC(puSrc1->au8[ 0], puSrc2->au8[ 0]);
    puDst->au8[ 1] = PAVGB_EXEC(puSrc1->au8[ 1], puSrc2->au8[ 1]);
    puDst->au8[ 2] = PAVGB_EXEC(puSrc1->au8[ 2], puSrc2->au8[ 2]);
    puDst->au8[ 3] = PAVGB_EXEC(puSrc1->au8[ 3], puSrc2->au8[ 3]);
    puDst->au8[ 4] = PAVGB_EXEC(puSrc1->au8[ 4], puSrc2->au8[ 4]);
    puDst->au8[ 5] = PAVGB_EXEC(puSrc1->au8[ 5], puSrc2->au8[ 5]);
    puDst->au8[ 6] = PAVGB_EXEC(puSrc1->au8[ 6], puSrc2->au8[ 6]);
    puDst->au8[ 7] = PAVGB_EXEC(puSrc1->au8[ 7], puSrc2->au8[ 7]);
    puDst->au8[ 8] = PAVGB_EXEC(puSrc1->au8[ 8], puSrc2->au8[ 8]);
    puDst->au8[ 9] = PAVGB_EXEC(puSrc1->au8[ 9], puSrc2->au8[ 9]);
    puDst->au8[10] = PAVGB_EXEC(puSrc1->au8[10], puSrc2->au8[10]);
    puDst->au8[11] = PAVGB_EXEC(puSrc1->au8[11], puSrc2->au8[11]);
    puDst->au8[12] = PAVGB_EXEC(puSrc1->au8[12], puSrc2->au8[12]);
    puDst->au8[13] = PAVGB_EXEC(puSrc1->au8[13], puSrc2->au8[13]);
    puDst->au8[14] = PAVGB_EXEC(puSrc1->au8[14], puSrc2->au8[14]);
    puDst->au8[15] = PAVGB_EXEC(puSrc1->au8[15], puSrc2->au8[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpavgb_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au8[ 0] = PAVGB_EXEC(puSrc1->au8[ 0], puSrc2->au8[ 0]);
    puDst->au8[ 1] = PAVGB_EXEC(puSrc1->au8[ 1], puSrc2->au8[ 1]);
    puDst->au8[ 2] = PAVGB_EXEC(puSrc1->au8[ 2], puSrc2->au8[ 2]);
    puDst->au8[ 3] = PAVGB_EXEC(puSrc1->au8[ 3], puSrc2->au8[ 3]);
    puDst->au8[ 4] = PAVGB_EXEC(puSrc1->au8[ 4], puSrc2->au8[ 4]);
    puDst->au8[ 5] = PAVGB_EXEC(puSrc1->au8[ 5], puSrc2->au8[ 5]);
    puDst->au8[ 6] = PAVGB_EXEC(puSrc1->au8[ 6], puSrc2->au8[ 6]);
    puDst->au8[ 7] = PAVGB_EXEC(puSrc1->au8[ 7], puSrc2->au8[ 7]);
    puDst->au8[ 8] = PAVGB_EXEC(puSrc1->au8[ 8], puSrc2->au8[ 8]);
    puDst->au8[ 9] = PAVGB_EXEC(puSrc1->au8[ 9], puSrc2->au8[ 9]);
    puDst->au8[10] = PAVGB_EXEC(puSrc1->au8[10], puSrc2->au8[10]);
    puDst->au8[11] = PAVGB_EXEC(puSrc1->au8[11], puSrc2->au8[11]);
    puDst->au8[12] = PAVGB_EXEC(puSrc1->au8[12], puSrc2->au8[12]);
    puDst->au8[13] = PAVGB_EXEC(puSrc1->au8[13], puSrc2->au8[13]);
    puDst->au8[14] = PAVGB_EXEC(puSrc1->au8[14], puSrc2->au8[14]);
    puDst->au8[15] = PAVGB_EXEC(puSrc1->au8[15], puSrc2->au8[15]);
    puDst->au8[16] = PAVGB_EXEC(puSrc1->au8[16], puSrc2->au8[16]);
    puDst->au8[17] = PAVGB_EXEC(puSrc1->au8[17], puSrc2->au8[17]);
    puDst->au8[18] = PAVGB_EXEC(puSrc1->au8[18], puSrc2->au8[18]);
    puDst->au8[19] = PAVGB_EXEC(puSrc1->au8[19], puSrc2->au8[19]);
    puDst->au8[20] = PAVGB_EXEC(puSrc1->au8[20], puSrc2->au8[20]);
    puDst->au8[21] = PAVGB_EXEC(puSrc1->au8[21], puSrc2->au8[21]);
    puDst->au8[22] = PAVGB_EXEC(puSrc1->au8[22], puSrc2->au8[22]);
    puDst->au8[23] = PAVGB_EXEC(puSrc1->au8[23], puSrc2->au8[23]);
    puDst->au8[24] = PAVGB_EXEC(puSrc1->au8[24], puSrc2->au8[24]);
    puDst->au8[25] = PAVGB_EXEC(puSrc1->au8[25], puSrc2->au8[25]);
    puDst->au8[26] = PAVGB_EXEC(puSrc1->au8[26], puSrc2->au8[26]);
    puDst->au8[27] = PAVGB_EXEC(puSrc1->au8[27], puSrc2->au8[27]);
    puDst->au8[28] = PAVGB_EXEC(puSrc1->au8[28], puSrc2->au8[28]);
    puDst->au8[29] = PAVGB_EXEC(puSrc1->au8[29], puSrc2->au8[29]);
    puDst->au8[30] = PAVGB_EXEC(puSrc1->au8[30], puSrc2->au8[30]);
    puDst->au8[31] = PAVGB_EXEC(puSrc1->au8[31], puSrc2->au8[31]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpavgw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    puDst->au16[ 0] = PAVGW_EXEC(puSrc1->au16[ 0], puSrc2->au16[ 0]);
    puDst->au16[ 1] = PAVGW_EXEC(puSrc1->au16[ 1], puSrc2->au16[ 1]);
    puDst->au16[ 2] = PAVGW_EXEC(puSrc1->au16[ 2], puSrc2->au16[ 2]);
    puDst->au16[ 3] = PAVGW_EXEC(puSrc1->au16[ 3], puSrc2->au16[ 3]);
    puDst->au16[ 4] = PAVGW_EXEC(puSrc1->au16[ 4], puSrc2->au16[ 4]);
    puDst->au16[ 5] = PAVGW_EXEC(puSrc1->au16[ 5], puSrc2->au16[ 5]);
    puDst->au16[ 6] = PAVGW_EXEC(puSrc1->au16[ 6], puSrc2->au16[ 6]);
    puDst->au16[ 7] = PAVGW_EXEC(puSrc1->au16[ 7], puSrc2->au16[ 7]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpavgw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au16[ 0] = PAVGW_EXEC(puSrc1->au16[ 0], puSrc2->au16[ 0]);
    puDst->au16[ 1] = PAVGW_EXEC(puSrc1->au16[ 1], puSrc2->au16[ 1]);
    puDst->au16[ 2] = PAVGW_EXEC(puSrc1->au16[ 2], puSrc2->au16[ 2]);
    puDst->au16[ 3] = PAVGW_EXEC(puSrc1->au16[ 3], puSrc2->au16[ 3]);
    puDst->au16[ 4] = PAVGW_EXEC(puSrc1->au16[ 4], puSrc2->au16[ 4]);
    puDst->au16[ 5] = PAVGW_EXEC(puSrc1->au16[ 5], puSrc2->au16[ 5]);
    puDst->au16[ 6] = PAVGW_EXEC(puSrc1->au16[ 6], puSrc2->au16[ 6]);
    puDst->au16[ 7] = PAVGW_EXEC(puSrc1->au16[ 7], puSrc2->au16[ 7]);
    puDst->au16[ 8] = PAVGW_EXEC(puSrc1->au16[ 8], puSrc2->au16[ 8]);
    puDst->au16[ 9] = PAVGW_EXEC(puSrc1->au16[ 9], puSrc2->au16[ 9]);
    puDst->au16[10] = PAVGW_EXEC(puSrc1->au16[10], puSrc2->au16[10]);
    puDst->au16[11] = PAVGW_EXEC(puSrc1->au16[11], puSrc2->au16[11]);
    puDst->au16[12] = PAVGW_EXEC(puSrc1->au16[12], puSrc2->au16[12]);
    puDst->au16[13] = PAVGW_EXEC(puSrc1->au16[13], puSrc2->au16[13]);
    puDst->au16[14] = PAVGW_EXEC(puSrc1->au16[14], puSrc2->au16[14]);
    puDst->au16[15] = PAVGW_EXEC(puSrc1->au16[15], puSrc2->au16[15]);
}

#undef PAVGB_EXEC
#undef PAVGW_EXEC


/*
 * PMOVMSKB / VPMOVMSKB
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pmovmskb_u64,(uint64_t *pu64Dst, uint64_t const *pu64Src))
{
    /* The the most signficant bit from each byte and store them in the given general purpose register. */
    uint64_t const uSrc = *pu64Src;
    *pu64Dst = ((uSrc >> ( 7-0)) & RT_BIT_64(0))
             | ((uSrc >> (15-1)) & RT_BIT_64(1))
             | ((uSrc >> (23-2)) & RT_BIT_64(2))
             | ((uSrc >> (31-3)) & RT_BIT_64(3))
             | ((uSrc >> (39-4)) & RT_BIT_64(4))
             | ((uSrc >> (47-5)) & RT_BIT_64(5))
             | ((uSrc >> (55-6)) & RT_BIT_64(6))
             | ((uSrc >> (63-7)) & RT_BIT_64(7));
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pmovmskb_u128,(uint64_t *pu64Dst, PCRTUINT128U pu128Src))
{
    /* The the most signficant bit from each byte and store them in the given general purpose register. */
    uint64_t const uSrc0 = pu128Src->QWords.qw0;
    uint64_t const uSrc1 = pu128Src->QWords.qw1;
    *pu64Dst = ((uSrc0 >> ( 7-0))        & RT_BIT_64(0))
             | ((uSrc0 >> (15-1))        & RT_BIT_64(1))
             | ((uSrc0 >> (23-2))        & RT_BIT_64(2))
             | ((uSrc0 >> (31-3))        & RT_BIT_64(3))
             | ((uSrc0 >> (39-4))        & RT_BIT_64(4))
             | ((uSrc0 >> (47-5))        & RT_BIT_64(5))
             | ((uSrc0 >> (55-6))        & RT_BIT_64(6))
             | ((uSrc0 >> (63-7))        & RT_BIT_64(7))
             | ((uSrc1 << (1 /*7-8*/))   & RT_BIT_64(8))
             | ((uSrc1 >> (15-9))        & RT_BIT_64(9))
             | ((uSrc1 >> (23-10))       & RT_BIT_64(10))
             | ((uSrc1 >> (31-11))       & RT_BIT_64(11))
             | ((uSrc1 >> (39-12))       & RT_BIT_64(12))
             | ((uSrc1 >> (47-13))       & RT_BIT_64(13))
             | ((uSrc1 >> (55-14))       & RT_BIT_64(14))
             | ((uSrc1 >> (63-15))       & RT_BIT_64(15));
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovmskb_u256_fallback,(uint64_t *pu64Dst, PCRTUINT256U puSrc))
{
    /* The the most signficant bit from each byte and store them in the given general purpose register. */
    uint64_t const uSrc0 = puSrc->QWords.qw0;
    uint64_t const uSrc1 = puSrc->QWords.qw1;
    uint64_t const uSrc2 = puSrc->QWords.qw2;
    uint64_t const uSrc3 = puSrc->QWords.qw3;
    *pu64Dst = ((uSrc0 >> ( 7-0))            & RT_BIT_64(0))
             | ((uSrc0 >> (15-1))            & RT_BIT_64(1))
             | ((uSrc0 >> (23-2))            & RT_BIT_64(2))
             | ((uSrc0 >> (31-3))            & RT_BIT_64(3))
             | ((uSrc0 >> (39-4))            & RT_BIT_64(4))
             | ((uSrc0 >> (47-5))            & RT_BIT_64(5))
             | ((uSrc0 >> (55-6))            & RT_BIT_64(6))
             | ((uSrc0 >> (63-7))            & RT_BIT_64(7))
             | ((uSrc1 << (1 /*7-8*/))       & RT_BIT_64(8))
             | ((uSrc1 >> (15-9))            & RT_BIT_64(9))
             | ((uSrc1 >> (23-10))           & RT_BIT_64(10))
             | ((uSrc1 >> (31-11))           & RT_BIT_64(11))
             | ((uSrc1 >> (39-12))           & RT_BIT_64(12))
             | ((uSrc1 >> (47-13))           & RT_BIT_64(13))
             | ((uSrc1 >> (55-14))           & RT_BIT_64(14))
             | ((uSrc1 >> (63-15))           & RT_BIT_64(15))
             | ((uSrc2 << (9 /* 7-16*/))     & RT_BIT_64(16))
             | ((uSrc2 << (2 /*15-17*/))     & RT_BIT_64(17))
             | ((uSrc2 >> (23-18))           & RT_BIT_64(18))
             | ((uSrc2 >> (31-19))           & RT_BIT_64(19))
             | ((uSrc2 >> (39-20))           & RT_BIT_64(20))
             | ((uSrc2 >> (47-21))           & RT_BIT_64(21))
             | ((uSrc2 >> (55-22))           & RT_BIT_64(22))
             | ((uSrc2 >> (63-23))           & RT_BIT_64(23))
             | ((uSrc3 << (17 /* 7-24*/))    & RT_BIT_64(24))
             | ((uSrc3 << (10 /*15-25*/))    & RT_BIT_64(25))
             | ((uSrc3 << (3  /*23-26*/))    & RT_BIT_64(26))
             | ((uSrc3 >> (31-27))           & RT_BIT_64(27))
             | ((uSrc3 >> (39-28))           & RT_BIT_64(28))
             | ((uSrc3 >> (47-29))           & RT_BIT_64(29))
             | ((uSrc3 >> (55-30))           & RT_BIT_64(30))
             | ((uSrc3 >> (63-31))           & RT_BIT_64(31));
}


/*
 * [V]PSHUFB
 */

IEM_DECL_IMPL_DEF(void, iemAImpl_pshufb_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U const uSrc    = { *puSrc };
    RTUINT64U const uDstIn  = { *puDst };
    ASMCompilerBarrier();
    RTUINT64U       uDstOut = { 0 };
    for (unsigned iByte = 0; iByte < RT_ELEMENTS(uDstIn.au8); iByte++)
    {
        uint8_t idxSrc = uSrc.au8[iByte];
        if (!(idxSrc & 0x80))
            uDstOut.au8[iByte] = uDstIn.au8[idxSrc & 7];
    }
    *puDst = uDstOut.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pshufb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U const uSrc    = *puSrc;
    RTUINT128U const uDstIn  = *puDst;
    ASMCompilerBarrier();
    puDst->au64[0] = 0;
    puDst->au64[1] = 0;
    for (unsigned iByte = 0; iByte < RT_ELEMENTS(puDst->au8); iByte++)
    {
        uint8_t idxSrc = uSrc.au8[iByte];
        if (!(idxSrc & 0x80))
            puDst->au8[iByte] = uDstIn.au8[idxSrc & 15];
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpshufb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc1 = *puSrc1; /* could be same as puDst */
    RTUINT128U const uSrc2 = *puSrc2; /* could be same as puDst */
    ASMCompilerBarrier();
    puDst->au64[0] = 0;
    puDst->au64[1] = 0;
    for (unsigned iByte = 0; iByte < 16; iByte++)
    {
        uint8_t idxSrc = uSrc2.au8[iByte];
        if (!(idxSrc & 0x80))
            puDst->au8[iByte] = uSrc1.au8[(idxSrc & 15)];
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpshufb_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc1 = *puSrc1; /* could be same as puDst */
    RTUINT256U const uSrc2 = *puSrc2; /* could be same as puDst */
    ASMCompilerBarrier();
    puDst->au64[0] = 0;
    puDst->au64[1] = 0;
    puDst->au64[2] = 0;
    puDst->au64[3] = 0;
    for (unsigned iByte = 0; iByte < 16; iByte++)
    {
        uint8_t idxSrc = uSrc2.au8[iByte];
        if (!(idxSrc & 0x80))
            puDst->au8[iByte] = uSrc1.au8[(idxSrc & 15)];
    }
    for (unsigned iByte = 16; iByte < RT_ELEMENTS(puDst->au8); iByte++)
    {
        uint8_t idxSrc = uSrc2.au8[iByte];
        if (!(idxSrc & 0x80))
            puDst->au8[iByte] = uSrc1.au8[(idxSrc & 15) + 16]; /* baka intel */
    }
}


/*
 * PSHUFW, [V]PSHUFHW, [V]PSHUFLW, [V]PSHUFD
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pshufw_u64,(uint64_t *puDst, uint64_t const *puSrc, uint8_t bEvil))
{
    uint64_t const uSrc = *puSrc;
    ASMCompilerBarrier();
    *puDst = RT_MAKE_U64_FROM_U16(uSrc >> (( bEvil       & 3) * 16),
                                  uSrc >> (((bEvil >> 2) & 3) * 16),
                                  uSrc >> (((bEvil >> 4) & 3) * 16),
                                  uSrc >> (((bEvil >> 6) & 3) * 16));
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pshufhw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    puDst->QWords.qw0   = puSrc->QWords.qw0;
    uint64_t const uSrc = puSrc->QWords.qw1;
    ASMCompilerBarrier();
    puDst->QWords.qw1   = RT_MAKE_U64_FROM_U16(uSrc >> (( bEvil       & 3) * 16),
                                               uSrc >> (((bEvil >> 2) & 3) * 16),
                                               uSrc >> (((bEvil >> 4) & 3) * 16),
                                               uSrc >> (((bEvil >> 6) & 3) * 16));
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpshufhw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t bEvil))
{
    puDst->QWords.qw0    = puSrc->QWords.qw0;
    uint64_t const uSrc1 = puSrc->QWords.qw1;
    puDst->QWords.qw2    = puSrc->QWords.qw2;
    uint64_t const uSrc3 = puSrc->QWords.qw3;
    ASMCompilerBarrier();
    puDst->QWords.qw1    = RT_MAKE_U64_FROM_U16(uSrc1 >> (( bEvil       & 3) * 16),
                                                uSrc1 >> (((bEvil >> 2) & 3) * 16),
                                                uSrc1 >> (((bEvil >> 4) & 3) * 16),
                                                uSrc1 >> (((bEvil >> 6) & 3) * 16));
    puDst->QWords.qw3    = RT_MAKE_U64_FROM_U16(uSrc3 >> (( bEvil       & 3) * 16),
                                                uSrc3 >> (((bEvil >> 2) & 3) * 16),
                                                uSrc3 >> (((bEvil >> 4) & 3) * 16),
                                                uSrc3 >> (((bEvil >> 6) & 3) * 16));
}

#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_pshuflw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    puDst->QWords.qw1   = puSrc->QWords.qw1;
    uint64_t const uSrc = puSrc->QWords.qw0;
    ASMCompilerBarrier();
    puDst->QWords.qw0   = RT_MAKE_U64_FROM_U16(uSrc >> (( bEvil       & 3) * 16),
                                               uSrc >> (((bEvil >> 2) & 3) * 16),
                                               uSrc >> (((bEvil >> 4) & 3) * 16),
                                               uSrc >> (((bEvil >> 6) & 3) * 16));

}
#endif


IEM_DECL_IMPL_DEF(void, iemAImpl_vpshuflw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t bEvil))
{
    puDst->QWords.qw3    = puSrc->QWords.qw3;
    uint64_t const uSrc2 = puSrc->QWords.qw2;
    puDst->QWords.qw1    = puSrc->QWords.qw1;
    uint64_t const uSrc0 = puSrc->QWords.qw0;
    ASMCompilerBarrier();
    puDst->QWords.qw0   = RT_MAKE_U64_FROM_U16(uSrc0 >> (( bEvil       & 3) * 16),
                                               uSrc0 >> (((bEvil >> 2) & 3) * 16),
                                               uSrc0 >> (((bEvil >> 4) & 3) * 16),
                                               uSrc0 >> (((bEvil >> 6) & 3) * 16));
    puDst->QWords.qw2   = RT_MAKE_U64_FROM_U16(uSrc2 >> (( bEvil       & 3) * 16),
                                               uSrc2 >> (((bEvil >> 2) & 3) * 16),
                                               uSrc2 >> (((bEvil >> 4) & 3) * 16),
                                               uSrc2 >> (((bEvil >> 6) & 3) * 16));

}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_pshufd_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    RTUINT128U const uSrc = *puSrc;
    ASMCompilerBarrier();
    puDst->au32[0] = uSrc.au32[bEvil & 3];
    puDst->au32[1] = uSrc.au32[(bEvil >> 2) & 3];
    puDst->au32[2] = uSrc.au32[(bEvil >> 4) & 3];
    puDst->au32[3] = uSrc.au32[(bEvil >> 6) & 3];
}
#endif


IEM_DECL_IMPL_DEF(void, iemAImpl_vpshufd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t bEvil))
{
    RTUINT256U const uSrc = *puSrc;
    ASMCompilerBarrier();
    puDst->au128[0].au32[0] = uSrc.au128[0].au32[bEvil & 3];
    puDst->au128[0].au32[1] = uSrc.au128[0].au32[(bEvil >> 2) & 3];
    puDst->au128[0].au32[2] = uSrc.au128[0].au32[(bEvil >> 4) & 3];
    puDst->au128[0].au32[3] = uSrc.au128[0].au32[(bEvil >> 6) & 3];
    puDst->au128[1].au32[0] = uSrc.au128[1].au32[bEvil & 3];
    puDst->au128[1].au32[1] = uSrc.au128[1].au32[(bEvil >> 2) & 3];
    puDst->au128[1].au32[2] = uSrc.au128[1].au32[(bEvil >> 4) & 3];
    puDst->au128[1].au32[3] = uSrc.au128[1].au32[(bEvil >> 6) & 3];
}


/*
 * PUNPCKHBW - high bytes -> words
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_punpckhbw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U const uSrc2 = { *puSrc };
    RTUINT64U const uSrc1 = { *puDst };
    ASMCompilerBarrier();
    RTUINT64U uDstOut;
    uDstOut.au8[0] = uSrc1.au8[4];
    uDstOut.au8[1] = uSrc2.au8[4];
    uDstOut.au8[2] = uSrc1.au8[5];
    uDstOut.au8[3] = uSrc2.au8[5];
    uDstOut.au8[4] = uSrc1.au8[6];
    uDstOut.au8[5] = uSrc2.au8[6];
    uDstOut.au8[6] = uSrc1.au8[7];
    uDstOut.au8[7] = uSrc2.au8[7];
    *puDst = uDstOut.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_punpckhbw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U const uSrc2 = *puSrc;
    RTUINT128U const uSrc1 = *puDst;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au8[ 0] = uSrc1.au8[ 8];
    uDstOut.au8[ 1] = uSrc2.au8[ 8];
    uDstOut.au8[ 2] = uSrc1.au8[ 9];
    uDstOut.au8[ 3] = uSrc2.au8[ 9];
    uDstOut.au8[ 4] = uSrc1.au8[10];
    uDstOut.au8[ 5] = uSrc2.au8[10];
    uDstOut.au8[ 6] = uSrc1.au8[11];
    uDstOut.au8[ 7] = uSrc2.au8[11];
    uDstOut.au8[ 8] = uSrc1.au8[12];
    uDstOut.au8[ 9] = uSrc2.au8[12];
    uDstOut.au8[10] = uSrc1.au8[13];
    uDstOut.au8[11] = uSrc2.au8[13];
    uDstOut.au8[12] = uSrc1.au8[14];
    uDstOut.au8[13] = uSrc2.au8[14];
    uDstOut.au8[14] = uSrc1.au8[15];
    uDstOut.au8[15] = uSrc2.au8[15];
    *puDst = uDstOut;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpckhbw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc2 = *puSrc2;
    RTUINT128U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au8[ 0] = uSrc1.au8[ 8];
    uDstOut.au8[ 1] = uSrc2.au8[ 8];
    uDstOut.au8[ 2] = uSrc1.au8[ 9];
    uDstOut.au8[ 3] = uSrc2.au8[ 9];
    uDstOut.au8[ 4] = uSrc1.au8[10];
    uDstOut.au8[ 5] = uSrc2.au8[10];
    uDstOut.au8[ 6] = uSrc1.au8[11];
    uDstOut.au8[ 7] = uSrc2.au8[11];
    uDstOut.au8[ 8] = uSrc1.au8[12];
    uDstOut.au8[ 9] = uSrc2.au8[12];
    uDstOut.au8[10] = uSrc1.au8[13];
    uDstOut.au8[11] = uSrc2.au8[13];
    uDstOut.au8[12] = uSrc1.au8[14];
    uDstOut.au8[13] = uSrc2.au8[14];
    uDstOut.au8[14] = uSrc1.au8[15];
    uDstOut.au8[15] = uSrc2.au8[15];
    *puDst = uDstOut;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpckhbw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc2 = *puSrc2;
    RTUINT256U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT256U uDstOut;
    uDstOut.au8[ 0] = uSrc1.au8[ 8];
    uDstOut.au8[ 1] = uSrc2.au8[ 8];
    uDstOut.au8[ 2] = uSrc1.au8[ 9];
    uDstOut.au8[ 3] = uSrc2.au8[ 9];
    uDstOut.au8[ 4] = uSrc1.au8[10];
    uDstOut.au8[ 5] = uSrc2.au8[10];
    uDstOut.au8[ 6] = uSrc1.au8[11];
    uDstOut.au8[ 7] = uSrc2.au8[11];
    uDstOut.au8[ 8] = uSrc1.au8[12];
    uDstOut.au8[ 9] = uSrc2.au8[12];
    uDstOut.au8[10] = uSrc1.au8[13];
    uDstOut.au8[11] = uSrc2.au8[13];
    uDstOut.au8[12] = uSrc1.au8[14];
    uDstOut.au8[13] = uSrc2.au8[14];
    uDstOut.au8[14] = uSrc1.au8[15];
    uDstOut.au8[15] = uSrc2.au8[15];
    /* As usual, the upper 128-bits are treated like a parallel register to the lower half. */
    uDstOut.au8[16] = uSrc1.au8[24];
    uDstOut.au8[17] = uSrc2.au8[24];
    uDstOut.au8[18] = uSrc1.au8[25];
    uDstOut.au8[19] = uSrc2.au8[25];
    uDstOut.au8[20] = uSrc1.au8[26];
    uDstOut.au8[21] = uSrc2.au8[26];
    uDstOut.au8[22] = uSrc1.au8[27];
    uDstOut.au8[23] = uSrc2.au8[27];
    uDstOut.au8[24] = uSrc1.au8[28];
    uDstOut.au8[25] = uSrc2.au8[28];
    uDstOut.au8[26] = uSrc1.au8[29];
    uDstOut.au8[27] = uSrc2.au8[29];
    uDstOut.au8[28] = uSrc1.au8[30];
    uDstOut.au8[29] = uSrc2.au8[30];
    uDstOut.au8[30] = uSrc1.au8[31];
    uDstOut.au8[31] = uSrc2.au8[31];
    *puDst = uDstOut;
}


/*
 * PUNPCKHBW - high words -> dwords
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_punpckhwd_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U const uSrc2 = { *puSrc };
    RTUINT64U const uSrc1 = { *puDst };
    ASMCompilerBarrier();
    RTUINT64U uDstOut;
    uDstOut.au16[0] = uSrc1.au16[2];
    uDstOut.au16[1] = uSrc2.au16[2];
    uDstOut.au16[2] = uSrc1.au16[3];
    uDstOut.au16[3] = uSrc2.au16[3];
    *puDst = uDstOut.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_punpckhwd_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U const uSrc2 = *puSrc;
    RTUINT128U const uSrc1 = *puDst;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au16[0] = uSrc1.au16[4];
    uDstOut.au16[1] = uSrc2.au16[4];
    uDstOut.au16[2] = uSrc1.au16[5];
    uDstOut.au16[3] = uSrc2.au16[5];
    uDstOut.au16[4] = uSrc1.au16[6];
    uDstOut.au16[5] = uSrc2.au16[6];
    uDstOut.au16[6] = uSrc1.au16[7];
    uDstOut.au16[7] = uSrc2.au16[7];
    *puDst = uDstOut;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpckhwd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc2 = *puSrc2;
    RTUINT128U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au16[0] = uSrc1.au16[4];
    uDstOut.au16[1] = uSrc2.au16[4];
    uDstOut.au16[2] = uSrc1.au16[5];
    uDstOut.au16[3] = uSrc2.au16[5];
    uDstOut.au16[4] = uSrc1.au16[6];
    uDstOut.au16[5] = uSrc2.au16[6];
    uDstOut.au16[6] = uSrc1.au16[7];
    uDstOut.au16[7] = uSrc2.au16[7];
    *puDst = uDstOut;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpckhwd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc2 = *puSrc2;
    RTUINT256U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT256U uDstOut;
    uDstOut.au16[0]  = uSrc1.au16[4];
    uDstOut.au16[1]  = uSrc2.au16[4];
    uDstOut.au16[2]  = uSrc1.au16[5];
    uDstOut.au16[3]  = uSrc2.au16[5];
    uDstOut.au16[4]  = uSrc1.au16[6];
    uDstOut.au16[5]  = uSrc2.au16[6];
    uDstOut.au16[6]  = uSrc1.au16[7];
    uDstOut.au16[7]  = uSrc2.au16[7];

    uDstOut.au16[8]  = uSrc1.au16[12];
    uDstOut.au16[9]  = uSrc2.au16[12];
    uDstOut.au16[10] = uSrc1.au16[13];
    uDstOut.au16[11] = uSrc2.au16[13];
    uDstOut.au16[12] = uSrc1.au16[14];
    uDstOut.au16[13] = uSrc2.au16[14];
    uDstOut.au16[14] = uSrc1.au16[15];
    uDstOut.au16[15] = uSrc2.au16[15];
    *puDst = uDstOut;
}


/*
 * PUNPCKHBW - high dwords -> qword(s)
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_punpckhdq_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U const uSrc2 = { *puSrc };
    RTUINT64U const uSrc1 = { *puDst };
    ASMCompilerBarrier();
    RTUINT64U uDstOut;
    uDstOut.au32[0] = uSrc1.au32[1];
    uDstOut.au32[1] = uSrc2.au32[1];
    *puDst = uDstOut.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_punpckhdq_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U const uSrc2 = *puSrc;
    RTUINT128U const uSrc1 = *puDst;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au32[0] = uSrc1.au32[2];
    uDstOut.au32[1] = uSrc2.au32[2];
    uDstOut.au32[2] = uSrc1.au32[3];
    uDstOut.au32[3] = uSrc2.au32[3];
    *puDst = uDstOut;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpckhdq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc2 = *puSrc2;
    RTUINT128U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au32[0] = uSrc1.au32[2];
    uDstOut.au32[1] = uSrc2.au32[2];
    uDstOut.au32[2] = uSrc1.au32[3];
    uDstOut.au32[3] = uSrc2.au32[3];
    *puDst = uDstOut;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpckhdq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc2 = *puSrc2;
    RTUINT256U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT256U uDstOut;
    uDstOut.au32[0] = uSrc1.au32[2];
    uDstOut.au32[1] = uSrc2.au32[2];
    uDstOut.au32[2] = uSrc1.au32[3];
    uDstOut.au32[3] = uSrc2.au32[3];

    uDstOut.au32[4] = uSrc1.au32[6];
    uDstOut.au32[5] = uSrc2.au32[6];
    uDstOut.au32[6] = uSrc1.au32[7];
    uDstOut.au32[7] = uSrc2.au32[7];
    *puDst = uDstOut;
}


/*
 * PUNPCKHQDQ -> High qwords -> double qword(s).
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_punpckhqdq_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U const uSrc2 = *puSrc;
    RTUINT128U const uSrc1 = *puDst;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au64[0] = uSrc1.au64[1];
    uDstOut.au64[1] = uSrc2.au64[1];
    *puDst = uDstOut;
}
#endif


IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpckhqdq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc2 = *puSrc2;
    RTUINT128U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au64[0] = uSrc1.au64[1];
    uDstOut.au64[1] = uSrc2.au64[1];
    *puDst = uDstOut;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpckhqdq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc2 = *puSrc2;
    RTUINT256U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT256U uDstOut;
    uDstOut.au64[0] = uSrc1.au64[1];
    uDstOut.au64[1] = uSrc2.au64[1];

    uDstOut.au64[2] = uSrc1.au64[3];
    uDstOut.au64[3] = uSrc2.au64[3];
    *puDst = uDstOut;
}


/*
 * PUNPCKLBW - low bytes -> words
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_punpcklbw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U const uSrc2 = { *puSrc };
    RTUINT64U const uSrc1 = { *puDst };
    ASMCompilerBarrier();
    RTUINT64U uDstOut;
    uDstOut.au8[0] = uSrc1.au8[0];
    uDstOut.au8[1] = uSrc2.au8[0];
    uDstOut.au8[2] = uSrc1.au8[1];
    uDstOut.au8[3] = uSrc2.au8[1];
    uDstOut.au8[4] = uSrc1.au8[2];
    uDstOut.au8[5] = uSrc2.au8[2];
    uDstOut.au8[6] = uSrc1.au8[3];
    uDstOut.au8[7] = uSrc2.au8[3];
    *puDst = uDstOut.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_punpcklbw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U const uSrc2 = *puSrc;
    RTUINT128U const uSrc1 = *puDst;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au8[ 0] = uSrc1.au8[0];
    uDstOut.au8[ 1] = uSrc2.au8[0];
    uDstOut.au8[ 2] = uSrc1.au8[1];
    uDstOut.au8[ 3] = uSrc2.au8[1];
    uDstOut.au8[ 4] = uSrc1.au8[2];
    uDstOut.au8[ 5] = uSrc2.au8[2];
    uDstOut.au8[ 6] = uSrc1.au8[3];
    uDstOut.au8[ 7] = uSrc2.au8[3];
    uDstOut.au8[ 8] = uSrc1.au8[4];
    uDstOut.au8[ 9] = uSrc2.au8[4];
    uDstOut.au8[10] = uSrc1.au8[5];
    uDstOut.au8[11] = uSrc2.au8[5];
    uDstOut.au8[12] = uSrc1.au8[6];
    uDstOut.au8[13] = uSrc2.au8[6];
    uDstOut.au8[14] = uSrc1.au8[7];
    uDstOut.au8[15] = uSrc2.au8[7];
    *puDst = uDstOut;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpcklbw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc2 = *puSrc2;
    RTUINT128U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au8[ 0] = uSrc1.au8[0];
    uDstOut.au8[ 1] = uSrc2.au8[0];
    uDstOut.au8[ 2] = uSrc1.au8[1];
    uDstOut.au8[ 3] = uSrc2.au8[1];
    uDstOut.au8[ 4] = uSrc1.au8[2];
    uDstOut.au8[ 5] = uSrc2.au8[2];
    uDstOut.au8[ 6] = uSrc1.au8[3];
    uDstOut.au8[ 7] = uSrc2.au8[3];
    uDstOut.au8[ 8] = uSrc1.au8[4];
    uDstOut.au8[ 9] = uSrc2.au8[4];
    uDstOut.au8[10] = uSrc1.au8[5];
    uDstOut.au8[11] = uSrc2.au8[5];
    uDstOut.au8[12] = uSrc1.au8[6];
    uDstOut.au8[13] = uSrc2.au8[6];
    uDstOut.au8[14] = uSrc1.au8[7];
    uDstOut.au8[15] = uSrc2.au8[7];
    *puDst = uDstOut;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpcklbw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc2 = *puSrc2;
    RTUINT256U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT256U uDstOut;
    uDstOut.au8[ 0] = uSrc1.au8[0];
    uDstOut.au8[ 1] = uSrc2.au8[0];
    uDstOut.au8[ 2] = uSrc1.au8[1];
    uDstOut.au8[ 3] = uSrc2.au8[1];
    uDstOut.au8[ 4] = uSrc1.au8[2];
    uDstOut.au8[ 5] = uSrc2.au8[2];
    uDstOut.au8[ 6] = uSrc1.au8[3];
    uDstOut.au8[ 7] = uSrc2.au8[3];
    uDstOut.au8[ 8] = uSrc1.au8[4];
    uDstOut.au8[ 9] = uSrc2.au8[4];
    uDstOut.au8[10] = uSrc1.au8[5];
    uDstOut.au8[11] = uSrc2.au8[5];
    uDstOut.au8[12] = uSrc1.au8[6];
    uDstOut.au8[13] = uSrc2.au8[6];
    uDstOut.au8[14] = uSrc1.au8[7];
    uDstOut.au8[15] = uSrc2.au8[7];
    /* As usual, the upper 128-bits are treated like a parallel register to the lower half. */
    uDstOut.au8[16] = uSrc1.au8[16];
    uDstOut.au8[17] = uSrc2.au8[16];
    uDstOut.au8[18] = uSrc1.au8[17];
    uDstOut.au8[19] = uSrc2.au8[17];
    uDstOut.au8[20] = uSrc1.au8[18];
    uDstOut.au8[21] = uSrc2.au8[18];
    uDstOut.au8[22] = uSrc1.au8[19];
    uDstOut.au8[23] = uSrc2.au8[19];
    uDstOut.au8[24] = uSrc1.au8[20];
    uDstOut.au8[25] = uSrc2.au8[20];
    uDstOut.au8[26] = uSrc1.au8[21];
    uDstOut.au8[27] = uSrc2.au8[21];
    uDstOut.au8[28] = uSrc1.au8[22];
    uDstOut.au8[29] = uSrc2.au8[22];
    uDstOut.au8[30] = uSrc1.au8[23];
    uDstOut.au8[31] = uSrc2.au8[23];
    *puDst = uDstOut;
}


/*
 * PUNPCKLBW - low words -> dwords
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_punpcklwd_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U const uSrc2 = { *puSrc };
    RTUINT64U const uSrc1 = { *puDst };
    ASMCompilerBarrier();
    RTUINT64U uDstOut;
    uDstOut.au16[0] = uSrc1.au16[0];
    uDstOut.au16[1] = uSrc2.au16[0];
    uDstOut.au16[2] = uSrc1.au16[1];
    uDstOut.au16[3] = uSrc2.au16[1];
    *puDst = uDstOut.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_punpcklwd_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U const uSrc2 = *puSrc;
    RTUINT128U const uSrc1 = *puDst;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au16[0] = uSrc1.au16[0];
    uDstOut.au16[1] = uSrc2.au16[0];
    uDstOut.au16[2] = uSrc1.au16[1];
    uDstOut.au16[3] = uSrc2.au16[1];
    uDstOut.au16[4] = uSrc1.au16[2];
    uDstOut.au16[5] = uSrc2.au16[2];
    uDstOut.au16[6] = uSrc1.au16[3];
    uDstOut.au16[7] = uSrc2.au16[3];
    *puDst = uDstOut;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpcklwd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc2 = *puSrc2;
    RTUINT128U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au16[0] = uSrc1.au16[0];
    uDstOut.au16[1] = uSrc2.au16[0];
    uDstOut.au16[2] = uSrc1.au16[1];
    uDstOut.au16[3] = uSrc2.au16[1];
    uDstOut.au16[4] = uSrc1.au16[2];
    uDstOut.au16[5] = uSrc2.au16[2];
    uDstOut.au16[6] = uSrc1.au16[3];
    uDstOut.au16[7] = uSrc2.au16[3];
    *puDst = uDstOut;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpcklwd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc2 = *puSrc2;
    RTUINT256U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT256U uDstOut;
    uDstOut.au16[0]  = uSrc1.au16[0];
    uDstOut.au16[1]  = uSrc2.au16[0];
    uDstOut.au16[2]  = uSrc1.au16[1];
    uDstOut.au16[3]  = uSrc2.au16[1];
    uDstOut.au16[4]  = uSrc1.au16[2];
    uDstOut.au16[5]  = uSrc2.au16[2];
    uDstOut.au16[6]  = uSrc1.au16[3];
    uDstOut.au16[7]  = uSrc2.au16[3];

    uDstOut.au16[8]  = uSrc1.au16[8];
    uDstOut.au16[9]  = uSrc2.au16[8];
    uDstOut.au16[10] = uSrc1.au16[9];
    uDstOut.au16[11] = uSrc2.au16[9];
    uDstOut.au16[12] = uSrc1.au16[10];
    uDstOut.au16[13] = uSrc2.au16[10];
    uDstOut.au16[14] = uSrc1.au16[11];
    uDstOut.au16[15] = uSrc2.au16[11];
    *puDst = uDstOut;
}


/*
 * PUNPCKLBW - low dwords -> qword(s)
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_punpckldq_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U const uSrc2 = { *puSrc };
    RTUINT64U const uSrc1 = { *puDst };
    ASMCompilerBarrier();
    RTUINT64U uDstOut;
    uDstOut.au32[0] = uSrc1.au32[0];
    uDstOut.au32[1] = uSrc2.au32[0];
    *puDst = uDstOut.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_punpckldq_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U const uSrc2 = *puSrc;
    RTUINT128U const uSrc1 = *puDst;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au32[0] = uSrc1.au32[0];
    uDstOut.au32[1] = uSrc2.au32[0];
    uDstOut.au32[2] = uSrc1.au32[1];
    uDstOut.au32[3] = uSrc2.au32[1];
    *puDst = uDstOut;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpckldq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc2 = *puSrc2;
    RTUINT128U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au32[0] = uSrc1.au32[0];
    uDstOut.au32[1] = uSrc2.au32[0];
    uDstOut.au32[2] = uSrc1.au32[1];
    uDstOut.au32[3] = uSrc2.au32[1];
    *puDst = uDstOut;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpckldq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc2 = *puSrc2;
    RTUINT256U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT256U uDstOut;
    uDstOut.au32[0] = uSrc1.au32[0];
    uDstOut.au32[1] = uSrc2.au32[0];
    uDstOut.au32[2] = uSrc1.au32[1];
    uDstOut.au32[3] = uSrc2.au32[1];

    uDstOut.au32[4] = uSrc1.au32[4];
    uDstOut.au32[5] = uSrc2.au32[4];
    uDstOut.au32[6] = uSrc1.au32[5];
    uDstOut.au32[7] = uSrc2.au32[5];
    *puDst = uDstOut;
}


/*
 * PUNPCKLQDQ -> Low qwords -> double qword(s).
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_punpcklqdq_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U const uSrc2 = *puSrc;
    RTUINT128U const uSrc1 = *puDst;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au64[0] = uSrc1.au64[0];
    uDstOut.au64[1] = uSrc2.au64[0];
    *puDst = uDstOut;
}
#endif


IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpcklqdq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc2 = *puSrc2;
    RTUINT128U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au64[0] = uSrc1.au64[0];
    uDstOut.au64[1] = uSrc2.au64[0];
    *puDst = uDstOut;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpunpcklqdq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc2 = *puSrc2;
    RTUINT256U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT256U uDstOut;
    uDstOut.au64[0] = uSrc1.au64[0];
    uDstOut.au64[1] = uSrc2.au64[0];

    uDstOut.au64[2] = uSrc1.au64[2];
    uDstOut.au64[3] = uSrc2.au64[2];
    *puDst = uDstOut;
}


/*
 * MASKMOVQ - Store Selected Bytes of Quadword
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_maskmovq_u64,(uint64_t *puMem, uint64_t const *puSrc, uint64_t const *puMsk))
{
    ASMCompilerBarrier();
    for (uint32_t i = 0; i < RT_ELEMENTS(((PCRTUINT64U)puMsk)->au8); i++)
    {
        if (((PCRTUINT64U)puMsk)->au8[i] & RT_BIT(7))
            ((PRTUINT64U)puMem)->au8[i] = ((PCRTUINT64U)puSrc)->au8[i];
    }
}


/*
 * MASKMOVDQU - Store Selected Bytes of Double Quadword
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_maskmovdqu_u128,(PRTUINT128U puMem, PCRTUINT128U puSrc, PCRTUINT128U puMsk))
{
    ASMCompilerBarrier();
    for (uint32_t i = 0; i < RT_ELEMENTS(puMsk->au8); i++)
    {
        if (puMsk->au8[i] & RT_BIT(7))
            puMem->au8[i] = puSrc->au8[i];
    }
}


/*
 * PACKSSWB - signed words -> signed bytes
 */

#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_packsswb_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U const uSrc2 = { *puSrc };
    RTUINT64U const uSrc1 = { *puDst };
    ASMCompilerBarrier();
    RTUINT64U uDstOut;
    uDstOut.au8[0]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[0]);
    uDstOut.au8[1]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[1]);
    uDstOut.au8[2]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[2]);
    uDstOut.au8[3]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[3]);
    uDstOut.au8[4]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[0]);
    uDstOut.au8[5]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[1]);
    uDstOut.au8[6]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[2]);
    uDstOut.au8[7]  = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[3]);
    *puDst = uDstOut.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_packsswb_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U const uSrc2 = *puSrc;
    RTUINT128U const uSrc1 = *puDst;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au8[ 0] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[0]);
    uDstOut.au8[ 1] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[1]);
    uDstOut.au8[ 2] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[2]);
    uDstOut.au8[ 3] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[3]);
    uDstOut.au8[ 4] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[4]);
    uDstOut.au8[ 5] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[5]);
    uDstOut.au8[ 6] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[6]);
    uDstOut.au8[ 7] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[7]);
    uDstOut.au8[ 8] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[0]);
    uDstOut.au8[ 9] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[1]);
    uDstOut.au8[10] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[2]);
    uDstOut.au8[11] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[3]);
    uDstOut.au8[12] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[4]);
    uDstOut.au8[13] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[5]);
    uDstOut.au8[14] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[6]);
    uDstOut.au8[15] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[7]);
    *puDst = uDstOut;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpacksswb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc2 = *puSrc2;
    RTUINT128U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au8[ 0] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[0]);
    uDstOut.au8[ 1] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[1]);
    uDstOut.au8[ 2] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[2]);
    uDstOut.au8[ 3] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[3]);
    uDstOut.au8[ 4] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[4]);
    uDstOut.au8[ 5] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[5]);
    uDstOut.au8[ 6] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[6]);
    uDstOut.au8[ 7] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[7]);
    uDstOut.au8[ 8] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[0]);
    uDstOut.au8[ 9] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[1]);
    uDstOut.au8[10] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[2]);
    uDstOut.au8[11] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[3]);
    uDstOut.au8[12] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[4]);
    uDstOut.au8[13] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[5]);
    uDstOut.au8[14] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[6]);
    uDstOut.au8[15] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[7]);
    *puDst = uDstOut;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpacksswb_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc2 = *puSrc2;
    RTUINT256U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT256U uDstOut;
    uDstOut.au8[ 0] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[0]);
    uDstOut.au8[ 1] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[1]);
    uDstOut.au8[ 2] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[2]);
    uDstOut.au8[ 3] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[3]);
    uDstOut.au8[ 4] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[4]);
    uDstOut.au8[ 5] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[5]);
    uDstOut.au8[ 6] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[6]);
    uDstOut.au8[ 7] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[7]);
    uDstOut.au8[ 8] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[0]);
    uDstOut.au8[ 9] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[1]);
    uDstOut.au8[10] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[2]);
    uDstOut.au8[11] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[3]);
    uDstOut.au8[12] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[4]);
    uDstOut.au8[13] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[5]);
    uDstOut.au8[14] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[6]);
    uDstOut.au8[15] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[7]);

    uDstOut.au8[16] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[ 8]);
    uDstOut.au8[17] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[ 9]);
    uDstOut.au8[18] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[10]);
    uDstOut.au8[19] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[11]);
    uDstOut.au8[20] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[12]);
    uDstOut.au8[21] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[13]);
    uDstOut.au8[22] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[14]);
    uDstOut.au8[23] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc1.au16[15]);
    uDstOut.au8[24] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[ 8]);
    uDstOut.au8[25] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[ 9]);
    uDstOut.au8[26] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[10]);
    uDstOut.au8[27] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[11]);
    uDstOut.au8[28] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[12]);
    uDstOut.au8[29] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[13]);
    uDstOut.au8[30] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[14]);
    uDstOut.au8[31] = SATURATED_SIGNED_WORD_TO_SIGNED_BYTE(uSrc2.au16[15]);
    *puDst = uDstOut;
}


/*
 * PACKUSWB - signed words -> unsigned bytes
 */
#define SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(a_iWord) \
        ( (uint16_t)(a_iWord) <= (uint16_t)0xff  \
          ? (uint8_t)(a_iWord) \
          : (uint8_t)0xff * (uint8_t)((((a_iWord) >> 15) & 1) ^ 1) ) /* 0xff = UINT8_MAX; 0x00 == UINT8_MIN; source bit 15 = sign */

#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_packuswb_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U const uSrc2 = { *puSrc };
    RTUINT64U const uSrc1 = { *puDst };
    ASMCompilerBarrier();
    RTUINT64U uDstOut;
    uDstOut.au8[0]  = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[0]);
    uDstOut.au8[1]  = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[1]);
    uDstOut.au8[2]  = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[2]);
    uDstOut.au8[3]  = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[3]);
    uDstOut.au8[4]  = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[0]);
    uDstOut.au8[5]  = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[1]);
    uDstOut.au8[6]  = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[2]);
    uDstOut.au8[7]  = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[3]);
    *puDst = uDstOut.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_packuswb_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U const uSrc2 = *puSrc;
    RTUINT128U const uSrc1 = *puDst;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au8[ 0] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[0]);
    uDstOut.au8[ 1] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[1]);
    uDstOut.au8[ 2] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[2]);
    uDstOut.au8[ 3] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[3]);
    uDstOut.au8[ 4] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[4]);
    uDstOut.au8[ 5] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[5]);
    uDstOut.au8[ 6] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[6]);
    uDstOut.au8[ 7] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[7]);
    uDstOut.au8[ 8] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[0]);
    uDstOut.au8[ 9] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[1]);
    uDstOut.au8[10] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[2]);
    uDstOut.au8[11] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[3]);
    uDstOut.au8[12] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[4]);
    uDstOut.au8[13] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[5]);
    uDstOut.au8[14] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[6]);
    uDstOut.au8[15] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[7]);
    *puDst = uDstOut;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpackuswb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc2 = *puSrc2;
    RTUINT128U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au8[ 0] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[0]);
    uDstOut.au8[ 1] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[1]);
    uDstOut.au8[ 2] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[2]);
    uDstOut.au8[ 3] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[3]);
    uDstOut.au8[ 4] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[4]);
    uDstOut.au8[ 5] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[5]);
    uDstOut.au8[ 6] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[6]);
    uDstOut.au8[ 7] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[7]);
    uDstOut.au8[ 8] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[0]);
    uDstOut.au8[ 9] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[1]);
    uDstOut.au8[10] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[2]);
    uDstOut.au8[11] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[3]);
    uDstOut.au8[12] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[4]);
    uDstOut.au8[13] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[5]);
    uDstOut.au8[14] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[6]);
    uDstOut.au8[15] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[7]);
    *puDst = uDstOut;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpackuswb_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc2 = *puSrc2;
    RTUINT256U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT256U uDstOut;
    uDstOut.au8[ 0] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[0]);
    uDstOut.au8[ 1] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[1]);
    uDstOut.au8[ 2] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[2]);
    uDstOut.au8[ 3] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[3]);
    uDstOut.au8[ 4] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[4]);
    uDstOut.au8[ 5] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[5]);
    uDstOut.au8[ 6] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[6]);
    uDstOut.au8[ 7] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[7]);
    uDstOut.au8[ 8] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[0]);
    uDstOut.au8[ 9] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[1]);
    uDstOut.au8[10] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[2]);
    uDstOut.au8[11] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[3]);
    uDstOut.au8[12] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[4]);
    uDstOut.au8[13] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[5]);
    uDstOut.au8[14] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[6]);
    uDstOut.au8[15] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[7]);

    uDstOut.au8[16] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[ 8]);
    uDstOut.au8[17] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[ 9]);
    uDstOut.au8[18] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[10]);
    uDstOut.au8[19] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[11]);
    uDstOut.au8[20] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[12]);
    uDstOut.au8[21] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[13]);
    uDstOut.au8[22] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[14]);
    uDstOut.au8[23] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc1.au16[15]);
    uDstOut.au8[24] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[ 8]);
    uDstOut.au8[25] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[ 9]);
    uDstOut.au8[26] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[10]);
    uDstOut.au8[27] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[11]);
    uDstOut.au8[28] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[12]);
    uDstOut.au8[29] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[13]);
    uDstOut.au8[30] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[14]);
    uDstOut.au8[31] = SATURATED_SIGNED_WORD_TO_UNSIGNED_BYTE(uSrc2.au16[15]);
    *puDst = uDstOut;
}


/*
 * PACKSSDW - signed dwords -> signed words
 */

#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_packssdw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U const uSrc2 = { *puSrc };
    RTUINT64U const uSrc1 = { *puDst };
    ASMCompilerBarrier();
    RTUINT64U uDstOut;
    uDstOut.au16[0]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[0]);
    uDstOut.au16[1]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[1]);
    uDstOut.au16[2]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[0]);
    uDstOut.au16[3]  = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[1]);
    *puDst = uDstOut.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_packssdw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U const uSrc2 = *puSrc;
    RTUINT128U const uSrc1 = *puDst;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au16[ 0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[0]);
    uDstOut.au16[ 1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[1]);
    uDstOut.au16[ 2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[2]);
    uDstOut.au16[ 3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[3]);
    uDstOut.au16[ 4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[0]);
    uDstOut.au16[ 5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[1]);
    uDstOut.au16[ 6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[2]);
    uDstOut.au16[ 7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[3]);
    *puDst = uDstOut;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpackssdw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc2 = *puSrc2;
    RTUINT128U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au16[ 0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[0]);
    uDstOut.au16[ 1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[1]);
    uDstOut.au16[ 2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[2]);
    uDstOut.au16[ 3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[3]);
    uDstOut.au16[ 4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[0]);
    uDstOut.au16[ 5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[1]);
    uDstOut.au16[ 6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[2]);
    uDstOut.au16[ 7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[3]);
    *puDst = uDstOut;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpackssdw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc2 = *puSrc2;
    RTUINT256U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT256U uDstOut;
    uDstOut.au16[ 0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[0]);
    uDstOut.au16[ 1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[1]);
    uDstOut.au16[ 2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[2]);
    uDstOut.au16[ 3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[3]);
    uDstOut.au16[ 4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[0]);
    uDstOut.au16[ 5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[1]);
    uDstOut.au16[ 6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[2]);
    uDstOut.au16[ 7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[3]);

    uDstOut.au16[ 8] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[4]);
    uDstOut.au16[ 9] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[5]);
    uDstOut.au16[10] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[6]);
    uDstOut.au16[11] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.au32[7]);
    uDstOut.au16[12] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[4]);
    uDstOut.au16[13] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[5]);
    uDstOut.au16[14] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[6]);
    uDstOut.au16[15] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.au32[7]);
    *puDst = uDstOut;
}


/*
 * PACKUSDW - signed dwords -> unsigned words
 */
#define SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(a_iDword) \
        ( (uint32_t)(a_iDword) <= (uint16_t)0xffff  \
          ? (uint16_t)(a_iDword) \
          : (uint16_t)0xffff * (uint16_t)((((a_iDword) >> 31) & 1) ^ 1) ) /* 0xffff = UINT16_MAX; source bit 31 = sign */

#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_packusdw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U const uSrc2 = *puSrc;
    RTUINT128U const uSrc1 = *puDst;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au16[ 0] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[0]);
    uDstOut.au16[ 1] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[1]);
    uDstOut.au16[ 2] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[2]);
    uDstOut.au16[ 3] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[3]);
    uDstOut.au16[ 4] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[0]);
    uDstOut.au16[ 5] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[1]);
    uDstOut.au16[ 6] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[2]);
    uDstOut.au16[ 7] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[3]);
    *puDst = uDstOut;
}
#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpackusdw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc2 = *puSrc2;
    RTUINT128U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT128U uDstOut;
    uDstOut.au16[ 0] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[0]);
    uDstOut.au16[ 1] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[1]);
    uDstOut.au16[ 2] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[2]);
    uDstOut.au16[ 3] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[3]);
    uDstOut.au16[ 4] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[0]);
    uDstOut.au16[ 5] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[1]);
    uDstOut.au16[ 6] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[2]);
    uDstOut.au16[ 7] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[3]);
    *puDst = uDstOut;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpackusdw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc2 = *puSrc2;
    RTUINT256U const uSrc1 = *puSrc1;
    ASMCompilerBarrier();
    RTUINT256U uDstOut;
    uDstOut.au16[ 0] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[0]);
    uDstOut.au16[ 1] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[1]);
    uDstOut.au16[ 2] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[2]);
    uDstOut.au16[ 3] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[3]);
    uDstOut.au16[ 4] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[0]);
    uDstOut.au16[ 5] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[1]);
    uDstOut.au16[ 6] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[2]);
    uDstOut.au16[ 7] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[3]);

    uDstOut.au16[ 8] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[4]);
    uDstOut.au16[ 9] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[5]);
    uDstOut.au16[10] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[6]);
    uDstOut.au16[11] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc1.au32[7]);
    uDstOut.au16[12] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[4]);
    uDstOut.au16[13] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[5]);
    uDstOut.au16[14] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[6]);
    uDstOut.au16[15] = SATURATED_SIGNED_DWORD_TO_UNSIGNED_WORD(uSrc2.au32[7]);
    *puDst = uDstOut;
}


/*
 * [V]PABSB / [V]PABSW / [V]PABSD
 */

IEM_DECL_IMPL_DEF(void, iemAImpl_pabsb_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U const uSrc    = { *puSrc };
    RTUINT64U       uDstOut = { 0 };

    uDstOut.au8[0] = RT_ABS(uSrc.ai8[0]);
    uDstOut.au8[1] = RT_ABS(uSrc.ai8[1]);
    uDstOut.au8[2] = RT_ABS(uSrc.ai8[2]);
    uDstOut.au8[3] = RT_ABS(uSrc.ai8[3]);
    uDstOut.au8[4] = RT_ABS(uSrc.ai8[4]);
    uDstOut.au8[5] = RT_ABS(uSrc.ai8[5]);
    uDstOut.au8[6] = RT_ABS(uSrc.ai8[6]);
    uDstOut.au8[7] = RT_ABS(uSrc.ai8[7]);
    *puDst = uDstOut.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pabsb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    puDst->au8[ 0] = RT_ABS(puSrc->ai8[ 0]);
    puDst->au8[ 1] = RT_ABS(puSrc->ai8[ 1]);
    puDst->au8[ 2] = RT_ABS(puSrc->ai8[ 2]);
    puDst->au8[ 3] = RT_ABS(puSrc->ai8[ 3]);
    puDst->au8[ 4] = RT_ABS(puSrc->ai8[ 4]);
    puDst->au8[ 5] = RT_ABS(puSrc->ai8[ 5]);
    puDst->au8[ 6] = RT_ABS(puSrc->ai8[ 6]);
    puDst->au8[ 7] = RT_ABS(puSrc->ai8[ 7]);
    puDst->au8[ 8] = RT_ABS(puSrc->ai8[ 8]);
    puDst->au8[ 9] = RT_ABS(puSrc->ai8[ 9]);
    puDst->au8[10] = RT_ABS(puSrc->ai8[10]);
    puDst->au8[11] = RT_ABS(puSrc->ai8[11]);
    puDst->au8[12] = RT_ABS(puSrc->ai8[12]);
    puDst->au8[13] = RT_ABS(puSrc->ai8[13]);
    puDst->au8[14] = RT_ABS(puSrc->ai8[14]);
    puDst->au8[15] = RT_ABS(puSrc->ai8[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pabsw_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U const uSrc    = { *puSrc };
    RTUINT64U       uDstOut = { 0 };

    uDstOut.au16[0] = RT_ABS(uSrc.ai16[0]);
    uDstOut.au16[1] = RT_ABS(uSrc.ai16[1]);
    uDstOut.au16[2] = RT_ABS(uSrc.ai16[2]);
    uDstOut.au16[3] = RT_ABS(uSrc.ai16[3]);
    *puDst = uDstOut.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pabsw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    puDst->au16[ 0] = RT_ABS(puSrc->ai16[ 0]);
    puDst->au16[ 1] = RT_ABS(puSrc->ai16[ 1]);
    puDst->au16[ 2] = RT_ABS(puSrc->ai16[ 2]);
    puDst->au16[ 3] = RT_ABS(puSrc->ai16[ 3]);
    puDst->au16[ 4] = RT_ABS(puSrc->ai16[ 4]);
    puDst->au16[ 5] = RT_ABS(puSrc->ai16[ 5]);
    puDst->au16[ 6] = RT_ABS(puSrc->ai16[ 6]);
    puDst->au16[ 7] = RT_ABS(puSrc->ai16[ 7]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pabsd_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U const uSrc    = { *puSrc };
    RTUINT64U       uDstOut = { 0 };

    uDstOut.au32[0] = RT_ABS(uSrc.ai32[0]);
    uDstOut.au32[1] = RT_ABS(uSrc.ai32[1]);
    *puDst = uDstOut.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pabsd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    puDst->au32[ 0] = RT_ABS(puSrc->ai32[ 0]);
    puDst->au32[ 1] = RT_ABS(puSrc->ai32[ 1]);
    puDst->au32[ 2] = RT_ABS(puSrc->ai32[ 2]);
    puDst->au32[ 3] = RT_ABS(puSrc->ai32[ 3]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpabsb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    puDst->au8[ 0] = RT_ABS(puSrc->ai8[ 0]);
    puDst->au8[ 1] = RT_ABS(puSrc->ai8[ 1]);
    puDst->au8[ 2] = RT_ABS(puSrc->ai8[ 2]);
    puDst->au8[ 3] = RT_ABS(puSrc->ai8[ 3]);
    puDst->au8[ 4] = RT_ABS(puSrc->ai8[ 4]);
    puDst->au8[ 5] = RT_ABS(puSrc->ai8[ 5]);
    puDst->au8[ 6] = RT_ABS(puSrc->ai8[ 6]);
    puDst->au8[ 7] = RT_ABS(puSrc->ai8[ 7]);
    puDst->au8[ 8] = RT_ABS(puSrc->ai8[ 8]);
    puDst->au8[ 9] = RT_ABS(puSrc->ai8[ 9]);
    puDst->au8[10] = RT_ABS(puSrc->ai8[10]);
    puDst->au8[11] = RT_ABS(puSrc->ai8[11]);
    puDst->au8[12] = RT_ABS(puSrc->ai8[12]);
    puDst->au8[13] = RT_ABS(puSrc->ai8[13]);
    puDst->au8[14] = RT_ABS(puSrc->ai8[14]);
    puDst->au8[15] = RT_ABS(puSrc->ai8[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpabsb_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc))
{
    puDst->au8[ 0] = RT_ABS(puSrc->ai8[ 0]);
    puDst->au8[ 1] = RT_ABS(puSrc->ai8[ 1]);
    puDst->au8[ 2] = RT_ABS(puSrc->ai8[ 2]);
    puDst->au8[ 3] = RT_ABS(puSrc->ai8[ 3]);
    puDst->au8[ 4] = RT_ABS(puSrc->ai8[ 4]);
    puDst->au8[ 5] = RT_ABS(puSrc->ai8[ 5]);
    puDst->au8[ 6] = RT_ABS(puSrc->ai8[ 6]);
    puDst->au8[ 7] = RT_ABS(puSrc->ai8[ 7]);
    puDst->au8[ 8] = RT_ABS(puSrc->ai8[ 8]);
    puDst->au8[ 9] = RT_ABS(puSrc->ai8[ 9]);
    puDst->au8[10] = RT_ABS(puSrc->ai8[10]);
    puDst->au8[11] = RT_ABS(puSrc->ai8[11]);
    puDst->au8[12] = RT_ABS(puSrc->ai8[12]);
    puDst->au8[13] = RT_ABS(puSrc->ai8[13]);
    puDst->au8[14] = RT_ABS(puSrc->ai8[14]);
    puDst->au8[15] = RT_ABS(puSrc->ai8[15]);
    puDst->au8[16] = RT_ABS(puSrc->ai8[16]);
    puDst->au8[17] = RT_ABS(puSrc->ai8[17]);
    puDst->au8[18] = RT_ABS(puSrc->ai8[18]);
    puDst->au8[19] = RT_ABS(puSrc->ai8[19]);
    puDst->au8[20] = RT_ABS(puSrc->ai8[20]);
    puDst->au8[21] = RT_ABS(puSrc->ai8[21]);
    puDst->au8[22] = RT_ABS(puSrc->ai8[22]);
    puDst->au8[23] = RT_ABS(puSrc->ai8[23]);
    puDst->au8[24] = RT_ABS(puSrc->ai8[24]);
    puDst->au8[25] = RT_ABS(puSrc->ai8[25]);
    puDst->au8[26] = RT_ABS(puSrc->ai8[26]);
    puDst->au8[27] = RT_ABS(puSrc->ai8[27]);
    puDst->au8[28] = RT_ABS(puSrc->ai8[28]);
    puDst->au8[29] = RT_ABS(puSrc->ai8[29]);
    puDst->au8[30] = RT_ABS(puSrc->ai8[30]);
    puDst->au8[31] = RT_ABS(puSrc->ai8[31]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpabsw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    puDst->au16[ 0] = RT_ABS(puSrc->ai16[ 0]);
    puDst->au16[ 1] = RT_ABS(puSrc->ai16[ 1]);
    puDst->au16[ 2] = RT_ABS(puSrc->ai16[ 2]);
    puDst->au16[ 3] = RT_ABS(puSrc->ai16[ 3]);
    puDst->au16[ 4] = RT_ABS(puSrc->ai16[ 4]);
    puDst->au16[ 5] = RT_ABS(puSrc->ai16[ 5]);
    puDst->au16[ 6] = RT_ABS(puSrc->ai16[ 6]);
    puDst->au16[ 7] = RT_ABS(puSrc->ai16[ 7]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpabsw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc))
{
    puDst->au16[ 0] = RT_ABS(puSrc->ai16[ 0]);
    puDst->au16[ 1] = RT_ABS(puSrc->ai16[ 1]);
    puDst->au16[ 2] = RT_ABS(puSrc->ai16[ 2]);
    puDst->au16[ 3] = RT_ABS(puSrc->ai16[ 3]);
    puDst->au16[ 4] = RT_ABS(puSrc->ai16[ 4]);
    puDst->au16[ 5] = RT_ABS(puSrc->ai16[ 5]);
    puDst->au16[ 6] = RT_ABS(puSrc->ai16[ 6]);
    puDst->au16[ 7] = RT_ABS(puSrc->ai16[ 7]);
    puDst->au16[ 8] = RT_ABS(puSrc->ai16[ 8]);
    puDst->au16[ 9] = RT_ABS(puSrc->ai16[ 9]);
    puDst->au16[10] = RT_ABS(puSrc->ai16[10]);
    puDst->au16[11] = RT_ABS(puSrc->ai16[11]);
    puDst->au16[12] = RT_ABS(puSrc->ai16[12]);
    puDst->au16[13] = RT_ABS(puSrc->ai16[13]);
    puDst->au16[14] = RT_ABS(puSrc->ai16[14]);
    puDst->au16[15] = RT_ABS(puSrc->ai16[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpabsd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    puDst->au32[ 0] = RT_ABS(puSrc->ai32[ 0]);
    puDst->au32[ 1] = RT_ABS(puSrc->ai32[ 1]);
    puDst->au32[ 2] = RT_ABS(puSrc->ai32[ 2]);
    puDst->au32[ 3] = RT_ABS(puSrc->ai32[ 3]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpabsd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc))
{
    puDst->au32[ 0] = RT_ABS(puSrc->ai32[ 0]);
    puDst->au32[ 1] = RT_ABS(puSrc->ai32[ 1]);
    puDst->au32[ 2] = RT_ABS(puSrc->ai32[ 2]);
    puDst->au32[ 3] = RT_ABS(puSrc->ai32[ 3]);
    puDst->au32[ 4] = RT_ABS(puSrc->ai32[ 4]);
    puDst->au32[ 5] = RT_ABS(puSrc->ai32[ 5]);
    puDst->au32[ 6] = RT_ABS(puSrc->ai32[ 6]);
    puDst->au32[ 7] = RT_ABS(puSrc->ai32[ 7]);
}


/*
 * PSIGNB / VPSIGNB / PSIGNW / VPSIGNW / PSIGND / VPSIGND
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_psignb_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst = { 0 }; /* Shut up MSVC. */

    for (uint32_t i = 0; i < RT_ELEMENTS(uDst.ai8); i++)
    {
        if (uSrc2.ai8[i] < 0)
            uDst.ai8[i] = -uSrc1.ai8[i];
        else if (uSrc2.ai8[i] == 0)
            uDst.ai8[i] = 0;
        else /* uSrc2.ai8[i] > 0 */
            uDst.ai8[i] = uSrc1.ai8[i];
    }

    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psignb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    for (uint32_t i = 0; i < RT_ELEMENTS(puDst->ai8); i++)
    {
        if (puSrc->ai8[i] < 0)
            puDst->ai8[i] = -uSrc1.ai8[i];
        else if (puSrc->ai8[i] == 0)
            puDst->ai8[i] = 0;
        else /* puSrc->ai8[i] > 0 */
            puDst->ai8[i] = uSrc1.ai8[i];
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psignw_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst = { 0 }; /* Shut up MSVC. */

    for (uint32_t i = 0; i < RT_ELEMENTS(uDst.ai16); i++)
    {
        if (uSrc2.ai16[i] < 0)
            uDst.ai16[i] = -uSrc1.ai16[i];
        else if (uSrc2.ai16[i] == 0)
            uDst.ai16[i] = 0;
        else /* uSrc2.ai16[i] > 0 */
            uDst.ai16[i] = uSrc1.ai16[i];
    }

    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psignw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    for (uint32_t i = 0; i < RT_ELEMENTS(puDst->ai16); i++)
    {
        if (puSrc->ai16[i] < 0)
            puDst->ai16[i] = -uSrc1.ai16[i];
        else if (puSrc->ai16[i] == 0)
            puDst->ai16[i] = 0;
        else /* puSrc->ai16[i] > 0 */
            puDst->ai16[i] = uSrc1.ai16[i];
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psignd_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst = { 0 }; /* Shut up MSVC. */

    for (uint32_t i = 0; i < RT_ELEMENTS(uDst.ai32); i++)
    {
        if (uSrc2.ai32[i] < 0)
            uDst.ai32[i] = -uSrc1.ai32[i];
        else if (uSrc2.ai32[i] == 0)
            uDst.ai32[i] = 0;
        else /* uSrc2.ai32[i] > 0 */
            uDst.ai32[i] = uSrc1.ai32[i];
    }

    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psignd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    for (uint32_t i = 0; i < RT_ELEMENTS(puDst->ai32); i++)
    {
        if (puSrc->ai32[i] < 0)
            puDst->ai32[i] = -uSrc1.ai32[i];
        else if (puSrc->ai32[i] == 0)
            puDst->ai32[i] = 0;
        else /* puSrc->ai32[i] > 0 */
            puDst->ai32[i] = uSrc1.ai32[i];
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpsignb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    for (uint32_t i = 0; i < RT_ELEMENTS(puDst->ai8); i++)
    {
        if (puSrc2->ai8[i] < 0)
            puDst->ai8[i] = -puSrc1->ai8[i];
        else if (puSrc2->ai8[i] == 0)
            puDst->ai8[i] = 0;
        else /* puSrc2->ai8[i] > 0 */
            puDst->ai8[i] = puSrc1->ai8[i];
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpsignb_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    for (uint32_t i = 0; i < RT_ELEMENTS(puDst->ai8); i++)
    {
        if (puSrc2->ai8[i] < 0)
            puDst->ai8[i] = -puSrc1->ai8[i];
        else if (puSrc2->ai8[i] == 0)
            puDst->ai8[i] = 0;
        else /* puSrc2->ai8[i] > 0 */
            puDst->ai8[i] = puSrc1->ai8[i];
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpsignw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    for (uint32_t i = 0; i < RT_ELEMENTS(puDst->ai16); i++)
    {
        if (puSrc2->ai16[i] < 0)
            puDst->ai16[i] = -puSrc1->ai16[i];
        else if (puSrc2->ai16[i] == 0)
            puDst->ai16[i] = 0;
        else /* puSrc2->ai16[i] > 0 */
            puDst->ai16[i] = puSrc1->ai16[i];
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpsignw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    for (uint32_t i = 0; i < RT_ELEMENTS(puDst->ai16); i++)
    {
        if (puSrc2->ai16[i] < 0)
            puDst->ai16[i] = -puSrc1->ai16[i];
        else if (puSrc2->ai16[i] == 0)
            puDst->ai16[i] = 0;
        else /* puSrc2->ai16[i] > 0 */
            puDst->ai16[i] = puSrc1->ai16[i];
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpsignd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    for (uint32_t i = 0; i < RT_ELEMENTS(puDst->ai32); i++)
    {
        if (puSrc2->ai32[i] < 0)
            puDst->ai32[i] = -puSrc1->ai32[i];
        else if (puSrc2->ai32[i] == 0)
            puDst->ai32[i] = 0;
        else /* puSrc2->ai32[i] > 0 */
            puDst->ai32[i] = puSrc1->ai32[i];
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpsignd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    for (uint32_t i = 0; i < RT_ELEMENTS(puDst->ai32); i++)
    {
        if (puSrc2->ai32[i] < 0)
            puDst->ai32[i] = -puSrc1->ai32[i];
        else if (puSrc2->ai32[i] == 0)
            puDst->ai32[i] = 0;
        else /* puSrc2->ai32[i] > 0 */
            puDst->ai32[i] = puSrc1->ai32[i];
    }
}


/*
 * PHADDW / VPHADDW / PHADDD / VPHADDD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_phaddw_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst = { 0 }; /* Shut up MSVC. */

    uDst.ai16[0] = uSrc1.ai16[0] + uSrc1.ai16[1];
    uDst.ai16[1] = uSrc1.ai16[2] + uSrc1.ai16[3];
    uDst.ai16[2] = uSrc2.ai16[0] + uSrc2.ai16[1];
    uDst.ai16[3] = uSrc2.ai16[2] + uSrc2.ai16[3];
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_phaddw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai16[0] = uSrc1.ai16[0] + uSrc1.ai16[1];
    puDst->ai16[1] = uSrc1.ai16[2] + uSrc1.ai16[3];
    puDst->ai16[2] = uSrc1.ai16[4] + uSrc1.ai16[5];
    puDst->ai16[3] = uSrc1.ai16[6] + uSrc1.ai16[7];

    puDst->ai16[4] = puSrc->ai16[0] + puSrc->ai16[1];
    puDst->ai16[5] = puSrc->ai16[2] + puSrc->ai16[3];
    puDst->ai16[6] = puSrc->ai16[4] + puSrc->ai16[5];
    puDst->ai16[7] = puSrc->ai16[6] + puSrc->ai16[7];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_phaddd_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst = { 0 }; /* Shut up MSVC. */

    uDst.ai32[0] = uSrc1.ai32[0] + uSrc1.ai32[1];
    uDst.ai32[1] = uSrc2.ai32[0] + uSrc2.ai32[1];
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_phaddd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai32[0] = uSrc1.ai32[0] + uSrc1.ai32[1];
    puDst->ai32[1] = uSrc1.ai32[2] + uSrc1.ai32[3];

    puDst->ai32[2] = puSrc->ai32[0] + puSrc->ai32[1];
    puDst->ai32[3] = puSrc->ai32[2] + puSrc->ai32[3];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vphaddw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai16[0] = puSrc1->ai16[0] + puSrc1->ai16[1];
    uDst.ai16[1] = puSrc1->ai16[2] + puSrc1->ai16[3];
    uDst.ai16[2] = puSrc1->ai16[4] + puSrc1->ai16[5];
    uDst.ai16[3] = puSrc1->ai16[6] + puSrc1->ai16[7];

    uDst.ai16[4] = puSrc2->ai16[0] + puSrc2->ai16[1];
    uDst.ai16[5] = puSrc2->ai16[2] + puSrc2->ai16[3];
    uDst.ai16[6] = puSrc2->ai16[4] + puSrc2->ai16[5];
    uDst.ai16[7] = puSrc2->ai16[6] + puSrc2->ai16[7];

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vphaddw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai16[ 0] = puSrc1->ai16[ 0] + puSrc1->ai16[ 1];
    uDst.ai16[ 1] = puSrc1->ai16[ 2] + puSrc1->ai16[ 3];
    uDst.ai16[ 2] = puSrc1->ai16[ 4] + puSrc1->ai16[ 5];
    uDst.ai16[ 3] = puSrc1->ai16[ 6] + puSrc1->ai16[ 7];
    uDst.ai16[ 4] = puSrc2->ai16[ 0] + puSrc2->ai16[ 1];
    uDst.ai16[ 5] = puSrc2->ai16[ 2] + puSrc2->ai16[ 3];
    uDst.ai16[ 6] = puSrc2->ai16[ 4] + puSrc2->ai16[ 5];
    uDst.ai16[ 7] = puSrc2->ai16[ 6] + puSrc2->ai16[ 7];

    uDst.ai16[ 8] = puSrc1->ai16[ 8] + puSrc1->ai16[ 9];
    uDst.ai16[ 9] = puSrc1->ai16[10] + puSrc1->ai16[11];
    uDst.ai16[10] = puSrc1->ai16[12] + puSrc1->ai16[13];
    uDst.ai16[11] = puSrc1->ai16[14] + puSrc1->ai16[15];
    uDst.ai16[12] = puSrc2->ai16[ 8] + puSrc2->ai16[ 9];
    uDst.ai16[13] = puSrc2->ai16[10] + puSrc2->ai16[11];
    uDst.ai16[14] = puSrc2->ai16[12] + puSrc2->ai16[13];
    uDst.ai16[15] = puSrc2->ai16[14] + puSrc2->ai16[15];

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
    puDst->au64[2] = uDst.au64[2];
    puDst->au64[3] = uDst.au64[3];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vphaddd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai32[0] = puSrc1->ai32[0] + puSrc1->ai32[1];
    uDst.ai32[1] = puSrc1->ai32[2] + puSrc1->ai32[3];

    uDst.ai32[2] = puSrc2->ai32[0] + puSrc2->ai32[1];
    uDst.ai32[3] = puSrc2->ai32[2] + puSrc2->ai32[3];

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vphaddd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai32[0] = puSrc1->ai32[ 0] + puSrc1->ai32[ 1];
    uDst.ai32[1] = puSrc1->ai32[ 2] + puSrc1->ai32[ 3];
    uDst.ai32[2] = puSrc2->ai32[ 0] + puSrc2->ai32[ 1];
    uDst.ai32[3] = puSrc2->ai32[ 2] + puSrc2->ai32[ 3];

    uDst.ai32[4] = puSrc1->ai32[ 4] + puSrc1->ai32[ 5];
    uDst.ai32[5] = puSrc1->ai32[ 6] + puSrc1->ai32[ 7];
    uDst.ai32[6] = puSrc2->ai32[ 4] + puSrc2->ai32[ 5];
    uDst.ai32[7] = puSrc2->ai32[ 6] + puSrc2->ai32[ 7];

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
    puDst->au64[2] = uDst.au64[2];
    puDst->au64[3] = uDst.au64[3];
}


/*
 * PHSUBW / VPHSUBW / PHSUBD / VPHSUBD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_phsubw_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst = { 0 }; /* Shut up MSVC. */

    uDst.ai16[0] = uSrc1.ai16[0] - uSrc1.ai16[1];
    uDst.ai16[1] = uSrc1.ai16[2] - uSrc1.ai16[3];
    uDst.ai16[2] = uSrc2.ai16[0] - uSrc2.ai16[1];
    uDst.ai16[3] = uSrc2.ai16[2] - uSrc2.ai16[3];
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_phsubw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai16[0] = uSrc1.ai16[0] - uSrc1.ai16[1];
    puDst->ai16[1] = uSrc1.ai16[2] - uSrc1.ai16[3];
    puDst->ai16[2] = uSrc1.ai16[4] - uSrc1.ai16[5];
    puDst->ai16[3] = uSrc1.ai16[6] - uSrc1.ai16[7];

    puDst->ai16[4] = puSrc->ai16[0] - puSrc->ai16[1];
    puDst->ai16[5] = puSrc->ai16[2] - puSrc->ai16[3];
    puDst->ai16[6] = puSrc->ai16[4] - puSrc->ai16[5];
    puDst->ai16[7] = puSrc->ai16[6] - puSrc->ai16[7];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_phsubd_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst = { 0 }; /* Shut up MSVC. */

    uDst.ai32[0] = uSrc1.ai32[0] - uSrc1.ai32[1];
    uDst.ai32[1] = uSrc2.ai32[0] - uSrc2.ai32[1];
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_phsubd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai32[0] = uSrc1.ai32[0] - uSrc1.ai32[1];
    puDst->ai32[1] = uSrc1.ai32[2] - uSrc1.ai32[3];

    puDst->ai32[2] = puSrc->ai32[0] - puSrc->ai32[1];
    puDst->ai32[3] = puSrc->ai32[2] - puSrc->ai32[3];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vphsubw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai16[0] = puSrc1->ai16[0] - puSrc1->ai16[1];
    uDst.ai16[1] = puSrc1->ai16[2] - puSrc1->ai16[3];
    uDst.ai16[2] = puSrc1->ai16[4] - puSrc1->ai16[5];
    uDst.ai16[3] = puSrc1->ai16[6] - puSrc1->ai16[7];

    uDst.ai16[4] = puSrc2->ai16[0] - puSrc2->ai16[1];
    uDst.ai16[5] = puSrc2->ai16[2] - puSrc2->ai16[3];
    uDst.ai16[6] = puSrc2->ai16[4] - puSrc2->ai16[5];
    uDst.ai16[7] = puSrc2->ai16[6] - puSrc2->ai16[7];

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vphsubw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai16[ 0] = puSrc1->ai16[ 0] - puSrc1->ai16[ 1];
    uDst.ai16[ 1] = puSrc1->ai16[ 2] - puSrc1->ai16[ 3];
    uDst.ai16[ 2] = puSrc1->ai16[ 4] - puSrc1->ai16[ 5];
    uDst.ai16[ 3] = puSrc1->ai16[ 6] - puSrc1->ai16[ 7];
    uDst.ai16[ 4] = puSrc2->ai16[ 0] - puSrc2->ai16[ 1];
    uDst.ai16[ 5] = puSrc2->ai16[ 2] - puSrc2->ai16[ 3];
    uDst.ai16[ 6] = puSrc2->ai16[ 4] - puSrc2->ai16[ 5];
    uDst.ai16[ 7] = puSrc2->ai16[ 6] - puSrc2->ai16[ 7];

    uDst.ai16[ 8] = puSrc1->ai16[ 8] - puSrc1->ai16[ 9];
    uDst.ai16[ 9] = puSrc1->ai16[10] - puSrc1->ai16[11];
    uDst.ai16[10] = puSrc1->ai16[12] - puSrc1->ai16[13];
    uDst.ai16[11] = puSrc1->ai16[14] - puSrc1->ai16[15];
    uDst.ai16[12] = puSrc2->ai16[ 8] - puSrc2->ai16[ 9];
    uDst.ai16[13] = puSrc2->ai16[10] - puSrc2->ai16[11];
    uDst.ai16[14] = puSrc2->ai16[12] - puSrc2->ai16[13];
    uDst.ai16[15] = puSrc2->ai16[14] - puSrc2->ai16[15];

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
    puDst->au64[2] = uDst.au64[2];
    puDst->au64[3] = uDst.au64[3];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vphsubd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai32[0] = puSrc1->ai32[0] - puSrc1->ai32[1];
    uDst.ai32[1] = puSrc1->ai32[2] - puSrc1->ai32[3];

    uDst.ai32[2] = puSrc2->ai32[0] - puSrc2->ai32[1];
    uDst.ai32[3] = puSrc2->ai32[2] - puSrc2->ai32[3];

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vphsubd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai32[0] = puSrc1->ai32[ 0] - puSrc1->ai32[ 1];
    uDst.ai32[1] = puSrc1->ai32[ 2] - puSrc1->ai32[ 3];
    uDst.ai32[2] = puSrc2->ai32[ 0] - puSrc2->ai32[ 1];
    uDst.ai32[3] = puSrc2->ai32[ 2] - puSrc2->ai32[ 3];

    uDst.ai32[4] = puSrc1->ai32[ 4] - puSrc1->ai32[ 5];
    uDst.ai32[5] = puSrc1->ai32[ 6] - puSrc1->ai32[ 7];
    uDst.ai32[6] = puSrc2->ai32[ 4] - puSrc2->ai32[ 5];
    uDst.ai32[7] = puSrc2->ai32[ 6] - puSrc2->ai32[ 7];

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
    puDst->au64[2] = uDst.au64[2];
    puDst->au64[3] = uDst.au64[3];
}


/*
 * PHADDSW / VPHADDSW
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_phaddsw_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst = { 0 }; /* Shut up MSVC. */

    uDst.ai16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[0] + uSrc1.ai16[1]);
    uDst.ai16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[2] + uSrc1.ai16[3]);
    uDst.ai16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.ai16[0] + uSrc2.ai16[1]);
    uDst.ai16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.ai16[2] + uSrc2.ai16[3]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_phaddsw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[0] + uSrc1.ai16[1]);
    puDst->ai16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[2] + uSrc1.ai16[3]);
    puDst->ai16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[4] + uSrc1.ai16[5]);
    puDst->ai16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[6] + uSrc1.ai16[7]);

    puDst->ai16[4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc->ai16[0] + puSrc->ai16[1]);
    puDst->ai16[5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc->ai16[2] + puSrc->ai16[3]);
    puDst->ai16[6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc->ai16[4] + puSrc->ai16[5]);
    puDst->ai16[7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc->ai16[6] + puSrc->ai16[7]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vphaddsw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[0] + puSrc1->ai16[1]);
    uDst.ai16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[2] + puSrc1->ai16[3]);
    uDst.ai16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[4] + puSrc1->ai16[5]);
    uDst.ai16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[6] + puSrc1->ai16[7]);

    uDst.ai16[4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[0] + puSrc2->ai16[1]);
    uDst.ai16[5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[2] + puSrc2->ai16[3]);
    uDst.ai16[6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[4] + puSrc2->ai16[5]);
    uDst.ai16[7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[6] + puSrc2->ai16[7]);

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vphaddsw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai16[ 0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[ 0] + puSrc1->ai16[ 1]);
    uDst.ai16[ 1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[ 2] + puSrc1->ai16[ 3]);
    uDst.ai16[ 2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[ 4] + puSrc1->ai16[ 5]);
    uDst.ai16[ 3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[ 6] + puSrc1->ai16[ 7]);
    uDst.ai16[ 4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[ 0] + puSrc2->ai16[ 1]);
    uDst.ai16[ 5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[ 2] + puSrc2->ai16[ 3]);
    uDst.ai16[ 6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[ 4] + puSrc2->ai16[ 5]);
    uDst.ai16[ 7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[ 6] + puSrc2->ai16[ 7]);

    uDst.ai16[ 8] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[ 8] + puSrc1->ai16[ 9]);
    uDst.ai16[ 9] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[10] + puSrc1->ai16[11]);
    uDst.ai16[10] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[12] + puSrc1->ai16[13]);
    uDst.ai16[11] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[14] + puSrc1->ai16[15]);
    uDst.ai16[12] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[ 8] + puSrc2->ai16[ 9]);
    uDst.ai16[13] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[10] + puSrc2->ai16[11]);
    uDst.ai16[14] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[12] + puSrc2->ai16[13]);
    uDst.ai16[15] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[14] + puSrc2->ai16[15]);

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
    puDst->au64[2] = uDst.au64[2];
    puDst->au64[3] = uDst.au64[3];
}


/*
 * PHSUBSW / VPHSUBSW
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_phsubsw_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst = { 0 }; /* Shut up MSVC. */

    uDst.ai16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[0] - uSrc1.ai16[1]);
    uDst.ai16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[2] - uSrc1.ai16[3]);
    uDst.ai16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.ai16[0] - uSrc2.ai16[1]);
    uDst.ai16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc2.ai16[2] - uSrc2.ai16[3]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_phsubsw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[0] - uSrc1.ai16[1]);
    puDst->ai16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[2] - uSrc1.ai16[3]);
    puDst->ai16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[4] - uSrc1.ai16[5]);
    puDst->ai16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(uSrc1.ai16[6] - uSrc1.ai16[7]);

    puDst->ai16[4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc->ai16[0] - puSrc->ai16[1]);
    puDst->ai16[5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc->ai16[2] - puSrc->ai16[3]);
    puDst->ai16[6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc->ai16[4] - puSrc->ai16[5]);
    puDst->ai16[7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc->ai16[6] - puSrc->ai16[7]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vphsubsw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[0] - puSrc1->ai16[1]);
    uDst.ai16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[2] - puSrc1->ai16[3]);
    uDst.ai16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[4] - puSrc1->ai16[5]);
    uDst.ai16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[6] - puSrc1->ai16[7]);

    uDst.ai16[4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[0] - puSrc2->ai16[1]);
    uDst.ai16[5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[2] - puSrc2->ai16[3]);
    uDst.ai16[6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[4] - puSrc2->ai16[5]);
    uDst.ai16[7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[6] - puSrc2->ai16[7]);

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vphsubsw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai16[ 0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[ 0] - puSrc1->ai16[ 1]);
    uDst.ai16[ 1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[ 2] - puSrc1->ai16[ 3]);
    uDst.ai16[ 2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[ 4] - puSrc1->ai16[ 5]);
    uDst.ai16[ 3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[ 6] - puSrc1->ai16[ 7]);
    uDst.ai16[ 4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[ 0] - puSrc2->ai16[ 1]);
    uDst.ai16[ 5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[ 2] - puSrc2->ai16[ 3]);
    uDst.ai16[ 6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[ 4] - puSrc2->ai16[ 5]);
    uDst.ai16[ 7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[ 6] - puSrc2->ai16[ 7]);

    uDst.ai16[ 8] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[ 8] - puSrc1->ai16[ 9]);
    uDst.ai16[ 9] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[10] - puSrc1->ai16[11]);
    uDst.ai16[10] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[12] - puSrc1->ai16[13]);
    uDst.ai16[11] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc1->ai16[14] - puSrc1->ai16[15]);
    uDst.ai16[12] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[ 8] - puSrc2->ai16[ 9]);
    uDst.ai16[13] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[10] - puSrc2->ai16[11]);
    uDst.ai16[14] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[12] - puSrc2->ai16[13]);
    uDst.ai16[15] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD(puSrc2->ai16[14] - puSrc2->ai16[15]);

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
    puDst->au64[2] = uDst.au64[2];
    puDst->au64[3] = uDst.au64[3];
}


/*
 * PMADDUBSW / VPMADDUBSW
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_pmaddubsw_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst = { 0 }; /* Shut up MSVC. */

    uDst.ai16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)uSrc1.au8[0] * uSrc2.ai8[0] + (uint16_t)uSrc1.au8[1] * uSrc2.ai8[1]);
    uDst.ai16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)uSrc1.au8[2] * uSrc2.ai8[2] + (uint16_t)uSrc1.au8[3] * uSrc2.ai8[3]);
    uDst.ai16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)uSrc1.au8[4] * uSrc2.ai8[4] + (uint16_t)uSrc1.au8[5] * uSrc2.ai8[5]);
    uDst.ai16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)uSrc1.au8[6] * uSrc2.ai8[6] + (uint16_t)uSrc1.au8[7] * uSrc2.ai8[7]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pmaddubsw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)uSrc1.au8[ 0] * puSrc->ai8[ 0] + (uint16_t)uSrc1.au8[ 1] * puSrc->ai8[ 1]);
    puDst->ai16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)uSrc1.au8[ 2] * puSrc->ai8[ 2] + (uint16_t)uSrc1.au8[ 3] * puSrc->ai8[ 3]);
    puDst->ai16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)uSrc1.au8[ 4] * puSrc->ai8[ 4] + (uint16_t)uSrc1.au8[ 5] * puSrc->ai8[ 5]);
    puDst->ai16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)uSrc1.au8[ 6] * puSrc->ai8[ 6] + (uint16_t)uSrc1.au8[ 7] * puSrc->ai8[ 7]);
    puDst->ai16[4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)uSrc1.au8[ 8] * puSrc->ai8[ 8] + (uint16_t)uSrc1.au8[ 9] * puSrc->ai8[ 9]);
    puDst->ai16[5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)uSrc1.au8[10] * puSrc->ai8[10] + (uint16_t)uSrc1.au8[11] * puSrc->ai8[11]);
    puDst->ai16[6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)uSrc1.au8[12] * puSrc->ai8[12] + (uint16_t)uSrc1.au8[13] * puSrc->ai8[13]);
    puDst->ai16[7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)uSrc1.au8[14] * puSrc->ai8[14] + (uint16_t)uSrc1.au8[15] * puSrc->ai8[15]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaddubsw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai16[0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[ 0] * puSrc2->ai8[ 0] + (uint16_t)puSrc1->au8[ 1] * puSrc2->ai8[ 1]);
    uDst.ai16[1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[ 2] * puSrc2->ai8[ 2] + (uint16_t)puSrc1->au8[ 3] * puSrc2->ai8[ 3]);
    uDst.ai16[2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[ 4] * puSrc2->ai8[ 4] + (uint16_t)puSrc1->au8[ 5] * puSrc2->ai8[ 5]);
    uDst.ai16[3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[ 6] * puSrc2->ai8[ 6] + (uint16_t)puSrc1->au8[ 7] * puSrc2->ai8[ 7]);
    uDst.ai16[4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[ 8] * puSrc2->ai8[ 8] + (uint16_t)puSrc1->au8[ 9] * puSrc2->ai8[ 9]);
    uDst.ai16[5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[10] * puSrc2->ai8[10] + (uint16_t)puSrc1->au8[11] * puSrc2->ai8[11]);
    uDst.ai16[6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[12] * puSrc2->ai8[12] + (uint16_t)puSrc1->au8[13] * puSrc2->ai8[13]);
    uDst.ai16[7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[14] * puSrc2->ai8[14] + (uint16_t)puSrc1->au8[15] * puSrc2->ai8[15]);

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmaddubsw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai16[ 0] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[ 0] * puSrc2->ai8[ 0] + (uint16_t)puSrc1->au8[ 1] * puSrc2->ai8[ 1]);
    uDst.ai16[ 1] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[ 2] * puSrc2->ai8[ 2] + (uint16_t)puSrc1->au8[ 3] * puSrc2->ai8[ 3]);
    uDst.ai16[ 2] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[ 4] * puSrc2->ai8[ 4] + (uint16_t)puSrc1->au8[ 5] * puSrc2->ai8[ 5]);
    uDst.ai16[ 3] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[ 6] * puSrc2->ai8[ 6] + (uint16_t)puSrc1->au8[ 7] * puSrc2->ai8[ 7]);
    uDst.ai16[ 4] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[ 8] * puSrc2->ai8[ 8] + (uint16_t)puSrc1->au8[ 9] * puSrc2->ai8[ 9]);
    uDst.ai16[ 5] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[10] * puSrc2->ai8[10] + (uint16_t)puSrc1->au8[11] * puSrc2->ai8[11]);
    uDst.ai16[ 6] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[12] * puSrc2->ai8[12] + (uint16_t)puSrc1->au8[13] * puSrc2->ai8[13]);
    uDst.ai16[ 7] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[14] * puSrc2->ai8[14] + (uint16_t)puSrc1->au8[15] * puSrc2->ai8[15]);
    uDst.ai16[ 8] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[16] * puSrc2->ai8[16] + (uint16_t)puSrc1->au8[17] * puSrc2->ai8[17]);
    uDst.ai16[ 9] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[18] * puSrc2->ai8[18] + (uint16_t)puSrc1->au8[19] * puSrc2->ai8[19]);
    uDst.ai16[10] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[20] * puSrc2->ai8[20] + (uint16_t)puSrc1->au8[21] * puSrc2->ai8[21]);
    uDst.ai16[11] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[22] * puSrc2->ai8[22] + (uint16_t)puSrc1->au8[23] * puSrc2->ai8[23]);
    uDst.ai16[12] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[24] * puSrc2->ai8[24] + (uint16_t)puSrc1->au8[25] * puSrc2->ai8[25]);
    uDst.ai16[13] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[26] * puSrc2->ai8[26] + (uint16_t)puSrc1->au8[27] * puSrc2->ai8[27]);
    uDst.ai16[14] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[28] * puSrc2->ai8[28] + (uint16_t)puSrc1->au8[29] * puSrc2->ai8[29]);
    uDst.ai16[15] = SATURATED_SIGNED_DWORD_TO_SIGNED_WORD((uint16_t)puSrc1->au8[30] * puSrc2->ai8[30] + (uint16_t)puSrc1->au8[31] * puSrc2->ai8[31]);

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
    puDst->au64[2] = uDst.au64[2];
    puDst->au64[3] = uDst.au64[3];
}


/*
 * PMULHRSW / VPMULHRSW
 */
#define DO_PMULHRSW(a_Src1, a_Src2) \
    (uint16_t)(((((int32_t)(a_Src1) * (a_Src2)) >> 14 ) + 1) >> 1)

IEM_DECL_IMPL_DEF(void, iemAImpl_pmulhrsw_u64_fallback,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;

    uDst.au16[0] = DO_PMULHRSW(uSrc1.ai16[0], uSrc2.ai16[0]);
    uDst.au16[1] = DO_PMULHRSW(uSrc1.ai16[1], uSrc2.ai16[1]);
    uDst.au16[2] = DO_PMULHRSW(uSrc1.ai16[2], uSrc2.ai16[2]);
    uDst.au16[3] = DO_PMULHRSW(uSrc1.ai16[3], uSrc2.ai16[3]);
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pmulhrsw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->ai16[0] = DO_PMULHRSW(uSrc1.ai16[0], puSrc->ai16[0]);
    puDst->ai16[1] = DO_PMULHRSW(uSrc1.ai16[1], puSrc->ai16[1]);
    puDst->ai16[2] = DO_PMULHRSW(uSrc1.ai16[2], puSrc->ai16[2]);
    puDst->ai16[3] = DO_PMULHRSW(uSrc1.ai16[3], puSrc->ai16[3]);
    puDst->ai16[4] = DO_PMULHRSW(uSrc1.ai16[4], puSrc->ai16[4]);
    puDst->ai16[5] = DO_PMULHRSW(uSrc1.ai16[5], puSrc->ai16[5]);
    puDst->ai16[6] = DO_PMULHRSW(uSrc1.ai16[6], puSrc->ai16[6]);
    puDst->ai16[7] = DO_PMULHRSW(uSrc1.ai16[7], puSrc->ai16[7]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmulhrsw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai16[0] = DO_PMULHRSW(puSrc1->ai16[0], puSrc2->ai16[0]);
    uDst.ai16[1] = DO_PMULHRSW(puSrc1->ai16[1], puSrc2->ai16[1]);
    uDst.ai16[2] = DO_PMULHRSW(puSrc1->ai16[2], puSrc2->ai16[2]);
    uDst.ai16[3] = DO_PMULHRSW(puSrc1->ai16[3], puSrc2->ai16[3]);
    uDst.ai16[4] = DO_PMULHRSW(puSrc1->ai16[4], puSrc2->ai16[4]);
    uDst.ai16[5] = DO_PMULHRSW(puSrc1->ai16[5], puSrc2->ai16[5]);
    uDst.ai16[6] = DO_PMULHRSW(puSrc1->ai16[6], puSrc2->ai16[6]);
    uDst.ai16[7] = DO_PMULHRSW(puSrc1->ai16[7], puSrc2->ai16[7]);

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmulhrsw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uDst; /* puDst can be the same as one of the source operands. */

    uDst.ai16[ 0] = DO_PMULHRSW(puSrc1->ai16[ 0], puSrc2->ai16[ 0]);
    uDst.ai16[ 1] = DO_PMULHRSW(puSrc1->ai16[ 1], puSrc2->ai16[ 1]);
    uDst.ai16[ 2] = DO_PMULHRSW(puSrc1->ai16[ 2], puSrc2->ai16[ 2]);
    uDst.ai16[ 3] = DO_PMULHRSW(puSrc1->ai16[ 3], puSrc2->ai16[ 3]);
    uDst.ai16[ 4] = DO_PMULHRSW(puSrc1->ai16[ 4], puSrc2->ai16[ 4]);
    uDst.ai16[ 5] = DO_PMULHRSW(puSrc1->ai16[ 5], puSrc2->ai16[ 5]);
    uDst.ai16[ 6] = DO_PMULHRSW(puSrc1->ai16[ 6], puSrc2->ai16[ 6]);
    uDst.ai16[ 7] = DO_PMULHRSW(puSrc1->ai16[ 7], puSrc2->ai16[ 7]);
    uDst.ai16[ 8] = DO_PMULHRSW(puSrc1->ai16[ 8], puSrc2->ai16[ 8]);
    uDst.ai16[ 9] = DO_PMULHRSW(puSrc1->ai16[ 9], puSrc2->ai16[ 9]);
    uDst.ai16[10] = DO_PMULHRSW(puSrc1->ai16[10], puSrc2->ai16[10]);
    uDst.ai16[11] = DO_PMULHRSW(puSrc1->ai16[11], puSrc2->ai16[11]);
    uDst.ai16[12] = DO_PMULHRSW(puSrc1->ai16[12], puSrc2->ai16[12]);
    uDst.ai16[13] = DO_PMULHRSW(puSrc1->ai16[13], puSrc2->ai16[13]);
    uDst.ai16[14] = DO_PMULHRSW(puSrc1->ai16[14], puSrc2->ai16[14]);
    uDst.ai16[15] = DO_PMULHRSW(puSrc1->ai16[15], puSrc2->ai16[15]);

    puDst->au64[0] = uDst.au64[0];
    puDst->au64[1] = uDst.au64[1];
    puDst->au64[2] = uDst.au64[2];
    puDst->au64[3] = uDst.au64[3];
}


/*
 * PSADBW / VPSADBW
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_psadbw_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    RTUINT64U uDst;
    uint16_t uSum = RT_ABS((int16_t)uSrc1.au8[0] - uSrc2.au8[0]);
    uSum += RT_ABS((int16_t)uSrc1.au8[1] - uSrc2.au8[1]);
    uSum += RT_ABS((int16_t)uSrc1.au8[2] - uSrc2.au8[2]);
    uSum += RT_ABS((int16_t)uSrc1.au8[3] - uSrc2.au8[3]);
    uSum += RT_ABS((int16_t)uSrc1.au8[4] - uSrc2.au8[4]);
    uSum += RT_ABS((int16_t)uSrc1.au8[5] - uSrc2.au8[5]);
    uSum += RT_ABS((int16_t)uSrc1.au8[6] - uSrc2.au8[6]);
    uSum += RT_ABS((int16_t)uSrc1.au8[7] - uSrc2.au8[7]);

    uDst.au64[0] = 0;
    uDst.au16[0] = uSum;
    *puDst = uDst.u;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_psadbw_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->au64[0] = 0;
    puDst->au64[1] = 0;

    uint16_t uSum = RT_ABS((int16_t)uSrc1.ai8[0] - puSrc->ai8[0]);
    uSum += RT_ABS((int16_t)uSrc1.au8[1] - puSrc->au8[1]);
    uSum += RT_ABS((int16_t)uSrc1.au8[2] - puSrc->au8[2]);
    uSum += RT_ABS((int16_t)uSrc1.au8[3] - puSrc->au8[3]);
    uSum += RT_ABS((int16_t)uSrc1.au8[4] - puSrc->au8[4]);
    uSum += RT_ABS((int16_t)uSrc1.au8[5] - puSrc->au8[5]);
    uSum += RT_ABS((int16_t)uSrc1.au8[6] - puSrc->au8[6]);
    uSum += RT_ABS((int16_t)uSrc1.au8[7] - puSrc->au8[7]);
    puDst->au16[0] = uSum;

    uSum  = RT_ABS((int16_t)uSrc1.au8[ 8] - puSrc->au8[ 8]);
    uSum += RT_ABS((int16_t)uSrc1.au8[ 9] - puSrc->au8[ 9]);
    uSum += RT_ABS((int16_t)uSrc1.au8[10] - puSrc->au8[10]);
    uSum += RT_ABS((int16_t)uSrc1.au8[11] - puSrc->au8[11]);
    uSum += RT_ABS((int16_t)uSrc1.au8[12] - puSrc->au8[12]);
    uSum += RT_ABS((int16_t)uSrc1.au8[13] - puSrc->au8[13]);
    uSum += RT_ABS((int16_t)uSrc1.au8[14] - puSrc->au8[14]);
    uSum += RT_ABS((int16_t)uSrc1.au8[15] - puSrc->au8[15]);
    puDst->au16[4] = uSum;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsadbw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uSrc1 = *puSrc1;
    RTUINT128U uSrc2 = *puSrc2;

    puDst->au64[0] = 0;
    puDst->au64[1] = 0;

    uint16_t uSum = RT_ABS((int16_t)uSrc1.ai8[0] - uSrc2.ai8[0]);
    uSum += RT_ABS((int16_t)uSrc1.au8[1] - uSrc2.au8[1]);
    uSum += RT_ABS((int16_t)uSrc1.au8[2] - uSrc2.au8[2]);
    uSum += RT_ABS((int16_t)uSrc1.au8[3] - uSrc2.au8[3]);
    uSum += RT_ABS((int16_t)uSrc1.au8[4] - uSrc2.au8[4]);
    uSum += RT_ABS((int16_t)uSrc1.au8[5] - uSrc2.au8[5]);
    uSum += RT_ABS((int16_t)uSrc1.au8[6] - uSrc2.au8[6]);
    uSum += RT_ABS((int16_t)uSrc1.au8[7] - uSrc2.au8[7]);
    puDst->au16[0] = uSum;

    uSum  = RT_ABS((int16_t)uSrc1.au8[ 8] - uSrc2.au8[ 8]);
    uSum += RT_ABS((int16_t)uSrc1.au8[ 9] - uSrc2.au8[ 9]);
    uSum += RT_ABS((int16_t)uSrc1.au8[10] - uSrc2.au8[10]);
    uSum += RT_ABS((int16_t)uSrc1.au8[11] - uSrc2.au8[11]);
    uSum += RT_ABS((int16_t)uSrc1.au8[12] - uSrc2.au8[12]);
    uSum += RT_ABS((int16_t)uSrc1.au8[13] - uSrc2.au8[13]);
    uSum += RT_ABS((int16_t)uSrc1.au8[14] - uSrc2.au8[14]);
    uSum += RT_ABS((int16_t)uSrc1.au8[15] - uSrc2.au8[15]);
    puDst->au16[4] = uSum;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpsadbw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uSrc1 = *puSrc1;
    RTUINT256U uSrc2 = *puSrc2;

    puDst->au64[0] = 0;
    puDst->au64[1] = 0;
    puDst->au64[2] = 0;
    puDst->au64[3] = 0;

    uint16_t uSum = RT_ABS((int16_t)uSrc1.au8[0] - uSrc2.au8[0]);
    uSum += RT_ABS((int16_t)uSrc1.au8[1] - uSrc2.au8[1]);
    uSum += RT_ABS((int16_t)uSrc1.au8[2] - uSrc2.au8[2]);
    uSum += RT_ABS((int16_t)uSrc1.au8[3] - uSrc2.au8[3]);
    uSum += RT_ABS((int16_t)uSrc1.au8[4] - uSrc2.au8[4]);
    uSum += RT_ABS((int16_t)uSrc1.au8[5] - uSrc2.au8[5]);
    uSum += RT_ABS((int16_t)uSrc1.au8[6] - uSrc2.au8[6]);
    uSum += RT_ABS((int16_t)uSrc1.au8[7] - uSrc2.au8[7]);
    puDst->au16[0] = uSum;

    uSum  = RT_ABS((int16_t)uSrc1.au8[ 8] - uSrc2.au8[ 8]);
    uSum += RT_ABS((int16_t)uSrc1.au8[ 9] - uSrc2.au8[ 9]);
    uSum += RT_ABS((int16_t)uSrc1.au8[10] - uSrc2.au8[10]);
    uSum += RT_ABS((int16_t)uSrc1.au8[11] - uSrc2.au8[11]);
    uSum += RT_ABS((int16_t)uSrc1.au8[12] - uSrc2.au8[12]);
    uSum += RT_ABS((int16_t)uSrc1.au8[13] - uSrc2.au8[13]);
    uSum += RT_ABS((int16_t)uSrc1.au8[14] - uSrc2.au8[14]);
    uSum += RT_ABS((int16_t)uSrc1.au8[15] - uSrc2.au8[15]);
    puDst->au16[4] = uSum;

    uSum  = RT_ABS((int16_t)uSrc1.au8[16] - uSrc2.au8[16]);
    uSum += RT_ABS((int16_t)uSrc1.au8[17] - uSrc2.au8[17]);
    uSum += RT_ABS((int16_t)uSrc1.au8[18] - uSrc2.au8[18]);
    uSum += RT_ABS((int16_t)uSrc1.au8[19] - uSrc2.au8[19]);
    uSum += RT_ABS((int16_t)uSrc1.au8[20] - uSrc2.au8[20]);
    uSum += RT_ABS((int16_t)uSrc1.au8[21] - uSrc2.au8[21]);
    uSum += RT_ABS((int16_t)uSrc1.au8[22] - uSrc2.au8[22]);
    uSum += RT_ABS((int16_t)uSrc1.au8[23] - uSrc2.au8[23]);
    puDst->au16[8] = uSum;

    uSum  = RT_ABS((int16_t)uSrc1.au8[24] - uSrc2.au8[24]);
    uSum += RT_ABS((int16_t)uSrc1.au8[25] - uSrc2.au8[25]);
    uSum += RT_ABS((int16_t)uSrc1.au8[26] - uSrc2.au8[26]);
    uSum += RT_ABS((int16_t)uSrc1.au8[27] - uSrc2.au8[27]);
    uSum += RT_ABS((int16_t)uSrc1.au8[28] - uSrc2.au8[28]);
    uSum += RT_ABS((int16_t)uSrc1.au8[29] - uSrc2.au8[29]);
    uSum += RT_ABS((int16_t)uSrc1.au8[30] - uSrc2.au8[30]);
    uSum += RT_ABS((int16_t)uSrc1.au8[31] - uSrc2.au8[31]);
    puDst->au16[12] = uSum;
}


/*
 * PMULDQ / VPMULDQ
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_pmuldq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;

    puDst->au64[0] = (int64_t)uSrc1.ai32[0] * puSrc->ai32[0];
    puDst->au64[1] = (int64_t)uSrc1.ai32[2] * puSrc->ai32[2];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpmuldq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uSrc1 = *puSrc1;
    RTUINT128U uSrc2 = *puSrc2;

    puDst->au64[0] = (int64_t)uSrc1.ai32[0] * uSrc2.ai32[0];
    puDst->au64[1] = (int64_t)uSrc1.ai32[2] * uSrc2.ai32[2];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpmuldq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uSrc1 = *puSrc1;
    RTUINT256U uSrc2 = *puSrc2;

    puDst->au64[0] = (int64_t)uSrc1.ai32[0] * uSrc2.ai32[0];
    puDst->au64[1] = (int64_t)uSrc1.ai32[2] * uSrc2.ai32[2];
    puDst->au64[2] = (int64_t)uSrc1.ai32[4] * uSrc2.ai32[4];
    puDst->au64[3] = (int64_t)uSrc1.ai32[6] * uSrc2.ai32[6];
}


/*
 * PMULUDQ / VPMULUDQ
 */
#ifdef IEM_WITHOUT_ASSEMBLY

IEM_DECL_IMPL_DEF(void, iemAImpl_pmuludq_u64,(uint64_t *puDst, uint64_t const *puSrc))
{
    RTUINT64U uSrc1 = { *puDst };
    RTUINT64U uSrc2 = { *puSrc };
    ASMCompilerBarrier();
    *puDst = (uint64_t)uSrc1.au32[0] * uSrc2.au32[0];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_pmuludq_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    RTUINT128U uSrc2 = *puSrc;
    ASMCompilerBarrier();
    puDst->au64[0] = (uint64_t)uSrc1.au32[0] * uSrc2.au32[0];
    puDst->au64[1] = (uint64_t)uSrc1.au32[2] * uSrc2.au32[2];
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vpmuludq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uSrc1 = *puSrc1; /* Could overlap with puDst */
    RTUINT128U uSrc2 = *puSrc2; /* Could overlap with puDst */
    ASMCompilerBarrier();
    puDst->au64[0] = (uint64_t)uSrc1.au32[0] * uSrc2.au32[0];
    puDst->au64[1] = (uint64_t)uSrc1.au32[2] * uSrc2.au32[2];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmuludq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uSrc1 = *puSrc1; /* Could overlap with puDst */
    RTUINT256U uSrc2 = *puSrc2; /* Could overlap with puDst */
    ASMCompilerBarrier();
    puDst->au64[0] = (uint64_t)uSrc1.au32[0] * uSrc2.au32[0];
    puDst->au64[1] = (uint64_t)uSrc1.au32[2] * uSrc2.au32[2];
    puDst->au64[2] = (uint64_t)uSrc1.au32[4] * uSrc2.au32[4];
    puDst->au64[3] = (uint64_t)uSrc1.au32[6] * uSrc2.au32[6];
}


/*
 * UNPCKLPS / VUNPCKLPS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_unpcklps_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    RTUINT128U uSrc2 = *puSrc;
    ASMCompilerBarrier();
    puDst->au32[0] = uSrc1.au32[0];
    puDst->au32[1] = uSrc2.au32[0];
    puDst->au32[2] = uSrc1.au32[1];
    puDst->au32[3] = uSrc2.au32[1];
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vunpcklps_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uSrc1 = *puSrc1; /* Could overlap with puDst */
    RTUINT128U uSrc2 = *puSrc2; /* Could overlap with puDst */
    ASMCompilerBarrier();
    puDst->au32[0] = uSrc1.au32[0];
    puDst->au32[1] = uSrc2.au32[0];
    puDst->au32[2] = uSrc1.au32[1];
    puDst->au32[3] = uSrc2.au32[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vunpcklps_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uSrc1 = *puSrc1; /* Could overlap with puDst */
    RTUINT256U uSrc2 = *puSrc2; /* Could overlap with puDst */
    ASMCompilerBarrier();
    puDst->au32[0] = uSrc1.au32[0];
    puDst->au32[1] = uSrc2.au32[0];
    puDst->au32[2] = uSrc1.au32[1];
    puDst->au32[3] = uSrc2.au32[1];

    puDst->au32[4] = uSrc1.au32[4];
    puDst->au32[5] = uSrc2.au32[4];
    puDst->au32[6] = uSrc1.au32[5];
    puDst->au32[7] = uSrc2.au32[5];
}


/*
 * UNPCKLPD / VUNPCKLPD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_unpcklpd_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    RTUINT128U uSrc2 = *puSrc;
    ASMCompilerBarrier();
    puDst->au64[0] = uSrc1.au64[0];
    puDst->au64[1] = uSrc2.au64[0];
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vunpcklpd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uSrc1 = *puSrc1; /* Could overlap with puDst */
    RTUINT128U uSrc2 = *puSrc2; /* Could overlap with puDst */
    ASMCompilerBarrier();
    puDst->au64[0] = uSrc1.au64[0];
    puDst->au64[1] = uSrc2.au64[0];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vunpcklpd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uSrc1 = *puSrc1; /* Could overlap with puDst */
    RTUINT256U uSrc2 = *puSrc2; /* Could overlap with puDst */
    ASMCompilerBarrier();
    puDst->au64[0] = uSrc1.au64[0];
    puDst->au64[1] = uSrc2.au64[0];
    puDst->au64[2] = uSrc1.au64[2];
    puDst->au64[3] = uSrc2.au64[2];
}


/*
 * UNPCKHPS / VUNPCKHPS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_unpckhps_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    RTUINT128U uSrc2 = *puSrc;
    ASMCompilerBarrier();
    puDst->au32[0] = uSrc1.au32[2];
    puDst->au32[1] = uSrc2.au32[2];
    puDst->au32[2] = uSrc1.au32[3];
    puDst->au32[3] = uSrc2.au32[3];
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vunpckhps_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uSrc1 = *puSrc1; /* Could overlap with puDst */
    RTUINT128U uSrc2 = *puSrc2; /* Could overlap with puDst */
    ASMCompilerBarrier();
    puDst->au32[0] = uSrc1.au32[2];
    puDst->au32[1] = uSrc2.au32[2];
    puDst->au32[2] = uSrc1.au32[3];
    puDst->au32[3] = uSrc2.au32[3];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vunpckhps_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uSrc1 = *puSrc1; /* Could overlap with puDst */
    RTUINT256U uSrc2 = *puSrc2; /* Could overlap with puDst */
    ASMCompilerBarrier();
    puDst->au32[0] = uSrc1.au32[2];
    puDst->au32[1] = uSrc2.au32[2];
    puDst->au32[2] = uSrc1.au32[3];
    puDst->au32[3] = uSrc2.au32[3];

    puDst->au32[4] = uSrc1.au32[6];
    puDst->au32[5] = uSrc2.au32[6];
    puDst->au32[6] = uSrc1.au32[7];
    puDst->au32[7] = uSrc2.au32[7];
}


/*
 * UNPCKHPD / VUNPCKHPD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_unpckhpd_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puDst;
    RTUINT128U uSrc2 = *puSrc;
    ASMCompilerBarrier();
    puDst->au64[0] = uSrc1.au64[1];
    puDst->au64[1] = uSrc2.au64[1];
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vunpckhpd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U uSrc1 = *puSrc1; /* Could overlap with puDst */
    RTUINT128U uSrc2 = *puSrc2; /* Could overlap with puDst */
    ASMCompilerBarrier();
    puDst->au64[0] = uSrc1.au64[1];
    puDst->au64[1] = uSrc2.au64[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vunpckhpd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U uSrc1 = *puSrc1; /* Could overlap with puDst */
    RTUINT256U uSrc2 = *puSrc2; /* Could overlap with puDst */
    ASMCompilerBarrier();
    puDst->au64[0] = uSrc1.au64[1];
    puDst->au64[1] = uSrc2.au64[1];
    puDst->au64[2] = uSrc1.au64[3];
    puDst->au64[3] = uSrc2.au64[3];
}


/*
 * CRC32 (SEE 4.2).
 */

IEM_DECL_IMPL_DEF(void, iemAImpl_crc32_u8_fallback,(uint32_t *puDst, uint8_t uSrc))
{
    *puDst = RTCrc32CProcess(*puDst, &uSrc, sizeof(uSrc));
}


IEM_DECL_IMPL_DEF(void, iemAImpl_crc32_u16_fallback,(uint32_t *puDst, uint16_t uSrc))
{
    *puDst = RTCrc32CProcess(*puDst, &uSrc, sizeof(uSrc));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_crc32_u32_fallback,(uint32_t *puDst, uint32_t uSrc))
{
    *puDst = RTCrc32CProcess(*puDst, &uSrc, sizeof(uSrc));
}

IEM_DECL_IMPL_DEF(void, iemAImpl_crc32_u64_fallback,(uint32_t *puDst, uint64_t uSrc))
{
    *puDst = RTCrc32CProcess(*puDst, &uSrc, sizeof(uSrc));
}


/*
 * PTEST (SSE 4.1) - special as it output only EFLAGS.
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_ptest_u128,(PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint32_t *pfEFlags))
{
    uint32_t fEfl = *pfEFlags & ~X86_EFL_STATUS_BITS;
    if (   (puSrc1->au64[0] & puSrc2->au64[0]) == 0
        && (puSrc1->au64[1] & puSrc2->au64[1]) == 0)
        fEfl |= X86_EFL_ZF;
    if (   (~puSrc1->au64[0] & puSrc2->au64[0]) == 0
        && (~puSrc1->au64[1] & puSrc2->au64[1]) == 0)
        fEfl |= X86_EFL_CF;
    *pfEFlags = fEfl;
}
#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vptest_u256_fallback,(PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint32_t *pfEFlags))
{
    uint32_t fEfl = *pfEFlags & ~X86_EFL_STATUS_BITS;
    if (   (puSrc1->au64[0] & puSrc2->au64[0]) == 0
        && (puSrc1->au64[1] & puSrc2->au64[1]) == 0
        && (puSrc1->au64[2] & puSrc2->au64[2]) == 0
        && (puSrc1->au64[3] & puSrc2->au64[3]) == 0)
        fEfl |= X86_EFL_ZF;
    if (   (~puSrc1->au64[0] & puSrc2->au64[0]) == 0
        && (~puSrc1->au64[1] & puSrc2->au64[1]) == 0
        && (~puSrc1->au64[2] & puSrc2->au64[2]) == 0
        && (~puSrc1->au64[3] & puSrc2->au64[3]) == 0)
        fEfl |= X86_EFL_CF;
    *pfEFlags = fEfl;
}


/* Worker for VEX.128 vtestp[s|d]. */
static void iemAImpl_vtestp_sd_u128_worker(PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint64_t fSignMask, uint32_t *pfEFlags)
{
    uint32_t fEfl = *pfEFlags & ~X86_EFL_STATUS_BITS;
    RTUINT128U uTemp;
    uTemp.au64[0] = puSrc1->au64[0] & puSrc2->au64[0];
    uTemp.au64[1] = puSrc1->au64[1] & puSrc2->au64[1];
    if (((  uTemp.au64[0]
          | uTemp.au64[1]) & fSignMask) == 0)
        fEfl |= X86_EFL_ZF;
    uTemp.au64[0] = ~puSrc1->au64[0] & puSrc2->au64[0];
    uTemp.au64[1] = ~puSrc1->au64[1] & puSrc2->au64[1];
    if (((  uTemp.au64[0]
          | uTemp.au64[1]) & fSignMask) == 0)
        fEfl |= X86_EFL_CF;
    *pfEFlags = fEfl;
}


/* Worker for VEX.256 vtestp[s|d]. */
static void iemAImpl_vtestp_sd_u256_worker(PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint64_t fSignMask, uint32_t *pfEFlags)
{
    uint32_t fEfl = *pfEFlags & ~X86_EFL_STATUS_BITS;
    RTUINT256U uTemp;
    uTemp.au64[0] = puSrc1->au64[0] & puSrc2->au64[0];
    uTemp.au64[1] = puSrc1->au64[1] & puSrc2->au64[1];
    uTemp.au64[2] = puSrc1->au64[2] & puSrc2->au64[2];
    uTemp.au64[3] = puSrc1->au64[3] & puSrc2->au64[3];
    if (((  uTemp.au64[0]
          | uTemp.au64[1]
          | uTemp.au64[2]
          | uTemp.au64[3]) & fSignMask) == 0)
        fEfl |= X86_EFL_ZF;
    uTemp.au64[0] = ~puSrc1->au64[0] & puSrc2->au64[0];
    uTemp.au64[1] = ~puSrc1->au64[1] & puSrc2->au64[1];
    uTemp.au64[2] = ~puSrc1->au64[2] & puSrc2->au64[2];
    uTemp.au64[3] = ~puSrc1->au64[3] & puSrc2->au64[3];
    if (((  uTemp.au64[0]
          | uTemp.au64[1]
          | uTemp.au64[2]
          | uTemp.au64[3]) & fSignMask) == 0)
        fEfl |= X86_EFL_CF;
    *pfEFlags = fEfl;
}


/*
 * VTESTPS
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vtestps_u128_fallback,(PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint32_t *pfEFlags))
{
    uint64_t const fSignMask = RT_BIT_64(63) | RT_BIT_64(31);
    return iemAImpl_vtestp_sd_u128_worker(puSrc1, puSrc2, fSignMask, pfEFlags);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vtestps_u256_fallback,(PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint32_t *pfEFlags))
{
    uint64_t const fSignMask = RT_BIT_64(63) | RT_BIT_64(31);
    return iemAImpl_vtestp_sd_u256_worker(puSrc1, puSrc2, fSignMask, pfEFlags);
}


/*
 * VTESTPD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vtestpd_u128_fallback,(PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint32_t *pfEFlags))
{
    uint64_t const fSignMask = RT_BIT_64(63);
    return iemAImpl_vtestp_sd_u128_worker(puSrc1, puSrc2, fSignMask, pfEFlags);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vtestpd_u256_fallback,(PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint32_t *pfEFlags))
{
    uint64_t const fSignMask = RT_BIT_64(63);
    return iemAImpl_vtestp_sd_u256_worker(puSrc1, puSrc2, fSignMask, pfEFlags);
}


/*
 * PMOVSXBW / VPMOVSXBW
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbw_u128_fallback,(PRTUINT128U puDst, uint64_t uSrc))
{
    RTUINT64U uSrc1 = { uSrc };
    puDst->ai16[0] = uSrc1.ai8[0];
    puDst->ai16[1] = uSrc1.ai8[1];
    puDst->ai16[2] = uSrc1.ai8[2];
    puDst->ai16[3] = uSrc1.ai8[3];
    puDst->ai16[4] = uSrc1.ai8[4];
    puDst->ai16[5] = uSrc1.ai8[5];
    puDst->ai16[6] = uSrc1.ai8[6];
    puDst->ai16[7] = uSrc1.ai8[7];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbw_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puSrc; /* puDst could overlap */
    puDst->ai16[ 0] = uSrc1.ai8[ 0];
    puDst->ai16[ 1] = uSrc1.ai8[ 1];
    puDst->ai16[ 2] = uSrc1.ai8[ 2];
    puDst->ai16[ 3] = uSrc1.ai8[ 3];
    puDst->ai16[ 4] = uSrc1.ai8[ 4];
    puDst->ai16[ 5] = uSrc1.ai8[ 5];
    puDst->ai16[ 6] = uSrc1.ai8[ 6];
    puDst->ai16[ 7] = uSrc1.ai8[ 7];
    puDst->ai16[ 8] = uSrc1.ai8[ 8];
    puDst->ai16[ 9] = uSrc1.ai8[ 9];
    puDst->ai16[10] = uSrc1.ai8[10];
    puDst->ai16[11] = uSrc1.ai8[11];
    puDst->ai16[12] = uSrc1.ai8[12];
    puDst->ai16[13] = uSrc1.ai8[13];
    puDst->ai16[14] = uSrc1.ai8[14];
    puDst->ai16[15] = uSrc1.ai8[15];
}


/*
 * PMOVSXBD / VPMOVSXBD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbd_u128_fallback,(PRTUINT128U puDst, uint32_t uSrc))
{
    RTUINT32U uSrc1 = { uSrc };
    puDst->ai32[0] = uSrc1.ai8[0];
    puDst->ai32[1] = uSrc1.ai8[1];
    puDst->ai32[2] = uSrc1.ai8[2];
    puDst->ai32[3] = uSrc1.ai8[3];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbd_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puSrc; /* puDst could overlap */
    puDst->ai32[0] = uSrc1.ai8[0];
    puDst->ai32[1] = uSrc1.ai8[1];
    puDst->ai32[2] = uSrc1.ai8[2];
    puDst->ai32[3] = uSrc1.ai8[3];
    puDst->ai32[4] = uSrc1.ai8[4];
    puDst->ai32[5] = uSrc1.ai8[5];
    puDst->ai32[6] = uSrc1.ai8[6];
    puDst->ai32[7] = uSrc1.ai8[7];
}


/*
 * PMOVSXBQ / VPMOVSXBQ
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbq_u128_fallback,(PRTUINT128U puDst, uint16_t uSrc))
{
    RTUINT16U uSrc1 = { uSrc };
    puDst->ai64[0] = uSrc1.ai8[0];
    puDst->ai64[1] = uSrc1.ai8[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxbq_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puSrc; /* puDst could overlap */
    puDst->ai64[0] = uSrc1.ai8[0];
    puDst->ai64[1] = uSrc1.ai8[1];
    puDst->ai64[2] = uSrc1.ai8[2];
    puDst->ai64[3] = uSrc1.ai8[3];
}


/*
 * PMOVSXWD / VPMOVSXWD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxwd_u128_fallback,(PRTUINT128U puDst, uint64_t uSrc))
{
    RTUINT64U uSrc1 = { uSrc };
    puDst->ai32[0] = uSrc1.ai16[0];
    puDst->ai32[1] = uSrc1.ai16[1];
    puDst->ai32[2] = uSrc1.ai16[2];
    puDst->ai32[3] = uSrc1.ai16[3];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxwd_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puSrc; /* puDst could overlap */
    puDst->ai32[0] = uSrc1.ai16[0];
    puDst->ai32[1] = uSrc1.ai16[1];
    puDst->ai32[2] = uSrc1.ai16[2];
    puDst->ai32[3] = uSrc1.ai16[3];
    puDst->ai32[4] = uSrc1.ai16[4];
    puDst->ai32[5] = uSrc1.ai16[5];
    puDst->ai32[6] = uSrc1.ai16[6];
    puDst->ai32[7] = uSrc1.ai16[7];
}


/*
 * PMOVSXWQ / VPMOVSXWQ
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxwq_u128_fallback,(PRTUINT128U puDst, uint32_t uSrc))
{
    RTUINT32U uSrc1 = { uSrc };
    puDst->ai64[0] = uSrc1.ai16[0];
    puDst->ai64[1] = uSrc1.ai16[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxwq_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puSrc; /* puDst could overlap */
    puDst->ai64[0] = uSrc1.ai16[0];
    puDst->ai64[1] = uSrc1.ai16[1];
    puDst->ai64[2] = uSrc1.ai16[2];
    puDst->ai64[3] = uSrc1.ai16[3];
}


/*
 * PMOVSXDQ / VPMOVSXDQ
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxdq_u128_fallback,(PRTUINT128U puDst, uint64_t uSrc))
{
    RTUINT64U uSrc1 = { uSrc };
    puDst->ai64[0] = uSrc1.ai32[0];
    puDst->ai64[1] = uSrc1.ai32[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovsxdq_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puSrc; /* puDst could overlap */
    puDst->ai64[0] = uSrc1.ai32[0];
    puDst->ai64[1] = uSrc1.ai32[1];
    puDst->ai64[2] = uSrc1.ai32[2];
    puDst->ai64[3] = uSrc1.ai32[3];
}


/*
 * PMOVZXBW / VPMOVZXBW
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbw_u128_fallback,(PRTUINT128U puDst, uint64_t uSrc))
{
    RTUINT64U uSrc1 = { uSrc };
    puDst->au16[0] = uSrc1.au8[0];
    puDst->au16[1] = uSrc1.au8[1];
    puDst->au16[2] = uSrc1.au8[2];
    puDst->au16[3] = uSrc1.au8[3];
    puDst->au16[4] = uSrc1.au8[4];
    puDst->au16[5] = uSrc1.au8[5];
    puDst->au16[6] = uSrc1.au8[6];
    puDst->au16[7] = uSrc1.au8[7];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbw_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puSrc; /* puDst could overlap */
    puDst->au16[ 0] = uSrc1.au8[ 0];
    puDst->au16[ 1] = uSrc1.au8[ 1];
    puDst->au16[ 2] = uSrc1.au8[ 2];
    puDst->au16[ 3] = uSrc1.au8[ 3];
    puDst->au16[ 4] = uSrc1.au8[ 4];
    puDst->au16[ 5] = uSrc1.au8[ 5];
    puDst->au16[ 6] = uSrc1.au8[ 6];
    puDst->au16[ 7] = uSrc1.au8[ 7];
    puDst->au16[ 8] = uSrc1.au8[ 8];
    puDst->au16[ 9] = uSrc1.au8[ 9];
    puDst->au16[10] = uSrc1.au8[10];
    puDst->au16[11] = uSrc1.au8[11];
    puDst->au16[12] = uSrc1.au8[12];
    puDst->au16[13] = uSrc1.au8[13];
    puDst->au16[14] = uSrc1.au8[14];
    puDst->au16[15] = uSrc1.au8[15];
}


/*
 * PMOVZXBD / VPMOVZXBD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbd_u128_fallback,(PRTUINT128U puDst, uint32_t uSrc))
{
    RTUINT32U uSrc1 = { uSrc };
    puDst->au32[0] = uSrc1.au8[0];
    puDst->au32[1] = uSrc1.au8[1];
    puDst->au32[2] = uSrc1.au8[2];
    puDst->au32[3] = uSrc1.au8[3];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbd_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puSrc; /* puDst could overlap */
    puDst->au32[0] = uSrc1.au8[0];
    puDst->au32[1] = uSrc1.au8[1];
    puDst->au32[2] = uSrc1.au8[2];
    puDst->au32[3] = uSrc1.au8[3];
    puDst->au32[4] = uSrc1.au8[4];
    puDst->au32[5] = uSrc1.au8[5];
    puDst->au32[6] = uSrc1.au8[6];
    puDst->au32[7] = uSrc1.au8[7];
}


/*
 * PMOVZXBQ / VPMOVZXBQ
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbq_u128_fallback,(PRTUINT128U puDst, uint16_t uSrc))
{
    RTUINT16U uSrc1 = { uSrc };
    puDst->au64[0] = uSrc1.au8[0];
    puDst->au64[1] = uSrc1.au8[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxbq_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puSrc; /* puDst could overlap */
    puDst->au64[0] = uSrc1.au8[0];
    puDst->au64[1] = uSrc1.au8[1];
    puDst->au64[2] = uSrc1.au8[2];
    puDst->au64[3] = uSrc1.au8[3];
}


/*
 * PMOVZXWD / VPMOVZXWD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxwd_u128_fallback,(PRTUINT128U puDst, uint64_t uSrc))
{
    RTUINT64U uSrc1 = { uSrc };
    puDst->au32[0] = uSrc1.au16[0];
    puDst->au32[1] = uSrc1.au16[1];
    puDst->au32[2] = uSrc1.au16[2];
    puDst->au32[3] = uSrc1.au16[3];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxwd_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puSrc; /* puDst could overlap */
    puDst->au32[0] = uSrc1.au16[0];
    puDst->au32[1] = uSrc1.au16[1];
    puDst->au32[2] = uSrc1.au16[2];
    puDst->au32[3] = uSrc1.au16[3];
    puDst->au32[4] = uSrc1.au16[4];
    puDst->au32[5] = uSrc1.au16[5];
    puDst->au32[6] = uSrc1.au16[6];
    puDst->au32[7] = uSrc1.au16[7];
}


/*
 * PMOVZXWQ / VPMOVZXWQ
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxwq_u128_fallback,(PRTUINT128U puDst, uint32_t uSrc))
{
    RTUINT32U uSrc1 = { uSrc };
    puDst->au64[0] = uSrc1.au16[0];
    puDst->au64[1] = uSrc1.au16[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxwq_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puSrc; /* puDst could overlap */
    puDst->au64[0] = uSrc1.au16[0];
    puDst->au64[1] = uSrc1.au16[1];
    puDst->au64[2] = uSrc1.au16[2];
    puDst->au64[3] = uSrc1.au16[3];
}


/*
 * PMOVZXDQ / VPMOVZXDQ
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxdq_u128_fallback,(PRTUINT128U puDst, uint64_t uSrc))
{
    RTUINT64U uSrc1 = { uSrc };
    puDst->au64[0] = uSrc1.au32[0];
    puDst->au64[1] = uSrc1.au32[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpmovzxdq_u256_fallback,(PRTUINT256U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U uSrc1 = *puSrc; /* puDst could overlap */
    puDst->au64[0] = uSrc1.au32[0];
    puDst->au64[1] = uSrc1.au32[1];
    puDst->au64[2] = uSrc1.au32[2];
    puDst->au64[3] = uSrc1.au32[3];
}

/**
 * Converts from the packed IPRT 32-bit (single precision) floating point format to
 * the SoftFloat 32-bit floating point format (float32_t).
 *
 * This is only a structure format conversion, nothing else.
 */
DECLINLINE(float32_t) iemFpSoftF32FromIprt(PCRTFLOAT32U pr32Val)
{
    float32_t Tmp;
    Tmp.v = pr32Val->u;
    return Tmp;
}


/**
 * Converts from SoftFloat 32-bit floating point format (float32_t)
 * to the packed IPRT 32-bit floating point (RTFLOAT32U) format.
 *
 * This is only a structure format conversion, nothing else.
 */
DECLINLINE(PRTFLOAT32U) iemFpSoftF32ToIprt(PRTFLOAT32U pr32Dst, float32_t const r32XSrc)
{
    pr32Dst->u = r32XSrc.v;
    return pr32Dst;
}


/**
 * Converts from the packed IPRT 64-bit (single precision) floating point format to
 * the SoftFloat 64-bit floating point format (float64_t).
 *
 * This is only a structure format conversion, nothing else.
 */
DECLINLINE(float64_t) iemFpSoftF64FromIprt(PCRTFLOAT64U pr64Val)
{
    float64_t Tmp;
    Tmp.v = pr64Val->u;
    return Tmp;
}


/**
 * Converts from SoftFloat 64-bit floating point format (float64_t)
 * to the packed IPRT 64-bit floating point (RTFLOAT64U) format.
 *
 * This is only a structure format conversion, nothing else.
 */
DECLINLINE(PRTFLOAT64U) iemFpSoftF64ToIprt(PRTFLOAT64U pr64Dst, float64_t const r64XSrc)
{
    pr64Dst->u = r64XSrc.v;
    return pr64Dst;
}


/** Initializer for the SoftFloat state structure. */
# define IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(a_Mxcsr) \
    { \
        softfloat_tininess_afterRounding, \
          ((a_Mxcsr) & X86_MXCSR_RC_MASK) == X86_MXCSR_RC_NEAREST ? (uint8_t)softfloat_round_near_even \
        : ((a_Mxcsr) & X86_MXCSR_RC_MASK) == X86_MXCSR_RC_UP      ? (uint8_t)softfloat_round_max \
        : ((a_Mxcsr) & X86_MXCSR_RC_MASK) == X86_MXCSR_RC_DOWN    ? (uint8_t)softfloat_round_min \
        :                                                           (uint8_t)softfloat_round_minMag, \
        0, \
        (uint8_t)(((a_Mxcsr) & X86_MXCSR_XCPT_MASK) >> X86_MXCSR_XCPT_MASK_SHIFT), /* Matches X86_FSW_?E */\
        32 /* Rounding precision, not relevant for SIMD. */ \
    }


/**
 * Helper for transfering exception to MXCSR and setting the result value
 * accordingly.
 *
 * @returns Updated MXCSR.
 * @param   pSoftState      The SoftFloat state following the operation.
 * @param   r32Result       The result of the SoftFloat operation.
 * @param   pr32Result      Where to store the result for IEM.
 * @param   fMxcsr          The original MXCSR value.
 */
DECLINLINE(uint32_t) iemSseSoftStateAndR32ToMxcsrAndIprtResult(softfloat_state_t const *pSoftState, float32_t r32Result,
                                                               PRTFLOAT32U pr32Result, uint32_t fMxcsr)
{
    iemFpSoftF32ToIprt(pr32Result, r32Result);
    uint8_t fXcpt = pSoftState->exceptionFlags;
    /* If DAZ is set \#DE is never set. */
    if (   fMxcsr & X86_MXCSR_DAZ
        || (   (fXcpt & X86_MXCSR_DE) /* Softfloat sets DE for sub-normal values. */
            && (RTFLOAT32U_IS_SUBNORMAL(pr32Result))))
        fXcpt &= ~X86_MXCSR_DE;

    /*
     * Skip computing the post-computational exception flags if any of the pre-computational exception flags
     * are set and are unmasked in MXCSR.
     */
    uint32_t const fMxcsrExcpMask = ~((fMxcsr & X86_MXCSR_XCPT_MASK) >> X86_MXCSR_XCPT_MASK_SHIFT);
    if (  ((fMxcsr | fXcpt) & (X86_MXCSR_IE | X86_MXCSR_DE | X86_MXCSR_ZE))
        & fMxcsrExcpMask)
        return fMxcsr | (fXcpt & (X86_MXCSR_IE | X86_MXCSR_DE | X86_MXCSR_ZE));

    if (   (fMxcsr & X86_MXCSR_FZ)
        && RTFLOAT32U_IS_SUBNORMAL(pr32Result))
    {
        /* Underflow masked and flush to zero is set. */
        pr32Result->s.uFraction = 0;
        pr32Result->s.uExponent = 0;
        fXcpt |= X86_MXCSR_UE | X86_MXCSR_PE;
    }

    /* If OE/UE get raised PE won't be set because of the lower priority. */
    if (  (fXcpt & (X86_MXCSR_UE | X86_MXCSR_OE))
        & fMxcsrExcpMask)
        fXcpt &= ~X86_MXCSR_PE;

    return fMxcsr | (fXcpt & X86_MXCSR_XCPT_FLAGS);
}


/**
 * Helper for transfering exception to MXCSR and setting the result value
 * accordingly - ignores Flush-to-Zero.
 *
 * @returns Updated MXCSR.
 * @param   pSoftState      The SoftFloat state following the operation.
 * @param   r32Result       The result of the SoftFloat operation.
 * @param   pr32Result      Where to store the result for IEM.
 * @param   fMxcsr          The original MXCSR value.
 */
DECLINLINE(uint32_t) iemSseSoftStateAndR32ToMxcsrAndIprtResultNoFz(softfloat_state_t const *pSoftState, float32_t r32Result,
                                                                   PRTFLOAT32U pr32Result, uint32_t fMxcsr)
{
    iemFpSoftF32ToIprt(pr32Result, r32Result);

    uint8_t fXcpt = pSoftState->exceptionFlags;
    /* If DAZ is set \#DE is never set. */
    if (   fMxcsr & X86_MXCSR_DAZ
        || (   (fXcpt & X86_MXCSR_DE) /* Softfloat sets DE for sub-normal values. */
            && (RTFLOAT32U_IS_SUBNORMAL(pr32Result))))
        fXcpt &= ~X86_MXCSR_DE;

    return fMxcsr | (fXcpt & X86_MXCSR_XCPT_FLAGS);
}


/**
 * Helper for transfering exception to MXCSR and setting the result value
 * accordingly.
 *
 * @returns Updated MXCSR.
 * @param   pSoftState           The SoftFloat state following the operation.
 * @param   r64Result            The result of the SoftFloat operation.
 * @param   pr64Result           Where to store the result for IEM.
 * @param   fMxcsr               The original MXCSR value.
 */
DECLINLINE(uint32_t) iemSseSoftStateAndR64ToMxcsrAndIprtResult(softfloat_state_t const *pSoftState, float64_t r64Result,
                                                               PRTFLOAT64U pr64Result, uint32_t fMxcsr)
{
    iemFpSoftF64ToIprt(pr64Result, r64Result);
    uint8_t fXcpt = pSoftState->exceptionFlags;
    /* If DAZ is set \#DE is never set. */
    if (   fMxcsr & X86_MXCSR_DAZ
        || (   (fXcpt & X86_MXCSR_DE) /* Softfloat sets DE for sub-normal values. */
            && (RTFLOAT64U_IS_SUBNORMAL(pr64Result))))
        fXcpt &= ~X86_MXCSR_DE;

    /*
     * Skip computing the post-computational exception flags if any of the pre-computational exception flags
     * are set and are unmasked in MXCSR.
     */
    uint32_t const fMxcsrExcpMask = ~((fMxcsr & X86_MXCSR_XCPT_MASK) >> X86_MXCSR_XCPT_MASK_SHIFT);
    if (  ((fMxcsr | fXcpt) & (X86_MXCSR_IE | X86_MXCSR_DE | X86_MXCSR_ZE))
        & fMxcsrExcpMask)
        return fMxcsr | (fXcpt & (X86_MXCSR_IE | X86_MXCSR_DE | X86_MXCSR_ZE));

    if (   (fMxcsr & X86_MXCSR_FZ)
        && RTFLOAT64U_IS_SUBNORMAL(pr64Result))
    {
        /* Underflow masked and flush to zero is set. */
        pr64Result->s.uFractionHigh = 0;
        pr64Result->s.uFractionLow  = 0;
        pr64Result->s.uExponent     = 0;
        fXcpt |= X86_MXCSR_UE | X86_MXCSR_PE;
    }

    /* If OE/UE get raised PE won't be set because of the lower priority. */
    if (  (fXcpt & (X86_MXCSR_UE | X86_MXCSR_OE))
        & fMxcsrExcpMask)
        fXcpt &= ~X86_MXCSR_PE;

    return fMxcsr | (fXcpt & X86_MXCSR_XCPT_FLAGS);
}


/**
 * Helper for transfering exception to MXCSR and setting the result value
 * accordingly - ignores Flush-to-Zero.
 *
 * @returns Updated MXCSR.
 * @param   pSoftState      The SoftFloat state following the operation.
 * @param   r64Result       The result of the SoftFloat operation.
 * @param   pr64Result      Where to store the result for IEM.
 * @param   fMxcsr          The original MXCSR value.
 */
DECLINLINE(uint32_t) iemSseSoftStateAndR64ToMxcsrAndIprtResultNoFz(softfloat_state_t const *pSoftState, float64_t r64Result,
                                                                   PRTFLOAT64U pr64Result, uint32_t fMxcsr)
{
    iemFpSoftF64ToIprt(pr64Result, r64Result);

    uint8_t fXcpt = pSoftState->exceptionFlags;
    /* If DAZ is set \#DE is never set. */
    if (   fMxcsr & X86_MXCSR_DAZ
        || (   (fXcpt & X86_MXCSR_DE) /* Softfloat sets DE for sub-normal values. */
            && (RTFLOAT64U_IS_SUBNORMAL(pr64Result))))
        fXcpt &= ~X86_MXCSR_DE;

    return fMxcsr | (fXcpt & X86_MXCSR_XCPT_FLAGS);
}


/**
 * Sets the given single precision floating point input value to the given output taking the Denormals-as-zero flag
 * in MXCSR into account.
 *
 * @returns The output MXCSR De-normal flag if the input is a de-normal and the DAZ flag is not set.
 * @param   pr32Val         Where to store the result.
 * @param   fMxcsr          The input MXCSR value.
 * @param   pr32Src         The value to use.
 */
DECLINLINE(uint32_t) iemSsePrepareValueR32(PRTFLOAT32U pr32Val, uint32_t fMxcsr, PCRTFLOAT32U pr32Src)
{
    if (RTFLOAT32U_IS_SUBNORMAL(pr32Src))
    {
        if (fMxcsr & X86_MXCSR_DAZ)
        {
            /* De-normals are changed to 0. */
            pr32Val->s.fSign     = pr32Src->s.fSign;
            pr32Val->s.uFraction = 0;
            pr32Val->s.uExponent = 0;
            return 0;
        }

        *pr32Val = *pr32Src;
        return X86_MXCSR_DE;
    }

    *pr32Val = *pr32Src;
    return 0;
}


/**
 * Sets the given double precision floating point input value to the given output taking the Denormals-as-zero flag
 * in MXCSR into account.
 *
 * @returns The output MXCSR De-normal flag if the input is a de-normal and the DAZ flag is not set.
 * @param   pr64Val         Where to store the result.
 * @param   fMxcsr          The input MXCSR value.
 * @param   pr64Src         The value to use.
 */
DECLINLINE(uint32_t) iemSsePrepareValueR64(PRTFLOAT64U pr64Val, uint32_t fMxcsr, PCRTFLOAT64U pr64Src)
{
    if (RTFLOAT64U_IS_SUBNORMAL(pr64Src))
    {
        if (fMxcsr & X86_MXCSR_DAZ)
        {
            /* De-normals are changed to 0. */
            pr64Val->s64.fSign     = pr64Src->s.fSign;
            pr64Val->s64.uFraction = 0;
            pr64Val->s64.uExponent = 0;
            return 0;
        }

        *pr64Val = *pr64Src;
        return X86_MXCSR_DE;
    }

    *pr64Val = *pr64Src;
    return 0;
}


/**
 * Validates the given input operands returning whether the operation can continue or whether one
 * of the source operands contains a NaN value, setting the output accordingly.
 *
 * @returns Flag whether the operation can continue (false) or whether a NaN value was detected in one of the operands (true).
 * @param   pr32Res         Where to store the result in case the operation can't continue.
 * @param   pr32Val1        The first input operand.
 * @param   pr32Val2        The second input operand.
 * @param   pfMxcsr         Where to return the modified MXCSR state when false is returned.
 */
DECLINLINE(bool) iemSseBinaryValIsNaNR32(PRTFLOAT32U pr32Res, PCRTFLOAT32U pr32Val1, PCRTFLOAT32U pr32Val2, uint32_t *pfMxcsr)
{
    uint8_t const cQNan = RTFLOAT32U_IS_QUIET_NAN(pr32Val1)      + RTFLOAT32U_IS_QUIET_NAN(pr32Val2);
    uint8_t const cSNan = RTFLOAT32U_IS_SIGNALLING_NAN(pr32Val1) + RTFLOAT32U_IS_SIGNALLING_NAN(pr32Val2);
    if (cSNan + cQNan == 2)
    {
        /* Both values are either SNan or QNan, first operand is placed into the result and converted to a QNan. */
        *pr32Res = *pr32Val1;
        pr32Res->s.uFraction |= RT_BIT_32(RTFLOAT32U_FRACTION_BITS - 1);
        *pfMxcsr |= (cSNan ? X86_MXCSR_IE : 0);
        return true;
    }
    if (cSNan)
    {
        /* One operand is an SNan and placed into the result, converting it to a QNan. */
        *pr32Res = RTFLOAT32U_IS_SIGNALLING_NAN(pr32Val1) ? *pr32Val1 : *pr32Val2;
        pr32Res->s.uFraction |= RT_BIT_32(RTFLOAT32U_FRACTION_BITS - 1);
        *pfMxcsr |= X86_MXCSR_IE;
        return true;
    }
    if (cQNan)
    {
        /* The QNan operand is placed into the result. */
        *pr32Res = RTFLOAT32U_IS_QUIET_NAN(pr32Val1) ? *pr32Val1 : *pr32Val2;
        return true;
    }

    Assert(!cQNan && !cSNan);
    return false;
}


/**
 * Validates the given double precision input operands returning whether the operation can continue or whether one
 * of the source operands contains a NaN value, setting the output accordingly.
 *
 * @returns Flag whether the operation can continue (false) or whether a NaN value was detected in one of the operands (true).
 * @param   pr64Res         Where to store the result in case the operation can't continue.
 * @param   pr64Val1        The first input operand.
 * @param   pr64Val2        The second input operand.
 * @param   pfMxcsr         Where to return the modified MXCSR state when false is returned.
 */
DECLINLINE(bool) iemSseBinaryValIsNaNR64(PRTFLOAT64U pr64Res, PCRTFLOAT64U pr64Val1, PCRTFLOAT64U pr64Val2, uint32_t *pfMxcsr)
{
    uint8_t const cQNan = RTFLOAT64U_IS_QUIET_NAN(pr64Val1)      + RTFLOAT64U_IS_QUIET_NAN(pr64Val2);
    uint8_t const cSNan = RTFLOAT64U_IS_SIGNALLING_NAN(pr64Val1) + RTFLOAT64U_IS_SIGNALLING_NAN(pr64Val2);
    if (cSNan + cQNan == 2)
    {
        /* Both values are either SNan or QNan, first operand is placed into the result and converted to a QNan. */
        *pr64Res = *pr64Val1;
        pr64Res->s64.uFraction |= RT_BIT_64(RTFLOAT64U_FRACTION_BITS - 1);
        *pfMxcsr |= (cSNan ? X86_MXCSR_IE : 0);
        return true;
    }
    if (cSNan)
    {
        /* One operand is an SNan and placed into the result, converting it to a QNan. */
        *pr64Res = RTFLOAT64U_IS_SIGNALLING_NAN(pr64Val1) ? *pr64Val1 : *pr64Val2;
        pr64Res->s64.uFraction |= RT_BIT_64(RTFLOAT64U_FRACTION_BITS - 1);
        *pfMxcsr |= X86_MXCSR_IE;
        return true;
    }
    if (cQNan)
    {
        /* The QNan operand is placed into the result. */
        *pr64Res = RTFLOAT64U_IS_QUIET_NAN(pr64Val1) ? *pr64Val1 : *pr64Val2;
        return true;
    }

    Assert(!cQNan && !cSNan);
    return false;
}


/**
 * Validates the given single input operand returning whether the operation can continue or whether
 * contains a NaN value, setting the output accordingly.
 *
 * @returns Flag whether the operation can continue (false) or whether a NaN value was detected in the operand (true).
 * @param   pr32Res         Where to store the result in case the operation can't continue.
 * @param   pr32Val         The input operand.
 * @param   pfMxcsr         Where to return the modified MXCSR state when false is returned.
 */
DECLINLINE(bool) iemSseUnaryValIsNaNR32(PRTFLOAT32U pr32Res, PCRTFLOAT32U pr32Val, uint32_t *pfMxcsr)
{
    if (RTFLOAT32U_IS_SIGNALLING_NAN(pr32Val))
    {
        /* One operand is an SNan and placed into the result, converting it to a QNan. */
        *pr32Res = *pr32Val;
        pr32Res->s.uFraction |= RT_BIT_32(RTFLOAT32U_FRACTION_BITS - 1);
        *pfMxcsr |= X86_MXCSR_IE;
        return true;
    }
    if (RTFLOAT32U_IS_QUIET_NAN(pr32Val))
    {
        /* The QNan operand is placed into the result. */
        *pr32Res = *pr32Val;
        return true;
    }

    return false;
}


/**
 * Validates the given double input operand returning whether the operation can continue or whether
 * contains a NaN value, setting the output accordingly.
 *
 * @returns Flag whether the operation can continue (false) or whether a NaN value was detected in the operand (true).
 * @param   pr64Res         Where to store the result in case the operation can't continue.
 * @param   pr64Val         The input operand.
 * @param   pfMxcsr         Where to return the modified MXCSR state when false is returned.
 */
DECLINLINE(bool) iemSseUnaryValIsNaNR64(PRTFLOAT64U pr64Res, PCRTFLOAT64U pr64Val, uint32_t *pfMxcsr)
{
    if (RTFLOAT64U_IS_SIGNALLING_NAN(pr64Val))
    {
        /* One operand is an SNan and placed into the result, converting it to a QNan. */
        *pr64Res = *pr64Val;
        pr64Res->s64.uFraction |= RT_BIT_64(RTFLOAT64U_FRACTION_BITS - 1);
        *pfMxcsr |= X86_MXCSR_IE;
        return true;
    }
    if (RTFLOAT64U_IS_QUIET_NAN(pr64Val))
    {
        /* The QNan operand is placed into the result. */
        *pr64Res = *pr64Val;
        return true;
    }

    return false;
}


/**
 * ADDPS
 */
static uint32_t iemAImpl_addps_u128_worker(PRTFLOAT32U pr32Res, uint32_t fMxcsr, PCRTFLOAT32U pr32Val1, PCRTFLOAT32U pr32Val2)
{
    if (iemSseBinaryValIsNaNR32(pr32Res, pr32Val1, pr32Val2, &fMxcsr))
        return fMxcsr;

    RTFLOAT32U r32Src1, r32Src2;
    fMxcsr |= iemSsePrepareValueR32(&r32Src1, fMxcsr, pr32Val1);
    fMxcsr |= iemSsePrepareValueR32(&r32Src2, fMxcsr, pr32Val2);
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float32_t r32Result = f32_add(iemFpSoftF32FromIprt(&r32Src1), iemFpSoftF32FromIprt(&r32Src2), &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Result, pr32Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_addps_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_addps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_addps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_addps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_addps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vaddps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_addps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_addps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_addps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_addps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vaddps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_addps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_addps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_addps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_addps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3])
           | iemAImpl_addps_u128_worker(&pResult->ar32[4], uMxCsrIn, &puSrc1->ar32[4], &puSrc2->ar32[4])
           | iemAImpl_addps_u128_worker(&pResult->ar32[5], uMxCsrIn, &puSrc1->ar32[5], &puSrc2->ar32[5])
           | iemAImpl_addps_u128_worker(&pResult->ar32[6], uMxCsrIn, &puSrc1->ar32[6], &puSrc2->ar32[6])
           | iemAImpl_addps_u128_worker(&pResult->ar32[7], uMxCsrIn, &puSrc1->ar32[7], &puSrc2->ar32[7]);
}


/**
 * [V]ADDSS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_addss_u128_r32,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_addps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], pr32Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vaddss_u128_r32_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_addps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], pr32Src2);
}


/**
 * ADDPD
 */
static uint32_t iemAImpl_addpd_u128_worker(PRTFLOAT64U pr64Res, uint32_t fMxcsr, PCRTFLOAT64U pr64Val1, PCRTFLOAT64U pr64Val2)
{
    if (iemSseBinaryValIsNaNR64(pr64Res, pr64Val1, pr64Val2, &fMxcsr))
        return fMxcsr;

    RTFLOAT64U r64Src1, r64Src2;
    fMxcsr |= iemSsePrepareValueR64(&r64Src1, fMxcsr, pr64Val1);
    fMxcsr |= iemSsePrepareValueR64(&r64Src2, fMxcsr, pr64Val2);
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float64_t r64Result = f64_add(iemFpSoftF64FromIprt(&r64Src1), iemFpSoftF64FromIprt(&r64Src2), &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResult(&SoftState, r64Result, pr64Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_addpd_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_addpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_addpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vaddpd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_addpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_addpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vaddpd_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_addpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_addpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1])
           | iemAImpl_addpd_u128_worker(&pResult->ar64[2], uMxCsrIn, &puSrc1->ar64[2], &puSrc2->ar64[2])
           | iemAImpl_addpd_u128_worker(&pResult->ar64[3], uMxCsrIn, &puSrc1->ar64[3], &puSrc2->ar64[3]);
}


/**
 * [V]ADDSD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_addsd_u128_r64,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_addpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], pr64Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vaddsd_u128_r64_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_addpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], pr64Src2);
}


/**
 * [V]MULPS
 */
static uint32_t iemAImpl_mulps_u128_worker(PRTFLOAT32U pr32Res, uint32_t fMxcsr, PCRTFLOAT32U pr32Val1, PCRTFLOAT32U pr32Val2)
{
    if (iemSseBinaryValIsNaNR32(pr32Res, pr32Val1, pr32Val2, &fMxcsr))
        return fMxcsr;

    RTFLOAT32U r32Src1, r32Src2;
    fMxcsr |= iemSsePrepareValueR32(&r32Src1, fMxcsr, pr32Val1);
    fMxcsr |= iemSsePrepareValueR32(&r32Src2, fMxcsr, pr32Val2);
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float32_t r32Result = f32_mul(iemFpSoftF32FromIprt(&r32Src1), iemFpSoftF32FromIprt(&r32Src2), &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Result, pr32Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_mulps_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_mulps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_mulps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_mulps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_mulps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vmulps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_mulps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_mulps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_mulps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_mulps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vmulps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_mulps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_mulps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_mulps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_mulps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3])
           | iemAImpl_mulps_u128_worker(&pResult->ar32[4], uMxCsrIn, &puSrc1->ar32[4], &puSrc2->ar32[4])
           | iemAImpl_mulps_u128_worker(&pResult->ar32[5], uMxCsrIn, &puSrc1->ar32[5], &puSrc2->ar32[5])
           | iemAImpl_mulps_u128_worker(&pResult->ar32[6], uMxCsrIn, &puSrc1->ar32[6], &puSrc2->ar32[6])
           | iemAImpl_mulps_u128_worker(&pResult->ar32[7], uMxCsrIn, &puSrc1->ar32[7], &puSrc2->ar32[7]);
}


/**
 * [V]MULSS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_mulss_u128_r32,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_mulps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], pr32Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vmulss_u128_r32_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_mulps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], pr32Src2);
}


/**
 * [V]MULPD
 */
static uint32_t iemAImpl_mulpd_u128_worker(PRTFLOAT64U pr64Res, uint32_t fMxcsr, PCRTFLOAT64U pr64Val1, PCRTFLOAT64U pr64Val2)
{
    if (iemSseBinaryValIsNaNR64(pr64Res, pr64Val1, pr64Val2, &fMxcsr))
        return fMxcsr;

    RTFLOAT64U r64Src1, r64Src2;
    fMxcsr |= iemSsePrepareValueR64(&r64Src1, fMxcsr, pr64Val1);
    fMxcsr |= iemSsePrepareValueR64(&r64Src2, fMxcsr, pr64Val2);
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float64_t r64Result = f64_mul(iemFpSoftF64FromIprt(&r64Src1), iemFpSoftF64FromIprt(&r64Src2), &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResult(&SoftState, r64Result, pr64Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_mulpd_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_mulpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_mulpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vmulpd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_mulpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_mulpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vmulpd_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_mulpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_mulpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1])
           | iemAImpl_mulpd_u128_worker(&pResult->ar64[2], uMxCsrIn, &puSrc1->ar64[2], &puSrc2->ar64[2])
           | iemAImpl_mulpd_u128_worker(&pResult->ar64[3], uMxCsrIn, &puSrc1->ar64[3], &puSrc2->ar64[3]);
}


/**
 * [V]MULSD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_mulsd_u128_r64,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_mulpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], pr64Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vmulsd_u128_r64_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_mulpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], pr64Src2);
}


/**
 * [V]SUBPS
 */
static uint32_t iemAImpl_subps_u128_worker(PRTFLOAT32U pr32Res, uint32_t fMxcsr, PCRTFLOAT32U pr32Val1, PCRTFLOAT32U pr32Val2)
{
    if (iemSseBinaryValIsNaNR32(pr32Res, pr32Val1, pr32Val2, &fMxcsr))
        return fMxcsr;

    RTFLOAT32U r32Src1, r32Src2;
    fMxcsr |= iemSsePrepareValueR32(&r32Src1, fMxcsr, pr32Val1);
    fMxcsr |= iemSsePrepareValueR32(&r32Src2, fMxcsr, pr32Val2);
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float32_t r32Result = f32_sub(iemFpSoftF32FromIprt(&r32Src1), iemFpSoftF32FromIprt(&r32Src2), &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Result, pr32Res, fMxcsr);
}

#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_subps_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_subps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_subps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_subps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_subps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vsubps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_subps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_subps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_subps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_subps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vsubps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_subps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_subps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_subps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_subps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3])
           | iemAImpl_subps_u128_worker(&pResult->ar32[4], uMxCsrIn, &puSrc1->ar32[4], &puSrc2->ar32[4])
           | iemAImpl_subps_u128_worker(&pResult->ar32[5], uMxCsrIn, &puSrc1->ar32[5], &puSrc2->ar32[5])
           | iemAImpl_subps_u128_worker(&pResult->ar32[6], uMxCsrIn, &puSrc1->ar32[6], &puSrc2->ar32[6])
           | iemAImpl_subps_u128_worker(&pResult->ar32[7], uMxCsrIn, &puSrc1->ar32[7], &puSrc2->ar32[7]);
}


/**
 * [V]SUBSS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_subss_u128_r32,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_subps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], pr32Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vsubss_u128_r32_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_subps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], pr32Src2);
}


/**
 * [V]SUBPD
 */
static uint32_t iemAImpl_subpd_u128_worker(PRTFLOAT64U pr64Res, uint32_t fMxcsr, PCRTFLOAT64U pr64Val1, PCRTFLOAT64U pr64Val2)
{
    if (iemSseBinaryValIsNaNR64(pr64Res, pr64Val1, pr64Val2, &fMxcsr))
        return fMxcsr;

    RTFLOAT64U r64Src1, r64Src2;
    fMxcsr |= iemSsePrepareValueR64(&r64Src1, fMxcsr, pr64Val1);
    fMxcsr |= iemSsePrepareValueR64(&r64Src2, fMxcsr, pr64Val2);
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float64_t r64Result = f64_sub(iemFpSoftF64FromIprt(&r64Src1), iemFpSoftF64FromIprt(&r64Src2), &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResult(&SoftState, r64Result, pr64Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_subpd_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_subpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_subpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vsubpd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_subpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_subpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vsubpd_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_subpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_subpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1])
           | iemAImpl_subpd_u128_worker(&pResult->ar64[2], uMxCsrIn, &puSrc1->ar64[2], &puSrc2->ar64[2])
           | iemAImpl_subpd_u128_worker(&pResult->ar64[3], uMxCsrIn, &puSrc1->ar64[3], &puSrc2->ar64[3]);
}


/**
 * [V]SUBSD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_subsd_u128_r64,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_subpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], pr64Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vsubsd_u128_r64_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_subpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], pr64Src2);
}


/**
 * [V]MINPS
 */
static uint32_t iemAImpl_minps_u128_worker(PRTFLOAT32U pr32Res, uint32_t fMxcsr, PCRTFLOAT32U pr32Val1, PCRTFLOAT32U pr32Val2)
{
    if (RTFLOAT32U_IS_NAN(pr32Val1) || RTFLOAT32U_IS_NAN(pr32Val2))
    {
        /* The DAZ flag gets honored but the DE flag will not get set because \#IE has higher priority. */
        iemSsePrepareValueR32(pr32Res, fMxcsr, pr32Val2);
        return fMxcsr | X86_MXCSR_IE;
    }

    RTFLOAT32U r32Src1, r32Src2;
    fMxcsr |= iemSsePrepareValueR32(&r32Src1, fMxcsr, pr32Val1);
    fMxcsr |= iemSsePrepareValueR32(&r32Src2, fMxcsr, pr32Val2);
    if (RTFLOAT32U_IS_ZERO(&r32Src1) && RTFLOAT32U_IS_ZERO(&r32Src2))
    {
        *pr32Res = r32Src2;
        return fMxcsr;
    }

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    bool fLe = f32_le(iemFpSoftF32FromIprt(&r32Src1), iemFpSoftF32FromIprt(&r32Src2), &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResultNoFz(&SoftState,
                                                           fLe
                                                         ? iemFpSoftF32FromIprt(&r32Src1)
                                                         : iemFpSoftF32FromIprt(&r32Src2),
                                                         pr32Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_minps_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_minps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_minps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_minps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_minps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vminps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_minps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_minps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_minps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_minps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vminps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_minps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_minps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_minps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_minps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3])
           | iemAImpl_minps_u128_worker(&pResult->ar32[4], uMxCsrIn, &puSrc1->ar32[4], &puSrc2->ar32[4])
           | iemAImpl_minps_u128_worker(&pResult->ar32[5], uMxCsrIn, &puSrc1->ar32[5], &puSrc2->ar32[5])
           | iemAImpl_minps_u128_worker(&pResult->ar32[6], uMxCsrIn, &puSrc1->ar32[6], &puSrc2->ar32[6])
           | iemAImpl_minps_u128_worker(&pResult->ar32[7], uMxCsrIn, &puSrc1->ar32[7], &puSrc2->ar32[7]);
}


/**
 * [V]MINSS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_minss_u128_r32,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_minps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], pr32Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vminss_u128_r32_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_minps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], pr32Src2);
}


/**
 * [V]MINPD
 */
static uint32_t iemAImpl_minpd_u128_worker(PRTFLOAT64U pr64Res, uint32_t fMxcsr, PCRTFLOAT64U pr64Val1, PCRTFLOAT64U pr64Val2)
{
    if (RTFLOAT64U_IS_NAN(pr64Val1) || RTFLOAT64U_IS_NAN(pr64Val2))
    {
        /* The DAZ flag gets honored but the DE flag will not get set because \#IE has higher priority. */
        iemSsePrepareValueR64(pr64Res, fMxcsr, pr64Val2);
        return fMxcsr | X86_MXCSR_IE;
    }

    RTFLOAT64U r64Src1, r64Src2;
    fMxcsr |= iemSsePrepareValueR64(&r64Src1, fMxcsr, pr64Val1);
    fMxcsr |= iemSsePrepareValueR64(&r64Src2, fMxcsr, pr64Val2);
    if (RTFLOAT64U_IS_ZERO(&r64Src1) && RTFLOAT64U_IS_ZERO(&r64Src2))
    {
        *pr64Res = r64Src2;
        return fMxcsr;
    }

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    bool fLe = f64_le(iemFpSoftF64FromIprt(&r64Src1), iemFpSoftF64FromIprt(&r64Src2), &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResultNoFz(&SoftState,
                                                           fLe
                                                         ? iemFpSoftF64FromIprt(&r64Src1)
                                                         : iemFpSoftF64FromIprt(&r64Src2),
                                                         pr64Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_minpd_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_minpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_minpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vminpd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_minpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_minpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vminpd_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_minpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_minpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1])
           | iemAImpl_minpd_u128_worker(&pResult->ar64[2], uMxCsrIn, &puSrc1->ar64[2], &puSrc2->ar64[2])
           | iemAImpl_minpd_u128_worker(&pResult->ar64[3], uMxCsrIn, &puSrc1->ar64[3], &puSrc2->ar64[3]);
}


/**
 * [V]MINSD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_minsd_u128_r64,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_minpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], pr64Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vminsd_u128_r64_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_minpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], pr64Src2);
}


/**
 * [V]DIVPS
 */
static uint32_t iemAImpl_divps_u128_worker(PRTFLOAT32U pr32Res, uint32_t fMxcsr, PCRTFLOAT32U pr32Val1, PCRTFLOAT32U pr32Val2)
{
    if (iemSseBinaryValIsNaNR32(pr32Res, pr32Val1, pr32Val2, &fMxcsr))
        return fMxcsr;

    RTFLOAT32U r32Src1, r32Src2;
    uint32_t fDe  = iemSsePrepareValueR32(&r32Src1, fMxcsr, pr32Val1);
             fDe |= iemSsePrepareValueR32(&r32Src2, fMxcsr, pr32Val2);
    if (RTFLOAT32U_IS_ZERO(&r32Src2))
    {
        if (   RTFLOAT32U_IS_ZERO(&r32Src1)
            || RTFLOAT32U_IS_QUIET_NAN(&r32Src1))
        {
            *pr32Res = g_ar32QNaN[1];
            return fMxcsr | X86_MXCSR_IE;
        }
        else if (RTFLOAT32U_IS_INF(&r32Src1))
        {
            *pr32Res = g_ar32Infinity[r32Src1.s.fSign != r32Src2.s.fSign];
            return fMxcsr;
        }
        else
        {
            *pr32Res = g_ar32Infinity[r32Src1.s.fSign != r32Src2.s.fSign];
            return fMxcsr | X86_MXCSR_ZE;
        }
    }

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float32_t r32Result = f32_div(iemFpSoftF32FromIprt(&r32Src1), iemFpSoftF32FromIprt(&r32Src2), &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Result, pr32Res, fMxcsr | fDe);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_divps_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_divps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_divps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_divps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_divps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vdivps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_divps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_divps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_divps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_divps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vdivps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_divps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_divps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_divps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_divps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3])
           | iemAImpl_divps_u128_worker(&pResult->ar32[4], uMxCsrIn, &puSrc1->ar32[4], &puSrc2->ar32[4])
           | iemAImpl_divps_u128_worker(&pResult->ar32[5], uMxCsrIn, &puSrc1->ar32[5], &puSrc2->ar32[5])
           | iemAImpl_divps_u128_worker(&pResult->ar32[6], uMxCsrIn, &puSrc1->ar32[6], &puSrc2->ar32[6])
           | iemAImpl_divps_u128_worker(&pResult->ar32[7], uMxCsrIn, &puSrc1->ar32[7], &puSrc2->ar32[7]);
}


/**
 * [V]DIVSS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_divss_u128_r32,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_divps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], pr32Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vdivss_u128_r32_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_divps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], pr32Src2);
}


/**
 * DIVPD
 */
static uint32_t iemAImpl_divpd_u128_worker(PRTFLOAT64U pr64Res, uint32_t fMxcsr, PCRTFLOAT64U pr64Val1, PCRTFLOAT64U pr64Val2)
{
    if (iemSseBinaryValIsNaNR64(pr64Res, pr64Val1, pr64Val2, &fMxcsr))
        return fMxcsr;

    RTFLOAT64U r64Src1, r64Src2;
    uint32_t fDe  = iemSsePrepareValueR64(&r64Src1, fMxcsr, pr64Val1);
             fDe |= iemSsePrepareValueR64(&r64Src2, fMxcsr, pr64Val2);
    if (RTFLOAT64U_IS_ZERO(&r64Src2))
    {
        if (   RTFLOAT64U_IS_ZERO(&r64Src1)
            || RTFLOAT64U_IS_QUIET_NAN(&r64Src1))
        {
            *pr64Res = g_ar64QNaN[1];
            return fMxcsr | X86_MXCSR_IE;
        }
        else if (RTFLOAT64U_IS_INF(&r64Src1))
        {
            *pr64Res = g_ar64Infinity[r64Src1.s.fSign != r64Src2.s.fSign];
            return fMxcsr;
        }
        else
        {
            *pr64Res = g_ar64Infinity[r64Src1.s.fSign != r64Src2.s.fSign];
            return fMxcsr | X86_MXCSR_ZE;
        }
    }

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float64_t r64Result = f64_div(iemFpSoftF64FromIprt(&r64Src1), iemFpSoftF64FromIprt(&r64Src2), &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResult(&SoftState, r64Result, pr64Res, fMxcsr | fDe);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_divpd_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_divpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_divpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vdivpd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_divpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_divpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vdivpd_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_divpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_divpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1])
           | iemAImpl_divpd_u128_worker(&pResult->ar64[2], uMxCsrIn, &puSrc1->ar64[2], &puSrc2->ar64[2])
           | iemAImpl_divpd_u128_worker(&pResult->ar64[3], uMxCsrIn, &puSrc1->ar64[3], &puSrc2->ar64[3]);
}


/**
 * [V]DIVSD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_divsd_u128_r64,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_divpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], pr64Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vdivsd_u128_r64_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_divpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], pr64Src2);
}


/**
 * [V]MAXPS
 */
static uint32_t iemAImpl_maxps_u128_worker(PRTFLOAT32U pr32Res, uint32_t fMxcsr, PCRTFLOAT32U pr32Val1, PCRTFLOAT32U pr32Val2)
{
    if (RTFLOAT32U_IS_NAN(pr32Val1) || RTFLOAT32U_IS_NAN(pr32Val2))
    {
        /* The DAZ flag gets honored but the DE flag will not get set because \#IE has higher priority. */
        iemSsePrepareValueR32(pr32Res, fMxcsr, pr32Val2);
        return fMxcsr | X86_MXCSR_IE;
    }

    RTFLOAT32U r32Src1, r32Src2;
    fMxcsr |= iemSsePrepareValueR32(&r32Src1, fMxcsr, pr32Val1);
    fMxcsr |= iemSsePrepareValueR32(&r32Src2, fMxcsr, pr32Val2);
    if (RTFLOAT32U_IS_ZERO(&r32Src1) && RTFLOAT32U_IS_ZERO(&r32Src2))
    {
        *pr32Res = r32Src2;
        return fMxcsr;
    }

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    bool fLe = f32_le(iemFpSoftF32FromIprt(&r32Src1), iemFpSoftF32FromIprt(&r32Src2), &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResultNoFz(&SoftState,
                                                           fLe
                                                         ? iemFpSoftF32FromIprt(&r32Src2)
                                                         : iemFpSoftF32FromIprt(&r32Src1),
                                                         pr32Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_maxps_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_maxps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_maxps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_maxps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_maxps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vmaxps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_maxps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_maxps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_maxps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_maxps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vmaxps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_maxps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_maxps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_maxps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_maxps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3])
           | iemAImpl_maxps_u128_worker(&pResult->ar32[4], uMxCsrIn, &puSrc1->ar32[4], &puSrc2->ar32[4])
           | iemAImpl_maxps_u128_worker(&pResult->ar32[5], uMxCsrIn, &puSrc1->ar32[5], &puSrc2->ar32[5])
           | iemAImpl_maxps_u128_worker(&pResult->ar32[6], uMxCsrIn, &puSrc1->ar32[6], &puSrc2->ar32[6])
           | iemAImpl_maxps_u128_worker(&pResult->ar32[7], uMxCsrIn, &puSrc1->ar32[7], &puSrc2->ar32[7]);
}


/**
 * [V]MAXSS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_maxss_u128_r32,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_maxps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], pr32Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vmaxss_u128_r32_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_maxps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], pr32Src2);
}


/**
 * MAXPD
 */
static uint32_t iemAImpl_maxpd_u128_worker(PRTFLOAT64U pr64Res, uint32_t fMxcsr, PCRTFLOAT64U pr64Val1, PCRTFLOAT64U pr64Val2)
{
    if (RTFLOAT64U_IS_NAN(pr64Val1) || RTFLOAT64U_IS_NAN(pr64Val2))
    {
        /* The DAZ flag gets honored but the DE flag will not get set because \#IE has higher priority. */
        iemSsePrepareValueR64(pr64Res, fMxcsr, pr64Val2);
        return fMxcsr | X86_MXCSR_IE;
    }

    RTFLOAT64U r64Src1, r64Src2;
    fMxcsr |= iemSsePrepareValueR64(&r64Src1, fMxcsr, pr64Val1);
    fMxcsr |= iemSsePrepareValueR64(&r64Src2, fMxcsr, pr64Val2);
    if (RTFLOAT64U_IS_ZERO(&r64Src1) && RTFLOAT64U_IS_ZERO(&r64Src2))
    {
        *pr64Res = r64Src2;
        return fMxcsr;
    }

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    bool fLe = f64_le(iemFpSoftF64FromIprt(&r64Src1), iemFpSoftF64FromIprt(&r64Src2), &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResultNoFz(&SoftState,
                                                           fLe
                                                         ? iemFpSoftF64FromIprt(&r64Src2)
                                                         : iemFpSoftF64FromIprt(&r64Src1),
                                                         pr64Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_maxpd_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_maxpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_maxpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vmaxpd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_maxpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_maxpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vmaxpd_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_maxpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_maxpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1])
           | iemAImpl_maxpd_u128_worker(&pResult->ar64[2], uMxCsrIn, &puSrc1->ar64[2], &puSrc2->ar64[2])
           | iemAImpl_maxpd_u128_worker(&pResult->ar64[3], uMxCsrIn, &puSrc1->ar64[3], &puSrc2->ar64[3]);
}


/**
 * [V]MAXSD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_maxsd_u128_r64,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_maxpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], pr64Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vmaxsd_u128_r64_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_maxpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], pr64Src2);
}


/**
 * [V]CVTSS2SD
 */
static uint32_t iemAImpl_cvtss2sd_u128_r32_worker(PRTFLOAT64U pr64Res, uint32_t fMxcsr, PCRTFLOAT32U pr32Val1)
{
    RTFLOAT32U r32Src1;
    fMxcsr |= iemSsePrepareValueR32(&r32Src1, fMxcsr, pr32Val1);

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float64_t r64Result = f32_to_f64(iemFpSoftF32FromIprt(&r32Src1), &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResult(&SoftState, r64Result, pr64Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtss2sd_u128_r32,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_cvtss2sd_u128_r32_worker(&pResult->ar64[0], uMxCsrIn, pr32Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtss2sd_u128_r32_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    puDst->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_cvtss2sd_u128_r32_worker(&puDst->ar64[0], uMxCsrIn, pr32Src2);
}


/**
 * [V]CVTSD2SS
 */
static uint32_t iemAImpl_cvtsd2ss_u128_r64_worker(PRTFLOAT32U pr32Res, uint32_t fMxcsr, PCRTFLOAT64U pr64Val1)
{
    RTFLOAT64U r64Src1;
    fMxcsr |= iemSsePrepareValueR64(&r64Src1, fMxcsr, pr64Val1);

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float32_t r32Result = f64_to_f32(iemFpSoftF64FromIprt(&r64Src1), &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Result, pr32Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtsd2ss_u128_r64,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_cvtsd2ss_u128_r64_worker(&pResult->ar32[0], uMxCsrIn, pr64Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtsd2ss_u128_r64_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_cvtsd2ss_u128_r64_worker(&pResult->ar32[0], uMxCsrIn, pr64Src2);
}


/**
 * [V]HADDPS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_haddps_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_addps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc1->ar32[1])
           | iemAImpl_addps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[2], &puSrc1->ar32[3])
           | iemAImpl_addps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc2->ar32[0], &puSrc2->ar32[1])
           | iemAImpl_addps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc2->ar32[2], &puSrc2->ar32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vhaddps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_addps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc1->ar32[1])
           | iemAImpl_addps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[2], &puSrc1->ar32[3])
           | iemAImpl_addps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc2->ar32[0], &puSrc2->ar32[1])
           | iemAImpl_addps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc2->ar32[2], &puSrc2->ar32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vhaddps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_addps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc1->ar32[1])
           | iemAImpl_addps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[2], &puSrc1->ar32[3])
           | iemAImpl_addps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc2->ar32[0], &puSrc2->ar32[1])
           | iemAImpl_addps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc2->ar32[2], &puSrc2->ar32[3])
           | iemAImpl_addps_u128_worker(&pResult->ar32[4], uMxCsrIn, &puSrc1->ar32[4], &puSrc1->ar32[5])
           | iemAImpl_addps_u128_worker(&pResult->ar32[5], uMxCsrIn, &puSrc1->ar32[6], &puSrc1->ar32[7])
           | iemAImpl_addps_u128_worker(&pResult->ar32[6], uMxCsrIn, &puSrc2->ar32[4], &puSrc2->ar32[5])
           | iemAImpl_addps_u128_worker(&pResult->ar32[7], uMxCsrIn, &puSrc2->ar32[6], &puSrc2->ar32[7]);
}


/**
 * [V]HADDPD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_haddpd_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_addpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc1->ar64[1])
           | iemAImpl_addpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc2->ar64[0], &puSrc2->ar64[1]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vhaddpd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_addpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc1->ar64[1])
           | iemAImpl_addpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc2->ar64[0], &puSrc2->ar64[1]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vhaddpd_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_addpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc1->ar64[1])
           | iemAImpl_addpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc2->ar64[0], &puSrc2->ar64[1])
           | iemAImpl_addpd_u128_worker(&pResult->ar64[2], uMxCsrIn, &puSrc1->ar64[2], &puSrc1->ar64[3])
           | iemAImpl_addpd_u128_worker(&pResult->ar64[3], uMxCsrIn, &puSrc2->ar64[2], &puSrc2->ar64[3]);
}


/**
 * [V]HSUBPS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_hsubps_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_subps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc1->ar32[1])
           | iemAImpl_subps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[2], &puSrc1->ar32[3])
           | iemAImpl_subps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc2->ar32[0], &puSrc2->ar32[1])
           | iemAImpl_subps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc2->ar32[2], &puSrc2->ar32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vhsubps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_subps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc1->ar32[1])
           | iemAImpl_subps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[2], &puSrc1->ar32[3])
           | iemAImpl_subps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc2->ar32[0], &puSrc2->ar32[1])
           | iemAImpl_subps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc2->ar32[2], &puSrc2->ar32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vhsubps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_subps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc1->ar32[1])
           | iemAImpl_subps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[2], &puSrc1->ar32[3])
           | iemAImpl_subps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc2->ar32[0], &puSrc2->ar32[1])
           | iemAImpl_subps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc2->ar32[2], &puSrc2->ar32[3])
           | iemAImpl_subps_u128_worker(&pResult->ar32[4], uMxCsrIn, &puSrc1->ar32[4], &puSrc1->ar32[5])
           | iemAImpl_subps_u128_worker(&pResult->ar32[5], uMxCsrIn, &puSrc1->ar32[6], &puSrc1->ar32[7])
           | iemAImpl_subps_u128_worker(&pResult->ar32[6], uMxCsrIn, &puSrc2->ar32[4], &puSrc2->ar32[5])
           | iemAImpl_subps_u128_worker(&pResult->ar32[7], uMxCsrIn, &puSrc2->ar32[6], &puSrc2->ar32[7]);
}


/**
 * [V]HSUBPD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_hsubpd_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_subpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc1->ar64[1])
           | iemAImpl_subpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc2->ar64[0], &puSrc2->ar64[1]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vhsubpd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_subpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc1->ar64[1])
           | iemAImpl_subpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc2->ar64[0], &puSrc2->ar64[1]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vhsubpd_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_subpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc1->ar64[1])
           | iemAImpl_subpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc2->ar64[0], &puSrc2->ar64[1])
           | iemAImpl_subpd_u128_worker(&pResult->ar64[2], uMxCsrIn, &puSrc1->ar64[2], &puSrc1->ar64[3])
           | iemAImpl_subpd_u128_worker(&pResult->ar64[3], uMxCsrIn, &puSrc2->ar64[2], &puSrc2->ar64[3]);
}


/**
 * [V]SQRTPS
 */
static uint32_t iemAImpl_sqrtps_u128_worker(PRTFLOAT32U pr32Res, uint32_t fMxcsr, PCRTFLOAT32U pr32Val)
{
    if (iemSseUnaryValIsNaNR32(pr32Res, pr32Val, &fMxcsr))
        return fMxcsr;

    RTFLOAT32U r32Src;
    uint32_t fDe = iemSsePrepareValueR32(&r32Src, fMxcsr, pr32Val);
    if (RTFLOAT32U_IS_ZERO(&r32Src))
    {
        *pr32Res = r32Src;
        return fMxcsr;
    }
    else if (r32Src.s.fSign)
    {
        *pr32Res = g_ar32QNaN[1];
        return fMxcsr | X86_MXCSR_IE;
    }

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float32_t r32Result = f32_sqrt(iemFpSoftF32FromIprt(&r32Src), &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Result, pr32Res, fMxcsr | fDe);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_sqrtps_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    RT_NOREF(puSrc1);

    return   iemAImpl_sqrtps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc2->ar32[0])
           | iemAImpl_sqrtps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc2->ar32[1])
           | iemAImpl_sqrtps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc2->ar32[2])
           | iemAImpl_sqrtps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc2->ar32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vsqrtps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc))
{
    return   iemAImpl_sqrtps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc->ar32[0])
           | iemAImpl_sqrtps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc->ar32[1])
           | iemAImpl_sqrtps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc->ar32[2])
           | iemAImpl_sqrtps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc->ar32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vsqrtps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc))
{
    return   iemAImpl_sqrtps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc->ar32[0])
           | iemAImpl_sqrtps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc->ar32[1])
           | iemAImpl_sqrtps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc->ar32[2])
           | iemAImpl_sqrtps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc->ar32[3])
           | iemAImpl_sqrtps_u128_worker(&pResult->ar32[4], uMxCsrIn, &puSrc->ar32[4])
           | iemAImpl_sqrtps_u128_worker(&pResult->ar32[5], uMxCsrIn, &puSrc->ar32[5])
           | iemAImpl_sqrtps_u128_worker(&pResult->ar32[6], uMxCsrIn, &puSrc->ar32[6])
           | iemAImpl_sqrtps_u128_worker(&pResult->ar32[7], uMxCsrIn, &puSrc->ar32[7]);
}


/**
 * [V]SQRTSS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_sqrtss_u128_r32,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_sqrtps_u128_worker(&pResult->ar32[0], uMxCsrIn, pr32Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vsqrtss_u128_r32_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_sqrtps_u128_worker(&pResult->ar32[0], uMxCsrIn, pr32Src2);
}


/**
 * [V]SQRTPD
 */
static uint32_t iemAImpl_sqrtpd_u128_worker(PRTFLOAT64U pr64Res, uint32_t fMxcsr, PCRTFLOAT64U pr64Val)
{
    if (iemSseUnaryValIsNaNR64(pr64Res, pr64Val, &fMxcsr))
        return fMxcsr;

    RTFLOAT64U r64Src;
    uint32_t fDe = iemSsePrepareValueR64(&r64Src, fMxcsr, pr64Val);
    if (RTFLOAT64U_IS_ZERO(&r64Src))
    {
        *pr64Res = r64Src;
        return fMxcsr;
    }
    else if (r64Src.s.fSign)
    {
        *pr64Res = g_ar64QNaN[1];
        return fMxcsr | X86_MXCSR_IE;
    }

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float64_t r64Result = f64_sqrt(iemFpSoftF64FromIprt(&r64Src), &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResult(&SoftState, r64Result, pr64Res, fMxcsr | fDe);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_sqrtpd_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    RT_NOREF(puSrc1);

    return   iemAImpl_sqrtpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc2->ar64[0])
           | iemAImpl_sqrtpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc2->ar64[1]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vsqrtpd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc))
{
    return   iemAImpl_sqrtpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc->ar64[0])
           | iemAImpl_sqrtpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc->ar64[1]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vsqrtpd_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc))
{
    return   iemAImpl_sqrtpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc->ar64[0])
           | iemAImpl_sqrtpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc->ar64[1])
           | iemAImpl_sqrtpd_u128_worker(&pResult->ar64[2], uMxCsrIn, &puSrc->ar64[2])
           | iemAImpl_sqrtpd_u128_worker(&pResult->ar64[3], uMxCsrIn, &puSrc->ar64[3]);
}


/**
 * SQRTSD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_sqrtsd_u128_r64,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_sqrtpd_u128_worker(&pResult->ar64[0], uMxCsrIn, pr64Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vsqrtsd_u128_r64_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT64U pr64Src2))
{
    pResult->ar64[1] = puSrc1->ar64[1];
    return iemAImpl_sqrtpd_u128_worker(&pResult->ar64[0], uMxCsrIn, pr64Src2);
}


/**
 * [V]RSQRTPS
 */
static uint32_t iemAImpl_rsqrt_worker(PRTFLOAT32U pr32Res, uint32_t fMxcsr, PCRTFLOAT32U pr32Val)
{
    if (iemSseUnaryValIsNaNR32(pr32Res, pr32Val, &fMxcsr))
        return fMxcsr;

    RTFLOAT32U r32Src;
    iemSsePrepareValueR32(&r32Src, fMxcsr | X86_MXCSR_DAZ, pr32Val);
    if (RTFLOAT32U_IS_ZERO(&r32Src))
    {
        *pr32Res = g_ar32Infinity[r32Src.s.fSign];
        return fMxcsr;
    }
    else if (r32Src.s.fSign)
    {
        *pr32Res = g_ar32QNaN[1];
        return fMxcsr | X86_MXCSR_IE;
    }

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float32_t r32Result = f32_rsqrt(iemFpSoftF32FromIprt(&r32Src), &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Result, pr32Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_rsqrtps_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    RT_NOREF(puSrc1);

    return   iemAImpl_rsqrt_worker(&pResult->ar32[0], uMxCsrIn, &puSrc2->ar32[0])
           | iemAImpl_rsqrt_worker(&pResult->ar32[1], uMxCsrIn, &puSrc2->ar32[1])
           | iemAImpl_rsqrt_worker(&pResult->ar32[2], uMxCsrIn, &puSrc2->ar32[2])
           | iemAImpl_rsqrt_worker(&pResult->ar32[3], uMxCsrIn, &puSrc2->ar32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vrsqrtps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc))
{
    return   iemAImpl_rsqrt_worker(&pResult->ar32[0], uMxCsrIn, &puSrc->ar32[0])
           | iemAImpl_rsqrt_worker(&pResult->ar32[1], uMxCsrIn, &puSrc->ar32[1])
           | iemAImpl_rsqrt_worker(&pResult->ar32[2], uMxCsrIn, &puSrc->ar32[2])
           | iemAImpl_rsqrt_worker(&pResult->ar32[3], uMxCsrIn, &puSrc->ar32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vrsqrtps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc))
{
    return   iemAImpl_rsqrt_worker(&pResult->ar32[0], uMxCsrIn, &puSrc->ar32[0])
           | iemAImpl_rsqrt_worker(&pResult->ar32[1], uMxCsrIn, &puSrc->ar32[1])
           | iemAImpl_rsqrt_worker(&pResult->ar32[2], uMxCsrIn, &puSrc->ar32[2])
           | iemAImpl_rsqrt_worker(&pResult->ar32[3], uMxCsrIn, &puSrc->ar32[3])
           | iemAImpl_rsqrt_worker(&pResult->ar32[4], uMxCsrIn, &puSrc->ar32[4])
           | iemAImpl_rsqrt_worker(&pResult->ar32[5], uMxCsrIn, &puSrc->ar32[5])
           | iemAImpl_rsqrt_worker(&pResult->ar32[6], uMxCsrIn, &puSrc->ar32[6])
           | iemAImpl_rsqrt_worker(&pResult->ar32[7], uMxCsrIn, &puSrc->ar32[7]);
}


/**
 * RSQRTSS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_rsqrtss_u128_r32,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_rsqrt_worker(&pResult->ar32[0], uMxCsrIn, pr32Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vrsqrtss_u128_r32_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_rsqrt_worker(&pResult->ar32[0], uMxCsrIn, pr32Src2);
}


/**
 * [V]RCPPS
 */
static uint32_t iemAImpl_rcp_worker(PRTFLOAT32U pr32Res, uint32_t fMxcsr, PCRTFLOAT32U pr32Val)
{
    if (iemSseUnaryValIsNaNR32(pr32Res, pr32Val, &fMxcsr))
        return fMxcsr;

    RTFLOAT32U r32Src;
    iemSsePrepareValueR32(&r32Src, fMxcsr | X86_MXCSR_DAZ, pr32Val);
    if (RTFLOAT32U_IS_ZERO(&r32Src))
    {
        *pr32Res = g_ar32Infinity[r32Src.s.fSign];
        return fMxcsr;
    }

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float32_t r32Result = f32_div(iemFpSoftF32FromIprt(&g_ar32One[0]), iemFpSoftF32FromIprt(&r32Src), &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Result, pr32Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_rcpps_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    RT_NOREF(puSrc1);

    return   iemAImpl_rcp_worker(&pResult->ar32[0], uMxCsrIn, &puSrc2->ar32[0])
           | iemAImpl_rcp_worker(&pResult->ar32[1], uMxCsrIn, &puSrc2->ar32[1])
           | iemAImpl_rcp_worker(&pResult->ar32[2], uMxCsrIn, &puSrc2->ar32[2])
           | iemAImpl_rcp_worker(&pResult->ar32[3], uMxCsrIn, &puSrc2->ar32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vrcpps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc))
{
    return   iemAImpl_rcp_worker(&pResult->ar32[0], uMxCsrIn, &puSrc->ar32[0])
           | iemAImpl_rcp_worker(&pResult->ar32[1], uMxCsrIn, &puSrc->ar32[1])
           | iemAImpl_rcp_worker(&pResult->ar32[2], uMxCsrIn, &puSrc->ar32[2])
           | iemAImpl_rcp_worker(&pResult->ar32[3], uMxCsrIn, &puSrc->ar32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vrcpps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc))
{
    return   iemAImpl_rcp_worker(&pResult->ar32[0], uMxCsrIn, &puSrc->ar32[0])
           | iemAImpl_rcp_worker(&pResult->ar32[1], uMxCsrIn, &puSrc->ar32[1])
           | iemAImpl_rcp_worker(&pResult->ar32[2], uMxCsrIn, &puSrc->ar32[2])
           | iemAImpl_rcp_worker(&pResult->ar32[3], uMxCsrIn, &puSrc->ar32[3])
           | iemAImpl_rcp_worker(&pResult->ar32[4], uMxCsrIn, &puSrc->ar32[4])
           | iemAImpl_rcp_worker(&pResult->ar32[5], uMxCsrIn, &puSrc->ar32[5])
           | iemAImpl_rcp_worker(&pResult->ar32[6], uMxCsrIn, &puSrc->ar32[6])
           | iemAImpl_rcp_worker(&pResult->ar32[7], uMxCsrIn, &puSrc->ar32[7]);
}


/**
 * [V]RCPSS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_rcpss_u128_r32,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_rcp_worker(&pResult->ar32[0], uMxCsrIn, pr32Src2);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vrcpss_u128_r32_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCRTFLOAT32U pr32Src2))
{
    pResult->ar32[1] = puSrc1->ar32[1];
    pResult->ar32[2] = puSrc1->ar32[2];
    pResult->ar32[3] = puSrc1->ar32[3];
    return iemAImpl_rcp_worker(&pResult->ar32[0], uMxCsrIn, pr32Src2);
}


/**
 * [V]ADDSUBPS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_addsubps_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_subps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_addps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_subps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_addps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vaddsubps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_subps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_addps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_subps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_addps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vaddsubps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_subps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc1->ar32[0], &puSrc2->ar32[0])
           | iemAImpl_addps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc1->ar32[1], &puSrc2->ar32[1])
           | iemAImpl_subps_u128_worker(&pResult->ar32[2], uMxCsrIn, &puSrc1->ar32[2], &puSrc2->ar32[2])
           | iemAImpl_addps_u128_worker(&pResult->ar32[3], uMxCsrIn, &puSrc1->ar32[3], &puSrc2->ar32[3])
           | iemAImpl_subps_u128_worker(&pResult->ar32[4], uMxCsrIn, &puSrc1->ar32[4], &puSrc2->ar32[4])
           | iemAImpl_addps_u128_worker(&pResult->ar32[5], uMxCsrIn, &puSrc1->ar32[5], &puSrc2->ar32[5])
           | iemAImpl_subps_u128_worker(&pResult->ar32[6], uMxCsrIn, &puSrc1->ar32[6], &puSrc2->ar32[6])
           | iemAImpl_addps_u128_worker(&pResult->ar32[7], uMxCsrIn, &puSrc1->ar32[7], &puSrc2->ar32[7]);
}


/**
 * [V]ADDSUBPD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_addsubpd_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_subpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_addpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vaddsubpd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    return   iemAImpl_subpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_addpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vaddsubpd_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG pResult, PCX86YMMREG puSrc1, PCX86YMMREG puSrc2))
{
    return   iemAImpl_subpd_u128_worker(&pResult->ar64[0], uMxCsrIn, &puSrc1->ar64[0], &puSrc2->ar64[0])
           | iemAImpl_addpd_u128_worker(&pResult->ar64[1], uMxCsrIn, &puSrc1->ar64[1], &puSrc2->ar64[1])
           | iemAImpl_subpd_u128_worker(&pResult->ar64[2], uMxCsrIn, &puSrc1->ar64[2], &puSrc2->ar64[2])
           | iemAImpl_addpd_u128_worker(&pResult->ar64[3], uMxCsrIn, &puSrc1->ar64[3], &puSrc2->ar64[3]);
}


/**
 * [V]CVTPD2PS
 */
static uint32_t iemAImpl_cvtpd2ps_u128_worker(PRTFLOAT32U pr32Res, uint32_t fMxcsr, PCRTFLOAT64U pr64Val1)
{
    RTFLOAT64U r64Src1;
    fMxcsr |= iemSsePrepareValueR64(&r64Src1, fMxcsr, pr64Val1);

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float32_t r32Result = f64_to_f32(iemFpSoftF64FromIprt(&r64Src1), &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Result, pr32Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtpd2ps_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    RT_NOREF(puSrc1);

    pResult->au32[2] = 0;
    pResult->au32[3] = 0;
    return   iemAImpl_cvtpd2ps_u128_worker(&pResult->ar32[0], uMxCsrIn, &puSrc2->ar64[0])
           | iemAImpl_cvtpd2ps_u128_worker(&pResult->ar32[1], uMxCsrIn, &puSrc2->ar64[1]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtpd2ps_u128_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc))
{
    uint32_t uMxCsrOut =   iemAImpl_cvtpd2ps_u128_worker(&puDst->ar32[0], uMxCsrIn, &puSrc->ar64[0])
                         | iemAImpl_cvtpd2ps_u128_worker(&puDst->ar32[1], uMxCsrIn, &puSrc->ar64[1]);
    puDst->au64[1] = 0;
    return uMxCsrOut;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtpd2ps_u128_u256_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86YMMREG puSrc))
{
    return   iemAImpl_cvtpd2ps_u128_worker(&puDst->ar32[0], uMxCsrIn, &puSrc->ar64[0])
           | iemAImpl_cvtpd2ps_u128_worker(&puDst->ar32[1], uMxCsrIn, &puSrc->ar64[1])
           | iemAImpl_cvtpd2ps_u128_worker(&puDst->ar32[2], uMxCsrIn, &puSrc->ar64[2])
           | iemAImpl_cvtpd2ps_u128_worker(&puDst->ar32[3], uMxCsrIn, &puSrc->ar64[3]);
}


/**
 * [V]CVTPS2PD
 */
static uint32_t iemAImpl_cvtps2pd_u128_worker(PRTFLOAT64U pr64Res, uint32_t fMxcsr, uint32_t u32SrcIn)
{
    RTFLOAT32U r32SrcConverted;
    RTFLOAT32U r32SrcIn;
    r32SrcIn.u = u32SrcIn;
    fMxcsr |= iemSsePrepareValueR32(&r32SrcConverted, fMxcsr, &r32SrcIn);

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float64_t r64Result = f32_to_f64(iemFpSoftF32FromIprt(&r32SrcConverted), &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResult(&SoftState, r64Result, pr64Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtps2pd_u128,(uint32_t fMxCsrIn, PX86XMMREG pResult, uint64_t const *pu64Src))
{
    uint64_t const u64Src = *pu64Src;
    return   iemAImpl_cvtps2pd_u128_worker(&pResult->ar64[0], fMxCsrIn, RT_LO_U32(u64Src))
           | iemAImpl_cvtps2pd_u128_worker(&pResult->ar64[1], fMxCsrIn, RT_HI_U32(u64Src));
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtps2pd_u128_u64_fallback,(uint32_t fMxCsrIn, PX86XMMREG puDst, uint64_t const *pu64Src))
{
    uint64_t const u64Src = *pu64Src;
    return   iemAImpl_cvtps2pd_u128_worker(&puDst->ar64[0], fMxCsrIn, RT_LO_U32(u64Src))
           | iemAImpl_cvtps2pd_u128_worker(&puDst->ar64[1], fMxCsrIn, RT_HI_U32(u64Src));
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtps2pd_u256_u128_fallback,(uint32_t fMxCsrIn, PX86YMMREG puDst, PCX86XMMREG puSrc))
{
    return   iemAImpl_cvtps2pd_u128_worker(&puDst->ar64[0], fMxCsrIn, puSrc->au32[0])
           | iemAImpl_cvtps2pd_u128_worker(&puDst->ar64[1], fMxCsrIn, puSrc->au32[1])
           | iemAImpl_cvtps2pd_u128_worker(&puDst->ar64[2], fMxCsrIn, puSrc->au32[2])
           | iemAImpl_cvtps2pd_u128_worker(&puDst->ar64[3], fMxCsrIn, puSrc->au32[3]);
}


/**
 * [V]CVTDQ2PS
 */
static uint32_t iemAImpl_cvtdq2ps_u128_worker(PRTFLOAT32U pr32Res, uint32_t fMxcsr, int32_t i32Val)
{
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float32_t r32Result = i32_to_f32(i32Val, &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Result, pr32Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtdq2ps_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    RT_NOREF(puSrc1);

    return   iemAImpl_cvtdq2ps_u128_worker(&pResult->ar32[0], uMxCsrIn, puSrc2->ai32[0])
           | iemAImpl_cvtdq2ps_u128_worker(&pResult->ar32[1], uMxCsrIn, puSrc2->ai32[1])
           | iemAImpl_cvtdq2ps_u128_worker(&pResult->ar32[2], uMxCsrIn, puSrc2->ai32[2])
           | iemAImpl_cvtdq2ps_u128_worker(&pResult->ar32[3], uMxCsrIn, puSrc2->ai32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtdq2ps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc))
{
    return   iemAImpl_cvtdq2ps_u128_worker(&puDst->ar32[0], uMxCsrIn, puSrc->ai32[0])
           | iemAImpl_cvtdq2ps_u128_worker(&puDst->ar32[1], uMxCsrIn, puSrc->ai32[1])
           | iemAImpl_cvtdq2ps_u128_worker(&puDst->ar32[2], uMxCsrIn, puSrc->ai32[2])
           | iemAImpl_cvtdq2ps_u128_worker(&puDst->ar32[3], uMxCsrIn, puSrc->ai32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtdq2ps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCX86YMMREG puSrc))
{
    return   iemAImpl_cvtdq2ps_u128_worker(&puDst->ar32[0], uMxCsrIn, puSrc->ai32[0])
           | iemAImpl_cvtdq2ps_u128_worker(&puDst->ar32[1], uMxCsrIn, puSrc->ai32[1])
           | iemAImpl_cvtdq2ps_u128_worker(&puDst->ar32[2], uMxCsrIn, puSrc->ai32[2])
           | iemAImpl_cvtdq2ps_u128_worker(&puDst->ar32[3], uMxCsrIn, puSrc->ai32[3])
           | iemAImpl_cvtdq2ps_u128_worker(&puDst->ar32[4], uMxCsrIn, puSrc->ai32[4])
           | iemAImpl_cvtdq2ps_u128_worker(&puDst->ar32[5], uMxCsrIn, puSrc->ai32[5])
           | iemAImpl_cvtdq2ps_u128_worker(&puDst->ar32[6], uMxCsrIn, puSrc->ai32[6])
           | iemAImpl_cvtdq2ps_u128_worker(&puDst->ar32[7], uMxCsrIn, puSrc->ai32[7]);
}


/**
 * [V]CVTPS2DQ
 */
static uint32_t iemAImpl_cvtps2dq_u128_worker(int32_t *pi32Res, uint32_t fMxcsr, PCRTFLOAT32U pr32Src)
{
    RTFLOAT32U r32Src;
    iemSsePrepareValueR32(&r32Src, fMxcsr, pr32Src); /* De-normal seems to be ignored. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    *pi32Res = f32_to_i32(iemFpSoftF32FromIprt(&r32Src), SoftState.roundingMode, true /*exact*/, &SoftState);
    return fMxcsr | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtps2dq_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    RT_NOREF(puSrc1);

    return   iemAImpl_cvtps2dq_u128_worker(&pResult->ai32[0], uMxCsrIn, &puSrc2->ar32[0])
           | iemAImpl_cvtps2dq_u128_worker(&pResult->ai32[1], uMxCsrIn, &puSrc2->ar32[1])
           | iemAImpl_cvtps2dq_u128_worker(&pResult->ai32[2], uMxCsrIn, &puSrc2->ar32[2])
           | iemAImpl_cvtps2dq_u128_worker(&pResult->ai32[3], uMxCsrIn, &puSrc2->ar32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtps2dq_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc))
{
    return   iemAImpl_cvtps2dq_u128_worker(&puDst->ai32[0], uMxCsrIn, &puSrc->ar32[0])
           | iemAImpl_cvtps2dq_u128_worker(&puDst->ai32[1], uMxCsrIn, &puSrc->ar32[1])
           | iemAImpl_cvtps2dq_u128_worker(&puDst->ai32[2], uMxCsrIn, &puSrc->ar32[2])
           | iemAImpl_cvtps2dq_u128_worker(&puDst->ai32[3], uMxCsrIn, &puSrc->ar32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtps2dq_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCX86YMMREG puSrc))
{
    return   iemAImpl_cvtps2dq_u128_worker(&puDst->ai32[0], uMxCsrIn, &puSrc->ar32[0])
           | iemAImpl_cvtps2dq_u128_worker(&puDst->ai32[1], uMxCsrIn, &puSrc->ar32[1])
           | iemAImpl_cvtps2dq_u128_worker(&puDst->ai32[2], uMxCsrIn, &puSrc->ar32[2])
           | iemAImpl_cvtps2dq_u128_worker(&puDst->ai32[3], uMxCsrIn, &puSrc->ar32[3])
           | iemAImpl_cvtps2dq_u128_worker(&puDst->ai32[4], uMxCsrIn, &puSrc->ar32[4])
           | iemAImpl_cvtps2dq_u128_worker(&puDst->ai32[5], uMxCsrIn, &puSrc->ar32[5])
           | iemAImpl_cvtps2dq_u128_worker(&puDst->ai32[6], uMxCsrIn, &puSrc->ar32[6])
           | iemAImpl_cvtps2dq_u128_worker(&puDst->ai32[7], uMxCsrIn, &puSrc->ar32[7]);
}


/**
 * [V]CVTTPS2DQ
 */
static uint32_t iemAImpl_cvttps2dq_u128_worker(int32_t *pi32Res, uint32_t fMxcsr, PCRTFLOAT32U pr32Src)
{
    RTFLOAT32U r32Src;
    iemSsePrepareValueR32(&r32Src, fMxcsr, pr32Src); /* De-normal seems to be ignored. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    SoftState.roundingMode = softfloat_round_minMag;
    *pi32Res = f32_to_i32_r_minMag(iemFpSoftF32FromIprt(&r32Src), true /*exact*/, &SoftState);
    return fMxcsr | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvttps2dq_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    RT_NOREF(puSrc1);

    return   iemAImpl_cvttps2dq_u128_worker(&pResult->ai32[0], uMxCsrIn, &puSrc2->ar32[0])
           | iemAImpl_cvttps2dq_u128_worker(&pResult->ai32[1], uMxCsrIn, &puSrc2->ar32[1])
           | iemAImpl_cvttps2dq_u128_worker(&pResult->ai32[2], uMxCsrIn, &puSrc2->ar32[2])
           | iemAImpl_cvttps2dq_u128_worker(&pResult->ai32[3], uMxCsrIn, &puSrc2->ar32[3]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvttps2dq_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc))
{
    return   iemAImpl_cvttps2dq_u128_worker(&puDst->ai32[0], uMxCsrIn, &puSrc->ar32[0])
           | iemAImpl_cvttps2dq_u128_worker(&puDst->ai32[1], uMxCsrIn, &puSrc->ar32[1])
           | iemAImpl_cvttps2dq_u128_worker(&puDst->ai32[2], uMxCsrIn, &puSrc->ar32[2])
           | iemAImpl_cvttps2dq_u128_worker(&puDst->ai32[3], uMxCsrIn, &puSrc->ar32[3]);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvttps2dq_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCX86YMMREG puSrc))
{
    return   iemAImpl_cvttps2dq_u128_worker(&puDst->ai32[0], uMxCsrIn, &puSrc->ar32[0])
           | iemAImpl_cvttps2dq_u128_worker(&puDst->ai32[1], uMxCsrIn, &puSrc->ar32[1])
           | iemAImpl_cvttps2dq_u128_worker(&puDst->ai32[2], uMxCsrIn, &puSrc->ar32[2])
           | iemAImpl_cvttps2dq_u128_worker(&puDst->ai32[3], uMxCsrIn, &puSrc->ar32[3])
           | iemAImpl_cvttps2dq_u128_worker(&puDst->ai32[4], uMxCsrIn, &puSrc->ar32[4])
           | iemAImpl_cvttps2dq_u128_worker(&puDst->ai32[5], uMxCsrIn, &puSrc->ar32[5])
           | iemAImpl_cvttps2dq_u128_worker(&puDst->ai32[6], uMxCsrIn, &puSrc->ar32[6])
           | iemAImpl_cvttps2dq_u128_worker(&puDst->ai32[7], uMxCsrIn, &puSrc->ar32[7]);
}


/**
 * [V]CVTTPD2DQ
 */
static uint32_t iemAImpl_cvttpd2dq_u128_worker(int32_t *pi32Res, uint32_t fMxcsr, PCRTFLOAT64U pr64Src)
{
    RTFLOAT64U r64Src;
    iemSsePrepareValueR64(&r64Src, fMxcsr, pr64Src); /* De-normal seems to be ignored. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    SoftState.roundingMode = softfloat_round_minMag;
    *pi32Res = f64_to_i32(iemFpSoftF64FromIprt(&r64Src), SoftState.roundingMode, true /*exact*/, &SoftState);
    return fMxcsr | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}

#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvttpd2dq_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    RT_NOREF(puSrc1);

    pResult->au64[1] = 0;
    return   iemAImpl_cvttpd2dq_u128_worker(&pResult->ai32[0], uMxCsrIn, &puSrc2->ar64[0])
           | iemAImpl_cvttpd2dq_u128_worker(&pResult->ai32[1], uMxCsrIn, &puSrc2->ar64[1]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvttpd2dq_u128_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc))
{
    uint32_t uMxCsrOut =   iemAImpl_cvttpd2dq_u128_worker(&puDst->ai32[0], uMxCsrIn, &puSrc->ar64[0])
                         | iemAImpl_cvttpd2dq_u128_worker(&puDst->ai32[1], uMxCsrIn, &puSrc->ar64[1]);
    puDst->au64[1] = 0;
    return uMxCsrOut;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvttpd2dq_u128_u256_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86YMMREG puSrc))
{
    return   iemAImpl_cvttpd2dq_u128_worker(&puDst->ai32[0], uMxCsrIn, &puSrc->ar64[0])
           | iemAImpl_cvttpd2dq_u128_worker(&puDst->ai32[1], uMxCsrIn, &puSrc->ar64[1])
           | iemAImpl_cvttpd2dq_u128_worker(&puDst->ai32[2], uMxCsrIn, &puSrc->ar64[2])
           | iemAImpl_cvttpd2dq_u128_worker(&puDst->ai32[3], uMxCsrIn, &puSrc->ar64[3]);
}


/**
 * [V]CVTDQ2PD
 */
static uint32_t iemAImpl_cvtdq2pd_u128_worker(PRTFLOAT64U pr64Res, uint32_t fMxcsr, int32_t i32Val)
{
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float64_t r64Result = i32_to_f64(i32Val, &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResult(&SoftState, r64Result, pr64Res, fMxcsr);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtdq2pd_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    RT_NOREF(puSrc1);

    return   iemAImpl_cvtdq2pd_u128_worker(&pResult->ar64[0], uMxCsrIn, puSrc2->ai32[0])
           | iemAImpl_cvtdq2pd_u128_worker(&pResult->ar64[1], uMxCsrIn, puSrc2->ai32[1]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtdq2pd_u128_u64_fallback,(uint32_t fMxCsrIn, PX86XMMREG puDst, uint64_t const *pu64Src))
{
    uint64_t const u64Src = *pu64Src;
    return   iemAImpl_cvtdq2pd_u128_worker(&puDst->ar64[0], fMxCsrIn, RT_LO_U32(u64Src))
           | iemAImpl_cvtdq2pd_u128_worker(&puDst->ar64[1], fMxCsrIn, RT_HI_U32(u64Src));
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtdq2pd_u256_u128_fallback,(uint32_t fMxCsrIn, PX86YMMREG puDst, PCX86XMMREG puSrc))
{
    return   iemAImpl_cvtdq2pd_u128_worker(&puDst->ar64[0], fMxCsrIn, puSrc->au32[0])
           | iemAImpl_cvtdq2pd_u128_worker(&puDst->ar64[1], fMxCsrIn, puSrc->au32[1])
           | iemAImpl_cvtdq2pd_u128_worker(&puDst->ar64[2], fMxCsrIn, puSrc->au32[2])
           | iemAImpl_cvtdq2pd_u128_worker(&puDst->ar64[3], fMxCsrIn, puSrc->au32[3]);
}


/**
 * CVTPD2DQ
 */
static uint32_t iemAImpl_cvtpd2dq_u128_worker(int32_t *pi32Res, uint32_t fMxcsr, PCRTFLOAT64U pr64Src)
{
    RTFLOAT64U r64Src;
    iemSsePrepareValueR64(&r64Src, fMxcsr, pr64Src); /* De-normal seems to be ignored. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    *pi32Res = f64_to_i32(iemFpSoftF64FromIprt(&r64Src), SoftState.roundingMode, true /*exact*/, &SoftState);
    return fMxcsr | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtpd2dq_u128,(uint32_t uMxCsrIn, PX86XMMREG pResult, PCX86XMMREG puSrc1, PCX86XMMREG puSrc2))
{
    RT_NOREF(puSrc1);

    pResult->au64[1] = 0;
    return   iemAImpl_cvtpd2dq_u128_worker(&pResult->ai32[0], uMxCsrIn, &puSrc2->ar64[0])
           | iemAImpl_cvtpd2dq_u128_worker(&pResult->ai32[1], uMxCsrIn, &puSrc2->ar64[1]);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtpd2dq_u128_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc))
{
    uint32_t uMxCsrOut =   iemAImpl_cvtpd2dq_u128_worker(&puDst->ai32[0], uMxCsrIn, &puSrc->ar64[0])
                         | iemAImpl_cvtpd2dq_u128_worker(&puDst->ai32[1], uMxCsrIn, &puSrc->ar64[1]);
    puDst->au64[1] = 0;
    return uMxCsrOut;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtpd2dq_u128_u256_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86YMMREG puSrc))
{
    return   iemAImpl_cvtpd2dq_u128_worker(&puDst->ai32[0], uMxCsrIn, &puSrc->ar64[0])
           | iemAImpl_cvtpd2dq_u128_worker(&puDst->ai32[1], uMxCsrIn, &puSrc->ar64[1])
           | iemAImpl_cvtpd2dq_u128_worker(&puDst->ai32[2], uMxCsrIn, &puSrc->ar64[2])
           | iemAImpl_cvtpd2dq_u128_worker(&puDst->ai32[3], uMxCsrIn, &puSrc->ar64[3]);
}


/**
 * [V]SHUFPS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_shufps_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    RTUINT128U const uSrc1 = *puDst;
    RTUINT128U const uSrc2 = *puSrc;
    ASMCompilerBarrier();
    puDst->au32[0] = uSrc1.au32[bEvil & 0x3];
    puDst->au32[1] = uSrc1.au32[(bEvil >> 2) & 0x3];
    puDst->au32[2] = uSrc2.au32[(bEvil >> 4) & 0x3];
    puDst->au32[3] = uSrc2.au32[(bEvil >> 6) & 0x3];
}
#endif


IEM_DECL_IMPL_DEF(void, iemAImpl_vshufps_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bEvil))
{
    RTUINT128U const uSrc1 = *puSrc1;
    RTUINT128U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();
    puDst->au32[0] = uSrc1.au32[bEvil & 0x3];
    puDst->au32[1] = uSrc1.au32[(bEvil >> 2) & 0x3];
    puDst->au32[2] = uSrc2.au32[(bEvil >> 4) & 0x3];
    puDst->au32[3] = uSrc2.au32[(bEvil >> 6) & 0x3];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vshufps_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bEvil))
{
    RTUINT256U const uSrc1 = *puSrc1;
    RTUINT256U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();
    puDst->au32[0] = uSrc1.au32[bEvil & 0x3];
    puDst->au32[1] = uSrc1.au32[(bEvil >> 2) & 0x3];
    puDst->au32[2] = uSrc2.au32[(bEvil >> 4) & 0x3];
    puDst->au32[3] = uSrc2.au32[(bEvil >> 6) & 0x3];

    puDst->au32[4] = uSrc1.au32[4 + (bEvil & 0x3)];
    puDst->au32[5] = uSrc1.au32[4 + ((bEvil >> 2) & 0x3)];
    puDst->au32[6] = uSrc2.au32[4 + ((bEvil >> 4) & 0x3)];
    puDst->au32[7] = uSrc2.au32[4 + ((bEvil >> 6) & 0x3)];
}


/**
 * [V]SHUFPD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_shufpd_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    RTUINT128U const uSrc1 = *puDst;
    RTUINT128U const uSrc2 = *puSrc;
    ASMCompilerBarrier();
    puDst->au64[0] = (bEvil & RT_BIT(0)) ? uSrc1.au64[1] : uSrc1.au64[0];
    puDst->au64[1] = (bEvil & RT_BIT(1)) ? uSrc2.au64[1] : uSrc2.au64[0];
}
#endif


IEM_DECL_IMPL_DEF(void, iemAImpl_vshufpd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bEvil))
{
    RTUINT128U const uSrc1 = *puSrc1;
    RTUINT128U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();
    puDst->au64[0] = (bEvil & RT_BIT(0)) ? uSrc1.au64[1] : uSrc1.au64[0];
    puDst->au64[1] = (bEvil & RT_BIT(1)) ? uSrc2.au64[1] : uSrc2.au64[0];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vshufpd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bEvil))
{
    RTUINT256U const uSrc1 = *puSrc1;
    RTUINT256U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();
    puDst->au64[0] = (bEvil & RT_BIT(0)) ? uSrc1.au64[1] : uSrc1.au64[0];
    puDst->au64[1] = (bEvil & RT_BIT(1)) ? uSrc2.au64[1] : uSrc2.au64[0];
    puDst->au64[2] = (bEvil & RT_BIT(2)) ? uSrc1.au64[3] : uSrc1.au64[2];
    puDst->au64[3] = (bEvil & RT_BIT(3)) ? uSrc2.au64[3] : uSrc2.au64[2];
}


/*
 * PHMINPOSUW / VPHMINPOSUW
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_phminposuw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    uint16_t u16Min = puSrc->au16[0];
    uint8_t  idxMin = 0;

    for (uint8_t i = 1; i < RT_ELEMENTS(puSrc->au16); i++)
        if (puSrc->au16[i] < u16Min)
        {
            u16Min = puSrc->au16[i];
            idxMin = i;
        }

    puDst->au64[0] = 0;
    puDst->au64[1] = 0;
    puDst->au16[0] = u16Min;
    puDst->au16[1] = idxMin;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vphminposuw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    iemAImpl_phminposuw_u128_fallback(puDst, puSrc);
}


/**
 * VPERMILPS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilps_imm_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    RTUINT128U const uSrc = *puSrc;
    ASMCompilerBarrier();

    puDst->au32[0] = uSrc.au32[bEvil & 0x3];
    puDst->au32[1] = uSrc.au32[(bEvil >> 2) & 0x3];
    puDst->au32[2] = uSrc.au32[(bEvil >> 4) & 0x3];
    puDst->au32[3] = uSrc.au32[(bEvil >> 6) & 0x3];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilps_imm_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t bEvil))
{
    RTUINT256U const uSrc = *puSrc;
    ASMCompilerBarrier();

    puDst->au32[0] = uSrc.au32[bEvil & 0x3];
    puDst->au32[1] = uSrc.au32[(bEvil >> 2) & 0x3];
    puDst->au32[2] = uSrc.au32[(bEvil >> 4) & 0x3];
    puDst->au32[3] = uSrc.au32[(bEvil >> 6) & 0x3];

    puDst->au32[4] = uSrc.au32[4 + (bEvil & 0x3)];
    puDst->au32[5] = uSrc.au32[4 + ((bEvil >> 2) & 0x3)];
    puDst->au32[6] = uSrc.au32[4 + ((bEvil >> 4) & 0x3)];
    puDst->au32[7] = uSrc.au32[4 + ((bEvil >> 6) & 0x3)];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilps_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc1 = *puSrc1;
    RTUINT128U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();

    puDst->au32[0] = uSrc1.au32[uSrc2.au8[0] & 0x3];
    puDst->au32[1] = uSrc1.au32[uSrc2.au8[4] & 0x3];
    puDst->au32[2] = uSrc1.au32[uSrc2.au8[8] & 0x3];
    puDst->au32[3] = uSrc1.au32[uSrc2.au8[12] & 0x3];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilps_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc1 = *puSrc1;
    RTUINT256U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();

    puDst->au32[0] = uSrc1.au32[uSrc2.au8[0] & 0x3];
    puDst->au32[1] = uSrc1.au32[uSrc2.au8[4] & 0x3];
    puDst->au32[2] = uSrc1.au32[uSrc2.au8[8] & 0x3];
    puDst->au32[3] = uSrc1.au32[uSrc2.au8[12] & 0x3];

    puDst->au32[4] = uSrc1.au32[4 + (uSrc2.au8[16] & 0x3)];
    puDst->au32[5] = uSrc1.au32[4 + (uSrc2.au8[20] & 0x3)];
    puDst->au32[6] = uSrc1.au32[4 + (uSrc2.au8[24] & 0x3)];
    puDst->au32[7] = uSrc1.au32[4 + (uSrc2.au8[28] & 0x3)];
}
#endif


IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilps_imm_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    RTUINT128U const uSrc = *puSrc;
    ASMCompilerBarrier();

    puDst->au32[0] = uSrc.au32[bEvil & 0x3];
    puDst->au32[1] = uSrc.au32[(bEvil >> 2) & 0x3];
    puDst->au32[2] = uSrc.au32[(bEvil >> 4) & 0x3];
    puDst->au32[3] = uSrc.au32[(bEvil >> 6) & 0x3];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilps_imm_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t bEvil))
{
    RTUINT256U const uSrc = *puSrc;
    ASMCompilerBarrier();

    puDst->au32[0] = uSrc.au32[bEvil & 0x3];
    puDst->au32[1] = uSrc.au32[(bEvil >> 2) & 0x3];
    puDst->au32[2] = uSrc.au32[(bEvil >> 4) & 0x3];
    puDst->au32[3] = uSrc.au32[(bEvil >> 6) & 0x3];

    puDst->au32[4] = uSrc.au32[4 + (bEvil & 0x3)];
    puDst->au32[5] = uSrc.au32[4 + ((bEvil >> 2) & 0x3)];
    puDst->au32[6] = uSrc.au32[4 + ((bEvil >> 4) & 0x3)];
    puDst->au32[7] = uSrc.au32[4 + ((bEvil >> 6) & 0x3)];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilps_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc1 = *puSrc1;
    RTUINT128U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();

    puDst->au32[0] = uSrc1.au32[uSrc2.au8[0] & 0x3];
    puDst->au32[1] = uSrc1.au32[uSrc2.au8[4] & 0x3];
    puDst->au32[2] = uSrc1.au32[uSrc2.au8[8] & 0x3];
    puDst->au32[3] = uSrc1.au32[uSrc2.au8[12] & 0x3];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilps_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc1 = *puSrc1;
    RTUINT256U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();

    puDst->au32[0] = uSrc1.au32[uSrc2.au8[0] & 0x3];
    puDst->au32[1] = uSrc1.au32[uSrc2.au8[4] & 0x3];
    puDst->au32[2] = uSrc1.au32[uSrc2.au8[8] & 0x3];
    puDst->au32[3] = uSrc1.au32[uSrc2.au8[12] & 0x3];

    puDst->au32[4] = uSrc1.au32[4 + (uSrc2.au8[16] & 0x3)];
    puDst->au32[5] = uSrc1.au32[4 + (uSrc2.au8[20] & 0x3)];
    puDst->au32[6] = uSrc1.au32[4 + (uSrc2.au8[24] & 0x3)];
    puDst->au32[7] = uSrc1.au32[4 + (uSrc2.au8[28] & 0x3)];
}


/**
 * VPERMILPD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilpd_imm_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    RTUINT128U const uSrc = *puSrc;
    ASMCompilerBarrier();

    puDst->au64[0] = uSrc.au64[bEvil & 0x1];
    puDst->au64[1] = uSrc.au64[(bEvil >> 1) & 0x1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilpd_imm_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t bEvil))
{
    RTUINT256U const uSrc = *puSrc;
    ASMCompilerBarrier();

    puDst->au64[0] = uSrc.au64[bEvil & 0x1];
    puDst->au64[1] = uSrc.au64[(bEvil >> 1) & 0x1];

    puDst->au64[2] = uSrc.au64[2 + ((bEvil >> 2) & 0x1)];
    puDst->au64[3] = uSrc.au64[2 + ((bEvil >> 3) & 0x1)];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilpd_u128,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc1 = *puSrc1;
    RTUINT128U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();

    puDst->au64[0] = uSrc1.au64[(uSrc2.au8[0] & 0x2) >> 1];
    puDst->au64[1] = uSrc1.au64[(uSrc2.au8[8] & 0x2) >> 1];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilpd_u256,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc1 = *puSrc1;
    RTUINT256U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();

    puDst->au64[0] = uSrc1.au64[(uSrc2.au8[0] & 0x2) >> 1];
    puDst->au64[1] = uSrc1.au64[(uSrc2.au8[8] & 0x2) >> 1];

    puDst->au64[2] = uSrc1.au64[2 + ((uSrc2.au8[16] & 0x2) >> 1)];
    puDst->au64[3] = uSrc1.au64[2 + ((uSrc2.au8[24] & 0x2) >> 1)];
}
#endif


IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilpd_imm_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    RTUINT128U const uSrc = *puSrc;
    ASMCompilerBarrier();

    puDst->au64[0] = uSrc.au64[bEvil & 0x1];
    puDst->au64[1] = uSrc.au64[(bEvil >> 1) & 0x1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilpd_imm_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t bEvil))
{
    RTUINT256U const uSrc = *puSrc;
    ASMCompilerBarrier();

    puDst->au64[0] = uSrc.au64[bEvil & 0x1];
    puDst->au64[1] = uSrc.au64[(bEvil >> 1) & 0x1];

    puDst->au64[2] = uSrc.au64[2 + ((bEvil >> 2) & 0x1)];
    puDst->au64[3] = uSrc.au64[2 + ((bEvil >> 3) & 0x1)];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilpd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U const uSrc1 = *puSrc1;
    RTUINT128U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();

    puDst->au64[0] = uSrc1.au64[(uSrc2.au8[0] & 0x2) >> 1];
    puDst->au64[1] = uSrc1.au64[(uSrc2.au8[8] & 0x2) >> 1];
}

IEM_DECL_IMPL_DEF(void, iemAImpl_vpermilpd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    RTUINT256U const uSrc1 = *puSrc1;
    RTUINT256U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();

    puDst->au64[0] = uSrc1.au64[(uSrc2.au8[0] & 0x2) >> 1];
    puDst->au64[1] = uSrc1.au64[(uSrc2.au8[8] & 0x2) >> 1];

    puDst->au64[2] = uSrc1.au64[2 + ((uSrc2.au8[16] & 0x2) >> 1)];
    puDst->au64[3] = uSrc1.au64[2 + ((uSrc2.au8[24] & 0x2) >> 1)];
}


/*
 * [V]PBLENDVB
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_pblendvb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, PCRTUINT128U puMask))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au8); i++)
        if (puMask->au8[i] & RT_BIT(7))
            puDst->au8[i] = puSrc->au8[i];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpblendvb_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, PCRTUINT128U puMask))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au8); i++)
            puDst->au8[i] = puMask->au8[i] & RT_BIT(7) ? puSrc2->au8[i] : puSrc1->au8[i];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpblendvb_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, PCRTUINT256U puMask))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au8); i++)
            puDst->au8[i] = puMask->au8[i] & RT_BIT(7) ? puSrc2->au8[i] : puSrc1->au8[i];
}


/*
 * [V]BLENDVPS
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_blendvps_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, PCRTUINT128U puMask))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au32); i++)
        if (puMask->au32[i] & RT_BIT_32(31))
            puDst->au32[i] = puSrc->au32[i];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vblendvps_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, PCRTUINT128U puMask))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au32); i++)
            puDst->au32[i] = (puMask->au32[i] & RT_BIT_32(31)) ? puSrc2->au32[i] : puSrc1->au32[i];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vblendvps_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, PCRTUINT256U puMask))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au32); i++)
            puDst->au32[i] = (puMask->au32[i] & RT_BIT_32(31)) ? puSrc2->au32[i] : puSrc1->au32[i];
}


/*
 * [V]BLENDVPD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_blendvpd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, PCRTUINT128U puMask))
{
    if (puMask->au64[0] & RT_BIT_64(63)) puDst->au64[0] = puSrc->au64[0];
    if (puMask->au64[1] & RT_BIT_64(63)) puDst->au64[1] = puSrc->au64[1];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vblendvpd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, PCRTUINT128U puMask))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au64); i++)
            puDst->au64[i] = (puMask->au64[i] & RT_BIT_64(63)) ? puSrc2->au64[i] : puSrc1->au64[i];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vblendvpd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, PCRTUINT256U puMask))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au64); i++)
            puDst->au64[i] = (puMask->au64[i] & RT_BIT_64(63)) ? puSrc2->au64[i] : puSrc1->au64[i];
}


/**
 * [V]PALIGNR
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_palignr_u64_fallback,(uint64_t *pu64Dst, uint64_t u64Src2, uint8_t bEvil))
{
    uint64_t const u64Src1 = *pu64Dst;
    ASMCompilerBarrier();

    if (bEvil >= 16)
        *pu64Dst = 0;
    else if (bEvil >= 8)
        *pu64Dst = u64Src1 >> ((bEvil - 8) * 8);
    else
    {
        uint8_t cShift = bEvil * 8;
        *pu64Dst =   ((u64Src1 & (RT_BIT_64(cShift) - 1)) << ((8 - bEvil) * 8))
                   | (u64Src2 >> cShift);
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_palignr_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    RTUINT128U const uSrc1 = *puDst;
    RTUINT128U const uSrc2 = *puSrc;
    ASMCompilerBarrier();

    puDst->au64[0] = 0;
    puDst->au64[1] = 0;
    if (bEvil >= 32)
    { /* Everything stays 0. */ }
    else if (bEvil >= 16)
    {
        bEvil -= 16;
        for (uint8_t i = bEvil; i < RT_ELEMENTS(puDst->au8); i++)
            puDst->au8[i - bEvil] = uSrc1.au8[i];
    }
    else
    {
        for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au8) - bEvil; i++)
            puDst->au8[i] = uSrc2.au8[i + bEvil];
        for (uint8_t i = 0; i < bEvil; i++)
            puDst->au8[i + RT_ELEMENTS(puDst->au8) - bEvil] = uSrc1.au8[i];
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpalignr_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bEvil))
{
    RTUINT128U const uSrc1 = *puSrc1; /* Might overlap with destination. */
    RTUINT128U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();

    puDst->au64[0] = 0;
    puDst->au64[1] = 0;
    if (bEvil >= 32)
    { /* Everything stays 0. */ }
    else if (bEvil >= 16)
    {
        bEvil -= 16;
        for (uint8_t i = bEvil; i < RT_ELEMENTS(puDst->au8); i++)
            puDst->au8[i - bEvil] = uSrc1.au8[i];
    }
    else
    {
        for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au8) - bEvil; i++)
            puDst->au8[i] = uSrc2.au8[i + bEvil];
        for (uint8_t i = 0; i < bEvil; i++)
            puDst->au8[i + RT_ELEMENTS(puDst->au8) - bEvil] = uSrc1.au8[i];
    }
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpalignr_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bEvil))
{
    RTUINT256U const uSrc1 = *puSrc1; /* Might overlap with destination. */
    RTUINT256U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();

    iemAImpl_vpalignr_u128_fallback(&puDst->au128[0], &uSrc1.au128[0], &uSrc2.au128[0], bEvil);
    iemAImpl_vpalignr_u128_fallback(&puDst->au128[1], &uSrc1.au128[1], &uSrc2.au128[1], bEvil);
}


/**
 * [V]PBLENDW
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_pblendw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au16); i++)
        if (bEvil & RT_BIT(i))
            puDst->au16[i] = puSrc->au16[i];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpblendw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au16); i++)
        if (bEvil & RT_BIT(i))
            puDst->au16[i] = puSrc2->au16[i];
        else
            puDst->au16[i] = puSrc1->au16[i];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpblendw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bEvil))
{
    for (uint8_t i = 0; i < 8; i++)
        if (bEvil & RT_BIT(i))
        {
            puDst->au16[    i] = puSrc2->au16[    i];
            puDst->au16[8 + i] = puSrc2->au16[8 + i];
        }
        else
        {
            puDst->au16[    i] = puSrc1->au16[    i];
            puDst->au16[8 + i] = puSrc1->au16[8 + i];
        }
}


/**
 * [V]PBLENDD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpblendd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au32); i++)
        if (bEvil & RT_BIT(i))
            puDst->au32[i] = puSrc2->au32[i];
        else
            puDst->au32[i] = puSrc1->au32[i];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpblendd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au32); i++)
        if (bEvil & RT_BIT(i))
            puDst->au32[i] = puSrc2->au32[i];
        else
            puDst->au32[i] = puSrc1->au32[i];
}


/**
 * [V]BLENDPS
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_blendps_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au32); i++)
        if (bEvil & RT_BIT(i))
            puDst->au32[i] = puSrc->au32[i];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vblendps_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au32); i++)
        if (bEvil & RT_BIT(i))
            puDst->au32[i] = puSrc2->au32[i];
        else
            puDst->au32[i] = puSrc1->au32[i];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vblendps_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au32); i++)
        if (bEvil & RT_BIT(i))
            puDst->au32[i] = puSrc2->au32[i];
        else
            puDst->au32[i] = puSrc1->au32[i];
}


/**
 * [V]BLENDPD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_blendpd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au64); i++)
        if (bEvil & RT_BIT(i))
            puDst->au64[i] = puSrc->au64[i];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vblendpd_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au64); i++)
        if (bEvil & RT_BIT(i))
            puDst->au64[i] = puSrc2->au64[i];
        else
            puDst->au64[i] = puSrc1->au64[i];
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vblendpd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au64); i++)
        if (bEvil & RT_BIT(i))
            puDst->au64[i] = puSrc2->au64[i];
        else
            puDst->au64[i] = puSrc1->au64[i];
}


/**
 * AES tables and helper routines. Tables from Intel AES-NI whitepaper.
 */

static uint8_t iemAImpl_aes_sbox[] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

/* The InvS-Box lookup table. */
static uint8_t iemAImpl_aes_inv_sbox[] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

/* The ShiftRows lookup table. */
static uint8_t iemAImpl_aes_shift_rows_tbl[] = {
     0,  5, 10, 15,  4,  9, 14,  3,  8, 13,  2,  7, 12,  1,  6, 11
};

/* The InvShiftRows lookup table. */
static uint8_t iemAImpl_aes_inv_shift_rows_tbl[] = {
     0, 13, 10,  7,  4,  1, 14, 11,  8,  5,  2, 15, 12,  9,  6,  3
};

static inline RTUINT128U iemAImpl_aes_sub_bytes(PCRTUINT128U puSrc, uint8_t abSubst[256])
{
    RTUINT128U  uVal;
    int         i;

    for (i = 0; i < 16; ++i)
        uVal.au8[i] = abSubst[puSrc->au8[i]];

    return uVal;
}

static inline uint8_t iemAImpl_aes_xtime(uint8_t u)
{
    return (u << 1) ^ (((u >> 7) & 1) * 27);
}

static RTUINT128U iemAImpl_aes_mix_col(PCRTUINT128U puSrc)
{
    RTUINT128U  uVal;
    int         i;
    uint8_t     tmp;

    for (i = 0; i < 16; i += 4) {
        tmp = puSrc->au8[i+0] ^ puSrc->au8[i+1] ^ puSrc->au8[i+2] ^ puSrc->au8[i+3];
        uVal.au8[i+0] = puSrc->au8[i+0] ^ tmp ^ iemAImpl_aes_xtime(puSrc->au8[i+0] ^ puSrc->au8[i+1]);
        uVal.au8[i+1] = puSrc->au8[i+1] ^ tmp ^ iemAImpl_aes_xtime(puSrc->au8[i+1] ^ puSrc->au8[i+2]);
        uVal.au8[i+2] = puSrc->au8[i+2] ^ tmp ^ iemAImpl_aes_xtime(puSrc->au8[i+2] ^ puSrc->au8[i+3]);
        uVal.au8[i+3] = puSrc->au8[i+3] ^ tmp ^ iemAImpl_aes_xtime(puSrc->au8[i+3] ^ puSrc->au8[i+0]);
    }

    return uVal;
}

static inline RTUINT128U iemAImpl_aes_shift_rows(PCRTUINT128U puSrc, uint8_t abShift[16])
{
    RTUINT128U  uVal;
    int         i;

    for (i = 0; i < 16; ++i)
        uVal.au8[i] = puSrc->au8[abShift[i]];

    return uVal;
}

static uint8_t iemAImpl_aes_clmul(uint8_t a, uint8_t b)
{
    uint8_t     val;

    val  = ((b >> 0) & 1) * a;
    val ^= ((b >> 1) & 1) * iemAImpl_aes_xtime(a);
    val ^= ((b >> 2) & 1) * iemAImpl_aes_xtime(iemAImpl_aes_xtime(a));
    val ^= ((b >> 3) & 1) * iemAImpl_aes_xtime(iemAImpl_aes_xtime(iemAImpl_aes_xtime(a)));
    val ^= ((b >> 4) & 1) * iemAImpl_aes_xtime(iemAImpl_aes_xtime(iemAImpl_aes_xtime(iemAImpl_aes_xtime(a))));

    return val;
}

static RTUINT128U iemAImpl_aes_inv_mix_col(PCRTUINT128U puSrc)
{
    RTUINT128U  uVal;
    int         i;

    for (i = 0; i < 16; i += 4) {
        uVal.au8[i+0] = iemAImpl_aes_clmul(puSrc->au8[i+0], 0x0e) ^ iemAImpl_aes_clmul(puSrc->au8[i+1], 0x0b)^ iemAImpl_aes_clmul(puSrc->au8[i+2], 0x0d) ^ iemAImpl_aes_clmul(puSrc->au8[i+3], 0x09);
        uVal.au8[i+1] = iemAImpl_aes_clmul(puSrc->au8[i+0], 0x09) ^ iemAImpl_aes_clmul(puSrc->au8[i+1], 0x0e)^ iemAImpl_aes_clmul(puSrc->au8[i+2], 0x0b) ^ iemAImpl_aes_clmul(puSrc->au8[i+3], 0x0d);
        uVal.au8[i+2] = iemAImpl_aes_clmul(puSrc->au8[i+0], 0x0d) ^ iemAImpl_aes_clmul(puSrc->au8[i+1], 0x09)^ iemAImpl_aes_clmul(puSrc->au8[i+2], 0x0e) ^ iemAImpl_aes_clmul(puSrc->au8[i+3], 0x0b);
        uVal.au8[i+3] = iemAImpl_aes_clmul(puSrc->au8[i+0], 0x0b) ^ iemAImpl_aes_clmul(puSrc->au8[i+1], 0x0d)^ iemAImpl_aes_clmul(puSrc->au8[i+2], 0x09) ^ iemAImpl_aes_clmul(puSrc->au8[i+3], 0x0e);
    }

    return uVal;
}

static inline uint32_t iemAImpl_aes_sub_word(uint32_t w)
{
    RTUINT32U    uTmp;

    uTmp.au32[0] = w;
    uTmp.au8[0] = iemAImpl_aes_sbox[uTmp.au8[0]];
    uTmp.au8[1] = iemAImpl_aes_sbox[uTmp.au8[1]];
    uTmp.au8[2] = iemAImpl_aes_sbox[uTmp.au8[2]];
    uTmp.au8[3] = iemAImpl_aes_sbox[uTmp.au8[3]];

    return uTmp.au32[0];
}

static inline uint32_t iemAImpl_aes_rot_word(uint32_t w)
{
    return (w << 24) | (w >> 8);
}

/**
 * [V]AESKEYGENASSIST
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_aeskeygenassist_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bImm))
{
    RTUINT128U  uTmp;
    uint32_t    uRCon = bImm;   /* Round constant. */

    uTmp.au32[0] = iemAImpl_aes_sub_word(puSrc->au32[1]);   /* puSrc = KeyGen. */
    uTmp.au32[1] = iemAImpl_aes_rot_word(iemAImpl_aes_sub_word(puSrc->au32[1])) ^ uRCon;
    uTmp.au32[2] = iemAImpl_aes_sub_word(puSrc->au32[3]);
    uTmp.au32[3] = iemAImpl_aes_rot_word(iemAImpl_aes_sub_word(puSrc->au32[3])) ^ uRCon;

    *puDst = uTmp;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vaeskeygenassist_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bImm))
{
    iemAImpl_aeskeygenassist_u128_fallback(puDst, puSrc, bImm);
}


/**
 * [V]AESIMC
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_aesimc_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    *puDst = iemAImpl_aes_inv_mix_col(puSrc);   /* Src = Key. */
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vaesimc_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    *puDst = iemAImpl_aes_inv_mix_col(puSrc);   /* Src = Key. */
}


/**
 * [V]AESENC
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_aesenc_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U  uTmp;

    uTmp = iemAImpl_aes_shift_rows(puDst, iemAImpl_aes_shift_rows_tbl); /* Dst = state. */
    uTmp = iemAImpl_aes_sub_bytes(&uTmp, iemAImpl_aes_sbox);
    uTmp = iemAImpl_aes_mix_col(&uTmp);
    uTmp.au64[0] ^= puSrc->au64[0];  /* Src = Round Key. */
    uTmp.au64[1] ^= puSrc->au64[1];

    *puDst = uTmp;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vaesenc_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U  uTmp;

    uTmp = iemAImpl_aes_shift_rows(puSrc1, iemAImpl_aes_shift_rows_tbl); /* Dst = state. */
    uTmp = iemAImpl_aes_sub_bytes(&uTmp, iemAImpl_aes_sbox);
    uTmp = iemAImpl_aes_mix_col(&uTmp);
    uTmp.au64[0] ^= puSrc2->au64[0];  /* Src = Round Key. */
    uTmp.au64[1] ^= puSrc2->au64[1];

    *puDst = uTmp;
}


/**
 * [V]AESENCLAST
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_aesenclast_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U  uTmp;

    uTmp = iemAImpl_aes_shift_rows(puDst, iemAImpl_aes_shift_rows_tbl); /* Dst = state. */
    uTmp = iemAImpl_aes_sub_bytes(&uTmp, iemAImpl_aes_sbox);
    uTmp.au64[0] ^= puSrc->au64[0];  /* Src = Round Key. */
    uTmp.au64[1] ^= puSrc->au64[1];

    *puDst = uTmp;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vaesenclast_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U  uTmp;

    uTmp = iemAImpl_aes_shift_rows(puSrc1, iemAImpl_aes_shift_rows_tbl); /* Dst = state. */
    uTmp = iemAImpl_aes_sub_bytes(&uTmp, iemAImpl_aes_sbox);
    uTmp.au64[0] ^= puSrc2->au64[0];  /* Src = Round Key. */
    uTmp.au64[1] ^= puSrc2->au64[1];

    *puDst = uTmp;
}


/**
 * [V]AESDEC
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_aesdec_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U  uTmp;

    uTmp = iemAImpl_aes_shift_rows(puDst, iemAImpl_aes_inv_shift_rows_tbl); /* Dst = state. */
    uTmp = iemAImpl_aes_sub_bytes(&uTmp, iemAImpl_aes_inv_sbox);
    uTmp = iemAImpl_aes_inv_mix_col(&uTmp);
    uTmp.au64[0] ^= puSrc->au64[0];  /* Src = Round Key. */
    uTmp.au64[1] ^= puSrc->au64[1];

    *puDst = uTmp;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vaesdec_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U  uTmp;

    uTmp = iemAImpl_aes_shift_rows(puSrc1, iemAImpl_aes_inv_shift_rows_tbl); /* Dst = state. */
    uTmp = iemAImpl_aes_sub_bytes(&uTmp, iemAImpl_aes_inv_sbox);
    uTmp = iemAImpl_aes_inv_mix_col(&uTmp);
    uTmp.au64[0] ^= puSrc2->au64[0];  /* Src = Round Key. */
    uTmp.au64[1] ^= puSrc2->au64[1];

    *puDst = uTmp;
}


/**
 * [V]AESDECLAST
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_aesdeclast_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    RTUINT128U  uTmp;

    uTmp = iemAImpl_aes_shift_rows(puDst, iemAImpl_aes_inv_shift_rows_tbl); /* Dst = state. */
    uTmp = iemAImpl_aes_sub_bytes(&uTmp, iemAImpl_aes_inv_sbox);
    uTmp.au64[0] ^= puSrc->au64[0];  /* Src = Round Key. */
    uTmp.au64[1] ^= puSrc->au64[1];

    *puDst = uTmp;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vaesdeclast_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2))
{
    RTUINT128U  uTmp;

    uTmp = iemAImpl_aes_shift_rows(puSrc1, iemAImpl_aes_inv_shift_rows_tbl); /* Dst = state. */
    uTmp = iemAImpl_aes_sub_bytes(&uTmp, iemAImpl_aes_inv_sbox);
    uTmp.au64[0] ^= puSrc2->au64[0];  /* Src = Round Key. */
    uTmp.au64[1] ^= puSrc2->au64[1];

    *puDst = uTmp;
}



/**
 * [V]PCMPISTRI
 */

/**
 * Does the comparisons based on the mode and source input format.
 */
static void iemAImpl_pcmpxstrx_cmp(bool afCmpRes[16][16], PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bImm)
{
#define PCMPXSTRX_CMP_CASE(a_fCmpRes, a_puSrc1, a_puSrc2, a_SrcMember, a_bAggOp) \
    do \
    { \
        for (uint8_t idxSrc2 = 0; idxSrc2 < RT_ELEMENTS((a_puSrc2)->a_SrcMember); idxSrc2++) \
            for (uint8_t idxSrc1 = 0; idxSrc1 < RT_ELEMENTS((a_puSrc1)->a_SrcMember); idxSrc1 += 2) \
            { \
                switch (a_bAggOp) \
                { \
                    case 0: \
                    case 2: \
                    case 3: \
                        afCmpRes[idxSrc2][idxSrc1]     = (a_puSrc1)->a_SrcMember[idxSrc1]     == (a_puSrc2)->a_SrcMember[idxSrc2]; \
                        afCmpRes[idxSrc2][idxSrc1 + 1] = (a_puSrc1)->a_SrcMember[idxSrc1 + 1] == (a_puSrc2)->a_SrcMember[idxSrc2]; \
                        break; \
                    case 1: \
                        afCmpRes[idxSrc2][idxSrc1]     = (a_puSrc1)->a_SrcMember[idxSrc1]     <= (a_puSrc2)->a_SrcMember[idxSrc2]; \
                        afCmpRes[idxSrc2][idxSrc1 + 1] = (a_puSrc1)->a_SrcMember[idxSrc1 + 1] >= (a_puSrc2)->a_SrcMember[idxSrc2]; \
                        break; \
                    default: \
                        AssertReleaseFailed(); \
                } \
            } \
    } while(0)

    uint8_t bAggOp = (bImm >> 2) & 0x3;
    switch (bImm & 0x3)
    {
        case 0:
            PCMPXSTRX_CMP_CASE(afCmpRes, puSrc1, puSrc2, au8, bAggOp);
            break;
        case 1:
            PCMPXSTRX_CMP_CASE(afCmpRes, puSrc1, puSrc2, au16, bAggOp);
            break;
        case 2:
            PCMPXSTRX_CMP_CASE(afCmpRes, puSrc1, puSrc2, ai8, bAggOp);
            break;
        case 3:
            PCMPXSTRX_CMP_CASE(afCmpRes, puSrc1, puSrc2, ai16, bAggOp);
            break;
        default:
            AssertReleaseFailed();
    }
#undef PCMPXSTRX_CMP_CASE
}

static uint8_t iemAImpl_pcmpistrx_get_str_len_implicit(PCRTUINT128U puSrc, uint8_t bImm)
{
    if (bImm & 0x1)
    {
        /* Words -> 8 elements. */
        for (uint8_t i = 0; i < RT_ELEMENTS(puSrc->au16); i++)
            if (puSrc->au16[i] == 0)
                return i;

        return 8;
    }
    else
    {
        /* Bytes -> 16 elements. */
        for (uint8_t i = 0; i < RT_ELEMENTS(puSrc->au8); i++)
            if (puSrc->au8[i] == 0)
                return i;

        return 16;
    }
}

static uint8_t iemAImpl_pcmpistrx_get_str_len_explicit(int64_t i64Len, uint8_t bImm)
{
    if (bImm & 0x1)
    {
        if (i64Len > -8 && i64Len < 8)
            return RT_ABS(i64Len);

        return 8;
    }
    else
    {
        if (i64Len > -16 && i64Len < 16)
            return RT_ABS(i64Len);

        return 16;
    }
}

/**
 * Valid/Invalid override of comparisons (Table 4-7 from 4.1.6 of SDM).
 */
static const bool g_afCmpOverride[4][4] =
{
    /* xmm1 AND xmm2/m128 invalid, xmm1 invalid BUT xmm2/m128 valid, xmm1 valid BUT xmm2/m128 invalid, unused dummy/padding for parfait  */
    {                       false,                            false,                            false, false }, /* Imm8[3:2] = 00b (equal any)     */
    {                       false,                            false,                            false, false }, /* Imm8[3:2] = 01b (ranges)        */
    {                        true,                            false,                            false, false }, /* Imm8[3:2] = 10b (equal each)    */
    {                        true,                             true,                            false, false }, /* Imm8[3:2] = 11b (equal ordered) */
};

DECL_FORCE_INLINE(bool) iemAImpl_pcmpxstrx_cmp_override_if_invalid(bool fCmpRes, bool fSrc1Valid, bool fSrc2Valid, uint8_t bAggOp)
{
    if (fSrc1Valid && fSrc2Valid)
        return fCmpRes;

    uint8_t const bSrc1Valid = fSrc1Valid ? 2 : 0;
    uint8_t const bSrc2Valid = fSrc2Valid ? 1 : 0;
    return g_afCmpOverride[bAggOp][bSrc1Valid + bSrc2Valid];
}

static uint16_t iemAImpl_pcmpxstrx_cmp_aggregate(bool afCmpRes[16][16], uint8_t idxLen1, uint8_t idxLen2, uint8_t cElems, uint8_t bImm)
{
    uint8_t bAggOp = (bImm >> 2) & 0x3;
    uint16_t u16Result = 0;

    switch (bAggOp)
    {
        case 0: /* Equal any */
            for (uint8_t idxSrc2 = 0; idxSrc2 < cElems; idxSrc2++)
            {
                uint16_t u16Res = 0;
                for (uint8_t idxSrc1 = 0; idxSrc1 < cElems; idxSrc1++)
                {
                    if (iemAImpl_pcmpxstrx_cmp_override_if_invalid(afCmpRes[idxSrc2][idxSrc1],
                                                                   idxSrc1 < idxLen1,
                                                                   idxSrc2 < idxLen2,
                                                                   bAggOp))
                    {
                        u16Res = RT_BIT(idxSrc2);
                        break;
                    }
                }

                u16Result |= u16Res;
            }
            break;

        case 1: /* Ranges */
            for (uint8_t idxSrc2 = 0; idxSrc2 < cElems; idxSrc2++)
            {
                uint16_t u16Res = 0;
                for (uint8_t idxSrc1 = 0; idxSrc1 < cElems; idxSrc1 += 2)
                {
                    if (   iemAImpl_pcmpxstrx_cmp_override_if_invalid(afCmpRes[idxSrc2][idxSrc1],
                                                                      idxSrc1 < idxLen1,
                                                                      idxSrc2 < idxLen2,
                                                                      bAggOp)
                        && iemAImpl_pcmpxstrx_cmp_override_if_invalid(afCmpRes[idxSrc2][idxSrc1 + 1],
                                                                      (idxSrc1 + 1) < idxLen1,
                                                                      idxSrc2 < idxLen2,
                                                                      bAggOp))
                    {
                        u16Res = RT_BIT(idxSrc2);
                        break;
                    }
                }

                u16Result |= u16Res;
            }
            break;

        case 2: /* Equal each */
            for (uint8_t i = 0; i < cElems; i++)
            {
                if (iemAImpl_pcmpxstrx_cmp_override_if_invalid(afCmpRes[i][i],
                                                               i < idxLen1,
                                                               i < idxLen2,
                                                               bAggOp))
                    u16Result |= RT_BIT(i);
            }
            break;

        case 3: /* Equal ordered */
            u16Result = 0;
            for (uint8_t idxSrc2 = 0; idxSrc2 < cElems; idxSrc2++)
            {
                uint16_t u16Res = RT_BIT(idxSrc2);
                for (uint8_t idxSrc1 = 0, k = idxSrc2; (idxSrc1 < (cElems - idxSrc2)) && (k < cElems); idxSrc1++, k++)
                {
                    if (!iemAImpl_pcmpxstrx_cmp_override_if_invalid(afCmpRes[k][idxSrc1],
                                                                    idxSrc1 < idxLen1,
                                                                    k < idxLen2,
                                                                    bAggOp))
                    {
                        u16Res = 0;
                        break;
                    }
                }

                u16Result |= u16Res;
            }
            break;
    }

    /* Polarity selection. */
    switch ((bImm >> 4) & 0x3)
    {
        case 0:
        case 2:
            /* Nothing to do. */
            break;
        case 1:
            u16Result = (cElems == 8 ? 0xff : 0xffff) ^ u16Result;
            break;
        case 3:
            u16Result ^= RT_BIT(idxLen2) - 1;
            break;
        default:
            AssertReleaseFailed();
    }

    return u16Result;
}

DECL_FORCE_INLINE(void) iemAImpl_pcmpxstrx_set_eflags(uint32_t *pfEFlags, uint16_t u16Result, uint8_t cLen1, uint8_t cLen2, uint8_t cElems)
{
    uint32_t fEFlags = 0;

    if (u16Result)
        fEFlags |= X86_EFL_CF;
    if (cLen2 < cElems)
        fEFlags |= X86_EFL_ZF;
    if (cLen1 < cElems)
        fEFlags |= X86_EFL_SF;
    if (u16Result & 0x1)
        fEFlags |= X86_EFL_OF;
    *pfEFlags = (*pfEFlags & ~X86_EFL_STATUS_BITS) | fEFlags;
}

DECL_FORCE_INLINE(uint16_t) iemAImpl_pcmpxstrx_worker(uint32_t *pEFlags, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2,
                                                      uint8_t cLen1, uint8_t cLen2, uint8_t bEvil)
{
    bool afCmpRes[16][16];
    uint8_t cElems = (bEvil & RT_BIT(0)) ? 8 : 16;

    iemAImpl_pcmpxstrx_cmp(afCmpRes, puSrc1, puSrc2, bEvil);
    uint16_t u16Result = iemAImpl_pcmpxstrx_cmp_aggregate(afCmpRes, cLen1, cLen2, cElems, bEvil);
    iemAImpl_pcmpxstrx_set_eflags(pEFlags, u16Result, cLen1, cLen2, cElems);

    return u16Result;
}

DECL_FORCE_INLINE(uint32_t) iemAImpl_pcmpxstri_set_result_index(uint16_t u16Result, uint8_t cElems, uint8_t bImm)
{
    uint32_t u32Ecx;
    if (bImm & RT_BIT(6))
    {
        /* Index for MSB set. */
        uint32_t idxMsb = ASMBitLastSetU16(u16Result);
        if (idxMsb)
            u32Ecx = idxMsb - 1;
        else
            u32Ecx = cElems;
    }
    else
    {
        /* Index for LSB set. */
        uint32_t idxLsb = ASMBitFirstSetU16(u16Result);
        if (idxLsb)
            u32Ecx = idxLsb - 1;
        else
            u32Ecx = cElems;
    }

    return u32Ecx;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_pcmpistri_u128_fallback,(uint32_t *pEFlags, PCRTUINT128U pSrc1, PCRTUINT128U pSrc2, uint8_t bEvil))
{
    uint8_t cElems = (bEvil & RT_BIT(0)) ? 8 : 16;
    uint8_t cLen1 = iemAImpl_pcmpistrx_get_str_len_implicit(pSrc1, bEvil);
    uint8_t cLen2 = iemAImpl_pcmpistrx_get_str_len_implicit(pSrc2, bEvil);

    uint16_t u16Result = iemAImpl_pcmpxstrx_worker(pEFlags, pSrc1, pSrc2, cLen1, cLen2, bEvil);
    return iemAImpl_pcmpxstri_set_result_index(u16Result, cElems, bEvil);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vpcmpistri_u128_fallback,(uint32_t *pEFlags, PCRTUINT128U pSrc1, PCRTUINT128U pSrc2, uint8_t bEvil))
{
    return iemAImpl_pcmpistri_u128_fallback(pEFlags, pSrc1, pSrc2, bEvil);
}


/**
 * [V]PCMPESTRI
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpestri_u128_fallback,(uint32_t *pu32Ecx, uint32_t *pEFlags, PCIEMPCMPESTRXSRC pSrc, uint8_t bEvil))
{
    uint8_t cElems = (bEvil & RT_BIT(0)) ? 8 : 16;
    uint8_t cLen1 = iemAImpl_pcmpistrx_get_str_len_explicit((int64_t)pSrc->u64Rax, bEvil);
    uint8_t cLen2 = iemAImpl_pcmpistrx_get_str_len_explicit((int64_t)pSrc->u64Rdx, bEvil);

    uint16_t u16Result = iemAImpl_pcmpxstrx_worker(pEFlags, &pSrc->uSrc1, &pSrc->uSrc2, cLen1, cLen2, bEvil);
    *pu32Ecx = iemAImpl_pcmpxstri_set_result_index(u16Result, cElems, bEvil);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpestri_u128_fallback,(uint32_t *pu32Ecx, uint32_t *pEFlags, PCIEMPCMPESTRXSRC pSrc, uint8_t bEvil))
{
    iemAImpl_pcmpestri_u128_fallback(pu32Ecx, pEFlags, pSrc, bEvil);
}


/**
 * [V]PCMPISTRM
 */
DECL_FORCE_INLINE(void) iemAImpl_pcmpxstrm_set_result_mask(PRTUINT128U puDst, uint16_t u16Result, uint8_t cElems, uint8_t bImm)
{
    if (bImm & RT_BIT(6))
    {
        /* Generate a mask. */
        if (cElems == 8)
        {
            for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au16); i++)
                if (u16Result & RT_BIT(i))
                    puDst->au16[i] = 0xffff;
                else
                    puDst->au16[i] = 0;
        }
        else
        {
            for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au8); i++)
                if (u16Result & RT_BIT(i))
                    puDst->au8[i] = 0xff;
                else
                    puDst->au8[i] = 0;
        }
    }
    else
    {
        /* Store the result. */
        puDst->au64[0] = u16Result;
        puDst->au64[1] = 0;
    }
}

IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpistrm_u128_fallback,(PRTUINT128U puDst, uint32_t *pEFlags, PCIEMPCMPISTRXSRC pSrc, uint8_t bEvil))
{
    uint8_t cElems = (bEvil & RT_BIT(0)) ? 8 : 16;
    uint8_t cLen1 = iemAImpl_pcmpistrx_get_str_len_implicit(&pSrc->uSrc1, bEvil);
    uint8_t cLen2 = iemAImpl_pcmpistrx_get_str_len_implicit(&pSrc->uSrc2, bEvil);

    uint16_t u16Result = iemAImpl_pcmpxstrx_worker(pEFlags, &pSrc->uSrc1, &pSrc->uSrc2, cLen1, cLen2, bEvil);
    iemAImpl_pcmpxstrm_set_result_mask(puDst, u16Result, cElems, bEvil);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpistrm_u128_fallback,(PRTUINT128U puDst, uint32_t *pEFlags, PCIEMPCMPISTRXSRC pSrc, uint8_t bEvil))
{
    iemAImpl_pcmpistrm_u128_fallback(puDst, pEFlags, pSrc, bEvil);
}


/**
 * [V]PCMPESTRM
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_pcmpestrm_u128_fallback,(PRTUINT128U puDst, uint32_t *pEFlags, PCIEMPCMPESTRXSRC pSrc, uint8_t bEvil))
{
    uint8_t cElems = (bEvil & RT_BIT(0)) ? 8 : 16;
    uint8_t cLen1 = iemAImpl_pcmpistrx_get_str_len_explicit((int64_t)pSrc->u64Rax, bEvil);
    uint8_t cLen2 = iemAImpl_pcmpistrx_get_str_len_explicit((int64_t)pSrc->u64Rdx, bEvil);

    uint16_t u16Result = iemAImpl_pcmpxstrx_worker(pEFlags, &pSrc->uSrc1, &pSrc->uSrc2, cLen1, cLen2, bEvil);
    iemAImpl_pcmpxstrm_set_result_mask(puDst, u16Result, cElems, bEvil);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpcmpestrm_u128_fallback,(PRTUINT128U puDst, uint32_t *pEFlags, PCIEMPCMPESTRXSRC pSrc, uint8_t bEvil))
{
    iemAImpl_pcmpestrm_u128_fallback(puDst, pEFlags, pSrc, bEvil);
}


/*
 * [V]PCLMULQDQ
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_pclmulqdq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    iemAImpl_vpclmulqdq_u128_fallback(puDst, puDst, puSrc, bEvil);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vpclmulqdq_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bEvil))
{
    uint64_t uSrc1 = puSrc1->au64[bEvil & 0x1];
    uint64_t uSrc2 = puSrc2->au64[(bEvil >> 4) & 0x1];

    puDst->au64[0] = 0;
    puDst->au64[1] = 0;

    /*
     * See https://en.wikipedia.org/wiki/Carry-less_product#Example (as of 2022-09-08) for the algorithm.
     * Do the first round outside the loop to avoid ASAN complaining about shift exponent being too large (64)
     * and squeeze out some optimizations.
     */
    if (uSrc1 & 0x1)
        puDst->au64[0] = uSrc2;

    uSrc1 >>= 1;

    uint8_t iDigit = 1;
    while (uSrc1)
    {
        if (uSrc1 & 0x1)
        {
            puDst->au64[0] ^= (uSrc2 << iDigit);
            puDst->au64[1] ^= uSrc2 >> (64 - iDigit);
        }

        uSrc1 >>= 1;
        iDigit++;
    }
}


/**
 * [V]MOVMSKPS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_movmskps_u128,(uint8_t *pu8Dst, PCRTUINT128U puSrc))
{
    *pu8Dst  =  puSrc->au32[0] >> 31;
    *pu8Dst |= (puSrc->au32[1] >> 31) << 1;
    *pu8Dst |= (puSrc->au32[2] >> 31) << 2;
    *pu8Dst |= (puSrc->au32[3] >> 31) << 3;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vmovmskps_u128_fallback,(uint8_t *pu8Dst, PCRTUINT128U puSrc))
{
    *pu8Dst  =  puSrc->au32[0] >> 31;
    *pu8Dst |= (puSrc->au32[1] >> 31) << 1;
    *pu8Dst |= (puSrc->au32[2] >> 31) << 2;
    *pu8Dst |= (puSrc->au32[3] >> 31) << 3;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vmovmskps_u256_fallback,(uint8_t *pu8Dst, PCRTUINT256U puSrc))
{
    *pu8Dst  =  puSrc->au32[0] >> 31;
    *pu8Dst |= (puSrc->au32[1] >> 31) << 1;
    *pu8Dst |= (puSrc->au32[2] >> 31) << 2;
    *pu8Dst |= (puSrc->au32[3] >> 31) << 3;
    *pu8Dst |= (puSrc->au32[4] >> 31) << 4;
    *pu8Dst |= (puSrc->au32[5] >> 31) << 5;
    *pu8Dst |= (puSrc->au32[6] >> 31) << 6;
    *pu8Dst |= (puSrc->au32[7] >> 31) << 7;
}


/**
 * [V]MOVMSKPD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(void, iemAImpl_movmskpd_u128,(uint8_t *pu8Dst, PCRTUINT128U puSrc))
{
    *pu8Dst  =  puSrc->au64[0] >> 63;
    *pu8Dst |= (puSrc->au64[1] >> 63) << 1;
}

#endif

IEM_DECL_IMPL_DEF(void, iemAImpl_vmovmskpd_u128_fallback,(uint8_t *pu8Dst, PCRTUINT128U puSrc))
{
    *pu8Dst  =  puSrc->au64[0] >> 63;
    *pu8Dst |= (puSrc->au64[1] >> 63) << 1;
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vmovmskpd_u256_fallback,(uint8_t *pu8Dst, PCRTUINT256U puSrc))
{
    *pu8Dst  =  puSrc->au64[0] >> 63;
    *pu8Dst |= (puSrc->au64[1] >> 63) << 1;
    *pu8Dst |= (puSrc->au64[2] >> 63) << 2;
    *pu8Dst |= (puSrc->au64[3] >> 63) << 3;
}


/**
 * [V]CVTTSD2SI
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvttsd2si_i32_r64,(uint32_t uMxCsrIn, int32_t *pi32Dst, const uint64_t *pu64Src))
{
    RTFLOAT64U r64Src;

    r64Src.u = *pu64Src;
    iemSsePrepareValueR64(&r64Src, uMxCsrIn, &r64Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi32Dst = f64_to_i32_r_minMag(iemFpSoftF64FromIprt(&r64Src), true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvttsd2si_i64_r64,(uint32_t uMxCsrIn, int64_t *pi64Dst, const uint64_t *pu64Src))
{
    RTFLOAT64U r64Src;

    r64Src.u = *pu64Src;
    iemSsePrepareValueR64(&r64Src, uMxCsrIn, &r64Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi64Dst = f64_to_i64_r_minMag(iemFpSoftF64FromIprt(&r64Src), true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvttsd2si_i32_r64_fallback,(uint32_t uMxCsrIn, int32_t *pi32Dst, PCRTFLOAT64U pr64Src))
{
    RTFLOAT64U r64Src;
    iemSsePrepareValueR64(&r64Src, uMxCsrIn, pr64Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi32Dst = f64_to_i32_r_minMag(iemFpSoftF64FromIprt(&r64Src), true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvttsd2si_i64_r64_fallback,(uint32_t uMxCsrIn, int64_t *pi64Dst, PCRTFLOAT64U pr64Src))
{
    RTFLOAT64U r64Src;
    iemSsePrepareValueR64(&r64Src, uMxCsrIn, pr64Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi64Dst = f64_to_i64_r_minMag(iemFpSoftF64FromIprt(&r64Src), true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


/**
 * [V]CVTSD2SI
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtsd2si_i32_r64,(uint32_t uMxCsrIn, int32_t *pi32Dst, const uint64_t *pu64Src))
{
    RTFLOAT64U r64Src;

    r64Src.u = *pu64Src;
    iemSsePrepareValueR64(&r64Src, uMxCsrIn, &r64Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi32Dst = f64_to_i32(iemFpSoftF64FromIprt(&r64Src), SoftState.roundingMode, true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtsd2si_i64_r64,(uint32_t uMxCsrIn, int64_t *pi64Dst, const uint64_t *pu64Src))
{
    RTFLOAT64U r64Src;

    r64Src.u = *pu64Src;
    iemSsePrepareValueR64(&r64Src, uMxCsrIn, &r64Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi64Dst = f64_to_i64(iemFpSoftF64FromIprt(&r64Src), SoftState.roundingMode, true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtsd2si_i32_r64_fallback,(uint32_t uMxCsrIn, int32_t *pi32Dst, PCRTFLOAT64U pr64Src))
{
    RTFLOAT64U r64Src;
    iemSsePrepareValueR64(&r64Src, uMxCsrIn, pr64Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi32Dst = f64_to_i32(iemFpSoftF64FromIprt(&r64Src), SoftState.roundingMode, true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtsd2si_i64_r64_fallback,(uint32_t uMxCsrIn, int64_t *pi64Dst, PCRTFLOAT64U pr64Src))
{
    RTFLOAT64U r64Src;
    iemSsePrepareValueR64(&r64Src, uMxCsrIn, pr64Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi64Dst = f64_to_i64(iemFpSoftF64FromIprt(&r64Src), SoftState.roundingMode, true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


/**
 * [V]CVTTSS2SI
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvttss2si_i32_r32,(uint32_t uMxCsrIn, int32_t *pi32Dst, const uint32_t *pu32Src))
{
    RTFLOAT32U r32Src;

    r32Src.u = *pu32Src;
    iemSsePrepareValueR32(&r32Src, uMxCsrIn, &r32Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi32Dst = f32_to_i32_r_minMag(iemFpSoftF32FromIprt(&r32Src), true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvttss2si_i64_r32,(uint32_t uMxCsrIn, int64_t *pi64Dst, const uint32_t *pu32Src))
{
    RTFLOAT32U r32Src;

    r32Src.u = *pu32Src;
    iemSsePrepareValueR32(&r32Src, uMxCsrIn, &r32Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi64Dst = f32_to_i64_r_minMag(iemFpSoftF32FromIprt(&r32Src), true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvttss2si_i32_r32_fallback,(uint32_t uMxCsrIn, int32_t *pi32Dst, PCRTFLOAT32U pr32Src))
{
    RTFLOAT32U r32Src;
    iemSsePrepareValueR32(&r32Src, uMxCsrIn, pr32Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi32Dst = f32_to_i32_r_minMag(iemFpSoftF32FromIprt(&r32Src), true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvttss2si_i64_r32_fallback,(uint32_t uMxCsrIn, int64_t *pi64Dst, PCRTFLOAT32U pr32Src))
{
    RTFLOAT32U r32Src;
    iemSsePrepareValueR32(&r32Src, uMxCsrIn, pr32Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi64Dst = f32_to_i64_r_minMag(iemFpSoftF32FromIprt(&r32Src), true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


/**
 * [V]CVTSS2SI
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtss2si_i32_r32,(uint32_t uMxCsrIn, int32_t *pi32Dst, const uint32_t *pu32Src))
{
    RTFLOAT32U r32Src;

    r32Src.u = *pu32Src;
    iemSsePrepareValueR32(&r32Src, uMxCsrIn, &r32Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi32Dst = f32_to_i32(iemFpSoftF32FromIprt(&r32Src), SoftState.roundingMode, true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtss2si_i64_r32,(uint32_t uMxCsrIn, int64_t *pi64Dst, const uint32_t *pu32Src))
{
    RTFLOAT32U r32Src;

    r32Src.u = *pu32Src;
    iemSsePrepareValueR32(&r32Src, uMxCsrIn, &r32Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi64Dst = f32_to_i64(iemFpSoftF32FromIprt(&r32Src), SoftState.roundingMode, true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtss2si_i32_r32_fallback,(uint32_t uMxCsrIn, int32_t *pi32Dst, PCRTFLOAT32U pr32Src))
{
    RTFLOAT32U r32Src;
    iemSsePrepareValueR32(&r32Src, uMxCsrIn, pr32Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi32Dst = f32_to_i32(iemFpSoftF32FromIprt(&r32Src), SoftState.roundingMode, true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtss2si_i64_r32_fallback,(uint32_t uMxCsrIn, int64_t *pi64Dst, PCRTFLOAT32U pr32Src))
{
    RTFLOAT32U r32Src;
    iemSsePrepareValueR32(&r32Src, uMxCsrIn, pr32Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    *pi64Dst = f32_to_i64(iemFpSoftF32FromIprt(&r32Src), SoftState.roundingMode, true /*exact*/, &SoftState);
    return uMxCsrIn | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


/**
 * [V]CVTSI2SD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtsi2sd_r64_i32,(uint32_t uMxCsrIn, PRTFLOAT64U pr64Dst, const int32_t *pi32Src))
{
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    float64_t r64Res = i32_to_f64(*pi32Src, &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResult(&SoftState, r64Res, pr64Dst, uMxCsrIn);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtsi2sd_r64_i64,(uint32_t uMxCsrIn, PRTFLOAT64U pr64Dst, const int64_t *pi64Src))
{
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    float64_t r64Res = i64_to_f64(*pi64Src, &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResult(&SoftState, r64Res, pr64Dst, uMxCsrIn);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtsi2sd_u128_i32_fallback, (uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc1, const int32_t *pi32Src2))
{
    puDst->au64[1] = puSrc1->au64[1];

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    float64_t r64Res = i32_to_f64(*pi32Src2, &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResult(&SoftState, r64Res, &puDst->ar64[0], uMxCsrIn);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtsi2sd_u128_i64_fallback, (uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc1, const int64_t *pi64Src2))
{
    puDst->au64[1] = puSrc1->au64[1];

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    float64_t r64Res = i64_to_f64(*pi64Src2, &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResult(&SoftState, r64Res, &puDst->ar64[0], uMxCsrIn);
}


/**
 * [V]CVTSI2SS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtsi2ss_r32_i32,(uint32_t uMxCsrIn, PRTFLOAT32U pr32Dst, const int32_t *pi32Src))
{
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    float32_t r32Res = i32_to_f32(*pi32Src, &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Res, pr32Dst, uMxCsrIn);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtsi2ss_r32_i64,(uint32_t uMxCsrIn, PRTFLOAT32U pr32Dst, const int64_t *pi64Src))
{
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    float32_t r32Res = i64_to_f32(*pi64Src, &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Res, pr32Dst, uMxCsrIn);
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtsi2ss_u128_i32_fallback, (uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc1, const int32_t *pi32Src2))
{
    puDst->au32[1] = puSrc1->au32[1];
    puDst->au64[1] = puSrc1->au64[1];

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    float32_t r32Res = i32_to_f32(*pi32Src2, &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Res, &puDst->ar32[0], uMxCsrIn);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcvtsi2ss_u128_i64_fallback, (uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc1, const int64_t *pi64Src2))
{
    puDst->au32[1] = puSrc1->au32[1];
    puDst->au64[1] = puSrc1->au64[1];

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);
    float32_t r32Res = i64_to_f32(*pi64Src2, &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Res, &puDst->ar32[0], uMxCsrIn);
}


/**
 * [V]UCOMISS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_ucomiss_u128,(uint32_t uMxCsrIn, uint32_t *pfEFlags, RTFLOAT32U uSrc1, RTFLOAT32U uSrc2))
{
    uint32_t fEFlagsNew = *pfEFlags & ~X86_EFL_STATUS_BITS;

    if (RTFLOAT32U_IS_SIGNALLING_NAN(&uSrc1) || RTFLOAT32U_IS_SIGNALLING_NAN(&uSrc2))
    {
        uMxCsrIn   |= X86_MXCSR_IE;
        fEFlagsNew |= X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF; /* UNORDERED 111 */
    }
    else if (RTFLOAT32U_IS_QUIET_NAN(&uSrc1) || RTFLOAT32U_IS_QUIET_NAN(&uSrc2))
    {
        /* ucomiss doesn't raise \#IE for quiet NaNs. */
        fEFlagsNew |= X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF; /* UNORDERED 111 */
    }
    else
    {
        softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);

        RTFLOAT32U r32Src1, r32Src2;
        uint32_t fDe  = iemSsePrepareValueR32(&r32Src1, uMxCsrIn, &uSrc1);
                 fDe |= iemSsePrepareValueR32(&r32Src2, uMxCsrIn, &uSrc2);

        float32_t f32Src1 = iemFpSoftF32FromIprt(&r32Src1);
        float32_t f32Src2 = iemFpSoftF32FromIprt(&r32Src2);
        if (f32_eq(f32Src1, f32Src2, &SoftState))
            fEFlagsNew |= X86_EFL_ZF; /* EQUAL        100 */
        else if (f32_lt(f32Src1, f32Src2, &SoftState))
            fEFlagsNew |= X86_EFL_CF; /* LESS_THAN    001 */
        /* else:                         GREATER_THAN 000 */

        uMxCsrIn |= fDe;
    }

    *pfEFlags = fEFlagsNew;
    return uMxCsrIn;
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vucomiss_u128_fallback,(uint32_t uMxCsrIn, uint32_t *pfEFlags, RTFLOAT32U uSrc1, RTFLOAT32U uSrc2))
{
    return iemAImpl_ucomiss_u128(uMxCsrIn, pfEFlags, uSrc1, uSrc2);
}


/**
 * [V]UCOMISD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_ucomisd_u128,(uint32_t uMxCsrIn, uint32_t *pfEFlags, RTFLOAT64U uSrc1, RTFLOAT64U uSrc2))
{
    uint32_t fEFlagsNew = *pfEFlags & ~X86_EFL_STATUS_BITS;

    if (RTFLOAT64U_IS_SIGNALLING_NAN(&uSrc1) || RTFLOAT64U_IS_SIGNALLING_NAN(&uSrc2))
    {
        uMxCsrIn   |= X86_MXCSR_IE;
        fEFlagsNew |= X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF; /* UNORDERED 111 */
    }
    else if (RTFLOAT64U_IS_QUIET_NAN(&uSrc1) || RTFLOAT64U_IS_QUIET_NAN(&uSrc2))
    {
        /* ucomiss doesn't raise \#IE for quiet NaNs. */
        fEFlagsNew |= X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF; /* UNORDERED 111 */
    }
    else
    {
        softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);

        RTFLOAT64U r64Src1, r64Src2;
        uint32_t fDe = iemSsePrepareValueR64(&r64Src1, uMxCsrIn, &uSrc1)
                     | iemSsePrepareValueR64(&r64Src2, uMxCsrIn, &uSrc2);

        float64_t f64Src1 = iemFpSoftF64FromIprt(&r64Src1);
        float64_t f64Src2 = iemFpSoftF64FromIprt(&r64Src2);
        if (f64_eq(f64Src1, f64Src2, &SoftState))
            fEFlagsNew |= X86_EFL_ZF; /* EQUAL        100 */
        else if (f64_lt(f64Src1, f64Src2, &SoftState))
            fEFlagsNew |= X86_EFL_CF; /* LESS_THAN    001 */
        /* else:                         GREATER_THAN 000 */

        uMxCsrIn |= fDe;
    }

    *pfEFlags = fEFlagsNew;
    return uMxCsrIn;
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vucomisd_u128_fallback,(uint32_t uMxCsrIn, uint32_t *pfEFlags, RTFLOAT64U uSrc1, RTFLOAT64U uSrc2))
{
    return iemAImpl_ucomisd_u128(uMxCsrIn, pfEFlags, uSrc1, uSrc2);
}


/**
 * [V]COMISS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_comiss_u128,(uint32_t uMxCsrIn, uint32_t *pfEFlags, RTFLOAT32U uSrc1, RTFLOAT32U uSrc2))
{
    uint32_t fEFlagsNew = *pfEFlags & ~X86_EFL_STATUS_BITS;

    if (   RTFLOAT32U_IS_SIGNALLING_NAN(&uSrc1) || RTFLOAT32U_IS_SIGNALLING_NAN(&uSrc2)
        || RTFLOAT32U_IS_QUIET_NAN(&uSrc1) || RTFLOAT32U_IS_QUIET_NAN(&uSrc2))
    {
        uMxCsrIn   |= X86_MXCSR_IE;
        fEFlagsNew |= X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF; /* UNORDERED 111 */
    }
    else
    {
        softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);

        RTFLOAT32U r32Src1, r32Src2;
        uint32_t fDe = iemSsePrepareValueR32(&r32Src1, uMxCsrIn, &uSrc1)
                     | iemSsePrepareValueR32(&r32Src2, uMxCsrIn, &uSrc2);

        float32_t f32Src1 = iemFpSoftF32FromIprt(&r32Src1);
        float32_t f32Src2 = iemFpSoftF32FromIprt(&r32Src2);
        if (f32_eq(f32Src1, f32Src2, &SoftState))
            fEFlagsNew |= X86_EFL_ZF; /* EQUAL        100 */
        else if (f32_lt(f32Src1, f32Src2, &SoftState))
            fEFlagsNew |= X86_EFL_CF; /* LESS_THAN    001 */
        /* else:                         GREATER_THAN 000 */

        uMxCsrIn |= fDe;
    }

    *pfEFlags = fEFlagsNew;
    return uMxCsrIn;
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcomiss_u128_fallback,(uint32_t uMxCsrIn, uint32_t *pfEFlags, RTFLOAT32U uSrc1, RTFLOAT32U uSrc2))
{
    return iemAImpl_comiss_u128(uMxCsrIn, pfEFlags, uSrc1, uSrc2);
}


/**
 * [V]COMISD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_comisd_u128,(uint32_t uMxCsrIn, uint32_t *pfEFlags, RTFLOAT64U uSrc1, RTFLOAT64U uSrc2))
{
    uint32_t fEFlagsNew = *pfEFlags & ~X86_EFL_STATUS_BITS;

    if (   RTFLOAT64U_IS_SIGNALLING_NAN(&uSrc1) || RTFLOAT64U_IS_SIGNALLING_NAN(&uSrc2)
        || RTFLOAT64U_IS_QUIET_NAN(&uSrc1) || RTFLOAT64U_IS_QUIET_NAN(&uSrc2))
    {
        uMxCsrIn   |= X86_MXCSR_IE;
        fEFlagsNew |= X86_EFL_ZF | X86_EFL_PF | X86_EFL_CF; /* UNORDERED 111 */
    }
    else
    {
        softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(uMxCsrIn);

        RTFLOAT64U r64Src1, r64Src2;
        uint32_t fDe  = iemSsePrepareValueR64(&r64Src1, uMxCsrIn, &uSrc1);
                 fDe |= iemSsePrepareValueR64(&r64Src2, uMxCsrIn, &uSrc2);

        float64_t f64Src1 = iemFpSoftF64FromIprt(&r64Src1);
        float64_t f64Src2 = iemFpSoftF64FromIprt(&r64Src2);
        if (f64_eq(f64Src1, f64Src2, &SoftState))
            fEFlagsNew |= X86_EFL_ZF; /* EQUAL        100 */
        else if (f64_lt(f64Src1, f64Src2, &SoftState))
            fEFlagsNew |= X86_EFL_CF; /* LESS_THAN    001 */
        /* else:                         GREATER_THAN 000 */

        uMxCsrIn |= fDe;
    }

    *pfEFlags = fEFlagsNew;
    return uMxCsrIn;
}
#endif

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcomisd_u128_fallback,(uint32_t uMxCsrIn, uint32_t *pfEFlags, RTFLOAT64U uSrc1, RTFLOAT64U uSrc2))
{
    return iemAImpl_comisd_u128(uMxCsrIn, pfEFlags, uSrc1, uSrc2);
}


/**
 * [V]CMPPS / [V]CMPPD / [V]CMPSS / [V]CMPSD
 */
/**
 * A compare truth table entry.
 */
typedef struct CMPTRUTHTBLENTRY
{
    /** Flag whether the \#IA is signalled when one of the source oeprans is a QNaN */
    bool                fSignalsOnQNan;
    /** The boolean result when the input operands are unordered. */
    bool                fUnordered;
    /** The boolean result when A = B. */
    bool                fEqual;
    /** The boolean result when A < B. */
    bool                fLowerThan;
    /** The boolean result when A > B. */
    bool                fGreaterThan;
} CMPTRUTHTBLENTRY;
/** Pointer to a const truth table entry. */
typedef const CMPTRUTHTBLENTRY *PCCMPTRUTHTBLENTRY;


/** The compare truth table (indexed by immediate). */
static const CMPTRUTHTBLENTRY g_aCmpTbl[] =
{
                            /* fSignalsOnQNan   fUnordered      fEqual      fLowerThan      fGreaterThan */
    /* 00H (EQ_OQ)   */     {  false,           false,          true,       false,          false           },
    /* 01H (LT_OS)   */     {  true,            false,          false,      true,           false           },
    /* 02H (LE_OS)   */     {  true,            false,          true,       true,           false           },
    /* 03H (UNORD_Q) */     {  false,           true,           false,      false,          false           },
    /* 04H (NEQ_UQ)  */     {  false,           true,           false,      true,           true            },
    /* 05H (NLT_US)  */     {  true,            true,           true,       false,          true            },
    /* 06H (NLE_US)  */     {  true,            true,           false,      false,          true            },
    /* 07H (ORQ_Q)   */     {  false,           false,          true,       true,           true            },
    /* Entries supported by the AVX variants. */
    /* 08H (EQ_UQ)   */     {  false,           true,           true,       false,          false           },
    /* 09H (NGE_US)  */     {  true,            true,           false,      true,           false           },
    /* 0aH (NGT_US)  */     {  true,            true,           true,       true,           false           },
    /* 0bH (FALSE_OQ)*/     {  false,           false,          false,      false,          false           },
    /* 0cH (NEQ_OQ)  */     {  false,           false,          false,      true,           true            },
    /* 0dH (GE_OS)   */     {  true,            false,          true,       false,          true            },
    /* 0eH (GT_OS)   */     {  true,            false,          false,      false,          true            },
    /* 0fH (TRUE_UQ) */     {  false,           true,           true,       true,           true            },
    /* 10H (EQ_OS)   */     {  true,            false,          true,       false,          false           },
    /* 11H (LT_OQ)   */     {  false,           false,          false,      true,           false           },
    /* 12H (LE_OQ)   */     {  false,           false,          true,       true,           false           },
    /* 13H (UNORD_S) */     {  true,            true,           false,      false,          false           },
    /* 14H (NEQ_US)  */     {  true,            true,           false,      true,           true            },
    /* 15H (NLT_UQ)  */     {  false,           true,           true,       false,          true            },
    /* 16H (NLE_UQ)  */     {  false,           true,           false,      false,          true            },
    /* 17H (ORD_S)   */     {  true,            false,          true,       true,           true            },
    /* 18H (EQ_US)   */     {  true,            true,           true,       false,          false           },
    /* 19H (NGE_UQ)  */     {  false,           true,           false,      true,           false           },
    /* 1aH (NGT_UQ)  */     {  false,           true,           true,       true,           false           },
    /* 1bH (FALSE_OS)*/     {  true,            false,          false,      false,          false           },
    /* 1cH (NEQ_OS)  */     {  true,            false,          false,      true,           true            },
    /* 1dH (GE_OQ)   */     {  false,           false,          true,       false,          true            },
    /* 1eH (GT_OQ)   */     {  false,           false,          false,      false,          true            },
    /* 1fH (TRUE_US) */     {  true,            true,           true,       true,           true            },
};


static bool iemAImpl_cmp_worker_r32(uint32_t *pfMxcsr, PCRTFLOAT32U pr32Src1, PCRTFLOAT32U pr32Src2, uint8_t bEvil)
{
    bool fRes;
    AssertRelease(bEvil < RT_ELEMENTS(g_aCmpTbl));

    if (RTFLOAT32U_IS_SIGNALLING_NAN(pr32Src1) || RTFLOAT32U_IS_SIGNALLING_NAN(pr32Src2))
    {
        *pfMxcsr |= X86_MXCSR_IE;
        fRes = g_aCmpTbl[bEvil].fUnordered;
    }
    else if (RTFLOAT32U_IS_QUIET_NAN(pr32Src1) || RTFLOAT32U_IS_QUIET_NAN(pr32Src2))
    {
        if (g_aCmpTbl[bEvil].fSignalsOnQNan)
            *pfMxcsr |= X86_MXCSR_IE;
        fRes = g_aCmpTbl[bEvil].fUnordered;
    }
    else
    {
        softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(*pfMxcsr);

        RTFLOAT32U r32Src1, r32Src2;
        uint32_t fDe  = iemSsePrepareValueR32(&r32Src1, *pfMxcsr, pr32Src1);
                 fDe |= iemSsePrepareValueR32(&r32Src2, *pfMxcsr, pr32Src2);

        *pfMxcsr |= fDe;
        float32_t f32Src1 = iemFpSoftF32FromIprt(&r32Src1);
        float32_t f32Src2 = iemFpSoftF32FromIprt(&r32Src2);
        if (f32_eq(f32Src1, f32Src2, &SoftState))
            fRes = g_aCmpTbl[bEvil].fEqual;
        else if (f32_lt(f32Src1, f32Src2, &SoftState))
            fRes = g_aCmpTbl[bEvil].fLowerThan;
        else
            fRes = g_aCmpTbl[bEvil].fGreaterThan;
    }

    return fRes;
}


static bool iemAImpl_cmp_worker_r64(uint32_t *pfMxcsr, PCRTFLOAT64U pr64Src1, PCRTFLOAT64U pr64Src2, uint8_t bEvil)
{
    bool fRes;
    AssertRelease(bEvil < RT_ELEMENTS(g_aCmpTbl));

    if (RTFLOAT64U_IS_SIGNALLING_NAN(pr64Src1) || RTFLOAT64U_IS_SIGNALLING_NAN(pr64Src2))
    {
        *pfMxcsr |= X86_MXCSR_IE;
        fRes = g_aCmpTbl[bEvil].fUnordered;
    }
    else if (RTFLOAT64U_IS_QUIET_NAN(pr64Src1) || RTFLOAT64U_IS_QUIET_NAN(pr64Src2))
    {
        if (g_aCmpTbl[bEvil].fSignalsOnQNan)
            *pfMxcsr |= X86_MXCSR_IE;
        fRes = g_aCmpTbl[bEvil].fUnordered;
    }
    else
    {
        softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(*pfMxcsr);

        RTFLOAT64U r64Src1, r64Src2;
        uint32_t fDe  = iemSsePrepareValueR64(&r64Src1, *pfMxcsr, pr64Src1)
                      | iemSsePrepareValueR64(&r64Src2, *pfMxcsr, pr64Src2);

        *pfMxcsr |= fDe;
        float64_t f64Src1 = iemFpSoftF64FromIprt(&r64Src1);
        float64_t f64Src2 = iemFpSoftF64FromIprt(&r64Src2);
        if (f64_eq(f64Src1, f64Src2, &SoftState))
            fRes = g_aCmpTbl[bEvil].fEqual;
        else if (f64_lt(f64Src1, f64Src2, &SoftState))
            fRes = g_aCmpTbl[bEvil].fLowerThan;
        else
            fRes = g_aCmpTbl[bEvil].fGreaterThan;
    }

    return fRes;
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cmpps_u128,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->ar32); i++)
    {
        if (iemAImpl_cmp_worker_r32(&uMxCsrIn, &pSrc->uSrc1.ar32[i], &pSrc->uSrc2.ar32[i], bEvil & 0x7))
            puDst->au32[i] = UINT32_MAX;
        else
            puDst->au32[i] = 0;
    }

    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cmppd_u128,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->ar64); i++)
    {
        if (iemAImpl_cmp_worker_r64(&uMxCsrIn, &pSrc->uSrc1.ar64[i], &pSrc->uSrc2.ar64[i], bEvil & 0x7))
            puDst->au64[i] = UINT64_MAX;
        else
            puDst->au64[i] = 0;
    }

    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cmpss_u128,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bEvil))
{
    if (iemAImpl_cmp_worker_r32(&uMxCsrIn, &pSrc->uSrc1.ar32[0], &pSrc->uSrc2.ar32[0], bEvil & 0x7))
        puDst->au32[0] = UINT32_MAX;
    else
        puDst->au32[0] = 0;

    puDst->au32[1] = pSrc->uSrc1.au32[1];
    puDst->au64[1] = pSrc->uSrc1.au64[1];
    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cmpsd_u128,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bEvil))
{
    if (iemAImpl_cmp_worker_r64(&uMxCsrIn, &pSrc->uSrc1.ar64[0], &pSrc->uSrc2.ar64[0], bEvil & 0x7))
        puDst->au64[0] = UINT64_MAX;
    else
        puDst->au64[0] = 0;

    puDst->au64[1] = pSrc->uSrc1.au64[1];
    return uMxCsrIn;
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcmpps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->ar32); i++)
    {
        if (iemAImpl_cmp_worker_r32(&uMxCsrIn, &pSrc->uSrc1.ar32[i], &pSrc->uSrc2.ar32[i], bEvil & 0x1f))
            puDst->au32[i] = UINT32_MAX;
        else
            puDst->au32[i] = 0;
    }

    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcmpps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCIEMMEDIAF2YMMSRC pSrc, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->ar32); i++)
    {
        if (iemAImpl_cmp_worker_r32(&uMxCsrIn, &pSrc->uSrc1.ar32[i], &pSrc->uSrc2.ar32[i], bEvil & 0x1f))
            puDst->au32[i] = UINT32_MAX;
        else
            puDst->au32[i] = 0;
    }

    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcmppd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->ar64); i++)
    {
        if (iemAImpl_cmp_worker_r64(&uMxCsrIn, &pSrc->uSrc1.ar64[i], &pSrc->uSrc2.ar64[i], bEvil & 0x1f))
            puDst->au64[i] = UINT64_MAX;
        else
            puDst->au64[i] = 0;
    }

    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcmppd_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCIEMMEDIAF2YMMSRC pSrc, uint8_t bEvil))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->ar64); i++)
    {
        if (iemAImpl_cmp_worker_r64(&uMxCsrIn, &pSrc->uSrc1.ar64[i], &pSrc->uSrc2.ar64[i], bEvil & 0x1f))
            puDst->au64[i] = UINT64_MAX;
        else
            puDst->au64[i] = 0;
    }

    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcmpss_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bEvil))
{
    if (iemAImpl_cmp_worker_r32(&uMxCsrIn, &pSrc->uSrc1.ar32[0], &pSrc->uSrc2.ar32[0], bEvil & 0x1f))
        puDst->au32[0] = UINT32_MAX;
    else
        puDst->au32[0] = 0;

    puDst->au32[1] = pSrc->uSrc1.au32[1];
    puDst->au64[1] = pSrc->uSrc1.au64[1];
    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vcmpsd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bEvil))
{
    if (iemAImpl_cmp_worker_r64(&uMxCsrIn, &pSrc->uSrc1.ar64[0], &pSrc->uSrc2.ar64[0], bEvil & 0x1f))
        puDst->au64[0] = UINT64_MAX;
    else
        puDst->au64[0] = 0;

    puDst->au64[1] = pSrc->uSrc1.au64[1];
    return uMxCsrIn;
}


/**
 * [V]ROUNDPS / [V]ROUNDPD / [V]ROUNDSS / [V]ROUNDSD
 */

#define X86_SSE_ROUNDXX_IMM_RC_MASK    UINT8_C(0x03)
#define X86_SSE_ROUNDXX_IMM_ROUND_SEL  UINT8_C(0x04)
#define X86_SSE_ROUNDXX_IMM_PRECISION  UINT8_C(0x08)

#define X86_SSE_ROUNDXX_IMM_MASK       UINT8_C(0x0F)

DECLINLINE(softfloat_state_t) iemSseRoundXXMxcsrAndImmToSoftState(uint32_t fMxcsr, uint8_t bImm)
{
    if (bImm & X86_SSE_ROUNDXX_IMM_ROUND_SEL)
        return IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);

    fMxcsr &= ~X86_MXCSR_RC_MASK;
    fMxcsr |= (bImm & X86_SSE_ROUNDXX_IMM_RC_MASK) << X86_MXCSR_RC_SHIFT;
    return IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
}

static RTFLOAT32U iemAImpl_round_worker_r32(uint32_t *pfMxcsr, PCRTFLOAT32U pr32Src, uint8_t bImm)
{
    RTFLOAT32U r32Src, r32Dst;
    float32_t f32Src;
    softfloat_state_t SoftState = iemSseRoundXXMxcsrAndImmToSoftState(*pfMxcsr, bImm);
    bool fExact = !RT_BOOL(bImm & X86_SSE_ROUNDXX_IMM_PRECISION);

    iemSsePrepareValueR32(&r32Src, *pfMxcsr, pr32Src);
    f32Src = f32_roundToInt(iemFpSoftF32FromIprt(&r32Src), SoftState.roundingMode, fExact, &SoftState);

    iemFpSoftF32ToIprt(&r32Dst, f32Src);
    return r32Dst;
}

static RTFLOAT64U iemAImpl_round_worker_r64(uint32_t *pfMxcsr, PCRTFLOAT64U pr64Src, uint8_t bImm)
{
    RTFLOAT64U r64Src, r64Dst;
    float64_t f64Src;
    softfloat_state_t SoftState = iemSseRoundXXMxcsrAndImmToSoftState(*pfMxcsr, bImm);
    bool fExact = !RT_BOOL(bImm & X86_SSE_ROUNDXX_IMM_PRECISION);

    iemSsePrepareValueR64(&r64Src, *pfMxcsr, pr64Src);
    f64Src = f64_roundToInt(iemFpSoftF64FromIprt(&r64Src), SoftState.roundingMode, fExact, &SoftState);

    iemFpSoftF64ToIprt(&r64Dst, f64Src);
    return r64Dst;
}


#ifdef IEM_WITHOUT_ASSEMBLY
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_roundss_u128,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bImm))
{
    puDst->ar32[0] = iemAImpl_round_worker_r32(&uMxCsrIn, &pSrc->uSrc2.ar32[0], bImm & X86_SSE_ROUNDXX_IMM_MASK);
    puDst->au32[1] = pSrc->uSrc1.au32[1];
    puDst->au64[1] = pSrc->uSrc1.au64[1];
    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_roundsd_u128,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bImm))
{
    puDst->ar64[0] = iemAImpl_round_worker_r64(&uMxCsrIn, &pSrc->uSrc2.ar64[0], bImm & X86_SSE_ROUNDXX_IMM_MASK);
    puDst->au64[1] = pSrc->uSrc1.au64[1];
    return uMxCsrIn;
}
#endif


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vroundss_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bImm))
{
    puDst->ar32[0] = iemAImpl_round_worker_r32(&uMxCsrIn, &pSrc->uSrc2.ar32[0], bImm & X86_SSE_ROUNDXX_IMM_MASK);
    puDst->au32[1] = pSrc->uSrc1.au32[1];
    puDst->au64[1] = pSrc->uSrc1.au64[1];
    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vroundsd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bImm))
{
    puDst->ar64[0] = iemAImpl_round_worker_r64(&uMxCsrIn, &pSrc->uSrc2.ar64[0], bImm & X86_SSE_ROUNDXX_IMM_MASK);
    puDst->au64[1] = pSrc->uSrc1.au64[1];
    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_roundps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc, uint8_t bImm))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->ar32); i++)
    {
        puDst->ar32[i] = iemAImpl_round_worker_r32(&uMxCsrIn, &puSrc->ar32[i], bImm & X86_SSE_ROUNDXX_IMM_MASK);
    }

    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_roundpd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc, uint8_t bImm))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->ar64); i++)
    {
        puDst->ar64[i] = iemAImpl_round_worker_r64(&uMxCsrIn, &puSrc->ar64[i], bImm & X86_SSE_ROUNDXX_IMM_MASK);
    }

    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vroundps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc, uint8_t bImm))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->ar32); i++)
    {
        puDst->ar32[i] = iemAImpl_round_worker_r32(&uMxCsrIn, &puSrc->ar32[i], bImm & X86_SSE_ROUNDXX_IMM_MASK);
    }

    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vroundpd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCX86XMMREG puSrc, uint8_t bImm))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->ar64); i++)
    {
        puDst->ar64[i] = iemAImpl_round_worker_r64(&uMxCsrIn, &puSrc->ar64[i], bImm & X86_SSE_ROUNDXX_IMM_MASK);
    }

    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vroundps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCX86YMMREG puSrc, uint8_t bImm))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->ar32); i++)
    {
        puDst->ar32[i] = iemAImpl_round_worker_r32(&uMxCsrIn, &puSrc->ar32[i], bImm & X86_SSE_ROUNDXX_IMM_MASK);
    }

    return uMxCsrIn;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vroundpd_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCX86YMMREG puSrc, uint8_t bImm))
{
    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->ar64); i++)
    {
        puDst->ar64[i] = iemAImpl_round_worker_r64(&uMxCsrIn, &puSrc->ar64[i], bImm & X86_SSE_ROUNDXX_IMM_MASK);
    }

    return uMxCsrIn;
}



/**
 * CVTPD2PI
 */
#ifdef IEM_WITHOUT_ASSEMBLY
static uint32_t iemAImpl_cvtpd2pi_u128_worker(uint32_t fMxcsr, int32_t *pi32Dst, PCRTFLOAT64U pr64Src)
{
    RTFLOAT64U r64Src;
    iemSsePrepareValueR64(&r64Src, fMxcsr, pr64Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    *pi32Dst = f64_to_i32(iemFpSoftF64FromIprt(&r64Src), SoftState.roundingMode, true /*exact*/, &SoftState);
    return fMxcsr | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtpd2pi_u128,(uint32_t fMxCsrIn, uint64_t *pu64Dst, PCX86XMMREG pSrc))
{
    RTUINT64U u64Res;
    uint32_t fMxcsrOut  = iemAImpl_cvtpd2pi_u128_worker(fMxCsrIn, &u64Res.ai32[0], &pSrc->ar64[0]);
             fMxcsrOut |= iemAImpl_cvtpd2pi_u128_worker(fMxCsrIn, &u64Res.ai32[1], &pSrc->ar64[1]);

    *pu64Dst = u64Res.u;
    return fMxcsrOut;
}
#endif


/**
 * CVTTPD2PI
 */
#ifdef IEM_WITHOUT_ASSEMBLY
static uint32_t iemAImpl_cvttpd2pi_u128_worker(uint32_t fMxcsr, int32_t *pi32Dst, PCRTFLOAT64U pr64Src)
{
    RTFLOAT64U r64Src;
    iemSsePrepareValueR64(&r64Src, fMxcsr, pr64Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    *pi32Dst = f64_to_i32_r_minMag(iemFpSoftF64FromIprt(&r64Src), true /*exact*/, &SoftState);
    return fMxcsr | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvttpd2pi_u128,(uint32_t fMxCsrIn, uint64_t *pu64Dst, PCX86XMMREG pSrc))
{
    RTUINT64U u64Res;
    uint32_t fMxcsrOut  = iemAImpl_cvttpd2pi_u128_worker(fMxCsrIn, &u64Res.ai32[0], &pSrc->ar64[0]);
             fMxcsrOut |= iemAImpl_cvttpd2pi_u128_worker(fMxCsrIn, &u64Res.ai32[1], &pSrc->ar64[1]);

    *pu64Dst = u64Res.u;
    return fMxcsrOut;
}
#endif


/**
 * CVTPI2PS
 */
#ifdef IEM_WITHOUT_ASSEMBLY
static uint32_t iemAImpl_cvtpi2ps_u128_worker(uint32_t fMxcsr, PRTFLOAT32U pr32Dst, int32_t i32Src)
{
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float32_t r32Res = i32_to_f32(i32Src, &SoftState);
    return iemSseSoftStateAndR32ToMxcsrAndIprtResult(&SoftState, r32Res, pr32Dst, fMxcsr);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtpi2ps_u128,(uint32_t fMxCsrIn, PX86XMMREG pDst, uint64_t u64Src))
{
    RTUINT64U uSrc = { u64Src };
    uint32_t fMxcsrOut  = iemAImpl_cvtpi2ps_u128_worker(fMxCsrIn, &pDst->ar32[0], uSrc.ai32[0]);
             fMxcsrOut |= iemAImpl_cvtpi2ps_u128_worker(fMxCsrIn, &pDst->ar32[1], uSrc.ai32[1]);
    return fMxcsrOut;
}
#endif


/**
 * CVTPI2PD
 */
#ifdef IEM_WITHOUT_ASSEMBLY
static uint32_t iemAImpl_cvtpi2pd_u128_worker(uint32_t fMxcsr, PRTFLOAT64U pr64Dst, int32_t i32Src)
{
    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    float64_t r64Res = i32_to_f64(i32Src, &SoftState);
    return iemSseSoftStateAndR64ToMxcsrAndIprtResult(&SoftState, r64Res, pr64Dst, fMxcsr);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtpi2pd_u128,(uint32_t fMxCsrIn, PX86XMMREG pDst, uint64_t u64Src))
{
    RTUINT64U uSrc = { u64Src };
    uint32_t fMxcsrOut  = iemAImpl_cvtpi2pd_u128_worker(fMxCsrIn, &pDst->ar64[0], uSrc.ai32[0]);
             fMxcsrOut |= iemAImpl_cvtpi2pd_u128_worker(fMxCsrIn, &pDst->ar64[1], uSrc.ai32[1]);
    return fMxcsrOut;
}
#endif


/**
 * CVTPS2PI
 */
#ifdef IEM_WITHOUT_ASSEMBLY
static uint32_t iemAImpl_cvtps2pi_u128_worker(uint32_t fMxcsr, int32_t *pi32Dst, PCRTFLOAT32U pr32Src)
{
    RTFLOAT32U r32Src;
    iemSsePrepareValueR32(&r32Src, fMxcsr, pr32Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    *pi32Dst = f32_to_i32(iemFpSoftF32FromIprt(&r32Src), SoftState.roundingMode, true /*exact*/, &SoftState);
    return fMxcsr | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvtps2pi_u128,(uint32_t fMxCsrIn, uint64_t *pu64Dst, uint64_t u64Src))
{
    RTUINT64U uDst;
    RTUINT64U uSrc = { u64Src };
    uint32_t fMxcsrOut  = iemAImpl_cvtps2pi_u128_worker(fMxCsrIn, &uDst.ai32[0], (PCRTFLOAT32U)&uSrc.au32[0]);
             fMxcsrOut |= iemAImpl_cvtps2pi_u128_worker(fMxCsrIn, &uDst.ai32[1], (PCRTFLOAT32U)&uSrc.au32[1]);
    *pu64Dst = uDst.u;
    return fMxcsrOut;
}
#endif


/**
 * CVTTPS2PI
 */
#ifdef IEM_WITHOUT_ASSEMBLY
static uint32_t iemAImpl_cvttps2pi_u128_worker(uint32_t fMxcsr, int32_t *pi32Dst, PCRTFLOAT32U pr32Src)
{
    RTFLOAT32U r32Src;
    iemSsePrepareValueR32(&r32Src, fMxcsr, pr32Src); /* The de-normal flag is not set. */

    softfloat_state_t SoftState = IEM_SOFTFLOAT_STATE_INITIALIZER_FROM_MXCSR(fMxcsr);
    *pi32Dst = f32_to_i32_r_minMag(iemFpSoftF32FromIprt(&r32Src), true /*exact*/, &SoftState);
    return fMxcsr | (SoftState.exceptionFlags & X86_MXCSR_XCPT_FLAGS);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_cvttps2pi_u128,(uint32_t fMxCsrIn, uint64_t *pu64Dst, uint64_t u64Src))
{
    RTUINT64U uDst;
    RTUINT64U uSrc = { u64Src };
    uint32_t fMxcsrOut  = iemAImpl_cvttps2pi_u128_worker(fMxCsrIn, &uDst.ai32[0], (PCRTFLOAT32U)&uSrc.au32[0]);
             fMxcsrOut |= iemAImpl_cvttps2pi_u128_worker(fMxCsrIn, &uDst.ai32[1], (PCRTFLOAT32U)&uSrc.au32[1]);
    *pu64Dst = uDst.u;
    return fMxcsrOut;
}
#endif

/**
 * RDRAND
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_rdrand_u16_fallback,(uint16_t *puDst, uint32_t *pEFlags))
{
    *puDst = 0;
    *pEFlags &= ~X86_EFL_STATUS_BITS;
    *pEFlags |= X86_EFL_CF;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_rdrand_u32_fallback,(uint32_t *puDst, uint32_t *pEFlags))
{
    *puDst = 0;
    *pEFlags &= ~X86_EFL_STATUS_BITS;
    *pEFlags |= X86_EFL_CF;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_rdrand_u64_fallback,(uint64_t *puDst, uint32_t *pEFlags))
{
    *puDst = 0;
    *pEFlags &= ~X86_EFL_STATUS_BITS;
    *pEFlags |= X86_EFL_CF;
}

/**
 * RDSEED
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_rdseed_u16_fallback,(uint16_t *puDst, uint32_t *pEFlags))
{
    *puDst = 0;
    *pEFlags &= ~X86_EFL_STATUS_BITS;
    *pEFlags |= X86_EFL_CF;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_rdseed_u32_fallback,(uint32_t *puDst, uint32_t *pEFlags))
{
    *puDst = 0;
    *pEFlags &= ~X86_EFL_STATUS_BITS;
    *pEFlags |= X86_EFL_CF;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_rdseed_u64_fallback,(uint64_t *puDst, uint32_t *pEFlags))
{
    *puDst = 0;
    *pEFlags &= ~X86_EFL_STATUS_BITS;
    *pEFlags |= X86_EFL_CF;
}


/**
 * SHA1NEXTE
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_sha1nexte_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    uint32_t u32Tmp = ASMRotateLeftU32(puDst->au32[3], 30);

    puDst->au32[0] = puSrc->au32[0];
    puDst->au32[1] = puSrc->au32[1];
    puDst->au32[2] = puSrc->au32[2];
    puDst->au32[3] = puSrc->au32[3] + u32Tmp;
}

/**
 * SHA1MSG1
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_sha1msg1_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    uint32_t u32W0 = puDst->au32[3];
    uint32_t u32W1 = puDst->au32[2];
    uint32_t u32W2 = puDst->au32[1];
    uint32_t u32W3 = puDst->au32[0];
    uint32_t u32W4 = puSrc->au32[3];
    uint32_t u32W5 = puSrc->au32[2];

    puDst->au32[3] = u32W2 ^ u32W0;
    puDst->au32[2] = u32W3 ^ u32W1;
    puDst->au32[1] = u32W4 ^ u32W2;
    puDst->au32[0] = u32W5 ^ u32W3;
}

/**
 * SHA1MSG2
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_sha1msg2_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    uint32_t u32W13 = puSrc->au32[2];
    uint32_t u32W14 = puSrc->au32[1];
    uint32_t u32W15 = puSrc->au32[0];
    uint32_t u32W16 = ASMRotateLeftU32(puDst->au32[3] ^ u32W13, 1);
    uint32_t u32W17 = ASMRotateLeftU32(puDst->au32[2] ^ u32W14, 1);
    uint32_t u32W18 = ASMRotateLeftU32(puDst->au32[1] ^ u32W15, 1);
    uint32_t u32W19 = ASMRotateLeftU32(puDst->au32[0] ^ u32W16, 1);

    puDst->au32[3] = u32W16;
    puDst->au32[2] = u32W17;
    puDst->au32[1] = u32W18;
    puDst->au32[0] = u32W19;
}

/**
 * SHA1RNDS4
 */
typedef IEM_DECL_IMPL_TYPE(uint32_t, FNIEMAIMPLSHA1RNDS4FN, (uint32_t u32B, uint32_t u32C, uint32_t u32D));
typedef FNIEMAIMPLSHA1RNDS4FN *PFNIEMAIMPLSHA1RNDS4FN;

static DECLCALLBACK(uint32_t) iemAImpl_sha1rnds4_f0(uint32_t u32B, uint32_t u32C, uint32_t u32D) RT_NOEXCEPT
{
    return (u32B & u32C) ^ (~u32B & u32D);
}

static DECLCALLBACK(uint32_t) iemAImpl_sha1rnds4_f1(uint32_t u32B, uint32_t u32C, uint32_t u32D) RT_NOEXCEPT
{
    return u32B ^ u32C ^ u32D;
}

static DECLCALLBACK(uint32_t) iemAImpl_sha1rnds4_f2(uint32_t u32B, uint32_t u32C, uint32_t u32D) RT_NOEXCEPT
{
    return (u32B & u32C) ^ (u32B & u32D) ^ (u32C & u32D);
}

static DECLCALLBACK(uint32_t) iemAImpl_sha1rnds4_f3(uint32_t u32B, uint32_t u32C, uint32_t u32D) RT_NOEXCEPT
{
    return u32B ^ u32C ^ u32D;
}

IEM_DECL_IMPL_DEF(void, iemAImpl_sha1rnds4_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    static uint32_t s_au32K[] = { UINT32_C(0x5a827999), UINT32_C(0x6ed9eba1), UINT32_C(0x8f1bbcdc), UINT32_C(0xca62c1d6) };
    static PFNIEMAIMPLSHA1RNDS4FN s_apfnFn[] = { iemAImpl_sha1rnds4_f0, iemAImpl_sha1rnds4_f1, iemAImpl_sha1rnds4_f2, iemAImpl_sha1rnds4_f3 };

    uint32_t au32A[5];
    uint32_t au32B[5];
    uint32_t au32C[5];
    uint32_t au32D[5];
    uint32_t au32E[5];
    uint32_t au32W[4];
    PFNIEMAIMPLSHA1RNDS4FN pfnFn = s_apfnFn[bEvil & 0x3];
    uint32_t u32K = s_au32K[bEvil & 0x3];

    au32A[0] = puDst->au32[3];
    au32B[0] = puDst->au32[2];
    au32C[0] = puDst->au32[1];
    au32D[0] = puDst->au32[0];
    for (uint32_t i = 0; i < RT_ELEMENTS(au32W); i++)
        au32W[i] = puSrc->au32[3 - i];

    /* Round 0 is a bit different than the other rounds. */
    au32A[1] = pfnFn(au32B[0], au32C[0], au32D[0]) + ASMRotateLeftU32(au32A[0], 5) + au32W[0] + u32K;
    au32B[1] = au32A[0];
    au32C[1] = ASMRotateLeftU32(au32B[0], 30);
    au32D[1] = au32C[0];
    au32E[1] = au32D[0];

    for (uint32_t i = 1; i <= 3; i++)
    {
        au32A[i + 1] = pfnFn(au32B[i], au32C[i], au32D[i]) + ASMRotateLeftU32(au32A[i], 5) + au32W[i] + au32E[i] + u32K;
        au32B[i + 1] = au32A[i];
        au32C[i + 1] = ASMRotateLeftU32(au32B[i], 30);
        au32D[i + 1] = au32C[i];
        au32E[i + 1] = au32D[i];
    }

    puDst->au32[3] = au32A[4];
    puDst->au32[2] = au32B[4];
    puDst->au32[1] = au32C[4];
    puDst->au32[0] = au32D[4];
}


/**
 * SHA256MSG1
 */
DECLINLINE(uint32_t) iemAImpl_sha256_lower_sigma0(uint32_t u32Val)
{
    return ASMRotateRightU32(u32Val, 7) ^ ASMRotateRightU32(u32Val, 18) ^ (u32Val >> 3);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_sha256msg1_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    uint32_t u32W4 = puSrc->au32[0];
    uint32_t u32W3 = puDst->au32[3];
    uint32_t u32W2 = puDst->au32[2];
    uint32_t u32W1 = puDst->au32[1];
    uint32_t u32W0 = puDst->au32[0];

    puDst->au32[3] = u32W3 + iemAImpl_sha256_lower_sigma0(u32W4);
    puDst->au32[2] = u32W2 + iemAImpl_sha256_lower_sigma0(u32W3);
    puDst->au32[1] = u32W1 + iemAImpl_sha256_lower_sigma0(u32W2);
    puDst->au32[0] = u32W0 + iemAImpl_sha256_lower_sigma0(u32W1);
}

/**
 * SHA256MSG2
 */
DECLINLINE(uint32_t) iemAImpl_sha256_lower_sigma1(uint32_t u32Val)
{
    return ASMRotateRightU32(u32Val, 17) ^ ASMRotateRightU32(u32Val, 19) ^ (u32Val >> 10);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_sha256msg2_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc))
{
    uint32_t u32W14 = puSrc->au32[2];
    uint32_t u32W15 = puSrc->au32[3];
    uint32_t u32W16 = puDst->au32[0] + iemAImpl_sha256_lower_sigma1(u32W14);
    uint32_t u32W17 = puDst->au32[1] + iemAImpl_sha256_lower_sigma1(u32W15);
    uint32_t u32W18 = puDst->au32[2] + iemAImpl_sha256_lower_sigma1(u32W16);
    uint32_t u32W19 = puDst->au32[3] + iemAImpl_sha256_lower_sigma1(u32W17);

    puDst->au32[3] = u32W19;
    puDst->au32[2] = u32W18;
    puDst->au32[1] = u32W17;
    puDst->au32[0] = u32W16;
}

/**
 * SHA256RNDS2
 */
DECLINLINE(uint32_t) iemAImpl_sha256_ch(uint32_t u32X, uint32_t u32Y, uint32_t u32Z)
{
    return (u32X & u32Y) ^ (~u32X & u32Z);
}

DECLINLINE(uint32_t) iemAImpl_sha256_maj(uint32_t u32X, uint32_t u32Y, uint32_t u32Z)
{
    return (u32X & u32Y) ^ (u32X & u32Z) ^ (u32Y & u32Z);
}

DECLINLINE(uint32_t) iemAImpl_sha256_upper_sigma0(uint32_t u32Val)
{
    return ASMRotateRightU32(u32Val, 2) ^ ASMRotateRightU32(u32Val, 13) ^ ASMRotateRightU32(u32Val, 22);
}

DECLINLINE(uint32_t) iemAImpl_sha256_upper_sigma1(uint32_t u32Val)
{
    return ASMRotateRightU32(u32Val, 6) ^ ASMRotateRightU32(u32Val, 11) ^ ASMRotateRightU32(u32Val, 25);
}

IEM_DECL_IMPL_DEF(void, iemAImpl_sha256rnds2_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, PCRTUINT128U puXmm0Constants))
{
    uint32_t au32A[3];
    uint32_t au32B[3];
    uint32_t au32C[3];
    uint32_t au32D[3];
    uint32_t au32E[3];
    uint32_t au32F[3];
    uint32_t au32G[3];
    uint32_t au32H[3];
    uint32_t au32WK[2];

    au32A[0]  = puSrc->au32[3];
    au32B[0]  = puSrc->au32[2];
    au32C[0]  = puDst->au32[3];
    au32D[0]  = puDst->au32[2];
    au32E[0]  = puSrc->au32[1];
    au32F[0]  = puSrc->au32[0];
    au32G[0]  = puDst->au32[1];
    au32H[0]  = puDst->au32[0];

    au32WK[0] = puXmm0Constants->au32[0];
    au32WK[1] = puXmm0Constants->au32[1];

    for (uint32_t i = 0; i < 2; i++)
    {
        au32A[i + 1] =    iemAImpl_sha256_ch(au32E[i], au32F[i], au32G[i])
                        + iemAImpl_sha256_upper_sigma1(au32E[i])
                        + au32WK[i]
                        + au32H[i]
                        + iemAImpl_sha256_maj(au32A[i], au32B[i], au32C[i])
                        + iemAImpl_sha256_upper_sigma0(au32A[i]);
        au32B[i + 1] = au32A[i];
        au32C[i + 1] = au32B[i];
        au32D[i + 1] = au32C[i];
        au32E[i + 1] =    iemAImpl_sha256_ch(au32E[i], au32F[i], au32G[i])
                        + iemAImpl_sha256_upper_sigma1(au32E[i])
                        + au32WK[i]
                        + au32H[i]
                        + au32D[i];
        au32F[i + 1] = au32E[i];
        au32G[i + 1] = au32F[i];
        au32H[i + 1] = au32G[i];
    }

    puDst->au32[3] = au32A[2];
    puDst->au32[2] = au32B[2];
    puDst->au32[1] = au32E[2];
    puDst->au32[0] = au32F[2];
}


/**
 * ADCX
 */
#define ADX_EMIT(a_Flag, a_Type, a_Max) \
    do \
    { \
        bool f = RT_BOOL(fEFlags & (a_Flag)); \
        a_Type uTmp = *puDst + uSrc; \
        if (uTmp < uSrc) \
            fEFlags |= (a_Flag); \
        else \
            fEFlags &= ~(a_Flag); \
        if (   uTmp == a_Max \
            && f) \
            fEFlags |= (a_Flag); \
        if (f) \
            uTmp++; \
        *puDst = uTmp; \
    } \
    while (0)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_adcx_u32_fallback,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    ADX_EMIT(X86_EFL_CF, uint32_t, UINT32_MAX);
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_adcx_u64_fallback,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    ADX_EMIT(X86_EFL_CF, uint64_t, UINT64_MAX);
    return fEFlags;
}

# if defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_adcx_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    ADX_EMIT(X86_EFL_CF, uint32_t, UINT32_MAX);
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_adcx_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    ADX_EMIT(X86_EFL_CF, uint64_t, UINT64_MAX);
    return fEFlags;
}

#endif


/**
 * ADOX
 */
IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_adox_u32_fallback,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    ADX_EMIT(X86_EFL_OF, uint32_t, UINT32_MAX);
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_adox_u64_fallback,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    ADX_EMIT(X86_EFL_OF, uint64_t, UINT64_MAX);
    return fEFlags;
}

# if defined(IEM_WITHOUT_ASSEMBLY)

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_adox_u32,(uint32_t fEFlags, uint32_t *puDst, uint32_t uSrc))
{
    ADX_EMIT(X86_EFL_OF, uint32_t, UINT32_MAX);
    return fEFlags;
}

IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_adox_u64,(uint32_t fEFlags, uint64_t *puDst, uint64_t uSrc))
{
    ADX_EMIT(X86_EFL_OF, uint64_t, UINT64_MAX);
    return fEFlags;
}

# endif


/**
 * MPSADBW
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_mpsadbw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc, uint8_t bEvil))
{
    uint8_t idxSrc2 = (bEvil & 0x3) * sizeof(uint32_t);
    uint8_t idxSrc1 = ((bEvil >> 2) & 0x1) * sizeof(uint32_t);
    int16_t ai16Src1[11];
    int16_t ai16Src2[4];

    for (uint32_t i = 0; i < RT_ELEMENTS(ai16Src1); i++)
        ai16Src1[i] = puDst->au8[idxSrc1 + i];

    for (uint32_t i = 0; i < RT_ELEMENTS(ai16Src2); i++)
        ai16Src2[i] = puSrc->au8[idxSrc2 + i];

    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au16); i++)
        puDst->au16[i] =   RT_ABS(ai16Src1[i]     - ai16Src2[0])
                         + RT_ABS(ai16Src1[i + 1] - ai16Src2[1])
                         + RT_ABS(ai16Src1[i + 2] - ai16Src2[2])
                         + RT_ABS(ai16Src1[i + 3] - ai16Src2[3]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vmpsadbw_u128_fallback,(PRTUINT128U puDst, PCRTUINT128U puSrc1, PCRTUINT128U puSrc2, uint8_t bEvil))
{
    uint8_t idxSrc2 = (bEvil & 0x3) * sizeof(uint32_t);
    uint8_t idxSrc1 = ((bEvil >> 2) & 0x1) * sizeof(uint32_t);
    int16_t ai16Src1[11];
    int16_t ai16Src2[4];

    for (uint32_t i = 0; i < RT_ELEMENTS(ai16Src1); i++)
        ai16Src1[i] = puSrc1->au8[idxSrc1 + i];

    for (uint32_t i = 0; i < RT_ELEMENTS(ai16Src2); i++)
        ai16Src2[i] = puSrc2->au8[idxSrc2 + i];

    for (uint8_t i = 0; i < RT_ELEMENTS(puDst->au16); i++)
        puDst->au16[i] =   RT_ABS(ai16Src1[i]     - ai16Src2[0])
                         + RT_ABS(ai16Src1[i + 1] - ai16Src2[1])
                         + RT_ABS(ai16Src1[i + 2] - ai16Src2[2])
                         + RT_ABS(ai16Src1[i + 3] - ai16Src2[3]);
}


IEM_DECL_IMPL_DEF(void, iemAImpl_vmpsadbw_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bEvil))
{
    RTUINT256U const uSrc1 = *puSrc1; /* Might overlap with destination. */
    RTUINT256U const uSrc2 = *puSrc2;
    ASMCompilerBarrier();
    iemAImpl_vmpsadbw_u128_fallback(&puDst->au128[0], &uSrc1.au128[0], &uSrc2.au128[0], bEvil);
    iemAImpl_vmpsadbw_u128_fallback(&puDst->au128[1], &uSrc1.au128[1], &uSrc2.au128[1], bEvil >> 3);
}


/**
 * VPERM2I128
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vperm2i128_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bImm))
{
    if (bImm & RT_BIT(3))
    {
        puDst->au64[0] = 0;
        puDst->au64[1] = 0;
    }
    else
    {
        switch (bImm & 0x3)
        {
            case 0:
                puDst->au64[0] = puSrc1->au64[0];
                puDst->au64[1] = puSrc1->au64[1];
                break;
            case 1:
                puDst->au64[0] = puSrc1->au64[2];
                puDst->au64[1] = puSrc1->au64[3];
                break;
            case 2:
                puDst->au64[0] = puSrc2->au64[0];
                puDst->au64[1] = puSrc2->au64[1];
                break;
            case 3:
                puDst->au64[0] = puSrc2->au64[2];
                puDst->au64[1] = puSrc2->au64[3];
                break;
        }
    }

    if (bImm & RT_BIT(7))
    {
        puDst->au64[2] = 0;
        puDst->au64[3] = 0;
    }
    else
    {
        switch ((bImm >> 4) & 0x3)
        {
            case 0:
                puDst->au64[2] = puSrc1->au64[0];
                puDst->au64[3] = puSrc1->au64[1];
                break;
            case 1:
                puDst->au64[2] = puSrc1->au64[2];
                puDst->au64[3] = puSrc1->au64[3];
                break;
            case 2:
                puDst->au64[2] = puSrc2->au64[0];
                puDst->au64[3] = puSrc2->au64[1];
                break;
            case 3:
                puDst->au64[2] = puSrc2->au64[2];
                puDst->au64[3] = puSrc2->au64[3];
                break;
        }
    }
}


/**
 * VPERM2F128
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vperm2f128_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2, uint8_t bImm))
{
    iemAImpl_vperm2i128_u256_fallback(puDst, puSrc1, puSrc2, bImm);
}


/**
 * [V]DPPS
 */
static uint32_t iemAImpl_dpps_u128_worker(PRTFLOAT32U par32Dst, uint32_t fMxcsr, PCRTFLOAT32U par32Src1, PCRTFLOAT32U par32Src2, uint8_t bImm)
{
    uint32_t fMxcsrOut = 0;

    RTFLOAT32U ar32Tmp[4]; RT_ZERO(ar32Tmp); /* This will set the values to 0.0 */
    if (bImm & RT_BIT(4))
        fMxcsrOut |= iemAImpl_mulps_u128_worker(&ar32Tmp[0], fMxcsr, &par32Src1[0], &par32Src2[0]);
    if (bImm & RT_BIT(5))
        fMxcsrOut |= iemAImpl_mulps_u128_worker(&ar32Tmp[1], fMxcsr, &par32Src1[1], &par32Src2[1]);
    if (bImm & RT_BIT(6))
        fMxcsrOut |= iemAImpl_mulps_u128_worker(&ar32Tmp[2], fMxcsr, &par32Src1[2], &par32Src2[2]);
    if (bImm & RT_BIT(7))
        fMxcsrOut |= iemAImpl_mulps_u128_worker(&ar32Tmp[3], fMxcsr, &par32Src1[3], &par32Src2[3]);

    RTFLOAT32U ar32TmpRes[2];
    fMxcsrOut |= iemAImpl_addps_u128_worker(&ar32TmpRes[0], fMxcsr, &ar32Tmp[0], &ar32Tmp[1]);
    fMxcsrOut |= iemAImpl_addps_u128_worker(&ar32TmpRes[1], fMxcsr, &ar32Tmp[2], &ar32Tmp[3]);

    RTFLOAT32U r32Res;
    fMxcsrOut |= iemAImpl_addps_u128_worker(&r32Res, fMxcsr, &ar32TmpRes[0], &ar32TmpRes[1]);

    if (bImm & RT_BIT(0))
        par32Dst[0] = r32Res;
    else
        par32Dst[0] = RTFLOAT32U_INIT_ZERO(0);

    if (bImm & RT_BIT(1))
        par32Dst[1] = r32Res;
    else
        par32Dst[1] = RTFLOAT32U_INIT_ZERO(0);

    if (bImm & RT_BIT(2))
        par32Dst[2] = r32Res;
    else
        par32Dst[2] = RTFLOAT32U_INIT_ZERO(0);

    if (bImm & RT_BIT(3))
        par32Dst[3] = r32Res;
    else
        par32Dst[3] = RTFLOAT32U_INIT_ZERO(0);

    return fMxcsrOut;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_dpps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bImm))
{
    return iemAImpl_dpps_u128_worker(&puDst->ar32[0], uMxCsrIn, &pSrc->uSrc1.ar32[0], &pSrc->uSrc2.ar32[0], bImm);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vdpps_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bImm))
{
    return iemAImpl_dpps_u128_worker(&puDst->ar32[0], uMxCsrIn, &pSrc->uSrc1.ar32[0], &pSrc->uSrc2.ar32[0], bImm);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vdpps_u256_fallback,(uint32_t uMxCsrIn, PX86YMMREG puDst, PCIEMMEDIAF2YMMSRC pSrc, uint8_t bImm))
{
    uint32_t fMxcsrOut =  iemAImpl_dpps_u128_worker(&puDst->ar32[0], uMxCsrIn, &pSrc->uSrc1.ar32[0], &pSrc->uSrc2.ar32[0], bImm);
             fMxcsrOut |= iemAImpl_dpps_u128_worker(&puDst->ar32[4], uMxCsrIn, &pSrc->uSrc1.ar32[4], &pSrc->uSrc2.ar32[4], bImm);
    return fMxcsrOut;
}


/**
 * [V]DPPD
 */
static uint32_t iemAImpl_dppd_u128_worker(PRTFLOAT64U par64Dst, uint32_t fMxcsr, PCRTFLOAT64U par64Src1, PCRTFLOAT64U par64Src2, uint8_t bImm)
{
    uint32_t fMxcsrOut = 0;

    RTFLOAT64U ar64Tmp[2]; RT_ZERO(ar64Tmp); /* This will set the values to 0.0 */
    if (bImm & RT_BIT(4))
        fMxcsrOut |= iemAImpl_mulpd_u128_worker(&ar64Tmp[0], fMxcsr, &par64Src1[0], &par64Src2[0]);
    if (bImm & RT_BIT(5))
        fMxcsrOut |= iemAImpl_mulpd_u128_worker(&ar64Tmp[1], fMxcsr, &par64Src1[1], &par64Src2[1]);

    RTFLOAT64U r64Res;
    fMxcsrOut |= iemAImpl_addpd_u128_worker(&r64Res, fMxcsr, &ar64Tmp[0], &ar64Tmp[1]);

    if (bImm & RT_BIT(0))
        par64Dst[0] = r64Res;
    else
        par64Dst[0] = RTFLOAT64U_INIT_ZERO(0);

    if (bImm & RT_BIT(1))
        par64Dst[1] = r64Res;
    else
        par64Dst[1] = RTFLOAT64U_INIT_ZERO(0);

    return fMxcsrOut;
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_dppd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bImm))
{
    return iemAImpl_dppd_u128_worker(&puDst->ar64[0], uMxCsrIn, &pSrc->uSrc1.ar64[0], &pSrc->uSrc2.ar64[0], bImm);
}


IEM_DECL_IMPL_DEF(uint32_t, iemAImpl_vdppd_u128_fallback,(uint32_t uMxCsrIn, PX86XMMREG puDst, PCIEMMEDIAF2XMMSRC pSrc, uint8_t bImm))
{
    return iemAImpl_dppd_u128_worker(&puDst->ar64[0], uMxCsrIn, &pSrc->uSrc1.ar64[0], &pSrc->uSrc2.ar64[0], bImm);
}


/**
 * VPERMQ
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpermq_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t bImm))
{
    puDst->au64[0] = puSrc->au64[bImm        & 0x3];
    puDst->au64[1] = puSrc->au64[(bImm >> 2) & 0x3];
    puDst->au64[2] = puSrc->au64[(bImm >> 4) & 0x3];
    puDst->au64[3] = puSrc->au64[(bImm >> 6) & 0x3];
}


/**
 * VPERMPD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpermpd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc, uint8_t bImm))
{
    puDst->au64[0] = puSrc->au64[bImm        & 0x3];
    puDst->au64[1] = puSrc->au64[(bImm >> 2) & 0x3];
    puDst->au64[2] = puSrc->au64[(bImm >> 4) & 0x3];
    puDst->au64[3] = puSrc->au64[(bImm >> 6) & 0x3];
}


/**
 * VPERMD
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpermd_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au32[0] = puSrc2->au32[puSrc1->au32[0] & 0x3];
    puDst->au32[1] = puSrc2->au32[puSrc1->au32[1] & 0x3];
    puDst->au32[2] = puSrc2->au32[puSrc1->au32[2] & 0x3];
    puDst->au32[3] = puSrc2->au32[puSrc1->au32[3] & 0x3];
    puDst->au32[4] = puSrc2->au32[puSrc1->au32[4] & 0x3];
    puDst->au32[5] = puSrc2->au32[puSrc1->au32[5] & 0x3];
    puDst->au32[6] = puSrc2->au32[puSrc1->au32[6] & 0x3];
    puDst->au32[7] = puSrc2->au32[puSrc1->au32[7] & 0x3];
}


/**
 * VPERMPS
 */
IEM_DECL_IMPL_DEF(void, iemAImpl_vpermps_u256_fallback,(PRTUINT256U puDst, PCRTUINT256U puSrc1, PCRTUINT256U puSrc2))
{
    puDst->au32[0] = puSrc2->au32[puSrc1->au32[0] & 0x3];
    puDst->au32[1] = puSrc2->au32[puSrc1->au32[1] & 0x3];
    puDst->au32[2] = puSrc2->au32[puSrc1->au32[2] & 0x3];
    puDst->au32[3] = puSrc2->au32[puSrc1->au32[3] & 0x3];
    puDst->au32[4] = puSrc2->au32[puSrc1->au32[4] & 0x3];
    puDst->au32[5] = puSrc2->au32[puSrc1->au32[5] & 0x3];
    puDst->au32[6] = puSrc2->au32[puSrc1->au32[6] & 0x3];
    puDst->au32[7] = puSrc2->au32[puSrc1->au32[7] & 0x3];
}
