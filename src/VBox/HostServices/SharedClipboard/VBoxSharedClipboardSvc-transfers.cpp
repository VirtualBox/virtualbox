/* $Id: VBoxSharedClipboardSvc-transfers.cpp 115104 2026-08-21 12:11:45Z andreas.loeffler@oracle.com $ */
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
#include <VBox/VMMDev.h>

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


/** Captures immutable transfer status metadata while the transfer is valid. */
static void shClSvcTransferStatusCapture(PSHCLSVCEXTTRANSFERSTATUS pStatus, PSHCLTRANSFER pTransfer,
                                         SHCLSOURCE enmReplySource, SHCLTRANSFERSTATUS enmStatus, int rcStatus)
{
    AssertPtrReturnVoid(pStatus);
    AssertPtrReturnVoid(pTransfer);

    pStatus->idSession         = ShClTransferGetSessionId(pTransfer);
    pStatus->idTransfer        = ShClTransferGetID(pTransfer);
    pStatus->uGeneration       = ShClTransferGetGeneration(pTransfer);
    pStatus->enmDir            = ShClTransferGetDir(pTransfer);
    pStatus->enmTransferSource = ShClTransferGetSource(pTransfer);
    pStatus->enmReplySource    = enmReplySource;
    pStatus->enmStatus         = enmStatus;
    pStatus->rcStatus          = rcStatus;
}


/**
 * Applies a terminal status reported by the guest without replacing a terminal
 * status which already won locally.
 *
 * @returns VBox status code.
 * @param   pTransfer           Transfer to update.
 * @param   enmStatus          Terminal status reported by the guest.
 * @param   rcStatus           Status-specific result code.
 * @param   pfAccepted         Where to return whether this status won the
 *                            terminal transition and should be published.
 */
static int shClSvcTransferApplyGuestTerminalStatus(PSHCLTRANSFER pTransfer, SHCLTRANSFERSTATUS enmStatus,
                                                    int rcStatus, bool *pfAccepted)
{
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertPtrReturn(pfAccepted, VERR_INVALID_POINTER);
    AssertReturn(   enmStatus == SHCLTRANSFERSTATUS_COMPLETED
                 || enmStatus == SHCLTRANSFERSTATUS_CANCELED
                 || enmStatus == SHCLTRANSFERSTATUS_KILLED
                 || enmStatus == SHCLTRANSFERSTATUS_ERROR, VERR_INVALID_PARAMETER);
    AssertReturn(ShClTransferStatusResultIsValid(enmStatus, rcStatus), VERR_INVALID_PARAMETER);

    *pfAccepted = false;

    if (ShClTransferStatusIsTerminal(ShClTransferGetStatus(pTransfer)))
        return VINF_SUCCESS;

    SHCLTRANSFERSTATUS enmNativeStatus = enmStatus;
    int rc;
    switch (enmStatus)
    {
        case SHCLTRANSFERSTATUS_COMPLETED:
            rc = ShClTransferComplete(pTransfer);
            break;

        case SHCLTRANSFERSTATUS_CANCELED:
            rc = ShClTransferCancel(pTransfer);
            break;

        case SHCLTRANSFERSTATUS_KILLED:
            rc = ShClTransferKill(pTransfer);
            enmNativeStatus = SHCLTRANSFERSTATUS_CANCELED; /* ShClTransferKill currently maps to cancellation. */
            break;

        case SHCLTRANSFERSTATUS_ERROR:
            rc = ShClTransferError(pTransfer, rcStatus);
            break;

        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }

    SHCLTRANSFERSTATUS const enmCurrentStatus = ShClTransferGetStatus(pTransfer);
    if (RT_SUCCESS(rc) && enmCurrentStatus == enmNativeStatus)
        *pfAccepted = true;
    else if (ShClTransferStatusIsTerminal(enmCurrentStatus))
        rc = VINF_SUCCESS; /* A concurrent terminal transition won. */

    return rc;
}


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
 * Accounts successfully read object payload and reports exact aggregate
 * progress to Main when an exact total is available.
 *
 * Progress reporting is best-effort and never changes the data-path result.
 *
 * @param   pClient            Service client owning the transfer.
 * @param   pTransfer          Transfer whose payload was read.
 * @param   hObj                Object handle whose sequential stream advanced.
 * @param   cbDelta             Number of successfully read payload bytes.
 */
