/* $Id: cidet.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU Instruction Decoding & Execution Tests - C/C++ Header.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_cpu_cidet_h
#define VBOX_INCLUDED_SRC_cpu_cidet_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/x86.h>


/** @name CIDET - Operand flags.
 * @{ */
#define CIDET_OF_FIXED_MASK     UINT32_C(0x0000001f) /**< Fixed register/whatever mask. */

#define CIDET_OF_Z_SHIFT        8                    /**< Size shift. */
#define CIDET_OF_Z_MASK         UINT32_C(0x00000f00) /**< Size mask. */
#define CIDET_OF_Z_NONE         UINT32_C(0x00000000) /**< Unused zero value. */
#define CIDET_OF_Z_BYTE         UINT32_C(0x00000100) /**< Byte size. */
#define CIDET_OF_Z_WORD         UINT32_C(0x00000200) /**< Word (2 bytes) size. */
#define CIDET_OF_Z_DWORD        UINT32_C(0x00000300) /**< Double word (4 bytes) size. */
#define CIDET_OF_Z_QWORD        UINT32_C(0x00000400) /**< Quad word (8 bytes) size. */
#define CIDET_OF_Z_TBYTE        UINT32_C(0x00000500) /**< Ten byte (10 bytes) size - aka TWORD. */
#define CIDET_OF_Z_OWORD        UINT32_C(0x00000600) /**< Octa word (16 bytes) size - aka DQWORD. */
#define CIDET_OF_Z_YWORD        UINT32_C(0x00000700) /**< Yxx sized, i.e. 32 bytes. */
#define CIDET_OF_Z_ZWORD        UINT32_C(0x00000800) /**< Zxx sized, i.e. 64 bytes. */
#define CIDET_OF_Z_VAR_WDQ      UINT32_C(0x00000900) /**< Variable size depending on size prefix (2, 4, or 8 bytes). */
#define CIDET_OF_Z_SPECIAL      UINT32_C(0x00000f00) /**< Special size, see instruction flags or smth. */

#define CIDET_OF_K_MASK         UINT32_C(0x0000f000) /**< Kind of operand. */
#define CIDET_OF_K_NONE         UINT32_C(0x00000000) /**< Unused zero value. */
#define CIDET_OF_K_GPR          UINT32_C(0x00001000) /**< General purpose register. Includes memory when used with CIDET_OF_M_RM. */
#define CIDET_OF_K_SREG         UINT32_C(0x00002000) /**< Segment register. */
#define CIDET_OF_K_CR           UINT32_C(0x00003000) /**< Control register. */
#define CIDET_OF_K_SSE          UINT32_C(0x00004000) /**< SSE register. */
#define CIDET_OF_K_AVX          UINT32_C(0x00005000) /**< AVX register. */
#define CIDET_OF_K_AVX512       UINT32_C(0x00006000) /**< AVX-512 register. */
#define CIDET_OF_K_AVXFUTURE    UINT32_C(0x00007000) /**< Reserved for future AVX register set. */
#define CIDET_OF_K_VRX_TST_MASK UINT32_C(0x0000c000) /**< Used for testing for VRX register kind, see CIDET_OF_K_IS_VRX. */
#define CIDET_OF_K_VRX_TST_RES  UINT32_C(0x00004000) /**< Used for testing for VRX register kind, see CIDET_OF_K_IS_VRX. */
#define CIDET_OF_K_FPU          UINT32_C(0x00008000) /**< FPU register. */
#define CIDET_OF_K_MMX          UINT32_C(0x00009000) /**< MMX register. */
#define CIDET_OF_K_TEST         UINT32_C(0x0000a000) /**< Test register. */
#define CIDET_OF_K_IMM          UINT32_C(0x0000d000) /**< Immediate. */
#define CIDET_OF_K_MEM          UINT32_C(0x0000e000) /**< Memory. */
#define CIDET_OF_K_SPECIAL      UINT32_C(0x0000f000) /**< Special. */
/** Check if @a a_fOp is a general purpose register. */
#define CIDET_OF_K_IS_GPR(a_fOp)    ( ((a_fOp) & CIDET_OF_K_MASK) == CIDET_OF_K_GPR )
/** Check if @a a_fOp is a XMM (SSE), YMM (AVX), ZMM (AVX-512) or similar register. */
#define CIDET_OF_K_IS_VRX(a_fOp)    ( ((a_fOp) & CIDET_OF_K_VRX_TST_MASK) == CIDET_OF_K_VRX_TST_RES )
/** Check if @a a_fOp1 and @a a_fOp2 specify the same kind of register,
 * treating SSE, AVX, AVX-512 and AVX-future as the same kind and ignoring the
 * special register kind. */
#define CIDET_OF_K_IS_SAME(a_fOp1, a_fOp2) \
    (   ((a_fOp1) & CIDET_OF_K_MASK) == ((a_fOp2) & CIDET_OF_K_MASK) \
     ?  ((a_fOp1) & CIDET_OF_K_MASK) != CIDET_OF_K_SPECIAL \
     :  (CIDET_OF_K_IS_VRX(a_fOp1) && CIDET_OF_K_IS_VRX(a_fOp2)) )

#define CIDET_OF_M_RM_ONLY_R    UINT32_C(0x00010000)
#define CIDET_OF_M_RM_ONLY_M    UINT32_C(0x00020000)
#define CIDET_OF_M_RM           (CIDET_OF_M_RM_ONLY_R | CIDET_OF_M_RM_ONLY_M)
#define CIDET_OF_M_REG          UINT32_C(0x00040000)

#define CIDET_OF_A_R            UINT32_C(0x00080000) /**< Read access. */
#define CIDET_OF_A_W            UINT32_C(0x00100000) /**< Write access. */
#define CIDET_OF_A_RW           UINT32_C(0x00180000) /**< Read & write access. */

/** The operand defaults to 64-bit width in 64-bit mode, making 32-bit width
 * inaccessible. */
#define CIDET_OF_DEFAULT_64BIT  UINT32_C(0x40000000)
/** Operand always uses the ES segment for memory accesses. */
#define CIDET_OF_ALWAYS_SEG_ES  UINT32_C(0x80000000)
/** @} */


/** @name CIDET - Instruction flags.
 * @{ */
#define CIDET_IF_MODRM          RT_BIT_64(0)    /**< ModR/M encoded. */
#define CIDET_IF_PRIVILEGED     RT_BIT_64(1)    /**< Privileged. */
/** @} */


/**
 * Callback function for setting up the input and expected output CPU contexts.
 *
 * @returns VBox status code.
 * @retval  VINF_EOF when static test data wraps (first entry is returned).
 * @retval  VERR_NO_DATA if @a fInvalid is set and there are no invalid operand
 *          values for this instruction.
 * @retval  VERR_NOT_SUPPORTED if something in the setup prevents us from
 *          comming up with working set of inputs and outputs.
 *
 * @param   pThis           The core CIDET state structure.  The InCtx
 *                          and ExpectedCtx members will be modified.
 * @param   fInvalid        When set, get the next invalid operands that will
 *                          cause exceptions/faults.
 */
