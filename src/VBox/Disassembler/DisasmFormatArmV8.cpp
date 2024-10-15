/* $Id: DisasmFormatArmV8.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Disassembler - ARMv8 Style Formatter.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <VBox/dis.h>
#include "DisasmInternal.h"
#include "DisasmInternal-armv8.h"
#include <iprt/armv8.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char g_szSpaces[] =
"                                                                               ";
static const char g_aszArmV8RegGen32[32][4] =
{
    "w0\0",  "w1\0",  "w2\0",  "w3\0",  "w4\0",  "w5\0",  "w6\0",  "w7\0",  "w8\0",  "w9\0",  "w10",  "w11",  "w12",  "w13",  "w14",  "w15",
    "w16",   "w17",   "w18",   "w19",   "w20",   "w21",   "w22",   "w23",   "w24",   "w25",   "w26",  "w27",  "w28",  "w29", "w30",   "wzr"
};
static const char g_aszArmV8RegGen64[32][4] =
{
    "x0\0",  "x1\0",  "x2\0",  "x3\0",  "x4\0",  "x5\0",  "x6\0",  "x7\0",  "x8\0",  "x9\0",  "x10",  "x11",  "x12",  "x13",  "x14",  "x15",
    "x16",   "x17",   "x18",   "x19",   "x20",   "x21",   "x22",   "x23",   "x24",   "x25",   "x26",  "x27",  "x28",  "x29",  "x30",  "xzr"
};
static const char g_aszArmV8RegFpSingle[32][4] =
{
    "s0\0",  "s1\0",  "s2\0",  "s3\0",  "s4\0",  "s5\0",  "s6\0",  "s7\0",  "s8\0",  "s9\0",  "s10",  "s11",  "s12",  "s13",  "s14",  "s15",
    "s16",   "s17",   "s18",   "s19",   "s20",   "s21",   "s22",   "s23",   "s24",   "s25",   "s26",  "s27",  "s28",  "s29",  "s30",  "s31"
};
static const char g_aszArmV8RegFpDouble[32][4] =
{
    "d0\0",  "d1\0",  "d2\0",  "d3\0",  "d4\0",  "d5\0",  "d6\0",  "d7\0",  "d8\0",  "d9\0",  "d10",  "d11",  "d12",  "d13",  "d14",  "d15",
    "d16",   "d17",   "d18",   "d19",   "d20",   "d21",   "d22",   "d23",   "d24",   "d25",   "d26",  "d27",  "d28",  "d29",  "d30",  "d31"
};
static const char g_aszArmV8RegFpHalf[32][4] =
{
    "h0\0",  "h1\0",  "h2\0",  "h3\0",  "h4\0",  "h5\0",  "h6\0",  "h7\0",  "h8\0",  "h9\0",  "h10",  "h11",  "h12",  "h13",  "h14",  "h15",
    "h16",   "h17",   "h18",   "h19",   "h20",   "h21",   "h22",   "h23",   "h24",   "h25",   "h26",  "h27",  "h28",  "h29",  "h30",  "h31"
};
static const char g_aszArmV8RegSimdScalar128Bit[32][4] =
{
    "q0\0",  "q1\0",  "q2\0",  "q3\0",  "q4\0",  "q5\0",  "q6\0",  "q7\0",  "q8\0",  "q9\0",  "q10",  "q11",  "q12",  "q13",  "q14",  "q15",
    "q16",   "q17",   "q18",   "q19",   "q20",   "q21",   "q22",   "q23",   "q24",   "q25",   "q26",  "q27",  "q28",  "q29",  "q30",  "q31"
};
static const char g_aszArmV8Cond[16][3] =
{
    "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al", "al"
};
static const char *g_apszArmV8PState[] =
{
    /* kDisArmv8InstrPState_SPSel    */ "spsel",
    /* kDisArmv8InstrPState_DAIFSet  */ "daifset",
    /* kDisArmv8InstrPState_DAIFClr  */ "daifclr",
    /* kDisArmv8InstrPState_UAO      */ "uao",
    /* kDisArmv8InstrPState_PAN      */ "pan",
    /* kDisArmv8InstrPState_ALLINT   */ "allint",
    /* kDisArmv8InstrPState_PM       */ "pm",
    /* kDisArmv8InstrPState_SSBS     */ "ssbs",
    /* kDisArmv8InstrPState_DIT      */ "dit",
    /* kDisArmv8InstrPState_SVCRSM   */ "svcrsm",
    /* kDisArmv8InstrPState_SVCRZA   */ "svcrza",
    /* kDisArmv8InstrPState_SVCRSMZA */ "svcrsmza",
    /* kDisArmv8InstrPState_TCO      */ "tco"
};


/**
 * List of known system registers.
 *
 * The list MUST be in ascending order of the system register ID!
 */
