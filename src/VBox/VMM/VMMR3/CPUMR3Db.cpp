/* $Id: CPUMR3Db.cpp 107963 2025-01-20 16:26:58Z knut.osmundsen@oracle.com $ */
/** @file
 * CPUM - CPU database part.
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
#include "CPUMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/mm.h>

#include <VBox/err.h>
#if !defined(RT_ARCH_ARM64)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/mem.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def NULL_ALONE
 * For eliminating an unnecessary data dependency in standalone builds (for
 * VBoxSVC). */
/** @def ZERO_ALONE
 * For eliminating an unnecessary data size dependency in standalone builds (for
 * VBoxSVC). */
#ifndef CPUM_DB_STANDALONE
# define NULL_ALONE(a_aTable)    a_aTable
# define ZERO_ALONE(a_cTable)    a_cTable
#else
# define NULL_ALONE(a_aTable)    NULL
# define ZERO_ALONE(a_cTable)    0
#endif


/** @name Short macros for the MSR range entries.
 *
 * These are rather cryptic, but this is to reduce the attack on the right
 * margin.
 *
 * @{ */
/** Alias one MSR onto another (a_uTarget). */
#define MAL(a_uMsr, a_szName, a_uTarget) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_MsrAlias, kCpumMsrWrFn_MsrAlias, 0, a_uTarget, 0, 0, a_szName)
/** Functions handles everything. */
#define MFN(a_uMsr, a_szName, a_enmRdFnSuff, a_enmWrFnSuff) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_##a_enmRdFnSuff, kCpumMsrWrFn_##a_enmWrFnSuff, 0, 0, 0, 0, a_szName)
/** Functions handles everything, with GP mask. */
#define MFG(a_uMsr, a_szName, a_enmRdFnSuff, a_enmWrFnSuff, a_fWrGpMask) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_##a_enmRdFnSuff, kCpumMsrWrFn_##a_enmWrFnSuff, 0, 0, 0, a_fWrGpMask, a_szName)
/** Function handlers, read-only. */
#define MFO(a_uMsr, a_szName, a_enmRdFnSuff) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_##a_enmRdFnSuff, kCpumMsrWrFn_ReadOnly, 0, 0, 0, UINT64_MAX, a_szName)
/** Function handlers, ignore all writes. */
#define MFI(a_uMsr, a_szName, a_enmRdFnSuff) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_##a_enmRdFnSuff, kCpumMsrWrFn_IgnoreWrite, 0, 0, UINT64_MAX, 0, a_szName)
/** Function handlers, with value. */
#define MFV(a_uMsr, a_szName, a_enmRdFnSuff, a_enmWrFnSuff, a_uValue) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_##a_enmRdFnSuff, kCpumMsrWrFn_##a_enmWrFnSuff, 0, a_uValue, 0, 0, a_szName)
/** Function handlers, with write ignore mask. */
#define MFW(a_uMsr, a_szName, a_enmRdFnSuff, a_enmWrFnSuff, a_fWrIgnMask) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_##a_enmRdFnSuff, kCpumMsrWrFn_##a_enmWrFnSuff, 0, 0, a_fWrIgnMask, 0, a_szName)
/** Function handlers, extended version. */
#define MFX(a_uMsr, a_szName, a_enmRdFnSuff, a_enmWrFnSuff, a_uValue, a_fWrIgnMask, a_fWrGpMask) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_##a_enmRdFnSuff, kCpumMsrWrFn_##a_enmWrFnSuff, 0, a_uValue, a_fWrIgnMask, a_fWrGpMask, a_szName)
/** Function handlers, with CPUMCPU storage variable. */
#define MFS(a_uMsr, a_szName, a_enmRdFnSuff, a_enmWrFnSuff, a_CpumCpuMember) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_##a_enmRdFnSuff, kCpumMsrWrFn_##a_enmWrFnSuff, \
         RT_OFFSETOF(CPUMCPU, a_CpumCpuMember), 0, 0, 0, a_szName)
/** Function handlers, with CPUMCPU storage variable, ignore mask and GP mask. */
#define MFZ(a_uMsr, a_szName, a_enmRdFnSuff, a_enmWrFnSuff, a_CpumCpuMember, a_fWrIgnMask, a_fWrGpMask) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_##a_enmRdFnSuff, kCpumMsrWrFn_##a_enmWrFnSuff, \
         RT_OFFSETOF(CPUMCPU, a_CpumCpuMember), 0, a_fWrIgnMask, a_fWrGpMask, a_szName)
/** Read-only fixed value. */
#define MVO(a_uMsr, a_szName, a_uValue) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_FixedValue, kCpumMsrWrFn_ReadOnly, 0, a_uValue, 0, UINT64_MAX, a_szName)
/** Read-only fixed value, ignores all writes. */
#define MVI(a_uMsr, a_szName, a_uValue) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_FixedValue, kCpumMsrWrFn_IgnoreWrite, 0, a_uValue, UINT64_MAX, 0, a_szName)
/** Read fixed value, ignore writes outside GP mask. */
#define MVG(a_uMsr, a_szName, a_uValue, a_fWrGpMask) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_FixedValue, kCpumMsrWrFn_IgnoreWrite, 0, a_uValue, 0, a_fWrGpMask, a_szName)
/** Read fixed value, extended version with both GP and ignore masks. */
#define MVX(a_uMsr, a_szName, a_uValue, a_fWrIgnMask, a_fWrGpMask) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_FixedValue, kCpumMsrWrFn_IgnoreWrite, 0, a_uValue, a_fWrIgnMask, a_fWrGpMask, a_szName)
/** The short form, no CPUM backing. */
#define MSN(a_uMsr, a_szName, a_enmRdFnSuff, a_enmWrFnSuff, a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask) \
    RINT(a_uMsr, a_uMsr, kCpumMsrRdFn_##a_enmRdFnSuff, kCpumMsrWrFn_##a_enmWrFnSuff, 0, \
         a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask, a_szName)

