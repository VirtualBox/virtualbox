/* $Id: VBoxSharedClipboardSvc-client.cpp 114858 2026-08-05 15:08:05Z andreas.loeffler@oracle.com $ */
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
static int  shClSvcClientStateInit(PSHCLCLIENTSTATE pState, uint32_t uClientID);
static int  shClSvcClientStateTerm(PSHCLCLIENTSTATE pState);
static void shClSvcClientStateReset(PSHCLCLIENTSTATE pState);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
static SHCLSESSIONID shClSvcClientAllocSessionId(void)
{
    bool const fOwnsSvcLock = RTCritSectIsOwner(&g_CritSect);
    if (!fOwnsSvcLock)
    {
        int rc = RTCritSectEnter(&g_CritSect);
        AssertRCReturn(rc, 1);
    }

    if (   g_idNextSession == 0
        || g_idNextSession == NIL_SHCLSESSIONID)
        g_idNextSession = 1;

    SHCLSESSIONID const idSession = g_idNextSession++;
    if (   g_idNextSession == 0
        || g_idNextSession == NIL_SHCLSESSIONID)
        g_idNextSession = 1;

    if (!fOwnsSvcLock)
    {
        int rc = RTCritSectLeave(&g_CritSect);
        AssertRC(rc);
    }

    return idSession;
}
#endif

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
 * @param   pClient             Client to initialize.
 * @param   uClientID           HGCM client ID to assign client to.
 */
int ShClSvcClientInit(PSHCLCLIENT pClient, uint32_t uClientID)
{
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);

    /* Assign the client ID. */
    pClient->State.uClientID = uClientID;

    /* Cache the current Shared Clipboard mode for the backend. */
    pClient->State.uMode = ShClSvcGetMode();

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /* Cache the current Shared Clipboard transfer (file) mode for the backend. */
    pClient->State.Transfers.uTransferMode = g_fTransferMode;
#endif

    RTListInit(&pClient->MsgQueue);
    pClient->cMsgAllocated = 0;

    RTListInit(&pClient->Legacy.lstCID);
    pClient->Legacy.cCID = 0;

    LogFlowFunc(("[Client %RU32]\n", pClient->State.uClientID));

    int rc = RTCritSectInit(&pClient->CritSect);
    if (RT_SUCCESS(rc))
    {
        /* Create the client's own event source. */
        rc = ShClEventSourceInit(&pClient->EventSrc, 0 /* ID, ignored */);
        if (RT_SUCCESS(rc))
        {
            LogFlowFunc(("[Client %RU32] Using event source %RU32\n", uClientID, pClient->EventSrc.uID));

            /* Reset the client state. */
            shClSvcClientStateReset(&pClient->State);

            /* (Re-)initialize the client state. */
            rc = shClSvcClientStateInit(&pClient->State, uClientID);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            if (RT_SUCCESS(rc))
            {
                rc = ShClTransferCtxInit(&pClient->Transfers.Ctx);
                if (RT_SUCCESS(rc))
                    rc = ShClTransferCtxBeginSession(&pClient->Transfers.Ctx, pClient->State.uSessionID);
            }
#endif
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

    /* Make sure to send a quit message to the guest so that it can terminate gracefully. */
    ShClSvcClientLock(pClient);

    if (pClient->Pending.uType)
    {
        if (pClient->Pending.cParms > 1)
            HGCMSvcSetU32(&pClient->Pending.paParms[0], VBOX_SHCL_HOST_MSG_QUIT);
        if (pClient->Pending.cParms > 2)
            HGCMSvcSetU32(&pClient->Pending.paParms[1], 0);
        g_pHelpers->pfnCallComplete(pClient->Pending.hHandle, VINF_SUCCESS);
        pClient->Pending.uType   = 0;
        pClient->Pending.cParms  = 0;
        pClient->Pending.hHandle = NULL;
        pClient->Pending.paParms = NULL;
    }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    shClSvcTransferDestroyAll(pClient);
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

    ClipboardClientMap::iterator itClient = g_mapClients.find(pClient->State.uClientID);
    if (itClient != g_mapClients.end())
        g_mapClients.erase(itClient);
    else
        AssertFailed();

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

    LogFlowFunc(("[Client %RU32]\n", pClient->State.uClientID));
    RTCritSectEnter(&pClient->CritSect);

    uint32_t const uClientID = pClient->State.uClientID;

    /* Reset message queue. */
    shClSvcClientMsgQueueReset(pClient);

    /* Reset event source. */
    ShClEventSourceReset(&pClient->EventSrc);

    /* Reset pending state. */
    RT_ZERO(pClient->Pending);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    shClSvcTransferDestroyAll(pClient);
#endif

    shClSvcClientStateReset(&pClient->State);
    int rc2 = shClSvcClientStateInit(&pClient->State, uClientID);
    AssertRC(rc2);
    pClient->State.uMode = ShClSvcGetMode();
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    pClient->State.Transfers.uTransferMode = g_fTransferMode;
    if (RT_SUCCESS(rc2))
    {
        rc2 = ShClTransferCtxBeginSession(&pClient->Transfers.Ctx, pClient->State.uSessionID);
        AssertRC(rc2);
    }
#endif

    RTCritSectLeave(&pClient->CritSect);
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
    pClient->State.uMode = ShClSvcGetMode();
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    pClient->State.Transfers.uTransferMode = g_fTransferMode;
#endif

#ifdef LOG_ENABLED
    Log2Func(("u32ClientID=%RU32, fn=%RU32 (%s), cParms=%RU32, paParms=%p\n",
              u32ClientID, u32Function, ShClGuestMsgToStr(u32Function), cParms, paParms));
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
              pClient->State.fFlags, pClient->State.fGuestFeatures0, pClient->State.fGuestFeatures1));
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
            LogRel2(("Shared Clipboard: Unknown guest function: %u (%#x)\n", u32Function, u32Function));
            rc = VERR_NOT_IMPLEMENTED;
