/* $Id: RTPathEnsureTrailingSeparator.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathEnsureTrailingSeparator & RTPathEnsureTrailingSeparatorEx
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
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Slash character indexed by path style. */
static char g_achSlashes[] =
{
    /*[RTPATH_STR_F_STYLE_HOST] =*/     RTPATH_SLASH,
    /*[RTPATH_STR_F_STYLE_DOS] =*/      '\\',
    /*[RTPATH_STR_F_STYLE_UNIX] =*/     '/',
    /*[RTPATH_STR_F_STYLE_RESERVED] =*/ '!',
};
AssertCompile(RTPATH_STR_F_STYLE_HOST == 0);
AssertCompile(RTPATH_STR_F_STYLE_DOS  == 1);
AssertCompile(RTPATH_STR_F_STYLE_UNIX == 2);


RTDECL(size_t) RTPathEnsureTrailingSeparatorEx(char *pszPath, size_t cbPath, uint32_t fFlags)
{
    Assert(RTPATH_STR_F_IS_VALID(fFlags, 0));

    size_t off = strlen(pszPath);
    if (off > 0)
    {
        char ch = pszPath[off - 1];
        if (ch == '/')
            return off;
        if (   (ch == ':' || ch == '\\')
            && (   (fFlags & RTPATH_STR_F_STYLE_MASK) == RTPATH_STR_F_STYLE_DOS
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
                || (fFlags & RTPATH_STR_F_STYLE_MASK) == RTPATH_STR_F_STYLE_HOST
#endif
               ))
            return off;

        if (off + 2 <= cbPath)
        {
            pszPath[off++] = g_achSlashes[fFlags & RTPATH_STR_F_STYLE_MASK];
            pszPath[off]   = '\0';
            return off;
        }
    }
    else if (off + 3 <= cbPath)
    {
        pszPath[off++] = '.';
        pszPath[off++] =  g_achSlashes[fFlags & RTPATH_STR_F_STYLE_MASK];
        pszPath[off]   = '\0';
        return off;
    }

    return 0;
}
RT_EXPORT_SYMBOL(RTPathEnsureTrailingSeparatorEx);


RTDECL(size_t) RTPathEnsureTrailingSeparator(char *pszPath, size_t cbPath)
{
    return RTPathEnsureTrailingSeparatorEx(pszPath, cbPath, RTPATH_STR_F_STYLE_HOST);
}
RT_EXPORT_SYMBOL(RTPathEnsureTrailingSeparator);