typedef DECLCALLBACKTYPE(int, FNCIDETSETUPINOUT,(struct CIDETCORE *pThis, bool fInvalid));
/** Pointer to a FNCIDETSETUPINOUT function. */
typedef FNCIDETSETUPINOUT *PFNCIDETSETUPINOUT;


/**
 * Instruction test descriptor.
 */
typedef struct CIDETINSTR
{
    /** The mnemonic (kind of). */
    const char         *pszMnemonic;
    /** Setup input and outputs. */
    PFNCIDETSETUPINOUT  pfnSetupInOut;
    /** Number of opcode bytes. */
    uint8_t             cbOpcode;
    /** Opcode byte(s). */
    uint8_t             abOpcode[3];
    /** Mandatory prefix (zero if not applicable). */
    uint8_t             bMandatoryPrefix;
    /** Number of operands. */
    uint8_t             cOperands;
    /** Operand flags. */
    uint32_t            afOperands[4];
    /** Flags. */
    uint64_t            fFlags;
} CIDETINSTR;
/** Pointer to an instruction test descriptor. */
typedef CIDETINSTR const *PCCIDETINSTR;


/**
 * CPU Context with a few extra bits for expectations and results.
 */
typedef struct CIDETCPUCTX
{
    uint64_t            rip;
    uint64_t            rfl;
    uint64_t            aGRegs[16];
    uint16_t            aSRegs[6];

#ifndef CIDET_REDUCED_CTX
    uint16_t            tr;
    uint16_t            ldtr;
    uint64_t            cr0;
#else
    uint16_t            au16Padding[2];
#endif
    uint64_t            cr2;
#ifndef CIDET_REDUCED_CTX
    uint64_t            cr3;
    uint64_t            cr4;
    uint64_t            cr8;
    uint64_t            dr0;
    uint64_t            dr1;
    uint64_t            dr2;
    uint64_t            dr3;
    uint64_t            dr6;
    uint64_t            dr7;
#endif

    uint64_t            uErr;           /**< Exception error code.  UINT64_MAX if not applicable.  (Not for input context.) */
    uint32_t            uXcpt;          /**< Exception number.  UINT32_MAX if no exception.  (Not for input context.) */

    uint32_t            fIgnoredRFlags; /**< Only for expected result. */
    bool                fTrickyStack;   /**< Set if the stack might be bad.  May come at the cost of accurate flags (32-bit). */
} CIDETCPUCTX;
typedef CIDETCPUCTX *PCIDETCPUCTX;
typedef CIDETCPUCTX const *PCCIDETCPUCTX;

/** Number of bytes of CIDETCPUCTX that can be compared quickly using memcmp.
 * Anything following these bytes are not relevant to the compare.  */
#define CIDETCPUCTX_COMPARE_SIZE    RT_UOFFSETOF(CIDETCPUCTX, fIgnoredRFlags)


/** @name CPU mode + bits + environment.
 * @{ */
#define CIDETMODE_BIT_MASK      UINT8_C(0x0e) /**< The instruction bit count. Results in byte size when masked. */
#define CIDETMODE_BIT_16        UINT8_C(0x02) /**< 16-bit instructions. */
#define CIDETMODE_BIT_32        UINT8_C(0x04) /**< 32-bit instructions. */
#define CIDETMODE_BIT_64        UINT8_C(0x08) /**< 64-bit instructions. */
#define CIDETMODE_MODE_MASK     UINT8_C(0x70) /**< CPU mode mask. */
#define CIDETMODE_MODE_RM       UINT8_C(0x00) /**< Real mode. */
#define CIDETMODE_MODE_PE       UINT8_C(0x10) /**< Protected mode without paging. */
#define CIDETMODE_MODE_PP       UINT8_C(0x20) /**< Paged protected mode. */
#define CIDETMODE_MODE_PAE      UINT8_C(0x30) /**< PAE protected mode (paged). */
#define CIDETMODE_MODE_LM       UINT8_C(0x40) /**< Long mode (paged). */
#define CIDETMODE_ENV_MASK      UINT8_C(0x81) /**< Execution environment. */
#define CIDETMODE_ENV_NORMAL    UINT8_C(0x01) /**< Normal environment. */
#define CIDETMODE_ENV_V86       UINT8_C(0x80) /**< V8086 environment. */
#define CIDETMODE_RM            (CIDETMODE_MODE_RM  | CIDETMODE_BIT_16 | CIDETMODE_ENV_NORMAL)
#define CIDETMODE_PE_16         (CIDETMODE_MODE_PE  | CIDETMODE_BIT_16 | CIDETMODE_ENV_NORMAL)
#define CIDETMODE_PE_32         (CIDETMODE_MODE_PE  | CIDETMODE_BIT_32 | CIDETMODE_ENV_NORMAL)
#define CIDETMODE_PE_V86        (CIDETMODE_MODE_PE  | CIDETMODE_BIT_16 | CIDETMODE_ENV_V86)
#define CIDETMODE_PP_16         (CIDETMODE_MODE_PP  | CIDETMODE_BIT_16 | CIDETMODE_ENV_NORMAL)
#define CIDETMODE_PP_32         (CIDETMODE_MODE_PP  | CIDETMODE_BIT_32 | CIDETMODE_ENV_NORMAL)
#define CIDETMODE_PP_V86        (CIDETMODE_MODE_PP  | CIDETMODE_BIT_16 | CIDETMODE_ENV_V86)
#define CIDETMODE_PAE_16        (CIDETMODE_MODE_PAE | CIDETMODE_BIT_16 | CIDETMODE_ENV_NORMAL)
#define CIDETMODE_PAE_32        (CIDETMODE_MODE_PAE | CIDETMODE_BIT_32 | CIDETMODE_ENV_NORMAL)
#define CIDETMODE_PAE_V86       (CIDETMODE_MODE_PAE | CIDETMODE_BIT_16 | CIDETMODE_ENV_V86)
#define CIDETMODE_LM_16         (CIDETMODE_MODE_LM  | CIDETMODE_BIT_16 | CIDETMODE_ENV_NORMAL)
#define CIDETMODE_LM_32         (CIDETMODE_MODE_LM  | CIDETMODE_BIT_32 | CIDETMODE_ENV_NORMAL)
#define CIDETMODE_LM_64         (CIDETMODE_MODE_LM  | CIDETMODE_BIT_64 | CIDETMODE_ENV_NORMAL)
/** Test if @a a_bMode is a 16-bit mode. */
#define CIDETMODE_IS_16BIT(a_bMode) ( ((a_bMode) & CIDETMODE_BIT_MASK) == CIDETMODE_BIT_16 )
/** Test if @a a_bMode is a 32-bit mode. */
#define CIDETMODE_IS_32BIT(a_bMode) ( ((a_bMode) & CIDETMODE_BIT_MASK) == CIDETMODE_BIT_32 )
/** Test if @a a_bMode is a 64-bit mode. */
#define CIDETMODE_IS_64BIT(a_bMode) ( ((a_bMode) & CIDETMODE_BIT_MASK) == CIDETMODE_BIT_64 )
/** Get the instruction bit count. */
#define CIDETMODE_GET_BIT_COUNT(a_bMode) ( CIDETMODE_GET_BYTE_COUNT(a_bMode) * 8 )
/** Get the instruction byte count. */
#define CIDETMODE_GET_BYTE_COUNT(a_bMode) ( (a_bMode) & CIDETMODE_BIT_MASK )
/** Test if @a a_bMode long mode. */
#define CIDETMODE_IS_LM(a_bMode) ( ((a_bMode) & CIDETMODE_MODE_MASK) == CIDETMODE_MODE_LM )
/** Test if @a a_bMode some kind of protected mode. */
#define CIDETMODE_IS_PROT(a_bMode) ( ((a_bMode) & CIDETMODE_MODE_MASK) >= CIDETMODE_MODE_PE )

