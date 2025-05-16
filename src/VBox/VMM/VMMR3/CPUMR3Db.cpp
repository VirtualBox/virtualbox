/* $Id: CPUMR3Db.cpp 168915 2025-05-16 21:41:53Z knut.osmundsen@oracle.com $ */
/** @file
 * CPUM - CPU database part.
 */

/*
 * Copyright (C) 2013-2025 Oracle and/or its affiliates.
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
#if defined(VBOX_VMM_TARGET_ARMV8) || defined(RT_ARCH_ARM64)
# include <iprt/armv8.h>
#endif
#if !defined(RT_ARCH_ARM64)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/mem.h>
#include <iprt/ctype.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int cpumDbPopulateInfoFromEntry(PCPUMINFO pInfo, PCCPUMDBENTRY pEntryCore, bool fHost);


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


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/*
 * Include the X86 profiles.
 */
#if defined(VBOX_VMM_TARGET_X86)

# include "target-x86/CPUMR3Msr-x86.h" /* MSR macros needed by the profiles. */

# include "cpus/Intel_Core_i7_6700K.h"
# include "cpus/Intel_Core_i7_5600U.h"
# include "cpus/Intel_Core_i7_3960X.h"
# include "cpus/Intel_Core_i5_3570.h"
# include "cpus/Intel_Core_i7_2635QM.h"
# include "cpus/Intel_Xeon_X5482_3_20GHz.h"
# include "cpus/Intel_Core2_X6800_2_93GHz.h"
# include "cpus/Intel_Core2_T7600_2_33GHz.h"
# include "cpus/Intel_Core_Duo_T2600_2_16GHz.h"
# include "cpus/Intel_Pentium_M_processor_2_00GHz.h"
# include "cpus/Intel_Pentium_4_3_00GHz.h"
# include "cpus/Intel_Pentium_N3530_2_16GHz.h"
# include "cpus/Intel_Atom_330_1_60GHz.h"
# include "cpus/Intel_80486.h"
# include "cpus/Intel_80386.h"
# include "cpus/Intel_80286.h"
# include "cpus/Intel_80186.h"
# include "cpus/Intel_8086.h"

# include "cpus/AMD_Ryzen_7_1800X_Eight_Core.h"
# include "cpus/AMD_FX_8150_Eight_Core.h"
# include "cpus/AMD_Phenom_II_X6_1100T.h"
# include "cpus/Quad_Core_AMD_Opteron_2384.h"
# include "cpus/AMD_Athlon_64_X2_Dual_Core_4200.h"
# include "cpus/AMD_Athlon_64_3200.h"

# include "cpus/VIA_QuadCore_L4700_1_2_GHz.h"

# include "cpus/ZHAOXIN_KaiXian_KX_U5581_1_8GHz.h"

# include "cpus/Hygon_C86_7185_32_core.h"

#endif  /* VBOX_VMM_TARGET_X86 */


/*
 * Include the ARM profiles.
 *
 * Note! We include these when on ARM64 hosts regardless of the VMM target, so
 *       we can get more info about the host CPU.
 */
#if defined(VBOX_VMM_TARGET_ARMV8) || defined(RT_ARCH_ARM64)

# include "cpus/ARM_Apple_M1.h"
# include "cpus/ARM_Apple_M2_Max.h"
# include "cpus/ARM_Apple_M3_Max.h"
# include "cpus/ARM_Qualcomm_Snapdragon_X.h"

#endif


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
#if defined(VBOX_VMM_TARGET_X86)
    /*
     * X86 profiles:
     */
# ifdef VBOX_CPUDB_Intel_Core_i7_6700K_h
    &g_Entry_Intel_Core_i7_6700K.Core,
# endif
# ifdef VBOX_CPUDB_Intel_Core_i7_5600U_h
    &g_Entry_Intel_Core_i7_5600U.Core,
# endif
# ifdef VBOX_CPUDB_Intel_Core_i5_3570_h
    &g_Entry_Intel_Core_i5_3570.Core,
# endif
# ifdef VBOX_CPUDB_Intel_Core_i7_3960X_h
    &g_Entry_Intel_Core_i7_3960X.Core,
# endif
# ifdef VBOX_CPUDB_Intel_Core_i7_2635QM_h
    &g_Entry_Intel_Core_i7_2635QM.Core,
# endif
# ifdef VBOX_CPUDB_Intel_Pentium_N3530_2_16GHz_h
    &g_Entry_Intel_Pentium_N3530_2_16GHz.Core,
