/* $Id: CPUM-armv8.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPUM - CPU Monitor / Manager (ARMv8 variant).
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

/** @page pg_cpum CPUM - CPU Monitor / Manager
 *
 * The CPU Monitor / Manager keeps track of all the CPU registers.
 * This is the ARMv8 variant which is doing much less than its x86/AMD6464
 * counterpart due to the fact that we currently only support the NEM backends
 * for running ARM guests. It might become complex iff we decide to implement our
 * own hypervisor.
 *
 * @section sec_cpum_logging_armv8      Logging Level Assignments.
 *
 * Following log level assignments:
 *      - @todo
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_CPUM
#define CPUM_WITH_NONCONST_HOST_FEATURES
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/cpumdis.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/ssm.h>
#include "CPUMInternal-armv8.h"
#include <VBox/vmm/vm.h>

#include <VBox/param.h>
#include <VBox/dis.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/cpuset.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/string.h>
#include <iprt/armv8.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Internal form used by the macros. */
#ifdef VBOX_WITH_STATISTICS
# define RINT(a_uFirst, a_uLast, a_enmRdFn, a_enmWrFn, a_offCpumCpu, a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask, a_szName) \
    { a_uFirst, a_uLast, a_enmRdFn, a_enmWrFn, a_offCpumCpu, 0, 0, a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask, a_szName, \
      { 0 }, { 0 }, { 0 }, { 0 } }
#else
# define RINT(a_uFirst, a_uLast, a_enmRdFn, a_enmWrFn, a_offCpumCpu, a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask, a_szName) \
    { a_uFirst, a_uLast, a_enmRdFn, a_enmWrFn, a_offCpumCpu, 0, 0, a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask, a_szName }
#endif

