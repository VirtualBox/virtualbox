/* $Id: dirrel-r3-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Directory relative base APIs, NT implementation
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
#define LOG_GROUP RTLOGGROUP_DIR
#include <iprt/dir.h>
#include "internal-r3-nt.h"

#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/symlink.h>
#include <iprt/utf16.h>
#include "internal/dir.h"
#include "internal/file.h"
#include "internal/fs.h"
#include "internal/path.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Getst the RTNTPATHRELATIVEASCENT value for RTNtPathRelativeFromUtf8. */
#define RTDIRREL_NT_GET_ASCENT(a_pThis) \
    ( !(pThis->fFlags & RTDIR_F_DENY_ASCENT) ? kRTNtPathRelativeAscent_Allow : kRTNtPathRelativeAscent_Fail )



/**
 * Helper that builds a full path for a directory relative path.
 *
 * @returns IPRT status code.
 * @param   pThis               The directory.
 * @param   pszPathDst          The destination buffer.
 * @param   cbPathDst           The size of the destination buffer.
 * @param   pszRelPath          The relative path.
 */
static int rtDirRelBuildFullPath(PRTDIRINTERNAL pThis, char *pszPathDst, size_t cbPathDst, const char *pszRelPath)
{
    AssertMsgReturn(!RTPathStartsWithRoot(pszRelPath), ("pszRelPath='%s'\n", pszRelPath), VERR_PATH_IS_NOT_RELATIVE);

    /*
     * Let's hope we can avoid checking for ascension.
     *
     * Note! We don't take symbolic links into account here.  That can be
     *       done later if desired.
     */
    if (   !(pThis->fFlags & RTDIR_F_DENY_ASCENT)
        || strstr(pszRelPath, "..") == NULL)
    {
        size_t const cchRelPath = strlen(pszRelPath);
        size_t const cchDirPath = pThis->cchPath;
        if (cchDirPath + cchRelPath < cbPathDst)
        {
            memcpy(pszPathDst, pThis->pszPath, cchDirPath);
            memcpy(&pszPathDst[cchDirPath], pszRelPath, cchRelPath);
            pszPathDst[cchDirPath + cchRelPath] = '\0';
            return VINF_SUCCESS;
        }
        return VERR_FILENAME_TOO_LONG;
    }

    /*
     * Calc the absolute path using the directory as a base, then check if the result
     * still starts with the full directory path.
     *
     * This ASSUMES that pThis->pszPath is an absolute path.
     */
    int rc = RTPathAbsEx(pThis->pszPath, pszRelPath, RTPATH_STR_F_STYLE_HOST, pszPathDst, &cbPathDst);
    if (RT_SUCCESS(rc))
    {
        if (RTPathStartsWith(pszPathDst, pThis->pszPath))
            return VINF_SUCCESS;
        return VERR_PATH_NOT_FOUND;
    }
    return rc;
}


/*
 *
 *
 * RTFile stuff.
 * RTFile stuff.
 * RTFile stuff.
 *
 *
 */


RTDECL(int)  RTDirRelFileOpen(RTDIR hDir, const char *pszRelFilename, uint64_t fOpen, PRTFILE phFile)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Validate and convert flags.
     */
    uint32_t    fDesiredAccess;
    uint32_t    fObjAttribs;
    uint32_t    fFileAttribs;
    uint32_t    fShareAccess;
    uint32_t    fCreateDisposition;
    uint32_t    fCreateOptions;
    int rc = rtFileNtValidateAndConvertFlags(fOpen, &fDesiredAccess, &fObjAttribs, &fFileAttribs,
                                             &fShareAccess, &fCreateDisposition, &fCreateOptions);
    if (RT_SUCCESS(rc))
    {
        /*
         * Convert and normalize the path.
         */
        UNICODE_STRING NtName;
        HANDLE hRoot = pThis->hDir;
        rc = RTNtPathRelativeFromUtf8(&NtName, &hRoot, pszRelFilename, RTDIRREL_NT_GET_ASCENT(pThis),
                                      pThis->enmInfoClass == FileMaximumInformation);
        if (RT_SUCCESS(rc))
        {
            HANDLE              hFile = RTNT_INVALID_HANDLE_VALUE;
            IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;
            OBJECT_ATTRIBUTES   ObjAttr;
            InitializeObjectAttributes(&ObjAttr, &NtName, fObjAttribs, hRoot, NULL /*pSecDesc*/);

            NTSTATUS rcNt = NtCreateFile(&hFile,
                                         fDesiredAccess,
                                         &ObjAttr,
                                         &Ios,
                                         NULL /* AllocationSize*/,
                                         fFileAttribs,
                                         fShareAccess,
                                         fCreateDisposition,
                                         fCreateOptions,
                                         NULL /*EaBuffer*/,
                                         0 /*EaLength*/);
            if (NT_SUCCESS(rcNt))
            {
                rc = RTFileFromNative(phFile, (uintptr_t)hFile);
                if (RT_FAILURE(rc))
                    NtClose(hFile);
            }
            else
                rc = RTErrConvertFromNtStatus(rcNt);
            RTNtPathFree(&NtName, NULL);
        }
    }
    return rc;
}