# endif
# ifdef VBOX_CPUDB_Intel_Atom_330_1_60GHz_h
    &g_Entry_Intel_Atom_330_1_60GHz.Core,
# endif
# ifdef VBOX_CPUDB_Intel_Pentium_M_processor_2_00GHz_h
    &g_Entry_Intel_Pentium_M_processor_2_00GHz.Core,
# endif
# ifdef VBOX_CPUDB_Intel_Xeon_X5482_3_20GHz_h
    &g_Entry_Intel_Xeon_X5482_3_20GHz.Core,
# endif
# ifdef VBOX_CPUDB_Intel_Core2_X6800_2_93GHz_h
    &g_Entry_Intel_Core2_X6800_2_93GHz.Core,
# endif
# ifdef VBOX_CPUDB_Intel_Core2_T7600_2_33GHz_h
    &g_Entry_Intel_Core2_T7600_2_33GHz.Core,
# endif
# ifdef VBOX_CPUDB_Intel_Core_Duo_T2600_2_16GHz_h
    &g_Entry_Intel_Core_Duo_T2600_2_16GHz.Core,
# endif
# ifdef VBOX_CPUDB_Intel_Pentium_4_3_00GHz_h
    &g_Entry_Intel_Pentium_4_3_00GHz.Core,
# endif
/** @todo pentium, pentium mmx, pentium pro, pentium II, pentium III */
# ifdef VBOX_CPUDB_Intel_80486_h
    &g_Entry_Intel_80486.Core,
# endif
# ifdef VBOX_CPUDB_Intel_80386_h
    &g_Entry_Intel_80386.Core,
# endif
# ifdef VBOX_CPUDB_Intel_80286_h
    &g_Entry_Intel_80286.Core,
# endif
# ifdef VBOX_CPUDB_Intel_80186_h
    &g_Entry_Intel_80186.Core,
# endif
# ifdef VBOX_CPUDB_Intel_8086_h
    &g_Entry_Intel_8086.Core,
# endif

# ifdef VBOX_CPUDB_AMD_Ryzen_7_1800X_Eight_Core_h
    &g_Entry_AMD_Ryzen_7_1800X_Eight_Core.Core,
# endif
# ifdef VBOX_CPUDB_AMD_FX_8150_Eight_Core_h
    &g_Entry_AMD_FX_8150_Eight_Core.Core,
# endif
# ifdef VBOX_CPUDB_AMD_Phenom_II_X6_1100T_h
    &g_Entry_AMD_Phenom_II_X6_1100T.Core,
# endif
# ifdef VBOX_CPUDB_Quad_Core_AMD_Opteron_2384_h
    &g_Entry_Quad_Core_AMD_Opteron_2384.Core,
# endif
# ifdef VBOX_CPUDB_AMD_Athlon_64_X2_Dual_Core_4200_h
    &g_Entry_AMD_Athlon_64_X2_Dual_Core_4200.Core,
# endif
# ifdef VBOX_CPUDB_AMD_Athlon_64_3200_h
    &g_Entry_AMD_Athlon_64_3200.Core,
# endif

# ifdef VBOX_CPUDB_ZHAOXIN_KaiXian_KX_U5581_1_8GHz_h
    &g_Entry_ZHAOXIN_KaiXian_KX_U5581_1_8GHz.Core,
# endif

# ifdef VBOX_CPUDB_VIA_QuadCore_L4700_1_2_GHz_h
    &g_Entry_VIA_QuadCore_L4700_1_2_GHz.Core,
# endif

# ifdef VBOX_CPUDB_NEC_V20_h
    &g_Entry_NEC_V20.Core,
# endif

# ifdef VBOX_CPUDB_Hygon_C86_7185_32_core_h
    &g_Entry_Hygon_C86_7185_32_core.Core,
# endif
#endif /* VBOX_VMM_TARGET_X86 */

#if defined(VBOX_VMM_TARGET_ARMV8) || defined(RT_ARCH_ARM64)
    /*
     * ARM profiles:
     */
    &g_Entry_ARM_Apple_M1.Core,
    &g_Entry_ARM_Apple_M2_Max.Core,
    &g_Entry_ARM_Apple_M3_Max.Core,
    &g_Entry_ARM_Qualcomm_Snapdragon_X.Core,
#endif /* VBOX_VMM_TARGET_ARMV8 || RT_ARCH_ARM64 */
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
 * Skips any blah-blah word at the start of @a psz.
 */