/** Function handlers, extended version. */
#define MFX(a_uMsr, a_szName, a_enmRdFnSuff, a_enmWrFnSuff, a_uValue, a_fWrIgnMask, a_fWrGpMask) \
    RINT(a_uMsr, a_uMsr, kCpumSysRegRdFn_##a_enmRdFnSuff, kCpumSysRegWrFn_##a_enmWrFnSuff, 0, a_uValue, a_fWrIgnMask, a_fWrGpMask, a_szName)
/** Function handlers, read-only. */
#define MFO(a_uMsr, a_szName, a_enmRdFnSuff) \
    RINT(a_uMsr, a_uMsr, kCpumSysRegRdFn_##a_enmRdFnSuff, kCpumSysRegWrFn_ReadOnly, 0, 0, 0, UINT64_MAX, a_szName)
/** Read-only fixed value, ignores all writes. */
#define MVI(a_uMsr, a_szName, a_uValue) \
    RINT(a_uMsr, a_uMsr, kCpumSysRegRdFn_FixedValue, kCpumSysRegWrFn_IgnoreWrite, 0, a_uValue, UINT64_MAX, 0, a_szName)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * What kind of cpu info dump to perform.
 */
typedef enum CPUMDUMPTYPE
{
    CPUMDUMPTYPE_TERSE,
    CPUMDUMPTYPE_DEFAULT,
    CPUMDUMPTYPE_VERBOSE
} CPUMDUMPTYPE;
/** Pointer to a cpu info dump type. */
typedef CPUMDUMPTYPE *PCPUMDUMPTYPE;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int)  cpumR3LiveExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uPass);
static DECLCALLBACK(int)  cpumR3SaveExec(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(int)  cpumR3LoadPrep(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(int)  cpumR3LoadExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);
static DECLCALLBACK(int)  cpumR3LoadDone(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(void) cpumR3InfoAll(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) cpumR3InfoGuest(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void) cpumR3InfoGuestInstr(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if defined(RT_ARCH_ARM64)
/** Host CPU features. */
DECL_HIDDEN_DATA(CPUHOSTFEATURES) g_CpumHostFeatures;
#endif

/**
 * System register ranges.
 */
static CPUMSYSREGRANGE const g_aSysRegRanges[] =
{
    MFX(ARMV8_AARCH64_SYSREG_OSLAR_EL1, "OSLAR_EL1", WriteOnly, OslarEl1, 0, UINT64_C(0xfffffffffffffffe), UINT64_C(0xfffffffffffffffe)),
    MFO(ARMV8_AARCH64_SYSREG_OSLSR_EL1, "OSLSR_EL1", OslsrEl1),
    MVI(ARMV8_AARCH64_SYSREG_OSDLR_EL1, "OSDLR_EL1", 0)
};


/** Saved state field descriptors for CPUMCTX. */
static const SSMFIELD g_aCpumCtxFields[] =
{
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[0].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[1].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[2].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[3].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[4].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[5].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[6].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[7].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[8].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[9].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[10].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[11].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[12].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[13].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[14].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[15].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[16].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[17].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[18].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[19].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[20].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[21].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[22].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[23].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[24].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[25].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[26].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[27].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[28].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[29].x),
    SSMFIELD_ENTRY(         CPUMCTX, aGRegs[30].x),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[0].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[1].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[2].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[3].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[4].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[5].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[6].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[7].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[8].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[9].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[10].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[11].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[12].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[13].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[14].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[15].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[16].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[17].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[18].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[19].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[20].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[21].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[22].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[23].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[24].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[25].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[26].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[27].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[28].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[29].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[30].v),
    SSMFIELD_ENTRY(         CPUMCTX, aVRegs[31].v),
    SSMFIELD_ENTRY(         CPUMCTX, aSpReg[0].u64),
    SSMFIELD_ENTRY(         CPUMCTX, aSpReg[1].u64),
    SSMFIELD_ENTRY(         CPUMCTX, Pc.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Spsr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Elr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Sctlr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Tcr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Ttbr0.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Ttbr1.u64),
    SSMFIELD_ENTRY(         CPUMCTX, VBar.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[0].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[0].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[1].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[1].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[2].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[2].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[3].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[3].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[4].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[4].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[5].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[5].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[6].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[6].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[7].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[7].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[8].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[8].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[9].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[9].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[10].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[10].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[11].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[11].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[12].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[12].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[13].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[13].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[14].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[14].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[15].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aBp[15].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[0].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[0].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[1].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[1].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[2].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[2].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[3].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[3].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[4].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[4].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[5].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[5].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[6].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[6].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[7].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[7].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[8].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[8].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[9].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[9].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[10].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[10].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[11].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[11].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[12].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[12].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[13].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[13].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[14].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[14].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[15].Ctrl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aWp[15].Value.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Mdscr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apda.Low.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apda.High.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apdb.Low.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apdb.High.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apga.Low.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apga.High.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apia.Low.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apia.High.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apib.Low.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Apib.High.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Afsr0.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Afsr1.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Amair.u64),
    SSMFIELD_ENTRY(         CPUMCTX, CntKCtl.u64),
    SSMFIELD_ENTRY(         CPUMCTX, ContextIdr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Cpacr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Csselr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Esr.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Far.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Mair.u64),
    SSMFIELD_ENTRY(         CPUMCTX, Par.u64),
    SSMFIELD_ENTRY(         CPUMCTX, TpIdrRoEl0.u64),
    SSMFIELD_ENTRY(         CPUMCTX, aTpIdr[0].u64),
    SSMFIELD_ENTRY(         CPUMCTX, aTpIdr[1].u64),
    SSMFIELD_ENTRY(         CPUMCTX, MDccInt.u64),
    SSMFIELD_ENTRY(         CPUMCTX, fpcr),
    SSMFIELD_ENTRY(         CPUMCTX, fpsr),
    SSMFIELD_ENTRY(         CPUMCTX, fPState),
    SSMFIELD_ENTRY(         CPUMCTX, fOsLck),
    SSMFIELD_ENTRY(         CPUMCTX, CntvCtlEl0),
    SSMFIELD_ENTRY(         CPUMCTX, CntvCValEl0),
    /** @name EL2 support:
     * @{ */
    SSMFIELD_ENTRY(         CPUMCTX, CntHCtlEl2),
    SSMFIELD_ENTRY(         CPUMCTX, CntHpCtlEl2),
    SSMFIELD_ENTRY(         CPUMCTX, CntHpCValEl2),
    SSMFIELD_ENTRY(         CPUMCTX, CntHpTValEl2),
    SSMFIELD_ENTRY(         CPUMCTX, CntVOffEl2),
    SSMFIELD_ENTRY(         CPUMCTX, CptrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, ElrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, EsrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, FarEl2),
    SSMFIELD_ENTRY(         CPUMCTX, HcrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, HpFarEl2),
    SSMFIELD_ENTRY(         CPUMCTX, MairEl2),
    SSMFIELD_ENTRY(         CPUMCTX, MdcrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, SctlrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, SpsrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, SpEl2),
    SSMFIELD_ENTRY(         CPUMCTX, TcrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, TpidrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, Ttbr0El2),
    SSMFIELD_ENTRY(         CPUMCTX, Ttbr1El2),
    SSMFIELD_ENTRY(         CPUMCTX, VBarEl2),
    SSMFIELD_ENTRY(         CPUMCTX, VMpidrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, VPidrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, VTcrEl2),
    SSMFIELD_ENTRY(         CPUMCTX, VTtbrEl2),
    /** @} */

    SSMFIELD_ENTRY_TERM()
};


/**
 * Initializes the guest system register states.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
static int cpumR3InitSysRegs(PVM pVM)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aSysRegRanges); i++)
    {
        int rc = CPUMR3SysRegRangesInsert(pVM, &g_aSysRegRanges[i]);
        AssertLogRelRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}


/**
 * Initializes the CPUM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(int) CPUMR3Init(PVM pVM)
{
    LogFlow(("CPUMR3Init\n"));

    /*
     * Assert alignment, sizes and tables.
     */
    AssertCompileMemberAlignment(VM, cpum.s, 32);
    AssertCompile(sizeof(pVM->cpum.s) <= sizeof(pVM->cpum.padding));
    AssertCompileSizeAlignment(CPUMCTX, 64);
    AssertCompileMemberAlignment(VM, cpum, 64);
    AssertCompileMemberAlignment(VMCPU, cpum.s, 64);
#ifdef VBOX_STRICT
    int rc2 = cpumR3SysRegStrictInitChecks();
    AssertRCReturn(rc2, rc2);
#endif

    pVM->cpum.s.GuestInfo.paSysRegRangesR3 = &pVM->cpum.s.GuestInfo.aSysRegRanges[0];
    pVM->cpum.s.bResetEl                   = ARMV8_AARCH64_EL_1;

    PCFGMNODE   pCpumCfg = CFGMR3GetChild(CFGMR3GetRoot(pVM), "CPUM");

    /** @cfgm{/CPUM/ResetPcValue, string}
     * Program counter value after a reset, sets the address of the first instruction to execute. */
    int rc = CFGMR3QueryU64Def(pCpumCfg, "ResetPcValue", &pVM->cpum.s.u64ResetPc, 0);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/NestedHWVirt, bool, false}
     * Whether to expose the hardware virtualization (EL2) feature to the guest.
     * The default is false, and when enabled requires a 64-bit CPU and a NEM backend
     * supporting it.
     */
    bool fNestedHWVirt = false;
    rc = CFGMR3QueryBoolDef(pCpumCfg, "NestedHWVirt", &fNestedHWVirt, false);
    AssertLogRelRCReturn(rc, rc);
    if (fNestedHWVirt)
        pVM->cpum.s.bResetEl = ARMV8_AARCH64_EL_2;

    /*
     * Register saved state data item.
     */
    rc = SSMR3RegisterInternal(pVM, "cpum", 1, CPUM_SAVED_STATE_VERSION, sizeof(CPUM),
                               NULL, cpumR3LiveExec, NULL,
                               NULL, cpumR3SaveExec, NULL,
                               cpumR3LoadPrep, cpumR3LoadExec, cpumR3LoadDone);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register info handlers and registers with the debugger facility.
     */
    DBGFR3InfoRegisterInternalEx(pVM, "cpum",             "Displays the all the cpu states.",
                                 &cpumR3InfoAll, DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "cpumguest",        "Displays the guest cpu state.",
                                 &cpumR3InfoGuest, DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternalEx(pVM, "cpumguestinstr",   "Displays the current guest instruction.",
                                 &cpumR3InfoGuestInstr, DBGFINFO_FLAGS_ALL_EMTS);
    DBGFR3InfoRegisterInternal(  pVM, "cpuid",            "Displays the guest cpuid information.",
                                 &cpumR3CpuIdInfo);
    DBGFR3InfoRegisterInternal(  pVM, "cpufeat",          "Displays the guest features.",
                                 &cpumR3CpuFeatInfo);

    rc = cpumR3DbgInit(pVM);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Initialize the Guest system register states.
     */
    rc = cpumR3InitSysRegs(pVM);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Initialize the general guest CPU state.
     */
    CPUMR3Reset(pVM);

    return VINF_SUCCESS;
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * The CPUM will update the addresses used by the switcher.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3DECL(void) CPUMR3Relocate(PVM pVM)
{
    RT_NOREF(pVM);
}


/**
 * Terminates the CPUM.
 *
 * Termination means cleaning up and freeing all resources,
 * the VM it self is at this point powered off or suspended.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(int) CPUMR3Term(PVM pVM)
{
    RT_NOREF(pVM);
    return VINF_SUCCESS;
}


/**
 * Resets a virtual CPU.
 *
 * Used by CPUMR3Reset and CPU hot plugging.
 *
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure of the CPU that is
 *                  being reset.  This may differ from the current EMT.
 */
VMMR3DECL(void) CPUMR3ResetCpu(PVM pVM, PVMCPU pVCpu)
{
    RT_NOREF(pVM);

    /** @todo anything different for VCPU > 0? */
    PCPUMCTX pCtx = &pVCpu->cpum.s.Guest;

    /*
     * Initialize everything to ZERO first.
     */
    RT_BZERO(pCtx, sizeof(*pCtx));

    /* Start in Supervisor mode. */
    /** @todo Differentiate between Aarch64 and Aarch32 configuation. */
    pCtx->fPState =   ARMV8_SPSR_EL2_AARCH64_SET_EL(pVM->cpum.s.bResetEl)
                    | ARMV8_SPSR_EL2_AARCH64_SP
                    | ARMV8_SPSR_EL2_AARCH64_D
                    | ARMV8_SPSR_EL2_AARCH64_A
                    | ARMV8_SPSR_EL2_AARCH64_I
                    | ARMV8_SPSR_EL2_AARCH64_F;

    pCtx->Pc.u64 = pVM->cpum.s.u64ResetPc;
    /** @todo */
}


/**
 * Resets the CPU.
 *
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(void) CPUMR3Reset(PVM pVM)
{
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        CPUMR3ResetCpu(pVM, pVCpu);
    }
}




/**
 * Pass 0 live exec callback.
 *
 * @returns VINF_SSM_DONT_CALL_AGAIN.
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 * @param   uPass               The pass (0).
 */
static DECLCALLBACK(int) cpumR3LiveExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uPass)
{
    AssertReturn(uPass == 0, VERR_SSM_UNEXPECTED_PASS);
    cpumR3SaveCpuId(pVM, pSSM);
    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * Execute state save operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) cpumR3SaveExec(PVM pVM, PSSMHANDLE pSSM)
{
    /*
     * Save.
     */
    SSMR3PutU32(pSSM, pVM->cCpus);
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU const   pVCpu   = pVM->apCpusR3[idCpu];
        PCPUMCTX const pGstCtx = &pVCpu->cpum.s.Guest;

        SSMR3PutStructEx(pSSM, pGstCtx, sizeof(*pGstCtx), 0, g_aCpumCtxFields, NULL);

        SSMR3PutU32(pSSM, pVCpu->cpum.s.fChanged);
    }

    cpumR3SaveCpuId(pVM, pSSM);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMINTLOADPREP}
 */
