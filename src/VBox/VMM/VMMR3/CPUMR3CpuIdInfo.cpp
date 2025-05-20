/* $Id: CPUMR3CpuIdInfo.cpp 168941 2025-05-20 21:23:52Z knut.osmundsen@oracle.com $ */
/** @file
 * CPUM - CPU ID part.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/ssm.h>
#include "CPUMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/sup.h>

#include <iprt/errcore.h>
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/ctype.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/x86-helpers.h>



DECLHIDDEN(void) cpumR3CpuIdInfoMnemonicListU32(PCPUMCPUIDINFOSTATE pThis, uint32_t uVal, PCDBGFREGSUBFIELD pDesc,
                                                const char *pszLeadIn, uint32_t cchWidth,
                                                const char *pszLeadIn2 /*= ""*/, uint32_t cchWidth2 /*= 0*/)
{
    PCDBGFINFOHLP const pHlp = pThis->pHlp;
    if (pszLeadIn)
        pHlp->pfnPrintf(pHlp, "%*s%*s", cchWidth, pszLeadIn, cchWidth2, pszLeadIn2);

    for (uint32_t iBit = 0; iBit < 32; iBit++)
        if (RT_BIT_32(iBit) & uVal)
        {
            while (   pDesc->pszName != NULL
                   && iBit >= (uint32_t)pDesc->iFirstBit + pDesc->cBits)
                pDesc++;
            if (   pDesc->pszName != NULL
                && iBit - (uint32_t)pDesc->iFirstBit < (uint32_t)pDesc->cBits)
            {
                if (pDesc->cBits == 1)
                    pHlp->pfnPrintf(pHlp, " %s", pDesc->pszName);
                else
                {
                    uint32_t uFieldValue = uVal >> pDesc->iFirstBit;
                    if (pDesc->cBits < 32)
                        uFieldValue &= RT_BIT_32(pDesc->cBits) - UINT32_C(1);
                    pHlp->pfnPrintf(pHlp, pDesc->cBits < 4 ? " %s=%u" : " %s=%#x", pDesc->pszName, uFieldValue);
                    iBit = pDesc->iFirstBit + pDesc->cBits - 1;
                }
            }
            else
                pHlp->pfnPrintf(pHlp, " %u", iBit);
        }
    if (pszLeadIn)
        pHlp->pfnPrintf(pHlp, "\n");
}


DECLHIDDEN(void) cpumR3CpuIdInfoMnemonicListU64(PCPUMCPUIDINFOSTATE pThis, uint64_t uVal, PCDBGFREGSUBFIELD pDesc,
                                                const char *pszLeadIn, uint32_t cchWidth)
{
    PCDBGFINFOHLP const pHlp = pThis->pHlp;
    if (pszLeadIn)
        pHlp->pfnPrintf(pHlp, "%*s", cchWidth, pszLeadIn);

    for (uint32_t iBit = 0; iBit < 64; iBit++)
        if (RT_BIT_64(iBit) & uVal)
        {
            while (   pDesc->pszName != NULL
                   && iBit >= (uint32_t)pDesc->iFirstBit + pDesc->cBits)
                pDesc++;
            if (   pDesc->pszName != NULL
                && iBit - (uint32_t)pDesc->iFirstBit < (uint32_t)pDesc->cBits)
            {
                if (pDesc->cBits == 1)
                    pHlp->pfnPrintf(pHlp, " %s", pDesc->pszName);
                else
                {
                    uint64_t uFieldValue = uVal >> pDesc->iFirstBit;
                    if (pDesc->cBits < 64)
                        uFieldValue &= RT_BIT_64(pDesc->cBits) - UINT64_C(1);
                    pHlp->pfnPrintf(pHlp, pDesc->cBits < 4 ? " %s=%llu" : " %s=%#llx", pDesc->pszName, uFieldValue);
                    iBit = pDesc->iFirstBit + pDesc->cBits - 1;
                }
            }
            else
                pHlp->pfnPrintf(pHlp, " %u", iBit);
        }
    if (pszLeadIn)
        pHlp->pfnPrintf(pHlp, "\n");
}


