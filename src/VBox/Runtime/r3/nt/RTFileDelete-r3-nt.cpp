/* $Id: RTFileDelete-r3-nt.cpp 107437 2024-12-19 14:03:57Z knut.osmundsen@oracle.com $ */
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

#include <iprt/file.h>
#include <iprt/err.h>

#include "internal/fs.h"


RTDECL(int) RTFileDelete(const char *pszFilename)
{
    /*
     * Convert and normalize the path.
     */
    UNICODE_STRING NtName;
    HANDLE         hRootDir;
    int rc = RTNtPathFromWinUtf8(&NtName, &hRootDir, pszFilename);
    if (RT_SUCCESS(rc))
    {
        ULONG fUnwantedFileAttribs = FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY;

        /*
         * Try open it as a file or reparse point.
         *
         * Note! This will succeed on directory alternate data streams, despite
         *       the FILE_NON_DIRECTORY_FILE flag.
         *
         *       OTOH, it will open the reparse point even if an ADS is
         *       specified in the path (i.e. given symlink 'foo' targeting
         *       directory 'bar\', attempts to open 'foo::$INDEX_ALLOCATION'
         *       will result in opening the 'foo' reparse point and any
         *       attempts to delete it will only delete 'foo', the 'bar'
         *       directory will be left untouched - so safe).
         */
        HANDLE              hPath   = RTNT_INVALID_HANDLE_VALUE;
        IO_STATUS_BLOCK     Ios     = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        OBJECT_ATTRIBUTES   ObjAttr;
        InitializeObjectAttributes(&ObjAttr, &NtName, OBJ_CASE_INSENSITIVE /*fAttrib*/, hRootDir, NULL);

        ULONG fOpenOptions = FILE_NON_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REPARSE_POINT
                           | FILE_SYNCHRONOUS_IO_NONALERT;
        NTSTATUS rcNt = NtCreateFile(&hPath,
                                     DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                                     &ObjAttr,
                                     &Ios,
                                     NULL /*AllocationSize*/,
                                     FILE_ATTRIBUTE_NORMAL,
                                     FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     FILE_OPEN,
                                     fOpenOptions,
                                     NULL /*EaBuffer*/,
                                     0 /*EaLength*/);
        if (NT_SUCCESS(rcNt))
        {
            /*
             * Check if it is a reparse point.
             *
             * DeleteFileW and MoveFileExW (on build 19040) will re-open reparse
             * points other than symlinks, mount points (junctions?) and named
             * pipe symlinks.  We OTOH, will just fail as that could be a
             * potentially security problem, since unknown reparse point behaviour
             * could be used for exploits if they work in a similar manner to the
             * three just mentioned.
             *
             * To delete symbolic links, use RTSymlinkDelete.
             *
             * Alternative: We could model this on linux instead and also allow
             * this function unlink symlinks, mount points and global reparse stuff,
             * but fail on any other reparse point.  This would make the APIs work
             * more or less the same across the platforms.  (Code is #if 0'ed below.)
             *
             * See @bugref{10632}.
             *
             * Note! On NT4 the value later used for FileAttributeTagInformation was
             *       called FileObjectIdInformation, but fortunately that could only
             *       be set and will fail with STATUS_INVALID_INFO_CLASS when queried.
             */
            FILE_ATTRIBUTE_TAG_INFORMATION TagInfo = {0, 0};
            RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
            rcNt = NtQueryInformationFile(hPath, &Ios, &TagInfo, sizeof(TagInfo), FileAttributeTagInformation);
            if (NT_SUCCESS(rcNt))
            {
                if (TagInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                {
#if 0
                    if (   TagInfo.ReparseTag == IO_REPARSE_TAG_SYMLINK
                        || TagInfo.ReparseTag == IO_REPARSE_TAG_MOUNT_POINT
                        || TagInfo.ReparseTag == IO_REPARSE_TAG_GLOBAL_REPARSE)
                        fUnwantedFileAttribs = 0; /* Consider all symlinks to be files, even the ones pointing at directories. */
                    else
#endif
                    {
                        NtClose(hPath);
                        hPath = RTNT_INVALID_HANDLE_VALUE;
                        rcNt  = STATUS_DIRECTORY_IS_A_REPARSE_POINT;
                        rc    = VERR_IS_A_SYMLINK;
                    }
                }
            }
            else if (   rcNt == STATUS_INVALID_PARAMETER
                     || rcNt == STATUS_NOT_IMPLEMENTED
                     || rcNt == STATUS_INVALID_INFO_CLASS /* NT4 and earlier */)
                rcNt = STATUS_SUCCESS;
            else
            {
                NtClose(hPath);
                hPath = RTNT_INVALID_HANDLE_VALUE;
            }
        }
        /*
         * Retry w/o the FILE_OPEN_REPARSE_POINT if the file system returns
         * STATUS_INVALID_PARAMETER as it could be an old one that doesn't
         * grok the reparse point stuff.
         */
        else if (rcNt == STATUS_INVALID_PARAMETER)
        {
            fOpenOptions &= ~FILE_OPEN_REPARSE_POINT;
            hPath = RTNT_INVALID_HANDLE_VALUE;
            RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
            rcNt = NtCreateFile(&hPath,
                                DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
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
        /* else:
           DeleteFileW will retry opening the file w/o FILE_READ_ATTRIBUTES here when
           the status code is STATUS_ACCESS_DENIED. We OTOH will not do that as we will
           be querying attributes to check that it is a file and not a directory. */

        if (NT_SUCCESS(rcNt))
        {
            /*
             * Recheck that this is a file and not a directory or a reparse point we
             * don't approve of.
             *
             * This prevents us from accidentally deleting a directory via the
             * ::$INDEX_ALLOCATION stream, at the cost of not being able to delete
             * any alternate data streams on directories using this API.
             *
             * See @bugref{10632}.
             */
            FILE_BASIC_INFORMATION BasicInfo = {};
            RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
            rcNt = NtQueryInformationFile(hPath, &Ios, &BasicInfo, sizeof(BasicInfo), FileBasicInformation);
            if (NT_SUCCESS(rcNt))
            {
                if (!(BasicInfo.FileAttributes & fUnwantedFileAttribs))
                {
                    /*
                     * Okay, it is a file.  Delete it.
                     */
                    FILE_DISPOSITION_INFORMATION DeleteInfo = { TRUE };
                    rcNt = NtSetInformationFile(hPath, &Ios, &DeleteInfo, sizeof(DeleteInfo), FileDispositionInformation);
                }
                else if (BasicInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    rc = VERR_IS_A_DIRECTORY;
                else
                    rc = VERR_IS_A_SYMLINK;
            }
            else
                rc = RTErrConvertFromNtStatus(rcNt);

            NTSTATUS const rcNt2 = NtClose(hPath);
            if (!NT_SUCCESS(rcNt2) && NT_SUCCESS(rcNt))
                rcNt = rcNt2;

            if (!NT_SUCCESS(rcNt) && RT_SUCCESS_NP(rc))
                rc = RTErrConvertFromNtStatus(rcNt);
        }
        else if (RT_SUCCESS_NP(rc))
            rc = RTErrConvertFromNtStatus(rcNt);
        RTNtPathFree(&NtName, &hRootDir);
    }
    return rc;
}

