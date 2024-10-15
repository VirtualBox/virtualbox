/* $Id: VBoxGuestR3LibDragAndDrop.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, Drag & Drop.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <iprt/path.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/uri.h>
#include <iprt/thread.h>

#include <iprt/cpp/list.h>
#include <iprt/cpp/ministring.h>

#ifdef LOG_GROUP
 #undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_GUEST_DND
#include <VBox/log.h>

#include <VBox/VBoxGuestLib.h>
#include <VBox/GuestHost/DragAndDrop.h>
#include <VBox/HostServices/DragAndDropSvc.h>

using namespace DragAndDropSvc;

#include "VBoxGuestR3LibInternal.h"


/*********************************************************************************************************************************
*   Private internal functions                                                                                                   *
*********************************************************************************************************************************/

/**
 * Receives the next upcoming message for a given DnD context.
 *
 * @returns IPRT status code.
 *          Will return VERR_CANCELLED (implemented by the host service) if we need to bail out.
 * @param   pCtx                DnD context to use.
 * @param   puMsg               Where to store the message type.
 * @param   pcParms             Where to store the number of parameters required for receiving the message.
 * @param   fWait               Whether to wait (block) for a new message to arrive or not.
 */
static int vbglR3DnDGetNextMsgType(PVBGLR3GUESTDNDCMDCTX pCtx, uint32_t *puMsg, uint32_t *pcParms, bool fWait)
{
    AssertPtrReturn(pCtx,    VERR_INVALID_POINTER);
    AssertPtrReturn(puMsg,   VERR_INVALID_POINTER);
    AssertPtrReturn(pcParms, VERR_INVALID_POINTER);

    int rc;

    do
    {
        HGCMMsgGetNext Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_DND_FN_GET_NEXT_HOST_MSG, 3);
        Msg.uMsg.SetUInt32(0);
        Msg.cParms.SetUInt32(0);
        Msg.fBlock.SetUInt32(fWait ? 1 : 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            rc = Msg.uMsg.GetUInt32(puMsg);     AssertRC(rc);
            rc = Msg.cParms.GetUInt32(pcParms); AssertRC(rc);
        }

        LogRel(("DnD: Received message %s (%#x) from host\n", DnDHostMsgToStr(*puMsg), *puMsg));

    } while (rc == VERR_INTERRUPTED);

    return rc;
}


/**
 * Sends a DnD error back to the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   rcErr               Error (IPRT-style) to send.
 */
VBGLR3DECL(int) VbglR3DnDSendError(PVBGLR3GUESTDNDCMDCTX pCtx, int rcErr)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMMsgGHError Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_DND_FN_EVT_ERROR, 2);
    /** @todo Context ID not used yet. */
    Msg.u.v3.uContext.SetUInt32(0);
    Msg.u.v3.rc.SetUInt32((uint32_t)rcErr); /* uint32_t vs. int. */

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));

    /*
     * Never return an error if the host did not accept the error at the current
     * time.  This can be due to the host not having any appropriate callbacks
     * set which would handle that error.
     *
     * bird: Looks like VERR_NOT_SUPPORTED is what the host will return if it
     *       doesn't an appropriate callback.  The code used to ignore ALL errors
     *       the host would return, also relevant ones.
     */
    if (RT_FAILURE(rc))
        LogFlowFunc(("Sending error %Rrc failed with rc=%Rrc\n", rcErr, rc));
    if (rc == VERR_NOT_SUPPORTED)
        rc = VINF_SUCCESS;

    return rc;
}

/**
 * Host -> Guest
 * Utility function to receive a so-called "action message" from the host.
 * Certain DnD messages use the same amount / sort of parameters and grouped as "action messages".
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   uMsg                Which kind of message to receive.
 * @param   puScreenID          Where to store the host screen ID the message is bound to. Optional.
 * @param   puX                 Where to store the absolute X coordinates. Optional.
 * @param   puY                 Where to store the absolute Y coordinates. Optional.
 * @param   puDefAction         Where to store the default action to perform. Optional.
 * @param   puAllActions        Where to store the available actions. Optional.
 * @param   ppszFormats         Where to store List of formats. Optional.
 * @param   pcbFormats          Size (in bytes) of where to store the list of formats. Optional.
 *
 * @todo r=andy Get rid of this function as soon as we resolved the protocol TODO #1.
 *              This was part of the initial protocol and needs to go.
 */
static int vbglR3DnDHGRecvAction(PVBGLR3GUESTDNDCMDCTX pCtx,
                                 uint32_t   uMsg,
                                 uint32_t  *puScreenID,
                                 uint32_t  *puX,
                                 uint32_t  *puY,
                                 uint32_t  *puDefAction,
                                 uint32_t  *puAllActions,
                                 char     **ppszFormats,
                                 uint32_t  *pcbFormats)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    /* The rest is optional. */

    const uint32_t cbFormatsTmp = pCtx->cbMaxChunkSize;

    char *pszFormatsTmp = static_cast<char *>(RTMemAlloc(cbFormatsTmp));
    if (!pszFormatsTmp)
        return VERR_NO_MEMORY;

    HGCMMsgHGAction Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, uMsg, 8);
    Msg.u.v3.uContext.SetUInt32(0);
    Msg.u.v3.uScreenId.SetUInt32(0);
    Msg.u.v3.uX.SetUInt32(0);
    Msg.u.v3.uY.SetUInt32(0);
    Msg.u.v3.uDefAction.SetUInt32(0);
    Msg.u.v3.uAllActions.SetUInt32(0);
    Msg.u.v3.pvFormats.SetPtr(pszFormatsTmp, cbFormatsTmp);
    Msg.u.v3.cbFormats.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        /** @todo Context ID not used yet. */
        if (RT_SUCCESS(rc) && puScreenID)
            rc = Msg.u.v3.uScreenId.GetUInt32(puScreenID);
        if (RT_SUCCESS(rc) && puX)
            rc = Msg.u.v3.uX.GetUInt32(puX);
        if (RT_SUCCESS(rc) && puY)
            rc = Msg.u.v3.uY.GetUInt32(puY);
        if (RT_SUCCESS(rc) && puDefAction)
            rc = Msg.u.v3.uDefAction.GetUInt32(puDefAction);
        if (RT_SUCCESS(rc) && puAllActions)
            rc = Msg.u.v3.uAllActions.GetUInt32(puAllActions);
        if (RT_SUCCESS(rc) && pcbFormats)
            rc = Msg.u.v3.cbFormats.GetUInt32(pcbFormats);

        if (RT_SUCCESS(rc))
        {
            if (ppszFormats)
            {
                *ppszFormats = RTStrDup(pszFormatsTmp);
                if (!*ppszFormats)
                    rc = VERR_NO_MEMORY;
            }
        }
    }

    RTStrFree(pszFormatsTmp);

    return rc;
}

/**
 * Host -> Guest
 * Utility function to receive a HOST_DND_FN_HG_EVT_LEAVE message from the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 */
static int vbglR3DnDHGRecvLeave(PVBGLR3GUESTDNDCMDCTX pCtx)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMMsgHGLeave Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, HOST_DND_FN_HG_EVT_LEAVE, 1);
    /** @todo Context ID not used yet. */
    Msg.u.v3.uContext.SetUInt32(0);

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}

/**
 * Host -> Guest
 * Utility function to receive a HOST_DND_FN_HG_EVT_CANCEL message from the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 */
static int vbglR3DnDHGRecvCancel(PVBGLR3GUESTDNDCMDCTX pCtx)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMMsgHGCancel Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, HOST_DND_FN_CANCEL, 1);
    /** @todo Context ID not used yet. */
    Msg.u.v3.uContext.SetUInt32(0);

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}

/**
 * Host -> Guest
 * Utility function to receive a HOST_DND_FN_HG_SND_DIR message from the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   pszDirname          Where to store the directory name of the directory being created.
 * @param   cbDirname           Size (in bytes) of where to store the directory name of the directory being created.
 * @param   pcbDirnameRecv      Size (in bytes) of the actual directory name received.
 * @param   pfMode              Where to store the directory creation mode.
 */
static int vbglR3DnDHGRecvDir(PVBGLR3GUESTDNDCMDCTX pCtx,
                              char     *pszDirname,
                              uint32_t  cbDirname,
                              uint32_t *pcbDirnameRecv,
                              uint32_t *pfMode)
{
    AssertPtrReturn(pCtx,           VERR_INVALID_POINTER);
    AssertPtrReturn(pszDirname,     VERR_INVALID_POINTER);
    AssertReturn(cbDirname,         VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbDirnameRecv, VERR_INVALID_POINTER);
    AssertPtrReturn(pfMode,         VERR_INVALID_POINTER);

    HGCMMsgHGSendDir Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, HOST_DND_FN_HG_SND_DIR, 4);
    /** @todo Context ID not used yet. */
    Msg.u.v3.uContext.SetUInt32(0);
    Msg.u.v3.pvName.SetPtr(pszDirname, cbDirname);
    Msg.u.v3.cbName.SetUInt32(cbDirname);
    Msg.u.v3.fMode.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        /** @todo Context ID not used yet. */
        rc = Msg.u.v3.cbName.GetUInt32(pcbDirnameRecv); AssertRC(rc);
        rc = Msg.u.v3.fMode.GetUInt32(pfMode);          AssertRC(rc);

        AssertReturn(cbDirname >= *pcbDirnameRecv, VERR_TOO_MUCH_DATA);
    }

    return rc;
}

