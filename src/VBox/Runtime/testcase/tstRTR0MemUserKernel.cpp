/* $Id: tstRTR0MemUserKernel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT R0 Testcase - Thread Preemption.
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
#include <iprt/mem.h>

#include <iprt/errcore.h>
#include <iprt/param.h>
#include <iprt/time.h>
#include <iprt/string.h>
#include <VBox/sup.h>
#include "tstRTR0MemUserKernel.h"



/**
 * Service request callback function.
 *
 * @returns VBox status code.
 * @param   pSession    The caller's session.
 * @param   u64Arg      64-bit integer argument.
 * @param   pReqHdr     The request header. Input / Output. Optional.
 */
DECLEXPORT(int) TSTRTR0MemUserKernelSrvReqHandler(PSUPDRVSESSION pSession, uint32_t uOperation,
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

    /*
     * R3Ptr is valid and good for up to a page. The page before
     * and after are both invalid. Or, it's a kernel page.
     */
    RTR3PTR R3Ptr = (RTR3PTR)u64Arg;
    if (R3Ptr != u64Arg)
        return VERR_INVALID_PARAMETER;

    /*
     * Allocate a kernel buffer.
     */
    uint8_t *pbKrnlBuf = (uint8_t *)RTMemAlloc(PAGE_SIZE * 2);
    if (!pbKrnlBuf)
    {
        RTStrPrintf(pszErr, cchErr, "!no memory for kernel buffers");
        return VINF_SUCCESS;
    }

    /*
     * The big switch.
     */
    switch (uOperation)
    {
        case TSTRTR0MEMUSERKERNEL_SANITY_OK:
            break;

        case TSTRTR0MEMUSERKERNEL_SANITY_FAILURE:
            RTStrPrintf(pszErr, cchErr, "!42failure42%1024s", "");
            break;

        case TSTRTR0MEMUSERKERNEL_BASIC:
        {
            int rc = RTR0MemUserCopyFrom(pbKrnlBuf, R3Ptr, PAGE_SIZE);
            if (rc == VINF_SUCCESS)
            {
                rc = RTR0MemUserCopyTo(R3Ptr, pbKrnlBuf, PAGE_SIZE);
                if (rc == VINF_SUCCESS)
                {
                    if (RTR0MemUserIsValidAddr(R3Ptr))
                    {
                        if (RTR0MemKernelIsValidAddr(pbKrnlBuf))
                        {
                            if (RTR0MemAreKrnlAndUsrDifferent())
                            {
                                RTStrPrintf(pszErr, cchErr, "RTR0MemAreKrnlAndUsrDifferent returns true");
                                if (!RTR0MemUserIsValidAddr((uintptr_t)pbKrnlBuf))
                                {
                                    if (!RTR0MemKernelIsValidAddr((void *)R3Ptr))
                                    {
                                        /* done */
                                    }
                                    else
                                        RTStrPrintf(pszErr, cchErr, "! #5 - RTR0MemKernelIsValidAddr -> true, expected false");
                                }
                                else
                                    RTStrPrintf(pszErr, cchErr, "! #5 - RTR0MemUserIsValidAddr -> true, expected false");
                            }
                            else
                                RTStrPrintf(pszErr, cchErr, "RTR0MemAreKrnlAndUsrDifferent returns false");
                        }
                        else
                            RTStrPrintf(pszErr, cchErr, "! #4 - RTR0MemKernelIsValidAddr -> false, expected true");
                    }
                    else
                        RTStrPrintf(pszErr, cchErr, "! #3 - RTR0MemUserIsValidAddr -> false, expected true");
                }
                else
                    RTStrPrintf(pszErr, cchErr, "! #2 - RTR0MemUserCopyTo -> %Rrc expected %Rrc", rc, VINF_SUCCESS);
            }
            else
                RTStrPrintf(pszErr, cchErr, "! #1 - RTR0MemUserCopyFrom -> %Rrc expected %Rrc", rc, VINF_SUCCESS);
            break;
        }

#define TEST_OFF_SIZE(off, size, rcExpect) \
    if (1) \
    { \
        int rc = RTR0MemUserCopyFrom(pbKrnlBuf, R3Ptr + (off), (size)); \
        if (rc != (rcExpect)) \
        { \
            RTStrPrintf(pszErr, cchErr, "!RTR0MemUserCopyFrom(, +%#x, %#x) -> %Rrc, expected %Rrc", \
                        (off), (size), rc, (rcExpect)); \
            break; \
        } \
        rc = RTR0MemUserCopyTo(R3Ptr + (off), pbKrnlBuf, (size)); \
        if (rc != (rcExpect)) \
        { \
            RTStrPrintf(pszErr, cchErr, "!RTR0MemUserCopyTo(+%#x,, %#x) -> %Rrc, expected %Rrc", \
                        (off), (size), rc, (rcExpect)); \
            break; \
        } \
    } else do {} while (0)

        case TSTRTR0MEMUSERKERNEL_GOOD:
        {
            for (unsigned off = 0; off < 16 && !*pszErr; off++)
                for (unsigned cb = 0; cb < PAGE_SIZE - 16; cb++)
                    TEST_OFF_SIZE(off, cb, VINF_SUCCESS);
            break;
        }

        case TSTRTR0MEMUSERKERNEL_BAD:
        {
            for (unsigned off = 0; off < 16 && !*pszErr; off++)
                for (unsigned cb = 0; cb < PAGE_SIZE - 16; cb++)
                    TEST_OFF_SIZE(off, cb, cb > 0 ? VERR_ACCESS_DENIED : VINF_SUCCESS);
            break;
        }

        case TSTRTR0MEMUSERKERNEL_INVALID_ADDRESS:
        {
            if (    !RTR0MemUserIsValidAddr(R3Ptr)
                &&  RTR0MemKernelIsValidAddr((void *)R3Ptr))
            {
                for (unsigned off = 0; off < 16 && !*pszErr; off++)
                    for (unsigned cb = 0; cb < PAGE_SIZE - 16; cb++)
                        TEST_OFF_SIZE(off, cb, cb > 0 ? VERR_ACCESS_DENIED : VINF_SUCCESS); /* ... */
            }
            else
                RTStrPrintf(pszErr, cchErr, "RTR0MemUserIsValidAddr returns true");
            break;
        }

        default:
            RTStrPrintf(pszErr, cchErr, "!Unknown test #%d", uOperation);
            break;
    }

    /* The error indicator is the '!' in the message buffer. */
    RTMemFree(pbKrnlBuf);
    return VINF_SUCCESS;
}

