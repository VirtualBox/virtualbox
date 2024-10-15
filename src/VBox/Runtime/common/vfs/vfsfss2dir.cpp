/* $Id: vfsfss2dir.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Virtual File System, FS write stream dumping in a normal directory.
 *
 * This is just a simple mechanism to provide a drop in for the TAR  creator
 * that writes files individually to the disk instead of a TAR archive.  It
 * has an additional feature for removing the files to help bail out on error.
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
/// @todo #define RTVFSFSS2DIR_USE_DIR
#include "internal/iprt.h"
#include <iprt/vfs.h>

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#ifndef RTVFSFSS2DIR_USE_DIR
# include <iprt/path.h>
#endif
#include <iprt/string.h>
#include <iprt/vfslowlevel.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Undo entry for RTVFSFSSWRITE2DIR.
 */
typedef struct RTVFSFSSWRITE2DIRENTRY
{
    /** The list entry (head is RTVFSFSSWRITE2DIR::Entries). */
    RTLISTNODE      Entry;
    /** The file mode mask. */
    RTFMODE         fMode;
#ifdef RTVFSFSS2DIR_USE_DIR
    /** The name (relative to RTVFSFSSWRITE2DIR::hVfsBaseDir). */
#else
    /** The name (relative to RTVFSFSSWRITE2DIR::szBaseDir). */
#endif
    RT_FLEXIBLE_ARRAY_EXTENSION
    char            szName[RT_FLEXIBLE_ARRAY];
} RTVFSFSSWRITE2DIRENTRY;
/** Pointer to a RTVFSFSSWRITE2DIR undo entry. */
typedef RTVFSFSSWRITE2DIRENTRY *PRTVFSFSSWRITE2DIRENTRY;

/**
 * FSS write to directory instance.
 */
