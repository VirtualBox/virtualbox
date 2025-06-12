/* $Id: env-win.cpp 109843 2025-06-12 11:54:33Z andreas.loeffler@oracle.com $ */
/** @file
 * IPRT - Environment, Posix.
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
#include <iprt/env.h>

#ifdef IPRT_NO_CRT
# include <iprt/asm.h>
#endif
#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/mem.h>
#include <iprt/utf16.h>

#ifndef IPRT_NO_CRT
# include <stdlib.h>
# include <errno.h>
#endif
#include <iprt/win/windows.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef IPRT_NO_CRT
static uint32_t volatile g_idxGetEnvBufs = 0;
static char             *g_apszGetEnvBufs[64]; /* leak */
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
int rtEnvSetUtf8Worker(const char *pchVar, size_t cchVar, const char *pszValue);


#if defined(RT_ARCH_X86) && defined(RT_OS_WINDOWS)
/**
 * This is a workaround for NT 3.x not setting the last error.
 */
static DWORD rtEnvNt31CheckEmpty(PCRTUTF16 pwszVar)
{
    /* Check the version first: */
    DWORD dwVersion = GetVersion();
    if (RT_BYTE1(dwVersion) != 3)
        return 0;

    /* When called with an empty buffer, we should get 1 if empty value
       and 0 if not found:  */
    DWORD cwcNeeded = GetEnvironmentVariableW(pwszVar, NULL, 0);
    return cwcNeeded == 0 ? ERROR_ENVVAR_NOT_FOUND : NO_ERROR;
}
#endif


RTDECL(bool) RTEnvExistsBad(const char *pszVar)
{
#ifndef IPRT_NO_CRT
    return RTEnvGetBad(pszVar) != NULL;
#else
    return RTEnvExistsUtf8(pszVar);
#endif
}


RTDECL(bool) RTEnvExist(const char *pszVar)
{
#ifndef IPRT_NO_CRT
    return RTEnvExistsBad(pszVar);
#else
    return RTEnvExistsUtf8(pszVar);
#endif
}


RTDECL(bool) RTEnvExistsUtf8(const char *pszVar)
{
    AssertReturn(strchr(pszVar, '=') == NULL, false);

    PRTUTF16 pwszVar;
    int rc = RTStrToUtf16(pszVar, &pwszVar);
    AssertRCReturn(rc, false);

#ifndef IPRT_NO_CRT
    bool fRet = _wgetenv(pwszVar) != NULL;
#else
    DWORD dwRet = GetEnvironmentVariableW(pwszVar, NULL, 0);
    bool fRet = dwRet != 0;
#endif

    RTUtf16Free(pwszVar);
    return fRet;
}


RTDECL(const char *) RTEnvGetBad(const char *pszVar)
{
    AssertReturn(strchr(pszVar, '=') == NULL, NULL);
#ifndef IPRT_NO_CRT
    return getenv(pszVar);
#else
    /*
     * Query the value into heap buffer which we give a lifetime of 64
     * RTEnvGetBad calls.
     */
    char *pszValue = RTEnvDup(pszVar);
    if (pszValue)
    {
        RTMEM_MAY_LEAK(pszValue); /* Quite possible we'll leak this, but the leak is limited to 64 values. */

        uint32_t idx = ASMAtomicIncU32(&g_idxGetEnvBufs) % RT_ELEMENTS(g_apszGetEnvBufs);
        char *pszOld = (char *)ASMAtomicXchgPtr((void * volatile *)&g_apszGetEnvBufs[idx], pszValue);
        RTStrFree(pszOld);
    }
    return pszValue;
#endif
}


RTDECL(const char *) RTEnvGet(const char *pszVar)
{
    return RTEnvGetBad(pszVar);
}

