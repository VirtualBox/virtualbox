/* $Id: bs3-cpu-generated-1.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - bs3-cpu-generated-1, common header file.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_bootsectors_bs3_cpu_generated_1_h
#define VBOX_INCLUDED_SRC_bootsectors_bs3_cpu_generated_1_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <bs3kit.h>
#include <iprt/assert.h>


/**
 * Operand details.
 *
 * Currently simply using the encoding from the reference manuals.
 */
typedef enum BS3CG1OP
{
    BS3CG1OP_INVALID = 0,

    BS3CG1OP_Eb,
    BS3CG1OP_Ed,
    BS3CG1OP_Ed_WO,
    BS3CG1OP_Eq,
    BS3CG1OP_Eq_WO,
    BS3CG1OP_Ev,
    BS3CG1OP_Qq,
    BS3CG1OP_Qq_WO,
    BS3CG1OP_Wss,
    BS3CG1OP_Wss_WO,
    BS3CG1OP_Wsd,
    BS3CG1OP_Wsd_WO,
    BS3CG1OP_Wps,
    BS3CG1OP_Wps_WO,
    BS3CG1OP_Wpd,
    BS3CG1OP_Wpd_WO,
    BS3CG1OP_Wdq,
    BS3CG1OP_Wdq_WO,
    BS3CG1OP_Wq,
    BS3CG1OP_Wq_WO,
    BS3CG1OP_WqZxReg_WO,
    BS3CG1OP_Wx,
    BS3CG1OP_Wx_WO,

    BS3CG1OP_Gb,
    BS3CG1OP_Gv,
    BS3CG1OP_Gv_RO,
    BS3CG1OP_HssHi,
    BS3CG1OP_HsdHi,
    BS3CG1OP_HqHi,
    BS3CG1OP_Nq,
    BS3CG1OP_Pd,
    BS3CG1OP_PdZx_WO,
    BS3CG1OP_Pq,
    BS3CG1OP_Pq_WO,
    BS3CG1OP_Uq,
    BS3CG1OP_UqHi,
    BS3CG1OP_Uss,
    BS3CG1OP_Uss_WO,
    BS3CG1OP_Usd,
    BS3CG1OP_Usd_WO,
    BS3CG1OP_Vd,
    BS3CG1OP_Vd_WO,
    BS3CG1OP_VdZx_WO,
    BS3CG1OP_Vss,
    BS3CG1OP_Vss_WO,
    BS3CG1OP_VssZx_WO,
    BS3CG1OP_Vsd,
    BS3CG1OP_Vsd_WO,
    BS3CG1OP_VsdZx_WO,
    BS3CG1OP_Vps,
    BS3CG1OP_Vps_WO,
    BS3CG1OP_Vpd,
    BS3CG1OP_Vpd_WO,
    BS3CG1OP_Vq,
    BS3CG1OP_Vq_WO,
    BS3CG1OP_Vdq,
    BS3CG1OP_Vdq_WO,
    BS3CG1OP_VqHi,
    BS3CG1OP_VqHi_WO,
    BS3CG1OP_VqZx_WO,
    BS3CG1OP_Vx,
    BS3CG1OP_Vx_WO,

    BS3CG1OP_Ib,
    BS3CG1OP_Iz,

    BS3CG1OP_AL,
    BS3CG1OP_rAX,

    BS3CG1OP_Ma,
    BS3CG1OP_Mb_RO,
    BS3CG1OP_Md,
    BS3CG1OP_Md_RO,
    BS3CG1OP_Md_WO,
    BS3CG1OP_Mdq,
    BS3CG1OP_Mdq_WO,
    BS3CG1OP_Mq,
    BS3CG1OP_Mq_WO,
    BS3CG1OP_Mps_WO,
    BS3CG1OP_Mpd_WO,
    BS3CG1OP_Mx,
    BS3CG1OP_Mx_WO,

    BS3CG1OP_END
} BS3CG1OP;
/** Pointer to a const operand enum. */
typedef const BS3CG1OP BS3_FAR *PCBS3CG1OP;


/**
 * Instruction encoding format.
 *
 * This duplicates some of the info in the operand array, however it makes it
 * easier to figure out encoding variations.
 */
