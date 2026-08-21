/* $Id: VBoxSharedClipboardSvc-client.cpp 115102 2026-08-21 11:14:19Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Service - Client/session and message queue handling.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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
#include <VBox/vmm/vmmr3vtable.h> /* must be included before hgcmsvc.h */

#include <VBox/AssertGuest.h>
#include <VBox/err.h>
#include <VBox/HostServices/Service.h>
#include <VBox/GuestHost/clipboard-helper.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/VMMDev.h>

#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include "VBoxSharedClipboardSvc-internal.h"
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include "VBoxSharedClipboardSvc-transfers.h"
#endif

using namespace HGCM;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int  shClSvcClientStateInit(PSHCLCLIENTSTATE pState, uint32_t uClientID, SHCLSESSIONID idSession);
static int  shClSvcClientStateTerm(PSHCLCLIENTSTATE pState);
static void shClSvcClientStateReset(PSHCLCLIENTSTATE pState);


/**
 * Allocates the next non-zero service session ID.
 *
 * @returns Session ID.
 */
static SHCLSESSIONID shClSvcClientAllocSessionId(void)
{
    shClSvcLock();

    if (   g_ShClSvc.idNextSession == 0
        || g_ShClSvc.idNextSession == NIL_SHCLSESSIONID)
        g_ShClSvc.idNextSession = 1;

    SHCLSESSIONID const idSession = g_ShClSvc.idNextSession++;
    if (   g_ShClSvc.idNextSession == 0
        || g_ShClSvc.idNextSession == NIL_SHCLSESSIONID)
        g_ShClSvc.idNextSession = 1;

    shClSvcUnlock();

    return idSession;
}

/**
 * Handles clipboard formats.
 *
 * This suppresses file-transfer announcements until transfers are enabled and
 * supported by the guest, and keeps host-to-guest transfer offers separate
 * from ordinary clipboard formats.  Older Windows Guest Additions with
 * transfer support
 * (for example 7.2.6 and 7.2.10) expect URI-list offers to be reported on
 * their own so they can replace the normal clipboard announcement with an OLE
 * IDataObject.
 *
 * @returns The new Shared Clipboard formats.
 * @param   fHostToGuest        Reporting direction.
 *                              \c true from host -> guest.
 *                              \c false from guest -> host.
 * @param   pClient             Pointer to client instance.
 * @param   fFormats            Reported clipboard formats.
 */