DECLHIDDEN(void) cpumR3CpuIdInfoValueWithMnemonicListU64(PCPUMCPUIDINFOSTATE pThis, uint64_t uVal, PCDBGFREGSUBFIELD pDesc,
                                                         const char *pszLeadIn, uint32_t cchWidth,
                                                         const char *pszLeadIn2, uint32_t cchWidth2)
{
    PCDBGFINFOHLP const pHlp = pThis->pHlp;
    if (!uVal)
        pHlp->pfnPrintf(pHlp, "%*s%*s: %#010x`%08x\n",
                        cchWidth, pszLeadIn, cchWidth2, pszLeadIn2, RT_HI_U32(uVal), RT_LO_U32(uVal));
    else
    {
        pHlp->pfnPrintf(pHlp, "%*s%*s: %#010x`%08x (",
                        cchWidth, pszLeadIn, cchWidth2, pszLeadIn2, RT_HI_U32(uVal), RT_LO_U32(uVal));
        cpumR3CpuIdInfoMnemonicListU64(pThis, uVal, pDesc, NULL, 0);
        pHlp->pfnPrintf(pHlp, " )\n");
    }
}


DECLHIDDEN(void) cpumR3CpuIdInfoVerboseCompareListU32(PCPUMCPUIDINFOSTATE pThis, uint32_t uVal1, uint32_t uVal2,
                                                      PCDBGFREGSUBFIELD pDesc,
                                                      const char *pszLeadIn /*= NULL*/, uint32_t cchWidth /*=0*/)
{
    PCDBGFINFOHLP const pHlp = pThis->pHlp;
    if (pszLeadIn)
        pHlp->pfnPrintf(pHlp,
                        "%s\n"
                        "  %-*s= %s%s%s%s\n",
                        pszLeadIn,
                        cchWidth, "Mnemonic - Description",
                        pThis->pszLabel,
                        pThis->pszLabel2 ? " (" : "",
                        pThis->pszLabel2 ? pThis->pszLabel2 : "",
                        pThis->pszLabel2 ? ")" : "");

    uint32_t uCombined = uVal1 | uVal2;
    for (uint32_t iBit = 0; iBit < 32; iBit++)
        if (   (RT_BIT_32(iBit) & uCombined)
            || (iBit == pDesc->iFirstBit && pDesc->pszName) )
        {
            while (   pDesc->pszName != NULL
                   && iBit >= (uint32_t)pDesc->iFirstBit + pDesc->cBits)
                pDesc++;

            if (   pDesc->pszName != NULL
                && iBit - (uint32_t)pDesc->iFirstBit < (uint32_t)pDesc->cBits)
            {
                size_t      cchMnemonic  = strlen(pDesc->pszName);
                const char *pszDesc      = pDesc->pszName + cchMnemonic + 1;
                size_t      cchDesc      = strlen(pszDesc);
                uint32_t    uFieldValue1 = uVal1 >> pDesc->iFirstBit;
                uint32_t    uFieldValue2 = uVal2 >> pDesc->iFirstBit;
                if (pDesc->cBits < 32)
                {
                    uFieldValue1 &= RT_BIT_32(pDesc->cBits) - UINT32_C(1);
                    uFieldValue2 &= RT_BIT_32(pDesc->cBits) - UINT32_C(1);
                }
                pHlp->pfnPrintf(pHlp,
                                pDesc->cBits < 4
                                ? (pThis->pszLabel2 ? "  %s - %s%*s= %u (%u)\n"   : "  %s - %s%*s= %u\n")
                                :  pThis->pszLabel2 ? "  %s - %s%*s= %#x (%#x)\n" : "  %s - %s%*s= %#x\n",
                                pDesc->pszName, pszDesc,
                                cchMnemonic + 3 + cchDesc < cchWidth ? cchWidth - (cchMnemonic + 3 + cchDesc) : 1, "",
                                uFieldValue1, uFieldValue2);

                iBit = pDesc->iFirstBit + pDesc->cBits - 1U;
                pDesc++;
            }
            else
                pHlp->pfnPrintf(pHlp, pThis->pszLabel2 ? "  %2u - Reserved%*s= %u (%u)\n" : "  %2u - Reserved%*s= %u\n",
                                iBit, 13 < cchWidth ? cchWidth - 13 : 1, "",
                                RT_BOOL(uVal1 & RT_BIT_32(iBit)), RT_BOOL(uVal2 & RT_BIT_32(iBit)));
        }
}


