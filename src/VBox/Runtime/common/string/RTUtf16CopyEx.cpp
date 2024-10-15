/* $Id: RTUtf16CopyEx.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTUtf16CopyEx.
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


RTDECL(int) RTUtf16CopyEx(PRTUTF16 pwszDst, size_t cwcDst, PCRTUTF16 pwszSrc, size_t cwcSrcMax)
{
    size_t cwcSrc = RTUtf16NLen(pwszSrc, cwcSrcMax);
    if (RT_LIKELY(cwcSrc < cwcDst))
    {
        memcpy(pwszDst, pwszSrc, cwcSrc * sizeof(RTUTF16));
        pwszDst[cwcSrc] = '\0';
        return VINF_SUCCESS;
    }

    if (cwcDst != 0)
    {
        memcpy(pwszDst, pwszSrc, (cwcDst - 1) * sizeof(RTUTF16));
        pwszDst[cwcDst - 1] = '\0';
    }
    return VERR_BUFFER_OVERFLOW;
}
RT_EXPORT_SYMBOL(RTUtf16CopyEx);

