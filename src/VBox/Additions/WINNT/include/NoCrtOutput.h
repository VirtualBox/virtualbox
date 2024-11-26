/* $Id: NoCrtOutput.h 107166 2024-11-26 09:03:23Z andreas.loeffler@oracle.com $ */
/** @file
 * NoCrtOutput - ErrorMsgXxx and PrintXxx functions for small EXEs.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef GA_INCLUDED_WINNT_NoCrtOutput_h
#define GA_INCLUDED_WINNT_NoCrtOutput_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/win/windows.h>
#include <iprt/string.h>
#include <iprt/utf16.h>


/** @name Output helpers
 *
 * The general ASSUMPTION here is that all strings are restricted to 7-bit
 * ASCII, with the exception of wchar_t ones.
 *
 * @note We don't use printf, RTPrintf or similar not for masochistic reasons
 *       but to keep the binary small and make it easier to switch between CRT
 *       and IPRT w/ no-CRT.
 *
 * @{
 */

DECLINLINE(void) OutputWStr(HANDLE hDst, const wchar_t *pwszStr)
{
    DWORD cbIgn;
    if (GetConsoleMode(hDst, &cbIgn))
        WriteConsoleW(hDst, pwszStr, (DWORD)RTUtf16Len(pwszStr), &cbIgn, NULL);
    else
    {
        char *pszTmp;
        int rc = RTUtf16ToUtf8(pwszStr, &pszTmp);
        if (RT_SUCCESS(rc))
        {
            char *pszInCodepage;
            rc = RTStrUtf8ToCurrentCP(&pszInCodepage, pszTmp);
            if (RT_SUCCESS(rc))
            {
                WriteFile(hDst, pszInCodepage, (DWORD)strlen(pszInCodepage), &cbIgn, NULL);
                RTStrFree(pszInCodepage);
            }
            else
                WriteFile(hDst, RT_STR_TUPLE("<RTStrUtf8ToCurrentCP error>"), &cbIgn, NULL);
            RTStrFree(pszTmp);
        }
        else
            WriteFile(hDst, RT_STR_TUPLE("<RTUtf16ToUtf8 error>"), &cbIgn, NULL);
    }
}


DECLINLINE(void) ErrorMsgBegin(const char *pszMsg)
{
    HANDLE const hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    DWORD        cbIgn;
    WriteFile(hStdErr, RT_STR_TUPLE("error: "), &cbIgn, NULL);
    WriteFile(hStdErr, pszMsg, (DWORD)strlen(pszMsg), &cbIgn, NULL);
}


DECLINLINE(void) ErrorMsgStr(const char *pszMsg)
{
    HANDLE const hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    DWORD cbIgn;
    WriteFile(hStdErr, pszMsg, (DWORD)strlen(pszMsg), &cbIgn, NULL);
}


DECLINLINE(void) ErrorMsgWStr(const wchar_t *pwszStr)
{
    OutputWStr(GetStdHandle(STD_ERROR_HANDLE), pwszStr);
}


DECLINLINE(int) ErrorMsgEnd(const char *pszMsg)
{
    HANDLE const hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    DWORD        cbIgn;
    if (pszMsg)
        WriteFile(hStdErr, pszMsg, (DWORD)strlen(pszMsg), &cbIgn, NULL);
    WriteFile(hStdErr, RT_STR_TUPLE("\r\n"), &cbIgn, NULL);
#ifdef EXIT_FAIL /* VBoxDrvInst.cpp speciality */
    return EXIT_FAIL;
#else
    return RTEXITCODE_FAILURE;
#endif
}


DECLINLINE(void) ErrorMsgU64(uint64_t uValue, bool fSigned = false)
{
    char szVal[128];
    RTStrFormatU64(szVal, sizeof(szVal), uValue, 10, 0, 0, fSigned ? RTSTR_F_VALSIGNED : 0);
    ErrorMsgStr(szVal);
}


DECLINLINE(void) ErrorMsgX64(uint64_t uValue)
{
    char szVal[128];
    RTStrFormatU64(szVal, sizeof(szVal), uValue, 16, 0, 0, 0);
    ErrorMsgStr(szVal);
}


DECLINLINE(int) ErrorMsg(const char *pszMsg)
{
    ErrorMsgBegin(pszMsg);
    return ErrorMsgEnd(NULL);
}


DECLINLINE(int) ErrorMsgSU(const char *pszMsg1, uint64_t uValue1)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgU64(uValue1);
    return ErrorMsgEnd(NULL);
}


DECLINLINE(int) ErrorMsgSWS(const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    return ErrorMsgEnd(pszMsg3);
}


