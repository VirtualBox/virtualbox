/* $Id: CPUMR3Db-armv8.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPUM - CPU database part - ARMv8 specifics.
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
#include <VBox/vmm/vm.h>
#include <VBox/vmm/mm.h>

#include <iprt/errcore.h>
#include <iprt/armv8.h>
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


#ifndef CPUM_DB_STANDALONE

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
    NULL
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
 * Binary search used by cpumR3SysRegRangesInsert and has some special properties
 * wrt to mismatches.
 *
 * @returns Insert location.
 * @param   paSysRegRanges      The system register ranges to search.
 * @param   cSysRegRanges       The number of system register ranges.
 * @param   uSysReg             What to search for.
 */
static uint32_t cpumR3SysRegRangesBinSearch(PCCPUMSYSREGRANGE paSysRegRanges, uint32_t cSysRegRanges, uint16_t uSysReg)
{
    if (!cSysRegRanges)
        return 0;

    uint32_t iStart = 0;
    uint32_t iLast  = cSysRegRanges - 1;
    for (;;)
    {
        uint32_t i = iStart + (iLast - iStart + 1) / 2;
        if (   uSysReg >= paSysRegRanges[i].uFirst
            && uSysReg <= paSysRegRanges[i].uLast)
            return i;
        if (uSysReg < paSysRegRanges[i].uFirst)
        {
            if (i <= iStart)
                return i;
            iLast = i - 1;
        }
        else
        {
            if (i >= iLast)
            {
                if (i < cSysRegRanges)
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
 * @returns Pointer to the system register ranges on success, NULL on failure.  On failure
 *          @a *ppaSysRegRanges is freed and set to NULL.
 * @param   pVM             The cross context VM structure.  If NULL,
 *                          use the process heap, otherwise the VM's hyper heap.
 * @param   ppaSysRegRanges The variable pointing to the ranges (input/output).
 * @param   cSysRegRanges   The current number of ranges.
 * @param   cNewRanges      The number of ranges to be added.
 */
static PCPUMSYSREGRANGE cpumR3SysRegRangesEnsureSpace(PVM pVM, PCPUMSYSREGRANGE *ppaSysRegRanges, uint32_t cSysRegRanges, uint32_t cNewRanges)
{
    if (  cSysRegRanges + cNewRanges
        > RT_ELEMENTS(pVM->cpum.s.GuestInfo.aSysRegRanges) + (pVM ? 0 : 128 /* Catch too many system registers in CPU reporter! */))
    {
        LogRel(("CPUM: Too many system register ranges! %#x, max %#x\n",
                cSysRegRanges + cNewRanges, RT_ELEMENTS(pVM->cpum.s.GuestInfo.aSysRegRanges)));
        return NULL;
    }
    if (pVM)
    {
        Assert(cSysRegRanges == pVM->cpum.s.GuestInfo.cSysRegRanges);
        Assert(*ppaSysRegRanges == pVM->cpum.s.GuestInfo.aSysRegRanges);
    }
    else
    {
        if (cSysRegRanges + cNewRanges > RT_ALIGN_32(cSysRegRanges, 16))
        {

            uint32_t const cNew = RT_ALIGN_32(cSysRegRanges + cNewRanges, 16);
            void *pvNew = RTMemRealloc(*ppaSysRegRanges, cNew * sizeof(**ppaSysRegRanges));
            if (pvNew)
                *ppaSysRegRanges = (PCPUMSYSREGRANGE)pvNew;
            else
            {
                RTMemFree(*ppaSysRegRanges);
                *ppaSysRegRanges = NULL;
                return NULL;
            }
        }
    }

    return *ppaSysRegRanges;
}


/**
 * Inserts a new system register range in into an sorted system register range array.
 *
 * If the new system register range overlaps existing ranges, the existing ones will be
 * adjusted/removed to fit in the new one.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS
 * @retval  VERR_NO_MEMORY
 *
 * @param   pVM             The cross context VM structure.  If NULL,
 *                          use the process heap, otherwise the VM's hyper heap.
 * @param   ppaSysRegRanges The variable pointing to the ranges (input/output).
 *                          Must be NULL if using the hyper heap.
 * @param   pcSysRegRanges  The variable holding number of ranges. Must be NULL
 *                          if using the hyper heap.
 * @param   pNewRange       The new range.
 */
static int cpumR3SysRegRangesInsert(PVM pVM, PCPUMSYSREGRANGE *ppaSysRegRanges, uint32_t *pcSysRegRanges, PCCPUMSYSREGRANGE pNewRange)
{
    Assert(pNewRange->uLast >= pNewRange->uFirst);
    Assert(pNewRange->enmRdFn > kCpumSysRegRdFn_Invalid && pNewRange->enmRdFn < kCpumSysRegRdFn_End);
    Assert(pNewRange->enmWrFn > kCpumSysRegWrFn_Invalid && pNewRange->enmWrFn < kCpumSysRegWrFn_End);

    /*
     * Validate and use the VM's system register ranges array if we are using the hyper heap.
     */
    if (pVM)
    {
        AssertReturn(!ppaSysRegRanges, VERR_INVALID_PARAMETER);
        AssertReturn(!pcSysRegRanges,  VERR_INVALID_PARAMETER);

        ppaSysRegRanges = &pVM->cpum.s.GuestInfo.paSysRegRangesR3;
        pcSysRegRanges  = &pVM->cpum.s.GuestInfo.cSysRegRanges;
    }
    else
    {
        AssertReturn(ppaSysRegRanges, VERR_INVALID_POINTER);
        AssertReturn(pcSysRegRanges, VERR_INVALID_POINTER);
    }

    uint32_t            cSysRegRanges  = *pcSysRegRanges;
    PCPUMSYSREGRANGE    paSysRegRanges = *ppaSysRegRanges;

    /*
     * Optimize the linear insertion case where we add new entries at the end.
     */
    if (   cSysRegRanges > 0
        && paSysRegRanges[cSysRegRanges - 1].uLast < pNewRange->uFirst)
    {
        paSysRegRanges = cpumR3SysRegRangesEnsureSpace(pVM, ppaSysRegRanges, cSysRegRanges, 1);
        if (!paSysRegRanges)
            return VERR_NO_MEMORY;
        paSysRegRanges[cSysRegRanges] = *pNewRange;
        *pcSysRegRanges += 1;
    }
    else
    {
        uint32_t i = cpumR3SysRegRangesBinSearch(paSysRegRanges, cSysRegRanges, pNewRange->uFirst);
        Assert(i == cSysRegRanges || pNewRange->uFirst <= paSysRegRanges[i].uLast);
        Assert(i == 0 || pNewRange->uFirst > paSysRegRanges[i - 1].uLast);

        /*
         * Adding an entirely new entry?
         */
        if (   i >= cSysRegRanges
            || pNewRange->uLast < paSysRegRanges[i].uFirst)
        {
            paSysRegRanges = cpumR3SysRegRangesEnsureSpace(pVM, ppaSysRegRanges, cSysRegRanges, 1);
            if (!paSysRegRanges)
                return VERR_NO_MEMORY;
            if (i < cSysRegRanges)
                memmove(&paSysRegRanges[i + 1], &paSysRegRanges[i], (cSysRegRanges - i) * sizeof(paSysRegRanges[0]));
            paSysRegRanges[i] = *pNewRange;
            *pcSysRegRanges += 1;
        }
        /*
         * Replace existing entry?
         */
        else if (   pNewRange->uFirst == paSysRegRanges[i].uFirst
                 && pNewRange->uLast  == paSysRegRanges[i].uLast)
            paSysRegRanges[i] = *pNewRange;
        /*
         * Splitting an existing entry?
         */
        else if (   pNewRange->uFirst > paSysRegRanges[i].uFirst
                 && pNewRange->uLast  < paSysRegRanges[i].uLast)
        {
            paSysRegRanges = cpumR3SysRegRangesEnsureSpace(pVM, ppaSysRegRanges, cSysRegRanges, 2);
            if (!paSysRegRanges)
                return VERR_NO_MEMORY;
            if (i < cSysRegRanges)
                memmove(&paSysRegRanges[i + 2], &paSysRegRanges[i], (cSysRegRanges - i) * sizeof(paSysRegRanges[0]));
            paSysRegRanges[i + 1] = *pNewRange;
            paSysRegRanges[i + 2] = paSysRegRanges[i];
            paSysRegRanges[i    ].uLast  = pNewRange->uFirst - 1;
            paSysRegRanges[i + 2].uFirst = pNewRange->uLast  + 1;
            *pcSysRegRanges += 2;
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
            if (   pNewRange->uFirst <= paSysRegRanges[i].uFirst
                && pNewRange->uLast  <  paSysRegRanges[i].uLast)
                paSysRegRanges[i].uFirst = pNewRange->uLast + 1;
            else
            {
                /* Adjust end of first match? */
                if (pNewRange->uFirst > paSysRegRanges[i].uFirst)
                {
                    Assert(paSysRegRanges[i].uLast >= pNewRange->uFirst);
                    paSysRegRanges[i].uLast = pNewRange->uFirst - 1;
                    i++;
                }
                /* Replace the whole first match (lazy bird). */
                else
                {
                    if (i + 1 < cSysRegRanges)
                        memmove(&paSysRegRanges[i], &paSysRegRanges[i + 1], (cSysRegRanges - i - 1) * sizeof(paSysRegRanges[0]));
                    cSysRegRanges = *pcSysRegRanges -= 1;
                }

                /* Do the new range affect more ranges? */
                while (   i < cSysRegRanges
                       && pNewRange->uLast >= paSysRegRanges[i].uFirst)
                {
                    if (pNewRange->uLast < paSysRegRanges[i].uLast)
                    {
                        /* Adjust the start of it, then we're done. */
                        paSysRegRanges[i].uFirst = pNewRange->uLast + 1;
                        break;
                    }

                    /* Remove it entirely. */
                    if (i + 1 < cSysRegRanges)
                        memmove(&paSysRegRanges[i], &paSysRegRanges[i + 1], (cSysRegRanges - i - 1) * sizeof(paSysRegRanges[0]));
                    cSysRegRanges = *pcSysRegRanges -= 1;
                }
            }

            /* Now, perform a normal insertion. */
            paSysRegRanges = cpumR3SysRegRangesEnsureSpace(pVM, ppaSysRegRanges, cSysRegRanges, 1);
            if (!paSysRegRanges)
                return VERR_NO_MEMORY;
            if (i < cSysRegRanges)
                memmove(&paSysRegRanges[i + 1], &paSysRegRanges[i], (cSysRegRanges - i) * sizeof(paSysRegRanges[0]));
            paSysRegRanges[i] = *pNewRange;
            *pcSysRegRanges += 1;
        }
    }

    return VINF_SUCCESS;
}


/**
 * Insert an system register range into the VM.
 *
 * If the new system register range overlaps existing ranges, the existing ones will be
 * adjusted/removed to fit in the new one.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pNewRange           Pointer to the MSR range being inserted.
 */
VMMR3DECL(int) CPUMR3SysRegRangesInsert(PVM pVM, PCCPUMSYSREGRANGE pNewRange)
{
    AssertReturn(pVM, VERR_INVALID_PARAMETER);
    AssertReturn(pNewRange, VERR_INVALID_PARAMETER);

    return cpumR3SysRegRangesInsert(pVM, NULL /* ppaSysRegRanges */, NULL /* pcSysRegRanges */, pNewRange);
}

#endif /* !CPUM_DB_STANDALONE */

