/* $Id: tstIEMAImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM Assembly Instruction Helper Testcase, Data Header File.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_testcase_tstIEMAImpl_h
#define VMM_INCLUDED_SRC_testcase_tstIEMAImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/x86.h>


/** @name Integer binary tests.
 * @{ */
typedef struct BINU8_TEST_T
{
    uint32_t                fEflIn;
    uint32_t                fEflOut;
    uint8_t                 uDstIn;
    uint8_t                 uDstOut;
    uint8_t                 uSrcIn;
    uint8_t                 uMisc;
} BINU8_TEST_T;

typedef struct BINU16_TEST_T
{
    uint32_t                fEflIn;
    uint32_t                fEflOut;
    uint16_t                uDstIn;
    uint16_t                uDstOut;
    uint16_t                uSrcIn;
    uint16_t                uMisc;
} BINU16_TEST_T;

typedef struct BINU32_TEST_T
{
    uint32_t                fEflIn;
    uint32_t                fEflOut;
    uint32_t                uDstIn;
    uint32_t                uDstOut;
    uint32_t                uSrcIn;
    uint32_t                uMisc;
} BINU32_TEST_T;

typedef struct BINU64_TEST_T
{
    uint32_t                fEflIn;
    uint32_t                fEflOut;
    uint64_t                uDstIn;
    uint64_t                uDstOut;
    uint64_t                uSrcIn;
    uint64_t                uMisc;
} BINU64_TEST_T;
/** @} */


/** @name mult/div (PFNIEMAIMPLBINU8, PFNIEMAIMPLBINU16, PFNIEMAIMPLBINU32, PFNIEMAIMPLBINU64)
 * @{ */
typedef struct MULDIVU8_TEST_T
{
    uint32_t                fEflIn;
    uint32_t                fEflOut;
    uint16_t                uDstIn;
    uint16_t                uDstOut;
    uint8_t                 uSrcIn;
    int32_t                 rc;
} MULDIVU8_TEST_T;

typedef struct MULDIVU16_TEST_T
{
    uint32_t                fEflIn;
    uint32_t                fEflOut;
    uint16_t                uDst1In;
    uint16_t                uDst1Out;
    uint16_t                uDst2In;
    uint16_t                uDst2Out;
    uint16_t                uSrcIn;
    int32_t                 rc;
} MULDIVU16_TEST_T;

typedef struct MULDIVU32_TEST_T
{
    uint32_t                fEflIn;
    uint32_t                fEflOut;
    uint32_t                uDst1In;
    uint32_t                uDst1Out;
    uint32_t                uDst2In;
    uint32_t                uDst2Out;
    uint32_t                uSrcIn;
    int32_t                 rc;
} MULDIVU32_TEST_T;

typedef struct MULDIVU64_TEST_T
{
    uint32_t                fEflIn;
    uint32_t                fEflOut;
    uint64_t                uDst1In;
    uint64_t                uDst1Out;
    uint64_t                uDst2In;
    uint64_t                uDst2Out;
    uint64_t                uSrcIn;
    int32_t                 rc;
} MULDIVU64_TEST_T;
/** @} */


/** @name x87 FPU
 * @{ */
typedef struct FPU_LD_CONST_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              rdResult;
} FPU_LD_CONST_TEST_T;

typedef struct FPU_R32_IN_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              rdResult;
    RTFLOAT32U              InVal;
} FPU_R32_IN_TEST_T;

typedef struct FPU_R64_IN_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              rdResult;
    RTFLOAT64U              InVal;
} FPU_R64_IN_TEST_T;

typedef struct FPU_R80_IN_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              rdResult;
    RTFLOAT80U              InVal;
} FPU_R80_IN_TEST_T;

typedef struct FPU_I16_IN_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              rdResult;
    int16_t                 iInVal;
} FPU_I16_IN_TEST_T;

typedef struct FPU_I32_IN_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              rdResult;
    int32_t                 iInVal;
} FPU_I32_IN_TEST_T;

typedef struct FPU_I64_IN_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              rdResult;
    int64_t                 iInVal;
} FPU_I64_IN_TEST_T;

typedef struct FPU_D80_IN_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              rdResult;
    RTPBCD80U               InVal;
} FPU_D80_IN_TEST_T;

typedef struct FPU_ST_R32_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal;
    RTFLOAT32U              OutVal;
} FPU_ST_R32_TEST_T;

