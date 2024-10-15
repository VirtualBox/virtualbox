/* $Id: DnDTransferList.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DnD - transfer list implemenation.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * This implementation is taylored to keeping track of a single DnD transfer by maintaining two separate entities,
 * namely a list of root entries and a list of (recursive file system) transfer ojects to actually transfer.
 *
 * The list of root entries is sent to the target (guest/host) beforehand so that the OS has a data for the
 * actual drag'n drop operation to work with. This also contains required header data like total number of
 * objects or total bytes being received.
 *
 * The list of transfer objects only is needed in order to sending data from the source to the target.
 * Currently there is no particular ordering implemented for the transfer object list; it depends on IPRT's RTDirRead().
 *
 * The target must not know anything about the actual (absolute) path the root entries are coming from
 * due to security reasons. Those root entries then can be re-based on the target to desired location there.
 *
 * All data handling internally is done in the so-called "transport" format, that is, non-URI (regular) paths
 * with the "/" as path separator. From/to URI conversion is provided for convenience only.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_GUEST_DND
#include <VBox/GuestHost/DragAndDrop.h>

#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/fs.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/symlink.h>
#include <iprt/uri.h>

#include <VBox/log.h>


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
static int dndTransferListSetRootPath(PDNDTRANSFERLIST pList, const char *pcszRootPathAbs);

static int dndTransferListRootEntryAdd(PDNDTRANSFERLIST pList, const char *pcszRoot);
static void dndTransferListRootEntryFree(PDNDTRANSFERLIST pList, PDNDTRANSFERLISTROOT pRootObj);

static int dndTransferListObjAdd(PDNDTRANSFERLIST pList, const char *pcszSrcAbs, RTFMODE fMode, DNDTRANSFERLISTFLAGS fFlags);
static void dndTransferListObjFree(PDNDTRANSFERLIST pList, PDNDTRANSFEROBJECT pLstObj);


/** The size of the directory entry buffer we're using. */
#define DNDTRANSFERLIST_DIRENTRY_BUF_SIZE (sizeof(RTDIRENTRYEX) + RTPATH_MAX)


/**
 * Initializes a transfer list, internal version.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to initialize.
 * @param   pcszRootPathAbs     Absolute root path to use for this list. Optional and can be NULL.
 */
static int dndTransferListInitInternal(PDNDTRANSFERLIST pList, const char *pcszRootPathAbs)
{
    AssertPtrReturn(pList, VERR_INVALID_POINTER);
    /* pcszRootPathAbs is optional. */

    if (pList->pszPathRootAbs) /* Already initialized? */
        return VERR_WRONG_ORDER;

    pList->pszPathRootAbs = NULL;

    RTListInit(&pList->lstRoot);
    pList->cRoots = 0;

    RTListInit(&pList->lstObj);
    pList->cObj = 0;
    pList->cbObjTotal = 0;

    if (pcszRootPathAbs)
        return dndTransferListSetRootPath(pList, pcszRootPathAbs);

    return VINF_SUCCESS;
}

/**
 * Initializes a transfer list, extended version.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to initialize.
 * @param   pcszRootPathAbs     Absolute root path to use for this list.
 * @param   enmFmt              Format of \a pcszRootPathAbs.
 */
int DnDTransferListInitEx(PDNDTRANSFERLIST pList, const char *pcszRootPathAbs, DNDTRANSFERLISTFMT enmFmt)
{
    AssertPtrReturn(pList, VERR_INVALID_POINTER);
    AssertPtrReturn(pcszRootPathAbs, VERR_INVALID_POINTER);
    AssertReturn(*pcszRootPathAbs, VERR_INVALID_PARAMETER);

    int rc;

    if (enmFmt == DNDTRANSFERLISTFMT_URI)
    {
        char *pszPath;
        rc = RTUriFilePathEx(pcszRootPathAbs, RTPATH_STR_F_STYLE_UNIX, &pszPath, 0 /*cbPath*/, NULL /*pcchPath*/);
        if (RT_SUCCESS(rc))
        {
            rc = dndTransferListInitInternal(pList, pszPath);
            RTStrFree(pszPath);
        }
    }
    else
        rc = dndTransferListInitInternal(pList, pcszRootPathAbs);

    return rc;
}

/**
 * Initializes a transfer list.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to initialize.
 */
int DnDTransferListInit(PDNDTRANSFERLIST pList)
{
    return dndTransferListInitInternal(pList, NULL /* pcszRootPathAbs */);
}

/**
 * Destroys a transfer list.
 *
 * @param   pList               Transfer list to destroy.
 */
void DnDTransferListDestroy(PDNDTRANSFERLIST pList)
{
    if (!pList)
        return;

    DnDTransferListReset(pList);

    RTStrFree(pList->pszPathRootAbs);
    pList->pszPathRootAbs = NULL;
}

/**
 * Initializes a transfer list and sets the root path.
 *
 * Convenience function which calls dndTransferListInitInternal() if not initialized already.
 *
 * @returns VBox status code.
 * @param   pList               List to determine root path for.
 * @param   pcszRootPathAbs     Root path to use.
 */
static int dndTransferInitAndSetRoot(PDNDTRANSFERLIST pList, const char *pcszRootPathAbs)
{
    int rc;

    if (!pList->pszPathRootAbs)
    {
        rc = dndTransferListInitInternal(pList, pcszRootPathAbs);
        AssertRCReturn(rc, rc);

        LogRel2(("DnD: Determined root path is '%s'\n", pList->pszPathRootAbs));
    }
    else
        rc = VINF_SUCCESS;

    return rc;
}

/**
 * Resets a transfer list to its initial state.
 *
 * @param   pList               Transfer list to reset.
 */
