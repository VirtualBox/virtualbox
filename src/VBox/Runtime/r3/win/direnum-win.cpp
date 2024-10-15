/* $Id: direnum-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Directory Enumeration, Windows.
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
#include <iprt/win/windows.h>

#include <iprt/dir.h>
#include <iprt/path.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include "internal/fs.h"
#include "internal/dir.h"


size_t rtDirNativeGetStructSize(const char *pszPath)
{
    NOREF(pszPath);
    return sizeof(RTDIRINTERNAL);
}


int rtDirNativeOpen(PRTDIRINTERNAL pDir, uintptr_t hRelativeDir, void *pvNativeRelative))
{
    RT_NOREF(hRelativeDir, pvNativeRelative);

    /*
     * Setup the search expression.
     *
     * pszPathBuf is pointing to the return 4K return buffer for the RTPathReal()
     * call in rtDirOpenCommon(), so all we gota do is check that we don't overflow
     * it when adding the wildcard expression.
     */
/** @todo the pszPathBuf argument was removed in order to support paths longer than RTPATH_MAX.  Rewrite this code. */
    size_t cbExpr;
    const char *pszExpr;
    if (pDir->enmFilter == RTDIRFILTER_WINNT)
    {
        pszExpr = pDir->pszFilter;
        cbExpr  = pDir->cchFilter + 1;
    }
    else
    {
        pszExpr = "*";
        cbExpr  = sizeof("*");
    }
    if (pDir->cchPath + cbExpr > RTPATH_MAX)
        return VERR_FILENAME_TOO_LONG;
    memcpy(pszPathBuf + pDir->cchPath, pszExpr, cbExpr);


    /*
     * Attempt opening the search.
     */
    PRTUTF16 pwszName;
    int rc = RTPathWinFromUtf8(pwszPathBuf, &pwszName, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        pDir->hDir = FindFirstFileW((LPCWSTR)pwszName, &pDir->Data);
        if (pDir->hDir != INVALID_HANDLE_VALUE)
            pDir->fDataUnread = true;
        else
        {
            DWORD dwErr = GetLastError();
            /* Theoretical case of an empty directory or more normal case of no matches. */
            if (   dwErr == ERROR_FILE_NOT_FOUND
                || dwErr == ERROR_NO_MORE_FILES /* ???*/)
                pDir->fDataUnread = false;
            else
                rc = RTErrConvertFromWin32(GetLastError());
        }
        RTPathWinFree(pwszName);
    }

    return rc;
}


