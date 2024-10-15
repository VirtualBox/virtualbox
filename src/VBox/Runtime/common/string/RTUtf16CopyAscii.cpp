/* $Id: RTUtf16CopyAscii.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTUtf16CopyAscii.
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

#include <iprt/errcore.h>


RTDECL(int) RTUtf16CopyAscii(PRTUTF16 pwszDst, size_t cwcDst, const char *pszSrc)
{
    int rc;
    size_t cchSrc = strlen(pszSrc);
    size_t cchCopy;
    if (RT_LIKELY(cchSrc < cwcDst))
    {
        rc = VINF_SUCCESS;
        cchCopy = cchSrc;
    }
    else if (cwcDst != 0)
    {
        rc = VERR_BUFFER_OVERFLOW;
        cchCopy = cwcDst - 1;
    }
    else
        return VERR_BUFFER_OVERFLOW;

    pwszDst[cchCopy] = '\0';
    while (cchCopy-- > 0)
    {
        unsigned char ch = pszSrc[cchCopy];
        if (RT_LIKELY(ch < 0x80))
            pwszDst[cchCopy] = ch;
        else
        {
            AssertMsgFailed(("ch=%#x\n", ch));
            pwszDst[cchCopy] = 0x7f;
            if (rc == VINF_SUCCESS)
                rc = VERR_OUT_OF_RANGE;
        }
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTUtf16CopyAscii);