RTDECL(int) RTEnvGetUtf8(const char *pszVar, char *pszValue, size_t cbValue, size_t *pcchActual)
{
    AssertPtrReturn(pszVar, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pszValue, VERR_INVALID_POINTER);
    AssertReturn(pszValue || !cbValue, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pcchActual, VERR_INVALID_POINTER);
    AssertReturn(pcchActual || (pszValue && cbValue), VERR_INVALID_PARAMETER);
    AssertReturn(strchr(pszVar, '=') == NULL, VERR_ENV_INVALID_VAR_NAME);

    if (pcchActual)
        *pcchActual = 0;

    /*
     * Convert the name to UTF-16.
     */
    PRTUTF16 pwszVar;
    int rc = RTStrToUtf16(pszVar, &pwszVar);
    AssertRCReturn(rc, rc);

    /*
     * Query the variable.  First try with a medium sized stack buffer (too
     * small for your typical PATH, but large enough for most other things).
     */
    RTUTF16  wszValue[512];
    uint32_t cwcValueBuf   = RT_ELEMENTS(wszValue);
    PRTUTF16 pwszValue     = wszValue;
    PRTUTF16 pwszValueFree = NULL;

    for (unsigned iTry = 0;; iTry++)
    {
        /* This API is weird, it didn't always set ERROR_BUFFER_OVERFLOW nor ERROR_ENVVAR_NOT_FOUND.
           Note! Assume that the CRT transparently updates the process
                 environment and that we don't need to use _wgetenv_s here. */
        SetLastError(NO_ERROR);
        DWORD const cwcValueRet = GetEnvironmentVariableW(pwszVar, pwszValue, cwcValueBuf);
        DWORD       dwErr       = GetLastError();

        if (cwcValueRet < cwcValueBuf)
        {
#ifdef RT_ARCH_X86
            if (cwcValueRet == 0 && dwErr == NO_ERROR)
                dwErr = rtEnvNt31CheckEmpty(pwszVar);
#endif
            if (cwcValueRet > 0 || dwErr == NO_ERROR) /* In case of empty values we have to see if last error was set or not. */
            {
                if (cbValue)
                    rc = RTUtf16ToUtf8Ex(pwszValue, cwcValueRet, &pszValue, cbValue, pcchActual);
                else
                    rc = RTUtf16CalcUtf8LenEx(pwszValue, cwcValueRet, pcchActual);
            }
            else
            {
                Assert(cwcValueRet == 0);
                Assert(dwErr != NO_ERROR);
                rc = RTErrConvertFromWin32(dwErr);
            }
            break;
        }

        /*
         * Insufficient buffer, so increase it.  The first re-try will use the
         * returned size, further re-tries will max out with a multiple of the
         * stack buffer till we reaches 32KB chars (128 loops).
         */
        Assert(dwErr == NO_ERROR || dwErr == ERROR_BUFFER_OVERFLOW);
        RTMemTmpFree(pwszValueFree);
        AssertBreakStmt(cwcValueBuf < _32K, rc = VERR_INTERNAL_ERROR_3 /* not a good one */);

        cwcValueBuf = RT_MAX(cwcValueRet + iTry, RT_ELEMENTS(wszValue) * iTry);
        pwszValueFree = pwszValue = (PRTUTF16)RTMemTmpAlloc(cwcValueBuf * sizeof(RTUTF16));
        AssertBreakStmt(pwszValue, rc = VERR_NO_TMP_MEMORY);
    }

    RTMemTmpFree(pwszValueFree);
    RTUtf16Free(pwszVar);
    return rc;
}