typedef enum BS3CG1ENC
{
    BS3CG1ENC_INVALID = 0,

    BS3CG1ENC_MODRM_Eb_Gb,
    BS3CG1ENC_MODRM_Ev_Gv,
    BS3CG1ENC_MODRM_Ed_WO_Pd_WZ,
    BS3CG1ENC_MODRM_Eq_WO_Pq_WNZ,
    BS3CG1ENC_MODRM_Ed_WO_Vd_WZ,
    BS3CG1ENC_MODRM_Eq_WO_Vq_WNZ,
    BS3CG1ENC_MODRM_Pq_WO_Qq,
    BS3CG1ENC_MODRM_Wss_WO_Vss,
    BS3CG1ENC_MODRM_Wsd_WO_Vsd,
    BS3CG1ENC_MODRM_Wps_WO_Vps,
    BS3CG1ENC_MODRM_Wpd_WO_Vpd,
    BS3CG1ENC_MODRM_WqZxReg_WO_Vq,

    BS3CG1ENC_MODRM_Gb_Eb,
    BS3CG1ENC_MODRM_Gv_Ev,
    BS3CG1ENC_MODRM_Gv_RO_Ma, /**< bound instruction */
    BS3CG1ENC_MODRM_Pq_WO_Uq,
    BS3CG1ENC_MODRM_PdZx_WO_Ed_WZ,
    BS3CG1ENC_MODRM_Pq_WO_Eq_WNZ,
    BS3CG1ENC_MODRM_VdZx_WO_Ed_WZ,
    BS3CG1ENC_MODRM_Vq_Mq,
    BS3CG1ENC_MODRM_Vq_WO_UqHi,
    BS3CG1ENC_MODRM_Vq_WO_Mq,
    BS3CG1ENC_MODRM_VqHi_WO_Uq,
    BS3CG1ENC_MODRM_VqHi_WO_Mq,
    BS3CG1ENC_MODRM_VqZx_WO_Eq_WNZ,
    BS3CG1ENC_MODRM_Vdq_WO_Mdq,
    BS3CG1ENC_MODRM_Vdq_WO_Wdq,
    BS3CG1ENC_MODRM_Vpd_WO_Wpd,
    BS3CG1ENC_MODRM_Vps_WO_Wps,
    BS3CG1ENC_MODRM_VssZx_WO_Wss,
    BS3CG1ENC_MODRM_VsdZx_WO_Wsd,
    BS3CG1ENC_MODRM_VqZx_WO_Wq,
    BS3CG1ENC_MODRM_VqZx_WO_Nq,
    BS3CG1ENC_MODRM_Mb_RO,
    BS3CG1ENC_MODRM_Md_RO,
    BS3CG1ENC_MODRM_Md_WO,
    BS3CG1ENC_MODRM_Mdq_WO_Vdq,
    BS3CG1ENC_MODRM_Mq_WO_Pq,
    BS3CG1ENC_MODRM_Mq_WO_Vq,
    BS3CG1ENC_MODRM_Mq_WO_VqHi,
    BS3CG1ENC_MODRM_Mps_WO_Vps,
    BS3CG1ENC_MODRM_Mpd_WO_Vpd,

    BS3CG1ENC_VEX_MODRM_Vd_WO_Ed_WZ,
    BS3CG1ENC_VEX_MODRM_Vps_WO_Wps,
    BS3CG1ENC_VEX_MODRM_Vpd_WO_Wpd,
    BS3CG1ENC_VEX_MODRM_Vss_WO_HssHi_Uss,
    BS3CG1ENC_VEX_MODRM_Vsd_WO_HsdHi_Usd,
    BS3CG1ENC_VEX_MODRM_Vq_WO_Eq_WNZ,
    BS3CG1ENC_VEX_MODRM_Vq_WO_HqHi_UqHi,
    BS3CG1ENC_VEX_MODRM_Vq_WO_HqHi_Mq,
    BS3CG1ENC_VEX_MODRM_Vq_WO_Wq,
    BS3CG1ENC_VEX_MODRM_VssZx_WO_Md,
    BS3CG1ENC_VEX_MODRM_VsdZx_WO_Mq,
    BS3CG1ENC_VEX_MODRM_Vx_WO_Mx_L0,
    BS3CG1ENC_VEX_MODRM_Vx_WO_Mx_L1,
    BS3CG1ENC_VEX_MODRM_Vx_WO_Wx,
    BS3CG1ENC_VEX_MODRM_Ed_WO_Vd_WZ,
    BS3CG1ENC_VEX_MODRM_Eq_WO_Vq_WNZ,
    BS3CG1ENC_VEX_MODRM_Md_WO,
    BS3CG1ENC_VEX_MODRM_Mq_WO_Vq,
    BS3CG1ENC_VEX_MODRM_Md_WO_Vss,
    BS3CG1ENC_VEX_MODRM_Mq_WO_Vsd,
    BS3CG1ENC_VEX_MODRM_Mps_WO_Vps,
    BS3CG1ENC_VEX_MODRM_Mpd_WO_Vpd,
    BS3CG1ENC_VEX_MODRM_Mx_WO_Vx,
    BS3CG1ENC_VEX_MODRM_Uss_WO_HssHi_Vss,
    BS3CG1ENC_VEX_MODRM_Usd_WO_HsdHi_Vsd,
    BS3CG1ENC_VEX_MODRM_Wps_WO_Vps,
    BS3CG1ENC_VEX_MODRM_Wpd_WO_Vpd,
    BS3CG1ENC_VEX_MODRM_Wq_WO_Vq,
    BS3CG1ENC_VEX_MODRM_Wx_WO_Vx,

    BS3CG1ENC_FIXED,
    BS3CG1ENC_FIXED_AL_Ib,
    BS3CG1ENC_FIXED_rAX_Iz,


    BS3CG1ENC_MODRM_MOD_EQ_3, /**< Unused or invalid instruction. */
    BS3CG1ENC_MODRM_MOD_NE_3, /**< Unused or invalid instruction. */
    //BS3CG1ENC_VEX_FIXED,          /**< Unused or invalid instruction. */
    BS3CG1ENC_VEX_MODRM_MOD_EQ_3, /**< Unused or invalid instruction. */
    BS3CG1ENC_VEX_MODRM_MOD_NE_3, /**< Unused or invalid instruction. */
    BS3CG1ENC_VEX_MODRM,          /**< Unused or invalid instruction. */

    BS3CG1ENC_END
} BS3CG1ENC;


