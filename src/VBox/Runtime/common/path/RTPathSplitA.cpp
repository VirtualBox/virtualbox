/* $Id: RTPathSplitA.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathSplitA and RTPathSplitFree.
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
#include <iprt/mem.h>
#include <iprt/string.h>



RTDECL(int) RTPathSplitATag(const char *pszPath, PRTPATHSPLIT *ppSplit, uint32_t fFlags, const char *pszTag)
{
    AssertPtrReturn(ppSplit, VERR_INVALID_POINTER);
    *ppSplit = NULL;

    /*
     * Try estimate a reasonable buffer size based on the path length.
     * Note! No point in trying very hard to get it right.
     */
    size_t cbSplit = strlen(pszPath);
    cbSplit += RT_UOFFSETOF_DYN(RTPATHSPLIT, apszComps[cbSplit / 8]) + cbSplit / 8 + 8;
    cbSplit = RT_ALIGN(cbSplit, 64);
    PRTPATHSPLIT pSplit = (PRTPATHSPLIT)RTMemAllocTag(cbSplit, pszTag);
    if (pSplit == NULL)
        return VERR_NO_MEMORY;

    /*
     * First try. If it fails due to buffer, reallocate the buffer and try again.
     */
    int rc = RTPathSplit(pszPath, pSplit, cbSplit, fFlags);
    if (rc == VERR_BUFFER_OVERFLOW)
    {
        cbSplit = RT_ALIGN(pSplit->cbNeeded, 64);
        RTMemFree(pSplit);

        pSplit = (PRTPATHSPLIT)RTMemAllocTag(cbSplit, pszTag);
        if (pSplit == NULL)
            return VERR_NO_MEMORY;
        rc = RTPathSplit(pszPath, pSplit, cbSplit, fFlags);
    }

    /*
     * Done (one way or the other).
     */
    if (RT_SUCCESS(rc))
        *ppSplit = pSplit;
    else
        RTMemFree(pSplit);
    return rc;
}


RTDECL(void) RTPathSplitFree(PRTPATHSPLIT pSplit)
{
    if (pSplit)
    {
        Assert(pSplit->u16Reserved = UINT16_C(0xbeef));
        RTMemFree(pSplit);
    }
}

