/* $Id: nocrt-vsscanf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - No-CRT - Simplistic vsscanf().
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
#include <iprt/nocrt/stdio.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/stdarg.h>
#include <iprt/string.h>


static const char *rtNoCrtScanString(const char *pszString, size_t cchWidth, char *pszDst, int *pcMatches)
{
    if (pszDst)
        *pcMatches += 1;
    while (cchWidth-- > 0)
    {
        char ch = *pszString;
        if (pszDst)
            *pszDst++ = ch;
        if (ch != '\0')
            pszString++;
        else
            return pszString;
    }
    if (pszDst)
        *pszDst = '\0';
    return pszString;
}


static const char *rtNoCrtScanChars(const char *pszString, unsigned cchWidth, char *pchDst, int *pcMatches)
{
    if (pchDst)
        *pcMatches += 1;
    while (cchWidth-- > 0)
    {
        char ch = *pszString;
        if (pchDst)
            *pchDst++ = ch;
        if (ch != '\0')
            pszString++;
        else
        {
            /** @todo how do we handle a too short strings? memset the remainder and
             *        count it as a match? */
            if (pchDst && cchWidth > 0)
                RT_BZERO(pchDst, cchWidth);
            return pszString;
        }
    }
    return pszString;
}


static void rtNoCrtStorInt(void *pvDst, char chPrefix, uint64_t uValue)
{
    switch (chPrefix)
    {
        default:
            AssertFailed();
            RT_FALL_THRU();
        case '\0':  *(unsigned int *)pvDst          = (unsigned int)uValue; break;
        case 'H':   *(unsigned char *)pvDst         = (unsigned char)uValue; break;
        case 'h':   *(unsigned short *)pvDst        = (unsigned short)uValue; break;
        case 'j':   *(uint64_t *)pvDst              = uValue; break;
        case 'l':   *(unsigned long *)pvDst         = (unsigned long)uValue; break;
        case 'L':   *(unsigned long long *)pvDst    = (unsigned long long)uValue; break;
        case 't':   *(ptrdiff_t *)pvDst             = (ptrdiff_t)uValue; break;
        case 'Z':
        case 'z':   *(size_t *)pvDst                = (size_t)uValue; break;
    }
}


static const char *rtNoCrtScanInt(const char *pszString, unsigned uBase, bool fSigned, char chPrefix, int cchWidth,
                                  void *pvDst, int *pcMatches)
{
    if (cchWidth >= 0 && cchWidth < _16M)
        uBase |= (unsigned)cchWidth << 8;

    int rc;
    if (fSigned)
    {
        int64_t iVal = 0;
        rc = RTStrToInt64Ex(pszString, (char **)&pszString, uBase, &iVal);
        if (RT_SUCCESS(rc))
        {
            if (pvDst)
            {
                rtNoCrtStorInt(pvDst, chPrefix, (uint64_t)iVal);
                *pcMatches += 1;
            }
        }
        else
            pszString = NULL;
    }
    else
    {
        uint64_t uVal = 0;
        rc = RTStrToUInt64Ex(pszString, (char **)&pszString, uBase, &uVal);
        if (RT_SUCCESS(rc))
        {
            if (pvDst)
            {
                rtNoCrtStorInt(pvDst, chPrefix, uVal);
                *pcMatches += 1;
            }
        }
        else
            pszString = NULL;
    }

    return pszString;
}


static const char *rtNoCrtScanFloat(const char *pszString, char chPrefix, int cchWidth, void *pvDst, int *pcMatches)
{
    size_t const cchMax = cchWidth > 0 ? (unsigned)cchWidth : 0;
    int          rc;
    switch (chPrefix)
    {
        default:
        case '\0':
#ifndef RT_OS_WINDOWS /* Windows doesn't do float, only double and "long" double (same as double). */
            rc = RTStrToFloatEx(pszString, (char **)&pszString, cchMax, (float *)pvDst);
            break;
#else
            RT_FALL_THRU();
#endif
        case 'l':
            rc = RTStrToDoubleEx(pszString, (char **)&pszString, cchMax, (double *)pvDst);
            break;

        case 'L':
            rc = RTStrToLongDoubleEx(pszString, (char **)&pszString, cchMax, (long double *)pvDst);
            break;
    }
    if (rc != VERR_NO_DIGITS)
        *pcMatches += pvDst != NULL;
    else
        pszString = NULL;
    return pszString;
}