/**
 * Prefix sensitivitiy kind.
 */
typedef enum BS3CG1PFXKIND
{
    BS3CG1PFXKIND_INVALID = 0,

    BS3CG1PFXKIND_NO_F2_F3_66,           /**< No 66, F2 or F3 prefixes allowed as that would alter the meaning. */
    BS3CG1PFXKIND_REQ_F2,                /**< Requires F2 (REPNE) prefix as part of the instr encoding. */
    BS3CG1PFXKIND_REQ_F3,                /**< Requires F3 (REPE) prefix as part of the instr encoding. */
    BS3CG1PFXKIND_REQ_66,                /**< Requires 66 (OP SIZE) prefix as part of the instr encoding.  */

    /** @todo more work to be done here...   */
    BS3CG1PFXKIND_MODRM,
    BS3CG1PFXKIND_MODRM_NO_OP_SIZES,

    BS3CG1PFXKIND_END
} BS3CG1PFXKIND;

/**
 * CPU selection or CPU ID.
 */
typedef enum BS3CG1CPU
{
    /** Works with an CPU. */
    BS3CG1CPU_ANY = 0,
    BS3CG1CPU_GE_80186,
    BS3CG1CPU_GE_80286,
    BS3CG1CPU_GE_80386,
    BS3CG1CPU_GE_80486,
    BS3CG1CPU_GE_Pentium,

    BS3CG1CPU_MMX,
    BS3CG1CPU_SSE,
    BS3CG1CPU_SSE2,
    BS3CG1CPU_SSE3,
    BS3CG1CPU_SSE4_1,
    BS3CG1CPU_AVX,
    BS3CG1CPU_AVX2,
    BS3CG1CPU_CLFSH,
    BS3CG1CPU_CLFLUSHOPT,

    BS3CG1CPU_END
} BS3CG1CPU;


/**
 * SSE & AVX exception types.
 */
typedef enum BS3CG1XCPTTYPE
{
    BS3CG1XCPTTYPE_NONE = 0,
    /* SSE: */
    BS3CG1XCPTTYPE_1,
    BS3CG1XCPTTYPE_2,
    BS3CG1XCPTTYPE_3,
    BS3CG1XCPTTYPE_4,
    BS3CG1XCPTTYPE_4UA,
    BS3CG1XCPTTYPE_5,
    BS3CG1XCPTTYPE_5LZ,
    BS3CG1XCPTTYPE_6,
    BS3CG1XCPTTYPE_7,
    BS3CG1XCPTTYPE_7LZ,
    BS3CG1XCPTTYPE_8,
    BS3CG1XCPTTYPE_11,
    BS3CG1XCPTTYPE_12,
    /* EVEX: */
    BS3CG1XCPTTYPE_E1,
    BS3CG1XCPTTYPE_E1NF,
    BS3CG1XCPTTYPE_E2,
    BS3CG1XCPTTYPE_E3,
    BS3CG1XCPTTYPE_E3NF,
    BS3CG1XCPTTYPE_E4,
    BS3CG1XCPTTYPE_E4NF,
    BS3CG1XCPTTYPE_E5,
    BS3CG1XCPTTYPE_E5NF,
    BS3CG1XCPTTYPE_E6,
    BS3CG1XCPTTYPE_E6NF,
    BS3CG1XCPTTYPE_E7NF,
    BS3CG1XCPTTYPE_E9,
    BS3CG1XCPTTYPE_E9NF,
    BS3CG1XCPTTYPE_E10,
    BS3CG1XCPTTYPE_E11,
    BS3CG1XCPTTYPE_E12,
    BS3CG1XCPTTYPE_E12NF,
    BS3CG1XCPTTYPE_END
} BS3CG1XCPTTYPE;
AssertCompile(BS3CG1XCPTTYPE_END <= 32);