typedef struct RTVFSFSSWRITE2DIR
{
    /** Flags (RTVFSFSS2DIR_F_XXX). */
    uint32_t        fFlags;
    /** Number of files and stuff we've created. */
    uint32_t        cEntries;
    /** Files and stuff we've created (RTVFSFSSWRITE2DIRENTRY).
     * This is used for reverting changes on failure. */
    RTLISTANCHOR    Entries;
#ifdef RTVFSFSS2DIR_USE_DIR
    /** The handle of the base directory. */
    RTVFSDIR        hVfsBaseDir;
#else
    /** Path to the directory that all operations are relative to. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char            szBaseDir[RT_FLEXIBLE_ARRAY];
#endif
} RTVFSFSSWRITE2DIR;
/** Pointer to a write-to-directory FSS instance. */
typedef RTVFSFSSWRITE2DIR *PRTVFSFSSWRITE2DIR;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) rtVfsFssToDir_PushFile(void *pvThis, const char *pszPath, uint64_t cbFile, PCRTFSOBJINFO paObjInfo,
                                                uint32_t cObjInfo, uint32_t fFlags, PRTVFSIOSTREAM phVfsIos);


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtVfsFssToDir_Close(void *pvThis)
{
    PRTVFSFSSWRITE2DIR pThis = (PRTVFSFSSWRITE2DIR)pvThis;

#ifdef RTVFSFSS2DIR_USE_DIR
    RTVfsDirRelease(pThis->hVfsBaseDir);
    pThis->hVfsBaseDir = NIL_RTVFSDIR;
#endif

    PRTVFSFSSWRITE2DIRENTRY pCur;
    PRTVFSFSSWRITE2DIRENTRY pNext;
    RTListForEachSafe(&pThis->Entries, pCur, pNext, RTVFSFSSWRITE2DIRENTRY, Entry)
    {
        RTMemFree(pCur);
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtVfsFssToDir_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RT_NOREF(pvThis);

    /* no info here, sorry. */
    RT_ZERO(*pObjInfo);
    pObjInfo->Attr.enmAdditional = enmAddAttr;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSFSSTREAMOPS,pfnAdd}
 */
static DECLCALLBACK(int) rtVfsFssToDir_Add(void *pvThis, const char *pszPath, RTVFSOBJ hVfsObj, uint32_t fFlags)
{
    PRTVFSFSSWRITE2DIR pThis = (PRTVFSFSSWRITE2DIR)pvThis;
    RT_NOREF(fFlags);

    /*
     * Query information about the object.
     */
    RTFSOBJINFO ObjInfo;
    int rc = RTVfsObjQueryInfo(hVfsObj, &ObjInfo, RTFSOBJATTRADD_UNIX);
    AssertRCReturn(rc, rc);

    /*
     * Deal with files.
     */
    if (RTFS_IS_FILE(ObjInfo.Attr.fMode))
    {
        RTVFSIOSTREAM hVfsIosSrc = RTVfsObjToIoStream(hVfsObj);
        AssertReturn(hVfsIosSrc != NIL_RTVFSIOSTREAM, VERR_WRONG_TYPE);

        RTVFSIOSTREAM hVfsIosDst;
        rc = rtVfsFssToDir_PushFile(pvThis, pszPath, ObjInfo.cbObject, &ObjInfo, 1, 0 /*fFlags*/, &hVfsIosDst);
        if (RT_SUCCESS(rc))
        {
            rc = RTVfsUtilPumpIoStreams(hVfsIosSrc, hVfsIosDst, (size_t)RT_ALIGN(ObjInfo.cbObject, _4K));
            RTVfsIoStrmRelease(hVfsIosDst);
        }
        RTVfsIoStrmRelease(hVfsIosSrc);
    }
    /*
     * Symbolic links.
     */
    else if (RTFS_IS_SYMLINK(ObjInfo.Attr.fMode))
    {
        RTVFSSYMLINK hVfsSymlink = RTVfsObjToSymlink(hVfsObj);
        AssertReturn(hVfsSymlink != NIL_RTVFSSYMLINK, VERR_WRONG_TYPE);

        AssertFailedStmt(rc = VERR_NOT_IMPLEMENTED);
        RT_NOREF(pThis);

        RTVfsSymlinkRelease(hVfsSymlink);
    }
    /*
     * Directories.
     */
    else if (RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
        AssertFailedStmt(rc = VERR_NOT_IMPLEMENTED);
    /*
     * And whatever else we need when we need it...
     */
    else
        AssertFailedStmt(rc = VERR_NOT_IMPLEMENTED);

    return rc;
}


/**
 * @interface_method_impl{RTVFSFSSTREAMOPS,pfnPushFile}
 */
static DECLCALLBACK(int) rtVfsFssToDir_PushFile(void *pvThis, const char *pszPath, uint64_t cbFile, PCRTFSOBJINFO paObjInfo,
                                                uint32_t cObjInfo, uint32_t fFlags, PRTVFSIOSTREAM phVfsIos)
{
    PRTVFSFSSWRITE2DIR pThis = (PRTVFSFSSWRITE2DIR)pvThis;
    RT_NOREF(cbFile, fFlags);
    int rc;

#ifndef RTVFSFSS2DIR_USE_DIR
    /*
     * Join up the path with the base dir and make sure it fits.
     */
    char szFullPath[RTPATH_MAX];
    rc = RTPathJoin(szFullPath, sizeof(szFullPath), pThis->szBaseDir, pszPath);
    if (RT_SUCCESS(rc))
    {
#endif
        /*
         * Create an undo entry for it.
         */
        size_t const            cbRelativePath = strlen(pszPath);
        PRTVFSFSSWRITE2DIRENTRY pEntry;
        pEntry = (PRTVFSFSSWRITE2DIRENTRY)RTMemAllocVar(RT_UOFFSETOF_DYN(RTVFSFSSWRITE2DIRENTRY, szName[cbRelativePath]));
        if (pEntry)
        {
            if (cObjInfo)
                pEntry->fMode = (paObjInfo[0].Attr.fMode & ~RTFS_TYPE_MASK) | RTFS_TYPE_FILE;
            else
                pEntry->fMode = RTFS_TYPE_FILE | 0664;
            memcpy(pEntry->szName, pszPath, cbRelativePath);

            /*
             * Create the file.
             */
            uint64_t fOpen = RTFILE_O_WRITE | RTFILE_O_DENY_WRITE;
            fOpen |= ((pEntry->fMode & RTFS_UNIX_ALL_ACCESS_PERMS) << RTFILE_O_CREATE_MODE_SHIFT);
            if (!(pThis->fFlags & RTVFSFSS2DIR_F_OVERWRITE_FILES))
                fOpen |= RTFILE_O_CREATE;
            else
                fOpen |= RTFILE_O_CREATE_REPLACE;
#ifdef RTVFSFSS2DIR_USE_DIR
            rc = RTVfsDirOpenFileAsIoStream(pThis->hVfsBaseDir, pszPath, fOpen, phVfsIos);
#else
            rc = RTVfsIoStrmOpenNormal(szFullPath, fOpen, phVfsIos);
#endif
            if (RT_SUCCESS(rc))
                RTListAppend(&pThis->Entries, &pEntry->Entry);
            else
                RTMemFree(pEntry);
        }
        else
            rc = VERR_NO_MEMORY;
#ifndef RTVFSFSS2DIR_USE_DIR
    }
    else if (rc == VERR_BUFFER_OVERFLOW)
        rc = VERR_FILENAME_TOO_LONG;
#endif
    return rc;
}


/**
 * @interface_method_impl{RTVFSFSSTREAMOPS,pfnEnd}
 */
static DECLCALLBACK(int) rtVfsFssToDir_End(void *pvThis)
{
    RT_NOREF(pvThis);
    return VINF_SUCCESS;
}


/**
 * The write-to-directory FSS operations.
 */
static const RTVFSFSSTREAMOPS g_rtVfsFssToDirOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_FS_STREAM,
        "TarFsStreamWriter",
        rtVfsFssToDir_Close,
        rtVfsFssToDir_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSFSSTREAMOPS_VERSION,
    0,
    NULL,
    rtVfsFssToDir_Add,
    rtVfsFssToDir_PushFile,
    rtVfsFssToDir_End,
    RTVFSFSSTREAMOPS_VERSION
};


#ifdef RTVFSFSS2DIR_USE_DIR
RTDECL(int) RTVfsFsStrmToDir(RTVFSDIR hVfsBaseDir, uint32_t fFlags, PRTVFSFSSTREAM phVfsFss)
{
    /*
     * Input validation.
     */
    AssertPtrReturn(phVfsFss, VERR_INVALID_HANDLE);
    *phVfsFss = NIL_RTVFSFSSTREAM;
    AssertReturn(!(fFlags & ~RTVFSFSS2DIR_F_VALID_MASK), VERR_INVALID_FLAGS);
    uint32_t cRefs = RTVfsDirRetain(hVfsBaseDir);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Create the file system stream handle and init our data.
     */
    PRTVFSFSSWRITE2DIR      pThis;
    RTVFSFSSTREAM           hVfsFss;
    int rc = RTVfsNewFsStream(&g_rtVfsFssToDirOps, sizeof(*pThis), NIL_RTVFS, NIL_RTVFSLOCK, RTFILE_O_WRITE,
                              &hVfsFss, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->fFlags       = fFlags;
        pThis->cEntries     = 0;
        pThis->hVfsBaseDir  = hVfsBaseDir;
        RTListInit(&pThis->Entries);

        *phVfsFss = hVfsFss;
        return VINF_SUCCESS;
    }
    RTVfsDirRelease(hVfsBaseDir);

}
#endif /* RTVFSFSS2DIR_USE_DIR */


RTDECL(int) RTVfsFsStrmToNormalDir(const char *pszBaseDir, uint32_t fFlags, PRTVFSFSSTREAM phVfsFss)
{
#ifdef RTVFSFSS2DIR_USE_DIR
    RTVFSDIR hVfsBaseDir;
    int rc = RTVfsDirOpenNormal(pszBaseDir, 0 /*fFlags*/, &hVfsBaseDir);
    if (RT_SUCCESS(rc))
    {
        rc = RTVfsFsStrmToDir(hVfsBaseDir, fFlags, phVfsFss);
        RTVfsDirRelease(hVfsBaseDir);
    }
#else

    /*
     * Input validation.
     */
    AssertPtrReturn(phVfsFss, VERR_INVALID_HANDLE);
    *phVfsFss = NIL_RTVFSFSSTREAM;
    AssertReturn(!(fFlags & ~RTVFSFSS2DIR_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertPtrReturn(pszBaseDir, VERR_INVALID_POINTER);
    AssertReturn(*pszBaseDir != '\0', VERR_INVALID_NAME);

    /*
     * Straighten the path and make sure it's an existing directory.
     */
    char szAbsPath[RTPATH_MAX];
    int rc = RTPathAbs(pszBaseDir, szAbsPath, sizeof(szAbsPath));
    if (RT_SUCCESS(rc))
    {
        RTFSOBJINFO ObjInfo;
        rc = RTPathQueryInfo(szAbsPath, &ObjInfo, RTFSOBJATTRADD_NOTHING);
        if (RT_SUCCESS(rc))
        {
            if (RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
            {
                /*
                 * Create the file system stream handle and init our data.
                 */
                size_t const            cbBaseDir = strlen(szAbsPath) + 1;
                PRTVFSFSSWRITE2DIR      pThis;
                RTVFSFSSTREAM           hVfsFss;
                rc = RTVfsNewFsStream(&g_rtVfsFssToDirOps, RT_UOFFSETOF_DYN(RTVFSFSSWRITE2DIR, szBaseDir[cbBaseDir]),
                                      NIL_RTVFS, NIL_RTVFSLOCK, RTFILE_O_WRITE, &hVfsFss, (void **)&pThis);
                if (RT_SUCCESS(rc))
                {
                    pThis->fFlags   = fFlags;
                    pThis->cEntries = 0;
                    RTListInit(&pThis->Entries);
                    memcpy(pThis->szBaseDir, szAbsPath, cbBaseDir);

                    *phVfsFss = hVfsFss;
                    return VINF_SUCCESS;
                }
            }
            else
                rc = VERR_NOT_A_DIRECTORY;
        }
    }
#endif
    return rc;
}


RTDECL(int) RTVfsFsStrmToDirUndo(RTVFSFSSTREAM hVfsFss)
{
    /*
     * Validate input.
     */
    PRTVFSFSSWRITE2DIR pThis = (PRTVFSFSSWRITE2DIR)RTVfsFsStreamToPrivate(hVfsFss, &g_rtVfsFssToDirOps);
    AssertReturn(pThis, VERR_WRONG_TYPE);

    /*
     * Do the job, in reverse order.  Dropping stuff we
     * successfully remove from the list.
     */
    int                   rc = VINF_SUCCESS;
    PRTVFSFSSWRITE2DIRENTRY pCur;
    PRTVFSFSSWRITE2DIRENTRY pPrev;
    RTListForEachReverseSafe(&pThis->Entries, pCur, pPrev, RTVFSFSSWRITE2DIRENTRY, Entry)
    {
#ifdef RTVFSFSS2DIR_USE_DIR
        int rc2 = RTVfsDirUnlinkEntry(pThis->hVfsBaseDir, pCur->szName);
#else
        char szFullPath[RTPATH_MAX];
        int rc2 = RTPathJoin(szFullPath, sizeof(szFullPath), pThis->szBaseDir, pCur->szName);
        AssertRC(rc2);
        if (RT_SUCCESS(rc2))
            rc2 = RTPathUnlink(szFullPath, 0 /*fUnlink*/);
#endif
        if (   RT_SUCCESS(rc2)
            || rc2 == VERR_PATH_NOT_FOUND
            || rc2 == VERR_FILE_NOT_FOUND
            || rc2 == VERR_NOT_FOUND)
        {
            RTListNodeRemove(&pCur->Entry);
            RTMemFree(pCur);
        }
        else if (RT_SUCCESS(rc))
            rc = rc2;
    }
    return rc;
}

