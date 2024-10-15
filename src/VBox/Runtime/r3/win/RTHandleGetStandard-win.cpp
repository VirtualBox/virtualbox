/* $Id: RTHandleGetStandard-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTHandleGetStandard, Windows.
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
#include <iprt/handle.h>

#include <iprt/file.h>
#include <iprt/pipe.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/log.h>

#include <iprt/win/windows.h>

#include "internal/socket.h"            /* (Needs Windows.h.) */
#include "internal-r3-win.h"            /* (Needs Windows.h.) */


RTDECL(int) RTHandleGetStandard(RTHANDLESTD enmStdHandle, bool fLeaveOpen, PRTHANDLE ph)
{
    /*
     * Validate and convert input.
     */
    AssertPtrReturn(ph, VERR_INVALID_POINTER);
    DWORD dwStdHandle;
    switch (enmStdHandle)
    {
        case RTHANDLESTD_INPUT:  dwStdHandle = STD_INPUT_HANDLE; break;
        case RTHANDLESTD_OUTPUT: dwStdHandle = STD_OUTPUT_HANDLE; break;
        case RTHANDLESTD_ERROR:  dwStdHandle = STD_ERROR_HANDLE; break;
        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }

    /*
     * Is the requested descriptor valid and which IPRT handle type does it
     * best map on to?
     */
    HANDLE hNative = GetStdHandle(dwStdHandle);
    if (hNative == INVALID_HANDLE_VALUE)
        return RTErrConvertFromWin32(GetLastError());

    DWORD dwInfo = 0;
    if (g_pfnGetHandleInformation && !g_pfnGetHandleInformation(hNative, &dwInfo))
        return RTErrConvertFromWin32(GetLastError());
    bool const fInherit = RT_BOOL(dwInfo & HANDLE_FLAG_INHERIT);

    SetLastError(NO_ERROR);
    RTHANDLE h;
    DWORD    dwType = GetFileType(hNative);
    switch (dwType & ~FILE_TYPE_REMOTE)
    {
        case FILE_TYPE_UNKNOWN:
            if (GetLastError() != NO_ERROR)
                return RTErrConvertFromWin32(GetLastError());
            RT_FALL_THROUGH();
        default:
        case FILE_TYPE_CHAR:
        case FILE_TYPE_DISK:
            h.enmType = RTHANDLETYPE_FILE;
            break;

        case FILE_TYPE_PIPE:
        {
            DWORD cMaxInstances;
            DWORD fInfo;
            if (!GetNamedPipeInfo(hNative, &fInfo, NULL, NULL, &cMaxInstances))
                h.enmType = RTHANDLETYPE_SOCKET;
            else
                h.enmType = RTHANDLETYPE_PIPE;
            break;
        }
    }

    /*
     * Create the IPRT handle.
     */
    int rc;
    switch (h.enmType)
    {
        case RTHANDLETYPE_FILE:
            /** @todo fLeaveOpen   */
            rc = RTFileFromNative(&h.u.hFile, (RTHCUINTPTR)hNative);
            break;

        case RTHANDLETYPE_PIPE:
            rc = RTPipeFromNative(&h.u.hPipe, (RTHCUINTPTR)hNative,
                                    (enmStdHandle == RTHANDLESTD_INPUT ? RTPIPE_N_READ : RTPIPE_N_WRITE)
                                  | (fInherit ? RTPIPE_N_INHERIT : 0)
                                  | (fLeaveOpen ? RTPIPE_N_LEAVE_OPEN : 0));
            break;

        case RTHANDLETYPE_SOCKET:
            rc = rtSocketCreateForNative(&h.u.hSocket, (RTHCUINTPTR)hNative, fLeaveOpen);
            break;

        default: /* shut up gcc */
            return VERR_INTERNAL_ERROR;
    }

    if (RT_SUCCESS(rc))
        *ph = h;

    return rc;
}