/** Range: Functions handles everything. */
#define RFN(a_uFirst, a_uLast, a_szName, a_enmRdFnSuff, a_enmWrFnSuff) \
    RINT(a_uFirst, a_uLast, kCpumMsrRdFn_##a_enmRdFnSuff, kCpumMsrWrFn_##a_enmWrFnSuff, 0, 0, 0, 0, a_szName)
/** Range: Read fixed value, read-only. */
#define RVO(a_uFirst, a_uLast, a_szName, a_uValue) \
    RINT(a_uFirst, a_uLast, kCpumMsrRdFn_FixedValue, kCpumMsrWrFn_ReadOnly, 0, a_uValue, 0, UINT64_MAX, a_szName)
/** Range: Read fixed value, ignore writes. */
#define RVI(a_uFirst, a_uLast, a_szName, a_uValue) \
    RINT(a_uFirst, a_uLast, kCpumMsrRdFn_FixedValue, kCpumMsrWrFn_IgnoreWrite, 0, a_uValue, UINT64_MAX, 0, a_szName)
/** Range: The short form, no CPUM backing. */
#define RSN(a_uFirst, a_uLast, a_szName, a_enmRdFnSuff, a_enmWrFnSuff, a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask) \
    RINT(a_uFirst, a_uLast, kCpumMsrRdFn_##a_enmRdFnSuff, kCpumMsrWrFn_##a_enmWrFnSuff, 0, \
         a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask, a_szName)

/** Internal form used by the macros. */
#ifdef VBOX_WITH_STATISTICS
# define RINT(a_uFirst, a_uLast, a_enmRdFn, a_enmWrFn, a_offCpumCpu, a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask, a_szName) \
    { a_uFirst, a_uLast, a_enmRdFn, a_enmWrFn, a_offCpumCpu, 0, a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask, a_szName, \
      { 0 }, { 0 }, { 0 }, { 0 } }
#else
# define RINT(a_uFirst, a_uLast, a_enmRdFn, a_enmWrFn, a_offCpumCpu, a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask, a_szName) \
    { a_uFirst, a_uLast, a_enmRdFn, a_enmWrFn, a_offCpumCpu, 0, a_uInitOrReadValue, a_fWrIgnMask, a_fWrGpMask, a_szName }
#endif
/** @} */

#ifndef CPUM_DB_STANDALONE

#include "cpus/Intel_Core_i7_6700K.h"
#include "cpus/Intel_Core_i7_5600U.h"
#include "cpus/Intel_Core_i7_3960X.h"
#include "cpus/Intel_Core_i5_3570.h"
#include "cpus/Intel_Core_i7_2635QM.h"
#include "cpus/Intel_Xeon_X5482_3_20GHz.h"
#include "cpus/Intel_Core2_X6800_2_93GHz.h"
#include "cpus/Intel_Core2_T7600_2_33GHz.h"
#include "cpus/Intel_Core_Duo_T2600_2_16GHz.h"
#include "cpus/Intel_Pentium_M_processor_2_00GHz.h"
#include "cpus/Intel_Pentium_4_3_00GHz.h"
#include "cpus/Intel_Pentium_N3530_2_16GHz.h"
#include "cpus/Intel_Atom_330_1_60GHz.h"
#include "cpus/Intel_80486.h"
#include "cpus/Intel_80386.h"
#include "cpus/Intel_80286.h"
#include "cpus/Intel_80186.h"
#include "cpus/Intel_8086.h"

#include "cpus/AMD_Ryzen_7_1800X_Eight_Core.h"
#include "cpus/AMD_FX_8150_Eight_Core.h"
#include "cpus/AMD_Phenom_II_X6_1100T.h"
#include "cpus/Quad_Core_AMD_Opteron_2384.h"
#include "cpus/AMD_Athlon_64_X2_Dual_Core_4200.h"
#include "cpus/AMD_Athlon_64_3200.h"

#include "cpus/VIA_QuadCore_L4700_1_2_GHz.h"

#include "cpus/ZHAOXIN_KaiXian_KX_U5581_1_8GHz.h"

#include "cpus/Hygon_C86_7185_32_core.h"


/**
 * The database entries.
 *
 * 1. The first entry is special.  It is the fallback for unknown
 *    processors.  Thus, it better be pretty representative.
 *
 * 2. The first entry for a CPU vendor is likewise important as it is
 *    the default entry for that vendor.
 *
 * Generally we put the most recent CPUs first, since these tend to have the
 * most complicated and backwards compatible list of MSRs.
 */
static CPUMDBENTRY const * const g_apCpumDbEntries[] =
{
#ifdef VBOX_CPUDB_Intel_Core_i7_6700K_h
    &g_Entry_Intel_Core_i7_6700K,
#endif
#ifdef VBOX_CPUDB_Intel_Core_i7_5600U_h
    &g_Entry_Intel_Core_i7_5600U,
#endif
#ifdef VBOX_CPUDB_Intel_Core_i5_3570_h
    &g_Entry_Intel_Core_i5_3570,
#endif
#ifdef VBOX_CPUDB_Intel_Core_i7_3960X_h
    &g_Entry_Intel_Core_i7_3960X,
#endif
#ifdef VBOX_CPUDB_Intel_Core_i7_2635QM_h
    &g_Entry_Intel_Core_i7_2635QM,
#endif
#ifdef VBOX_CPUDB_Intel_Pentium_N3530_2_16GHz_h
    &g_Entry_Intel_Pentium_N3530_2_16GHz,
#endif
#ifdef VBOX_CPUDB_Intel_Atom_330_1_60GHz_h
    &g_Entry_Intel_Atom_330_1_60GHz,
#endif
#ifdef VBOX_CPUDB_Intel_Pentium_M_processor_2_00GHz_h
    &g_Entry_Intel_Pentium_M_processor_2_00GHz,
#endif
#ifdef VBOX_CPUDB_Intel_Xeon_X5482_3_20GHz_h
    &g_Entry_Intel_Xeon_X5482_3_20GHz,
#endif
#ifdef VBOX_CPUDB_Intel_Core2_X6800_2_93GHz_h
    &g_Entry_Intel_Core2_X6800_2_93GHz,
#endif
#ifdef VBOX_CPUDB_Intel_Core2_T7600_2_33GHz_h
    &g_Entry_Intel_Core2_T7600_2_33GHz,
#endif
#ifdef VBOX_CPUDB_Intel_Core_Duo_T2600_2_16GHz_h
    &g_Entry_Intel_Core_Duo_T2600_2_16GHz,
#endif
#ifdef VBOX_CPUDB_Intel_Pentium_4_3_00GHz_h
    &g_Entry_Intel_Pentium_4_3_00GHz,
#endif
/** @todo pentium, pentium mmx, pentium pro, pentium II, pentium III */
#ifdef VBOX_CPUDB_Intel_80486_h
    &g_Entry_Intel_80486,
#endif
#ifdef VBOX_CPUDB_Intel_80386_h
    &g_Entry_Intel_80386,
#endif
#ifdef VBOX_CPUDB_Intel_80286_h
    &g_Entry_Intel_80286,
#endif
#ifdef VBOX_CPUDB_Intel_80186_h
    &g_Entry_Intel_80186,
#endif
#ifdef VBOX_CPUDB_Intel_8086_h
    &g_Entry_Intel_8086,
#endif

#ifdef VBOX_CPUDB_AMD_Ryzen_7_1800X_Eight_Core_h
    &g_Entry_AMD_Ryzen_7_1800X_Eight_Core,
#endif
#ifdef VBOX_CPUDB_AMD_FX_8150_Eight_Core_h
    &g_Entry_AMD_FX_8150_Eight_Core,
#endif
#ifdef VBOX_CPUDB_AMD_Phenom_II_X6_1100T_h
    &g_Entry_AMD_Phenom_II_X6_1100T,
#endif
#ifdef VBOX_CPUDB_Quad_Core_AMD_Opteron_2384_h
    &g_Entry_Quad_Core_AMD_Opteron_2384,
#endif
#ifdef VBOX_CPUDB_AMD_Athlon_64_X2_Dual_Core_4200_h
    &g_Entry_AMD_Athlon_64_X2_Dual_Core_4200,
#endif
#ifdef VBOX_CPUDB_AMD_Athlon_64_3200_h
    &g_Entry_AMD_Athlon_64_3200,
#endif

#ifdef VBOX_CPUDB_ZHAOXIN_KaiXian_KX_U5581_1_8GHz_h
    &g_Entry_ZHAOXIN_KaiXian_KX_U5581_1_8GHz,
#endif

#ifdef VBOX_CPUDB_VIA_QuadCore_L4700_1_2_GHz_h
    &g_Entry_VIA_QuadCore_L4700_1_2_GHz,
#endif

#ifdef VBOX_CPUDB_NEC_V20_h
    &g_Entry_NEC_V20,
#endif

#ifdef VBOX_CPUDB_Hygon_C86_7185_32_core_h
    &g_Entry_Hygon_C86_7185_32_core,
#endif
};


/**
 * Returns the number of entries in the CPU database.
 *
 * @returns Number of entries.
 * @sa      PFNCPUMDBGETENTRIES
 */
VMMR3DECL(uint32_t)         CPUMR3DbGetEntries(void)
{
    return RT_ELEMENTS(g_apCpumDbEntries);
}


/**
 * Returns CPU database entry for the given index.
 *
 * @returns Pointer the CPU database entry, NULL if index is out of bounds.
 * @param   idxCpuDb            The index (0..CPUMR3DbGetEntries).
 * @sa      PFNCPUMDBGETENTRYBYINDEX
 */
VMMR3DECL(PCCPUMDBENTRY)    CPUMR3DbGetEntryByIndex(uint32_t idxCpuDb)
{
    AssertReturn(idxCpuDb < RT_ELEMENTS(g_apCpumDbEntries), NULL);
    return g_apCpumDbEntries[idxCpuDb];
}


/**
 * Returns CPU database entry with the given name.
 *
 * @returns Pointer the CPU database entry, NULL if not found.
 * @param   pszName             The name of the profile to return.
 * @sa      PFNCPUMDBGETENTRYBYNAME
 */
VMMR3DECL(PCCPUMDBENTRY)    CPUMR3DbGetEntryByName(const char *pszName)
{
    AssertPtrReturn(pszName, NULL);
    AssertReturn(*pszName, NULL);
    for (size_t i = 0; i < RT_ELEMENTS(g_apCpumDbEntries); i++)
        if (strcmp(g_apCpumDbEntries[i]->pszName, pszName) == 0)
            return g_apCpumDbEntries[i];
    return NULL;
}



/**
 * Binary search used by cpumR3MsrRangesInsert and has some special properties
 * wrt to mismatches.
 *
 * @returns Insert location.
 * @param   paMsrRanges         The MSR ranges to search.
 * @param   cMsrRanges          The number of MSR ranges.
 * @param   uMsr                What to search for.
 */
static uint32_t cpumR3MsrRangesBinSearch(PCCPUMMSRRANGE paMsrRanges, uint32_t cMsrRanges, uint32_t uMsr)
{
    if (!cMsrRanges)
        return 0;

    uint32_t iStart = 0;
    uint32_t iLast  = cMsrRanges - 1;
    for (;;)
    {
        uint32_t i = iStart + (iLast - iStart + 1) / 2;
        if (   uMsr >= paMsrRanges[i].uFirst
            && uMsr <= paMsrRanges[i].uLast)
            return i;
        if (uMsr < paMsrRanges[i].uFirst)
        {
            if (i <= iStart)
                return i;
            iLast = i - 1;
        }
        else
        {
            if (i >= iLast)
            {
                if (i < cMsrRanges)
                    i++;
                return i;
            }
            iStart = i + 1;
        }
    }
}


/**
 * Ensures that there is space for at least @a cNewRanges in the table,
 * reallocating the table if necessary.
 *
 * @returns Pointer to the MSR ranges on success, NULL on failure.  On failure
 *          @a *ppaMsrRanges is freed and set to NULL.
 * @param   pVM             The cross context VM structure.  If NULL,
 *                          use the process heap, otherwise the VM's hyper heap.
 * @param   ppaMsrRanges    The variable pointing to the ranges (input/output).
 * @param   cMsrRanges      The current number of ranges.
 * @param   cNewRanges      The number of ranges to be added.
 */
static PCPUMMSRRANGE cpumR3MsrRangesEnsureSpace(PVM pVM, PCPUMMSRRANGE *ppaMsrRanges, uint32_t cMsrRanges, uint32_t cNewRanges)
{
    if (  cMsrRanges + cNewRanges
        > RT_ELEMENTS(pVM->cpum.s.GuestInfo.aMsrRanges) + (pVM ? 0 : 128 /* Catch too many MSRs in CPU reporter! */))
    {
        LogRel(("CPUM: Too many MSR ranges! %#x, max %#x\n",
                cMsrRanges + cNewRanges, RT_ELEMENTS(pVM->cpum.s.GuestInfo.aMsrRanges)));
        return NULL;
    }
    if (pVM)
    {
        Assert(cMsrRanges == pVM->cpum.s.GuestInfo.cMsrRanges);
        Assert(*ppaMsrRanges == pVM->cpum.s.GuestInfo.aMsrRanges);
    }
    else
    {
        if (cMsrRanges + cNewRanges > RT_ALIGN_32(cMsrRanges, 16))
        {

            uint32_t const cNew = RT_ALIGN_32(cMsrRanges + cNewRanges, 16);
            void *pvNew = RTMemRealloc(*ppaMsrRanges, cNew * sizeof(**ppaMsrRanges));
            if (pvNew)
                *ppaMsrRanges = (PCPUMMSRRANGE)pvNew;
            else
            {
                RTMemFree(*ppaMsrRanges);
                *ppaMsrRanges = NULL;
                return NULL;
            }
        }
    }

    return *ppaMsrRanges;
}


/**
 * Inserts a new MSR range in into an sorted MSR range array.
 *
 * If the new MSR range overlaps existing ranges, the existing ones will be
 * adjusted/removed to fit in the new one.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_MEMORY
 *
 * @param   pVM             The cross context VM structure.  If NULL,
 *                          use the process heap, otherwise the VM's hyper heap.
 * @param   ppaMsrRanges    The variable pointing to the ranges (input/output).
 *                          Must be NULL if using the hyper heap.
 * @param   pcMsrRanges     The variable holding number of ranges. Must be NULL
 *                          if using the hyper heap.
 * @param   pNewRange       The new range.
 */
int cpumR3MsrRangesInsert(PVM pVM, PCPUMMSRRANGE *ppaMsrRanges, uint32_t *pcMsrRanges, PCCPUMMSRRANGE pNewRange)
{
    Assert(pNewRange->uLast >= pNewRange->uFirst);
    Assert(pNewRange->enmRdFn > kCpumMsrRdFn_Invalid && pNewRange->enmRdFn < kCpumMsrRdFn_End);
    Assert(pNewRange->enmWrFn > kCpumMsrWrFn_Invalid && pNewRange->enmWrFn < kCpumMsrWrFn_End);

    /*
     * Validate and use the VM's MSR ranges array if we are using the hyper heap.
     */
    if (pVM)
    {
        AssertReturn(!ppaMsrRanges, VERR_INVALID_PARAMETER);
        AssertReturn(!pcMsrRanges,  VERR_INVALID_PARAMETER);
        AssertReturn(pVM->cpum.s.GuestInfo.paMsrRangesR3 == pVM->cpum.s.GuestInfo.aMsrRanges, VERR_INTERNAL_ERROR_3);

        ppaMsrRanges = &pVM->cpum.s.GuestInfo.paMsrRangesR3;
        pcMsrRanges  = &pVM->cpum.s.GuestInfo.cMsrRanges;
    }
    else
    {
        AssertReturn(ppaMsrRanges, VERR_INVALID_POINTER);
        AssertReturn(pcMsrRanges, VERR_INVALID_POINTER);
    }

    uint32_t        cMsrRanges  = *pcMsrRanges;
    PCPUMMSRRANGE   paMsrRanges = *ppaMsrRanges;

    /*
     * Optimize the linear insertion case where we add new entries at the end.
     */
    if (   cMsrRanges > 0
        && paMsrRanges[cMsrRanges - 1].uLast < pNewRange->uFirst)
    {
        paMsrRanges = cpumR3MsrRangesEnsureSpace(pVM, ppaMsrRanges, cMsrRanges, 1);
        if (!paMsrRanges)
            return VERR_NO_MEMORY;
        paMsrRanges[cMsrRanges] = *pNewRange;
        *pcMsrRanges += 1;
    }
    else
    {
        uint32_t i = cpumR3MsrRangesBinSearch(paMsrRanges, cMsrRanges, pNewRange->uFirst);
        Assert(i == cMsrRanges || pNewRange->uFirst <= paMsrRanges[i].uLast);
        Assert(i == 0 || pNewRange->uFirst > paMsrRanges[i - 1].uLast);

        /*
         * Adding an entirely new entry?
         */
        if (   i >= cMsrRanges
            || pNewRange->uLast < paMsrRanges[i].uFirst)
        {
            paMsrRanges = cpumR3MsrRangesEnsureSpace(pVM, ppaMsrRanges, cMsrRanges, 1);
            if (!paMsrRanges)
                return VERR_NO_MEMORY;
            if (i < cMsrRanges)
                memmove(&paMsrRanges[i + 1], &paMsrRanges[i], (cMsrRanges - i) * sizeof(paMsrRanges[0]));
            paMsrRanges[i] = *pNewRange;
            *pcMsrRanges += 1;
        }
        /*
         * Replace existing entry?
         */
        else if (   pNewRange->uFirst == paMsrRanges[i].uFirst
                 && pNewRange->uLast  == paMsrRanges[i].uLast)
            paMsrRanges[i] = *pNewRange;
        /*
         * Splitting an existing entry?
         */
        else if (   pNewRange->uFirst > paMsrRanges[i].uFirst
                 && pNewRange->uLast  < paMsrRanges[i].uLast)
        {
            paMsrRanges = cpumR3MsrRangesEnsureSpace(pVM, ppaMsrRanges, cMsrRanges, 2);
            if (!paMsrRanges)
                return VERR_NO_MEMORY;
            if (i < cMsrRanges)
                memmove(&paMsrRanges[i + 2], &paMsrRanges[i], (cMsrRanges - i) * sizeof(paMsrRanges[0]));
            paMsrRanges[i + 1] = *pNewRange;
            paMsrRanges[i + 2] = paMsrRanges[i];
            paMsrRanges[i    ].uLast  = pNewRange->uFirst - 1;
            paMsrRanges[i + 2].uFirst = pNewRange->uLast  + 1;
            *pcMsrRanges += 2;
        }
        /*
         * Complicated scenarios that can affect more than one range.
         *
         * The current code does not optimize memmove calls when replacing
         * one or more existing ranges, because it's tedious to deal with and
         * not expected to be a frequent usage scenario.
         */
        else
        {
            /* Adjust start of first match? */
            if (   pNewRange->uFirst <= paMsrRanges[i].uFirst
                && pNewRange->uLast  <  paMsrRanges[i].uLast)
                paMsrRanges[i].uFirst = pNewRange->uLast + 1;
            else
            {
                /* Adjust end of first match? */
                if (pNewRange->uFirst > paMsrRanges[i].uFirst)
                {
                    Assert(paMsrRanges[i].uLast >= pNewRange->uFirst);
                    paMsrRanges[i].uLast = pNewRange->uFirst - 1;
                    i++;
                }
                /* Replace the whole first match (lazy bird). */
                else
                {
                    if (i + 1 < cMsrRanges)
                        memmove(&paMsrRanges[i], &paMsrRanges[i + 1], (cMsrRanges - i - 1) * sizeof(paMsrRanges[0]));
                    cMsrRanges = *pcMsrRanges -= 1;
                }

                /* Do the new range affect more ranges? */
                while (   i < cMsrRanges
                       && pNewRange->uLast >= paMsrRanges[i].uFirst)
                {
                    if (pNewRange->uLast < paMsrRanges[i].uLast)
                    {
                        /* Adjust the start of it, then we're done. */
                        paMsrRanges[i].uFirst = pNewRange->uLast + 1;
                        break;
                    }

                    /* Remove it entirely. */
                    if (i + 1 < cMsrRanges)
                        memmove(&paMsrRanges[i], &paMsrRanges[i + 1], (cMsrRanges - i - 1) * sizeof(paMsrRanges[0]));
                    cMsrRanges = *pcMsrRanges -= 1;
                }
            }

            /* Now, perform a normal insertion. */
            paMsrRanges = cpumR3MsrRangesEnsureSpace(pVM, ppaMsrRanges, cMsrRanges, 1);
            if (!paMsrRanges)
                return VERR_NO_MEMORY;
            if (i < cMsrRanges)
                memmove(&paMsrRanges[i + 1], &paMsrRanges[i], (cMsrRanges - i) * sizeof(paMsrRanges[0]));
            paMsrRanges[i] = *pNewRange;
            *pcMsrRanges += 1;
        }
    }

    return VINF_SUCCESS;
}


/**
 * Reconciles CPUID info with MSRs (selected ones).
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   fForceFlushCmd      Make sure MSR_IA32_FLUSH_CMD is present.
 * @param   fForceSpecCtrl      Make sure MSR_IA32_SPEC_CTRL is present.
 */
DECLHIDDEN(int) cpumR3MsrReconcileWithCpuId(PVM pVM, bool fForceFlushCmd, bool fForceSpecCtrl)
{
    PCCPUMMSRRANGE apToAdd[10];
    uint32_t       cToAdd = 0;

    /*
     * The IA32_FLUSH_CMD MSR was introduced in MCUs for CVS-2018-3646 and associates.
     */
    if (   pVM->cpum.s.GuestFeatures.fFlushCmd
        || fForceFlushCmd)
    {
        static CPUMMSRRANGE const s_FlushCmd =
        {
            /*.uFirst =*/       MSR_IA32_FLUSH_CMD,
            /*.uLast =*/        MSR_IA32_FLUSH_CMD,
            /*.enmRdFn =*/      kCpumMsrRdFn_WriteOnly,
            /*.enmWrFn =*/      kCpumMsrWrFn_Ia32FlushCmd,
            /*.offCpumCpu =*/   UINT16_MAX,
            /*.fReserved =*/    0,
            /*.uValue =*/       0,
            /*.fWrIgnMask =*/   0,
            /*.fWrGpMask =*/    ~MSR_IA32_FLUSH_CMD_F_L1D,
            /*.szName = */      "IA32_FLUSH_CMD"
        };
        apToAdd[cToAdd++] = &s_FlushCmd;
    }

    /*
     * The IA32_PRED_CMD MSR was introduced in MCUs for CVS-2018-3646 and associates.
     */
    if (   pVM->cpum.s.GuestFeatures.fIbpb
        /** @todo || pVM->cpum.s.GuestFeatures.fSbpb*/)
    {
        static CPUMMSRRANGE const s_PredCmd =
        {
            /*.uFirst =*/       MSR_IA32_PRED_CMD,
            /*.uLast =*/        MSR_IA32_PRED_CMD,
            /*.enmRdFn =*/      kCpumMsrRdFn_WriteOnly,
            /*.enmWrFn =*/      kCpumMsrWrFn_Ia32PredCmd,
            /*.offCpumCpu =*/   UINT16_MAX,
            /*.fReserved =*/    0,
            /*.uValue =*/       0,
            /*.fWrIgnMask =*/   0,
            /*.fWrGpMask =*/    ~MSR_IA32_PRED_CMD_F_IBPB,
            /*.szName = */      "IA32_PRED_CMD"
        };
        apToAdd[cToAdd++] = &s_PredCmd;
    }

    /*
     * The IA32_SPEC_CTRL MSR was introduced in MCUs for CVS-2018-3646 and associates.
     */
    if (   pVM->cpum.s.GuestFeatures.fSpecCtrlMsr
        || fForceSpecCtrl)
    {
        static CPUMMSRRANGE const s_SpecCtrl =
        {
            /*.uFirst =*/       MSR_IA32_SPEC_CTRL,
            /*.uLast =*/        MSR_IA32_SPEC_CTRL,
            /*.enmRdFn =*/      kCpumMsrRdFn_Ia32SpecCtrl,
            /*.enmWrFn =*/      kCpumMsrWrFn_Ia32SpecCtrl,
            /*.offCpumCpu =*/   UINT16_MAX,
            /*.fReserved =*/    0,
            /*.uValue =*/       0,
            /*.fWrIgnMask =*/   0,
            /*.fWrGpMask =*/    0,
            /*.szName = */      "IA32_SPEC_CTRL"
        };
        apToAdd[cToAdd++] = &s_SpecCtrl;
    }

    /*
     * The MSR_IA32_ARCH_CAPABILITIES was introduced in various spectre MCUs, or at least
     * documented in relation to such.
     */
    if (pVM->cpum.s.GuestFeatures.fArchCap)
    {
        static CPUMMSRRANGE const s_ArchCaps =
        {
            /*.uFirst =*/       MSR_IA32_ARCH_CAPABILITIES,
            /*.uLast =*/        MSR_IA32_ARCH_CAPABILITIES,
            /*.enmRdFn =*/      kCpumMsrRdFn_Ia32ArchCapabilities,
            /*.enmWrFn =*/      kCpumMsrWrFn_ReadOnly,
            /*.offCpumCpu =*/   UINT16_MAX,
            /*.fReserved =*/    0,
            /*.uValue =*/       0,
            /*.fWrIgnMask =*/   0,
            /*.fWrGpMask =*/    UINT64_MAX,
            /*.szName = */      "IA32_ARCH_CAPABILITIES"
        };
        apToAdd[cToAdd++] = &s_ArchCaps;
    }

    /*
     * Do the adding.
     */
    Assert(cToAdd <= RT_ELEMENTS(apToAdd));
    for (uint32_t i = 0; i < cToAdd; i++)
    {
        PCCPUMMSRRANGE pRange = apToAdd[i];
        Assert(pRange->uFirst == pRange->uLast);
        if (!cpumLookupMsrRange(pVM, pRange->uFirst))
        {
            LogRel(("CPUM: MSR/CPUID reconciliation insert: %#010x %s\n", pRange->uFirst, pRange->szName));
            int rc = cpumR3MsrRangesInsert(NULL /* pVM */, &pVM->cpum.s.GuestInfo.paMsrRangesR3,
                                           &pVM->cpum.s.GuestInfo.cMsrRanges, pRange);
            AssertRCReturn(rc, rc);
        }
    }
    return VINF_SUCCESS;
}


/**
 * Worker for cpumR3MsrApplyFudge that applies one table.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   paRanges            Array of MSRs to fudge.
 * @param   cRanges             Number of MSRs in the array.
 */
static int cpumR3MsrApplyFudgeTable(PVM pVM, PCCPUMMSRRANGE paRanges, size_t cRanges)
{
    for (uint32_t i = 0; i < cRanges; i++)
        if (!cpumLookupMsrRange(pVM, paRanges[i].uFirst))
        {
            LogRel(("CPUM: MSR fudge: %#010x %s\n", paRanges[i].uFirst, paRanges[i].szName));
            int rc = cpumR3MsrRangesInsert(NULL /* pVM */, &pVM->cpum.s.GuestInfo.paMsrRangesR3, &pVM->cpum.s.GuestInfo.cMsrRanges,
                                           &paRanges[i]);
            if (RT_FAILURE(rc))
                return rc;
        }
    return VINF_SUCCESS;
}


/**
 * Fudges the MSRs that guest are known to access in some odd cases.
 *
 * A typical example is a VM that has been moved between different hosts where
 * for instance the cpu vendor differs.
 *
 * Another example is older CPU profiles (e.g. Atom Bonnet) for newer CPUs (e.g.
 * Atom Silvermont), where features reported thru CPUID aren't present in the
 * MSRs (e.g. AMD64_TSC_AUX).
 *
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 */
int cpumR3MsrApplyFudge(PVM pVM)
{
    /*
     * Basic.
     */
    static CPUMMSRRANGE const s_aFudgeMsrs[] =
    {
        MFO(0x00000000, "IA32_P5_MC_ADDR",          Ia32P5McAddr),
        MFX(0x00000001, "IA32_P5_MC_TYPE",          Ia32P5McType,   Ia32P5McType,   0, 0, UINT64_MAX),
        MVO(0x00000017, "IA32_PLATFORM_ID",         0),
        MFN(0x0000001b, "IA32_APIC_BASE",           Ia32ApicBase,   Ia32ApicBase),
        MVI(0x0000008b, "BIOS_SIGN",                0),
        MFX(0x000000fe, "IA32_MTRRCAP",             Ia32MtrrCap,    ReadOnly,       0x508, 0, 0),
        MFX(0x00000179, "IA32_MCG_CAP",             Ia32McgCap,     ReadOnly,       0x005, 0, 0),
        MFX(0x0000017a, "IA32_MCG_STATUS",          Ia32McgStatus,  Ia32McgStatus,  0, ~(uint64_t)UINT32_MAX, 0),
        MFN(0x000001a0, "IA32_MISC_ENABLE",         Ia32MiscEnable, Ia32MiscEnable),
        MFN(0x000001d9, "IA32_DEBUGCTL",            Ia32DebugCtl,   Ia32DebugCtl),
        MFO(0x000001db, "P6_LAST_BRANCH_FROM_IP",   P6LastBranchFromIp),
        MFO(0x000001dc, "P6_LAST_BRANCH_TO_IP",     P6LastBranchToIp),
        MFO(0x000001dd, "P6_LAST_INT_FROM_IP",      P6LastIntFromIp),
        MFO(0x000001de, "P6_LAST_INT_TO_IP",        P6LastIntToIp),
        MFS(0x00000277, "IA32_PAT",                 Ia32Pat, Ia32Pat, Guest.msrPAT),
        MFZ(0x000002ff, "IA32_MTRR_DEF_TYPE",       Ia32MtrrDefType, Ia32MtrrDefType, GuestMsrs.msr.MtrrDefType, 0, ~(uint64_t)0xc07),
        MFN(0x00000400, "IA32_MCi_CTL_STATUS_ADDR_MISC", Ia32McCtlStatusAddrMiscN, Ia32McCtlStatusAddrMiscN),
    };
    int rc = cpumR3MsrApplyFudgeTable(pVM, &s_aFudgeMsrs[0], RT_ELEMENTS(s_aFudgeMsrs));
    AssertLogRelRCReturn(rc, rc);

    /*
     * XP might mistake opterons and other newer CPUs for P4s.
     */
    if (pVM->cpum.s.GuestFeatures.uFamily >= 0xf)
    {
        static CPUMMSRRANGE const s_aP4FudgeMsrs[] =
        {
            MFX(0x0000002c, "P4_EBC_FREQUENCY_ID", IntelP4EbcFrequencyId, IntelP4EbcFrequencyId, 0xf12010f, UINT64_MAX, 0),
        };
        rc = cpumR3MsrApplyFudgeTable(pVM, &s_aP4FudgeMsrs[0], RT_ELEMENTS(s_aP4FudgeMsrs));
        AssertLogRelRCReturn(rc, rc);
    }

    if (pVM->cpum.s.GuestFeatures.fRdTscP)
    {
        static CPUMMSRRANGE const s_aRdTscPFudgeMsrs[] =
        {
            MFX(0xc0000103, "AMD64_TSC_AUX", Amd64TscAux, Amd64TscAux, 0, 0, ~(uint64_t)UINT32_MAX),
        };
        rc = cpumR3MsrApplyFudgeTable(pVM, &s_aRdTscPFudgeMsrs[0], RT_ELEMENTS(s_aRdTscPFudgeMsrs));
        AssertLogRelRCReturn(rc, rc);
    }

    /*
     * Windows 10 incorrectly writes to MSR_IA32_TSX_CTRL without checking
     * CPUID.ARCH_CAP(EAX=7h,ECX=0):EDX[bit 29] or the MSR feature bits in
     * MSR_IA32_ARCH_CAPABILITIES[bit 7], see @bugref{9630}.
     * Ignore writes to this MSR and return 0 on reads.
     *
     * Windows 11 24H2 incorrectly reads MSR_IA32_MCU_OPT_CTRL without
     * checking CPUID.ARCH_CAP(EAX=7h,ECX=0).EDX[bit 9] or the MSR feature
     * bits in MSR_IA32_ARCH_CAPABILITIES[bit 18], see @bugref{10794}.
     * Ignore wrties to this MSR and return 0 on reads.
     */
    if (pVM->cpum.s.GuestFeatures.fArchCap)
    {
        static CPUMMSRRANGE const s_aTsxCtrl[] =
        {
            MVI(MSR_IA32_TSX_CTRL, "IA32_TSX_CTRL", 0),
            MVI(MSR_IA32_MCU_OPT_CTRL, "IA32_MCU_OPT_CTRL", 0),
        };
        rc = cpumR3MsrApplyFudgeTable(pVM, &s_aTsxCtrl[0], RT_ELEMENTS(s_aTsxCtrl));
        AssertLogRelRCReturn(rc, rc);
    }

    return rc;
}

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)

