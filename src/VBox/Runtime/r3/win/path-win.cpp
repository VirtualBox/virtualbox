/* $Id: path-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Path manipulation.
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
#define LOG_GROUP RTLOGGROUP_PATH
#include <iprt/win/windows.h>
#include <iprt/win/shlobj.h>

#include <iprt/path.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/ldr.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/utf16.h>
#include "internal/path.h"
#include "internal/fs.h"

/* Needed for lazy loading SHGetFolderPathW in RTPathUserDocuments(). */
typedef HRESULT WINAPI FNSHGETFOLDERPATHW(HWND, int, HANDLE, DWORD, LPWSTR);
typedef FNSHGETFOLDERPATHW *PFNSHGETFOLDERPATHW;


RTDECL(int) RTPathReal(const char *pszPath, char *pszRealPath, size_t cchRealPath)
{
    /*
     * Convert to UTF-16, call Win32 APIs, convert back.
     */
    PRTUTF16 pwszPath;
    int rc = RTPathWinFromUtf8(&pwszPath, pszPath, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        LPWSTR lpFile;
        WCHAR  wsz[RTPATH_MAX];
        rc = GetFullPathNameW((LPCWSTR)pwszPath, RT_ELEMENTS(wsz), &wsz[0], &lpFile);
        if (rc > 0 && rc < RT_ELEMENTS(wsz))
        {
            /* Check that it exists. (Use RTPathAbs() to just resolve the name.) */
            DWORD dwAttr = GetFileAttributesW(wsz);
            if (dwAttr != INVALID_FILE_ATTRIBUTES)
                rc = RTUtf16ToUtf8Ex((PRTUTF16)&wsz[0], RTSTR_MAX, &pszRealPath, cchRealPath, NULL);
            else
                rc = RTErrConvertFromWin32(GetLastError());
        }
        else if (rc <= 0)
            rc = RTErrConvertFromWin32(GetLastError());
        else
            rc = VERR_FILENAME_TOO_LONG;

        RTPathWinFree(pwszPath);
    }
    return rc;
}

#if 0
RTDECL(int) RTPathAbs(const char *pszPath, char *pszAbsPath, size_t cchAbsPath)
{
    /*
     * Validation.
     */
    AssertPtr(pszAbsPath);
    AssertPtr(pszPath);
    if (RT_UNLIKELY(!*pszPath))
        return VERR_INVALID_PARAMETER;

    /*
     * Convert to UTF-16, call Win32 API, convert back.
     */
    LPWSTR pwszPath;
    int rc = RTStrToUtf16(pszPath, &pwszPath);
    if (!RT_SUCCESS(rc))
        return (rc);

    LPWSTR pwszFile; /* Ignored */
    RTUTF16 wsz[RTPATH_MAX];
    rc = GetFullPathNameW(pwszPath, RT_ELEMENTS(wsz), &wsz[0], &pwszFile);
    if (rc > 0 && rc < RT_ELEMENTS(wsz))
    {
        size_t cch;
        rc = RTUtf16ToUtf8Ex(&wsz[0], RTSTR_MAX, &pszAbsPath, cchAbsPath, &cch);
        if (RT_SUCCESS(rc))
        {
# if 1 /** @todo This code is completely bonkers. */
            /*
             * Remove trailing slash if the path may be pointing to a directory.
             * (See posix variant.)
             */
            if (    cch > 1
                &&  RTPATH_IS_SLASH(pszAbsPath[cch - 1])
                &&  !RTPATH_IS_VOLSEP(pszAbsPath[cch - 2])
                &&  !RTPATH_IS_SLASH(pszAbsPath[cch - 2]))
                pszAbsPath[cch - 1] = '\0';
# endif
        }
    }
    else if (rc <= 0)
        rc = RTErrConvertFromWin32(GetLastError());
    else
        rc = VERR_FILENAME_TOO_LONG;

    RTUtf16Free(pwszPath);
    return rc;
}
#endif


