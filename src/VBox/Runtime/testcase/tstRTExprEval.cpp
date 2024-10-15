/* $Id: tstRTExprEval.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTExprEval
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
#include <iprt/expreval.h>

#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;
static bool   g_fQueryVariableExpected = false;



/** @callbackmethodimpl{PFNRTEXPREVALQUERYVARIABLE} */
static DECLCALLBACK(int) tstBasicQueryVariable(const char *pchName, size_t cchName, void *pvUser, char **ppszValue)
{
    RTTESTI_CHECK(pvUser == (void *)g_hTest);
    RTTESTI_CHECK_RET(g_fQueryVariableExpected, VERR_WRONG_ORDER);

#define MATCH_VAR(a_szVariable) \
        (cchName == sizeof(a_szVariable) - 1 && memcmp(pchName, a_szVariable, sizeof(a_szVariable) - 1) == 0)
    const char *pszValue;
    if (MATCH_VAR("MYVAR1"))
        pszValue = "42";
    else if (MATCH_VAR("MYVAR2"))
        pszValue = "string";
    else if (MATCH_VAR("MYNESTED1"))
        pszValue = "MYVAR1";
    else if (MATCH_VAR("FOX_AND_DOG"))
        pszValue = "The quick brown fox jumps over the lazy dog";
    else
        return VERR_NOT_FOUND;

    if (ppszValue)
        return RTStrDupEx(ppszValue, pszValue);
    return VINF_SUCCESS;
}


static void tstBasic(void)
{
    RTTestISub("Basics");

    RTEXPREVAL hExprEval;
    RTTESTI_CHECK_RC_RETV(RTExprEvalCreate(&hExprEval, 0, "basics", NULL, NULL), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(hExprEval != NIL_RTEXPREVAL);
    RTTESTI_CHECK_RETV(RTExprEvalRelease(hExprEval) == 0);

    RTTESTI_CHECK_RC_RETV(RTExprEvalCreate(&hExprEval, 0, "basics", g_hTest, tstBasicQueryVariable), VINF_SUCCESS);
    RTTESTI_CHECK_RETV(hExprEval != NIL_RTEXPREVAL);

    bool fResult;
#define CHECK_fResult(a_fExpect) do { \
        if (fResult != (a_fExpect)) RTTestIFailed("line %u: fResult=%RTbool, expected %RTbool", __LINE__, fResult, (a_fExpect)); \
    } while (0)
    RTTESTI_CHECK_RC(RTExprEvalToBool(hExprEval, RT_STR_TUPLE("1"), &fResult, NULL), VINF_SUCCESS);
    CHECK_fResult(true);
    RTTESTI_CHECK_RC(RTExprEvalToBool(hExprEval, RT_STR_TUPLE("0"), &fResult, NULL), VINF_SUCCESS);
    CHECK_fResult(false);

    RTTESTI_CHECK_RC(RTExprEvalToBool(hExprEval, RT_STR_TUPLE("true"), &fResult, NULL), VINF_SUCCESS);
    CHECK_fResult(true);
    RTTESTI_CHECK_RC(RTExprEvalToBool(hExprEval, RT_STR_TUPLE("false"), &fResult, NULL), VINF_SUCCESS);
    CHECK_fResult(false);

    g_fQueryVariableExpected = true;
    RTTESTI_CHECK_RC(RTExprEvalToBool(hExprEval, RT_STR_TUPLE("defined(MYVAR1)"), &fResult, NULL), VINF_SUCCESS);
    CHECK_fResult(true);
    RTTESTI_CHECK_RC(RTExprEvalToBool(hExprEval, RT_STR_TUPLE("defined(NO_SUCH_VARIABLE)"), &fResult, NULL), VINF_SUCCESS);
    CHECK_fResult(false);
    g_fQueryVariableExpected = false;

    RTTESTI_CHECK_RC(RTExprEvalToBool(hExprEval, RT_STR_TUPLE("1.0.1 vle 2.0"), &fResult, NULL), VINF_SUCCESS);
    CHECK_fResult(true);
    RTTESTI_CHECK_RC(RTExprEvalToBool(hExprEval, RT_STR_TUPLE("1.0.1 vle 1.0"), &fResult, NULL), VINF_SUCCESS);
    CHECK_fResult(false);
    RTTESTI_CHECK_RC(RTExprEvalToBool(hExprEval, RT_STR_TUPLE("1.0.1 vle 1.0.1"), &fResult, NULL), VINF_SUCCESS);
    CHECK_fResult(true);

    int64_t iResult;
#define CHECK_iResult(a_iExpect) do { \
        if (iResult != (a_iExpect)) RTTestIFailed("line %u: iResult=%#RX64, expected %#RX64", __LINE__, iResult, (a_iExpect)); \
    } while (0)
    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("1"), &iResult, NULL), VINF_SUCCESS);
    CHECK_iResult(1);
    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("0"), &iResult, NULL), VINF_SUCCESS);
    CHECK_iResult(0);
    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("123459876"), &iResult, NULL), VINF_SUCCESS);
    CHECK_iResult(123459876);
    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("123459876"), &iResult, NULL), VINF_SUCCESS);
    CHECK_iResult(123459876);
    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("-123459876"), &iResult, NULL), VINF_SUCCESS);
    CHECK_iResult(-123459876);
    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("0x7fffffffffffffff"), &iResult, NULL), VINF_SUCCESS);
    CHECK_iResult(INT64_MAX);
    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("-9223372036854775808"), &iResult, NULL), VINF_SUCCESS);
    CHECK_iResult(INT64_MIN);

    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("true"), &iResult, NULL), VINF_SUCCESS);
    CHECK_iResult(1);
    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("false"), &iResult, NULL), VINF_SUCCESS);
    CHECK_iResult(0);
    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("false + 2"), &iResult, NULL), VINF_SUCCESS);
    CHECK_iResult(2);
    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("false - true"), &iResult, NULL), VINF_SUCCESS);
    CHECK_iResult(-1);
    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("false - ((true))"), &iResult, NULL), VINF_SUCCESS);
    CHECK_iResult(-1);
    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("false true - "), &iResult, NULL), VERR_PARSE_ERROR);
    CHECK_iResult(INT64_MAX);
    g_fQueryVariableExpected = true;
    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("${MYVAR1} + 0"), &iResult, NULL), VINF_SUCCESS);
    CHECK_iResult(42);
    RTTESTI_CHECK_RC(RTExprEvalToInteger(hExprEval, RT_STR_TUPLE("${${MYNESTED1}} + 2"), &iResult, NULL), VINF_SUCCESS);
    CHECK_iResult(44);
    g_fQueryVariableExpected = false;

    char *pszResult;