static size_t cpumSkipCpuNameBlahBlah(size_t off, const char *psz)
{
    static RTSTRTUPLE const s_aWords[] =
    {
        { RT_STR_TUPLE("(R)")  },
        { RT_STR_TUPLE("(C)")  },
        { RT_STR_TUPLE("(TM)") },
    };
    for (size_t i = 0; i < RT_ELEMENTS(s_aWords); i++)
        if (RTStrNICmp(&psz[off], s_aWords[i].psz, s_aWords[i].cch) == 0)
        {
            /* If what we're skipping was preceeded by whitespace, skip whitespace after it
               so we'll correctly match a string that doesn't include this blah-blah word. */
            char chPrev = off > 0 ? psz[off - 1] : '\0';
            off += s_aWords[i].cch;
            if (RT_C_IS_SPACE(chPrev) || chPrev == '@')
                while (RT_C_IS_SPACE(psz[off]) || psz[off] == '@')
                    off++;

            /* Recurse to match more blah-blah following this one. */
            return cpumSkipCpuNameBlahBlah(off, psz);
        }
    return off;
}

/**
 * A RTStrStartsWith variant that takes care if pszStart ends with a number.
 */
static bool cpumDbStartsWith(const char *pszString, size_t cchString, const char *pszStart, size_t cchStart)
{
    if (cchString == RTSTR_MAX)
        cchString = strlen(pszString);
    if (cchStart == RTSTR_MAX)
        cchStart = strlen(pszStart);
    if (cchStart <= cchString)
        if (memcmp(pszString, pszStart, cchStart) == 0)
        {
            if (cchString == cchStart)
                return true;
            if (!RT_C_IS_DIGIT(pszStart[cchStart - 1]))
                return true;
            /* pszStart ends with a digit, so if pszString continues with a digit
               we don't have a match (unless there its all zeros). Just require
               a non-digit as the next character. */
            if (!RT_C_IS_DIGIT(pszString[cchStart]))
                return true;
        }
    return false;
}


/**
 * Returns CPU database entry considered the best match for the given name.
 *
 * @returns Pointer the CPU database entry, NULL if nothing suitable was found.
 * @param   pszName             The CPU name to locte a profile for.
 * @param   enmEntryType        The type of profile to return.
 *                              CPUMDBENTRYTYPE_INVALID for any.
 * @param   puScore             Where to return the score.  A score of 100 is a
 *                              perfect name match.
 */
