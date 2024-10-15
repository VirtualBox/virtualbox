/* $Id: filelock-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File Locking, POSIX.
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
#define LOG_GROUP RTLOGGROUP_FILE

#include <errno.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

#include <iprt/file.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include "internal/file.h"
#include "internal/fs.h"




RTR3DECL(int)  RTFileLock(RTFILE hFile, unsigned fLock, int64_t offLock, uint64_t cbLock)
{
    Assert(offLock >= 0);

    /* Check arguments. */
    if (fLock & ~RTFILE_LOCK_MASK)
    {
        AssertMsgFailed(("Invalid fLock=%08X\n", fLock));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Validate offset.
     */
    if (    sizeof(off_t) < sizeof(cbLock)
        &&  (    (offLock >> 32) != 0
             ||  (cbLock >> 32) != 0
             ||  ((offLock + cbLock) >> 32) != 0))
    {
        AssertMsgFailed(("64-bit file i/o not supported! offLock=%lld cbLock=%lld\n", offLock, cbLock));
        return VERR_NOT_SUPPORTED;
    }

    /* Prepare flock structure. */
    struct flock fl;
    Assert(RTFILE_LOCK_WRITE);
    fl.l_type   = (fLock & RTFILE_LOCK_WRITE) ? F_WRLCK : F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = (off_t)offLock;
    fl.l_len    = (off_t)cbLock;
    fl.l_pid    = 0;

    Assert(RTFILE_LOCK_WAIT);
    if (fcntl(RTFileToNative(hFile), (fLock & RTFILE_LOCK_WAIT) ? F_SETLKW : F_SETLK, &fl) >= 0)
        return VINF_SUCCESS;

    int iErr = errno;
    if (    iErr == EAGAIN
        ||  iErr == EACCES)
        return VERR_FILE_LOCK_VIOLATION;

    return RTErrConvertFromErrno(iErr);
}


RTR3DECL(int)  RTFileChangeLock(RTFILE hFile, unsigned fLock, int64_t offLock, uint64_t cbLock)
{
    /** @todo We never returns VERR_FILE_NOT_LOCKED for now. */
    return RTFileLock(hFile, fLock, offLock, cbLock);
}


RTR3DECL(int)  RTFileUnlock(RTFILE hFile, int64_t offLock, uint64_t cbLock)
{
    Assert(offLock >= 0);

    /*
     * Validate offset.
     */
    if (    sizeof(off_t) < sizeof(cbLock)
        &&  (    (offLock >> 32) != 0
             ||  (cbLock >> 32) != 0
             ||  ((offLock + cbLock) >> 32) != 0))
    {
        AssertMsgFailed(("64-bit file i/o not supported! offLock=%lld cbLock=%lld\n", offLock, cbLock));
        return VERR_NOT_SUPPORTED;
    }

    /* Prepare flock structure. */
    struct flock fl;
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = (off_t)offLock;
    fl.l_len    = (off_t)cbLock;
    fl.l_pid    = 0;

    if (fcntl(RTFileToNative(hFile), F_SETLK, &fl) >= 0)
        return VINF_SUCCESS;

    /** @todo check error codes for non existing lock. */
    int iErr = errno;
    if (    iErr == EAGAIN
        ||  iErr == EACCES)
        return VERR_FILE_LOCK_VIOLATION;

    return RTErrConvertFromErrno(iErr);
}