/*
 *
 *
 * RTDir stuff.
 * RTDir stuff.
 * RTDir stuff.
 *
 *
 */


/**
 * Helper for cooking up a path string for rtDirOpenRelativeOrHandle.
 *
 * @returns IPRT status code.
 * @param   pszDst              The destination buffer.
 * @param   cbDst               The size of the destination buffer.
 * @param   pThis               The directory this is relative to.
 * @param   pNtPath             The NT path with a possibly relative path.
 * @param   fRelative           Whether @a pNtPath is relative or not.
 * @param   pszPath             The input path.
 */
static int rtDirRelJoinPathForDirOpen(char *pszDst, size_t cbDst, PRTDIRINTERNAL pThis,
                                      PUNICODE_STRING pNtPath, bool fRelative, const char *pszPath)
{
    int rc;
    if (fRelative)
    {
        size_t cchRel = 0;
        rc = RTUtf16CalcUtf8LenEx(pNtPath->Buffer, pNtPath->Length / sizeof(RTUTF16), &cchRel);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            if (pThis->cchPath + cchRel < cbDst)
            {
                size_t cchBase = pThis->cchPath;
                memcpy(pszDst, pThis->pszPath, cchBase);
                pszDst += cchBase;
                cbDst  -= cchBase;
                rc = RTUtf16ToUtf8Ex(pNtPath->Buffer, pNtPath->Length / sizeof(RTUTF16), &pszDst, cbDst, NULL);
            }
            else
                rc = VERR_FILENAME_TOO_LONG;
        }
    }
    else
    {
        /** @todo would be better to convert pNtName to DOS/WIN path here,
         *        as it is absolute and doesn't need stuff resolved. */
        rc = RTPathJoin(pszDst, cbDst, pThis->pszPath, pszPath);
    }
    return rc;
}

RTDECL(int) RTDirRelDirOpen(RTDIR hDir, const char *pszDir, RTDIR *phDir)
{
    return RTDirRelDirOpenFiltered(hDir, pszDir, RTDIRFILTER_NONE, 0 /*fFlags*/, phDir);
}


RTDECL(int) RTDirRelDirOpenFiltered(RTDIR hDir, const char *pszDirAndFilter, RTDIRFILTER enmFilter,
                                    uint32_t fFlags, RTDIR *phDir)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Convert and normalize the path.
     */
    UNICODE_STRING NtName;
    HANDLE hRoot = pThis->hDir;
    int rc = RTNtPathRelativeFromUtf8(&NtName, &hRoot, pszDirAndFilter, RTDIRREL_NT_GET_ASCENT(pThis),
                                      pThis->enmInfoClass == FileMaximumInformation);
    if (RT_SUCCESS(rc))
    {
        char szAbsDirAndFilter[RTPATH_MAX];
        rc = rtDirRelJoinPathForDirOpen(szAbsDirAndFilter, sizeof(szAbsDirAndFilter), pThis,
                                        &NtName, hRoot != NULL, pszDirAndFilter);
        if (RT_SUCCESS(rc))
        {
            /* Drop the filter from the NT name. */
            switch (enmFilter)
            {
                case RTDIRFILTER_NONE:
                    break;
                case RTDIRFILTER_WINNT:
                case RTDIRFILTER_UNIX:
                case RTDIRFILTER_UNIX_UPCASED:
                {
                    size_t cwc = NtName.Length / sizeof(RTUTF16);
                    while (   cwc > 0
                           && NtName.Buffer[cwc - 1] != '\\')
                        cwc--;
                    NtName.Buffer[cwc] = '\0';
                    NtName.Length = (uint16_t)(cwc * sizeof(RTUTF16));
                    break;
                }
                default:
                    AssertFailedBreak();
            }

            rc = rtDirOpenRelativeOrHandle(phDir, szAbsDirAndFilter, enmFilter, fFlags, (uintptr_t)hRoot, &NtName);
        }
        RTNtPathFree(&NtName, NULL);
    }
    return rc;
}