SHCLFORMATS shClSvcHandleFormats(bool fHostToGuest, PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
    SHCLFORMATS const fFormatsOrg = fFormats;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (fFormats & VBOX_SHCL_FMT_URI_LIST)
    {
        if (!shClSvcClientTransfersAreAllowed(pClient))
        {
            uint32_t const fTransferMode  = ShClSvcClientGetTransferMode(pClient);
            uint64_t const fGuestFeatures = ShClSvcClientGetGuestFeatures0(pClient);
            uint64_t const fRequired      = VBOX_SHCL_GF_0_CONTEXT_ID | VBOX_SHCL_GF_0_TRANSFERS;
            LogRelMax(16, ("Shared Clipboard: File transfer format %#x was reported by %s without enabled and negotiated transfers (mode=%#x, features0=%#RX64, required=%#RX64), masking it\n",
                           VBOX_SHCL_FMT_URI_LIST, fHostToGuest ? "host" : "guest", fTransferMode,
                           fGuestFeatures, fRequired));
            fFormats &= ~VBOX_SHCL_FMT_URI_LIST;
        }
        else if (fHostToGuest)
        {
            if (fFormats != VBOX_SHCL_FMT_URI_LIST)
                LogRelMax2(16, ("Shared Clipboard: Host reported file transfer together with regular formats %#x; announcing URI-list alone for Guest Additions compatibility\n",
                               fFormats & ~VBOX_SHCL_FMT_URI_LIST));
            fFormats = VBOX_SHCL_FMT_URI_LIST;
        }
    }
#else
    RT_NOREF(pClient, fHostToGuest);
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

    if (LogRelIs2Enabled())
    {
        char *pszFmts = ShClFormatsToStrA(fFormats);
        LogRel2(("Shared Clipboard: %s reported formats %#x/'%s' to %s\n",
                 fHostToGuest ? "Host" : "Guest",
                 fFormats, pszFmts ? pszFmts : "<alloc error>",
                 fHostToGuest ? "guest" : "host"));
        RTStrFree(pszFmts);
    }

    if (fFormats != fFormatsOrg)
        LogRelMax2(16, ("Shared Clipboard: Adjusted %s clipboard formats from %#x to %#x before reporting to %s\n",
                       fHostToGuest ? "host" : "guest", fFormatsOrg, fFormats, fHostToGuest ? "guest" : "host"));

    return fFormats;
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Checks whether file transfers are enabled and supported by a client.
 *
 * Clipboard direction is deliberately not considered here and must be checked
 * separately by the operation being authorized.
 *
 * @returns true if file transfers may be used, false otherwise.
 * @param   pClient             Client to check.
 */
bool shClSvcClientTransfersAreAllowed(PSHCLCLIENT pClient)
{
    AssertPtrReturn(pClient, false);

    uint64_t const fRequired = VBOX_SHCL_GF_0_CONTEXT_ID | VBOX_SHCL_GF_0_TRANSFERS;
    return    !ASMAtomicReadBool(&pClient->Transfers.fResetting)
           && (ShClSvcClientGetTransferMode(pClient) & VBOX_SHCL_TRANSFER_MODE_F_ENABLED)
           && (ShClSvcClientGetGuestFeatures0(pClient) & fRequired) == fRequired;
}
#endif

/**
 * Acquires a Shared Clipboard client's critical section.
 *
 * @param   pClient             Client to lock.
 *
 * Lock acquisition is an internal client-lifetime invariant.  Failures are
 * reported by a debug assertion rather than propagated to callers.
 */
void ShClSvcClientLock(PSHCLCLIENT pClient)
{
    int rc2 = RTCritSectEnter(&pClient->CritSect);
    AssertRC(rc2);
}

/**
 * Releases a Shared Clipboard client's critical section.
 *
 * @param   pClient             Client to unlock.
 *
 * Lock release is an internal client-lifetime invariant.  Failures are
 * reported by a debug assertion rather than propagated to callers.
 */
void ShClSvcClientUnlock(PSHCLCLIENT pClient)
{
    int rc2 = RTCritSectLeave(&pClient->CritSect);
    AssertRC(rc2);
}

/**
 * Allocates a new clipboard message.
 *
 * @returns Allocated clipboard message, or NULL on failure.
 * @param   pClient     The client which is target of this message.
 * @param   idMsg       The message ID (VBOX_SHCL_HOST_MSG_XXX) to use
 * @param   cParms      The number of parameters the message takes.
 */
PSHCLCLIENTMSG ShClSvcClientMsgAlloc(PSHCLCLIENT pClient, uint32_t idMsg, uint32_t cParms)
{
    RT_NOREF(pClient);
    PSHCLCLIENTMSG pMsg = (PSHCLCLIENTMSG)RTMemAllocZ(RT_UOFFSETOF_DYN(SHCLCLIENTMSG, aParms[cParms]));
    if (pMsg)
    {
        uint32_t cAllocated = ASMAtomicIncU32(&pClient->cMsgAllocated);
        if (cAllocated <= 4096)
        {
            RTListInit(&pMsg->ListEntry);
            pMsg->cParms = cParms;
            pMsg->idMsg  = idMsg;
            return pMsg;
        }
        AssertMsgFailed(("Too many messages allocated for client %u! (%u)\n", pClient->State.uClientID, cAllocated));
        ASMAtomicDecU32(&pClient->cMsgAllocated);
        RTMemFree(pMsg);
    }
    return NULL;
}

/**
 * Frees a formerly allocated client clipboard message.
 *
 * @param   pClient     The client which was the target of this message.
 * @param   pMsg        Clipboard message to free.
 */
void ShClSvcClientMsgFree(PSHCLCLIENT pClient, PSHCLCLIENTMSG pMsg)
{
    RT_NOREF(pClient);
    /** @todo r=bird: Do accounting. */
    if (pMsg)
    {
        pMsg->idMsg = UINT32_C(0xdeadface);
        RTMemFree(pMsg);

        uint32_t cAllocated = ASMAtomicDecU32(&pClient->cMsgAllocated);
        Assert(cAllocated < UINT32_MAX / 2);
        RT_NOREF(cAllocated);
    }
}

/**
 * Sets the VBOX_SHCL_GUEST_FN_MSG_PEEK_WAIT and VBOX_SHCL_GUEST_FN_MSG_PEEK_NOWAIT
 * return parameters.
 *
 * @param   pMsg        Message to set return parameters to.
 * @param   paDstParms  The peek parameter vector.
 * @param   cDstParms   The number of peek parameters (at least two).
 * @remarks ASSUMES the parameters has been cleared by clientMsgPeek.
 */
void shClSvcMsgSetPeekReturn(PSHCLCLIENTMSG pMsg, PVBOXHGCMSVCPARM paDstParms, uint32_t cDstParms)
{
    Assert(cDstParms >= 2);
    if (paDstParms[0].type == VBOX_HGCM_SVC_PARM_32BIT)
        paDstParms[0].u.uint32 = pMsg->idMsg;
    else
        paDstParms[0].u.uint64 = pMsg->idMsg;
    paDstParms[1].u.uint32 = pMsg->cParms;

    uint32_t i = RT_MIN(cDstParms, pMsg->cParms + 2);
    while (i-- > 2)
        switch (pMsg->aParms[i - 2].type)
        {
            case VBOX_HGCM_SVC_PARM_32BIT: paDstParms[i].u.uint32 = ~(uint32_t)sizeof(uint32_t); break;
            case VBOX_HGCM_SVC_PARM_64BIT: paDstParms[i].u.uint32 = ~(uint32_t)sizeof(uint64_t); break;
            case VBOX_HGCM_SVC_PARM_PTR:   paDstParms[i].u.uint32 = pMsg->aParms[i - 2].u.pointer.size; break;
        }
}

/**
 * Sets the VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT return parameters.
 *
 * @returns VBox status code.
 * @param   pMsg        The message which parameters to return to the guest.
 * @param   paDstParms  The peek parameter vector.
 * @param   cDstParms   The number of peek parameters should be exactly two
 */
int shClSvcMsgSetOldWaitReturn(PSHCLCLIENTMSG pMsg, PVBOXHGCMSVCPARM paDstParms, uint32_t cDstParms)
{
    /*
     * Assert sanity.
     */
    AssertPtr(pMsg);
    AssertPtrReturn(paDstParms, VERR_INVALID_POINTER);
    AssertReturn(cDstParms >= 2, VERR_INVALID_PARAMETER);

    Assert(pMsg->cParms == 2);
    Assert(pMsg->aParms[0].u.uint32 == pMsg->idMsg);
    switch (pMsg->idMsg)
    {
        case VBOX_SHCL_HOST_MSG_READ_DATA:
        case VBOX_SHCL_HOST_MSG_FORMATS_REPORT:
            break;
        default:
            AssertFailed();
    }

    /*
     * Set the parameters.
     */
    if (pMsg->cParms > 0)
        paDstParms[0] = pMsg->aParms[0];
    if (pMsg->cParms > 1)
        paDstParms[1] = pMsg->aParms[1];
    return VINF_SUCCESS;
}


/**
 * Wakes up a pending client (i.e. waiting for new messages).
 *
 * @returns VBox status code.
 * @retval  VINF_NO_CHANGE if the client is not in pending mode.
 * @param   pClient             Client to wake up.
 *
 * @note    Caller must enter critical section.
 */
int ShClSvcClientWakeup(PSHCLCLIENT pClient)
{
    Assert(RTCritSectIsOwner(&pClient->CritSect));
    int rc = VINF_NO_CHANGE;

    if (pClient->Pending.uType != 0)
    {
        LogFunc(("[Client %RU32] Waking up ...\n", pClient->State.uClientID));

        PSHCLCLIENTMSG pFirstMsg = RTListGetFirst(&pClient->MsgQueue, SHCLCLIENTMSG, ListEntry);
        AssertReturn(pFirstMsg, VERR_INTERNAL_ERROR);

        LogFunc(("[Client %RU32] Current host message is %s (%RU32), cParms=%RU32\n",
                 pClient->State.uClientID, ShClSvcHostMsgToStr(pFirstMsg->idMsg), pFirstMsg->idMsg, pFirstMsg->cParms));

        if (pClient->Pending.uType == VBOX_SHCL_GUEST_FN_MSG_PEEK_WAIT)
            shClSvcMsgSetPeekReturn(pFirstMsg, pClient->Pending.paParms, pClient->Pending.cParms);
        else if (pClient->Pending.uType == VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT) /* Legacy, Guest Additions < 6.1. */
            shClSvcMsgSetOldWaitReturn(pFirstMsg, pClient->Pending.paParms, pClient->Pending.cParms);
        else
            AssertMsgFailedReturn(("pClient->Pending.uType=%u\n", pClient->Pending.uType), VERR_INTERNAL_ERROR_3);

        AssertPtrReturn(pClient->pHelpers, VERR_INVALID_POINTER);
        rc = pClient->pHelpers->pfnCallComplete(pClient->Pending.hHandle, VINF_SUCCESS);

        if (   rc != VERR_CANCELLED
            && pClient->Pending.uType == VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT)
        {
            RTListNodeRemove(&pFirstMsg->ListEntry);
            ShClSvcClientMsgFree(pClient, pFirstMsg);
        }

        pClient->Pending.hHandle = NULL;
        pClient->Pending.paParms = NULL;
        pClient->Pending.cParms  = 0;
        pClient->Pending.uType   = 0;
    }
    else
        LogFunc(("[Client %RU32] Not in pending state, skipping wakeup\n", pClient->State.uClientID));

    return rc;
}

/**
 * Appends a message to the client's queue and wake it up.
 *
 * @returns VBox status code, though the message is consumed regardless of what
 *          is returned.
 * @param   pClient             The client to queue the message on.
 * @param   pMsg                The message to queue.  Ownership is always
 *                              transfered to the queue.
 *
 * @note    Caller must enter critical section.
 */
int shClSvcClientMsgAddAndWakeupClient(PSHCLCLIENT pClient, PSHCLCLIENTMSG pMsg)
{
    Assert(RTCritSectIsOwner(&pClient->CritSect));
    AssertPtr(pMsg);
    AssertPtr(pClient);
    LogFlowFunc(("idMsg=%s (%u) cParms=%u\n", ShClSvcHostMsgToStr(pMsg->idMsg), pMsg->idMsg, pMsg->cParms));

    RTListAppend(&pClient->MsgQueue, &pMsg->ListEntry);
    int const rc = ShClSvcClientWakeup(pClient);
    if (RT_FAILURE(rc))
    {
        PSHCLCLIENTMSG pQueued;
        RTListForEach(&pClient->MsgQueue, pQueued, SHCLCLIENTMSG, ListEntry)
            if (pQueued == pMsg)
            {
                RTListNodeRemove(&pQueued->ListEntry);
                ShClSvcClientMsgFree(pClient, pQueued);
                break;
            }
    }
    return rc;
}

/**
 * Adds a new message to a client's message queue.
 *
 * @param   pClient             Pointer to the client data structure to add new message to.
 * @param   pMsg                Pointer to message to add. The queue then owns the pointer.
 * @param   fAppend             Whether to append or prepend the message to the queue.
 *
 * @note    Caller must enter critical section.
 */
void ShClSvcClientMsgAdd(PSHCLCLIENT pClient, PSHCLCLIENTMSG pMsg, bool fAppend)
{
    Assert(RTCritSectIsOwner(&pClient->CritSect));
    AssertPtr(pMsg);

    LogFlowFunc(("idMsg=%s (%RU32) cParms=%RU32 fAppend=%RTbool\n",
                 ShClSvcHostMsgToStr(pMsg->idMsg), pMsg->idMsg, pMsg->cParms, fAppend));

    if (fAppend)
        RTListAppend(&pClient->MsgQueue, &pMsg->ListEntry);
    else
        RTListPrepend(&pClient->MsgQueue, &pMsg->ListEntry);
}



/**
 * Resets a client's state message queue.
 *
 * @param   pClient             Pointer to the client data structure to reset message queue for.
 * @note    Caller enters pClient->CritSect.
 */
static void shClSvcClientMsgQueueReset(PSHCLCLIENT pClient)
{
    Assert(RTCritSectIsOwner(&pClient->CritSect));
    LogFlowFuncEnter();

    while (!RTListIsEmpty(&pClient->MsgQueue))
    {
        PSHCLCLIENTMSG pMsg = RTListRemoveFirst(&pClient->MsgQueue, SHCLCLIENTMSG, ListEntry);
        ShClSvcClientMsgFree(pClient, pMsg);
    }
    pClient->cMsgAllocated = 0;

    while (!RTListIsEmpty(&pClient->Legacy.lstCID))
    {
        PSHCLCLIENTLEGACYCID pCID = RTListRemoveFirst(&pClient->Legacy.lstCID, SHCLCLIENTLEGACYCID, Node);
        RTMemFree(pCID);
    }
    pClient->Legacy.cCID = 0;
}

/**
 * Initializes a Shared Clipboard client.
 *
 * @returns VBox status code.
 * @param   pClient             Client to initialize.
 * @param   uClientID           HGCM client ID to assign client to.
 */
int ShClSvcClientInit(PSHCLCLIENT pClient, uint32_t uClientID)
{
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);

    SHCLSESSIONID const idSession = shClSvcClientAllocSessionId();

    /* Assign the client ID. */
    pClient->State.uClientID = uClientID;
    pClient->pHelpers = g_ShClSvc.pHelpers;

    /* Cache the current Shared Clipboard mode in the client protocol state. */
    ASMAtomicWriteU32(&pClient->State.uMode, ShClSvcGetMode());

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /* Cache the current Shared Clipboard transfer (file) mode in the client protocol state. */
    ASMAtomicWriteU32(&pClient->State.Transfers.uTransferMode, shClSvcTransferModeGet());
    ASMAtomicWriteBool(&pClient->Transfers.fResetting, false);
#endif

    RTListInit(&pClient->MsgQueue);
    pClient->cMsgAllocated = 0;

    RTListInit(&pClient->Legacy.lstCID);
    pClient->Legacy.cCID = 0;

    LogFlowFunc(("[Client %RU32]\n", pClient->State.uClientID));

    bool fEventSourceInitialized = false;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    bool fTransferCtxInitialized = false;
#endif

    int rc = RTCritSectInit(&pClient->CritSect);
    if (RT_SUCCESS(rc))
    {
        /* Create the client's own event source. */
        rc = ShClEventSourceInit(&pClient->EventSrc, 0 /* ID, ignored */);
        if (RT_SUCCESS(rc))
        {
            fEventSourceInitialized = true;
            LogFlowFunc(("[Client %RU32] Using event source %RU32\n", uClientID, pClient->EventSrc.uID));

            /* Reset the client state. */
            shClSvcClientStateReset(&pClient->State);

            /* (Re-)initialize the client state. */
            rc = shClSvcClientStateInit(&pClient->State, uClientID, idSession);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            if (RT_SUCCESS(rc))
            {
                rc = ShClTransferCtxInit(&pClient->Transfers.Ctx);
                if (RT_SUCCESS(rc))
                {
                    fTransferCtxInitialized = true;
                    rc = ShClTransferCtxBeginSession(&pClient->Transfers.Ctx, pClient->State.uSessionID);
                }
            }
#endif
        }

        if (RT_FAILURE(rc))
        {
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            if (fTransferCtxInitialized)
                ShClTransferCtxDestroy(&pClient->Transfers.Ctx);
#endif
            if (fEventSourceInitialized)
                ShClEventSourceTerm(&pClient->EventSrc);
            shClSvcClientStateTerm(&pClient->State);

            int const rc2 = RTCritSectDelete(&pClient->CritSect);
            AssertRC(rc2);
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Destroys a Shared Clipboard client.
 *
 * @param   pClient             Client to destroy.
 */
void shClSvcClientDestroy(PSHCLCLIENT pClient)
{
    AssertPtrReturnVoid(pClient);

    LogFlowFunc(("[Client %RU32]\n", pClient->State.uClientID));

    shClSvcLock();
    if (g_ShClSvc.pActiveClient == pClient)
        g_ShClSvc.pActiveClient = NULL;
    shClSvcUnlock();

    /* Make sure to send a quit message to the guest so that it can terminate gracefully. */
    ShClSvcClientLock(pClient);

    if (pClient->Pending.uType)
    {
        if (pClient->Pending.cParms > 1)
            HGCMSvcSetU32(&pClient->Pending.paParms[0], VBOX_SHCL_HOST_MSG_QUIT);
        if (pClient->Pending.cParms > 2)
            HGCMSvcSetU32(&pClient->Pending.paParms[1], 0);
        g_ShClSvc.pHelpers->pfnCallComplete(pClient->Pending.hHandle, VINF_SUCCESS);
        pClient->Pending.uType   = 0;
        pClient->Pending.cParms  = 0;
        pClient->Pending.hHandle = NULL;
        pClient->Pending.paParms = NULL;
    }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    ASMAtomicWriteBool(&pClient->Transfers.fResetting, true);
    ShClSvcClientUnlock(pClient);
    shClSvcTransferDestroyAll(pClient);
    ShClSvcClientLock(pClient);
    ShClTransferCtxDestroy(&pClient->Transfers.Ctx);
#endif

    ShClEventSourceTerm(&pClient->EventSrc);
    shClSvcClientStateTerm(&pClient->State);

    ShClSvcClientUnlock(pClient);

    PSHCLCLIENTLEGACYCID pCidIter, pCidIterNext;
    RTListForEachSafe(&pClient->Legacy.lstCID, pCidIter, pCidIterNext, SHCLCLIENTLEGACYCID, Node)
        RTMemFree(pCidIter);

    int rc2 = RTCritSectDelete(&pClient->CritSect);
    AssertRC(rc2);

    LogFlowFuncLeave();
}

/**
 * Resets a Shared Clipboard client.
 *
 * @param   pClient             Client to reset.
 */
void shClSvcClientReset(PSHCLCLIENT pClient)
{
    if (!pClient)
        return;

    /* Stop native callbacks from admitting a new transfer while the old
     * service session is being detached and replaced. */
    ShClSvcClientLock(pClient);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    ASMAtomicWriteBool(&pClient->Transfers.fResetting, true);
#endif
    ShClSvcClientUnlock(pClient);

    /* Allocate outside the client lock to preserve the service -> client lock order. */
    SHCLSESSIONID const idSession = shClSvcClientAllocSessionId();

    LogFlowFunc(("[Client %RU32]\n", pClient->State.uClientID));
    ShClSvcClientLock(pClient);

    uint32_t const uClientID = pClient->State.uClientID;

    /* Reset message queue. */
    shClSvcClientMsgQueueReset(pClient);

    /* Reset event source. */
    ShClEventSourceReset(&pClient->EventSrc);

    /* Reset pending state. */
    RT_ZERO(pClient->Pending);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    ShClSvcClientUnlock(pClient);
    RTLISTANCHOR ListDestroy;
    RTListInit(&ListDestroy);
    shClSvcLock();
    shClSvcTransferResetAllLocked(pClient, &ListDestroy);
    shClSvcUnlock();

    int const rcReset = shClSvcExtNotifyTransferReset(pClient);
    if (   RT_FAILURE(rcReset)
        && rcReset != VERR_NOT_SUPPORTED)
        LogFlowFunc(("Resetting Main transfer state failed with %Rrc\n", rcReset));

    shClSvcTransferDestroyDetachedAll(&ListDestroy);
    ShClSvcClientLock(pClient);
#endif

    shClSvcClientStateReset(&pClient->State);
    int rc2 = shClSvcClientStateInit(&pClient->State, uClientID, idSession);
    AssertRC(rc2);
    ASMAtomicWriteU32(&pClient->State.uMode, ShClSvcGetMode());
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    ASMAtomicWriteU32(&pClient->State.Transfers.uTransferMode, shClSvcTransferModeGet());
    if (RT_SUCCESS(rc2))
    {
        rc2 = ShClTransferCtxBeginSession(&pClient->Transfers.Ctx, pClient->State.uSessionID);
        AssertRC(rc2);
        if (RT_SUCCESS(rc2))
            ASMAtomicWriteBool(&pClient->Transfers.fResetting, false);
    }
#endif

    ShClSvcClientUnlock(pClient);
}

DECLCALLBACK(void) shClSvcClientCall(void *,
                                     VBOXHGCMCALLHANDLE callHandle,
                                     uint32_t u32ClientID,
                                     void *pvClient,
                                     uint32_t u32Function,
                                     uint32_t cParms,
                                     VBOXHGCMSVCPARM paParms[],
                                     uint64_t tsArrival)
{
    RT_NOREF(u32ClientID, pvClient, tsArrival);
    PSHCLCLIENT pClient = (PSHCLCLIENT)pvClient;
    AssertPtr(pClient);

#ifdef LOG_ENABLED
    Log2Func(("u32ClientID=%RU32, fn=%RU32 (%s), cParms=%RU32, paParms=%p\n",
              u32ClientID, u32Function, ShClSvcGuestMsgToStr(u32Function), cParms, paParms));
    for (uint32_t i = 0; i < cParms; i++)
    {
        switch (paParms[i].type)
        {
            case VBOX_HGCM_SVC_PARM_32BIT:
                Log3Func(("    paParms[%RU32]: type uint32_t - value %RU32\n", i, paParms[i].u.uint32));
                break;
            case VBOX_HGCM_SVC_PARM_64BIT:
                Log3Func(("    paParms[%RU32]: type uint64_t - value %RU64\n", i, paParms[i].u.uint64));
                break;
            case VBOX_HGCM_SVC_PARM_PTR:
                Log3Func(("    paParms[%RU32]: type ptr - value 0x%p (%RU32 bytes)\n",
                          i, paParms[i].u.pointer.addr, paParms[i].u.pointer.size));
                break;
            case VBOX_HGCM_SVC_PARM_PAGES:
                Log3Func(("    paParms[%RU32]: type pages - cb=%RU32, cPages=%RU16\n",
                          i, paParms[i].u.Pages.cb, paParms[i].u.Pages.cPages));
                break;
            default:
                AssertFailed();
        }
    }
    Log2Func(("Client state: fFlags=0x%x, fGuestFeatures0=0x%x, fGuestFeatures1=0x%x\n",
              pClient->State.fFlags, ShClSvcClientGetGuestFeatures0(pClient), ShClSvcClientGetGuestFeatures1(pClient)));
#endif

    int rc;
    switch (u32Function)
    {
        case VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT:
            RTCritSectEnter(&pClient->CritSect);
            rc = shClSvcClientMsgOldGet(pClient, callHandle, cParms, paParms);
            RTCritSectLeave(&pClient->CritSect);
            break;

        case VBOX_SHCL_GUEST_FN_CONNECT:
            LogRel(("Shared Clipboard: 6.1.0 beta or rc Guest Additions detected. Please upgrade!\n"));
            rc = VERR_NOT_IMPLEMENTED;
            break;

        case VBOX_SHCL_GUEST_FN_NEGOTIATE_CHUNK_SIZE:
            rc = shClSvcClientNegotiateChunkSize(pClient, callHandle, cParms, paParms);
            break;

        case VBOX_SHCL_GUEST_FN_REPORT_FEATURES:
            rc = shClSvcClientReportFeatures(pClient, callHandle, cParms, paParms);
            break;

        case VBOX_SHCL_GUEST_FN_QUERY_FEATURES:
            rc = shClSvcClientMsgQueryFeatures(callHandle, cParms, paParms);
            break;

        case VBOX_SHCL_GUEST_FN_MSG_PEEK_NOWAIT:
            RTCritSectEnter(&pClient->CritSect);
            rc = shClSvcClientMsgPeek(pClient, callHandle, cParms, paParms, false /*fWait*/);
            RTCritSectLeave(&pClient->CritSect);
            break;

        case VBOX_SHCL_GUEST_FN_MSG_PEEK_WAIT:
            RTCritSectEnter(&pClient->CritSect);
            rc = shClSvcClientMsgPeek(pClient, callHandle, cParms, paParms, true /*fWait*/);
            RTCritSectLeave(&pClient->CritSect);
            break;

        case VBOX_SHCL_GUEST_FN_MSG_GET:
            RTCritSectEnter(&pClient->CritSect);
            rc = shClSvcClientMsgGet(pClient, callHandle, cParms, paParms);
            RTCritSectLeave(&pClient->CritSect);
            break;

        case VBOX_SHCL_GUEST_FN_MSG_CANCEL:
            RTCritSectEnter(&pClient->CritSect);
            rc = shClSvcClientMsgCancel(pClient, cParms);
            RTCritSectLeave(&pClient->CritSect);
            break;

        case VBOX_SHCL_GUEST_FN_REPORT_FORMATS:
            rc = shClSvcClientMsgReportFormats(pClient, cParms, paParms);
            break;

        case VBOX_SHCL_GUEST_FN_DATA_READ:
            rc = shClSvcClientMsgDataRead(pClient, cParms, paParms);
            break;

        case VBOX_SHCL_GUEST_FN_DATA_WRITE:
            rc = shClSvcClientMsgDataWrite(pClient, cParms, paParms);
            break;

        case VBOX_SHCL_GUEST_FN_ERROR:
        {
            int rcGuest;
            rc = shClSvcClientMsgError(pClient, cParms, paParms, &rcGuest);
            if (RT_SUCCESS(rc))
            {
                LogRel(("Shared Clipboard: Error reported from guest side: %Rrc\n", rcGuest));

                /* Start over. */
                shClSvcClientReset(pClient);
            }
            break;
        }

        default:
        {
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            rc = ShClSvcTransferMsgClientHandler(pClient, callHandle, u32Function, cParms, paParms, tsArrival);
#else
            LogRelMax(16, ("Shared Clipboard: Unknown guest function: %u (%#x)\n", u32Function, u32Function));
            rc = VERR_NOT_IMPLEMENTED;
#endif
            break;
        }
    }

    LogFlowFunc(("[Client %RU32] rc=%Rrc\n", pClient->State.uClientID, rc));

    if (rc != VINF_HGCM_ASYNC_EXECUTE)
        g_ShClSvc.pHelpers->pfnCallComplete(callHandle, rc);
}

int shClSvcClientNegotiateChunkSize(PSHCLCLIENT pClient, VBOXHGCMCALLHANDLE hCall,
                                    uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    /*
     * Validate the request.
     */
    ASSERT_GUEST_RETURN(cParms == VBOX_SHCL_CPARMS_NEGOTIATE_CHUNK_SIZE, VERR_WRONG_PARAMETER_COUNT);
    ASSERT_GUEST_RETURN(paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT, VERR_WRONG_PARAMETER_TYPE);
    uint32_t const cbClientMaxChunkSize = paParms[0].u.uint32;
    ASSERT_GUEST_RETURN(paParms[1].type == VBOX_HGCM_SVC_PARM_32BIT, VERR_WRONG_PARAMETER_TYPE);
    uint32_t const cbClientChunkSize    = paParms[1].u.uint32;

    uint32_t const cbHostMaxChunkSize = VBOX_SHCL_MAX_CHUNK_SIZE; /** @todo Make this configurable. */

    /*
     * Do the work.
     */
    if (cbClientChunkSize == 0) /* Does the client want us to choose? */
    {
        paParms[0].u.uint32 = cbHostMaxChunkSize;                                     /* Maximum */
        paParms[1].u.uint32 = RT_MIN(pClient->State.cbChunkSize, cbHostMaxChunkSize); /* Preferred */

    }
    else /* The client told us what it supports, so update and report back. */
    {
        paParms[0].u.uint32 = RT_MIN(cbClientMaxChunkSize, cbHostMaxChunkSize);         /* Maximum */
        paParms[1].u.uint32 = RT_MIN(cbClientMaxChunkSize, pClient->State.cbChunkSize); /* Preferred */
    }

    int rc = g_ShClSvc.pHelpers->pfnCallComplete(hCall, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        Log(("[Client %RU32] chunk size: %#RU32, max: %#RU32\n",
             pClient->State.uClientID, paParms[1].u.uint32, paParms[0].u.uint32));
    }
    else
        LogFunc(("pfnCallComplete -> %Rrc\n", rc));

    return VINF_HGCM_ASYNC_EXECUTE;
}

/**
 * Implements VBOX_SHCL_GUEST_FN_REPORT_FEATURES.
 *
 * @returns VBox status code.
 * @retval  VINF_HGCM_ASYNC_EXECUTE on success (we complete the message here).
 * @retval  VERR_ACCESS_DENIED if not master
 * @retval  VERR_INVALID_PARAMETER if bit 63 in the 2nd parameter isn't set.
 * @retval  VERR_WRONG_PARAMETER_COUNT
 *
 * @param   pClient     The client state.
 * @param   hCall       The client's call handle.
 * @param   cParms      Number of parameters.
 * @param   paParms     Array of parameters.
 */
int shClSvcClientReportFeatures(PSHCLCLIENT pClient, VBOXHGCMCALLHANDLE hCall,
                                uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    /*
     * Validate the request.
     */
    ASSERT_GUEST_RETURN(cParms == 2, VERR_WRONG_PARAMETER_COUNT);
    ASSERT_GUEST_RETURN(paParms[0].type == VBOX_HGCM_SVC_PARM_64BIT, VERR_WRONG_PARAMETER_TYPE);
    uint64_t const fFeatures0 = paParms[0].u.uint64;
    ASSERT_GUEST_RETURN(paParms[1].type == VBOX_HGCM_SVC_PARM_64BIT, VERR_WRONG_PARAMETER_TYPE);
    uint64_t const fFeatures1 = paParms[1].u.uint64;
    ASSERT_GUEST_RETURN(fFeatures1 & VBOX_SHCL_GF_1_MUST_BE_ONE, VERR_INVALID_PARAMETER);

    /*
     * Do the work.
     */
    paParms[0].u.uint64 = g_ShClSvc.fHostFeatures0;
    paParms[1].u.uint64 = 0;

    int rc = g_ShClSvc.pHelpers->pfnCallComplete(hCall, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        ShClSvcClientLock(pClient);
        ASMAtomicWriteU64(&pClient->State.fGuestFeatures0, fFeatures0);
        ASMAtomicWriteU64(&pClient->State.fGuestFeatures1, fFeatures1);
        ShClSvcClientUnlock(pClient);

        LogRel2(("Shared Clipboard: Guest reported the following features: %#RX64\n",
                 fFeatures0)); /* Note: fFeatures1 not used yet. */
        if (fFeatures0 & VBOX_SHCL_GF_0_TRANSFERS)
            LogRel2(("Shared Clipboard: Guest supports file transfers\n"));
    }
    else
        LogFunc(("pfnCallComplete -> %Rrc\n", rc));

    return VINF_HGCM_ASYNC_EXECUTE;
}

/**
 * Implements VBOX_SHCL_GUEST_FN_QUERY_FEATURES.
 *
 * @returns VBox status code.
 * @retval  VINF_HGCM_ASYNC_EXECUTE on success (we complete the message here).
 * @retval  VERR_WRONG_PARAMETER_COUNT
 *
 * @param   hCall       The client's call handle.
 * @param   cParms      Number of parameters.
 * @param   paParms     Array of parameters.
 */
int shClSvcClientMsgQueryFeatures(VBOXHGCMCALLHANDLE hCall, uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    /*
     * Validate the request.
     */
    ASSERT_GUEST_RETURN(cParms == 2, VERR_WRONG_PARAMETER_COUNT);
    ASSERT_GUEST_RETURN(paParms[0].type == VBOX_HGCM_SVC_PARM_64BIT, VERR_WRONG_PARAMETER_TYPE);
    ASSERT_GUEST_RETURN(paParms[1].type == VBOX_HGCM_SVC_PARM_64BIT, VERR_WRONG_PARAMETER_TYPE);
    ASSERT_GUEST(paParms[1].u.uint64 & RT_BIT_64(63));

    /*
     * Do the work.
     */
    paParms[0].u.uint64 = g_ShClSvc.fHostFeatures0;
    paParms[1].u.uint64 = 0;
    int rc = g_ShClSvc.pHelpers->pfnCallComplete(hCall, VINF_SUCCESS);
    if (RT_FAILURE(rc))
        LogFunc(("pfnCallComplete -> %Rrc\n", rc));

    return VINF_HGCM_ASYNC_EXECUTE;
}

/**
 * Implements VBOX_SHCL_GUEST_FN_MSG_PEEK_WAIT and VBOX_SHCL_GUEST_FN_MSG_PEEK_NOWAIT.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if a message was pending and is being returned.
 * @retval  VERR_TRY_AGAIN if no message pending and not blocking.
 * @retval  VERR_RESOURCE_BUSY if another read already made a waiting call.
 * @retval  VINF_HGCM_ASYNC_EXECUTE if message wait is pending.
 *
 * @param   pClient     The client state.
 * @param   hCall       The client's call handle.
 * @param   cParms      Number of parameters.
 * @param   paParms     Array of parameters.
 * @param   fWait       Set if we should wait for a message, clear if to return
 *                      immediately.
 *
 * @note    Caller takes and leave the client's critical section.
 */
int shClSvcClientMsgPeek(PSHCLCLIENT pClient, VBOXHGCMCALLHANDLE hCall, uint32_t cParms, VBOXHGCMSVCPARM paParms[], bool fWait)
{
    /*
     * Validate the request.
     */
    ASSERT_GUEST_MSG_RETURN(cParms >= 2, ("cParms=%u!\n", cParms), VERR_WRONG_PARAMETER_COUNT);

    uint64_t idRestoreCheck = 0;
    uint32_t i              = 0;
    if (paParms[i].type == VBOX_HGCM_SVC_PARM_64BIT)
    {
        idRestoreCheck = paParms[0].u.uint64;
        paParms[0].u.uint64 = 0;
        i++;
    }
    for (; i < cParms; i++)
    {
        ASSERT_GUEST_MSG_RETURN(paParms[i].type == VBOX_HGCM_SVC_PARM_32BIT, ("#%u type=%u\n", i, paParms[i].type),
                                VERR_WRONG_PARAMETER_TYPE);
        paParms[i].u.uint32 = 0;
    }

    /*
     * Check restore session ID.
     */
    if (idRestoreCheck != 0)
    {
        uint64_t idRestore = g_ShClSvc.pHelpers->pfnGetVMMDevSessionId(g_ShClSvc.pHelpers);
        if (idRestoreCheck != idRestore)
        {
            paParms[0].u.uint64 = idRestore;
            LogFlowFunc(("[Client %RU32] VBOX_SHCL_GUEST_FN_MSG_PEEK_XXX -> VERR_VM_RESTORED (%#RX64 -> %#RX64)\n",
                         pClient->State.uClientID, idRestoreCheck, idRestore));
            return VERR_VM_RESTORED;
        }
        Assert(!g_ShClSvc.pHelpers->pfnIsCallRestored(hCall));
    }

    /*
     * Return information about the first message if one is pending in the list.
     */
    PSHCLCLIENTMSG pFirstMsg = RTListGetFirst(&pClient->MsgQueue, SHCLCLIENTMSG, ListEntry);
    if (pFirstMsg)
    {
        shClSvcMsgSetPeekReturn(pFirstMsg, paParms, cParms);
        LogFlowFunc(("[Client %RU32] VBOX_SHCL_GUEST_FN_MSG_PEEK_XXX -> VINF_SUCCESS (idMsg=%s (%u), cParms=%u)\n",
                     pClient->State.uClientID, ShClSvcHostMsgToStr(pFirstMsg->idMsg), pFirstMsg->idMsg, pFirstMsg->cParms));
        return VINF_SUCCESS;
    }

    /*
     * If we cannot wait, fail the call.
     */
    if (!fWait)
    {
        LogFlowFunc(("[Client %RU32] GUEST_MSG_PEEK_NOWAIT -> VERR_TRY_AGAIN\n", pClient->State.uClientID));
        return VERR_TRY_AGAIN;
    }

    /*
     * Wait for the host to queue a message for this client.
     */
    ASSERT_GUEST_MSG_RETURN(pClient->Pending.uType == 0, ("Already pending! (idClient=%RU32)\n",
                                                           pClient->State.uClientID), VERR_RESOURCE_BUSY);
    pClient->Pending.hHandle = hCall;
    pClient->Pending.cParms  = cParms;
    pClient->Pending.paParms = paParms;
    pClient->Pending.uType   = VBOX_SHCL_GUEST_FN_MSG_PEEK_WAIT;
    LogFlowFunc(("[Client %RU32] Is now in pending mode...\n", pClient->State.uClientID));
    return VINF_HGCM_ASYNC_EXECUTE;
}

/**
 * Implements VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if a message was pending and is being returned.
 * @retval  VINF_HGCM_ASYNC_EXECUTE if message wait is pending.
 *
 * @param   pClient     The client state.
 * @param   hCall       The client's call handle.
 * @param   cParms      Number of parameters.
 * @param   paParms     Array of parameters.
 *
 * @note    Caller takes and leave the client's critical section.
 */
int shClSvcClientMsgOldGet(PSHCLCLIENT pClient, VBOXHGCMCALLHANDLE hCall, uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    /*
     * Validate input.
     */
    ASSERT_GUEST_RETURN(cParms == VBOX_SHCL_CPARMS_GET_HOST_MSG_OLD, VERR_WRONG_PARAMETER_COUNT);
    ASSERT_GUEST_RETURN(paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT, VERR_WRONG_PARAMETER_TYPE); /* id32Msg */
    ASSERT_GUEST_RETURN(paParms[1].type == VBOX_HGCM_SVC_PARM_32BIT, VERR_WRONG_PARAMETER_TYPE); /* f32Formats */

    paParms[0].u.uint32 = 0;
    paParms[1].u.uint32 = 0;

    /*
     * If there is a message pending we can return immediately.
     */
    int rc;
    PSHCLCLIENTMSG pFirstMsg = RTListGetFirst(&pClient->MsgQueue, SHCLCLIENTMSG, ListEntry);
    if (pFirstMsg)
    {
        LogFlowFunc(("[Client %RU32] uMsg=%s (%RU32), cParms=%RU32\n", pClient->State.uClientID,
                     ShClSvcHostMsgToStr(pFirstMsg->idMsg), pFirstMsg->idMsg, pFirstMsg->cParms));

        rc = shClSvcMsgSetOldWaitReturn(pFirstMsg, paParms, cParms);
        AssertPtr(g_ShClSvc.pHelpers);
        rc = g_ShClSvc.pHelpers->pfnCallComplete(hCall, rc);
        if (rc != VERR_CANCELLED)
        {
            RTListNodeRemove(&pFirstMsg->ListEntry);
            ShClSvcClientMsgFree(pClient, pFirstMsg);

            rc = VINF_HGCM_ASYNC_EXECUTE; /* The caller must not complete it. */
        }
    }
    /*
     * Otherwise we must wait.
     */
    else
    {
        ASSERT_GUEST_MSG_RETURN(pClient->Pending.uType == 0, ("Already pending! (idClient=%RU32)\n", pClient->State.uClientID),
                                VERR_RESOURCE_BUSY);

        pClient->Pending.hHandle = hCall;
        pClient->Pending.cParms  = cParms;
        pClient->Pending.paParms = paParms;
        pClient->Pending.uType   = VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT;

        rc = VINF_HGCM_ASYNC_EXECUTE; /* The caller must not complete it. */

        LogFlowFunc(("[Client %RU32] Is now in pending mode...\n", pClient->State.uClientID));
    }

    LogFlowFunc(("[Client %RU32] rc=%Rrc\n", pClient->State.uClientID, rc));
    return rc;
}

/**
 * Implements VBOX_SHCL_GUEST_FN_MSG_GET.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if message retrieved and removed from the pending queue.
 * @retval  VERR_TRY_AGAIN if no message pending.
 * @retval  VERR_BUFFER_OVERFLOW if a parmeter buffer is too small.  The buffer
 *          size was updated to reflect the required size, though this isn't yet
 *          forwarded to the guest.  (The guest is better of using peek with
 *          parameter count + 2 parameters to get the sizes.)
 * @retval  VERR_MISMATCH if the incoming message ID does not match the pending.
 * @retval  VINF_HGCM_ASYNC_EXECUTE if message was completed already.
 *
 * @param   pClient      The client state.
 * @param   hCall        The client's call handle.
 * @param   cParms       Number of parameters.
 * @param   paParms      Array of parameters.
 *
 * @note    Called from within pClient->CritSect.
 */
int shClSvcClientMsgGet(PSHCLCLIENT pClient, VBOXHGCMCALLHANDLE hCall, uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    /*
     * Validate the request.
     */
    ASSERT_GUEST_MSG_RETURN(cParms >= 2, ("cParms=%u!\n", cParms), VERR_WRONG_PARAMETER_COUNT);

    uint32_t const idMsgExpected = cParms > 0 && paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT ? paParms[0].u.uint32
                                 : cParms > 0 && paParms[0].type == VBOX_HGCM_SVC_PARM_64BIT ? paParms[0].u.uint64
                                 : UINT32_MAX;

    /*
     * Return information about the first message if one is pending in the list.
     */
    PSHCLCLIENTMSG pFirstMsg = RTListGetFirst(&pClient->MsgQueue, SHCLCLIENTMSG, ListEntry);
    if (pFirstMsg)
    {
        LogFlowFunc(("First message is: %s (%u), cParms=%RU32\n", ShClSvcHostMsgToStr(pFirstMsg->idMsg), pFirstMsg->idMsg, pFirstMsg->cParms));

        ASSERT_GUEST_MSG_RETURN(pFirstMsg->idMsg == idMsgExpected || idMsgExpected == UINT32_MAX,
                                ("idMsg=%u (%s) cParms=%u, caller expected %u (%s) and %u\n",
                                 pFirstMsg->idMsg, ShClSvcHostMsgToStr(pFirstMsg->idMsg), pFirstMsg->cParms,
                                 idMsgExpected, ShClSvcHostMsgToStr(idMsgExpected), cParms),
                                VERR_MISMATCH);
        ASSERT_GUEST_MSG_RETURN(pFirstMsg->cParms == cParms,
                                ("idMsg=%u (%s) cParms=%u, caller expected %u (%s) and %u\n",
                                 pFirstMsg->idMsg, ShClSvcHostMsgToStr(pFirstMsg->idMsg), pFirstMsg->cParms,
                                 idMsgExpected, ShClSvcHostMsgToStr(idMsgExpected), cParms),
                                VERR_WRONG_PARAMETER_COUNT);

        /* Check the parameter types. */
        for (uint32_t i = 0; i < cParms; i++)
            ASSERT_GUEST_MSG_RETURN(pFirstMsg->aParms[i].type == paParms[i].type,
                                    ("param #%u: type %u, caller expected %u (idMsg=%u %s)\n", i, pFirstMsg->aParms[i].type,
                                     paParms[i].type, pFirstMsg->idMsg, ShClSvcHostMsgToStr(pFirstMsg->idMsg)),
                                    VERR_WRONG_PARAMETER_TYPE);
        /*
         * Copy out the parameters.
         *
         * No assertions on buffer overflows, and keep going till the end so we can
         * communicate all the required buffer sizes.
         */
        int rc = VINF_SUCCESS;
        for (uint32_t i = 0; i < cParms; i++)
            switch (pFirstMsg->aParms[i].type)
            {
                case VBOX_HGCM_SVC_PARM_32BIT:
                    paParms[i].u.uint32 = pFirstMsg->aParms[i].u.uint32;
                    break;

                case VBOX_HGCM_SVC_PARM_64BIT:
                    paParms[i].u.uint64 = pFirstMsg->aParms[i].u.uint64;
                    break;

                case VBOX_HGCM_SVC_PARM_PTR:
                {
                    uint32_t const cbSrc = pFirstMsg->aParms[i].u.pointer.size;
                    uint32_t const cbDst = paParms[i].u.pointer.size;
                    paParms[i].u.pointer.size = cbSrc; /** @todo Check if this is safe in other layers...
                                                        * Update: Safe, yes, but VMMDevHGCM doesn't pass it along. */
                    if (cbSrc <= cbDst)
                        memcpy(paParms[i].u.pointer.addr, pFirstMsg->aParms[i].u.pointer.addr, cbSrc);
                    else
                    {
                        AssertMsgFailed(("#%u: cbSrc=%RU32 is bigger than cbDst=%RU32\n", i, cbSrc, cbDst));
                        rc = VERR_BUFFER_OVERFLOW;
                    }
                    break;
                }

                default:
                    AssertMsgFailed(("#%u: %u\n", i, pFirstMsg->aParms[i].type));
                    rc = VERR_INTERNAL_ERROR;
                    break;
            }
        if (RT_SUCCESS(rc))
        {
            /*
             * Complete the message and remove the pending message unless the
             * guest raced us and cancelled this call in the meantime.
             */
            AssertPtr(g_ShClSvc.pHelpers);
            rc = g_ShClSvc.pHelpers->pfnCallComplete(hCall, rc);

            LogFlowFunc(("[Client %RU32] pfnCallComplete -> %Rrc\n", pClient->State.uClientID, rc));

            if (rc != VERR_CANCELLED)
            {
                RTListNodeRemove(&pFirstMsg->ListEntry);
                ShClSvcClientMsgFree(pClient, pFirstMsg);
            }

            return VINF_HGCM_ASYNC_EXECUTE; /* The caller must not complete it. */
        }

        LogFlowFunc(("[Client %RU32] Returning %Rrc\n", pClient->State.uClientID, rc));
        return rc;
    }

    paParms[0].u.uint32 = 0;
    paParms[1].u.uint32 = 0;
    LogFlowFunc(("[Client %RU32] -> VERR_TRY_AGAIN\n", pClient->State.uClientID));
    return VERR_TRY_AGAIN;
}

/**
 * Implements VBOX_SHCL_GUEST_FN_MSG_GET.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if message retrieved and removed from the pending queue.
 * @retval  VERR_TRY_AGAIN if no message pending.
 * @retval  VERR_MISMATCH if the incoming message ID does not match the pending.
 * @retval  VINF_HGCM_ASYNC_EXECUTE if message was completed already.
 *
 * @param   pClient      The client state.
 * @param   cParms       Number of parameters.
 *
 * @note    Called from within pClient->CritSect.
 */
int shClSvcClientMsgCancel(PSHCLCLIENT pClient, uint32_t cParms)
{
    /*
     * Validate the request.
     */
    ASSERT_GUEST_MSG_RETURN(cParms == 0, ("cParms=%u!\n", cParms), VERR_WRONG_PARAMETER_COUNT);

    /*
     * Execute.
     */
    if (pClient->Pending.uType != 0)
    {
        LogFlowFunc(("[Client %RU32] Cancelling waiting thread, isPending=%d, pendingNumParms=%RU32, m_idSession=%x\n",
                     pClient->State.uClientID, pClient->Pending.uType, pClient->Pending.cParms, pClient->State.uSessionID));

        /*
         * The PEEK call is simple: At least two parameters, all set to zero before sleeping.
         */
        int rcComplete;
        if (pClient->Pending.uType == VBOX_SHCL_GUEST_FN_MSG_PEEK_WAIT)
        {
            Assert(pClient->Pending.cParms >= 2);
            if (pClient->Pending.paParms[0].type == VBOX_HGCM_SVC_PARM_64BIT)
                HGCMSvcSetU64(&pClient->Pending.paParms[0], VBOX_SHCL_HOST_MSG_CANCELED);
            else
                HGCMSvcSetU32(&pClient->Pending.paParms[0], VBOX_SHCL_HOST_MSG_CANCELED);
            rcComplete = VINF_TRY_AGAIN;
        }
        /*
         * The MSG_OLD call is complicated, though we're
         * generally here to wake up someone who is peeking and have two parameters.
         * If there aren't two parameters, fail the call.
         */
        else
        {
            Assert(pClient->Pending.uType == VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT);
            if (pClient->Pending.cParms > 0)
                HGCMSvcSetU32(&pClient->Pending.paParms[0], VBOX_SHCL_HOST_MSG_CANCELED);
            if (pClient->Pending.cParms > 1)
                HGCMSvcSetU32(&pClient->Pending.paParms[1], 0);
            rcComplete = pClient->Pending.cParms == 2 ? VINF_SUCCESS : VERR_TRY_AGAIN;
        }

        g_ShClSvc.pHelpers->pfnCallComplete(pClient->Pending.hHandle, rcComplete);

        pClient->Pending.hHandle    = NULL;
        pClient->Pending.paParms    = NULL;
        pClient->Pending.cParms     = 0;
        pClient->Pending.uType      = 0;
        return VINF_SUCCESS;
    }
    return VWRN_NOT_FOUND;
}


/**
 * Implements VBOX_SHCL_GUEST_FN_REPORT_FORMATS.
 *
 * @returns VBox status code.
 * @param   pClient      The client state.
 * @param   cParms       Number of parameters.
 * @param   paParms      Array of parameters.
 */
int shClSvcClientMsgReportFormats(PSHCLCLIENT pClient, uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    /*
     * Check if the service mode allows this operation and whether the guest is
     * supposed to be reading from the host.
     */
    uint32_t uMode = ShClSvcGetMode();
    if (   uMode == VBOX_SHCL_MODE_BIDIRECTIONAL
        || uMode == VBOX_SHCL_MODE_GUEST_TO_HOST)
    { /* likely */ }
    else
        return VERR_ACCESS_DENIED;

    /*
     * Digest parameters.
     */
    ASSERT_GUEST_RETURN(   cParms == VBOX_SHCL_CPARMS_REPORT_FORMATS
                        || (   cParms == VBOX_SHCL_CPARMS_REPORT_FORMATS_61B
                            && (ShClSvcClientGetGuestFeatures0(pClient) & VBOX_SHCL_GF_0_CONTEXT_ID)),
                        VERR_WRONG_PARAMETER_COUNT);

    uintptr_t iParm = 0;
    if (cParms == VBOX_SHCL_CPARMS_REPORT_FORMATS_61B)
    {
        ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_64BIT, VERR_WRONG_PARAMETER_TYPE);
        /* no defined value, so just ignore it */
        iParm++;
    }
    ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_32BIT, VERR_WRONG_PARAMETER_TYPE);
    uint32_t fFormats = paParms[iParm].u.uint32;
    ASSERT_GUEST_RETURN(ShClFormatsAreValid(fFormats), VERR_INVALID_FLAGS);
    iParm++;
    if (cParms == VBOX_SHCL_CPARMS_REPORT_FORMATS_61B)
    {
        ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_32BIT, VERR_WRONG_PARAMETER_TYPE);
        ASSERT_GUEST_RETURN(paParms[iParm].u.uint32 == 0, VERR_INVALID_FLAGS);
        iParm++;
    }
    Assert(iParm == cParms);

    /*
     * Report the formats.
     *
     * We ignore empty reports if the guest isn't the clipboard owner, this
     * prevents a freshly booted guest with an empty clibpoard from clearing
     * the host clipboard on startup.  Likewise, when a guest shutdown it will
     * typically issue an empty report in case it's the owner, we don't want
     * that to clear host content either.
     */
    int rc;
    if (!fFormats)
        rc = VINF_SUCCESS;
    else
    {
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        fFormats = shClSvcHandleFormats(false /* fHostToGuest */, pClient, fFormats);
#endif
        shClSvcLock();
        rc = shClSvcExtReportFormatsToHost(pClient, fFormats);
        shClSvcUnlock();
    }

    return rc;
}

