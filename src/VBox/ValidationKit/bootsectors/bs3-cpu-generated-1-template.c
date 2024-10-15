/* $Id: bs3-cpu-generated-1-template.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - bs3-cpu-generated-1, C code template.
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

#ifndef BS3_INSTANTIATING_CMN
# error "BS3_INSTANTIATING_CMN not defined"
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>

#include "bs3-cpu-generated-1.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define BS3CG1_WITH_VEX

#define P_CS        X86_OP_PRF_CS
#define P_SS        X86_OP_PRF_SS
#define P_DS        X86_OP_PRF_DS
#define P_ES        X86_OP_PRF_ES
#define P_FS        X86_OP_PRF_FS
#define P_GS        X86_OP_PRF_GS
#define P_OZ        X86_OP_PRF_SIZE_OP
#define P_AZ        X86_OP_PRF_SIZE_ADDR
#define P_LK        X86_OP_PRF_LOCK
#define P_RN        X86_OP_PRF_REPNZ
#define P_RZ        X86_OP_PRF_REPZ

#define REX_WRBX    (X86_OP_REX_W | X86_OP_REX_R | X86_OP_REX_B | X86_OP_REX_X)
#define REX_W___    (X86_OP_REX_W)
#define REX_WR__    (X86_OP_REX_W | X86_OP_REX_R)
#define REX_W_B_    (X86_OP_REX_W | X86_OP_REX_B)
#define REX_W__X    (X86_OP_REX_W | X86_OP_REX_X)
#define REX_WRB_    (X86_OP_REX_W | X86_OP_REX_R | X86_OP_REX_B)
#define REX_WR_X    (X86_OP_REX_W | X86_OP_REX_R | X86_OP_REX_X)
#define REX_W_BX    (X86_OP_REX_W | X86_OP_REX_B | X86_OP_REX_X)
#define REX__R__    (X86_OP_REX_R)
#define REX__RB_    (X86_OP_REX_R | X86_OP_REX_B)
#define REX__R_X    (X86_OP_REX_R | X86_OP_REX_X)
#define REX__RBX    (X86_OP_REX_R | X86_OP_REX_B | X86_OP_REX_X)
#define REX___B_    (X86_OP_REX_B)
#define REX___BX    (X86_OP_REX_B | X86_OP_REX_X)
#define REX____X    (X86_OP_REX_X)
#define REX_____    (0x40)


/** @def  BS3CG1_DPRINTF
 * Debug print macro.
 */
#if 0
# define BS3CG1_DPRINTF(a_ArgList) Bs3TestPrintf a_ArgList
# define BS3CG1_DEBUG_CTX_MOD
#else
# define BS3CG1_DPRINTF(a_ArgList) do { } while (0)
#endif

/**
 * Checks if this is a 64-bit test target or not.
 * Helps avoid ifdefs or code bloat.
 */
#if ARCH_BITS == 64
# define BS3CG1_IS_64BIT_TARGET(a_pThis)    BS3_MODE_IS_64BIT_CODE((a_pThis)->bMode)
#else
# define BS3CG1_IS_64BIT_TARGET(a_pThis)    (false)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Operand value location. */
typedef enum BS3CG1OPLOC
{
    BS3CG1OPLOC_INVALID = 0,
    BS3CG1OPLOC_CTX,
    BS3CG1OPLOC_CTX_ZX_VLMAX,
    BS3CG1OPLOC_IMM,
    BS3CG1OPLOC_MEM,
    BS3CG1OPLOC_MEM_RW,
    BS3CG1OPLOC_MEM_WO,
    BS3CG1OPLOC_END
} BS3CG1OPLOC;
AssertCompile(BS3CG1OPLOC_END <= 16);


/** Pointer to the generated test state. */
typedef struct BS3CG1STATE *PBS3CG1STATE;

/**
 * Encoder callback.
 * @returns Next encoding.  If equal or less to @a iEncoding, no
 *          further encodings are available for testing.
 * @param   pThis       The state.
 * @param   iEncoding   The encoding.
 */
typedef unsigned BS3_NEAR_CODE FNBS3CG1ENCODER(PBS3CG1STATE pThis, unsigned iEncoding);
/** Pointer to a encoder callback. */
typedef FNBS3CG1ENCODER *PFNBS3CG1ENCODER;


/**
 * The state.
 */
typedef struct BS3CG1STATE
{
    /** @name Instruction details (expanded from BS3CG1INSTR).
     * @{ */
    /** Pointer to the mnemonic string (not terminated) (g_achBs3Cg1Mnemonics). */
    const char BS3_FAR     *pchMnemonic;
    /** Pointer to the test header. */
    PCBS3CG1TESTHDR         pTestHdr;
    /** Pointer to the per operand flags (g_abBs3Cg1Operands). */
    const uint8_t BS3_FAR  *pabOperands;
    /** Opcode bytes (g_abBs3Cg1Opcodes). */
    const uint8_t BS3_FAR  *pabOpcodes;
    /** The current instruction number in the input array (for error reporting). */
    uint32_t                iInstr;

    /** The instruction flags. */
    uint32_t                fFlags;
    /** The encoding. */
    BS3CG1ENC               enmEncoding;
    /** The non-invalid encoding.  This may differ from enmEncoding when
     * Bs3Cg1CalcNoneIntelInvalidEncoding has been called. */
    BS3CG1ENC               enmEncodingNonInvalid;
    /** The CPU test / CPU ID. */
    BS3CG1CPU               enmCpuTest;
    /** Prefix sensitivity and requirements. */
    BS3CG1PFXKIND           enmPrefixKind;
    /** Exception type (SSE, AVX). */
    BS3CG1XCPTTYPE          enmXcptType;
    /** Per operand flags. */
    BS3CG1OP                aenmOperands[4];
    /** Opcode bytes. */
    uint8_t                 abOpcodes[4];
    /** The instruction encoder. */
    PFNBS3CG1ENCODER        pfnEncoder;

    /** The length of the mnemonic. */
    uint8_t                 cchMnemonic;
    /** Whether to advance the mnemonic pointer or not. */
    uint8_t                 fAdvanceMnemonic;
    /** The opcode map number.  */
    uint8_t                 uOpcodeMap;
    /** The number of opcode bytes.   */
    uint8_t                 cbOpcodes;
    /** Number of operands. */
    uint8_t                 cOperands;
    /** @} */

    /** Default operand size. */
    uint8_t                 cbOpDefault;
    /** Operand size when overridden by 066h. */
    uint8_t                 cbOpOvrd66;
    /** Operand size when overridden by REX.W. */
    uint8_t                 cbOpOvrdRexW;

    /** Operand size in bytes (0 if not applicable). */
    uint8_t                 cbOperand;
    /** Current VEX.L value (UINT8_MAX if not applicable). */
    uint8_t                 uVexL;
    /** Current target ring (0..3). */
    uint8_t                 uCpl;

    /** The current test number. */
    uint8_t                 iTest;

    /** Target mode (g_bBs3CurrentMode).  */
    uint8_t                 bMode;
    /** The CPU vendor (BS3CPUVENDOR). */
    uint8_t                 bCpuVendor;
    /** First ring being tested. */
    uint8_t                 iFirstRing;
    /** End of rings being tested. */
    uint8_t                 iEndRing;

    /** @name Current encoded instruction.
     * @{ */
    /** The size of the current instruction that we're testing. */
    uint8_t                 cbCurInstr;
    /** The size the prefixes. */
    uint8_t                 cbCurPrefix;
    /** The offset into abCurInstr of the immediate. */
    uint8_t                 offCurImm;
    /** Buffer for assembling the current instruction. */
    uint8_t                 abCurInstr[23];

    /** Set if the encoding can't be tested in the same ring as this test code.
     *  This is used to deal with encodings modifying SP/ESP/RSP. */
    bool                    fSameRingNotOkay;
    /** Whether to work the extended context too. */
    bool                    fWorkExtCtx;
    /** The aOperands index of the modrm.reg operand (if applicable). */
    uint8_t                 iRegOp;
    /** The aOperands index of the modrm.rm operand (if applicable). */
    uint8_t                 iRmOp;

    /** Operands details. */
    struct
    {
        uint8_t             cbOp;
        /** BS3CG1OPLOC_XXX. */
        uint8_t             enmLocation;
        /** BS3CG1OPLOC_XXX for memory encodings (MODRM.rm field). */
        uint8_t             enmLocationMem : 4;
        /** BS3CG1OPLOC_XXX for register encodings (MODRM.rm field). */
        uint8_t             enmLocationReg : 4;
        /** The BS3CG1DST value for this field.
         * Set to BS3CG1DST_INVALID if memory or immediate.  */
        uint8_t             idxField;
        /** The base BS3CG1DST value for this field.
         * Used only by some generalized encoders when dealing with registers. */
        uint8_t             idxFieldBase;
        /** Depends on enmLocation.
         * - BS3CG1OPLOC_IMM:       offset relative to start of the instruction.
         * - BS3CG1OPLOC_MEM:       offset should be subtracted from &pbDataPg[_4K].
         * - BS3CG1OPLOC_MEM_RW:    offset should be subtracted from &pbDataPg[_4K].
         * - BS3CG1OPLOC_MEM_RO:    offset should be subtracted from &pbDataPg[_4K].
         * - BS3CG1OPLOC_CTX:       not used (use idxField instead).
         */
        uint8_t             off;
    } aOperands[4];
    /** @} */

    /** Page to put code in.  When paging is enabled, the page before and after
     * are marked not-present. */
    uint8_t BS3_FAR        *pbCodePg;
    /** The flat address corresponding to pbCodePg.  */
    uintptr_t               uCodePgFlat;
    /** The 16-bit address corresponding to pbCodePg if relevant for bMode.  */
    RTFAR16                 CodePgFar;
    /** The IP/EIP/RIP value for pbCodePg[0] relative to CS (bMode). */
    uintptr_t               CodePgRip;

    /** Page for placing data operands in.  When paging is enabled, the page before
     * and after are marked not-present.  */
    uint8_t BS3_FAR        *pbDataPg;
    /** The flat address corresponding to pbDataPg.  */
    uintptr_t               uDataPgFlat;
    /** The 16-bit address corresponding to pbDataPg.  */
    RTFAR16                 DataPgFar;

    /** The name corresponding to bMode. */
    const char BS3_FAR     *pszMode;
    /** The short name corresponding to bMode. */
    const char BS3_FAR     *pszModeShort;

    /** @name Expected result (modifiable by output program).
     * @{ */
    /** The expected exception based on operand values or result.
     * UINT8_MAX if no special exception expected. */
    uint8_t                 bValueXcpt;
    /** @} */
    /** Alignment exception expected by the encoder.
     * UINT8_MAX if no special exception expected. */
    uint8_t                 bAlignmentXcpt;
    /** Set by the encoding method to indicating invalid encoding. */
    bool                    fInvalidEncoding;
    /** The result of Bs3Cg1CpuSetupFirst(). */
    bool                    fCpuSetupFirstResult;

    /** The context we're working on. */
    BS3REGCTX               Ctx;
    /** The trap context and frame. */
    BS3TRAPFRAME            TrapFrame;
    /** Initial contexts, one for each ring. */
    BS3REGCTX               aInitialCtxs[4];

    /** The extended context we're working on (input, expected output). */
    PBS3EXTCTX              pExtCtx;
    /** The extended result context (analoguous to TrapFrame). */
    PBS3EXTCTX              pResultExtCtx;
    /** The initial extended context. */
    PBS3EXTCTX              pInitialExtCtx;

    /** Memory operand scratch space. */
    union
    {
        uint8_t             ab[128];
        uint16_t            au16[128 / sizeof(uint16_t)];
        uint32_t            au32[128 / sizeof(uint32_t)];
        uint64_t            au64[128 / sizeof(uint64_t)];
    } MemOp;

    /** Array parallel to aInitialCtxs for saving segment registers. */
    struct
    {
        RTSEL               ds;
    } aSavedSegRegs[4];

} BS3CG1STATE;


#define BS3CG1_PF_OZ  UINT16_C(0x0001)
#define BS3CG1_PF_AZ  UINT16_C(0x0002)
#define BS3CG1_PF_CS  UINT16_C(0x0004)
#define BS3CG1_PF_DS  UINT16_C(0x0008)
#define BS3CG1_PF_ES  UINT16_C(0x0010)
#define BS3CG1_PF_FS  UINT16_C(0x0020)
#define BS3CG1_PF_GS  UINT16_C(0x0040)
#define BS3CG1_PF_SS  UINT16_C(0x0080)
#define BS3CG1_PF_SEGS (BS3CG1_PF_CS | BS3CG1_PF_DS | BS3CG1_PF_ES | BS3CG1_PF_FS | BS3CG1_PF_GS | BS3CG1_PF_SS)
#define BS3CG1_PF_MEM  (BS3CG1_PF_SEGS | BS3CG1_PF_AZ)
#define BS3CG1_PF_LK  UINT16_C(0x0100)
#define BS3CG1_PF_RN  UINT16_C(0x0200)
#define BS3CG1_PF_RZ  UINT16_C(0x0400)
#define BS3CG1_PF_W   UINT16_C(0x0800) /**< REX.W */
#define BS3CG1_PF_R   UINT16_C(0x1000) /**< REX.R */
#define BS3CG1_PF_B   UINT16_C(0x2000) /**< REX.B */
#define BS3CG1_PF_X   UINT16_C(0x4000) /**< REX.X */


/** Used in g_cbBs3Cg1DstFields to indicate that it's one of the 4 operands. */
#define BS3CG1DSTSIZE_OPERAND               UINT8_C(255)
/** Used in g_cbBs3Cg1DstFields to indicate that the operand size determins
 * the field size (2, 4, or 8). */
#define BS3CG1DSTSIZE_OPERAND_SIZE_GRP      UINT8_C(254)



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Destination field sizes indexed by bBS3CG1DST.
 * Zero means operand size sized.  */
static const uint8_t g_acbBs3Cg1DstFields[] =
{
    /* [BS3CG1DST_INVALID] = */ BS3CG1DSTSIZE_OPERAND,

    /* [BS3CG1DST_OP1] = */     BS3CG1DSTSIZE_OPERAND,
    /* [BS3CG1DST_OP2] = */     BS3CG1DSTSIZE_OPERAND,
    /* [BS3CG1DST_OP3] = */     BS3CG1DSTSIZE_OPERAND,
    /* [BS3CG1DST_OP4] = */     BS3CG1DSTSIZE_OPERAND,
    /* [BS3CG1DST_EFL] = */     4,
    /* [BS3CG1DST_EFL_UNDEF]=*/ 4,

    /* [BS3CG1DST_AL] = */      1,
    /* [BS3CG1DST_CL] = */      1,
    /* [BS3CG1DST_DL] = */      1,
    /* [BS3CG1DST_BL] = */      1,
    /* [BS3CG1DST_AH] = */      1,
    /* [BS3CG1DST_CH] = */      1,
    /* [BS3CG1DST_DH] = */      1,
    /* [BS3CG1DST_BH] = */      1,
    /* [BS3CG1DST_SPL] = */     1,
    /* [BS3CG1DST_BPL] = */     1,
    /* [BS3CG1DST_SIL] = */     1,
    /* [BS3CG1DST_DIL] = */     1,
    /* [BS3CG1DST_R8L] = */     1,
    /* [BS3CG1DST_R9L] = */     1,
    /* [BS3CG1DST_R10L] = */    1,
    /* [BS3CG1DST_R11L] = */    1,
    /* [BS3CG1DST_R12L] = */    1,
    /* [BS3CG1DST_R13L] = */    1,
    /* [BS3CG1DST_R14L] = */    1,
    /* [BS3CG1DST_R15L] = */    1,

    /* [BS3CG1DST_AX] = */      2,
    /* [BS3CG1DST_CX] = */      2,
    /* [BS3CG1DST_DX] = */      2,
    /* [BS3CG1DST_BX] = */      2,
    /* [BS3CG1DST_SP] = */      2,
    /* [BS3CG1DST_BP] = */      2,
    /* [BS3CG1DST_SI] = */      2,
    /* [BS3CG1DST_DI] = */      2,
    /* [BS3CG1DST_R8W] = */     2,
    /* [BS3CG1DST_R9W] = */     2,
    /* [BS3CG1DST_R10W] = */    2,
    /* [BS3CG1DST_R11W] = */    2,
    /* [BS3CG1DST_R12W] = */    2,
    /* [BS3CG1DST_R13W] = */    2,
    /* [BS3CG1DST_R14W] = */    2,
    /* [BS3CG1DST_R15W] = */    2,

    /* [BS3CG1DST_EAX] = */     4,
    /* [BS3CG1DST_ECX] = */     4,
    /* [BS3CG1DST_EDX] = */     4,
    /* [BS3CG1DST_EBX] = */     4,
    /* [BS3CG1DST_ESP] = */     4,
    /* [BS3CG1DST_EBP] = */     4,
    /* [BS3CG1DST_ESI] = */     4,
    /* [BS3CG1DST_EDI] = */     4,
    /* [BS3CG1DST_R8D] = */     4,
    /* [BS3CG1DST_R9D] = */     4,
    /* [BS3CG1DST_R10D] = */    4,
    /* [BS3CG1DST_R11D] = */    4,
    /* [BS3CG1DST_R12D] = */    4,
    /* [BS3CG1DST_R13D] = */    4,
    /* [BS3CG1DST_R14D] = */    4,
    /* [BS3CG1DST_R15D] = */    4,

    /* [BS3CG1DST_RAX] = */     8,
    /* [BS3CG1DST_RCX] = */     8,
    /* [BS3CG1DST_RDX] = */     8,
    /* [BS3CG1DST_RBX] = */     8,
    /* [BS3CG1DST_RSP] = */     8,
    /* [BS3CG1DST_RBP] = */     8,
    /* [BS3CG1DST_RSI] = */     8,
    /* [BS3CG1DST_RDI] = */     8,
    /* [BS3CG1DST_R8] = */      8,
    /* [BS3CG1DST_R9] = */      8,
    /* [BS3CG1DST_R10] = */     8,
    /* [BS3CG1DST_R11] = */     8,
    /* [BS3CG1DST_R12] = */     8,
    /* [BS3CG1DST_R13] = */     8,
    /* [BS3CG1DST_R14] = */     8,
    /* [BS3CG1DST_R15] = */     8,

    /* [BS3CG1DST_OZ_RAX] = */  BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_RCX] = */  BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_RDX] = */  BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_RBX] = */  BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_RSP] = */  BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_RBP] = */  BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_RSI] = */  BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_RDI] = */  BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_R8] = */   BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_R9] = */   BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_R10] = */  BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_R11] = */  BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_R12] = */  BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_R13] = */  BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_R14] = */  BS3CG1DSTSIZE_OPERAND_SIZE_GRP,
    /* [BS3CG1DST_OZ_R15] = */  BS3CG1DSTSIZE_OPERAND_SIZE_GRP,

    /* [BS3CG1DST_CR0] = */     4,
    /* [BS3CG1DST_CR4] = */     4,
    /* [BS3CG1DST_XCR0] = */    8,

    /* [BS3CG1DST_FCW] = */         2,
    /* [BS3CG1DST_FSW] = */         2,
    /* [BS3CG1DST_FTW] = */         2,
    /* [BS3CG1DST_FOP] = */         2,
    /* [BS3CG1DST_FPUIP] = */       2,
    /* [BS3CG1DST_FPUCS] = */       2,
    /* [BS3CG1DST_FPUDP] = */       2,
    /* [BS3CG1DST_FPUDS] = */       2,
    /* [BS3CG1DST_MXCSR] = */       4,
    /* [BS3CG1DST_ST0] = */         12,
    /* [BS3CG1DST_ST1] = */         12,
    /* [BS3CG1DST_ST2] = */         12,
    /* [BS3CG1DST_ST3] = */         12,
    /* [BS3CG1DST_ST4] = */         12,
    /* [BS3CG1DST_ST5] = */         12,
    /* [BS3CG1DST_ST6] = */         12,
    /* [BS3CG1DST_ST7] = */         12,
    /* [BS3CG1DST_MM0] = */         8,
    /* [BS3CG1DST_MM1] = */         8,
    /* [BS3CG1DST_MM2] = */         8,
    /* [BS3CG1DST_MM3] = */         8,
    /* [BS3CG1DST_MM4] = */         8,
    /* [BS3CG1DST_MM5] = */         8,
    /* [BS3CG1DST_MM6] = */         8,
    /* [BS3CG1DST_MM7] = */         8,
    /* [BS3CG1DST_MM0_LO_ZX] = */   4,
    /* [BS3CG1DST_MM1_LO_ZX] = */   4,
    /* [BS3CG1DST_MM2_LO_ZX] = */   4,
    /* [BS3CG1DST_MM3_LO_ZX] = */   4,
    /* [BS3CG1DST_MM4_LO_ZX] = */   4,
    /* [BS3CG1DST_MM5_LO_ZX] = */   4,
    /* [BS3CG1DST_MM6_LO_ZX] = */   4,
    /* [BS3CG1DST_MM7_LO_ZX] = */   4,
    /* [BS3CG1DST_XMM0] = */        16,
    /* [BS3CG1DST_XMM1] = */        16,
    /* [BS3CG1DST_XMM2] = */        16,
    /* [BS3CG1DST_XMM3] = */        16,
    /* [BS3CG1DST_XMM4] = */        16,
    /* [BS3CG1DST_XMM5] = */        16,
    /* [BS3CG1DST_XMM6] = */        16,
    /* [BS3CG1DST_XMM7] = */        16,
    /* [BS3CG1DST_XMM8] = */        16,
    /* [BS3CG1DST_XMM9] = */        16,
    /* [BS3CG1DST_XMM10] = */       16,
    /* [BS3CG1DST_XMM11] = */       16,
    /* [BS3CG1DST_XMM12] = */       16,
    /* [BS3CG1DST_XMM13] = */       16,
    /* [BS3CG1DST_XMM14] = */       16,
    /* [BS3CG1DST_XMM15] = */       16,
    /* [BS3CG1DST_XMM0_LO] = */     8,
    /* [BS3CG1DST_XMM1_LO] = */     8,
    /* [BS3CG1DST_XMM2_LO] = */     8,
    /* [BS3CG1DST_XMM3_LO] = */     8,
    /* [BS3CG1DST_XMM4_LO] = */     8,
    /* [BS3CG1DST_XMM5_LO] = */     8,
    /* [BS3CG1DST_XMM6_LO] = */     8,
    /* [BS3CG1DST_XMM7_LO] = */     8,
    /* [BS3CG1DST_XMM8_LO] = */     8,
    /* [BS3CG1DST_XMM9_LO] = */     8,
    /* [BS3CG1DST_XMM10_LO] = */    8,
    /* [BS3CG1DST_XMM11_LO] = */    8,
    /* [BS3CG1DST_XMM12_LO] = */    8,
    /* [BS3CG1DST_XMM13_LO] = */    8,
    /* [BS3CG1DST_XMM14_LO] = */    8,
    /* [BS3CG1DST_XMM15_LO] = */    8,
    /* [BS3CG1DST_XMM0_HI] = */     8,
    /* [BS3CG1DST_XMM1_HI] = */     8,
    /* [BS3CG1DST_XMM2_HI] = */     8,
    /* [BS3CG1DST_XMM3_HI] = */     8,
    /* [BS3CG1DST_XMM4_HI] = */     8,
    /* [BS3CG1DST_XMM5_HI] = */     8,
    /* [BS3CG1DST_XMM6_HI] = */     8,
    /* [BS3CG1DST_XMM7_HI] = */     8,
    /* [BS3CG1DST_XMM8_HI] = */     8,
    /* [BS3CG1DST_XMM9_HI] = */     8,
    /* [BS3CG1DST_XMM10_HI] = */    8,
    /* [BS3CG1DST_XMM11_HI] = */    8,
    /* [BS3CG1DST_XMM12_HI] = */    8,
    /* [BS3CG1DST_XMM13_HI] = */    8,
    /* [BS3CG1DST_XMM14_HI] = */    8,
    /* [BS3CG1DST_XMM15_HI] = */    8,
    /* [BS3CG1DST_XMM0_LO_ZX] = */  8,
    /* [BS3CG1DST_XMM1_LO_ZX] = */  8,
    /* [BS3CG1DST_XMM2_LO_ZX] = */  8,
    /* [BS3CG1DST_XMM3_LO_ZX] = */  8,
    /* [BS3CG1DST_XMM4_LO_ZX] = */  8,
    /* [BS3CG1DST_XMM5_LO_ZX] = */  8,
    /* [BS3CG1DST_XMM6_LO_ZX] = */  8,
    /* [BS3CG1DST_XMM7_LO_ZX] = */  8,
    /* [BS3CG1DST_XMM8_LO_ZX] = */  8,
    /* [BS3CG1DST_XMM9_LO_ZX] = */  8,
    /* [BS3CG1DST_XMM10_LO_ZX] = */ 8,
    /* [BS3CG1DST_XMM11_LO_ZX] = */ 8,
    /* [BS3CG1DST_XMM12_LO_ZX] = */ 8,
    /* [BS3CG1DST_XMM13_LO_ZX] = */ 8,
    /* [BS3CG1DST_XMM14_LO_ZX] = */ 8,
    /* [BS3CG1DST_XMM15_LO_ZX] = */ 8,
    /* [BS3CG1DST_XMM0_DW0] = */    4,
    /* [BS3CG1DST_XMM1_DW0] = */    4,
    /* [BS3CG1DST_XMM2_DW0] = */    4,
    /* [BS3CG1DST_XMM3_DW0] = */    4,
    /* [BS3CG1DST_XMM4_DW0] = */    4,
    /* [BS3CG1DST_XMM5_DW0] = */    4,
    /* [BS3CG1DST_XMM6_DW0] = */    4,
    /* [BS3CG1DST_XMM7_DW0] = */    4,
    /* [BS3CG1DST_XMM8_DW0] = */    4,
    /* [BS3CG1DST_XMM9_DW0] = */    4,
    /* [BS3CG1DST_XMM10_DW0] = */   4,
    /* [BS3CG1DST_XMM11_DW0] = */   4,
    /* [BS3CG1DST_XMM12_DW0] = */   4,
    /* [BS3CG1DST_XMM13_DW0] = */   4,
    /* [BS3CG1DST_XMM14_DW0] = */   4,
    /* [BS3CG1DST_XMM15_DW0] = */   4,
    /* [BS3CG1DST_XMM0_DW0_ZX] = */ 4,
    /* [BS3CG1DST_XMM1_DW0_ZX] = */ 4,
    /* [BS3CG1DST_XMM2_DW0_ZX] = */ 4,
    /* [BS3CG1DST_XMM3_DW0_ZX] = */ 4,
    /* [BS3CG1DST_XMM4_DW0_ZX] = */ 4,
    /* [BS3CG1DST_XMM5_DW0_ZX] = */ 4,
    /* [BS3CG1DST_XMM6_DW0_ZX] = */ 4,
    /* [BS3CG1DST_XMM7_DW0_ZX] = */ 4,
    /* [BS3CG1DST_XMM8_DW0_ZX] = */ 4,
    /* [BS3CG1DST_XMM9_DW0_ZX] = */ 4,
    /* [BS3CG1DST_XMM10_DW0_ZX] =*/ 4,
    /* [BS3CG1DST_XMM11_DW0_ZX] =*/ 4,
    /* [BS3CG1DST_XMM12_DW0_ZX] =*/ 4,
    /* [BS3CG1DST_XMM13_DW0_ZX] =*/ 4,
    /* [BS3CG1DST_XMM14_DW0_ZX] =*/ 4,
    /* [BS3CG1DST_XMM15_DW0_ZX] =*/ 4,
    /* [BS3CG1DST_XMM0_HI96] = */   12,
    /* [BS3CG1DST_XMM1_HI96] = */   12,
    /* [BS3CG1DST_XMM2_HI96] = */   12,
    /* [BS3CG1DST_XMM3_HI96] = */   12,
    /* [BS3CG1DST_XMM4_HI96] = */   12,
    /* [BS3CG1DST_XMM5_HI96] = */   12,
    /* [BS3CG1DST_XMM6_HI96] = */   12,
    /* [BS3CG1DST_XMM7_HI96] = */   12,
    /* [BS3CG1DST_XMM8_HI96] = */   12,
    /* [BS3CG1DST_XMM9_HI96] = */   12,
    /* [BS3CG1DST_XMM10_HI96] =*/   12,
    /* [BS3CG1DST_XMM11_HI96] =*/   12,
    /* [BS3CG1DST_XMM12_HI96] =*/   12,
    /* [BS3CG1DST_XMM13_HI96] =*/   12,
    /* [BS3CG1DST_XMM14_HI96] =*/   12,
    /* [BS3CG1DST_XMM15_HI96] =*/   12,
    /* [BS3CG1DST_YMM0] = */        32,
    /* [BS3CG1DST_YMM1] = */        32,
    /* [BS3CG1DST_YMM2] = */        32,
    /* [BS3CG1DST_YMM3] = */        32,
    /* [BS3CG1DST_YMM4] = */        32,
    /* [BS3CG1DST_YMM5] = */        32,
    /* [BS3CG1DST_YMM6] = */        32,
    /* [BS3CG1DST_YMM7] = */        32,
    /* [BS3CG1DST_YMM8] = */        32,
    /* [BS3CG1DST_YMM9] = */        32,
    /* [BS3CG1DST_YMM10] = */       32,
    /* [BS3CG1DST_YMM11] = */       32,
    /* [BS3CG1DST_YMM12] = */       32,
    /* [BS3CG1DST_YMM13] = */       32,
    /* [BS3CG1DST_YMM14] = */       32,
    /* [BS3CG1DST_YMM15] = */       32,

    /* [BS3CG1DST_VALUE_XCPT] = */ 1,
};
AssertCompile(RT_ELEMENTS(g_acbBs3Cg1DstFields) == BS3CG1DST_END);

/** Destination field offset indexed by bBS3CG1DST.
 * Zero means operand size sized.  */