/**
 * Host -> Guest
 * Utility function to receive a HOST_DND_FN_HG_SND_FILE_DATA message from the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   pvData              Where to store the file data chunk.
 * @param   cbData              Size (in bytes) of where to store the data chunk.
 * @param   pcbDataRecv         Size (in bytes) of the actual data chunk size received.
 */
static int vbglR3DnDHGRecvFileData(PVBGLR3GUESTDNDCMDCTX pCtx,
                                   void                 *pvData,
                                   uint32_t              cbData,
                                   uint32_t             *pcbDataRecv)
{
    AssertPtrReturn(pCtx,            VERR_INVALID_POINTER);
    AssertPtrReturn(pvData,          VERR_INVALID_POINTER);
    AssertReturn(cbData,             VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbDataRecv,     VERR_INVALID_POINTER);

    HGCMMsgHGSendFileData Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, HOST_DND_FN_HG_SND_FILE_DATA, 5);
    Msg.u.v3.uContext.SetUInt32(0);
    Msg.u.v3.pvData.SetPtr(pvData, cbData);
    Msg.u.v3.cbData.SetUInt32(0);
    Msg.u.v3.pvChecksum.SetPtr(NULL, 0);
    Msg.u.v3.cbChecksum.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        /** @todo Context ID not used yet. */
        rc = Msg.u.v3.cbData.GetUInt32(pcbDataRecv); AssertRC(rc);
        AssertReturn(cbData >= *pcbDataRecv, VERR_TOO_MUCH_DATA);
        /** @todo Add checksum support. */
    }

    return rc;
}

/**
 * Host -> Guest
 * Utility function to receive the HOST_DND_FN_HG_SND_FILE_HDR message from the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   pszFilename         Where to store the file name of the file being transferred.
 * @param   cbFilename          Size (in bytes) of where to store the file name of the file being transferred.
 * @param   puFlags             File transfer flags. Currently not being used.
 * @param   pfMode              Where to store the file creation mode.
 * @param   pcbTotal            Where to store the file size (in bytes).
 */
static int vbglR3DnDHGRecvFileHdr(PVBGLR3GUESTDNDCMDCTX  pCtx,
                                  char                  *pszFilename,
                                  uint32_t               cbFilename,
                                  uint32_t              *puFlags,
                                  uint32_t              *pfMode,
                                  uint64_t              *pcbTotal)
{
    AssertPtrReturn(pCtx,        VERR_INVALID_POINTER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(cbFilename,     VERR_INVALID_PARAMETER);
    AssertPtrReturn(puFlags,     VERR_INVALID_POINTER);
    AssertPtrReturn(pfMode,      VERR_INVALID_POINTER);
    AssertReturn(pcbTotal,       VERR_INVALID_POINTER);

    HGCMMsgHGSendFileHdr Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, HOST_DND_FN_HG_SND_FILE_HDR, 6);
    Msg.uContext.SetUInt32(0); /** @todo Not used yet. */
    Msg.pvName.SetPtr(pszFilename, cbFilename);
    Msg.cbName.SetUInt32(cbFilename);
    Msg.uFlags.SetUInt32(0);
    Msg.fMode.SetUInt32(0);
    Msg.cbTotal.SetUInt64(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        /** @todo Get context ID. */
        rc = Msg.uFlags.GetUInt32(puFlags);   AssertRC(rc);
        rc = Msg.fMode.GetUInt32(pfMode);     AssertRC(rc);
        rc = Msg.cbTotal.GetUInt64(pcbTotal); AssertRC(rc);
    }

    return rc;
}

/**
 * Host -> Guest
 * Helper function for receiving URI data from the host. Do not call directly.
 * This function also will take care of the file creation / locking on the guest.
 *
 * @returns IPRT status code.
 * @retval  VERR_CANCELLED if the transfer was cancelled by the host.
 * @param   pCtx                DnD context to use.
 * @param   pDataHdr            DnD data header to use. Needed for accounting.
 * @param   pDroppedFiles       Dropped files object to use for maintaining the file creation / locking.
 */