VMMR3DECL(PCCPUMDBENTRY) CPUMR3DbGetBestEntryByName(const char *pszName, CPUMDBENTRYTYPE enmEntryType, uint32_t *puScore)
{
    AssertStmt(!RT_C_IS_SPACE(*pszName), pszName = RTStrStripL(pszName));
    AssertReturnStmt(*pszName, *puScore = 0, NULL);

    /*
     * Is there a perfect match in the database?
     */
    *puScore = 100;
    for (size_t i = 0; i < RT_ELEMENTS(g_apCpumDbEntries); i++)
    {
        PCCPUMDBENTRY pEntry = g_apCpumDbEntries[i];
        if (enmEntryType == pEntry->enmEntryType || enmEntryType == CPUMDBENTRYTYPE_INVALID)
        {
            if (   strcmp(pEntry->pszName, pszName) == 0
                || strcmp(pEntry->pszFullName, pszName) == 0)
                return g_apCpumDbEntries[i];
            if (pEntry->enmEntryType == CPUMDBENTRYTYPE_ARM)
            {
                PCCPUMDBENTRYARM pArmEntry = (PCCPUMDBENTRYARM)pEntry;
                for (uint32_t iVar = 0; iVar < pArmEntry->cVariants; iVar++)
                    if (strcmp(pArmEntry->aVariants[iVar].pszName, pszName) == 0)
                        return g_apCpumDbEntries[i];
            }
        }
    }

    /*
     * See if a database name is a subset of the given name.
     */
    size_t cchName = strlen(pszName);
    while (cchName > 0 && RT_C_IS_SPACE(pszName[cchName - 1]))
        cchName--;
    AssertReturnStmt(cchName > 0, *puScore = 0, NULL);

    *puScore = 90;
    for (size_t i = 0; i < RT_ELEMENTS(g_apCpumDbEntries); i++)
    {
        PCCPUMDBENTRY pEntry = g_apCpumDbEntries[i];
        if (enmEntryType == pEntry->enmEntryType || enmEntryType == CPUMDBENTRYTYPE_INVALID)
        {
            if (   cpumDbStartsWith(pszName, cchName, pEntry->pszName, RTSTR_MAX)
                || cpumDbStartsWith(pszName, cchName, pEntry->pszFullName, RTSTR_MAX))
                return g_apCpumDbEntries[i];
            if (pEntry->enmEntryType == CPUMDBENTRYTYPE_ARM)
            {
                PCCPUMDBENTRYARM pArmEntry = (PCCPUMDBENTRYARM)pEntry;
                for (uint32_t iVar = 0; iVar < pArmEntry->cVariants; iVar++)
                    if (cpumDbStartsWith(pszName, cchName, pArmEntry->aVariants[iVar].pszName, RTSTR_MAX))
                        return g_apCpumDbEntries[i];
            }
        }
    }

    /*
     * The other way around.
     */
    *puScore = 88;
    for (size_t i = 0; i < RT_ELEMENTS(g_apCpumDbEntries); i++)
    {
        PCCPUMDBENTRY pEntry = g_apCpumDbEntries[i];
        if (enmEntryType == pEntry->enmEntryType || enmEntryType == CPUMDBENTRYTYPE_INVALID)
        {
            if (   cpumDbStartsWith(pEntry->pszName,     RTSTR_MAX, pszName, cchName)
                || cpumDbStartsWith(pEntry->pszFullName, RTSTR_MAX, pszName, cchName))
                return g_apCpumDbEntries[i];
            if (pEntry->enmEntryType == CPUMDBENTRYTYPE_ARM)
            {
                PCCPUMDBENTRYARM pArmEntry = (PCCPUMDBENTRYARM)pEntry;
                for (uint32_t iVar = 0; iVar < pArmEntry->cVariants; iVar++)
                    if (cpumDbStartsWith(pArmEntry->aVariants[iVar].pszName, RTSTR_MAX, pszName, cchName))
                        return g_apCpumDbEntries[i];
            }
        }
    }

    /*
     * Match the name strings.
     *
     * This is need quite some more work to work efficiently, however, we only
     * really care about strings like 'Apple M3 Max' at present.
     */
    PCCPUMDBENTRY  pBest           = NULL;
    const char    *pszBestNm       = NULL;
    size_t         cchBestNm       = 0;
    size_t         offBestNmN      = 0;
    size_t         cchBestInputNm  = 0;
    size_t         offBestInputNmN = 0;
    for (size_t i = 0; i < RT_ELEMENTS(g_apCpumDbEntries); i++)
    {
        PCCPUMDBENTRY pEntry = g_apCpumDbEntries[i];
        if (enmEntryType == pEntry->enmEntryType || enmEntryType == CPUMDBENTRYTYPE_INVALID)
        {
            /* Gather strings to consider: */
            const char *apszNames[2 + 2] = { pEntry->pszName, pEntry->pszFullName, };
            size_t      cNames           = 2;
            if (pEntry->enmEntryType == CPUMDBENTRYTYPE_ARM)
            {
                PCCPUMDBENTRYARM pArmEntry = (PCCPUMDBENTRYARM)pEntry;
                AssertCompile(RT_ELEMENTS(pArmEntry->aVariants) == 2); /* apszName size */
                for (uint32_t iVar = 0; iVar < pArmEntry->cVariants; iVar++)
                    apszNames[cNames++] = pArmEntry->aVariants[iVar].pszName;
            }

            /* Match each name. */
            for (size_t iName = 0; iName < cNames; iName++)
            {
                const char * const pszCand = apszNames[iName];
                Assert(pszCand);
                Assert(!RT_C_IS_SPACE(*pszCand));

                /* See how much of the two names matches up and keep the best ones... */
                size_t offCand = 0;
                size_t offName = 0;
                for (;;)
                {
                    char chCand = pszCand[offCand];
                    if (chCand == '(')
                    {
                        offCand = cpumSkipCpuNameBlahBlah(offCand, pszCand);
                        chCand  = pszCand[offCand];
                    }

                    char chName      = pszName[offName];
                    if (chName == '(')
                    {
                        offName = cpumSkipCpuNameBlahBlah(offName, pszName);
                        chName  = pszName[offName];
                    }

                    if (chCand != chName)
                    {
                        chCand = RT_C_TO_LOWER(chCand);
                        chName = RT_C_TO_LOWER(chName);
                        if (RT_C_IS_SPACE(chCand) || chCand == '@')
                            chCand = ' ';
                        if (RT_C_IS_SPACE(chName) || chName == '@')
                            chName      = ' ';

                        if (   chCand != chName
                            && offName > 0
                            && pszName[offName - 1] == 'i'
                            && pszName[offName + 1] == '-'
                            && pszCand[offCand + 1] == '-'
                            && (chName == '3' || chName == '5' || chName == '7' || chName == '9')
                            && (chCand == '3' || chCand == '5' || chCand == '7' || chCand == '9')
                            /*&& chName < chCand ? */ )
                        { /* We let 'i3/i5/i7/i9-' match as the model number following is often more helpful ... */ }
                        else if (chCand != chName)
                        {
                            /** @todo i3/i5/i7/i9 should probably all be made to match here... */

                            /*
                             * If we match more of the input name it is a clear improvement.
                             *
                             * If we end up with the same length match we will try for better
                             * numeric matches.  The idea here is that if we have matched up
                             * 'Apple M' and is considering whether 'Apple M2' or 'Apple M4'
                             * is better when looking for 'Apple M3 Ultra', we should pick
                             * the older M2 entry as it is less likely to have unsupported
                             * features and whatnot listed in it.
                             */
                            if (offName > 0 && offCand > 0)
                            {
                                /** @todo this isn't exactly perfect, but it'll do for now, I hope... */
                                /** @todo maybe add some word-boundrary logic here, so we don't match
                                 *        'dragon ya' and 'dragon yb'. This would fit nicely with the current
                                 *        handling of trailing numbers. */
                                bool const fNameIsDigit    = RT_C_IS_DIGIT(chName);
                                bool const fCandIsDigit    = RT_C_IS_DIGIT(chCand);
                                bool       fToBeConsidered = fNameIsDigit == fCandIsDigit
                                                          || (!fNameIsDigit && RT_C_IS_DIGIT(pszName[offName - 1]))
                                                          || (!fCandIsDigit && RT_C_IS_DIGIT(pszCand[offCand - 1]));
                                bool const fNumeric        = fToBeConsidered && (fNameIsDigit || fCandIsDigit);
                                size_t     offNameN        = offName;
                                size_t     offCandN        = offCand;
                                if (fNumeric)
                                {
                                    if (fNameIsDigit != fCandIsDigit)
                                        offNameN--, offCandN--;
                                    while (   offNameN > 0
                                           && RT_C_IS_DIGIT(pszName[offNameN - 1])
                                           && offCandN > 0
                                           && RT_C_IS_DIGIT(pszCand[offCandN - 1]))
                                        offNameN--, offCandN--;
                                    fToBeConsidered = RTStrVersionCompare(&pszCand[offCandN], &pszName[offNameN]) <= 0;
                                }

                                if (   fToBeConsidered
                                    && (  !fNumeric
                                        ? offName > cchBestInputNm
                                        :    offNameN > offBestInputNmN
                                          || (    offNameN == offBestInputNmN
                                               && RTStrVersionCompare(&pszCand[offCandN], &pszBestNm[offBestNmN]) > 0) ) )
                                {
                                    pBest           = pEntry;
                                    pszBestNm       = pszCand;
                                    cchBestNm       = offCand;
                                    offBestNmN      = offCandN;
                                    cchBestInputNm  = offName;
                                    offBestInputNmN = offNameN;
                                }
                            }
                            break;
                        }
                    }

                    /* If we've somehow matched the whole input name due to normalization and
                       case-insensitivity, return a prefect match. */
                    if (offName >= cchName)
                    {
                        *puScore = 100;
                        return pEntry;
                    }
                    Assert(chName != '\0' && chCand != '\0');

                    /* Advance, normalizing spaces. */
                    if (!RT_C_IS_SPACE(chName) && chName != '@')
                    {
                        offCand += 1;
                        offName += 1;
                    }
                    else
                    {
                        Assert(RT_C_IS_SPACE(chCand));
                        do
                            chCand = pszCand[++offCand];
                        while (RT_C_IS_SPACE(chCand) || chCand == '@');
                        do
                            chName = pszName[++offName];
                        while (RT_C_IS_SPACE(chName) || chName == '@');
                    }
                }
            }
        }
    }

    /*
     * If we've got a match, check that it carry some weight and is not just
     * matching vendor part of the name or something like that.  This is a
     * little bit tricky...
     */
    if (cchBestInputNm > 2)
    {
        /* In most cases we can sidestep the issue by scanning for digits, if
           we already matched some we're probably good and if the difference
           is down to digits (e.g. M2 vs M3), we're also good. */
        uint32_t cDigits = 0;
        for (uint32_t off = 0; off < cchBestInputNm; off++)
            cDigits += RT_C_IS_DIGIT(pszName[off]);
        if (   RT_C_IS_DIGIT(pszBestNm[cchBestNm])
            && RT_C_IS_DIGIT(pszBestNm[cchBestNm]))
            cDigits += 1;
        if (cDigits > 0)
        {
            *puScore = RT_MIN(10 + cDigits, 80);
            return pBest;
        }

        /* Now, the above doesn't work for all names in the DB. */
        static RTSTRTUPLE s_aWeightlessWords[] =
        {
            { RT_STR_TUPLE("Core")          },
            { RT_STR_TUPLE("Dual-Core")     },
            { RT_STR_TUPLE("Quad-Core")     },
            { RT_STR_TUPLE("Dual")          },
            { RT_STR_TUPLE("Quad")          },
            { RT_STR_TUPLE("Genuin")        },
            { RT_STR_TUPLE("Authentic")     },
            { RT_STR_TUPLE("Processor")     },
            { RT_STR_TUPLE("CPU")           },
            { RT_STR_TUPLE("(R)")           },
            { RT_STR_TUPLE("(C)")           },
            { RT_STR_TUPLE("(TM)")          },
            { RT_STR_TUPLE("Apple")         },
            { RT_STR_TUPLE("Qualcomm")      },
            {     RT_STR_TUPLE("Snapdragon") },
            { RT_STR_TUPLE("Intel")         },
            {     RT_STR_TUPLE("Pentium")   },
            {     RT_STR_TUPLE("i3")        },
            {     RT_STR_TUPLE("i5")        },
            {     RT_STR_TUPLE("i7")        },
            {     RT_STR_TUPLE("i9")        },
            {     RT_STR_TUPLE("Atom")      },
            {     RT_STR_TUPLE("Xeon")      },
            {     RT_STR_TUPLE("Gold")      },
            { RT_STR_TUPLE("AMD")           },
            {     RT_STR_TUPLE("FX-")       },
            {     RT_STR_TUPLE("Phenom")    },
            {     RT_STR_TUPLE("Ryzen")     },
            { RT_STR_TUPLE("Hygon")         },
            { RT_STR_TUPLE("VIA")           },
            { RT_STR_TUPLE("ZHAOXIN")       },
            {     RT_STR_TUPLE("KaiXian")   },
            { RT_STR_TUPLE("VIA")           },
            {     RT_STR_TUPLE("Nano")      },
        };
        size_t offWeightless = 0;
        while (offWeightless < cchName)
        {
            /* skip spaces and '@' */
            char ch = pszName[offWeightless];
            while (RT_C_IS_SPACE(ch) || ch == '@')
                ch = pszName[++offWeightless];
            if (!ch)
                break;

            /* Do look for the above words */
            size_t i = 0;
            while (   i < RT_ELEMENTS(s_aWeightlessWords)
                   && RTStrNICmp(&pszName[offWeightless], s_aWeightlessWords[i].psz, s_aWeightlessWords[i].cch) != 0)
                i++;
            if (i >= RT_ELEMENTS(s_aWeightlessWords))
                break;
            offWeightless += s_aWeightlessWords[i].cch;
        }

        if (offWeightless < cchBestInputNm)
        {
            *puScore = (uint32_t)(cchBestInputNm - offWeightless);
            return pBest;
        }
    }

    *puScore = 0;
    return NULL;
}