RTDECL(char *) RTEnvDup(const char *pszVar)
{
    AssertPtrReturn(pszVar, NULL);

    /*
     * Convert the name to UTF-16.
     */
    PRTUTF16 pwszVar;
    int rc = RTStrToUtf16(pszVar, &pwszVar);
    AssertRCReturn(rc, NULL);

    /*
     * Query the variable.  First try with a medium sized stack buffer (too
     * small for your typical PATH, but large enough for most other things).
     */
    char    *pszRet = NULL;
    RTUTF16  wszValue[512];
    uint32_t cwcValueBuf   = RT_ELEMENTS(wszValue);
    PRTUTF16 pwszValue     = wszValue;
    PRTUTF16 pwszValueFree = NULL;

    for (unsigned iTry = 0;; iTry++)
    {
        /* This API is weird, it didn't always set ERROR_BUFFER_OVERFLOW nor ERROR_ENVVAR_NOT_FOUND.
           Note! Assume that the CRT transparently updates the process
                 environment and that we don't need to use _wgetenv_s here. */
        SetLastError(NO_ERROR);
        DWORD const cwcValueRet = GetEnvironmentVariableW(pwszVar, pwszValue, cwcValueBuf);
        DWORD       dwErr       = GetLastError();

        if (cwcValueRet < cwcValueBuf)
        {
#ifdef RT_ARCH_X86
            if (cwcValueRet == 0 && dwErr == NO_ERROR)
                dwErr = rtEnvNt31CheckEmpty(pwszVar);
#endif
            if (cwcValueRet > 0 || dwErr == NO_ERROR) /* In case of empty values we have to see if last error was set or not. */
            {
                rc = RTUtf16ToUtf8Ex(pwszValue, cwcValueRet, &pszRet, 0, NULL);
                if (RT_FAILURE(rc))
                    pszRet = NULL;
            }
            else
            {
                Assert(cwcValueRet == 0);
                Assert(dwErr != NO_ERROR);
            }
            break;
        }

        /*
         * Insufficient buffer, so increase it.  The first re-try will use the
         * returned size, further re-tries will max out with a multiple of the
         * stack buffer till we reaches 32KB chars (128 loops).
         */
        Assert(dwErr == NO_ERROR || dwErr == ERROR_BUFFER_OVERFLOW);
        RTMemTmpFree(pwszValueFree);
        AssertBreakStmt(cwcValueBuf < _32K, rc = VERR_INTERNAL_ERROR_3 /* not a good one */);

        cwcValueBuf = RT_MAX(cwcValueRet + iTry, RT_ELEMENTS(wszValue) * iTry);
        pwszValueFree = pwszValue = (PRTUTF16)RTMemTmpAlloc(cwcValueBuf * sizeof(RTUTF16));
        AssertBreakStmt(pwszValue, rc = VERR_NO_TMP_MEMORY);
    }

    RTMemTmpFree(pwszValueFree);
    RTUtf16Free(pwszVar);
    return pszRet;
}


RTDECL(int) RTEnvPutBad(const char *pszVarEqualValue)
{
#ifndef IPRT_NO_CRT
    /** @todo putenv is a source memory leaks. deal with this on a per system basis. */
    if (!putenv((char *)pszVarEqualValue))
        return 0;
    return RTErrConvertFromErrno(errno);
#else
    return RTEnvPutUtf8(pszVarEqualValue);
#endif
}


RTDECL(int) RTEnvPut(const char *pszVarEqualValue)
{
#ifndef IPRT_NO_CRT
    return RTEnvPutBad(pszVarEqualValue);
#else
    return RTEnvPutUtf8(pszVarEqualValue);
#endif
}


RTDECL(int) RTEnvPutUtf8(const char *pszVarEqualValue)
{
    PRTUTF16 pwszVarEqualValue;
    int rc = RTStrToUtf16(pszVarEqualValue, &pwszVarEqualValue);
    if (RT_SUCCESS(rc))
    {
#ifndef IPRT_NO_CRT
        if (!_wputenv(pwszVarEqualValue))
            rc = VINF_SUCCESS;
        else
            rc = RTErrConvertFromErrno(errno);
#else
        PRTUTF16 pwszValue = RTUtf16Chr(pwszVarEqualValue, '=');
        if (pwszValue)
        {
            *pwszValue++ = '\0';

            SetLastError(*pwszValue ? ERROR_OUTOFMEMORY : ERROR_ENVVAR_NOT_FOUND); /* The API did not always set the last error. */
            if (SetEnvironmentVariableW(pwszVarEqualValue, *pwszValue ? pwszValue : NULL))
                rc = VINF_SUCCESS;
            else
            {
                DWORD dwErr = GetLastError();
                if (dwErr == ERROR_ENVVAR_NOT_FOUND)
                {
                    Assert(!*pwszValue);
                    rc = VINF_SUCCESS;
                }
                else
                {
                    Assert(*pwszValue);
                    rc = RTErrConvertFromWin32(GetLastError());
                }
            }
        }
        else
            rc = VERR_INVALID_PARAMETER;
#endif
        RTUtf16Free(pwszVarEqualValue);
    }
    return rc;
}



RTDECL(int) RTEnvSetBad(const char *pszVar, const char *pszValue)
{
#ifndef IPRT_NO_CRT
    AssertMsgReturn(strchr(pszVar, '=') == NULL, ("'%s'\n", pszVar), VERR_ENV_INVALID_VAR_NAME);
    int rc;
    if (!RTEnvExist(pszVar))
        rc = VINF_ENV_VAR_NOT_FOUND;
    else
    {
        errno_t rcErrno = _putenv_s(pszVar, *pszValue ? pszValue : " " /* wrong, but will be treated as unset otherwise */);
        if (rcErrno == 0)
            rc = VINF_SUCCESS;
        else
            rc = RTErrConvertFromErrno(rcErrno);
    }
    return rc;
#else
    return RTEnvSetUtf8(pszVar, pszValue);
#endif
}