static const unsigned g_aoffBs3Cg1DstFields[] =
{
    /* [BS3CG1DST_INVALID] = */     ~0U,
    /* [BS3CG1DST_OP1] = */         ~0U,
    /* [BS3CG1DST_OP2] = */         ~0U,
    /* [BS3CG1DST_OP3] = */         ~0U,
    /* [BS3CG1DST_OP4] = */         ~0U,
    /* [BS3CG1DST_EFL] = */         RT_OFFSETOF(BS3REGCTX, rflags),
    /* [BS3CG1DST_EFL_UNDEF]=*/     ~0, /* special field */

    /* [BS3CG1DST_AL] = */          RT_OFFSETOF(BS3REGCTX, rax.u8),
    /* [BS3CG1DST_CL] = */          RT_OFFSETOF(BS3REGCTX, rcx.u8),
    /* [BS3CG1DST_DL] = */          RT_OFFSETOF(BS3REGCTX, rdx.u8),
    /* [BS3CG1DST_BL] = */          RT_OFFSETOF(BS3REGCTX, rbx.u8),
    /* [BS3CG1DST_AH] = */          RT_OFFSETOF(BS3REGCTX, rax.b.bHi),
    /* [BS3CG1DST_CH] = */          RT_OFFSETOF(BS3REGCTX, rcx.b.bHi),
    /* [BS3CG1DST_DH] = */          RT_OFFSETOF(BS3REGCTX, rdx.b.bHi),
    /* [BS3CG1DST_BH] = */          RT_OFFSETOF(BS3REGCTX, rbx.b.bHi),
    /* [BS3CG1DST_SPL] = */         RT_OFFSETOF(BS3REGCTX, rsp.u8),
    /* [BS3CG1DST_BPL] = */         RT_OFFSETOF(BS3REGCTX, rbp.u8),
    /* [BS3CG1DST_SIL] = */         RT_OFFSETOF(BS3REGCTX, rsi.u8),
    /* [BS3CG1DST_DIL] = */         RT_OFFSETOF(BS3REGCTX, rdi.u8),
    /* [BS3CG1DST_R8L] = */         RT_OFFSETOF(BS3REGCTX, r8.u8),
    /* [BS3CG1DST_R9L] = */         RT_OFFSETOF(BS3REGCTX, r9.u8),
    /* [BS3CG1DST_R10L] = */        RT_OFFSETOF(BS3REGCTX, r10.u8),
    /* [BS3CG1DST_R11L] = */        RT_OFFSETOF(BS3REGCTX, r11.u8),
    /* [BS3CG1DST_R12L] = */        RT_OFFSETOF(BS3REGCTX, r12.u8),
    /* [BS3CG1DST_R13L] = */        RT_OFFSETOF(BS3REGCTX, r13.u8),
    /* [BS3CG1DST_R14L] = */        RT_OFFSETOF(BS3REGCTX, r14.u8),
    /* [BS3CG1DST_R15L] = */        RT_OFFSETOF(BS3REGCTX, r15.u8),

    /* [BS3CG1DST_AX] = */          RT_OFFSETOF(BS3REGCTX, rax.u16),
    /* [BS3CG1DST_CX] = */          RT_OFFSETOF(BS3REGCTX, rcx.u16),
    /* [BS3CG1DST_DX] = */          RT_OFFSETOF(BS3REGCTX, rdx.u16),
    /* [BS3CG1DST_BX] = */          RT_OFFSETOF(BS3REGCTX, rbx.u16),
    /* [BS3CG1DST_SP] = */          RT_OFFSETOF(BS3REGCTX, rsp.u16),
    /* [BS3CG1DST_BP] = */          RT_OFFSETOF(BS3REGCTX, rbp.u16),
    /* [BS3CG1DST_SI] = */          RT_OFFSETOF(BS3REGCTX, rsi.u16),
    /* [BS3CG1DST_DI] = */          RT_OFFSETOF(BS3REGCTX, rdi.u16),
    /* [BS3CG1DST_R8W] = */         RT_OFFSETOF(BS3REGCTX, r8.u16),
    /* [BS3CG1DST_R9W] = */         RT_OFFSETOF(BS3REGCTX, r9.u16),
    /* [BS3CG1DST_R10W] = */        RT_OFFSETOF(BS3REGCTX, r10.u16),
    /* [BS3CG1DST_R11W] = */        RT_OFFSETOF(BS3REGCTX, r11.u16),
    /* [BS3CG1DST_R12W] = */        RT_OFFSETOF(BS3REGCTX, r12.u16),
    /* [BS3CG1DST_R13W] = */        RT_OFFSETOF(BS3REGCTX, r13.u16),
    /* [BS3CG1DST_R14W] = */        RT_OFFSETOF(BS3REGCTX, r14.u16),
    /* [BS3CG1DST_R15W] = */        RT_OFFSETOF(BS3REGCTX, r15.u16),

    /* [BS3CG1DST_EAX] = */         RT_OFFSETOF(BS3REGCTX, rax.u32),
    /* [BS3CG1DST_ECX] = */         RT_OFFSETOF(BS3REGCTX, rcx.u32),
    /* [BS3CG1DST_EDX] = */         RT_OFFSETOF(BS3REGCTX, rdx.u32),
    /* [BS3CG1DST_EBX] = */         RT_OFFSETOF(BS3REGCTX, rbx.u32),
    /* [BS3CG1DST_ESP] = */         RT_OFFSETOF(BS3REGCTX, rsp.u32),
    /* [BS3CG1DST_EBP] = */         RT_OFFSETOF(BS3REGCTX, rbp.u32),
    /* [BS3CG1DST_ESI] = */         RT_OFFSETOF(BS3REGCTX, rsi.u32),
    /* [BS3CG1DST_EDI] = */         RT_OFFSETOF(BS3REGCTX, rdi.u32),
    /* [BS3CG1DST_R8D] = */         RT_OFFSETOF(BS3REGCTX, r8.u32),
    /* [BS3CG1DST_R9D] = */         RT_OFFSETOF(BS3REGCTX, r9.u32),
    /* [BS3CG1DST_R10D] = */        RT_OFFSETOF(BS3REGCTX, r10.u32),
    /* [BS3CG1DST_R11D] = */        RT_OFFSETOF(BS3REGCTX, r11.u32),
    /* [BS3CG1DST_R12D] = */        RT_OFFSETOF(BS3REGCTX, r12.u32),
    /* [BS3CG1DST_R13D] = */        RT_OFFSETOF(BS3REGCTX, r13.u32),
    /* [BS3CG1DST_R14D] = */        RT_OFFSETOF(BS3REGCTX, r14.u32),
    /* [BS3CG1DST_R15D] = */        RT_OFFSETOF(BS3REGCTX, r15.u32),

    /* [BS3CG1DST_RAX] = */         RT_OFFSETOF(BS3REGCTX, rax.u64),
    /* [BS3CG1DST_RCX] = */         RT_OFFSETOF(BS3REGCTX, rcx.u64),
    /* [BS3CG1DST_RDX] = */         RT_OFFSETOF(BS3REGCTX, rdx.u64),
    /* [BS3CG1DST_RBX] = */         RT_OFFSETOF(BS3REGCTX, rbx.u64),
    /* [BS3CG1DST_RSP] = */         RT_OFFSETOF(BS3REGCTX, rsp.u64),
    /* [BS3CG1DST_RBP] = */         RT_OFFSETOF(BS3REGCTX, rbp.u64),
    /* [BS3CG1DST_RSI] = */         RT_OFFSETOF(BS3REGCTX, rsi.u64),
    /* [BS3CG1DST_RDI] = */         RT_OFFSETOF(BS3REGCTX, rdi.u64),
    /* [BS3CG1DST_R8] = */          RT_OFFSETOF(BS3REGCTX, r8.u64),
    /* [BS3CG1DST_R9] = */          RT_OFFSETOF(BS3REGCTX, r9.u64),
    /* [BS3CG1DST_R10] = */         RT_OFFSETOF(BS3REGCTX, r10.u64),
    /* [BS3CG1DST_R11] = */         RT_OFFSETOF(BS3REGCTX, r11.u64),
    /* [BS3CG1DST_R12] = */         RT_OFFSETOF(BS3REGCTX, r12.u64),
    /* [BS3CG1DST_R13] = */         RT_OFFSETOF(BS3REGCTX, r13.u64),
    /* [BS3CG1DST_R14] = */         RT_OFFSETOF(BS3REGCTX, r14.u64),
    /* [BS3CG1DST_R15] = */         RT_OFFSETOF(BS3REGCTX, r15.u64),

    /* [BS3CG1DST_OZ_RAX] = */      RT_OFFSETOF(BS3REGCTX, rax),
    /* [BS3CG1DST_OZ_RCX] = */      RT_OFFSETOF(BS3REGCTX, rcx),
    /* [BS3CG1DST_OZ_RDX] = */      RT_OFFSETOF(BS3REGCTX, rdx),
    /* [BS3CG1DST_OZ_RBX] = */      RT_OFFSETOF(BS3REGCTX, rbx),
    /* [BS3CG1DST_OZ_RSP] = */      RT_OFFSETOF(BS3REGCTX, rsp),
    /* [BS3CG1DST_OZ_RBP] = */      RT_OFFSETOF(BS3REGCTX, rbp),
    /* [BS3CG1DST_OZ_RSI] = */      RT_OFFSETOF(BS3REGCTX, rsi),
    /* [BS3CG1DST_OZ_RDI] = */      RT_OFFSETOF(BS3REGCTX, rdi),
    /* [BS3CG1DST_OZ_R8] = */       RT_OFFSETOF(BS3REGCTX, r8),
    /* [BS3CG1DST_OZ_R9] = */       RT_OFFSETOF(BS3REGCTX, r9),
    /* [BS3CG1DST_OZ_R10] = */      RT_OFFSETOF(BS3REGCTX, r10),
    /* [BS3CG1DST_OZ_R11] = */      RT_OFFSETOF(BS3REGCTX, r11),
    /* [BS3CG1DST_OZ_R12] = */      RT_OFFSETOF(BS3REGCTX, r12),
    /* [BS3CG1DST_OZ_R13] = */      RT_OFFSETOF(BS3REGCTX, r13),
    /* [BS3CG1DST_OZ_R14] = */      RT_OFFSETOF(BS3REGCTX, r14),
    /* [BS3CG1DST_OZ_R15] = */      RT_OFFSETOF(BS3REGCTX, r15),

    /* [BS3CG1DST_CR0] = */         RT_OFFSETOF(BS3REGCTX, cr0),
    /* [BS3CG1DST_CR4] = */         RT_OFFSETOF(BS3REGCTX, cr4),
    /* [BS3CG1DST_XCR0] = */        sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, fXcr0Saved),

    /* [BS3CG1DST_FCW] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.FCW),
    /* [BS3CG1DST_FSW] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.FSW),
    /* [BS3CG1DST_FTW] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.FTW),
    /* [BS3CG1DST_FOP] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.FOP),
    /* [BS3CG1DST_FPUIP] = */       sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.FPUIP),
    /* [BS3CG1DST_FPUCS] = */       sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.CS),
    /* [BS3CG1DST_FPUDP] = */       sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.FPUDP),
    /* [BS3CG1DST_FPUDS] = */       sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.DS),
    /* [BS3CG1DST_MXCSR] = */       sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.MXCSR),
    /* [BS3CG1DST_ST0] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[0]),
    /* [BS3CG1DST_ST1] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[1]),
    /* [BS3CG1DST_ST2] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[2]),
    /* [BS3CG1DST_ST3] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[3]),
    /* [BS3CG1DST_ST4] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[4]),
    /* [BS3CG1DST_ST5] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[5]),
    /* [BS3CG1DST_ST6] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[6]),
    /* [BS3CG1DST_ST7] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[7]),
    /* [BS3CG1DST_MM0] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[0]),
    /* [BS3CG1DST_MM1] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[1]),
    /* [BS3CG1DST_MM2] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[2]),
    /* [BS3CG1DST_MM3] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[3]),
    /* [BS3CG1DST_MM4] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[4]),
    /* [BS3CG1DST_MM5] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[5]),
    /* [BS3CG1DST_MM6] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[6]),
    /* [BS3CG1DST_MM7] = */         sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[7]),
    /* [BS3CG1DST_MM0_LO_ZX] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[0]),
    /* [BS3CG1DST_MM1_LO_ZX] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[1]),
    /* [BS3CG1DST_MM2_LO_ZX] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[2]),
    /* [BS3CG1DST_MM3_LO_ZX] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[3]),
    /* [BS3CG1DST_MM4_LO_ZX] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[4]),
    /* [BS3CG1DST_MM5_LO_ZX] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[5]),
    /* [BS3CG1DST_MM6_LO_ZX] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[6]),
    /* [BS3CG1DST_MM7_LO_ZX] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aRegs[7]),

    /* [BS3CG1DST_XMM0] = */        sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[0]),
    /* [BS3CG1DST_XMM1] = */        sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[1]),
    /* [BS3CG1DST_XMM2] = */        sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[2]),
    /* [BS3CG1DST_XMM3] = */        sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[3]),
    /* [BS3CG1DST_XMM4] = */        sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[4]),
    /* [BS3CG1DST_XMM5] = */        sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[5]),
    /* [BS3CG1DST_XMM6] = */        sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[6]),
    /* [BS3CG1DST_XMM7] = */        sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[7]),
    /* [BS3CG1DST_XMM8] = */        sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[8]),
    /* [BS3CG1DST_XMM9] = */        sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[9]),
    /* [BS3CG1DST_XMM10] = */       sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[10]),
    /* [BS3CG1DST_XMM11] = */       sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[11]),
    /* [BS3CG1DST_XMM12] = */       sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[12]),
    /* [BS3CG1DST_XMM13] = */       sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[13]),
    /* [BS3CG1DST_XMM14] = */       sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[14]),
    /* [BS3CG1DST_XMM15] = */       sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[15]),
    /* [BS3CG1DST_XMM0_LO] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[0]),
    /* [BS3CG1DST_XMM1_LO] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[1]),
    /* [BS3CG1DST_XMM2_LO] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[2]),
    /* [BS3CG1DST_XMM3_LO] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[3]),
    /* [BS3CG1DST_XMM4_LO] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[4]),
    /* [BS3CG1DST_XMM5_LO] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[5]),
    /* [BS3CG1DST_XMM6_LO] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[6]),
    /* [BS3CG1DST_XMM7_LO] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[7]),
    /* [BS3CG1DST_XMM8_LO] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[8]),
    /* [BS3CG1DST_XMM9_LO] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[9]),
    /* [BS3CG1DST_XMM10_LO] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[10]),
    /* [BS3CG1DST_XMM11_LO] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[11]),
    /* [BS3CG1DST_XMM12_LO] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[12]),
    /* [BS3CG1DST_XMM13_LO] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[13]),
    /* [BS3CG1DST_XMM14_LO] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[14]),
    /* [BS3CG1DST_XMM15_LO] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[15]),
    /* [BS3CG1DST_XMM0_HI] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[0])  + sizeof(uint64_t),
    /* [BS3CG1DST_XMM1_HI] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[1])  + sizeof(uint64_t),
    /* [BS3CG1DST_XMM2_HI] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[2])  + sizeof(uint64_t),
    /* [BS3CG1DST_XMM3_HI] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[3])  + sizeof(uint64_t),
    /* [BS3CG1DST_XMM4_HI] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[4])  + sizeof(uint64_t),
    /* [BS3CG1DST_XMM5_HI] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[5])  + sizeof(uint64_t),
    /* [BS3CG1DST_XMM6_HI] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[6])  + sizeof(uint64_t),
    /* [BS3CG1DST_XMM7_HI] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[7])  + sizeof(uint64_t),
    /* [BS3CG1DST_XMM8_HI] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[8])  + sizeof(uint64_t),
    /* [BS3CG1DST_XMM9_HI] = */     sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[9])  + sizeof(uint64_t),
    /* [BS3CG1DST_XMM10_HI] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[10]) + sizeof(uint64_t),
    /* [BS3CG1DST_XMM11_HI] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[11]) + sizeof(uint64_t),
    /* [BS3CG1DST_XMM12_HI] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[12]) + sizeof(uint64_t),
    /* [BS3CG1DST_XMM13_HI] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[13]) + sizeof(uint64_t),
    /* [BS3CG1DST_XMM14_HI] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[14]) + sizeof(uint64_t),
    /* [BS3CG1DST_XMM15_HI] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[15]) + sizeof(uint64_t),
    /* [BS3CG1DST_XMM0_LO_ZX] = */  sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[0]),
    /* [BS3CG1DST_XMM1_LO_ZX] = */  sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[1]),
    /* [BS3CG1DST_XMM2_LO_ZX] = */  sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[2]),
    /* [BS3CG1DST_XMM3_LO_ZX] = */  sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[3]),
    /* [BS3CG1DST_XMM4_LO_ZX] = */  sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[4]),
    /* [BS3CG1DST_XMM5_LO_ZX] = */  sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[5]),
    /* [BS3CG1DST_XMM6_LO_ZX] = */  sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[6]),
    /* [BS3CG1DST_XMM7_LO_ZX] = */  sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[7]),
    /* [BS3CG1DST_XMM8_LO_ZX] = */  sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[8]),
    /* [BS3CG1DST_XMM9_LO_ZX] = */  sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[9]),
    /* [BS3CG1DST_XMM10_LO_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[10]),
    /* [BS3CG1DST_XMM11_LO_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[11]),
    /* [BS3CG1DST_XMM12_LO_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[12]),
    /* [BS3CG1DST_XMM13_LO_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[13]),
    /* [BS3CG1DST_XMM14_LO_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[14]),
    /* [BS3CG1DST_XMM15_LO_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[15]),
    /* [BS3CG1DST_XMM0_DW0] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[0]),
    /* [BS3CG1DST_XMM1_DW0] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[1]),
    /* [BS3CG1DST_XMM2_DW0] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[2]),
    /* [BS3CG1DST_XMM3_DW0] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[3]),
    /* [BS3CG1DST_XMM4_DW0] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[4]),
    /* [BS3CG1DST_XMM5_DW0] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[5]),
    /* [BS3CG1DST_XMM6_DW0] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[6]),
    /* [BS3CG1DST_XMM7_DW0] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[7]),
    /* [BS3CG1DST_XMM8_DW0] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[8]),
    /* [BS3CG1DST_XMM9_DW0] = */    sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[9]),
    /* [BS3CG1DST_XMM10_DW0] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[10]),
    /* [BS3CG1DST_XMM11_DW0] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[11]),
    /* [BS3CG1DST_XMM12_DW0] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[12]),
    /* [BS3CG1DST_XMM13_DW0] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[13]),
    /* [BS3CG1DST_XMM14_DW0] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[14]),
    /* [BS3CG1DST_XMM15_DW0] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[15]),
    /* [BS3CG1DST_XMM0_DW0_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[0]),
    /* [BS3CG1DST_XMM1_DW0_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[1]),
    /* [BS3CG1DST_XMM2_DW0_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[2]),
    /* [BS3CG1DST_XMM3_DW0_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[3]),
    /* [BS3CG1DST_XMM4_DW0_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[4]),
    /* [BS3CG1DST_XMM5_DW0_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[5]),
    /* [BS3CG1DST_XMM6_DW0_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[6]),
    /* [BS3CG1DST_XMM7_DW0_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[7]),
    /* [BS3CG1DST_XMM8_DW0_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[8]),
    /* [BS3CG1DST_XMM9_DW0_ZX] = */ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[9]),
    /* [BS3CG1DST_XMM10_DW0_ZX] =*/ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[10]),
    /* [BS3CG1DST_XMM11_DW0_ZX] =*/ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[11]),
    /* [BS3CG1DST_XMM12_DW0_ZX] =*/ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[12]),
    /* [BS3CG1DST_XMM13_DW0_ZX] =*/ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[13]),
    /* [BS3CG1DST_XMM14_DW0_ZX] =*/ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[14]),
    /* [BS3CG1DST_XMM15_DW0_ZX] =*/ sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[15]),
    /* [BS3CG1DST_XMM0_HI96] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[0].au32[1]),
    /* [BS3CG1DST_XMM1_HI96] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[1].au32[1]),
    /* [BS3CG1DST_XMM2_HI96] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[2].au32[1]),
    /* [BS3CG1DST_XMM3_HI96] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[3].au32[1]),
    /* [BS3CG1DST_XMM4_HI96] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[4].au32[1]),
    /* [BS3CG1DST_XMM5_HI96] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[5].au32[1]),
    /* [BS3CG1DST_XMM6_HI96] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[6].au32[1]),
    /* [BS3CG1DST_XMM7_HI96] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[7].au32[1]),
    /* [BS3CG1DST_XMM8_HI96] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[8].au32[1]),
    /* [BS3CG1DST_XMM9_HI96] = */   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[9].au32[1]),
    /* [BS3CG1DST_XMM10_HI96] =*/   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[10].au32[1]),
    /* [BS3CG1DST_XMM11_HI96] =*/   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[11].au32[1]),
    /* [BS3CG1DST_XMM12_HI96] =*/   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[12].au32[1]),
    /* [BS3CG1DST_XMM13_HI96] =*/   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[13].au32[1]),
    /* [BS3CG1DST_XMM14_HI96] =*/   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[14].au32[1]),
    /* [BS3CG1DST_XMM15_HI96] =*/   sizeof(BS3REGCTX) + RT_OFFSETOF(BS3EXTCTX, Ctx.x87.aXMM[15].au32[1]),

    /* [BS3CG1DST_YMM0] = */        ~0U,
    /* [BS3CG1DST_YMM1] = */        ~0U,
    /* [BS3CG1DST_YMM2] = */        ~0U,
    /* [BS3CG1DST_YMM3] = */        ~0U,
    /* [BS3CG1DST_YMM4] = */        ~0U,
    /* [BS3CG1DST_YMM5] = */        ~0U,
    /* [BS3CG1DST_YMM6] = */        ~0U,
    /* [BS3CG1DST_YMM7] = */        ~0U,
    /* [BS3CG1DST_YMM8] = */        ~0U,
    /* [BS3CG1DST_YMM9] = */        ~0U,
    /* [BS3CG1DST_YMM10] = */       ~0U,
    /* [BS3CG1DST_YMM11] = */       ~0U,
    /* [BS3CG1DST_YMM12] = */       ~0U,
    /* [BS3CG1DST_YMM13] = */       ~0U,
    /* [BS3CG1DST_YMM14] = */       ~0U,
    /* [BS3CG1DST_YMM15] = */       ~0U,

    /* [BS3CG1DST_VALUE_XCPT] = */  ~0U,
};
AssertCompile(RT_ELEMENTS(g_aoffBs3Cg1DstFields) == BS3CG1DST_END);

/** Destination field names. */
static const struct { char sz[12]; } g_aszBs3Cg1DstFields[] =
{
    { "INVALID" },
    { "OP1" },
    { "OP2" },
    { "OP3" },
    { "OP4" },
    { "EFL" },
    { "EFL_UND" },

    { "AL" },
    { "CL" },
    { "DL" },
    { "BL" },
    { "AH" },
    { "CH" },
    { "DH" },
    { "BH" },
    { "SPL" },
    { "BPL" },
    { "SIL" },
    { "DIL" },
    { "R8L" },
    { "R9L" },
    { "R10L" },
    { "R11L" },
    { "R12L" },
    { "R13L" },
    { "R14L" },
    { "R15L" },

    { "AX" },
    { "CX" },
    { "DX" },
    { "BX" },
    { "SP" },
    { "BP" },
    { "SI" },
    { "DI" },
    { "R8W" },
    { "R9W" },
    { "R10W" },
    { "R11W" },
    { "R12W" },
    { "R13W" },
    { "R14W" },
    { "R15W" },

    { "EAX" },
    { "ECX" },
    { "EDX" },
    { "EBX" },
    { "ESP" },
    { "EBP" },
    { "ESI" },
    { "EDI" },
    { "R8D" },
    { "R9D" },
    { "R10D" },
    { "R11D" },
    { "R12D" },
    { "R13D" },
    { "R14D" },
    { "R15D" },

    { "RAX" },
    { "RCX" },
    { "RDX" },
    { "RBX" },
    { "RSP" },
    { "RBP" },
    { "RSI" },
    { "RDI" },
    { "R8"  },
    { "R9"  },
    { "R10" },
    { "R11" },
    { "R12" },
    { "R13" },
    { "R14" },
    { "R15" },

    { "OZ_RAX" },
    { "OZ_RCX" },
    { "OZ_RDX" },
    { "OZ_RBX" },
    { "OZ_RSP" },
    { "OZ_RBP" },
    { "OZ_RSI" },
    { "OZ_RDI" },
    { "OZ_R8"  },
    { "OZ_R9"  },
    { "OZ_R10" },
    { "OZ_R11" },
    { "OZ_R12" },
    { "OZ_R13" },
    { "OZ_R14" },
    { "OZ_R15" },

    { "CR0" },
    { "CR4" },
    { "XCR0" },

    { "FCW" },
    { "FSW" },
    { "FTW" },
    { "FOP" },
    { "FPUIP" },
    { "FPUCS" },
    { "FPUDP" },
    { "FPUDS" },
    { "MXCSR" },
    { "ST0" },
    { "ST1" },
    { "ST2" },
    { "ST3" },
    { "ST4" },
    { "ST5" },
    { "ST6" },
    { "ST7" },
    { "MM0" },
    { "MM1" },
    { "MM2" },
    { "MM3" },
    { "MM4" },
    { "MM5" },
    { "MM6" },
    { "MM7" },
    { "MM0_LO_ZX" },
    { "MM1_LO_ZX" },
    { "MM2_LO_ZX" },
    { "MM3_LO_ZX" },
    { "MM4_LO_ZX" },
    { "MM5_LO_ZX" },
    { "MM6_LO_ZX" },
    { "MM7_LO_ZX" },
    { "XMM0" },
    { "XMM1" },
    { "XMM2" },
    { "XMM3" },
    { "XMM4" },
    { "XMM5" },
    { "XMM6" },
    { "XMM7" },
    { "XMM8" },
    { "XMM9" },
    { "XMM10" },
    { "XMM11" },
    { "XMM12" },
    { "XMM13" },
    { "XMM14" },
    { "XMM15" },
    { "XMM0_LO" },
    { "XMM1_LO" },
    { "XMM2_LO" },
    { "XMM3_LO" },
    { "XMM4_LO" },
    { "XMM5_LO" },
    { "XMM6_LO" },
    { "XMM7_LO" },
    { "XMM8_LO" },
    { "XMM9_LO" },
    { "XMM10_LO" },
    { "XMM11_LO" },
    { "XMM12_LO" },
    { "XMM13_LO" },
    { "XMM14_LO" },
    { "XMM15_LO" },
    { "XMM0_HI" },
    { "XMM1_HI" },
    { "XMM2_HI" },
    { "XMM3_HI" },
    { "XMM4_HI" },
    { "XMM5_HI" },
    { "XMM6_HI" },
    { "XMM7_HI" },
    { "XMM8_HI" },
    { "XMM9_HI" },
    { "XMM10_HI" },
    { "XMM11_HI" },
    { "XMM12_HI" },
    { "XMM13_HI" },
    { "XMM14_HI" },
    { "XMM15_HI" },
    { "XMM0_LO_ZX" },
    { "XMM1_LO_ZX" },
    { "XMM2_LO_ZX" },
    { "XMM3_LO_ZX" },
    { "XMM4_LO_ZX" },
    { "XMM5_LO_ZX" },
    { "XMM6_LO_ZX" },
    { "XMM7_LO_ZX" },
    { "XMM8_LO_ZX" },
    { "XMM9_LO_ZX" },
    { "XMM10_LO_ZX" },
    { "XMM11_LO_ZX" },
    { "XMM12_LO_ZX" },
    { "XMM13_LO_ZX" },
    { "XMM14_LO_ZX" },
    { "XMM15_LO_ZX" },
    { "XMM0_DW0" },
    { "XMM1_DW0" },
    { "XMM2_DW0" },
    { "XMM3_DW0" },
    { "XMM4_DW0" },
    { "XMM5_DW0" },
    { "XMM6_DW0" },
    { "XMM7_DW0" },
    { "XMM8_DW0" },
    { "XMM9_DW0" },
    { "XMM10_DW0" },
    { "XMM11_DW0" },
    { "XMM12_DW0" },
    { "XMM13_DW0" },
    { "XMM14_DW0" },
    { "XMM15_DW0" },
    { "XMM0_DW0_ZX" },
    { "XMM1_DW0_ZX" },
    { "XMM2_DW0_ZX" },
    { "XMM3_DW0_ZX" },
    { "XMM4_DW0_ZX" },
    { "XMM5_DW0_ZX" },
    { "XMM6_DW0_ZX" },
    { "XMM7_DW0_ZX" },
    { "XMM8_DW0_ZX" },
    { "XMM9_DW0_ZX" },
    { "XMM10_DW0_ZX" },
    { "XMM11_DW0_ZX" },
    { "XMM12_DW0_ZX" },
    { "XMM13_DW0_ZX" },
    { "XMM14_DW0_ZX" },
    { "XMM15_DW0_ZX" },
    { "XMM0_HI96" },
    { "XMM1_HI96" },
    { "XMM2_HI96" },
    { "XMM3_HI96" },
    { "XMM4_HI96" },
    { "XMM5_HI96" },
    { "XMM6_HI96" },
    { "XMM7_HI96" },
    { "XMM8_HI96" },
    { "XMM9_HI96" },
    { "XMM10_HI96" },
    { "XMM11_HI96" },
    { "XMM12_HI96" },
    { "XMM13_HI96" },
    { "XMM14_HI96" },
    { "XMM15_HI96" },
    { "YMM0" },
    { "YMM1" },
    { "YMM2" },
    { "YMM3" },
    { "YMM4" },
    { "YMM5" },
    { "YMM6" },
    { "YMM7" },
    { "YMM8" },
    { "YMM9" },
    { "YMM10" },
    { "YMM11" },
    { "YMM12" },
    { "YMM13" },
    { "YMM14" },
    { "YMM15" },

    { "VALXCPT" },
};
AssertCompile(RT_ELEMENTS(g_aszBs3Cg1DstFields) >= BS3CG1DST_END);
AssertCompile(RT_ELEMENTS(g_aszBs3Cg1DstFields) == BS3CG1DST_END);