/**
 * Generated instruction info.
 */
typedef struct BS3CG1INSTR
{
    /** The opcode size.   */
    uint32_t    cbOpcodes : 2;
    /** The number of operands.   */
    uint32_t    cOperands : 2;
    /** The length of the mnemonic. */
    uint32_t    cchMnemonic : 4;
    /** Whether to advance the mnemonic array pointer. */
    uint32_t    fAdvanceMnemonic : 1;
    /** Offset into g_abBs3Cg1Tests of the first test. */
    uint32_t    offTests : 23;
    /** BS3CG1ENC values. */
    uint32_t    enmEncoding : 10;
    /** The VEX, EVEX or XOP opcode map number (VEX.mmmm). */
    uint32_t    uOpcodeMap : 4;
    /** BS3CG1PFXKIND values. */
    uint32_t    enmPrefixKind : 4;
    /** CPU test / CPU ID bit test (BS3CG1CPU). */
    uint32_t    enmCpuTest : 6;
    /** Exception type (BS3CG1XCPTTYPE)   */
    uint32_t    enmXcptType : 5;
    /** Currently unused bits. */
    uint32_t    uUnused : 3;
    /** BS3CG1INSTR_F_XXX. */
    uint32_t    fFlags;
} BS3CG1INSTR;
AssertCompileSize(BS3CG1INSTR, 12);
/** Pointer to a const instruction. */
typedef BS3CG1INSTR const BS3_FAR *PCBS3CG1INSTR;


/** @name BS3CG1INSTR_F_XXX
 * @{ */
/** Defaults to SS rather than DS. */
#define BS3CG1INSTR_F_DEF_SS                UINT32_C(0x00000001)
/** Invalid instruction in 64-bit mode. */
#define BS3CG1INSTR_F_INVALID_64BIT         UINT32_C(0x00000002)
/** Unused instruction. */
#define BS3CG1INSTR_F_UNUSED                UINT32_C(0x00000004)
/** Invalid instruction. */
#define BS3CG1INSTR_F_INVALID               UINT32_C(0x00000008)
/** Only intel does full ModR/M(, ++) decoding for invalid instruction.
 * Always used with BS3CG1INSTR_F_INVALID or BS3CG1INSTR_F_UNUSED. */
#define BS3CG1INSTR_F_INTEL_DECODES_INVALID UINT32_C(0x00000010)
/** VEX.L must be zero (IEMOPHINT_VEX_L_ZERO). */
#define BS3CG1INSTR_F_VEX_L_ZERO            UINT32_C(0x00000020)
/** VEX.L is ignored (IEMOPHINT_VEX_L_IGNORED). */
#define BS3CG1INSTR_F_VEX_L_IGNORED         UINT32_C(0x00000040)
/** @} */


/**
 * Test header.
 */
typedef struct BS3CG1TESTHDR
{
    /** The size of the selector program in bytes.
     * This is also the offset of the input context modification program.  */
    uint32_t    cbSelector : 8;
    /** The size of the input context modification program in bytes.
     * This immediately follows the selector program.  */
    uint32_t    cbInput    : 12;
    /** The size of the output context modification program in bytes.
     * This immediately follows the input context modification program.  The
     * program takes the result of the input program as starting point. */
    uint32_t    cbOutput   : 11;
    /** Indicates whether this is the last test or not. */
    uint32_t    fLast      : 1;
} BS3CG1TESTHDR;
AssertCompileSize(BS3CG1TESTHDR, 4);
/** Pointer to a const test header. */
typedef BS3CG1TESTHDR const BS3_FAR *PCBS3CG1TESTHDR;

/** @name Opcode format for the BS3CG1 context modifier.
 *
 * Used by both the input and output context programs.
 *
 * The most common operations are encoded as a single byte opcode followed by
 * one or more immediate bytes with data.
 *
 * @{ */
