/* $Id: vfsiosmisc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Virtual File System, Misc I/O Stream Operations.
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
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>

#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>



RTDECL(int) RTVfsIoStrmValidateUtf8Encoding(RTVFSIOSTREAM hVfsIos, uint32_t fFlags, PRTFOFF poffError)
{
    /*
     * Validate input.
     */
    if (poffError)
    {
        AssertPtrReturn(poffError, VINF_SUCCESS);
        *poffError = 0;
    }
    AssertReturn(!(fFlags & ~RTVFS_VALIDATE_UTF8_VALID_MASK), VERR_INVALID_PARAMETER);

    /*
     * The loop.
     */
    char    achBuf[1024 + 1];
    size_t  cbUsed = 0;
    int     rc;
    for (;;)
    {
        /*
         * Fill the buffer
         */
        size_t cbRead = 0;
        rc = RTVfsIoStrmRead(hVfsIos, &achBuf[cbUsed], sizeof(achBuf) - cbUsed - 1, true /*fBlocking*/, &cbRead);
        if (RT_FAILURE(rc))
            break;
        cbUsed += cbRead;
        if (!cbUsed)
        {
            Assert(rc == VINF_EOF);
            break;
        }
        achBuf[sizeof(achBuf) - 1] = '\0';

        /*
         * Process the data in the buffer, maybe leaving the final chars till
         * the next round.
         */
        const char *pszCur = achBuf;
        size_t      offEnd = rc == VINF_EOF
                           ? cbUsed
                           : cbUsed >= 7
                           ? cbUsed - 7
                           : 0;
        size_t      off;
        while ((off = (pszCur - &achBuf[0])) < offEnd)
        {
            RTUNICP uc;
            rc = RTStrGetCpEx(&pszCur, &uc);
            if (RT_FAILURE(rc))
                break;
            if (!uc)
            {
                if (fFlags & RTVFS_VALIDATE_UTF8_NO_NULL)
                {
                    rc = VERR_INVALID_UTF8_ENCODING;
                    break;
                }
            }
            else if (uc > 0x10ffff)
            {
                if (fFlags & RTVFS_VALIDATE_UTF8_BY_RTC_3629)
                {
                    rc = VERR_INVALID_UTF8_ENCODING;
                    break;
                }
            }
        }

        if (RT_FAILURE(rc))
            break;

        if (off < cbUsed)
        {
            cbUsed -= off;
            memmove(achBuf, pszCur, cbUsed);
        }
    }

    /*
     * Set the offset on failure.
     */
    if (poffError && RT_FAILURE(rc))
    {
        /** @todo r=andy Implement this? */
    }

    return rc == VINF_EOF ? VINF_SUCCESS : rc;
}


/** Header size.  */
#define READ_ALL_HEADER_SIZE    0x20
/** The header magic. It's followed by the size (both size_t). */
#define READ_ALL_HEADER_MAGIC   UINT32_C(0x11223355)

RTDECL(int) RTVfsIoStrmReadAll(RTVFSIOSTREAM hVfsIos, void **ppvBuf, size_t *pcbBuf)
{
    /*
     * Try query the object information and in case the stream has a known
     * size we could use for guidance.
     */
    RTFSOBJINFO ObjInfo;
    int    rc = RTVfsIoStrmQueryInfo(hVfsIos, &ObjInfo, RTFSOBJATTRADD_NOTHING);
    size_t cbAllocated = RT_SUCCESS(rc) && ObjInfo.cbObject > 0 && ObjInfo.cbObject < _1G
                       ? (size_t)ObjInfo.cbObject + 1 : _16K;
    cbAllocated += READ_ALL_HEADER_SIZE;
    void *pvBuf = RTMemAlloc(cbAllocated);
    if (pvBuf)
    {
        memset(pvBuf, 0xfe, READ_ALL_HEADER_SIZE);
        size_t off = 0;
        for (;;)
        {
            /*
             * Handle buffer growing and detecting the end of it all.
             */
            size_t cbToRead = cbAllocated - off - READ_ALL_HEADER_SIZE - 1;
            if (!cbToRead)
            {
                /* The end? */
                uint8_t bIgn;
                size_t cbIgn;
                rc = RTVfsIoStrmRead(hVfsIos, &bIgn, 0, true /*fBlocking*/, &cbIgn);
                if (rc == VINF_EOF)
                    break;

                /* Grow the buffer. */
                cbAllocated -= READ_ALL_HEADER_SIZE - 1;
                cbAllocated  = RT_MAX(RT_MIN(cbAllocated, _32M), _1K);
                cbAllocated  = RT_ALIGN_Z(cbAllocated, _4K);
                cbAllocated += READ_ALL_HEADER_SIZE + 1;

                void *pvNew = RTMemRealloc(pvBuf, cbAllocated);
                AssertBreakStmt(pvNew, rc = VERR_NO_MEMORY);
                pvBuf = pvNew;

                cbToRead = cbAllocated - off - READ_ALL_HEADER_SIZE - 1;
            }
            Assert(cbToRead < cbAllocated);

            /*
             * Read.
             */
            size_t cbActual;
            rc = RTVfsIoStrmRead(hVfsIos, (uint8_t *)pvBuf + READ_ALL_HEADER_SIZE + off, cbToRead,
                                 true /*fBlocking*/, &cbActual);
            if (RT_FAILURE(rc))
                break;
            Assert(cbActual > 0 || (cbActual == 0 && rc == VINF_EOF));
            Assert(cbActual <= cbToRead);
            off += cbActual;
            if (rc == VINF_EOF)
                break;
        }
        Assert(rc != VERR_EOF);
        if (RT_SUCCESS(rc))
        {
            ((size_t *)pvBuf)[0] = READ_ALL_HEADER_MAGIC;
            ((size_t *)pvBuf)[1] = off;
            ((uint8_t *)pvBuf)[READ_ALL_HEADER_SIZE + off] = 0;

            *ppvBuf = (uint8_t *)pvBuf + READ_ALL_HEADER_SIZE;
            *pcbBuf = off;
            return VINF_SUCCESS;
        }

        RTMemFree(pvBuf);
    }
    else
        rc = VERR_NO_MEMORY;
    *ppvBuf = NULL;
    *pcbBuf = 0;
    return rc;
}


RTDECL(void) RTVfsIoStrmReadAllFree(void *pvBuf, size_t cbBuf)
{
    AssertPtrReturnVoid(pvBuf);

    /* Spool back to the start of the header. */
    pvBuf = (uint8_t *)pvBuf - READ_ALL_HEADER_SIZE;

    /* Make sure the caller isn't messing with us. Hardcoded, but works. */
    Assert(((size_t *)pvBuf)[0] == READ_ALL_HEADER_MAGIC);
    Assert(((size_t *)pvBuf)[1] == cbBuf); RT_NOREF_PV(cbBuf);

    /* Free it. */
    RTMemFree(pvBuf);
}


RTDECL(int) RTVfsFileReadAll(RTVFSFILE hVfsFile, void **ppvBuf, size_t *pcbBuf)
{
    RTVFSIOSTREAM hVfsIos = RTVfsFileToIoStream(hVfsFile);
    int rc = RTVfsIoStrmReadAll(hVfsIos, ppvBuf, pcbBuf);
    RTVfsIoStrmRelease(hVfsIos);
    return rc;
}


RTDECL(void) RTVfsFileReadAllFree(void *pvBuf, size_t cbBuf)
{
    RTVfsIoStrmReadAllFree(pvBuf, cbBuf);
}

