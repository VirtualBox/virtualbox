/* $Id: bs3-cpu-instr-2-template.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - bs3-cpu-instr-2, C code template.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include "bs3-cpu-instr-2.h"
#include "bs3-cpu-instr-2-data.h"
#include "bs3-cpu-instr-2-asm-auto.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#ifdef BS3_INSTANTIATING_CMN
# if ARCH_BITS == 64
typedef struct BS3CI2FSGSBASE
{
    const char *pszDesc;
    bool        f64BitOperand;
    FPFNBS3FAR  pfnWorker;
    uint8_t     offWorkerUd2;
    FPFNBS3FAR  pfnVerifyWorker;
    uint8_t     offVerifyWorkerUd2;
} BS3CI2FSGSBASE;
# endif
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef BS3_INSTANTIATING_CMN
# if ARCH_BITS == 64
static BS3CI2FSGSBASE const s_aWrFsBaseWorkers[] =
{
    { "wrfsbase rbx", true,  BS3_CMN_NM(bs3CpuInstr2_wrfsbase_rbx_ud2), 5, BS3_CMN_NM(bs3CpuInstr2_wrfsbase_rbx_rdfsbase_rcx_ud2), 15 },
    { "wrfsbase ebx", false, BS3_CMN_NM(bs3CpuInstr2_wrfsbase_ebx_ud2), 4, BS3_CMN_NM(bs3CpuInstr2_wrfsbase_ebx_rdfsbase_ecx_ud2), 13 },
};

static BS3CI2FSGSBASE const s_aWrGsBaseWorkers[] =
{
    { "wrgsbase rbx", true,  BS3_CMN_NM(bs3CpuInstr2_wrgsbase_rbx_ud2), 5, BS3_CMN_NM(bs3CpuInstr2_wrgsbase_rbx_rdgsbase_rcx_ud2), 15 },
    { "wrgsbase ebx", false, BS3_CMN_NM(bs3CpuInstr2_wrgsbase_ebx_ud2), 4, BS3_CMN_NM(bs3CpuInstr2_wrgsbase_ebx_rdgsbase_ecx_ud2), 13 },
};

static BS3CI2FSGSBASE const s_aRdFsBaseWorkers[] =
{
    { "rdfsbase rbx", true,  BS3_CMN_NM(bs3CpuInstr2_rdfsbase_rbx_ud2), 5, BS3_CMN_NM(bs3CpuInstr2_wrfsbase_rbx_rdfsbase_rcx_ud2), 15 },
    { "rdfsbase ebx", false, BS3_CMN_NM(bs3CpuInstr2_rdfsbase_ebx_ud2), 4, BS3_CMN_NM(bs3CpuInstr2_wrfsbase_ebx_rdfsbase_ecx_ud2), 13 },
};

static BS3CI2FSGSBASE const s_aRdGsBaseWorkers[] =
{
    { "rdgsbase rbx", true,  BS3_CMN_NM(bs3CpuInstr2_rdgsbase_rbx_ud2), 5, BS3_CMN_NM(bs3CpuInstr2_wrgsbase_rbx_rdgsbase_rcx_ud2), 15 },
    { "rdgsbase ebx", false, BS3_CMN_NM(bs3CpuInstr2_rdgsbase_ebx_ud2), 4, BS3_CMN_NM(bs3CpuInstr2_wrgsbase_ebx_rdgsbase_ecx_ud2), 13 },
};
# endif
#endif /* BS3_INSTANTIATING_CMN - global */


/*
 * Common code.
 * Common code.
 * Common code.
 */
#ifdef BS3_INSTANTIATING_CMN

/*
 * Basic binary arithmetic tests.
 */

# if ARCH_BITS == 64                                                                           /* fDstMem      cBitsImm */
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_8_64BIT(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _sil_dil),      X86_GREG_xSI,    X86_GREG_xDI,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r9b_r8b),      X86_GREG_x9,     X86_GREG_x8,     false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _al_r13b),      X86_GREG_xAX,    X86_GREG_x13,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSx14_r11b),   X86_GREG_x14,    X86_GREG_x11,    true,  false,  0 },
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_8_64BIT(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r8b_Ib),       X86_GREG_x8,     X86_GREG_x15,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r14b_Ib),      X86_GREG_x14,    X86_GREG_x15,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSx13_Ib),     X86_GREG_x13,    X86_GREG_x15,    true,  false,  8 },
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_8_64BIT(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_alt_ ## a_Ins ## _dl_r14b),  X86_GREG_xDX,    X86_GREG_x14,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_alt_ ## a_Ins ## _r8b_bl),   X86_GREG_x8,     X86_GREG_xBX,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r11b_DSx12),   X86_GREG_x11,    X86_GREG_x12,    false, true,   0 },
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_16_64BIT(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r8w_cx),       X86_GREG_x8,     X86_GREG_xCX,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r15w_r10w),    X86_GREG_x15,    X86_GREG_x10,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSx15_r12w),   X86_GREG_x15,    X86_GREG_x12,    true,  false,  0 },
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B_64BIT(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r8w_Ib),       X86_GREG_x8,     X86_GREG_xBX,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r12w_Ib),      X86_GREG_x12,    X86_GREG_xBX,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _wDSx14_Ib),    X86_GREG_x14,    X86_GREG_xBX,    true,  false,  8 },
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16W_64BIT(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r8w_Iw),       X86_GREG_x8,     X86_GREG_xBX,    false, false, 16 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r13w_Iw),      X86_GREG_x13,    X86_GREG_xBX,    false, false, 16 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSx11_Iw),     X86_GREG_x11,    X86_GREG_xBX,    true,  false, 16 },
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_16_64BIT(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_alt_ ## a_Ins ## _r13w_ax),  X86_GREG_x13,    X86_GREG_xAX,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_alt_ ## a_Ins ## _si_r9w),   X86_GREG_xSI,    X86_GREG_x9,     false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r9w_DSx8),     X86_GREG_x9,     X86_GREG_x8,     false, true,   0 },
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_32_64BIT(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _eax_r8d),      X86_GREG_xAX,    X86_GREG_x8,     false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r9d_ecx),      X86_GREG_x9,     X86_GREG_xCX,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r13d_r14d),    X86_GREG_x13,    X86_GREG_x14,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSx10_r11d),   X86_GREG_x10,    X86_GREG_x11,    true,  false,  0 },
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B_64BIT(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r8d_Ib),       X86_GREG_x8,     X86_GREG_xBX,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r11d_Ib),      X86_GREG_x11,    X86_GREG_xBX,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _dwDSx15_Ib),   X86_GREG_x15,    X86_GREG_xBX,    true,  false,  8 },
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32DW_64BIT(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r8d_Id),       X86_GREG_x8,     X86_GREG_xBX,    false, false, 32 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r14d_Id),      X86_GREG_x14,    X86_GREG_xBX,    false, false, 32 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSx12_Id),     X86_GREG_x12,    X86_GREG_xBX,    true,  false, 32 },
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_32_64BIT(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_alt_ ## a_Ins ## _r15d_esi), X86_GREG_x15,    X86_GREG_xSI,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_alt_ ## a_Ins ## _eax_r10d), X86_GREG_xAX,    X86_GREG_x10,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r14d_DSx12),   X86_GREG_x14,    X86_GREG_x12,    false, true,   0 },

# define BS3CPUINSTR2CMNBINTEST_ENTRIES_64(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _rax_rbx),      X86_GREG_xAX,    X86_GREG_xBX,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r8_rax),       X86_GREG_x8,     X86_GREG_xAX,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _rdx_r10),      X86_GREG_xDX,    X86_GREG_x10,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSxBX_rax),    X86_GREG_xBX,    X86_GREG_xAX,    true,  false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSx12_r8),     X86_GREG_x12,    X86_GREG_x8,     true,  false,  0 },
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_64(a_Ins) \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_64(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_alt_ ## a_Ins ## _r15_rsi),  X86_GREG_x15,    X86_GREG_xSI,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_alt_ ## a_Ins ## _rbx_r14),  X86_GREG_xBX,    X86_GREG_x14,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _rax_DSxBX),    X86_GREG_xAX,    X86_GREG_xBX,    false, true,   0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r8_DSx12),     X86_GREG_x8,     X86_GREG_x12,    false, true,   0 },

# define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64B(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _rax_Ib),       X86_GREG_xAX,    X86_GREG_x15,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _rbp_Ib),       X86_GREG_xBP,    X86_GREG_x15,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r8_Ib),        X86_GREG_x8,     X86_GREG_x15,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r11_Ib),       X86_GREG_x11,    X86_GREG_x15,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _qwDSxSI_Ib),   X86_GREG_xSI,    X86_GREG_x15,    true,  false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _qwDSx8_Ib),    X86_GREG_x8,     X86_GREG_x15,    true,  false,  8 },

# define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64DW(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _rax_Id),       X86_GREG_xAX,    X86_GREG_x15,    false, false, 32 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r8_Id),        X86_GREG_x8,     X86_GREG_x15,    false, false, 32 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _rbx_Id),       X86_GREG_xBX,    X86_GREG_x15,    false, false, 32 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r14_Id),       X86_GREG_x14,    X86_GREG_x15,    false, false, 32 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _qwDSx12_Id),   X86_GREG_x12,    X86_GREG_x15,    true,  false, 32 },

# else
#  define BS3CPUINSTR2CMNBINTEST_ENTRIES_8_64BIT(aIns)
#  define BS3CPUINSTR2CMNBINTEST_ENTRIES_16_64BIT(aIns)
#  define BS3CPUINSTR2CMNBINTEST_ENTRIES_32_64BIT(aIns)
#  define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_8_64BIT(aIns)
#  define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B_64BIT(aIns)
#  define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16W_64BIT(aIns)
#  define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B_64BIT(aIns)
#  define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32DW_64BIT(aIns)
#  define BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_8_64BIT(aIns)
#  define BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_16_64BIT(aIns)
#  define BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_32_64BIT(aIns)
# endif

# define BS3CPUINSTR2CMNBINTEST_ENTRIES_8(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _al_dl),       X86_GREG_xAX,     X86_GREG_xDX,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _ch_bh),       X86_GREG_xCX+16,  X86_GREG_xBX+16, false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _dl_ah),       X86_GREG_xDX,     X86_GREG_xAX+16, false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSxBX_ah),    X86_GREG_xBX,     X86_GREG_xAX+16, true,  false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSxDI_bl),    X86_GREG_xDI,     X86_GREG_xBX,    true,  false,  0 }, \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_8_64BIT(a_Ins)
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_8(a_Ins) \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_8(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_alt_ ## a_Ins ## _dh_cl),   X86_GREG_xDX+16,  X86_GREG_xCX,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _dl_DSxBX),    X86_GREG_xDX,     X86_GREG_xBX,    false, true,   0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _ch_DSxBX),    X86_GREG_xCX+16,  X86_GREG_xBX,    false, true,   0 }, \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_8_64BIT(a_Ins)

# define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_8(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _al_Ib),       X86_GREG_xAX,     X86_GREG_x15,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _cl_Ib),       X86_GREG_xCX,     X86_GREG_x15,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _dh_Ib),       X86_GREG_xDX+16,  X86_GREG_x15,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSxDI_Ib),    X86_GREG_xDI,     X86_GREG_x15,    true,  false,  8 }, \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_8_64BIT(a_Ins)

# define BS3CPUINSTR2CMNBINTEST_ENTRIES_16(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _di_si),       X86_GREG_xDI,     X86_GREG_xSI,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _cx_bp),       X86_GREG_xCX,     X86_GREG_xBP,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSxDI_si),    X86_GREG_xDI,     X86_GREG_xSI,    true,  false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSxBX_ax),    X86_GREG_xBX,     X86_GREG_xAX,    true,  false,  0 }, \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_16_64BIT(a_Ins)
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_16(a_Ins) \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_16(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_alt_ ## a_Ins ## _bp_bx),   X86_GREG_xBP,     X86_GREG_xBX,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _si_DSxDI),    X86_GREG_xSI,     X86_GREG_xDI,    false, true,   0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _ax_DSxBX),    X86_GREG_xAX,     X86_GREG_xBX,    false, true,   0 }, \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_16_64BIT(a_Ins)

# define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _ax_Ib),       X86_GREG_xAX,     X86_GREG_x15,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _si_Ib),       X86_GREG_xSI,     X86_GREG_x15,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _wDSxDI_Ib),   X86_GREG_xDI,     X86_GREG_x15,    true,  false,  8 }, \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B_64BIT(a_Ins)

# define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16W(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _ax_Iw),       X86_GREG_xAX,     X86_GREG_x15,    false, false, 16 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _bx_Iw),       X86_GREG_xBX,     X86_GREG_x15,    false, false, 16 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSxBX_Iw),    X86_GREG_xBX,     X86_GREG_x15,    true,  false, 16 }, \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16W_64BIT(a_Ins)


# define BS3CPUINSTR2CMNBINTEST_ENTRIES_32(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _eax_ebx),     X86_GREG_xAX,     X86_GREG_xBX,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _ecx_ebp),     X86_GREG_xCX,     X86_GREG_xBP,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _edx_edi),     X86_GREG_xDX,     X86_GREG_xDI,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSxDI_esi),   X86_GREG_xDI,     X86_GREG_xSI,    true,  false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSxBX_eax),   X86_GREG_xBX,     X86_GREG_xAX,    true,  false,  0 }, \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_32_64BIT(a_Ins)
# define BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_32(a_Ins) \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_32(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_alt_ ## a_Ins ## _edi_esi), X86_GREG_xDI,     X86_GREG_xSI,    false, false,  0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _eax_DSxBX),   X86_GREG_xAX,     X86_GREG_xBX,    false, true,   0 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _ebp_DSxDI),   X86_GREG_xBP,     X86_GREG_xDI,    false, true,   0 }, \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_32_64BIT(a_Ins)

# define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _eax_Ib),      X86_GREG_xAX,     X86_GREG_x15,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _ecx_Ib),      X86_GREG_xCX,     X86_GREG_x15,    false, false,  8 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _dwDSxDI_Ib),  X86_GREG_xDI,     X86_GREG_x15,    true,  false,  8 }, \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B_64BIT(a_Ins)

# define BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32DW(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _eax_Id),      X86_GREG_xAX,     X86_GREG_x15,    false, false, 32 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _ebp_Id),      X86_GREG_xBP,     X86_GREG_x15,    false, false, 32 }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _DSxSI_Id),    X86_GREG_xSI,     X86_GREG_x15,    true,  false, 32 }, \
        BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32DW_64BIT(a_Ins)


typedef struct BS3CPUINSTR2CMNBINTEST
{
    FPFNBS3FAR  pfnWorker;
    uint8_t     idxDstReg;
    uint8_t     idxSrcReg;
    bool        fDstMem : 1;
    bool        fSrcMem : 1;
    uint8_t     cBitsImm;
} BS3CPUINSTR2CMNBINTEST;
typedef BS3CPUINSTR2CMNBINTEST const BS3_FAR_DATA *PCBS3CPUINSTR2CMNBINTEST;


static uint16_t const g_auEflStatusBitsVars[] =
{
    0,
    X86_EFL_STATUS_BITS,
    X86_EFL_CF,
    X86_EFL_PF,
    X86_EFL_AF,
    X86_EFL_ZF,
    X86_EFL_SF,
    X86_EFL_OF,
    X86_EFL_PF | X86_EFL_AF,
};


DECLINLINE(void RT_FAR *) Code2RwPtr(void RT_FAR *pfn)
{
#if ARCH_BITS == 16
    if (!BS3_MODE_IS_RM_OR_V86(g_bBs3CurrentMode))
        pfn = BS3_FP_MAKE(BS3_SEL_TILED + 8, BS3_FP_OFF(pfn)); /* ASSUMES CS */
#endif
    return pfn;
}

#define BS3CPUINSTR2_COMMON_BINARY_U(a_cBits, a_UIntType, a_UIntImmType, a_szFmt) \
static uint8_t \
RT_CONCAT(bs3CpuInstr2_CommonBinaryU,a_cBits)(uint8_t bMode, PCBS3CPUINSTR2CMNBINTEST paTests, unsigned cTests, uint16_t fPassthruEfl, \
                                              RT_CONCAT(PCBS3CPUINSTR2BIN,a_cBits) paTestData, unsigned cTestData, bool fCarryIn, \
                                              bool fMaskSrcWhenMemDst, bool fReadOnly) \
{ \
    BS3REGCTX       Ctx; \
    BS3REGCTX       CtxExpect; \
    BS3TRAPFRAME    TrapFrame; \
    unsigned        iTest; \
    struct \
    { \
        char        achPreGuard[8]; \
        a_UIntType  uData; \
        char        achPostGuard[8]; \
    } Buf = { { '0','1','2','3','4','5','6','7' }, 0, { '8','9','a','b','c','d','e','f'} }; \
    a_UIntType      uMemExpect = 0; \
    a_UIntType      uMemDummy  = 0; \
    \
    /* Ensure the structures are allocated before we sample the stack pointer. */ \
    Bs3MemSet(&Ctx, 0, sizeof(Ctx)); \
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame)); \
    \
    /* \
     * Create test context. \
     */ \
    Bs3RegCtxSaveEx(&Ctx, bMode, 640); \
    Ctx.rflags.u32 &= ~X86_EFL_RF; \
    if (ARCH_BITS == 64) \
        for (iTest = 0; iTest < 16; iTest++) \
            if (iTest != X86_GREG_xSP) \
                (&Ctx.rax)[iTest].au32[1] = UINT32_C(0x8572ade) << (iTest & 7); \
    Bs3MemCpy(&CtxExpect, &Ctx, sizeof(CtxExpect)); \
    if (!BS3_MODE_IS_16BIT_SYS(bMode)) \
        CtxExpect.rflags.u32 |= X86_EFL_RF; \
    \
    /* \
     * Each test worker. \
     */ \
    for (iTest = 0; iTest < cTests; iTest++) \
    { \
        uint8_t const               cbInstr        = ((uint8_t BS3_FAR *)paTests[iTest].pfnWorker)[-1]; /* the function is prefixed by the length */ \
        uint8_t RT_FAR * const      pbImm          = (uint8_t BS3_FAR *)Code2RwPtr(&((uint8_t BS3_FAR *)paTests[iTest].pfnWorker)[cbInstr - 1]); \
        a_UIntImmType RT_FAR * const puImm         = (a_UIntImmType RT_FAR *)Code2RwPtr(&((uint8_t BS3_FAR *)paTests[iTest].pfnWorker)[cbInstr - sizeof(a_UIntImmType)]); \
        unsigned const              idxDstReg      = paTests[iTest].idxDstReg; \
        unsigned const              idxSrcReg      = paTests[iTest].idxSrcReg; \
        uint16_t const              SavedDs        = Ctx.ds; \
        BS3REG const                SavedDst       =  (&Ctx.rax)[idxDstReg & 15]; /* saves memptr too */ \
        BS3REG const                SavedSrc       =  (&Ctx.rax)[idxSrcReg & 15]; /* ditto */ \
        a_UIntType RT_FAR * const   puCtxDst       = paTests[iTest].fDstMem ? &Buf.uData \
                                                   : &(&Ctx.rax)[idxDstReg & 15].RT_CONCAT(au,a_cBits)[idxDstReg >> 4]; \
        a_UIntType RT_FAR * const   puCtxSrc       = paTests[iTest].fSrcMem ? &Buf.uData \
                                                   : paTests[iTest].cBitsImm == 0 \
                                                   ? &(&Ctx.rax)[idxSrcReg & 15].RT_CONCAT(au,a_cBits)[idxSrcReg >> 4] \
                                                   : &uMemDummy; \
        a_UIntType RT_FAR * const   puCtxExpectDst = paTests[iTest].fDstMem ? &uMemExpect \
                                                   : &(&CtxExpect.rax)[idxDstReg & 15].RT_CONCAT(au,a_cBits)[idxDstReg >> 4]; \
        a_UIntType RT_FAR * const   puCtxExpectSrc = paTests[iTest].fSrcMem ? &uMemExpect \
                                                   : paTests[iTest].cBitsImm == 0 \
                                                   ? &(&CtxExpect.rax)[idxSrcReg & 15].RT_CONCAT(au,a_cBits)[idxSrcReg >> 4] \
                                                   : &uMemDummy; \
        uint64_t RT_FAR * const     puMemPtrReg    = paTests[iTest].fDstMem ? &(&Ctx.rax)[idxDstReg & 15].u \
                                                   : paTests[iTest].fSrcMem ? &(&Ctx.rax)[idxSrcReg & 15].u : NULL; \
        uint64_t RT_FAR * const     puMemPtrRegExpt= paTests[iTest].fDstMem ? &(&CtxExpect.rax)[idxDstReg & 15].u \
                                                   : paTests[iTest].fSrcMem ? &(&CtxExpect.rax)[idxSrcReg & 15].u : NULL; \
        unsigned                    iTestData; \
        /*Bs3TestPrintf("pfnWorker=%p\n", paTests[iTest].pfnWorker);*/ \
        \
        Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, paTests[iTest].pfnWorker); \
        CtxExpect.rip.u = Ctx.rip.u + cbInstr; \
        CtxExpect.cs    = Ctx.cs; \
        \
        if (puMemPtrReg) \
            CtxExpect.ds = Ctx.ds = Ctx.ss; \
        \
        /* \
         * Loop over the test data and feed it to the worker. \
         */\
        for (iTestData = 0; iTestData < cTestData; iTestData++) \
        { \
            unsigned iRecompiler; \
            a_UIntType const uSrc = !fMaskSrcWhenMemDst | !paTests[iTest].fDstMem \
                                  ? paTestData[iTestData].uSrc2 : paTestData[iTestData].uSrc2 & (a_cBits - 1); \
            if (!paTests[iTest].cBitsImm) \
            { \
                *puCtxSrc             = uSrc; \
                *puCtxExpectSrc       = uSrc; \
            } \
            else if (paTests[iTest].cBitsImm == 8) \
            { \
                if ((int8_t)uSrc == (int##a_cBits##_t)uSrc) \
                    *pbImm = (uint8_t)uSrc; \
                else continue; \
            } \
            else if (sizeof(*puImm) == sizeof(*puCtxSrc) || (int32_t)uSrc == (int64_t)uSrc) \
                *puImm = (a_UIntImmType)uSrc; \
            else continue; \
            \
            *puCtxDst             = paTestData[iTestData].uSrc1; \
            *puCtxExpectDst       = paTestData[iTestData].uResult; \
            if (a_cBits == 32 && !fReadOnly && !paTests[iTest].fDstMem) \
                puCtxExpectDst[1] = 0; \
            \
            if (puMemPtrReg) \
            { \
                *puMemPtrReg     = BS3_FP_OFF(&Buf.uData); \
                *puMemPtrRegExpt = BS3_FP_OFF(&Buf.uData); \
            } \
            \
            CtxExpect.rflags.u16 &= ~X86_EFL_STATUS_BITS; \
            CtxExpect.rflags.u16 |= paTestData[iTestData].fEflOut & X86_EFL_STATUS_BITS; \
            \
            /* \
             * Do input the eight EFLAGS variations three times, so we're sure to trigger \
             * native recompilation of the test worker code. \
             */ \
            for (iRecompiler = 0; iRecompiler < 2; iRecompiler++) \
            { \
                unsigned iEflVar = 0; \
                for (iEflVar = 0; iEflVar < RT_ELEMENTS(g_auEflStatusBitsVars); iEflVar++) \
                { \
                    if (paTests[iTest].fDstMem) \
                        *puCtxDst = paTestData[iTestData].uSrc1; \
                    \
                    Ctx.rflags.u16 &= ~X86_EFL_STATUS_BITS; \
                    if (!fCarryIn) \
                        Ctx.rflags.u16 |= g_auEflStatusBitsVars[iEflVar]; \
                    else \
                        Ctx.rflags.u16 |= (g_auEflStatusBitsVars[iEflVar] & ~X86_EFL_CF) \
                                       |  (paTestData[iTestData].fEflOut >> BS3CPUINSTR2BIN_EFL_CARRY_IN_BIT) & X86_EFL_CF; \
                    if (fPassthruEfl) \
                        CtxExpect.rflags.u16 = (CtxExpect.rflags.u16 & ~fPassthruEfl) | (Ctx.rflags.u16 & fPassthruEfl); \
                    \
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame); \
                    if (TrapFrame.bXcpt != X86_XCPT_UD) \
                    { \
                        Bs3TestFailedF("Expected #UD got %#x", TrapFrame.bXcpt); \
                        Bs3TrapPrintFrame(&TrapFrame); \
                    } \
                    else if (Bs3TestCheckRegCtxEx(&TrapFrame.Ctx, &CtxExpect, 0 /*cbPcAdjust*/,  0 /*cbSpAdjust*/, \
                                                  0 /*fExtraEfl*/, "mode", (iTest << 8) | (iTestData & 0xff))) \
                    { \
                        if (!puMemPtrReg) \
                            continue; \
                        if (paTests[iTest].fDstMem && Buf.uData != uMemExpect) \
                            Bs3TestPrintf("Wrong memory result: %" a_szFmt ", expected %" a_szFmt "\n", Buf.uData, uMemExpect); \
                        else if (!paTests[iTest].fDstMem && Buf.uData != uSrc) \
                            Bs3TestPrintf("Memory input result modified: %" a_szFmt ", expected %" a_szFmt "\n", Buf.uData, uSrc); \
                        else \
                            continue; \
                    } \
                    /*else { Bs3RegCtxPrint(&Ctx); Bs3TrapPrintFrame(&TrapFrame); }*/ \
                    Bs3TestPrintf(#a_cBits ": iTest=%u iData=%u: uSrc1=%" a_szFmt "%s uSrc2=%" a_szFmt "%s %s-> %" a_szFmt "\n", \
                                  iTest, iTestData, paTestData[iTestData].uSrc1, paTests[iTest].fDstMem ? " mem" : "", \
                                  paTestData[iTestData].uSrc2, paTests[iTest].fSrcMem ? " mem" : "", \
                                  !fCarryIn ? "" : Ctx.rflags.u16 & X86_EFL_CF ? "CF " : "NC ", \
                                  paTestData[iTestData].uResult); \
                    Bs3RegCtxPrint(&Ctx); Bs3TrapPrintFrame(&TrapFrame); \
                    ASMHalt(); \
                    iRecompiler = ~0U - 1; \
                    break; \
                } \
            } \
        } \
        \
        /* Restore modified context registers (except EFLAGS). */ \
        CtxExpect.ds                       = Ctx.ds                       = SavedDs; \
        (&CtxExpect.rax)[idxDstReg & 15].u = (&Ctx.rax)[idxDstReg & 15].u = SavedDst.u; \
        (&CtxExpect.rax)[idxSrcReg & 15].u = (&Ctx.rax)[idxSrcReg & 15].u = SavedSrc.u; \
    } \
    \
    return 0; \
}

BS3CPUINSTR2_COMMON_BINARY_U(8,  uint8_t,  uint8_t,  "RX8")
BS3CPUINSTR2_COMMON_BINARY_U(16, uint16_t, uint16_t, "RX16")
BS3CPUINSTR2_COMMON_BINARY_U(32, uint32_t, uint32_t, "RX32")
#if ARCH_BITS == 64
BS3CPUINSTR2_COMMON_BINARY_U(64, uint64_t, uint32_t, "RX64")
#endif


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_and)(uint8_t bMode)
{
    static const BS3CPUINSTR2CMNBINTEST s_aTests8[]  = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_8(and)  BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_8(and) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests16[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_16(and) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B(and) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16W(and) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests32[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_32(and) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B(and) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32DW(and) };
#if ARCH_BITS == 64
    static const BS3CPUINSTR2CMNBINTEST s_aTests64[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_64(and) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64B(and) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64DW(and) };
#endif
    bs3CpuInstr2_CommonBinaryU8(bMode, s_aTests8, RT_ELEMENTS(s_aTests8), 0 /*fPassthruEfl*/,
                                g_aBs3CpuInstr2_and_TestDataU8, g_cBs3CpuInstr2_and_TestDataU8,
                                false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_and_TestDataU16, g_cBs3CpuInstr2_and_TestDataU16,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_and_TestDataU32, g_cBs3CpuInstr2_and_TestDataU32,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#if ARCH_BITS == 64
    bs3CpuInstr2_CommonBinaryU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_and_TestDataU64, g_cBs3CpuInstr2_and_TestDataU64,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#endif
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_or)(uint8_t bMode)
{
    static const BS3CPUINSTR2CMNBINTEST s_aTests8[]  = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_8(or)  BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_8(or) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests16[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_16(or) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B(or) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16W(or) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests32[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_32(or) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B(or) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32DW(or) };
#if ARCH_BITS == 64
    static const BS3CPUINSTR2CMNBINTEST s_aTests64[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_64(or) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64B(or) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64DW(or) };
#endif
    bs3CpuInstr2_CommonBinaryU8(bMode, s_aTests8, RT_ELEMENTS(s_aTests8), 0 /*fPassthruEfl*/,
                                g_aBs3CpuInstr2_or_TestDataU8, g_cBs3CpuInstr2_or_TestDataU8,
                                false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_or_TestDataU16, g_cBs3CpuInstr2_or_TestDataU16,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_or_TestDataU32, g_cBs3CpuInstr2_or_TestDataU32,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#if ARCH_BITS == 64
    bs3CpuInstr2_CommonBinaryU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_or_TestDataU64, g_cBs3CpuInstr2_or_TestDataU64,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#endif
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_xor)(uint8_t bMode)
{
    static const BS3CPUINSTR2CMNBINTEST s_aTests8[]  = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_8(xor)  BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_8(xor) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests16[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_16(xor) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B(xor) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16W(xor) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests32[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_32(xor) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B(xor) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32DW(xor) };
#if ARCH_BITS == 64
    static const BS3CPUINSTR2CMNBINTEST s_aTests64[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_64(xor) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64B(xor) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64DW(xor) };
#endif
    bs3CpuInstr2_CommonBinaryU8(bMode, s_aTests8, RT_ELEMENTS(s_aTests8), 0 /*fPassthruEfl*/,
                                g_aBs3CpuInstr2_xor_TestDataU8, g_cBs3CpuInstr2_xor_TestDataU8,
                                false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_xor_TestDataU16, g_cBs3CpuInstr2_xor_TestDataU16,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_xor_TestDataU32, g_cBs3CpuInstr2_xor_TestDataU32,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#if ARCH_BITS == 64
    bs3CpuInstr2_CommonBinaryU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_xor_TestDataU64, g_cBs3CpuInstr2_xor_TestDataU64,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#endif
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_test)(uint8_t bMode)
{
    static const BS3CPUINSTR2CMNBINTEST s_aTests8[]  = { BS3CPUINSTR2CMNBINTEST_ENTRIES_8(test)  BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_8(test) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests16[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_16(test) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16W(test) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests32[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_32(test) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32DW(test) };
#if ARCH_BITS == 64
    static const BS3CPUINSTR2CMNBINTEST s_aTests64[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_64(test) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64DW(test) };
#endif
    bs3CpuInstr2_CommonBinaryU8(bMode, s_aTests8, RT_ELEMENTS(s_aTests8), 0 /*fPassthruEfl*/,
                                g_aBs3CpuInstr2_test_TestDataU8, g_cBs3CpuInstr2_test_TestDataU8,
                                false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, true /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_test_TestDataU16, g_cBs3CpuInstr2_test_TestDataU16,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, true /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_test_TestDataU32, g_cBs3CpuInstr2_test_TestDataU32,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, true /*fReadOnly*/);
#if ARCH_BITS == 64
    bs3CpuInstr2_CommonBinaryU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_test_TestDataU64, g_cBs3CpuInstr2_test_TestDataU64,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, true /*fReadOnly*/);
#endif
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_add)(uint8_t bMode)
{
    static const BS3CPUINSTR2CMNBINTEST s_aTests8[]  = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_8(add)  BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_8(add) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests16[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_16(add) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B(add) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16W(add) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests32[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_32(add) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B(add) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32DW(add) };
#if ARCH_BITS == 64
    static const BS3CPUINSTR2CMNBINTEST s_aTests64[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_64(add) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64B(add) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64DW(add) };
#endif
    bs3CpuInstr2_CommonBinaryU8(bMode, s_aTests8, RT_ELEMENTS(s_aTests8), 0 /*fPassthruEfl*/,
                                g_aBs3CpuInstr2_add_TestDataU8, g_cBs3CpuInstr2_add_TestDataU8,
                                false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_add_TestDataU16, g_cBs3CpuInstr2_add_TestDataU16,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_add_TestDataU32, g_cBs3CpuInstr2_add_TestDataU32,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#if ARCH_BITS == 64
    bs3CpuInstr2_CommonBinaryU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_add_TestDataU64, g_cBs3CpuInstr2_add_TestDataU64,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#endif
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_adc)(uint8_t bMode)
{
    static const BS3CPUINSTR2CMNBINTEST s_aTests8[]  = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_8(adc)  BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_8(adc) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests16[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_16(adc) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B(adc) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16W(adc) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests32[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_32(adc) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B(adc) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32DW(adc) };
#if ARCH_BITS == 64
    static const BS3CPUINSTR2CMNBINTEST s_aTests64[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_64(adc) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64B(adc) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64DW(adc) };
#endif
    bs3CpuInstr2_CommonBinaryU8(bMode, s_aTests8, RT_ELEMENTS(s_aTests8), 0 /*fPassthruEfl*/,
                                g_aBs3CpuInstr2_adc_TestDataU8, g_cBs3CpuInstr2_adc_TestDataU8,
                                true /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_adc_TestDataU16, g_cBs3CpuInstr2_adc_TestDataU16,
                                 true /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_adc_TestDataU32, g_cBs3CpuInstr2_adc_TestDataU32,
                                 true /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#if ARCH_BITS == 64
    bs3CpuInstr2_CommonBinaryU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_adc_TestDataU64, g_cBs3CpuInstr2_adc_TestDataU64,
                                 true /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#endif
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_sub)(uint8_t bMode)
{
    static const BS3CPUINSTR2CMNBINTEST s_aTests8[]  = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_8(sub)  BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_8(sub) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests16[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_16(sub) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B(sub) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16W(sub) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests32[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_32(sub) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B(sub) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32DW(sub) };
#if ARCH_BITS == 64
    static const BS3CPUINSTR2CMNBINTEST s_aTests64[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_64(sub) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64B(sub) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64DW(sub) };
#endif
    bs3CpuInstr2_CommonBinaryU8(bMode, s_aTests8, RT_ELEMENTS(s_aTests8), 0 /*fPassthruEfl*/,
                                g_aBs3CpuInstr2_sub_TestDataU8, g_cBs3CpuInstr2_sub_TestDataU8,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_sub_TestDataU16, g_cBs3CpuInstr2_sub_TestDataU16,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_sub_TestDataU32, g_cBs3CpuInstr2_sub_TestDataU32,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#if ARCH_BITS == 64
    bs3CpuInstr2_CommonBinaryU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_sub_TestDataU64, g_cBs3CpuInstr2_sub_TestDataU64,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#endif
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_sbb)(uint8_t bMode)
{
    static const BS3CPUINSTR2CMNBINTEST s_aTests8[]  = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_8(sbb)  BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_8(sbb) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests16[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_16(sbb) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B(sbb) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16W(sbb) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests32[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_32(sbb) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B(sbb) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32DW(sbb) };
#if ARCH_BITS == 64
    static const BS3CPUINSTR2CMNBINTEST s_aTests64[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_64(sbb) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64B(sbb) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64DW(sbb) };
#endif
    bs3CpuInstr2_CommonBinaryU8(bMode, s_aTests8, RT_ELEMENTS(s_aTests8), 0 /*fPassthruEfl*/,
                                g_aBs3CpuInstr2_sbb_TestDataU8, g_cBs3CpuInstr2_sbb_TestDataU8,
                                true /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_sbb_TestDataU16, g_cBs3CpuInstr2_sbb_TestDataU16,
                                 true /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_sbb_TestDataU32, g_cBs3CpuInstr2_sbb_TestDataU32,
                                 true /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#if ARCH_BITS == 64
    bs3CpuInstr2_CommonBinaryU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_sbb_TestDataU64, g_cBs3CpuInstr2_sbb_TestDataU64,
                                 true /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#endif
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_cmp)(uint8_t bMode)
{
    static const BS3CPUINSTR2CMNBINTEST s_aTests8[]  = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_8(cmp)  BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_8(cmp) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests16[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_16(cmp) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B(cmp) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16W(cmp) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests32[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_32(cmp) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B(cmp) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32DW(cmp) };
#if ARCH_BITS == 64
    static const BS3CPUINSTR2CMNBINTEST s_aTests64[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_ALT_64(cmp) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64B(cmp) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64DW(cmp) };
#endif
    bs3CpuInstr2_CommonBinaryU8(bMode, s_aTests8, RT_ELEMENTS(s_aTests8), 0 /*fPassthruEfl*/,
                                g_aBs3CpuInstr2_cmp_TestDataU8, g_cBs3CpuInstr2_cmp_TestDataU8,
                                false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, true /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_cmp_TestDataU16, g_cBs3CpuInstr2_cmp_TestDataU16,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, true /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_cmp_TestDataU32, g_cBs3CpuInstr2_cmp_TestDataU32,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, true /*fReadOnly*/);
#if ARCH_BITS == 64
    bs3CpuInstr2_CommonBinaryU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), 0 /*fPassthruEfl*/,
                                 g_aBs3CpuInstr2_cmp_TestDataU64, g_cBs3CpuInstr2_cmp_TestDataU64,
                                 false /*fCarryIn*/, false /*fMaskSrcWhenMemDst*/, true /*fReadOnly*/);
#endif
    return 0;
}


#define BS3CPUINSTR2_BTx_PASSTHRU_EFL (X86_EFL_ZF | X86_EFL_PF | X86_EFL_AF | X86_EFL_SF | X86_EFL_OF)

BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_bt)(uint8_t bMode)
{
    static const BS3CPUINSTR2CMNBINTEST s_aTests16[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_16(bt) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B(bt) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests32[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_32(bt) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B(bt) };
#if ARCH_BITS == 64
    static const BS3CPUINSTR2CMNBINTEST s_aTests64[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_64(bt) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64B(bt) };
#endif
    bs3CpuInstr2_CommonBinaryU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), BS3CPUINSTR2_BTx_PASSTHRU_EFL,
                                 g_aBs3CpuInstr2_bt_TestDataU16, g_cBs3CpuInstr2_bt_TestDataU16,
                                 false /*fCarryIn*/, true /*fMaskSrcWhenMemDst*/, true /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), BS3CPUINSTR2_BTx_PASSTHRU_EFL,
                                 g_aBs3CpuInstr2_bt_TestDataU32, g_cBs3CpuInstr2_bt_TestDataU32,
                                 false /*fCarryIn*/, true /*fMaskSrcWhenMemDst*/, true /*fReadOnly*/);
#if ARCH_BITS == 64
    bs3CpuInstr2_CommonBinaryU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), BS3CPUINSTR2_BTx_PASSTHRU_EFL,
                                 g_aBs3CpuInstr2_bt_TestDataU64, g_cBs3CpuInstr2_bt_TestDataU64,
                                 false /*fCarryIn*/, true /*fMaskSrcWhenMemDst*/, true /*fReadOnly*/);
#endif
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_btc)(uint8_t bMode)
{
    static const BS3CPUINSTR2CMNBINTEST s_aTests16[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_16(btc) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B(btc) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests32[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_32(btc) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B(btc) };
#if ARCH_BITS == 64
    static const BS3CPUINSTR2CMNBINTEST s_aTests64[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_64(btc) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64B(btc) };
#endif
    bs3CpuInstr2_CommonBinaryU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), BS3CPUINSTR2_BTx_PASSTHRU_EFL,
                                 g_aBs3CpuInstr2_btc_TestDataU16, g_cBs3CpuInstr2_btc_TestDataU16,
                                 false /*fCarryIn*/, true /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), BS3CPUINSTR2_BTx_PASSTHRU_EFL,
                                 g_aBs3CpuInstr2_btc_TestDataU32, g_cBs3CpuInstr2_btc_TestDataU32,
                                 false /*fCarryIn*/, true /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#if ARCH_BITS == 64
    bs3CpuInstr2_CommonBinaryU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), BS3CPUINSTR2_BTx_PASSTHRU_EFL,
                                 g_aBs3CpuInstr2_btc_TestDataU64, g_cBs3CpuInstr2_btc_TestDataU64,
                                 false /*fCarryIn*/, true /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#endif
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_btr)(uint8_t bMode)
{
    static const BS3CPUINSTR2CMNBINTEST s_aTests16[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_16(btr) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B(btr) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests32[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_32(btr) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B(btr) };
#if ARCH_BITS == 64
    static const BS3CPUINSTR2CMNBINTEST s_aTests64[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_64(btr) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64B(btr) };
#endif
    bs3CpuInstr2_CommonBinaryU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), BS3CPUINSTR2_BTx_PASSTHRU_EFL,
                                 g_aBs3CpuInstr2_btr_TestDataU16, g_cBs3CpuInstr2_btr_TestDataU16,
                                 false /*fCarryIn*/, true /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), BS3CPUINSTR2_BTx_PASSTHRU_EFL,
                                 g_aBs3CpuInstr2_btr_TestDataU32, g_cBs3CpuInstr2_btr_TestDataU32,
                                 false /*fCarryIn*/, true /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#if ARCH_BITS == 64
    bs3CpuInstr2_CommonBinaryU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), BS3CPUINSTR2_BTx_PASSTHRU_EFL,
                                 g_aBs3CpuInstr2_btr_TestDataU64, g_cBs3CpuInstr2_btr_TestDataU64,
                                 false /*fCarryIn*/, true /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#endif
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_bts)(uint8_t bMode)
{
    static const BS3CPUINSTR2CMNBINTEST s_aTests16[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_16(bts) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_16B(bts) };
    static const BS3CPUINSTR2CMNBINTEST s_aTests32[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_32(bts) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_32B(bts) };
#if ARCH_BITS == 64
    static const BS3CPUINSTR2CMNBINTEST s_aTests64[] = { BS3CPUINSTR2CMNBINTEST_ENTRIES_64(bts) BS3CPUINSTR2CMNBINTEST_ENTRIES_IMM_64B(bts) };
#endif
    bs3CpuInstr2_CommonBinaryU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), BS3CPUINSTR2_BTx_PASSTHRU_EFL,
                                 g_aBs3CpuInstr2_bts_TestDataU16, g_cBs3CpuInstr2_bts_TestDataU16,
                                 false /*fCarryIn*/, true /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
    bs3CpuInstr2_CommonBinaryU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), BS3CPUINSTR2_BTx_PASSTHRU_EFL,
                                 g_aBs3CpuInstr2_bts_TestDataU32, g_cBs3CpuInstr2_bts_TestDataU32,
                                 false /*fCarryIn*/, true /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#if ARCH_BITS == 64
    bs3CpuInstr2_CommonBinaryU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), BS3CPUINSTR2_BTx_PASSTHRU_EFL,
                                 g_aBs3CpuInstr2_bts_TestDataU64, g_cBs3CpuInstr2_bts_TestDataU64,
                                 false /*fCarryIn*/, true /*fMaskSrcWhenMemDst*/, false /*fReadOnly*/);
#endif
    return 0;
}



/*
 * Basic shift & rotate tests.
 */

# if ARCH_BITS == 64                                                                           /* fDstMem      cBitsImm */
#  define BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_8_64BIT(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _sil_1),       X86_GREG_xSI,     1, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r9b_Ib),      X86_GREG_x9,      8, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r13b_cl),     X86_GREG_x13,     0, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _bDSx14_1),    X86_GREG_x14,     1, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _bDSxAX_Ib),   X86_GREG_xAX,     8, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _bDSx9_cl),    X86_GREG_x9,      0, true  },

#  define BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_16_64BIT(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r8w_1),       X86_GREG_x8,      1, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r9w_Ib),      X86_GREG_x9,      8, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r13w_cl),     X86_GREG_x13,     0, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _wDSx14_1),    X86_GREG_x14,     1, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _wDSxBP_Ib),   X86_GREG_xBP,     8, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _wDSx9_cl),    X86_GREG_x9,      0, true  },

#  define BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_32_64BIT(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r8d_1),       X86_GREG_x8,      1, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r9d_Ib),      X86_GREG_x9,      8, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r13d_cl),     X86_GREG_x13,     0, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _dwDSx14_1),   X86_GREG_x14,     1, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _dwDSxBP_Ib),  X86_GREG_xBP,     8, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _dwDSx9_cl),   X86_GREG_x9,      0, true  },

#  define BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_64(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _rdi_1),       X86_GREG_xDI,     1, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _rcx_Ib),      X86_GREG_xCX,     8, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _rbp_cl),      X86_GREG_xBP,     0, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _qwDSxSI_1),   X86_GREG_xSI,     1, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _qwDSxBX_Ib),  X86_GREG_xBX,     8, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _qwDSxDI_cl),  X86_GREG_xDI,     0, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r8_1),        X86_GREG_x8,      1, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r9_Ib),       X86_GREG_x9,      8, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _r13_cl),      X86_GREG_x13,     0, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _qwDSx14_1),   X86_GREG_x14,     1, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _qwDSxBP_Ib),  X86_GREG_xBP,     8, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _qwDSx9_cl),   X86_GREG_x9,      0, true  },

# else
#  define BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_8_64BIT(aIns)
#  define BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_16_64BIT(aIns)
#  define BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_32_64BIT(aIns)
# endif

# define BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_8(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _al_1),        X86_GREG_xAX,     1, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _dl_Ib),       X86_GREG_xDX,     8, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _ch_cl),       X86_GREG_xCX+16,  0, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _bDSxBX_1),    X86_GREG_xBX,     1, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _bDSxDI_Ib),   X86_GREG_xDI,     8, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _bDSxSI_cl),   X86_GREG_xSI,     0, true  }, \
        BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_8_64BIT(a_Ins)

# define BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_16(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _di_1),        X86_GREG_xDI,     1, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _cx_Ib),       X86_GREG_xCX,     8, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _bp_cl),       X86_GREG_xBP,     0, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _wDSxSI_1),    X86_GREG_xSI,     1, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _wDSxDI_Ib),   X86_GREG_xDI,     8, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _wDSxBX_cl),   X86_GREG_xBX,     0, true  }, \
        BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_16_64BIT(a_Ins)

# define BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_32(a_Ins) \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _edi_1),       X86_GREG_xDI,     1, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _ecx_Ib),      X86_GREG_xCX,     8, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _ebp_cl),      X86_GREG_xBP,     0, false }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _dwDSxSI_1),   X86_GREG_xSI,     1, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _dwDSxBX_Ib),  X86_GREG_xBX,     8, true  }, \
        { BS3_CMN_NM(bs3CpuInstr2_ ## a_Ins ## _dwDSxDI_cl),  X86_GREG_xDI,     0, true  }, \
        BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_32_64BIT(a_Ins)


typedef struct BS3CPUINSTR2CMNSHIFTTEST
{
    FPFNBS3FAR  pfnWorker;
    uint8_t     idxDstReg;
    uint8_t     cBitsImm; /**< 0 for CL; 1 for single shift w/o immediate; 8 for 8-bit immediate. */
    bool        fDstMem;
} BS3CPUINSTR2CMNSHIFTTEST;
typedef BS3CPUINSTR2CMNSHIFTTEST const BS3_FAR_DATA *PCBS3CPUINSTR2CMNSHIFTTEST;


DECLINLINE(uint16_t) bs3CpuInstr2_UndefEflByCpuVendor(BS3CPUVENDOR enmDataVendor, uint16_t fUndefEfl)
{
    BS3CPUVENDOR enmVendor = Bs3GetCpuVendor();
    return    enmDataVendor == enmVendor
           || (enmDataVendor == BS3CPUVENDOR_INTEL) == (enmVendor != BS3CPUVENDOR_AMD && enmVendor != BS3CPUVENDOR_HYGON)
         ? 0 : fUndefEfl;
}


#define BS3CPUINSTR2_COMMON_SHIFT_U(a_cBits, a_UIntType, a_szFmt) \
static uint8_t \
RT_CONCAT(bs3CpuInstr2_CommonShiftU,a_cBits)(uint8_t bMode, PCBS3CPUINSTR2CMNSHIFTTEST paTests, unsigned cTests, \
                                             RT_CONCAT(PCBS3CPUINSTR2SHIFT,a_cBits) paTestData, unsigned cTestData, \
                                             uint16_t fUndefEfl, bool fIntelIbProblem) \
{ \
    BS3REGCTX       Ctx; \
    BS3REGCTX       CtxExpect; \
    BS3TRAPFRAME    TrapFrame; \
    unsigned        iTest; \
    struct \
    { \
        char        achPreGuard[8]; \
        a_UIntType  uData; \
        char        achPostGuard[8]; \
    } Buf = { { '0','1','2','3','4','5','6','7' }, 0, { '8','9','a','b','c','d','e','f'} }; \
    a_UIntType      uMemExpect = 0; \
    uint8_t         bMemDummy  = 0; \
    \
    /* May have no test data for a CPU vendor*/ \
    if (!cTestData) \
        return 0; \
    \
    /* Ensure the structures are allocated before we sample the stack pointer. */ \
    Bs3MemSet(&Ctx, 0, sizeof(Ctx)); \
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame)); \
    \
    /* \
     * Create test context. \
     */ \
    Bs3RegCtxSaveEx(&Ctx, bMode, 640); \
    Ctx.rflags.u32 &= ~X86_EFL_RF; \
    if (ARCH_BITS == 64) \
        for (iTest = 0; iTest < 16; iTest++) \
            if (iTest != X86_GREG_xSP) \
                (&Ctx.rax)[iTest].au32[1] = UINT32_C(0x8572ade) << (iTest & 7); \
    Bs3MemCpy(&CtxExpect, &Ctx, sizeof(CtxExpect)); \
    if (!BS3_MODE_IS_16BIT_SYS(bMode)) \
        CtxExpect.rflags.u32 |= X86_EFL_RF; \
    \
    /* \
     * Each test worker. \
     */ \
    for (iTest = 0; iTest < cTests; iTest++) \
    { \
        uint8_t const               cbInstr        = ((uint8_t BS3_FAR *)paTests[iTest].pfnWorker)[-1]; /* the function is prefixed by the length */ \
        uint8_t RT_FAR * const      pbImm          = (uint8_t BS3_FAR *)Code2RwPtr(&((uint8_t BS3_FAR *)paTests[iTest].pfnWorker)[cbInstr - 1]); \
        uint8_t const               idxDstReg      = paTests[iTest].idxDstReg; \
        uint8_t const               cBitsImm       = paTests[iTest].cBitsImm; \
        uint16_t const              SavedDs        = Ctx.ds; \
        BS3REG const                SavedDst       = (&Ctx.rax)[idxDstReg & 15]; /* saves memptr too */ \
        BS3REG const                SavedRcx       = Ctx.rcx; \
        a_UIntType RT_FAR * const   puCtxDst       = paTests[iTest].fDstMem ? &Buf.uData \
                                                   : &(&Ctx.rax)[idxDstReg & 15].RT_CONCAT(au,a_cBits)[idxDstReg >> 4]; \
        uint8_t RT_FAR * const      puCtxSrc       = cBitsImm == 0 ? &Ctx.rcx.au8[0] : &bMemDummy; \
        a_UIntType RT_FAR * const   puCtxExpectDst = paTests[iTest].fDstMem ? &uMemExpect \
                                                   : &(&CtxExpect.rax)[idxDstReg & 15].RT_CONCAT(au,a_cBits)[idxDstReg >> 4]; \
        uint8_t RT_FAR * const      puCtxExpectSrc = cBitsImm == 0 ? &CtxExpect.rcx.au8[0] : &bMemDummy; \
        uint64_t RT_FAR * const     puMemPtrReg    = paTests[iTest].fDstMem ? &(&Ctx.rax)[idxDstReg & 15].u       : NULL; \
        uint64_t RT_FAR * const     puMemPtrRegExpt= paTests[iTest].fDstMem ? &(&CtxExpect.rax)[idxDstReg & 15].u : NULL; \
        unsigned                    cRecompOuter   = 0; \
        unsigned const              cMaxRecompOuter= cBitsImm != 8 ? g_cBs3ThresholdNativeRecompiler + cTestData : 1; \
        unsigned const              cMaxRecompInner= cBitsImm != 8 ? 1 : g_cBs3ThresholdNativeRecompiler; \
        /*Bs3TestPrintf("\n"#a_cBits ": pfnWorker=%p cBitsImm=%d (%d)\n", paTests[iTest].pfnWorker, cBitsImm, paTests[iTest].cBitsImm);*/ \
        \
        Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, paTests[iTest].pfnWorker); \
        CtxExpect.rip.u = Ctx.rip.u + cbInstr; \
        CtxExpect.cs    = Ctx.cs; \
        \
        if (puMemPtrReg) \
            CtxExpect.ds = Ctx.ds = Ctx.ss; \
        \
        /* \
         * Iterate twice or more over the input data to ensure that the recompiler kicks in. \
         * For instructions with an immediate byte, we do this in the inner loop below. \
         */ \
        while (cRecompOuter < cMaxRecompOuter) \
        { \
            /* \
             * Loop over the test data and feed it to the worker. \
             */\
            unsigned iTestData; \
            for (iTestData = 0; iTestData < cTestData; iTestData++) \
            { \
                unsigned      cRecompInner; \
                uint8_t const uSrc2 = paTestData[iTestData].uSrc2; \
                if (cBitsImm == 0) \
                { \
                    *puCtxSrc       = uSrc2; \
                    *puCtxExpectSrc = uSrc2; \
                } \
                else if (cBitsImm == 8) \
                    *pbImm = uSrc2; \
                else if ((uSrc2 & RT_MAX(a_cBits - 1, 31)) != 1) \
                    continue; \
                cRecompOuter++; \
                \
                *puCtxDst             = paTestData[iTestData].uSrc1; \
                *puCtxExpectDst       = paTestData[iTestData].uResult; \
                if (a_cBits == 32 && !paTests[iTest].fDstMem) \
                    puCtxExpectDst[1] = 0; \
                \
                if (puMemPtrReg) \
                { \
                    *puMemPtrReg     = BS3_FP_OFF(&Buf.uData); \
                    *puMemPtrRegExpt = BS3_FP_OFF(&Buf.uData); \
                } \
                \
                Ctx.rflags.u16       &= ~X86_EFL_STATUS_BITS; \
                Ctx.rflags.u16       |= paTestData[iTestData].fEflIn  & X86_EFL_STATUS_BITS; \
                CtxExpect.rflags.u16 &= ~X86_EFL_STATUS_BITS; \
                CtxExpect.rflags.u16 |= paTestData[iTestData].fEflOut & X86_EFL_STATUS_BITS; \
                /* Intel: 'ROL reg,imm8' and 'ROR reg,imm8' produces different OF values. \
                          stored in bit 3 of the output.  Observed on 8700B, 9980HK, 10980xe, \
                          1260p, ++. */ \
                if (fIntelIbProblem && cBitsImm == 8 && !paTests[iTest].fDstMem) \
                { \
                    CtxExpect.rflags.u16 &= ~X86_EFL_OF; \
                    CtxExpect.rflags.u16 |= (paTestData[iTestData].fEflOut & RT_BIT_32(BS3CPUINSTR2SHIFT_EFL_IB_OVERFLOW_OUT_BIT)) \
                                         << (X86_EFL_OF_BIT - BS3CPUINSTR2SHIFT_EFL_IB_OVERFLOW_OUT_BIT); \
                } \
                \
                /* Inner recompiler trigger loop, for instructions with immediates that we modify. */ \
                cRecompInner = 0; \
                do \
                { \
                    if (paTests[iTest].fDstMem) \
                        *puCtxDst = paTestData[iTestData].uSrc1; \
                    \
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame); \
                    \
                    if (fUndefEfl) /* When executing tests for the other CPU vendor. */ \
                        CtxExpect.rflags.u16 = (CtxExpect.rflags.u16 & ~fUndefEfl) | (TrapFrame.Ctx.rflags.u16 & fUndefEfl); \
                    \
                    if (TrapFrame.bXcpt != X86_XCPT_UD) \
                    { \
                        Bs3TestFailedF("Expected #UD got %#x", TrapFrame.bXcpt); \
                        Bs3TrapPrintFrame(&TrapFrame); \
                    } \
                    else if (Bs3TestCheckRegCtxEx(&TrapFrame.Ctx, &CtxExpect, 0 /*cbPcAdjust*/,  0 /*cbSpAdjust*/, \
                                                  0 /*fExtraEfl*/, "mode", (iTest << 8) | (iTestData & 0xff))) \
                    { \
                        if (puMemPtrReg && paTests[iTest].fDstMem && Buf.uData != uMemExpect) \
                            Bs3TestPrintf("Wrong memory result: %" a_szFmt ", expected %" a_szFmt "\n", Buf.uData, uMemExpect); \
                        else \
                        { \
                            cRecompInner++; \
                            continue; \
                        } \
                    } \
                    /*else { Bs3RegCtxPrint(&Ctx); Bs3TrapPrintFrame(&TrapFrame); }*/ \
                    Bs3TestPrintf(#a_cBits ": iTest=%u iData=%u inner=%u: uSrc1=%" a_szFmt "%s uSrc2=%RX8 (%s) fEfl=%RX16 -> %" a_szFmt " fEfl=%RX16\n", \
                                  iTest, iTestData, cRecompInner, \
                                  paTestData[iTestData].uSrc1, paTests[iTest].fDstMem ? " mem" : "", \
                                  paTestData[iTestData].uSrc2, cBitsImm == 0 ? "cl" : cBitsImm == 1 ? "1" : "Ib", \
                                  (uint16_t)(paTestData[iTestData].fEflIn & X86_EFL_STATUS_BITS), \
                                  paTestData[iTestData].uResult, paTestData[iTestData].fEflOut & X86_EFL_STATUS_BITS); \
                    Bs3RegCtxPrint(&Ctx); Bs3TrapPrintFrame(&TrapFrame); \
                    ASMHalt(); \
                } while (cRecompInner < cMaxRecompInner); \
            } \
        } \
        \
        /* Restore modified context registers (except EFLAGS). */ \
        CtxExpect.ds                       = Ctx.ds                       = SavedDs; \
        (&CtxExpect.rax)[idxDstReg & 15].u = (&Ctx.rax)[idxDstReg & 15].u = SavedDst.u; \
        CtxExpect.rcx.u                    = Ctx.rcx.u                    = SavedRcx.u; \
    } \
    \
    return 0; \
}