DECLINLINE(int) ErrorMsgSWSWS(const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3,
                              const wchar_t *pwszMsg4, const char *pszMsg5)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    ErrorMsgStr(pszMsg3);
    ErrorMsgWStr(pwszMsg4);
    return ErrorMsgEnd(pszMsg5);
}


DECLINLINE(int) ErrorMsgSUSUS(const char *pszMsg1, uint64_t uValue1, const char *pszMsg2, uint64_t uValue2, const char *pszMsg3)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgU64(uValue1);
    ErrorMsgStr(pszMsg2);
    ErrorMsgU64(uValue2);
    return ErrorMsgEnd(pszMsg3);
}


DECLINLINE(void) ErrorMsgErrVal(uint32_t uErrVal, bool fSigned)
{
    char    szVal[128];
    ssize_t cchVal = RTStrFormatU32(szVal, sizeof(szVal) - 1, uErrVal, 10, 0, 0, fSigned ? RTSTR_F_VALSIGNED : 0);
    szVal[cchVal++] = '/';
    szVal[cchVal]   = '\0';
    ErrorMsgStr(szVal);

    RTStrFormatU32(szVal, sizeof(szVal) - 1, uErrVal, 16, 0, 0, RTSTR_F_SPECIAL);
    ErrorMsgStr(szVal);
}


DECLINLINE(int) ErrorMsgErr(const char *pszMsg, uint32_t uErrVal, const char *pszErrIntro, bool fSigned)
{
    ErrorMsgBegin(pszMsg);
    ErrorMsgStr(pszErrIntro);
    ErrorMsgErrVal(uErrVal, fSigned);
    return ErrorMsgEnd(")");
}


DECLINLINE(int) ErrorMsgRc(int rcExit, const char *pszMsg)
{
    ErrorMsgBegin(pszMsg);
    ErrorMsgEnd(NULL);
    return rcExit;
}


DECLINLINE(int) ErrorMsgRcSU(int rcExit, const char *pszMsg1, uint64_t uValue)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgU64(uValue);
    ErrorMsgEnd(NULL);
    return rcExit;
}


DECLINLINE(int) ErrorMsgRcSX(int rcExit, const char *pszMsg1, uint64_t uValue)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgX64(uValue);
    ErrorMsgEnd(NULL);
    return rcExit;
}


DECLINLINE(int) ErrorMsgRcSUS(int rcExit, const char *pszMsg1, uint64_t uValue, const char *pszMsg2)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgU64(uValue);
    ErrorMsgEnd(pszMsg2);
    return rcExit;
}


DECLINLINE(int) ErrorMsgRcSW(int rcExit, const char *pszMsg1, const wchar_t *pwszMsg2)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    ErrorMsgEnd(NULL);
    return rcExit;
}


DECLINLINE(int) ErrorMsgRcSWS(int rcExit, const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    ErrorMsgEnd(pszMsg3);
    return rcExit;
}


DECLINLINE(int) ErrorMsgRcSWSS(int rcExit, const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3, const char *pszMsg4)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    ErrorMsgStr(pszMsg3);
    ErrorMsgEnd(pszMsg4);
    return rcExit;
}


DECLINLINE(int) ErrorMsgRcSWSU(int rcExit, const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3, uint64_t uValue4)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    ErrorMsgStr(pszMsg3);
    ErrorMsgU64(uValue4);
    ErrorMsgEnd(NULL);
    return rcExit;
}


DECLINLINE(int) ErrorMsgRcSWSX(int rcExit, const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3, uint64_t uValue4)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    ErrorMsgStr(pszMsg3);
    ErrorMsgX64(uValue4);
    ErrorMsgEnd(NULL);
    return rcExit;
}


DECLINLINE(int) ErrorMsgVBoxErr(const char *pszMsg, int rc)
{
    return ErrorMsgErr(pszMsg, rc, " (", true);
}


DECLINLINE(int) ErrorMsgLastErr(const char *pszMsg)
{
    return ErrorMsgErr(pszMsg, GetLastError(), " (last error ", false);
}


DECLINLINE(int) ErrorMsgLastErrSUR(const char *pszMsg1, uint64_t uValue)
{
    DWORD dwErr = GetLastError();
    ErrorMsgBegin(pszMsg1);
    ErrorMsgU64(uValue);
    ErrorMsgStr(" (last error ");
    ErrorMsgErrVal(dwErr, false);
    return ErrorMsgEnd(")");
}


DECLINLINE(int) ErrorMsgLastErrSWS(const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3)
{
    DWORD dwErr = GetLastError();
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    ErrorMsgStr(pszMsg3);
    ErrorMsgStr(" (last error ");
    ErrorMsgErrVal(dwErr, false);
    return ErrorMsgEnd(")");
}


