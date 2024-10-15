/* $Id: RTStrSplit.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTStrSplit.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#include <iprt/mem.h>
#include <iprt/string.h>
#include "internal/iprt.h"


RTDECL(int) RTStrSplit(const char *pcszStrings, size_t cbStrings,
                       const char *pcszSeparator, char ***ppapszStrings, size_t *pcStrings)
{
    AssertPtrReturn(pcszStrings, VERR_INVALID_POINTER);
    AssertReturn(cbStrings, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcszSeparator, VERR_INVALID_POINTER);
    AssertPtrReturn(ppapszStrings, VERR_INVALID_POINTER);
    AssertPtrReturn(pcStrings, VERR_INVALID_POINTER);

    size_t cStrings = 0;

    /* Determine the number of paths in buffer first. */
    size_t      cch     = cbStrings - 1;
    char const *pcszTmp = pcszStrings;
    const char *pcszEnd = RTStrEnd(pcszTmp, RTSTR_MAX);
    char const *pcszNext;
    const size_t cchSep = strlen(pcszSeparator);
          size_t cchNext;
    while (cch > 0)
    {
        pcszNext = RTStrStr(pcszTmp, pcszSeparator);
        if (!pcszNext)
            break;
        cchNext = pcszNext - pcszTmp;
        if (cchNext + cchSep > cch)
            break;
        pcszNext += cchSep;
        pcszTmp  += cchNext + cchSep;
        cch      -= cchNext + cchSep;
        if (cchNext)
            ++cStrings;
    }

    if (pcszTmp != pcszEnd) /* Do we need to take a trailing string without separator into account? */
        cStrings++;

    if (!cStrings)
    {
        *ppapszStrings = NULL;
        *pcStrings     = 0;
        return VINF_SUCCESS;
    }

    char **papszStrings = (char **)RTMemAllocZ(cStrings * sizeof(char *));
    if (!papszStrings)
        return VERR_NO_MEMORY;

    int rc = VINF_SUCCESS;

    cch    = cbStrings - 1;
    pcszTmp = pcszStrings;

    for (size_t i = 0; i < cStrings;)
    {
        pcszNext = RTStrStr(pcszTmp, pcszSeparator);
        if (!pcszNext)
            pcszNext = pcszEnd;
        cchNext = pcszNext - pcszTmp;
        if (cchNext)
        {
            papszStrings[i] = RTStrDupN(pcszTmp, cchNext);
            if (!papszStrings[i])
            {
                rc = VERR_NO_MEMORY;
                break;
            }
            i++;
        }
        pcszTmp += cchNext + cchSep;
        cch     -= cchNext + cchSep;
    }

    if (RT_SUCCESS(rc))
    {
        *ppapszStrings = papszStrings;
        *pcStrings     = cStrings;

        return VINF_SUCCESS;
    }

    for (size_t i = 0; i < cStrings; ++i)
        RTStrFree(papszStrings[i]);
    RTMemFree(papszStrings);

    return rc;
}
RT_EXPORT_SYMBOL(RTStrSplit);

