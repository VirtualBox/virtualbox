/* $Id: RTPathParsedReassemble.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathParsedReassemble.
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
#include <iprt/errcore.h>
#include <iprt/string.h>


RTDECL(int) RTPathParsedReassemble(const char *pszSrcPath, PRTPATHPARSED pParsed, uint32_t fFlags,
                                   char *pszDstPath, size_t cbDstPath)
{
    /*
     * Input validation.
     */
    AssertPtrReturn(pszSrcPath, VERR_INVALID_POINTER);
    AssertPtrReturn(pParsed, VERR_INVALID_POINTER);
    AssertReturn(pParsed->cComps > 0, VERR_INVALID_PARAMETER);
    AssertReturn(RTPATH_STR_F_IS_VALID(fFlags, 0) && !(fFlags & RTPATH_STR_F_MIDDLE), VERR_INVALID_FLAGS);
    AssertPtrReturn(pszDstPath, VERR_INVALID_POINTER);

    /*
     * Recalculate the length.
     */
    uint32_t const  cComps  = pParsed->cComps;
    uint32_t        idxComp = 0;
    uint32_t        cchPath = 0;
    if (RTPATH_PROP_HAS_ROOT_SPEC(pParsed->fProps))
    {
        cchPath = pParsed->aComps[0].cch;
        idxComp++;
    }
    bool fNeedSlash = false;
    while (idxComp < cComps)
    {
        uint32_t const cchComp = pParsed->aComps[idxComp].cch;
        idxComp++;
        if (cchComp > 0)
        {
            cchPath += cchComp + fNeedSlash;
            fNeedSlash = true;
        }
    }
    if ((pParsed->fProps & RTPATH_PROP_DIR_SLASH) && fNeedSlash)
        cchPath += 1;
    pParsed->cchPath = cchPath;
    if (cbDstPath > cchPath)
    { /* likely */ }
    else
    {
        if (cbDstPath)
            *pszDstPath = '\0';
        return VERR_BUFFER_OVERFLOW;
    }

    /*
     * Figure which slash to use.
     */
    char chSlash;
    switch (fFlags & RTPATH_STR_F_STYLE_MASK)
    {
        case RTPATH_STR_F_STYLE_HOST:
            chSlash = RTPATH_SLASH;
            break;

        case RTPATH_STR_F_STYLE_DOS:
            chSlash = '\\';
            break;

        case RTPATH_STR_F_STYLE_UNIX:
            chSlash = '/';
            break;

        default:
            AssertFailedReturn(VERR_INVALID_FLAGS); /* impossible */
    }

    /*
     * Do the joining.
     * Note! Using memmove here as we want to support pszSrcPath == pszDstPath.
     */
    char *pszDst = pszDstPath;
    idxComp      = 0;
    fNeedSlash   = false;

    if (RTPATH_PROP_HAS_ROOT_SPEC(pParsed->fProps))
    {
        uint32_t cchComp = pParsed->aComps[0].cch;
        memmove(pszDst, &pszSrcPath[pParsed->aComps[0].off], cchComp);

        /* fix the slashes (harmless for unix style) */
        char chOtherSlash = chSlash == '\\' ? '/' : '\\';
        while (cchComp-- > 0)
        {
            if (*pszDst == chOtherSlash)
                *pszDst = chSlash;
            pszDst++;
        }
        idxComp = 1;
    }

    while (idxComp < cComps)
    {
        uint32_t const cchComp = pParsed->aComps[idxComp].cch;
        if (cchComp > 0)
        {
            if (fNeedSlash)
                *pszDst++ = chSlash;
            fNeedSlash = true;
            memmove(pszDst, &pszSrcPath[pParsed->aComps[idxComp].off], cchComp);
            pszDst += cchComp;
        }
        idxComp++;
    }

    if ((pParsed->fProps & RTPATH_PROP_DIR_SLASH) && fNeedSlash)
        *pszDst++ = chSlash;
    *pszDst = '\0';

    return VINF_SUCCESS;
}