/**
 * Implements VBOX_SHCL_GUEST_FN_DATA_READ.
 *
 * Called when the guest wants to read host clipboard data.
 *
 * @returns VBox status code.
 * @retval  VINF_BUFFER_OVERFLOW if the guest supplied a smaller buffer than needed in order to read the host clipboard data.
 * @retval  VERR_INVALID_PARAMETER if the requested format is invalid.
 * @retval  VERR_ACCESS_DENIED if the guest requests file transfer data without
 *          having file transfers enabled and negotiated.
 * @param   pClient             Client that wants to read host clipboard data.
 * @param   cParms              Number of HGCM parameters supplied in \a paParms.
 * @param   paParms             Array of HGCM parameters.
 */
int shClSvcClientMsgDataRead(PSHCLCLIENT pClient, uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    LogFlowFuncEnter();

    /*
     * Check if the service mode allows this operation and whether the guest is
     * supposed to be reading from the host.
     */
    uint32_t uMode = ShClSvcGetMode();
    if (   uMode == VBOX_SHCL_MODE_BIDIRECTIONAL
        || uMode == VBOX_SHCL_MODE_HOST_TO_GUEST)
    { /* likely */ }
    else
        return VERR_ACCESS_DENIED;

    /*
     * Digest parameters.
     *
     * We are dragging some legacy here from the 6.1 dev cycle, a 5 parameter
     * variant which prepends a 64-bit context ID (RAZ as meaning not defined),
     * a 32-bit flag (MBZ, no defined meaning) and switches the last two parameters.
     */
    ASSERT_GUEST_RETURN(   cParms == VBOX_SHCL_CPARMS_DATA_READ
                        || (    cParms == VBOX_SHCL_CPARMS_DATA_READ_61B
                            &&  (ShClSvcClientGetGuestFeatures0(pClient) & VBOX_SHCL_GF_0_CONTEXT_ID)),
                        VERR_WRONG_PARAMETER_COUNT);

    uintptr_t iParm = 0;
    SHCLCLIENTCMDCTX cmdCtx;
    RT_ZERO(cmdCtx);
    if (cParms == VBOX_SHCL_CPARMS_DATA_READ_61B)
    {
        ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_64BIT, VERR_WRONG_PARAMETER_TYPE);
        /* This has no defined meaning and was never used, however the guest passed stuff, so ignore it and leave idContext=0. */
        iParm++;
        ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_32BIT, VERR_WRONG_PARAMETER_TYPE);
        ASSERT_GUEST_RETURN(paParms[iParm].u.uint32 == 0, VERR_INVALID_FLAGS);
        iParm++;
    }

    SHCLFORMAT  uFormat = VBOX_SHCL_FMT_NONE;
    uint32_t    cbData  = 0;
    void       *pvData  = NULL;

    ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_32BIT, VERR_WRONG_PARAMETER_TYPE);
    uFormat = paParms[iParm].u.uint32;
    iParm++;
    if (cParms != VBOX_SHCL_CPARMS_DATA_READ_61B)
    {
        ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_PTR, VERR_WRONG_PARAMETER_TYPE); /* Data buffer */
        pvData = paParms[iParm].u.pointer.addr;
        cbData = paParms[iParm].u.pointer.size;
        iParm++;
        ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_32BIT, VERR_WRONG_PARAMETER_TYPE); /*cbDataReturned*/
        iParm++;
    }
    else
    {
        ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_32BIT, VERR_WRONG_PARAMETER_TYPE); /*cbDataReturned*/
        iParm++;
        ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_PTR, VERR_WRONG_PARAMETER_TYPE); /* Data buffer */
        pvData = paParms[iParm].u.pointer.addr;
        cbData = paParms[iParm].u.pointer.size;
        iParm++;
    }
    Assert(iParm == cParms);

    if (!ShClFormatIsValid(uFormat))
    {
        LogRelMax(16, ("Shared Clipboard: Rejecting host clipboard data request with invalid format %#x\n", uFormat));
        return VERR_INVALID_PARAMETER;
    }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (   uFormat == VBOX_SHCL_FMT_URI_LIST
        && !shClSvcClientTransfersAreAllowed(pClient))