/**
 * Do we consider @a enmConsider a better match for @a enmTarget than
 * @a enmFound?
 *
 * Only called when @a enmConsider isn't exactly what we're looking for.
 *
 * @returns true/false.
 * @param   enmConsider         The new microarch to consider.
 * @param   enmTarget           The target microarch.
 * @param   enmFound            The best microarch match we've found thus far.
 */
DECLINLINE(bool) cpumR3DbIsBetterMarchMatch(CPUMMICROARCH enmConsider, CPUMMICROARCH enmTarget, CPUMMICROARCH enmFound)
{
    Assert(enmConsider != enmTarget);

    /*
     * If we've got an march match, don't bother with enmConsider.
     */
    if (enmFound == enmTarget)
        return false;

    /*
     * Found is below: Pick 'consider' if it's closer to the target or above it.
     */
    if (enmFound < enmTarget)
        return enmConsider > enmFound;

    /*
     * Found is above: Pick 'consider' if it's also above (paranoia: or equal)
     *                 and but closer to the target.
     */
    return enmConsider >= enmTarget && enmConsider < enmFound;
}


/**
 * Do we consider @a enmConsider a better match for @a enmTarget than
 * @a enmFound?
 *
 * Only called for intel family 06h CPUs.
 *
 * @returns true/false.
 * @param   enmConsider         The new microarch to consider.
 * @param   enmTarget           The target microarch.
 * @param   enmFound            The best microarch match we've found thus far.
 */