RTDECL(int) RTDirRelDirCreate(RTDIR hDir, const char *pszRelPath, RTFMODE fMode, uint32_t fCreate, RTDIR *phSubDir)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!(fCreate & ~RTDIRCREATE_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);
    fMode = rtFsModeNormalize(fMode, pszRelPath, 0, RTFS_TYPE_DIRECTORY);
    AssertReturn(rtFsModeIsValidPermissions(fMode), VERR_INVALID_FMODE);
    AssertPtrNullReturn(phSubDir, VERR_INVALID_POINTER);

    /*
     * Convert and normalize the path.
     */
    UNICODE_STRING NtName;
    HANDLE hRoot = pThis->hDir;
    int rc = RTNtPathRelativeFromUtf8(&NtName, &hRoot, pszRelPath, RTDIRREL_NT_GET_ASCENT(pThis),
                                      pThis->enmInfoClass == FileMaximumInformation);
    if (RT_SUCCESS(rc))
    {
        HANDLE              hNewDir = RTNT_INVALID_HANDLE_VALUE;
        IO_STATUS_BLOCK     Ios     = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        OBJECT_ATTRIBUTES   ObjAttr;
        InitializeObjectAttributes(&ObjAttr, &NtName, 0 /*fAttrib*/, hRoot, NULL);

        ULONG fDirAttribs = (fCreate & RTFS_DOS_MASK_NT) >> RTFS_DOS_SHIFT;
        if (!(fCreate & RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_DONT_SET))
            fDirAttribs |= FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
        if (!fDirAttribs)
            fDirAttribs = FILE_ATTRIBUTE_NORMAL;

        NTSTATUS rcNt = NtCreateFile(&hNewDir,
                                     phSubDir
                                     ? FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | FILE_TRAVERSE | SYNCHRONIZE
                                     : SYNCHRONIZE,
                                     &ObjAttr,
                                     &Ios,
                                     NULL /*AllocationSize*/,
                                     fDirAttribs,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     FILE_CREATE,
                                     FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                     NULL /*EaBuffer*/,
                                     0 /*EaLength*/);

        /* Just in case someone takes offence at FILE_ATTRIBUTE_NOT_CONTENT_INDEXED. */
        if (   (   rcNt == STATUS_INVALID_PARAMETER
                || rcNt == STATUS_INVALID_PARAMETER_7)
            && (fDirAttribs & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED)
            && (fCreate & RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_NOT_CRITICAL) )
        {
            fDirAttribs &= ~FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
            if (!fDirAttribs)
                fDirAttribs = FILE_ATTRIBUTE_NORMAL;
            rcNt = NtCreateFile(&hNewDir,
                                phSubDir
                                ? FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | FILE_TRAVERSE | SYNCHRONIZE
                                : SYNCHRONIZE,
                                &ObjAttr,
                                &Ios,
                                NULL /*AllocationSize*/,
                                fDirAttribs,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                FILE_CREATE,
                                FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT,
                                NULL /*EaBuffer*/,
                                0 /*EaLength*/);
        }

        if (NT_SUCCESS(rcNt))
        {
            if (!phSubDir)
            {
                NtClose(hNewDir);
                rc = VINF_SUCCESS;
            }
            else
            {
                char szAbsDirAndFilter[RTPATH_MAX];
                rc = rtDirRelJoinPathForDirOpen(szAbsDirAndFilter, sizeof(szAbsDirAndFilter), pThis,
                                                &NtName, hRoot != NULL, pszRelPath);
                if (RT_SUCCESS(rc))
                    rc = rtDirOpenRelativeOrHandle(phSubDir, pszRelPath, RTDIRFILTER_NONE, 0 /*fFlags*/,
                                                   (uintptr_t)hNewDir, NULL /*pvNativeRelative*/);
                if (RT_FAILURE(rc))
                    NtClose(hNewDir);
            }
        }
        else
            rc = RTErrConvertFromNtStatus(rcNt);
        RTNtPathFree(&NtName, NULL);
    }
    return rc;
}