RTDECL(int) RTPathUserHome(char *pszPath, size_t cchPath)
{
    /*
     * Validate input
     */
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(cchPath, VERR_INVALID_PARAMETER);

    RTUTF16 wszPath[RTPATH_MAX];
    bool    fValidFolderPath = false;

    /*
     * Try with Windows XP+ functionality first.
     */
    RTLDRMOD hShell32;
    int rc = RTLdrLoadSystem("Shell32.dll", true /*fNoUnload*/, &hShell32);
    if (RT_SUCCESS(rc))
    {
        PFNSHGETFOLDERPATHW pfnSHGetFolderPathW;
        rc = RTLdrGetSymbol(hShell32, "SHGetFolderPathW", (void**)&pfnSHGetFolderPathW);
        if (RT_SUCCESS(rc))
        {
            HRESULT hrc = pfnSHGetFolderPathW(0, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, wszPath);
            fValidFolderPath = (hrc == S_OK);
        }
        RTLdrClose(hShell32);
    }

    DWORD   dwAttr;
    if (    !fValidFolderPath
        ||  (dwAttr = GetFileAttributesW(&wszPath[0])) == INVALID_FILE_ATTRIBUTES
        ||  !(dwAttr & FILE_ATTRIBUTE_DIRECTORY))
    {
        /*
         * Fall back to Windows specific environment variables. HOME is not used.
         */
        if (    !GetEnvironmentVariableW(L"USERPROFILE", &wszPath[0], RTPATH_MAX)
            ||  (dwAttr = GetFileAttributesW(&wszPath[0])) == INVALID_FILE_ATTRIBUTES
            ||  !(dwAttr & FILE_ATTRIBUTE_DIRECTORY))
        {
            /* %HOMEDRIVE%%HOMEPATH% */
            if (!GetEnvironmentVariableW(L"HOMEDRIVE", &wszPath[0], RTPATH_MAX))
                return VERR_PATH_NOT_FOUND;
            size_t const cwc = RTUtf16Len(&wszPath[0]);
            if (    !GetEnvironmentVariableW(L"HOMEPATH", &wszPath[cwc], RTPATH_MAX - (DWORD)cwc)
                ||  (dwAttr = GetFileAttributesW(&wszPath[0])) == INVALID_FILE_ATTRIBUTES
                ||  !(dwAttr & FILE_ATTRIBUTE_DIRECTORY))
                return VERR_PATH_NOT_FOUND;
        }
    }

    /*
     * Convert and return.
     */
    return RTUtf16ToUtf8Ex(&wszPath[0], RTSTR_MAX, &pszPath, cchPath, NULL);
}


RTDECL(int) RTPathUserDocuments(char *pszPath, size_t cchPath)
{
    /*
     * Validate input
     */
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(cchPath, VERR_INVALID_PARAMETER);

    RTLDRMOD hShell32;
    int rc = RTLdrLoadSystem("Shell32.dll", true /*fNoUnload*/, &hShell32);
    if (RT_SUCCESS(rc))
    {
        PFNSHGETFOLDERPATHW pfnSHGetFolderPathW;
        rc = RTLdrGetSymbol(hShell32, "SHGetFolderPathW", (void**)&pfnSHGetFolderPathW);
        if (RT_SUCCESS(rc))
        {
            RTUTF16 wszPath[RTPATH_MAX];
            HRESULT hrc = pfnSHGetFolderPathW(0, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, wszPath);
            if (   hrc == S_OK     /* Found */
                || hrc == S_FALSE) /* Found, but doesn't exist */
            {
                /*
                 * Convert and return.
                 */
                RTLdrClose(hShell32);
                return RTUtf16ToUtf8Ex(&wszPath[0], RTSTR_MAX, &pszPath, cchPath, NULL);
            }
        }
        RTLdrClose(hShell32);
    }
    return VERR_PATH_NOT_FOUND;
}