static bool cpumR3DbIsBetterIntelFam06Match(CPUMMICROARCH enmConsider, CPUMMICROARCH enmTarget, CPUMMICROARCH enmFound)
{
    /* Check intel family 06h claims. */
    AssertReturn(enmConsider >= kCpumMicroarch_Intel_P6_Core_Atom_First && enmConsider <= kCpumMicroarch_Intel_P6_Core_Atom_End,
                 false);
    AssertReturn(   (enmTarget >= kCpumMicroarch_Intel_P6_Core_Atom_First && enmTarget <= kCpumMicroarch_Intel_P6_Core_Atom_End)
                 || enmTarget == kCpumMicroarch_Intel_Unknown,
                 false);

    /* Put matches out of the way. */
    if (enmConsider == enmTarget)
        return true;
    if (enmFound == enmTarget)
        return false;

    /* If found isn't a family 06h march, whatever we're considering must be a better choice. */
    if (   enmFound < kCpumMicroarch_Intel_P6_Core_Atom_First
        || enmFound > kCpumMicroarch_Intel_P6_Core_Atom_End)
        return true;

    /*
     * The family 06h stuff is split into three categories:
     *      - Common P6 heritage
     *      - Core
     *      - Atom
     *
     * Determin which of the three arguments are Atom marchs, because that's
     * all we need to make the right choice.
     */
    bool const fConsiderAtom = enmConsider >= kCpumMicroarch_Intel_Atom_First;
    bool const fTargetAtom   = enmTarget   >= kCpumMicroarch_Intel_Atom_First;
    bool const fFoundAtom    = enmFound    >= kCpumMicroarch_Intel_Atom_First;

    /*
     * Want atom:
     */
    if (fTargetAtom)
    {
        /* Pick the atom if we've got one of each.*/
        if (fConsiderAtom != fFoundAtom)
            return fConsiderAtom;
        /* If we haven't got any atoms under consideration, pick a P6 or the earlier core.
           Note! Not entirely sure Dothan is the best choice, but it'll do for now. */
        if (!fConsiderAtom)
        {
            if (enmConsider > enmFound)
                return enmConsider <= kCpumMicroarch_Intel_P6_M_Dothan;
            return enmFound > kCpumMicroarch_Intel_P6_M_Dothan;
        }
        /* else: same category, default comparison rules. */
        Assert(fConsiderAtom && fFoundAtom);
    }
    /*
     * Want non-atom:
     */
    /* Pick the non-atom if we've got one of each. */
    else if (fConsiderAtom != fFoundAtom)
        return fFoundAtom;
    /* If we've only got atoms under consideration, pick the older one just to pick something. */
    else if (fConsiderAtom)
        return enmConsider < enmFound;
    else
        Assert(!fConsiderAtom && !fFoundAtom);

    /*
     * Same basic category.  Do same compare as caller.
     */
    return cpumR3DbIsBetterMarchMatch(enmConsider, enmTarget, enmFound);
}

