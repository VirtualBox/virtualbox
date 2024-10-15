/* $Id: fs-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - File System, Win32.
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
#include <iprt/win/windows.h>

#include <iprt/fs.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/param.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/assert.h>
#include "internal/fs.h"

/* from ntdef.h */
typedef LONG NTSTATUS;

/* from ntddk.h */
typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef enum _FSINFOCLASS {
    FileFsAttributeInformation = 5,
} FS_INFORMATION_CLASS, *PFS_INFORMATION_CLASS;

/* from ntifs.h */

typedef struct _FILE_FS_ATTRIBUTE_INFORMATION {
    ULONG FileSystemAttributes;
    LONG MaximumComponentNameLength;
    ULONG FileSystemNameLength;
    WCHAR FileSystemName[1];
} FILE_FS_ATTRIBUTE_INFORMATION, *PFILE_FS_ATTRIBUTE_INFORMATION;

extern "C" NTSTATUS NTAPI NtQueryVolumeInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FS_INFORMATION_CLASS);

/**
 * Checks quickly if this is an correct root specification.
 * Root specs ends with a slash of some kind.
 *
 * @returns indicator.
 * @param   pszFsPath       Path to check.
 */
static bool rtFsIsRoot(const char *pszFsPath)
{
    /*
     * UNC has exactly two slashes..
     *
     * Anything else starting with slashe(s) requires
     * expansion and will have to take the long road.
     */
    if (RTPATH_IS_SLASH(pszFsPath[0]))
    {
        if (    !RTPATH_IS_SLASH(pszFsPath[1])
            ||  RTPATH_IS_SLASH(pszFsPath[2]))
            return false;

        /* end of machine name */
        const char *pszSlash = strpbrk(pszFsPath + 2, "\\/");
        if (!pszSlash)
            return false;

        /* end of service name. */
        pszSlash = strpbrk(pszSlash + 1, "\\/");
        if (!pszSlash)
            return false;

        return pszSlash[1] == '\0';
    }

    /*
     * Ok the other alternative is driver letter.
     */
    return  pszFsPath[0] >= 'A' && pszFsPath[0] <= 'Z'
        &&  pszFsPath[1] == ':'
        &&  RTPATH_IS_SLASH(pszFsPath[2])
        && !pszFsPath[3];
}



/**
 * Finds the root of the specified volume.
 *
 * @returns iprt status code.
 * @param   pszFsPath       Path within the filesystem. Verified as one byte or more.
 * @param   ppwszFsRoot     Where to store the returned string. Free with rtFsFreeRoot(),
 */
static int rtFsGetRoot(const char *pszFsPath, PRTUTF16 *ppwszFsRoot)
{
    /*
     * Do straight forward stuff first,
     */
    if (rtFsIsRoot(pszFsPath))
        return RTStrToUtf16(pszFsPath, ppwszFsRoot);

    /*
     * Expand and add slash (if required).
     */
    char szFullPath[RTPATH_MAX];
    int rc = RTPathAbs(pszFsPath, szFullPath, sizeof(szFullPath));
    if (RT_FAILURE(rc))
        return rc;
    size_t cb = strlen(szFullPath);
    if (!RTPATH_IS_SLASH(szFullPath[cb - 1]))
    {
        AssertReturn(cb + 1 < RTPATH_MAX, VERR_FILENAME_TOO_LONG);
        szFullPath[cb] = '\\';
        szFullPath[++cb] = '\0';
    }

    /*
     * Convert the path.
     */
    rc = RTStrToUtf16(szFullPath, ppwszFsRoot);
    if (RT_FAILURE(rc))
        return rc == VERR_BUFFER_OVERFLOW ? VERR_FILENAME_TOO_LONG : rc;

    /*
     * Walk the path until our proper API is happy or there is no more path left.
     */
    PRTUTF16 pwszStart = *ppwszFsRoot;
    if (!GetVolumeInformationW(pwszStart, NULL, 0, NULL, NULL, 0, NULL, 0))
    {
        PRTUTF16 pwszEnd = pwszStart + RTUtf16Len(pwszStart);
        PRTUTF16 pwszMin = pwszStart + 2;
        do
        {
            /* Strip off the last path component. */
            while (pwszEnd-- > pwszMin)
                if (RTPATH_IS_SLASH(*pwszEnd))
                    break;
            AssertReturn(pwszEnd >= pwszMin, VERR_INTERNAL_ERROR); /* leaks, but that's irrelevant for an internal error. */
            pwszEnd[1] = '\0';
        } while (!GetVolumeInformationW(pwszStart, NULL, 0, NULL, NULL, 0, NULL, 0));
    }

    return VINF_SUCCESS;
}