#if 0 /* use nt version of this */

RTR3DECL(int) RTPathQueryInfo(const char *pszPath, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs)
{
    return RTPathQueryInfoEx(pszPath, pObjInfo, enmAdditionalAttribs, RTPATH_F_ON_LINK);
}
#endif
#if 0


RTR3DECL(int) RTPathQueryInfoEx(const char *pszPath, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pObjInfo, VERR_INVALID_POINTER);
    AssertMsgReturn(    enmAdditionalAttribs >= RTFSOBJATTRADD_NOTHING
                    &&  enmAdditionalAttribs <= RTFSOBJATTRADD_LAST,
                    ("Invalid enmAdditionalAttribs=%p\n", enmAdditionalAttribs),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(RTPATH_F_IS_VALID(fFlags, 0), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);

    /*
     * Query file info.
     */
    uint32_t uReparseTag = RTFSMODE_SYMLINK_REPARSE_TAG;
    WIN32_FILE_ATTRIBUTE_DATA Data;
    PRTUTF16 pwszPath;
    int rc = RTStrToUtf16(pszPath, &pwszPath);
    if (RT_FAILURE(rc))
        return rc;
    if (!GetFileAttributesExW(pwszPath, GetFileExInfoStandard, &Data))
    {
        /* Fallback to FindFileFirst in case of sharing violation. */
        if (GetLastError() == ERROR_SHARING_VIOLATION)
        {
            WIN32_FIND_DATAW FindData;
            HANDLE hDir = FindFirstFileW(pwszPath, &FindData);
            if (hDir == INVALID_HANDLE_VALUE)
            {
                rc = RTErrConvertFromWin32(GetLastError());
                RTUtf16Free(pwszPath);
                return rc;
            }
            FindClose(hDir);

            Data.dwFileAttributes   = FindData.dwFileAttributes;
            Data.ftCreationTime     = FindData.ftCreationTime;
            Data.ftLastAccessTime   = FindData.ftLastAccessTime;
            Data.ftLastWriteTime    = FindData.ftLastWriteTime;
            Data.nFileSizeHigh      = FindData.nFileSizeHigh;
            Data.nFileSizeLow       = FindData.nFileSizeLow;
            uReparseTag             = FindData.dwReserved0;
        }
        else
        {
            rc = RTErrConvertFromWin32(GetLastError());
            RTUtf16Free(pwszPath);
            return rc;
        }
    }

    /*
     * Getting the information for the link target is a bit annoying and
     * subject to the same access violation mess as above.. :/
     */
    /** @todo we're too lazy wrt to error paths here... */
    if (   (Data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        && ((fFlags & RTPATH_F_FOLLOW_LINK) || uReparseTag != RTFSMODE_SYMLINK_REPARSE_TAG))
    {
        HANDLE hFinal = CreateFileW(pwszPath,
                                    GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    NULL,
                                    OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS,
                                    NULL);
        if (hFinal != INVALID_HANDLE_VALUE)
        {
            BY_HANDLE_FILE_INFORMATION FileData;
            if (GetFileInformationByHandle(hFinal, &FileData))
            {
                Data.dwFileAttributes   = FileData.dwFileAttributes;
                Data.ftCreationTime     = FileData.ftCreationTime;
                Data.ftLastAccessTime   = FileData.ftLastAccessTime;
                Data.ftLastWriteTime    = FileData.ftLastWriteTime;
                Data.nFileSizeHigh      = FileData.nFileSizeHigh;
                Data.nFileSizeLow       = FileData.nFileSizeLow;
                uReparseTag             = 0;
            }
            CloseHandle(hFinal);
        }
        else if (GetLastError() != ERROR_SHARING_VIOLATION)
        {
            rc = RTErrConvertFromWin32(GetLastError());
            RTUtf16Free(pwszPath);
            return rc;
        }
    }

    RTUtf16Free(pwszPath);

    /*
     * Setup the returned data.
     */
    pObjInfo->cbObject    = ((uint64_t)Data.nFileSizeHigh << 32)
                          |  (uint64_t)Data.nFileSizeLow;
    pObjInfo->cbAllocated = pObjInfo->cbObject;

    Assert(sizeof(uint64_t) == sizeof(Data.ftCreationTime));
    RTTimeSpecSetNtTime(&pObjInfo->BirthTime,         *(uint64_t *)&Data.ftCreationTime);
    RTTimeSpecSetNtTime(&pObjInfo->AccessTime,        *(uint64_t *)&Data.ftLastAccessTime);
    RTTimeSpecSetNtTime(&pObjInfo->ModificationTime,  *(uint64_t *)&Data.ftLastWriteTime);
    pObjInfo->ChangeTime  = pObjInfo->ModificationTime;

    pObjInfo->Attr.fMode  = rtFsModeFromDos((Data.dwFileAttributes << RTFS_DOS_SHIFT) & RTFS_DOS_MASK_NT,
                                            pszPath, strlen(pszPath), uReparseTag);

    /*
     * Requested attributes (we cannot provide anything actually).
     */
    switch (enmAdditionalAttribs)
    {
        case RTFSOBJATTRADD_NOTHING:
            pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_NOTHING;
            break;

        case RTFSOBJATTRADD_UNIX:
            pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_UNIX;
            pObjInfo->Attr.u.Unix.uid             = ~0U;
            pObjInfo->Attr.u.Unix.gid             = ~0U;
            pObjInfo->Attr.u.Unix.cHardlinks      = 1;
            pObjInfo->Attr.u.Unix.INodeIdDevice   = 0; /** @todo use volume serial number */
            pObjInfo->Attr.u.Unix.INodeId         = 0; /** @todo use fileid (see GetFileInformationByHandle). */
            pObjInfo->Attr.u.Unix.fFlags          = 0;
            pObjInfo->Attr.u.Unix.GenerationId    = 0;
            pObjInfo->Attr.u.Unix.Device          = 0;
            break;

        case RTFSOBJATTRADD_UNIX_OWNER:
            pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_UNIX_OWNER;
            pObjInfo->Attr.u.UnixOwner.uid        = ~0U;
            pObjInfo->Attr.u.UnixOwner.szName[0]  = '\0'; /** @todo return something sensible here. */
            break;

        case RTFSOBJATTRADD_UNIX_GROUP:
            pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_UNIX_GROUP;
            pObjInfo->Attr.u.UnixGroup.gid        = ~0U;
            pObjInfo->Attr.u.UnixGroup.szName[0]  = '\0';
            break;

        case RTFSOBJATTRADD_EASIZE:
            pObjInfo->Attr.enmAdditional          = RTFSOBJATTRADD_EASIZE;
            pObjInfo->Attr.u.EASize.cb            = 0;
            break;

        default:
            AssertMsgFailed(("Impossible!\n"));
            return VERR_INTERNAL_ERROR;
    }

    return VINF_SUCCESS;
}

#endif /* using NT version*/


RTR3DECL(int) RTPathSetTimes(const char *pszPath, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                             PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    return RTPathSetTimesEx(pszPath, pAccessTime, pModificationTime, pChangeTime, pBirthTime, RTPATH_F_ON_LINK);
}


RTR3DECL(int) RTPathSetTimesEx(const char *pszPath, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                               PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pAccessTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pModificationTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pChangeTime, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pBirthTime, VERR_INVALID_POINTER);
    AssertMsgReturn(RTPATH_F_IS_VALID(fFlags, 0), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);

    /*
     * Convert the path.
     */
    PRTUTF16 pwszPath;
    int rc = RTPathWinFromUtf8(&pwszPath, pszPath, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        HANDLE hFile;
        if (fFlags & RTPATH_F_FOLLOW_LINK)
            hFile = CreateFileW(pwszPath,
                                FILE_WRITE_ATTRIBUTES,   /* dwDesiredAccess */
                                FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE, /* dwShareMode */
                                NULL,                    /* security attribs */
                                OPEN_EXISTING,           /* dwCreationDisposition */
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_ATTRIBUTE_NORMAL,
                                NULL);
        else
        {
/** @todo Symlink: Test RTPathSetTimesEx on Windows. (The code is disabled
 *        because it's not tested yet.) */
#if 0 //def FILE_FLAG_OPEN_REPARSE_POINT
            hFile = CreateFileW(pwszPath,
                                FILE_WRITE_ATTRIBUTES,   /* dwDesiredAccess */
                                FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE, /* dwShareMode */
                                NULL,                    /* security attribs */
                                OPEN_EXISTING,           /* dwCreationDisposition */
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                                NULL);

            if (hFile == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_PARAMETER)
#endif
                hFile = CreateFileW(pwszPath,
                                    FILE_WRITE_ATTRIBUTES,   /* dwDesiredAccess */
                                    FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE, /* dwShareMode */
                                    NULL,                    /* security attribs */
                                    OPEN_EXISTING,           /* dwCreationDisposition */
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_ATTRIBUTE_NORMAL,
                                    NULL);
        }
        if (hFile != INVALID_HANDLE_VALUE)
        {
            /*
             * Check if it's a no-op.
             */
            if (!pAccessTime && !pModificationTime && !pBirthTime)
                rc = VINF_SUCCESS;    /* NOP */
            else
            {
                /*
                 * Convert the input and call the API.
                 */
                FILETIME    CreationTimeFT;
                PFILETIME   pCreationTimeFT = NULL;
                if (pBirthTime)
                    pCreationTimeFT = RTTimeSpecGetNtFileTime(pBirthTime, &CreationTimeFT);

                FILETIME    LastAccessTimeFT;
                PFILETIME   pLastAccessTimeFT = NULL;
                if (pAccessTime)
                    pLastAccessTimeFT = RTTimeSpecGetNtFileTime(pAccessTime, &LastAccessTimeFT);

                FILETIME    LastWriteTimeFT;
                PFILETIME   pLastWriteTimeFT = NULL;
                if (pModificationTime)
                    pLastWriteTimeFT = RTTimeSpecGetNtFileTime(pModificationTime, &LastWriteTimeFT);

                if (SetFileTime(hFile, pCreationTimeFT, pLastAccessTimeFT, pLastWriteTimeFT))
                    rc = VINF_SUCCESS;
                else
                {
                    DWORD Err = GetLastError();
                    rc = RTErrConvertFromWin32(Err);
                    Log(("RTPathSetTimes('%s', %p, %p, %p, %p): SetFileTime failed with lasterr %d (%Rrc)\n",
                         pszPath, pAccessTime, pModificationTime, pChangeTime, pBirthTime, Err, rc));
                }
            }
            BOOL fRc = CloseHandle(hFile); Assert(fRc); NOREF(fRc);
        }
        else
        {
            DWORD Err = GetLastError();
            rc = RTErrConvertFromWin32(Err);
            Log(("RTPathSetTimes('%s',,,,): failed with %Rrc and lasterr=%u\n", pszPath, rc, Err));
        }

        RTPathWinFree(pwszPath);
    }

    LogFlow(("RTPathSetTimes(%p:{%s}, %p:{%RDtimespec}, %p:{%RDtimespec}, %p:{%RDtimespec}, %p:{%RDtimespec}): return %Rrc\n",
             pszPath, pszPath, pAccessTime, pAccessTime, pModificationTime, pModificationTime,
             pChangeTime, pChangeTime, pBirthTime, pBirthTime));
    return rc;
}