BS3CPUINSTR2_COMMON_SHIFT_U(8,  uint8_t,  "RX8")
BS3CPUINSTR2_COMMON_SHIFT_U(16, uint16_t, "RX16")
BS3CPUINSTR2_COMMON_SHIFT_U(32, uint32_t, "RX32")
#if ARCH_BITS == 64
BS3CPUINSTR2_COMMON_SHIFT_U(64, uint64_t, "RX64")
#endif


#define BS3CPUINSTR2_SHIFT_INSTR_NOT_64(a_Ins, a_fEflUndef, a_fIntelIbProblem) \
    { \
        static const BS3CPUINSTR2CMNSHIFTTEST s_aTests8[]  = { BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_8(a_Ins)  }; \
        static const BS3CPUINSTR2CMNSHIFTTEST s_aTests16[] = { BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_16(a_Ins) }; \
        static const BS3CPUINSTR2CMNSHIFTTEST s_aTests32[] = { BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_32(a_Ins) }; \
        uint16_t const fEflUndefIntel = bs3CpuInstr2_UndefEflByCpuVendor(BS3CPUVENDOR_INTEL, a_fEflUndef); \
        uint16_t const fEflUndefAmd   = bs3CpuInstr2_UndefEflByCpuVendor(BS3CPUVENDOR_AMD, a_fEflUndef); \
        bs3CpuInstr2_CommonShiftU8(bMode, s_aTests8, RT_ELEMENTS(s_aTests8), \
                                   g_aBs3CpuInstr2_ ## a_Ins ## _intel_TestDataU8, \
                                   g_cBs3CpuInstr2_ ## a_Ins ## _intel_TestDataU8, \
                                   fEflUndefIntel, a_fIntelIbProblem); \
        bs3CpuInstr2_CommonShiftU8(bMode, s_aTests8, RT_ELEMENTS(s_aTests8), \
                                   g_aBs3CpuInstr2_ ## a_Ins ## _amd_TestDataU8, \
                                   g_cBs3CpuInstr2_ ## a_Ins ## _amd_TestDataU8, \
                                   fEflUndefAmd, false); \
        bs3CpuInstr2_CommonShiftU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), \
                                    g_aBs3CpuInstr2_ ## a_Ins ## _intel_TestDataU16, \
                                    g_cBs3CpuInstr2_ ## a_Ins ## _intel_TestDataU16, \
                                    fEflUndefIntel, a_fIntelIbProblem); \
        bs3CpuInstr2_CommonShiftU16(bMode, s_aTests16, RT_ELEMENTS(s_aTests16), \
                                    g_aBs3CpuInstr2_ ## a_Ins ## _amd_TestDataU16, \
                                    g_cBs3CpuInstr2_ ## a_Ins ## _amd_TestDataU16, \
                                    fEflUndefAmd, false); \
        bs3CpuInstr2_CommonShiftU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), \
                                    g_aBs3CpuInstr2_ ## a_Ins ## _intel_TestDataU32, \
                                    g_cBs3CpuInstr2_ ## a_Ins ## _intel_TestDataU32, \
                                    fEflUndefIntel, a_fIntelIbProblem); \
        bs3CpuInstr2_CommonShiftU32(bMode, s_aTests32, RT_ELEMENTS(s_aTests32), \
                                    g_aBs3CpuInstr2_ ## a_Ins ## _amd_TestDataU32, \
                                    g_cBs3CpuInstr2_ ## a_Ins ## _amd_TestDataU32, \
                                    fEflUndefAmd, false); \
    } (void)0
#if ARCH_BITS == 64
# define BS3CPUINSTR2_SHIFT_INSTR_ONLY64(a_Ins, a_fEflUndef, a_fIntelIbProblem) \
    { \
        static const BS3CPUINSTR2CMNSHIFTTEST s_aTests64[] = { BS3CPUINSTR2CMNSHIFTTEST_ENTRIES_64(a_Ins) }; \
        bs3CpuInstr2_CommonShiftU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), \
                                    g_aBs3CpuInstr2_ ## a_Ins ## _intel_TestDataU64, \
                                    g_cBs3CpuInstr2_ ## a_Ins ## _intel_TestDataU64, \
                                    bs3CpuInstr2_UndefEflByCpuVendor(BS3CPUVENDOR_INTEL, a_fEflUndef), a_fIntelIbProblem); \
        bs3CpuInstr2_CommonShiftU64(bMode, s_aTests64, RT_ELEMENTS(s_aTests64), \
                                    g_aBs3CpuInstr2_ ## a_Ins ## _amd_TestDataU64, \
                                    g_cBs3CpuInstr2_ ## a_Ins ## _amd_TestDataU64, \
                                    bs3CpuInstr2_UndefEflByCpuVendor(BS3CPUVENDOR_AMD, a_fEflUndef), false); \
    } (void)0
#else
# define BS3CPUINSTR2_SHIFT_INSTR_ONLY64(a_Ins, a_fEflUndef, a_fIntelIbProblem) (void)0
#endif


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_shl)(uint8_t bMode)
{
    BS3CPUINSTR2_SHIFT_INSTR_NOT_64(shl, X86_EFL_AF | X86_EFL_OF, false);
    BS3CPUINSTR2_SHIFT_INSTR_ONLY64(shl, X86_EFL_AF | X86_EFL_OF, false);
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_shr)(uint8_t bMode)
{
    BS3CPUINSTR2_SHIFT_INSTR_NOT_64(shr, X86_EFL_AF | X86_EFL_OF, false);
    BS3CPUINSTR2_SHIFT_INSTR_ONLY64(shr, X86_EFL_AF | X86_EFL_OF, false);
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_sar)(uint8_t bMode)
{
    BS3CPUINSTR2_SHIFT_INSTR_NOT_64(sar, X86_EFL_AF | X86_EFL_OF, false);
    BS3CPUINSTR2_SHIFT_INSTR_ONLY64(sar, X86_EFL_AF | X86_EFL_OF, false);
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_rol)(uint8_t bMode)
{
    BS3CPUINSTR2_SHIFT_INSTR_NOT_64(rol, X86_EFL_OF | X86_EFL_CF, true /*fIntelIbProblem*/);
    BS3CPUINSTR2_SHIFT_INSTR_ONLY64(rol, X86_EFL_OF | X86_EFL_CF, true /*fIntelIbProblem*/);
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_ror)(uint8_t bMode)
{
    BS3CPUINSTR2_SHIFT_INSTR_NOT_64(ror, X86_EFL_OF | X86_EFL_CF, true /*fIntelIbProblem*/);
    BS3CPUINSTR2_SHIFT_INSTR_ONLY64(ror, X86_EFL_OF | X86_EFL_CF, true /*fIntelIbProblem*/);
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_rcl)(uint8_t bMode)
{
    BS3CPUINSTR2_SHIFT_INSTR_NOT_64(rcl, X86_EFL_OF, false);
    BS3CPUINSTR2_SHIFT_INSTR_ONLY64(rcl, X86_EFL_OF, false);
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_rcr)(uint8_t bMode)
{
    BS3CPUINSTR2_SHIFT_INSTR_NOT_64(rcr, X86_EFL_OF, false);
    BS3CPUINSTR2_SHIFT_INSTR_ONLY64(rcr, X86_EFL_OF, false);
    return 0;
}





/*
 * Multiplication
 */

BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_mul)(uint8_t bMode)
{
#define MUL_CHECK_EFLAGS_ZERO  (uint16_t)(X86_EFL_AF | X86_EFL_ZF)
#define MUL_CHECK_EFLAGS       (uint16_t)(X86_EFL_CF | X86_EFL_OF | X86_EFL_SF | X86_EFL_PF)

    static const struct
    {
        RTCCUINTREG uInAX;
        RTCCUINTREG uInBX;
        RTCCUINTREG uOutDX;
        RTCCUINTREG uOutAX;
        uint16_t    fFlags;
    } s_aTests[] =
    {
        {   1,      1,
            0,      1,      0 },
        {   2,      2,
            0,      4,      0 },
        {   RTCCUINTREG_MAX, RTCCUINTREG_MAX,
            RTCCUINTREG_MAX-1,  1,                  X86_EFL_CF | X86_EFL_OF },
        {   RTCCINTREG_MAX,  RTCCINTREG_MAX,
            RTCCINTREG_MAX / 2, 1,                  X86_EFL_CF | X86_EFL_OF },
        {   1, RTCCUINTREG_MAX,
            0, RTCCUINTREG_MAX,                     X86_EFL_PF | X86_EFL_SF },
        {   1, RTCCINTREG_MAX,
            0, RTCCINTREG_MAX,                      X86_EFL_PF },
        {   2, RTCCINTREG_MAX,
            0, RTCCUINTREG_MAX - 1,                 X86_EFL_SF },
        {   (RTCCUINTREG)RTCCINTREG_MAX + 1, 2,
            1, 0,                                   X86_EFL_PF | X86_EFL_CF | X86_EFL_OF },
        {   (RTCCUINTREG)RTCCINTREG_MAX / 2 + 1, 3,
            0, ((RTCCUINTREG)RTCCINTREG_MAX / 2 + 1) * 3, X86_EFL_PF | X86_EFL_SF },
    };

    BS3REGCTX       Ctx;
    BS3TRAPFRAME    TrapFrame;
    unsigned        i, j, k;

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);
    Bs3RegCtxSetRipCsFromCurPtr(&Ctx, BS3_CMN_NM(bs3CpuInstr2_mul_xBX_ud2));
    for (k = 0; k < 2; k++)
    {
        Ctx.rflags.u16 |= MUL_CHECK_EFLAGS | MUL_CHECK_EFLAGS_ZERO;
        for (j = 0; j < 2; j++)
        {
            for (i = 0; i < RT_ELEMENTS(s_aTests); i++)
            {
                if (k == 0)
                {
                    Ctx.rax.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInAX;
                    Ctx.rbx.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInBX;
                }
                else
                {
                    Ctx.rax.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInBX;
                    Ctx.rbx.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInAX;
                }
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);
                if (TrapFrame.bXcpt != X86_XCPT_UD)
                    Bs3TestFailedF("Expected #UD got %#x", TrapFrame.bXcpt);
                else if (   TrapFrame.Ctx.rax.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutAX
                         || TrapFrame.Ctx.rdx.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutDX
                         ||    (TrapFrame.Ctx.rflags.u16 & (MUL_CHECK_EFLAGS | MUL_CHECK_EFLAGS_ZERO))
                            != (s_aTests[i].fFlags & MUL_CHECK_EFLAGS) )
                {
                    Bs3TestFailedF("test #%i failed: input %#" RTCCUINTREG_XFMT " * %#" RTCCUINTREG_XFMT,
                                   i, s_aTests[i].uInAX, s_aTests[i].uInBX);

                    if (TrapFrame.Ctx.rax.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutAX)
                        Bs3TestFailedF("Expected xAX = %#RX" RT_XSTR(ARCH_BITS) " got %#RX" RT_XSTR(ARCH_BITS),
                                       s_aTests[i].uOutAX, TrapFrame.Ctx.rax.RT_CONCAT(u,ARCH_BITS));
                    if (TrapFrame.Ctx.rdx.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutDX)
                        Bs3TestFailedF("Expected xDX = %#RX" RT_XSTR(ARCH_BITS) " got %#RX" RT_XSTR(ARCH_BITS),
                                       s_aTests[i].uOutDX, TrapFrame.Ctx.rdx.RT_CONCAT(u,ARCH_BITS));
                    if (   (TrapFrame.Ctx.rflags.u16 & (MUL_CHECK_EFLAGS | MUL_CHECK_EFLAGS_ZERO))
                        != (s_aTests[i].fFlags & MUL_CHECK_EFLAGS) )
                        Bs3TestFailedF("Expected EFLAGS = %#06RX16, got %#06RX16", s_aTests[i].fFlags & MUL_CHECK_EFLAGS,
                                       TrapFrame.Ctx.rflags.u16 & (MUL_CHECK_EFLAGS | MUL_CHECK_EFLAGS_ZERO));
                }
            }
            Ctx.rflags.u16 &= ~(MUL_CHECK_EFLAGS | MUL_CHECK_EFLAGS_ZERO);
        }
    }

    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_imul)(uint8_t bMode)
{
#define IMUL_CHECK_EFLAGS_ZERO  (uint16_t)(X86_EFL_AF | X86_EFL_ZF)
#define IMUL_CHECK_EFLAGS       (uint16_t)(X86_EFL_CF | X86_EFL_OF | X86_EFL_SF | X86_EFL_PF)
    static const struct
    {
        RTCCUINTREG uInAX;
        RTCCUINTREG uInBX;
        RTCCUINTREG uOutDX;
        RTCCUINTREG uOutAX;
        uint16_t    fFlags;
    } s_aTests[] =
    {
        /* two positive values. */
        {   1,      1,
            0,      1,      0 },
        {   2,      2,
            0,      4,      0 },
        {   RTCCINTREG_MAX, RTCCINTREG_MAX,
            RTCCINTREG_MAX/2, 1,                    X86_EFL_CF | X86_EFL_OF },
        {   1, RTCCINTREG_MAX,
            0, RTCCINTREG_MAX,                      X86_EFL_PF },
        {   2, RTCCINTREG_MAX,
            0, RTCCUINTREG_MAX - 1U,                X86_EFL_CF | X86_EFL_OF | X86_EFL_SF },
        {   2, RTCCINTREG_MAX / 2,
            0, RTCCINTREG_MAX - 1U,                 0 },
        {   2, (RTCCINTREG_MAX / 2 + 1),
            0, (RTCCUINTREG)RTCCINTREG_MAX + 1U,    X86_EFL_CF | X86_EFL_OF | X86_EFL_SF | X86_EFL_PF },
        {   4, (RTCCINTREG_MAX / 2 + 1),
            1, 0,                                   X86_EFL_CF | X86_EFL_OF | X86_EFL_PF },

        /* negative and positive */
        {   -4,     3,
            -1,     -12,                            X86_EFL_SF },
        {   32,     -127,
            -1,     -4064,                          X86_EFL_SF },
        {   RTCCINTREG_MIN, 1,
            -1, RTCCINTREG_MIN,                     X86_EFL_SF | X86_EFL_PF },
        {   RTCCINTREG_MIN, 2,
            -1,     0,                              X86_EFL_CF | X86_EFL_OF | X86_EFL_PF },
        {   RTCCINTREG_MIN, 3,
            -2,     RTCCINTREG_MIN,                 X86_EFL_CF | X86_EFL_OF | X86_EFL_SF | X86_EFL_PF },
        {   RTCCINTREG_MIN, 4,
            -2,     0,                              X86_EFL_CF | X86_EFL_OF | X86_EFL_PF },
        {   RTCCINTREG_MIN, RTCCINTREG_MAX,
            RTCCINTREG_MIN / 2, RTCCINTREG_MIN,     X86_EFL_CF | X86_EFL_OF | X86_EFL_SF | X86_EFL_PF },
        {   RTCCINTREG_MIN, RTCCINTREG_MAX - 1,
            RTCCINTREG_MIN / 2 + 1, 0,              X86_EFL_CF | X86_EFL_OF | X86_EFL_PF },

        /* two negative values. */
        {   -4,     -63,
            0,      252,                            X86_EFL_PF },
        {   RTCCINTREG_MIN, RTCCINTREG_MIN,
            RTCCUINTREG_MAX / 4 + 1, 0,             X86_EFL_CF | X86_EFL_OF | X86_EFL_PF },
        {   RTCCINTREG_MIN, RTCCINTREG_MIN + 1,
            RTCCUINTREG_MAX / 4, RTCCINTREG_MIN,    X86_EFL_CF | X86_EFL_OF | X86_EFL_SF | X86_EFL_PF},
        {   RTCCINTREG_MIN + 1, RTCCINTREG_MIN + 1,
            RTCCUINTREG_MAX / 4, 1,                 X86_EFL_CF | X86_EFL_OF },

    };

    BS3REGCTX       Ctx;
    BS3TRAPFRAME    TrapFrame;
    unsigned        i, j, k;

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);
    Bs3RegCtxSetRipCsFromCurPtr(&Ctx, BS3_CMN_NM(bs3CpuInstr2_imul_xBX_ud2));

    for (k = 0; k < 2; k++)
    {
        Ctx.rflags.u16 |= MUL_CHECK_EFLAGS | MUL_CHECK_EFLAGS_ZERO;
        for (j = 0; j < 2; j++)
        {
            for (i = 0; i < RT_ELEMENTS(s_aTests); i++)
            {
                if (k == 0)
                {
                    Ctx.rax.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInAX;
                    Ctx.rbx.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInBX;
                }
                else
                {
                    Ctx.rax.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInBX;
                    Ctx.rbx.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInAX;
                }
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);
                if (TrapFrame.bXcpt != X86_XCPT_UD)
                    Bs3TestFailedF("Expected #UD got %#x", TrapFrame.bXcpt);
                else if (   TrapFrame.Ctx.rax.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutAX
                         || TrapFrame.Ctx.rdx.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutDX
                         ||    (TrapFrame.Ctx.rflags.u16 & (IMUL_CHECK_EFLAGS | IMUL_CHECK_EFLAGS_ZERO))
                            != (s_aTests[i].fFlags & IMUL_CHECK_EFLAGS) )
                {
                    Bs3TestFailedF("test #%i failed: input %#" RTCCUINTREG_XFMT " * %#" RTCCUINTREG_XFMT,
                                   i, s_aTests[i].uInAX, s_aTests[i].uInBX);

                    if (TrapFrame.Ctx.rax.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutAX)
                        Bs3TestFailedF("Expected xAX = %#RX" RT_XSTR(ARCH_BITS) " got %#RX" RT_XSTR(ARCH_BITS),
                                       s_aTests[i].uOutAX, TrapFrame.Ctx.rax.RT_CONCAT(u,ARCH_BITS));
                    if (TrapFrame.Ctx.rdx.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutDX)
                        Bs3TestFailedF("Expected xDX = %#RX" RT_XSTR(ARCH_BITS) " got %#RX" RT_XSTR(ARCH_BITS),
                                       s_aTests[i].uOutDX, TrapFrame.Ctx.rdx.RT_CONCAT(u,ARCH_BITS));
                    if (   (TrapFrame.Ctx.rflags.u16 & (IMUL_CHECK_EFLAGS | IMUL_CHECK_EFLAGS_ZERO))
                        != (s_aTests[i].fFlags & IMUL_CHECK_EFLAGS) )
                        Bs3TestFailedF("Expected EFLAGS = %#06RX16, got %#06RX16", s_aTests[i].fFlags & IMUL_CHECK_EFLAGS,
                                       TrapFrame.Ctx.rflags.u16 & (IMUL_CHECK_EFLAGS | IMUL_CHECK_EFLAGS_ZERO));
                }
            }
        }
    }

    /*
     * Repeat for the truncating two operand version.
     */
    Bs3RegCtxSetRipCsFromCurPtr(&Ctx, BS3_CMN_NM(bs3CpuInstr2_imul_xCX_xBX_ud2));

    for (k = 0; k < 2; k++)
    {
        Ctx.rflags.u16 |= MUL_CHECK_EFLAGS | MUL_CHECK_EFLAGS_ZERO;
        for (j = 0; j < 2; j++)
        {
            for (i = 0; i < RT_ELEMENTS(s_aTests); i++)
            {
                if (k == 0)
                {
                    Ctx.rcx.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInAX;
                    Ctx.rbx.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInBX;
                }
                else
                {
                    Ctx.rcx.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInBX;
                    Ctx.rbx.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInAX;
                }
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);
                if (TrapFrame.bXcpt != X86_XCPT_UD)
                    Bs3TestFailedF("Expected #UD got %#x", TrapFrame.bXcpt);
                else if (   TrapFrame.Ctx.rcx.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutAX
                         || TrapFrame.Ctx.rdx.u != Ctx.rdx.u
                         || TrapFrame.Ctx.rbx.u != Ctx.rbx.u
                         ||    (TrapFrame.Ctx.rflags.u16 & (IMUL_CHECK_EFLAGS | IMUL_CHECK_EFLAGS_ZERO))
                            != (s_aTests[i].fFlags & IMUL_CHECK_EFLAGS) )
                {
                    Bs3TestFailedF("test #%i failed: input %#" RTCCUINTREG_XFMT " * %#" RTCCUINTREG_XFMT,
                                   i, s_aTests[i].uInAX, s_aTests[i].uInBX);

                    if (TrapFrame.Ctx.rcx.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutAX)
                        Bs3TestFailedF("Expected xAX = %#RX" RT_XSTR(ARCH_BITS) " got %#RX" RT_XSTR(ARCH_BITS),
                                       s_aTests[i].uOutAX, TrapFrame.Ctx.rcx.RT_CONCAT(u,ARCH_BITS));
                    if (   (TrapFrame.Ctx.rflags.u16 & (IMUL_CHECK_EFLAGS | IMUL_CHECK_EFLAGS_ZERO))
                        != (s_aTests[i].fFlags & IMUL_CHECK_EFLAGS) )
                        Bs3TestFailedF("Expected EFLAGS = %#06RX16, got %#06RX16", s_aTests[i].fFlags & IMUL_CHECK_EFLAGS,
                                       TrapFrame.Ctx.rflags.u16 & (IMUL_CHECK_EFLAGS | IMUL_CHECK_EFLAGS_ZERO));
                }
            }
        }
    }

    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_div)(uint8_t bMode)
{
#define DIV_CHECK_EFLAGS (uint16_t)(X86_EFL_CF | X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF)
    static const struct
    {
        RTCCUINTREG uInDX;
        RTCCUINTREG uInAX;
        RTCCUINTREG uInBX;
        RTCCUINTREG uOutAX;
        RTCCUINTREG uOutDX;
        uint8_t     bXcpt;
    } s_aTests[] =
    {
        {   0,    1,                            1,
            1,    0,                                                    X86_XCPT_UD },
        {   0,    5,                            2,
            2,    1,                                                    X86_XCPT_UD },
        {   0,    0,                            0,
            0,    0,                                                    X86_XCPT_DE },
        { RTCCUINTREG_MAX, RTCCUINTREG_MAX,     0,
            0,    0,                                                    X86_XCPT_DE },
        { RTCCUINTREG_MAX, RTCCUINTREG_MAX,     1,
            0,    0,                                                    X86_XCPT_DE },
        { RTCCUINTREG_MAX, RTCCUINTREG_MAX,     RTCCUINTREG_MAX,
            0,    0,                                                    X86_XCPT_DE },
        { RTCCUINTREG_MAX - 1, RTCCUINTREG_MAX, RTCCUINTREG_MAX,
            RTCCUINTREG_MAX, RTCCUINTREG_MAX - 1,                       X86_XCPT_UD },
    };

    BS3REGCTX       Ctx;
    BS3TRAPFRAME    TrapFrame;
    unsigned        i, j;

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);
    Bs3RegCtxSetRipCsFromCurPtr(&Ctx, BS3_CMN_NM(bs3CpuInstr2_div_xBX_ud2));

    /*
     * Do the tests twice, first with all flags set, then once again with
     * flags cleared.  The flags are not touched by my intel skylake CPU.
     */
    Ctx.rflags.u16 |= DIV_CHECK_EFLAGS;
    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            Ctx.rax.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInAX;
            Ctx.rdx.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInDX;
            Ctx.rbx.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInBX;
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);

            if (   TrapFrame.bXcpt                          != s_aTests[i].bXcpt
                || (   s_aTests[i].bXcpt == X86_XCPT_UD
                    ?    TrapFrame.Ctx.rax.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutAX
                      || TrapFrame.Ctx.rdx.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutDX
                      || (TrapFrame.Ctx.rflags.u16 & DIV_CHECK_EFLAGS) != (Ctx.rflags.u16 & DIV_CHECK_EFLAGS)
                    :    TrapFrame.Ctx.rax.u != Ctx.rax.u
                      || TrapFrame.Ctx.rdx.u != Ctx.rdx.u
                      || (TrapFrame.Ctx.rflags.u16 & DIV_CHECK_EFLAGS) != (Ctx.rflags.u16 & DIV_CHECK_EFLAGS) ) )
            {
                Bs3TestFailedF("test #%i failed: input %#" RTCCUINTREG_XFMT ":%" RTCCUINTREG_XFMT " / %#" RTCCUINTREG_XFMT,
                               i, s_aTests[i].uInDX, s_aTests[i].uInAX, s_aTests[i].uInBX);
                if (TrapFrame.bXcpt != s_aTests[i].bXcpt)
                    Bs3TestFailedF("Expected bXcpt = %#x, got %#x", s_aTests[i].bXcpt, TrapFrame.bXcpt);
                if (s_aTests[i].bXcpt == X86_XCPT_UD)
                {
                    if (TrapFrame.Ctx.rax.RT_CONCAT(u, ARCH_BITS) != s_aTests[i].uOutAX)
                        Bs3TestFailedF("Expected xAX = %#" RTCCUINTREG_XFMT ", got %#" RTCCUINTREG_XFMT,
                                       s_aTests[i].uOutAX, TrapFrame.Ctx.rax.RT_CONCAT(u,ARCH_BITS));
                    if (TrapFrame.Ctx.rdx.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutDX)
                        Bs3TestFailedF("Expected xDX = %#" RTCCUINTREG_XFMT ", got %#" RTCCUINTREG_XFMT,
                                       s_aTests[i].uOutDX, TrapFrame.Ctx.rdx.RT_CONCAT(u,ARCH_BITS));
                    if ((TrapFrame.Ctx.rflags.u16 & DIV_CHECK_EFLAGS) != (Ctx.rflags.u16 & DIV_CHECK_EFLAGS))
                        Bs3TestFailedF("Expected EFLAGS = %#06RX16, got %#06RX16",
                                       Ctx.rflags.u16 & DIV_CHECK_EFLAGS, TrapFrame.Ctx.rflags.u16 & DIV_CHECK_EFLAGS);
                }
            }
        }
        Ctx.rflags.u16 &= ~DIV_CHECK_EFLAGS;
    }

    return 0;
}



BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_idiv)(uint8_t bMode)
{
#define IDIV_CHECK_EFLAGS (uint16_t)(X86_EFL_CF | X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF)
    static const struct
    {
        RTCCUINTREG uInDX;
        RTCCUINTREG uInAX;
        RTCCUINTREG uInBX;
        RTCCUINTREG uOutAX;
        RTCCUINTREG uOutDX;
        uint8_t     bXcpt;
    } s_aTests[] =
    {
        {   0,    0,                            0,
            0,    0,                                                    X86_XCPT_DE },
        {   RTCCINTREG_MAX, RTCCINTREG_MAX,     0,
            0,    0,                                                    X86_XCPT_DE },
        /* two positive values. */
        {   0,    1,                    1,
            1,    0,                                                    X86_XCPT_UD },
        {   0,    5,                    2,
            2,    1,                                                    X86_XCPT_UD },
        {   RTCCINTREG_MAX / 2, RTCCUINTREG_MAX / 2,        RTCCINTREG_MAX,
            RTCCINTREG_MAX, RTCCINTREG_MAX - 1,                         X86_XCPT_UD },
        {   RTCCINTREG_MAX / 2, RTCCUINTREG_MAX / 2 + 1,    RTCCINTREG_MAX,
            RTCCINTREG_MAX, RTCCINTREG_MAX - 1,                         X86_XCPT_DE },
        /* negative dividend, positive divisor. */
        {   -1,  -7,                    2,
            -3,  -1,                                                    X86_XCPT_UD },
        {   RTCCINTREG_MIN / 2 + 1, 0,                      RTCCINTREG_MAX,
            RTCCINTREG_MIN + 2, RTCCINTREG_MIN + 2,                     X86_XCPT_UD },
        {   RTCCINTREG_MIN / 2,     0,                      RTCCINTREG_MAX,
            0,    0,                                                    X86_XCPT_DE },
        /* positive dividend, negative divisor. */
        {   0,    7,                    -2,
            -3,   1,                                                    X86_XCPT_UD },
        {   RTCCINTREG_MAX / 2 + 1, RTCCINTREG_MAX,         RTCCINTREG_MIN,
            RTCCINTREG_MIN,     RTCCINTREG_MAX,                         X86_XCPT_UD },
        {   RTCCINTREG_MAX / 2 + 1, (RTCCUINTREG)RTCCINTREG_MAX+1, RTCCINTREG_MIN,
            0,    0,                                                    X86_XCPT_DE },
        /* negative dividend, negative divisor. */
        {   -1,  -7,                    -2,
            3,   -1,                                                    X86_XCPT_UD },
        {   RTCCINTREG_MIN / 2, 1,                          RTCCINTREG_MIN,
            RTCCINTREG_MAX, RTCCINTREG_MIN + 1,                         X86_XCPT_UD },
        {   RTCCINTREG_MIN / 2, 2,                          RTCCINTREG_MIN,
            RTCCINTREG_MAX, RTCCINTREG_MIN + 2,                         X86_XCPT_UD },
        {   RTCCINTREG_MIN / 2, 0,                          RTCCINTREG_MIN,
            0, 0,                                                       X86_XCPT_DE },
    };

    BS3REGCTX       Ctx;
    BS3TRAPFRAME    TrapFrame;
    unsigned        i, j;

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);
    Bs3RegCtxSetRipCsFromCurPtr(&Ctx, BS3_CMN_NM(bs3CpuInstr2_idiv_xBX_ud2));

    /*
     * Do the tests twice, first with all flags set, then once again with
     * flags cleared.  The flags are not touched by my intel skylake CPU.
     */
    Ctx.rflags.u16 |= IDIV_CHECK_EFLAGS;
    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            Ctx.rax.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInAX;
            Ctx.rdx.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInDX;
            Ctx.rbx.RT_CONCAT(u,ARCH_BITS) = s_aTests[i].uInBX;
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);

            if (   TrapFrame.bXcpt                          != s_aTests[i].bXcpt
                || (   s_aTests[i].bXcpt == X86_XCPT_UD
                    ?    TrapFrame.Ctx.rax.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutAX
                      || TrapFrame.Ctx.rdx.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutDX
                      || (TrapFrame.Ctx.rflags.u16 & IDIV_CHECK_EFLAGS) != (Ctx.rflags.u16 & IDIV_CHECK_EFLAGS)
                    :    TrapFrame.Ctx.rax.u != Ctx.rax.u
                      || TrapFrame.Ctx.rdx.u != Ctx.rdx.u
                      || (TrapFrame.Ctx.rflags.u16 & IDIV_CHECK_EFLAGS) != (Ctx.rflags.u16 & IDIV_CHECK_EFLAGS) ) )
            {
                Bs3TestFailedF("test #%i failed: input %#" RTCCUINTREG_XFMT ":%" RTCCUINTREG_XFMT " / %#" RTCCUINTREG_XFMT,
                               i, s_aTests[i].uInDX, s_aTests[i].uInAX, s_aTests[i].uInBX);
                if (TrapFrame.bXcpt != s_aTests[i].bXcpt)
                    Bs3TestFailedF("Expected bXcpt = %#x, got %#x", s_aTests[i].bXcpt, TrapFrame.bXcpt);
                if (s_aTests[i].bXcpt == X86_XCPT_UD)
                {
                    if (TrapFrame.Ctx.rax.RT_CONCAT(u, ARCH_BITS) != s_aTests[i].uOutAX)
                        Bs3TestFailedF("Expected xAX = %#" RTCCUINTREG_XFMT ", got %#" RTCCUINTREG_XFMT,
                                       s_aTests[i].uOutAX, TrapFrame.Ctx.rax.RT_CONCAT(u,ARCH_BITS));
                    if (TrapFrame.Ctx.rdx.RT_CONCAT(u,ARCH_BITS) != s_aTests[i].uOutDX)
                        Bs3TestFailedF("Expected xDX = %#" RTCCUINTREG_XFMT ", got %#" RTCCUINTREG_XFMT,
                                       s_aTests[i].uOutDX, TrapFrame.Ctx.rdx.RT_CONCAT(u,ARCH_BITS));
                    if ((TrapFrame.Ctx.rflags.u16 & IDIV_CHECK_EFLAGS) != (Ctx.rflags.u16 & IDIV_CHECK_EFLAGS))
                        Bs3TestFailedF("Expected EFLAGS = %#06RX16, got %#06RX16",
                                       Ctx.rflags.u16 & IDIV_CHECK_EFLAGS, TrapFrame.Ctx.rflags.u16 & IDIV_CHECK_EFLAGS);
                }
            }
        }
        Ctx.rflags.u16 &= ~IDIV_CHECK_EFLAGS;
    }

    return 0;
}


/*
 * BSF/BSR (386+) & TZCNT/LZCNT (BMI1,ABM)
 */

typedef struct BS3CPUINSTR2_SUBTEST_BITSCAN_T
{
    RTCCUINTXREG    uSrc;
    RTCCUINTXREG    uOut;
    bool            fOutNotSet;
    uint16_t        fEflOut;
} BS3CPUINSTR2_SUBTEST_BITSCAN_T;

typedef struct BS3CPUINSTR2_TEST_BITSCAN_T
{
    FPFNBS3FAR      pfnWorker;
    bool            fMemSrc;
    uint8_t         cbInstr;
    uint8_t         cOpBits;
    uint16_t        fEflCheck;
    uint8_t         cSubTests;
    BS3CPUINSTR2_SUBTEST_BITSCAN_T const *paSubTests;
} BS3CPUINSTR2_TEST_BITSCAN_T;

static uint8_t bs3CpuInstr2_BitScan(uint8_t bMode, BS3CPUINSTR2_TEST_BITSCAN_T const *paTests, unsigned cTests)
{
    BS3REGCTX       Ctx;
    BS3TRAPFRAME    TrapFrame;
    unsigned        i, j, k;

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);

    /*
     * Do the tests twice, first with all flags set, then once again with
     * flags cleared.  The flags are not supposed to be touched at all.
     */
    Ctx.rflags.u16 |= X86_EFL_STATUS_BITS;
    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < cTests; i++)
        {
            for (k = 0; k < paTests[i].cSubTests; k++)
            {
                uint64_t      uExpectRax, uExpectRip;
                RTCCUINTXREG  uMemSrc, uMemSrcExpect;

                Ctx.rax.uCcXReg = RTCCUINTXREG_MAX * 1019;
                if (!paTests[i].fMemSrc)
                {
                    Ctx.rbx.uCcXReg         = paTests[i].paSubTests[k].uSrc;
                    uMemSrcExpect = uMemSrc = ~paTests[i].paSubTests[k].uSrc;
                }
                else
                {
                    uMemSrcExpect = uMemSrc = paTests[i].paSubTests[k].uSrc;
                    Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, &Ctx.fs, &uMemSrc);
                }
                Bs3RegCtxSetRipCsFromCurPtr(&Ctx, paTests[i].pfnWorker);
                if (paTests[i].paSubTests[k].fOutNotSet)
                    uExpectRax = Ctx.rax.u;
                else if (paTests[i].cOpBits != 16)
                    uExpectRax = paTests[i].paSubTests[k].uOut;
                else
                    uExpectRax = paTests[i].paSubTests[k].uOut | (Ctx.rax.u & UINT64_C(0xffffffffffff0000));
                uExpectRip = Ctx.rip.u + paTests[i].cbInstr;
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);

                if (   TrapFrame.bXcpt != X86_XCPT_UD
                    || TrapFrame.Ctx.rip.u != uExpectRip
                    || TrapFrame.Ctx.rbx.u != Ctx.rbx.u
                    || TrapFrame.Ctx.rax.u != uExpectRax
                    ||    (TrapFrame.Ctx.rflags.u16 & paTests[i].fEflCheck)
                       != (paTests[i].paSubTests[k].fEflOut & paTests[i].fEflCheck)
                    /* check that nothing else really changed: */
                    || TrapFrame.Ctx.rcx.u != Ctx.rcx.u
                    || TrapFrame.Ctx.rdx.u != Ctx.rdx.u
                    || TrapFrame.Ctx.rsp.u != Ctx.rsp.u
                    || TrapFrame.Ctx.rbp.u != Ctx.rbp.u
                    || TrapFrame.Ctx.rsi.u != Ctx.rsi.u
                    || TrapFrame.Ctx.rdi.u != Ctx.rdi.u
                    || uMemSrc != uMemSrcExpect
                   )
                {
                    Bs3TestFailedF("test #%i/%i failed: input %#" RTCCUINTXREG_XFMT,
                                   i, k, paTests[i].paSubTests[k].uSrc);
                    if (TrapFrame.bXcpt != X86_XCPT_UD)
                        Bs3TestFailedF("Expected bXcpt = %#x, got %#x", X86_XCPT_UD, TrapFrame.bXcpt);
                    if (TrapFrame.Ctx.rip.u != uExpectRip)
                        Bs3TestFailedF("Expected RIP = %#06RX64, got %#06RX64", uExpectRip, TrapFrame.Ctx.rip.u);
                    if (TrapFrame.Ctx.rax.u != uExpectRax)
                        Bs3TestFailedF("Expected RAX = %#06RX64, got %#06RX64", uExpectRax, TrapFrame.Ctx.rax.u);
                    if (TrapFrame.Ctx.rcx.u != Ctx.rcx.u)
                        Bs3TestFailedF("Expected RCX = %#06RX64, got %#06RX64", Ctx.rcx.u, TrapFrame.Ctx.rcx.u);
                    if (TrapFrame.Ctx.rbx.u != Ctx.rbx.u)
                        Bs3TestFailedF("Expected RBX = %#06RX64, got %#06RX64 (dst)", Ctx.rbx.u, TrapFrame.Ctx.rbx.u);
                    if (   (TrapFrame.Ctx.rflags.u16 & paTests[i].fEflCheck)
                        != (paTests[i].paSubTests[k].fEflOut & paTests[i].fEflCheck))
                        Bs3TestFailedF("Expected EFLAGS = %#06RX32, got %#06RX32 (output)",
                                       paTests[i].paSubTests[k].fEflOut & paTests[i].fEflCheck,
                                       TrapFrame.Ctx.rflags.u16 & paTests[i].fEflCheck);

                    if (TrapFrame.Ctx.rdx.u != Ctx.rdx.u)
                        Bs3TestFailedF("Expected RDX = %#06RX64, got %#06RX64 (src)", Ctx.rdx.u, TrapFrame.Ctx.rdx.u);
                    if (TrapFrame.Ctx.rsp.u != Ctx.rsp.u)
                        Bs3TestFailedF("Expected RSP = %#06RX64, got %#06RX64", Ctx.rsp.u, TrapFrame.Ctx.rsp.u);
                    if (TrapFrame.Ctx.rbp.u != Ctx.rbp.u)
                        Bs3TestFailedF("Expected RBP = %#06RX64, got %#06RX64", Ctx.rbp.u, TrapFrame.Ctx.rbp.u);
                    if (TrapFrame.Ctx.rsi.u != Ctx.rsi.u)
                        Bs3TestFailedF("Expected RSI = %#06RX64, got %#06RX64", Ctx.rsi.u, TrapFrame.Ctx.rsi.u);
                    if (TrapFrame.Ctx.rdi.u != Ctx.rdi.u)
                        Bs3TestFailedF("Expected RDI = %#06RX64, got %#06RX64", Ctx.rdi.u, TrapFrame.Ctx.rdi.u);
                    if (uMemSrc != uMemSrcExpect)
                        Bs3TestFailedF("Expected uMemSrc = %#06RX64, got %#06RX64", (uint64_t)uMemSrcExpect, (uint64_t)uMemSrc);
                }
            }
        }
        Ctx.rflags.u16 &= ~X86_EFL_STATUS_BITS;
    }

    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_bsf_tzcnt)(uint8_t bMode)
{
    static BS3CPUINSTR2_SUBTEST_BITSCAN_T const s_aSubTestsBsf16[] =
    {
        {   0,                              /* -> */    0,  true,  X86_EFL_ZF },
        {   ~(RTCCUINTXREG)UINT16_MAX,      /* -> */    0,  true,  X86_EFL_ZF },
        {   ~(RTCCUINTXREG)0,               /* -> */    0,  false, 0 },
        {   ~(RTCCUINTXREG)1,               /* -> */    1,  false, 0 },
        {   UINT16_C(0x8000),               /* -> */    15, false, 0 },
        {   UINT16_C(0x4560),               /* -> */    5,  false, 0 },
    };
    static BS3CPUINSTR2_SUBTEST_BITSCAN_T const s_aSubTestsTzCnt16[] =
    {
        {   0,                              /* -> */    16, false, X86_EFL_CF },
        {   ~(RTCCUINTXREG)UINT16_MAX,      /* -> */    16, false, X86_EFL_CF },
        {   ~(RTCCUINTXREG)0,               /* -> */    0,  false, X86_EFL_ZF },
        {   ~(RTCCUINTXREG)1,               /* -> */    1,  false, 0 },
        {   UINT16_C(0x8000),               /* -> */    15, false, 0 },
        {   UINT16_C(0x4560),               /* -> */    5,  false, 0 },
    };
    static BS3CPUINSTR2_SUBTEST_BITSCAN_T const s_aSubTestsBsf32[] =
    {
        {   0,                              /* -> */    0,  true,  X86_EFL_ZF },
#if ARCH_BITS == 64
        {   ~(RTCCUINTXREG)UINT32_MAX,      /* -> */    0,  true,  X86_EFL_ZF },
#endif
        {   ~(RTCCUINTXREG)0,               /* -> */    0,  false, 0 },
        {   ~(RTCCUINTXREG)1,               /* -> */    1,  false, 0 },
        {   UINT16_C(0x8000),               /* -> */    15, false, 0 },
        {   UINT16_C(0x4560),               /* -> */    5,  false, 0 },
        {   UINT32_C(0x80000000),           /* -> */    31, false, 0 },
        {   UINT32_C(0x45600000),           /* -> */    21, false, 0 },
    };
    static BS3CPUINSTR2_SUBTEST_BITSCAN_T const s_aSubTestsTzCnt32[] =
    {
        {   0,                              /* -> */    32, false, X86_EFL_CF },
#if ARCH_BITS == 64
        {   ~(RTCCUINTXREG)UINT32_MAX,      /* -> */    32, false, X86_EFL_CF },
#endif
        {   ~(RTCCUINTXREG)0,               /* -> */    0,  false, X86_EFL_ZF },
        {   ~(RTCCUINTXREG)1,               /* -> */    1,  false, 0 },
        {   UINT16_C(0x8000),               /* -> */    15, false, 0 },
        {   UINT16_C(0x4560),               /* -> */    5,  false, 0 },
        {   UINT32_C(0x80000000),           /* -> */    31, false, 0 },
        {   UINT32_C(0x45600000),           /* -> */    21, false, 0 },
    };
#if ARCH_BITS == 64
    static BS3CPUINSTR2_SUBTEST_BITSCAN_T const s_aSubTestsBsf64[] =
    {
        {   0,                              /* -> */    0,  true,  X86_EFL_ZF },
        {   ~(RTCCUINTXREG)0,               /* -> */    0,  false, 0 },
        {   ~(RTCCUINTXREG)1,               /* -> */    1,  false, 0 },
        {   UINT16_C(0x8000),               /* -> */    15, false, 0 },
        {   UINT16_C(0x4560),               /* -> */    5,  false, 0 },
        {   UINT32_C(0x80000000),           /* -> */    31, false, 0 },
        {   UINT32_C(0x45600000),           /* -> */    21, false, 0 },
        {   UINT64_C(0x8000000000000000),   /* -> */    63, false, 0 },
        {   UINT64_C(0x4560000000000000),   /* -> */    53, false, 0 },
    };
    static BS3CPUINSTR2_SUBTEST_BITSCAN_T const s_aSubTestsTzCnt64[] =
    {
        {   0,                              /* -> */    64, false, X86_EFL_CF },
        {   ~(RTCCUINTXREG)0,               /* -> */    0,  false, X86_EFL_ZF },
        {   ~(RTCCUINTXREG)1,               /* -> */    1,  false, 0 },
        {   UINT16_C(0x8000),               /* -> */    15, false, 0 },
        {   UINT16_C(0x4560),               /* -> */    5,  false, 0 },
        {   UINT32_C(0x80000000),           /* -> */    31, false, 0 },
        {   UINT32_C(0x45600000),           /* -> */    21, false, 0 },
        {   UINT64_C(0x8000000000000000),   /* -> */    63, false, 0 },
        {   UINT64_C(0x4560000000000000),   /* -> */    53, false, 0 },
    };
#endif
    static BS3CPUINSTR2_TEST_BITSCAN_T s_aTests[] =
    {
        {   BS3_CMN_NM(bs3CpuInstr2_bsf_AX_BX_ud2),         false,  3 + (ARCH_BITS != 16), 16, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsf16), s_aSubTestsBsf16 },
        {   BS3_CMN_NM(bs3CpuInstr2_bsf_AX_FSxBX_ud2),      true,   4 + (ARCH_BITS != 16), 16, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsf16), s_aSubTestsBsf16 },
        {   BS3_CMN_NM(bs3CpuInstr2_bsf_EAX_EBX_ud2),       false,  3 + (ARCH_BITS == 16), 32, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsf32), s_aSubTestsBsf32 },
        {   BS3_CMN_NM(bs3CpuInstr2_bsf_EAX_FSxBX_ud2),     true,   4 + (ARCH_BITS == 16), 32, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsf32), s_aSubTestsBsf32 },