void ShClSvcTransferReportProgress(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer,
                                   SHCLOBJHANDLE hObj, uint32_t cbDelta)
{
    AssertPtrReturnVoid(pClient);
    AssertPtrReturnVoid(pTransfer);
    if (!cbDelta)
        return;

    uint64_t cbProcessed;
    uint64_t cbTotal;
    bool fNotify;
    int const rc = ShClTransferProgressObjAdd(pTransfer, hObj, cbDelta, &cbProcessed, &cbTotal, &fNotify);
    if (   RT_SUCCESS(rc)
        && fNotify
        && cbTotal > 0)
    {
        SHCLSVCEXTTRANSFERPROGRESS Progress;
        Progress.idSession   = ShClTransferGetSessionId(pTransfer);
        Progress.idTransfer  = ShClTransferGetID(pTransfer);
        Progress.uGeneration = ShClTransferGetGeneration(pTransfer);
        Progress.cbProcessed = cbProcessed;
        Progress.cbTotal     = cbTotal;
        int const rc2 = shClSvcExtNotifyTransferProgress(pClient, &Progress);
        if (RT_FAILURE(rc2))
            LogFlowFunc(("Reporting transfer progress failed with %Rrc\n", rc2));
    }
}


/**
 * Aborts a transfer from a host request and tears it down locally.
 *
 * Native unregistration and destruction can wait for active data consumers,
 * so they are performed only after all service, client and transfer locks have
 * been released.  Host cancellation itself is dispatched by Main on its
 * existing worker pool and therefore does not block the GUI event thread.
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

    int rcState;
    if (enmStatus == SHCLTRANSFERSTATUS_CANCELED)
        rcState = ShClTransferCancel(pTransfer);
    else
        rcState = ShClTransferError(pTransfer, rcTransfer);
    if (   RT_SUCCESS(rcState)
        && ShClTransferGetStatus(pTransfer) != enmStatus)
        rcState = VERR_WRONG_ORDER;

    SHCLSVCEXTTRANSFERSTATUS Status;
    if (RT_SUCCESS(rcState))
        shClSvcTransferStatusCapture(&Status, pTransfer, SHCLSOURCE_LOCAL, enmStatus, rcTransfer);

    int rcStatus = VINF_SUCCESS;
    if (RT_SUCCESS(rcState))
    {
        ShClSvcClientLock(pClient);
        rcStatus = shClSvcTransferSendStatusAsync(pClient, pTransfer, enmStatus, rcTransfer, NULL /* ppEvent */);
        ShClSvcClientUnlock(pClient);
    }

    if (   RT_SUCCESS(rcState)
        && shClSvcExtIsRegistered())
    {
        int const rc2 = shClSvcExtNotifyTransferStatus(pClient, &Status);
        if (RT_FAILURE(rc2))
            LogFlowFunc(("Reporting host transfer abort to Main failed with %Rrc\n", rc2));
    }

    /* Detach the exact object only after Main has synchronously consumed the
     * pointer-free terminal snapshot. */
    PSHCLTRANSFER pTransferDetached = NULL;
    if (RT_SUCCESS(rcState))
        pTransferDetached = shClSvcTransferDetach(pClient, pTransfer);

    /* A consuming destroy must not wait for this lookup retain. */
    ShClTransferRelease(pTransfer);

    if (pTransferDetached)
        shClSvcTransferDestroyDetached(pTransferDetached);

    if (RT_FAILURE(rcState))
        return rcState;

    /* The terminal state is committed.  Guest notification is best-effort and
     * must not make the synchronous host cancellation appear to have failed. */
    if (RT_FAILURE(rcStatus))
        LogFlowFunc(("Queueing transfer abort for the guest failed with %Rrc\n", rcStatus));
    return VINF_SUCCESS;
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
 * Creates an event payload containing a private copy of an object data chunk.
 *
 * The chunk descriptor and its data are kept in one allocation so that the
 * generic event payload destructor owns and releases the complete chunk.
 *
 * @returns VBox status code.
 * @param   idEvent             Event ID to assign to the payload.
 * @param   pDataChunk          Object data chunk to copy.
 * @param   ppPayload           Where to return the allocated event payload.
 */
