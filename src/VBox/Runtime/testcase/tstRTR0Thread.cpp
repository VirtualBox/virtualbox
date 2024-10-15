/* $Id: tstRTR0Thread.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT R0 Testcase - Kernel thread.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
#include <iprt/errcore.h>
#include <VBox/sup.h>
#include "tstRTR0Thread.h"
#include "tstRTR0Common.h"

#define TSTRTR0THREADDATA_MAGIC             0xcececece

/**
 * State structure for tstRTR0ThreadCallback().
 */
typedef struct TSTRTR0THREADDATA
{
    /** The magic value. */
    uint32_t                uMagic;
    /** Sample counter.  */
    uint32_t volatile       cCounter;
    /** The thread handle. */
    RTTHREAD                hThread;
} TSTRTR0THREADDATA;
/** Pointer to state structure for tstRTR0ThreadCallback(). */
typedef TSTRTR0THREADDATA *PTSTRTR0THREADDATA;


static DECLCALLBACK(int) tstRTR0ThreadCallback(RTTHREAD hThread, void *pvUser)
{
    PTSTRTR0THREADDATA pData = (PTSTRTR0THREADDATA)pvUser;
    RT_NOREF1(hThread);
    if (RT_LIKELY(pData))
    {
        if (pData->uMagic == TSTRTR0THREADDATA_MAGIC)
            pData->uMagic = ~pData->uMagic;
        if (pData->cCounter == 127)
            pData->cCounter = 196;
    }
    RTThreadUserSignal(RTThreadSelf());
    return VINF_SUCCESS;
}


/**
 * Service request callback function.
 *
 * @returns VBox status code.
 * @param   pSession    The caller's session.
 * @param   u64Arg      64-bit integer argument.
 * @param   pReqHdr     The request header. Input / Output. Optional.
 */
DECLEXPORT(int) TSTRTR0ThreadSrvReqHandler(PSUPDRVSESSION pSession, uint32_t uOperation,
                                                   uint64_t u64Arg, PSUPR0SERVICEREQHDR pReqHdr)
{
    RTR0TESTR0_SRV_REQ_PROLOG_RET(pReqHdr);
    RT_NOREF2(pSession, u64Arg);

    /*
     * The big switch.
     */
    switch (uOperation)
    {
        RTR0TESTR0_IMPLEMENT_SANITY_CASES();
        RTR0TESTR0_IMPLEMENT_DEFAULT_CASE(uOperation);

        case TSTRTR0THREAD_BASIC:
        {
            TSTRTR0THREADDATA Data;
            RT_ZERO(Data);
            Data.uMagic   = TSTRTR0THREADDATA_MAGIC;
            Data.cCounter = 127;

            /* Create the kernel thread. */
            RTR0TESTR0_CHECK_RC_BREAK(RTThreadCreate(&Data.hThread, tstRTR0ThreadCallback, &Data, 0 /* cbStack */,
                                                     RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "tstRTR0Thr"), VINF_SUCCESS);

            /* Wait for thread to signal. */
            RTR0TESTR0_CHECK_RC(RTThreadUserWait(Data.hThread, 500 /* ms */), VINF_SUCCESS);

            /* Reset the semaphore. */
            RTR0TESTR0_CHECK_RC(RTThreadUserReset(Data.hThread), VINF_SUCCESS);

            /* Check if the thread has modified data as expected. */
            RTR0TESTR0_CHECK_MSG_BREAK(Data.cCounter = 196 && Data.uMagic == ~TSTRTR0THREADDATA_MAGIC,
                                       ("Thread didn't modify data as expected.\n"));
            break;
        }
    }

    RTR0TESTR0_SRV_REQ_EPILOG(pReqHdr);
    /* The error indicator is the '!' in the message buffer. */
    return VINF_SUCCESS;
}