/** @} */


/** @name Test Configuration Flags.
 * @{ */
#define CIDET_TESTCFG_SEG_PRF_CS            UINT64_C(0x0000000000000001)
#define CIDET_TESTCFG_SEG_PRF_SS            UINT64_C(0x0000000000000002)
#define CIDET_TESTCFG_SEG_PRF_DS            UINT64_C(0x0000000000000004)
#define CIDET_TESTCFG_SEG_PRF_ES            UINT64_C(0x0000000000000008)
#define CIDET_TESTCFG_SEG_PRF_FS            UINT64_C(0x0000000000000010)
#define CIDET_TESTCFG_SEG_PRF_GS            UINT64_C(0x0000000000000020)
#define CIDET_TESTCFG_SEG_PRF_MASK          UINT64_C(0x000000000000003f)
/** @} */

/** */
typedef enum CIDETREG
{
    kCidetReg_Gpr_Invalid = 0,

    kCidetReg_Gpr_al,
    kCidetReg_Gpr_cl,
    kCidetReg_Gpr_dl,
    kCidetReg_Gpr_bl,
    kCidetReg_Gpr_spl,
    kCidetReg_Gpr_bpl,
    kCidetReg_Gpr_sil,
    kCidetReg_Gpr_dil,
    kCidetReg_Gpr_r8b,
    kCidetReg_Gpr_r9b,
    kCidetReg_Gpr_r10b,
    kCidetReg_Gpr_r11b,
    kCidetReg_Gpr_r12b,
    kCidetReg_Gpr_r13b,
    kCidetReg_Gpr_r14b,
    kCidetReg_Gpr_r15b,
    kCidetReg_Gpr_ah,
    kCidetReg_Gpr_ch,
    kCidetReg_Gpr_dh,
    kCidetReg_Gpr_bh,
#define kCidetReg_Gpr_Byte_First                    kCidetReg_Gpr_al
#define kCidetReg_Gpr_Byte_First_Upper              kCidetReg_Gpr_ah
#define kCidetReg_Gpr_Byte_Last                     kCidetReg_Gpr_bh

    kCidetReg_Gpr_ax,
    kCidetReg_Gpr_cx,
    kCidetReg_Gpr_dx,
    kCidetReg_Gpr_bx,
    kCidetReg_Gpr_sp,
    kCidetReg_Gpr_bp,
    kCidetReg_Gpr_si,
    kCidetReg_Gpr_di,
    kCidetReg_Gpr_r8w,
    kCidetReg_Gpr_r9w,
    kCidetReg_Gpr_r10w,
    kCidetReg_Gpr_r11w,
    kCidetReg_Gpr_r12w,
    kCidetReg_Gpr_r13w,
    kCidetReg_Gpr_r14w,
    kCidetReg_Gpr_r15w,
#define kCidetReg_Gpr_Word_First                    kCidetReg_Gpr_ax
#define kCidetReg_Gpr_Word_Last                     kCidetReg_Gpr_r15w

    kCidetReg_Gpr_eax,
    kCidetReg_Gpr_ecx,
    kCidetReg_Gpr_edx,
    kCidetReg_Gpr_ebx,
    kCidetReg_Gpr_esp,
    kCidetReg_Gpr_ebp,
    kCidetReg_Gpr_esi,
    kCidetReg_Gpr_edi,
    kCidetReg_Gpr_r8d,
    kCidetReg_Gpr_r9d,
    kCidetReg_Gpr_r10d,
    kCidetReg_Gpr_r11d,
    kCidetReg_Gpr_r12d,
    kCidetReg_Gpr_r13d,
    kCidetReg_Gpr_r14d,
    kCidetReg_Gpr_r15d,
#define kCidetReg_Gpr_DWord_First                    kCidetReg_Gpr_eax
#define kCidetReg_Gpr_DWord_Last                     kCidetReg_Gpr_r15d

    kCidetReg_Gpr_rax,
    kCidetReg_Gpr_rcx,
    kCidetReg_Gpr_rdx,
    kCidetReg_Gpr_rbx,
    kCidetReg_Gpr_rsp,
    kCidetReg_Gpr_rbp,
    kCidetReg_Gpr_rsi,
    kCidetReg_Gpr_rdi,
    kCidetReg_Gpr_r8,
    kCidetReg_Gpr_r9,
    kCidetReg_Gpr_r10,
    kCidetReg_Gpr_r11,
    kCidetReg_Gpr_r12,
    kCidetReg_Gpr_r13,
    kCidetReg_Gpr_r14,
    kCidetReg_Gpr_r15,
#define kCidetReg_Gpr_QWord_First                    kCidetReg_Gpr_rax
#define kCidetReg_Gpr_QWord_Last                     kCidetReg_Gpr_r15

    kCidetReg_Seg_es,
    kCidetReg_Seg_cs,
    kCidetReg_Seg_ss,
    kCidetReg_Seg_ds,
    kCidetReg_Seg_fs,
    kCidetReg_Seg_gs,
    kCidetReg_Seg_Inv6,
    kCidetReg_Seg_Inv7,
#define kCidetReg_Seg_First                         kCidetReg_Seg_es
#define kCidetReg_Seg_Last                          kCidetReg_Seg_gs
#define kCidetReg_Seg_Last_Inv                      kCidetReg_Seg_Inv7

    kCidetReg_Misc_ip,
    kCidetReg_Misc_eip,
    kCidetReg_Misc_rip,
    kCidetReg_Misc_flags,
    kCidetReg_Misc_eflags,
    kCidetReg_Misc_rflags,
    kCidetReg_Misc_tr,
    kCidetReg_Misc_ldtr,
    kCidetReg_Misc_gdtr,
    kCidetReg_Misc_idtr,

    kCidetReg_Ctrl_cr0,
    kCidetReg_Ctrl_cr1,
    kCidetReg_Ctrl_cr2,
    kCidetReg_Ctrl_cr3,
    kCidetReg_Ctrl_cr4,
    kCidetReg_Ctrl_cr5,
    kCidetReg_Ctrl_cr6,
    kCidetReg_Ctrl_cr7,
    kCidetReg_Ctrl_cr8,
    kCidetReg_Ctrl_cr9,
    kCidetReg_Ctrl_cr10,
    kCidetReg_Ctrl_cr11,
    kCidetReg_Ctrl_cr12,
    kCidetReg_Ctrl_cr13,
    kCidetReg_Ctrl_cr14,
    kCidetReg_Ctrl_cr15,
#define kCidetReg_Ctrl_First                        kCidetReg_Ctrl_cr0
#define kCidetReg_Ctrl_Last                         kCidetReg_Ctrl_cr15
#define CIDETREG_CTRL_IS_VALID(a_iReg) (   (a_iReg) == kCidetReg_Ctrl_cr0 \
                                        && (a_iReg) == kCidetReg_Ctrl_cr2 \
                                        && (a_iReg) == kCidetReg_Ctrl_cr3 \
                                        && (a_iReg) == kCidetReg_Ctrl_cr4 \
                                        && (a_iReg) == kCidetReg_Ctrl_cr8 )

    kCidetReg_Dbg_dr0,
    kCidetReg_Dbg_dr1,
    kCidetReg_Dbg_dr2,
    kCidetReg_Dbg_dr3,
    kCidetReg_Dbg_dr4,
    kCidetReg_Dbg_dr5,
    kCidetReg_Dbg_dr6,
    kCidetReg_Dbg_dr7,
    kCidetReg_Dbg_dr8,
    kCidetReg_Dbg_dr9,
    kCidetReg_Dbg_dr10,
    kCidetReg_Dbg_dr11,
    kCidetReg_Dbg_dr12,
    kCidetReg_Dbg_dr13,
    kCidetReg_Dbg_dr14,
    kCidetReg_Dbg_dr15,
