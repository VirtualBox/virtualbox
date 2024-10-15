/* $Id: filelock-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File Locking, OS/2.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <sys/fcntl.h>
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




RTR3DECL(int)  RTFileLock(RTFILE File, unsigned fLock, int64_t offLock, uint64_t cbLock)
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
    if (fcntl(RTFileToNative(File), (fLock & RTFILE_LOCK_WAIT) ? F_SETLKW : F_SETLK, &fl) >= 0)
        return VINF_SUCCESS;

    int iErr = errno;
    if (    iErr == EAGAIN
        ||  iErr == EACCES)
        return VERR_FILE_LOCK_VIOLATION;

    return RTErrConvertFromErrno(iErr);
}


RTR3DECL(int)  RTFileChangeLock(RTFILE File, unsigned fLock, int64_t offLock, uint64_t cbLock)
{
    /** @todo copied from ../win/fileio-win.cpp for now but a proper solution
     * would probably be to modify kLIBC so that __fcntl_locking() first
     * assumes a change lock request is made (e.g. the same region was
     * previously F_RDLCK'ed and now needs to be F_WRLCK'ed or vice versa) and
     * tries to use atomic locking, and only if it fails, it does the regular
     * lock procedure. The alternative is to use DosSetFileLocks directly here
     * which basically means copy-pasting the __fcntl_locking() source
     * code :) Note that the first attempt to call RTFileLock() below assumes
     * that kLIBC is patched as described above one day and gives it a chance;
     * on failure, we fall back to the Win-like unlock-then-lock approach. */

    int rc = RTFileLock(File, fLock, offLock, cbLock);
    if (RT_FAILURE(rc) && rc != VERR_FILE_LOCK_VIOLATION)
        return rc;

    /* Check arguments. */
    if (fLock & ~RTFILE_LOCK_MASK)
    {
        AssertMsgFailed(("Invalid fLock=%08X\n", fLock));
        return VERR_INVALID_PARAMETER;
    }

    /* Remove old lock. */
    rc = RTFileUnlock(File, offLock, cbLock);
    if (RT_FAILURE(rc))
        return rc;

    /* Set new lock. */
    rc = RTFileLock(File, fLock, offLock, cbLock);
    if (RT_SUCCESS(rc))
        return rc;

    /* Try to restore old lock. */
    unsigned fLockOld = (fLock & RTFILE_LOCK_WRITE) ? fLock & ~RTFILE_LOCK_WRITE : fLock | RTFILE_LOCK_WRITE;
    rc = RTFileLock(File, fLockOld, offLock, cbLock);
    if (RT_SUCCESS(rc))
        return VERR_FILE_LOCK_VIOLATION;
    else
        return VERR_FILE_LOCK_LOST;
}


RTR3DECL(int)  RTFileUnlock(RTFILE File, int64_t offLock, uint64_t cbLock)
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

    if (fcntl(RTFileToNative(File), F_SETLK, &fl) >= 0)
        return VINF_SUCCESS;

    /** @todo check error codes for non existing lock. */
    int iErr = errno;
    if (    iErr == EAGAIN
        ||  iErr == EACCES)
        return VERR_FILE_LOCK_VIOLATION;

    return RTErrConvertFromErrno(iErr);
}