typedef struct FPU_ST_R64_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal;
    RTFLOAT64U              OutVal;
} FPU_ST_R64_TEST_T;

typedef struct FPU_ST_R80_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal;
    RTFLOAT80U              OutVal;
} FPU_ST_R80_TEST_T;

typedef struct FPU_ST_I16_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal;
    int16_t                 iOutVal;
} FPU_ST_I16_TEST_T;

typedef struct FPU_ST_I32_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal;
    int32_t                 iOutVal;
} FPU_ST_I32_TEST_T;

typedef struct FPU_ST_I64_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal;
    int64_t                 iOutVal;
} FPU_ST_I64_TEST_T;

typedef struct FPU_ST_D80_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal;
    RTPBCD80U               OutVal;
} FPU_ST_D80_TEST_T;

typedef struct FPU_BINARY_R80_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal1;
    RTFLOAT80U              InVal2;
    RTFLOAT80U              OutVal;
} FPU_BINARY_R80_TEST_T;

typedef struct FPU_BINARY_R64_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal1;
    RTFLOAT64U              InVal2;
    RTFLOAT80U              OutVal;
} FPU_BINARY_R64_TEST_T;

typedef struct FPU_BINARY_R32_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal1;
    RTFLOAT32U              InVal2;
    RTFLOAT80U              OutVal;
} FPU_BINARY_R32_TEST_T;

typedef struct FPU_BINARY_I64_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal1;
    int64_t                 InVal2;
    RTFLOAT80U              OutVal;
} FPU_BINARY_I64_TEST_T;

typedef struct FPU_BINARY_I32_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal1;
    int32_t                 InVal2;
    RTFLOAT80U              OutVal;
} FPU_BINARY_I32_TEST_T;

typedef struct FPU_BINARY_I16_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal1;
    int16_t                 InVal2;
    RTFLOAT80U              OutVal;
} FPU_BINARY_I16_TEST_T;

typedef struct FPU_BINARY_EFL_R80_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal1;
    RTFLOAT80U              InVal2;
    uint32_t                fEflOut;
} FPU_BINARY_EFL_R80_TEST_T;

typedef struct FPU_UNARY_R80_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal;
    RTFLOAT80U              OutVal;
} FPU_UNARY_R80_TEST_T;

typedef struct FPU_UNARY_TWO_R80_TEST_T
{
    uint16_t                fFcw;
    uint16_t                fFswIn;
    uint16_t                fFswOut;
    RTFLOAT80U              InVal;
    RTFLOAT80U              OutVal1;
    RTFLOAT80U              OutVal2;
} FPU_UNARY_TWO_R80_TEST_T;

typedef struct SSE_BINARY_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    uint32_t                au32Padding[2];
    X86XMMREG               InVal1;
    X86XMMREG               InVal2;
    X86XMMREG               OutVal;
} SSE_BINARY_TEST_T;

typedef struct SSE_BINARY_U128_R32_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    uint32_t                au32Padding[2];
    X86XMMREG               OutVal;
    X86XMMREG               InVal1;
    RTFLOAT32U              r32Val2;
} SSE_BINARY_U128_R32_TEST_T;

typedef struct SSE_BINARY_U128_R64_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    uint32_t                au32Padding[2];
    X86XMMREG               OutVal;
    X86XMMREG               InVal1;
    RTFLOAT64U              r64Val2;
} SSE_BINARY_U128_R64_TEST_T;

typedef struct SSE_BINARY_I32_R64_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    uint32_t                u32Padding;
    int32_t                 i32ValOut;
    RTFLOAT64U              r64ValIn;
} SSE_BINARY_I32_R64_TEST_T;

typedef struct SSE_BINARY_I64_R64_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    int64_t                 i64ValOut;
    RTFLOAT64U              r64ValIn;
} SSE_BINARY_I64_R64_TEST_T;

typedef struct SSE_BINARY_I32_R32_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    uint32_t                u32Padding;
    int32_t                 i32ValOut;
    RTFLOAT32U              r32ValIn;
} SSE_BINARY_I32_R32_TEST_T;

typedef struct SSE_BINARY_I64_R32_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    int64_t                 i64ValOut;
    RTFLOAT32U              r32ValIn;
} SSE_BINARY_I64_R32_TEST_T;

typedef struct SSE_BINARY_R32_I32_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    uint32_t                u32Padding;
    int32_t                 i32ValIn;
    RTFLOAT32U              r32ValOut;
} SSE_BINARY_R32_I32_TEST_T;

