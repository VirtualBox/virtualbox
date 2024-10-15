/* $Id: RTNtPathExpand8dot3PathA.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Native NT, RTNtPathExpand8dot3PathA.
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
#define LOG_GROUP RTLOGGROUP_FS
#ifdef IN_SUP_HARDENED_R3
# include <iprt/nt/nt-and-windows.h>
#else
# include <iprt/nt/nt.h>
#endif

#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/path.h>
#include <iprt/utf16.h>



/**
 * Wrapper around RTNtPathExpand8dot3Path that allocates a buffer instead of
 * working on the input buffer.
 *
 * @returns IPRT status code, see RTNtPathExpand8dot3Path().
 * @param   pUniStrSrc  The path to fix up. MaximumLength is the max buffer
 *                      length.
 * @param   fPathOnly   Whether to only process the path and leave the filename
 *                      as passed in.
 * @param   pUniStrDst  Output string.  On success, the caller must use
 *                      RTUtf16Free to free what the Buffer member points to.
 *                      This is all zeros and NULL on failure.
 */
RTDECL(int) RTNtPathExpand8dot3PathA(PCUNICODE_STRING pUniStrSrc, bool fPathOnly, PUNICODE_STRING pUniStrDst)
{
    /* Guess a reasonable size for the final version. */
    size_t const cbShort = pUniStrSrc->Length;
    size_t       cbLong  = RT_MIN(_64K - 1, cbShort * 8);
    if (cbLong < RTPATH_MAX)
        cbLong = RTPATH_MAX * 2;
    AssertCompile(RTPATH_MAX * 2 < _64K);

    pUniStrDst->Buffer = (WCHAR *)RTUtf16Alloc(cbLong);
    if (pUniStrDst->Buffer != NULL)
    {
        /* Copy over the short name and fix it up. */
        pUniStrDst->MaximumLength = (uint16_t)cbLong;
        pUniStrDst->Length        = (uint16_t)cbShort;
        memcpy(pUniStrDst->Buffer, pUniStrSrc->Buffer, cbShort);
        pUniStrDst->Buffer[cbShort / sizeof(WCHAR)] = '\0';
        int rc = RTNtPathExpand8dot3Path(pUniStrDst, fPathOnly);
        if (RT_SUCCESS(rc))
            return rc;

        /* We failed, bail. */
        RTUtf16Free(pUniStrDst->Buffer);
        pUniStrDst->Buffer    = NULL;
    }
    pUniStrDst->Length        = 0;
    pUniStrDst->MaximumLength = 0;
    return VERR_NO_UTF16_MEMORY;
}