#define CHECK_FREE_pszResult(a_pszExpect) do { \
        const char *pszMacroExpect = (a_pszExpect); \
        if (   (pszResult != NULL || pszMacroExpect != NULL) \
            && RTStrCmp(pszResult, pszMacroExpect) != 0 ) \
            RTTestIFailed("line %u: pszResult=%s, expected %s", __LINE__, pszResult, pszMacroExpect); \
        RTStrFree(pszResult); \
    } while (0)
    RTTESTI_CHECK_RC(RTExprEvalToString(hExprEval, RT_STR_TUPLE("true"), &pszResult, NULL), VINF_SUCCESS);
    CHECK_FREE_pszResult("true");
    RTTESTI_CHECK_RC(RTExprEvalToString(hExprEval, RT_STR_TUPLE("false"), &pszResult, NULL), VINF_SUCCESS);
    CHECK_FREE_pszResult("false");
    RTTESTI_CHECK_RC(RTExprEvalToString(hExprEval, RT_STR_TUPLE("1+2"), &pszResult, NULL), VINF_SUCCESS);
    CHECK_FREE_pszResult("3");

    /* hash functions: */
    g_fQueryVariableExpected = true;
    RTTESTI_CHECK_RC(RTExprEvalToString(hExprEval, RT_STR_TUPLE("sha1 \"${FOX_AND_DOG}\""), &pszResult, NULL), VINF_SUCCESS);
    g_fQueryVariableExpected = false;
    CHECK_FREE_pszResult("2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");

    g_fQueryVariableExpected = true;
    RTTESTI_CHECK_RC(RTExprEvalToString(hExprEval, RT_STR_TUPLE("sha1(\"${FOX_AND_DOG}\")"), &pszResult, NULL), VINF_SUCCESS);
    g_fQueryVariableExpected = false;
    CHECK_FREE_pszResult("2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");

    g_fQueryVariableExpected = true;
    RTTESTI_CHECK_RC(RTExprEvalToString(hExprEval, RT_STR_TUPLE("sha1(${FOX_AND_DOG})"), &pszResult, NULL), VINF_SUCCESS);
    g_fQueryVariableExpected = false;
    CHECK_FREE_pszResult("2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");

#if 0 /** @todo not happy with 'strcat' as an operator. Dot doesn't work, so, figure something else out... */
    g_fQueryVariableExpected = true;
    RTTESTI_CHECK_RC(RTExprEvalToString(hExprEval, RT_STR_TUPLE("sha1(${FOX_AND_DOG}) strcat sha1('')"), &pszResult, NULL), VINF_SUCCESS);
    g_fQueryVariableExpected = false;
    CHECK_FREE_pszResult("2fd4e1c67a2d28fced849ee1bb76e7391b93eb12da39a3ee5e6b4b0d3255bfef95601890afd80709");
#endif

    g_fQueryVariableExpected = true;
    RTTESTI_CHECK_RC(RTExprEvalToString(hExprEval, RT_STR_TUPLE("sha256('')"), &pszResult, NULL), VINF_SUCCESS);
    g_fQueryVariableExpected = false;
    CHECK_FREE_pszResult("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    g_fQueryVariableExpected = true;
    RTTESTI_CHECK_RC(RTExprEvalToString(hExprEval, RT_STR_TUPLE("sha512('')"), &pszResult, NULL), VINF_SUCCESS);
    g_fQueryVariableExpected = false;
    CHECK_FREE_pszResult("cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");

    g_fQueryVariableExpected = true;
    RTTESTI_CHECK_RC(RTExprEvalToString(hExprEval, RT_STR_TUPLE("md5(${FOX_AND_DOG})"), &pszResult, NULL), VINF_SUCCESS);
    g_fQueryVariableExpected = false;
    CHECK_FREE_pszResult("9e107d9d372bb6826bd81d3542a419d6");

    RTTESTI_CHECK_RETV(RTExprEvalRelease(hExprEval) == 0);
}



int main()
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTExprEval", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    tstBasic();

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(g_hTest);
}