static int vbglR3DnDHGRecvURIData(PVBGLR3GUESTDNDCMDCTX pCtx, PVBOXDNDSNDDATAHDR pDataHdr, PDNDDROPPEDFILES pDroppedFiles)
{
    AssertPtrReturn(pCtx,          VERR_INVALID_POINTER);
    AssertPtrReturn(pDataHdr,      VERR_INVALID_POINTER);
    AssertPtrReturn(pDroppedFiles, VERR_INVALID_POINTER);

    /* Only count the raw data minus the already received meta data. */
    Assert(pDataHdr->cbTotal >= pDataHdr->cbMeta);
    uint64_t cbToRecvBytes = pDataHdr->cbTotal - pDataHdr->cbMeta;
    uint64_t cToRecvObjs   = pDataHdr->cObjects;

    LogFlowFunc(("cbToRecvBytes=%RU64, cToRecvObjs=%RU64, (cbTotal=%RU64, cbMeta=%RU32)\n",
                 cbToRecvBytes, cToRecvObjs, pDataHdr->cbTotal, pDataHdr->cbMeta));

    /* Anything to do at all? */
    /* Note: Do not check for cbToRecvBytes == 0 here, as this might be just
     *       a bunch of 0-byte files to be transferred. */
    if (!cToRecvObjs)
        return VINF_SUCCESS;

    LogRel2(("DnD: Receiving URI data started\n"));

    /*
     * Allocate temporary chunk buffer.
     */
    uint32_t cbChunkMax = pCtx->cbMaxChunkSize;
    void *pvChunk = RTMemAlloc(cbChunkMax);
    if (!pvChunk)
        return VERR_NO_MEMORY;
    uint32_t cbChunkRead   = 0;

    uint64_t cbFileSize    = 0; /* Total file size (in bytes). */
    uint64_t cbFileWritten = 0; /* Written bytes. */

    const char *pszDropDir = DnDDroppedFilesGetDirAbs(pDroppedFiles);
    AssertPtr(pszDropDir);

    int rc;

    /*
     * Enter the main loop of retieving files + directories.
     */
    DNDTRANSFEROBJECT objCur;
    RT_ZERO(objCur);

    char szPathName[RTPATH_MAX] = { 0 };
    uint32_t cbPathName = 0;
    uint32_t fFlags     = 0;
    uint32_t fMode      = 0;

    do
    {
        LogFlowFunc(("Waiting for new message ...\n"));

        uint32_t uNextMsg;
        uint32_t cNextParms;
        rc = vbglR3DnDGetNextMsgType(pCtx, &uNextMsg, &cNextParms, true /* fWait */);
        if (RT_SUCCESS(rc))
        {
            LogFlowFunc(("uNextMsg=%RU32, cNextParms=%RU32\n", uNextMsg, cNextParms));

            switch (uNextMsg)
            {
                case HOST_DND_FN_HG_SND_DIR:
                {
                    rc = vbglR3DnDHGRecvDir(pCtx,
                                            szPathName,
                                            sizeof(szPathName),
                                            &cbPathName,
                                            &fMode);
                    LogFlowFunc(("HOST_DND_FN_HG_SND_DIR: "
                                 "pszPathName=%s, cbPathName=%RU32, fMode=0x%x, rc=%Rrc\n",
                                 szPathName, cbPathName, fMode, rc));

                    char *pszPathAbs = RTPathJoinA(pszDropDir, szPathName);
                    if (pszPathAbs)
                    {
#ifdef RT_OS_WINDOWS
                        uint32_t fCreationMode = (fMode & RTFS_DOS_MASK) | RTFS_DOS_NT_NORMAL;
#else
                        uint32_t fCreationMode = (fMode & RTFS_UNIX_MASK) | RTFS_UNIX_IRWXU;
#endif
                        rc = RTDirCreate(pszPathAbs, fCreationMode, 0);
                        if (RT_SUCCESS(rc))
                            rc = DnDDroppedFilesAddDir(pDroppedFiles, pszPathAbs);

                        if (RT_SUCCESS(rc))
                        {
                            Assert(cToRecvObjs);
                            cToRecvObjs--;
                        }

                        RTStrFree(pszPathAbs);
                    }
                    else
                        rc = VERR_NO_MEMORY;
                    break;
                }
                case HOST_DND_FN_HG_SND_FILE_HDR:
                    RT_FALL_THROUGH();
                case HOST_DND_FN_HG_SND_FILE_DATA:
                {
                    if (uNextMsg == HOST_DND_FN_HG_SND_FILE_HDR)
                    {
                        rc = vbglR3DnDHGRecvFileHdr(pCtx,
                                                    szPathName,
                                                    sizeof(szPathName),
                                                    &fFlags,
                                                    &fMode,
                                                    &cbFileSize);
                        LogFlowFunc(("HOST_DND_FN_HG_SND_FILE_HDR: "
                                     "szPathName=%s, fFlags=0x%x, fMode=0x%x, cbFileSize=%RU64, rc=%Rrc\n",
                                     szPathName, fFlags, fMode, cbFileSize, rc));
                    }
                    else
                    {
                        rc = vbglR3DnDHGRecvFileData(pCtx,
                                                     pvChunk,
                                                     cbChunkMax,
                                                     &cbChunkRead);
                        LogFlowFunc(("HOST_DND_FN_HG_SND_FILE_DATA: "
                                     "cbChunkRead=%RU32, rc=%Rrc\n", cbChunkRead, rc));
                    }

                    if (   RT_SUCCESS(rc)
                        && uNextMsg == HOST_DND_FN_HG_SND_FILE_HDR)
                    {
                        char *pszPathAbs = RTPathJoinA(pszDropDir, szPathName);
                        if (pszPathAbs)
                        {
                            LogFlowFunc(("Opening pszPathName=%s, cbPathName=%RU32, fMode=0x%x, cbFileSize=%zu\n",
                                         szPathName, cbPathName, fMode, cbFileSize));

                            uint64_t fOpen  =   RTFILE_O_WRITE | RTFILE_O_DENY_WRITE
                                              | RTFILE_O_CREATE_REPLACE;

                            /* Is there already a file open, e.g. in transfer? */
                            if (!DnDTransferObjectIsOpen(&objCur))
                            {
#ifdef RT_OS_WINDOWS
                                uint32_t fCreationMode = (fMode & RTFS_DOS_MASK) | RTFS_DOS_NT_NORMAL;
#else
                                uint32_t fCreationMode = (fMode & RTFS_UNIX_MASK) | RTFS_UNIX_IRUSR | RTFS_UNIX_IWUSR;
#endif
                                rc = DnDTransferObjectInitEx(&objCur, DNDTRANSFEROBJTYPE_FILE,
                                                             pszDropDir /* Source (base) path */, szPathName /* Destination path */);
                                if (RT_SUCCESS(rc))
                                {
                                    rc = DnDTransferObjectOpen(&objCur, fOpen, fCreationMode, DNDTRANSFEROBJECT_FLAGS_NONE);
                                    if (RT_SUCCESS(rc))
                                    {
                                        rc = DnDDroppedFilesAddFile(pDroppedFiles, pszPathAbs);
                                        if (RT_SUCCESS(rc))
                                        {
                                            cbFileWritten = 0;
                                            DnDTransferObjectSetSize(&objCur, cbFileSize);
                                        }
                                    }
                                }
                            }
                            else
                            {
                                AssertMsgFailed(("ObjType=%RU32\n", DnDTransferObjectGetType(&objCur)));
                                rc = VERR_WRONG_ORDER;
                            }

                            RTStrFree(pszPathAbs);
                        }
                        else
                            rc = VERR_NO_MEMORY;
                    }

                    if (   RT_SUCCESS(rc)
                        && uNextMsg == HOST_DND_FN_HG_SND_FILE_DATA
                        && cbChunkRead)
                    {
                        uint32_t cbChunkWritten;
                        rc = DnDTransferObjectWrite(&objCur, pvChunk, cbChunkRead, &cbChunkWritten);
                        if (RT_SUCCESS(rc))
                        {
                            LogFlowFunc(("HOST_DND_FN_HG_SND_FILE_DATA: "
                                         "cbChunkRead=%RU32, cbChunkWritten=%RU32, cbFileWritten=%RU64 cbFileSize=%RU64\n",
                                         cbChunkRead, cbChunkWritten, cbFileWritten + cbChunkWritten, cbFileSize));

                            cbFileWritten += cbChunkWritten;

                            Assert(cbChunkRead <= cbToRecvBytes);
                            cbToRecvBytes -= cbChunkRead;
                        }
                    }

                    /* Data transfer complete? Close the file. */
                    bool fClose = DnDTransferObjectIsComplete(&objCur);
                    if (fClose)
                    {
                        Assert(cToRecvObjs);
                        cToRecvObjs--;
                    }

                    /* Only since protocol v2 we know the file size upfront. */
                    Assert(cbFileWritten <= cbFileSize);

                    if (fClose)
                    {
                        LogFlowFunc(("Closing file\n"));
                        DnDTransferObjectDestroy(&objCur);
                    }

                    break;
                }
                case HOST_DND_FN_CANCEL:
                {
                    rc = vbglR3DnDHGRecvCancel(pCtx);
                    if (RT_SUCCESS(rc))
                        rc = VERR_CANCELLED;
                    break;
                }
                default:
                {
                    LogRel(("DnD: Warning: Message %s (%#x) from host not supported or in wrong order\n", DnDHostMsgToStr(uNextMsg), uNextMsg));
                    rc = VERR_NOT_SUPPORTED;
                    break;
                }
            }
        }

        if (RT_FAILURE(rc))
            break;

        LogFlowFunc(("cbToRecvBytes=%RU64, cToRecvObjs=%RU64\n", cbToRecvBytes, cToRecvObjs));
        if (   !cbToRecvBytes
            && !cToRecvObjs)
        {
            break;
        }

    } while (RT_SUCCESS(rc));

    LogFlowFunc(("Loop ended with %Rrc\n", rc));

    /* All URI data processed? */
    if (rc == VERR_NO_DATA)
        rc = VINF_SUCCESS;

    /* Delete temp buffer again. */
    if (pvChunk)
        RTMemFree(pvChunk);

    /* Cleanup on failure or if the user has canceled the operation or
     * something else went wrong. */
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_CANCELLED)
            LogRel2(("DnD: Receiving URI data was cancelled by the host\n"));
        else
            LogRel(("DnD: Receiving URI data failed with %Rrc\n", rc));

        DnDTransferObjectDestroy(&objCur);
        DnDDroppedFilesRollback(pDroppedFiles);
    }
    else
    {
        LogRel2(("DnD: Receiving URI data finished\n"));

        /** @todo Compare the transfer list with the dirs/files we really transferred. */
        /** @todo Implement checksum verification, if any. */
    }

    /*
     * Close the dropped files directory.
     * Don't try to remove it here, however, as the files are being needed
     * by the client's drag'n drop operation lateron.
     */
    int rc2 = DnDDroppedFilesReset(pDroppedFiles, false /* fRemoveDropDir */);
    if (RT_FAILURE(rc2)) /* Not fatal, don't report back to host. */
        LogFlowFunc(("Closing dropped files directory failed with %Rrc\n", rc2));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Host -> Guest
 * Utility function to receive the HOST_DND_FN_HG_SND_DATA message from the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   pDataHdr            DnD data header to use. Need for accounting and stuff.
 * @param   pvData              Where to store the received data from the host.
 * @param   cbData              Size (in bytes) of where to store the received data.
 * @param   pcbDataRecv         Where to store the received amount of data (in bytes).
 */
static int vbglR3DnDHGRecvDataRaw(PVBGLR3GUESTDNDCMDCTX pCtx, PVBOXDNDSNDDATAHDR pDataHdr,
                                  void *pvData, uint32_t cbData, uint32_t *pcbDataRecv)
{
    AssertPtrReturn(pCtx,            VERR_INVALID_POINTER);
    AssertPtrReturn(pDataHdr,        VERR_INVALID_POINTER);
    AssertPtrReturn(pvData,          VERR_INVALID_POINTER);
    AssertReturn(cbData,             VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pcbDataRecv, VERR_INVALID_POINTER);

    LogFlowFunc(("pvDate=%p, cbData=%RU32\n", pvData, cbData));

    HGCMMsgHGSendData Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, HOST_DND_FN_HG_SND_DATA, 5);
    Msg.u.v3.uContext.SetUInt32(0);
    Msg.u.v3.pvData.SetPtr(pvData, cbData);
    Msg.u.v3.cbData.SetUInt32(0);
    Msg.u.v3.pvChecksum.SetPtr(NULL, 0);
    Msg.u.v3.cbChecksum.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        uint32_t cbDataRecv;
        rc = Msg.u.v3.cbData.GetUInt32(&cbDataRecv);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            /** @todo Use checksum for validating the received data. */
            if (pcbDataRecv)
                *pcbDataRecv = cbDataRecv;
            LogFlowFuncLeaveRC(rc);
            return rc;
        }
    }

    /* failure */
    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Host -> Guest
 * Utility function to receive the HOST_DND_FN_HG_SND_DATA_HDR message from the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   pDataHdr            Where to store the receivd DnD data header.
 */
static int vbglR3DnDHGRecvDataHdr(PVBGLR3GUESTDNDCMDCTX pCtx, PVBOXDNDSNDDATAHDR pDataHdr)
{
    AssertPtrReturn(pCtx,     VERR_INVALID_POINTER);
    AssertPtrReturn(pDataHdr, VERR_INVALID_POINTER);

    Assert(pCtx->uProtocolDeprecated >= 3); /* Only for protocol v3 and up. */

    HGCMMsgHGSendDataHdr Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, HOST_DND_FN_HG_SND_DATA_HDR, 12);
    Msg.uContext.SetUInt32(0);
    Msg.uFlags.SetUInt32(0);
    Msg.uScreenId.SetUInt32(0);
    Msg.cbTotal.SetUInt64(0);
    Msg.cbMeta.SetUInt32(0);
    Msg.pvMetaFmt.SetPtr(pDataHdr->pvMetaFmt, pDataHdr->cbMetaFmt);
    Msg.cbMetaFmt.SetUInt32(0);
    Msg.cObjects.SetUInt64(0);
    Msg.enmCompression.SetUInt32(0);
    Msg.enmChecksumType.SetUInt32(0);
    Msg.pvChecksum.SetPtr(pDataHdr->pvChecksum, pDataHdr->cbChecksum);
    Msg.cbChecksum.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        /* Msg.uContext not needed here. */
        Msg.uFlags.GetUInt32(&pDataHdr->uFlags);
        Msg.uScreenId.GetUInt32(&pDataHdr->uScreenId);
        Msg.cbTotal.GetUInt64(&pDataHdr->cbTotal);
        Msg.cbMeta.GetUInt32(&pDataHdr->cbMeta);
        Msg.cbMetaFmt.GetUInt32(&pDataHdr->cbMetaFmt);
        Msg.cObjects.GetUInt64(&pDataHdr->cObjects);
        Msg.enmCompression.GetUInt32(&pDataHdr->enmCompression);
        Msg.enmChecksumType.GetUInt32((uint32_t *)&pDataHdr->enmChecksumType);
        Msg.cbChecksum.GetUInt32(&pDataHdr->cbChecksum);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Host -> Guest
 * Helper function for receiving the actual DnD data from the host. Do not call directly.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   pDataHdr            Where to store the data header data.
 * @param   ppvData             Returns the received meta data. Needs to be free'd by the caller.
 * @param   pcbData             Where to store the size (in bytes) of the received meta data.
 */