#define kCidetReg_Dbg_First                         kCidetReg_Dbg_dr0
#define kCidetReg_Dbg_Last                          kCidetReg_Dbg_dr15
#define CIDETREG_DBG_IS_VALID(a_iReg) ((a_iReg) < kCidetReg_Dbg_dr8 && (a_iReg) >= kCidetReg_Dbg_First)

    kCidetReg_Test_tr0,
    kCidetReg_Test_tr1,
    kCidetReg_Test_tr2,
    kCidetReg_Test_tr3,
    kCidetReg_Test_tr4,
    kCidetReg_Test_tr5,
    kCidetReg_Test_tr6,
    kCidetReg_Test_tr7,
    kCidetReg_Test_tr8,
    kCidetReg_Test_tr9,
    kCidetReg_Test_tr10,
    kCidetReg_Test_tr11,
    kCidetReg_Test_tr12,
    kCidetReg_Test_tr13,
    kCidetReg_Test_tr14,
    kCidetReg_Test_tr15,
#define kCidetReg_Test_First                        kCidetReg_Test_tr0
#define kCidetReg_Test_Last                         kCidetReg_Test_tr15

    kCidetReg_Fpu_st0,
    kCidetReg_Fpu_st1,
    kCidetReg_Fpu_st2,
    kCidetReg_Fpu_st3,
    kCidetReg_Fpu_st4,
    kCidetReg_Fpu_st5,
    kCidetReg_Fpu_st6,
    kCidetReg_Fpu_st7,
#define kCidetReg_Fpu_First                         kCidetReg_Mmx_st0
#define kCidetReg_Fpu_Last                          kCidetReg_Mmx_st7

    kCidetReg_FpuMisc_cs,
    kCidetReg_FpuMisc_ip,
    kCidetReg_FpuMisc_ds,
    kCidetReg_FpuMisc_dp,
    kCidetReg_FpuMisc_fop,
    kCidetReg_FpuMisc_ftw,
    kCidetReg_FpuMisc_fsw,
    kCidetReg_FpuMisc_fcw,
    kCidetReg_FpuMisc_mxcsr_mask,
    kCidetReg_FpuMisc_mxcsr,

    kCidetReg_Mmx_mm0,
    kCidetReg_Mmx_mm1,
    kCidetReg_Mmx_mm2,
    kCidetReg_Mmx_mm3,
    kCidetReg_Mmx_mm4,
    kCidetReg_Mmx_mm5,
    kCidetReg_Mmx_mm6,
    kCidetReg_Mmx_mm7,
#define kCidetReg_Mmx_First                         kCidetReg_Mmx_mm0
#define kCidetReg_Mmx_Last                          kCidetReg_Mmx_mm7

    kCidetReg_Sse_xmm0,
    kCidetReg_Sse_xmm1,
    kCidetReg_Sse_xmm2,
    kCidetReg_Sse_xmm3,
    kCidetReg_Sse_xmm4,
    kCidetReg_Sse_xmm5,
    kCidetReg_Sse_xmm6,
    kCidetReg_Sse_xmm7,
    kCidetReg_Sse_xmm8,
    kCidetReg_Sse_xmm9,
    kCidetReg_Sse_xmm10,
    kCidetReg_Sse_xmm11,
    kCidetReg_Sse_xmm12,
    kCidetReg_Sse_xmm13,
    kCidetReg_Sse_xmm14,
    kCidetReg_Sse_xmm15,
    kCidetReg_Sse_xmm16,
    kCidetReg_Sse_xmm17,
    kCidetReg_Sse_xmm18,
    kCidetReg_Sse_xmm19,
    kCidetReg_Sse_xmm20,
    kCidetReg_Sse_xmm21,
    kCidetReg_Sse_xmm22,
    kCidetReg_Sse_xmm23,
    kCidetReg_Sse_xmm24,
    kCidetReg_Sse_xmm25,
    kCidetReg_Sse_xmm26,
    kCidetReg_Sse_xmm27,
    kCidetReg_Sse_xmm28,
    kCidetReg_Sse_xmm29,
    kCidetReg_Sse_xmm30,
    kCidetReg_Sse_xmm31,