#if defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32) || defined(VBOX_VMM_TARGET_ARMV8)
/**
 * Gets the best matching DB entry for the given ARM ID register value.
 *
 * @returns Pointer to best match, NULL if nothing suitable was found.
 * @param   idMain              The main ID register value.
 * @param   puScore             Where to return the score.  100 for a direct
 *                              hit, less for a partial hit only matching the
 *                              microcode.
 */
VMMR3DECL(PCCPUMDBENTRYARM) CPUMR3DbGetBestEntryByArm64MainId(uint64_t idMain, uint32_t *puScore)
{
    /*
     * A quick search for a perfect match.
     */
    *puScore = 100;
    for (size_t i = 0; i < RT_ELEMENTS(g_apCpumDbEntries); i++)
    {
        PCCPUMDBENTRYARM const pEntry = (PCCPUMDBENTRYARM)g_apCpumDbEntries[i];
        if (pEntry->Core.enmEntryType == CPUMDBENTRYTYPE_ARM)
            for (uint32_t iVar = 0; iVar < pEntry->cVariants; iVar++)
                if (pEntry->aVariants[iVar].Midr.u64 == idMain)
                    return pEntry;
    }

    /*
     * Translate the ID to a microarchitecture and see if we can fine something similar.
     */
    CPUMMICROARCH enmMicroarch = kCpumMicroarch_Invalid;
    int rc = CPUMCpuIdDetermineArmV8MicroarchEx(idMain, NULL, &enmMicroarch, NULL, NULL, NULL, NULL);
    if (   RT_SUCCESS(rc)
        && enmMicroarch != kCpumMicroarch_Unknown)
    {
        uint32_t const   uPartNum   = (uint32_t)((idMain >> 4) & 0xfff);
        PCCPUMDBENTRYARM pBestEntry = NULL;
        for (size_t i = 0; i < RT_ELEMENTS(g_apCpumDbEntries); i++)
        {
            PCCPUMDBENTRYARM const pEntry = (PCCPUMDBENTRYARM)g_apCpumDbEntries[i];
            if (   pEntry->Core.enmMicroarch == enmMicroarch
                && pEntry->Core.enmEntryType == CPUMDBENTRYTYPE_ARM)
            {
                /* Just using the part number part, pick the entry that's closest from below. */
                if (   !pBestEntry
                    || (pBestEntry->aVariants[0].Midr.s.u12PartNum > uPartNum
                        ? pEntry->aVariants[0].Midr.s.u12PartNum < pBestEntry->aVariants[0].Midr.s.u12PartNum
                        :    pEntry->aVariants[0].Midr.s.u12PartNum <= uPartNum
                          && pEntry->aVariants[0].Midr.s.u12PartNum > pBestEntry->aVariants[0].Midr.s.u12PartNum))
                    pBestEntry = pEntry;
            }
        }
        if (pBestEntry)
        {
            *puScore = pBestEntry->aVariants[0].Midr.s.u12PartNum == uPartNum ? 90 : 80;
            return pBestEntry;
        }
    }

    *puScore = 0;
    return NULL;
}
#endif /* #if defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32) || defined(VBOX_VMM_TARGET_ARMV8) */



