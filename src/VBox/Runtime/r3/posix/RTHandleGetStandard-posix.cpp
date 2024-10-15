/* $Id: RTHandleGetStandard-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTHandleGetStandard, POSIX.
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
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#ifdef _MSC_VER
# include <io.h>
#else
# include <unistd.h>
#endif

#include "internal/iprt.h"
#include <iprt/handle.h>

#include <iprt/file.h>
#include <iprt/pipe.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/log.h>

#include "internal/socket.h"



RTDECL(int) RTHandleGetStandard(RTHANDLESTD enmStdHandle, bool fLeaveOpen, PRTHANDLE ph)
{
    /*
     * Validate and convert input.
     */
    AssertPtrReturn(ph, VERR_INVALID_POINTER);
    int fd;
    switch (enmStdHandle)
    {
        case RTHANDLESTD_INPUT:  fd = 0; break;
        case RTHANDLESTD_OUTPUT: fd = 1; break;
        case RTHANDLESTD_ERROR:  fd = 2; break;
        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }

    /*
     * Is the requested descriptor valid and which IPRT handle type does it
     * best map on to?
     */
    struct stat st;
    int rc = fstat(fd, &st);
    if (rc == -1)
        return RTErrConvertFromErrno(errno);

    rc = fcntl(fd, F_GETFD, 0);
    if (rc == -1)
        return RTErrConvertFromErrno(errno);
    bool const fInherit = !(rc & FD_CLOEXEC);

    RTHANDLE h;
    if (S_ISREG(st.st_mode))
        h.enmType = RTHANDLETYPE_FILE;
    else if (   S_ISFIFO(st.st_mode)
             || (st.st_mode == 0 && st.st_nlink == 0 /*see bugs on bsd manpage*/))
        h.enmType = RTHANDLETYPE_PIPE;
    else if (S_ISSOCK(st.st_mode))
    {
        /** @todo check if it's really a socket... IIRC some OSes reports
         *        anonymouse pips as sockets. */
        h.enmType = RTHANDLETYPE_SOCKET;
    }
#if 0 /** @todo re-enable this when the VFS pipe has been coded up. */
    else if (isatty(fd))
        h.enmType = RTHANDLETYPE_PIPE;
#endif
    else
        h.enmType = RTHANDLETYPE_FILE;

    /*
     * Create the IPRT handle.
     */
    switch (h.enmType)
    {
        case RTHANDLETYPE_FILE:
            /** @todo fLeaveOpen   */
            rc = RTFileFromNative(&h.u.hFile, fd);
            break;

        case RTHANDLETYPE_PIPE:
            rc = RTPipeFromNative(&h.u.hPipe, fd,
                                    (enmStdHandle == RTHANDLESTD_INPUT ? RTPIPE_N_READ : RTPIPE_N_WRITE)
                                  | (fInherit ? RTPIPE_N_INHERIT : 0)
                                  | (fLeaveOpen ? RTPIPE_N_LEAVE_OPEN : 0));
            break;

        case RTHANDLETYPE_SOCKET:
            rc = rtSocketCreateForNative(&h.u.hSocket, fd, fLeaveOpen);
            break;

        default: /* shut up gcc */
            return VERR_INTERNAL_ERROR;
    }

    if (RT_SUCCESS(rc))
        *ph = h;

    return rc;
}