#define BS3CG1_CTXOP_SIZE_MASK      UINT8_C(0x07)
#define BS3CG1_CTXOP_1_BYTE         UINT8_C(0x00)
#define BS3CG1_CTXOP_2_BYTES        UINT8_C(0x01)
#define BS3CG1_CTXOP_4_BYTES        UINT8_C(0x02)
#define BS3CG1_CTXOP_8_BYTES        UINT8_C(0x03)
#define BS3CG1_CTXOP_16_BYTES       UINT8_C(0x04)
#define BS3CG1_CTXOP_32_BYTES       UINT8_C(0x05)
#define BS3CG1_CTXOP_12_BYTES       UINT8_C(0x06)
#define BS3CG1_CTXOP_SIZE_ESC       UINT8_C(0x07)   /**< Separate byte encoding the value size following any destination escape byte. */

#define BS3CG1_CTXOP_DST_MASK       UINT8_C(0x18)
#define BS3CG1_CTXOP_OP1            UINT8_C(0x00)
#define BS3CG1_CTXOP_OP2            UINT8_C(0x08)
#define BS3CG1_CTXOP_EFL            UINT8_C(0x10)
#define BS3CG1_CTXOP_DST_ESC        UINT8_C(0x18)   /**< Separate byte giving the destination follows immediately. */

#define BS3CG1_CTXOP_SIGN_EXT       UINT8_C(0x20)   /**< Whether to sign-extend (set) the immediate value. */

#define BS3CG1_CTXOP_OPERATOR_MASK  UINT8_C(0xc0)
#define BS3CG1_CTXOP_ASSIGN         UINT8_C(0x00)   /**< Simple assignment operator (=) */
#define BS3CG1_CTXOP_OR             UINT8_C(0x40)   /**< OR assignment operator (|=). */
#define BS3CG1_CTXOP_AND            UINT8_C(0x80)   /**< AND assignment operator (&=). */
#define BS3CG1_CTXOP_AND_INV        UINT8_C(0xc0)   /**< AND assignment operator of the inverted value (&~=). */
/** @} */

/**
 * Escaped destination values
 *
 * These are just uppercased versions of TestInOut.kdFields, where dots are
 * replaced by underscores.
 */