static int shClSvcTransferObjDataChunkPayloadCreate(SHCLEVENTID idEvent, PSHCLOBJDATACHUNK pDataChunk,
                                                     PSHCLEVENTPAYLOAD *ppPayload)
{
    AssertPtrReturn(pDataChunk, VERR_INVALID_POINTER);
    AssertPtrReturn(ppPayload, VERR_INVALID_POINTER);
#if ARCH_BITS == 32
    AssertReturn(pDataChunk->cbData <= SIZE_MAX - sizeof(*pDataChunk), VERR_OUT_OF_RANGE);
#endif
    AssertReturn(pDataChunk->cbData == 0 || pDataChunk->pvData != NULL, VERR_INVALID_POINTER);

    size_t const cbPayloadData = sizeof(*pDataChunk) + pDataChunk->cbData;
    PSHCLOBJDATACHUNK pDataChunkCopy = (PSHCLOBJDATACHUNK)RTMemAlloc(cbPayloadData);
    if (!pDataChunkCopy)
        return VERR_NO_MEMORY;

    pDataChunkCopy->uHandle = pDataChunk->uHandle;
    pDataChunkCopy->cbData  = pDataChunk->cbData;
    pDataChunkCopy->pvData  = pDataChunk->cbData ? pDataChunkCopy + 1 : NULL;
    if (pDataChunk->cbData)
        memcpy(pDataChunkCopy->pvData, pDataChunk->pvData, pDataChunk->cbData);

    int const rc = ShClPayloadCreate(idEvent, pDataChunkCopy, (uint32_t)sizeof(*pDataChunkCopy), ppPayload);
    if (RT_FAILURE(rc))
        RTMemFree(pDataChunkCopy);
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
 * @param   pStatus             Where to return a status snapshot for subsequent
 *                              Main delivery, or NONE when there is none.
 */
static int shClSvcTransferMsgHandleReply(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer, uint32_t cParms,
                                         VBOXHGCMSVCPARM aParms[], bool fZeroContext, bool *pfDestroyTransfer,
                                         PSHCLSVCEXTTRANSFERSTATUS pStatus)
{
    AssertPtrReturn(pfDestroyTransfer, VERR_INVALID_POINTER);
    AssertPtrReturn(pStatus, VERR_INVALID_POINTER);
    *pfDestroyTransfer = false;
    RT_ZERO(*pStatus);

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
                        bool fCaptureTransferStatus = true;

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
                                            rc = ShClSvcTransferCreate(pClient, SHCLTRANSFERDIR_HOST_TO_GUEST, SHCLSOURCE_LOCAL,
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
                                    case SHCLTRANSFERDIR_GUEST_TO_HOST:
                                        /* Already done locally when creating the transfer. */
                                        break;

                                    case SHCLTRANSFERDIR_HOST_TO_GUEST:
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
                                if (ShClTransferGetDir(pTransfer) == SHCLTRANSFERDIR_HOST_TO_GUEST)
                                {
                                    /* Start the transfer on the host side. */
                                    rc = ShClSvcTransferStart(pClient, pTransfer);
                                }
                                break;
                            }

                            case SHCLTRANSFERSTATUS_CANCELED:
                            case SHCLTRANSFERSTATUS_KILLED:
                            case SHCLTRANSFERSTATUS_COMPLETED:
                            case SHCLTRANSFERSTATUS_ERROR:
                            {
                                bool fAccepted = false;
                                rc = shClSvcTransferApplyGuestTerminalStatus(pTransfer,
                                                                             pReply->u.TransferStatus.uStatus,
                                                                             pReply->rc, &fAccepted);
                                fCaptureTransferStatus = fAccepted;
                                if (fAccepted)
                                {
                                    if (pReply->u.TransferStatus.uStatus == SHCLTRANSFERSTATUS_ERROR)
                                    {
                                        LogRelMax(16, ("Shared Clipboard: Guest reported error %Rrc for transfer %RU16\n",
                                                       pReply->rc, pTransfer->State.uID));

                                        if (shClSvcExtIsRegistered())
                                        {
                                            char *pszMsg = RTStrAPrintf2("Guest reported error %Rrc for transfer %RU16", /** @todo Make the error messages more fine-grained based on rc. */
                                                                         pReply->rc, pTransfer->State.uID);
                                            AssertPtrBreakStmt(pszMsg, rc = VERR_NO_MEMORY);

                                            shClSvcExtReportError(NULL, pszMsg, pReply->rc);

                                            RTStrFree(pszMsg);
                                        }
                                    }
                                    else
                                        LogRel2(("Shared Clipboard: Guest has %s transfer %RU16\n",
                                                 ShClTransferStatusToStr(pReply->u.TransferStatus.uStatus), idTransfer));
                                }
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

                        if (   pTransfer
                            && fCaptureTransferStatus
                            && pReply->u.TransferStatus.uStatus != SHCLTRANSFERSTATUS_NONE
                            && ShClTransferStatusResultIsValid(pReply->u.TransferStatus.uStatus, (int)pReply->rc))
                            shClSvcTransferStatusCapture(pStatus, pTransfer, SHCLSOURCE_REMOTE,
                                                         pReply->u.TransferStatus.uStatus, (int)pReply->rc);
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
                            PSHCLEVENT pEvent
                                = ShClEventSourceRetainFromId(&pTransfer->Events, VBOX_SHCL_CONTEXTID_GET_EVENT(uCID));
                            if (pEvent)
                            {
                                LogFlowFunc(("uCID=%RU64 -> idEvent=%RU32, rcReply=%Rrc\n", uCID, pEvent->idEvent, pReply->rc));

                                rc = ShClEventSignalEx(pEvent, pReply->rc, pPayload);
                                if (RT_SUCCESS(rc))
                                {
                                    pPayload = NULL; /* The event owns the payload now. */
                                    pReply   = NULL; /* The payload owns the reply now. */
                                }

                                ShClEventRelease(pEvent);
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
    SHCLSVCEXTTRANSFERSTATUS Status;
    RT_ZERO(Status);

    switch (u32Function)
    {
        case VBOX_SHCL_GUEST_FN_REPLY:
        {
            rc = shClSvcTransferMsgHandleReply(pClient, pTransfer, cParms, aParms, fZeroContext,
                                                &fDestroyTransfer, &Status);
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

                PSHCLEVENT pEvent
                    = ShClEventSourceRetainFromId(&pTransfer->Events, VBOX_SHCL_CONTEXTID_GET_EVENT(uCID));
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

                    ShClEventRelease(pEvent);
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

                PSHCLEVENT pEvent
                    = ShClEventSourceRetainFromId(&pTransfer->Events, VBOX_SHCL_CONTEXTID_GET_EVENT(uCID));
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

                    ShClEventRelease(pEvent);
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

                    PSHCLEVENT pEvent
                        = ShClEventSourceRetainFromId(&pTransfer->Events, VBOX_SHCL_CONTEXTID_GET_EVENT(uCID));
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

                        ShClEventRelease(pEvent);
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

                    PSHCLEVENT pEvent
                        = ShClEventSourceRetainFromId(&pTransfer->Events, VBOX_SHCL_CONTEXTID_GET_EVENT(uCID));
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

                        ShClEventRelease(pEvent);
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

            /* Windows Guest Additions through 7.2.16 forward a complete
             * IStream::Read as one OBJ_READ.  Permit it only within the HGCM
             * limit and split it into provider-sized chunks. */
            if (   RT_SUCCESS(rc)
                && (   !pvBuf
                    || !cbBuf
                    || !cbToRead
                    ||  cbBuf < cbToRead
                    ||  cbToRead > VBOX_SHCL_MAX_CHUNK_SIZE
                   )
               )
            {
                rc = VERR_INVALID_PARAMETER;
            }

            if (RT_SUCCESS(rc))
            {
                uint32_t cbRead = 0;
                while (cbRead < cbToRead)
                {
                    uint32_t const cbToReadChunk = RT_MIN(cbToRead - cbRead, pTransfer->cbMaxChunkSize);
                    uint32_t       cbReadChunk   = 0;
                    rc = ShClTransferObjRead(pTransfer, hObj, (uint8_t *)pvBuf + cbRead, cbToReadChunk,
                                             0 /* fFlags */, &cbReadChunk);
                    if (RT_FAILURE(rc))
                        break;

                    AssertBreakStmt(cbReadChunk <= cbToReadChunk, rc = VERR_TOO_MUCH_DATA);

                    cbRead += cbReadChunk;
                    if (cbReadChunk < cbToReadChunk)
                        break;
                }

                if (RT_SUCCESS(rc))
                {
                    HGCMSvcSetU32(&aParms[2], cbRead);
                    ShClSvcTransferReportProgress(pClient, pTransfer, hObj, cbRead);

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
                PSHCLEVENT pEvent
                    = ShClEventSourceRetainFromId(&pTransfer->Events, VBOX_SHCL_CONTEXTID_GET_EVENT(uCID));
                if (pEvent)
                {
                    PSHCLEVENTPAYLOAD pPayload;
                    rc = shClSvcTransferObjDataChunkPayloadCreate(pEvent->idEvent, &dataChunk, &pPayload);
                    if (RT_SUCCESS(rc))
                    {
                        rc = ShClEventSignal(pEvent, pPayload);
                        if (RT_FAILURE(rc))
                            ShClPayloadDestroy(pPayload);
                    }

                    ShClEventRelease(pEvent);
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

    /* Cancellation may signal and retire a transfer event before the guest's
     * in-flight producer reaches us.  The terminal status already won in that
     * case, so a late reply must not synthesize a second ERROR transition. */
    if (   pTransfer
        && (   rc == VERR_WRONG_ORDER
            || rc == VERR_SHCLPB_EVENT_ID_NOT_FOUND)
        && ShClTransferStatusIsTerminal(ShClTransferGetStatus(pTransfer)))
        rc = VINF_SUCCESS;

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

        /* Replace a previously captured non-terminal reply with the terminal
         * failure which actually ends this transfer. */
        shClSvcTransferStatusCapture(&Status, pTransfer, SHCLSOURCE_LOCAL,
                                     SHCLTRANSFERSTATUS_ERROR, rc);
        fDestroyTransfer = true;
    }

    if (pTransfer)
    {
        /* A consuming destroy cannot wait while this handler retains the transfer. */
        ShClTransferRelease(pTransfer);
        pTransfer = NULL;
    }
    if (   Status.enmStatus != SHCLTRANSFERSTATUS_NONE
        && shClSvcExtIsRegistered())
    {
        int const rc2 = shClSvcExtNotifyTransferStatus(pClient, &Status);
        if (RT_FAILURE(rc2))
            LogFlowFunc(("Reporting guest transfer status to Main failed with %Rrc\n", rc2));
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
 * Reports a host-side terminal transfer status to the guest and Main.
 *
 * The transfer has already entered the terminal state.  This function only
 * publishes that state and deliberately leaves transfer destruction to the
 * normal lifecycle owner.
 *
 * @returns VBox status code from queuing the guest status.
 * @param   pClient            Service client owning the transfer.
 * @param   pTransfer          Transfer whose terminal state is being reported.
 * @param   enmStatus          Terminal transfer status.
 * @param   rcStatus           Status-specific result code.
 */
int ShClSvcTransferReportStatus(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer,
                                SHCLTRANSFERSTATUS enmStatus, int rcStatus)
{
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertReturn(ShClTransferStatusIsTerminal(enmStatus), VERR_INVALID_PARAMETER);
    AssertReturn(ShClTransferStatusResultIsValid(enmStatus, rcStatus), VERR_INVALID_PARAMETER);
    AssertReturn(ShClTransferGetStatus(pTransfer) == enmStatus, VERR_WRONG_ORDER);

    PSHCLTRANSFER const pRegisteredTransfer
        = ShClTransferCtxGetTransferByKeyRetained(&pClient->Transfers.Ctx,
                                                   ShClTransferGetSessionId(pTransfer),
                                                   ShClTransferGetID(pTransfer),
                                                   ShClTransferGetGeneration(pTransfer));
    if (pRegisteredTransfer != pTransfer)
    {
        if (pRegisteredTransfer)
            ShClTransferRelease(pRegisteredTransfer);
        return VERR_INVALID_CONTEXT;
    }

    SHCLSVCEXTTRANSFERSTATUS Status;
    shClSvcTransferStatusCapture(&Status, pTransfer, SHCLSOURCE_LOCAL, enmStatus, rcStatus);

    /* Main owns the user-visible lifecycle record, so publish there before
     * making the terminal status visible to the guest.  Otherwise the guest
     * can acknowledge the terminal message on the HGCM service thread and
     * detach this transfer before the native backend thread reaches Main. */
    if (shClSvcExtIsRegistered())
    {
        int const rc2 = shClSvcExtNotifyTransferStatus(pClient, &Status);
        if (RT_FAILURE(rc2))
            LogFlowFunc(("Reporting host transfer status to Main failed with %Rrc\n", rc2));
    }

    ShClSvcClientLock(pClient);
    int const rc = shClSvcTransferSendStatusAsync(pClient, pTransfer, enmStatus, rcStatus, NULL /* ppEvent */);
    ShClSvcClientUnlock(pClient);

    ShClTransferRelease(pRegisteredTransfer);
    return rc;
}


/**
 * Reports a terminal transfer status to Main after the native transfer was detached.
 *
 * The caller owns the detached transfer until this function returns.  The
 * immutable generation key prevents a delayed detached status from matching a
 * newer Main record which reuses the transfer ID.
 *
 * @returns VBox status code from reporting the status to Main.
 * @param   pClient            Service client which owned the transfer.
 * @param   pTransfer          Detached transfer whose metadata remains valid.
 * @param   enmStatus          Terminal transfer status.
 * @param   rcStatus           Status-specific result code.
 */
int ShClSvcTransferReportDetachedStatus(PSHCLCLIENT pClient, PSHCLTRANSFER pTransfer,
                                        SHCLTRANSFERSTATUS enmStatus, int rcStatus)
{
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);
    AssertPtrReturn(pTransfer, VERR_INVALID_POINTER);
    AssertReturn(ShClTransferStatusIsTerminal(enmStatus), VERR_INVALID_PARAMETER);
    AssertReturn(ShClTransferStatusResultIsValid(enmStatus, rcStatus), VERR_INVALID_PARAMETER);

    if (!shClSvcExtIsRegistered())
        return VINF_SUCCESS;

    SHCLSVCEXTTRANSFERSTATUS Status;
    shClSvcTransferStatusCapture(&Status, pTransfer, SHCLSOURCE_LOCAL, enmStatus, rcStatus);
    return shClSvcExtNotifyTransferDetachedStatus(pClient, &Status);
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
 * Detaches all native transfers while the client pointer is stable.
 *
 * Potentially blocking native unregistration remains deferred to
 * shClSvcTransferDestroyDetachedAll() after the caller releases the service lock.
 *
 * @param   pClient             Stable active client to reset.
 * @param   pList               Destination list for detached transfers.
 *
 * @note    The service lock must be held by the caller.
 * @note    The caller must notify Main of the reset after releasing the service lock.
 */
void shClSvcTransferResetAllLocked(PSHCLCLIENT pClient, PRTLISTANCHOR pList)
{
    Assert(RTCritSectIsOwner(&g_ShClSvc.CritSect));
    AssertPtrReturnVoid(pClient);
    AssertPtrReturnVoid(pList);

    shClSvcTransferDetachAll(pClient, pList);
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
    bool fNotifyReset = false;

    /* If file transfers are being disabled, detach all pending transfers from
     * the active client while its weak pointer is stable. */
    if (!(fMode & VBOX_SHCL_TRANSFER_MODE_F_ENABLED))
    {
        if (pClient)
        {
            shClSvcTransferResetAllLocked(pClient, &ListDestroy);
            fNotifyReset = true;
        }
    }

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    shClSvcUnlock();

    if (   fNotifyReset
        && shClSvcExtIsRegistered())
    {
        int const rc2 = shClSvcExtNotifyTransferReset(pClient);
        if (RT_FAILURE(rc2))
            LogFlowFunc(("Reporting the transfer reset to Main failed with %Rrc\n", rc2));
    }
    shClSvcTransferDestroyDetachedAll(&ListDestroy);

    return VINF_SUCCESS;
}
