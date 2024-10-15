/* $Id: RTPathParentLength.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathParentLength
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/string.h>

#define RTPATH_TEMPLATE_CPP_H "RTPathParentLength.cpp.h"
#include "rtpath-expand-template.cpp.h"



RTDECL(size_t) RTPathParentLengthEx(const char *pszPath, uint32_t fFlags)
{
    /*
     * Input validation.
     */
    AssertPtrReturn(pszPath, 0);
    AssertReturn(*pszPath, 0);
    AssertReturn(RTPATH_STR_F_IS_VALID(fFlags, 0), 0);
    Assert(!(fFlags & RTPATH_STR_F_NO_END));

    /*
     * Invoke the worker for the selected path style.
     */
    switch (fFlags & RTPATH_STR_F_STYLE_MASK)
    {
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
        case RTPATH_STR_F_STYLE_HOST:
#endif
        case RTPATH_STR_F_STYLE_DOS:
            return rtPathParentLengthStyleDos(pszPath, fFlags);

#if RTPATH_STYLE != RTPATH_STR_F_STYLE_DOS
        case RTPATH_STR_F_STYLE_HOST:
#endif
        case RTPATH_STR_F_STYLE_UNIX:
            return rtPathParentLengthStyleUnix(pszPath, fFlags);

        default:
            AssertFailedReturn(0); /* impossible */
    }
}


RTDECL(size_t) RTPathParentLength(const char *pszPath)
{
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
    return rtPathParentLengthStyleDos(pszPath, 0);
#else
    return rtPathParentLengthStyleUnix(pszPath, 0);
#endif
}

