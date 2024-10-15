/* $Id: IEMAllInstCommon.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Instruction Decoding and Emulation, Common Bits.
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

#ifndef VMM_INCLUDED_SRC_VMMAll_IEMAllInstCommon_cpp_h
#define VMM_INCLUDED_SRC_VMMAll_IEMAllInstCommon_cpp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Repeats a_fn four times.  For decoding tables. */
#define IEMOP_X4(a_fn) a_fn, a_fn, a_fn, a_fn


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifndef TST_IEM_CHECK_MC

/** Function table for the BSF instruction. */
IEM_STATIC const IEMOPBINSIZES g_iemAImpl_bsf =
{
    NULL,  NULL,
    iemAImpl_bsf_u16, NULL,
    iemAImpl_bsf_u32, NULL,
    iemAImpl_bsf_u64, NULL
};

/** Function table for the BSF instruction, AMD EFLAGS variant. */
IEM_STATIC const IEMOPBINSIZES g_iemAImpl_bsf_amd =
{
    NULL,  NULL,
    iemAImpl_bsf_u16_amd, NULL,
    iemAImpl_bsf_u32_amd, NULL,
    iemAImpl_bsf_u64_amd, NULL
};

/** Function table for the BSF instruction, Intel EFLAGS variant. */
IEM_STATIC const IEMOPBINSIZES g_iemAImpl_bsf_intel =
{
    NULL,  NULL,
    iemAImpl_bsf_u16_intel, NULL,
    iemAImpl_bsf_u32_intel, NULL,
    iemAImpl_bsf_u64_intel, NULL
};

/** EFLAGS variation selection table for the BSF instruction. */
IEM_STATIC const IEMOPBINSIZES * const g_iemAImpl_bsf_eflags[] =
{
    &g_iemAImpl_bsf,
    &g_iemAImpl_bsf_intel,
    &g_iemAImpl_bsf_amd,
    &g_iemAImpl_bsf,
};

/** Function table for the BSR instruction. */
IEM_STATIC const IEMOPBINSIZES g_iemAImpl_bsr =
{
    NULL,  NULL,
    iemAImpl_bsr_u16, NULL,
    iemAImpl_bsr_u32, NULL,
    iemAImpl_bsr_u64, NULL
};

/** Function table for the BSR instruction, AMD EFLAGS variant. */
IEM_STATIC const IEMOPBINSIZES g_iemAImpl_bsr_amd =
{
    NULL,  NULL,
    iemAImpl_bsr_u16_amd, NULL,
    iemAImpl_bsr_u32_amd, NULL,
    iemAImpl_bsr_u64_amd, NULL
};

/** Function table for the BSR instruction, Intel EFLAGS variant. */
IEM_STATIC const IEMOPBINSIZES g_iemAImpl_bsr_intel =
{
    NULL,  NULL,
    iemAImpl_bsr_u16_intel, NULL,
    iemAImpl_bsr_u32_intel, NULL,
    iemAImpl_bsr_u64_intel, NULL
};

/** EFLAGS variation selection table for the BSR instruction. */
IEM_STATIC const IEMOPBINSIZES * const g_iemAImpl_bsr_eflags[] =
{
    &g_iemAImpl_bsr,
    &g_iemAImpl_bsr_intel,
    &g_iemAImpl_bsr_amd,
    &g_iemAImpl_bsr,
};

/** Function table for the IMUL instruction. */
IEM_STATIC const IEMOPBINSIZES g_iemAImpl_imul_two =
{
    NULL,  NULL,
    iemAImpl_imul_two_u16, NULL,
    iemAImpl_imul_two_u32, NULL,
    iemAImpl_imul_two_u64, NULL
};

/** Function table for the IMUL instruction, AMD EFLAGS variant. */
IEM_STATIC const IEMOPBINSIZES g_iemAImpl_imul_two_amd =
{
    NULL,  NULL,
    iemAImpl_imul_two_u16_amd, NULL,
    iemAImpl_imul_two_u32_amd, NULL,
    iemAImpl_imul_two_u64_amd, NULL
};

/** Function table for the IMUL instruction, Intel EFLAGS variant. */
IEM_STATIC const IEMOPBINSIZES g_iemAImpl_imul_two_intel =
{
    NULL,  NULL,
    iemAImpl_imul_two_u16_intel, NULL,
    iemAImpl_imul_two_u32_intel, NULL,
    iemAImpl_imul_two_u64_intel, NULL
};

/** EFLAGS variation selection table for the IMUL instruction. */
IEM_STATIC const IEMOPBINSIZES * const g_iemAImpl_imul_two_eflags[] =
{
    &g_iemAImpl_imul_two,
    &g_iemAImpl_imul_two_intel,
    &g_iemAImpl_imul_two_amd,
    &g_iemAImpl_imul_two,
};

/** EFLAGS variation selection table for the 16-bit IMUL instruction. */
IEM_STATIC PFNIEMAIMPLBINU16 const g_iemAImpl_imul_two_u16_eflags[] =
{
    iemAImpl_imul_two_u16,
    iemAImpl_imul_two_u16_intel,
    iemAImpl_imul_two_u16_amd,
    iemAImpl_imul_two_u16,
};

/** EFLAGS variation selection table for the 32-bit IMUL instruction. */
IEM_STATIC PFNIEMAIMPLBINU32 const g_iemAImpl_imul_two_u32_eflags[] =
{
    iemAImpl_imul_two_u32,
    iemAImpl_imul_two_u32_intel,
    iemAImpl_imul_two_u32_amd,
    iemAImpl_imul_two_u32,
};