#if ARCH_BITS == 64
        {   BS3_CMN_NM(bs3CpuInstr2_bsf_RAX_RBX_ud2),       false,  4,                     64, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsf64), s_aSubTestsBsf64 },
        {   BS3_CMN_NM(bs3CpuInstr2_bsf_RAX_FSxBX_ud2),     true,   5,                     64, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsf64), s_aSubTestsBsf64 },
#endif
        /* f2 prefixed variant: */
        {   BS3_CMN_NM(bs3CpuInstr2_f2_bsf_AX_BX_ud2),      false,  4 + (ARCH_BITS != 16), 16, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsf16), s_aSubTestsBsf16 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_bsf_AX_FSxBX_ud2),   true,   5 + (ARCH_BITS != 16), 16, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsf16), s_aSubTestsBsf16 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_bsf_EAX_EBX_ud2),    false,  4 + (ARCH_BITS == 16), 32, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsf32), s_aSubTestsBsf32 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_bsf_EAX_FSxBX_ud2),  true,   5 + (ARCH_BITS == 16), 32, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsf32), s_aSubTestsBsf32 },
#if ARCH_BITS == 64
        {   BS3_CMN_NM(bs3CpuInstr2_f2_bsf_RAX_RBX_ud2),    false,  5,                     64, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsf64), s_aSubTestsBsf64 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_bsf_RAX_FSxBX_ud2),  true,   6,                     64, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsf64), s_aSubTestsBsf64 },
#endif

        /* tzcnt: */
        {   BS3_CMN_NM(bs3CpuInstr2_tzcnt_AX_BX_ud2),       false,  4 + (ARCH_BITS != 16), 16, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsTzCnt16), s_aSubTestsTzCnt16 },
        {   BS3_CMN_NM(bs3CpuInstr2_tzcnt_AX_FSxBX_ud2),    true,   5 + (ARCH_BITS != 16), 16, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsTzCnt16), s_aSubTestsTzCnt16 },
        {   BS3_CMN_NM(bs3CpuInstr2_tzcnt_EAX_EBX_ud2),     false,  4 + (ARCH_BITS == 16), 32, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsTzCnt32), s_aSubTestsTzCnt32 },
        {   BS3_CMN_NM(bs3CpuInstr2_tzcnt_EAX_FSxBX_ud2),   true,   5 + (ARCH_BITS == 16), 32, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsTzCnt32), s_aSubTestsTzCnt32 },
#if ARCH_BITS == 64
        {   BS3_CMN_NM(bs3CpuInstr2_tzcnt_RAX_RBX_ud2),     false,  5,                     64, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsTzCnt64), s_aSubTestsTzCnt64 },
        {   BS3_CMN_NM(bs3CpuInstr2_tzcnt_RAX_FSxBX_ud2),   true,   6,                     64, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsTzCnt64), s_aSubTestsTzCnt64 },
#endif
        /* f2 prefixed tzcnt variant (last prefix (f3) should prevail): */
        {   BS3_CMN_NM(bs3CpuInstr2_f2_tzcnt_AX_BX_ud2),    false,  5 + (ARCH_BITS != 16), 16, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsTzCnt16), s_aSubTestsTzCnt16 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_tzcnt_AX_FSxBX_ud2), true,   6 + (ARCH_BITS != 16), 16, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsTzCnt16), s_aSubTestsTzCnt16 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_tzcnt_EAX_EBX_ud2),  false,  5 + (ARCH_BITS == 16), 32, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsTzCnt32), s_aSubTestsTzCnt32 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_tzcnt_EAX_FSxBX_ud2),true,   6 + (ARCH_BITS == 16), 32, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsTzCnt32), s_aSubTestsTzCnt32 },
#if ARCH_BITS == 64
        {   BS3_CMN_NM(bs3CpuInstr2_f2_tzcnt_RAX_RBX_ud2),  false,  6,                     64, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsTzCnt64), s_aSubTestsTzCnt64 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_tzcnt_RAX_FSxBX_ud2),true,   7,                     64, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsTzCnt64), s_aSubTestsTzCnt64 },
#endif
    };

    uint32_t uStdExtFeatEbx = 0;
    if (g_uBs3CpuDetected & BS3CPU_F_CPUID)
        ASMCpuIdExSlow(7, 0, 0, 0, NULL, &uStdExtFeatEbx, NULL, NULL);
    if (!(uStdExtFeatEbx & X86_CPUID_STEXT_FEATURE_EBX_BMI1))
    {
        unsigned i = RT_ELEMENTS(s_aTests);
        while (i-- > 0)
            if (s_aTests[i].fEflCheck & X86_EFL_CF)
            {
                s_aTests[i].fEflCheck = X86_EFL_ZF;
                switch (s_aTests[i].cOpBits)
                {
                    case 16:
                        s_aTests[i].cSubTests  = RT_ELEMENTS(s_aSubTestsBsf16);
                        s_aTests[i].paSubTests = s_aSubTestsBsf16;
                        break;
                    case 32:
                        s_aTests[i].cSubTests  = RT_ELEMENTS(s_aSubTestsBsf32);
                        s_aTests[i].paSubTests = s_aSubTestsBsf32;
                        break;
#if ARCH_BITS == 64
                    case 64:
                        s_aTests[i].cSubTests  = RT_ELEMENTS(s_aSubTestsBsf64);
                        s_aTests[i].paSubTests = s_aSubTestsBsf64;
                        break;
#endif
                }
            }
        Bs3TestPrintf("tzcnt not supported\n");
    }

    return bs3CpuInstr2_BitScan(bMode, s_aTests, RT_ELEMENTS(s_aTests));
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_bsr_lzcnt)(uint8_t bMode)
{
    static BS3CPUINSTR2_SUBTEST_BITSCAN_T const s_aSubTestsBsr16[] =
    {
        {   0,                              /* -> */    0,  true,  X86_EFL_ZF },
        {   ~(RTCCUINTXREG)UINT16_MAX,      /* -> */    0,  true,  X86_EFL_ZF },
        {   ~(RTCCUINTXREG)0,               /* -> */    15, false, 0 },
        {   ~(RTCCUINTXREG)1,               /* -> */    15, false, 0 },
        {   UINT16_C(0x0001),               /* -> */    0,  false, 0 },
        {   UINT16_C(0x0002),               /* -> */    1,  false, 0 },
        {   UINT16_C(0x4560),               /* -> */    14, false, 0 },
    };
    static BS3CPUINSTR2_SUBTEST_BITSCAN_T const s_aSubTestsLzCnt16[] =
    {
        {   0,                              /* -> */    16, false, X86_EFL_CF },
        {   ~(RTCCUINTXREG)UINT16_MAX,      /* -> */    16, false, X86_EFL_CF },
        {   ~(RTCCUINTXREG)0,               /* -> */    0,  false, X86_EFL_ZF },
        {   ~(RTCCUINTXREG)1,               /* -> */    0,  false, X86_EFL_ZF },
        {   UINT16_C(0x8000),               /* -> */    0,  false, X86_EFL_ZF },
        {   UINT16_C(0x4560),               /* -> */    1,  false, 0 },
        {   UINT16_C(0x003f),               /* -> */    10, false, 0 },
        {   UINT16_C(0x0001),               /* -> */    15, false, 0 },
    };
    static BS3CPUINSTR2_SUBTEST_BITSCAN_T const s_aSubTestsBsr32[] =
    {
        {   0,                              /* -> */    0,  true,  X86_EFL_ZF },
#if ARCH_BITS == 64
        {   ~(RTCCUINTXREG)UINT32_MAX,      /* -> */    0,  true,  X86_EFL_ZF },
#endif
        {   ~(RTCCUINTXREG)0,               /* -> */    31, false, 0 },
        {   ~(RTCCUINTXREG)1,               /* -> */    31, false, 0 },
        {   1,                              /* -> */    0,  false, 0 },
        {   2,                              /* -> */    1,  false, 0 },
        {   UINT16_C(0x8000),               /* -> */    15, false, 0 },
        {   UINT16_C(0x4560),               /* -> */    14, false, 0 },
        {   UINT32_C(0x80000000),           /* -> */    31, false, 0 },
        {   UINT32_C(0x45600000),           /* -> */    30, false, 0 },
    };
    static BS3CPUINSTR2_SUBTEST_BITSCAN_T const s_aSubTestsLzCnt32[] =
    {
        {   0,                              /* -> */    32, false, X86_EFL_CF },
#if ARCH_BITS == 64
        {   ~(RTCCUINTXREG)UINT32_MAX,      /* -> */    32, false, X86_EFL_CF },
#endif
        {   ~(RTCCUINTXREG)0,               /* -> */    0,  false, X86_EFL_ZF },
        {   ~(RTCCUINTXREG)1,               /* -> */    0,  false, X86_EFL_ZF },
        {   1,                              /* -> */    31, false, 0 },
        {   2,                              /* -> */    30, false, 0},
        {   UINT16_C(0x8000),               /* -> */    16, false, 0 },
        {   UINT16_C(0x4560),               /* -> */    17, false, 0 },
        {   UINT32_C(0x80000000),           /* -> */    0,  false, X86_EFL_ZF },
        {   UINT32_C(0x45600000),           /* -> */    1,  false, 0 },
        {   UINT32_C(0x0000ffff),           /* -> */    16, false, 0 },
    };
#if ARCH_BITS == 64
    static BS3CPUINSTR2_SUBTEST_BITSCAN_T const s_aSubTestsBsr64[] =
    {
        {   0,                              /* -> */    0,  true,  X86_EFL_ZF },
        {   ~(RTCCUINTXREG)0,               /* -> */    63, false, 0 },
        {   ~(RTCCUINTXREG)1,               /* -> */    63, false, 0 },
        {   1,                              /* -> */    0,  false, 0 },
        {   2,                              /* -> */    1,  false, 0 },
        {   UINT16_C(0x8000),               /* -> */    15, false, 0 },
        {   UINT16_C(0x4560),               /* -> */    14, false, 0 },
        {   UINT32_C(0x80000000),           /* -> */    31, false, 0 },
        {   UINT32_C(0x45600000),           /* -> */    30, false, 0 },
        {   UINT64_C(0x8000000000000000),   /* -> */    63, false, 0 },
        {   UINT64_C(0x0045600000000000),   /* -> */    54, false, 0 },
    };
    static BS3CPUINSTR2_SUBTEST_BITSCAN_T const s_aSubTestsLzCnt64[] =
    {
        {   0,                              /* -> */    64, false, X86_EFL_CF },
        {   ~(RTCCUINTXREG)0,               /* -> */    0,  false, X86_EFL_ZF },
        {   ~(RTCCUINTXREG)1,               /* -> */    0,  false, X86_EFL_ZF },
        {   1,                              /* -> */    63, false, 0 },
        {   2,                              /* -> */    62, false, 0 },
        {   UINT16_C(0x8000),               /* -> */    48, false, 0 },
        {   UINT16_C(0x4560),               /* -> */    49, false, 0 },
        {   UINT32_C(0x80000000),           /* -> */    32, false, 0 },
        {   UINT32_C(0x45600000),           /* -> */    33, false, 0 },
        {   UINT64_C(0x8000000000000000),   /* -> */    0,  false, X86_EFL_ZF },
        {   UINT64_C(0x4560000000000000),   /* -> */    1,  false, 0 },
        {   UINT64_C(0x0045600000000000),   /* -> */    9,  false, 0 },
    };
#endif
    static BS3CPUINSTR2_TEST_BITSCAN_T s_aTests[] =
    {
        {   BS3_CMN_NM(bs3CpuInstr2_bsr_AX_BX_ud2),         false,  3 + (ARCH_BITS != 16), 16, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsr16), s_aSubTestsBsr16 },
        {   BS3_CMN_NM(bs3CpuInstr2_bsr_AX_FSxBX_ud2),      true,   4 + (ARCH_BITS != 16), 16, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsr16), s_aSubTestsBsr16 },
        {   BS3_CMN_NM(bs3CpuInstr2_bsr_EAX_EBX_ud2),       false,  3 + (ARCH_BITS == 16), 32, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsr32), s_aSubTestsBsr32 },
        {   BS3_CMN_NM(bs3CpuInstr2_bsr_EAX_FSxBX_ud2),     true,   4 + (ARCH_BITS == 16), 32, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsr32), s_aSubTestsBsr32 },
#if ARCH_BITS == 64
        {   BS3_CMN_NM(bs3CpuInstr2_bsr_RAX_RBX_ud2),       false,  4,                     64, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsr64), s_aSubTestsBsr64 },
        {   BS3_CMN_NM(bs3CpuInstr2_bsr_RAX_FSxBX_ud2),     true,   5,                     64, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsr64), s_aSubTestsBsr64 },
#endif
        /* f2 prefixed variant: */
        {   BS3_CMN_NM(bs3CpuInstr2_f2_bsr_AX_BX_ud2),      false,  4 + (ARCH_BITS != 16), 16, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsr16), s_aSubTestsBsr16 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_bsr_AX_FSxBX_ud2),   true,   5 + (ARCH_BITS != 16), 16, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsr16), s_aSubTestsBsr16 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_bsr_EAX_EBX_ud2),    false,  4 + (ARCH_BITS == 16), 32, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsr32), s_aSubTestsBsr32 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_bsr_EAX_FSxBX_ud2),  true,   5 + (ARCH_BITS == 16), 32, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsr32), s_aSubTestsBsr32 },
#if ARCH_BITS == 64
        {   BS3_CMN_NM(bs3CpuInstr2_f2_bsr_RAX_RBX_ud2),    false,  5,                     64, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsr64), s_aSubTestsBsr64 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_bsr_RAX_FSxBX_ud2),  true,   6,                     64, X86_EFL_ZF,
            RT_ELEMENTS(s_aSubTestsBsr64), s_aSubTestsBsr64 },
#endif

        /* lzcnt: */
        {   BS3_CMN_NM(bs3CpuInstr2_lzcnt_AX_BX_ud2),       false,  4 + (ARCH_BITS != 16), 16, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsLzCnt16), s_aSubTestsLzCnt16 },
        {   BS3_CMN_NM(bs3CpuInstr2_lzcnt_AX_FSxBX_ud2),    true,   5 + (ARCH_BITS != 16), 16, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsLzCnt16), s_aSubTestsLzCnt16 },
        {   BS3_CMN_NM(bs3CpuInstr2_lzcnt_EAX_EBX_ud2),     false,  4 + (ARCH_BITS == 16), 32, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsLzCnt32), s_aSubTestsLzCnt32 },
        {   BS3_CMN_NM(bs3CpuInstr2_lzcnt_EAX_FSxBX_ud2),   true,   5 + (ARCH_BITS == 16), 32, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsLzCnt32), s_aSubTestsLzCnt32 },
#if ARCH_BITS == 64
        {   BS3_CMN_NM(bs3CpuInstr2_lzcnt_RAX_RBX_ud2),     false,  5,                     64, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsLzCnt64), s_aSubTestsLzCnt64 },
        {   BS3_CMN_NM(bs3CpuInstr2_lzcnt_RAX_FSxBX_ud2),   true,   6,                     64, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsLzCnt64), s_aSubTestsLzCnt64 },
#endif
        /* f2 prefixed lzcnt variant (last prefix (f3) should prevail): */
        {   BS3_CMN_NM(bs3CpuInstr2_f2_lzcnt_AX_BX_ud2),    false,  5 + (ARCH_BITS != 16), 16, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsLzCnt16), s_aSubTestsLzCnt16 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_lzcnt_AX_FSxBX_ud2), true,   6 + (ARCH_BITS != 16), 16, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsLzCnt16), s_aSubTestsLzCnt16 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_lzcnt_EAX_EBX_ud2),  false,  5 + (ARCH_BITS == 16), 32, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsLzCnt32), s_aSubTestsLzCnt32 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_lzcnt_EAX_FSxBX_ud2),true,   6 + (ARCH_BITS == 16), 32, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsLzCnt32), s_aSubTestsLzCnt32 },
#if ARCH_BITS == 64
        {   BS3_CMN_NM(bs3CpuInstr2_f2_lzcnt_RAX_RBX_ud2),  false,  6,                     64, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsLzCnt64), s_aSubTestsLzCnt64 },
        {   BS3_CMN_NM(bs3CpuInstr2_f2_lzcnt_RAX_FSxBX_ud2),true,   7,                     64, X86_EFL_ZF | X86_EFL_CF,
            RT_ELEMENTS(s_aSubTestsLzCnt64), s_aSubTestsLzCnt64 },
#endif
    };

    uint32_t uExtFeatEcx = 0;
    if (g_uBs3CpuDetected & BS3CPU_F_CPUID_EXT_LEAVES)
        ASMCpuIdExSlow(UINT32_C(0x80000001), 0, 0, 0, NULL, NULL, &uExtFeatEcx, NULL);
    if (!(uExtFeatEcx & X86_CPUID_AMD_FEATURE_ECX_ABM))
    {
        unsigned i = RT_ELEMENTS(s_aTests);
        while (i-- > 0)
            if (s_aTests[i].fEflCheck & X86_EFL_CF)
            {
                s_aTests[i].fEflCheck = X86_EFL_ZF;
                switch (s_aTests[i].cOpBits)
                {
                    case 16:
                        s_aTests[i].cSubTests  = RT_ELEMENTS(s_aSubTestsBsr16);
                        s_aTests[i].paSubTests = s_aSubTestsBsr16;
                        break;
                    case 32:
                        s_aTests[i].cSubTests  = RT_ELEMENTS(s_aSubTestsBsr32);
                        s_aTests[i].paSubTests = s_aSubTestsBsr32;
                        break;
#if ARCH_BITS == 64
                    case 64:
                        s_aTests[i].cSubTests  = RT_ELEMENTS(s_aSubTestsBsr64);
                        s_aTests[i].paSubTests = s_aSubTestsBsr64;
                        break;
#endif
                }
            }
        Bs3TestPrintf("lzcnt not supported\n");
    }

    return bs3CpuInstr2_BitScan(bMode, s_aTests, RT_ELEMENTS(s_aTests));
}


/**
 * RORX
 */
BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_rorx)(uint8_t bMode)
{
    static const struct
    {
        FPFNBS3FAR      pfnWorker;
        bool            fMemSrc;
        bool            fOkay;
        RTCCUINTXREG    uIn;
        RTCCUINTXREG    uOut;
    } s_aTests[] =
    {
        /* 64 bits register width (32 bits in 32- and 16-bit modes): */
        {   BS3_CMN_NM(bs3CpuInstr2_rorx_RBX_RDX_2_icebp),      false, true,    // #0
            0,                      /* -> */ 0 },
        {   BS3_CMN_NM(bs3CpuInstr2_rorx_RBX_RDX_2_icebp),      false, true,    // #1
            ~(RTCCUINTXREG)2,       /* -> */ ~(RTCCUINTXREG)0 >> 1 },
        {   BS3_CMN_NM(bs3CpuInstr2_rorx_RBX_DSxDI_68_icebp),   true,  true,    // #2
            0,                      /* -> */ 0 },
        {   BS3_CMN_NM(bs3CpuInstr2_rorx_RBX_DSxDI_68_icebp),   true,  true,    // #3
            ~(RTCCUINTXREG)2,       /* -> */ (RTCCUINTXREG_MAX >> 4) | (~(RTCCUINTXREG)2 << (sizeof(RTCCUINTXREG) * 8 - 4)) },

        /* 32 bits register width: */
        {   BS3_CMN_NM(bs3CpuInstr2_rorx_EBX_EDX_2_icebp),      false, true,    // #4
            0,                      /* -> */ 0 },
        {   BS3_CMN_NM(bs3CpuInstr2_rorx_EBX_EDX_2_icebp),      false, true,    // #5
            ~(RTCCUINTXREG)2,       /* -> */ (RTCCUINTXREG)(~(uint32_t)0 >> 1) },
        {   BS3_CMN_NM(bs3CpuInstr2_rorx_EBX_DSxDI_36_icebp),   true,  true,    // #6
            0,                      /* -> */ 0 },
        {   BS3_CMN_NM(bs3CpuInstr2_rorx_EBX_DSxDI_36_icebp),   true,  true,    // #7
            ~(RTCCUINTXREG)2,       /* -> */ (RTCCUINTXREG)UINT32_C(0xdfffffff) },

        /* encoding tests: */
        {   BS3_CMN_NM(bs3CpuInstr2_rorx_EBX_EDX_2_icebp_L1),   false, false,   // #8
            RTCCUINTXREG_MAX,       /* -> */ 0 },
        {   BS3_CMN_NM(bs3CpuInstr2_rorx_EBX_EDX_2_icebp_V1),   false, false,   // #9
            RTCCUINTXREG_MAX,       /* -> */ 0 },
        {   BS3_CMN_NM(bs3CpuInstr2_rorx_EBX_EDX_2_icebp_V15),  false, false,   // #10
            RTCCUINTXREG_MAX,       /* -> */ 0 },
# if ARCH_BITS == 64 /* The VEX.X=0 encoding mean LES instruction in 32-bit and 16-bit mode. */
        {   BS3_CMN_NM(bs3CpuInstr2_rorx_EBX_EDX_2_icebp_X1),   false, true,    // #11
            UINT32_C(0xf1e2d3c5),   /* -> */ (RTCCUINTXREG)UINT32_C(0x7c78b4f1) },
# endif
    };

    BS3REGCTX       Ctx;
    BS3TRAPFRAME    TrapFrame;
    unsigned        i, j;
    uint32_t        uStdExtFeatEbx = 0;
    bool            fSupportsRorX;

    if (g_uBs3CpuDetected & BS3CPU_F_CPUID)
        ASMCpuIdExSlow(7, 0, 0, 0, NULL, &uStdExtFeatEbx, NULL, NULL);
    fSupportsRorX = RT_BOOL(uStdExtFeatEbx & X86_CPUID_STEXT_FEATURE_EBX_BMI2);

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);

    /*
     * Do the tests twice, first with all flags set, then once again with
     * flags cleared.  The flags are not supposed to be touched at all.
     */
    Ctx.rflags.u16 |= X86_EFL_STATUS_BITS;
    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            bool const    fOkay       = !BS3_MODE_IS_RM_OR_V86(bMode) && s_aTests[i].fOkay && fSupportsRorX;
            uint8_t const bExpectXcpt = fOkay ? X86_XCPT_DB : X86_XCPT_UD;
            uint64_t      uExpectRbx, uExpectRip;
            RTCCUINTXREG  uMemSrc, uMemSrcExpect;
            Ctx.rbx.uCcXReg = RTCCUINTXREG_MAX * 1019;
            if (!s_aTests[i].fMemSrc)
            {
                Ctx.rdx.uCcXReg         = s_aTests[i].uIn;
                uMemSrcExpect = uMemSrc = ~s_aTests[i].uIn;
            }
            else
            {
                Ctx.rdx.uCcXReg         = ~s_aTests[i].uIn;
                uMemSrcExpect = uMemSrc = s_aTests[i].uIn;
                Bs3RegCtxSetGrpDsFromCurPtr(&Ctx, &Ctx.rdi, &uMemSrc);
            }
            Bs3RegCtxSetRipCsFromCurPtr(&Ctx, s_aTests[i].pfnWorker);
            uExpectRbx = fOkay ? s_aTests[i].uOut : Ctx.rbx.u;
            uExpectRip = Ctx.rip.u + (fOkay ? 6 + 1 : 0);
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);

            if (   TrapFrame.bXcpt != bExpectXcpt
                || TrapFrame.Ctx.rip.u != uExpectRip
                || TrapFrame.Ctx.rdx.u != Ctx.rdx.u
                || TrapFrame.Ctx.rbx.u != uExpectRbx
                /* check that nothing else really changed: */
                || (TrapFrame.Ctx.rflags.u16 & X86_EFL_STATUS_BITS) != (Ctx.rflags.u16 & X86_EFL_STATUS_BITS)
                || TrapFrame.Ctx.rax.u != Ctx.rax.u
                || TrapFrame.Ctx.rcx.u != Ctx.rcx.u
                || TrapFrame.Ctx.rsp.u != Ctx.rsp.u
                || TrapFrame.Ctx.rbp.u != Ctx.rbp.u
                || TrapFrame.Ctx.rsi.u != Ctx.rsi.u
                || TrapFrame.Ctx.rdi.u != Ctx.rdi.u
                || uMemSrc != uMemSrcExpect
               )
            {
                Bs3TestFailedF("test #%i failed: input %#" RTCCUINTXREG_XFMT, i, s_aTests[i].uIn);
                if (TrapFrame.bXcpt != bExpectXcpt)
                    Bs3TestFailedF("Expected bXcpt = %#x, got %#x", bExpectXcpt, TrapFrame.bXcpt);
                if (TrapFrame.Ctx.rip.u != uExpectRip)
                    Bs3TestFailedF("Expected RIP = %#06RX64, got %#06RX64", uExpectRip, TrapFrame.Ctx.rip.u);
                if (TrapFrame.Ctx.rdx.u != Ctx.rdx.u)
                    Bs3TestFailedF("Expected RDX = %#06RX64, got %#06RX64 (src)", Ctx.rdx.u, TrapFrame.Ctx.rdx.u);
                if (TrapFrame.Ctx.rbx.u != uExpectRbx)
                    Bs3TestFailedF("Expected RBX = %#06RX64, got %#06RX64 (dst)", uExpectRbx, TrapFrame.Ctx.rbx.u);

                if ((TrapFrame.Ctx.rflags.u16 & X86_EFL_STATUS_BITS) != (Ctx.rflags.u16 & X86_EFL_STATUS_BITS))
                    Bs3TestFailedF("Expected EFLAGS = %#06RX64, got %#06RX64",
                                   Ctx.rflags.u16 & X86_EFL_STATUS_BITS, TrapFrame.Ctx.rflags.u16 & X86_EFL_STATUS_BITS);
                if (TrapFrame.Ctx.rax.u != Ctx.rax.u)
                    Bs3TestFailedF("Expected RAX = %#06RX64, got %#06RX64", Ctx.rax.u, TrapFrame.Ctx.rax.u);
                if (TrapFrame.Ctx.rcx.u != Ctx.rcx.u)
                    Bs3TestFailedF("Expected RCX = %#06RX64, got %#06RX64", Ctx.rcx.u, TrapFrame.Ctx.rcx.u);
                if (TrapFrame.Ctx.rsp.u != Ctx.rsp.u)
                    Bs3TestFailedF("Expected RSP = %#06RX64, got %#06RX64", Ctx.rsp.u, TrapFrame.Ctx.rsp.u);
                if (TrapFrame.Ctx.rbp.u != Ctx.rbp.u)
                    Bs3TestFailedF("Expected RBP = %#06RX64, got %#06RX64", Ctx.rbp.u, TrapFrame.Ctx.rbp.u);
                if (TrapFrame.Ctx.rsi.u != Ctx.rsi.u)
                    Bs3TestFailedF("Expected RSI = %#06RX64, got %#06RX64", Ctx.rsi.u, TrapFrame.Ctx.rsi.u);
                if (TrapFrame.Ctx.rdi.u != Ctx.rdi.u)
                    Bs3TestFailedF("Expected RDI = %#06RX64, got %#06RX64", Ctx.rdi.u, TrapFrame.Ctx.rdi.u);
                if (uMemSrc != uMemSrcExpect)
                    Bs3TestFailedF("Expected uMemSrc = %#06RX64, got %#06RX64", (uint64_t)uMemSrcExpect, (uint64_t)uMemSrc);
            }
        }
        Ctx.rflags.u16 &= ~X86_EFL_STATUS_BITS;
    }

    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_andn)(uint8_t bMode)
{
#define ANDN_CHECK_EFLAGS  (uint16_t)(X86_EFL_CF | X86_EFL_ZF | X86_EFL_OF | X86_EFL_SF)
#define ANDN_IGNORE_EFLAGS (uint16_t)(X86_EFL_AF | X86_EFL_PF) /* undefined, ignoring for now */
    static const struct
    {
        FPFNBS3FAR      pfnWorker;
        bool            fMemSrc;
        uint8_t         cbInstr;
        RTCCUINTXREG    uSrc1;
        RTCCUINTXREG    uSrc2;
        RTCCUINTXREG    uOut;
        uint16_t        fEFlags;
    } s_aTests[] =
    {
        /* 64 bits register width (32 bits in 32- and 16-bit modes): */
        {   BS3_CMN_NM(bs3CpuInstr2_andn_RAX_RCX_RBX_icebp),    false,    5,                            // #0
            0,                      0,                      /* -> */ 0,                 X86_EFL_ZF },
        {   BS3_CMN_NM(bs3CpuInstr2_andn_RAX_RCX_RBX_icebp),    false,    5,                            // #1
            2,                      ~(RTCCUINTXREG)3,       /* -> */ ~(RTCCUINTXREG)3,  X86_EFL_SF },
        {   BS3_CMN_NM(bs3CpuInstr2_andn_RAX_RCX_FSxBX_icebp),  true,     6,                            // #2
            0,                      0,                      /* -> */ 0,                 X86_EFL_ZF },
        {   BS3_CMN_NM(bs3CpuInstr2_andn_RAX_RCX_FSxBX_icebp),  true,     6,                            // #3
            2,                      ~(RTCCUINTXREG)3,       /* -> */ ~(RTCCUINTXREG)3,  X86_EFL_SF },

        /* 32-bit register width */
        {   BS3_CMN_NM(bs3CpuInstr2_andn_EAX_ECX_EBX_icebp),    false,    5,                            // #4
            0,                      0,                      /* -> */ 0,                 X86_EFL_ZF },
        {   BS3_CMN_NM(bs3CpuInstr2_andn_EAX_ECX_EBX_icebp),    false,    5,                            // #5
            2,                      ~(RTCCUINTXREG)7,       /* -> */ ~(uint32_t)7,      X86_EFL_SF },
        {   BS3_CMN_NM(bs3CpuInstr2_andn_EAX_ECX_FSxBX_icebp),  true,     6,                            // #6
            0,                      0,                      /* -> */ 0,                 X86_EFL_ZF },
        {   BS3_CMN_NM(bs3CpuInstr2_andn_EAX_ECX_FSxBX_icebp),  true,     6,                            // #7
            2,                      ~(RTCCUINTXREG)7,       /* -> */ ~(uint32_t)7,      X86_EFL_SF },

    };

    BS3REGCTX       Ctx;
    BS3TRAPFRAME    TrapFrame;
    unsigned        i, j;
    uint32_t        uStdExtFeatEbx = 0;
    bool            fSupportsAndN;

    if (g_uBs3CpuDetected & BS3CPU_F_CPUID)
        ASMCpuIdExSlow(7, 0, 0, 0, NULL, &uStdExtFeatEbx, NULL, NULL);
    fSupportsAndN = RT_BOOL(uStdExtFeatEbx & X86_CPUID_STEXT_FEATURE_EBX_BMI1);

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);

    /*
     * Do the tests twice, first with all flags set, then once again with
     * flags cleared.  The flags are not supposed to be touched at all.
     */
    Ctx.rflags.u16 |= X86_EFL_STATUS_BITS;
    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            bool const    fOkay       = !BS3_MODE_IS_RM_OR_V86(bMode) && fSupportsAndN;
            uint8_t const bExpectXcpt = fOkay ? X86_XCPT_DB : X86_XCPT_UD;
            uint64_t      uExpectRax, uExpectRip;
            RTCCUINTXREG  uMemSrc2, uMemSrc2Expect;

            Ctx.rax.uCcXReg = RTCCUINTXREG_MAX * 1019;
            Ctx.rcx.uCcXReg = s_aTests[i].uSrc1;
            if (!s_aTests[i].fMemSrc)
            {
                Ctx.rbx.uCcXReg           = s_aTests[i].uSrc2;
                uMemSrc2Expect = uMemSrc2 = ~s_aTests[i].uSrc2;
            }
            else
            {
                uMemSrc2Expect = uMemSrc2 = s_aTests[i].uSrc2;
                Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, &Ctx.fs, &uMemSrc2);
            }
            Bs3RegCtxSetRipCsFromCurPtr(&Ctx, s_aTests[i].pfnWorker);
            uExpectRax = fOkay ? s_aTests[i].uOut : Ctx.rax.u;
            uExpectRip = Ctx.rip.u + (fOkay ? s_aTests[i].cbInstr + 1 : 0);
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);

            if (   TrapFrame.bXcpt != bExpectXcpt
                || TrapFrame.Ctx.rip.u != uExpectRip
                || TrapFrame.Ctx.rcx.u != Ctx.rcx.u
                || TrapFrame.Ctx.rbx.u != Ctx.rbx.u
                || TrapFrame.Ctx.rax.u != uExpectRax
                /* check that nothing else really changed: */
                ||    (TrapFrame.Ctx.rflags.u16 & ANDN_CHECK_EFLAGS)
                   != ((fOkay ? s_aTests[i].fEFlags : Ctx.rflags.u16) & ANDN_CHECK_EFLAGS)
                ||    (TrapFrame.Ctx.rflags.u16 & ~(ANDN_CHECK_EFLAGS | ANDN_IGNORE_EFLAGS) & X86_EFL_STATUS_BITS)
                   != (Ctx.rflags.u16           & ~(ANDN_CHECK_EFLAGS | ANDN_IGNORE_EFLAGS) & X86_EFL_STATUS_BITS)
                || TrapFrame.Ctx.rdx.u != Ctx.rdx.u
                || TrapFrame.Ctx.rsp.u != Ctx.rsp.u
                || TrapFrame.Ctx.rbp.u != Ctx.rbp.u
                || TrapFrame.Ctx.rsi.u != Ctx.rsi.u
                || TrapFrame.Ctx.rdi.u != Ctx.rdi.u
                || uMemSrc2 != uMemSrc2Expect
               )
            {
                Bs3TestFailedF("test #%i failed: input %#" RTCCUINTXREG_XFMT ", %#" RTCCUINTXREG_XFMT, i, s_aTests[i].uSrc1, s_aTests[i].uSrc2);
                if (TrapFrame.bXcpt != bExpectXcpt)
                    Bs3TestFailedF("Expected bXcpt = %#x, got %#x", bExpectXcpt, TrapFrame.bXcpt);
                if (TrapFrame.Ctx.rip.u != uExpectRip)
                    Bs3TestFailedF("Expected RIP = %#06RX64, got %#06RX64", uExpectRip, TrapFrame.Ctx.rip.u);
                if (TrapFrame.Ctx.rax.u != uExpectRax)
                    Bs3TestFailedF("Expected RAX = %#06RX64, got %#06RX64", uExpectRax, TrapFrame.Ctx.rax.u);
                if (TrapFrame.Ctx.rcx.u != Ctx.rcx.u)
                    Bs3TestFailedF("Expected RCX = %#06RX64, got %#06RX64", Ctx.rcx.u, TrapFrame.Ctx.rcx.u);
                if (TrapFrame.Ctx.rbx.u != Ctx.rbx.u)
                    Bs3TestFailedF("Expected RBX = %#06RX64, got %#06RX64 (dst)", Ctx.rbx.u, TrapFrame.Ctx.rbx.u);
                if (   (TrapFrame.Ctx.rflags.u16 & ANDN_CHECK_EFLAGS)
                    != ((fOkay ? s_aTests[i].fEFlags : Ctx.rflags.u16) & ANDN_CHECK_EFLAGS))
                    Bs3TestFailedF("Expected EFLAGS = %#06RX32, got %#06RX32 (output)",
                                   (fOkay ? s_aTests[i].fEFlags : Ctx.rflags.u16) & ANDN_CHECK_EFLAGS, TrapFrame.Ctx.rflags.u16 & ANDN_CHECK_EFLAGS);
                if (   (TrapFrame.Ctx.rflags.u16 & ~(ANDN_CHECK_EFLAGS | ANDN_IGNORE_EFLAGS) & X86_EFL_STATUS_BITS)
                    != (Ctx.rflags.u16           & ~(ANDN_CHECK_EFLAGS | ANDN_IGNORE_EFLAGS) & X86_EFL_STATUS_BITS))
                    Bs3TestFailedF("Expected EFLAGS = %#06RX32, got %#06RX32 (immutable)",
                                   Ctx.rflags.u16           & ~(ANDN_CHECK_EFLAGS | ANDN_IGNORE_EFLAGS) & X86_EFL_STATUS_BITS,
                                   TrapFrame.Ctx.rflags.u16 & ~(ANDN_CHECK_EFLAGS | ANDN_IGNORE_EFLAGS) & X86_EFL_STATUS_BITS);

                if (TrapFrame.Ctx.rdx.u != Ctx.rdx.u)
                    Bs3TestFailedF("Expected RDX = %#06RX64, got %#06RX64 (src)", Ctx.rdx.u, TrapFrame.Ctx.rdx.u);
                if (TrapFrame.Ctx.rsp.u != Ctx.rsp.u)
                    Bs3TestFailedF("Expected RSP = %#06RX64, got %#06RX64", Ctx.rsp.u, TrapFrame.Ctx.rsp.u);
                if (TrapFrame.Ctx.rbp.u != Ctx.rbp.u)
                    Bs3TestFailedF("Expected RBP = %#06RX64, got %#06RX64", Ctx.rbp.u, TrapFrame.Ctx.rbp.u);
                if (TrapFrame.Ctx.rsi.u != Ctx.rsi.u)
                    Bs3TestFailedF("Expected RSI = %#06RX64, got %#06RX64", Ctx.rsi.u, TrapFrame.Ctx.rsi.u);
                if (TrapFrame.Ctx.rdi.u != Ctx.rdi.u)
                    Bs3TestFailedF("Expected RDI = %#06RX64, got %#06RX64", Ctx.rdi.u, TrapFrame.Ctx.rdi.u);
                if (uMemSrc2 != uMemSrc2Expect)
                    Bs3TestFailedF("Expected uMemSrc2 = %#06RX64, got %#06RX64", (uint64_t)uMemSrc2Expect, (uint64_t)uMemSrc2);
            }
        }
        Ctx.rflags.u16 &= ~X86_EFL_STATUS_BITS;
    }

    return 0;
}