#else
    if (uFormat == VBOX_SHCL_FMT_URI_LIST)
#endif
    {
        LogRelMax(16, ("Shared Clipboard: Rejecting guest file transfer data request without enabled and negotiated transfers\n"));
        return VERR_ACCESS_DENIED;
    }

    /*
     * For some reason we need to do this (makes absolutely no sense to bird).
     */
    /** @todo r=bird: I really don't get why you need the State.POD.uFormat
     *        member.  I'm sure there is a reason.  Incomplete code? */
    if (!(ShClSvcClientGetGuestFeatures0(pClient) & VBOX_SHCL_GF_0_CONTEXT_ID))
    {
        if (pClient->State.POD.uFormat == VBOX_SHCL_FMT_NONE)
            pClient->State.POD.uFormat = uFormat;
    }

    if (LogRelIs2Enabled())
    {
        char *pszFmt = ShClFormatsToStrA(uFormat);
        LogRel2(("Shared Clipboard: Guest wants to read %RU32 bytes host clipboard data in format %#x/'%s'\n",
                 cbData, uFormat, pszFmt ? pszFmt : "<alloc failed>"));
        RTStrFree(pszFmt);
    }

    /*
     * Do the reading.
     */
    uint32_t cbActual = 0;

    shClSvcLock();

    /* Read data from Main, which selects the remote or local host provider. */
    int rc = shClSvcExtReadData(pClient, uFormat, pvData, cbData, &cbActual);

    if (RT_FAILURE(rc))
        LogRelMax(16, ("Shared Clipboard: Reading extension clipboard data (max %RU32 bytes) failed after %RU32 bytes: %Rrc\n",
                       cbData, cbActual, rc));
    else
        LogRel2(("Shared Clipboard: Read extension clipboard data (max %RU32 bytes), got %RU32 bytes: rc=%Rrc\n",
                 cbData, cbActual, rc));

    if (RT_SUCCESS(rc))
    {
        /* Return the actual size required to fullfil the request. */
        if (cParms != VBOX_SHCL_CPARMS_DATA_READ_61B)
            HGCMSvcSetU32(&paParms[2], cbActual);
        else
            HGCMSvcSetU32(&paParms[3], cbActual);

        /* If the data to return exceeds the buffer the guest supplies, tell it (and let it try again). */
        if (cbActual > cbData)
            rc = VINF_BUFFER_OVERFLOW;
    }

    shClSvcUnlock();

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Implements VBOX_SHCL_GUEST_FN_DATA_WRITE.
 *
 * Called when the guest writes clipboard data to the host.
 *
 * @returns VBox status code.
 * @param   pClient             Client that wants to read host clipboard data.
 * @param   cParms              Number of HGCM parameters supplied in \a paParms.
 * @param   paParms             Array of HGCM parameters.
 */
