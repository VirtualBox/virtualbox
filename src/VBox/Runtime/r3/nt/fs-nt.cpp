/* $Id: fs-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File System, Native NT.
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
#define LOG_GROUP RTLOGGROUP_FS
#include "internal-r3-nt.h"

#include <iprt/fs.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/param.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/assert.h>
#include "internal/fs.h"




RTR3DECL(int) RTFsQuerySizes(const char *pszFsPath, RTFOFF *pcbTotal, RTFOFF *pcbFree,
                             uint32_t *pcbBlock, uint32_t *pcbSector)
{
    AssertPtrReturn(pszFsPath, VERR_INVALID_POINTER);

    /*
     * Open the file/dir/whatever.
     */
    HANDLE hFile;
    int rc = RTNtPathOpen(pszFsPath,
                          GENERIC_READ,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          FILE_OPEN,
                          FILE_OPEN_FOR_BACKUP_INTENT,
                          OBJ_CASE_INSENSITIVE,
                          &hFile,
                          NULL);
    if (RT_SUCCESS(rc))
    {
        RTFILE hIprtFile = NIL_RTFILE;
        rc = RTFileFromNative(&hIprtFile, (RTHCINTPTR)hFile);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
            rc = RTFileQueryFsSizes(hIprtFile, pcbTotal, pcbFree, pcbBlock, pcbSector);

        RTNtPathClose(hFile);
    }
    return rc;
}


RTR3DECL(int) RTFsQuerySerial(const char *pszFsPath, uint32_t *pu32Serial)
{
    /*
     * Validate & get valid root path.
     */
    AssertPtrReturn(pszFsPath, VERR_INVALID_POINTER);
    AssertPtrReturn(pu32Serial, VERR_INVALID_POINTER);

    /*
     * Open the file/dir/whatever.
     */
    HANDLE hFile;
    int rc = RTNtPathOpen(pszFsPath,
                          GENERIC_READ,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          FILE_OPEN,
                          FILE_OPEN_FOR_BACKUP_INTENT,
                          OBJ_CASE_INSENSITIVE,
                          &hFile,
                          NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * Get the volume information.
         */
        union
        {
             FILE_FS_VOLUME_INFORMATION FsVolInfo;
             uint8_t abBuf[sizeof(FILE_FS_VOLUME_INFORMATION) + 4096];
        } u;
        IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        NTSTATUS rcNt = NtQueryVolumeInformationFile(hFile, &Ios, &u, sizeof(u), FileFsVolumeInformation);
        if (NT_SUCCESS(rcNt))
            *pu32Serial = u.FsVolInfo.VolumeSerialNumber;
        else
            rc = RTErrConvertFromNtStatus(rcNt);

        RTNtPathClose(hFile);
    }
    return rc;
}


RTR3DECL(int) RTFsQueryProperties(const char *pszFsPath, PRTFSPROPERTIES pProperties)
{
    /*
     * Validate & get valid root path.
     */
    AssertPtrReturn(pszFsPath, VERR_INVALID_POINTER);
    AssertPtrReturn(pProperties, VERR_INVALID_POINTER);

    /*
     * Open the file/dir/whatever.
     */
    HANDLE hFile;
    int rc = RTNtPathOpen(pszFsPath,
                          GENERIC_READ,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          FILE_OPEN,
                          FILE_OPEN_FOR_BACKUP_INTENT,
                          OBJ_CASE_INSENSITIVE,
                          &hFile,
                          NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * Get the volume information.
         */
        union
        {
            FILE_FS_ATTRIBUTE_INFORMATION FsAttrInfo;
            uint8_t abBuf[sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + 4096];
        } u;
        IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        NTSTATUS rcNt = NtQueryVolumeInformationFile(hFile, &Ios, &u, sizeof(u), FileFsAttributeInformation);
        if (NT_SUCCESS(rcNt))
        {
            FILE_FS_DEVICE_INFORMATION FsDevInfo;
            rcNt = NtQueryVolumeInformationFile(hFile, &Ios, &FsDevInfo, sizeof(FsDevInfo), FileFsDeviceInformation);
            if (NT_SUCCESS(rcNt))
            {
                /*
                 * Fill in the return structure.
                 */
                memset(pProperties, 0, sizeof(*pProperties));
                pProperties->cbMaxComponent   = u.FsAttrInfo.MaximumComponentNameLength;
                pProperties->fFileCompression = !!(u.FsAttrInfo.FileSystemAttributes & FILE_FILE_COMPRESSION);
                pProperties->fCompressed      = !!(u.FsAttrInfo.FileSystemAttributes & FILE_VOLUME_IS_COMPRESSED);
                pProperties->fReadOnly        = !!(u.FsAttrInfo.FileSystemAttributes & FILE_READ_ONLY_VOLUME);
                pProperties->fSupportsUnicode = !!(u.FsAttrInfo.FileSystemAttributes & FILE_UNICODE_ON_DISK);
                pProperties->fCaseSensitive   = false;    /* win32 is case preserving only */
                /** @todo r=bird: What about FILE_CASE_SENSITIVE_SEARCH ?  Is this set for NTFS
                 *        as well perchance?  If so, better mention it instead of just setting
                 *        fCaseSensitive to false. */

                /* figure the remote stuff */
                pProperties->fRemote          = RT_BOOL(FsDevInfo.Characteristics & FILE_REMOTE_DEVICE);
            }
            else
                rc = RTErrConvertFromNtStatus(rcNt);
        }
        else
            rc = RTErrConvertFromNtStatus(rcNt);

        RTNtPathClose(hFile);
    }
    return rc;
}