static int vbglR3DnDHGRecvDataLoop(PVBGLR3GUESTDNDCMDCTX pCtx, PVBOXDNDSNDDATAHDR pDataHdr,
                                   void **ppvData, uint64_t *pcbData)
{
    AssertPtrReturn(pCtx,     VERR_INVALID_POINTER);
    AssertPtrReturn(pDataHdr, VERR_INVALID_POINTER);
    AssertPtrReturn(ppvData,  VERR_INVALID_POINTER);
    AssertPtrReturn(pcbData,  VERR_INVALID_POINTER);

    int rc;
    uint32_t cbDataRecv;

    LogFlowFuncEnter();

    rc = vbglR3DnDHGRecvDataHdr(pCtx, pDataHdr);
    if (RT_FAILURE(rc))
        return rc;

    LogFlowFunc(("cbTotal=%RU64, cbMeta=%RU32, cObjects=%RU32\n", pDataHdr->cbTotal, pDataHdr->cbMeta, pDataHdr->cObjects));
    if (pDataHdr->cbMeta)
    {
        uint64_t cbDataTmp = 0;
        void    *pvDataTmp = RTMemAlloc(pDataHdr->cbMeta);
        if (!pvDataTmp)
            rc = VERR_NO_MEMORY;

        if (RT_SUCCESS(rc))
        {
            uint8_t *pvDataOff = (uint8_t *)pvDataTmp;
            while (cbDataTmp < pDataHdr->cbMeta)
            {
                rc = vbglR3DnDHGRecvDataRaw(pCtx, pDataHdr,
                                            pvDataOff, RT_MIN(pDataHdr->cbMeta - cbDataTmp, pCtx->cbMaxChunkSize),
                                            &cbDataRecv);
                if (RT_SUCCESS(rc))
                {
                    LogFlowFunc(("cbDataRecv=%RU32, cbDataTmp=%RU64\n", cbDataRecv, cbDataTmp));
                    Assert(cbDataTmp + cbDataRecv <= pDataHdr->cbMeta);
                    cbDataTmp += cbDataRecv;
                    pvDataOff += cbDataRecv;
                }
                else
                    break;
            }

            if (RT_SUCCESS(rc))
            {
                Assert(cbDataTmp == pDataHdr->cbMeta);

                LogFlowFunc(("Received %RU64 bytes of data\n", cbDataTmp));

                *ppvData = pvDataTmp;
                *pcbData = cbDataTmp;
            }
            else
                RTMemFree(pvDataTmp);
        }
    }
    else
    {
        *ppvData = NULL;
        *pcbData = 0;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Host -> Guest
 * Main function for receiving the actual DnD data from the host.
 *
 * @returns VBox status code.
 * @retval  VERR_CANCELLED if cancelled by the host.
 * @param   pCtx                DnD context to use.
 * @param   pMeta               Where to store the actual meta data received from the host.
 */
static int vbglR3DnDHGRecvDataMain(PVBGLR3GUESTDNDCMDCTX   pCtx,
                                   PVBGLR3GUESTDNDMETADATA pMeta)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pMeta, VERR_INVALID_POINTER);

    AssertMsgReturn(pCtx->cbMaxChunkSize, ("Maximum chunk size must not be 0\n"), VERR_INVALID_PARAMETER);

    VBOXDNDDATAHDR dataHdr;
    RT_ZERO(dataHdr);
    dataHdr.cbMetaFmt = pCtx->cbMaxChunkSize;
    dataHdr.pvMetaFmt = RTMemAlloc(dataHdr.cbMetaFmt);
    if (!dataHdr.pvMetaFmt)
        return VERR_NO_MEMORY;

    void    *pvData = NULL;
    uint64_t cbData = 0;
    int rc = vbglR3DnDHGRecvDataLoop(pCtx, &dataHdr, &pvData, &cbData);
    if (RT_SUCCESS(rc))
    {
        LogRel2(("DnD: Received %RU64 bytes meta data in format '%s'\n", cbData, (char *)dataHdr.pvMetaFmt));

        /**
         * Check if this is an URI event. If so, let VbglR3 do all the actual
         * data transfer + file/directory creation internally without letting
         * the caller know.
         *
         * This keeps the actual (guest OS-)dependent client (like VBoxClient /
         * VBoxTray) small by not having too much redundant code.
         */
        Assert(dataHdr.cbMetaFmt);
        AssertPtr(dataHdr.pvMetaFmt);
        if (DnDMIMEHasFileURLs((char *)dataHdr.pvMetaFmt, dataHdr.cbMetaFmt)) /* URI data. */
        {
            DNDDROPPEDFILES droppedFiles;
            RT_ZERO(droppedFiles);

            rc = DnDDroppedFilesInit(&droppedFiles);
            if (RT_SUCCESS(rc))
                rc = DnDDroppedFilesOpenTemp(&droppedFiles, DNDURIDROPPEDFILE_FLAGS_NONE);

            if (RT_FAILURE(rc))
            {
                LogRel(("DnD: Initializing dropped files directory failed with %Rrc\n", rc));
            }
            else
            {
                AssertPtr(pvData);
                Assert(cbData);

                /* Use the dropped files directory as the root directory for the current transfer. */
                rc = DnDTransferListInitEx(&pMeta->u.URI.Transfer, DnDDroppedFilesGetDirAbs(&droppedFiles),
                                           DNDTRANSFERLISTFMT_NATIVE);
                if (RT_SUCCESS(rc))
                {
                    rc = DnDTransferListAppendRootsFromBuffer(&pMeta->u.URI.Transfer, DNDTRANSFERLISTFMT_URI, (const char *)pvData, cbData,
                                                              DND_PATH_SEPARATOR_STR, 0 /* fFlags */);
                    if (RT_SUCCESS(rc))
                    {
                        rc = vbglR3DnDHGRecvURIData(pCtx, &dataHdr, &droppedFiles);
                        if (RT_SUCCESS(rc))
                        {
                            pMeta->enmType = VBGLR3GUESTDNDMETADATATYPE_URI_LIST;
                        }
                    }
                }
            }
        }
        else /* Raw data. */
        {
            pMeta->u.Raw.cbMeta = cbData;
            pMeta->u.Raw.pvMeta = pvData;

            pMeta->enmType = VBGLR3GUESTDNDMETADATATYPE_RAW;
        }

        if (pvData)
            RTMemFree(pvData);
    }

    if (dataHdr.pvMetaFmt)
        RTMemFree(dataHdr.pvMetaFmt);

    if (RT_FAILURE(rc))
    {
        if (rc != VERR_CANCELLED)
        {
            LogRel(("DnD: Receiving data failed with %Rrc\n", rc));

            int rc2 = VbglR3DnDHGSendProgress(pCtx, DND_PROGRESS_ERROR, 100 /* Percent */, rc);
            if (RT_FAILURE(rc2))
                LogRel(("DnD: Unable to send progress error %Rrc to host: %Rrc\n", rc, rc2));
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

#ifdef VBOX_WITH_DRAG_AND_DROP_GH
/**
 * Guest -> Host
 * Utility function to receive the HOST_DND_FN_GH_REQ_PENDING message from the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   puScreenID          For which screen on the host the request is for. Optional.
 */
static int vbglR3DnDGHRecvPending(PVBGLR3GUESTDNDCMDCTX pCtx, uint32_t *puScreenID)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
   /* pScreenID is optional. */

    HGCMMsgGHReqPending Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, HOST_DND_FN_GH_REQ_PENDING, 2);
    /** @todo Context ID not used yet. */
    Msg.u.v3.uContext.SetUInt32(0);
    Msg.u.v3.uScreenId.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        /** @todo Context ID not used yet. */
        if (puScreenID)
            rc = Msg.u.v3.uContext.GetUInt32(puScreenID);
    }

    return rc;
}

/**
 * Guest -> Host
 * Utility function to receive the HOST_DND_FN_GH_EVT_DROPPED message from the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   ppszFormat          Requested data format from the host. Optional.
 * @param   pcbFormat           Size of requested data format (in bytes). Optional.
 * @param   puAction            Requested action from the host. Optional.
 */
