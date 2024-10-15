/* $Id: bs3-cmn-Printf.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3Printf, Bs3PrintfV
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include "bs3kit-template-header.h"
#include <iprt/ctype.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Output buffering for Bs3TestPrintfV. */
typedef struct BS3PRINTBUF
{
    uint8_t cchBuf;
    char    achBuf[79];
} BS3PRINTBUF;


static BS3_DECL_CALLBACK(size_t) bs3PrintFmtOutput(char ch, void BS3_FAR *pvUser)
{
    BS3PRINTBUF BS3_FAR *pBuf = (BS3PRINTBUF BS3_FAR *)pvUser;
    if (ch != '\0')
    {
        BS3_ASSERT(pBuf->cchBuf < RT_ELEMENTS(pBuf->achBuf));
        pBuf->achBuf[pBuf->cchBuf++] = ch;

        /* Whether to flush the buffer.  We do line flushing here to avoid
           dropping too much info when the formatter crashes on bad input. */
        if (   pBuf->cchBuf < RT_ELEMENTS(pBuf->achBuf)
            && ch != '\n')
            return 1;
    }
    Bs3PrintStrN(&pBuf->achBuf[0], pBuf->cchBuf);
    pBuf->cchBuf = 0;
    return ch != '\0';
}


#undef Bs3PrintfV
BS3_CMN_DEF(size_t, Bs3PrintfV,(const char BS3_FAR *pszFormat, va_list BS3_FAR va))
{
    BS3PRINTBUF Buf;
    Buf.cchBuf = 0;
    return Bs3StrFormatV(pszFormat, va, bs3PrintFmtOutput, &Buf);
}


#undef Bs3Printf
BS3_CMN_DEF(size_t, Bs3Printf,(const char BS3_FAR *pszFormat, ...))
{
    size_t cchRet;
    va_list va;
    va_start(va, pszFormat);
    cchRet = BS3_CMN_NM(Bs3PrintfV)(pszFormat, va);
    va_end(va);
    return cchRet;
}