DECLINLINE(int) ErrorMsgLastErrSWSWS(const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3,
                                     const wchar_t *pwszMsg4, const char *pszMsg5)
{
    DWORD dwErr = GetLastError();
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    ErrorMsgStr(pszMsg3);
    ErrorMsgWStr(pwszMsg4);
    ErrorMsgStr(pszMsg5);
    ErrorMsgStr(" (last error ");
    ErrorMsgErrVal(dwErr, false);
    return ErrorMsgEnd(")");
}


DECLINLINE(int) ErrorMsgLastErrSWSRSUS(const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3, const char *pszMsg4,
                                       uint64_t uValue, const char *pszMsg5)
{
    DWORD dwErr = GetLastError();
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    ErrorMsgStr(pszMsg3);
    ErrorMsgStr(" (last error ");
    ErrorMsgErrVal(dwErr, false);
    ErrorMsgStr(")");
    ErrorMsgStr(pszMsg4);
    ErrorMsgU64(uValue);
    return ErrorMsgEnd(pszMsg5);
}


DECLINLINE(int) ErrorMsgLastErrSSS(const char *pszMsg1, const char *pszMsg2, const char *pszMsg3)
{
    DWORD dwErr = GetLastError();
    ErrorMsgBegin(pszMsg1);
    ErrorMsgStr(pszMsg2);
    ErrorMsgStr(pszMsg3);
    ErrorMsgStr(" (last error ");
    ErrorMsgErrVal(dwErr, false);
    return ErrorMsgEnd(")");
}


DECLINLINE(int) ErrorMsgRcLastErr(int rcExit, const char *pszMsg)
{
    ErrorMsgErr(pszMsg, GetLastError(), " (last error ", false);
    return rcExit;
}


DECLINLINE(int) ErrorMsgRcLastErrSUR(int rcExit, const char *pszMsg1, uint64_t uValue)
{
    ErrorMsgLastErrSUR(pszMsg1, uValue);
    return rcExit;
}


static int ErrorMsgRcLastErrSWSR(int rcExit, const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3)
{
    DWORD dwErr = GetLastError();
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    ErrorMsgStr(pszMsg3);
    ErrorMsgStr(" (last error ");
    ErrorMsgErrVal(dwErr, false);
    ErrorMsgEnd(")");
    return rcExit;
}



DECLINLINE(int) ErrorMsgLStatus(const char *pszMsg, LSTATUS lrc)
{
    return ErrorMsgErr(pszMsg, (DWORD)lrc, " (", true);
}


DECLINLINE(int) ErrorMsgLStatusSWSRS(const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3,
                                LSTATUS lrc, const char *pszMsg4)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    ErrorMsgStr(pszMsg3);
    ErrorMsgErrVal((DWORD)lrc, true);
    return ErrorMsgEnd(pszMsg4);
}


DECLINLINE(int) ErrorMsgLStatusSWSWSRS(const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3, const wchar_t *pwszMsg4,
                                  const char *pszMsg5, LSTATUS lrc, const char *pszMsg6)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    ErrorMsgStr(pszMsg3);
    ErrorMsgWStr(pwszMsg4);
    ErrorMsgStr(pszMsg5);
    ErrorMsgErrVal((DWORD)lrc, true);
    return ErrorMsgEnd(pszMsg6);
}


DECLINLINE(int) ErrorMsgLStatusSWSWSWSRS(const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3,
                                         const wchar_t *pwszMsg4, const char *pszMsg5, const wchar_t *pwszMsg6,
                                         const char *pszMsg7, LSTATUS lrc, const char *pszMsg8)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    ErrorMsgStr(pszMsg3);
    ErrorMsgWStr(pwszMsg4);
    ErrorMsgStr(pszMsg5);
    ErrorMsgWStr(pwszMsg6);
    ErrorMsgStr(pszMsg7);
    ErrorMsgErrVal((DWORD)lrc, true);
    return ErrorMsgEnd(pszMsg8);
}


DECLINLINE(int) ErrorMsgLStatusSWSWSWSWSRS(const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3,
                                           const wchar_t *pwszMsg4, const char *pszMsg5, const wchar_t *pwszMsg6,
                                           const char *pszMsg7, const wchar_t *pwszMsg8, const char *pszMsg9, LSTATUS lrc,
                                           const char *pszMsg10)
{
    ErrorMsgBegin(pszMsg1);
    ErrorMsgWStr(pwszMsg2);
    ErrorMsgStr(pszMsg3);
    ErrorMsgWStr(pwszMsg4);
    ErrorMsgStr(pszMsg5);
    ErrorMsgWStr(pwszMsg6);
    ErrorMsgStr(pszMsg7);
    ErrorMsgWStr(pwszMsg8);
    ErrorMsgStr(pszMsg9);
    ErrorMsgErrVal((DWORD)lrc, true);
    return ErrorMsgEnd(pszMsg10);
}


