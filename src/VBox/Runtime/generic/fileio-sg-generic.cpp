/* $Id: fileio-sg-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File I/O, RTFileSgRead & RTFileSgWrite, generic.
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
#include "internal/iprt.h"
#include <iprt/file.h>

#include <iprt/assert.h>
#include <iprt/err.h>


RTDECL(int)  RTFileSgRead(RTFILE hFile, PRTSGBUF pSgBuf, size_t cbToRead, size_t *pcbRead)
{
    int rc = VINF_SUCCESS;
    size_t cbRead = 0;
    while (cbToRead)
    {
        size_t cbBuf = cbToRead;
        void  *pvBuf = RTSgBufGetNextSegment(pSgBuf, &cbBuf); /** @todo this is wrong as it may advance the buffer past what's actually read. */

        size_t cbThisRead = cbBuf;
        rc = RTFileRead(hFile, pvBuf, cbBuf, pcbRead ? &cbThisRead : NULL);
        if (RT_SUCCESS(rc))
            cbRead += cbThisRead;
        else
            break;
        if (cbThisRead < cbBuf)
        {
            AssertStmt(pcbRead, rc = VERR_INTERNAL_ERROR_2);
            break;
        }
        Assert(cbBuf == cbThisRead);

        cbToRead -= cbBuf;
    }

    if (pcbRead)
        *pcbRead = cbRead;

    return rc;
}


RTDECL(int)  RTFileSgWrite(RTFILE hFile, PRTSGBUF pSgBuf, size_t cbToWrite, size_t *pcbWritten)
{
    int rc = VINF_SUCCESS;
    size_t cbWritten = 0;
    while (cbToWrite)
    {
        size_t cbBuf = cbToWrite;
        void  *pvBuf = RTSgBufGetNextSegment(pSgBuf, &cbBuf); /** @todo this is wrong as it may advance the buffer past what's actually read. */

        size_t cbThisWritten = cbBuf;
        rc = RTFileWrite(hFile, pvBuf, cbBuf, pcbWritten ? &cbThisWritten : NULL);
        if (RT_SUCCESS(rc))
            cbWritten += cbThisWritten;
        else
            break;
        if (cbThisWritten < cbBuf)
        {
            AssertStmt(pcbWritten, rc = VERR_INTERNAL_ERROR_2);
            break;
        }

        Assert(cbBuf == cbThisWritten);
        cbToWrite -= cbBuf;
    }

    if (pcbWritten)
        *pcbWritten = cbWritten;

    return rc;
}