#define kCidetReg_Sse_First                         kCidetReg_Mmx_Xmm0
#define kCidetReg_Sse_Last                          kCidetReg_Mmx_Xmm15
#define kCidetReg_Sse_Last_Avx512                   kCidetReg_Mmx_Xmm31

    kCidetReg_Avx_Ymm0,
    kCidetReg_Avx_Ymm1,
    kCidetReg_Avx_Ymm2,
    kCidetReg_Avx_Ymm3,
    kCidetReg_Avx_Ymm4,
    kCidetReg_Avx_Ymm5,
    kCidetReg_Avx_Ymm6,
    kCidetReg_Avx_Ymm7,
    kCidetReg_Avx_Ymm8,
    kCidetReg_Avx_Ymm9,
    kCidetReg_Avx_Ymm10,
    kCidetReg_Avx_Ymm11,
    kCidetReg_Avx_Ymm12,
    kCidetReg_Avx_Ymm13,
    kCidetReg_Avx_Ymm14,
    kCidetReg_Avx_Ymm15,
    kCidetReg_Avx_Ymm16,
    kCidetReg_Avx_Ymm17,
    kCidetReg_Avx_Ymm18,
    kCidetReg_Avx_Ymm19,
    kCidetReg_Avx_Ymm20,
    kCidetReg_Avx_Ymm21,
    kCidetReg_Avx_Ymm22,
    kCidetReg_Avx_Ymm23,
    kCidetReg_Avx_Ymm24,
    kCidetReg_Avx_Ymm25,
    kCidetReg_Avx_Ymm26,
    kCidetReg_Avx_Ymm27,
    kCidetReg_Avx_Ymm28,
    kCidetReg_Avx_Ymm29,
    kCidetReg_Avx_Ymm30,
    kCidetReg_Avx_Ymm31,
#define kCidetReg_Avx_First                         kCidetReg_Avx_Ymm0
#define kCidetReg_Avx_Last                          kCidetReg_Avx_Ymm15
#define kCidetReg_Avx_Last_Avx512                   kCidetReg_Avx_Ymm31

    kCidetReg_Avx512_Zmm0,
    kCidetReg_Avx512_Zmm1,
    kCidetReg_Avx512_Zmm2,
    kCidetReg_Avx512_Zmm3,
    kCidetReg_Avx512_Zmm4,
    kCidetReg_Avx512_Zmm5,
    kCidetReg_Avx512_Zmm6,
    kCidetReg_Avx512_Zmm7,
    kCidetReg_Avx512_Zmm8,
    kCidetReg_Avx512_Zmm9,
    kCidetReg_Avx512_Zmm10,
    kCidetReg_Avx512_Zmm11,
    kCidetReg_Avx512_Zmm12,
    kCidetReg_Avx512_Zmm13,
    kCidetReg_Avx512_Zmm14,
    kCidetReg_Avx512_Zmm15,
    kCidetReg_Avx512_Zmm16,
    kCidetReg_Avx512_Zmm17,
    kCidetReg_Avx512_Zmm18,
    kCidetReg_Avx512_Zmm19,
    kCidetReg_Avx512_Zmm20,
    kCidetReg_Avx512_Zmm21,
    kCidetReg_Avx512_Zmm22,
    kCidetReg_Avx512_Zmm23,
    kCidetReg_Avx512_Zmm24,
    kCidetReg_Avx512_Zmm25,
    kCidetReg_Avx512_Zmm26,
    kCidetReg_Avx512_Zmm27,
    kCidetReg_Avx512_Zmm28,
    kCidetReg_Avx512_Zmm29,
    kCidetReg_Avx512_Zmm30,
    kCidetReg_Avx512_Zmm31,
#define kCidetReg_Avx512_First                      kCidetReg_Avx512_Zmm0
#define kCidetReg_Avx512_Last                       kCidetReg_Avx512_Zmm31

    kCidetReg_End
} CIDETREG;


/** @name CIDETBUF_XXX - buffer flags.
 * @{ */
#define CIDETBUF_PROT_MASK          UINT32_C(0x0000000f) /**< Page protection mask. */
#define CIDETBUF_PROT_RWX           UINT32_C(0x00000001) /**< Read + write + execute. */
#define CIDETBUF_PROT_RWNX          UINT32_C(0x00000002) /**< Read + write + no execute. */
#define CIDETBUF_PROT_RX            UINT32_C(0x00000003) /**< Read + execute. */
#define CIDETBUF_PROT_RNX           UINT32_C(0x00000004) /**< Read + no execute. */
#define CIDETBUF_PROT_RWX_1NP       UINT32_C(0x00000005) /**< Read + write + execute; 1 page not present. */
#define CIDETBUF_PROT_RWX_1RWNX     UINT32_C(0x00000006) /**< Read + write + execute; 1 page read + write + no execute. */
#define CIDETBUF_PROT_RWX_1RNX      UINT32_C(0x00000007) /**< Read + write + execute; 1 page read + no execute. */
#define CIDETBUF_PROT_RWX_1RWXS     UINT32_C(0x00000008) /**< Read + write + execute; 1 page read + execute + supervisor. */

#define CIDETBUF_LOC_MASK           UINT32_C(0x000000f0) /**< Location mask. */
/** Buffer located at top and start of the 32-bit address space. */
#define CIDETBUF_LOC_32BIT_WRAP     UINT32_C(0x00000010)
/** Buffer located at the low canonical boundrary (AMD64).   */
#define CIDETBUF_LOC_CANON_LO       UINT32_C(0x00000020)
/** Buffer located at the high canonical boundrary (AMD64).   */
#define CIDETBUF_LOC_CANON_HI       UINT32_C(0x00000030)

/** Segment protection mask. */
#define CIDETBUF_SEG_MASK           UINT32_C(0x00000f00)
#define CIDETBUF_SEG_EO             UINT32_C(0x00000100) /**< Execute only */
#define CIDETBUF_SEG_ER             UINT32_C(0x00000200) /**< Execute + read */
#define CIDETBUF_SEG_EO_CONF        UINT32_C(0x00000300) /**< Execute only + conforming. */
#define CIDETBUF_SEG_ER_CONF        UINT32_C(0x00000400) /**< Execute + read + conforming. */
#define CIDETBUF_SEG_RO             UINT32_C(0x00000500) /**< Read only. */
#define CIDETBUF_SEG_RW             UINT32_C(0x00000600) /**< Read + write. */
#define CIDETBUF_SEG_RO_DOWN        UINT32_C(0x00000700) /**< Read only + expand down. */
#define CIDETBUF_SEG_RW_DOWN        UINT32_C(0x00000800) /**< Read + write + expand down. */

#define CIDETBUF_DPL_MASK           UINT32_C(0x00003000) /**< DPL mask. */
#define CIDETBUF_DPL_0              UINT32_C(0x00000000) /**< DPL=0. */
#define CIDETBUF_DPL_1              UINT32_C(0x00001000) /**< DPL=1. */
#define CIDETBUF_DPL_2              UINT32_C(0x00002000) /**< DPL=2. */
#define CIDETBUF_DPL_3              UINT32_C(0x00003000) /**< DPL=3. */
#define CIDETBUF_DPL_SAME           UINT32_C(0x00004000) /**< Same DPL as the execution environment. */

#define CIDETBUF_SEG_LIMIT_BASE_CAP UINT32_C(0x00008000) /**< Capability to change segment limit and base. */