#if 0
static const struct
{
    uint8_t     cbPrefixes;
    uint8_t     abPrefixes[14];
    uint16_t    fEffective;
} g_aPrefixVariations[] =
{
    { 0, { 0x00 }, BS3CG1_PF_NONE },

    { 1, { P_OZ }, BS3CG1_PF_OZ },
    { 1, { P_CS }, BS3CG1_PF_CS },
    { 1, { P_DS }, BS3CG1_PF_DS },
    { 1, { P_ES }, BS3CG1_PF_ES },
    { 1, { P_FS }, BS3CG1_PF_FS },
    { 1, { P_GS }, BS3CG1_PF_GS },
    { 1, { P_SS }, BS3CG1_PF_SS },
    { 1, { P_LK }, BS3CG1_PF_LK },

    { 2, { P_CS, P_OZ, }, BS3CG1_PF_CS | BS3CFG1_PF_OZ },
    { 2, { P_DS, P_OZ, }, BS3CG1_PF_DS | BS3CFG1_PF_OZ },
    { 2, { P_ES, P_OZ, }, BS3CG1_PF_ES | BS3CFG1_PF_OZ },
    { 2, { P_FS, P_OZ, }, BS3CG1_PF_FS | BS3CFG1_PF_OZ },
    { 2, { P_GS, P_OZ, }, BS3CG1_PF_GS | BS3CFG1_PF_OZ },
    { 2, { P_GS, P_OZ, }, BS3CG1_PF_SS | BS3CFG1_PF_OZ },
    { 2, { P_SS, P_OZ, }, BS3CG1_PF_SS | BS3CFG1_PF_OZ },

    { 2, { P_OZ, P_CS, }, BS3CG1_PF_CS | BS3CFG1_PF_OZ },
    { 2, { P_OZ, P_DS, }, BS3CG1_PF_DS | BS3CFG1_PF_OZ },
    { 2, { P_OZ, P_ES, }, BS3CG1_PF_ES | BS3CFG1_PF_OZ },
    { 2, { P_OZ, P_FS, }, BS3CG1_PF_FS | BS3CFG1_PF_OZ },
    { 2, { P_OZ, P_GS, }, BS3CG1_PF_GS | BS3CFG1_PF_OZ },
    { 2, { P_OZ, P_GS, }, BS3CG1_PF_SS | BS3CFG1_PF_OZ },
    { 2, { P_OZ, P_SS, }, BS3CG1_PF_SS | BS3CFG1_PF_OZ },
};

static const uint16_t g_afPfxKindToIgnoredFlags[BS3CG1PFXKIND_END] =
{
    /* [BS3CG1PFXKIND_INVALID] = */              UINT16_MAX,
    /* [BS3CG1PFXKIND_MODRM] = */                0,
    /* [BS3CG1PFXKIND_MODRM_NO_OP_SIZES] = */    BS3CG1_PF_OZ | BS3CG1_PF_W,
};

#endif


/**
 * Checks if >= 16 byte SSE alignment are exempted for the exception type.
 *
 * @returns true / false.
 * @param   enmXcptType         The type to check.
 */
static bool BS3_NEAR_CODE Bs3Cg1XcptTypeIsUnaligned(BS3CG1XCPTTYPE enmXcptType)
{
    switch (enmXcptType)
    {
        case BS3CG1XCPTTYPE_1:
        case BS3CG1XCPTTYPE_2:
        case BS3CG1XCPTTYPE_4:
            return false;
        case BS3CG1XCPTTYPE_NONE:
        case BS3CG1XCPTTYPE_3:
        case BS3CG1XCPTTYPE_4UA:
        case BS3CG1XCPTTYPE_5:
            return true;
        default:
            return false;
    }
}


/**
 * Checks if >= 16 byte AVX alignment are exempted for the exception type.
 *
 * @returns true / false.
 * @param   enmXcptType         The type to check.
 */
static bool BS3_NEAR_CODE Bs3Cg1XcptTypeIsVexUnaligned(BS3CG1XCPTTYPE enmXcptType)
{
    switch (enmXcptType)
    {
        case BS3CG1XCPTTYPE_1:
            return false;

        case BS3CG1XCPTTYPE_NONE:
        case BS3CG1XCPTTYPE_2:
        case BS3CG1XCPTTYPE_3:
        case BS3CG1XCPTTYPE_4:
        case BS3CG1XCPTTYPE_4UA:
        case BS3CG1XCPTTYPE_5:
        case BS3CG1XCPTTYPE_6:
        case BS3CG1XCPTTYPE_11:
        case BS3CG1XCPTTYPE_12:
            return true;

        default:
            return false;
    }
}


DECLINLINE(unsigned) BS3_NEAR_CODE Bs3Cg1InsertReqPrefix(PBS3CG1STATE pThis, unsigned offDst)
{
    switch (pThis->enmPrefixKind)
    {
        case BS3CG1PFXKIND_REQ_66:
            pThis->abCurInstr[offDst] = 0x66;
            break;
        case BS3CG1PFXKIND_REQ_F2:
            pThis->abCurInstr[offDst] = 0xf2;
            break;
        case BS3CG1PFXKIND_REQ_F3:
            pThis->abCurInstr[offDst] = 0xf3;
            break;
        default:
            return offDst;
    }
    return offDst + 1;
}


DECLINLINE(unsigned) BS3_NEAR_CODE Bs3Cg1InsertOpcodes(PBS3CG1STATE pThis, unsigned offDst)
{
    switch (pThis->cbOpcodes)
    {
        case 4: pThis->abCurInstr[offDst + 3] = pThis->abOpcodes[3];
        case 3: pThis->abCurInstr[offDst + 2] = pThis->abOpcodes[2];
        case 2: pThis->abCurInstr[offDst + 1] = pThis->abOpcodes[1];
        case 1: pThis->abCurInstr[offDst]     = pThis->abOpcodes[0];
            return offDst + pThis->cbOpcodes;

        default:
            BS3_ASSERT(0);
            return 0;
    }
}


/**
 * Inserts a ModR/M byte with mod=3 and set the two idxFields members.
 *
 * @returns off + 1.
 * @param   pThis       The state.
 * @param   off         Current instruction offset.
 * @param   uReg        Register index for ModR/M.reg.
 * @param   uRegMem     Register index for ModR/M.rm.
 */
static unsigned Bs3Cg1InsertModRmWithRegFields(PBS3CG1STATE pThis, unsigned off, uint8_t uReg, uint8_t uRegMem)
{
    pThis->abCurInstr[off++] = X86_MODRM_MAKE(3, uReg & 7, uRegMem & 7);
    pThis->aOperands[pThis->iRegOp].idxField = pThis->aOperands[pThis->iRegOp].idxFieldBase + uReg;
    pThis->aOperands[pThis->iRmOp ].idxField = pThis->aOperands[pThis->iRmOp ].idxFieldBase + uRegMem;
    return off;
}



/**
 * Cleans up state and context changes made by the encoder.
 *
 * @param   pThis       The state.
 */
static void BS3_NEAR_CODE Bs3Cg1EncodeCleanup(PBS3CG1STATE pThis)
{
    /* Restore the DS registers in the contexts. */
    unsigned iRing = 4;
    while (iRing-- > 0)
        pThis->aInitialCtxs[iRing].ds = pThis->aSavedSegRegs[iRing].ds;

    switch (pThis->enmEncoding)
    {
        /* Most encodings currently doesn't need any special cleaning up. */
        default:
            return;
    }
}


static unsigned BS3_NEAR_CODE Bs3Cfg1EncodeMemMod0Disp(PBS3CG1STATE pThis, bool fAddrOverride, unsigned off, uint8_t iReg,
                                                       uint8_t cbOp, uint8_t cbMisalign, BS3CG1OPLOC enmLocation)
{
    pThis->aOperands[pThis->iRmOp].idxField     = BS3CG1DST_INVALID;
    pThis->aOperands[pThis->iRmOp].enmLocation  = enmLocation;
    pThis->aOperands[pThis->iRmOp].cbOp         = cbOp;
    pThis->aOperands[pThis->iRmOp].off          = cbOp + cbMisalign;

    if (   BS3_MODE_IS_16BIT_CODE(pThis->bMode)
        || (fAddrOverride && BS3_MODE_IS_32BIT_CODE(pThis->bMode)) )
    {
        /*
         * 16-bit code doing 16-bit or 32-bit addressing,
         * or 32-bit code doing 16-bit addressing.
         */
        unsigned iRing = 4;
        if (BS3_MODE_IS_RM_OR_V86(pThis->bMode))
            while (iRing-- > 0)
                pThis->aInitialCtxs[iRing].ds = pThis->DataPgFar.sel;
        else
            while (iRing-- > 0)
                pThis->aInitialCtxs[iRing].ds = pThis->DataPgFar.sel | iRing;
        if (!fAddrOverride || BS3_MODE_IS_32BIT_CODE(pThis->bMode))
        {
            pThis->abCurInstr[off++] = X86_MODRM_MAKE(0, iReg, 6 /*disp16*/);
            *(uint16_t *)&pThis->abCurInstr[off] = pThis->DataPgFar.off + X86_PAGE_SIZE - cbOp - cbMisalign;
            off += 2;
        }
        else
        {
            pThis->abCurInstr[off++] = X86_MODRM_MAKE(0, iReg, 5 /*disp32*/);
            *(uint32_t *)&pThis->abCurInstr[off] = pThis->DataPgFar.off + X86_PAGE_SIZE - cbOp - cbMisalign;
            off += 4;
        }
    }
    else
    {
        /*
         * 32-bit code doing 32-bit addressing,
         * or 64-bit code doing either 64-bit or 32-bit addressing.
         */
        pThis->abCurInstr[off++] = X86_MODRM_MAKE(0, iReg, 5 /*disp32*/);
        *(uint32_t *)&pThis->abCurInstr[off] = BS3_FP_OFF(pThis->pbDataPg) + X86_PAGE_SIZE - cbOp - cbMisalign;

#if ARCH_BITS == 64
        /* In 64-bit mode we always have a rip relative encoding regardless of fAddrOverride. */
        if (BS3CG1_IS_64BIT_TARGET(pThis))
            *(uint32_t *)&pThis->abCurInstr[off] -= BS3_FP_OFF(&pThis->pbCodePg[X86_PAGE_SIZE]);
#endif
        off += 4;
    }

    /*
     * Fill the memory with 0xcc.
     */
    switch (cbOp + cbMisalign)
    {
        case 8: pThis->pbDataPg[X86_PAGE_SIZE - 8] = 0xcc;  RT_FALL_THRU();
        case 7: pThis->pbDataPg[X86_PAGE_SIZE - 7] = 0xcc;  RT_FALL_THRU();
        case 6: pThis->pbDataPg[X86_PAGE_SIZE - 6] = 0xcc;  RT_FALL_THRU();
        case 5: pThis->pbDataPg[X86_PAGE_SIZE - 5] = 0xcc;  RT_FALL_THRU();
        case 4: pThis->pbDataPg[X86_PAGE_SIZE - 4] = 0xcc;  RT_FALL_THRU();
        case 3: pThis->pbDataPg[X86_PAGE_SIZE - 3] = 0xcc;  RT_FALL_THRU();
        case 2: pThis->pbDataPg[X86_PAGE_SIZE - 2] = 0xcc;  RT_FALL_THRU();
        case 1: pThis->pbDataPg[X86_PAGE_SIZE - 1] = 0xcc;  RT_FALL_THRU();
        case 0: break;
        default:
        {
            BS3CG1_DPRINTF(("Bs3MemSet(%p,%#x,%#x)\n", &pThis->pbDataPg[X86_PAGE_SIZE - cbOp - cbMisalign], 0xcc, cbOp - cbMisalign));
            Bs3MemSet(&pThis->pbDataPg[X86_PAGE_SIZE - cbOp - cbMisalign], 0xcc, cbOp - cbMisalign);
            break;
        }
    }

    return off;
}


#if 0 /* unused */
/** Also encodes idxField of the register operand using idxFieldBase.   */
static unsigned BS3_NEAR_CODE
Bs3Cfg1EncodeMemMod0DispWithRegField(PBS3CG1STATE pThis, bool fAddrOverride, unsigned off, uint8_t iReg,
                                     uint8_t cbOp, uint8_t cbMisalign, BS3CG1OPLOC enmLocation)
{
    pThis->aOperands[pThis->iRegOp].idxField = pThis->aOperands[pThis->iRegOp].idxFieldBase + iReg;
    return Bs3Cfg1EncodeMemMod0Disp(pThis, fAddrOverride, off, iReg & 7, cbOp, cbMisalign, enmLocation);
}
#endif

/** Also encodes idxField of the register operand using idxFieldBase. */
static unsigned BS3_NEAR_CODE
Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(PBS3CG1STATE pThis, unsigned off, uint8_t iReg)
{
    pThis->aOperands[pThis->iRegOp].idxField = pThis->aOperands[pThis->iRegOp].idxFieldBase + iReg;
    return Bs3Cfg1EncodeMemMod0Disp(pThis, false /*fAddrOverride*/, off, iReg & 7,
                                    pThis->aOperands[pThis->iRmOp].cbOp,
                                    0 /*cbMisalign*/,
                                    pThis->aOperands[pThis->iRmOp].enmLocation);
}

/** Also encodes idxField of the register operand using idxFieldBase. */
static unsigned BS3_NEAR_CODE
Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsAddrOverride(PBS3CG1STATE pThis, unsigned off, uint8_t iReg)
{
    pThis->aOperands[pThis->iRegOp].idxField = pThis->aOperands[pThis->iRegOp].idxFieldBase + iReg;
    return Bs3Cfg1EncodeMemMod0Disp(pThis, true /*fAddrOverride*/, off, iReg & 7,
                                    pThis->aOperands[pThis->iRmOp].cbOp,
                                    0 /*cbMisalign*/,
                                    pThis->aOperands[pThis->iRmOp].enmLocation);
}


/** Also encodes idxField of the register operand using idxFieldBase. */
static unsigned BS3_NEAR_CODE
Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(PBS3CG1STATE pThis, unsigned off, uint8_t iReg, uint8_t cbMisalign)
{
    pThis->aOperands[pThis->iRegOp].idxField = pThis->aOperands[pThis->iRegOp].idxFieldBase + iReg;
    return Bs3Cfg1EncodeMemMod0Disp(pThis, false /*fAddrOverride*/, off, iReg & 7,
                                    pThis->aOperands[pThis->iRmOp].cbOp,
                                    cbMisalign,
                                    pThis->aOperands[pThis->iRmOp].enmLocation);
}


/** Also encodes idxField of the register operand using idxFieldBase. */
static unsigned BS3_NEAR_CODE
Bs3Cfg1EncodeMemMod0DispWithRegFieldAndSizeAndDefaults(PBS3CG1STATE pThis, unsigned off, uint8_t iReg, uint8_t cbOp)
{
    pThis->aOperands[pThis->iRegOp].idxField = pThis->aOperands[pThis->iRegOp].idxFieldBase + iReg;
    return Bs3Cfg1EncodeMemMod0Disp(pThis, false /*fAddrOverride*/, off, iReg & 7, cbOp, 0 /*cbMisalign*/,
                                    pThis->aOperands[pThis->iRmOp].enmLocation);
}

/** Also encodes idxField of the register operand using idxFieldBase. */
static unsigned BS3_NEAR_CODE
Bs3Cfg1EncodeMemMod0DispWithRegFieldAndSizeAndDefaultsAddrOverride(PBS3CG1STATE pThis, unsigned off, uint8_t iReg, uint8_t cbOp)
{
    pThis->aOperands[pThis->iRegOp].idxField = pThis->aOperands[pThis->iRegOp].idxFieldBase + iReg;
    return Bs3Cfg1EncodeMemMod0Disp(pThis, true /*fAddrOverride*/, off, iReg & 7, cbOp, 0 /*cbMisalign*/,
                                    pThis->aOperands[pThis->iRmOp].enmLocation);
}


/** The modrm.reg value is taken from the instruction byte at @a off.  */
static unsigned BS3_NEAR_CODE
Bs3Cfg1EncodeMemMod0DispWithDefaultsAndNoReg(PBS3CG1STATE pThis, unsigned off)
{
    return Bs3Cfg1EncodeMemMod0Disp(pThis, false /*fAddrOverride*/, off,
                                    (pThis->abCurInstr[off] & X86_MODRM_REG_MASK) >> X86_MODRM_REG_SHIFT,
                                    pThis->aOperands[pThis->iRmOp].cbOp,
                                    0 /*cbMisalign*/,
                                    pThis->aOperands[pThis->iRmOp].enmLocation);
}



static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_Eb_Gb_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        /* Start by reg,reg encoding. */
        case 0:
            pThis->aOperands[pThis->iRmOp].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, X86_GREG_xAX, X86_GREG_xCX);
            break;
        case 1:
            pThis->aOperands[pThis->iRmOp].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5 /*CH*/);
            break;
        case 2:
            if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) < BS3CPU_80386)
                return 0;
            pThis->aOperands[pThis->iRmOp].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
            pThis->abCurInstr[0] = P_OZ;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 6 /*DH*/);
            break;
        /* Tests with address overrides go last! */
        case 3:
            pThis->abCurInstr[0] = P_AZ;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 1));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsAddrOverride(pThis, off, 7 /*BH*/);
            break;

        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_Gv_Ev__OR__MODRM_Ev_Gv(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    unsigned cbOp;
    switch (iEncoding)
    {
        case 0:
            pThis->aOperands[pThis->iRmOp].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, X86_GREG_xBX, X86_GREG_xDX);
            cbOp = pThis->cbOpDefault;
            break;
        case 1:
            pThis->aOperands[pThis->iRmOp].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            cbOp = pThis->cbOpDefault;
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndSizeAndDefaults(pThis, off, X86_GREG_xBP, cbOp);
            break;
        case 2:
            if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) < BS3CPU_80386)
                return 0;
            pThis->aOperands[pThis->iRmOp].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
            pThis->abCurInstr[0] = P_OZ;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 1));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, X86_GREG_xAX, X86_GREG_xCX);
            cbOp = pThis->cbOpOvrd66;
            break;
        case 3:
            pThis->aOperands[pThis->iRmOp].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
            pThis->abCurInstr[0] = P_OZ;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 1));
            cbOp = pThis->cbOpOvrd66;
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndSizeAndDefaults(pThis, off, X86_GREG_xSI, cbOp);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 2 : 0;
            break;
        case 4:
            pThis->aOperands[pThis->iRmOp].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
            off = Bs3Cg1InsertReqPrefix(pThis, 0);
            pThis->abCurInstr[off++] = REX_W___;
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, X86_GREG_xBX, X86_GREG_xDX);
            cbOp = pThis->cbOpOvrdRexW;
            break;
        case 5:
            off = Bs3Cg1InsertReqPrefix(pThis, 0);
            pThis->abCurInstr[off++] = REX__RB_;
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, X86_GREG_x14, X86_GREG_x12);
            cbOp = pThis->cbOpDefault;
            break;
        /* Tests with address overrides go last!*/
        case 6:
            pThis->aOperands[pThis->iRmOp].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
            pThis->abCurInstr[0] = P_AZ;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 1));
            cbOp = pThis->cbOpDefault;
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndSizeAndDefaultsAddrOverride(pThis, off, X86_GREG_xDI, cbOp);
            break;
        case 7:
            pThis->abCurInstr[0] = P_OZ;
            pThis->abCurInstr[1] = P_AZ;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 2));
            cbOp = pThis->cbOpOvrd66;
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndSizeAndDefaultsAddrOverride(pThis, off, X86_GREG_xDI, cbOp);
            break;
        default:
            return 0;
    }
    pThis->aOperands[0].cbOp = cbOp;
    pThis->aOperands[1].cbOp = cbOp;
    pThis->cbOperand  = cbOp;
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_Pq_WO_Qq(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 1, 0);
            break;
        case 1:
            pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 4, 7);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
#if ARCH_BITS == 64
        case 2:
            off = Bs3Cg1InsertReqPrefix(pThis, 0);
            pThis->abCurInstr[off++] = REX__RBX;
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6 /*no +8*/, 2 /*no +8*/);
            break;
#endif
        case 3:
            pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 4 /*iReg*/);
            break;
        case 4:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(pThis, off, 7 /*iReg*/, 1 /*cbMisalign*/);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
#if ARCH_BITS == 64
        case 5:
            off = Bs3Cg1InsertReqPrefix(pThis, 0);
            pThis->abCurInstr[off++] = REX__RBX;
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 7 /*iReg - no +8*/);
            break;
#endif

        default:
            return 0;
    }

    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_Pq_WO_Uq(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 1, 0);
            break;
        case 1:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6, 2);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
        case 2:
            off = Bs3Cg1InsertReqPrefix(pThis, 0);
            pThis->abCurInstr[off++] = REX__RBX;
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6 /*no+8*/, 2 + 8);
            break;
        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_PdZx_WO_Ed_WZ(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 1, 0);
            break;
        case 1:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6, 2);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
#if ARCH_BITS == 64
        case 2:
            off = Bs3Cg1InsertReqPrefix(pThis, 0);
            pThis->abCurInstr[off++] = REX__RBX;
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6 /*no +8*/, 2+8);
            break;
#endif
        case 3:
            pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 4 /*iReg*/);
            break;
        case 4:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(pThis, off, 7 /*iReg*/, 1 /*cbMisalign*/);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
#if ARCH_BITS == 64
        case 5:
            off = Bs3Cg1InsertReqPrefix(pThis, 0);
            pThis->abCurInstr[off++] = REX__RBX;
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 7 /*iReg*/);
            break;
#endif

        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_Pq_WO_Eq_WNZ(PBS3CG1STATE pThis, unsigned iEncoding)
{
#if ARCH_BITS == 64
    if (BS3CG1_IS_64BIT_TARGET(pThis))
    {
        unsigned off;
        switch (iEncoding)
        {
            case 0:
                pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
                off = Bs3Cg1InsertReqPrefix(pThis, 0);
                pThis->abCurInstr[off++] = REX_W___;
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 1, 0);
                break;
            case 1:
                off = Bs3Cg1InsertReqPrefix(pThis, 0);
                pThis->abCurInstr[off++] = REX_W___;
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6, 2);
                break;
            case 2:
                off = Bs3Cg1InsertReqPrefix(pThis, 0);
                pThis->abCurInstr[off++] = REX_WRBX;
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6 /*no +8*/, 2+8);
                break;
            case 3:
                pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
                off = Bs3Cg1InsertReqPrefix(pThis, 0);
                pThis->abCurInstr[off++] = REX_W___;
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 4 /*iReg*/);
                break;
            case 4:
                off = Bs3Cg1InsertReqPrefix(pThis, 0);
                pThis->abCurInstr[off++] = REX_W___;
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(pThis, off, 7 /*iReg*/, 1 /*cbMisalign*/);
                break;
            case 5:
                off = Bs3Cg1InsertReqPrefix(pThis, 0);
                pThis->abCurInstr[off++] = REX_WRBX;
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 7 /*iReg*/);
                break;

            default:
                return 0;
        }
        pThis->cbCurInstr = off;
        return iEncoding + 1;
    }
#endif
    return 0;
}


/* Differs from Bs3Cg1EncodeNext_MODRM_PdZx_WO_Ed_WZ in that REX.R isn't ignored. */
static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_Vd_WO_Ed_WZ(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 1, 0);
            break;
        case 1:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6, 2);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
#if ARCH_BITS == 64
        case 2:
            off = Bs3Cg1InsertReqPrefix(pThis, 0);
            pThis->abCurInstr[off++] = REX__RBX;
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6+8, 2+8);
            break;
#endif
        case 3:
            pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 4 /*iReg*/);
            break;
        case 4:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(pThis, off, 7 /*iReg*/, 1 /*cbMisalign*/);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
#if ARCH_BITS == 64
        case 5:
            off = Bs3Cg1InsertReqPrefix(pThis, 0);
            pThis->abCurInstr[off++] = REX__RBX;
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 7+8 /*iReg*/);
            break;
#endif

        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


/* Differs from Bs3Cg1EncodeNext_MODRM_Pq_WO_Eq_WNZ in that REX.R isn't ignored. */
static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_Vq_WO_Eq_WNZ(PBS3CG1STATE pThis, unsigned iEncoding)
{
#if ARCH_BITS == 64
    if (BS3CG1_IS_64BIT_TARGET(pThis))
    {
        unsigned off;
        switch (iEncoding)
        {
            case 0:
                pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
                off = Bs3Cg1InsertReqPrefix(pThis, 0);
                pThis->abCurInstr[off++] = REX_W___;
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 1, 0);
                break;
            case 1:
                off = Bs3Cg1InsertReqPrefix(pThis, 0);
                pThis->abCurInstr[off++] = REX_W___;
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6, 2);
                break;
            case 2:
                off = Bs3Cg1InsertReqPrefix(pThis, 0);
                pThis->abCurInstr[off++] = REX_WRBX;
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6+8, 2+8);
                break;
            case 4:
                pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
                off = Bs3Cg1InsertReqPrefix(pThis, 0);
                pThis->abCurInstr[off++] = REX_W___;
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 4 /*iReg*/);
                break;
            case 5:
                off = Bs3Cg1InsertReqPrefix(pThis, 0);
                pThis->abCurInstr[off++] = REX_W___;
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(pThis, off, 7 /*iReg*/, 1 /*cbMisalign*/);
                break;
            case 6:
                off = Bs3Cg1InsertReqPrefix(pThis, 0);
                pThis->abCurInstr[off++] = REX_WRBX;
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 7+8 /*iReg*/);
                break;

            default:
                return 0;
        }
        pThis->cbCurInstr = off;
        return iEncoding + 1;
    }
#endif
    return 0;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_Vsomething_Usomething_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 1, 0);
            break;
        case 1:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 2, 2);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
        case 2:
            off = Bs3Cg1InsertReqPrefix(pThis, 0);
            pThis->abCurInstr[off++] = REX__RBX;
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 3+8, 7+8);
            break;
        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_Vsomething_Wsomething_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            pThis->aOperands[pThis->iRmOp].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
            off = Bs3Cg1InsertModRmWithRegFields(pThis, Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0)), 1, 0);
            break;
        case 1:
            pThis->aOperands[pThis->iRmOp].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 2 /*iReg*/);
            break;
        case 2:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(pThis, off, 3 /*iReg*/, 1 /*cbMisalign*/);
            if (!Bs3Cg1XcptTypeIsUnaligned(pThis->enmXcptType))
                pThis->bAlignmentXcpt = X86_XCPT_GP;
            break;
        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_Vsomething_Nsomething(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 1, 0);
            break;
        case 1:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6, 7);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
        case 2:
            off = Bs3Cg1InsertReqPrefix(pThis, 0);
            pThis->abCurInstr[off++] = REX_WRBX;
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6 + 8, 7 /*no +8*/);
            break;

        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_Gv_RO_Ma(PBS3CG1STATE pThis, unsigned iEncoding) /* bound instr */
{
    unsigned off;
    unsigned cbOp = BS3_MODE_IS_16BIT_CODE(pThis->bMode) ? 2 : 4;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndSizeAndDefaults(pThis, off, X86_GREG_xBP, cbOp * 2);
            break;
        case 1:
            if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) < BS3CPU_80386)
                return 0;
            cbOp = cbOp == 2 ? 4 : 2;
            pThis->abCurInstr[0] = P_OZ;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 1));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndSizeAndDefaults(pThis, off, X86_GREG_xBP, cbOp * 2);
            break;
        case 2:
            pThis->abCurInstr[0] = P_AZ;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 1));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndSizeAndDefaultsAddrOverride(pThis, off, X86_GREG_xBP, cbOp * 2);
            break;
        case 3:
            cbOp = cbOp == 2 ? 4 : 2;
            pThis->abCurInstr[0] = P_AZ;
            pThis->abCurInstr[1] = P_OZ;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 2));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndSizeAndDefaultsAddrOverride(pThis, off, X86_GREG_xBP, cbOp * 2);
            break;
        default:
            return 0;
    }
    pThis->aOperands[pThis->iRegOp].cbOp = cbOp;
    pThis->cbOperand  = cbOp;
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_Msomething(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0)) - 1;
            off = Bs3Cfg1EncodeMemMod0DispWithDefaultsAndNoReg(pThis, off);
            break;
        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_Msomething_Psomething(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 4 /*iReg*/);
            break;
        case 1:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(pThis, off, 7 /*iReg*/, 1 /*cbMisalign*/);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
#if ARCH_BITS == 64
        case 2:
            off = Bs3Cg1InsertReqPrefix(pThis, 0);
            pThis->abCurInstr[off++] = REX__RBX;
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 7 /*iReg - no +8*/);
            break;