/**
 * Internal worker for RTFileRename and RTFileMove.
 *
 * @returns iprt status code.
 * @param   pszSrc      The source filename.
 * @param   pszDst      The destination filename.
 * @param   fFlags      The windows MoveFileEx flags.
 * @param   fFileType   The filetype. We use the RTFMODE filetypes here. If it's 0,
 *                      anything goes. If it's RTFS_TYPE_DIRECTORY we'll check that the
 *                      source is a directory. If Its RTFS_TYPE_FILE we'll check that it's
 *                      not a directory (we are NOT checking whether it's a file).
 */
DECLHIDDEN(int) rtPathWin32MoveRename(const char *pszSrc, const char *pszDst, uint32_t fFlags, RTFMODE fFileType)
{
    /*
     * Convert the strings.
     */
    PRTUTF16 pwszSrc;
    int rc = RTPathWinFromUtf8(&pwszSrc, pszSrc, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        PRTUTF16 pwszDst;
        rc = RTPathWinFromUtf8(&pwszDst, pszDst, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            /*
             * Check object type if requested.
             * This is open to race conditions.
             */
            if (fFileType)
            {
                DWORD dwAttr = GetFileAttributesW(pwszSrc);
                if (dwAttr == INVALID_FILE_ATTRIBUTES)
                    rc = RTErrConvertFromWin32(GetLastError());
                else if (RTFS_IS_DIRECTORY(fFileType))
                    rc = dwAttr & FILE_ATTRIBUTE_DIRECTORY ? VINF_SUCCESS : VERR_NOT_A_DIRECTORY;
                else
                    rc = dwAttr & FILE_ATTRIBUTE_DIRECTORY ? VERR_IS_A_DIRECTORY : VINF_SUCCESS;
            }
            if (RT_SUCCESS(rc))
            {
                if (MoveFileExW(pwszSrc, pwszDst, fFlags))
                    rc = VINF_SUCCESS;
                else
                {
                    DWORD Err = GetLastError();
                    rc = RTErrConvertFromWin32(Err);
                    Log(("MoveFileExW('%s', '%s', %#x, %RTfmode): fails with rc=%Rrc & lasterr=%d\n",
                         pszSrc, pszDst, fFlags, fFileType, rc, Err));
                }
            }
            RTPathWinFree(pwszDst);
        }
        RTPathWinFree(pwszSrc);
    }
    return rc;
}