typedef enum BS3CG1DST
{
    BS3CG1DST_INVALID = 0,
    /* Operands. */
    BS3CG1DST_OP1,
    BS3CG1DST_OP2,
    BS3CG1DST_OP3,
    BS3CG1DST_OP4,
    /* Flags. */
    BS3CG1DST_EFL,
    BS3CG1DST_EFL_UNDEF, /**< Special field only valid in output context modifiers: EFLAGS |= Value & Ouput.EFLAGS; */
    /* 8-bit GPRs. */
    BS3CG1DST_AL,
    BS3CG1DST_CL,
    BS3CG1DST_DL,
    BS3CG1DST_BL,
    BS3CG1DST_AH,
    BS3CG1DST_CH,
    BS3CG1DST_DH,
    BS3CG1DST_BH,
    BS3CG1DST_SPL,
    BS3CG1DST_BPL,
    BS3CG1DST_SIL,
    BS3CG1DST_DIL,
    BS3CG1DST_R8L,
    BS3CG1DST_R9L,
    BS3CG1DST_R10L,
    BS3CG1DST_R11L,
    BS3CG1DST_R12L,
    BS3CG1DST_R13L,
    BS3CG1DST_R14L,
    BS3CG1DST_R15L,
    /* 16-bit GPRs. */
    BS3CG1DST_AX,
    BS3CG1DST_CX,
    BS3CG1DST_DX,
    BS3CG1DST_BX,
    BS3CG1DST_SP,
    BS3CG1DST_BP,
    BS3CG1DST_SI,
    BS3CG1DST_DI,
    BS3CG1DST_R8W,
    BS3CG1DST_R9W,
    BS3CG1DST_R10W,
    BS3CG1DST_R11W,
    BS3CG1DST_R12W,
    BS3CG1DST_R13W,
    BS3CG1DST_R14W,
    BS3CG1DST_R15W,
    /* 32-bit GPRs. */
    BS3CG1DST_EAX,
    BS3CG1DST_ECX,
    BS3CG1DST_EDX,
    BS3CG1DST_EBX,
    BS3CG1DST_ESP,
    BS3CG1DST_EBP,
    BS3CG1DST_ESI,
    BS3CG1DST_EDI,
    BS3CG1DST_R8D,
    BS3CG1DST_R9D,
    BS3CG1DST_R10D,
    BS3CG1DST_R11D,
    BS3CG1DST_R12D,
    BS3CG1DST_R13D,
    BS3CG1DST_R14D,
    BS3CG1DST_R15D,
    /* 64-bit GPRs. */
    BS3CG1DST_RAX,
    BS3CG1DST_RCX,
    BS3CG1DST_RDX,
    BS3CG1DST_RBX,
    BS3CG1DST_RSP,
    BS3CG1DST_RBP,
    BS3CG1DST_RSI,
    BS3CG1DST_RDI,
    BS3CG1DST_R8,
    BS3CG1DST_R9,
    BS3CG1DST_R10,
    BS3CG1DST_R11,
    BS3CG1DST_R12,
    BS3CG1DST_R13,
    BS3CG1DST_R14,
    BS3CG1DST_R15,
    /* 16-bit, 32-bit or 64-bit registers according to operand size. */
    BS3CG1DST_OZ_RAX,
    BS3CG1DST_OZ_RCX,
    BS3CG1DST_OZ_RDX,
    BS3CG1DST_OZ_RBX,
    BS3CG1DST_OZ_RSP,
    BS3CG1DST_OZ_RBP,
    BS3CG1DST_OZ_RSI,
    BS3CG1DST_OZ_RDI,
    BS3CG1DST_OZ_R8,
    BS3CG1DST_OZ_R9,
    BS3CG1DST_OZ_R10,
    BS3CG1DST_OZ_R11,
    BS3CG1DST_OZ_R12,
    BS3CG1DST_OZ_R13,
    BS3CG1DST_OZ_R14,
    BS3CG1DST_OZ_R15,

    /* Control registers.*/
    BS3CG1DST_CR0,
    BS3CG1DST_CR4,
    BS3CG1DST_XCR0,

    /* FPU registers. */
    BS3CG1DST_FPU_FIRST,
    BS3CG1DST_FCW = BS3CG1DST_FPU_FIRST,
    BS3CG1DST_FSW,
    BS3CG1DST_FTW,
    BS3CG1DST_FOP,
    BS3CG1DST_FPUIP,
    BS3CG1DST_FPUCS,
    BS3CG1DST_FPUDP,
    BS3CG1DST_FPUDS,
    BS3CG1DST_MXCSR,
    BS3CG1DST_ST0,
    BS3CG1DST_ST1,
    BS3CG1DST_ST2,
    BS3CG1DST_ST3,
    BS3CG1DST_ST4,
    BS3CG1DST_ST5,
    BS3CG1DST_ST6,
    BS3CG1DST_ST7,
    /* MMX registers. */
    BS3CG1DST_MM0,
    BS3CG1DST_MM1,
    BS3CG1DST_MM2,
    BS3CG1DST_MM3,
    BS3CG1DST_MM4,
    BS3CG1DST_MM5,
    BS3CG1DST_MM6,
    BS3CG1DST_MM7,
    BS3CG1DST_MM0_LO_ZX,
    BS3CG1DST_MM1_LO_ZX,
    BS3CG1DST_MM2_LO_ZX,
    BS3CG1DST_MM3_LO_ZX,
    BS3CG1DST_MM4_LO_ZX,
    BS3CG1DST_MM5_LO_ZX,
    BS3CG1DST_MM6_LO_ZX,
    BS3CG1DST_MM7_LO_ZX,
    /* SSE registers. */
    BS3CG1DST_XMM0,
    BS3CG1DST_XMM1,
    BS3CG1DST_XMM2,
    BS3CG1DST_XMM3,
    BS3CG1DST_XMM4,
    BS3CG1DST_XMM5,
    BS3CG1DST_XMM6,
    BS3CG1DST_XMM7,
    BS3CG1DST_XMM8,
    BS3CG1DST_XMM9,
    BS3CG1DST_XMM10,
    BS3CG1DST_XMM11,
    BS3CG1DST_XMM12,
    BS3CG1DST_XMM13,
    BS3CG1DST_XMM14,
    BS3CG1DST_XMM15,
    BS3CG1DST_XMM0_LO,
    BS3CG1DST_XMM1_LO,
    BS3CG1DST_XMM2_LO,
    BS3CG1DST_XMM3_LO,
    BS3CG1DST_XMM4_LO,
    BS3CG1DST_XMM5_LO,
    BS3CG1DST_XMM6_LO,
    BS3CG1DST_XMM7_LO,
    BS3CG1DST_XMM8_LO,
    BS3CG1DST_XMM9_LO,
    BS3CG1DST_XMM10_LO,
    BS3CG1DST_XMM11_LO,
    BS3CG1DST_XMM12_LO,
    BS3CG1DST_XMM13_LO,
    BS3CG1DST_XMM14_LO,
    BS3CG1DST_XMM15_LO,
    BS3CG1DST_XMM0_HI,
    BS3CG1DST_XMM1_HI,
    BS3CG1DST_XMM2_HI,
    BS3CG1DST_XMM3_HI,
    BS3CG1DST_XMM4_HI,
    BS3CG1DST_XMM5_HI,
    BS3CG1DST_XMM6_HI,
    BS3CG1DST_XMM7_HI,
    BS3CG1DST_XMM8_HI,
    BS3CG1DST_XMM9_HI,
    BS3CG1DST_XMM10_HI,
    BS3CG1DST_XMM11_HI,
    BS3CG1DST_XMM12_HI,
    BS3CG1DST_XMM13_HI,
    BS3CG1DST_XMM14_HI,
    BS3CG1DST_XMM15_HI,
    BS3CG1DST_XMM0_LO_ZX,
    BS3CG1DST_XMM1_LO_ZX,
    BS3CG1DST_XMM2_LO_ZX,
    BS3CG1DST_XMM3_LO_ZX,
    BS3CG1DST_XMM4_LO_ZX,
    BS3CG1DST_XMM5_LO_ZX,
    BS3CG1DST_XMM6_LO_ZX,
    BS3CG1DST_XMM7_LO_ZX,
    BS3CG1DST_XMM8_LO_ZX,
    BS3CG1DST_XMM9_LO_ZX,
    BS3CG1DST_XMM10_LO_ZX,
    BS3CG1DST_XMM11_LO_ZX,
    BS3CG1DST_XMM12_LO_ZX,
    BS3CG1DST_XMM13_LO_ZX,
    BS3CG1DST_XMM14_LO_ZX,
    BS3CG1DST_XMM15_LO_ZX,
    BS3CG1DST_XMM0_DW0,
    BS3CG1DST_XMM1_DW0,
    BS3CG1DST_XMM2_DW0,
    BS3CG1DST_XMM3_DW0,
    BS3CG1DST_XMM4_DW0,
    BS3CG1DST_XMM5_DW0,
    BS3CG1DST_XMM6_DW0,
    BS3CG1DST_XMM7_DW0,
    BS3CG1DST_XMM8_DW0,
    BS3CG1DST_XMM9_DW0,
    BS3CG1DST_XMM10_DW0,
    BS3CG1DST_XMM11_DW0,
    BS3CG1DST_XMM12_DW0,
    BS3CG1DST_XMM13_DW0,
    BS3CG1DST_XMM14_DW0,
    BS3CG1DST_XMM15_DW0,
    BS3CG1DST_XMM0_DW0_ZX,
    BS3CG1DST_XMM1_DW0_ZX,
    BS3CG1DST_XMM2_DW0_ZX,
    BS3CG1DST_XMM3_DW0_ZX,
    BS3CG1DST_XMM4_DW0_ZX,
    BS3CG1DST_XMM5_DW0_ZX,
    BS3CG1DST_XMM6_DW0_ZX,
    BS3CG1DST_XMM7_DW0_ZX,
    BS3CG1DST_XMM8_DW0_ZX,
    BS3CG1DST_XMM9_DW0_ZX,
    BS3CG1DST_XMM10_DW0_ZX,
    BS3CG1DST_XMM11_DW0_ZX,
    BS3CG1DST_XMM12_DW0_ZX,
    BS3CG1DST_XMM13_DW0_ZX,
    BS3CG1DST_XMM14_DW0_ZX,
    BS3CG1DST_XMM15_DW0_ZX,
    BS3CG1DST_XMM0_HI96,
    BS3CG1DST_XMM1_HI96,
    BS3CG1DST_XMM2_HI96,
    BS3CG1DST_XMM3_HI96,
    BS3CG1DST_XMM4_HI96,
    BS3CG1DST_XMM5_HI96,
    BS3CG1DST_XMM6_HI96,
    BS3CG1DST_XMM7_HI96,
    BS3CG1DST_XMM8_HI96,
    BS3CG1DST_XMM9_HI96,
    BS3CG1DST_XMM10_HI96,
    BS3CG1DST_XMM11_HI96,
    BS3CG1DST_XMM12_HI96,
    BS3CG1DST_XMM13_HI96,
    BS3CG1DST_XMM14_HI96,
    BS3CG1DST_XMM15_HI96,
    /* AVX registers. */
    BS3CG1DST_YMM0,
    BS3CG1DST_YMM1,
    BS3CG1DST_YMM2,
    BS3CG1DST_YMM3,
    BS3CG1DST_YMM4,
    BS3CG1DST_YMM5,
    BS3CG1DST_YMM6,
    BS3CG1DST_YMM7,
    BS3CG1DST_YMM8,
    BS3CG1DST_YMM9,
    BS3CG1DST_YMM10,
    BS3CG1DST_YMM11,
    BS3CG1DST_YMM12,
    BS3CG1DST_YMM13,
    BS3CG1DST_YMM14,
    BS3CG1DST_YMM15,

    /* Special fields: */
    BS3CG1DST_SPECIAL_START,
    BS3CG1DST_VALUE_XCPT = BS3CG1DST_SPECIAL_START, /**< Expected exception based on input or result. */

    BS3CG1DST_END
} BS3CG1DST;
AssertCompile(BS3CG1DST_END <= 256);