#if defined(VBOX_VMM_TARGET_X86) && (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64))

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


/**
 * X86 version of helper that picks a DB entry for the host and merges it with
 * available info in the @a pInfo structure.
 */
static int cpumR3DbCreateHostEntry(PCPUMINFO pInfo)
{
    /*
     * Create a CPU database entry for the host CPU.  This means getting
     * the CPUID bits from the real CPU and grabbing the closest matching
     * database entry for MSRs.
     */
    int rc = CPUMR3CpuIdDetectUnknownLeafMethod(&pInfo->enmUnknownCpuIdMethod, &pInfo->DefCpuId);
    if (RT_FAILURE(rc))
        return rc;
    rc = CPUMCpuIdCollectLeavesFromX86Host(&pInfo->paCpuIdLeavesR3, &pInfo->cCpuIdLeaves);
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

    PCCPUMDBENTRYX86    pEntry = NULL;
    for (unsigned i = 0; i < RT_ELEMENTS(g_apCpumDbEntries); i++)
    {
        CPUMDBENTRY const * const pCurCore = g_apCpumDbEntries[i];
        if (   (CPUMCPUVENDOR)pCurCore->enmVendor == enmVendor
            && pCurCore->enmEntryType == CPUMDBENTRYTYPE_X86)
        {
            CPUMDBENTRYX86 const * const pCur = (CPUMDBENTRYX86 const *)pCurCore;

            /* Match against Family, Microarch, model and stepping.  Except
               for family, always match the closer with preference given to
               the later/older ones. */
            if (pCur->uFamily == uFamily)
            {
                if (pCur->Core.enmMicroarch == enmMicroarch)
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
                            || pEntry->uModel            != uModel
                            || pEntry->Core.enmMicroarch != enmMicroarch
                            || pEntry->uFamily           != uFamily)
                            pEntry = pCur;
                        else if (  pCur->uStepping >= uStepping
                                 ? pCur->uStepping < pEntry->uStepping || pEntry->uStepping < uStepping
                                 : pCur->uStepping > pEntry->uStepping)
                                 pEntry = pCur;
                    }
                    else if (   !pEntry
                             || pEntry->Core.enmMicroarch != enmMicroarch
                             || pEntry->uFamily           != uFamily)
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
                         ? cpumR3DbIsBetterIntelFam06Match(pCur->Core.enmMicroarch, enmMicroarch, pEntry->Core.enmMicroarch)
                         : cpumR3DbIsBetterMarchMatch(pCur->Core.enmMicroarch, enmMicroarch, pEntry->Core.enmMicroarch))
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
                pEntry->Core.pszName,  CPUMCpuVendorName(pEntry->Core.enmVendor), pEntry->uFamily, pEntry->uModel,
                pEntry->uStepping, CPUMMicroarchName(pEntry->Core.enmMicroarch) ));
    else
    {
        pEntry = (CPUMDBENTRYX86 const *)g_apCpumDbEntries[0];
        LogRel(("CPUM: No matching processor database entry %s %#x/%#x/%#x %s, falling back on '%s'\n",
                CPUMCpuVendorName(enmVendor), uFamily, uModel, uStepping, CPUMMicroarchName(enmMicroarch),
                pEntry->Core.pszName));
    }

    return cpumDbPopulateInfoFromEntry(pInfo, &pEntry->Core, true /*fHost*/);
}