/**
 * Frees string returned by rtFsGetRoot().
 */
static void rtFsFreeRoot(PRTUTF16 pwszFsRoot)
{
    RTUtf16Free(pwszFsRoot);
}


RTR3DECL(int) RTFsQuerySizes(const char *pszFsPath, RTFOFF *pcbTotal, RTFOFF *pcbFree,
                             uint32_t *pcbBlock, uint32_t *pcbSector)
{
    /*
     * Validate & get valid root path.
     */
    AssertPtrReturn(pszFsPath, VERR_INVALID_POINTER);
    AssertReturn(*pszFsPath != '\0', VERR_INVALID_PARAMETER);
    PRTUTF16 pwszFsRoot;
    int rc = rtFsGetRoot(pszFsPath, &pwszFsRoot);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Free and total.
     */
    if (pcbTotal || pcbFree)
    {
        ULARGE_INTEGER cbTotal;
        ULARGE_INTEGER cbFree;
        if (GetDiskFreeSpaceExW(pwszFsRoot, &cbFree, &cbTotal, NULL))
        {
            if (pcbTotal)
                *pcbTotal = cbTotal.QuadPart;
            if (pcbFree)
                *pcbFree  = cbFree.QuadPart;
        }
        else
        {
            DWORD Err = GetLastError();
            rc = RTErrConvertFromWin32(Err);
            Log(("RTFsQuerySizes(%s,): GetDiskFreeSpaceEx failed with lasterr %d (%Rrc)\n",
                 pszFsPath, Err, rc));
        }
    }

    /*
     * Block and sector size.
     */
    if (    RT_SUCCESS(rc)
        &&  (pcbBlock || pcbSector))
    {
        DWORD dwDummy1, dwDummy2;
        DWORD cbSector;
        DWORD cSectorsPerCluster;
        if (GetDiskFreeSpaceW(pwszFsRoot, &cSectorsPerCluster, &cbSector, &dwDummy1, &dwDummy2))
        {
            if (pcbBlock)
                *pcbBlock = cbSector * cSectorsPerCluster;
            if (pcbSector)
                *pcbSector = cbSector;
        }
        else
        {
            DWORD Err = GetLastError();
            rc = RTErrConvertFromWin32(Err);
            Log(("RTFsQuerySizes(%s,): GetDiskFreeSpace failed with lasterr %d (%Rrc)\n",
                 pszFsPath, Err, rc));
        }
    }

    rtFsFreeRoot(pwszFsRoot);
    return rc;
}


/**
 * Query the serial number of a filesystem.
 *
 * @returns iprt status code.
 * @param   pszFsPath       Path within the mounted filesystem.
 * @param   pu32Serial      Where to store the serial number.
 */
