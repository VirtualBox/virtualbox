/* $Id: tstRTEnv.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Environment.
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
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/test.h>


int main()
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTEnv", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

#define CHECK(expr)             RTTEST_CHECK(hTest, expr)
#define CHECK_RC(expr, rc)      RTTEST_CHECK_RC(hTest, expr, rc)
#define CHECK_STR(str1, str2)  do { if (strcmp(str1, str2)) { RTTestFailed(hTest, "line %u: '%s' != '%s' (*)", __LINE__, str1, str2); } } while (0)

    RTTestSub(hTest, "Basics");

    /*
     * Try mess around with the path a bit.
     */
#ifdef RT_OS_WINDOWS
    static const char * const k_pszPathVar = "Path";
#else
    static const char * const k_pszPathVar = "PATH";
#endif
    static const char * const k_pszNonExistantVar = "IPRT_I_DON_T_EXIST";

    CHECK(RTEnvExist(k_pszPathVar));
    CHECK(RTEnvExistEx(RTENV_DEFAULT, k_pszPathVar));
    CHECK(!RTEnvExist(k_pszNonExistantVar));
    CHECK(!RTEnvExistEx(RTENV_DEFAULT, k_pszNonExistantVar));

    CHECK(RTEnvGet(k_pszPathVar) != NULL);
    char szBuf[8192];
    char szBuf2[1024];
    size_t cch;
    CHECK_RC(RTEnvGetEx(RTENV_DEFAULT, k_pszPathVar, NULL, 0, &cch), VINF_SUCCESS);
    CHECK(cch < sizeof(szBuf));
    CHECK_RC(RTEnvGetEx(RTENV_DEFAULT, k_pszPathVar, szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(RTENV_DEFAULT, k_pszPathVar, szBuf, sizeof(szBuf), NULL), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(RTENV_DEFAULT, k_pszPathVar, szBuf, 1, &cch), VERR_BUFFER_OVERFLOW);
    CHECK_RC(RTEnvGetEx(RTENV_DEFAULT, k_pszPathVar, szBuf, 1, NULL), VERR_BUFFER_OVERFLOW);

    /* ditto for a clone. */
    RTENV Env = NIL_RTENV;
    CHECK_RC(RTEnvClone(&Env, RTENV_DEFAULT), VINF_SUCCESS);
    RTENV hEnvEq = RTENV_DEFAULT;
    CHECK_RC(RTEnvCreateEx(&hEnvEq, RTENV_CREATE_F_ALLOW_EQUAL_FIRST_IN_VAR), VINF_SUCCESS);
    RTENV hEnvNoEq = RTENV_DEFAULT;
    CHECK_RC(RTEnvCreateEx(&hEnvNoEq, 0), VINF_SUCCESS);

    CHECK(RTEnvExistEx(Env, k_pszPathVar));
    CHECK(!RTEnvExistEx(Env, k_pszNonExistantVar));

    CHECK_RC(RTEnvGetEx(Env, k_pszPathVar, NULL, 0, &cch), VINF_SUCCESS);
    CHECK(cch < sizeof(szBuf));
    CHECK_RC(RTEnvGetEx(Env, k_pszPathVar, szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(Env, k_pszPathVar, szBuf, sizeof(szBuf), NULL), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(Env, k_pszPathVar, szBuf, 1, &cch), VERR_BUFFER_OVERFLOW);
    CHECK_RC(RTEnvGetEx(Env, k_pszPathVar, szBuf, 1, NULL), VERR_BUFFER_OVERFLOW);

    CHECK_RC(RTEnvGetEx(hEnvEq, k_pszPathVar, szBuf, sizeof(szBuf), NULL), VERR_ENV_VAR_NOT_FOUND);
    CHECK_RC(RTEnvGetEx(hEnvEq, k_pszPathVar, szBuf, 0, &cch), VERR_ENV_VAR_NOT_FOUND);
    CHECK_RC(RTEnvGetEx(hEnvEq, "=D:", szBuf, 1, NULL), VERR_ENV_VAR_NOT_FOUND);
    CHECK_RC(RTEnvGetEx(hEnvNoEq, k_pszPathVar, szBuf, sizeof(szBuf), &cch), VERR_ENV_VAR_NOT_FOUND);
    CHECK_RC(RTEnvGetEx(hEnvNoEq, k_pszPathVar, szBuf, 1, NULL), VERR_ENV_VAR_NOT_FOUND);
    RTTestDisableAssertions(hTest);
    CHECK_RC(RTEnvGetEx(hEnvNoEq, "=D:", szBuf, 1, NULL), VERR_ENV_INVALID_VAR_NAME);
    RTTestRestoreAssertions(hTest);

    /*
     * Set and Unset
     */
    CHECK_RC(RTEnvSetEx(RTENV_DEFAULT, "IPRTMyNewVar", "MyValue1"), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(RTENV_DEFAULT, "IPRTMyNewVar", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue1");
    CHECK_RC(RTEnvSetEx(RTENV_DEFAULT, "IPRTMyNewVar", "MyValue2"), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(RTENV_DEFAULT, "IPRTMyNewVar", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue2");

    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar", "MyValue1"), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue1");
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar", "MyValue2"), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue2");

    CHECK_RC(RTEnvUnsetEx(Env, "IPRTMyNewVar"), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar", szBuf, sizeof(szBuf), &cch), VERR_ENV_VAR_NOT_FOUND);
    CHECK_RC(RTEnvUnsetEx(Env, "IPRTMyNewVar"), VINF_ENV_VAR_NOT_FOUND);

    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar0", "MyValue0"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar1", "MyValue1"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar2", "MyValue2"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar3", "MyValue3"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar4", "MyValue4"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar5", "MyValue5"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar6", "MyValue6"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar7", "MyValue7"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar8", "MyValue8"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar9", "MyValue9"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar10", "MyValue10"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar11", "MyValue11"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar12", "MyValue12"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar13", "MyValue13"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar14", "MyValue14"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar15", "MyValue15"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar16", "MyValue16"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar17", "MyValue17"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar18", "MyValue18"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar19", "MyValue19"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar20", "MyValue20"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar21", "MyValue21"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar22", "MyValue22"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar23", "MyValue23"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar24", "MyValue24"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar25", "MyValue25"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar26", "MyValue26"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar27", "MyValue27"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar28", "MyValue28"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar29", "MyValue29"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar30", "MyValue30"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar31", "MyValue31"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar32", "MyValue32"), VINF_SUCCESS);
    CHECK_RC(RTEnvSetEx(Env, "IPRTMyNewVar33", "MyValue33"), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar30", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue30");
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar31", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue31");
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar32", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue32");
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar33", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue33");

    CHECK_RC(RTEnvUnsetEx(Env, "IPRTMyNewVar33"), VINF_SUCCESS);
    CHECK(!RTEnvExistEx(Env, "IPRTMyNewVar33"));
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar33", szBuf, sizeof(szBuf), &cch), VERR_ENV_VAR_NOT_FOUND);
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar32", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue32");
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar15", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue15");

    CHECK_RC(RTEnvUnsetEx(Env, "IPRTMyNewVar3"), VINF_SUCCESS);
    CHECK(!RTEnvExistEx(Env, "IPRTMyNewVar3"));
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar3", szBuf, sizeof(szBuf), &cch), VERR_ENV_VAR_NOT_FOUND);
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar32", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue32");
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar15", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue15");

    CHECK_RC(RTEnvUnsetEx(Env, k_pszPathVar), VINF_SUCCESS);
    CHECK(!RTEnvExistEx(Env, k_pszPathVar));
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar32", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue32");
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar15", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue15");

    RTTestDisableAssertions(hTest);