#endif

        default:
            return 0;
    }

    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_Msomething_Vsomething_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 2 /*iReg*/);
            break;
        case 1:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(pThis, off, 2 /*iReg*/, 1 /*cbMisalign*/ );
            if (!Bs3Cg1XcptTypeIsUnaligned(pThis->enmXcptType))
                pThis->bAlignmentXcpt = X86_XCPT_GP;
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
        case 2:
            off = Bs3Cg1InsertReqPrefix(pThis, 0);
            pThis->abCurInstr[off++] = REX__R__;
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 2+8 /*iReg*/);
            break;
        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_FIXED(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            pThis->cbCurInstr = off;
            break;
        default:
            return 0;
    }
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_FIXED_AL_Ib(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            pThis->aOperands[1].off  = (uint8_t)off;
            pThis->abCurInstr[off++] = 0xff;
            pThis->cbCurInstr        = off;
            break;
        default:
            return 0;
    }
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_FIXED_rAX_Iz(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    unsigned cbOp;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 0));
            pThis->aOperands[1].off = (uint8_t)off;
            cbOp = pThis->cbOpDefault;
            if (cbOp == 2)
                *(uint16_t *)&pThis->abCurInstr[off] = UINT16_MAX;
            else
                *(uint32_t *)&pThis->abCurInstr[off] = UINT32_MAX;
            off += cbOp;
            pThis->aOperands[0].cbOp = cbOp;
            pThis->aOperands[1].cbOp = cbOp;
            pThis->cbOperand         = cbOp;
            break;
        case 1:
            if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) < BS3CPU_80386)
                return 0;
            pThis->abCurInstr[0] = P_OZ;
            off = Bs3Cg1InsertOpcodes(pThis, Bs3Cg1InsertReqPrefix(pThis, 1));
            pThis->aOperands[1].off = (uint8_t)off;
            cbOp = pThis->cbOpOvrd66;
            if (cbOp == 2)
                *(uint16_t *)&pThis->abCurInstr[off] = UINT16_MAX;
            else
                *(uint32_t *)&pThis->abCurInstr[off] = UINT32_MAX;
            off += cbOp;
            pThis->aOperands[0].cbOp = cbOp;
            pThis->aOperands[1].cbOp = cbOp;
            pThis->cbOperand         = cbOp;
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
        case 2:
            off = Bs3Cg1InsertReqPrefix(pThis, 0);
            pThis->abCurInstr[off++] = REX_W___;
            off = Bs3Cg1InsertOpcodes(pThis, off);
            pThis->aOperands[1].off = (uint8_t)off;
            *(uint32_t *)&pThis->abCurInstr[off] = UINT32_MAX;
            off += 4;
            pThis->aOperands[0].cbOp = 8;
            pThis->aOperands[1].cbOp = 4;
            pThis->cbOperand         = 8;
            break;
        default:
            return 0;

            /* IMAGE PADDING - workaround for "rd err" - remove later! */
        case 4:
            ASMHalt();
            ASMHalt();
            ASMHalt();
            return 0;

    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_MOD_EQ_3(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    if (iEncoding < 8)
    {
        off = Bs3Cg1InsertReqPrefix(pThis, 0);
        off = Bs3Cg1InsertOpcodes(pThis, off);
        pThis->abCurInstr[off++] = X86_MODRM_MAKE(3, iEncoding, 1);
    }
    else if (iEncoding < 16)
    {
        off = Bs3Cg1InsertReqPrefix(pThis, 0);
        off = Bs3Cg1InsertOpcodes(pThis, off);
        pThis->abCurInstr[off++] = X86_MODRM_MAKE(3, 0, iEncoding & 7);
    }
    else
        return 0;
    pThis->cbCurInstr = off;

    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_MODRM_MOD_NE_3(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    if (iEncoding < 3)
    {
        off = Bs3Cg1InsertReqPrefix(pThis, 0);
        off = Bs3Cg1InsertOpcodes(pThis, off);
        pThis->abCurInstr[off++] = X86_MODRM_MAKE(iEncoding, 0, 1);
        if (iEncoding >= 1)
            pThis->abCurInstr[off++] = 0x7f;
        if (iEncoding == 2)
        {
            pThis->abCurInstr[off++] = 0x5f;
            if (!BS3_MODE_IS_16BIT_CODE(pThis->bMode))
            {
                pThis->abCurInstr[off++] = 0x3f;
                pThis->abCurInstr[off++] = 0x1f;
            }
        }
    }
    else
        return 0;
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


/*
 *
 * VEX
 * VEX
 * VEX
 *
 */
#ifdef BS3CG1_WITH_VEX

/**
 * Inserts a 3-byte VEX prefix.
 *
 * @returns New offDst value.
 * @param   pThis       The state.
 * @param   offDst      The current instruction offset.
 * @param   uVexL       The VEX.L value.
 * @param   uVexV       The VEX.V value (caller inverted it already).
 * @param   uVexR       The VEX.R value (caller inverted it already).
 * @param   uVexX       The VEX.X value (caller inverted it already).
 * @param   uVexB       The VEX.B value (caller inverted it already).
 * @param   uVexW       The VEX.W value (straight).
 */
DECLINLINE(unsigned) BS3_NEAR_CODE Bs3Cg1InsertVex3bPrefix(PBS3CG1STATE pThis, unsigned offDst, uint8_t uVexV, uint8_t uVexL,
                                                           uint8_t uVexR, uint8_t uVexX, uint8_t uVexB, uint8_t uVexW)
{
    uint8_t b1;
    uint8_t b2;
    b1        = uVexR << 7;
    b1       |= uVexX << 6;
    b1       |= uVexB << 5;
    b1       |= pThis->uOpcodeMap;
    b2        = uVexV << 3;
    b2       |= uVexW << 7;
    b2       |= uVexL << 2;
    switch (pThis->enmPrefixKind)
    {
        case BS3CG1PFXKIND_NO_F2_F3_66:     b2 |= 0; break;
        case BS3CG1PFXKIND_REQ_66:          b2 |= 1; break;
        case BS3CG1PFXKIND_REQ_F3:          b2 |= 2; break;
        case BS3CG1PFXKIND_REQ_F2:          b2 |= 3; break;
        default:
            Bs3TestFailedF("enmPrefixKind=%d not supported for VEX!\n", pThis->enmPrefixKind);
            break;
    }

    pThis->abCurInstr[offDst]     = 0xc4; /* vex3 */
    pThis->abCurInstr[offDst + 1] = b1;
    pThis->abCurInstr[offDst + 2] = b2;
    pThis->uVexL                  = uVexL;
    return offDst + 3;
}


/**
 * Inserts a 2-byte VEX prefix.
 *
 * @note    Will switch to 3-byte VEX prefix if uOpcodeMap isn't one.
 *
 * @returns New offDst value.
 * @param   pThis       The state.
 * @param   offDst      The current instruction offset.
 * @param   uVexL       The VEX.L value.
 * @param   uVexV       The VEX.V value (caller inverted it already).
 * @param   uVexR       The VEX.R value (caller inverted it already).
 */
DECLINLINE(unsigned) BS3_NEAR_CODE Bs3Cg1InsertVex2bPrefix(PBS3CG1STATE pThis, unsigned offDst,
                                                           uint8_t uVexV, uint8_t uVexL, uint8_t uVexR)
{
    if (pThis->uOpcodeMap == 1)
    {
        uint8_t b = uVexR << 7;
        b        |= uVexV << 3;
        b        |= uVexL << 2;
        switch (pThis->enmPrefixKind)
        {
            case BS3CG1PFXKIND_NO_F2_F3_66:     b |= 0; break;
            case BS3CG1PFXKIND_REQ_66:          b |= 1; break;
            case BS3CG1PFXKIND_REQ_F3:          b |= 2; break;
            case BS3CG1PFXKIND_REQ_F2:          b |= 3; break;
            default:
                Bs3TestFailedF("enmPrefixKind=%d not supported for VEX!\n");
                break;
        }

        pThis->abCurInstr[offDst]     = 0xc5; /* vex2 */
        pThis->abCurInstr[offDst + 1] = b;
        pThis->uVexL                  = uVexL;
        return offDst + 2;
    }
    return Bs3Cg1InsertVex3bPrefix(pThis, offDst, uVexV, uVexL, uVexR, 1 /*uVexX*/, 1 /*uVexB*/, 0/*uVexW*/);
}


/**
 * Inserts a ModR/M byte with mod=3 and set the two idxFields members.
 *
 * @returns off + 1.
 * @param   pThis       The state.
 * @param   off         Current instruction offset.
 * @param   uReg        Register index for ModR/M.reg.
 * @param   uRegMem     Register index for ModR/M.rm.
 * @param   uVexVvvv    The VEX.vvvv register.
 */
static unsigned Bs3Cg1InsertModRmWithRegFieldsAndVvvv(PBS3CG1STATE pThis, unsigned off,
                                                      uint8_t uReg, uint8_t uRegMem, uint8_t uVexVvvv)
{
    pThis->abCurInstr[off++] = X86_MODRM_MAKE(3, uReg & 7, uRegMem & 7);
    pThis->aOperands[pThis->iRegOp].idxField = pThis->aOperands[pThis->iRegOp].idxFieldBase + uReg;
    pThis->aOperands[1            ].idxField = pThis->aOperands[1            ].idxFieldBase + uVexVvvv;
    pThis->aOperands[pThis->iRmOp ].idxField = pThis->aOperands[pThis->iRmOp ].idxFieldBase + uRegMem;
    return off;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_VEX_MODRM_Vd_WO_Ed_WZ(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 1, 0);
            break;
        case 1:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6, 2);
            break;
        case 2:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L-invalid*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6, 2);
            pThis->fInvalidEncoding = true;
            break;
        case 3:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xe /*~V-invalid*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6, 2);
            pThis->fInvalidEncoding = true;
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
#if ARCH_BITS == 64
        case 4:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 0 /*~R*/, 1 /*~X*/, 0 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6+8, 2+8);
            break;
#endif
        case 5:
            pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 4 /*iReg*/);
            break;
        case 6:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 4 /*iReg*/);
            break;
        case 7:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(pThis, off, 4 /*iReg*/, 1 /*cbMisalign*/);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 2 : 0;
            break;
#if ARCH_BITS == 64
        case 8:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 0 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 4+8 /*iReg*/);
            break;
        case 9:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 0 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5+8 /*iReg*/);
            iEncoding += 2;
            break;
#endif
        case 10: /* VEX.W is ignored in 32-bit mode. flag? */
            BS3_ASSERT(!BS3CG1_IS_64BIT_TARGET(pThis));
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 4 /*iReg*/);
            break;

        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


/* Differs from Bs3Cg1EncodeNext_MODRM_Pq_WO_Eq_WNZ in that REX.R isn't ignored. */
static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_VEX_MODRM_Vq_WO_Eq_WNZ(PBS3CG1STATE pThis, unsigned iEncoding)
{
#if ARCH_BITS == 64
    if (BS3CG1_IS_64BIT_TARGET(pThis))
    {
        unsigned off;
        switch (iEncoding)
        {
            case 0:
                pThis->aOperands[pThis->iRmOp].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
                off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W*/);
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6, 2);
                break;
            case 1:
                off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L-invalid*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W*/);
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6, 2);
                pThis->fInvalidEncoding = true;
                break;
            case 2:
                off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xe /*~V-invalid*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W*/);
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6, 2);
                pThis->fInvalidEncoding = true;
                break;
            case 3:
                off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 0 /*~R*/, 1 /*~X*/, 0 /*~B*/, 1 /*W*/);
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 6+8, 2+8);
                break;
            case 4:
                pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
                off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W*/);
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 4 /*iReg*/);
                break;
            case 5:
                off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W*/);
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(pThis, off, 4 /*iReg*/, 1 /*cbMisalign*/);
                iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 2 : 0;
                break;
            case 6:
                off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 0 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W*/);
                off = Bs3Cg1InsertOpcodes(pThis, off);
                off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 4+8 /*iReg*/);
                break;

            default:
                return 0;
        }
        pThis->cbCurInstr = off;
        return iEncoding + 1;
    }
#endif
    return 0;
}


/**
 * Wip - VEX.W ignored.
 * Lig - VEX.L ignored.
 */
static unsigned BS3_NEAR_CODE
Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Hsomething_Usomething_Lip_Wip_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 2, 1, 0);
            break;
        case 1:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0x8 /*~V*/, 1 /*L-ignored*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 3, 1, 7);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
        case 2:
#if ARCH_BITS == 64
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0 /*~V*/, 0 /*L*/, 0 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 3+8, 2, 15);
            break;
#endif
        case 3:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 2, 1, 0);
            break;
        case 4:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L - ignored*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 2, 1, 0);
            break;
        case 5:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xc /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W-ignored*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 2, 1, 3);
            break;
        case 6:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0 /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W-ignored*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 2, 1, BS3CG1_IS_64BIT_TARGET(pThis) ? 15 : 7);
            break;
        case 7:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0 /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 2, 1, BS3CG1_IS_64BIT_TARGET(pThis) ? 15 : 7);
            break;
        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


/**
 * Wip - VEX.W ignored.
 */
static unsigned BS3_NEAR_CODE
Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_HdqCsomething_Usomething_Wip_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 2, 1, 0);
            break;
        case 1:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0x8 /*~V*/, 1 /*L-ignored*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 3, 1, 7);
            pThis->fInvalidEncoding = true;
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
        case 2:
#if ARCH_BITS == 64
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0 /*~V*/, 0 /*L*/, 0 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 3+8, 2, 15);
            break;
#endif
        case 3:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 2, 1, 0);
            break;
        case 4:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L - ignored*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 2, 1, 0);
            pThis->fInvalidEncoding = true;
            break;
        case 5:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xc /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W-ignored*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 2, 1, 3);
            break;
        case 6:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0 /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W-ignored*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 2, 1, BS3CG1_IS_64BIT_TARGET(pThis) ? 15 : 7);
            break;
        case 7:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0 /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFieldsAndVvvv(pThis, off, 2, 1, BS3CG1_IS_64BIT_TARGET(pThis) ? 15 : 7);
            break;
        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


/**
 * Wip - VEX.W ignored.
 */
static unsigned BS3_NEAR_CODE
Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 20: /* Switch to 256-bit operands. */
            pThis->aOperands[pThis->iRegOp].idxFieldBase = BS3CG1DST_YMM0;
            pThis->aOperands[pThis->iRegOp].cbOp = 32;
            pThis->aOperands[pThis->iRmOp ].cbOp = 32;
            RT_FALL_THRU();
        case 0:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 0);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
#if ARCH_BITS == 64
        case 1:
        case 21:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 0 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 7 + 8);
            break;
#endif
        case 2:
        case 22:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xe /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 0);
            pThis->fInvalidEncoding = true;
            break;
        case 3:
        case 23:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 1);
            break;
        case 4:
        case 24:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W-ignored*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 3 : 0;
            break;
#if ARCH_BITS == 64
        case 5:
        case 25:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 0 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5+8);
            break;
        case 6:
        case 26:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 0 /*~B-ignored*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 1);
            break;
        case 7:
        case 27:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 0 /*~X-ignored*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 2);
            break;
#endif
        case 8:
        case 28:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0 /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5);
            pThis->fInvalidEncoding = true;
            break;
        case 9:
        case 29:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 7 /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 2);
            pThis->fInvalidEncoding = true;
            iEncoding += 10;
            break;

        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}



/**
 * Wip - VEX.W ignored.
 * Lig - VEX.L ignored.
 */
static unsigned BS3_NEAR_CODE
Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_Lig_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 0);
            break;
        case 1:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L - ignored*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 7);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
#if ARCH_BITS == 64
        case 2:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L - ignored*/, 0 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 7 + 8);
            break;
#endif
        case 3:
            iEncoding = 3;
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xe /*~V*/, 0 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 0);
            pThis->fInvalidEncoding = true;
            break;
        case 4:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 1);
            break;
        case 5:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L-ignored*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 1);
            break;
        case 6:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W-ignored*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 3 : 0;
            break;
#if ARCH_BITS == 64
        case 7:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 0 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5+8);
            break;
        case 8:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 0 /*~B-ignored*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 1);
            break;
        case 9:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 0 /*~X-ignored*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 2);
            break;
#endif
        case 10:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0 /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5);
            pThis->fInvalidEncoding = true;
            break;
        case 11:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 7 /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 2);
            pThis->fInvalidEncoding = true;
            break;
        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


/**
 * Wip - VEX.W ignored.
 * L0 - VEX.L must be zero.
 */
static unsigned BS3_NEAR_CODE
Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_Lmbz_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 0);
            break;
        case 1:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L - invalid*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 7);
            pThis->fInvalidEncoding = true;
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 2 : 0;
            break;
#if ARCH_BITS == 64
        case 2:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 0 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 7 + 8);
            break;
        case 3:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L - invalid*/, 0 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5 + 8);
            pThis->fInvalidEncoding = true;
            break;
#endif
        case 4:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xe /*~V*/, 0 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 0);
            pThis->fInvalidEncoding = true;
            break;
        case 5:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 1);
            break;
        case 6:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L - invalid*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 1);
            pThis->fInvalidEncoding = true;
            break;
        case 7:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W-ignored*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 3 : 0;
            break;
#if ARCH_BITS == 64
        case 8:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 0 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5+8);
            break;
        case 9:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 0 /*~B-ignored*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 1);
            break;
        case 10:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 0 /*~X-ignored*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 2);
            break;
#endif
        case 11:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0 /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5);
            pThis->fInvalidEncoding = true;
            break;
        case 12:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 7 /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 2);
            pThis->fInvalidEncoding = true;
            break;
        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


/**
 * Wip - VEX.W ignored.
 */
static unsigned BS3_NEAR_CODE
Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_Lxx_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding, uint8_t uVexL)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, uVexL, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 0);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
#if ARCH_BITS == 64
        case 1:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, uVexL, 0 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 7 + 8);
            break;
#endif
        case 2:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xe /*~V*/, uVexL, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 0);
            pThis->fInvalidEncoding = true;
            break;
        case 3:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, uVexL, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 1);
            break;
        case 4:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, uVexL, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W-ignored*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 3 : 0;
            break;
#if ARCH_BITS == 64
        case 5:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, uVexL, 0 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5+8);
            break;
        case 6:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, uVexL, 1 /*~R*/, 1 /*~X*/, 0 /*~B-ignored*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 1);
            break;
        case 7:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, uVexL, 1 /*~R*/, 0 /*~X-ignored*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 2);
            break;
#endif
        case 8:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0 /*~V*/, uVexL, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5);
            pThis->fInvalidEncoding = true;
            break;
        case 9:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 7 /*~V*/, uVexL, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 2);
            pThis->fInvalidEncoding = true;
            break;
        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


/**
 * Wip - VEX.W ignored.
 * L0 - VEX.L is zero (encoding may exist where it isn't).
 */
static unsigned BS3_NEAR_CODE
Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_L0_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding)
{
    return Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_Lxx_OR_ViceVersa(pThis, iEncoding, 0 /*uVexL*/);
}


/**
 * Wip - VEX.W ignored.
 * L1 - VEX.L is one (encoding may exist where it isn't).
 */
static unsigned BS3_NEAR_CODE
Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_L1_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding)
{
    return Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_Lxx_OR_ViceVersa(pThis, iEncoding, 1 /*uVexL*/);
}



/**
 * Wip - VEX.W ignored.
 */
static unsigned BS3_NEAR_CODE
Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Hsomething_Msomething_Wip_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xc /*~V*/, 0 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 0);
            pThis->aOperands[1].idxField = pThis->aOperands[1].idxFieldBase + 3;
            break;
        case 1:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 7);
            pThis->aOperands[1].idxField = pThis->aOperands[1].idxFieldBase + 0;
            pThis->fInvalidEncoding = true;
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
#if ARCH_BITS == 64
        case 2:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0x1 /*~V*/, 0 /*L*/, 0 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 7 + 8);
            pThis->aOperands[1].idxField = pThis->aOperands[1].idxFieldBase + 14;
            break;
#endif
        case 3:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xe /*~V*/, 0 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 0);
            pThis->aOperands[1].idxField = pThis->aOperands[1].idxFieldBase + 1;
            break;
        case 4:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 1);
            pThis->aOperands[1].idxField = pThis->aOperands[1].idxFieldBase + 0;
            break;
        case 5:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L-ignored*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 1);
            pThis->aOperands[1].idxField = pThis->aOperands[1].idxFieldBase + 0;
            pThis->fInvalidEncoding = true;
            break;
        case 6:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W-ignored*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5);
            pThis->aOperands[1].idxField = pThis->aOperands[1].idxFieldBase + 0;
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 3 : 0;
            break;
#if ARCH_BITS == 64
        case 7:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 0 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5+8);
            pThis->aOperands[1].idxField = pThis->aOperands[1].idxFieldBase + 0;
            break;
        case 8:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 0 /*~B-ignored*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 1);
            pThis->aOperands[1].idxField = pThis->aOperands[1].idxFieldBase + 0;
            break;
        case 9:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 0 /*~X-ignored*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 2);
            pThis->aOperands[1].idxField = pThis->aOperands[1].idxFieldBase + 0;
            break;
#endif
        case 10:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0 /*~V*/, 1 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 5);
            pThis->aOperands[1].idxField = pThis->aOperands[1].idxFieldBase + (BS3CG1_IS_64BIT_TARGET(pThis) ? 15 : 7);
            pThis->fInvalidEncoding = true;
            break;
        default:
            return 0;
    }
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_VEX_MODRM_Md_WO(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        case 0:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off) - 1;
            off = Bs3Cfg1EncodeMemMod0DispWithDefaultsAndNoReg(pThis, off);
            break;
        case 1:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off) - 1;
            off = Bs3Cfg1EncodeMemMod0DispWithDefaultsAndNoReg(pThis, off);
            break;
        case 2:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0x7 /*~V-invalid*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off) - 1;
            off = Bs3Cfg1EncodeMemMod0DispWithDefaultsAndNoReg(pThis, off);
            pThis->fInvalidEncoding = true;
            break;
        case 3:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off) - 1;
            off = Bs3Cfg1EncodeMemMod0DispWithDefaultsAndNoReg(pThis, off);
            pThis->fInvalidEncoding = true;
            break;
        case 4:
            pThis->abCurInstr[0] = P_OZ;
            off = Bs3Cg1InsertVex3bPrefix(pThis, 1 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off) - 1;
            off = Bs3Cfg1EncodeMemMod0DispWithDefaultsAndNoReg(pThis, off);
            pThis->fInvalidEncoding = true;
            break;
        case 5:
            pThis->abCurInstr[0] = P_RZ;
            off = Bs3Cg1InsertVex3bPrefix(pThis, 1 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off) - 1;
            off = Bs3Cfg1EncodeMemMod0DispWithDefaultsAndNoReg(pThis, off);
            pThis->fInvalidEncoding = true;
            break;
        case 6:
            pThis->abCurInstr[0] = P_RN;
            off = Bs3Cg1InsertVex3bPrefix(pThis, 1 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off) - 1;
            off = Bs3Cfg1EncodeMemMod0DispWithDefaultsAndNoReg(pThis, off);
            pThis->fInvalidEncoding = true;
            break;
        case 7:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off) - 1;
            off = Bs3Cfg1EncodeMemMod0DispWithDefaultsAndNoReg(pThis, off);
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 1 : 0;
            break;
#if ARCH_BITS == 64
        case 8:
            pThis->abCurInstr[0] = REX_____;
            off = Bs3Cg1InsertVex3bPrefix(pThis, 1 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off) - 1;
            off = Bs3Cfg1EncodeMemMod0DispWithDefaultsAndNoReg(pThis, off);
            pThis->fInvalidEncoding = true;
            break;
#endif
        default:
            return 0;
    }

    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


/**
 * Wip = VEX.W ignored.
 * Lmbz = VEX.L must be zero.
 */
static unsigned BS3_NEAR_CODE
Bs3Cg1EncodeNext_VEX_MODRM_WsomethingWO_Vsomething_Wip_Lmbz_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    switch (iEncoding)
    {
        /* 128-bit wide stuff goes first, then we'll update the operand widths afterwards. */
        case 0:
            pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 1, 0);
            break;

        case 1:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 4, 5);
            break;
        case 2:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W - ignored*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 5, 4);
            break;
        case 3:
            pThis->aOperands[pThis->iRmOp].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 2 /*iReg*/);
            break;
        case 4:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 3 /*iReg*/);
            break;
        case 5:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W - ignored */);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 3 /*iReg*/);
            break;
        case 6:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(pThis, off, 3 /*iReg*/, 1 /*cbMisalign*/);
            if (!Bs3Cg1XcptTypeIsVexUnaligned(pThis->enmXcptType))
                pThis->bAlignmentXcpt = X86_XCPT_GP;
            break;
        case 7:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(pThis, off, 3 /*iReg*/, 1 /*cbMisalign*/);
            if (!Bs3Cg1XcptTypeIsVexUnaligned(pThis->enmXcptType))
                pThis->bAlignmentXcpt = X86_XCPT_GP;
            break;
        /* 128-bit invalid encodings: */
        case 8:
            pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xe /*~V*/, 0 /*L*/, 1 /*~R*/); /* Bad V value */
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 1, 0);
            pThis->fInvalidEncoding = true;
            break;
        case 9:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0 /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 4, 5);
            pThis->fInvalidEncoding = true;
            iEncoding = 20-1;
            break;

        default:
            return 0;
    }

    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


/**
 * Wip = VEX.W ignored.
 */
static unsigned BS3_NEAR_CODE
Bs3Cg1EncodeNext_VEX_MODRM_WsomethingWO_Vsomething_Wip_OR_ViceVersa(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;

    switch (iEncoding)
    {
        case 20: /* switch to 256-bit */
            pThis->aOperands[pThis->iRmOp ].cbOp         = 32;
            pThis->aOperands[pThis->iRmOp ].idxFieldBase = BS3CG1DST_YMM0;
            pThis->aOperands[pThis->iRegOp].cbOp         = 32;
            pThis->aOperands[pThis->iRegOp].idxFieldBase = BS3CG1DST_YMM0;
            RT_FALL_THRU();
        case 0:
            pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 1, 0);
            break;

        case 1:
        case 21:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 4, 5);
            break;
        case 2:
        case 22:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W - ignored*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 5, 4);
            break;
        case 3:
        case 23:
            pThis->aOperands[pThis->iRmOp].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationMem;
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 2 /*iReg*/);
            break;
        case 4:
        case 24:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 3 /*iReg*/);
            break;
        case 5:
        case 25:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 1 /*W - ignored */);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaults(pThis, off, 3 /*iReg*/);
            break;
        case 6:
        case 26:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(pThis, off, 3 /*iReg*/, 1 /*cbMisalign*/);
            if (!Bs3Cg1XcptTypeIsVexUnaligned(pThis->enmXcptType))
                pThis->bAlignmentXcpt = X86_XCPT_GP;
            break;
        case 7:
        case 27:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cfg1EncodeMemMod0DispWithRegFieldAndDefaultsMisaligned(pThis, off, 3 /*iReg*/, 1 /*cbMisalign*/);
            if (!Bs3Cg1XcptTypeIsVexUnaligned(pThis->enmXcptType))
                pThis->bAlignmentXcpt = X86_XCPT_GP;
            break;
        /* invalid encodings: */
        case 8:
        case 28:
            pThis->aOperands[pThis->iRmOp ].enmLocation = pThis->aOperands[pThis->iRmOp].enmLocationReg;
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xe /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/); /* Bad V value */
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 1, 0);
            pThis->fInvalidEncoding = true;
            break;
        case 9:
        case 29:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0 /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 4, 5);
            pThis->fInvalidEncoding = true;
            break;

        case 10:
        case 30:
            pThis->abCurInstr[0] = P_RN;
            off = Bs3Cg1InsertVex3bPrefix(pThis, 1 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 4, 5);
            pThis->fInvalidEncoding = true;
            break;
        case 11:
        case 31:
            pThis->abCurInstr[0] = P_RZ;
            off = Bs3Cg1InsertVex3bPrefix(pThis, 1 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 4, 5);
            pThis->fInvalidEncoding = true;
            break;
        case 12:
        case 32:
            pThis->abCurInstr[0] = P_OZ;
            off = Bs3Cg1InsertVex3bPrefix(pThis, 1 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 4, 5);
            pThis->fInvalidEncoding = true;
            break;
        case 13:
        case 33:
            pThis->abCurInstr[0] = P_LK;
            off = Bs3Cg1InsertVex3bPrefix(pThis, 1 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 4, 5);
            pThis->fInvalidEncoding = true;
            iEncoding += !BS3CG1_IS_64BIT_TARGET(pThis) ? 2 + 4 : 0;
            break;

#if ARCH_BITS == 64
        /* 64-bit mode registers */
        case 14:
        case 34:
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 0 /*~R*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 3+8, 4);
            break;
        case 15:
        case 35:
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, iEncoding >= 20 /*L*/, 0 /*~R*/, 1 /*~X*/, 0 /*~B*/, 0 /*W*/);
            off = Bs3Cg1InsertOpcodes(pThis, off);
            off = Bs3Cg1InsertModRmWithRegFields(pThis, off, 1+8, 4+8);
            iEncoding += 4;
            break;
#endif
        default:
            return 0;
    }

    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


