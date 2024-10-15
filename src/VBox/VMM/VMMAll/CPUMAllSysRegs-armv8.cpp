/* $Id: CPUMAllSysRegs-armv8.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPUM - ARMv8 CPU System Registers.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_CPUM
#include <VBox/vmm/cpum.h>
#include "CPUMInternal-armv8.h"
#include <VBox/vmm/gic.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/err.h>

#include <iprt/armv8.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/**
 * Validates the CPUMSYSREGRANGE::offCpumCpu value and declares a local variable
 * pointing to it.
 *
 * ASSUMES sizeof(a_Type) is a power of two and that the member is aligned
 * correctly.
 */
#define CPUM_SYSREG_ASSERT_CPUMCPU_OFFSET_RETURN(a_pVCpu, a_pRange, a_Type, a_VarName) \
    AssertMsgReturn(   (a_pRange)->offCpumCpu >= 8 \
                    && (a_pRange)->offCpumCpu < sizeof(CPUMCPU) \
                    && !((a_pRange)->offCpumCpu & (RT_MIN(sizeof(a_Type), 8) - 1)) \
                    , ("offCpumCpu=%#x %s\n", (a_pRange)->offCpumCpu, (a_pRange)->szName), \
                    VERR_CPUM_MSR_BAD_CPUMCPU_OFFSET); \
    a_Type *a_VarName = (a_Type *)((uintptr_t)&(a_pVCpu)->cpum.s + (a_pRange)->offCpumCpu)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Implements reading one or more system registers.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_CPUM_R3_MSR_READ if the MSR read could not be serviced in the
 *          current context (raw-mode or ring-0).
 * @retval  VERR_CPUM_RAISE_GP_0 on failure (invalid system register).
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idSysReg    The system register we're reading.
 * @param   pRange      The system register range descriptor.
 * @param   puValue     Where to return the value.
 */
typedef DECLCALLBACKTYPE(VBOXSTRICTRC, FNCPUMRDSYSREG,(PVMCPUCC pVCpu, uint32_t idSysReg, PCCPUMSYSREGRANGE pRange, uint64_t *puValue));
/** Pointer to a MRS worker for a specific system register or range of system registers.  */
typedef FNCPUMRDSYSREG *PFNCPUMRDSYSREG;


/**
 * Implements writing one or more system registers.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_CPUM_R3_MSR_WRITE if the MSR write could not be serviced in the
 *          current context (raw-mode or ring-0).
 * @retval  VERR_CPUM_RAISE_GP_0 on failure.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idSysReg    The system register we're writing.
 * @param   pRange      The system register range descriptor.
 * @param   uValue      The value to set, ignored bits masked.
 * @param   uRawValue   The raw value with the ignored bits not masked.
 */
typedef DECLCALLBACKTYPE(VBOXSTRICTRC, FNCPUMWRSYSREG,(PVMCPUCC pVCpu, uint32_t idSysReg, PCCPUMSYSREGRANGE pRange,
                                                       uint64_t uValue, uint64_t uRawValue));
/** Pointer to a MSR worker for a specific system register or range of system registers.  */
typedef FNCPUMWRSYSREG *PFNCPUMWRSYSREG;



/*
 * Generic functions.
 * Generic functions.
 * Generic functions.
 */


/** @callback_method_impl{FNCPUMRDSYSREG} */
static DECLCALLBACK(VBOXSTRICTRC) cpumSysRegRd_FixedValue(PVMCPUCC pVCpu, uint32_t idSysReg, PCCPUMSYSREGRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idSysReg);
    *puValue = pRange->uValue;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRSYSREG} */
static DECLCALLBACK(VBOXSTRICTRC) cpumSysRegWr_IgnoreWrite(PVMCPUCC pVCpu, uint32_t idSysReg, PCCPUMSYSREGRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idSysReg); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    Log(("CPUM: Ignoring MSR %#x (%s), %#llx\n", idSysReg, pRange->szName, uValue));
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMRDSYSREG} */
static DECLCALLBACK(VBOXSTRICTRC) cpumSysRegRd_WriteOnly(PVMCPUCC pVCpu, uint32_t idSysReg, PCCPUMSYSREGRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idSysReg); RT_NOREF_PV(pRange); RT_NOREF_PV(puValue);
    return VERR_CPUM_RAISE_GP_0;
}