/** EFLAGS variation selection table for the 64-bit IMUL instruction. */
IEM_STATIC PFNIEMAIMPLBINU64 const g_iemAImpl_imul_two_u64_eflags[] =
{
    iemAImpl_imul_two_u64,
    iemAImpl_imul_two_u64_intel,
    iemAImpl_imul_two_u64_amd,
    iemAImpl_imul_two_u64,
};

/** Function table for the ROL instruction. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_rol =
{
    iemAImpl_rol_u8,
    iemAImpl_rol_u16,
    iemAImpl_rol_u32,
    iemAImpl_rol_u64
};

/** Function table for the ROL instruction, AMD EFLAGS variant. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_rol_amd =
{
    iemAImpl_rol_u8_amd,
    iemAImpl_rol_u16_amd,
    iemAImpl_rol_u32_amd,
    iemAImpl_rol_u64_amd
};

/** Function table for the ROL instruction, Intel EFLAGS variant. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_rol_intel =
{
    iemAImpl_rol_u8_intel,
    iemAImpl_rol_u16_intel,
    iemAImpl_rol_u32_intel,
    iemAImpl_rol_u64_intel
};

/** EFLAGS variation selection table for the ROL instruction. */
IEM_STATIC const IEMOPSHIFTSIZES * const g_iemAImpl_rol_eflags[] =
{
    &g_iemAImpl_rol,
    &g_iemAImpl_rol_intel,
    &g_iemAImpl_rol_amd,
    &g_iemAImpl_rol,
};


/** Function table for the ROR instruction. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_ror =
{
    iemAImpl_ror_u8,
    iemAImpl_ror_u16,
    iemAImpl_ror_u32,
    iemAImpl_ror_u64
};

/** Function table for the ROR instruction, AMD EFLAGS variant. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_ror_amd =
{
    iemAImpl_ror_u8_amd,
    iemAImpl_ror_u16_amd,
    iemAImpl_ror_u32_amd,
    iemAImpl_ror_u64_amd
};

/** Function table for the ROR instruction, Intel EFLAGS variant. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_ror_intel =
{
    iemAImpl_ror_u8_intel,
    iemAImpl_ror_u16_intel,
    iemAImpl_ror_u32_intel,
    iemAImpl_ror_u64_intel
};

/** EFLAGS variation selection table for the ROR instruction. */
IEM_STATIC const IEMOPSHIFTSIZES * const g_iemAImpl_ror_eflags[] =
{
    &g_iemAImpl_ror,
    &g_iemAImpl_ror_intel,
    &g_iemAImpl_ror_amd,
    &g_iemAImpl_ror,
};


/** Function table for the RCL instruction. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_rcl =
{
    iemAImpl_rcl_u8,
    iemAImpl_rcl_u16,
    iemAImpl_rcl_u32,
    iemAImpl_rcl_u64
};

/** Function table for the RCL instruction, AMD EFLAGS variant. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_rcl_amd =
{
    iemAImpl_rcl_u8_amd,
    iemAImpl_rcl_u16_amd,
    iemAImpl_rcl_u32_amd,
    iemAImpl_rcl_u64_amd
};

/** Function table for the RCL instruction, Intel EFLAGS variant. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_rcl_intel =
{
    iemAImpl_rcl_u8_intel,
    iemAImpl_rcl_u16_intel,
    iemAImpl_rcl_u32_intel,
    iemAImpl_rcl_u64_intel
};

/** EFLAGS variation selection table for the RCL instruction. */
IEM_STATIC const IEMOPSHIFTSIZES * const g_iemAImpl_rcl_eflags[] =
{
    &g_iemAImpl_rcl,
    &g_iemAImpl_rcl_intel,
    &g_iemAImpl_rcl_amd,
    &g_iemAImpl_rcl,
};


/** Function table for the RCR instruction. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_rcr =
{
    iemAImpl_rcr_u8,
    iemAImpl_rcr_u16,
    iemAImpl_rcr_u32,
    iemAImpl_rcr_u64
};

/** Function table for the RCR instruction, AMD EFLAGS variant. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_rcr_amd =
{
    iemAImpl_rcr_u8_amd,
    iemAImpl_rcr_u16_amd,
    iemAImpl_rcr_u32_amd,
    iemAImpl_rcr_u64_amd
};

/** Function table for the RCR instruction, Intel EFLAGS variant. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_rcr_intel =
{
    iemAImpl_rcr_u8_intel,
    iemAImpl_rcr_u16_intel,
    iemAImpl_rcr_u32_intel,
    iemAImpl_rcr_u64_intel
};

/** EFLAGS variation selection table for the RCR instruction. */
IEM_STATIC const IEMOPSHIFTSIZES * const g_iemAImpl_rcr_eflags[] =
{
    &g_iemAImpl_rcr,
    &g_iemAImpl_rcr_intel,
    &g_iemAImpl_rcr_amd,
    &g_iemAImpl_rcr,
};


