/** $Id: VBoxGuestR3LibPidFile.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions,
 * Create a PID file.
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
#include <iprt/file.h>
#include <iprt/string.h>
#include <iprt/process.h>
#include <iprt/thread.h>
#include <iprt/err.h>
#include "VBoxGuestR3LibInternal.h"

/* A time to wait before starting the next attempt to check a pidfile. */
#define VBGL_PIDFILE_WAIT_RELAX_TIME_MS (250)

/**
 * Creates a PID File and returns the open file descriptor.
 *
 * On DOS based system, file sharing (deny write) is used for locking the PID
 * file.
 *
 * On Unix-y systems, an exclusive advisory lock is used for locking the PID
 * file since the file sharing support is usually missing there.
 *
 * This API will overwrite any existing PID Files without a lock on them, on the
 * assumption that they are stale files which an old process did not properly
 * clean up.
 *
 * @returns IPRT status code.
 * @param   pszPath  The path and filename to create the PID File under
 * @param   phFile   Where to store the file descriptor of the open (and locked
 *                   on Unix-y systems) PID File. On failure, or if another
 *                   process owns the PID File, this will be set to NIL_RTFILE.
 */
VBGLR3DECL(int) VbglR3PidFile(const char *pszPath, PRTFILE phFile)
{
    AssertPtrReturn(pszPath, VERR_INVALID_PARAMETER);
    AssertPtrReturn(phFile, VERR_INVALID_PARAMETER);
    *phFile = NIL_RTFILE;

    RTFILE hPidFile;
    int rc = RTFileOpen(&hPidFile, pszPath,
                        RTFILE_O_READWRITE | RTFILE_O_OPEN_CREATE | RTFILE_O_DENY_WRITE
                        | (0644 << RTFILE_O_CREATE_MODE_SHIFT));
    if (RT_SUCCESS(rc))
    {
#if !defined(RT_OS_WINDOWS) && !defined(RT_OS_OS2)
        /** @todo using size 0 for locking means lock all on Posix.
         * We should adopt this as our convention too, or something
         * similar. */
        rc = RTFileLock(hPidFile, RTFILE_LOCK_WRITE, 0, 0);
        if (RT_FAILURE(rc))
            RTFileClose(hPidFile);
        else
#endif
        {
            char szBuf[256];
            size_t cbPid = RTStrPrintf(szBuf, sizeof(szBuf), "%d\n",
                                       RTProcSelf());
            RTFileWrite(hPidFile, szBuf, cbPid, NULL);
            *phFile = hPidFile;
        }
    }
    return rc;
}


/**
 * Close and remove an open PID File.
 *
 * @param  pszPath  The path to the PID File,
 * @param  hFile    The handle for the file. NIL_RTFILE is ignored as usual.
 */
VBGLR3DECL(void) VbglR3ClosePidFile(const char *pszPath, RTFILE hFile)
{
    AssertPtrReturnVoid(pszPath);
    if (hFile != NIL_RTFILE)
    {
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
        RTFileWriteAt(hFile, 0, "-1", 2, NULL);
#else
        RTFileDelete(pszPath);
#endif
        RTFileClose(hFile);
    }
}


/**
 * Wait for other process to release pidfile.
 *
 * This function is a wrapper to VbglR3PidFile().
 *
 * @returns IPRT status code.
 * @param   szPidfile       Path to pidfile.
 * @param   phPidfile       Handle to pidfile.
 * @param   u64TimeoutMs    A timeout value in milliseconds to wait for
 *                          other process to release pidfile.
 */
VBGLR3DECL(int) VbglR3PidfileWait(const char *szPidfile, RTFILE *phPidfile, uint64_t u64TimeoutMs)
{
    int rc = VERR_FILE_LOCK_VIOLATION;
    uint64_t u64Start = RTTimeSystemMilliTS();

    AssertPtrReturn(szPidfile, VERR_INVALID_PARAMETER);
    AssertPtrReturn(phPidfile, VERR_INVALID_PARAMETER);

    while (   !RT_SUCCESS((rc = VbglR3PidFile(szPidfile, phPidfile)))
           && (RTTimeSystemMilliTS() - u64Start < u64TimeoutMs))
    {
        RTThreadSleep(VBGL_PIDFILE_WAIT_RELAX_TIME_MS);
    }

    return rc;
}
