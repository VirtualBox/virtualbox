/* $Id: RTPathSplitReassemble.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathSplitReassemble.
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


RTDECL(int) RTPathSplitReassemble(PRTPATHSPLIT pSplit, uint32_t fFlags, char *pszDstPath, size_t cbDstPath)
{
    /*
     * Input validation.
     */
    AssertPtrReturn(pSplit, VERR_INVALID_POINTER);
    AssertReturn(pSplit->cComps > 0, VERR_INVALID_PARAMETER);
    AssertReturn(RTPATH_STR_F_IS_VALID(fFlags, 0) && !(fFlags & RTPATH_STR_F_MIDDLE), VERR_INVALID_FLAGS);
    AssertPtrReturn(pszDstPath, VERR_INVALID_POINTER);
    AssertReturn(cbDstPath > pSplit->cchPath, VERR_BUFFER_OVERFLOW);

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
     */
    uint32_t const  cchOrgPath = pSplit->cchPath;
    size_t          cchDstPath = 0;
    uint32_t const  cComps     = pSplit->cComps;
    uint32_t        idxComp    = 0;
    char           *pszDst     = pszDstPath;
    size_t          cchComp;

    if (RTPATH_PROP_HAS_ROOT_SPEC(pSplit->fProps))
    {
        cchComp = strlen(pSplit->apszComps[0]);
        cchDstPath += cchComp;
        AssertReturn(cchDstPath <= cchOrgPath, VERR_INVALID_PARAMETER);
        memcpy(pszDst, pSplit->apszComps[0], cchComp);

        /* fix the slashes */
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
        cchComp = strlen(pSplit->apszComps[idxComp]);
        cchDstPath += cchComp;
        AssertReturn(cchDstPath <= cchOrgPath, VERR_INVALID_PARAMETER);
        memcpy(pszDst, pSplit->apszComps[idxComp], cchComp);
        pszDst += cchComp;
        idxComp++;
        if (idxComp != cComps || (pSplit->fProps & RTPATH_PROP_DIR_SLASH))
        {
            cchDstPath++;
            AssertReturn(cchDstPath <= cchOrgPath, VERR_INVALID_PARAMETER);
            *pszDst++ = chSlash;
        }
    }

    *pszDst = '\0';

    return VINF_SUCCESS;
}