static const struct
{
    /** IPRT system register ID. */
    uint32_t    idSysReg;
    /** Name of the system register. */
    const char  *pszSysReg;
    /** Character count of the system register name. */
    size_t      cchSysReg;
} g_aArmV8SysReg64[] =
{
#define DIS_ARMV8_SYSREG(a_idSysReg) { (ARMV8_AARCH64_SYSREG_ ## a_idSysReg), #a_idSysReg, sizeof(#a_idSysReg) - 1 }
    DIS_ARMV8_SYSREG(OSDTRRX_EL1),
    DIS_ARMV8_SYSREG(MDSCR_EL1),
    //DIS_ARMV8_SYSREG(DBGBVRn_EL1(a_Id)),
    //DIS_ARMV8_SYSREG(DBGBCRn_EL1(a_Id)),
    //DIS_ARMV8_SYSREG(DBGWVRn_EL1(a_Id)),
    //DIS_ARMV8_SYSREG(DBGWCRn_EL1(a_Id)),
    DIS_ARMV8_SYSREG(MDCCINT_EL1),
    DIS_ARMV8_SYSREG(OSDTRTX_EL1),
    DIS_ARMV8_SYSREG(OSECCR_EL1),
    DIS_ARMV8_SYSREG(MDRAR_EL1),
    DIS_ARMV8_SYSREG(OSLAR_EL1),
    DIS_ARMV8_SYSREG(OSLSR_EL1),
    DIS_ARMV8_SYSREG(OSDLR_EL1),
    DIS_ARMV8_SYSREG(MIDR_EL1),
    DIS_ARMV8_SYSREG(MPIDR_EL1),
    DIS_ARMV8_SYSREG(REVIDR_EL1),
    DIS_ARMV8_SYSREG(ID_PFR0_EL1),
    DIS_ARMV8_SYSREG(ID_PFR1_EL1),
    DIS_ARMV8_SYSREG(ID_DFR0_EL1),
    DIS_ARMV8_SYSREG(ID_AFR0_EL1),
    DIS_ARMV8_SYSREG(ID_MMFR0_EL1),
    DIS_ARMV8_SYSREG(ID_MMFR1_EL1),
    DIS_ARMV8_SYSREG(ID_MMFR2_EL1),
    DIS_ARMV8_SYSREG(ID_MMFR3_EL1),
    DIS_ARMV8_SYSREG(ID_ISAR0_EL1),
    DIS_ARMV8_SYSREG(ID_ISAR1_EL1),
    DIS_ARMV8_SYSREG(ID_ISAR2_EL1),
    DIS_ARMV8_SYSREG(ID_ISAR3_EL1),
    DIS_ARMV8_SYSREG(ID_ISAR4_EL1),
    DIS_ARMV8_SYSREG(ID_ISAR5_EL1),
    DIS_ARMV8_SYSREG(ID_MMFR4_EL1),
    DIS_ARMV8_SYSREG(ID_ISAR6_EL1),
    DIS_ARMV8_SYSREG(MVFR0_EL1),
    DIS_ARMV8_SYSREG(MVFR1_EL1),
    DIS_ARMV8_SYSREG(MVFR2_EL1),
    DIS_ARMV8_SYSREG(ID_PFR2_EL1),
    DIS_ARMV8_SYSREG(ID_DFR1_EL1),
    DIS_ARMV8_SYSREG(ID_MMFR5_EL1),
    DIS_ARMV8_SYSREG(ID_AA64PFR0_EL1),
    DIS_ARMV8_SYSREG(ID_AA64PFR1_EL1),
    DIS_ARMV8_SYSREG(ID_AA64ZFR0_EL1),
    DIS_ARMV8_SYSREG(ID_AA64SMFR0_EL1),
    DIS_ARMV8_SYSREG(ID_AA64DFR0_EL1),
    DIS_ARMV8_SYSREG(ID_AA64DFR1_EL1),
    DIS_ARMV8_SYSREG(ID_AA64AFR0_EL1),
    DIS_ARMV8_SYSREG(ID_AA64AFR1_EL1),
    DIS_ARMV8_SYSREG(ID_AA64ISAR0_EL1),
    DIS_ARMV8_SYSREG(ID_AA64ISAR1_EL1),
    DIS_ARMV8_SYSREG(ID_AA64ISAR2_EL1),
    DIS_ARMV8_SYSREG(ID_AA64MMFR0_EL1),
    DIS_ARMV8_SYSREG(ID_AA64MMFR1_EL1),
    DIS_ARMV8_SYSREG(ID_AA64MMFR2_EL1),
    DIS_ARMV8_SYSREG(SCTRL_EL1),
    DIS_ARMV8_SYSREG(ACTRL_EL1),
    DIS_ARMV8_SYSREG(CPACR_EL1),
    DIS_ARMV8_SYSREG(RGSR_EL1),
    DIS_ARMV8_SYSREG(GCR_EL1),
    DIS_ARMV8_SYSREG(ZCR_EL1),
    DIS_ARMV8_SYSREG(TRFCR_EL1),
    DIS_ARMV8_SYSREG(SMPRI_EL1),
    DIS_ARMV8_SYSREG(SMCR_EL1),
    DIS_ARMV8_SYSREG(TTBR0_EL1),
    DIS_ARMV8_SYSREG(TTBR1_EL1),
    DIS_ARMV8_SYSREG(TCR_EL1),
    DIS_ARMV8_SYSREG(APIAKeyLo_EL1),
    DIS_ARMV8_SYSREG(APIAKeyHi_EL1),
    DIS_ARMV8_SYSREG(APIBKeyLo_EL1),
    DIS_ARMV8_SYSREG(APIBKeyHi_EL1),
    DIS_ARMV8_SYSREG(APDAKeyLo_EL1),
    DIS_ARMV8_SYSREG(APDAKeyHi_EL1),
    DIS_ARMV8_SYSREG(APDBKeyLo_EL1),
    DIS_ARMV8_SYSREG(APDBKeyHi_EL1),
    DIS_ARMV8_SYSREG(APGAKeyLo_EL1),
    DIS_ARMV8_SYSREG(APGAKeyHi_EL1),
    DIS_ARMV8_SYSREG(SPSR_EL1),
    DIS_ARMV8_SYSREG(ELR_EL1),
    DIS_ARMV8_SYSREG(SP_EL0),
    DIS_ARMV8_SYSREG(SPSEL),
    DIS_ARMV8_SYSREG(CURRENTEL),
    DIS_ARMV8_SYSREG(PAN),
    DIS_ARMV8_SYSREG(UAO),
    DIS_ARMV8_SYSREG(ALLINT),
    DIS_ARMV8_SYSREG(ICC_PMR_EL1),
    DIS_ARMV8_SYSREG(AFSR0_EL1),
    DIS_ARMV8_SYSREG(AFSR1_EL1),
    DIS_ARMV8_SYSREG(ESR_EL1),
    DIS_ARMV8_SYSREG(ERRIDR_EL1),
    DIS_ARMV8_SYSREG(ERRSELR_EL1),
    DIS_ARMV8_SYSREG(FAR_EL1),
    DIS_ARMV8_SYSREG(PAR_EL1),
    DIS_ARMV8_SYSREG(MAIR_EL1),
    DIS_ARMV8_SYSREG(AMAIR_EL1),
    DIS_ARMV8_SYSREG(VBAR_EL1),
    DIS_ARMV8_SYSREG(ICC_IAR0_EL1),
    DIS_ARMV8_SYSREG(ICC_EOIR0_EL1),
    DIS_ARMV8_SYSREG(ICC_HPPIR0_EL1),
    DIS_ARMV8_SYSREG(ICC_BPR0_EL1),
    DIS_ARMV8_SYSREG(ICC_AP0R0_EL1),
    DIS_ARMV8_SYSREG(ICC_AP0R1_EL1),
    DIS_ARMV8_SYSREG(ICC_AP0R2_EL1),
    DIS_ARMV8_SYSREG(ICC_AP0R3_EL1),
    DIS_ARMV8_SYSREG(ICC_AP1R0_EL1),
    DIS_ARMV8_SYSREG(ICC_AP1R1_EL1),
    DIS_ARMV8_SYSREG(ICC_AP1R2_EL1),
    DIS_ARMV8_SYSREG(ICC_AP1R3_EL1),
    DIS_ARMV8_SYSREG(ICC_NMIAR1_EL1),
    DIS_ARMV8_SYSREG(ICC_DIR_EL1),
    DIS_ARMV8_SYSREG(ICC_RPR_EL1),
    DIS_ARMV8_SYSREG(ICC_SGI1R_EL1),
    DIS_ARMV8_SYSREG(ICC_ASGI1R_EL1),
    DIS_ARMV8_SYSREG(ICC_SGI0R_EL1),
    DIS_ARMV8_SYSREG(ICC_IAR1_EL1),
    DIS_ARMV8_SYSREG(ICC_EOIR1_EL1),
    DIS_ARMV8_SYSREG(ICC_HPPIR1_EL1),
    DIS_ARMV8_SYSREG(ICC_BPR1_EL1),
    DIS_ARMV8_SYSREG(ICC_CTLR_EL1),
    DIS_ARMV8_SYSREG(ICC_SRE_EL1),
    DIS_ARMV8_SYSREG(ICC_IGRPEN0_EL1),
    DIS_ARMV8_SYSREG(ICC_IGRPEN1_EL1),
    DIS_ARMV8_SYSREG(CONTEXTIDR_EL1),
    DIS_ARMV8_SYSREG(TPIDR_EL1),
    DIS_ARMV8_SYSREG(CNTKCTL_EL1),
    DIS_ARMV8_SYSREG(CSSELR_EL1),
    DIS_ARMV8_SYSREG(NZCV),
    DIS_ARMV8_SYSREG(DAIF),
    DIS_ARMV8_SYSREG(SVCR),
    DIS_ARMV8_SYSREG(DIT),
    DIS_ARMV8_SYSREG(SSBS),
    DIS_ARMV8_SYSREG(TCO),
    DIS_ARMV8_SYSREG(FPCR),
    DIS_ARMV8_SYSREG(FPSR),
    DIS_ARMV8_SYSREG(ICC_SRE_EL2),
    DIS_ARMV8_SYSREG(TPIDR_EL0),
    DIS_ARMV8_SYSREG(TPIDRRO_EL0),
    DIS_ARMV8_SYSREG(CNTFRQ_EL0),
    DIS_ARMV8_SYSREG(CNTVCT_EL0),
    DIS_ARMV8_SYSREG(CNTP_TVAL_EL0),
    DIS_ARMV8_SYSREG(CNTP_CTL_EL0),
    DIS_ARMV8_SYSREG(CNTP_CVAL_EL0),
    DIS_ARMV8_SYSREG(CNTV_CTL_EL0),
    DIS_ARMV8_SYSREG(VPIDR_EL2),
    DIS_ARMV8_SYSREG(VMPIDR_EL2),
    DIS_ARMV8_SYSREG(SCTLR_EL2),
    DIS_ARMV8_SYSREG(ACTLR_EL2),
    DIS_ARMV8_SYSREG(HCR_EL2),
    DIS_ARMV8_SYSREG(MDCR_EL2),
    DIS_ARMV8_SYSREG(CPTR_EL2),
    DIS_ARMV8_SYSREG(HSTR_EL2),
    DIS_ARMV8_SYSREG(HFGRTR_EL2),
    DIS_ARMV8_SYSREG(HFGWTR_EL2),
    DIS_ARMV8_SYSREG(HFGITR_EL2),
    DIS_ARMV8_SYSREG(HACR_EL2),
    DIS_ARMV8_SYSREG(ZCR_EL2),
    DIS_ARMV8_SYSREG(TRFCR_EL2),
    DIS_ARMV8_SYSREG(HCRX_EL2),
    DIS_ARMV8_SYSREG(SDER32_EL2),
    DIS_ARMV8_SYSREG(TTBR0_EL2),
    DIS_ARMV8_SYSREG(TTBR1_EL2),
    DIS_ARMV8_SYSREG(TCR_EL2),
    DIS_ARMV8_SYSREG(VTTBR_EL2),
    DIS_ARMV8_SYSREG(VTCR_EL2),
    DIS_ARMV8_SYSREG(VNCR_EL2),
    DIS_ARMV8_SYSREG(VSTTBR_EL2),
    DIS_ARMV8_SYSREG(VSTCR_EL2),
    DIS_ARMV8_SYSREG(DACR32_EL2),
    DIS_ARMV8_SYSREG(HDFGRTR_EL2),
    DIS_ARMV8_SYSREG(HDFGWTR_EL2),
    DIS_ARMV8_SYSREG(HAFGRTR_EL2),
    DIS_ARMV8_SYSREG(SPSR_EL2),
    DIS_ARMV8_SYSREG(ELR_EL2),
    DIS_ARMV8_SYSREG(SP_EL1),
    DIS_ARMV8_SYSREG(IFSR32_EL2),
    DIS_ARMV8_SYSREG(AFSR0_EL2),
    DIS_ARMV8_SYSREG(AFSR1_EL2),
    DIS_ARMV8_SYSREG(ESR_EL2),
    DIS_ARMV8_SYSREG(VSESR_EL2),
    DIS_ARMV8_SYSREG(FPEXC32_EL2),
    DIS_ARMV8_SYSREG(TFSR_EL2),
    DIS_ARMV8_SYSREG(FAR_EL2),
    DIS_ARMV8_SYSREG(HPFAR_EL2),
    DIS_ARMV8_SYSREG(PMSCR_EL2),
    DIS_ARMV8_SYSREG(MAIR_EL2),
    DIS_ARMV8_SYSREG(AMAIR_EL2),
    DIS_ARMV8_SYSREG(MPAMHCR_EL2),
    DIS_ARMV8_SYSREG(MPAMVPMV_EL2),
    DIS_ARMV8_SYSREG(MPAM2_EL2),
    DIS_ARMV8_SYSREG(MPAMVPM0_EL2),
    DIS_ARMV8_SYSREG(MPAMVPM1_EL2),
    DIS_ARMV8_SYSREG(MPAMVPM2_EL2),
    DIS_ARMV8_SYSREG(MPAMVPM3_EL2),
    DIS_ARMV8_SYSREG(MPAMVPM4_EL2),
    DIS_ARMV8_SYSREG(MPAMVPM5_EL2),
    DIS_ARMV8_SYSREG(MPAMVPM6_EL2),
    DIS_ARMV8_SYSREG(MPAMVPM7_EL2),
    DIS_ARMV8_SYSREG(VBAR_EL2),
    DIS_ARMV8_SYSREG(RVBAR_EL2),
    DIS_ARMV8_SYSREG(RMR_EL2),
    DIS_ARMV8_SYSREG(VDISR_EL2),
    DIS_ARMV8_SYSREG(CONTEXTIDR_EL2),
    DIS_ARMV8_SYSREG(TPIDR_EL2),
    DIS_ARMV8_SYSREG(SCXTNUM_EL2),
    DIS_ARMV8_SYSREG(CNTVOFF_EL2),
    DIS_ARMV8_SYSREG(CNTPOFF_EL2),
    DIS_ARMV8_SYSREG(CNTHCTL_EL2),
    DIS_ARMV8_SYSREG(CNTHP_TVAL_EL2),
    DIS_ARMV8_SYSREG(CNTHP_CTL_EL2),
    DIS_ARMV8_SYSREG(CNTHP_CVAL_EL2),
    DIS_ARMV8_SYSREG(CNTHV_TVAL_EL2),
    DIS_ARMV8_SYSREG(CNTHV_CTL_EL2),
    DIS_ARMV8_SYSREG(CNTHV_CVAL_EL2),
    DIS_ARMV8_SYSREG(CNTHVS_TVAL_EL2),
    DIS_ARMV8_SYSREG(CNTHVS_CTL_EL2),
    DIS_ARMV8_SYSREG(CNTHVS_CVAL_EL2),
    DIS_ARMV8_SYSREG(CNTHPS_TVAL_EL2),
    DIS_ARMV8_SYSREG(CNTHPS_CTL_EL2),
    DIS_ARMV8_SYSREG(CNTHPS_CVAL_EL2),
    DIS_ARMV8_SYSREG(SP_EL2)
#undef  DIS_ARMV8_SYSREG
};


