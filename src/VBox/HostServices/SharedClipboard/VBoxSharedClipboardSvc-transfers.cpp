/* $Id: VBoxSharedClipboardSvc-transfers.cpp 115055 2026-08-17 16:40:05Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Service - Internal code for transfer (list) handling.
 */

/*
 * Copyright (C) 2019-2026 Oracle and/or its affiliates.
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
#include <VBox/log.h>

#include <VBox/err.h>

#include <VBox/GuestHost/clipboard-helper.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/HostServices/VBoxSharedClipboardSvc.h>
#include <VBox/HostServices/VBoxClipboardExt.h>

#include <VBox/AssertGuest.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/path.h>

#include <VBox/GuestHost/SharedClipboard-transfers.h>

#include "VBoxSharedClipboardSvc-internal.h"
#include "VBoxSharedClipboardSvc-transfers.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int shClSvcTransferModeSet(uint32_t fMode);


/**
 * Looks up a transfer by service-session/transfer/generation key in the active client.
 *
 * @returns VBox status code.
 * @param   idSession           Service session ID to look up.
 * @param   idTransfer          Transfer ID to look up.
 * @param   uGeneration         Host-private transfer generation to look up.
 * @param   ppClient            Where to return the owning client.
 * @param   ppTransfer          Where to return the retained transfer. The caller
 *                              must release it with ShClTransferRelease().
 */
static int shClSvcTransferFindByKey(SHCLSESSIONID idSession, SHCLTRANSFERID idTransfer, SHCLTRANSFERGEN uGeneration,
                                    PSHCLCLIENT *ppClient, PSHCLTRANSFER *ppTransfer)
{
    AssertPtrReturn(ppClient, VERR_INVALID_POINTER);
    AssertPtrReturn(ppTransfer, VERR_INVALID_POINTER);
    AssertReturn(ShClTransferKeyIsValid(idSession, idTransfer, uGeneration), VERR_INVALID_CONTEXT);

    *ppClient   = NULL;
    *ppTransfer = NULL;

    shClSvcLock();

    PSHCLCLIENT pClient = g_ShClSvc.pActiveClient;
    if (   pClient
        && pClient->State.uSessionID == idSession)
    {
        PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferByKeyRetained(&pClient->Transfers.Ctx, idSession,
                                                                           idTransfer, uGeneration);
        if (pTransfer)
        {
            *ppClient   = pClient;
            *ppTransfer = pTransfer;
        }
    }

    shClSvcUnlock();

    return *ppTransfer ? VINF_SUCCESS : VERR_SHCLPB_TRANSFER_ID_NOT_FOUND;
}


/**
 * Aborts a transfer from a host request and tears it down locally.
 *
 * @returns VBox status code.
 * @param   uContextId          Context ID containing service session and transfer IDs.
 * @param   uGeneration         Host-private transfer generation to abort.
 * @param   enmStatus           Terminal abort status to report.
 * @param   rcTransfer          Transfer result code to report.
 */
static int shClSvcTransferAbortByHostKey(uint64_t uContextId, SHCLTRANSFERGEN uGeneration,
                                         SHCLTRANSFERSTATUS enmStatus, int rcTransfer)
{
    AssertReturn(   enmStatus == SHCLTRANSFERSTATUS_CANCELED
                 || enmStatus == SHCLTRANSFERSTATUS_ERROR, VERR_INVALID_PARAMETER);
    AssertReturn(VBOX_SHCL_CONTEXTID_GET_EVENT(uContextId) == 0, VERR_INVALID_CONTEXT);

    SHCLSESSIONID const idSession  = VBOX_SHCL_CONTEXTID_GET_SESSION(uContextId);
    SHCLTRANSFERID const idTransfer = VBOX_SHCL_CONTEXTID_GET_TRANSFER(uContextId);

    PSHCLCLIENT   pClient;
    PSHCLTRANSFER pTransfer;
    int rc = shClSvcTransferFindByKey(idSession, idTransfer, uGeneration, &pClient, &pTransfer);
    if (RT_FAILURE(rc))
        return rc;

    ShClSvcClientLock(pClient);

    int rcState;
    if (enmStatus == SHCLTRANSFERSTATUS_CANCELED)
        rcState = ShClTransferCancel(pTransfer);
    else
        rcState = ShClTransferError(pTransfer, rcTransfer);

    int rcStatus = VINF_SUCCESS;
    if (RT_SUCCESS(rcState))
        rcStatus = shClSvcTransferSendStatusAsync(pClient, pTransfer, enmStatus, rcTransfer, NULL /* ppEvent */);

    ShClSvcClientUnlock(pClient);

    /* Drop the lookup retain before the consuming destroy waits for all users. */
    ShClTransferRelease(pTransfer);

    /* The terminal status was already reported above. */
    ShClSvcTransferDestroyByIdEx(pClient, idTransfer, false /* fNotifyGuest */);

    if (RT_SUCCESS(rc))
        rc = rcState;
    if (RT_SUCCESS(rc))
        rc = rcStatus;

    return rc;
}


/*********************************************************************************************************************************
*   HGCM getters / setters                                                                                                       *
*********************************************************************************************************************************/

/**
 * Returns whether a HGCM message is allowed in a certain service mode or not.
 *
 * @returns \c true if message is allowed, \c false if not.
 * @param   uMode               Service mode to check allowance for.
 * @param   uMsg                HGCM message to check allowance for.
 */
static bool shClSvcTransferMsgIsAllowed(uint32_t uMode, uint32_t uMsg)
{
    const bool fHostToGuest =    uMode == VBOX_SHCL_MODE_HOST_TO_GUEST
                              || uMode == VBOX_SHCL_MODE_BIDIRECTIONAL;

    const bool fGuestToHost =    uMode == VBOX_SHCL_MODE_GUEST_TO_HOST
                              || uMode == VBOX_SHCL_MODE_BIDIRECTIONAL;

    bool fAllowed = false; /* If in doubt, don't allow. */

    switch (uMsg)
    {
        case VBOX_SHCL_GUEST_FN_ROOT_LIST_HDR_WRITE:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_ROOT_LIST_ENTRY_WRITE:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_LIST_HDR_WRITE:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_LIST_ENTRY_WRITE:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_OBJ_WRITE:
            fAllowed = fGuestToHost;
            break;

        case VBOX_SHCL_GUEST_FN_ROOT_LIST_HDR_READ:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_ROOT_LIST_ENTRY_READ:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_LIST_HDR_READ:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_LIST_ENTRY_READ:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_OBJ_READ:
            fAllowed = fHostToGuest;
            break;

        case VBOX_SHCL_GUEST_FN_CONNECT:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_NEGOTIATE_CHUNK_SIZE:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_MSG_PEEK_WAIT:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_MSG_PEEK_NOWAIT:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_REPORT_FEATURES:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_QUERY_FEATURES:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_MSG_GET:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_REPLY:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_MSG_CANCEL:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_ERROR:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_LIST_OPEN:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_LIST_CLOSE:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_OBJ_OPEN:
            RT_FALL_THROUGH();
        case VBOX_SHCL_GUEST_FN_OBJ_CLOSE:
            fAllowed = fHostToGuest || fGuestToHost;
            break;

        default:
            break;
    }

    LogFlowFunc(("uMsg=%RU32 (%s), uMode=%RU32 -> fAllowed=%RTbool\n", uMsg, ShClSvcGuestMsgToStr(uMsg), uMode, fAllowed));
    return fAllowed;
}

/**
 * Gets a transfer message reply from HGCM service parameters.
 *
 * @returns VBox status code.
 * @param   cParms              Number of HGCM parameters supplied in \a aParms.
 * @param   aParms              Array of HGCM parameters.
 * @param   pReply              Where to store the reply.
 */