#undef vsscanf
int RT_NOCRT(vsscanf)(const char *pszString, const char *pszFormat, va_list va)
{
#ifdef RT_STRICT
    const char * const  pszFormatStart = pszFormat;
#endif
    const char * const  pszStart       = pszString;
    int                 cMatches       = 0;
    char                chFmt;
    while ((chFmt = *pszFormat++) != '\0')
    {
        switch (chFmt)
        {
            default:
                if (chFmt == *pszString)
                    pszString++;
                else
                    return cMatches;
                break;

            /*
             * White space will match zero or more whitespace character in the
             * source string, no specific type.  So, we advance to the next no-space
             * character in each of the string.
             */
            case ' ': /* See RT_C_IS_SPACE */
            case '\t':
            case '\n':
            case '\r':
            case '\v':
            case '\f':
                while (RT_C_IS_SPACE(*pszFormat))
                    pszFormat++;
                while (RT_C_IS_SPACE(*pszString))
                    pszFormat++;
                break;

            /*
             * %[*][width][h|l|ll|L|q|t|z|Z|]type
             */
            case '%':
            {
                chFmt = *pszFormat++;

                /* Escaped '%s'? */
                if (chFmt == '%')
                {
                    if (*pszString == '%')
                        pszString++;
                    else
                        return cMatches;
                    break;
                }

                /* Assigning or non-assigning argument? */
                bool const fAssign = chFmt != '*';
                if (chFmt == '*')
                    chFmt = *pszFormat++;

                /* Width specifier? */
                int cchWidth = -1;
                if (RT_C_IS_DIGIT(chFmt))
                {
                    cchWidth = chFmt - '0';
                    for (;;)
                    {
                        chFmt = *pszFormat++;
                        if (!RT_C_IS_DIGIT(chFmt))
                            break;
                        cchWidth *= 10;
                        cchWidth += chFmt - '0';
                    }
                }

                /* Size prefix?
                   We convert 'hh' to 'H', 'll' to 'L', and 'I64' to 'L'. The
                   latter is for MSC compatibility, of course.  */
                char chPrefix = '\0';
                switch (chFmt)
                {
                    case 'q':
                        chPrefix = 'L';
                        RT_FALL_THRU();
                    case 'L':
                    case 'j':
                    case 'z':
                    case 'Z':
                    case 't':
                        chPrefix = chFmt;
                        chFmt = *pszFormat++;
                        break;
                    case 'l':
                    case 'h':
                        chPrefix = chFmt;
                        chFmt = *pszFormat++;
                        if (chPrefix != 'L' && chFmt == chPrefix)
                        {
                            chPrefix = chPrefix == 'l' ? 'L' : 'H';
                            chFmt = *pszFormat++;
                        }
                        break;
                    case 'I':
                        if (pszFormat[0] == '6' && pszFormat[1] == '4')
                        {
                            chPrefix = 'L';
                            pszFormat += 2;
                        }
                        break;
                }

                /* Now for the type. */
                switch (chFmt)
                {
                    case 'p':
                        chPrefix = 'j';
                        RT_FALL_THRU();
                    case 'd':
                    case 'i':
                    case 'o':
                    case 'u':
                    case 'x':
                    case 'X':
                    {
                        while (RT_C_IS_SPACE(*pszString))
                            pszString++;

                        void *pvDst = NULL;
                        if (fAssign)
                            pvDst = va_arg(va, void *); /* This ought to work most place... Probably not standard conforming. */
                        pszString = rtNoCrtScanInt(pszString,
                                                   chFmt == 'i' ? 0 : chFmt == 'd' || chFmt == 'u' ? 10 : chFmt == 'o' ? 8 :  16,
                                                   chFmt == 'd' || chFmt == 'i' /* fSigned */,
                                                   chPrefix, cchWidth, pvDst, &cMatches);
                        if (!pszString)
                            return cMatches;
                        break;
                    }

                    case 'a':
                    case 'A':
                    case 'e':
                    case 'E':
                    case 'f':
                    case 'F':
                    case 'g':
                    case 'G':
                    {
                        while (RT_C_IS_SPACE(*pszString))
                            pszString++;

                        /* Note! We don't really give a hoot what input format type we're given,
                                 we keep and open mind and acceept whatever we find that looks like
                                 floating point.  This is doubtfully standard compliant. */
                        void *pvDst = NULL;
                        if (fAssign)
                            pvDst = va_arg(va, void *); /* This ought to work most place... Probably not standard conforming. */
                        pszString = rtNoCrtScanFloat(pszString, chPrefix, cchWidth, pvDst, &cMatches);
                        if (!pszString)
                            return cMatches;
                        break;
                    }

                    case 'n':
                    {
                        if (fAssign)
                        {
                            void *pvDst = va_arg(va, void *);
                            rtNoCrtStorInt(pvDst, chPrefix, (size_t)(pszString - pszStart));
                        }
                        break;
                    }

                    case 'c':
                        if (chPrefix != 'l' && chPrefix != 'L')
                        {
                            /* no whitespace skipped for %c */
                            char *pchDst = NULL;
                            if (fAssign)
                                pchDst = va_arg(va, char *);
                            pszString = rtNoCrtScanChars(pszString, cchWidth < 0 ? 1U : (unsigned)cchWidth, pchDst, &cMatches);
                            break;
                        }
                        RT_FALL_THRU();
                    case 'C':
                        AssertMsgFailedReturn(("Unsupported sscanf type: C/Lc (%s)\n", pszFormatStart), cMatches);
                        break;

                    case 's':
                        if (chPrefix != 'l' && chPrefix != 'L')
                        {
                            while (RT_C_IS_SPACE(*pszString))
                                pszString++;

                            char *pszDst = NULL;
                            if (fAssign)
                                pszDst = va_arg(va, char *);
                            pszString = rtNoCrtScanString(pszString, cchWidth < 0 ? RTSTR_MAX : (unsigned)cchWidth,
                                                          pszDst, &cMatches);
                        }
                        RT_FALL_THRU();
                    case 'S':
                        while (RT_C_IS_SPACE(*pszString))
                            pszString++;

                        AssertMsgFailedReturn(("Unsupported sscanf type: S/Ls (%s)\n", pszFormatStart), cMatches);
                        break;

                    default:
                        AssertMsgFailedReturn(("Unsupported sscanf type: %c (%s)\n", chFmt, pszFormatStart), cMatches);
                }
                break;
            }
        }
    }
    return cMatches;
}
RT_ALIAS_AND_EXPORT_NOCRT_SYMBOL(vsscanf);