void DnDTransferListReset(PDNDTRANSFERLIST pList)
{
    AssertPtrReturnVoid(pList);

    if (!pList->pszPathRootAbs)
        return;

    RTStrFree(pList->pszPathRootAbs);
    pList->pszPathRootAbs = NULL;

    PDNDTRANSFERLISTROOT pRootCur, pRootNext;
    RTListForEachSafe(&pList->lstRoot, pRootCur, pRootNext, DNDTRANSFERLISTROOT, Node)
        dndTransferListRootEntryFree(pList, pRootCur);
    Assert(RTListIsEmpty(&pList->lstRoot));

    PDNDTRANSFEROBJECT pObjCur, pObjNext;
    RTListForEachSafe(&pList->lstObj, pObjCur, pObjNext, DNDTRANSFEROBJECT, Node)
        dndTransferListObjFree(pList, pObjCur);
    Assert(RTListIsEmpty(&pList->lstObj));

    Assert(pList->cRoots == 0);
    Assert(pList->cObj == 0);

    pList->cbObjTotal = 0;
}

/**
 * Adds a single transfer object entry to a transfer List.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to add entry to.
 * @param   pcszSrcAbs          Absolute source path (local) to use.
 * @param   fMode               File mode of entry to add.
 * @param   fFlags              Transfer list flags to use for appending.
 */