/** Function table for the SHL instruction. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_shl =
{
    iemAImpl_shl_u8,
    iemAImpl_shl_u16,
    iemAImpl_shl_u32,
    iemAImpl_shl_u64
};

/** Function table for the SHL instruction, AMD EFLAGS variant. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_shl_amd =
{
    iemAImpl_shl_u8_amd,
    iemAImpl_shl_u16_amd,
    iemAImpl_shl_u32_amd,
    iemAImpl_shl_u64_amd
};

/** Function table for the SHL instruction, Intel EFLAGS variant. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_shl_intel =
{
    iemAImpl_shl_u8_intel,
    iemAImpl_shl_u16_intel,
    iemAImpl_shl_u32_intel,
    iemAImpl_shl_u64_intel
};

/** EFLAGS variation selection table for the SHL instruction. */
IEM_STATIC const IEMOPSHIFTSIZES * const g_iemAImpl_shl_eflags[] =
{
    &g_iemAImpl_shl,
    &g_iemAImpl_shl_intel,
    &g_iemAImpl_shl_amd,
    &g_iemAImpl_shl,
};


/** Function table for the SHR instruction. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_shr =
{
    iemAImpl_shr_u8,
    iemAImpl_shr_u16,
    iemAImpl_shr_u32,
    iemAImpl_shr_u64
};

/** Function table for the SHR instruction, AMD EFLAGS variant. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_shr_amd =
{
    iemAImpl_shr_u8_amd,
    iemAImpl_shr_u16_amd,
    iemAImpl_shr_u32_amd,
    iemAImpl_shr_u64_amd
};

/** Function table for the SHR instruction, Intel EFLAGS variant. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_shr_intel =
{
    iemAImpl_shr_u8_intel,
    iemAImpl_shr_u16_intel,
    iemAImpl_shr_u32_intel,
    iemAImpl_shr_u64_intel
};

/** EFLAGS variation selection table for the SHR instruction. */
IEM_STATIC const IEMOPSHIFTSIZES * const g_iemAImpl_shr_eflags[] =
{
    &g_iemAImpl_shr,
    &g_iemAImpl_shr_intel,
    &g_iemAImpl_shr_amd,
    &g_iemAImpl_shr,
};


/** Function table for the SAR instruction. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_sar =
{
    iemAImpl_sar_u8,
    iemAImpl_sar_u16,
    iemAImpl_sar_u32,
    iemAImpl_sar_u64
};

/** Function table for the SAR instruction, AMD EFLAGS variant. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_sar_amd =
{
    iemAImpl_sar_u8_amd,
    iemAImpl_sar_u16_amd,
    iemAImpl_sar_u32_amd,
    iemAImpl_sar_u64_amd
};

/** Function table for the SAR instruction, Intel EFLAGS variant. */
IEM_STATIC const IEMOPSHIFTSIZES g_iemAImpl_sar_intel =
{
    iemAImpl_sar_u8_intel,
    iemAImpl_sar_u16_intel,
    iemAImpl_sar_u32_intel,
    iemAImpl_sar_u64_intel
};

/** EFLAGS variation selection table for the SAR instruction. */
IEM_STATIC const IEMOPSHIFTSIZES * const g_iemAImpl_sar_eflags[] =
{
    &g_iemAImpl_sar,
    &g_iemAImpl_sar_intel,
    &g_iemAImpl_sar_amd,
    &g_iemAImpl_sar,
};


/** Function table for the MUL instruction. */
IEM_STATIC const IEMOPMULDIVSIZES g_iemAImpl_mul =
{
    iemAImpl_mul_u8,
    iemAImpl_mul_u16,
    iemAImpl_mul_u32,
    iemAImpl_mul_u64
};

/** Function table for the MUL instruction, AMD EFLAGS variation. */
IEM_STATIC const IEMOPMULDIVSIZES g_iemAImpl_mul_amd =
{
    iemAImpl_mul_u8_amd,
    iemAImpl_mul_u16_amd,
    iemAImpl_mul_u32_amd,
    iemAImpl_mul_u64_amd
};

/** Function table for the MUL instruction, Intel EFLAGS variation. */
IEM_STATIC const IEMOPMULDIVSIZES g_iemAImpl_mul_intel =
{
    iemAImpl_mul_u8_intel,
    iemAImpl_mul_u16_intel,
    iemAImpl_mul_u32_intel,
    iemAImpl_mul_u64_intel
};

/** EFLAGS variation selection table for the MUL instruction. */
IEM_STATIC const IEMOPMULDIVSIZES * const g_iemAImpl_mul_eflags[] =
{
    &g_iemAImpl_mul,
    &g_iemAImpl_mul_intel,
    &g_iemAImpl_mul_amd,
    &g_iemAImpl_mul,
};

/** EFLAGS variation selection table for the 8-bit MUL instruction. */
IEM_STATIC PFNIEMAIMPLMULDIVU8 const g_iemAImpl_mul_u8_eflags[] =
{
    iemAImpl_mul_u8,
    iemAImpl_mul_u8_intel,
    iemAImpl_mul_u8_amd,
    iemAImpl_mul_u8
};


/** Function table for the IMUL instruction working implicitly on rAX. */
IEM_STATIC const IEMOPMULDIVSIZES g_iemAImpl_imul =
{
    iemAImpl_imul_u8,
    iemAImpl_imul_u16,
    iemAImpl_imul_u32,
    iemAImpl_imul_u64
};

/** Function table for the IMUL instruction working implicitly on rAX, AMD EFLAGS variation. */
IEM_STATIC const IEMOPMULDIVSIZES g_iemAImpl_imul_amd =
{
    iemAImpl_imul_u8_amd,
    iemAImpl_imul_u16_amd,
    iemAImpl_imul_u32_amd,
    iemAImpl_imul_u64_amd
};