/**
 * Gets the base register name for the given parameter.
 *
 * @returns Pointer to the register name.
 * @param   pDis        The disassembler state.
 * @param   pParam      The parameter.
 * @param   pcchReg     Where to store the length of the name.
 */
DECLINLINE(const char *) disasmFormatArmV8Reg(PCDISSTATE pDis, PCDISOPPARAMARMV8REG pReg, size_t *pcchReg)
{
    RT_NOREF_PV(pDis);

    switch (pReg->enmRegType)
    {
        case kDisOpParamArmV8RegType_Gpr_32Bit:
        {
            Assert(pReg->idReg < RT_ELEMENTS(g_aszArmV8RegGen32));
            const char *psz = g_aszArmV8RegGen32[pReg->idReg];
            *pcchReg = 2 + !!psz[2];
            return psz;
        }
        case kDisOpParamArmV8RegType_Gpr_64Bit:
        {
            Assert(pReg->idReg < RT_ELEMENTS(g_aszArmV8RegGen64));
            const char *psz = g_aszArmV8RegGen64[pReg->idReg];
            *pcchReg = 2 + !!psz[2];
            return psz;
        }
        case kDisOpParamArmV8RegType_FpReg_Single:
        {
            Assert(pDis->armv8.enmFpType != kDisArmv8InstrFpType_Invalid);
            Assert(pReg->idReg < RT_ELEMENTS(g_aszArmV8RegFpSingle));
            const char *psz = g_aszArmV8RegFpSingle[pReg->idReg];
            *pcchReg = 2 + !!psz[2];
            return psz;
        }
        case kDisOpParamArmV8RegType_FpReg_Double:
        {
            Assert(pDis->armv8.enmFpType != kDisArmv8InstrFpType_Invalid);
            Assert(pReg->idReg < RT_ELEMENTS(g_aszArmV8RegFpDouble));
            const char *psz = g_aszArmV8RegFpDouble[pReg->idReg];
            *pcchReg = 2 + !!psz[2];
            return psz;
        }
        case kDisOpParamArmV8RegType_FpReg_Half:
        {
            Assert(pDis->armv8.enmFpType != kDisArmv8InstrFpType_Invalid);
            Assert(pReg->idReg < RT_ELEMENTS(g_aszArmV8RegFpHalf));
            const char *psz = g_aszArmV8RegFpHalf[pReg->idReg];
            *pcchReg = 2 + !!psz[2];
            return psz;
        }
        case kDisOpParamArmV8RegType_Simd_Scalar_64Bit:
        {
            /* Using the floating point double register names here. */
            Assert(pReg->idReg < RT_ELEMENTS(g_aszArmV8RegFpDouble));
            const char *psz = g_aszArmV8RegFpDouble[pReg->idReg];
            *pcchReg = 2 + !!psz[2];
            return psz;
        }
        case kDisOpParamArmV8RegType_Simd_Scalar_128Bit:
        {
            Assert(pReg->idReg < RT_ELEMENTS(g_aszArmV8RegSimdScalar128Bit));
            const char *psz = g_aszArmV8RegSimdScalar128Bit[pReg->idReg];
            *pcchReg = 2 + !!psz[2];
            return psz;
        }
        default:
            AssertFailed();
            *pcchReg = 0;
            return NULL;
    }
}