RTR3DECL(int) RTPathRename(const char *pszSrc, const char *pszDst, unsigned fRename)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszSrc, VERR_INVALID_POINTER);
    AssertPtrReturn(pszDst, VERR_INVALID_POINTER);
    AssertMsgReturn(*pszSrc, ("%p\n", pszSrc), VERR_INVALID_PARAMETER);
    AssertMsgReturn(*pszDst, ("%p\n", pszDst), VERR_INVALID_PARAMETER);
    AssertMsgReturn(!(fRename & ~RTPATHRENAME_FLAGS_REPLACE), ("%#x\n", fRename), VERR_INVALID_PARAMETER);

    /*
     * Call the worker.
     */
    int rc = rtPathWin32MoveRename(pszSrc, pszDst, fRename & RTPATHRENAME_FLAGS_REPLACE ? MOVEFILE_REPLACE_EXISTING : 0, 0);

    LogFlow(("RTPathRename(%p:{%s}, %p:{%s}, %#x): returns %Rrc\n", pszSrc, pszSrc, pszDst, pszDst, fRename, rc));
    return rc;
}


#if 0 /* RTPathUnlink-r3-nt.cpp */
RTR3DECL(int) RTPathUnlink(const char *pszPath, uint32_t fUnlink)
{
    RT_NOREF_PV(pszPath); RT_NOREF_PV(fUnlink);
    return VERR_NOT_IMPLEMENTED;
}
#endif


