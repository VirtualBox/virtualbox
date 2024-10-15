/* $Id: RTStrCatP.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTStrCat.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/string.h>
#include "internal/iprt.h"

#include <iprt/errcore.h>


RTDECL(int) RTStrCatP(char **ppszDst, size_t *pcbDst, const char *pszSrc)
{
    /*
     * Advance past the current string in the output buffer and turn this into
     * a copy operation.
     */
    char   *pszDstOrg = *ppszDst;
    size_t  cbDst     = *pcbDst;
    char   *pszDst    = RTStrEnd(pszDstOrg, cbDst);
    AssertReturn(pszDst, VERR_INVALID_PARAMETER);
    *ppszDst = pszDst;
    *pcbDst  = cbDst - (pszDst - pszDstOrg);

    return RTStrCopyP(ppszDst, pcbDst, pszSrc);
}
RT_EXPORT_SYMBOL(RTStrCatP);