RTDECL(int) RTDirRelDirRemove(RTDIR hDir, const char *pszRelPath)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Convert and normalize the path.
     */
    UNICODE_STRING NtName;
    HANDLE hRoot = pThis->hDir;
    int rc = RTNtPathRelativeFromUtf8(&NtName, &hRoot, pszRelPath, RTDIRREL_NT_GET_ASCENT(pThis),
                                      pThis->enmInfoClass == FileMaximumInformation);
    if (RT_SUCCESS(rc))
    {
        HANDLE              hSubDir = RTNT_INVALID_HANDLE_VALUE;
        IO_STATUS_BLOCK     Ios     = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        OBJECT_ATTRIBUTES   ObjAttr;
        InitializeObjectAttributes(&ObjAttr, &NtName, 0 /*fAttrib*/, hRoot, NULL);

        NTSTATUS rcNt = NtCreateFile(&hSubDir,
                                     DELETE | SYNCHRONIZE,
                                     &ObjAttr,
                                     &Ios,
                                     NULL /*AllocationSize*/,
                                     FILE_ATTRIBUTE_NORMAL,
                                     FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     FILE_OPEN,
                                     FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT,
                                     NULL /*EaBuffer*/,
                                     0 /*EaLength*/);
        if (NT_SUCCESS(rcNt))
        {
            FILE_DISPOSITION_INFORMATION DispInfo;
            DispInfo.DeleteFile = TRUE;
            RTNT_IO_STATUS_BLOCK_REINIT(&Ios);
            rcNt = NtSetInformationFile(hSubDir, &Ios, &DispInfo, sizeof(DispInfo), FileDispositionInformation);

            NTSTATUS rcNt2 = NtClose(hSubDir);
            if (!NT_SUCCESS(rcNt2) && NT_SUCCESS(rcNt))
                rcNt = rcNt2;
        }

        if (NT_SUCCESS(rcNt))
            rc = VINF_SUCCESS;
        else
            rc = RTErrConvertFromNtStatus(rcNt);

        RTNtPathFree(&NtName, NULL);
    }
    return rc;
}


/*
 *
 * RTPath stuff.
 * RTPath stuff.
 * RTPath stuff.
 *
 *
 */


RTDECL(int) RTDirRelPathQueryInfo(RTDIR hDir, const char *pszRelPath, PRTFSOBJINFO pObjInfo,
                                  RTFSOBJATTRADD enmAddAttr, uint32_t fFlags)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Validate and convert flags.
     */
    UNICODE_STRING NtName;
    HANDLE hRoot = pThis->hDir;
    int rc = RTNtPathRelativeFromUtf8(&NtName, &hRoot, pszRelPath, RTDIRREL_NT_GET_ASCENT(pThis),
                                      pThis->enmInfoClass == FileMaximumInformation);
    if (RT_SUCCESS(rc))
    {
        if (NtName.Length != 0 || hRoot == NULL)
            rc = rtPathNtQueryInfoWorker(hRoot, &NtName, pObjInfo, enmAddAttr, fFlags, pszRelPath);
        else
            rc = RTDirQueryInfo(hDir, pObjInfo, enmAddAttr);
       RTNtPathFree(&NtName, NULL);
    }
    return rc;
}