int shClSvcClientMsgDataWrite(PSHCLCLIENT pClient, uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    LogFlowFuncEnter();

    /*
     * Check if the service mode allows this operation and whether the guest is
     * supposed to be reading from the host.
     */
    uint32_t uMode = ShClSvcGetMode();
    if (   uMode == VBOX_SHCL_MODE_BIDIRECTIONAL
        || uMode == VBOX_SHCL_MODE_GUEST_TO_HOST)
    { /* likely */ }
    else
        return VERR_ACCESS_DENIED;

    const bool fReportsContextID = RT_BOOL(ShClSvcClientGetGuestFeatures0(pClient) & VBOX_SHCL_GF_0_CONTEXT_ID);

    /*
     * Digest parameters.
     *
     * There are 3 different format here, formatunately no parameters have been
     * switch around so it's plain sailing compared to the DATA_READ message.
     */
    ASSERT_GUEST_RETURN(fReportsContextID
                        ? cParms == VBOX_SHCL_CPARMS_DATA_WRITE || cParms == VBOX_SHCL_CPARMS_DATA_WRITE_61B
                        : cParms == VBOX_SHCL_CPARMS_DATA_WRITE_OLD,
                        VERR_WRONG_PARAMETER_COUNT);

    uintptr_t iParm = 0;
    SHCLCLIENTCMDCTX cmdCtx;
    RT_ZERO(cmdCtx);
    if (cParms > VBOX_SHCL_CPARMS_DATA_WRITE_OLD)
    {
        ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_64BIT, VERR_WRONG_PARAMETER_TYPE);
        cmdCtx.uContextID = paParms[iParm].u.uint64;
        iParm++;
    }
    else
    {
        /* Older Guest Additions (< 6.1) did not supply a context ID.
         * We dig it out from our saved context ID list then a bit down below. */
    }

    if (cParms == VBOX_SHCL_CPARMS_DATA_WRITE_61B)
    {
        ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_32BIT, VERR_WRONG_PARAMETER_TYPE);
        ASSERT_GUEST_RETURN(paParms[iParm].u.uint32 == 0, VERR_INVALID_FLAGS);
        iParm++;
    }

    SHCLFORMAT  uFormat = VBOX_SHCL_FMT_NONE;
    uint32_t    cbData  = 0;
    void       *pvData  = NULL;

    ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_32BIT, VERR_WRONG_PARAMETER_TYPE); /* Format bit. */
    uFormat = paParms[iParm].u.uint32;
    iParm++;
    if (!ShClFormatIsValid(uFormat))
    {
        LogRelMax(16, ("Shared Clipboard: Rejecting guest clipboard data with invalid format %#x\n", uFormat));
        return VERR_INVALID_PARAMETER;
    }
    if (cParms == VBOX_SHCL_CPARMS_DATA_WRITE_61B)
    {
        ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_32BIT, VERR_WRONG_PARAMETER_TYPE); /* "cbData" - duplicates buffer size. */
        iParm++;
    }
    ASSERT_GUEST_RETURN(paParms[iParm].type == VBOX_HGCM_SVC_PARM_PTR, VERR_WRONG_PARAMETER_TYPE); /* Data buffer */
    pvData = paParms[iParm].u.pointer.addr;
    cbData = paParms[iParm].u.pointer.size;
    iParm++;
    Assert(iParm == cParms);

    /*
     * Handle / check context ID.
     */
    if (!fReportsContextID) /* Do we have to deal with old(er) GAs (< 6.1) which don't support context IDs? Dig out the context ID then. */
    {
        PSHCLCLIENTLEGACYCID pCID = NULL;
        PSHCLCLIENTLEGACYCID pCIDIter;
        RTListForEach(&pClient->Legacy.lstCID, pCIDIter, SHCLCLIENTLEGACYCID, Node) /* Slow, but does the job for now. */
        {
            if (pCIDIter->uFormat == uFormat)
            {
                pCID = pCIDIter;
                break;
            }
        }

        ASSERT_GUEST_MSG_RETURN(pCID != NULL, ("Context ID for format %#x not found\n", uFormat), VERR_INVALID_CONTEXT);
        cmdCtx.uContextID = pCID->uCID;

        /* Not needed anymore; clean up. */
        Assert(pClient->Legacy.cCID);
        pClient->Legacy.cCID--;
        RTListNodeRemove(&pCID->Node);
        RTMemFree(pCID);
    }

    uint64_t const idCtxExpected = VBOX_SHCL_CONTEXTID_MAKE(pClient->State.uSessionID, pClient->EventSrc.uID,
                                                            VBOX_SHCL_CONTEXTID_GET_EVENT(cmdCtx.uContextID));
    ASSERT_GUEST_MSG_RETURN(cmdCtx.uContextID == idCtxExpected,
                            ("Wrong context ID: %#RX64, expected %#RX64\n", cmdCtx.uContextID, idCtxExpected),
                            VERR_INVALID_CONTEXT);

    /*
     * For some reason we need to do this (makes absolutely no sense to bird).
     */
    /** @todo r=bird: I really don't get why you need the State.POD.uFormat
     *        member.  I'm sure there is a reason.  Incomplete code? */
    if (!(ShClSvcClientGetGuestFeatures0(pClient) & VBOX_SHCL_GF_0_CONTEXT_ID))
    {
        if (pClient->State.POD.uFormat == VBOX_SHCL_FMT_NONE)
            pClient->State.POD.uFormat = uFormat;
    }

    if (LogRelIs2Enabled())
    {
        char *pszFmt = ShClFormatsToStrA(uFormat);
        LogRel2(("Shared Clipboard: Guest writes %RU32 bytes clipboard data in format %#x/'%s' to host\n",
                 cbData, uFormat, pszFmt ? pszFmt : "<alloc failed>"));
        RTStrFree(pszFmt);
    }

    /*
     * Write the data to the active host side clipboard.
     */
    shClSvcLock();

    int const rc = shClSvcExtWriteData(pClient, &cmdCtx, uFormat, pvData, cbData);

    shClSvcUnlock();

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Implements the VBOX_SHCL_GUEST_FN_ERROR.
 *
 * @returns VBox status code.
 * @param   pClient             Client reporting the error.
 * @param   cParms              Number of HGCM parameters supplied in \a paParms.
 * @param   paParms             Array of HGCM parameters.
 * @param   pRc                 Where to store the received error code.
 */
