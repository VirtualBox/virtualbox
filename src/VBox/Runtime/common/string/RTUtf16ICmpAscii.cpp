/* $Id: RTUtf16ICmpAscii.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTUtf16ICmpAscii.
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

#include <iprt/assert.h>
#include <iprt/ctype.h>


RTDECL(int) RTUtf16ICmpAscii(PCRTUTF16 pwsz1, const char *psz2)
{
    for (;;)
    {
        RTUTF16         wc1  = *pwsz1++;
        unsigned char   uch2 = *psz2++; Assert(uch2 < 0x80);
        if (wc1 != uch2)
        {
            if (wc1 >= 0x80)
                return 1;
            if (RT_C_TO_LOWER(wc1) != RT_C_TO_LOWER(uch2))
                return wc1 < uch2 ? -1 : 1;
        }
        if (!uch2)
            return 0;
    }
}
RT_EXPORT_SYMBOL(RTUtf16ICmpAscii);

