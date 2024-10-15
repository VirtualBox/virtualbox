/* $Id: RTPathParse.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathParse
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#define RTPATH_TEMPLATE_CPP_H "RTPathParse.cpp.h"
#include "rtpath-expand-template.cpp.h"


RTDECL(int) RTPathParse(const char *pszPath, PRTPATHPARSED pParsed, size_t cbParsed, uint32_t fFlags)
{
    /*
     * Input validation.
     */
    AssertReturn(cbParsed >= RT_UOFFSETOF(RTPATHPARSED, aComps), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pParsed, VERR_INVALID_POINTER);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_PATH_ZERO_LENGTH);
    AssertReturn(RTPATH_STR_F_IS_VALID(fFlags, 0), VERR_INVALID_FLAGS);

    /*
     * Invoke the worker for the selected path style.
     */
    switch (fFlags & RTPATH_STR_F_STYLE_MASK)
    {
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
        case RTPATH_STR_F_STYLE_HOST:
#endif
        case RTPATH_STR_F_STYLE_DOS:
            return rtPathParseStyleDos(pszPath, pParsed, cbParsed, fFlags);

#if !defined(RT_OS_OS2) && !defined(RT_OS_WINDOWS)
        case RTPATH_STR_F_STYLE_HOST:
#endif
        case RTPATH_STR_F_STYLE_UNIX:
            return rtPathParseStyleUnix(pszPath, pParsed, cbParsed, fFlags);

        default:
            AssertFailedReturn(VERR_INVALID_FLAGS); /* impossible */
    }
}