#endif /* RT_ARCH_X86 || RT_ARCH_AMD64 */

int cpumR3DbGetCpuInfo(const char *pszName, PCPUMINFO pInfo)
{
    CPUMDBENTRY const *pEntry = NULL;
    int                rc;

    if (!strcmp(pszName, "host"))
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    {
        /*
         * Create a CPU database entry for the host CPU.  This means getting
         * the CPUID bits from the real CPU and grabbing the closest matching
         * database entry for MSRs.
         */
        rc = CPUMR3CpuIdDetectUnknownLeafMethod(&pInfo->enmUnknownCpuIdMethod, &pInfo->DefCpuId);
        if (RT_FAILURE(rc))
            return rc;
        rc = CPUMCpuIdCollectLeavesX86(&pInfo->paCpuIdLeavesR3, &pInfo->cCpuIdLeaves);
        if (RT_FAILURE(rc))
            return rc;
        pInfo->fMxCsrMask = CPUMR3DeterminHostMxCsrMask();

        /* Lookup database entry for MSRs. */
        CPUMCPUVENDOR const enmVendor    = CPUMCpuIdDetectX86VendorEx(pInfo->paCpuIdLeavesR3[0].uEax,
                                                                      pInfo->paCpuIdLeavesR3[0].uEbx,
                                                                      pInfo->paCpuIdLeavesR3[0].uEcx,
                                                                      pInfo->paCpuIdLeavesR3[0].uEdx);
        uint32_t      const uStd1Eax     = pInfo->paCpuIdLeavesR3[1].uEax;
        uint8_t       const uFamily      = RTX86GetCpuFamily(uStd1Eax);
        uint8_t       const uModel       = RTX86GetCpuModel(uStd1Eax, enmVendor == CPUMCPUVENDOR_INTEL);
        uint8_t       const uStepping    = RTX86GetCpuStepping(uStd1Eax);
        CPUMMICROARCH const enmMicroarch = CPUMCpuIdDetermineX86MicroarchEx(enmVendor, uFamily, uModel, uStepping);

        for (unsigned i = 0; i < RT_ELEMENTS(g_apCpumDbEntries); i++)
        {
            CPUMDBENTRY const *pCur = g_apCpumDbEntries[i];
            if ((CPUMCPUVENDOR)pCur->enmVendor == enmVendor)
            {
                /* Match against Family, Microarch, model and stepping.  Except
                   for family, always match the closer with preference given to
                   the later/older ones. */
                if (pCur->uFamily == uFamily)
                {
                    if (pCur->enmMicroarch == enmMicroarch)
                    {
                        if (pCur->uModel == uModel)
                        {
                            if (pCur->uStepping == uStepping)
                            {
                                /* Perfect match. */
                                pEntry = pCur;
                                break;
                            }

                            if (   !pEntry
                                || pEntry->uModel       != uModel
                                || pEntry->enmMicroarch != enmMicroarch
                                || pEntry->uFamily      != uFamily)
                                pEntry = pCur;
                            else if (  pCur->uStepping >= uStepping
                                     ? pCur->uStepping < pEntry->uStepping || pEntry->uStepping < uStepping
                                     : pCur->uStepping > pEntry->uStepping)
                                     pEntry = pCur;
                        }
                        else if (   !pEntry
                                 || pEntry->enmMicroarch != enmMicroarch
                                 || pEntry->uFamily      != uFamily)
                            pEntry = pCur;
                        else if (  pCur->uModel >= uModel
                                 ? pCur->uModel < pEntry->uModel || pEntry->uModel < uModel
                                 : pCur->uModel > pEntry->uModel)
                            pEntry = pCur;
                    }
                    else if (   !pEntry
                             || pEntry->uFamily != uFamily)
                        pEntry = pCur;
                    /* Special march matching rules applies to intel family 06h. */
                    else if (     enmVendor == CPUMCPUVENDOR_INTEL
                               && uFamily   == 6
                             ? cpumR3DbIsBetterIntelFam06Match(pCur->enmMicroarch, enmMicroarch, pEntry->enmMicroarch)
                             : cpumR3DbIsBetterMarchMatch(pCur->enmMicroarch, enmMicroarch, pEntry->enmMicroarch))
                        pEntry = pCur;
                }
                /* We don't do closeness matching on family, we use the first
                   entry for the CPU vendor instead. (P4 workaround.) */
                else if (!pEntry)
                    pEntry = pCur;
            }
        }

        if (pEntry)
            LogRel(("CPUM: Matched host CPU %s %#x/%#x/%#x %s with CPU DB entry '%s' (%s %#x/%#x/%#x %s)\n",
                    CPUMCpuVendorName(enmVendor), uFamily, uModel, uStepping, CPUMMicroarchName(enmMicroarch),
                    pEntry->pszName,  CPUMCpuVendorName((CPUMCPUVENDOR)pEntry->enmVendor), pEntry->uFamily, pEntry->uModel,
                    pEntry->uStepping, CPUMMicroarchName(pEntry->enmMicroarch) ));
        else
        {
            pEntry = g_apCpumDbEntries[0];
            LogRel(("CPUM: No matching processor database entry %s %#x/%#x/%#x %s, falling back on '%s'\n",
                    CPUMCpuVendorName(enmVendor), uFamily, uModel, uStepping, CPUMMicroarchName(enmMicroarch),
                    pEntry->pszName));
        }
    }
    else
#else
        pszName = g_apCpumDbEntries[0]->pszName; /* Just pick the first entry for non-x86 hosts. */
#endif
    {
        /*
         * We're supposed to be emulating a specific CPU that is included in
         * our CPU database.  The CPUID tables needs to be copied onto the
         * heap so the caller can modify them and so they can be freed like
         * in the host case above.
         */
        for (unsigned i = 0; i < RT_ELEMENTS(g_apCpumDbEntries); i++)
            if (!strcmp(pszName, g_apCpumDbEntries[i]->pszName))
            {
                pEntry = g_apCpumDbEntries[i];
                break;
            }
        if (!pEntry)
        {
            LogRel(("CPUM: Cannot locate any CPU by the name '%s'\n", pszName));
            return VERR_CPUM_DB_CPU_NOT_FOUND;
        }

        pInfo->cCpuIdLeaves = pEntry->cCpuIdLeaves;
        if (pEntry->cCpuIdLeaves)
        {
            /* Must allocate a multiple of 16 here, matching cpumR3CpuIdEnsureSpace. */
            size_t cbExtra = sizeof(pEntry->paCpuIdLeaves[0]) * (RT_ALIGN(pEntry->cCpuIdLeaves, 16) - pEntry->cCpuIdLeaves);
            pInfo->paCpuIdLeavesR3 = (PCPUMCPUIDLEAF)RTMemDupEx(pEntry->paCpuIdLeaves,
                                                                sizeof(pEntry->paCpuIdLeaves[0]) * pEntry->cCpuIdLeaves,
                                                                cbExtra);
            if (!pInfo->paCpuIdLeavesR3)
                return VERR_NO_MEMORY;
        }
        else
            pInfo->paCpuIdLeavesR3 = NULL;

        pInfo->enmUnknownCpuIdMethod = pEntry->enmUnknownCpuId;
        pInfo->DefCpuId              = pEntry->DefUnknownCpuId;
        pInfo->fMxCsrMask            = pEntry->fMxCsrMask;

        LogRel(("CPUM: Using CPU DB entry '%s' (%s %#x/%#x/%#x %s)\n",
                pEntry->pszName, CPUMCpuVendorName((CPUMCPUVENDOR)pEntry->enmVendor),
                pEntry->uFamily, pEntry->uModel, pEntry->uStepping, CPUMMicroarchName(pEntry->enmMicroarch) ));
    }

    pInfo->fMsrMask             = pEntry->fMsrMask;
    pInfo->iFirstExtCpuIdLeaf   = 0; /* Set by caller. */
    pInfo->uScalableBusFreq     = pEntry->uScalableBusFreq;

    /*
     * Copy the MSR range.
     */
    uint32_t        cMsrs   = 0;
    PCPUMMSRRANGE   paMsrs  = NULL;

    PCCPUMMSRRANGE  pCurMsr = pEntry->paMsrRanges;
    uint32_t        cLeft   = pEntry->cMsrRanges;
    while (cLeft-- > 0)
    {
        rc = cpumR3MsrRangesInsert(NULL /* pVM */, &paMsrs, &cMsrs, pCurMsr);
        if (RT_FAILURE(rc))
        {
            Assert(!paMsrs); /* The above function frees this. */
            RTMemFree(pInfo->paCpuIdLeavesR3);
            pInfo->paCpuIdLeavesR3 = NULL;
            return rc;
        }
        pCurMsr++;
    }

    pInfo->paMsrRangesR3   = paMsrs;
    pInfo->cMsrRanges      = cMsrs;
    return VINF_SUCCESS;
}