#define CIDETBUF_KIND_DATA          UINT32_C(0x00000000) /**< Data buffer. */
#define CIDETBUF_KIND_CODE          UINT32_C(0x80000000) /**< Code buffer. */
/** Checks if @a a_fFlags describes a code buffer. */
#define CIDETBUF_IS_CODE(a_fFlags)  (((a_fFlags) & CIDETBUF_KIND_CODE) != 0)
/** Checks if @a a_fFlags describes a data buffer. */
#define CIDETBUF_IS_DATA(a_fFlags)  (((a_fFlags) & CIDETBUF_KIND_CODE) == 0)
/** @} */

/** Code buffer size.  (At least two pages.) */
#define CIDET_CODE_BUF_SIZE         (PAGE_SIZE * 2)
/** Data buffer size.  (At least two pages.) */
#define CIDET_DATA_BUF_SIZE         (PAGE_SIZE * 3)


/**
 * Detailed expected exception.
 *
 * This is used to internally in the core to calculate the expected exception
 * considering all the things that may cause exceptions.
 */
typedef enum CIDETEXPECTXCPT
{
    kCidetExpectXcpt_Invalid = 0,
    /** No exception expected.   */
    kCidetExpectXcpt_None,

    /** Page not present. */
    kCidetExpectXcpt_PageNotPresent,
    /** Write access to a non-writable page. */
    kCidetExpectXcpt_PageNotWritable,
    /** Executable access to a non-executable page. */
    kCidetExpectXcpt_PageNotExecutable,
    /** Access to supervisor page from user mode code. */
    kCidetExpectXcpt_PagePrivileged,
#define kCidetExpectXcpt_First_PageFault                kCidetExpectXcpt_PageNotPresent
#define kCidetExpectXcpt_Last_PageFault                 kCidetExpectXcpt_PagePrivileged

    /** Read or write access to an execute only segment. */
    kCidetExpectXcpt_SegExecuteOnly,
    /** Write to a read only or execute+read segment. */
    kCidetExpectXcpt_SegNotWritable,
    /** Exceeded the limit of a non-stack access. */
    kCidetExpectXcpt_SegExceededLimit,
    /** Non-canonical address via any segment other than the stack. */
    kCidetExpectXcpt_AddrNotCanonical,
    /** Misaligned 16 or 32 byte SSE or AVX operand. */
    kCidetExpectXcpt_MisalignedSseAvx,
    /** Privileged instruction. */
    kCidetExpectXcpt_PrivilegedInstruction,
#define kCidetExpectXcpt_First_GeneralProtectionFault   kCidetExpectXcpt_SegExecuteOnly
#define kCidetExpectXcpt_Last_GeneralProtectionFault    kCidetExpectXcpt_PrivilegedInstruction

    /** Exceeded the limit of a stack access. */
    kCidetExpectXcpt_StackExceededLimit,
    /** Non-canonical stack address. */
    kCidetExpectXcpt_StackAddrNotCanonical,
#define kCidetExpectXcpt_First_StackFault               kCidetExpectXcpt_StackExceededLimit
#define kCidetExpectXcpt_Last_StackFault                kCidetExpectXcpt_StackAddrNotCanonical

    /** Misaligned memory operand (and alignment checking is in effect) if AC is
     * enabled (executing in ring-3). */
    kCidetExpectXcpt_MisalignedIfAcEnabled,
    /** Misaligned 16 byte memory operand resulting in \#AC if ring-3 and
     *  enable, otherwise \#GP(0). */
    kCidetExpectXcpt_Misaligned16ByteAcEnabledOrGp,
#define kCidetExpectXcpt_First_AlignmentCheckFault      kCidetExpectXcpt_MisalignedIfAcEnabled
#define kCidetExpectXcpt_Last_AlignmentCheckFault       kCidetExpectXcpt_Misaligned16ByteAcEnabledOrGp

    kCidetExpectXcpt_End
} CIDETEXPECTXCPT;


/**
 * Buffer configuration.
 */
typedef struct CIDETBUFCFG
{
    /** The name of this buffer configuration. */
    const char *pszName;
    /** The buffer flags (CIDETBUF_XXX) */
    uint32_t    fFlags;
} CIDETBUFCFG;
/** Pointer to a constant buffer configuration. */
typedef CIDETBUFCFG const *PCCIDETBUFCFG;


/**
 * CIDET buffer for code or data.
 *
 * ASSUMES page aligned buffers.
 */
typedef struct CIDETBUF
{
    /** @name Owned & modified by the front end.
     * @{ */
    /** Effective buffer address. */
    uint64_t        uEffBufAddr;
    /** The segment base address. */
    uint64_t        uSegBase;
    /** The active segment limit (see also cbSegLimit). UINT64_MAX if flat. */
    uint64_t        cbActiveSegLimit;
    /** This specifies the selector to use if a non-flat segment limit or special
     * segment flags was requested via pfnSetupBuf.  UINT32_MAX if any segment is
     * selector works. */
    uint32_t        uSeg;
    /** The off value at the last pfnReinitBuf call.  */
    uint16_t        offActive;
    /** The cb value at the last pfnReinitBuf call.  */
    uint16_t        cbActive;
    /** Prologue (or front fence) size. */
    uint16_t        cbPrologue;
    /** Epilogue (or tail fence) size. */
    uint16_t        cbEpilogue;
    /** @} */

    /** @name Set by the core before pfnReinitBuf call.
     * @{ */
    /** Pointer to the buffer config. */
    PCCIDETBUFCFG   pCfg;
    /** The configuration index. */
    uint32_t        idxCfg;
    /** The offset into the buffer of the data / code. */
    uint16_t        off;
    /** The number of bytes of data / code. */
    uint16_t        cb;
    /** The segment limit relative to the start of the buffer (last byte included
     *  in count). UINT16_MAX if maximum segment size should be used. */
    uint16_t        cbSegLimit;
    /** Desired segment base offset.
     * This is for checking where the alignment checks are performed. */
    uint8_t         offSegBase;

    /** Set if this buffer is actively being used. */
    bool            fActive : 1;
    /** The operand index (if data), 7 if not active. */
    uint8_t         idxOp : 3;
    /** Code: Set if the expected exception is supposed to occur on the
     * following insturction, not the instruction unter test. */
    bool            fXcptAfterInstruction : 1;
    /** Set if the instruction will read from the buffer. */
    bool            fRead : 1;
    /** Set if the instruction will write to the buffer. */
    bool            fWrite : 1;
    /** The expected exception. */
    CIDETEXPECTXCPT enmExpectXcpt;
    /** @} */
} CIDETBUF;
/** Pointer to a CIDET buffer for code or data. */
typedef CIDETBUF *PCIDETBUF;


/**
 * CPU Instruction Decoding & Execution Testing (CIDET) state.
 */