int shClSvcClientMsgError(PSHCLCLIENT pClient, uint32_t cParms, VBOXHGCMSVCPARM paParms[], int *pRc)
{
    AssertPtrReturn(pClient,  VERR_INVALID_PARAMETER);
    AssertPtrReturn(paParms,  VERR_INVALID_PARAMETER);
    AssertPtrReturn(pRc,      VERR_INVALID_PARAMETER);

    int rc;

    if (cParms == VBOX_SHCL_CPARMS_ERROR)
    {
        uint64_t uContextID;
        rc = HGCMSvcGetU64(&paParms[0], &uContextID);
        if (RT_SUCCESS(rc))
        {
            if (VBOX_SHCL_CONTEXTID_GET_SESSION(uContextID) == pClient->State.uSessionID)
                rc = HGCMSvcGetU32(&paParms[1], (uint32_t *)pRc); /** @todo int vs. uint32_t !!! */
            else
                rc = VERR_INVALID_CONTEXT;
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Initializes a Shared Clipboard service's client state.
 *
 * @returns VBox status code.
 * @param   pClientState        Client state to initialize.
 * @param   uClientID           Client ID (HGCM) to use for this client state.
 * @param   idSession           Service session ID to assign.
 */
static int shClSvcClientStateInit(PSHCLCLIENTSTATE pClientState, uint32_t uClientID, SHCLSESSIONID idSession)
{
    LogFlowFuncEnter();

    shClSvcClientStateReset(pClientState);

    /* Register the client. */
    pClientState->uClientID = uClientID;
    pClientState->uSessionID = idSession;

    return VINF_SUCCESS;
}

/**
 * Terminated (uninitializes) a Shared Clipboard service's client state.
 *
 * @returns VBox status code.
 * @param   pState              Client state to destroy.
 */
static int shClSvcClientStateTerm(PSHCLCLIENTSTATE pState)
{
    LogFlowFuncEnter();

    shClSvcClientStateReset(pState);

    return VINF_SUCCESS;
}

/**
 * Resets a Shared Clipboard service's client state.
 *
 * @param   pState              Client state to reset.
 */
static void shClSvcClientStateReset(PSHCLCLIENTSTATE pState)
{
    LogFlowFuncEnter();

    ASMAtomicWriteU64(&pState->fGuestFeatures0, VBOX_SHCL_GF_NONE);
    ASMAtomicWriteU64(&pState->fGuestFeatures1, VBOX_SHCL_GF_NONE);

    pState->cbChunkSize     = VBOX_SHCL_DEFAULT_CHUNK_SIZE; /** @todo Make this configurable. */
    pState->enmSource       = SHCLSOURCE_INVALID;
    pState->fFlags          = SHCLCLIENTSTATE_FLAGS_NONE;

    pState->POD.enmDir             = SHCLTRANSFERDIR_UNKNOWN;
    pState->POD.uFormat            = VBOX_SHCL_FMT_NONE;
    pState->POD.cbToReadWriteTotal = 0;
    pState->POD.cbReadWritten      = 0;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    pState->Transfers.enmTransferDir = SHCLTRANSFERDIR_UNKNOWN;
#endif
}