static int shClSvcTransferMsgGetReply(uint32_t cParms, VBOXHGCMSVCPARM aParms[],
                                      PSHCLREPLY pReply)
{
    int rc;

    if (cParms >= VBOX_SHCL_CPARMS_REPLY_MIN)
    {
        /* aParms[0] has the context ID. */
        rc = HGCMSvcGetU32(&aParms[1], &pReply->uType);
        if (RT_SUCCESS(rc))
            rc = HGCMSvcGetU32(&aParms[2], &pReply->rc);
        if (RT_SUCCESS(rc))
            rc = HGCMSvcGetPv(&aParms[3], &pReply->pvPayload, &pReply->cbPayload);

        if (RT_SUCCESS(rc))
        {
            rc = VERR_INVALID_PARAMETER; /* Play safe. */

            const unsigned idxParm = VBOX_SHCL_CPARMS_REPLY_MIN;

            switch (pReply->uType)
            {
                case VBOX_SHCL_TX_REPLYMSGTYPE_TRANSFER_STATUS:
                {
                    if (cParms > idxParm)
                        rc = HGCMSvcGetU32(&aParms[idxParm], &pReply->u.TransferStatus.uStatus);
                    else
                        rc = VERR_INVALID_PARAMETER;

                    LogFlowFunc(("uTransferStatus=%RU32 (%s)\n",
                                 pReply->u.TransferStatus.uStatus, ShClTransferStatusToStr(pReply->u.TransferStatus.uStatus)));
                    break;
                }

                case VBOX_SHCL_TX_REPLYMSGTYPE_LIST_OPEN:
                {
                    if (cParms > idxParm)
                        rc = HGCMSvcGetU64(&aParms[idxParm], &pReply->u.ListOpen.uHandle);
                    else
                        rc = VERR_INVALID_PARAMETER;

                    LogFlowFunc(("hListOpen=%RU64\n", pReply->u.ListOpen.uHandle));
                    break;
                }

                case VBOX_SHCL_TX_REPLYMSGTYPE_LIST_CLOSE:
                {
                    if (cParms > idxParm)
                        rc = HGCMSvcGetU64(&aParms[idxParm], &pReply->u.ListClose.uHandle);
                    else
                        rc = VERR_INVALID_PARAMETER;

                    LogFlowFunc(("hListClose=%RU64\n", pReply->u.ListClose.uHandle));
                    break;
                }

                case VBOX_SHCL_TX_REPLYMSGTYPE_OBJ_OPEN:
                {
                    if (cParms > idxParm)
                        rc = HGCMSvcGetU64(&aParms[idxParm], &pReply->u.ObjOpen.uHandle);
                    else
                        rc = VERR_INVALID_PARAMETER;

                    LogFlowFunc(("hObjOpen=%RU64\n", pReply->u.ObjOpen.uHandle));
                    break;
                }

                case VBOX_SHCL_TX_REPLYMSGTYPE_OBJ_CLOSE:
                {
                    if (cParms > idxParm)
                        rc = HGCMSvcGetU64(&aParms[idxParm], &pReply->u.ObjClose.uHandle);
                    else
                        rc = VERR_INVALID_PARAMETER;

                    LogFlowFunc(("hObjClose=%RU64\n", pReply->u.ObjClose.uHandle));
                    break;
                }

                default:
                    rc = VERR_NOT_SUPPORTED;
                    break;
            }
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Gets a transfer root list header from HGCM service parameters.
 *
 * @returns VBox status code.
 * @param   cParms              Number of HGCM parameters supplied in \a aParms.
 * @param   aParms              Array of HGCM parameters.
 * @param   pRootLstHdr         Where to store the transfer root list header on success.
 */
static int shClSvcTransferMsgGetRootListHdr(uint32_t cParms, VBOXHGCMSVCPARM aParms[],
                                            PSHCLLISTHDR pRootLstHdr)
{
    int rc;

    if (cParms == VBOX_SHCL_CPARMS_ROOT_LIST_HDR_WRITE)
    {
        rc = HGCMSvcGetU32(&aParms[1], &pRootLstHdr->fFeatures);
        if (RT_SUCCESS(rc))
            rc = HGCMSvcGetU64(&aParms[2], &pRootLstHdr->cEntries);
        if (RT_SUCCESS(rc))
        {
            pRootLstHdr->cbTotalSize = 0;
            if (!ShClTransferListHdrIsValid(pRootLstHdr))
                rc = VERR_INVALID_PARAMETER;
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Gets a transfer root list entry from HGCM service parameters.
 *
 * @returns VBox status code.
 * @param   cParms              Number of HGCM parameters supplied in \a aParms.
 * @param   aParms              Array of HGCM parameters.
 * @param   pListEntry          Where to store the root list entry.
 */
static int shClSvcTransferMsgGetRootListEntry(uint32_t cParms, VBOXHGCMSVCPARM aParms[],
                                              PSHCLLISTENTRY pListEntry)
{
    int rc;

    if (cParms == VBOX_SHCL_CPARMS_ROOT_LIST_ENTRY_WRITE)
    {
        rc = HGCMSvcGetU32(&aParms[1], &pListEntry->fInfo);
        /* Note: aParms[2] contains the entry index, currently being ignored. */
        if (RT_SUCCESS(rc))
            rc = HGCMSvcGetPv(&aParms[3], (void **)&pListEntry->pszName, &pListEntry->cbName);
        if (RT_SUCCESS(rc))
        {
            uint32_t cbInfo;
            rc = HGCMSvcGetU32(&aParms[4], &cbInfo);
            if (RT_SUCCESS(rc))
            {
                rc = HGCMSvcGetPv(&aParms[5], &pListEntry->pvInfo, &pListEntry->cbInfo);
                AssertReturn(cbInfo == pListEntry->cbInfo, VERR_INVALID_PARAMETER);
            }
        }
        if (RT_SUCCESS(rc))
        {
            if (!ShClTransferListEntryIsValid(pListEntry))
                rc = VERR_INVALID_PARAMETER;
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Gets a transfer list open request from HGCM service parameters.
 *
 * @returns VBox status code.
 * @param   cParms              Number of HGCM parameters supplied in \a aParms.
 * @param   aParms              Array of HGCM parameters.
 * @param   pOpenParms          Where to store the open parameters of the request.
 */
static int shClSvcTransferMsgGetListOpen(uint32_t cParms, VBOXHGCMSVCPARM aParms[],
                                         PSHCLLISTOPENPARMS pOpenParms)
{
    int rc;

    if (cParms == VBOX_SHCL_CPARMS_LIST_OPEN)
    {
        rc = HGCMSvcGetU32(&aParms[1], &pOpenParms->fList);
        if (RT_SUCCESS(rc))
            rc = HGCMSvcGetStr(&aParms[2], &pOpenParms->pszFilter, &pOpenParms->cbFilter);
        if (RT_SUCCESS(rc))
            rc = HGCMSvcGetStr(&aParms[3], &pOpenParms->pszPath, &pOpenParms->cbPath);

        if (RT_SUCCESS(rc))
        {
            if (pOpenParms->fList & ~VBOX_SHCL_LIST_F_VALID_MASK)
                rc = VERR_INVALID_FLAGS;
            else if (   pOpenParms->pszFilter
                     && !RTStrIsValidEncoding(pOpenParms->pszFilter))
                rc = VERR_INVALID_UTF8_ENCODING;
            else if (pOpenParms->pszPath)
                rc = ShClTransferValidatePath(pOpenParms->pszPath, false /* fMustExist */);
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Gets a transfer list header from HGCM service parameters.
 *
 * @returns VBox status code.
 * @param   cParms              Number of HGCM parameters supplied in \a aParms.
 * @param   aParms              Array of HGCM parameters.
 * @param   phList              Where to store the list handle.
 * @param   pListHdr            Where to store the list header.
 */
static int shClSvcTransferMsgGetListHdr(uint32_t cParms, VBOXHGCMSVCPARM aParms[],
                                        PSHCLLISTHANDLE phList, PSHCLLISTHDR pListHdr)
{
    int rc;

    if (cParms == VBOX_SHCL_CPARMS_LIST_HDR)
    {
        rc = HGCMSvcGetU64(&aParms[1], phList);
        /* Note: Flags (aParms[2]) not used here. */
        if (RT_SUCCESS(rc))
            rc = HGCMSvcGetU32(&aParms[3], &pListHdr->fFeatures);
        if (RT_SUCCESS(rc))
            rc = HGCMSvcGetU64(&aParms[4], &pListHdr->cEntries);
        if (RT_SUCCESS(rc))
            rc = HGCMSvcGetU64(&aParms[5], &pListHdr->cbTotalSize);

        if (RT_SUCCESS(rc))
        {
            /** @todo Validate pvMetaFmt + cbMetaFmt. */
            /** @todo Validate header checksum. */
            if (!ShClTransferListHdrIsValid(pListHdr))
                rc = VERR_INVALID_PARAMETER;
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sets a transfer list header to HGCM service parameters.
 *
 * @returns VBox status code.
 * @param   cParms              Number of HGCM parameters supplied in \a aParms.
 * @param   aParms              Array of HGCM parameters.
 * @param   pListHdr            Pointer to list header to set.
 */
static int shClSvcTransferMsgSetListHdr(uint32_t cParms, VBOXHGCMSVCPARM aParms[], PSHCLLISTHDR pListHdr)
{
    int rc;

    if (cParms == VBOX_SHCL_CPARMS_LIST_HDR)
    {
        /** @todo Set pvMetaFmt + cbMetaFmt. */
        /** @todo Calculate header checksum. */

        HGCMSvcSetU32(&aParms[3], pListHdr->fFeatures);
        HGCMSvcSetU64(&aParms[4], pListHdr->cEntries);
        HGCMSvcSetU64(&aParms[5], pListHdr->cbTotalSize);

        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Gets a transfer list entry from HGCM service parameters.
 *
 * @returns VBox status code.
 * @param   cParms              Number of HGCM parameters supplied in \a aParms.
 * @param   aParms              Array of HGCM parameters.
 * @param   phList              Where to store the list handle.
 * @param   pListEntry          Where to store the list entry.
 */
static int shClSvcTransferMsgGetListEntry(uint32_t cParms, VBOXHGCMSVCPARM aParms[],
                                          PSHCLLISTHANDLE phList, PSHCLLISTENTRY pListEntry)
{
    int rc;

    if (cParms == VBOX_SHCL_CPARMS_LIST_ENTRY)
    {
        rc = HGCMSvcGetU64(&aParms[1], phList);
        if (RT_SUCCESS(rc))
            rc = HGCMSvcGetU32(&aParms[2], &pListEntry->fInfo);
        if (RT_SUCCESS(rc))
            rc = HGCMSvcGetPv(&aParms[3], (void **)&pListEntry->pszName, &pListEntry->cbName);
        if (RT_SUCCESS(rc))
        {
            uint32_t cbInfo;
            rc = HGCMSvcGetU32(&aParms[4], &cbInfo);
            if (RT_SUCCESS(rc))
            {
                rc = HGCMSvcGetPv(&aParms[5], &pListEntry->pvInfo, &pListEntry->cbInfo);
                AssertReturn(cbInfo == pListEntry->cbInfo, VERR_INVALID_PARAMETER);
            }
        }

        if (RT_SUCCESS(rc))
        {
            if (!ShClTransferListEntryIsValid(pListEntry))
                rc = VERR_INVALID_PARAMETER;
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Sets a Shared Clipboard list entry to HGCM service parameters.
 *
 * @returns VBox status code.
 * @param   cParms              Number of HGCM parameters supplied in \a aParms.
 * @param   aParms              Array of HGCM parameters.
 * @param   pEntry              Pointer list entry to set.
 */
static int shClSvcTransferMsgSetListEntry(uint32_t cParms, VBOXHGCMSVCPARM aParms[],
                                          PSHCLLISTENTRY pEntry)
{
    int rc;

    /* Sanity. */
    AssertReturn(ShClTransferListEntryIsValid(pEntry), VERR_INVALID_PARAMETER);

    if (cParms == VBOX_SHCL_CPARMS_LIST_ENTRY)
    {
        /* Entry name */
        void  *pvDst = aParms[3].u.pointer.addr;
        size_t cbDst = aParms[3].u.pointer.size;
        memcpy(pvDst, pEntry->pszName, RT_MIN(pEntry->cbName, cbDst));

        /* Info size */
        HGCMSvcSetU32(&aParms[4], pEntry->cbInfo);

        /* Info data */
        pvDst = aParms[5].u.pointer.addr;
        cbDst = aParms[5].u.pointer.size;
        memcpy(pvDst, pEntry->pvInfo, RT_MIN(pEntry->cbInfo, cbDst));

        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Gets a transfer object data chunk from HGCM service parameters.
 *
 * @returns VBox status code.
 * @param   cParms              Number of HGCM parameters supplied in \a aParms.
 * @param   aParms              Array of HGCM parameters.
 * @param   pDataChunk          Where to store the object data chunk data.
 */
static int shClSvcTransferGetObjDataChunk(uint32_t cParms, VBOXHGCMSVCPARM aParms[], PSHCLOBJDATACHUNK pDataChunk)
{
    AssertPtrReturn(aParms,    VERR_INVALID_PARAMETER);
    AssertPtrReturn(pDataChunk, VERR_INVALID_PARAMETER);

    int rc;

    if (cParms == VBOX_SHCL_CPARMS_OBJ_WRITE)
    {
        rc = HGCMSvcGetU64(&aParms[1], &pDataChunk->uHandle);
        if (RT_SUCCESS(rc))
        {
            uint32_t cbToRead;
            rc = HGCMSvcGetU32(&aParms[2], &cbToRead);
            if (RT_SUCCESS(rc))
            {
                rc = HGCMSvcGetPv(&aParms[3], &pDataChunk->pvData, &pDataChunk->cbData);
                if (RT_SUCCESS(rc))
                    rc = cbToRead == pDataChunk->cbData ? VINF_SUCCESS : VERR_INVALID_PARAMETER;
            }

            /** @todo Implement checksum handling. */
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Handles a guest reply (VBOX_SHCL_GUEST_FN_REPLY) message.
 *
 * @returns VBox status code.
 * @param   pClient             Pointer to associated client.
 * @param   pTransfer           Transfer to handle reply for.
 * @param   cParms              Number of function parameters supplied.
 * @param   aParms              Array function parameters supplied.
 * @param   fZeroContext        Whether the guest supplied the special zero context ID.
 * @param   pfDestroyTransfer   Where to return whether the caller must destroy
 *                              the retained transfer after releasing it.
 */
static int shClSvcTransferMsgHandleReply(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, uint32_t cParms,
                                         VBOXHGCMSVCPARM aParms[], bool fZeroContext, bool *pfDestroyTransfer)
{
    AssertPtrReturn(pfDestroyTransfer, VERR_INVALID_POINTER);
    *pfDestroyTransfer = false;

    LogFlowFunc(("pTransfer=%p\n", pTransfer));

    int  rc;
    bool fReleaseCreatedTransfer = false;

    uint32_t   cbReply = sizeof(SHCLREPLY);
    PSHCLREPLY pReply  = (PSHCLREPLY)RTMemAllocZ(cbReply);
    if (pReply)
    {
        rc = shClSvcTransferMsgGetReply(cParms, aParms, pReply);
        if (RT_SUCCESS(rc))
        {
            if (   pReply->uType                    == VBOX_SHCL_TX_REPLYMSGTYPE_TRANSFER_STATUS
                && pReply->u.TransferStatus.uStatus == SHCLTRANSFERSTATUS_REQUESTED)
            {
                /* SHCLTRANSFERSTATUS_REQUESTED is special, as it doesn't provide a transfer. */
                if (!fZeroContext)
                    rc = VERR_INVALID_CONTEXT;
            }
            else /* Everything else needs a valid transfer ID. */
            {
                if (!pTransfer)
                {
                    LogRelMax(16, ("Shared Clipboard: Guest reply did not specify a valid transfer context (reply type=%RU32)\n",
                                                        pReply->uType));
                    rc = VERR_SHCLPB_TRANSFER_ID_NOT_FOUND;
                }
            }

            if (RT_FAILURE(rc))
            {
                RTMemFree(pReply);
                pReply = NULL;

                return rc;
            }

            PSHCLEVENTPAYLOAD pPayload
                = (PSHCLEVENTPAYLOAD)RTMemAlloc(sizeof(SHCLEVENTPAYLOAD));
            if (pPayload)
            {
                pPayload->pvData = pReply;
                pPayload->cbData = cbReply;

                SHCLTRANSFERID const idTransfer = pTransfer ? ShClTransferGetID(pTransfer) : NIL_SHCLTRANSFERID;

                switch (pReply->uType)
                {
                    case VBOX_SHCL_TX_REPLYMSGTYPE_TRANSFER_STATUS:
                    {
                        LogRel2(("Shared Clipboard: Guest reported status %s for transfer %RU16\n",
                                 ShClTransferStatusToStr(pReply->u.TransferStatus.uStatus), idTransfer));

                        /* SHCLTRANSFERSTATUS_REQUESTED is special, as it doesn't provide a transfer ID. */
                        if (SHCLTRANSFERSTATUS_REQUESTED == pReply->u.TransferStatus.uStatus)
                        {
                            LogRelMax2(16, ("Shared Clipboard: Guest requested a new host -> guest transfer\n"));
                        }

                        switch (pReply->u.TransferStatus.uStatus)
                        {
                            case SHCLTRANSFERSTATUS_REQUESTED: /* Guest requests a H->G transfer. */
                            {
                                uint32_t const uMode = ShClSvcGetMode();
                                if (   uMode == VBOX_SHCL_MODE_HOST_TO_GUEST
                                    || uMode == VBOX_SHCL_MODE_BIDIRECTIONAL)
                                {
                                    /* We only create (but not initialize) the transfer here. This is the most lightweight form of
                                     * having a pending transfer around. Report back the new transfer ID to the guest then. */
                                    if (pTransfer == NULL) /* Must not exist yet. */
                                    {
                                        SHCLTRANSFERCALLBACKS Callbacks;
                                        rc = shClSvcExtQueryTransferCallbacks(pClient, &Callbacks);
                                        if (rc == VERR_NOT_SUPPORTED)
                                        {
                                            RT_ZERO(Callbacks);
                                            rc = VINF_SUCCESS;
                                        }
                                        if (RT_SUCCESS(rc))
                                            rc = ShClSvcTransferCreate(pClient, SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL,
                                                                       &Callbacks,
                                                                       NIL_SHCLTRANSFERID /* Creates a new transfer ID */,
                                                                       &pTransfer);
                                        if (RT_SUCCESS(rc))
                                        {
                                            fReleaseCreatedTransfer = true;

                                            ShClSvcClientLock(pClient);

                                            rc = shClSvcTransferSendStatusAsync(pClient, pTransfer,
                                                                                SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS,
                                                                                NULL);
                                            ShClSvcClientUnlock(pClient);
                                        }
                                    }
                                    else
                                        rc = VERR_WRONG_ORDER;
                                }
                                else
                                {
                                    LogRelMax(16, ("Shared Clipboard: Guest requested host -> guest transfer, but clipboard mode %RU32 does not allow it\n",
                                                    uMode));
                                    rc = VERR_INVALID_PARAMETER;
                                }

                                break;
                            }

                            case SHCLTRANSFERSTATUS_INITIALIZED: /* Guest reports the transfer as being initialized. */
                            {
                                switch (ShClTransferGetDir(pTransfer))
                                {
                                    case SHCLTRANSFERDIR_FROM_REMOTE: /* G->H */
                                        /* Already done locally when creating the transfer. */
                                        break;

                                    case SHCLTRANSFERDIR_TO_REMOTE:   /* H->G */
                                    {
                                        /* Initialize the transfer on the host side. */
                                        rc = ShClSvcTransferInit(pClient, pTransfer);
                                        break;
                                    }

                                    default:
                                        AssertFailed();
                                        break;
                                }

                                break;
                            }
                            case SHCLTRANSFERSTATUS_STARTED:     /* Guest has started the transfer on its side. */
                            {
                                /* We only need to start for H->G transfers here.
                                 * For G->H transfers we start this as soon as the host clipboard requests data. */
                                if (ShClTransferGetDir(pTransfer) == SHCLTRANSFERDIR_TO_REMOTE)
                                {
                                    /* Start the transfer on the host side. */
                                    rc = ShClSvcTransferStart(pClient, pTransfer);
                                }
                                break;
                            }

                            case SHCLTRANSFERSTATUS_CANCELED:
                                RT_FALL_THROUGH();
                            case SHCLTRANSFERSTATUS_KILLED:
                            {
                                LogRel2(("Shared Clipboard: Guest has %s transfer %RU16\n",
                                         pReply->u.TransferStatus.uStatus == SHCLTRANSFERSTATUS_CANCELED ? "canceled" : "killed", idTransfer));

                                switch (pReply->u.TransferStatus.uStatus)
                                {
                                    case SHCLTRANSFERSTATUS_CANCELED:
                                        rc = ShClTransferCancel(pTransfer);
                                        break;

                                    case SHCLTRANSFERSTATUS_KILLED:
                                        rc = ShClTransferKill(pTransfer);
                                        break;

                                    default:
                                        AssertFailed();
                                        break;
                                }

                                break;
                            }

                            case SHCLTRANSFERSTATUS_COMPLETED:
                            {
                                LogRel2(("Shared Clipboard: Guest has completed transfer %RU16\n", idTransfer));

                                rc = ShClTransferComplete(pTransfer);
                                break;
                            }

                            case SHCLTRANSFERSTATUS_ERROR:
                            {
                                LogRelMax(16, ("Shared Clipboard: Guest reported error %Rrc for transfer %RU16\n",
                                               pReply->rc, pTransfer->State.uID));

                                if (shClSvcExtIsRegistered())
                                {
                                    char *pszMsg = RTStrAPrintf2("Guest reported error %Rrc for transfer %RU16", /** @todo Make the error messages more fine-grained based on rc. */
                                                                 pReply->rc, pTransfer->State.uID);
                                    AssertPtrBreakStmt(pszMsg, rc = VERR_NO_MEMORY);

                                    (void) shClSvcExtReportError(NULL, pszMsg, pReply->rc);

                                    RTStrFree(pszMsg);
                                    pszMsg = NULL;
                                }

                                rc = ShClTransferError(pTransfer, pReply->rc);
                                break;
                            }

                            default:
                            {
                                LogRelMax(16, ("Shared Clipboard: Unknown transfer status %#x from guest received\n",
                                               pReply->u.TransferStatus.uStatus));
                                rc = VERR_INVALID_PARAMETER;
                                break;
                            }
                        }

                        /* Notify the service extension. */
                        if (shClSvcExtIsRegistered())
                        {
                            int rc2 = shClSvcExtNotifyTransferStatus(pClient, pTransfer, SHCLSOURCE_REMOTE, pReply);
                            if (RT_SUCCESS(rc))
                                rc = rc2;
                        }
                        RT_FALL_THROUGH(); /* Make sure to also signal any waiters by using the block down below. */
                    }
                    case VBOX_SHCL_TX_REPLYMSGTYPE_LIST_OPEN:
                        RT_FALL_THROUGH();
                    case VBOX_SHCL_TX_REPLYMSGTYPE_LIST_CLOSE:
                        RT_FALL_THROUGH();
                    case VBOX_SHCL_TX_REPLYMSGTYPE_OBJ_OPEN:
                        RT_FALL_THROUGH();
                    case VBOX_SHCL_TX_REPLYMSGTYPE_OBJ_CLOSE:
                    {
                        uint64_t uCID;
                        rc = HGCMSvcGetU64(&aParms[0], &uCID);
                        if (RT_SUCCESS(rc))
                        {
                            const PSHCLEVENT pEvent
                                = ShClEventSourceGetFromId(&pTransfer->Events, VBOX_SHCL_CONTEXTID_GET_EVENT(uCID));
                            if (pEvent)
                            {
                                LogFlowFunc(("uCID=%RU64 -> idEvent=%RU32, rcReply=%Rrc\n", uCID, pEvent->idEvent, pReply->rc));

                                rc = ShClEventSignalEx(pEvent, pReply->rc, pPayload);
                                if (RT_SUCCESS(rc))
                                {
                                    pPayload = NULL; /* The event owns the payload now. */
                                    pReply   = NULL; /* The payload owns the reply now. */
                                }
                            }
                        }
                        break;
                    }

                    default:
                        LogRelMax(16, ("Shared Clipboard: Unknown reply type %#x from guest received\n", pReply->uType));
                        ShClTransferCancel(pTransfer); /* Avoid clogging up the transfer list. */
                        rc = VERR_INVALID_PARAMETER;
                        break;
                }

                if (   pTransfer
                    && (   ShClTransferIsAborted(pTransfer)
                        || ShClTransferIsComplete(pTransfer)))
                    *pfDestroyTransfer = true;

                if (pPayload)
                {
                    RTMemFree(pPayload);
                    pPayload = NULL;
                }
                if (pReply)
                {
                    RTMemFree(pReply);
                    pReply = NULL;
                }
            }
            else
                rc = VERR_NO_MEMORY;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    if (RT_FAILURE(rc))
    {
        if (pReply)
            RTMemFree(pReply);
    }

    if (fReleaseCreatedTransfer)
        ShClTransferRelease(pTransfer);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Transfer message client (guest) handler for the Shared Clipboard host service.
 *
 * @returns VBox status code, or VINF_HGCM_ASYNC_EXECUTE if returning to the client will be deferred.
 * @param   pClient             Pointer to associated client.
 * @param   callHandle          The client's call handle of this call.
 * @param   u32Function         Function number being called.
 * @param   cParms              Number of function parameters supplied.
 * @param   aParms              Array function parameters supplied.
 * @param   tsArrival           Timestamp of arrival.
 */
int ShClSvcTransferMsgClientHandler(PSHCLCLIENT pClient,
                                    VBOXHGCMCALLHANDLE callHandle,
                                    uint32_t u32Function,
                                    uint32_t cParms,
                                    VBOXHGCMSVCPARM aParms[],
                                    uint64_t tsArrival)
{
    RT_NOREF(callHandle, aParms, tsArrival);

    LogFlowFunc(("uClient=%RU32, u32Function=%RU32 (%s), cParms=%RU32, fExtRegistered=%RTbool\n",
                 pClient->State.uClientID, u32Function, ShClSvcGuestMsgToStr(u32Function), cParms,
                 shClSvcExtIsRegistered()));

    uint64_t const fGuestFeatures0 = ShClSvcClientGetGuestFeatures0(pClient);
    if (   u32Function > VBOX_SHCL_GUEST_FN_LAST
        || !(fGuestFeatures0 & VBOX_SHCL_GF_0_CONTEXT_ID))
        return VERR_NOT_IMPLEMENTED;

    if (!(fGuestFeatures0 & VBOX_SHCL_GF_0_TRANSFERS))
    {
        LogRelMax(16, ("Shared Clipboard: Guest attempted file transfer message %s without negotiated transfer support (features0=%#RX64)\n",
                        ShClSvcGuestMsgToStr(u32Function), fGuestFeatures0));
        return VERR_ACCESS_DENIED;
    }

    uint32_t const fTransferMode = shClSvcTransferModeGet();
    if (!(fTransferMode & VBOX_SHCL_TRANSFER_MODE_F_ENABLED))
    {
        LogRelMax(16, ("Shared Clipboard: Guest attempted file transfer message %s, but file transfers are disabled for this VM (transfer mode=%#x)\n",
                        ShClSvcGuestMsgToStr(u32Function), fTransferMode));
        return VERR_ACCESS_DENIED;
    }

    /* Check if we've the right mode set. */
    uint32_t const uMode = ShClSvcGetMode();
    if (!shClSvcTransferMsgIsAllowed(uMode, u32Function))
    {
        LogRelMax(16, ("Shared Clipboard: Guest file transfer message %s is not allowed in clipboard mode %RU32\n",
                        ShClSvcGuestMsgToStr(u32Function), uMode));
        return VERR_ACCESS_DENIED;
    }

    int rc = VERR_INVALID_PARAMETER; /* Play safe by default. */

    if (cParms < 1)
        return rc;
    ASSERT_GUEST_RETURN(aParms[0].type == VBOX_HGCM_SVC_PARM_64BIT, VERR_WRONG_PARAMETER_TYPE);

    uint64_t uCID  = 0; /* Context ID */
    rc = HGCMSvcGetU64(&aParms[0], &uCID);
    if (RT_FAILURE(rc))
        return rc;

    bool const fZeroContext = uCID == 0;
    if (fZeroContext)
    {
        /* A guest requests a new host -> guest transfer by sending
         * SHCLTRANSFERSTATUS_REQUESTED without an existing transfer context. */
        if (u32Function != VBOX_SHCL_GUEST_FN_REPLY)
        {
            LogRelMax(16, ("Shared Clipboard: Guest file transfer message %s used zero context ID; only transfer status replies may do this\n",
                            ShClSvcGuestMsgToStr(u32Function)));
            return VERR_INVALID_CONTEXT;
        }
    }
    else if (VBOX_SHCL_CONTEXTID_GET_SESSION(uCID) != pClient->State.uSessionID)
    {
        LogRelMax(16, ("Shared Clipboard: Guest file transfer message %s used context %#RX64 for session %RU32, expected session %RU32\n",
                        ShClSvcGuestMsgToStr(u32Function), uCID, VBOX_SHCL_CONTEXTID_GET_SESSION(uCID), pClient->State.uSessionID));
        return VERR_INVALID_CONTEXT;
    }

    /*
     * Pre-check: For certain messages we need to make sure that a (right) transfer is present.
     */
    const SHCLTRANSFERID idTransfer = fZeroContext ? NIL_SHCLTRANSFERID : VBOX_SHCL_CONTEXTID_GET_TRANSFER(uCID);
    PSHCLTRANSFER        pTransfer  = fZeroContext ? NULL
                                                  : ShClTransferCtxGetTransferByIdRetained(&pClient->Transfers.Ctx,
                                                                                           idTransfer);

    if (   u32Function != VBOX_SHCL_GUEST_FN_REPLY
        && !pTransfer)
    {
        LogRelMax(16, ("Shared Clipboard: Guest file transfer message %s references unknown transfer %RU16 (context=%#RX64)\n",
                        ShClSvcGuestMsgToStr(u32Function), idTransfer, uCID));
        return VERR_SHCLPB_TRANSFER_ID_NOT_FOUND;
    }

    rc = VERR_INVALID_PARAMETER; /* Play safe. */
    bool fDestroyTransfer = false;

    switch (u32Function)
    {
        case VBOX_SHCL_GUEST_FN_REPLY:
        {
            rc = shClSvcTransferMsgHandleReply(pClient, pTransfer, cParms, aParms, fZeroContext, &fDestroyTransfer);
            break;
        }

        case VBOX_SHCL_GUEST_FN_ROOT_LIST_HDR_READ:
        {
            if (cParms != VBOX_SHCL_CPARMS_ROOT_LIST_HDR_READ)
                break;

            ASSERT_GUEST_STMT_BREAK(aParms[1].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* Features */
            ASSERT_GUEST_STMT_BREAK(aParms[2].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* # Entries  */

            SHCLLISTHDR rootListHdr;
            RT_ZERO(rootListHdr);

            rootListHdr.cEntries = ShClTransferRootsCount(pTransfer);
            /** @todo BUGBUG What about the features? */

            HGCMSvcSetU64(&aParms[0], 0 /* Context ID */);
            HGCMSvcSetU32(&aParms[1], rootListHdr.fFeatures);
            HGCMSvcSetU64(&aParms[2], rootListHdr.cEntries);

            rc = VINF_SUCCESS;
            break;
        }

        case VBOX_SHCL_GUEST_FN_ROOT_LIST_HDR_WRITE:
        {
            SHCLLISTHDR lstHdr;
            rc = shClSvcTransferMsgGetRootListHdr(cParms, aParms, &lstHdr);
            if (RT_SUCCESS(rc))
            {
                void    *pvData = ShClTransferListHdrDup(&lstHdr);
                uint32_t cbData = sizeof(SHCLLISTHDR);

                const PSHCLEVENT pEvent = ShClEventSourceGetFromId(&pTransfer->Events, VBOX_SHCL_CONTEXTID_GET_EVENT(uCID));
                if (pEvent)
                {
                    PSHCLEVENTPAYLOAD pPayload;
                    rc = ShClPayloadCreateDupData(pEvent->idEvent, pvData, cbData, &pPayload);
                    if (RT_SUCCESS(rc))
                    {
                        rc = ShClEventSignal(pEvent, pPayload);
                        if (RT_FAILURE(rc))
                            ShClPayloadDestroy(pPayload);
                    }
                }
                else
                    rc = VERR_SHCLPB_EVENT_ID_NOT_FOUND;
            }
            break;
        }

        case VBOX_SHCL_GUEST_FN_ROOT_LIST_ENTRY_READ:
        {
            if (cParms != VBOX_SHCL_CPARMS_ROOT_LIST_ENTRY_READ)
                break;

            ASSERT_GUEST_STMT_BREAK(aParms[1].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* Info flags */
            ASSERT_GUEST_STMT_BREAK(aParms[2].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* Entry index # */
            ASSERT_GUEST_STMT_BREAK(aParms[3].type == VBOX_HGCM_SVC_PARM_PTR,   rc = VERR_WRONG_PARAMETER_TYPE); /* Entry name */
            ASSERT_GUEST_STMT_BREAK(aParms[4].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* Info size */
            ASSERT_GUEST_STMT_BREAK(aParms[5].type == VBOX_HGCM_SVC_PARM_PTR,   rc = VERR_WRONG_PARAMETER_TYPE); /* Info data */

            uint32_t fInfo;
            rc = HGCMSvcGetU32(&aParms[1], &fInfo);
            AssertRCBreak(rc);

            ASSERT_GUEST_STMT_BREAK(fInfo & VBOX_SHCL_INFO_F_FSOBJINFO, rc = VERR_WRONG_PARAMETER_TYPE); /* Validate info flags.  */

            uint64_t uIdx;
            rc = HGCMSvcGetU64(&aParms[2], &uIdx);
            AssertRCBreak(rc);

            PCSHCLLISTENTRY pEntry = ShClTransferRootsEntryGet(pTransfer, uIdx);
            if (pEntry)
            {
                /* Entry name */
                void  *pvDst = aParms[3].u.pointer.addr;
                size_t cbDst = aParms[3].u.pointer.size;
                memcpy(pvDst, pEntry->pszName, RT_MIN(pEntry->cbName, cbDst));

                /* Info size */
                HGCMSvcSetU32(&aParms[4], pEntry->cbInfo);

                /* Info data */
                pvDst = aParms[5].u.pointer.addr;
                cbDst = aParms[5].u.pointer.size;
                memcpy(pvDst, pEntry->pvInfo, RT_MIN(pEntry->cbInfo, cbDst));
            }
            else
                rc = VERR_NOT_FOUND;

            break;
        }

        case VBOX_SHCL_GUEST_FN_ROOT_LIST_ENTRY_WRITE:
        {
            SHCLLISTENTRY lstEntry;
            rc = shClSvcTransferMsgGetRootListEntry(cParms, aParms, &lstEntry);
            if (RT_SUCCESS(rc))
            {
                void    *pvData = ShClTransferListEntryDup(&lstEntry);
                uint32_t cbData = sizeof(SHCLLISTENTRY);

                const PSHCLEVENT pEvent = ShClEventSourceGetFromId(&pTransfer->Events, VBOX_SHCL_CONTEXTID_GET_EVENT(uCID));
                if (pEvent)
                {
                    PSHCLEVENTPAYLOAD pPayload;
                    rc = ShClPayloadCreateDupData(pEvent->idEvent, pvData, cbData, &pPayload);
                    if (RT_SUCCESS(rc))
                    {
                        rc = ShClEventSignal(pEvent, pPayload);
                        if (RT_FAILURE(rc))
                            ShClPayloadDestroy(pPayload);
                    }
                }
                else
                    rc = VERR_SHCLPB_EVENT_ID_NOT_FOUND;
            }
            break;
        }

        case VBOX_SHCL_GUEST_FN_LIST_OPEN:
        {
            if (cParms != VBOX_SHCL_CPARMS_LIST_OPEN)
                break;

            SHCLLISTOPENPARMS listOpenParms;
            rc = shClSvcTransferMsgGetListOpen(cParms, aParms, &listOpenParms);
            if (RT_SUCCESS(rc))
            {
                SHCLLISTHANDLE hList;
                rc = ShClTransferListOpen(pTransfer, &listOpenParms, &hList);
                if (RT_SUCCESS(rc))
                {
                    /* Return list handle. */
                    HGCMSvcSetU64(&aParms[4], hList);
                }
            }
            break;
        }

        case VBOX_SHCL_GUEST_FN_LIST_CLOSE:
        {
            if (cParms != VBOX_SHCL_CPARMS_LIST_CLOSE)
                break;

            SHCLLISTHANDLE hList;
            rc = HGCMSvcGetU64(&aParms[1], &hList);
            if (RT_SUCCESS(rc))
            {
                rc = ShClTransferListClose(pTransfer, hList);
            }
            break;
        }

        case VBOX_SHCL_GUEST_FN_LIST_HDR_READ:
        {
            if (cParms != VBOX_SHCL_CPARMS_LIST_HDR)
                break;

            SHCLLISTHANDLE hList;
            rc = HGCMSvcGetU64(&aParms[1], &hList); /* Get list handle. */
            if (RT_SUCCESS(rc))
            {
                SHCLLISTHDR hdrList;
                rc = ShClTransferListGetHeader(pTransfer, hList, &hdrList);
                if (RT_SUCCESS(rc))
                    rc = shClSvcTransferMsgSetListHdr(cParms, aParms, &hdrList);
            }
            break;
        }

        case VBOX_SHCL_GUEST_FN_LIST_HDR_WRITE:
        {
            SHCLLISTHDR hdrList;
            rc = ShClTransferListHdrInit(&hdrList);
            if (RT_SUCCESS(rc))
            {
                SHCLLISTHANDLE hList;
                rc = shClSvcTransferMsgGetListHdr(cParms, aParms, &hList, &hdrList);
                if (RT_SUCCESS(rc))
                {
                    void    *pvData = ShClTransferListHdrDup(&hdrList);
                    uint32_t cbData = sizeof(SHCLLISTHDR);

                    const PSHCLEVENT pEvent = ShClEventSourceGetFromId(&pTransfer->Events, VBOX_SHCL_CONTEXTID_GET_EVENT(uCID));
                    if (pEvent)
                    {
                        PSHCLEVENTPAYLOAD pPayload;
                        rc = ShClPayloadCreateDupData(pEvent->idEvent, pvData, cbData, &pPayload);
                        if (RT_SUCCESS(rc))
                        {
                            rc = ShClEventSignal(pEvent, pPayload);
                            if (RT_FAILURE(rc))
                                ShClPayloadDestroy(pPayload);
                        }
                    }
                    else
                        rc = VERR_SHCLPB_EVENT_ID_NOT_FOUND;
                }
            }
            break;
        }

        case VBOX_SHCL_GUEST_FN_LIST_ENTRY_READ:
        {
            if (cParms != VBOX_SHCL_CPARMS_LIST_ENTRY)
                break;

            SHCLLISTHANDLE hList;
            rc = HGCMSvcGetU64(&aParms[1], &hList); /* Get list handle. */
            if (RT_SUCCESS(rc))
            {
                SHCLLISTENTRY entryList;
                rc = ShClTransferListEntryInit(&entryList);
                if (RT_SUCCESS(rc))
                {
                    rc = ShClTransferListRead(pTransfer, hList, &entryList);
                    if (RT_SUCCESS(rc))
                        rc = shClSvcTransferMsgSetListEntry(cParms, aParms, &entryList);
                }
            }
            break;
        }

        case VBOX_SHCL_GUEST_FN_LIST_ENTRY_WRITE:
        {
            SHCLLISTENTRY entryList;
            rc = ShClTransferListEntryInit(&entryList);
            if (RT_SUCCESS(rc))
            {
                SHCLLISTHANDLE hList;
                rc = shClSvcTransferMsgGetListEntry(cParms, aParms, &hList, &entryList);
                if (RT_SUCCESS(rc))
                {
                    void    *pvData = ShClTransferListEntryDup(&entryList);
                    uint32_t cbData = sizeof(SHCLLISTENTRY);

                    const PSHCLEVENT pEvent = ShClEventSourceGetFromId(&pTransfer->Events, VBOX_SHCL_CONTEXTID_GET_EVENT(uCID));
                    if (pEvent)
                    {
                        PSHCLEVENTPAYLOAD pPayload;
                        rc = ShClPayloadCreateDupData(pEvent->idEvent, pvData, cbData, &pPayload);
                        if (RT_SUCCESS(rc))
                        {
                            rc = ShClEventSignal(pEvent, pPayload);
                            if (RT_FAILURE(rc))
                                ShClPayloadDestroy(pPayload);
                        }
                    }
                    else
                        rc = VERR_SHCLPB_EVENT_ID_NOT_FOUND;
                }
            }
            break;
        }

        case VBOX_SHCL_GUEST_FN_OBJ_OPEN:
        {
            ASSERT_GUEST_STMT_BREAK(cParms == VBOX_SHCL_CPARMS_OBJ_OPEN, rc = VERR_WRONG_PARAMETER_COUNT);

            SHCLOBJOPENCREATEPARMS openCreateParms;
            RT_ZERO(openCreateParms);

            /* aParms[1] will return the object handle on success; see below. */
            rc = HGCMSvcGetStr(&aParms[2], &openCreateParms.pszPath, &openCreateParms.cbPath);
            if (RT_SUCCESS(rc))
                rc = HGCMSvcGetU32(&aParms[3], &openCreateParms.fCreate);

            if (RT_SUCCESS(rc))
            {
                SHCLOBJHANDLE hObj;
                rc = ShClTransferObjOpen(pTransfer, &openCreateParms, &hObj);
                if (RT_SUCCESS(rc))
                {
                    LogFlowFunc(("hObj=%RU64\n", hObj));

                    HGCMSvcSetU64(&aParms[1], hObj);
                }
            }
            break;
        }

        case VBOX_SHCL_GUEST_FN_OBJ_CLOSE:
        {
            if (cParms != VBOX_SHCL_CPARMS_OBJ_CLOSE)
                break;

            SHCLOBJHANDLE hObj;
            rc = HGCMSvcGetU64(&aParms[1], &hObj); /* Get object handle. */
            if (RT_SUCCESS(rc))
                rc = ShClTransferObjClose(pTransfer, hObj);
            break;
        }

        case VBOX_SHCL_GUEST_FN_OBJ_READ:
        {
            if (cParms != VBOX_SHCL_CPARMS_OBJ_READ)
                break;

            ASSERT_GUEST_STMT_BREAK(aParms[1].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* Object handle */
            ASSERT_GUEST_STMT_BREAK(aParms[2].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* Bytes to read */
            ASSERT_GUEST_STMT_BREAK(aParms[3].type == VBOX_HGCM_SVC_PARM_PTR,   rc = VERR_WRONG_PARAMETER_TYPE); /* Data buffer */
            ASSERT_GUEST_STMT_BREAK(aParms[4].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* Checksum data size */
            ASSERT_GUEST_STMT_BREAK(aParms[5].type == VBOX_HGCM_SVC_PARM_PTR,   rc = VERR_WRONG_PARAMETER_TYPE); /* Checksum data buffer*/

            SHCLOBJHANDLE hObj;
            rc = HGCMSvcGetU64(&aParms[1], &hObj); /* Get object handle. */
            AssertRCBreak(rc);

            uint32_t cbToRead = 0;
            rc = HGCMSvcGetU32(&aParms[2], &cbToRead);
            AssertRCBreak(rc);

            void    *pvBuf = NULL;
            uint32_t cbBuf = 0;
            rc = HGCMSvcGetPv(&aParms[3], &pvBuf, &cbBuf);
            AssertRCBreak(rc);

            LogFlowFunc(("hObj=%RU64, cbBuf=%RU32, cbToRead=%RU32, rc=%Rrc\n", hObj, cbBuf, cbToRead, rc));

            if (   RT_SUCCESS(rc)
                && (   !cbBuf
                    || !cbToRead
                    ||  cbBuf < cbToRead
                    ||  cbToRead > pTransfer->cbMaxChunkSize
                   )
               )
            {
                rc = VERR_INVALID_PARAMETER;
            }

            if (RT_SUCCESS(rc))
            {
                uint32_t cbRead;
                rc = ShClTransferObjRead(pTransfer, hObj, pvBuf, cbToRead, 0 /* fFlags */, &cbRead);
                if (RT_SUCCESS(rc))
                {
                    HGCMSvcSetU32(&aParms[2], cbRead);

                    /** @todo Implement checksum support. */
                }
            }
            break;
        }

        case VBOX_SHCL_GUEST_FN_OBJ_WRITE:
        {
            SHCLOBJDATACHUNK dataChunk;

            rc = shClSvcTransferGetObjDataChunk(cParms, aParms, &dataChunk);
            if (   RT_SUCCESS(rc)
                && dataChunk.cbData > pTransfer->cbMaxChunkSize)
                rc = VERR_BUFFER_OVERFLOW;
            if (RT_SUCCESS(rc))
            {
                void    *pvData = ShClTransferObjDataChunkDup(&dataChunk);
                uint32_t cbData = sizeof(SHCLOBJDATACHUNK);

                const PSHCLEVENT pEvent = ShClEventSourceGetFromId(&pTransfer->Events, VBOX_SHCL_CONTEXTID_GET_EVENT(uCID));
                if (pEvent)
                {
                    PSHCLEVENTPAYLOAD pPayload;
                    rc = ShClPayloadCreateDupData(pEvent->idEvent, pvData, cbData, &pPayload);
                    if (RT_SUCCESS(rc))
                    {
                        rc = ShClEventSignal(pEvent, pPayload);
                        if (RT_FAILURE(rc))
                            ShClPayloadDestroy(pPayload);
                    }
                }
                else
                    rc = VERR_SHCLPB_EVENT_ID_NOT_FOUND;
            }

            break;
        }

        default:
            rc = VERR_NOT_IMPLEMENTED;
            break;
    }

    /* If anything wrong has happened, make sure to unregister the transfer again (if not done already) and tell the guest. */
    if (   RT_FAILURE(rc)
        && pTransfer)
    {
        ShClSvcClientLock(pClient);

        /* Let the guest know. */
        int rc2 = shClSvcTransferSendStatusAsync(pClient, pTransfer,
                                                 SHCLTRANSFERSTATUS_ERROR, rc, NULL /* ppEvent */);
        AssertRC(rc2);

        ShClSvcClientUnlock(pClient);

        fDestroyTransfer = true;
    }

    if (pTransfer)
    {
        /* A consuming destroy cannot wait while this handler retains the transfer. */
        ShClTransferRelease(pTransfer);
        pTransfer = NULL;
    }
    if (fDestroyTransfer)
        ShClSvcTransferDestroyById(pClient, idTransfer);

    LogFlowFunc(("[Client %RU32] Returning rc=%Rrc\n", pClient->State.uClientID, rc));
    return rc;
}

/**
 * Transfer message host handler for the Shared Clipboard host service.
 *
 * @returns VBox status code.
 * @param   u32Function         Function number being called.
 * @param   cParms              Number of function parameters supplied.
 * @param   aParms              Array function parameters supplied.
 */
int ShClSvcTransferMsgHostHandler(uint32_t u32Function,
                                  uint32_t cParms,
                                  VBOXHGCMSVCPARM aParms[])
{
    int rc = VERR_NOT_IMPLEMENTED; /* Play safe. */

    switch (u32Function)
    {
        case VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE:
        {
            if (cParms != 1)
                rc = VERR_INVALID_PARAMETER;
            else
            {
                uint32_t fTransferMode;
                rc = HGCMSvcGetU32(&aParms[0], &fTransferMode);
                if (RT_SUCCESS(rc))
                    rc = shClSvcTransferModeSet(fTransferMode);
            }
            break;
        }

        case VBOX_SHCL_HOST_FN_CANCEL:
        {
            if (cParms != 2)
                rc = VERR_INVALID_PARAMETER;
            else
            {
                uint64_t uContextId;
                rc = HGCMSvcGetU64(&aParms[0], &uContextId);
                if (RT_SUCCESS(rc))
                {
                    uint64_t uGeneration;
                    rc = HGCMSvcGetU64(&aParms[1], &uGeneration);
                    if (RT_SUCCESS(rc))
                        rc = shClSvcTransferAbortByHostKey(uContextId, uGeneration,
                                                           SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);
                }
            }
            break;
        }

        case VBOX_SHCL_HOST_FN_ERROR:
        {
            if (cParms != 3)
                rc = VERR_INVALID_PARAMETER;
            else
            {
                uint64_t uContextId;
                rc = HGCMSvcGetU64(&aParms[0], &uContextId);
                if (RT_SUCCESS(rc))
                {
                    uint64_t uGeneration;
                    rc = HGCMSvcGetU64(&aParms[1], &uGeneration);
                    if (RT_SUCCESS(rc))
                    {
                        uint32_t uRcTransfer;
                        rc = HGCMSvcGetU32(&aParms[2], &uRcTransfer);
                        if (RT_SUCCESS(rc))
                        {
                            int const rcTransfer = (int32_t)uRcTransfer;
                            if (   RT_FAILURE(rcTransfer)
                                && rcTransfer != VERR_CANCELLED)
                                rc = shClSvcTransferAbortByHostKey(uContextId, uGeneration,
                                                                   SHCLTRANSFERSTATUS_ERROR, rcTransfer);
                            else
                                rc = VERR_INVALID_PARAMETER;
                        }
                    }
                }
            }
            break;
        }

        default:
            break;

    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Starts a transfer, communicating the status to the guest side.
 *
 * @returns VBox status code.
 * @param   pClient             Client that owns the transfer.
 * @param   pTransfer           Transfer to start.
 */
int ShClSvcTransferStart(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer)
{
    LogRel2(("Shared Clipboard: Starting transfer %RU16 ...\n", pTransfer->State.uID));

    ShClSvcClientLock(pClient);

    int rc = ShClTransferStart(pTransfer);

    /* Let the guest know in any case
     * (so that it can tear down the transfer on error as well). */
    int rc2 = shClSvcTransferSendStatusAsync(pClient, pTransfer,
                                               RT_SUCCESS(rc)
                                             ? SHCLTRANSFERSTATUS_STARTED : SHCLTRANSFERSTATUS_ERROR, rc,
                                             NULL /* ppEvent */);
    if (RT_SUCCESS(rc))
        rc = rc2;

    ShClSvcClientUnlock(pClient);
    return rc;
}

/**
 * Returns the current host service file transfer mode.
 *
 * @returns File transfer mode (VBOX_SHCL_TRANSFER_MODE_F_XXX).
 */
uint32_t shClSvcTransferModeGet(void)
{
    return ASMAtomicReadU32(&g_ShClSvc.fTransferMode);
}

/**
 * Sets the host service's (file) transfer mode.
 *
 * @returns VBox status code.
 * @param   fMode               Transfer mode to set.
 */
static int shClSvcTransferModeSet(uint32_t fMode)
{
    if (fMode & ~VBOX_SHCL_TRANSFER_MODE_F_VALID_MASK)
        return VERR_INVALID_FLAGS;

    shClSvcLock();
    ASMAtomicWriteU32(&g_ShClSvc.fTransferMode, fMode);

    PSHCLCLIENT const pClient = g_ShClSvc.pActiveClient;
    if (pClient)
    {
        ShClSvcClientLock(pClient);
        ASMAtomicWriteU32(&pClient->State.Transfers.uTransferMode, fMode);
        ShClSvcClientUnlock(pClient);
    }

    LogRel2(("Shared Clipboard: File transfers are now %s\n",
             fMode & VBOX_SHCL_TRANSFER_MODE_F_ENABLED ? "enabled" : "disabled"));

    RTLISTANCHOR ListDestroy;
    RTListInit(&ListDestroy);

    /* If file transfers are being disabled, detach all pending transfers from
     * the active client while its weak pointer is stable. */
    if (!(fMode & VBOX_SHCL_TRANSFER_MODE_F_ENABLED))
    {
        if (pClient)
            shClSvcTransferDetachAll(pClient, &ListDestroy);
    }

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    shClSvcUnlock();

    shClSvcTransferDestroyDetachedAll(&ListDestroy);

    return VINF_SUCCESS;
}