#endif
            break;
        }
    }

    LogFlowFunc(("[Client %RU32] rc=%Rrc\n", pClient->State.uClientID, rc));

    if (rc != VINF_HGCM_ASYNC_EXECUTE)
        g_pHelpers->pfnCallComplete(callHandle, rc);
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

    int rc = g_pHelpers->pfnCallComplete(hCall, VINF_SUCCESS);
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
    paParms[0].u.uint64 = g_fHostFeatures0;
    paParms[1].u.uint64 = 0;

    int rc = g_pHelpers->pfnCallComplete(hCall, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        pClient->State.fGuestFeatures0 = fFeatures0;
        pClient->State.fGuestFeatures1 = fFeatures1;
        LogRel2(("Shared Clipboard: Guest reported the following features: %#RX64\n",
                 pClient->State.fGuestFeatures0)); /* Note: fFeatures1 not used yet. */
        if (pClient->State.fGuestFeatures0 & VBOX_SHCL_GF_0_TRANSFERS)
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
    paParms[0].u.uint64 = g_fHostFeatures0;
    paParms[1].u.uint64 = 0;
    int rc = g_pHelpers->pfnCallComplete(hCall, VINF_SUCCESS);
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
        uint64_t idRestore = g_pHelpers->pfnGetVMMDevSessionId(g_pHelpers);
        if (idRestoreCheck != idRestore)
        {
            paParms[0].u.uint64 = idRestore;
            LogFlowFunc(("[Client %RU32] VBOX_SHCL_GUEST_FN_MSG_PEEK_XXX -> VERR_VM_RESTORED (%#RX64 -> %#RX64)\n",
                         pClient->State.uClientID, idRestoreCheck, idRestore));
            return VERR_VM_RESTORED;
        }
        Assert(!g_pHelpers->pfnIsCallRestored(hCall));
    }

    /*
     * Return information about the first message if one is pending in the list.
     */
    PSHCLCLIENTMSG pFirstMsg = RTListGetFirst(&pClient->MsgQueue, SHCLCLIENTMSG, ListEntry);
    if (pFirstMsg)
    {
        shClSvcMsgSetPeekReturn(pFirstMsg, paParms, cParms);
        LogFlowFunc(("[Client %RU32] VBOX_SHCL_GUEST_FN_MSG_PEEK_XXX -> VINF_SUCCESS (idMsg=%s (%u), cParms=%u)\n",
                     pClient->State.uClientID, ShClHostMsgToStr(pFirstMsg->idMsg), pFirstMsg->idMsg, pFirstMsg->cParms));
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
                     ShClHostMsgToStr(pFirstMsg->idMsg), pFirstMsg->idMsg, pFirstMsg->cParms));

        rc = shClSvcMsgSetOldWaitReturn(pFirstMsg, paParms, cParms);
        AssertPtr(g_pHelpers);
        rc = g_pHelpers->pfnCallComplete(hCall, rc);
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
    uint32_t const idMsgExpected = cParms > 0 && paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT ? paParms[0].u.uint32
                                 : cParms > 0 && paParms[0].type == VBOX_HGCM_SVC_PARM_64BIT ? paParms[0].u.uint64
                                 : UINT32_MAX;

    /*
     * Return information about the first message if one is pending in the list.
     */
    PSHCLCLIENTMSG pFirstMsg = RTListGetFirst(&pClient->MsgQueue, SHCLCLIENTMSG, ListEntry);
    if (pFirstMsg)
    {
        LogFlowFunc(("First message is: %s (%u), cParms=%RU32\n", ShClHostMsgToStr(pFirstMsg->idMsg), pFirstMsg->idMsg, pFirstMsg->cParms));

        ASSERT_GUEST_MSG_RETURN(pFirstMsg->idMsg == idMsgExpected || idMsgExpected == UINT32_MAX,
                                ("idMsg=%u (%s) cParms=%u, caller expected %u (%s) and %u\n",
                                 pFirstMsg->idMsg, ShClHostMsgToStr(pFirstMsg->idMsg), pFirstMsg->cParms,
                                 idMsgExpected, ShClHostMsgToStr(idMsgExpected), cParms),
                                VERR_MISMATCH);
        ASSERT_GUEST_MSG_RETURN(pFirstMsg->cParms == cParms,
                                ("idMsg=%u (%s) cParms=%u, caller expected %u (%s) and %u\n",
                                 pFirstMsg->idMsg, ShClHostMsgToStr(pFirstMsg->idMsg), pFirstMsg->cParms,
                                 idMsgExpected, ShClHostMsgToStr(idMsgExpected), cParms),
                                VERR_WRONG_PARAMETER_COUNT);

        /* Check the parameter types. */
        for (uint32_t i = 0; i < cParms; i++)
            ASSERT_GUEST_MSG_RETURN(pFirstMsg->aParms[i].type == paParms[i].type,
                                    ("param #%u: type %u, caller expected %u (idMsg=%u %s)\n", i, pFirstMsg->aParms[i].type,
                                     paParms[i].type, pFirstMsg->idMsg, ShClHostMsgToStr(pFirstMsg->idMsg)),
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
            AssertPtr(g_pHelpers);
            rc = g_pHelpers->pfnCallComplete(hCall, rc);

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

        g_pHelpers->pfnCallComplete(pClient->Pending.hHandle, rcComplete);

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
                            && (pClient->State.fGuestFeatures0 & VBOX_SHCL_GF_0_CONTEXT_ID)),
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
        rc = RTCritSectEnter(&g_CritSect);
        if (RT_SUCCESS(rc))
        {
            rc = shClSvcBackendReportFormatsToHost(pClient, fFormats);

            RTCritSectLeave(&g_CritSect);
        }
        else
            LogRel2(("Shared Clipboard: Unable to take internal lock while receiving guest clipboard announcement: %Rrc\n", rc));
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
                            &&  (pClient->State.fGuestFeatures0 & VBOX_SHCL_GF_0_CONTEXT_ID)),
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

    /*
     * For some reason we need to do this (makes absolutely no sense to bird).
     */
    /** @todo r=bird: I really don't get why you need the State.POD.uFormat
     *        member.  I'm sure there is a reason.  Incomplete code? */
    if (!(pClient->State.fGuestFeatures0 & VBOX_SHCL_GF_0_CONTEXT_ID))
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

    int rc = RTCritSectEnter(&g_CritSect);
    AssertRCReturn(rc, rc);

    g_ExtState.fReadingData = true;

    /* If there is a service extension active, try reading data from it first. */
    rc = shClSvcBackendReadData(pClient, uFormat, pvData, cbData, &cbActual);

    LogRel2(("Shared Clipboard: Read extension clipboard data (fDelayedAnnouncement=%RTbool, fDelayedFormats=%#x, "
             "max %RU32 bytes), got %RU32 bytes: rc=%Rrc\n", g_ExtState.fDelayedAnnouncement, g_ExtState.fDelayedFormats,
             cbData, cbActual, rc));

    /* Did the extension send the clipboard formats yet?
     * Otherwise, do this now. */
    if (g_ExtState.fDelayedAnnouncement)
    {
        int rc2 = shClSvcBackendReportFormatsToGuest(pClient, g_ExtState.fDelayedFormats, SHCLSOURCE_REMOTE);
        AssertRC(rc2);

        g_ExtState.fDelayedAnnouncement = false;
        g_ExtState.fDelayedFormats = 0;
    }

    g_ExtState.fReadingData = false;

    if (RT_SUCCESS(rc))
    {
        /* Return the actual size required to fullfil the request. */
        if (cParms != VBOX_SHCL_CPARMS_DATA_READ_61B)
            HGCMSvcSetU32(&paParms[2], cbActual);
        else
            HGCMSvcSetU32(&paParms[3], cbActual);

        /* If the data to return exceeds the buffer the guest supplies, tell it (and let it try again). */
        if (cbActual >= cbData)
            rc = VINF_BUFFER_OVERFLOW;
    }

    RTCritSectLeave(&g_CritSect);

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

    const bool fReportsContextID = RT_BOOL(pClient->State.fGuestFeatures0 & VBOX_SHCL_GF_0_CONTEXT_ID);

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
        LogRelMax2(16, ("Shared Clipboard: Rejecting guest clipboard data with invalid format %#x\n", uFormat));
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
    if (!(pClient->State.fGuestFeatures0 & VBOX_SHCL_GF_0_CONTEXT_ID))
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
    int rc = RTCritSectEnter(&g_CritSect);
    AssertRCReturn(rc, rc);

    rc = shClSvcBackendWriteData(pClient, &cmdCtx, uFormat, pvData, cbData);

    RTCritSectLeave(&g_CritSect);

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
 */
static int shClSvcClientStateInit(PSHCLCLIENTSTATE pClientState, uint32_t uClientID)
{
    LogFlowFuncEnter();

    shClSvcClientStateReset(pClientState);

    /* Register the client. */
    pClientState->uClientID = uClientID;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    pClientState->uSessionID = shClSvcClientAllocSessionId();
#endif

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

    pState->fGuestFeatures0 = VBOX_SHCL_GF_NONE;
    pState->fGuestFeatures1 = VBOX_SHCL_GF_NONE;

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
