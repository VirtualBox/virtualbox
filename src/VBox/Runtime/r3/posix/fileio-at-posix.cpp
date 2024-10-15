/* $Id: fileio-at-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File I/O, RTFileReadAt and RTFileWriteAt, posix.
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
#include <sys/types.h>
#include <unistd.h>

#include "internal/iprt.h"
#include <iprt/file.h>

#include <iprt/err.h>
#include <iprt/log.h>



RTDECL(int)  RTFileReadAt(RTFILE hFile, RTFOFF off, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    ssize_t cbRead = pread(RTFileToNative(hFile), pvBuf, cbToRead, off);
    if (cbRead >= 0)
    {
        if (pcbRead)
            /* caller can handle partial read. */
            *pcbRead = cbRead;
        else
        {
            /* Caller expects all to be read. */
            while ((ssize_t)cbToRead > cbRead)
            {
                ssize_t cbReadPart = pread(RTFileToNative(hFile), (char*)pvBuf + cbRead, cbToRead - cbRead, off + cbRead);
                if (cbReadPart <= 0)
                {
                    if (cbReadPart == 0)
                        return VERR_EOF;
                    return RTErrConvertFromErrno(errno);
                }
                cbRead += cbReadPart;
            }
        }
        return VINF_SUCCESS;
    }

    return RTErrConvertFromErrno(errno);
}


RTDECL(int)  RTFileWriteAt(RTFILE hFile, RTFOFF off, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    ssize_t cbWritten = pwrite(RTFileToNative(hFile), pvBuf, cbToWrite, off);
    if (cbWritten >= 0)
    {
        if (pcbWritten)
            /* caller can handle partial write. */
            *pcbWritten = cbWritten;
        else
        {
            /* Caller expects all to be write. */
            while ((ssize_t)cbToWrite > cbWritten)
            {
                ssize_t cbWrittenPart = pwrite(RTFileToNative(hFile), (const char *)pvBuf + cbWritten, cbToWrite - cbWritten,
                                               off + cbWritten);
                if (cbWrittenPart < 0)
                    return cbWrittenPart < 0 ? RTErrConvertFromErrno(errno) : VERR_TRY_AGAIN;
                cbWritten += cbWrittenPart;
            }
        }
        return VINF_SUCCESS;
    }
    return RTErrConvertFromErrno(errno);
}