static int dndTransferListObjAdd(PDNDTRANSFERLIST pList, const char *pcszSrcAbs, RTFMODE fMode, DNDTRANSFERLISTFLAGS fFlags)
{
    AssertPtrReturn(pList, VERR_INVALID_POINTER);
    AssertPtrReturn(pcszSrcAbs, VERR_INVALID_POINTER);

    LogFlowFunc(("pcszSrcAbs=%s, fMode=%#x, fFlags=0x%x\n", pcszSrcAbs, fMode, fFlags));

    int rc = VINF_SUCCESS;

    if (   !RTFS_IS_FILE(fMode)
        && !RTFS_IS_DIRECTORY(fMode))
        /** @todo Symlinks not allowed. */
    {
        rc = VERR_NOT_SUPPORTED;
    }

    if (RT_SUCCESS(rc))
    {
        /* Calculate the path to add as the destination path to our URI object. */
        const size_t idxPathToAdd = strlen(pList->pszPathRootAbs);
        AssertReturn(strlen(pcszSrcAbs) > idxPathToAdd, VERR_INVALID_PARAMETER); /* Should never happen (tm). */

        PDNDTRANSFEROBJECT pObj = (PDNDTRANSFEROBJECT)RTMemAllocZ(sizeof(DNDTRANSFEROBJECT));
        if (pObj)
        {
            const bool fIsFile = RTFS_IS_FILE(fMode);

            rc = DnDTransferObjectInitEx(pObj, fIsFile ? DNDTRANSFEROBJTYPE_FILE : DNDTRANSFEROBJTYPE_DIRECTORY,
                                         pList->pszPathRootAbs, &pcszSrcAbs[idxPathToAdd]);
            if (RT_SUCCESS(rc))
            {
                if (fIsFile)
                    rc = DnDTransferObjectOpen(pObj,
                                               RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_WRITE, /** @todo Add a standard fOpen mode for this list. */
                                               0 /* fMode */, DNDTRANSFEROBJECT_FLAGS_NONE);
                if (RT_SUCCESS(rc))
                {
                    RTListAppend(&pList->lstObj, &pObj->Node);

                    pList->cObj++;
                    if (fIsFile)
                        pList->cbObjTotal += DnDTransferObjectGetSize(pObj);

                    if (   fIsFile
                        && !(fFlags & DNDTRANSFERLIST_FLAGS_KEEP_OPEN)) /* Shall we keep the file open while being added to this list? */
                        rc = DnDTransferObjectClose(pObj);
                }

                if (RT_FAILURE(rc))
                    DnDTransferObjectDestroy(pObj);
            }

            if (RT_FAILURE(rc))
                RTMemFree(pObj);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(rc))
        LogRel(("DnD: Adding entry '%s' of type %#x failed with rc=%Rrc\n", pcszSrcAbs, fMode & RTFS_TYPE_MASK, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Frees an internal DnD transfer list object.
 *
 * @param   pList               Transfer list to free object for.
 * @param   pLstObj             transfer list object to free. The pointer will be invalid after calling.
 */
static void dndTransferListObjFree(PDNDTRANSFERLIST pList, PDNDTRANSFEROBJECT pObj)
{
    if (!pObj)
        return;

    DnDTransferObjectDestroy(pObj);
    RTListNodeRemove(&pObj->Node);
    RTMemFree(pObj);

    AssertReturnVoid(pList->cObj);
    pList->cObj--;
}

/**
 * Helper routine for handling adding sub directories.
 *
 * @return  IPRT status code.
 * @param   pList           transfer list to add found entries to.
 * @param   pszDir          Pointer to the directory buffer.
 * @param   cchDir          The length of pszDir in pszDir.
 * @param   pDirEntry       Pointer to the directory entry.
 * @param   fFlags          Flags of type DNDTRANSFERLISTFLAGS.
 */
static int dndTransferListAppendPathRecursiveSub(PDNDTRANSFERLIST pList,
                                                 char *pszDir, size_t cchDir, PRTDIRENTRYEX pDirEntry,
                                                 DNDTRANSFERLISTFLAGS fFlags)

{
    Assert(cchDir > 0); Assert(pszDir[cchDir] == '\0');

    /* Make sure we've got some room in the path, to save us extra work further down. */
    if (cchDir + 3 >= RTPATH_MAX)
        return VERR_BUFFER_OVERFLOW;

    /* Open directory. */
    RTDIR hDir;
    int rc = RTDirOpen(&hDir, pszDir);
    if (RT_FAILURE(rc))
        return rc;

    /* Ensure we've got a trailing slash (there is space for it see above). */
    if (!RTPATH_IS_SEP(pszDir[cchDir - 1]))
    {
        pszDir[cchDir++] = RTPATH_SLASH;
        pszDir[cchDir]   = '\0';
    }

    rc = dndTransferListObjAdd(pList, pszDir, pDirEntry->Info.Attr.fMode, fFlags);
    AssertRCReturn(rc, rc);

    LogFlowFunc(("pszDir=%s\n", pszDir));

    /*
     * Process the files and subdirs.
     */
    for (;;)
    {
        /* Get the next directory. */
        size_t cbDirEntry = DNDTRANSFERLIST_DIRENTRY_BUF_SIZE;
        rc = RTDirReadEx(hDir, pDirEntry, &cbDirEntry, RTFSOBJATTRADD_UNIX, RTPATH_F_ON_LINK);
        if (RT_FAILURE(rc))
            break;

        /* Check length. */
        if (pDirEntry->cbName + cchDir + 3 >= RTPATH_MAX)
        {
            rc = VERR_BUFFER_OVERFLOW;
            break;
        }

        switch (pDirEntry->Info.Attr.fMode & RTFS_TYPE_MASK)
        {
            case RTFS_TYPE_SYMLINK:
            {
                if (!(fFlags & DNDTRANSFERLIST_FLAGS_RESOLVE_SYMLINKS))
                    break;
                RT_FALL_THRU();
            }
            case RTFS_TYPE_DIRECTORY:
            {
                if (RTDirEntryExIsStdDotLink(pDirEntry))
                    continue;

                memcpy(&pszDir[cchDir], pDirEntry->szName, pDirEntry->cbName + 1);
                int rc2 = dndTransferListAppendPathRecursiveSub(pList, pszDir, cchDir + pDirEntry->cbName, pDirEntry, fFlags);
                if (RT_SUCCESS(rc))
                    rc = rc2;
                break;
            }

            case RTFS_TYPE_FILE:
            {
                memcpy(&pszDir[cchDir], pDirEntry->szName, pDirEntry->cbName + 1);
                rc = dndTransferListObjAdd(pList, pszDir, pDirEntry->Info.Attr.fMode, fFlags);
                break;
            }

            default:
            {

                break;
            }
        }
    }

    if (rc == VERR_NO_MORE_FILES) /* Done reading current directory? */
    {
        rc = VINF_SUCCESS;
    }
    else if (RT_FAILURE(rc))
        LogRel(("DnD: Error while adding files recursively, rc=%Rrc\n", rc));

    int rc2 = RTDirClose(hDir);
    if (RT_FAILURE(rc2))
    {
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}

/**
 * Appends a native system path recursively by adding these entries as transfer objects.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to add found entries to.
 * @param   pcszPathAbs         Absolute path to add.
 * @param   fFlags              Flags of type DNDTRANSFERLISTFLAGS.
 */
static int dndTransferListAppendDirectoryRecursive(PDNDTRANSFERLIST pList,
                                                   const char *pcszPathAbs, DNDTRANSFERLISTFLAGS fFlags)
{
    char szPathAbs[RTPATH_MAX];
    int rc = RTStrCopy(szPathAbs, sizeof(szPathAbs), pcszPathAbs);
    if (RT_FAILURE(rc))
        return rc;

    union
    {
        uint8_t         abPadding[DNDTRANSFERLIST_DIRENTRY_BUF_SIZE];
        RTDIRENTRYEX    DirEntry;
    } uBuf;

    const size_t cchPathAbs = RTStrNLen(szPathAbs, RTPATH_MAX);
    AssertReturn(cchPathAbs, VERR_BUFFER_OVERFLOW);

    /* Use the directory entry to hand-in the directorie's information. */
    rc = RTPathQueryInfo(pcszPathAbs, &uBuf.DirEntry.Info, RTFSOBJATTRADD_NOTHING);
    AssertRCReturn(rc, rc);

    return dndTransferListAppendPathRecursiveSub(pList, szPathAbs, cchPathAbs, &uBuf.DirEntry, fFlags);
}

/**
 * Helper function for appending a local directory to a DnD transfer list.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to return total number of root entries for.
 * @param   pszPathAbs          Absolute path of directory to append.
 * @param   cbPathAbs           Size (in bytes) of absolute path of directory to append.
 * @param   pObjInfo            Pointer to directory object info to append.
 * @param   fFlags              Transfer list flags to use for appending.
 */
static int dndTransferListAppendDirectory(PDNDTRANSFERLIST pList, char* pszPathAbs, size_t cbPathAbs,
                                          PRTFSOBJINFO pObjInfo, DNDTRANSFERLISTFLAGS fFlags)
{
    const size_t cchPathRoot = RTStrNLen(pList->pszPathRootAbs, RTPATH_MAX);
    AssertReturn(cchPathRoot, VERR_INVALID_PARAMETER);

    const size_t cchPathAbs = RTPathEnsureTrailingSeparator(pszPathAbs, sizeof(cbPathAbs));
    AssertReturn(cchPathAbs, VERR_BUFFER_OVERFLOW);
    AssertReturn(cchPathAbs >= cchPathRoot, VERR_BUFFER_UNDERFLOW);

    const bool fPathIsRoot  = cchPathAbs == cchPathRoot;

    int rc;

    if (!fPathIsRoot)
    {
        rc = dndTransferListObjAdd(pList, pszPathAbs, pObjInfo->Attr.fMode, fFlags);
        AssertRCReturn(rc, rc);
    }

    RTDIR hDir;
    rc = RTDirOpen(&hDir, pszPathAbs);
    AssertRCReturn(rc, rc);

    for (;;)
    {
        /* Get the next entry. */
        RTDIRENTRYEX dirEntry;
        rc = RTDirReadEx(hDir, &dirEntry, NULL, RTFSOBJATTRADD_UNIX,
                         RTPATH_F_ON_LINK /** @todo No symlinks yet. */);
        if (RT_SUCCESS(rc))
        {
            if (RTDirEntryExIsStdDotLink(&dirEntry))
                continue;

            /* Check length. */
            if (dirEntry.cbName + cchPathAbs + 3 >= cbPathAbs)
            {
                rc = VERR_BUFFER_OVERFLOW;
                break;
            }

            /* Append the directory entry to our absolute path. */
            memcpy(&pszPathAbs[cchPathAbs], dirEntry.szName, dirEntry.cbName + 1 /* Include terminator */);

            LogFlowFunc(("szName=%s, pszPathAbs=%s\n", dirEntry.szName, pszPathAbs));

            switch (dirEntry.Info.Attr.fMode & RTFS_TYPE_MASK)
            {
                case RTFS_TYPE_DIRECTORY:
                {
                    if (fFlags & DNDTRANSFERLIST_FLAGS_RECURSIVE)
                        rc = dndTransferListAppendDirectoryRecursive(pList, pszPathAbs, fFlags);
                    break;
                }

                case RTFS_TYPE_FILE:
                {
                    rc = dndTransferListObjAdd(pList, pszPathAbs, dirEntry.Info.Attr.fMode, fFlags);
                    break;
                }

                default:
                    /* Silently skip everything else. */
                    break;
            }

            if (   RT_SUCCESS(rc)
                /* Make sure to add a root entry if we're processing the root path at the moment. */
                && fPathIsRoot)
            {
                rc = dndTransferListRootEntryAdd(pList, pszPathAbs);
                if (RT_FAILURE(rc))
                    break;
            }
        }
        else if (rc == VERR_NO_MORE_FILES)
        {
            rc = VINF_SUCCESS;
            break;
        }
        else
            break;
    }

    RTDirClose(hDir);
    return rc;
}

/**
 * Appends a native path to a DnD transfer list.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to append native path to.
 * @param   pcszPath            Path (native) to append.
 * @param   fFlags              Transfer list flags to use for appending.
 */
static int dndTransferListAppendPathNative(PDNDTRANSFERLIST pList, const char *pcszPath, DNDTRANSFERLISTFLAGS fFlags)
{
    /* We don't want to have a relative directory here. */
    if (!RTPathStartsWithRoot(pcszPath))
        return VERR_INVALID_PARAMETER;

    int rc = DnDPathValidate(pcszPath, false /* fMustExist */);
    AssertRCReturn(rc, rc);

    char   szPathAbs[RTPATH_MAX];
    size_t cbPathAbs = sizeof(szPathAbs);
    rc = RTStrCopy(szPathAbs, cbPathAbs, pcszPath);
    AssertRCReturn(rc, rc);

    size_t cchPathAbs = RTStrNLen(szPathAbs, cbPathAbs);
    AssertReturn(cchPathAbs, VERR_INVALID_PARAMETER);

    /* Convert path to transport style. */
    rc = DnDPathConvert(szPathAbs, cbPathAbs, DNDPATHCONVERT_FLAGS_TRANSPORT);
    if (RT_SUCCESS(rc))
    {
        /* Make sure the path has the same root path as our list. */
        if (RTPathStartsWith(szPathAbs, pList->pszPathRootAbs))
        {
            RTFSOBJINFO objInfo;
            rc = RTPathQueryInfo(szPathAbs, &objInfo, RTFSOBJATTRADD_NOTHING);
            if (RT_SUCCESS(rc))
            {
                const uint32_t fType = objInfo.Attr.fMode & RTFS_TYPE_MASK;

                if (   RTFS_IS_DIRECTORY(fType)
                    || RTFS_IS_FILE(fType))
                {
                    if (RTFS_IS_DIRECTORY(fType))
                    {
                        cchPathAbs = RTPathEnsureTrailingSeparator(szPathAbs, cbPathAbs);
                        AssertReturn(cchPathAbs, VERR_BUFFER_OVERFLOW);
                    }

                    const size_t cchPathRoot = RTStrNLen(pList->pszPathRootAbs, RTPATH_MAX);
                    AssertStmt(cchPathRoot, rc = VERR_INVALID_PARAMETER);

                    if (   RT_SUCCESS(rc)
                        && cchPathAbs > cchPathRoot)
                        rc = dndTransferListRootEntryAdd(pList, szPathAbs);
                }
                else
                    rc = VERR_NOT_SUPPORTED;

                if (RT_SUCCESS(rc))
                {
                    switch (fType)
                    {
                        case RTFS_TYPE_DIRECTORY:
                        {
                            rc = dndTransferListAppendDirectory(pList, szPathAbs, cbPathAbs, &objInfo, fFlags);
                            break;
                        }

                        case RTFS_TYPE_FILE:
                        {
                            rc = dndTransferListObjAdd(pList, szPathAbs, objInfo.Attr.fMode, fFlags);
                            break;
                        }

                        default:
                            AssertFailed();
                            break;
                    }
                }
            }
            /* On UNIX-y OSes RTPathQueryInfo() returns VERR_FILE_NOT_FOUND in some cases
             * so tweak this to make it uniform to Windows. */
            else if (rc == VERR_FILE_NOT_FOUND)
                rc = VERR_PATH_NOT_FOUND;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    if (RT_FAILURE(rc))
        LogRel(("DnD: Adding native path '%s' failed with rc=%Rrc\n", pcszPath, rc));

    return rc;
}

/**
 * Appends an URI path to a DnD transfer list.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to append native path to.
 * @param   pcszPath            URI path to append.
 * @param   fFlags              Transfer list flags to use for appending.
 */
static int dndTransferListAppendPathURI(PDNDTRANSFERLIST pList, const char *pcszPath, DNDTRANSFERLISTFLAGS fFlags)
{
    RT_NOREF(fFlags);

    /* Query the path component of a file URI. If this hasn't a
     * file scheme, NULL is returned. */
    char *pszPath;
    int rc = RTUriFilePathEx(pcszPath, RTPATH_STR_F_STYLE_UNIX, &pszPath, 0 /*cbPath*/, NULL /*pcchPath*/);
    if (RT_SUCCESS(rc))
    {
        rc = dndTransferListAppendPathNative(pList, pszPath, fFlags);
        RTStrFree(pszPath);
    }

    if (RT_FAILURE(rc))
        LogRel(("DnD: Adding URI path '%s' failed with rc=%Rrc\n", pcszPath, rc));

    return rc;
}

/**
 * Appends a single path to a transfer list.
 *
 * @returns VBox status code. VERR_NOT_SUPPORTED if the path is not supported.
 * @param   pList               Transfer list to append to.
 * @param   enmFmt              Format of \a pszPaths to append.
 * @param   pcszPath            Path to append. Must be part of the list's set root path.
 * @param   fFlags              Transfer list flags to use for appending.
 */
int DnDTransferListAppendPath(PDNDTRANSFERLIST pList,
                              DNDTRANSFERLISTFMT enmFmt, const char *pcszPath, DNDTRANSFERLISTFLAGS fFlags)
{
    AssertPtrReturn(pList, VERR_INVALID_POINTER);
    AssertPtrReturn(pcszPath, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~DNDTRANSFERLIST_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(!(fFlags & DNDTRANSFERLIST_FLAGS_RESOLVE_SYMLINKS), VERR_NOT_SUPPORTED);

    int rc;

    switch (enmFmt)
    {
        case DNDTRANSFERLISTFMT_NATIVE:
            rc = dndTransferListAppendPathNative(pList, pcszPath, fFlags);
            break;

        case DNDTRANSFERLISTFMT_URI:
            rc = dndTransferListAppendPathURI(pList, pcszPath, fFlags);
            break;

        default:
            AssertFailedStmt(rc = VERR_NOT_SUPPORTED);
            break; /* Never reached */
    }

    return rc;
}

/**
 * Appends native paths to a transfer list.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to append to.
 * @param   enmFmt              Format of \a pszPaths to append.
 * @param   pszPaths            Buffer of paths to append.
 * @param   cbPaths             Size (in bytes) of buffer of paths to append.
 * @param   pcszSeparator       Separator string to use.
 * @param   fFlags              Transfer list flags to use for appending.
 */
int DnDTransferListAppendPathsFromBuffer(PDNDTRANSFERLIST pList,
                                         DNDTRANSFERLISTFMT enmFmt, const char *pszPaths, size_t cbPaths,
                                         const char *pcszSeparator, DNDTRANSFERLISTFLAGS fFlags)
{
    AssertPtrReturn(pList, VERR_INVALID_POINTER);
    AssertPtrReturn(pszPaths, VERR_INVALID_POINTER);
    AssertReturn(cbPaths, VERR_INVALID_PARAMETER);

    char **papszPaths = NULL;
    size_t cPaths = 0;
    int rc = RTStrSplit(pszPaths, cbPaths, pcszSeparator, &papszPaths, &cPaths);
    if (RT_SUCCESS(rc))
        rc = DnDTransferListAppendPathsFromArray(pList, enmFmt, papszPaths, cPaths, fFlags);

    for (size_t i = 0; i < cPaths; ++i)
        RTStrFree(papszPaths[i]);
    RTMemFree(papszPaths);

    return rc;
}

/**
 * Appends paths to a transfer list.
 *
 * @returns VBox status code. Will return VERR_INVALID_PARAMETER if a common root path could not be found.
 * @param   pList               Transfer list to append path to.
 * @param   enmFmt              Format of \a papcszPaths to append.
 * @param   papcszPaths         Array of paths to append.
 * @param   cPaths              Number of paths in \a papcszPaths to append.
 * @param   fFlags              Transfer list flags to use for appending.
 */
int DnDTransferListAppendPathsFromArray(PDNDTRANSFERLIST pList,
                                        DNDTRANSFERLISTFMT enmFmt,
                                        const char * const *papcszPaths, size_t cPaths, DNDTRANSFERLISTFLAGS fFlags)
{
    AssertPtrReturn(pList, VERR_INVALID_POINTER);
    AssertPtrReturn(papcszPaths, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~DNDTRANSFERLIST_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);

    int rc = VINF_SUCCESS;

    if (!cPaths) /* Nothing to add? Bail out. */
        return VINF_SUCCESS;

    char **papszPathsTmp = NULL;

    /* If URI data is being handed in, extract the paths first. */
    if (enmFmt == DNDTRANSFERLISTFMT_URI)
    {
        papszPathsTmp = (char **)RTMemAlloc(sizeof(char *) * cPaths);
        if (papszPathsTmp)
        {
            for (size_t i = 0; i < cPaths; i++)
                papszPathsTmp[i] = RTUriFilePath(papcszPaths[i]);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(rc))
        return rc;

    /* If we don't have a root path set, try to find the common path of all handed-in paths. */
    if (!pList->pszPathRootAbs)
    {
        /* Can we work on the unmodified, handed-in data or do we need to use our temporary paths? */
        const char * const *papszPathTmp = enmFmt == DNDTRANSFERLISTFMT_NATIVE
                                         ? papcszPaths : papszPathsTmp;

        size_t cchRootPath = 0; /* Length of root path in chars. */
        if (cPaths > 1)
        {
            cchRootPath = RTPathFindCommon(cPaths, papszPathTmp);
        }
        else
            cchRootPath = RTPathParentLength(papszPathTmp[0]);

        if (cchRootPath)
        {
            /* Just use the first path in the array as the reference. */
            char *pszRootPath = RTStrDupN(papszPathTmp[0], cchRootPath);
            if (pszRootPath)
            {
                rc = dndTransferInitAndSetRoot(pList, pszRootPath);
                RTStrFree(pszRootPath);
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }

    if (RT_SUCCESS(rc))
    {
        /*
         * Add all paths to the list.
         */
        for (size_t i = 0; i < cPaths; i++)
        {
            const char *pcszPath = enmFmt == DNDTRANSFERLISTFMT_NATIVE
                                 ? papcszPaths[i] : papszPathsTmp[i];
            rc = DnDTransferListAppendPath(pList, DNDTRANSFERLISTFMT_NATIVE, pcszPath, fFlags);
            if (RT_FAILURE(rc))
            {
                LogRel(("DnD: Adding path '%s' (format %#x, root '%s') to transfer list failed with %Rrc\n",
                        pcszPath, enmFmt, pList->pszPathRootAbs ? pList->pszPathRootAbs : "<None>", rc));
                break;
            }
        }
    }

    if (papszPathsTmp)
    {
        for (size_t i = 0; i < cPaths; i++)
            RTStrFree(papszPathsTmp[i]);
        RTMemFree(papszPathsTmp);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Appends the root entries for a transfer list.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to append to.
 * @param   enmFmt              Format of \a pszPaths to append.
 * @param   pszPaths            Buffer of paths to append.
 * @param   cbPaths             Size (in bytes) of buffer of paths to append.
 * @param   pcszSeparator       Separator string to use.
 * @param   fFlags              Transfer list flags to use for appending.
 */
int DnDTransferListAppendRootsFromBuffer(PDNDTRANSFERLIST pList,
                                         DNDTRANSFERLISTFMT enmFmt, const char *pszPaths, size_t cbPaths,
                                         const char *pcszSeparator, DNDTRANSFERLISTFLAGS fFlags)
{
    AssertPtrReturn(pList, VERR_INVALID_POINTER);
    AssertPtrReturn(pszPaths, VERR_INVALID_POINTER);
    AssertReturn(cbPaths, VERR_INVALID_PARAMETER);

    char **papszPaths = NULL;
    size_t cPaths = 0;
    int rc = RTStrSplit(pszPaths, cbPaths, pcszSeparator, &papszPaths, &cPaths);
    if (RT_SUCCESS(rc))
        rc = DnDTransferListAppendRootsFromArray(pList, enmFmt, papszPaths, cPaths, fFlags);

    for (size_t i = 0; i < cPaths; ++i)
        RTStrFree(papszPaths[i]);
    RTMemFree(papszPaths);

    return rc;
}

/**
 * Appends root entries to a transfer list.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to append root entries to.
 * @param   enmFmt              Format of \a papcszPaths to append.
 * @param   papcszPaths         Array of paths to append.
 * @param   cPaths              Number of paths in \a papcszPaths to append.
 * @param   fFlags              Transfer list flags to use for appending.
 */
int DnDTransferListAppendRootsFromArray(PDNDTRANSFERLIST pList,
                                        DNDTRANSFERLISTFMT enmFmt,
                                        const char * const *papcszPaths, size_t cPaths, DNDTRANSFERLISTFLAGS fFlags)
{
    AssertPtrReturn(pList, VERR_INVALID_POINTER);
    AssertPtrReturn(papcszPaths, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~DNDTRANSFERLIST_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);

    AssertMsgReturn(pList->pszPathRootAbs, ("Root path not set yet\n"), VERR_WRONG_ORDER);

    int rc = VINF_SUCCESS;

    if (!cPaths) /* Nothing to add? Bail out. */
        return VINF_SUCCESS;

    char **papszPathsTmp = NULL;

    /* If URI data is being handed in, extract the paths first. */
    if (enmFmt == DNDTRANSFERLISTFMT_URI)
    {
        papszPathsTmp = (char **)RTMemAlloc(sizeof(char *) * cPaths);
        if (papszPathsTmp)
        {
            for (size_t i = 0; i < cPaths; i++)
                papszPathsTmp[i] = RTUriFilePath(papcszPaths[i]);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    if (RT_FAILURE(rc))
        return rc;

    char szPath[RTPATH_MAX];

    /*
     * Add all root entries to the root list.
     */
    for (size_t i = 0; i < cPaths; i++)
    {
        const char *pcszPath = enmFmt == DNDTRANSFERLISTFMT_NATIVE
                             ? papcszPaths[i] : papszPathsTmp[i];

        rc = RTPathJoin(szPath, sizeof(szPath), pList->pszPathRootAbs, pcszPath);
        AssertRCBreak(rc);

        rc = DnDPathConvert(szPath, sizeof(szPath), DNDPATHCONVERT_FLAGS_TRANSPORT);
        AssertRCBreak(rc);

        rc = dndTransferListRootEntryAdd(pList, szPath);
        if (RT_FAILURE(rc))
        {
            LogRel(("DnD: Adding root entry '%s' (format %#x, root '%s') to transfer list failed with %Rrc\n",
                    szPath, enmFmt, pList->pszPathRootAbs, rc));
            break;
        }
    }

    if (papszPathsTmp)
    {
        for (size_t i = 0; i < cPaths; i++)
            RTStrFree(papszPathsTmp[i]);
        RTMemFree(papszPathsTmp);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Returns the first transfer object in a list.
 *
 * @returns Pointer to transfer object if found, or NULL if not found.
 * @param   pList               Transfer list to get first transfer object from.
 */
PDNDTRANSFEROBJECT DnDTransferListObjGetFirst(PDNDTRANSFERLIST pList)
{
    AssertPtrReturn(pList, NULL);

    return RTListGetFirst(&pList->lstObj, DNDTRANSFEROBJECT, Node);
}

/**
 * Removes an object from a transfer list, internal version.
 *
 * @param   pList               Transfer list to remove object from.
 * @param   pObj                Object to remove. The object will be free'd and the pointer is invalid after calling.
 */
static void dndTransferListObjRemoveInternal(PDNDTRANSFERLIST pList, PDNDTRANSFEROBJECT pObj)
{
    AssertPtrReturnVoid(pList);
    AssertPtrReturnVoid(pObj);

    /** @todo Validate if \a pObj is part of \a pList. */

    uint64_t cbSize = DnDTransferObjectGetSize(pObj);
    Assert(pList->cbObjTotal >= cbSize);
    pList->cbObjTotal -= cbSize; /* Adjust total size. */

    dndTransferListObjFree(pList, pObj);
}

/**
 * Removes an object from a transfer list.
 *
 * @param   pList               Transfer list to remove object from.
 * @param   pObj                Object to remove. The object will be free'd and the pointer is invalid after calling.
 */
void DnDTransferListObjRemove(PDNDTRANSFERLIST pList, PDNDTRANSFEROBJECT pObj)
{
    return dndTransferListObjRemoveInternal(pList, pObj);
}

/**
 * Removes the first DnD transfer object from a transfer list.
 *
 * @param   pList               Transfer list to remove first entry for.
 */
void DnDTransferListObjRemoveFirst(PDNDTRANSFERLIST pList)
{
    AssertPtrReturnVoid(pList);

    if (!pList->cObj)
        return;

    PDNDTRANSFEROBJECT pObj = RTListGetFirst(&pList->lstObj, DNDTRANSFEROBJECT, Node);
    AssertPtr(pObj);

    dndTransferListObjRemoveInternal(pList, pObj);
}

/**
 * Returns all root entries of a transfer list as a string.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to return root paths for.
 * @param   pcszPathBase        Root path to use as a base path. If NULL, the list's absolute root path will be used (if any).
 * @param   pcszSeparator       Separator to use for separating the root entries.
 * @param   ppszBuffer          Where to return the allocated string on success. Needs to be free'd with RTStrFree().
 * @param   pcbBuffer           Where to return the size (in bytes) of the allocated string on success, including terminator.
 */
int DnDTransferListGetRootsEx(PDNDTRANSFERLIST pList,
                              DNDTRANSFERLISTFMT enmFmt, const char *pcszPathBase, const char *pcszSeparator,
                              char **ppszBuffer, size_t *pcbBuffer)
{
    AssertPtrReturn(pList, VERR_INVALID_POINTER);
    /* pcszPathBase can be NULL. */
    AssertPtrReturn(pcszSeparator, VERR_INVALID_POINTER);
    AssertPtrReturn(ppszBuffer, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbBuffer, VERR_INVALID_POINTER);

    char  *pszString = NULL;
    size_t cchString = 0;

    const size_t cchSep = RTStrNLen(pcszSeparator, RTPATH_MAX);

    /* Find out which root path to use. */
    const char *pcszPathRootTmp = pcszPathBase ? pcszPathBase : pList->pszPathRootAbs;
    /* pcszPathRootTmp can be NULL */

    LogFlowFunc(("Using root path '%s'\n", pcszPathRootTmp ? pcszPathRootTmp : "<None>"));

    int rc = DnDPathValidate(pcszPathRootTmp, false /* fMustExist */);
    if (RT_FAILURE(rc))
        return rc;

    char szPath[RTPATH_MAX];

    PDNDTRANSFERLISTROOT pRoot;
    RTListForEach(&pList->lstRoot, pRoot, DNDTRANSFERLISTROOT, Node)
    {
        if (pcszPathRootTmp)
        {
            rc = RTStrCopy(szPath, sizeof(szPath), pcszPathRootTmp);
            AssertRCBreak(rc);
        }

        rc = RTPathAppend(szPath, sizeof(szPath), pRoot->pszPathRoot);
        AssertRCBreak(rc);

        if (enmFmt == DNDTRANSFERLISTFMT_URI)
        {
            char *pszPathURI = RTUriFileCreate(szPath);
            AssertPtrBreakStmt(pszPathURI, rc = VERR_NO_MEMORY);
            rc = RTStrAAppend(&pszString, pszPathURI);
            cchString += RTStrNLen(pszPathURI, RTPATH_MAX);
            RTStrFree(pszPathURI);
            AssertRCBreak(rc);
        }
        else /* Native */
        {
#if RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS
            /* Convert paths to native path style. */
            rc = DnDPathConvert(szPath, sizeof(szPath), DNDPATHCONVERT_FLAGS_TO_DOS);
#endif
            if (RT_SUCCESS(rc))
            {
                rc = RTStrAAppend(&pszString, szPath);
                AssertRCBreak(rc);

                cchString += RTStrNLen(szPath, RTPATH_MAX);
            }
        }

        rc = RTStrAAppend(&pszString, pcszSeparator);
        AssertRCBreak(rc);

        cchString += cchSep;
    }

    if (RT_SUCCESS(rc))
    {
        *ppszBuffer = pszString;
        *pcbBuffer  = pszString ? cchString + 1 /* Include termination */ : 0;
    }
    else
        RTStrFree(pszString);
    return rc;
}

/**
 * Returns all root entries for a DnD transfer list.
 *
 * Note: Convenience function which uses the default DnD path separator.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to return root entries for.
 * @param   enmFmt              Which format to use for returning the entries.
 * @param   ppszBuffer          Where to return the allocated string on success. Needs to be free'd with RTStrFree().
 * @param   pcbBuffer           Where to return the size (in bytes) of the allocated string on success, including terminator.
 */
int DnDTransferListGetRoots(PDNDTRANSFERLIST pList,
                            DNDTRANSFERLISTFMT enmFmt, char **ppszBuffer, size_t *pcbBuffer)
{
    return DnDTransferListGetRootsEx(pList, enmFmt, "" /* pcszPathRoot */, DND_PATH_SEPARATOR_STR,
                                     ppszBuffer, pcbBuffer);
}

/**
 * Returns the total root entries count for a DnD transfer list.
 *
 * @returns Total number of root entries.
 * @param   pList               Transfer list to return total number of root entries for.
 */
uint64_t DnDTransferListGetRootCount(PDNDTRANSFERLIST pList)
{
    AssertPtrReturn(pList, 0);
    return pList->cRoots;
}

/**
 * Returns the absolute root path for a DnD transfer list.
 *
 * @returns Pointer to the root path.
 * @param   pList               Transfer list to return root path for.
 */
const char *DnDTransferListGetRootPathAbs(PDNDTRANSFERLIST pList)
{
    AssertPtrReturn(pList, NULL);
    return pList->pszPathRootAbs;
}

/**
 * Returns the total transfer object count for a DnD transfer list.
 *
 * @returns Total number of transfer objects.
 * @param   pList               Transfer list to return total number of transfer objects for.
 */
uint64_t DnDTransferListObjCount(PDNDTRANSFERLIST pList)
{
    AssertPtrReturn(pList, 0);
    return pList->cObj;
}

/**
 * Returns the total bytes of all handled transfer objects for a DnD transfer list.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to return total bytes for.
 */
uint64_t DnDTransferListObjTotalBytes(PDNDTRANSFERLIST pList)
{
    AssertPtrReturn(pList, 0);
    return pList->cbObjTotal;
}

/**
 * Sets the absolute root path of a transfer list.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to set root path for.
 * @param   pcszRootPathAbs     Absolute root path to set.
 */
static int dndTransferListSetRootPath(PDNDTRANSFERLIST pList, const char *pcszRootPathAbs)
{
    AssertPtrReturn(pList, VERR_INVALID_POINTER);
    AssertPtrReturn(pcszRootPathAbs, VERR_INVALID_POINTER);
    AssertReturn(pList->pszPathRootAbs == NULL, VERR_WRONG_ORDER); /* Already initialized? */

    LogFlowFunc(("pcszRootPathAbs=%s\n", pcszRootPathAbs));

    char szRootPath[RTPATH_MAX];
    int rc = RTStrCopy(szRootPath, sizeof(szRootPath), pcszRootPathAbs);
    if (RT_FAILURE(rc))
        return rc;

    /* Note: The list's root path is kept in native style, so no conversion needed here. */

    RTPathEnsureTrailingSeparatorEx(szRootPath, sizeof(szRootPath), RTPATH_STR_F_STYLE_HOST);

    /* Make sure the root path is a directory (and no symlink or stuff). */
    RTFSOBJINFO objInfo;
    rc = RTPathQueryInfo(szRootPath, &objInfo, RTFSOBJATTRADD_NOTHING);
    if (RT_SUCCESS(rc))
    {
        if (RTFS_IS_DIRECTORY(objInfo.Attr.fMode & RTFS_TYPE_MASK))
        {
            pList->pszPathRootAbs = RTStrDup(szRootPath);
            if (pList->pszPathRootAbs)
            {
                LogFlowFunc(("Root path is '%s'\n", pList->pszPathRootAbs));
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_NOT_A_DIRECTORY;
    }

    return rc;
}

/**
 * Adds a root entry to a DnD transfer list.
 *
 * @returns VBox status code.
 * @param   pList               Transfer list to add root entry to.
 * @param   pcszRoot            Root entry to add.
 */
static int dndTransferListRootEntryAdd(PDNDTRANSFERLIST pList, const char *pcszRoot)
{
    AssertPtrReturn(pList->pszPathRootAbs, VERR_WRONG_ORDER); /* The list's root path must be set first. */

    int rc;

    /** @todo Handle / reject double entries. */

    /* Get the index pointing to the relative path in relation to set the root path. */
    const size_t idxPathToAdd = strlen(pList->pszPathRootAbs);
    AssertReturn(strlen(pcszRoot) > idxPathToAdd, VERR_INVALID_PARAMETER); /* Should never happen (tm). */

    PDNDTRANSFERLISTROOT pRoot = (PDNDTRANSFERLISTROOT)RTMemAllocZ(sizeof(DNDTRANSFERLISTROOT));
    if (pRoot)
    {
        const char *pcszRootIdx = &pcszRoot[idxPathToAdd];

        LogFlowFunc(("pcszRoot=%s\n", pcszRootIdx));

        pRoot->pszPathRoot = RTStrDup(pcszRootIdx);
        if (pRoot->pszPathRoot)
        {
            RTListAppend(&pList->lstRoot, &pRoot->Node);
            pList->cRoots++;

            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_NO_MEMORY;

        if (RT_FAILURE(rc))
        {
            RTMemFree(pRoot);
            pRoot = NULL;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/**
 * Removes (and destroys) a DnD transfer root entry.
 *
 * @param   pList               Transfer list to free root for.
 * @param   pRootObj            Transfer list root to free. The pointer will be invalid after calling.
 */
static void dndTransferListRootEntryFree(PDNDTRANSFERLIST pList, PDNDTRANSFERLISTROOT pRootObj)
{
    if (!pRootObj)
        return;

    RTStrFree(pRootObj->pszPathRoot);

    RTListNodeRemove(&pRootObj->Node);
    RTMemFree(pRootObj);

    AssertReturnVoid(pList->cRoots);
    pList->cRoots--;
}

