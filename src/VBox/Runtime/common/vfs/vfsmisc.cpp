/* $Id: vfsmisc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Virtual File System, Misc functions with heavy dependencies.
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
#include "internal/iprt.h"
#include <iprt/vfs.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/handle.h>



RTDECL(int)         RTVfsIoStrmFromStdHandle(RTHANDLESTD enmStdHandle, uint64_t fOpen, bool fLeaveOpen,
                                             PRTVFSIOSTREAM phVfsIos)
{
    /*
     * Input validation.
     */
    AssertPtrReturn(phVfsIos, VERR_INVALID_POINTER);
    *phVfsIos = NIL_RTVFSIOSTREAM;
    AssertReturn(   enmStdHandle == RTHANDLESTD_INPUT
                 || enmStdHandle == RTHANDLESTD_OUTPUT
                 || enmStdHandle == RTHANDLESTD_ERROR,
                 VERR_INVALID_PARAMETER);
    AssertReturn(!(fOpen & ~RTFILE_O_VALID_MASK), VERR_INVALID_PARAMETER);
    if (enmStdHandle == RTHANDLESTD_INPUT)
        fOpen |= RTFILE_O_READ;
    else
        fOpen |= RTFILE_O_WRITE;

    /*
     * Open the handle and see what we get back.
     */
    RTHANDLE h;
    int rc = RTHandleGetStandard(enmStdHandle, fLeaveOpen, &h);
    if (RT_SUCCESS(rc))
    {
        switch (h.enmType)
        {
            case RTHANDLETYPE_FILE:
                rc = RTVfsIoStrmFromRTFile(h.u.hFile, fOpen, fLeaveOpen, phVfsIos);
                break;

            case RTHANDLETYPE_PIPE:
                rc = RTVfsIoStrmFromRTPipe(h.u.hPipe, fLeaveOpen, phVfsIos);
                break;

            case RTHANDLETYPE_SOCKET:
                /** @todo  */
                rc = VERR_NOT_IMPLEMENTED;
                break;

            default:
                rc = VERR_NOT_IMPLEMENTED;
                break;
        }
    }

    return rc;
}