#endif /* VBOX_VMM_TARGET_X86 && (RT_ARCH_AMD64 || RT_ARCH_X86) */


/**
 * Helper that populates the CPUMINFO structure from DB entry.
 */
static int cpumDbPopulateInfoFromEntry(PCPUMINFO pInfo, PCCPUMDBENTRY pEntryCore, bool fHost)
{
#ifdef VBOX_VMM_TARGET_X86
    /*
     * X86.
     */
    AssertReturn(pEntryCore->enmEntryType == CPUMDBENTRYTYPE_X86, VERR_INTERNAL_ERROR_3);
    PCCPUMDBENTRYX86 const pEntry = (PCCPUMDBENTRYX86)pEntryCore;

    if (!fHost)
    {
        /*
         * The CPUID tables needs to be copied onto the heap so the caller can
         * modify them and so they can be freed like in the host case.
         */
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
                pEntry->Core.pszName, CPUMCpuVendorName(pEntry->Core.enmVendor),
                pEntry->uFamily, pEntry->uModel, pEntry->uStepping, CPUMMicroarchName(pEntry->Core.enmMicroarch) ));
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
        int rc = cpumR3MsrRangesInsert(NULL /* pVM */, &paMsrs, &cMsrs, pCurMsr);
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

#elif defined(VBOX_VMM_TARGET_ARMV8)
    /*
     * ARM.
     */
    AssertReturn(pEntryCore->enmEntryType == CPUMDBENTRYTYPE_ARM, VERR_INTERNAL_ERROR_3);
    PCCPUMDBENTRYARM const pEntry = (PCCPUMDBENTRYARM)pEntryCore;
    RT_NOREF(pInfo, pEntry, fHost);