DECLINLINE(int) ErrorBadArg(const char *pszName, wchar_t const *pwszArg, const char *pszValues = NULL)
{
    ErrorMsgBegin("Bad argument '");
    ErrorMsgStr(pszName);
    ErrorMsgStr("': ");
    ErrorMsgWStr(pwszArg);
    if (pszValues)
        ErrorMsgStr(", expected: ");
    return ErrorMsgEnd(pszValues);
}


/** Simple fputs(stdout) replacement. */
DECLINLINE(void) PrintStr(const char *pszMsg)
{
    HANDLE const hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD        cbIgn;
    WriteFile(hStdOut, pszMsg, (DWORD)strlen(pszMsg), &cbIgn, NULL);
}


/** Simple fputs(stdout) replacement. */
DECLINLINE(void) PrintWStr(const wchar_t *pwszStr)
{
    OutputWStr(GetStdHandle(STD_OUTPUT_HANDLE), pwszStr);
}


DECLINLINE(void) PrintX64(uint64_t uValue)
{
    char szVal[128];
    RTStrFormatU64(szVal, sizeof(szVal), uValue, 16, 0, 0, RTSTR_F_64BIT | RTSTR_F_SPECIAL);
    PrintStr(szVal);
}


DECLINLINE(void) PrintSS(const char *pszMsg1, const char *pszMsg2)
{
    PrintStr(pszMsg1);
    PrintStr(pszMsg2);
}


DECLINLINE(void) PrintSSS(const char *pszMsg1, const char *pszMsg2, const char *pszMsg3)
{
    PrintStr(pszMsg1);
    PrintStr(pszMsg2);
    PrintStr(pszMsg3);
}


DECLINLINE(void) PrintSWS(const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3)
{
    PrintStr(pszMsg1);
    PrintWStr(pwszMsg2);
    PrintStr(pszMsg3);
}


DECLINLINE(void) PrintSWSWS(const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3,
                            const wchar_t *pwszMsg4, const char *pszMsg5)
{
    PrintStr(pszMsg1);
    PrintWStr(pwszMsg2);
    PrintStr(pszMsg3);
    PrintWStr(pwszMsg4);
    PrintStr(pszMsg5);
}


DECLINLINE(void) PrintSWSWSWS(const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3, const wchar_t *pwszMsg4,
                              const char *pszMsg5, const wchar_t *pwszMsg6, const char *pszMsg7)
{
    PrintStr(pszMsg1);
    PrintWStr(pwszMsg2);
    PrintStr(pszMsg3);
    PrintWStr(pwszMsg4);
    PrintStr(pszMsg5);
    PrintWStr(pwszMsg6);
    PrintStr(pszMsg7);
}


DECLINLINE(void) PrintSWSWSWSWS(const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3, const wchar_t *pwszMsg4,
                                const char *pszMsg5, const wchar_t *pwszMsg6, const char *pszMsg7, const wchar_t *pwszMsg8,
                                const char *pszMsg9)
{
    PrintStr(pszMsg1);
    PrintWStr(pwszMsg2);
    PrintStr(pszMsg3);
    PrintWStr(pwszMsg4);
    PrintStr(pszMsg5);
    PrintWStr(pwszMsg6);
    PrintStr(pszMsg7);
    PrintWStr(pwszMsg8);
    PrintStr(pszMsg9);
}


DECLINLINE(void) PrintSXS(const char *pszMsg1, uint64_t uValue, const char *pszMsg2)
{
    PrintStr(pszMsg1);
    PrintX64(uValue);
    PrintStr(pszMsg2);
}


DECLINLINE(void) PrintSWSWSWSXS(const char *pszMsg1, const wchar_t *pwszMsg2, const char *pszMsg3, const wchar_t *pwszMsg4,
                                const char *pszMsg5, const wchar_t *pwszMsg6, const char *pszMsg7, uint64_t uValue,
                                const char *pszMsg8)
{
    PrintStr(pszMsg1);
    PrintWStr(pwszMsg2);
    PrintStr(pszMsg3);
    PrintWStr(pwszMsg4);
    PrintStr(pszMsg5);
    PrintWStr(pwszMsg6);
    PrintStr(pszMsg7);
    PrintX64(uValue);
    PrintStr(pszMsg8);
}

/** @} */

#endif /* !GA_INCLUDED_WINNT_NoCrtOutput_h */