/*
 * For testing BEXTR, SHLX SARX & SHRX.
 */
typedef struct BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T
{
    RTCCUINTXREG    uSrc1;
    RTCCUINTXREG    uSrc2;
    RTCCUINTXREG    uOut;
    uint16_t        fEflOut;
} BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T;

typedef struct BS3CPUINSTR2_TEST_Gy_Ey_By_T
{
    FPFNBS3FAR      pfnWorker;
    bool            fMemSrc;
    uint8_t         cbInstr;
    uint8_t         cSubTests;
    BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const *paSubTests;
} BS3CPUINSTR2_TEST_Gy_Ey_By_T;

static uint8_t bs3CpuInstr2_Common_Gy_Ey_By(uint8_t bMode, BS3CPUINSTR2_TEST_Gy_Ey_By_T const *paTests, unsigned cTests,
                                            uint32_t fStdExtFeatEbx, uint16_t fEflCheck, uint16_t fEflIgnore)
{
    BS3REGCTX       Ctx;
    BS3TRAPFRAME    TrapFrame;
    unsigned        i, j, k;
    uint32_t        uStdExtFeatEbx = 0;
    bool            fSupportsInstr;

    fEflCheck &= ~fEflIgnore;

    if (g_uBs3CpuDetected & BS3CPU_F_CPUID)
        ASMCpuIdExSlow(7, 0, 0, 0, NULL, &uStdExtFeatEbx, NULL, NULL);
    fSupportsInstr = RT_BOOL(uStdExtFeatEbx & fStdExtFeatEbx);

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);

    /*
     * Do the tests twice, first with all flags set, then once again with
     * flags cleared.  The flags are not supposed to be touched at all.
     */
    Ctx.rflags.u16 |= X86_EFL_STATUS_BITS;
    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < cTests; i++)
        {
            for (k = 0; k < paTests[i].cSubTests; k++)
            {
                bool const    fOkay       = !BS3_MODE_IS_RM_OR_V86(bMode) && fSupportsInstr;
                uint8_t const bExpectXcpt = fOkay ? X86_XCPT_DB : X86_XCPT_UD;
                uint64_t      uExpectRax, uExpectRip;
                RTCCUINTXREG  uMemSrc1, uMemSrc1Expect;

                Ctx.rax.uCcXReg = RTCCUINTXREG_MAX * 1019;
                Ctx.rcx.uCcXReg = paTests[i].paSubTests[k].uSrc2;
                if (!paTests[i].fMemSrc)
                {
                    Ctx.rbx.uCcXReg           = paTests[i].paSubTests[k].uSrc1;
                    uMemSrc1Expect = uMemSrc1 = ~paTests[i].paSubTests[k].uSrc1;
                }
                else
                {
                    uMemSrc1Expect = uMemSrc1 = paTests[i].paSubTests[k].uSrc1;
                    Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, &Ctx.fs, &uMemSrc1);
                }
                Bs3RegCtxSetRipCsFromCurPtr(&Ctx, paTests[i].pfnWorker);
                uExpectRax = fOkay ? paTests[i].paSubTests[k].uOut : Ctx.rax.u;
                uExpectRip = Ctx.rip.u + (fOkay ? paTests[i].cbInstr + 1 : 0);
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);

                if (   TrapFrame.bXcpt != bExpectXcpt
                    || TrapFrame.Ctx.rip.u != uExpectRip
                    || TrapFrame.Ctx.rcx.u != Ctx.rcx.u
                    || TrapFrame.Ctx.rbx.u != Ctx.rbx.u
                    || TrapFrame.Ctx.rax.u != uExpectRax
                    /* check that nothing else really changed: */
                    ||    (TrapFrame.Ctx.rflags.u16 & fEflCheck)
                       != ((fOkay ? paTests[i].paSubTests[k].fEflOut : Ctx.rflags.u16) & fEflCheck)
                    ||    (TrapFrame.Ctx.rflags.u16 & ~(fEflCheck | fEflIgnore) & X86_EFL_STATUS_BITS)
                       != (Ctx.rflags.u16           & ~(fEflCheck | fEflIgnore) & X86_EFL_STATUS_BITS)
                    || TrapFrame.Ctx.rdx.u != Ctx.rdx.u
                    || TrapFrame.Ctx.rsp.u != Ctx.rsp.u
                    || TrapFrame.Ctx.rbp.u != Ctx.rbp.u
                    || TrapFrame.Ctx.rsi.u != Ctx.rsi.u
                    || TrapFrame.Ctx.rdi.u != Ctx.rdi.u
                    || uMemSrc1 != uMemSrc1Expect
                   )
                {
                    Bs3TestFailedF("test #%i/%i failed: input %#" RTCCUINTXREG_XFMT ", %#" RTCCUINTXREG_XFMT,
                                   i, k, paTests[i].paSubTests[k].uSrc1, paTests[i].paSubTests[k].uSrc2);
                    if (TrapFrame.bXcpt != bExpectXcpt)
                        Bs3TestFailedF("Expected bXcpt = %#x, got %#x", bExpectXcpt, TrapFrame.bXcpt);
                    if (TrapFrame.Ctx.rip.u != uExpectRip)
                        Bs3TestFailedF("Expected RIP = %#06RX64, got %#06RX64", uExpectRip, TrapFrame.Ctx.rip.u);
                    if (TrapFrame.Ctx.rax.u != uExpectRax)
                        Bs3TestFailedF("Expected RAX = %#06RX64, got %#06RX64", uExpectRax, TrapFrame.Ctx.rax.u);
                    if (TrapFrame.Ctx.rcx.u != Ctx.rcx.u)
                        Bs3TestFailedF("Expected RCX = %#06RX64, got %#06RX64", Ctx.rcx.u, TrapFrame.Ctx.rcx.u);
                    if (TrapFrame.Ctx.rbx.u != Ctx.rbx.u)
                        Bs3TestFailedF("Expected RBX = %#06RX64, got %#06RX64", Ctx.rbx.u, TrapFrame.Ctx.rbx.u);
                    if (   (TrapFrame.Ctx.rflags.u16 & fEflCheck)
                        != ((fOkay ? paTests[i].paSubTests[k].fEflOut : Ctx.rflags.u16) & fEflCheck))
                        Bs3TestFailedF("Expected EFLAGS = %#06RX32, got %#06RX32 (output)",
                                       (fOkay ? paTests[i].paSubTests[k].fEflOut : Ctx.rflags.u16) & fEflCheck,
                                       TrapFrame.Ctx.rflags.u16 & fEflCheck);
                    if (   (TrapFrame.Ctx.rflags.u16 & ~(fEflCheck | fEflIgnore) & X86_EFL_STATUS_BITS)
                        != (Ctx.rflags.u16           & ~(fEflCheck | fEflIgnore) & X86_EFL_STATUS_BITS))
                        Bs3TestFailedF("Expected EFLAGS = %#06RX32, got %#06RX32 (immutable)",
                                       Ctx.rflags.u16           & ~(fEflCheck | fEflIgnore) & X86_EFL_STATUS_BITS,
                                       TrapFrame.Ctx.rflags.u16 & ~(fEflCheck | fEflIgnore) & X86_EFL_STATUS_BITS);

                    if (TrapFrame.Ctx.rdx.u != Ctx.rdx.u)
                        Bs3TestFailedF("Expected RDX = %#06RX64, got %#06RX64", Ctx.rdx.u, TrapFrame.Ctx.rdx.u);
                    if (TrapFrame.Ctx.rsp.u != Ctx.rsp.u)
                        Bs3TestFailedF("Expected RSP = %#06RX64, got %#06RX64", Ctx.rsp.u, TrapFrame.Ctx.rsp.u);
                    if (TrapFrame.Ctx.rbp.u != Ctx.rbp.u)
                        Bs3TestFailedF("Expected RBP = %#06RX64, got %#06RX64", Ctx.rbp.u, TrapFrame.Ctx.rbp.u);
                    if (TrapFrame.Ctx.rsi.u != Ctx.rsi.u)
                        Bs3TestFailedF("Expected RSI = %#06RX64, got %#06RX64", Ctx.rsi.u, TrapFrame.Ctx.rsi.u);
                    if (TrapFrame.Ctx.rdi.u != Ctx.rdi.u)
                        Bs3TestFailedF("Expected RDI = %#06RX64, got %#06RX64", Ctx.rdi.u, TrapFrame.Ctx.rdi.u);
                    if (uMemSrc1 != uMemSrc1Expect)
                        Bs3TestFailedF("Expected uMemSrc1 = %#06RX64, got %#06RX64", (uint64_t)uMemSrc1Expect, (uint64_t)uMemSrc1);
                }
            }
        }
        Ctx.rflags.u16 &= ~X86_EFL_STATUS_BITS;
    }

    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_bextr)(uint8_t bMode)
{
    /* 64 bits register width (32 bits in 32- and 16-bit modes): */
    static BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const s_aSubTests64[] =
    {
        {   0,                  RT_MAKE_U16(0, 0),   /* -> */ 0, X86_EFL_ZF },
        {   0,                  RT_MAKE_U16(16, 33), /* -> */ 0, X86_EFL_ZF },
        {   ~(RTCCUINTXREG)7,   RT_MAKE_U16(2, 4), /* -> */   0xe, 0},
        {   ~(RTCCUINTXREG)7,   RT_MAKE_U16(40, 8), /* -> */  ARCH_BITS == 64 ? 0xff : 0x00, ARCH_BITS == 64 ? 0 : X86_EFL_ZF },
    };

    /* 32-bit register width */
    static BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const s_aSubTests32[] =
    {
        {   0,                  RT_MAKE_U16(0, 0),   /* -> */ 0,        X86_EFL_ZF },
        {   0,                  RT_MAKE_U16(16, 18), /* -> */ 0,        X86_EFL_ZF },
        {   ~(RTCCUINTXREG)7,   RT_MAKE_U16(2, 4), /* -> */   0xe,      0 },
        {   ~(RTCCUINTXREG)7,   RT_MAKE_U16(24, 8), /* -> */  0xff,     0 },
        {   ~(RTCCUINTXREG)7,   RT_MAKE_U16(31, 9), /* -> */  1,        0 },
        {   ~(RTCCUINTXREG)7,   RT_MAKE_U16(42, 8), /* -> */  0,        X86_EFL_ZF },
    };

    static BS3CPUINSTR2_TEST_Gy_Ey_By_T const s_aTests[] =
    {
        { BS3_CMN_NM(bs3CpuInstr2_bextr_RAX_RBX_RCX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_bextr_RAX_FSxBX_RCX_icebp), true,  6, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_bextr_EAX_EBX_ECX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
        { BS3_CMN_NM(bs3CpuInstr2_bextr_EAX_FSxBX_ECX_icebp), true,  6, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
    };
    return bs3CpuInstr2_Common_Gy_Ey_By(bMode, s_aTests, RT_ELEMENTS(s_aTests), X86_CPUID_STEXT_FEATURE_EBX_BMI1,
                                        X86_EFL_STATUS_BITS, X86_EFL_AF | X86_EFL_SF | X86_EFL_PF);
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_bzhi)(uint8_t bMode)
{
    /* 64 bits register width (32 bits in 32- and 16-bit modes): */
    static BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const s_aSubTests64[] =
    {
        {   0,                  0,                      /* -> */ 0,                     X86_EFL_ZF },
        {   0,                  ~(RTCCUINTXREG)255,     /* -> */ 0,                     X86_EFL_ZF },
        {   0,                  64,                     /* -> */ 0,                     X86_EFL_ZF | X86_EFL_CF },
        {   ~(RTCCUINTXREG)0,   64,                     /* -> */ ~(RTCCUINTXREG)0,      X86_EFL_CF | X86_EFL_SF },
        {   ~(RTCCUINTXREG)0,   63,
        /* -> */ ARCH_BITS >= 64 ? ~(RTCCUINTXREG)0 >> 1 : ~(RTCCUINTXREG)0, ARCH_BITS >= 64 ? 0 : X86_EFL_CF | X86_EFL_SF },
        {   ~(RTCCUINTXREG)0 << 31 | UINT32_C(0x63849607), 24, /* -> */ UINT32_C(0x00849607), 0 },
        {   ~(RTCCUINTXREG)0 << 31 | UINT32_C(0x63849607), 33,
        /* -> */ ARCH_BITS >= 64 ? UINT64_C(0x1e3849607) : UINT32_C(0xe3849607), ARCH_BITS >= 64 ? 0 : X86_EFL_CF | X86_EFL_SF },
    };

    /* 32-bit register width */
    static BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const s_aSubTests32[] =
    {
        {   0,                  0,                      /* -> */ 0,                     X86_EFL_ZF },
        {   0,                  ~(RTCCUINTXREG)255,     /* -> */ 0,                     X86_EFL_ZF },
        {   0,                  32,                     /* -> */ 0,                     X86_EFL_ZF | X86_EFL_CF },
        {   ~(RTCCUINTXREG)0,   32,                     /* -> */ UINT32_MAX,            X86_EFL_CF | X86_EFL_SF },
        {   ~(RTCCUINTXREG)0,   31,                     /* -> */ UINT32_MAX >> 1,       0 },
        {   UINT32_C(0x1230fd34), 15,                   /* -> */ UINT32_C(0x00007d34),  0 },
    };

    static BS3CPUINSTR2_TEST_Gy_Ey_By_T const s_aTests[] =
    {
        { BS3_CMN_NM(bs3CpuInstr2_bzhi_RAX_RBX_RCX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_bzhi_RAX_FSxBX_RCX_icebp), true,  6, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_bzhi_EAX_EBX_ECX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
        { BS3_CMN_NM(bs3CpuInstr2_bzhi_EAX_FSxBX_ECX_icebp), true,  6, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
    };
    return bs3CpuInstr2_Common_Gy_Ey_By(bMode, s_aTests, RT_ELEMENTS(s_aTests), X86_CPUID_STEXT_FEATURE_EBX_BMI2,
                                        X86_EFL_STATUS_BITS, 0);
}


/** @note This is a Gy_By_Ey format instruction, so we're switching the two
 *        source registers around when calling bs3CpuInstr2_Common_Gy_Ey_By.
 *        Sorry for the confusion, but it saves some unnecessary code dup. */
BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_pdep)(uint8_t bMode)
{
    /* 64 bits register width (32 bits in 32- and 16-bit modes): */
    static BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const s_aSubTests64[] =
    {   /*  Mask (RBX/[FS:xBX]), source=RCX */
        {   0,                  0,                      /* -> */ 0,                     0 },
        {   0,                  ~(RTCCUINTXREG)0,       /* -> */ 0,                     0 },
        {   ~(RTCCUINTXREG)0,   0,                      /* -> */ 0,                     0 },
        {   ~(RTCCUINTXREG)0,   ~(RTCCUINTXREG)0,       /* -> */ ~(RTCCUINTXREG)0,      0 },
#if ARCH_BITS >= 64
        {   UINT64_C(0x3586049947589201), ~(RTCCUINTXREG)0,         /* -> */ UINT64_C(0x3586049947589201),  0 },
        {   UINT64_C(0x3586049947589201), ~(RTCCUINTXREG)7,         /* -> */ UINT64_C(0x3586049947588000),  0 },
#endif
        {           UINT32_C(0x47589201), ~(RTCCUINTXREG)0,         /* -> */         UINT32_C(0x47589201),  0 },
        {           UINT32_C(0x47589201), ~(RTCCUINTXREG)7,         /* -> */         UINT32_C(0x47588000),  0 },
    };

    /* 32-bit register width */
    static BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const s_aSubTests32[] =
    {   /*  Mask (EBX/[FS:xBX]), source=ECX */
        {   0,                  0,                      /* -> */ 0,                     0 },
        {   0,                  ~(RTCCUINTXREG)0,       /* -> */ 0,                     0 },
        {   ~(RTCCUINTXREG)0,   0,                      /* -> */ 0,                     0 },
        {   ~(RTCCUINTXREG)0,   ~(RTCCUINTXREG)0,       /* -> */ UINT32_MAX,            0 },
        {   UINT32_C(0x01010101), ~(RTCCUINTXREG)0,     /* -> */ UINT32_C(0x01010101),  0 },
        {   UINT32_C(0x01010101), ~(RTCCUINTXREG)3,     /* -> */ UINT32_C(0x01010000),  0 },
        {   UINT32_C(0x47589201), ~(RTCCUINTXREG)0,     /* -> */ UINT32_C(0x47589201),  0 },
    };

    static BS3CPUINSTR2_TEST_Gy_Ey_By_T const s_aTests[] =
    {
        { BS3_CMN_NM(bs3CpuInstr2_pdep_RAX_RCX_RBX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_pdep_RAX_RCX_FSxBX_icebp), true,  6, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_pdep_EAX_ECX_EBX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
        { BS3_CMN_NM(bs3CpuInstr2_pdep_EAX_ECX_FSxBX_icebp), true,  6, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
    };
    return bs3CpuInstr2_Common_Gy_Ey_By(bMode, s_aTests, RT_ELEMENTS(s_aTests), X86_CPUID_STEXT_FEATURE_EBX_BMI2, 0, 0);
}


/** @note Same note as for bs3CpuInstr2_pdep */
BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_pext)(uint8_t bMode)
{
    /* 64 bits register width (32 bits in 32- and 16-bit modes): */
    static BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const s_aSubTests64[] =
    {   /*  Mask (RBX/[FS:xBX]), source=RCX */
        {   0,                  0,                      /* -> */ 0,                     0 },
        {   0,                  ~(RTCCUINTXREG)0,       /* -> */ 0,                     0 },
        {   ~(RTCCUINTXREG)0,   0,                      /* -> */ 0,                     0 },
        {   ~(RTCCUINTXREG)0,   ~(RTCCUINTXREG)0,       /* -> */ ~(RTCCUINTXREG)0,      0 },
#if ARCH_BITS >= 64
        {   UINT64_C(0x3586049947589201), ~(RTCCUINTXREG)0,         /* -> */ UINT64_C(0x00000000007fffff),  0 },
        {   UINT64_C(0x3586049947589201), ~(RTCCUINTXREG)7,         /* -> */ UINT64_C(0x00000000007ffffe),  0 },
#endif
        {           UINT32_C(0x47589201), ~(RTCCUINTXREG)0,         /* -> */         UINT32_C(0x000007ff),  0 },
        {           UINT32_C(0x47589201), ~(RTCCUINTXREG)7,         /* -> */         UINT32_C(0x000007fe),  0 },
    };

    /* 32-bit register width */
    static BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const s_aSubTests32[] =
    {   /*  Mask (EBX/[FS:xBX]), source=ECX */
        {   0,                  0,                      /* -> */ 0,                     0 },
        {   0,                  ~(RTCCUINTXREG)0,       /* -> */ 0,                     0 },
        {   ~(RTCCUINTXREG)0,   0,                      /* -> */ 0,                     0 },
        {   ~(RTCCUINTXREG)0,   ~(RTCCUINTXREG)0,       /* -> */ UINT32_MAX,            0 },
        {   UINT32_C(0x01010101), ~(RTCCUINTXREG)0,     /* -> */ UINT32_C(0x0000000f),  0 },
        {   UINT32_C(0x01010101), ~(RTCCUINTXREG)3,     /* -> */ UINT32_C(0x0000000e),  0 },
        {   UINT32_C(0x47589201), ~(RTCCUINTXREG)0,     /* -> */ UINT32_C(0x000007ff),  0 },
        {   UINT32_C(0x47589201), ~(RTCCUINTXREG)7,     /* -> */ UINT32_C(0x000007fe),  0 },
    };

    static BS3CPUINSTR2_TEST_Gy_Ey_By_T const s_aTests[] =
    {
        { BS3_CMN_NM(bs3CpuInstr2_pext_RAX_RCX_RBX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_pext_RAX_RCX_FSxBX_icebp), true,  6, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_pext_EAX_ECX_EBX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
        { BS3_CMN_NM(bs3CpuInstr2_pext_EAX_ECX_FSxBX_icebp), true,  6, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
    };
    return bs3CpuInstr2_Common_Gy_Ey_By(bMode, s_aTests, RT_ELEMENTS(s_aTests), X86_CPUID_STEXT_FEATURE_EBX_BMI2, 0, 0);
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_shlx)(uint8_t bMode)
{
    /* 64 bits register width (32 bits in 32- and 16-bit modes): */
    static BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const s_aSubTests64[] =
    {
        {   0,                  0,                  /* -> */    0, 0 },
        {   0,                  ~(RTCCUINTXREG)3,   /* -> */    0, 0 },
        {   ~(RTCCUINTXREG)7,   8,                  /* -> */    ~(RTCCUINTXREG)0x7ff, 0},
        {   ~(RTCCUINTXREG)7,   40,                 /* -> */    ~(RTCCUINTXREG)7 << (ARCH_BITS == 64 ? 40 : 8), 0 },
        {   ~(RTCCUINTXREG)7,   72,                 /* -> */    ~(RTCCUINTXREG)7 << 8, 0 },
    };

    /* 32-bit register width */
    static BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const s_aSubTests32[] =
    {
        {   0,                  0,                  /* -> */    0, 0 },
        {   0,                  ~(RTCCUINTXREG)9,   /* -> */    0, 0 },
        {   ~(RTCCUINTXREG)7,   8,                  /* -> */    UINT32_C(0xfffff800), 0 },
        {   ~(RTCCUINTXREG)7,   8,                  /* -> */    UINT32_C(0xfffff800), 0 },
    };

    static BS3CPUINSTR2_TEST_Gy_Ey_By_T const s_aTests[] =
    {
        { BS3_CMN_NM(bs3CpuInstr2_shlx_RAX_RBX_RCX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_shlx_RAX_FSxBX_RCX_icebp), true,  6, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_shlx_EAX_EBX_ECX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
        { BS3_CMN_NM(bs3CpuInstr2_shlx_EAX_FSxBX_ECX_icebp), true,  6, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
    };
    return bs3CpuInstr2_Common_Gy_Ey_By(bMode, s_aTests, RT_ELEMENTS(s_aTests), X86_CPUID_STEXT_FEATURE_EBX_BMI1,
                                        0, 0);
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_sarx)(uint8_t bMode)
{
    /* 64 bits register width (32 bits in 32- and 16-bit modes): */
    static BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const s_aSubTests64[] =
    {
        {   0,                  0,                  /* -> */    0, 0 },
        {   0,                  ~(RTCCUINTXREG)3,   /* -> */    0, 0 },
        {   (RTCCUINTXREG)1 << (RTCCINTXREG_BITS - 1), RTCCINTXREG_BITS - 1,        /* -> */ ~(RTCCUINTXREG)0, 0 },
        {   (RTCCUINTXREG)1 << (RTCCINTXREG_BITS - 1), RTCCINTXREG_BITS - 1 + 64,   /* -> */ ~(RTCCUINTXREG)0, 0 },
        {   (RTCCUINTXREG)1 << (RTCCINTXREG_BITS - 2), RTCCINTXREG_BITS - 3,        /* -> */ 2, 0 },
        {   (RTCCUINTXREG)1 << (RTCCINTXREG_BITS - 2), RTCCINTXREG_BITS - 3 + 64,   /* -> */ 2, 0 },
    };

    /* 32-bit register width */
    static BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const s_aSubTests32[] =
    {
        {   0,                  0,                      /* -> */    0, 0 },
        {   0,                  ~(RTCCUINTXREG)9,       /* -> */    0, 0 },
        {   ~(RTCCUINTXREG)UINT32_C(0x7fffffff), 24,    /* -> */    UINT32_C(0xffffff80), 0 },
        {   ~(RTCCUINTXREG)UINT32_C(0x7fffffff), 24+32, /* -> */    UINT32_C(0xffffff80), 0 },
        {   ~(RTCCUINTXREG)UINT32_C(0xbfffffff), 24,    /* -> */    UINT32_C(0x40), 0 },
        {   ~(RTCCUINTXREG)UINT32_C(0xbfffffff), 24+32, /* -> */    UINT32_C(0x40), 0 },
    };

    static BS3CPUINSTR2_TEST_Gy_Ey_By_T const s_aTests[] =
    {
        { BS3_CMN_NM(bs3CpuInstr2_sarx_RAX_RBX_RCX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_sarx_RAX_FSxBX_RCX_icebp), true,  6, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_sarx_EAX_EBX_ECX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
        { BS3_CMN_NM(bs3CpuInstr2_sarx_EAX_FSxBX_ECX_icebp), true,  6, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
    };
    return bs3CpuInstr2_Common_Gy_Ey_By(bMode, s_aTests, RT_ELEMENTS(s_aTests), X86_CPUID_STEXT_FEATURE_EBX_BMI1,
                                        0, 0);
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_shrx)(uint8_t bMode)
{
    /* 64 bits register width (32 bits in 32- and 16-bit modes): */
    static BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const s_aSubTests64[] =
    {
        {   0,                  0,                  /* -> */    0, 0 },
        {   0,                  ~(RTCCUINTXREG)3,   /* -> */    0, 0 },
        {   (RTCCUINTXREG)1 << (RTCCINTXREG_BITS - 1), RTCCINTXREG_BITS - 1,        /* -> */ 1, 0 },
        {   (RTCCUINTXREG)1 << (RTCCINTXREG_BITS - 1), RTCCINTXREG_BITS - 1 + 64,   /* -> */ 1, 0 },
        {   (RTCCUINTXREG)1 << (RTCCINTXREG_BITS - 2), RTCCINTXREG_BITS - 3,        /* -> */ 2, 0 },
        {   (RTCCUINTXREG)1 << (RTCCINTXREG_BITS - 2), RTCCINTXREG_BITS - 3 + 64,   /* -> */ 2, 0 },
    };

    /* 32-bit register width */
    static BS3CPUINSTR2_SUBTEST_Gy_Ey_By_T const s_aSubTests32[] =
    {
        {   0,                  0,                      /* -> */    0, 0 },
        {   0,                  ~(RTCCUINTXREG)9,       /* -> */    0, 0 },
        {   ~(RTCCUINTXREG)UINT32_C(0x7fffffff), 24,    /* -> */    UINT32_C(0x80), 0 },
        {   ~(RTCCUINTXREG)UINT32_C(0x7fffffff), 24+32, /* -> */    UINT32_C(0x80), 0 },
        {   ~(RTCCUINTXREG)UINT32_C(0xbfffffff), 24,    /* -> */    UINT32_C(0x40), 0 },
        {   ~(RTCCUINTXREG)UINT32_C(0xbfffffff), 24+32, /* -> */    UINT32_C(0x40), 0 },
    };

    static BS3CPUINSTR2_TEST_Gy_Ey_By_T const s_aTests[] =
    {
        { BS3_CMN_NM(bs3CpuInstr2_shrx_RAX_RBX_RCX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_shrx_RAX_FSxBX_RCX_icebp), true,  6, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_shrx_EAX_EBX_ECX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
        { BS3_CMN_NM(bs3CpuInstr2_shrx_EAX_FSxBX_ECX_icebp), true,  6, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
    };
    return bs3CpuInstr2_Common_Gy_Ey_By(bMode, s_aTests, RT_ELEMENTS(s_aTests), X86_CPUID_STEXT_FEATURE_EBX_BMI1,
                                        0, 0);
}


/*
 * For testing BLSR, BLSMSK, and BLSI.
 */
typedef struct BS3CPUINSTR2_SUBTEST_By_Ey_T
{
    RTCCUINTXREG    uSrc;
    RTCCUINTXREG    uDst;
    uint16_t        fEflOut;
} BS3CPUINSTR2_SUBTEST_By_Ey_T;

typedef struct BS3CPUINSTR2_TEST_By_Ey_T
{
    FPFNBS3FAR      pfnWorker;
    bool            fMemSrc;
    uint8_t         cbInstr;
    uint8_t         cSubTests;
    BS3CPUINSTR2_SUBTEST_By_Ey_T const *paSubTests;
} BS3CPUINSTR2_TEST_By_Ey_T;

static uint8_t bs3CpuInstr2_Common_By_Ey(uint8_t bMode, BS3CPUINSTR2_TEST_By_Ey_T const *paTests, unsigned cTests,
                                         uint32_t fStdExtFeatEbx, uint16_t fEflCheck, uint16_t fEflIgnore)
{
    BS3REGCTX       Ctx;
    BS3TRAPFRAME    TrapFrame;
    unsigned        i, j, k;
    uint32_t        uStdExtFeatEbx = 0;
    bool            fSupportsInstr;

    fEflCheck &= ~fEflIgnore;

    if (g_uBs3CpuDetected & BS3CPU_F_CPUID)
        ASMCpuIdExSlow(7, 0, 0, 0, NULL, &uStdExtFeatEbx, NULL, NULL);
    fSupportsInstr = RT_BOOL(uStdExtFeatEbx & fStdExtFeatEbx);

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);

    /*
     * Do the tests twice, first with all flags set, then once again with
     * flags cleared.  The flags are not supposed to be touched at all.
     */
    Ctx.rflags.u16 |= X86_EFL_STATUS_BITS;
    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < cTests; i++)
        {
            for (k = 0; k < paTests[i].cSubTests; k++)
            {
                bool const    fOkay       = !BS3_MODE_IS_RM_OR_V86(bMode) && fSupportsInstr;
                uint8_t const bExpectXcpt = fOkay ? X86_XCPT_DB : X86_XCPT_UD;
                uint64_t      uExpectRax, uExpectRip;
                RTCCUINTXREG  uMemSrc, uMemSrcExpect;

                Ctx.rax.uCcXReg = ~paTests[i].paSubTests[k].uSrc ^ 0x593e7591;
                if (!paTests[i].fMemSrc)
                {
                    Ctx.rbx.uCcXReg         = paTests[i].paSubTests[k].uSrc;
                    uMemSrcExpect = uMemSrc = ~paTests[i].paSubTests[k].uSrc;
                }
                else
                {
                    uMemSrcExpect = uMemSrc = paTests[i].paSubTests[k].uSrc;
                    Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, &Ctx.fs, &uMemSrc);
                }
                Bs3RegCtxSetRipCsFromCurPtr(&Ctx, paTests[i].pfnWorker);
                uExpectRax = fOkay ? paTests[i].paSubTests[k].uDst : Ctx.rax.u;
                uExpectRip = Ctx.rip.u + (fOkay ? paTests[i].cbInstr + 1 : 0);
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);

                if (   TrapFrame.bXcpt != bExpectXcpt
                    || TrapFrame.Ctx.rip.u != uExpectRip
                    || TrapFrame.Ctx.rbx.u != Ctx.rbx.u
                    || TrapFrame.Ctx.rax.u != uExpectRax
                    /* check that nothing else really changed: */
                    ||    (TrapFrame.Ctx.rflags.u16 & fEflCheck)
                       != ((fOkay ? paTests[i].paSubTests[k].fEflOut : Ctx.rflags.u16) & fEflCheck)
                    ||    (TrapFrame.Ctx.rflags.u16 & ~(fEflCheck | fEflIgnore) & X86_EFL_STATUS_BITS)
                       != (Ctx.rflags.u16           & ~(fEflCheck | fEflIgnore) & X86_EFL_STATUS_BITS)
                    || TrapFrame.Ctx.rcx.u != Ctx.rcx.u
                    || TrapFrame.Ctx.rdx.u != Ctx.rdx.u
                    || TrapFrame.Ctx.rsp.u != Ctx.rsp.u
                    || TrapFrame.Ctx.rbp.u != Ctx.rbp.u
                    || TrapFrame.Ctx.rsi.u != Ctx.rsi.u
                    || TrapFrame.Ctx.rdi.u != Ctx.rdi.u
                    || uMemSrc != uMemSrcExpect
                   )
                {
                    Bs3TestFailedF("test #%i/%i failed: input %#" RTCCUINTXREG_XFMT,
                                   i, k, paTests[i].paSubTests[k].uSrc);
                    if (TrapFrame.bXcpt != bExpectXcpt)
                        Bs3TestFailedF("Expected bXcpt = %#x, got %#x", bExpectXcpt, TrapFrame.bXcpt);
                    if (TrapFrame.Ctx.rip.u != uExpectRip)
                        Bs3TestFailedF("Expected RIP = %#06RX64, got %#06RX64", uExpectRip, TrapFrame.Ctx.rip.u);
                    if (TrapFrame.Ctx.rax.u != uExpectRax)
                        Bs3TestFailedF("Expected RAX = %#06RX64, got %#06RX64", uExpectRax, TrapFrame.Ctx.rax.u);
                    if (TrapFrame.Ctx.rbx.u != Ctx.rbx.u)
                        Bs3TestFailedF("Expected RBX = %#06RX64, got %#06RX64 (dst)", Ctx.rbx.u, TrapFrame.Ctx.rbx.u);
                    if (   (TrapFrame.Ctx.rflags.u16 & fEflCheck)
                        != ((fOkay ? paTests[i].paSubTests[k].fEflOut : Ctx.rflags.u16) & fEflCheck))
                        Bs3TestFailedF("Expected EFLAGS = %#06RX32, got %#06RX32 (output)",
                                       (fOkay ? paTests[i].paSubTests[k].fEflOut : Ctx.rflags.u16) & fEflCheck,
                                       TrapFrame.Ctx.rflags.u16 & fEflCheck);
                    if (   (TrapFrame.Ctx.rflags.u16 & ~(fEflCheck | fEflIgnore) & X86_EFL_STATUS_BITS)
                        != (Ctx.rflags.u16           & ~(fEflCheck | fEflIgnore) & X86_EFL_STATUS_BITS))
                        Bs3TestFailedF("Expected EFLAGS = %#06RX32, got %#06RX32 (immutable)",
                                       Ctx.rflags.u16           & ~(fEflCheck | fEflIgnore) & X86_EFL_STATUS_BITS,
                                       TrapFrame.Ctx.rflags.u16 & ~(fEflCheck | fEflIgnore) & X86_EFL_STATUS_BITS);

                    if (TrapFrame.Ctx.rcx.u != Ctx.rcx.u)
                        Bs3TestFailedF("Expected RCX = %#06RX64, got %#06RX64", Ctx.rcx.u, TrapFrame.Ctx.rcx.u);
                    if (TrapFrame.Ctx.rdx.u != Ctx.rdx.u)
                        Bs3TestFailedF("Expected RDX = %#06RX64, got %#06RX64", Ctx.rdx.u, TrapFrame.Ctx.rdx.u);
                    if (TrapFrame.Ctx.rsp.u != Ctx.rsp.u)
                        Bs3TestFailedF("Expected RSP = %#06RX64, got %#06RX64", Ctx.rsp.u, TrapFrame.Ctx.rsp.u);
                    if (TrapFrame.Ctx.rbp.u != Ctx.rbp.u)
                        Bs3TestFailedF("Expected RBP = %#06RX64, got %#06RX64", Ctx.rbp.u, TrapFrame.Ctx.rbp.u);
                    if (TrapFrame.Ctx.rsi.u != Ctx.rsi.u)
                        Bs3TestFailedF("Expected RSI = %#06RX64, got %#06RX64", Ctx.rsi.u, TrapFrame.Ctx.rsi.u);
                    if (TrapFrame.Ctx.rdi.u != Ctx.rdi.u)
                        Bs3TestFailedF("Expected RDI = %#06RX64, got %#06RX64", Ctx.rdi.u, TrapFrame.Ctx.rdi.u);
                    if (uMemSrc != uMemSrcExpect)
                        Bs3TestFailedF("Expected uMemSrc = %#06RX64, got %#06RX64", (uint64_t)uMemSrcExpect, (uint64_t)uMemSrc);
                }
            }
        }
        Ctx.rflags.u16 &= ~X86_EFL_STATUS_BITS;
    }

    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_blsr)(uint8_t bMode)
{
    /* 64 bits register width (32 bits in 32- and 16-bit modes): */
    static BS3CPUINSTR2_SUBTEST_By_Ey_T const s_aSubTests64[] =
    {
        {   0,                      /* -> */ 0,                     X86_EFL_ZF | X86_EFL_CF },
        {   1,                      /* -> */ 0,                     X86_EFL_ZF },
        {   2,                      /* -> */ 0,                     X86_EFL_ZF },
        {   3,                      /* -> */ 2,                     0 },
        {   5,                      /* -> */ 4,                     0 },
        {   6,                      /* -> */ 4,                     0 },
        {   7,                      /* -> */ 6,                     0 },
        {   9,                      /* -> */ 8,                     0 },
        {   10,                     /* -> */ 8,                     0 },
        {   ~(RTCCUINTXREG)1,       /* -> */ ~(RTCCUINTXREG)3,      X86_EFL_SF },
        {   (RTCCUINTXREG)3 << (RTCCINTXREG_BITS - 2), /* -> */ (RTCCUINTXREG)2 << (RTCCINTXREG_BITS - 2), X86_EFL_SF },
    };

    /* 32-bit register width */
    static BS3CPUINSTR2_SUBTEST_By_Ey_T const s_aSubTests32[] =
    {
        {   0,                      /* -> */ 0,                     X86_EFL_ZF | X86_EFL_CF },
        {   1,                      /* -> */ 0,                     X86_EFL_ZF },
        {   ~(RTCCUINTXREG)1,       /* -> */ UINT32_C(0xfffffffc),  X86_EFL_SF },
        {   ~(RTCCUINTXREG)0 << 30, /* -> */ UINT32_C(0x80000000),  X86_EFL_SF },
    };

    static BS3CPUINSTR2_TEST_By_Ey_T const s_aTests[] =
    {
        { BS3_CMN_NM(bs3CpuInstr2_blsr_RAX_RBX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_blsr_RAX_FSxBX_icebp), true,  6, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_blsr_EAX_EBX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
        { BS3_CMN_NM(bs3CpuInstr2_blsr_EAX_FSxBX_icebp), true,  6, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
    };
    return bs3CpuInstr2_Common_By_Ey(bMode, s_aTests, RT_ELEMENTS(s_aTests), X86_CPUID_STEXT_FEATURE_EBX_BMI1,
                                     X86_EFL_STATUS_BITS, 0);
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_blsmsk)(uint8_t bMode)
{
    /* 64 bits register width (32 bits in 32- and 16-bit modes): */
    static BS3CPUINSTR2_SUBTEST_By_Ey_T const s_aSubTests64[] =
    {
        {   0,                      /* -> */ ~(RTCCUINTXREG)0,      X86_EFL_CF | X86_EFL_SF },
        {   1,                      /* -> */ 1,                     0 },
        {   ~(RTCCUINTXREG)1,       /* -> */ 3,                     0 },
        {   (RTCCUINTXREG)3 << (RTCCINTXREG_BITS - 2), /* -> */ ~((RTCCUINTXREG)2 << (RTCCINTXREG_BITS - 2)), 0 },
    };

    /* 32-bit register width */
    static BS3CPUINSTR2_SUBTEST_By_Ey_T const s_aSubTests32[] =
    {
        {   0,                      /* -> */ UINT32_MAX,            X86_EFL_CF | X86_EFL_SF },
        {   1,                      /* -> */ 1,                     0 },
        {   ~(RTCCUINTXREG)1,       /* -> */ 3,                     0 },
        {   ~(RTCCUINTXREG)0 << 30, /* -> */ UINT32_C(0x7fffffff),  0},
    };

    static BS3CPUINSTR2_TEST_By_Ey_T const s_aTests[] =
    {
        { BS3_CMN_NM(bs3CpuInstr2_blsmsk_RAX_RBX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_blsmsk_RAX_FSxBX_icebp), true,  6, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_blsmsk_EAX_EBX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
        { BS3_CMN_NM(bs3CpuInstr2_blsmsk_EAX_FSxBX_icebp), true,  6, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
    };
    return bs3CpuInstr2_Common_By_Ey(bMode, s_aTests, RT_ELEMENTS(s_aTests), X86_CPUID_STEXT_FEATURE_EBX_BMI1,
                                     X86_EFL_STATUS_BITS, 0);
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_blsi)(uint8_t bMode)
{
    /* 64 bits register width (32 bits in 32- and 16-bit modes): */
    static BS3CPUINSTR2_SUBTEST_By_Ey_T const s_aSubTests64[] =
    {
        {   0,                      /* -> */ 0,                     X86_EFL_ZF },
        {   1,                      /* -> */ 1,                     X86_EFL_CF },
        {   ~(RTCCUINTXREG)1,       /* -> */ 2,                     X86_EFL_CF },
        {   (RTCCUINTXREG)3 << (RTCCINTXREG_BITS - 2), /* -> */ (RTCCUINTXREG)1 << (RTCCINTXREG_BITS - 2), X86_EFL_CF },
    };

    /* 32-bit register width */
    static BS3CPUINSTR2_SUBTEST_By_Ey_T const s_aSubTests32[] =
    {
        {   0,                      /* -> */ 0,                     X86_EFL_ZF },
        {   1,                      /* -> */ 1,                     X86_EFL_CF },
        {   ~(RTCCUINTXREG)1,       /* -> */ 2,                     X86_EFL_CF },
        {   ~(RTCCUINTXREG)0 << 30, /* -> */ UINT32_C(0x40000000),  X86_EFL_CF },
    };

    static BS3CPUINSTR2_TEST_By_Ey_T const s_aTests[] =
    {
        { BS3_CMN_NM(bs3CpuInstr2_blsi_RAX_RBX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_blsi_RAX_FSxBX_icebp), true,  6, RT_ELEMENTS(s_aSubTests64), s_aSubTests64 },
        { BS3_CMN_NM(bs3CpuInstr2_blsi_EAX_EBX_icebp),   false, 5, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
        { BS3_CMN_NM(bs3CpuInstr2_blsi_EAX_FSxBX_icebp), true,  6, RT_ELEMENTS(s_aSubTests32), s_aSubTests32 },
    };
    return bs3CpuInstr2_Common_By_Ey(bMode, s_aTests, RT_ELEMENTS(s_aTests), X86_CPUID_STEXT_FEATURE_EBX_BMI1,
                                     X86_EFL_STATUS_BITS, 0);
}


/*
 * MULX (BMI2) - destination registers (/r & vvvv) = r/m * rDX
 */
BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_mulx)(uint8_t bMode)
{
    static const struct
    {
        FPFNBS3FAR      pfnWorker;
        bool            fMemSrc;
        bool            fSameDst;
        uint8_t         cbInstr;
        RTCCUINTXREG    uSrc1;
        RTCCUINTXREG    uSrc2;
        RTCCUINTXREG    uDst1;
        RTCCUINTXREG    uDst2;
    } s_aTests[] =
    {
        /* 64 bits register width (32 bits in 32- and 16-bit modes): */
        {   BS3_CMN_NM(bs3CpuInstr2_mulx_RAX_RCX_RBX_RDX_icebp),    false,  false,  5,                  // #0
            0,                      0,                      /* -> */ 0,                     0 },
        {   BS3_CMN_NM(bs3CpuInstr2_mulx_RAX_RCX_RBX_RDX_icebp),    false,  false,  5,                  // #1
            ~(RTCCUINTXREG)0,       ~(RTCCUINTXREG)0,       /* -> */ ~(RTCCUINTXREG)1,      1 },
        {   BS3_CMN_NM(bs3CpuInstr2_mulx_RCX_RCX_RBX_RDX_icebp),    false,  true,   5,                  // #2
            ~(RTCCUINTXREG)0,       ~(RTCCUINTXREG)0,       /* -> */ ~(RTCCUINTXREG)1,      ~(RTCCUINTXREG)1 },
        {   BS3_CMN_NM(bs3CpuInstr2_mulx_RAX_RCX_RBX_RDX_icebp),    false,  false,  5,                  // #3
            2,                      2,                      /* -> */ 0,                     4 },
        {   BS3_CMN_NM(bs3CpuInstr2_mulx_RAX_RCX_RBX_RDX_icebp),    false,  false,  5,                  // #4
            ~(RTCCUINTXREG)0,       42,                     /* -> */ 0x29,                  ~(RTCCUINTXREG)41 },

        {   BS3_CMN_NM(bs3CpuInstr2_mulx_RAX_RCX_FSxBX_RDX_icebp),  true,   false,  6,                  // #5
            0,                      0,                      /* -> */ 0,                     0 },
        {   BS3_CMN_NM(bs3CpuInstr2_mulx_RAX_RCX_FSxBX_RDX_icebp),  true,   false,  6,                  // #6
            ~(RTCCUINTXREG)0,       ~(RTCCUINTXREG)0,       /* -> */ ~(RTCCUINTXREG)1,      1 },
        {   BS3_CMN_NM(bs3CpuInstr2_mulx_RAX_RCX_FSxBX_RDX_icebp),  true,   false,  6,                  // #7
            ~(RTCCUINTXREG)0,       42,                     /* -> */ 0x29,                  ~(RTCCUINTXREG)41 },

        /* 32-bit register width */
        {   BS3_CMN_NM(bs3CpuInstr2_mulx_EAX_ECX_EBX_EDX_icebp),    false,  false,  5,                  // #8
            0,                      0,                      /* -> */ 0,                     0 },
        {   BS3_CMN_NM(bs3CpuInstr2_mulx_EAX_ECX_EBX_EDX_icebp),    false,  false,  5,                  // #9
            ~(RTCCUINTXREG)0,       ~(RTCCUINTXREG)0,       /* -> */ ~(uint32_t)1,          1 },
        {   BS3_CMN_NM(bs3CpuInstr2_mulx_ECX_ECX_EBX_EDX_icebp),    false,  true,   5,                  // #10
            ~(RTCCUINTXREG)0,       ~(RTCCUINTXREG)0,       /* -> */ ~(uint32_t)1,          ~(uint32_t)1 },
        {   BS3_CMN_NM(bs3CpuInstr2_mulx_EAX_ECX_EBX_EDX_icebp),    false,  false,  5,                  // #11
            2,                      2,                      /* -> */ 0,                     4 },
        {   BS3_CMN_NM(bs3CpuInstr2_mulx_EAX_ECX_EBX_EDX_icebp),    false,  false,  5,                  // #12
            ~(RTCCUINTXREG)0,       42,                     /* -> */ 0x29,                  ~(uint32_t)41 },

        {   BS3_CMN_NM(bs3CpuInstr2_mulx_EAX_ECX_FSxBX_EDX_icebp),  true,   false,  6,                  // #13
            0,                      0,                      /* -> */ 0,                     0 },
        {   BS3_CMN_NM(bs3CpuInstr2_mulx_EAX_ECX_FSxBX_EDX_icebp),  true,   false,  6,                  // #14
            ~(RTCCUINTXREG)0,       ~(RTCCUINTXREG)0,       /* -> */ ~(uint32_t)1,          1 },
        {   BS3_CMN_NM(bs3CpuInstr2_mulx_EAX_ECX_FSxBX_EDX_icebp),  true,   false,  6,                  // #15
            ~(RTCCUINTXREG)0,       42,                     /* -> */ 0x29,                  ~(uint32_t)41 },
    };

    BS3REGCTX       Ctx;
    BS3TRAPFRAME    TrapFrame;
    unsigned        i, j;
    uint32_t        uStdExtFeatEbx = 0;
    bool            fSupportsAndN;

    if (g_uBs3CpuDetected & BS3CPU_F_CPUID)
        ASMCpuIdExSlow(7, 0, 0, 0, NULL, &uStdExtFeatEbx, NULL, NULL);
    fSupportsAndN = RT_BOOL(uStdExtFeatEbx & X86_CPUID_STEXT_FEATURE_EBX_BMI2);

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);

    /*
     * Do the tests twice, first with all flags set, then once again with
     * flags cleared.  The flags are not supposed to be touched at all.
     */
    Ctx.rflags.u16 |= X86_EFL_STATUS_BITS;
    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            bool const    fOkay       = !BS3_MODE_IS_RM_OR_V86(bMode) && fSupportsAndN;
            uint8_t const bExpectXcpt = fOkay ? X86_XCPT_DB : X86_XCPT_UD;
            uint64_t      uExpectRax, uExpectRcx, uExpectRip;
            RTCCUINTXREG  uMemSrc1, uMemSrc1Expect;

            Ctx.rax.uCcXReg = RTCCUINTXREG_MAX * 1019;
            Ctx.rcx.uCcXReg = RTCCUINTXREG_MAX * 4095;
            Ctx.rdx.uCcXReg = s_aTests[i].uSrc2;
            if (!s_aTests[i].fMemSrc)
            {
                Ctx.rbx.uCcXReg           = s_aTests[i].uSrc1;
                uMemSrc1Expect = uMemSrc1 = ~s_aTests[i].uSrc1;
            }
            else
            {
                uMemSrc1Expect = uMemSrc1 = s_aTests[i].uSrc1;
                Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, &Ctx.fs, &uMemSrc1);
            }
            Bs3RegCtxSetRipCsFromCurPtr(&Ctx, s_aTests[i].pfnWorker);
            uExpectRax = fOkay && !s_aTests[i].fSameDst ? s_aTests[i].uDst1 : Ctx.rax.u;
            uExpectRcx = fOkay                          ? s_aTests[i].uDst2 : Ctx.rcx.u;
            uExpectRip = Ctx.rip.u + (fOkay ? s_aTests[i].cbInstr + 1 : 0);
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);

            if (   TrapFrame.bXcpt != bExpectXcpt
                || TrapFrame.Ctx.rip.u != uExpectRip
                || TrapFrame.Ctx.rbx.u != Ctx.rbx.u
                || TrapFrame.Ctx.rdx.u != Ctx.rdx.u
                || TrapFrame.Ctx.rax.u != uExpectRax
                || TrapFrame.Ctx.rcx.u != uExpectRcx
                /* check that nothing else really changed: */
                || (TrapFrame.Ctx.rflags.u16 & X86_EFL_STATUS_BITS) != (Ctx.rflags.u16 & X86_EFL_STATUS_BITS)
                || TrapFrame.Ctx.rsp.u != Ctx.rsp.u
                || TrapFrame.Ctx.rbp.u != Ctx.rbp.u
                || TrapFrame.Ctx.rsi.u != Ctx.rsi.u
                || TrapFrame.Ctx.rdi.u != Ctx.rdi.u
                || uMemSrc1 != uMemSrc1Expect
               )
            {
                Bs3TestFailedF("test #%i failed: input %#" RTCCUINTXREG_XFMT ", %#" RTCCUINTXREG_XFMT, i, s_aTests[i].uSrc1, s_aTests[i].uSrc2);
                if (TrapFrame.bXcpt != bExpectXcpt)
                    Bs3TestFailedF("Expected bXcpt = %#x, got %#x", bExpectXcpt, TrapFrame.bXcpt);
                if (TrapFrame.Ctx.rip.u != uExpectRip)
                    Bs3TestFailedF("Expected RIP = %#06RX64, got %#06RX64", uExpectRip, TrapFrame.Ctx.rip.u);
                if (TrapFrame.Ctx.rax.u != uExpectRax)
                    Bs3TestFailedF("Expected RAX = %#06RX64, got %#06RX64", uExpectRax, TrapFrame.Ctx.rax.u);
                if (TrapFrame.Ctx.rcx.u != uExpectRcx)
                    Bs3TestFailedF("Expected RCX = %#06RX64, got %#06RX64", uExpectRcx, TrapFrame.Ctx.rcx.u);
                if (TrapFrame.Ctx.rbx.u != Ctx.rbx.u)
                    Bs3TestFailedF("Expected RBX = %#06RX64, got %#06RX64 (dst)", Ctx.rbx.u, TrapFrame.Ctx.rbx.u);
                if (TrapFrame.Ctx.rdx.u != Ctx.rdx.u)
                    Bs3TestFailedF("Expected RDX = %#06RX64, got %#06RX64 (src)", Ctx.rdx.u, TrapFrame.Ctx.rdx.u);

                if (   (TrapFrame.Ctx.rflags.u16 & X86_EFL_STATUS_BITS) != (Ctx.rflags.u16 & X86_EFL_STATUS_BITS))
                    Bs3TestFailedF("Expected EFLAGS = %#06RX32, got %#06RX32 (immutable)",
                                   Ctx.rflags.u16 & X86_EFL_STATUS_BITS, TrapFrame.Ctx.rflags.u16 & X86_EFL_STATUS_BITS);
                if (TrapFrame.Ctx.rsp.u != Ctx.rsp.u)
                    Bs3TestFailedF("Expected RSP = %#06RX64, got %#06RX64", Ctx.rsp.u, TrapFrame.Ctx.rsp.u);
                if (TrapFrame.Ctx.rbp.u != Ctx.rbp.u)
                    Bs3TestFailedF("Expected RBP = %#06RX64, got %#06RX64", Ctx.rbp.u, TrapFrame.Ctx.rbp.u);
                if (TrapFrame.Ctx.rsi.u != Ctx.rsi.u)
                    Bs3TestFailedF("Expected RSI = %#06RX64, got %#06RX64", Ctx.rsi.u, TrapFrame.Ctx.rsi.u);
                if (TrapFrame.Ctx.rdi.u != Ctx.rdi.u)
                    Bs3TestFailedF("Expected RDI = %#06RX64, got %#06RX64", Ctx.rdi.u, TrapFrame.Ctx.rdi.u);
                if (uMemSrc1 != uMemSrc1Expect)
                    Bs3TestFailedF("Expected uMemSrc1 = %#06RX64, got %#06RX64", (uint64_t)uMemSrc1Expect, (uint64_t)uMemSrc1);
            }
        }
        Ctx.rflags.u16 &= ~X86_EFL_STATUS_BITS;
    }

    return 0;
}


/*
 * POPCNT - Intel: POPCNT; AMD: ABM.
 */
BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_popcnt)(uint8_t bMode)
{
    static const struct
    {
        FPFNBS3FAR      pfnWorker;
        bool            fMemSrc;
        uint8_t         cWidth;
        uint8_t         cbInstr;
        RTCCUINTXREG    uSrc;
        RTCCUINTXREG    uDst;
        uint16_t        fEFlags;
    } s_aTests[] =
    {
        /* 16-bit register width */
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_AX_BX_icebp),      false,  16, 4 + (ARCH_BITS != 16),    // #0
            0,                      /* -> */ 0,     X86_EFL_ZF },
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_AX_BX_icebp),      false,  16, 4 + (ARCH_BITS != 16),    // #1
            ~(RTCCUINTXREG)0,       /* -> */ 16,    0 },
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_AX_BX_icebp),      false,  16, 4 + (ARCH_BITS != 16),    // #2
            UINT16_C(0xffff),       /* -> */ 16,    0 },
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_AX_BX_icebp),      false,  16, 4 + (ARCH_BITS != 16),    // #3
            UINT16_C(0x0304),       /* -> */ 3,     0 },
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_AX_FSxBX_icebp),   true,   16, 5 + (ARCH_BITS != 16),    // #4
            UINT16_C(0xd569),       /* -> */ 9,     0},
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_AX_FSxBX_icebp),   true,   16, 5 + (ARCH_BITS != 16),    // #5
            0,                      /* -> */ 0,     X86_EFL_ZF },

        /* 32-bit register width */
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_EAX_EBX_icebp),    false,  32, 4 + (ARCH_BITS == 16),    // #6
            0,                      /* -> */ 0,     X86_EFL_ZF },
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_EAX_EBX_icebp),    false,  32, 4 + (ARCH_BITS == 16),    // #7
            ~(RTCCUINTXREG)0,       /* -> */ 32,    0},
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_EAX_EBX_icebp),    false,  32, 4 + (ARCH_BITS == 16),    // #8
            UINT32_C(0x01020304),   /* -> */ 5,     0},
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_EAX_FSxBX_icebp),  true,   32, 5 + (ARCH_BITS == 16),    // #9
            0,                      /* -> */ 0,     X86_EFL_ZF },
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_EAX_FSxBX_icebp),  true,   32, 5 + (ARCH_BITS == 16),    // #10
            UINT32_C(0x49760948),   /* -> */ 12,     0 },

