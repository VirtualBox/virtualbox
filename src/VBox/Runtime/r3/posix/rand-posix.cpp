/* $Id: rand-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Random Numbers and Byte Streams, POSIX.
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
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#ifdef _MSC_VER
# include <io.h>
# include <stdio.h>
#else
# include <unistd.h>
# include <sys/time.h>
#endif

#include <iprt/rand.h>
#include <iprt/mem.h>
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include "internal/rand.h"
#include "internal/magics.h"



/** @copydoc RTRANDINT::pfnGetBytes */
static DECLCALLBACK(void) rtRandAdvPosixGetBytes(PRTRANDINT pThis, uint8_t *pb, size_t cb)
{
    ssize_t cbRead = read(pThis->u.File.hFile, pb, cb);
    if ((size_t)cbRead != cb)
    {
        /* S10 has been observed returning 1040 bytes at the time from /dev/urandom.
           Which means we need to do than 256 rounds to reach 668171 bytes if
           that's what demanded by the caller (like tstRTMemWipe.cpp). */
        ssize_t cTries = RT_MAX(256, cb / 64);
        do
        {
            if (cbRead > 0)
            {
                cb -= cbRead;
                pb += cbRead;
            }
            cbRead = read(pThis->u.File.hFile, pb, cb);
        } while (   (size_t)cbRead != cb
                 && cTries-- > 0);
        AssertReleaseMsg((size_t)cbRead == cb, ("%zu != %zu, cTries=%zd errno=%d\n", cbRead, cb, cTries, errno));
    }
}


/** @copydoc RTRANDINT::pfnDestroy */
static DECLCALLBACK(int) rtRandAdvPosixDestroy(PRTRANDINT pThis)
{
    pThis->u32Magic = ~RTRANDINT_MAGIC;
    int fd = pThis->u.File.hFile;
    pThis->u.File.hFile = -1;
    RTMemFree(pThis);
    close(fd);
    return VINF_SUCCESS;
}


static int rtRandAdvPosixCreateSystem(PRTRAND phRand, const char *pszDev) RT_NO_THROW_DEF
{
    /*
     * Try open it first and then setup the handle structure.
     */
    int fd = open(pszDev, O_RDONLY);
    if (fd < 0)
        return RTErrConvertFromErrno(errno);
    int rc;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) != -1)
    {
        PRTRANDINT pThis = (PRTRANDINT)RTMemAlloc(sizeof(*pThis));
        if (pThis)
        {
            pThis->u32Magic     = RTRANDINT_MAGIC;
            pThis->pfnGetBytes  = rtRandAdvPosixGetBytes;
            pThis->pfnGetU32    = rtRandAdvSynthesizeU32FromBytes;
            pThis->pfnGetU64    = rtRandAdvSynthesizeU64FromBytes;
            pThis->pfnSeed      = rtRandAdvStubSeed;
            pThis->pfnSaveState = rtRandAdvStubSaveState;
            pThis->pfnRestoreState = rtRandAdvStubRestoreState;
            pThis->pfnDestroy   = rtRandAdvPosixDestroy;
            pThis->u.File.hFile = fd;

            *phRand = pThis;
            return VINF_SUCCESS;
        }

        /* bail out */
        rc = VERR_NO_MEMORY;
    }
    else
        rc = RTErrConvertFromErrno(errno);
    close(fd);
    return rc;
}


RTDECL(int) RTRandAdvCreateSystemFaster(PRTRAND phRand) RT_NO_THROW_DEF
{
    return rtRandAdvPosixCreateSystem(phRand, "/dev/urandom");
}


RTDECL(int) RTRandAdvCreateSystemTruer(PRTRAND phRand) RT_NO_THROW_DEF
{
    return rtRandAdvPosixCreateSystem(phRand, "/dev/random");
}