RTDECL(bool) RTPathExists(const char *pszPath)
{
    return RTPathExistsEx(pszPath, RTPATH_F_FOLLOW_LINK);
}


RTDECL(bool) RTPathExistsEx(const char *pszPath, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszPath, false);
    AssertReturn(*pszPath, false);
    Assert(RTPATH_F_IS_VALID(fFlags, 0));

    /*
     * Try query file info.
     */
    DWORD dwAttr;
    PRTUTF16 pwszPath;
    int rc = RTPathWinFromUtf8(&pwszPath, pszPath, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        dwAttr = GetFileAttributesW(pwszPath);
        RTPathWinFree(pwszPath);
    }
    else
        dwAttr = INVALID_FILE_ATTRIBUTES;
    if (dwAttr == INVALID_FILE_ATTRIBUTES)
        return false;

#ifdef FILE_ATTRIBUTE_REPARSE_POINT
    if (   (fFlags & RTPATH_F_FOLLOW_LINK)
        && (dwAttr & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        AssertFailed();
        /** @todo Symlinks: RTPathExists+RTPathExistsEx is misbehaving on symbolic
         *        links on Windows. */
    }
#endif

    return true;
}


RTDECL(int) RTPathGetCurrent(char *pszPath, size_t cchPath)
{
    int rc;

    if (cchPath > 0)
    {
        /*
         * GetCurrentDirectory may in some cases omit the drive letter, according
         * to MSDN, thus the GetFullPathName call.
         */
        RTUTF16 wszCurPath[RTPATH_MAX];
        if (GetCurrentDirectoryW(RTPATH_MAX, wszCurPath))
        {
            RTUTF16 wszFullPath[RTPATH_MAX];
            if (GetFullPathNameW(wszCurPath, RTPATH_MAX, wszFullPath, NULL))
            {
                if (   wszFullPath[1] == ':'
                    && RT_C_IS_LOWER(wszFullPath[0]))
                    wszFullPath[0] = RT_C_TO_UPPER(wszFullPath[0]);

                rc = RTUtf16ToUtf8Ex(&wszFullPath[0], RTSTR_MAX, &pszPath, cchPath, NULL);
            }
            else
                rc = RTErrConvertFromWin32(GetLastError());
        }
        else
            rc = RTErrConvertFromWin32(GetLastError());
    }
    else
        rc = VERR_BUFFER_OVERFLOW;
    return rc;
}