/**
 * Gets the base register name for the given parameter.
 *
 * @returns Pointer to the register name.
 * @param   pDis        The disassembler state.
 * @param   pParam      The parameter.
 * @param   pachTmp     Pointer to temporary string storage when building
 *                      the register name.
 * @param   pcchReg     Where to store the length of the name.
 */
static const char *disasmFormatArmV8SysReg(PCDISSTATE pDis, PCDISOPPARAM pParam, char *pachTmp, size_t *pcchReg)
{
    RT_NOREF_PV(pDis);

    /* Try to find the system register ID in the table. */
    /** @todo Binary search (lazy). */
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aArmV8SysReg64); i++)
    {
        if (g_aArmV8SysReg64[i].idSysReg == pParam->armv8.Op.idSysReg)
        {
            *pcchReg = g_aArmV8SysReg64[i].cchSysReg;
            return g_aArmV8SysReg64[i].pszSysReg;
        }
    }

    /* Generate S<op0>_<op1>_<Cn>_<Cm>_<op2> identifier. */
    uint32_t const idSysReg = pParam->armv8.Op.idSysReg;
    uint8_t idx = 0;
    pachTmp[idx++] = 'S';
    pachTmp[idx++] = '2' + ((idSysReg >> 14) & 0x1);
    pachTmp[idx++] = '_';
    pachTmp[idx++] = '0' + ((idSysReg >> 11) & 0x7);
    pachTmp[idx++] = '_';

    uint8_t bTmp = (idSysReg >> 7) & 0xf;
    if (bTmp >= 10)
    {
        pachTmp[idx++] = '1' + (bTmp - 10);
        bTmp -= 10;
    }
    pachTmp[idx++] = '0' + bTmp;
    pachTmp[idx++] = '_';

    bTmp = (idSysReg >> 3) & 0xf;
    if (bTmp >= 10)
    {
        pachTmp[idx++] = '1' + (bTmp - 10);
        bTmp -= 10;
    }
    pachTmp[idx++] = '0' + bTmp;

    pachTmp[idx++] = '_';
    pachTmp[idx++] = '0' + (idSysReg & 0x7);
    pachTmp[idx]   = '\0';
    *pcchReg = idx;
    return pachTmp;
}