#if ARCH_BITS == 64
        /* 64-bit register width */
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_RAX_RBX_icebp),    false,  64, 5,                        // #11
            0,                              /* -> */ 0,     X86_EFL_ZF },
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_RAX_RBX_icebp),    false,  64, 5,                        // #12
            ~(RTCCUINTXREG)0,               /* -> */ 64,    0 },
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_RAX_RBX_icebp),    false,  64, 5,                        // #13
            UINT64_C(0x1234123412341234),   /* -> */ 5*4,   0 },
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_RAX_FSxBX_icebp),  true,   64, 6,                        // #14
            0,                              /* -> */ 0,     X86_EFL_ZF },
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_RAX_FSxBX_icebp),  true,   64, 6,                        // #15
            ~(RTCCUINTXREG)0,               /* -> */ 64,    0 },
        {   BS3_CMN_NM(bs3CpuInstr2_popcnt_RAX_FSxBX_icebp),  true,   64, 6,                        // #16
            UINT64_C(0x5908760293769087),   /* -> */ 26,    0 },
#endif
    };

    BS3REGCTX       Ctx;
    BS3TRAPFRAME    TrapFrame;
    unsigned        i, j;
    bool const      fSupportsPopCnt = (g_uBs3CpuDetected & BS3CPU_F_CPUID)
                                   && (ASMCpuId_ECX(1) & X86_CPUID_FEATURE_ECX_POPCNT);

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);

    /*
     * Do the tests twice, first with all flags set, then once again with
     * flags cleared.  The flags are not supposed to be touched at all.
     */
    Ctx.rflags.u16 |= X86_EFL_STATUS_BITS;
    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            bool const    fOkay       = fSupportsPopCnt;
            uint8_t const bExpectXcpt = fOkay ? X86_XCPT_DB : X86_XCPT_UD;
            uint64_t      uExpectRax, uExpectRip;
            RTCCUINTXREG  uMemSrc, uMemSrcExpect;

            Ctx.rax.uCcXReg = RTCCUINTXREG_MAX * 1019;
            if (!s_aTests[i].fMemSrc)
            {
                Ctx.rbx.uCcXReg          = s_aTests[i].uSrc;
                uMemSrcExpect = uMemSrc = ~s_aTests[i].uSrc;
            }
            else
            {
                uMemSrcExpect = uMemSrc = s_aTests[i].uSrc;
                Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, &Ctx.fs, &uMemSrc);
            }
            Bs3RegCtxSetRipCsFromCurPtr(&Ctx, s_aTests[i].pfnWorker);
            uExpectRax = fOkay ? s_aTests[i].uDst : Ctx.rax.u;
            if (s_aTests[i].cWidth == 16)
                uExpectRax = (uExpectRax & UINT16_MAX) | (Ctx.rax.u & ~(uint64_t)UINT16_MAX);

            uExpectRip = Ctx.rip.u + (fOkay ? s_aTests[i].cbInstr + 1 : 0);
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);

            if (   TrapFrame.bXcpt != bExpectXcpt
                || TrapFrame.Ctx.rip.u != uExpectRip
                || TrapFrame.Ctx.rbx.u != Ctx.rbx.u
                || TrapFrame.Ctx.rax.u != uExpectRax
                || (TrapFrame.Ctx.rflags.u16 & X86_EFL_STATUS_BITS) != (fOkay ? s_aTests[i].fEFlags : Ctx.rflags.u16)
                /* check that nothing else really changed: */
                || TrapFrame.Ctx.rcx.u != Ctx.rcx.u
                || TrapFrame.Ctx.rdx.u != Ctx.rdx.u
                || TrapFrame.Ctx.rsp.u != Ctx.rsp.u
                || TrapFrame.Ctx.rbp.u != Ctx.rbp.u
                || TrapFrame.Ctx.rsi.u != Ctx.rsi.u
                || TrapFrame.Ctx.rdi.u != Ctx.rdi.u
                || uMemSrc != uMemSrcExpect
               )
            {
                Bs3TestFailedF("test #%i failed: input %#" RTCCUINTXREG_XFMT, i, s_aTests[i].uSrc);
                if (TrapFrame.bXcpt != bExpectXcpt)
                    Bs3TestFailedF("Expected bXcpt = %#x, got %#x", bExpectXcpt, TrapFrame.bXcpt);
                if (TrapFrame.Ctx.rip.u != uExpectRip)
                    Bs3TestFailedF("Expected RIP = %#06RX64, got %#06RX64", uExpectRip, TrapFrame.Ctx.rip.u);
                if (TrapFrame.Ctx.rax.u != uExpectRax)
                    Bs3TestFailedF("Expected RAX = %#06RX64, got %#06RX64", uExpectRax, TrapFrame.Ctx.rax.u);
                if (TrapFrame.Ctx.rbx.u != Ctx.rbx.u)
                    Bs3TestFailedF("Expected RBX = %#06RX64, got %#06RX64 (dst)", Ctx.rbx.u, TrapFrame.Ctx.rbx.u);
                if ((TrapFrame.Ctx.rflags.u16 & X86_EFL_STATUS_BITS) != (fOkay ? s_aTests[i].fEFlags : Ctx.rflags.u16))
                    Bs3TestFailedF("Expected EFLAGS = %#06RX32, got %#06RX32",
                                   fOkay ? s_aTests[i].fEFlags : Ctx.rflags.u16, TrapFrame.Ctx.rflags.u16 & X86_EFL_STATUS_BITS);

                if (TrapFrame.Ctx.rcx.u != Ctx.rcx.u)
                    Bs3TestFailedF("Expected RCX = %#06RX64, got %#06RX64", Ctx.rcx.u, TrapFrame.Ctx.rcx.u);
                if (TrapFrame.Ctx.rdx.u != Ctx.rdx.u)
                    Bs3TestFailedF("Expected RDX = %#06RX64, got %#06RX64 (src)", Ctx.rdx.u, TrapFrame.Ctx.rdx.u);
                if (TrapFrame.Ctx.rsp.u != Ctx.rsp.u)
                    Bs3TestFailedF("Expected RSP = %#06RX64, got %#06RX64", Ctx.rsp.u, TrapFrame.Ctx.rsp.u);
                if (TrapFrame.Ctx.rbp.u != Ctx.rbp.u)
                    Bs3TestFailedF("Expected RBP = %#06RX64, got %#06RX64", Ctx.rbp.u, TrapFrame.Ctx.rbp.u);
                if (TrapFrame.Ctx.rsi.u != Ctx.rsi.u)
                    Bs3TestFailedF("Expected RSI = %#06RX64, got %#06RX64", Ctx.rsi.u, TrapFrame.Ctx.rsi.u);
                if (TrapFrame.Ctx.rdi.u != Ctx.rdi.u)
                    Bs3TestFailedF("Expected RDI = %#06RX64, got %#06RX64", Ctx.rdi.u, TrapFrame.Ctx.rdi.u);
                if (uMemSrc != uMemSrcExpect)
                    Bs3TestFailedF("Expected uMemSrc = %#06RX64, got %#06RX64", (uint64_t)uMemSrcExpect, (uint64_t)uMemSrc);
            }
        }
        Ctx.rflags.u16 &= ~X86_EFL_STATUS_BITS;
    }

    return 0;
}