typedef struct SSE_BINARY_R32_I64_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    int64_t                 i64ValIn;
    RTFLOAT32U              r32ValOut;
} SSE_BINARY_R32_I64_TEST_T;

typedef struct SSE_BINARY_R64_I32_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    uint32_t                u32Padding;
    int32_t                 i32ValIn;
    RTFLOAT64U              r64ValOut;
} SSE_BINARY_R64_I32_TEST_T;

typedef struct SSE_BINARY_R64_I64_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    int64_t                 i64ValIn;
    RTFLOAT64U              r64ValOut;
} SSE_BINARY_R64_I64_TEST_T;

typedef struct SSE_COMPARE_EFL_R32_R32_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    uint32_t                fEflIn;
    uint32_t                fEflOut;
    RTFLOAT32U              r32ValIn1;
    RTFLOAT32U              r32ValIn2;
} SSE_COMPARE_EFL_R32_R32_TEST_T;

typedef struct SSE_COMPARE_EFL_R64_R64_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    uint32_t                fEflIn;
    uint32_t                fEflOut;
    RTFLOAT64U              r64ValIn1;
    RTFLOAT64U              r64ValIn2;
} SSE_COMPARE_EFL_R64_R64_TEST_T;

typedef struct SSE_COMPARE_F3_XMM_IMM8_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    uint8_t                 bImm;
    uint8_t                 abPadding0[3];
    uint32_t                u32Padding1;
    X86XMMREG               InVal1;
    X86XMMREG               InVal2;
    X86XMMREG               OutVal;
} SSE_COMPARE_F3_XMM_IMM8_TEST_T;

typedef struct SSE_CONVERT_XMM_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    uint32_t                au32Padding[2];
    X86XMMREG               InVal;
    X86XMMREG               OutVal;
} SSE_CONVERT_XMM_TEST_T;

typedef struct SSE_CONVERT_MM_XMM_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    RTUINT64U               OutVal;
    X86XMMREG               InVal;
} SSE_CONVERT_MM_XMM_TEST_T;

typedef struct SSE_CONVERT_XMM_MM_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    RTUINT64U               InVal;
    X86XMMREG               OutVal;
} SSE_CONVERT_XMM_MM_TEST_T;

typedef struct SSE_CONVERT_MM_R32_TEST_T
{
    uint32_t                fMxcsrIn;
    uint32_t                fMxcsrOut;
    RTFLOAT32U              ar32InVal[2];
    RTUINT64U               OutVal;
} SSE_CONVERT_MM_R32_TEST_T;

typedef struct SSE_PCMPISTRI_TEST_T
{
    X86XMMREG               InVal1;
    X86XMMREG               InVal2;
    uint32_t                u32EcxOut;
    uint32_t                fEFlagsIn;
    uint32_t                fEFlagsOut;
    uint8_t                 bImm;
} SSE_PCMPISTRI_TEST_T;

typedef struct SSE_PCMPESTRI_TEST_T
{
    X86XMMREG               InVal1;
    X86XMMREG               InVal2;
    uint64_t                u64Rax;
    uint64_t                u64Rdx;
    uint32_t                u32EcxOut;
    uint32_t                fEFlagsIn;
    uint32_t                fEFlagsOut;
    uint8_t                 bImm;
} SSE_PCMPESTRI_TEST_T;

typedef struct SSE_PCMPISTRM_TEST_T
{
    X86XMMREG               InVal1;
    X86XMMREG               InVal2;
    X86XMMREG               OutVal;
    uint32_t                fEFlagsIn;
    uint32_t                fEFlagsOut;
    uint8_t                 bImm;
} SSE_PCMPISTRM_TEST_T;

typedef struct SSE_PCMPESTRM_TEST_T
{
    X86XMMREG               InVal1;
    X86XMMREG               InVal2;
    X86XMMREG               OutVal;
    uint64_t                u64Rax;
    uint64_t                u64Rdx;
    uint32_t                fEFlagsIn;
    uint32_t                fEFlagsOut;
    uint8_t                 bImm;
} SSE_PCMPESTRM_TEST_T;

/** @} */


#define TSTIEM_DEFINE_EMPTY_TEST_ARRAY_BIN(a_Type, a_Instr) \
    extern a_Type   const RT_CONCAT(g_abTests_, a_Instr)[] = { {0} }; \
    extern uint32_t const RT_CONCAT(g_cbTests_, a_Instr)  = 0

