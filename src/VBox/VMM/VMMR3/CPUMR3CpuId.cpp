/* $Id: CPUMR3CpuId.cpp 107848 2025-01-15 10:15:06Z knut.osmundsen@oracle.com $ */
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

#include <VBox/err.h>
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/ctype.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/x86-helpers.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** For sanity and avoid wasting hyper heap on buggy config / saved state. */
#define CPUM_CPUID_MAX_LEAVES       2048


#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
/**
 * Determins the host CPU MXCSR mask.
 *
 * @returns MXCSR mask.
 */
VMMR3DECL(uint32_t) CPUMR3DeterminHostMxCsrMask(void)
{
    if (   ASMHasCpuId()
        && RTX86IsValidStdRange(ASMCpuId_EAX(0))
        && ASMCpuId_EDX(1) & X86_CPUID_FEATURE_EDX_FXSR)
    {
        uint8_t volatile abBuf[sizeof(X86FXSTATE) + 64];
        PX86FXSTATE      pState = (PX86FXSTATE)&abBuf[64 - ((uintptr_t)&abBuf[0] & 63)];
        RT_ZERO(*pState);
        ASMFxSave(pState);
        if (pState->MXCSR_MASK == 0)
            return 0xffbf;
        return pState->MXCSR_MASK;
    }
    return 0;
}
#endif



#ifndef IN_VBOX_CPU_REPORT
/**
 * Gets a matching leaf in the CPUID leaf array, converted to a CPUMCPUID.
 *
 * @returns true if found, false it not.
 * @param   paLeaves            The CPUID leaves to search.  This is sorted.
 * @param   cLeaves             The number of leaves in the array.
 * @param   uLeaf               The leaf to locate.
 * @param   uSubLeaf            The subleaf to locate.  Pass 0 if no sub-leaves.
 * @param   pLegacy             The legacy output leaf.
 */
static bool cpumR3CpuIdGetLeafLegacy(PCPUMCPUIDLEAF paLeaves, uint32_t cLeaves, uint32_t uLeaf, uint32_t uSubLeaf,
                                     PCPUMCPUID pLegacy)
{
    PCPUMCPUIDLEAF pLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, uLeaf, uSubLeaf);
    if (pLeaf)
    {
        pLegacy->uEax = pLeaf->uEax;
        pLegacy->uEbx = pLeaf->uEbx;
        pLegacy->uEcx = pLeaf->uEcx;
        pLegacy->uEdx = pLeaf->uEdx;
        return true;
    }
    return false;
}
#endif /* IN_VBOX_CPU_REPORT */


/**
 * Inserts a CPU ID leaf, replacing any existing ones.
 *
 * When inserting a simple leaf where we already got a series of sub-leaves with
 * the same leaf number (eax), the simple leaf will replace the whole series.
 *
 * When pVM is NULL, this ASSUMES that the leaves array is still on the normal
 * host-context heap and has only been allocated/reallocated by the
 * cpumCpuIdEnsureSpace function.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.  If NULL, use
 *                      the process heap, otherwise the VM's hyper heap.
 * @param   ppaLeaves   Pointer to the pointer to the array of sorted
 *                      CPUID leaves and sub-leaves. Must be NULL if using
 *                      the hyper heap.
 * @param   pcLeaves    Where we keep the leaf count for *ppaLeaves. Must
 *                      be NULL if using the hyper heap.
 * @param   pNewLeaf    Pointer to the data of the new leaf we're about to
 *                      insert.
 */
static int cpumR3CpuIdInsert(PVM pVM, PCPUMCPUIDLEAF *ppaLeaves, uint32_t *pcLeaves, PCPUMCPUIDLEAF pNewLeaf)
{
    /*
     * Validate input parameters if we are using the hyper heap and use the VM's CPUID arrays.
     */
    if (pVM)
    {
        AssertReturn(!ppaLeaves, VERR_INVALID_PARAMETER);
        AssertReturn(!pcLeaves, VERR_INVALID_PARAMETER);
        AssertReturn(pVM->cpum.s.GuestInfo.paCpuIdLeavesR3 == pVM->cpum.s.GuestInfo.aCpuIdLeaves, VERR_INVALID_PARAMETER);

        ppaLeaves = &pVM->cpum.s.GuestInfo.paCpuIdLeavesR3;
        pcLeaves  = &pVM->cpum.s.GuestInfo.cCpuIdLeaves;
    }

    PCPUMCPUIDLEAF  paLeaves = *ppaLeaves;
    uint32_t        cLeaves  = *pcLeaves;

    /*
     * Validate the new leaf a little.
     */
    AssertLogRelMsgReturn(!(pNewLeaf->fFlags & ~CPUMCPUIDLEAF_F_VALID_MASK),
                          ("%#x/%#x: %#x", pNewLeaf->uLeaf, pNewLeaf->uSubLeaf, pNewLeaf->fFlags),
                          VERR_INVALID_FLAGS);
    AssertLogRelMsgReturn(pNewLeaf->fSubLeafMask != 0 || pNewLeaf->uSubLeaf == 0,
                          ("%#x/%#x: %#x", pNewLeaf->uLeaf, pNewLeaf->uSubLeaf, pNewLeaf->fSubLeafMask),
                          VERR_INVALID_PARAMETER);
    AssertLogRelMsgReturn(RT_IS_POWER_OF_TWO(pNewLeaf->fSubLeafMask + 1),
                          ("%#x/%#x: %#x", pNewLeaf->uLeaf, pNewLeaf->uSubLeaf, pNewLeaf->fSubLeafMask),
                          VERR_INVALID_PARAMETER);
    AssertLogRelMsgReturn((pNewLeaf->fSubLeafMask & pNewLeaf->uSubLeaf) == pNewLeaf->uSubLeaf,
                          ("%#x/%#x: %#x", pNewLeaf->uLeaf, pNewLeaf->uSubLeaf, pNewLeaf->fSubLeafMask),
                          VERR_INVALID_PARAMETER);

    /*
     * Find insertion point.  The lazy bird uses the same excuse as in
     * cpumCpuIdGetLeaf(), but optimizes for linear insertion (saved state).
     */
    uint32_t i;
    if (   cLeaves > 0
        && paLeaves[cLeaves - 1].uLeaf < pNewLeaf->uLeaf)
    {
        /* Add at end. */
        i = cLeaves;
    }
    else if (   cLeaves > 0
             && paLeaves[cLeaves - 1].uLeaf == pNewLeaf->uLeaf)
    {
        /* Either replacing the last leaf or dealing with sub-leaves. Spool
           back to the first sub-leaf to pretend we did the linear search. */
        i = cLeaves - 1;
        while (   i > 0
               && paLeaves[i - 1].uLeaf == pNewLeaf->uLeaf)
            i--;
    }
    else
    {
        /* Linear search from the start. */
        i = 0;
        while (   i < cLeaves
                  && paLeaves[i].uLeaf < pNewLeaf->uLeaf)
            i++;
    }
    if (   i < cLeaves
        && paLeaves[i].uLeaf == pNewLeaf->uLeaf)
    {
        if (paLeaves[i].fSubLeafMask != pNewLeaf->fSubLeafMask)
        {
            /*
             * The sub-leaf mask differs, replace all existing leaves with the
             * same leaf number.
             */
            uint32_t c = 1;
            while (   i + c < cLeaves
                   && paLeaves[i + c].uLeaf == pNewLeaf->uLeaf)
                c++;
            if (c > 1 && i + c < cLeaves)
            {
                memmove(&paLeaves[i + c], &paLeaves[i + 1], (cLeaves - i - c) * sizeof(paLeaves[0]));
                *pcLeaves = cLeaves -= c - 1;
            }

            paLeaves[i] = *pNewLeaf;
#ifdef VBOX_STRICT
            cpumCpuIdAssertOrder(*ppaLeaves, *pcLeaves);
#endif
            return VINF_SUCCESS;
        }

        /* Find sub-leaf insertion point. */
        while (   i < cLeaves
               && paLeaves[i].uSubLeaf < pNewLeaf->uSubLeaf
               && paLeaves[i].uLeaf == pNewLeaf->uLeaf)
            i++;

        /*
         * If we've got an exactly matching leaf, replace it.
         */
        if (   i < cLeaves
            && paLeaves[i].uLeaf    == pNewLeaf->uLeaf
            && paLeaves[i].uSubLeaf == pNewLeaf->uSubLeaf)
        {
            paLeaves[i] = *pNewLeaf;
#ifdef VBOX_STRICT
            cpumCpuIdAssertOrder(*ppaLeaves, *pcLeaves);
#endif
            return VINF_SUCCESS;
        }
    }

    /*
     * Adding a new leaf at 'i'.
     */
    AssertLogRelReturn(cLeaves < CPUM_CPUID_MAX_LEAVES, VERR_TOO_MANY_CPUID_LEAVES);
    paLeaves = cpumCpuIdEnsureSpace(pVM, ppaLeaves, cLeaves);
    if (!paLeaves)
        return VERR_NO_MEMORY;

    if (i < cLeaves)
        memmove(&paLeaves[i + 1], &paLeaves[i], (cLeaves - i) * sizeof(paLeaves[0]));
    *pcLeaves += 1;
    paLeaves[i] = *pNewLeaf;

#ifdef VBOX_STRICT
    cpumCpuIdAssertOrder(*ppaLeaves, *pcLeaves);
#endif
    return VINF_SUCCESS;
}


#ifndef IN_VBOX_CPU_REPORT
/**
 * Removes a range of CPUID leaves.
 *
 * This will not reallocate the array.
 *
 * @param   paLeaves        The array of sorted CPUID leaves and sub-leaves.
 * @param   pcLeaves        Where we keep the leaf count for @a paLeaves.
 * @param   uFirst          The first leaf.
 * @param   uLast           The last leaf.
 */
static void cpumR3CpuIdRemoveRange(PCPUMCPUIDLEAF paLeaves, uint32_t *pcLeaves, uint32_t uFirst, uint32_t uLast)
{
    uint32_t cLeaves = *pcLeaves;

    Assert(uFirst <= uLast);

    /*
     * Find the first one.
     */
    uint32_t iFirst = 0;
    while (   iFirst < cLeaves
           && paLeaves[iFirst].uLeaf < uFirst)
        iFirst++;

    /*
     * Find the end (last + 1).
     */
    uint32_t iEnd = iFirst;
    while (   iEnd < cLeaves
           && paLeaves[iEnd].uLeaf <= uLast)
        iEnd++;

    /*
     * Adjust the array if anything needs removing.
     */
    if (iFirst < iEnd)
    {
        if (iEnd < cLeaves)
            memmove(&paLeaves[iFirst], &paLeaves[iEnd], (cLeaves - iEnd) * sizeof(paLeaves[0]));
        *pcLeaves = cLeaves -= (iEnd - iFirst);
    }

# ifdef VBOX_STRICT
    cpumCpuIdAssertOrder(paLeaves, *pcLeaves);
# endif
}
#endif /* IN_VBOX_CPU_REPORT */


/**
 * Gets a CPU ID leaf.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pLeaf       Where to store the found leaf.
 * @param   uLeaf       The leaf to locate.
 * @param   uSubLeaf    The subleaf to locate.  Pass 0 if no sub-leaves.
 */
VMMR3DECL(int) CPUMR3CpuIdGetLeaf(PVM pVM, PCPUMCPUIDLEAF pLeaf, uint32_t uLeaf, uint32_t uSubLeaf)
{
    PCPUMCPUIDLEAF pcLeaf = cpumCpuIdGetLeafInt(pVM->cpum.s.GuestInfo.paCpuIdLeavesR3, pVM->cpum.s.GuestInfo.cCpuIdLeaves,
                                                uLeaf, uSubLeaf);
    if (pcLeaf)
    {
        memcpy(pLeaf, pcLeaf, sizeof(*pLeaf));
        return VINF_SUCCESS;
    }

    return VERR_NOT_FOUND;
}


/**
 * Gets all the leaves.
 *
 * This only works after the CPUID leaves have been initialized.  The interface
 * is intended for NEM and configuring CPUID leaves for the native hypervisor.
 *
 * @returns Pointer to the array of leaves.  NULL on failure.
 * @param   pVM         The cross context VM structure.
 * @param   pcLeaves    Where to return the number of leaves.
 */
VMMR3_INT_DECL(PCCPUMCPUIDLEAF) CPUMR3CpuIdGetPtr(PVM pVM, uint32_t *pcLeaves)
{
    *pcLeaves = pVM->cpum.s.GuestInfo.cCpuIdLeaves;
    return pVM->cpum.s.GuestInfo.paCpuIdLeavesR3;
}


/**
 * Inserts a CPU ID leaf, replacing any existing ones.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pNewLeaf    Pointer to the leaf being inserted.
 */
VMMR3DECL(int) CPUMR3CpuIdInsert(PVM pVM, PCPUMCPUIDLEAF pNewLeaf)
{
    /*
     * Validate parameters.
     */
    AssertReturn(pVM, VERR_INVALID_PARAMETER);
    AssertReturn(pNewLeaf, VERR_INVALID_PARAMETER);

    /*
     * Disallow replacing CPU ID leaves that this API currently cannot manage.
     * These leaves have dependencies on saved-states, see PATMCpuidReplacement().
     * If you want to modify these leaves, use CPUMSetGuestCpuIdFeature().
     */
    if (   pNewLeaf->uLeaf == UINT32_C(0x00000000)  /* Standard */
        || pNewLeaf->uLeaf == UINT32_C(0x00000001)
        || pNewLeaf->uLeaf == UINT32_C(0x80000000)  /* Extended */
        || pNewLeaf->uLeaf == UINT32_C(0x80000001)
        || pNewLeaf->uLeaf == UINT32_C(0xc0000000)  /* Centaur */
        || pNewLeaf->uLeaf == UINT32_C(0xc0000001) )
    {
        return VERR_NOT_SUPPORTED;
    }

    return cpumR3CpuIdInsert(pVM, NULL /* ppaLeaves */, NULL /* pcLeaves */, pNewLeaf);
}


#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
/**
 * Determines the method the CPU uses to handle unknown CPUID leaves.
 *
 * @returns VBox status code.
 * @param   penmUnknownMethod   Where to return the method.
 * @param   pDefUnknown         Where to return default unknown values.  This
 *                              will be set, even if the resulting method
 *                              doesn't actually needs it.
 */
VMMR3DECL(int) CPUMR3CpuIdDetectUnknownLeafMethod(PCPUMUNKNOWNCPUID penmUnknownMethod, PCPUMCPUID pDefUnknown)
{
    uint32_t uLastStd = ASMCpuId_EAX(0);
    uint32_t uLastExt = ASMCpuId_EAX(0x80000000);
    if (!RTX86IsValidExtRange(uLastExt))
        uLastExt = 0x80000000;

    uint32_t auChecks[] =
    {
        uLastStd + 1,
        uLastStd + 5,
        uLastStd + 8,
        uLastStd + 32,
        uLastStd + 251,
        uLastExt + 1,
        uLastExt + 8,
        uLastExt + 15,
        uLastExt + 63,
        uLastExt + 255,
        0x7fbbffcc,
        0x833f7872,
        0xefff2353,
        0x35779456,
        0x1ef6d33e,
    };

    static const uint32_t s_auValues[] =
    {
        0xa95d2156,
        0x00000001,
        0x00000002,
        0x00000008,
        0x00000000,
        0x55773399,
        0x93401769,
        0x12039587,
    };

    /*
     * Simple method, all zeros.
     */
    *penmUnknownMethod = CPUMUNKNOWNCPUID_DEFAULTS;
    pDefUnknown->uEax = 0;
    pDefUnknown->uEbx = 0;
    pDefUnknown->uEcx = 0;
    pDefUnknown->uEdx = 0;

    /*
     * Intel has been observed returning the last standard leaf.
     */
    uint32_t auLast[4];
    ASMCpuIdExSlow(uLastStd, 0, 0, 0, &auLast[0], &auLast[1], &auLast[2], &auLast[3]);

    uint32_t cChecks = RT_ELEMENTS(auChecks);
    while (cChecks > 0)
    {
        uint32_t auCur[4];
        ASMCpuIdExSlow(auChecks[cChecks - 1], 0, 0, 0, &auCur[0], &auCur[1], &auCur[2], &auCur[3]);
        if (memcmp(auCur, auLast, sizeof(auCur)))
            break;
        cChecks--;
    }
    if (cChecks == 0)
    {
        /* Now, what happens when the input changes? Esp. ECX. */
        uint32_t cTotal       = 0;
        uint32_t cSame        = 0;
        uint32_t cLastWithEcx = 0;
        uint32_t cNeither     = 0;
        uint32_t cValues = RT_ELEMENTS(s_auValues);
        while (cValues > 0)
        {
            uint32_t uValue = s_auValues[cValues - 1];
            uint32_t auLastWithEcx[4];
            ASMCpuIdExSlow(uLastStd, uValue, uValue, uValue,
                           &auLastWithEcx[0], &auLastWithEcx[1], &auLastWithEcx[2], &auLastWithEcx[3]);

            cChecks = RT_ELEMENTS(auChecks);
            while (cChecks > 0)
            {
                uint32_t auCur[4];
                ASMCpuIdExSlow(auChecks[cChecks - 1], uValue, uValue, uValue, &auCur[0], &auCur[1], &auCur[2], &auCur[3]);
                if (!memcmp(auCur, auLast, sizeof(auCur)))
                {
                    cSame++;
                    if (!memcmp(auCur, auLastWithEcx, sizeof(auCur)))
                        cLastWithEcx++;
                }
                else if (!memcmp(auCur, auLastWithEcx, sizeof(auCur)))
                    cLastWithEcx++;
                else
                    cNeither++;
                cTotal++;
                cChecks--;
            }
            cValues--;
        }

        Log(("CPUM: cNeither=%d cSame=%d cLastWithEcx=%d cTotal=%d\n", cNeither, cSame, cLastWithEcx, cTotal));
        if (cSame == cTotal)
            *penmUnknownMethod = CPUMUNKNOWNCPUID_LAST_STD_LEAF;
        else if (cLastWithEcx == cTotal)
            *penmUnknownMethod = CPUMUNKNOWNCPUID_LAST_STD_LEAF_WITH_ECX;
        else
            *penmUnknownMethod = CPUMUNKNOWNCPUID_LAST_STD_LEAF;
        pDefUnknown->uEax = auLast[0];
        pDefUnknown->uEbx = auLast[1];
        pDefUnknown->uEcx = auLast[2];
        pDefUnknown->uEdx = auLast[3];
        return VINF_SUCCESS;
    }

    /*
     * Unchanged register values?
     */
    cChecks = RT_ELEMENTS(auChecks);
    while (cChecks > 0)
    {
        uint32_t const  uLeaf   = auChecks[cChecks - 1];
        uint32_t        cValues = RT_ELEMENTS(s_auValues);
        while (cValues > 0)
        {
            uint32_t uValue = s_auValues[cValues - 1];
            uint32_t auCur[4];
            ASMCpuIdExSlow(uLeaf, uValue, uValue, uValue, &auCur[0], &auCur[1], &auCur[2], &auCur[3]);
            if (   auCur[0] != uLeaf
                || auCur[1] != uValue
                || auCur[2] != uValue
                || auCur[3] != uValue)
                break;
            cValues--;
        }
        if (cValues != 0)
            break;
        cChecks--;
    }
    if (cChecks == 0)
    {
        *penmUnknownMethod = CPUMUNKNOWNCPUID_PASSTHRU;
        return VINF_SUCCESS;
    }

    /*
     * Just go with the simple method.
     */
    return VINF_SUCCESS;
}
#endif /* RT_ARCH_X86 || RT_ARCH_AMD64 */


/**
 * Translates a unknow CPUID leaf method into the constant name (sans prefix).
 *
 * @returns Read only name string.
 * @param   enmUnknownMethod    The method to translate.
 */
VMMR3DECL(const char *) CPUMR3CpuIdUnknownLeafMethodName(CPUMUNKNOWNCPUID enmUnknownMethod)
{
    switch (enmUnknownMethod)
    {
        case CPUMUNKNOWNCPUID_DEFAULTS:                  return "DEFAULTS";
        case CPUMUNKNOWNCPUID_LAST_STD_LEAF:             return "LAST_STD_LEAF";
        case CPUMUNKNOWNCPUID_LAST_STD_LEAF_WITH_ECX:    return "LAST_STD_LEAF_WITH_ECX";
        case CPUMUNKNOWNCPUID_PASSTHRU:                  return "PASSTHRU";

        case CPUMUNKNOWNCPUID_INVALID:
        case CPUMUNKNOWNCPUID_END:
        case CPUMUNKNOWNCPUID_32BIT_HACK:
            break;
    }
    return "Invalid-unknown-CPUID-method";
}


/*
 *
 * Init related code.
 * Init related code.
 * Init related code.
 *
 *
 */
#ifndef IN_VBOX_CPU_REPORT


/**
 * Gets an exactly matching leaf + sub-leaf in the CPUID leaf array.
 *
 * This ignores the fSubLeafMask.
 *
 * @returns Pointer to the matching leaf, or NULL if not found.
 * @param   pCpum               The CPUM instance data.
 * @param   uLeaf               The leaf to locate.
 * @param   uSubLeaf            The subleaf to locate.
 */
static PCPUMCPUIDLEAF cpumR3CpuIdGetExactLeaf(PCPUM pCpum, uint32_t uLeaf, uint32_t uSubLeaf)
{
    uint64_t        uNeedle   = RT_MAKE_U64(uSubLeaf, uLeaf);
    PCPUMCPUIDLEAF  paLeaves  = pCpum->GuestInfo.paCpuIdLeavesR3;
    uint32_t        iEnd      = pCpum->GuestInfo.cCpuIdLeaves;
    if (iEnd)
    {
        uint32_t    iBegin   = 0;
        for (;;)
        {
            uint32_t const i    = (iEnd - iBegin) / 2 + iBegin;
            uint64_t const uCur = RT_MAKE_U64(paLeaves[i].uSubLeaf, paLeaves[i].uLeaf);
            if (uNeedle < uCur)
            {
                if (i > iBegin)
                    iEnd = i;
                else
                    break;
            }
            else if (uNeedle > uCur)
            {
                if (i + 1 < iEnd)
                    iBegin = i + 1;
                else
                    break;
            }
            else
                return &paLeaves[i];
        }
    }
    return NULL;
}


/**
 * Loads MSR range overrides.
 *
 * This must be called before the MSR ranges are moved from the normal heap to
 * the hyper heap!
 *
 * @returns VBox status code (VMSetError called).
 * @param   pVM                 The cross context VM structure.
 * @param   pMsrNode            The CFGM node with the MSR overrides.
 */
static int cpumR3LoadMsrOverrides(PVM pVM, PCFGMNODE pMsrNode)
{
    for (PCFGMNODE pNode = CFGMR3GetFirstChild(pMsrNode); pNode; pNode = CFGMR3GetNextChild(pNode))
    {
        /*
         * Assemble a valid MSR range.
         */
        CPUMMSRRANGE MsrRange;
        MsrRange.offCpumCpu = 0;
        MsrRange.fReserved  = 0;

        int rc = CFGMR3GetName(pNode, MsrRange.szName, sizeof(MsrRange.szName));
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Invalid MSR entry (name is probably too long): %Rrc\n", rc);

        rc = CFGMR3QueryU32(pNode, "First", &MsrRange.uFirst);
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Invalid MSR entry '%s': Error querying mandatory 'First' value: %Rrc\n",
                              MsrRange.szName, rc);

        rc = CFGMR3QueryU32Def(pNode, "Last", &MsrRange.uLast, MsrRange.uFirst);
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Invalid MSR entry '%s': Error querying 'Last' value: %Rrc\n",
                              MsrRange.szName, rc);

        char szType[32];
        rc = CFGMR3QueryStringDef(pNode, "Type", szType, sizeof(szType), "FixedValue");
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Invalid MSR entry '%s': Error querying 'Type' value: %Rrc\n",
                              MsrRange.szName, rc);
        if (!RTStrICmp(szType, "FixedValue"))
        {
            MsrRange.enmRdFn = kCpumMsrRdFn_FixedValue;
            MsrRange.enmWrFn = kCpumMsrWrFn_IgnoreWrite;

            rc = CFGMR3QueryU64Def(pNode, "Value", &MsrRange.uValue, 0);
            if (RT_FAILURE(rc))
                return VMSetError(pVM, rc, RT_SRC_POS, "Invalid MSR entry '%s': Error querying 'Value' value: %Rrc\n",
                                  MsrRange.szName, rc);

            rc = CFGMR3QueryU64Def(pNode, "WrGpMask", &MsrRange.fWrGpMask, 0);
            if (RT_FAILURE(rc))
                return VMSetError(pVM, rc, RT_SRC_POS, "Invalid MSR entry '%s': Error querying 'WrGpMask' value: %Rrc\n",
                                  MsrRange.szName, rc);

            rc = CFGMR3QueryU64Def(pNode, "WrIgnMask", &MsrRange.fWrIgnMask, 0);
            if (RT_FAILURE(rc))
                return VMSetError(pVM, rc, RT_SRC_POS, "Invalid MSR entry '%s': Error querying 'WrIgnMask' value: %Rrc\n",
                                  MsrRange.szName, rc);
        }
        else
            return VMSetError(pVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                              "Invalid MSR entry '%s': Unknown type '%s'\n", MsrRange.szName, szType);

        /*
         * Insert the range into the table (replaces/splits/shrinks existing
         * MSR ranges).
         */
        rc = cpumR3MsrRangesInsert(NULL /* pVM */, &pVM->cpum.s.GuestInfo.paMsrRangesR3, &pVM->cpum.s.GuestInfo.cMsrRanges,
                                   &MsrRange);
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Error adding MSR entry '%s': %Rrc\n", MsrRange.szName, rc);
    }

    return VINF_SUCCESS;
}


/**
 * Loads CPUID leaf overrides.
 *
 * This must be called before the CPUID leaves are moved from the normal
 * heap to the hyper heap!
 *
 * @returns VBox status code (VMSetError called).
 * @param   pVM             The cross context VM structure.
 * @param   pParentNode     The CFGM node with the CPUID leaves.
 * @param   pszLabel        How to label the overrides we're loading.
 */
static int cpumR3LoadCpuIdOverrides(PVM pVM, PCFGMNODE pParentNode, const char *pszLabel)
{
    for (PCFGMNODE pNode = CFGMR3GetFirstChild(pParentNode); pNode; pNode = CFGMR3GetNextChild(pNode))
    {
        /*
         * Get the leaf and subleaf numbers.
         */
        char szName[128];
        int rc = CFGMR3GetName(pNode, szName, sizeof(szName));
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Invalid %s entry (name is probably too long): %Rrc\n", pszLabel, rc);

        /* The leaf number is either specified directly or thru the node name. */
        uint32_t uLeaf;
        rc = CFGMR3QueryU32(pNode, "Leaf", &uLeaf);
        if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        {
            rc = RTStrToUInt32Full(szName, 16, &uLeaf);
            if (rc != VINF_SUCCESS)
                return VMSetError(pVM, VERR_INVALID_NAME, RT_SRC_POS,
                                  "Invalid %s entry: Invalid leaf number: '%s' \n", pszLabel, szName);
        }
        else if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Invalid %s entry '%s': Error querying 'Leaf' value: %Rrc\n",
                              pszLabel, szName, rc);

        uint32_t uSubLeaf;
        rc = CFGMR3QueryU32Def(pNode, "SubLeaf", &uSubLeaf, 0);
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Invalid %s entry '%s': Error querying 'SubLeaf' value: %Rrc\n",
                              pszLabel, szName, rc);

        uint32_t fSubLeafMask;
        rc = CFGMR3QueryU32Def(pNode, "SubLeafMask", &fSubLeafMask, 0);
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Invalid %s entry '%s': Error querying 'SubLeafMask' value: %Rrc\n",
                              pszLabel, szName, rc);

        /*
         * Look up the specified leaf, since the output register values
         * defaults to any existing values.  This allows overriding a single
         * register, without needing to know the other values.
         */
        PCCPUMCPUIDLEAF pLeaf = cpumR3CpuIdGetExactLeaf(&pVM->cpum.s, uLeaf, uSubLeaf);
        CPUMCPUIDLEAF   Leaf;
        if (pLeaf)
            Leaf = *pLeaf;
        else
            RT_ZERO(Leaf);
        Leaf.uLeaf          = uLeaf;
        Leaf.uSubLeaf       = uSubLeaf;
        Leaf.fSubLeafMask   = fSubLeafMask;

        rc = CFGMR3QueryU32Def(pNode, "eax", &Leaf.uEax, Leaf.uEax);
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Invalid %s entry '%s': Error querying 'eax' value: %Rrc\n",
                              pszLabel, szName, rc);
        rc = CFGMR3QueryU32Def(pNode, "ebx", &Leaf.uEbx, Leaf.uEbx);
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Invalid %s entry '%s': Error querying 'ebx' value: %Rrc\n",
                              pszLabel, szName, rc);
        rc = CFGMR3QueryU32Def(pNode, "ecx", &Leaf.uEcx, Leaf.uEcx);
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Invalid %s entry '%s': Error querying 'ecx' value: %Rrc\n",
                              pszLabel, szName, rc);
        rc = CFGMR3QueryU32Def(pNode, "edx", &Leaf.uEdx, Leaf.uEdx);
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Invalid %s entry '%s': Error querying 'edx' value: %Rrc\n",
                              pszLabel, szName, rc);

        /*
         * Insert the leaf into the table (replaces existing ones).
         */
        rc = cpumR3CpuIdInsert(NULL /* pVM */, &pVM->cpum.s.GuestInfo.paCpuIdLeavesR3, &pVM->cpum.s.GuestInfo.cCpuIdLeaves,
                               &Leaf);
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Error adding CPUID leaf entry '%s': %Rrc\n", szName, rc);
    }

    return VINF_SUCCESS;
}



/**
 * Fetches overrides for a CPUID leaf.
 *
 * @returns VBox status code.
 * @param   pLeaf               The leaf to load the overrides into.
 * @param   pCfgNode            The CFGM node containing the overrides
 *                              (/CPUM/HostCPUID/ or /CPUM/CPUID/).
 * @param   iLeaf               The CPUID leaf number.
 */
static int cpumR3CpuIdFetchLeafOverride(PCPUMCPUID pLeaf, PCFGMNODE pCfgNode, uint32_t iLeaf)
{
    PCFGMNODE pLeafNode = CFGMR3GetChildF(pCfgNode, "%RX32", iLeaf);
    if (pLeafNode)
    {
        uint32_t u32;
        int rc = CFGMR3QueryU32(pLeafNode, "eax", &u32);
        if (RT_SUCCESS(rc))
            pLeaf->uEax = u32;
        else
            AssertReturn(rc == VERR_CFGM_VALUE_NOT_FOUND, rc);

        rc = CFGMR3QueryU32(pLeafNode, "ebx", &u32);
        if (RT_SUCCESS(rc))
            pLeaf->uEbx = u32;
        else
            AssertReturn(rc == VERR_CFGM_VALUE_NOT_FOUND, rc);

        rc = CFGMR3QueryU32(pLeafNode, "ecx", &u32);
        if (RT_SUCCESS(rc))
            pLeaf->uEcx = u32;
        else
            AssertReturn(rc == VERR_CFGM_VALUE_NOT_FOUND, rc);

        rc = CFGMR3QueryU32(pLeafNode, "edx", &u32);
        if (RT_SUCCESS(rc))
            pLeaf->uEdx = u32;
        else
            AssertReturn(rc == VERR_CFGM_VALUE_NOT_FOUND, rc);

    }
    return VINF_SUCCESS;
}


/**
 * Load the overrides for a set of CPUID leaves.
 *
 * @returns VBox status code.
 * @param   paLeaves            The leaf array.
 * @param   cLeaves             The number of leaves.
 * @param   uStart              The start leaf number.
 * @param   pCfgNode            The CFGM node containing the overrides
 *                              (/CPUM/HostCPUID/ or /CPUM/CPUID/).
 */
static int cpumR3CpuIdInitLoadOverrideSet(uint32_t uStart, PCPUMCPUID paLeaves, uint32_t cLeaves, PCFGMNODE pCfgNode)
{
    for (uint32_t i = 0; i < cLeaves; i++)
    {
        int rc = cpumR3CpuIdFetchLeafOverride(&paLeaves[i], pCfgNode, uStart + i);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}


/**
 * Installs the CPUID leaves and explods the data into structures like
 * GuestFeatures and CPUMCTX::aoffXState.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pCpum       The CPUM part of @a VM.
 * @param   paLeaves    The leaves.  These will be copied (but not freed).
 * @param   cLeaves     The number of leaves.
 * @param   pMsrs       The MSRs.
 */
static int cpumR3CpuIdInstallAndExplodeLeaves(PVM pVM, PCPUM pCpum, PCPUMCPUIDLEAF paLeaves, uint32_t cLeaves, PCCPUMMSRS pMsrs)
{
# ifdef VBOX_STRICT
    cpumCpuIdAssertOrder(paLeaves, cLeaves);
# endif

    /*
     * Install the CPUID information.
     */
    AssertLogRelMsgReturn(cLeaves <= RT_ELEMENTS(pVM->cpum.s.GuestInfo.aCpuIdLeaves),
                          ("cLeaves=%u - max %u\n", cLeaves, RT_ELEMENTS(pVM->cpum.s.GuestInfo.aCpuIdLeaves)),
                          VERR_CPUM_IPE_1); /** @todo better status! */
    if (paLeaves != pCpum->GuestInfo.aCpuIdLeaves)
        memcpy(pCpum->GuestInfo.aCpuIdLeaves, paLeaves, cLeaves * sizeof(paLeaves[0]));
    pCpum->GuestInfo.paCpuIdLeavesR3 = pCpum->GuestInfo.aCpuIdLeaves;
    pCpum->GuestInfo.cCpuIdLeaves    = cLeaves;

    /*
     * Update the default CPUID leaf if necessary.
     */
    switch (pCpum->GuestInfo.enmUnknownCpuIdMethod)
    {
        case CPUMUNKNOWNCPUID_LAST_STD_LEAF:
        case CPUMUNKNOWNCPUID_LAST_STD_LEAF_WITH_ECX:
        {
            /* We don't use CPUID(0).eax here because of the NT hack that only
               changes that value without actually removing any leaves. */
            uint32_t i = 0;
            if (   pCpum->GuestInfo.cCpuIdLeaves > 0
                && pCpum->GuestInfo.paCpuIdLeavesR3[0].uLeaf <= UINT32_C(0xff))
            {
                while (   i + 1 < pCpum->GuestInfo.cCpuIdLeaves
                       && pCpum->GuestInfo.paCpuIdLeavesR3[i + 1].uLeaf <= UINT32_C(0xff))
                    i++;
                pCpum->GuestInfo.DefCpuId.uEax = pCpum->GuestInfo.paCpuIdLeavesR3[i].uEax;
                pCpum->GuestInfo.DefCpuId.uEbx = pCpum->GuestInfo.paCpuIdLeavesR3[i].uEbx;
                pCpum->GuestInfo.DefCpuId.uEcx = pCpum->GuestInfo.paCpuIdLeavesR3[i].uEcx;
                pCpum->GuestInfo.DefCpuId.uEdx = pCpum->GuestInfo.paCpuIdLeavesR3[i].uEdx;
            }
            break;
        }
        default:
            break;
    }

    /*
     * Explode the guest CPU features.
     */
    int rc = cpumCpuIdExplodeFeaturesX86(pCpum->GuestInfo.paCpuIdLeavesR3, pCpum->GuestInfo.cCpuIdLeaves, pMsrs,
                                         &pCpum->GuestFeatures);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Adjust the scalable bus frequency according to the CPUID information
     * we're now using.
     */
    if (CPUMMICROARCH_IS_INTEL_CORE7(pVM->cpum.s.GuestFeatures.enmMicroarch))
        pCpum->GuestInfo.uScalableBusFreq = pCpum->GuestFeatures.enmMicroarch >= kCpumMicroarch_Intel_Core7_SandyBridge
                                          ? UINT64_C(100000000)  /* 100MHz */
                                          : UINT64_C(133333333); /* 133MHz */

    /*
     * Populate the legacy arrays.  Currently used for everything, later only
     * for patch manager.
     */
    struct { PCPUMCPUID paCpuIds; uint32_t cCpuIds, uBase; } aOldRanges[] =
    {
        { pCpum->aGuestCpuIdPatmStd,        RT_ELEMENTS(pCpum->aGuestCpuIdPatmStd),     0x00000000 },
        { pCpum->aGuestCpuIdPatmExt,        RT_ELEMENTS(pCpum->aGuestCpuIdPatmExt),     0x80000000 },
        { pCpum->aGuestCpuIdPatmCentaur,    RT_ELEMENTS(pCpum->aGuestCpuIdPatmCentaur), 0xc0000000 },
    };
    for (uint32_t i = 0; i < RT_ELEMENTS(aOldRanges); i++)
    {
        uint32_t    cLeft       = aOldRanges[i].cCpuIds;
        uint32_t    uLeaf       = aOldRanges[i].uBase + cLeft;
        PCPUMCPUID  pLegacyLeaf = &aOldRanges[i].paCpuIds[cLeft];
        while (cLeft-- > 0)
        {
            uLeaf--;
            pLegacyLeaf--;

            PCCPUMCPUIDLEAF pLeaf = cpumR3CpuIdGetExactLeaf(pCpum, uLeaf, 0 /* uSubLeaf */);
            if (pLeaf)
            {
                pLegacyLeaf->uEax = pLeaf->uEax;
                pLegacyLeaf->uEbx = pLeaf->uEbx;
                pLegacyLeaf->uEcx = pLeaf->uEcx;
                pLegacyLeaf->uEdx = pLeaf->uEdx;
            }
            else
                *pLegacyLeaf = pCpum->GuestInfo.DefCpuId;
        }
    }

    /*
     * Configure XSAVE offsets according to the CPUID info and set the feature flags.
     */
    PVMCPU pVCpu0 = pVM->apCpusR3[0];
    AssertCompile(sizeof(pVCpu0->cpum.s.Guest.abXState) == CPUM_MAX_XSAVE_AREA_SIZE);
    memset(&pVCpu0->cpum.s.Guest.aoffXState[0], 0xff, sizeof(pVCpu0->cpum.s.Guest.aoffXState));
    pVCpu0->cpum.s.Guest.aoffXState[XSAVE_C_X87_BIT] = 0;
    pVCpu0->cpum.s.Guest.aoffXState[XSAVE_C_SSE_BIT] = 0;
    for (uint32_t iComponent = XSAVE_C_SSE_BIT + 1; iComponent < 63; iComponent++)
        if (pCpum->fXStateGuestMask & RT_BIT_64(iComponent))
        {
            PCPUMCPUIDLEAF pSubLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 0xd, iComponent);
            AssertLogRelMsgReturn(pSubLeaf, ("iComponent=%#x\n", iComponent), VERR_CPUM_IPE_1);
            AssertLogRelMsgReturn(pSubLeaf->fSubLeafMask >= iComponent, ("iComponent=%#x\n", iComponent), VERR_CPUM_IPE_1);
            AssertLogRelMsgReturn(   pSubLeaf->uEax > 0
                                  && pSubLeaf->uEbx >= CPUM_MIN_XSAVE_AREA_SIZE
                                  && pSubLeaf->uEax <= pCpum->GuestFeatures.cbMaxExtendedState
                                  && pSubLeaf->uEbx <= pCpum->GuestFeatures.cbMaxExtendedState
                                  && pSubLeaf->uEbx + pSubLeaf->uEax <= pCpum->GuestFeatures.cbMaxExtendedState,
                                  ("iComponent=%#x eax=%#x ebx=%#x cbMax=%#x\n", iComponent, pSubLeaf->uEax, pSubLeaf->uEbx,
                                   pCpum->GuestFeatures.cbMaxExtendedState),
                                  VERR_CPUM_IPE_1);
            pVCpu0->cpum.s.Guest.aoffXState[iComponent] = pSubLeaf->uEbx;
        }

    /* Copy the CPU #0  data to the other CPUs. */
    for (VMCPUID idCpu = 1; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        memcpy(&pVCpu->cpum.s.Guest.aoffXState[0], &pVCpu0->cpum.s.Guest.aoffXState[0], sizeof(pVCpu0->cpum.s.Guest.aoffXState));
    }

    return VINF_SUCCESS;
}


/** @name Instruction Set Extension Options
 * @{  */
/** Configuration option type (extended boolean, really). */
typedef uint8_t CPUMISAEXTCFG;
/** Always disable the extension. */
#define CPUMISAEXTCFG_DISABLED              false
/** Enable the extension if it's supported by the host CPU. */
#define CPUMISAEXTCFG_ENABLED_SUPPORTED     true
/** Enable the extension if it's supported by the host CPU or when on ARM64. */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# define CPUMISAEXTCFG_ENABLED_SUPPORTED_OR_NOT_AMD64   CPUMISAEXTCFG_ENABLED_SUPPORTED
#else
# define CPUMISAEXTCFG_ENABLED_SUPPORTED_OR_NOT_AMD64   CPUMISAEXTCFG_ENABLED_ALWAYS
#endif
/** Enable the extension if it's supported by the host CPU, but don't let
 * the portable CPUID feature disable it. */
#define CPUMISAEXTCFG_ENABLED_PORTABLE      UINT8_C(127)
/** Always enable the extension. */
#define CPUMISAEXTCFG_ENABLED_ALWAYS        UINT8_C(255)
/** @} */

/**
 * CPUID Configuration (from CFGM).
 *
 * @remarks  The members aren't document since we would only be duplicating the
 *           \@cfgm entries in cpumR3CpuIdReadConfig.
 */
typedef struct CPUMCPUIDCONFIG
{
    bool            fNt4LeafLimit;
    bool            fInvariantTsc;
    bool            fInvariantApic;
    bool            fForceVme;
    bool            fNestedHWVirt;

    CPUMISAEXTCFG   enmCmpXchg16b;
    CPUMISAEXTCFG   enmMonitor;
    CPUMISAEXTCFG   enmMWaitExtensions;
    CPUMISAEXTCFG   enmSse41;
    CPUMISAEXTCFG   enmSse42;
    CPUMISAEXTCFG   enmAvx;
    CPUMISAEXTCFG   enmAvx2;
    CPUMISAEXTCFG   enmXSave;
    CPUMISAEXTCFG   enmAesNi;
    CPUMISAEXTCFG   enmPClMul;
    CPUMISAEXTCFG   enmPopCnt;
    CPUMISAEXTCFG   enmMovBe;
    CPUMISAEXTCFG   enmRdRand;
    CPUMISAEXTCFG   enmRdSeed;
    CPUMISAEXTCFG   enmSha;
    CPUMISAEXTCFG   enmAdx;
    CPUMISAEXTCFG   enmCLFlushOpt;
    CPUMISAEXTCFG   enmFsGsBase;
    CPUMISAEXTCFG   enmPcid;
    CPUMISAEXTCFG   enmInvpcid;
    CPUMISAEXTCFG   enmFlushCmdMsr;
    CPUMISAEXTCFG   enmMdsClear;
    CPUMISAEXTCFG   enmArchCapMsr;
    CPUMISAEXTCFG   enmFma;
    CPUMISAEXTCFG   enmF16c;
    CPUMISAEXTCFG   enmMcdtNo;
    CPUMISAEXTCFG   enmMonitorMitgNo;

    CPUMISAEXTCFG   enmAbm;
    CPUMISAEXTCFG   enmSse4A;
    CPUMISAEXTCFG   enmMisAlnSse;
    CPUMISAEXTCFG   enm3dNowPrf;
    CPUMISAEXTCFG   enmAmdExtMmx;

    uint32_t        uMaxStdLeaf;
    uint32_t        uMaxExtLeaf;
    uint32_t        uMaxCentaurLeaf;
    uint32_t        uMaxIntelFamilyModelStep;
    char            szCpuName[128];
} CPUMCPUIDCONFIG;
/** Pointer to CPUID config (from CFGM). */
typedef CPUMCPUIDCONFIG *PCPUMCPUIDCONFIG;


/**
 * Mini CPU selection support for making Mac OS X happy.
 *
 * Executes the  /CPUM/MaxIntelFamilyModelStep config.
 *
 * @param   pCpum       The CPUM instance data.
 * @param   pConfig     The CPUID configuration we've read from CFGM.
 */
static void cpumR3CpuIdLimitIntelFamModStep(PCPUM pCpum, PCPUMCPUIDCONFIG pConfig)
{
    if (pCpum->GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_INTEL)
    {
        PCPUMCPUIDLEAF pStdFeatureLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 1, 0);
        uint32_t uCurIntelFamilyModelStep = RT_MAKE_U32_FROM_U8(RTX86GetCpuStepping(pStdFeatureLeaf->uEax),
                                                                RTX86GetCpuModelIntel(pStdFeatureLeaf->uEax),
                                                                RTX86GetCpuFamily(pStdFeatureLeaf->uEax),
                                                                0);
        uint32_t uMaxIntelFamilyModelStep = pConfig->uMaxIntelFamilyModelStep;
        if (pConfig->uMaxIntelFamilyModelStep < uCurIntelFamilyModelStep)
        {
            uint32_t uNew = pStdFeatureLeaf->uEax & UINT32_C(0xf0003000);
            uNew |= RT_BYTE1(uMaxIntelFamilyModelStep) & 0xf; /* stepping */
            uNew |= (RT_BYTE2(uMaxIntelFamilyModelStep) & 0xf) << 4; /* 4 low model bits */
            uNew |= (RT_BYTE2(uMaxIntelFamilyModelStep) >> 4) << 16; /* 4 high model bits */
            uNew |= (RT_BYTE3(uMaxIntelFamilyModelStep) & 0xf) << 8; /* 4 low family bits */
            if (RT_BYTE3(uMaxIntelFamilyModelStep) > 0xf) /* 8 high family bits, using intel's suggested calculation. */
                uNew |= ( (RT_BYTE3(uMaxIntelFamilyModelStep) - (RT_BYTE3(uMaxIntelFamilyModelStep) & 0xf)) & 0xff ) << 20;
            LogRel(("CPU: CPUID(0).EAX %#x -> %#x (uMaxIntelFamilyModelStep=%#x, uCurIntelFamilyModelStep=%#x\n",
                    pStdFeatureLeaf->uEax, uNew, uMaxIntelFamilyModelStep, uCurIntelFamilyModelStep));
            pStdFeatureLeaf->uEax = uNew;
        }
    }
}



/**
 * Limit it the number of entries, zapping the remainder.
 *
 * The limits are masking off stuff about power saving and similar, this
 * is perhaps a bit crudely done as there is probably some relatively harmless
 * info too in these leaves (like words about having a constant TSC).
 *
 * @param   pCpum       The CPUM instance data.
 * @param   pConfig     The CPUID configuration we've read from CFGM.
 */
static void cpumR3CpuIdLimitLeaves(PCPUM pCpum, PCPUMCPUIDCONFIG pConfig)
{
    /*
     * Standard leaves.
     */
    uint32_t       uSubLeaf = 0;
    PCPUMCPUIDLEAF pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 0, uSubLeaf);
    if (pCurLeaf)
    {
        uint32_t uLimit = pCurLeaf->uEax;
        if (uLimit <= UINT32_C(0x000fffff))
        {
            if (uLimit > pConfig->uMaxStdLeaf)
            {
                pCurLeaf->uEax = uLimit = pConfig->uMaxStdLeaf;
                cpumR3CpuIdRemoveRange(pCpum->GuestInfo.paCpuIdLeavesR3, &pCpum->GuestInfo.cCpuIdLeaves,
                                       uLimit + 1, UINT32_C(0x000fffff));
            }

            /* NT4 hack, no zapping of extra leaves here. */
            if (pConfig->fNt4LeafLimit && uLimit > 3)
                pCurLeaf->uEax = uLimit = 3;

            while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0x00000000), ++uSubLeaf)) != NULL)
                pCurLeaf->uEax = uLimit;
        }
        else
        {
            LogRel(("CPUID: Invalid standard range: %#x\n", uLimit));
            cpumR3CpuIdRemoveRange(pCpum->GuestInfo.paCpuIdLeavesR3, &pCpum->GuestInfo.cCpuIdLeaves,
                                   UINT32_C(0x00000000), UINT32_C(0x0fffffff));
        }
    }

    /*
     * Extended leaves.
     */
    uSubLeaf = 0;
    pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0x80000000), uSubLeaf);
    if (pCurLeaf)
    {
        uint32_t uLimit = pCurLeaf->uEax;
        if (   uLimit >= UINT32_C(0x80000000)
            && uLimit <= UINT32_C(0x800fffff))
        {
            if (uLimit > pConfig->uMaxExtLeaf)
            {
                pCurLeaf->uEax = uLimit = pConfig->uMaxExtLeaf;
                cpumR3CpuIdRemoveRange(pCpum->GuestInfo.paCpuIdLeavesR3, &pCpum->GuestInfo.cCpuIdLeaves,
                                       uLimit + 1, UINT32_C(0x800fffff));
                while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0x80000000), ++uSubLeaf)) != NULL)
                    pCurLeaf->uEax = uLimit;
            }
        }
        else
        {
            LogRel(("CPUID: Invalid extended range: %#x\n", uLimit));
            cpumR3CpuIdRemoveRange(pCpum->GuestInfo.paCpuIdLeavesR3, &pCpum->GuestInfo.cCpuIdLeaves,
                                   UINT32_C(0x80000000), UINT32_C(0x8ffffffd));
        }
    }

    /*
     * Centaur leaves (VIA).
     */
    uSubLeaf = 0;
    pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0xc0000000), uSubLeaf);
    if (pCurLeaf)
    {
        uint32_t uLimit = pCurLeaf->uEax;
        if (   uLimit >= UINT32_C(0xc0000000)
            && uLimit <= UINT32_C(0xc00fffff))
        {
            if (uLimit > pConfig->uMaxCentaurLeaf)
            {
                pCurLeaf->uEax = uLimit = pConfig->uMaxCentaurLeaf;
                cpumR3CpuIdRemoveRange(pCpum->GuestInfo.paCpuIdLeavesR3, &pCpum->GuestInfo.cCpuIdLeaves,
                                       uLimit + 1, UINT32_C(0xcfffffff));
                while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0xc0000000), ++uSubLeaf)) != NULL)
                    pCurLeaf->uEax = uLimit;
            }
        }
        else
        {
            LogRel(("CPUID: Invalid centaur range: %#x\n", uLimit));
            cpumR3CpuIdRemoveRange(pCpum->GuestInfo.paCpuIdLeavesR3, &pCpum->GuestInfo.cCpuIdLeaves,
                                   UINT32_C(0xc0000000), UINT32_C(0xcfffffff));
        }
    }
}


/**
 * Clears a CPUID leaf and all sub-leaves (to zero).
 *
 * @param   pCpum       The CPUM instance data.
 * @param   uLeaf       The leaf to clear.
 */
static void cpumR3CpuIdZeroLeaf(PCPUM pCpum, uint32_t uLeaf)
{
    uint32_t       uSubLeaf = 0;
    PCPUMCPUIDLEAF pCurLeaf;
    while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, uLeaf, uSubLeaf)) != NULL)
    {
        pCurLeaf->uEax = 0;
        pCurLeaf->uEbx = 0;
        pCurLeaf->uEcx = 0;
        pCurLeaf->uEdx = 0;
        uSubLeaf++;
    }
}


/**
 * Used by cpumR3CpuIdSanitize to ensure that we don't have any sub-leaves for
 * the given leaf.
 *
 * @returns pLeaf.
 * @param   pCpum       The CPUM instance data.
 * @param   pLeaf       The leaf to ensure is alone with it's EAX input value.
 */
static PCPUMCPUIDLEAF cpumR3CpuIdMakeSingleLeaf(PCPUM pCpum, PCPUMCPUIDLEAF pLeaf)
{
    Assert((uintptr_t)(pLeaf - pCpum->GuestInfo.paCpuIdLeavesR3) < pCpum->GuestInfo.cCpuIdLeaves);
    if (pLeaf->fSubLeafMask != 0)
    {
        /*
         * Figure out how many sub-leaves in need of removal (we'll keep the first).
         * Log everything while we're at it.
         */
        LogRel(("CPUM:\n"
                "CPUM: Unexpected CPUID sub-leaves for leaf %#x; fSubLeafMask=%#x\n", pLeaf->uLeaf, pLeaf->fSubLeafMask));
        PCPUMCPUIDLEAF  pLast    = &pCpum->GuestInfo.paCpuIdLeavesR3[pCpum->GuestInfo.cCpuIdLeaves - 1];
        PCPUMCPUIDLEAF  pSubLeaf = pLeaf;
        for (;;)
        {
            LogRel(("CPUM: %08x/%08x: %08x %08x %08x %08x; flags=%#x mask=%#x\n",
                    pSubLeaf->uLeaf, pSubLeaf->uSubLeaf,
                    pSubLeaf->uEax, pSubLeaf->uEbx, pSubLeaf->uEcx, pSubLeaf->uEdx,
                    pSubLeaf->fFlags, pSubLeaf->fSubLeafMask));
            if (pSubLeaf == pLast || pSubLeaf[1].uLeaf != pLeaf->uLeaf)
                break;
            pSubLeaf++;
        }
        LogRel(("CPUM:\n"));

        /*
         * Remove the offending sub-leaves.
         */
        if (pSubLeaf != pLeaf)
        {
            if (pSubLeaf != pLast)
                memmove(pLeaf + 1, pSubLeaf + 1, (uintptr_t)pLast - (uintptr_t)pSubLeaf);
            pCpum->GuestInfo.cCpuIdLeaves -= (uint32_t)(pSubLeaf - pLeaf);
        }

        /*
         * Convert the first sub-leaf into a single leaf.
         */
        pLeaf->uSubLeaf     = 0;
        pLeaf->fSubLeafMask = 0;
    }
    return pLeaf;
}


/**
 * Sanitizes and adjust the CPUID leaves.
 *
 * Drop features that aren't virtualized (or virtualizable).  Adjust information
 * and capabilities to fit the virtualized hardware.  Remove information the
 * guest shouldn't have (because it's wrong in the virtual world or because it
 * gives away host details) or that we don't have documentation for and no idea
 * what means.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure (for cCpus).
 * @param   pCpum       The CPUM instance data.
 * @param   pConfig     The CPUID configuration we've read from CFGM.
 */
static int cpumR3CpuIdSanitize(PVM pVM, PCPUM pCpum, PCPUMCPUIDCONFIG pConfig)
{
#define PORTABLE_CLEAR_BITS_WHEN(Lvl, a_pLeafReg, FeatNm, fMask, uValue) \
    if ( pCpum->u8PortableCpuIdLevel >= (Lvl) && ((a_pLeafReg) & (fMask)) == (uValue) ) \
    { \
        LogRel(("PortableCpuId: " #a_pLeafReg "[" #FeatNm "]: %#x -> 0\n", (a_pLeafReg) & (fMask))); \
        (a_pLeafReg) &= ~(uint32_t)(fMask); \
    }
#define PORTABLE_DISABLE_FEATURE_BIT(Lvl, a_pLeafReg, FeatNm, fBitMask) \
    if ( pCpum->u8PortableCpuIdLevel >= (Lvl) && ((a_pLeafReg) & (fBitMask)) ) \
    { \
        LogRel(("PortableCpuId: " #a_pLeafReg "[" #FeatNm "]: 1 -> 0\n")); \
        (a_pLeafReg) &= ~(uint32_t)(fBitMask); \
    }
#define PORTABLE_DISABLE_FEATURE_BIT_CFG(Lvl, a_pLeafReg, FeatNm, fBitMask, enmConfig) \
    if (   pCpum->u8PortableCpuIdLevel >= (Lvl) \
        && ((a_pLeafReg) & (fBitMask)) \
        && (enmConfig) != CPUMISAEXTCFG_ENABLED_PORTABLE ) \
    { \
        LogRel(("PortableCpuId: " #a_pLeafReg "[" #FeatNm "]: 1 -> 0\n")); \
        (a_pLeafReg) &= ~(uint32_t)(fBitMask); \
    }
    Assert(pCpum->GuestFeatures.enmCpuVendor != CPUMCPUVENDOR_INVALID);

    /* The CPUID entries we start with here isn't necessarily the ones of the host, so we
       must consult HostFeatures when processing CPUMISAEXTCFG variables. */
    PCCPUMFEATURES pHstFeat = &pCpum->HostFeatures;
#define PASSTHRU_FEATURE(enmConfig, fHostFeature, fConst) \
    ((enmConfig) && ((enmConfig) == CPUMISAEXTCFG_ENABLED_ALWAYS || (fHostFeature)) ? (fConst) : 0)
#define PASSTHRU_FEATURE_EX(enmConfig, fHostFeature, fAndExpr, fConst) \
    ((enmConfig) && ((enmConfig) == CPUMISAEXTCFG_ENABLED_ALWAYS || (fHostFeature)) && (fAndExpr) ? (fConst) : 0)
#define PASSTHRU_FEATURE_TODO(enmConfig, fConst) ((enmConfig) ? (fConst) : 0)

    /* Cpuid 1:
     * EAX: CPU model, family and stepping.
     *
     * ECX + EDX: Supported features.  Only report features we can support.
     * Note! When enabling new features the Synthetic CPU and Portable CPUID
     *       options may require adjusting (i.e. stripping what was enabled).
     *
     * EBX: Branding, CLFLUSH line size, logical processors per package and
     *      initial APIC ID.
     */
    PCPUMCPUIDLEAF pStdFeatureLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 1, 0); /* Note! Must refetch when used later. */
    AssertLogRelReturn(pStdFeatureLeaf, VERR_CPUM_IPE_2);
    pStdFeatureLeaf = cpumR3CpuIdMakeSingleLeaf(pCpum, pStdFeatureLeaf);

    pStdFeatureLeaf->uEdx &= X86_CPUID_FEATURE_EDX_FPU
                           | X86_CPUID_FEATURE_EDX_VME
                           | X86_CPUID_FEATURE_EDX_DE
                           | X86_CPUID_FEATURE_EDX_PSE
                           | X86_CPUID_FEATURE_EDX_TSC
                           | X86_CPUID_FEATURE_EDX_MSR
                           //| X86_CPUID_FEATURE_EDX_PAE   - set later if configured.
                           | X86_CPUID_FEATURE_EDX_MCE
                           | X86_CPUID_FEATURE_EDX_CX8
                           //| X86_CPUID_FEATURE_EDX_APIC  - set by the APIC device if present.
                           //| RT_BIT_32(10)               - not defined
                           | X86_CPUID_FEATURE_EDX_SEP
                           | X86_CPUID_FEATURE_EDX_MTRR
                           | X86_CPUID_FEATURE_EDX_PGE
                           | X86_CPUID_FEATURE_EDX_MCA
                           | X86_CPUID_FEATURE_EDX_CMOV
                           | X86_CPUID_FEATURE_EDX_PAT     /* 16 */
                           | X86_CPUID_FEATURE_EDX_PSE36
                           //| X86_CPUID_FEATURE_EDX_PSN   - no serial number.
                           | X86_CPUID_FEATURE_EDX_CLFSH
                           //| RT_BIT_32(20)               - not defined
                           //| X86_CPUID_FEATURE_EDX_DS    - no debug store.
                           //| X86_CPUID_FEATURE_EDX_ACPI  - not supported (not DevAcpi, right?).
                           | X86_CPUID_FEATURE_EDX_MMX
                           | X86_CPUID_FEATURE_EDX_FXSR
                           | X86_CPUID_FEATURE_EDX_SSE
                           | X86_CPUID_FEATURE_EDX_SSE2
                           //| X86_CPUID_FEATURE_EDX_SS    - no self snoop.
                           | X86_CPUID_FEATURE_EDX_HTT
                           //| X86_CPUID_FEATURE_EDX_TM    - no thermal monitor.
                           //| RT_BIT_32(30)               - not defined
                           //| X86_CPUID_FEATURE_EDX_PBE   - no pending break enabled.
                           ;
    pStdFeatureLeaf->uEcx &= X86_CPUID_FEATURE_ECX_SSE3
                           | PASSTHRU_FEATURE_TODO(pConfig->enmPClMul, X86_CPUID_FEATURE_ECX_PCLMUL)
                           //| X86_CPUID_FEATURE_ECX_DTES64 - not implemented yet.
                           /* Can't properly emulate monitor & mwait with guest SMP; force the guest to use hlt for idling VCPUs. */
                           | PASSTHRU_FEATURE_EX(pConfig->enmMonitor, pHstFeat->fMonitorMWait, pVM->cCpus == 1, X86_CPUID_FEATURE_ECX_MONITOR)
                           //| X86_CPUID_FEATURE_ECX_CPLDS - no CPL qualified debug store.
                           | (pConfig->fNestedHWVirt ? X86_CPUID_FEATURE_ECX_VMX : 0)
                           //| X86_CPUID_FEATURE_ECX_SMX   - not virtualized yet.
                           //| X86_CPUID_FEATURE_ECX_EST   - no extended speed step.
                           //| X86_CPUID_FEATURE_ECX_TM2   - no thermal monitor 2.
                           | X86_CPUID_FEATURE_ECX_SSSE3
                           //| X86_CPUID_FEATURE_ECX_CNTXID - no L1 context id (MSR++).
                           | PASSTHRU_FEATURE(pConfig->enmFma, pHstFeat->fFma, X86_CPUID_FEATURE_ECX_FMA)
                           | PASSTHRU_FEATURE(pConfig->enmCmpXchg16b, pHstFeat->fCmpXchg16b, X86_CPUID_FEATURE_ECX_CX16)
                           /* ECX Bit 14 - xTPR Update Control. Processor supports changing IA32_MISC_ENABLES[bit 23]. */
                           //| X86_CPUID_FEATURE_ECX_TPRUPDATE
                           //| X86_CPUID_FEATURE_ECX_PDCM  - not implemented yet.
                           | PASSTHRU_FEATURE(pConfig->enmPcid, pHstFeat->fPcid, X86_CPUID_FEATURE_ECX_PCID)
                           //| X86_CPUID_FEATURE_ECX_DCA   - not implemented yet.
                           | PASSTHRU_FEATURE(pConfig->enmSse41, pHstFeat->fSse41, X86_CPUID_FEATURE_ECX_SSE4_1)
                           | PASSTHRU_FEATURE(pConfig->enmSse42, pHstFeat->fSse42, X86_CPUID_FEATURE_ECX_SSE4_2)
                           //| X86_CPUID_FEATURE_ECX_X2APIC - turned on later by the device if enabled.
                           | PASSTHRU_FEATURE(pConfig->enmMovBe, pHstFeat->fMovBe, X86_CPUID_FEATURE_ECX_MOVBE)
                           | PASSTHRU_FEATURE(pConfig->enmPopCnt, pHstFeat->fPopCnt, X86_CPUID_FEATURE_ECX_POPCNT)
                           //| X86_CPUID_FEATURE_ECX_TSCDEADL - not implemented yet.
                           | PASSTHRU_FEATURE_TODO(pConfig->enmAesNi, X86_CPUID_FEATURE_ECX_AES)
                           | PASSTHRU_FEATURE(pConfig->enmXSave, pHstFeat->fXSaveRstor, X86_CPUID_FEATURE_ECX_XSAVE)
                           //| X86_CPUID_FEATURE_ECX_OSXSAVE - mirrors CR4.OSXSAVE state, set dynamically.
                           | PASSTHRU_FEATURE(pConfig->enmAvx, pHstFeat->fAvx, X86_CPUID_FEATURE_ECX_AVX)
                           | PASSTHRU_FEATURE(pConfig->enmF16c, pHstFeat->fF16c, X86_CPUID_FEATURE_ECX_F16C)
                           | PASSTHRU_FEATURE_TODO(pConfig->enmRdRand, X86_CPUID_FEATURE_ECX_RDRAND)
                           //| X86_CPUID_FEATURE_ECX_HVP   - Set explicitly later.
                           ;

    /* Mask out PCID unless FSGSBASE is exposed due to a bug in Windows 10 SMP guests, see @bugref{9089#c15}. */
    if (   !pVM->cpum.s.GuestFeatures.fFsGsBase
        && (pStdFeatureLeaf->uEcx & X86_CPUID_FEATURE_ECX_PCID))
    {
        pStdFeatureLeaf->uEcx &= ~X86_CPUID_FEATURE_ECX_PCID;
        LogRel(("CPUM: Disabled PCID without FSGSBASE to workaround buggy guests\n"));
    }

    if (pCpum->u8PortableCpuIdLevel > 0)
    {
        PORTABLE_CLEAR_BITS_WHEN(1, pStdFeatureLeaf->uEax, ProcessorType, (UINT32_C(3) << 12), (UINT32_C(2) << 12));
        PORTABLE_DISABLE_FEATURE_BIT(    1, pStdFeatureLeaf->uEcx, SSSE3,  X86_CPUID_FEATURE_ECX_SSSE3);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pStdFeatureLeaf->uEcx, PCID,   X86_CPUID_FEATURE_ECX_PCID,   pConfig->enmPcid);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pStdFeatureLeaf->uEcx, SSE4_1, X86_CPUID_FEATURE_ECX_SSE4_1, pConfig->enmSse41);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pStdFeatureLeaf->uEcx, SSE4_2, X86_CPUID_FEATURE_ECX_SSE4_2, pConfig->enmSse42);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pStdFeatureLeaf->uEcx, MOVBE,  X86_CPUID_FEATURE_ECX_MOVBE,  pConfig->enmMovBe);
        PORTABLE_DISABLE_FEATURE_BIT(    1, pStdFeatureLeaf->uEcx, AES,    X86_CPUID_FEATURE_ECX_AES);
        PORTABLE_DISABLE_FEATURE_BIT(    1, pStdFeatureLeaf->uEcx, VMX,    X86_CPUID_FEATURE_ECX_VMX);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pStdFeatureLeaf->uEcx, PCLMUL, X86_CPUID_FEATURE_ECX_PCLMUL, pConfig->enmPClMul);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pStdFeatureLeaf->uEcx, POPCNT, X86_CPUID_FEATURE_ECX_POPCNT, pConfig->enmPopCnt);
        PORTABLE_DISABLE_FEATURE_BIT(    1, pStdFeatureLeaf->uEcx, F16C,   X86_CPUID_FEATURE_ECX_F16C);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pStdFeatureLeaf->uEcx, XSAVE,  X86_CPUID_FEATURE_ECX_XSAVE,  pConfig->enmXSave);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pStdFeatureLeaf->uEcx, AVX,    X86_CPUID_FEATURE_ECX_AVX,    pConfig->enmAvx);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pStdFeatureLeaf->uEcx, RDRAND, X86_CPUID_FEATURE_ECX_RDRAND, pConfig->enmRdRand);
        PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pStdFeatureLeaf->uEcx, CX16,   X86_CPUID_FEATURE_ECX_CX16,   pConfig->enmCmpXchg16b);
        PORTABLE_DISABLE_FEATURE_BIT(    2, pStdFeatureLeaf->uEcx, SSE3,   X86_CPUID_FEATURE_ECX_SSE3);
        PORTABLE_DISABLE_FEATURE_BIT(    3, pStdFeatureLeaf->uEdx, SSE2,   X86_CPUID_FEATURE_EDX_SSE2);
        PORTABLE_DISABLE_FEATURE_BIT(    3, pStdFeatureLeaf->uEdx, SSE,    X86_CPUID_FEATURE_EDX_SSE);
        PORTABLE_DISABLE_FEATURE_BIT(    3, pStdFeatureLeaf->uEdx, CLFSH,  X86_CPUID_FEATURE_EDX_CLFSH);
        PORTABLE_DISABLE_FEATURE_BIT(    3, pStdFeatureLeaf->uEdx, CMOV,   X86_CPUID_FEATURE_EDX_CMOV);

        Assert(!(pStdFeatureLeaf->uEdx & (  X86_CPUID_FEATURE_EDX_SEP ///??
                                          | X86_CPUID_FEATURE_EDX_PSN
                                          | X86_CPUID_FEATURE_EDX_DS
                                          | X86_CPUID_FEATURE_EDX_ACPI
                                          | X86_CPUID_FEATURE_EDX_SS
                                          | X86_CPUID_FEATURE_EDX_TM
                                          | X86_CPUID_FEATURE_EDX_PBE
                                          )));
        Assert(!(pStdFeatureLeaf->uEcx & (  X86_CPUID_FEATURE_ECX_DTES64
                                          | X86_CPUID_FEATURE_ECX_CPLDS
                                          | X86_CPUID_FEATURE_ECX_AES
                                          | X86_CPUID_FEATURE_ECX_VMX
                                          | X86_CPUID_FEATURE_ECX_SMX
                                          | X86_CPUID_FEATURE_ECX_EST
                                          | X86_CPUID_FEATURE_ECX_TM2
                                          | X86_CPUID_FEATURE_ECX_CNTXID
                                          | X86_CPUID_FEATURE_ECX_FMA
                                          | X86_CPUID_FEATURE_ECX_TPRUPDATE
                                          | X86_CPUID_FEATURE_ECX_PDCM
                                          | X86_CPUID_FEATURE_ECX_DCA
                                          | X86_CPUID_FEATURE_ECX_OSXSAVE
                                          )));
    }

    /* Set up APIC ID for CPU 0, configure multi core/threaded smp. */
    pStdFeatureLeaf->uEbx &= UINT32_C(0x0000ffff); /* (APIC-ID := 0 and #LogCpus := 0) */

    /* The HTT bit is architectural and does not directly indicate hyper-threading or multiple cores;
     * it was set even on single-core/non-HT Northwood P4s for example. The HTT bit only means that the
     * information in EBX[23:16] (max number of addressable logical processor IDs) is valid.
     */
#ifdef VBOX_WITH_MULTI_CORE
    if (pVM->cCpus > 1)
        pStdFeatureLeaf->uEdx |= X86_CPUID_FEATURE_EDX_HTT;  /* Force if emulating a multi-core CPU. */
#endif
    if (pStdFeatureLeaf->uEdx & X86_CPUID_FEATURE_EDX_HTT)
    {
        /* If CPUID Fn0000_0001_EDX[HTT] = 1 then LogicalProcessorCount is the number of threads per CPU
           core times the number of CPU cores per processor */
#ifdef VBOX_WITH_MULTI_CORE
        pStdFeatureLeaf->uEbx |= pVM->cCpus <= 0xff ? (pVM->cCpus << 16) : UINT32_C(0x00ff0000);
#else
        /* Single logical processor in a package. */
        pStdFeatureLeaf->uEbx |= (1 << 16);
#endif
    }

    uint32_t uMicrocodeRev;
    int rc = SUPR3QueryMicrocodeRev(&uMicrocodeRev);
    if (RT_SUCCESS(rc))
    {
        LogRel(("CPUM: Microcode revision 0x%08X\n", uMicrocodeRev));
    }
    else
    {
        uMicrocodeRev = 0;
        LogRel(("CPUM: Failed to query microcode revision. rc=%Rrc\n", rc));
    }

    /* Mask out the VME capability on certain CPUs, unless overridden by fForceVme.
     * VME bug was fixed in AGESA 1.0.0.6, microcode patch level 8001126.
     */
    if (   (   pVM->cpum.s.GuestFeatures.enmMicroarch == kCpumMicroarch_AMD_Zen_Ryzen
            /** @todo The following ASSUMES that Hygon uses the same version numbering
             * as AMD and that they shipped buggy firmware. */
            || pVM->cpum.s.GuestFeatures.enmMicroarch == kCpumMicroarch_Hygon_Dhyana)
        && uMicrocodeRev < 0x8001126
        && !pConfig->fForceVme)
    {
        /** @todo The above is a very coarse test but at the moment we don't know any better (see @bugref{8852}). */
        LogRel(("CPUM: Zen VME workaround engaged\n"));
        pStdFeatureLeaf->uEdx &= ~X86_CPUID_FEATURE_EDX_VME;
    }

    /* Force standard feature bits. */
    if (pConfig->enmPClMul == CPUMISAEXTCFG_ENABLED_ALWAYS)
        pStdFeatureLeaf->uEcx |= X86_CPUID_FEATURE_ECX_PCLMUL;
    if (pConfig->enmMonitor == CPUMISAEXTCFG_ENABLED_ALWAYS)
        pStdFeatureLeaf->uEcx |= X86_CPUID_FEATURE_ECX_MONITOR;
    if (pConfig->enmCmpXchg16b == CPUMISAEXTCFG_ENABLED_ALWAYS)
        pStdFeatureLeaf->uEcx |= X86_CPUID_FEATURE_ECX_CX16;
    if (pConfig->enmSse41 == CPUMISAEXTCFG_ENABLED_ALWAYS)
        pStdFeatureLeaf->uEcx |= X86_CPUID_FEATURE_ECX_SSE4_1;
    if (pConfig->enmSse42 == CPUMISAEXTCFG_ENABLED_ALWAYS)
        pStdFeatureLeaf->uEcx |= X86_CPUID_FEATURE_ECX_SSE4_2;
    if (pConfig->enmMovBe == CPUMISAEXTCFG_ENABLED_ALWAYS)
        pStdFeatureLeaf->uEcx |= X86_CPUID_FEATURE_ECX_MOVBE;
    if (pConfig->enmPopCnt == CPUMISAEXTCFG_ENABLED_ALWAYS)
        pStdFeatureLeaf->uEcx |= X86_CPUID_FEATURE_ECX_POPCNT;
    if (pConfig->enmAesNi == CPUMISAEXTCFG_ENABLED_ALWAYS)
        pStdFeatureLeaf->uEcx |= X86_CPUID_FEATURE_ECX_AES;
    if (pConfig->enmXSave == CPUMISAEXTCFG_ENABLED_ALWAYS)
        pStdFeatureLeaf->uEcx |= X86_CPUID_FEATURE_ECX_XSAVE;
    if (pConfig->enmAvx == CPUMISAEXTCFG_ENABLED_ALWAYS)
        pStdFeatureLeaf->uEcx |= X86_CPUID_FEATURE_ECX_AVX;
    if (pConfig->enmRdRand == CPUMISAEXTCFG_ENABLED_ALWAYS)
        pStdFeatureLeaf->uEcx |= X86_CPUID_FEATURE_ECX_RDRAND;

    pStdFeatureLeaf = NULL; /* Must refetch! */

    /* Cpuid 0x80000001: (Similar, but in no way identical to 0x00000001.)
     * AMD:
     *  EAX: CPU model, family and stepping.
     *
     *  ECX + EDX: Supported features.  Only report features we can support.
     *  Note! When enabling new features the Synthetic CPU and Portable CPUID
     *        options may require adjusting (i.e. stripping what was enabled).
     *  ASSUMES that this is ALWAYS the AMD defined feature set if present.
     *
     *  EBX: Branding ID and package type (or reserved).
     *
     * Intel and probably most others:
     *  EAX: 0
     *  EBX: 0
     *  ECX + EDX: Subset of AMD features, mainly for AMD64 support.
     */
    PCPUMCPUIDLEAF pExtFeatureLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0x80000001), 0);
    if (pExtFeatureLeaf)
    {
        pExtFeatureLeaf = cpumR3CpuIdMakeSingleLeaf(pCpum, pExtFeatureLeaf);

        pExtFeatureLeaf->uEdx &= X86_CPUID_AMD_FEATURE_EDX_FPU
                               | X86_CPUID_AMD_FEATURE_EDX_VME
                               | X86_CPUID_AMD_FEATURE_EDX_DE
                               | X86_CPUID_AMD_FEATURE_EDX_PSE
                               | X86_CPUID_AMD_FEATURE_EDX_TSC
                               | X86_CPUID_AMD_FEATURE_EDX_MSR //?? this means AMD MSRs..
                               //| X86_CPUID_AMD_FEATURE_EDX_PAE    - turned on when necessary
                               //| X86_CPUID_AMD_FEATURE_EDX_MCE    - not virtualized yet.
                               | X86_CPUID_AMD_FEATURE_EDX_CX8
                               //| X86_CPUID_AMD_FEATURE_EDX_APIC   - set by the APIC device if present.
                               //| RT_BIT_32(10)                    - reserved
                               | X86_CPUID_EXT_FEATURE_EDX_SYSCALL
                               | X86_CPUID_AMD_FEATURE_EDX_MTRR
                               | X86_CPUID_AMD_FEATURE_EDX_PGE
                               | X86_CPUID_AMD_FEATURE_EDX_MCA
                               | X86_CPUID_AMD_FEATURE_EDX_CMOV
                               | X86_CPUID_AMD_FEATURE_EDX_PAT
                               | X86_CPUID_AMD_FEATURE_EDX_PSE36
                               //| RT_BIT_32(18)                    - reserved
                               //| RT_BIT_32(19)                    - reserved
                               | X86_CPUID_EXT_FEATURE_EDX_NX
                               //| RT_BIT_32(21)                    - reserved
                               | PASSTHRU_FEATURE(pConfig->enmAmdExtMmx, pHstFeat->fAmdMmxExts, X86_CPUID_AMD_FEATURE_EDX_AXMMX)
                               | X86_CPUID_AMD_FEATURE_EDX_MMX
                               | X86_CPUID_AMD_FEATURE_EDX_FXSR
                               | X86_CPUID_AMD_FEATURE_EDX_FFXSR
                               //| X86_CPUID_EXT_FEATURE_EDX_PAGE1GB
                               | X86_CPUID_EXT_FEATURE_EDX_RDTSCP
                               //| RT_BIT_32(28)                    - reserved
                               //| X86_CPUID_EXT_FEATURE_EDX_LONG_MODE - turned on when necessary
                               | X86_CPUID_AMD_FEATURE_EDX_3DNOW_EX
                               | X86_CPUID_AMD_FEATURE_EDX_3DNOW
                               ;
        pExtFeatureLeaf->uEcx &= X86_CPUID_EXT_FEATURE_ECX_LAHF_SAHF
                               //| X86_CPUID_AMD_FEATURE_ECX_CMPL   - set below if applicable.
                               | (pConfig->fNestedHWVirt ? X86_CPUID_AMD_FEATURE_ECX_SVM : 0)
                               //| X86_CPUID_AMD_FEATURE_ECX_EXT_APIC
                               /* Note: This could prevent teleporting from AMD to Intel CPUs! */
                               | X86_CPUID_AMD_FEATURE_ECX_CR8L         /* expose lock mov cr0 = mov cr8 hack for guests that can use this feature to access the TPR. */
                               | PASSTHRU_FEATURE(pConfig->enmAbm,       pHstFeat->fAbm, X86_CPUID_AMD_FEATURE_ECX_ABM)
                               | PASSTHRU_FEATURE_TODO(pConfig->enmSse4A,     X86_CPUID_AMD_FEATURE_ECX_SSE4A)
                               | PASSTHRU_FEATURE_TODO(pConfig->enmMisAlnSse, X86_CPUID_AMD_FEATURE_ECX_MISALNSSE)
                               | PASSTHRU_FEATURE(pConfig->enm3dNowPrf, pHstFeat->f3DNowPrefetch, X86_CPUID_AMD_FEATURE_ECX_3DNOWPRF)
                               //| X86_CPUID_AMD_FEATURE_ECX_OSVW
                               //| X86_CPUID_AMD_FEATURE_ECX_IBS
                               //| X86_CPUID_AMD_FEATURE_ECX_XOP
                               //| X86_CPUID_AMD_FEATURE_ECX_SKINIT
                               //| X86_CPUID_AMD_FEATURE_ECX_WDT
                               //| RT_BIT_32(14)                    - reserved
                               //| X86_CPUID_AMD_FEATURE_ECX_LWP    - not supported
                               //| X86_CPUID_AMD_FEATURE_ECX_FMA4   - not yet virtualized.
                               //| RT_BIT_32(17)                    - reserved
                               //| RT_BIT_32(18)                    - reserved
                               //| X86_CPUID_AMD_FEATURE_ECX_NODEID - not yet virtualized.
                               //| RT_BIT_32(20)                    - reserved
                               //| X86_CPUID_AMD_FEATURE_ECX_TBM    - not yet virtualized.
                               //| X86_CPUID_AMD_FEATURE_ECX_TOPOEXT - not yet virtualized.
                               //| RT_BIT_32(23)                    - reserved
                               //| RT_BIT_32(24)                    - reserved
                               //| RT_BIT_32(25)                    - reserved
                               //| RT_BIT_32(26)                    - reserved
                               //| RT_BIT_32(27)                    - reserved
                               //| RT_BIT_32(28)                    - reserved
                               //| RT_BIT_32(29)                    - reserved
                               //| RT_BIT_32(30)                    - reserved
                               //| RT_BIT_32(31)                    - reserved
                               ;
#ifdef VBOX_WITH_MULTI_CORE
        if (   pVM->cCpus > 1
            && (   pCpum->GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_AMD
                || pCpum->GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_HYGON))
            pExtFeatureLeaf->uEcx |= X86_CPUID_AMD_FEATURE_ECX_CMPL; /* CmpLegacy */
#endif

        if (pCpum->u8PortableCpuIdLevel > 0)
        {
            PORTABLE_DISABLE_FEATURE_BIT(    1, pExtFeatureLeaf->uEcx, CR8L,       X86_CPUID_AMD_FEATURE_ECX_CR8L);
            PORTABLE_DISABLE_FEATURE_BIT(    1, pExtFeatureLeaf->uEcx, SVM,        X86_CPUID_AMD_FEATURE_ECX_SVM);
            PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pExtFeatureLeaf->uEcx, ABM,        X86_CPUID_AMD_FEATURE_ECX_ABM,       pConfig->enmAbm);
            PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pExtFeatureLeaf->uEcx, SSE4A,      X86_CPUID_AMD_FEATURE_ECX_SSE4A,     pConfig->enmSse4A);
            PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pExtFeatureLeaf->uEcx, MISALNSSE,  X86_CPUID_AMD_FEATURE_ECX_MISALNSSE, pConfig->enmMisAlnSse);
            PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pExtFeatureLeaf->uEcx, 3DNOWPRF,   X86_CPUID_AMD_FEATURE_ECX_3DNOWPRF,  pConfig->enm3dNowPrf);
            PORTABLE_DISABLE_FEATURE_BIT(    1, pExtFeatureLeaf->uEcx, XOP,        X86_CPUID_AMD_FEATURE_ECX_XOP);
            PORTABLE_DISABLE_FEATURE_BIT(    1, pExtFeatureLeaf->uEcx, TBM,        X86_CPUID_AMD_FEATURE_ECX_TBM);
            PORTABLE_DISABLE_FEATURE_BIT(    1, pExtFeatureLeaf->uEcx, FMA4,       X86_CPUID_AMD_FEATURE_ECX_FMA4);
            PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pExtFeatureLeaf->uEdx, AXMMX,      X86_CPUID_AMD_FEATURE_EDX_AXMMX,     pConfig->enmAmdExtMmx);
            PORTABLE_DISABLE_FEATURE_BIT(    1, pExtFeatureLeaf->uEdx, 3DNOW,      X86_CPUID_AMD_FEATURE_EDX_3DNOW);
            PORTABLE_DISABLE_FEATURE_BIT(    1, pExtFeatureLeaf->uEdx, 3DNOW_EX,   X86_CPUID_AMD_FEATURE_EDX_3DNOW_EX);
            PORTABLE_DISABLE_FEATURE_BIT(    1, pExtFeatureLeaf->uEdx, FFXSR,      X86_CPUID_AMD_FEATURE_EDX_FFXSR);
            PORTABLE_DISABLE_FEATURE_BIT(    1, pExtFeatureLeaf->uEdx, RDTSCP,     X86_CPUID_EXT_FEATURE_EDX_RDTSCP);
            PORTABLE_DISABLE_FEATURE_BIT(    2, pExtFeatureLeaf->uEcx, LAHF_SAHF,  X86_CPUID_EXT_FEATURE_ECX_LAHF_SAHF);
            PORTABLE_DISABLE_FEATURE_BIT(    3, pExtFeatureLeaf->uEcx, CMOV,       X86_CPUID_AMD_FEATURE_EDX_CMOV);

            Assert(!(pExtFeatureLeaf->uEcx & (  X86_CPUID_AMD_FEATURE_ECX_SVM
                                              | X86_CPUID_AMD_FEATURE_ECX_EXT_APIC
                                              | X86_CPUID_AMD_FEATURE_ECX_OSVW
                                              | X86_CPUID_AMD_FEATURE_ECX_IBS
                                              | X86_CPUID_AMD_FEATURE_ECX_SKINIT
                                              | X86_CPUID_AMD_FEATURE_ECX_WDT
                                              | X86_CPUID_AMD_FEATURE_ECX_LWP
                                              | X86_CPUID_AMD_FEATURE_ECX_NODEID
                                              | X86_CPUID_AMD_FEATURE_ECX_TOPOEXT
                                              | UINT32_C(0xff964000)
                                              )));
            Assert(!(pExtFeatureLeaf->uEdx & (  RT_BIT(10)
                                              | X86_CPUID_EXT_FEATURE_EDX_SYSCALL
                                              | RT_BIT(18)
                                              | RT_BIT(19)
                                              | RT_BIT(21)
                                              | X86_CPUID_AMD_FEATURE_EDX_AXMMX
                                              | X86_CPUID_EXT_FEATURE_EDX_PAGE1GB
                                              | RT_BIT(28)
                                              )));
        }

        /* Force extended feature bits. */
        if (pConfig->enmAbm       == CPUMISAEXTCFG_ENABLED_ALWAYS)
            pExtFeatureLeaf->uEcx |= X86_CPUID_AMD_FEATURE_ECX_ABM;
        if (pConfig->enmSse4A     == CPUMISAEXTCFG_ENABLED_ALWAYS)
            pExtFeatureLeaf->uEcx |= X86_CPUID_AMD_FEATURE_ECX_SSE4A;
        if (pConfig->enmMisAlnSse == CPUMISAEXTCFG_ENABLED_ALWAYS)
            pExtFeatureLeaf->uEcx |= X86_CPUID_AMD_FEATURE_ECX_MISALNSSE;
        if (pConfig->enm3dNowPrf  == CPUMISAEXTCFG_ENABLED_ALWAYS)
            pExtFeatureLeaf->uEcx |= X86_CPUID_AMD_FEATURE_ECX_3DNOWPRF;
        if (pConfig->enmAmdExtMmx  == CPUMISAEXTCFG_ENABLED_ALWAYS)
            pExtFeatureLeaf->uEdx |= X86_CPUID_AMD_FEATURE_EDX_AXMMX;
    }
    pExtFeatureLeaf = NULL; /* Must refetch! */


    /* Cpuid 2:
     * Intel: (Nondeterministic) Cache and TLB information
     * AMD:   Reserved
     * VIA:   Reserved
     * Safe to expose.
     */
    uint32_t        uSubLeaf = 0;
    PCPUMCPUIDLEAF  pCurLeaf;
    while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 2, uSubLeaf)) != NULL)
    {
        if ((pCurLeaf->uEax & 0xff) > 1)
        {
            LogRel(("CpuId: Std[2].al: %d -> 1\n", pCurLeaf->uEax & 0xff));
            pCurLeaf->uEax &= UINT32_C(0xffffff01);
        }
        uSubLeaf++;
    }

    /* Cpuid 3:
     * Intel: EAX, EBX - reserved (transmeta uses these)
     *        ECX, EDX - Processor Serial Number if available, otherwise reserved
     * AMD:   Reserved
     * VIA:   Reserved
     * Safe to expose
     */
    pStdFeatureLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 1, 0);
    if (!(pStdFeatureLeaf->uEdx & X86_CPUID_FEATURE_EDX_PSN))
    {
        uSubLeaf = 0;
        while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 3, uSubLeaf)) != NULL)
        {
            pCurLeaf->uEcx = pCurLeaf->uEdx = 0;
            if (pCpum->u8PortableCpuIdLevel > 0)
                pCurLeaf->uEax = pCurLeaf->uEbx = 0;
            uSubLeaf++;
        }
    }

    /* Cpuid 4 + ECX:
     * Intel: Deterministic Cache Parameters Leaf.
     * AMD:   Reserved
     * VIA:   Reserved
     * Safe to expose, except for EAX:
     *      Bits 25-14: Maximum number of addressable IDs for logical processors sharing this cache (see note)**
     *      Bits 31-26: Maximum number of processor cores in this physical package**
     * Note: These SMP values are constant regardless of ECX
     */
    uSubLeaf = 0;
    while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 4, uSubLeaf)) != NULL)
    {
        pCurLeaf->uEax &= UINT32_C(0x00003fff); /* Clear the #maxcores, #threads-sharing-cache (both are #-1).*/
#ifdef VBOX_WITH_MULTI_CORE
        if (   pVM->cCpus > 1
            && pCpum->GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_INTEL)
        {
            AssertReturn(pVM->cCpus <= 64, VERR_TOO_MANY_CPUS);
            /* One logical processor with possibly multiple cores. */
            /* See  http://www.intel.com/Assets/PDF/appnote/241618.pdf p. 29 */
            pCurLeaf->uEax |= pVM->cCpus <= 0x40 ? ((pVM->cCpus - 1) << 26) : UINT32_C(0xfc000000); /* 6 bits only -> 64 cores! */
        }
#endif
        uSubLeaf++;
    }

    /* Cpuid 5:     Monitor/mwait Leaf
     * Intel: ECX, EDX - reserved
     *        EAX, EBX - Smallest and largest monitor line size
     * AMD:   EDX - reserved
     *        EAX, EBX - Smallest and largest monitor line size
     *        ECX - extensions (ignored for now)
     * VIA:   Reserved
     * Safe to expose
     */
    uSubLeaf = 0;
    while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 5, uSubLeaf)) != NULL)
    {
        pStdFeatureLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 1, 0);
        if (!(pStdFeatureLeaf->uEcx & X86_CPUID_FEATURE_ECX_MONITOR))
            pCurLeaf->uEax = pCurLeaf->uEbx = 0;

        pCurLeaf->uEcx = pCurLeaf->uEdx = 0;
        if (pConfig->enmMWaitExtensions)
        {
            pCurLeaf->uEcx = X86_CPUID_MWAIT_ECX_EXT | X86_CPUID_MWAIT_ECX_BREAKIRQIF0;
            /** @todo for now we just expose host's MWAIT C-states, although conceptually
               it shall be part of our power management virtualization model */
#if 0
            /* MWAIT sub C-states */
            pCurLeaf->uEdx =
                    (0 << 0)  /* 0 in C0 */ |
                    (2 << 4)  /* 2 in C1 */ |
                    (2 << 8)  /* 2 in C2 */ |
                    (2 << 12) /* 2 in C3 */ |
                    (0 << 16) /* 0 in C4 */
                    ;
#endif
        }
        else
            pCurLeaf->uEcx = pCurLeaf->uEdx = 0;
        uSubLeaf++;
    }

    /* Cpuid 6: Digital Thermal Sensor and Power Management Paramenters.
     * Intel: Various thermal and power management related stuff.
     * AMD: EBX, EDX - reserved.
     *      EAX - Bit two is ARAT, indicating that APIC timers run at a constant
     *            rate regardless of processor P-states. Same as Intel.
     *      ECX - Bit zero is EffFreq, indicating MSR_0000_00e7 and MSR_0000_00e8
     *            present.  Same as Intel.
     * VIA: ??
     *
     * We clear everything except for the ARAT bit which is important for Windows 11.
     */
    uSubLeaf = 0;
    while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 6, uSubLeaf)) != NULL)
    {
        pCurLeaf->uEbx = pCurLeaf->uEcx = pCurLeaf->uEdx = 0;
        pCurLeaf->uEax &= 0
                       | X86_CPUID_POWER_EAX_ARAT
                       ;

        /* Since we emulate the APIC timers, we can normally set the ARAT bit
         * regardless of whether the host CPU sets it or not. Intel sets the ARAT
         * bit circa since the Westmere generation, AMD probably only since Zen.
         * See @bugref{10567}.
         */
        if (pConfig->fInvariantApic)
            pCurLeaf->uEax |= X86_CPUID_POWER_EAX_ARAT;

        uSubLeaf++;
    }

    /* Cpuid 7 + ECX: Structured Extended Feature Flags Enumeration
     * EAX: Number of sub leaves.
     * EBX+ECX+EDX: Feature flags
     *
     * We only have documentation for one sub-leaf, so clear all other (no need
     * to remove them as such, just set them to zero).
     *
     * Note! When enabling new features the Synthetic CPU and Portable CPUID
     *       options may require adjusting (i.e. stripping what was enabled).
     */
    uSubLeaf = 0;
    while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 7, uSubLeaf)) != NULL)
    {
        switch (uSubLeaf)
        {
            case 0:
            {
                pCurLeaf->uEax  = RT_MIN(pCurLeaf->uEax, 2); /* Max ECX input is 2. */
                pCurLeaf->uEbx &= 0
                               | PASSTHRU_FEATURE(pConfig->enmFsGsBase, pHstFeat->fFsGsBase, X86_CPUID_STEXT_FEATURE_EBX_FSGSBASE)
                               //| X86_CPUID_STEXT_FEATURE_EBX_TSC_ADJUST        RT_BIT(1)
                               //| X86_CPUID_STEXT_FEATURE_EBX_SGX               RT_BIT(2)
                               | X86_CPUID_STEXT_FEATURE_EBX_BMI1
                               //| X86_CPUID_STEXT_FEATURE_EBX_HLE               RT_BIT(4)
                               | PASSTHRU_FEATURE(pConfig->enmAvx2, pHstFeat->fAvx2, X86_CPUID_STEXT_FEATURE_EBX_AVX2)
                               | X86_CPUID_STEXT_FEATURE_EBX_FDP_EXCPTN_ONLY
                               //| X86_CPUID_STEXT_FEATURE_EBX_SMEP              RT_BIT(7)
                               | X86_CPUID_STEXT_FEATURE_EBX_BMI2
                               //| X86_CPUID_STEXT_FEATURE_EBX_ERMS              RT_BIT(9)
                               | PASSTHRU_FEATURE(pConfig->enmInvpcid, pHstFeat->fInvpcid, X86_CPUID_STEXT_FEATURE_EBX_INVPCID)
                               //| X86_CPUID_STEXT_FEATURE_EBX_RTM               RT_BIT(11)
                               //| X86_CPUID_STEXT_FEATURE_EBX_PQM               RT_BIT(12)
                               | X86_CPUID_STEXT_FEATURE_EBX_DEPR_FPU_CS_DS
                               //| X86_CPUID_STEXT_FEATURE_EBX_MPE               RT_BIT(14)
                               //| X86_CPUID_STEXT_FEATURE_EBX_PQE               RT_BIT(15)
                               //| X86_CPUID_STEXT_FEATURE_EBX_AVX512F           RT_BIT(16)
                               //| RT_BIT(17) - reserved
                               | PASSTHRU_FEATURE_TODO(pConfig->enmRdSeed, X86_CPUID_STEXT_FEATURE_EBX_RDSEED)
                               | PASSTHRU_FEATURE(pConfig->enmAdx, pHstFeat->fAdx, X86_CPUID_STEXT_FEATURE_EBX_ADX)
                               //| X86_CPUID_STEXT_FEATURE_EBX_SMAP              RT_BIT(20)
                               //| RT_BIT(21) - reserved
                               //| RT_BIT(22) - reserved
                               | PASSTHRU_FEATURE(pConfig->enmCLFlushOpt, pHstFeat->fClFlushOpt, X86_CPUID_STEXT_FEATURE_EBX_CLFLUSHOPT)
                               //| RT_BIT(24) - reserved
                               //| X86_CPUID_STEXT_FEATURE_EBX_INTEL_PT          RT_BIT(25)
                               //| X86_CPUID_STEXT_FEATURE_EBX_AVX512PF          RT_BIT(26)
                               //| X86_CPUID_STEXT_FEATURE_EBX_AVX512ER          RT_BIT(27)
                               //| X86_CPUID_STEXT_FEATURE_EBX_AVX512CD          RT_BIT(28)
                               | PASSTHRU_FEATURE(pConfig->enmSha, pHstFeat->fSha, X86_CPUID_STEXT_FEATURE_EBX_SHA)
                               //| RT_BIT(30) - reserved
                               //| RT_BIT(31) - reserved
                               ;
                pCurLeaf->uEcx &= 0
                               //| X86_CPUID_STEXT_FEATURE_ECX_PREFETCHWT1 - we do not do vector functions yet.
                               ;
                pCurLeaf->uEdx &= 0
                               //| X86_CPUID_STEXT_FEATURE_EDX_SRBDS_CTRL        RT_BIT(9)
                               | PASSTHRU_FEATURE(pConfig->enmMdsClear,   pHstFeat->fMdsClear, X86_CPUID_STEXT_FEATURE_EDX_MD_CLEAR)
                               //| X86_CPUID_STEXT_FEATURE_EDX_TSX_FORCE_ABORT   RT_BIT_32(11)
                               //| X86_CPUID_STEXT_FEATURE_EDX_CET_IBT           RT_BIT(20)
                               //| X86_CPUID_STEXT_FEATURE_EDX_IBRS_IBPB         RT_BIT(26)
                               //| X86_CPUID_STEXT_FEATURE_EDX_STIBP             RT_BIT(27)
                               | PASSTHRU_FEATURE(pConfig->enmFlushCmdMsr, pHstFeat->fFlushCmd, X86_CPUID_STEXT_FEATURE_EDX_FLUSH_CMD)
                               | PASSTHRU_FEATURE(pConfig->enmArchCapMsr,  pHstFeat->fArchCap,  X86_CPUID_STEXT_FEATURE_EDX_ARCHCAP)
                               //| X86_CPUID_STEXT_FEATURE_EDX_CORECAP           RT_BIT_32(30)
                               //| X86_CPUID_STEXT_FEATURE_EDX_SSBD              RT_BIT_32(31)
                               ;

                /* Mask out INVPCID unless FSGSBASE is exposed due to a bug in Windows 10 SMP guests, see @bugref{9089#c15}. */
                if (  !pVM->cpum.s.GuestFeatures.fFsGsBase
                   && (pCurLeaf->uEbx & X86_CPUID_STEXT_FEATURE_EBX_INVPCID))
                {
                    pCurLeaf->uEbx &= ~X86_CPUID_STEXT_FEATURE_EBX_INVPCID;
                    LogRel(("CPUM: Disabled INVPCID without FSGSBASE to work around buggy guests\n"));
                }

                if (pCpum->u8PortableCpuIdLevel > 0)
                {
                    PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pCurLeaf->uEbx, FSGSBASE,   X86_CPUID_STEXT_FEATURE_EBX_FSGSBASE, pConfig->enmFsGsBase);
                    PORTABLE_DISABLE_FEATURE_BIT(    1, pCurLeaf->uEbx, SGX,        X86_CPUID_STEXT_FEATURE_EBX_SGX);
                    PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pCurLeaf->uEbx, AVX2,       X86_CPUID_STEXT_FEATURE_EBX_AVX2, pConfig->enmAvx2);
                    PORTABLE_DISABLE_FEATURE_BIT(    1, pCurLeaf->uEbx, SMEP,       X86_CPUID_STEXT_FEATURE_EBX_SMEP);
                    PORTABLE_DISABLE_FEATURE_BIT(    1, pCurLeaf->uEbx, BMI2,       X86_CPUID_STEXT_FEATURE_EBX_BMI2);
                    PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pCurLeaf->uEbx, INVPCID,    X86_CPUID_STEXT_FEATURE_EBX_INVPCID, pConfig->enmInvpcid);
                    PORTABLE_DISABLE_FEATURE_BIT(    1, pCurLeaf->uEbx, AVX512F,    X86_CPUID_STEXT_FEATURE_EBX_AVX512F);
                    PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pCurLeaf->uEbx, RDSEED,     X86_CPUID_STEXT_FEATURE_EBX_RDSEED, pConfig->enmRdSeed);
                    PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pCurLeaf->uEbx, ADX,        X86_CPUID_STEXT_FEATURE_EBX_ADX, pConfig->enmAdx);
                    PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pCurLeaf->uEbx, CLFLUSHOPT, X86_CPUID_STEXT_FEATURE_EBX_RDSEED, pConfig->enmCLFlushOpt);
                    PORTABLE_DISABLE_FEATURE_BIT(    1, pCurLeaf->uEbx, AVX512PF,   X86_CPUID_STEXT_FEATURE_EBX_AVX512PF);
                    PORTABLE_DISABLE_FEATURE_BIT(    1, pCurLeaf->uEbx, AVX512ER,   X86_CPUID_STEXT_FEATURE_EBX_AVX512ER);
                    PORTABLE_DISABLE_FEATURE_BIT(    1, pCurLeaf->uEbx, AVX512CD,   X86_CPUID_STEXT_FEATURE_EBX_AVX512CD);
                    PORTABLE_DISABLE_FEATURE_BIT(    1, pCurLeaf->uEbx, SMAP,       X86_CPUID_STEXT_FEATURE_EBX_SMAP);
                    PORTABLE_DISABLE_FEATURE_BIT_CFG(1, pCurLeaf->uEbx, SHA,        X86_CPUID_STEXT_FEATURE_EBX_SHA, pConfig->enmSha);
                    PORTABLE_DISABLE_FEATURE_BIT(    1, pCurLeaf->uEcx, PREFETCHWT1, X86_CPUID_STEXT_FEATURE_ECX_PREFETCHWT1);
                    PORTABLE_DISABLE_FEATURE_BIT_CFG(3, pCurLeaf->uEdx, FLUSH_CMD,  X86_CPUID_STEXT_FEATURE_EDX_FLUSH_CMD, pConfig->enmFlushCmdMsr);
                    PORTABLE_DISABLE_FEATURE_BIT_CFG(3, pCurLeaf->uEdx, MD_CLEAR,   X86_CPUID_STEXT_FEATURE_EDX_MD_CLEAR, pConfig->enmMdsClear);
                    PORTABLE_DISABLE_FEATURE_BIT_CFG(3, pCurLeaf->uEdx, ARCHCAP,    X86_CPUID_STEXT_FEATURE_EDX_ARCHCAP, pConfig->enmArchCapMsr);
                }

                /* Dependencies. */
                if (!(pCurLeaf->uEdx & X86_CPUID_STEXT_FEATURE_EDX_FLUSH_CMD))
                    pCurLeaf->uEdx &= ~X86_CPUID_STEXT_FEATURE_EDX_MD_CLEAR;

                /* Force standard feature bits. */
                if (pConfig->enmFsGsBase == CPUMISAEXTCFG_ENABLED_ALWAYS)
                    pCurLeaf->uEbx |= X86_CPUID_STEXT_FEATURE_EBX_FSGSBASE;
                if (pConfig->enmAvx2 == CPUMISAEXTCFG_ENABLED_ALWAYS)
                    pCurLeaf->uEbx |= X86_CPUID_STEXT_FEATURE_EBX_AVX2;
                if (pConfig->enmRdSeed == CPUMISAEXTCFG_ENABLED_ALWAYS)
                    pCurLeaf->uEbx |= X86_CPUID_STEXT_FEATURE_EBX_RDSEED;
                if (pConfig->enmAdx == CPUMISAEXTCFG_ENABLED_ALWAYS)
                    pCurLeaf->uEbx |= X86_CPUID_STEXT_FEATURE_EBX_ADX;
                if (pConfig->enmCLFlushOpt == CPUMISAEXTCFG_ENABLED_ALWAYS)
                    pCurLeaf->uEbx |= X86_CPUID_STEXT_FEATURE_EBX_CLFLUSHOPT;
                if (pConfig->enmSha == CPUMISAEXTCFG_ENABLED_ALWAYS)
                    pCurLeaf->uEbx |= X86_CPUID_STEXT_FEATURE_EBX_SHA;
                if (pConfig->enmInvpcid == CPUMISAEXTCFG_ENABLED_ALWAYS)
                    pCurLeaf->uEbx |= X86_CPUID_STEXT_FEATURE_EBX_INVPCID;
                if (pConfig->enmFlushCmdMsr == CPUMISAEXTCFG_ENABLED_ALWAYS)
                    pCurLeaf->uEdx |= X86_CPUID_STEXT_FEATURE_EDX_FLUSH_CMD;
                if (pConfig->enmMdsClear == CPUMISAEXTCFG_ENABLED_ALWAYS)
                    pCurLeaf->uEdx |= X86_CPUID_STEXT_FEATURE_EDX_MD_CLEAR;
                if (pConfig->enmArchCapMsr == CPUMISAEXTCFG_ENABLED_ALWAYS)
                    pCurLeaf->uEdx |= X86_CPUID_STEXT_FEATURE_EDX_ARCHCAP;
                break;
            }

            case 2:
            {
                pCurLeaf->uEax  = 0;
                pCurLeaf->uEbx  = 0;
                pCurLeaf->uEcx  = 0;
                pCurLeaf->uEdx &= 0
                               //| X86_CPUID_STEXT_FEATURE_2_EDX_PSFD              RT_BIT_32(0)
                               //| X86_CPUID_STEXT_FEATURE_2_EDX_IPRED_CTRL        RT_BIT_32(1)
                               //| X86_CPUID_STEXT_FEATURE_2_EDX_RRSBA_CTRL        RT_BIT_32(2)
                               //| X86_CPUID_STEXT_FEATURE_2_EDX_DDPD_U            RT_BIT_32(3)
                               //| X86_CPUID_STEXT_FEATURE_2_EDX_BHI_CTRL          RT_BIT_32(4)
                               | PASSTHRU_FEATURE(pConfig->enmMcdtNo, pHstFeat->fMcdtNo, X86_CPUID_STEXT_FEATURE_2_EDX_MCDT_NO)
                               //| X86_CPUID_STEXT_FEATURE_2_EDX_UC_LOCK_DIS       RT_BIT_32(6)
                               //| Bit 7 - MONITOR_MITG_NO - No need for MONITOR/UMONITOR power mitigrations. */
                               | PASSTHRU_FEATURE(pConfig->enmMonitorMitgNo, pHstFeat->fMonitorMitgNo, X86_CPUID_STEXT_FEATURE_2_EDX_MONITOR_MITG_NO)
                               ;

                /* Force standard feature bits. */
                if (pConfig->enmMcdtNo == CPUMISAEXTCFG_ENABLED_ALWAYS)
                    pCurLeaf->uEdx |= X86_CPUID_STEXT_FEATURE_2_EDX_MCDT_NO;
                if (pConfig->enmMonitorMitgNo == CPUMISAEXTCFG_ENABLED_ALWAYS)
                    pCurLeaf->uEdx |= X86_CPUID_STEXT_FEATURE_2_EDX_MONITOR_MITG_NO;
                break;
            }

            default:
                /* Invalid index, all values are zero. */
                pCurLeaf->uEax = 0;
                pCurLeaf->uEbx = 0;
                pCurLeaf->uEcx = 0;
                pCurLeaf->uEdx = 0;
                break;
        }
        uSubLeaf++;
    }

    /* Cpuid 8: Marked as reserved by Intel and AMD.
     * We zero this since we don't know what it may have been used for.
     */
    cpumR3CpuIdZeroLeaf(pCpum, 8);

    /* Cpuid 9: Direct Cache Access (DCA) Parameters
     * Intel: EAX - Value of PLATFORM_DCA_CAP bits.
     *        EBX, ECX, EDX - reserved.
     * AMD:   Reserved
     * VIA:   ??
     *
     * We zero this.
     */
    cpumR3CpuIdZeroLeaf(pCpum, 9);

    /* Cpuid 0xa: Architectural Performance Monitor Features
     * Intel: EAX - Value of PLATFORM_DCA_CAP bits.
     *        EBX, ECX, EDX - reserved.
     * AMD:   Reserved
     * VIA:   ??
     *
     * We zero this, for now at least.
     */
    cpumR3CpuIdZeroLeaf(pCpum, 10);

    /* Cpuid 0xb+ECX: x2APIC Features / Processor Topology.
     * Intel: EAX - APCI ID shift right for next level.
     *        EBX - Factory configured cores/threads at this level.
     *        ECX - Level number (same as input) and level type (1,2,0).
     *        EDX - Extended initial APIC ID.
     * AMD:   Reserved
     * VIA:   ??
     */
    uSubLeaf = 0;
    while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 11, uSubLeaf)) != NULL)
    {
        if (pCurLeaf->fFlags & CPUMCPUIDLEAF_F_CONTAINS_APIC_ID)
        {
            uint8_t bLevelType = RT_BYTE2(pCurLeaf->uEcx);
            if (bLevelType == 1)
            {
                /* Thread level - we don't do threads at the moment. */
                pCurLeaf->uEax = 0; /** @todo is this correct? Real CPUs never do 0 here, I think... */
                pCurLeaf->uEbx = 1;
            }
            else if (bLevelType == 2)
            {
                /* Core level. */
                pCurLeaf->uEax = 1; /** @todo real CPUs are supposed to be in the 4-6 range, not 1.  Our APIC ID assignments are a little special... */
#ifdef VBOX_WITH_MULTI_CORE
                while (RT_BIT_32(pCurLeaf->uEax) < pVM->cCpus)
                    pCurLeaf->uEax++;
#endif
                pCurLeaf->uEbx = pVM->cCpus;
            }
            else
            {
                AssertLogRelMsg(bLevelType == 0, ("bLevelType=%#x uSubLeaf=%#x\n", bLevelType, uSubLeaf));
                pCurLeaf->uEax = 0;
                pCurLeaf->uEbx = 0;
                pCurLeaf->uEcx = 0;
            }
            pCurLeaf->uEcx = (pCurLeaf->uEcx & UINT32_C(0xffffff00)) | (uSubLeaf & 0xff);
            pCurLeaf->uEdx = 0;  /* APIC ID is filled in by CPUMGetGuestCpuId() at runtime.  Init for EMT(0) as usual. */
        }
        else
        {
            pCurLeaf->uEax = 0;
            pCurLeaf->uEbx = 0;
            pCurLeaf->uEcx = 0;
            pCurLeaf->uEdx = 0;
        }
        uSubLeaf++;
    }

    /* Cpuid 0xc: Marked as reserved by Intel and AMD.
     * We zero this since we don't know what it may have been used for.
     */
    cpumR3CpuIdZeroLeaf(pCpum, 12);

    /* Cpuid 0xd + ECX: Processor Extended State Enumeration
     * ECX=0:   EAX - Valid bits in XCR0[31:0].
     *          EBX - Maximum state size as per current XCR0 value.
     *          ECX - Maximum state size for all supported features.
     *          EDX - Valid bits in XCR0[63:32].
     * ECX=1:   EAX - Various X-features.
     *          EBX - Maximum state size as per current XCR0|IA32_XSS value.
     *          ECX - Valid bits in IA32_XSS[31:0].
     *          EDX - Valid bits in IA32_XSS[63:32].
     * ECX=N, where N in 2..63 and indicates a bit in XCR0 and/or IA32_XSS,
     *        if the bit invalid all four registers are set to zero.
     *          EAX - The state size for this feature.
     *          EBX - The state byte offset of this feature.
     *          ECX - Bit 0 indicates whether this sub-leaf maps to a valid IA32_XSS bit (=1) or a valid XCR0 bit (=0).
     *          EDX - Reserved, but is set to zero if invalid sub-leaf index.
     *
     * Clear them all as we don't currently implement extended CPU state.
     */
    /* Figure out the supported XCR0/XSS mask component and make sure CPUID[1].ECX[27] = CR4.OSXSAVE. */
    uint64_t fGuestXcr0Mask = 0;
    pStdFeatureLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 1, 0);
    if (pStdFeatureLeaf && (pStdFeatureLeaf->uEcx & X86_CPUID_FEATURE_ECX_XSAVE))
    {
        fGuestXcr0Mask = XSAVE_C_X87 | XSAVE_C_SSE;
        if (pStdFeatureLeaf && (pStdFeatureLeaf->uEcx & X86_CPUID_FEATURE_ECX_AVX))
            fGuestXcr0Mask |= XSAVE_C_YMM;
        pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 7, 0);
        if (pCurLeaf && (pCurLeaf->uEbx & X86_CPUID_STEXT_FEATURE_EBX_AVX512F))
            fGuestXcr0Mask |= XSAVE_C_ZMM_16HI | XSAVE_C_ZMM_HI256 | XSAVE_C_OPMASK;
        fGuestXcr0Mask &= pCpum->fXStateHostMask;

        pStdFeatureLeaf->fFlags |= CPUMCPUIDLEAF_F_CONTAINS_OSXSAVE;
    }
    pStdFeatureLeaf = NULL;
    pCpum->fXStateGuestMask = fGuestXcr0Mask;

    /* Work the sub-leaves. */
    uint32_t cbXSaveMaxActual = CPUM_MIN_XSAVE_AREA_SIZE;
    uint32_t cbXSaveMaxReport = CPUM_MIN_XSAVE_AREA_SIZE;
    for (uSubLeaf = 0; uSubLeaf < 63; uSubLeaf++)
    {
        pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 13, uSubLeaf);
        if (pCurLeaf)
        {
            if (fGuestXcr0Mask)
            {
                switch (uSubLeaf)
                {
                    case 0:
                        pCurLeaf->uEax &= RT_LO_U32(fGuestXcr0Mask);
                        pCurLeaf->uEdx &= RT_HI_U32(fGuestXcr0Mask);
                        AssertLogRelMsgReturn((pCurLeaf->uEax & (XSAVE_C_X87 | XSAVE_C_SSE)) == (XSAVE_C_X87 | XSAVE_C_SSE),
                                              ("CPUID(0xd/0).EAX missing mandatory X87 or SSE bits: %#RX32", pCurLeaf->uEax),
                                              VERR_CPUM_IPE_1);
                        cbXSaveMaxActual = pCurLeaf->uEcx;
                        AssertLogRelMsgReturn(cbXSaveMaxActual <= CPUM_MAX_XSAVE_AREA_SIZE && cbXSaveMaxActual >= CPUM_MIN_XSAVE_AREA_SIZE,
                                              ("%#x max=%#x\n", cbXSaveMaxActual, CPUM_MAX_XSAVE_AREA_SIZE), VERR_CPUM_IPE_2);
                        AssertLogRelMsgReturn(pCurLeaf->uEbx >= CPUM_MIN_XSAVE_AREA_SIZE && pCurLeaf->uEbx <= cbXSaveMaxActual,
                                              ("ebx=%#x cbXSaveMaxActual=%#x\n", pCurLeaf->uEbx, cbXSaveMaxActual),
                                              VERR_CPUM_IPE_2);
                        continue;
                    case 1:
                        pCurLeaf->uEax &= 0;
                        pCurLeaf->uEcx &= 0;
                        pCurLeaf->uEdx &= 0;
                        /** @todo what about checking ebx? */
                        continue;
                    default:
                        if (fGuestXcr0Mask & RT_BIT_64(uSubLeaf))
                        {
                            AssertLogRelMsgReturn(   pCurLeaf->uEax <= cbXSaveMaxActual
                                                  && pCurLeaf->uEax >  0
                                                  && pCurLeaf->uEbx < cbXSaveMaxActual
                                                  && pCurLeaf->uEbx >= CPUM_MIN_XSAVE_AREA_SIZE
                                                  && pCurLeaf->uEbx + pCurLeaf->uEax <= cbXSaveMaxActual,
                                                  ("%#x: eax=%#x ebx=%#x cbMax=%#x\n",
                                                   uSubLeaf, pCurLeaf->uEax, pCurLeaf->uEbx, cbXSaveMaxActual),
                                                  VERR_CPUM_IPE_2);
                            AssertLogRel(!(pCurLeaf->uEcx & 1));
                            pCurLeaf->uEcx = 0; /* Bit 0 should be zero (XCR0), the reset are reserved... */
                            pCurLeaf->uEdx = 0; /* it's reserved... */
                            if (pCurLeaf->uEbx + pCurLeaf->uEax > cbXSaveMaxReport)
                                cbXSaveMaxReport = pCurLeaf->uEbx + pCurLeaf->uEax;
                            continue;
                        }
                        break;
                }
            }

            /* Clear the leaf. */
            pCurLeaf->uEax = 0;
            pCurLeaf->uEbx = 0;
            pCurLeaf->uEcx = 0;
            pCurLeaf->uEdx = 0;
        }
    }

    /* Update the max and current feature sizes to shut up annoying Linux kernels. */
    if (cbXSaveMaxReport != cbXSaveMaxActual && fGuestXcr0Mask)
    {
        pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 13, 0);
        if (pCurLeaf)
        {
            LogRel(("CPUM: Changing leaf 13[0]: EBX=%#RX32 -> %#RX32, ECX=%#RX32 -> %#RX32\n",
                    pCurLeaf->uEbx, cbXSaveMaxReport, pCurLeaf->uEcx, cbXSaveMaxReport));
            pCurLeaf->uEbx = cbXSaveMaxReport;
            pCurLeaf->uEcx = cbXSaveMaxReport;
        }
    }

    /* Cpuid 0xe: Marked as reserved by Intel and AMD.
     * We zero this since we don't know what it may have been used for.
     */
    cpumR3CpuIdZeroLeaf(pCpum, 14);

    /* Cpuid 0xf + ECX: Platform quality of service monitoring (PQM),
     * also known as Intel Resource Director Technology (RDT) Monitoring
     * We zero this as we don't currently virtualize PQM.
     */
    cpumR3CpuIdZeroLeaf(pCpum, 15);

    /* Cpuid 0x10 + ECX: Platform quality of service enforcement (PQE),
     * also known as Intel Resource Director Technology (RDT) Allocation
     * We zero this as we don't currently virtualize PQE.
     */
    cpumR3CpuIdZeroLeaf(pCpum, 16);

    /* Cpuid 0x11: Marked as reserved by Intel and AMD.
     * We zero this since we don't know what it may have been used for.
     */
    cpumR3CpuIdZeroLeaf(pCpum, 17);

    /* Cpuid 0x12 + ECX: SGX resource enumeration.
     * We zero this as we don't currently virtualize this.
     */
    cpumR3CpuIdZeroLeaf(pCpum, 18);

    /* Cpuid 0x13: Marked as reserved by Intel and AMD.
     * We zero this since we don't know what it may have been used for.
     */
    cpumR3CpuIdZeroLeaf(pCpum, 19);

    /* Cpuid 0x14 + ECX: Processor Trace (PT) capability enumeration.
     * We zero this as we don't currently virtualize this.
     */
    cpumR3CpuIdZeroLeaf(pCpum, 20);

    /* Cpuid 0x15: Timestamp Counter / Core Crystal Clock info.
     * Intel: uTscFrequency = uCoreCrystalClockFrequency * EBX / EAX.
     *        EAX - denominator (unsigned).
     *        EBX - numerator (unsigned).
     *        ECX, EDX - reserved.
     * AMD:   Reserved / undefined / not implemented.
     * VIA:   Reserved / undefined / not implemented.
     * We zero this as we don't currently virtualize this.
     */
    cpumR3CpuIdZeroLeaf(pCpum, 21);

    /* Cpuid 0x16: Processor frequency info
     * Intel: EAX - Core base frequency in MHz.
     *        EBX - Core maximum frequency in MHz.
     *        ECX - Bus (reference) frequency in MHz.
     *        EDX - Reserved.
     * AMD:   Reserved / undefined / not implemented.
     * VIA:   Reserved / undefined / not implemented.
     * We zero this as we don't currently virtualize this.
     */
    cpumR3CpuIdZeroLeaf(pCpum, 22);

    /* Cpuid 0x17..0x10000000: Unknown.
     * We don't know these and what they mean, so remove them. */
    cpumR3CpuIdRemoveRange(pCpum->GuestInfo.paCpuIdLeavesR3, &pCpum->GuestInfo.cCpuIdLeaves,
                           UINT32_C(0x00000017), UINT32_C(0x0fffffff));


    /* CpuId 0x40000000..0x4fffffff: Reserved for hypervisor/emulator.
     * We remove all these as we're a hypervisor and must provide our own.
     */
    cpumR3CpuIdRemoveRange(pCpum->GuestInfo.paCpuIdLeavesR3, &pCpum->GuestInfo.cCpuIdLeaves,
                           UINT32_C(0x40000000), UINT32_C(0x4fffffff));


    /* Cpuid 0x80000000 is harmless. */

    /* Cpuid 0x80000001 is handled with cpuid 1 way up above. */

    /* Cpuid 0x80000002...0x80000004 contains the processor name and is considered harmless. */

    /* Cpuid 0x80000005 & 0x80000006 contain information about L1, L2 & L3 cache and TLB identifiers.
     * Safe to pass on to the guest.
     *
     * AMD:   0x80000005 L1 cache information
     *        0x80000006 L2/L3 cache information
     * Intel: 0x80000005 reserved
     *        0x80000006 L2 cache information
     * VIA:   0x80000005 TLB and L1 cache information
     *        0x80000006 L2 cache information
     */

    uSubLeaf = 0;
    while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0x80000006), uSubLeaf)) != NULL)
    {
        if (   pCpum->GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_AMD
            || pCpum->GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_HYGON)
        {
            /*
             * Some AMD CPUs (e.g. Ryzen 7940HS) report zero L3 cache line size here and refer
             * to CPUID Fn8000_001D. This triggers division by zero in Linux if the
             * TopologyExtensions aka TOPOEXT bit in Fn8000_0001_ECX is not set, or if the kernel
             * is old enough (e.g. Linux 3.13) that it does not know about the topology extension
             * CPUID leaves.
             * We put a non-zero value in the cache line size here, if possible the actual value
             * gleaned from Fn8000_001D, or worst case a made-up valid number.
             */
            PCPUMCPUIDLEAF  pTopoLeaf;
            uint32_t        uTopoSubLeaf;
            uint32_t        uCacheLineSize;

            if ((pCurLeaf->uEdx & 0xff) == 0)
            {
                uTopoSubLeaf = 0;

                uCacheLineSize = 64;    /* Use 64-byte line size as a fallback. */

                /* Find L3 cache information. Have to check the cache level in EAX. */
                while ((pTopoLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0x8000001d), uTopoSubLeaf)) != NULL)
                {
                    if (((pTopoLeaf->uEax >> 5) & 0x07) == 3) {
                        uCacheLineSize = (pTopoLeaf->uEbx & 0xfff) + 1;
                        /* Fn8000_0006 can't report power of two line sizes greater than 128. */
                        if (uCacheLineSize > 128)
                            uCacheLineSize = 128;

                        break;
                    }
                    uTopoSubLeaf++;
                }

                Assert(uCacheLineSize < 256);
                pCurLeaf->uEdx |= uCacheLineSize;
                LogRel(("CPUM: AMD L3 cache line size in CPUID leaf 0x80000006 was zero, adjusting to %u\n", uCacheLineSize));
            }
        }
        uSubLeaf++;
    }

    /* Cpuid 0x80000007: Advanced Power Management Information.
     * AMD:   EAX: Processor feedback capabilities.
     *        EBX: RAS capabilites.
     *        ECX: Advanced power monitoring interface.
     *        EDX: Enhanced power management capabilities.
     * Intel: EAX, EBX, ECX - reserved.
     *        EDX - Invariant TSC indicator supported (bit 8), the rest is reserved.
     * VIA:   Reserved
     * We let the guest see EDX_TSCINVAR (and later maybe EDX_EFRO). Actually, we should set EDX_TSCINVAR.
     */
    uSubLeaf = 0;
    while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0x80000007), uSubLeaf)) != NULL)
    {
        pCurLeaf->uEax = pCurLeaf->uEbx = pCurLeaf->uEcx = 0;
        if (   pCpum->GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_AMD
            || pCpum->GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_HYGON)
        {
            /*
             * Older 64-bit linux kernels blindly assume that the AMD performance counters work
             * if X86_CPUID_AMD_ADVPOWER_EDX_TSCINVAR is set, see @bugref{7243#c85}. Exposing this
             * bit is now configurable.
             */
            pCurLeaf->uEdx &= 0
                           //| X86_CPUID_AMD_ADVPOWER_EDX_TS
                           //| X86_CPUID_AMD_ADVPOWER_EDX_FID
                           //| X86_CPUID_AMD_ADVPOWER_EDX_VID
                           //| X86_CPUID_AMD_ADVPOWER_EDX_TTP
                           //| X86_CPUID_AMD_ADVPOWER_EDX_TM
                           //| X86_CPUID_AMD_ADVPOWER_EDX_STC
                           //| X86_CPUID_AMD_ADVPOWER_EDX_MC
                           //| X86_CPUID_AMD_ADVPOWER_EDX_HWPSTATE
                             | X86_CPUID_AMD_ADVPOWER_EDX_TSCINVAR
                           //| X86_CPUID_AMD_ADVPOWER_EDX_CPB       RT_BIT(9)
                           //| X86_CPUID_AMD_ADVPOWER_EDX_EFRO      RT_BIT(10)
                           //| X86_CPUID_AMD_ADVPOWER_EDX_PFI       RT_BIT(11)
                           //| X86_CPUID_AMD_ADVPOWER_EDX_PA        RT_BIT(12)
                           | 0;
        }
        else
            pCurLeaf->uEdx &= X86_CPUID_AMD_ADVPOWER_EDX_TSCINVAR;
        if (!pConfig->fInvariantTsc)
            pCurLeaf->uEdx &= ~X86_CPUID_AMD_ADVPOWER_EDX_TSCINVAR;
        uSubLeaf++;
    }

    /* Cpuid 0x80000008:
     * AMD:               EAX: Long Mode Size Identifiers
     *                    EBX: Extended Feature Identifiers
     *                    ECX: Number of cores + APICIdCoreIdSize
     *                    EDX: RDPRU Register Identifier Range
     * Intel:             EAX: Virtual/Physical address Size
     *                    EBX, ECX, EDX - reserved
     * VIA:               EAX: Virtual/Physical address Size
     *                    EBX, ECX, EDX - reserved
     *
     * We only expose the virtual+pysical address size to the guest atm.
     * On AMD we set the core count, but not the apic id stuff as we're
     * currently not doing the apic id assignments in a compatible manner.
     */
    uSubLeaf = 0;
    while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0x80000008), uSubLeaf)) != NULL)
    {
        pCurLeaf->uEax &= UINT32_C(0x0000ffff); /* Virtual & physical address sizes only. */
        if (   pCpum->GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_AMD
            || pCpum->GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_HYGON)
        {
            /* Expose XSaveErPtr aka RstrFpErrPtrs to guest. */
            pCurLeaf->uEbx &= X86_CPUID_AMD_EFEID_EBX_XSAVE_ER_PTR;  /* reserved - [12] == IBPB */
        }
        else
            pCurLeaf->uEbx  = 0;    /* reserved */

        pCurLeaf->uEdx  = 0;  /* reserved */

        /* Set APICIdCoreIdSize to zero (use legacy method to determine the number of cores per cpu).
         * Set core count to 0, indicating 1 core. Adjust if we're in multi core mode on AMD. */
        pCurLeaf->uEcx = 0;
#ifdef VBOX_WITH_MULTI_CORE
        if (   pVM->cCpus > 1
            && (   pCpum->GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_AMD
                || pCpum->GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_HYGON))
            pCurLeaf->uEcx |= (pVM->cCpus - 1) & UINT32_C(0xff);
#endif
        uSubLeaf++;
    }

    /* Cpuid 0x80000009: Reserved
     * We zero this since we don't know what it may have been used for.
     */
    cpumR3CpuIdZeroLeaf(pCpum, UINT32_C(0x80000009));

    /* Cpuid 0x8000000a: SVM information on AMD, invalid on Intel.
     * AMD:   EAX - SVM revision.
     *        EBX - Number of ASIDs.
     *        ECX - Reserved.
     *        EDX - SVM Feature identification.
     */
    if (   pCpum->GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_AMD
        || pCpum->GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_HYGON)
    {
        pExtFeatureLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0x80000001), 0);
        if (   pExtFeatureLeaf
            && (pExtFeatureLeaf->uEcx & X86_CPUID_AMD_FEATURE_ECX_SVM))
        {
            PCPUMCPUIDLEAF pSvmFeatureLeaf = cpumR3CpuIdGetExactLeaf(pCpum, 0x8000000a, 0);
            if (pSvmFeatureLeaf)
            {
                pSvmFeatureLeaf->uEax  = 0x1;
                pSvmFeatureLeaf->uEbx  = 0x8000;                                        /** @todo figure out virtual NASID. */
                pSvmFeatureLeaf->uEcx  = 0;
                pSvmFeatureLeaf->uEdx &= (  X86_CPUID_SVM_FEATURE_EDX_NRIP_SAVE         /** @todo Support other SVM features */
                                          | X86_CPUID_SVM_FEATURE_EDX_FLUSH_BY_ASID
                                          | X86_CPUID_SVM_FEATURE_EDX_DECODE_ASSISTS);
            }
            else
            {
                /* Should never happen. */
                LogRel(("CPUM: Warning! Expected CPUID leaf 0x8000000a not present! SVM features not exposed to the guest\n"));
                cpumR3CpuIdZeroLeaf(pCpum, UINT32_C(0x8000000a));
            }
        }
        else
        {
            /* If SVM is not supported, this is reserved, zero out. */
            cpumR3CpuIdZeroLeaf(pCpum, UINT32_C(0x8000000a));
        }
    }
    else
    {
        /* Cpuid 0x8000000a: Reserved on Intel.
         * We zero this since we don't know what it may have been used for.
         */
        cpumR3CpuIdZeroLeaf(pCpum, UINT32_C(0x8000000a));
    }

    /* Cpuid 0x8000000b thru 0x80000018: Reserved
     * We clear these as we don't know what purpose they might have. */
    for (uint32_t uLeaf = UINT32_C(0x8000000b); uLeaf <= UINT32_C(0x80000018); uLeaf++)
        cpumR3CpuIdZeroLeaf(pCpum, uLeaf);

    /* Cpuid 0x80000019: TLB configuration
     * Seems to be harmless, pass them thru as is. */

    /* Cpuid 0x8000001a: Peformance optimization identifiers.
     * Strip anything we don't know what is or addresses feature we don't implement. */
    uSubLeaf = 0;
    while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0x8000001a), uSubLeaf)) != NULL)
    {
        pCurLeaf->uEax &= RT_BIT_32(0) /* FP128 - use 1x128-bit instead of 2x64-bit. */
                        | RT_BIT_32(1) /* MOVU - Prefere unaligned MOV over MOVL + MOVH. */
                        //| RT_BIT_32(2) /* FP256 - use 1x256-bit instead of 2x128-bit. */
                        ;
        pCurLeaf->uEbx  = 0;  /* reserved */
        pCurLeaf->uEcx  = 0;  /* reserved */
        pCurLeaf->uEdx  = 0;  /* reserved */
        uSubLeaf++;
    }

    /* Cpuid 0x8000001b: Instruct based sampling (IBS) information.
     * Clear this as we don't currently virtualize this feature. */
    cpumR3CpuIdZeroLeaf(pCpum, UINT32_C(0x8000001b));

    /* Cpuid 0x8000001c: Lightweight profiling (LWP) information.
     * Clear this as we don't currently virtualize this feature. */
    cpumR3CpuIdZeroLeaf(pCpum, UINT32_C(0x8000001c));

    /* Cpuid 0x8000001d+ECX: Get cache configuration descriptors.
     * We need to sanitize the cores per cache (EAX[25:14]).
     *
     * This is very much the same as Intel's CPUID(4) leaf, except EAX[31:26]
     * and EDX[2] are reserved here, and EAX[14:25] is documented having a
     * slightly different meaning.
     */
    uSubLeaf = 0;
    while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0x8000001d), uSubLeaf)) != NULL)
    {
#ifdef VBOX_WITH_MULTI_CORE
        uint32_t cCores = ((pCurLeaf->uEax >> 14) & 0xfff) + 1;
        if (cCores > pVM->cCpus)
            cCores = pVM->cCpus;
        pCurLeaf->uEax &= UINT32_C(0x00003fff);
        pCurLeaf->uEax |= ((cCores - 1) & 0xfff) << 14;
#else
        pCurLeaf->uEax &= UINT32_C(0x00003fff);
#endif
        uSubLeaf++;
    }

    /* Cpuid 0x8000001e: Get APIC / unit / node information.
     * If AMD, we configure it for our layout (on EMT(0)).  In the multi-core
     * setup, we have one compute unit with all the cores in it.  Single node.
     */
    uSubLeaf = 0;
    while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0x8000001e), uSubLeaf)) != NULL)
    {
        pCurLeaf->uEax = 0; /* Extended APIC ID = EMT(0).idApic (== 0).  */
        if (pCurLeaf->fFlags & CPUMCPUIDLEAF_F_CONTAINS_APIC_ID)
        {
#ifdef VBOX_WITH_MULTI_CORE
            pCurLeaf->uEbx = pVM->cCpus < 0x100
                           ? (pVM->cCpus - 1) << 8 : UINT32_C(0x0000ff00); /* Compute unit ID 0, core per unit. */
#else
            pCurLeaf->uEbx = 0; /* Compute unit ID 0, 1 core per unit. */
#endif
            pCurLeaf->uEcx = 0; /* Node ID 0, 1 node per CPU. */
        }
        else
        {
            Assert(pCpum->GuestFeatures.enmCpuVendor != CPUMCPUVENDOR_AMD);
            Assert(pCpum->GuestFeatures.enmCpuVendor != CPUMCPUVENDOR_HYGON);
            pCurLeaf->uEbx = 0; /* Reserved. */
            pCurLeaf->uEcx = 0; /* Reserved. */
        }
        pCurLeaf->uEdx = 0; /* Reserved. */
        uSubLeaf++;
    }

    /* Cpuid 0x8000001f...0x8ffffffd: Unknown.
     * We don't know these and what they mean, so remove them. */
    cpumR3CpuIdRemoveRange(pCpum->GuestInfo.paCpuIdLeavesR3, &pCpum->GuestInfo.cCpuIdLeaves,
                           UINT32_C(0x8000001f), UINT32_C(0x8ffffffd));

    /* Cpuid 0x8ffffffe: Mystery AMD K6 leaf.
     * Just pass it thru for now. */

    /* Cpuid 0x8fffffff: Mystery hammer time leaf!
     * Just pass it thru for now. */

    /* Cpuid 0xc0000000: Centaur stuff.
     * Harmless, pass it thru. */

    /* Cpuid 0xc0000001: Centaur features.
     * VIA: EAX - Family, model, stepping.
     *      EDX - Centaur extended feature flags.  Nothing interesting, except may
     *            FEMMS (bit 5), but VIA marks it as 'reserved', so never mind.
     *      EBX, ECX - reserved.
     * We keep EAX but strips the rest.
     */
    uSubLeaf = 0;
    while ((pCurLeaf = cpumR3CpuIdGetExactLeaf(pCpum, UINT32_C(0xc0000001), uSubLeaf)) != NULL)
    {
        pCurLeaf->uEbx = 0;
        pCurLeaf->uEcx = 0;
        pCurLeaf->uEdx = 0; /* Bits 0 thru 9 are documented on sandpil.org, but we don't want them, except maybe 5 (FEMMS). */
        uSubLeaf++;
    }

    /* Cpuid 0xc0000002: Old Centaur Current Performance Data.
     * We only have fixed stale values, but should be harmless. */

    /* Cpuid 0xc0000003: Reserved.
     * We zero this since we don't know what it may have been used for.
     */
    cpumR3CpuIdZeroLeaf(pCpum, UINT32_C(0xc0000003));

    /* Cpuid 0xc0000004: Centaur Performance Info.
     * We only have fixed stale values, but should be harmless. */


    /* Cpuid 0xc0000005...0xcfffffff: Unknown.
     * We don't know these and what they mean, so remove them. */
    cpumR3CpuIdRemoveRange(pCpum->GuestInfo.paCpuIdLeavesR3, &pCpum->GuestInfo.cCpuIdLeaves,
                           UINT32_C(0xc0000005), UINT32_C(0xcfffffff));

    return VINF_SUCCESS;
#undef PORTABLE_DISABLE_FEATURE_BIT
#undef PORTABLE_CLEAR_BITS_WHEN
}


/**
 * Reads a value in /CPUM/IsaExts/ node.
 *
 * @returns VBox status code (error message raised).
 * @param   pVM             The cross context VM structure. (For errors.)
 * @param   pIsaExts        The /CPUM/IsaExts node (can be NULL).
 * @param   pszValueName    The value / extension name.
 * @param   penmValue       Where to return the choice.
 * @param   enmDefault      The default choice.
 */
static int cpumR3CpuIdReadIsaExtCfg(PVM pVM, PCFGMNODE pIsaExts, const char *pszValueName,
                                    CPUMISAEXTCFG *penmValue, CPUMISAEXTCFG enmDefault)
{
    /*
     * Try integer encoding first.
     */
    uint64_t uValue;
    int rc = CFGMR3QueryInteger(pIsaExts, pszValueName, &uValue);
    if (RT_SUCCESS(rc))
        switch (uValue)
        {
            case 0: *penmValue = CPUMISAEXTCFG_DISABLED; break;
            case 1: *penmValue = CPUMISAEXTCFG_ENABLED_SUPPORTED; break;
            case 2: *penmValue = CPUMISAEXTCFG_ENABLED_ALWAYS; break;
            case 9: *penmValue = CPUMISAEXTCFG_ENABLED_PORTABLE; break;
            default:
                return VMSetError(pVM, VERR_CPUM_INVALID_CONFIG_VALUE, RT_SRC_POS,
                                  "Invalid config value for '/CPUM/IsaExts/%s': %llu (expected 0/'disabled', 1/'enabled', 2/'portable', or 9/'forced')",
                                  pszValueName, uValue);
        }
    /*
     * If missing, use default.
     */
    else if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
        *penmValue = enmDefault;
    else
    {
        if (rc == VERR_CFGM_NOT_INTEGER)
        {
            /*
             * Not an integer, try read it as a string.
             */
            char szValue[32];
            rc = CFGMR3QueryString(pIsaExts, pszValueName, szValue, sizeof(szValue));
            if (RT_SUCCESS(rc))
            {
                RTStrToLower(szValue);
                size_t cchValue = strlen(szValue);
#define EQ(a_str) (cchValue == sizeof(a_str) - 1U && memcmp(szValue, a_str, sizeof(a_str) - 1))
                if (     EQ("disabled") || EQ("disable") || EQ("off") || EQ("no"))
                    *penmValue = CPUMISAEXTCFG_DISABLED;
                else if (EQ("enabled")  || EQ("enable")  || EQ("on")  || EQ("yes"))
                    *penmValue = CPUMISAEXTCFG_ENABLED_SUPPORTED;
                else if (EQ("forced")   || EQ("force")   || EQ("always"))
                    *penmValue = CPUMISAEXTCFG_ENABLED_ALWAYS;
                else if (EQ("portable"))
                    *penmValue = CPUMISAEXTCFG_ENABLED_PORTABLE;
                else if (EQ("default")  || EQ("def"))
                    *penmValue = enmDefault;
                else
                    return VMSetError(pVM, VERR_CPUM_INVALID_CONFIG_VALUE, RT_SRC_POS,
                                      "Invalid config value for '/CPUM/IsaExts/%s': '%s' (expected 0/'disabled', 1/'enabled', 2/'portable', or 9/'forced')",
                                      pszValueName, uValue);
#undef EQ
            }
        }
        if (RT_FAILURE(rc))
            return VMSetError(pVM, rc, RT_SRC_POS, "Error reading config value '/CPUM/IsaExts/%s': %Rrc", pszValueName, rc);
    }
    return VINF_SUCCESS;
}


/**
 * Reads a value in /CPUM/IsaExts/ node, forcing it to DISABLED if wanted.
 *
 * @returns VBox status code (error message raised).
 * @param   pVM             The cross context VM structure. (For errors.)
 * @param   pIsaExts        The /CPUM/IsaExts node (can be NULL).
 * @param   pszValueName    The value / extension name.
 * @param   penmValue       Where to return the choice.
 * @param   enmDefault      The default choice.
 * @param   fAllowed        Allowed choice.  Applied both to the result and to
 *                          the default value.
 */
static int cpumR3CpuIdReadIsaExtCfgEx(PVM pVM, PCFGMNODE pIsaExts, const char *pszValueName,
                                      CPUMISAEXTCFG *penmValue, CPUMISAEXTCFG enmDefault, bool fAllowed)
{
    int rc;
    if (fAllowed)
        rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, pszValueName, penmValue, enmDefault);
    else
    {
        rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, pszValueName, penmValue, false /*enmDefault*/);
        if (RT_SUCCESS(rc) && *penmValue == CPUMISAEXTCFG_ENABLED_ALWAYS)
            LogRel(("CPUM: Ignoring forced '%s'\n", pszValueName));
        *penmValue = CPUMISAEXTCFG_DISABLED;
    }
    return rc;
}


/**
 * Reads a value in /CPUM/IsaExts/ node that used to be located in /CPUM/.
 *
 * @returns VBox status code (error message raised).
 * @param   pVM             The cross context VM structure. (For errors.)
 * @param   pIsaExts        The /CPUM/IsaExts node (can be NULL).
 * @param   pCpumCfg        The /CPUM node (can be NULL).
 * @param   pszValueName    The value / extension name.
 * @param   penmValue       Where to return the choice.
 * @param   enmDefault      The default choice.
 */
static int cpumR3CpuIdReadIsaExtCfgLegacy(PVM pVM, PCFGMNODE pIsaExts, PCFGMNODE pCpumCfg, const char *pszValueName,
                                          CPUMISAEXTCFG *penmValue, CPUMISAEXTCFG enmDefault)
{
    if (CFGMR3Exists(pCpumCfg, pszValueName))
    {
        if (!CFGMR3Exists(pIsaExts, pszValueName))
            LogRel(("Warning: /CPUM/%s is deprecated, use /CPUM/IsaExts/%s instead.\n", pszValueName, pszValueName));
        else
            return VMSetError(pVM, VERR_DUPLICATE, RT_SRC_POS,
                              "Duplicate config values '/CPUM/%s' and '/CPUM/IsaExts/%s' - please remove the former!",
                              pszValueName, pszValueName);

        bool fLegacy;
        int rc = CFGMR3QueryBoolDef(pCpumCfg, pszValueName, &fLegacy, enmDefault != CPUMISAEXTCFG_DISABLED);
        if (RT_SUCCESS(rc))
        {
            *penmValue = fLegacy;
            return VINF_SUCCESS;
        }
        return VMSetError(pVM, VERR_DUPLICATE, RT_SRC_POS, "Error querying '/CPUM/%s': %Rrc", pszValueName, rc);
    }

    return cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, pszValueName, penmValue, enmDefault);
}


static int cpumR3CpuIdReadConfig(PVM pVM, PCPUMCPUIDCONFIG pConfig, PCFGMNODE pCpumCfg, bool fNestedPagingAndFullGuestExec)
{
    int rc;

    /** @cfgm{/CPUM/PortableCpuIdLevel, 8-bit, 0, 3, 0}
     * When non-zero CPUID features that could cause portability issues will be
     * stripped.  The higher the value the more features gets stripped.  Higher
     * values should only be used when older CPUs are involved since it may
     * harm performance and maybe also cause problems with specific guests. */
    rc = CFGMR3QueryU8Def(pCpumCfg, "PortableCpuIdLevel", &pVM->cpum.s.u8PortableCpuIdLevel, 0);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/GuestCpuName, string}
     * The name of the CPU we're to emulate.  The default is the host CPU.
     * Note! CPUs other than "host" one is currently unsupported. */
    rc = CFGMR3QueryStringDef(pCpumCfg, "GuestCpuName", pConfig->szCpuName, sizeof(pConfig->szCpuName), "host");
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/NT4LeafLimit, boolean, false}
     * Limit the number of standard CPUID leaves to 0..3 to prevent NT4 from
     * bugchecking with MULTIPROCESSOR_CONFIGURATION_NOT_SUPPORTED (0x3e).
     * This option corresponds somewhat to IA32_MISC_ENABLES.BOOT_NT4[bit 22].
     */
    rc = CFGMR3QueryBoolDef(pCpumCfg, "NT4LeafLimit", &pConfig->fNt4LeafLimit, false);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/InvariantTsc, boolean, true}
     * Pass-through the invariant TSC flag in 0x80000007 if available on the host
     * CPU. On AMD CPUs, users may wish to suppress it to avoid trouble from older
     * 64-bit linux guests which assume the presence of AMD performance counters
     * that we do not virtualize.
     */
    rc = CFGMR3QueryBoolDef(pCpumCfg, "InvariantTsc", &pConfig->fInvariantTsc, true);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/InvariantApic, boolean, true}
     * Set the Always Running APIC Timer (ARAT) flag in lea if true; otherwise
     * pass through the host setting. The Windows 10/11 HAL won't use APIC timers
     * unless the ARAT bit is set. Note that both Intel and AMD set this bit.
     */
    rc = CFGMR3QueryBoolDef(pCpumCfg, "InvariantApic", &pConfig->fInvariantApic, true);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/ForceVme, boolean, false}
     * Always expose the VME (Virtual-8086 Mode Extensions) capability if true.
     * By default the flag is passed thru as is from the host CPU, except
     * on AMD Ryzen CPUs where it's masked to avoid trouble with XP/Server 2003
     * guests and DOS boxes in general.
     */
    rc = CFGMR3QueryBoolDef(pCpumCfg, "ForceVme", &pConfig->fForceVme, false);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/MaxIntelFamilyModelStep, uint32_t, UINT32_MAX}
     * Restrict the reported CPU family+model+stepping of intel CPUs.  This is
     * probably going to be a temporary hack, so don't depend on this.
     * The 1st byte of the value is the stepping, the 2nd byte value is the model
     * number and the 3rd byte value is the family, and the 4th value must be zero.
     */
    rc = CFGMR3QueryU32Def(pCpumCfg, "MaxIntelFamilyModelStep", &pConfig->uMaxIntelFamilyModelStep, UINT32_MAX);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/MaxStdLeaf, uint32_t, 0x00000016}
     * The last standard leaf to keep.  The actual last value that is stored in EAX
     * is RT_MAX(CPUID[0].EAX,/CPUM/MaxStdLeaf).  Leaves beyond the max leaf are
     * removed.  (This works independently of and differently from NT4LeafLimit.)
     * The default is usually set to what we're able to reasonably sanitize.
     */
    rc = CFGMR3QueryU32Def(pCpumCfg, "MaxStdLeaf", &pConfig->uMaxStdLeaf, UINT32_C(0x00000016));
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/MaxExtLeaf, uint32_t, 0x8000001e}
     * The last extended leaf to keep.  The actual last value that is stored in EAX
     * is RT_MAX(CPUID[0x80000000].EAX,/CPUM/MaxStdLeaf).  Leaves beyond the max
     * leaf are removed.  The default is set to what we're able to sanitize.
     */
    rc = CFGMR3QueryU32Def(pCpumCfg, "MaxExtLeaf", &pConfig->uMaxExtLeaf, UINT32_C(0x8000001e));
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/MaxCentaurLeaf, uint32_t, 0xc0000004}
     * The last extended leaf to keep.  The actual last value that is stored in EAX
     * is RT_MAX(CPUID[0xc0000000].EAX,/CPUM/MaxCentaurLeaf).  Leaves beyond the max
     * leaf are removed.  The default is set to what we're able to sanitize.
     */
    rc = CFGMR3QueryU32Def(pCpumCfg, "MaxCentaurLeaf", &pConfig->uMaxCentaurLeaf, UINT32_C(0xc0000004));
    AssertLogRelRCReturn(rc, rc);

    bool fQueryNestedHwvirt = false
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
                           || pVM->cpum.s.HostFeatures.enmCpuVendor == CPUMCPUVENDOR_AMD
                           || pVM->cpum.s.HostFeatures.enmCpuVendor == CPUMCPUVENDOR_HYGON
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
                           || pVM->cpum.s.HostFeatures.enmCpuVendor == CPUMCPUVENDOR_INTEL
                           || pVM->cpum.s.HostFeatures.enmCpuVendor == CPUMCPUVENDOR_VIA
#endif
                           ;
    if (fQueryNestedHwvirt)
    {
        /** @cfgm{/CPUM/NestedHWVirt, bool, false}
         * Whether to expose the hardware virtualization (VMX/SVM) feature to the guest.
         * The default is false, and when enabled requires a 64-bit CPU with support for
         * nested-paging and AMD-V or unrestricted guest mode.
         */
        rc = CFGMR3QueryBoolDef(pCpumCfg, "NestedHWVirt", &pConfig->fNestedHWVirt, false);
        AssertLogRelRCReturn(rc, rc);
        if (pConfig->fNestedHWVirt)
        {
            /** @todo Think about enabling this later with NEM/KVM. */
            if (VM_IS_NEM_ENABLED(pVM))
            {
                LogRel(("CPUM: Warning! Can't turn on nested VT-x/AMD-V when NEM is used! (later)\n"));
                pConfig->fNestedHWVirt = false;
            }
            else if (!fNestedPagingAndFullGuestExec)
                return VMSetError(pVM, VERR_CPUM_INVALID_HWVIRT_CONFIG, RT_SRC_POS,
                                  "Cannot enable nested VT-x/AMD-V without nested-paging and unrestricted guest execution!\n");
        }
    }

    /*
     * Instruction Set Architecture (ISA) Extensions.
     */
    PCFGMNODE pIsaExts = CFGMR3GetChild(pCpumCfg, "IsaExts");
    if (pIsaExts)
    {
        rc = CFGMR3ValidateConfig(pIsaExts, "/CPUM/IsaExts/",
                                   "CMPXCHG16B"
                                  "|MONITOR"
                                  "|MWaitExtensions"
                                  "|SSE4.1"
                                  "|SSE4.2"
                                  "|XSAVE"
                                  "|AVX"
                                  "|AVX2"
                                  "|AESNI"
                                  "|PCLMUL"
                                  "|POPCNT"
                                  "|MOVBE"
                                  "|RDRAND"
                                  "|RDSEED"
                                  "|ADX"
                                  "|CLFLUSHOPT"
                                  "|SHA"
                                  "|FSGSBASE"
                                  "|PCID"
                                  "|INVPCID"
                                  "|FlushCmdMsr"
                                  "|FMA"
                                  "|F16C"
                                  "|McdtNo"
                                  "|MonitorMitgNo"
                                  "|ABM"
                                  "|SSE4A"
                                  "|MISALNSSE"
                                  "|3DNOWPRF"
                                  "|AXMMX"
                                  , "" /*pszValidNodes*/, "CPUM" /*pszWho*/, 0 /*uInstance*/);
        if (RT_FAILURE(rc))
            return rc;
    }

    /** @cfgm{/CPUM/IsaExts/CMPXCHG16B, boolean, true}
     * Expose CMPXCHG16B to the guest if available. All host CPUs which support
     * hardware virtualization have it.
     */
    rc = cpumR3CpuIdReadIsaExtCfgLegacy(pVM, pIsaExts, pCpumCfg, "CMPXCHG16B", &pConfig->enmCmpXchg16b, true);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/MONITOR, boolean, true}
     * Expose MONITOR/MWAIT instructions to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfgLegacy(pVM, pIsaExts, pCpumCfg, "MONITOR", &pConfig->enmMonitor, true);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/MWaitExtensions, boolean, false}
     * Expose MWAIT extended features to the guest.  For now we expose just MWAIT
     * break on interrupt feature (bit 1).
     */
    rc = cpumR3CpuIdReadIsaExtCfgLegacy(pVM, pIsaExts, pCpumCfg, "MWaitExtensions", &pConfig->enmMWaitExtensions, false);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/SSE4.1, boolean, true}
     * Expose SSE4.1 to the guest if available.
     */
    rc = cpumR3CpuIdReadIsaExtCfgLegacy(pVM, pIsaExts, pCpumCfg, "SSE4.1", &pConfig->enmSse41, true);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/SSE4.2, boolean, true}
     * Expose SSE4.2 to the guest if available.
     */
    rc = cpumR3CpuIdReadIsaExtCfgLegacy(pVM, pIsaExts, pCpumCfg, "SSE4.2", &pConfig->enmSse42, true);
    AssertLogRelRCReturn(rc, rc);

    bool const fMayHaveXSave = pVM->cpum.s.HostFeatures.fXSaveRstor
                            && pVM->cpum.s.HostFeatures.fOpSysXSaveRstor
                            && (  VM_IS_NEM_ENABLED(pVM)
                                ? NEMHCGetFeatures(pVM) & NEM_FEAT_F_XSAVE_XRSTOR
                                : VM_IS_EXEC_ENGINE_IEM(pVM)
                                ? true
                                : fNestedPagingAndFullGuestExec);
    uint64_t const fXStateHostMask = pVM->cpum.s.fXStateHostMask;

    /** @cfgm{/CPUM/IsaExts/XSAVE, boolean, depends}
     * Expose XSAVE/XRSTOR to the guest if available.  For the time being the
     * default is to only expose this to VMs with nested paging and AMD-V or
     * unrestricted guest execution mode.  Not possible to force this one without
     * host support at the moment.
     */
    rc = cpumR3CpuIdReadIsaExtCfgEx(pVM, pIsaExts, "XSAVE", &pConfig->enmXSave, true,
                                    fMayHaveXSave /*fAllowed*/);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/AVX, boolean, depends}
     * Expose the AVX instruction set extensions to the guest if available and
     * XSAVE is exposed too.  For the time being the default is to only expose this
     * to VMs with nested paging and AMD-V or unrestricted guest execution mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfgEx(pVM, pIsaExts, "AVX", &pConfig->enmAvx, fNestedPagingAndFullGuestExec,
                                    fMayHaveXSave && pConfig->enmXSave && (fXStateHostMask & XSAVE_C_YMM)  /*fAllowed*/);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/AVX2, boolean, depends}
     * Expose the AVX2 instruction set extensions to the guest if available and
     * XSAVE is exposed too. For the time being the default is to only expose this
     * to VMs with nested paging and AMD-V or unrestricted guest execution mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfgEx(pVM, pIsaExts, "AVX2", &pConfig->enmAvx2, fNestedPagingAndFullGuestExec /* temporarily */,
                                    fMayHaveXSave && pConfig->enmXSave && (fXStateHostMask & XSAVE_C_YMM) /*fAllowed*/);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/AESNI, isaextcfg, depends}
     * Whether to expose the AES instructions to the guest.  For the time being the
     * default is to only do this for VMs with nested paging and AMD-V or
     * unrestricted guest mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "AESNI", &pConfig->enmAesNi, fNestedPagingAndFullGuestExec);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/PCLMUL, isaextcfg, depends}
     * Whether to expose the PCLMULQDQ instructions to the guest.  For the time
     * being the default is to only do this for VMs with nested paging and AMD-V or
     * unrestricted guest mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "PCLMUL", &pConfig->enmPClMul, fNestedPagingAndFullGuestExec);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/POPCNT, isaextcfg, true}
     * Whether to expose the POPCNT instructions to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "POPCNT", &pConfig->enmPopCnt, CPUMISAEXTCFG_ENABLED_SUPPORTED);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/MOVBE, isaextcfg, depends}
     * Whether to expose the MOVBE instructions to the guest.  For the time
     * being the default is to only do this for VMs with nested paging and AMD-V or
     * unrestricted guest mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "MOVBE", &pConfig->enmMovBe, true);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/RDRAND, isaextcfg, depends}
     * Whether to expose the RDRAND instructions to the guest.  For the time being
     * the default is to only do this for VMs with nested paging and AMD-V or
     * unrestricted guest mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "RDRAND", &pConfig->enmRdRand, fNestedPagingAndFullGuestExec);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/RDSEED, isaextcfg, depends}
     * Whether to expose the RDSEED instructions to the guest.  For the time being
     * the default is to only do this for VMs with nested paging and AMD-V or
     * unrestricted guest mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "RDSEED", &pConfig->enmRdSeed, fNestedPagingAndFullGuestExec);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/ADX, isaextcfg, depends}
     * Whether to expose the ADX instructions to the guest.  For the time being
     * the default is to only do this for VMs with nested paging and AMD-V or
     * unrestricted guest mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "ADX", &pConfig->enmAdx, fNestedPagingAndFullGuestExec);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/CLFLUSHOPT, isaextcfg, depends}
     * Whether to expose the CLFLUSHOPT instructions to the guest.  For the time
     * being the default is to only do this for VMs with nested paging and AMD-V or
     * unrestricted guest mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "CLFLUSHOPT", &pConfig->enmCLFlushOpt, fNestedPagingAndFullGuestExec);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/SHA, isaextcfg, depends}
     * Whether to expose the SHA instructions to the guest.  For the time being
     * the default is to only do this for VMs with nested paging and AMD-V or
     * unrestricted guest mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "SHA", &pConfig->enmSha, fNestedPagingAndFullGuestExec);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/FSGSBASE, isaextcfg, true}
     * Whether to expose the read/write FSGSBASE instructions to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "FSGSBASE", &pConfig->enmFsGsBase, true);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/PCID, isaextcfg, true}
     * Whether to expose the PCID feature to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "PCID", &pConfig->enmPcid, pConfig->enmFsGsBase);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/INVPCID, isaextcfg, true}
     * Whether to expose the INVPCID instruction to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "INVPCID", &pConfig->enmInvpcid, pConfig->enmFsGsBase);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/FlushCmdMsr, isaextcfg, true}
     * Whether to expose the IA32_FLUSH_CMD MSR to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "FlushCmdMsr", &pConfig->enmFlushCmdMsr, CPUMISAEXTCFG_ENABLED_SUPPORTED);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/MdsClear, isaextcfg, true}
     * Whether to advertise the VERW and MDS related IA32_FLUSH_CMD MSR bits to
     * the guest.  Requires FlushCmdMsr to be present too.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "MdsClear", &pConfig->enmMdsClear, CPUMISAEXTCFG_ENABLED_SUPPORTED);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/ArchCapMSr, isaextcfg, true}
     * Whether to expose the MSR_IA32_ARCH_CAPABILITIES MSR to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "ArchCapMsr", &pConfig->enmArchCapMsr, CPUMISAEXTCFG_ENABLED_SUPPORTED_OR_NOT_AMD64);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/FMA, boolean, depends}
     * Expose the FMA instruction set extensions to the guest if available and
     * XSAVE is exposed too. For the time being the default is to only expose this
     * to VMs with nested paging and AMD-V or unrestricted guest execution mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfgEx(pVM, pIsaExts, "FMA", &pConfig->enmFma, fNestedPagingAndFullGuestExec /* temporarily */,
                                    fMayHaveXSave && pConfig->enmXSave && (fXStateHostMask & XSAVE_C_YMM) /*fAllowed*/);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/F16C, boolean, depends}
     * Expose the F16C instruction set extensions to the guest if available and
     * XSAVE is exposed too. For the time being the default is to only expose this
     * to VMs with nested paging and AMD-V or unrestricted guest execution mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfgEx(pVM, pIsaExts, "F16C", &pConfig->enmF16c, fNestedPagingAndFullGuestExec /* temporarily */,
                                    fMayHaveXSave && pConfig->enmXSave && (fXStateHostMask & XSAVE_C_YMM) /*fAllowed*/);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/McdtNo, isaextcfg, true}
     * Whether the CPU is not susceptible to the MXCSR configuration dependent
     * timing (MCDT) behaviour.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "McdtNo", &pConfig->enmMcdtNo, CPUMISAEXTCFG_ENABLED_SUPPORTED);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/MonitorMitgNo, isaextcfg, true}
     * Whether the CPU is not susceptible MONITOR/UMONITOR internal table capacity
     * issues.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "MonitorMitgNo", &pConfig->enmMonitorMitgNo, CPUMISAEXTCFG_ENABLED_SUPPORTED);
    AssertLogRelRCReturn(rc, rc);


    /* AMD: */

    /** @cfgm{/CPUM/IsaExts/ABM, isaextcfg, true}
     * Whether to expose the AMD ABM instructions to the guest.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "ABM", &pConfig->enmAbm, CPUMISAEXTCFG_ENABLED_SUPPORTED);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/SSE4A, isaextcfg, depends}
     * Whether to expose the AMD SSE4A instructions to the guest.  For the time
     * being the default is to only do this for VMs with nested paging and AMD-V or
     * unrestricted guest mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "SSE4A", &pConfig->enmSse4A, fNestedPagingAndFullGuestExec);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/MISALNSSE, isaextcfg, depends}
     * Whether to expose the AMD MisAlSse feature (MXCSR flag 17) to the guest.  For
     * the time being the default is to only do this for VMs with nested paging and
     * AMD-V or unrestricted guest mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "MISALNSSE", &pConfig->enmMisAlnSse, fNestedPagingAndFullGuestExec);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/3DNOWPRF, isaextcfg, depends}
     * Whether to expose the AMD 3D Now! prefetch instructions to the guest.
     * For the time being the default is to only do this for VMs with nested paging
     * and AMD-V or unrestricted guest mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "3DNOWPRF", &pConfig->enm3dNowPrf, fNestedPagingAndFullGuestExec);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/CPUM/IsaExts/AXMMX, isaextcfg, depends}
     * Whether to expose the AMD's MMX Extensions to the guest.  For the time being
     * the default is to only do this for VMs with nested paging and AMD-V or
     * unrestricted guest mode.
     */
    rc = cpumR3CpuIdReadIsaExtCfg(pVM, pIsaExts, "AXMMX", &pConfig->enmAmdExtMmx, fNestedPagingAndFullGuestExec);
    AssertLogRelRCReturn(rc, rc);

    return VINF_SUCCESS;
}


/**
 * Checks and fixes the maximum physical address width supported by the
 * variable-range MTRR MSRs to be consistent with what is reported in CPUID.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   cVarMtrrs   The number of variable-range MTRRs reported to the guest.
 */
static int cpumR3FixVarMtrrPhysAddrWidths(PVM pVM, uint8_t const cVarMtrrs)
{
    AssertLogRelMsgReturn(cVarMtrrs <= RT_ELEMENTS(pVM->apCpusR3[0]->cpum.s.GuestMsrs.msr.aMtrrVarMsrs),
                          ("Invalid number of variable range MTRRs reported (%u)\n", cVarMtrrs),
                          VERR_CPUM_IPE_2);

    /*
     * CPUID determines the actual maximum physical address width reported and supported.
     * If the CPU DB profile reported fewer address bits, we must correct it here by
     * updating the MSR write #GP masks of all the variable-range MTRR MSRs. Otherwise,
     * they cause problems when guests write to these MTRR MSRs, see @bugref{10498#c32}.
     */
    PCPUMMSRRANGE pBaseRange0 = cpumLookupMsrRange(pVM, MSR_IA32_MTRR_PHYSBASE0);
    AssertLogRelMsgReturn(pBaseRange0, ("Failed to lookup the IA32_MTRR_PHYSBASE[0] MSR range\n"), VERR_NOT_FOUND);

    PCPUMMSRRANGE pMaskRange0 = cpumLookupMsrRange(pVM, MSR_IA32_MTRR_PHYSMASK0);
    AssertLogRelMsgReturn(pMaskRange0, ("Failed to lookup the IA32_MTRR_PHYSMASK[0] MSR range\n"), VERR_NOT_FOUND);

    uint64_t const fPhysBaseWrGpMask = pBaseRange0->fWrGpMask;
    uint64_t const fPhysMaskWrGpMask = pMaskRange0->fWrGpMask;

    uint8_t const  cGuestMaxPhysAddrWidth           = pVM->cpum.s.GuestFeatures.cMaxPhysAddrWidth;
    uint8_t const  cProfilePhysBaseMaxPhysAddrWidth = ASMBitLastSetU64(~fPhysBaseWrGpMask);
    uint8_t const  cProfilePhysMaskMaxPhysAddrWidth = ASMBitLastSetU64(~fPhysMaskWrGpMask);

    AssertLogRelMsgReturn(cProfilePhysBaseMaxPhysAddrWidth == cProfilePhysMaskMaxPhysAddrWidth,
                          ("IA32_MTRR_PHYSBASE and IA32_MTRR_PHYSMASK report different physical address widths (%u and %u)\n",
                           cProfilePhysBaseMaxPhysAddrWidth, cProfilePhysMaskMaxPhysAddrWidth),
                          VERR_CPUM_IPE_2);
    AssertLogRelMsgReturn(cProfilePhysBaseMaxPhysAddrWidth > 12 && cProfilePhysBaseMaxPhysAddrWidth <= 64,
                          ("IA32_MTRR_PHYSBASE and IA32_MTRR_PHYSMASK reports an invalid physical address width of %u bits\n",
                           cProfilePhysBaseMaxPhysAddrWidth), VERR_CPUM_IPE_2);

    if (cProfilePhysBaseMaxPhysAddrWidth < cGuestMaxPhysAddrWidth)
    {
        uint64_t fNewPhysBaseWrGpMask = fPhysBaseWrGpMask;
        uint64_t fNewPhysMaskWrGpMask = fPhysMaskWrGpMask;
        int8_t   cBits = cGuestMaxPhysAddrWidth - cProfilePhysBaseMaxPhysAddrWidth;
        while (cBits)
        {
            uint64_t const fWrGpAndMask = ~(uint64_t)RT_BIT_64(cProfilePhysBaseMaxPhysAddrWidth + cBits - 1);
            fNewPhysBaseWrGpMask &= fWrGpAndMask;
            fNewPhysMaskWrGpMask &= fWrGpAndMask;
            --cBits;
        }

        for (uint8_t iVarMtrr = 1; iVarMtrr < cVarMtrrs; iVarMtrr++)
        {
            PCPUMMSRRANGE pBaseRange = cpumLookupMsrRange(pVM, MSR_IA32_MTRR_PHYSBASE0 + (iVarMtrr * 2));
            AssertLogRelMsgReturn(pBaseRange, ("Failed to lookup the IA32_MTRR_PHYSBASE[%u] MSR range\n", iVarMtrr),
                                  VERR_NOT_FOUND);

            PCPUMMSRRANGE pMaskRange = cpumLookupMsrRange(pVM, MSR_IA32_MTRR_PHYSMASK0 + (iVarMtrr * 2));
            AssertLogRelMsgReturn(pMaskRange, ("Failed to lookup the IA32_MTRR_PHYSMASK[%u] MSR range\n", iVarMtrr),
                                  VERR_NOT_FOUND);

            AssertLogRelMsgReturn(pBaseRange->fWrGpMask == fPhysBaseWrGpMask,
                                  ("IA32_MTRR_PHYSBASE[%u] write GP mask (%#016RX64) differs from IA32_MTRR_PHYSBASE[0] write GP mask (%#016RX64)\n",
                                   iVarMtrr, pBaseRange->fWrGpMask, fPhysBaseWrGpMask),
                                  VERR_CPUM_IPE_1);
            AssertLogRelMsgReturn(pMaskRange->fWrGpMask == fPhysMaskWrGpMask,
                                  ("IA32_MTRR_PHYSMASK[%u] write GP mask (%#016RX64) differs from IA32_MTRR_PHYSMASK[0] write GP mask (%#016RX64)\n",
                                   iVarMtrr, pMaskRange->fWrGpMask, fPhysMaskWrGpMask),
                                  VERR_CPUM_IPE_1);

            pBaseRange->fWrGpMask = fNewPhysBaseWrGpMask;
            pMaskRange->fWrGpMask = fNewPhysMaskWrGpMask;
        }

        pBaseRange0->fWrGpMask = fNewPhysBaseWrGpMask;
        pMaskRange0->fWrGpMask = fNewPhysMaskWrGpMask;

        LogRel(("CPUM: Updated IA32_MTRR_PHYSBASE[0..%u] MSR write #GP mask (old=%#016RX64 new=%#016RX64)\n",
                cVarMtrrs - 1, fPhysBaseWrGpMask, fNewPhysBaseWrGpMask));
        LogRel(("CPUM: Updated IA32_MTRR_PHYSMASK[0..%u] MSR write #GP mask (old=%#016RX64 new=%#016RX64)\n",
                cVarMtrrs - 1, fPhysMaskWrGpMask, fNewPhysMaskWrGpMask));
    }

    return VINF_SUCCESS;
}


/**
 * Inserts variable-range MTRR MSR ranges based on the given count.
 *
 * Since we need to insert the MSRs beyond what the CPU profile has inserted, we
 * reinsert the whole range here since the variable-range MTRR MSR read+write
 * functions handle ranges as well as the \#GP checking.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   cVarMtrrs   The number of variable-range MTRRs to insert. This must be
 *                      less than or equal to CPUMCTX_MAX_MTRRVAR_COUNT.
 */
static int cpumR3VarMtrrMsrRangeInsert(PVM pVM, uint8_t const cVarMtrrs)
{
#ifdef VBOX_WITH_STATISTICS
# define CPUM_MTRR_PHYSBASE_MSRRANGE(a_uMsr, a_uValue, a_szName) \
    { (a_uMsr), (a_uMsr), kCpumMsrRdFn_Ia32MtrrPhysBaseN, kCpumMsrWrFn_Ia32MtrrPhysBaseN, 0, 0, a_uValue, 0, 0, a_szName, { 0 }, { 0 }, { 0 }, { 0 } }
# define CPUM_MTRR_PHYSMASK_MSRRANGE(a_uMsr, a_uValue, a_szName) \
    { (a_uMsr), (a_uMsr), kCpumMsrRdFn_Ia32MtrrPhysMaskN, kCpumMsrWrFn_Ia32MtrrPhysMaskN, 0, 0, a_uValue, 0, 0, a_szName, { 0 }, { 0 }, { 0 }, { 0 } }
#else
# define CPUM_MTRR_PHYSBASE_MSRRANGE(a_uMsr, a_uValue, a_szName) \
    { (a_uMsr), (a_uMsr), kCpumMsrRdFn_Ia32MtrrPhysBaseN, kCpumMsrWrFn_Ia32MtrrPhysBaseN, 0, 0, a_uValue, 0, 0, a_szName }
# define CPUM_MTRR_PHYSMASK_MSRRANGE(a_uMsr, a_uValue, a_szName) \
    { (a_uMsr), (a_uMsr), kCpumMsrRdFn_Ia32MtrrPhysMaskN, kCpumMsrWrFn_Ia32MtrrPhysMaskN, 0, 0, a_uValue, 0, 0, a_szName }
#endif
    static CPUMMSRRANGE const s_aMsrRanges_MtrrPhysBase[CPUMCTX_MAX_MTRRVAR_COUNT] =
    {
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE0,       0, "MSR_IA32_MTRR_PHYSBASE0"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE1,       1, "MSR_IA32_MTRR_PHYSBASE1"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE2,       2, "MSR_IA32_MTRR_PHYSBASE2"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE3,       3, "MSR_IA32_MTRR_PHYSBASE3"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE4,       4, "MSR_IA32_MTRR_PHYSBASE4"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE5,       5, "MSR_IA32_MTRR_PHYSBASE5"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE6,       6, "MSR_IA32_MTRR_PHYSBASE6"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE7,       7, "MSR_IA32_MTRR_PHYSBASE7"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE8,       8, "MSR_IA32_MTRR_PHYSBASE8"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE9,       9, "MSR_IA32_MTRR_PHYSBASE9"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE9 +  2, 10, "MSR_IA32_MTRR_PHYSBASE10"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE9 +  4, 11, "MSR_IA32_MTRR_PHYSBASE11"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE9 +  6, 12, "MSR_IA32_MTRR_PHYSBASE12"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE9 +  8, 13, "MSR_IA32_MTRR_PHYSBASE13"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE9 + 10, 14, "MSR_IA32_MTRR_PHYSBASE14"),
        CPUM_MTRR_PHYSBASE_MSRRANGE(MSR_IA32_MTRR_PHYSBASE9 + 12, 15, "MSR_IA32_MTRR_PHYSBASE15"),
    };
    static CPUMMSRRANGE const s_aMsrRanges_MtrrPhysMask[CPUMCTX_MAX_MTRRVAR_COUNT] =
    {
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK0,       0, "MSR_IA32_MTRR_PHYSMASK0"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK1,       1, "MSR_IA32_MTRR_PHYSMASK1"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK2,       2, "MSR_IA32_MTRR_PHYSMASK2"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK3,       3, "MSR_IA32_MTRR_PHYSMASK3"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK4,       4, "MSR_IA32_MTRR_PHYSMASK4"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK5,       5, "MSR_IA32_MTRR_PHYSMASK5"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK6,       6, "MSR_IA32_MTRR_PHYSMASK6"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK7,       7, "MSR_IA32_MTRR_PHYSMASK7"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK8,       8, "MSR_IA32_MTRR_PHYSMASK8"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK9,       9, "MSR_IA32_MTRR_PHYSMASK9"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK9 +  2, 10, "MSR_IA32_MTRR_PHYSMASK10"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK9 +  4, 11, "MSR_IA32_MTRR_PHYSMASK11"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK9 +  6, 12, "MSR_IA32_MTRR_PHYSMASK12"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK9 +  8, 13, "MSR_IA32_MTRR_PHYSMASK13"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK9 + 10, 14, "MSR_IA32_MTRR_PHYSMASK14"),
        CPUM_MTRR_PHYSMASK_MSRRANGE(MSR_IA32_MTRR_PHYSMASK9 + 12, 15, "MSR_IA32_MTRR_PHYSMASK15"),
    };
    AssertCompile(RT_ELEMENTS(s_aMsrRanges_MtrrPhysBase) == RT_ELEMENTS(pVM->apCpusR3[0]->cpum.s.GuestMsrs.msr.aMtrrVarMsrs));
    AssertCompile(RT_ELEMENTS(s_aMsrRanges_MtrrPhysMask) == RT_ELEMENTS(pVM->apCpusR3[0]->cpum.s.GuestMsrs.msr.aMtrrVarMsrs));

    Assert(cVarMtrrs <= RT_ELEMENTS(pVM->apCpusR3[0]->cpum.s.GuestMsrs.msr.aMtrrVarMsrs));
    for (unsigned i = 0; i < cVarMtrrs; i++)
    {
        int rc = CPUMR3MsrRangesInsert(pVM, &s_aMsrRanges_MtrrPhysBase[i]);
        AssertLogRelRCReturn(rc, rc);
        rc     = CPUMR3MsrRangesInsert(pVM, &s_aMsrRanges_MtrrPhysMask[i]);
        AssertLogRelRCReturn(rc, rc);
    }
    return VINF_SUCCESS;

#undef CPUM_MTRR_PHYSBASE_MSRRANGE
#undef CPUM_MTRR_PHYSMASK_MSRRANGE
}


/**
 * Initialize MTRR capability based on what the guest CPU profile (typically host)
 * supports.
 *
 * @returns VBox status code.
 * @param   pVM                     The cross context VM structure.
 * @param   fMtrrVarCountIsVirt     Whether the variable-range MTRR count is fully
 *                                  virtualized (@c true) or derived from the CPU
 *                                  profile (@c false).
 */
static int cpumR3InitMtrrCap(PVM pVM, bool fMtrrVarCountIsVirt)
{
#ifdef RT_ARCH_AMD64
    Assert(pVM->cpum.s.HostFeatures.fMtrr);
#endif

    /* Lookup the number of variable-range MTRRs supported by the CPU profile. */
    PCCPUMMSRRANGE pMtrrCapRange = cpumLookupMsrRange(pVM, MSR_IA32_MTRR_CAP);
    AssertLogRelMsgReturn(pMtrrCapRange, ("Failed to lookup IA32_MTRR_CAP MSR range\n"), VERR_NOT_FOUND);
    uint8_t const cProfileVarRangeRegs = pMtrrCapRange->uValue & MSR_IA32_MTRR_CAP_VCNT_MASK;

    /* Construct guest MTRR support capabilities. */
    uint8_t const  cGuestVarRangeRegs = fMtrrVarCountIsVirt ? CPUMCTX_MAX_MTRRVAR_COUNT
                                                            : RT_MIN(cProfileVarRangeRegs, CPUMCTX_MAX_MTRRVAR_COUNT);
    uint64_t const uGstMtrrCap        = cGuestVarRangeRegs
                                      | MSR_IA32_MTRR_CAP_FIX
                                      | MSR_IA32_MTRR_CAP_WC;
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        pVCpu->cpum.s.GuestMsrs.msr.MtrrCap     = uGstMtrrCap;
        pVCpu->cpum.s.GuestMsrs.msr.MtrrDefType = MSR_IA32_MTRR_DEF_TYPE_FIXED_EN
                                                | MSR_IA32_MTRR_DEF_TYPE_MTRR_EN
                                                | X86_MTRR_MT_UC;
    }

    if (fMtrrVarCountIsVirt)
    {
        /*
         * Insert the full variable-range MTRR MSR range ourselves so it extends beyond what is
         * typically reported by the hardware CPU profile.
         */
        LogRel(("CPUM: Enabled fixed-range MTRRs and %u (virtualized) variable-range MTRRs\n", cGuestVarRangeRegs));
        return cpumR3VarMtrrMsrRangeInsert(pVM, cGuestVarRangeRegs);
    }

    /*
     * Ensure that the maximum physical address width supported by the variable-range MTRRs
     * are consistent with what is reported to the guest via CPUID.
     */
    LogRel(("CPUM: Enabled fixed-range MTRRs and %u (CPU profile derived) variable-range MTRRs\n", cGuestVarRangeRegs));
    return cpumR3FixVarMtrrPhysAddrWidths(pVM, cGuestVarRangeRegs);
}


/**
 * Initializes the emulated CPU's CPUID & MSR information.
 *
 * @returns VBox status code.
 * @param   pVM          The cross context VM structure.
 * @param   pHostMsrs    Pointer to the host MSRs.
 */
int cpumR3InitCpuIdAndMsrs(PVM pVM, PCCPUMMSRS pHostMsrs)
{
    Assert(pHostMsrs);

    PCPUM       pCpum    = &pVM->cpum.s;
    PCFGMNODE   pCpumCfg = CFGMR3GetChild(CFGMR3GetRoot(pVM), "CPUM");

    /*
     * Set the fCpuIdApicFeatureVisible flags so the APIC can assume visibility
     * on construction and manage everything from here on.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        pVCpu->cpum.s.fCpuIdApicFeatureVisible = true;
    }

    /*
     * Read the configuration.
     */
    CPUMCPUIDCONFIG Config;
    RT_ZERO(Config);

    bool const fNestedPagingAndFullGuestExec = VM_IS_NEM_ENABLED(pVM)
                                            || HMAreNestedPagingAndFullGuestExecEnabled(pVM);
    int rc = cpumR3CpuIdReadConfig(pVM, &Config, pCpumCfg, fNestedPagingAndFullGuestExec);
    AssertRCReturn(rc, rc);

    /*
     * Get the guest CPU data from the database and/or the host.
     *
     * The CPUID and MSRs are currently living on the regular heap to avoid
     * fragmenting the hyper heap (and because there isn't/wasn't any realloc
     * API for the hyper heap).  This means special cleanup considerations.
     */
    /** @todo The hyper heap will be removed ASAP, so the final destination is
     *        now a fixed sized arrays in the VM structure.  Maybe we can simplify
     *        this allocation fun a little now?  Or maybe it's too convenient for
     *        the CPU reporter code... No time to figure that out now.   */
    rc = cpumR3DbGetCpuInfo(Config.szCpuName, &pCpum->GuestInfo);
    if (RT_FAILURE(rc))
        return rc == VERR_CPUM_DB_CPU_NOT_FOUND
             ? VMSetError(pVM, rc, RT_SRC_POS,
                          "Info on guest CPU '%s' could not be found. Please, select a different CPU.", Config.szCpuName)
             : rc;

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    if (pCpum->GuestInfo.fMxCsrMask & ~pVM->cpum.s.fHostMxCsrMask)
    {
        LogRel(("Stripping unsupported MXCSR bits from guest mask: %#x -> %#x (host: %#x)\n", pCpum->GuestInfo.fMxCsrMask,
                pCpum->GuestInfo.fMxCsrMask & pVM->cpum.s.fHostMxCsrMask, pVM->cpum.s.fHostMxCsrMask));
        pCpum->GuestInfo.fMxCsrMask &= pVM->cpum.s.fHostMxCsrMask;
    }
    LogRel(("CPUM: MXCSR_MASK=%#x (host: %#x)\n", pCpum->GuestInfo.fMxCsrMask, pVM->cpum.s.fHostMxCsrMask));
#else
    LogRel(("CPUM: MXCSR_MASK=%#x\n", pCpum->GuestInfo.fMxCsrMask));
#endif

    /** @cfgm{/CPUM/GuestMicrocodeRev,32-bit}
     * CPU microcode revision number to use.  If UINT32_MAX we use the host
     * revision of the host CPU for the host-cpu profile and the database entry if a
     * specific one is selected (amd64 host only). */
    rc = CFGMR3QueryU32Def(pCpumCfg, "GuestMicrocodeRevision", &pCpum->GuestInfo.uMicrocodeRevision, UINT32_MAX);
    AssertLogRelRCReturn(rc, rc);
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    if (   pCpum->GuestInfo.uMicrocodeRevision == UINT32_MAX
        && strcmp(Config.szCpuName, "host") == 0)
    {
        rc = SUPR3QueryMicrocodeRev(&pCpum->GuestInfo.uMicrocodeRevision);
        if (RT_FAILURE(rc))
            pCpum->GuestInfo.uMicrocodeRevision = UINT32_MAX;
    }
#endif

    /** @cfgm{/CPUM/MSRs/[Name]/[First|Last|Type|Value|...],}
     * Overrides the guest MSRs.
     */
    rc = cpumR3LoadMsrOverrides(pVM, CFGMR3GetChild(pCpumCfg, "MSRs"));

    /** @cfgm{/CPUM/HostCPUID/[000000xx|800000xx|c000000x]/[eax|ebx|ecx|edx],32-bit}
     * Overrides the CPUID leaf values (from the host CPU usually) used for
     * calculating the guest CPUID leaves.  This can be used to preserve the CPUID
     * values when moving a VM to a different machine.  Another use is restricting
     * (or extending) the feature set exposed to the guest. */
    if (RT_SUCCESS(rc))
        rc = cpumR3LoadCpuIdOverrides(pVM, CFGMR3GetChild(pCpumCfg, "HostCPUID"), "HostCPUID");

    if (RT_SUCCESS(rc) && CFGMR3GetChild(pCpumCfg, "CPUID")) /* 2nd override, now discontinued. */
        rc = VMSetError(pVM, VERR_CFGM_CONFIG_UNKNOWN_NODE, RT_SRC_POS,
                        "Found unsupported configuration node '/CPUM/CPUID/'. "
                        "Please use IMachine::setCPUIDLeaf() instead.");

    CPUMMSRS GuestMsrs;
    RT_ZERO(GuestMsrs);

    /*
     * Pre-explode the CPUID info.
     */
    if (RT_SUCCESS(rc))
        rc = cpumCpuIdExplodeFeaturesX86(pCpum->GuestInfo.paCpuIdLeavesR3, pCpum->GuestInfo.cCpuIdLeaves, &GuestMsrs,
                                         &pCpum->GuestFeatures);

    /*
     * Sanitize the cpuid information passed on to the guest.
     */
    if (RT_SUCCESS(rc))
    {
        rc = cpumR3CpuIdSanitize(pVM, pCpum, &Config);
        if (RT_SUCCESS(rc))
        {
            cpumR3CpuIdLimitLeaves(pCpum, &Config);
            cpumR3CpuIdLimitIntelFamModStep(pCpum, &Config);
        }
    }

    /*
     * Move the CPUID array over to the static VM structure allocation
     * and explode guest CPU features again.  We must do this *before*
     * reconciling MSRs with CPUIDs and applying any fudging (esp on ARM64).
     */
    if (RT_SUCCESS(rc))
    {
        void * const pvFree = pCpum->GuestInfo.paCpuIdLeavesR3;
        rc = cpumR3CpuIdInstallAndExplodeLeaves(pVM, pCpum, pCpum->GuestInfo.paCpuIdLeavesR3,
                                                pCpum->GuestInfo.cCpuIdLeaves, &GuestMsrs);
        AssertLogRelRC(rc);
        RTMemFree(pvFree);
        if (RT_SUCCESS(rc))
        {
            /*
             * Setup MSRs introduced in microcode updates or that are otherwise not in
             * the CPU profile, but are advertised in the CPUID info we just sanitized.
             */
            if (RT_SUCCESS(rc))
                rc = cpumR3MsrReconcileWithCpuId(pVM);
            /*
             * MSR fudging.
             */
            if (RT_SUCCESS(rc))
            {
                /** @cfgm{/CPUM/FudgeMSRs, boolean, true}
                 * Fudges some common MSRs if not present in the selected CPU database entry.
                 * This is for trying to keep VMs running when moved between different hosts
                 * and different CPU vendors. */
                bool fEnable;
                rc = CFGMR3QueryBoolDef(pCpumCfg, "FudgeMSRs", &fEnable, true); AssertRC(rc);
                if (RT_SUCCESS(rc) && fEnable)
                {
                    rc = cpumR3MsrApplyFudge(pVM);
                    AssertLogRelRC(rc);
                }
            }
            if (RT_SUCCESS(rc))
            {
                /*
                 * Move the MSR arrays over to the static VM structure allocation.
                 */
                AssertFatalMsg(pCpum->GuestInfo.cMsrRanges <= RT_ELEMENTS(pCpum->GuestInfo.aMsrRanges),
                               ("%u\n", pCpum->GuestInfo.cMsrRanges));
                memcpy(pCpum->GuestInfo.aMsrRanges, pCpum->GuestInfo.paMsrRangesR3,
                       sizeof(pCpum->GuestInfo.paMsrRangesR3[0]) * pCpum->GuestInfo.cMsrRanges);
                RTMemFree(pCpum->GuestInfo.paMsrRangesR3);
                pCpum->GuestInfo.paMsrRangesR3 = pCpum->GuestInfo.aMsrRanges;

                /*
                 * Some more configuration that we're applying at the end of everything
                 * via the CPUMR3SetGuestCpuIdFeature API.
                 */

                /* Check if 64-bit guest supported was enabled. */
                bool fEnable64bit;
                rc = CFGMR3QueryBoolDef(pCpumCfg, "Enable64bit", &fEnable64bit, false);
                AssertRCReturn(rc, rc);
                if (fEnable64bit)
                {
                    /* In case of a CPU upgrade: */
                    CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_SEP);
                    CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_SYSCALL);      /* (Long mode only on Intel CPUs.) */
                    CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_PAE);
                    CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_LAHF);
                    CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_NX);

                    /* The actual feature: */
                    CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_LONG_MODE);
                }

                /* Check if PAE was explicitely enabled by the user. */
                bool fEnable;
                rc = CFGMR3QueryBoolDef(CFGMR3GetRoot(pVM), "EnablePAE", &fEnable, fEnable64bit);
                AssertRCReturn(rc, rc);
                if (fEnable && !pVM->cpum.s.GuestFeatures.fPae)
                    CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_PAE);

                /* We don't normally enable NX for raw-mode, so give the user a chance to force it on. */
                rc = CFGMR3QueryBoolDef(pCpumCfg, "EnableNX", &fEnable, fEnable64bit);
                AssertRCReturn(rc, rc);
                if (fEnable && !pVM->cpum.s.GuestFeatures.fNoExecute)
                    CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_NX);

                /* Check if speculation control is enabled. */
                rc = CFGMR3QueryBoolDef(pCpumCfg, "SpecCtrl", &fEnable, false);
                AssertRCReturn(rc, rc);
                if (fEnable)
                    CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_SPEC_CTRL);
                else
                {
                    /*
                     * Set the "SSBD-not-needed" flag to work around a bug in some Linux kernels when the VIRT_SPEC_CTL
                     * feature is not exposed on AMD CPUs and there is only 1 vCPU configured.
                     * This was observed with kernel "4.15.0-29-generic #31~16.04.1-Ubuntu" but more versions are likely affected.
                     *
                     * The kernel doesn't initialize a lock and causes a NULL pointer exception later on when configuring SSBD:
                     *    EIP: _raw_spin_lock+0x14/0x30
                     *    EFLAGS: 00010046 CPU: 0
                     *    EAX: 00000000 EBX: 00000001 ECX: 00000004 EDX: 00000000
                     *    ESI: 00000000 EDI: 00000000 EBP: ee023f1c ESP: ee023f18
                     *    DS: 007b ES: 007b FS: 00d8 GS: 00e0 SS: 0068
                     *    CR0: 80050033 CR2: 00000004 CR3: 3671c180 CR4: 000006f0
                     *    Call Trace:
                     *     speculative_store_bypass_update+0x8e/0x180
                     *     ssb_prctl_set+0xc0/0xe0
                     *     arch_seccomp_spec_mitigate+0x1d/0x20
                     *     do_seccomp+0x3cb/0x610
                     *     SyS_seccomp+0x16/0x20
                     *     do_fast_syscall_32+0x7f/0x1d0
                     *     entry_SYSENTER_32+0x4e/0x7c
                     *
                     * The lock would've been initialized in process.c:speculative_store_bypass_ht_init() called from two places in smpboot.c.
                     * First when a secondary CPU is started and second in native_smp_prepare_cpus() which is not called in a single vCPU environment.
                     *
                     * As spectre control features are completely disabled anyway when we arrived here there is no harm done in informing the
                     * guest to not even try.
                     */
                    if (   pVM->cpum.s.GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_AMD
                        || pVM->cpum.s.GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_HYGON)
                    {
                        PCPUMCPUIDLEAF pLeaf = cpumR3CpuIdGetExactLeaf(&pVM->cpum.s, UINT32_C(0x80000008), 0);
                        if (pLeaf)
                        {
                            pLeaf->uEbx |= X86_CPUID_AMD_EFEID_EBX_NO_SSBD_REQUIRED;
                            LogRel(("CPUM: Set SSBD not required flag for AMD to work around some buggy Linux kernels!\n"));
                        }
                    }
                }

                /*
                 * MTRR support.
                 * We've always reported the MTRR feature bit in CPUID.
                 * Here we allow exposing MTRRs with reasonable default values (especially required
                 * by Windows 10 guests with Hyper-V enabled). The MTRR support isn't feature
                 * complete, see @bugref{10318} and bugref{10498}.
                 */
                if (pVM->cpum.s.GuestFeatures.fMtrr)
                {
                    /** @cfgm{/CPUM/MtrrWrite, boolean, true}
                     * Whether to enable MTRR read-write support. This overrides the MTRR read-only CFGM
                     * setting. */
                    bool fEnableMtrrReadWrite;
                    rc = CFGMR3QueryBoolDef(pCpumCfg, "MtrrReadWrite", &fEnableMtrrReadWrite, true);
                    AssertRCReturn(rc, rc);
                    if (fEnableMtrrReadWrite)
                    {
                        pVM->cpum.s.fMtrrRead  = true;
                        pVM->cpum.s.fMtrrWrite = true;
                        LogRel(("CPUM: Enabled MTRR read-write support\n"));
                    }
                    else
                    {
                        /** @cfgm{/CPUM/MtrrReadOnly, boolean, false}
                         * Whether to enable MTRR read-only support and to initialize mapping of guest
                         * memory via MTRRs. When disabled, MTRRs are left blank, returns 0 on reads and
                         * ignores writes. Some guests like GNU/Linux recognize a virtual system when MTRRs
                         * are left blank but some guests may expect their RAM to be mapped via MTRRs
                         * similar to real hardware. */
                        rc = CFGMR3QueryBoolDef(pCpumCfg, "MtrrReadOnly", &pVM->cpum.s.fMtrrRead, false);
                        AssertRCReturn(rc, rc);
                        LogRel(("CPUM: Enabled MTRR read-only support\n"));
                    }

                    /* Setup MTRR capability based on what the guest CPU profile (typically host) supports. */
                    Assert(!pVM->cpum.s.fMtrrWrite || pVM->cpum.s.fMtrrRead);
                    if (pVM->cpum.s.fMtrrRead)
                    {
                        /** @cfgm{/CPUM/MtrrVarCountIsVirtual, boolean, true}
                         * When enabled, the number of variable-range MTRRs are virtualized. When disabled,
                         * the number of variable-range MTRRs are derived from the CPU profile. Unless
                         * guests have problems with a virtualized number of variable-range MTRRs, it is
                         * recommended to keep this enabled so that there are sufficient MTRRs to fully
                         * describe all regions of the guest RAM. */
                        bool fMtrrVarCountIsVirt;
                        rc = CFGMR3QueryBoolDef(pCpumCfg, "MtrrVarCountIsVirtual", &fMtrrVarCountIsVirt, true);
                        AssertRCReturn(rc, rc);

                        rc = cpumR3InitMtrrCap(pVM, fMtrrVarCountIsVirt);
                        if (RT_SUCCESS(rc))
                        { /* likely */ }
                        else
                            return rc;
                    }
                }

                /*
                 * Finally, initialize guest VMX MSRs.
                 *
                 * This needs to be done -after- exploding guest features and sanitizing CPUID leaves
                 * as constructing VMX capabilities MSRs rely on CPU feature bits like long mode,
                 * unrestricted-guest execution, CR4 feature bits and possibly more in the future.
                 */
                /** @todo r=bird: given that long mode never used to be enabled before the
                 *        VMINITCOMPLETED_RING0 state, and we're a lot earlier here in ring-3
                 *        init, the above comment cannot be entirely accurate. */
                if (pVM->cpum.s.GuestFeatures.fVmx)
                {
                    Assert(Config.fNestedHWVirt);
                    cpumR3InitVmxGuestFeaturesAndMsrs(pVM, pCpumCfg, &pHostMsrs->hwvirt.vmx, &GuestMsrs.hwvirt.vmx);

                    /* Copy MSRs to all VCPUs */
                    PCVMXMSRS pVmxMsrs = &GuestMsrs.hwvirt.vmx;
                    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
                    {
                        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
                        memcpy(&pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs, pVmxMsrs, sizeof(*pVmxMsrs));
                    }
                }

                return VINF_SUCCESS;
            }

            /*
             * Failed before/while switching to internal VM structure storage.
             */
            RTMemFree(pCpum->GuestInfo.paCpuIdLeavesR3);
            pCpum->GuestInfo.paCpuIdLeavesR3 = NULL;
        }
    }
    RTMemFree(pCpum->GuestInfo.paMsrRangesR3);
    pCpum->GuestInfo.paMsrRangesR3 = NULL;
    return rc;
}


/**
 * Sets a CPUID feature bit during VM initialization.
 *
 * Since the CPUID feature bits are generally related to CPU features, other
 * CPUM configuration like MSRs can also be modified by calls to this API.
 *
 * @param   pVM             The cross context VM structure.
 * @param   enmFeature      The feature to set.
 */
VMMR3_INT_DECL(void) CPUMR3SetGuestCpuIdFeature(PVM pVM, CPUMCPUIDFEATURE enmFeature)
{
    PCPUMCPUIDLEAF pLeaf;
    PCPUMMSRRANGE  pMsrRange;

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
# define CHECK_X86_HOST_FEATURE_RET(a_fFeature, a_szFeature) \
    if (!pVM->cpum.s.HostFeatures. a_fFeature) \
    { \
        LogRel(("CPUM: WARNING! Can't turn on " a_szFeature " when the host doesn't support it!\n")); \
        return; \
    } else do { } while (0)
#else
# define CHECK_X86_HOST_FEATURE_RET(a_fFeature, a_szFeature) do { } while (0)
#endif

#define GET_8000_0001_CHECK_X86_HOST_FEATURE_RET(a_fFeature, a_szFeature) \
    do \
    { \
        pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x80000001)); \
        if (!pLeaf) \
        { \
            LogRel(("CPUM: WARNING! Can't turn on " a_szFeature " when no 0x80000001 CPUID leaf!\n")); \
            return; \
        } \
        CHECK_X86_HOST_FEATURE_RET(a_fFeature,a_szFeature); \
    } while (0)

    switch (enmFeature)
    {
        /*
         * Set the APIC bit in both feature masks.
         */
        case CPUMCPUIDFEATURE_APIC:
            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x00000001));
            if (pLeaf && (pLeaf->fFlags & CPUMCPUIDLEAF_F_CONTAINS_APIC))
                pVM->cpum.s.aGuestCpuIdPatmStd[1].uEdx = pLeaf->uEdx |= X86_CPUID_FEATURE_EDX_APIC;

            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x80000001));
            if (pLeaf && (pLeaf->fFlags & CPUMCPUIDLEAF_F_CONTAINS_APIC))
                pVM->cpum.s.aGuestCpuIdPatmExt[1].uEdx = pLeaf->uEdx |= X86_CPUID_AMD_FEATURE_EDX_APIC;

            pVM->cpum.s.GuestFeatures.fApic = 1;

            /* Make sure we've got the APICBASE MSR present. */
            pMsrRange = cpumLookupMsrRange(pVM, MSR_IA32_APICBASE);
            if (!pMsrRange)
            {
                static CPUMMSRRANGE const s_ApicBase =
                {
                    /*.uFirst =*/ MSR_IA32_APICBASE, /*.uLast =*/ MSR_IA32_APICBASE,
                    /*.enmRdFn =*/ kCpumMsrRdFn_Ia32ApicBase, /*.enmWrFn =*/ kCpumMsrWrFn_Ia32ApicBase,
                    /*.offCpumCpu =*/ UINT16_MAX, /*.fReserved =*/ 0, /*.uValue =*/ 0, /*.fWrIgnMask =*/ 0, /*.fWrGpMask =*/ 0,
                    /*.szName = */ "IA32_APIC_BASE"
                };
                int rc = CPUMR3MsrRangesInsert(pVM, &s_ApicBase);
                AssertLogRelRC(rc);
            }

            LogRel(("CPUM: SetGuestCpuIdFeature: Enabled xAPIC\n"));
            break;

        /*
         * Set the x2APIC bit in the standard feature mask.
         * Note! ASSUMES CPUMCPUIDFEATURE_APIC is called first.
         */
        case CPUMCPUIDFEATURE_X2APIC:
            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x00000001));
            if (pLeaf)
                pVM->cpum.s.aGuestCpuIdPatmStd[1].uEcx = pLeaf->uEcx |= X86_CPUID_FEATURE_ECX_X2APIC;
            pVM->cpum.s.GuestFeatures.fX2Apic = 1;

            /* Make sure the MSR doesn't GP or ignore the EXTD bit. */
            pMsrRange = cpumLookupMsrRange(pVM, MSR_IA32_APICBASE);
            if (pMsrRange)
            {
                pMsrRange->fWrGpMask  &= ~MSR_IA32_APICBASE_EXTD;
                pMsrRange->fWrIgnMask &= ~MSR_IA32_APICBASE_EXTD;
            }

            LogRel(("CPUM: SetGuestCpuIdFeature: Enabled x2APIC\n"));
            break;

        /*
         * Set the sysenter/sysexit bit in the standard feature mask.
         * Assumes the caller knows what it's doing! (host must support these)
         */
        case CPUMCPUIDFEATURE_SEP:
            CHECK_X86_HOST_FEATURE_RET(fSysEnter, "SEP");
            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x00000001));
            if (pLeaf)
                pVM->cpum.s.aGuestCpuIdPatmStd[1].uEdx = pLeaf->uEdx |= X86_CPUID_FEATURE_EDX_SEP;
            pVM->cpum.s.GuestFeatures.fSysEnter = 1;
            LogRel(("CPUM: SetGuestCpuIdFeature: Enabled SYSENTER/EXIT\n"));
            break;

        /*
         * Set the syscall/sysret bit in the extended feature mask.
         * Assumes the caller knows what it's doing! (host must support these)
         */
        case CPUMCPUIDFEATURE_SYSCALL:
            GET_8000_0001_CHECK_X86_HOST_FEATURE_RET(fSysCall, "SYSCALL/SYSRET");

            /* Valid for both Intel and AMD CPUs, although only in 64 bits mode for Intel. */
            pVM->cpum.s.aGuestCpuIdPatmExt[1].uEdx = pLeaf->uEdx |= X86_CPUID_EXT_FEATURE_EDX_SYSCALL;
            pVM->cpum.s.GuestFeatures.fSysCall = 1;
            LogRel(("CPUM: SetGuestCpuIdFeature: Enabled SYSCALL/RET\n"));
            break;

        /*
         * Set the PAE bit in both feature masks.
         * Assumes the caller knows what it's doing! (host must support these)
         */
        case CPUMCPUIDFEATURE_PAE:
            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x00000001));
            if (pLeaf)
                pVM->cpum.s.aGuestCpuIdPatmStd[1].uEdx = pLeaf->uEdx |= X86_CPUID_FEATURE_EDX_PAE;

            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x80000001));
            if (    pLeaf
                &&  (   pVM->cpum.s.GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_AMD
                     || pVM->cpum.s.GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_HYGON))
                pVM->cpum.s.aGuestCpuIdPatmExt[1].uEdx = pLeaf->uEdx |= X86_CPUID_AMD_FEATURE_EDX_PAE;

            pVM->cpum.s.GuestFeatures.fPae = 1;
            LogRel(("CPUM: SetGuestCpuIdFeature: Enabled PAE\n"));
            break;

        /*
         * Set the LONG MODE bit in the extended feature mask.
         * Assumes the caller knows what it's doing! (host must support these)
         */
        case CPUMCPUIDFEATURE_LONG_MODE:
            GET_8000_0001_CHECK_X86_HOST_FEATURE_RET(fLongMode, "LONG MODE");

            /* Valid for both Intel and AMD. */
            pVM->cpum.s.aGuestCpuIdPatmExt[1].uEdx = pLeaf->uEdx |= X86_CPUID_EXT_FEATURE_EDX_LONG_MODE;
            pVM->cpum.s.GuestFeatures.fLongMode = 1;
            pVM->cpum.s.GuestFeatures.cVmxMaxPhysAddrWidth = pVM->cpum.s.GuestFeatures.cMaxPhysAddrWidth;
            if (pVM->cpum.s.GuestFeatures.fVmx)
                for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
                {
                    PVMCPU pVCpu = pVM->apCpusR3[idCpu];
                    pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.u64Basic &= ~VMX_BASIC_PHYSADDR_WIDTH_32BIT;
                }
            LogRel(("CPUM: SetGuestCpuIdFeature: Enabled LONG MODE\n"));
            break;

        /*
         * Set the NX/XD bit in the extended feature mask.
         * Assumes the caller knows what it's doing! (host must support these)
         */
        case CPUMCPUIDFEATURE_NX:
            GET_8000_0001_CHECK_X86_HOST_FEATURE_RET(fNoExecute, "NX/XD");

            /* Valid for both Intel and AMD. */
            pVM->cpum.s.aGuestCpuIdPatmExt[1].uEdx = pLeaf->uEdx |= X86_CPUID_EXT_FEATURE_EDX_NX;
            pVM->cpum.s.GuestFeatures.fNoExecute = 1;
            LogRel(("CPUM: SetGuestCpuIdFeature: Enabled NX\n"));
            break;


        /*
         * Set the LAHF/SAHF support in 64-bit mode.
         * Assumes the caller knows what it's doing! (host must support this)
         */
        case CPUMCPUIDFEATURE_LAHF:
            GET_8000_0001_CHECK_X86_HOST_FEATURE_RET(fLahfSahf, "LAHF/SAHF");

            /* Valid for both Intel and AMD. */
            pVM->cpum.s.aGuestCpuIdPatmExt[1].uEcx = pLeaf->uEcx |= X86_CPUID_EXT_FEATURE_ECX_LAHF_SAHF;
            pVM->cpum.s.GuestFeatures.fLahfSahf = 1;
            LogRel(("CPUM: SetGuestCpuIdFeature: Enabled LAHF/SAHF\n"));
            break;

        /*
         * Set the RDTSCP support bit.
         * Assumes the caller knows what it's doing! (host must support this)
         */
        case CPUMCPUIDFEATURE_RDTSCP:
            if (pVM->cpum.s.u8PortableCpuIdLevel > 0)
                return;
            GET_8000_0001_CHECK_X86_HOST_FEATURE_RET(fRdTscP, "RDTSCP");
            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x80000001));

            /* Valid for both Intel and AMD. */
            pVM->cpum.s.aGuestCpuIdPatmExt[1].uEdx = pLeaf->uEdx |= X86_CPUID_EXT_FEATURE_EDX_RDTSCP;
            pVM->cpum.s.HostFeatures.fRdTscP = 1;
            LogRel(("CPUM: SetGuestCpuIdFeature: Enabled RDTSCP.\n"));
            break;

       /*
        * Set the Hypervisor Present bit in the standard feature mask.
        */
        case CPUMCPUIDFEATURE_HVP:
            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x00000001));
            if (pLeaf)
                pVM->cpum.s.aGuestCpuIdPatmStd[1].uEcx = pLeaf->uEcx |= X86_CPUID_FEATURE_ECX_HVP;
            pVM->cpum.s.GuestFeatures.fHypervisorPresent = 1;
            LogRel(("CPUM: SetGuestCpuIdFeature: Enabled Hypervisor Present bit\n"));
            break;

        /*
         * Set up the speculation control CPUID bits and MSRs. This is quite complicated
         * on Intel CPUs, and different on AMDs.
         */
        case CPUMCPUIDFEATURE_SPEC_CTRL:
            if (pVM->cpum.s.GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_INTEL)
            {
                pLeaf = cpumR3CpuIdGetExactLeaf(&pVM->cpum.s, UINT32_C(0x00000007), 0);
#ifdef RT_ARCH_AMD64
                if (   !pLeaf
                    || !(pVM->cpum.s.HostFeatures.fIbpb || pVM->cpum.s.HostFeatures.fIbrs))
                {
                    LogRel(("CPUM: WARNING! Can't turn on Speculation Control when the host doesn't support it!\n"));
                    return;
                }
#else
                if (!pLeaf)
                {
                    LogRel(("CPUM: WARNING! Can't turn on Speculation Control without leaf 0x00000007!\n"));
                    return;
                }
#endif

                /* The feature can be enabled. Let's see what we can actually do. */
                pVM->cpum.s.GuestFeatures.fSpeculationControl = 1;

#ifdef RT_ARCH_AMD64
                /* We will only expose STIBP if IBRS is present to keep things simpler (simple is not an option). */
                if (pVM->cpum.s.HostFeatures.fIbrs)
#endif
                {
/** @todo make this more configurable? */
                    pLeaf->uEdx |= X86_CPUID_STEXT_FEATURE_EDX_IBRS_IBPB;
                    pVM->cpum.s.GuestFeatures.fIbrs = 1;
#ifdef RT_ARCH_AMD64
                    if (pVM->cpum.s.HostFeatures.fStibp)
#endif
                    {
                        pLeaf->uEdx |= X86_CPUID_STEXT_FEATURE_EDX_STIBP;
                        pVM->cpum.s.GuestFeatures.fStibp = 1;
                    }

#ifdef RT_ARCH_AMD64
                    if (pVM->cpum.s.HostFeatures.fSsbd)
#endif
                    {
                        pLeaf->uEdx |= X86_CPUID_STEXT_FEATURE_EDX_SSBD;
                        pVM->cpum.s.GuestFeatures.fSsbd = 1;
                    }

                    PCPUMCPUIDLEAF const pSubLeaf2 = cpumR3CpuIdGetExactLeaf(&pVM->cpum.s, UINT32_C(0x00000007), 2);
                    if (pSubLeaf2)
                    {
#ifdef RT_ARCH_AMD64
                        if (pVM->cpum.s.HostFeatures.fPsfd)
#endif
                        {
                            pSubLeaf2->uEdx |= X86_CPUID_STEXT_FEATURE_2_EDX_PSFD;
                            pVM->cpum.s.GuestFeatures.fPsfd = 1;
                        }

#ifdef RT_ARCH_AMD64
                        if (pVM->cpum.s.HostFeatures.fIpredCtrl)
#endif
                        {
                            pSubLeaf2->uEdx |= X86_CPUID_STEXT_FEATURE_2_EDX_IPRED_CTRL;
                            pVM->cpum.s.GuestFeatures.fIpredCtrl = 1;
                        }

#ifdef RT_ARCH_AMD64
                        if (pVM->cpum.s.HostFeatures.fRrsbaCtrl)
#endif
                        {
                            pSubLeaf2->uEdx |= X86_CPUID_STEXT_FEATURE_2_EDX_RRSBA_CTRL;
                            pVM->cpum.s.GuestFeatures.fRrsbaCtrl = 1;
                        }

#ifdef RT_ARCH_AMD64
                        if (pVM->cpum.s.HostFeatures.fDdpdU)
#endif
                        {
                            pSubLeaf2->uEdx |= X86_CPUID_STEXT_FEATURE_2_EDX_DDPD_U;
                            pVM->cpum.s.GuestFeatures.fDdpdU = 1;
                        }

#ifdef RT_ARCH_AMD64
                        if (pVM->cpum.s.HostFeatures.fBhiCtrl)
#endif
                        {
                            pSubLeaf2->uEdx |= X86_CPUID_STEXT_FEATURE_2_EDX_BHI_CTRL;
                            pVM->cpum.s.GuestFeatures.fBhiCtrl = 1;
                        }
                    }

                    /* Make sure we have the speculation control MSR... */
                    pMsrRange = cpumLookupMsrRange(pVM, MSR_IA32_SPEC_CTRL);
                    if (!pMsrRange)
                    {
                        static CPUMMSRRANGE const s_SpecCtrl =
                        {
                            /*.uFirst =*/ MSR_IA32_SPEC_CTRL, /*.uLast =*/ MSR_IA32_SPEC_CTRL,
                            /*.enmRdFn =*/ kCpumMsrRdFn_Ia32SpecCtrl, /*.enmWrFn =*/ kCpumMsrWrFn_Ia32SpecCtrl,
                            /*.offCpumCpu =*/ UINT16_MAX, /*.fReserved =*/ 0, /*.uValue =*/ 0, /*.fWrIgnMask =*/ 0, /*.fWrGpMask =*/ 0,
                            /*.szName = */ "IA32_SPEC_CTRL"
                        };
                        int rc = CPUMR3MsrRangesInsert(pVM, &s_SpecCtrl);
                        AssertLogRelRC(rc);
                    }

                    /* ... and the predictor command MSR. */
                    pMsrRange = cpumLookupMsrRange(pVM, MSR_IA32_PRED_CMD);
                    if (!pMsrRange)
                    {
                        /** @todo incorrect fWrGpMask. */
                        static CPUMMSRRANGE const s_SpecCtrl =
                        {
                            /*.uFirst =*/ MSR_IA32_PRED_CMD, /*.uLast =*/ MSR_IA32_PRED_CMD,
                            /*.enmRdFn =*/ kCpumMsrRdFn_WriteOnly, /*.enmWrFn =*/ kCpumMsrWrFn_Ia32PredCmd,
                            /*.offCpumCpu =*/ UINT16_MAX, /*.fReserved =*/ 0, /*.uValue =*/ 0, /*.fWrIgnMask =*/ 0, /*.fWrGpMask =*/ 0,
                            /*.szName = */ "IA32_PRED_CMD"
                        };
                        int rc = CPUMR3MsrRangesInsert(pVM, &s_SpecCtrl);
                        AssertLogRelRC(rc);
                    }

                }

#ifdef RT_ARCH_AMD64
                if (pVM->cpum.s.HostFeatures.fArchCap)
#endif
                {
                    /* Install the architectural capabilities MSR. */
                    pMsrRange = cpumLookupMsrRange(pVM, MSR_IA32_ARCH_CAPABILITIES);
                    if (!pMsrRange)
                    {
                        static CPUMMSRRANGE const s_ArchCaps =
                        {
                            /*.uFirst =*/ MSR_IA32_ARCH_CAPABILITIES, /*.uLast =*/ MSR_IA32_ARCH_CAPABILITIES,
                            /*.enmRdFn =*/ kCpumMsrRdFn_Ia32ArchCapabilities, /*.enmWrFn =*/ kCpumMsrWrFn_ReadOnly,
                            /*.offCpumCpu =*/ UINT16_MAX, /*.fReserved =*/ 0, /*.uValue =*/ 0, /*.fWrIgnMask =*/ 0, /*.fWrGpMask =*/ UINT64_MAX,
                            /*.szName = */ "IA32_ARCH_CAPABILITIES"
                        };
                        int rc = CPUMR3MsrRangesInsert(pVM, &s_ArchCaps);
                        AssertLogRelRC(rc);
                    }

                    /* Advertise IBRS_ALL if present at this point... */
                    if (pVM->cpum.s.HostFeatures.fArchCap & MSR_IA32_ARCH_CAP_F_IBRS_ALL)
                        VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->cpum.s.GuestMsrs.msr.ArchCaps |= MSR_IA32_ARCH_CAP_F_IBRS_ALL);
                }

                LogRel(("CPUM: SetGuestCpuIdFeature: Enabled Speculation Control.\n"));
            }
            else if (   pVM->cpum.s.GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_AMD
                     || pVM->cpum.s.GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_HYGON)
            {
                /* The precise details of AMD's implementation are not yet clear. */
            }
            break;

        default:
            AssertMsgFailed(("enmFeature=%d\n", enmFeature));
            break;
    }

    /** @todo can probably kill this as this API is now init time only... */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        pVCpu->cpum.s.fChanged |= CPUM_CHANGED_CPUID;
    }

#undef GET_8000_0001_CHECK_X86_HOST_FEATURE_RET
#undef CHECK_X86_HOST_FEATURE_RET
}


/**
 * Queries a CPUID feature bit.
 *
 * @returns boolean for feature presence
 * @param   pVM             The cross context VM structure.
 * @param   enmFeature      The feature to query.
 * @deprecated Use the cpum.ro.GuestFeatures directly instead.
 */
VMMR3_INT_DECL(bool) CPUMR3GetGuestCpuIdFeature(PVM pVM, CPUMCPUIDFEATURE enmFeature)
{
    switch (enmFeature)
    {
        case CPUMCPUIDFEATURE_APIC:         return pVM->cpum.s.GuestFeatures.fApic;
        case CPUMCPUIDFEATURE_X2APIC:       return pVM->cpum.s.GuestFeatures.fX2Apic;
        case CPUMCPUIDFEATURE_SYSCALL:      return pVM->cpum.s.GuestFeatures.fSysCall;
        case CPUMCPUIDFEATURE_SEP:          return pVM->cpum.s.GuestFeatures.fSysEnter;
        case CPUMCPUIDFEATURE_PAE:          return pVM->cpum.s.GuestFeatures.fPae;
        case CPUMCPUIDFEATURE_NX:           return pVM->cpum.s.GuestFeatures.fNoExecute;
        case CPUMCPUIDFEATURE_LAHF:         return pVM->cpum.s.GuestFeatures.fLahfSahf;
        case CPUMCPUIDFEATURE_LONG_MODE:    return pVM->cpum.s.GuestFeatures.fLongMode;
        case CPUMCPUIDFEATURE_RDTSCP:       return pVM->cpum.s.GuestFeatures.fRdTscP;
        case CPUMCPUIDFEATURE_HVP:          return pVM->cpum.s.GuestFeatures.fHypervisorPresent;
        case CPUMCPUIDFEATURE_SPEC_CTRL:    return pVM->cpum.s.GuestFeatures.fSpeculationControl;
        case CPUMCPUIDFEATURE_INVALID:
        case CPUMCPUIDFEATURE_32BIT_HACK:
            break;
    }
    AssertFailed();
    return false;
}


/**
 * Clears a CPUID feature bit.
 *
 * @param   pVM             The cross context VM structure.
 * @param   enmFeature      The feature to clear.
 *
 * @deprecated Probably better to default the feature to disabled and only allow
 *             setting (enabling) it during construction.
 */
VMMR3_INT_DECL(void) CPUMR3ClearGuestCpuIdFeature(PVM pVM, CPUMCPUIDFEATURE enmFeature)
{
    PCPUMCPUIDLEAF pLeaf;
    switch (enmFeature)
    {
        case CPUMCPUIDFEATURE_APIC:
            Assert(!pVM->cpum.s.GuestFeatures.fApic); /* We only expect this call during init. No MSR adjusting needed. */
            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x00000001));
            if (pLeaf)
                pVM->cpum.s.aGuestCpuIdPatmStd[1].uEdx = pLeaf->uEdx &= ~X86_CPUID_FEATURE_EDX_APIC;

            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x80000001));
            if (pLeaf && (pLeaf->fFlags & CPUMCPUIDLEAF_F_CONTAINS_APIC))
                pVM->cpum.s.aGuestCpuIdPatmExt[1].uEdx = pLeaf->uEdx &= ~X86_CPUID_AMD_FEATURE_EDX_APIC;

            pVM->cpum.s.GuestFeatures.fApic = 0;
            Log(("CPUM: ClearGuestCpuIdFeature: Disabled xAPIC\n"));
            break;

        case CPUMCPUIDFEATURE_X2APIC:
            Assert(!pVM->cpum.s.GuestFeatures.fX2Apic); /* We only expect this call during init. No MSR adjusting needed. */
            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x00000001));
            if (pLeaf)
                pVM->cpum.s.aGuestCpuIdPatmStd[1].uEcx = pLeaf->uEcx &= ~X86_CPUID_FEATURE_ECX_X2APIC;
            pVM->cpum.s.GuestFeatures.fX2Apic = 0;
            Log(("CPUM: ClearGuestCpuIdFeature: Disabled x2APIC\n"));
            break;

#if 0
        case CPUMCPUIDFEATURE_PAE:
            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x00000001));
            if (pLeaf)
                pVM->cpum.s.aGuestCpuIdPatmStd[1].uEdx = pLeaf->uEdx &= ~X86_CPUID_FEATURE_EDX_PAE;

            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x80000001));
            if (   pLeaf
                && (   pVM->cpum.s.GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_AMD
                    || pVM->cpum.s.GuestFeatures.enmCpuVendor == CPUMCPUVENDOR_HYGON))
                pVM->cpum.s.aGuestCpuIdPatmExt[1].uEdx = pLeaf->uEdx &= ~X86_CPUID_AMD_FEATURE_EDX_PAE;

            pVM->cpum.s.GuestFeatures.fPae = 0;
            Log(("CPUM: ClearGuestCpuIdFeature: Disabled PAE!\n"));
            break;

        case CPUMCPUIDFEATURE_LONG_MODE:
            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x80000001));
            if (pLeaf)
                pVM->cpum.s.aGuestCpuIdPatmExt[1].uEdx = pLeaf->uEdx &= ~X86_CPUID_EXT_FEATURE_EDX_LONG_MODE;
            pVM->cpum.s.GuestFeatures.fLongMode = 0;
            pVM->cpum.s.GuestFeatures.cVmxMaxPhysAddrWidth = 32;
            if (pVM->cpum.s.GuestFeatures.fVmx)
                for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
                {
                    PVMCPU pVCpu = pVM->apCpusR3[idCpu];
                    pVCpu->cpum.s.Guest.hwvirt.vmx.Msrs.u64Basic |= VMX_BASIC_PHYSADDR_WIDTH_32BIT;
                }
            break;

        case CPUMCPUIDFEATURE_LAHF:
            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x80000001));
            if (pLeaf)
                pVM->cpum.s.aGuestCpuIdPatmExt[1].uEcx = pLeaf->uEcx &= ~X86_CPUID_EXT_FEATURE_ECX_LAHF_SAHF;
            pVM->cpum.s.GuestFeatures.fLahfSahf = 0;
            break;
#endif
        case CPUMCPUIDFEATURE_RDTSCP:
            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x80000001));
            if (pLeaf)
                pVM->cpum.s.aGuestCpuIdPatmExt[1].uEdx = pLeaf->uEdx &= ~X86_CPUID_EXT_FEATURE_EDX_RDTSCP;
            pVM->cpum.s.GuestFeatures.fRdTscP = 0;
            Log(("CPUM: ClearGuestCpuIdFeature: Disabled RDTSCP!\n"));
            break;

#if 0
        case CPUMCPUIDFEATURE_HVP:
            pLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x00000001));
            if (pLeaf)
                pVM->cpum.s.aGuestCpuIdPatmStd[1].uEcx = pLeaf->uEcx &= ~X86_CPUID_FEATURE_ECX_HVP;
            pVM->cpum.s.GuestFeatures.fHypervisorPresent = 0;
            break;

        case CPUMCPUIDFEATURE_SPEC_CTRL:
            pLeaf = cpumR3CpuIdGetExactLeaf(&pVM->cpum.s, UINT32_C(0x00000007), 0);
            if (pLeaf)
                pLeaf->uEdx &= ~(X86_CPUID_STEXT_FEATURE_EDX_IBRS_IBPB | X86_CPUID_STEXT_FEATURE_EDX_STIBP);
            VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->cpum.s.GuestMsrs.msr.ArchCaps &= ~MSR_IA32_ARCH_CAP_F_IBRS_ALL);
            Log(("CPUM: ClearGuestCpuIdFeature: Disabled speculation control!\n"));
            break;
#endif
        default:
            AssertMsgFailed(("enmFeature=%d\n", enmFeature));
            break;
    }

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        pVCpu->cpum.s.fChanged |= CPUM_CHANGED_CPUID;
    }
}


/**
 * Do some final polishing after all calls to CPUMR3SetGuestCpuIdFeature and
 * CPUMR3ClearGuestCpuIdFeature are (probably) done.
 *
 * @param   pVM             The cross context VM structure.
 */
void cpumR3CpuIdRing3InitDone(PVM pVM)
{
    /*
     * Do not advertise NX w/o PAE, seems to confuse windows 7 (black screen very
     * early in real mode).
     */
    PCPUMCPUIDLEAF pStdLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x00000001));
    PCPUMCPUIDLEAF pExtLeaf = cpumCpuIdGetLeaf(pVM, UINT32_C(0x80000001));
    if (pStdLeaf && pExtLeaf)
    {
        if (   !(pStdLeaf->uEdx & X86_CPUID_FEATURE_EDX_PAE)
            && (pExtLeaf->uEdx & X86_CPUID_EXT_FEATURE_EDX_NX))
            pExtLeaf->uEdx &= ~X86_CPUID_EXT_FEATURE_EDX_NX;
    }
}


/*
 *
 *
 * Saved state related code.
 * Saved state related code.
 * Saved state related code.
 *
 *
 */

/**
 * Called both in pass 0 and the final pass.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 */
void cpumR3SaveCpuId(PVM pVM, PSSMHANDLE pSSM)
{
    /*
     * Save all the CPU ID leaves.
     */
    SSMR3PutU32(pSSM, sizeof(pVM->cpum.s.GuestInfo.paCpuIdLeavesR3[0]));
    SSMR3PutU32(pSSM, pVM->cpum.s.GuestInfo.cCpuIdLeaves);
    SSMR3PutMem(pSSM, pVM->cpum.s.GuestInfo.paCpuIdLeavesR3,
                sizeof(pVM->cpum.s.GuestInfo.paCpuIdLeavesR3[0]) * pVM->cpum.s.GuestInfo.cCpuIdLeaves);

    SSMR3PutMem(pSSM, &pVM->cpum.s.GuestInfo.DefCpuId, sizeof(pVM->cpum.s.GuestInfo.DefCpuId));

    /*
     * Save a good portion of the raw CPU IDs as well as they may come in
     * handy when validating features for raw mode.
     */
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    CPUMCPUID   aRawStd[16];
    for (unsigned i = 0; i < RT_ELEMENTS(aRawStd); i++)
        ASMCpuIdExSlow(i, 0, 0, 0, &aRawStd[i].uEax, &aRawStd[i].uEbx, &aRawStd[i].uEcx, &aRawStd[i].uEdx);
    SSMR3PutU32(pSSM, RT_ELEMENTS(aRawStd));
    SSMR3PutMem(pSSM, &aRawStd[0], sizeof(aRawStd));

    CPUMCPUID   aRawExt[32];
    for (unsigned i = 0; i < RT_ELEMENTS(aRawExt); i++)
        ASMCpuIdExSlow(i | UINT32_C(0x80000000), 0, 0, 0, &aRawExt[i].uEax, &aRawExt[i].uEbx, &aRawExt[i].uEcx, &aRawExt[i].uEdx);
    SSMR3PutU32(pSSM, RT_ELEMENTS(aRawExt));
    SSMR3PutMem(pSSM, &aRawExt[0], sizeof(aRawExt));

#else
    /* Two zero counts on non-x86 hosts. */
    SSMR3PutU32(pSSM, 0);
    SSMR3PutU32(pSSM, 0);
#endif
}


static int cpumR3LoadOneOldGuestCpuIdArray(PSSMHANDLE pSSM, uint32_t uBase, PCPUMCPUIDLEAF *ppaLeaves, uint32_t *pcLeaves)
{
    uint32_t cCpuIds;
    int rc = SSMR3GetU32(pSSM, &cCpuIds);
    if (RT_SUCCESS(rc))
    {
        if (cCpuIds < 64)
        {
            for (uint32_t i = 0; i < cCpuIds; i++)
            {
                CPUMCPUID CpuId;
                rc = SSMR3GetMem(pSSM, &CpuId, sizeof(CpuId));
                if (RT_FAILURE(rc))
                    break;

                CPUMCPUIDLEAF NewLeaf;
                NewLeaf.uLeaf           = uBase + i;
                NewLeaf.uSubLeaf        = 0;
                NewLeaf.fSubLeafMask    = 0;
                NewLeaf.uEax            = CpuId.uEax;
                NewLeaf.uEbx            = CpuId.uEbx;
                NewLeaf.uEcx            = CpuId.uEcx;
                NewLeaf.uEdx            = CpuId.uEdx;
                NewLeaf.fFlags          = 0;
                rc = cpumR3CpuIdInsert(NULL /* pVM */, ppaLeaves, pcLeaves, &NewLeaf);
            }
        }
        else
            rc = VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    }
    if (RT_FAILURE(rc))
    {
        RTMemFree(*ppaLeaves);
        *ppaLeaves = NULL;
        *pcLeaves = 0;
    }
    return rc;
}


static int cpumR3LoadGuestCpuIdArray(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, PCPUMCPUIDLEAF *ppaLeaves, uint32_t *pcLeaves)
{
    *ppaLeaves = NULL;
    *pcLeaves = 0;

    int rc;
    if (uVersion > CPUM_SAVED_STATE_VERSION_PUT_STRUCT)
    {
        /*
         * The new format. Starts by declaring the leave size and count.
         */
        uint32_t cbLeaf;
        SSMR3GetU32(pSSM, &cbLeaf);
        uint32_t cLeaves;
        rc = SSMR3GetU32(pSSM, &cLeaves);
        if (RT_SUCCESS(rc))
        {
            if (cbLeaf == sizeof(**ppaLeaves))
            {
                if (cLeaves <= CPUM_CPUID_MAX_LEAVES)
                {
                    /*
                     * Load the leaves one by one.
                     *
                     * The uPrev stuff is a kludge for working around a week worth of bad saved
                     * states during the CPUID revamp in March 2015.  We saved too many leaves
                     * due to a bug in cpumR3CpuIdInstallAndExplodeLeaves, thus ending up with
                     * garbage entires at the end of the array when restoring.  We also had
                     * a subleaf insertion bug that triggered with the leaf 4 stuff below,
                     * this kludge doesn't deal correctly with that, but who cares...
                     */
                    uint32_t uPrev = 0;
                    for (uint32_t i = 0; i < cLeaves && RT_SUCCESS(rc); i++)
                    {
                        CPUMCPUIDLEAF Leaf;
                        rc = SSMR3GetMem(pSSM, &Leaf, sizeof(Leaf));
                        if (RT_SUCCESS(rc))
                        {
                            if (   uVersion != CPUM_SAVED_STATE_VERSION_BAD_CPUID_COUNT
                                || Leaf.uLeaf >= uPrev)
                            {
                                rc = cpumR3CpuIdInsert(NULL /* pVM */, ppaLeaves, pcLeaves, &Leaf);
                                uPrev = Leaf.uLeaf;
                            }
                            else
                                uPrev = UINT32_MAX;
                        }
                    }
                }
                else
                    rc = SSMR3SetLoadError(pSSM, VERR_TOO_MANY_CPUID_LEAVES, RT_SRC_POS,
                                           "Too many CPUID leaves: %#x, max %#x", cLeaves, CPUM_CPUID_MAX_LEAVES);
            }
            else
                rc = SSMR3SetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                       "CPUMCPUIDLEAF size differs: saved=%#x, our=%#x", cbLeaf, sizeof(**ppaLeaves));
        }
    }
    else
    {
        /*
         * The old format with its three inflexible arrays.
         */
        rc = cpumR3LoadOneOldGuestCpuIdArray(pSSM, UINT32_C(0x00000000), ppaLeaves, pcLeaves);
        if (RT_SUCCESS(rc))
            rc = cpumR3LoadOneOldGuestCpuIdArray(pSSM, UINT32_C(0x80000000), ppaLeaves, pcLeaves);
        if (RT_SUCCESS(rc))
            rc = cpumR3LoadOneOldGuestCpuIdArray(pSSM, UINT32_C(0xc0000000), ppaLeaves, pcLeaves);
        if (RT_SUCCESS(rc))
        {
            /*
             * Fake up leaf 4 on intel like we used to do in CPUMGetGuestCpuId earlier.
             */
            PCPUMCPUIDLEAF pLeaf = cpumCpuIdGetLeafInt(*ppaLeaves, *pcLeaves, 0, 0);
            if (   pLeaf
                && RTX86IsIntelCpu(pLeaf->uEbx, pLeaf->uEcx, pLeaf->uEdx))
            {
                CPUMCPUIDLEAF Leaf;
                Leaf.uLeaf        = 4;
                Leaf.fSubLeafMask = UINT32_MAX;
                Leaf.uSubLeaf     = 0;
                Leaf.uEdx = UINT32_C(0);          /* 3 flags, 0 is fine. */
                Leaf.uEcx = UINT32_C(63);         /* sets - 1 */
                Leaf.uEbx = (UINT32_C(7) << 22)   /* associativity -1 */
                          | (UINT32_C(0) << 12)   /* phys line partitions - 1 */
                          | UINT32_C(63);         /* system coherency line size - 1 */
                Leaf.uEax = (RT_MIN(pVM->cCpus - 1, UINT32_C(0x3f)) << 26)  /* cores per package - 1 */
                          | (UINT32_C(0) << 14)   /* threads per cache - 1 */
                          | (UINT32_C(1) << 5)    /* cache level */
                          | UINT32_C(1);          /* cache type (data) */
                Leaf.fFlags       = 0;
                rc = cpumR3CpuIdInsert(NULL /* pVM */, ppaLeaves, pcLeaves, &Leaf);
                if (RT_SUCCESS(rc))
                {
                    Leaf.uSubLeaf = 1; /* Should've been cache type 2 (code), but buggy code made it data. */
                    rc = cpumR3CpuIdInsert(NULL /* pVM */, ppaLeaves, pcLeaves, &Leaf);
                }
                if (RT_SUCCESS(rc))
                {
                    Leaf.uSubLeaf = 2; /* Should've been cache type 3 (unified), but buggy code made it data. */
                    Leaf.uEcx = 4095;                   /* sets - 1 */
                    Leaf.uEbx &= UINT32_C(0x003fffff);  /* associativity - 1 */
                    Leaf.uEbx |= UINT32_C(23) << 22;
                    Leaf.uEax &= UINT32_C(0xfc003fff);  /* threads per cache - 1 */
                    Leaf.uEax |= RT_MIN(pVM->cCpus - 1, UINT32_C(0xfff)) << 14;
                    Leaf.uEax &= UINT32_C(0xffffff1f);  /* level */
                    Leaf.uEax |= UINT32_C(2) << 5;
                    rc = cpumR3CpuIdInsert(NULL /* pVM */, ppaLeaves, pcLeaves, &Leaf);
                }
            }
        }
    }
    return rc;
}


/**
 * Loads the CPU ID leaves saved by pass 0, inner worker.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 * @param   uVersion            The format version.
 * @param   paLeaves            Guest CPUID leaves loaded from the state.
 * @param   cLeaves             The number of leaves in @a paLeaves.
 * @param   pMsrs               The guest MSRs.
 */
static int cpumR3LoadCpuIdInner(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, PCPUMCPUIDLEAF paLeaves, uint32_t cLeaves, PCCPUMMSRS pMsrs)
{
    AssertMsgReturn(uVersion >= CPUM_SAVED_STATE_VERSION_VER3_2, ("%u\n", uVersion), VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);
#if !defined(RT_ARCH_AMD64) && !defined(RT_ARCH_X86)
    AssertMsgFailed(("Port me!"));
#endif

    /*
     * Continue loading the state into stack buffers.
     */
    CPUMCPUID   GuestDefCpuId;
    int rc = SSMR3GetMem(pSSM, &GuestDefCpuId, sizeof(GuestDefCpuId));
    AssertRCReturn(rc, rc);

    CPUMCPUID   aRawStd[16];
    uint32_t    cRawStd;
    rc = SSMR3GetU32(pSSM, &cRawStd); AssertRCReturn(rc, rc);
    if (cRawStd > RT_ELEMENTS(aRawStd))
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    rc = SSMR3GetMem(pSSM, &aRawStd[0], cRawStd * sizeof(aRawStd[0]));
    AssertRCReturn(rc, rc);
    for (uint32_t i = cRawStd; i < RT_ELEMENTS(aRawStd); i++)
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
        ASMCpuIdExSlow(i, 0, 0, 0, &aRawStd[i].uEax, &aRawStd[i].uEbx, &aRawStd[i].uEcx, &aRawStd[i].uEdx);
#else
        RT_ZERO(aRawStd[i]);
#endif

    CPUMCPUID   aRawExt[32];
    uint32_t    cRawExt;
    rc = SSMR3GetU32(pSSM, &cRawExt); AssertRCReturn(rc, rc);
    if (cRawExt > RT_ELEMENTS(aRawExt))
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    rc = SSMR3GetMem(pSSM, &aRawExt[0], cRawExt * sizeof(aRawExt[0]));
    AssertRCReturn(rc, rc);
    for (uint32_t i = cRawExt; i < RT_ELEMENTS(aRawExt); i++)
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
        ASMCpuIdExSlow(i | UINT32_C(0x80000000), 0, 0, 0, &aRawExt[i].uEax, &aRawExt[i].uEbx, &aRawExt[i].uEcx, &aRawExt[i].uEdx);
#else
        RT_ZERO(aRawExt[i]);
#endif

    /*
     * Get the raw CPU IDs for the current host.
     */
    CPUMCPUID   aHostRawStd[16];
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    for (unsigned i = 0; i < RT_ELEMENTS(aHostRawStd); i++)
        ASMCpuIdExSlow(i, 0, 0, 0, &aHostRawStd[i].uEax, &aHostRawStd[i].uEbx, &aHostRawStd[i].uEcx, &aHostRawStd[i].uEdx);
#else
    RT_ZERO(aHostRawStd);
#endif

    CPUMCPUID   aHostRawExt[32];
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    for (unsigned i = 0; i < RT_ELEMENTS(aHostRawExt); i++)
        ASMCpuIdExSlow(i | UINT32_C(0x80000000), 0, 0, 0,
                       &aHostRawExt[i].uEax, &aHostRawExt[i].uEbx, &aHostRawExt[i].uEcx, &aHostRawExt[i].uEdx);
#else
    RT_ZERO(aHostRawExt);
#endif

    /*
     * Get the host and guest overrides so we don't reject the state because
     * some feature was enabled thru these interfaces.
     * Note! We currently only need the feature leaves, so skip rest.
     */
    PCFGMNODE   pOverrideCfg = CFGMR3GetChild(CFGMR3GetRoot(pVM), "CPUM/HostCPUID");
    CPUMCPUID   aHostOverrideStd[2];
    memcpy(&aHostOverrideStd[0], &aHostRawStd[0], sizeof(aHostOverrideStd));
    cpumR3CpuIdInitLoadOverrideSet(UINT32_C(0x00000000), &aHostOverrideStd[0], RT_ELEMENTS(aHostOverrideStd), pOverrideCfg);

    CPUMCPUID   aHostOverrideExt[2];
    memcpy(&aHostOverrideExt[0], &aHostRawExt[0], sizeof(aHostOverrideExt));
    cpumR3CpuIdInitLoadOverrideSet(UINT32_C(0x80000000), &aHostOverrideExt[0], RT_ELEMENTS(aHostOverrideExt), pOverrideCfg);

    /*
     * This can be skipped.
     *
     * @note On ARM we disable the strict checks for now because we can't verify with what the host supports
     *       and just assume the interpreter/recompiler supports everything what was exposed earlier.
     */
    bool fStrictCpuIdChecks;
    CFGMR3QueryBoolDef(CFGMR3GetChild(CFGMR3GetRoot(pVM), "CPUM"), "StrictCpuIdChecks", &fStrictCpuIdChecks,
#ifdef RT_ARCH_ARM64
                       false
#else
                       true
#endif
                       );

    /*
     * Define a bunch of macros for simplifying the santizing/checking code below.
     */
    /* Generic expression + failure message. */
#define CPUID_CHECK_RET(expr, fmt) \
    do { \
        if (!(expr)) \
        { \
            char *pszMsg = RTStrAPrintf2 fmt; /* lack of variadic macros sucks */ \
            if (fStrictCpuIdChecks) \
            { \
                int rcCpuid = SSMR3SetLoadError(pSSM, VERR_SSM_LOAD_CPUID_MISMATCH, RT_SRC_POS, "%s", pszMsg); \
                RTStrFree(pszMsg); \
                return rcCpuid; \
            } \
            LogRel(("CPUM: %s\n", pszMsg)); \
            RTStrFree(pszMsg); \
        } \
    } while (0)
#define CPUID_CHECK_WRN(expr, fmt) \
    do { \
        if (!(expr)) \
            LogRel(fmt); \
    } while (0)

    /* For comparing two values and bitch if they differs. */
#define CPUID_CHECK2_RET(what, host, saved) \
    do { \
        if ((host) != (saved)) \
        { \
            if (fStrictCpuIdChecks) \
                return SSMR3SetLoadError(pSSM, VERR_SSM_LOAD_CPUID_MISMATCH, RT_SRC_POS, \
                                         N_(#what " mismatch: host=%#x saved=%#x"), (host), (saved)); \
            LogRel(("CPUM: " #what " differs: host=%#x saved=%#x\n", (host), (saved))); \
        } \
    } while (0)
#define CPUID_CHECK2_WRN(what, host, saved) \
    do { \
        if ((host) != (saved)) \
            LogRel(("CPUM: " #what " differs: host=%#x saved=%#x\n", (host), (saved))); \
    } while (0)

    /* For checking raw cpu features (raw mode). */
#define CPUID_RAW_FEATURE_RET(set, reg, bit) \
    do { \
        if ((aHostRaw##set [1].reg & bit) != (aRaw##set [1].reg & bit)) \
        { \
            if (fStrictCpuIdChecks) \
                return SSMR3SetLoadError(pSSM, VERR_SSM_LOAD_CPUID_MISMATCH, RT_SRC_POS, \
                                         N_(#bit " mismatch: host=%d saved=%d"), \
                                         !!(aHostRaw##set [1].reg & (bit)), !!(aRaw##set [1].reg & (bit)) ); \
            LogRel(("CPUM: " #bit" differs: host=%d saved=%d\n", \
                    !!(aHostRaw##set [1].reg & (bit)), !!(aRaw##set [1].reg & (bit)) )); \
        } \
    } while (0)
#define CPUID_RAW_FEATURE_WRN(set, reg, bit) \
    do { \
        if ((aHostRaw##set [1].reg & bit) != (aRaw##set [1].reg & bit)) \
            LogRel(("CPUM: " #bit" differs: host=%d saved=%d\n", \
                    !!(aHostRaw##set [1].reg & (bit)), !!(aRaw##set [1].reg & (bit)) )); \
    } while (0)
#define CPUID_RAW_FEATURE_IGN(set, reg, bit) do { } while (0)

    /* For checking guest features. */
#define CPUID_GST_FEATURE_RET(set, reg, bit) \
    do { \
        if (    (aGuestCpuId##set [1].reg & bit) \
            && !(aHostRaw##set [1].reg & bit) \
            && !(aHostOverride##set [1].reg & bit) \
           ) \
        { \
            if (fStrictCpuIdChecks) \
                return SSMR3SetLoadError(pSSM, VERR_SSM_LOAD_CPUID_MISMATCH, RT_SRC_POS, \
                                         N_(#bit " is not supported by the host but has already exposed to the guest")); \
            LogRel(("CPUM: " #bit " is not supported by the host but has already exposed to the guest\n")); \
        } \
    } while (0)
#define CPUID_GST_FEATURE_WRN(set, reg, bit) \
    do { \
        if (    (aGuestCpuId##set [1].reg & bit) \
            && !(aHostRaw##set [1].reg & bit) \
            && !(aHostOverride##set [1].reg & bit) \
           ) \
            LogRel(("CPUM: " #bit " is not supported by the host but has already exposed to the guest\n")); \
    } while (0)
#define CPUID_GST_FEATURE_EMU(set, reg, bit) \
    do { \
        if (    (aGuestCpuId##set [1].reg & bit) \
            && !(aHostRaw##set [1].reg & bit) \
            && !(aHostOverride##set [1].reg & bit) \
           ) \
            LogRel(("CPUM: Warning - " #bit " is not supported by the host but already exposed to the guest. This may impact performance.\n")); \
    } while (0)
#define CPUID_GST_FEATURE_IGN(set, reg, bit) do { } while (0)

    /* For checking guest features if AMD guest CPU. */
#define CPUID_GST_AMD_FEATURE_RET(set, reg, bit) \
    do { \
        if (    (aGuestCpuId##set [1].reg & bit) \
            &&  fGuestAmd \
            && (!fGuestAmd || !(aHostRaw##set [1].reg & bit)) \
            && !(aHostOverride##set [1].reg & bit) \
           ) \
        { \
            if (fStrictCpuIdChecks) \
                return SSMR3SetLoadError(pSSM, VERR_SSM_LOAD_CPUID_MISMATCH, RT_SRC_POS, \
                                         N_(#bit " is not supported by the host but has already exposed to the guest")); \
            LogRel(("CPUM: " #bit " is not supported by the host but has already exposed to the guest\n")); \
        } \
    } while (0)
#define CPUID_GST_AMD_FEATURE_WRN(set, reg, bit) \
    do { \
        if (    (aGuestCpuId##set [1].reg & bit) \
            &&  fGuestAmd \
            && (!fGuestAmd || !(aHostRaw##set [1].reg & bit)) \
            && !(aHostOverride##set [1].reg & bit) \
           ) \
            LogRel(("CPUM: " #bit " is not supported by the host but has already exposed to the guest\n")); \
    } while (0)
#define CPUID_GST_AMD_FEATURE_EMU(set, reg, bit) \
    do { \
        if (    (aGuestCpuId##set [1].reg & bit) \
            &&  fGuestAmd \
            && (!fGuestAmd || !(aHostRaw##set [1].reg & bit)) \
            && !(aHostOverride##set [1].reg & bit) \
           ) \
            LogRel(("CPUM: Warning - " #bit " is not supported by the host but already exposed to the guest. This may impact performance.\n")); \
    } while (0)
#define CPUID_GST_AMD_FEATURE_IGN(set, reg, bit) do { } while (0)

    /* For checking AMD features which have a corresponding bit in the standard
       range.  (Intel defines very few bits in the extended feature sets.) */
#define CPUID_GST_FEATURE2_RET(reg, ExtBit, StdBit) \
    do { \
        if (    (aGuestCpuIdExt [1].reg    & (ExtBit)) \
            && !(fHostAmd  \
                 ? aHostRawExt[1].reg      & (ExtBit) \
                 : aHostRawStd[1].reg      & (StdBit)) \
            && !(aHostOverrideExt[1].reg   & (ExtBit)) \
           ) \
        { \
            if (fStrictCpuIdChecks) \
                return SSMR3SetLoadError(pSSM, VERR_SSM_LOAD_CPUID_MISMATCH, RT_SRC_POS, \
                                         N_(#ExtBit " is not supported by the host but has already exposed to the guest")); \
            LogRel(("CPUM: " #ExtBit " is not supported by the host but has already exposed to the guest\n")); \
        } \
    } while (0)
#define CPUID_GST_FEATURE2_WRN(reg, ExtBit, StdBit) \
    do { \
        if (    (aGuestCpuId[1].reg        & (ExtBit)) \
            && !(fHostAmd  \
                 ? aHostRawExt[1].reg      & (ExtBit) \
                 : aHostRawStd[1].reg      & (StdBit)) \
            && !(aHostOverrideExt[1].reg   & (ExtBit)) \
           ) \
            LogRel(("CPUM: " #ExtBit " is not supported by the host but has already exposed to the guest\n")); \
    } while (0)
#define CPUID_GST_FEATURE2_EMU(reg, ExtBit, StdBit) \
    do { \
        if (    (aGuestCpuIdExt [1].reg    & (ExtBit)) \
            && !(fHostAmd  \
                 ? aHostRawExt[1].reg      & (ExtBit) \
                 : aHostRawStd[1].reg      & (StdBit)) \
            && !(aHostOverrideExt[1].reg   & (ExtBit)) \
           ) \
            LogRel(("CPUM: Warning - " #ExtBit " is not supported by the host but already exposed to the guest. This may impact performance.\n")); \
    } while (0)
#define CPUID_GST_FEATURE2_IGN(reg, ExtBit, StdBit) do { } while (0)


    /*
     * Verify that we can support the features already exposed to the guest on
     * this host.
     *
     * Most of the features we're emulating requires intercepting instruction
     * and doing it the slow way, so there is no need to warn when they aren't
     * present in the host CPU.  Thus we use IGN instead of EMU on these.
     *
     * Trailing comments:
     *      "EMU"  - Possible to emulate, could be lots of work and very slow.
     *      "EMU?" - Can this be emulated?
     */
    CPUMCPUID aGuestCpuIdStd[2];
    RT_ZERO(aGuestCpuIdStd);
    cpumR3CpuIdGetLeafLegacy(paLeaves, cLeaves, 1, 0, &aGuestCpuIdStd[1]);

    /* CPUID(1).ecx */
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_SSE3);    // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_PCLMUL);  // -> EMU?
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_DTES64);  // -> EMU?
    CPUID_GST_FEATURE_IGN(Std, uEcx, X86_CPUID_FEATURE_ECX_MONITOR);
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_CPLDS);   // -> EMU?
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_VMX);     // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_SMX);     // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_EST);     // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_TM2);     // -> EMU?
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_SSSE3);   // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_CNTXID);  // -> EMU
    CPUID_GST_FEATURE_IGN(Std, uEcx, X86_CPUID_FEATURE_ECX_SDBG);
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_FMA);     // -> EMU? what's this?
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_CX16);    // -> EMU?
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_TPRUPDATE);//-> EMU
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_PDCM);    // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEcx, RT_BIT_32(16) /*reserved*/);
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_PCID);
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_DCA);     // -> EMU?
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_SSE4_1);  // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_SSE4_2);  // -> EMU
    CPUID_GST_FEATURE_IGN(Std, uEcx, X86_CPUID_FEATURE_ECX_X2APIC);
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_MOVBE);   // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_POPCNT);  // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_TSCDEADL);
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_AES);     // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_XSAVE);   // -> EMU
    CPUID_GST_FEATURE_IGN(Std, uEcx, X86_CPUID_FEATURE_ECX_OSXSAVE);
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_AVX);     // -> EMU?
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_F16C);
    CPUID_GST_FEATURE_RET(Std, uEcx, X86_CPUID_FEATURE_ECX_RDRAND);
    CPUID_GST_FEATURE_IGN(Std, uEcx, X86_CPUID_FEATURE_ECX_HVP);     // Normally not set by host

    /* CPUID(1).edx */
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_FPU);
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_VME);
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_DE);      // -> EMU?
    CPUID_GST_FEATURE_IGN(Std, uEdx, X86_CPUID_FEATURE_EDX_PSE);
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_TSC);     // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_MSR);     // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_PAE);
    CPUID_GST_FEATURE_IGN(Std, uEdx, X86_CPUID_FEATURE_EDX_MCE);
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_CX8);     // -> EMU?
    CPUID_GST_FEATURE_IGN(Std, uEdx, X86_CPUID_FEATURE_EDX_APIC);
    CPUID_GST_FEATURE_RET(Std, uEdx, RT_BIT_32(10) /*reserved*/);
    CPUID_GST_FEATURE_IGN(Std, uEdx, X86_CPUID_FEATURE_EDX_SEP);
    CPUID_GST_FEATURE_IGN(Std, uEdx, X86_CPUID_FEATURE_EDX_MTRR);
    CPUID_GST_FEATURE_IGN(Std, uEdx, X86_CPUID_FEATURE_EDX_PGE);
    CPUID_GST_FEATURE_IGN(Std, uEdx, X86_CPUID_FEATURE_EDX_MCA);
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_CMOV);    // -> EMU
    CPUID_GST_FEATURE_IGN(Std, uEdx, X86_CPUID_FEATURE_EDX_PAT);
    CPUID_GST_FEATURE_IGN(Std, uEdx, X86_CPUID_FEATURE_EDX_PSE36);
    CPUID_GST_FEATURE_IGN(Std, uEdx, X86_CPUID_FEATURE_EDX_PSN);
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_CLFSH);   // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEdx, RT_BIT_32(20) /*reserved*/);
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_DS);      // -> EMU?
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_ACPI);    // -> EMU?
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_MMX);     // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_FXSR);    // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_SSE);     // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_SSE2);    // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_SS);      // -> EMU?
    CPUID_GST_FEATURE_IGN(Std, uEdx, X86_CPUID_FEATURE_EDX_HTT);     // -> EMU?
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_TM);      // -> EMU?
    CPUID_GST_FEATURE_RET(Std, uEdx, RT_BIT_32(30) /*JMPE/IA64*/);   // -> EMU
    CPUID_GST_FEATURE_RET(Std, uEdx, X86_CPUID_FEATURE_EDX_PBE);     // -> EMU?

    /* CPUID(0x80000000). */
    CPUMCPUID aGuestCpuIdExt[2];
    RT_ZERO(aGuestCpuIdExt);
    if (cpumR3CpuIdGetLeafLegacy(paLeaves, cLeaves, UINT32_C(0x80000001), 0, &aGuestCpuIdExt[1]))
    {
        /** @todo deal with no 0x80000001 on the host. */
        bool const fHostAmd  = RTX86IsAmdCpu(aHostRawStd[0].uEbx, aHostRawStd[0].uEcx, aHostRawStd[0].uEdx)
                            || RTX86IsHygonCpu(aHostRawStd[0].uEbx, aHostRawStd[0].uEcx, aHostRawStd[0].uEdx);
        bool const fGuestAmd = RTX86IsAmdCpu(aGuestCpuIdExt[0].uEbx, aGuestCpuIdExt[0].uEcx, aGuestCpuIdExt[0].uEdx)
                            || RTX86IsHygonCpu(aGuestCpuIdExt[0].uEbx, aGuestCpuIdExt[0].uEcx, aGuestCpuIdExt[0].uEdx);

        /* CPUID(0x80000001).ecx */
        CPUID_GST_FEATURE_WRN(Ext, uEcx, X86_CPUID_EXT_FEATURE_ECX_LAHF_SAHF);   // -> EMU
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, X86_CPUID_AMD_FEATURE_ECX_CMPL);    // -> EMU
        CPUID_GST_AMD_FEATURE_RET(Ext, uEcx, X86_CPUID_AMD_FEATURE_ECX_SVM);     // -> EMU
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, X86_CPUID_AMD_FEATURE_ECX_EXT_APIC);// ???
        CPUID_GST_AMD_FEATURE_RET(Ext, uEcx, X86_CPUID_AMD_FEATURE_ECX_CR8L);    // -> EMU
        CPUID_GST_AMD_FEATURE_RET(Ext, uEcx, X86_CPUID_AMD_FEATURE_ECX_ABM);     // -> EMU
        CPUID_GST_AMD_FEATURE_RET(Ext, uEcx, X86_CPUID_AMD_FEATURE_ECX_SSE4A);   // -> EMU
        CPUID_GST_AMD_FEATURE_RET(Ext, uEcx, X86_CPUID_AMD_FEATURE_ECX_MISALNSSE);//-> EMU
        CPUID_GST_AMD_FEATURE_RET(Ext, uEcx, X86_CPUID_AMD_FEATURE_ECX_3DNOWPRF);// -> EMU
        CPUID_GST_AMD_FEATURE_RET(Ext, uEcx, X86_CPUID_AMD_FEATURE_ECX_OSVW);    // -> EMU?
        CPUID_GST_AMD_FEATURE_RET(Ext, uEcx, X86_CPUID_AMD_FEATURE_ECX_IBS);     // -> EMU
        CPUID_GST_AMD_FEATURE_RET(Ext, uEcx, X86_CPUID_AMD_FEATURE_ECX_XOP);     // -> EMU
        CPUID_GST_AMD_FEATURE_RET(Ext, uEcx, X86_CPUID_AMD_FEATURE_ECX_SKINIT);  // -> EMU
        CPUID_GST_AMD_FEATURE_RET(Ext, uEcx, X86_CPUID_AMD_FEATURE_ECX_WDT);     // -> EMU
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(14));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(15));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(16));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(17));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(18));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(19));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(20));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(21));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(22));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(23));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(24));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(25));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(26));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(27));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(28));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(29));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(30));
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEcx, RT_BIT_32(31));

        /* CPUID(0x80000001).edx */
        CPUID_GST_FEATURE2_RET(        uEdx, X86_CPUID_AMD_FEATURE_EDX_FPU,   X86_CPUID_FEATURE_EDX_FPU);     // -> EMU
        CPUID_GST_FEATURE2_RET(        uEdx, X86_CPUID_AMD_FEATURE_EDX_VME,   X86_CPUID_FEATURE_EDX_VME);     // -> EMU
        CPUID_GST_FEATURE2_RET(        uEdx, X86_CPUID_AMD_FEATURE_EDX_DE,    X86_CPUID_FEATURE_EDX_DE);      // -> EMU
        CPUID_GST_FEATURE2_IGN(        uEdx, X86_CPUID_AMD_FEATURE_EDX_PSE,   X86_CPUID_FEATURE_EDX_PSE);
        CPUID_GST_FEATURE2_RET(        uEdx, X86_CPUID_AMD_FEATURE_EDX_TSC,   X86_CPUID_FEATURE_EDX_TSC);     // -> EMU
        CPUID_GST_FEATURE2_RET(        uEdx, X86_CPUID_AMD_FEATURE_EDX_MSR,   X86_CPUID_FEATURE_EDX_MSR);     // -> EMU
        CPUID_GST_FEATURE2_RET(        uEdx, X86_CPUID_AMD_FEATURE_EDX_PAE,   X86_CPUID_FEATURE_EDX_PAE);
        CPUID_GST_FEATURE2_IGN(        uEdx, X86_CPUID_AMD_FEATURE_EDX_MCE,   X86_CPUID_FEATURE_EDX_MCE);
        CPUID_GST_FEATURE2_RET(        uEdx, X86_CPUID_AMD_FEATURE_EDX_CX8,   X86_CPUID_FEATURE_EDX_CX8);     // -> EMU?
        CPUID_GST_FEATURE2_IGN(        uEdx, X86_CPUID_AMD_FEATURE_EDX_APIC,  X86_CPUID_FEATURE_EDX_APIC);
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEdx, RT_BIT_32(10) /*reserved*/);
        CPUID_GST_FEATURE_IGN(    Ext, uEdx, X86_CPUID_EXT_FEATURE_EDX_SYSCALL);                              // On Intel: long mode only.
        CPUID_GST_FEATURE2_IGN(        uEdx, X86_CPUID_AMD_FEATURE_EDX_MTRR,  X86_CPUID_FEATURE_EDX_MTRR);
        CPUID_GST_FEATURE2_IGN(        uEdx, X86_CPUID_AMD_FEATURE_EDX_PGE,   X86_CPUID_FEATURE_EDX_PGE);
        CPUID_GST_FEATURE2_IGN(        uEdx, X86_CPUID_AMD_FEATURE_EDX_MCA,   X86_CPUID_FEATURE_EDX_MCA);
        CPUID_GST_FEATURE2_RET(        uEdx, X86_CPUID_AMD_FEATURE_EDX_CMOV,  X86_CPUID_FEATURE_EDX_CMOV);    // -> EMU
        CPUID_GST_FEATURE2_IGN(        uEdx, X86_CPUID_AMD_FEATURE_EDX_PAT,   X86_CPUID_FEATURE_EDX_PAT);
        CPUID_GST_FEATURE2_IGN(        uEdx, X86_CPUID_AMD_FEATURE_EDX_PSE36, X86_CPUID_FEATURE_EDX_PSE36);
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEdx, RT_BIT_32(18) /*reserved*/);
        CPUID_GST_AMD_FEATURE_WRN(Ext, uEdx, RT_BIT_32(19) /*reserved*/);
        CPUID_GST_FEATURE_RET(    Ext, uEdx, X86_CPUID_EXT_FEATURE_EDX_NX);
        CPUID_GST_FEATURE_WRN(    Ext, uEdx, RT_BIT_32(21) /*reserved*/);
        CPUID_GST_FEATURE_RET(    Ext, uEdx, X86_CPUID_AMD_FEATURE_EDX_AXMMX);
        CPUID_GST_FEATURE2_RET(        uEdx, X86_CPUID_AMD_FEATURE_EDX_MMX,   X86_CPUID_FEATURE_EDX_MMX);     // -> EMU
        CPUID_GST_FEATURE2_RET(        uEdx, X86_CPUID_AMD_FEATURE_EDX_FXSR,  X86_CPUID_FEATURE_EDX_FXSR);    // -> EMU
        CPUID_GST_AMD_FEATURE_RET(Ext, uEdx, X86_CPUID_AMD_FEATURE_EDX_FFXSR);
        CPUID_GST_AMD_FEATURE_RET(Ext, uEdx, X86_CPUID_EXT_FEATURE_EDX_PAGE1GB);
        CPUID_GST_AMD_FEATURE_RET(Ext, uEdx, X86_CPUID_EXT_FEATURE_EDX_RDTSCP);
        CPUID_GST_FEATURE_IGN(    Ext, uEdx, RT_BIT_32(28) /*reserved*/);
        CPUID_GST_FEATURE_RET(    Ext, uEdx, X86_CPUID_EXT_FEATURE_EDX_LONG_MODE);
        CPUID_GST_AMD_FEATURE_RET(Ext, uEdx, X86_CPUID_AMD_FEATURE_EDX_3DNOW_EX);
        CPUID_GST_AMD_FEATURE_RET(Ext, uEdx, X86_CPUID_AMD_FEATURE_EDX_3DNOW);
    }

    /** @todo check leaf 7   */

    /* CPUID(d) - XCR0 stuff - takes ECX as input.
     * ECX=0:   EAX - Valid bits in XCR0[31:0].
     *          EBX - Maximum state size as per current XCR0 value.
     *          ECX - Maximum state size for all supported features.
     *          EDX - Valid bits in XCR0[63:32].
     * ECX=1:   EAX - Various X-features.
     *          EBX - Maximum state size as per current XCR0|IA32_XSS value.
     *          ECX - Valid bits in IA32_XSS[31:0].
     *          EDX - Valid bits in IA32_XSS[63:32].
     * ECX=N, where N in 2..63 and indicates a bit in XCR0 and/or IA32_XSS,
     *        if the bit invalid all four registers are set to zero.
     *          EAX - The state size for this feature.
     *          EBX - The state byte offset of this feature.
     *          ECX - Bit 0 indicates whether this sub-leaf maps to a valid IA32_XSS bit (=1) or a valid XCR0 bit (=0).
     *          EDX - Reserved, but is set to zero if invalid sub-leaf index.
     */
    uint64_t fGuestXcr0Mask = 0;
    PCPUMCPUIDLEAF pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x0000000d), 0);
    if (   pCurLeaf
        && (aGuestCpuIdStd[1].uEcx & X86_CPUID_FEATURE_ECX_XSAVE)
        && (   pCurLeaf->uEax
            || pCurLeaf->uEbx
            || pCurLeaf->uEcx
            || pCurLeaf->uEdx) )
    {
        fGuestXcr0Mask = RT_MAKE_U64(pCurLeaf->uEax, pCurLeaf->uEdx);
        if (fGuestXcr0Mask & ~pVM->cpum.s.fXStateHostMask)
            return SSMR3SetLoadError(pSSM, VERR_SSM_LOAD_CPUID_MISMATCH, RT_SRC_POS,
                                     N_("CPUID(0xd/0).EDX:EAX mismatch: %#llx saved, %#llx supported by the current host (XCR0 bits)"),
                                     fGuestXcr0Mask, pVM->cpum.s.fXStateHostMask);
        if ((fGuestXcr0Mask & (XSAVE_C_X87 | XSAVE_C_SSE)) != (XSAVE_C_X87 | XSAVE_C_SSE))
            return SSMR3SetLoadError(pSSM, VERR_SSM_LOAD_CPUID_MISMATCH, RT_SRC_POS,
                                     N_("CPUID(0xd/0).EDX:EAX missing mandatory X87 or SSE bits: %#RX64"), fGuestXcr0Mask);

        /* We don't support any additional features yet. */
        pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x0000000d), 1);
        if (pCurLeaf && pCurLeaf->uEax)
            return SSMR3SetLoadError(pSSM, VERR_SSM_LOAD_CPUID_MISMATCH, RT_SRC_POS,
                                     N_("CPUID(0xd/1).EAX=%#x, expected zero"), pCurLeaf->uEax);
        if (pCurLeaf && (pCurLeaf->uEcx || pCurLeaf->uEdx))
            return SSMR3SetLoadError(pSSM, VERR_SSM_LOAD_CPUID_MISMATCH, RT_SRC_POS,
                                     N_("CPUID(0xd/1).EDX:ECX=%#llx, expected zero"),
                                     RT_MAKE_U64(pCurLeaf->uEdx, pCurLeaf->uEcx));


#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
        for (uint32_t uSubLeaf = 2; uSubLeaf < 64; uSubLeaf++)
        {
            pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x0000000d), uSubLeaf);
            if (pCurLeaf)
            {
                /* If advertised, the state component offset and size must match the one used by host. */
                if (pCurLeaf->uEax || pCurLeaf->uEbx || pCurLeaf->uEcx || pCurLeaf->uEdx)
                {
                    CPUMCPUID RawHost;
                    ASMCpuIdExSlow(UINT32_C(0x0000000d), 0, uSubLeaf, 0,
                                   &RawHost.uEax, &RawHost.uEbx, &RawHost.uEcx, &RawHost.uEdx);
                    if (   RawHost.uEbx != pCurLeaf->uEbx
                        || RawHost.uEax != pCurLeaf->uEax)
                        return SSMR3SetLoadError(pSSM, VERR_SSM_LOAD_CPUID_MISMATCH, RT_SRC_POS,
                                                 N_("CPUID(0xd/%#x).EBX/EAX=%#x/%#x, current host uses %#x/%#x (offset/size)"),
                                                 uSubLeaf, pCurLeaf->uEbx, pCurLeaf->uEax, RawHost.uEbx, RawHost.uEax);
                }
            }
        }
#endif
    }
    /* Clear leaf 0xd just in case we're loading an old state... */
    else if (pCurLeaf)
    {
        for (uint32_t uSubLeaf = 0; uSubLeaf < 64; uSubLeaf++)
        {
            pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x0000000d), uSubLeaf);
            if (pCurLeaf)
            {
                AssertLogRelMsg(   uVersion <= CPUM_SAVED_STATE_VERSION_PUT_STRUCT
                                || (   pCurLeaf->uEax == 0
                                    && pCurLeaf->uEbx == 0
                                    && pCurLeaf->uEcx == 0
                                    && pCurLeaf->uEdx == 0),
                                ("uVersion=%#x; %#x %#x %#x %#x\n",
                                 uVersion, pCurLeaf->uEax, pCurLeaf->uEbx, pCurLeaf->uEcx, pCurLeaf->uEdx));
                pCurLeaf->uEax = pCurLeaf->uEbx = pCurLeaf->uEcx = pCurLeaf->uEdx = 0;
            }
        }
    }

    /* Update the fXStateGuestMask value for the VM. */
    if (pVM->cpum.s.fXStateGuestMask != fGuestXcr0Mask)
    {
        LogRel(("CPUM: fXStateGuestMask=%#llx -> %#llx\n", pVM->cpum.s.fXStateGuestMask, fGuestXcr0Mask));
        pVM->cpum.s.fXStateGuestMask = fGuestXcr0Mask;
        if (!fGuestXcr0Mask && (aGuestCpuIdStd[1].uEcx & X86_CPUID_FEATURE_ECX_XSAVE))
            return SSMR3SetLoadError(pSSM, VERR_SSM_LOAD_CPUID_MISMATCH, RT_SRC_POS,
                                     N_("Internal Processing Error: XSAVE feature bit enabled, but leaf 0xd is empty."));
    }

#undef CPUID_CHECK_RET
#undef CPUID_CHECK_WRN
#undef CPUID_CHECK2_RET
#undef CPUID_CHECK2_WRN
#undef CPUID_RAW_FEATURE_RET
#undef CPUID_RAW_FEATURE_WRN
#undef CPUID_RAW_FEATURE_IGN
#undef CPUID_GST_FEATURE_RET
#undef CPUID_GST_FEATURE_WRN
#undef CPUID_GST_FEATURE_EMU
#undef CPUID_GST_FEATURE_IGN
#undef CPUID_GST_FEATURE2_RET
#undef CPUID_GST_FEATURE2_WRN
#undef CPUID_GST_FEATURE2_EMU
#undef CPUID_GST_FEATURE2_IGN
#undef CPUID_GST_AMD_FEATURE_RET
#undef CPUID_GST_AMD_FEATURE_WRN
#undef CPUID_GST_AMD_FEATURE_EMU
#undef CPUID_GST_AMD_FEATURE_IGN

    /*
     * We're good, commit the CPU ID leaves.
     */
    pVM->cpum.s.GuestInfo.DefCpuId = GuestDefCpuId;
    rc = cpumR3CpuIdInstallAndExplodeLeaves(pVM, &pVM->cpum.s, paLeaves, cLeaves, pMsrs);
    AssertLogRelRCReturn(rc, rc);

    return VINF_SUCCESS;
}


/**
 * Loads the CPU ID leaves saved by pass 0.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 * @param   uVersion            The format version.
 * @param   pMsrs               The guest MSRs.
 */
int cpumR3LoadCpuId(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, PCCPUMMSRS pMsrs)
{
    AssertMsgReturn(uVersion >= CPUM_SAVED_STATE_VERSION_VER3_2, ("%u\n", uVersion), VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);

    /*
     * Load the CPUID leaves array first and call worker to do the rest, just so
     * we can free the memory when we need to without ending up in column 1000.
     */
    PCPUMCPUIDLEAF paLeaves;
    uint32_t       cLeaves;
    int rc = cpumR3LoadGuestCpuIdArray(pVM, pSSM, uVersion, &paLeaves, &cLeaves);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        rc = cpumR3LoadCpuIdInner(pVM, pSSM, uVersion, paLeaves, cLeaves, pMsrs);
        RTMemFree(paLeaves);
    }
    return rc;
}



/**
 * Loads the CPU ID leaves saved by pass 0 in an pre 3.2 saved state.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pSSM                The saved state handle.
 * @param   uVersion            The format version.
 */
int cpumR3LoadCpuIdPre32(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion)
{
    AssertMsgReturn(uVersion < CPUM_SAVED_STATE_VERSION_VER3_2, ("%u\n", uVersion), VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);

    /*
     * Restore the CPUID leaves.
     *
     * Note that we support restoring less than the current amount of standard
     * leaves because we've been allowed more is newer version of VBox.
     */
    uint32_t cElements;
    int rc = SSMR3GetU32(pSSM, &cElements); AssertRCReturn(rc, rc);
    if (cElements > RT_ELEMENTS(pVM->cpum.s.aGuestCpuIdPatmStd))
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    SSMR3GetMem(pSSM, &pVM->cpum.s.aGuestCpuIdPatmStd[0], cElements*sizeof(pVM->cpum.s.aGuestCpuIdPatmStd[0]));

    rc = SSMR3GetU32(pSSM, &cElements); AssertRCReturn(rc, rc);
    if (cElements != RT_ELEMENTS(pVM->cpum.s.aGuestCpuIdPatmExt))
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    SSMR3GetMem(pSSM, &pVM->cpum.s.aGuestCpuIdPatmExt[0], sizeof(pVM->cpum.s.aGuestCpuIdPatmExt));

    rc = SSMR3GetU32(pSSM, &cElements); AssertRCReturn(rc, rc);
    if (cElements != RT_ELEMENTS(pVM->cpum.s.aGuestCpuIdPatmCentaur))
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    SSMR3GetMem(pSSM, &pVM->cpum.s.aGuestCpuIdPatmCentaur[0], sizeof(pVM->cpum.s.aGuestCpuIdPatmCentaur));

    SSMR3GetMem(pSSM, &pVM->cpum.s.GuestInfo.DefCpuId, sizeof(pVM->cpum.s.GuestInfo.DefCpuId));

    /*
     * Check that the basic cpuid id information is unchanged.
     */
    /** @todo we should check the 64 bits capabilities too! */
    uint32_t au32CpuId[8] = {0,0,0,0, 0,0,0,0};
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    ASMCpuIdExSlow(0, 0, 0, 0, &au32CpuId[0], &au32CpuId[1], &au32CpuId[2], &au32CpuId[3]);
    ASMCpuIdExSlow(1, 0, 0, 0, &au32CpuId[4], &au32CpuId[5], &au32CpuId[6], &au32CpuId[7]);
#endif
    uint32_t au32CpuIdSaved[8];
    rc = SSMR3GetMem(pSSM, &au32CpuIdSaved[0], sizeof(au32CpuIdSaved));
    if (RT_SUCCESS(rc))
    {
        /* Ignore CPU stepping. */
        au32CpuId[4]      &=  0xfffffff0;
        au32CpuIdSaved[4] &=  0xfffffff0;

        /* Ignore APIC ID (AMD specs). */
        au32CpuId[5]      &= ~0xff000000;
        au32CpuIdSaved[5] &= ~0xff000000;

        /* Ignore the number of Logical CPUs (AMD specs). */
        au32CpuId[5]      &= ~0x00ff0000;
        au32CpuIdSaved[5] &= ~0x00ff0000;

        /* Ignore some advanced capability bits, that we don't expose to the guest. */
        au32CpuId[6]      &= ~(   X86_CPUID_FEATURE_ECX_DTES64
                               |  X86_CPUID_FEATURE_ECX_VMX
                               |  X86_CPUID_FEATURE_ECX_SMX
                               |  X86_CPUID_FEATURE_ECX_EST
                               |  X86_CPUID_FEATURE_ECX_TM2
                               |  X86_CPUID_FEATURE_ECX_CNTXID
                               |  X86_CPUID_FEATURE_ECX_TPRUPDATE
                               |  X86_CPUID_FEATURE_ECX_PDCM
                               |  X86_CPUID_FEATURE_ECX_DCA
                               |  X86_CPUID_FEATURE_ECX_X2APIC
                              );
        au32CpuIdSaved[6] &= ~(   X86_CPUID_FEATURE_ECX_DTES64
                               |  X86_CPUID_FEATURE_ECX_VMX
                               |  X86_CPUID_FEATURE_ECX_SMX
                               |  X86_CPUID_FEATURE_ECX_EST
                               |  X86_CPUID_FEATURE_ECX_TM2
                               |  X86_CPUID_FEATURE_ECX_CNTXID
                               |  X86_CPUID_FEATURE_ECX_TPRUPDATE
                               |  X86_CPUID_FEATURE_ECX_PDCM
                               |  X86_CPUID_FEATURE_ECX_DCA
                               |  X86_CPUID_FEATURE_ECX_X2APIC
                              );

        /* Make sure we don't forget to update the masks when enabling
         * features in the future.
         */
        AssertRelease(!(pVM->cpum.s.aGuestCpuIdPatmStd[1].uEcx &
                              (   X86_CPUID_FEATURE_ECX_DTES64
                               |  X86_CPUID_FEATURE_ECX_VMX
                               |  X86_CPUID_FEATURE_ECX_SMX
                               |  X86_CPUID_FEATURE_ECX_EST
                               |  X86_CPUID_FEATURE_ECX_TM2
                               |  X86_CPUID_FEATURE_ECX_CNTXID
                               |  X86_CPUID_FEATURE_ECX_TPRUPDATE
                               |  X86_CPUID_FEATURE_ECX_PDCM
                               |  X86_CPUID_FEATURE_ECX_DCA
                               |  X86_CPUID_FEATURE_ECX_X2APIC
                              )));
        /* do the compare */
        if (memcmp(au32CpuIdSaved, au32CpuId, sizeof(au32CpuIdSaved)))
        {
            if (SSMR3HandleGetAfter(pSSM) == SSMAFTER_DEBUG_IT)
                LogRel(("cpumR3LoadExec: CpuId mismatch! (ignored due to SSMAFTER_DEBUG_IT)\n"
                        "Saved=%.*Rhxs\n"
                        "Real =%.*Rhxs\n",
                        sizeof(au32CpuIdSaved), au32CpuIdSaved,
                        sizeof(au32CpuId), au32CpuId));
            else
            {
                LogRel(("cpumR3LoadExec: CpuId mismatch!\n"
                        "Saved=%.*Rhxs\n"
                        "Real =%.*Rhxs\n",
                        sizeof(au32CpuIdSaved), au32CpuIdSaved,
                        sizeof(au32CpuId), au32CpuId));
                rc = VERR_SSM_LOAD_CPUID_MISMATCH;
            }
        }
    }

    return rc;
}



/*
 *
 *
 * CPUID Info Handler.
 * CPUID Info Handler.
 * CPUID Info Handler.
 *
 *
 */



/**
 * Get L1 cache / TLS associativity.
 */
static const char *getCacheAss(unsigned u, char *pszBuf)
{
    if (u == 0)
        return "res0  ";
    if (u == 1)
        return "direct";
    if (u == 255)
        return "fully";
    if (u >= 256)
        return "???";

    RTStrPrintf(pszBuf, 16, "%d way", u);
    return pszBuf;
}


/**
 * Get L2/L3 cache associativity.
 */
static const char *getL23CacheAss(unsigned u)
{
    switch (u)
    {
        case 0:  return "off   ";
        case 1:  return "direct";
        case 2:  return "2 way ";
        case 3:  return "3 way ";
        case 4:  return "4 way ";
        case 5:  return "6 way ";
        case 6:  return "8 way ";
        case 7:  return "res7  ";
        case 8:  return "16 way";
        case 9:  return "tpoext";   /* Overridden by Fn8000_001D */
        case 10: return "32 way";
        case 11: return "48 way";
        case 12: return "64 way";
        case 13: return "96 way";
        case 14: return "128way";
        case 15: return "fully ";
        default: return "????";
    }
}


/** CPUID(1).EDX field descriptions. */
static DBGFREGSUBFIELD const g_aLeaf1EdxSubFields[] =
{
    DBGFREGSUBFIELD_RO("FPU\0"         "x87 FPU on Chip",                            0, 1, 0),
    DBGFREGSUBFIELD_RO("VME\0"         "Virtual 8086 Mode Enhancements",             1, 1, 0),
    DBGFREGSUBFIELD_RO("DE\0"          "Debugging extensions",                       2, 1, 0),
    DBGFREGSUBFIELD_RO("PSE\0"         "Page Size Extension",                        3, 1, 0),
    DBGFREGSUBFIELD_RO("TSC\0"         "Time Stamp Counter",                         4, 1, 0),
    DBGFREGSUBFIELD_RO("MSR\0"         "Model Specific Registers",                   5, 1, 0),
    DBGFREGSUBFIELD_RO("PAE\0"         "Physical Address Extension",                 6, 1, 0),
    DBGFREGSUBFIELD_RO("MCE\0"         "Machine Check Exception",                    7, 1, 0),
    DBGFREGSUBFIELD_RO("CX8\0"         "CMPXCHG8B instruction",                      8, 1, 0),
    DBGFREGSUBFIELD_RO("APIC\0"        "APIC On-Chip",                               9, 1, 0),
    DBGFREGSUBFIELD_RO("SEP\0"         "SYSENTER and SYSEXIT Present",              11, 1, 0),
    DBGFREGSUBFIELD_RO("MTRR\0"        "Memory Type Range Registers",               12, 1, 0),
    DBGFREGSUBFIELD_RO("PGE\0"         "PTE Global Bit",                            13, 1, 0),
    DBGFREGSUBFIELD_RO("MCA\0"         "Machine Check Architecture",                14, 1, 0),
    DBGFREGSUBFIELD_RO("CMOV\0"        "Conditional Move instructions",             15, 1, 0),
    DBGFREGSUBFIELD_RO("PAT\0"         "Page Attribute Table",                      16, 1, 0),
    DBGFREGSUBFIELD_RO("PSE-36\0"      "36-bit Page Size Extension",                17, 1, 0),
    DBGFREGSUBFIELD_RO("PSN\0"         "Processor Serial Number",                   18, 1, 0),
    DBGFREGSUBFIELD_RO("CLFSH\0"       "CLFLUSH instruction",                       19, 1, 0),
    DBGFREGSUBFIELD_RO("DS\0"          "Debug Store",                               21, 1, 0),
    DBGFREGSUBFIELD_RO("ACPI\0"        "Thermal Mon. & Soft. Clock Ctrl.",          22, 1, 0),
    DBGFREGSUBFIELD_RO("MMX\0"         "Intel MMX Technology",                      23, 1, 0),
    DBGFREGSUBFIELD_RO("FXSR\0"        "FXSAVE and FXRSTOR instructions",           24, 1, 0),
    DBGFREGSUBFIELD_RO("SSE\0"         "SSE support",                               25, 1, 0),
    DBGFREGSUBFIELD_RO("SSE2\0"        "SSE2 support",                              26, 1, 0),
    DBGFREGSUBFIELD_RO("SS\0"          "Self Snoop",                                27, 1, 0),
    DBGFREGSUBFIELD_RO("HTT\0"         "Hyper-Threading Technology",                28, 1, 0),
    DBGFREGSUBFIELD_RO("TM\0"          "Therm. Monitor",                            29, 1, 0),
    DBGFREGSUBFIELD_RO("PBE\0"         "Pending Break Enabled",                     31, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(1).ECX field descriptions. */
static DBGFREGSUBFIELD const g_aLeaf1EcxSubFields[] =
{
    DBGFREGSUBFIELD_RO("SSE3\0"       "SSE3 support",                                    0, 1, 0),
    DBGFREGSUBFIELD_RO("PCLMUL\0"     "PCLMULQDQ support (for AES-GCM)",                 1, 1, 0),
    DBGFREGSUBFIELD_RO("DTES64\0"     "DS Area 64-bit Layout",                           2, 1, 0),
    DBGFREGSUBFIELD_RO("MONITOR\0"    "MONITOR/MWAIT instructions",                      3, 1, 0),
    DBGFREGSUBFIELD_RO("CPL-DS\0"     "CPL Qualified Debug Store",                       4, 1, 0),
    DBGFREGSUBFIELD_RO("VMX\0"        "Virtual Machine Extensions",                      5, 1, 0),
    DBGFREGSUBFIELD_RO("SMX\0"        "Safer Mode Extensions",                           6, 1, 0),
    DBGFREGSUBFIELD_RO("EST\0"        "Enhanced SpeedStep Technology",                   7, 1, 0),
    DBGFREGSUBFIELD_RO("TM2\0"        "Terminal Monitor 2",                              8, 1, 0),
    DBGFREGSUBFIELD_RO("SSSE3\0"      "Supplemental Streaming SIMD Extensions 3",        9, 1, 0),
    DBGFREGSUBFIELD_RO("CNTX-ID\0"    "L1 Context ID",                                  10, 1, 0),
    DBGFREGSUBFIELD_RO("SDBG\0"       "Silicon Debug interface",                        11, 1, 0),
    DBGFREGSUBFIELD_RO("FMA\0"        "Fused Multiply Add extensions",                  12, 1, 0),
    DBGFREGSUBFIELD_RO("CX16\0"       "CMPXCHG16B instruction",                         13, 1, 0),
    DBGFREGSUBFIELD_RO("TPRUPDATE\0"  "xTPR Update Control",                            14, 1, 0),
    DBGFREGSUBFIELD_RO("PDCM\0"       "Perf/Debug Capability MSR",                      15, 1, 0),
    DBGFREGSUBFIELD_RO("PCID\0"       "Process Context Identifiers",                    17, 1, 0),
    DBGFREGSUBFIELD_RO("DCA\0"        "Direct Cache Access",                            18, 1, 0),
    DBGFREGSUBFIELD_RO("SSE4_1\0"     "SSE4_1 support",                                 19, 1, 0),
    DBGFREGSUBFIELD_RO("SSE4_2\0"     "SSE4_2 support",                                 20, 1, 0),
    DBGFREGSUBFIELD_RO("X2APIC\0"     "x2APIC support",                                 21, 1, 0),
    DBGFREGSUBFIELD_RO("MOVBE\0"      "MOVBE instruction",                              22, 1, 0),
    DBGFREGSUBFIELD_RO("POPCNT\0"     "POPCNT instruction",                             23, 1, 0),
    DBGFREGSUBFIELD_RO("TSCDEADL\0"   "Time Stamp Counter Deadline",                    24, 1, 0),
    DBGFREGSUBFIELD_RO("AES\0"        "AES instructions",                               25, 1, 0),
    DBGFREGSUBFIELD_RO("XSAVE\0"      "XSAVE instruction",                              26, 1, 0),
    DBGFREGSUBFIELD_RO("OSXSAVE\0"    "OSXSAVE instruction",                            27, 1, 0),
    DBGFREGSUBFIELD_RO("AVX\0"        "AVX support",                                    28, 1, 0),
    DBGFREGSUBFIELD_RO("F16C\0"       "16-bit floating point conversion instructions",  29, 1, 0),
    DBGFREGSUBFIELD_RO("RDRAND\0"     "RDRAND instruction",                             30, 1, 0),
    DBGFREGSUBFIELD_RO("HVP\0"        "Hypervisor Present (we're a guest)",             31, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(7,0).EBX field descriptions. */
static DBGFREGSUBFIELD const g_aLeaf7Sub0EbxSubFields[] =
{
    DBGFREGSUBFIELD_RO("FSGSBASE\0"         "RDFSBASE/RDGSBASE/WRFSBASE/WRGSBASE instr.",    0, 1, 0),
    DBGFREGSUBFIELD_RO("TSCADJUST\0"        "Supports MSR_IA32_TSC_ADJUST",                  1, 1, 0),
    DBGFREGSUBFIELD_RO("SGX\0"              "Supports Software Guard Extensions",            2, 1, 0),
    DBGFREGSUBFIELD_RO("BMI1\0"             "Advanced Bit Manipulation extension 1",         3, 1, 0),
    DBGFREGSUBFIELD_RO("HLE\0"              "Hardware Lock Elision",                         4, 1, 0),
    DBGFREGSUBFIELD_RO("AVX2\0"             "Advanced Vector Extensions 2",                  5, 1, 0),
    DBGFREGSUBFIELD_RO("FDP_EXCPTN_ONLY\0"  "FPU DP only updated on exceptions",             6, 1, 0),
    DBGFREGSUBFIELD_RO("SMEP\0"             "Supervisor Mode Execution Prevention",          7, 1, 0),
    DBGFREGSUBFIELD_RO("BMI2\0"             "Advanced Bit Manipulation extension 2",         8, 1, 0),
    DBGFREGSUBFIELD_RO("ERMS\0"             "Enhanced REP MOVSB/STOSB instructions",         9, 1, 0),
    DBGFREGSUBFIELD_RO("INVPCID\0"          "INVPCID instruction",                          10, 1, 0),
    DBGFREGSUBFIELD_RO("RTM\0"              "Restricted Transactional Memory",              11, 1, 0),
    DBGFREGSUBFIELD_RO("PQM\0"              "Platform Quality of Service Monitoring",       12, 1, 0),
    DBGFREGSUBFIELD_RO("DEPFPU_CS_DS\0"     "Deprecates FPU CS, FPU DS values if set",      13, 1, 0),
    DBGFREGSUBFIELD_RO("MPE\0"              "Intel Memory Protection Extensions",           14, 1, 0),
    DBGFREGSUBFIELD_RO("PQE\0"              "Platform Quality of Service Enforcement",      15, 1, 0),
    DBGFREGSUBFIELD_RO("AVX512F\0"          "AVX512 Foundation instructions",               16, 1, 0),
    DBGFREGSUBFIELD_RO("RDSEED\0"           "RDSEED instruction",                           18, 1, 0),
    DBGFREGSUBFIELD_RO("ADX\0"              "ADCX/ADOX instructions",                       19, 1, 0),
    DBGFREGSUBFIELD_RO("SMAP\0"             "Supervisor Mode Access Prevention",            20, 1, 0),
    DBGFREGSUBFIELD_RO("CLFLUSHOPT\0"       "CLFLUSHOPT (Cache Line Flush) instruction",    23, 1, 0),
    DBGFREGSUBFIELD_RO("CLWB\0"             "CLWB instruction",                             24, 1, 0),
    DBGFREGSUBFIELD_RO("INTEL_PT\0"         "Intel Processor Trace",                        25, 1, 0),
    DBGFREGSUBFIELD_RO("AVX512PF\0"         "AVX512 Prefetch instructions",                 26, 1, 0),
    DBGFREGSUBFIELD_RO("AVX512ER\0"         "AVX512 Exponential & Reciprocal instructions", 27, 1, 0),
    DBGFREGSUBFIELD_RO("AVX512CD\0"         "AVX512 Conflict Detection instructions",       28, 1, 0),
    DBGFREGSUBFIELD_RO("SHA\0"              "Secure Hash Algorithm extensions",             29, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(7,0).ECX field descriptions.   */
static DBGFREGSUBFIELD const g_aLeaf7Sub0EcxSubFields[] =
{
    DBGFREGSUBFIELD_RO("PREFETCHWT1\0" "PREFETCHWT1 instruction",                        0, 1, 0),
    DBGFREGSUBFIELD_RO("UMIP\0"         "User mode insturction prevention",              2, 1, 0),
    DBGFREGSUBFIELD_RO("PKU\0"          "Protection Key for Usermode pages",             3, 1, 0),
    DBGFREGSUBFIELD_RO("OSPKE\0"        "CR4.PKU mirror",                                4, 1, 0),
    DBGFREGSUBFIELD_RO("MAWAU\0"        "Value used by BNDLDX & BNDSTX",                17, 5, 0),
    DBGFREGSUBFIELD_RO("RDPID\0"        "Read processor ID support",                    22, 1, 0),
    DBGFREGSUBFIELD_RO("SGX_LC\0"       "Supports SGX Launch Configuration",            30, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(7,0).EDX field descriptions.   */
static DBGFREGSUBFIELD const g_aLeaf7Sub0EdxSubFields[] =
{
    DBGFREGSUBFIELD_RO("MCU_OPT_CTRL\0" "Supports IA32_MCU_OPT_CTRL ",                   9, 1, 0),
    DBGFREGSUBFIELD_RO("MD_CLEAR\0"     "Supports MDS related buffer clearing",         10, 1, 0),
    DBGFREGSUBFIELD_RO("TSX_FORCE_ABORT\0" "Supports IA32_TSX_FORCE_ABORT",             11, 1, 0),
    DBGFREGSUBFIELD_RO("CET_IBT\0"      "Supports indirect branch tracking w/ CET",     20, 1, 0),
    DBGFREGSUBFIELD_RO("IBRS_IBPB\0"    "IA32_SPEC_CTRL.IBRS and IA32_PRED_CMD.IBPB",   26, 1, 0),
    DBGFREGSUBFIELD_RO("STIBP\0"        "Supports IA32_SPEC_CTRL.STIBP",                27, 1, 0),
    DBGFREGSUBFIELD_RO("FLUSH_CMD\0"    "Supports IA32_FLUSH_CMD",                      28, 1, 0),
    DBGFREGSUBFIELD_RO("ARCHCAP\0"      "Supports IA32_ARCH_CAP",                       29, 1, 0),
    DBGFREGSUBFIELD_RO("CORECAP\0"      "Supports IA32_CORE_CAP",                       30, 1, 0),
    DBGFREGSUBFIELD_RO("SSBD\0"         "Supports IA32_SPEC_CTRL.SSBD",                 31, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** CPUID(7,2).EBX field descriptions. */
static DBGFREGSUBFIELD const g_aLeaf7Sub2EbxSubFields[] =
{
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(7,2).ECX field descriptions. */
static DBGFREGSUBFIELD const g_aLeaf7Sub2EcxSubFields[] =
{
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(7,2).EDX field descriptions. */
static DBGFREGSUBFIELD const g_aLeaf7Sub2EdxSubFields[] =
{
    DBGFREGSUBFIELD_RO("PSFD\0"         "Supports IA32_SPEC_CTRL[7] (PSFD)",             0, 1, 0),
    DBGFREGSUBFIELD_RO("IPRED_CTRL\0"   "Supports IA32_SPEC_CTRL[4:3] (IPRED_DIS)",      1, 1, 0),
    DBGFREGSUBFIELD_RO("RRSBA_CTRL\0"   "Supports IA32_SPEC_CTRL[6:5] (RRSBA_DIS)",      2, 1, 0),
    DBGFREGSUBFIELD_RO("DDPD_U\0"       "Supports IA32_SPEC_CTRL[8] (DDPD_U)",           3, 1, 0),
    DBGFREGSUBFIELD_RO("BHI_CTRL\0"     "Supports IA32_SPEC_CTRL[10] (BHI_DIS_S) ",      4, 1, 0),
    DBGFREGSUBFIELD_RO("MCDT_NO\0"      "No MXCSR Config Dependent Timing issues",       5, 1, 0),
    DBGFREGSUBFIELD_RO("UC_LOCK_DIS\0"  "Supports UC-lock disable and causing #AC",      6, 1, 0),
    DBGFREGSUBFIELD_RO("MONITOR_MITG_NO\0" "No MONITOR/UMONITOR power issues",           7, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** CPUID(13,0).EAX+EDX, XCR0, ++ bit descriptions. */
static DBGFREGSUBFIELD const g_aXSaveStateBits[] =
{
    DBGFREGSUBFIELD_RO("x87\0"       "Legacy FPU state",                                 0, 1, 0),
    DBGFREGSUBFIELD_RO("SSE\0"       "128-bit SSE state",                                1, 1, 0),
    DBGFREGSUBFIELD_RO("YMM_Hi128\0" "Upper 128 bits of YMM0-15 (AVX)",                  2, 1, 0),
    DBGFREGSUBFIELD_RO("BNDREGS\0"   "MPX bound register state",                         3, 1, 0),
    DBGFREGSUBFIELD_RO("BNDCSR\0"    "MPX bound config and status state",                4, 1, 0),
    DBGFREGSUBFIELD_RO("Opmask\0"    "opmask state",                                     5, 1, 0),
    DBGFREGSUBFIELD_RO("ZMM_Hi256\0" "Upper 256 bits of ZMM0-15 (AVX-512)",              6, 1, 0),
    DBGFREGSUBFIELD_RO("Hi16_ZMM\0"  "512-bits ZMM16-31 state (AVX-512)",                7, 1, 0),
    DBGFREGSUBFIELD_RO("LWP\0"       "Lightweight Profiling (AMD)",                     62, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(13,1).EAX field descriptions.   */
static DBGFREGSUBFIELD const g_aLeaf13Sub1EaxSubFields[] =
{
    DBGFREGSUBFIELD_RO("XSAVEOPT\0"  "XSAVEOPT is available",                            0, 1, 0),
    DBGFREGSUBFIELD_RO("XSAVEC\0"    "XSAVEC and compacted XRSTOR supported",            1, 1, 0),
    DBGFREGSUBFIELD_RO("XGETBC1\0"   "XGETBV with ECX=1 supported",                      2, 1, 0),
    DBGFREGSUBFIELD_RO("XSAVES\0"    "XSAVES/XRSTORS and IA32_XSS supported",            3, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** CPUID(0x80000001,0).EDX field descriptions.   */
static DBGFREGSUBFIELD const g_aExtLeaf1EdxSubFields[] =
{
    DBGFREGSUBFIELD_RO("FPU\0"          "x87 FPU on Chip",                               0, 1, 0),
    DBGFREGSUBFIELD_RO("VME\0"          "Virtual 8086 Mode Enhancements",                1, 1, 0),
    DBGFREGSUBFIELD_RO("DE\0"           "Debugging extensions",                          2, 1, 0),
    DBGFREGSUBFIELD_RO("PSE\0"          "Page Size Extension",                           3, 1, 0),
    DBGFREGSUBFIELD_RO("TSC\0"          "Time Stamp Counter",                            4, 1, 0),
    DBGFREGSUBFIELD_RO("MSR\0"          "K86 Model Specific Registers",                  5, 1, 0),
    DBGFREGSUBFIELD_RO("PAE\0"          "Physical Address Extension",                    6, 1, 0),
    DBGFREGSUBFIELD_RO("MCE\0"          "Machine Check Exception",                       7, 1, 0),
    DBGFREGSUBFIELD_RO("CX8\0"          "CMPXCHG8B instruction",                         8, 1, 0),
    DBGFREGSUBFIELD_RO("APIC\0"         "APIC On-Chip",                                  9, 1, 0),
    DBGFREGSUBFIELD_RO("SEP\0"          "SYSCALL/SYSRET",                               11, 1, 0),
    DBGFREGSUBFIELD_RO("MTRR\0"         "Memory Type Range Registers",                  12, 1, 0),
    DBGFREGSUBFIELD_RO("PGE\0"          "PTE Global Bit",                               13, 1, 0),
    DBGFREGSUBFIELD_RO("MCA\0"          "Machine Check Architecture",                   14, 1, 0),
    DBGFREGSUBFIELD_RO("CMOV\0"         "Conditional Move instructions",                15, 1, 0),
    DBGFREGSUBFIELD_RO("PAT\0"          "Page Attribute Table",                         16, 1, 0),
    DBGFREGSUBFIELD_RO("PSE-36\0"       "36-bit Page Size Extension",                   17, 1, 0),
    DBGFREGSUBFIELD_RO("NX\0"           "No-Execute/Execute-Disable",                   20, 1, 0),
    DBGFREGSUBFIELD_RO("AXMMX\0"        "AMD Extensions to MMX instructions",           22, 1, 0),
    DBGFREGSUBFIELD_RO("MMX\0"          "Intel MMX Technology",                         23, 1, 0),
    DBGFREGSUBFIELD_RO("FXSR\0"         "FXSAVE and FXRSTOR Instructions",              24, 1, 0),
    DBGFREGSUBFIELD_RO("FFXSR\0"        "AMD fast FXSAVE and FXRSTOR instructions",     25, 1, 0),
    DBGFREGSUBFIELD_RO("Page1GB\0"      "1 GB large page",                              26, 1, 0),
    DBGFREGSUBFIELD_RO("RDTSCP\0"       "RDTSCP instruction",                           27, 1, 0),
    DBGFREGSUBFIELD_RO("LM\0"           "AMD64 Long Mode",                              29, 1, 0),
    DBGFREGSUBFIELD_RO("3DNOWEXT\0"     "AMD Extensions to 3DNow",                      30, 1, 0),
    DBGFREGSUBFIELD_RO("3DNOW\0"        "AMD 3DNow",                                    31, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(0x80000001,0).ECX field descriptions.   */
static DBGFREGSUBFIELD const g_aExtLeaf1EcxSubFields[] =
{
    DBGFREGSUBFIELD_RO("LahfSahf\0"     "LAHF/SAHF support in 64-bit mode",              0, 1, 0),
    DBGFREGSUBFIELD_RO("CmpLegacy\0"    "Core multi-processing legacy mode",             1, 1, 0),
    DBGFREGSUBFIELD_RO("SVM\0"          "AMD Secure Virtual Machine extensions",         2, 1, 0),
    DBGFREGSUBFIELD_RO("EXTAPIC\0"      "AMD Extended APIC registers",                   3, 1, 0),
    DBGFREGSUBFIELD_RO("CR8L\0"         "AMD LOCK MOV CR0 means MOV CR8",                4, 1, 0),
    DBGFREGSUBFIELD_RO("ABM\0"          "AMD Advanced Bit Manipulation",                 5, 1, 0),
    DBGFREGSUBFIELD_RO("SSE4A\0"        "SSE4A instructions",                            6, 1, 0),
    DBGFREGSUBFIELD_RO("MISALIGNSSE\0"  "AMD Misaligned SSE mode",                       7, 1, 0),
    DBGFREGSUBFIELD_RO("3DNOWPRF\0"     "AMD PREFETCH and PREFETCHW instructions",       8, 1, 0),
    DBGFREGSUBFIELD_RO("OSVW\0"         "AMD OS Visible Workaround",                     9, 1, 0),
    DBGFREGSUBFIELD_RO("IBS\0"          "Instruct Based Sampling",                      10, 1, 0),
    DBGFREGSUBFIELD_RO("XOP\0"          "Extended Operation support",                   11, 1, 0),
    DBGFREGSUBFIELD_RO("SKINIT\0"       "SKINIT, STGI, and DEV support",                12, 1, 0),
    DBGFREGSUBFIELD_RO("WDT\0"          "AMD Watchdog Timer support",                   13, 1, 0),
    DBGFREGSUBFIELD_RO("LWP\0"          "Lightweight Profiling support",                15, 1, 0),
    DBGFREGSUBFIELD_RO("FMA4\0"         "Four operand FMA instruction support",         16, 1, 0),
    DBGFREGSUBFIELD_RO("TCE\0"          "Translation Cache Extension support",          17, 1, 0),
    DBGFREGSUBFIELD_RO("NodeId\0"       "NodeId in MSR C001_100C",                      19, 1, 0),
    DBGFREGSUBFIELD_RO("TBM\0"          "Trailing Bit Manipulation instructions",       21, 1, 0),
    DBGFREGSUBFIELD_RO("TOPOEXT\0"      "Topology Extensions",                          22, 1, 0),
    DBGFREGSUBFIELD_RO("PRFEXTCORE\0"   "Performance Counter Extensions support",       23, 1, 0),
    DBGFREGSUBFIELD_RO("PRFEXTNB\0"     "NB Performance Counter Extensions support",    24, 1, 0),
    DBGFREGSUBFIELD_RO("DATABPEXT\0"    "Data-access Breakpoint Extension",             26, 1, 0),
    DBGFREGSUBFIELD_RO("PERFTSC\0"      "Performance Time Stamp Counter",               27, 1, 0),
    DBGFREGSUBFIELD_RO("PCX_L2I\0"      "L2I/L3 Performance Counter Extensions",        28, 1, 0),
    DBGFREGSUBFIELD_RO("MONITORX\0"     "MWAITX and MONITORX instructions",             29, 1, 0),
    DBGFREGSUBFIELD_RO("AddrMaskExt\0"  "BP Addressing masking extended to bit 31",     30, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(0x8000000a,0).EDX field descriptions.   */
static DBGFREGSUBFIELD const g_aExtLeafAEdxSubFields[] =
{
    DBGFREGSUBFIELD_RO("NP\0"             "Nested Paging",                               0, 1, 0),
    DBGFREGSUBFIELD_RO("LbrVirt\0"        "Last Branch Record Virtualization",           1, 1, 0),
    DBGFREGSUBFIELD_RO("SVML\0"           "SVM Lock",                                    2, 1, 0),
    DBGFREGSUBFIELD_RO("NRIPS\0"          "NextRIP Save",                                3, 1, 0),
    DBGFREGSUBFIELD_RO("TscRateMsr\0"     "MSR based TSC rate control",                  4, 1, 0),
    DBGFREGSUBFIELD_RO("VmcbClean\0"      "VMCB clean bits",                             5, 1, 0),
    DBGFREGSUBFIELD_RO("FlushByASID\0"    "Flush by ASID",                               6, 1, 0),
    DBGFREGSUBFIELD_RO("DecodeAssists\0"  "Decode Assists",                              7, 1, 0),
    DBGFREGSUBFIELD_RO("PauseFilter\0"    "Pause intercept filter",                     10, 1, 0),
    DBGFREGSUBFIELD_RO("PauseFilterThreshold\0" "Pause filter threshold",               12, 1, 0),
    DBGFREGSUBFIELD_RO("AVIC\0"           "Advanced Virtual Interrupt Controller",      13, 1, 0),
    DBGFREGSUBFIELD_RO("VMSAVEVirt\0"     "VMSAVE and VMLOAD Virtualization",           15, 1, 0),
    DBGFREGSUBFIELD_RO("VGIF\0"           "Virtual Global-Interrupt Flag",              16, 1, 0),
    DBGFREGSUBFIELD_RO("GMET\0"           "Guest Mode Execute Trap Extension",          17, 1, 0),
    DBGFREGSUBFIELD_RO("x2AVIC\0"         "AVIC support for x2APIC mode",               18, 1, 0),
    DBGFREGSUBFIELD_RO("SSSCheck\0"       "SVM supervisor shadow stack restrictions",   19, 1, 0),
    DBGFREGSUBFIELD_RO("SpecCtrl\0"       "SPEC_CTRL virtualization",                   20, 1, 0),
    DBGFREGSUBFIELD_RO("ROGPT\0"          "Read-Only Guest Page Table feature support", 21, 1, 0),
    DBGFREGSUBFIELD_RO("HOST_MCE_OVERRIDE\0"    "Guest #MC can be intercepted",         23, 1, 0),
    DBGFREGSUBFIELD_RO("TlbiCtl\0"        "INVLPGB/TLBSYNC enable and intercept",       24, 1, 0),
    DBGFREGSUBFIELD_RO("VNMI\0"           "NMI Virtualization",                         25, 1, 0),
    DBGFREGSUBFIELD_RO("IbsVirt\0"        "IBS Virtualization",                         26, 1, 0),
    DBGFREGSUBFIELD_RO("ExtLvtAvicAccessChg\0"  "Extended LVT AVIC access changes",     27, 1, 0),
    DBGFREGSUBFIELD_RO("NestedVirtVmcbAddrChk\0""Guest VMCB address check",             28, 1, 0),
    DBGFREGSUBFIELD_RO("BusLockThreshold\0"     "Bus Lock Threshold",                   29, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** CPUID(0x80000007,0).EDX field descriptions.   */
static DBGFREGSUBFIELD const g_aExtLeaf7EdxSubFields[] =
{
    DBGFREGSUBFIELD_RO("TS\0"           "Temperature Sensor",                            0, 1, 0),
    DBGFREGSUBFIELD_RO("FID\0"          "Frequency ID control",                          1, 1, 0),
    DBGFREGSUBFIELD_RO("VID\0"          "Voltage ID control",                            2, 1, 0),
    DBGFREGSUBFIELD_RO("TTP\0"          "Thermal Trip",                                  3, 1, 0),
    DBGFREGSUBFIELD_RO("TM\0"           "Hardware Thermal Control (HTC)",                4, 1, 0),
    DBGFREGSUBFIELD_RO("100MHzSteps\0"  "100 MHz Multiplier control",                    6, 1, 0),
    DBGFREGSUBFIELD_RO("HwPstate\0"     "Hardware P-state control",                      7, 1, 0),
    DBGFREGSUBFIELD_RO("TscInvariant\0" "Invariant Time Stamp Counter",                  8, 1, 0),
    DBGFREGSUBFIELD_RO("CPB\0"          "Core Performance Boost",                        9, 1, 0),
    DBGFREGSUBFIELD_RO("EffFreqRO\0"    "Read-only Effective Frequency Interface",      10, 1, 0),
    DBGFREGSUBFIELD_RO("ProcFdbkIf\0"   "Processor Feedback Interface",                 11, 1, 0),
    DBGFREGSUBFIELD_RO("ProcPwrRep\0"   "Core power reporting interface support",       12, 1, 0),
    DBGFREGSUBFIELD_RO("ConnectedStandby\0" "Connected Standby",                        13, 1, 0),
    DBGFREGSUBFIELD_RO("RAPL\0"         "Running average power limit",                  14, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** CPUID(0x80000008,0).EBX field descriptions.   */
static DBGFREGSUBFIELD const g_aExtLeaf8EbxSubFields[] =
{
    DBGFREGSUBFIELD_RO("CLZERO\0"       "Clear zero instruction (cacheline)",            0, 1, 0),
    DBGFREGSUBFIELD_RO("IRPerf\0"       "Instructions retired count support",            1, 1, 0),
    DBGFREGSUBFIELD_RO("XSaveErPtr\0"   "Save/restore error pointers (FXSAVE/RSTOR)",    2, 1, 0),
    DBGFREGSUBFIELD_RO("INVLPGB\0"      "INVLPGB and TLBSYNC instructions",              3, 1, 0),
    DBGFREGSUBFIELD_RO("RDPRU\0"        "RDPRU instruction",                             4, 1, 0),
    DBGFREGSUBFIELD_RO("BE\0"           "Bandwidth Enforcement extension",               6, 1, 0),
    DBGFREGSUBFIELD_RO("MCOMMIT\0"      "MCOMMIT instruction",                           8, 1, 0),
    DBGFREGSUBFIELD_RO("WBNOINVD\0"     "WBNOINVD instruction",                          9, 1, 0),
    DBGFREGSUBFIELD_RO("IBPB\0"         "Supports the IBPB command in IA32_PRED_CMD",   12, 1, 0),
    DBGFREGSUBFIELD_RO("INT_WBINVD\0"   "WBINVD/WBNOINVD interruptible",                13, 1, 0),
    DBGFREGSUBFIELD_RO("IBRS\0"         "Indirect Branch Restricted Speculation",       14, 1, 0),
    DBGFREGSUBFIELD_RO("STIBP\0"        "Single Thread Indirect Branch Prediction",     15, 1, 0),
    DBGFREGSUBFIELD_RO("IbrsAlwaysOn\0" "Processor prefers that IBRS be left on",       16, 1, 0),
    DBGFREGSUBFIELD_RO("StibpAlwaysOn\0""Processor prefers that STIBP be left on",      17, 1, 0),
    DBGFREGSUBFIELD_RO("IbrsPreferred\0""IBRS preferred over software solution",        18, 1, 0),
    DBGFREGSUBFIELD_RO("IbrsSameMode\0" "IBRS limits same mode speculation",            19, 1, 0),
    DBGFREGSUBFIELD_RO("EferLmsleUnsupported\0" "EFER.LMSLE is unsupported",            20, 1, 0),
    DBGFREGSUBFIELD_RO("INVLPGBnestedPages\0"   "INVLPGB for nested translation",       21, 1, 0),
    DBGFREGSUBFIELD_RO("SSBD\0"         "Speculative Store Bypass Disable",             24, 1, 0),
    DBGFREGSUBFIELD_RO("SsbdVirtSpecCtrl\0"     "Use VIRT_SPEC_CTL for SSBD",           25, 1, 0),
    DBGFREGSUBFIELD_RO("SsbdNotRequired\0"      "SSBD not needed on this processor",    26, 1, 0),
    DBGFREGSUBFIELD_RO("CPPC\0"         "Collaborative Processor Performance Control",  27, 1, 0),
    DBGFREGSUBFIELD_RO("PSFD\0"         "Predictive Store Forward Disable",             28, 1, 0),
    DBGFREGSUBFIELD_RO("BTC_NO\0"       "Unaffected by branch type confusion",          29, 1, 0),
    DBGFREGSUBFIELD_RO("IBPB_RET\0"     "Clears RA predictor when PRED_CMD.IBPB set",   30, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


static void cpumR3CpuIdInfoMnemonicListU32(PCDBGFINFOHLP pHlp, uint32_t uVal, PCDBGFREGSUBFIELD pDesc,
                                           const char *pszLeadIn, uint32_t cchWidth)
{
    if (pszLeadIn)
        pHlp->pfnPrintf(pHlp, "%*s", cchWidth, pszLeadIn);

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


static void cpumR3CpuIdInfoMnemonicListU64(PCDBGFINFOHLP pHlp, uint64_t uVal, PCDBGFREGSUBFIELD pDesc,
                                           const char *pszLeadIn, uint32_t cchWidth)
{
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


static void cpumR3CpuIdInfoValueWithMnemonicListU64(PCDBGFINFOHLP pHlp, uint64_t uVal, PCDBGFREGSUBFIELD pDesc,
                                                    const char *pszLeadIn, uint32_t cchWidth)
{
    if (!uVal)
        pHlp->pfnPrintf(pHlp, "%*s %#010x`%08x\n", cchWidth, pszLeadIn, RT_HI_U32(uVal), RT_LO_U32(uVal));
    else
    {
        pHlp->pfnPrintf(pHlp, "%*s %#010x`%08x (", cchWidth, pszLeadIn, RT_HI_U32(uVal), RT_LO_U32(uVal));
        cpumR3CpuIdInfoMnemonicListU64(pHlp, uVal, pDesc, NULL, 0);
        pHlp->pfnPrintf(pHlp, " )\n");
    }
}


static void cpumR3CpuIdInfoVerboseCompareListU32(PCDBGFINFOHLP pHlp, uint32_t uVal1, uint32_t uVal2, PCDBGFREGSUBFIELD pDesc,
                                                 uint32_t cchWidth)
{
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

                pHlp->pfnPrintf(pHlp,  pDesc->cBits < 4 ? "  %s - %s%*s= %u (%u)\n" : "  %s - %s%*s= %#x (%#x)\n",
                                pDesc->pszName, pszDesc,
                                cchMnemonic + 3 + cchDesc < cchWidth ? cchWidth - (cchMnemonic + 3 + cchDesc) : 1, "",
                                uFieldValue1, uFieldValue2);

                iBit = pDesc->iFirstBit + pDesc->cBits - 1U;
                pDesc++;
            }
            else
                pHlp->pfnPrintf(pHlp, "  %2u - Reserved%*s= %u (%u)\n", iBit, 13 < cchWidth ? cchWidth - 13 : 1, "",
                                RT_BOOL(uVal1 & RT_BIT_32(iBit)), RT_BOOL(uVal2 & RT_BIT_32(iBit)));
        }
}


/**
 * Produces a detailed summary of standard leaf 0x00000001.
 *
 * @param   pHlp        The info helper functions.
 * @param   pCurLeaf    The 0x00000001 leaf.
 * @param   fVerbose    Whether to be very verbose or not.
 * @param   fIntel      Set if intel CPU.
 */
static void cpumR3CpuIdInfoStdLeaf1Details(PCDBGFINFOHLP pHlp, PCCPUMCPUIDLEAF pCurLeaf, bool fVerbose, bool fIntel)
{
    Assert(pCurLeaf); Assert(pCurLeaf->uLeaf == 1);
    static const char * const s_apszTypes[4] = { "primary", "overdrive", "MP", "reserved" };
    uint32_t uEAX = pCurLeaf->uEax;
    uint32_t uEBX = pCurLeaf->uEbx;

    pHlp->pfnPrintf(pHlp,
                    "%36s %2d \tExtended: %d \tEffective: %d\n"
                    "%36s %2d \tExtended: %d \tEffective: %d\n"
                    "%36s %d\n"
                    "%36s %d (%s)\n"
                    "%36s %#04x\n"
                    "%36s %d\n"
                    "%36s %d\n"
                    "%36s %#04x\n"
                    ,
                    "Family:",      (uEAX >> 8) & 0xf, (uEAX >> 20) & 0x7f, RTX86GetCpuFamily(uEAX),
                    "Model:",       (uEAX >> 4) & 0xf, (uEAX >> 16) & 0x0f, RTX86GetCpuModel(uEAX, fIntel),
                    "Stepping:",    RTX86GetCpuStepping(uEAX),
                    "Type:",        (uEAX >> 12) & 3, s_apszTypes[(uEAX >> 12) & 3],
                    "APIC ID:",     (uEBX >> 24) & 0xff,
                    "Logical CPUs:",(uEBX >> 16) & 0xff,
                    "CLFLUSH Size:",(uEBX >>  8) & 0xff,
                    "Brand ID:",    (uEBX >>  0) & 0xff);
    if (fVerbose)
    {
        CPUMCPUID Host = {0};
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
        ASMCpuIdExSlow(1, 0, 0, 0, &Host.uEax, &Host.uEbx, &Host.uEcx, &Host.uEdx);
#endif
        pHlp->pfnPrintf(pHlp, "Features\n");
        pHlp->pfnPrintf(pHlp, "  Mnemonic - Description                                  = guest (host)\n");
        cpumR3CpuIdInfoVerboseCompareListU32(pHlp, pCurLeaf->uEdx, Host.uEdx, g_aLeaf1EdxSubFields, 56);
        cpumR3CpuIdInfoVerboseCompareListU32(pHlp, pCurLeaf->uEcx, Host.uEcx, g_aLeaf1EcxSubFields, 56);
    }
    else
    {
        cpumR3CpuIdInfoMnemonicListU32(pHlp, pCurLeaf->uEdx, g_aLeaf1EdxSubFields, "Features EDX:", 36);
        cpumR3CpuIdInfoMnemonicListU32(pHlp, pCurLeaf->uEcx, g_aLeaf1EcxSubFields, "Features ECX:", 36);
    }
}


/**
 * Produces a detailed summary of standard leaf 0x00000007.
 *
 * @param   pHlp        The info helper functions.
 * @param   paLeaves    The CPUID leaves array.
 * @param   cLeaves     The number of leaves in the array.
 * @param   pCurLeaf    The first 0x00000007 leaf.
 * @param   fVerbose    Whether to be very verbose or not.
 */
static void cpumR3CpuIdInfoStdLeaf7Details(PCDBGFINFOHLP pHlp, PCCPUMCPUIDLEAF paLeaves, uint32_t cLeaves,
                                           PCCPUMCPUIDLEAF pCurLeaf, bool fVerbose)
{
    Assert(pCurLeaf); Assert(pCurLeaf->uLeaf == 7);
    pHlp->pfnPrintf(pHlp, "Structured Extended Feature Flags Enumeration (leaf 7):\n");
    for (;;)
    {
        CPUMCPUID Host = {0};
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
        ASMCpuIdExSlow(pCurLeaf->uLeaf, 0, pCurLeaf->uSubLeaf, 0, &Host.uEax, &Host.uEbx, &Host.uEcx, &Host.uEdx);
#endif

        switch (pCurLeaf->uSubLeaf)
        {
            case 0:
                if (fVerbose)
                {
                    pHlp->pfnPrintf(pHlp, " Sub-leaf 0\n");
                    pHlp->pfnPrintf(pHlp, "  Mnemonic - Description                                  = guest (host)\n");
                    cpumR3CpuIdInfoVerboseCompareListU32(pHlp, pCurLeaf->uEbx, Host.uEbx, g_aLeaf7Sub0EbxSubFields, 56);
                    cpumR3CpuIdInfoVerboseCompareListU32(pHlp, pCurLeaf->uEcx, Host.uEcx, g_aLeaf7Sub0EcxSubFields, 56);
                    if (pCurLeaf->uEdx || Host.uEdx)
                        cpumR3CpuIdInfoVerboseCompareListU32(pHlp, pCurLeaf->uEdx, Host.uEdx, g_aLeaf7Sub0EdxSubFields, 56);
                }
                else
                {
                    cpumR3CpuIdInfoMnemonicListU32(pHlp, pCurLeaf->uEbx, g_aLeaf7Sub0EbxSubFields, "Ext Features #0 EBX:", 36);
                    cpumR3CpuIdInfoMnemonicListU32(pHlp, pCurLeaf->uEcx, g_aLeaf7Sub0EcxSubFields, "Ext Features #0 ECX:", 36);
                    if (pCurLeaf->uEdx)
                        cpumR3CpuIdInfoMnemonicListU32(pHlp, pCurLeaf->uEdx, g_aLeaf7Sub0EdxSubFields, "Ext Features #0 EDX:", 36);
                }
                break;

            /** @todo case 1   */

            case 2:
                if (fVerbose)
                {
                    pHlp->pfnPrintf(pHlp, " Sub-leaf 2\n");
                    pHlp->pfnPrintf(pHlp, "  Mnemonic - Description                                  = guest (host)\n");
                    if (pCurLeaf->uEbx || Host.uEbx)
                        cpumR3CpuIdInfoVerboseCompareListU32(pHlp, pCurLeaf->uEbx, Host.uEbx, g_aLeaf7Sub2EbxSubFields, 56);
                    if (pCurLeaf->uEcx || Host.uEcx)
                        cpumR3CpuIdInfoVerboseCompareListU32(pHlp, pCurLeaf->uEcx, Host.uEcx, g_aLeaf7Sub2EcxSubFields, 56);
                    cpumR3CpuIdInfoVerboseCompareListU32(pHlp, pCurLeaf->uEdx, Host.uEdx, g_aLeaf7Sub2EdxSubFields, 56);
                }
                else
                {
                    if (pCurLeaf->uEbx)
                        cpumR3CpuIdInfoMnemonicListU32(pHlp, pCurLeaf->uEbx, g_aLeaf7Sub2EbxSubFields, "Ext Features #2 EBX:", 36);
                    if (pCurLeaf->uEcx)
                        cpumR3CpuIdInfoMnemonicListU32(pHlp, pCurLeaf->uEcx, g_aLeaf7Sub2EcxSubFields, "Ext Features #2 ECX:", 36);
                    if (pCurLeaf->uEdx)
                        cpumR3CpuIdInfoMnemonicListU32(pHlp, pCurLeaf->uEdx, g_aLeaf7Sub2EdxSubFields, "Ext Features #2 EDX:", 36);
                }
                break;

            default:
                if (pCurLeaf->uEdx || pCurLeaf->uEcx || pCurLeaf->uEbx)
                    pHlp->pfnPrintf(pHlp, "Unknown extended feature sub-leaf #%u: EAX=%#x EBX=%#x ECX=%#x EDX=%#x\n",
                                    pCurLeaf->uSubLeaf, pCurLeaf->uEax, pCurLeaf->uEbx, pCurLeaf->uEcx, pCurLeaf->uEdx);
                break;

        }

        /* advance. */
        pCurLeaf++;
        if (   (uintptr_t)(pCurLeaf - paLeaves) >= cLeaves
            || pCurLeaf->uLeaf != 0x7)
            break;
    }
}


/**
 * Produces a detailed summary of standard leaf 0x0000000d.
 *
 * @param   pHlp        The info helper functions.
 * @param   paLeaves    The CPUID leaves array.
 * @param   cLeaves     The number of leaves in the array.
 * @param   pCurLeaf    The first 0x00000007 leaf.
 * @param   fVerbose    Whether to be very verbose or not.
 */
static void cpumR3CpuIdInfoStdLeaf13Details(PCDBGFINFOHLP pHlp, PCCPUMCPUIDLEAF paLeaves, uint32_t cLeaves,
                                            PCCPUMCPUIDLEAF pCurLeaf, bool fVerbose)
{
    RT_NOREF_PV(fVerbose);
    Assert(pCurLeaf); Assert(pCurLeaf->uLeaf == 13);
    pHlp->pfnPrintf(pHlp, "Processor Extended State Enumeration (leaf 0xd):\n");
    for (uint32_t uSubLeaf = 0; uSubLeaf < 64; uSubLeaf++)
    {
        CPUMCPUID Host = {0};
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
        ASMCpuIdExSlow(UINT32_C(0x0000000d), 0, uSubLeaf, 0, &Host.uEax, &Host.uEbx, &Host.uEcx, &Host.uEdx);
#endif

        switch (uSubLeaf)
        {
            case 0:
                if (pCurLeaf && pCurLeaf->uSubLeaf == uSubLeaf)
                    pHlp->pfnPrintf(pHlp, "%42s %#x/%#x\n", "XSAVE area cur/max size by XCR0, guest:",
                                    pCurLeaf->uEbx, pCurLeaf->uEcx);
                pHlp->pfnPrintf(pHlp, "%42s %#x/%#x\n", "XSAVE area cur/max size by XCR0, host:",  Host.uEbx, Host.uEcx);

                if (pCurLeaf && pCurLeaf->uSubLeaf == uSubLeaf)
                    cpumR3CpuIdInfoValueWithMnemonicListU64(pHlp, RT_MAKE_U64(pCurLeaf->uEax, pCurLeaf->uEdx), g_aXSaveStateBits,
                                                            "Valid XCR0 bits, guest:", 42);
                cpumR3CpuIdInfoValueWithMnemonicListU64(pHlp, RT_MAKE_U64(Host.uEax, Host.uEdx), g_aXSaveStateBits,
                                                        "Valid XCR0 bits, host:", 42);
                break;

            case 1:
                if (pCurLeaf && pCurLeaf->uSubLeaf == uSubLeaf)
                    cpumR3CpuIdInfoMnemonicListU32(pHlp, pCurLeaf->uEax, g_aLeaf13Sub1EaxSubFields, "XSAVE features, guest:", 42);
                cpumR3CpuIdInfoMnemonicListU32(pHlp, Host.uEax, g_aLeaf13Sub1EaxSubFields, "XSAVE features, host:", 42);

                if (pCurLeaf && pCurLeaf->uSubLeaf == uSubLeaf)
                    pHlp->pfnPrintf(pHlp, "%42s %#x\n", "XSAVE area cur size XCR0|XSS, guest:", pCurLeaf->uEbx);
                pHlp->pfnPrintf(pHlp, "%42s %#x\n", "XSAVE area cur size XCR0|XSS, host:", Host.uEbx);

                if (pCurLeaf && pCurLeaf->uSubLeaf == uSubLeaf)
                    cpumR3CpuIdInfoValueWithMnemonicListU64(pHlp, RT_MAKE_U64(pCurLeaf->uEcx, pCurLeaf->uEdx), g_aXSaveStateBits,
                                                            "  Valid IA32_XSS bits, guest:", 42);
                cpumR3CpuIdInfoValueWithMnemonicListU64(pHlp, RT_MAKE_U64(Host.uEdx, Host.uEcx), g_aXSaveStateBits,
                                                        "  Valid IA32_XSS bits, host:", 42);
                break;

            default:
                if (   pCurLeaf
                    && pCurLeaf->uSubLeaf == uSubLeaf
                    && (pCurLeaf->uEax || pCurLeaf->uEbx || pCurLeaf->uEcx || pCurLeaf->uEdx) )
                {
                    pHlp->pfnPrintf(pHlp, "  State #%u, guest: off=%#06x, cb=%#06x %s", uSubLeaf, pCurLeaf->uEbx,
                                    pCurLeaf->uEax, pCurLeaf->uEcx & RT_BIT_32(0) ? "XCR0-bit" : "IA32_XSS-bit");
                    if (pCurLeaf->uEcx & ~RT_BIT_32(0))
                        pHlp->pfnPrintf(pHlp, " ECX[reserved]=%#x\n", pCurLeaf->uEcx & ~RT_BIT_32(0));
                    if (pCurLeaf->uEdx)
                        pHlp->pfnPrintf(pHlp, " EDX[reserved]=%#x\n", pCurLeaf->uEdx);
                    pHlp->pfnPrintf(pHlp, " --");
                    cpumR3CpuIdInfoMnemonicListU64(pHlp, RT_BIT_64(uSubLeaf), g_aXSaveStateBits, NULL, 0);
                    pHlp->pfnPrintf(pHlp, "\n");
                }
                if (Host.uEax || Host.uEbx || Host.uEcx || Host.uEdx)
                {
                    pHlp->pfnPrintf(pHlp, "  State #%u, host:  off=%#06x, cb=%#06x %s", uSubLeaf, Host.uEbx,
                                    Host.uEax, Host.uEcx & RT_BIT_32(0) ? "XCR0-bit" : "IA32_XSS-bit");
                    if (Host.uEcx & ~RT_BIT_32(0))
                        pHlp->pfnPrintf(pHlp, " ECX[reserved]=%#x\n", Host.uEcx & ~RT_BIT_32(0));
                    if (Host.uEdx)
                        pHlp->pfnPrintf(pHlp, " EDX[reserved]=%#x\n", Host.uEdx);
                    pHlp->pfnPrintf(pHlp, " --");
                    cpumR3CpuIdInfoMnemonicListU64(pHlp, RT_BIT_64(uSubLeaf), g_aXSaveStateBits, NULL, 0);
                    pHlp->pfnPrintf(pHlp, "\n");
                }
                break;

        }

        /* advance. */
        if (pCurLeaf)
        {
            while (   (uintptr_t)(pCurLeaf - paLeaves) < cLeaves
                   && pCurLeaf->uSubLeaf <= uSubLeaf
                   && pCurLeaf->uLeaf == UINT32_C(0x0000000d))
                pCurLeaf++;
            if (   (uintptr_t)(pCurLeaf - paLeaves) >= cLeaves
                || pCurLeaf->uLeaf != UINT32_C(0x0000000d))
                pCurLeaf = NULL;
        }
    }
}


static PCCPUMCPUIDLEAF cpumR3CpuIdInfoRawRange(PCDBGFINFOHLP pHlp, PCCPUMCPUIDLEAF paLeaves, uint32_t cLeaves,
                                               PCCPUMCPUIDLEAF pCurLeaf, uint32_t uUpToLeaf, const char *pszTitle)
{
    if (   (uintptr_t)(pCurLeaf - paLeaves) < cLeaves
        && pCurLeaf->uLeaf <= uUpToLeaf)
    {
        pHlp->pfnPrintf(pHlp,
                        "         %s\n"
                        "     Leaf/sub-leaf  eax      ebx      ecx      edx\n", pszTitle);
        while (   (uintptr_t)(pCurLeaf - paLeaves) < cLeaves
               && pCurLeaf->uLeaf <= uUpToLeaf)
        {
            CPUMCPUID Host = {0};
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
            ASMCpuIdExSlow(pCurLeaf->uLeaf, 0, pCurLeaf->uSubLeaf, 0, &Host.uEax, &Host.uEbx, &Host.uEcx, &Host.uEdx);
#endif
            pHlp->pfnPrintf(pHlp,
                            "Gst: %08x/%04x  %08x %08x %08x %08x\n"
                            "Hst:                %08x %08x %08x %08x\n",
                            pCurLeaf->uLeaf, pCurLeaf->uSubLeaf, pCurLeaf->uEax, pCurLeaf->uEbx, pCurLeaf->uEcx, pCurLeaf->uEdx,
                            Host.uEax, Host.uEbx, Host.uEcx, Host.uEdx);
            pCurLeaf++;
        }
    }

    return pCurLeaf;
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

    uint32_t        uLeaf;
    CPUMCPUID       Host     = {0};
    uint32_t        cLeaves  = pVM->cpum.s.GuestInfo.cCpuIdLeaves;
    PCPUMCPUIDLEAF  paLeaves = pVM->cpum.s.GuestInfo.paCpuIdLeavesR3;
    PCCPUMCPUIDLEAF pCurLeaf;
    PCCPUMCPUIDLEAF pNextLeaf;
    bool const      fIntel = RTX86IsIntelCpu(pVM->cpum.s.aGuestCpuIdPatmStd[0].uEbx,
                                               pVM->cpum.s.aGuestCpuIdPatmStd[0].uEcx,
                                               pVM->cpum.s.aGuestCpuIdPatmStd[0].uEdx);

    /*
     * Standard leaves.  Custom raw dump here due to ECX sub-leaves host handling.
     */
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    uint32_t        cHstMax = ASMCpuId_EAX(0);
#else
    uint32_t        cHstMax = 0;
#endif
    uint32_t        cGstMax = paLeaves[0].uLeaf == 0 ? paLeaves[0].uEax : 0;
    uint32_t        cMax    = RT_MAX(cGstMax, cHstMax);
    pHlp->pfnPrintf(pHlp,
                    "         Raw Standard CPUID Leaves\n"
                    "     Leaf/sub-leaf  eax      ebx      ecx      edx\n");
    for (uLeaf = 0, pCurLeaf = paLeaves; uLeaf <= cMax; uLeaf++)
    {
        uint32_t cMaxSubLeaves = 1;
        if (uLeaf == 4 || uLeaf == 7 || uLeaf == 0xb)
            cMaxSubLeaves = 16;
        else if (uLeaf == 0xd)
            cMaxSubLeaves = 128;

        for (uint32_t uSubLeaf = 0; uSubLeaf < cMaxSubLeaves; uSubLeaf++)
        {
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
            ASMCpuIdExSlow(uLeaf, 0, uSubLeaf, 0, &Host.uEax, &Host.uEbx, &Host.uEcx, &Host.uEdx);
#endif
            if (   (uintptr_t)(pCurLeaf - paLeaves) < cLeaves
                && pCurLeaf->uLeaf    == uLeaf
                && pCurLeaf->uSubLeaf == uSubLeaf)
            {
                pHlp->pfnPrintf(pHlp,
                                "Gst: %08x/%04x  %08x %08x %08x %08x\n"
                                "Hst:                %08x %08x %08x %08x\n",
                                uLeaf, uSubLeaf, pCurLeaf->uEax, pCurLeaf->uEbx, pCurLeaf->uEcx, pCurLeaf->uEdx,
                                Host.uEax, Host.uEbx, Host.uEcx, Host.uEdx);
                pCurLeaf++;
            }
            else if (   uLeaf != 0xd
                     || uSubLeaf <= 1
                     || Host.uEbx != 0 )
                pHlp->pfnPrintf(pHlp,
                                "Hst: %08x/%04x  %08x %08x %08x %08x\n",
                                uLeaf, uSubLeaf, Host.uEax, Host.uEbx, Host.uEcx, Host.uEdx);

            /* Done? */
            if (   (   (uintptr_t)(pCurLeaf - paLeaves) >= cLeaves
                    || pCurLeaf->uLeaf != uLeaf)
                && (   (uLeaf == 0x4 && ((Host.uEax & 0x000f) == 0 || (Host.uEax & 0x000f) >= 8))
                    || (uLeaf == 0x7 && Host.uEax == 0)
                    || (uLeaf == 0xb && ((Host.uEcx & 0xff00) == 0 || (Host.uEcx & 0xff00) >= 8))
                    || (uLeaf == 0xb && (Host.uEcx & 0xff) != uSubLeaf)
                    || (uLeaf == 0xd && uSubLeaf >= 128)
                   )
               )
                break;
        }
    }
    pNextLeaf = pCurLeaf;

    /*
     * If verbose, decode it.
     */
    if (iVerbosity && paLeaves[0].uLeaf == 0)
        pHlp->pfnPrintf(pHlp,
                        "%36s %.04s%.04s%.04s\n"
                        "%36s 0x00000000-%#010x\n"
                        ,
                        "Name:", &paLeaves[0].uEbx, &paLeaves[0].uEdx, &paLeaves[0].uEcx,
                        "Supports:", paLeaves[0].uEax);

    if (iVerbosity && (pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x00000001), 0)) != NULL)
        cpumR3CpuIdInfoStdLeaf1Details(pHlp, pCurLeaf, iVerbosity > 1, fIntel);

    if (iVerbosity && (pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x00000007), 0)) != NULL)
        cpumR3CpuIdInfoStdLeaf7Details(pHlp, paLeaves, cLeaves, pCurLeaf, iVerbosity > 1);

    if (iVerbosity && (pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x0000000d), 0)) != NULL)
        cpumR3CpuIdInfoStdLeaf13Details(pHlp, paLeaves, cLeaves, pCurLeaf, iVerbosity > 1);

    pCurLeaf = pNextLeaf;

    /*
     * Hypervisor leaves.
     *
     * Unlike most of the other leaves reported, the guest hypervisor leaves
     * aren't a subset of the host CPUID bits.
     */
    pCurLeaf = cpumR3CpuIdInfoRawRange(pHlp, paLeaves, cLeaves, pCurLeaf, UINT32_C(0x3fffffff), "Unknown CPUID Leaves");

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    ASMCpuIdExSlow(UINT32_C(0x40000000), 0, 0, 0, &Host.uEax, &Host.uEbx, &Host.uEcx, &Host.uEdx);
#endif
    cHstMax  = Host.uEax >= UINT32_C(0x40000001) && Host.uEax <= UINT32_C(0x40000fff) ? Host.uEax : 0;
    cGstMax  = (uintptr_t)(pCurLeaf - paLeaves) < cLeaves && pCurLeaf->uLeaf == UINT32_C(0x40000000)
             ? RT_MIN(pCurLeaf->uEax, UINT32_C(0x40000fff)) : 0;
    cMax     = RT_MAX(cHstMax, cGstMax);
    if (cMax >= UINT32_C(0x40000000))
    {
        pNextLeaf = cpumR3CpuIdInfoRawRange(pHlp, paLeaves, cLeaves, pCurLeaf, cMax, "Raw Hypervisor CPUID Leaves");

        /** @todo dump these in more detail. */

        pCurLeaf = pNextLeaf;
    }


    /*
     * Extended.  Custom raw dump here due to ECX sub-leaves host handling.
     * Implemented after AMD specs.
     */
    pCurLeaf = cpumR3CpuIdInfoRawRange(pHlp, paLeaves, cLeaves, pCurLeaf, UINT32_C(0x7fffffff), "Unknown CPUID Leaves");

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    ASMCpuIdExSlow(UINT32_C(0x80000000), 0, 0, 0, &Host.uEax, &Host.uEbx, &Host.uEcx, &Host.uEdx);
#endif
    cHstMax  = RTX86IsValidExtRange(Host.uEax) ? RT_MIN(Host.uEax, UINT32_C(0x80000fff)) : 0;
    cGstMax  = (uintptr_t)(pCurLeaf - paLeaves) < cLeaves && pCurLeaf->uLeaf == UINT32_C(0x80000000)
             ? RT_MIN(pCurLeaf->uEax, UINT32_C(0x80000fff)) : 0;
    cMax     = RT_MAX(cHstMax, cGstMax);
    if (cMax >= UINT32_C(0x80000000))
    {

        pHlp->pfnPrintf(pHlp,
                        "         Raw Extended CPUID Leaves\n"
                        "     Leaf/sub-leaf  eax      ebx      ecx      edx\n");
        PCCPUMCPUIDLEAF pExtLeaf = pCurLeaf;
        for (uLeaf = UINT32_C(0x80000000); uLeaf <= cMax; uLeaf++)
        {
            uint32_t cMaxSubLeaves = 1;
            if (uLeaf == UINT32_C(0x8000001d))
                cMaxSubLeaves = 16;

            for (uint32_t uSubLeaf = 0; uSubLeaf < cMaxSubLeaves; uSubLeaf++)
            {
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
                ASMCpuIdExSlow(uLeaf, 0, uSubLeaf, 0, &Host.uEax, &Host.uEbx, &Host.uEcx, &Host.uEdx);
#endif
                if (   (uintptr_t)(pCurLeaf - paLeaves) < cLeaves
                    && pCurLeaf->uLeaf    == uLeaf
                    && pCurLeaf->uSubLeaf == uSubLeaf)
                {
                    pHlp->pfnPrintf(pHlp,
                                    "Gst: %08x/%04x  %08x %08x %08x %08x\n"
                                    "Hst:                %08x %08x %08x %08x\n",
                                    uLeaf, uSubLeaf, pCurLeaf->uEax, pCurLeaf->uEbx, pCurLeaf->uEcx, pCurLeaf->uEdx,
                                    Host.uEax, Host.uEbx, Host.uEcx, Host.uEdx);
                    pCurLeaf++;
                }
                else if (   uLeaf != 0xd
                         || uSubLeaf <= 1
                         || Host.uEbx != 0 )
                    pHlp->pfnPrintf(pHlp,
                                    "Hst: %08x/%04x  %08x %08x %08x %08x\n",
                                    uLeaf, uSubLeaf, Host.uEax, Host.uEbx, Host.uEcx, Host.uEdx);

                /* Done? */
                if (   (   (uintptr_t)(pCurLeaf - paLeaves) >= cLeaves
                        || pCurLeaf->uLeaf != uLeaf)
                    && (uLeaf == UINT32_C(0x8000001d) && ((Host.uEax & 0x000f) == 0 || (Host.uEax & 0x000f) >= 8)) )
                    break;
            }
        }
        pNextLeaf = pCurLeaf;

        /*
         * Understandable output
         */
        if (iVerbosity)
            pHlp->pfnPrintf(pHlp,
                            "Ext Name:                        %.4s%.4s%.4s\n"
                            "Ext Supports:                    0x80000000-%#010x\n",
                            &pExtLeaf->uEbx, &pExtLeaf->uEdx, &pExtLeaf->uEcx, pExtLeaf->uEax);

        pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x80000001), 0);
        if (iVerbosity && pCurLeaf)
        {
            uint32_t uEAX = pCurLeaf->uEax;
            pHlp->pfnPrintf(pHlp,
                            "Family:                          %d  \tExtended: %d \tEffective: %d\n"
                            "Model:                           %d  \tExtended: %d \tEffective: %d\n"
                            "Stepping:                        %d\n"
                            "Brand ID:                        %#05x\n",
                            (uEAX >> 8) & 0xf, (uEAX >> 20) & 0x7f, RTX86GetCpuFamily(uEAX),
                            (uEAX >> 4) & 0xf, (uEAX >> 16) & 0x0f, RTX86GetCpuModel(uEAX, fIntel),
                            RTX86GetCpuStepping(uEAX),
                            pCurLeaf->uEbx & 0xfff);

            if (iVerbosity == 1)
            {
                cpumR3CpuIdInfoMnemonicListU32(pHlp, pCurLeaf->uEdx, g_aExtLeaf1EdxSubFields, "Ext Features EDX:", 34);
                cpumR3CpuIdInfoMnemonicListU32(pHlp, pCurLeaf->uEcx, g_aExtLeaf1EdxSubFields, "Ext Features ECX:", 34);
            }
            else
            {
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
                ASMCpuIdExSlow(0x80000001, 0, 0, 0, &Host.uEax, &Host.uEbx, &Host.uEcx, &Host.uEdx);
#endif
                pHlp->pfnPrintf(pHlp, "Ext Features\n");
                pHlp->pfnPrintf(pHlp, "  Mnemonic - Description                                  = guest (host)\n");
                cpumR3CpuIdInfoVerboseCompareListU32(pHlp, pCurLeaf->uEdx, Host.uEdx, g_aExtLeaf1EdxSubFields, 56);
                cpumR3CpuIdInfoVerboseCompareListU32(pHlp, pCurLeaf->uEcx, Host.uEcx, g_aExtLeaf1EcxSubFields, 56);
                if (Host.uEcx & X86_CPUID_AMD_FEATURE_ECX_SVM)
                {
                    pHlp->pfnPrintf(pHlp, "SVM Feature Identification (leaf A):\n");
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
                    ASMCpuIdExSlow(0x8000000a, 0, 0, 0, &Host.uEax, &Host.uEbx, &Host.uEcx, &Host.uEdx);
#endif
                    pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x8000000a), 0);
                    uint32_t const uGstEdx = pCurLeaf ? pCurLeaf->uEdx : 0;
                    cpumR3CpuIdInfoVerboseCompareListU32(pHlp, uGstEdx, Host.uEdx, g_aExtLeafAEdxSubFields, 56);
                }
            }
        }

        if (iVerbosity && (pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x80000002), 0)) != NULL)
        {
            char szString[4*4*3+1] = {0};
            uint32_t *pu32 = (uint32_t *)szString;
            *pu32++ = pCurLeaf->uEax;
            *pu32++ = pCurLeaf->uEbx;
            *pu32++ = pCurLeaf->uEcx;
            *pu32++ = pCurLeaf->uEdx;
            pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x80000003), 0);
            if (pCurLeaf)
            {
                *pu32++ = pCurLeaf->uEax;
                *pu32++ = pCurLeaf->uEbx;
                *pu32++ = pCurLeaf->uEcx;
                *pu32++ = pCurLeaf->uEdx;
            }
            pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x80000004), 0);
            if (pCurLeaf)
            {
                *pu32++ = pCurLeaf->uEax;
                *pu32++ = pCurLeaf->uEbx;
                *pu32++ = pCurLeaf->uEcx;
                *pu32++ = pCurLeaf->uEdx;
            }
            pHlp->pfnPrintf(pHlp, "Full Name:                       \"%s\"\n", szString);
        }

        if (iVerbosity && (pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x80000005), 0)) != NULL)
        {
            uint32_t uEAX = pCurLeaf->uEax;
            uint32_t uEBX = pCurLeaf->uEbx;
            uint32_t uECX = pCurLeaf->uEcx;
            uint32_t uEDX = pCurLeaf->uEdx;
            char sz1[32];
            char sz2[32];

            pHlp->pfnPrintf(pHlp,
                            "TLB 2/4M Instr/Uni:              %s %3d entries\n"
                            "TLB 2/4M Data:                   %s %3d entries\n",
                            getCacheAss((uEAX >>  8) & 0xff, sz1), (uEAX >>  0) & 0xff,
                            getCacheAss((uEAX >> 24) & 0xff, sz2), (uEAX >> 16) & 0xff);
            pHlp->pfnPrintf(pHlp,
                            "TLB 4K Instr/Uni:                %s %3d entries\n"
                            "TLB 4K Data:                     %s %3d entries\n",
                            getCacheAss((uEBX >>  8) & 0xff, sz1), (uEBX >>  0) & 0xff,
                            getCacheAss((uEBX >> 24) & 0xff, sz2), (uEBX >> 16) & 0xff);
            pHlp->pfnPrintf(pHlp, "L1 Instr Cache Line Size:        %d bytes\n"
                            "L1 Instr Cache Lines Per Tag:    %d\n"
                            "L1 Instr Cache Associativity:    %s\n"
                            "L1 Instr Cache Size:             %d KB\n",
                            (uEDX >> 0) & 0xff,
                            (uEDX >> 8) & 0xff,
                            getCacheAss((uEDX >> 16) & 0xff, sz1),
                            (uEDX >> 24) & 0xff);
            pHlp->pfnPrintf(pHlp,
                            "L1 Data Cache Line Size:         %d bytes\n"
                            "L1 Data Cache Lines Per Tag:     %d\n"
                            "L1 Data Cache Associativity:     %s\n"
                            "L1 Data Cache Size:              %d KB\n",
                            (uECX >> 0) & 0xff,
                            (uECX >> 8) & 0xff,
                            getCacheAss((uECX >> 16) & 0xff, sz1),
                            (uECX >> 24) & 0xff);
        }

        if (iVerbosity && (pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x80000006), 0)) != NULL)
        {
            uint32_t uEAX = pCurLeaf->uEax;
            uint32_t uEBX = pCurLeaf->uEbx;
            uint32_t uECX = pCurLeaf->uEcx;
            uint32_t uEDX = pCurLeaf->uEdx;

            pHlp->pfnPrintf(pHlp,
                            "L2 TLB 2/4M Instr/Uni:           %s %4d entries\n"
                            "L2 TLB 2/4M Data:                %s %4d entries\n",
                            getL23CacheAss((uEAX >> 12) & 0xf), (uEAX >>  0) & 0xfff,
                            getL23CacheAss((uEAX >> 28) & 0xf), (uEAX >> 16) & 0xfff);
            pHlp->pfnPrintf(pHlp,
                            "L2 TLB 4K Instr/Uni:             %s %4d entries\n"
                            "L2 TLB 4K Data:                  %s %4d entries\n",
                            getL23CacheAss((uEBX >> 12) & 0xf), (uEBX >>  0) & 0xfff,
                            getL23CacheAss((uEBX >> 28) & 0xf), (uEBX >> 16) & 0xfff);
            pHlp->pfnPrintf(pHlp,
                            "L2 Cache Line Size:              %d bytes\n"
                            "L2 Cache Lines Per Tag:          %d\n"
                            "L2 Cache Associativity:          %s\n"
                            "L2 Cache Size:                   %d KB\n",
                            (uECX >> 0) & 0xff,
                            (uECX >> 8) & 0xf,
                            getL23CacheAss((uECX >> 12) & 0xf),
                            (uECX >> 16) & 0xffff);
            pHlp->pfnPrintf(pHlp,
                            "L3 Cache Line Size:              %d bytes\n"
                            "L3 Cache Lines Per Tag:          %d\n"
                            "L3 Cache Associativity:          %s\n"
                            "L3 Cache Size:                   %d KB\n",
                            (uEDX >> 0) & 0xff,
                            (uEDX >> 8) & 0xf,
                            getL23CacheAss((uEDX >> 12) & 0xf),
                            ((uEDX >> 18) & 0x3fff) * 512);
        }

        if (iVerbosity && (pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x80000007), 0)) != NULL)
        {
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
            ASMCpuIdExSlow(UINT32_C(0x80000007), 0, 0, 0, &Host.uEax, &Host.uEbx, &Host.uEcx, &Host.uEdx);
#endif
            if (pCurLeaf->uEdx || (Host.uEdx && iVerbosity))
            {
                if (iVerbosity < 1)
                    cpumR3CpuIdInfoMnemonicListU32(pHlp, pCurLeaf->uEdx, g_aExtLeaf7EdxSubFields, "APM Features EDX:", 34);
                else
                    cpumR3CpuIdInfoVerboseCompareListU32(pHlp, pCurLeaf->uEdx, Host.uEdx, g_aExtLeaf7EdxSubFields, 56);
            }
        }

        pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0x80000008), 0);
        if (pCurLeaf != NULL)
        {
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
            ASMCpuIdExSlow(UINT32_C(0x80000008), 0, 0, 0, &Host.uEax, &Host.uEbx, &Host.uEcx, &Host.uEdx);
#endif
            if (pCurLeaf->uEbx || (Host.uEbx && iVerbosity))
            {
                if (iVerbosity < 1)
                    cpumR3CpuIdInfoMnemonicListU32(pHlp, pCurLeaf->uEbx, g_aExtLeaf8EbxSubFields, "Ext Features ext IDs EBX:", 34);
                else
                    cpumR3CpuIdInfoVerboseCompareListU32(pHlp, pCurLeaf->uEbx, Host.uEbx, g_aExtLeaf8EbxSubFields, 56);
            }

            if (iVerbosity)
            {
                uint32_t uEAX = pCurLeaf->uEax;
                uint32_t uECX = pCurLeaf->uEcx;

                /** @todo 0x80000008:EAX[23:16] is only defined for AMD. We'll get 0 on Intel. On
                 *        AMD if we get 0, the guest physical address width should be taken from
                 *        0x80000008:EAX[7:0] instead. Guest Physical address width is relevant
                 *        for guests using nested paging. */
                pHlp->pfnPrintf(pHlp,
                                "Physical Address Width:          %d bits\n"
                                "Virtual Address Width:           %d bits\n"
                                "Guest Physical Address Width:    %d bits\n",
                                (uEAX >> 0) & 0xff,
                                (uEAX >> 8) & 0xff,
                                (uEAX >> 16) & 0xff);

                /** @todo 0x80000008:ECX is reserved on Intel (we'll get incorrect physical core
                 *        count here). */
                pHlp->pfnPrintf(pHlp,
                                "Physical Core Count:             %d\n",
                                ((uECX >> 0) & 0xff) + 1);
            }
        }

        pCurLeaf = pNextLeaf;
    }



    /*
     * Centaur.
     */
    pCurLeaf = cpumR3CpuIdInfoRawRange(pHlp, paLeaves, cLeaves, pCurLeaf, UINT32_C(0xbfffffff), "Unknown CPUID Leaves");

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    ASMCpuIdExSlow(UINT32_C(0xc0000000), 0, 0, 0, &Host.uEax, &Host.uEbx, &Host.uEcx, &Host.uEdx);
#endif
    cHstMax  = Host.uEax >= UINT32_C(0xc0000001) && Host.uEax <= UINT32_C(0xc0000fff)
             ? RT_MIN(Host.uEax,      UINT32_C(0xc0000fff)) : 0;
    cGstMax  = (uintptr_t)(pCurLeaf - paLeaves) < cLeaves && pCurLeaf->uLeaf == UINT32_C(0xc0000000)
             ? RT_MIN(pCurLeaf->uEax, UINT32_C(0xc0000fff)) : 0;
    cMax     = RT_MAX(cHstMax, cGstMax);
    if (cMax >= UINT32_C(0xc0000000))
    {
        pNextLeaf = cpumR3CpuIdInfoRawRange(pHlp, paLeaves, cLeaves, pCurLeaf, cMax, "Raw Centaur CPUID Leaves");

        /*
         * Understandable output
         */
        if (iVerbosity && (pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0xc0000000), 0)) != NULL)
            pHlp->pfnPrintf(pHlp,
                            "Centaur Supports:                0xc0000000-%#010x\n",
                            pCurLeaf->uEax);

        if (iVerbosity && (pCurLeaf = cpumCpuIdGetLeafInt(paLeaves, cLeaves, UINT32_C(0xc0000001), 0)) != NULL)
        {
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
            ASMCpuIdExSlow(0xc0000001, 0, 0, 0, &Host.uEax, &Host.uEbx, &Host.uEcx, &Host.uEdx);
#endif
            uint32_t uEdxGst = pCurLeaf->uEdx;
            uint32_t uEdxHst = Host.uEdx;

            if (iVerbosity == 1)
            {
                pHlp->pfnPrintf(pHlp, "Centaur Features EDX:           ");
                if (uEdxGst & RT_BIT(0))   pHlp->pfnPrintf(pHlp, " AIS");
                if (uEdxGst & RT_BIT(1))   pHlp->pfnPrintf(pHlp, " AIS-E");
                if (uEdxGst & RT_BIT(2))   pHlp->pfnPrintf(pHlp, " RNG");
                if (uEdxGst & RT_BIT(3))   pHlp->pfnPrintf(pHlp, " RNG-E");
                if (uEdxGst & RT_BIT(4))   pHlp->pfnPrintf(pHlp, " LH");
                if (uEdxGst & RT_BIT(5))   pHlp->pfnPrintf(pHlp, " FEMMS");
                if (uEdxGst & RT_BIT(6))   pHlp->pfnPrintf(pHlp, " ACE");
                if (uEdxGst & RT_BIT(7))   pHlp->pfnPrintf(pHlp, " ACE-E");
                /* possibly indicating MM/HE and MM/HE-E on older chips... */
                if (uEdxGst & RT_BIT(8))   pHlp->pfnPrintf(pHlp, " ACE2");
                if (uEdxGst & RT_BIT(9))   pHlp->pfnPrintf(pHlp, " ACE2-E");
                if (uEdxGst & RT_BIT(10))  pHlp->pfnPrintf(pHlp, " PHE");
                if (uEdxGst & RT_BIT(11))  pHlp->pfnPrintf(pHlp, " PHE-E");
                if (uEdxGst & RT_BIT(12))  pHlp->pfnPrintf(pHlp, " PMM");
                if (uEdxGst & RT_BIT(13))  pHlp->pfnPrintf(pHlp, " PMM-E");
                for (unsigned iBit = 14; iBit < 32; iBit++)
                    if (uEdxGst & RT_BIT(iBit))
                        pHlp->pfnPrintf(pHlp, " %d", iBit);
                pHlp->pfnPrintf(pHlp, "\n");
            }
            else
            {
                pHlp->pfnPrintf(pHlp, "Mnemonic - Description                 = guest (host)\n");
                pHlp->pfnPrintf(pHlp, "AIS - Alternate Instruction Set        = %d (%d)\n",  !!(uEdxGst & RT_BIT( 0)),  !!(uEdxHst & RT_BIT( 0)));
                pHlp->pfnPrintf(pHlp, "AIS-E - AIS enabled                    = %d (%d)\n",  !!(uEdxGst & RT_BIT( 1)),  !!(uEdxHst & RT_BIT( 1)));
                pHlp->pfnPrintf(pHlp, "RNG - Random Number Generator          = %d (%d)\n",  !!(uEdxGst & RT_BIT( 2)),  !!(uEdxHst & RT_BIT( 2)));
                pHlp->pfnPrintf(pHlp, "RNG-E - RNG enabled                    = %d (%d)\n",  !!(uEdxGst & RT_BIT( 3)),  !!(uEdxHst & RT_BIT( 3)));
                pHlp->pfnPrintf(pHlp, "LH - LongHaul MSR 0000_110Ah           = %d (%d)\n",  !!(uEdxGst & RT_BIT( 4)),  !!(uEdxHst & RT_BIT( 4)));
                pHlp->pfnPrintf(pHlp, "FEMMS - FEMMS                          = %d (%d)\n",  !!(uEdxGst & RT_BIT( 5)),  !!(uEdxHst & RT_BIT( 5)));
                pHlp->pfnPrintf(pHlp, "ACE - Advanced Cryptography Engine     = %d (%d)\n",  !!(uEdxGst & RT_BIT( 6)),  !!(uEdxHst & RT_BIT( 6)));
                pHlp->pfnPrintf(pHlp, "ACE-E - ACE enabled                    = %d (%d)\n",  !!(uEdxGst & RT_BIT( 7)),  !!(uEdxHst & RT_BIT( 7)));
                /* possibly indicating MM/HE and MM/HE-E on older chips... */
                pHlp->pfnPrintf(pHlp, "ACE2 - Advanced Cryptography Engine 2  = %d (%d)\n",  !!(uEdxGst & RT_BIT( 8)),  !!(uEdxHst & RT_BIT( 8)));
                pHlp->pfnPrintf(pHlp, "ACE2-E - ACE enabled                   = %d (%d)\n",  !!(uEdxGst & RT_BIT( 9)),  !!(uEdxHst & RT_BIT( 9)));
                pHlp->pfnPrintf(pHlp, "PHE - Padlock Hash Engine              = %d (%d)\n",  !!(uEdxGst & RT_BIT(10)),  !!(uEdxHst & RT_BIT(10)));
                pHlp->pfnPrintf(pHlp, "PHE-E - PHE enabled                    = %d (%d)\n",  !!(uEdxGst & RT_BIT(11)),  !!(uEdxHst & RT_BIT(11)));
                pHlp->pfnPrintf(pHlp, "PMM - Montgomery Multiplier            = %d (%d)\n",  !!(uEdxGst & RT_BIT(12)),  !!(uEdxHst & RT_BIT(12)));
                pHlp->pfnPrintf(pHlp, "PMM-E - PMM enabled                    = %d (%d)\n",  !!(uEdxGst & RT_BIT(13)),  !!(uEdxHst & RT_BIT(13)));
                pHlp->pfnPrintf(pHlp, "14 - Reserved                          = %d (%d)\n",  !!(uEdxGst & RT_BIT(14)),  !!(uEdxHst & RT_BIT(14)));
                pHlp->pfnPrintf(pHlp, "15 - Reserved                          = %d (%d)\n",  !!(uEdxGst & RT_BIT(15)),  !!(uEdxHst & RT_BIT(15)));
                pHlp->pfnPrintf(pHlp, "Parallax                               = %d (%d)\n",  !!(uEdxGst & RT_BIT(16)),  !!(uEdxHst & RT_BIT(16)));
                pHlp->pfnPrintf(pHlp, "Parallax enabled                       = %d (%d)\n",  !!(uEdxGst & RT_BIT(17)),  !!(uEdxHst & RT_BIT(17)));
                pHlp->pfnPrintf(pHlp, "Overstress                             = %d (%d)\n",  !!(uEdxGst & RT_BIT(18)),  !!(uEdxHst & RT_BIT(18)));
                pHlp->pfnPrintf(pHlp, "Overstress enabled                     = %d (%d)\n",  !!(uEdxGst & RT_BIT(19)),  !!(uEdxHst & RT_BIT(19)));
                pHlp->pfnPrintf(pHlp, "TM3 - Temperature Monitoring 3         = %d (%d)\n",  !!(uEdxGst & RT_BIT(20)),  !!(uEdxHst & RT_BIT(20)));
                pHlp->pfnPrintf(pHlp, "TM3-E - TM3 enabled                    = %d (%d)\n",  !!(uEdxGst & RT_BIT(21)),  !!(uEdxHst & RT_BIT(21)));
                pHlp->pfnPrintf(pHlp, "RNG2 - Random Number Generator 2       = %d (%d)\n",  !!(uEdxGst & RT_BIT(22)),  !!(uEdxHst & RT_BIT(22)));
                pHlp->pfnPrintf(pHlp, "RNG2-E - RNG2 enabled                  = %d (%d)\n",  !!(uEdxGst & RT_BIT(23)),  !!(uEdxHst & RT_BIT(23)));
                pHlp->pfnPrintf(pHlp, "24 - Reserved                          = %d (%d)\n",  !!(uEdxGst & RT_BIT(24)),  !!(uEdxHst & RT_BIT(24)));
                pHlp->pfnPrintf(pHlp, "PHE2 - Padlock Hash Engine 2           = %d (%d)\n",  !!(uEdxGst & RT_BIT(25)),  !!(uEdxHst & RT_BIT(25)));
                pHlp->pfnPrintf(pHlp, "PHE2-E - PHE2 enabled                  = %d (%d)\n",  !!(uEdxGst & RT_BIT(26)),  !!(uEdxHst & RT_BIT(26)));
                for (unsigned iBit = 27; iBit < 32; iBit++)
                    if ((uEdxGst | uEdxHst) & RT_BIT(iBit))
                        pHlp->pfnPrintf(pHlp, "Bit %d                                 = %d (%d)\n", iBit, !!(uEdxGst & RT_BIT(iBit)), !!(uEdxHst & RT_BIT(iBit)));
                pHlp->pfnPrintf(pHlp, "\n");
            }
        }

        pCurLeaf = pNextLeaf;
    }

    /*
     * The remainder.
     */
    pCurLeaf = cpumR3CpuIdInfoRawRange(pHlp, paLeaves, cLeaves, pCurLeaf, UINT32_C(0xffffffff), "Unknown CPUID Leaves");
}

#endif /* !IN_VBOX_CPU_REPORT */

