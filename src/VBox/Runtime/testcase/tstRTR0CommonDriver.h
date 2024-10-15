/* $Id: tstRTR0CommonDriver.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT R0 Testcase - Common header for the testcase drivers.
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

#ifndef IPRT_INCLUDED_SRC_testcase_tstRTR0CommonDriver_h
#define IPRT_INCLUDED_SRC_testcase_tstRTR0CommonDriver_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/ctype.h>
#include <iprt/string.h>
#include <VBox/sup.h>
#include "tstRTR0CommonReq.h"


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** The test handle. */
RTTEST              g_hTest;
/** The test & service name. */
char                g_szSrvName[64];
/** The length of the service name.  */
size_t              g_cchSrvName;
/** The base address of the service module. */
void               *g_pvImageBase;


/**
 * Initializes the test driver.
 *
 * This means creating a test instance (RTTEST), initializing the support
 * library, and loading the service module.
 *
 * @returns RTEXITCODE_SUCCESS on success, appropriate exit code on failure.
 * @param   pszTestServiceName      The name of the test and service.  This
 *                                  will be used when creating the test
 *                                  instance as well as when talking with the
 *                                  kernel side of the test.
 *
 *                                  The ring-0 module name will be derived from
 *                                  this + '.r0'.
 *
 *                                  The service request handler function name
 *                                  will be derived by upper casing the first
 *                                  chars and appending 'SrvReqHandler'.
 *
 */
static RTEXITCODE RTR3TestR0CommonDriverInit(const char *pszTestServiceName)
{
    /*
     * Init the test.
     */
    RTEXITCODE rcExit = RTTestInitAndCreate(pszTestServiceName, &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    /*
     * Init the globals.
     */
    g_cchSrvName = strlen(pszTestServiceName);
    int rc = RTStrCopy(g_szSrvName, sizeof(g_szSrvName), pszTestServiceName);
    if (rc != VINF_SUCCESS)
    {
        RTTestFailed(g_hTest, "The test name is too long! (%zu bytes)", g_cchSrvName);
        return RTTestSummaryAndDestroy(g_hTest);
    }

    /*
     * Initialize the support driver session.
     */
    PSUPDRVSESSION pSession;
    rc = SUPR3Init(&pSession);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "SUPR3Init failed with rc=%Rrc\n", rc);
        return RTTestSummaryAndDestroy(g_hTest);
    }

    char szPath[RTPATH_MAX];
    rc = RTPathExecDir(szPath, sizeof(szPath));
    if (RT_SUCCESS(rc))
        rc = RTPathAppend(szPath, sizeof(szPath), pszTestServiceName);
    if (RT_SUCCESS(rc))
        rc = RTStrCat(szPath, sizeof(szPath), ".r0");
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "Failed constructing .r0 filename (rc=%Rrc)", rc);
        return RTTestSummaryAndDestroy(g_hTest);
    }

    static char const s_szSrvReqHandlerSuff[] = "SrvReqHandler";
    char szSrvReqHandler[sizeof(g_szSrvName) + sizeof(s_szSrvReqHandlerSuff)];
    memcpy(mempcpy(szSrvReqHandler, g_szSrvName, g_cchSrvName), s_szSrvReqHandlerSuff, sizeof(s_szSrvReqHandlerSuff));
    for (size_t off = 0; RT_C_IS_LOWER(szSrvReqHandler[off]); off++)
        szSrvReqHandler[off] = RT_C_TO_UPPER(szSrvReqHandler[off]);

    rc = SUPR3LoadServiceModule(szPath, pszTestServiceName, szSrvReqHandler, &g_pvImageBase);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "SUPR3LoadServiceModule(%s,%s,%s,) failed with rc=%Rrc\n",
                     szPath, pszTestServiceName, szSrvReqHandler, rc);
        return RTTestSummaryAndDestroy(g_hTest);
    }

    /*
     * Do the sanity checks.
     */
    RTTestSub(g_hTest, "Sanity");
    RTTSTR0REQ Req;
    Req.Hdr.u32Magic    = SUPR0SERVICEREQHDR_MAGIC;
    Req.Hdr.cbReq       = sizeof(Req);
    memset(Req.szMsg, 0xef, sizeof(Req.szMsg));
    RTTESTI_CHECK_RC(rc = SUPR3CallR0Service(g_szSrvName, g_cchSrvName, RTTSTR0REQ_SANITY_OK, 0, &Req.Hdr), VINF_SUCCESS);
    if (RT_FAILURE(rc))
        return RTTestSummaryAndDestroy(g_hTest);
    RTTESTI_CHECK_MSG(Req.szMsg[0] == '\0', ("%s", Req.szMsg));
    if (Req.szMsg[0] != '\0')
        return RTTestSummaryAndDestroy(g_hTest);


    Req.Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
    Req.Hdr.cbReq    = sizeof(Req);
    Req.szMsg[0]     = '\0';
    memset(Req.szMsg, 0xfe, sizeof(Req.szMsg));
    RTTESTI_CHECK_RC(rc = SUPR3CallR0Service(g_szSrvName, g_cchSrvName, RTTSTR0REQ_SANITY_FAILURE, 0, &Req.Hdr), VINF_SUCCESS);
    if (RT_FAILURE(rc))
        return RTTestSummaryAndDestroy(g_hTest);
    rc = !strncmp(Req.szMsg, "!42failure42", sizeof("!42failure42"));
    if (rc)
    {
        RTTestFailed(g_hTest, "the negative sanity check failed");
        return RTTestSummaryAndDestroy(g_hTest);
    }
    RTTestSubDone(g_hTest);

    return RTEXITCODE_SUCCESS;
}