#else
# error "port me"
#endif
    return VINF_SUCCESS;
}


int cpumR3DbGetCpuInfo(const char *pszName, PCPUMINFO pInfo)
{
#ifdef VBOX_VMM_TARGET_X86
    CPUMDBENTRYTYPE const enmEntryType = CPUMDBENTRYTYPE_X86;
#elif defined(VBOX_VMM_TARGET_ARMV8)
    CPUMDBENTRYTYPE const enmEntryType = CPUMDBENTRYTYPE_ARM;
#else
# error "port me"
#endif

    /*
     * Deal with the dynamic 'host' entry first.
     *
     * If we're not on a matchin host, we just pick the first entry in the
     * table and proceed as if this was specified by the caller (configured).
     */
    if (!strcmp(pszName, "host"))
    {
#if (defined(VBOX_VMM_TARGET_X86) && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86))) \
 || (defined(VBOX_VMM_TARGET_ARMV8) && defined(RT_ARCH_ARM64) && 0)
        return cpumR3DbCreateHostEntry(pInfo);
#else
        Assert(g_apCpumDbEntries[0]->enmEntryType == enmEntryType);
        pszName = g_apCpumDbEntries[0]->pszName; /* Just pick the first entry for non-x86 hosts. */
#endif
    }

    /*
     * We're supposed to be emulating a specific CPU from the database.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(g_apCpumDbEntries); i++)
        if (   g_apCpumDbEntries[i]->enmEntryType == enmEntryType
            && !strcmp(pszName, g_apCpumDbEntries[i]->pszName))
            return cpumDbPopulateInfoFromEntry(pInfo, g_apCpumDbEntries[i], false /*fHost*/);
    LogRel(("CPUM: Cannot locate any CPU by the name '%s'\n", pszName));
    return VERR_CPUM_DB_CPU_NOT_FOUND;
}