static DECLCALLBACK(int) cpumR3LoadPrep(PVM pVM, PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);
    pVM->cpum.s.fPendingRestore = true;
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMINTLOADEXEC}
 */
static DECLCALLBACK(int) cpumR3LoadExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    /*
     * Validate version.
     */
    if (uVersion != CPUM_SAVED_STATE_VERSION)
    {
        AssertMsgFailed(("cpumR3LoadExec: Invalid version uVersion=%d!\n", uVersion));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    if (uPass == SSM_PASS_FINAL)
    {
        uint32_t cCpus;
        int rc = SSMR3GetU32(pSSM, &cCpus); AssertRCReturn(rc, rc);
        AssertLogRelMsgReturn(cCpus == pVM->cCpus, ("Mismatching CPU counts: saved: %u; configured: %u \n", cCpus, pVM->cCpus),
                              VERR_SSM_UNEXPECTED_DATA);

        /*
         * Do the per-CPU restoring.
         */
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PVMCPU   pVCpu   = pVM->apCpusR3[idCpu];
            PCPUMCTX pGstCtx = &pVCpu->cpum.s.Guest;

            /*
             * Restore the CPUMCTX structure.
             */
            rc = SSMR3GetStructEx(pSSM, pGstCtx, sizeof(*pGstCtx), 0, g_aCpumCtxFields, NULL);
            AssertRCReturn(rc, rc);

            /*
             * Restore a couple of flags.
             */
            SSMR3GetU32(pSSM, &pVCpu->cpum.s.fChanged);
        }
    }

    pVM->cpum.s.fPendingRestore = false;

    /* Load CPUID and explode guest features. */
    return cpumR3LoadCpuId(pVM, pSSM, uVersion);
}