/** @callback_method_impl{FNCPUMWRSYSREG} */
static DECLCALLBACK(VBOXSTRICTRC) cpumSysRegWr_ReadOnly(PVMCPUCC pVCpu, uint32_t idSysReg, PCCPUMSYSREGRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pVCpu); RT_NOREF_PV(idSysReg); RT_NOREF_PV(pRange); RT_NOREF_PV(uValue); RT_NOREF_PV(uRawValue);
    Assert(pRange->fWrExcpMask == UINT64_MAX);
    return VERR_CPUM_RAISE_GP_0;
}



/** @callback_method_impl{FNCPUMRDSYSREG} */
static DECLCALLBACK(VBOXSTRICTRC) cpumSysRegRd_GicV3Icc(PVMCPUCC pVCpu, uint32_t idSysReg, PCCPUMSYSREGRANGE pRange, uint64_t *puValue)
{
    RT_NOREF_PV(pRange);
    return GICReadSysReg(pVCpu, idSysReg, puValue);
}


/** @callback_method_impl{FNCPUMWRSYSREG} */
static DECLCALLBACK(VBOXSTRICTRC) cpumSysRegWr_GicV3Icc(PVMCPUCC pVCpu, uint32_t idSysReg, PCCPUMSYSREGRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF_PV(pRange); RT_NOREF_PV(uRawValue);
    return GICWriteSysReg(pVCpu, idSysReg, uValue);
}



/** @callback_method_impl{FNCPUMRDSYSREG} */
static DECLCALLBACK(VBOXSTRICTRC) cpumSysRegRd_OslsrEl1(PVMCPUCC pVCpu, uint32_t idSysReg, PCCPUMSYSREGRANGE pRange, uint64_t *puValue)
{
    RT_NOREF(idSysReg, pRange);
    *puValue = pVCpu->cpum.s.Guest.fOsLck ? ARMV8_OSLSR_EL1_AARCH64_OSLK : 0;
    return VINF_SUCCESS;
}


/** @callback_method_impl{FNCPUMWRSYSREG} */
static DECLCALLBACK(VBOXSTRICTRC) cpumSysRegWr_OslarEl1(PVMCPUCC pVCpu, uint32_t idSysReg, PCCPUMSYSREGRANGE pRange, uint64_t uValue, uint64_t uRawValue)
{
    RT_NOREF(idSysReg, pRange, uRawValue);
    Assert(!(uValue & ~ARMV8_OSLAR_EL1_AARCH64_OSLK));
    pVCpu->cpum.s.Guest.fOsLck = RT_BOOL(uValue);
    return VINF_SUCCESS;
}


/**
 * System register read function table.
 */
static const struct READSYSREGCLANG11WEIRDNOTHROW { PFNCPUMRDSYSREG pfnRdSysReg; } g_aCpumRdSysRegFns[kCpumSysRegRdFn_End] =
{
    { NULL }, /* Invalid */
    { cpumSysRegRd_FixedValue },
    { NULL }, /* Alias */
    { cpumSysRegRd_WriteOnly },
    { cpumSysRegRd_GicV3Icc  },
    { cpumSysRegRd_OslsrEl1  },
};


/**
 * System register write function table.
 */
static const struct WRITESYSREGCLANG11WEIRDNOTHROW { PFNCPUMWRSYSREG pfnWrSysReg; } g_aCpumWrSysRegFns[kCpumSysRegWrFn_End] =
{
    { NULL }, /* Invalid */
    { cpumSysRegWr_IgnoreWrite },
    { cpumSysRegWr_ReadOnly },
    { NULL }, /* Alias */
    { cpumSysRegWr_GicV3Icc },
    { cpumSysRegWr_OslarEl1 },
};


/**
 * Looks up the range for the given system register.
 *
 * @returns Pointer to the range if found, NULL if not.
 * @param   pVM                The cross context VM structure.
 * @param   idSysReg           The system register to look up.
 */