RTR3DECL(bool) RTFsIsCaseSensitive(const char *pszFsPath)
{
    RT_NOREF_PV(pszFsPath);
    return false;
}


int rtNtQueryFsType(HANDLE hHandle, PRTFSTYPE penmType)
{
    /*
     * Get the file system name.
     */
    union
    {
        FILE_FS_ATTRIBUTE_INFORMATION FsAttrInfo;
        uint8_t abBuf[sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + 4096];
    } u;
    IO_STATUS_BLOCK Ios = RTNT_IO_STATUS_BLOCK_INITIALIZER;
    NTSTATUS rcNt = NtQueryVolumeInformationFile(hHandle, &Ios, &u, sizeof(u), FileFsAttributeInformation);
    if (NT_SUCCESS(rcNt))
    {
#define IS_FS(a_szName) rtNtCompWideStrAndAscii(u.FsAttrInfo.FileSystemName, u.FsAttrInfo.FileSystemNameLength, RT_STR_TUPLE(a_szName))
        if (IS_FS("NTFS"))
            *penmType = RTFSTYPE_NTFS;
        else if (IS_FS("FAT"))
            *penmType = RTFSTYPE_FAT;
        else if (IS_FS("FAT32"))
            *penmType = RTFSTYPE_FAT;
        else if (IS_FS("exFAT"))
            *penmType = RTFSTYPE_EXFAT;
        else if (IS_FS("UDF"))
            *penmType = RTFSTYPE_UDF;
        else if (IS_FS("CDFS"))
            *penmType = RTFSTYPE_ISO9660;
        else if (IS_FS("HPFS"))
            *penmType = RTFSTYPE_HPFS;
        else if (IS_FS("ReFS")) /** @todo verify ReFS signature. */
            *penmType = RTFSTYPE_REFS;
        else if (IS_FS("VBoxSharedFolderFS"))
            *penmType = RTFSTYPE_VBOXSHF;
#undef IS_FS
        return VINF_SUCCESS;
    }

    *penmType = RTFSTYPE_UNKNOWN;
    return RTErrConvertFromNtStatus(rcNt);
}


RTR3DECL(int) RTFsQueryType(const char *pszFsPath, PRTFSTYPE penmType)
{
    /*
     * Validate input.
     */
    *penmType = RTFSTYPE_UNKNOWN;
    AssertPtrReturn(pszFsPath, VERR_INVALID_POINTER);
    AssertReturn(*pszFsPath, VERR_INVALID_PARAMETER);

    /*
     * Open the file/dir/whatever.
     */
    HANDLE hFile;
    int rc = RTNtPathOpen(pszFsPath,
                          GENERIC_READ,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          FILE_OPEN,
                          FILE_OPEN_FOR_BACKUP_INTENT,
                          OBJ_CASE_INSENSITIVE,
                          &hFile,
                          NULL);
    if (RT_SUCCESS(rc))
    {
        rc = rtNtQueryFsType(hFile, penmType);
        RTNtPathClose(hFile);
    }
    return rc;
}