/** @name Selector opcode definitions.
 *
 * Selector programs are very simple, they are zero or more predicate tests
 * that are ANDed together.  If a predicate test fails, the test is skipped.
 *
 * One instruction is encoded as byte, where the first bit indicates what kind
 * of test and the 7 remaining bits indicates which predicate to check.
 *
 * @{ */
#define BS3CG1SEL_OP_KIND_MASK  UINT8_C(0x01)   /**< The operator part (put in lower bit to reduce switch value range). */
#define BS3CG1SEL_OP_IS_TRUE    UINT8_C(0x00)   /**< Check that the predicate is true. */
#define BS3CG1SEL_OP_IS_FALSE   UINT8_C(0x01)   /**< Check that the predicate is false. */
#define BS3CG1SEL_OP_PRED_SHIFT 1               /**< Shift factor for getting/putting a BS3CG1PRED value into/from a byte. */
/** @} */

/**
 * Test selector predicates (values are shifted by BS3CG1SEL_OP_PRED_SHIFT).
 */
typedef enum BS3CG1PRED
{
    BS3CG1PRED_INVALID = 0,

    /* Operand size. */
    BS3CG1PRED_SIZE_O16,
    BS3CG1PRED_SIZE_O32,
    BS3CG1PRED_SIZE_O64,
    /* VEX.L values. */
    BS3CG1PRED_VEXL_0,
    BS3CG1PRED_VEXL_1,
    /* Execution ring. */
    BS3CG1PRED_RING_0,
    BS3CG1PRED_RING_1,
    BS3CG1PRED_RING_2,
    BS3CG1PRED_RING_3,
    BS3CG1PRED_RING_0_THRU_2,
    BS3CG1PRED_RING_1_THRU_3,
    /* Basic code mode. */
    BS3CG1PRED_CODE_64BIT,
    BS3CG1PRED_CODE_32BIT,
    BS3CG1PRED_CODE_16BIT,
    /* CPU modes. */
    BS3CG1PRED_MODE_REAL,
    BS3CG1PRED_MODE_PROT,
    BS3CG1PRED_MODE_LONG,
    BS3CG1PRED_MODE_V86,
    BS3CG1PRED_MODE_SMM,
    BS3CG1PRED_MODE_VMX,
    BS3CG1PRED_MODE_SVM,
    /* Paging on/off */
    BS3CG1PRED_PAGING_ON,
    BS3CG1PRED_PAGING_OFF,
    /* CPU Vendors. */
    BS3CG1PRED_VENDOR_AMD,
    BS3CG1PRED_VENDOR_INTEL,
    BS3CG1PRED_VENDOR_VIA,
    BS3CG1PRED_VENDOR_SHANGHAI,
    BS3CG1PRED_VENDOR_HYGON,

    BS3CG1PRED_END
} BS3CG1PRED;