# ifndef IN_RING3
static
# endif
PCPUMSYSREGRANGE cpumLookupSysRegRange(PVM pVM, uint32_t idSysReg)
{
    /*
     * Binary lookup.
     */
    uint32_t         cRanges   = RT_MIN(pVM->cpum.s.GuestInfo.cSysRegRanges, RT_ELEMENTS(pVM->cpum.s.GuestInfo.aSysRegRanges));
    if (!cRanges)
        return NULL;
    PCPUMSYSREGRANGE paRanges  = pVM->cpum.s.GuestInfo.aSysRegRanges;
    for (;;)
    {
        uint32_t i = cRanges / 2;
        if (idSysReg < paRanges[i].uFirst)
        {
            if (i == 0)
                break;
            cRanges = i;
        }
        else if (idSysReg > paRanges[i].uLast)
        {
            i++;
            if (i >= cRanges)
                break;
            cRanges -= i;
            paRanges = &paRanges[i];
        }
        else
        {
            if (paRanges[i].enmRdFn == kCpumSysRegRdFn_Alias)
                return cpumLookupSysRegRange(pVM, paRanges[i].uValue);
            return &paRanges[i];
        }
    }

# ifdef VBOX_STRICT
    /*
     * Linear lookup to verify the above binary search.
     */
    uint32_t         cLeft = RT_MIN(pVM->cpum.s.GuestInfo.cSysRegRanges, RT_ELEMENTS(pVM->cpum.s.GuestInfo.aSysRegRanges));
    PCPUMSYSREGRANGE pCur  = pVM->cpum.s.GuestInfo.aSysRegRanges;
    while (cLeft-- > 0)
    {
        if (idSysReg >= pCur->uFirst && idSysReg <= pCur->uLast)
        {
            AssertFailed();
            if (pCur->enmRdFn == kCpumSysRegRdFn_Alias)
                return cpumLookupSysRegRange(pVM, pCur->uValue);
            return pCur;
        }
        pCur++;
    }
# endif
    return NULL;
}


/**
 * Query a guest system register.
 *
 * The caller is responsible for checking privilege if the call is the result of
 * a MRS instruction.  We'll do the rest.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_CPUM_R3_MSR_READ if the system register read could not be serviced in the
 *          current context (raw-mode or ring-0).
 * @retval  VERR_CPUM_RAISE_GP_0 on failure (invalid system register), the caller is
 *          expected to take the appropriate actions. @a *puValue is set to 0.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   idSysReg            The system register.
 * @param   puValue             Where to return the value.
 *
 * @remarks This will always return the right values, even when we're in the
 *          recompiler.
 */
VMMDECL(VBOXSTRICTRC) CPUMQueryGuestSysReg(PVMCPUCC pVCpu, uint32_t idSysReg, uint64_t *puValue)
{
    *puValue = 0;

    VBOXSTRICTRC        rcStrict;
    PVM                 pVM    = pVCpu->CTX_SUFF(pVM);
    PCPUMSYSREGRANGE    pRange = cpumLookupSysRegRange(pVM, idSysReg);
    if (pRange)
    {
        CPUMSYSREGRDFN  enmRdFn = (CPUMSYSREGRDFN)pRange->enmRdFn;
        AssertReturn(enmRdFn > kCpumSysRegRdFn_Invalid && enmRdFn < kCpumSysRegRdFn_End, VERR_CPUM_IPE_1);

        PFNCPUMRDSYSREG pfnRdSysReg = g_aCpumRdSysRegFns[enmRdFn].pfnRdSysReg;
        AssertReturn(pfnRdSysReg, VERR_CPUM_IPE_2);

        STAM_COUNTER_INC(&pRange->cReads);
        STAM_REL_COUNTER_INC(&pVM->cpum.s.cSysRegReads);

        rcStrict = pfnRdSysReg(pVCpu, idSysReg, pRange, puValue);
        if (rcStrict == VINF_SUCCESS)
            Log2(("CPUM: MRS %#x (%s) -> %#llx\n", idSysReg, pRange->szName, *puValue));
        else if (rcStrict == VERR_CPUM_RAISE_GP_0)
        {
            Log(("CPUM: MRS %#x (%s) -> #GP(0)\n", idSysReg, pRange->szName));
            STAM_COUNTER_INC(&pRange->cExcp);
            STAM_REL_COUNTER_INC(&pVM->cpum.s.cSysRegReadsRaiseExcp);
        }
#ifndef IN_RING3
        else if (rcStrict == VINF_CPUM_R3_MSR_READ)
            Log(("CPUM: MRS %#x (%s) -> ring-3\n", idSysReg, pRange->szName));
#endif
        else
        {
            Log(("CPUM: MRS %#x (%s) -> rcStrict=%Rrc\n", idSysReg, pRange->szName, VBOXSTRICTRC_VAL(rcStrict)));
            AssertMsgStmt(RT_FAILURE_NP(rcStrict), ("%Rrc idSysReg=%#x\n", VBOXSTRICTRC_VAL(rcStrict), idSysReg),
                          rcStrict = VERR_IPE_UNEXPECTED_INFO_STATUS);
            Assert(rcStrict != VERR_EM_INTERPRETER);
        }
    }
    else
    {
        Log(("CPUM: Unknown MRS %#x -> Ignore\n", idSysReg));
        STAM_REL_COUNTER_INC(&pVM->cpum.s.cSysRegReads);
        STAM_REL_COUNTER_INC(&pVM->cpum.s.cSysRegReadsUnknown);
        *puValue = 0;
        rcStrict = VINF_SUCCESS;
    }
    return rcStrict;
}


