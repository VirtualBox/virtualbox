/* $Id: RTFileSetMode-r3-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTFileSetMode, Native NT.
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
#include "internal-r3-nt.h"

#include <iprt/file.h>
#include <iprt/err.h>

#include "internal/fs.h"


/**
 * Common worker for RTFileSetMode, RTPathSetMode and RTDirRelPathSetMode.
 *
 * @returns IPRT status code.
 * @param   hNativeFile The NT handle to the file system object.
 * @param   fMode       Valid and normalized file mode mask to set.
 */
DECLHIDDEN(int) rtNtFileSetModeWorker(HANDLE hNativeFile, RTFMODE fMode)
{
    IO_STATUS_BLOCK         Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    FILE_BASIC_INFORMATION  BasicInfo;
    BasicInfo.CreationTime.QuadPart   = 0;
    BasicInfo.ChangeTime.QuadPart     = 0;
    BasicInfo.LastAccessTime.QuadPart = 0;
    BasicInfo.LastWriteTime.QuadPart  = 0;
    BasicInfo.FileAttributes          = (fMode & ~(  RTFS_DOS_NT_ENCRYPTED
                                                   | RTFS_DOS_NT_COMPRESSED
                                                   | RTFS_DOS_NT_REPARSE_POINT
                                                   | RTFS_DOS_NT_SPARSE_FILE
                                                   | RTFS_DOS_NT_DEVICE
                                                   | RTFS_DOS_DIRECTORY)
                                               & RTFS_DOS_MASK_NT)
                                      >> RTFS_DOS_SHIFT;
    Assert(!(BasicInfo.FileAttributes & ~0x31a7U /* FILE_ATTRIBUTE_VALID_SET_FLAGS */));
    if (!BasicInfo.FileAttributes)
        BasicInfo.FileAttributes |= FILE_ATTRIBUTE_NORMAL;

    NTSTATUS rcNt = NtSetInformationFile(hNativeFile, &Ios, &BasicInfo, sizeof(BasicInfo), FileBasicInformation);
    if (NT_SUCCESS(rcNt))
        return VINF_SUCCESS;
    return RTErrConvertFromNtStatus(rcNt);
}


RTDECL(int) RTFileSetMode(RTFILE hFile, RTFMODE fMode)
{
    HANDLE hNative = (HANDLE)RTFileToNative(hFile);
    AssertReturn(hNative != RTNT_INVALID_HANDLE_VALUE, VERR_INVALID_HANDLE);
    fMode = rtFsModeNormalize(fMode, NULL, 0, RTFS_TYPE_FILE);
    AssertReturn(rtFsModeIsValidPermissions(fMode), VERR_INVALID_FMODE);

    return rtNtFileSetModeWorker(hNative, fMode);
}