static int vbglR3DnDGHRecvDropped(PVBGLR3GUESTDNDCMDCTX pCtx,
                                  char     **ppszFormat,
                                  uint32_t  *pcbFormat,
                                  uint32_t  *puAction)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    /* The rest is optional. */

    const uint32_t cbFormatTmp = pCtx->cbMaxChunkSize;

    char *pszFormatTmp = static_cast<char *>(RTMemAlloc(cbFormatTmp));
    if (!pszFormatTmp)
        return VERR_NO_MEMORY;

    HGCMMsgGHDropped Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, HOST_DND_FN_GH_EVT_DROPPED, 4);
    Msg.u.v3.uContext.SetUInt32(0);
    Msg.u.v3.pvFormat.SetPtr(pszFormatTmp, cbFormatTmp);
    Msg.u.v3.cbFormat.SetUInt32(0);
    Msg.u.v3.uAction.SetUInt32(0);

    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        /** @todo Context ID not used yet. */
        if (pcbFormat)
            rc = Msg.u.v3.cbFormat.GetUInt32(pcbFormat);
        if (RT_SUCCESS(rc) && puAction)
            rc = Msg.u.v3.uAction.GetUInt32(puAction);

        if (RT_SUCCESS(rc))
        {
            *ppszFormat = RTStrDup(pszFormatTmp);
            if (!*ppszFormat)
                rc = VERR_NO_MEMORY;
        }
    }

    RTMemFree(pszFormatTmp);

    return rc;
}
#endif /* VBOX_WITH_DRAG_AND_DROP_GH */


/*********************************************************************************************************************************
*   Public functions                                                                                                             *
*********************************************************************************************************************************/

/**
 * Connects a DnD context to the DnD host service.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to connect.
 */
VBGLR3DECL(int) VbglR3DnDConnect(PVBGLR3GUESTDNDCMDCTX pCtx)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    /* Initialize header */
    int rc = VbglR3HGCMConnect("VBoxDragAndDropSvc", &pCtx->uClientID);
    if (RT_FAILURE(rc))
        return rc;
    Assert(pCtx->uClientID);

    /* Set the default protocol version we would like to use.
     * Deprecated since VBox 6.1.x, but let this set to 3 to (hopefully) not break things. */
    pCtx->uProtocolDeprecated = 3;

    pCtx->fHostFeatures  = VBOX_DND_HF_NONE;
    pCtx->fGuestFeatures = VBOX_DND_GF_NONE;

    /*
     * Get the VM's session ID.
     * This is not fatal in case we're running with an ancient VBox version.
     */
    pCtx->uSessionID = 0;
    int rc2 = VbglR3GetSessionId(&pCtx->uSessionID); RT_NOREF(rc2);
    LogFlowFunc(("uSessionID=%RU64, rc=%Rrc\n", pCtx->uSessionID, rc2));

    /*
     * Try sending the connect message to tell the protocol version to use.
     * Note: This might fail when the Guest Additions run on an older VBox host (< VBox 5.0) which
     *       does not implement this command.
     */
    HGCMMsgConnect Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_DND_FN_CONNECT, 3);
    Msg.u.v3.uContext.SetUInt32(0);                /** @todo Context ID not used yet. */
    Msg.u.v3.uProtocol.SetUInt32(pCtx->uProtocolDeprecated); /* Deprecated since VBox 6.1.x. */
    Msg.u.v3.uFlags.SetUInt32(0);                  /* Unused at the moment. */

    rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    if (RT_SUCCESS(rc))
    {
        /* Set the protocol version we're going to use as told by the host. */
        rc = Msg.u.v3.uProtocol.GetUInt32(&pCtx->uProtocolDeprecated); AssertRC(rc);

        /*
         * Next is reporting our features.  If this fails, assume older host.
         */
        rc2 = VbglR3DnDReportFeatures(pCtx->uClientID, pCtx->fGuestFeatures, &pCtx->fHostFeatures);
        if (RT_SUCCESS(rc2))
        {
            LogRel2(("DnD: Guest features: %#RX64 - Host features: %#RX64\n",
                     pCtx->fGuestFeatures, pCtx->fHostFeatures));
        }
        else /* Failing here is not fatal; might be running with an older host. */
        {
            AssertLogRelMsg(rc2 == VERR_NOT_SUPPORTED || rc2 == VERR_NOT_IMPLEMENTED,
                            ("Reporting features failed: %Rrc\n", rc2));
        }

        pCtx->cbMaxChunkSize = DND_DEFAULT_CHUNK_SIZE; /** @todo Use a scratch buffer on the heap? */
    }
    else
        pCtx->uProtocolDeprecated = 0; /*  We're using protocol v0 (initial draft) as a fallback. */

    LogFlowFunc(("uClient=%RU32, uProtocol=%RU32, rc=%Rrc\n", pCtx->uClientID, pCtx->uProtocolDeprecated, rc));
    return rc;
}

/**
 * Disconnects a given DnD context from the DnD host service.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to disconnect.
 *                              The context is invalid afterwards on successful disconnection.
 */
VBGLR3DECL(int) VbglR3DnDDisconnect(PVBGLR3GUESTDNDCMDCTX pCtx)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    if (!pCtx->uClientID) /* Already disconnected? Bail out early. */
        return VINF_SUCCESS;

    int rc = VbglR3HGCMDisconnect(pCtx->uClientID);
    if (RT_SUCCESS(rc))
        pCtx->uClientID = 0;

    return rc;
}

/**
 * Reports features to the host and retrieve host feature set.
 *
 * @returns VBox status code.
 * @param   idClient        The client ID returned by VbglR3DnDConnect().
 * @param   fGuestFeatures  Features to report, VBOX_DND_GF_XXX.
 * @param   pfHostFeatures  Where to store the features VBOX_DND_HF_XXX.
 */
VBGLR3DECL(int) VbglR3DnDReportFeatures(uint32_t idClient, uint64_t fGuestFeatures, uint64_t *pfHostFeatures)
{
    int rc;
    do
    {
        struct
        {
            VBGLIOCHGCMCALL         Hdr;
            HGCMFunctionParameter   f64Features0;
            HGCMFunctionParameter   f64Features1;
        } Msg;
        VBGL_HGCM_HDR_INIT(&Msg.Hdr, idClient, GUEST_DND_FN_REPORT_FEATURES, 2);
        VbglHGCMParmUInt64Set(&Msg.f64Features0, fGuestFeatures);
        VbglHGCMParmUInt64Set(&Msg.f64Features1, VBOX_DND_GF_1_MUST_BE_ONE);

        rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Assert(Msg.f64Features0.type == VMMDevHGCMParmType_64bit);
            Assert(Msg.f64Features1.type == VMMDevHGCMParmType_64bit);
            if (Msg.f64Features1.u.value64 & VBOX_DND_GF_1_MUST_BE_ONE)
                rc = VERR_NOT_SUPPORTED;
            else if (pfHostFeatures)
                *pfHostFeatures = Msg.f64Features0.u.value64;
            break;
        }
    } while (rc == VERR_INTERRUPTED);
    return rc;

}

/**
 * Receives the next upcoming DnD event.
 *
 * This is the main function DnD clients call in order to implement any DnD functionality.
 * The purpose of it is to abstract the actual DnD protocol handling as much as possible from
 * the clients -- those only need to react to certain events, regardless of how the underlying
 * protocol actually is working.
 *
 * @returns VBox status code.
 * @param   pCtx                DnD context to work with.
 * @param   ppEvent             Next DnD event received on success; needs to be free'd by the client calling
 *                              VbglR3DnDEventFree() when done.
 */