/*
 * CRC32 - SSE4.2
 */
BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_crc32)(uint8_t bMode)
{
    typedef struct BS3CPUINSTR2_CRC32_VALUES_T
    {
        uint32_t uDstIn;
        uint32_t uDstOut;
        uint64_t uSrc;
    } BS3CPUINSTR2_CRC32_VALUES_T;
    static const BS3CPUINSTR2_CRC32_VALUES_T s_aValues1[] =
    {
        { UINT32_C(0000000000), UINT32_C(0000000000), UINT8_C(0000) },
        { UINT32_C(0xffffffff), UINT32_C(0x25502c8c), UINT8_C(0xea) },
        { UINT32_C(0x25502c8c), UINT32_C(0x474224a6), UINT8_C(0xea) },
        { UINT32_C(0x474224a6), UINT32_C(0x0c7f9048), UINT8_C(0xea) },
        { UINT32_C(0x0c7f9048), UINT32_C(0x39c5b9e0), UINT8_C(0x01) },
        { UINT32_C(0x39c5b9e0), UINT32_C(0x2493fabc), UINT8_C(0x04) },
        { UINT32_C(0x2493fabc), UINT32_C(0x0b05c4d6), UINT8_C(0x27) },
        { UINT32_C(0x0b05c4d6), UINT32_C(0xbe26a561), UINT8_C(0x2a) },
        { UINT32_C(0xbe26a561), UINT32_C(0xe1855652), UINT8_C(0x63) },
        { UINT32_C(0xe1855652), UINT32_C(0xc67efe3f), UINT8_C(0xa7) },
        { UINT32_C(0xc67efe3f), UINT32_C(0x227028cd), UINT8_C(0xfd) },
        { UINT32_C(0x227028cd), UINT32_C(0xf4559a1d), UINT8_C(0xea) },
    };
    static const BS3CPUINSTR2_CRC32_VALUES_T s_aValues2[] =
    {
        { UINT32_C(0000000000), UINT32_C(0000000000), UINT16_C(000000) },
        { UINT32_C(0xffffffff), UINT32_C(0xd550e2a0), UINT16_C(0x04d2) },
        { UINT32_C(0xd550e2a0), UINT32_C(0x38e07a0a), UINT16_C(0xe8cc) },
        { UINT32_C(0x38e07a0a), UINT32_C(0x60ebd519), UINT16_C(0x82a2) },
        { UINT32_C(0x60ebd519), UINT32_C(0xaaa127b5), UINT16_C(0x0fff) },
        { UINT32_C(0xaaa127b5), UINT32_C(0xb13175c6), UINT16_C(0x00ff) },
        { UINT32_C(0xb13175c6), UINT32_C(0x3a226f1b), UINT16_C(0x0300) },
        { UINT32_C(0x3a226f1b), UINT32_C(0xbaedef0c), UINT16_C(0x270f) },
        { UINT32_C(0xbaedef0c), UINT32_C(0x2d18866e), UINT16_C(0x3ff6) },
        { UINT32_C(0x2d18866e), UINT32_C(0x07e2e954), UINT16_C(0x9316) },
        { UINT32_C(0x07e2e954), UINT32_C(0x95f82acb), UINT16_C(0xa59c) },
    };
    static const BS3CPUINSTR2_CRC32_VALUES_T s_aValues4[] =
    {
        { UINT32_C(0000000000), UINT32_C(0000000000), UINT32_C(0000000000) },
        { UINT32_C(0xffffffff), UINT32_C(0xc9a7250e), UINT32_C(0x0270fa68) },
        { UINT32_C(0xc9a7250e), UINT32_C(0x7340d175), UINT32_C(0x23729736) },
        { UINT32_C(0x7340d175), UINT32_C(0x7e17b67d), UINT32_C(0x8bc75d35) },
        { UINT32_C(0x7e17b67d), UINT32_C(0x5028eb71), UINT32_C(0x0e9bebf2) },
        { UINT32_C(0x5028eb71), UINT32_C(0xc0a7f45a), UINT32_C(0x000001bc) },
        { UINT32_C(0xc0a7f45a), UINT32_C(0xa96f4012), UINT32_C(0x0034ba02) },
        { UINT32_C(0xa96f4012), UINT32_C(0xb27c0718), UINT32_C(0x0000002a) },
        { UINT32_C(0xb27c0718), UINT32_C(0x79fb2d35), UINT32_C(0x0153158e) },
        { UINT32_C(0x79fb2d35), UINT32_C(0x23434fc9), UINT32_C(0x02594882) },
        { UINT32_C(0x23434fc9), UINT32_C(0x354bf3b6), UINT32_C(0xb230b8f3) },
    };
#if ARCH_BITS >= 64
    static const BS3CPUINSTR2_CRC32_VALUES_T s_aValues8[] =
    {
        { UINT32_C(0000000000), UINT32_C(0000000000), UINT64_C(000000000000000000) },
        { UINT32_C(0xffffffff), UINT32_C(0xadc36834), UINT64_C(0x02b0b5e2a975c1cc) },
        { UINT32_C(0xadc36834), UINT32_C(0xf0e893c9), UINT64_C(0x823d386bf7517583) },
        { UINT32_C(0xf0e893c9), UINT32_C(0x1a22a837), UINT64_C(0x0481f5311fa061d0) },
        { UINT32_C(0x1a22a837), UINT32_C(0xcf8b6d61), UINT64_C(0x13fa70f64d52a92d) },
        { UINT32_C(0xcf8b6d61), UINT32_C(0xc7dde203), UINT64_C(0x3ccc8b035903d3e1) },
        { UINT32_C(0xc7dde203), UINT32_C(0xd42b5823), UINT64_C(0x0000011850ec2fac) },
        { UINT32_C(0xd42b5823), UINT32_C(0x8b1ce49e), UINT64_C(0x0000000000001364) },
        { UINT32_C(0x8b1ce49e), UINT32_C(0x1af31710), UINT64_C(0x000000057840205a) },
        { UINT32_C(0x1af31710), UINT32_C(0xdea35e8b), UINT64_C(0x2e5d93688d9a0bfa) },
        { UINT32_C(0xdea35e8b), UINT32_C(0x594c013a), UINT64_C(0x8ac7230489e7ffff) },
        { UINT32_C(0x594c013a), UINT32_C(0x27b061e5), UINT64_C(0x6bf037ae325f1c71) },
        { UINT32_C(0x27b061e5), UINT32_C(0x3120b5f7), UINT64_C(0x0fffffff34503556) },
    };
#endif
    static const struct
    {
        FPFNBS3FAR      pfnWorker;
        bool            fMemSrc;
        uint8_t         cbOp;
        uint8_t         cValues;
        BS3CPUINSTR2_CRC32_VALUES_T const BS3_FAR *paValues;
    } s_aTests[] =
    {
        /* 8-bit register width */
        {   BS3_CMN_NM(bs3CpuInstr2_crc32_EAX_BL_icebp),            false,  1, RT_ELEMENTS(s_aValues1), s_aValues1 },
        {   BS3_CMN_NM(bs3CpuInstr2_crc32_EAX_byte_FSxBX_icebp),    true,   1, RT_ELEMENTS(s_aValues1), s_aValues1 },

        /* 16-bit register width */
        {   BS3_CMN_NM(bs3CpuInstr2_crc32_EAX_BX_icebp),            false,  2, RT_ELEMENTS(s_aValues2), s_aValues2 },
        {   BS3_CMN_NM(bs3CpuInstr2_crc32_EAX_word_FSxBX_icebp),    true,   2, RT_ELEMENTS(s_aValues2), s_aValues2 },

        /* 32-bit register width */
        {   BS3_CMN_NM(bs3CpuInstr2_crc32_EAX_EBX_icebp),           false,  4, RT_ELEMENTS(s_aValues4), s_aValues4 },
        {   BS3_CMN_NM(bs3CpuInstr2_crc32_EAX_dword_FSxBX_icebp),   true,   4, RT_ELEMENTS(s_aValues4), s_aValues4 },
#if ARCH_BITS >= 64
        /* 32-bit register width */
        {   BS3_CMN_NM(bs3CpuInstr2_crc32_EAX_RBX_icebp),           false,  8, RT_ELEMENTS(s_aValues8), s_aValues8 },
        {   BS3_CMN_NM(bs3CpuInstr2_crc32_EAX_qword_FSxBX_icebp),   true,   8, RT_ELEMENTS(s_aValues8), s_aValues8 },
#endif
    };

    BS3REGCTX       Ctx;
    BS3TRAPFRAME    TrapFrame;
    unsigned        i, j;
    bool const      fSupportsCrc32 = (g_uBs3CpuDetected & BS3CPU_F_CPUID)
                                  && (ASMCpuId_ECX(1) & X86_CPUID_FEATURE_ECX_SSE4_2);

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);

    /*
     * Do the tests twice, first with all flags set, then once again with
     * flags cleared.  The flags are not supposed to be touched at all.
     */
    Ctx.rflags.u16 |= X86_EFL_STATUS_BITS;
    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            uint8_t const                               cbOp        = s_aTests[i].cbOp;
            unsigned const                              cValues     = s_aTests[i].cValues;
            BS3CPUINSTR2_CRC32_VALUES_T const BS3_FAR  *paValues    = s_aTests[i].paValues;
            unsigned                                    iValue;
            bool const                                  fOkay       = fSupportsCrc32;
            uint8_t const                               bExpectXcpt = fOkay ? X86_XCPT_DB : X86_XCPT_UD;
            uint64_t const                              uSrcGarbage = (  cbOp == 1 ? UINT64_C(0x03948314d0f03400)
                                                                       : cbOp == 2 ? UINT64_C(0x03948314d0f00000)
                                                                       : cbOp == 4 ? UINT64_C(0x0394831000000000) : 0)
                                                                      & (ARCH_BITS >= 64 ? UINT64_MAX : UINT32_MAX);
            uint64_t                                    uExpectRip;

            Bs3RegCtxSetRipCsFromCurPtr(&Ctx, s_aTests[i].pfnWorker);
            uExpectRip = Ctx.rip.u + (fOkay ? ((uint8_t const BS3_FAR *)s_aTests[i].pfnWorker)[-1] + 1 : 0);

            for (iValue = 0; iValue < cValues; iValue++)
            {
                uint64_t const uExpectRax = fOkay ? paValues[iValue].uDstOut : paValues[iValue].uDstIn;
                uint64_t       uMemSrc, uMemSrcExpect;

                Ctx.rax.uCcXReg = paValues[iValue].uDstIn;
                if (!s_aTests[i].fMemSrc)
                {
                    Ctx.rbx.u64 = paValues[iValue].uSrc | uSrcGarbage;
                    uMemSrcExpect = uMemSrc = ~(paValues[iValue].uSrc | uSrcGarbage);
                }
                else
                {
                    uMemSrcExpect = uMemSrc = paValues[iValue].uSrc | uSrcGarbage;
                    Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, &Ctx.fs, &uMemSrc);
                }

                Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);

                if (   TrapFrame.bXcpt     != bExpectXcpt
                    || TrapFrame.Ctx.rip.u != uExpectRip
                    || TrapFrame.Ctx.rbx.u != Ctx.rbx.u
                    || TrapFrame.Ctx.rax.u != uExpectRax
                    /* check that nothing else really changed: */
                    || TrapFrame.Ctx.rflags.u16 != Ctx.rflags.u16
                    || TrapFrame.Ctx.rcx.u != Ctx.rcx.u
                    || TrapFrame.Ctx.rdx.u != Ctx.rdx.u
                    || TrapFrame.Ctx.rsp.u != Ctx.rsp.u
                    || TrapFrame.Ctx.rbp.u != Ctx.rbp.u
                    || TrapFrame.Ctx.rsi.u != Ctx.rsi.u
                    || TrapFrame.Ctx.rdi.u != Ctx.rdi.u
                    || uMemSrc             != uMemSrcExpect
                   )
                {
                    Bs3TestFailedF("test #%i value #%i failed: input %#RX32, %#RX64",
                                   i, iValue, paValues[iValue].uDstIn, paValues[iValue].uSrc);
                    if (TrapFrame.bXcpt != bExpectXcpt)
                        Bs3TestFailedF("Expected bXcpt = %#x, got %#x", bExpectXcpt, TrapFrame.bXcpt);
                    if (TrapFrame.Ctx.rip.u != uExpectRip)
                        Bs3TestFailedF("Expected RIP = %#06RX64, got %#06RX64", uExpectRip, TrapFrame.Ctx.rip.u);
                    if (TrapFrame.Ctx.rax.u != uExpectRax)
                        Bs3TestFailedF("Expected RAX = %#010RX64, got %#010RX64", uExpectRax, TrapFrame.Ctx.rax.u);
                    if (TrapFrame.Ctx.rbx.u != Ctx.rbx.u)
                        Bs3TestFailedF("Expected RBX = %#06RX64, got %#06RX64 (dst)", Ctx.rbx.u, TrapFrame.Ctx.rbx.u);

                    if (TrapFrame.Ctx.rflags.u16 != Ctx.rflags.u16)
                        Bs3TestFailedF("Expected EFLAGS = %#06RX32, got %#06RX32", Ctx.rflags.u16, TrapFrame.Ctx.rflags.u16);
                    if (TrapFrame.Ctx.rcx.u != Ctx.rcx.u)
                        Bs3TestFailedF("Expected RCX = %#06RX64, got %#06RX64", Ctx.rcx.u, TrapFrame.Ctx.rcx.u);
                    if (TrapFrame.Ctx.rdx.u != Ctx.rdx.u)
                        Bs3TestFailedF("Expected RDX = %#06RX64, got %#06RX64 (src)", Ctx.rdx.u, TrapFrame.Ctx.rdx.u);
                    if (TrapFrame.Ctx.rsp.u != Ctx.rsp.u)
                        Bs3TestFailedF("Expected RSP = %#06RX64, got %#06RX64", Ctx.rsp.u, TrapFrame.Ctx.rsp.u);
                    if (TrapFrame.Ctx.rbp.u != Ctx.rbp.u)
                        Bs3TestFailedF("Expected RBP = %#06RX64, got %#06RX64", Ctx.rbp.u, TrapFrame.Ctx.rbp.u);
                    if (TrapFrame.Ctx.rsi.u != Ctx.rsi.u)
                        Bs3TestFailedF("Expected RSI = %#06RX64, got %#06RX64", Ctx.rsi.u, TrapFrame.Ctx.rsi.u);
                    if (TrapFrame.Ctx.rdi.u != Ctx.rdi.u)
                        Bs3TestFailedF("Expected RDI = %#06RX64, got %#06RX64", Ctx.rdi.u, TrapFrame.Ctx.rdi.u);
                    if (uMemSrc != uMemSrcExpect)
                        Bs3TestFailedF("Expected uMemSrc = %#06RX64, got %#06RX64", (uint64_t)uMemSrcExpect, (uint64_t)uMemSrc);
                }
            }
        }
        Ctx.rflags.u16 &= ~X86_EFL_STATUS_BITS;
    }

    return 0;
}

#if 0 /* Program for generating CRC32 value sets: */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int      cbOp       = atoi(argv[1]);
    uint32_t uBefore    = atoi(argv[2]);
    int      i          = 3;
    while (i < argc)
    {
        unsigned long long uValue = strtoull(argv[i], NULL, 0);
        uint32_t           uAfter = uBefore;
        switch (cbOp)
        {
            case 1:
                __asm__ __volatile__("crc32b %2, %0" : "=r" (uAfter) : "0" (uAfter), "r" ((uint8_t)uValue));
                printf("        { UINT32_C(%#010x), UINT32_C(%#010x), UINT8_C(%#04x) },\n",
                       uBefore, uAfter, (unsigned)(uint8_t)uValue);
                break;
            case 2:
                __asm__ __volatile__("crc32w %2, %0" : "=r" (uAfter) : "0" (uAfter), "r" ((uint16_t)uValue));
                printf("        { UINT32_C(%#010x), UINT32_C(%#010x), UINT16_C(%#06x) },\n",
                       uBefore, uAfter, (unsigned)(uint16_t)uValue);
                break;
            case 4:
                __asm__ __volatile__("crc32l %2, %0" : "=r" (uAfter) : "0" (uAfter), "r" ((uint32_t)uValue));
                printf("        { UINT32_C(%#010x), UINT32_C(%#010x), UINT32_C(%#010x) },\n",
                       uBefore, uAfter, (uint32_t)uValue);
                break;
            case 8:
            {
                uint64_t u64After = uBefore;
                __asm__ __volatile__("crc32q %2, %0" : "=r" (u64After) : "0" (u64After), "r" (uValue));
                uAfter = (uint32_t)u64After;
                printf("        { UINT32_C(%#010x), UINT32_C(%#010x), UINT64_C(%#018llx) },\n", uBefore, uAfter, uValue);
                break;
            }
        }

        /* next */
        uBefore = uAfter;
        i++;
    }
    return 0;
}
#endif


/*
 * ADCX/ADOX - ADX
 */
BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_adcx_adox)(uint8_t bMode)
{
    typedef struct BS3CPUINSTR2_ADX_VALUES_T
    {
        uint64_t uDstOut;
        uint64_t uDstIn;
        uint64_t uSrc;
        bool     fFlagIn;
        bool     fFlagOut;
    } BS3CPUINSTR2_ADX_VALUES_T;
    static const BS3CPUINSTR2_ADX_VALUES_T s_aValues4[] =
    {
        { UINT32_C(0000000000), UINT32_C(0000000000), UINT32_C(0000000000), false, false },
        { UINT32_C(0000000001), UINT32_C(0000000000), UINT32_C(0000000000), true,  false },

        { UINT32_C(0xfffffffe),       UINT32_MAX / 2,       UINT32_MAX / 2, false, false },
        { UINT32_C(0xffffffff),       UINT32_MAX / 2,       UINT32_MAX / 2, true,  false },

        { UINT32_C(0x7ffffffe),           UINT32_MAX,       UINT32_MAX / 2, false, true  },
        { UINT32_C(0x7fffffff),           UINT32_MAX,       UINT32_MAX / 2, true,  true  },

        { UINT32_C(0x7ffffffe),       UINT32_MAX / 2,           UINT32_MAX, false, true  },
        { UINT32_C(0x7fffffff),       UINT32_MAX / 2,           UINT32_MAX, true,  true  },

        { UINT32_C(0xfffffffe),           UINT32_MAX,           UINT32_MAX, false, true  },
        { UINT32_C(0xffffffff),           UINT32_MAX,           UINT32_MAX, true,  true  },
    };
#if ARCH_BITS >= 64
    static const BS3CPUINSTR2_ADX_VALUES_T s_aValues8[] =
    {
        { UINT64_C(00000000000000000000), UINT64_C(00000000000000000000), UINT64_C(00000000000000000000), false, false },
        { UINT64_C(00000000000000000001), UINT64_C(00000000000000000000), UINT64_C(00000000000000000000), true,  false },

        { UINT64_C(0xfffffffffffffffe),                   UINT64_MAX / 2,                 UINT64_MAX / 2, false, false },
        { UINT64_C(0xffffffffffffffff),                   UINT64_MAX / 2,                 UINT64_MAX / 2, true,  false },

        { UINT64_C(0x7ffffffffffffffe),                   UINT64_MAX,                     UINT64_MAX / 2, false, true  },
        { UINT64_C(0x7fffffffffffffff),                   UINT64_MAX,                     UINT64_MAX / 2, true,  true  },

        { UINT64_C(0x7ffffffffffffffe),                   UINT64_MAX / 2,                     UINT64_MAX, false, true  },
        { UINT64_C(0x7fffffffffffffff),                   UINT64_MAX / 2,                     UINT64_MAX, true,  true  },

        { UINT64_C(0xfffffffffffffffe),                   UINT64_MAX,                         UINT64_MAX, false, true  },
        { UINT64_C(0xffffffffffffffff),                   UINT64_MAX,                         UINT64_MAX, true,  true  },
    };
#endif
    static const struct
    {
        FPFNBS3FAR      pfnWorker;
        bool            fMemSrc;
        uint8_t         cbOp;
        uint8_t         cValues;
        BS3CPUINSTR2_ADX_VALUES_T const BS3_FAR *paValues;
        uint32_t        fEFlagsMod;
    } s_aTests[] =
    {
        /* 32-bit register width */
        {   BS3_CMN_NM(bs3CpuInstr2_adcx_EAX_EBX_icebp),           false,  4, RT_ELEMENTS(s_aValues4), s_aValues4, X86_EFL_CF },
        {   BS3_CMN_NM(bs3CpuInstr2_adcx_EAX_dword_FSxBX_icebp),   true,   4, RT_ELEMENTS(s_aValues4), s_aValues4, X86_EFL_CF },

        {   BS3_CMN_NM(bs3CpuInstr2_adox_EAX_EBX_icebp),           false,  4, RT_ELEMENTS(s_aValues4), s_aValues4, X86_EFL_OF },
        {   BS3_CMN_NM(bs3CpuInstr2_adox_EAX_dword_FSxBX_icebp),   true,   4, RT_ELEMENTS(s_aValues4), s_aValues4, X86_EFL_OF },
#if ARCH_BITS >= 64
        /* 64-bit register width */
        {   BS3_CMN_NM(bs3CpuInstr2_adcx_RAX_RBX_icebp),           false,  8, RT_ELEMENTS(s_aValues8), s_aValues8, X86_EFL_CF },
        {   BS3_CMN_NM(bs3CpuInstr2_adcx_RAX_qword_FSxBX_icebp),   true,   8, RT_ELEMENTS(s_aValues8), s_aValues8, X86_EFL_CF },

        {   BS3_CMN_NM(bs3CpuInstr2_adox_RAX_RBX_icebp),           false,  8, RT_ELEMENTS(s_aValues8), s_aValues8, X86_EFL_OF },
        {   BS3_CMN_NM(bs3CpuInstr2_adox_RAX_qword_FSxBX_icebp),   true,   8, RT_ELEMENTS(s_aValues8), s_aValues8, X86_EFL_OF },
#endif
    };

    BS3REGCTX       Ctx;
    BS3TRAPFRAME    TrapFrame;
    unsigned        i, j;
    bool            fSupportsAdx = false;

    if (   (g_uBs3CpuDetected & BS3CPU_F_CPUID)
        && ASMCpuId_EAX(0) >= 7)
    {
        uint32_t fEbx = 0;
        ASMCpuIdExSlow(7, 0, 0, 0, NULL, &fEbx, NULL, NULL);
        fSupportsAdx = RT_BOOL(fEbx & X86_CPUID_STEXT_FEATURE_EBX_ADX);
    }

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);

    /*
     * Do the tests twice, first with all flags set, then once again with
     * flags cleared.  The flags are not supposed to be touched at all except for the one indicated (CF or OF).
     */
    Ctx.rflags.u16 |= X86_EFL_STATUS_BITS;
    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            uint8_t const                               cbOp        = s_aTests[i].cbOp;
            unsigned const                              cValues     = s_aTests[i].cValues;
            BS3CPUINSTR2_ADX_VALUES_T const BS3_FAR    *paValues    = s_aTests[i].paValues;
            uint32_t const                              fEFlagsMod  = s_aTests[i].fEFlagsMod;
            unsigned                                    iValue;
            bool const                                  fOkay       = fSupportsAdx;
            uint8_t const                               bExpectXcpt = fOkay ? X86_XCPT_DB : X86_XCPT_UD;
            uint64_t const                              uSrcGarbage = ( cbOp == 4 ? UINT64_C(0x0394831000000000) : 0)
                                                                      & (ARCH_BITS >= 64 ? UINT64_MAX : UINT32_MAX);
            uint64_t                                    uExpectRip;

            Bs3RegCtxSetRipCsFromCurPtr(&Ctx, s_aTests[i].pfnWorker);
            uExpectRip = Ctx.rip.u + (fOkay ? ((uint8_t const BS3_FAR *)s_aTests[i].pfnWorker)[-1] + 1 : 0);

            for (iValue = 0; iValue < cValues; iValue++)
            {
                uint64_t const uExpectRax = fOkay ? paValues[iValue].uDstOut : paValues[iValue].uDstIn;
                uint64_t       uMemSrc, uMemSrcExpect;

                Ctx.rax.uCcXReg = paValues[iValue].uDstIn;
                if (!s_aTests[i].fMemSrc)
                {
                    Ctx.rbx.u64 = paValues[iValue].uSrc | uSrcGarbage;
                    uMemSrcExpect = uMemSrc = ~(paValues[iValue].uSrc | uSrcGarbage);
                }
                else
                {
                    uMemSrcExpect = uMemSrc = paValues[iValue].uSrc | uSrcGarbage;
                    Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, &Ctx.fs, &uMemSrc);
                }

                Ctx.rflags.u16 &= ~fEFlagsMod;
                if (paValues[iValue].fFlagIn)
                    Ctx.rflags.u16 |= fEFlagsMod;

                Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);

                if (fOkay)
                {
                    Ctx.rflags.u16 &= ~fEFlagsMod;
                    if (paValues[iValue].fFlagOut)
                        Ctx.rflags.u16 |= fEFlagsMod;
                }

                if (   TrapFrame.bXcpt     != bExpectXcpt
                    || TrapFrame.Ctx.rip.u != uExpectRip
                    || TrapFrame.Ctx.rbx.u != Ctx.rbx.u
                    || TrapFrame.Ctx.rax.u != uExpectRax
                    /* check that nothing else really changed: */
                    || TrapFrame.Ctx.rflags.u16 != Ctx.rflags.u16
                    || TrapFrame.Ctx.rcx.u != Ctx.rcx.u
                    || TrapFrame.Ctx.rdx.u != Ctx.rdx.u
                    || TrapFrame.Ctx.rsp.u != Ctx.rsp.u
                    || TrapFrame.Ctx.rbp.u != Ctx.rbp.u
                    || TrapFrame.Ctx.rsi.u != Ctx.rsi.u
                    || TrapFrame.Ctx.rdi.u != Ctx.rdi.u
                    || uMemSrc             != uMemSrcExpect
                   )
                {
                    Bs3TestFailedF("test #%i value #%i failed: input %#RX64, %#RX64",
                                   i, iValue, paValues[iValue].uDstIn, paValues[iValue].uSrc);
                    if (TrapFrame.bXcpt != bExpectXcpt)
                        Bs3TestFailedF("Expected bXcpt = %#x, got %#x", bExpectXcpt, TrapFrame.bXcpt);
                    if (TrapFrame.Ctx.rip.u != uExpectRip)
                        Bs3TestFailedF("Expected RIP = %#06RX64, got %#06RX64", uExpectRip, TrapFrame.Ctx.rip.u);
                    if (TrapFrame.Ctx.rax.u != uExpectRax)
                        Bs3TestFailedF("Expected RAX = %#010RX64, got %#010RX64", uExpectRax, TrapFrame.Ctx.rax.u);
                    if (TrapFrame.Ctx.rbx.u != Ctx.rbx.u)
                        Bs3TestFailedF("Expected RBX = %#06RX64, got %#06RX64 (dst)", Ctx.rbx.u, TrapFrame.Ctx.rbx.u);

                    if (TrapFrame.Ctx.rflags.u16 != Ctx.rflags.u16)
                        Bs3TestFailedF("Expected EFLAGS = %#06RX16, got %#06RX16", Ctx.rflags.u16, TrapFrame.Ctx.rflags.u16);
                    if (TrapFrame.Ctx.rcx.u != Ctx.rcx.u)
                        Bs3TestFailedF("Expected RCX = %#06RX64, got %#06RX64", Ctx.rcx.u, TrapFrame.Ctx.rcx.u);
                    if (TrapFrame.Ctx.rdx.u != Ctx.rdx.u)
                        Bs3TestFailedF("Expected RDX = %#06RX64, got %#06RX64 (src)", Ctx.rdx.u, TrapFrame.Ctx.rdx.u);
                    if (TrapFrame.Ctx.rsp.u != Ctx.rsp.u)
                        Bs3TestFailedF("Expected RSP = %#06RX64, got %#06RX64", Ctx.rsp.u, TrapFrame.Ctx.rsp.u);
                    if (TrapFrame.Ctx.rbp.u != Ctx.rbp.u)
                        Bs3TestFailedF("Expected RBP = %#06RX64, got %#06RX64", Ctx.rbp.u, TrapFrame.Ctx.rbp.u);
                    if (TrapFrame.Ctx.rsi.u != Ctx.rsi.u)
                        Bs3TestFailedF("Expected RSI = %#06RX64, got %#06RX64", Ctx.rsi.u, TrapFrame.Ctx.rsi.u);
                    if (TrapFrame.Ctx.rdi.u != Ctx.rdi.u)
                        Bs3TestFailedF("Expected RDI = %#06RX64, got %#06RX64", Ctx.rdi.u, TrapFrame.Ctx.rdi.u);
                    if (uMemSrc != uMemSrcExpect)
                        Bs3TestFailedF("Expected uMemSrc = %#06RX64, got %#06RX64", (uint64_t)uMemSrcExpect, (uint64_t)uMemSrc);
                }
            }
        }
        Ctx.rflags.u16 &= ~X86_EFL_STATUS_BITS;
    }

    return 0;
}



/*
 * MOVBE
 */
BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_movbe)(uint8_t bMode)
{
    const char BS3_FAR * const  pszMode     = Bs3GetModeName(bMode);

    typedef struct BS3CPUINSTR2_MOVBE_VALUES_T
    {
        RTCCUINTXREG uDstOut;
        RTCCUINTXREG uDstIn;
        RTCCUINTXREG uSrc;
    } BS3CPUINSTR2_MOVBE_VALUES_T;
    static const BS3CPUINSTR2_MOVBE_VALUES_T s_aValues2[] =
    {
        { UINT64_C(0xc0dedeaddead3412), UINT64_C(0xc0dedeaddeadc0de), UINT16_C(0x1234) }
    };
    static const BS3CPUINSTR2_MOVBE_VALUES_T s_aValues4MemSrc[] =
    {
        { UINT64_C(0x78563412), UINT64_C(0xc0dedeaddeadc0de), UINT32_C(0x12345678) }
    };
    static const BS3CPUINSTR2_MOVBE_VALUES_T s_aValues4MemDst[] =
    {
        { UINT64_C(0xc0dedead78563412), UINT64_C(0xc0dedeaddeadc0de), UINT32_C(0x12345678) }
    };
#if ARCH_BITS >= 64
    static const BS3CPUINSTR2_MOVBE_VALUES_T s_aValues8[] =
    {
        { UINT64_C(0xf0debc9a78563412), UINT64_C(0xc0dedeaddeadc0de), UINT64_C(0x123456789abcdef0) }
    };
#endif
    static const struct
    {
        FPFNBS3FAR      pfnWorker;
        bool            fMemSrc;
        uint8_t         offIcebp;
        uint8_t         cValues;
        BS3CPUINSTR2_MOVBE_VALUES_T const BS3_FAR *paValues;
    } s_aTests[] =
    {
        /* 16-bit register width */
        {   BS3_CMN_NM(bs3CpuInstr2_movbe_AX_word_FSxBX_icebp),     true,    6 + (ARCH_BITS != 16), RT_ELEMENTS(s_aValues2),       s_aValues2 },
        {   BS3_CMN_NM(bs3CpuInstr2_movbe_word_FSxBX_AX_icebp),     false,   6 + (ARCH_BITS != 16), RT_ELEMENTS(s_aValues2),       s_aValues2 },
        /* 32-bit register width */
        {   BS3_CMN_NM(bs3CpuInstr2_movbe_EAX_dword_FSxBX_icebp),   true,    6 + (ARCH_BITS == 16), RT_ELEMENTS(s_aValues4MemSrc), s_aValues4MemSrc },
        {   BS3_CMN_NM(bs3CpuInstr2_movbe_dword_FSxBX_EAX_icebp),   false,   6 + (ARCH_BITS == 16), RT_ELEMENTS(s_aValues4MemDst), s_aValues4MemDst },
#if ARCH_BITS >= 64
        /* 64-bit register width */
        {   BS3_CMN_NM(bs3CpuInstr2_movbe_RAX_qword_FSxBX_icebp),   true,    7,                     RT_ELEMENTS(s_aValues8),       s_aValues8 },
        {   BS3_CMN_NM(bs3CpuInstr2_movbe_qword_FSxBX_RAX_icebp),   false,   7,                     RT_ELEMENTS(s_aValues8),       s_aValues8 },
#endif
    };

    BS3REGCTX       Ctx;
    BS3REGCTX       ExpectCtx;
    BS3TRAPFRAME    TrapFrame;
    unsigned        i, j;
    bool            fSupportsMovBe = false;

    if (   (g_uBs3CpuDetected & BS3CPU_F_CPUID)
        && ASMCpuId_EAX(0) >= 1)
    {
        uint32_t fEcx = 0;
        ASMCpuIdExSlow(1, 0, 0, 0, NULL, NULL, &fEcx, NULL);
        fSupportsMovBe = RT_BOOL(fEcx & X86_CPUID_FEATURE_ECX_MOVBE);
    }

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));
    Bs3MemSet(&ExpectCtx, 0, sizeof(ExpectCtx));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);

    /*
     * Do the tests twice, first with all flags set, then once again with
     * flags cleared.  The flags are not supposed to be touched at all.
     */
    g_usBs3TestStep = 0;
    for (j = 0; j < 2; j++)
    {
        for (i = 0; i < RT_ELEMENTS(s_aTests); i++)
        {
            unsigned const                              cValues     = s_aTests[i].cValues;
            BS3CPUINSTR2_MOVBE_VALUES_T const BS3_FAR  *paValues    = s_aTests[i].paValues;
            unsigned                                    iValue;
            bool const                                  fOkay       = fSupportsMovBe;
            uint8_t const                               bExpectXcpt = fOkay ? X86_XCPT_DB : X86_XCPT_UD;
            uint64_t                                    uExpectRip;

            Bs3RegCtxSetRipCsFromCurPtr(&Ctx, s_aTests[i].pfnWorker);
            uExpectRip = Ctx.rip.u + (fOkay ? ((uint8_t const BS3_FAR *)s_aTests[i].pfnWorker)[-1] + 1 : 0);

            for (iValue = 0; iValue < cValues; iValue++)
            {
                //uint64_t const uExpectRax = fOkay ? paValues[iValue].uDstOut : paValues[iValue].uDstIn;
                uint64_t       uMem, uMemExpect;

                Bs3MemCpy(&ExpectCtx, &Ctx, sizeof(ExpectCtx));

                if (!s_aTests[i].fMemSrc)
                {
                    /* Memory is destination */
                    Ctx.rax.u64       = paValues[iValue].uSrc;
                    ExpectCtx.rax.u64 = paValues[iValue].uSrc;
                    uMem              = paValues[iValue].uDstIn;
                    uMemExpect        = paValues[iValue].uDstOut;
                    Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, &Ctx.fs, &uMem);
                    Bs3RegCtxSetGrpSegFromCurPtr(&ExpectCtx, &ExpectCtx.rbx, &ExpectCtx.fs, &uMem);
                }
                else
                {
                    /* Memory is source */
                    uMemExpect = uMem = paValues[iValue].uSrc;
                    Ctx.rax.u64       = paValues[iValue].uDstIn;
                    ExpectCtx.rax.u64 = paValues[iValue].uDstOut;
                    Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, &Ctx.fs, &uMem);
                    Bs3RegCtxSetGrpSegFromCurPtr(&ExpectCtx, &ExpectCtx.rbx, &ExpectCtx.fs, &uMem);
                }

                Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);
                g_usBs3TestStep++;

                if (   !Bs3TestCheckRegCtxEx(&TrapFrame.Ctx, &ExpectCtx, bExpectXcpt == X86_XCPT_DB ? s_aTests[i].offIcebp : 0 /*cbPcAdjust*/,
                                             0 /*cbSpAcjust*/, 0 /*fExtraEfl*/, pszMode, g_usBs3TestStep)
                    || TrapFrame.bXcpt != bExpectXcpt
                    || uMem            != uMemExpect
                   )
                {
                    if (TrapFrame.bXcpt != bExpectXcpt)
                        Bs3TestFailedF("Expected bXcpt=#%x, got %#x (%#x)", bExpectXcpt, TrapFrame.bXcpt, TrapFrame.uErrCd);
                    if (uMem != uMemExpect)
                        Bs3TestFailedF("Expected uMem = %#06RX64, got %#06RX64", (uint64_t)uMemExpect, (uint64_t)uMem);
                    Bs3TestFailedF("^^^ iCfg=%u iWorker=%d iValue=%d\n",
                                   j, i, iValue);
                }
            }
        }
        Ctx.rflags.u16 &= ~X86_EFL_STATUS_BITS;
    }

    return 0;
}



