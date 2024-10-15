/* $Id: RTPathFindCommon.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathFindCommon implementations.
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
#include "internal/iprt.h"
#include <iprt/path.h>

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/uni.h>


#define RTPATH_TEMPLATE_CPP_H "RTPathFindCommon.cpp.h"
#include "rtpath-expand-template.cpp.h"


RTDECL(size_t) RTPathFindCommonEx(size_t cPaths, const char * const *papszPaths, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    AssertReturn(RTPATH_STR_F_IS_VALID(fFlags, RTPATHFINDCOMMON_F_IGNORE_DOTDOT), 0);
    AssertReturn(cPaths > 0, 0);
    AssertPtrReturn(papszPaths, 0);
    size_t i = cPaths;
    while (i-- > 0)
        AssertPtrReturn(papszPaths[i], 0);

    /*
     * Duplicate papszPaths so we can have individual positions in each path.
     * Use the stack if we haven't got too many paths.
     */
    void        *pvFree;
    const char **papszCopy;
    size_t      cbNeeded = cPaths * sizeof(papszCopy[0]);
    if (cbNeeded <= _2K)
    {
        pvFree = NULL;
        papszCopy = (const char **)alloca(cbNeeded);
    }
    else
    {
        pvFree = RTMemTmpAlloc(cbNeeded);
        papszCopy = (const char **)pvFree;
    }
    AssertReturn(papszCopy, 0);
    memcpy(papszCopy, papszPaths, cbNeeded);

    /*
     * Invoke the worker for the selected path style.
     */
    size_t cchRet;
    switch (fFlags & RTPATH_STR_F_STYLE_MASK)
    {
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
        case RTPATH_STR_F_STYLE_HOST:
#endif
        case RTPATH_STR_F_STYLE_DOS:
            cchRet= rtPathFindCommonStyleDos(cPaths, papszCopy, fFlags);
            break;

#if RTPATH_STYLE != RTPATH_STR_F_STYLE_DOS
        case RTPATH_STR_F_STYLE_HOST:
#endif
        case RTPATH_STR_F_STYLE_UNIX:
            cchRet = rtPathFindCommonStyleUnix(cPaths, papszCopy, fFlags);
            break;

        default:
            AssertFailedStmt(cchRet = 0); /* impossible */
    }

    /*
     * Clean up and return.
     */
    if (pvFree)
        RTMemTmpFree(pvFree);
    return cchRet;
}
RT_EXPORT_SYMBOL(RTPathFindCommonEx);


RTDECL(size_t) RTPathFindCommon(size_t cPaths, const char * const *papszPaths)
{
    return RTPathFindCommonEx(cPaths, papszPaths, RTPATH_STR_F_STYLE_HOST);
}
RT_EXPORT_SYMBOL(RTPathFindCommon);