/**
 * Insert an MSR range into the VM.
 *
 * If the new MSR range overlaps existing ranges, the existing ones will be
 * adjusted/removed to fit in the new one.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pNewRange           Pointer to the MSR range being inserted.
 */
VMMR3DECL(int) CPUMR3MsrRangesInsert(PVM pVM, PCCPUMMSRRANGE pNewRange)
{
    AssertReturn(pVM, VERR_INVALID_PARAMETER);
    AssertReturn(pNewRange, VERR_INVALID_PARAMETER);

    return cpumR3MsrRangesInsert(pVM, NULL /* ppaMsrRanges */, NULL /* pcMsrRanges */, pNewRange);
}


/**
 * Register statistics for the MSRs.
 *
 * This must not be called before the MSRs have been finalized and moved to the
 * hyper heap.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 */
int cpumR3MsrRegStats(PVM pVM)
{
    /*
     * Global statistics.
     */
    PCPUM pCpum = &pVM->cpum.s;
    STAM_REL_REG(pVM, &pCpum->cMsrReads,                STAMTYPE_COUNTER,   "/CPUM/MSR-Totals/Reads",
                 STAMUNIT_OCCURENCES, "All RDMSRs making it to CPUM.");
    STAM_REL_REG(pVM, &pCpum->cMsrReadsRaiseGp,         STAMTYPE_COUNTER,   "/CPUM/MSR-Totals/ReadsRaisingGP",
                 STAMUNIT_OCCURENCES, "RDMSR raising #GPs, except unknown MSRs.");
    STAM_REL_REG(pVM, &pCpum->cMsrReadsUnknown,         STAMTYPE_COUNTER,   "/CPUM/MSR-Totals/ReadsUnknown",
                 STAMUNIT_OCCURENCES, "RDMSR on unknown MSRs (raises #GP).");
    STAM_REL_REG(pVM, &pCpum->cMsrWrites,               STAMTYPE_COUNTER,   "/CPUM/MSR-Totals/Writes",
                 STAMUNIT_OCCURENCES, "All WRMSRs making it to CPUM.");
    STAM_REL_REG(pVM, &pCpum->cMsrWritesRaiseGp,        STAMTYPE_COUNTER,   "/CPUM/MSR-Totals/WritesRaisingGP",
                 STAMUNIT_OCCURENCES, "WRMSR raising #GPs, except unknown MSRs.");
    STAM_REL_REG(pVM, &pCpum->cMsrWritesToIgnoredBits,  STAMTYPE_COUNTER,   "/CPUM/MSR-Totals/WritesToIgnoredBits",
                 STAMUNIT_OCCURENCES, "Writing of ignored bits.");
    STAM_REL_REG(pVM, &pCpum->cMsrWritesUnknown,        STAMTYPE_COUNTER,   "/CPUM/MSR-Totals/WritesUnknown",
                 STAMUNIT_OCCURENCES, "WRMSR on unknown MSRs (raises #GP).");


# ifdef VBOX_WITH_STATISTICS
    /*
     * Per range.
     */
    PCPUMMSRRANGE   paRanges = pVM->cpum.s.GuestInfo.paMsrRangesR3;
    uint32_t        cRanges  = pVM->cpum.s.GuestInfo.cMsrRanges;
    for (uint32_t i = 0; i < cRanges; i++)
    {
        char    szName[160];
        ssize_t cchName;

        if (paRanges[i].uFirst == paRanges[i].uLast)
            cchName = RTStrPrintf(szName, sizeof(szName), "/CPUM/MSRs/%#010x-%s",
                                  paRanges[i].uFirst, paRanges[i].szName);
        else
            cchName = RTStrPrintf(szName, sizeof(szName), "/CPUM/MSRs/%#010x-%#010x-%s",
                                  paRanges[i].uFirst, paRanges[i].uLast, paRanges[i].szName);

        RTStrCopy(&szName[cchName], sizeof(szName) - cchName, "-reads");
        STAMR3Register(pVM, &paRanges[i].cReads, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, szName, STAMUNIT_OCCURENCES, "RDMSR");

        RTStrCopy(&szName[cchName], sizeof(szName) - cchName, "-writes");
        STAMR3Register(pVM, &paRanges[i].cWrites, STAMTYPE_COUNTER, STAMVISIBILITY_USED, szName, STAMUNIT_OCCURENCES, "WRMSR");

        RTStrCopy(&szName[cchName], sizeof(szName) - cchName, "-GPs");
        STAMR3Register(pVM, &paRanges[i].cGps, STAMTYPE_COUNTER, STAMVISIBILITY_USED, szName, STAMUNIT_OCCURENCES, "#GPs");

        RTStrCopy(&szName[cchName], sizeof(szName) - cchName, "-ign-bits-writes");
        STAMR3Register(pVM, &paRanges[i].cIgnoredBits, STAMTYPE_COUNTER, STAMVISIBILITY_USED, szName, STAMUNIT_OCCURENCES, "WRMSR w/ ignored bits");
    }
# endif /* VBOX_WITH_STATISTICS */

    return VINF_SUCCESS;
}

#endif /* !CPUM_DB_STANDALONE */