RTDECL(int) RTDirClose(PRTDIRINTERNAL pDir)
{
    /*
     * Validate input.
     */
    if (!pDir)
        return VERR_INVALID_PARAMETER;
    if (pDir->u32Magic != RTDIR_MAGIC)
    {
        AssertMsgFailed(("Invalid pDir=%p\n", pDir));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Close the handle.
     */
    pDir->u32Magic++;
    if (pDir->hDir != INVALID_HANDLE_VALUE)
    {
        BOOL fRc = FindClose(pDir->hDir);
        Assert(fRc);
        pDir->hDir = INVALID_HANDLE_VALUE;
    }
    RTStrFree(pDir->pszName);
    pDir->pszName = NULL;
    RTMemFree(pDir);

    return VINF_SUCCESS;
}


RTDECL(int) RTDirRead(RTDIR hDir, PRTDIRENTRY pDirEntry, size_t *pcbDirEntry)
{
    PPRTDIRINTERNAL pDir = hDir;

    /*
     * Validate input.
     */
    if (!pDir || pDir->u32Magic != RTDIR_MAGIC)
    {
        AssertMsgFailed(("Invalid pDir=%p\n", pDir));
        return VERR_INVALID_PARAMETER;
    }
    if (!pDirEntry)
    {
        AssertMsgFailed(("Invalid pDirEntry=%p\n", pDirEntry));
        return VERR_INVALID_PARAMETER;
    }
    size_t cbDirEntry = sizeof(*pDirEntry);
    if (pcbDirEntry)
    {
        cbDirEntry = *pcbDirEntry;
        if (cbDirEntry < RT_UOFFSETOF(RTDIRENTRY, szName[2]))
        {
            AssertMsgFailed(("Invalid *pcbDirEntry=%zu (min %zu)\n", *pcbDirEntry, RT_UOFFSETOF(RTDIRENTRY, szName[2])));
            return VERR_INVALID_PARAMETER;
        }
    }

    /*
     * Fetch data?
     */
    if (!pDir->fDataUnread)
    {
        RTStrFree(pDir->pszName);
        pDir->pszName = NULL;

        BOOL fRc = FindNextFileW(pDir->hDir, &pDir->Data);
        if (!fRc)
        {
            int iErr = GetLastError();
            if (pDir->hDir == INVALID_HANDLE_VALUE || iErr == ERROR_NO_MORE_FILES)
                return VERR_NO_MORE_FILES;
            return RTErrConvertFromWin32(iErr);
        }
    }

    /*
     * Convert the filename to UTF-8.
     */
    if (!pDir->pszName)
    {
        int rc = RTUtf16ToUtf8((PCRTUTF16)pDir->Data.cFileName, &pDir->pszName);
        if (RT_FAILURE(rc))
        {
            pDir->pszName = NULL;
            return rc;
        }
        pDir->cchName = strlen(pDir->pszName);
    }

    /*
     * Check if we've got enough space to return the data.
     */
    const char  *pszName    = pDir->pszName;
    const size_t cchName    = pDir->cchName;
    const size_t cbRequired = RT_UOFFSETOF(RTDIRENTRY, szName[1]) + cchName;
    if (pcbDirEntry)
        *pcbDirEntry = cbRequired;
    if (cbRequired > cbDirEntry)
        return VERR_BUFFER_OVERFLOW;

    /*
     * Setup the returned data.
     */
    pDir->fDataUnread  = false;
    pDirEntry->INodeId = 0; /** @todo we can use the fileid here if we must (see GetFileInformationByHandle). */
    pDirEntry->enmType = pDir->Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY
                       ? RTDIRENTRYTYPE_DIRECTORY : RTDIRENTRYTYPE_FILE;
    pDirEntry->cbName  = (uint16_t)cchName;
    Assert(pDirEntry->cbName == cchName);
    memcpy(pDirEntry->szName, pszName, cchName + 1);

    return VINF_SUCCESS;
}


RTDECL(int) RTDirReadEx(RTDIR hDir, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry, RTFSOBJATTRADD enmAdditionalAttribs, uint32_t fFlags)
{
    PPRTDIRINTERNAL pDir = hDir;
    /** @todo Symlinks: Find[First|Next]FileW will return info about
        the link, so RTPATH_F_FOLLOW_LINK is not handled correctly. */
    /*
     * Validate input.
     */
    if (!pDir || pDir->u32Magic != RTDIR_MAGIC)
    {
        AssertMsgFailed(("Invalid pDir=%p\n", pDir));
        return VERR_INVALID_PARAMETER;
    }
    if (!pDirEntry)
    {
        AssertMsgFailed(("Invalid pDirEntry=%p\n", pDirEntry));
        return VERR_INVALID_PARAMETER;
    }
    if (    enmAdditionalAttribs < RTFSOBJATTRADD_NOTHING
        ||  enmAdditionalAttribs > RTFSOBJATTRADD_LAST)
    {
        AssertMsgFailed(("Invalid enmAdditionalAttribs=%p\n", enmAdditionalAttribs));
        return VERR_INVALID_PARAMETER;
    }
    AssertMsgReturn(RTPATH_F_IS_VALID(fFlags, 0), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);
    size_t cbDirEntry = sizeof(*pDirEntry);
    if (pcbDirEntry)
    {
        cbDirEntry = *pcbDirEntry;
        if (cbDirEntry < RT_UOFFSETOF(RTDIRENTRYEX, szName[2]))
        {
            AssertMsgFailed(("Invalid *pcbDirEntry=%zu (min %zu)\n", *pcbDirEntry, RT_UOFFSETOF(RTDIRENTRYEX, szName[2])));
            return VERR_INVALID_PARAMETER;
        }
    }

    /*
     * Fetch data?
     */
    if (!pDir->fDataUnread)
    {
        RTStrFree(pDir->pszName);
        pDir->pszName = NULL;

        BOOL fRc = FindNextFileW(pDir->hDir, &pDir->Data);
        if (!fRc)
        {
            int iErr = GetLastError();
            if (pDir->hDir == INVALID_HANDLE_VALUE || iErr == ERROR_NO_MORE_FILES)
                return VERR_NO_MORE_FILES;
            return RTErrConvertFromWin32(iErr);
        }
    }

    /*
     * Convert the filename to UTF-8.
     */
    if (!pDir->pszName)
    {
        int rc = RTUtf16ToUtf8((PCRTUTF16)pDir->Data.cFileName, &pDir->pszName);
        if (RT_FAILURE(rc))
        {
            pDir->pszName = NULL;
            return rc;
        }
        pDir->cchName = strlen(pDir->pszName);
    }

    /*
     * Check if we've got enough space to return the data.
     */
    const char  *pszName    = pDir->pszName;
    const size_t cchName    = pDir->cchName;
    const size_t cbRequired = RT_UOFFSETOF(RTDIRENTRYEX, szName[1]) + cchName;
    if (pcbDirEntry)
        *pcbDirEntry = cbRequired;
    if (cbRequired > cbDirEntry)
        return VERR_BUFFER_OVERFLOW;

    /*
     * Setup the returned data.
     */
    pDir->fDataUnread  = false;
    pDirEntry->cbName  = (uint16_t)cchName;
    Assert(pDirEntry->cbName == cchName);
    memcpy(pDirEntry->szName, pszName, cchName + 1);
    if (pDir->Data.cAlternateFileName[0])
    {
        /* copy and calc length */
        PCRTUTF16 pwszSrc = (PCRTUTF16)pDir->Data.cAlternateFileName;
        PRTUTF16  pwszDst = pDirEntry->wszShortName;
        uint32_t  off = 0;
        while (off < RT_ELEMENTS(pDirEntry->wszShortName) - 1U && pwszSrc[off])
        {
            pwszDst[off] = pwszSrc[off];
            off++;
        }
        pDirEntry->cwcShortName = (uint16_t)off;

        /* zero the rest */
        do
            pwszDst[off++] = '\0';
        while (off < RT_ELEMENTS(pDirEntry->wszShortName));
    }
    else
    {
        memset(pDirEntry->wszShortName, 0, sizeof(pDirEntry->wszShortName));
        pDirEntry->cwcShortName = 0;
    }

    pDirEntry->Info.cbObject    = ((uint64_t)pDir->Data.nFileSizeHigh << 32)
                                |  (uint64_t)pDir->Data.nFileSizeLow;
    pDirEntry->Info.cbAllocated = pDirEntry->Info.cbObject;

    Assert(sizeof(uint64_t) == sizeof(pDir->Data.ftCreationTime));
    RTTimeSpecSetNtTime(&pDirEntry->Info.BirthTime,         *(uint64_t *)&pDir->Data.ftCreationTime);
    RTTimeSpecSetNtTime(&pDirEntry->Info.AccessTime,        *(uint64_t *)&pDir->Data.ftLastAccessTime);
    RTTimeSpecSetNtTime(&pDirEntry->Info.ModificationTime,  *(uint64_t *)&pDir->Data.ftLastWriteTime);
    pDirEntry->Info.ChangeTime  = pDirEntry->Info.ModificationTime;

    pDirEntry->Info.Attr.fMode  = rtFsModeFromDos((pDir->Data.dwFileAttributes << RTFS_DOS_SHIFT) & RTFS_DOS_MASK_NT,
                                                   pszName, cchName, pDir->Data.dwReserved0, 0);

    /*
     * Requested attributes (we cannot provide anything actually).
     */
    switch (enmAdditionalAttribs)
    {
        case RTFSOBJATTRADD_EASIZE:
            pDirEntry->Info.Attr.enmAdditional          = RTFSOBJATTRADD_EASIZE;
            pDirEntry->Info.Attr.u.EASize.cb            = 0;
            break;

        case RTFSOBJATTRADD_UNIX:
            pDirEntry->Info.Attr.enmAdditional          = RTFSOBJATTRADD_UNIX;
            pDirEntry->Info.Attr.u.Unix.uid             = ~0U;
            pDirEntry->Info.Attr.u.Unix.gid             = ~0U;
            pDirEntry->Info.Attr.u.Unix.cHardlinks      = 1;
            pDirEntry->Info.Attr.u.Unix.INodeIdDevice   = 0; /** @todo Use the volume serial number (see GetFileInformationByHandle). */
            pDirEntry->Info.Attr.u.Unix.INodeId         = 0; /** @todo Use the fileid (see GetFileInformationByHandle). */
            pDirEntry->Info.Attr.u.Unix.fFlags          = 0;
            pDirEntry->Info.Attr.u.Unix.GenerationId    = 0;
            pDirEntry->Info.Attr.u.Unix.Device          = 0;
            break;

        case RTFSOBJATTRADD_NOTHING:
            pDirEntry->Info.Attr.enmAdditional          = RTFSOBJATTRADD_NOTHING;
            break;

        case RTFSOBJATTRADD_UNIX_OWNER:
            pDirEntry->Info.Attr.enmAdditional          = RTFSOBJATTRADD_UNIX_OWNER;
            pDirEntry->Info.Attr.u.UnixOwner.uid        = ~0U;
            pDirEntry->Info.Attr.u.UnixOwner.szName[0]  = '\0'; /** @todo return something sensible here. */
            break;

        case RTFSOBJATTRADD_UNIX_GROUP:
            pDirEntry->Info.Attr.enmAdditional          = RTFSOBJATTRADD_UNIX_GROUP;
            pDirEntry->Info.Attr.u.UnixGroup.gid        = ~0U;
            pDirEntry->Info.Attr.u.UnixGroup.szName[0]  = '\0';
            break;

        default:
            AssertMsgFailed(("Impossible!\n"));
            return VERR_INTERNAL_ERROR;
    }

    return VINF_SUCCESS;
}