/**
 * @callback_method_impl{FNSSMINTLOADDONE}
 */
static DECLCALLBACK(int) cpumR3LoadDone(PVM pVM, PSSMHANDLE pSSM)
{
    if (RT_FAILURE(SSMR3HandleGetStatus(pSSM)))
        return VINF_SUCCESS;

    /* just check this since we can. */ /** @todo Add a SSM unit flag for indicating that it's mandatory during a restore.  */
    if (pVM->cpum.s.fPendingRestore)
    {
        LogRel(("CPUM: Missing state!\n"));
        return VERR_INTERNAL_ERROR_2;
    }

    /** @todo */
    return VINF_SUCCESS;
}


/**
 * Checks if the CPUM state restore is still pending.
 *
 * @returns true / false.
 * @param   pVM                 The cross context VM structure.
 */
VMMDECL(bool) CPUMR3IsStateRestorePending(PVM pVM)
{
    return pVM->cpum.s.fPendingRestore;
}


/**
 * Formats the PSTATE value into mnemonics.
 *
 * @param   pszPState   Where to write the mnemonics. (Assumes sufficient buffer space.)
 * @param   fPState     The PSTATE value with both guest hardware and VBox
 *                      internal bits included.
 */
static void cpumR3InfoFormatPState(char *pszPState, uint32_t fPState)
{
    /*
     * Format the flags.
     */
    static const struct
    {
        const char *pszSet; const char *pszClear; uint32_t fFlag;
    }   s_aFlags[] =
    {
        { "SP", "nSP", ARMV8_SPSR_EL2_AARCH64_SP },
        { "M4", "nM4", ARMV8_SPSR_EL2_AARCH64_M4 },
        { "T",  "nT",  ARMV8_SPSR_EL2_AARCH64_T  },
        { "nF", "F",   ARMV8_SPSR_EL2_AARCH64_F  },
        { "nI", "I",   ARMV8_SPSR_EL2_AARCH64_I  },
        { "nA", "A",   ARMV8_SPSR_EL2_AARCH64_A  },
        { "nD", "D",   ARMV8_SPSR_EL2_AARCH64_D  },
        { "V",  "nV",  ARMV8_SPSR_EL2_AARCH64_V  },
        { "C",  "nC",  ARMV8_SPSR_EL2_AARCH64_C  },
        { "Z",  "nZ",  ARMV8_SPSR_EL2_AARCH64_Z  },
        { "N",  "nN",  ARMV8_SPSR_EL2_AARCH64_N  },
    };
    char *psz = pszPState;
    for (unsigned i = 0; i < RT_ELEMENTS(s_aFlags); i++)
    {
        const char *pszAdd = s_aFlags[i].fFlag & fPState ? s_aFlags[i].pszSet : s_aFlags[i].pszClear;
        if (pszAdd)
        {
            strcpy(psz, pszAdd);
            psz += strlen(pszAdd);
            *psz++ = ' ';
        }
    }
    psz[-1] = '\0';
}


