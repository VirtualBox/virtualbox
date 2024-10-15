/* $Id: RTPathSplit.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathSplit
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
#include "internal/iprt.h"
#include <iprt/path.h>

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/string.h>



RTDECL(int) RTPathSplit(const char *pszPath, PRTPATHSPLIT pSplit, size_t cbSplit, uint32_t fFlags)
{
    /*
     * Input validation.
     */
    AssertReturn(cbSplit >= RT_UOFFSETOF(RTPATHSPLIT, apszComps), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pSplit, VERR_INVALID_POINTER);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_PATH_ZERO_LENGTH);
    AssertReturn(RTPATH_STR_F_IS_VALID(fFlags, 0), VERR_INVALID_FLAGS);

    /*
     * Use RTPathParse to do the parsing.
     * - This makes the ASSUMPTION that the output of this function is greater
     *   or equal to that of RTPathParsed.
     * - We're aliasing the buffer here, so use volatile to avoid issues due to
     *   compiler optimizations.
     */
    RTPATHPARSED volatile  *pParsedVolatile = (RTPATHPARSED volatile *)pSplit;
    RTPATHSPLIT  volatile  *pSplitVolatile  = (RTPATHSPLIT  volatile *)pSplit;

    AssertCompile(sizeof(*pParsedVolatile) <= sizeof(*pSplitVolatile));
    AssertCompile(sizeof(pParsedVolatile->aComps[0]) <= sizeof(pSplitVolatile->apszComps[0]));

    int rc = RTPathParse(pszPath, (PRTPATHPARSED)pParsedVolatile, cbSplit, fFlags);
    if (RT_FAILURE(rc) && rc != VERR_BUFFER_OVERFLOW)
        return rc;

    /*
     * Calculate the required buffer space.
     */
    uint16_t const  cComps    = pParsedVolatile->cComps;
    uint16_t const  fProps    = pParsedVolatile->fProps;
    uint16_t const  cchPath   = pParsedVolatile->cchPath;
    uint16_t const  offSuffix = pParsedVolatile->offSuffix;
    uint32_t        cbNeeded  = RT_UOFFSETOF_DYN(RTPATHSPLIT, apszComps[cComps])
                              + cchPath
                              + RTPATH_PROP_FIRST_NEEDS_NO_SLASH(fProps) /* zero terminator for root spec. */
                              - RT_BOOL(fProps & RTPATH_PROP_DIR_SLASH) /* counted by cchPath, not included in the comp str. */
                              + 1; /* zero terminator. */
    if (cbNeeded > cbSplit)
    {
        pSplitVolatile->cbNeeded = cbNeeded;
        return VERR_BUFFER_OVERFLOW;
    }
    Assert(RT_SUCCESS(rc));

    /*
     * Convert the array and copy the strings, both backwards.
     */
    char    *psz     = (char *)pSplit + cbNeeded;
    uint32_t idxComp = cComps - 1;

    /* the final component first (because of suffix handling). */
    uint16_t offComp = pParsedVolatile->aComps[idxComp].off;
    uint16_t cchComp = pParsedVolatile->aComps[idxComp].cch;

    *--psz = '\0';
    psz -= cchComp;
    memcpy(psz, &pszPath[offComp], cchComp);
    pSplitVolatile->apszComps[idxComp] = psz;

    char *pszSuffix;
    if (offSuffix >= offComp + cchComp)
        pszSuffix = &psz[cchComp];
    else
        pszSuffix = &psz[offSuffix - offComp];

    /* the remainder */
    while (idxComp-- > 0)
    {
        offComp = pParsedVolatile->aComps[idxComp].off;
        cchComp = pParsedVolatile->aComps[idxComp].cch;
        *--psz = '\0';
        psz -= cchComp;
        memcpy(psz, &pszPath[offComp], cchComp);
        pSplitVolatile->apszComps[idxComp] = psz;
    }

    /*
     * Store / reshuffle the non-array bits. This MUST be done after finishing
     * the array processing because there may be members in RTPATHSPLIT
     * overlapping the array of RTPATHPARSED.
     */
    AssertCompileMembersSameSizeAndOffset(RTPATHPARSED, cComps,  RTPATHSPLIT, cComps);  Assert(pSplitVolatile->cComps == cComps);
    AssertCompileMembersSameSizeAndOffset(RTPATHPARSED, fProps,  RTPATHSPLIT, fProps);  Assert(pSplitVolatile->fProps == fProps);
    AssertCompileMembersSameSizeAndOffset(RTPATHPARSED, cchPath, RTPATHSPLIT, cchPath); Assert(pSplitVolatile->cchPath == cchPath);
    pSplitVolatile->u16Reserved = 0;
    pSplitVolatile->cbNeeded    = cbNeeded;
    pSplitVolatile->pszSuffix   = pszSuffix;

    return rc;
}