//static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_VEX_FIXED(PBS3CG1STATE pThis, unsigned iEncoding)
//{
//    unsigned off;
//    if (iEncoding == 0)
//        off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/);
//    else if (iEncoding == 0)
//        off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
//    else
//        return 0;
//    pThis->cbCurInstr = off;
//    return iEncoding + 1;
//}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_VEX_MODRM_MOD_EQ_3(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    if (iEncoding < 8)
    {
        if (iEncoding & 1)
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/);
        else
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
        off = Bs3Cg1InsertOpcodes(pThis, off);
        pThis->abCurInstr[off++] = X86_MODRM_MAKE(3, iEncoding, 1);
    }
    else if (iEncoding < 16)
    {
        if (iEncoding & 1)
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L*/, 1 /*~R*/);
        else
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 1 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
        off = Bs3Cg1InsertOpcodes(pThis, off);
        pThis->abCurInstr[off++] = X86_MODRM_MAKE(3, iEncoding & 7, 1);
    }
    else if (iEncoding < 24)
    {
        if (iEncoding & 1)
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/);
        else
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, 0 /*L*/, 1 /*~R*/, 1 /*~X*/, 1 /*~B*/, 0 /*W*/);
        off = Bs3Cg1InsertOpcodes(pThis, off);
        pThis->abCurInstr[off++] = X86_MODRM_MAKE(3, 0, iEncoding & 7);
    }
    else if (iEncoding < 32)
    {
        if (iEncoding & 1)
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, (iEncoding & 3) != 0 /*L*/, 1 /*~R*/);
        else
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, (iEncoding & 2) != 0 /*L*/, 1 /*~R*/, 1 /*~X*/,
                                          1 /*~B*/, (iEncoding & 4) != 0 /*W*/);
        off = Bs3Cg1InsertOpcodes(pThis, off);
        pThis->abCurInstr[off++] = X86_MODRM_MAKE(3, 0, iEncoding & 7);
    }
    else
        return 0;
    pThis->cbCurInstr = off;

    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_VEX_MODRM_MOD_NE_3(PBS3CG1STATE pThis, unsigned iEncoding)
{
    unsigned off;
    if (iEncoding < 8)
    {
        unsigned iMod = iEncoding % 3;
        if (iEncoding & 1)
            off = Bs3Cg1InsertVex2bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, (iEncoding & 2) != 0 /*L*/, 1 /*~R*/);
        else
            off = Bs3Cg1InsertVex3bPrefix(pThis, 0 /*offDst*/, 0xf /*~V*/, (iEncoding & 2) != 0 /*L*/, 1 /*~R*/,
                                          1 /*~X*/, 1 /*~B*/, (iEncoding & 4) != 0 /*W*/);
        off = Bs3Cg1InsertOpcodes(pThis, off);
        pThis->abCurInstr[off++] = X86_MODRM_MAKE(iMod, 0, 1);
        if (iMod >= 1)
            pThis->abCurInstr[off++] = 0x7f;
        if (iMod == 2)
        {
            pThis->abCurInstr[off++] = 0x5f;
            if (!BS3_MODE_IS_16BIT_CODE(pThis->bMode))
            {
                pThis->abCurInstr[off++] = 0x3f;
                pThis->abCurInstr[off++] = 0x1f;
            }
        }
    }
    else
        return 0;
    pThis->cbCurInstr = off;
    return iEncoding + 1;
}


static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext_VEX_MODRM(PBS3CG1STATE pThis, unsigned iEncoding)
{
    const unsigned cFirstEncodings = 32;
    if (iEncoding < cFirstEncodings)
    {
        unsigned iRet = Bs3Cg1EncodeNext_VEX_MODRM_MOD_EQ_3(pThis, iEncoding);
        BS3_ASSERT(iRet > iEncoding);
        return iRet;
    }
    return Bs3Cg1EncodeNext_VEX_MODRM_MOD_NE_3(pThis, iEncoding - cFirstEncodings) + cFirstEncodings;
}

#endif /* BS3CG1_WITH_VEX */


/**
 * Encodes the next instruction.
 *
 * @returns Next iEncoding value.  Returns @a iEncoding unchanged to indicate
 *          that there are no more encodings to test.
 * @param   pThis           The state.
 * @param   iEncoding       The encoding to produce.  Meaning is specific to
 *                          each BS3CG1ENC_XXX value and should be considered
 *                          internal.
 */
static unsigned BS3_NEAR_CODE Bs3Cg1EncodeNext(PBS3CG1STATE pThis, unsigned iEncoding)
{
    pThis->bAlignmentXcpt = UINT8_MAX;
    pThis->uVexL          = UINT8_MAX;
    if (pThis->pfnEncoder)
        return pThis->pfnEncoder(pThis, iEncoding);

    Bs3TestFailedF("Internal error! BS3CG1ENC_XXX = %u not implemented", pThis->enmEncoding);
    return iEncoding;
}


/**
 * Prepares doing instruction encodings.
 *
 * This is in part specific to how the instruction is encoded, but generally it
 * sets up basic operand values that doesn't change (much) when Bs3Cg1EncodeNext
 * is called from within the loop.
 *
 * @returns Success indicator (true/false).
 * @param   pThis           The state.
 */
#define Bs3Cg1EncodePrep BS3_CMN_NM(Bs3Cg1EncodePrep)
bool BS3_NEAR_CODE Bs3Cg1EncodePrep(PBS3CG1STATE pThis)
{
    unsigned i = 4;
    while (i-- > 0)
        pThis->aSavedSegRegs[i].ds = pThis->aInitialCtxs[i].ds;

    i = RT_ELEMENTS(pThis->aOperands);
    while (i-- > 0)
    {
        pThis->aOperands[i].enmLocationReg  = BS3CG1OPLOC_INVALID;
        pThis->aOperands[i].enmLocationMem  = BS3CG1OPLOC_INVALID;
        pThis->aOperands[i].idxFieldBase    = BS3CG1DST_INVALID;
    }

    pThis->iRmOp            = RT_ELEMENTS(pThis->aOperands) - 1;
    pThis->iRegOp           = RT_ELEMENTS(pThis->aOperands) - 1;
    pThis->fSameRingNotOkay = false;
    pThis->cbOperand        = 0;
    pThis->pfnEncoder       = NULL;

    switch (pThis->enmEncoding)
    {
        case BS3CG1ENC_MODRM_Eb_Gb:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Eb_Gb_OR_ViceVersa;
            pThis->iRmOp                        = 0;
            pThis->iRegOp                       = 1;
            pThis->aOperands[0].cbOp            = 1;
            pThis->aOperands[1].cbOp            = 1;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_AL;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_AL;
            pThis->aOperands[0].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_RW;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_MODRM_Ev_Gv:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Gv_Ev__OR__MODRM_Ev_Gv;
            pThis->iRmOp                        = 0;
            pThis->iRegOp                       = 1;
            pThis->cbOperand                    = 2;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_OZ_RAX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_OZ_RAX;
            pThis->aOperands[0].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_RW;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_MODRM_Ed_WO_Pd_WZ:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_PdZx_WO_Ed_WZ;
            pThis->iRmOp                        = 0;
            pThis->iRegOp                       = 1;
            pThis->aOperands[0].cbOp            = 4;
            pThis->aOperands[1].cbOp            = 4;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_EAX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_MM0;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_MODRM_Eq_WO_Pq_WNZ:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Pq_WO_Eq_WNZ;
            pThis->iRmOp                        = 0;
            pThis->iRegOp                       = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_RAX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_MM0;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_MODRM_Ed_WO_Vd_WZ:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Vd_WO_Ed_WZ;
            pThis->iRmOp                        = 0;
            pThis->iRegOp                       = 1;
            pThis->aOperands[0].cbOp            = 4;
            pThis->aOperands[1].cbOp            = 4;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_EAX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_MODRM_Eq_WO_Vq_WNZ:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Vq_WO_Eq_WNZ;
            pThis->iRmOp                        = 0;
            pThis->iRegOp                       = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_RAX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_MODRM_Gb_Eb:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Eb_Gb_OR_ViceVersa;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 1;
            pThis->aOperands[1].cbOp            = 1;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_AL;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_AL;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem  = BS3CG1OPLOC_MEM;
            break;

        case BS3CG1ENC_MODRM_Gv_Ev:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Gv_Ev__OR__MODRM_Ev_Gv;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->cbOperand                    = 2;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_OZ_RAX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_OZ_RAX;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem  = BS3CG1OPLOC_MEM;
            break;

        case BS3CG1ENC_MODRM_Gv_RO_Ma: /* bound instr */
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Gv_RO_Ma;
            pThis->iRmOp                        = 1;
            pThis->iRegOp                       = 0;
            pThis->cbOperand                    = 2;
            pThis->aOperands[0].cbOp            = 2;
            pThis->aOperands[1].cbOp            = 4;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_MEM;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_OZ_RAX;
            break;

        case BS3CG1ENC_MODRM_Wss_WO_Vss:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Vsomething_Wsomething_OR_ViceVersa;
            pThis->iRmOp                        = 0;
            pThis->iRegOp                       = 1;
            pThis->aOperands[0].cbOp            = 4;
            pThis->aOperands[1].cbOp            = 4;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0_DW0;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0_DW0;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[0].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_MODRM_Wsd_WO_Vsd:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Vsomething_Wsomething_OR_ViceVersa;
            pThis->iRmOp                        = 0;
            pThis->iRegOp                       = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0_LO;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0_LO;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[0].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_MODRM_WqZxReg_WO_Vq:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Vsomething_Wsomething_OR_ViceVersa;
            pThis->iRmOp                        = 0;
            pThis->iRegOp                       = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0_LO_ZX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0_LO;
            pThis->aOperands[0].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_MODRM_Wps_WO_Vps:
        case BS3CG1ENC_MODRM_Wpd_WO_Vpd:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Vsomething_Wsomething_OR_ViceVersa;
            pThis->iRmOp                        = 0;
            pThis->iRegOp                       = 1;
            pThis->aOperands[0].cbOp            = 16;
            pThis->aOperands[1].cbOp            = 16;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0;
            pThis->aOperands[0].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_MODRM_Vdq_WO_Mdq:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Msomething_Vsomething_OR_ViceVersa;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 16;
            pThis->aOperands[1].cbOp            = 16;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_MEM;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0;
            break;

        case BS3CG1ENC_MODRM_Vdq_WO_Wdq:
        case BS3CG1ENC_MODRM_Vpd_WO_Wpd:
        case BS3CG1ENC_MODRM_Vps_WO_Wps:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Vsomething_Wsomething_OR_ViceVersa;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 16;
            pThis->aOperands[1].cbOp            = 16;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem  = BS3CG1OPLOC_MEM;
            break;

        case BS3CG1ENC_MODRM_Pq_WO_Qq:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Pq_WO_Qq;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_MM0;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_MM0;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem  = BS3CG1OPLOC_MEM;
            break;

        case BS3CG1ENC_MODRM_Pq_WO_Uq:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Pq_WO_Uq;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_MM0;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0_LO;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX; /* reg only */
            break;

        case BS3CG1ENC_MODRM_PdZx_WO_Ed_WZ:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_PdZx_WO_Ed_WZ;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 4;
            pThis->aOperands[1].cbOp            = 4;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_MM0_LO_ZX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_EAX;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem  = BS3CG1OPLOC_MEM;
            break;

        case BS3CG1ENC_MODRM_Pq_WO_Eq_WNZ:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Pq_WO_Eq_WNZ;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_MM0;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_RAX;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem  = BS3CG1OPLOC_MEM;
            break;

        case BS3CG1ENC_MODRM_VdZx_WO_Ed_WZ:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Vd_WO_Ed_WZ;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 4;
            pThis->aOperands[1].cbOp            = 4;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0_DW0_ZX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_EAX;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem  = BS3CG1OPLOC_MEM;
            break;

        case BS3CG1ENC_MODRM_VqZx_WO_Eq_WNZ:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Vq_WO_Eq_WNZ;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0_LO_ZX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_RAX;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem  = BS3CG1OPLOC_MEM;
            break;

        case BS3CG1ENC_MODRM_Vq_WO_UqHi:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Vsomething_Usomething_OR_ViceVersa;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0_LO;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0_HI;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_MODRM_VqHi_WO_Uq:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Vsomething_Usomething_OR_ViceVersa;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0_HI;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0_LO;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_MODRM_VqHi_WO_Mq:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Msomething_Vsomething_OR_ViceVersa;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0_HI;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_MEM;
            break;

        case BS3CG1ENC_MODRM_Vq_WO_Mq:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Msomething_Vsomething_OR_ViceVersa;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0_LO;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_MEM;
            break;

        case BS3CG1ENC_MODRM_VssZx_WO_Wss:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Vsomething_Wsomething_OR_ViceVersa;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 4;
            pThis->aOperands[1].cbOp            = 4;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem  = BS3CG1OPLOC_MEM;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0_DW0_ZX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0_LO;
            break;

        case BS3CG1ENC_MODRM_VqZx_WO_Nq:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Vsomething_Nsomething;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0_LO_ZX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_MM0;
            break;

        case BS3CG1ENC_MODRM_VsdZx_WO_Wsd:
        case BS3CG1ENC_MODRM_VqZx_WO_Wq:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Vsomething_Wsomething_OR_ViceVersa;
            pThis->iRegOp                       = 0;
            pThis->iRmOp                        = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem  = BS3CG1OPLOC_MEM;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0_LO_ZX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0_LO;
            break;

        case BS3CG1ENC_MODRM_Mb_RO:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Msomething;
            pThis->iRmOp                        = 0;
            pThis->aOperands[0].cbOp            = 1;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_MEM;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM;
            break;

        case BS3CG1ENC_MODRM_Md_RO:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Msomething;
            pThis->iRmOp                        = 0;
            pThis->aOperands[0].cbOp            = 4;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_MEM;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM;
            break;

        case BS3CG1ENC_MODRM_Md_WO:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Msomething;
            pThis->iRmOp                        = 0;
            pThis->aOperands[0].cbOp            = 4;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_WO;
            break;

        case BS3CG1ENC_MODRM_Mdq_WO_Vdq:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Msomething_Vsomething_OR_ViceVersa;
            pThis->iRmOp                        = 0;
            pThis->iRegOp                       = 1;
            pThis->aOperands[0].cbOp            = 16;
            pThis->aOperands[1].cbOp            = 16;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0;
            break;

        case BS3CG1ENC_MODRM_Mq_WO_Pq:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Msomething_Psomething;
            pThis->iRmOp                        = 0;
            pThis->iRegOp                       = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_MM0;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_MODRM_Mq_WO_Vq:
        case BS3CG1ENC_MODRM_Mq_WO_VqHi:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Msomething_Vsomething_OR_ViceVersa;
            pThis->iRmOp                        = 0;
            pThis->iRegOp                       = 1;
            pThis->aOperands[0].cbOp            = 8;
            pThis->aOperands[1].cbOp            = 8;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].idxFieldBase    = pThis->enmEncoding == BS3CG1ENC_MODRM_Mq_WO_Vq
                                                ? BS3CG1DST_XMM0_LO : BS3CG1DST_XMM0_HI;
            break;

        case BS3CG1ENC_MODRM_Mps_WO_Vps:
        case BS3CG1ENC_MODRM_Mpd_WO_Vpd:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_Msomething_Vsomething_OR_ViceVersa;
            pThis->iRmOp                        = 0;
            pThis->iRegOp                       = 1;
            pThis->aOperands[0].cbOp            = 16;
            pThis->aOperands[1].cbOp            = 16;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0;
            break;

        case BS3CG1ENC_FIXED:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_FIXED;
            break;

        case BS3CG1ENC_FIXED_AL_Ib:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_FIXED_AL_Ib;
            pThis->aOperands[0].cbOp            = 1;
            pThis->aOperands[1].cbOp            = 1;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_IMM;
            pThis->aOperands[0].idxField        = BS3CG1DST_AL;
            pThis->aOperands[1].idxField        = BS3CG1DST_INVALID;
            break;

        case BS3CG1ENC_FIXED_rAX_Iz:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_FIXED_rAX_Iz;
            pThis->aOperands[0].cbOp            = 2;
            pThis->aOperands[1].cbOp            = 2;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_IMM;
            pThis->aOperands[0].idxField        = BS3CG1DST_OZ_RAX;
            pThis->aOperands[1].idxField        = BS3CG1DST_INVALID;
            break;

            /* Unused or invalid instructions mostly. */
        case BS3CG1ENC_MODRM_MOD_EQ_3:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_MOD_EQ_3;
            break;
        case BS3CG1ENC_MODRM_MOD_NE_3:
            pThis->pfnEncoder                   = Bs3Cg1EncodeNext_MODRM_MOD_NE_3;
            break;

#ifdef BS3CG1_WITH_VEX

        case BS3CG1ENC_VEX_MODRM_Vd_WO_Ed_WZ:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_Vd_WO_Ed_WZ;
            pThis->iRegOp            = 0;
            pThis->iRmOp             = 1;
            pThis->aOperands[0].cbOp = 4;
            pThis->aOperands[1].cbOp = 4;
            pThis->aOperands[0].idxFieldBase   = BS3CG1DST_XMM0_DW0_ZX;
            pThis->aOperands[1].idxFieldBase   = BS3CG1DST_EAX;
            pThis->aOperands[0].enmLocation    = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation    = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem = BS3CG1OPLOC_MEM;
            break;

        case BS3CG1ENC_VEX_MODRM_Vq_WO_Eq_WNZ:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_Vq_WO_Eq_WNZ;
            pThis->iRegOp            = 0;
            pThis->iRmOp             = 1;
            pThis->aOperands[0].cbOp = 8;
            pThis->aOperands[1].cbOp = 8;
            pThis->aOperands[0].idxFieldBase   = BS3CG1DST_XMM0_LO_ZX;
            pThis->aOperands[1].idxFieldBase   = BS3CG1DST_RAX;
            pThis->aOperands[0].enmLocation    = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation    = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem = BS3CG1OPLOC_MEM;
            break;

        case BS3CG1ENC_VEX_MODRM_Vps_WO_Wps:
        case BS3CG1ENC_VEX_MODRM_Vpd_WO_Wpd:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_WsomethingWO_Vsomething_Wip_OR_ViceVersa;
            pThis->iRegOp            = 0;
            pThis->iRmOp             = 1;
            pThis->aOperands[0].cbOp = 16;
            pThis->aOperands[1].cbOp = 16;
            pThis->aOperands[0].enmLocation    = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation    = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem = BS3CG1OPLOC_MEM;
            pThis->aOperands[0].idxFieldBase   = BS3CG1DST_XMM0;
            pThis->aOperands[1].idxFieldBase   = BS3CG1DST_XMM0;
            break;

        case BS3CG1ENC_VEX_MODRM_VssZx_WO_Md:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_Lig_OR_ViceVersa;
            pThis->iRmOp             = 1;
            pThis->iRegOp            = 0;
            pThis->aOperands[0].cbOp = 4;
            pThis->aOperands[1].cbOp = 4;
            pThis->aOperands[0].enmLocation  = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation  = BS3CG1OPLOC_MEM;
            pThis->aOperands[0].idxFieldBase = BS3CG1DST_XMM0_DW0;
            pThis->aOperands[1].idxFieldBase = BS3CG1DST_INVALID;
            break;

        case BS3CG1ENC_VEX_MODRM_Vss_WO_HssHi_Uss:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Hsomething_Usomething_Lip_Wip_OR_ViceVersa;
            pThis->iRegOp            = 0;
            pThis->iRmOp             = 2;
            pThis->aOperands[0].cbOp = 16;
            pThis->aOperands[1].cbOp = 12;
            pThis->aOperands[2].cbOp = 4;
            pThis->aOperands[0].enmLocation  = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[2].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].idxFieldBase = BS3CG1DST_XMM0;
            pThis->aOperands[1].idxFieldBase = BS3CG1DST_XMM0_HI96;
            pThis->aOperands[2].idxFieldBase = BS3CG1DST_XMM0_DW0;
            break;

        case BS3CG1ENC_VEX_MODRM_VsdZx_WO_Mq:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_Lig_OR_ViceVersa;
            pThis->iRmOp             = 1;
            pThis->iRegOp            = 0;
            pThis->aOperands[0].cbOp = 8;
            pThis->aOperands[1].cbOp = 8;
            pThis->aOperands[0].enmLocation  = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation  = BS3CG1OPLOC_MEM;
            pThis->aOperands[0].idxFieldBase = BS3CG1DST_XMM0_LO;
            pThis->aOperands[1].idxFieldBase = BS3CG1DST_INVALID;
            break;

        case BS3CG1ENC_VEX_MODRM_Vx_WO_Mx_L0:
            BS3_ASSERT(!(pThis->fFlags & BS3CG1INSTR_F_VEX_L_ZERO));
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_L0_OR_ViceVersa;
            pThis->iRegOp            = 0;
            pThis->iRmOp             = 1;
            pThis->aOperands[0].cbOp = 16;
            pThis->aOperands[1].cbOp = 16;
            pThis->aOperands[0].enmLocation  = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation  = BS3CG1OPLOC_MEM;
            pThis->aOperands[0].idxFieldBase = BS3CG1DST_XMM0;
            break;

        case BS3CG1ENC_VEX_MODRM_Vx_WO_Mx_L1:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_L1_OR_ViceVersa;
            pThis->iRegOp            = 0;
            pThis->iRmOp             = 1;
            pThis->aOperands[0].cbOp = 32;
            pThis->aOperands[1].cbOp = 32;
            pThis->aOperands[0].enmLocation  = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation  = BS3CG1OPLOC_MEM;
            pThis->aOperands[0].idxFieldBase = BS3CG1DST_YMM0;
            break;

        case BS3CG1ENC_VEX_MODRM_Vsd_WO_HsdHi_Usd:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Hsomething_Usomething_Lip_Wip_OR_ViceVersa;
            pThis->iRegOp            = 0;
            pThis->iRmOp             = 2;
            pThis->aOperands[0].cbOp = 16;
            pThis->aOperands[1].cbOp = 8;
            pThis->aOperands[2].cbOp = 8;
            pThis->aOperands[0].enmLocation  = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[2].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].idxFieldBase = BS3CG1DST_XMM0;
            pThis->aOperands[1].idxFieldBase = BS3CG1DST_XMM0_HI;
            pThis->aOperands[2].idxFieldBase = BS3CG1DST_XMM0_LO;
            break;

        case BS3CG1ENC_VEX_MODRM_Vq_WO_HqHi_UqHi:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_HdqCsomething_Usomething_Wip_OR_ViceVersa;
            pThis->iRegOp            = 0;
            pThis->iRmOp             = 2;
            pThis->aOperands[0].cbOp = 16;
            pThis->aOperands[1].cbOp = 8;
            pThis->aOperands[2].cbOp = 8;
            pThis->aOperands[0].enmLocation  = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[2].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].idxFieldBase = BS3CG1DST_XMM0;
            pThis->aOperands[1].idxFieldBase = BS3CG1DST_XMM0_HI;
            pThis->aOperands[2].idxFieldBase = BS3CG1DST_XMM0_HI;
            break;

        case BS3CG1ENC_VEX_MODRM_Vq_WO_HqHi_Mq:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Hsomething_Msomething_Wip_OR_ViceVersa;
            pThis->iRegOp            = 0;
            pThis->iRmOp             = 2;
            pThis->aOperands[0].cbOp = 16;
            pThis->aOperands[1].cbOp = 8;
            pThis->aOperands[2].cbOp = 8;
            pThis->aOperands[0].enmLocation  = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[2].enmLocation  = BS3CG1OPLOC_MEM;
            pThis->aOperands[0].idxFieldBase = BS3CG1DST_XMM0;
            pThis->aOperands[1].idxFieldBase = BS3CG1DST_XMM0_HI;
            pThis->aOperands[2].idxFieldBase = BS3CG1DST_INVALID;
            break;

        case BS3CG1ENC_VEX_MODRM_Vq_WO_Wq:
            BS3_ASSERT(pThis->fFlags & BS3CG1INSTR_F_VEX_L_ZERO);
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_WsomethingWO_Vsomething_Wip_Lmbz_OR_ViceVersa;
            pThis->iRegOp            = 0;
            pThis->iRmOp             = 1;
            pThis->aOperands[0].cbOp = 8;
            pThis->aOperands[1].cbOp = 8;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem  = BS3CG1OPLOC_MEM;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0_LO;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0_LO;
            break;

        case BS3CG1ENC_VEX_MODRM_Vx_WO_Wx:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_WsomethingWO_Vsomething_Wip_OR_ViceVersa;
            pThis->iRegOp            = 0;
            pThis->iRmOp             = 1;
            pThis->aOperands[0].cbOp = 16;
            pThis->aOperands[1].cbOp = 16;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationReg  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].enmLocationMem  = BS3CG1OPLOC_MEM;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0;
            break;

        case BS3CG1ENC_VEX_MODRM_Ed_WO_Vd_WZ:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_Vd_WO_Ed_WZ;
            pThis->iRmOp             = 0;
            pThis->iRegOp            = 1;
            pThis->aOperands[0].cbOp = 4;
            pThis->aOperands[1].cbOp = 4;
            pThis->aOperands[0].idxFieldBase   = BS3CG1DST_EAX;
            pThis->aOperands[1].idxFieldBase   = BS3CG1DST_XMM0_DW0_ZX;
            pThis->aOperands[0].enmLocation    = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationReg = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationMem = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation    = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_VEX_MODRM_Eq_WO_Vq_WNZ:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_Vq_WO_Eq_WNZ;
            pThis->iRmOp             = 0;
            pThis->iRegOp            = 1;
            pThis->aOperands[0].cbOp = 8;
            pThis->aOperands[1].cbOp = 8;
            pThis->aOperands[0].idxFieldBase   = BS3CG1DST_RAX;
            pThis->aOperands[1].idxFieldBase   = BS3CG1DST_XMM0_LO_ZX;
            pThis->aOperands[0].enmLocation    = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationReg = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].enmLocationMem = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation    = BS3CG1OPLOC_CTX;
            break;

        case BS3CG1ENC_VEX_MODRM_Md_WO:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_Md_WO;
            pThis->iRmOp             = 0;
            pThis->aOperands[0].cbOp = 4;
            pThis->aOperands[0].enmLocation    = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[0].enmLocationMem = BS3CG1OPLOC_MEM_WO;
            break;

        case BS3CG1ENC_VEX_MODRM_Md_WO_Vss:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_Lig_OR_ViceVersa;
            pThis->iRmOp             = 0;
            pThis->iRegOp            = 1;
            pThis->aOperands[0].cbOp = 4;
            pThis->aOperands[1].cbOp = 4;
            pThis->aOperands[0].enmLocation  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].idxFieldBase = BS3CG1DST_INVALID;
            pThis->aOperands[1].idxFieldBase = BS3CG1DST_XMM0_DW0;
            break;

        case BS3CG1ENC_VEX_MODRM_Mq_WO_Vq:
            BS3_ASSERT(pThis->fFlags & (BS3CG1INSTR_F_VEX_L_ZERO | BS3CG1INSTR_F_VEX_L_IGNORED));
            pThis->pfnEncoder        = pThis->fFlags & BS3CG1INSTR_F_VEX_L_ZERO
                                     ? Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_Lmbz_OR_ViceVersa
                                     : Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_Lig_OR_ViceVersa;
            pThis->iRmOp             = 0;
            pThis->iRegOp            = 1;
            pThis->aOperands[0].cbOp = 8;
            pThis->aOperands[1].cbOp = 8;
            pThis->aOperands[0].enmLocation  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].idxFieldBase = BS3CG1DST_XMM0_LO;
            break;

        case BS3CG1ENC_VEX_MODRM_Mq_WO_Vsd:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_Lig_OR_ViceVersa;
            pThis->iRmOp             = 0;
            pThis->iRegOp            = 1;
            pThis->aOperands[0].cbOp = 8;
            pThis->aOperands[1].cbOp = 8;
            pThis->aOperands[0].enmLocation  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].idxFieldBase = BS3CG1DST_INVALID;
            pThis->aOperands[1].idxFieldBase = BS3CG1DST_XMM0_LO;
            break;

        case BS3CG1ENC_VEX_MODRM_Mps_WO_Vps:
        case BS3CG1ENC_VEX_MODRM_Mpd_WO_Vpd:
        case BS3CG1ENC_VEX_MODRM_Mx_WO_Vx:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Msomething_Wip_OR_ViceVersa;
            pThis->iRmOp             = 0;
            pThis->iRegOp            = 1;
            pThis->aOperands[0].cbOp = 16;
            pThis->aOperands[1].cbOp = 16;
            pThis->aOperands[0].enmLocation  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[1].idxFieldBase = BS3CG1DST_XMM0;
            break;

        case BS3CG1ENC_VEX_MODRM_Uss_WO_HssHi_Vss:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Hsomething_Usomething_Lip_Wip_OR_ViceVersa;
            pThis->iRegOp            = 2;
            pThis->iRmOp             = 0;
            pThis->aOperands[0].cbOp = 16;
            pThis->aOperands[1].cbOp = 96;
            pThis->aOperands[2].cbOp = 4;
            pThis->aOperands[0].enmLocation  = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[2].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].idxFieldBase = BS3CG1DST_XMM0;
            pThis->aOperands[1].idxFieldBase = BS3CG1DST_XMM0_HI96;
            pThis->aOperands[2].idxFieldBase = BS3CG1DST_XMM0_DW0;
            break;

        case BS3CG1ENC_VEX_MODRM_Usd_WO_HsdHi_Vsd:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_VsomethingWO_Hsomething_Usomething_Lip_Wip_OR_ViceVersa;
            pThis->iRegOp            = 2;
            pThis->iRmOp             = 0;
            pThis->aOperands[0].cbOp = 16;
            pThis->aOperands[1].cbOp = 8;
            pThis->aOperands[2].cbOp = 8;
            pThis->aOperands[0].enmLocation  = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[1].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[2].enmLocation  = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].idxFieldBase = BS3CG1DST_XMM0;
            pThis->aOperands[1].idxFieldBase = BS3CG1DST_XMM0_HI;
            pThis->aOperands[2].idxFieldBase = BS3CG1DST_XMM0_LO;
            break;

        case BS3CG1ENC_VEX_MODRM_Wps_WO_Vps:
        case BS3CG1ENC_VEX_MODRM_Wpd_WO_Vpd:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_WsomethingWO_Vsomething_Wip_OR_ViceVersa;
            pThis->iRmOp             = 0;
            pThis->iRegOp            = 1;
            pThis->aOperands[0].cbOp = 16;
            pThis->aOperands[1].cbOp = 16;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[0].enmLocationReg  = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0;
            break;

        case BS3CG1ENC_VEX_MODRM_Wq_WO_Vq:
            BS3_ASSERT(pThis->fFlags & BS3CG1INSTR_F_VEX_L_ZERO);
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_WsomethingWO_Vsomething_Wip_Lmbz_OR_ViceVersa;
            pThis->iRegOp            = 1;
            pThis->iRmOp             = 0;
            pThis->aOperands[0].cbOp = 8;
            pThis->aOperands[1].cbOp = 8;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[0].enmLocationReg  = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0_LO;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0_LO;
            break;

        case BS3CG1ENC_VEX_MODRM_Wx_WO_Vx:
            pThis->pfnEncoder        = Bs3Cg1EncodeNext_VEX_MODRM_WsomethingWO_Vsomething_Wip_OR_ViceVersa;
            pThis->iRmOp             = 0;
            pThis->iRegOp            = 1;
            pThis->aOperands[0].cbOp = 16;
            pThis->aOperands[1].cbOp = 16;
            pThis->aOperands[0].enmLocation     = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[0].enmLocationReg  = BS3CG1OPLOC_CTX_ZX_VLMAX;
            pThis->aOperands[0].enmLocationMem  = BS3CG1OPLOC_MEM_WO;
            pThis->aOperands[1].enmLocation     = BS3CG1OPLOC_CTX;
            pThis->aOperands[0].idxFieldBase    = BS3CG1DST_XMM0;
            pThis->aOperands[1].idxFieldBase    = BS3CG1DST_XMM0;
            break;


            /* Unused or invalid instructions mostly. */
        //case BS3CG1ENC_VEX_FIXED:
        //    pThis->pfnEncoder = Bs3Cg1EncodeNext_VEX_FIXED;
        //    break;
        case BS3CG1ENC_VEX_MODRM_MOD_EQ_3:
            pThis->pfnEncoder = Bs3Cg1EncodeNext_VEX_MODRM_MOD_EQ_3;
            break;
        case BS3CG1ENC_VEX_MODRM_MOD_NE_3:
            pThis->pfnEncoder = Bs3Cg1EncodeNext_VEX_MODRM_MOD_NE_3;
            break;
        case BS3CG1ENC_VEX_MODRM:
            pThis->pfnEncoder = Bs3Cg1EncodeNext_VEX_MODRM;
            break;

