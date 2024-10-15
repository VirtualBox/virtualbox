/* $Id: strprintf2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - String Formatters, alternative.
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
#include <iprt/string.h>
#include "internal/iprt.h"

#include <iprt/assert.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** rtStrPrintf2Output() argument structure. */
typedef struct STRPRINTF2OUTPUTARGS
{
    /** Pointer to current buffer position. */
    char   *pszCur;
    /** Number of bytes left in the buffer (including the trailing zero). */
    size_t  cbLeft;
    /** Set if we overflowed. */
    bool    fOverflowed;
} STRPRINTF2OUTPUTARGS;
/** Pointer to a rtStrPrintf2Output() argument structure. */
typedef STRPRINTF2OUTPUTARGS *PSTRPRINTF2OUTPUTARGS;


/**
 * Output callback.
 *
 * @returns cbChars
 *
 * @param   pvArg       Pointer to a STRBUFARG structure.
 * @param   pachChars   Pointer to an array of utf-8 characters.
 * @param   cbChars     Number of bytes in the character array pointed to by pachChars.
 */
static DECLCALLBACK(size_t) rtStrPrintf2Output(void *pvArg, const char *pachChars, size_t cbChars)
{
    PSTRPRINTF2OUTPUTARGS pArgs = (PSTRPRINTF2OUTPUTARGS)pvArg;
    char *pszCur = pArgs->pszCur; /* We actually have to spell this out for VS2010, or it will load it for each case. */

    if (cbChars < pArgs->cbLeft)
    {
        pArgs->cbLeft -= cbChars;

        /* Note! For VS2010/64 we need at least 7 case statements before it generates a jump table. */
        switch (cbChars)
        {
            default:
                memcpy(pszCur, pachChars, cbChars);
                break;
            case 8: pszCur[7] = pachChars[7]; RT_FALL_THRU();
            case 7: pszCur[6] = pachChars[6]; RT_FALL_THRU();
            case 6: pszCur[5] = pachChars[5]; RT_FALL_THRU();
            case 5: pszCur[4] = pachChars[4]; RT_FALL_THRU();
            case 4: pszCur[3] = pachChars[3]; RT_FALL_THRU();
            case 3: pszCur[2] = pachChars[2]; RT_FALL_THRU();
            case 2: pszCur[1] = pachChars[1]; RT_FALL_THRU();
            case 1: pszCur[0] = pachChars[0]; RT_FALL_THRU();
            case 0:
                break;
        }
        pArgs->pszCur = pszCur += cbChars;
        *pszCur = '\0';
    }
    else
    {
        size_t cbLeft = pArgs->cbLeft;
        if (cbLeft-- > 1)
        {
            memcpy(pszCur, pachChars, cbLeft);
            pArgs->pszCur = pszCur += cbLeft;
            *pszCur = '\0';
            pArgs->cbLeft = 1;
        }
        pArgs->fOverflowed = true;
    }

    return cbChars;
}


RTDECL(ssize_t) RTStrPrintf2V(char *pszBuffer, size_t cchBuffer, const char *pszFormat, va_list args)
{
    STRPRINTF2OUTPUTARGS Args;
    size_t cchRet;
    AssertMsg(cchBuffer > 0, ("Excellent idea! Format a string with no space for the output!\n"));

    Args.pszCur      = pszBuffer;
    Args.cbLeft      = cchBuffer;
    Args.fOverflowed = false;

    cchRet = RTStrFormatV(rtStrPrintf2Output, &Args, NULL, NULL, pszFormat, args);

    return !Args.fOverflowed ? (ssize_t)cchRet : -(ssize_t)cchRet - 1;
}
RT_EXPORT_SYMBOL(RTStrPrintf2V);


RTDECL(ssize_t) RTStrPrintf2ExV(PFNSTRFORMAT pfnFormat, void *pvArg, char *pszBuffer, size_t cchBuffer,
                                const char *pszFormat,  va_list args)
{
    STRPRINTF2OUTPUTARGS Args;
    size_t cchRet;
    AssertMsg(cchBuffer > 0, ("Excellent idea! Format a string with no space for the output!\n"));

    Args.pszCur      = pszBuffer;
    Args.cbLeft      = cchBuffer;
    Args.fOverflowed = false;
    cchRet = RTStrFormatV(rtStrPrintf2Output, &Args, pfnFormat, pvArg, pszFormat, args);
    return !Args.fOverflowed ? (ssize_t)cchRet : -(ssize_t)cchRet - 1;
}
RT_EXPORT_SYMBOL(RTStrPrintf2ExV);

