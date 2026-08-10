/* $Id: clipboard-transfers-provider-local.cpp 114961 2026-08-10 15:01:12Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard - Transfers interface implementation for local file systems.
 */

/*
 * Copyright (C) 2023-2026 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <VBox/GuestHost/SharedClipboard-transfers.h>

#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/string.h>


/**
 * Adds a file to a transfer list header.
 *
 * @returns VBox status code.
 * @param   pHdr                List header to add file to.
 * @param   pszPath             Path of file to add.
 */
static int shclTransferLocalListHdrAddFile(PSHCLLISTHDR pHdr, const char *pszPath)
{
    AssertPtrReturn(pHdr, VERR_INVALID_POINTER);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);

    uint64_t cbSize = 0;
    int rc = RTFileQuerySizeByPath(pszPath, &cbSize);
    if (RT_SUCCESS(rc))
    {
        pHdr->cbTotalSize += cbSize;
        pHdr->cEntries++;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Builds a transfer list header, internal version.
 *
 * @returns VBox status code.
 * @param   pHdr                Where to store the build list header.
 * @param   pcszPathAbs         Absolute path to use for building the transfer list.
 */
static int shclTransferLocalListHdrFromDir(PSHCLLISTHDR pHdr, const char *pcszPathAbs)
{
    AssertPtrReturn(pcszPathAbs, VERR_INVALID_POINTER);

    LogFlowFunc(("pcszPathAbs=%s\n", pcszPathAbs));

    RTFSOBJINFO objInfo;
    int rc = RTPathQueryInfoEx(pcszPathAbs, &objInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    if (RT_SUCCESS(rc))
    {
        if (RTFS_IS_DIRECTORY(objInfo.Attr.fMode))
        {
            RTDIR hDir;
            rc = RTDirOpenFiltered(&hDir, pcszPathAbs, RTDIRFILTER_NONE, RTDIR_F_NO_FOLLOW);
            if (RT_SUCCESS(rc))
            {
                size_t        cbDirEntry = 0;
                PRTDIRENTRYEX pDirEntry  = NULL;
                do
                {
                    /* Retrieve the next directory entry. */
                    rc = RTDirReadExA(hDir, &pDirEntry, &cbDirEntry, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
                    if (RT_FAILURE(rc))
                    {
                        if (rc == VERR_NO_MORE_FILES)
                            rc = VINF_SUCCESS;
                        break;
                    }

                    switch (pDirEntry->Info.Attr.fMode & RTFS_TYPE_MASK)
                    {
                        case RTFS_TYPE_DIRECTORY:
                        {
                            /* Skip "." and ".." entries. */
                            if (RTDirEntryExIsStdDotLink(pDirEntry))
                                break;

                            pHdr->cEntries++;
                            break;
                        }
                        case RTFS_TYPE_FILE:
                        {
                            char *pszSrc = RTPathJoinA(pcszPathAbs, pDirEntry->szName);
                            if (pszSrc)
                            {
                                rc = shclTransferLocalListHdrAddFile(pHdr, pszSrc);
                                RTStrFree(pszSrc);
                            }
                            else
                                rc = VERR_NO_MEMORY;
                            break;
                        }
                        case RTFS_TYPE_SYMLINK:
                        {
                            /** @todo Not implemented yet. */
                            break;
                        }

                        default:
                            break;
                    }

                } while (RT_SUCCESS(rc));

                RTDirReadExAFree(&pDirEntry, &cbDirEntry);
                RTDirClose(hDir);
            }
        }
        else if (RTFS_IS_FILE(objInfo.Attr.fMode))
        {
            rc = shclTransferLocalListHdrAddFile(pHdr, pcszPathAbs);
        }
        else if (RTFS_IS_SYMLINK(objInfo.Attr.fMode))
            rc = VERR_IS_A_SYMLINK;
        else
            rc = VERR_NOT_SUPPORTED;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Creates a new list handle (local only).
 *
 * @returns New List handle on success, or NIL_SHCLLISTHANDLE on error.
 * @param   pTransfer           Clipboard transfer to create new list handle for.
 */
DECLINLINE(SHCLLISTHANDLE) shClTransferLocalListHandleNew(PSHCLTRANSFER pTransfer)
{
    return pTransfer->uListHandleNext++; /** @todo Good enough for now. Improve this later. */
}

/**
 * Checks all components in a local absolute path for symbolic links.
 *
 * @returns VBox status code.
 * @param   pszPathAbs          Absolute path to check.
 */
static int shClTransferLocalPathEnsureNoSymlinks(const char *pszPathAbs)
{
    AssertPtrReturn(pszPathAbs, VERR_INVALID_POINTER);

    union
    {
        RTPATHPARSED    Parsed;
        uint8_t         ab[RTPATH_MAX + sizeof(RTPATHPARSED)];
    } u;

    int rc = RTPathParse(pszPathAbs, &u.Parsed, sizeof(u), RTPATH_STR_F_STYLE_HOST);
    if (RT_FAILURE(rc))
        return rc;

    uint16_t const cComps = u.Parsed.cComps;
    uint16_t idxComp = RTPATH_PROP_HAS_ROOT_SPEC(u.Parsed.fProps) ? 2 : 1;
    if (idxComp > cComps)
        idxComp = cComps;

    for (; idxComp <= cComps; idxComp++)
    {
        u.Parsed.cComps = idxComp;

        char szPath[RTPATH_MAX];
        rc = RTPathParsedReassemble(pszPathAbs, &u.Parsed, RTPATH_STR_F_STYLE_HOST, szPath, sizeof(szPath));
        if (RT_FAILURE(rc))
            break;

        RTFSOBJINFO ObjInfo;
        rc = RTPathQueryInfoEx(szPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
        if (RT_FAILURE(rc))
            break;
        if (RTFS_IS_SYMLINK(ObjInfo.Attr.fMode))
        {
            LogRelMax2(16, ("Shared Clipboard: Path component '%s' is a symbolic link\n", szPath));
            rc = VERR_IS_A_SYMLINK;
            break;
        }
    }

    return rc;
}

/**
 * Queries information about a local list entry.
 *
 * @returns VBox status code.
 * @param   pszPathRootAbs      Absolute root path to use for the entry.
 * @param   pListEntry          List entry to query information for.
 * @param   pFsObjInfo          Where to store the queried information on success.
 */
static int shClTransferLocalListEntryQueryFsInfo(const char *pszPathRootAbs, PSHCLLISTENTRY pListEntry, PSHCLFSOBJINFO pFsObjInfo)
{
    AssertPtrReturn(pszPathRootAbs, VERR_INVALID_POINTER);
    AssertPtrReturn(pListEntry, VERR_INVALID_POINTER);

    char *pszSrcPathAbs = RTPathJoinA(pszPathRootAbs, pListEntry->pszName);
    AssertPtrReturn(pszSrcPathAbs, VERR_NO_MEMORY);

    int rc = shClTransferLocalPathEnsureNoSymlinks(pszSrcPathAbs);
    if (RT_SUCCESS(rc))
    {
        RTFSOBJINFO fsObjInfo;
        rc = RTPathQueryInfoEx(pszSrcPathAbs, &fsObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
        if (RT_SUCCESS(rc))
            rc = ShClFsObjInfoFromIPRT(pFsObjInfo, &fsObjInfo);
    }

    RTStrFree(pszSrcPathAbs);

    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnRootListRead */
static DECLCALLBACK(int) shclTransferIfaceLocalRootListRead(PSHCLTXPROVIDERCTX pCtx)
{
    LogFlowFuncEnter();

    int rc = VINF_SUCCESS;

    PSHCLLISTENTRY pEntry;
    RTListForEach(&pCtx->pTransfer->lstRoots.lstEntries, pEntry, SHCLLISTENTRY, Node)
    {
        AssertBreakStmt(pEntry->cbInfo == sizeof(SHCLFSOBJINFO), rc = VERR_WRONG_ORDER);
        rc = shClTransferLocalListEntryQueryFsInfo(pCtx->pTransfer->pszPathRootAbs, pEntry, (PSHCLFSOBJINFO)pEntry->pvInfo);
        if (RT_FAILURE(rc)) /* Currently this is an all-or-nothing op. */
            break;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnListOpen */
static DECLCALLBACK(int) shclTransferIfaceLocalListOpen(PSHCLTXPROVIDERCTX pCtx, PSHCLLISTOPENPARMS pOpenParms,
                                                        PSHCLLISTHANDLE phList)
{
    LogFlowFunc(("pszPath=%s\n", pOpenParms->pszPath));

    int rc;

    PSHCLTRANSFER const pTransfer = pCtx->pTransfer;
    AssertPtr(pTransfer);

    PSHCLLISTHANDLEINFO pInfo
        = (PSHCLLISTHANDLEINFO)RTMemAllocZ(sizeof(SHCLLISTHANDLEINFO));
    if (pInfo)
    {
        rc = ShClTransferListHandleInfoInit(pInfo);
        if (RT_SUCCESS(rc))
        {
            rc = ShClTransferResolvePathAbs(pTransfer, pOpenParms->pszPath, 0 /* fFlags */, &pInfo->pszPathLocalAbs);
            if (RT_SUCCESS(rc))
            {
                rc = shClTransferLocalPathEnsureNoSymlinks(pInfo->pszPathLocalAbs);
                if (RT_SUCCESS(rc))
                {
                    RTFSOBJINFO objInfo;
                    rc = RTPathQueryInfoEx(pInfo->pszPathLocalAbs, &objInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
                    if (RT_SUCCESS(rc))
                    {
                        if (RTFS_IS_DIRECTORY(objInfo.Attr.fMode))
                        {
                            rc = RTDirOpenFiltered(&pInfo->u.Local.hDir, pInfo->pszPathLocalAbs,
                                                   RTDIRFILTER_NONE, RTDIR_F_NO_FOLLOW);
                            if (RT_SUCCESS(rc))
                            {
                                pInfo->enmType = SHCLOBJTYPE_DIRECTORY;

                                LogRel2(("Shared Clipboard: Opening directory '%s'\n", pInfo->pszPathLocalAbs));
                            }
                            else
                                LogRel(("Shared Clipboard: Opening directory '%s' failed with %Rrc\n", pInfo->pszPathLocalAbs, rc));

                        }
                        else if (RTFS_IS_FILE(objInfo.Attr.fMode))
                        {
                            rc = RTFileOpen(&pInfo->u.Local.hFile, pInfo->pszPathLocalAbs,
                                            RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_WRITE | RTFILE_O_NO_SYMLINKS);
                            if (RT_SUCCESS(rc))
                            {
                                pInfo->enmType = SHCLOBJTYPE_FILE;

                                LogRel2(("Shared Clipboard: Opening file '%s'\n", pInfo->pszPathLocalAbs));
                            }
                            else
                                LogRel(("Shared Clipboard: Opening file '%s' failed with %Rrc\n", pInfo->pszPathLocalAbs, rc));
                        }
                        else if (RTFS_IS_SYMLINK(objInfo.Attr.fMode))
                            rc = VERR_IS_A_SYMLINK;
                        else
                            rc = VERR_NOT_SUPPORTED;

                        if (RT_SUCCESS(rc))
                        {
                            pInfo->hList = shClTransferLocalListHandleNew(pTransfer);

                            RTListAppend(&pTransfer->lstHandles, &pInfo->Node);
                            pTransfer->cListHandles++;

                            if (phList)
                                *phList = pInfo->hList;

                            LogFlowFunc(("pszPathLocalAbs=%s, hList=%RU64, cListHandles=%RU32\n",
                                         pInfo->pszPathLocalAbs, pInfo->hList, pTransfer->cListHandles));
                        }
                    }
                }
            }
        }

        if (RT_FAILURE(rc))
        {
            ShClTransferListHandleInfoDestroy(pInfo);

            RTMemFree(pInfo);
            pInfo = NULL;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnListClose */
static DECLCALLBACK(int) shclTransferIfaceLocalListClose(PSHCLTXPROVIDERCTX pCtx, SHCLLISTHANDLE hList)
{
    LogFlowFuncEnter();

    int rc = VINF_SUCCESS;

    PSHCLTRANSFER const pTransfer = pCtx->pTransfer;
    AssertPtr(pTransfer);

    PSHCLLISTHANDLEINFO pInfo = ShClTransferListGetByHandle(pTransfer, hList);
    if (pInfo)
    {
        switch (pInfo->enmType)
        {
            case SHCLOBJTYPE_DIRECTORY:
            case SHCLOBJTYPE_FILE:
                break;

            default:
                rc = VERR_NOT_SUPPORTED;
                break;
        }

        RTListNodeRemove(&pInfo->Node);

        Assert(pTransfer->cListHandles);
        pTransfer->cListHandles--;

        ShClTransferListHandleInfoDestroy(pInfo);
        RTMemFree(pInfo);
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnListHdrRead */
static DECLCALLBACK(int) shclTransferIfaceLocalListHdrRead(PSHCLTXPROVIDERCTX pCtx,
                                                           SHCLLISTHANDLE hList, PSHCLLISTHDR pListHdr)
{
    LogFlowFuncEnter();

    PSHCLTRANSFER const pTransfer = pCtx->pTransfer;
    AssertPtr(pTransfer);

    int rc;

    PSHCLLISTHANDLEINFO pInfo = ShClTransferListGetByHandle(pTransfer, hList);
    if (pInfo)
    {
        rc = ShClTransferListHdrInit(pListHdr);
        if (RT_SUCCESS(rc))
        {
            switch (pInfo->enmType)
            {
                case SHCLOBJTYPE_DIRECTORY:
                {
                    LogFlowFunc(("DirAbs: %s\n", pInfo->pszPathLocalAbs));

                    rc = shclTransferLocalListHdrFromDir(pListHdr, pInfo->pszPathLocalAbs);
                    break;
                }

                case SHCLOBJTYPE_FILE:
                {
                    LogFlowFunc(("FileAbs: %s\n", pInfo->pszPathLocalAbs));

                    pListHdr->cEntries = 1;

                    RTFSOBJINFO objInfo;
                    rc = RTFileQueryInfo(pInfo->u.Local.hFile, &objInfo, RTFSOBJATTRADD_NOTHING);
                    if (RT_SUCCESS(rc))
                    {
                        pListHdr->cbTotalSize = objInfo.cbObject;
                    }
                    break;
                }

                /* We don't support symlinks (yet). */

                default:
                    rc = VERR_NOT_SUPPORTED;
                    break;
            }
        }

        LogFlowFunc(("cTotalObj=%RU64, cbTotalSize=%RU64\n", pListHdr->cEntries, pListHdr->cbTotalSize));
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Initializes a local list entry.
 *
 * @returns VBox status code.
 * @param   pEntry              List entry to init.
 * @param   pszName             File name to use.
 * @param   pObjInfo            Object information to use.
 */
static int shclTransferIfaceLocalListEntryInit(PSHCLLISTENTRY pEntry, const char *pszName, PRTFSOBJINFO pObjInfo)
{
    PSHCLFSOBJINFO pFsObjInfo = (PSHCLFSOBJINFO)RTMemAllocZ(sizeof(SHCLFSOBJINFO));
    AssertPtrReturn(pFsObjInfo, VERR_NO_MEMORY);

    int rc = ShClFsObjInfoFromIPRT(pFsObjInfo, pObjInfo);
    if (RT_SUCCESS(rc))
    {
        rc = ShClTransferListEntryInitEx(pEntry, VBOX_SHCL_INFO_F_FSOBJINFO, pszName, pFsObjInfo, sizeof(SHCLFSOBJINFO));
        /* pEntry has taken ownership of pFsObjInfo on success. */
    }

    if (RT_FAILURE(rc))
    {
        RTMemFree(pFsObjInfo);
        pFsObjInfo = NULL;
    }

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Initializing list entry '%s' failed: %Rrc\n", pszName, rc));

    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnListEntryRead */
static DECLCALLBACK(int) shclTransferIfaceLocalListEntryRead(PSHCLTXPROVIDERCTX pCtx,
                                                             SHCLLISTHANDLE hList, PSHCLLISTENTRY pEntry)
{
    LogFlowFuncEnter();

    int rc;

    PSHCLTRANSFER const pTransfer = pCtx->pTransfer;
    AssertPtr(pTransfer);

    PSHCLLISTHANDLEINFO pInfo = ShClTransferListGetByHandle(pTransfer, hList);
    if (pInfo)
    {
        switch (pInfo->enmType)
        {
            case SHCLOBJTYPE_DIRECTORY:
            {
                LogFlowFunc(("\tDirectory: %s\n", pInfo->pszPathLocalAbs));

                for (;;)
                {
                    bool fSkipEntry = false; /* Whether to skip an entry in the enumeration. */

                    size_t        cbDirEntry = 0;
                    PRTDIRENTRYEX pDirEntry  = NULL;
                    rc = RTDirReadExA(pInfo->u.Local.hDir, &pDirEntry, &cbDirEntry, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
                    if (RT_SUCCESS(rc))
                    {
                        switch (pDirEntry->Info.Attr.fMode & RTFS_TYPE_MASK)
                        {
                            case RTFS_TYPE_DIRECTORY:
                            {
                                /* Skip "." and ".." entries. */
                                if (RTDirEntryExIsStdDotLink(pDirEntry))
                                {
                                    fSkipEntry = true;
                                    break;
                                }

                                LogFlowFunc(("Directory: %s\n", pDirEntry->szName));
                                break;
                            }

                            case RTFS_TYPE_FILE:
                            {
                                LogFlowFunc(("File: %s\n", pDirEntry->szName));
                                break;
                            }

                            case RTFS_TYPE_SYMLINK:
                            {
                                fSkipEntry = true;
                                break;
                            }

                            default:
                                break;
                        }

                        if (   RT_SUCCESS(rc)
                            && !fSkipEntry)
                            rc = shclTransferIfaceLocalListEntryInit(pEntry, pDirEntry->szName, &pDirEntry->Info);

                        RTDirReadExAFree(&pDirEntry, &cbDirEntry);
                    }

                    if (   !fSkipEntry /* Do we have a valid entry? Bail out. */
                        || RT_FAILURE(rc))
                    {
                        break;
                    }
                }

                break;
            }

            case SHCLOBJTYPE_FILE:
            {
                LogFlowFunc(("\tSingle file: %s\n", pInfo->pszPathLocalAbs));

                RTFSOBJINFO objInfo;
                rc = RTFileQueryInfo(pInfo->u.Local.hFile, &objInfo, RTFSOBJATTRADD_NOTHING);
                if (RT_SUCCESS(rc))
                    rc = shclTransferIfaceLocalListEntryInit(pEntry, RTPathFilename(pInfo->pszPathLocalAbs), &objInfo);

                break;
            }

            default:
                rc = VERR_NOT_SUPPORTED;
                break;
        }
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnObjOpen */
static DECLCALLBACK(int) shclTransferIfaceLocalObjOpen(PSHCLTXPROVIDERCTX pCtx,
                                                       PSHCLOBJOPENCREATEPARMS pCreateParms, PSHCLOBJHANDLE phObj)
{
    LogFlowFuncEnter();

    int rc;

    PSHCLTRANSFER const pTransfer = pCtx->pTransfer;
    AssertPtr(pTransfer);

    PSHCLTRANSFEROBJ pInfo = (PSHCLTRANSFEROBJ)RTMemAllocZ(sizeof(SHCLTRANSFEROBJ));
    if (pInfo)
    {
        rc = ShClTransferObjInit(pInfo);
        if (RT_SUCCESS(rc))
        {
            uint64_t fOpen = 0; /* Shut up GCC. */
            rc = ShClTransferConvertFileCreateFlags(pCreateParms->fCreate, &fOpen);
            if (RT_SUCCESS(rc))
            {
                rc = ShClTransferResolvePathAbs(pTransfer, pCreateParms->pszPath, 0 /* fFlags */,
                                                &pInfo->pszPathLocalAbs);
                if (RT_SUCCESS(rc))
                {
                    rc = shClTransferLocalPathEnsureNoSymlinks(pInfo->pszPathLocalAbs);
                    if (RT_SUCCESS(rc))
                    {
                        RTFSOBJINFO objInfo;
                        rc = RTPathQueryInfoEx(pInfo->pszPathLocalAbs, &objInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
                        if (RT_SUCCESS(rc))
                        {
                            if (RTFS_IS_SYMLINK(objInfo.Attr.fMode))
                                rc = VERR_IS_A_SYMLINK;
                            else if (!RTFS_IS_FILE(objInfo.Attr.fMode))
                                rc = VERR_NOT_A_FILE;
                        }
                        if (RT_SUCCESS(rc))
                        {
                            rc = RTFileOpen(&pInfo->u.Local.hFile, pInfo->pszPathLocalAbs, fOpen | RTFILE_O_NO_SYMLINKS);
                            if (RT_SUCCESS(rc))
                            {
                                rc = ShClFsObjInfoFromIPRT(&pCreateParms->ObjInfo, &objInfo);
                                if (RT_SUCCESS(rc))
                                    LogRel2(("Shared Clipboard: Opened file '%s'\n", pInfo->pszPathLocalAbs));
                                else
                                {
                                    RTFileClose(pInfo->u.Local.hFile);
                                    pInfo->u.Local.hFile = NIL_RTFILE;
                                }
                            }
                            else
                                LogRel(("Shared Clipboard: Error opening file '%s': rc=%Rrc\n", pInfo->pszPathLocalAbs, rc));
                        }
                    }
                }
            }
        }

        if (RT_SUCCESS(rc))
        {
            pInfo->hObj    = pTransfer->uObjHandleNext++;
            pInfo->enmType = SHCLOBJTYPE_FILE;

            RTListAppend(&pTransfer->lstObj, &pInfo->Node);
            pTransfer->cObjHandles++;

            LogFlowFunc(("cObjHandles=%RU32\n", pTransfer->cObjHandles));

            *phObj = pInfo->hObj;
        }
        else
        {
            ShClTransferObjDestroy(pInfo);
            RTMemFree(pInfo);
        }
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnObjClose */
static DECLCALLBACK(int) shclTransferIfaceLocalObjClose(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj)
{
    LogFlowFuncEnter();

    int rc;

    PSHCLTRANSFER const pTransfer = pCtx->pTransfer;
    AssertPtr(pTransfer);

    PSHCLTRANSFEROBJ pInfo = ShClTransferObjGet(pTransfer, hObj);
    if (pInfo)
    {
        switch (pInfo->enmType)
        {
            case SHCLOBJTYPE_DIRECTORY:
            {
                rc = RTDirClose(pInfo->u.Local.hDir);
                if (RT_SUCCESS(rc))
                {
                    pInfo->u.Local.hDir = NIL_RTDIR;

                    LogRel2(("Shared Clipboard: Closed directory '%s'\n", pInfo->pszPathLocalAbs));
                }
                else
                    LogRel(("Shared Clipboard: Closing directory '%s' failed with %Rrc\n", pInfo->pszPathLocalAbs, rc));
                break;
            }

            case SHCLOBJTYPE_FILE:
            {
                rc = RTFileClose(pInfo->u.Local.hFile);
                if (RT_SUCCESS(rc))
                {
                    pInfo->u.Local.hFile = NIL_RTFILE;

                    LogRel2(("Shared Clipboard: Closed file '%s'\n", pInfo->pszPathLocalAbs));
                }
                else
                    LogRel(("Shared Clipboard: Closing file '%s' failed with %Rrc\n", pInfo->pszPathLocalAbs, rc));
                break;
            }

            default:
                rc = VERR_NOT_IMPLEMENTED;
                break;
        }

        RTListNodeRemove(&pInfo->Node);

        Assert(pTransfer->cObjHandles);
        pTransfer->cObjHandles--;

        ShClTransferObjDestroy(pInfo);

        RTMemFree(pInfo);
        pInfo = NULL;
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnObjRead */
static DECLCALLBACK(int) shclTransferIfaceLocalObjRead(PSHCLTXPROVIDERCTX pCtx,
                                                       SHCLOBJHANDLE hObj, void *pvData, uint32_t cbData,
                                                       uint32_t fFlags, uint32_t *pcbRead)
{
    RT_NOREF(fFlags);

    LogFlowFuncEnter();

    int rc;

    PSHCLTRANSFER const pTransfer = pCtx->pTransfer;
    AssertPtr(pTransfer);

    PSHCLTRANSFEROBJ pInfo = ShClTransferObjGet(pTransfer, hObj);
    if (pInfo)
    {
        switch (pInfo->enmType)
        {
            case SHCLOBJTYPE_FILE:
            {
                size_t cbRead;
                rc = RTFileRead(pInfo->u.Local.hFile, pvData, cbData, &cbRead);
                if (RT_SUCCESS(rc))
                {
                    if (pcbRead)
                        *pcbRead = (uint32_t)cbRead;
                }
                break;
            }

            default:
                rc = VERR_NOT_SUPPORTED;
                break;
        }
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/** @copydoc SHCLTXPROVIDERIFACE::pfnObjWrite */
static DECLCALLBACK(int) shclTransferIfaceLocalObjWrite(PSHCLTXPROVIDERCTX pCtx,
                                                        SHCLOBJHANDLE hObj, void *pvData, uint32_t cbData, uint32_t fFlags,
                                                        uint32_t *pcbWritten)
{
    RT_NOREF(fFlags);

    LogFlowFuncEnter();

    int rc;

    PSHCLTRANSFER const pTransfer = pCtx->pTransfer;
    AssertPtr(pTransfer);

    PSHCLTRANSFEROBJ pInfo = ShClTransferObjGet(pTransfer, hObj);
    if (pInfo)
    {
        switch (pInfo->enmType)
        {
            case SHCLOBJTYPE_FILE:
            {
                size_t cbWritten = 0;
                rc = RTFileWrite(pInfo->u.Local.hFile, pvData, cbData, &cbWritten);
                if (RT_SUCCESS(rc) && pcbWritten)
                    *pcbWritten = (uint32_t)cbWritten;
                break;
            }

            default:
                rc = VERR_NOT_SUPPORTED;
                break;
        }
    }
    else
        rc = VERR_NOT_FOUND;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Queries (assigns) the local provider to an interface.
 *
 * The local provider is being used for accessing files on local file systems.
 *
 * @returns Interface pointer assigned to the provider.
 * @param   pProvider           Provider to assign interface to.
 */
PSHCLTXPROVIDERIFACE ShClTransferProviderLocalQueryInterface(PSHCLTXPROVIDER pProvider)
{
    RT_ZERO(pProvider->Interface);

    pProvider->Interface.pfnRootListRead   = shclTransferIfaceLocalRootListRead;
    pProvider->Interface.pfnListOpen       = shclTransferIfaceLocalListOpen;
    pProvider->Interface.pfnListClose      = shclTransferIfaceLocalListClose;
    pProvider->Interface.pfnListHdrRead    = shclTransferIfaceLocalListHdrRead;
    pProvider->Interface.pfnListEntryRead  = shclTransferIfaceLocalListEntryRead;
    pProvider->Interface.pfnObjOpen        = shclTransferIfaceLocalObjOpen;
    pProvider->Interface.pfnObjClose       = shclTransferIfaceLocalObjClose;
    pProvider->Interface.pfnObjRead        = shclTransferIfaceLocalObjRead;
    pProvider->Interface.pfnObjWrite       = shclTransferIfaceLocalObjWrite;

    return &pProvider->Interface;
}