#endif /* BS3CG1_WITH_VEX */

        default:
            Bs3TestFailedF("Invalid/unimplemented enmEncoding for instruction #%RU32 (%.*s): %d",
                           pThis->iInstr, pThis->cchMnemonic, pThis->pchMnemonic, pThis->enmEncoding);
            return false;
    }
    return true;
}


/**
 * Calculates the appropriate non-intel invalid instruction encoding.
 *
 * @returns the encoding to use instead.
 * @param   enmEncoding         The intel invalid instruction encoding.
 */
static BS3CG1ENC Bs3Cg1CalcNoneIntelInvalidEncoding(BS3CG1ENC enmEncoding)
{
    switch (enmEncoding)
    {
        case BS3CG1ENC_MODRM_Gb_Eb:
        case BS3CG1ENC_MODRM_Gv_RO_Ma:
        case BS3CG1ENC_FIXED:
            return BS3CG1ENC_FIXED;
        default:
            Bs3TestFailedF("Bs3Cg1CalcNoneIntelInvalidEncoding: Unsupported encoding: %d\n", enmEncoding);
            return BS3CG1ENC_FIXED;
    }
}


/**
 * Sets cbOpDefault, cbOpOvrd66 and cbOpOvrdRexW.
 *
 * @param   pThis               The state.
 * @param   bMode               The mode (only code part is used).
 */
static void Bs3Cg1SetOpSizes(PBS3CG1STATE pThis, uint8_t bMode)
{
    if (BS3_MODE_IS_16BIT_CODE(bMode))
    {
        pThis->cbOpDefault  = 2;
        pThis->cbOpOvrd66   = 4;
        pThis->cbOpOvrdRexW = 0;
    }
    else if (BS3_MODE_IS_32BIT_CODE(bMode))
    {
        pThis->cbOpDefault  = 4;
        pThis->cbOpOvrd66   = 2;
        pThis->cbOpOvrdRexW = 0;
    }
    else
    {
        pThis->cbOpDefault  = 4;
        pThis->cbOpOvrd66   = 2;
        pThis->cbOpOvrdRexW = 8;
    }
}


/**
 * Sets up SSE and maybe AVX.
 *
 * @returns true (if successful, false if not and the SSE instructions ends up
 *          being invalid).
 * @param   pThis               The state.
 */
static bool BS3_NEAR_CODE Bs3Cg3SetupSseAndAvx(PBS3CG1STATE pThis)
{
    if (!pThis->fWorkExtCtx)
    {
        unsigned i;
        uint32_t cr0 = ASMGetCR0();
        uint32_t cr4 = ASMGetCR4();

        cr0 &= ~(X86_CR0_TS | X86_CR0_MP | X86_CR0_EM);
        cr0 |= X86_CR0_NE;
        ASMSetCR0(cr0);
        if (pThis->pExtCtx->enmMethod == BS3EXTCTXMETHOD_XSAVE)
        {
            cr4 |= X86_CR4_OSFXSR | X86_CR4_OSXMMEEXCPT | X86_CR4_OSXSAVE;
            ASMSetCR4(cr4);
            ASMSetXcr0(pThis->pExtCtx->fXcr0Nominal);
        }
        else
        {
            cr4 |= X86_CR4_OSFXSR | X86_CR4_OSXMMEEXCPT;
            ASMSetCR4(cr4);
        }

        for (i = 0; i < RT_ELEMENTS(pThis->aInitialCtxs); i++)
        {
            pThis->aInitialCtxs[i].cr0.u32 = cr0;
            pThis->aInitialCtxs[i].cr4.u32 = cr4;
        }
        pThis->fWorkExtCtx = true;
    }

    return true;
}


/**
 * Next CPU configuration to test the current instruction in.
 *
 * This is for testing FPU, SSE and AVX instructions with the various lazy state
 * load and enable bits in different configurations to ensure we're getting the
 * right response.
 *
 * This also cleans up the CPU and test driver state.
 *
 * @returns true if we're to do another round, false if we're done.
 * @param   pThis           The state.
 * @param   iCpuSetup       The current CPU setup number.
 * @param   pfInvalidInstr  Where to indicate whether the setup causes an
 *                          invalid instruction or not.  This is also used as
 *                          input to avoid unnecessary CPUID work.
 */
static bool BS3_NEAR_CODE Bs3Cg1CpuSetupNext(PBS3CG1STATE pThis, unsigned iCpuSetup, bool BS3_FAR *pfInvalidInstr)
{
    if (   (pThis->fFlags & BS3CG1INSTR_F_INVALID_64BIT)
        && BS3CG1_IS_64BIT_TARGET(pThis))
        return false;

    switch (pThis->enmCpuTest)
    {
        case BS3CG1CPU_ANY:
        case BS3CG1CPU_GE_80186:
        case BS3CG1CPU_GE_80286:
        case BS3CG1CPU_GE_80386:
        case BS3CG1CPU_GE_80486:
        case BS3CG1CPU_GE_Pentium:
        case BS3CG1CPU_CLFSH:
        case BS3CG1CPU_CLFLUSHOPT:
            return false;

        case BS3CG1CPU_MMX:
            return false;

        case BS3CG1CPU_SSE:
        case BS3CG1CPU_SSE2:
        case BS3CG1CPU_SSE3:
        case BS3CG1CPU_SSE4_1:
        case BS3CG1CPU_AVX:
        case BS3CG1CPU_AVX2:
            if (iCpuSetup > 0 || *pfInvalidInstr)
            {
                /** @todo do more configs here. */
                pThis->fWorkExtCtx = false;
                ASMSetCR0(ASMGetCR0() | X86_CR0_EM | X86_CR0_MP);
                ASMSetCR4(ASMGetCR4() & ~(X86_CR4_OSFXSR | X86_CR4_OSXMMEEXCPT | X86_CR4_OSXSAVE));
                return false;
            }
            return false;

        default:
            Bs3TestFailedF("Invalid enmCpuTest value: %d", pThis->enmCpuTest);
            return false;
    }
}


/**
 * Check if the instruction is supported by the CPU, possibly making state
 * adjustments to enable support for it.
 *
 * @returns true if supported, false if not.
 * @param   pThis               The state.
 */
static bool BS3_NEAR_CODE Bs3Cg1CpuSetupFirst(PBS3CG1STATE pThis)
{
    uint32_t fEax;
    uint32_t fEbx;
    uint32_t fEcx;
    uint32_t fEdx;

    if (   (pThis->fFlags & BS3CG1INSTR_F_INVALID_64BIT)
        && BS3CG1_IS_64BIT_TARGET(pThis))
        return false;

    switch (pThis->enmCpuTest)
    {
        case BS3CG1CPU_ANY:
            return true;

        case BS3CG1CPU_GE_80186:
            if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80186)
                return true;
            return false;

        case BS3CG1CPU_GE_80286:
            if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80286)
                return true;
            return false;

        case BS3CG1CPU_GE_80386:
            if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80386)
                return true;
            return false;

        case BS3CG1CPU_GE_80486:
            if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80486)
                return true;
            return false;

        case BS3CG1CPU_GE_Pentium:
            if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_Pentium)
                return true;
            return false;

        case BS3CG1CPU_MMX:
            if (g_uBs3CpuDetected & BS3CPU_F_CPUID)
            {
                ASMCpuIdExSlow(1, 0, 0, 0, NULL, NULL, NULL, &fEdx);
                if (fEdx & X86_CPUID_FEATURE_EDX_MMX)
                    return Bs3Cg3SetupSseAndAvx(pThis); /** @todo only do FNSAVE/FXSAVE here? */
            }
            return false;

        case BS3CG1CPU_SSE:
        case BS3CG1CPU_SSE2:
        case BS3CG1CPU_SSE3:
        case BS3CG1CPU_SSE4_1:
        case BS3CG1CPU_AVX:
            if (g_uBs3CpuDetected & BS3CPU_F_CPUID)
            {
                ASMCpuIdExSlow(1, 0, 0, 0, NULL, NULL, &fEcx, &fEdx);
                switch (pThis->enmCpuTest)
                {
                    case BS3CG1CPU_SSE:
                        if (fEdx & X86_CPUID_FEATURE_EDX_SSE)
                            return Bs3Cg3SetupSseAndAvx(pThis);
                        return false;
                    case BS3CG1CPU_SSE2:
                        if (fEdx & X86_CPUID_FEATURE_EDX_SSE2)
                            return Bs3Cg3SetupSseAndAvx(pThis);
                        return false;
                    case BS3CG1CPU_SSE3:
                        if (fEcx & X86_CPUID_FEATURE_ECX_SSE3)
                            return Bs3Cg3SetupSseAndAvx(pThis);
                        return false;
                    case BS3CG1CPU_SSE4_1:
                        if (fEcx & X86_CPUID_FEATURE_ECX_SSE4_1)
                            return Bs3Cg3SetupSseAndAvx(pThis);
                        return false;
                    case BS3CG1CPU_AVX:
                        if (fEcx & X86_CPUID_FEATURE_ECX_AVX)
                            return Bs3Cg3SetupSseAndAvx(pThis) && !BS3_MODE_IS_RM_OR_V86(pThis->bMode);
                        return false;
                    default: BS3_ASSERT(0); /* impossible */
                }
            }
            return false;

        case BS3CG1CPU_AVX2:
            if (g_uBs3CpuDetected & BS3CPU_F_CPUID)
            {
                ASMCpuIdExSlow(7, 0, 0/*leaf*/, 0, &fEax, &fEbx, &fEcx, &fEdx);
                switch (pThis->enmCpuTest)
                {
                    case BS3CG1CPU_AVX2:
                        if (fEbx & X86_CPUID_STEXT_FEATURE_EBX_AVX2)
                            return Bs3Cg3SetupSseAndAvx(pThis) && !BS3_MODE_IS_RM_OR_V86(pThis->bMode);
                        return false;
                    default: BS3_ASSERT(0); return false; /* impossible */
                }
            }
            return false;

        case BS3CG1CPU_CLFSH:
            if (g_uBs3CpuDetected & BS3CPU_F_CPUID)
            {
                ASMCpuIdExSlow(1, 0, 0, 0, NULL, NULL, NULL, &fEdx);
                if (fEdx & X86_CPUID_FEATURE_EDX_CLFSH)
                    return true;
            }
            return false;

        case BS3CG1CPU_CLFLUSHOPT:
            if (g_uBs3CpuDetected & BS3CPU_F_CPUID)
            {
                ASMCpuIdExSlow(7, 0, 0/*leaf*/, 0, NULL, &fEbx, NULL, NULL);
                if (fEbx & X86_CPUID_STEXT_FEATURE_EBX_CLFLUSHOPT)
                    return true;
            }
            return false;

        default:
            Bs3TestFailedF("Invalid enmCpuTest value: %d", pThis->enmCpuTest);
            return false;
    }
}



/**
 * Checks the preconditions for a test.
 *
 * @returns true if the test be executed, false if not.
 * @param   pThis       The state.
 * @param   pHdr        The test header.
 */
static bool BS3_NEAR_CODE Bs3Cg1RunSelector(PBS3CG1STATE pThis, PCBS3CG1TESTHDR pHdr)
{

    uint8_t const BS3_FAR *pbCode = (uint8_t const BS3_FAR *)(pHdr + 1);
    unsigned cbLeft = pHdr->cbSelector;
    while (cbLeft-- > 0)
    {
        switch (*pbCode++)
        {
#define CASE_PRED(a_Pred, a_Expr) \
            case ((a_Pred) << BS3CG1SEL_OP_KIND_MASK) | BS3CG1SEL_OP_IS_TRUE: \
                if (!(a_Expr)) return false; \
                break; \
            case ((a_Pred) << BS3CG1SEL_OP_KIND_MASK) | BS3CG1SEL_OP_IS_FALSE: \
                if (a_Expr) return false; \
                break
            CASE_PRED(BS3CG1PRED_SIZE_O16, pThis->cbOperand == 2);
            CASE_PRED(BS3CG1PRED_SIZE_O32, pThis->cbOperand == 4);
            CASE_PRED(BS3CG1PRED_SIZE_O64, pThis->cbOperand == 8);
            CASE_PRED(BS3CG1PRED_VEXL_0, pThis->uVexL == 0);
            CASE_PRED(BS3CG1PRED_VEXL_1, pThis->uVexL == 1);
            CASE_PRED(BS3CG1PRED_RING_0, pThis->uCpl == 0);
            CASE_PRED(BS3CG1PRED_RING_1, pThis->uCpl == 1);
            CASE_PRED(BS3CG1PRED_RING_2, pThis->uCpl == 2);
            CASE_PRED(BS3CG1PRED_RING_3, pThis->uCpl == 3);
            CASE_PRED(BS3CG1PRED_RING_0_THRU_2, pThis->uCpl <= 2);
            CASE_PRED(BS3CG1PRED_RING_1_THRU_3, pThis->uCpl >= 1);
            CASE_PRED(BS3CG1PRED_CODE_64BIT, BS3CG1_IS_64BIT_TARGET(pThis));
            CASE_PRED(BS3CG1PRED_CODE_32BIT, BS3_MODE_IS_32BIT_CODE(pThis->bMode));
            CASE_PRED(BS3CG1PRED_CODE_16BIT, BS3_MODE_IS_16BIT_CODE(pThis->bMode));
            CASE_PRED(BS3CG1PRED_MODE_REAL,  BS3_MODE_IS_RM_SYS(pThis->bMode));
            CASE_PRED(BS3CG1PRED_MODE_PROT,  BS3_MODE_IS_PM_SYS(pThis->bMode));
            CASE_PRED(BS3CG1PRED_MODE_LONG,  BS3_MODE_IS_64BIT_SYS(pThis->bMode));
            CASE_PRED(BS3CG1PRED_MODE_SMM,   false);
            CASE_PRED(BS3CG1PRED_MODE_VMX,   false);
            CASE_PRED(BS3CG1PRED_MODE_SVM,   false);
            CASE_PRED(BS3CG1PRED_PAGING_ON,  BS3_MODE_IS_PAGED(pThis->bMode));
            CASE_PRED(BS3CG1PRED_PAGING_OFF, !BS3_MODE_IS_PAGED(pThis->bMode));
            CASE_PRED(BS3CG1PRED_VENDOR_AMD,   pThis->bCpuVendor == BS3CPUVENDOR_AMD);
            CASE_PRED(BS3CG1PRED_VENDOR_INTEL, pThis->bCpuVendor == BS3CPUVENDOR_INTEL);
            CASE_PRED(BS3CG1PRED_VENDOR_VIA,   pThis->bCpuVendor == BS3CPUVENDOR_VIA);
            CASE_PRED(BS3CG1PRED_VENDOR_SHANGHAI, pThis->bCpuVendor == BS3CPUVENDOR_SHANGHAI);
            CASE_PRED(BS3CG1PRED_VENDOR_HYGON, pThis->bCpuVendor == BS3CPUVENDOR_HYGON);

#undef CASE_PRED
            default:
                return Bs3TestFailedF("Invalid selector opcode %#x!", pbCode[-1]);
        }
    }

    return true;
}


#ifdef BS3CG1_DEBUG_CTX_MOD
/**
 * Translates the operator into a string.
 *
 * @returns Read-only string pointer.
 * @param   bOpcode             The context modifier program opcode.
 */
static const char BS3_FAR * BS3_NEAR_CODE Bs3Cg1CtxOpToString(uint8_t bOpcode)
{
    switch (bOpcode & BS3CG1_CTXOP_OPERATOR_MASK)
    {
        case BS3CG1_CTXOP_ASSIGN:   return "=";
        case BS3CG1_CTXOP_OR:       return "|=";
        case BS3CG1_CTXOP_AND:      return "&=";
        case BS3CG1_CTXOP_AND_INV:  return "&~=";
        default:                    return "?WTF?";
    }
}
#endif


/**
 * Runs a context modifier program.
 *
 * @returns Success indicator (true/false).
 * @param   pThis       The state.
 * @param   pCtx        The context.
 * @param   pHdr        The program header.
 * @param   off         The program offset relative to the end of the header.
 * @param   cb          The program size.
 * @param   pEflCtx     The context to take undefined EFLAGS from.  (This is NULL
 *                      if we're processing a input context modifier program.)
 * @param   pbInstr     Points to the first instruction byte.  For storing
 *                      immediate operands during input context modification.
 *                      NULL for output contexts.
 */