/** The test instructions (generated). */
extern const BS3CG1INSTR BS3_FAR_DATA   g_aBs3Cg1Instructions[];
/** The number of test instructions (generated). */
extern const uint16_t BS3_FAR_DATA      g_cBs3Cg1Instructions;
/** The mnemonics (generated).
 * Variable length sequence of mnemonics that runs in parallel to
 * g_aBs3Cg1Instructions. */
extern const char BS3_FAR_DATA          g_achBs3Cg1Mnemonics[];
/** The opcodes (generated).
 * Variable length sequence of opcode bytes that runs in parallel to
 * g_aBs3Cg1Instructions, advancing by BS3CG1INSTR::cbOpcodes each time. */
extern const uint8_t BS3_FAR_DATA       g_abBs3Cg1Opcodes[];
/** The operands (generated).
 * Variable length sequence of opcode values (BS3CG1OP) that runs in
 * parallel to g_aBs3Cg1Instructions, advancing by BS3CG1INSTR::cOperands. */
extern const uint8_t BS3_FAR_DATA       g_abBs3Cg1Operands[];
/** The test data that BS3CG1INSTR.
 * In order to simplify generating these, we use a byte array. */
extern const uint8_t BS3_FAR_DATA       g_abBs3Cg1Tests[];


#endif /* !VBOX_INCLUDED_SRC_bootsectors_bs3_cpu_generated_1_h */

