/* $Id: RTUtf16FindAscii.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTUtf16FindAscii.
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
#include <iprt/utf16.h>
#include "internal/iprt.h"

#include <iprt/string.h>


RTDECL(ssize_t) RTUtf16FindAscii(PCRTUTF16 pwszString, const char *pszSubStr)
{
    if (pwszString)
    {
        if (pszSubStr)
        {
            const size_t   cchSubStr = strlen(pszSubStr);
            RTUTF16 const wcFirst   = *pszSubStr;
            Assert(wcFirst < 0x80);
            if (wcFirst != 0)
            {
                PCRTUTF16 const pwszStart = pwszString;
                for (;;)
                {
                    RTUTF16 wc = *pwszString;
                    if (wc)
                    {
                        if (   wc == wcFirst
                            && RTUtf16NCmpAscii(pwszString, pszSubStr, cchSubStr) == 0)
                            return pwszString - pwszStart;
                        pwszString++;
                    }
                    else
                        break;
                }
            }
        }
    }
    return -1;
}
RT_EXPORT_SYMBOL(RTUtf16FindAscii);