DECLHIDDEN(void) cpumR3CpuIdInfoVerboseCompareListU64(PCPUMCPUIDINFOSTATE pThis, uint64_t uVal1, uint64_t uVal2,
                                                      PCDBGFREGSUBFIELD pDesc, uint32_t cchWidth,
                                                      bool fColumnHeaders /*=false*/, const char *pszLeadIn /*=NULL*/)
{
    PCDBGFINFOHLP const pHlp = pThis->pHlp;
    if (pszLeadIn)
        pHlp->pfnPrintf(pHlp,
                        "%s\n",
                        pszLeadIn);
    if (fColumnHeaders)
        pHlp->pfnPrintf(pHlp,
                        "  %-*s= %s%s%s%s\n",
                        cchWidth, "Mnemonic - Description",
                        pThis->pszLabel,
                        pThis->pszLabel2 ? " (" : "",
                        pThis->pszLabel2 ? pThis->pszLabel2 : "",
                        pThis->pszLabel2 ? ")" : "");

    uint64_t uCombined = uVal1 | uVal2;
    for (uint32_t iBit = 0; iBit < 64; iBit++)
        if (   (RT_BIT_64(iBit) & uCombined)
            || (iBit == pDesc->iFirstBit && pDesc->pszName) )
        {
            while (   pDesc->pszName != NULL
                   && iBit >= (uint32_t)pDesc->iFirstBit + pDesc->cBits)
                pDesc++;

            if (   pDesc->pszName != NULL
                && iBit - (uint32_t)pDesc->iFirstBit < (uint32_t)pDesc->cBits)
            {
                size_t      cchMnemonic  = strlen(pDesc->pszName);
                const char *pszDesc      = pDesc->pszName + cchMnemonic + 1;
                size_t      cchDesc      = strlen(pszDesc);
                uint64_t    uFieldValue1 = uVal1 >> pDesc->iFirstBit;
                uint64_t    uFieldValue2 = uVal2 >> pDesc->iFirstBit;
                if (pDesc->cBits < 64)
                {
                    uFieldValue1 &= RT_BIT_64(pDesc->cBits) - UINT64_C(1);
                    uFieldValue2 &= RT_BIT_64(pDesc->cBits) - UINT64_C(1);
                }
                pHlp->pfnPrintf(pHlp,
                                pDesc->cBits < 4
                                ? (pThis->pszLabel2 ? "  %s - %s%*s= %RU64 (%RU64)\n"   : "  %s - %s%*s= %RU64\n")
                                :  pThis->pszLabel2 ? "  %s - %s%*s= %#RX64 (%#RX64)\n" : "  %s - %s%*s= %#RX64\n",
                                pDesc->pszName, pszDesc,
                                cchMnemonic + 3 + cchDesc < cchWidth ? cchWidth - (cchMnemonic + 3 + cchDesc) : 1, "",
                                uFieldValue1, uFieldValue2);

                iBit = pDesc->iFirstBit + pDesc->cBits - 1U;
                pDesc++;
            }
            else
                pHlp->pfnPrintf(pHlp, pThis->pszLabel2 ? "  %2u - Reserved%*s= %u (%u)\n" : "  %2u - Reserved%*s= %u\n",
                                iBit, 13 < cchWidth ? cchWidth - 13 : 1, "",
                                RT_BOOL(uVal1 & RT_BIT_64(iBit)), RT_BOOL(uVal2 & RT_BIT_64(iBit)));
        }
}