#ifdef RT_OS_WINDOWS
    CHECK_RC(RTEnvSetEx(Env, "=C:", "C:\\Temp"), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(Env, "=C:", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "C:\\Temp");
#else
    CHECK_RC(RTEnvSetEx(Env, "=C:", "C:\\Temp"), VERR_ENV_INVALID_VAR_NAME);
    CHECK_RC(RTEnvSetEx(Env, "=", ""), VERR_ENV_INVALID_VAR_NAME);
#endif
    CHECK_RC(RTEnvSetEx(Env, "", ""), VERR_ENV_INVALID_VAR_NAME);
    RTTestRestoreAssertions(hTest);

    CHECK_RC(RTEnvSetEx(hEnvEq, "=D:", "D:\\TMP"), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(hEnvEq, "=D:", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "D:\\TMP");
    RTTESTI_CHECK(RTEnvExistEx(hEnvEq, "=D:") == true);
    CHECK_RC(RTEnvUnsetEx(hEnvEq, "=D:"), VINF_SUCCESS);
    CHECK_RC(RTEnvUnsetEx(hEnvEq, "=D:"), VINF_ENV_VAR_NOT_FOUND);
    RTTESTI_CHECK(RTEnvExistEx(hEnvEq, "=D:") == false);

    RTTestDisableAssertions(hTest);
    CHECK_RC(RTEnvSetEx(hEnvNoEq, "=D:", "D:\\TMP"), VERR_ENV_INVALID_VAR_NAME);
    CHECK_RC(RTEnvGetEx(hEnvNoEq, "=D:", szBuf, sizeof(szBuf), &cch), VERR_ENV_INVALID_VAR_NAME);
    CHECK_RC(RTEnvUnsetEx(hEnvNoEq, "=D:"), VERR_ENV_INVALID_VAR_NAME);
    RTTESTI_CHECK(RTEnvExistEx(hEnvNoEq, "=D:") == false);
    RTTestRestoreAssertions(hTest);

    /*
     * Put.
     */
    RTTestSub(hTest, "RTEnvPutEx");
    CHECK_RC(RTEnvPutEx(Env, "IPRTMyNewVar28"), VINF_SUCCESS);
    CHECK(!RTEnvExistEx(Env, "IPRTMyNewVar28"));
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar32", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue32");
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar15", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue15");

    CHECK_RC(RTEnvPutEx(Env, "IPRTMyNewVar28=MyValue28"), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar28", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue28");
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar32", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue32");
    CHECK_RC(RTEnvGetEx(Env, "IPRTMyNewVar15", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "MyValue15");

    RTTestDisableAssertions(hTest);
#ifdef RT_OS_WINDOWS
    CHECK_RC(RTEnvPutEx(Env, "=D:=D:\\Temp"), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(Env, "=D:", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "D:\\Temp");
#else
    CHECK_RC(RTEnvPutEx(Env, "=D:=D:\\Temp"), VERR_ENV_INVALID_VAR_NAME);
    CHECK_RC(RTEnvPutEx(Env, "="), VERR_ENV_INVALID_VAR_NAME);
#endif
    CHECK_RC(RTEnvPutEx(Env, ""), VERR_ENV_INVALID_VAR_NAME);
    RTTestRestoreAssertions(hTest);

    CHECK_RC(RTEnvPutEx(hEnvEq, "=C:=C:\\"), VINF_SUCCESS);
    CHECK_RC(RTEnvPutEx(hEnvEq, "=E:=E:\\TEMP"), VINF_SUCCESS);
    CHECK_RC(RTEnvGetEx(hEnvEq, "=E:", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "E:\\TEMP");
    RTTESTI_CHECK(RTEnvExistEx(hEnvEq, "=E:") == true);
    CHECK_RC(RTEnvPutEx(hEnvEq, "=E:"), VINF_SUCCESS);
    CHECK_RC(RTEnvPutEx(hEnvEq, "=E:"), VINF_ENV_VAR_NOT_FOUND);
    CHECK_RC(RTEnvGetEx(hEnvEq, "=E:", szBuf, sizeof(szBuf), &cch), VERR_ENV_VAR_NOT_FOUND);
    CHECK_RC(RTEnvGetEx(hEnvEq, "=C:", szBuf, sizeof(szBuf), &cch), VINF_SUCCESS);
    CHECK_STR(szBuf, "C:\\");
    CHECK_RC(RTEnvGetByIndexEx(hEnvEq, 0, szBuf2, sizeof(szBuf2), szBuf, sizeof(szBuf)), VINF_SUCCESS);
    CHECK_STR(szBuf2, "=C:");
    CHECK_STR(szBuf, "C:\\");
    CHECK_RC(RTEnvGetByIndexEx(hEnvEq, 1, szBuf2, sizeof(szBuf2), szBuf, sizeof(szBuf)), VERR_ENV_VAR_NOT_FOUND);
    RTTESTI_CHECK(RTEnvExistEx(hEnvEq, "=C:") == true);

    RTTestDisableAssertions(hTest);
    CHECK_RC(RTEnvPutEx(hEnvNoEq, "=C:=C:\\"), VERR_ENV_INVALID_VAR_NAME);
    CHECK_RC(RTEnvPutEx(hEnvNoEq, "=E:=E:\\TEMP"), VERR_ENV_INVALID_VAR_NAME);
    CHECK_RC(RTEnvPutEx(hEnvNoEq, "=E:"), VERR_ENV_INVALID_VAR_NAME);
    RTTESTI_CHECK(RTEnvExistEx(hEnvNoEq, "=C:") == false);
    RTTestRestoreAssertions(hTest);

    /*
     * Dup.
     */
    RTTestSub(hTest, "RTEnvDupEx");
    char *psz1;
    CHECK(RTEnvDupEx(Env, "NonExistantVariable") == NULL);
    psz1 = RTEnvDupEx(Env, "IPRTMyNewVar15");
    CHECK(psz1);
    if (psz1)
        CHECK_STR(psz1, "MyValue15");
    RTStrFree(psz1);

    static char s_szBigValue[10999];
    memset(s_szBigValue, 'a', sizeof(s_szBigValue));
    s_szBigValue[sizeof(s_szBigValue) - 1] = '\0';
    CHECK_RC(RTEnvSetEx(Env, "IPRTBigValue", s_szBigValue), VINF_SUCCESS);
    psz1 = RTEnvDupEx(Env, "IPRTBigValue");
    CHECK(psz1);
    if (psz1)
        CHECK_STR(psz1, s_szBigValue);
    RTStrFree(psz1);

    /*
     * Another cloning.
     */
    RTTestSub(hTest, "RTEnvClone");
    RTENV Env2;
    CHECK_RC(RTEnvClone(&Env2, Env), VINF_SUCCESS);
    CHECK_RC(RTEnvDestroy(Env2), VINF_SUCCESS);

    /*
     * execve envp and we're done.
     */
#ifndef RT_OS_WINDOWS
    RTTestSub(hTest, "RTEnvGetExecEnvP");
    char const * const *papsz = RTEnvGetExecEnvP(RTENV_DEFAULT);
    CHECK(papsz != NULL);
    papsz = RTEnvGetExecEnvP(RTENV_DEFAULT);
    CHECK(papsz != NULL);

    papsz = RTEnvGetExecEnvP(Env);
    CHECK(papsz != NULL);
    papsz = RTEnvGetExecEnvP(Env);
    CHECK(papsz != NULL);
#endif

    CHECK_RC(RTEnvDestroy(Env), VINF_SUCCESS);

    /*
     * Cleanups.
     */
    RTTESTI_CHECK_RC(RTEnvDestroy(hEnvEq), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTEnvDestroy(hEnvNoEq), VINF_SUCCESS);

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}

