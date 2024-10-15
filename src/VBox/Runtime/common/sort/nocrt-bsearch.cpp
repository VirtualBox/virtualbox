/* $Id: nocrt-bsearch.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - No-CRT - bsearch().
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#define IPRT_NO_CRT_FOR_3RD_PARTY
#include "internal/nocrt.h"
#include <iprt/nocrt/stdlib.h>


void *RT_NOCRT(bsearch)(const void *pvKey, const void *pvBase, size_t cEntries, size_t cbEntry,
                        int (*pfnCompare)(const void *pvKey, const void *pvEntry))
{
    if (cEntries > 0)
    { /* likely */ }
    else
        return NULL;

    size_t iStart = 0;
    size_t iEnd   = cEntries;
    for (;;)
    {
        size_t const        i       = (iEnd - iStart) / 2 + iStart;
        const void * const  pvEntry = (const char *)pvBase + cbEntry * i;
        int const           iDiff   = pfnCompare(pvKey, pvEntry);
        if (iDiff > 0)       /* target is before i */
        {
            if (i > iStart)
                iEnd = i;
            else
                return NULL;
        }
        else if (iDiff)      /* target is after i */
        {
            if (i + 1 < iEnd)
                iStart = i + 1;
            else
                return NULL;
        }
        else                /* match */
            return (void *)pvEntry;
    }
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(bsearch);