/**
 * Formats a full register dump.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pCtx        The context to format.
 * @param   pHlp        Output functions.
 * @param   enmType     The dump type.
 */
static void cpumR3InfoOne(PVM pVM, PCPUMCTX pCtx, PCDBGFINFOHLP pHlp, CPUMDUMPTYPE enmType)
{
    RT_NOREF(pVM);

    /*
     * Format the PSTATE.
     */
    char szPState[80];
    cpumR3InfoFormatPState(&szPState[0], pCtx->fPState);

    /*
     * Format the registers.
     */
    switch (enmType)
    {
        case CPUMDUMPTYPE_TERSE:
            if (CPUMIsGuestIn64BitCodeEx(pCtx))
                pHlp->pfnPrintf(pHlp,
                    "x0=%016RX64 x1=%016RX64 x2=%016RX64 x3=%016RX64\n"
                    "x4=%016RX64 x5=%016RX64 x6=%016RX64 x7=%016RX64\n"
                    "x8=%016RX64 x9=%016RX64 x10=%016RX64 x11=%016RX64\n"
                    "x12=%016RX64 x13=%016RX64 x14=%016RX64 x15=%016RX64\n"
                    "x16=%016RX64 x17=%016RX64 x18=%016RX64 x19=%016RX64\n"
                    "x20=%016RX64 x21=%016RX64 x22=%016RX64 x23=%016RX64\n"
                    "x24=%016RX64 x25=%016RX64 x26=%016RX64 x27=%016RX64\n"
                    "x28=%016RX64 x29=%016RX64 x30=%016RX64\n"
                    "pc=%016RX64 pstate=%016RX64 %s\n"
                    "sp_el0=%016RX64 sp_el1=%016RX64\n",
                    pCtx->aGRegs[0],  pCtx->aGRegs[1],  pCtx->aGRegs[2],  pCtx->aGRegs[3],
                    pCtx->aGRegs[4],  pCtx->aGRegs[5],  pCtx->aGRegs[6],  pCtx->aGRegs[7],
                    pCtx->aGRegs[8],  pCtx->aGRegs[9],  pCtx->aGRegs[10], pCtx->aGRegs[11],
                    pCtx->aGRegs[12], pCtx->aGRegs[13], pCtx->aGRegs[14], pCtx->aGRegs[15],
                    pCtx->aGRegs[16], pCtx->aGRegs[17], pCtx->aGRegs[18], pCtx->aGRegs[19],
                    pCtx->aGRegs[20], pCtx->aGRegs[21], pCtx->aGRegs[22], pCtx->aGRegs[23],
                    pCtx->aGRegs[24], pCtx->aGRegs[25], pCtx->aGRegs[26], pCtx->aGRegs[27],
                    pCtx->aGRegs[28], pCtx->aGRegs[29], pCtx->aGRegs[30],
                    pCtx->Pc.u64, pCtx->fPState, szPState,
                    pCtx->aSpReg[0].u64, pCtx->aSpReg[1].u64);
            else
                AssertFailed();
            break;

        case CPUMDUMPTYPE_DEFAULT:
            if (CPUMIsGuestIn64BitCodeEx(pCtx))
                pHlp->pfnPrintf(pHlp,
                    "x0=%016RX64 x1=%016RX64 x2=%016RX64 x3=%016RX64\n"
                    "x4=%016RX64 x5=%016RX64 x6=%016RX64 x7=%016RX64\n"
                    "x8=%016RX64 x9=%016RX64 x10=%016RX64 x11=%016RX64\n"
                    "x12=%016RX64 x13=%016RX64 x14=%016RX64 x15=%016RX64\n"
                    "x16=%016RX64 x17=%016RX64 x18=%016RX64 x19=%016RX64\n"
                    "x20=%016RX64 x21=%016RX64 x22=%016RX64 x23=%016RX64\n"
                    "x24=%016RX64 x25=%016RX64 x26=%016RX64 x27=%016RX64\n"
                    "x28=%016RX64 x29=%016RX64 x30=%016RX64\n"
                    "pc=%016RX64 pstate=%016RX64 %s\n"
                    "sp_el0=%016RX64 sp_el1=%016RX64 sctlr_el1=%016RX64\n"
                    "tcr_el1=%016RX64 ttbr0_el1=%016RX64 ttbr1_el1=%016RX64\n"
                    "vbar_el1=%016RX64 elr_el1=%016RX64 esr_el1=%016RX64\n",
                    pCtx->aGRegs[0],  pCtx->aGRegs[1],  pCtx->aGRegs[2],  pCtx->aGRegs[3],
                    pCtx->aGRegs[4],  pCtx->aGRegs[5],  pCtx->aGRegs[6],  pCtx->aGRegs[7],
                    pCtx->aGRegs[8],  pCtx->aGRegs[9],  pCtx->aGRegs[10], pCtx->aGRegs[11],
                    pCtx->aGRegs[12], pCtx->aGRegs[13], pCtx->aGRegs[14], pCtx->aGRegs[15],
                    pCtx->aGRegs[16], pCtx->aGRegs[17], pCtx->aGRegs[18], pCtx->aGRegs[19],
                    pCtx->aGRegs[20], pCtx->aGRegs[21], pCtx->aGRegs[22], pCtx->aGRegs[23],
                    pCtx->aGRegs[24], pCtx->aGRegs[25], pCtx->aGRegs[26], pCtx->aGRegs[27],
                    pCtx->aGRegs[28], pCtx->aGRegs[29], pCtx->aGRegs[30],
                    pCtx->Pc.u64, pCtx->fPState, szPState,
                    pCtx->aSpReg[0].u64, pCtx->aSpReg[1].u64, pCtx->Sctlr.u64,
                    pCtx->Tcr.u64, pCtx->Ttbr0.u64, pCtx->Ttbr1.u64,
                    pCtx->VBar.u64, pCtx->Elr.u64, pCtx->Esr.u64);
            else
                AssertFailed();
            break;

        case CPUMDUMPTYPE_VERBOSE:
            if (CPUMIsGuestIn64BitCodeEx(pCtx))
                pHlp->pfnPrintf(pHlp,
                    "x0=%016RX64 x1=%016RX64 x2=%016RX64 x3=%016RX64\n"
                    "x4=%016RX64 x5=%016RX64 x6=%016RX64 x7=%016RX64\n"
                    "x8=%016RX64 x9=%016RX64 x10=%016RX64 x11=%016RX64\n"
                    "x12=%016RX64 x13=%016RX64 x14=%016RX64 x15=%016RX64\n"
                    "x16=%016RX64 x17=%016RX64 x18=%016RX64 x19=%016RX64\n"
                    "x20=%016RX64 x21=%016RX64 x22=%016RX64 x23=%016RX64\n"
                    "x24=%016RX64 x25=%016RX64 x26=%016RX64 x27=%016RX64\n"
                    "x28=%016RX64 x29=%016RX64 x30=%016RX64\n"
                    "pc=%016RX64 pstate=%016RX64 %s\n"
                    "sp_el0=%016RX64 sp_el1=%016RX64 sctlr_el1=%016RX64\n"
                    "tcr_el1=%016RX64 ttbr0_el1=%016RX64 ttbr1_el1=%016RX64\n"
                    "vbar_el1=%016RX64 elr_el1=%016RX64 esr_el1=%016RX64\n"
                    "contextidr_el1=%016RX64 tpidrr0_el0=%016RX64\n"
                    "tpidr_el0=%016RX64 tpidr_el1=%016RX64\n"
                    "far_el1=%016RX64 mair_el1=%016RX64 par_el1=%016RX64\n"
                    "cntv_ctl_el0=%016RX64 cntv_val_el0=%016RX64\n"
                    "afsr0_el1=%016RX64 afsr0_el1=%016RX64 amair_el1=%016RX64\n"
                    "cntkctl_el1=%016RX64 cpacr_el1=%016RX64 csselr_el1=%016RX64\n"
                    "mdccint_el1=%016RX64\n",
                    pCtx->aGRegs[0],  pCtx->aGRegs[1],  pCtx->aGRegs[2],  pCtx->aGRegs[3],
                    pCtx->aGRegs[4],  pCtx->aGRegs[5],  pCtx->aGRegs[6],  pCtx->aGRegs[7],
                    pCtx->aGRegs[8],  pCtx->aGRegs[9],  pCtx->aGRegs[10], pCtx->aGRegs[11],
                    pCtx->aGRegs[12], pCtx->aGRegs[13], pCtx->aGRegs[14], pCtx->aGRegs[15],
                    pCtx->aGRegs[16], pCtx->aGRegs[17], pCtx->aGRegs[18], pCtx->aGRegs[19],
                    pCtx->aGRegs[20], pCtx->aGRegs[21], pCtx->aGRegs[22], pCtx->aGRegs[23],
                    pCtx->aGRegs[24], pCtx->aGRegs[25], pCtx->aGRegs[26], pCtx->aGRegs[27],
                    pCtx->aGRegs[28], pCtx->aGRegs[29], pCtx->aGRegs[30],
                    pCtx->Pc.u64, pCtx->fPState, szPState,
                    pCtx->aSpReg[0].u64, pCtx->aSpReg[1].u64, pCtx->Sctlr.u64,
                    pCtx->Tcr.u64, pCtx->Ttbr0.u64, pCtx->Ttbr1.u64,
                    pCtx->VBar.u64, pCtx->Elr.u64, pCtx->Esr.u64,
                    pCtx->ContextIdr.u64, pCtx->TpIdrRoEl0.u64,
                    pCtx->aTpIdr[0].u64, pCtx->aTpIdr[1].u64,
                    pCtx->Far.u64, pCtx->Mair.u64, pCtx->Par.u64,
                    pCtx->CntvCtlEl0, pCtx->CntvCValEl0,
                    pCtx->Afsr0.u64, pCtx->Afsr1.u64, pCtx->Amair.u64,
                    pCtx->CntKCtl.u64, pCtx->Cpacr.u64, pCtx->Csselr.u64,
                    pCtx->MDccInt.u64);
            else
                AssertFailed();

            pHlp->pfnPrintf(pHlp, "fpcr=%016RX64 fpsr=%016RX64\n", pCtx->fpcr, pCtx->fpsr);
            for (unsigned i = 0; i < RT_ELEMENTS(pCtx->aVRegs); i++)
                pHlp->pfnPrintf(pHlp,
                                i & 1
                                ? "q%u%s=%08RX32'%08RX32'%08RX32'%08RX32\n"
                                : "q%u%s=%08RX32'%08RX32'%08RX32'%08RX32  ",
                                i, i < 10 ? " " : "",
                                pCtx->aVRegs[i].au32[3],
                                pCtx->aVRegs[i].au32[2],
                                pCtx->aVRegs[i].au32[1],
                                pCtx->aVRegs[i].au32[0]);

            pHlp->pfnPrintf(pHlp, "mdscr_el1=%016RX64\n", pCtx->Mdscr.u64);
            for (unsigned i = 0; i < RT_ELEMENTS(pCtx->aBp); i++)
                pHlp->pfnPrintf(pHlp, "DbgBp%u%s: Control=%016RX64 Value=%016RX64\n",
                                i, i < 10 ? " " : "",
                                pCtx->aBp[i].Ctrl, pCtx->aBp[i].Value);

            for (unsigned i = 0; i < RT_ELEMENTS(pCtx->aWp); i++)
                pHlp->pfnPrintf(pHlp, "DbgWp%u%s: Control=%016RX64 Value=%016RX64\n",
                                i, i < 10 ? " " : "",
                                pCtx->aWp[i].Ctrl, pCtx->aWp[i].Value);

            pHlp->pfnPrintf(pHlp, "APDAKey=%016RX64'%016RX64\n", pCtx->Apda.High.u64, pCtx->Apda.Low.u64);
            pHlp->pfnPrintf(pHlp, "APDBKey=%016RX64'%016RX64\n", pCtx->Apdb.High.u64, pCtx->Apdb.Low.u64);
            pHlp->pfnPrintf(pHlp, "APGAKey=%016RX64'%016RX64\n", pCtx->Apga.High.u64, pCtx->Apga.Low.u64);
            pHlp->pfnPrintf(pHlp, "APIAKey=%016RX64'%016RX64\n", pCtx->Apia.High.u64, pCtx->Apia.Low.u64);
            pHlp->pfnPrintf(pHlp, "APIBKey=%016RX64'%016RX64\n", pCtx->Apib.High.u64, pCtx->Apib.Low.u64);

            break;
    }
}