/**
 * Writes to a guest system register.
 *
 * The caller is responsible for checking privilege if the call is the result of
 * a MSR instruction.  We'll do the rest.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_CPUM_R3_MSR_WRITE if the system register write could not be serviced in the
 *          current context (raw-mode or ring-0).
 * @retval  VERR_CPUM_RAISE_GP_0 on failure, the caller is expected to take the
 *          appropriate actions.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idSysReg    The system register id.
 * @param   uValue      The value to set.
 *
 * @remarks Everyone changing system register values, shall do it
 *          by calling this method.  This makes sure we have current values and
 *          that we trigger all the right actions when something changes.
 */
VMMDECL(VBOXSTRICTRC) CPUMSetGuestSysReg(PVMCPUCC pVCpu, uint32_t idSysReg, uint64_t uValue)
{
    VBOXSTRICTRC        rcStrict;
    PVM                 pVM    = pVCpu->CTX_SUFF(pVM);
    PCPUMSYSREGRANGE    pRange = cpumLookupSysRegRange(pVM, idSysReg);
    if (pRange)
    {
        STAM_COUNTER_INC(&pRange->cWrites);
        STAM_REL_COUNTER_INC(&pVM->cpum.s.cSysRegWrites);

        if (!(uValue & pRange->fWrExcpMask))
        {
            CPUMSYSREGWRFN  enmWrFn = (CPUMSYSREGWRFN)pRange->enmWrFn;
            AssertReturn(enmWrFn > kCpumSysRegWrFn_Invalid && enmWrFn < kCpumSysRegWrFn_End, VERR_CPUM_IPE_1);

            PFNCPUMWRSYSREG pfnWrSysReg = g_aCpumWrSysRegFns[enmWrFn].pfnWrSysReg;
            AssertReturn(pfnWrSysReg, VERR_CPUM_IPE_2);

            uint64_t uValueAdjusted = uValue & ~pRange->fWrIgnMask;
            if (uValueAdjusted != uValue)
            {
                STAM_COUNTER_INC(&pRange->cIgnoredBits);
                STAM_REL_COUNTER_INC(&pVM->cpum.s.cSysRegWritesToIgnoredBits);
            }

            rcStrict = pfnWrSysReg(pVCpu, idSysReg, pRange, uValueAdjusted, uValue);
            if (rcStrict == VINF_SUCCESS)
                Log2(("CPUM: MSR %#x (%s), %#llx [%#llx]\n", idSysReg, pRange->szName, uValueAdjusted, uValue));
            else if (rcStrict == VERR_CPUM_RAISE_GP_0)
            {
                Log(("CPUM: MSR %#x (%s), %#llx [%#llx] -> #GP(0)\n", idSysReg, pRange->szName, uValueAdjusted, uValue));
                STAM_COUNTER_INC(&pRange->cExcp);
                STAM_REL_COUNTER_INC(&pVM->cpum.s.cSysRegWritesRaiseExcp);
            }
#ifndef IN_RING3
            else if (rcStrict == VINF_CPUM_R3_MSR_WRITE)
                Log(("CPUM: MSR %#x (%s), %#llx [%#llx] -> ring-3\n", idSysReg, pRange->szName, uValueAdjusted, uValue));
#endif
            else
            {
                Log(("CPUM: MSR %#x (%s), %#llx [%#llx] -> rcStrict=%Rrc\n",
                     idSysReg, pRange->szName, uValueAdjusted, uValue, VBOXSTRICTRC_VAL(rcStrict)));
                AssertMsgStmt(RT_FAILURE_NP(rcStrict), ("%Rrc idSysReg=%#x\n", VBOXSTRICTRC_VAL(rcStrict), idSysReg),
                              rcStrict = VERR_IPE_UNEXPECTED_INFO_STATUS);
                Assert(rcStrict != VERR_EM_INTERPRETER);
            }
        }
        else
        {
            Log(("CPUM: MSR %#x (%s), %#llx -> #GP(0) - invalid bits %#llx\n",
                 idSysReg, pRange->szName, uValue, uValue & pRange->fWrExcpMask));
            STAM_COUNTER_INC(&pRange->cExcp);
            STAM_REL_COUNTER_INC(&pVM->cpum.s.cSysRegWritesRaiseExcp);
            rcStrict = VERR_CPUM_RAISE_GP_0;
        }
    }
    else
    {
        Log(("CPUM: Unknown MSR %#x, %#llx -> #GP(0)\n", idSysReg, uValue));
        STAM_REL_COUNTER_INC(&pVM->cpum.s.cSysRegWrites);
        STAM_REL_COUNTER_INC(&pVM->cpum.s.cSysRegWritesUnknown);
        rcStrict = VERR_CPUM_RAISE_GP_0; /** @todo Better status code. */
    }
    return rcStrict;
}