/**
 * Formats the current instruction in ARMv8 style.
 *
 *
 * @returns The number of output characters. If this is >= cchBuf, then the content
 *          of pszBuf will be truncated.
 * @param   pDis            Pointer to the disassembler state.
 * @param   pszBuf          The output buffer.
 * @param   cchBuf          The size of the output buffer.
 * @param   fFlags          Format flags, see DIS_FORMAT_FLAGS_*.
 * @param   pfnGetSymbol    Get symbol name for a jmp or call target address. Optional.
 * @param   pvUser          User argument for pfnGetSymbol.
 */
DISDECL(size_t) DISFormatArmV8Ex(PCDISSTATE pDis, char *pszBuf, size_t cchBuf, uint32_t fFlags,
                                 PFNDISGETSYMBOL pfnGetSymbol, void *pvUser)
{
    /*
     * Input validation and massaging.
     */
    AssertPtr(pDis);
    AssertPtrNull(pszBuf);
    Assert(pszBuf || !cchBuf);
    AssertPtrNull(pfnGetSymbol);
    AssertMsg(DIS_FMT_FLAGS_IS_VALID(fFlags), ("%#x\n", fFlags));
    if (fFlags & DIS_FMT_FLAGS_ADDR_COMMENT)
        fFlags = (fFlags & ~DIS_FMT_FLAGS_ADDR_LEFT) | DIS_FMT_FLAGS_ADDR_RIGHT;
    if (fFlags & DIS_FMT_FLAGS_BYTES_COMMENT)
        fFlags = (fFlags & ~DIS_FMT_FLAGS_BYTES_LEFT) | DIS_FMT_FLAGS_BYTES_RIGHT;

    PCDISOPCODE const pOp = pDis->pCurInstr;

    /*
     * Output macros
     */
    char           *pszDst = pszBuf;
    size_t          cchDst = cchBuf;
    size_t          cchOutput = 0;
#define PUT_C(ch)       \
            do { \
                cchOutput++; \
                if (cchDst > 1) \
                { \
                    cchDst--; \
                    *pszDst++ = (ch); \
                } \
            } while (0)
#define PUT_STR(pszSrc, cchSrc) \
            do { \
                cchOutput += (cchSrc); \
                if (cchDst > (cchSrc)) \
                { \
                    memcpy(pszDst, (pszSrc), (cchSrc)); \
                    pszDst += (cchSrc); \
                    cchDst -= (cchSrc); \
                } \
                else if (cchDst > 1) \
                { \
                    memcpy(pszDst, (pszSrc), cchDst - 1); \
                    pszDst += cchDst - 1; \
                    cchDst = 1; \
                } \
            } while (0)
#define PUT_SZ(sz) \
            PUT_STR((sz), sizeof(sz) - 1)
#define PUT_SZ_STRICT(szStrict, szRelaxed) \
            do { if (fFlags & DIS_FMT_FLAGS_STRICT) PUT_SZ(szStrict); else PUT_SZ(szRelaxed); } while (0)
#define PUT_PSZ(psz) \
            do { const size_t cchTmp = strlen(psz); PUT_STR((psz), cchTmp); } while (0)
#define PUT_NUM(cch, fmt, num) \
            do { \
                 cchOutput += (cch); \
                 if (cchDst > 1) \
                 { \
                    const size_t cchTmp = RTStrPrintf(pszDst, cchDst, fmt, (num)); \
                    pszDst += cchTmp; \
                    cchDst -= cchTmp; \
                    Assert(cchTmp == (cch) || cchDst == 1); \
                 } \
            } while (0)
/** @todo add two flags for choosing between %X / %x and h / 0x. */
#define PUT_NUM_8(num)  PUT_NUM(4,  "0x%02x", (uint8_t)(num))
#define PUT_NUM_16(num) PUT_NUM(6,  "0x%04x", (uint16_t)(num))
#define PUT_NUM_32(num) PUT_NUM(10, "0x%08x", (uint32_t)(num))
#define PUT_NUM_64(num) PUT_NUM(18, "0x%016RX64", (uint64_t)(num))

#define PUT_NUM_SIGN(cch, fmt, num, stype, utype) \
            do { \
                if ((stype)(num) >= 0) \
                { \
                    PUT_C('+'); \
                    PUT_NUM(cch, fmt, (utype)(num)); \
                } \
                else \
                { \
                    PUT_C('-'); \
                    PUT_NUM(cch, fmt, (utype)-(stype)(num)); \
                } \
            } while (0)
#define PUT_NUM_S8(num)  PUT_NUM_SIGN(4,  "0x%02x", num, int8_t,  uint8_t)
#define PUT_NUM_S16(num) PUT_NUM_SIGN(6,  "0x%04x", num, int16_t, uint16_t)
#define PUT_NUM_S32(num) PUT_NUM_SIGN(10, "0x%08x", num, int32_t, uint32_t)
#define PUT_NUM_S64(num) PUT_NUM_SIGN(18, "0x%016RX64", num, int64_t, uint64_t)

#define PUT_SYMBOL_TWO(a_rcSym, a_szStart, a_chEnd) \
        do { \
            if (RT_SUCCESS(a_rcSym)) \
            { \
                PUT_SZ(a_szStart); \
                PUT_PSZ(szSymbol); \
                if (off != 0) \
                { \
                    if ((int8_t)off == off) \
                        PUT_NUM_S8(off); \
                    else if ((int16_t)off == off) \
                        PUT_NUM_S16(off); \
                    else if ((int32_t)off == off) \
                        PUT_NUM_S32(off); \
                    else \
                        PUT_NUM_S64(off); \
                } \
                PUT_C(a_chEnd); \
            } \
        } while (0)

#define PUT_SYMBOL(a_uSeg, a_uAddr, a_szStart, a_chEnd) \
        do { \
            if (pfnGetSymbol) \
            { \
                int rcSym = pfnGetSymbol(pDis, a_uSeg, a_uAddr, szSymbol, sizeof(szSymbol), &off, pvUser); \
                PUT_SYMBOL_TWO(rcSym, a_szStart, a_chEnd); \
            } \
        } while (0)


    /*
     * The address?
     */
    if (fFlags & DIS_FMT_FLAGS_ADDR_LEFT)
    {
#if HC_ARCH_BITS == 64 || GC_ARCH_BITS == 64
        if (pDis->uInstrAddr >= _4G)
            PUT_NUM(9, "%08x`", (uint32_t)(pDis->uInstrAddr >> 32));
#endif
        PUT_NUM(8, "%08x", (uint32_t)pDis->uInstrAddr);
        PUT_C(' ');
    }

    /*
     * The opcode bytes?
     */
    if (fFlags & DIS_FMT_FLAGS_BYTES_LEFT)
    {
        size_t cchTmp = disFormatBytes(pDis, pszDst, cchDst, fFlags);
        cchOutput += cchTmp;
        if (cchDst > 1)
        {
            if (cchTmp <= cchDst)
            {
                cchDst -= cchTmp;
                pszDst += cchTmp;
            }
            else
            {
                pszDst += cchDst - 1;
                cchDst = 1;
            }
        }

        /* Some padding to align the instruction. */
        size_t cchPadding = (7 * (2 + !!(fFlags & DIS_FMT_FLAGS_BYTES_SPACED)))
                          + !!(fFlags & DIS_FMT_FLAGS_BYTES_BRACKETS) * 2
                          + 2;
        cchPadding = cchTmp + 1 >= cchPadding ? 1 : cchPadding - cchTmp;
        PUT_STR(g_szSpaces, cchPadding);
    }


    /*
     * Filter out invalid opcodes first as they need special
     * treatment. UDF is an exception and should be handled normally.
     */
    size_t const offInstruction = cchOutput;
    if (pOp->uOpcode == OP_INVALID)
        PUT_SZ("Illegal opcode");
    else
    {
        /* Start with the instruction. */
        PUT_PSZ(pOp->pszOpcode);

        /* Add any conditionals. */
        if (pDis->armv8.enmCond != kDisArmv8InstrCond_Al)
        {
            PUT_C('.');
            Assert((uint16_t)pDis->armv8.enmCond < RT_ELEMENTS(g_aszArmV8Cond));
            PUT_STR(g_aszArmV8Cond[pDis->armv8.enmCond], sizeof(g_aszArmV8Cond[0]) - 1);
        }

        /*
         * Format the parameters.
         */
        RTINTPTR off;
        char szSymbol[128];
        for (uint32_t i = 0; i < RT_ELEMENTS(pDis->aParams); i++)
        {
            PCDISOPPARAM pParam = &pDis->aParams[i];

            /* First None parameter marks end of parameters. */
            if (pParam->armv8.enmType == kDisArmv8OpParmNone)
                break;

            if (i > 0)
                PUT_C(',');
            PUT_C(' '); /** @todo Make the indenting configurable. */

            switch (pParam->armv8.enmType)
            {
                case kDisArmv8OpParmImm:
                {
                    PUT_C('#');
                    switch (pParam->fUse & (  DISUSE_IMMEDIATE8 | DISUSE_IMMEDIATE16 | DISUSE_IMMEDIATE32 | DISUSE_IMMEDIATE64
                                            | DISUSE_IMMEDIATE16_SX8 | DISUSE_IMMEDIATE32_SX8 | DISUSE_IMMEDIATE64_SX8))
                    {
                        case DISUSE_IMMEDIATE8:
                            PUT_NUM_8(pParam->uValue);
                            break;
                        case DISUSE_IMMEDIATE16:
                            PUT_NUM_16(pParam->uValue);
                            break;
                        case DISUSE_IMMEDIATE16_SX8:
                            PUT_NUM_16(pParam->uValue);
                            break;
                        case DISUSE_IMMEDIATE32:
                            PUT_NUM_32(pParam->uValue);
                            /** @todo Symbols */
                            break;
                        case DISUSE_IMMEDIATE32_SX8:
                            PUT_NUM_32(pParam->uValue);
                            break;
                        case DISUSE_IMMEDIATE64_SX8:
                            PUT_NUM_64(pParam->uValue);
                            break;
                        case DISUSE_IMMEDIATE64:
                            PUT_NUM_64(pParam->uValue);
                            /** @todo Symbols */
                            break;
                        default:
                            AssertFailed();
                            break;
                    }
                    break;
                }
                case kDisArmv8OpParmImmRel:
                /*case kDisParmParseImmAdr:*/
                {
                    int32_t offDisplacement;

                    PUT_C('#');
                    if (pParam->fUse & DISUSE_IMMEDIATE8_REL)
                    {
                        offDisplacement = (int8_t)pParam->uValue;
                        if (fFlags & DIS_FMT_FLAGS_RELATIVE_BRANCH)
                            PUT_NUM_S8(offDisplacement * sizeof(uint32_t));
                    }
                    else if (pParam->fUse & DISUSE_IMMEDIATE16_REL)
                    {
                        offDisplacement = (int16_t)pParam->uValue;
                        if (fFlags & DIS_FMT_FLAGS_RELATIVE_BRANCH)
                            PUT_NUM_S16(offDisplacement * sizeof(uint32_t));
                    }
                    else
                    {
                        offDisplacement = (int32_t)pParam->uValue;
                        if (fFlags & DIS_FMT_FLAGS_RELATIVE_BRANCH)
                            PUT_NUM_S32(offDisplacement * sizeof(uint32_t));
                    }
                    if (fFlags & DIS_FMT_FLAGS_RELATIVE_BRANCH)
                        PUT_SZ(" ; (");

                    RTUINTPTR uTrgAddr = pDis->uInstrAddr + (offDisplacement * sizeof(uint32_t));
                    if (   pDis->uCpuMode == DISCPUMODE_ARMV8_A32
                        || pDis->uCpuMode == DISCPUMODE_ARMV8_T32)
                        PUT_NUM_32(uTrgAddr);
                    else if (pDis->uCpuMode == DISCPUMODE_ARMV8_A64)
                        PUT_NUM_64(uTrgAddr);
                    else
                        AssertReleaseFailed();

                    if (fFlags & DIS_FMT_FLAGS_RELATIVE_BRANCH)
                    {
                        PUT_SYMBOL(DIS_FMT_SEL_FROM_REG(DISSELREG_CS), uTrgAddr, " = ", ' ');
                        PUT_C(')');
                    }
                    else
                        PUT_SYMBOL(DIS_FMT_SEL_FROM_REG(DISSELREG_CS), uTrgAddr, " (", ')');
                    break;
                }
                case kDisArmv8OpParmReg:
                {
                    Assert(!(pParam->fUse & (DISUSE_DISPLACEMENT8 | DISUSE_DISPLACEMENT16 | DISUSE_DISPLACEMENT32 | DISUSE_DISPLACEMENT64 | DISUSE_RIPDISPLACEMENT32)));

                    size_t cchReg;
                    const char *pszReg = disasmFormatArmV8Reg(pDis, &pParam->armv8.Op.Reg, &cchReg);
                    PUT_STR(pszReg, cchReg);
                    break;
                }
                case kDisArmv8OpParmSysReg:
                {
                    Assert(pParam->fUse == DISUSE_REG_SYSTEM);

                    size_t cchReg;
                    char achTmp[32];
                    const char *pszReg = disasmFormatArmV8SysReg(pDis, pParam, &achTmp[0], &cchReg);
                    PUT_STR(pszReg, cchReg);
                    break;
                }
                case kDisArmv8OpParmAddrInGpr:
                {
                    Assert(   (pParam->fUse & (DISUSE_PRE_INDEXED | DISUSE_POST_INDEXED))
                           != (DISUSE_PRE_INDEXED | DISUSE_POST_INDEXED));
                    Assert(   (   RT_BOOL(pParam->fUse & (DISUSE_PRE_INDEXED | DISUSE_POST_INDEXED))
                               != RT_BOOL(pParam->fUse & DISUSE_INDEX))
                           || !(pParam->fUse & (DISUSE_PRE_INDEXED | DISUSE_POST_INDEXED | DISUSE_INDEX)));

                    PUT_C('[');

                    size_t cchReg;
                    const char *pszReg = disasmFormatArmV8Reg(pDis, &pParam->armv8.Op.Reg, &cchReg);
                    PUT_STR(pszReg, cchReg);

                    if (pParam->fUse & DISUSE_POST_INDEXED)
                    {
                        Assert(pParam->armv8.enmExtend == kDisArmv8OpParmExtendNone);
                        PUT_SZ("], #");
                        PUT_NUM_S16(pParam->armv8.u.offBase);
                    }
                    else
                    {
                        if (pParam->fUse & DISUSE_INDEX)
                        {
                            PUT_SZ(", ");

                            pszReg = disasmFormatArmV8Reg(pDis, &pParam->armv8.GprIndex, &cchReg);
                            PUT_STR(pszReg, cchReg);
                        }
                        else if (pParam->armv8.u.offBase)
                        {
                            PUT_SZ(", #");
                            PUT_NUM_S16(pParam->armv8.u.offBase);
                        }

                        if (pParam->armv8.enmExtend != kDisArmv8OpParmExtendNone)
                        {
                            PUT_SZ(", ");
                            switch (pParam->armv8.enmExtend)
                            {
                                case kDisArmv8OpParmExtendUxtX: /* UXTX is same as LSL which is preferred by most disassemblers/assemblers. */
                                case kDisArmv8OpParmExtendLsl:
                                    PUT_SZ("LSL #");
                                    break;
                                case kDisArmv8OpParmExtendUxtB: PUT_SZ("UXTB #"); break;
                                case kDisArmv8OpParmExtendUxtH: PUT_SZ("UXTH #"); break;
                                case kDisArmv8OpParmExtendUxtW: PUT_SZ("UXTW #"); break;
                                case kDisArmv8OpParmExtendSxtB: PUT_SZ("SXTB #"); break;
                                case kDisArmv8OpParmExtendSxtH: PUT_SZ("SXTH #"); break;
                                case kDisArmv8OpParmExtendSxtW: PUT_SZ("SXTW #"); break;
                                case kDisArmv8OpParmExtendSxtX: PUT_SZ("SXTX #"); break;
                                default:
                                    AssertFailed();
                            }
                            PUT_NUM_8(pParam->armv8.u.cExtend);
                        }

                        PUT_C(']');

                        if (pParam->fUse & DISUSE_PRE_INDEXED)
                            PUT_C('!');
                    }

                    break;
                }
                case kDisArmv8OpParmCond:
                {
                    Assert((uint16_t)pParam->armv8.Op.enmCond < RT_ELEMENTS(g_aszArmV8Cond));
                    PUT_STR(g_aszArmV8Cond[pParam->armv8.Op.enmCond], sizeof(g_aszArmV8Cond[0]) - 1);
                    break;
                }
                case kDisArmv8OpParmPState:
                {
                    Assert((uint16_t)pParam->armv8.Op.enmPState < RT_ELEMENTS(g_apszArmV8PState));
                    PUT_PSZ(g_apszArmV8PState[pParam->armv8.Op.enmPState]);
                    break;
                }
                default:
                    AssertFailed();
            }

            if (   pParam->armv8.enmType != kDisArmv8OpParmAddrInGpr
                && pParam->armv8.enmExtend != kDisArmv8OpParmExtendNone)
            {
                Assert(   pParam->armv8.enmType == kDisArmv8OpParmImm
                       || pParam->armv8.enmType == kDisArmv8OpParmReg);
                PUT_SZ(", ");
                switch (pParam->armv8.enmExtend)
                {
                    case kDisArmv8OpParmExtendLsl:
                        PUT_SZ("LSL #");
                        break;
                    case kDisArmv8OpParmExtendLsr:
                        PUT_SZ("LSR #");
                        break;
                    case kDisArmv8OpParmExtendAsr:
                        PUT_SZ("ASR #");
                        break;
                    case kDisArmv8OpParmExtendRor:
                        PUT_SZ("ROR #");
                        break;
                    default:
                        AssertFailed();
                }
                PUT_NUM_8(pParam->armv8.u.cExtend);
            }
        }
    }

    /*
     * Any additional output to the right of the instruction?
     */
    if (fFlags & (DIS_FMT_FLAGS_BYTES_RIGHT | DIS_FMT_FLAGS_ADDR_RIGHT))
    {
        /* some up front padding. */
        size_t cchPadding = cchOutput - offInstruction;
        cchPadding = cchPadding + 1 >= 42 ? 1 : 42 - cchPadding;
        PUT_STR(g_szSpaces, cchPadding);

        /* comment? */
        if (fFlags & (DIS_FMT_FLAGS_BYTES_RIGHT | DIS_FMT_FLAGS_ADDR_RIGHT))
            PUT_SZ(";");

        /*
         * The address?
         */
        if (fFlags & DIS_FMT_FLAGS_ADDR_RIGHT)
        {
            PUT_C(' ');
#if HC_ARCH_BITS == 64 || GC_ARCH_BITS == 64
            if (pDis->uInstrAddr >= _4G)
                PUT_NUM(9, "%08x`", (uint32_t)(pDis->uInstrAddr >> 32));
#endif
            PUT_NUM(8, "%08x", (uint32_t)pDis->uInstrAddr);
        }

        /*
         * Opcode bytes?
         */
        if (fFlags & DIS_FMT_FLAGS_BYTES_RIGHT)
        {
            PUT_C(' ');
            size_t cchTmp = disFormatBytes(pDis, pszDst, cchDst, fFlags);
            cchOutput += cchTmp;
            if (cchTmp >= cchDst)
                cchTmp = cchDst - (cchDst != 0);
            cchDst -= cchTmp;
            pszDst += cchTmp;
        }
    }

    /*
     * Terminate it - on overflow we'll have reserved one byte for this.
     */
    if (cchDst > 0)
        *pszDst = '\0';
    else
        Assert(!cchBuf);

    /* clean up macros */
#undef PUT_PSZ
#undef PUT_SZ
#undef PUT_STR
#undef PUT_C
    return cchOutput;
}


/**
 * Formats the current instruction in ARMv8 style.
 *
 * This is a simplified version of DISFormatArmV8Ex() provided for your convenience.
 *
 *
 * @returns The number of output characters. If this is >= cchBuf, then the content
 *          of pszBuf will be truncated.
 * @param   pDis    Pointer to the disassembler state.
 * @param   pszBuf  The output buffer.
 * @param   cchBuf  The size of the output buffer.
 */
DISDECL(size_t) DISFormatArmV8(PCDISSTATE pDis, char *pszBuf, size_t cchBuf)
{
    return DISFormatArmV8Ex(pDis, pszBuf, cchBuf, 0 /* fFlags */, NULL /* pfnGetSymbol */, NULL /* pvUser */);
}