/**
 * Display all cpu states and any other cpum info.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) cpumR3InfoAll(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    cpumR3InfoGuest(pVM, pHlp, pszArgs);
    cpumR3InfoGuestInstr(pVM, pHlp, pszArgs);
}


/**
 * Parses the info argument.
 *
 * The argument starts with 'verbose', 'terse' or 'default' and then
 * continues with the comment string.
 *
 * @param   pszArgs         The pointer to the argument string.
 * @param   penmType        Where to store the dump type request.
 * @param   ppszComment     Where to store the pointer to the comment string.
 */
static void cpumR3InfoParseArg(const char *pszArgs, CPUMDUMPTYPE *penmType, const char **ppszComment)
{
    if (!pszArgs)
    {
        *penmType = CPUMDUMPTYPE_DEFAULT;
        *ppszComment = "";
    }
    else
    {
        if (!strncmp(pszArgs, RT_STR_TUPLE("verbose")))
        {
            pszArgs += 7;
            *penmType = CPUMDUMPTYPE_VERBOSE;
        }
        else if (!strncmp(pszArgs, RT_STR_TUPLE("terse")))
        {
            pszArgs += 5;
            *penmType = CPUMDUMPTYPE_TERSE;
        }
        else if (!strncmp(pszArgs, RT_STR_TUPLE("default")))
        {
            pszArgs += 7;
            *penmType = CPUMDUMPTYPE_DEFAULT;
        }
        else
            *penmType = CPUMDUMPTYPE_DEFAULT;
        *ppszComment = RTStrStripL(pszArgs);
    }
}


