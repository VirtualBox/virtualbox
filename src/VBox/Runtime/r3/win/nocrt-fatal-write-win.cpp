/* $Id: nocrt-fatal-write-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - No-CRT - Fatal Message Writing Primitives, Windows.
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
#include "internal/nocrt.h"

#include <iprt/win/windows.h>
#include <iprt/string.h>


/** @todo invent some kind of weak linking with the debug and release
 *        loggers, e.g. theset some innocent function we can call to do
 *        logging and we'll call it if it isn't NULL. */

void rtNoCrtFatalWrite(const char *pchMsg, size_t cchMsg)
{
    DWORD cbIgn;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), pchMsg, (DWORD)cchMsg, &cbIgn, NULL);
}


void rtNoCrtFatalWriteBegin(const char *pchMsg, size_t cchMsg)
{
    rtNoCrtFatalWrite(pchMsg, cchMsg);
}


void rtNoCrtFatalWriteEnd(const char *pchMsg, size_t cchMsg)
{
    rtNoCrtFatalWrite(pchMsg, cchMsg);
}


void rtNoCrtFatalWritePtr(void const *pv)
{
    char    szValue[128];
#if ARCH_BITS == 64
    ssize_t cchValue = RTStrFormatU64(szValue, sizeof(szValue), (uintptr_t)pv, 16, 16, 16, RTSTR_F_WIDTH | RTSTR_F_64BIT);
#elif ARCH_BITS == 32
    ssize_t cchValue = RTStrFormatU32(szValue, sizeof(szValue), (uintptr_t)pv, 16,  8,  8, RTSTR_F_WIDTH | RTSTR_F_32BIT);
#else
# error ARCH_BITS
#endif
    rtNoCrtFatalWrite(szValue, (size_t)cchValue);
}


void rtNoCrtFatalWriteX64(uint64_t uValue)
{
    char    szValue[128];
    ssize_t cchValue = RTStrFormatU64(szValue, sizeof(szValue), uValue, 16, 0, 0, RTSTR_F_SPECIAL | RTSTR_F_64BIT);
    rtNoCrtFatalWrite(szValue, (size_t)cchValue);
}


void rtNoCrtFatalWriteX32(uint32_t uValue)
{
    char    szValue[128];
    ssize_t cchValue = RTStrFormatU32(szValue, sizeof(szValue), uValue, 16, 0, 0, RTSTR_F_SPECIAL | RTSTR_F_32BIT);
    rtNoCrtFatalWrite(szValue, (size_t)cchValue);
}


void rtNoCrtFatalWriteRc(int rc)
{
    char    szValue[128];
    ssize_t cchValue = RTStrFormatU32(szValue, sizeof(szValue), rc, 10, 0, 0, RTSTR_F_32BIT | RTSTR_F_VALSIGNED);
    rtNoCrtFatalWrite(szValue, (size_t)cchValue);
}

void rtNoCrtFatalWriteWinRc(uint32_t rc)
{
    char    szValue[168];
    ssize_t cchValue = RTStrFormatU32(szValue, sizeof(szValue), rc, 10, 0, 0, RTSTR_F_32BIT);
    szValue[cchValue++] = ' ';
    szValue[cchValue++] = '(';
    cchValue += RTStrFormatU32(&szValue[cchValue], sizeof(szValue) - cchValue, rc, 16, 0, 0, RTSTR_F_32BIT | RTSTR_F_SPECIAL);
    szValue[cchValue++] = ')';
    rtNoCrtFatalWrite(szValue, (size_t)cchValue);
}


void rtNoCrtFatalWriteStr(const char *pszString)
{
    if (RT_VALID_PTR(pszString))
    {
        size_t cch = 0;
        while (pszString[cch] != '\0')
            cch++;
        rtNoCrtFatalWrite(pszString, cch);
    }
    else
    {
        rtNoCrtFatalWrite(RT_STR_TUPLE("<pszString="));
        rtNoCrtFatalWritePtr(pszString);
        rtNoCrtFatalWrite(RT_STR_TUPLE(">"));
    }
}


void rtNoCrtFatalMsg(const char *pchMsg, size_t cchMsg)
{
    rtNoCrtFatalWriteBegin(pchMsg, cchMsg);
    rtNoCrtFatalWriteEnd(RT_STR_TUPLE(""));
}


void rtNoCrtFatalMsgWithRc(const char *pchMsg, size_t cchMsg, int rc)
{
    rtNoCrtFatalWriteBegin(pchMsg, cchMsg);
    rtNoCrtFatalWriteRc(rc);
    rtNoCrtFatalWriteEnd(RT_STR_TUPLE("\r\n"));
}