#if defined(VBOX_STRICT) && defined(IN_RING3)
/**
 * Performs some checks on the static data related to MSRs.
 *
 * @returns VINF_SUCCESS on success, error on failure.
 */
DECLHIDDEN(int) cpumR3SysRegStrictInitChecks(void)
{
#define CPUM_ASSERT_RD_SYSREG_FN(a_Register) \
        AssertReturn(g_aCpumRdSysRegFns[kCpumSysRegRdFn_##a_Register].pfnRdSysReg == cpumSysRegRd_##a_Register, VERR_CPUM_IPE_2);
#define CPUM_ASSERT_WR_SYSREG_FN(a_Register) \
        AssertReturn(g_aCpumWrSysRegFns[kCpumSysRegWrFn_##a_Register].pfnWrSysReg == cpumSysRegWr_##a_Register, VERR_CPUM_IPE_2);

    AssertReturn(g_aCpumRdSysRegFns[kCpumSysRegRdFn_Invalid].pfnRdSysReg == NULL, VERR_CPUM_IPE_2);
    CPUM_ASSERT_RD_SYSREG_FN(FixedValue);
    CPUM_ASSERT_RD_SYSREG_FN(WriteOnly);
    CPUM_ASSERT_RD_SYSREG_FN(GicV3Icc);
    CPUM_ASSERT_RD_SYSREG_FN(OslsrEl1);

    AssertReturn(g_aCpumWrSysRegFns[kCpumSysRegWrFn_Invalid].pfnWrSysReg == NULL, VERR_CPUM_IPE_2);
    CPUM_ASSERT_WR_SYSREG_FN(IgnoreWrite);
    CPUM_ASSERT_WR_SYSREG_FN(ReadOnly);
    CPUM_ASSERT_WR_SYSREG_FN(GicV3Icc);
    CPUM_ASSERT_WR_SYSREG_FN(OslarEl1);

    return VINF_SUCCESS;
}
#endif /* VBOX_STRICT && IN_RING3 */