/**
 * Display the guest cpu state.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments.
 */
static DECLCALLBACK(void) cpumR3InfoGuest(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    CPUMDUMPTYPE enmType;
    const char *pszComment;
    cpumR3InfoParseArg(pszArgs, &enmType, &pszComment);

    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    pHlp->pfnPrintf(pHlp, "Guest CPUM (VCPU %d) state: %s\n", pVCpu->idCpu, pszComment);

    PCPUMCTX pCtx = &pVCpu->cpum.s.Guest;
    cpumR3InfoOne(pVM, pCtx, pHlp, enmType);
}


/**
 * Display the current guest instruction
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) cpumR3InfoGuestInstr(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);

    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    char szInstruction[256];
    szInstruction[0] = '\0';
    DBGFR3DisasInstrCurrent(pVCpu, szInstruction, sizeof(szInstruction));
    pHlp->pfnPrintf(pHlp, "\nCPUM%u: %s\n\n", pVCpu->idCpu, szInstruction);
}


/**
 * Called when the ring-3 init phase completes.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   enmWhat             Which init phase.
 */
VMMR3DECL(int) CPUMR3InitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    RT_NOREF(pVM, enmWhat);
    return VINF_SUCCESS;
}


/**
 * Called when the ring-0 init phases completed.
 *
 * @param   pVM                 The cross context VM structure.
 */
