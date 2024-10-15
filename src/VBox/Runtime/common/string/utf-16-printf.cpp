/* $Id: utf-16-printf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - String Formatters, Outputting UTF-16.
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
#include <iprt/utf16.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/uni.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** rtUtf16PrintfOutput() argument structure. */
typedef struct UTF16PRINTFOUTPUTARGS
{
    /** Pointer to current buffer position. */
    PRTUTF16    pwszCur;
    /** Number of RTUTF16 units left in the buffer (including the trailing zero). */
    size_t      cwcLeft;
    /** Set if we overflowed. */
    bool        fOverflowed;
} UTF16PRINTFOUTPUTARGS;
/** Pointer to a rtUtf16PrintfOutput() argument structure. */
typedef UTF16PRINTFOUTPUTARGS *PUTF16PRINTFOUTPUTARGS;


/**
 * Output callback.
 *
 * @returns Number of RTUTF16 units we (would have) outputted.
 *
 * @param   pvArg       Pointer to a STRBUFARG structure.
 * @param   pachChars   Pointer to an array of utf-8 characters.
 * @param   cbChars     Number of bytes in the character array pointed to by pachChars.
 */
static DECLCALLBACK(size_t) rtUtf16PrintfOutput(void *pvArg, const char *pachChars, size_t cbChars)
{
    PUTF16PRINTFOUTPUTARGS pArgs  = (PUTF16PRINTFOUTPUTARGS)pvArg;
    size_t                 cwcRet = 0;

    size_t cwcLeft = pArgs->cwcLeft;
    if (cwcLeft > 1)
    {
        Assert(!pArgs->fOverflowed);

        PRTUTF16 pwszCur = pArgs->pwszCur;
        for (;;)
        {
            if (cbChars > 0)
            {
                RTUNICP uc;
                int rc = RTStrGetCpNEx(&pachChars, &cbChars, &uc);
                AssertRCStmt(rc, uc = 0xfffd /* REPLACEMENT */);

                /* Simple: */
                if (RTUniCpIsBMP(uc))
                {
                    cwcRet += 1;
                    if (RT_LIKELY(cwcLeft > 1))
                        *pwszCur++ = uc;
                    else
                        break;
                    cwcLeft--;
                }
                /* Surrogate pair: */
                else if (uc >= 0x10000 && uc <= 0x0010ffff)
                {
                    cwcRet += 2;
                    if (RT_LIKELY(cwcLeft > 2))
                        *pwszCur++ = 0xd800 | (uc >> 10);
                    else
                    {
                        if (cwcLeft > 1)
                        {
                            cwcLeft = 1;
                            pwszCur[1] = '\0';
                        }
                        break;
                    }
                    *pwszCur++ = 0xdc00 | (uc & 0x3ff);
                    cwcLeft -= 2;
                }
                else
                {
                    AssertMsgFailed(("uc=%#x\n", uc));
                    cwcRet += 1;
                    if (RT_LIKELY(cwcLeft > 1))
                        *pwszCur++ = 0xfffd; /* REPLACEMENT */
                    else
                        break;
                    cwcLeft--;
                }
            }
            else
            {
                *pwszCur = '\0';
                pArgs->pwszCur = pwszCur;
                pArgs->cwcLeft = cwcLeft;
                return cwcRet;
            }
        }

        /*
         * We only get here if we run out of buffer space.
         */
        Assert(cwcLeft == 1);
        *pwszCur = '\0';
        pArgs->pwszCur = pwszCur;
        pArgs->cwcLeft = cwcLeft;
    }
    /*
     * We get a special zero byte call at the end for the formatting operation.
     *
     * Make sure we don't turn that into an overflow and that we'll terminate
     * empty result strings.
     */
    else if (cbChars == 0 && cwcLeft > 0)
    {
        *pArgs->pwszCur = '\0';
        return 0;
    }

    /*
     * Overflow handling.  Calc needed space.
     */
    pArgs->fOverflowed = true;

    while (cbChars > 0)
    {
        RTUNICP uc;
        int rc = RTStrGetCpNEx(&pachChars, &cbChars, &uc);
        AssertRCStmt(rc, uc = 0xfffd /* REPLACEMENT */);

        if (RTUniCpIsBMP(uc))
            cwcRet += 1;
        else if (uc >= 0x10000 && uc <= 0x0010ffff)
            cwcRet += 2;
        else
        {
            AssertMsgFailed(("uc=%#x\n", uc));
            cwcRet += 1;
        }
    }

    return cwcRet;
}


RTDECL(ssize_t) RTUtf16Printf(PRTUTF16 pwszBuffer, size_t cwcBuffer, const char *pszFormat, ...)
{
    /* Explicitly inline RTStrPrintfV + RTStrPrintfExV here because this is a frequently use API. */
    UTF16PRINTFOUTPUTARGS Args;
    size_t cwcRet;
    va_list args;
    AssertMsg(cwcBuffer > 0, ("Excellent idea! Format a string with no space for the output!\n"));

    Args.pwszCur     = pwszBuffer;
    Args.cwcLeft     = cwcBuffer;
    Args.fOverflowed = false;

    va_start(args, pszFormat);
    cwcRet = RTStrFormatV(rtUtf16PrintfOutput, &Args, NULL, NULL, pszFormat, args);
    va_end(args);

    return !Args.fOverflowed ? (ssize_t)cwcRet : -(ssize_t)cwcRet - 1;
}
RT_EXPORT_SYMBOL(RTStrPrintf2);


RTDECL(ssize_t) RTUtf16PrintfExV(PFNSTRFORMAT pfnFormat, void *pvArg, PRTUTF16 pwszBuffer, size_t cwcBuffer,
                                 const char *pszFormat,  va_list args)
{
    UTF16PRINTFOUTPUTARGS Args;
    size_t cwcRet;
    AssertMsg(cwcBuffer > 0, ("Excellent idea! Format a string with no space for the output!\n"));

    Args.pwszCur     = pwszBuffer;
    Args.cwcLeft     = cwcBuffer;
    Args.fOverflowed = false;
    cwcRet = RTStrFormatV(rtUtf16PrintfOutput, &Args, pfnFormat, pvArg, pszFormat, args);
    return !Args.fOverflowed ? (ssize_t)cwcRet : -(ssize_t)cwcRet - 1;
}
RT_EXPORT_SYMBOL(RTUtf16PrintfExV);


RTDECL(ssize_t) RTUtf16PrintfV(PRTUTF16 pwszBuffer, size_t cwcBuffer, const char *pszFormat, va_list args)
{
    return RTUtf16PrintfExV(NULL, NULL, pwszBuffer, cwcBuffer, pszFormat, args);
}
RT_EXPORT_SYMBOL(RTUtf16Printf2V);


RTDECL(ssize_t) RTUtf16PrintfEx(PFNSTRFORMAT pfnFormat, void *pvArg, PRTUTF16 pwszBuffer, size_t cwcBuffer,
                                const char *pszFormat, ...)
{
    va_list args;
    ssize_t cbRet;
    va_start(args, pszFormat);
    cbRet = RTUtf16PrintfExV(pfnFormat, pvArg, pwszBuffer, cwcBuffer, pszFormat, args);
    va_end(args);
    return cbRet;
}
RT_EXPORT_SYMBOL(RTUtf16PrintfEx);