VBGLR3DECL(int) VbglR3DnDEventGetNext(PVBGLR3GUESTDNDCMDCTX pCtx, PVBGLR3DNDEVENT *ppEvent)
{
    AssertPtrReturn(pCtx,    VERR_INVALID_POINTER);
    AssertPtrReturn(ppEvent, VERR_INVALID_POINTER);

    PVBGLR3DNDEVENT pEvent = (PVBGLR3DNDEVENT)RTMemAllocZ(sizeof(VBGLR3DNDEVENT));
    if (!pEvent)
        return VERR_NO_MEMORY;

    uint32_t uMsg   = 0;
    uint32_t cParms = 0;
    int rc = vbglR3DnDGetNextMsgType(pCtx, &uMsg, &cParms, true /* fWait */);
    if (RT_SUCCESS(rc))
    {
        /* Check for VM session change. */
        uint64_t uSessionID;
        int rc2 = VbglR3GetSessionId(&uSessionID);
        if (   RT_SUCCESS(rc2)
            && (uSessionID != pCtx->uSessionID))
        {
            LogRel2(("DnD: VM session ID changed to %RU64\n", uSessionID));
            rc = VbglR3DnDDisconnect(pCtx);
            if (RT_SUCCESS(rc))
                rc = VbglR3DnDConnect(pCtx);
        }
    }

    if (rc == VERR_CANCELLED) /* Host service told us that we have to bail out. */
    {
        LogRel2(("DnD: Host service requested termination\n"));

        pEvent->enmType = VBGLR3DNDEVENTTYPE_QUIT;
        *ppEvent = pEvent;

        return VINF_SUCCESS;
    }

    if (RT_SUCCESS(rc))
    {
        LogFunc(("Handling uMsg=%RU32\n", uMsg));

        switch(uMsg)
        {
            case HOST_DND_FN_HG_EVT_ENTER:
            {
                rc = vbglR3DnDHGRecvAction(pCtx,
                                           uMsg,
                                           &pEvent->u.HG_Enter.uScreenID,
                                           NULL /* puXPos */,
                                           NULL /* puYPos */,
                                           NULL /* uDefAction */,
                                           &pEvent->u.HG_Enter.dndLstActionsAllowed,
                                           &pEvent->u.HG_Enter.pszFormats,
                                           &pEvent->u.HG_Enter.cbFormats);
                if (RT_SUCCESS(rc))
                    pEvent->enmType = VBGLR3DNDEVENTTYPE_HG_ENTER;
                break;
            }
            case HOST_DND_FN_HG_EVT_MOVE:
            {
                rc = vbglR3DnDHGRecvAction(pCtx,
                                           uMsg,
                                           NULL /* puScreenId */,
                                           &pEvent->u.HG_Move.uXpos,
                                           &pEvent->u.HG_Move.uYpos,
                                           &pEvent->u.HG_Move.dndActionDefault,
                                           NULL /* puAllActions */,
                                           NULL /* pszFormats */,
                                           NULL /* pcbFormats */);
                if (RT_SUCCESS(rc))
                    pEvent->enmType = VBGLR3DNDEVENTTYPE_HG_MOVE;
                break;
            }
            case HOST_DND_FN_HG_EVT_DROPPED:
            {
                rc = vbglR3DnDHGRecvAction(pCtx,
                                           uMsg,
                                           NULL /* puScreenId */,
                                           &pEvent->u.HG_Drop.uXpos,
                                           &pEvent->u.HG_Drop.uYpos,
                                           &pEvent->u.HG_Drop.dndActionDefault,
                                           NULL /* puAllActions */,
                                           NULL /* pszFormats */,
                                           NULL /* pcbFormats */);
                if (RT_SUCCESS(rc))
                    pEvent->enmType = VBGLR3DNDEVENTTYPE_HG_DROP;
                break;
            }
            case HOST_DND_FN_HG_EVT_LEAVE:
            {
                rc = vbglR3DnDHGRecvLeave(pCtx);
                if (RT_SUCCESS(rc))
                    pEvent->enmType = VBGLR3DNDEVENTTYPE_HG_LEAVE;
                break;
            }
            case HOST_DND_FN_HG_SND_DATA_HDR:
            {
                rc = vbglR3DnDHGRecvDataMain(pCtx, &pEvent->u.HG_Received.Meta);
                if (RT_SUCCESS(rc))
                    pEvent->enmType = VBGLR3DNDEVENTTYPE_HG_RECEIVE;
                break;
            }
            case HOST_DND_FN_HG_SND_DIR:
                RT_FALL_THROUGH();
            case HOST_DND_FN_HG_SND_FILE_HDR:
                RT_FALL_THROUGH();
            case HOST_DND_FN_HG_SND_FILE_DATA:
            {
                /*
                 * All messages for this block are handled internally
                 * by vbglR3DnDHGRecvDataMain(), see above.
                 *
                 * So if we land here our code is buggy.
                 */
                rc = VERR_WRONG_ORDER;
                break;
            }
            case HOST_DND_FN_CANCEL:
            {
                rc = vbglR3DnDHGRecvCancel(pCtx);
                if (RT_SUCCESS(rc))
                    rc = VERR_CANCELLED; /* Will emit a cancel event below. */
                break;
            }
#ifdef VBOX_WITH_DRAG_AND_DROP_GH
            case HOST_DND_FN_GH_REQ_PENDING:
            {
                rc = vbglR3DnDGHRecvPending(pCtx, &pEvent->u.GH_IsPending.uScreenID);
                if (RT_SUCCESS(rc))
                    pEvent->enmType = VBGLR3DNDEVENTTYPE_GH_REQ_PENDING;
                break;
            }
            case HOST_DND_FN_GH_EVT_DROPPED:
            {
                rc = vbglR3DnDGHRecvDropped(pCtx,
                                            &pEvent->u.GH_Drop.pszFormat,
                                            &pEvent->u.GH_Drop.cbFormat,
                                            &pEvent->u.GH_Drop.dndActionRequested);
                if (RT_SUCCESS(rc))
                    pEvent->enmType = VBGLR3DNDEVENTTYPE_GH_DROP;
                break;
            }
#endif
            default:
            {
                rc = VERR_NOT_SUPPORTED;
                break;
            }
        }
    }

    if (RT_FAILURE(rc))
    {
        /* Current operation cancelled? Set / overwrite event type and tell the caller. */
        if (rc == VERR_CANCELLED)
        {
            pEvent->enmType = VBGLR3DNDEVENTTYPE_CANCEL;
            rc              = VINF_SUCCESS; /* Deliver the event to the caller. */
        }
        else
        {
            VbglR3DnDEventFree(pEvent);
            LogRel(("DnD: Handling message %s (%#x) failed with %Rrc\n", DnDHostMsgToStr(uMsg), uMsg, rc));
        }
    }

    if (RT_SUCCESS(rc))
        *ppEvent = pEvent;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Frees (destroys) a formerly allocated DnD event.
 *
 * @returns IPRT status code.
 * @param   pEvent              Event to free (destroy).
 */
VBGLR3DECL(void) VbglR3DnDEventFree(PVBGLR3DNDEVENT pEvent)
{
    if (!pEvent)
        return;

    /* Some messages require additional cleanup. */
    switch (pEvent->enmType)
    {
        case VBGLR3DNDEVENTTYPE_HG_ENTER:
        {
            if (pEvent->u.HG_Enter.pszFormats)
                RTStrFree(pEvent->u.HG_Enter.pszFormats);
            break;
        }

#ifdef VBOX_WITH_DRAG_AND_DROP_GH
        case VBGLR3DNDEVENTTYPE_GH_DROP:
        {
            if (pEvent->u.GH_Drop.pszFormat)
                RTStrFree(pEvent->u.GH_Drop.pszFormat);
            break;
        }
#endif
        case VBGLR3DNDEVENTTYPE_HG_RECEIVE:
        {
            PVBGLR3GUESTDNDMETADATA pMeta = &pEvent->u.HG_Received.Meta;
            switch (pMeta->enmType)
            {
                case VBGLR3GUESTDNDMETADATATYPE_RAW:
                {
                    if (pMeta->u.Raw.pvMeta)
                    {
                        Assert(pMeta->u.Raw.cbMeta);
                        RTMemFree(pMeta->u.Raw.pvMeta);
                        pMeta->u.Raw.cbMeta = 0;
                    }
                    break;
                }

                case VBGLR3GUESTDNDMETADATATYPE_URI_LIST:
                {
                    DnDTransferListDestroy(&pMeta->u.URI.Transfer);
                    break;
                }

                default:
                    break;
            }
            break;
        }

        default:
            break;
    }

    RTMemFree(pEvent);
    pEvent = NULL;
}

VBGLR3DECL(int) VbglR3DnDHGSendAckOp(PVBGLR3GUESTDNDCMDCTX pCtx, VBOXDNDACTION dndAction)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMMsgHGAck Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_DND_FN_HG_ACK_OP, 2);
    /** @todo Context ID not used yet. */
    Msg.u.v3.uContext.SetUInt32(0);
    Msg.u.v3.uAction.SetUInt32(dndAction);

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}

/**
 * Host -> Guest
 * Requests the actual DnD data to be sent from the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   pcszFormat          Format to request the data from the host in.
 */
VBGLR3DECL(int) VbglR3DnDHGSendReqData(PVBGLR3GUESTDNDCMDCTX pCtx, const char* pcszFormat)
{
    AssertPtrReturn(pCtx,       VERR_INVALID_POINTER);
    AssertPtrReturn(pcszFormat, VERR_INVALID_POINTER);
    if (!RTStrIsValidEncoding(pcszFormat))
        return VERR_INVALID_PARAMETER;

    const uint32_t cbFormat = (uint32_t)strlen(pcszFormat) + 1; /* Include termination */

    HGCMMsgHGReqData Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_DND_FN_HG_REQ_DATA, 3);
    /** @todo Context ID not used yet. */
    Msg.u.v3.uContext.SetUInt32(0);
    Msg.u.v3.pvFormat.SetPtr((void*)pcszFormat, cbFormat);
    Msg.u.v3.cbFormat.SetUInt32(cbFormat);

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}

/**
 * Host -> Guest
 * Reports back its progress back to the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   uStatus             DnD status to report.
 * @param   uPercent            Overall progress (in percent) to report.
 * @param   rcErr               Error code (IPRT-style) to report.
 */
VBGLR3DECL(int) VbglR3DnDHGSendProgress(PVBGLR3GUESTDNDCMDCTX pCtx, uint32_t uStatus, uint8_t uPercent, int rcErr)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(uStatus > DND_PROGRESS_UNKNOWN, VERR_INVALID_PARAMETER);

    HGCMMsgHGProgress Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_DND_FN_HG_EVT_PROGRESS, 4);
    /** @todo Context ID not used yet. */
    Msg.u.v3.uContext.SetUInt32(0);
    Msg.u.v3.uStatus.SetUInt32(uStatus);
    Msg.u.v3.uPercent.SetUInt32(uPercent);
    Msg.u.v3.rc.SetUInt32((uint32_t)rcErr); /* uint32_t vs. int. */

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}

#ifdef VBOX_WITH_DRAG_AND_DROP_GH
/**
 * Guest -> Host
 * Acknowledges that there currently is a drag'n drop operation in progress on the guest,
 * which eventually could be dragged over to the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                 DnD context to use.
 * @param   dndActionDefault     Default action for the operation to report.
 * @param   dndLstActionsAllowed All available actions for the operation to report.
 * @param   pcszFormats          Available formats for the operation to report.
 * @param   cbFormats            Size (in bytes) of formats to report.
 */
