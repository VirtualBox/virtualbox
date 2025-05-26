/* $Id: tstRTProcCreateEx.cpp 169006 2025-05-26 22:58:20Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT Testcase - RTProcCreateEx.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/process.h>

#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/path.h> /* For CWD testing. */
#include <iprt/pipe.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/test.h>
#include <iprt/thread.h>

#ifdef RT_OS_WINDOWS
# define SECURITY_WIN32
# include <iprt/win/windows.h>
# include <Security.h>
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTENV g_hEnvInitial = NIL_RTENV;
static char  g_szExecName[RTPATH_MAX];


static const char * const g_apszArgs4[] =
{
    /* 0 */ "non existing non executable file",
    /* 1 */ "--testcase-child-4",
    /* 2 */ "a b",
    /* 3 */ " cdef",
    /* 4 */ "ghijkl ",
    /* 5 */ "\"",
    /* 6 */ "\\",
    /* 7 */ "\\\"",
    /* 8 */ "\\\"\\",
    /* 9 */ "\\\\\"\\",
    /*10 */ "%TEMP%",
    /*11 */ "%TEMP%\filename",
    /*12 */ "%TEMP%postfix",
    /*13 */ "Prefix%TEMP%postfix",
    /*14 */ "%",
    /*15 */ "%%",
    /*16 */ "%%%",
    /*17 */ "%X",
    /*18 */ "%%X",
    NULL
};