RTDECL(int) RTDirRelPathSetMode(RTDIR hDir, const char *pszRelPath, RTFMODE fMode, uint32_t fFlags)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);
    fMode = rtFsModeNormalize(fMode, pszRelPath, 0, 0);
    AssertReturn(rtFsModeIsValidPermissions(fMode), VERR_INVALID_FMODE);
    AssertMsgReturn(RTPATH_F_IS_VALID(fFlags, 0), ("%#x\n", fFlags), VERR_INVALID_FLAGS);

    /*
     * Convert and normalize the path.
     */
    UNICODE_STRING NtName;
    HANDLE hRoot = pThis->hDir;
    int rc = RTNtPathRelativeFromUtf8(&NtName, &hRoot, pszRelPath, RTDIRREL_NT_GET_ASCENT(pThis),
                                      pThis->enmInfoClass == FileMaximumInformation);
    if (RT_SUCCESS(rc))
    {
        HANDLE              hSubDir = RTNT_INVALID_HANDLE_VALUE;
        IO_STATUS_BLOCK     Ios     = RTNT_IO_STATUS_BLOCK_INITIALIZER;
        OBJECT_ATTRIBUTES   ObjAttr;
        InitializeObjectAttributes(&ObjAttr, &NtName, 0 /*fAttrib*/, hRoot, NULL);

        ULONG fOpenOptions = FILE_OPEN_FOR_BACKUP_INTENT | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT;
        if (fFlags & RTPATH_F_ON_LINK)
            fOpenOptions |= FILE_OPEN_REPARSE_POINT;
        NTSTATUS rcNt = NtCreateFile(&hSubDir,
                                     FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
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
            rc = rtNtFileSetModeWorker(hSubDir, fMode);

            rcNt = NtClose(hSubDir);
            if (!NT_SUCCESS(rcNt) && RT_SUCCESS(rc))
                rc = RTErrConvertFromNtStatus(rcNt);
        }
        else
            rc = RTErrConvertFromNtStatus(rcNt);

        RTNtPathFree(&NtName, NULL);
    }
    return rc;
}


RTDECL(int) RTDirRelPathSetTimes(RTDIR hDir, const char *pszRelPath, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                 PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime, uint32_t fFlags)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszRelPath);
    if (RT_SUCCESS(rc))
        rc = RTPathSetTimesEx(szPath, pAccessTime, pModificationTime, pChangeTime, pBirthTime, fFlags);
    return rc;
}


RTDECL(int) RTDirRelPathSetOwner(RTDIR hDir, const char *pszRelPath, uint32_t uid, uint32_t gid, uint32_t fFlags)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszRelPath);
    if (RT_SUCCESS(rc))
    {
#ifndef RT_OS_WINDOWS
        rc = RTPathSetOwnerEx(szPath, uid, gid, fFlags);
#else
        rc = VERR_NOT_IMPLEMENTED;
        RT_NOREF(uid, gid, fFlags);
#endif
    }
    return rc;
}


RTDECL(int) RTDirRelPathRename(RTDIR hDirSrc, const char *pszSrc, RTDIR hDirDst, const char *pszDst, unsigned fRename)
{
    PRTDIRINTERNAL pThis = hDirSrc;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    PRTDIRINTERNAL pThat = hDirDst;
    if (pThat != pThis)
    {
        AssertPtrReturn(pThat, VERR_INVALID_HANDLE);
        AssertReturn(pThat->u32Magic != RTDIR_MAGIC, VERR_INVALID_HANDLE);
    }

    char szSrcPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szSrcPath, sizeof(szSrcPath), pszSrc);
    if (RT_SUCCESS(rc))
    {
        char szDstPath[RTPATH_MAX];
        rc = rtDirRelBuildFullPath(pThis, szDstPath, sizeof(szDstPath), pszDst);
        if (RT_SUCCESS(rc))
            rc = RTPathRename(szSrcPath, szDstPath, fRename);
    }
    return rc;
}


RTDECL(int) RTDirRelPathUnlink(RTDIR hDir, const char *pszRelPath, uint32_t fUnlink)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszRelPath);
    if (RT_SUCCESS(rc))
        rc = RTPathUnlink(szPath, fUnlink);
    return rc;
}


/*
 *
 * RTSymlink stuff.
 * RTSymlink stuff.
 * RTSymlink stuff.
 *
 *
 */


RTDECL(int) RTDirRelSymlinkCreate(RTDIR hDir, const char *pszSymlink, const char *pszTarget,
                                  RTSYMLINKTYPE enmType, uint32_t fCreate)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszSymlink);
    if (RT_SUCCESS(rc))
        rc = RTSymlinkCreate(szPath, pszTarget, enmType, fCreate);
    return rc;
}


RTDECL(int) RTDirRelSymlinkRead(RTDIR hDir, const char *pszSymlink, char *pszTarget, size_t cbTarget, uint32_t fRead)
{
    PRTDIRINTERNAL pThis = hDir;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTDIR_MAGIC, VERR_INVALID_HANDLE);

    char szPath[RTPATH_MAX];
    int rc = rtDirRelBuildFullPath(pThis, szPath, sizeof(szPath), pszSymlink);
    if (RT_SUCCESS(rc))
        rc = RTSymlinkRead(szPath, pszTarget, cbTarget, fRead);
    return rc;
}