/** Function table for the IMUL instruction working implicitly on rAX, Intel EFLAGS variation. */
IEM_STATIC const IEMOPMULDIVSIZES g_iemAImpl_imul_intel =
{
    iemAImpl_imul_u8_intel,
    iemAImpl_imul_u16_intel,
    iemAImpl_imul_u32_intel,
    iemAImpl_imul_u64_intel
};

/** EFLAGS variation selection table for the IMUL instruction. */
IEM_STATIC const IEMOPMULDIVSIZES * const g_iemAImpl_imul_eflags[] =
{
    &g_iemAImpl_imul,
    &g_iemAImpl_imul_intel,
    &g_iemAImpl_imul_amd,
    &g_iemAImpl_imul,
};

/** EFLAGS variation selection table for the 8-bit IMUL instruction. */
IEM_STATIC PFNIEMAIMPLMULDIVU8 const g_iemAImpl_imul_u8_eflags[] =
{
    iemAImpl_imul_u8,
    iemAImpl_imul_u8_intel,
    iemAImpl_imul_u8_amd,
    iemAImpl_imul_u8
};


/** Function table for the DIV instruction. */
IEM_STATIC const IEMOPMULDIVSIZES g_iemAImpl_div =
{
    iemAImpl_div_u8,
    iemAImpl_div_u16,
    iemAImpl_div_u32,
    iemAImpl_div_u64
};

/** Function table for the DIV instruction, AMD EFLAGS variation. */
IEM_STATIC const IEMOPMULDIVSIZES g_iemAImpl_div_amd =
{
    iemAImpl_div_u8_amd,
    iemAImpl_div_u16_amd,
    iemAImpl_div_u32_amd,
    iemAImpl_div_u64_amd
};

/** Function table for the DIV instruction, Intel EFLAGS variation. */
IEM_STATIC const IEMOPMULDIVSIZES g_iemAImpl_div_intel =
{
    iemAImpl_div_u8_intel,
    iemAImpl_div_u16_intel,
    iemAImpl_div_u32_intel,
    iemAImpl_div_u64_intel
};

/** EFLAGS variation selection table for the DIV instruction. */
IEM_STATIC const IEMOPMULDIVSIZES * const g_iemAImpl_div_eflags[] =
{
    &g_iemAImpl_div,
    &g_iemAImpl_div_intel,
    &g_iemAImpl_div_amd,
    &g_iemAImpl_div,
};

/** EFLAGS variation selection table for the 8-bit DIV instruction. */
IEM_STATIC PFNIEMAIMPLMULDIVU8 const g_iemAImpl_div_u8_eflags[] =
{
    iemAImpl_div_u8,
    iemAImpl_div_u8_intel,
    iemAImpl_div_u8_amd,
    iemAImpl_div_u8
};


/** Function table for the IDIV instruction. */
IEM_STATIC const IEMOPMULDIVSIZES g_iemAImpl_idiv =
{
    iemAImpl_idiv_u8,
    iemAImpl_idiv_u16,
    iemAImpl_idiv_u32,
    iemAImpl_idiv_u64
};

/** Function table for the IDIV instruction, AMD EFLAGS variation. */
IEM_STATIC const IEMOPMULDIVSIZES g_iemAImpl_idiv_amd =
{
    iemAImpl_idiv_u8_amd,
    iemAImpl_idiv_u16_amd,
    iemAImpl_idiv_u32_amd,
    iemAImpl_idiv_u64_amd
};

/** Function table for the IDIV instruction, Intel EFLAGS variation. */
IEM_STATIC const IEMOPMULDIVSIZES g_iemAImpl_idiv_intel =
{
    iemAImpl_idiv_u8_intel,
    iemAImpl_idiv_u16_intel,
    iemAImpl_idiv_u32_intel,
    iemAImpl_idiv_u64_intel
};

/** EFLAGS variation selection table for the IDIV instruction. */
IEM_STATIC const IEMOPMULDIVSIZES * const g_iemAImpl_idiv_eflags[] =
{
    &g_iemAImpl_idiv,
    &g_iemAImpl_idiv_intel,
    &g_iemAImpl_idiv_amd,
    &g_iemAImpl_idiv,
};

/** EFLAGS variation selection table for the 8-bit IDIV instruction. */
IEM_STATIC PFNIEMAIMPLMULDIVU8 const g_iemAImpl_idiv_u8_eflags[] =
{
    iemAImpl_idiv_u8,
    iemAImpl_idiv_u8_intel,
    iemAImpl_idiv_u8_amd,
    iemAImpl_idiv_u8
};


/** Function table for the SHLD instruction. */
IEM_STATIC const IEMOPSHIFTDBLSIZES g_iemAImpl_shld =
{
    iemAImpl_shld_u16,
    iemAImpl_shld_u32,
    iemAImpl_shld_u64,
};

/** Function table for the SHLD instruction, AMD EFLAGS variation. */
IEM_STATIC const IEMOPSHIFTDBLSIZES g_iemAImpl_shld_amd =
{
    iemAImpl_shld_u16_amd,
    iemAImpl_shld_u32_amd,
    iemAImpl_shld_u64_amd
};

/** Function table for the SHLD instruction, Intel EFLAGS variation. */
IEM_STATIC const IEMOPSHIFTDBLSIZES g_iemAImpl_shld_intel =
{
    iemAImpl_shld_u16_intel,
    iemAImpl_shld_u32_intel,
    iemAImpl_shld_u64_intel
};