static int tstRTCreateProcExCwdChild(int argc, char **argv)
{
    int rc = RTR3InitExeNoArguments(0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    int cErrors = 0;

    if (argc < 3)
        return RTEXITCODE_FAILURE;

    const char *pszCWD = argv[2];

    RTStrmPrintf(g_pStdOut, "childcwd: Called with CWD '%s'\n", pszCWD);

    if (!RTStrICmp(pszCWD, "<cwd-not-specified>")) /* Bail out early if no CWD has been explicitly specified. */
        return RTEXITCODE_SUCCESS;

    /* Validate if we really are in the CWD the parent told us. */
    char szCWD[RTPATH_MAX];
    rc = RTPathGetCurrent(szCWD, sizeof(szCWD));
    if (RT_FAILURE(rc))
    {
        RTStrmPrintf(g_pStdErr, "childcwd: Unable to retrieve CWD, rc=%Rrc\n", rc);
        cErrors++;
    }
    else
    {
        if (RTStrCmp(szCWD, pszCWD))
        {
            RTStrmPrintf(g_pStdErr, "childcwd: CWD is '%s', but expected '%s'\n", szCWD, pszCWD);
            cErrors++;
        }
        else
        {
            /* Check if we can query information of the current CWD. */
            char *pszUser = NULL;
            if (   argc >= 4
                && argv[3][0] != '\0')
                pszUser = argv[3];

            RTFSOBJINFO objInfo;
            rc = RTPathQueryInfo(szCWD, &objInfo, RTFSOBJATTRADD_NOTHING);
            if (pszUser)
                RTStrmPrintf(g_pStdOut, "childcwd: Accessing CWD '%s' via user '%s' -> %Rrc\n", szCWD, pszUser, rc);
            else
                RTStrmPrintf(g_pStdOut, "childcwd: Accesing CWD '%s' -> %Rrc\n", szCWD, rc);
            if (RT_FAILURE(rc))
                cErrors++;
        }
    }

    RTStrmPrintf(g_pStdOut, "childcwd: Exiting (%d errors)\n", cErrors);
    return cErrors == 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static void tstRTCreateProcExCwd(const char *pszAsUser, const char *pszPassword)
{
    RTTestISub("Current working directory (CWD)");

    const char *apszArgs[5] =
    {
        g_szExecName,
        "--testcase-child-cwd",
        "<cwd-not-specified>",
        pszAsUser,
        NULL
    };

    bool const fMayPanic = RTAssertSetMayPanic(false);
    bool const fQuiet    = RTAssertSetQuiet(true);

    RTPROCESS hProc;
    uint32_t  fFlags = 0;
    /* Missing RTPROC_FLAGS_CWD. */
    RTTESTI_CHECK_RC_RETV(RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, fFlags,
                                         NULL, NULL, NULL, pszAsUser, pszPassword,
                                         (void *)apszArgs[2] /* pvExtraData (CWD) */, &hProc),
                          VERR_INVALID_PARAMETER);

    /* Invalid flag combinations. Windows flags ignored elsewhere. */
#ifdef RT_OS_WINDOWS
    int rc = VERR_INVALID_PARAMETER;
#else
    int rc = VERR_INVALID_POINTER; /* Due to missing pvExtraData. */
#endif
    fFlags = RTPROC_FLAGS_CWD | RTPROC_FLAGS_DESIRED_SESSION_ID | RTPROC_FLAGS_SERVICE;
    RTTESTI_CHECK_RC_RETV(RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, fFlags,
                                         NULL, NULL, NULL, pszAsUser, pszPassword,
                                         NULL, &hProc), rc);

    /* Windows-only flags. Ignored elsewhere. */
#ifdef RT_OS_WINDOWS
    rc = VERR_INVALID_POINTER;
#else
    rc = VINF_SUCCESS;
#endif
    fFlags = RTPROC_FLAGS_DESIRED_SESSION_ID | RTPROC_FLAGS_SERVICE;
    RTTESTI_CHECK_RC_RETV(RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, fFlags,
                                         NULL, NULL, NULL, pszAsUser, pszPassword,
                                         NULL, &hProc), rc);

    /* RTPROC_FLAGS_CWD set, but CWD missing as pvExtradata. */
    fFlags = RTPROC_FLAGS_CWD;
    RTTESTI_CHECK_RC_RETV(RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, fFlags,
                                         NULL, NULL, NULL, pszAsUser, pszPassword,
                                         NULL, &hProc),
                          VERR_INVALID_POINTER);

    char szCWD[RTPATH_MAX];
    char szResolved[RTPATH_MAX];

    /* Try current CWD, whatever that is. */
    RTTESTI_CHECK_RC_OK_RETV(RTPathGetCurrent(szCWD, sizeof(szCWD)));
    RTTESTI_CHECK_RC_OK_RETV(RTPathReal(szCWD, szResolved, sizeof(szResolved)));
    apszArgs[2] = szResolved;
    RTTESTI_CHECK_RC_RETV(RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, fFlags,
                                         NULL, NULL, NULL, pszAsUser, pszPassword,
                                         (void *)szCWD /* pvExtraData (CWD) */, &hProc),
                          VINF_SUCCESS);
    RTPROCSTATUS ProcStatus = { -1, RTPROCEXITREASON_ABEND };
    RTTESTI_CHECK_RC(RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus), VINF_SUCCESS);
    if (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0)
        RTTestIFailed("enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);

    /* Try temporary directory. */
    RTTESTI_CHECK_RC_OK_RETV(RTPathTemp(szCWD, sizeof(szCWD)));
    RTTESTI_CHECK_RC_OK_RETV(RTPathReal(szCWD, szResolved, sizeof(szResolved)));
    apszArgs[2] = szResolved;
    RTTESTI_CHECK_RC_RETV(RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, fFlags,
                                         NULL, NULL, NULL, pszAsUser, pszPassword,
                                         (void *)szCWD /* pvExtraData (CWD) */, &hProc),
                          VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus), VINF_SUCCESS);
    if (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0)
        RTTestIFailed("enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);

#if 0 /* braindead - the home directory of the wrong user, so, typically inaccessible. */
    /* Try user home. */
    RTTESTI_CHECK_RC_OK_RETV(RTPathUserHome(szCWD, sizeof(szCWD)));
    RTTESTI_CHECK_RC_OK_RETV(RTPathReal(szCWD, szResolved, sizeof(szResolved)));
    apszArgs[2] = szResolved;
    RTTESTI_CHECK_RC_RETV(RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, fFlags,
                                         NULL, NULL, NULL, pszAsUser, pszPassword,
                                         (void *)szCWD /* pvExtraData (CWD) */, &hProc),
                          VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus), VINF_SUCCESS);
    if (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0)
        RTTestIFailed("enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);
#endif

    /* Try non-existing directory. */
    RTTESTI_CHECK_RC_OK_RETV(RTPathTemp(szCWD, sizeof(szCWD)));
    RTTESTI_CHECK_RC_OK_RETV(RTPathAppend(szCWD, sizeof(szCWD), "no-such-directory" RTPATH_SLASH_STR "no-such-subdir" RTPATH_SLASH_STR "no-such-subsubdir"));
    apszArgs[2] = szCWD;
    rc = RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, fFlags,
                        NULL, NULL, NULL, pszAsUser, pszPassword,
                        (void *)szCWD /* pvExtraData (CWD) */, &hProc);
    if (rc != VERR_PATH_NOT_FOUND && rc != VERR_FILE_NOT_FOUND) /* Not sure why I get the latter on wsl1/ubuntu_24_04 */
        RTTestIFailed("Invalid CWD -> rc=%Rrc; expected VERR_PATH_NOT_FOUND or VERR_FILE_NOT_FOUND", rc);

    RTAssertSetMayPanic(fMayPanic);
    RTAssertSetQuiet(fQuiet);
}