RTR3DECL(int) RTFsQuerySerial(const char *pszFsPath, uint32_t *pu32Serial)
{
    /*
     * Validate & get valid root path.
     */
    AssertPtrReturn(pszFsPath, VERR_INVALID_POINTER);
    AssertReturn(*pszFsPath != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(pu32Serial, VERR_INVALID_POINTER);
    PRTUTF16 pwszFsRoot;
    int rc = rtFsGetRoot(pszFsPath, &pwszFsRoot);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Do work.
     */
    DWORD   dwMaxName;
    DWORD   dwFlags;
    DWORD   dwSerial;
    if (GetVolumeInformationW(pwszFsRoot, NULL, 0, &dwSerial, &dwMaxName, &dwFlags, NULL, 0))
        *pu32Serial = dwSerial;
    else
    {
        DWORD Err = GetLastError();
        rc = RTErrConvertFromWin32(Err);
        Log(("RTFsQuerySizes(%s,): GetDiskFreeSpaceEx failed with lasterr %d (%Rrc)\n",
             pszFsPath, Err, rc));
    }

    rtFsFreeRoot(pwszFsRoot);
    return rc;
}


/**
 * Query the properties of a mounted filesystem.
 *
 * @returns iprt status code.
 * @param   pszFsPath       Path within the mounted filesystem.
 * @param   pProperties     Where to store the properties.
 */
RTR3DECL(int) RTFsQueryProperties(const char *pszFsPath, PRTFSPROPERTIES pProperties)
{
    /*
     * Validate & get valid root path.
     */
    AssertPtrReturn(pszFsPath, VERR_INVALID_POINTER);
    AssertReturn(*pszFsPath != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(pProperties, VERR_INVALID_POINTER);
    PRTUTF16 pwszFsRoot;
    int rc = rtFsGetRoot(pszFsPath, &pwszFsRoot);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Do work.
     */
    DWORD   dwMaxName;
    DWORD   dwFlags;
    DWORD   dwSerial;
    if (GetVolumeInformationW(pwszFsRoot, NULL, 0, &dwSerial, &dwMaxName, &dwFlags, NULL, 0))
    {
        memset(pProperties, 0, sizeof(*pProperties));
        pProperties->cbMaxComponent   = dwMaxName;
        pProperties->fFileCompression = !!(dwFlags & FILE_FILE_COMPRESSION);
        pProperties->fCompressed      = !!(dwFlags & FILE_VOLUME_IS_COMPRESSED);
        pProperties->fReadOnly        = !!(dwFlags & FILE_READ_ONLY_VOLUME);
        pProperties->fSupportsUnicode = !!(dwFlags & FILE_UNICODE_ON_DISK);
        pProperties->fCaseSensitive   = false;    /* win32 is case preserving only */
        /** @todo r=bird: What about FILE_CASE_SENSITIVE_SEARCH ?  Is this set for NTFS
         *        as well perchance?  If so, better mention it instead of just setting
         *        fCaseSensitive to false. */
        pProperties->fRemote          = false;    /* no idea yet */
    }
    else
    {
        DWORD Err = GetLastError();
        rc = RTErrConvertFromWin32(Err);
        Log(("RTFsQuerySizes(%s,): GetVolumeInformation failed with lasterr %d (%Rrc)\n",
             pszFsPath, Err, rc));
    }

    rtFsFreeRoot(pwszFsRoot);
    return rc;
}


RTR3DECL(bool) RTFsIsCaseSensitive(const char *pszFsPath)
{
    return false;
}


/**
 * Internal helper for comparing a WCHAR string with a char string.
 *
 * @returns @c true if equal, @c false if not.
 * @param   pwsz1               The first string.
 * @param   cch1                The length of the first string, in bytes.
 * @param   psz2                The second string.
 * @param   cch2                The length of the second string.
 */
static bool rtFsWinAreEqual(WCHAR const *pwsz1, size_t cch1, const char *psz2, size_t cch2)
{
    if (cch1 != cch2 * 2)
        return false;
    while (cch2-- > 0)
    {
        unsigned ch1 = *pwsz1++;
        unsigned ch2 = (unsigned char)*psz2++;
        if (ch1 != ch2)
            return false;
    }
    return true;
}


RTR3DECL(int) RTFsQueryType(const char *pszFsPath, PRTFSTYPE penmType)
{
    *penmType = RTFSTYPE_UNKNOWN;

    AssertPtrReturn(pszFsPath, VERR_INVALID_POINTER);
    AssertReturn(*pszFsPath, VERR_INVALID_PARAMETER);

    /*
     * Convert the path and try open it.
     */
    PRTUTF16 pwszFsPath;
    int rc = RTPathWinFromUtf8(&pwszFsPath, pszFsPath, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        HANDLE hFile = CreateFileW(pwszFsPath,
                                   GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   NULL,
                                   OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS,
                                   NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            /*
             * Use the NT api directly to get the file system name.
             */
            char            abBuf[8192];
            IO_STATUS_BLOCK Ios;
            NTSTATUS        rcNt = NtQueryVolumeInformationFile(hFile, &Ios,
                                                                abBuf, sizeof(abBuf),
                                                                FileFsAttributeInformation);
            if (rcNt >= 0)
            {
                PFILE_FS_ATTRIBUTE_INFORMATION pFsAttrInfo = (PFILE_FS_ATTRIBUTE_INFORMATION)abBuf;
#define IS_FS(szName) \
    rtFsWinAreEqual(pFsAttrInfo->FileSystemName, pFsAttrInfo->FileSystemNameLength, szName, sizeof(szName) - 1)
                if (IS_FS("NTFS"))
                    *penmType = RTFSTYPE_NTFS;
                else if (IS_FS("FAT"))
                    *penmType = RTFSTYPE_FAT;
                else if (IS_FS("FAT32"))
                    *penmType = RTFSTYPE_FAT;
                else if (IS_FS("EXFAT"))
                    *penmType = RTFSTYPE_EXFAT;
                else if (IS_FS("VBoxSharedFolderFS"))
                    *penmType = RTFSTYPE_VBOXSHF;
#undef IS_FS
            }
            else
                rc = RTErrConvertFromNtStatus(rcNt);
            CloseHandle(hFile);
        }
        else
            rc = RTErrConvertFromWin32(GetLastError());
        RTPathWinFree(pwszFsPath);
    }
    return rc;
}