/*
 * CMPXCHG8B
 */
BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_cmpxchg8b)(uint8_t bMode)
{

    BS3REGCTX                   Ctx;
    BS3REGCTX                   ExpectCtx;
    BS3TRAPFRAME                TrapFrame;
    RTUINT64U                   au64[3];
    PRTUINT64U                  pau64       = RT_ALIGN_PT(&au64[0], sizeof(RTUINT64U), PRTUINT64U);
    bool const                  fSupportCX8 = RT_BOOL(ASMCpuId_EDX(1) & X86_CPUID_FEATURE_EDX_CX8);
    const char BS3_FAR * const  pszMode     = Bs3GetModeName(bMode);
    uint8_t                     bRing       = BS3_MODE_IS_V86(bMode) ? 3 : 0;
    unsigned                    iFlags;
    unsigned                    offBuf;
    unsigned                    iMatch;
    unsigned                    iWorker;
    static struct
    {
        bool        fLocked;
        uint8_t     offIcebp;
        FNBS3FAR   *pfnWorker;
    } const s_aWorkers[] =
    {
        {   false,  5,  BS3_CMN_NM(bs3CpuInstr2_cmpxchg8b_FSxDI_icebp)            },
#if TMPL_MODE == BS3_MODE_RM || TMPL_MODE == BS3_MODE_PP16
        {   false,  5,  BS3_CMN_NM(bs3CpuInstr2_o16_cmpxchg8b_FSxDI_icebp)        },
#else
        {   false,  6,  BS3_CMN_NM(bs3CpuInstr2_o16_cmpxchg8b_FSxDI_icebp)        },
#endif
        {   false,  6,  BS3_CMN_NM(bs3CpuInstr2_repz_cmpxchg8b_FSxDI_icebp)       },
        {   false,  6,  BS3_CMN_NM(bs3CpuInstr2_repnz_cmpxchg8b_FSxDI_icebp)      },
        {   true, 1+5,  BS3_CMN_NM(bs3CpuInstr2_lock_cmpxchg8b_FSxDI_icebp)       },
        {   true, 1+6,  BS3_CMN_NM(bs3CpuInstr2_lock_o16_cmpxchg8b_FSxDI_icebp)   },
        {   true, 1+6,  BS3_CMN_NM(bs3CpuInstr2_lock_repz_cmpxchg8b_FSxDI_icebp)  },
        {   true, 1+6,  BS3_CMN_NM(bs3CpuInstr2_lock_repnz_cmpxchg8b_FSxDI_icebp) },
    };

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&ExpectCtx, 0, sizeof(ExpectCtx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));
    Bs3MemSet(pau64, 0, sizeof(pau64[0]) * 2);

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);
    if (!fSupportCX8)
        Bs3TestPrintf("Note! CMPXCHG8B is not supported by the CPU!\n");

    /*
     * Run the tests in all rings since alignment issues may behave
     * differently in ring-3 compared to ring-0.
     */
    for (;;)
    {
        /*
         * One loop with alignment checks disabled and one with enabled.
         */
        unsigned iCfg;
        for (iCfg = 0; iCfg < 2; iCfg++)
        {
            if (iCfg)
            {
                Ctx.rflags.u32 |= X86_EFL_AC;
                Ctx.cr0.u32    |= X86_CR0_AM;
            }
            else
            {
                Ctx.rflags.u32 &= ~X86_EFL_AC;
                Ctx.cr0.u32    &= ~X86_CR0_AM;
            }

            /*
             * One loop with the normal variant and one with the locked one
             */
            g_usBs3TestStep = 0;
            for (iWorker = 0; iWorker < RT_ELEMENTS(s_aWorkers); iWorker++)
            {
                Bs3RegCtxSetRipCsFromCurPtr(&Ctx, s_aWorkers[iWorker].pfnWorker);

                /*
                 * One loop with all status flags set, and one with them clear.
                 */
                Ctx.rflags.u16 |= X86_EFL_STATUS_BITS;
                for (iFlags = 0; iFlags < 2; iFlags++)
                {
                    Bs3MemCpy(&ExpectCtx, &Ctx, sizeof(ExpectCtx));

                    for (offBuf = 0; offBuf < sizeof(RTUINT64U); offBuf++)
                    {
#  define CX8_OLD_LO       UINT32_C(0xcc9c4bbd)
#  define CX8_OLD_HI       UINT32_C(0x749549ab)
#  define CX8_MISMATCH_LO  UINT32_C(0x90f18981)
#  define CX8_MISMATCH_HI  UINT32_C(0xfd5b4000)
#  define CX8_STORE_LO     UINT32_C(0x51f6559b)
#  define CX8_STORE_HI     UINT32_C(0xd1b54963)

                        PRTUINT64U pBuf = (PRTUINT64U)&pau64->au8[offBuf];

                        ExpectCtx.rax.u = Ctx.rax.u = CX8_MISMATCH_LO;
                        ExpectCtx.rdx.u = Ctx.rdx.u = CX8_MISMATCH_HI;

                        Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rdi, &Ctx.fs, pBuf);
                        Bs3RegCtxSetGrpSegFromCurPtr(&ExpectCtx, &ExpectCtx.rdi, &ExpectCtx.fs, pBuf);

                        for (iMatch = 0; iMatch < 2; iMatch++)
                        {
                            uint8_t bExpectXcpt;
                            pBuf->s.Lo = CX8_OLD_LO;
                            pBuf->s.Hi = CX8_OLD_HI;

                            Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);
                            g_usBs3TestStep++;
                            //Bs3TestPrintf("Test: iFlags=%d offBuf=%d iMatch=%u iWorker=%u\n", iFlags, offBuf, iMatch, iWorker);
                            bExpectXcpt = X86_XCPT_UD;
                            if (fSupportCX8)
                            {
                                if (   offBuf
                                    && bRing == 3
                                    && bMode != BS3_MODE_RM
                                    && !BS3_MODE_IS_V86(bMode)
                                    && iCfg)
                                {
                                    bExpectXcpt = X86_XCPT_AC;
                                    ExpectCtx.rflags.u32 = Ctx.rflags.u32;
                                }
                                else
                                {
                                    bExpectXcpt = X86_XCPT_DB;

                                    ExpectCtx.rax.u = CX8_OLD_LO;
                                    ExpectCtx.rdx.u = CX8_OLD_HI;

                                    if (iMatch & 1)
                                        ExpectCtx.rflags.u32 = Ctx.rflags.u32 | X86_EFL_ZF;
                                    else
                                        ExpectCtx.rflags.u32 = Ctx.rflags.u32 & ~X86_EFL_ZF;
                                }

                                /* Kludge! Looks like EFLAGS.AC is cleared when raising #GP in real mode on an i7-6700K. WEIRD! */
                                if (bMode == BS3_MODE_RM && (Ctx.rflags.u32 & X86_EFL_AC))
                                {
                                    if (TrapFrame.Ctx.rflags.u32 & X86_EFL_AC)
                                        Bs3TestFailedF("Expected EFLAGS.AC to be cleared (bXcpt=%d)", TrapFrame.bXcpt);
                                    TrapFrame.Ctx.rflags.u32 |= X86_EFL_AC;
                                }
                            }

                            if (   !Bs3TestCheckRegCtxEx(&TrapFrame.Ctx, &ExpectCtx, bExpectXcpt == X86_XCPT_DB ? s_aWorkers[iWorker].offIcebp : 0, 0 /*cbSpAdjust*/,
                                                         bExpectXcpt == X86_XCPT_DB || BS3_MODE_IS_16BIT_SYS(bMode) ? 0 : X86_EFL_RF, pszMode, g_usBs3TestStep)
                                || TrapFrame.bXcpt != bExpectXcpt)
                            {
                                if (TrapFrame.bXcpt != bExpectXcpt)
                                    Bs3TestFailedF("Expected bXcpt=#%x, got %#x (%#x)", bExpectXcpt, TrapFrame.bXcpt, TrapFrame.uErrCd);
                                Bs3TestFailedF("^^^ bRing=%u iCfg=%d iWorker=%d iFlags=%d offBuf=%d iMatch=%u\n",
                                               bRing, iCfg, iWorker, iFlags, offBuf, iMatch);
                                ASMHalt();
                            }

                            ExpectCtx.rax.u = Ctx.rax.u = CX8_OLD_LO;
                            ExpectCtx.rdx.u = Ctx.rdx.u = CX8_OLD_HI;
                        }
                    }
                    Ctx.rflags.u16 &= ~X86_EFL_STATUS_BITS;
                }
            }
        } /* for each test config. */

        /*
         * Next ring.
         */
        bRing++;
        if (bRing > 3 || bMode == BS3_MODE_RM)
            break;
        Bs3RegCtxConvertToRingX(&Ctx, bRing);
    }

    return 0;
}



/*
 *
 */
# if ARCH_BITS == 64

BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_cmpxchg16b)(uint8_t bMode)
{
    BS3REGCTX                   Ctx;
    BS3REGCTX                   ExpectCtx;
    BS3TRAPFRAME                TrapFrame;
    RTUINT128U                  au128[3];
    PRTUINT128U                 pau128       = RT_ALIGN_PT(&au128[0], sizeof(RTUINT128U), PRTUINT128U);
    bool const                  fSupportCX16 = RT_BOOL(ASMCpuId_ECX(1) & X86_CPUID_FEATURE_ECX_CX16);
    const char BS3_FAR * const  pszMode      = Bs3GetModeName(bMode);
    uint8_t                     bRing        = BS3_MODE_IS_V86(bMode) ? 3 : 0;
    unsigned                    iFlags;
    unsigned                    offBuf;
    unsigned                    iMatch;
    unsigned                    iWorker;
    static struct
    {
        bool        fLocked;
        uint8_t     offUd2;
        FNBS3FAR   *pfnWorker;
    } const s_aWorkers[] =
    {
        {   false,  4,  BS3_CMN_NM(bs3CpuInstr2_cmpxchg16b_rdi_ud2) },
        {   false,  5,  BS3_CMN_NM(bs3CpuInstr2_o16_cmpxchg16b_rdi_ud2) },
        {   false,  5,  BS3_CMN_NM(bs3CpuInstr2_repz_cmpxchg16b_rdi_ud2) },
        {   false,  5,  BS3_CMN_NM(bs3CpuInstr2_repnz_cmpxchg16b_rdi_ud2) },
        {   true, 1+4,  BS3_CMN_NM(bs3CpuInstr2_lock_cmpxchg16b_rdi_ud2) },
        {   true, 1+5,  BS3_CMN_NM(bs3CpuInstr2_lock_o16_cmpxchg16b_rdi_ud2) },
        {   true, 1+5,  BS3_CMN_NM(bs3CpuInstr2_lock_repz_cmpxchg16b_rdi_ud2) },
        {   true, 1+5,  BS3_CMN_NM(bs3CpuInstr2_lock_repnz_cmpxchg16b_rdi_ud2) },
    };

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&ExpectCtx, 0, sizeof(ExpectCtx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));
    Bs3MemSet(pau128, 0, sizeof(pau128[0]) * 2);

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);
    if (!fSupportCX16)
        Bs3TestPrintf("Note! CMPXCHG16B is not supported by the CPU!\n");


    /*
     * Run the tests in all rings since alignment issues may behave
     * differently in ring-3 compared to ring-0.
     */
    for (;;)
    {
        /*
         * One loop with alignment checks disabled and one with enabled.
         */
        unsigned iCfg;
        for (iCfg = 0; iCfg < 2; iCfg++)
        {
            if (iCfg)
            {
                Ctx.rflags.u32 |= X86_EFL_AC;
                Ctx.cr0.u32    |= X86_CR0_AM;
            }
            else
            {
                Ctx.rflags.u32 &= ~X86_EFL_AC;
                Ctx.cr0.u32    &= ~X86_CR0_AM;
            }

            /*
             * One loop with the normal variant and one with the locked one
             */
            g_usBs3TestStep = 0;
            for (iWorker = 0; iWorker < RT_ELEMENTS(s_aWorkers); iWorker++)
            {
                Bs3RegCtxSetRipCsFromCurPtr(&Ctx, s_aWorkers[iWorker].pfnWorker);

                /*
                 * One loop with all status flags set, and one with them clear.
                 */
                Ctx.rflags.u16 |= X86_EFL_STATUS_BITS;
                for (iFlags = 0; iFlags < 2; iFlags++)
                {
                    Bs3MemCpy(&ExpectCtx, &Ctx, sizeof(ExpectCtx));

                    for (offBuf = 0; offBuf < sizeof(RTUINT128U); offBuf++)
                    {
#  define CX16_OLD_LO       UINT64_C(0xabb6345dcc9c4bbd)
#  define CX16_OLD_HI       UINT64_C(0x7b06ea35749549ab)
#  define CX16_MISMATCH_LO  UINT64_C(0xbace3e3590f18981)
#  define CX16_MISMATCH_HI  UINT64_C(0x9b385e8bfd5b4000)
#  define CX16_STORE_LO     UINT64_C(0x5cbd27d251f6559b)
#  define CX16_STORE_HI     UINT64_C(0x17ff434ed1b54963)

                        PRTUINT128U pBuf = (PRTUINT128U)&pau128->au8[offBuf];

                        ExpectCtx.rax.u = Ctx.rax.u = CX16_MISMATCH_LO;
                        ExpectCtx.rdx.u = Ctx.rdx.u = CX16_MISMATCH_HI;
                        for (iMatch = 0; iMatch < 2; iMatch++)
                        {
                            uint8_t bExpectXcpt;
                            pBuf->s.Lo = CX16_OLD_LO;
                            pBuf->s.Hi = CX16_OLD_HI;
                            ExpectCtx.rdi.u = Ctx.rdi.u = (uintptr_t)pBuf;
                            Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);
                            g_usBs3TestStep++;
                            //Bs3TestPrintf("Test: iFlags=%d offBuf=%d iMatch=%u iWorker=%u\n", iFlags, offBuf, iMatch, iWorker);
                            bExpectXcpt = X86_XCPT_UD;
                            if (fSupportCX16)
                            {
                                if (offBuf & 15)
                                {
                                    bExpectXcpt = X86_XCPT_GP;
                                    ExpectCtx.rip.u = Ctx.rip.u;
                                    ExpectCtx.rflags.u32 = Ctx.rflags.u32;
                                }
                                else
                                {
                                    ExpectCtx.rax.u = CX16_OLD_LO;
                                    ExpectCtx.rdx.u = CX16_OLD_HI;
                                    if (iMatch & 1)
                                        ExpectCtx.rflags.u32 = Ctx.rflags.u32 | X86_EFL_ZF;
                                    else
                                        ExpectCtx.rflags.u32 = Ctx.rflags.u32 & ~X86_EFL_ZF;
                                    ExpectCtx.rip.u = Ctx.rip.u + s_aWorkers[iWorker].offUd2;
                                }
                                ExpectCtx.rflags.u32 |= X86_EFL_RF;
                            }
                            if (   !Bs3TestCheckRegCtxEx(&TrapFrame.Ctx, &ExpectCtx, 0 /*cbPcAdjust*/, 0 /*cbSpAdjust*/,
                                                         0 /*fExtraEfl*/, pszMode, 0 /*idTestStep*/)
                                || TrapFrame.bXcpt != bExpectXcpt)
                            {
                                if (TrapFrame.bXcpt != bExpectXcpt)
                                    Bs3TestFailedF("Expected bXcpt=#%x, got %#x (%#x)", bExpectXcpt, TrapFrame.bXcpt, TrapFrame.uErrCd);
                                Bs3TestFailedF("^^^bRing=%d iWorker=%d iFlags=%d offBuf=%d iMatch=%u\n", bRing, iWorker, iFlags, offBuf, iMatch);
                                ASMHalt();
                            }

                            ExpectCtx.rax.u = Ctx.rax.u = CX16_OLD_LO;
                            ExpectCtx.rdx.u = Ctx.rdx.u = CX16_OLD_HI;
                        }
                    }
                    Ctx.rflags.u16 &= ~X86_EFL_STATUS_BITS;
                }
            }
        } /* for each test config. */

        /*
         * Next ring.
         */
        bRing++;
        if (bRing > 3 || bMode == BS3_MODE_RM)
            break;
        Bs3RegCtxConvertToRingX(&Ctx, bRing);
    }

    return 0;
}


static void bs3CpuInstr2_fsgsbase_ExpectUD(uint8_t bMode, PBS3REGCTX pCtx, PBS3REGCTX pExpectCtx, PBS3TRAPFRAME pTrapFrame)
{
    pCtx->rbx.u  = 0;
    Bs3MemCpy(pExpectCtx, pCtx, sizeof(*pExpectCtx));
    Bs3TrapSetJmpAndRestore(pCtx, pTrapFrame);
    pExpectCtx->rip.u       = pCtx->rip.u;
    pExpectCtx->rflags.u32 |= X86_EFL_RF;
    if (   !Bs3TestCheckRegCtxEx(&pTrapFrame->Ctx, pExpectCtx, 0 /*cbPcAdjust*/, 0 /*cbSpAdjust*/, 0 /*fExtraEfl*/, "lm64",
                                 0 /*idTestStep*/)
        || pTrapFrame->bXcpt != X86_XCPT_UD)
    {
        Bs3TestFailedF("Expected #UD, got %#x (%#x)", pTrapFrame->bXcpt, pTrapFrame->uErrCd);
        ASMHalt();
    }
}


static bool bs3CpuInstr2_fsgsbase_VerifyWorker(uint8_t bMode, PBS3REGCTX pCtx, PBS3REGCTX pExpectCtx, PBS3TRAPFRAME pTrapFrame,
                                               BS3CI2FSGSBASE const *pFsGsBaseWorker, unsigned *puIter)
{
    bool     fPassed = true;
    unsigned iValue  = 0;
    static const struct
    {
        bool      fGP;
        uint64_t  u64Base;
    } s_aValues64[] =
    {
        { false, UINT64_C(0x0000000000000000) },
        { false, UINT64_C(0x0000000000000001) },
        { false, UINT64_C(0x0000000000000010) },
        { false, UINT64_C(0x0000000000000123) },
        { false, UINT64_C(0x0000000000001234) },
        { false, UINT64_C(0x0000000000012345) },
        { false, UINT64_C(0x0000000000123456) },
        { false, UINT64_C(0x0000000001234567) },
        { false, UINT64_C(0x0000000012345678) },
        { false, UINT64_C(0x0000000123456789) },
        { false, UINT64_C(0x000000123456789a) },
        { false, UINT64_C(0x00000123456789ab) },
        { false, UINT64_C(0x0000123456789abc) },
        { false, UINT64_C(0x00007ffffeefefef) },
        { false, UINT64_C(0x00007fffffffffff) },
        {  true, UINT64_C(0x0000800000000000) },
        {  true, UINT64_C(0x0000800000000000) },
        {  true, UINT64_C(0x0000800000000333) },
        {  true, UINT64_C(0x0001000000000000) },
        {  true, UINT64_C(0x0012000000000000) },
        {  true, UINT64_C(0x0123000000000000) },
        {  true, UINT64_C(0x1234000000000000) },
        {  true, UINT64_C(0xffff300000000000) },
        {  true, UINT64_C(0xffff7fffffffffff) },
        {  true, UINT64_C(0xffff7fffffffffff) },
        { false, UINT64_C(0xffff800000000000) },
        { false, UINT64_C(0xffffffffffeefefe) },
        { false, UINT64_C(0xffffffffffffffff) },
        { false, UINT64_C(0xffffffffffffffff) },
        { false, UINT64_C(0x00000000efefefef) },
        { false, UINT64_C(0x0000000080204060) },
        { false, UINT64_C(0x00000000ddeeffaa) },
        { false, UINT64_C(0x00000000fdecdbca) },
        { false, UINT64_C(0x000000006098456b) },
        { false, UINT64_C(0x0000000098506099) },
        { false, UINT64_C(0x00000000206950bc) },
        { false, UINT64_C(0x000000009740395d) },
        { false, UINT64_C(0x0000000064a9455e) },
        { false, UINT64_C(0x00000000d20b6eff) },
        { false, UINT64_C(0x0000000085296d46) },
        { false, UINT64_C(0x0000000007000039) },
        { false, UINT64_C(0x000000000007fe00) },
    };

    Bs3RegCtxSetRipCsFromCurPtr(pCtx, pFsGsBaseWorker->pfnVerifyWorker);
    if (pFsGsBaseWorker->f64BitOperand)
    {
        for (iValue = 0; iValue < RT_ELEMENTS(s_aValues64); iValue++)
        {
            bool const fGP = s_aValues64[iValue].fGP;

            pCtx->rbx.u  = s_aValues64[iValue].u64Base;
            pCtx->rcx.u  = 0;
            pCtx->cr4.u |= X86_CR4_FSGSBASE;
            Bs3MemCpy(pExpectCtx, pCtx, sizeof(*pExpectCtx));
            Bs3TrapSetJmpAndRestore(pCtx, pTrapFrame);
            pExpectCtx->rip.u       = pCtx->rip.u + (!fGP ? pFsGsBaseWorker->offVerifyWorkerUd2 : 0);
            pExpectCtx->rbx.u       = !fGP ? 0 : s_aValues64[iValue].u64Base;
            pExpectCtx->rcx.u       = !fGP ? s_aValues64[iValue].u64Base : 0;
            pExpectCtx->rflags.u32 |= X86_EFL_RF;
            if (  !Bs3TestCheckRegCtxEx(&pTrapFrame->Ctx, pExpectCtx, 0 /*cbPcAdjust*/, 0 /*cbSpAdjust*/,
                                        0 /*fExtraEfl*/,    "lm64", 0 /*idTestStep*/)
                || (fGP && pTrapFrame->bXcpt != X86_XCPT_GP))
            {
                if (fGP && pTrapFrame->bXcpt != X86_XCPT_GP)
                    Bs3TestFailedF("Expected #GP, got %#x (%#x)", pTrapFrame->bXcpt, pTrapFrame->uErrCd);
                else
                    Bs3TestFailedF("iValue=%u\n", iValue);
                fPassed = false;
                break;
            }
        }
    }
    else
    {
        for (iValue = 0; iValue < RT_ELEMENTS(s_aValues64); iValue++)
        {
            pCtx->rbx.u  =  s_aValues64[iValue].u64Base;
            pCtx->rcx.u  = ~s_aValues64[iValue].u64Base;
            pCtx->cr4.u |= X86_CR4_FSGSBASE;
            Bs3MemCpy(pExpectCtx, pCtx, sizeof(*pExpectCtx));
            Bs3TrapSetJmpAndRestore(pCtx, pTrapFrame);
            pExpectCtx->rip.u       = pCtx->rip.u + pFsGsBaseWorker->offVerifyWorkerUd2;
            pExpectCtx->rbx.u       = 0;
            pExpectCtx->rcx.u       = s_aValues64[iValue].u64Base & UINT64_C(0x00000000ffffffff);
            pExpectCtx->rflags.u32 |= X86_EFL_RF;
            if (!Bs3TestCheckRegCtxEx(&pTrapFrame->Ctx, pExpectCtx, 0 /*cbPcAdjust*/, 0 /*cbSpAdjust*/,
                                      0 /*fExtraEfl*/, "lm64", 0 /*idTestStep*/))
            {
                Bs3TestFailedF("iValue=%u\n", iValue);
                fPassed = false;
                break;
            }
        }
    }

    *puIter = iValue;
    return fPassed;
}


static void bs3CpuInstr2_rdfsbase_rdgsbase_Common(uint8_t bMode, BS3CI2FSGSBASE const *paFsGsBaseWorkers,
                                                  unsigned cFsGsBaseWorkers, uint32_t idxFsGsBaseMsr)
{
    BS3REGCTX         Ctx;
    BS3REGCTX         ExpectCtx;
    BS3TRAPFRAME      TrapFrame;
    unsigned          iWorker;
    unsigned          iIter;
    uint32_t          uDummy;
    uint32_t          uStdExtFeatEbx;
    bool              fSupportsFsGsBase;

    ASMCpuId_Idx_ECX(7, 0, &uDummy, &uStdExtFeatEbx, &uDummy, &uDummy);
    fSupportsFsGsBase = RT_BOOL(uStdExtFeatEbx & X86_CPUID_STEXT_FEATURE_EBX_FSGSBASE);

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&ExpectCtx, 0, sizeof(ExpectCtx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);

    for (iWorker = 0; iWorker < cFsGsBaseWorkers; iWorker++)
    {
        Bs3RegCtxSetRipCsFromCurPtr(&Ctx, paFsGsBaseWorkers[iWorker].pfnWorker);
        if (fSupportsFsGsBase)
        {
            uint64_t const uBaseAddr = ASMRdMsr(idxFsGsBaseMsr);

            /* CR4.FSGSBASE disabled -> #UD. */
            Ctx.cr4.u &= ~X86_CR4_FSGSBASE;
            bs3CpuInstr2_fsgsbase_ExpectUD(bMode, &Ctx, &ExpectCtx, &TrapFrame);

            /* Read and verify existing base address. */
            Ctx.rbx.u  = 0;
            Ctx.cr4.u |= X86_CR4_FSGSBASE;
            Bs3MemCpy(&ExpectCtx, &Ctx, sizeof(ExpectCtx));
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);
            ExpectCtx.rip.u       = Ctx.rip.u + paFsGsBaseWorkers[iWorker].offWorkerUd2;
            ExpectCtx.rbx.u       = uBaseAddr;
            ExpectCtx.rflags.u32 |= X86_EFL_RF;
            if (!Bs3TestCheckRegCtxEx(&TrapFrame.Ctx, &ExpectCtx, 0 /*cbPcAdjust*/, 0 /*cbSpAdjust*/, 0 /*fExtraEfl*/, "lm64",
                                      0 /*idTestStep*/))
            {
                ASMHalt();
            }

            /* Write, read and verify series of base addresses. */
            if (!bs3CpuInstr2_fsgsbase_VerifyWorker(bMode, &Ctx, &ExpectCtx, &TrapFrame, &paFsGsBaseWorkers[iWorker], &iIter))
            {
                Bs3TestFailedF("^^^ %s: iWorker=%u iIter=%u\n", paFsGsBaseWorkers[iWorker].pszDesc, iWorker, iIter);
                ASMHalt();
            }

            /* Restore original base address. */
            ASMWrMsr(idxFsGsBaseMsr, uBaseAddr);

            /* Clean used GPRs. */
            Ctx.rbx.u = 0;
            Ctx.rcx.u = 0;
        }
        else
        {
            /* Unsupported by CPUID -> #UD. */
            Bs3TestPrintf("Note! FSGSBASE is not supported by the CPU!\n");
            bs3CpuInstr2_fsgsbase_ExpectUD(bMode, &Ctx, &ExpectCtx, &TrapFrame);
        }
    }
}


static void bs3CpuInstr2_wrfsbase_wrgsbase_Common(uint8_t bMode, BS3CI2FSGSBASE const *paFsGsBaseWorkers,
                                                  unsigned cFsGsBaseWorkers, uint32_t idxFsGsBaseMsr)
{
    BS3REGCTX         Ctx;
    BS3REGCTX         ExpectCtx;
    BS3TRAPFRAME      TrapFrame;
    unsigned          iWorker;
    unsigned          iIter;
    uint32_t          uDummy;
    uint32_t          uStdExtFeatEbx;
    bool              fSupportsFsGsBase;

    ASMCpuId_Idx_ECX(7, 0, &uDummy, &uStdExtFeatEbx, &uDummy, &uDummy);
    fSupportsFsGsBase = RT_BOOL(uStdExtFeatEbx & X86_CPUID_STEXT_FEATURE_EBX_FSGSBASE);

    /* Ensure the structures are allocated before we sample the stack pointer. */
    Bs3MemSet(&Ctx, 0, sizeof(Ctx));
    Bs3MemSet(&ExpectCtx, 0, sizeof(ExpectCtx));
    Bs3MemSet(&TrapFrame, 0, sizeof(TrapFrame));

    /*
     * Create test context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);

    for (iWorker = 0; iWorker < cFsGsBaseWorkers; iWorker++)
    {
        Bs3RegCtxSetRipCsFromCurPtr(&Ctx, paFsGsBaseWorkers[iWorker].pfnWorker);
        if (fSupportsFsGsBase)
        {
            uint64_t const uBaseAddr = ASMRdMsr(idxFsGsBaseMsr);

            /* CR4.FSGSBASE disabled -> #UD. */
            Ctx.cr4.u &= ~X86_CR4_FSGSBASE;
            bs3CpuInstr2_fsgsbase_ExpectUD(bMode, &Ctx, &ExpectCtx, &TrapFrame);

            /* Write a base address. */
            Ctx.rbx.u  = 0xa0000;
            Ctx.cr4.u |= X86_CR4_FSGSBASE;
            Bs3MemCpy(&ExpectCtx, &Ctx, sizeof(ExpectCtx));
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapFrame);
            ExpectCtx.rip.u       = Ctx.rip.u + paFsGsBaseWorkers[iWorker].offWorkerUd2;
            ExpectCtx.rflags.u32 |= X86_EFL_RF;
            if (!Bs3TestCheckRegCtxEx(&TrapFrame.Ctx, &ExpectCtx, 0 /*cbPcAdjust*/, 0 /*cbSpAdjust*/, 0 /*fExtraEfl*/, "lm64",
                                      0 /*idTestStep*/))
            {
                ASMHalt();
            }

            /* Write and read back series of base addresses. */
            if (!bs3CpuInstr2_fsgsbase_VerifyWorker(bMode, &Ctx, &ExpectCtx, &TrapFrame, &paFsGsBaseWorkers[iWorker], &iIter))
            {
                Bs3TestFailedF("^^^ %s: iWorker=%u iIter=%u\n", paFsGsBaseWorkers[iWorker].pszDesc, iWorker, iIter);
                ASMHalt();
            }

            /* Restore original base address. */
            ASMWrMsr(idxFsGsBaseMsr, uBaseAddr);

            /* Clean used GPRs. */
            Ctx.rbx.u = 0;
            Ctx.rcx.u = 0;
        }
        else
        {
            /* Unsupported by CPUID -> #UD. */
            Bs3TestPrintf("Note! FSGSBASE is not supported by the CPU!\n");
            bs3CpuInstr2_fsgsbase_ExpectUD(bMode, &Ctx, &ExpectCtx, &TrapFrame);
        }
    }
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_wrfsbase)(uint8_t bMode)
{
    bs3CpuInstr2_wrfsbase_wrgsbase_Common(bMode, s_aWrFsBaseWorkers, RT_ELEMENTS(s_aWrFsBaseWorkers), MSR_K8_FS_BASE);
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_wrgsbase)(uint8_t bMode)
{
    bs3CpuInstr2_wrfsbase_wrgsbase_Common(bMode, s_aWrGsBaseWorkers, RT_ELEMENTS(s_aWrGsBaseWorkers), MSR_K8_GS_BASE);
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_rdfsbase)(uint8_t bMode)
{
    bs3CpuInstr2_rdfsbase_rdgsbase_Common(bMode, s_aRdFsBaseWorkers, RT_ELEMENTS(s_aRdFsBaseWorkers), MSR_K8_FS_BASE);
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuInstr2_rdgsbase)(uint8_t bMode)
{
    bs3CpuInstr2_rdfsbase_rdgsbase_Common(bMode, s_aRdGsBaseWorkers, RT_ELEMENTS(s_aRdGsBaseWorkers), MSR_K8_GS_BASE);
    return 0;
}

# endif /* ARCH_BITS == 64 */

#endif /* BS3_INSTANTIATING_CMN */



/*
 * Mode specific code.
 * Mode specific code.
 * Mode specific code.
 */
#ifdef BS3_INSTANTIATING_MODE


#endif /* BS3_INSTANTIATING_MODE */

