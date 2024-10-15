/* $Id: tstRTR0DbgKrnlInfo.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT R0 Testcase - Debug kernel information.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <iprt/thread.h>

#include <iprt/asm-amd64-x86.h>
#include <iprt/err.h>
#include <iprt/time.h>
#include <iprt/string.h>
#include <VBox/sup.h>
#include <iprt/dbg.h>
#include "tstRTR0DbgKrnlInfo.h"
#include "tstRTR0Common.h"


/**
 * Service request callback function.
 *
 * @returns VBox status code.
 * @param   pSession    The caller's session.
 * @param   u64Arg      64-bit integer argument.
 * @param   pReqHdr     The request header. Input / Output. Optional.
 */
DECLEXPORT(int) TSTR0DbgKrnlInfoSrvReqHandler(PSUPDRVSESSION pSession, uint32_t uOperation,
                                                   uint64_t u64Arg, PSUPR0SERVICEREQHDR pReqHdr)
{
    NOREF(pSession);
    if (u64Arg)
        return VERR_INVALID_PARAMETER;
    if (!RT_VALID_PTR(pReqHdr))
        return VERR_INVALID_PARAMETER;
    char   *pszErr = (char *)(pReqHdr + 1);
    size_t  cchErr = pReqHdr->cbReq - sizeof(*pReqHdr);
    if (cchErr < 32 || cchErr >= 0x10000)
        return VERR_INVALID_PARAMETER;
    *pszErr = '\0';

    /*
     * The big switch.
     */
    bool fSavedMayPanic = RTAssertSetMayPanic(false); /* Don't crash the host with strict builds! */
    RTDBGKRNLINFO hKrnlInfo = NIL_RTDBGKRNLINFO;
    switch (uOperation)
    {
        case TSTRTR0DBGKRNLINFO_SANITY_OK:
            break;

        case TSTRTR0DBGKRNLINFO_SANITY_FAILURE:
            RTStrPrintf(pszErr, cchErr, "!42failure42%1024s", "");
            break;

        case TSTRTR0DBGKRNLINFO_BASIC:
        {
            RTR0TESTR0_CHECK_RC_BREAK(RTR0DbgKrnlInfoOpen(&hKrnlInfo, 1), VERR_INVALID_PARAMETER);
            RTR0TESTR0_CHECK_RC_BREAK(RTR0DbgKrnlInfoOpen(NULL, 0), VERR_INVALID_PARAMETER);
            RTR0TESTR0_CHECK_RC_BREAK(RTR0DbgKrnlInfoOpen(&hKrnlInfo, 0), VINF_SUCCESS);

            size_t offMemb;
            RTR0TESTR0_CHECK_RC_BREAK(RTR0DbgKrnlInfoQueryMember(NULL, NULL, "Test", "Test", &offMemb), VERR_INVALID_HANDLE);
            RTR0TESTR0_CHECK_RC_BREAK(RTR0DbgKrnlInfoQueryMember(hKrnlInfo, NULL, NULL, "Test", &offMemb), VERR_INVALID_PARAMETER);
            RTR0TESTR0_CHECK_RC_BREAK(RTR0DbgKrnlInfoQueryMember(hKrnlInfo, NULL, "Test", NULL, &offMemb), VERR_INVALID_PARAMETER);
            RTR0TESTR0_CHECK_RC_BREAK(RTR0DbgKrnlInfoQueryMember(hKrnlInfo, NULL, "Test", "Test", NULL), VERR_INVALID_PARAMETER);

            void *pvSymbol;
            RTR0TESTR0_CHECK_RC_BREAK(RTR0DbgKrnlInfoQuerySymbol(NULL, "Test", "Test", &pvSymbol), VERR_INVALID_HANDLE);
            RTR0TESTR0_CHECK_RC_BREAK(RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, "TestModule", "Test", &pvSymbol), VERR_MODULE_NOT_FOUND);
            RTR0TESTR0_CHECK_RC_BREAK(RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, NULL, NULL, &pvSymbol), VERR_INVALID_PARAMETER);

            RTR0DbgKrnlInfoRelease(hKrnlInfo);
            hKrnlInfo = NIL_RTDBGKRNLINFO;
            uint32_t cRefs;
            RTR0TESTR0_CHECK_MSG((cRefs = RTR0DbgKrnlInfoRelease(NIL_RTDBGKRNLINFO)) == 0, ("cRefs=%#x", cRefs));
            break;
        }

        /** @todo check member retreival based on target platform. */
        /** @todo check symbol lookups based on target platform. */

        default:
            RTStrPrintf(pszErr, cchErr, "!Unknown test #%d", uOperation);
            break;
    }
    if (hKrnlInfo != NIL_RTDBGKRNLINFO)
        RTR0DbgKrnlInfoRelease(hKrnlInfo);
    RTAssertSetMayPanic(fSavedMayPanic);

    /* The error indicator is the '!' in the message buffer. */
    return VINF_SUCCESS;
}

