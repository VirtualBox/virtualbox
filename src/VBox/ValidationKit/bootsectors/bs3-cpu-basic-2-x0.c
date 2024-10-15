/* $Id: bs3-cpu-basic-2-x0.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - bs3-cpu-basic-2, C test driver code (16-bit).
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
#define BS3_USE_X0_TEXT_SEG
#include <bs3kit.h>
#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/asm-mem.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#undef  CHECK_MEMBER
#define CHECK_MEMBER(a_szName, a_szFmt, a_Actual, a_Expected) \
    do \
    { \
        if ((a_Actual) == (a_Expected)) { /* likely */ } \
        else bs3CpuBasic2_FailedF(a_szName "=" a_szFmt " expected " a_szFmt, (a_Actual), (a_Expected)); \
    } while (0)


/** Indicating that we've got operand size prefix and that it matters. */
#define BS3CB2SIDTSGDT_F_OPSIZE    UINT8_C(0x01)
/** Worker requires 386 or later. */
#define BS3CB2SIDTSGDT_F_386PLUS   UINT8_C(0x02)


/** @name MYOP_XXX - Values for FNBS3CPUBASIC2ACTSTCODE::fOp.
 *
 * These are flags, though we've precombined a few shortening things down.
 *
 * @{ */
#define MYOP_LD           0x1       /**< The instruction loads. */
#define MYOP_ST           0x2       /**< The instruction stores */
#define MYOP_EFL          0x4       /**< The instruction modifies EFLAGS. */
#define MYOP_AC_GP        0x8       /**< The instruction may cause either \#AC or \#GP (FXSAVE). */

#define MYOP_LD_ST        0x3       /**< Convenience: The instruction both loads and stores. */
#define MYOP_LD_DIV       0x5       /**< Convenience: DIV instruction - loading and modifying flags. */
/** @} */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Near void pointer. */
typedef void BS3_NEAR *NPVOID;

typedef struct BS3CB2INVLDESCTYPE
{
    uint8_t u4Type;
    uint8_t u1DescType;
} BS3CB2INVLDESCTYPE;

typedef struct BS3CB2SIDTSGDT
{
    const char *pszDesc;
    FPFNBS3FAR  fpfnWorker;
    uint8_t     cbInstr;
    bool        fSs;
    uint8_t     bMode;
    uint8_t     fFlags;
} BS3CB2SIDTSGDT;


typedef void BS3_CALL FNBS3CPUBASIC2ACSNIPPET(void);

typedef struct FNBS3CPUBASIC2ACTSTCODE
{
    FNBS3CPUBASIC2ACSNIPPET BS3_FAR    *pfn;
    uint8_t                             fOp;
    uint16_t                            cbMem;
    uint8_t                             cbAlign;
    uint8_t                             offFaultInstr; /**< For skipping fninit with the fld test. */
} FNBS3CPUBASIC2ACTSTCODE;
typedef FNBS3CPUBASIC2ACTSTCODE const *PCFNBS3CPUBASIC2ACTSTCODE;

typedef struct BS3CPUBASIC2ACTTSTCMNMODE
{
    uint8_t                     bMode;
    uint16_t                    cEntries;
    PCFNBS3CPUBASIC2ACTSTCODE   paEntries;
} BS3CPUBASIC2PFTTSTCMNMODE;
typedef BS3CPUBASIC2PFTTSTCMNMODE const *PCBS3CPUBASIC2PFTTSTCMNMODE;


/*********************************************************************************************************************************
*   External Symbols                                                                                                             *
*********************************************************************************************************************************/
extern FNBS3FAR     bs3CpuBasic2_Int80;
extern FNBS3FAR     bs3CpuBasic2_Int81;
extern FNBS3FAR     bs3CpuBasic2_Int82;
extern FNBS3FAR     bs3CpuBasic2_Int83;

extern FNBS3FAR     bs3CpuBasic2_ud2;
#define             g_bs3CpuBasic2_ud2_FlatAddr BS3_DATA_NM(g_bs3CpuBasic2_ud2_FlatAddr)
extern uint32_t     g_bs3CpuBasic2_ud2_FlatAddr;

extern FNBS3FAR     bs3CpuBasic2_salc_ud2;
extern FNBS3FAR     bs3CpuBasic2_swapgs;

extern FNBS3FAR     bs3CpuBasic2_iret;
extern FNBS3FAR     bs3CpuBasic2_iret_opsize;
extern FNBS3FAR     bs3CpuBasic2_iret_rexw;

extern FNBS3FAR     bs3CpuBasic2_sidt_bx_ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_sidt_bx_ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_sidt_bx_ud2_c64;
extern FNBS3FAR     bs3CpuBasic2_sidt_ss_bx_ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_sidt_ss_bx_ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_sidt_rexw_bx_ud2_c64;
extern FNBS3FAR     bs3CpuBasic2_sidt_opsize_bx_ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_sidt_opsize_bx_ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_sidt_opsize_bx_ud2_c64;
extern FNBS3FAR     bs3CpuBasic2_sidt_opsize_ss_bx_ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_sidt_opsize_ss_bx_ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_sidt_opsize_rexw_bx_ud2_c64;

extern FNBS3FAR     bs3CpuBasic2_sgdt_bx_ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_sgdt_bx_ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_sgdt_bx_ud2_c64;
extern FNBS3FAR     bs3CpuBasic2_sgdt_ss_bx_ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_sgdt_ss_bx_ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_sgdt_rexw_bx_ud2_c64;
extern FNBS3FAR     bs3CpuBasic2_sgdt_opsize_bx_ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_sgdt_opsize_bx_ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_sgdt_opsize_bx_ud2_c64;
extern FNBS3FAR     bs3CpuBasic2_sgdt_opsize_ss_bx_ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_sgdt_opsize_ss_bx_ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_sgdt_opsize_rexw_bx_ud2_c64;

extern FNBS3FAR     bs3CpuBasic2_lidt_bx__sidt_es_di__lidt_es_si__ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_lidt_bx__sidt_es_di__lidt_es_si__ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_lidt_bx__sidt_es_di__lidt_es_si__ud2_c64;
extern FNBS3FAR     bs3CpuBasic2_lidt_ss_bx__sidt_es_di__lidt_es_si__ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_lidt_ss_bx__sidt_es_di__lidt_es_si__ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_lidt_rexw_bx__sidt_es_di__lidt_es_si__ud2_c64;
extern FNBS3FAR     bs3CpuBasic2_lidt_opsize_bx__sidt_es_di__lidt_es_si__ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_lidt_opsize_bx__sidt32_es_di__lidt_es_si__ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_lidt_opsize_bx__sidt_es_di__lidt_es_si__ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_lidt_opsize_bx__sidt_es_di__lidt_es_si__ud2_c64;
extern FNBS3FAR     bs3CpuBasic2_lidt_opsize_ss_bx__sidt_es_di__lidt_es_si__ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_lidt_opsize_ss_bx__sidt_es_di__lidt_es_si__ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_lidt_opsize_rexw_bx__sidt_es_di__lidt_es_si__ud2_c64;

extern FNBS3FAR     bs3CpuBasic2_lgdt_bx__sgdt_es_di__lgdt_es_si__ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_lgdt_bx__sgdt_es_di__lgdt_es_si__ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_lgdt_bx__sgdt_es_di__lgdt_es_si__ud2_c64;
extern FNBS3FAR     bs3CpuBasic2_lgdt_ss_bx__sgdt_es_di__lgdt_es_si__ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_lgdt_ss_bx__sgdt_es_di__lgdt_es_si__ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_lgdt_rexw_bx__sgdt_es_di__lgdt_es_si__ud2_c64;
extern FNBS3FAR     bs3CpuBasic2_lgdt_opsize_bx__sgdt_es_di__lgdt_es_si__ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_lgdt_opsize_bx__sgdt_es_di__lgdt_es_si__ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_lgdt_opsize_bx__sgdt_es_di__lgdt_es_si__ud2_c64;
extern FNBS3FAR     bs3CpuBasic2_lgdt_opsize_ss_bx__sgdt_es_di__lgdt_es_si__ud2_c16;
extern FNBS3FAR     bs3CpuBasic2_lgdt_opsize_ss_bx__sgdt_es_di__lgdt_es_si__ud2_c32;
extern FNBS3FAR     bs3CpuBasic2_lgdt_opsize_rexw_bx__sgdt_es_di__lgdt_es_si__ud2_c64;


/* bs3-cpu-basic-2-template.mac: */
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_mov_ax_ds_bx__ud2_c16;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_mov_ds_bx_ax__ud2_c16;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_xchg_ds_bx_ax__ud2_c16;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_cmpxchg_ds_bx_cx__ud2_c16;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_div_ds_bx__ud2_c16;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_fninit_fld_ds_bx__ud2_c16;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_fninit_fbld_ds_bx__ud2_c16;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_fninit_fldz_fstp_ds_bx__ud2_c16;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_fxsave_ds_bx__ud2_c16;

FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_mov_ax_ds_bx__ud2_c32;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_mov_ds_bx_ax__ud2_c32;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_xchg_ds_bx_ax__ud2_c32;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_cmpxchg_ds_bx_cx__ud2_c32;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_div_ds_bx__ud2_c32;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_fninit_fld_ds_bx__ud2_c32;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_fninit_fbld_ds_bx__ud2_c32;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_fninit_fldz_fstp_ds_bx__ud2_c32;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_fxsave_ds_bx__ud2_c32;

FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_mov_ax_ds_bx__ud2_c64;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_mov_ds_bx_ax__ud2_c64;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_xchg_ds_bx_ax__ud2_c64;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_cmpxchg_ds_bx_cx__ud2_c64;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_div_ds_bx__ud2_c64;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_fninit_fld_ds_bx__ud2_c64;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_fninit_fbld_ds_bx__ud2_c64;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_fninit_fldz_fstp_ds_bx__ud2_c64;
FNBS3CPUBASIC2ACSNIPPET bs3CpuBasic2_fxsave_ds_bx__ud2_c64;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char BS3_FAR  *g_pszTestMode = (const char *)1;
static uint8_t              g_bTestMode = 1;
static bool                 g_f16BitSys = 1;


/** SIDT test workers. */
static BS3CB2SIDTSGDT const g_aSidtWorkers[] =
{
    { "sidt [bx]",              bs3CpuBasic2_sidt_bx_ud2_c16,             3, false,   BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, 0 },
    { "sidt [ss:bx]",           bs3CpuBasic2_sidt_ss_bx_ud2_c16,          4, true,    BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, 0 },
    { "o32 sidt [bx]",          bs3CpuBasic2_sidt_opsize_bx_ud2_c16,      4, false,   BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, BS3CB2SIDTSGDT_F_386PLUS },
    { "o32 sidt [ss:bx]",       bs3CpuBasic2_sidt_opsize_ss_bx_ud2_c16,   5, true,    BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, BS3CB2SIDTSGDT_F_386PLUS },
    { "sidt [ebx]",             bs3CpuBasic2_sidt_bx_ud2_c32,             3, false,   BS3_MODE_CODE_32, 0 },
    { "sidt [ss:ebx]",          bs3CpuBasic2_sidt_ss_bx_ud2_c32,          4, true,    BS3_MODE_CODE_32, 0 },
    { "o16 sidt [ebx]",         bs3CpuBasic2_sidt_opsize_bx_ud2_c32,      4, false,   BS3_MODE_CODE_32, 0 },
    { "o16 sidt [ss:ebx]",      bs3CpuBasic2_sidt_opsize_ss_bx_ud2_c32,   5, true,    BS3_MODE_CODE_32, 0 },
    { "sidt [rbx]",             bs3CpuBasic2_sidt_bx_ud2_c64,             3, false,   BS3_MODE_CODE_64, 0 },
    { "o64 sidt [rbx]",         bs3CpuBasic2_sidt_rexw_bx_ud2_c64,        4, false,   BS3_MODE_CODE_64, 0 },
    { "o32 sidt [rbx]",         bs3CpuBasic2_sidt_opsize_bx_ud2_c64,      4, false,   BS3_MODE_CODE_64, 0 },
    { "o32 o64 sidt [rbx]",     bs3CpuBasic2_sidt_opsize_rexw_bx_ud2_c64, 5, false,   BS3_MODE_CODE_64, 0 },
};

/** SGDT test workers. */
static BS3CB2SIDTSGDT const g_aSgdtWorkers[] =
{
    { "sgdt [bx]",              bs3CpuBasic2_sgdt_bx_ud2_c16,             3, false,   BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, 0 },
    { "sgdt [ss:bx]",           bs3CpuBasic2_sgdt_ss_bx_ud2_c16,          4, true,    BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, 0 },
    { "o32 sgdt [bx]",          bs3CpuBasic2_sgdt_opsize_bx_ud2_c16,      4, false,   BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, BS3CB2SIDTSGDT_F_386PLUS },
    { "o32 sgdt [ss:bx]",       bs3CpuBasic2_sgdt_opsize_ss_bx_ud2_c16,   5, true,    BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, BS3CB2SIDTSGDT_F_386PLUS },
    { "sgdt [ebx]",             bs3CpuBasic2_sgdt_bx_ud2_c32,             3, false,   BS3_MODE_CODE_32, 0 },
    { "sgdt [ss:ebx]",          bs3CpuBasic2_sgdt_ss_bx_ud2_c32,          4, true,    BS3_MODE_CODE_32, 0 },
    { "o16 sgdt [ebx]",         bs3CpuBasic2_sgdt_opsize_bx_ud2_c32,      4, false,   BS3_MODE_CODE_32, 0 },
    { "o16 sgdt [ss:ebx]",      bs3CpuBasic2_sgdt_opsize_ss_bx_ud2_c32,   5, true,    BS3_MODE_CODE_32, 0 },
    { "sgdt [rbx]",             bs3CpuBasic2_sgdt_bx_ud2_c64,             3, false,   BS3_MODE_CODE_64, 0 },
    { "o64 sgdt [rbx]",         bs3CpuBasic2_sgdt_rexw_bx_ud2_c64,        4, false,   BS3_MODE_CODE_64, 0 },
    { "o32 sgdt [rbx]",         bs3CpuBasic2_sgdt_opsize_bx_ud2_c64,      4, false,   BS3_MODE_CODE_64, 0 },
    { "o32 o64 sgdt [rbx]",     bs3CpuBasic2_sgdt_opsize_rexw_bx_ud2_c64, 5, false,   BS3_MODE_CODE_64, 0 },
};

/** LIDT test workers. */
static BS3CB2SIDTSGDT const g_aLidtWorkers[] =
{
    { "lidt [bx]",              bs3CpuBasic2_lidt_bx__sidt_es_di__lidt_es_si__ud2_c16,             11, false,   BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, 0 },
    { "lidt [ss:bx]",           bs3CpuBasic2_lidt_ss_bx__sidt_es_di__lidt_es_si__ud2_c16,          12, true,    BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, 0 },
    { "o32 lidt [bx]",          bs3CpuBasic2_lidt_opsize_bx__sidt_es_di__lidt_es_si__ud2_c16,      12, false,   BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, BS3CB2SIDTSGDT_F_OPSIZE | BS3CB2SIDTSGDT_F_386PLUS },
    { "o32 lidt [bx]; sidt32",  bs3CpuBasic2_lidt_opsize_bx__sidt32_es_di__lidt_es_si__ud2_c16,    27, false,   BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, BS3CB2SIDTSGDT_F_OPSIZE | BS3CB2SIDTSGDT_F_386PLUS },
    { "o32 lidt [ss:bx]",       bs3CpuBasic2_lidt_opsize_ss_bx__sidt_es_di__lidt_es_si__ud2_c16,   13, true,    BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, BS3CB2SIDTSGDT_F_OPSIZE | BS3CB2SIDTSGDT_F_386PLUS },
    { "lidt [ebx]",             bs3CpuBasic2_lidt_bx__sidt_es_di__lidt_es_si__ud2_c32,             11, false,   BS3_MODE_CODE_32, 0 },
    { "lidt [ss:ebx]",          bs3CpuBasic2_lidt_ss_bx__sidt_es_di__lidt_es_si__ud2_c32,          12, true,    BS3_MODE_CODE_32, 0 },
    { "o16 lidt [ebx]",         bs3CpuBasic2_lidt_opsize_bx__sidt_es_di__lidt_es_si__ud2_c32,      12, false,   BS3_MODE_CODE_32, BS3CB2SIDTSGDT_F_OPSIZE },
    { "o16 lidt [ss:ebx]",      bs3CpuBasic2_lidt_opsize_ss_bx__sidt_es_di__lidt_es_si__ud2_c32,   13, true,    BS3_MODE_CODE_32, BS3CB2SIDTSGDT_F_OPSIZE },
    { "lidt [rbx]",             bs3CpuBasic2_lidt_bx__sidt_es_di__lidt_es_si__ud2_c64,              9, false,   BS3_MODE_CODE_64, 0 },
    { "o64 lidt [rbx]",         bs3CpuBasic2_lidt_rexw_bx__sidt_es_di__lidt_es_si__ud2_c64,        10, false,   BS3_MODE_CODE_64, 0 },
    { "o32 lidt [rbx]",         bs3CpuBasic2_lidt_opsize_bx__sidt_es_di__lidt_es_si__ud2_c64,      10, false,   BS3_MODE_CODE_64, 0 },
    { "o32 o64 lidt [rbx]",     bs3CpuBasic2_lidt_opsize_rexw_bx__sidt_es_di__lidt_es_si__ud2_c64, 11, false,   BS3_MODE_CODE_64, 0 },
};

/** LGDT test workers. */
static BS3CB2SIDTSGDT const g_aLgdtWorkers[] =
{
    { "lgdt [bx]",              bs3CpuBasic2_lgdt_bx__sgdt_es_di__lgdt_es_si__ud2_c16,             11, false,   BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, 0 },
    { "lgdt [ss:bx]",           bs3CpuBasic2_lgdt_ss_bx__sgdt_es_di__lgdt_es_si__ud2_c16,          12, true,    BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, 0 },
    { "o32 lgdt [bx]",          bs3CpuBasic2_lgdt_opsize_bx__sgdt_es_di__lgdt_es_si__ud2_c16,      12, false,   BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, BS3CB2SIDTSGDT_F_OPSIZE | BS3CB2SIDTSGDT_F_386PLUS },
    { "o32 lgdt [ss:bx]",       bs3CpuBasic2_lgdt_opsize_ss_bx__sgdt_es_di__lgdt_es_si__ud2_c16,   13, true,    BS3_MODE_CODE_16 | BS3_MODE_CODE_V86, BS3CB2SIDTSGDT_F_OPSIZE | BS3CB2SIDTSGDT_F_386PLUS },
    { "lgdt [ebx]",             bs3CpuBasic2_lgdt_bx__sgdt_es_di__lgdt_es_si__ud2_c32,             11, false,   BS3_MODE_CODE_32, 0 },
    { "lgdt [ss:ebx]",          bs3CpuBasic2_lgdt_ss_bx__sgdt_es_di__lgdt_es_si__ud2_c32,          12, true,    BS3_MODE_CODE_32, 0 },
    { "o16 lgdt [ebx]",         bs3CpuBasic2_lgdt_opsize_bx__sgdt_es_di__lgdt_es_si__ud2_c32,      12, false,   BS3_MODE_CODE_32, BS3CB2SIDTSGDT_F_OPSIZE },
    { "o16 lgdt [ss:ebx]",      bs3CpuBasic2_lgdt_opsize_ss_bx__sgdt_es_di__lgdt_es_si__ud2_c32,   13, true,    BS3_MODE_CODE_32, BS3CB2SIDTSGDT_F_OPSIZE },
    { "lgdt [rbx]",             bs3CpuBasic2_lgdt_bx__sgdt_es_di__lgdt_es_si__ud2_c64,              9, false,   BS3_MODE_CODE_64, 0 },
    { "o64 lgdt [rbx]",         bs3CpuBasic2_lgdt_rexw_bx__sgdt_es_di__lgdt_es_si__ud2_c64,        10, false,   BS3_MODE_CODE_64, 0 },
    { "o32 lgdt [rbx]",         bs3CpuBasic2_lgdt_opsize_bx__sgdt_es_di__lgdt_es_si__ud2_c64,      10, false,   BS3_MODE_CODE_64, 0 },
    { "o32 o64 lgdt [rbx]",     bs3CpuBasic2_lgdt_opsize_rexw_bx__sgdt_es_di__lgdt_es_si__ud2_c64, 11, false,   BS3_MODE_CODE_64, 0 },
};



#if 0
/** Table containing invalid CS selector types. */
static const BS3CB2INVLDESCTYPE g_aInvalidCsTypes[] =
{
    {   X86_SEL_TYPE_RO,            1 },
    {   X86_SEL_TYPE_RO_ACC,        1 },
    {   X86_SEL_TYPE_RW,            1 },
    {   X86_SEL_TYPE_RW_ACC,        1 },
    {   X86_SEL_TYPE_RO_DOWN,       1 },
    {   X86_SEL_TYPE_RO_DOWN_ACC,   1 },
    {   X86_SEL_TYPE_RW_DOWN,       1 },
    {   X86_SEL_TYPE_RW_DOWN_ACC,   1 },
    {   0,                          0 },
    {   1,                          0 },
    {   2,                          0 },
    {   3,                          0 },
    {   4,                          0 },
    {   5,                          0 },
    {   6,                          0 },
    {   7,                          0 },
    {   8,                          0 },
    {   9,                          0 },
    {   10,                         0 },
    {   11,                         0 },
    {   12,                         0 },
    {   13,                         0 },
    {   14,                         0 },
    {   15,                         0 },
};

/** Table containing invalid SS selector types. */
static const BS3CB2INVLDESCTYPE g_aInvalidSsTypes[] =
{
    {   X86_SEL_TYPE_EO,            1 },
    {   X86_SEL_TYPE_EO_ACC,        1 },
    {   X86_SEL_TYPE_ER,            1 },
    {   X86_SEL_TYPE_ER_ACC,        1 },
    {   X86_SEL_TYPE_EO_CONF,       1 },
    {   X86_SEL_TYPE_EO_CONF_ACC,   1 },
    {   X86_SEL_TYPE_ER_CONF,       1 },
    {   X86_SEL_TYPE_ER_CONF_ACC,   1 },
    {   0,                          0 },
    {   1,                          0 },
    {   2,                          0 },
    {   3,                          0 },
    {   4,                          0 },
    {   5,                          0 },
    {   6,                          0 },
    {   7,                          0 },
    {   8,                          0 },
    {   9,                          0 },
    {   10,                         0 },
    {   11,                         0 },
    {   12,                         0 },
    {   13,                         0 },
    {   14,                         0 },
    {   15,                         0 },
};
#endif


static const FNBS3CPUBASIC2ACTSTCODE g_aCmn16[] =
{
    {   bs3CpuBasic2_mov_ax_ds_bx__ud2_c16,             MYOP_LD,                2,  2 },
    {   bs3CpuBasic2_mov_ds_bx_ax__ud2_c16,             MYOP_ST,                2,  2 },
    {   bs3CpuBasic2_xchg_ds_bx_ax__ud2_c16,            MYOP_LD_ST,             2,  2 },
    {   bs3CpuBasic2_cmpxchg_ds_bx_cx__ud2_c16,         MYOP_LD_ST | MYOP_EFL,  2,  2 },
    {   bs3CpuBasic2_div_ds_bx__ud2_c16,                MYOP_LD_DIV,            2,  2 },
    {   bs3CpuBasic2_fninit_fld_ds_bx__ud2_c16,         MYOP_LD,               10,  8, 2 /*fninit*/ },
    {   bs3CpuBasic2_fninit_fbld_ds_bx__ud2_c16,        MYOP_LD,               10,  8, 2 /*fninit*/ },
    {   bs3CpuBasic2_fninit_fldz_fstp_ds_bx__ud2_c16,   MYOP_ST,               10,  8, 4 /*fninit+fldz*/ },
    {   bs3CpuBasic2_fxsave_ds_bx__ud2_c16,             MYOP_ST | MYOP_AC_GP, 512, 16 },
};

static const FNBS3CPUBASIC2ACTSTCODE g_aCmn32[] =
{
    {   bs3CpuBasic2_mov_ax_ds_bx__ud2_c32,             MYOP_LD,                4,  4 },
    {   bs3CpuBasic2_mov_ds_bx_ax__ud2_c32,             MYOP_ST,                4,  4 },
    {   bs3CpuBasic2_xchg_ds_bx_ax__ud2_c32,            MYOP_LD_ST,             4,  4 },
    {   bs3CpuBasic2_cmpxchg_ds_bx_cx__ud2_c32,         MYOP_LD_ST | MYOP_EFL,  4,  4 },
    {   bs3CpuBasic2_div_ds_bx__ud2_c32,                MYOP_LD_DIV,            4,  4 },
    {   bs3CpuBasic2_fninit_fld_ds_bx__ud2_c32,         MYOP_LD,               10,  8, 2 /*fninit*/ },
    {   bs3CpuBasic2_fninit_fbld_ds_bx__ud2_c32,        MYOP_LD,               10,  8, 2 /*fninit*/ },
    {   bs3CpuBasic2_fninit_fldz_fstp_ds_bx__ud2_c32,   MYOP_ST,               10,  8, 4 /*fninit+fldz*/ },
    {   bs3CpuBasic2_fxsave_ds_bx__ud2_c32,             MYOP_ST | MYOP_AC_GP, 512, 16 },
};

static const FNBS3CPUBASIC2ACTSTCODE g_aCmn64[] =
{
    {   bs3CpuBasic2_mov_ax_ds_bx__ud2_c64,             MYOP_LD,                8,  8 },
    {   bs3CpuBasic2_mov_ds_bx_ax__ud2_c64,             MYOP_ST,                8,  8 },
    {   bs3CpuBasic2_xchg_ds_bx_ax__ud2_c64,            MYOP_LD_ST,             8,  8 },
    {   bs3CpuBasic2_cmpxchg_ds_bx_cx__ud2_c64,         MYOP_LD_ST | MYOP_EFL,  8,  8 },
    {   bs3CpuBasic2_div_ds_bx__ud2_c64,                MYOP_LD_DIV,            8,  8 },
    {   bs3CpuBasic2_fninit_fld_ds_bx__ud2_c64,         MYOP_LD,               10,  8, 2 /*fninit*/ },
    {   bs3CpuBasic2_fninit_fbld_ds_bx__ud2_c64,        MYOP_LD,               10,  8, 2 /*fninit*/ },
    {   bs3CpuBasic2_fninit_fldz_fstp_ds_bx__ud2_c64,   MYOP_ST,               10,  8, 4 /*fninit+fldz*/ },
    {   bs3CpuBasic2_fxsave_ds_bx__ud2_c64,             MYOP_ST | MYOP_AC_GP, 512, 16 },
};

static const BS3CPUBASIC2PFTTSTCMNMODE g_aCmnModes[] =
{
    {   BS3_MODE_CODE_16,  RT_ELEMENTS(g_aCmn16), g_aCmn16 },
    {   BS3_MODE_CODE_V86, RT_ELEMENTS(g_aCmn16), g_aCmn16 },
    {   BS3_MODE_CODE_32,  RT_ELEMENTS(g_aCmn32), g_aCmn32 },
    {   BS3_MODE_CODE_64,  RT_ELEMENTS(g_aCmn64), g_aCmn64 },
};


/**
 * Sets globals according to the mode.
 *
 * @param   bTestMode   The test mode.
 */
static void bs3CpuBasic2_SetGlobals(uint8_t bTestMode)
{
    g_bTestMode     = bTestMode;
    g_pszTestMode   = Bs3GetModeName(bTestMode);
    g_f16BitSys     = BS3_MODE_IS_16BIT_SYS(bTestMode);
    g_usBs3TestStep = 0;
}


uint32_t ASMGetESP(void);
#pragma aux ASMGetESP = \
    ".386" \
    "mov ax, sp" \
    "mov edx, esp" \
    "shr edx, 16" \
    value [ax dx] \
    modify exact [ax dx];


/**
 * Wrapper around Bs3TestFailedF that prefixes the error with g_usBs3TestStep
 * and g_pszTestMode.
 */
static void bs3CpuBasic2_FailedF(const char *pszFormat, ...)
{
    va_list va;

    char szTmp[168];
    va_start(va, pszFormat);
    Bs3StrPrintfV(szTmp, sizeof(szTmp), pszFormat, va);
    va_end(va);

    Bs3TestFailedF("%u - %s: %s", g_usBs3TestStep, g_pszTestMode, szTmp);
}


#if 0
/**
 * Compares trap stuff.
 */
static void bs3CpuBasic2_CompareIntCtx1(PCBS3TRAPFRAME pTrapCtx, PCBS3REGCTX pStartCtx, uint8_t bXcpt)
{
    uint16_t const cErrorsBefore = Bs3TestSubErrorCount();
    CHECK_MEMBER("bXcpt",   "%#04x",    pTrapCtx->bXcpt,        bXcpt);
    CHECK_MEMBER("bErrCd",  "%#06RX64", pTrapCtx->uErrCd,       0);
    Bs3TestCheckRegCtxEx(&pTrapCtx->Ctx, pStartCtx, 2 /*int xx*/, 0 /*cbSpAdjust*/, 0 /*fExtraEfl*/, g_pszTestMode, g_usBs3TestStep);
    if (Bs3TestSubErrorCount() != cErrorsBefore)
    {
        Bs3TrapPrintFrame(pTrapCtx);
#if 1
        Bs3TestPrintf("Halting: g_uBs3CpuDetected=%#x\n", g_uBs3CpuDetected);
        Bs3TestPrintf("Halting in CompareTrapCtx1: bXcpt=%#x\n", bXcpt);
        ASMHalt();
#endif
    }
}
#endif


#if 0
/**
 * Compares trap stuff.
 */
static void bs3CpuBasic2_CompareTrapCtx2(PCBS3TRAPFRAME pTrapCtx, PCBS3REGCTX pStartCtx, uint16_t cbIpAdjust,
                                         uint8_t bXcpt, uint16_t uHandlerCs)
{
    uint16_t const cErrorsBefore = Bs3TestSubErrorCount();
    CHECK_MEMBER("bXcpt",   "%#04x",    pTrapCtx->bXcpt,        bXcpt);
    CHECK_MEMBER("bErrCd",  "%#06RX64", pTrapCtx->uErrCd,       0);
    CHECK_MEMBER("uHandlerCs", "%#06x", pTrapCtx->uHandlerCs,   uHandlerCs);
    Bs3TestCheckRegCtxEx(&pTrapCtx->Ctx, pStartCtx, cbIpAdjust, 0 /*cbSpAdjust*/, 0 /*fExtraEfl*/, g_pszTestMode, g_usBs3TestStep);
    if (Bs3TestSubErrorCount() != cErrorsBefore)
    {
        Bs3TrapPrintFrame(pTrapCtx);
#if 1
        Bs3TestPrintf("Halting: g_uBs3CpuDetected=%#x\n", g_uBs3CpuDetected);
        Bs3TestPrintf("Halting in CompareTrapCtx2: bXcpt=%#x\n", bXcpt);
        ASMHalt();
#endif
    }
}
#endif

/**
 * Compares a CPU trap.
 */
static void bs3CpuBasic2_CompareCpuTrapCtx(PCBS3TRAPFRAME pTrapCtx, PCBS3REGCTX pStartCtx, uint16_t uErrCd,
                                           uint8_t bXcpt, bool f486ResumeFlagHint, uint8_t cbIpAdjust)
{
    uint16_t const cErrorsBefore = Bs3TestSubErrorCount();
    uint32_t fExtraEfl;

    CHECK_MEMBER("bXcpt",   "%#04x",    pTrapCtx->bXcpt,        bXcpt);
    CHECK_MEMBER("bErrCd",  "%#06RX16", (uint16_t)pTrapCtx->uErrCd, (uint16_t)uErrCd); /* 486 only writes a word */

    if (   g_f16BitSys
        || bXcpt == X86_XCPT_DB /* hack (10980xe)... */
        || (   !f486ResumeFlagHint
            && (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) <= BS3CPU_80486 ) )
        fExtraEfl = 0;
    else
        fExtraEfl = X86_EFL_RF;
#if 0 /** @todo Running on an AMD Phenom II X6 1100T under AMD-V I'm not getting good X86_EFL_RF results.  Enable this to get on with other work.  */
    fExtraEfl = pTrapCtx->Ctx.rflags.u32 & X86_EFL_RF;
#endif
    Bs3TestCheckRegCtxEx(&pTrapCtx->Ctx, pStartCtx, cbIpAdjust, 0 /*cbSpAdjust*/, fExtraEfl, g_pszTestMode, g_usBs3TestStep);
    if (Bs3TestSubErrorCount() != cErrorsBefore)
    {
        Bs3TrapPrintFrame(pTrapCtx);
#if 1
        Bs3TestPrintf("Halting: g_uBs3CpuDetected=%#x\n", g_uBs3CpuDetected);
        Bs3TestPrintf("Halting: bXcpt=%#x uErrCd=%#x\n", bXcpt, uErrCd);
        ASMHalt();
#endif
    }
}


/**
 * Compares \#GP trap.
 */
static void bs3CpuBasic2_CompareGpCtx(PCBS3TRAPFRAME pTrapCtx, PCBS3REGCTX pStartCtx, uint16_t uErrCd)
{
    bs3CpuBasic2_CompareCpuTrapCtx(pTrapCtx, pStartCtx, uErrCd, X86_XCPT_GP, true /*f486ResumeFlagHint*/, 0 /*cbIpAdjust*/);
}

#if 0
/**
 * Compares \#NP trap.
 */
static void bs3CpuBasic2_CompareNpCtx(PCBS3TRAPFRAME pTrapCtx, PCBS3REGCTX pStartCtx, uint16_t uErrCd)
{
    bs3CpuBasic2_CompareCpuTrapCtx(pTrapCtx, pStartCtx, uErrCd, X86_XCPT_NP, true /*f486ResumeFlagHint*/, 0 /*cbIpAdjust*/);
}
#endif

/**
 * Compares \#SS trap.
 */
static void bs3CpuBasic2_CompareSsCtx(PCBS3TRAPFRAME pTrapCtx, PCBS3REGCTX pStartCtx, uint16_t uErrCd, bool f486ResumeFlagHint)
{
    bs3CpuBasic2_CompareCpuTrapCtx(pTrapCtx, pStartCtx, uErrCd, X86_XCPT_SS, f486ResumeFlagHint, 0 /*cbIpAdjust*/);
}

#if 0
/**
 * Compares \#TS trap.
 */
static void bs3CpuBasic2_CompareTsCtx(PCBS3TRAPFRAME pTrapCtx, PCBS3REGCTX pStartCtx, uint16_t uErrCd)
{
    bs3CpuBasic2_CompareCpuTrapCtx(pTrapCtx, pStartCtx, uErrCd, X86_XCPT_TS, false /*f486ResumeFlagHint*/, 0 /*cbIpAdjust*/);
}
#endif

/**
 * Compares \#PF trap.
 */
static void bs3CpuBasic2_ComparePfCtx(PCBS3TRAPFRAME pTrapCtx, PBS3REGCTX pStartCtx, uint16_t uErrCd,
                                      uint64_t uCr2Expected, uint8_t cbIpAdjust)
{
    uint64_t const uCr2Saved     = pStartCtx->cr2.u;
    pStartCtx->cr2.u = uCr2Expected;
    bs3CpuBasic2_CompareCpuTrapCtx(pTrapCtx, pStartCtx, uErrCd, X86_XCPT_PF, true /*f486ResumeFlagHint*/, cbIpAdjust);
    pStartCtx->cr2.u = uCr2Saved;
}

/**
 * Compares \#UD trap.
 */
static void bs3CpuBasic2_CompareUdCtx(PCBS3TRAPFRAME pTrapCtx, PCBS3REGCTX pStartCtx)
{
    bs3CpuBasic2_CompareCpuTrapCtx(pTrapCtx, pStartCtx, 0 /*no error code*/, X86_XCPT_UD,
                                   true /*f486ResumeFlagHint*/, 0 /*cbIpAdjust*/);
}

/**
 * Compares \#AC trap.
 */
static void bs3CpuBasic2_CompareAcCtx(PCBS3TRAPFRAME pTrapCtx, PCBS3REGCTX pStartCtx, uint8_t cbIpAdjust)
{
    bs3CpuBasic2_CompareCpuTrapCtx(pTrapCtx, pStartCtx, 0 /*always zero*/, X86_XCPT_AC, true /*f486ResumeFlagHint*/, cbIpAdjust);
}

/**
 * Compares \#DB trap.
 */
static void bs3CpuBasic2_CompareDbCtx(PCBS3TRAPFRAME pTrapCtx, PCBS3REGCTX pStartCtx, uint32_t fDr6Expect)
{
    uint16_t const cErrorsBefore = Bs3TestSubErrorCount();
    uint32_t const fDr6          = Bs3RegGetDr6();
    fDr6Expect |= X86_DR6_RA1_MASK;
    CHECK_MEMBER("dr6", "%#08RX32", fDr6, fDr6Expect);

    bs3CpuBasic2_CompareCpuTrapCtx(pTrapCtx, pStartCtx, 0 /*always zero*/, X86_XCPT_DB, false /*f486ResumeFlagHint?*/, 0 /*cbIpAdjust*/);

    if (Bs3TestSubErrorCount() > cErrorsBefore)
    {
#if 0
        Bs3TestPrintf("Halting\n");
        ASMHalt();
#endif
    }
}


/**
 * Checks that DR6 has the initial value, i.e. is unchanged when other exception
 * was raised before a \#DB could occur.
 */
static void bs3CpuBasic2_CheckDr6InitVal(void)
{
    uint16_t const cErrorsBefore = Bs3TestSubErrorCount();
    uint32_t const fDr6          = Bs3RegGetDr6();
    uint32_t const fDr6Expect    = X86_DR6_INIT_VAL;
    CHECK_MEMBER("dr6", "%#08RX32", fDr6, fDr6Expect);
    if (Bs3TestSubErrorCount() > cErrorsBefore)
    {
        Bs3TestPrintf("Halting\n");
        ASMHalt();
    }
}

#if 0 /* convert me */
static void bs3CpuBasic2_RaiseXcpt1Common(uint16_t const uSysR0Cs, uint16_t const uSysR0CsConf, uint16_t const uSysR0Ss,
                                          PX86DESC const paIdt, unsigned const cIdteShift)
{
    BS3TRAPFRAME    TrapCtx;
    BS3REGCTX       Ctx80;
    BS3REGCTX       Ctx81;
    BS3REGCTX       Ctx82;
    BS3REGCTX       Ctx83;
    BS3REGCTX       CtxTmp;
    BS3REGCTX       CtxTmp2;
    PBS3REGCTX      apCtx8x[4];
    unsigned        iCtx;
    unsigned        iRing;
    unsigned        iDpl;
    unsigned        iRpl;
    unsigned        i, j, k;
    uint32_t        uExpected;
    bool const      f486Plus = (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80486;
# if TMPL_BITS == 16
    bool const      f386Plus = (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80386;
    bool const      f286     = (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) == BS3CPU_80286;
# else
    bool const      f286     = false;
    bool const      f386Plus = true;
    int             rc;
    uint8_t        *pbIdtCopyAlloc;
    PX86DESC        pIdtCopy;
    const unsigned  cbIdte = 1 << (3 + cIdteShift);
    RTCCUINTXREG    uCr0Saved = ASMGetCR0();
    RTGDTR          GdtrSaved;
# endif
    RTIDTR          IdtrSaved;
    RTIDTR          Idtr;

    ASMGetIDTR(&IdtrSaved);
# if TMPL_BITS != 16
    ASMGetGDTR(&GdtrSaved);
# endif

    /* make sure they're allocated  */
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));
    Bs3MemZero(&Ctx80, sizeof(Ctx80));
    Bs3MemZero(&Ctx81, sizeof(Ctx81));
    Bs3MemZero(&Ctx82, sizeof(Ctx82));
    Bs3MemZero(&Ctx83, sizeof(Ctx83));
    Bs3MemZero(&CtxTmp, sizeof(CtxTmp));
    Bs3MemZero(&CtxTmp2, sizeof(CtxTmp2));

    /* Context array. */
    apCtx8x[0] = &Ctx80;
    apCtx8x[1] = &Ctx81;
    apCtx8x[2] = &Ctx82;
    apCtx8x[3] = &Ctx83;

# if TMPL_BITS != 16
    /* Allocate memory for playing around with the IDT. */
    pbIdtCopyAlloc = NULL;
    if (BS3_MODE_IS_PAGED(g_bTestMode))
        pbIdtCopyAlloc = Bs3MemAlloc(BS3MEMKIND_FLAT32, 12*_1K);
# endif

    /*
     * IDT entry 80 thru 83 are assigned DPLs according to the number.
     * (We'll be useing more, but this'll do for now.)
     */
    paIdt[0x80 << cIdteShift].Gate.u2Dpl = 0;
    paIdt[0x81 << cIdteShift].Gate.u2Dpl = 1;
    paIdt[0x82 << cIdteShift].Gate.u2Dpl = 2;
    paIdt[0x83 << cIdteShift].Gate.u2Dpl = 3;

    Bs3RegCtxSave(&Ctx80);
    Ctx80.rsp.u -= 0x300;
    Ctx80.rip.u  = (uintptr_t)BS3_FP_OFF(&bs3CpuBasic2_Int80);
# if TMPL_BITS == 16
    Ctx80.cs = BS3_MODE_IS_RM_OR_V86(g_bTestMode) ? BS3_SEL_TEXT16 : BS3_SEL_R0_CS16;
# elif TMPL_BITS == 32
    g_uBs3TrapEipHint = Ctx80.rip.u32;
# endif
    Bs3MemCpy(&Ctx81, &Ctx80, sizeof(Ctx80));
    Ctx81.rip.u  = (uintptr_t)BS3_FP_OFF(&bs3CpuBasic2_Int81);
    Bs3MemCpy(&Ctx82, &Ctx80, sizeof(Ctx80));
    Ctx82.rip.u  = (uintptr_t)BS3_FP_OFF(&bs3CpuBasic2_Int82);
    Bs3MemCpy(&Ctx83, &Ctx80, sizeof(Ctx80));
    Ctx83.rip.u  = (uintptr_t)BS3_FP_OFF(&bs3CpuBasic2_Int83);

    /*
     * Check that all the above gates work from ring-0.
     */
    for (iCtx = 0; iCtx < RT_ELEMENTS(apCtx8x); iCtx++)
    {
        g_usBs3TestStep = iCtx;
# if TMPL_BITS == 32
        g_uBs3TrapEipHint = apCtx8x[iCtx]->rip.u32;
# endif
        Bs3TrapSetJmpAndRestore(apCtx8x[iCtx], &TrapCtx);
        bs3CpuBasic2_CompareIntCtx1(&TrapCtx, apCtx8x[iCtx], 0x80+iCtx /*bXcpt*/);
    }

    /*
     * Check that the gate DPL checks works.
     */
    g_usBs3TestStep = 100;
    for (iRing = 0; iRing <= 3; iRing++)
    {
        for (iCtx = 0; iCtx < RT_ELEMENTS(apCtx8x); iCtx++)
        {
            Bs3MemCpy(&CtxTmp, apCtx8x[iCtx], sizeof(CtxTmp));
            Bs3RegCtxConvertToRingX(&CtxTmp, iRing);
# if TMPL_BITS == 32
            g_uBs3TrapEipHint = CtxTmp.rip.u32;
# endif
            Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
            if (iCtx < iRing)
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, ((0x80 + iCtx) << X86_TRAP_ERR_SEL_SHIFT) | X86_TRAP_ERR_IDT);
            else
                bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &CtxTmp, 0x80 + iCtx /*bXcpt*/);
            g_usBs3TestStep++;
        }
    }

    /*
     * Modify the gate CS value and run the handler at a different CPL.
     * Throw RPL variations into the mix (completely ignored) together
     * with gate presence.
     *      1. CPL <= GATE.DPL
     *      2. GATE.P
     *      3. GATE.CS.DPL <= CPL (non-conforming segments)
     */
    g_usBs3TestStep = 1000;
    for (i = 0; i <= 3; i++)
    {
        for (iRing = 0; iRing <= 3; iRing++)
        {
            for (iCtx = 0; iCtx < RT_ELEMENTS(apCtx8x); iCtx++)
            {
# if TMPL_BITS == 32
                g_uBs3TrapEipHint = apCtx8x[iCtx]->rip.u32;
# endif
                Bs3MemCpy(&CtxTmp, apCtx8x[iCtx], sizeof(CtxTmp));
                Bs3RegCtxConvertToRingX(&CtxTmp, iRing);

                for (j = 0; j <= 3; j++)
                {
                    uint16_t const uCs = (uSysR0Cs | j) + (i << BS3_SEL_RING_SHIFT);
                    for (k = 0; k < 2; k++)
                    {
                        g_usBs3TestStep++;
                        /*Bs3TestPrintf("g_usBs3TestStep=%u iCtx=%u iRing=%u i=%u uCs=%04x\n", g_usBs3TestStep,  iCtx,  iRing, i, uCs);*/
                        paIdt[(0x80 + iCtx) << cIdteShift].Gate.u16Sel = uCs;
                        paIdt[(0x80 + iCtx) << cIdteShift].Gate.u1Present = k;
                        Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
                        /*Bs3TrapPrintFrame(&TrapCtx);*/
                        if (iCtx < iRing)
                            bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, ((0x80 + iCtx) << X86_TRAP_ERR_SEL_SHIFT) | X86_TRAP_ERR_IDT);
                        else if (k == 0)
                            bs3CpuBasic2_CompareNpCtx(&TrapCtx, &CtxTmp, ((0x80 + iCtx) << X86_TRAP_ERR_SEL_SHIFT) | X86_TRAP_ERR_IDT);
                        else if (i > iRing)
                            bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, uCs & X86_SEL_MASK_OFF_RPL);
                        else
                        {
                            uint16_t uExpectedCs = uCs & X86_SEL_MASK_OFF_RPL;
                            if (i <= iCtx && i <= iRing)
                                uExpectedCs |= i;
                            bs3CpuBasic2_CompareTrapCtx2(&TrapCtx, &CtxTmp, 2 /*int 8xh*/, 0x80 + iCtx /*bXcpt*/, uExpectedCs);
                        }
                    }
                }

                paIdt[(0x80 + iCtx) << cIdteShift].Gate.u16Sel = uSysR0Cs;
                paIdt[(0x80 + iCtx) << cIdteShift].Gate.u1Present = 1;
            }
        }
    }
    BS3_ASSERT(g_usBs3TestStep < 1600);

    /*
     * Various CS and SS related faults
     *
     * We temporarily reconfigure gate 80 and 83 with new CS selectors, the
     * latter have a CS.DPL of 2 for testing ring transisions and SS loading
     * without making it impossible to handle faults.
     */
    g_usBs3TestStep = 1600;
    Bs3GdteTestPage00 = Bs3Gdt[uSysR0Cs >> X86_SEL_SHIFT];
    Bs3GdteTestPage00.Gen.u1Present = 0;
    Bs3GdteTestPage00.Gen.u4Type &= ~X86_SEL_TYPE_ACCESSED;
    paIdt[0x80 << cIdteShift].Gate.u16Sel = BS3_SEL_TEST_PAGE_00;

    /* CS.PRESENT = 0 */
    Bs3TrapSetJmpAndRestore(&Ctx80, &TrapCtx);
    bs3CpuBasic2_CompareNpCtx(&TrapCtx, &Ctx80, BS3_SEL_TEST_PAGE_00);
    if (Bs3GdteTestPage00.Gen.u4Type & X86_SEL_TYPE_ACCESSED)
        bs3CpuBasic2_FailedF("selector was accessed");
    g_usBs3TestStep++;

    /* Check that GATE.DPL is checked before CS.PRESENT. */
    for (iRing = 1; iRing < 4; iRing++)
    {
        Bs3MemCpy(&CtxTmp, &Ctx80, sizeof(CtxTmp));
        Bs3RegCtxConvertToRingX(&CtxTmp, iRing);
        Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, (0x80 << X86_TRAP_ERR_SEL_SHIFT) | X86_TRAP_ERR_IDT);
        if (Bs3GdteTestPage00.Gen.u4Type & X86_SEL_TYPE_ACCESSED)
            bs3CpuBasic2_FailedF("selector was accessed");
        g_usBs3TestStep++;
    }

    /* CS.DPL mismatch takes precedence over CS.PRESENT = 0. */
    Bs3GdteTestPage00.Gen.u4Type &= ~X86_SEL_TYPE_ACCESSED;
    Bs3TrapSetJmpAndRestore(&Ctx80, &TrapCtx);
    bs3CpuBasic2_CompareNpCtx(&TrapCtx, &Ctx80, BS3_SEL_TEST_PAGE_00);
    if (Bs3GdteTestPage00.Gen.u4Type & X86_SEL_TYPE_ACCESSED)
        bs3CpuBasic2_FailedF("CS selector was accessed");
    g_usBs3TestStep++;
    for (iDpl = 1; iDpl < 4; iDpl++)
    {
        Bs3GdteTestPage00.Gen.u2Dpl = iDpl;
        Bs3TrapSetJmpAndRestore(&Ctx80, &TrapCtx);
        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx80, BS3_SEL_TEST_PAGE_00);
        if (Bs3GdteTestPage00.Gen.u4Type & X86_SEL_TYPE_ACCESSED)
            bs3CpuBasic2_FailedF("CS selector was accessed");
        g_usBs3TestStep++;
    }

    /* 1608: Check all the invalid CS selector types alone. */
    Bs3GdteTestPage00 = Bs3Gdt[uSysR0Cs >> X86_SEL_SHIFT];
    for (i = 0; i < RT_ELEMENTS(g_aInvalidCsTypes); i++)
    {
        Bs3GdteTestPage00.Gen.u4Type     = g_aInvalidCsTypes[i].u4Type;
        Bs3GdteTestPage00.Gen.u1DescType = g_aInvalidCsTypes[i].u1DescType;
        Bs3TrapSetJmpAndRestore(&Ctx80, &TrapCtx);
        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx80, BS3_SEL_TEST_PAGE_00);
        if (Bs3GdteTestPage00.Gen.u4Type != g_aInvalidCsTypes[i].u4Type)
            bs3CpuBasic2_FailedF("Invalid CS type %#x/%u -> %#x/%u\n",
                                 g_aInvalidCsTypes[i].u4Type, g_aInvalidCsTypes[i].u1DescType,
                                 Bs3GdteTestPage00.Gen.u4Type, Bs3GdteTestPage00.Gen.u1DescType);
        g_usBs3TestStep++;

        /* Incorrect CS.TYPE takes precedence over CS.PRESENT = 0. */
        Bs3GdteTestPage00.Gen.u1Present = 0;
        Bs3TrapSetJmpAndRestore(&Ctx80, &TrapCtx);
        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx80, BS3_SEL_TEST_PAGE_00);
        Bs3GdteTestPage00.Gen.u1Present = 1;
        g_usBs3TestStep++;
    }

    /* Fix CS again. */
    Bs3GdteTestPage00 = Bs3Gdt[uSysR0Cs >> X86_SEL_SHIFT];

    /* 1632: Test SS. */
    if (!BS3_MODE_IS_64BIT_SYS(g_bTestMode))
    {
        uint16_t BS3_FAR *puTssSs2    = BS3_MODE_IS_16BIT_SYS(g_bTestMode) ? &Bs3Tss16.ss2 : &Bs3Tss32.ss2;
        uint16_t const    uSavedSs2   = *puTssSs2;
        X86DESC const     SavedGate83 = paIdt[0x83 << cIdteShift];

        /* Make the handler execute in ring-2. */
        Bs3GdteTestPage02 = Bs3Gdt[(uSysR0Cs + (2 << BS3_SEL_RING_SHIFT)) >> X86_SEL_SHIFT];
        Bs3GdteTestPage02.Gen.u4Type &= ~X86_SEL_TYPE_ACCESSED;
        paIdt[0x83 << cIdteShift].Gate.u16Sel = BS3_SEL_TEST_PAGE_02 | 2;

        Bs3MemCpy(&CtxTmp, &Ctx83, sizeof(CtxTmp));
        Bs3RegCtxConvertToRingX(&CtxTmp, 3); /* yeah, from 3 so SS:xSP is reloaded. */
        Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
        bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &CtxTmp, 0x83);
        if (!(Bs3GdteTestPage02.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
            bs3CpuBasic2_FailedF("CS selector was not access");
        g_usBs3TestStep++;

        /* Create a SS.DPL=2 stack segment and check that SS2.RPL matters and
           that we get #SS if the selector isn't present. */
        i = 0; /* used for cycling thru invalid CS types */
        for (k = 0; k < 10; k++)
        {
            /* k=0: present,
               k=1: not-present,
               k=2: present but very low limit,
               k=3: not-present, low limit.
               k=4: present, read-only.
               k=5: not-present, read-only.
               k=6: present, code-selector.
               k=7: not-present, code-selector.
               k=8: present, read-write / no access + system (=LDT).
               k=9: not-present, read-write / no access + system (=LDT).
               */
            Bs3GdteTestPage03 = Bs3Gdt[(uSysR0Ss + (2 << BS3_SEL_RING_SHIFT)) >> X86_SEL_SHIFT];
            Bs3GdteTestPage03.Gen.u1Present  = !(k & 1);
            if (k >= 8)
            {
                Bs3GdteTestPage03.Gen.u1DescType = 0; /* system */
                Bs3GdteTestPage03.Gen.u4Type = X86_SEL_TYPE_RW; /* = LDT */
            }
            else if (k >= 6)
                Bs3GdteTestPage03.Gen.u4Type = X86_SEL_TYPE_ER;
            else if (k >= 4)
                Bs3GdteTestPage03.Gen.u4Type = X86_SEL_TYPE_RO;
            else if (k >= 2)
            {
                Bs3GdteTestPage03.Gen.u16LimitLow   = 0x400;
                Bs3GdteTestPage03.Gen.u4LimitHigh   = 0;
                Bs3GdteTestPage03.Gen.u1Granularity = 0;
            }

            for (iDpl = 0; iDpl < 4; iDpl++)
            {
                Bs3GdteTestPage03.Gen.u2Dpl = iDpl;

                for (iRpl = 0; iRpl < 4; iRpl++)
                {
                    *puTssSs2 = BS3_SEL_TEST_PAGE_03 | iRpl;
                    //Bs3TestPrintf("k=%u iDpl=%u iRpl=%u step=%u\n", k, iDpl, iRpl, g_usBs3TestStep);
                    Bs3GdteTestPage02.Gen.u4Type &= ~X86_SEL_TYPE_ACCESSED;
                    Bs3GdteTestPage03.Gen.u4Type &= ~X86_SEL_TYPE_ACCESSED;
                    Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
                    if (iRpl != 2 || iRpl != iDpl || k >= 4)
                        bs3CpuBasic2_CompareTsCtx(&TrapCtx, &CtxTmp, BS3_SEL_TEST_PAGE_03);
                    else if (k != 0)
                        bs3CpuBasic2_CompareSsCtx(&TrapCtx, &CtxTmp, BS3_SEL_TEST_PAGE_03,
                                                  k == 2 /*f486ResumeFlagHint*/);
                    else
                    {
                        bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &CtxTmp, 0x83);
                        if (TrapCtx.uHandlerSs != (BS3_SEL_TEST_PAGE_03 | 2))
                            bs3CpuBasic2_FailedF("uHandlerSs=%#x expected %#x\n", TrapCtx.uHandlerSs, BS3_SEL_TEST_PAGE_03 | 2);
                    }
                    if (!(Bs3GdteTestPage02.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
                        bs3CpuBasic2_FailedF("CS selector was not access");
                    if (   TrapCtx.bXcpt == 0x83
                        || (TrapCtx.bXcpt == X86_XCPT_SS && k == 2) )
                    {
                        if (!(Bs3GdteTestPage03.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
                            bs3CpuBasic2_FailedF("SS selector was not accessed");
                    }
                    else if (Bs3GdteTestPage03.Gen.u4Type & X86_SEL_TYPE_ACCESSED)
                        bs3CpuBasic2_FailedF("SS selector was accessed");
                    g_usBs3TestStep++;

                    /* +1: Modify the gate DPL to check that this is checked before SS.DPL and SS.PRESENT. */
                    paIdt[0x83 << cIdteShift].Gate.u2Dpl = 2;
                    Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, (0x83 << X86_TRAP_ERR_SEL_SHIFT) | X86_TRAP_ERR_IDT);
                    paIdt[0x83 << cIdteShift].Gate.u2Dpl = 3;
                    g_usBs3TestStep++;

                    /* +2: Check the CS.DPL check is done before the SS ones. Restoring the
                           ring-0 INT 83 context triggers the CS.DPL < CPL check. */
                    Bs3TrapSetJmpAndRestore(&Ctx83, &TrapCtx);
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx83, BS3_SEL_TEST_PAGE_02);
                    g_usBs3TestStep++;

                    /* +3: Now mark the CS selector not present and check that that also triggers before SS stuff. */
                    Bs3GdteTestPage02.Gen.u1Present = 0;
                    Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
                    bs3CpuBasic2_CompareNpCtx(&TrapCtx, &CtxTmp, BS3_SEL_TEST_PAGE_02);
                    Bs3GdteTestPage02.Gen.u1Present = 1;
                    g_usBs3TestStep++;

                    /* +4: Make the CS selector some invalid type and check it triggers before SS stuff. */
                    Bs3GdteTestPage02.Gen.u4Type = g_aInvalidCsTypes[i].u4Type;
                    Bs3GdteTestPage02.Gen.u1DescType = g_aInvalidCsTypes[i].u1DescType;
                    Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, BS3_SEL_TEST_PAGE_02);
                    Bs3GdteTestPage02.Gen.u4Type = X86_SEL_TYPE_ER_ACC;
                    Bs3GdteTestPage02.Gen.u1DescType = 1;
                    g_usBs3TestStep++;

                    /* +5: Now, make the CS selector limit too small and that it triggers after SS trouble.
                           The 286 had a simpler approach to these GP(0). */
                    Bs3GdteTestPage02.Gen.u16LimitLow = 0;
                    Bs3GdteTestPage02.Gen.u4LimitHigh = 0;
                    Bs3GdteTestPage02.Gen.u1Granularity = 0;
                    Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
                    if (f286)
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, 0 /*uErrCd*/);
                    else if (iRpl != 2 || iRpl != iDpl || k >= 4)
                        bs3CpuBasic2_CompareTsCtx(&TrapCtx, &CtxTmp, BS3_SEL_TEST_PAGE_03);
                    else if (k != 0)
                        bs3CpuBasic2_CompareSsCtx(&TrapCtx, &CtxTmp, BS3_SEL_TEST_PAGE_03, k == 2 /*f486ResumeFlagHint*/);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, 0 /*uErrCd*/);
                    Bs3GdteTestPage02 = Bs3Gdt[(uSysR0Cs + (2 << BS3_SEL_RING_SHIFT)) >> X86_SEL_SHIFT];
                    g_usBs3TestStep++;
                }
            }
        }

        /* Check all the invalid SS selector types alone. */
        Bs3GdteTestPage02 = Bs3Gdt[(uSysR0Cs + (2 << BS3_SEL_RING_SHIFT)) >> X86_SEL_SHIFT];
        Bs3GdteTestPage03 = Bs3Gdt[(uSysR0Ss + (2 << BS3_SEL_RING_SHIFT)) >> X86_SEL_SHIFT];
        *puTssSs2 = BS3_SEL_TEST_PAGE_03 | 2;
        Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
        bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &CtxTmp, 0x83);
        g_usBs3TestStep++;
        for (i = 0; i < RT_ELEMENTS(g_aInvalidSsTypes); i++)
        {
            Bs3GdteTestPage03.Gen.u4Type     = g_aInvalidSsTypes[i].u4Type;
            Bs3GdteTestPage03.Gen.u1DescType = g_aInvalidSsTypes[i].u1DescType;
            Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
            bs3CpuBasic2_CompareTsCtx(&TrapCtx, &CtxTmp, BS3_SEL_TEST_PAGE_03);
            if (Bs3GdteTestPage03.Gen.u4Type != g_aInvalidSsTypes[i].u4Type)
                bs3CpuBasic2_FailedF("Invalid SS type %#x/%u -> %#x/%u\n",
                                     g_aInvalidSsTypes[i].u4Type, g_aInvalidSsTypes[i].u1DescType,
                                     Bs3GdteTestPage03.Gen.u4Type, Bs3GdteTestPage03.Gen.u1DescType);
            g_usBs3TestStep++;
        }

        /*
         * Continue the SS experiments with a expand down segment.  We'll use
         * the same setup as we already have with gate 83h being DPL and
         * having CS.DPL=2.
         *
         * Expand down segments are weird. The valid area is practically speaking
         * reversed.  So, a 16-bit segment with a limit of 0x6000 will have valid
         * addresses from 0xffff thru 0x6001.
         *
         * So, with expand down segments we can more easily cut partially into the
         * pushing of the iret frame and trigger more interesting behavior than
         * with regular "expand up" segments where the whole pushing area is either
         * all fine or not not fine.
         */
        Bs3GdteTestPage02 = Bs3Gdt[(uSysR0Cs + (2 << BS3_SEL_RING_SHIFT)) >> X86_SEL_SHIFT];
        Bs3GdteTestPage03 = Bs3Gdt[(uSysR0Ss + (2 << BS3_SEL_RING_SHIFT)) >> X86_SEL_SHIFT];
        Bs3GdteTestPage03.Gen.u2Dpl = 2;
        Bs3GdteTestPage03.Gen.u4Type = X86_SEL_TYPE_RW_DOWN;
        *puTssSs2 = BS3_SEL_TEST_PAGE_03 | 2;

        /* First test, limit = max --> no bytes accessible --> #GP */
        Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
        bs3CpuBasic2_CompareSsCtx(&TrapCtx, &CtxTmp, BS3_SEL_TEST_PAGE_03, true /*f486ResumeFlagHint*/);

        /* Second test, limit = 0 --> all by zero byte accessible --> works */
        Bs3GdteTestPage03.Gen.u16LimitLow = 0;
        Bs3GdteTestPage03.Gen.u4LimitHigh = 0;
        Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
        bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &CtxTmp, 0x83);

        /* Modify the gate handler to be a dummy that immediately does UD2
           and triggers #UD, then advance the limit down till we get the #UD. */
        Bs3GdteTestPage03.Gen.u1Granularity = 0;

        Bs3MemCpy(&CtxTmp2, &CtxTmp, sizeof(CtxTmp2));  /* #UD result context */
        if (g_f16BitSys)
        {
            CtxTmp2.rip.u = g_bs3CpuBasic2_ud2_FlatAddr - BS3_ADDR_BS3TEXT16;
            Bs3Trap16SetGate(0x83, X86_SEL_TYPE_SYS_286_INT_GATE, 3, BS3_SEL_TEST_PAGE_02, CtxTmp2.rip.u16, 0 /*cParams*/);
            CtxTmp2.rsp.u = Bs3Tss16.sp2 - 2*5;
        }
        else
        {
            CtxTmp2.rip.u = g_bs3CpuBasic2_ud2_FlatAddr;
            Bs3Trap32SetGate(0x83, X86_SEL_TYPE_SYS_386_INT_GATE, 3, BS3_SEL_TEST_PAGE_02, CtxTmp2.rip.u32, 0 /*cParams*/);
            CtxTmp2.rsp.u = Bs3Tss32.esp2 - 4*5;
        }
        CtxTmp2.bMode = g_bTestMode; /* g_bBs3CurrentMode not changed by the UD2 handler. */
        CtxTmp2.cs = BS3_SEL_TEST_PAGE_02 | 2;
        CtxTmp2.ss = BS3_SEL_TEST_PAGE_03 | 2;
        CtxTmp2.bCpl = 2;

        /* test run. */
        Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxTmp2);
        g_usBs3TestStep++;

        /* Real run. */
        i = (g_f16BitSys ? 2 : 4) * 6 + 1;
        while (i-- > 0)
        {
            Bs3GdteTestPage03.Gen.u16LimitLow = CtxTmp2.rsp.u16 + i - 1;
            Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
            if (i > 0)
                bs3CpuBasic2_CompareSsCtx(&TrapCtx, &CtxTmp, BS3_SEL_TEST_PAGE_03, true /*f486ResumeFlagHint*/);
            else
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxTmp2);
            g_usBs3TestStep++;
        }

        /* Do a run where we do the same-ring kind of access.  */
        Bs3RegCtxConvertToRingX(&CtxTmp, 2);
        if (g_f16BitSys)
        {
            CtxTmp2.rsp.u32 = CtxTmp.rsp.u32 - 2*3;
            i = 2*3 - 1;
        }
        else
        {
            CtxTmp2.rsp.u32 = CtxTmp.rsp.u32 - 4*3;
            i = 4*3 - 1;
        }
        CtxTmp.ss = BS3_SEL_TEST_PAGE_03 | 2;
        CtxTmp2.ds = CtxTmp.ds;
        CtxTmp2.es = CtxTmp.es;
        CtxTmp2.fs = CtxTmp.fs;
        CtxTmp2.gs = CtxTmp.gs;
        while (i-- > 0)
        {
            Bs3GdteTestPage03.Gen.u16LimitLow = CtxTmp2.rsp.u16 + i - 1;
            Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
            if (i > 0)
                bs3CpuBasic2_CompareSsCtx(&TrapCtx, &CtxTmp, 0 /*BS3_SEL_TEST_PAGE_03*/, true /*f486ResumeFlagHint*/);
            else
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxTmp2);
            g_usBs3TestStep++;
        }

        *puTssSs2 = uSavedSs2;
        paIdt[0x83 << cIdteShift] = SavedGate83;
    }
    paIdt[0x80 << cIdteShift].Gate.u16Sel = uSysR0Cs;
    BS3_ASSERT(g_usBs3TestStep < 3000);

    /*
     * Modify the gate CS value with a conforming segment.
     */
    g_usBs3TestStep = 3000;
    for (i = 0; i <= 3; i++) /* cs.dpl */
    {
        for (iRing = 0; iRing <= 3; iRing++)
        {
            for (iCtx = 0; iCtx < RT_ELEMENTS(apCtx8x); iCtx++)
            {
                Bs3MemCpy(&CtxTmp, apCtx8x[iCtx], sizeof(CtxTmp));
                Bs3RegCtxConvertToRingX(&CtxTmp, iRing);
# if TMPL_BITS == 32
                g_uBs3TrapEipHint = CtxTmp.rip.u32;
# endif

                for (j = 0; j <= 3; j++) /* rpl */
                {
                    uint16_t const uCs = (uSysR0CsConf | j) + (i << BS3_SEL_RING_SHIFT);
                    /*Bs3TestPrintf("g_usBs3TestStep=%u iCtx=%u iRing=%u i=%u uCs=%04x\n", g_usBs3TestStep,  iCtx,  iRing, i, uCs);*/
                    paIdt[(0x80 + iCtx) << cIdteShift].Gate.u16Sel = uCs;
                    Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
                    //Bs3TestPrintf("%u/%u/%u/%u: cs=%04x hcs=%04x xcpt=%02x\n", i, iRing, iCtx, j, uCs, TrapCtx.uHandlerCs, TrapCtx.bXcpt);
                    /*Bs3TrapPrintFrame(&TrapCtx);*/
                    g_usBs3TestStep++;
                    if (iCtx < iRing)
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, ((0x80 + iCtx) << X86_TRAP_ERR_SEL_SHIFT) | X86_TRAP_ERR_IDT);
                    else if (i > iRing)
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, uCs & X86_SEL_MASK_OFF_RPL);
                    else
                        bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &CtxTmp, 0x80 + iCtx /*bXcpt*/);
                }
                paIdt[(0x80 + iCtx) << cIdteShift].Gate.u16Sel = uSysR0Cs;
            }
        }
    }
    BS3_ASSERT(g_usBs3TestStep < 3500);

    /*
     * The gates must be 64-bit in long mode.
     */
    if (cIdteShift != 0)
    {
        g_usBs3TestStep = 3500;
        for (i = 0; i <= 3; i++)
        {
            for (iRing = 0; iRing <= 3; iRing++)
            {
                for (iCtx = 0; iCtx < RT_ELEMENTS(apCtx8x); iCtx++)
                {
                    Bs3MemCpy(&CtxTmp, apCtx8x[iCtx], sizeof(CtxTmp));
                    Bs3RegCtxConvertToRingX(&CtxTmp, iRing);

                    for (j = 0; j < 2; j++)
                    {
                        static const uint16_t s_auCSes[2] = { BS3_SEL_R0_CS16, BS3_SEL_R0_CS32 };
                        uint16_t uCs = (s_auCSes[j] | i) + (i << BS3_SEL_RING_SHIFT);
                        g_usBs3TestStep++;
                        /*Bs3TestPrintf("g_usBs3TestStep=%u iCtx=%u iRing=%u i=%u uCs=%04x\n", g_usBs3TestStep,  iCtx,  iRing, i, uCs);*/
                        paIdt[(0x80 + iCtx) << cIdteShift].Gate.u16Sel = uCs;
                        Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
                        /*Bs3TrapPrintFrame(&TrapCtx);*/
                        if (iCtx < iRing)
                            bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, ((0x80 + iCtx) << X86_TRAP_ERR_SEL_SHIFT) | X86_TRAP_ERR_IDT);
                        else
                            bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, uCs & X86_SEL_MASK_OFF_RPL);
                    }
                    paIdt[(0x80 + iCtx) << cIdteShift].Gate.u16Sel = uSysR0Cs;
                }
            }
        }
        BS3_ASSERT(g_usBs3TestStep < 4000);
    }

    /*
     * IDT limit check.  The 286 does not access X86DESCGATE::u16OffsetHigh.
     */
    g_usBs3TestStep = 5000;
    i = (0x80 << (cIdteShift + 3)) - 1;
    j = (0x82 << (cIdteShift + 3)) - (!f286 ? 1 : 3);
    k = (0x83 << (cIdteShift + 3)) - 1;
    for (; i <= k; i++, g_usBs3TestStep++)
    {
        Idtr = IdtrSaved;
        Idtr.cbIdt  = i;
        ASMSetIDTR(&Idtr);
        Bs3TrapSetJmpAndRestore(&Ctx81, &TrapCtx);
        if (i < j)
            bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx81, (0x81 << X86_TRAP_ERR_SEL_SHIFT) | X86_TRAP_ERR_IDT);
        else
            bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &Ctx81, 0x81 /*bXcpt*/);
    }
    ASMSetIDTR(&IdtrSaved);
    BS3_ASSERT(g_usBs3TestStep < 5100);

# if TMPL_BITS != 16 /* Only do the paging related stuff in 32-bit and 64-bit modes. */

    /*
     * IDT page not present. Placing the IDT copy such that 0x80 is on the
     * first page and 0x81 is on the second page.  We need proceed to move
     * it down byte by byte to check that any inaccessible byte means #PF.
     *
     * Note! We must reload the alternative IDTR for each run as any kind of
     *       printing to the string (like error reporting) will cause a switch
     *       to real mode and back, reloading the default IDTR.
     */
    g_usBs3TestStep = 5200;
    if (BS3_MODE_IS_PAGED(g_bTestMode) && pbIdtCopyAlloc)
    {
        uint32_t const uCr2Expected = Bs3SelPtrToFlat(pbIdtCopyAlloc) + _4K;
        for (j = 0; j < cbIdte; j++)
        {
            pIdtCopy = (PX86DESC)&pbIdtCopyAlloc[_4K - cbIdte * 0x81 - j];
            Bs3MemCpy(pIdtCopy, paIdt, cbIdte * 256);

            Idtr.cbIdt = IdtrSaved.cbIdt;
            Idtr.pIdt  = Bs3SelPtrToFlat(pIdtCopy);

            ASMSetIDTR(&Idtr);
            Bs3TrapSetJmpAndRestore(&Ctx81, &TrapCtx);
            bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &Ctx81, 0x81 /*bXcpt*/);
            g_usBs3TestStep++;

            ASMSetIDTR(&Idtr);
            Bs3TrapSetJmpAndRestore(&Ctx80, &TrapCtx);
            bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &Ctx80, 0x80 /*bXcpt*/);
            g_usBs3TestStep++;

            rc = Bs3PagingProtect(uCr2Expected, _4K, 0 /*fSet*/, X86_PTE_P /*fClear*/);
            if (RT_SUCCESS(rc))
            {
                ASMSetIDTR(&Idtr);
                Bs3TrapSetJmpAndRestore(&Ctx80, &TrapCtx);
                bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &Ctx80, 0x80 /*bXcpt*/);
                g_usBs3TestStep++;

                ASMSetIDTR(&Idtr);
                Bs3TrapSetJmpAndRestore(&Ctx81, &TrapCtx);
                if (f486Plus)
                    bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx81, 0 /*uErrCd*/, uCr2Expected);
                else
                    bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx81, X86_TRAP_PF_RW /*uErrCd*/, uCr2Expected + 4 - RT_MIN(j, 4));
                g_usBs3TestStep++;

                Bs3PagingProtect(uCr2Expected, _4K, X86_PTE_P /*fSet*/, 0 /*fClear*/);

                /* Check if that the entry type is checked after the whole IDTE has been cleared for #PF. */
                pIdtCopy[0x80 << cIdteShift].Gate.u4Type = 0;
                rc = Bs3PagingProtect(uCr2Expected, _4K, 0 /*fSet*/, X86_PTE_P /*fClear*/);
                if (RT_SUCCESS(rc))
                {
                    ASMSetIDTR(&Idtr);
                    Bs3TrapSetJmpAndRestore(&Ctx81, &TrapCtx);
                    if (f486Plus)
                        bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx81, 0 /*uErrCd*/, uCr2Expected);
                    else
                        bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx81, X86_TRAP_PF_RW /*uErrCd*/, uCr2Expected + 4 - RT_MIN(j, 4));
                    g_usBs3TestStep++;

                    Bs3PagingProtect(uCr2Expected, _4K, X86_PTE_P /*fSet*/, 0 /*fClear*/);
                }
            }
            else
                Bs3TestPrintf("Bs3PagingProtectPtr: %d\n", i);

            ASMSetIDTR(&IdtrSaved);
        }
    }

    /*
     * The read/write and user/supervisor bits the IDT PTEs are irrelevant.
     */
    g_usBs3TestStep = 5300;
    if (BS3_MODE_IS_PAGED(g_bTestMode) && pbIdtCopyAlloc)
    {
        Bs3MemCpy(pbIdtCopyAlloc, paIdt, cbIdte * 256);
        Idtr.cbIdt = IdtrSaved.cbIdt;
        Idtr.pIdt  = Bs3SelPtrToFlat(pbIdtCopyAlloc);

        ASMSetIDTR(&Idtr);
        Bs3TrapSetJmpAndRestore(&Ctx81, &TrapCtx);
        bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &Ctx81, 0x81 /*bXcpt*/);
        g_usBs3TestStep++;

        rc = Bs3PagingProtect(Idtr.pIdt, _4K, 0 /*fSet*/, X86_PTE_RW | X86_PTE_US /*fClear*/);
        if (RT_SUCCESS(rc))
        {
            ASMSetIDTR(&Idtr);
            Bs3TrapSetJmpAndRestore(&Ctx81, &TrapCtx);
            bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &Ctx81, 0x81 /*bXcpt*/);
            g_usBs3TestStep++;

            Bs3PagingProtect(Idtr.pIdt, _4K, X86_PTE_RW | X86_PTE_US /*fSet*/, 0 /*fClear*/);
        }
        ASMSetIDTR(&IdtrSaved);
    }

    /*
     * Check that CS.u1Accessed is set to 1. Use the test page selector #0 and #3 together
     * with interrupt gates 80h and 83h, respectively.
     */
/** @todo Throw in SS.u1Accessed too. */
    g_usBs3TestStep = 5400;
    if (BS3_MODE_IS_PAGED(g_bTestMode) && pbIdtCopyAlloc)
    {
        Bs3GdteTestPage00 = Bs3Gdt[uSysR0Cs >> X86_SEL_SHIFT];
        Bs3GdteTestPage00.Gen.u4Type &= ~X86_SEL_TYPE_ACCESSED;
        paIdt[0x80 << cIdteShift].Gate.u16Sel   = BS3_SEL_TEST_PAGE_00;

        Bs3GdteTestPage03 = Bs3Gdt[(uSysR0Cs + (3 << BS3_SEL_RING_SHIFT)) >> X86_SEL_SHIFT];
        Bs3GdteTestPage03.Gen.u4Type &= ~X86_SEL_TYPE_ACCESSED;
        paIdt[0x83 << cIdteShift].Gate.u16Sel   = BS3_SEL_TEST_PAGE_03; /* rpl is ignored, so leave it as zero. */

        /* Check that the CS.A bit is being set on a general basis and that
           the special CS values work with out generic handler code. */
        Bs3TrapSetJmpAndRestore(&Ctx80, &TrapCtx);
        bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &Ctx80, 0x80 /*bXcpt*/);
        if (!(Bs3GdteTestPage00.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
            bs3CpuBasic2_FailedF("u4Type=%#x, not accessed", Bs3GdteTestPage00.Gen.u4Type);
        g_usBs3TestStep++;

        Bs3MemCpy(&CtxTmp, &Ctx83, sizeof(CtxTmp));
        Bs3RegCtxConvertToRingX(&CtxTmp, 3);
        Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
        bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &CtxTmp, 0x83 /*bXcpt*/);
        if (!(Bs3GdteTestPage03.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
            bs3CpuBasic2_FailedF("u4Type=%#x, not accessed!", Bs3GdteTestPage00.Gen.u4Type);
        if (TrapCtx.uHandlerCs != (BS3_SEL_TEST_PAGE_03 | 3))
            bs3CpuBasic2_FailedF("uHandlerCs=%#x, expected %#x", TrapCtx.uHandlerCs, (BS3_SEL_TEST_PAGE_03 | 3));
        g_usBs3TestStep++;

        /*
         * Now check that setting CS.u1Access to 1 does __NOT__ trigger a page
         * fault due to the RW bit being zero.
         * (We check both with with and without the WP bit if 80486.)
         */
        if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80486)
            ASMSetCR0(uCr0Saved | X86_CR0_WP);

        Bs3GdteTestPage00.Gen.u4Type &= ~X86_SEL_TYPE_ACCESSED;
        Bs3GdteTestPage03.Gen.u4Type &= ~X86_SEL_TYPE_ACCESSED;
        rc = Bs3PagingProtect(GdtrSaved.pGdt + BS3_SEL_TEST_PAGE_00, 8, 0 /*fSet*/, X86_PTE_RW /*fClear*/);
        if (RT_SUCCESS(rc))
        {
            /* ring-0 handler */
            Bs3TrapSetJmpAndRestore(&Ctx80, &TrapCtx);
            bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &Ctx80, 0x80 /*bXcpt*/);
            if (!(Bs3GdteTestPage00.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
                bs3CpuBasic2_FailedF("u4Type=%#x, not accessed!", Bs3GdteTestPage00.Gen.u4Type);
            g_usBs3TestStep++;

            /* ring-3 handler */
            Bs3MemCpy(&CtxTmp, &Ctx83, sizeof(CtxTmp));
            Bs3RegCtxConvertToRingX(&CtxTmp, 3);
            Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
            bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &CtxTmp, 0x83 /*bXcpt*/);
            if (!(Bs3GdteTestPage03.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
                bs3CpuBasic2_FailedF("u4Type=%#x, not accessed!", Bs3GdteTestPage00.Gen.u4Type);
            g_usBs3TestStep++;

            /* clear WP and repeat the above. */
            if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80486)
                ASMSetCR0(uCr0Saved & ~X86_CR0_WP);
            Bs3GdteTestPage00.Gen.u4Type &= ~X86_SEL_TYPE_ACCESSED; /* (No need to RW the page - ring-0, WP=0.) */
            Bs3GdteTestPage03.Gen.u4Type &= ~X86_SEL_TYPE_ACCESSED; /* (No need to RW the page - ring-0, WP=0.) */

            Bs3TrapSetJmpAndRestore(&Ctx80, &TrapCtx);
            bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &Ctx80, 0x80 /*bXcpt*/);
            if (!(Bs3GdteTestPage00.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
                bs3CpuBasic2_FailedF("u4Type=%#x, not accessed!", Bs3GdteTestPage00.Gen.u4Type);
            g_usBs3TestStep++;

            Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
            bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &CtxTmp, 0x83 /*bXcpt*/);
            if (!(Bs3GdteTestPage03.Gen.u4Type & X86_SEL_TYPE_ACCESSED))
                bs3CpuBasic2_FailedF("u4Type=%#x, not accessed!n", Bs3GdteTestPage03.Gen.u4Type);
            g_usBs3TestStep++;

            Bs3PagingProtect(GdtrSaved.pGdt + BS3_SEL_TEST_PAGE_00, 8, X86_PTE_RW /*fSet*/, 0 /*fClear*/);
        }

        ASMSetCR0(uCr0Saved);

        /*
         * While we're here, check that if the CS GDT entry is a non-present
         * page we do get a #PF with the rigth error code and CR2.
         */
        Bs3GdteTestPage00.Gen.u4Type &= ~X86_SEL_TYPE_ACCESSED; /* Just for fun, really a pointless gesture. */
        Bs3GdteTestPage03.Gen.u4Type &= ~X86_SEL_TYPE_ACCESSED;
        rc = Bs3PagingProtect(GdtrSaved.pGdt + BS3_SEL_TEST_PAGE_00, 8, 0 /*fSet*/, X86_PTE_P /*fClear*/);
        if (RT_SUCCESS(rc))
        {
            Bs3TrapSetJmpAndRestore(&Ctx80, &TrapCtx);
            if (f486Plus)
                bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx80, 0 /*uErrCd*/, GdtrSaved.pGdt + BS3_SEL_TEST_PAGE_00);
            else
                bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx80, X86_TRAP_PF_RW, GdtrSaved.pGdt + BS3_SEL_TEST_PAGE_00 + 4);
            g_usBs3TestStep++;

            /* Do it from ring-3 to check ErrCd, which doesn't set X86_TRAP_PF_US it turns out. */
            Bs3MemCpy(&CtxTmp, &Ctx83, sizeof(CtxTmp));
            Bs3RegCtxConvertToRingX(&CtxTmp, 3);
            Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);

            if (f486Plus)
                bs3CpuBasic2_ComparePfCtx(&TrapCtx, &CtxTmp, 0 /*uErrCd*/, GdtrSaved.pGdt + BS3_SEL_TEST_PAGE_03);
            else
                bs3CpuBasic2_ComparePfCtx(&TrapCtx, &CtxTmp, X86_TRAP_PF_RW, GdtrSaved.pGdt + BS3_SEL_TEST_PAGE_03 + 4);
            g_usBs3TestStep++;

            Bs3PagingProtect(GdtrSaved.pGdt + BS3_SEL_TEST_PAGE_00, 8, X86_PTE_P /*fSet*/, 0 /*fClear*/);
            if (Bs3GdteTestPage00.Gen.u4Type & X86_SEL_TYPE_ACCESSED)
                bs3CpuBasic2_FailedF("u4Type=%#x, accessed! #1", Bs3GdteTestPage00.Gen.u4Type);
            if (Bs3GdteTestPage03.Gen.u4Type & X86_SEL_TYPE_ACCESSED)
                bs3CpuBasic2_FailedF("u4Type=%#x, accessed! #2", Bs3GdteTestPage03.Gen.u4Type);
        }

        /* restore */
        paIdt[0x80 << cIdteShift].Gate.u16Sel = uSysR0Cs;
        paIdt[0x83 << cIdteShift].Gate.u16Sel = uSysR0Cs;// + (3 << BS3_SEL_RING_SHIFT) + 3;
    }

# endif /* 32 || 64*/

    /*
     * Check broad EFLAGS effects.
     */
    g_usBs3TestStep = 5600;
    for (iCtx = 0; iCtx < RT_ELEMENTS(apCtx8x); iCtx++)
    {
        for (iRing = 0; iRing < 4; iRing++)
        {
            Bs3MemCpy(&CtxTmp, apCtx8x[iCtx], sizeof(CtxTmp));
            Bs3RegCtxConvertToRingX(&CtxTmp, iRing);

            /* all set */
            CtxTmp.rflags.u32 &= X86_EFL_VM | X86_EFL_1;
            CtxTmp.rflags.u32 |= X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF /* | X86_EFL_TF */ /*| X86_EFL_IF*/
                               | X86_EFL_DF | X86_EFL_OF | X86_EFL_IOPL /* | X86_EFL_NT*/;
            if (f486Plus)
                CtxTmp.rflags.u32 |= X86_EFL_AC;
            if (f486Plus && !g_f16BitSys)
                CtxTmp.rflags.u32 |= X86_EFL_RF;
            if (g_uBs3CpuDetected & BS3CPU_F_CPUID)
                CtxTmp.rflags.u32 |= X86_EFL_VIF | X86_EFL_VIP;
            Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
            CtxTmp.rflags.u32 &= ~X86_EFL_RF;

            if (iCtx >= iRing)
                bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &CtxTmp, 0x80 + iCtx /*bXcpt*/);
            else
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, ((0x80 + iCtx) << X86_TRAP_ERR_SEL_SHIFT) | X86_TRAP_ERR_IDT);
            uExpected = CtxTmp.rflags.u32
                      & (  X86_EFL_1 |  X86_EFL_CF | X86_EFL_PF | X86_EFL_AF | X86_EFL_ZF | X86_EFL_SF | X86_EFL_DF
                         | X86_EFL_OF | X86_EFL_IOPL | X86_EFL_NT | X86_EFL_VM | X86_EFL_AC | X86_EFL_VIF | X86_EFL_VIP
                         | X86_EFL_ID /*| X86_EFL_TF*/ /*| X86_EFL_IF*/ /*| X86_EFL_RF*/ );
            if (TrapCtx.fHandlerRfl != uExpected)
                bs3CpuBasic2_FailedF("unexpected handler rflags value: %RX64 expected %RX32; CtxTmp.rflags=%RX64 Ctx.rflags=%RX64\n",
                                     TrapCtx.fHandlerRfl, uExpected, CtxTmp.rflags.u, TrapCtx.Ctx.rflags.u);
            g_usBs3TestStep++;

            /* all cleared */
            if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) < BS3CPU_80286)
                CtxTmp.rflags.u32 = apCtx8x[iCtx]->rflags.u32 & (X86_EFL_RA1_MASK | UINT16_C(0xf000));
            else
                CtxTmp.rflags.u32 = apCtx8x[iCtx]->rflags.u32 & (X86_EFL_VM | X86_EFL_RA1_MASK);
            Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
            if (iCtx >= iRing)
                bs3CpuBasic2_CompareIntCtx1(&TrapCtx, &CtxTmp, 0x80 + iCtx /*bXcpt*/);
            else
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, ((0x80 + iCtx) << X86_TRAP_ERR_SEL_SHIFT) | X86_TRAP_ERR_IDT);
            uExpected = CtxTmp.rflags.u32;
            if (TrapCtx.fHandlerRfl != uExpected)
                bs3CpuBasic2_FailedF("unexpected handler rflags value: %RX64 expected %RX32; CtxTmp.rflags=%RX64 Ctx.rflags=%RX64\n",
                                     TrapCtx.fHandlerRfl, uExpected, CtxTmp.rflags.u, TrapCtx.Ctx.rflags.u);
            g_usBs3TestStep++;
        }
    }

/** @todo CS.LIMIT / canonical(CS)  */


    /*
     * Check invalid gate types.
     */
    g_usBs3TestStep = 32000;
    for (iRing = 0; iRing <= 3; iRing++)
    {
        static const uint16_t   s_auCSes[]        = { BS3_SEL_R0_CS16, BS3_SEL_R0_CS32, BS3_SEL_R0_CS64,
                                                      BS3_SEL_TSS16, BS3_SEL_TSS32, BS3_SEL_TSS64, 0, BS3_SEL_SPARE_1f };
        static uint16_t const   s_auInvlTypes64[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13,
                                                      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                                      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f };
        static uint16_t const   s_auInvlTypes32[] = { 0, 1, 2, 3, 8, 9, 10, 11, 13,
                                                      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                                      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
                                                      /*286:*/ 12, 14, 15 };
        uint16_t const * const  pauInvTypes       = cIdteShift != 0 ? s_auInvlTypes64 : s_auInvlTypes32;
        uint16_t const          cInvTypes         = cIdteShift != 0 ? RT_ELEMENTS(s_auInvlTypes64)
                                                  : f386Plus ? RT_ELEMENTS(s_auInvlTypes32) - 3 : RT_ELEMENTS(s_auInvlTypes32);


        for (iCtx = 0; iCtx < RT_ELEMENTS(apCtx8x); iCtx++)
        {
            unsigned iType;

            Bs3MemCpy(&CtxTmp, apCtx8x[iCtx], sizeof(CtxTmp));
            Bs3RegCtxConvertToRingX(&CtxTmp, iRing);
# if TMPL_BITS == 32
            g_uBs3TrapEipHint = CtxTmp.rip.u32;
# endif
            for (iType = 0; iType < cInvTypes; iType++)
            {
                uint8_t const bSavedType = paIdt[(0x80 + iCtx) << cIdteShift].Gate.u4Type;
                paIdt[(0x80 + iCtx) << cIdteShift].Gate.u1DescType = pauInvTypes[iType] >> 4;
                paIdt[(0x80 + iCtx) << cIdteShift].Gate.u4Type     = pauInvTypes[iType] & 0xf;

                for (i = 0; i < 4; i++)
                {
                    for (j = 0; j < RT_ELEMENTS(s_auCSes); j++)
                    {
                        uint16_t uCs = (unsigned)(s_auCSes[j] - BS3_SEL_R0_FIRST) < (unsigned)(4 << BS3_SEL_RING_SHIFT)
                                     ? (s_auCSes[j] | i) + (i << BS3_SEL_RING_SHIFT)
                                     : s_auCSes[j] | i;
                        /*Bs3TestPrintf("g_usBs3TestStep=%u iCtx=%u iRing=%u i=%u uCs=%04x type=%#x\n", g_usBs3TestStep, iCtx, iRing, i, uCs, pauInvTypes[iType]);*/
                        paIdt[(0x80 + iCtx) << cIdteShift].Gate.u16Sel = uCs;
                        Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
                        g_usBs3TestStep++;
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, ((0x80 + iCtx) << X86_TRAP_ERR_SEL_SHIFT) | X86_TRAP_ERR_IDT);

                        /* Mark it not-present to check that invalid type takes precedence. */
                        paIdt[(0x80 + iCtx) << cIdteShift].Gate.u1Present = 0;
                        Bs3TrapSetJmpAndRestore(&CtxTmp, &TrapCtx);
                        g_usBs3TestStep++;
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxTmp, ((0x80 + iCtx) << X86_TRAP_ERR_SEL_SHIFT) | X86_TRAP_ERR_IDT);
                        paIdt[(0x80 + iCtx) << cIdteShift].Gate.u1Present = 1;
                    }
                }

                paIdt[(0x80 + iCtx) << cIdteShift].Gate.u16Sel     = uSysR0Cs;
                paIdt[(0x80 + iCtx) << cIdteShift].Gate.u4Type     = bSavedType;
                paIdt[(0x80 + iCtx) << cIdteShift].Gate.u1DescType = 0;
                paIdt[(0x80 + iCtx) << cIdteShift].Gate.u1Present  = 1;
            }
        }
    }
    BS3_ASSERT(g_usBs3TestStep < 62000U && g_usBs3TestStep > 32000U);


    /** @todo
     *  - Run \#PF and \#GP (and others?) at CPLs other than zero.
     *  - Quickly generate all faults.
     *  - All the peculiarities v8086.
     */

# if TMPL_BITS != 16
    Bs3MemFree(pbIdtCopyAlloc, 12*_1K);
# endif
}
#endif /* convert me */


static void bs3CpuBasic2_RaiseXcpt11Worker(uint8_t bMode, uint8_t *pbBuf, unsigned cbCacheLine, bool fAm, bool fPf,
                                           RTCCUINTXREG uFlatBufPtr, BS3CPUBASIC2PFTTSTCMNMODE const BS3_FAR *pCmn)
{
    BS3TRAPFRAME        TrapCtx;
    BS3REGCTX           Ctx;
    BS3REGCTX           CtxUdExpected;
    uint8_t const       cRings = bMode == BS3_MODE_RM ? 1 : 4;
    uint8_t             iRing;
    uint16_t            iTest;

    /* make sure they're allocated  */
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&CtxUdExpected, sizeof(CtxUdExpected));

    /*
     * Test all relevant rings.
     *
     * The memory operand is ds:xBX, so point it to pbBuf.
     * The test snippets mostly use xAX as operand, with the div
     * one also using xDX, so make sure they make some sense.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 512);

    Ctx.cr0.u32 &= ~(X86_CR0_MP | X86_CR0_EM | X86_CR0_TS); /* so fninit + fld works */

    for (iRing = BS3_MODE_IS_V86(bMode) ? 3 : 0; iRing < cRings; iRing++)
    {
        uint32_t    uEbx;
        uint8_t     fAc;

        if (!BS3_MODE_IS_RM_OR_V86(bMode))
            Bs3RegCtxConvertToRingX(&Ctx, iRing);

        if (!fPf || BS3_MODE_IS_32BIT_CODE(bMode) || BS3_MODE_IS_64BIT_CODE(bMode))
            Bs3RegCtxSetGrpDsFromCurPtr(&Ctx, &Ctx.rbx, pbBuf);
        else
        {
            /* Bs3RegCtxSetGrpDsFromCurPtr barfs when trying to output a sel:off address for the aliased buffer. */
            Ctx.ds      = BS3_FP_SEG(pbBuf);
            Ctx.rbx.u32 = BS3_FP_OFF(pbBuf);
        }
        uEbx = Ctx.rbx.u32;

        Ctx.rax.u = (bMode & BS3_MODE_CODE_MASK) == BS3_MODE_CODE_64
                  ? UINT64_C(0x80868028680386fe) : UINT32_C(0x65020686);
        Ctx.rdx.u = UINT32_C(0x00100100); /* careful with range due to div */

        Bs3MemCpy(&CtxUdExpected, &Ctx, sizeof(Ctx));

        /*
         * AC flag loop.
         */
        for (fAc = 0; fAc < 2; fAc++)
        {
            if (fAc)
                Ctx.rflags.u32 |= X86_EFL_AC;
            else
                Ctx.rflags.u32 &= ~X86_EFL_AC;

            /*
             * Loop over the test snippets.
             */
            for (iTest = 0; iTest < pCmn->cEntries; iTest++)
            {
                uint8_t const    fOp     = pCmn->paEntries[iTest].fOp;
                uint16_t const   cbMem   = pCmn->paEntries[iTest].cbMem;
                uint8_t const    cbAlign = pCmn->paEntries[iTest].cbAlign;
                uint16_t const   cbMax   = cbCacheLine + cbMem;
                uint16_t         offMem;
                uint8_t BS3_FAR *poffUd = (uint8_t BS3_FAR *)Bs3SelLnkPtrToCurPtr(pCmn->paEntries[iTest].pfn);
                Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, pCmn->paEntries[iTest].pfn);
                CtxUdExpected.rip    = Ctx.rip;
                CtxUdExpected.rip.u  = Ctx.rip.u + poffUd[-1];
                CtxUdExpected.cs     = Ctx.cs;
                CtxUdExpected.rflags = Ctx.rflags;
                if (bMode == BS3_MODE_RM)
                    CtxUdExpected.rflags.u32 &= ~X86_EFL_AC; /** @todo investigate. automatically cleared, or is it just our code?  Observed with bs3-cpu-instr-3 too (10980xe), seems to be the CPU doing it. */
                CtxUdExpected.rdx    = Ctx.rdx;
                CtxUdExpected.rax    = Ctx.rax;
                if (fOp & MYOP_LD)
                {
                    switch (cbMem)
                    {
                        case 2:
                            CtxUdExpected.rax.u16 = 0x0101;
                            break;
                        case 4:
                            CtxUdExpected.rax.u32 = UINT32_C(0x01010101);
                            break;
                        case 8:
                            CtxUdExpected.rax.u64 = UINT64_C(0x0101010101010101);
                            break;
                    }
                }

                /*
                 * Buffer misalignment loop.
                 * Note! We must make sure to cross a cache line here to make sure
                 *       to cover the split-lock scenario. (The buffer is cache
                 *       line aligned.)
                 */
                for (offMem = 0; offMem < cbMax; offMem++)
                {
                    bool const fMisaligned = (offMem & (cbAlign - 1)) != 0;
                    unsigned   offBuf      = cbMax + cbMem * 2;
                    while (offBuf-- > 0)
                        pbBuf[offBuf] = 1; /* byte-by-byte to make sure it doesn't trigger AC. */

                    CtxUdExpected.rbx.u32 = Ctx.rbx.u32 = uEbx + offMem; /* ASSUMES memory in first 4GB. */
                    if (BS3_MODE_IS_16BIT_SYS(bMode))
                        g_uBs3TrapEipHint = Ctx.rip.u32;

                    //Bs3TestPrintf("iRing=%d iTest=%d cs:rip=%04RX16:%08RX32 ds:rbx=%04RX16:%08RX32 ss:esp=%04RX16:%08RX32 bXcpt=%#x errcd=%#x fAm=%d fAc=%d ESP=%#RX32\n",
                    //              iRing, iTest, Ctx.cs, Ctx.rip.u32, Ctx.ds, Ctx.rbx.u32, Ctx.ss, Ctx.rsp.u32, TrapCtx.bXcpt, (unsigned)TrapCtx.uErrCd, fAm, fAc, ASMGetESP());

                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);

                    if (   (pCmn->paEntries[iTest].fOp & MYOP_AC_GP)
                        && fMisaligned
                        && (!fAm || iRing != 3 || !fAc || (offMem & 3 /* 10980XE */) == 0) )
                    {
                        if (fAc && bMode == BS3_MODE_RM)
                            TrapCtx.Ctx.rflags.u32 |= X86_EFL_AC;
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                    }
                    else if (fPf && iRing == 3 && (!fAm || !fAc || !fMisaligned)) /* #AC beats #PF */
                        bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx,
                                                  X86_TRAP_PF_P | X86_TRAP_PF_US
                                                  | (pCmn->paEntries[iTest].fOp & MYOP_ST ? X86_TRAP_PF_RW : 0),
                                                  uFlatBufPtr + offMem + (cbMem > 64 ? cbMem - 1 /*FXSAVE*/ : 0),
                                                  pCmn->paEntries[iTest].offFaultInstr);
                    else if (!fAm || iRing != 3 || !fAc || !fMisaligned)
                    {
                        if (fOp & MYOP_EFL)
                        {
                            CtxUdExpected.rflags.u16 &= ~X86_EFL_STATUS_BITS;
                            CtxUdExpected.rflags.u16 |= TrapCtx.Ctx.rflags.u16 & X86_EFL_STATUS_BITS;
                        }
                        if (fOp == MYOP_LD_DIV)
                        {
                            CtxUdExpected.rax = TrapCtx.Ctx.rax;
                            CtxUdExpected.rdx = TrapCtx.Ctx.rdx;
                        }
                        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                    }
                    else
                        bs3CpuBasic2_CompareAcCtx(&TrapCtx, &Ctx, pCmn->paEntries[iTest].offFaultInstr);

                    g_usBs3TestStep++;
                }
            }
        }
    }
}


/**
 * Entrypoint for \#AC tests.
 *
 * @returns 0 or BS3TESTDOMODE_SKIPPED.
 * @param   bMode       The CPU mode we're testing.
 *
 * @note    When testing v8086 code, we'll be running in v8086 mode. So, careful
 *          with control registers and such.
 */
BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuBasic2_RaiseXcpt11)(uint8_t bMode)
{
    unsigned            cbCacheLine = 128; /** @todo detect */
    uint8_t BS3_FAR    *pbBufAlloc;
    uint8_t BS3_FAR    *pbBuf;
    unsigned            idxCmnModes;
    uint32_t            fCr0;

    /*
     * Skip if 386 or older.
     */
    if ((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) < BS3CPU_80486)
    {
        Bs3TestSkipped("#AC test requires 486 or later");
        return BS3TESTDOMODE_SKIPPED;
    }

    bs3CpuBasic2_SetGlobals(bMode);

    /* Get us a 64-byte aligned buffer. */
    pbBufAlloc = pbBuf = Bs3MemAllocZ(BS3_MODE_IS_RM_OR_V86(bMode) ? BS3MEMKIND_REAL : BS3MEMKIND_TILED, X86_PAGE_SIZE * 2);
    if (!pbBufAlloc)
        return Bs3TestFailed("Failed to allocate 2 pages of real-mode memory");
    if (BS3_FP_OFF(pbBuf) & (X86_PAGE_SIZE - 1))
        pbBuf = &pbBufAlloc[X86_PAGE_SIZE - (BS3_FP_OFF(pbBuf) & X86_PAGE_OFFSET_MASK)];
    BS3_ASSERT(pbBuf - pbBufAlloc <= X86_PAGE_SIZE);
    //Bs3TestPrintf("pbBuf=%p\n", pbBuf);

    /* Find the g_aCmnModes entry. */
    idxCmnModes = 0;
    while (g_aCmnModes[idxCmnModes].bMode != (bMode & BS3_MODE_CODE_MASK))
        idxCmnModes++;
    //Bs3TestPrintf("idxCmnModes=%d bMode=%#x\n", idxCmnModes, bMode);

    /* First round is w/o alignment checks enabled. */
    //Bs3TestPrintf("round 1\n");
    fCr0 = Bs3RegGetCr0();
    BS3_ASSERT(!(fCr0 & X86_CR0_AM));
    Bs3RegSetCr0(fCr0 & ~X86_CR0_AM);
#if 1
    bs3CpuBasic2_RaiseXcpt11Worker(bMode, pbBuf, cbCacheLine, false /*fAm*/, false /*fPf*/, 0, &g_aCmnModes[idxCmnModes]);
#endif

    /* The second round is with aligment checks enabled. */
#if 1
    //Bs3TestPrintf("round 2\n");
    Bs3RegSetCr0(Bs3RegGetCr0() | X86_CR0_AM);
    bs3CpuBasic2_RaiseXcpt11Worker(bMode, pbBuf, cbCacheLine, true /*fAm*/, false /*fPf*/, 0, &g_aCmnModes[idxCmnModes]);
#endif

#if 1
    /* The third and fourth round access the buffer via a page alias that's not
       accessible from ring-3.  The third round has ACs disabled and the fourth
       has them enabled. */
    if (BS3_MODE_IS_PAGED(bMode) && !BS3_MODE_IS_V86(bMode))
    {
        /* Alias the buffer as system memory so ring-3 access with AC+AM will cause #PF: */
        /** @todo the aliasing is not necessary any more...   */
        int            rc;
        RTCCUINTXREG   uFlatBufPtr = Bs3SelPtrToFlat(pbBuf);
        uint64_t const uAliasPgPtr = bMode & BS3_MODE_CODE_64 ? UINT64_C(0x0000648680000000) : UINT32_C(0x80000000);
        rc = Bs3PagingAlias(uAliasPgPtr, uFlatBufPtr & ~(uint64_t)X86_PAGE_OFFSET_MASK, X86_PAGE_SIZE * 2,
                            X86_PTE_P | X86_PTE_RW);
        if (RT_SUCCESS(rc))
        {
            /* We 'misalign' the segment base here to make sure it's the final
               address that gets alignment checked and not just the operand value. */
            RTCCUINTXREG     uAliasBufPtr = (RTCCUINTXREG)uAliasPgPtr + (uFlatBufPtr & X86_PAGE_OFFSET_MASK);
            uint8_t BS3_FAR *pbBufAlias   = BS3_FP_MAKE(BS3_SEL_SPARE_00 | 3, (uFlatBufPtr & X86_PAGE_OFFSET_MASK) + 1);
            Bs3SelSetup16BitData(&Bs3GdteSpare00, uAliasPgPtr - 1);

            //Bs3TestPrintf("round 3 pbBufAlias=%p\n", pbBufAlias);
            Bs3RegSetCr0(Bs3RegGetCr0() & ~X86_CR0_AM);
            bs3CpuBasic2_RaiseXcpt11Worker(bMode, pbBufAlias, cbCacheLine, false /*fAm*/,
                                           true /*fPf*/, uAliasBufPtr, &g_aCmnModes[idxCmnModes]);

            //Bs3TestPrintf("round 4\n");
            Bs3RegSetCr0(Bs3RegGetCr0() | X86_CR0_AM);
            bs3CpuBasic2_RaiseXcpt11Worker(bMode, pbBufAlias, cbCacheLine, true /*fAm*/,
                                           true /*fPf*/, uAliasBufPtr, &g_aCmnModes[idxCmnModes]);

            Bs3PagingUnalias(uAliasPgPtr, X86_PAGE_SIZE * 2);
        }
        else
            Bs3TestFailedF("Bs3PagingAlias failed with %Rrc", rc);
    }
#endif

    Bs3MemFree(pbBufAlloc, X86_PAGE_SIZE * 2);
    Bs3RegSetCr0(fCr0);
    return 0;
}


/**
 * Executes one round of SIDT and SGDT tests using one assembly worker.
 *
 * This is written with driving everything from the 16-bit or 32-bit worker in
 * mind, i.e. not assuming the test bitcount is the same as the current.
 */
static void bs3CpuBasic2_sidt_sgdt_One(BS3CB2SIDTSGDT const BS3_FAR *pWorker, uint8_t bTestMode, uint8_t bRing,
                                       uint8_t const *pbExpected)
{
    BS3TRAPFRAME        TrapCtx;
    BS3REGCTX           Ctx;
    BS3REGCTX           CtxUdExpected;
    BS3REGCTX           TmpCtx;
    uint8_t const       cbBuf = 8*2;         /* test buffer area  */
    uint8_t             abBuf[8*2 + 8 + 8];  /* test buffer w/ misalignment test space and some extra guard. */
    uint8_t BS3_FAR    *pbBuf  = abBuf;
    uint8_t const       cbIdtr = BS3_MODE_IS_64BIT_CODE(bTestMode) ? 2+8 : 2+4;
    bool const          f286   = (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) == BS3CPU_80286;
    uint8_t             bFiller;
    int                 off;
    int                 off2;
    unsigned            cb;
    uint8_t BS3_FAR    *pbTest;

    /* make sure they're allocated  */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&CtxUdExpected, sizeof(CtxUdExpected));
    Bs3MemZero(&TmpCtx, sizeof(TmpCtx));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));
    Bs3MemZero(&abBuf, sizeof(abBuf));

    /* Create a context, give this routine some more stack space, point the context
       at our SIDT [xBX] + UD2 combo, and point DS:xBX at abBuf. */
    Bs3RegCtxSaveEx(&Ctx, bTestMode, 256 /*cbExtraStack*/);
    Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, pWorker->fSs ? &Ctx.ss : &Ctx.ds, abBuf);
    Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, pWorker->fpfnWorker);
    if (BS3_MODE_IS_16BIT_SYS(bTestMode))
        g_uBs3TrapEipHint = Ctx.rip.u32;
    if (!BS3_MODE_IS_RM_OR_V86(bTestMode))
        Bs3RegCtxConvertToRingX(&Ctx, bRing);

    /* For successful SIDT attempts, we'll stop at the UD2. */
    Bs3MemCpy(&CtxUdExpected, &Ctx, sizeof(Ctx));
    CtxUdExpected.rip.u += pWorker->cbInstr;

    /*
     * Check that it works at all and that only bytes we expect gets written to.
     */
    /* First with zero buffer. */
    Bs3MemZero(abBuf, sizeof(abBuf));
    if (!ASMMemIsAllU8(abBuf, sizeof(abBuf), 0))
        Bs3TestFailedF("ASMMemIsAllU8 or Bs3MemZero is busted: abBuf=%.*Rhxs\n", sizeof(abBuf), pbBuf);
    if (!ASMMemIsZero(abBuf, sizeof(abBuf)))
        Bs3TestFailedF("ASMMemIsZero or Bs3MemZero is busted: abBuf=%.*Rhxs\n", sizeof(abBuf), pbBuf);
    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
    bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
    if (f286 && abBuf[cbIdtr - 1] != 0xff)
        Bs3TestFailedF("286: Top base byte isn't 0xff (#1): %#x\n", abBuf[cbIdtr - 1]);
    if (!ASMMemIsZero(&abBuf[cbIdtr], cbBuf - cbIdtr))
        Bs3TestFailedF("Unexpected buffer bytes set (#1): cbIdtr=%u abBuf=%.*Rhxs\n", cbIdtr, cbBuf, pbBuf);
    if (Bs3MemCmp(abBuf, pbExpected, cbIdtr) != 0)
        Bs3TestFailedF("Mismatch (%s,#1): expected %.*Rhxs, got %.*Rhxs\n", pWorker->pszDesc, cbIdtr, pbExpected, cbIdtr, abBuf);
    g_usBs3TestStep++;

    /* Again with a buffer filled with a byte not occuring in the previous result. */
    bFiller = 0x55;
    while (Bs3MemChr(abBuf, bFiller, cbBuf) != NULL)
        bFiller++;
    Bs3MemSet(abBuf, bFiller, sizeof(abBuf));
    if (!ASMMemIsAllU8(abBuf, sizeof(abBuf), bFiller))
        Bs3TestFailedF("ASMMemIsAllU8 or Bs3MemSet is busted: bFiller=%#x abBuf=%.*Rhxs\n", bFiller, sizeof(abBuf), pbBuf);

    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
    bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
    if (f286 && abBuf[cbIdtr - 1] != 0xff)
        Bs3TestFailedF("286: Top base byte isn't 0xff (#2): %#x\n", abBuf[cbIdtr - 1]);
    if (!ASMMemIsAllU8(&abBuf[cbIdtr], cbBuf - cbIdtr, bFiller))
        Bs3TestFailedF("Unexpected buffer bytes set (#2): cbIdtr=%u bFiller=%#x abBuf=%.*Rhxs\n", cbIdtr, bFiller, cbBuf, pbBuf);
    if (Bs3MemChr(abBuf, bFiller, cbIdtr) != NULL)
        Bs3TestFailedF("Not all bytes touched: cbIdtr=%u bFiller=%#x abBuf=%.*Rhxs\n", cbIdtr, bFiller, cbBuf, pbBuf);
    if (Bs3MemCmp(abBuf, pbExpected, cbIdtr) != 0)
        Bs3TestFailedF("Mismatch (%s,#2): expected %.*Rhxs, got %.*Rhxs\n", pWorker->pszDesc, cbIdtr, pbExpected, cbIdtr, abBuf);
    g_usBs3TestStep++;

    /*
     * Slide the buffer along 8 bytes to cover misalignment.
     */
    for (off = 0; off < 8; off++)
    {
        pbBuf = &abBuf[off];
        Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, pWorker->fSs ? &Ctx.ss : &Ctx.ds, &abBuf[off]);
        CtxUdExpected.rbx.u = Ctx.rbx.u;

        /* First with zero buffer. */
        Bs3MemZero(abBuf, sizeof(abBuf));
        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
        if (off > 0 && !ASMMemIsZero(abBuf, off))
            Bs3TestFailedF("Unexpected buffer bytes set before (#3): cbIdtr=%u off=%u abBuf=%.*Rhxs\n",
                           cbIdtr, off, off + cbBuf, abBuf);
        if (!ASMMemIsZero(&abBuf[off + cbIdtr], sizeof(abBuf) - cbIdtr - off))
            Bs3TestFailedF("Unexpected buffer bytes set after (#3): cbIdtr=%u off=%u abBuf=%.*Rhxs\n",
                           cbIdtr, off, off + cbBuf, abBuf);
        if (f286 && abBuf[off + cbIdtr - 1] != 0xff)
            Bs3TestFailedF("286: Top base byte isn't 0xff (#3): %#x\n", abBuf[off + cbIdtr - 1]);
        if (Bs3MemCmp(&abBuf[off], pbExpected, cbIdtr) != 0)
            Bs3TestFailedF("Mismatch (#3): expected %.*Rhxs, got %.*Rhxs\n", cbIdtr, pbExpected, cbIdtr, &abBuf[off]);
        g_usBs3TestStep++;

        /* Again with a buffer filled with a byte not occuring in the previous result. */
        Bs3MemSet(abBuf, bFiller, sizeof(abBuf));
        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
        if (off > 0 && !ASMMemIsAllU8(abBuf, off, bFiller))
            Bs3TestFailedF("Unexpected buffer bytes set before (#4): cbIdtr=%u off=%u bFiller=%#x abBuf=%.*Rhxs\n",
                           cbIdtr, off, bFiller, off + cbBuf, abBuf);
        if (!ASMMemIsAllU8(&abBuf[off + cbIdtr], sizeof(abBuf) - cbIdtr - off, bFiller))
            Bs3TestFailedF("Unexpected buffer bytes set after (#4): cbIdtr=%u off=%u bFiller=%#x abBuf=%.*Rhxs\n",
                           cbIdtr, off, bFiller, off + cbBuf, abBuf);
        if (Bs3MemChr(&abBuf[off], bFiller, cbIdtr) != NULL)
            Bs3TestFailedF("Not all bytes touched (#4): cbIdtr=%u off=%u bFiller=%#x abBuf=%.*Rhxs\n",
                           cbIdtr, off, bFiller, off + cbBuf, abBuf);
        if (f286 && abBuf[off + cbIdtr - 1] != 0xff)
            Bs3TestFailedF("286: Top base byte isn't 0xff (#4): %#x\n", abBuf[off + cbIdtr - 1]);
        if (Bs3MemCmp(&abBuf[off], pbExpected, cbIdtr) != 0)
            Bs3TestFailedF("Mismatch (#4): expected %.*Rhxs, got %.*Rhxs\n", cbIdtr, pbExpected, cbIdtr, &abBuf[off]);
        g_usBs3TestStep++;
    }
    pbBuf = abBuf;
    Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, pWorker->fSs ? &Ctx.ss : &Ctx.ds, abBuf);
    CtxUdExpected.rbx.u = Ctx.rbx.u;

    /*
     * Play with the selector limit if the target mode supports limit checking
     * We use BS3_SEL_TEST_PAGE_00 for this
     */
    if (   !BS3_MODE_IS_RM_OR_V86(bTestMode)
        && !BS3_MODE_IS_64BIT_CODE(bTestMode))
    {
        uint16_t cbLimit;
        uint32_t uFlatBuf = Bs3SelPtrToFlat(abBuf);
        Bs3GdteTestPage00 = Bs3Gdte_DATA16;
        Bs3GdteTestPage00.Gen.u2Dpl       = bRing;
        Bs3GdteTestPage00.Gen.u16BaseLow  = (uint16_t)uFlatBuf;
        Bs3GdteTestPage00.Gen.u8BaseHigh1 = (uint8_t)(uFlatBuf >> 16);
        Bs3GdteTestPage00.Gen.u8BaseHigh2 = (uint8_t)(uFlatBuf >> 24);

        if (pWorker->fSs)
            CtxUdExpected.ss = Ctx.ss = BS3_SEL_TEST_PAGE_00 | bRing;
        else
            CtxUdExpected.ds = Ctx.ds = BS3_SEL_TEST_PAGE_00 | bRing;

        /* Expand up (normal). */
        for (off = 0; off < 8; off++)
        {
            CtxUdExpected.rbx.u = Ctx.rbx.u = off;
            for (cbLimit = 0; cbLimit < cbIdtr*2; cbLimit++)
            {
                Bs3GdteTestPage00.Gen.u16LimitLow = cbLimit;
                Bs3MemSet(abBuf, bFiller, sizeof(abBuf));
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                if (off + cbIdtr <= cbLimit + 1)
                {
                    bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                    if (Bs3MemChr(&abBuf[off], bFiller, cbIdtr) != NULL)
                        Bs3TestFailedF("Not all bytes touched (#5): cbIdtr=%u off=%u cbLimit=%u bFiller=%#x abBuf=%.*Rhxs\n",
                                       cbIdtr, off, cbLimit, bFiller, off + cbBuf, abBuf);
                    if (Bs3MemCmp(&abBuf[off], pbExpected, cbIdtr) != 0)
                        Bs3TestFailedF("Mismatch (#5): expected %.*Rhxs, got %.*Rhxs\n", cbIdtr, pbExpected, cbIdtr, &abBuf[off]);
                    if (f286 && abBuf[off + cbIdtr - 1] != 0xff)
                        Bs3TestFailedF("286: Top base byte isn't 0xff (#5): %#x\n", abBuf[off + cbIdtr - 1]);
                }
                else
                {
                    if (pWorker->fSs)
                        bs3CpuBasic2_CompareSsCtx(&TrapCtx, &Ctx, 0, false /*f486ResumeFlagHint*/);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                    if (off + 2 <= cbLimit + 1)
                    {
                        if (Bs3MemChr(&abBuf[off], bFiller, 2) != NULL)
                            Bs3TestFailedF("Limit bytes not touched (#6): cbIdtr=%u off=%u cbLimit=%u bFiller=%#x abBuf=%.*Rhxs\n",
                                           cbIdtr, off, cbLimit, bFiller, off + cbBuf, abBuf);
                        if (Bs3MemCmp(&abBuf[off], pbExpected, 2) != 0)
                            Bs3TestFailedF("Mismatch (#6): expected %.2Rhxs, got %.2Rhxs\n", pbExpected, &abBuf[off]);
                        if (!ASMMemIsAllU8(&abBuf[off + 2], cbIdtr - 2, bFiller))
                            Bs3TestFailedF("Base bytes touched on #GP (#6): cbIdtr=%u off=%u cbLimit=%u bFiller=%#x abBuf=%.*Rhxs\n",
                                           cbIdtr, off, cbLimit, bFiller, off + cbBuf, abBuf);
                    }
                    else if (!ASMMemIsAllU8(abBuf, sizeof(abBuf), bFiller))
                        Bs3TestFailedF("Bytes touched on #GP: cbIdtr=%u off=%u cbLimit=%u bFiller=%#x abBuf=%.*Rhxs\n",
                                       cbIdtr, off, cbLimit, bFiller, off + cbBuf, abBuf);
                }

                if (off > 0 && !ASMMemIsAllU8(abBuf, off, bFiller))
                    Bs3TestFailedF("Leading bytes touched (#7): cbIdtr=%u off=%u cbLimit=%u bFiller=%#x abBuf=%.*Rhxs\n",
                                   cbIdtr, off, cbLimit, bFiller, off + cbBuf, abBuf);
                if (!ASMMemIsAllU8(&abBuf[off + cbIdtr], sizeof(abBuf) - off - cbIdtr, bFiller))
                    Bs3TestFailedF("Trailing bytes touched (#7): cbIdtr=%u off=%u cbLimit=%u bFiller=%#x abBuf=%.*Rhxs\n",
                                   cbIdtr, off, cbLimit, bFiller, off + cbBuf, abBuf);

                g_usBs3TestStep++;
            }
        }

        /* Expand down (weird).  Inverted valid area compared to expand up,
           so a limit of zero give us a valid range for 0001..0ffffh (instead of
           a segment with one valid byte at 0000h).  Whereas a limit of 0fffeh
           means one valid byte at 0ffffh, and a limit of 0ffffh means none
           (because in a normal expand up the 0ffffh means all 64KB are
           accessible). */
        Bs3GdteTestPage00.Gen.u4Type = X86_SEL_TYPE_RW_DOWN_ACC;
        for (off = 0; off < 8; off++)
        {
            CtxUdExpected.rbx.u = Ctx.rbx.u = off;
            for (cbLimit = 0; cbLimit < cbIdtr*2; cbLimit++)
            {
                Bs3GdteTestPage00.Gen.u16LimitLow = cbLimit;
                Bs3MemSet(abBuf, bFiller, sizeof(abBuf));
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);

                if (off > cbLimit)
                {
                    bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                    if (Bs3MemChr(&abBuf[off], bFiller, cbIdtr) != NULL)
                        Bs3TestFailedF("Not all bytes touched (#8): cbIdtr=%u off=%u cbLimit=%u bFiller=%#x abBuf=%.*Rhxs\n",
                                       cbIdtr, off, cbLimit, bFiller, off + cbBuf, abBuf);
                    if (Bs3MemCmp(&abBuf[off], pbExpected, cbIdtr) != 0)
                        Bs3TestFailedF("Mismatch (#8): expected %.*Rhxs, got %.*Rhxs\n", cbIdtr, pbExpected, cbIdtr, &abBuf[off]);
                    if (f286 && abBuf[off + cbIdtr - 1] != 0xff)
                        Bs3TestFailedF("286: Top base byte isn't 0xff (#8): %#x\n", abBuf[off + cbIdtr - 1]);
                }
                else
                {
                    if (pWorker->fSs)
                        bs3CpuBasic2_CompareSsCtx(&TrapCtx, &Ctx, 0, false /*f486ResumeFlagHint*/);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                    if (!ASMMemIsAllU8(abBuf, sizeof(abBuf), bFiller))
                        Bs3TestFailedF("Bytes touched on #GP: cbIdtr=%u off=%u cbLimit=%u bFiller=%#x abBuf=%.*Rhxs\n",
                                       cbIdtr, off, cbLimit, bFiller, off + cbBuf, abBuf);
                }

                if (off > 0 && !ASMMemIsAllU8(abBuf, off, bFiller))
                    Bs3TestFailedF("Leading bytes touched (#9): cbIdtr=%u off=%u cbLimit=%u bFiller=%#x abBuf=%.*Rhxs\n",
                                   cbIdtr, off, cbLimit, bFiller, off + cbBuf, abBuf);
                if (!ASMMemIsAllU8(&abBuf[off + cbIdtr], sizeof(abBuf) - off - cbIdtr, bFiller))
                    Bs3TestFailedF("Trailing bytes touched (#9): cbIdtr=%u off=%u cbLimit=%u bFiller=%#x abBuf=%.*Rhxs\n",
                                   cbIdtr, off, cbLimit, bFiller, off + cbBuf, abBuf);

                g_usBs3TestStep++;
            }
        }

        Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, pWorker->fSs ? &Ctx.ss : &Ctx.ds, abBuf);
        CtxUdExpected.rbx.u = Ctx.rbx.u;
        CtxUdExpected.ss = Ctx.ss;
        CtxUdExpected.ds = Ctx.ds;
    }

    /*
     * Play with the paging.
     */
    if (   BS3_MODE_IS_PAGED(bTestMode)
        && (!pWorker->fSs || bRing == 3) /* SS.DPL == CPL, we'll get some tiled ring-3 selector here.  */
        && (pbTest = (uint8_t BS3_FAR *)Bs3MemGuardedTestPageAlloc(BS3MEMKIND_TILED)) != NULL)
    {
        RTCCUINTXREG uFlatTest = Bs3SelPtrToFlat(pbTest);

        /*
         * Slide the buffer towards the trailing guard page.  We'll observe the
         * first word being written entirely separately from the 2nd dword/qword.
         */
        for (off = X86_PAGE_SIZE - cbIdtr - 4; off < X86_PAGE_SIZE + 4; off++)
        {
            Bs3MemSet(&pbTest[X86_PAGE_SIZE - cbIdtr * 2], bFiller, cbIdtr * 2);
            Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, pWorker->fSs ? &Ctx.ss : &Ctx.ds, &pbTest[off]);
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (off + cbIdtr <= X86_PAGE_SIZE)
            {
                CtxUdExpected.rbx = Ctx.rbx;
                CtxUdExpected.ss  = Ctx.ss;
                CtxUdExpected.ds  = Ctx.ds;
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                if (Bs3MemCmp(&pbTest[off], pbExpected, cbIdtr) != 0)
                    Bs3TestFailedF("Mismatch (#9): expected %.*Rhxs, got %.*Rhxs\n", cbIdtr, pbExpected, cbIdtr, &pbTest[off]);
            }
            else
            {
                bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx, X86_TRAP_PF_RW | (Ctx.bCpl == 3 ? X86_TRAP_PF_US : 0),
                                          uFlatTest + RT_MAX(off, X86_PAGE_SIZE), 0 /*cbIpAdjust*/);
                if (   off <= X86_PAGE_SIZE - 2
                    && Bs3MemCmp(&pbTest[off], pbExpected, 2) != 0)
                    Bs3TestFailedF("Mismatch (#10): Expected limit %.2Rhxs, got %.2Rhxs; off=%#x\n",
                                   pbExpected, &pbTest[off], off);
                if (   off < X86_PAGE_SIZE - 2
                    && !ASMMemIsAllU8(&pbTest[off + 2], X86_PAGE_SIZE - off - 2, bFiller))
                    Bs3TestFailedF("Wrote partial base on #PF (#10): bFiller=%#x, got %.*Rhxs; off=%#x\n",
                                   bFiller, X86_PAGE_SIZE - off - 2, &pbTest[off + 2], off);
                if (off == X86_PAGE_SIZE - 1 && pbTest[off] != bFiller)
                    Bs3TestFailedF("Wrote partial limit on #PF (#10): Expected %02x, got %02x\n", bFiller, pbTest[off]);
            }
            g_usBs3TestStep++;
        }

        /*
         * Now, do it the other way around. It should look normal now since writing
         * the limit will #PF first and nothing should be written.
         */
        for (off = cbIdtr + 4; off >= -cbIdtr - 4; off--)
        {
            Bs3MemSet(pbTest, bFiller, 48);
            Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, pWorker->fSs ? &Ctx.ss : &Ctx.ds, &pbTest[off]);
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (off >= 0)
            {
                CtxUdExpected.rbx = Ctx.rbx;
                CtxUdExpected.ss  = Ctx.ss;
                CtxUdExpected.ds  = Ctx.ds;
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                if (Bs3MemCmp(&pbTest[off], pbExpected, cbIdtr) != 0)
                    Bs3TestFailedF("Mismatch (#11): expected %.*Rhxs, got %.*Rhxs\n", cbIdtr, pbExpected, cbIdtr, &pbTest[off]);
            }
            else
            {
                bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx, X86_TRAP_PF_RW | (Ctx.bCpl == 3 ? X86_TRAP_PF_US : 0),
                                          uFlatTest + off, 0 /*cbIpAdjust*/);
                if (   -off < cbIdtr
                    && !ASMMemIsAllU8(pbTest, cbIdtr + off, bFiller))
                    Bs3TestFailedF("Wrote partial content on #PF (#12): bFiller=%#x, found %.*Rhxs; off=%d\n",
                                   bFiller, cbIdtr + off, pbTest, off);
            }
            if (!ASMMemIsAllU8(&pbTest[RT_MAX(cbIdtr + off, 0)], 16, bFiller))
                Bs3TestFailedF("Wrote beyond expected area (#13): bFiller=%#x, found %.16Rhxs; off=%d\n",
                               bFiller, &pbTest[RT_MAX(cbIdtr + off, 0)], off);
            g_usBs3TestStep++;
        }

        /*
         * Combine paging and segment limit and check ordering.
         * This is kind of interesting here since it the instruction seems to
         * be doing two separate writes.
         */
        if (   !BS3_MODE_IS_RM_OR_V86(bTestMode)
            && !BS3_MODE_IS_64BIT_CODE(bTestMode))
        {
            uint16_t cbLimit;

            Bs3GdteTestPage00 = Bs3Gdte_DATA16;
            Bs3GdteTestPage00.Gen.u2Dpl       = bRing;
            Bs3GdteTestPage00.Gen.u16BaseLow  = (uint16_t)uFlatTest;
            Bs3GdteTestPage00.Gen.u8BaseHigh1 = (uint8_t)(uFlatTest >> 16);
            Bs3GdteTestPage00.Gen.u8BaseHigh2 = (uint8_t)(uFlatTest >> 24);

            if (pWorker->fSs)
                CtxUdExpected.ss = Ctx.ss = BS3_SEL_TEST_PAGE_00 | bRing;
            else
                CtxUdExpected.ds = Ctx.ds = BS3_SEL_TEST_PAGE_00 | bRing;

            /* Expand up (normal), approaching tail guard page. */
            for (off = X86_PAGE_SIZE - cbIdtr - 4; off < X86_PAGE_SIZE + 4; off++)
            {
                CtxUdExpected.rbx.u = Ctx.rbx.u = off;
                for (cbLimit = X86_PAGE_SIZE - cbIdtr*2; cbLimit < X86_PAGE_SIZE + cbIdtr*2; cbLimit++)
                {
                    Bs3GdteTestPage00.Gen.u16LimitLow = cbLimit;
                    Bs3MemSet(&pbTest[X86_PAGE_SIZE - cbIdtr * 2], bFiller, cbIdtr * 2);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (off + cbIdtr <= cbLimit + 1)
                    {
                        /* No #GP, but maybe #PF. */
                        if (off + cbIdtr <= X86_PAGE_SIZE)
                        {
                            bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                            if (Bs3MemCmp(&pbTest[off], pbExpected, cbIdtr) != 0)
                                Bs3TestFailedF("Mismatch (#14): expected %.*Rhxs, got %.*Rhxs\n",
                                               cbIdtr, pbExpected, cbIdtr, &pbTest[off]);
                        }
                        else
                        {
                            bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx, X86_TRAP_PF_RW | (Ctx.bCpl == 3 ? X86_TRAP_PF_US : 0),
                                                      uFlatTest + RT_MAX(off, X86_PAGE_SIZE), 0 /*cbIpAdjust*/);
                            if (   off <= X86_PAGE_SIZE - 2
                                && Bs3MemCmp(&pbTest[off], pbExpected, 2) != 0)
                                Bs3TestFailedF("Mismatch (#15): Expected limit %.2Rhxs, got %.2Rhxs; off=%#x\n",
                                               pbExpected, &pbTest[off], off);
                            cb = X86_PAGE_SIZE - off - 2;
                            if (   off < X86_PAGE_SIZE - 2
                                && !ASMMemIsAllU8(&pbTest[off + 2], cb, bFiller))
                                Bs3TestFailedF("Wrote partial base on #PF (#15): bFiller=%#x, got %.*Rhxs; off=%#x\n",
                                               bFiller, cb, &pbTest[off + 2], off);
                            if (off == X86_PAGE_SIZE - 1 && pbTest[off] != bFiller)
                                Bs3TestFailedF("Wrote partial limit on #PF (#15): Expected %02x, got %02x\n", bFiller, pbTest[off]);
                        }
                    }
                    else if (off + 2 <= cbLimit + 1)
                    {
                        /* [ig]tr.limit writing does not cause #GP, but may cause #PG, if not writing the base causes #GP. */
                        if (off <= X86_PAGE_SIZE - 2)
                        {
                            if (pWorker->fSs)
                                bs3CpuBasic2_CompareSsCtx(&TrapCtx, &Ctx, 0, false /*f486ResumeFlagHint*/);
                            else
                                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                            if (Bs3MemCmp(&pbTest[off], pbExpected, 2) != 0)
                                Bs3TestFailedF("Mismatch (#16): Expected limit %.2Rhxs, got %.2Rhxs; off=%#x\n",
                                               pbExpected, &pbTest[off], off);
                            cb = X86_PAGE_SIZE - off - 2;
                            if (   off < X86_PAGE_SIZE - 2
                                && !ASMMemIsAllU8(&pbTest[off + 2], cb, bFiller))
                                Bs3TestFailedF("Wrote partial base with limit (#16): bFiller=%#x, got %.*Rhxs; off=%#x\n",
                                               bFiller, cb, &pbTest[off + 2], off);
                        }
                        else
                        {
                            bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx, X86_TRAP_PF_RW | (Ctx.bCpl == 3 ? X86_TRAP_PF_US : 0),
                                                      uFlatTest + RT_MAX(off, X86_PAGE_SIZE), 0 /*cbIpAdjust*/);
                            if (   off < X86_PAGE_SIZE
                                && !ASMMemIsAllU8(&pbTest[off], X86_PAGE_SIZE - off, bFiller))
                                Bs3TestFailedF("Mismatch (#16): Partial limit write on #PF: bFiller=%#x, got %.*Rhxs\n",
                                               bFiller, X86_PAGE_SIZE - off, &pbTest[off]);
                        }
                    }
                    else
                    {
                        /* #GP/#SS on limit. */
                        if (pWorker->fSs)
                            bs3CpuBasic2_CompareSsCtx(&TrapCtx, &Ctx, 0, false /*f486ResumeFlagHint*/);
                        else
                            bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                        if (   off < X86_PAGE_SIZE
                            && !ASMMemIsAllU8(&pbTest[off], X86_PAGE_SIZE - off, bFiller))
                            Bs3TestFailedF("Mismatch (#17): Partial write on #GP: bFiller=%#x, got %.*Rhxs\n",
                                           bFiller, X86_PAGE_SIZE - off, &pbTest[off]);
                    }

                    cb = RT_MIN(cbIdtr * 2, off - (X86_PAGE_SIZE - cbIdtr*2));
                    if (!ASMMemIsAllU8(&pbTest[X86_PAGE_SIZE - cbIdtr * 2], cb, bFiller))
                        Bs3TestFailedF("Leading bytes touched (#18): cbIdtr=%u off=%u cbLimit=%u bFiller=%#x pbTest=%.*Rhxs\n",
                                       cbIdtr, off, cbLimit, bFiller, cb, pbTest[X86_PAGE_SIZE - cbIdtr * 2]);

                    g_usBs3TestStep++;

                    /* Set DS to 0 and check that we get #GP(0). */
                    if (!pWorker->fSs)
                    {
                        Ctx.ds = 0;
                        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                        Ctx.ds = BS3_SEL_TEST_PAGE_00 | bRing;
                        g_usBs3TestStep++;
                    }
                }
            }

            /* Expand down. */
            pbTest    -= X86_PAGE_SIZE; /* Note! we're backing up a page to simplify things */
            uFlatTest -= X86_PAGE_SIZE;

            Bs3GdteTestPage00.Gen.u4Type = X86_SEL_TYPE_RW_DOWN_ACC;
            Bs3GdteTestPage00.Gen.u16BaseLow  = (uint16_t)uFlatTest;
            Bs3GdteTestPage00.Gen.u8BaseHigh1 = (uint8_t)(uFlatTest >> 16);
            Bs3GdteTestPage00.Gen.u8BaseHigh2 = (uint8_t)(uFlatTest >> 24);

            for (off = X86_PAGE_SIZE - cbIdtr - 4; off < X86_PAGE_SIZE + 4; off++)
            {
                CtxUdExpected.rbx.u = Ctx.rbx.u = off;
                for (cbLimit = X86_PAGE_SIZE - cbIdtr*2; cbLimit < X86_PAGE_SIZE + cbIdtr*2; cbLimit++)
                {
                    Bs3GdteTestPage00.Gen.u16LimitLow = cbLimit;
                    Bs3MemSet(&pbTest[X86_PAGE_SIZE], bFiller, cbIdtr * 2);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (cbLimit < off && off >= X86_PAGE_SIZE)
                    {
                        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                        if (Bs3MemCmp(&pbTest[off], pbExpected, cbIdtr) != 0)
                            Bs3TestFailedF("Mismatch (#19): expected %.*Rhxs, got %.*Rhxs\n",
                                           cbIdtr, pbExpected, cbIdtr, &pbTest[off]);
                        cb = X86_PAGE_SIZE + cbIdtr*2 - off;
                        if (!ASMMemIsAllU8(&pbTest[off + cbIdtr], cb, bFiller))
                            Bs3TestFailedF("Trailing bytes touched (#20): cbIdtr=%u off=%u cbLimit=%u bFiller=%#x pbTest=%.*Rhxs\n",
                                           cbIdtr, off, cbLimit, bFiller, cb, pbTest[off + cbIdtr]);
                    }
                    else
                    {
                        if (cbLimit < off && off < X86_PAGE_SIZE)
                            bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx, X86_TRAP_PF_RW | (Ctx.bCpl == 3 ? X86_TRAP_PF_US : 0),
                                                      uFlatTest + off, 0 /*cbIpAdjust*/);
                        else if (pWorker->fSs)
                            bs3CpuBasic2_CompareSsCtx(&TrapCtx, &Ctx, 0, false /*f486ResumeFlagHint*/);
                        else
                            bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                        cb = cbIdtr*2;
                        if (!ASMMemIsAllU8(&pbTest[X86_PAGE_SIZE], cb, bFiller))
                            Bs3TestFailedF("Trailing bytes touched (#20): cbIdtr=%u off=%u cbLimit=%u bFiller=%#x pbTest=%.*Rhxs\n",
                                           cbIdtr, off, cbLimit, bFiller, cb, pbTest[X86_PAGE_SIZE]);
                    }
                    g_usBs3TestStep++;
                }
            }

            pbTest    += X86_PAGE_SIZE;
            uFlatTest += X86_PAGE_SIZE;
        }

        Bs3MemGuardedTestPageFree(pbTest);
    }

    /*
     * Check non-canonical 64-bit space.
     */
    if (   BS3_MODE_IS_64BIT_CODE(bTestMode)
        && (pbTest = (uint8_t BS3_FAR *)Bs3PagingSetupCanonicalTraps()) != NULL)
    {
        /* Make our references relative to the gap. */
        pbTest += g_cbBs3PagingOneCanonicalTrap;

        /* Hit it from below. */
        for (off = -cbIdtr - 8; off < cbIdtr + 8; off++)
        {
            Ctx.rbx.u = CtxUdExpected.rbx.u = UINT64_C(0x0000800000000000) + off;
            Bs3MemSet(&pbTest[-64], bFiller, 64*2);
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (off + cbIdtr <= 0)
            {
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                if (Bs3MemCmp(&pbTest[off], pbExpected, cbIdtr) != 0)
                    Bs3TestFailedF("Mismatch (#21): expected %.*Rhxs, got %.*Rhxs\n", cbIdtr, pbExpected, cbIdtr, &pbTest[off]);
            }
            else
            {
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                if (off <= -2 && Bs3MemCmp(&pbTest[off], pbExpected, 2) != 0)
                    Bs3TestFailedF("Mismatch (#21): expected limit %.2Rhxs, got %.2Rhxs\n", pbExpected, &pbTest[off]);
                off2 = off <= -2 ? 2 : 0;
                cb   = cbIdtr - off2;
                if (!ASMMemIsAllU8(&pbTest[off + off2], cb, bFiller))
                    Bs3TestFailedF("Mismatch (#21): touched base %.*Rhxs, got %.*Rhxs\n",
                                   cb, &pbExpected[off], cb, &pbTest[off + off2]);
            }
            if (!ASMMemIsAllU8(&pbTest[off - 16], 16, bFiller))
                Bs3TestFailedF("Leading bytes touched (#21): bFiller=%#x, got %.16Rhxs\n", bFiller, &pbTest[off]);
            if (!ASMMemIsAllU8(&pbTest[off + cbIdtr], 16, bFiller))
                Bs3TestFailedF("Trailing bytes touched (#21): bFiller=%#x, got %.16Rhxs\n", bFiller, &pbTest[off + cbIdtr]);
        }

        /* Hit it from above. */
        for (off = -cbIdtr - 8; off < cbIdtr + 8; off++)
        {
            Ctx.rbx.u = CtxUdExpected.rbx.u = UINT64_C(0xffff800000000000) + off;
            Bs3MemSet(&pbTest[-64], bFiller, 64*2);
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (off >= 0)
            {
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                if (Bs3MemCmp(&pbTest[off], pbExpected, cbIdtr) != 0)
                    Bs3TestFailedF("Mismatch (#22): expected %.*Rhxs, got %.*Rhxs\n", cbIdtr, pbExpected, cbIdtr, &pbTest[off]);
            }
            else
            {
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                if (!ASMMemIsAllU8(&pbTest[off], cbIdtr, bFiller))
                    Bs3TestFailedF("Mismatch (#22): touched base %.*Rhxs, got %.*Rhxs\n",
                                   cbIdtr, &pbExpected[off], cbIdtr, &pbTest[off]);
            }
            if (!ASMMemIsAllU8(&pbTest[off - 16], 16, bFiller))
                Bs3TestFailedF("Leading bytes touched (#22): bFiller=%#x, got %.16Rhxs\n", bFiller, &pbTest[off]);
            if (!ASMMemIsAllU8(&pbTest[off + cbIdtr], 16, bFiller))
                Bs3TestFailedF("Trailing bytes touched (#22): bFiller=%#x, got %.16Rhxs\n", bFiller, &pbTest[off + cbIdtr]);
        }

    }
}


static void bs3CpuBasic2_sidt_sgdt_Common(uint8_t bTestMode, BS3CB2SIDTSGDT const BS3_FAR *paWorkers, unsigned cWorkers,
                                          uint8_t const *pbExpected)
{
    unsigned idx;
    unsigned bRing;
    unsigned iStep = 0;

    /* Note! We skip the SS checks for ring-0 since we badly mess up SS in the
             test and don't want to bother with double faults. */
    for (bRing = 0; bRing <= 3; bRing++)
    {
        for (idx = 0; idx < cWorkers; idx++)
            if (    (paWorkers[idx].bMode & (bTestMode & BS3_MODE_CODE_MASK))
                && (!paWorkers[idx].fSs || bRing != 0 /** @todo || BS3_MODE_IS_64BIT_SYS(bTestMode)*/ ))
            {
                g_usBs3TestStep = iStep;
                bs3CpuBasic2_sidt_sgdt_One(&paWorkers[idx], bTestMode, bRing, pbExpected);
                iStep += 1000;
            }
        if (BS3_MODE_IS_RM_OR_V86(bTestMode))
            break;
    }
}


BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuBasic2_sidt)(uint8_t bMode)
{
    union
    {
        RTIDTR  Idtr;
        uint8_t ab[16];
    } Expected;

    //if (bMode != BS3_MODE_LM64) return BS3TESTDOMODE_SKIPPED;
    bs3CpuBasic2_SetGlobals(bMode);

    /*
     * Pass to common worker which is only compiled once per mode.
     */
    Bs3MemZero(&Expected, sizeof(Expected));
    ASMGetIDTR(&Expected.Idtr);
    bs3CpuBasic2_sidt_sgdt_Common(bMode, g_aSidtWorkers, RT_ELEMENTS(g_aSidtWorkers), Expected.ab);

    /*
     * Re-initialize the IDT.
     */
    Bs3TrapReInit();
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuBasic2_sgdt)(uint8_t bMode)
{
    uint64_t const uOrgAddr = Bs3Lgdt_Gdt.uAddr;
    uint64_t       uNew     = 0;
    union
    {
        RTGDTR  Gdtr;
        uint8_t ab[16];
    } Expected;

    //if (bMode != BS3_MODE_LM64) return BS3TESTDOMODE_SKIPPED;
    bs3CpuBasic2_SetGlobals(bMode);

    /*
     * If paged mode, try push the GDT way up.
     */
    Bs3MemZero(&Expected, sizeof(Expected));
    ASMGetGDTR(&Expected.Gdtr);
    if (BS3_MODE_IS_PAGED(bMode))
    {
/** @todo loading non-canonical base addresses.   */
        int rc;
        uNew  = BS3_MODE_IS_64BIT_SYS(bMode) ? UINT64_C(0xffff80fedcb70000) : UINT64_C(0xc2d28000);
        uNew |= uOrgAddr & X86_PAGE_OFFSET_MASK;
        rc = Bs3PagingAlias(uNew, uOrgAddr, Bs3Lgdt_Gdt.cb, X86_PTE_P | X86_PTE_RW | X86_PTE_US | X86_PTE_D | X86_PTE_A);
        if (RT_SUCCESS(rc))
        {
            Bs3Lgdt_Gdt.uAddr = uNew;
            Bs3UtilSetFullGdtr(Bs3Lgdt_Gdt.cb, uNew);
            ASMGetGDTR(&Expected.Gdtr);
            if (BS3_MODE_IS_64BIT_SYS(bMode) && ARCH_BITS != 64)
                *(uint32_t *)&Expected.ab[6] = (uint32_t)(uNew >> 32);
        }
    }

    /*
     * Pass to common worker which is only compiled once per mode.
     */
    bs3CpuBasic2_sidt_sgdt_Common(bMode, g_aSgdtWorkers, RT_ELEMENTS(g_aSgdtWorkers), Expected.ab);

    /*
     * Unalias the GDT.
     */
    if (uNew != 0)
    {
        Bs3Lgdt_Gdt.uAddr = uOrgAddr;
        Bs3UtilSetFullGdtr(Bs3Lgdt_Gdt.cb, uOrgAddr);
        Bs3PagingUnalias(uNew, Bs3Lgdt_Gdt.cb);
    }

    /*
     * Re-initialize the IDT.
     */
    Bs3TrapReInit();
    return 0;
}



/*
 * LIDT & LGDT
 */

/**
 * Executes one round of LIDT and LGDT tests using one assembly worker.
 *
 * This is written with driving everything from the 16-bit or 32-bit worker in
 * mind, i.e. not assuming the test bitcount is the same as the current.
 */
static void bs3CpuBasic2_lidt_lgdt_One(BS3CB2SIDTSGDT const BS3_FAR *pWorker, uint8_t bTestMode, uint8_t bRing,
                                       uint8_t const *pbRestore, size_t cbRestore, uint8_t const *pbExpected)
{
    static const struct
    {
        bool        fGP;
        uint16_t    cbLimit;
        uint64_t    u64Base;
    } s_aValues64[] =
    {
        { false, 0x0000, UINT64_C(0x0000000000000000) },
        { false, 0x0001, UINT64_C(0x0000000000000001) },
        { false, 0x0002, UINT64_C(0x0000000000000010) },
        { false, 0x0003, UINT64_C(0x0000000000000123) },
        { false, 0x0004, UINT64_C(0x0000000000001234) },
        { false, 0x0005, UINT64_C(0x0000000000012345) },
        { false, 0x0006, UINT64_C(0x0000000000123456) },
        { false, 0x0007, UINT64_C(0x0000000001234567) },
        { false, 0x0008, UINT64_C(0x0000000012345678) },
        { false, 0x0009, UINT64_C(0x0000000123456789) },
        { false, 0x000a, UINT64_C(0x000000123456789a) },
        { false, 0x000b, UINT64_C(0x00000123456789ab) },
        { false, 0x000c, UINT64_C(0x0000123456789abc) },
        { false, 0x001c, UINT64_C(0x00007ffffeefefef) },
        { false, 0xffff, UINT64_C(0x00007fffffffffff) },
        {  true, 0xf3f1, UINT64_C(0x0000800000000000) },
        {  true, 0x0000, UINT64_C(0x0000800000000000) },
        {  true, 0x0000, UINT64_C(0x0000800000000333) },
        {  true, 0x00f0, UINT64_C(0x0001000000000000) },
        {  true, 0x0ff0, UINT64_C(0x0012000000000000) },
        {  true, 0x0eff, UINT64_C(0x0123000000000000) },
        {  true, 0xe0fe, UINT64_C(0x1234000000000000) },
        {  true, 0x00ad, UINT64_C(0xffff300000000000) },
        {  true, 0x0000, UINT64_C(0xffff7fffffffffff) },
        {  true, 0x00f0, UINT64_C(0xffff7fffffffffff) },
        { false, 0x5678, UINT64_C(0xffff800000000000) },
        { false, 0x2969, UINT64_C(0xffffffffffeefefe) },
        { false, 0x1221, UINT64_C(0xffffffffffffffff) },
        { false, 0x1221, UINT64_C(0xffffffffffffffff) },
    };
    static const struct
    {
        uint16_t    cbLimit;
        uint32_t    u32Base;
    } s_aValues32[] =
    {
        { 0xdfdf, UINT32_C(0xefefefef) },
        { 0x0000, UINT32_C(0x00000000) },
        { 0x0001, UINT32_C(0x00000001) },
        { 0x0002, UINT32_C(0x00000012) },
        { 0x0003, UINT32_C(0x00000123) },
        { 0x0004, UINT32_C(0x00001234) },
        { 0x0005, UINT32_C(0x00012345) },
        { 0x0006, UINT32_C(0x00123456) },
        { 0x0007, UINT32_C(0x01234567) },
        { 0x0008, UINT32_C(0x12345678) },
        { 0x0009, UINT32_C(0x80204060) },
        { 0x000a, UINT32_C(0xddeeffaa) },
        { 0x000b, UINT32_C(0xfdecdbca) },
        { 0x000c, UINT32_C(0x6098456b) },
        { 0x000d, UINT32_C(0x98506099) },
        { 0x000e, UINT32_C(0x206950bc) },
        { 0x000f, UINT32_C(0x9740395d) },
        { 0x0334, UINT32_C(0x64a9455e) },
        { 0xb423, UINT32_C(0xd20b6eff) },
        { 0x4955, UINT32_C(0x85296d46) },
        { 0xffff, UINT32_C(0x07000039) },
        { 0xefe1, UINT32_C(0x0007fe00) },
    };

    BS3TRAPFRAME        TrapCtx;
    BS3REGCTX           Ctx;
    BS3REGCTX           CtxUdExpected;
    BS3REGCTX           TmpCtx;
    uint8_t             abBufLoad[40];          /* Test buffer w/ misalignment test space and some (cbIdtr) extra guard. */
    uint8_t             abBufSave[32];          /* For saving the result after loading. */
    uint8_t             abBufRestore[24];       /* For restoring sane value (same seg as abBufSave!). */
    uint8_t             abExpectedFilled[32];   /* Same as pbExpected, except it's filled with bFiller2 instead of zeros. */
    uint8_t BS3_FAR    *pbBufSave;              /* Correctly aligned pointer into abBufSave. */
    uint8_t BS3_FAR    *pbBufRestore;           /* Correctly aligned pointer into abBufRestore. */
    uint8_t const       cbIdtr        = BS3_MODE_IS_64BIT_CODE(bTestMode) ? 2+8 : 2+4;
    uint8_t const       cbBaseLoaded  = BS3_MODE_IS_64BIT_CODE(bTestMode) ? 8
                                      : BS3_MODE_IS_16BIT_CODE(bTestMode) == !(pWorker->fFlags & BS3CB2SIDTSGDT_F_OPSIZE)
                                      ? 3 : 4;
    bool const          f286          = (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) == BS3CPU_80286;
    uint8_t const       bTop16BitBase = f286 ? 0xff : 0x00;
    uint8_t             bFiller1;               /* For filling abBufLoad.  */
    uint8_t             bFiller2;               /* For filling abBufSave and expectations. */
    int                 off;
    uint8_t BS3_FAR    *pbTest;
    unsigned            i;

    /* make sure they're allocated  */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&CtxUdExpected, sizeof(CtxUdExpected));
    Bs3MemZero(&TmpCtx, sizeof(TmpCtx));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));
    Bs3MemZero(abBufSave, sizeof(abBufSave));
    Bs3MemZero(abBufLoad, sizeof(abBufLoad));
    Bs3MemZero(abBufRestore, sizeof(abBufRestore));

    /*
     * Create a context, giving this routine some more stack space.
     *  - Point the context at our LIDT [xBX] + SIDT [xDI] + LIDT [xSI] + UD2 combo.
     *  - Point DS/SS:xBX at abBufLoad.
     *  - Point ES:xDI at abBufSave.
     *  - Point ES:xSI at abBufRestore.
     */
    Bs3RegCtxSaveEx(&Ctx, bTestMode, 256 /*cbExtraStack*/);
    Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, pWorker->fpfnWorker);
    if (BS3_MODE_IS_16BIT_SYS(bTestMode))
        g_uBs3TrapEipHint = Ctx.rip.u32;
    Ctx.rflags.u16 &= ~X86_EFL_IF;
    Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, pWorker->fSs ? &Ctx.ss : &Ctx.ds, abBufLoad);

    pbBufSave = abBufSave;
    if ((BS3_FP_OFF(pbBufSave) + 2) & 7)
        pbBufSave += 8 - ((BS3_FP_OFF(pbBufSave) + 2) & 7);
    Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rdi, &Ctx.es, pbBufSave);

    pbBufRestore = abBufRestore;
    if ((BS3_FP_OFF(pbBufRestore) + 2) & 7)
        pbBufRestore += 8 - ((BS3_FP_OFF(pbBufRestore) + 2) & 7);
    Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rsi, &Ctx.es, pbBufRestore);
    Bs3MemCpy(pbBufRestore, pbRestore, cbRestore);

    if (!BS3_MODE_IS_RM_OR_V86(bTestMode))
        Bs3RegCtxConvertToRingX(&Ctx, bRing);

    /* For successful SIDT attempts, we'll stop at the UD2. */
    Bs3MemCpy(&CtxUdExpected, &Ctx, sizeof(Ctx));
    CtxUdExpected.rip.u += pWorker->cbInstr;

    /*
     * Check that it works at all.
     */
    Bs3MemZero(abBufLoad, sizeof(abBufLoad));
    Bs3MemCpy(abBufLoad, pbBufRestore, cbIdtr);
    Bs3MemZero(abBufSave, sizeof(abBufSave));
    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
    if (bRing != 0)
        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
    else
    {
        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
        if (Bs3MemCmp(pbBufSave, pbExpected, cbIdtr * 2) != 0)
            Bs3TestFailedF("Mismatch (%s, #1): expected %.*Rhxs, got %.*Rhxs\n",
                           pWorker->pszDesc, cbIdtr*2, pbExpected, cbIdtr*2, pbBufSave);
    }
    g_usBs3TestStep++;

    /* Determine two filler bytes that doesn't appear in the previous result or our expectations. */
    bFiller1 = ~0x55;
    while (   Bs3MemChr(pbBufSave, bFiller1, cbIdtr) != NULL
           || Bs3MemChr(pbRestore, bFiller1, cbRestore) != NULL
           || bFiller1 == 0xff)
        bFiller1++;
    bFiller2 = 0x33;
    while (   Bs3MemChr(pbBufSave, bFiller2, cbIdtr) != NULL
           || Bs3MemChr(pbRestore, bFiller2, cbRestore) != NULL
           || bFiller2 == 0xff
           || bFiller2 == bFiller1)
        bFiller2++;
    Bs3MemSet(abExpectedFilled, bFiller2, sizeof(abExpectedFilled));
    Bs3MemCpy(abExpectedFilled, pbExpected, cbIdtr);

    /* Again with a buffer filled with a byte not occuring in the previous result. */
    Bs3MemSet(abBufLoad, bFiller1, sizeof(abBufLoad));
    Bs3MemCpy(abBufLoad, pbBufRestore, cbIdtr);
    Bs3MemSet(abBufSave, bFiller2, sizeof(abBufSave));
    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
    if (bRing != 0)
        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
    else
    {
        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
        if (Bs3MemCmp(pbBufSave, abExpectedFilled, cbIdtr * 2) != 0)
            Bs3TestFailedF("Mismatch (%s, #2): expected %.*Rhxs, got %.*Rhxs\n",
                           pWorker->pszDesc, cbIdtr*2, abExpectedFilled, cbIdtr*2, pbBufSave);
    }
    g_usBs3TestStep++;

    /*
     * Try loading a bunch of different limit+base value to check what happens,
     * especially what happens wrt the top part of the base in 16-bit mode.
     */
    if (BS3_MODE_IS_64BIT_CODE(bTestMode))
    {
        for (i = 0; i < RT_ELEMENTS(s_aValues64); i++)
        {
            Bs3MemSet(abBufLoad, bFiller1, sizeof(abBufLoad));
            Bs3MemCpy(&abBufLoad[0], &s_aValues64[i].cbLimit, 2);
            Bs3MemCpy(&abBufLoad[2], &s_aValues64[i].u64Base, 8);
            Bs3MemSet(abBufSave, bFiller2, sizeof(abBufSave));
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (bRing != 0 || s_aValues64[i].fGP)
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
            else
            {
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                if (   Bs3MemCmp(&pbBufSave[0], &s_aValues64[i].cbLimit, 2) != 0
                    || Bs3MemCmp(&pbBufSave[2], &s_aValues64[i].u64Base, 8) != 0
                    || !ASMMemIsAllU8(&pbBufSave[10], cbIdtr, bFiller2))
                    Bs3TestFailedF("Mismatch (%s, #2): expected %04RX16:%016RX64, fillers %#x %#x, got %.*Rhxs\n",
                                   pWorker->pszDesc, s_aValues64[i].cbLimit, s_aValues64[i].u64Base,
                                   bFiller1, bFiller2, cbIdtr*2, pbBufSave);
            }
            g_usBs3TestStep++;
        }
    }
    else
    {
        for (i = 0; i < RT_ELEMENTS(s_aValues32); i++)
        {
            Bs3MemSet(abBufLoad, bFiller1, sizeof(abBufLoad));
            Bs3MemCpy(&abBufLoad[0], &s_aValues32[i].cbLimit, 2);
            Bs3MemCpy(&abBufLoad[2], &s_aValues32[i].u32Base, cbBaseLoaded);
            Bs3MemSet(abBufSave, bFiller2, sizeof(abBufSave));
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (bRing != 0)
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
            else
            {
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                if (   Bs3MemCmp(&pbBufSave[0], &s_aValues32[i].cbLimit, 2) != 0
                    || Bs3MemCmp(&pbBufSave[2], &s_aValues32[i].u32Base, cbBaseLoaded) != 0
                    || (   cbBaseLoaded != 4
                        && pbBufSave[2+3] != bTop16BitBase)
                    || !ASMMemIsAllU8(&pbBufSave[8], cbIdtr, bFiller2))
                    Bs3TestFailedF("Mismatch (%s,#3): loaded %04RX16:%08RX32, fillers %#x %#x%s, got %.*Rhxs\n",
                                   pWorker->pszDesc, s_aValues32[i].cbLimit, s_aValues32[i].u32Base, bFiller1, bFiller2,
                                   f286 ? ", 286" : "", cbIdtr*2, pbBufSave);
            }
            g_usBs3TestStep++;
        }
    }

    /*
     * Slide the buffer along 8 bytes to cover misalignment.
     */
    for (off = 0; off < 8; off++)
    {
        Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, pWorker->fSs ? &Ctx.ss : &Ctx.ds, &abBufLoad[off]);
        CtxUdExpected.rbx.u = Ctx.rbx.u;

        Bs3MemSet(abBufLoad, bFiller1, sizeof(abBufLoad));
        Bs3MemCpy(&abBufLoad[off], pbBufRestore, cbIdtr);
        Bs3MemSet(abBufSave, bFiller2, sizeof(abBufSave));
        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        if (bRing != 0)
            bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
        else
        {
            bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
            if (Bs3MemCmp(pbBufSave, abExpectedFilled, cbIdtr * 2) != 0)
                Bs3TestFailedF("Mismatch (%s, #4): expected %.*Rhxs, got %.*Rhxs\n",
                               pWorker->pszDesc, cbIdtr*2, abExpectedFilled, cbIdtr*2, pbBufSave);
        }
        g_usBs3TestStep++;
    }
    Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, pWorker->fSs ? &Ctx.ss : &Ctx.ds, abBufLoad);
    CtxUdExpected.rbx.u = Ctx.rbx.u;

    /*
     * Play with the selector limit if the target mode supports limit checking
     * We use BS3_SEL_TEST_PAGE_00 for this
     */
    if (   !BS3_MODE_IS_RM_OR_V86(bTestMode)
        && !BS3_MODE_IS_64BIT_CODE(bTestMode))
    {
        uint16_t cbLimit;
        uint32_t uFlatBuf = Bs3SelPtrToFlat(abBufLoad);
        Bs3GdteTestPage00 = Bs3Gdte_DATA16;
        Bs3GdteTestPage00.Gen.u2Dpl       = bRing;
        Bs3GdteTestPage00.Gen.u16BaseLow  = (uint16_t)uFlatBuf;
        Bs3GdteTestPage00.Gen.u8BaseHigh1 = (uint8_t)(uFlatBuf >> 16);
        Bs3GdteTestPage00.Gen.u8BaseHigh2 = (uint8_t)(uFlatBuf >> 24);

        if (pWorker->fSs)
            CtxUdExpected.ss = Ctx.ss = BS3_SEL_TEST_PAGE_00 | bRing;
        else
            CtxUdExpected.ds = Ctx.ds = BS3_SEL_TEST_PAGE_00 | bRing;

        /* Expand up (normal). */
        for (off = 0; off < 8; off++)
        {
            CtxUdExpected.rbx.u = Ctx.rbx.u = off;
            for (cbLimit = 0; cbLimit < cbIdtr*2; cbLimit++)
            {
                Bs3GdteTestPage00.Gen.u16LimitLow = cbLimit;

                Bs3MemSet(abBufLoad, bFiller1, sizeof(abBufLoad));
                Bs3MemCpy(&abBufLoad[off], pbBufRestore, cbIdtr);
                Bs3MemSet(abBufSave, bFiller2, sizeof(abBufSave));
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                if (bRing != 0)
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                else if (off + cbIdtr <= cbLimit + 1)
                {
                    bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                    if (Bs3MemCmp(pbBufSave, abExpectedFilled, cbIdtr * 2) != 0)
                        Bs3TestFailedF("Mismatch (%s, #5): expected %.*Rhxs, got %.*Rhxs\n",
                                       pWorker->pszDesc, cbIdtr*2, abExpectedFilled, cbIdtr*2, pbBufSave);
                }
                else if (pWorker->fSs)
                    bs3CpuBasic2_CompareSsCtx(&TrapCtx, &Ctx, 0, false /*f486ResumeFlagHint*/);
                else
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                g_usBs3TestStep++;

                /* Again with zero limit and messed up base (should trigger tripple fault if partially loaded). */
                abBufLoad[off] = abBufLoad[off + 1] = 0;
                abBufLoad[off + 2] |= 1;
                abBufLoad[off + cbIdtr - 2] ^= 0x5a;
                abBufLoad[off + cbIdtr - 1] ^= 0xa5;
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                if (bRing != 0)
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                else if (off + cbIdtr <= cbLimit + 1)
                    bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                else if (pWorker->fSs)
                    bs3CpuBasic2_CompareSsCtx(&TrapCtx, &Ctx, 0, false /*f486ResumeFlagHint*/);
                else
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
            }
        }

        /* Expand down (weird).  Inverted valid area compared to expand up,
           so a limit of zero give us a valid range for 0001..0ffffh (instead of
           a segment with one valid byte at 0000h).  Whereas a limit of 0fffeh
           means one valid byte at 0ffffh, and a limit of 0ffffh means none
           (because in a normal expand up the 0ffffh means all 64KB are
           accessible). */
        Bs3GdteTestPage00.Gen.u4Type = X86_SEL_TYPE_RW_DOWN_ACC;
        for (off = 0; off < 8; off++)
        {
            CtxUdExpected.rbx.u = Ctx.rbx.u = off;
            for (cbLimit = 0; cbLimit < cbIdtr*2; cbLimit++)
            {
                Bs3GdteTestPage00.Gen.u16LimitLow = cbLimit;

                Bs3MemSet(abBufLoad, bFiller1, sizeof(abBufLoad));
                Bs3MemCpy(&abBufLoad[off], pbBufRestore, cbIdtr);
                Bs3MemSet(abBufSave, bFiller2, sizeof(abBufSave));
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                if (bRing != 0)
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                else if (off > cbLimit)
                {
                    bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                    if (Bs3MemCmp(pbBufSave, abExpectedFilled, cbIdtr * 2) != 0)
                        Bs3TestFailedF("Mismatch (%s, #6): expected %.*Rhxs, got %.*Rhxs\n",
                                       pWorker->pszDesc, cbIdtr*2, abExpectedFilled, cbIdtr*2, pbBufSave);
                }
                else if (pWorker->fSs)
                    bs3CpuBasic2_CompareSsCtx(&TrapCtx, &Ctx, 0, false /*f486ResumeFlagHint*/);
                else
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
               g_usBs3TestStep++;

               /* Again with zero limit and messed up base (should trigger triple fault if partially loaded). */
               abBufLoad[off] = abBufLoad[off + 1] = 0;
               abBufLoad[off + 2] |= 3;
               abBufLoad[off + cbIdtr - 2] ^= 0x55;
               abBufLoad[off + cbIdtr - 1] ^= 0xaa;
               Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
               if (bRing != 0)
                   bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
               else if (off > cbLimit)
                   bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
               else if (pWorker->fSs)
                   bs3CpuBasic2_CompareSsCtx(&TrapCtx, &Ctx, 0, false /*f486ResumeFlagHint*/);
               else
                   bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
            }
        }

        Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, pWorker->fSs ? &Ctx.ss : &Ctx.ds, abBufLoad);
        CtxUdExpected.rbx.u = Ctx.rbx.u;
        CtxUdExpected.ss = Ctx.ss;
        CtxUdExpected.ds = Ctx.ds;
    }

    /*
     * Play with the paging.
     */
    if (   BS3_MODE_IS_PAGED(bTestMode)
        && (!pWorker->fSs || bRing == 3) /* SS.DPL == CPL, we'll get some tiled ring-3 selector here.  */
        && (pbTest = (uint8_t BS3_FAR *)Bs3MemGuardedTestPageAlloc(BS3MEMKIND_TILED)) != NULL)
    {
        RTCCUINTXREG uFlatTest = Bs3SelPtrToFlat(pbTest);

        /*
         * Slide the load buffer towards the trailing guard page.
         */
        Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, pWorker->fSs ? &Ctx.ss : &Ctx.ds, &pbTest[X86_PAGE_SIZE]);
        CtxUdExpected.ss = Ctx.ss;
        CtxUdExpected.ds = Ctx.ds;
        for (off = X86_PAGE_SIZE - cbIdtr - 4; off < X86_PAGE_SIZE + 4; off++)
        {
            Bs3MemSet(&pbTest[X86_PAGE_SIZE - cbIdtr * 2], bFiller1, cbIdtr*2);
            if (off < X86_PAGE_SIZE)
                Bs3MemCpy(&pbTest[off], pbBufRestore, RT_MIN(X86_PAGE_SIZE - off, cbIdtr));
            Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, pWorker->fSs ? &Ctx.ss : &Ctx.ds, &pbTest[off]);
            Bs3MemSet(abBufSave, bFiller2, sizeof(abBufSave));
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (bRing != 0)
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
            else if (off + cbIdtr <= X86_PAGE_SIZE)
            {
                CtxUdExpected.rbx = Ctx.rbx;
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                if (Bs3MemCmp(pbBufSave, abExpectedFilled, cbIdtr*2) != 0)
                    Bs3TestFailedF("Mismatch (%s, #7): expected %.*Rhxs, got %.*Rhxs\n",
                                   pWorker->pszDesc, cbIdtr*2, abExpectedFilled, cbIdtr*2, pbBufSave);
            }
            else
                bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx, 0, uFlatTest + RT_MAX(off, X86_PAGE_SIZE), 0 /*cbIpAdjust*/);
            g_usBs3TestStep++;

            /* Again with zero limit and maybe messed up base as well (triple fault if buggy).
               The 386DX-40 here triple faults (or something) with off == 0xffe, nothing else. */
            if (   off < X86_PAGE_SIZE && off + cbIdtr > X86_PAGE_SIZE
                && (   off != X86_PAGE_SIZE - 2
                    || (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) != BS3CPU_80386)
                )
            {
                pbTest[off] = 0;
                if (off + 1 < X86_PAGE_SIZE)
                    pbTest[off + 1] = 0;
                if (off + 2 < X86_PAGE_SIZE)
                    pbTest[off + 2] |= 7;
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                if (bRing != 0)
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                else
                    bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx, 0, uFlatTest + RT_MAX(off, X86_PAGE_SIZE), 0 /*cbIpAdjust*/);
                g_usBs3TestStep++;
            }
        }

        /*
         * Now, do it the other way around. It should look normal now since writing
         * the limit will #PF first and nothing should be written.
         */
        for (off = cbIdtr + 4; off >= -cbIdtr - 4; off--)
        {
            Bs3MemSet(pbTest, bFiller1, 48);
            if (off >= 0)
                Bs3MemCpy(&pbTest[off], pbBufRestore, cbIdtr);
            else if (off + cbIdtr > 0)
                Bs3MemCpy(pbTest, &pbBufRestore[-off], cbIdtr + off);
            Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rbx, pWorker->fSs ? &Ctx.ss : &Ctx.ds, &pbTest[off]);
            Bs3MemSet(abBufSave, bFiller2, sizeof(abBufSave));
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (bRing != 0)
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
            else if (off >= 0)
            {
                CtxUdExpected.rbx = Ctx.rbx;
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                if (Bs3MemCmp(pbBufSave, abExpectedFilled, cbIdtr*2) != 0)
                    Bs3TestFailedF("Mismatch (%s, #8): expected %.*Rhxs, got %.*Rhxs\n",
                                   pWorker->pszDesc, cbIdtr*2, abExpectedFilled, cbIdtr*2, pbBufSave);
            }
            else
                bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx, 0, uFlatTest + off, 0 /*cbIpAdjust*/);
            g_usBs3TestStep++;

            /* Again with messed up base as well (triple fault if buggy). */
            if (off < 0 && off > -cbIdtr)
            {
                if (off + 2 >= 0)
                    pbTest[off + 2] |= 15;
                pbTest[off + cbIdtr - 1] ^= 0xaa;
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                if (bRing != 0)
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                else
                    bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx, 0, uFlatTest + off, 0 /*cbIpAdjust*/);
                g_usBs3TestStep++;
            }
        }

        /*
         * Combine paging and segment limit and check ordering.
         * This is kind of interesting here since it the instruction seems to
         * actually be doing two separate read, just like it's S[IG]DT counterpart.
         *
         * Note! My 486DX4 does a DWORD limit read when the operand size is 32-bit,
         *       that's what f486Weirdness deals with.
         */
        if (   !BS3_MODE_IS_RM_OR_V86(bTestMode)
            && !BS3_MODE_IS_64BIT_CODE(bTestMode))
        {
            bool const f486Weirdness = (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) == BS3CPU_80486
                                    && BS3_MODE_IS_32BIT_CODE(bTestMode) == !(pWorker->fFlags & BS3CB2SIDTSGDT_F_OPSIZE);
            uint16_t   cbLimit;

            Bs3GdteTestPage00 = Bs3Gdte_DATA16;
            Bs3GdteTestPage00.Gen.u2Dpl       = bRing;
            Bs3GdteTestPage00.Gen.u16BaseLow  = (uint16_t)uFlatTest;
            Bs3GdteTestPage00.Gen.u8BaseHigh1 = (uint8_t)(uFlatTest >> 16);
            Bs3GdteTestPage00.Gen.u8BaseHigh2 = (uint8_t)(uFlatTest >> 24);

            if (pWorker->fSs)
                CtxUdExpected.ss = Ctx.ss = BS3_SEL_TEST_PAGE_00 | bRing;
            else
                CtxUdExpected.ds = Ctx.ds = BS3_SEL_TEST_PAGE_00 | bRing;

            /* Expand up (normal), approaching tail guard page. */
            for (off = X86_PAGE_SIZE - cbIdtr - 4; off < X86_PAGE_SIZE + 4; off++)
            {
                CtxUdExpected.rbx.u = Ctx.rbx.u = off;
                for (cbLimit = X86_PAGE_SIZE - cbIdtr*2; cbLimit < X86_PAGE_SIZE + cbIdtr*2; cbLimit++)
                {
                    Bs3GdteTestPage00.Gen.u16LimitLow = cbLimit;
                    Bs3MemSet(&pbTest[X86_PAGE_SIZE - cbIdtr * 2], bFiller1, cbIdtr * 2);
                    if (off < X86_PAGE_SIZE)
                        Bs3MemCpy(&pbTest[off], pbBufRestore, RT_MIN(cbIdtr, X86_PAGE_SIZE - off));
                    Bs3MemSet(abBufSave, bFiller2, sizeof(abBufSave));
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (bRing != 0)
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                    else if (off + cbIdtr <= cbLimit + 1)
                    {
                        /* No #GP, but maybe #PF. */
                        if (off + cbIdtr <= X86_PAGE_SIZE)
                        {
                            bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                            if (Bs3MemCmp(pbBufSave, abExpectedFilled, cbIdtr * 2) != 0)
                                Bs3TestFailedF("Mismatch (%s, #9): expected %.*Rhxs, got %.*Rhxs\n",
                                               pWorker->pszDesc, cbIdtr*2, abExpectedFilled, cbIdtr*2, pbBufSave);
                        }
                        else
                            bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx, 0, uFlatTest + RT_MAX(off, X86_PAGE_SIZE), 0 /*cbIpAdjust*/);
                    }
                    /* No #GP/#SS on limit, but instead #PF? */
                    else if (  !f486Weirdness
                             ? off     < cbLimit && off >= 0xfff
                             : off + 2 < cbLimit && off >= 0xffd)
                        bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx, 0, uFlatTest + RT_MAX(off, X86_PAGE_SIZE), 0 /*cbIpAdjust*/);
                    /* #GP/#SS on limit or base. */
                    else if (pWorker->fSs)
                        bs3CpuBasic2_CompareSsCtx(&TrapCtx, &Ctx, 0, false /*f486ResumeFlagHint*/);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);

                    g_usBs3TestStep++;

                    /* Set DS to 0 and check that we get #GP(0). */
                    if (!pWorker->fSs)
                    {
                        Ctx.ds = 0;
                        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                        Ctx.ds = BS3_SEL_TEST_PAGE_00 | bRing;
                        g_usBs3TestStep++;
                    }
                }
            }

            /* Expand down. */
            pbTest    -= X86_PAGE_SIZE; /* Note! we're backing up a page to simplify things */
            uFlatTest -= X86_PAGE_SIZE;

            Bs3GdteTestPage00.Gen.u4Type = X86_SEL_TYPE_RW_DOWN_ACC;
            Bs3GdteTestPage00.Gen.u16BaseLow  = (uint16_t)uFlatTest;
            Bs3GdteTestPage00.Gen.u8BaseHigh1 = (uint8_t)(uFlatTest >> 16);
            Bs3GdteTestPage00.Gen.u8BaseHigh2 = (uint8_t)(uFlatTest >> 24);

            for (off = X86_PAGE_SIZE - cbIdtr - 4; off < X86_PAGE_SIZE + 4; off++)
            {
                CtxUdExpected.rbx.u = Ctx.rbx.u = off;
                for (cbLimit = X86_PAGE_SIZE - cbIdtr*2; cbLimit < X86_PAGE_SIZE + cbIdtr*2; cbLimit++)
                {
                    Bs3GdteTestPage00.Gen.u16LimitLow = cbLimit;
                    Bs3MemSet(&pbTest[X86_PAGE_SIZE], bFiller1, cbIdtr * 2);
                    if (off >= X86_PAGE_SIZE)
                        Bs3MemCpy(&pbTest[off], pbBufRestore, cbIdtr);
                    else if (off > X86_PAGE_SIZE - cbIdtr)
                        Bs3MemCpy(&pbTest[X86_PAGE_SIZE], &pbBufRestore[X86_PAGE_SIZE - off], cbIdtr - (X86_PAGE_SIZE - off));
                    Bs3MemSet(abBufSave, bFiller2, sizeof(abBufSave));
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (bRing != 0)
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                    else if (cbLimit < off && off >= X86_PAGE_SIZE)
                    {
                        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                        if (Bs3MemCmp(pbBufSave, abExpectedFilled, cbIdtr * 2) != 0)
                            Bs3TestFailedF("Mismatch (%s, #10): expected %.*Rhxs, got %.*Rhxs\n",
                                           pWorker->pszDesc, cbIdtr*2, abExpectedFilled, cbIdtr*2, pbBufSave);
                    }
                    else if (cbLimit < off && off < X86_PAGE_SIZE)
                        bs3CpuBasic2_ComparePfCtx(&TrapCtx, &Ctx, 0, uFlatTest + off, 0 /*cbIpAdjust*/);
                    else if (pWorker->fSs)
                        bs3CpuBasic2_CompareSsCtx(&TrapCtx, &Ctx, 0, false /*f486ResumeFlagHint*/);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
                    g_usBs3TestStep++;
                }
            }

            pbTest    += X86_PAGE_SIZE;
            uFlatTest += X86_PAGE_SIZE;
        }

        Bs3MemGuardedTestPageFree(pbTest);
    }

    /*
     * Check non-canonical 64-bit space.
     */
    if (   BS3_MODE_IS_64BIT_CODE(bTestMode)
        && (pbTest = (uint8_t BS3_FAR *)Bs3PagingSetupCanonicalTraps()) != NULL)
    {
        /* Make our references relative to the gap. */
        pbTest += g_cbBs3PagingOneCanonicalTrap;

        /* Hit it from below. */
        for (off = -cbIdtr - 8; off < cbIdtr + 8; off++)
        {
            Ctx.rbx.u = CtxUdExpected.rbx.u = UINT64_C(0x0000800000000000) + off;
            Bs3MemSet(&pbTest[-64], bFiller1, 64*2);
            Bs3MemCpy(&pbTest[off], pbBufRestore, cbIdtr);
            Bs3MemSet(abBufSave, bFiller2, sizeof(abBufSave));
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (off + cbIdtr > 0 || bRing != 0)
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
            else
            {
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                if (Bs3MemCmp(pbBufSave, abExpectedFilled, cbIdtr * 2) != 0)
                    Bs3TestFailedF("Mismatch (%s, #11): expected %.*Rhxs, got %.*Rhxs\n",
                                   pWorker->pszDesc, cbIdtr*2, abExpectedFilled, cbIdtr*2, pbBufSave);
            }
        }

        /* Hit it from above. */
        for (off = -cbIdtr - 8; off < cbIdtr + 8; off++)
        {
            Ctx.rbx.u = CtxUdExpected.rbx.u = UINT64_C(0xffff800000000000) + off;
            Bs3MemSet(&pbTest[-64], bFiller1, 64*2);
            Bs3MemCpy(&pbTest[off], pbBufRestore, cbIdtr);
            Bs3MemSet(abBufSave, bFiller2, sizeof(abBufSave));
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (off < 0 || bRing != 0)
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
            else
            {
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
                if (Bs3MemCmp(pbBufSave, abExpectedFilled, cbIdtr * 2) != 0)
                    Bs3TestFailedF("Mismatch (%s, #19): expected %.*Rhxs, got %.*Rhxs\n",
                                   pWorker->pszDesc, cbIdtr*2, abExpectedFilled, cbIdtr*2, pbBufSave);
            }
        }

    }
}


static void bs3CpuBasic2_lidt_lgdt_Common(uint8_t bTestMode, BS3CB2SIDTSGDT const BS3_FAR *paWorkers, unsigned cWorkers,
                                          void const *pvRestore, size_t cbRestore, uint8_t const *pbExpected)
{
    unsigned idx;
    unsigned bRing;
    unsigned iStep = 0;

    /* Note! We skip the SS checks for ring-0 since we badly mess up SS in the
             test and don't want to bother with double faults. */
    for (bRing = BS3_MODE_IS_V86(bTestMode) ? 3 : 0; bRing <= 3; bRing++)
    {
        for (idx = 0; idx < cWorkers; idx++)
            if (    (paWorkers[idx].bMode & (bTestMode & BS3_MODE_CODE_MASK))
                && (!paWorkers[idx].fSs || bRing != 0 /** @todo || BS3_MODE_IS_64BIT_SYS(bTestMode)*/ )
                && (   !(paWorkers[idx].fFlags & BS3CB2SIDTSGDT_F_386PLUS)
                    || (   bTestMode > BS3_MODE_PE16
                        || (   bTestMode == BS3_MODE_PE16
                            && (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80386)) ) )
            {
                //Bs3TestPrintf("idx=%-2d fpfnWorker=%p fSs=%d cbInstr=%d\n",
                //              idx, paWorkers[idx].fpfnWorker, paWorkers[idx].fSs, paWorkers[idx].cbInstr);
                g_usBs3TestStep = iStep;
                bs3CpuBasic2_lidt_lgdt_One(&paWorkers[idx], bTestMode, bRing, pvRestore, cbRestore, pbExpected);
                iStep += 1000;
            }
        if (BS3_MODE_IS_RM_SYS(bTestMode))
            break;
    }
}


BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuBasic2_lidt)(uint8_t bMode)
{
    union
    {
        RTIDTR  Idtr;
        uint8_t ab[32]; /* At least cbIdtr*2! */
    } Expected;

    //if (bMode != BS3_MODE_LM64) return 0;
    bs3CpuBasic2_SetGlobals(bMode);

    /*
     * Pass to common worker which is only compiled once per mode.
     */
    Bs3MemZero(&Expected, sizeof(Expected));
    ASMGetIDTR(&Expected.Idtr);

    if (BS3_MODE_IS_RM_SYS(bMode))
        bs3CpuBasic2_lidt_lgdt_Common(bMode, g_aLidtWorkers, RT_ELEMENTS(g_aLidtWorkers),
                                      &Bs3Lidt_Ivt, sizeof(Bs3Lidt_Ivt), Expected.ab);
    else if (BS3_MODE_IS_16BIT_SYS(bMode))
        bs3CpuBasic2_lidt_lgdt_Common(bMode, g_aLidtWorkers, RT_ELEMENTS(g_aLidtWorkers),
                                      &Bs3Lidt_Idt16, sizeof(Bs3Lidt_Idt16), Expected.ab);
    else if (BS3_MODE_IS_32BIT_SYS(bMode))
        bs3CpuBasic2_lidt_lgdt_Common(bMode, g_aLidtWorkers, RT_ELEMENTS(g_aLidtWorkers),
                                      &Bs3Lidt_Idt32, sizeof(Bs3Lidt_Idt32), Expected.ab);
    else
        bs3CpuBasic2_lidt_lgdt_Common(bMode, g_aLidtWorkers, RT_ELEMENTS(g_aLidtWorkers),
                                      &Bs3Lidt_Idt64, sizeof(Bs3Lidt_Idt64), Expected.ab);

    /*
     * Re-initialize the IDT.
     */
    Bs3TrapReInit();
    return 0;
}


BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuBasic2_lgdt)(uint8_t bMode)
{
    union
    {
        RTGDTR  Gdtr;
        uint8_t ab[32]; /* At least cbIdtr*2! */
    } Expected;

    //if (!BS3_MODE_IS_64BIT_SYS(bMode)) return 0;
    bs3CpuBasic2_SetGlobals(bMode);

    /*
     * Pass to common worker which is only compiled once per mode.
     */
    if (BS3_MODE_IS_RM_SYS(bMode))
        ASMSetGDTR((PRTGDTR)&Bs3LgdtDef_Gdt);
    Bs3MemZero(&Expected, sizeof(Expected));
    ASMGetGDTR(&Expected.Gdtr);

    bs3CpuBasic2_lidt_lgdt_Common(bMode, g_aLgdtWorkers, RT_ELEMENTS(g_aLgdtWorkers),
                                  &Bs3LgdtDef_Gdt, sizeof(Bs3LgdtDef_Gdt), Expected.ab);

    /*
     * Re-initialize the IDT.
     */
    Bs3TrapReInit();
    return 0;
}

typedef union IRETBUF
{
    uint64_t        au64[6];  /* max req is 5 */
    uint32_t        au32[12]; /* max req is 9 */
    uint16_t        au16[24]; /* max req is 5 */
    uint8_t         ab[48];
} IRETBUF;
typedef IRETBUF BS3_FAR *PIRETBUF;


static void iretbuf_SetupFrame(PIRETBUF pIretBuf, unsigned const cbPop,
                               uint16_t uCS, uint64_t uPC, uint32_t fEfl, uint16_t uSS, uint64_t uSP)
{
     if (cbPop == 2)
     {
         pIretBuf->au16[0] = (uint16_t)uPC;
         pIretBuf->au16[1] = uCS;
         pIretBuf->au16[2] = (uint16_t)fEfl;
         pIretBuf->au16[3] = (uint16_t)uSP;
         pIretBuf->au16[4] = uSS;
     }
     else if (cbPop != 8)
     {
         pIretBuf->au32[0]   = (uint32_t)uPC;
         pIretBuf->au16[1*2] = uCS;
         pIretBuf->au32[2]   = (uint32_t)fEfl;
         pIretBuf->au32[3]   = (uint32_t)uSP;
         pIretBuf->au16[4*2] = uSS;
     }
     else
     {
         pIretBuf->au64[0]   = uPC;
         pIretBuf->au16[1*4] = uCS;
         pIretBuf->au64[2]   = fEfl;
         pIretBuf->au64[3]   = uSP;
         pIretBuf->au16[4*4] = uSS;
     }
}


static void bs3CpuBasic2_iret_Worker(uint8_t bTestMode, FPFNBS3FAR pfnIret, unsigned const cbPop,
                                     PIRETBUF pIretBuf, const char BS3_FAR *pszDesc)
{
    BS3TRAPFRAME        TrapCtx;
    BS3REGCTX           Ctx;
    BS3REGCTX           CtxUdExpected;
    BS3REGCTX           TmpCtx;
    BS3REGCTX           TmpCtxExpected;
    uint8_t             abLowUd[8];
    uint8_t             abLowIret[8];
    FPFNBS3FAR          pfnUdLow = (FPFNBS3FAR)abLowUd;
    FPFNBS3FAR          pfnIretLow = (FPFNBS3FAR)abLowIret;
    unsigned const      cbSameCplFrame = BS3_MODE_IS_64BIT_CODE(bTestMode) ? 5*cbPop : 3*cbPop;
    bool const          fUseLowCode = cbPop == 2 && !BS3_MODE_IS_16BIT_CODE(bTestMode);
    int                 iRingDst;
    int                 iRingSrc;
    uint16_t            uDplSs;
    uint16_t            uRplCs;
    uint16_t            uRplSs;
//    int                 i;
    uint8_t BS3_FAR    *pbTest;

    NOREF(abLowUd);
#define IRETBUF_SET_SEL(a_idx, a_uValue) \
        do { *(uint16_t)&pIretBuf->ab[a_idx * cbPop] = (a_uValue); } while (0)
#define IRETBUF_SET_REG(a_idx, a_uValue) \
        do { uint8_t const BS3_FAR *pbTmp = &pIretBuf->ab[a_idx * cbPop]; \
            if (cbPop == 2)       *(uint16_t)pbTmp = (uint16_t)(a_uValue); \
             else if (cbPop != 8) *(uint32_t)pbTmp = (uint32_t)(a_uValue); \
             else                 *(uint64_t)pbTmp = (a_uValue); \
         } while (0)

    /* make sure they're allocated  */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&CtxUdExpected, sizeof(CtxUdExpected));
    Bs3MemZero(&TmpCtx, sizeof(TmpCtx));
    Bs3MemZero(&TmpCtxExpected, sizeof(TmpCtxExpected));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));

    /*
     * When dealing with 16-bit irets in 32-bit or 64-bit mode, we must have
     * copies of both iret and ud in the first 64KB of memory.  The stack is
     * below 64KB, so we'll just copy the instructions onto the stack.
     */
    Bs3MemCpy(abLowUd, bs3CpuBasic2_ud2, 4);
    Bs3MemCpy(abLowIret, pfnIret, 4);

    /*
     * Create a context (stack is irrelevant, we'll mainly be using pIretBuf).
     *  - Point the context at our iret instruction.
     *  - Point SS:xSP at pIretBuf.
     */
    Bs3RegCtxSaveEx(&Ctx, bTestMode, 0);
    if (!fUseLowCode)
        Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, pfnIret);
    else
        Bs3RegCtxSetRipCsFromCurPtr(&Ctx, pfnIretLow);
    if (BS3_MODE_IS_16BIT_SYS(bTestMode))
        g_uBs3TrapEipHint = Ctx.rip.u32;
    Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rsp, &Ctx.ss, pIretBuf);

    /*
     * The first success (UD) context keeps the same code bit-count as the iret.
     */
    Bs3MemCpy(&CtxUdExpected, &Ctx, sizeof(Ctx));
    if (!fUseLowCode)
        Bs3RegCtxSetRipCsFromLnkPtr(&CtxUdExpected, bs3CpuBasic2_ud2);
    else
        Bs3RegCtxSetRipCsFromCurPtr(&CtxUdExpected, pfnUdLow);
    CtxUdExpected.rsp.u += cbSameCplFrame;

    /*
     * Check that it works at all.
     */
    iretbuf_SetupFrame(pIretBuf, cbPop, CtxUdExpected.cs, CtxUdExpected.rip.u,
                       CtxUdExpected.rflags.u32, CtxUdExpected.ss, CtxUdExpected.rsp.u);

    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
    bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
    g_usBs3TestStep++;

    if (!BS3_MODE_IS_RM_OR_V86(bTestMode))
    {
        /* Selectors are modified when switching rings, so we need to know
           what we're dealing with there. */
        if (   !BS3_SEL_IS_IN_R0_RANGE(Ctx.cs) || !BS3_SEL_IS_IN_R0_RANGE(Ctx.ss)
            || !BS3_SEL_IS_IN_R0_RANGE(Ctx.ds) || !BS3_SEL_IS_IN_R0_RANGE(Ctx.es))
            Bs3TestFailedF("Expected R0 CS, SS, DS and ES; not %#x, %#x, %#x and %#x\n", Ctx.cs, Ctx.ss, Ctx.ds, Ctx.es);
        if (Ctx.fs || Ctx.gs)
            Bs3TestFailed("Expected R0 FS and GS to be 0!\n");

        /*
         * Test returning to outer rings if protected mode.
         */
        Bs3MemCpy(&TmpCtx, &Ctx, sizeof(TmpCtx));
        Bs3MemCpy(&TmpCtxExpected, &CtxUdExpected, sizeof(TmpCtxExpected));
        for (iRingDst = 3; iRingDst >= 0; iRingDst--)
        {
            Bs3RegCtxConvertToRingX(&TmpCtxExpected, iRingDst);
            TmpCtxExpected.ds = iRingDst ? 0 : TmpCtx.ds;
            TmpCtx.es = TmpCtxExpected.es;
            iretbuf_SetupFrame(pIretBuf, cbPop, TmpCtxExpected.cs, TmpCtxExpected.rip.u,
                               TmpCtxExpected.rflags.u32, TmpCtxExpected.ss, TmpCtxExpected.rsp.u);
            Bs3TrapSetJmpAndRestore(&TmpCtx, &TrapCtx);
            bs3CpuBasic2_CompareUdCtx(&TrapCtx, &TmpCtxExpected);
            g_usBs3TestStep++;
        }

        /*
         * Check CS.RPL and SS.RPL.
         */
        for (iRingDst = 3; iRingDst >= 0; iRingDst--)
        {
            uint16_t const uDstSsR0 = (CtxUdExpected.ss & BS3_SEL_RING_SUB_MASK) + BS3_SEL_R0_FIRST;
            Bs3MemCpy(&TmpCtxExpected, &CtxUdExpected, sizeof(TmpCtxExpected));
            Bs3RegCtxConvertToRingX(&TmpCtxExpected, iRingDst);
            for (iRingSrc = 3; iRingSrc >= 0; iRingSrc--)
            {
                Bs3MemCpy(&TmpCtx, &Ctx, sizeof(TmpCtx));
                Bs3RegCtxConvertToRingX(&TmpCtx, iRingSrc);
                TmpCtx.es         = TmpCtxExpected.es;
                TmpCtxExpected.ds = iRingDst != iRingSrc ? 0 : TmpCtx.ds;
                for (uRplCs = 0; uRplCs <= 3; uRplCs++)
                {
                    uint16_t const uSrcEs = TmpCtx.es;
                    uint16_t const uDstCs = (TmpCtxExpected.cs & X86_SEL_MASK_OFF_RPL) | uRplCs;
                    //Bs3TestPrintf("dst=%d src=%d rplCS=%d\n", iRingDst, iRingSrc, uRplCs);

                    /* CS.RPL */
                    iretbuf_SetupFrame(pIretBuf, cbPop, uDstCs, TmpCtxExpected.rip.u, TmpCtxExpected.rflags.u32,
                                       TmpCtxExpected.ss, TmpCtxExpected.rsp.u);
                    Bs3TrapSetJmpAndRestore(&TmpCtx, &TrapCtx);
                    if (uRplCs == iRingDst && iRingDst >= iRingSrc)
                        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &TmpCtxExpected);
                    else
                    {
                        if (iRingDst < iRingSrc)
                            TmpCtx.es = 0;
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &TmpCtx, uDstCs & X86_SEL_MASK_OFF_RPL);
                        TmpCtx.es = uSrcEs;
                    }
                    g_usBs3TestStep++;

                    /* SS.RPL */
                    if (iRingDst != iRingSrc || BS3_MODE_IS_64BIT_CODE(bTestMode))
                    {
                        uint16_t uSavedDstSs = TmpCtxExpected.ss;
                        for (uRplSs = 0; uRplSs <= 3; uRplSs++)
                        {
                            /* SS.DPL (iRingDst == CS.DPL) */
                            for (uDplSs = 0; uDplSs <= 3; uDplSs++)
                            {
                                uint16_t const uDstSs = ((uDplSs << BS3_SEL_RING_SHIFT) | uRplSs) + uDstSsR0;
                                //Bs3TestPrintf("dst=%d src=%d rplCS=%d rplSS=%d dplSS=%d dst %04x:%08RX64 %08RX32 %04x:%08RX64\n",
                                //              iRingDst, iRingSrc, uRplCs, uRplSs, uDplSs, uDstCs, TmpCtxExpected.rip.u,
                                //              TmpCtxExpected.rflags.u32, uDstSs, TmpCtxExpected.rsp.u);

                                iretbuf_SetupFrame(pIretBuf, cbPop, uDstCs, TmpCtxExpected.rip.u,
                                                   TmpCtxExpected.rflags.u32, uDstSs, TmpCtxExpected.rsp.u);
                                Bs3TrapSetJmpAndRestore(&TmpCtx, &TrapCtx);
                                if (uRplCs != iRingDst || iRingDst < iRingSrc)
                                {
                                    if (iRingDst < iRingSrc)
                                        TmpCtx.es = 0;
                                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &TmpCtx, uDstCs & X86_SEL_MASK_OFF_RPL);
                                }
                                else if (uRplSs != iRingDst || uDplSs != iRingDst)
                                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &TmpCtx, uDstSs & X86_SEL_MASK_OFF_RPL);
                                else
                                    bs3CpuBasic2_CompareUdCtx(&TrapCtx, &TmpCtxExpected);
                                TmpCtx.es = uSrcEs;
                                g_usBs3TestStep++;
                            }
                        }

                        TmpCtxExpected.ss = uSavedDstSs;
                    }
                }
            }
        }
    }

    /*
     * Special 64-bit checks.
     */
    if (BS3_MODE_IS_64BIT_CODE(bTestMode))
    {
        /* The VM flag is completely ignored. */
        iretbuf_SetupFrame(pIretBuf, cbPop, CtxUdExpected.cs, CtxUdExpected.rip.u,
                           CtxUdExpected.rflags.u32 | X86_EFL_VM, CtxUdExpected.ss, CtxUdExpected.rsp.u);
        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
        g_usBs3TestStep++;

        /* The NT flag can be loaded just fine. */
        CtxUdExpected.rflags.u32 |= X86_EFL_NT;
        iretbuf_SetupFrame(pIretBuf, cbPop, CtxUdExpected.cs, CtxUdExpected.rip.u,
                           CtxUdExpected.rflags.u32, CtxUdExpected.ss, CtxUdExpected.rsp.u);
        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxUdExpected);
        CtxUdExpected.rflags.u32 &= ~X86_EFL_NT;
        g_usBs3TestStep++;

        /* However, we'll #GP(0) if it's already set (in RFLAGS) when executing IRET. */
        Ctx.rflags.u32 |= X86_EFL_NT;
        iretbuf_SetupFrame(pIretBuf, cbPop, CtxUdExpected.cs, CtxUdExpected.rip.u,
                           CtxUdExpected.rflags.u32, CtxUdExpected.ss, CtxUdExpected.rsp.u);
        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
        g_usBs3TestStep++;

        /* The NT flag #GP(0) should trump all other exceptions - pit it against #PF. */
        pbTest = (uint8_t BS3_FAR *)Bs3MemGuardedTestPageAlloc(BS3MEMKIND_TILED);
        if (pbTest != NULL)
        {
            Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rsp, &Ctx.ss, &pbTest[X86_PAGE_SIZE]);
            iretbuf_SetupFrame(pIretBuf, cbPop, CtxUdExpected.cs, CtxUdExpected.rip.u,
                               CtxUdExpected.rflags.u32, CtxUdExpected.ss, CtxUdExpected.rsp.u);
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            bs3CpuBasic2_CompareGpCtx(&TrapCtx, &Ctx, 0);
            g_usBs3TestStep++;

            Bs3RegCtxSetGrpSegFromCurPtr(&Ctx, &Ctx.rsp, &Ctx.ss, pIretBuf);
            Bs3MemGuardedTestPageFree(pbTest);
        }
        Ctx.rflags.u32 &= ~X86_EFL_NT;
    }
}


BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuBasic2_iret)(uint8_t bMode)
{
    struct
    {
        uint8_t abExtraStack[4096]; /**< we've got ~30KB of stack, so 4KB for the trap handlers++ is not a problem. */
        IRETBUF IRetBuf;
        uint8_t abGuard[32];
    } uBuf;
    size_t cbUnused;

    //if (bMode != BS3_MODE_LM64) return BS3TESTDOMODE_SKIPPED;
    bs3CpuBasic2_SetGlobals(bMode);

    /*
     * Primary instruction form.
     */
    Bs3MemSet(&uBuf, 0xaa, sizeof(uBuf));
    Bs3MemSet(uBuf.abGuard, 0x88, sizeof(uBuf.abGuard));
    if (BS3_MODE_IS_16BIT_CODE(bMode))
        bs3CpuBasic2_iret_Worker(bMode, bs3CpuBasic2_iret,              2, &uBuf.IRetBuf, "iret");
    else if (BS3_MODE_IS_32BIT_CODE(bMode))
        bs3CpuBasic2_iret_Worker(bMode, bs3CpuBasic2_iret,              4, &uBuf.IRetBuf, "iretd");
    else
        bs3CpuBasic2_iret_Worker(bMode, bs3CpuBasic2_iret_rexw,         8, &uBuf.IRetBuf, "o64 iret");

    BS3_ASSERT(ASMMemIsAllU8(uBuf.abGuard, sizeof(uBuf.abGuard), 0x88));
    cbUnused = (uintptr_t)ASMMemFirstMismatchingU8(uBuf.abExtraStack, sizeof(uBuf.abExtraStack) + sizeof(uBuf.IRetBuf), 0xaa)
             - (uintptr_t)uBuf.abExtraStack;
    if (cbUnused < 2048)
        Bs3TestFailedF("cbUnused=%u #%u\n", cbUnused, 1);

    /*
     * Secondary variation: opsize prefixed.
     */
    Bs3MemSet(&uBuf, 0xaa, sizeof(uBuf));
    Bs3MemSet(uBuf.abGuard, 0x88, sizeof(uBuf.abGuard));
    if (BS3_MODE_IS_16BIT_CODE(bMode) && (g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80386)
        bs3CpuBasic2_iret_Worker(bMode, bs3CpuBasic2_iret_opsize,       4, &uBuf.IRetBuf, "o32 iret");
    else if (BS3_MODE_IS_32BIT_CODE(bMode))
        bs3CpuBasic2_iret_Worker(bMode, bs3CpuBasic2_iret_opsize,       2, &uBuf.IRetBuf, "o16 iret");
    else if (BS3_MODE_IS_64BIT_CODE(bMode))
        bs3CpuBasic2_iret_Worker(bMode, bs3CpuBasic2_iret,              4, &uBuf.IRetBuf, "iretd");
    BS3_ASSERT(ASMMemIsAllU8(uBuf.abGuard, sizeof(uBuf.abGuard), 0x88));
    cbUnused = (uintptr_t)ASMMemFirstMismatchingU8(uBuf.abExtraStack, sizeof(uBuf.abExtraStack) + sizeof(uBuf.IRetBuf), 0xaa)
             - (uintptr_t)uBuf.abExtraStack;
    if (cbUnused < 2048)
        Bs3TestFailedF("cbUnused=%u #%u\n", cbUnused, 2);

    /*
     * Third variation: 16-bit in 64-bit mode (truly unlikely)
     */
    if (BS3_MODE_IS_64BIT_CODE(bMode))
    {
        Bs3MemSet(&uBuf, 0xaa, sizeof(uBuf));
        Bs3MemSet(uBuf.abGuard, 0x88, sizeof(uBuf.abGuard));
        bs3CpuBasic2_iret_Worker(bMode, bs3CpuBasic2_iret_opsize,       2, &uBuf.IRetBuf, "o16 iret");
        BS3_ASSERT(ASMMemIsAllU8(uBuf.abGuard, sizeof(uBuf.abGuard), 0x88));
        cbUnused = (uintptr_t)ASMMemFirstMismatchingU8(uBuf.abExtraStack, sizeof(uBuf.abExtraStack) + sizeof(uBuf.IRetBuf), 0xaa)
                 - (uintptr_t)uBuf.abExtraStack;
        if (cbUnused < 2048)
            Bs3TestFailedF("cbUnused=%u #%u\n", cbUnused, 3);
    }

    return 0;
}



/*********************************************************************************************************************************
*   Non-far JMP & CALL Tests                                                                                                     *
*********************************************************************************************************************************/
#define PROTO_ALL(a_Template) \
    FNBS3FAR a_Template ## _c16, \
             a_Template ## _c32, \
             a_Template ## _c64
PROTO_ALL(bs3CpuBasic2_jmp_jb__ud2);
PROTO_ALL(bs3CpuBasic2_jmp_jb_back__ud2);
PROTO_ALL(bs3CpuBasic2_jmp_jv__ud2);
PROTO_ALL(bs3CpuBasic2_jmp_jv_back__ud2);
PROTO_ALL(bs3CpuBasic2_jmp_ind_mem__ud2);
PROTO_ALL(bs3CpuBasic2_jmp_ind_xAX__ud2);
PROTO_ALL(bs3CpuBasic2_jmp_ind_xDI__ud2);
FNBS3FAR  bs3CpuBasic2_jmp_ind_r9__ud2_c64;
PROTO_ALL(bs3CpuBasic2_call_jv__ud2);
PROTO_ALL(bs3CpuBasic2_call_jv_back__ud2);
PROTO_ALL(bs3CpuBasic2_call_ind_mem__ud2);
PROTO_ALL(bs3CpuBasic2_call_ind_xAX__ud2);
PROTO_ALL(bs3CpuBasic2_call_ind_xDI__ud2);
FNBS3FAR  bs3CpuBasic2_call_ind_r9__ud2_c64;

PROTO_ALL(bs3CpuBasic2_jmp_opsize_begin);
PROTO_ALL(bs3CpuBasic2_jmp_jb_opsize__ud2);
PROTO_ALL(bs3CpuBasic2_jmp_jb_opsize_back__ud2);
PROTO_ALL(bs3CpuBasic2_jmp_jv_opsize__ud2);
PROTO_ALL(bs3CpuBasic2_jmp_jv_opsize_back__ud2);
PROTO_ALL(bs3CpuBasic2_jmp_ind_mem_opsize__ud2);
FNBS3FAR  bs3CpuBasic2_jmp_ind_mem_opsize__ud2__intel_c64;
PROTO_ALL(bs3CpuBasic2_jmp_ind_xAX_opsize__ud2);
PROTO_ALL(bs3CpuBasic2_call_jv_opsize__ud2);
PROTO_ALL(bs3CpuBasic2_call_jv_opsize_back__ud2);
PROTO_ALL(bs3CpuBasic2_call_ind_mem_opsize__ud2);
FNBS3FAR  bs3CpuBasic2_call_ind_mem_opsize__ud2__intel_c64;
PROTO_ALL(bs3CpuBasic2_call_ind_xAX_opsize__ud2);
PROTO_ALL(bs3CpuBasic2_jmp_opsize_end);
#undef PROTO_ALL

FNBS3FAR bs3CpuBasic2_jmptext16_start;

FNBS3FAR bs3CpuBasic2_jmp_target_wrap_forward;
FNBS3FAR bs3CpuBasic2_jmp_jb_wrap_forward__ud2;
FNBS3FAR bs3CpuBasic2_jmp_jb_opsize_wrap_forward__ud2;
FNBS3FAR bs3CpuBasic2_jmp_jv16_wrap_forward__ud2;
FNBS3FAR bs3CpuBasic2_jmp_jv16_opsize_wrap_forward__ud2;
FNBS3FAR bs3CpuBasic2_call_jv16_wrap_forward__ud2;
FNBS3FAR bs3CpuBasic2_call_jv16_opsize_wrap_forward__ud2;

FNBS3FAR bs3CpuBasic2_jmp_target_wrap_backward;
FNBS3FAR bs3CpuBasic2_jmp_jb_wrap_backward__ud2;
FNBS3FAR bs3CpuBasic2_jmp_jb_opsize_wrap_backward__ud2;
FNBS3FAR bs3CpuBasic2_jmp_jv16_wrap_backward__ud2;
FNBS3FAR bs3CpuBasic2_jmp_jv16_opsize_wrap_backward__ud2;
FNBS3FAR bs3CpuBasic2_call_jv16_wrap_backward__ud2;
FNBS3FAR bs3CpuBasic2_call_jv16_opsize_wrap_backward__ud2;



/**
 * Entrypoint for non-far JMP & CALL tests.
 *
 * @returns 0 or BS3TESTDOMODE_SKIPPED.
 * @param   bMode       The CPU mode we're testing.
 *
 * @note    When testing v8086 code, we'll be running in v8086 mode. So, careful
 *          with control registers and such.
 */
BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuBasic2_jmp_call)(uint8_t bMode)
{
    BS3TRAPFRAME        TrapCtx;
    BS3REGCTX           Ctx;
    BS3REGCTX           CtxExpected;
    unsigned            iTest;
    unsigned const      cMaxRecompRuns = g_cBs3ThresholdNativeRecompiler ? g_cBs3ThresholdNativeRecompiler : 1;
    unsigned            iRecompRun;

    /* make sure they're allocated  */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&CtxExpected, sizeof(Ctx));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));

    bs3CpuBasic2_SetGlobals(bMode);

    /*
     * Create a context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 768);
    Bs3MemCpy(&CtxExpected, &Ctx, sizeof(CtxExpected));

    /*
     * 16-bit tests.
     *
     * When opsize is 16-bit relative jumps will do 16-bit calculations and
     * modify IP.  This means that it is not possible to trigger a segment
     * limit #GP(0) when the limit is set to 0xffff.
     */
    if (BS3_MODE_IS_16BIT_CODE(bMode))
    {
        static struct
        {
            int8_t      iWrap;
            bool        fOpSizePfx;
            int8_t      iGprIndirect;
            bool        fCall;
            FPFNBS3FAR  pfnTest;
        }
        const s_aTests[] =
        {
            {  0, false,           -1, false, bs3CpuBasic2_jmp_jb__ud2_c16,                     },
            {  0, false,           -1, false, bs3CpuBasic2_jmp_jb_back__ud2_c16,                },
            {  0,  true,           -1, false, bs3CpuBasic2_jmp_jb_opsize__ud2_c16,              },
            {  0,  true,           -1, false, bs3CpuBasic2_jmp_jb_opsize_back__ud2_c16,         },
            {  0, false,           -1, false, bs3CpuBasic2_jmp_jv__ud2_c16,                     },
            {  0, false,           -1, false, bs3CpuBasic2_jmp_jv_back__ud2_c16,                },
            {  0,  true,           -1, false, bs3CpuBasic2_jmp_jv_opsize__ud2_c16,              },
            {  0,  true,           -1, false, bs3CpuBasic2_jmp_jv_opsize_back__ud2_c16,         },
            {  0, false,           -1, false, bs3CpuBasic2_jmp_ind_mem__ud2_c16,                },
            {  0,  true,           -1, false, bs3CpuBasic2_jmp_ind_mem_opsize__ud2_c16,         },
            {  0, false, X86_GREG_xAX, false, bs3CpuBasic2_jmp_ind_xAX__ud2_c16,                },
            {  0, false, X86_GREG_xDI, false, bs3CpuBasic2_jmp_ind_xDI__ud2_c16,                },
            {  0,  true, X86_GREG_xAX, false, bs3CpuBasic2_jmp_ind_xAX_opsize__ud2_c16,         },
            {  0, false,           -1,  true, bs3CpuBasic2_call_jv__ud2_c16,                    },
            {  0, false,           -1,  true, bs3CpuBasic2_call_jv_back__ud2_c16,               },
            {  0,  true,           -1,  true, bs3CpuBasic2_call_jv_opsize__ud2_c16,             },
            {  0,  true,           -1,  true, bs3CpuBasic2_call_jv_opsize_back__ud2_c16,        },
            {  0, false,           -1,  true, bs3CpuBasic2_call_ind_mem__ud2_c16,               },
            {  0,  true,           -1,  true, bs3CpuBasic2_call_ind_mem_opsize__ud2_c16,        },
            {  0, false, X86_GREG_xAX,  true, bs3CpuBasic2_call_ind_xAX__ud2_c16,               },
            {  0, false, X86_GREG_xDI,  true, bs3CpuBasic2_call_ind_xDI__ud2_c16,               },
            {  0,  true, X86_GREG_xAX,  true, bs3CpuBasic2_call_ind_xAX_opsize__ud2_c16,        },

            { -1, false,           -1, false, bs3CpuBasic2_jmp_jb_wrap_backward__ud2,           },
            { +1, false,           -1, false, bs3CpuBasic2_jmp_jb_wrap_forward__ud2,            },
            { -1,  true,           -1, false, bs3CpuBasic2_jmp_jb_opsize_wrap_backward__ud2,    },
            { +1,  true,           -1, false, bs3CpuBasic2_jmp_jb_opsize_wrap_forward__ud2,     },

            { -1, false,           -1, false, bs3CpuBasic2_jmp_jv16_wrap_backward__ud2,         },
            { +1, false,           -1, false, bs3CpuBasic2_jmp_jv16_wrap_forward__ud2,          },
            { -1,  true,           -1, false, bs3CpuBasic2_jmp_jv16_opsize_wrap_backward__ud2,  },
            { +1,  true,           -1, false, bs3CpuBasic2_jmp_jv16_opsize_wrap_forward__ud2,   },
            { -1, false,           -1,  true, bs3CpuBasic2_call_jv16_wrap_backward__ud2,        },
            { +1, false,           -1,  true, bs3CpuBasic2_call_jv16_wrap_forward__ud2,         },
            { -1,  true,           -1,  true, bs3CpuBasic2_call_jv16_opsize_wrap_backward__ud2, },
            { +1,  true,           -1,  true, bs3CpuBasic2_call_jv16_opsize_wrap_forward__ud2,  },
        };

        if (!BS3_MODE_IS_RM_OR_V86(bMode))
            Bs3SelSetup16BitCode(&Bs3GdteSpare03, Bs3SelLnkPtrToFlat(bs3CpuBasic2_jmptext16_start), 0);

        for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
        {
            uint64_t uGprSaved;
            if (s_aTests[iTest].iWrap == 0)
            {
                uint8_t const BS3_FAR *fpbCode;
                Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, s_aTests[iTest].pfnTest);
                fpbCode = (uint8_t const BS3_FAR *)BS3_FP_MAKE(Ctx.cs, Ctx.rip.u16);
                CtxExpected.rip.u = Ctx.rip.u + (int64_t)(int8_t)fpbCode[-1];
            }
            else
            {
                if (BS3_MODE_IS_RM_OR_V86(bMode))
                    Ctx.cs = BS3_FP_SEG(s_aTests[iTest].pfnTest);
                else
                    Ctx.cs = BS3_SEL_SPARE_03;
                Ctx.rip.u  = BS3_FP_OFF(s_aTests[iTest].pfnTest);
                if (s_aTests[iTest].fOpSizePfx)
                    CtxExpected.rip.u = Ctx.rip.u;
                else if (s_aTests[iTest].iWrap < 0)
                    CtxExpected.rip.u = BS3_FP_OFF(bs3CpuBasic2_jmp_target_wrap_backward);
                else
                    CtxExpected.rip.u = BS3_FP_OFF(bs3CpuBasic2_jmp_target_wrap_forward);
            }
            CtxExpected.cs = Ctx.cs;
            if (s_aTests[iTest].iGprIndirect >= 0)
            {
                uGprSaved = (&Ctx.rax)[s_aTests[iTest].iGprIndirect].u;
                (&Ctx.rax)[s_aTests[iTest].iGprIndirect].u
                    = (&CtxExpected.rax)[s_aTests[iTest].iGprIndirect].u = CtxExpected.rip.u;
            }
            CtxExpected.rsp.u = Ctx.rsp.u;
            if (s_aTests[iTest].fCall && (s_aTests[iTest].iWrap == 0 || !s_aTests[iTest].fOpSizePfx))
                CtxExpected.rsp.u -= s_aTests[iTest].fOpSizePfx ? 4 : 2;
            //Bs3TestPrintf("cs:rip=%04RX16:%04RX64\n", Ctx.cs, Ctx.rip.u);

            for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
            {
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                if (s_aTests[iTest].iWrap == 0 || !s_aTests[iTest].fOpSizePfx)
                    bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
                else
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, 0);
            }
            g_usBs3TestStep++;

            /* Again single stepping: */
            //Bs3TestPrintf("stepping...\n");
            Ctx.rflags.u16        |= X86_EFL_TF;
            CtxExpected.rflags.u16 = Ctx.rflags.u16;
            for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
            {
                Bs3RegSetDr6(0);
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                if (s_aTests[iTest].iWrap == 0 || !s_aTests[iTest].fOpSizePfx)
                    bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected, X86_DR6_BS);
                else
                {
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, 0);
                    bs3CpuBasic2_CheckDr6InitVal();
                }
            }
            Ctx.rflags.u16        &= ~X86_EFL_TF;
            CtxExpected.rflags.u16 = Ctx.rflags.u16;
            g_usBs3TestStep++;

            if (s_aTests[iTest].iGprIndirect >= 0)
                (&Ctx.rax)[s_aTests[iTest].iGprIndirect].u = (&CtxExpected.rax)[s_aTests[iTest].iGprIndirect].u = uGprSaved;
        }

        /* Limit the wraparound CS segment to exclude bs3CpuBasic2_jmp_target_wrap_backward
           and run the backward wrapping tests. */
        if (!BS3_MODE_IS_RM_OR_V86(bMode))
        {
            Bs3GdteSpare03.Gen.u16LimitLow = BS3_FP_OFF(bs3CpuBasic2_jmp_target_wrap_backward) - 1;
            CtxExpected.cs    = Ctx.cs = BS3_SEL_SPARE_03;
            CtxExpected.rsp.u = Ctx.rsp.u;
            for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
                if (s_aTests[iTest].iWrap < 0)
                {
                    CtxExpected.rip.u = Ctx.rip.u = BS3_FP_OFF(s_aTests[iTest].pfnTest);
                    //Bs3TestPrintf("cs:rip=%04RX16:%04RX64 v1\n", Ctx.cs, Ctx.rip.u);
                    for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
                    {
                        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, 0);
                    }
                    g_usBs3TestStep++;
                }

            /* Do another round where we put the limit in the middle of the UD2
               instruction we're jumping to: */
            Bs3GdteSpare03.Gen.u16LimitLow = BS3_FP_OFF(bs3CpuBasic2_jmp_target_wrap_backward);
            for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
                if (s_aTests[iTest].iWrap < 0)
                {
                    Ctx.rip.u = BS3_FP_OFF(s_aTests[iTest].pfnTest);
                    if (s_aTests[iTest].fOpSizePfx)
                        CtxExpected.rip.u = Ctx.rip.u;
                    else
                        CtxExpected.rip.u = BS3_FP_OFF(bs3CpuBasic2_jmp_target_wrap_backward);
                    CtxExpected.rsp.u = Ctx.rsp.u;
                    if (s_aTests[iTest].fCall && (s_aTests[iTest].iWrap == 0 || !s_aTests[iTest].fOpSizePfx))
                        CtxExpected.rsp.u -= s_aTests[iTest].fOpSizePfx ? 4 : 2;
                    //Bs3TestPrintf("cs:rip=%04RX16:%04RX64 v2\n", Ctx.cs, Ctx.rip.u);
                    for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
                    {
                        Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, 0);
                    }
                    g_usBs3TestStep++;
                }
        }

    }
    /*
     * 32-bit & 64-bit tests.
     *
     * When the opsize prefix is applied here, IP is updated and bits 63:16
     * cleared.  However in 64-bit mode, Intel ignores the opsize prefix
     * whereas AMD doesn't and it works like you expect.
     */
    else
    {
        static struct
        {
            uint8_t     cBits;
            bool        fOpSizePfx;
            bool        fIgnPfx;
            int8_t      iGprIndirect;
            bool        fCall;
            FPFNBS3FAR  pfnTest;
        }
        const s_aTests[] =
        {
            {  32, false, false,           -1, false, bs3CpuBasic2_jmp_jb__ud2_c32,                    },
            {  32, false, false,           -1, false, bs3CpuBasic2_jmp_jb_back__ud2_c32,               },
            {  32,  true, false,           -1, false, bs3CpuBasic2_jmp_jb_opsize__ud2_c32,             },
            {  32,  true, false,           -1, false, bs3CpuBasic2_jmp_jb_opsize_back__ud2_c32,        },
            {  32, false, false,           -1, false, bs3CpuBasic2_jmp_jv__ud2_c32,                    },
            {  32, false, false,           -1, false, bs3CpuBasic2_jmp_jv_back__ud2_c32,               },
            {  32,  true, false,           -1, false, bs3CpuBasic2_jmp_jv_opsize__ud2_c32,             },
            {  32,  true, false,           -1, false, bs3CpuBasic2_jmp_jv_opsize_back__ud2_c32,        },
            {  32, false, false,           -1, false, bs3CpuBasic2_jmp_ind_mem__ud2_c32,               },
            {  32,  true, false,           -1, false, bs3CpuBasic2_jmp_ind_mem_opsize__ud2_c32,        },
            {  32, false, false, X86_GREG_xAX, false, bs3CpuBasic2_jmp_ind_xAX__ud2_c32,               },
            {  32, false, false, X86_GREG_xDI, false, bs3CpuBasic2_jmp_ind_xDI__ud2_c32,               },
            {  32,  true, false, X86_GREG_xAX, false, bs3CpuBasic2_jmp_ind_xAX_opsize__ud2_c32,        },
            {  32, false, false,           -1,  true, bs3CpuBasic2_call_jv__ud2_c32,                   },
            {  32, false, false,           -1,  true, bs3CpuBasic2_call_jv_back__ud2_c32,              },
            {  32,  true, false,           -1,  true, bs3CpuBasic2_call_jv_opsize__ud2_c32,            },
            {  32,  true, false,           -1,  true, bs3CpuBasic2_call_jv_opsize_back__ud2_c32,       },
            {  32, false, false,           -1,  true, bs3CpuBasic2_call_ind_mem__ud2_c32,              },
            {  32,  true, false,           -1,  true, bs3CpuBasic2_call_ind_mem_opsize__ud2_c32,       },
            {  32, false, false, X86_GREG_xAX,  true, bs3CpuBasic2_call_ind_xAX__ud2_c32,              },
            {  32, false, false, X86_GREG_xDI,  true, bs3CpuBasic2_call_ind_xDI__ud2_c32,              },
            {  32,  true, false, X86_GREG_xAX,  true, bs3CpuBasic2_call_ind_xAX_opsize__ud2_c32,       },
            /* 64bit/Intel: Use the _c64 tests, which are written to ignore the o16 prefix. */
            {  64, false,  true,           -1, false, bs3CpuBasic2_jmp_jb__ud2_c64,                    },
            {  64, false,  true,           -1, false, bs3CpuBasic2_jmp_jb_back__ud2_c64,               },
            {  64,  true,  true,           -1, false, bs3CpuBasic2_jmp_jb_opsize__ud2_c64,             },
            {  64,  true,  true,           -1, false, bs3CpuBasic2_jmp_jb_opsize_back__ud2_c64,        },
            {  64, false,  true,           -1, false, bs3CpuBasic2_jmp_jv__ud2_c64,                    },
            {  64, false,  true,           -1, false, bs3CpuBasic2_jmp_jv_back__ud2_c64,               },
            {  64,  true,  true,           -1, false, bs3CpuBasic2_jmp_jv_opsize__ud2_c64,             },
            {  64,  true,  true,           -1, false, bs3CpuBasic2_jmp_jv_opsize_back__ud2_c64,        },
            {  64, false,  true,           -1, false, bs3CpuBasic2_jmp_ind_mem__ud2_c64,               },
            {  64,  true,  true,           -1, false, bs3CpuBasic2_jmp_ind_mem_opsize__ud2__intel_c64, },
            {  64, false,  true, X86_GREG_xAX, false, bs3CpuBasic2_jmp_ind_xAX__ud2_c64,               },
            {  64, false,  true, X86_GREG_xDI, false, bs3CpuBasic2_jmp_ind_xDI__ud2_c64,               },
            {  64, false,  true, X86_GREG_x9,  false, bs3CpuBasic2_jmp_ind_r9__ud2_c64,                },
            {  64,  true,  true, X86_GREG_xAX, false, bs3CpuBasic2_jmp_ind_xAX_opsize__ud2_c64,        }, /* no intel version needed */
            {  64, false,  true,           -1,  true, bs3CpuBasic2_call_jv__ud2_c64,                   },
            {  64, false,  true,           -1,  true, bs3CpuBasic2_call_jv_back__ud2_c64,              },
            {  64,  true,  true,           -1,  true, bs3CpuBasic2_call_jv_opsize__ud2_c64,            },
            {  64,  true,  true,           -1,  true, bs3CpuBasic2_call_jv_opsize_back__ud2_c64,       },
            {  64, false,  true,           -1,  true, bs3CpuBasic2_call_ind_mem__ud2_c64,              },
            {  64,  true,  true,           -1,  true, bs3CpuBasic2_call_ind_mem_opsize__ud2__intel_c64,},
            {  64, false,  true, X86_GREG_xAX,  true, bs3CpuBasic2_call_ind_xAX__ud2_c64,              },
            {  64, false,  true, X86_GREG_xDI,  true, bs3CpuBasic2_call_ind_xDI__ud2_c64,              },
            {  64, false,  true, X86_GREG_x9,   true, bs3CpuBasic2_call_ind_r9__ud2_c64,               },
            {  64,  true,  true, X86_GREG_xAX,  true, bs3CpuBasic2_call_ind_xAX_opsize__ud2_c64,       }, /* no intel version needed */
            /* 64bit/AMD: Use the _c32 tests. */
            {  64, false, false,           -1, false, bs3CpuBasic2_jmp_jb__ud2_c32,                    },
            {  64, false, false,           -1, false, bs3CpuBasic2_jmp_jb_back__ud2_c32,               },
            {  64,  true, false,           -1, false, bs3CpuBasic2_jmp_jb_opsize__ud2_c32,             },
            {  64,  true, false,           -1, false, bs3CpuBasic2_jmp_jb_opsize_back__ud2_c32,        },
            {  64, false, false,           -1, false, bs3CpuBasic2_jmp_jv__ud2_c32,                    },
            {  64, false, false,           -1, false, bs3CpuBasic2_jmp_jv_back__ud2_c32,               },
            {  64,  true, false,           -1, false, bs3CpuBasic2_jmp_jv_opsize__ud2_c32,             },
            {  64,  true, false,           -1, false, bs3CpuBasic2_jmp_jv_opsize_back__ud2_c32,        },
            {  64, false, false,           -1, false, bs3CpuBasic2_jmp_ind_mem__ud2_c64,               }, /* using c64 here */
            {  64,  true, false,           -1, false, bs3CpuBasic2_jmp_ind_mem_opsize__ud2_c64,        }, /* ditto */
            {  64, false, false, X86_GREG_xAX, false, bs3CpuBasic2_jmp_ind_xAX__ud2_c64,               }, /* ditto */
            {  64, false, false, X86_GREG_xDI, false, bs3CpuBasic2_jmp_ind_xDI__ud2_c64,               }, /* ditto */
            {  64, false, false, X86_GREG_x9,  false, bs3CpuBasic2_jmp_ind_r9__ud2_c64,                }, /* ditto */
            {  64,  true, false, X86_GREG_xAX, false, bs3CpuBasic2_jmp_ind_xAX_opsize__ud2_c64,        }, /* ditto */
            {  64, false, false,           -1,  true, bs3CpuBasic2_call_jv__ud2_c32,                   }, /* using c32 again */
            {  64, false, false,           -1,  true, bs3CpuBasic2_call_jv_back__ud2_c32,              },
            {  64,  true, false,           -1,  true, bs3CpuBasic2_call_jv_opsize__ud2_c32,            },
            {  64,  true, false,           -1,  true, bs3CpuBasic2_call_jv_opsize_back__ud2_c32,       },
            {  64, false, false,           -1,  true, bs3CpuBasic2_call_ind_mem__ud2_c64,              }, /* using c64 here */
            {  64,  true, false,           -1,  true, bs3CpuBasic2_call_ind_mem_opsize__ud2_c64,       }, /* ditto */
            {  64, false, false, X86_GREG_xAX,  true, bs3CpuBasic2_call_ind_xAX__ud2_c64,              }, /* ditto */
            {  64, false, false, X86_GREG_xDI,  true, bs3CpuBasic2_call_ind_xDI__ud2_c64,              }, /* ditto */
            {  64, false, false, X86_GREG_x9,   true, bs3CpuBasic2_call_ind_r9__ud2_c64,               }, /* ditto */
            {  64,  true, false, X86_GREG_xAX,  true, bs3CpuBasic2_call_ind_xAX_opsize__ud2_c64,       }, /* ditto */
        };
        uint8_t const           cBits    = BS3_MODE_IS_64BIT_CODE(bMode) ? 64 : 32;
        BS3CPUVENDOR const      enmCpuVendor = Bs3GetCpuVendor();
        bool const              fIgnPfx  = cBits == 64 && enmCpuVendor == BS3CPUVENDOR_INTEL; /** @todo what does VIA do? */

        /* Prepare a copy of the UD2 instructions in low memory for opsize prefixed tests. */
        uint16_t const          offLow   = BS3_FP_OFF(bs3CpuBasic2_jmp_opsize_begin_c32);
        uint16_t const          cbLow    = BS3_FP_OFF(bs3CpuBasic2_jmp_opsize_end_c64) - offLow;
        uint8_t BS3_FAR * const pbCode16 = BS3_MAKE_PROT_R0PTR_FROM_FLAT(BS3_ADDR_BS3TEXT16);
        uint8_t BS3_FAR * const pbLow    = BS3_FP_MAKE(BS3_SEL_TILED_R0, 0);
        if (offLow < 0x600 || offLow + cbLow >= BS3_ADDR_STACK_R2)
            Bs3TestFailedF("Opsize overriden jumps are out of place: %#x LB %#x\n", offLow, cbLow);
        Bs3MemSet(&pbLow[offLow], 0xcc /*int3*/, cbLow);
        if (!fIgnPfx)
        {
            for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
                if (s_aTests[iTest].fOpSizePfx && s_aTests[iTest].cBits == cBits && s_aTests[iTest].fIgnPfx == fIgnPfx)
                {
                    uint16_t const offFn = BS3_FP_OFF(s_aTests[iTest].pfnTest);
                    uint16_t const offUd = offFn + (int16_t)(int8_t)pbCode16[offFn - 1];
                    BS3_ASSERT(offUd - offLow + 1 < cbLow);
                    pbCode16[offUd]     = 0xf1; /* replace original ud2 with icebp */
                    pbCode16[offUd + 1] = 0xf1;
                    pbLow[offUd]        = 0x0f; /* plant ud2 in low memory */
                    pbLow[offUd + 1]    = 0x0b;
                }
        }

        /* Run the tests. */
        for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
        {
            if (s_aTests[iTest].cBits == cBits && s_aTests[iTest].fIgnPfx == fIgnPfx)
            {
                uint64_t               uGprSaved;
                uint8_t const BS3_FAR *fpbCode = Bs3SelLnkPtrToCurPtr(s_aTests[iTest].pfnTest);
                Ctx.rip.u = Bs3SelLnkPtrToFlat(s_aTests[iTest].pfnTest);
                CtxExpected.rip.u = Ctx.rip.u + (int64_t)(int8_t)fpbCode[-1];
                if (s_aTests[iTest].iGprIndirect >= 0)
                {
                    uGprSaved = (&Ctx.rax)[s_aTests[iTest].iGprIndirect].u;
                    (&Ctx.rax)[s_aTests[iTest].iGprIndirect].u
                        = (&CtxExpected.rax)[s_aTests[iTest].iGprIndirect].u = CtxExpected.rip.u;
                }
                if (s_aTests[iTest].fOpSizePfx && !fIgnPfx)
                    CtxExpected.rip.u &= UINT16_MAX;
                CtxExpected.rsp.u = Ctx.rsp.u;
                if (s_aTests[iTest].fCall)
                    CtxExpected.rsp.u -= s_aTests[iTest].cBits == 64 ? 8
                                       : !s_aTests[iTest].fOpSizePfx ? 4 : 2;

                //Bs3TestPrintf("cs:rip=%04RX16:%08RX64\n", Ctx.cs, Ctx.rip.u);

                if (BS3_MODE_IS_16BIT_SYS(bMode))
                    g_uBs3TrapEipHint = s_aTests[iTest].fOpSizePfx ? 0 : Ctx.rip.u32;
                for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
                {
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
                }
                g_usBs3TestStep++;

                /* Again single stepping: */
                //Bs3TestPrintf("stepping...\n");
                Ctx.rflags.u16        |= X86_EFL_TF;
                CtxExpected.rflags.u16 = Ctx.rflags.u16;
                for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
                {
                    Bs3RegSetDr6(0);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected, X86_DR6_BS);
                }
                Ctx.rflags.u16        &= ~X86_EFL_TF;
                CtxExpected.rflags.u16 = Ctx.rflags.u16;
                g_usBs3TestStep++;

                if (s_aTests[iTest].iGprIndirect >= 0)
                    (&Ctx.rax)[s_aTests[iTest].iGprIndirect].u = (&CtxExpected.rax)[s_aTests[iTest].iGprIndirect].u = uGprSaved;
            }
        }

        Bs3MemSet(&pbLow[offLow], 0xcc /*int3*/, cbLow);
    }

    return 0;
}


/*********************************************************************************************************************************
*   FAR JMP & FAR CALL Tests                                                                                                     *
*********************************************************************************************************************************/
#define PROTO_ALL(a_Template) \
    FNBS3FAR a_Template ## _c16, \
             a_Template ## _c32, \
             a_Template ## _c64
PROTO_ALL(bs3CpuBasic2_far_jmp_call_opsize_begin);

FNBS3FAR  bs3CpuBasic2_jmpf_ptr_rm__ud2_c16;
PROTO_ALL(bs3CpuBasic2_jmpf_ptr_same_r0__ud2);
PROTO_ALL(bs3CpuBasic2_jmpf_ptr_same_r1__ud2);
PROTO_ALL(bs3CpuBasic2_jmpf_ptr_same_r2__ud2);
PROTO_ALL(bs3CpuBasic2_jmpf_ptr_same_r3__ud2);
PROTO_ALL(bs3CpuBasic2_jmpf_ptr_opsize_flipbit_r0__ud2);
PROTO_ALL(bs3CpuBasic2_jmpf_ptr_r0_cs64__ud2);
PROTO_ALL(bs3CpuBasic2_jmpf_ptr_r0_cs16l__ud2);

FNBS3FAR  bs3CpuBasic2_callf_ptr_rm__ud2_c16;
PROTO_ALL(bs3CpuBasic2_callf_ptr_same_r0__ud2);
PROTO_ALL(bs3CpuBasic2_callf_ptr_same_r1__ud2);
PROTO_ALL(bs3CpuBasic2_callf_ptr_same_r2__ud2);
PROTO_ALL(bs3CpuBasic2_callf_ptr_same_r3__ud2);
PROTO_ALL(bs3CpuBasic2_callf_ptr_opsize_flipbit_r0__ud2);
PROTO_ALL(bs3CpuBasic2_callf_ptr_r0_cs64__ud2);
PROTO_ALL(bs3CpuBasic2_callf_ptr_r0_cs16l__ud2);

FNBS3FAR  bs3CpuBasic2_jmpf_mem_rm__ud2_c16;
PROTO_ALL(bs3CpuBasic2_jmpf_mem_same_r0__ud2);
PROTO_ALL(bs3CpuBasic2_jmpf_mem_same_r1__ud2);
PROTO_ALL(bs3CpuBasic2_jmpf_mem_same_r2__ud2);
PROTO_ALL(bs3CpuBasic2_jmpf_mem_same_r3__ud2);
PROTO_ALL(bs3CpuBasic2_jmpf_mem_r0_cs16__ud2);
PROTO_ALL(bs3CpuBasic2_jmpf_mem_r0_cs32__ud2);
PROTO_ALL(bs3CpuBasic2_jmpf_mem_r0_cs64__ud2);
PROTO_ALL(bs3CpuBasic2_jmpf_mem_r0_cs16l__ud2);

FNBS3FAR  bs3CpuBasic2_jmpf_mem_same_r0__ud2_intel_c64;
FNBS3FAR  bs3CpuBasic2_jmpf_mem_same_r1__ud2_intel_c64;
FNBS3FAR  bs3CpuBasic2_jmpf_mem_same_r2__ud2_intel_c64;
FNBS3FAR  bs3CpuBasic2_jmpf_mem_same_r3__ud2_intel_c64;
FNBS3FAR  bs3CpuBasic2_jmpf_mem_r0_cs16__ud2_intel_c64;
FNBS3FAR  bs3CpuBasic2_jmpf_mem_r0_cs32__ud2_intel_c64;
FNBS3FAR  bs3CpuBasic2_jmpf_mem_r0_cs64__ud2_intel_c64;
FNBS3FAR  bs3CpuBasic2_jmpf_mem_r0_cs16l__ud2_intel_c64;

FNBS3FAR  bs3CpuBasic2_callf_mem_rm__ud2_c16;
PROTO_ALL(bs3CpuBasic2_callf_mem_same_r0__ud2);
PROTO_ALL(bs3CpuBasic2_callf_mem_same_r1__ud2);
PROTO_ALL(bs3CpuBasic2_callf_mem_same_r2__ud2);
PROTO_ALL(bs3CpuBasic2_callf_mem_same_r3__ud2);
PROTO_ALL(bs3CpuBasic2_callf_mem_r0_cs16__ud2);
PROTO_ALL(bs3CpuBasic2_callf_mem_r0_cs32__ud2);
PROTO_ALL(bs3CpuBasic2_callf_mem_r0_cs64__ud2);
PROTO_ALL(bs3CpuBasic2_callf_mem_r0_cs16l__ud2);

FNBS3FAR  bs3CpuBasic2_callf_mem_same_r0__ud2_intel_c64;
FNBS3FAR  bs3CpuBasic2_callf_mem_same_r1__ud2_intel_c64;
FNBS3FAR  bs3CpuBasic2_callf_mem_same_r2__ud2_intel_c64;
FNBS3FAR  bs3CpuBasic2_callf_mem_same_r3__ud2_intel_c64;
FNBS3FAR  bs3CpuBasic2_callf_mem_r0_cs16__ud2_intel_c64;
FNBS3FAR  bs3CpuBasic2_callf_mem_r0_cs32__ud2_intel_c64;
FNBS3FAR  bs3CpuBasic2_callf_mem_r0_cs64__ud2_intel_c64;
FNBS3FAR  bs3CpuBasic2_callf_mem_r0_cs16l__ud2_intel_c64;

PROTO_ALL(bs3CpuBasic2_far_jmp_call_opsize_end);
#undef PROTO_ALL



/**
 * Entrypoint for FAR JMP & FAR CALL tests.
 *
 * @returns 0 or BS3TESTDOMODE_SKIPPED.
 * @param   bMode       The CPU mode we're testing.
 *
 * @note    When testing v8086 code, we'll be running in v8086 mode. So, careful
 *          with control registers and such.
 */
BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuBasic2_far_jmp_call)(uint8_t bMode)
{
    BS3TRAPFRAME        TrapCtx;
    BS3REGCTX           Ctx;
    BS3REGCTX           CtxExpected;
    unsigned            iTest;

    /* make sure they're allocated  */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&CtxExpected, sizeof(Ctx));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));

    bs3CpuBasic2_SetGlobals(bMode);

    /*
     * Create a context.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 768);
    Bs3MemCpy(&CtxExpected, &Ctx, sizeof(CtxExpected));

    if (Ctx.rax.u8 == 0 || Ctx.rax.u8 == 0xff) /* for salc & the 64-bit detection */
        CtxExpected.rax.u8 = Ctx.rax.u8 = 0x42;

    /*
     * Set up spare selectors.
     */
    Bs3GdteSpare00 = Bs3Gdte_CODE16;
    Bs3GdteSpare00.Gen.u1Long = 1;

    /*
     * 16-bit tests.
     */
    if (BS3_MODE_IS_16BIT_CODE(bMode))
    {
        static struct
        {
            bool        fRmOrV86;
            bool        fCall;
            uint16_t    uDstSel;
            uint8_t     uDstBits;
            bool        fOpSizePfx;
            FPFNBS3FAR  pfnTest;
        }
        const s_aTests[] =
        {
            {  true, false, BS3_SEL_TEXT16,         16, false, bs3CpuBasic2_jmpf_ptr_rm__ud2_c16, },
            { false, false, BS3_SEL_R0_CS16,        16, false, bs3CpuBasic2_jmpf_ptr_same_r0__ud2_c16, },
            { false, false, BS3_SEL_R1_CS16 | 1,    16, false, bs3CpuBasic2_jmpf_ptr_same_r1__ud2_c16, },
            { false, false, BS3_SEL_R2_CS16 | 2,    16, false, bs3CpuBasic2_jmpf_ptr_same_r2__ud2_c16, },
            { false, false, BS3_SEL_R3_CS16 | 3,    16, false, bs3CpuBasic2_jmpf_ptr_same_r3__ud2_c16, },
            { false, false, BS3_SEL_R0_CS32,        32,  true, bs3CpuBasic2_jmpf_ptr_opsize_flipbit_r0__ud2_c16, },
            { false, false, BS3_SEL_R0_CS64,        64,  true, bs3CpuBasic2_jmpf_ptr_r0_cs64__ud2_c16, },  /* 16-bit CS, except in LM. */
            { false, false, BS3_SEL_SPARE_00,       64, false, bs3CpuBasic2_jmpf_ptr_r0_cs16l__ud2_c16, }, /* 16-bit CS, except in LM. */

            {  true,  true, BS3_SEL_TEXT16,         16, false, bs3CpuBasic2_callf_ptr_rm__ud2_c16, },
            { false,  true, BS3_SEL_R0_CS16,        16, false, bs3CpuBasic2_callf_ptr_same_r0__ud2_c16, },
            { false,  true, BS3_SEL_R1_CS16 | 1,    16, false, bs3CpuBasic2_callf_ptr_same_r1__ud2_c16, },
            { false,  true, BS3_SEL_R2_CS16 | 2,    16, false, bs3CpuBasic2_callf_ptr_same_r2__ud2_c16, },
            { false,  true, BS3_SEL_R3_CS16 | 3,    16, false, bs3CpuBasic2_callf_ptr_same_r3__ud2_c16, },
            { false,  true, BS3_SEL_R0_CS32,        32,  true, bs3CpuBasic2_callf_ptr_opsize_flipbit_r0__ud2_c16, },
            { false,  true, BS3_SEL_R0_CS64,        64,  true, bs3CpuBasic2_callf_ptr_r0_cs64__ud2_c16, },  /* 16-bit CS, except in LM. */
            { false,  true, BS3_SEL_SPARE_00,       64, false, bs3CpuBasic2_callf_ptr_r0_cs16l__ud2_c16, }, /* 16-bit CS, except in LM. */

            {  true, false, BS3_SEL_TEXT16,         16, false, bs3CpuBasic2_jmpf_mem_rm__ud2_c16, },
            { false, false, BS3_SEL_R0_CS16,        16, false, bs3CpuBasic2_jmpf_mem_same_r0__ud2_c16, },
            { false, false, BS3_SEL_R1_CS16 | 1,    16, false, bs3CpuBasic2_jmpf_mem_same_r1__ud2_c16, },
            { false, false, BS3_SEL_R2_CS16 | 2,    16, false, bs3CpuBasic2_jmpf_mem_same_r2__ud2_c16, },
            { false, false, BS3_SEL_R3_CS16 | 3,    16, false, bs3CpuBasic2_jmpf_mem_same_r3__ud2_c16, },
            { false, false, BS3_SEL_R0_CS16,        16, false, bs3CpuBasic2_jmpf_mem_r0_cs16__ud2_c16, },
            { false, false, BS3_SEL_R0_CS32,        32,  true, bs3CpuBasic2_jmpf_mem_r0_cs32__ud2_c16, },
            { false, false, BS3_SEL_R0_CS64,        64,  true, bs3CpuBasic2_jmpf_mem_r0_cs64__ud2_c16, },  /* 16-bit CS, except in LM. */
            { false, false, BS3_SEL_SPARE_00,       64, false, bs3CpuBasic2_jmpf_mem_r0_cs16l__ud2_c16, }, /* 16-bit CS, except in LM. */

            {  true,  true, BS3_SEL_TEXT16,         16, false, bs3CpuBasic2_callf_mem_rm__ud2_c16, },
            { false,  true, BS3_SEL_R0_CS16,        16, false, bs3CpuBasic2_callf_mem_same_r0__ud2_c16, },
            { false,  true, BS3_SEL_R1_CS16 | 1,    16, false, bs3CpuBasic2_callf_mem_same_r1__ud2_c16, },
            { false,  true, BS3_SEL_R2_CS16 | 2,    16, false, bs3CpuBasic2_callf_mem_same_r2__ud2_c16, },
            { false,  true, BS3_SEL_R3_CS16 | 3,    16, false, bs3CpuBasic2_callf_mem_same_r3__ud2_c16, },
            { false,  true, BS3_SEL_R0_CS16,        16, false, bs3CpuBasic2_callf_mem_r0_cs16__ud2_c16, },
            { false,  true, BS3_SEL_R0_CS32,        32,  true, bs3CpuBasic2_callf_mem_r0_cs32__ud2_c16, },
            { false,  true, BS3_SEL_R0_CS64,        64,  true, bs3CpuBasic2_callf_mem_r0_cs64__ud2_c16, },  /* 16-bit CS, except in LM. */
            { false,  true, BS3_SEL_SPARE_00,       64, false, bs3CpuBasic2_callf_mem_r0_cs16l__ud2_c16, }, /* 16-bit CS, except in LM. */
        };
        bool const fRmOrV86 = BS3_MODE_IS_RM_OR_V86(bMode);

        /* Prepare a copy of the SALC & UD2 instructions in low memory for opsize
           prefixed tests jumping to BS3_SEL_SPARE_00 when in 64-bit mode, because
           it'll be a 64-bit CS then with base=0 instead of a CS16 with base=0x10000. */
        if (BS3_MODE_IS_64BIT_SYS(bMode))
        {
            uint16_t const          offLow   = BS3_FP_OFF(bs3CpuBasic2_far_jmp_call_opsize_begin_c16);
            uint16_t const          cbLow    = BS3_FP_OFF(bs3CpuBasic2_far_jmp_call_opsize_end_c16) - offLow;
            uint8_t BS3_FAR * const pbLow    = BS3_FP_MAKE(BS3_SEL_TILED_R0, 0);
            uint8_t BS3_FAR * const pbCode16 = BS3_MAKE_PROT_R0PTR_FROM_FLAT(BS3_ADDR_BS3TEXT16);
            if (offLow < 0x600 || offLow + cbLow >= BS3_ADDR_STACK_R2)
                Bs3TestFailedF("Opsize overriden jumps/calls are out of place: %#x LB %#x\n", offLow, cbLow);
            Bs3MemSet(&pbLow[offLow], 0xcc /*int3*/, cbLow);
            for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
                if (s_aTests[iTest].uDstSel == BS3_SEL_SPARE_00 && s_aTests[iTest].uDstBits == 64)
                {
                    uint16_t const offFn = BS3_FP_OFF(s_aTests[iTest].pfnTest);
                    uint16_t const offUd = offFn + (int16_t)(int8_t)pbCode16[offFn - 1];
                    BS3_ASSERT(offUd - offLow + 1 < cbLow);
                    pbLow[offUd - 1]    = 0xd6; /* plant salc + ud2 in low memory */
                    pbLow[offUd]        = 0x0f;
                    pbLow[offUd + 1]    = 0x0b;
                }
        }

        for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
            if (s_aTests[iTest].fRmOrV86 == fRmOrV86)
            {
                uint64_t const         uSavedRsp = Ctx.rsp.u;
                bool const             fGp       = (s_aTests[iTest].uDstSel & X86_SEL_RPL) != 0;
                uint8_t const BS3_FAR *fpbCode;

                Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, s_aTests[iTest].pfnTest);
                fpbCode = (uint8_t const BS3_FAR *)BS3_FP_MAKE(Ctx.cs, Ctx.rip.u16);
                CtxExpected.rip.u = Ctx.rip.u + (int64_t)(int8_t)fpbCode[-1];
                if (   s_aTests[iTest].uDstBits == 32
                    || (   s_aTests[iTest].uDstBits == 64
                        && !BS3_MODE_IS_16BIT_SYS(bMode)
                        && s_aTests[iTest].uDstSel != BS3_SEL_SPARE_00))
                    CtxExpected.rip.u += BS3_ADDR_BS3TEXT16;
                if (s_aTests[iTest].uDstSel == BS3_SEL_SPARE_00 && s_aTests[iTest].uDstBits == 64 && BS3_MODE_IS_64BIT_SYS(bMode))
                    CtxExpected.rip.u &= UINT16_MAX;
                CtxExpected.cs    = s_aTests[iTest].uDstSel;
                if (fGp)
                {
                    CtxExpected.rip.u = Ctx.rip.u;
                    CtxExpected.cs    = Ctx.cs;
                }
                g_uBs3TrapEipHint = CtxExpected.rip.u32;
                CtxExpected.rsp.u = Ctx.rsp.u;
                if (s_aTests[iTest].fCall && !fGp)
                    CtxExpected.rsp.u -= s_aTests[iTest].fOpSizePfx ? 8 : 4;
                if (s_aTests[iTest].uDstBits == 64 && !fGp)
                {
                    if (BS3_MODE_IS_64BIT_SYS(bMode))
                        CtxExpected.rip.u -= 1;
                    else
                        CtxExpected.rax.u8 = CtxExpected.rflags.u & X86_EFL_CF ? 0xff : 0x00;
                }
                //Bs3TestPrintf("cs:rip=%04RX16:%04RX64 -> %04RX16:%04RX64\n", Ctx.cs, Ctx.rip.u, CtxExpected.cs, CtxExpected.rip.u);
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                if (!fGp)
                    bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
                else
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aTests[iTest].uDstSel & X86_TRAP_ERR_SEL_MASK);
                Ctx.rsp.u = uSavedRsp;
                g_usBs3TestStep++;

                /* Again single stepping: */
                //Bs3TestPrintf("stepping...\n");
                Bs3RegSetDr6(X86_DR6_INIT_VAL);
                Ctx.rflags.u16        |= X86_EFL_TF;
                CtxExpected.rflags.u16 = Ctx.rflags.u16;
                CtxExpected.rax.u      = Ctx.rax.u;
                if (s_aTests[iTest].uDstBits == 64 && !fGp && !BS3_MODE_IS_64BIT_SYS(bMode))
                    CtxExpected.rip.u -= 1;
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                if (!fGp)
                    bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected, X86_DR6_BS);
                else
                {
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aTests[iTest].uDstSel & X86_TRAP_ERR_SEL_MASK);
                    bs3CpuBasic2_CheckDr6InitVal();
                }
                Ctx.rflags.u16        &= ~X86_EFL_TF;
                CtxExpected.rflags.u16 = Ctx.rflags.u16;
                Ctx.rsp.u              = uSavedRsp;
                g_usBs3TestStep++;
            }
    }
    /*
     * 32-bit tests.
     */
    else if (BS3_MODE_IS_32BIT_CODE(bMode))
    {
        static struct
        {
            bool        fCall;
            uint16_t    uDstSel;
            uint8_t     uDstBits;
            bool        fOpSizePfx;
            FPFNBS3FAR  pfnTest;
        }
        const s_aTests[] =
        {
            { false, BS3_SEL_R0_CS32,        32, false, bs3CpuBasic2_jmpf_ptr_same_r0__ud2_c32, },
            { false, BS3_SEL_R1_CS32 | 1,    32, false, bs3CpuBasic2_jmpf_ptr_same_r1__ud2_c32, },
            { false, BS3_SEL_R2_CS32 | 2,    32, false, bs3CpuBasic2_jmpf_ptr_same_r2__ud2_c32, },
            { false, BS3_SEL_R3_CS32 | 3,    32, false, bs3CpuBasic2_jmpf_ptr_same_r3__ud2_c32, },
            { false, BS3_SEL_R0_CS16,        16,  true, bs3CpuBasic2_jmpf_ptr_opsize_flipbit_r0__ud2_c32, },
            { false, BS3_SEL_R0_CS64,        64, false, bs3CpuBasic2_jmpf_ptr_r0_cs64__ud2_c32, },  /* 16-bit CS, except in LM. */
            { false, BS3_SEL_SPARE_00,       64,  true, bs3CpuBasic2_jmpf_ptr_r0_cs16l__ud2_c32, }, /* 16-bit CS, except in LM. */

            {  true, BS3_SEL_R0_CS32,        32, false, bs3CpuBasic2_callf_ptr_same_r0__ud2_c32, },
            {  true, BS3_SEL_R1_CS32 | 1,    32, false, bs3CpuBasic2_callf_ptr_same_r1__ud2_c32, },
            {  true, BS3_SEL_R2_CS32 | 2,    32, false, bs3CpuBasic2_callf_ptr_same_r2__ud2_c32, },
            {  true, BS3_SEL_R3_CS32 | 3,    32, false, bs3CpuBasic2_callf_ptr_same_r3__ud2_c32, },
            {  true, BS3_SEL_R0_CS16,        16,  true, bs3CpuBasic2_callf_ptr_opsize_flipbit_r0__ud2_c32, },
            {  true, BS3_SEL_R0_CS64,        64, false, bs3CpuBasic2_callf_ptr_r0_cs64__ud2_c32, },  /* 16-bit CS, except in LM. */
            {  true, BS3_SEL_SPARE_00,       64,  true, bs3CpuBasic2_callf_ptr_r0_cs16l__ud2_c32, }, /* 16-bit CS, except in LM. */

            { false, BS3_SEL_R0_CS32,        32, false, bs3CpuBasic2_jmpf_mem_same_r0__ud2_c32, },
            { false, BS3_SEL_R1_CS32 | 1,    32, false, bs3CpuBasic2_jmpf_mem_same_r1__ud2_c32, },
            { false, BS3_SEL_R2_CS32 | 2,    32, false, bs3CpuBasic2_jmpf_mem_same_r2__ud2_c32, },
            { false, BS3_SEL_R3_CS32 | 3,    32, false, bs3CpuBasic2_jmpf_mem_same_r3__ud2_c32, },
            { false, BS3_SEL_R0_CS16,        16,  true, bs3CpuBasic2_jmpf_mem_r0_cs16__ud2_c32, },
            { false, BS3_SEL_R0_CS32,        32, false, bs3CpuBasic2_jmpf_mem_r0_cs32__ud2_c32, },
            { false, BS3_SEL_R0_CS64,        64, false, bs3CpuBasic2_jmpf_mem_r0_cs64__ud2_c32, },  /* 16-bit CS, except in LM. */
            { false, BS3_SEL_SPARE_00,       64,  true, bs3CpuBasic2_jmpf_mem_r0_cs16l__ud2_c32, }, /* 16-bit CS, except in LM. */

            {  true, BS3_SEL_R0_CS32,        32, false, bs3CpuBasic2_callf_mem_same_r0__ud2_c32, },
            {  true, BS3_SEL_R1_CS32 | 1,    32, false, bs3CpuBasic2_callf_mem_same_r1__ud2_c32, },
            {  true, BS3_SEL_R2_CS32 | 2,    32, false, bs3CpuBasic2_callf_mem_same_r2__ud2_c32, },
            {  true, BS3_SEL_R3_CS32 | 3,    32, false, bs3CpuBasic2_callf_mem_same_r3__ud2_c32, },
            {  true, BS3_SEL_R0_CS16,        16,  true, bs3CpuBasic2_callf_mem_r0_cs16__ud2_c32, },
            {  true, BS3_SEL_R0_CS32,        32, false, bs3CpuBasic2_callf_mem_r0_cs32__ud2_c32, },
            {  true, BS3_SEL_R0_CS64,        64, false, bs3CpuBasic2_callf_mem_r0_cs64__ud2_c32, },  /* 16-bit CS, except in LM. */
            {  true, BS3_SEL_SPARE_00,       64,  true, bs3CpuBasic2_callf_mem_r0_cs16l__ud2_c32, }, /* 16-bit CS, except in LM. */
        };

        /* Prepare a copy of the SALC & UD2 instructions in low memory for opsize
           prefixed tests jumping to BS3_SEL_SPARE_00 when in 64-bit mode, because
           it'll be a 64-bit CS then with base=0 instead of a CS16 with base=0x10000. */
        if (BS3_MODE_IS_64BIT_SYS(bMode))
        {
            uint16_t const          offLow   = BS3_FP_OFF(bs3CpuBasic2_far_jmp_call_opsize_begin_c32);
            uint16_t const          cbLow    = BS3_FP_OFF(bs3CpuBasic2_far_jmp_call_opsize_end_c32) - offLow;
            uint8_t BS3_FAR * const pbLow    = BS3_FP_MAKE(BS3_SEL_TILED_R0, 0);
            uint8_t BS3_FAR * const pbCode16 = BS3_MAKE_PROT_R0PTR_FROM_FLAT(BS3_ADDR_BS3TEXT16);
            if (offLow < 0x600 || offLow + cbLow >= BS3_ADDR_STACK_R2)
                Bs3TestFailedF("Opsize overriden jumps/calls are out of place: %#x LB %#x\n", offLow, cbLow);
            Bs3MemSet(&pbLow[offLow], 0xcc /*int3*/, cbLow);
            for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
                if (s_aTests[iTest].uDstSel == BS3_SEL_SPARE_00 && s_aTests[iTest].uDstBits == 64)
                {
                    uint16_t const offFn = BS3_FP_OFF(s_aTests[iTest].pfnTest);
                    uint16_t const offUd = offFn + (int16_t)(int8_t)pbCode16[offFn - 1];
                    BS3_ASSERT(offUd - offLow + 1 < cbLow);
                    pbLow[offUd - 1]    = 0xd6; /* plant salc + ud2 in low memory */
                    pbLow[offUd]        = 0x0f;
                    pbLow[offUd + 1]    = 0x0b;
                }
        }
        for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
        {
            uint64_t const         uSavedRsp = Ctx.rsp.u;
            bool const             fGp       = (s_aTests[iTest].uDstSel & X86_SEL_RPL) != 0;
            uint8_t const BS3_FAR *fpbCode   = Bs3SelLnkPtrToCurPtr(s_aTests[iTest].pfnTest);

            Ctx.rip.u = Bs3SelLnkPtrToFlat(s_aTests[iTest].pfnTest);
            CtxExpected.rip.u = Ctx.rip.u + (int64_t)(int8_t)fpbCode[-1];
            if (   s_aTests[iTest].uDstBits == 16
                || (   s_aTests[iTest].uDstBits == 64
                    && (   BS3_MODE_IS_16BIT_SYS(bMode))
                        || s_aTests[iTest].uDstSel == BS3_SEL_SPARE_00))
                CtxExpected.rip.u &= UINT16_MAX;
            CtxExpected.cs    = s_aTests[iTest].uDstSel;
            if (fGp)
            {
                CtxExpected.rip.u = Ctx.rip.u;
                CtxExpected.cs    = Ctx.cs;
            }
            g_uBs3TrapEipHint = CtxExpected.rip.u32;
            CtxExpected.rsp.u = Ctx.rsp.u;
            if (s_aTests[iTest].fCall && !fGp)
                CtxExpected.rsp.u -= s_aTests[iTest].fOpSizePfx ? 4 : 8;
            if (s_aTests[iTest].uDstBits == 64 && !fGp)
            {
                if (BS3_MODE_IS_64BIT_SYS(bMode))
                    CtxExpected.rip.u -= 1;
                else
                    CtxExpected.rax.u8 = CtxExpected.rflags.u & X86_EFL_CF ? 0xff : 0x00;
            }
            //Bs3TestPrintf("cs:rip=%04RX16:%04RX64 -> %04RX16:%04RX64\n", Ctx.cs, Ctx.rip.u, CtxExpected.cs, CtxExpected.rip.u);
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (!fGp)
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
            else
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aTests[iTest].uDstSel & X86_TRAP_ERR_SEL_MASK);
            Ctx.rsp.u = uSavedRsp;
            g_usBs3TestStep++;

            /* Again single stepping: */
            //Bs3TestPrintf("stepping...\n");
            Bs3RegSetDr6(X86_DR6_INIT_VAL);
            Ctx.rflags.u16        |= X86_EFL_TF;
            CtxExpected.rflags.u16 = Ctx.rflags.u16;
            CtxExpected.rax.u      = Ctx.rax.u;
            if (s_aTests[iTest].uDstBits == 64 && !fGp && !BS3_MODE_IS_64BIT_SYS(bMode))
                CtxExpected.rip.u -= 1;
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (!fGp)
                bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected, X86_DR6_BS);
            else
            {
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aTests[iTest].uDstSel & X86_TRAP_ERR_SEL_MASK);
                bs3CpuBasic2_CheckDr6InitVal();
            }
            Ctx.rflags.u16        &= ~X86_EFL_TF;
            CtxExpected.rflags.u16 = Ctx.rflags.u16;
            Ctx.rsp.u              = uSavedRsp;
            g_usBs3TestStep++;
        }
    }
    /*
     * 64-bit tests.
     */
    else if (BS3_MODE_IS_64BIT_CODE(bMode))
    {
        static struct
        {
            bool        fInvalid;
            bool        fCall;
            uint16_t    uDstSel;
            uint8_t     uDstBits;
            uint8_t     fOpSizePfx; /**< 0: none, 1: 066h, 2: REX.W, 3: 066h REX.W */
            int8_t      fFix64OpSize;
            FPFNBS3FAR  pfnTest;
        }
        const s_aTests[] =
        {
            /* invalid opcodes: */
            {  true, false, BS3_SEL_R0_CS32,        64, 0,    -1, bs3CpuBasic2_jmpf_ptr_same_r0__ud2_c32, },
            {  true, false, BS3_SEL_R1_CS32 | 1,    64, 0,    -1, bs3CpuBasic2_jmpf_ptr_same_r1__ud2_c32, },
            {  true, false, BS3_SEL_R2_CS32 | 2,    64, 0,    -1, bs3CpuBasic2_jmpf_ptr_same_r2__ud2_c32, },
            {  true, false, BS3_SEL_R3_CS32 | 3,    64, 0,    -1, bs3CpuBasic2_jmpf_ptr_same_r3__ud2_c32, },
            {  true, false, BS3_SEL_R0_CS16,        64, 0,    -1, bs3CpuBasic2_jmpf_ptr_opsize_flipbit_r0__ud2_c32, },
            {  true, false, BS3_SEL_R0_CS64,        64, 0,    -1, bs3CpuBasic2_jmpf_ptr_r0_cs64__ud2_c32, },
            {  true, false, BS3_SEL_SPARE_00,       64, 0,    -1, bs3CpuBasic2_jmpf_ptr_r0_cs16l__ud2_c32, },

            {  true,  true, BS3_SEL_R0_CS32,        64, 0,    -1, bs3CpuBasic2_callf_ptr_same_r0__ud2_c32, },
            {  true,  true, BS3_SEL_R1_CS32 | 1,    64, 0,    -1, bs3CpuBasic2_callf_ptr_same_r1__ud2_c32, },
            {  true,  true, BS3_SEL_R2_CS32 | 2,    64, 0,    -1, bs3CpuBasic2_callf_ptr_same_r2__ud2_c32, },
            {  true,  true, BS3_SEL_R3_CS32 | 3,    64, 0,    -1, bs3CpuBasic2_callf_ptr_same_r3__ud2_c32, },
            {  true,  true, BS3_SEL_R0_CS16,        64, 0,    -1, bs3CpuBasic2_callf_ptr_opsize_flipbit_r0__ud2_c32, },
            {  true,  true, BS3_SEL_R0_CS64,        64, 0,    -1, bs3CpuBasic2_callf_ptr_r0_cs64__ud2_c32, },
            {  true,  true, BS3_SEL_SPARE_00,       64, 0,    -1, bs3CpuBasic2_callf_ptr_r0_cs16l__ud2_c32, },

            { false, false, BS3_SEL_R0_CS64,        64, 0, false, bs3CpuBasic2_jmpf_mem_same_r0__ud2_c64, },
            { false, false, BS3_SEL_R1_CS64 | 1,    64, 0, false, bs3CpuBasic2_jmpf_mem_same_r1__ud2_c64, },
            { false, false, BS3_SEL_R2_CS64 | 2,    64, 0, false, bs3CpuBasic2_jmpf_mem_same_r2__ud2_c64, },
            { false, false, BS3_SEL_R3_CS64 | 3,    64, 0, false, bs3CpuBasic2_jmpf_mem_same_r3__ud2_c64, },
            { false, false, BS3_SEL_R0_CS16,        16, 1, false, bs3CpuBasic2_jmpf_mem_r0_cs16__ud2_c64, },
            { false, false, BS3_SEL_R0_CS32,        32, 0, false, bs3CpuBasic2_jmpf_mem_r0_cs32__ud2_c64, },
            { false, false, BS3_SEL_R0_CS64,        64, 0, false, bs3CpuBasic2_jmpf_mem_r0_cs64__ud2_c64, },  /* 16-bit CS, except in LM. */
            { false, false, BS3_SEL_SPARE_00,       64, 0, false, bs3CpuBasic2_jmpf_mem_r0_cs16l__ud2_c64, }, /* 16-bit CS, except in LM. */

            { false, false, BS3_SEL_R0_CS64,        64, 2,  true, bs3CpuBasic2_jmpf_mem_same_r0__ud2_intel_c64, },
            { false, false, BS3_SEL_R1_CS64 | 1,    64, 2,  true, bs3CpuBasic2_jmpf_mem_same_r1__ud2_intel_c64, },
            { false, false, BS3_SEL_R2_CS64 | 2,    64, 0,  true, bs3CpuBasic2_jmpf_mem_same_r2__ud2_intel_c64, },
            { false, false, BS3_SEL_R3_CS64 | 3,    64, 2,  true, bs3CpuBasic2_jmpf_mem_same_r3__ud2_intel_c64, },
            { false, false, BS3_SEL_R0_CS16,        16, 1,  true, bs3CpuBasic2_jmpf_mem_r0_cs16__ud2_intel_c64, },
            { false, false, BS3_SEL_R0_CS32,        32, 0,  true, bs3CpuBasic2_jmpf_mem_r0_cs32__ud2_intel_c64, },
            { false, false, BS3_SEL_R0_CS64,        64, 2,  true, bs3CpuBasic2_jmpf_mem_r0_cs64__ud2_intel_c64, },  /* 16-bit CS, except in LM. */
            { false, false, BS3_SEL_SPARE_00,       64, 0,  true, bs3CpuBasic2_jmpf_mem_r0_cs16l__ud2_intel_c64, }, /* 16-bit CS, except in LM. */

            { false,  true, BS3_SEL_R0_CS64,        64, 2, false, bs3CpuBasic2_callf_mem_same_r0__ud2_c64, },
            { false,  true, BS3_SEL_R1_CS64 | 1,    64, 2, false, bs3CpuBasic2_callf_mem_same_r1__ud2_c64, },
            { false,  true, BS3_SEL_R2_CS64 | 2,    64, 0, false, bs3CpuBasic2_callf_mem_same_r2__ud2_c64, },
            { false,  true, BS3_SEL_R3_CS64 | 3,    64, 2, false, bs3CpuBasic2_callf_mem_same_r3__ud2_c64, },
            { false,  true, BS3_SEL_R0_CS16,        16, 1, false, bs3CpuBasic2_callf_mem_r0_cs16__ud2_c64, },
            { false,  true, BS3_SEL_R0_CS32,        32, 2, false, bs3CpuBasic2_callf_mem_r0_cs32__ud2_c64, },
            { false,  true, BS3_SEL_R0_CS64,        64, 0, false, bs3CpuBasic2_callf_mem_r0_cs64__ud2_c64, },  /* 16-bit CS, except in LM. */
            { false,  true, BS3_SEL_SPARE_00,       64, 0, false, bs3CpuBasic2_callf_mem_r0_cs16l__ud2_c64, }, /* 16-bit CS, except in LM. */

            { false,  true, BS3_SEL_R0_CS64,        64, 2,  true, bs3CpuBasic2_callf_mem_same_r0__ud2_intel_c64, },
            { false,  true, BS3_SEL_R1_CS64 | 1,    64, 2,  true, bs3CpuBasic2_callf_mem_same_r1__ud2_intel_c64, },
            { false,  true, BS3_SEL_R2_CS64 | 2,    64, 0,  true, bs3CpuBasic2_callf_mem_same_r2__ud2_intel_c64, },
            { false,  true, BS3_SEL_R3_CS64 | 3,    64, 2,  true, bs3CpuBasic2_callf_mem_same_r3__ud2_intel_c64, },
            { false,  true, BS3_SEL_R0_CS16,        16, 1,  true, bs3CpuBasic2_callf_mem_r0_cs16__ud2_intel_c64, },
            { false,  true, BS3_SEL_R0_CS32,        32, 0,  true, bs3CpuBasic2_callf_mem_r0_cs32__ud2_intel_c64, },
            { false,  true, BS3_SEL_R0_CS64,        64, 2,  true, bs3CpuBasic2_callf_mem_r0_cs64__ud2_intel_c64, },  /* 16-bit CS, except in LM. */
            { false,  true, BS3_SEL_SPARE_00,       64, 0,  true, bs3CpuBasic2_callf_mem_r0_cs16l__ud2_intel_c64, }, /* 16-bit CS, except in LM. */
        };
        BS3CPUVENDOR const enmCpuVendor = Bs3GetCpuVendor();
        bool const         fFix64OpSize = enmCpuVendor == BS3CPUVENDOR_INTEL; /** @todo what does VIA do? */

        for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
        {
            uint64_t const         uSavedRsp = Ctx.rsp.u;
            bool const             fUd       = s_aTests[iTest].fInvalid;
            bool const             fGp       = (s_aTests[iTest].uDstSel & X86_SEL_RPL) != 0;
            uint8_t const BS3_FAR *fpbCode   = Bs3SelLnkPtrToCurPtr(s_aTests[iTest].pfnTest);

            if (s_aTests[iTest].fFix64OpSize != fFix64OpSize && s_aTests[iTest].fFix64OpSize >= 0)
                continue;

            Ctx.rip.u = Bs3SelLnkPtrToFlat(s_aTests[iTest].pfnTest);
            CtxExpected.rip.u = Ctx.rip.u + (int64_t)(int8_t)fpbCode[-1];
            CtxExpected.cs    = s_aTests[iTest].uDstSel;
            if (s_aTests[iTest].uDstBits == 16)
                CtxExpected.rip.u &= UINT16_MAX;
            else if (s_aTests[iTest].uDstBits == 64 && fFix64OpSize && s_aTests[iTest].uDstSel != BS3_SEL_SPARE_00)
                CtxExpected.rip.u |= UINT64_C(0xfffff00000000000);

            if (fGp || fUd)
            {
                CtxExpected.rip.u = Ctx.rip.u;
                CtxExpected.cs    = Ctx.cs;
            }
            CtxExpected.rsp.u = Ctx.rsp.u;
            if (s_aTests[iTest].fCall && !fGp && !fUd)
            {
                CtxExpected.rsp.u -= s_aTests[iTest].fOpSizePfx == 0 ? 8
                                   : s_aTests[iTest].fOpSizePfx == 1 ? 4 : 16;
                //Bs3TestPrintf("cs:rsp=%04RX16:%04RX64 -> %04RX64 (fOpSizePfx=%d)\n", Ctx.ss, Ctx.rsp.u, CtxExpected.rsp.u, s_aTests[iTest].fOpSizePfx);
            }
            //Bs3TestPrintf("cs:rip=%04RX16:%04RX64 -> %04RX16:%04RX64\n", Ctx.cs, Ctx.rip.u, CtxExpected.cs, CtxExpected.rip.u);
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (!fGp || fUd)
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
            else
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aTests[iTest].uDstSel & X86_TRAP_ERR_SEL_MASK);
            Ctx.rsp.u = uSavedRsp;
            g_usBs3TestStep++;

            /* Again single stepping: */
            //Bs3TestPrintf("stepping...\n");
            Bs3RegSetDr6(X86_DR6_INIT_VAL);
            Ctx.rflags.u16        |= X86_EFL_TF;
            CtxExpected.rflags.u16 = Ctx.rflags.u16;
            CtxExpected.rax.u      = Ctx.rax.u;
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (fUd)
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
            else if (!fGp)
                bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected, X86_DR6_BS);
            else
            {
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aTests[iTest].uDstSel & X86_TRAP_ERR_SEL_MASK);
                bs3CpuBasic2_CheckDr6InitVal();
            }
            Ctx.rflags.u16        &= ~X86_EFL_TF;
            CtxExpected.rflags.u16 = Ctx.rflags.u16;
            Ctx.rsp.u              = uSavedRsp;
            g_usBs3TestStep++;
        }
    }
    else
        Bs3TestFailed("wtf?");

    return 0;
}


/*********************************************************************************************************************************
*   Near RET                                                                                                                     *
*********************************************************************************************************************************/
#define PROTO_ALL(a_Template) \
    FNBS3FAR a_Template ## _c16, \
             a_Template ## _c32, \
             a_Template ## _c64
PROTO_ALL(bs3CpuBasic2_retn_opsize_begin);
PROTO_ALL(bs3CpuBasic2_retn__ud2);
PROTO_ALL(bs3CpuBasic2_retn_opsize__ud2);
PROTO_ALL(bs3CpuBasic2_retn_i24__ud2);
PROTO_ALL(bs3CpuBasic2_retn_i24_opsize__ud2);
PROTO_ALL(bs3CpuBasic2_retn_i760__ud2);
PROTO_ALL(bs3CpuBasic2_retn_i5193__ud2);
PROTO_ALL(bs3CpuBasic2_retn_i5193_opsize__ud2);
PROTO_ALL(bs3CpuBasic2_retn_i0__ud2);
PROTO_ALL(bs3CpuBasic2_retn_i0_opsize__ud2);
FNBS3FAR  bs3CpuBasic2_retn_rexw__ud2_c64;
FNBS3FAR  bs3CpuBasic2_retn_i24_rexw__ud2_c64;
FNBS3FAR  bs3CpuBasic2_retn_i5193_rexw__ud2_c64;
FNBS3FAR  bs3CpuBasic2_retn_opsize_rexw__ud2_c64;
FNBS3FAR  bs3CpuBasic2_retn_rexw_opsize__ud2_c64;
FNBS3FAR  bs3CpuBasic2_retn_i24_opsize_rexw__ud2_c64;
FNBS3FAR  bs3CpuBasic2_retn_i24_rexw_opsize__ud2_c64;
FNBS3FAR  bs3CpuBasic2_retn_i5193_opsize_rexw__ud2_c64;
FNBS3FAR  bs3CpuBasic2_retn_i5193_rexw_opsize__ud2_c64;
PROTO_ALL(bs3CpuBasic2_retn_begin);
PROTO_ALL(bs3CpuBasic2_retn_end);
PROTO_ALL(bs3CpuBasic2_retn_opsize_end);
#undef PROTO_ALL


static void bs3CpuBasic2_retn_PrepStack(BS3PTRUNION StkPtr, PCBS3REGCTX pCtxExpected, uint8_t cbAddr)
{
    StkPtr.pu32[3]  = UINT32_MAX;
    StkPtr.pu32[2]  = UINT32_MAX;
    StkPtr.pu32[1]  = UINT32_MAX;
    StkPtr.pu32[0]  = UINT32_MAX;
    StkPtr.pu32[-1] = UINT32_MAX;
    StkPtr.pu32[-2] = UINT32_MAX;
    StkPtr.pu32[-3] = UINT32_MAX;
    StkPtr.pu32[-4] = UINT32_MAX;
    if (cbAddr == 2)
        StkPtr.pu16[0] = pCtxExpected->rip.u16;
    else if (cbAddr == 4)
        StkPtr.pu32[0] = pCtxExpected->rip.u32;
    else
        StkPtr.pu64[0] = pCtxExpected->rip.u64;
}


static void bs3CpuBasic2_retn_PrepStackInvalid(BS3PTRUNION StkPtr, PCBS3REGCTX pCtxExpected, uint8_t cbAddr)
{
    StkPtr.pu32[3]  = UINT32_MAX;
    StkPtr.pu32[2]  = UINT32_MAX;
    StkPtr.pu32[1]  = UINT32_MAX;
    StkPtr.pu32[0]  = UINT32_MAX;
    StkPtr.pu32[-1] = UINT32_MAX;
    StkPtr.pu32[-2] = UINT32_MAX;
    StkPtr.pu32[-3] = UINT32_MAX;
    StkPtr.pu32[-4] = UINT32_MAX;
    if (cbAddr == 2)
        StkPtr.pu16[0] = BS3_FP_OFF(bs3CpuBasic2_retn_opsize_end_c16);
    else if (cbAddr == 4)
        StkPtr.pu32[0] = BS3_FP_OFF(bs3CpuBasic2_retn_end_c32) - BS3_FP_OFF(bs3CpuBasic2_retn_begin_c32);
    else
        StkPtr.pu64[0] = UINT64_C(0x0000800000000000);
}


/**
 * Entrypoint for NEAR RET tests.
 *
 * @returns 0 or BS3TESTDOMODE_SKIPPED.
 * @param   bMode       The CPU mode we're testing.
 */
BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuBasic2_near_ret)(uint8_t bMode)
{
    BS3TRAPFRAME            TrapCtx;
    BS3REGCTX               Ctx;
    BS3REGCTX               CtxExpected;
    unsigned                iTest;
    BS3PTRUNION             StkPtr;
    unsigned const          cMaxRecompRuns = g_cBs3ThresholdNativeRecompiler ? g_cBs3ThresholdNativeRecompiler : 1;
    unsigned                iRecompRun;

    /* make sure they're allocated  */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&CtxExpected, sizeof(Ctx));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));

    bs3CpuBasic2_SetGlobals(bMode);

    /*
     * Create a context.
     *
     * ASSUMES we're in on the ring-0 stack in ring-0 and using less than 16KB.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 1664);
    Ctx.rsp.u = BS3_ADDR_STACK - _16K;
    Bs3MemCpy(&CtxExpected, &Ctx, sizeof(CtxExpected));

    StkPtr.pv = Bs3RegCtxGetRspSsAsCurPtr(&Ctx);
    //Bs3TestPrintf("Stack=%p rsp=%RX64\n", StkPtr.pv, Ctx.rsp.u);

    /*
     * 16-bit tests.
     */
    if (BS3_MODE_IS_16BIT_CODE(bMode))
    {
        static struct
        {
            bool        fOpSizePfx;
            uint16_t    cbImm;
            FPFNBS3FAR  pfnTest;
        }
        const s_aTests[] =
        {
            { false,    0, bs3CpuBasic2_retn__ud2_c16, },
            {  true,    0, bs3CpuBasic2_retn_opsize__ud2_c16, },
            { false,   24, bs3CpuBasic2_retn_i24__ud2_c16, },
            {  true,   24, bs3CpuBasic2_retn_i24_opsize__ud2_c16, },
            { false,    0, bs3CpuBasic2_retn_i0__ud2_c16, },
            {  true,    0, bs3CpuBasic2_retn_i0_opsize__ud2_c16, },
            { false,  760, bs3CpuBasic2_retn_i760__ud2_c16, },
            { false, 5193, bs3CpuBasic2_retn_i5193__ud2_c16, },
            { true,  5193, bs3CpuBasic2_retn_i5193_opsize__ud2_c16, },
        };

        for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
        {
            uint8_t const BS3_FAR *fpbCode;

            Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, s_aTests[iTest].pfnTest);
            fpbCode = (uint8_t const BS3_FAR *)BS3_FP_MAKE(Ctx.cs, Ctx.rip.u16);
            CtxExpected.rip.u = Ctx.rip.u + (int64_t)(int8_t)fpbCode[-1];
            g_uBs3TrapEipHint = CtxExpected.rip.u32;
            CtxExpected.cs    = Ctx.cs;
            if (!s_aTests[iTest].fOpSizePfx)
                CtxExpected.rsp.u = Ctx.rsp.u + s_aTests[iTest].cbImm + 2;
            else
                CtxExpected.rsp.u = Ctx.rsp.u + s_aTests[iTest].cbImm + 4;
            //Bs3TestPrintf("cs:rip=%04RX16:%04RX64 -> %04RX16:%04RX64\n", Ctx.cs, Ctx.rip.u, CtxExpected.cs, CtxExpected.rip.u);
            //Bs3TestPrintf("ss:rsp=%04RX16:%04RX64\n", Ctx.ss, Ctx.rsp.u);
            for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
            {
                bs3CpuBasic2_retn_PrepStack(StkPtr, &CtxExpected, s_aTests[iTest].fOpSizePfx ? 4 : 2);
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
            }
            g_usBs3TestStep++;

            /* Again single stepping: */
            //Bs3TestPrintf("stepping...\n");
            Ctx.rflags.u16        |= X86_EFL_TF;
            CtxExpected.rflags.u16 = Ctx.rflags.u16;
            for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
            {
                Bs3RegSetDr6(X86_DR6_INIT_VAL);
                bs3CpuBasic2_retn_PrepStack(StkPtr, &CtxExpected, s_aTests[iTest].fOpSizePfx ? 4 : 2);
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected, X86_DR6_BS);
            }
            Ctx.rflags.u16        &= ~X86_EFL_TF;
            CtxExpected.rflags.u16 = Ctx.rflags.u16;
            g_usBs3TestStep++;

            /*
             * Test exceeding the selector limit by using a spare selector and
             * set the limit just below the return target.
             */
            if (!BS3_MODE_IS_RM_OR_V86(bMode))
            {
                Bs3SelSetup16BitCode(&Bs3GdteSpare03, BS3_ADDR_BS3TEXT16, 0);

                Ctx.cs         = BS3_SEL_SPARE_03;
                CtxExpected.cs = Ctx.cs;

                Bs3GdteSpare03.Gen.u16LimitLow = BS3_FP_OFF(bs3CpuBasic2_retn_opsize_end_c16) - 1;

                Ctx.rip.u = BS3_FP_OFF(s_aTests[iTest].pfnTest);
                CtxExpected.rip.u = Ctx.rip.u;
                CtxExpected.rsp.u = Ctx.rsp.u;
                //Bs3TestPrintf("cs:rip=%04RX16:%04RX64 v2\n", Ctx.cs, Ctx.rip.u);
                for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
                {
                    bs3CpuBasic2_retn_PrepStackInvalid(StkPtr, &CtxExpected, 2);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, 0);
                }
                g_usBs3TestStep++;
            }
        }
    }
    /*
     * 32-bit tests.
     */
    else if (BS3_MODE_IS_32BIT_CODE(bMode))
    {
        static struct
        {
            uint8_t     cBits;
            bool        fOpSizePfx;
            uint16_t    cbImm;
            FPFNBS3FAR  pfnTest;
        }
        const s_aTests[] =
        {
            { 32, false,    0, bs3CpuBasic2_retn__ud2_c32, },
            { 32,  true,    0, bs3CpuBasic2_retn_opsize__ud2_c32, },
            { 32, false,   24, bs3CpuBasic2_retn_i24__ud2_c32, },
            { 32,  true,   24, bs3CpuBasic2_retn_i24_opsize__ud2_c32, },
            { 32, false,    0, bs3CpuBasic2_retn_i0__ud2_c32, },
            { 32,  true,    0, bs3CpuBasic2_retn_i0_opsize__ud2_c32, },
            { 32, false,  760, bs3CpuBasic2_retn_i760__ud2_c32, },
            { 32, false, 5193, bs3CpuBasic2_retn_i5193__ud2_c32, },
            { 32, true,  5193, bs3CpuBasic2_retn_i5193_opsize__ud2_c32, },
        };

        /* Prepare a copy of the UD2 instructions in low memory for opsize prefixed tests. */
        uint16_t const          offLow   = BS3_FP_OFF(bs3CpuBasic2_retn_opsize_begin_c32);
        uint16_t const          cbLow    = BS3_FP_OFF(bs3CpuBasic2_retn_opsize_end_c32) - offLow;
        uint8_t BS3_FAR * const pbLow    = BS3_FP_MAKE(BS3_SEL_TILED_R0, 0);
        uint8_t BS3_FAR * const pbCode16 = BS3_MAKE_PROT_R0PTR_FROM_FLAT(BS3_ADDR_BS3TEXT16);
        if (offLow < 0x600 || offLow + cbLow >= BS3_ADDR_STACK_R2)
            Bs3TestFailedF("Opsize overriden jumps/calls are out of place: %#x LB %#x\n", offLow, cbLow);
        Bs3MemSet(&pbLow[offLow], 0xcc /*int3*/, cbLow);
        for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
            if (s_aTests[iTest].fOpSizePfx)
            {
                uint16_t const offFn = BS3_FP_OFF(s_aTests[iTest].pfnTest);
                uint16_t const offUd = offFn + (int16_t)(int8_t)pbCode16[offFn - 1];
                BS3_ASSERT(offUd - offLow + 1 < cbLow);
                pbCode16[offUd]     = 0xf1; /* replace original ud2 with icebp */
                pbCode16[offUd + 1] = 0xf1;
                pbLow[offUd]        = 0x0f; /* plant ud2 in low memory */
                pbLow[offUd + 1]    = 0x0b;
            }

        for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
        {
            uint8_t const BS3_FAR *fpbCode   = Bs3SelLnkPtrToCurPtr(s_aTests[iTest].pfnTest);

            Ctx.rip.u = Bs3SelLnkPtrToFlat(s_aTests[iTest].pfnTest);
            CtxExpected.rip.u = Ctx.rip.u + (int64_t)(int8_t)fpbCode[-1];
            CtxExpected.cs    = Ctx.cs;
            if (!s_aTests[iTest].fOpSizePfx)
                CtxExpected.rsp.u = Ctx.rsp.u + s_aTests[iTest].cbImm + 4;
            else
            {
                CtxExpected.rsp.u  = Ctx.rsp.u + s_aTests[iTest].cbImm + 2;
                CtxExpected.rip.u &= UINT16_MAX;
            }
            g_uBs3TrapEipHint = CtxExpected.rip.u32;
            //Bs3TestPrintf("cs:rip=%04RX16:%04RX64 -> %04RX16:%04RX64\n", Ctx.cs, Ctx.rip.u, CtxExpected.cs, CtxExpected.rip.u);
            //Bs3TestPrintf("ss:rsp=%04RX16:%04RX64\n", Ctx.ss, Ctx.rsp.u);
            for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
            {
                bs3CpuBasic2_retn_PrepStack(StkPtr, &CtxExpected, s_aTests[iTest].fOpSizePfx ? 2 : 4);
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
            }
            g_usBs3TestStep++;

            /* Again single stepping: */
            //Bs3TestPrintf("stepping...\n");
            Ctx.rflags.u16        |= X86_EFL_TF;
            CtxExpected.rflags.u16 = Ctx.rflags.u16;
            for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
            {
                Bs3RegSetDr6(X86_DR6_INIT_VAL);
                bs3CpuBasic2_retn_PrepStack(StkPtr, &CtxExpected, s_aTests[iTest].fOpSizePfx ? 2 : 4);
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected, X86_DR6_BS);
            }
            Ctx.rflags.u16        &= ~X86_EFL_TF;
            CtxExpected.rflags.u16 = Ctx.rflags.u16;
            g_usBs3TestStep++;

            if (   !BS3_MODE_IS_16BIT_SYS_NO_RM(bMode)
                && !s_aTests[iTest].fOpSizePfx)
            {
                /*
                 * Test exceeding the selector limit by using a spare selector and
                 * set the limit just below the return target.
                 */
                uint16_t CsOrig = Ctx.cs;
                uint32_t const uFlatCode = Bs3SelLnkPtrToFlat(bs3CpuBasic2_retn_begin_c32);
                //Bs3TestPrintf("uFlatCode=%04RX32\n", uFlatCode);

                Bs3SelSetup32BitCode(&Bs3GdteSpare03, uFlatCode,
                                     BS3_FP_OFF(bs3CpuBasic2_retn_end_c32) - BS3_FP_OFF(bs3CpuBasic2_retn_begin_c32) - 1,
                                     0);

                Ctx.cs         = BS3_SEL_SPARE_03;
                CtxExpected.cs = Ctx.cs;
                Ctx.rip.u = BS3_FP_OFF(s_aTests[iTest].pfnTest) - BS3_FP_OFF(bs3CpuBasic2_retn_begin_c32);
                CtxExpected.rip.u = Ctx.rip.u;
                CtxExpected.rsp.u = Ctx.rsp.u;
                //Bs3TestPrintf("cs:rip=%04RX16:%04RX64 v2\n", Ctx.cs, Ctx.rip.u);
                for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
                {
                    bs3CpuBasic2_retn_PrepStackInvalid(StkPtr, &CtxExpected, 4);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, 0);
                }

                Ctx.cs = CsOrig;
                g_usBs3TestStep++;
            }
        }
    }
    /*
     * 64-bit tests.
     */
    else if (BS3_MODE_IS_64BIT_CODE(bMode))
    {
        static struct
        {
            uint8_t     cBits;
            bool        fOpSizePfx;
            uint16_t    cbImm;
            FPFNBS3FAR  pfnTest;
        }
        const s_aTests[] =
        {
            { 32, false,    0, bs3CpuBasic2_retn__ud2_c64, },
            { 32, false,    0, bs3CpuBasic2_retn_rexw__ud2_c64, },
            { 32,  true,    0, bs3CpuBasic2_retn_opsize__ud2_c64, },
            { 32, false,    0, bs3CpuBasic2_retn_opsize_rexw__ud2_c64, },
            { 32,  true,    0, bs3CpuBasic2_retn_rexw_opsize__ud2_c64, },
            { 32, false,   24, bs3CpuBasic2_retn_i24__ud2_c64, },
            { 32, false,   24, bs3CpuBasic2_retn_i24_rexw__ud2_c64, },
            { 32,  true,   24, bs3CpuBasic2_retn_i24_opsize__ud2_c64, },
            { 32, false,   24, bs3CpuBasic2_retn_i24_opsize_rexw__ud2_c64, },
            { 32,  true,   24, bs3CpuBasic2_retn_i24_rexw_opsize__ud2_c64, },
            { 32, false,    0, bs3CpuBasic2_retn_i0__ud2_c64, },
            { 32,  true,    0, bs3CpuBasic2_retn_i0_opsize__ud2_c64, },
            { 32, false,  760, bs3CpuBasic2_retn_i760__ud2_c64, },
            { 32, false, 5193, bs3CpuBasic2_retn_i5193__ud2_c64, },
            { 32, false, 5193, bs3CpuBasic2_retn_i5193_rexw__ud2_c64, },
            { 32,  true, 5193, bs3CpuBasic2_retn_i5193_opsize__ud2_c64, },
            { 32, false, 5193, bs3CpuBasic2_retn_i5193_opsize_rexw__ud2_c64, },
            { 32,  true, 5193, bs3CpuBasic2_retn_i5193_rexw_opsize__ud2_c64, },
        };
        BS3CPUVENDOR const enmCpuVendor = Bs3GetCpuVendor();
        bool const         fFix64OpSize = enmCpuVendor == BS3CPUVENDOR_INTEL; /** @todo what does VIA do? */

        /* Prepare a copy of the UD2 instructions in low memory for opsize prefixed
           tests, unless we're on intel where the opsize prefix is ignored. Here we
           just fill low memory with int3's so we can detect non-intel behaviour.  */
        uint16_t const          offLow   = BS3_FP_OFF(bs3CpuBasic2_retn_opsize_begin_c64);
        uint16_t const          cbLow    = BS3_FP_OFF(bs3CpuBasic2_retn_opsize_end_c64) - offLow;
        uint8_t BS3_FAR * const pbLow    = BS3_FP_MAKE(BS3_SEL_TILED_R0, 0);
        uint8_t BS3_FAR * const pbCode16 = BS3_MAKE_PROT_R0PTR_FROM_FLAT(BS3_ADDR_BS3TEXT16);
        if (offLow < 0x600 || offLow + cbLow >= BS3_ADDR_STACK_R2)
            Bs3TestFailedF("Opsize overriden jumps/calls are out of place: %#x LB %#x\n", offLow, cbLow);
        Bs3MemSet(&pbLow[offLow], 0xcc /*int3*/, cbLow);
        if (!fFix64OpSize)
            for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
                if (s_aTests[iTest].fOpSizePfx)
                {
                    uint16_t const offFn = BS3_FP_OFF(s_aTests[iTest].pfnTest);
                    uint16_t const offUd = offFn + (int16_t)(int8_t)pbCode16[offFn - 1];
                    BS3_ASSERT(offUd - offLow + 1 < cbLow);
                    pbCode16[offUd]     = 0xf1; /* replace original ud2 with icebp */
                    pbCode16[offUd + 1] = 0xf1;
                    pbLow[offUd]        = 0x0f; /* plant ud2 in low memory */
                    pbLow[offUd + 1]    = 0x0b;
                }

        for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
        {
            uint8_t const BS3_FAR *fpbCode   = Bs3SelLnkPtrToCurPtr(s_aTests[iTest].pfnTest);

            Ctx.rip.u = Bs3SelLnkPtrToFlat(s_aTests[iTest].pfnTest);
            CtxExpected.rip.u = Ctx.rip.u + (int64_t)(int8_t)fpbCode[-1];
            CtxExpected.cs    = Ctx.cs;
            if (!s_aTests[iTest].fOpSizePfx || fFix64OpSize)
                CtxExpected.rsp.u = Ctx.rsp.u + s_aTests[iTest].cbImm + 8;
            else
            {
                CtxExpected.rsp.u  = Ctx.rsp.u + s_aTests[iTest].cbImm + 2;
                CtxExpected.rip.u &= UINT16_MAX;
            }
            g_uBs3TrapEipHint = CtxExpected.rip.u32;
            //Bs3TestPrintf("cs:rip=%04RX16:%04RX64 -> %04RX16:%04RX64\n", Ctx.cs, Ctx.rip.u, CtxExpected.cs, CtxExpected.rip.u);
            //Bs3TestPrintf("ss:rsp=%04RX16:%04RX64\n", Ctx.ss, Ctx.rsp.u);
            for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
            {
                bs3CpuBasic2_retn_PrepStack(StkPtr, &CtxExpected, s_aTests[iTest].fOpSizePfx && !fFix64OpSize ? 2 : 8);
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
            }
            g_usBs3TestStep++;

            /* Again single stepping: */
            //Bs3TestPrintf("stepping...\n");
            Ctx.rflags.u16        |= X86_EFL_TF;
            CtxExpected.rflags.u16 = Ctx.rflags.u16;
            for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
            {
                Bs3RegSetDr6(X86_DR6_INIT_VAL);
                bs3CpuBasic2_retn_PrepStack(StkPtr, &CtxExpected, s_aTests[iTest].fOpSizePfx && !fFix64OpSize ? 2 : 8);
                Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected, X86_DR6_BS);
            }
            Ctx.rflags.u16        &= ~X86_EFL_TF;
            CtxExpected.rflags.u16 = Ctx.rflags.u16;
            g_usBs3TestStep++;

            /* Non-canonical return address (should \#GP(0)). */
            if (!s_aTests[iTest].fOpSizePfx || fFix64OpSize)
            {
                CtxExpected.rip.u = Ctx.rip.u;
                CtxExpected.cs    = Ctx.cs;
                CtxExpected.rsp.u = Ctx.rsp.u;
                for (iRecompRun = 0; iRecompRun < cMaxRecompRuns; iRecompRun++)
                {
                    bs3CpuBasic2_retn_PrepStackInvalid(StkPtr, &CtxExpected, 8);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, 0 /*uErrCd*/);
                }
            }
            g_usBs3TestStep++;
        }
    }
    else
        Bs3TestFailed("wtf?");

    return 0;
}


/*********************************************************************************************************************************
*   Far RET                                                                                                                      *
*********************************************************************************************************************************/
#define PROTO_ALL(a_Template) \
    FNBS3FAR a_Template ## _c16, \
             a_Template ## _c32, \
             a_Template ## _c64
PROTO_ALL(bs3CpuBasic2_retf);
PROTO_ALL(bs3CpuBasic2_retf_opsize);
FNBS3FAR  bs3CpuBasic2_retf_rexw_c64;
FNBS3FAR  bs3CpuBasic2_retf_rexw_opsize_c64;
FNBS3FAR  bs3CpuBasic2_retf_opsize_rexw_c64;
PROTO_ALL(bs3CpuBasic2_retf_i32);
PROTO_ALL(bs3CpuBasic2_retf_i32_opsize);
FNBS3FAR  bs3CpuBasic2_retf_i24_rexw_c64;
FNBS3FAR  bs3CpuBasic2_retf_i24_rexw_opsize_c64;
FNBS3FAR  bs3CpuBasic2_retf_i24_opsize_rexw_c64;
PROTO_ALL(bs3CpuBasic2_retf_i888);
#undef PROTO_ALL


static void bs3CpuBasic2_retf_PrepStack(BS3PTRUNION StkPtr, uint8_t cbStkItem, RTSEL uRetCs, uint64_t uRetRip,
                                        bool fWithStack, uint16_t cbImm, RTSEL uRetSs, uint64_t uRetRsp)
{
    Bs3MemSet(&StkPtr.pu32[-4], 0xff, 96);
    if (cbStkItem == 2)
    {
        StkPtr.pu16[0] = (uint16_t)uRetRip;
        StkPtr.pu16[1] = uRetCs;
        if (fWithStack)
        {
            StkPtr.pb += cbImm;
            StkPtr.pu16[2] = (uint16_t)uRetRsp;
            StkPtr.pu16[3] = uRetSs;
        }
    }
    else if (cbStkItem == 4)
    {
        StkPtr.pu32[0] = (uint32_t)uRetRip;
        StkPtr.pu16[2] = uRetCs;
        if (fWithStack)
        {
            StkPtr.pb += cbImm;
            StkPtr.pu32[2] = (uint32_t)uRetRsp;
            StkPtr.pu16[6] = uRetSs;
        }
    }
    else
    {
        StkPtr.pu64[0] = uRetRip;
        StkPtr.pu16[4] = uRetCs;
        if (fWithStack)
        {
            StkPtr.pb += cbImm;
            StkPtr.pu64[2]  = uRetRsp;
            StkPtr.pu16[12] = uRetSs;
        }
    }
}


/**
 * Entrypoint for FAR RET tests.
 *
 * @returns 0 or BS3TESTDOMODE_SKIPPED.
 * @param   bMode       The CPU mode we're testing.
 */
BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuBasic2_far_ret)(uint8_t bMode)
{
    BS3TRAPFRAME            TrapCtx;
    BS3REGCTX               Ctx;
    BS3REGCTX               Ctx2;
    BS3REGCTX               CtxExpected;
    unsigned                iTest;
    unsigned                iSubTest;
    BS3PTRUNION             StkPtr;

#define LOW_UD_ADDR             0x0609
    uint8_t BS3_FAR * const pbLowUd     = BS3_FP_MAKE(BS3_FP_SEG(&StkPtr), LOW_UD_ADDR);
#define LOW_SALC_UD_ADDR        0x0611
    uint8_t BS3_FAR * const pbLowSalcUd = BS3_FP_MAKE(BS3_FP_SEG(&StkPtr), LOW_SALC_UD_ADDR);
#define LOW_SWAPGS_ADDR         0x061d
    uint8_t BS3_FAR * const pbLowSwapGs = BS3_FP_MAKE(BS3_FP_SEG(&StkPtr), LOW_SWAPGS_ADDR);
#define BS3TEXT16_ADDR_HI   (BS3_ADDR_BS3TEXT16 >> 16)

    /* make sure they're allocated  */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&Ctx2, sizeof(Ctx2));
    Bs3MemZero(&CtxExpected, sizeof(CtxExpected));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));

    bs3CpuBasic2_SetGlobals(bMode);

    //if (!BS3_MODE_IS_64BIT_SYS(bMode) && bMode != BS3_MODE_PP32_16) return 0xff;
    //if (bMode != BS3_MODE_PE32_16) return 0xff;

    /*
     * When dealing retf with 16-bit effective operand size to 32-bit or 64-bit
     * code, we're restricted to a 16-bit address.  So, we plant a UD
     * instruction below 64KB that we can target with flat 32/64 code segments.
     * (Putting it on the stack would be possible too, but we'd have to create
     * the sub-test tables dynamically, which isn't necessary.)
     */
    Bs3MemSet(&pbLowUd[-9], 0xcc, 32);
    Bs3MemSet(&pbLowSalcUd[-9], 0xcc, 32);
    Bs3MemSet(&pbLowSwapGs[-9], 0xcc, 32);

    pbLowUd[0] = 0x0f;      /* ud2 */
    pbLowUd[1] = 0x0b;

    /* A variation to detect whether we're in 64-bit or 16-bit mode when
       executing the code. */
    pbLowSalcUd[0] = 0xd6;  /* salc */
    pbLowSalcUd[1] = 0x0f;  /* ud2 */
    pbLowSalcUd[2] = 0x0b;

    /* A variation to check that we're not in 64-bit mode. */
    pbLowSwapGs[0] = 0x0f;  /* swapgs */
    pbLowSwapGs[1] = 0x01;
    pbLowSwapGs[2] = 0xf8;

    /*
     * Use separate stacks for all relevant CPU exceptions so we can put
     * garbage in unused RSP bits w/o needing to care about where a long mode
     * handler will end up when accessing the whole RSP.  (Not an issue with
     * 16-bit and 32-bit protected mode kernels, as here the weird SS based
     * stack pointer handling is in effect and the exception handler code
     * will just continue using the same SS and same portion of RSP.)
     *
     * See r154660.
     */
    if (BS3_MODE_IS_64BIT_SYS(bMode))
        Bs3Trap64InitEx(true);

    /*
     * Create some call gates and whatnot for the UD2 code using the spare selectors.
     */
    if (BS3_MODE_IS_64BIT_SYS(bMode))
        for (iTest = 0; iTest < 16; iTest++)
            Bs3SelSetupGate64(&Bs3GdteSpare00 + iTest * 2, iTest /*bType*/, 3 /*bDpl*/,
                              BS3_SEL_R0_CS64, BS3_FP_OFF(bs3CpuBasic2_ud2) + BS3_ADDR_BS3TEXT16);
    else
    {
        for (iTest = 0; iTest < 16; iTest++)
        {
            Bs3SelSetupGate(&Bs3GdteSpare00 + iTest,      iTest /*bType*/, 3 /*bDpl*/,
                            BS3_SEL_R0_CS16, BS3_FP_OFF(bs3CpuBasic2_ud2), 0);
            Bs3SelSetupGate(&Bs3GdteSpare00 + iTest + 16, iTest /*bType*/, 3 /*bDpl*/,
                            BS3_SEL_R0_CS32, BS3_FP_OFF(bs3CpuBasic2_ud2) + BS3_ADDR_BS3TEXT16, 0);
        }
    }

    /*
     * Create a context.
     *
     * ASSUMES we're in on the ring-0 stack in ring-0 and using less than 16KB.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 1728);
    Ctx.rsp.u = BS3_ADDR_STACK - _16K;
    Bs3MemCpy(&CtxExpected, &Ctx, sizeof(CtxExpected));

    StkPtr.pv = Bs3RegCtxGetRspSsAsCurPtr(&Ctx);
    //Bs3TestPrintf("Stack=%p rsp=%RX64\n", StkPtr.pv, Ctx.rsp.u);

    /*
     * 16-bit tests.
     */
    if (BS3_MODE_IS_16BIT_CODE(bMode))
    {
        static struct
        {
            bool        fOpSizePfx;
            uint16_t    cbImm;
            FPFNBS3FAR  pfnTest;
        } const s_aTests[] =
        {
            { false,  0, bs3CpuBasic2_retf_c16, },
            {  true,  0, bs3CpuBasic2_retf_opsize_c16, },
            { false, 32, bs3CpuBasic2_retf_i32_c16, },
            {  true, 32, bs3CpuBasic2_retf_i32_opsize_c16, },
            { false,888, bs3CpuBasic2_retf_i888_c16, },
        };

        static struct
        {
            bool        fRmOrV86;
            bool        fInterPriv;
            int8_t      iXcpt;
            RTSEL       uStartSs;
            uint8_t     cDstBits;
            RTSEL       uDstCs;
            union   /* must use a union here as the compiler won't compile if uint16_t and will mess up fixups for uint32_t. */
            {
                uint32_t     offDst;
                struct
                {
                    NPVOID   pv;
                    uint16_t uHigh;
                } s;
            };
            RTSEL       uDstSs;
            uint16_t    uErrCd;
        } const s_aSubTests[] =
        { /* rm/v86, PriChg, Xcpt,  uStartSs,    => bits    uDstCs                   offDst/pv                                                    uDstSs               uErrCd */
            {  true, false, -1,                   0, 16, BS3_SEL_TEXT16,          { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          0,                   0 },
            { false, false, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_TEXT16  | 0,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R0_SS16 | 0, 0 },
            { false, false, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R0_CS16 | 0,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R0_SS16 | 0, 0 },
            { false, false, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16 | 0,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R0_SS16 | 0, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R1_CS16 | 1,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R1_SS16 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16 | 1,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R1_SS16 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R1_CS16 | 1,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R1_SS32 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16 | 1,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R1_SS32 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R2_CS16 | 2,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R2_CS16 | 2,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R2_CS16 | 2,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R2_SS32 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R2_CS16 | 2,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R2_SS32 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16 | 3,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R3_SS16 | 3, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16 | 3,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R3_SS16 | 3, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16 | 3,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R3_SS32 | 3, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16 | 3,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R3_SS32 | 3, 0 },
            /* conforming stuff */
            { false, false, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R0_CS16_CNF | 0, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R0_SS16 | 0, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R0_CS16_CNF | 1, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R1_SS16 | 1, 0 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R0_CS16_CNF | 1, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R0_SS16 | 1, BS3_SEL_R0_SS16 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R0_CS16_CNF | 2, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R0_CS16_CNF | 3, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R3_SS16 | 3, 0 },
            { false, false, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R1_CS16_CNF | 0, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R0_SS16 | 0, BS3_SEL_R1_CS16_CNF },
            { false, false, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R1_CS16_CNF | 0, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R1_SS16 | 1, BS3_SEL_R1_CS16_CNF },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R1_CS16_CNF | 1, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R1_SS16 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R1_CS16_CNF | 2, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R1_CS16_CNF | 3, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R3_SS16 | 3, 0 },
            { false, false, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R2_CS16_CNF | 0, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R0_SS16 | 0, BS3_SEL_R2_CS16_CNF },
            { false, false, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R2_CS16_CNF | 0, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R1_SS16 | 1, BS3_SEL_R2_CS16_CNF },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R2_CS16_CNF | 1, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R1_SS16 | 1, BS3_SEL_R2_CS16_CNF },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R2_CS16_CNF | 1, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R0_SS16 | 0, BS3_SEL_R2_CS16_CNF },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R2_CS16_CNF | 2, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R2_CS16_CNF | 3, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R3_SS16 | 3, 0 },
            { false, false, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16_CNF | 0, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R0_SS16 | 0, BS3_SEL_R3_CS16_CNF },
            { false, false, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16_CNF | 0, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R1_SS16 | 1, BS3_SEL_R3_CS16_CNF },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16_CNF | 1, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R1_SS16 | 1, BS3_SEL_R3_CS16_CNF },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16_CNF | 1, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R0_SS16 | 0, BS3_SEL_R3_CS16_CNF },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16_CNF | 2, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R2_SS16 | 2, BS3_SEL_R3_CS16_CNF },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16_CNF | 2, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R3_SS16 | 2, BS3_SEL_R3_CS16_CNF },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16_CNF | 3, { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R3_SS16 | 3, 0 },
            /* returning to 32-bit code: */
            { false, false, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R0_CS32 | 0,     { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R0_SS16 | 0, 0 },
            { false, false, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R0_CS32 | 0,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },        BS3_SEL_R0_SS16 | 0, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R1_SS16 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },        BS3_SEL_R1_SS16 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R1_SS32 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },        BS3_SEL_R1_SS32 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },        BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R2_SS32 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },        BS3_SEL_R2_SS32 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R3_SS16 | 3, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },        BS3_SEL_R3_SS16 | 3, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R3_SS32 | 3, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },        BS3_SEL_R3_SS32 | 3, 0 },
            { false, false, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32 | 0,     { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R0_SS32 | 0, 0 },
            { false, false, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32 | 0,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },        BS3_SEL_R0_SS32 | 0, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R1_SS32 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },        BS3_SEL_R1_SS32 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R1_SS16 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },        BS3_SEL_R1_SS16 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R2_SS32 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },        BS3_SEL_R2_SS32 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },        BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R3_SS32 | 3, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },        BS3_SEL_R3_SS32 | 3, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R3_SS16 | 3, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },        BS3_SEL_R3_SS16 | 3, 0 },
            /* returning to 32-bit conforming code: */
            { false, false, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R0_CS32_CNF | 0, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R0_SS16 | 0, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R0_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R1_SS16 | 1, 0 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R0_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R0_SS16 | 1, BS3_SEL_R0_SS16 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R0_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R0_SS16 | 0, BS3_SEL_R0_SS16 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R0_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R3_SS16 | 1, BS3_SEL_R3_SS16 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R0_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R3_SS16 | 3, BS3_SEL_R3_SS16 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R0_CS32_CNF | 2, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R0_CS32_CNF | 3, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R3_SS16 | 3, 0 },
            { false, false, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32_CNF | 0, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R0_SS16 | 0, BS3_SEL_R1_CS32_CNF },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R1_SS16 | 1, 0 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R0_SS16 | 1, BS3_SEL_R0_SS16 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R0_SS16 | 0, BS3_SEL_R0_SS16 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R3_SS16 | 1, BS3_SEL_R3_SS16 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R3_SS16 | 3, BS3_SEL_R3_SS16 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32_CNF | 2, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32_CNF | 3, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R3_SS16 | 3, 0 },
            { false, false, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32_CNF | 0, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R0_SS16 | 0, BS3_SEL_R2_CS32_CNF },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R1_SS16 | 1, BS3_SEL_R2_CS32_CNF },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32_CNF | 2, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32_CNF | 3, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R3_SS16 | 3, 0 },
            { false, false, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32_CNF | 0, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R0_SS16 | 0, BS3_SEL_R3_CS32_CNF },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R1_SS16 | 1, BS3_SEL_R3_CS32_CNF },
            { false,  true, 42, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32_CNF | 2, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R2_SS16 | 2, BS3_SEL_R3_CS32_CNF },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32_CNF | 3, { .offDst = LOW_UD_ADDR },                                      BS3_SEL_R3_SS16 | 3, 0 },
            /* returning to 64-bit code or 16-bit when not in long mode: */
            { false, false, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R0_CS64 | 0,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R0_SS16 | 0, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R1_SS16 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R2_CS64 | 2,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R3_SS16 | 3, 0 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R0_DS64 | 1, BS3_SEL_R0_DS64 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R1_DS64 | 1, 0 },
            { false, false, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R0_CS64 | 0,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R0_SS32 | 0, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R1_SS16 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R1_SS32 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64 | 2,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64 | 2,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R2_SS32 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R3_SS16 | 3, 0 },
            { false,  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R3_SS32 | 3, 0 },
            { false,  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R3_SS32 | 3, BS3_SEL_R2_CS64 },
            { false,  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R1_SS32 | 3, BS3_SEL_R2_CS64 },
            { false,  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R1_SS32 | 3, BS3_SEL_R1_SS32 },
            { false,  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R3_SS32 | 2, BS3_SEL_R3_SS32 },
            /* returning to 64-bit code or 16-bit when not in long mode, conforming code variant: */
            { false, false, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R0_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R0_SS16 | 0, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R0_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R1_SS16 | 1, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R0_CS64_CNF | 2, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R0_CS64_CNF | 3, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R3_SS16 | 3, 0 },

            { false, false, 14, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R0_SS16 | 0, BS3_SEL_R1_CS64_CNF },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R1_SS16 | 1, 0 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R1_SS16 | 2, BS3_SEL_R1_SS16 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R2_SS16 | 1, BS3_SEL_R2_SS16 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R2_SS16 | 2, BS3_SEL_R2_SS16 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64_CNF | 2, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64_CNF | 3, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R3_SS16 | 3, 0 },

            { false, false, 14, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R2_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R0_SS16 | 0, BS3_SEL_R2_CS64_CNF },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R2_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R1_SS16 | 1, BS3_SEL_R2_CS64_CNF },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R2_CS64_CNF | 2, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R2_SS16 | 2, 0 },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R2_CS64_CNF | 3, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R3_SS16 | 3, 0 },

            { false, false, 14, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R3_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R0_SS16 | 0, BS3_SEL_R3_CS64_CNF },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R3_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R1_SS16 | 1, BS3_SEL_R3_CS64_CNF },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R3_CS64_CNF | 2, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R2_SS16 | 2, BS3_SEL_R3_CS64_CNF },
            { false,  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R3_CS64_CNF | 3, { .offDst = LOW_SALC_UD_ADDR },                                 BS3_SEL_R3_SS16 | 3, 0 },

            /* some additional #GP variations */ /** @todo test all possible exceptions! */
            { false,  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16 | 2,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                          BS3_SEL_R2_SS16 | 2, BS3_SEL_R3_CS16 },
            { false,  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_TSS32_DF | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS32 | 0, BS3_SEL_TSS32_DF },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_00 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_00 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_01 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_01 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_02 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_02 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_03 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_03 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_04 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_04 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_05 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_05 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_06 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_06 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_07 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_07 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_08 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_08 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_09 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_09 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_0a | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_0a },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_0b | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_0b },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_0c | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_0c },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_0d | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_0d },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_0e | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_0e },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_SPARE_0f | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_0f },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_10 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_10 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_11 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_11 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_12 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_12 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_13 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_13 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_14 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_14 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_15 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_15 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_16 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_16 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_17 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_17 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_18 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_18 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_19 | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_19 },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_1a | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_1a },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_1b | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_1b },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_1c | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_1c },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_1d | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_1d },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_1e | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_1e },
            { false,  true, 14, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_SPARE_1f | 0,    { .offDst = 0 },                                                BS3_SEL_R0_SS16 | 0, BS3_SEL_SPARE_1f },
        };

        bool const         fRmOrV86     = BS3_MODE_IS_RM_OR_V86(bMode);
        BS3CPUVENDOR const enmCpuVendor = Bs3GetCpuVendor();

        Bs3RegSetDr7(X86_DR7_INIT_VAL);
        for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
        {
            Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, s_aTests[iTest].pfnTest);

            for (iSubTest = 0; iSubTest < RT_ELEMENTS(s_aSubTests); iSubTest++)
            {
                g_usBs3TestStep = (iTest << 12) | (iSubTest << 4);
                if (   s_aSubTests[iSubTest].fRmOrV86 == fRmOrV86
                    && (s_aSubTests[iSubTest].offDst <= UINT16_MAX || s_aTests[iTest].fOpSizePfx))
                {
                    uint16_t const cbFrmDisp = s_aSubTests[iSubTest].fInterPriv ? iSubTest % 7 : 0;
                    uint16_t const cbStkItem = s_aTests[iTest].fOpSizePfx ? 4 : 2;
                    uint16_t const cbFrame   = (s_aSubTests[iSubTest].fInterPriv ? 4 : 2) * cbStkItem;
                    uint32_t const uFlatDst  = Bs3SelFar32ToFlat32(s_aSubTests[iSubTest].offDst, s_aSubTests[iSubTest].uDstCs)
                                             + (s_aSubTests[iSubTest].cDstBits == 64 && !BS3_MODE_IS_64BIT_SYS(bMode));
                    RTSEL    const uDstSs    = s_aSubTests[iSubTest].uDstSs;
                    uint64_t       uDstRspExpect, uDstRspPush;
                    uint16_t       cErrors;

                    Ctx.ss = s_aSubTests[iSubTest].uStartSs;
                    if (Ctx.ss != BS3_SEL_R0_SS32)
                        Ctx.rsp.u32 |= UINT32_C(0xfffe0000);
                    else
                        Ctx.rsp.u32 &= UINT16_MAX;
                    uDstRspExpect = uDstRspPush = Ctx.rsp.u + s_aTests[iTest].cbImm + cbFrame + cbFrmDisp;
                    if (s_aSubTests[iSubTest].fInterPriv)
                    {
                        if (s_aTests[iTest].fOpSizePfx)
                            uDstRspPush = (uDstRspPush & UINT16_MAX) | UINT32_C(0xacdc0000);
                        if (   uDstSs == (BS3_SEL_R1_SS32 | 1)
                            || uDstSs == (BS3_SEL_R2_SS32 | 2)
                            || uDstSs == (BS3_SEL_R3_SS32 | 3)
                            || (s_aSubTests[iSubTest].cDstBits == 64 && BS3_MODE_IS_64BIT_SYS(bMode)))
                        {
                            if (s_aTests[iTest].fOpSizePfx)
                                uDstRspExpect = uDstRspPush;
                            else
                                uDstRspExpect &= UINT16_MAX;
                        }
                    }

                    CtxExpected.bCpl  = Ctx.bCpl;
                    CtxExpected.cs    = Ctx.cs;
                    CtxExpected.ss    = Ctx.ss;
                    CtxExpected.ds    = Ctx.ds;
                    CtxExpected.es    = Ctx.es;
                    CtxExpected.fs    = Ctx.fs;
                    CtxExpected.gs    = Ctx.gs;
                    CtxExpected.rip.u = Ctx.rip.u;
                    CtxExpected.rsp.u = Ctx.rsp.u;
                    CtxExpected.rax.u = Ctx.rax.u;
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                    {
                        CtxExpected.cs    = s_aSubTests[iSubTest].uDstCs;
                        CtxExpected.rip.u = s_aSubTests[iSubTest].offDst;
                        if (s_aSubTests[iSubTest].cDstBits == 64 && !BS3_MODE_IS_64BIT_SYS(bMode))
                        {
                            CtxExpected.rip.u     += 1;
                            CtxExpected.rax.au8[0] = CtxExpected.rflags.u16 & X86_EFL_CF ? 0xff : 0;
                        }
                        CtxExpected.ss    = uDstSs;
                        CtxExpected.rsp.u = uDstRspExpect;
                        if (s_aSubTests[iSubTest].fInterPriv)
                        {
                            uint16_t BS3_FAR *puSel = &CtxExpected.ds; /* ASSUME member order! */
                            unsigned          cSels = 4;
                            CtxExpected.bCpl = CtxExpected.ss & X86_SEL_RPL;
                            while (cSels-- > 0)
                            {
                                uint16_t uSel = *puSel;
                                if (   (uSel & X86_SEL_MASK_OFF_RPL)
                                    && Bs3Gdt[uSel >> X86_SEL_SHIFT].Gen.u2Dpl < CtxExpected.bCpl
                                    &&    (Bs3Gdt[uSel >> X86_SEL_SHIFT].Gen.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF))
                                       != (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF))
                                    *puSel = 0;
                                puSel++;
                            }
                            CtxExpected.rsp.u += s_aTests[iTest].cbImm; /* arguments are dropped from both stacks. */
                        }
                    }
                    g_uBs3TrapEipHint = CtxExpected.rip.u32;
                    //Bs3TestPrintf("cs:rip=%04RX16:%04RX64 -> %04RX16:%04RX64\n", Ctx.cs, Ctx.rip.u, CtxExpected.cs, CtxExpected.rip.u);
                    //Bs3TestPrintf("ss:rsp=%04RX16:%04RX64 -> %04RX16:%04RX64 [pushed %#RX64]\n", Ctx.ss, Ctx.rsp.u, CtxExpected.ss, CtxExpected.rsp.u, uDstRspPush);
                    bs3CpuBasic2_retf_PrepStack(StkPtr, cbStkItem, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst,
                                                s_aSubTests[iSubTest].fInterPriv, s_aTests[iTest].cbImm,
                                                s_aSubTests[iSubTest].uDstSs, uDstRspPush);
                    //Bs3TestPrintf("%p: %04RX16 %04RX16 %04RX16 %04RX16\n", StkPtr.pu16, StkPtr.pu16[0], StkPtr.pu16[1], StkPtr.pu16[2], StkPtr.pu16[3]);
                    //Bs3TestPrintf("%.48Rhxd\n", StkPtr.pu16);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aSubTests[iSubTest].uErrCd);
                    g_usBs3TestStep++;  /* 1 */

                    /* Bad hw bp: Setup DR0-3 but use invalid length encodings (non-byte) */
                    //Bs3TestPrintf("hw bp: bad len\n");
                    Bs3RegSetDr0(uFlatDst);
                    Bs3RegSetDr1(uFlatDst);
                    Bs3RegSetDr2(uFlatDst);
                    Bs3RegSetDr3(uFlatDst);
                    Bs3RegSetDr6(X86_DR6_INIT_VAL);
                    Bs3RegSetDr7(X86_DR7_INIT_VAL
                                 | X86_DR7_RW(0, X86_DR7_RW_EO) | X86_DR7_LEN(1, X86_DR7_LEN_WORD)  | X86_DR7_L_G(1)
                                 | X86_DR7_RW(2, X86_DR7_RW_EO) | X86_DR7_LEN(2, X86_DR7_LEN_DWORD) | X86_DR7_L_G(2)
                                 | (  BS3_MODE_IS_64BIT_SYS(bMode)
                                    ? X86_DR7_RW(3, X86_DR7_RW_EO) | X86_DR7_LEN(3, X86_DR7_LEN_QWORD) | X86_DR7_L_G(3) : 0) );
                    bs3CpuBasic2_retf_PrepStack(StkPtr, cbStkItem, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst,
                                                s_aSubTests[iSubTest].fInterPriv, s_aTests[iTest].cbImm,
                                                s_aSubTests[iSubTest].uDstSs, uDstRspPush);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    Bs3RegSetDr7(X86_DR7_INIT_VAL);
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aSubTests[iSubTest].uErrCd);
                    bs3CpuBasic2_CheckDr6InitVal();
                    g_usBs3TestStep++; /* 2 */

                    /* Bad hw bp: setup DR0-3 but don't enable them */
                    //Bs3TestPrintf("hw bp: disabled\n");
                    //Bs3RegSetDr0(uFlatDst);
                    //Bs3RegSetDr1(uFlatDst);
                    //Bs3RegSetDr2(uFlatDst);
                    //Bs3RegSetDr3(uFlatDst);
                    Bs3RegSetDr6(X86_DR6_INIT_VAL);
                    Bs3RegSetDr7(X86_DR7_INIT_VAL);
                    bs3CpuBasic2_retf_PrepStack(StkPtr, cbStkItem, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst,
                                                s_aSubTests[iSubTest].fInterPriv, s_aTests[iTest].cbImm,
                                                s_aSubTests[iSubTest].uDstSs, uDstRspPush);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    Bs3RegSetDr7(X86_DR7_INIT_VAL);
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aSubTests[iSubTest].uErrCd);
                    bs3CpuBasic2_CheckDr6InitVal();
                    g_usBs3TestStep++; /* 3 */

                    /* Bad hw bp: Points at 2nd byte in the UD2.  Docs says it only works when pointing at first byte. */
                    //Bs3TestPrintf("hw bp: byte 2\n");
                    Bs3RegSetDr0(uFlatDst + 1);
                    Bs3RegSetDr1(uFlatDst + 1);
                    //Bs3RegSetDr2(uFlatDst);
                    //Bs3RegSetDr3(uFlatDst);
                    Bs3RegSetDr6(X86_DR6_INIT_VAL);
                    Bs3RegSetDr7(X86_DR7_INIT_VAL
                                 | X86_DR7_RW(0, X86_DR7_RW_EO) | X86_DR7_LEN(0, X86_DR7_LEN_BYTE) | X86_DR7_L_G(0)
                                 | X86_DR7_RW(1, X86_DR7_RW_EO) | X86_DR7_LEN(1, X86_DR7_LEN_BYTE) | X86_DR7_L_G(1));
                    bs3CpuBasic2_retf_PrepStack(StkPtr, cbStkItem, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst,
                                                s_aSubTests[iSubTest].fInterPriv, s_aTests[iTest].cbImm,
                                                s_aSubTests[iSubTest].uDstSs, uDstRspPush);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    Bs3RegSetDr7(X86_DR7_INIT_VAL);
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aSubTests[iSubTest].uErrCd);
                    bs3CpuBasic2_CheckDr6InitVal();
                    g_usBs3TestStep++; /* 4 */

                    /* Again with two correctly hardware breakpoints and a disabled one that just matches the address: */
                    //Bs3TestPrintf("bp 1 + 3...\n");
                    Bs3RegSetDr0(uFlatDst);
                    Bs3RegSetDr1(uFlatDst);
                    Bs3RegSetDr2(0);
                    Bs3RegSetDr3(uFlatDst);
                    Bs3RegSetDr6(X86_DR6_INIT_VAL);
                    Bs3RegSetDr7(X86_DR7_INIT_VAL
                                 | X86_DR7_RW(1, X86_DR7_RW_EO) | X86_DR7_LEN(1, X86_DR7_LEN_BYTE) | X86_DR7_L_G(1)
                                 | X86_DR7_RW(3, X86_DR7_RW_EO) | X86_DR7_LEN(3, X86_DR7_LEN_BYTE) | X86_DR7_L_G(3) );
                    bs3CpuBasic2_retf_PrepStack(StkPtr, cbStkItem, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst,
                                                s_aSubTests[iSubTest].fInterPriv, s_aTests[iTest].cbImm,
                                                s_aSubTests[iSubTest].uDstSs, uDstRspPush);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    Bs3RegSetDr7(X86_DR7_INIT_VAL);
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                        bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected,
                                                  enmCpuVendor == BS3CPUVENDOR_AMD ? X86_DR6_B1 | X86_DR6_B3 /* 3990x */
                                                  : X86_DR6_B0 | X86_DR6_B1 | X86_DR6_B3);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aSubTests[iSubTest].uErrCd);
                    g_usBs3TestStep++; /* 5 */

                    /* Again with a single locally enabled breakpoint. */
                    //Bs3TestPrintf("bp 0/l...\n");
                    Bs3RegSetDr0(uFlatDst);
                    Bs3RegSetDr1(0);
                    Bs3RegSetDr2(0);
                    Bs3RegSetDr3(0);
                    Bs3RegSetDr6(X86_DR6_INIT_VAL | X86_DR6_B1 | X86_DR6_B2 | X86_DR6_B3 | X86_DR6_BS);
                    Bs3RegSetDr7(X86_DR7_INIT_VAL
                                 | X86_DR7_RW(0, X86_DR7_RW_EO) | X86_DR7_LEN(0, X86_DR7_LEN_BYTE) | X86_DR7_L(0));
                    bs3CpuBasic2_retf_PrepStack(StkPtr, cbStkItem, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst,
                                                s_aSubTests[iSubTest].fInterPriv, s_aTests[iTest].cbImm,
                                                s_aSubTests[iSubTest].uDstSs, uDstRspPush);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    Bs3RegSetDr7(X86_DR7_INIT_VAL);
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                        bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected, X86_DR6_B0 | X86_DR6_BS); /* B0-B3 set, BS preserved */
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aSubTests[iSubTest].uErrCd);
                    g_usBs3TestStep++; /* 6 */

                    /* Again with a single globally enabled breakpoint and serveral other types of breakpoints
                       configured but not enabled. */
                    //Bs3TestPrintf("bp 2/g+...\n");
                    cErrors = Bs3TestSubErrorCount();
                    Bs3RegSetDr0(uFlatDst);
                    Bs3RegSetDr1(uFlatDst);
                    Bs3RegSetDr2(uFlatDst);
                    Bs3RegSetDr3(uFlatDst);
                    Bs3RegSetDr6(X86_DR6_INIT_VAL | X86_DR6_BS | X86_DR6_BD | X86_DR6_BT | X86_DR6_B2);
                    Bs3RegSetDr7(X86_DR7_INIT_VAL
                                 | X86_DR7_RW(0, X86_DR7_RW_RW) | X86_DR7_LEN(0, X86_DR7_LEN_BYTE)
                                 | X86_DR7_RW(1, X86_DR7_RW_RW) | X86_DR7_LEN(1, X86_DR7_LEN_BYTE) | X86_DR7_L_G(1)
                                 | X86_DR7_RW(2, X86_DR7_RW_EO) | X86_DR7_LEN(2, X86_DR7_LEN_BYTE) | X86_DR7_G(2)
                                 | X86_DR7_RW(3, X86_DR7_RW_WO) | X86_DR7_LEN(3, X86_DR7_LEN_BYTE) | X86_DR7_G(3)
                                 );
                    bs3CpuBasic2_retf_PrepStack(StkPtr, cbStkItem, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst,
                                                s_aSubTests[iSubTest].fInterPriv, s_aTests[iTest].cbImm,
                                                s_aSubTests[iSubTest].uDstSs, uDstRspPush);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    Bs3RegSetDr7(X86_DR7_INIT_VAL);
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                        bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected, X86_DR6_B2 | X86_DR6_BS | X86_DR6_BD | X86_DR6_BT);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aSubTests[iSubTest].uErrCd);
                    g_usBs3TestStep++; /* 7 */

                    /* Now resume it with lots of execution breakpoints configured. */
                    if (s_aSubTests[iSubTest].iXcpt < 0 && Bs3TestSubErrorCount() == cErrors)
                    {
                        Bs3MemCpy(&Ctx2, &TrapCtx.Ctx, sizeof(Ctx2));
                        Ctx2.rflags.u32 |= X86_EFL_RF;
                        //Bs3TestPrintf("bp 3/g+rf %04RX16:%04RX64 efl=%RX32 ds=%04RX16...\n", Ctx2.cs, Ctx2.rip.u, Ctx2.rflags.u32, Ctx2.ds);
                        Bs3RegSetDr6(X86_DR6_INIT_VAL);
                        Bs3RegSetDr7(X86_DR7_INIT_VAL
                                     | X86_DR7_RW(0, X86_DR7_RW_EO) | X86_DR7_LEN(0, X86_DR7_LEN_BYTE)
                                     | X86_DR7_RW(1, X86_DR7_RW_EO) | X86_DR7_LEN(1, X86_DR7_LEN_BYTE) | X86_DR7_L_G(1)
                                     | X86_DR7_RW(2, X86_DR7_RW_EO) | X86_DR7_LEN(2, X86_DR7_LEN_BYTE) | X86_DR7_G(2)
                                     | X86_DR7_RW(3, X86_DR7_RW_EO) | X86_DR7_LEN(3, X86_DR7_LEN_BYTE) | X86_DR7_G(3)
                                     );
                        Bs3TrapSetJmpAndRestore(&Ctx2, &TrapCtx);
                        Bs3RegSetDr7(X86_DR7_INIT_VAL);
                        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
                        bs3CpuBasic2_CheckDr6InitVal();
                    }
                    g_usBs3TestStep++; /* 8 */

                    /* Now do single stepping: */
                    //Bs3TestPrintf("stepping...\n");
                    Bs3RegSetDr6(X86_DR6_INIT_VAL);
                    Ctx.rflags.u16        |= X86_EFL_TF;
                    CtxExpected.rflags.u16 = Ctx.rflags.u16;
                    if (s_aSubTests[iSubTest].iXcpt < 0 && s_aSubTests[iSubTest].cDstBits == 64 && !BS3_MODE_IS_64BIT_SYS(bMode))
                    {
                        CtxExpected.rip.u -= 1;
                        CtxExpected.rax.u  = Ctx.rax.u;
                    }
                    bs3CpuBasic2_retf_PrepStack(StkPtr, cbStkItem, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst,
                                                s_aSubTests[iSubTest].fInterPriv, s_aTests[iTest].cbImm,
                                                s_aSubTests[iSubTest].uDstSs, uDstRspPush);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                        bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected, X86_DR6_BS);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aSubTests[iSubTest].uErrCd);
                    Ctx.rflags.u16        &= ~X86_EFL_TF;
                    CtxExpected.rflags.u16 = Ctx.rflags.u16;
                    g_usBs3TestStep++; /* 9 */

                    /* Single step with B0-B3 set to check that they're not preserved
                       and with BD & BT to check that they are (checked on Intel 6700K): */
                    //Bs3TestPrintf("stepping b0-b3+bd+bt=1...\n");
                    Bs3RegSetDr6(X86_DR6_INIT_VAL | X86_DR6_B_MASK | X86_DR6_BD | X86_DR6_BT);
                    Ctx.rflags.u16        |= X86_EFL_TF;
                    CtxExpected.rflags.u16 = Ctx.rflags.u16;
                    bs3CpuBasic2_retf_PrepStack(StkPtr, cbStkItem, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst,
                                                s_aSubTests[iSubTest].fInterPriv, s_aTests[iTest].cbImm,
                                                s_aSubTests[iSubTest].uDstSs, uDstRspPush);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                        bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected, X86_DR6_BS | X86_DR6_BD | X86_DR6_BT);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aSubTests[iSubTest].uErrCd);
                    Ctx.rflags.u16        &= ~X86_EFL_TF;
                    CtxExpected.rflags.u16 = Ctx.rflags.u16;
                    g_usBs3TestStep++; /* 10 */

                }
            }
        }
    }
    /*
     * 32-bit tests.
     */
    else if (BS3_MODE_IS_32BIT_CODE(bMode))
    {
        static struct
        {
            bool        fOpSizePfx;
            uint16_t    cbImm;
            FPFNBS3FAR  pfnTest;
        } const s_aTests[] =
        {
            { false,  0, bs3CpuBasic2_retf_c32, },
            {  true,  0, bs3CpuBasic2_retf_opsize_c32, },
            { false, 32, bs3CpuBasic2_retf_i32_c32, },
            {  true, 32, bs3CpuBasic2_retf_i32_opsize_c32, },
            { false,888, bs3CpuBasic2_retf_i888_c32, },
        };

        static struct
        {
            bool        fInterPriv;
            int8_t      iXcpt;
            RTSEL       uStartSs;
            uint8_t     cDstBits;
            RTSEL       uDstCs;
            union   /* must use a union here as the compiler won't compile if uint16_t and will mess up fixups for uint32_t. */
            {
                uint32_t     offDst;
                struct
                {
                    NPVOID   pv;
                    uint16_t uHigh;
                } s;
            };
            RTSEL       uDstSs;
            uint16_t    uErrCd;
        } const s_aSubTests[] =
        { /* PriChg, Xcpt,  uStartSs,     => bits    uDstCs                   offDst/pv                                                 uDstSs               uErrCd */
            { false, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32 | 0,     { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R0_SS32 | 0, 0 },
            { false, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R0_CS32 | 0,     { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R0_SS32 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R3_SS32 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R3_SS32 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R3_SS16 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R3_SS16 | 3, 0 },
            /* same with 32-bit wide target addresses: */
            { false, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32 | 0,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },     BS3_SEL_R0_SS32 | 0, 0 },
            { false, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R0_CS32 | 0,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },     BS3_SEL_R0_SS32 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },     BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },     BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },     BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },     BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },     BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },     BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },     BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },     BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },     BS3_SEL_R3_SS32 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },     BS3_SEL_R3_SS32 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },     BS3_SEL_R3_SS16 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .s = {(NPVOID)bs3CpuBasic2_ud2, BS3TEXT16_ADDR_HI } },     BS3_SEL_R3_SS16 | 3, 0 },
            /* conforming stuff */
            { false, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32_CNF | 0, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R0_SS32 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R1_SS32 | 1, 0 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R0_SS32 | 1, BS3_SEL_R0_SS32 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32_CNF | 2, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32_CNF | 3, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R3_SS32 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32_CNF | 0, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R0_SS32 | 0, BS3_SEL_R1_CS32_CNF },
            { false, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32_CNF | 0, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R1_SS32 | 1, BS3_SEL_R1_CS32_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32_CNF | 2, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32_CNF | 3, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R3_SS32 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32_CNF | 0, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R0_SS32 | 0, BS3_SEL_R2_CS32_CNF },
            { false, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32_CNF | 0, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R1_SS32 | 1, BS3_SEL_R2_CS32_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R1_SS32 | 1, BS3_SEL_R2_CS32_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R0_SS32 | 0, BS3_SEL_R2_CS32_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32_CNF | 2, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32_CNF | 3, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R3_SS32 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32_CNF | 0, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R0_SS32 | 0, BS3_SEL_R3_CS32_CNF },
            { false, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32_CNF | 0, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R1_SS32 | 1, BS3_SEL_R3_CS32_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R1_SS32 | 1, BS3_SEL_R3_CS32_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32_CNF | 1, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R0_SS32 | 0, BS3_SEL_R3_CS32_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32_CNF | 2, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R2_SS32 | 2, BS3_SEL_R3_CS32_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32_CNF | 2, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R3_SS32 | 2, BS3_SEL_R3_CS32_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32_CNF | 3, { .offDst = LOW_UD_ADDR },                                   BS3_SEL_R3_SS32 | 3, 0 },
            /* returning to 16-bit code: */
            { false, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16 | 0,     { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R0_SS32 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16 | 1,     { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16 | 1,     { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R2_CS16 | 2,     { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R2_CS16 | 2,     { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16 | 3,     { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R3_SS32 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16 | 3,     { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R3_SS16 | 3, 0 },
            { false, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R0_CS16 | 0,     { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R0_SS16 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R1_CS16 | 1,     { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R1_CS16 | 1,     { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R2_CS16 | 2,     { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R2_CS16 | 2,     { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16 | 3,     { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R3_SS16 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16 | 3,     { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R3_SS32 | 3, 0 },
            /* returning to 16-bit conforming code: */
            { false, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 0, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R0_SS32 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R1_SS32 | 1, 0 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R0_SS32 | 1, BS3_SEL_R0_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R0_SS32 | 0, BS3_SEL_R0_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R3_SS32 | 1, BS3_SEL_R3_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R3_SS32 | 3, BS3_SEL_R3_SS32 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 2, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 3, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R3_SS32 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 0, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R0_SS32 | 0, BS3_SEL_R1_CS16_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R1_SS16 | 1, 0 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R0_SS32 | 1, BS3_SEL_R0_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R0_SS32 | 0, BS3_SEL_R0_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R3_SS32 | 1, BS3_SEL_R3_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R3_SS32 | 3, BS3_SEL_R3_SS32 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 2, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 3, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R3_SS32 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R2_CS16_CNF | 0, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R0_SS32 | 0, BS3_SEL_R2_CS16_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R2_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R1_SS32 | 1, BS3_SEL_R2_CS16_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R2_CS16_CNF | 2, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R2_CS16_CNF | 3, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R3_SS16 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16_CNF | 0, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R0_SS32 | 0, BS3_SEL_R3_CS16_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R1_SS32 | 1, BS3_SEL_R3_CS16_CNF },
            {  true, 42, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16_CNF | 2, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R2_SS32 | 2, BS3_SEL_R3_CS16_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16_CNF | 3, { .s = {(NPVOID)bs3CpuBasic2_ud2, 0 } },                     BS3_SEL_R3_SS32 | 3, 0 },
            /* returning to 64-bit code or 16-bit when not in long mode: */
            { false, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R0_CS64 | 0,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R0_SS16 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64 | 2,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R3_SS16 | 3, 0 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R0_DS64 | 1, BS3_SEL_R0_DS64 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R1_DS64 | 1, 0 },
            { false, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R0_CS64 | 0,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R0_SS32 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R2_CS64 | 2,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R2_CS64 | 2,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R3_SS16 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R3_SS32 | 3, 0 },
            {  true, 14, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R2_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R3_SS32 | 3, BS3_SEL_R2_CS64 },
            {  true, 14, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R2_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R1_SS32 | 3, BS3_SEL_R2_CS64 },
            {  true, 14, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R1_SS32 | 3, BS3_SEL_R1_SS32 },
            {  true, 14, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R3_SS32 | 2, BS3_SEL_R3_SS32 },
            /* returning to 64-bit code or 16-bit when not in long mode, conforming code variant: */
            { false, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R0_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R0_SS16 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R0_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R0_CS64_CNF | 2, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R0_CS64_CNF | 3, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R3_SS16 | 3, 0 },

            { false, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R0_SS16 | 0, BS3_SEL_R1_CS64_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R1_SS16 | 1, 0 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R1_SS16 | 2, BS3_SEL_R1_SS16 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R2_SS16 | 1, BS3_SEL_R2_SS16 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R2_SS16 | 2, BS3_SEL_R2_SS16 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64_CNF | 2, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64_CNF | 3, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R3_SS16 | 3, 0 },

            { false, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R0_SS16 | 0, BS3_SEL_R2_CS64_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R1_SS16 | 1, BS3_SEL_R2_CS64_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64_CNF | 2, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64_CNF | 3, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R3_SS16 | 3, 0 },

            { false, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R0_SS16 | 0, BS3_SEL_R3_CS64_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R1_SS16 | 1, BS3_SEL_R3_CS64_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64_CNF | 2, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R2_SS16 | 2, BS3_SEL_R3_CS64_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64_CNF | 3, { .offDst = LOW_SALC_UD_ADDR },                              BS3_SEL_R3_SS16 | 3, 0 },

            /* some additional #GP variations */ /** @todo test all possible exceptions! */
            {  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16 | 2,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                       BS3_SEL_R2_SS16 | 2, BS3_SEL_R3_CS16 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_00 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_00 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_01 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_01 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_02 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_02 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_03 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_03 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_04 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_04 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_05 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_05 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_06 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_06 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_07 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_07 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_08 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_08 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_09 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_09 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_0a | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_0a },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_0b | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_0b },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_0c | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_0c },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_0d | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_0d },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_0e | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_0e },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_SPARE_0f | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_0f },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_10 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_10 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_11 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_11 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_12 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_12 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_13 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_13 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_14 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_14 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_15 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_15 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_16 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_16 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_17 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_17 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_18 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_18 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_19 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_19 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_1a | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_1a },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_1b | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_1b },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_1c | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_1c },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_1d | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_1d },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_1e | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_1e },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_SPARE_1f | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_1f },
        };

        for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
        {
            Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, s_aTests[iTest].pfnTest);
            //Bs3TestPrintf("-------------- #%u: cs:eip=%04RX16:%08RX64 imm=%u%s\n",
            //              iTest, Ctx.cs, Ctx.rip.u, s_aTests[iTest].cbImm, s_aTests[iTest].fOpSizePfx ? " o16" : "");

            for (iSubTest = 0; iSubTest < RT_ELEMENTS(s_aSubTests); iSubTest++)
            {
                g_usBs3TestStep = (iTest << 12) | (iSubTest << 1);
                if (!s_aTests[iTest].fOpSizePfx || s_aSubTests[iSubTest].offDst <= UINT16_MAX)
                {
                    uint16_t const cbFrmDisp = s_aSubTests[iSubTest].fInterPriv ? iSubTest % 7 : 0;
                    uint16_t const cbStkItem = s_aTests[iTest].fOpSizePfx ? 2 : 4;
                    uint16_t const cbFrame   = (s_aSubTests[iSubTest].fInterPriv ? 4 : 2) * cbStkItem;
                    RTSEL    const uDstSs    = s_aSubTests[iSubTest].uDstSs;
                    uint64_t       uDstRspExpect, uDstRspPush;
                    //Bs3TestPrintf(" #%u: %s %d %#04RX16 -> %u %#04RX16:%#04RX32 %#04RX16 %#RX16\n", iSubTest, s_aSubTests[iSubTest].fInterPriv ? "priv" : "same", s_aSubTests[iSubTest].iXcpt, s_aSubTests[iSubTest].uStartSs,
                    //              s_aSubTests[iSubTest].cDstBits, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst, s_aSubTests[iSubTest].uDstSs, s_aSubTests[iSubTest].uErrCd);

                    Ctx.ss = s_aSubTests[iSubTest].uStartSs;
                    if (Ctx.ss != BS3_SEL_R0_SS32)
                        Ctx.rsp.u32 |= UINT32_C(0xfffe0000);
                    else
                        Ctx.rsp.u32 &= UINT16_MAX;
                    uDstRspExpect = uDstRspPush = Ctx.rsp.u + s_aTests[iTest].cbImm + cbFrame + cbFrmDisp;
                    if (s_aSubTests[iSubTest].fInterPriv)
                    {
                        if (!s_aTests[iTest].fOpSizePfx)
                            uDstRspPush = (uDstRspPush & UINT16_MAX) | UINT32_C(0xacdc0000);
                        if (   uDstSs == (BS3_SEL_R1_SS32 | 1)
                            || uDstSs == (BS3_SEL_R2_SS32 | 2)
                            || uDstSs == (BS3_SEL_R3_SS32 | 3)
                            || (s_aSubTests[iSubTest].cDstBits == 64 && BS3_MODE_IS_64BIT_SYS(bMode)))
                        {
                            if (!s_aTests[iTest].fOpSizePfx)
                                uDstRspExpect = uDstRspPush;
                            else
                                uDstRspExpect &= UINT16_MAX;
                        }
                    }

                    CtxExpected.bCpl  = Ctx.bCpl;
                    CtxExpected.cs    = Ctx.cs;
                    CtxExpected.ss    = Ctx.ss;
                    CtxExpected.ds    = Ctx.ds;
                    CtxExpected.es    = Ctx.es;
                    CtxExpected.fs    = Ctx.fs;
                    CtxExpected.gs    = Ctx.gs;
                    CtxExpected.rip.u = Ctx.rip.u;
                    CtxExpected.rsp.u = Ctx.rsp.u;
                    CtxExpected.rax.u = Ctx.rax.u;
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                    {
                        CtxExpected.cs    = s_aSubTests[iSubTest].uDstCs;
                        CtxExpected.rip.u = s_aSubTests[iSubTest].offDst;
                        if (s_aSubTests[iSubTest].cDstBits == 64 && !BS3_MODE_IS_64BIT_SYS(bMode))
                        {
                            CtxExpected.rip.u     += 1;
                            CtxExpected.rax.au8[0] = CtxExpected.rflags.u16 & X86_EFL_CF ? 0xff : 0;
                        }
                        CtxExpected.ss    = uDstSs;
                        CtxExpected.rsp.u = uDstRspExpect;
                        if (s_aSubTests[iSubTest].fInterPriv)
                        {
                            uint16_t BS3_FAR *puSel = &CtxExpected.ds; /* ASSUME member order! */
                            unsigned          cSels = 4;
                            CtxExpected.bCpl = CtxExpected.ss & X86_SEL_RPL;
                            while (cSels-- > 0)
                            {
                                uint16_t uSel = *puSel;
                                if (   (uSel & X86_SEL_MASK_OFF_RPL)
                                    && Bs3Gdt[uSel >> X86_SEL_SHIFT].Gen.u2Dpl < CtxExpected.bCpl
                                    &&    (Bs3Gdt[uSel >> X86_SEL_SHIFT].Gen.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF))
                                       != (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF))
                                    *puSel = 0;
                                puSel++;
                            }
                            CtxExpected.rsp.u += s_aTests[iTest].cbImm; /* arguments are dropped from both stacks. */
                        }
                    }
                    g_uBs3TrapEipHint = CtxExpected.rip.u32;
                    //Bs3TestPrintf("ss:rsp=%04RX16:%04RX64 -> %04RX16:%04RX64 [pushed %#RX64]; %04RX16:%04RX64\n",Ctx.ss, Ctx.rsp.u,
                    //              CtxExpected.ss, CtxExpected.rsp.u, uDstRspPush, CtxExpected.cs, CtxExpected.rip.u);
                    bs3CpuBasic2_retf_PrepStack(StkPtr, cbStkItem, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst,
                                                s_aSubTests[iSubTest].fInterPriv, s_aTests[iTest].cbImm,
                                                s_aSubTests[iSubTest].uDstSs, uDstRspPush);
                    //Bs3TestPrintf("%p: %04RX16 %04RX16 %04RX16 %04RX16\n", StkPtr.pu16, StkPtr.pu16[0], StkPtr.pu16[1], StkPtr.pu16[2], StkPtr.pu16[3]);
                    //Bs3TestPrintf("%.48Rhxd\n", StkPtr.pu16);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aSubTests[iSubTest].uErrCd);
                    g_usBs3TestStep++;

                    /* Again single stepping: */
                    //Bs3TestPrintf("stepping...\n");
                    Bs3RegSetDr6(X86_DR6_INIT_VAL);
                    Ctx.rflags.u16        |= X86_EFL_TF;
                    CtxExpected.rflags.u16 = Ctx.rflags.u16;
                    if (s_aSubTests[iSubTest].iXcpt < 0 && s_aSubTests[iSubTest].cDstBits == 64 && !BS3_MODE_IS_64BIT_SYS(bMode))
                    {
                        CtxExpected.rip.u -= 1;
                        CtxExpected.rax.u  = Ctx.rax.u;
                    }
                    bs3CpuBasic2_retf_PrepStack(StkPtr, cbStkItem, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst,
                                                s_aSubTests[iSubTest].fInterPriv, s_aTests[iTest].cbImm,
                                                s_aSubTests[iSubTest].uDstSs, uDstRspPush);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                        bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected, X86_DR6_BS);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aSubTests[iSubTest].uErrCd);
                    Ctx.rflags.u16        &= ~X86_EFL_TF;
                    CtxExpected.rflags.u16 = Ctx.rflags.u16;
                    g_usBs3TestStep++;
                }
            }
        }
    }
    /*
     * 64-bit tests.
     */
    else if (BS3_MODE_IS_64BIT_CODE(bMode))
    {
        static struct
        {
            uint8_t     fOpSizePfx; /**< 0: none, 1: 066h, 2: REX.W;  Effective op size prefix. */
            uint16_t    cbImm;
            FPFNBS3FAR  pfnTest;
        } const s_aTests[] =
        {
            { 0,  0, bs3CpuBasic2_retf_c64, },
            { 1,  0, bs3CpuBasic2_retf_opsize_c64, },
            { 0, 32, bs3CpuBasic2_retf_i32_c64, },
            { 1, 32, bs3CpuBasic2_retf_i32_opsize_c64, },
            { 2,  0, bs3CpuBasic2_retf_rexw_c64, },
            { 2,  0, bs3CpuBasic2_retf_opsize_rexw_c64, },
            { 1,  0, bs3CpuBasic2_retf_rexw_opsize_c64, },
            { 2, 24, bs3CpuBasic2_retf_i24_rexw_c64, },
            { 2, 24, bs3CpuBasic2_retf_i24_opsize_rexw_c64, },
            { 1, 24, bs3CpuBasic2_retf_i24_rexw_opsize_c64, },
            { 0,888, bs3CpuBasic2_retf_i888_c64, },
        };

        static struct
        {
            bool        fInterPriv;
            int8_t      iXcpt;
            RTSEL       uStartSs;
            uint8_t     cDstBits;
            RTSEL       uDstCs;
            union   /* must use a union here as the compiler won't compile if uint16_t and will mess up fixups for uint32_t. */
            {
                uint32_t     offDst;
                struct
                {
                    NPVOID   pv;
                    uint16_t uHigh;
                } s;
            };
            RTSEL       uDstSs;
            uint16_t    uErrCd;
        } const s_aSubTests[] =
        { /* PriChg, Xcpt,  uStartSs,     => bits    uDstCs                   offDst/pv                                                     uDstSs               uErrCd */
            { false, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R0_CS64 | 0,     { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R0_SS32 | 0, 0 },
            { false, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R0_CS64 | 0,     { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R0_SS32 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64 | 2,     { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R2_CS64 | 2,     { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64 | 2,     { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R2_CS64 | 2,     { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R3_SS32 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R3_SS32 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R3_SS16 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R3_SS16 | 3, 0 },
            /* same with 32-bit wide target addresses: */
            { false, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R0_CS64 | 0,     { .s = {(NPVOID)bs3CpuBasic2_salc_ud2, BS3TEXT16_ADDR_HI } },    BS3_SEL_R0_SS32 | 0, 0 },
            { false, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R0_CS64 | 0,     { .s = {(NPVOID)bs3CpuBasic2_salc_ud2, BS3TEXT16_ADDR_HI } },    BS3_SEL_R0_SS32 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .s = {(NPVOID)bs3CpuBasic2_salc_ud2, BS3TEXT16_ADDR_HI } },    BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .s = {(NPVOID)bs3CpuBasic2_salc_ud2, BS3TEXT16_ADDR_HI } },    BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .s = {(NPVOID)bs3CpuBasic2_salc_ud2, BS3TEXT16_ADDR_HI } },    BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R1_CS64 | 1,     { .s = {(NPVOID)bs3CpuBasic2_salc_ud2, BS3TEXT16_ADDR_HI } },    BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64 | 2,     { .s = {(NPVOID)bs3CpuBasic2_salc_ud2, BS3TEXT16_ADDR_HI } },    BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R2_CS64 | 2,     { .s = {(NPVOID)bs3CpuBasic2_salc_ud2, BS3TEXT16_ADDR_HI } },    BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64 | 2,     { .s = {(NPVOID)bs3CpuBasic2_salc_ud2, BS3TEXT16_ADDR_HI } },    BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R2_CS64 | 2,     { .s = {(NPVOID)bs3CpuBasic2_salc_ud2, BS3TEXT16_ADDR_HI } },    BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .s = {(NPVOID)bs3CpuBasic2_salc_ud2, BS3TEXT16_ADDR_HI } },    BS3_SEL_R3_SS32 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .s = {(NPVOID)bs3CpuBasic2_salc_ud2, BS3TEXT16_ADDR_HI } },    BS3_SEL_R3_SS32 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .s = {(NPVOID)bs3CpuBasic2_salc_ud2, BS3TEXT16_ADDR_HI } },    BS3_SEL_R3_SS16 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 64, BS3_SEL_R3_CS64 | 3,     { .s = {(NPVOID)bs3CpuBasic2_salc_ud2, BS3TEXT16_ADDR_HI } },    BS3_SEL_R3_SS16 | 3, 0 },
            /* conforming stuff */
            { false, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R0_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R0_SS32 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R0_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R1_SS32 | 1, 0 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R0_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R0_SS32 | 1, BS3_SEL_R0_SS32 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R0_CS64_CNF | 2, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R0_CS64_CNF | 3, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R3_SS32 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R0_SS32 | 0, BS3_SEL_R1_CS64_CNF },
            { false, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R1_SS32 | 1, BS3_SEL_R1_CS64_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64_CNF | 2, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R1_CS64_CNF | 3, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R3_SS32 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R0_SS32 | 0, BS3_SEL_R2_CS64_CNF },
            { false, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R1_SS32 | 1, BS3_SEL_R2_CS64_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R1_SS32 | 1, BS3_SEL_R2_CS64_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R0_SS32 | 0, BS3_SEL_R2_CS64_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64_CNF | 2, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R2_CS64_CNF | 3, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R3_SS32 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R0_SS32 | 0, BS3_SEL_R3_CS64_CNF },
            { false, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64_CNF | 0, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R1_SS32 | 1, BS3_SEL_R3_CS64_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R1_SS32 | 1, BS3_SEL_R3_CS64_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64_CNF | 1, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R0_SS32 | 0, BS3_SEL_R3_CS64_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64_CNF | 2, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R2_SS32 | 2, BS3_SEL_R3_CS64_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64_CNF | 2, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R3_SS32 | 2, BS3_SEL_R3_CS64_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_R3_CS64_CNF | 3, { .offDst = LOW_SALC_UD_ADDR },                                  BS3_SEL_R3_SS32 | 3, 0 },
            /* returning to 16-bit code: */
            { false, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16 | 0,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R0_SS32 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16 | 1,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16 | 1,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R2_CS16 | 2,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R2_CS16 | 2,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16 | 3,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R3_SS32 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16 | 3,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R3_SS16 | 3, 0 },
            { false, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R0_CS16 | 0,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R0_SS16 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R1_CS16 | 1,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R1_CS16 | 1,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R2_CS16 | 2,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R2_CS16 | 2,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16 | 3,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R3_SS16 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16 | 3,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R3_SS32 | 3, 0 },
            /* returning to 16-bit conforming code: */
            { false, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 0, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R0_SS32 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R1_SS32 | 1, 0 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R0_SS32 | 1, BS3_SEL_R0_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R0_SS32 | 0, BS3_SEL_R0_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R3_SS32 | 1, BS3_SEL_R3_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R3_SS32 | 3, BS3_SEL_R3_SS32 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 2, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R0_CS16_CNF | 3, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R3_SS32 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 0, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R0_SS32 | 0, BS3_SEL_R1_CS16_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R1_SS16 | 1, 0 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R0_SS32 | 1, BS3_SEL_R0_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R0_SS32 | 0, BS3_SEL_R0_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R3_SS32 | 1, BS3_SEL_R3_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R3_SS32 | 3, BS3_SEL_R3_SS32 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 2, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R1_CS16_CNF | 3, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R3_SS32 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R2_CS16_CNF | 0, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R0_SS32 | 0, BS3_SEL_R2_CS16_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R2_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R1_SS32 | 1, BS3_SEL_R2_CS16_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R2_CS16_CNF | 2, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R2_CS16_CNF | 3, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R3_SS16 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16_CNF | 0, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R0_SS32 | 0, BS3_SEL_R3_CS16_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R1_SS32 | 1, BS3_SEL_R3_CS16_CNF },
            {  true, 42, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16_CNF | 2, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R2_SS32 | 2, BS3_SEL_R3_CS16_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 16, BS3_SEL_R3_CS16_CNF | 3, { .s = {(NPVOID)bs3CpuBasic2_swapgs, 0 } },                      BS3_SEL_R3_SS32 | 3, 0 },
            /* returning to 32-bit code - narrow 16-bit target address: */
            { false, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32 | 0,     { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R0_SS32 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R3_SS32 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R3_SS16 | 3, 0 },
            { false, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R0_CS32 | 0,     { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R0_SS16 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R3_SS16 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R3_SS32 | 3, 0 },
            /* returning to 32-bit code - wider 32-bit target address: */
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R3_SS32 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R3_SS16 | 3, 0 },
            { false, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R0_CS32 | 0,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R0_SS16 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R1_SS16 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R1_CS32 | 1,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R1_SS32 | 1, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R2_CS32 | 2,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R3_SS16 | 3, 0 },
            {  true, -1, BS3_SEL_R0_SS16 | 0, 32, BS3_SEL_R3_CS32 | 3,     { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R3_SS32 | 3, 0 },
            /* returning to 32-bit conforming code: */
            { false, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32_CNF | 0, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R0_SS32 | 0, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R1_SS32 | 1, 0 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R0_SS32 | 1, BS3_SEL_R0_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R0_SS32 | 0, BS3_SEL_R0_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R3_SS32 | 1, BS3_SEL_R3_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R3_SS32 | 3, BS3_SEL_R3_SS32 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32_CNF | 2, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R2_SS16 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R0_CS32_CNF | 3, { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R3_SS32 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32_CNF | 0, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R0_SS32 | 0, BS3_SEL_R1_CS32_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R1_SS16 | 1, 0 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R0_SS32 | 1, BS3_SEL_R0_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R0_SS32 | 0, BS3_SEL_R0_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R3_SS32 | 1, BS3_SEL_R3_SS32 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R3_SS32 | 3, BS3_SEL_R3_SS32 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32_CNF | 2, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R1_CS32_CNF | 3, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R3_SS32 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32_CNF | 0, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R0_SS32 | 0, BS3_SEL_R2_CS32_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R1_SS32 | 1, BS3_SEL_R2_CS32_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32_CNF | 2, { .offDst = LOW_SWAPGS_ADDR },                                   BS3_SEL_R2_SS32 | 2, 0 },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R2_CS32_CNF | 3, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R3_SS16 | 3, 0 },
            { false, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32_CNF | 0, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R0_SS32 | 0, BS3_SEL_R3_CS32_CNF },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32_CNF | 1, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R1_SS32 | 1, BS3_SEL_R3_CS32_CNF },
            {  true, 42, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32_CNF | 2, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R2_SS32 | 2, BS3_SEL_R3_CS32_CNF },
            {  true, -1, BS3_SEL_R0_SS32 | 0, 32, BS3_SEL_R3_CS32_CNF | 3, { .s = {(NPVOID)bs3CpuBasic2_swapgs, BS3TEXT16_ADDR_HI } },      BS3_SEL_R3_SS32 | 3, 0 },

            /* some additional #GP variations */ /** @todo test all possible exceptions! */
            {  true, 14, BS3_SEL_R0_SS16 | 0, 16, BS3_SEL_R3_CS16 | 2,     { .s = { (NPVOID)bs3CpuBasic2_ud2 } },                       BS3_SEL_R2_SS16 | 2, BS3_SEL_R3_CS16 },

            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_00 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_00 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_02 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_02 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_04 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_04 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_06 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_06 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_08 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_08 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_0a | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_0a },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_0c | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_0c },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_0e | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_0e },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_10 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_10 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_12 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_12 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_14 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_14 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_16 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_16 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_18 | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_18 },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_1a | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_1a },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_1c | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_1c },
            {  true, 14, BS3_SEL_R0_SS32 | 0, 64, BS3_SEL_SPARE_1e | 0,    { .offDst = 0 },                                             BS3_SEL_R0_SS32 | 0, BS3_SEL_SPARE_1e },
        };

        for (iTest = 0; iTest < RT_ELEMENTS(s_aTests); iTest++)
        {
            Bs3RegCtxSetRipCsFromLnkPtr(&Ctx, s_aTests[iTest].pfnTest);
            //Bs3TestPrintf("-------------- #%u: cs:eip=%04RX16:%08RX64 imm=%u%s\n", iTest, Ctx.cs, Ctx.rip.u, s_aTests[iTest].cbImm,
            //              s_aTests[iTest].fOpSizePfx == 1 ? " o16" : s_aTests[iTest].fOpSizePfx == 2 ? " o64" : "");

            for (iSubTest = 0; iSubTest < RT_ELEMENTS(s_aSubTests); iSubTest++)
            {
                g_usBs3TestStep = (iTest << 12) | (iSubTest << 1);
                if (s_aTests[iTest].fOpSizePfx != 1 || s_aSubTests[iSubTest].offDst <= UINT16_MAX)
                {
                    uint16_t const cbFrmDisp = s_aSubTests[iSubTest].fInterPriv ? iSubTest % 7 : 0;
                    uint16_t const cbStkItem = s_aTests[iTest].fOpSizePfx == 2 ? 8 : s_aTests[iTest].fOpSizePfx == 0 ? 4 : 2;
                    uint16_t const cbFrame   = (s_aSubTests[iSubTest].fInterPriv ? 4 : 2) * cbStkItem;
                    RTSEL    const uDstSs    = s_aSubTests[iSubTest].uDstSs;
                    uint64_t       uDstRspExpect, uDstRspPush;
                    //Bs3TestPrintf(" #%u: %s %d %#04RX16 -> %u %#04RX16:%#04RX32 %#04RX16 %#RX16\n", iSubTest, s_aSubTests[iSubTest].fInterPriv ? "priv" : "same", s_aSubTests[iSubTest].iXcpt, s_aSubTests[iSubTest].uStartSs,
                    //              s_aSubTests[iSubTest].cDstBits, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst, s_aSubTests[iSubTest].uDstSs, s_aSubTests[iSubTest].uErrCd);

                    Ctx.ss = s_aSubTests[iSubTest].uStartSs;
                    uDstRspExpect = uDstRspPush = Ctx.rsp.u + s_aTests[iTest].cbImm + cbFrame + cbFrmDisp;
                    if (s_aSubTests[iSubTest].fInterPriv)
                    {
                        if (s_aTests[iTest].fOpSizePfx != 1)
                        {
                            if (s_aTests[iTest].fOpSizePfx == 2)
                                uDstRspPush |= UINT64_C(0xf00dfaceacdc0000);
                            else
                                uDstRspPush |=         UINT32_C(0xacdc0000);
                            if (s_aSubTests[iSubTest].cDstBits == 64)
                                uDstRspExpect = uDstRspPush;
                            else if (!BS3_SEL_IS_SS16(uDstSs))
                                uDstRspExpect = (uint32_t)uDstRspPush;
                        }
                    }

                    CtxExpected.bCpl  = Ctx.bCpl;
                    CtxExpected.cs    = Ctx.cs;
                    CtxExpected.ss    = Ctx.ss;
                    CtxExpected.ds    = Ctx.ds;
                    CtxExpected.es    = Ctx.es;
                    CtxExpected.fs    = Ctx.fs;
                    CtxExpected.gs    = Ctx.gs;
                    CtxExpected.rip.u = Ctx.rip.u;
                    CtxExpected.rsp.u = Ctx.rsp.u;
                    CtxExpected.rax.u = Ctx.rax.u;
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                    {
                        CtxExpected.cs    = s_aSubTests[iSubTest].uDstCs;
                        CtxExpected.rip.u = s_aSubTests[iSubTest].offDst;
                        if (s_aSubTests[iSubTest].cDstBits == 64 && !BS3_MODE_IS_64BIT_SYS(bMode))
                        {
                            CtxExpected.rip.u     += 1;
                            CtxExpected.rax.au8[0] = CtxExpected.rflags.u16 & X86_EFL_CF ? 0xff : 0;
                        }
                        CtxExpected.ss    = uDstSs;
                        CtxExpected.rsp.u = uDstRspExpect;
                        if (s_aSubTests[iSubTest].fInterPriv)
                        {
                            uint16_t BS3_FAR *puSel = &CtxExpected.ds; /* ASSUME member order! */
                            unsigned          cSels = 4;
                            CtxExpected.bCpl = CtxExpected.ss & X86_SEL_RPL;
                            while (cSels-- > 0)
                            {
                                uint16_t uSel = *puSel;
                                if (   (uSel & X86_SEL_MASK_OFF_RPL)
                                    && Bs3Gdt[uSel >> X86_SEL_SHIFT].Gen.u2Dpl < CtxExpected.bCpl
                                    &&    (Bs3Gdt[uSel >> X86_SEL_SHIFT].Gen.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF))
                                       != (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF))
                                    *puSel = 0;
                                puSel++;
                            }
                            CtxExpected.rsp.u += s_aTests[iTest].cbImm; /* arguments are dropped from both stacks. */
                        }
                    }
                    g_uBs3TrapEipHint = CtxExpected.rip.u32;
                    //Bs3TestPrintf("ss:rsp=%04RX16:%04RX64 -> %04RX16:%04RX64 [pushed %#RX64]; %04RX16:%04RX64\n",Ctx.ss, Ctx.rsp.u,
                    //              CtxExpected.ss, CtxExpected.rsp.u, uDstRspPush, CtxExpected.cs, CtxExpected.rip.u);
                    bs3CpuBasic2_retf_PrepStack(StkPtr, cbStkItem, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst,
                                                s_aSubTests[iSubTest].fInterPriv, s_aTests[iTest].cbImm,
                                                s_aSubTests[iSubTest].uDstSs, uDstRspPush);
                    //Bs3TestPrintf("%p: %04RX16 %04RX16 %04RX16 %04RX16\n", StkPtr.pu16, StkPtr.pu16[0], StkPtr.pu16[1], StkPtr.pu16[2], StkPtr.pu16[3]);
                    //Bs3TestPrintf("%.48Rhxd\n", StkPtr.pu16);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                        bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aSubTests[iSubTest].uErrCd);
                    g_usBs3TestStep++;

                    /* Again single stepping: */
                    //Bs3TestPrintf("stepping...\n");
                    Bs3RegSetDr6(X86_DR6_INIT_VAL);
                    Ctx.rflags.u16        |= X86_EFL_TF;
                    CtxExpected.rflags.u16 = Ctx.rflags.u16;
                    if (s_aSubTests[iSubTest].iXcpt < 0 && s_aSubTests[iSubTest].cDstBits == 64 && !BS3_MODE_IS_64BIT_SYS(bMode))
                    {
                        CtxExpected.rip.u -= 1;
                        CtxExpected.rax.u  = Ctx.rax.u;
                    }
                    bs3CpuBasic2_retf_PrepStack(StkPtr, cbStkItem, s_aSubTests[iSubTest].uDstCs, s_aSubTests[iSubTest].offDst,
                                                s_aSubTests[iSubTest].fInterPriv, s_aTests[iTest].cbImm,
                                                s_aSubTests[iSubTest].uDstSs, uDstRspPush);
                    Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
                    if (s_aSubTests[iSubTest].iXcpt < 0)
                        bs3CpuBasic2_CompareDbCtx(&TrapCtx, &CtxExpected, X86_DR6_BS);
                    else
                        bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, s_aSubTests[iSubTest].uErrCd);
                    Ctx.rflags.u16        &= ~X86_EFL_TF;
                    CtxExpected.rflags.u16 = Ctx.rflags.u16;
                    g_usBs3TestStep++;
                }
            }
        }
    }
    else
        Bs3TestFailed("wtf?");

    if (BS3_MODE_IS_64BIT_SYS(bMode))
        Bs3TrapReInit();
    return 0;
}



/*********************************************************************************************************************************
*   Instruction Length                                                                                                           *
*********************************************************************************************************************************/


static uint8_t bs3CpuBasic2_instr_len_Worker(uint8_t bMode, uint8_t BS3_FAR *pbCodeBuf)
{
    BS3TRAPFRAME    TrapCtx;
    BS3REGCTX       Ctx;
    BS3REGCTX       CtxExpected;
    uint32_t        uEipBase;
    unsigned        cbInstr;
    unsigned        off;

    /* Make sure they're allocated and all zeroed. */
    Bs3MemZero(&Ctx, sizeof(Ctx));
    Bs3MemZero(&CtxExpected, sizeof(Ctx));
    Bs3MemZero(&TrapCtx, sizeof(TrapCtx));

    /*
     * Create a context.
     *
     * ASSUMES we're in on the ring-0 stack in ring-0 and using less than 16KB.
     */
    Bs3RegCtxSaveEx(&Ctx, bMode, 768);
    Bs3RegCtxSetRipCsFromCurPtr(&Ctx, (FPFNBS3FAR)pbCodeBuf);
    uEipBase = Ctx.rip.u32;

    Bs3MemCpy(&CtxExpected, &Ctx, sizeof(CtxExpected));

    /*
     * Simple stuff crossing the page.
     */
    for (off = X86_PAGE_SIZE - 32; off <= X86_PAGE_SIZE + 16; off++)
    {
        Ctx.rip.u32 = uEipBase + off;
        for (cbInstr = 0; cbInstr < 24; cbInstr++)
        {
            /*
             * Generate the instructions:
             *      [es] nop
             *      ud2
             */
            if (cbInstr > 0)
            {
                Bs3MemSet(&pbCodeBuf[off], 0x26 /* es */, cbInstr);
                pbCodeBuf[off + cbInstr - 1] = 0x90; /* nop */
            }
            pbCodeBuf[off + cbInstr + 0] = 0x0f; /* ud2 */
            pbCodeBuf[off + cbInstr + 1] = 0x0b;

            /*
             * Test it.
             */
            if (cbInstr < 16)
                CtxExpected.rip.u32 = Ctx.rip.u32 + cbInstr;
            else
                CtxExpected.rip.u32 = Ctx.rip.u32;
            g_uBs3TrapEipHint = CtxExpected.rip.u32;
            Bs3TrapSetJmpAndRestore(&Ctx, &TrapCtx);
            if (cbInstr < 16)
                bs3CpuBasic2_CompareUdCtx(&TrapCtx, &CtxExpected);
            else
                bs3CpuBasic2_CompareGpCtx(&TrapCtx, &CtxExpected, 0);
        }
        pbCodeBuf[off] = 0xf1; /* icebp */
    }

    /*
     * Pit instruction length violations against the segment limit (#GP).
     */
    if (!BS3_MODE_IS_RM_OR_V86(bMode) && bMode != BS3_MODE_LM64)
    {
        /** @todo */
    }

    /*
     * Pit instruction length violations against an invalid page (#PF).
     */
    if (BS3_MODE_IS_PAGED(bMode))
    {
        /** @todo */
    }

    return 0;
}


/**
 * Entrypoint for FAR RET tests.
 *
 * @returns 0 or BS3TESTDOMODE_SKIPPED.
 * @param   bMode       The CPU mode we're testing.
 */
BS3_DECL_FAR(uint8_t) BS3_CMN_FAR_NM(bs3CpuBasic2_instr_len)(uint8_t bMode)
{
    /*
     * Allocate three pages so we can straddle an instruction across the
     * boundrary for testing special IEM cases, with the last page being
     * made in accessible and useful for pitting #PF against #GP.
     */
    uint8_t BS3_FAR * const pbCodeBuf = (uint8_t BS3_FAR *)Bs3MemAlloc(BS3MEMKIND_REAL, X86_PAGE_SIZE * 3);
    //Bs3TestPrintf("pbCodeBuf=%p\n", pbCodeBuf);
    if (pbCodeBuf)
    {
        Bs3MemSet(pbCodeBuf, 0xf1, X86_PAGE_SIZE * 3);
        bs3CpuBasic2_SetGlobals(bMode);

        if (!BS3_MODE_IS_PAGED(bMode))
            bs3CpuBasic2_instr_len_Worker(bMode, pbCodeBuf);
        else
        {
            uint32_t const uFlatLastPg = Bs3SelPtrToFlat(pbCodeBuf) + X86_PAGE_SIZE * 2;
            int rc = Bs3PagingProtect(uFlatLastPg, X86_PAGE_SIZE, 0, X86_PTE_P);
            if (RT_SUCCESS(rc))
            {
                bs3CpuBasic2_instr_len_Worker(bMode, pbCodeBuf);
                Bs3PagingProtect(uFlatLastPg, X86_PAGE_SIZE, X86_PTE_P, 0);
            }
            else
                Bs3TestFailed("Failed to allocate 3 code pages");
        }

        Bs3MemFree(pbCodeBuf, X86_PAGE_SIZE * 3);
    }
    else
        Bs3TestFailed("Failed to allocate 3 code pages");
    return 0;
}