VBGLR3DECL(int) VbglR3DnDGHSendAckPending(PVBGLR3GUESTDNDCMDCTX pCtx,
                                          VBOXDNDACTION dndActionDefault, VBOXDNDACTIONLIST dndLstActionsAllowed,
                                          const char* pcszFormats, uint32_t cbFormats)
{
    AssertPtrReturn(pCtx,        VERR_INVALID_POINTER);
    AssertPtrReturn(pcszFormats, VERR_INVALID_POINTER);
    AssertReturn(cbFormats,      VERR_INVALID_PARAMETER);

    if (!RTStrIsValidEncoding(pcszFormats))
        return VERR_INVALID_UTF8_ENCODING;

    HGCMMsgGHAckPending Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_DND_FN_GH_ACK_PENDING, 5);
    /** @todo Context ID not used yet. */
    Msg.u.v3.uContext.SetUInt32(0);
    Msg.u.v3.uDefAction.SetUInt32(dndActionDefault);
    Msg.u.v3.uAllActions.SetUInt32(dndLstActionsAllowed);
    Msg.u.v3.pvFormats.SetPtr((void*)pcszFormats, cbFormats);
    Msg.u.v3.cbFormats.SetUInt32(cbFormats);

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}

/**
 * Guest -> Host
 * Utility function to send DnD data from guest to the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   pvData              Data block to send.
 * @param   cbData              Size (in bytes) of data block to send.
 * @param   pDataHdr            Data header to use -- needed for accounting.
 */
static int vbglR3DnDGHSendDataInternal(PVBGLR3GUESTDNDCMDCTX pCtx,
                                       void *pvData, uint64_t cbData, PVBOXDNDSNDDATAHDR pDataHdr)
{
    AssertPtrReturn(pCtx,     VERR_INVALID_POINTER);
    AssertPtrReturn(pvData,   VERR_INVALID_POINTER);
    AssertReturn(cbData,      VERR_INVALID_PARAMETER);
    AssertPtrReturn(pDataHdr, VERR_INVALID_POINTER);

    HGCMMsgGHSendDataHdr MsgHdr;
    VBGL_HGCM_HDR_INIT(&MsgHdr.hdr, pCtx->uClientID, GUEST_DND_FN_GH_SND_DATA_HDR, 12);
    MsgHdr.uContext.SetUInt32(0);                           /** @todo Not used yet. */
    MsgHdr.uFlags.SetUInt32(0);                             /** @todo Not used yet. */
    MsgHdr.uScreenId.SetUInt32(0);                          /** @todo Not used for guest->host (yet). */
    MsgHdr.cbTotal.SetUInt64(pDataHdr->cbTotal);
    MsgHdr.cbMeta.SetUInt32(pDataHdr->cbMeta);
    MsgHdr.pvMetaFmt.SetPtr(pDataHdr->pvMetaFmt, pDataHdr->cbMetaFmt);
    MsgHdr.cbMetaFmt.SetUInt32(pDataHdr->cbMetaFmt);
    MsgHdr.cObjects.SetUInt64(pDataHdr->cObjects);
    MsgHdr.enmCompression.SetUInt32(0);                     /** @todo Not used yet. */
    MsgHdr.enmChecksumType.SetUInt32(RTDIGESTTYPE_INVALID); /** @todo Not used yet. */
    MsgHdr.pvChecksum.SetPtr(NULL, 0);                      /** @todo Not used yet. */
    MsgHdr.cbChecksum.SetUInt32(0);                         /** @todo Not used yet. */

    int rc = VbglR3HGCMCall(&MsgHdr.hdr, sizeof(MsgHdr));

    LogFlowFunc(("cbTotal=%RU64, cbMeta=%RU32, cObjects=%RU64, rc=%Rrc\n",
                 pDataHdr->cbTotal, pDataHdr->cbMeta, pDataHdr->cObjects, rc));

    if (RT_SUCCESS(rc))
    {
        HGCMMsgGHSendData MsgData;
        VBGL_HGCM_HDR_INIT(&MsgData.hdr, pCtx->uClientID, GUEST_DND_FN_GH_SND_DATA, 5);
        MsgData.u.v3.uContext.SetUInt32(0);      /** @todo Not used yet. */
        MsgData.u.v3.pvChecksum.SetPtr(NULL, 0); /** @todo Not used yet. */
        MsgData.u.v3.cbChecksum.SetUInt32(0);    /** @todo Not used yet. */

        uint32_t       cbCurChunk;
        const uint32_t cbMaxChunk = pCtx->cbMaxChunkSize;
        uint32_t       cbSent     = 0;

        while (cbSent < cbData)
        {
            cbCurChunk = RT_MIN(cbData - cbSent, cbMaxChunk);
            MsgData.u.v3.pvData.SetPtr(static_cast<uint8_t *>(pvData) + cbSent, cbCurChunk);
            MsgData.u.v3.cbData.SetUInt32(cbCurChunk);

            rc = VbglR3HGCMCall(&MsgData.hdr, sizeof(MsgData));
            if (RT_FAILURE(rc))
                break;

            cbSent += cbCurChunk;
        }

        LogFlowFunc(("cbMaxChunk=%RU32, cbData=%RU64, cbSent=%RU32, rc=%Rrc\n",
                     cbMaxChunk, cbData, cbSent, rc));

        if (RT_SUCCESS(rc))
            Assert(cbSent == cbData);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Guest -> Host
 * Utility function to send a guest directory to the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   pObj                transfer object containing the directory to send.
 */
static int vbglR3DnDGHSendDir(PVBGLR3GUESTDNDCMDCTX pCtx, DNDTRANSFEROBJECT *pObj)
{
    AssertPtrReturn(pObj,                                         VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx,                                         VERR_INVALID_POINTER);
    AssertReturn(DnDTransferObjectGetType(pObj) == DNDTRANSFEROBJTYPE_DIRECTORY, VERR_INVALID_PARAMETER);

    const char *pcszPath = DnDTransferObjectGetDestPath(pObj);
    const size_t cbPath  = RTStrNLen(pcszPath, RTPATH_MAX) + 1 /* Include termination. */;
    const RTFMODE fMode  = DnDTransferObjectGetMode(pObj);

    LogFlowFunc(("strDir=%s (%zu bytes), fMode=0x%x\n", pcszPath, cbPath, fMode));

    if (cbPath > RTPATH_MAX + 1) /* Can't happen, but check anyway. */
        return VERR_INVALID_PARAMETER;

    HGCMMsgGHSendDir Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_DND_FN_GH_SND_DIR, 4);
    /** @todo Context ID not used yet. */
    Msg.u.v3.uContext.SetUInt32(0);
    Msg.u.v3.pvName.SetPtr((void *)pcszPath, (uint32_t)cbPath);
    Msg.u.v3.cbName.SetUInt32((uint32_t)cbPath);
    Msg.u.v3.fMode.SetUInt32(fMode);

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}

/**
 * Guest -> Host
 * Utility function to send a file from the guest to the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   pObj                Transfer object containing the file to send.
 */
static int vbglR3DnDGHSendFile(PVBGLR3GUESTDNDCMDCTX pCtx, PDNDTRANSFEROBJECT pObj)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pObj, VERR_INVALID_POINTER);
    AssertReturn(DnDTransferObjectIsOpen(pObj) == false, VERR_INVALID_STATE);
    AssertReturn(DnDTransferObjectGetType(pObj) == DNDTRANSFEROBJTYPE_FILE, VERR_INVALID_PARAMETER);

    uint64_t fOpen = RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_WRITE;

    int rc = DnDTransferObjectOpen(pObj, fOpen, 0 /* fMode */, DNDTRANSFEROBJECT_FLAGS_NONE);
    if (RT_FAILURE(rc))
        return rc;

    uint32_t cbBuf = pCtx->cbMaxChunkSize;
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);
    void *pvBuf = RTMemAlloc(cbBuf); /** @todo Make this buffer part of PVBGLR3GUESTDNDCMDCTX? */
    if (!pvBuf)
    {
        int rc2 = DnDTransferObjectClose(pObj);
        AssertRC(rc2);
        return VERR_NO_MEMORY;
    }

    const char *pcszPath  = DnDTransferObjectGetDestPath(pObj);
    const size_t cchPath  = RTStrNLen(pcszPath, RTPATH_MAX);
    const uint64_t cbSize = DnDTransferObjectGetSize(pObj);
    const RTFMODE fMode   = DnDTransferObjectGetMode(pObj);

    LogFlowFunc(("strFile=%s (%zu), cbSize=%RU64, fMode=0x%x\n", pcszPath, cchPath, cbSize, fMode));

    HGCMMsgGHSendFileHdr MsgHdr;
    VBGL_HGCM_HDR_INIT(&MsgHdr.hdr, pCtx->uClientID, GUEST_DND_FN_GH_SND_FILE_HDR, 6);
    MsgHdr.uContext.SetUInt32(0);                                                    /* Context ID; unused at the moment. */
    MsgHdr.pvName.SetPtr((void *)pcszPath, (uint32_t)(cchPath + 1));                 /* Include termination. */
    MsgHdr.cbName.SetUInt32((uint32_t)(cchPath + 1));                                /* Ditto. */
    MsgHdr.uFlags.SetUInt32(0);                                                      /* Flags; unused at the moment. */
    MsgHdr.fMode.SetUInt32(fMode);                                                   /* File mode */
    MsgHdr.cbTotal.SetUInt64(cbSize);                                                /* File size (in bytes). */

    rc = VbglR3HGCMCall(&MsgHdr.hdr, sizeof(MsgHdr));

    LogFlowFunc(("Sending file header resulted in %Rrc\n", rc));

    if (RT_SUCCESS(rc))
    {
        /*
         * Send the actual file data, chunk by chunk.
         */
        HGCMMsgGHSendFileData Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_DND_FN_GH_SND_FILE_DATA, 5);
        Msg.u.v3.uContext.SetUInt32(0);
        Msg.u.v3.pvChecksum.SetPtr(NULL, 0);
        Msg.u.v3.cbChecksum.SetUInt32(0);

        uint64_t cbToReadTotal  = cbSize;
        uint64_t cbWrittenTotal = 0;
        while (cbToReadTotal)
        {
            uint32_t cbToRead = RT_MIN(cbToReadTotal, cbBuf);
            uint32_t cbRead   = 0;
            if (cbToRead)
                rc = DnDTransferObjectRead(pObj, pvBuf, cbToRead, &cbRead);

            LogFlowFunc(("cbToReadTotal=%RU64, cbToRead=%RU32, cbRead=%RU32, rc=%Rrc\n",
                         cbToReadTotal, cbToRead, cbRead, rc));

            if (   RT_SUCCESS(rc)
                && cbRead)
            {
                Msg.u.v3.pvData.SetPtr(pvBuf, cbRead);
                Msg.u.v3.cbData.SetUInt32(cbRead);
                /** @todo Calculate + set checksums. */

                rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
            }

            if (RT_FAILURE(rc))
            {
                LogFlowFunc(("Reading failed with rc=%Rrc\n", rc));
                break;
            }

            Assert(cbRead  <= cbToReadTotal);
            cbToReadTotal  -= cbRead;
            cbWrittenTotal += cbRead;

            LogFlowFunc(("%RU64/%RU64 -- %RU8%%\n", cbWrittenTotal, cbSize, cbWrittenTotal * 100 / cbSize));
        };
    }

    RTMemFree(pvBuf);
    int rc2 = DnDTransferObjectClose(pObj);
    if (RT_SUCCESS(rc))
        rc = rc2;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Guest -> Host
 * Utility function to send a transfer object from guest to the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   pObj                Transfer object to send from guest to the host.
 */