/**
 * Display the guest CpuId leaves.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     "terse", "default" or "verbose".
 */
DECLCALLBACK(void) cpumR3CpuIdInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    /*
     * Parse the argument.
     */
    unsigned iVerbosity = 1;
    if (pszArgs)
    {
        pszArgs = RTStrStripL(pszArgs);
        if (!strcmp(pszArgs, "terse"))
            iVerbosity--;
        else if (!strcmp(pszArgs, "verbose"))
            iVerbosity++;
    }

    /*
     * Call the appropriate worker for the target.
     */
#ifdef VBOX_VMM_TARGET_X86
    CPUMCPUIDINFOSTATEX86   InfoState;
#elif defined(VBOX_VMM_TARGET_ARMV8)
    CPUMCPUIDINFOSTATEARMV8 InfoState;
#else
# error "port me"
#endif
    InfoState.Cmn.pHlp          = pHlp;
    InfoState.Cmn.iVerbosity    = iVerbosity;
    InfoState.Cmn.cchLabelMax   = 5;
    InfoState.Cmn.pszShort      = "Gst";
    InfoState.Cmn.pszLabel      = "Guest";
    InfoState.Cmn.cchLabel      = 5;
#if  (defined(VBOX_VMM_TARGET_X86)   && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86))) \
  || (defined(VBOX_VMM_TARGET_ARMV8) && (defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32)))
    InfoState.Cmn.cchLabel2     = 4U;
    InfoState.Cmn.pszShort2     = "Hst";
    InfoState.Cmn.pszLabel2     = "Host";
#else
    InfoState.Cmn.cchLabel2     = 0;
    InfoState.Cmn.pszShort2     = NULL;
    InfoState.Cmn.pszLabel2     = NULL;
#endif
    InfoState.pFeatures     = &pVM->cpum.s.GuestFeatures;
#if defined(VBOX_VMM_TARGET_X86)
    /* x86 specifics: */
    InfoState.paLeaves      = pVM->cpum.s.GuestInfo.paCpuIdLeavesR3;
    InfoState.cLeaves       = pVM->cpum.s.GuestInfo.cCpuIdLeaves;
    InfoState.cLeaves2      = 0;
    InfoState.paLeaves2     = NULL;
#elif defined(VBOX_VMM_TARGET_ARMV8)
    /* ARMv8 specifics: */
    InfoState.paIdRegs      = NULL; /** @todo */
    InfoState.cIdRegs       = 0;
    InfoState.cIdRegs2      = 0;
    InfoState.paIdRegs2     = NULL;
    /* Convert from IdRegs struct for now (must be sorted): */
    InfoState.cIdRegs = 15;
    PSUPARMSYSREGVAL paFreeArmV8 = (PSUPARMSYSREGVAL)RTMemAllocZ(sizeof(paFreeArmV8[0]) * InfoState.cIdRegs);
    AssertReturnVoid(paFreeArmV8);
    InfoState.paIdRegs = paFreeArmV8;
