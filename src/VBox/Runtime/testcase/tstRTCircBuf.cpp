/* $Id: tstRTCircBuf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Lock free circular buffers.
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
#include <iprt/circbuf.h>

#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/test.h>


/**
 * Basic API checks.
 */
static void tst1(void)
{
    void *pvBuf;
    size_t cbSize;

    char achTestPattern1[] = { 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9 };
//    char achTestPattern2[] = { 0x8, 0x9, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9 };
//    char achTestPattern3[] = { 0x5, 0x6, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7 };

    /* Create */
    RTTestISub("Creation");
    PRTCIRCBUF pBuf;
    RTTESTI_CHECK_RC_OK_RETV(RTCircBufCreate(&pBuf, 10));
    RTTESTI_CHECK(RTCircBufFree(pBuf) == 10);
    RTTESTI_CHECK(RTCircBufUsed(pBuf) == 0);

    /* Full write */
    RTTestISub("Full write");
    RTCircBufAcquireWriteBlock(pBuf, 10, &pvBuf, &cbSize);
    RTTESTI_CHECK(cbSize == 10);
    memcpy(pvBuf, achTestPattern1, 10);
    RTCircBufReleaseWriteBlock(pBuf, 10);
    RTTESTI_CHECK(RTCircBufFree(pBuf) == 0);
    RTTESTI_CHECK(RTCircBufUsed(pBuf) == 10);
//    RTTESTI_CHECK(memcmp(pBuf->pvBuf, achTestPattern1, 10) == 0); /* Check the internal state */

    /* Half read */
    RTTestISub("Half read");
    RTCircBufAcquireReadBlock(pBuf, 5, &pvBuf, &cbSize);
    RTTESTI_CHECK(cbSize == 5);
    RTTESTI_CHECK(memcmp(pvBuf, achTestPattern1, 5) == 0);
    RTCircBufReleaseReadBlock(pBuf, 5);
    RTTESTI_CHECK(RTCircBufFree(pBuf) == 5);
    RTTESTI_CHECK(RTCircBufUsed(pBuf) == 5);

    /* Sub write */
    RTTestISub("Sub write");
    RTCircBufAcquireWriteBlock(pBuf, 2, &pvBuf, &cbSize);
    RTTESTI_CHECK(cbSize == 2);
    memcpy(pvBuf, &achTestPattern1[8], 2);
    RTCircBufReleaseWriteBlock(pBuf, 2);
    RTTESTI_CHECK(RTCircBufFree(pBuf) == 3);
    RTTESTI_CHECK(RTCircBufUsed(pBuf) == 7);
//    RTTESTI_CHECK(memcmp(pBuf->pvBuf, achTestPattern2, 10) == 0); /* Check the internal state */

    /* Split tests */
    /* Split read */
    RTTestISub("Split read");
    RTCircBufAcquireReadBlock(pBuf, 7, &pvBuf, &cbSize);
    RTTESTI_CHECK(cbSize == 5);
    RTTESTI_CHECK(memcmp(pvBuf, &achTestPattern1[5], 5) == 0);
    RTCircBufReleaseReadBlock(pBuf, 5);
    RTTESTI_CHECK(RTCircBufFree(pBuf) == 8);
    RTTESTI_CHECK(RTCircBufUsed(pBuf) == 2);
    RTCircBufAcquireReadBlock(pBuf, 2, &pvBuf, &cbSize);
    RTTESTI_CHECK(cbSize == 2);
    RTTESTI_CHECK(memcmp(pvBuf, &achTestPattern1[8], 2) == 0);
    RTCircBufReleaseReadBlock(pBuf, 2);
    RTTESTI_CHECK(RTCircBufFree(pBuf) == 10);
    RTTESTI_CHECK(RTCircBufUsed(pBuf) == 0);

    /* Split write */
    RTTestISub("Split write");
    RTCircBufAcquireWriteBlock(pBuf, 10, &pvBuf, &cbSize);
    RTTESTI_CHECK(cbSize == 8);
    memcpy(pvBuf, achTestPattern1, 8);
    RTCircBufReleaseWriteBlock(pBuf, 8);
    RTTESTI_CHECK(RTCircBufFree(pBuf) == 2);
    RTTESTI_CHECK(RTCircBufUsed(pBuf) == 8);
    RTCircBufAcquireWriteBlock(pBuf, 2, &pvBuf, &cbSize);
    RTTESTI_CHECK(cbSize == 2);
    memcpy(pvBuf, &achTestPattern1[5], 2);
    RTCircBufReleaseWriteBlock(pBuf, 2);
    RTTESTI_CHECK(RTCircBufFree(pBuf) == 0);
    RTTESTI_CHECK(RTCircBufUsed(pBuf) == 10);
//    RTTESTI_CHECK(memcmp(pBuf->pvBuf, achTestPattern3, 10) == 0); /* Check the internal state */

    /* Destroy */
    RTCircBufDestroy(pBuf);
}


int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTCircBuf", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    tst1();

    return RTTestSummaryAndDestroy(hTest);
}