RTDECL(int) RTPathSetCurrent(const char *pszPath)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_INVALID_PARAMETER);

    /*
     * This interface is almost identical to the Windows API.
     */
    PRTUTF16 pwszPath;
    int rc = RTPathWinFromUtf8(&pwszPath, pszPath, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        /** @todo improve the slash stripping a bit? */
        size_t cwc = RTUtf16Len(pwszPath);
        if (    cwc >= 2
            &&  (   pwszPath[cwc - 1] == L'/'
                 || pwszPath[cwc - 1] == L'\\')
            &&  pwszPath[cwc - 2] != ':')
            pwszPath[cwc - 1] = L'\0';

        if (!SetCurrentDirectoryW(pwszPath))
            rc = RTErrConvertFromWin32(GetLastError());

        RTPathWinFree(pwszPath);
    }
    return rc;
}


RTDECL(int) RTPathGetCurrentOnDrive(char chDrive, char *pszPath, size_t cbPath)
{
    int rc;
    if (cbPath > 0)
    {
        WCHAR wszInput[4];
        wszInput[0] = chDrive;
        wszInput[1] = ':';
        wszInput[2] = '\0';
        RTUTF16 wszFullPath[RTPATH_MAX];
        if (GetFullPathNameW(wszInput, RTPATH_MAX, wszFullPath, NULL))
            rc = RTUtf16ToUtf8Ex(&wszFullPath[0], RTSTR_MAX, &pszPath, cbPath, NULL);
        else
            rc = RTErrConvertFromWin32(GetLastError());
    }
    else
        rc = VERR_BUFFER_OVERFLOW;
    return rc;
}