#define TSTIEM_DECLARE_TEST_ARRAY_BIN(a_szFile, a_Type, a_Instr) \
    extern a_Type   const RT_CONCAT(g_abTests_, a_Instr)[]; \
    extern uint32_t const RT_CONCAT(g_cbTests_, a_Instr)

RT_C_DECLS_BEGIN

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               add_u8                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               add_u8_locked            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               adc_u8                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               adc_u8_locked            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               sub_u8                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               sub_u8_locked            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               sbb_u8                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               sbb_u8_locked            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               or_u8                    );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               or_u8_locked             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               xor_u8                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               xor_u8_locked            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               and_u8                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               and_u8_locked            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               cmp_u8                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               test_u8                  );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              add_u16                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              add_u16_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              adc_u16                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              adc_u16_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              sub_u16                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              sub_u16_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              sbb_u16                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              sbb_u16_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              or_u16                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              or_u16_locked            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              xor_u16                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              xor_u16_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              and_u16                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              and_u16_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              cmp_u16                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              test_u16                 );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              bt_u16                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              btc_u16                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              btc_u16_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              btr_u16                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              btr_u16_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              bts_u16                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              bts_u16_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              arpl                     );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU16_TEST_T,              bsf_u16_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU16_TEST_T,              bsr_u16_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU16_TEST_T,              imul_two_u16_amd         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU16_TEST_T,              bsf_u16_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU16_TEST_T,              bsr_u16_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU16_TEST_T,              imul_two_u16_intel       );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              add_u32                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              add_u32_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              adc_u32                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              adc_u32_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              sub_u32                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              sub_u32_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              sbb_u32                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              sbb_u32_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              or_u32                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              or_u32_locked            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              xor_u32                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              xor_u32_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              and_u32                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              and_u32_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              cmp_u32                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              test_u32                 );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              bt_u32                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              btc_u32                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              btc_u32_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              btr_u32                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              btr_u32_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              bts_u32                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              bts_u32_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU32_TEST_T,              bsf_u32_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU32_TEST_T,              bsr_u32_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU32_TEST_T,              imul_two_u32_amd         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU32_TEST_T,              bsf_u32_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU32_TEST_T,              bsr_u32_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU32_TEST_T,              imul_two_u32_intel       );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              adcx_u32                 );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              adox_u32                 );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              add_u64                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              add_u64_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              adc_u64                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              adc_u64_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              sub_u64                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              sub_u64_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              sbb_u64                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              sbb_u64_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              or_u64                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              or_u64_locked            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              xor_u64                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              xor_u64_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              and_u64                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              and_u64_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              cmp_u64                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              test_u64                 );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              bt_u64                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              btc_u64                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              btc_u64_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              btr_u64                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              btr_u64_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              bts_u64                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              bts_u64_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU64_TEST_T,              bsf_u64_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU64_TEST_T,              bsr_u64_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU64_TEST_T,              imul_two_u64_amd         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU64_TEST_T,              bsf_u64_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU64_TEST_T,              bsr_u64_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU64_TEST_T,              imul_two_u64_intel       );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              adcx_u64                 );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              adox_u64                 );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU16_TEST_T,              shrd_u16_amd             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU16_TEST_T,              shld_u16_amd             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU32_TEST_T,              shrd_u32_amd             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU32_TEST_T,              shld_u32_amd             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU64_TEST_T,              shrd_u64_amd             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU64_TEST_T,              shld_u64_amd             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU16_TEST_T,              shrd_u16_intel           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU16_TEST_T,              shld_u16_intel           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU32_TEST_T,              shrd_u32_intel           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU32_TEST_T,              shld_u32_intel           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU64_TEST_T,              shrd_u64_intel           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU64_TEST_T,              shld_u64_intel           );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               inc_u8                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               inc_u8_locked            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               dec_u8                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               dec_u8_locked            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               not_u8                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               not_u8_locked            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               neg_u8                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU8_TEST_T,               neg_u8_locked            );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              inc_u16                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              inc_u16_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              dec_u16                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              dec_u16_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              not_u16                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              not_u16_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              neg_u16                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU16_TEST_T,              neg_u16_locked           );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              inc_u32                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              inc_u32_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              dec_u32                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              dec_u32_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              not_u32                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              not_u32_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              neg_u32                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU32_TEST_T,              neg_u32_locked           );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              inc_u64                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              inc_u64_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              dec_u64                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              dec_u64_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              not_u64                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              not_u64_locked           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              neg_u64                  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int,          BINU64_TEST_T,              neg_u64_locked           );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU8_TEST_T,               rol_u8_amd               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU8_TEST_T,               ror_u8_amd               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU8_TEST_T,               rcl_u8_amd               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU8_TEST_T,               rcr_u8_amd               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU8_TEST_T,               shl_u8_amd               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU8_TEST_T,               shr_u8_amd               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU8_TEST_T,               sar_u8_amd               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU8_TEST_T,               rol_u8_intel             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU8_TEST_T,               ror_u8_intel             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU8_TEST_T,               rcl_u8_intel             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU8_TEST_T,               rcr_u8_intel             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU8_TEST_T,               shl_u8_intel             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU8_TEST_T,               shr_u8_intel             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU8_TEST_T,               sar_u8_intel             );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU16_TEST_T,              rol_u16_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU16_TEST_T,              ror_u16_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU16_TEST_T,              rcl_u16_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU16_TEST_T,              rcr_u16_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU16_TEST_T,              shl_u16_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU16_TEST_T,              shr_u16_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU16_TEST_T,              sar_u16_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU16_TEST_T,              rol_u16_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU16_TEST_T,              ror_u16_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU16_TEST_T,              rcl_u16_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU16_TEST_T,              rcr_u16_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU16_TEST_T,              shl_u16_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU16_TEST_T,              shr_u16_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU16_TEST_T,              sar_u16_intel            );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU32_TEST_T,              rol_u32_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU32_TEST_T,              ror_u32_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU32_TEST_T,              rcl_u32_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU32_TEST_T,              rcr_u32_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU32_TEST_T,              shl_u32_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU32_TEST_T,              shr_u32_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU32_TEST_T,              sar_u32_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU32_TEST_T,              rol_u32_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU32_TEST_T,              ror_u32_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU32_TEST_T,              rcl_u32_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU32_TEST_T,              rcr_u32_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU32_TEST_T,              shl_u32_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU32_TEST_T,              shr_u32_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU32_TEST_T,              sar_u32_intel            );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU64_TEST_T,              rol_u64_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU64_TEST_T,              ror_u64_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU64_TEST_T,              rcl_u64_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU64_TEST_T,              rcr_u64_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU64_TEST_T,              shl_u64_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU64_TEST_T,              shr_u64_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      BINU64_TEST_T,              sar_u64_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU64_TEST_T,              rol_u64_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU64_TEST_T,              ror_u64_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU64_TEST_T,              rcl_u64_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU64_TEST_T,              rcr_u64_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU64_TEST_T,              shl_u64_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU64_TEST_T,              shr_u64_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    BINU64_TEST_T,              sar_u64_intel            );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU8_TEST_T,            mul_u8_amd               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU8_TEST_T,            imul_u8_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU8_TEST_T,            div_u8_amd               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU8_TEST_T,            idiv_u8_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU8_TEST_T,            mul_u8_intel             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU8_TEST_T,            imul_u8_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU8_TEST_T,            div_u8_intel             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU8_TEST_T,            idiv_u8_intel            );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU16_TEST_T,           mul_u16_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU16_TEST_T,           imul_u16_amd             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU16_TEST_T,           div_u16_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU16_TEST_T,           idiv_u16_amd             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU16_TEST_T,           mul_u16_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU16_TEST_T,           imul_u16_intel           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU16_TEST_T,           div_u16_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU16_TEST_T,           idiv_u16_intel           );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU32_TEST_T,           mul_u32_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU32_TEST_T,           imul_u32_amd             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU32_TEST_T,           div_u32_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU32_TEST_T,           idiv_u32_amd             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU32_TEST_T,           mul_u32_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU32_TEST_T,           imul_u32_intel           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU32_TEST_T,           div_u32_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU32_TEST_T,           idiv_u32_intel           );

TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU64_TEST_T,           mul_u64_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU64_TEST_T,           imul_u64_amd             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU64_TEST_T,           div_u64_amd              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Amd,      MULDIVU64_TEST_T,           idiv_u64_amd             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU64_TEST_T,           mul_u64_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU64_TEST_T,           imul_u64_intel           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU64_TEST_T,           div_u64_intel            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(Int-Intel,    MULDIVU64_TEST_T,           idiv_u64_intel           );

TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_LD_CONST_TEST_T,        fld1                     );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_LD_CONST_TEST_T,        fldl2t                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_LD_CONST_TEST_T,        fldl2e                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_LD_CONST_TEST_T,        fldpi                    );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_LD_CONST_TEST_T,        fldlg2                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_LD_CONST_TEST_T,        fldln2                   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_LD_CONST_TEST_T,        fldz                     );

TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_R80_IN_TEST_T,          fld_r80_from_r80         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_R64_IN_TEST_T,          fld_r80_from_r64         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_R32_IN_TEST_T,          fld_r80_from_r32         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_I64_IN_TEST_T,          fild_r80_from_i64        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_I32_IN_TEST_T,          fild_r80_from_i32        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_I16_IN_TEST_T,          fild_r80_from_i16        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_D80_IN_TEST_T,          fld_r80_from_d80         );

TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_ST_R80_TEST_T,          fst_r80_to_r80           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_ST_R64_TEST_T,          fst_r80_to_r64           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_ST_R32_TEST_T,          fst_r80_to_r32           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_ST_I64_TEST_T,          fist_r80_to_i64          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_ST_I32_TEST_T,          fist_r80_to_i32          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_ST_I16_TEST_T,          fist_r80_to_i16          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_ST_I64_TEST_T,          fistt_r80_to_i64         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_ST_I32_TEST_T,          fistt_r80_to_i32         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt-Amd,      FPU_ST_I16_TEST_T,          fistt_r80_to_i16_amd     );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt-Intel,    FPU_ST_I16_TEST_T,          fistt_r80_to_i16_intel   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuLdSt,          FPU_ST_D80_TEST_T,          fst_r80_to_d80           );

TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1,       FPU_BINARY_R80_TEST_T,      fadd_r80_by_r80          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1,       FPU_BINARY_R80_TEST_T,      fsub_r80_by_r80          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1,       FPU_BINARY_R80_TEST_T,      fsubr_r80_by_r80         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1,       FPU_BINARY_R80_TEST_T,      fmul_r80_by_r80          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1,       FPU_BINARY_R80_TEST_T,      fdiv_r80_by_r80          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1,       FPU_BINARY_R80_TEST_T,      fdivr_r80_by_r80         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1,       FPU_BINARY_R80_TEST_T,      fprem_r80_by_r80         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1,       FPU_BINARY_R80_TEST_T,      fprem1_r80_by_r80        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1,       FPU_BINARY_R80_TEST_T,      fscale_r80_by_r80        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1-Amd,   FPU_BINARY_R80_TEST_T,      fpatan_r80_by_r80_amd    );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1-Intel, FPU_BINARY_R80_TEST_T,      fpatan_r80_by_r80_intel  );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1-Amd,   FPU_BINARY_R80_TEST_T,      fyl2x_r80_by_r80_amd     );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1-Intel, FPU_BINARY_R80_TEST_T,      fyl2x_r80_by_r80_intel   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1-Amd,   FPU_BINARY_R80_TEST_T,      fyl2xp1_r80_by_r80_amd   );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1-Intel, FPU_BINARY_R80_TEST_T,      fyl2xp1_r80_by_r80_intel );

TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_R64_TEST_T,      fadd_r80_by_r64          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_R64_TEST_T,      fmul_r80_by_r64          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_R64_TEST_T,      fsub_r80_by_r64          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_R64_TEST_T,      fsubr_r80_by_r64         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_R64_TEST_T,      fdiv_r80_by_r64          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_R64_TEST_T,      fdivr_r80_by_r64         );

TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_R32_TEST_T,      fadd_r80_by_r32          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_R32_TEST_T,      fmul_r80_by_r32          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_R32_TEST_T,      fsub_r80_by_r32          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_R32_TEST_T,      fsubr_r80_by_r32         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_R32_TEST_T,      fdiv_r80_by_r32          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_R32_TEST_T,      fdivr_r80_by_r32         );

TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_I32_TEST_T,      fiadd_r80_by_i32         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_I32_TEST_T,      fimul_r80_by_i32         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_I32_TEST_T,      fisub_r80_by_i32         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_I32_TEST_T,      fisubr_r80_by_i32        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_I32_TEST_T,      fidiv_r80_by_i32         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_I32_TEST_T,      fidivr_r80_by_i32        );

TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_I16_TEST_T,      fiadd_r80_by_i16         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_I16_TEST_T,      fimul_r80_by_i16         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_I16_TEST_T,      fisub_r80_by_i16         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_I16_TEST_T,      fisubr_r80_by_i16        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_I16_TEST_T,      fidiv_r80_by_i16         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_I16_TEST_T,      fidivr_r80_by_i16        );

TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1,       FPU_BINARY_R80_TEST_T,      fcom_r80_by_r80          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1,       FPU_BINARY_R80_TEST_T,      fucom_r80_by_r80         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_R64_TEST_T,      fcom_r80_by_r64          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_R32_TEST_T,      fcom_r80_by_r32          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_I32_TEST_T,      ficom_r80_by_i32         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary2,       FPU_BINARY_I16_TEST_T,      ficom_r80_by_i16         );

TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1,       FPU_BINARY_EFL_R80_TEST_T,  fcomi_r80_by_r80         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuBinary1,       FPU_BINARY_EFL_R80_TEST_T,  fucomi_r80_by_r80        );

TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther,         FPU_UNARY_R80_TEST_T,       fabs_r80                 );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther,         FPU_UNARY_R80_TEST_T,       fchs_r80                 );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther-Amd,     FPU_UNARY_R80_TEST_T,       f2xm1_r80_amd            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther-Intel,   FPU_UNARY_R80_TEST_T,       f2xm1_r80_intel          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther,         FPU_UNARY_R80_TEST_T,       fsqrt_r80                );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther,         FPU_UNARY_R80_TEST_T,       frndint_r80              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther-Amd,     FPU_UNARY_R80_TEST_T,       fsin_r80_amd             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther-Intel,   FPU_UNARY_R80_TEST_T,       fsin_r80_intel           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther-Amd,     FPU_UNARY_R80_TEST_T,       fcos_r80_amd             );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther-Intel,   FPU_UNARY_R80_TEST_T,       fcos_r80_intel           );

TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther,         FPU_UNARY_R80_TEST_T,       ftst_r80                 );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther,         FPU_UNARY_R80_TEST_T,       fxam_r80                 );

TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther-Amd,     FPU_UNARY_TWO_R80_TEST_T,   fptan_r80_r80_amd        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther-Intel,   FPU_UNARY_TWO_R80_TEST_T,   fptan_r80_r80_intel      );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther,         FPU_UNARY_TWO_R80_TEST_T,   fxtract_r80_r80          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther-Amd,     FPU_UNARY_TWO_R80_TEST_T,   fsincos_r80_r80_amd      );
TSTIEM_DECLARE_TEST_ARRAY_BIN(FpuOther-Intel,   FPU_UNARY_TWO_R80_TEST_T,   fsincos_r80_r80_intel    );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          addps_u128               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          mulps_u128               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          subps_u128               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          minps_u128               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          divps_u128               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          maxps_u128               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          haddps_u128              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          hsubps_u128              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          sqrtps_u128              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          addsubps_u128            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          cvtps2pd_u128x           );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R32_TEST_T, addss_u128_r32           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R32_TEST_T, mulss_u128_r32           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R32_TEST_T, subss_u128_r32           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R32_TEST_T, minss_u128_r32           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R32_TEST_T, divss_u128_r32           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R32_TEST_T, maxss_u128_r32           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R32_TEST_T, cvtss2sd_u128_r32        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R32_TEST_T, sqrtss_u128_r32          );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          addpd_u128               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          mulpd_u128               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          subpd_u128               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          minpd_u128               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          divpd_u128               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          maxpd_u128               );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          haddpd_u128              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          hsubpd_u128              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          sqrtpd_u128              );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          addsubpd_u128            );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_TEST_T,          cvtpd2ps_u128            );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R64_TEST_T, addsd_u128_r64           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R64_TEST_T, mulsd_u128_r64           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R64_TEST_T, subsd_u128_r64           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R64_TEST_T, minsd_u128_r64           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R64_TEST_T, divsd_u128_r64           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R64_TEST_T, maxsd_u128_r64           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R64_TEST_T, cvtsd2ss_u128_r64        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_U128_R64_TEST_T, sqrtsd_u128_r64          );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_I32_R64_TEST_T,  cvttsd2si_i32_r64        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_I32_R64_TEST_T,  cvtsd2si_i32_r64         );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_I64_R64_TEST_T,  cvttsd2si_i64_r64        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_I64_R64_TEST_T,  cvtsd2si_i64_r64         );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_I32_R32_TEST_T,  cvttss2si_i32_r32        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_I32_R32_TEST_T,  cvtss2si_i32_r32         );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_I64_R32_TEST_T,  cvttss2si_i64_r32        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_I64_R32_TEST_T,  cvtss2si_i64_r32         );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_R32_I32_TEST_T,  cvtsi2ss_r32_i32         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_R32_I64_TEST_T,  cvtsi2ss_r32_i64         );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_R64_I32_TEST_T,  cvtsi2sd_r64_i32         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseBinary,    SSE_BINARY_R64_I64_TEST_T,  cvtsi2sd_r64_i64         );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseCompare,   SSE_COMPARE_EFL_R32_R32_TEST_T, ucomiss_u128         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseCompare,   SSE_COMPARE_EFL_R32_R32_TEST_T, vucomiss_u128        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseCompare,   SSE_COMPARE_EFL_R32_R32_TEST_T, comiss_u128          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseCompare,   SSE_COMPARE_EFL_R32_R32_TEST_T, vcomiss_u128         );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseCompare,   SSE_COMPARE_EFL_R64_R64_TEST_T, ucomisd_u128         );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseCompare,   SSE_COMPARE_EFL_R64_R64_TEST_T, vucomisd_u128        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseCompare,   SSE_COMPARE_EFL_R64_R64_TEST_T, comisd_u128          );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseCompare,   SSE_COMPARE_EFL_R64_R64_TEST_T, vcomisd_u128         );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseCompare,   SSE_COMPARE_F3_XMM_IMM8_TEST_T, cmpps_u128           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseCompare,   SSE_COMPARE_F3_XMM_IMM8_TEST_T, cmppd_u128           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseCompare,   SSE_COMPARE_F3_XMM_IMM8_TEST_T, cmpss_u128           );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseCompare,   SSE_COMPARE_F3_XMM_IMM8_TEST_T, cmpsd_u128           );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseConvert,   SSE_CONVERT_XMM_TEST_T,         cvtdq2ps_u128        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseConvert,   SSE_CONVERT_XMM_TEST_T,         cvtps2dq_u128        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseConvert,   SSE_CONVERT_XMM_TEST_T,         cvttps2dq_u128       );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseConvert,   SSE_CONVERT_XMM_TEST_T,         cvttpd2dq_u128       );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseConvert,   SSE_CONVERT_XMM_TEST_T,         cvtdq2pd_u128        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseConvert,   SSE_CONVERT_XMM_TEST_T,         cvtpd2dq_u128        );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseConvert,   SSE_CONVERT_MM_XMM_TEST_T,      cvtpd2pi_u128        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseConvert,   SSE_CONVERT_MM_XMM_TEST_T,      cvttpd2pi_u128       );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseConvert,   SSE_CONVERT_XMM_MM_TEST_T,      cvtpi2ps_u128        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseConvert,   SSE_CONVERT_XMM_MM_TEST_T,      cvtpi2pd_u128        );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SseConvert,   SSE_CONVERT_MM_R32_TEST_T,      cvtps2pi_u128        );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SseConvert,   SSE_CONVERT_MM_R32_TEST_T,      cvttps2pi_u128       );

TSTIEM_DECLARE_TEST_ARRAY_BIN(SsePcmpxstrx, SSE_PCMPISTRI_TEST_T,           pcmpistri_u128       );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SsePcmpxstrx, SSE_PCMPISTRI_TEST_T,           vpcmpistri_u128      );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SsePcmpxstrx, SSE_PCMPESTRI_TEST_T,           pcmpestri_u128       );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SsePcmpxstrx, SSE_PCMPESTRI_TEST_T,           vpcmpestri_u128      );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SsePcmpxstrx, SSE_PCMPISTRM_TEST_T,           pcmpistrm_u128       );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SsePcmpxstrx, SSE_PCMPISTRM_TEST_T,           vpcmpistrm_u128      );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SsePcmpxstrx, SSE_PCMPESTRM_TEST_T,           pcmpestrm_u128       );
TSTIEM_DECLARE_TEST_ARRAY_BIN(SsePcmpxstrx, SSE_PCMPESTRM_TEST_T,           vpcmpestrm_u128      );

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_testcase_tstIEMAImpl_h */

