/* $Id: VBoxServiceControlSession.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxServiceControlSession - Guest session handling. Also handles the spawned session processes.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/handle.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/poll.h>
#include <iprt/process.h>
#include <iprt/rand.h>
#include <iprt/system.h> /* For RTShutdown. */

#include "VBoxServiceInternal.h"
#include "VBoxServiceUtils.h"
#include "VBoxServiceControl.h"

using namespace guestControl;


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Generic option indices for session spawn arguments. */
enum
{
    VBOXSERVICESESSIONOPT_FIRST = 1000, /* For initialization. */
    VBOXSERVICESESSIONOPT_DOMAIN,
#ifdef DEBUG
    VBOXSERVICESESSIONOPT_DUMP_STDOUT,
    VBOXSERVICESESSIONOPT_DUMP_STDERR,
#endif
    VBOXSERVICESESSIONOPT_LOG_FILE,
    VBOXSERVICESESSIONOPT_USERNAME,
    VBOXSERVICESESSIONOPT_SESSION_ID,
    VBOXSERVICESESSIONOPT_SESSION_PROTO,
    VBOXSERVICESESSIONOPT_THREAD_ID
};


static int vgsvcGstCtrlSessionCleanupProcesses(const PVBOXSERVICECTRLSESSION pSession);
static int vgsvcGstCtrlSessionProcessRemoveInternal(PVBOXSERVICECTRLSESSION pSession, PVBOXSERVICECTRLPROCESS pProcess);


/**
 * Helper that grows the scratch buffer.
 * @returns Success indicator.
 */
static bool vgsvcGstCtrlSessionGrowScratchBuf(void **ppvScratchBuf, uint32_t *pcbScratchBuf, uint32_t cbMinBuf)
{
    uint32_t cbNew = *pcbScratchBuf * 2;
    if (   cbNew    <= VMMDEV_MAX_HGCM_DATA_SIZE
        && cbMinBuf <= VMMDEV_MAX_HGCM_DATA_SIZE)
    {
        while (cbMinBuf > cbNew)
            cbNew *= 2;
        void *pvNew = RTMemRealloc(*ppvScratchBuf, cbNew);
        if (pvNew)
        {
            *ppvScratchBuf = pvNew;
            *pcbScratchBuf = cbNew;
            return true;
        }
    }
    return false;
}


#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
/**
 * Free's a guest directory entry.
 *
 * @returns VBox status code.
 * @param   pDir                Directory entry to free.
 *                              The pointer will be invalid on success.
 */
static int vgsvcGstCtrlSessionDirFree(PVBOXSERVICECTRLDIR pDir)
{
    if (!pDir)
        return VINF_SUCCESS;

    RTMemFree(pDir->pDirEntryEx);

    int rc;
    if (pDir->hDir != NIL_RTDIR)
    {
        rc = RTDirClose(pDir->hDir);
        pDir->hDir = NIL_RTDIR;
    }
    else
        rc = VINF_SUCCESS;

    if (RT_SUCCESS(rc))
    {
        RTStrFree(pDir->pszPathAbs);
        RTListNodeRemove(&pDir->Node);
        RTMemFree(pDir);
    }

    return rc;
}


/**
 * Acquires an internal guest directory.
 *
 * Must be released via vgsvcGstCtrlSessionDirRelease().
 *
 * @returns Guest directory entry on success, or NULL if not found.
 * @param   pSession            Guest control session to acquire guest directory for.
 * @param   uHandle             Handle of directory to acquire.
 *
 * @note    No locking done yet.
 */
static PVBOXSERVICECTRLDIR vgsvcGstCtrlSessionDirAcquire(const PVBOXSERVICECTRLSESSION pSession, uint32_t uHandle)
{
    AssertPtrReturn(pSession, NULL);

    /** @todo Use a map later! */
    PVBOXSERVICECTRLDIR pDirCur;
    RTListForEach(&pSession->lstDirs, pDirCur, VBOXSERVICECTRLDIR, Node)
    {
        if (pDirCur->uHandle == uHandle)
            return pDirCur;
    }

    return NULL;
}


/**
 * Releases a formerly acquired guest directory.
 *
 * @param   pDir                Directory to release.
 */
static void vgsvcGstCtrlSessionDirRelease(PVBOXSERVICECTRLDIR pDir)
{
    RT_NOREF(pDir);
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */


/**
 * Free's a guest file entry.
 *
 * @returns VBox status code.
 * @param   pFile               Guest file entry to free.
 *                              The pointer wil be invalid on success.
 */
static int vgsvcGstCtrlSessionFileFree(PVBOXSERVICECTRLFILE pFile)
{
    AssertPtrReturn(pFile, VERR_INVALID_POINTER);

    int rc = RTFileClose(pFile->hFile);
    if (RT_SUCCESS(rc))
    {
        RTStrFree(pFile->pszName);

        /* Remove file entry in any case. */
        RTListNodeRemove(&pFile->Node);
        /* Destroy this object. */
        RTMemFree(pFile);
    }

    return rc;
}


/**
 * Acquires an internal guest file entry.
 *
 * Must be released via vgsvcGstCtrlSessionFileRelease().
 *
 * @returns Guest file entry on success, or NULL if not found.
 * @param   pSession            Guest session to use.
 * @param   uHandle             Handle of guest file entry to acquire.
 *
 * @note    No locking done yet.
 */
static PVBOXSERVICECTRLFILE vgsvcGstCtrlSessionFileAcquire(const PVBOXSERVICECTRLSESSION pSession, uint32_t uHandle)
{
    AssertPtrReturn(pSession, NULL);

    /** @todo Use a map later! */
    PVBOXSERVICECTRLFILE pFileCur;
    RTListForEach(&pSession->lstFiles, pFileCur, VBOXSERVICECTRLFILE, Node)
    {
        if (pFileCur->uHandle == uHandle)
            return pFileCur;
    }

    return NULL;
}


/**
 * Releases a formerly acquired guest file.
 *
 * @param   pFile               File to release.
 */
static void vgsvcGstCtrlSessionFileRelease(PVBOXSERVICECTRLFILE pFile)
{
    RT_NOREF(pFile);
}


/**
 * Recursion worker for vgsvcGstCtrlSessionHandleDirRemove.
 * Only (recursively) removes directory structures which are not empty. Will fail if not empty.
 *
 * @returns IPRT status code.
 * @param   pszDir              The directory buffer, RTPATH_MAX in length.
 *                              Contains the abs path to the directory to
 *                              recurse into. Trailing slash.
 * @param   cchDir              The length of the directory we're recursing into,
 *                              including the trailing slash.
 * @param   pDirEntry           The dir entry buffer.  (Shared to save stack.)
 */
static int vgsvcGstCtrlSessionHandleDirRemoveSub(char *pszDir, size_t cchDir, PRTDIRENTRY pDirEntry)
{
    RTDIR hDir;
    int rc = RTDirOpen(&hDir, pszDir);
    if (RT_FAILURE(rc))
    {
        /* Ignore non-existing directories like RTDirRemoveRecursive does: */
        if (rc == VERR_FILE_NOT_FOUND || rc == VERR_PATH_NOT_FOUND)
            return VINF_SUCCESS;
        return rc;
    }

    for (;;)
    {
        rc = RTDirRead(hDir, pDirEntry, NULL);
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_NO_MORE_FILES)
                rc = VINF_SUCCESS;
            break;
        }

        if (!RTDirEntryIsStdDotLink(pDirEntry))
        {
            /* Construct the full name of the entry. */
            if (cchDir + pDirEntry->cbName + 1 /* dir slash */ < RTPATH_MAX)
                memcpy(&pszDir[cchDir], pDirEntry->szName, pDirEntry->cbName + 1);
            else
            {
                rc = VERR_FILENAME_TOO_LONG;
                break;
            }

            /* Make sure we've got the entry type. */
            if (pDirEntry->enmType == RTDIRENTRYTYPE_UNKNOWN)
                RTDirQueryUnknownType(pszDir, false /*fFollowSymlinks*/, &pDirEntry->enmType);

            /* Recurse into subdirs and remove them: */
            if (pDirEntry->enmType == RTDIRENTRYTYPE_DIRECTORY)
            {
                size_t cchSubDir    = cchDir + pDirEntry->cbName;
                pszDir[cchSubDir++] = RTPATH_SLASH;
                pszDir[cchSubDir]   = '\0';
                rc = vgsvcGstCtrlSessionHandleDirRemoveSub(pszDir, cchSubDir, pDirEntry);
                if (RT_SUCCESS(rc))
                {
                    pszDir[cchSubDir] = '\0';
                    rc = RTDirRemove(pszDir);
                    if (RT_FAILURE(rc))
                        break;
                }
                else
                    break;
            }
            /* Not a subdirectory - fail: */
            else
            {
                rc = VERR_DIR_NOT_EMPTY;
                break;
            }
        }
    }

    RTDirClose(hDir);
    return rc;
}