VMMR3DECL(void) CPUMR3LogCpuIdAndMsrFeatures(PVM pVM)
{
    /*
     * Enable log buffering as we're going to log a lot of lines.
     */
    bool const fOldBuffered = RTLogRelSetBuffering(true /*fBuffered*/);

    /*
     * Log the cpuid.
     */
    RTCPUSET OnlineSet;
    LogRel(("CPUM: Logical host processors: %u present, %u max, %u online, online mask: %016RX64\n",
                (unsigned)RTMpGetPresentCount(), (unsigned)RTMpGetCount(), (unsigned)RTMpGetOnlineCount(),
                RTCpuSetToU64(RTMpGetOnlineSet(&OnlineSet)) ));
    RTCPUID cCores = RTMpGetCoreCount();
    if (cCores)
        LogRel(("CPUM: Physical host cores: %u\n", (unsigned)cCores));
    LogRel(("************************* CPUID dump ************************\n"));
    DBGFR3Info(pVM->pUVM, "cpuid", "verbose", DBGFR3InfoLogRelHlp());
    LogRel(("\n"));
    DBGFR3_INFO_LOG_SAFE(pVM, "cpuid", "verbose"); /* macro */
    LogRel(("******************** End of CPUID dump **********************\n"));

    LogRel(("******************** CPU feature dump ***********************\n"));
    DBGFR3Info(pVM->pUVM, "cpufeat", "verbose", DBGFR3InfoLogRelHlp());
    LogRel(("\n"));
    DBGFR3_INFO_LOG_SAFE(pVM, "cpufeat", "verbose"); /* macro */
    LogRel(("***************** End of CPU feature dump *******************\n"));

    /*
     * Restore the log buffering state to what it was previously.
     */
    RTLogRelSetBuffering(fOldBuffered);
}


/**
 * Marks the guest debug state as active.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 *
 * @note This is used solely by NEM (hence the name) to set the correct flags here
 *       without loading the host's DRx registers, which is not possible from ring-3 anyway.
 *       The specific NEM backends have to make sure to load the correct values.
 */
VMMR3_INT_DECL(void) CPUMR3NemActivateGuestDebugState(PVMCPUCC pVCpu)
{
    ASMAtomicAndU32(&pVCpu->cpum.s.fUseFlags, ~CPUM_USED_DEBUG_REGS_HYPER);
    ASMAtomicOrU32(&pVCpu->cpum.s.fUseFlags, CPUM_USED_DEBUG_REGS_GUEST);
}


/**
 * Marks the hyper debug state as active.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 *
 * @note This is used solely by NEM (hence the name) to set the correct flags here
 *       without loading the host's debug registers, which is not possible from ring-3 anyway.
 *       The specific NEM backends have to make sure to load the correct values.
 */
VMMR3_INT_DECL(void) CPUMR3NemActivateHyperDebugState(PVMCPUCC pVCpu)
{
    ASMAtomicAndU32(&pVCpu->cpum.s.fUseFlags, ~CPUM_USED_DEBUG_REGS_GUEST);
    ASMAtomicOrU32(&pVCpu->cpum.s.fUseFlags, CPUM_USED_DEBUG_REGS_HYPER);
}
