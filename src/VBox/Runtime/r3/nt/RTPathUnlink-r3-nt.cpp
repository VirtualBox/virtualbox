/* $Id: RTPathUnlink-r3-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTFileDelete, Native NT.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#include "internal-r3-nt.h"

#include <iprt/path.h>
#include <iprt/err.h>

#include "internal/fs.h"


RTR3DECL(int) RTPathUnlink(const char *pszPath, uint32_t fUnlink)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_INVALID_NAME);
    AssertReturn(!(fUnlink & ~RTPATHUNLINK_FLAGS_NO_SYMLINKS), VERR_INVALID_FLAGS);

    /*
     * Convert and normalize the path.
     */
    UNICODE_STRING NtName;
    HANDLE         hRootDir;
    int rc = RTNtPathFromWinUtf8(&NtName, &hRootDir, pszPath);
    if (RT_SUCCESS(rc))
    {
        /*
         * Try open the file system object without preference to directory or
         * non-directory, while making sure we don't follow reparse points.
         */
        HANDLE              hPath   = RTNT_INVALID_HANDLE_VALUE;
        IO_STATUS_BLOCK     Ios     = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        OBJECT_ATTRIBUTES   ObjAttr;
        InitializeObjectAttributes(&ObjAttr, &NtName, 0 /*fAttrib*/, hRootDir, NULL);

        ULONG fOpenOptions = FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT;
        NTSTATUS rcNt = NtCreateFile(&hPath,
                                     DELETE | SYNCHRONIZE,
                                     &ObjAttr,
                                     &Ios,
                                     NULL /*AllocationSize*/,
                                     FILE_ATTRIBUTE_NORMAL,
                                     FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     FILE_OPEN,
                                     fOpenOptions,
                                     NULL /*EaBuffer*/,
                                     0 /*EaLength*/);

        /*
         * Retry w/o the FILE_OPEN_REPARSE_POINT if the file system returns
         * STATUS_INVALID_PARAMETER as it could be an old one that doesn't
         * grok the reparse point stuff.
         */
        if (rcNt == STATUS_INVALID_PARAMETER)
        {
            fOpenOptions &= ~FILE_OPEN_REPARSE_POINT;
            hPath = RTNT_INVALID_HANDLE_VALUE;
            RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
            rcNt = NtCreateFile(&hPath,
                                DELETE | SYNCHRONIZE,
                                &ObjAttr,
                                &Ios,
                                NULL /*AllocationSize*/,
                                FILE_ATTRIBUTE_NORMAL,
                                FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                                FILE_OPEN,
                                fOpenOptions,
                                NULL /*EaBuffer*/,
                                0 /*EaLength*/);
        }
        if (NT_SUCCESS(rcNt))
        {
            /*
             * Delete it.
             */
            FILE_DISPOSITION_INFORMATION DeleteInfo = { TRUE };
            rcNt = NtSetInformationFile(hPath, &Ios, &DeleteInfo, sizeof(DeleteInfo), FileDispositionInformation);
            if (NT_SUCCESS(rcNt))
                rc = VINF_SUCCESS;
            else
                rc = RTErrConvertFromNtStatus(rcNt);

            rcNt = NtClose(hPath);
            if (!NT_SUCCESS(rcNt) && RT_SUCCESS_NP(rc))
                rc = RTErrConvertFromNtStatus(rcNt);
        }
        else
            rc = RTErrConvertFromNtStatus(rcNt);
        RTNtPathFree(&NtName, &hRootDir);
    }
    return rc;
}

