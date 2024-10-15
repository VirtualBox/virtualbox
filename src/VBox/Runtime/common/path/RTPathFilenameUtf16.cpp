/* $Id: RTPathFilenameUtf16.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTPathFilenameUtf16
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



RTDECL(PRTUTF16) RTPathFilenameUtf16(PCRTUTF16 pwszPath)
{
    return RTPathFilenameExUtf16(pwszPath, RTPATH_STYLE);
}
RT_EXPORT_SYMBOL(RTPathFilenameUtf16);


RTDECL(PRTUTF16) RTPathFilenameExUtf16(PCRTUTF16 pwszPath, uint32_t fFlags)
{
    PCRTUTF16 pwsz = pwszPath;
    PCRTUTF16 pwszName = pwszPath;

    Assert(RTPATH_STR_F_IS_VALID(fFlags, 0 /*no extra flags*/));
    fFlags &= RTPATH_STR_F_STYLE_MASK;
    if (fFlags == RTPATH_STR_F_STYLE_HOST)
        fFlags = RTPATH_STYLE;
    if (fFlags == RTPATH_STR_F_STYLE_DOS)
    {
        for (;; pwsz++)
        {
            switch (*pwsz)
            {
                /* handle separators. */
                case ':':
                case '\\':
                case '/':
                    pwszName = pwsz + 1;
                    break;

                /* the end */
                case '\0':
                    if (*pwszName)
                        return (PRTUTF16)(void *)pwszName;
                    return NULL;
            }
        }
    }
    else
    {
        Assert(fFlags == RTPATH_STR_F_STYLE_UNIX);
        for (;; pwsz++)
        {
            switch (*pwsz)
            {
                /* handle separators. */
                case '/':
                    pwszName = pwsz + 1;
                    break;

                /* the end */
                case '\0':
                    if (*pwszName)
                        return (PRTUTF16)(void *)pwszName;
                    return NULL;
            }
        }
    }

    /* not reached */
}
RT_EXPORT_SYMBOL(RTPathFilenameExUtf16);