typedef struct CIDETCORE
{
    /** Magic number (CIDETCORE_MAGIC).  */
    uint32_t            u32Magic;

    /** The target CPU mode / environment. */
    uint8_t             bMode;
    /** The target ring. */
    uint8_t             iRing;
    /** Unused padding bytes.  */
    uint8_t             abPadding1[2];

    /** Test configuration. */
    uint64_t            fTestCfg;

    /** Code buffer configurations to test.
     * The first buffer must be a normal buffer that does not cause any problems. */
    PCCIDETBUFCFG       paCodeBufConfigs;
    /** The number of code buffer configurations to test (pafCodeBufConfigs). */
    uint32_t            cCodeBufConfigs;
    /** The number of data buffer configurations to test (pafDataBufConfigs). */
    uint32_t            cDataBufConfigs;
    /** Data buffer configurations to test.
     * The first buffer must be a normal buffer that does not cause any problems. */
    PCCIDETBUFCFG       paDataBufConfigs;

    /** The instruction currently under testing. */
    PCCIDETINSTR        pCurInstr;

    /** Primary data buffer. */
    CIDETBUF            DataBuf;
    /** Secondary data buffer. */
    CIDETBUF            DataBuf2;

    /** Handle to the random number source. */
    RTRAND              hRand;

    /**
     * Re-initializes one of the data buffers.
     *
     * @returns true on succes, false if the request cannot be satisfied.
     * @param   pThis           The core state.
     * @param   pBuf            Pointer to the buffer structure.
     */
    DECLCALLBACKMEMBER(bool, pfnReInitDataBuf,(struct CIDETCORE *pThis, PCIDETBUF pBuf));

    /**
     * Copies bytes into the data buffer and sets it up for execution.
     *
     * @returns true on succes, false if the request cannot be satisfied.
     * @param   pThis           The core state.
     * @param   pBuf            Pointer to the buffer structure.
     * @param   pvSrc           The source bytes (size and destination offset
     *                          given in pfnReinitBuf call).
     */
    DECLCALLBACKMEMBER(bool, pfnSetupDataBuf,(struct CIDETCORE *pThis, PCIDETBUF pBuf, void const *pvSrc));

    /**
     * Compares buffer content after test execution.
     *
     * This also checks any fill bytes in the buffer that the front end may
     * have put up.  The front end will double buffer the content of supposedly
     * inaccessible pages as well as non-existing pages to simplify things for
     * the core code.
     *
     * @returns true if equal, false if not.
     * @param   pThis           The core state.
     * @param   pBuf            Pointer to the buffer structure.
     * @param   pvExpected      Pointer to the expected source bytes (size and
     *                          buffer offset given in pfnReinitBuf call).
     */
    DECLCALLBACKMEMBER(bool, pfnIsBufEqual,(struct CIDETCORE *pThis, struct CIDETBUF *pBuf, void const *pvExpected));

    /**
     * Re-initializes the code buffer.
     *
     * @returns true on succes, false if the request cannot be satisfied.
     * @param   pThis           The core state.
     * @param   pBuf            Pointer to the CodeBuf member.  The off and cb
     *                          members represent what the core wants to
     *                          execute.
     */
    DECLCALLBACKMEMBER(bool, pfnReInitCodeBuf,(struct CIDETCORE *pThis, PCIDETBUF pBuf));

    /**
     * Emit code into the code buffer, making everything ready for pfnExecute.
     *
     * @returns VBox status code.
     * @param   pThis           Pointer to the core structure.
     * @param   pBuf            Pointer to the CodeBuf member.
     * @param   pvInstr         Pointer to the encoded instruction bytes.
     */
    DECLCALLBACKMEMBER(bool, pfnSetupCodeBuf,(struct CIDETCORE *pThis, PCIDETBUF pBuf, void const *pvInstr));

    /**
     * Executes the code indicated by InCtx, returning the result in ActualCtx.
     *
     * @returns true if execute, false if skipped.
     * @param   pThis           Pointer to the core structure.
     */
    DECLCALLBACKMEMBER(bool, pfnExecute,(struct CIDETCORE *pThis));

    /**
     * Report a test failure.
     *
     * @param   pThis           Pointer to the core structure.
     * @param   pszFormat       Format string containing failure details.
     * @param   va              Arguments referenced in @a pszFormat.
     */
    DECLCALLBACKMEMBER(void, pfnFailure,(struct CIDETCORE *pThis, const char *pszFormat, va_list va));

    /** Array of indexes for use by FNCIDETSETUPINOUT.
     * Reset when changing instruction or switching between valid and invalid
     * inputs. */
    uint32_t            aiInOut[4];

    /** @name Copyied and extracted instruction information.
     * @{ */
    /** The flags (CIDET_OF_XXX) for the MODRM.REG operand, 0 if not applicable. */
    uint32_t            fMrmRegOp;
    /** The flags (CIDET_OF_XXX) for the MODRM.RM operand, 0 if not applicable. */
    uint32_t            fMrmRmOp;
    /** Instruction flags (CIDETINSTR::fFlags). */
    uint64_t            fInstrFlags;
    /** Number of operands (CIDETINSTR::cOperands). */
    uint8_t             cOperands;
    /** Number of memory operands (set by CidetCoreSetupFirstMemoryOperandConfig). */
    uint8_t             cMemoryOperands : 3;
    /** Set if we're working on a MOD R/M byte. */
    bool                fUsesModRm : 1;
    /** The index of the MODRM.REG operand, 7 if not applicable. */
    uint8_t             idxMrmRegOp : 3;
    /** The index of the MODRM.RM operand, 7 if not applicable. */
    uint8_t             idxMrmRmOp : 3;
    /** Set if the SIB byte uses VEX registers for indexing. */
    bool                fUsesVexIndexRegs : 1;
    /** @}  */

    /** @name Basic encoding knobs, wheels and indicators.
     * @{ */
    /** Set if we're working on a SIB byte. */
    bool                fSib : 1;
    /** Required segment prefix (X86_SREG_XXX), X86_SREG_COUNT if not. */
    uint8_t             uSegPrf : 3;
    /** The address size prefix. */
    bool                fAddrSizePrf : 1;
    /** The operand size prefix. */
    bool                fOpSizePrf : 1;
    /** The REX.W prefix value. */
    bool                fRexW : 1;
    /** The REX.R prefix value. */
    bool                fRexR : 1;
    /** The REX.X prefix value. */
    bool                fRexX : 1;
    /** The REX.B prefix value. */
    bool                fRexB : 1;
    /** Set if a REX prefix is required with or without flags (for byte regs). */
    bool                fRex : 1;
    /** Use VEX encoding. */
    bool                fVex : 1;
    /** Use EVEX encoding. */
    bool                fEvex : 1;
    /** Indicator: Effective addressing mode in bytes (2, 4, 8). */
    uint8_t             cbAddrMode : 4;
    /** Indicator: Set if there is an operand accessing memory. */
    bool                fHasMemoryOperand : 1;
    /** Indicator: Set if a register is used in two or more operands, and one of
     * them being for addressing. */
    bool                fHasRegCollisionMem : 1;
    /** Indicator: Helper indicator for tracking SIB.BASE collision. */
    bool                fHasRegCollisionMemBase : 1;
    /** Indicator: Helper indicator for tracking SIB.INDEX collision. */
    bool                fHasRegCollisionMemIndex : 1;
    /** Indicator: Set if a register is used directly in more than one operand. */
    bool                fHasRegCollisionDirect : 1;

    /** Indicator: Set if MODRM.REG is the stack register. */
    bool                fHasStackRegInMrmReg : 1;
    /** Indicator: Set if MODRM.RM or SIB.BASE is the stack register. */
    bool                fHasStackRegInMrmRmBase: 1;

    /** Indicator: High byte-register specified by MODRM.REG. */
    bool                fHasHighByteRegInMrmReg : 1;
    /** Indicator: High byte-register specified by MODRM.RM. */
    bool                fHasHighByteRegInMrmRm : 1;
    /** Indicator: Set if REX prefixes are incompatible with the byte-register
     * specified by MODRM.REG. */
    bool                fNoRexPrefixMrmReg : 1;
    /** Indicator: Set if REX prefixes are incompatible with the byte-register
     * specified by MODRM.RM. */
    bool                fNoRexPrefixMrmRm : 1;
    /** Indicator: fNoRexPrefixMrmReg || fNoRexPrefixMrmMr. */
    bool                fNoRexPrefix : 1;
    /** The MOD R/M byte we're working on (if fUsesModRm is set). */
    uint8_t             bModRm;
    /** The SIB/VSIB byte we're working on (if fSib is set). */
    uint8_t             bSib;
    /** @} */

    /** The effective instruction address.  (See InCtx.rip and InCtx.cs for the
     * rest of the instruction addressing stuff.) */
    uint64_t            uInstrEffAddr;

    /** Operand information, mainly for the FNCIDETSETUPINOUT and similar. */
    struct
    {
        /** The operand flags copied from (CIDETINSTR::afOperands).   */
        uint32_t        fFlags;
        /** The encoded register number, if register, UINT8_MAX if not.  */
        uint8_t         iReg;
        /** The actual operand size (encoded). */
        uint8_t         cb;
        /** Set if immediate value. */
        bool            fIsImmediate : 1;
        /** Set if memory access. */
        bool            fIsMem : 1;
        /** Set if addressing is relative to RIP. */
        bool            fIsRipRelative : 1;
        /** Set if it's a high byte register. */
        bool            fIsHighByteRegister : 1;
        /** Size of the disposition, 0 if none. */
        uint8_t         cbMemDisp;
        /** Base register, UINT8_MAX if not applicable. */
        uint8_t         iMemBaseReg;
        /** Index register, UINT8_MAX if not applicable. */
        uint8_t         iMemIndexReg;
        /** Index register, 1 if not applicable. */
        uint8_t         uMemScale;
        /** Effective segment register, UINT8_MAX if not memory access. */
        uint8_t         iEffSeg;
        /** Segment offset if memory access.  Undefined if not memory access. */
        uint64_t        offSeg;
        /** The effective address if memory access. */
        uint64_t        uEffAddr;
        /** Immediate or displacement value. */
        uint64_t        uImmDispValue;
        /** Base register value, undefined if irrelevant. */
        uint64_t        uMemBaseRegValue;
        /** Index register value, undefined if irrelevant. */
        uint64_t        uMemIndexRegValue;
        /** Points to where the input data for this operand should be placed,
         * when possible.  In the fIsMem = true case, it either points directly
         * to the input buffer or to a temporary one.  While in the other case,
         * it'll point into InCtx when possible. */
        RTPTRUNION      In;
        /** Points to where the expected output data for this operand should be
         * stored, when possible.  In the fIsMem = false case, it'll point into
         * ExpectedCtx when possible. */
        RTPTRUNION      Expected;
        /** Pointer to the data buffer for this operand. */
        PCIDETBUF       pDataBuf;
    }                   aOperands[4];

    /** Buffer where we assemble the instruction. */
    uint8_t             abInstr[45];
    /** The size of the instruction in abInstr. */
    uint8_t             cbInstr;
    /** Offset of the instruction into the buffer. */
    uint16_t            offInstr;
    /** Current code buffer. */
    CIDETBUF            CodeBuf;

    /** The input context.  Initalized by driver and FNCIDETSETUPINOUT. */
    CIDETCPUCTX         InCtx;
    /** The expected output context. */
    CIDETCPUCTX         ExpectedCtx;
    /** The actual output context. */
    CIDETCPUCTX         ActualCtx;
    /** Template input context, initialized when setting the mode. */
    CIDETCPUCTX         InTemplateCtx;

    /** Input and expected output temporary memory buffers. */
    uint8_t             abBuf[0x2000];


    /** Number of skipped tests because of pfnSetupInOut failures. */
    uint32_t            cSkippedSetupInOut;
    /** Number of skipped tests because of pfnReInitDataBuf failures. */
    uint32_t            cSkippedReInitDataBuf;
    /** Number of skipped tests because of pfnSetupDataBuf failures. */
    uint32_t            cSkippedSetupDataBuf;
    /** Number of skipped tests because RIP relative addressing constraints. */
    uint32_t            cSkippedDataBufWrtRip;
    /** Number of skipped tests because of assemble failures. */
    uint32_t            cSkippedAssemble;
    /** Number of skipped tests because of pfnReInitCodeBuf failures. */
    uint32_t            cSkippedReInitCodeBuf;
    /** Number of skipped tests because of pfnSetupCodeBuf failures. */
    uint32_t            cSkippedSetupCodeBuf;
    /** Number of skipped tests because the base and index registers are the same
     * one and there was a remainder when trying to point to the data buffer. */
    uint32_t            cSkippedSameBaseIndexRemainder;
    /** Number of skipped tests because index-only addressing left a remainder. */
    uint32_t            cSkippedOnlyIndexRemainder;
    /** Number of skipped tests because of direct addressing overflowed. */
    uint32_t            cSkippedDirectAddressingOverflow;


} CIDETCORE;
/** Pointer to the CIDET core state. */
typedef CIDETCORE *PCIDETCORE;

/** Magic number for CIDETCORE (Lee Konitz). */
#define CIDETCORE_MAGIC     UINT32_C(0x19271013)


int     CidetCoreInit(PCIDETCORE pThis, RTRAND hRand);
void    CidetCoreDelete(PCIDETCORE pThis);
int     CidetCoreSetTargetMode(PCIDETCORE pThis, uint8_t bMode);
uint32_t CidetCoreGetOperandSize(PCIDETCORE pThis, uint8_t iOp);
bool    CidetCoreTestInstruction(PCIDETCORE pThis, PCCIDETINSTR pInstr);


extern const CIDETINSTR g_aCidetInstructions1[];
extern const uint32_t   g_cCidetInstructions1;

#endif /* !VBOX_INCLUDED_SRC_cpu_cidet_h */