/**
 * Processes the messages in the request.
 *
 * @returns true on success, false on failure.
 * @param   pReq                The request.
 */
static bool rtR3TestR0ProcessMessages(PRTTSTR0REQ pReq)
{
    /*
     * Process the message strings.
     *
     * We can have multiple failures and info messages packed into szMsg.  They
     * are separated by a double newline.  The kind of message is indicated by
     * the first character, '!' means error and '?' means info message. '$' means
     * the test was skipped because a feature is not supported on the host.
     */
    bool fRc = true;
    if (pReq->szMsg[0])
    {
        pReq->szMsg[sizeof(pReq->szMsg) - 1] = '\0'; /* paranoia */

        char *pszNext = &pReq->szMsg[0];
        do
        {
            char *pszCur = pszNext;
            do
                pszNext = strpbrk(pszNext + 1, "!?$");
            while (pszNext && (pszNext[-1] != '\n' || pszNext[-2] != '\n'));

            char *pszEnd = pszNext ? pszNext - 1 : strchr(pszCur, '\0');
            while (   (uintptr_t)pszEnd > (uintptr_t)pszCur
                   && pszEnd[-1] == '\n')
                *--pszEnd = '\0';

            if (*pszCur == '!')
            {
                RTTestFailed(g_hTest, "%s", pszCur + 1);
                fRc = false;
            }
            else if (*pszCur == '$')
            {
                RTTestSkipped(g_hTest, "%s", pszCur + 1);
            }
            else
                RTTestPrintfNl(g_hTest, RTTESTLVL_ALWAYS, "%s", pszCur + 1);
        } while (pszNext);
    }

    return fRc;
}


/**
 * Performs a simple test with an argument (@a u64Arg).
 *
 * @returns true on success, false on failure.
 * @param   uOperation          The operation to perform.
 * @param   u64Arg              64-bit argument.
 * @param   pszTestFmt          The sub-test name.
 * @param   ...                 Format arguments.
 */
DECLINLINE(bool) RTR3TestR0SimpleTestWithArg(uint32_t uOperation, uint64_t u64Arg, const char *pszTestFmt, ...)
{
    va_list va;
    va_start(va, pszTestFmt);
    RTTestSubV(g_hTest, pszTestFmt, va);
    va_end(va);

    RTTSTR0REQ Req;
    Req.Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
    Req.Hdr.cbReq = sizeof(Req);
    RT_ZERO(Req.szMsg);
    int rc = SUPR3CallR0Service(g_szSrvName, g_cchSrvName, uOperation, u64Arg, &Req.Hdr);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "SUPR3CallR0Service failed with rc=%Rrc", rc);
        return false;
    }

    return rtR3TestR0ProcessMessages(&Req);
}


/**
 * Performs a simple test.
 *
 * @returns true on success, false on failure.
 * @param   uOperation          The operation to perform.
 * @param   pszTestFmt          The sub-test name.
 * @param   ...                 Format arguments.
 */
static bool RTR3TestR0SimpleTest(uint32_t uOperation, const char *pszTestFmt, ...)
{
    va_list va;
    va_start(va, pszTestFmt);
    RTTestSubV(g_hTest, pszTestFmt, va);
    va_end(va);

    RTTSTR0REQ Req;
    Req.Hdr.u32Magic = SUPR0SERVICEREQHDR_MAGIC;
    Req.Hdr.cbReq = sizeof(Req);
    RT_ZERO(Req.szMsg);
    int rc = SUPR3CallR0Service(g_szSrvName, g_cchSrvName, uOperation, 0, &Req.Hdr);
    if (RT_FAILURE(rc))
    {
        RTTestFailed(g_hTest, "SUPR3CallR0Service failed with rc=%Rrc", rc);
        return false;
    }

    return rtR3TestR0ProcessMessages(&Req);
}

#endif /* !IPRT_INCLUDED_SRC_testcase_tstRTR0CommonDriver_h */