/** EFLAGS variation selection table for the SHLD instruction. */
IEM_STATIC const IEMOPSHIFTDBLSIZES * const g_iemAImpl_shld_eflags[] =
{
    &g_iemAImpl_shld,
    &g_iemAImpl_shld_intel,
    &g_iemAImpl_shld_amd,
    &g_iemAImpl_shld
};

/** Function table for the SHRD instruction. */
IEM_STATIC const IEMOPSHIFTDBLSIZES g_iemAImpl_shrd =
{
    iemAImpl_shrd_u16,
    iemAImpl_shrd_u32,
    iemAImpl_shrd_u64
};

/** Function table for the SHRD instruction, AMD EFLAGS variation. */
IEM_STATIC const IEMOPSHIFTDBLSIZES g_iemAImpl_shrd_amd =
{
    iemAImpl_shrd_u16_amd,
    iemAImpl_shrd_u32_amd,
    iemAImpl_shrd_u64_amd
};

/** Function table for the SHRD instruction, Intel EFLAGS variation. */
IEM_STATIC const IEMOPSHIFTDBLSIZES g_iemAImpl_shrd_intel =
{
    iemAImpl_shrd_u16_intel,
    iemAImpl_shrd_u32_intel,
    iemAImpl_shrd_u64_intel
};

/** EFLAGS variation selection table for the SHRD instruction. */
IEM_STATIC const IEMOPSHIFTDBLSIZES * const g_iemAImpl_shrd_eflags[] =
{
    &g_iemAImpl_shrd,
    &g_iemAImpl_shrd_intel,
    &g_iemAImpl_shrd_amd,
    &g_iemAImpl_shrd
};


# if (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)) && !defined(IEM_WITHOUT_ASSEMBLY)
/** Function table for the VPXOR instruction */
IEM_STATIC const IEMOPMEDIAOPTF3 g_iemAImpl_vpand          = { iemAImpl_vpand_u128,   iemAImpl_vpand_u256 };
/** Function table for the VPXORN instruction */
IEM_STATIC const IEMOPMEDIAOPTF3 g_iemAImpl_vpandn         = { iemAImpl_vpandn_u128,  iemAImpl_vpandn_u256 };
/** Function table for the VPOR instruction */
IEM_STATIC const IEMOPMEDIAOPTF3 g_iemAImpl_vpor           = { iemAImpl_vpor_u128,    iemAImpl_vpor_u256 };
/** Function table for the VPXOR instruction */
IEM_STATIC const IEMOPMEDIAOPTF3 g_iemAImpl_vpxor          = { iemAImpl_vpxor_u128,   iemAImpl_vpxor_u256 };
# endif

/** Function table for the VPAND instruction, software fallback. */
IEM_STATIC const IEMOPMEDIAOPTF3 g_iemAImpl_vpand_fallback = { iemAImpl_vpand_u128_fallback,  iemAImpl_vpand_u256_fallback };
/** Function table for the VPANDN instruction, software fallback. */
IEM_STATIC const IEMOPMEDIAOPTF3 g_iemAImpl_vpandn_fallback= { iemAImpl_vpandn_u128_fallback, iemAImpl_vpandn_u256_fallback };
/** Function table for the VPOR instruction, software fallback. */
IEM_STATIC const IEMOPMEDIAOPTF3 g_iemAImpl_vpor_fallback  = { iemAImpl_vpor_u128_fallback,   iemAImpl_vpor_u256_fallback };
/** Function table for the VPXOR instruction, software fallback. */
IEM_STATIC const IEMOPMEDIAOPTF3 g_iemAImpl_vpxor_fallback = { iemAImpl_vpxor_u128_fallback,  iemAImpl_vpxor_u256_fallback };

#endif /* !TST_IEM_CHECK_MC */