# define ADD_ID_REG(a_idx, a_idReg, a_uValue) do { \
            Assert((a_idx) == 0 || paFreeArmV8[(a_idx) - 1].idReg < (a_idReg)); \
            paFreeArmV8[a_idx].uValue = (a_uValue); \
            paFreeArmV8[a_idx].idReg  = (a_idReg); \
            paFreeArmV8[a_idx].fFlags = 0; \
        } while (0)
    ADD_ID_REG( 0, ARMV8_AARCH64_SYSREG_ID_AA64PFR0_EL1,  pVM->cpum.s.GuestIdRegs.u64RegIdAa64Pfr0El1);  /* 3.0.0.4.0 */
    ADD_ID_REG( 1, ARMV8_AARCH64_SYSREG_ID_AA64PFR1_EL1,  pVM->cpum.s.GuestIdRegs.u64RegIdAa64Pfr1El1);
    ADD_ID_REG( 2, ARMV8_AARCH64_SYSREG_ID_AA64DFR0_EL1,  pVM->cpum.s.GuestIdRegs.u64RegIdAa64Dfr0El1);  /* 3.0.0.5.0 */
    ADD_ID_REG( 3, ARMV8_AARCH64_SYSREG_ID_AA64DFR1_EL1,  pVM->cpum.s.GuestIdRegs.u64RegIdAa64Dfr1El1);
    ADD_ID_REG( 4, ARMV8_AARCH64_SYSREG_ID_AA64AFR0_EL1,  pVM->cpum.s.GuestIdRegs.u64RegIdAa64Afr0El1);  /* 3.0.0.5.4*/
    ADD_ID_REG( 5, ARMV8_AARCH64_SYSREG_ID_AA64AFR1_EL1,  pVM->cpum.s.GuestIdRegs.u64RegIdAa64Afr1El1);
    ADD_ID_REG( 6, ARMV8_AARCH64_SYSREG_ID_AA64ISAR0_EL1, pVM->cpum.s.GuestIdRegs.u64RegIdAa64Isar0El1); /* 3.0.0.6.0 */
    ADD_ID_REG( 7, ARMV8_AARCH64_SYSREG_ID_AA64ISAR1_EL1, pVM->cpum.s.GuestIdRegs.u64RegIdAa64Isar1El1);
    ADD_ID_REG( 8, ARMV8_AARCH64_SYSREG_ID_AA64ISAR2_EL1, pVM->cpum.s.GuestIdRegs.u64RegIdAa64Isar2El1);
    ADD_ID_REG( 9, ARMV8_AARCH64_SYSREG_ID_AA64MMFR0_EL1, pVM->cpum.s.GuestIdRegs.u64RegIdAa64Mmfr0El1); /* 3.0.0.7.0 */
    ADD_ID_REG(10, ARMV8_AARCH64_SYSREG_ID_AA64MMFR1_EL1, pVM->cpum.s.GuestIdRegs.u64RegIdAa64Mmfr1El1);
    ADD_ID_REG(11, ARMV8_AARCH64_SYSREG_ID_AA64MMFR2_EL1, pVM->cpum.s.GuestIdRegs.u64RegIdAa64Mmfr2El1);
    ADD_ID_REG(12, ARMV8_AARCH64_SYSREG_CLIDR_EL1,        pVM->cpum.s.GuestIdRegs.u64RegClidrEl1);       /* 3.1.0.0.1 */
    ADD_ID_REG(13, ARMV8_AARCH64_SYSREG_CTR_EL0,          pVM->cpum.s.GuestIdRegs.u64RegCtrEl0);         /* 3.3.0.0.1 */
    ADD_ID_REG(14, ARMV8_AARCH64_SYSREG_DCZID_EL0,        pVM->cpum.s.GuestIdRegs.u64RegDczidEl0);       /* 3.3.0.0.7 */
# undef ADD_ID_REG
#else
# error "port me"
#endif

#if defined(VBOX_VMM_TARGET_X86) && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86))
    PCPUMCPUIDLEAF paFree = NULL;
    InfoState.paLeaves2 = pVM->cpum.s.paHostLeavesR3;
    InfoState.cLeaves2  = pVM->cpum.s.cHostLeaves;
    if (!InfoState.paLeaves2 || !InfoState.cLeaves2)
    {
        int rc = CPUMCpuIdCollectLeavesFromX86Host(&paFree, &InfoState.cLeaves2);
        if (RT_SUCCESS(rc))
            InfoState.paLeaves2 = paFree;
        else
            InfoState.cLeaves2  = 0;
    }
    CPUMR3CpuIdInfoX86(&InfoState);
    RTMemFree(paFree);

