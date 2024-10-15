/* $Id: tstRTR0SemMutex.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT R0 Testcase - Mutex Semaphores.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <iprt/semaphore.h>

#include <iprt/errcore.h>
#include <VBox/sup.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/thread.h>
#include "tstRTR0SemMutex.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The mutex used in test #2. */
static RTSEMMUTEX g_hMtxTest2 = NIL_RTSEMMUTEX;



/**
 * Service request callback function.
 *
 * @returns VBox status code.
 * @param   pSession    The caller's session.
 * @param   u64Arg      64-bit integer argument.
 * @param   pReqHdr     The request header. Input / Output. Optional.
 */
DECLEXPORT(int) TSTRTR0SemMutexSrvReqHandler(PSUPDRVSESSION pSession, uint32_t uOperation,
                                             uint64_t u64Arg, PSUPR0SERVICEREQHDR pReqHdr)
{
    NOREF(pSession);
    if (!RT_VALID_PTR(pReqHdr))
        return VERR_INVALID_PARAMETER;
    char   *pszErr = (char *)(pReqHdr + 1);
    size_t  cchErr = pReqHdr->cbReq - sizeof(*pReqHdr);
    if (cchErr < 32 || cchErr >= 0x10000)
        return VERR_INVALID_PARAMETER;
    *pszErr = '\0';

#define SET_ERROR(szFmt)                do { if (!*pszErr) RTStrPrintf(pszErr, cchErr, "!" szFmt); } while (0)
#define SET_ERROR1(szFmt, a1)           do { if (!*pszErr) RTStrPrintf(pszErr, cchErr, "!" szFmt, a1); } while (0)
#define SET_ERROR2(szFmt, a1, a2)       do { if (!*pszErr) RTStrPrintf(pszErr, cchErr, "!" szFmt, a1, a2); } while (0)
#define SET_ERROR3(szFmt, a1, a2, a3)   do { if (!*pszErr) RTStrPrintf(pszErr, cchErr, "!" szFmt, a1, a2, a3); } while (0)
#define CHECK_RC_BREAK(rc, rcExpect, szOp) \
        if ((rc) != (rcExpect)) \
        { \
            RTStrPrintf(pszErr, cchErr, "!%s -> %Rrc, expected %Rrc. line %u", szOp, rc, rcExpect, __LINE__); \
            SUPR0Printf("%s -> %d, expected %d. line %u", szOp, rc, rcExpect, __LINE__); \
            break; \
        }

    /*
     * Set up test timeout (when applicable).
     */
    if (u64Arg > 120)
    {
        SET_ERROR1("Timeout is too large (max 120): %lld",  u64Arg);
        return VINF_SUCCESS;
    }
    uint64_t const  StartTS = RTTimeSystemMilliTS();
    uint32_t const  cMsMax  = (uint32_t)u64Arg * 1000;

    /*
     * The big switch.
     */
    RTSEMMUTEX      hMtx;
    int             rc;
    switch (uOperation)
    {
        case TSTRTR0SEMMUTEX_SANITY_OK:
            break;

        case TSTRTR0SEMMUTEX_SANITY_FAILURE:
            SET_ERROR1("42failure42%1024s", "");
            break;

        case TSTRTR0SEMMUTEX_BASIC:
            rc = RTSemMutexCreate(&hMtx);
            CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexCreate");
            do
            {
                /*
                 * The interruptible version first.
                 */
                /* simple request and release, polling. */
                rc = RTSemMutexRequestNoResume(hMtx, 0);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequestNoResume");
                rc = RTSemMutexRelease(hMtx);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease");

                /* simple request and release, wait for ever. */
                rc = RTSemMutexRequestNoResume(hMtx, RT_INDEFINITE_WAIT);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequestNoResume(indef_wait)");
                rc = RTSemMutexRelease(hMtx);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease");

                /* simple request and release, wait a tiny while. */
                rc = RTSemMutexRequestNoResume(hMtx, 133);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequestNoResume(133)");
                rc = RTSemMutexRelease(hMtx);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease");

                /* nested request and release. */
                rc = RTSemMutexRequestNoResume(hMtx, RT_INDEFINITE_WAIT);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequestNoResume#1");
                rc = RTSemMutexRequestNoResume(hMtx, RT_INDEFINITE_WAIT);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequestNoResume#2");
                rc = RTSemMutexRequestNoResume(hMtx, RT_INDEFINITE_WAIT);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequestNoResume#3");
                rc = RTSemMutexRelease(hMtx);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease#3");
                rc = RTSemMutexRequestNoResume(hMtx, RT_INDEFINITE_WAIT);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequestNoResume#3b");
                rc = RTSemMutexRelease(hMtx);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease#3b");
                rc = RTSemMutexRelease(hMtx);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease#2");
                rc = RTSemMutexRelease(hMtx);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease#1");

                /*
                 * The uninterruptible variant.
                 */
                /* simple request and release, polling. */
                rc = RTSemMutexRequest(hMtx, 0);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequest");
                rc = RTSemMutexRelease(hMtx);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease");

                /* simple request and release, wait for ever. */
                rc = RTSemMutexRequest(hMtx, RT_INDEFINITE_WAIT);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequest(indef_wait)");
                rc = RTSemMutexRelease(hMtx);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease");

                /* simple request and release, wait a tiny while. */
                rc = RTSemMutexRequest(hMtx, 133);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequest(133)");
                rc = RTSemMutexRelease(hMtx);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease");

                /* nested request and release. */
                rc = RTSemMutexRequest(hMtx, RT_INDEFINITE_WAIT);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequest#1");
                rc = RTSemMutexRequest(hMtx, RT_INDEFINITE_WAIT);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequest#2");
                rc = RTSemMutexRequest(hMtx, RT_INDEFINITE_WAIT);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequest#3");
                rc = RTSemMutexRelease(hMtx);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease#3");
                rc = RTSemMutexRequest(hMtx, RT_INDEFINITE_WAIT);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequest#3b");
                rc = RTSemMutexRelease(hMtx);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease#3b");
                rc = RTSemMutexRelease(hMtx);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease#2");
                rc = RTSemMutexRelease(hMtx);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease#1");

            } while (false);

            rc = RTSemMutexDestroy(hMtx);
            CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexDestroy");
            break;

        case TSTRTR0SEMMUTEX_TEST2_SETUP:
        case TSTRTR0SEMMUTEX_TEST3_SETUP:
        case TSTRTR0SEMMUTEX_TEST4_SETUP:
            rc = RTSemMutexCreate(&g_hMtxTest2);
            CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexCreate");
            break;

        case TSTRTR0SEMMUTEX_TEST2_DO:
            for (unsigned i = 0; i < 200; i++)
            {
                if (i & 1)
                {
                    rc = RTSemMutexRequestNoResume(g_hMtxTest2, RT_INDEFINITE_WAIT);
                    CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequestNoResume(,indef_wait)");
                }
                else
                {
                    rc = RTSemMutexRequestNoResume(g_hMtxTest2, 30000);
                    CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequestNoResume(,30000)");
                }
                RTThreadSleep(1);
                rc = RTSemMutexRelease(g_hMtxTest2);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease");

                if ((i % 16) == 15 && RTTimeSystemMilliTS() - StartTS >= cMsMax)
                    break;
            }
            break;


        case TSTRTR0SEMMUTEX_TEST3_DO:
            for (unsigned i = 0; i < 1000000; i++)
            {
                if (i & 1)
                {
                    rc = RTSemMutexRequestNoResume(g_hMtxTest2, RT_INDEFINITE_WAIT);
                    CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequestNoResume(,indef_wait)");
                }
                else
                {
                    rc = RTSemMutexRequestNoResume(g_hMtxTest2, 30000);
                    CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequestNoResume(,30000)");
                }
                rc = RTSemMutexRelease(g_hMtxTest2);
                CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease");

                if ((i % 256) == 255 && RTTimeSystemMilliTS() - StartTS >= cMsMax)
                    break;
            }
            break;

        case TSTRTR0SEMMUTEX_TEST4_DO:
            for (unsigned i = 0; i < 1024; i++)
            {
                rc = RTSemMutexRequestNoResume(g_hMtxTest2, (i % 32));
                if (rc != VERR_TIMEOUT)
                {
                    CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRequestNoResume");
                    RTThreadSleep(1000);

                    rc = RTSemMutexRelease(g_hMtxTest2);
                    CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexRelease");
                }

                if (RTTimeSystemMilliTS() - StartTS >= cMsMax)
                    break;
            }
            break;

        case TSTRTR0SEMMUTEX_TEST2_CLEANUP:
        case TSTRTR0SEMMUTEX_TEST3_CLEANUP:
        case TSTRTR0SEMMUTEX_TEST4_CLEANUP:
            rc = RTSemMutexDestroy(g_hMtxTest2);
            CHECK_RC_BREAK(rc, VINF_SUCCESS, "RTSemMutexCreate");
            g_hMtxTest2 = NIL_RTSEMMUTEX;
            break;


        default:
            SET_ERROR1("Unknown test #%d", uOperation);
            break;
    }

    /* The error indicator is the '!' in the message buffer. */
    return VINF_SUCCESS;
}