static int vgsvcGstCtrlSessionHandleDirRemove(PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the message.
     */
    char        szDir[RTPATH_MAX];
    uint32_t    fFlags; /* DIRREMOVE_FLAG_XXX */
    int rc = VbglR3GuestCtrlDirGetRemove(pHostCtx, szDir, sizeof(szDir), &fFlags);
    if (RT_SUCCESS(rc))
    {
        /*
         * Do some validating before executing the job.
         */
        if (!(fFlags & ~DIRREMOVEREC_FLAG_VALID_MASK))
        {
            if (fFlags & DIRREMOVEREC_FLAG_RECURSIVE)
            {
                if (fFlags & (DIRREMOVEREC_FLAG_CONTENT_AND_DIR | DIRREMOVEREC_FLAG_CONTENT_ONLY))
                {
                    uint32_t fFlagsRemRec = fFlags & DIRREMOVEREC_FLAG_CONTENT_AND_DIR
                                          ? RTDIRRMREC_F_CONTENT_AND_DIR : RTDIRRMREC_F_CONTENT_ONLY;
                    rc = RTDirRemoveRecursive(szDir, fFlagsRemRec);
                }
                else /* Only remove empty directory structures. Will fail if non-empty. */
                {
                    RTDIRENTRY DirEntry;
                    RTPathEnsureTrailingSeparator(szDir, sizeof(szDir));
                    rc = vgsvcGstCtrlSessionHandleDirRemoveSub(szDir, strlen(szDir), &DirEntry);
                }
                VGSvcVerbose(4, "[Dir %s]: rmdir /s (%#x) -> rc=%Rrc\n", szDir, fFlags, rc);
            }
            else
            {
                /* Only delete directory if not empty. */
                rc = RTDirRemove(szDir);
                VGSvcVerbose(4, "[Dir %s]: rmdir (%#x), rc=%Rrc\n", szDir, fFlags, rc);
            }
        }
        else
        {
            VGSvcError("[Dir %s]: Unsupported flags: %#x (all %#x)\n", szDir, (fFlags & ~DIRREMOVEREC_FLAG_VALID_MASK), fFlags);
            rc = VERR_NOT_SUPPORTED;
        }

        /*
         * Report result back to host.
         */
        int rc2 = VbglR3GuestCtrlMsgReply(pHostCtx, rc);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("[Dir %s]: Failed to report removing status, rc=%Rrc\n", szDir, rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for rmdir operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }

    VGSvcVerbose(6, "Removing directory '%s' returned rc=%Rrc\n", szDir, rc);
    return rc;
}


static int vgsvcGstCtrlSessionHandleFileOpen(PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the message.
     */
    char     szFile[RTPATH_MAX];
    char     szAccess[64];
    char     szDisposition[64];
    char     szSharing[64];
    uint32_t uCreationMode = 0;
    uint64_t offOpen       = 0;
    uint32_t uHandle       = 0;
    int rc = VbglR3GuestCtrlFileGetOpen(pHostCtx,
                                        /* File to open. */
                                        szFile, sizeof(szFile),
                                        /* Open mode. */
                                        szAccess, sizeof(szAccess),
                                        /* Disposition. */
                                        szDisposition, sizeof(szDisposition),
                                        /* Sharing. */
                                        szSharing, sizeof(szSharing),
                                        /* Creation mode. */
                                        &uCreationMode,
                                        /* Offset. */
                                        &offOpen);
    VGSvcVerbose(4, "[File %s]: szAccess=%s, szDisposition=%s, szSharing=%s, offOpen=%RU64, rc=%Rrc\n",
                 szFile, szAccess, szDisposition, szSharing, offOpen, rc);
    if (RT_SUCCESS(rc))
    {
        PVBOXSERVICECTRLFILE pFile = (PVBOXSERVICECTRLFILE)RTMemAllocZ(sizeof(VBOXSERVICECTRLFILE));
        if (pFile)
        {
            pFile->hFile = NIL_RTFILE; /* Not zero or NULL! */
            if (szFile[0])
            {
                pFile->pszName = RTStrDup(szFile);
                if (!pFile->pszName)
                    rc = VERR_NO_MEMORY;
/** @todo
 * Implement szSharing!
 */
                uint64_t fFlags;
                if (RT_SUCCESS(rc))
                {
                    rc = RTFileModeToFlagsEx(szAccess, szDisposition, NULL /* pszSharing, not used yet */, &fFlags);
                    VGSvcVerbose(4, "[File %s] Opening with fFlags=%#RX64 -> rc=%Rrc\n", pFile->pszName, fFlags, rc);
                }

                if (RT_SUCCESS(rc))
                {
                    fFlags |= (uCreationMode << RTFILE_O_CREATE_MODE_SHIFT) & RTFILE_O_CREATE_MODE_MASK;
                    /* If we're opening a file in read-only mode, strip truncation mode.
                     * rtFileRecalcAndValidateFlags() will validate it anyway, but avoid asserting in debug builds. */
                    if (fFlags & RTFILE_O_READ)
                        fFlags &= ~RTFILE_O_TRUNCATE;
                    rc = RTFileOpen(&pFile->hFile, pFile->pszName, fFlags);
                    if (RT_SUCCESS(rc))
                    {
                        RTFSOBJINFO objInfo;
                        rc = RTFileQueryInfo(pFile->hFile, &objInfo, RTFSOBJATTRADD_NOTHING);
                        if (RT_SUCCESS(rc))
                        {
                            /* Make sure that we only open stuff we really support.
                             * Only POSIX / UNIX we could open stuff like directories and sockets as well. */
                            if (   RT_LIKELY(RTFS_IS_FILE(objInfo.Attr.fMode))
                                ||           RTFS_IS_SYMLINK(objInfo.Attr.fMode))
                            {
                                /* Seeking is optional. However, the whole operation
                                 * will fail if we don't succeed seeking to the wanted position. */
                                if (offOpen)
                                    rc = RTFileSeek(pFile->hFile, (int64_t)offOpen, RTFILE_SEEK_BEGIN, NULL /* Current offset */);
                                if (RT_SUCCESS(rc))
                                {
                                    /*
                                     * Succeeded!
                                     */
                                    uHandle = VBOX_GUESTCTRL_CONTEXTID_GET_OBJECT(pHostCtx->uContextID);
                                    pFile->uHandle = uHandle;
                                    pFile->fOpen   = fFlags;
                                    RTListAppend(&pSession->lstFiles, &pFile->Node);
                                    VGSvcVerbose(2, "[File %s] Opened (ID=%RU32)\n", pFile->pszName, pFile->uHandle);
                                }
                                else
                                    VGSvcError("[File %s] Seeking to offset %RU64 failed: rc=%Rrc\n", pFile->pszName, offOpen, rc);
                            }
                            else
                            {
                                VGSvcError("[File %s] Unsupported mode %#x\n", pFile->pszName, objInfo.Attr.fMode);
                                rc = VERR_NOT_SUPPORTED;
                            }
                        }
                        else
                            VGSvcError("[File %s] Getting mode failed with rc=%Rrc\n", pFile->pszName, rc);
                    }
                    else
                        VGSvcError("[File %s] Opening failed with rc=%Rrc\n", pFile->pszName, rc);
                }
            }
            else
            {
                VGSvcError("Opening file failed: Empty filename!\n");
                rc = VERR_INVALID_NAME;
            }

            /* clean up if we failed. */
            if (RT_FAILURE(rc))
            {
                RTStrFree(pFile->pszName);
                if (pFile->hFile != NIL_RTFILE)
                    RTFileClose(pFile->hFile);
                RTMemFree(pFile);
            }
        }
        else
            rc = VERR_NO_MEMORY;

        /*
         * Report result back to host.
         */
        int rc2 = VbglR3GuestCtrlFileCbOpen(pHostCtx, rc, uHandle);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("[File %s]: Failed to report file open status, rc=%Rrc\n", szFile, rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for open file operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }

    VGSvcVerbose(4, "[File %s] Opening (open mode='%s', disposition='%s', creation mode=0x%x) returned rc=%Rrc\n",
                 szFile, szAccess, szDisposition, uCreationMode, rc);
    return rc;
}


static int vgsvcGstCtrlSessionHandleFileClose(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the message.
     */
    uint32_t uHandle = 0;
    int rc = VbglR3GuestCtrlFileGetClose(pHostCtx, &uHandle /* File handle to close */);
    if (RT_SUCCESS(rc))
    {
        PVBOXSERVICECTRLFILE pFile = vgsvcGstCtrlSessionFileAcquire(pSession, uHandle);
        if (pFile)
        {
            VGSvcVerbose(2, "[File %s] Closing (handle=%RU32)\n", pFile ? pFile->pszName : "<Not found>", uHandle);
            rc = vgsvcGstCtrlSessionFileFree(pFile);
        }
        else
        {
            VGSvcError("File %u (%#x) not found!\n", uHandle, uHandle);
            rc = VERR_NOT_FOUND;
        }

        /*
         * Report result back to host.
         */
        int rc2 = VbglR3GuestCtrlFileCbClose(pHostCtx, rc);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report file close status, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for close file operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


static int vgsvcGstCtrlSessionHandleFileRead(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx,
                                             void **ppvScratchBuf, uint32_t *pcbScratchBuf)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    uint32_t uHandle = 0;
    uint32_t cbToRead;
    int rc = VbglR3GuestCtrlFileGetRead(pHostCtx, &uHandle, &cbToRead);
    if (RT_SUCCESS(rc))
    {
        /*
         * Locate the file and do the reading.
         *
         * If the request is larger than our scratch buffer, try grow it - just
         * ignore failure as the host better respect our buffer limits.
         */
        uint32_t offNew = 0;
        size_t   cbRead = 0;
        PVBOXSERVICECTRLFILE pFile = vgsvcGstCtrlSessionFileAcquire(pSession, uHandle);
        if (pFile)
        {
            if (*pcbScratchBuf < cbToRead)
                 vgsvcGstCtrlSessionGrowScratchBuf(ppvScratchBuf, pcbScratchBuf, cbToRead);

            rc = RTFileRead(pFile->hFile, *ppvScratchBuf, RT_MIN(cbToRead, *pcbScratchBuf), &cbRead);
            offNew = (int64_t)RTFileTell(pFile->hFile);
            VGSvcVerbose(5, "[File %s] Read %zu/%RU32 bytes, rc=%Rrc, offNew=%RI64\n", pFile->pszName, cbRead, cbToRead, rc, offNew);
            vgsvcGstCtrlSessionFileRelease(pFile);
        }
        else
        {
            VGSvcError("File %u (%#x) not found!\n", uHandle, uHandle);
            rc = VERR_NOT_FOUND;
        }

        /*
         * Report result and data back to the host.
         */
        int rc2;
        if (g_fControlHostFeatures0 & VBOX_GUESTCTRL_HF_0_NOTIFY_RDWR_OFFSET)
            rc2 = VbglR3GuestCtrlFileCbReadOffset(pHostCtx, rc, *ppvScratchBuf, (uint32_t)cbRead, offNew);
        else
            rc2 = VbglR3GuestCtrlFileCbRead(pHostCtx, rc, *ppvScratchBuf, (uint32_t)cbRead);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report file read status, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for file read operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


static int vgsvcGstCtrlSessionHandleFileReadAt(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx,
                                               void **ppvScratchBuf, uint32_t *pcbScratchBuf)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    uint32_t uHandle = 0;
    uint32_t cbToRead;
    uint64_t offReadAt;
    int rc = VbglR3GuestCtrlFileGetReadAt(pHostCtx, &uHandle, &cbToRead, &offReadAt);
    if (RT_SUCCESS(rc))
    {
        /*
         * Locate the file and do the reading.
         *
         * If the request is larger than our scratch buffer, try grow it - just
         * ignore failure as the host better respect our buffer limits.
         */
        int64_t offNew = 0;
        size_t  cbRead = 0;
        PVBOXSERVICECTRLFILE pFile = vgsvcGstCtrlSessionFileAcquire(pSession, uHandle);
        if (pFile)
        {
            if (*pcbScratchBuf < cbToRead)
                 vgsvcGstCtrlSessionGrowScratchBuf(ppvScratchBuf, pcbScratchBuf, cbToRead);

            rc = RTFileReadAt(pFile->hFile, (RTFOFF)offReadAt, *ppvScratchBuf, RT_MIN(cbToRead, *pcbScratchBuf), &cbRead);
            if (RT_SUCCESS(rc))
            {
                offNew = offReadAt + cbRead;
                RTFileSeek(pFile->hFile, offNew, RTFILE_SEEK_BEGIN, NULL); /* RTFileReadAt does not always change position. */
            }
            else
                offNew = (int64_t)RTFileTell(pFile->hFile);
            VGSvcVerbose(5, "[File %s] Read %zu bytes @ %RU64, rc=%Rrc, offNew=%RI64\n", pFile->pszName, cbRead, offReadAt, rc, offNew);
            vgsvcGstCtrlSessionFileRelease(pFile);
        }
        else
        {
            VGSvcError("File %u (%#x) not found!\n", uHandle, uHandle);
            rc = VERR_NOT_FOUND;
        }

        /*
         * Report result and data back to the host.
         */
        int rc2;
        if (g_fControlHostFeatures0 & VBOX_GUESTCTRL_HF_0_NOTIFY_RDWR_OFFSET)
            rc2 = VbglR3GuestCtrlFileCbReadOffset(pHostCtx, rc, *ppvScratchBuf, (uint32_t)cbRead, offNew);
        else
            rc2 = VbglR3GuestCtrlFileCbRead(pHostCtx, rc, *ppvScratchBuf, (uint32_t)cbRead);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report file read at status, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for file read at operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


static int vgsvcGstCtrlSessionHandleFileWrite(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx,
                                              void **ppvScratchBuf, uint32_t *pcbScratchBuf)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request and data to write.
     */
    uint32_t uHandle = 0;
    uint32_t cbToWrite;
    int rc = VbglR3GuestCtrlFileGetWrite(pHostCtx, &uHandle, *ppvScratchBuf, *pcbScratchBuf, &cbToWrite);
    if (   rc == VERR_BUFFER_OVERFLOW
        && vgsvcGstCtrlSessionGrowScratchBuf(ppvScratchBuf, pcbScratchBuf, cbToWrite))
        rc = VbglR3GuestCtrlFileGetWrite(pHostCtx, &uHandle, *ppvScratchBuf, *pcbScratchBuf, &cbToWrite);
    if (RT_SUCCESS(rc))
    {
        /*
         * Locate the file and do the writing.
         */
        int64_t offNew    = 0;
        size_t  cbWritten = 0;
        PVBOXSERVICECTRLFILE pFile = vgsvcGstCtrlSessionFileAcquire(pSession, uHandle);
        if (pFile)
        {
            rc = RTFileWrite(pFile->hFile, *ppvScratchBuf, RT_MIN(cbToWrite, *pcbScratchBuf), &cbWritten);
            offNew = (int64_t)RTFileTell(pFile->hFile);
            VGSvcVerbose(5, "[File %s] Writing %p LB %RU32 =>  %Rrc, cbWritten=%zu, offNew=%RI64\n",
                         pFile->pszName, *ppvScratchBuf, RT_MIN(cbToWrite, *pcbScratchBuf), rc, cbWritten, offNew);
            vgsvcGstCtrlSessionFileRelease(pFile);
        }
        else
        {
            VGSvcError("File %u (%#x) not found!\n", uHandle, uHandle);
            rc = VERR_NOT_FOUND;
        }

        /*
         * Report result back to host.
         */
        int rc2;
        if (g_fControlHostFeatures0 & VBOX_GUESTCTRL_HF_0_NOTIFY_RDWR_OFFSET)
            rc2 = VbglR3GuestCtrlFileCbWriteOffset(pHostCtx, rc, (uint32_t)cbWritten, offNew);
        else
            rc2 = VbglR3GuestCtrlFileCbWrite(pHostCtx, rc, (uint32_t)cbWritten);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report file write status, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for file write operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


static int vgsvcGstCtrlSessionHandleFileWriteAt(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx,
                                                void **ppvScratchBuf, uint32_t *pcbScratchBuf)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request and data to write.
     */
    uint32_t uHandle = 0;
    uint32_t cbToWrite;
    uint64_t offWriteAt;
    int rc = VbglR3GuestCtrlFileGetWriteAt(pHostCtx, &uHandle, *ppvScratchBuf, *pcbScratchBuf, &cbToWrite, &offWriteAt);
    if (   rc == VERR_BUFFER_OVERFLOW
        && vgsvcGstCtrlSessionGrowScratchBuf(ppvScratchBuf, pcbScratchBuf, cbToWrite))
        rc = VbglR3GuestCtrlFileGetWriteAt(pHostCtx, &uHandle, *ppvScratchBuf, *pcbScratchBuf, &cbToWrite, &offWriteAt);
    if (RT_SUCCESS(rc))
    {
        /*
         * Locate the file and do the writing.
         */
        int64_t offNew    = 0;
        size_t  cbWritten = 0;
        PVBOXSERVICECTRLFILE pFile = vgsvcGstCtrlSessionFileAcquire(pSession, uHandle);
        if (pFile)
        {
            rc = RTFileWriteAt(pFile->hFile, (RTFOFF)offWriteAt, *ppvScratchBuf, RT_MIN(cbToWrite, *pcbScratchBuf), &cbWritten);
            if (RT_SUCCESS(rc))
            {
                offNew = offWriteAt + cbWritten;

                /* RTFileWriteAt does not always change position: */
                if (!(pFile->fOpen & RTFILE_O_APPEND))
                    RTFileSeek(pFile->hFile, offNew, RTFILE_SEEK_BEGIN, NULL);
                else
                    RTFileSeek(pFile->hFile, 0, RTFILE_SEEK_END, (uint64_t *)&offNew);
            }
            else
                offNew = (int64_t)RTFileTell(pFile->hFile);
            VGSvcVerbose(5, "[File %s] Writing %p LB %RU32 @ %RU64 =>  %Rrc, cbWritten=%zu, offNew=%RI64\n",
                         pFile->pszName, *ppvScratchBuf, RT_MIN(cbToWrite, *pcbScratchBuf), offWriteAt, rc, cbWritten, offNew);
            vgsvcGstCtrlSessionFileRelease(pFile);
        }
        else
        {
            VGSvcError("File %u (%#x) not found!\n", uHandle, uHandle);
            rc = VERR_NOT_FOUND;
        }

        /*
         * Report result back to host.
         */
        int rc2;
        if (g_fControlHostFeatures0 & VBOX_GUESTCTRL_HF_0_NOTIFY_RDWR_OFFSET)
            rc2 = VbglR3GuestCtrlFileCbWriteOffset(pHostCtx, rc, (uint32_t)cbWritten, offNew);
        else
            rc2 = VbglR3GuestCtrlFileCbWrite(pHostCtx, rc, (uint32_t)cbWritten);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report file write status, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for file write at operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


static int vgsvcGstCtrlSessionHandleFileSeek(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    uint32_t uHandle = 0;
    uint32_t uSeekMethod;
    uint64_t offSeek; /* Will be converted to int64_t. */
    int rc = VbglR3GuestCtrlFileGetSeek(pHostCtx, &uHandle, &uSeekMethod, &offSeek);
    if (RT_SUCCESS(rc))
    {
        uint64_t offActual = 0;

        /*
         * Validate and convert the seek method to IPRT speak.
         */
        static const uint8_t s_abMethods[GUEST_FILE_SEEKTYPE_END + 1] =
        {
            UINT8_MAX, RTFILE_SEEK_BEGIN, UINT8_MAX, UINT8_MAX, RTFILE_SEEK_CURRENT,
            UINT8_MAX, UINT8_MAX, UINT8_MAX, RTFILE_SEEK_END
        };
        if (   uSeekMethod < RT_ELEMENTS(s_abMethods)
            && s_abMethods[uSeekMethod] != UINT8_MAX)
        {
            /*
             * Locate the file and do the seek.
             */
            PVBOXSERVICECTRLFILE pFile = vgsvcGstCtrlSessionFileAcquire(pSession, uHandle);
            if (pFile)
            {
                rc = RTFileSeek(pFile->hFile, (int64_t)offSeek, s_abMethods[uSeekMethod], &offActual);
                VGSvcVerbose(5, "[File %s]: Seeking to offSeek=%RI64, uSeekMethodIPRT=%u, rc=%Rrc\n",
                             pFile->pszName, offSeek, s_abMethods[uSeekMethod], rc);
                vgsvcGstCtrlSessionFileRelease(pFile);
            }
            else
            {
                VGSvcError("File %u (%#x) not found!\n", uHandle, uHandle);
                rc = VERR_NOT_FOUND;
            }
        }
        else
        {
            VGSvcError("Invalid seek method: %#x\n", uSeekMethod);
            rc = VERR_NOT_SUPPORTED;
        }

        /*
         * Report result back to host.
         */
        int rc2 = VbglR3GuestCtrlFileCbSeek(pHostCtx, rc, offActual);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report file seek status, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for file seek operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


static int vgsvcGstCtrlSessionHandleFileTell(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    uint32_t uHandle = 0;
    int rc = VbglR3GuestCtrlFileGetTell(pHostCtx, &uHandle);
    if (RT_SUCCESS(rc))
    {
        /*
         * Locate the file and ask for the current position.
         */
        uint64_t offCurrent = 0;
        PVBOXSERVICECTRLFILE pFile = vgsvcGstCtrlSessionFileAcquire(pSession, uHandle);
        if (pFile)
        {
            offCurrent = RTFileTell(pFile->hFile);
            VGSvcVerbose(5, "[File %s]: Telling offCurrent=%RU64\n", pFile->pszName, offCurrent);
            vgsvcGstCtrlSessionFileRelease(pFile);
        }
        else
        {
            VGSvcError("File %u (%#x) not found!\n", uHandle, uHandle);
            rc = VERR_NOT_FOUND;
        }

        /*
         * Report result back to host.
         */
        int rc2 = VbglR3GuestCtrlFileCbTell(pHostCtx, rc, offCurrent);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report file tell status, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for file tell operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


static int vgsvcGstCtrlSessionHandleFileSetSize(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    uint32_t uHandle = 0;
    uint64_t cbNew = 0;
    int rc = VbglR3GuestCtrlFileGetSetSize(pHostCtx, &uHandle, &cbNew);
    if (RT_SUCCESS(rc))
    {
        /*
         * Locate the file and ask for the current position.
         */
        PVBOXSERVICECTRLFILE pFile = vgsvcGstCtrlSessionFileAcquire(pSession, uHandle);
        if (pFile)
        {
            rc = RTFileSetSize(pFile->hFile, cbNew);
            VGSvcVerbose(5, "[File %s]: Changing size to %RU64 (%#RX64), rc=%Rrc\n", pFile->pszName, cbNew, cbNew, rc);
            vgsvcGstCtrlSessionFileRelease(pFile);
        }
        else
        {
            VGSvcError("File %u (%#x) not found!\n", uHandle, uHandle);
            cbNew = UINT64_MAX;
            rc = VERR_NOT_FOUND;
        }

        /*
         * Report result back to host.
         */
        int rc2 = VbglR3GuestCtrlFileCbSetSize(pHostCtx, rc, cbNew);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report file tell status, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for file tell operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
static int vgsvcGstCtrlSessionHandleFileRemove(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    char szPath[RTPATH_MAX];
    int rc = VbglR3GuestCtrlFileGetRemove(pHostCtx, szPath, sizeof(szPath));
    if (RT_SUCCESS(rc))
    {
        VGSvcVerbose(4, "Deleting file szPath=%s\n", szPath);
        rc = RTFileDelete(szPath);

        /*
         * Report result back to host.
         */
        int rc2 = VbglR3GuestCtrlMsgReply(pHostCtx, rc);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report file deletion status, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for file deletion operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    VGSvcVerbose(5, "Deleting file returned rc=%Rrc\n", rc);
    return rc;
}


static int vgsvcGstCtrlSessionHandleDirOpen(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    char               szPath[RTPATH_MAX];
    uint32_t           fFlags;
    GSTCTLDIRFILTER    enmFilter;
    uint32_t           uHandle = 0;
    uint32_t           fReadFlags;
    GSTCTLFSOBJATTRADD enmAttrAdd;
    int rc = VbglR3GuestCtrlDirGetOpen(pHostCtx, szPath, sizeof(szPath), &fFlags, &enmFilter, &enmAttrAdd, &fReadFlags);
    VGSvcVerbose(4, "[Dir %s]: fFlags=%#x, enmFilter=%#x, rc=%Rrc\n", szPath, fFlags, enmFilter, rc);
    if (RT_SUCCESS(rc))
    {
        PVBOXSERVICECTRLDIR pDir = (PVBOXSERVICECTRLDIR)RTMemAllocZ(sizeof(VBOXSERVICECTRLDIR));
        AssertPtrReturn(pDir, VERR_NO_MEMORY);
        pDir->hDir = NIL_RTDIR; /* Not zero or NULL! */
        if (szPath[0])
        {
            pDir->pszPathAbs = RTStrDup(szPath);
            if (pDir->pszPathAbs)
            {
                /* Save reading parameters for subsequent directory entry read calls later. */
                pDir->fRead          = fReadFlags;
                pDir->enmReadAttrAdd = enmAttrAdd;

                pDir->cbDirEntryEx = GSTCTL_DIRENTRY_MAX_SIZE;
                pDir->pDirEntryEx  = (PRTDIRENTRYEX)RTMemAlloc(pDir->cbDirEntryEx);
                if (pDir->pDirEntryEx)
                {
                    rc = RTDirOpenFiltered(&pDir->hDir, pDir->pszPathAbs, (RTDIRFILTER)enmFilter, fFlags);
                    if (RT_SUCCESS(rc))
                    {
                        uHandle = VBOX_GUESTCTRL_CONTEXTID_GET_OBJECT(pHostCtx->uContextID);
                        pDir->uHandle = uHandle;
                        RTListAppend(&pSession->lstDirs, &pDir->Node);
                        VGSvcVerbose(2, "[Dir %s] Opened (ID=%RU32)\n", pDir->pszPathAbs, pDir->uHandle);
                    }
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
        {
            VGSvcError("Opening directory failed: Empty path!\n");
            rc = VERR_INVALID_NAME;
        }

        /* Clean up if we failed. */
        if (RT_FAILURE(rc))
        {
            RTStrFree(pDir->pszPathAbs);
            pDir->pszPathAbs = NULL;
            if (pDir->hDir != NIL_RTDIR)
                RTDirClose(pDir->hDir);
            RTMemFree(pDir);
            pDir = NULL;
        }

        /*
         * Report result back to host.
         */
        int rc2 = VbglR3GuestCtrlDirCbOpen(pHostCtx, rc, uHandle);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("[Dir %s]: Failed to report directory open status, rc=%Rrc\n", szPath, rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for directory open operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }

    VGSvcVerbose(4, "[Dir %s] Opening (flags=%#x, filter flags=%#x) returned rc=%Rrc\n",
                 szPath, fFlags, enmFilter, rc);
    return rc;
}


static int vgsvcGstCtrlSessionHandleDirClose(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the message.
     */
    uint32_t uHandle = 0;
    int rc = VbglR3GuestCtrlDirGetClose(pHostCtx, &uHandle /* Dir handle to close */);
    if (RT_SUCCESS(rc))
    {
        PVBOXSERVICECTRLDIR pDir = vgsvcGstCtrlSessionDirAcquire(pSession, uHandle);
        if (pDir)
        {
            VGSvcVerbose(2, "[Dir %s] Closing (handle=%RU32)\n", pDir ? pDir->pszPathAbs : "<Not found>", uHandle);
            rc = vgsvcGstCtrlSessionDirFree(pDir);
        }
        else
        {
            VGSvcError("Directory %u (%#x) not found!\n", uHandle, uHandle);
            rc = VERR_NOT_FOUND;
        }

        /*
         * Report result back to host.
         */
        int rc2 = VbglR3GuestCtrlDirCbClose(pHostCtx, rc);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report directory close status, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for directory close operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


/**
 * Worker function for reading a next guest directory entry.
 *
 * @returns VBox status code.
 * @param   pSession            Guest Control session to use.
 * @param   pDir                Guest directory to read the next entry for.
 * @param   ppDirEntryEx        Where to store the pointer to the next guest directory entry on success.
 * @param   pcbDirEntryEx       Where to return the size (in bytes) of \a ppGstCtlDirEntryEx on success.
 * @param   ppszUser            Where to return the resolved user name string.
 * @param   ppszGroups          Where to return the resolved groups string.
 */
static int vgsvcGstCtrlSessionDirReadNext(const PVBOXSERVICECTRLSESSION pSession,
                                          PVBOXSERVICECTRLDIR pDir,
                                          PGSTCTLDIRENTRYEX *ppDirEntryEx, size_t *pcbDirEntryEx,
                                          const char **ppszUser, const char **ppszGroups)
{
    PRTDIRENTRYEX pDirEntryEx = pDir->pDirEntryEx;

    size_t cbDirEntryEx = pDir->cbDirEntryEx;
    int rc = RTDirReadEx(pDir->hDir, pDir->pDirEntryEx, &cbDirEntryEx, (RTFSOBJATTRADD)pDir->enmReadAttrAdd, pDir->fRead);

    VGSvcVerbose(2, "[Dir %s] Read next entry '%s' -> %Rrc (%zu bytes)\n",
                 pDir->pszPathAbs, RT_SUCCESS(rc) ? pDirEntryEx->szName : "<None>", rc, cbDirEntryEx);

    if (RT_FAILURE(rc))
        return rc;

    /* Paranoia. */
    AssertReturn(cbDirEntryEx <= pDir->cbDirEntryEx, VERR_BUFFER_OVERFLOW);

    *ppszUser   = VGSvcIdCacheGetUidName(&pSession->UidCache,
                                         pDirEntryEx->Info.Attr.u.Unix.uid, pDirEntryEx->szName, pDir->pszPathAbs);
    *ppszGroups = VGSvcIdCacheGetGidName(&pSession->GidCache,
                                         pDirEntryEx->Info.Attr.u.Unix.gid, pDirEntryEx->szName, pDir->pszPathAbs);

    VGSvcVerbose(2, "[Dir %s] Entry '%s': %zu bytes, uid=%s (%d), gid=%s (%d)\n",
                 pDir->pszPathAbs, pDirEntryEx->szName, pDirEntryEx->Info.cbObject,
                 *ppszUser, pDirEntryEx->Info.Attr.u.UnixOwner.uid,
                 *ppszGroups, pDirEntryEx->Info.Attr.u.UnixGroup.gid);

    /*
     * For now we ASSUME that RTDIRENTRYEX == GSTCTLDIRENTRYEX, which implies that we simply can cast RTDIRENTRYEX
     * to GSTCTLDIRENTRYEX. This might change in the future, however, so be extra cautious here.
     *
     * Ditto for RTFSOBJATTRADD == GSTCTLFSOBJATTRADD.
     */
    AssertCompile(sizeof(GSTCTLDIRENTRYEX)              == sizeof(RTDIRENTRYEX));
    AssertCompile(RT_OFFSETOF(GSTCTLDIRENTRYEX, Info)   == RT_OFFSETOF(RTDIRENTRYEX, Info));
    AssertCompile(RT_OFFSETOF(GSTCTLDIRENTRYEX, cbName) == RT_OFFSETOF(RTDIRENTRYEX, cbName));
    AssertCompile(RT_OFFSETOF(GSTCTLDIRENTRYEX, szName) == RT_OFFSETOF(RTDIRENTRYEX, szName));
    AssertCompile(RT_OFFSETOF(GSTCTLFSOBJINFO,  Attr)   == RT_OFFSETOF(RTFSOBJINFO,  Attr));

    if (RT_SUCCESS(rc))
    {
        *ppDirEntryEx  = (PGSTCTLDIRENTRYEX)pDirEntryEx;
        *pcbDirEntryEx = cbDirEntryEx;
    }

    return rc;
}


static int vgsvcGstCtrlSessionHandleDirRead(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the message.
     */
    uint32_t uHandle;
    int rc = VbglR3GuestCtrlDirGetRead(pHostCtx, &uHandle);
    if (RT_SUCCESS(rc))
    {
        PVBOXSERVICECTRLDIR pDir = vgsvcGstCtrlSessionDirAcquire(pSession, uHandle);
        if (pDir)
        {
            PGSTCTLDIRENTRYEX pDirEntryEx;
            size_t            cbDirEntryEx;
            const char       *pszUser   = NULL;
            const char       *pszGroups = NULL;
            rc = vgsvcGstCtrlSessionDirReadNext(pSession, pDir,
                                                &pDirEntryEx, &cbDirEntryEx, &pszUser, &pszGroups);

            VGSvcVerbose(2, "[Dir %s] %Rrc\n", pDir->pszPathAbs, rc);

            int rc2;
            if (RT_SUCCESS(rc))
                rc2 = VbglR3GuestCtrlDirCbReadEx(pHostCtx, rc, pDirEntryEx, (uint32_t)cbDirEntryEx, pszUser, pszGroups);
            else
                rc2 = VbglR3GuestCtrlDirCbRead(pHostCtx, rc, NULL /* pEntry */, 0 /* cbSize */);
            if (RT_FAILURE(rc2))
                VGSvcError("Failed to report directory read status (%Rrc), rc=%Rrc\n", rc, rc2);

            VGSvcVerbose(2, "[Dir %s] 2 %Rrc\n", pDir->pszPathAbs, rc);

            if (rc == VERR_NO_MORE_FILES) /* Directory reading done. */
                rc = VINF_SUCCESS;

            vgsvcGstCtrlSessionDirRelease(pDir);
        }
        else
        {
            VGSvcError("Directory %u (%#x) not found!\n", uHandle, uHandle);
            rc = VERR_NOT_FOUND;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for directory read operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


static int vgsvcGstCtrlSessionHandleDirRewind(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the message.
     */
    uint32_t uHandle = 0;
    int rc = VbglR3GuestCtrlDirGetRewind(pHostCtx, &uHandle);
    if (RT_SUCCESS(rc))
    {
        PVBOXSERVICECTRLDIR pDir = vgsvcGstCtrlSessionDirAcquire(pSession, uHandle);
        if (pDir)
        {
            VGSvcVerbose(2, "[Dir %s] Rewinding (handle=%RU32)\n", pDir ? pDir->pszPathAbs : "<Not found>", uHandle);

            rc = RTDirRewind(pDir->hDir);

            vgsvcGstCtrlSessionDirRelease(pDir);
        }
        else
        {
            VGSvcError("Directory %u (%#x) not found!\n", uHandle, uHandle);
            rc = VERR_NOT_FOUND;
        }

        /*
         * Report result back to host.
         */
        int rc2 = VbglR3GuestCtrlDirCbRewind(pHostCtx, rc);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report directory rewind status, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for directory rewind operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


static int vgsvcGstCtrlSessionHandleDirCreate(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    char     szPath[RTPATH_MAX];
    RTFMODE  fMode;
    uint32_t fCreate;
    int rc = VbglR3GuestCtrlDirGetCreate(pHostCtx, szPath, sizeof(szPath), &fMode, &fCreate);
    if (RT_SUCCESS(rc))
    {
        if (!(fCreate & ~GSTCTL_CREATEDIRECTORY_F_VALID_MASK))
        {
            /* Translate flags. */
            int fCreateRuntime = 0; /* RTDIRCREATE_FLAGS_XXX */
            if (fCreate & GSTCTL_CREATEDIRECTORY_F_NO_SYMLINKS)
                fCreateRuntime |= RTDIRCREATE_FLAGS_NO_SYMLINKS;
            if (fCreate & GSTCTL_CREATEDIRECTORY_F_IGNORE_UMASK)
                fCreateRuntime |= RTDIRCREATE_FLAGS_IGNORE_UMASK;

            if (fCreate & GSTCTL_CREATEDIRECTORY_F_PARENTS)
                rc = RTDirCreateFullPath(szPath, fMode);
            else
                rc = RTDirCreate(szPath, fMode, fCreateRuntime);

            VGSvcVerbose(4, "Creating directory (szPath='%s', fMode=%#x, fCreate=%#x) -> rc=%Rrc\n", szPath, fMode, fCreate, rc);
        }
        else
        {
            VGSvcError("Invalid directory creation flags: %#x\n", fCreate);
            rc = VERR_NOT_SUPPORTED;
        }

        /*
         * Report result back to host.
         */
        int rc2 = VbglR3GuestCtrlMsgReply(pHostCtx, rc);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report directory creation status, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for directory creation operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    VGSvcVerbose(5, "Creating directory returned rc=%Rrc\n", rc);
    return rc;
}

static int vgsvcGstCtrlSessionHandleDirList(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    uint32_t uHandle;
    uint32_t cEntries;
    uint32_t cEntriesRead = 0;
    uint32_t fFlags;
    int rc = VbglR3GuestCtrlDirGetList(pHostCtx, &uHandle, &cEntries, &fFlags);
    if (RT_SUCCESS(rc))
    {
        size_t cbGrowSize = _64K;
        void  *pvBuf      = RTMemAlloc(cbGrowSize);
        if (!pvBuf)
            rc = VERR_NO_MEMORY;
        size_t cbBufUsed  = 0;
        if (RT_SUCCESS(rc))
        {
            if (!(fFlags & ~GSTCTL_DIRLIST_F_VALID_MASK))
            {
                size_t cbBufSize = cbGrowSize;

                PVBOXSERVICECTRLDIR pDir = vgsvcGstCtrlSessionDirAcquire(pSession, uHandle);
                if (pDir)
                {
                    PGSTCTLDIRENTRYEX pDirEntryEx;
                    size_t            cbDirEntryEx;
                    const char       *pszUser   = NULL;
                    const char       *pszGroups = NULL;

                    while (cEntries--)
                    {
                        rc = vgsvcGstCtrlSessionDirReadNext(pSession, pDir,
                                                            &pDirEntryEx, &cbDirEntryEx, &pszUser, &pszGroups);
                        if (RT_FAILURE(rc)) /* Might be VERR_NO_MORE_FILES. */
                            break;

                        size_t const cbHdr    = sizeof(GSTCTLDIRENTRYLISTHDR);
                        size_t const cbUser   = strlen(pszUser)   + 1; /* Include terminator. */
                        size_t const cbGroups = strlen(pszGroups) + 1; /* Ditto. */
                        size_t const cbTotal  = cbHdr + cbDirEntryEx + cbUser + cbGroups;

                        if (cbBufSize - cbBufUsed < cbTotal) /* Grow buffer, if needed. */
                        {
                            AssertBreakStmt(cbTotal <= cbGrowSize, rc = VERR_BUFFER_OVERFLOW);
                            pvBuf      = RTMemRealloc(pvBuf, cbBufSize + cbGrowSize);
                            AssertPtrBreakStmt(pvBuf, rc = VERR_NO_MEMORY);
                            cbBufSize += cbGrowSize;
                        }

                        GSTCTLDIRENTRYLISTHDR Hdr;
                        Hdr.cbDirEntryEx = (uint32_t)cbDirEntryEx;
                        Hdr.cbUser       = (uint32_t)cbUser;
                        Hdr.cbGroups     = (uint32_t)cbGroups;

                        memcpy((uint8_t *)pvBuf + cbBufUsed, &Hdr, cbHdr);
                        cbBufUsed += cbHdr;
                        memcpy((uint8_t *)pvBuf + cbBufUsed, pDirEntryEx, cbDirEntryEx);
                        cbBufUsed += cbDirEntryEx;
                        memcpy((uint8_t *)pvBuf + cbBufUsed, pszUser, cbUser);
                        cbBufUsed += cbUser;
                        memcpy((uint8_t *)pvBuf + cbBufUsed, pszGroups, cbGroups);
                        cbBufUsed += cbGroups;

                        Assert(cbBufUsed <= cbBufSize);
                        cEntriesRead++;
                    }

                    vgsvcGstCtrlSessionDirRelease(pDir);
                }

                VGSvcVerbose(4, "Read %RU32 directory entries (requested %RU32 entries) -> %zu bytes, rc=%Rrc\n",
                             cEntriesRead, cEntries, cbBufUsed, rc);

                /* Directory reading done? */
                if (   cEntriesRead
                    && rc == VERR_NO_MORE_FILES)
                    rc = VINF_SUCCESS;

                /* Note: Subsequent calls will return VERR_NO_MORE_FILES to the host. */
            }
            else
            {
                VGSvcError("Unsupported directory listing flags: %#x (all %#x)\n", (fFlags & ~GSTCTL_DIRLIST_F_VALID_MASK), fFlags);
                rc = VERR_NOT_SUPPORTED;
            }
        }

        /*
         * Report result back to host.
         */
        int rc2 = VbglR3GuestCtrlDirCbList(pHostCtx, rc, cEntriesRead, pvBuf, (uint32_t)cbBufUsed);
        if (RT_FAILURE(rc2))
            VGSvcError("Failed to report directory listing (%Rrc), rc=%Rrc\n", rc, rc2);

        if (rc == VERR_NO_MORE_FILES) /* Directory reading done? */
            rc = VINF_SUCCESS;

        RTMemFree(pvBuf);
    }
    else
    {
        VGSvcError("Error fetching parameters for directory listing operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    VGSvcVerbose(5, "Listing directory returned rc=%Rrc\n", rc);
    return rc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */


static int vgsvcGstCtrlSessionHandlePathRename(PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    char     szSource[RTPATH_MAX];
    char     szDest[RTPATH_MAX];
    uint32_t fFlags = 0; /* PATHRENAME_FLAG_XXX */
    int rc = VbglR3GuestCtrlPathGetRename(pHostCtx, szSource, sizeof(szSource), szDest, sizeof(szDest), &fFlags);
    if (RT_SUCCESS(rc))
    {
        /*
         * Validate the flags (kudos for using the same as IPRT), then do the renaming.
         */
        AssertCompile(PATHRENAME_FLAG_NO_REPLACE  == RTPATHRENAME_FLAGS_NO_REPLACE);
        AssertCompile(PATHRENAME_FLAG_REPLACE     == RTPATHRENAME_FLAGS_REPLACE);
        AssertCompile(PATHRENAME_FLAG_NO_SYMLINKS == RTPATHRENAME_FLAGS_NO_SYMLINKS);
        AssertCompile(PATHRENAME_FLAG_VALID_MASK  == (RTPATHRENAME_FLAGS_NO_REPLACE | RTPATHRENAME_FLAGS_REPLACE | RTPATHRENAME_FLAGS_NO_SYMLINKS));
        if (!(fFlags & ~PATHRENAME_FLAG_VALID_MASK))
        {
            VGSvcVerbose(4, "Renaming '%s' to '%s', fFlags=%#x, rc=%Rrc\n", szSource, szDest, fFlags, rc);
            rc = RTPathRename(szSource, szDest, fFlags);
        }
        else
        {
            VGSvcError("Invalid rename flags: %#x\n", fFlags);
            rc = VERR_NOT_SUPPORTED;
        }

        /*
         * Report result back to host.
         */
        int rc2 = VbglR3GuestCtrlMsgReply(pHostCtx, rc);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report renaming status, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for rename operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    VGSvcVerbose(5, "Renaming '%s' to '%s' returned rc=%Rrc\n", szSource, szDest, rc);
    return rc;
}


/**
 * Handles getting the user's documents directory.
 *
 * @returns VBox status code.
 * @param   pSession        Guest session.
 * @param   pHostCtx        Host context.
 */
static int vgsvcGstCtrlSessionHandlePathUserDocuments(PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    int rc = VbglR3GuestCtrlPathGetUserDocuments(pHostCtx);
    if (RT_SUCCESS(rc))
    {
        /*
         * Get the path and pass it back to the host..
         */
        char szPath[RTPATH_MAX];
        rc = RTPathUserDocuments(szPath, sizeof(szPath));
#ifdef DEBUG
        VGSvcVerbose(2, "User documents is '%s', rc=%Rrc\n", szPath, rc);
#endif

        int rc2 = VbglR3GuestCtrlMsgReplyEx(pHostCtx, rc, 0 /* Type */, szPath,
                                            RT_SUCCESS(rc) ? (uint32_t)strlen(szPath) + 1 /* Include terminating zero */ : 0);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report user documents, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for user documents path request: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


/**
 * Handles shutting down / rebooting the guest OS.
 *
 * @returns VBox status code.
 * @param   pSession        Guest session.
 * @param   pHostCtx        Host context.
 */
static int vgsvcGstCtrlSessionHandleShutdown(PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    uint32_t fAction;
    int rc = VbglR3GuestCtrlGetShutdown(pHostCtx, &fAction);
    if (RT_SUCCESS(rc))
    {
        VGSvcVerbose(1, "Host requested to %s system ...\n", (fAction & RTSYSTEM_SHUTDOWN_REBOOT) ? "reboot" : "shutdown");

        /* Reply first to the host, in order to avoid host hangs when issuing the guest shutdown. */
        rc = VbglR3GuestCtrlMsgReply(pHostCtx, VINF_SUCCESS);
        if (RT_FAILURE(rc))
        {
            VGSvcError("Failed to reply to shutdown / reboot request, rc=%Rrc\n", rc);
        }
        else
        {
            uint32_t fSystemShutdown = RTSYSTEM_SHUTDOWN_PLANNED;

            /* Translate SHUTDOWN_FLAG_ into RTSYSTEM_SHUTDOWN_ flags. */
            if (fAction & GUEST_SHUTDOWN_FLAG_REBOOT)
                fSystemShutdown |= RTSYSTEM_SHUTDOWN_REBOOT;
            else /* SHUTDOWN_FLAG_POWER_OFF */
                fSystemShutdown |= RTSYSTEM_SHUTDOWN_POWER_OFF;

            if (fAction & GUEST_SHUTDOWN_FLAG_FORCE)
                fSystemShutdown |= RTSYSTEM_SHUTDOWN_FORCE;

            rc = RTSystemShutdown(0 /*cMsDelay*/, fSystemShutdown, "VBoxService");
            if (RT_FAILURE(rc))
                VGSvcError("%s system failed with %Rrc\n",
                           (fAction & RTSYSTEM_SHUTDOWN_REBOOT) ? "Rebooting" : "Shutting down", rc);
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for shutdown / reboot request: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }

    return rc;
}


/**
 * Handles getting the user's home directory.
 *
 * @returns VBox status code.
 * @param   pSession        Guest session.
 * @param   pHostCtx        Host context.
 */
static int vgsvcGstCtrlSessionHandlePathUserHome(PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    int rc = VbglR3GuestCtrlPathGetUserHome(pHostCtx);
    if (RT_SUCCESS(rc))
    {
        /*
         * Get the path and pass it back to the host..
         */
        char szPath[RTPATH_MAX];
        rc = RTPathUserHome(szPath, sizeof(szPath));

#ifdef DEBUG
        VGSvcVerbose(2, "User home is '%s', rc=%Rrc\n", szPath, rc);
#endif
        /* Report back in any case. */
        int rc2 = VbglR3GuestCtrlMsgReplyEx(pHostCtx, rc, 0 /* Type */, szPath,
                                            RT_SUCCESS(rc) ?(uint32_t)strlen(szPath) + 1 /* Include terminating zero */ : 0);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report user home, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for user home directory path request: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}

/**
 * Structure for keeping a mount point enumeration context.
 */
typedef struct VGSVCMOUNTPOINTENUMCTX
{
    /** Mount points as strings, delimited with a string terminator ('\0').
     *  List ends with an additional string terminator. Might get re-allocated, so don't rely on this pointer! */
    char  *psz;
    /** Current size of \a psz in bytes. Includes ending terminator. */
    size_t cb;
    /** Total allocation Size of \a psz in bytes. */
    size_t cbAlloc;
} VGSVCMOUNTPOINTENUMCTX;
/** Pointer to a structure for keeping a mount point enumeration context. */
typedef VGSVCMOUNTPOINTENUMCTX *PVGSVCMOUNTPOINTENUMCTX;

/**
 * Enumeration callback for storing the mount points.
 *
 * @returns VBox status code.
 * @param   pszMountpoint       Mount point to handle.
 * @param   pvUser              Pointer of type PVGSVCMOUNTPOINTENUMCTX.
 */
static DECLCALLBACK(int) vgsvcGstCtrlSessionHandleMountPointsEnumCallback(const char *pszMountpoint, void *pvUser)
{
    PVGSVCMOUNTPOINTENUMCTX pCtx = (PVGSVCMOUNTPOINTENUMCTX)pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    size_t const cch = strlen(pszMountpoint) + 1 /* Entry terminator */;
    AssertReturn(cch < RTPATH_MAX, VERR_INVALID_PARAMETER); /* Paranoia. */
    if (cch > pCtx->cbAlloc - pCtx->cb - 1 /* Ending terminator */)
    {
        int rc2 = RTStrRealloc(&pCtx->psz, pCtx->cbAlloc + RT_MAX(_4K, cch));
        AssertRCReturn(rc2, rc2);
    }

    memcpy(&pCtx->psz[pCtx->cb], pszMountpoint, cch);
    pCtx->cb += cch;
    AssertReturn(pCtx->cb <= pCtx->cbAlloc, VERR_BUFFER_OVERFLOW); /* Paranoia. */

    return VINF_SUCCESS;
}

/**
 * Handles getting the current mount points.
 *
 * @returns VBox status code.
 * @param   pSession        Guest session.
 * @param   pHostCtx        Host context.
 */
static int vgsvcGstCtrlSessionHandleMountPoints(PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    uint32_t fFlags;
    int rc = VbglR3GuestCtrlGetMountPoints(pHostCtx, &fFlags);
    if (RT_SUCCESS(rc))
    {
        /* Note: fFlags is currently unused, so we simply ignore this here. */

        VGSVCMOUNTPOINTENUMCTX Ctx;
        Ctx.cb      = 0;
        Ctx.cbAlloc = _4K; /* Start with something sensible. */
        Ctx.psz     = RTStrAlloc(Ctx.cbAlloc);
        if (!Ctx.psz)
            rc = VERR_NO_MEMORY;

        if (RT_SUCCESS(rc))
            rc = RTFsMountpointsEnum(vgsvcGstCtrlSessionHandleMountPointsEnumCallback, &Ctx);

        /* Report back in any case. */
        int rc2 = VbglR3GuestCtrlMsgReplyEx(pHostCtx, rc, 0 /* Type */, Ctx.psz,
                                            RT_SUCCESS(rc) ? (uint32_t)Ctx.cb : 0);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report mount points, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }

        RTStrFree(Ctx.psz);
        Ctx.psz = NULL;
    }
    else
    {
        VGSvcError("Error fetching parameters for getting mount points request: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


/**
 * Handles starting a guest processes.
 *
 * @returns VBox status code.
 * @param   pSession        Guest session.
 * @param   pHostCtx        Host context.
 */
static int vgsvcGstCtrlSessionHandleProcExec(PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /* Initialize maximum environment block size -- needed as input
     * parameter to retrieve the stuff from the host. On output this then
     * will contain the actual block size. */
    PVBGLR3GUESTCTRLPROCSTARTUPINFO pStartupInfo;
    int rc = VbglR3GuestCtrlProcGetStart(pHostCtx, &pStartupInfo);
    if (RT_SUCCESS(rc))
    {
        VGSvcVerbose(3, "Request to start process szCmd=%s, fFlags=0x%x, szArgs=%s, szEnv=%s, szCwd=%s, uTimeout=%RU32\n",
                     pStartupInfo->pszCmd, pStartupInfo->fFlags,
                     pStartupInfo->cArgs    ? pStartupInfo->pszArgs : "<None>",
                     pStartupInfo->cEnvVars ? pStartupInfo->pszEnv  : "<None>",
                     pStartupInfo->cbCwd    ? pStartupInfo->pszCwd  : "<None>",
                     pStartupInfo->uTimeLimitMS);

        bool fStartAllowed = false; /* Flag indicating whether starting a process is allowed or not. */
        rc = VGSvcGstCtrlSessionProcessStartAllowed(pSession, &fStartAllowed);
        if (RT_SUCCESS(rc))
        {
            vgsvcGstCtrlSessionCleanupProcesses(pSession);

            if (fStartAllowed)
                rc = VGSvcGstCtrlProcessStart(pSession, pStartupInfo, pHostCtx->uContextID);
            else
                rc = VERR_MAX_PROCS_REACHED; /* Maximum number of processes reached. */
        }

        /* We're responsible for signaling errors to the host (it will wait for ever otherwise). */
        if (RT_FAILURE(rc))
        {
            VGSvcError("Starting process failed with rc=%Rrc, protocol=%RU32, parameters=%RU32\n",
                       rc, pHostCtx->uProtocol, pHostCtx->uNumParms);
            int rc2 = VbglR3GuestCtrlProcCbStatus(pHostCtx, 0 /*nil-PID*/, PROC_STS_ERROR, rc, NULL /*pvData*/, 0 /*cbData*/);
            if (RT_FAILURE(rc2))
                VGSvcError("Error sending start process status to host, rc=%Rrc\n", rc2);
        }

        VbglR3GuestCtrlProcStartupInfoFree(pStartupInfo);
        pStartupInfo = NULL;
    }
    else
    {
        VGSvcError("Failed to retrieve parameters for process start: %Rrc (cParms=%u)\n", rc, pHostCtx->uNumParms);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }

    return rc;
}


/**
 * Sends stdin input to a specific guest process.
 *
 * @returns VBox status code.
 * @param   pSession            The session which is in charge.
 * @param   pHostCtx            The host context to use.
 * @param   ppvScratchBuf       The scratch buffer, we may grow it.
 * @param   pcbScratchBuf       The scratch buffer size for retrieving the input
 *                              data.
 */
static int vgsvcGstCtrlSessionHandleProcInput(PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx,
                                              void **ppvScratchBuf, uint32_t *pcbScratchBuf)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the data from the host.
     */
    uint32_t uPID;
    uint32_t fFlags;
    uint32_t cbInput;
    int rc = VbglR3GuestCtrlProcGetInput(pHostCtx, &uPID, &fFlags, *ppvScratchBuf, *pcbScratchBuf, &cbInput);
    if (   rc == VERR_BUFFER_OVERFLOW
        && vgsvcGstCtrlSessionGrowScratchBuf(ppvScratchBuf, pcbScratchBuf, cbInput))
        rc = VbglR3GuestCtrlProcGetInput(pHostCtx, &uPID, &fFlags, *ppvScratchBuf, *pcbScratchBuf, &cbInput);
    if (RT_SUCCESS(rc))
    {
        if (fFlags & GUEST_PROC_IN_FLAG_EOF)
            VGSvcVerbose(4, "Got last process input block for PID=%RU32 (%RU32 bytes) ...\n", uPID, cbInput);

        /*
         * Locate the process and feed it.
         */
        PVBOXSERVICECTRLPROCESS pProcess = VGSvcGstCtrlSessionRetainProcess(pSession, uPID);
        if (pProcess)
        {
            rc = VGSvcGstCtrlProcessHandleInput(pProcess, pHostCtx, RT_BOOL(fFlags & GUEST_PROC_IN_FLAG_EOF),
                                                *ppvScratchBuf, RT_MIN(cbInput, *pcbScratchBuf));
            if (RT_FAILURE(rc))
                VGSvcError("Error handling input message for PID=%RU32, rc=%Rrc\n", uPID, rc);
            VGSvcGstCtrlProcessRelease(pProcess);
        }
        else
        {
            VGSvcError("Could not find PID %u for feeding %u bytes to it.\n", uPID, cbInput);
            rc = VERR_PROCESS_NOT_FOUND;
            VbglR3GuestCtrlProcCbStatusInput(pHostCtx, uPID, INPUT_STS_ERROR, rc, 0);
        }
    }
    else
    {
        VGSvcError("Failed to retrieve parameters for process input: %Rrc (scratch %u bytes)\n", rc, *pcbScratchBuf);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }

    VGSvcVerbose(6, "Feeding input to PID=%RU32 resulted in rc=%Rrc\n", uPID, rc);
    return rc;
}


/**
 * Gets stdout/stderr output of a specific guest process.
 *
 * @returns VBox status code.
 * @param   pSession            The session which is in charge.
 * @param   pHostCtx            The host context to use.
 */
static int vgsvcGstCtrlSessionHandleProcOutput(PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    uint32_t uPID;
    uint32_t uHandleID;
    uint32_t fFlags;
    int rc = VbglR3GuestCtrlProcGetOutput(pHostCtx, &uPID, &uHandleID, &fFlags);
#ifdef DEBUG_andy
    VGSvcVerbose(4, "Getting output for PID=%RU32, CID=%RU32, uHandleID=%RU32, fFlags=%RU32\n",
                 uPID, pHostCtx->uContextID, uHandleID, fFlags);
#endif
    if (RT_SUCCESS(rc))
    {
        /*
         * Locate the process and hand it the output request.
         */
        PVBOXSERVICECTRLPROCESS pProcess = VGSvcGstCtrlSessionRetainProcess(pSession, uPID);
        if (pProcess)
        {
            rc = VGSvcGstCtrlProcessHandleOutput(pProcess, pHostCtx, uHandleID, _64K /* cbToRead */, fFlags);
            if (RT_FAILURE(rc))
                VGSvcError("Error getting output for PID=%RU32, rc=%Rrc\n", uPID, rc);
            VGSvcGstCtrlProcessRelease(pProcess);
        }
        else
        {
            VGSvcError("Could not find PID %u for draining handle %u (%#x).\n", uPID, uHandleID, uHandleID);
            rc = VERR_PROCESS_NOT_FOUND;
/** @todo r=bird:
 *
 *  No way to report status status code for output requests?
 *
 */
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for process output request: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }

#ifdef DEBUG_andy
    VGSvcVerbose(4, "Getting output for PID=%RU32 resulted in rc=%Rrc\n", uPID, rc);
#endif
    return rc;
}


/**
 * Tells a guest process to terminate.
 *
 * @returns VBox status code.
 * @param   pSession            The session which is in charge.
 * @param   pHostCtx            The host context to use.
 */
static int vgsvcGstCtrlSessionHandleProcTerminate(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    uint32_t uPID;
    int rc = VbglR3GuestCtrlProcGetTerminate(pHostCtx, &uPID);
    if (RT_SUCCESS(rc))
    {
        /*
         * Locate the process and terminate it.
         */
        PVBOXSERVICECTRLPROCESS pProcess = VGSvcGstCtrlSessionRetainProcess(pSession, uPID);
        if (pProcess)
        {
            rc = VGSvcGstCtrlProcessHandleTerm(pProcess);
            if (RT_FAILURE(rc))
                VGSvcError("Error terminating PID=%RU32, rc=%Rrc\n", uPID, rc);

            VGSvcGstCtrlProcessRelease(pProcess);
        }
        else
        {
            VGSvcError("Could not find PID %u for termination.\n", uPID);
            rc = VERR_PROCESS_NOT_FOUND;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for process termination request: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
#ifdef DEBUG_andy
    VGSvcVerbose(4, "Terminating PID=%RU32 resulted in rc=%Rrc\n", uPID, rc);
#endif
    return rc;
}


static int vgsvcGstCtrlSessionHandleProcWaitFor(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    uint32_t uPID;
    uint32_t uWaitFlags;
    uint32_t uTimeoutMS;
    int rc = VbglR3GuestCtrlProcGetWaitFor(pHostCtx, &uPID, &uWaitFlags, &uTimeoutMS);
    if (RT_SUCCESS(rc))
    {
        /*
         * Locate the process and the realize that this call makes no sense
         * since we'll notify the host when a process terminates anyway and
         * hopefully don't need any additional encouragement.
         */
        PVBOXSERVICECTRLPROCESS pProcess = VGSvcGstCtrlSessionRetainProcess(pSession, uPID);
        if (pProcess)
        {
            rc = VERR_NOT_IMPLEMENTED; /** @todo */
            VGSvcGstCtrlProcessRelease(pProcess);
        }
        else
            rc = VERR_NOT_FOUND;
    }
    else
    {
        VGSvcError("Error fetching parameters for process wait request: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
static int vgsvcGstCtrlSessionHandleFsQueryInfo(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    char szPath[RTPATH_MAX];
    int rc = VbglR3GuestCtrlFsGetQueryInfo(pHostCtx, szPath, sizeof(szPath));
    if (RT_SUCCESS(rc))
    {
        GSTCTLFSINFO fsInfo;
        RT_ZERO(fsInfo);

        /* Query as much as we can; ignore any errors and continue. */
        int rc2 = RTFsQuerySizes(szPath, (RTFOFF *)&fsInfo.cbTotalSize, (RTFOFF *)&fsInfo.cbFree,
                                 &fsInfo.cbBlockSize, &fsInfo.cbSectorSize);
        if (RT_FAILURE(rc2))
            VGSvcError("Error calling RTFsQuerySizes() for fsqueryinfo operation: %Rrc\n", rc2);

        RTFSPROPERTIES fsProps;
        rc2 = RTFsQueryProperties(szPath, &fsProps);
        if (RT_SUCCESS(rc2))
        {
            /* Regular (status) flags. */
            fsInfo.cMaxComponent = fsProps.cbMaxComponent;
            if (fsProps.fRemote)
                fsInfo.fFlags |= GSTCTLFSINFO_F_IS_REMOTE;
            if (fsProps.fCaseSensitive)
                fsInfo.fFlags |= GSTCTLFSINFO_F_IS_CASE_SENSITIVE;
            if (fsProps.fReadOnly)
                fsInfo.fFlags |= GSTCTLFSINFO_F_IS_READ_ONLY;
            if (fsProps.fCompressed)
                fsInfo.fFlags |= GSTCTLFSINFO_F_IS_COMPRESSED;

            /* Feature flags. */
            if (fsProps.fSupportsUnicode)
                fsInfo.fFeatures |= GSTCTLFSINFO_FEATURE_F_UNICODE;
            if (fsProps.fFileCompression)
                fsInfo.fFeatures |= GSTCTLFSINFO_FEATURE_F_FILE_COMPRESSION;
        }
        else
            VGSvcError("Error calling RTFsQueryProperties() for fsqueryinfo operation: %Rrc\n", rc2);

        rc2 = RTFsQuerySerial(szPath, &fsInfo.uSerialNumber);
        if (RT_FAILURE(rc2))
            VGSvcError("Error calling RTFsQuerySerial() for fsqueryinfo operation: %Rrc\n", rc2);

#if 0 /** @todo Enable as soon as RTFsQueryLabel() is implemented. */
        rc2 = RTFsQueryLabel(szPath, fsInfo.szLabel, sizeof(fsInfo.szLabel));
        if (RT_FAILURE(rc2))
            VGSvcError("Error calling RTFsQueryLabel() for fsqueryinfo operation: %Rrc\n", rc2);
#endif

        RTFSTYPE enmFsType;
        rc2 = RTFsQueryType(szPath, &enmFsType);
        if (RT_SUCCESS(rc2))
        {
            if (RTStrPrintf2(fsInfo.szName, sizeof(fsInfo.szName), "%s", RTFsTypeName(enmFsType)) <= 0)
                VGSvcError("Error printing type returned by RTFsQueryType()\n");
        }
        else
            VGSvcError("Error calling RTFsQueryType() for fsqueryinfo operation: %Rrc\n", rc2);

#if 0 /** @todo Enable as soon as RTFsQueryMountpoint() is implemented. */
        char szMountpoint[RTPATH_MAX];
        rc2 = RTFsQueryMountpoint(szPath, szMountpoint, sizeof(szMountpoint));
        if (RT_SUCCESS(rc2))
        {
            #error "Implement me"
        }
        else
            VGSvcError("Error calling RTFsQueryMountpoint() for fsqueryinfo operation: %Rrc\n", rc2);
#endif

        if (RT_SUCCESS(rc))
        {
            VGSvcVerbose(3, "cbTotalSize=%RU64, cbFree=%RU64, cbBlockSize=%RU32, cbSectorSize=%RU32, fFlags=%#x, fFeatures=%#x\n",
                         fsInfo.cbTotalSize, fsInfo.cbFree, fsInfo.cbBlockSize, fsInfo.cbSectorSize,
                         fsInfo.fFlags, fsInfo.fFeatures);
            VGSvcVerbose(3, "szName=%s, szLabel=%s\n", fsInfo.szName, fsInfo.szLabel);
        }

        uint32_t const cbFsInfo = sizeof(GSTCTLFSINFO); /** @todo Needs tweaking as soon as we resolve the mountpoint above. */

        rc2 = VbglR3GuestCtrlFsCbQueryInfo(pHostCtx, rc, &fsInfo, cbFsInfo);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to reply to fsobjquerinfo request %Rrc, rc=%Rrc\n", rc, rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for fsqueryinfo operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}

static int vgsvcGstCtrlSessionHandleFsObjQueryInfo(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    char               szPath[RTPATH_MAX];
    GSTCTLFSOBJATTRADD enmAttrAdd;
    uint32_t           fFlags;
    RTFSOBJINFO        objInfoRuntime;

    int rc = VbglR3GuestCtrlFsObjGetQueryInfo(pHostCtx, szPath, sizeof(szPath), &enmAttrAdd, &fFlags);
    if (RT_SUCCESS(rc))
    {
        uint32_t fFlagsRuntime = 0;

        if (!(fFlags & ~GSTCTL_PATH_F_VALID_MASK))
        {
            if (fFlags & GSTCTL_PATH_F_ON_LINK)
                fFlagsRuntime |= RTPATH_F_ON_LINK;
            if (fFlags & GSTCTL_PATH_F_FOLLOW_LINK)
                fFlagsRuntime |= RTPATH_F_FOLLOW_LINK;
            if (fFlags & GSTCTL_PATH_F_NO_SYMLINKS)
                fFlagsRuntime |= RTPATH_F_NO_SYMLINKS;

            if (!RTPATH_F_IS_VALID(fFlagsRuntime, 0))
                rc = VERR_INVALID_PARAMETER;
        }
        else
            rc = VERR_INVALID_PARAMETER;

        if (RT_FAILURE(rc))
            VGSvcError("Invalid fsobjqueryinfo flags: %#x (%#x)\n", fFlags, fFlagsRuntime);

        if (RT_SUCCESS(rc))
        {
#define CASE_ATTR_ADD_VAL(a_Val) \
            case GSTCTL##a_Val: enmAttrRuntime = RT##a_Val; break;

            RTFSOBJATTRADD enmAttrRuntime;
            switch (enmAttrAdd)
            {
                CASE_ATTR_ADD_VAL(FSOBJATTRADD_NOTHING);
                CASE_ATTR_ADD_VAL(FSOBJATTRADD_UNIX);
                CASE_ATTR_ADD_VAL(FSOBJATTRADD_UNIX_OWNER);
                CASE_ATTR_ADD_VAL(FSOBJATTRADD_UNIX_GROUP);
                CASE_ATTR_ADD_VAL(FSOBJATTRADD_EASIZE);
                default:
                    enmAttrRuntime = RTFSOBJATTRADD_NOTHING;
                    break;
            }

#undef CASE_ATTR_ADD_VAL

            /*
             * For now we ASSUME that RTFSOBJINFO == GSTCTLFSOBJINFO, which implies that we simply can cast RTFSOBJINFO
             * to GSTCTLFSOBJINFO. This might change in the future, however, so be extra cautious here.
             *
             * Ditto for RTFSOBJATTR == GSTCTLFSOBJATTR.
             */
            AssertCompileSize(objInfoRuntime, sizeof(GSTCTLFSOBJINFO));
            AssertCompile    (RT_OFFSETOF(GSTCTLFSOBJINFO, cbObject) == RT_OFFSETOF(GSTCTLFSOBJINFO, cbObject));
            AssertCompile    (RT_OFFSETOF(GSTCTLFSOBJINFO, Attr)     == RT_OFFSETOF(GSTCTLFSOBJINFO, Attr));
            AssertCompileSize(RTFSOBJATTR, sizeof(GSTCTLFSOBJATTR));

            rc = RTPathQueryInfoEx(szPath, &objInfoRuntime, enmAttrRuntime, fFlagsRuntime);
        }

        PGSTCTLFSOBJINFO pObjInfo = (PGSTCTLFSOBJINFO)&objInfoRuntime;

        const char *pszUser  = VGSvcIdCacheGetUidName(&pSession->UidCache, pObjInfo->Attr.u.Unix.uid, szPath, NULL /* pszRelativeTo */);
        const char *pszGroup = VGSvcIdCacheGetGidName(&pSession->GidCache, pObjInfo->Attr.u.Unix.gid, szPath, NULL /* pszRelativeTo */);

        int rc2 = VbglR3GuestCtrlFsObjCbQueryInfoEx(pHostCtx, rc, pObjInfo, pszUser, pszGroup);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to reply to fsobjquerinfo request %Rrc, rc=%Rrc\n", rc, rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for fsobjqueryinfo operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    return rc;
}


static int vgsvcGstCtrlSessionHandleFsCreateTemp(const PVBOXSERVICECTRLSESSION pSession, PVBGLR3GUESTCTRLCMDCTX pHostCtx)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);

    /*
     * Retrieve the request.
     */
    char     szTemplate[RTPATH_MAX];
    char     szTempPathAbs[RTPATH_MAX] = ""; /* Absolute path to temporary directory / file created. */
    char     szPath[RTPATH_MAX];
    uint32_t fFlags = GSTCTL_CREATETEMP_F_NONE;
    RTFMODE  fMode  = 0700;
    int rc = VbglR3GuestCtrlFsGetCreateTemp(pHostCtx, szTemplate, sizeof(szTemplate), szPath, sizeof(szPath), &fFlags, &fMode);
    if (RT_SUCCESS(rc))
    {
        if (!(fFlags & ~GSTCTL_CREATETEMP_F_VALID_MASK))
        {
            const char *pszWhat = fFlags & GSTCTL_CREATETEMP_F_DIRECTORY ? "directory" : "file";

            /* Validate that the template is as IPRT requires (asserted by IPRT). */
            if (   RTPathHasPath(szTemplate)
                || (   !strstr(szTemplate, "XXX")
                    && szTemplate[strlen(szTemplate) - 1] != 'X'))
            {
                VGSvcError("createtemp: Template '%s' should contain a file name with no path and at least three consecutive 'X' characters or ending in 'X'\n",
                            szTemplate);
                rc = VERR_INVALID_PARAMETER;
            }

            if (   RT_SUCCESS(rc)
                && szPath[0] != '\0' && !RTPathStartsWithRoot(szPath))
            {
                VGSvcError("createtemp: Path '%s' must be absolute\n", szPath);
                rc = VERR_INVALID_PARAMETER;
            }

            if (RT_SUCCESS(rc))
            {
                if (szPath[0] != '\0')
                {
                    rc = RTStrCopy(szTempPathAbs, sizeof(szTempPathAbs), szPath);
                    if (RT_FAILURE(rc))
                    {
                        VGSvcError("createtemp: Path '%s' too long\n", szPath);
                        rc = VERR_INVALID_PARAMETER;
                    }
                }
                else
                {
                    rc = RTPathTemp(szTempPathAbs, sizeof(szTempPathAbs));
                    if (RT_FAILURE(rc))
                    {
                        VGSvcError("createtemp: Failed to get the temporary directory (%Rrc)", rc);
                        rc = VERR_INVALID_PARAMETER;
                    }
                }

                if (RT_SUCCESS(rc))
                {
                    rc = RTPathAppend(szTempPathAbs, sizeof(szTempPathAbs), szTemplate);
                    if (RT_FAILURE(rc))
                    {
                        VGSvcError("createtemp: Template '%s' too long for path\n", szTemplate);
                        rc = VERR_INVALID_PARAMETER;
                    }
                    else
                    {
                        bool const fSecure = RT_BOOL(fFlags & GSTCTL_CREATETEMP_F_SECURE);
                        if (fFlags & GSTCTL_CREATETEMP_F_DIRECTORY)
                        {
                            if (fSecure)
                                rc = RTDirCreateTempSecure(szTempPathAbs); /* File mode is fixed to 0700. */
                            else
                                rc = RTDirCreateTemp(szTempPathAbs, fMode);
                        }
                        else /* File */
                        {
                            if (fSecure)
                                rc = RTFileCreateTempSecure(szTempPathAbs); /* File mode is fixed to 0700. */
                            else
                                rc = RTFileCreateTemp(szTempPathAbs, fMode);
                        }

                        VGSvcVerbose(3, "Creating temporary %s (szTemplate='%s', fFlags=%#x, fMode=%#x) -> %s, rc=%Rrc\n",
                                     pszWhat, szTemplate, fFlags, fMode, szTempPathAbs, rc);
                    }
                }
            }
        }
        else
        {
            VGSvcError("Invalid temporary directory/file creation flags: %#x\n", fFlags);
            rc = VERR_NOT_SUPPORTED;
        }

        /*
         * Report result back to host.
         */
        int rc2 = VbglR3GuestCtrlFsCbCreateTemp(pHostCtx, rc, szTempPathAbs);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Failed to report temporary file/directory creation status, rc=%Rrc\n", rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }
    else
    {
        VGSvcError("Error fetching parameters for file/directory creation operation: %Rrc\n", rc);
        VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, rc, UINT32_MAX);
    }
    VGSvcVerbose(3, "Creating temporary file/directory returned rc=%Rrc\n", rc);
    return rc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */


int VGSvcGstCtrlSessionHandler(PVBOXSERVICECTRLSESSION pSession, uint32_t uMsg, PVBGLR3GUESTCTRLCMDCTX pHostCtx,
                               void **ppvScratchBuf, uint32_t *pcbScratchBuf, volatile bool *pfShutdown)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pHostCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(*ppvScratchBuf, VERR_INVALID_POINTER);
    AssertPtrReturn(pfShutdown, VERR_INVALID_POINTER);


    /*
     * Only anonymous sessions (that is, sessions which run with local
     * service privileges) or spawned session processes can do certain
     * operations.
     */
    bool const fImpersonated = RT_BOOL(pSession->fFlags & (  VBOXSERVICECTRLSESSION_FLAG_SPAWN
                                                           | VBOXSERVICECTRLSESSION_FLAG_ANONYMOUS));
    int rc = VERR_NOT_SUPPORTED; /* Play safe by default. */

    switch (uMsg)
    {
        case HOST_MSG_SESSION_CLOSE:
            /* Shutdown (this spawn). */
            rc = VGSvcGstCtrlSessionClose(pSession);
            *pfShutdown = true; /* Shutdown in any case. */
            break;

        case HOST_MSG_EXEC_CMD:
            rc = vgsvcGstCtrlSessionHandleProcExec(pSession, pHostCtx);
            break;

        case HOST_MSG_EXEC_SET_INPUT:
            rc = vgsvcGstCtrlSessionHandleProcInput(pSession, pHostCtx, ppvScratchBuf, pcbScratchBuf);
            break;

        case HOST_MSG_EXEC_GET_OUTPUT:
            rc = vgsvcGstCtrlSessionHandleProcOutput(pSession, pHostCtx);
            break;

        case HOST_MSG_EXEC_TERMINATE:
            rc = vgsvcGstCtrlSessionHandleProcTerminate(pSession, pHostCtx);
            break;

        case HOST_MSG_EXEC_WAIT_FOR:
            rc = vgsvcGstCtrlSessionHandleProcWaitFor(pSession, pHostCtx);
            break;

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
        case HOST_MSG_FS_OBJ_QUERY_INFO:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleFsObjQueryInfo(pSession, pHostCtx);
            break;

        case HOST_MSG_FS_CREATE_TEMP:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleFsCreateTemp(pSession, pHostCtx);
            break;

        case HOST_MSG_FS_QUERY_INFO:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleFsQueryInfo(pSession, pHostCtx);
            break;
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */

        case HOST_MSG_FILE_OPEN:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleFileOpen(pSession, pHostCtx);
            break;

        case HOST_MSG_FILE_CLOSE:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleFileClose(pSession, pHostCtx);
            break;

        case HOST_MSG_FILE_READ:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleFileRead(pSession, pHostCtx, ppvScratchBuf, pcbScratchBuf);
            break;

        case HOST_MSG_FILE_READ_AT:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleFileReadAt(pSession, pHostCtx, ppvScratchBuf, pcbScratchBuf);
            break;

        case HOST_MSG_FILE_WRITE:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleFileWrite(pSession, pHostCtx, ppvScratchBuf, pcbScratchBuf);
            break;

        case HOST_MSG_FILE_WRITE_AT:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleFileWriteAt(pSession, pHostCtx, ppvScratchBuf, pcbScratchBuf);
            break;

        case HOST_MSG_FILE_SEEK:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleFileSeek(pSession, pHostCtx);
            break;

        case HOST_MSG_FILE_TELL:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleFileTell(pSession, pHostCtx);
            break;

        case HOST_MSG_FILE_SET_SIZE:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleFileSetSize(pSession, pHostCtx);
            break;

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
        case HOST_MSG_FILE_REMOVE:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleFileRemove(pSession, pHostCtx);
            break;

        case HOST_MSG_DIR_OPEN:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleDirOpen(pSession, pHostCtx);
            break;

        case HOST_MSG_DIR_CLOSE:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleDirClose(pSession, pHostCtx);
            break;

        case HOST_MSG_DIR_READ:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleDirRead(pSession, pHostCtx);
            break;

        case HOST_MSG_DIR_REWIND:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleDirRewind(pSession, pHostCtx);
            break;

        case HOST_MSG_DIR_CREATE:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleDirCreate(pSession, pHostCtx);
            break;

        case HOST_MSG_DIR_LIST:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleDirList(pSession, pHostCtx);
            break;
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */

        case HOST_MSG_DIR_REMOVE:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleDirRemove(pSession, pHostCtx);
            break;

        case HOST_MSG_PATH_RENAME:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandlePathRename(pSession, pHostCtx);
            break;

        case HOST_MSG_PATH_USER_DOCUMENTS:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandlePathUserDocuments(pSession, pHostCtx);
            break;

        case HOST_MSG_PATH_USER_HOME:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandlePathUserHome(pSession, pHostCtx);
            break;

        case HOST_MSG_MOUNT_POINTS:
            if (fImpersonated)
                rc = vgsvcGstCtrlSessionHandleMountPoints(pSession, pHostCtx);
            break;

        case HOST_MSG_SHUTDOWN:
            rc = vgsvcGstCtrlSessionHandleShutdown(pSession, pHostCtx);
            break;

        default: /* Not supported, see next code block. */
            break;
    }
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else if (rc != VERR_NOT_SUPPORTED) /* Note: Reply to host must must be sent by above handler. */
        VGSvcError("Error while handling message %s (%#x, cParms=%RU32), rc=%Rrc\n",
                   GstCtrlHostMsgtoStr((eHostMsg)uMsg), uMsg, pHostCtx->uNumParms, rc);
    else
    {
        /* We must skip and notify host here as best we can... */
        VGSvcVerbose(1, "Unsupported message (uMsg=%RU32, cParms=%RU32) from host, skipping\n", uMsg, pHostCtx->uNumParms);
        if (VbglR3GuestCtrlSupportsOptimizations(pHostCtx->uClientID))
            VbglR3GuestCtrlMsgSkip(pHostCtx->uClientID, VERR_NOT_SUPPORTED, uMsg);
        else
            VbglR3GuestCtrlMsgSkipOld(pHostCtx->uClientID);
        rc = VINF_SUCCESS;
    }

    return rc;
}


/**
 * Thread main routine for a spawned guest session process.
 *
 * This thread runs in the main executable to control the spawned session process.
 *
 * @returns VBox status code.
 * @param   hThreadSelf     Thread handle.
 * @param   pvUser          Pointer to a VBOXSERVICECTRLSESSIONTHREAD structure.
 *
 */
static DECLCALLBACK(int) vgsvcGstCtrlSessionThread(RTTHREAD hThreadSelf, void *pvUser)
{
    PVBOXSERVICECTRLSESSIONTHREAD pThread = (PVBOXSERVICECTRLSESSIONTHREAD)pvUser;
    AssertPtrReturn(pThread, VERR_INVALID_POINTER);

    uint32_t const idSession = pThread->pStartupInfo->uSessionID;
    uint32_t const idClient  = g_idControlSvcClient;
    VGSvcVerbose(3, "Session ID=%RU32 thread running\n", idSession);

    /* Let caller know that we're done initializing, regardless of the result. */
    int rc2 = RTThreadUserSignal(hThreadSelf);
    AssertRC(rc2);

    /*
     * Wait for the child process to stop or the shutdown flag to be signalled.
     */
    RTPROCSTATUS    ProcessStatus       = { 0, RTPROCEXITREASON_NORMAL };
    bool            fProcessAlive       = true;
    bool            fSessionCancelled   = VbglR3GuestCtrlSupportsOptimizations(g_idControlSvcClient);
    uint32_t        cMsShutdownTimeout  = 30 * 1000; /** @todo Make this configurable. Later. */
    uint64_t        msShutdownStart     = 0;
    uint64_t const  msStart             = RTTimeMilliTS();
    size_t          offSecretKey        = 0;
    int             rcWait;
    for (;;)
    {
        /* Secret key feeding. */
        if (offSecretKey < sizeof(pThread->abKey))
        {
            size_t cbWritten = 0;
            rc2 = RTPipeWrite(pThread->hKeyPipe, &pThread->abKey[offSecretKey], sizeof(pThread->abKey) - offSecretKey, &cbWritten);
            if (RT_SUCCESS(rc2))
                offSecretKey += cbWritten;
        }

        /* Poll child process status. */
        rcWait = RTProcWaitNoResume(pThread->hProcess, RTPROCWAIT_FLAGS_NOBLOCK, &ProcessStatus);
        if (   rcWait == VINF_SUCCESS
            || rcWait == VERR_PROCESS_NOT_FOUND)
        {
            fProcessAlive = false;
            break;
        }
        AssertMsgBreak(rcWait == VERR_PROCESS_RUNNING || rcWait == VERR_INTERRUPTED,
                       ("Got unexpected rc=%Rrc while waiting for session process termination\n", rcWait));

        /* Shutting down? */
        if (ASMAtomicReadBool(&pThread->fShutdown))
        {
            if (!msShutdownStart)
            {
                VGSvcVerbose(3, "Notifying guest session process (PID=%RU32, session ID=%RU32) ...\n",
                             pThread->hProcess, idSession);

                VBGLR3GUESTCTRLCMDCTX hostCtx =
                {
                    /* .idClient  = */  idClient,
                    /* .idContext = */  VBOX_GUESTCTRL_CONTEXTID_MAKE_SESSION(idSession),
                    /* .uProtocol = */  pThread->pStartupInfo->uProtocol,
                    /* .cParams   = */  2
                };
                rc2 = VbglR3GuestCtrlSessionClose(&hostCtx, 0 /* fFlags */);
                if (RT_FAILURE(rc2))
                {
                    VGSvcError("Unable to notify guest session process (PID=%RU32, session ID=%RU32), rc=%Rrc\n",
                               pThread->hProcess, idSession, rc2);

                    if (rc2 == VERR_NOT_SUPPORTED)
                    {
                        /* Terminate guest session process in case it's not supported by a too old host. */
                        rc2 = RTProcTerminate(pThread->hProcess);
                        VGSvcVerbose(3, "Terminating guest session process (PID=%RU32) ended with rc=%Rrc\n",
                                     pThread->hProcess, rc2);
                    }
                    break;
                }

                VGSvcVerbose(3, "Guest session ID=%RU32 thread was asked to terminate, waiting for session process to exit (%RU32 ms timeout) ...\n",
                             idSession, cMsShutdownTimeout);
                msShutdownStart = RTTimeMilliTS();
                continue; /* Don't waste time on waiting. */
            }
            if (RTTimeMilliTS() - msShutdownStart > cMsShutdownTimeout)
            {
                 VGSvcVerbose(3, "Guest session ID=%RU32 process did not shut down within time\n", idSession);
                 break;
            }
        }

        /* Cancel the prepared session stuff after 30 seconds. */
        if (  !fSessionCancelled
            && RTTimeMilliTS() - msStart >= 30000)
        {
            VbglR3GuestCtrlSessionCancelPrepared(g_idControlSvcClient, idSession);
            fSessionCancelled = true;
        }

/** @todo r=bird: This 100ms sleep is _extremely_ sucky! */
        RTThreadSleep(100); /* Wait a bit. */
    }

    if (!fSessionCancelled)
        VbglR3GuestCtrlSessionCancelPrepared(g_idControlSvcClient, idSession);

    if (!fProcessAlive)
    {
        VGSvcVerbose(2, "Guest session process (ID=%RU32) terminated with rc=%Rrc, reason=%d, status=%d\n",
                     idSession, rcWait, ProcessStatus.enmReason, ProcessStatus.iStatus);
        if (ProcessStatus.iStatus == RTEXITCODE_INIT)
        {
            VGSvcError("Guest session process (ID=%RU32) failed to initialize. Here some hints:\n", idSession);
            VGSvcError("- Is logging enabled and the output directory is read-only by the guest session user?\n");
            /** @todo Add more here. */
        }
    }

    uint32_t uSessionStatus = GUEST_SESSION_NOTIFYTYPE_UNDEFINED;
    int32_t  iSessionResult = VINF_SUCCESS;

    if (fProcessAlive)
    {
        for (int i = 0; i < 3; i++)
        {
            if (i)
                RTThreadSleep(3000);

            VGSvcVerbose(2, "Guest session ID=%RU32 process still alive, killing attempt %d/3\n", idSession, i + 1);

            rc2 = RTProcTerminate(pThread->hProcess);
            if (RT_SUCCESS(rc2))
                break;
        }

        VGSvcVerbose(2, "Guest session ID=%RU32 process termination resulted in rc=%Rrc\n", idSession, rc2);
        uSessionStatus = RT_SUCCESS(rc2) ? GUEST_SESSION_NOTIFYTYPE_TOK : GUEST_SESSION_NOTIFYTYPE_TOA;
    }
    else if (RT_SUCCESS(rcWait))
    {
        switch (ProcessStatus.enmReason)
        {
            case RTPROCEXITREASON_NORMAL:
                uSessionStatus = GUEST_SESSION_NOTIFYTYPE_TEN;
                iSessionResult = ProcessStatus.iStatus; /* Report back the session's exit code. */
                break;

            case RTPROCEXITREASON_ABEND:
                uSessionStatus = GUEST_SESSION_NOTIFYTYPE_TEA;
                /* iSessionResult is undefined (0). */
                break;

            case RTPROCEXITREASON_SIGNAL:
                uSessionStatus = GUEST_SESSION_NOTIFYTYPE_TES;
                iSessionResult = ProcessStatus.iStatus; /* Report back the signal number. */
                break;

            default:
                AssertMsgFailed(("Unhandled process termination reason (%d)\n", ProcessStatus.enmReason));
                uSessionStatus = GUEST_SESSION_NOTIFYTYPE_TEA;
                break;
        }
    }
    else
    {
        /* If we didn't find the guest process anymore, just assume it terminated normally. */
        uSessionStatus = GUEST_SESSION_NOTIFYTYPE_TEN;
    }

    /* Make sure to set stopped state before we let the host know. */
    ASMAtomicWriteBool(&pThread->fStopped, true);

    /* Report final status, regardless if we failed to wait above, so that the host knows what's going on. */
    VGSvcVerbose(3, "Reporting final status %RU32 of session ID=%RU32\n", uSessionStatus, idSession);
    Assert(uSessionStatus != GUEST_SESSION_NOTIFYTYPE_UNDEFINED);

    VBGLR3GUESTCTRLCMDCTX ctx = { idClient, VBOX_GUESTCTRL_CONTEXTID_MAKE_SESSION(idSession),
                                  0 /* uProtocol, unused */, 0 /* uNumParms, unused */ };
    rc2 = VbglR3GuestCtrlSessionNotify(&ctx, uSessionStatus, iSessionResult);
    if (RT_FAILURE(rc2))
        VGSvcError("Reporting final status of session ID=%RU32 failed with rc=%Rrc\n", idSession, rc2);

    VGSvcVerbose(3, "Thread for session ID=%RU32 ended with sessionStatus=%#x (%RU32), sessionRc=%#x (%Rrc)\n",
                 idSession, uSessionStatus, uSessionStatus, iSessionResult, iSessionResult);

    return VINF_SUCCESS;
}

/**
 * Reads the secret key the parent VBoxService instance passed us and pass it
 * along as a authentication token to the host service.
 *
 * For older hosts, this sets up the message filtering.
 *
 * @returns VBox status code.
 * @param   idClient        The HGCM client ID.
 * @param   idSession       The session ID.
 */
static int vgsvcGstCtrlSessionReadKeyAndAccept(uint32_t idClient, uint32_t idSession)
{
    /*
     * Read it.
     */
    RTHANDLE Handle;
    int rc = RTHandleGetStandard(RTHANDLESTD_INPUT, true /*fLeaveOpen*/, &Handle);
    if (RT_SUCCESS(rc))
    {
        if (Handle.enmType == RTHANDLETYPE_PIPE)
        {
            uint8_t abSecretKey[RT_SIZEOFMEMB(VBOXSERVICECTRLSESSIONTHREAD, abKey)];
            rc = RTPipeReadBlocking(Handle.u.hPipe, abSecretKey, sizeof(abSecretKey), NULL);
            if (RT_SUCCESS(rc))
            {
                VGSvcVerbose(3, "Got secret key from standard input.\n");

                /*
                 * Do the accepting, if appropriate.
                 */
                if (g_fControlSupportsOptimizations)
                {
                    rc = VbglR3GuestCtrlSessionAccept(idClient, idSession, abSecretKey, sizeof(abSecretKey));
                    if (RT_SUCCESS(rc))
                        VGSvcVerbose(3, "Session %u accepted (client ID %u)\n", idClient, idSession);
                    else
                        VGSvcError("Failed to accept session %u (client ID %u): %Rrc\n", idClient, idSession, rc);
                }
                else
                {
                    /* For legacy hosts, we do the filtering thingy. */
                    rc = VbglR3GuestCtrlMsgFilterSet(idClient, VBOX_GUESTCTRL_CONTEXTID_MAKE_SESSION(idSession),
                                                     VBOX_GUESTCTRL_FILTER_BY_SESSION(idSession), 0);
                    if (RT_SUCCESS(rc))
                        VGSvcVerbose(3, "Session %u filtering successfully enabled\n", idSession);
                    else
                        VGSvcError("Failed to set session filter: %Rrc\n", rc);
                }
            }
            else
                VGSvcError("Error reading secret key from standard input: %Rrc\n", rc);
        }
        else
        {
            VGSvcError("Standard input is not a pipe!\n");
            rc = VERR_INVALID_HANDLE;
        }
        RTHandleClose(&Handle);
    }
    else
        VGSvcError("RTHandleGetStandard failed on standard input: %Rrc\n", rc);
    return rc;
}

/**
 * Invalidates a guest session by updating all it's internal parameters like host features and stuff.
 *
 * @param   pSession            Session to invalidate.
 * @param   idClient            Client ID to use.
 */
static void vgsvcGstCtrlSessionInvalidate(PVBOXSERVICECTRLSESSION pSession, uint32_t idClient)
{
    RT_NOREF(pSession);

    VGSvcVerbose(1, "Invalidating session %RU32 (client ID=%RU32)\n", idClient, pSession->StartupInfo.uSessionID);

    int rc2 = VbglR3GuestCtrlQueryFeatures(idClient, &g_fControlHostFeatures0);
    if (RT_SUCCESS(rc2)) /* Querying host features is not fatal -- do not use rc here. */
    {
        VGSvcVerbose(1, "g_fControlHostFeatures0=%#x\n", g_fControlHostFeatures0);
    }
    else
        VGSvcVerbose(1, "Querying host features failed with %Rrc\n", rc2);
}

/**
 * Main message handler for the guest control session process.
 *
 * @returns exit code.
 * @param   pSession    Pointer to g_Session.
 * @thread  main.
 */
static RTEXITCODE vgsvcGstCtrlSessionSpawnWorker(PVBOXSERVICECTRLSESSION pSession)
{
    AssertPtrReturn(pSession, RTEXITCODE_FAILURE);
    VGSvcVerbose(0, "Hi, this is guest session ID=%RU32\n", pSession->StartupInfo.uSessionID);

    /*
     * Connect to the host service.
     */
    uint32_t idClient;
    int rc = VbglR3GuestCtrlConnect(&idClient);
    if (RT_FAILURE(rc))
        return VGSvcError("Error connecting to guest control service, rc=%Rrc\n", rc);
    g_fControlSupportsOptimizations = VbglR3GuestCtrlSupportsOptimizations(idClient);
    g_idControlSvcClient            = idClient;

    VGSvcVerbose(1, "Using client ID=%RU32\n", idClient);

    vgsvcGstCtrlSessionInvalidate(pSession, idClient);

    rc = vgsvcGstCtrlSessionReadKeyAndAccept(idClient, pSession->StartupInfo.uSessionID);
    if (RT_SUCCESS(rc))
    {
        /*
         * Report started status.
         * If session status cannot be posted to the host for some reason, bail out.
         */
        VBGLR3GUESTCTRLCMDCTX ctx = { idClient, VBOX_GUESTCTRL_CONTEXTID_MAKE_SESSION(pSession->StartupInfo.uSessionID),
                                      0 /* uProtocol, unused */, 0 /* uNumParms, unused */ };
        rc = VbglR3GuestCtrlSessionNotify(&ctx, GUEST_SESSION_NOTIFYTYPE_STARTED, VINF_SUCCESS);
        if (RT_SUCCESS(rc))
        {
            /*
             * Allocate a scratch buffer for messages which also send payload data with them.
             * This buffer may grow if the host sends us larger chunks of data.
             */
            uint32_t cbScratchBuf = _64K;
            void    *pvScratchBuf = RTMemAlloc(cbScratchBuf);
            if (pvScratchBuf)
            {
                int cFailedMsgPeeks = 0;

                /*
                 * Message processing loop.
                 */
                VBGLR3GUESTCTRLCMDCTX CtxHost = { idClient, 0 /* Context ID */, pSession->StartupInfo.uProtocol, 0 };
                for (;;)
                {
                    VGSvcVerbose(3, "Waiting for host msg ...\n");
                    uint32_t uMsg = 0;
                    rc = VbglR3GuestCtrlMsgPeekWait(idClient, &uMsg, &CtxHost.uNumParms, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        VGSvcVerbose(4, "Msg=%RU32 (%RU32 parms) retrieved (%Rrc)\n", uMsg, CtxHost.uNumParms, rc);

                        /*
                         * Pass it on to the session handler.
                         * Note! Only when handling HOST_SESSION_CLOSE is the rc used.
                         */
                        bool fShutdown = false;
                        rc = VGSvcGstCtrlSessionHandler(pSession, uMsg, &CtxHost, &pvScratchBuf, &cbScratchBuf, &fShutdown);
                        if (fShutdown)
                            break;

                        cFailedMsgPeeks = 0;

                        /* Let others run (guests are often single CPU) ... */
                        RTThreadYield();
                    }
                    /*
                     * Handle restore notification from host.  All the context IDs (sessions,
                     * files, proceses, etc) are invalidated by a VM restore and must be closed.
                     */
                    else if (rc == VERR_VM_RESTORED)
                    {
                        VGSvcVerbose(1, "The VM session ID changed (i.e. restored), closing stale session %RU32\n",
                                     pSession->StartupInfo.uSessionID);

                        /* We currently don't serialize guest sessions, guest processes and other guest control objects
                         * within saved states. So just close this session and report success to the parent process.
                         *
                         * Note: Not notifying the host here is intentional, as it wouldn't have any information
                         *       about what to do with it.
                         */
                        rc = VINF_SUCCESS; /* Report success as exit code. */
                        break;
                    }
                    else
                    {
                        VGSvcVerbose(1, "Getting host message failed with %Rrc\n", rc);

                        if (cFailedMsgPeeks++ == 3)
                            break;

                        RTThreadSleep(3 * RT_MS_1SEC);

                        /** @todo Shouldn't we have a plan for handling connection loss and such? */
                    }
                }

                /*
                 * Shutdown.
                 */
                RTMemFree(pvScratchBuf);
            }
            else
                rc = VERR_NO_MEMORY;

            VGSvcVerbose(0, "Session %RU32 ended\n", pSession->StartupInfo.uSessionID);
        }
        else
            VGSvcError("Reporting session ID=%RU32 started status failed with rc=%Rrc\n", pSession->StartupInfo.uSessionID, rc);
    }
    else
        VGSvcError("Setting message filterAdd=0x%x failed with rc=%Rrc\n", pSession->StartupInfo.uSessionID, rc);

    VGSvcVerbose(3, "Disconnecting client ID=%RU32 ...\n", idClient);
    VbglR3GuestCtrlDisconnect(idClient);
    g_idControlSvcClient = 0;

    VGSvcVerbose(3, "Session worker returned with rc=%Rrc\n", rc);
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * Finds a (formerly) started guest process given by its PID and increases its
 * reference count.
 *
 * Must be decreased by the caller with VGSvcGstCtrlProcessRelease().
 *
 * @returns Guest process if found, otherwise NULL.
 * @param   pSession    Pointer to guest session where to search process in.
 * @param   uPID        PID to search for.
 *
 * @note    This does *not lock the process!
 */
PVBOXSERVICECTRLPROCESS VGSvcGstCtrlSessionRetainProcess(PVBOXSERVICECTRLSESSION pSession, uint32_t uPID)
{
    AssertPtrReturn(pSession, NULL);

    PVBOXSERVICECTRLPROCESS pProcess = NULL;
    int rc = RTCritSectEnter(&pSession->CritSect);
    if (RT_SUCCESS(rc))
    {
        PVBOXSERVICECTRLPROCESS pCurProcess;
        RTListForEach(&pSession->lstProcesses, pCurProcess, VBOXSERVICECTRLPROCESS, Node)
        {
            if (pCurProcess->uPID == uPID)
            {
                rc = RTCritSectEnter(&pCurProcess->CritSect);
                if (RT_SUCCESS(rc))
                {
                    pCurProcess->cRefs++;
                    rc = RTCritSectLeave(&pCurProcess->CritSect);
                    AssertRC(rc);
                }

                if (RT_SUCCESS(rc))
                    pProcess = pCurProcess;
                break;
            }
        }

        rc = RTCritSectLeave(&pSession->CritSect);
        AssertRC(rc);
    }

    return pProcess;
}


int VGSvcGstCtrlSessionClose(PVBOXSERVICECTRLSESSION pSession)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);

    VGSvcVerbose(0, "Session %RU32 is about to close ...\n", pSession->StartupInfo.uSessionID);

    int rc = RTCritSectEnter(&pSession->CritSect);
    if (RT_SUCCESS(rc))
    {
        /*
         * Close all guest processes.
         */
        VGSvcVerbose(0, "Stopping all guest processes ...\n");

        /* Signal all guest processes in the active list that we want to shutdown. */
        PVBOXSERVICECTRLPROCESS pProcess;
        RTListForEach(&pSession->lstProcesses, pProcess, VBOXSERVICECTRLPROCESS, Node)
            VGSvcGstCtrlProcessStop(pProcess);

        VGSvcVerbose(1, "%RU32 guest processes were signalled to stop\n", pSession->cProcesses);

        /* Wait for all active threads to shutdown and destroy the active thread list. */
        PVBOXSERVICECTRLPROCESS pProcessNext;
        RTListForEachSafe(&pSession->lstProcesses, pProcess, pProcessNext, VBOXSERVICECTRLPROCESS, Node)
        {
            int rc3 = RTCritSectLeave(&pSession->CritSect);
            AssertRC(rc3);

            int rc2 = VGSvcGstCtrlProcessWait(pProcess, 30 * 1000 /* Wait 30 seconds max. */, NULL /* rc */);

            rc3 = RTCritSectEnter(&pSession->CritSect);
            AssertRC(rc3);

            if (RT_SUCCESS(rc2))
            {
                rc2 = vgsvcGstCtrlSessionProcessRemoveInternal(pSession, pProcess);
                if (RT_SUCCESS(rc2))
                {
                    VGSvcGstCtrlProcessFree(pProcess);
                    pProcess = NULL;
                }
            }
        }

        AssertMsg(pSession->cProcesses == 0,
                  ("Session process list still contains %RU32 when it should not\n", pSession->cProcesses));
        AssertMsg(RTListIsEmpty(&pSession->lstProcesses),
                  ("Session process list is not empty when it should\n"));

        /*
         * Close all left guest files.
         */
        VGSvcVerbose(0, "Closing all guest files ...\n");

        PVBOXSERVICECTRLFILE pFile, pFileNext;
        RTListForEachSafe(&pSession->lstFiles, pFile, pFileNext, VBOXSERVICECTRLFILE, Node)
        {
            int rc2 = vgsvcGstCtrlSessionFileFree(pFile);
            if (RT_FAILURE(rc2))
            {
                VGSvcError("Unable to close file '%s'; rc=%Rrc\n", pFile->pszName, rc2);
                if (RT_SUCCESS(rc))
                    rc = rc2;
                /* Keep going. */
            }

            pFile = NULL; /* To make it obvious. */
        }

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
        AssertMsg(pSession->cDirs == 0,
                  ("Session directory list still contains %RU32 when it should not\n", pSession->cDirs));
        AssertMsg(RTListIsEmpty(&pSession->lstDirs),
                  ("Session directory list is not empty when it should\n"));
#endif
        AssertMsg(pSession->cFiles == 0,
                  ("Session file list still contains %RU32 when it should not\n", pSession->cFiles));
        AssertMsg(RTListIsEmpty(&pSession->lstFiles),
                  ("Session file list is not empty when it should\n"));

        int rc2 = RTCritSectLeave(&pSession->CritSect);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}


int VGSvcGstCtrlSessionDestroy(PVBOXSERVICECTRLSESSION pSession)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);

    int rc = VGSvcGstCtrlSessionClose(pSession);

    /* Destroy critical section. */
    RTCritSectDelete(&pSession->CritSect);

    return rc;
}


int VGSvcGstCtrlSessionInit(PVBOXSERVICECTRLSESSION pSession, uint32_t fFlags)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);

    RTListInit(&pSession->lstProcesses);
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    RTListInit(&pSession->lstDirs);
#endif
    RTListInit(&pSession->lstFiles);

    pSession->cProcesses = 0;
    pSession->cFiles     = 0;

    pSession->fFlags = fFlags;

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    RT_ZERO(pSession->UidCache);
    RT_ZERO(pSession->GidCache);
#endif

    /* Init critical section for protecting the thread lists. */
    int rc = RTCritSectInit(&pSession->CritSect);
    AssertRC(rc);

    return rc;
}


/**
 * Adds a guest process to a session's process list.
 *
 * @return  VBox status code.
 * @param   pSession                Guest session to add process to.
 * @param   pProcess                Guest process to add.
 */
int VGSvcGstCtrlSessionProcessAdd(PVBOXSERVICECTRLSESSION pSession, PVBOXSERVICECTRLPROCESS pProcess)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);

    int rc = RTCritSectEnter(&pSession->CritSect);
    if (RT_SUCCESS(rc))
    {
        VGSvcVerbose(3, "Adding process (PID %RU32) to session ID=%RU32\n", pProcess->uPID, pSession->StartupInfo.uSessionID);

        /* Add process to session list. */
        RTListAppend(&pSession->lstProcesses, &pProcess->Node);

        pSession->cProcesses++;
        VGSvcVerbose(3, "Now session ID=%RU32 has %RU32 processes total\n",
                     pSession->StartupInfo.uSessionID, pSession->cProcesses);

        int rc2 = RTCritSectLeave(&pSession->CritSect);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return VINF_SUCCESS;
}

/**
 * Removes a guest process from a session's process list.
 * Internal version, does not do locking.
 *
 * @return  VBox status code.
 * @param   pSession                Guest session to remove process from.
 * @param   pProcess                Guest process to remove.
 */
static int vgsvcGstCtrlSessionProcessRemoveInternal(PVBOXSERVICECTRLSESSION pSession, PVBOXSERVICECTRLPROCESS pProcess)
{
    VGSvcVerbose(3, "Removing process (PID %RU32) from session ID=%RU32\n", pProcess->uPID, pSession->StartupInfo.uSessionID);
    AssertReturn(pProcess->cRefs == 0, VERR_WRONG_ORDER);

    RTListNodeRemove(&pProcess->Node);

    AssertReturn(pSession->cProcesses, VERR_WRONG_ORDER);
    pSession->cProcesses--;
    VGSvcVerbose(3, "Now session ID=%RU32 has %RU32 processes total\n",
                 pSession->StartupInfo.uSessionID, pSession->cProcesses);

    return VINF_SUCCESS;
}

/**
 * Removes a guest process from a session's process list.
 *
 * @return  VBox status code.
 * @param   pSession                Guest session to remove process from.
 * @param   pProcess                Guest process to remove.
 */
int VGSvcGstCtrlSessionProcessRemove(PVBOXSERVICECTRLSESSION pSession, PVBOXSERVICECTRLPROCESS pProcess)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);

    int rc = RTCritSectEnter(&pSession->CritSect);
    if (RT_SUCCESS(rc))
    {
        rc = vgsvcGstCtrlSessionProcessRemoveInternal(pSession, pProcess);

        int rc2 = RTCritSectLeave(&pSession->CritSect);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}


/**
 * Determines whether starting a new guest process according to the
 * maximum number of concurrent guest processes defined is allowed or not.
 *
 * @return  VBox status code.
 * @param   pSession            The guest session.
 * @param   pfAllowed           \c True if starting (another) guest process
 *                              is allowed, \c false if not.
 */
int VGSvcGstCtrlSessionProcessStartAllowed(const PVBOXSERVICECTRLSESSION pSession, bool *pfAllowed)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pfAllowed, VERR_INVALID_POINTER);

    int rc = RTCritSectEnter(&pSession->CritSect);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check if we're respecting our memory policy by checking
         * how many guest processes are started and served already.
         */
        bool fLimitReached = false;
        if (pSession->uProcsMaxKept) /* If we allow unlimited processes (=0), take a shortcut. */
        {
            VGSvcVerbose(3, "Maximum kept guest processes set to %RU32, acurrent=%RU32\n",
                         pSession->uProcsMaxKept, pSession->cProcesses);

            int32_t iProcsLeft = (pSession->uProcsMaxKept - pSession->cProcesses - 1);
            if (iProcsLeft < 0)
            {
                VGSvcVerbose(3, "Maximum running guest processes reached (%RU32)\n", pSession->uProcsMaxKept);
                fLimitReached = true;
            }
        }

        *pfAllowed = !fLimitReached;

        int rc2 = RTCritSectLeave(&pSession->CritSect);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}


/**
 * Cleans up stopped and no longer used processes.
 *
 * This will free and remove processes from the session's process list.
 *
 * @returns VBox status code.
 * @param   pSession            Session to clean up processes for.
 */
static int vgsvcGstCtrlSessionCleanupProcesses(const PVBOXSERVICECTRLSESSION pSession)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);

    VGSvcVerbose(3, "Cleaning up stopped processes for session %RU32 ...\n", pSession->StartupInfo.uSessionID);

    int rc2 = RTCritSectEnter(&pSession->CritSect);
    AssertRC(rc2);

    int rc = VINF_SUCCESS;

    PVBOXSERVICECTRLPROCESS pCurProcess, pNextProcess;
    RTListForEachSafe(&pSession->lstProcesses, pCurProcess, pNextProcess, VBOXSERVICECTRLPROCESS, Node)
    {
        if (ASMAtomicReadBool(&pCurProcess->fStopped))
        {
            rc2 = RTCritSectLeave(&pSession->CritSect);
            AssertRC(rc2);

            rc = VGSvcGstCtrlProcessWait(pCurProcess, 30 * 1000 /* Wait 30 seconds max. */, NULL /* rc */);
            if (RT_SUCCESS(rc))
            {
                VGSvcGstCtrlSessionProcessRemove(pSession, pCurProcess);
                VGSvcGstCtrlProcessFree(pCurProcess);
            }

            rc2 = RTCritSectEnter(&pSession->CritSect);
            AssertRC(rc2);

            /* If failed, try next time we're being called. */
        }
    }

    rc2 = RTCritSectLeave(&pSession->CritSect);
    AssertRC(rc2);

    if (RT_FAILURE(rc))
        VGSvcError("Cleaning up stopped processes for session %RU32 failed with %Rrc\n", pSession->StartupInfo.uSessionID, rc);

    return rc;
}


/**
 * Creates the process for a guest session.
 *
 * @return  VBox status code.
 * @param   pSessionStartupInfo     Session startup info.
 * @param   pSessionThread          The session thread under construction.
 * @param   uCtrlSessionThread      The session thread debug ordinal.
 */
static int vgsvcVGSvcGstCtrlSessionThreadCreateProcess(const PVBGLR3GUESTCTRLSESSIONSTARTUPINFO pSessionStartupInfo,
                                                       PVBOXSERVICECTRLSESSIONTHREAD pSessionThread, uint32_t uCtrlSessionThread)
{
    RT_NOREF(uCtrlSessionThread);

    /*
     * Is this an anonymous session?  Anonymous sessions run with the same
     * privileges as the main VBoxService executable.
     */
    bool const fAnonymous =    pSessionThread->pStartupInfo->pszUser
                            && pSessionThread->pStartupInfo->pszUser[0] == '\0';
    if (fAnonymous)
    {
        Assert(!strlen(pSessionThread->pStartupInfo->pszPassword));
        Assert(!strlen(pSessionThread->pStartupInfo->pszDomain));

        VGSvcVerbose(3, "New anonymous guest session ID=%RU32 created, fFlags=%x, using protocol %RU32\n",
                     pSessionStartupInfo->uSessionID,
                     pSessionStartupInfo->fFlags,
                     pSessionStartupInfo->uProtocol);
    }
    else
    {
        VGSvcVerbose(3, "Spawning new guest session ID=%RU32, szUser=%s, szPassword=%s, szDomain=%s, fFlags=%x, using protocol %RU32\n",
                     pSessionStartupInfo->uSessionID,
                     pSessionStartupInfo->pszUser,
#ifdef DEBUG
                     pSessionStartupInfo->pszPassword,
#else
                     "XXX", /* Never show passwords in release mode. */
#endif
                     pSessionStartupInfo->pszDomain,
                     pSessionStartupInfo->fFlags,
                     pSessionStartupInfo->uProtocol);
    }

    /*
     * Spawn a child process for doing the actual session handling.
     * Start by assembling the argument list.
     */
    char szExeName[RTPATH_MAX];
    char *pszExeName = RTProcGetExecutablePath(szExeName, sizeof(szExeName));
    AssertPtrReturn(pszExeName, VERR_FILENAME_TOO_LONG);

    char szParmSessionID[32];
    RTStrPrintf(szParmSessionID, sizeof(szParmSessionID), "--session-id=%RU32", pSessionThread->pStartupInfo->uSessionID);

    char szParmSessionProto[32];
    RTStrPrintf(szParmSessionProto, sizeof(szParmSessionProto), "--session-proto=%RU32",
                pSessionThread->pStartupInfo->uProtocol);
#ifdef DEBUG
    char szParmThreadId[32];
    RTStrPrintf(szParmThreadId, sizeof(szParmThreadId), "--thread-id=%RU32", uCtrlSessionThread);
#endif
    unsigned    idxArg = 0; /* Next index in argument vector. */
    char const *apszArgs[24];

    apszArgs[idxArg++] = pszExeName;
#ifdef VBOXSERVICE_ARG1_UTF8_ARGV
    apszArgs[idxArg++] = VBOXSERVICE_ARG1_UTF8_ARGV; Assert(idxArg == 2);
#endif
    apszArgs[idxArg++] = "guestsession";
    apszArgs[idxArg++] = szParmSessionID;
    apszArgs[idxArg++] = szParmSessionProto;
#ifdef DEBUG
    apszArgs[idxArg++] = szParmThreadId;
#endif
    if (!fAnonymous) /* Do we need to pass a user name? */
    {
        apszArgs[idxArg++] = "--user";
        apszArgs[idxArg++] = pSessionThread->pStartupInfo->pszUser;

        if (strlen(pSessionThread->pStartupInfo->pszDomain))
        {
            apszArgs[idxArg++] = "--domain";
            apszArgs[idxArg++] = pSessionThread->pStartupInfo->pszDomain;
        }
    }

    /* Add same verbose flags as parent process. */
    char szParmVerbose[32];
    if (g_cVerbosity > 0)
    {
        unsigned cVs = RT_MIN(g_cVerbosity, RT_ELEMENTS(szParmVerbose) - 2);
        szParmVerbose[0] = '-';
        memset(&szParmVerbose[1], 'v', cVs);
        szParmVerbose[1 + cVs] = '\0';
        apszArgs[idxArg++] = szParmVerbose;
    }

    /* Add log file handling. Each session will have an own
     * log file, naming based on the parent log file. */
    char szParmLogFile[sizeof(g_szLogFile) + 128];
    if (g_szLogFile[0])
    {
        const char *pszSuffix = RTPathSuffix(g_szLogFile);
        if (!pszSuffix)
            pszSuffix = strchr(g_szLogFile, '\0');
        size_t cchBase = pszSuffix - g_szLogFile;

        RTTIMESPEC Now;
        RTTimeNow(&Now);
        char szTime[64];
        RTTimeSpecToString(&Now, szTime, sizeof(szTime));

        /* Replace out characters not allowed on Windows platforms, put in by RTTimeSpecToString(). */
        static const RTUNICP s_uszValidRangePairs[] =
        {
            ' ', ' ',
            '(', ')',
            '-', '.',
            '0', '9',
            'A', 'Z',
            'a', 'z',
            '_', '_',
            0xa0, 0xd7af,
            '\0'
        };
        ssize_t cReplaced = RTStrPurgeComplementSet(szTime, s_uszValidRangePairs, '_' /* chReplacement */);
        AssertReturn(cReplaced, VERR_INVALID_UTF8_ENCODING);

#ifndef DEBUG
        RTStrPrintf(szParmLogFile, sizeof(szParmLogFile), "%.*s-%RU32-%s-%s%s",
                    cchBase, g_szLogFile, pSessionStartupInfo->uSessionID, pSessionStartupInfo->pszUser, szTime, pszSuffix);
#else
        RTStrPrintf(szParmLogFile, sizeof(szParmLogFile), "%.*s-%RU32-%RU32-%s-%s%s",
                    cchBase, g_szLogFile, pSessionStartupInfo->uSessionID, uCtrlSessionThread,
                    pSessionStartupInfo->pszUser, szTime, pszSuffix);
#endif
        apszArgs[idxArg++] = "--logfile";
        apszArgs[idxArg++] = szParmLogFile;
    }

#ifdef DEBUG
    if (g_Session.fFlags & VBOXSERVICECTRLSESSION_FLAG_DUMPSTDOUT)
        apszArgs[idxArg++] = "--dump-stdout";
    if (g_Session.fFlags & VBOXSERVICECTRLSESSION_FLAG_DUMPSTDERR)
        apszArgs[idxArg++] = "--dump-stderr";
#endif
    apszArgs[idxArg] = NULL;
    Assert(idxArg < RT_ELEMENTS(apszArgs));

    if (g_cVerbosity > 3)
    {
        VGSvcVerbose(4, "Spawning parameters:\n");
        for (idxArg = 0; apszArgs[idxArg]; idxArg++)
            VGSvcVerbose(4, "    %s\n", apszArgs[idxArg]);
    }

    /*
     * Flags.
     */
    uint32_t const fProcCreate = RTPROC_FLAGS_PROFILE
#ifdef RT_OS_WINDOWS
                               | RTPROC_FLAGS_SERVICE
                               | RTPROC_FLAGS_HIDDEN
#endif
                               | VBOXSERVICE_PROC_F_UTF8_ARGV;

    /*
     * Configure standard handles.
     */
    RTHANDLE hStdIn;
    int rc = RTPipeCreate(&hStdIn.u.hPipe, &pSessionThread->hKeyPipe, RTPIPE_C_INHERIT_READ);
    if (RT_SUCCESS(rc))
    {
        hStdIn.enmType = RTHANDLETYPE_PIPE;

        RTHANDLE hStdOutAndErr;
        rc = RTFileOpenBitBucket(&hStdOutAndErr.u.hFile, RTFILE_O_WRITE);
        if (RT_SUCCESS(rc))
        {
            hStdOutAndErr.enmType = RTHANDLETYPE_FILE;

            /*
             * Windows: If a domain name is given, construct an UPN (User Principle Name)
             *          with the domain name built-in, e.g. "joedoe@example.com".
             */
            const char *pszUser    = pSessionThread->pStartupInfo->pszUser;
#ifdef RT_OS_WINDOWS
            char       *pszUserUPN = NULL;
            if (pSessionThread->pStartupInfo->pszDomain[0])
            {
                int cchbUserUPN = RTStrAPrintf(&pszUserUPN, "%s@%s",
                                               pSessionThread->pStartupInfo->pszUser,
                                               pSessionThread->pStartupInfo->pszDomain);
                if (cchbUserUPN > 0)
                {
                    pszUser = pszUserUPN;
                    VGSvcVerbose(3, "Using UPN: %s\n", pszUserUPN);
                }
                else
                    rc = VERR_NO_STR_MEMORY;
            }
            if (RT_SUCCESS(rc))
#endif
            {
                /*
                 * Finally, create the process.
                 */
                rc = RTProcCreateEx(pszExeName, apszArgs, RTENV_DEFAULT, fProcCreate,
                                    &hStdIn, &hStdOutAndErr, &hStdOutAndErr,
                                    !fAnonymous ? pszUser : NULL,
                                    !fAnonymous ? pSessionThread->pStartupInfo->pszPassword : NULL,
                                    NULL /*pvExtraData*/,
                                    &pSessionThread->hProcess);
            }
#ifdef RT_OS_WINDOWS
            RTStrFree(pszUserUPN);
#endif
            RTFileClose(hStdOutAndErr.u.hFile);
        }

        RTPipeClose(hStdIn.u.hPipe);
    }
    return rc;
}


/**
 * Creates a guest session.
 *
 * This will spawn a new VBoxService.exe instance under behalf of the given user
 * which then will act as a session host. On successful open, the session will
 * be added to the given session thread list.
 *
 * @return  VBox status code.
 * @param   pList                   Which list to use to store the session thread in.
 * @param   pSessionStartupInfo     Session startup info.
 * @param   ppSessionThread         Returns newly created session thread on success.
 *                                  Optional.
 */
int VGSvcGstCtrlSessionThreadCreate(PRTLISTANCHOR pList, const PVBGLR3GUESTCTRLSESSIONSTARTUPINFO pSessionStartupInfo,
                                    PVBOXSERVICECTRLSESSIONTHREAD *ppSessionThread)
{
    AssertPtrReturn(pList, VERR_INVALID_POINTER);
    AssertPtrReturn(pSessionStartupInfo, VERR_INVALID_POINTER);
    /* ppSessionThread is optional. */

#ifdef VBOX_STRICT
    /* Check for existing session in debug mode. Should never happen because of
     * Main consistency. */
    PVBOXSERVICECTRLSESSIONTHREAD pSessionCur;
    RTListForEach(pList, pSessionCur, VBOXSERVICECTRLSESSIONTHREAD, Node)
    {
        AssertMsgReturn(   pSessionCur->fStopped == true
                        || pSessionCur->pStartupInfo->uSessionID != pSessionStartupInfo->uSessionID,
                        ("Guest session thread ID=%RU32 already exists (fStopped=%RTbool)\n",
                         pSessionCur->pStartupInfo->uSessionID, pSessionCur->fStopped), VERR_ALREADY_EXISTS);
    }
#endif

    /* Static counter to help tracking session thread <-> process relations. */
    static uint32_t s_uCtrlSessionThread = 0;

    /*
     * Allocate and initialize the session thread structure.
     */
    int rc;
    PVBOXSERVICECTRLSESSIONTHREAD pSessionThread = (PVBOXSERVICECTRLSESSIONTHREAD)RTMemAllocZ(sizeof(*pSessionThread));
    if (pSessionThread)
    {
        //pSessionThread->fShutdown = false;
        //pSessionThread->fStarted  = false;
        //pSessionThread->fStopped  = false;
        pSessionThread->hKeyPipe  = NIL_RTPIPE;
        pSessionThread->Thread    = NIL_RTTHREAD;
        pSessionThread->hProcess  = NIL_RTPROCESS;

        /* Duplicate startup info. */
        pSessionThread->pStartupInfo = VbglR3GuestCtrlSessionStartupInfoDup(pSessionStartupInfo);
        AssertPtrReturn(pSessionThread->pStartupInfo, VERR_NO_MEMORY);

        /* Generate the secret key. */
        RTRandBytes(pSessionThread->abKey, sizeof(pSessionThread->abKey));

        rc = RTCritSectInit(&pSessionThread->CritSect);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            /*
             * Give the session key to the host so it can validate the client.
             */
            if (VbglR3GuestCtrlSupportsOptimizations(g_idControlSvcClient))
            {
                for (uint32_t i = 0; i < 10; i++)
                {
                    rc = VbglR3GuestCtrlSessionPrepare(g_idControlSvcClient, pSessionStartupInfo->uSessionID,
                                                       pSessionThread->abKey, sizeof(pSessionThread->abKey));
                    if (rc != VERR_OUT_OF_RESOURCES)
                        break;
                    RTThreadSleep(100);
                }
            }
            if (RT_SUCCESS(rc))
            {
                s_uCtrlSessionThread++;

                /*
                 * Start the session child process.
                 */
                rc = vgsvcVGSvcGstCtrlSessionThreadCreateProcess(pSessionStartupInfo, pSessionThread, s_uCtrlSessionThread);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Start the session thread.
                     */
                    rc = RTThreadCreateF(&pSessionThread->Thread, vgsvcGstCtrlSessionThread, pSessionThread /*pvUser*/, 0 /*cbStack*/,
                                         RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "gctls%RU32", s_uCtrlSessionThread);
                    if (RT_SUCCESS(rc))
                    {
                        /* Wait for the thread to initialize. */
                        rc = RTThreadUserWait(pSessionThread->Thread, RT_MS_1MIN);
                        if (   RT_SUCCESS(rc)
                            && !ASMAtomicReadBool(&pSessionThread->fShutdown))
                        {
                            VGSvcVerbose(2, "Thread for session ID=%RU32 started\n", pSessionThread->pStartupInfo->uSessionID);

                            ASMAtomicXchgBool(&pSessionThread->fStarted, true);

                            /* Add session to list. */
                            RTListAppend(pList, &pSessionThread->Node);
                            if (ppSessionThread) /* Return session if wanted. */
                                *ppSessionThread = pSessionThread;
                            return VINF_SUCCESS;
                        }

                        /*
                         * Bail out.
                         */
                        VGSvcError("Thread for session ID=%RU32 failed to start, rc=%Rrc\n",
                                   pSessionThread->pStartupInfo->uSessionID, rc);
                        if (RT_SUCCESS_NP(rc))
                            rc = VERR_CANT_CREATE; /** @todo Find a better rc. */
                    }
                    else
                        VGSvcError("Creating session thread failed, rc=%Rrc\n", rc);

                    RTProcTerminate(pSessionThread->hProcess);
                    uint32_t cMsWait = 1;
                    while (   RTProcWait(pSessionThread->hProcess, RTPROCWAIT_FLAGS_NOBLOCK, NULL) == VERR_PROCESS_RUNNING
                           && cMsWait <= 9) /* 1023 ms */
                    {
                        RTThreadSleep(cMsWait);
                        cMsWait <<= 1;
                    }
                }

                if (VbglR3GuestCtrlSupportsOptimizations(g_idControlSvcClient))
                    VbglR3GuestCtrlSessionCancelPrepared(g_idControlSvcClient, pSessionStartupInfo->uSessionID);
            }
            else
                VGSvcVerbose(3, "VbglR3GuestCtrlSessionPrepare failed: %Rrc\n", rc);
            RTPipeClose(pSessionThread->hKeyPipe);
            pSessionThread->hKeyPipe = NIL_RTPIPE;
            RTCritSectDelete(&pSessionThread->CritSect);
        }
        RTMemFree(pSessionThread);
    }
    else
        rc = VERR_NO_MEMORY;

    VGSvcVerbose(3, "Spawning session thread returned returned rc=%Rrc\n", rc);
    return rc;
}


/**
 * Waits for a formerly opened guest session process to close.
 *
 * @return  VBox status code.
 * @param   pThread                 Guest session thread to wait for.
 * @param   uTimeoutMS              Waiting timeout (in ms).
 * @param   fFlags                  Closing flags.
 */
static int VGSvcGstCtrlSessionThreadWait(PVBOXSERVICECTRLSESSIONTHREAD pThread, uint32_t uTimeoutMS, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    AssertPtrReturn(pThread, VERR_INVALID_POINTER);
    /** @todo Validate closing flags. */

    AssertMsgReturn(pThread->Thread != NIL_RTTHREAD,
                    ("Guest session thread of session %p does not exist when it should\n", pThread),
                    VERR_NOT_FOUND);

    int rc = VINF_SUCCESS;

    /*
     * The spawned session process should have received the same closing request,
     * so just wait for the process to close.
     */
    if (ASMAtomicReadBool(&pThread->fStarted))
    {
        /* Ask the thread to shutdown. */
        ASMAtomicXchgBool(&pThread->fShutdown, true);

        VGSvcVerbose(3, "Waiting for session thread ID=%RU32 to close (%RU32ms) ...\n",
                     pThread->pStartupInfo->uSessionID, uTimeoutMS);

        int rcThread;
        rc = RTThreadWait(pThread->Thread, uTimeoutMS, &rcThread);
        if (RT_SUCCESS(rc))
        {
            AssertMsg(pThread->fStopped, ("Thread of session ID=%RU32 not in stopped state when it should\n",
                      pThread->pStartupInfo->uSessionID));

            VGSvcVerbose(3, "Session thread ID=%RU32 ended with rc=%Rrc\n", pThread->pStartupInfo->uSessionID, rcThread);
        }
        else
            VGSvcError("Waiting for session thread ID=%RU32 to close failed with rc=%Rrc\n", pThread->pStartupInfo->uSessionID, rc);
    }
    else
        VGSvcVerbose(3, "Thread for session ID=%RU32 not in started state, skipping wait\n", pThread->pStartupInfo->uSessionID);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Waits for the specified session thread to end and remove
 * it from the session thread list.
 *
 * @return  VBox status code.
 * @param   pThread                 Session thread to destroy.
 * @param   fFlags                  Closing flags.
 */
int VGSvcGstCtrlSessionThreadDestroy(PVBOXSERVICECTRLSESSIONTHREAD pThread, uint32_t fFlags)
{
    AssertPtrReturn(pThread, VERR_INVALID_POINTER);
    AssertPtrReturn(pThread->pStartupInfo, VERR_WRONG_ORDER);

    const uint32_t uSessionID = pThread->pStartupInfo->uSessionID;

    VGSvcVerbose(3, "Destroying session ID=%RU32 ...\n", uSessionID);

    int rc = VGSvcGstCtrlSessionThreadWait(pThread, 5 * 60 * 1000 /* 5 minutes timeout */, fFlags);
    if (RT_SUCCESS(rc))
    {
        VbglR3GuestCtrlSessionStartupInfoFree(pThread->pStartupInfo);
        pThread->pStartupInfo = NULL;

        RTPipeClose(pThread->hKeyPipe);
        pThread->hKeyPipe = NIL_RTPIPE;

        RTCritSectDelete(&pThread->CritSect);

        /* Remove session from list and destroy object. */
        RTListNodeRemove(&pThread->Node);

        RTMemFree(pThread);
        pThread = NULL;
    }

    VGSvcVerbose(3, "Destroyed session ID=%RU32 with %Rrc\n", uSessionID, rc);
    return rc;
}

/**
 * Close all open guest session threads.
 *
 * @note    Caller is responsible for locking!
 *
 * @return  VBox status code.
 * @param   pList                   Which list to close the session threads for.
 * @param   fFlags                  Closing flags.
 */
int VGSvcGstCtrlSessionThreadDestroyAll(PRTLISTANCHOR pList, uint32_t fFlags)
{
    AssertPtrReturn(pList, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    /*int rc = VbglR3GuestCtrlClose
        if (RT_FAILURE(rc))
            VGSvcError("Cancelling pending waits failed; rc=%Rrc\n", rc);*/

    PVBOXSERVICECTRLSESSIONTHREAD pSessIt;
    PVBOXSERVICECTRLSESSIONTHREAD pSessItNext;
    RTListForEachSafe(pList, pSessIt, pSessItNext, VBOXSERVICECTRLSESSIONTHREAD, Node)
    {
        int rc2 = VGSvcGstCtrlSessionThreadDestroy(pSessIt, fFlags);
        if (RT_FAILURE(rc2))
        {
            VGSvcError("Closing session thread '%s' failed with rc=%Rrc\n", RTThreadGetName(pSessIt->Thread), rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
            /* Keep going. */
        }
    }

    VGSvcVerbose(4, "Destroying guest session threads ended with %Rrc\n", rc);
    return rc;
}


/**
 * Main function for the session process.
 *
 * @returns exit code.
 * @param   argc        Argument count.
 * @param   argv        Argument vector (UTF-8).
 */
RTEXITCODE VGSvcGstCtrlSessionSpawnInit(int argc, char **argv)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--domain",          VBOXSERVICESESSIONOPT_DOMAIN,          RTGETOPT_REQ_STRING },
#ifdef DEBUG
        { "--dump-stdout",     VBOXSERVICESESSIONOPT_DUMP_STDOUT,     RTGETOPT_REQ_NOTHING },
        { "--dump-stderr",     VBOXSERVICESESSIONOPT_DUMP_STDERR,     RTGETOPT_REQ_NOTHING },
#endif
        { "--logfile",         VBOXSERVICESESSIONOPT_LOG_FILE,        RTGETOPT_REQ_STRING },
        { "--user",            VBOXSERVICESESSIONOPT_USERNAME,        RTGETOPT_REQ_STRING },
        { "--session-id",      VBOXSERVICESESSIONOPT_SESSION_ID,      RTGETOPT_REQ_UINT32 },
        { "--session-proto",   VBOXSERVICESESSIONOPT_SESSION_PROTO,   RTGETOPT_REQ_UINT32 },
#ifdef DEBUG
        { "--thread-id",       VBOXSERVICESESSIONOPT_THREAD_ID,       RTGETOPT_REQ_UINT32 },
#endif /* DEBUG */
        { "--verbose",         'v',                                   RTGETOPT_REQ_NOTHING }
    };

    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv,
                 s_aOptions, RT_ELEMENTS(s_aOptions),
                 1 /*iFirst*/, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    uint32_t fSession = VBOXSERVICECTRLSESSION_FLAG_SPAWN;

    /* Protocol and session ID must be specified explicitly. */
    g_Session.StartupInfo.uProtocol  = UINT32_MAX;
    g_Session.StartupInfo.uSessionID = UINT32_MAX;

    int ch;
    RTGETOPTUNION ValueUnion;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            case VBOXSERVICESESSIONOPT_DOMAIN:
                /* Information not needed right now, skip. */
                break;
#ifdef DEBUG
            case VBOXSERVICESESSIONOPT_DUMP_STDOUT:
                fSession |= VBOXSERVICECTRLSESSION_FLAG_DUMPSTDOUT;
                break;

            case VBOXSERVICESESSIONOPT_DUMP_STDERR:
                fSession |= VBOXSERVICECTRLSESSION_FLAG_DUMPSTDERR;
                break;
#endif
            case VBOXSERVICESESSIONOPT_SESSION_ID:
                g_Session.StartupInfo.uSessionID = ValueUnion.u32;
                break;

            case VBOXSERVICESESSIONOPT_SESSION_PROTO:
                g_Session.StartupInfo.uProtocol = ValueUnion.u32;
                break;
#ifdef DEBUG
            case VBOXSERVICESESSIONOPT_THREAD_ID:
                /* Not handled. Mainly for processs listing. */
                break;
#endif
            case VBOXSERVICESESSIONOPT_LOG_FILE:
            {
                int rc = RTStrCopy(g_szLogFile, sizeof(g_szLogFile), ValueUnion.psz);
                if (RT_FAILURE(rc))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error copying log file name: %Rrc", rc);
                break;
            }

            case VBOXSERVICESESSIONOPT_USERNAME:
                /* Information not needed right now, skip. */
                break;

            /** @todo Implement help? */

            case 'v':
                g_cVerbosity++;
                break;

            case VINF_GETOPT_NOT_OPTION:
            {
                if (!RTStrICmp(ValueUnion.psz, VBOXSERVICECTRLSESSION_GETOPT_PREFIX))
                    break;
                /* else fall through and bail out. */
                RT_FALL_THROUGH();
            }
            default:
                return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown argument '%s'", ValueUnion.psz);
        }
    }

    /* Check that we've got all the required options. */
    if (g_Session.StartupInfo.uProtocol == UINT32_MAX)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No protocol version specified");

    if (g_Session.StartupInfo.uSessionID == UINT32_MAX)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "No session ID specified");

    /* Init the session object. */
    int rc = VGSvcGstCtrlSessionInit(&g_Session, fSession);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_INIT, "Failed to initialize session object, rc=%Rrc\n", rc);

    rc = VGSvcLogCreate(g_szLogFile[0] ? g_szLogFile : NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_INIT, "Failed to create log file '%s', rc=%Rrc\n",
                              g_szLogFile[0] ? g_szLogFile : "<None>", rc);

    RTEXITCODE rcExit = vgsvcGstCtrlSessionSpawnWorker(&g_Session);

    VGSvcLogDestroy();
    return rcExit;
}