#elif defined(VBOX_VMM_TARGET_ARMV8) && (defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32))
    PSUPARMSYSREGVAL paFree = NULL;
    InfoState.paIdRegs2 = pVM->cpum.s.paHostIdRegsR3;
    InfoState.cIdRegs2  = pVM->cpum.s.cHostIdRegs;
    if (!InfoState.paIdRegs2 || !InfoState.cIdRegs2)
    {
        int rc = CPUMCpuIdCollectIdSysRegsFromArmV8Host(&paFree, &InfoState.cIdRegs2);
        if (RT_SUCCESS(rc))
            InfoState.paIdRegs2 = paFree;
        else
            InfoState.cIdRegs2  = 0;
    }
    CPUMR3CpuIdInfoArmV8(&InfoState);
    RTMemFree(paFree);

#elif defined(VBOX_VMM_TARGET_X86)
    CPUMR3CpuIdInfoX86(&InfoState);

#elif defined(VBOX_VMM_TARGET_ARMV8)
    CPUMR3CpuIdInfoArmV8(&InfoState);

#else
# error "port me"
#endif

#if defined(VBOX_VMM_TARGET_ARMV8)
    RTMemFree(paFreeArmV8);
#endif
}


/**
 * Display the guest CpuId leaves.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     "terse", "default" or "verbose".
 */
DECLCALLBACK(void) cpumR3CpuIdInfoHost(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    /*
     * Parse the argument.
     */
    unsigned iVerbosity = 1;
    if (pszArgs)
    {
        pszArgs = RTStrStripL(pszArgs);
        if (!strcmp(pszArgs, "terse"))
            iVerbosity--;
        else if (!strcmp(pszArgs, "verbose"))
            iVerbosity++;
    }

    /*
     * Call the appropriate worker for the target.
     */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    CPUMCPUIDINFOSTATEX86   InfoState;
#elif defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32)
    CPUMCPUIDINFOSTATEARMV8 InfoState;
#else
# error "port me"
#endif
    InfoState.Cmn.pHlp          = pHlp;
    InfoState.Cmn.iVerbosity    = iVerbosity;
    InfoState.Cmn.cchLabelMax   = 4;
    InfoState.Cmn.pszShort      = "Hst";
    InfoState.Cmn.pszLabel      = "Host";
    InfoState.Cmn.cchLabel      = 4;
    InfoState.Cmn.cchLabel2     = 0;
    InfoState.Cmn.pszShort2     = NULL;
    InfoState.Cmn.pszLabel2     = NULL;
    InfoState.pFeatures     = &g_CpumHostFeatures.s;
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    /* x86 specifics: */
    InfoState.paLeaves      = pVM->cpum.s.paHostLeavesR3;
    InfoState.cLeaves       = pVM->cpum.s.cHostLeaves;
    InfoState.cLeaves2      = 0;
    InfoState.paLeaves2     = NULL;
    PCPUMCPUIDLEAF paFree = NULL;
    if (!InfoState.paLeaves || !InfoState.cLeaves)
    {
        int rc = CPUMCpuIdCollectLeavesFromX86Host(&paFree, &InfoState.cLeaves);
        if (RT_SUCCESS(rc))
            InfoState.paLeaves = paFree;
        else
            InfoState.cLeaves  = 0;
    }
    CPUMR3CpuIdInfoX86(&InfoState);
    RTMemFree(paFree);

#elif defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32)
    /* ARMv8 specifics: */
    InfoState.paIdRegs      = pVM->cpum.s.paHostIdRegsR3;
    InfoState.cIdRegs       = pVM->cpum.s.cHostIdRegs;
    InfoState.cIdRegs2      = 0;
    InfoState.paIdRegs2     = NULL;
    PSUPARMSYSREGVAL paFree = NULL;
    if (!InfoState.paIdRegs || !InfoState.cIdRegs)
    {
        int rc = CPUMCpuIdCollectIdSysRegsFromArmV8Host(&paFree, &InfoState.cIdRegs);
        if (RT_SUCCESS(rc))
            InfoState.paIdRegs = paFree;
        else
            InfoState.cIdRegs  = 0;
    }
    CPUMR3CpuIdInfoArmV8(&InfoState);
    RTMemFree(paFree);

#else
# error "port me"
#endif
}

