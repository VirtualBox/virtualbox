/* $Id: fileio-sg-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File I/O, RTFileSgRead & RTFileSgWrite, posixy.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#include <iprt/cdefs.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <limits.h>
#if defined(RT_OS_DARWIN) || defined(RT_OS_FREEBSD) || defined(RT_OS_NETBSD) || defined(RT_OS_OPENBSD)
# include <sys/syslimits.h>
#endif

#include "internal/iprt.h"
#include <iprt/file.h>

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>

#ifndef UIO_MAXIOV
# ifdef IOV_MAX
#  define UIO_MAXIOV IOV_MAX
# else
#  error "UIO_MAXIOV and IOV_MAX are undefined"
# endif
#endif


/* These assumptions simplifies things a lot here. */
AssertCompileMembersSameSizeAndOffset(struct iovec, iov_base, RTSGSEG, pvSeg);
AssertCompileMembersSameSizeAndOffset(struct iovec, iov_len,  RTSGSEG, cbSeg);


RTDECL(int)  RTFileSgRead(RTFILE hFile, PRTSGBUF pSgBuf, size_t cbToRead, size_t *pcbRead)
{
    /*
     * Make sure we set pcbRead.
     */
    if (pcbRead)
        *pcbRead = 0;

    /*
     * Special case: Zero read == nop.
     */
    if (cbToRead == 0)
        return VINF_SUCCESS;

    /*
     * We can use the segment array directly if we're at the start of the
     * current S/G segment and cbToRead matches the remainder exactly.
     */
    size_t cbTotalRead = 0;

    size_t const cbSgBufLeft = RTSgBufCalcLengthLeft(pSgBuf);
    AssertMsgReturn(cbSgBufLeft >= cbToRead, ("%#zx vs %#zx\n", cbSgBufLeft, cbToRead), VERR_INVALID_PARAMETER);

    if (cbToRead == cbSgBufLeft)
        while (RTSgBufIsAtStartOfSegment(pSgBuf))
        {
            size_t const cSegsLeft  = pSgBuf->cSegs - pSgBuf->idxSeg;
            ssize_t      cbThisRead = readv(RTFileToNative(hFile), (const struct iovec *)&pSgBuf->paSegs[pSgBuf->idxSeg],
                                            RT_MIN(cSegsLeft, UIO_MAXIOV));
            if (cbThisRead >= 0)
            {
                AssertStmt((size_t)cbThisRead <= cbToRead, cbThisRead = cbToRead);

                RTSgBufAdvance(pSgBuf, cbThisRead);
                cbTotalRead += cbThisRead;
                cbToRead    -= cbThisRead;
                if (cbToRead == 0)
                {
                    if (pcbRead)
                        *pcbRead = cbTotalRead;
                    return VINF_SUCCESS;
                }

                if (   pcbRead
                    && (   cSegsLeft <= UIO_MAXIOV
                        || cbThisRead == 0 /* typically EOF */ ))
                {
                    *pcbRead = cbTotalRead;
                    return VINF_SUCCESS;
                }
                if (cbThisRead == 0)
                    return VERR_EOF;
            }
            else if (cbTotalRead > 0 && pcbRead)
            {
                *pcbRead = cbTotalRead;
                return VINF_SUCCESS;
            }
            else
                return RTErrConvertFromErrno(errno);
        }

    /*
     * Unaligned start or not reading the whole buffer.  For reasons of
     * simplicity, we work the input segment by segment like the generic code.
     */
    int rc = VINF_SUCCESS;
    while (cbToRead > 0)
    {
        size_t cbSeg;
        void  *pvSeg = RTSgBufGetCurrentSegment(pSgBuf, cbToRead, &cbSeg);
        size_t cbThisRead = cbSeg;
        rc = RTFileRead(hFile, pvSeg, cbSeg, pcbRead ? &cbThisRead : NULL);
        if (RT_SUCCESS(rc))
        {
            RTSgBufAdvance(pSgBuf, cbThisRead);
            cbTotalRead += cbThisRead;
        }
        else
            break;
        if ((size_t)cbThisRead < cbSeg)
        {
            AssertStmt(pcbRead, rc = VERR_INTERNAL_ERROR_2);
            break;
        }

        Assert(cbSeg == cbThisRead);
        cbToRead -= cbSeg;
    }
    if (pcbRead)
        *pcbRead = cbTotalRead;
    return rc;
}