RTDECL(int) RTEnvSet(const char *pszVar, const char *pszValue)
{
#ifndef IPRT_NO_CRT
    return RTEnvSetBad(pszVar, pszValue);
#else
    return RTEnvSetUtf8(pszVar, pszValue);
#endif
}


/**
 * Worker common to RTEnvSetUtf8() and rtEnvSetExWorker().
 */
int rtEnvSetUtf8Worker(const char *pchVar, size_t cchVar, const char *pszValue)
{
    PRTUTF16 pwszVar;
    int rc = RTStrToUtf16Ex(pchVar, cchVar, &pwszVar, 0, NULL);
    if (RT_SUCCESS(rc))
    {
        PRTUTF16 pwszValue;
        rc = RTStrToUtf16(pszValue, &pwszValue);
        if (RT_SUCCESS(rc))
        {
#ifndef IPRT_NO_CRT
            errno_t rcErrno = _wputenv_s(pwszVar,
                                         *pwszValue ? pwszValue : L" " /* wrong, but will be treated as unset otherwise */);
            if (rcErrno == 0)
                rc = VINF_SUCCESS;
            else
                rc = RTErrConvertFromErrno(rcErrno);
#else
            SetLastError(ERROR_OUTOFMEMORY); /* The API did not always set the last error. */
            if (SetEnvironmentVariableW(pwszVar, pwszValue))
                rc = VINF_SUCCESS;
            else
                rc = RTErrConvertFromWin32(GetLastError());
#endif
            RTUtf16Free(pwszValue);
        }
        RTUtf16Free(pwszVar);
    }
    return rc;
}


RTDECL(int) RTEnvSetUtf8(const char *pszVar, const char *pszValue)
{
    size_t cchVar = strlen(pszVar);
    AssertReturn(memchr(pszVar, '=', cchVar) == NULL, VERR_ENV_INVALID_VAR_NAME);
    return rtEnvSetUtf8Worker(pszVar, cchVar, pszValue);
}


RTDECL(int) RTEnvUnsetBad(const char *pszVar)
{
#ifndef IPRT_NO_CRT
    AssertReturn(strchr(pszVar, '=') == NULL, VERR_ENV_INVALID_VAR_NAME);
    int rc;
    if (!RTEnvExist(pszVar))
        rc = VINF_ENV_VAR_NOT_FOUND;
    else
    {
        errno_t rcErrno = _putenv_s(pszVar, NULL);
        if (rcErrno == 0)
            rc = VINF_SUCCESS;
        else
            rc = RTErrConvertFromErrno(rcErrno);
    }
    return rc;
#else
    return RTEnvUnsetUtf8(pszVar);
#endif
}


RTDECL(int) RTEnvUnset(const char *pszVar)
{
#ifndef IPRT_NO_CRT
    return RTEnvUnsetBad(pszVar);
#else
    return RTEnvUnsetUtf8(pszVar);
#endif
}


RTDECL(int) RTEnvUnsetUtf8(const char *pszVar)
{
    AssertReturn(strchr(pszVar, '=') == NULL, VERR_ENV_INVALID_VAR_NAME);

    PRTUTF16 pwszVar;
    int rc = RTStrToUtf16(pszVar, &pwszVar);
    if (RT_SUCCESS(rc))
    {
#ifndef IPRT_NO_CRT
        if (_wgetenv(pwszVar))
        {
            errno_t rcErrno = _wputenv_s(pwszVar, L"");
            if (rcErrno == 0)
                rc = VINF_SUCCESS;
            else
                rc = RTErrConvertFromErrno(rcErrno);
        }
        else
            rc = VINF_ENV_VAR_NOT_FOUND;
#else
        SetLastError(ERROR_ENVVAR_NOT_FOUND); /* The API did not always set the last error. */
        if (SetEnvironmentVariableW(pwszVar, NULL))
            rc = VINF_SUCCESS;
        else
        {
            DWORD dwErr = GetLastError();
            rc = dwErr == ERROR_ENVVAR_NOT_FOUND ? VINF_ENV_VAR_NOT_FOUND : RTErrConvertFromWin32(dwErr);
        }
#endif
        RTUtf16Free(pwszVar);
    }
    return rc;
}