static bool BS3_NEAR_CODE Bs3Cg1RunContextModifier(PBS3CG1STATE pThis, PBS3REGCTX pCtx, PCBS3CG1TESTHDR pHdr,
                                                   unsigned off, unsigned cb,
                                                   PCBS3REGCTX pEflCtx, uint8_t BS3_FAR *pbInstr)
{
    uint8_t const BS3_FAR *pbCode = (uint8_t const BS3_FAR *)(pHdr + 1) + off;
    int                    cbLeft = cb;
    while (cbLeft-- > 0)
    {
        /*
         * Decode the instruction.
         */
        uint8_t const       bOpcode = *pbCode++;
        unsigned            cbValue;
        unsigned            cbDst;
        BS3CG1DST           idxField;
        BS3PTRUNION         PtrField;
        uint8_t BS3_FAR    *pbMemCopy = NULL;
        bool                fZxVlMax;

        /* Expand the destiation field (can be escaped). Set fZxVlMax. */
        switch (bOpcode & BS3CG1_CTXOP_DST_MASK)
        {
            case BS3CG1_CTXOP_OP1:
                idxField = pThis->aOperands[0].idxField;
                if (idxField == BS3CG1DST_INVALID)
                    idxField = BS3CG1DST_OP1;
                fZxVlMax = pEflCtx != NULL && pThis->aOperands[0].enmLocation == BS3CG1OPLOC_CTX_ZX_VLMAX;
                break;

            case BS3CG1_CTXOP_OP2:
                idxField = pThis->aOperands[1].idxField;
                if (idxField == BS3CG1DST_INVALID)
                    idxField = BS3CG1DST_OP2;
                fZxVlMax = pEflCtx != NULL && pThis->aOperands[1].enmLocation == BS3CG1OPLOC_CTX_ZX_VLMAX;
                break;

            case BS3CG1_CTXOP_EFL:
                idxField = BS3CG1DST_EFL;
                fZxVlMax = false;
                break;

            case BS3CG1_CTXOP_DST_ESC:
                if (cbLeft-- > 0)
                {
                    idxField = (BS3CG1DST)*pbCode++;
                    if (idxField <= BS3CG1DST_OP4)
                    {
                        if (idxField > BS3CG1DST_INVALID)
                        {
                            unsigned idxOp     = idxField - BS3CG1DST_OP1;
                            uint8_t  idxField2 = pThis->aOperands[idxOp].idxField;
                            if (idxField2 != BS3CG1DST_INVALID)
                                idxField = idxField2;
                            fZxVlMax = pEflCtx != NULL && pThis->aOperands[idxOp].enmLocation == BS3CG1OPLOC_CTX_ZX_VLMAX;
                            break;
                        }
                    }
                    else if (idxField < BS3CG1DST_END)
                    {
                        fZxVlMax = false;
                        break;
                    }
                    return Bs3TestFailedF("Malformed context instruction: idxField=%d", idxField);
                }
                RT_FALL_THRU();
            default:
                return Bs3TestFailed("Malformed context instruction: Destination");
        }

        /* Expand value size (can be escaped). */
        switch (bOpcode & BS3CG1_CTXOP_SIZE_MASK)
        {
            case BS3CG1_CTXOP_1_BYTE:   cbValue =  1; break;
            case BS3CG1_CTXOP_2_BYTES:  cbValue =  2; break;
            case BS3CG1_CTXOP_4_BYTES:  cbValue =  4; break;
            case BS3CG1_CTXOP_8_BYTES:  cbValue =  8; break;
            case BS3CG1_CTXOP_16_BYTES: cbValue = 16; break;
            case BS3CG1_CTXOP_32_BYTES: cbValue = 32; break;
            case BS3CG1_CTXOP_12_BYTES: cbValue = 12; break;
            case BS3CG1_CTXOP_SIZE_ESC:
                if (cbLeft-- > 0)
                {
                    cbValue = *pbCode++;
                    if (cbValue)
                        break;
                }
                RT_FALL_THRU();
            default:
                return Bs3TestFailed("Malformed context instruction: size");
        }

        /* Make sure there is enough instruction bytes for the value. */
        if (cbValue <= cbLeft)
        { /* likely */ }
        else
            return Bs3TestFailedF("Malformed context instruction: %u bytes value, %u bytes left", cbValue, cbLeft);

        /*
         * Do value processing specific to the target field size.
         */
        cbDst = g_acbBs3Cg1DstFields[idxField];
        if (cbDst == BS3CG1DSTSIZE_OPERAND)
            cbDst = pThis->aOperands[idxField - BS3CG1DST_OP1].cbOp;
        else if (cbDst == BS3CG1DSTSIZE_OPERAND_SIZE_GRP)
            cbDst = pThis->cbOperand;
        if (cbDst <= 8)
        {
            unsigned const offField = g_aoffBs3Cg1DstFields[idxField];

            /*
             * Deal with fields up to 8-byte wide.
             */

            /* Get the value. */
            uint64_t uValue;
            if ((bOpcode & BS3CG1_CTXOP_SIGN_EXT))
                switch (cbValue)
                {
                    case 1: uValue = *(int8_t   const BS3_FAR *)pbCode; break;
                    case 2: uValue = *(int16_t  const BS3_FAR *)pbCode; break;
                    case 4: uValue = *(int32_t  const BS3_FAR *)pbCode; break;
                    default:
                        if (cbValue >= 8)
                        {
                            uValue = *(uint64_t const BS3_FAR *)pbCode;
                            break;
                        }
                        return Bs3TestFailedF("Malformed context instruction: %u bytes value (%u dst)", cbValue, cbDst);
                }
            else
                switch (cbValue)
                {
                    case 1: uValue = *(uint8_t  const BS3_FAR *)pbCode; break;
                    case 2: uValue = *(uint16_t const BS3_FAR *)pbCode; break;
                    case 4: uValue = *(uint32_t const BS3_FAR *)pbCode; break;
                    default:
                        if (cbValue >= 8)
                        {
                            uValue = *(uint64_t const BS3_FAR *)pbCode;
                            break;
                        }
                        return Bs3TestFailedF("Malformed context instruction: %u bytes value (%u dst)", cbValue, cbDst);
                }

            /* Find the field. */
            if (offField < sizeof(BS3REGCTX))
                PtrField.pu8 = (uint8_t BS3_FAR *)pCtx + offField;
            /* Non-register operands: */
            else if ((unsigned)(idxField - BS3CG1DST_OP1) < 4U)
            {
                unsigned const idxOp = idxField - BS3CG1DST_OP1;

                switch (pThis->aOperands[idxOp].enmLocation)
                {
                    case BS3CG1OPLOC_IMM:
                        if (pbInstr)
                            PtrField.pu8 = &pbInstr[pThis->aOperands[idxOp].off];
                        else
                            return Bs3TestFailedF("Immediate operand referenced in output context!");
                        break;

                    case BS3CG1OPLOC_MEM:
                        if (!pbInstr)
                            return Bs3TestFailedF("Read only operand specified in output!");
                        PtrField.pu8 = &pThis->pbDataPg[X86_PAGE_SIZE - pThis->aOperands[idxOp].off];
                        break;

                    case BS3CG1OPLOC_MEM_RW:
                    case BS3CG1OPLOC_MEM_WO:
                        if (pbInstr)
                        {
                            PtrField.pu8 = &pThis->pbDataPg[X86_PAGE_SIZE - pThis->aOperands[idxOp].off];
                            pbMemCopy    = pThis->MemOp.ab;
                        }
                        else
                            PtrField.pu8 = pThis->MemOp.ab;
                        break;

                    default:
                        if (pThis->enmEncoding != pThis->enmEncodingNonInvalid)
                            goto l_advance_to_next;
                        return Bs3TestFailedF("Internal error: cbDst=%u idxField=%d (%d) offField=%#x: enmLocation=%u off=%#x idxField=%u",
                                              cbDst, idxField, idxOp, offField, pThis->aOperands[idxOp].enmLocation,
                                              pThis->aOperands[idxOp].off, pThis->aOperands[idxOp].idxField);
                }
            }
            /* Special field: Copying in undefined EFLAGS from the result context. */
            else if (idxField == BS3CG1DST_EFL_UNDEF)
            {
                if (!pEflCtx || (bOpcode & BS3CG1_CTXOP_OPERATOR_MASK) != BS3CG1_CTXOP_ASSIGN)
                    return Bs3TestFailed("Invalid BS3CG1DST_EFL_UNDEF usage");
                PtrField.pu32 = &pCtx->rflags.u32;
                uValue = (*PtrField.pu32 & ~(uint32_t)uValue) | (pEflCtx->rflags.u32 & (uint32_t)uValue);
            }
            /* Special field: Expected value (in/result) exception. */
            else if (idxField == BS3CG1DST_VALUE_XCPT)
            {
                if (!pEflCtx || (bOpcode & BS3CG1_CTXOP_OPERATOR_MASK) != BS3CG1_CTXOP_ASSIGN || cbDst != 1)
                    return Bs3TestFailed("Invalid BS3CG1DST_VALUE_XCPT usage");
                PtrField.pu8 = &pThis->bValueXcpt;
            }
            /* FPU and FXSAVE format. */
            else if (   pThis->pExtCtx->enmMethod != BS3EXTCTXMETHOD_ANCIENT
                     && offField - sizeof(BS3REGCTX) < RT_UOFFSET_AFTER(BS3EXTCTX, Ctx.x87.aXMM[15]))
            {
                if (pThis->fWorkExtCtx)
                    PtrField.pb = (uint8_t *)pThis->pExtCtx + offField - sizeof(BS3REGCTX);
                else if (!pThis->fCpuSetupFirstResult)
                {
                    BS3CG1_DPRINTF(("dbg: Extended context disabled: skipping modification (<=8)\n"));
                    goto l_advance_to_next;
                }
                else
                    return Bs3TestFailedF("Extended context disabled: Field %d (%s) @ %#x LB %u\n",
                                          idxField, g_aszBs3Cg1DstFields[idxField].sz, offField, cbDst);
            }
            /** @todo other FPU fields and FPU state formats. */
            else
                return Bs3TestFailedF("Todo implement me: cbDst=%u idxField=%d %s offField=%#x (<= 8)",
                                      cbDst, idxField, g_aszBs3Cg1DstFields[idxField].sz, offField);

#ifdef BS3CG1_DEBUG_CTX_MOD
            switch (cbDst)
            {
                case 1:
                    BS3CG1_DPRINTF(("dbg: modify %s: %#04RX8 (LB %u) %s %#RX64 (LB %u)\n", g_aszBs3Cg1DstFields[idxField].sz,
                                    *PtrField.pu8, cbDst, Bs3Cg1CtxOpToString(bOpcode), uValue, cbValue));
                    break;
                case 2:
                    BS3CG1_DPRINTF(("dbg: modify %s: %#06RX16 (LB %u) %s %#RX64 (LB %u)\n", g_aszBs3Cg1DstFields[idxField].sz,
                                    *PtrField.pu16, cbDst, Bs3Cg1CtxOpToString(bOpcode), uValue, cbValue));
                    break;
                case 4:
                    BS3CG1_DPRINTF(("dbg: modify %s: %#010RX32 (LB %u) %s %#RX64 (LB %u)\n", g_aszBs3Cg1DstFields[idxField].sz,
                                    *PtrField.pu32, cbDst, Bs3Cg1CtxOpToString(bOpcode), uValue, cbValue));
                    break;
                default:
                    BS3CG1_DPRINTF(("dbg: modify %s: %#018RX64 (LB %u) %s %#RX64 (LB %u)\n", g_aszBs3Cg1DstFields[idxField].sz,
                                    *PtrField.pu64, cbDst, Bs3Cg1CtxOpToString(bOpcode), uValue, cbValue));
                    break;
            }
#endif

            /* Modify the field. */
            switch (cbDst)
            {
                case 1:
                    switch (bOpcode & BS3CG1_CTXOP_OPERATOR_MASK)
                    {
                        case BS3CG1_CTXOP_ASSIGN:   *PtrField.pu8  =  (uint8_t)uValue; break;
                        case BS3CG1_CTXOP_OR:       *PtrField.pu8 |=  (uint8_t)uValue; break;
                        case BS3CG1_CTXOP_AND:      *PtrField.pu8 &=  (uint8_t)uValue; break;
                        case BS3CG1_CTXOP_AND_INV:  *PtrField.pu8 &= ~(uint8_t)uValue; break;
                    }
                    break;

                case 2:
                    switch (bOpcode & BS3CG1_CTXOP_OPERATOR_MASK)
                    {
                        case BS3CG1_CTXOP_ASSIGN:   *PtrField.pu16  =  (uint16_t)uValue; break;
                        case BS3CG1_CTXOP_OR:       *PtrField.pu16 |=  (uint16_t)uValue; break;
                        case BS3CG1_CTXOP_AND:      *PtrField.pu16 &=  (uint16_t)uValue; break;
                        case BS3CG1_CTXOP_AND_INV:  *PtrField.pu16 &= ~(uint16_t)uValue; break;
                    }
                    break;

                case 4:
                    if (   (unsigned)(idxField - BS3CG1DST_XMM0_DW0_ZX) <= (unsigned)(BS3CG1DST_XMM15_DW0_ZX - BS3CG1DST_XMM0_DW0_ZX)
                        || fZxVlMax)
                    {
                        PtrField.pu32[1] = 0;
                        PtrField.pu64[1] = 0;
                    }
                    else if (offField <= RT_UOFFSETOF(BS3REGCTX, r15) /* Clear the top dword. */)
                        PtrField.pu32[1] = 0;
                    else if ((unsigned)(idxField - BS3CG1DST_MM0_LO_ZX) <= (unsigned)(BS3CG1DST_MM7_LO_ZX - BS3CG1DST_MM0_LO_ZX))
                    {
                        PtrField.pu32[1] = 0;
                        PtrField.pu32[2] = 0xffff; /* observed on skylake */
                    }
                    switch (bOpcode & BS3CG1_CTXOP_OPERATOR_MASK)
                    {
                        case BS3CG1_CTXOP_ASSIGN:   *PtrField.pu32  =  (uint32_t)uValue; break;
                        case BS3CG1_CTXOP_OR:       *PtrField.pu32 |=  (uint32_t)uValue; break;
                        case BS3CG1_CTXOP_AND:      *PtrField.pu32 &=  (uint32_t)uValue; break;
                        case BS3CG1_CTXOP_AND_INV:  *PtrField.pu32 &= ~(uint32_t)uValue; break;
                    }
                    break;

                case 8:
                    if (   (unsigned)(idxField - BS3CG1DST_XMM0_LO_ZX) <= (unsigned)(BS3CG1DST_XMM15_LO_ZX - BS3CG1DST_XMM0_LO_ZX)
                        || fZxVlMax)
                        PtrField.pu64[1] = 0;
                    else if ((unsigned)(idxField - BS3CG1DST_MM0) <= (unsigned)(BS3CG1DST_MM7 - BS3CG1DST_MM0))
                        PtrField.pu32[2] = 0xffff; /* observed on skylake */

                    switch (bOpcode & BS3CG1_CTXOP_OPERATOR_MASK)
                    {
                        case BS3CG1_CTXOP_ASSIGN:   *PtrField.pu64  =  (uint64_t)uValue; break;
                        case BS3CG1_CTXOP_OR:       *PtrField.pu64 |=  (uint64_t)uValue; break;
                        case BS3CG1_CTXOP_AND:      *PtrField.pu64 &=  (uint64_t)uValue; break;
                        case BS3CG1_CTXOP_AND_INV:  *PtrField.pu64 &= ~(uint64_t)uValue; break;
                    }
                    break;

                default:
                    return Bs3TestFailedF("Malformed context instruction: cbDst=%u, expected 1, 2, 4, or 8", cbDst);
            }

#ifdef BS3CG1_DEBUG_CTX_MOD
            switch (cbDst)
            {
                case 1:  BS3CG1_DPRINTF(("dbg:    --> %s: %#04RX8\n",   g_aszBs3Cg1DstFields[idxField].sz, *PtrField.pu8));  break;
                case 2:  BS3CG1_DPRINTF(("dbg:    --> %s: %#06RX16\n",  g_aszBs3Cg1DstFields[idxField].sz, *PtrField.pu16)); break;
                case 4:  BS3CG1_DPRINTF(("dbg:    --> %s: %#010RX32\n", g_aszBs3Cg1DstFields[idxField].sz, *PtrField.pu32)); break;
                default: BS3CG1_DPRINTF(("dbg:    --> %s: %#018RX64\n", g_aszBs3Cg1DstFields[idxField].sz, *PtrField.pu64)); break;
            }
#endif
            if (fZxVlMax)
            {
                uintptr_t iReg = ((uintptr_t)PtrField.pu8 - (uintptr_t)&pThis->pExtCtx->Ctx.x87.aXMM[0])
                               / sizeof(pThis->pExtCtx->Ctx.x87.aXMM[0]);
                pThis->pExtCtx->Ctx.x.u.YmmHi.aYmmHi[iReg].au64[0] = 0;
                pThis->pExtCtx->Ctx.x.u.YmmHi.aYmmHi[iReg].au64[1] = 0;
#ifdef BS3CG1_DEBUG_CTX_MOD
                BS3CG1_DPRINTF(("dbg:    --> cleared YMM%u_HI\n", iReg));
#endif
            }
        }
        /*
         * Deal with larger field (FPU, SSE, AVX, ...).
         */
        else if (pThis->fWorkExtCtx)
        {
            union
            {
                X86FPUREG   FpuReg;
                X86XMMREG   XmmReg;
                X86YMMREG   YmmReg;
                X86ZMMREG   ZmmReg;
                uint8_t     ab[sizeof(X86ZMMREG)];
                uint32_t    au32[sizeof(X86ZMMREG) / sizeof(uint32_t)];
                uint64_t    au64[sizeof(X86ZMMREG) / sizeof(uint64_t)];
            } Value;
            unsigned const  offField = g_aoffBs3Cg1DstFields[idxField];
            unsigned        iReg;

            /* Copy the value into the union, doing the zero padding / extending. */
            Bs3MemCpy(&Value, pbCode, cbValue);
            if (cbValue < sizeof(Value))
            {
                if ((bOpcode & BS3CG1_CTXOP_SIGN_EXT) && (Value.ab[cbValue - 1] & 0x80))
                    Bs3MemSet(&Value.ab[cbValue], 0xff, sizeof(Value) - cbValue);
                else
                    Bs3MemSet(&Value.ab[cbValue], 0x00, sizeof(Value) - cbValue);
            }

            /* Optimized access to XMM and STx registers. */
            if (   pThis->pExtCtx->enmMethod != BS3EXTCTXMETHOD_ANCIENT
                && offField - sizeof(BS3REGCTX) < RT_UOFFSET_AFTER(BS3EXTCTX, Ctx.x87.aXMM[15]) )
                PtrField.pb = (uint8_t *)pThis->pExtCtx + offField - sizeof(BS3REGCTX);
            /* Non-register operands: */
            else if ((unsigned)(idxField - BS3CG1DST_OP1) < 4U)
            {
                unsigned const idxOp = idxField - BS3CG1DST_OP1;
                switch (pThis->aOperands[idxOp].enmLocation)
                {
                    case BS3CG1OPLOC_MEM:
                        if (!pbInstr)
                            return Bs3TestFailedF("Read only operand specified in output!");
                        PtrField.pu8 = &pThis->pbDataPg[X86_PAGE_SIZE - pThis->aOperands[idxOp].off];
                        break;

                    case BS3CG1OPLOC_MEM_RW:
                    case BS3CG1OPLOC_MEM_WO:
                        if (pbInstr)
                        {
                            PtrField.pu8 = &pThis->pbDataPg[X86_PAGE_SIZE - pThis->aOperands[idxOp].off];
                            pbMemCopy    = pThis->MemOp.ab;
                        }
                        else
                            PtrField.pu8 = pThis->MemOp.ab;
                        break;

                    default:
                        return Bs3TestFailedF("Internal error: Field %d (%d) @ %#x LB %u: enmLocation=%u off=%#x idxField=%u",
                                              idxField, idxOp, offField, cbDst, pThis->aOperands[idxOp].enmLocation,
                                              pThis->aOperands[idxOp].off, pThis->aOperands[idxOp].idxField);
                }
            }
            /* The YMM (AVX) registers have split storage in the state, so they need special handling. */
            else if ((iReg = idxField - BS3CG1DST_YMM0) < 16U)
            {
                /* The first 128-bits in XMM land. */
                PtrField.pu64 = &pThis->pExtCtx->Ctx.x87.aXMM[iReg].au64[0];
                switch (bOpcode & BS3CG1_CTXOP_OPERATOR_MASK)
                {
                    case BS3CG1_CTXOP_ASSIGN:
                        PtrField.pu64[0]  =  Value.au64[0];
                        PtrField.pu64[1]  =  Value.au64[1];
                        break;
                    case BS3CG1_CTXOP_OR:
                        PtrField.pu64[0] |=  Value.au64[0];
                        PtrField.pu64[1] |=  Value.au64[1];
                        break;
                    case BS3CG1_CTXOP_AND:
                        PtrField.pu64[0] &=  Value.au64[0];
                        PtrField.pu64[1] &=  Value.au64[1];
                        break;
                    case BS3CG1_CTXOP_AND_INV:
                        PtrField.pu64[0] &= ~Value.au64[0];
                        PtrField.pu64[1] &= ~Value.au64[1];
                        break;
                }

                /* The second 128-bit in YMM_HI land. */
                PtrField.pu64 = &pThis->pExtCtx->Ctx.x.u.YmmHi.aYmmHi[iReg].au64[0];
                switch (bOpcode & BS3CG1_CTXOP_OPERATOR_MASK)
                {
                    case BS3CG1_CTXOP_ASSIGN:
                        PtrField.pu64[0]  =  Value.au64[2];
                        PtrField.pu64[1]  =  Value.au64[3];
                        break;
                    case BS3CG1_CTXOP_OR:
                        PtrField.pu64[0] |=  Value.au64[2];
                        PtrField.pu64[1] |=  Value.au64[3];
                        break;
                    case BS3CG1_CTXOP_AND:
                        PtrField.pu64[0] &=  Value.au64[2];
                        PtrField.pu64[1] &=  Value.au64[3];
                        break;
                    case BS3CG1_CTXOP_AND_INV:
                        PtrField.pu64[0] &= ~Value.au64[2];
                        PtrField.pu64[1] &= ~Value.au64[3];
                        break;
                }
                PtrField.pb = NULL;
            }
            /* AVX512 needs handling like above, but more complicated. */
            else
                return Bs3TestFailedF("TODO: implement me: cbDst=%d idxField=%d (AVX and other weird state)", cbDst, idxField);

            if (PtrField.pb)
            {
                /* Modify the field / memory. */
                unsigned i;
                if (cbDst & 3)
                    return Bs3TestFailedF("Malformed context instruction: cbDst=%u, multiple of 4", cbDst);

#ifdef BS3CG1_DEBUG_CTX_MOD
                BS3CG1_DPRINTF(("dbg: modify %s: %.*Rhxs (LB %u) %s %.*Rhxs (LB %u)\n", g_aszBs3Cg1DstFields[idxField].sz,
                                cbDst, PtrField.pb, cbDst, Bs3Cg1CtxOpToString(bOpcode), cbValue, Value.ab, cbValue));
#endif

                i = cbDst / 4;
                while (i-- > 0)
                {
                    switch (bOpcode & BS3CG1_CTXOP_OPERATOR_MASK)
                    {
                        case BS3CG1_CTXOP_ASSIGN:   PtrField.pu32[i]  =  Value.au32[i]; break;
                        case BS3CG1_CTXOP_OR:       PtrField.pu32[i] |=  Value.au32[i]; break;
                        case BS3CG1_CTXOP_AND:      PtrField.pu32[i] &=  Value.au32[i]; break;
                        case BS3CG1_CTXOP_AND_INV:  PtrField.pu32[i] &= ~Value.au32[i]; break;
                    }
                }

#ifdef BS3CG1_DEBUG_CTX_MOD
                BS3CG1_DPRINTF(("dbg:    --> %s: %.*Rhxs\n", g_aszBs3Cg1DstFields[idxField].sz, cbDst, PtrField.pb));
#endif

                if (fZxVlMax)
                {
                    uintptr_t iReg = ((uintptr_t)PtrField.pu8 - (uintptr_t)&pThis->pExtCtx->Ctx.x87.aXMM[0])
                                   / sizeof(pThis->pExtCtx->Ctx.x87.aXMM[0]);
                    if (cbDst < 16)
                    {
                        for (i = cbDst / 4; i < 4; i++)
                            PtrField.pu32[i++] = 0;
#ifdef BS3CG1_DEBUG_CTX_MOD
                        BS3CG1_DPRINTF(("dbg:    --> cleared high %u bytes of XMM%u\n", 16 - cbDst, iReg));
#endif
                    }
                    pThis->pExtCtx->Ctx.x.u.YmmHi.aYmmHi[iReg].au64[0] = 0;
                    pThis->pExtCtx->Ctx.x.u.YmmHi.aYmmHi[iReg].au64[1] = 0;
#ifdef BS3CG1_DEBUG_CTX_MOD
                    BS3CG1_DPRINTF(("dbg:    --> cleared YMM%u_HI\n", iReg));
#endif
                }
            }

            /*
             * Hack! Update pThis->MemOp when setting up the inputs so we can
             *       correctly validate value and alignment exceptions.
             */
            if (pbMemCopy && PtrField.pv)
                Bs3MemCpy(pbMemCopy, PtrField.pv, cbDst);
        }
        /* !pThis->fWorkExtCtx: */
        else if (pThis->fCpuSetupFirstResult)
            return Bs3TestFailedF("Extended context disabled: Field %d (%s) @ %#x LB %u\n",
                                  idxField, g_aszBs3Cg1DstFields[idxField].sz, g_aoffBs3Cg1DstFields[idxField], cbDst);
        else
            BS3CG1_DPRINTF(("dbg: Extended context disabled: skipping modification [> 8]\n"));

        /*
         * Advance to the next instruction.
         */
l_advance_to_next:
        pbCode += cbValue;
        cbLeft -= cbValue;
    }

    return true;
}


/**
 * Checks the result of a run.
 *
 * @returns true if successful, false if not.
 * @param   pThis                   The state.
 * @param   bTestXcptExpected       The exception causing the test code to stop
 *                                  executing.
 * @param   fInvalidEncodingPgFault Set if we've cut the instruction a byte
 *                                  short and is expecting a \#PF on the page
 *                                  boundrary rather than a \#UD.  Only set if
 *                                  fInvalidEncoding is also set.
 * @param   iEncoding               For error reporting.
 */
static bool BS3_NEAR_CODE Bs3Cg1CheckResult(PBS3CG1STATE pThis, uint8_t bTestXcptExpected,
                                            bool fInvalidEncodingPgFault,  unsigned iEncoding)
{
    unsigned iOperand;

    /*
     * Check the exception state first.
     */
    uint8_t bExpectedXcpt;
    uint8_t cbAdjustPc;
    if (!pThis->fInvalidEncoding)
    {
        bExpectedXcpt = pThis->bAlignmentXcpt;
        if (bExpectedXcpt == UINT8_MAX)
            bExpectedXcpt = pThis->bValueXcpt;
        if (bExpectedXcpt == UINT8_MAX)
        {
            cbAdjustPc    = pThis->cbCurInstr;
            bExpectedXcpt = bTestXcptExpected;
            if (bTestXcptExpected == X86_XCPT_PF)
                pThis->Ctx.cr2.u = pThis->uCodePgFlat + X86_PAGE_SIZE;
        }
        else
            cbAdjustPc = 0;
    }
    else
    {
        cbAdjustPc = 0;
        if (!fInvalidEncodingPgFault)
            bExpectedXcpt = X86_XCPT_UD;
        else
        {
            bExpectedXcpt = X86_XCPT_PF;
            pThis->Ctx.cr2.u = pThis->uCodePgFlat + X86_PAGE_SIZE;
        }
    }
    if (RT_LIKELY(   pThis->TrapFrame.bXcpt     == bExpectedXcpt
                  && pThis->TrapFrame.Ctx.rip.u == pThis->Ctx.rip.u + cbAdjustPc))
    {
        /*
         * Check the register content.
         */
        bool fOkay = Bs3TestCheckRegCtxEx(&pThis->TrapFrame.Ctx, &pThis->Ctx,
                                           cbAdjustPc, 0 /*cbSpAdjust*/, 0 /*fExtraEfl*/,
                                           pThis->pszMode, iEncoding);

        /*
         * Check memory output operands.
         */
        if (!pThis->fInvalidEncoding)
        {
            iOperand = pThis->cOperands;
            while (iOperand-- > 0)
                if (   pThis->aOperands[iOperand].enmLocation == BS3CG1OPLOC_MEM_RW
                    || pThis->aOperands[iOperand].enmLocation == BS3CG1OPLOC_MEM_WO)
                {
                    if (pThis->aOperands[iOperand].off)
                    {
                        BS3PTRUNION PtrUnion;
                        PtrUnion.pb = &pThis->pbDataPg[X86_PAGE_SIZE - pThis->aOperands[iOperand].off];
                        switch (pThis->aOperands[iOperand].cbOp)
                        {
                            case 1:
                                if (*PtrUnion.pu8 == pThis->MemOp.ab[0])
                                    continue;
                                Bs3TestFailedF("op%u: Wrote %#04RX8, expected %#04RX8",
                                               iOperand, *PtrUnion.pu8, pThis->MemOp.ab[0]);
                                break;
                            case 2:
                                if (*PtrUnion.pu16 == pThis->MemOp.au16[0])
                                    continue;
                                Bs3TestFailedF("op%u: Wrote %#06RX16, expected %#06RX16",
                                               iOperand, *PtrUnion.pu16, pThis->MemOp.au16[0]);
                                break;
                            case 4:
                                if (*PtrUnion.pu32 == pThis->MemOp.au32[0])
                                    continue;
                                Bs3TestFailedF("op%u: Wrote %#010RX32, expected %#010RX32",
                                               iOperand, *PtrUnion.pu32, pThis->MemOp.au32[0]);
                                break;
                            case 8:
                                if (*PtrUnion.pu64 == pThis->MemOp.au64[0])
                                    continue;
                                Bs3TestFailedF("op%u: Wrote %#018RX64, expected %#018RX64",
                                               iOperand, *PtrUnion.pu64, pThis->MemOp.au64[0]);
                                break;
                            default:
                                if (Bs3MemCmp(PtrUnion.pb, pThis->MemOp.ab, pThis->aOperands[iOperand].cbOp) == 0)
                                    continue;
                                Bs3TestFailedF("op%u: Wrote %.*Rhxs, expected %.*Rhxs",
                                               iOperand,
                                               pThis->aOperands[iOperand].cbOp, PtrUnion.pb,
                                               pThis->aOperands[iOperand].cbOp, pThis->MemOp.ab);
                                break;
                        }
                    }
                    else
                        Bs3TestFailedF("op%u: off is zero\n", iOperand);
                    fOkay = false;
                }
        }

        /*
         * Check extended context if enabled.
         */
        if (pThis->fWorkExtCtx)
        {
            PBS3EXTCTX pExpect = pThis->pExtCtx;
            PBS3EXTCTX pResult = pThis->pResultExtCtx;
            unsigned   i;
            if (   pExpect->enmMethod == BS3EXTCTXMETHOD_XSAVE
                || pExpect->enmMethod == BS3EXTCTXMETHOD_FXSAVE)
            {
                /* Compare the x87 state, ASSUMING XCR0 bit 1 is set. */
#define CHECK_FIELD(a_Field, a_szFmt) \
    if (pResult->Ctx.a_Field != pExpect->Ctx.a_Field) fOkay = Bs3TestFailedF(a_szFmt, pResult->Ctx.a_Field, pExpect->Ctx.a_Field)
                CHECK_FIELD(x87.FCW, "FCW: %#06x, expected %#06x");
                CHECK_FIELD(x87.FSW, "FSW: %#06x, expected %#06x");
                CHECK_FIELD(x87.FTW, "FTW: %#06x, expected %#06x");
                //CHECK_FIELD(x87.FOP,      "FOP: %#06x, expected %#06x");
                //CHECK_FIELD(x87.FPUIP,    "FPUIP:  %#010RX32, expected %#010RX32");
                //CHECK_FIELD(x87.CS,       "FPUCS:  %#06x, expected %#06x");
                //CHECK_FIELD(x87.Rsrvd1,   "Rsrvd1: %#06x, expected %#06x");
                //CHECK_FIELD(x87.DP,       "FPUDP:  %#010RX32, expected %#010RX32");
                //CHECK_FIELD(x87.DS,       "FPUDS:  %#06x, expected %#06x");
                //CHECK_FIELD(x87.Rsrvd2,   "Rsrvd2: %#06x, expected %#06x");
                CHECK_FIELD(x87.MXCSR,      "MXCSR:  %#010RX32, expected %#010RX32");
#undef CHECK_FIELD
                for (i = 0; i < RT_ELEMENTS(pExpect->Ctx.x87.aRegs); i++)
                    if (   pResult->Ctx.x87.aRegs[i].au64[0] != pExpect->Ctx.x87.aRegs[i].au64[0]
                        || pResult->Ctx.x87.aRegs[i].au16[4] != pExpect->Ctx.x87.aRegs[i].au16[4])
                        fOkay = Bs3TestFailedF("ST[%u]: %c m=%#RX64 e=%d, expected %c m=%#RX64 e=%d", i,
                                               pResult->Ctx.x87.aRegs[i].r80Ex.s.fSign ? '-' : '+',
                                               pResult->Ctx.x87.aRegs[i].r80Ex.s.uMantissa,
                                               pResult->Ctx.x87.aRegs[i].r80Ex.s.uExponent,
                                               pExpect->Ctx.x87.aRegs[i].r80Ex.s.fSign ? '-' : '+',
                                               pExpect->Ctx.x87.aRegs[i].r80Ex.s.uMantissa,
                                               pExpect->Ctx.x87.aRegs[i].r80Ex.s.uExponent);
                for (i = 0; i < (ARCH_BITS == 64 ? 16 : 8); i++)
                    if (   pResult->Ctx.x87.aXMM[i].au64[0] != pExpect->Ctx.x87.aXMM[i].au64[0]
                        || pResult->Ctx.x87.aXMM[i].au64[1] != pExpect->Ctx.x87.aXMM[i].au64[1])
                        fOkay = Bs3TestFailedF("XMM%u: %#010RX64'%016RX64, expected %#010RX64'%08RX64", i,
                                               pResult->Ctx.x87.aXMM[i].au64[1],
                                               pResult->Ctx.x87.aXMM[i].au64[0],
                                               pExpect->Ctx.x87.aXMM[i].au64[1],
                                               pExpect->Ctx.x87.aXMM[i].au64[0]);
                if (pExpect->fXcr0Saved & XSAVE_C_YMM)
                    for (i = 0; i < (ARCH_BITS == 64 ? 16 : 8); i++)
                        if (   pResult->Ctx.x.u.YmmHi.aYmmHi[i].au64[0] != pExpect->Ctx.x.u.YmmHi.aYmmHi[i].au64[0]
                            || pResult->Ctx.x.u.YmmHi.aYmmHi[i].au64[1] != pExpect->Ctx.x.u.YmmHi.aYmmHi[i].au64[1])
                            fOkay = Bs3TestFailedF("YMM%u_HI: %#010RX64'%016RX64, expected %#010RX64'%08RX64", i,
                                                   pResult->Ctx.x.u.YmmHi.aYmmHi[i].au64[1],
                                                   pResult->Ctx.x.u.YmmHi.aYmmHi[i].au64[0],
                                                   pExpect->Ctx.x.u.YmmHi.aYmmHi[i].au64[1],
                                                   pExpect->Ctx.x.u.YmmHi.aYmmHi[i].au64[0]);
            }
            else
                fOkay = Bs3TestFailedF("Unsupported extended CPU context method: %d", pExpect->enmMethod);
        }

        /*
         * Done.
         */
        if (fOkay)
            return true;

        /*
         * Report failure.
         */
        Bs3TestFailedF("ins#%RU32/test#%u: encoding #%u: %.*Rhxs%s",
                       pThis->iInstr, pThis->iTest, iEncoding, pThis->cbCurInstr, pThis->abCurInstr,
                       fInvalidEncodingPgFault ? " (cut short)" : "");
    }
    else
        Bs3TestFailedF("ins#%RU32/test#%u: bXcpt=%#x expected %#x; rip=%RX64 expected %RX64; encoding#%u: %.*Rhxs%s",
                       pThis->iInstr, pThis->iTest,
                       pThis->TrapFrame.bXcpt, bExpectedXcpt,
                       pThis->TrapFrame.Ctx.rip.u, pThis->Ctx.rip.u + cbAdjustPc,
                       iEncoding, pThis->cbCurInstr, pThis->abCurInstr, fInvalidEncodingPgFault ? " (cut short)" : "");
    Bs3TestPrintf("cpl=%u cbOperands=%u\n", pThis->uCpl, pThis->cbOperand);

    /*
     * Display memory operands.
     */
    for (iOperand = 0; iOperand < pThis->cOperands; iOperand++)
    {
        BS3PTRUNION PtrUnion;
        switch (pThis->aOperands[iOperand].enmLocation)
        {
            case BS3CG1OPLOC_CTX:
            {
                uint8_t  idxField = pThis->aOperands[iOperand].idxField;
                unsigned offField = g_aoffBs3Cg1DstFields[idxField];
                if (offField <= sizeof(BS3REGCTX))
                    PtrUnion.pb = (uint8_t BS3_FAR *)&pThis->Ctx + offField;
                else
                {
                    Bs3TestPrintf("op%u: ctx%u: xxxx\n", iOperand, pThis->aOperands[iOperand].cbOp * 8);
                    break;
                }
                switch (pThis->aOperands[iOperand].cbOp)
                {
                    case 1: Bs3TestPrintf("op%u: ctx08: %#04RX8\n", iOperand, *PtrUnion.pu8); break;
                    case 2: Bs3TestPrintf("op%u: ctx16: %#06RX16\n", iOperand, *PtrUnion.pu16); break;
                    case 4: Bs3TestPrintf("op%u: ctx32: %#010RX32\n", iOperand, *PtrUnion.pu32); break;
                    case 8: Bs3TestPrintf("op%u: ctx64: %#018RX64\n", iOperand, *PtrUnion.pu64); break;
                    default:
                        Bs3TestPrintf("op%u: ctx%u: %.*Rhxs\n", iOperand, pThis->aOperands[iOperand].cbOp * 8,
                                      pThis->aOperands[iOperand].cbOp, PtrUnion.pb);
                        break;
                }
                break;
            }

            case BS3CG1OPLOC_IMM:
                PtrUnion.pb = &pThis->pbCodePg[pThis->aOperands[iOperand].off];
                switch (pThis->aOperands[iOperand].cbOp)
                {
                    case 1: Bs3TestPrintf("op%u: imm08: %#04RX8\n", iOperand, *PtrUnion.pu8); break;
                    case 2: Bs3TestPrintf("op%u: imm16: %#06RX16\n", iOperand, *PtrUnion.pu16); break;
                    case 4: Bs3TestPrintf("op%u: imm32: %#010RX32\n", iOperand, *PtrUnion.pu32); break;
                    case 8: Bs3TestPrintf("op%u: imm64: %#018RX64\n", iOperand, *PtrUnion.pu64); break;
                    default:
                        Bs3TestPrintf("op%u: imm%u: %.*Rhxs\n", iOperand, pThis->aOperands[iOperand].cbOp * 8,
                                      pThis->aOperands[iOperand].cbOp, PtrUnion.pb);
                        break;
                }
                break;

            case BS3CG1OPLOC_MEM:
            case BS3CG1OPLOC_MEM_RW:
            case BS3CG1OPLOC_MEM_WO:
                if (pThis->aOperands[iOperand].off)
                {
                    PtrUnion.pb = &pThis->pbDataPg[X86_PAGE_SIZE - pThis->aOperands[iOperand].off];
                    switch (pThis->aOperands[iOperand].cbOp)
                    {
                        case 1: Bs3TestPrintf("op%u: result mem08: %#04RX8\n", iOperand, *PtrUnion.pu8); break;
                        case 2: Bs3TestPrintf("op%u: result mem16: %#06RX16\n", iOperand, *PtrUnion.pu16); break;
                        case 4: Bs3TestPrintf("op%u: result mem32: %#010RX32\n", iOperand, *PtrUnion.pu32); break;
                        case 8: Bs3TestPrintf("op%u: result mem64: %#018RX64\n", iOperand, *PtrUnion.pu64); break;
                        default:
                            Bs3TestPrintf("op%u: result mem%u: %.*Rhxs\n", iOperand, pThis->aOperands[iOperand].cbOp * 8,
                                          pThis->aOperands[iOperand].cbOp, PtrUnion.pb);
                            break;
                    }
                    if (   pThis->aOperands[iOperand].enmLocation == BS3CG1OPLOC_MEM_WO
                        || pThis->aOperands[iOperand].enmLocation == BS3CG1OPLOC_MEM_RW)
                    {
                        PtrUnion.pb = pThis->MemOp.ab;
                        switch (pThis->aOperands[iOperand].cbOp)
                        {
                            case 1: Bs3TestPrintf("op%u: expect mem08: %#04RX8\n", iOperand, *PtrUnion.pu8); break;
                            case 2: Bs3TestPrintf("op%u: expect mem16: %#06RX16\n", iOperand, *PtrUnion.pu16); break;
                            case 4: Bs3TestPrintf("op%u: expect mem32: %#010RX32\n", iOperand, *PtrUnion.pu32); break;
                            case 8: Bs3TestPrintf("op%u: expect mem64: %#018RX64\n", iOperand, *PtrUnion.pu64); break;
                            default:
                                Bs3TestPrintf("op%u: expect mem%u: %.*Rhxs\n", iOperand, pThis->aOperands[iOperand].cbOp * 8,
                                              pThis->aOperands[iOperand].cbOp, PtrUnion.pb);
                                break;
                        }
                    }
                }
                else
                    Bs3TestPrintf("op%u: mem%u: zero off value!!\n", iOperand, pThis->aOperands[iOperand].cbOp * 8);
                break;
        }
    }

    /*
     * Display contexts.
     */
    Bs3TestPrintf("-- Expected context:\n");
    Bs3RegCtxPrint(&pThis->Ctx);
    if (pThis->fWorkExtCtx)
        Bs3TestPrintf("xcr0=%RX64\n", pThis->pExtCtx->fXcr0Saved);
    Bs3TestPrintf("-- Actual context:\n");
    Bs3TrapPrintFrame(&pThis->TrapFrame);
    if (pThis->fWorkExtCtx)
        Bs3TestPrintf("xcr0=%RX64\n", pThis->pResultExtCtx->fXcr0Saved);
    Bs3TestPrintf("\n");
    return false;
}