static int tstRTCreateProcEx6Child(int argc, char **argv)
{
    int rc = RTR3InitExeNoArguments(0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    int cErrors = 0;
    char szValue[_16K];

    /*
     * Check for the environment variable we've set in the parent process.
     */
    if (argc >= 3 && strcmp(argv[2], "inherit") == 0)
    {
        if (!RTEnvExistEx(RTENV_DEFAULT, "testcase-child-6"))
        {
            RTStrmPrintf(g_pStdErr, "child6: Env.var. 'testcase-child-6' was not inherited from parent\n");
            cErrors++;
        }
    }
    else if (argc >= 3 && strstr(argv[2], "change-record") != NULL)
    {
        rc = RTEnvGetEx(RTENV_DEFAULT, "testcase-child-6", szValue, sizeof(szValue), NULL);
        if (RT_SUCCESS(rc) && strcmp(szValue, "changed"))
        {
            RTStrmPrintf(g_pStdErr, "child6: Env.var. 'testcase-child-6'='%s', expected 'changed'.\n", szValue);
            cErrors++;
        }
        else if (RT_FAILURE(rc))
        {
            RTStrmPrintf(g_pStdErr, "child6: RTEnvGetEx(,'testcase-child-6',,) -> %Rrc\n", rc);
            cErrors++;
        }
    }
    else
    {
        if (RTEnvExistEx(RTENV_DEFAULT, "testcase-child-6"))
        {
            RTStrmPrintf(g_pStdErr, "child6: Env.var. 'testcase-child-6' was inherited from parent\n");
            cErrors++;
        }
    }

    /*
     * Check the user name if present we didn't inherit from parent.
     */
    if (   argc >= 4
        && argv[3][0] != '\0'
        && strstr(argv[2], "noinherit") != NULL)
    {
        static struct
        {
            const char *pszVarNm;
            bool fReq;
        } const s_aVars[] =
        {
#ifdef RT_OS_WINDOWS
            { "USERNAME", true },
#else
            { "LOGNAME",  true },
            { "USER",    false },
#endif
        };
        for (unsigned i = 0; i < RT_ELEMENTS(s_aVars); i++)
        {
            rc = RTEnvGetEx(RTENV_DEFAULT, s_aVars[i].pszVarNm, szValue, sizeof(szValue), NULL);
            if (RT_SUCCESS(rc))
            {
                if (strcmp(szValue, argv[3]))
                {
                    RTStrmPrintf(g_pStdErr, "child6: env.var. '%s'='%s', expected '%s'\n",
                                 s_aVars[i].pszVarNm, szValue, argv[3]);
                    cErrors++;
                }
            }
            else if (rc != VERR_ENV_VAR_NOT_FOUND || s_aVars[i].fReq)
            {
                RTStrmPrintf(g_pStdErr, "child6: RTGetEnv('%s') -> %Rrc\n", s_aVars[i].pszVarNm, rc);
                cErrors++;
            }
        }
    }

#if 1
    /* For manual testing. */
    if (strcmp(argv[2],"noinherit") == 0)
    //if (strcmp(argv[2],"noinherit-change-record") == 0)
    {
        RTENV hEnv;
        rc = RTEnvClone(&hEnv, RTENV_DEFAULT);
        if (RT_SUCCESS(rc))
        {
            uint32_t cVars = RTEnvCountEx(hEnv);
            for (uint32_t i = 0; i < cVars; i++)
            {
                char szVarNm[_1K];
                rc = RTEnvGetByIndexEx(hEnv, i, szVarNm, sizeof(szVarNm), szValue, sizeof(szValue));
                if (RT_SUCCESS(rc))
                    RTStrmPrintf(g_pStdErr, "child6: #%u: %s=%s\n", i, szVarNm, szValue);
                else
                {
                    RTStrmPrintf(g_pStdErr, "child6: #%u: %Rrc\n", i, rc);
                    cErrors++;
                }
            }
            RTEnvDestroy(hEnv);
        }
        else
        {
            RTStrmPrintf(g_pStdErr, "child6: RTEnvClone failed: %Rrc\n", rc);
            cErrors++;
        }
    }
#endif

    return cErrors == 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static void tstRTCreateProcEx6(const char *pszAsUser, const char *pszPassword)
{
    RTTestISub("Profile environment");

    const char *apszArgs[5] =
    {
        g_szExecName,
        "--testcase-child-6",
        "inherit",
        pszAsUser,
        NULL
    };

    RTTESTI_CHECK_RC_RETV(RTEnvSetEx(RTENV_DEFAULT, "testcase-child-6", "true"), VINF_SUCCESS);

    /* Use the process environment first. */
    RTPROCESS hProc;
    RTTESTI_CHECK_RC_RETV(RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, 0 /*fFlags*/,
                                         NULL, NULL, NULL, pszAsUser, pszPassword, NULL, &hProc), VINF_SUCCESS);
    RTPROCSTATUS ProcStatus = { -1, RTPROCEXITREASON_ABEND };
    RTTESTI_CHECK_RC(RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus), VINF_SUCCESS);

    if (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0)
        RTTestIFailed("enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);

    /* Use the process environment first with a little change. */
    apszArgs[2] = "change-record";
    RTENV hEnvChange;
    RTTESTI_CHECK_RC_RETV(RTEnvCreateChangeRecord(&hEnvChange), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(RTEnvSetEx(hEnvChange, "testcase-child-6", "changed"), VINF_SUCCESS);
    int rc;
    RTTESTI_CHECK_RC(rc = RTProcCreateEx(g_szExecName, apszArgs, hEnvChange, RTPROC_FLAGS_ENV_CHANGE_RECORD,
                                         NULL, NULL, NULL, pszAsUser, pszPassword, NULL, &hProc), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        ProcStatus.enmReason = RTPROCEXITREASON_ABEND;
        ProcStatus.iStatus   = -1;
        RTTESTI_CHECK_RC(RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus), VINF_SUCCESS);

        if (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0)
            RTTestIFailed("enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);
    }


    /* Use profile environment this time. */
    apszArgs[2] = "noinherit";
    RTTESTI_CHECK_RC(rc = RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, RTPROC_FLAGS_PROFILE,
                                         NULL, NULL, NULL, pszAsUser, pszPassword, NULL, &hProc), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        ProcStatus.enmReason = RTPROCEXITREASON_ABEND;
        ProcStatus.iStatus   = -1;
        RTTESTI_CHECK_RC(RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus), VINF_SUCCESS);

        if (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0)
            RTTestIFailed("enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);
    }

    /* Use profile environment this time. */
    apszArgs[2] = "noinherit-change-record";
    RTTESTI_CHECK_RC(rc = RTProcCreateEx(g_szExecName, apszArgs, hEnvChange,
                                         RTPROC_FLAGS_PROFILE | RTPROC_FLAGS_ENV_CHANGE_RECORD,
                                         NULL, NULL, NULL, pszAsUser, pszPassword, NULL, &hProc), VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        ProcStatus.enmReason = RTPROCEXITREASON_ABEND;
        ProcStatus.iStatus   = -1;
        RTTESTI_CHECK_RC(RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus), VINF_SUCCESS);

        if (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0)
            RTTestIFailed("enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);
    }


    RTTESTI_CHECK_RC(RTEnvDestroy(hEnvChange), VINF_SUCCESS);

    /*
     * Restore the environment and check that the PROFILE flag didn't mess with
     * the process environment.  (Note! The bug may be elsewhere as well.)
     */
    RTTESTI_CHECK_RC(RTEnvUnsetEx(RTENV_DEFAULT, "testcase-child-6"), VINF_SUCCESS);

    RTENV hEnvCur;
    RTTESTI_CHECK_RC_RETV(RTEnvClone(&hEnvCur, RTENV_DEFAULT), VINF_SUCCESS);
    uint32_t cCurrent = RTEnvCountEx(hEnvCur);
    uint32_t cInitial = RTEnvCountEx(g_hEnvInitial);
    RTTESTI_CHECK_MSG(cCurrent == cInitial, ("cCurrent=%u cInitial=%u\n", cCurrent, cInitial));
    uint32_t    cVars1;
    RTENV       hEnv1,    hEnv2;
    const char *pszEnv1, *pszEnv2;
    if (cCurrent >= cInitial)
    {
        hEnv1   = hEnvCur;
        pszEnv1 = "current";
        cVars1  = cCurrent;
        hEnv2   = g_hEnvInitial;
        pszEnv2 = "initial";
    }
    else
    {
        hEnv2   = hEnvCur;
        pszEnv2 = "current";
        hEnv1   = g_hEnvInitial;
        pszEnv1 = "initial";
        cVars1  = cInitial;
    }
    for (uint32_t i = 0; i < cVars1; i++)
    {
        char szValue1[_16K];
        char szVarNm[_1K];
        rc = RTEnvGetByIndexEx(hEnv1, i, szVarNm, sizeof(szVarNm), szValue1, sizeof(szValue1));
        if (RT_SUCCESS(rc))
        {
            char szValue2[_16K];
            rc = RTEnvGetEx(hEnv2, szVarNm, szValue2, sizeof(szValue2), NULL);
            if (RT_SUCCESS(rc))
            {
                if (strcmp(szValue1, szValue2) != 0)
                {
                    RTTestIFailed("Variable '%s' differs", szVarNm);
                    RTTestIFailureDetails("%s: '%s'\n"
                                          "%s: '%s'\n",
                                          pszEnv1, szValue1,
                                          pszEnv2, szValue2);
                }
            }
            else
                RTTestIFailed("RTEnvGetEx(%s,%s,,) failed: %Rrc", pszEnv2, szVarNm, rc);

        }
        else
            RTTestIFailed("RTEnvGetByIndexEx(%s,%u,,,,) failed: %Rrc", pszEnv1, i, rc);
    }

    RTEnvDestroy(hEnvCur);
}


static int tstRTCreateProcEx5Child(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    uint32_t cErrors = 0;

    /* Check that the OS thinks we're running as the user we're supposed to. */
    char *pszUser;
    rc = RTProcQueryUsernameA(NIL_RTPROCESS, &pszUser);
    if (RT_SUCCESS(rc))
    {
#ifdef RT_OS_WINDOWS
        if (RTStrICmp(pszUser, argv[2]) != 0)
#else
        if (RTStrCmp(pszUser, argv[2]) != 0)
#endif
        {
            RTStrmPrintf(g_pStdErr, "child4: user name is '%s', expected '%s'\n", pszUser, argv[2]);
            cErrors++;
        }
        RTStrFree(pszUser);
    }
    else
    {
        RTStrmPrintf(g_pStdErr, "child4: RTProcQueryUsernameA failed: %Rrc\n", rc);
        cErrors++;
    }

    return cErrors == 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static void tstRTCreateProcEx5(const char *pszUser, const char *pszPassword)
{
    RTTestISubF("As user \"%s\" with password \"%s\"", pszUser, pszPassword);
    RTTESTI_CHECK_RETV(pszUser && *pszUser);

    const char * apszArgs[] =
    {
        "test", /* user name */
        "--testcase-child-5",
        pszUser,
        NULL
    };

    /* Test for invalid logons. */
    RTPROCESS hProc;
    int rc = RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, 0 /*fFlags*/, NULL, NULL, NULL,
                            "non-existing-user", "wrong-password", NULL, &hProc);
    if (rc != VERR_AUTHENTICATION_FAILURE && rc != VERR_PRIVILEGE_NOT_HELD && rc != VERR_PROC_TCB_PRIV_NOT_HELD)
        RTTestIFailed("rc=%Rrc", rc);

    /* Test for invalid application. */
    RTTESTI_CHECK_RC(RTProcCreateEx("non-existing-app", apszArgs, RTENV_DEFAULT, 0 /*fFlags*/, NULL,
                                    NULL, NULL, NULL, NULL, NULL, &hProc), VERR_FILE_NOT_FOUND);

    /* Test a (hopefully) valid user/password logon (given by parameters of this function). */
    RTTESTI_CHECK_RC_RETV(RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, 0 /*fFlags*/, NULL,
                                         NULL, NULL, pszUser, pszPassword, NULL, &hProc), VINF_SUCCESS);
    RTPROCSTATUS ProcStatus = { -1, RTPROCEXITREASON_ABEND };
    RTTESTI_CHECK_RC(RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus), VINF_SUCCESS);

    if (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0)
        RTTestIFailed("enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);
}