#if defined(TST_IEM_CHECK_MC) || defined(IEM_WITH_ONE_BYTE_TABLE) || defined(IEM_WITH_TWO_BYTE_TABLE)
/** Opcodes 0xf1, 0xd6. */
FNIEMOP_DEF(iemOp_Invalid)
{
    IEMOP_MNEMONIC(Invalid, "Invalid");
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#endif


#if defined(TST_IEM_CHECK_MC) || defined(IEM_WITH_TWO_BYTE_TABLE) || defined(IEM_WITH_VEX_TABLES)
/** Invalid with RM byte . */
FNIEMOPRM_DEF(iemOp_InvalidWithRM)
{
    RT_NOREF_PV(bRm);
    IEMOP_MNEMONIC(InvalidWithRm, "InvalidWithRM");
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#endif


#if defined(TST_IEM_CHECK_MC) || defined(IEM_WITH_TWO_BYTE_TABLE)
/** Invalid with RM byte where intel decodes any additional address encoding
 *  bytes. */
FNIEMOPRM_DEF(iemOp_InvalidWithRMNeedDecode)
{
    IEMOP_MNEMONIC(InvalidWithRMNeedDecode, "InvalidWithRMNeedDecode");
    if (pVCpu->iem.s.enmCpuVendor == CPUMCPUVENDOR_INTEL && IEM_IS_MODRM_MEM_MODE(bRm))
        IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES(bRm);
    IEMOP_HLP_DONE_DECODING();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#endif


#if defined(TST_IEM_CHECK_MC) || defined(IEM_WITH_TWO_BYTE_TABLE)
/** Invalid with RM byte where both AMD and Intel decodes any additional
 *  address encoding bytes. */
FNIEMOPRM_DEF(iemOp_InvalidWithRMAllNeeded)
{
    IEMOP_MNEMONIC(InvalidWithRMAllNeeded, "InvalidWithRMAllNeeded");
    if (IEM_IS_MODRM_MEM_MODE(bRm))
        IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES(bRm);
    IEMOP_HLP_DONE_DECODING();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#endif


#if defined(TST_IEM_CHECK_MC) || defined(IEM_WITH_TWO_BYTE_TABLE) || defined(IEM_WITH_VEX_TABLES)
/** Invalid with RM byte where intel requires 8-byte immediate.
 * Intel will also need SIB and displacement if bRm indicates memory. */
FNIEMOPRM_DEF(iemOp_InvalidWithRMNeedImm8)
{
    IEMOP_MNEMONIC(InvalidWithRMNeedImm8, "InvalidWithRMNeedImm8");
    if (pVCpu->iem.s.enmCpuVendor == CPUMCPUVENDOR_INTEL)
    {
        if (IEM_IS_MODRM_MEM_MODE(bRm))
            IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES(bRm);
        uint8_t bImm8;  IEM_OPCODE_GET_NEXT_U8(&bImm8);  RT_NOREF(bImm8);
    }
    IEMOP_HLP_DONE_DECODING();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#endif


#if defined(TST_IEM_CHECK_MC) || defined(IEM_WITH_TWO_BYTE_TABLE)
/** Invalid with RM byte where intel requires 8-byte immediate.
 * Both AMD and Intel also needs SIB and displacement according to bRm. */
FNIEMOPRM_DEF(iemOp_InvalidWithRMAllNeedImm8)
{
    IEMOP_MNEMONIC(InvalidWithRMAllNeedImm8, "InvalidWithRMAllNeedImm8");
    if (IEM_IS_MODRM_MEM_MODE(bRm))
        IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES(bRm);
    uint8_t bImm8;  IEM_OPCODE_GET_NEXT_U8(&bImm8);  RT_NOREF(bImm8);
    IEMOP_HLP_DONE_DECODING();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#endif


#if defined(TST_IEM_CHECK_MC) || defined(IEM_WITH_TWO_BYTE_TABLE) || defined(IEM_WITH_THREE_BYTE_TABLES) || defined(IEM_WITH_VEX_TABLES)
/** Invalid opcode where intel requires Mod R/M sequence. */
FNIEMOP_DEF(iemOp_InvalidNeedRM)
{
    IEMOP_MNEMONIC(InvalidNeedRM, "InvalidNeedRM");
    if (pVCpu->iem.s.enmCpuVendor == CPUMCPUVENDOR_INTEL)
    {
        uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); RT_NOREF(bRm);
        if (IEM_IS_MODRM_MEM_MODE(bRm))
            IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES(bRm);
    }
    IEMOP_HLP_DONE_DECODING();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#endif


#if defined(TST_IEM_CHECK_MC) || defined(IEM_WITH_ONE_BYTE_TABLE)
/** Invalid opcode where both AMD and Intel requires Mod R/M sequence. */
FNIEMOP_DEF(iemOp_InvalidAllNeedRM)
{
    IEMOP_MNEMONIC(InvalidAllNeedRM, "InvalidAllNeedRM");
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); RT_NOREF(bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
        IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES(bRm);
    IEMOP_HLP_DONE_DECODING();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#endif


#if defined(TST_IEM_CHECK_MC) || defined(IEM_WITH_TWO_BYTE_TABLE) || defined(IEM_WITH_THREE_BYTE_TABLES) || defined(IEM_WITH_VEX_TABLES)
/** Invalid opcode where intel requires Mod R/M sequence and 8-byte
 *  immediate. */
FNIEMOP_DEF(iemOp_InvalidNeedRMImm8)
{
    IEMOP_MNEMONIC(InvalidNeedRMImm8, "InvalidNeedRMImm8");
    if (pVCpu->iem.s.enmCpuVendor == CPUMCPUVENDOR_INTEL)
    {
        uint8_t bRm;  IEM_OPCODE_GET_NEXT_U8(&bRm);  RT_NOREF(bRm);
        if (IEM_IS_MODRM_MEM_MODE(bRm))
            IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES(bRm);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); RT_NOREF(bImm);
    }
    IEMOP_HLP_DONE_DECODING();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#endif


#if defined(TST_IEM_CHECK_MC) || defined(IEM_WITH_TWO_BYTE_TABLE)
/** Invalid opcode where intel requires a 3rd escape byte and a Mod R/M
 *  sequence. */
FNIEMOP_DEF(iemOp_InvalidNeed3ByteEscRM)
{
    IEMOP_MNEMONIC(InvalidNeed3ByteEscRM, "InvalidNeed3ByteEscRM");
    if (pVCpu->iem.s.enmCpuVendor == CPUMCPUVENDOR_INTEL)
    {
        uint8_t b3rd; IEM_OPCODE_GET_NEXT_U8(&b3rd); RT_NOREF(b3rd);
        uint8_t bRm;  IEM_OPCODE_GET_NEXT_U8(&bRm);  RT_NOREF(bRm);
        if (IEM_IS_MODRM_MEM_MODE(bRm))
            IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES(bRm);
    }
    IEMOP_HLP_DONE_DECODING();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#endif


#if defined(TST_IEM_CHECK_MC) || defined(IEM_WITH_TWO_BYTE_TABLE)
/** Invalid opcode where intel requires a 3rd escape byte, Mod R/M sequence, and
 *  a 8-byte immediate. */
FNIEMOP_DEF(iemOp_InvalidNeed3ByteEscRMImm8)
{
    IEMOP_MNEMONIC(InvalidNeed3ByteEscRMImm8, "InvalidNeed3ByteEscRMImm8");
    if (pVCpu->iem.s.enmCpuVendor == CPUMCPUVENDOR_INTEL)
    {
        uint8_t b3rd; IEM_OPCODE_GET_NEXT_U8(&b3rd); RT_NOREF(b3rd);
        uint8_t bRm;  IEM_OPCODE_GET_NEXT_U8(&bRm);  RT_NOREF(bRm);
        if (IEM_IS_MODRM_MEM_MODE(bRm))
            IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES(bRm);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); RT_NOREF(bImm);
    }
    IEMOP_HLP_DONE_DECODING();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#endif

#if defined(IEM_WITH_ONE_BYTE_TABLE) || defined(IEM_WITH_TWO_BYTE_TABLE)

/**
 * Common 'push segment-register' helper.
 */
FNIEMOP_DEF_1(iemOpCommonPushSReg, uint8_t, iReg)
{
    Assert(iReg >= X86_SREG_FS || !IEM_IS_64BIT_CODE(pVCpu));
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE();

    switch (pVCpu->iem.s.enmEffOpSize)
    {
        case IEMMODE_16BIT:
            IEM_MC_BEGIN(0, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_LOCAL(uint16_t, u16Value);
            IEM_MC_FETCH_SREG_U16(u16Value, iReg);
            IEM_MC_PUSH_U16(u16Value);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
            break;

        case IEMMODE_32BIT:
            IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_LOCAL(uint32_t, u32Value);
            IEM_MC_FETCH_SREG_ZX_U32(u32Value, iReg);
            IEM_MC_PUSH_U32_SREG(u32Value); /* Intel CPUs do funny things with this instruction. */
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
            break;

        case IEMMODE_64BIT:
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_LOCAL(uint64_t, u64Value);
            IEM_MC_FETCH_SREG_ZX_U64(u64Value, iReg);
            IEM_MC_PUSH_U64(u64Value);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
            break;

        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }
}


FNIEMOP_DEF_2(iemOpCommonLoadSRegAndGreg, uint8_t, iSegReg, uint8_t, bRm)
{
    Assert(IEM_IS_MODRM_MEM_MODE(bRm)); /* Caller checks this */
    uint8_t const iGReg = IEM_GET_MODRM_REG(pVCpu, bRm);

    switch (pVCpu->iem.s.enmEffOpSize)
    {
        case IEMMODE_16BIT:
            if (iSegReg >= X86_SREG_FS || !IEM_IS_32BIT_CODE(pVCpu)) /* IEM_CIMPL_F_XXX flag are combined for whole MC block, */
            {                                                        /* thus the duplication. */
                IEM_MC_BEGIN(0, 0);
                IEM_MC_LOCAL(RTGCPTR,       GCPtrEff);
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_ARG(uint16_t,        offSeg,                                  1);
                IEM_MC_FETCH_MEM_U16(offSeg, pVCpu->iem.s.iEffSeg, GCPtrEff); /** @todo check memory access pattern */
                IEM_MC_ARG(uint16_t,        uSel,                                    0);
                IEM_MC_FETCH_MEM_U16_DISP(uSel, pVCpu->iem.s.iEffSeg, GCPtrEff, 2);
                IEM_MC_ARG_CONST(uint8_t,   iSegRegArg,/*=*/iSegReg,                 2);
                IEM_MC_ARG_CONST(uint8_t,   iGRegArg,  /*=*/iGReg,                   3);
                IEM_MC_ARG_CONST(IEMMODE,   enmEffOpSize,/*=*/pVCpu->iem.s.enmEffOpSize, 4);
                IEM_MC_CALL_CIMPL_5(               0,
                                      RT_BIT_64(kIemNativeGstReg_GprFirst       + iGReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegSelFirst    + iSegReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegBaseFirst   + iSegReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegLimitFirst  + iSegReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegAttribFirst + iSegReg),
                                    iemCImpl_load_SReg_Greg, uSel, offSeg, iSegRegArg, iGRegArg, enmEffOpSize);
                IEM_MC_END();
            }
            else
            {
                IEM_MC_BEGIN(0, 0);
                IEM_MC_LOCAL(RTGCPTR,       GCPtrEff);
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_ARG(uint16_t,        offSeg,                                  1);
                IEM_MC_FETCH_MEM_U16(offSeg, pVCpu->iem.s.iEffSeg, GCPtrEff); /** @todo check memory access pattern */
                IEM_MC_ARG(uint16_t,        uSel,                                    0);
                IEM_MC_FETCH_MEM_U16_DISP(uSel, pVCpu->iem.s.iEffSeg, GCPtrEff, 2);
                IEM_MC_ARG_CONST(uint8_t,   iSegRegArg,/*=*/iSegReg,                 2);
                IEM_MC_ARG_CONST(uint8_t,   iGRegArg,  /*=*/iGReg,                   3);
                IEM_MC_ARG_CONST(IEMMODE,   enmEffOpSize,/*=*/pVCpu->iem.s.enmEffOpSize, 4);
                IEM_MC_CALL_CIMPL_5(IEM_CIMPL_F_MODE,
                                      RT_BIT_64(kIemNativeGstReg_GprFirst       + iGReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegSelFirst    + iSegReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegBaseFirst   + iSegReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegLimitFirst  + iSegReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegAttribFirst + iSegReg),
                                    iemCImpl_load_SReg_Greg, uSel, offSeg, iSegRegArg, iGRegArg, enmEffOpSize);
                IEM_MC_END();
            }

        case IEMMODE_32BIT:
            if (iSegReg >= X86_SREG_FS || !IEM_IS_32BIT_CODE(pVCpu))
            {
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEM_MC_LOCAL(RTGCPTR,       GCPtrEff);
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_ARG(uint32_t,        offSeg,                                  1);    /** @todo check memory access pattern */
                IEM_MC_FETCH_MEM_U32(offSeg, pVCpu->iem.s.iEffSeg, GCPtrEff);
                IEM_MC_ARG(uint16_t,        uSel,                                    0);
                IEM_MC_FETCH_MEM_U16_DISP(uSel, pVCpu->iem.s.iEffSeg, GCPtrEff, 4);
                IEM_MC_ARG_CONST(uint8_t,   iSegRegArg,/*=*/iSegReg,                 2);
                IEM_MC_ARG_CONST(uint8_t,   iGRegArg,  /*=*/iGReg,                   3);
                IEM_MC_ARG_CONST(IEMMODE,   enmEffOpSize,/*=*/pVCpu->iem.s.enmEffOpSize, 4);
                IEM_MC_CALL_CIMPL_5(               0,
                                      RT_BIT_64(kIemNativeGstReg_GprFirst       + iGReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegSelFirst    + iSegReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegBaseFirst   + iSegReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegLimitFirst  + iSegReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegAttribFirst + iSegReg),
                                    iemCImpl_load_SReg_Greg, uSel, offSeg, iSegRegArg, iGRegArg, enmEffOpSize);
                IEM_MC_END();
            }
            else
            {
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEM_MC_LOCAL(RTGCPTR,       GCPtrEff);
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_ARG(uint32_t,        offSeg,                                  1);    /** @todo check memory access pattern */
                IEM_MC_FETCH_MEM_U32(offSeg, pVCpu->iem.s.iEffSeg, GCPtrEff);
                IEM_MC_ARG(uint16_t,        uSel,                                    0);
                IEM_MC_FETCH_MEM_U16_DISP(uSel, pVCpu->iem.s.iEffSeg, GCPtrEff, 4);
                IEM_MC_ARG_CONST(uint8_t,   iSegRegArg,/*=*/iSegReg,                 2);
                IEM_MC_ARG_CONST(uint8_t,   iGRegArg,  /*=*/iGReg,                   3);
                IEM_MC_ARG_CONST(IEMMODE,   enmEffOpSize,/*=*/pVCpu->iem.s.enmEffOpSize, 4);
                IEM_MC_CALL_CIMPL_5(IEM_CIMPL_F_MODE,
                                      RT_BIT_64(kIemNativeGstReg_GprFirst       + iGReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegSelFirst    + iSegReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegBaseFirst   + iSegReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegLimitFirst  + iSegReg)
                                    | RT_BIT_64(kIemNativeGstReg_SegAttribFirst + iSegReg),
                                    iemCImpl_load_SReg_Greg, uSel, offSeg, iSegRegArg, iGRegArg, enmEffOpSize);
                IEM_MC_END();
            }

        case IEMMODE_64BIT:
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_ARG(uint16_t,        uSel,                                    0);
            IEM_MC_ARG(uint64_t,        offSeg,                                  1);
            IEM_MC_LOCAL(RTGCPTR,       GCPtrEff);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
IEM_MC_NO_NATIVE_RECOMPILE(); /** @todo sort out the IEM_IS_GUEST_CPU_AMD stuff. */
            if (IEM_IS_GUEST_CPU_AMD(pVCpu)) /** @todo testcase: rev 3.15 of the amd manuals claims it only loads a 32-bit greg. */
                IEM_MC_FETCH_MEM_U32_SX_U64(offSeg, pVCpu->iem.s.iEffSeg, GCPtrEff);
            else
                IEM_MC_FETCH_MEM_U64(offSeg, pVCpu->iem.s.iEffSeg, GCPtrEff);
            IEM_MC_FETCH_MEM_U16_DISP(uSel, pVCpu->iem.s.iEffSeg, GCPtrEff, 8);
            IEM_MC_ARG_CONST(uint8_t,   iSegRegArg,/*=*/iSegReg,                 2);
            IEM_MC_ARG_CONST(uint8_t,   iGRegArg,  /*=*/iGReg,                   3);
            IEM_MC_ARG_CONST(IEMMODE,   enmEffOpSize,/*=*/pVCpu->iem.s.enmEffOpSize, 4);
            IEM_MC_CALL_CIMPL_5(0,
                                  RT_BIT_64(kIemNativeGstReg_GprFirst       + iGReg)
                                | RT_BIT_64(kIemNativeGstReg_SegSelFirst    + iSegReg)
                                | RT_BIT_64(kIemNativeGstReg_SegBaseFirst   + iSegReg)
                                | RT_BIT_64(kIemNativeGstReg_SegLimitFirst  + iSegReg)
                                | RT_BIT_64(kIemNativeGstReg_SegAttribFirst + iSegReg),
                                iemCImpl_load_SReg_Greg, uSel, offSeg, iSegRegArg, iGRegArg, enmEffOpSize);
            IEM_MC_END();

        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }
}

#endif

#endif /* !VMM_INCLUDED_SRC_VMMAll_IEMAllInstCommon_cpp_h */