/**
 * Destroys the state, freeing all allocations and such.
 *
 * @param   pThis               The state.
 */
static void BS3_NEAR_CODE Bs3Cg1Destroy(PBS3CG1STATE pThis)
{
    if (BS3_MODE_IS_PAGED(pThis->bMode))
    {
#if ARCH_BITS != 16
        Bs3MemGuardedTestPageFree(pThis->pbCodePg);
        Bs3MemGuardedTestPageFree(pThis->pbDataPg);
#endif
    }
    else
    {
        Bs3MemFree(pThis->pbCodePg, X86_PAGE_SIZE);
        Bs3MemFree(pThis->pbDataPg, X86_PAGE_SIZE);
    }

    if (pThis->pExtCtx)
        Bs3MemFree(pThis->pExtCtx, pThis->pExtCtx->cb * 3);

    pThis->pbCodePg       = NULL;
    pThis->pbDataPg       = NULL;
    pThis->pExtCtx        = NULL;
    pThis->pResultExtCtx  = NULL;
    pThis->pInitialExtCtx = NULL;

    Bs3TestSubDone();
}


/**
 * Initializes the state.
 *
 * @returns Success indicator (true/false)
 * @param   pThis               The state.
 * @param   bMode               The mode being tested.
 */
bool BS3_NEAR_CODE BS3_CMN_NM(Bs3Cg1Init)(PBS3CG1STATE pThis, uint8_t bMode)
{
    BS3MEMKIND const    enmMemKind = BS3_MODE_IS_RM_OR_V86(bMode) ? BS3MEMKIND_REAL
                                   : !BS3_MODE_IS_64BIT_CODE(bMode) ? BS3MEMKIND_TILED : BS3MEMKIND_FLAT32;
    unsigned            iRing;
    unsigned            cb;
    unsigned            i;
    uint64_t            fFlags;
    PBS3EXTCTX          pExtCtx;

    Bs3MemSet(pThis, 0, sizeof(*pThis));

    pThis->iFirstRing         = BS3_MODE_IS_V86(bMode)    ? 3 : 0;
    pThis->iEndRing           = BS3_MODE_IS_RM_SYS(bMode) ? 1 : 4;
    pThis->bMode              = bMode;
    pThis->pszMode            = Bs3GetModeName(bMode);
    pThis->pszModeShort       = Bs3GetModeNameShortLower(bMode);
    pThis->bCpuVendor         = Bs3GetCpuVendor();
    pThis->pchMnemonic        = g_achBs3Cg1Mnemonics;
    pThis->pabOperands        = g_abBs3Cg1Operands;
    pThis->pabOpcodes         = g_abBs3Cg1Opcodes;
    pThis->fAdvanceMnemonic   = 1;

    /* Start the mode sub-test before we start reporting errors against it. */
    Bs3TestSub(pThis->pszModeShort);

    /* Allocate extended context structures. */
    cb = Bs3ExtCtxGetSize(&fFlags);
    pExtCtx = Bs3MemAlloc(BS3MEMKIND_TILED, cb * 3);
    if (!pExtCtx)
        return Bs3TestFailedF("Bs3MemAlloc(tiled,%#x)", cb * 3);
    pThis->pExtCtx        = pExtCtx;
    pThis->pResultExtCtx  = (PBS3EXTCTX)((uint8_t BS3_FAR *)pExtCtx + cb);
    pThis->pInitialExtCtx = (PBS3EXTCTX)((uint8_t BS3_FAR *)pExtCtx + cb + cb);

    Bs3ExtCtxInit(pThis->pExtCtx, cb, fFlags);
    Bs3ExtCtxInit(pThis->pResultExtCtx, cb, fFlags);
    Bs3ExtCtxInit(pThis->pInitialExtCtx, cb, fFlags);
    //Bs3TestPrintf("fCR0=%RX64 cbExtCtx=%#x method=%d\n", fFlags, cb, pExtCtx->enmMethod);

    /* Allocate guarded exectuable and data memory. */
    if (BS3_MODE_IS_PAGED(bMode))
    {
#if ARCH_BITS != 16
        pThis->pbCodePg = Bs3MemGuardedTestPageAlloc(enmMemKind);
        pThis->pbDataPg = Bs3MemGuardedTestPageAlloc(enmMemKind);
        if (!pThis->pbCodePg || !pThis->pbDataPg)
        {
            Bs3TestFailedF("Bs3MemGuardedTestPageAlloc(%d) failed", enmMemKind);
            Bs3MemPrintInfo();
            Bs3Shutdown();
            return Bs3TestFailedF("Bs3MemGuardedTestPageAlloc(%d) failed", enmMemKind);
        }
        if (   BS3_MODE_IS_64BIT_CODE(bMode)
            && (uintptr_t)pThis->pbDataPg >= _2G)
            return Bs3TestFailedF("pbDataPg=%p is above 2GB and not simple to address from 64-bit code", pThis->pbDataPg);
#else
        return Bs3TestFailed("WTF?! #1");
#endif
    }
    else
    {
        pThis->pbCodePg = Bs3MemAlloc(enmMemKind, X86_PAGE_SIZE);
        pThis->pbDataPg = Bs3MemAlloc(enmMemKind, X86_PAGE_SIZE);
        if (!pThis->pbCodePg || !pThis->pbDataPg)
        {
            Bs3MemPrintInfo();
            return Bs3TestFailedF("Bs3MemAlloc(%d,Pg) failed", enmMemKind);
        }
    }
    pThis->uCodePgFlat = Bs3SelPtrToFlat(pThis->pbCodePg);
    pThis->uDataPgFlat = Bs3SelPtrToFlat(pThis->pbDataPg);
#if ARCH_BITS == 16
    pThis->CodePgFar.sel = BS3_FP_SEG(pThis->pbCodePg);
    pThis->CodePgFar.off = BS3_FP_OFF(pThis->pbCodePg);
    pThis->CodePgRip     = BS3_FP_OFF(pThis->pbCodePg);
    pThis->DataPgFar.sel = BS3_FP_SEG(pThis->pbDataPg);
    pThis->DataPgFar.off = BS3_FP_OFF(pThis->pbDataPg);
#else
    if (BS3_MODE_IS_RM_OR_V86(bMode))
    {
        *(uint32_t *)&pThis->DataPgFar = Bs3SelFlatDataToRealMode(pThis->uDataPgFlat);
        ASMCompilerBarrier();
        pThis->CodePgFar.off = 0;
        pThis->CodePgFar.sel = pThis->uCodePgFlat >> 4;
        pThis->CodePgRip     = pThis->CodePgFar.off;
    }
    else if (BS3_MODE_IS_16BIT_CODE(bMode))
    {
        *(uint32_t *)&pThis->DataPgFar = Bs3SelFlatDataToProtFar16(pThis->uDataPgFlat);
        ASMCompilerBarrier();
        pThis->CodePgFar.sel = BS3_SEL_SPARE_00;
        pThis->CodePgFar.off = 0;
        pThis->CodePgRip     = 0;
    }
    else if (BS3_MODE_IS_32BIT_CODE(bMode))
    {
        *(uint32_t *)&pThis->DataPgFar = Bs3SelFlatDataToProtFar16(pThis->uDataPgFlat);
        ASMCompilerBarrier();
        pThis->CodePgFar.sel = 0;
        pThis->CodePgFar.off = 0;
        pThis->CodePgRip     = (uintptr_t)pThis->pbCodePg;
    }
    else
    {
        pThis->DataPgFar.off = 0;
        pThis->DataPgFar.sel = 0;
        pThis->CodePgFar.off = 0;
        pThis->CodePgFar.sel = 0;
        pThis->CodePgRip     = (uintptr_t)pThis->pbCodePg;
    }
#endif
    BS3CG1_DPRINTF(("pbDataPg=%p %04x:%04x  pbCodePg=%p %04x:%04x\n",
                    pThis->pbDataPg, pThis->DataPgFar.sel, pThis->DataPgFar.off,
                    pThis->pbCodePg, pThis->CodePgFar.sel, pThis->CodePgFar.off));

    /*
     * Create basic context for each target ring.
     *
     * In protected 16-bit code we need set up code selectors that can access
     * pbCodePg.
     *
     * In long mode we make sure the high 32-bits of GPRs (sans RSP) have some
     * bits set so we can check that the implicit clearing is tested.
     */
    Bs3RegCtxSaveEx(&pThis->aInitialCtxs[pThis->iFirstRing], bMode, 1024 * 3);
#if ARCH_BITS == 64
    pThis->aInitialCtxs[pThis->iFirstRing].rax.u |= UINT64_C(0x0101010100000000);
    pThis->aInitialCtxs[pThis->iFirstRing].rbx.u |= UINT64_C(0x0202020200000000);
    pThis->aInitialCtxs[pThis->iFirstRing].rcx.u |= UINT64_C(0x0303030300000000);
    pThis->aInitialCtxs[pThis->iFirstRing].rdx.u |= UINT64_C(0x0404040400000000);
    pThis->aInitialCtxs[pThis->iFirstRing].rbp.u |= UINT64_C(0x0505050500000000);
    pThis->aInitialCtxs[pThis->iFirstRing].rdi.u |= UINT64_C(0x0606060600000000);
    pThis->aInitialCtxs[pThis->iFirstRing].rsi.u |= UINT64_C(0x0707070700000000);
    pThis->aInitialCtxs[pThis->iFirstRing].r8.u  |= UINT64_C(0x0808080800000000);
    pThis->aInitialCtxs[pThis->iFirstRing].r9.u  |= UINT64_C(0x0909090900000000);
    pThis->aInitialCtxs[pThis->iFirstRing].r10.u |= UINT64_C(0x1010101000000000);
    pThis->aInitialCtxs[pThis->iFirstRing].r11.u |= UINT64_C(0x1111111100000000);
    pThis->aInitialCtxs[pThis->iFirstRing].r12.u |= UINT64_C(0x1212121200000000);
    pThis->aInitialCtxs[pThis->iFirstRing].r13.u |= UINT64_C(0x1313131300000000);
    pThis->aInitialCtxs[pThis->iFirstRing].r14.u |= UINT64_C(0x1414141400000000);
    pThis->aInitialCtxs[pThis->iFirstRing].r15.u |= UINT64_C(0x1515151500000000);
#endif

    if (BS3_MODE_IS_RM_OR_V86(bMode))
    {
        pThis->aInitialCtxs[pThis->iFirstRing].cs = pThis->CodePgFar.sel;
        BS3_ASSERT(pThis->iFirstRing + 1 == pThis->iEndRing);
    }
    else if (BS3_MODE_IS_16BIT_CODE(bMode))
    {
#if ARCH_BITS == 16
        uintptr_t const uFlatCodePgSeg = Bs3SelPtrToFlat(BS3_FP_MAKE(BS3_FP_SEG(pThis->pbCodePg), 0));
#else
        uintptr_t const uFlatCodePgSeg = (uintptr_t)pThis->pbCodePg;
#endif
        for (iRing = pThis->iFirstRing + 1; iRing < pThis->iEndRing; iRing++)
        {
            Bs3MemCpy(&pThis->aInitialCtxs[iRing], &pThis->aInitialCtxs[pThis->iFirstRing], sizeof(pThis->aInitialCtxs[iRing]));
            Bs3RegCtxConvertToRingX(&pThis->aInitialCtxs[iRing], iRing);
        }
        for (iRing = pThis->iFirstRing; iRing < pThis->iEndRing; iRing++)
        {
            pThis->aInitialCtxs[iRing].cs = BS3_SEL_SPARE_00 + iRing * 8 + iRing;
            Bs3SelSetup16BitCode(&Bs3GdteSpare00 + iRing, uFlatCodePgSeg, iRing);
        }
    }
    else
    {
        Bs3RegCtxSetRipCsFromCurPtr(&pThis->aInitialCtxs[pThis->iFirstRing], (FPFNBS3FAR)pThis->pbCodePg);
        for (iRing = pThis->iFirstRing + 1; iRing < pThis->iEndRing; iRing++)
        {
            Bs3MemCpy(&pThis->aInitialCtxs[iRing], &pThis->aInitialCtxs[pThis->iFirstRing], sizeof(pThis->aInitialCtxs[iRing]));
            Bs3RegCtxConvertToRingX(&pThis->aInitialCtxs[iRing], iRing);
        }
    }

    /*
     * Create an initial extended CPU context.
     */
    pExtCtx = pThis->pInitialExtCtx;
    if (   pExtCtx->enmMethod == BS3EXTCTXMETHOD_FXSAVE
        || pExtCtx->enmMethod == BS3EXTCTXMETHOD_XSAVE)
    {
        pExtCtx->Ctx.x87.FCW   = X86_FCW_MASK_ALL | X86_FCW_PC_64 | X86_FCW_RC_NEAREST;
        pExtCtx->Ctx.x87.FSW   = 0;
        pExtCtx->Ctx.x87.MXCSR      = X86_MXCSR_IM | X86_MXCSR_DM | X86_MXCSR_RC_NEAREST;
        pExtCtx->Ctx.x87.MXCSR_MASK = 0;
        for (i = 0; i < RT_ELEMENTS(pExtCtx->Ctx.x87.aRegs); i++)
        {
            pExtCtx->Ctx.x87.aRegs[i].au16[0] = i << 4;
            pExtCtx->Ctx.x87.aRegs[i].au16[1] = i << 4;
            pExtCtx->Ctx.x87.aRegs[i].au16[2] = i << 4;
            pExtCtx->Ctx.x87.aRegs[i].au16[3] = i << 4;
        }
        for (i = 0; i < RT_ELEMENTS(pExtCtx->Ctx.x87.aXMM); i++)
        {
            pExtCtx->Ctx.x87.aXMM[i].au16[0] = i | UINT16_C(0x8f00);
            pExtCtx->Ctx.x87.aXMM[i].au16[1] = i | UINT16_C(0x8e00);
            pExtCtx->Ctx.x87.aXMM[i].au16[2] = i | UINT16_C(0x8d00);
            pExtCtx->Ctx.x87.aXMM[i].au16[3] = i | UINT16_C(0x8c00);
            pExtCtx->Ctx.x87.aXMM[i].au16[4] = i | UINT16_C(0x8b00);
            pExtCtx->Ctx.x87.aXMM[i].au16[5] = i | UINT16_C(0x8a00);
            pExtCtx->Ctx.x87.aXMM[i].au16[6] = i | UINT16_C(0x8900);
            pExtCtx->Ctx.x87.aXMM[i].au16[7] = i | UINT16_C(0x8800);
        }
        if (pExtCtx->fXcr0Nominal & XSAVE_C_YMM)
            for (i = 0; i < RT_ELEMENTS(pExtCtx->Ctx.x.u.YmmHi.aYmmHi); i++)
            {
                pExtCtx->Ctx.x.u.YmmHi.aYmmHi[i].au16[0] = (i << 8) | (i << 12) | 0xff;
                pExtCtx->Ctx.x.u.YmmHi.aYmmHi[i].au16[1] = (i << 8) | (i << 12) | 0xfe;
                pExtCtx->Ctx.x.u.YmmHi.aYmmHi[i].au16[2] = (i << 8) | (i << 12) | 0xfd;
                pExtCtx->Ctx.x.u.YmmHi.aYmmHi[i].au16[3] = (i << 8) | (i << 12) | 0xfc;
                pExtCtx->Ctx.x.u.YmmHi.aYmmHi[i].au16[4] = (i << 8) | (i << 12) | 0xfb;
                pExtCtx->Ctx.x.u.YmmHi.aYmmHi[i].au16[5] = (i << 8) | (i << 12) | 0xfa;
                pExtCtx->Ctx.x.u.YmmHi.aYmmHi[i].au16[6] = (i << 8) | (i << 12) | 0xf9;
                pExtCtx->Ctx.x.u.YmmHi.aYmmHi[i].au16[7] = (i << 8) | (i << 12) | 0xf8;
            }

    }
    //else if (pExtCtx->enmMethod == BS3EXTCTXMETHOD_ANCIENT)
    else
        return Bs3TestFailedF("Unsupported extended CPU context method: %d", pExtCtx->enmMethod);

    return true;
}


static uint8_t BS3_NEAR_CODE BS3_CMN_NM(Bs3Cg1WorkerInner)(PBS3CG1STATE pThis)
{
    uint8_t  iRing;
    unsigned iInstr;

    /*
     * Test the instructions.
     */
    for (iInstr = 0; iInstr < g_cBs3Cg1Instructions;
         iInstr++,
         pThis->pchMnemonic += pThis->fAdvanceMnemonic * pThis->cchMnemonic,
         pThis->pabOperands += pThis->cOperands,
         pThis->pabOpcodes  += pThis->cbOpcodes)
    {
        uint8_t const   bTestXcptExpected  = BS3_MODE_IS_PAGED(pThis->bMode) ? X86_XCPT_PF : X86_XCPT_UD;
        bool            fOuterInvalidInstr = false;
        unsigned        iCpuSetup;

        /*
         * Expand the instruction information into the state.
         * Note! 16-bit will switch to a two level test header lookup once we exceed 64KB.
         */
        PCBS3CG1INSTR pInstr = &g_aBs3Cg1Instructions[iInstr];
        pThis->iInstr                   = iInstr;
        pThis->pTestHdr                 = (PCBS3CG1TESTHDR)&g_abBs3Cg1Tests[pInstr->offTests];
        pThis->fFlags                   = pInstr->fFlags;
        pThis->enmEncoding              = (BS3CG1ENC)pInstr->enmEncoding;
        pThis->enmEncodingNonInvalid    = (BS3CG1ENC)pInstr->enmEncoding;
        pThis->enmCpuTest               = (BS3CG1CPU)pInstr->enmCpuTest;
        pThis->enmPrefixKind            = (BS3CG1PFXKIND)pInstr->enmPrefixKind;
        pThis->enmXcptType              = (BS3CG1XCPTTYPE)pInstr->enmXcptType;
        pThis->cchMnemonic              = pInstr->cchMnemonic;
        if (pThis->fAdvanceMnemonic)
            Bs3TestSubSubF("%.*s", pThis->cchMnemonic, pThis->pchMnemonic);
        pThis->fAdvanceMnemonic         = pInstr->fAdvanceMnemonic;
        pThis->uOpcodeMap               = pInstr->uOpcodeMap;
        pThis->cOperands                = pInstr->cOperands;
        pThis->cbOpcodes                = pInstr->cbOpcodes;
        switch (pThis->cOperands)
        {
            case 4: pThis->aenmOperands[3] = (BS3CG1OP)pThis->pabOperands[3];
            case 3: pThis->aenmOperands[2] = (BS3CG1OP)pThis->pabOperands[2];
            case 2: pThis->aenmOperands[1] = (BS3CG1OP)pThis->pabOperands[1];
            case 1: pThis->aenmOperands[0] = (BS3CG1OP)pThis->pabOperands[0];
        }
        switch (pThis->cbOpcodes)
        {
            case 4: pThis->abOpcodes[3] = pThis->pabOpcodes[3];
            case 3: pThis->abOpcodes[2] = pThis->pabOpcodes[2];
            case 2: pThis->abOpcodes[1] = pThis->pabOpcodes[1];
            case 1: pThis->abOpcodes[0] = pThis->pabOpcodes[0];
        }

        /*
         * Check if the CPU supports the instruction.
         */
        pThis->fCpuSetupFirstResult = Bs3Cg1CpuSetupFirst(pThis);
        if (   !pThis->fCpuSetupFirstResult
            || (pThis->fFlags & (BS3CG1INSTR_F_UNUSED | BS3CG1INSTR_F_INVALID)))
            fOuterInvalidInstr = true;

        /* Switch the encoder for some of the invalid instructions on non-intel CPUs. */
        if (   (pThis->fFlags & BS3CG1INSTR_F_INTEL_DECODES_INVALID)
            && pThis->bCpuVendor != BS3CPUVENDOR_INTEL
            && (   (pThis->fFlags & (BS3CG1INSTR_F_UNUSED | BS3CG1INSTR_F_INVALID))
                || (BS3CG1_IS_64BIT_TARGET(pThis) && (pThis->fFlags & BS3CG1INSTR_F_INVALID_64BIT))
                || fOuterInvalidInstr ) )
            pThis->enmEncoding = Bs3Cg1CalcNoneIntelInvalidEncoding(pThis->enmEncoding);

        for (iCpuSetup = 0;; iCpuSetup++)
        {
            unsigned iEncoding;
            unsigned iEncodingNext;

            /*
             * Prep the operands and encoding handling.
             */
            Bs3Cg1SetOpSizes(pThis, pThis->bMode);
            if (!Bs3Cg1EncodePrep(pThis))
                break;

            /*
             * Encode the instruction in various ways and check out the test values.
             */
            for (iEncoding = 0;; iEncoding = iEncodingNext)
            {
                /*
                 * Encode the next instruction variation.
                 */
                pThis->fInvalidEncoding = fOuterInvalidInstr;
                iEncodingNext = Bs3Cg1EncodeNext(pThis, iEncoding);
                if (iEncodingNext <= iEncoding)
                    break;
                BS3CG1_DPRINTF(("\ndbg: Encoding #%u: cbCurInst=%u: %.*Rhxs  fInvalidEncoding=%d\n",
                                iEncoding, pThis->cbCurInstr, pThis->cbCurInstr, pThis->abCurInstr, pThis->fInvalidEncoding));

                /*
                 * Do the rings.
                 */
                for (iRing = pThis->iFirstRing + pThis->fSameRingNotOkay; iRing < pThis->iEndRing; iRing++)
                {
                    PCBS3CG1TESTHDR pHdr;

                    pThis->uCpl = iRing;
                    BS3CG1_DPRINTF(("dbg:  Ring %u\n", iRing));

                    /*
                     * Do the tests one by one.
                     */
                    pHdr = pThis->pTestHdr;
                    for (pThis->iTest = 0;; pThis->iTest++)
                    {
                        if (Bs3Cg1RunSelector(pThis, pHdr))
                        {
                            /* Okay, set up the execution context. */
                            unsigned         offCode;
                            uint8_t BS3_FAR *pbCode;

                            Bs3MemCpy(&pThis->Ctx, &pThis->aInitialCtxs[iRing], sizeof(pThis->Ctx));
                            if (pThis->fWorkExtCtx)
                                Bs3ExtCtxCopy(pThis->pExtCtx, pThis->pInitialExtCtx);
                            if (BS3_MODE_IS_PAGED(pThis->bMode))
                            {
                                offCode = X86_PAGE_SIZE - pThis->cbCurInstr;
                                pbCode = &pThis->pbCodePg[offCode];
                                //if (iEncoding > 0) { pbCode[-1] = 0xf4; offCode--; }
                            }
                            else
                            {
                                pbCode = pThis->pbCodePg;
                                pbCode[pThis->cbCurInstr]     = 0x0f; /* UD2 */
                                pbCode[pThis->cbCurInstr + 1] = 0x0b;
                                offCode = 0;
                            }
                            pThis->Ctx.rip.u = pThis->CodePgRip + offCode;
                            Bs3MemCpy(pbCode, pThis->abCurInstr, pThis->cbCurInstr);

                            if (Bs3Cg1RunContextModifier(pThis, &pThis->Ctx, pHdr, pHdr->cbSelector, pHdr->cbInput, NULL, pbCode))
                            {
                                /* Run the instruction. */
                                BS3CG1_DPRINTF(("dbg:  Running test #%u\n", pThis->iTest));
                                //Bs3RegCtxPrint(&pThis->Ctx);
                                if (pThis->fWorkExtCtx)
                                    Bs3ExtCtxRestore(pThis->pExtCtx);
                                Bs3TrapSetJmpAndRestore(&pThis->Ctx, &pThis->TrapFrame);
                                if (pThis->fWorkExtCtx)
                                    Bs3ExtCtxSave(pThis->pResultExtCtx);
                                BS3CG1_DPRINTF(("dbg:  bXcpt=%#x rip=%RX64 -> %RX64\n",
                                                pThis->TrapFrame.bXcpt, pThis->Ctx.rip.u, pThis->TrapFrame.Ctx.rip.u));

                                /*
                                 * Apply the output modification program to the context.
                                 */
                                pThis->Ctx.rflags.u32 &= ~X86_EFL_RF;
                                pThis->Ctx.rflags.u32 |= pThis->TrapFrame.Ctx.rflags.u32 & X86_EFL_RF;
                                pThis->bValueXcpt      = UINT8_MAX; //???
                                if (   pThis->fInvalidEncoding
                                    || pThis->bAlignmentXcpt != UINT8_MAX
                                    || pThis->bValueXcpt     != UINT8_MAX
                                    || Bs3Cg1RunContextModifier(pThis, &pThis->Ctx, pHdr,
                                                                pHdr->cbSelector + pHdr->cbInput, pHdr->cbOutput,
                                                                &pThis->TrapFrame.Ctx, NULL /*pbCode*/))
                                    Bs3Cg1CheckResult(pThis, bTestXcptExpected, false /*fInvalidEncodingPgFault*/, iEncoding);
                                else
                                {
                                    Bs3TestPrintf("Bs3Cg1RunContextModifier(out): iEncoding=%u iTest=%RU32 iInstr=%u %.*s\n",
                                                  iEncoding, pThis->iTest, pThis->iInstr, pThis->cchMnemonic, pThis->pchMnemonic);
                                    ASMHalt();
                                }

                                /*
                                 * If this is an invalid encoding or instruction, check that we
                                 * get a page fault when shortening it by one byte.
                                 * (Since we didn't execute the output context modifier, we don't
                                 * need to re-initialize the start context.)
                                 */
                                if (   pThis->fInvalidEncoding
                                    && BS3_MODE_IS_PAGED(pThis->bMode)
                                    && pThis->cbCurInstr)
                                {
                                    pbCode  += 1;
                                    offCode += 1;
                                    pThis->Ctx.rip.u = pThis->CodePgRip + offCode;
                                    Bs3MemCpy(pbCode, pThis->abCurInstr, pThis->cbCurInstr - 1);

                                    /* Run the instruction. */
                                    BS3CG1_DPRINTF(("dbg:  Running test #%u (cut short #PF)\n", pThis->iTest));
                                    //Bs3RegCtxPrint(&pThis->Ctx);
                                    if (pThis->fWorkExtCtx)
                                        Bs3ExtCtxRestore(pThis->pExtCtx);
                                    Bs3TrapSetJmpAndRestore(&pThis->Ctx, &pThis->TrapFrame);
                                    if (pThis->fWorkExtCtx)
                                        Bs3ExtCtxSave(pThis->pResultExtCtx);
                                    BS3CG1_DPRINTF(("dbg:  bXcpt=%#x rip=%RX64 -> %RX64 (cut short #PF)\n",
                                                    pThis->TrapFrame.bXcpt, pThis->Ctx.rip.u, pThis->TrapFrame.Ctx.rip.u));

                                    /* Check it */
                                    pThis->Ctx.rflags.u32 &= ~X86_EFL_RF;
                                    pThis->Ctx.rflags.u32 |= pThis->TrapFrame.Ctx.rflags.u32 & X86_EFL_RF;
                                    Bs3Cg1CheckResult(pThis, X86_XCPT_PF, true /*fInvalidEncodingPgFault*/, iEncoding);
                                }
                            }
                            else
                            {
                                Bs3TestPrintf("Bs3Cg1RunContextModifier(in): iEncoding=%u iTest=%u iInstr=%RU32 %.*s\n",
                                              iEncoding, pThis->iTest, pThis->iInstr, pThis->cchMnemonic, pThis->pchMnemonic);
                                ASMHalt();
                            }
                        }
                        else
                            BS3CG1_DPRINTF(("dbg:  Skipping #%u\n", pThis->iTest));

                        /* advance */
                        if (pHdr->fLast)
                        {
                            BS3CG1_DPRINTF(("dbg:  Last\n\n"));
                            break;
                        }
                        pHdr = (PCBS3CG1TESTHDR)((uint8_t BS3_FAR *)(pHdr + 1) + pHdr->cbInput + pHdr->cbOutput + pHdr->cbSelector);
                    }
                }
            }

            /*
             * Clean up (segment registers, etc) and get the next CPU config.
             */
            Bs3Cg1EncodeCleanup(pThis);
            if (!Bs3Cg1CpuSetupNext(pThis, iCpuSetup, &fOuterInvalidInstr))
                break;
            if (pThis->fFlags & (BS3CG1INSTR_F_UNUSED | BS3CG1INSTR_F_INVALID))
                fOuterInvalidInstr = true;
        }
    }

    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(Bs3Cg1Worker)(uint8_t bMode)
{
    uint8_t     bRet = 1;
    BS3CG1STATE This;

#if 0
    /* (for debugging) */
    if (bMode != BS3_MODE_LM64)
        return BS3TESTDOMODE_SKIPPED;
#endif

    if (BS3_CMN_NM(Bs3Cg1Init)(&This, bMode))
        bRet = BS3_CMN_NM(Bs3Cg1WorkerInner)(&This);
    Bs3Cg1Destroy(&This);

#if 0
    /* (for debugging) */
    if (bMode == BS3_MODE_PPV86)
    {
        Bs3TestTerm();
        Bs3Shutdown();
    }
#endif
    return bRet;
}