RTDECL(int)  RTFileSgWrite(RTFILE hFile, PRTSGBUF pSgBuf, size_t cbToWrite, size_t *pcbWritten)
{
    /*
     * Make sure we set pcbWritten.
     */
    if (pcbWritten)
        *pcbWritten = 0;

    /*
     * Special case: Zero write == nop.
     */
    if (cbToWrite == 0)
        return VINF_SUCCESS;

    /*
     * We can use the segment array directly if we're at the start of the
     * current S/G segment and cbToWrite matches the remainder exactly.
     */
    size_t cbTotalWritten = 0;

    size_t const cbSgBufLeft = RTSgBufCalcLengthLeft(pSgBuf);
    AssertMsgReturn(cbSgBufLeft >= cbToWrite, ("%#zx vs %#zx\n", cbSgBufLeft, cbToWrite), VERR_INVALID_PARAMETER);

    if (cbToWrite == cbSgBufLeft)
        while (RTSgBufIsAtStartOfSegment(pSgBuf))
        {
            size_t const cSegsLeft     = pSgBuf->cSegs - pSgBuf->idxSeg;
            ssize_t      cbThisWritten = writev(RTFileToNative(hFile), (const struct iovec *)&pSgBuf->paSegs[pSgBuf->idxSeg],
                                                RT_MIN(cSegsLeft, UIO_MAXIOV));
            if (cbThisWritten >= 0)
            {
                AssertStmt((size_t)cbThisWritten <= cbToWrite, cbThisWritten = cbToWrite);

                RTSgBufAdvance(pSgBuf, cbThisWritten);
                cbTotalWritten += cbThisWritten;
                cbToWrite      -= cbThisWritten;
                if (cbToWrite == 0)
                {
                    if (pcbWritten)
                        *pcbWritten = cbTotalWritten;
                    return VINF_SUCCESS;
                }

                if (   pcbWritten
                    && (   cSegsLeft <= UIO_MAXIOV
                        || cbThisWritten == 0 /* non-file, full buffer/whatever */ ))
                {
                    *pcbWritten = cbTotalWritten;
                    return VINF_SUCCESS;
                }
                if (cbThisWritten == 0)
                    return VERR_TRY_AGAIN;
            }
            else if (cbTotalWritten > 0 && pcbWritten)
            {
                *pcbWritten = cbTotalWritten;
                return VINF_SUCCESS;
            }
            else
                return RTErrConvertFromErrno(errno);
        }

    /*
     * Unaligned start or not writing the whole buffer.  For reasons of
     * simplicity, we work the input segment by segment like the generic code.
     */
    int rc = VINF_SUCCESS;
    while (cbToWrite > 0)
    {
        size_t cbSeg;
        void  *pvSeg = RTSgBufGetCurrentSegment(pSgBuf, cbToWrite, &cbSeg);
        size_t cbThisWritten = cbSeg;
        rc = RTFileWrite(hFile, pvSeg, cbSeg, pcbWritten ? &cbThisWritten : NULL);
        if (RT_SUCCESS(rc))
        {
            RTSgBufAdvance(pSgBuf, cbThisWritten);
            cbTotalWritten += cbThisWritten;
        }
        else
            break;
        if ((size_t)cbThisWritten < cbSeg)
        {
            AssertStmt(pcbWritten, rc = VERR_INTERNAL_ERROR_2);
            break;
        }

        Assert(cbSeg == cbThisWritten);
        cbToWrite -= cbSeg;
    }
    if (pcbWritten)
        *pcbWritten = cbTotalWritten;
    return rc;
}