static int vbglR3DnDGHSendURIObject(PVBGLR3GUESTDNDCMDCTX pCtx, PDNDTRANSFEROBJECT pObj)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pObj, VERR_INVALID_POINTER);

    int rc;

    const DNDTRANSFEROBJTYPE enmType = DnDTransferObjectGetType(pObj);

    switch (enmType)
    {
        case DNDTRANSFEROBJTYPE_DIRECTORY:
            rc = vbglR3DnDGHSendDir(pCtx, pObj);
            break;

        case DNDTRANSFEROBJTYPE_FILE:
            rc = vbglR3DnDGHSendFile(pCtx, pObj);
            break;

        default:
            AssertMsgFailed(("Object type %ld not implemented\n", enmType));
            rc = VERR_NOT_IMPLEMENTED;
            break;
    }

    return rc;
}

/**
 * Guest -> Host
 * Utility function to send raw data from guest to the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   pvData              Block to raw data to send.
 * @param   cbData              Size (in bytes) of raw data to send.
 */
static int vbglR3DnDGHSendRawData(PVBGLR3GUESTDNDCMDCTX pCtx, void *pvData, size_t cbData)
{
    AssertPtrReturn(pCtx,   VERR_INVALID_POINTER);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    /* cbData can be 0. */

    VBOXDNDDATAHDR dataHdr;
    RT_ZERO(dataHdr);

    dataHdr.cbMeta  = (uint32_t)cbData;
    dataHdr.cbTotal = cbData;

    return vbglR3DnDGHSendDataInternal(pCtx, pvData, cbData, &dataHdr);
}

/**
 * Guest -> Host
 * Utility function to send transfer data from guest to the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   pTransferList       Dnd transfer list to send.
 */
static int vbglR3DnDGHSendTransferData(PVBGLR3GUESTDNDCMDCTX pCtx, PDNDTRANSFERLIST pTransferList)
{
    AssertPtrReturn(pCtx,VERR_INVALID_POINTER);
    AssertPtrReturn(pTransferList, VERR_INVALID_POINTER);

    /*
     * Send the (meta) data; in case of URIs it's the root entries of a
     * transfer list the host needs to know upfront to set up the drag'n drop operation.
     */
    char *pszList = NULL;
    size_t cbList;
    int rc = DnDTransferListGetRoots(pTransferList, DNDTRANSFERLISTFMT_URI, &pszList, &cbList);
    if (RT_FAILURE(rc))
        return rc;

    void    *pvURIList = (void *)pszList;
    uint32_t cbURLIist = (uint32_t)cbList;

    /* The total size also contains the size of the meta data. */
    uint64_t cbTotal  = cbURLIist;
             cbTotal += DnDTransferListObjTotalBytes(pTransferList);

    /* We're going to send a transfer list in text format. */
    const char     szMetaFmt[] = "text/uri-list";
    const uint32_t cbMetaFmt   = (uint32_t)strlen(szMetaFmt) + 1; /* Include termination. */

    VBOXDNDDATAHDR dataHdr;
    dataHdr.uFlags    = 0; /* Flags not used yet. */
    dataHdr.cbTotal   = cbTotal;
    dataHdr.cbMeta    = cbURLIist;
    dataHdr.pvMetaFmt = (void *)szMetaFmt;
    dataHdr.cbMetaFmt = cbMetaFmt;
    dataHdr.cObjects  = DnDTransferListObjCount(pTransferList);

    rc = vbglR3DnDGHSendDataInternal(pCtx, pvURIList, cbURLIist, &dataHdr);

    if (RT_SUCCESS(rc))
    {
        while (DnDTransferListObjCount(pTransferList))
        {
            PDNDTRANSFEROBJECT pObj = DnDTransferListObjGetFirst(pTransferList);

            rc = vbglR3DnDGHSendURIObject(pCtx, pObj);
            if (RT_FAILURE(rc))
                break;

            DnDTransferListObjRemoveFirst(pTransferList);
        }

        Assert(DnDTransferListObjCount(pTransferList) == 0);
    }

    return rc;
}

/**
 * Guest -> Host
 * Sends data, which either can be raw or URI data, from guest to the host. This function
 * initiates the actual data transfer from guest to the host.
 *
 * @returns IPRT status code.
 * @param   pCtx                DnD context to use.
 * @param   pszFormat           In which format the data will be sent.
 * @param   pvData              Data block to send.
 *                              For URI data this must contain the absolute local URI paths, separated by DND_PATH_SEPARATOR_STR.
 * @param   cbData              Size (in bytes) of data block to send.
 */
VBGLR3DECL(int) VbglR3DnDGHSendData(PVBGLR3GUESTDNDCMDCTX pCtx, const char *pszFormat, void *pvData, uint32_t cbData)
{
    AssertPtrReturn(pCtx,      VERR_INVALID_POINTER);
    AssertPtrReturn(pszFormat, VERR_INVALID_POINTER);
    AssertPtrReturn(pvData,    VERR_INVALID_POINTER);
    AssertReturn(cbData,       VERR_INVALID_PARAMETER);

    LogFlowFunc(("pszFormat=%s, pvData=%p, cbData=%RU32\n", pszFormat, pvData, cbData));

    LogRel2(("DnD: Sending %RU32 bytes meta data in format '%s'\n", cbData, pszFormat));

    int rc;
    if (DnDMIMEHasFileURLs(pszFormat, strlen(pszFormat)))
    {
        DNDTRANSFERLIST lstTransfer;
        RT_ZERO(lstTransfer);

        rc = DnDTransferListInit(&lstTransfer);
        if (RT_SUCCESS(rc))
        {
            /** @todo Add symlink support (DNDTRANSFERLIST_FLAGS_RESOLVE_SYMLINKS) here. */
            /** @todo Add lazy loading (DNDTRANSFERLIST_FLAGS_LAZY) here. */
            const DNDTRANSFERLISTFLAGS fFlags = DNDTRANSFERLIST_FLAGS_RECURSIVE;

            rc = DnDTransferListAppendPathsFromBuffer(&lstTransfer, DNDTRANSFERLISTFMT_URI, (const char *)pvData, cbData,
                                                      DND_PATH_SEPARATOR_STR, fFlags);
            if (RT_SUCCESS(rc))
                rc = vbglR3DnDGHSendTransferData(pCtx, &lstTransfer);
            DnDTransferListDestroy(&lstTransfer);
        }
    }
    else
        rc = vbglR3DnDGHSendRawData(pCtx, pvData, cbData);

    if (RT_FAILURE(rc))
    {
        LogRel(("DnD: Sending data failed with rc=%Rrc\n", rc));

        if (rc != VERR_CANCELLED)
        {
            int rc2 = VbglR3DnDSendError(pCtx, rc);
            if (RT_FAILURE(rc2))
                LogFlowFunc(("Unable to send error (%Rrc) to host, rc=%Rrc\n", rc, rc2));
        }
    }

    return rc;
}
#endif /* VBOX_WITH_DRAG_AND_DROP_GH */