static int tstRTCreateProcEx4Child(int argc, char **argv)
{
    int rc = RTR3InitExeNoArguments(0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    int cErrors = 0;
    if (argc >= (int)RT_ELEMENTS(g_apszArgs4))
    {
        RTStrmPrintf(g_pStdErr,
                     "Expected argument count to be <= %u, got %u\n",
                     argc, RT_ELEMENTS(g_apszArgs4));
        cErrors++;
    }

    for (int i = 0; i < RT_MIN(argc, (int)RT_ELEMENTS(g_apszArgs4) - 1); i++)
        if (strcmp(argv[i], g_apszArgs4[i]))
        {
            RTStrmPrintf(g_pStdErr,
                         "child4: argv[%2u]='%s'\n"
                         "child4: expected='%s'\n",
                         i, argv[i], g_apszArgs4[i]);
            cErrors++;
        }

    return cErrors == 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static void tstRTCreateProcEx4(const char *pszAsUser, const char *pszPassword)
{
    RTTestISub("Argument with spaces and stuff");

    RTPROCESS hProc;
    RTTESTI_CHECK_RC_RETV(RTProcCreateEx(g_szExecName, g_apszArgs4, RTENV_DEFAULT, 0 /*fFlags*/, NULL,
                                         NULL, NULL, pszAsUser, pszPassword, NULL, &hProc), VINF_SUCCESS);
    RTPROCSTATUS ProcStatus = { -1, RTPROCEXITREASON_ABEND };
    RTTESTI_CHECK_RC(RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus), VINF_SUCCESS);

    if (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0)
        RTTestIFailed("enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);
}


static int tstRTCreateProcEx3Child(void)
{
    int rc = RTR3InitExeNoArguments(0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    RTStrmPrintf(g_pStdOut, "w"); RTStrmFlush(g_pStdOut);
    RTStrmPrintf(g_pStdErr, "o"); RTStrmFlush(g_pStdErr);
    RTStrmPrintf(g_pStdOut, "r"); RTStrmFlush(g_pStdOut);
    RTStrmPrintf(g_pStdErr, "k"); RTStrmFlush(g_pStdErr);
    RTStrmPrintf(g_pStdOut, "s");

    return RTEXITCODE_SUCCESS;
}

static void tstRTCreateProcEx3(const char *pszAsUser, const char *pszPassword)
{
    RTTestISub("Standard Out+Err");

    RTPIPE hPipeR, hPipeW;
    RTTESTI_CHECK_RC_RETV(RTPipeCreate(&hPipeR, &hPipeW, RTPIPE_C_INHERIT_WRITE), VINF_SUCCESS);
    const char * apszArgs[3] =
    {
        "non-existing-non-executable-file",
        "--testcase-child-3",
        NULL
    };
    RTHANDLE Handle;
    Handle.enmType = RTHANDLETYPE_PIPE;
    Handle.u.hPipe = hPipeW;
    RTPROCESS hProc;
    RTTESTI_CHECK_RC_RETV(RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, 0 /*fFlags*/, NULL,
                                         &Handle, &Handle, pszAsUser, pszPassword, NULL, &hProc), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTPipeClose(hPipeW), VINF_SUCCESS);

    char    szOutput[_4K];
    size_t  offOutput = 0;
    for (;;)
    {
        size_t cbLeft = sizeof(szOutput) - 1 - offOutput;
        RTTESTI_CHECK(cbLeft > 0);
        if (cbLeft == 0)
            break;

        size_t cbRead;
        int rc = RTPipeReadBlocking(hPipeR, &szOutput[offOutput], cbLeft, &cbRead);
        if (RT_FAILURE(rc))
        {
            RTTESTI_CHECK_RC(rc, VERR_BROKEN_PIPE);
            break;
        }
        offOutput += cbRead;
    }
    szOutput[offOutput] = '\0';
    RTTESTI_CHECK_RC(RTPipeClose(hPipeR), VINF_SUCCESS);

    RTPROCSTATUS ProcStatus = { -1, RTPROCEXITREASON_ABEND };
    RTTESTI_CHECK_RC(RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus), VINF_SUCCESS);
    RTThreadSleep(10);

    if (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0)
        RTTestIFailed("enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);
    else if (   offOutput != sizeof("works") - 1
             || strcmp(szOutput, "works"))
        RTTestIFailed("wrong output: \"%s\" (len=%u)", szOutput, offOutput);
}


static int tstRTCreateProcEx2Child(void)
{
    int rc = RTR3InitExeNoArguments(0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    RTStrmPrintf(g_pStdErr, "howdy");
    RTStrmPrintf(g_pStdOut, "ignore this output\n");

    return RTEXITCODE_SUCCESS;
}

static void tstRTCreateProcEx2(const char *pszAsUser, const char *pszPassword)
{
    RTTestISub("Standard Err");

    RTPIPE hPipeR, hPipeW;
    RTTESTI_CHECK_RC_RETV(RTPipeCreate(&hPipeR, &hPipeW, RTPIPE_C_INHERIT_WRITE), VINF_SUCCESS);
    const char * apszArgs[3] =
    {
        "non-existing-non-executable-file",
        "--testcase-child-2",
        NULL
    };
    RTHANDLE Handle;
    Handle.enmType = RTHANDLETYPE_PIPE;
    Handle.u.hPipe = hPipeW;
    RTPROCESS hProc;
    RTTESTI_CHECK_RC_RETV(RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, 0 /*fFlags*/, NULL,
                                         NULL, &Handle, pszAsUser, pszPassword, NULL, &hProc), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTPipeClose(hPipeW), VINF_SUCCESS);

    char    szOutput[_4K];
    size_t  offOutput = 0;
    for (;;)
    {
        size_t cbLeft = sizeof(szOutput) - 1 - offOutput;
        RTTESTI_CHECK(cbLeft > 0);
        if (cbLeft == 0)
            break;

        size_t cbRead;
        int rc = RTPipeReadBlocking(hPipeR, &szOutput[offOutput], cbLeft, &cbRead);
        if (RT_FAILURE(rc))
        {
            RTTESTI_CHECK_RC(rc, VERR_BROKEN_PIPE);
            break;
        }
        offOutput += cbRead;
    }
    szOutput[offOutput] = '\0';
    RTTESTI_CHECK_RC(RTPipeClose(hPipeR), VINF_SUCCESS);

    RTPROCSTATUS ProcStatus = { -1, RTPROCEXITREASON_ABEND };
    RTTESTI_CHECK_RC(RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus), VINF_SUCCESS);
    RTThreadSleep(10);

    if (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0)
        RTTestIFailed("enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);
    else if (   offOutput != sizeof("howdy") - 1
             || strcmp(szOutput, "howdy"))
        RTTestIFailed("wrong output: \"%s\" (len=%u)", szOutput, offOutput);
}


static int tstRTCreateProcEx1Child(void)
{
    int rc = RTR3InitExeNoArguments(0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    RTPrintf("it works");
    RTStrmPrintf(g_pStdErr, "ignore this output\n");

    return RTEXITCODE_SUCCESS;
}


static void tstRTCreateProcEx1(const char *pszAsUser, const char *pszPassword)
{
    RTTestISub("Standard Out");

    RTPIPE hPipeR, hPipeW;
    RTTESTI_CHECK_RC_RETV(RTPipeCreate(&hPipeR, &hPipeW, RTPIPE_C_INHERIT_WRITE), VINF_SUCCESS);
    const char * apszArgs[3] =
    {
        "non-existing-non-executable-file",
        "--testcase-child-1",
        NULL
    };
    RTHANDLE Handle;
    Handle.enmType = RTHANDLETYPE_PIPE;
    Handle.u.hPipe = hPipeW;
    RTPROCESS hProc;
    RTTESTI_CHECK_RC_RETV(RTProcCreateEx(g_szExecName, apszArgs, RTENV_DEFAULT, 0 /*fFlags*/, NULL,
                                         &Handle, NULL, pszAsUser, pszPassword, NULL, &hProc), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTPipeClose(hPipeW), VINF_SUCCESS);

    char    szOutput[_4K];
    size_t  offOutput = 0;
    for (;;)
    {
        size_t cbLeft = sizeof(szOutput) - 1 - offOutput;
        RTTESTI_CHECK(cbLeft > 0);
        if (cbLeft == 0)
            break;

        size_t cbRead;
        int rc = RTPipeReadBlocking(hPipeR, &szOutput[offOutput], cbLeft, &cbRead);
        if (RT_FAILURE(rc))
        {
            RTTESTI_CHECK_RC(rc, VERR_BROKEN_PIPE);
            break;
        }
        offOutput += cbRead;
    }
    szOutput[offOutput] = '\0';
    RTTESTI_CHECK_RC(RTPipeClose(hPipeR), VINF_SUCCESS);

    RTPROCSTATUS ProcStatus = { -1, RTPROCEXITREASON_ABEND };
    RTTESTI_CHECK_RC(RTProcWait(hProc, RTPROCWAIT_FLAGS_BLOCK, &ProcStatus), VINF_SUCCESS);

    if (ProcStatus.enmReason != RTPROCEXITREASON_NORMAL || ProcStatus.iStatus != 0)
        RTTestIFailed("enmReason=%d iStatus=%d", ProcStatus.enmReason, ProcStatus.iStatus);
    else if (   offOutput != sizeof("it works") - 1
             || strcmp(szOutput, "it works"))
        RTTestIFailed("wrong output: \"%s\" (len=%u)", szOutput, offOutput);
}


int main(int argc, char **argv)
{
    /*
     * Deal with child processes first.
     */
    if (argc == 2 && !strcmp(argv[1], "--testcase-child-1"))
        return tstRTCreateProcEx1Child();
    if (argc == 2 && !strcmp(argv[1], "--testcase-child-2"))
        return tstRTCreateProcEx2Child();
    if (argc == 2 && !strcmp(argv[1], "--testcase-child-3"))
        return tstRTCreateProcEx3Child();
    if (argc >= 5 && !strcmp(argv[1], "--testcase-child-4"))
        return tstRTCreateProcEx4Child(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "--testcase-child-5"))
        return tstRTCreateProcEx5Child(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "--testcase-child-6"))
        return tstRTCreateProcEx6Child(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "--testcase-child-cwd"))
        return tstRTCreateProcExCwdChild(argc, argv);

    /*
     * Main process.
     */
    const char *pszAsUser   = NULL;
    const char *pszPassword = NULL;
    if (argc != 1)
    {
        if (argc != 4 || strcmp(argv[1], "--as-user"))
            return 99;
        pszAsUser   = argv[2];
        pszPassword = argv[3];
    }

    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTProcCreateEx", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * Init globals.
     */
    if (!RTProcGetExecutablePath(g_szExecName, sizeof(g_szExecName)))
        RTStrCopy(g_szExecName, sizeof(g_szExecName), argv[0]);
    RTTESTI_CHECK_RC(RTEnvClone(&g_hEnvInitial, RTENV_DEFAULT), VINF_SUCCESS);

    /*
     * The tests.
     */
    tstRTCreateProcEx1(pszAsUser, pszPassword);
    tstRTCreateProcEx2(pszAsUser, pszPassword);
    tstRTCreateProcEx3(pszAsUser, pszPassword);
    tstRTCreateProcEx4(pszAsUser, pszPassword);
    if (pszAsUser)
        tstRTCreateProcEx5(pszAsUser, pszPassword);
    tstRTCreateProcEx6(pszAsUser, pszPassword);
    tstRTCreateProcExCwd(pszAsUser, pszPassword);

    /** @todo Cover files, ++ */

    RTEnvDestroy(g_hEnvInitial);

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

