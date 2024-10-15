/* $Id: VBoxDragAndDropSvc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Drag and Drop Service.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

/** @page pg_svc_dnd    Drag and drop HGCM Service
 *
 * @sa See src/VBox/Main/src-client/GuestDnDPrivate.cpp for more information.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_GUEST_DND
#include <VBox/GuestHost/DragAndDrop.h>
#include <VBox/GuestHost/DragAndDropDefs.h>
#include <VBox/HostServices/Service.h>
#include <VBox/HostServices/DragAndDropSvc.h>
#include <VBox/AssertGuest.h>

#include <VBox/err.h>

#include <algorithm>
#include <list>
#include <map>

#include "dndmanager.h"

using namespace DragAndDropSvc;


/*********************************************************************************************************************************
*   Service class declaration                                                                                                    *
*********************************************************************************************************************************/

class DragAndDropClient : public HGCM::Client
{
public:

    DragAndDropClient(uint32_t idClient)
        : HGCM::Client(idClient)
        , uProtocolVerDeprecated(0)
        , fGuestFeatures0(VBOX_DND_GF_NONE)
        , fGuestFeatures1(VBOX_DND_GF_NONE)
    {
        RT_ZERO(m_SvcCtx);
    }

    virtual ~DragAndDropClient(void)
    {
        disconnect();
    }

public:

    void disconnect(void) RT_NOEXCEPT;

public:

    /** Protocol version used by this client.
     *  Deprecated; only used for keeping backwards compatibility. */
    uint32_t                uProtocolVerDeprecated;
    /** Guest feature flags, VBOX_DND_GF_0_XXX. */
    uint64_t                fGuestFeatures0;
    /** Guest feature flags, VBOX_DND_GF_1_XXX. */
    uint64_t                fGuestFeatures1;
};

/** Map holding pointers to drag and drop clients. Key is the (unique) HGCM client ID. */
typedef std::map<uint32_t, DragAndDropClient*> DnDClientMap;

/** Simple queue (list) which holds deferred (waiting) clients. */
typedef std::list<uint32_t> DnDClientQueue;

/**
 * Specialized drag & drop service class.
 */
class DragAndDropService : public HGCM::AbstractService<DragAndDropService>
{
public:
    explicit DragAndDropService(PVBOXHGCMSVCHELPERS pHelpers)
        : HGCM::AbstractService<DragAndDropService>(pHelpers)
        , m_pManager(NULL)
        , m_u32Mode(VBOX_DRAG_AND_DROP_MODE_OFF)
        , m_fHostFeatures0(VBOX_DND_HF_NONE)
    {}

protected:
    int  init(VBOXHGCMSVCFNTABLE *pTable) RT_NOEXCEPT RT_OVERRIDE;
    int  uninit(void) RT_NOEXCEPT RT_OVERRIDE;
    int  clientConnect(uint32_t idClient, void *pvClient) RT_NOEXCEPT RT_OVERRIDE;
    int  clientDisconnect(uint32_t idClient, void *pvClient) RT_NOEXCEPT RT_OVERRIDE;
    int  clientQueryFeatures(uint32_t cParms, VBOXHGCMSVCPARM paParms[]) RT_NOEXCEPT;
    int  clientReportFeatures(DragAndDropClient *pClient, uint32_t cParms, VBOXHGCMSVCPARM paParms[]) RT_NOEXCEPT;
    void guestCall(VBOXHGCMCALLHANDLE callHandle, uint32_t idClient, void *pvClient, uint32_t u32Function,
                   uint32_t cParms, VBOXHGCMSVCPARM paParms[]) RT_NOEXCEPT RT_OVERRIDE;
    int  hostCall(uint32_t u32Function, uint32_t cParms, VBOXHGCMSVCPARM paParms[]) RT_NOEXCEPT RT_OVERRIDE;

private:
    int  modeSet(uint32_t u32Mode) RT_NOEXCEPT;
    inline uint32_t modeGet(void) const RT_NOEXCEPT
    { return m_u32Mode; };

    static DECLCALLBACK(int) progressCallback(uint32_t uStatus, uint32_t uPercentage, int rc, void *pvUser);

private:
    /** Pointer to our DnD manager instance. */
    DnDManager                        *m_pManager;
    /** Map of all connected clients.
     *  The primary key is the (unique) client ID, the secondary value
     *  an allocated pointer to the DragAndDropClient class, managed
     *  by this service class. */
    DnDClientMap                       m_clientMap;
    /** List of all clients which are queued up (deferred return) and ready
     *  to process new commands. The key is the (unique) client ID. */
    DnDClientQueue                     m_clientQueue;
    /** Current drag and drop mode, VBOX_DRAG_AND_DROP_MODE_XXX. */
    uint32_t                           m_u32Mode;
    /** Host feature mask (VBOX_DND_HF_0_XXX) for DND_GUEST_REPORT_FEATURES
     * and DND_GUEST_QUERY_FEATURES. */
    uint64_t                           m_fHostFeatures0;
};


/*********************************************************************************************************************************
*   Client implementation                                                                                                        *
*********************************************************************************************************************************/

/**
 * Called when the HGCM client disconnected on the guest side.
 *
 * This function takes care of the client's data cleanup and also lets the host
 * know that the client has been disconnected.
 */
void DragAndDropClient::disconnect(void) RT_NOEXCEPT
{
    LogFlowThisFunc(("uClient=%RU32, fDeferred=%RTbool\n", m_idClient, IsDeferred()));

    /*
     * If the client still is waiting for a message (i.e in deferred mode),
     * complete the call with a VERR_CANCELED status so that the client (VBoxTray / VBoxClient) knows
     * it should bail out.
     */
    if (IsDeferred())
        CompleteDeferred(VERR_CANCELLED);

    /*
     * Let the host know.
     */
    VBOXDNDCBDISCONNECTMSGDATA data;
    RT_ZERO(data);
    /** @todo Magic needed? */
    /** @todo Add context ID. */

    if (m_SvcCtx.pfnHostCallback)
    {
        int rc2 = m_SvcCtx.pfnHostCallback(m_SvcCtx.pvHostData, GUEST_DND_FN_DISCONNECT, &data, sizeof(data));
        if (RT_FAILURE(rc2))
            LogFlowFunc(("Warning: Unable to notify host about client %RU32 disconnect, rc=%Rrc\n", m_idClient, rc2));
        /* Not fatal. */
    }
}


/*********************************************************************************************************************************
*   Service class implementation                                                                                                 *
*********************************************************************************************************************************/

int DragAndDropService::init(VBOXHGCMSVCFNTABLE *pTable) RT_NOEXCEPT
{
    /* Legacy clients map to the root category. */
    pTable->idxLegacyClientCategory = HGCM_CLIENT_CATEGORY_ROOT;

    /* Limit to 255 clients (see also DragAndDropService::clientConnect). */
    for (uintptr_t i = 0; i < RT_ELEMENTS(pTable->acMaxClients); i++)
        pTable->acMaxClients[i] = UINT8_MAX;

    /* Limit the number of concurrent calls to 256 (playing safe).  */
    /** @todo Properly determin the max number of pending/concurrent calls for DnD. */
    for (uintptr_t i = 0; i < RT_ELEMENTS(pTable->acMaxClients); i++)
        pTable->acMaxCallsPerClient[i] = 256;

    /* Register functions. */
    pTable->pfnHostCall          = svcHostCall;
    pTable->pfnSaveState         = NULL;  /* The service is stateless, so the normal */
    pTable->pfnLoadState         = NULL;  /* construction done before restoring suffices */
    pTable->pfnRegisterExtension = svcRegisterExtension;
    pTable->pfnNotify            = NULL;

    /* Drag'n drop mode is disabled by default. */
    modeSet(VBOX_DRAG_AND_DROP_MODE_OFF);

    /* Set host features. */
    m_fHostFeatures0 = VBOX_DND_HF_NONE;

    int rc = VINF_SUCCESS;

    try
    {
        m_pManager = new DnDManager(&DragAndDropService::progressCallback, this);
    }
    catch (std::bad_alloc &)
    {
        rc = VERR_NO_MEMORY;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

int DragAndDropService::uninit(void) RT_NOEXCEPT
{
    LogFlowFuncEnter();

    if (m_pManager)
    {
        delete m_pManager;
        m_pManager = NULL;
    }

    DnDClientMap::iterator itClient =  m_clientMap.begin();
    while (itClient != m_clientMap.end())
    {
        delete itClient->second;
        m_clientMap.erase(itClient);
        itClient = m_clientMap.begin();
    }

    LogFlowFuncLeave();
    return VINF_SUCCESS;
}

int DragAndDropService::clientConnect(uint32_t idClient, void *pvClient) RT_NOEXCEPT
{
    RT_NOREF1(pvClient);
    if (m_clientMap.size() >= UINT8_MAX) /* Don't allow too much clients at the same time. */
    {
        AssertMsgFailed(("Maximum number of clients reached\n"));
        return VERR_MAX_PROCS_REACHED;
    }

    /*
     * Add client to our client map.
     */
    if (m_clientMap.find(idClient) != m_clientMap.end())
    {
        LogFunc(("Client %RU32 is already connected!\n", idClient));
        return VERR_ALREADY_EXISTS;
    }

    try
    {
        DragAndDropClient *pClient = new DragAndDropClient(idClient);
        pClient->SetSvcContext(m_SvcCtx);
        m_clientMap[idClient] = pClient;
    }
    catch (std::bad_alloc &)
    {
        LogFunc(("Client %RU32 - VERR_NO_MEMORY!\n", idClient));
        return VERR_NO_MEMORY;
    }

    LogFlowFunc(("Client %RU32 connected (VINF_SUCCESS)\n", idClient));
    return VINF_SUCCESS;
}

int DragAndDropService::clientDisconnect(uint32_t idClient, void *pvClient) RT_NOEXCEPT
{
    RT_NOREF1(pvClient);

    /* Client not found? Bail out early. */
    DnDClientMap::iterator itClient =  m_clientMap.find(idClient);
    if (itClient == m_clientMap.end())
    {
        LogFunc(("Client %RU32 not found!\n", idClient));
        return VERR_NOT_FOUND;
    }

    /*
     * Remove from waiters queue.
     */
    m_clientQueue.remove(idClient);

    /*
     * Remove from client map and deallocate.
     */
    AssertPtr(itClient->second);
    delete itClient->second;

    m_clientMap.erase(itClient);

    LogFlowFunc(("Client %RU32 disconnected\n", idClient));
    return VINF_SUCCESS;
}

/**
 * Implements GUEST_DND_FN_REPORT_FEATURES.
 *
 * @returns VBox status code.
 * @retval  VINF_HGCM_ASYNC_EXECUTE on success (we complete the message here).
 * @retval  VERR_ACCESS_DENIED if not master
 * @retval  VERR_INVALID_PARAMETER if bit 63 in the 2nd parameter isn't set.
 * @retval  VERR_WRONG_PARAMETER_COUNT
 *
 * @param   pClient     The client state.
 * @param   cParms      Number of parameters.
 * @param   paParms     Array of parameters.
 */
int DragAndDropService::clientReportFeatures(DragAndDropClient *pClient, uint32_t cParms, VBOXHGCMSVCPARM paParms[]) RT_NOEXCEPT
{
    RT_NOREF(pClient);

    /*
     * Validate the request.
     */
    ASSERT_GUEST_RETURN(cParms == 2, VERR_WRONG_PARAMETER_COUNT);
    ASSERT_GUEST_RETURN(paParms[0].type == VBOX_HGCM_SVC_PARM_64BIT, VERR_WRONG_PARAMETER_TYPE);
    uint64_t const fFeatures0 = paParms[0].u.uint64;
    ASSERT_GUEST_RETURN(paParms[1].type == VBOX_HGCM_SVC_PARM_64BIT, VERR_WRONG_PARAMETER_TYPE);
    uint64_t const fFeatures1 = paParms[1].u.uint64;
    ASSERT_GUEST_RETURN(fFeatures1 & VBOX_DND_GF_1_MUST_BE_ONE, VERR_INVALID_PARAMETER);

    /*
     * Report back the host features.
     */
    paParms[0].u.uint64 = m_fHostFeatures0;
    paParms[1].u.uint64 = 0;

    pClient->fGuestFeatures0 = fFeatures0;
    pClient->fGuestFeatures1 = fFeatures1;

    Log(("[Client %RU32] features: %#RX64 %#RX64\n", pClient->GetClientID(), fFeatures0, fFeatures1));

    return VINF_SUCCESS;
}

/**
 * Implements GUEST_DND_FN_QUERY_FEATURES.
 *
 * @returns VBox status code.
 * @retval  VINF_HGCM_ASYNC_EXECUTE on success (we complete the message here).
 * @retval  VERR_WRONG_PARAMETER_COUNT
 *
 * @param   cParms      Number of parameters.
 * @param   paParms     Array of parameters.
 */
int DragAndDropService::clientQueryFeatures(uint32_t cParms, VBOXHGCMSVCPARM paParms[]) RT_NOEXCEPT
{
    /*
     * Validate the request.
     */
    ASSERT_GUEST_RETURN(cParms == 2, VERR_WRONG_PARAMETER_COUNT);
    ASSERT_GUEST_RETURN(paParms[0].type == VBOX_HGCM_SVC_PARM_64BIT, VERR_WRONG_PARAMETER_TYPE);
    ASSERT_GUEST_RETURN(paParms[1].type == VBOX_HGCM_SVC_PARM_64BIT, VERR_WRONG_PARAMETER_TYPE);
    ASSERT_GUEST(paParms[1].u.uint64 & RT_BIT_64(63));

    /*
     * Report back the host features.
     */
    paParms[0].u.uint64 = m_fHostFeatures0;
    paParms[1].u.uint64 = 0;

    return VINF_SUCCESS;
}

int DragAndDropService::modeSet(uint32_t u32Mode) RT_NOEXCEPT
{
#ifndef VBOX_WITH_DRAG_AND_DROP_GH
    if (   u32Mode == VBOX_DRAG_AND_DROP_MODE_GUEST_TO_HOST
        || u32Mode == VBOX_DRAG_AND_DROP_MODE_BIDIRECTIONAL)
    {
        m_u32Mode = VBOX_DRAG_AND_DROP_MODE_OFF;
        return VERR_NOT_SUPPORTED;
    }
#endif

    switch (u32Mode)
    {
        case VBOX_DRAG_AND_DROP_MODE_OFF:
        case VBOX_DRAG_AND_DROP_MODE_HOST_TO_GUEST:
        case VBOX_DRAG_AND_DROP_MODE_GUEST_TO_HOST:
        case VBOX_DRAG_AND_DROP_MODE_BIDIRECTIONAL:
            m_u32Mode = u32Mode;
            break;

        default:
            m_u32Mode = VBOX_DRAG_AND_DROP_MODE_OFF;
            break;
    }

    return VINF_SUCCESS;
}

void DragAndDropService::guestCall(VBOXHGCMCALLHANDLE callHandle, uint32_t idClient,
                                   void *pvClient, uint32_t u32Function,
                                   uint32_t cParms, VBOXHGCMSVCPARM paParms[]) RT_NOEXCEPT
{
    RT_NOREF1(pvClient);
    LogFlowFunc(("idClient=%RU32, u32Function=%s (%#x), cParms=%RU32\n",
                 idClient, DnDGuestMsgToStr(u32Function), u32Function, cParms));

    /* Check if we've the right mode set. */
    int rc = VERR_ACCESS_DENIED; /* Play safe. */
    switch (u32Function)
    {
        case GUEST_DND_FN_GET_NEXT_HOST_MSG:
        {
            if (modeGet() != VBOX_DRAG_AND_DROP_MODE_OFF)
                rc = VINF_SUCCESS;
            else
            {
                LogRel(("DnD: Feature is disabled, ignoring request from guest\n"));
                rc = VINF_HGCM_ASYNC_EXECUTE;
            }
            break;
        }

        /* New since protocol v2. */
        case GUEST_DND_FN_CONNECT:
            RT_FALL_THROUGH();
        /* New since VBox 6.1.x. */
        case GUEST_DND_FN_REPORT_FEATURES:
            RT_FALL_THROUGH();
        /* New since VBox 6.1.x. */
        case GUEST_DND_FN_QUERY_FEATURES:
        {
            /*
             * Never block these calls, as the clients issues those when
             * initializing and might get stuck if drag and drop is set to "disabled" at
             * that time.
             */
            rc = VINF_SUCCESS;
            break;
        }

        /* New since VBOx 7.0.x. See define for details. */
        case GUEST_DND_FN_EVT_ERROR:
        {
            rc = VINF_SUCCESS;
            break;
        }

        case GUEST_DND_FN_HG_ACK_OP:
        case GUEST_DND_FN_HG_REQ_DATA:
        case GUEST_DND_FN_HG_EVT_PROGRESS:
        {
            if (   modeGet() == VBOX_DRAG_AND_DROP_MODE_BIDIRECTIONAL
                || modeGet() == VBOX_DRAG_AND_DROP_MODE_HOST_TO_GUEST)
                rc = VINF_SUCCESS;
            else
                LogRel(("DnD: Host -> Guest mode disabled, ignoring request from guest\n"));
            break;
        }

        case GUEST_DND_FN_GH_ACK_PENDING:
        case GUEST_DND_FN_GH_SND_DATA_HDR:
        case GUEST_DND_FN_GH_SND_DATA:
        case GUEST_DND_FN_GH_SND_DIR:
        case GUEST_DND_FN_GH_SND_FILE_HDR:
        case GUEST_DND_FN_GH_SND_FILE_DATA:
        {
#ifdef VBOX_WITH_DRAG_AND_DROP_GH
            if (   modeGet() == VBOX_DRAG_AND_DROP_MODE_BIDIRECTIONAL
                || modeGet() == VBOX_DRAG_AND_DROP_MODE_GUEST_TO_HOST)
                rc = VINF_SUCCESS;
            else
#endif
                LogRel(("DnD: Guest -> Host mode disabled, ignoring request from guest\n"));
            break;
        }

        default:
            /* Reach through to DnD manager. */
            rc = VINF_SUCCESS;
            break;
    }

#define DO_HOST_CALLBACK();                                                                   \
    if (   RT_SUCCESS(rc)                                                                     \
        && m_SvcCtx.pfnHostCallback)                                                          \
    {                                                                                         \
        rc = m_SvcCtx.pfnHostCallback(m_SvcCtx.pvHostData, u32Function, &data, sizeof(data)); \
    }

    /*
     * Lookup client.
     */
    DragAndDropClient *pClient = NULL;

    DnDClientMap::iterator itClient =  m_clientMap.find(idClient);
    if (itClient != m_clientMap.end())
    {
        pClient = itClient->second;
        AssertPtr(pClient);
    }
    else
    {
        LogFunc(("Client %RU32 was not found\n", idClient));
        rc = VERR_NOT_FOUND;
    }

/* Verifies that an uint32 parameter has the expected buffer size set.
 * Will set rc to VERR_INVALID_PARAMETER otherwise. See #9777. */
#define VERIFY_BUFFER_SIZE_UINT32(a_ParmUInt32, a_SizeExpected) \
do { \
    uint32_t cbTemp = 0; \
    rc = HGCMSvcGetU32(&a_ParmUInt32, &cbTemp); \
    ASSERT_GUEST_BREAK(RT_SUCCESS(rc) && cbTemp == a_SizeExpected); \
} while (0)

/* Gets the context ID from the first parameter and store it into the data header.
 * Then increments idxParm by one if more than one parameter is available. */
#define GET_CONTEXT_ID_PARM0() \
    if (fHasCtxID) \
    { \
        ASSERT_GUEST_BREAK(cParms >= 1); \
        rc = HGCMSvcGetU32(&paParms[0], &data.hdr.uContextID); \
        ASSERT_GUEST_BREAK(RT_SUCCESS(rc)); \
        if (cParms > 1) \
            idxParm++; \
    }

    if (rc == VINF_SUCCESS) /* Note: rc might be VINF_HGCM_ASYNC_EXECUTE! */
    {
        rc = VERR_INVALID_PARAMETER; /* Play safe by default. */

        /* Whether the client's advertised protocol sends context IDs with commands. */
        const bool fHasCtxID = pClient->uProtocolVerDeprecated >= 3;

        /* Current parameter index to process. */
        unsigned idxParm = 0;

        switch (u32Function)
        {
            /*
             * Note: Older VBox versions with enabled DnD guest->host support (< 5.0)
             *       used the same message ID (300) for GUEST_DND_FN_GET_NEXT_HOST_MSG and
             *       HOST_DND_FN_GH_REQ_PENDING, which led this service returning
             *       VERR_INVALID_PARAMETER when the guest wanted to actually
             *       handle HOST_DND_FN_GH_REQ_PENDING.
             */
            case GUEST_DND_FN_GET_NEXT_HOST_MSG:
            {
                if (cParms == 3)
                {
                    /* Make sure to increase the reference count so that the next message doesn't get removed between
                     * the guest's GUEST_DND_FN_GET_NEXT_HOST_MSG call and the actual message retrieval call. */
                    rc = m_pManager->GetNextMsgInfo(true /* fAddRef */,
                                                    &paParms[0].u.uint32 /* uMsg */, &paParms[1].u.uint32 /* cParms */);
                    if (RT_FAILURE(rc)) /* No queued messages available? */
                    {
                        if (m_SvcCtx.pfnHostCallback) /* Try asking the host. */
                        {
                            VBOXDNDCBHGGETNEXTHOSTMSG data;
                            RT_ZERO(data);
                            data.hdr.uMagic = CB_MAGIC_DND_HG_GET_NEXT_HOST_MSG;
                            rc = m_SvcCtx.pfnHostCallback(m_SvcCtx.pvHostData, u32Function, &data, sizeof(data));
                            if (RT_SUCCESS(rc))
                            {
                                paParms[0].u.uint32 = data.uMsg;   /* uMsg */
                                paParms[1].u.uint32 = data.cParms; /* cParms */
                                /* Note: paParms[2] was set by the guest as blocking flag. */
                            }

                            LogFlowFunc(("Host callback returned %Rrc\n", rc));
                        }
                        else /* No host callback in place, so drag and drop is not supported by the host. */
                            rc = VERR_NOT_SUPPORTED;

                        if (RT_FAILURE(rc))
                            rc = m_pManager->GetNextMsg(u32Function, cParms, paParms);

                        /* Some error occurred or no (new) messages available? */
                        if (RT_FAILURE(rc))
                        {
                            uint32_t fFlags = 0;
                            int rc2 = HGCMSvcGetU32(&paParms[2], &fFlags);
                            if (   RT_SUCCESS(rc2)
                                && fFlags) /* Blocking flag set? */
                            {
                                /* Defer client returning. */
                                rc = VINF_HGCM_ASYNC_EXECUTE;
                            }
                            else
                                rc = VERR_INVALID_PARAMETER;

                            LogFlowFunc(("Message queue is empty, returning %Rrc to guest\n", rc));
                        }
                    }
                }
                break;
            }
            case GUEST_DND_FN_CONNECT:
            {
                ASSERT_GUEST_BREAK(cParms >= 2);

                VBOXDNDCBCONNECTDATA data;
                RT_ZERO(data);
                data.hdr.uMagic = CB_MAGIC_DND_CONNECT;

                rc = HGCMSvcGetU32(&paParms[idxParm++], &data.hdr.uContextID); \
                ASSERT_GUEST_RC_BREAK(rc);
                rc = HGCMSvcGetU32(&paParms[idxParm++], &data.uProtocolVersion);
                ASSERT_GUEST_RC_BREAK(rc);
                rc = HGCMSvcGetU32(&paParms[idxParm], &data.fFlags);
                ASSERT_GUEST_RC_BREAK(rc);

                unsigned uProtocolVer = 3; /* The protocol version we're going to use. */

                /* Make sure we're only setting a protocl version we're supporting on the host. */
                if (data.uProtocolVersion > uProtocolVer)
                    data.uProtocolVersion = uProtocolVer;

                pClient->uProtocolVerDeprecated = data.uProtocolVersion;

                /* Return the highest protocol version we're supporting. */
                AssertBreak(idxParm);
                ASSERT_GUEST_BREAK(idxParm);
                paParms[idxParm - 1].u.uint32 = data.uProtocolVersion;

                LogFlowFunc(("Client %RU32 is now using protocol v%RU32\n",
                             pClient->GetClientID(), pClient->uProtocolVerDeprecated));

                DO_HOST_CALLBACK();
                break;
            }
            case GUEST_DND_FN_REPORT_FEATURES:
            {
                rc = clientReportFeatures(pClient, cParms, paParms);
                if (RT_SUCCESS(rc))
                {
                    VBOXDNDCBREPORTFEATURESDATA data;
                    RT_ZERO(data);
                    data.hdr.uMagic = CB_MAGIC_DND_REPORT_FEATURES;

                    data.fGuestFeatures0 = pClient->fGuestFeatures0;
                    /* fGuestFeatures1 is not used yet. */

                    /* Don't touch initial rc. */
                    int rc2 = m_SvcCtx.pfnHostCallback(m_SvcCtx.pvHostData, u32Function, &data, sizeof(data));
                    AssertRC(rc2);
                }
                break;
            }
            case GUEST_DND_FN_QUERY_FEATURES:
            {
                rc = clientQueryFeatures(cParms, paParms);
                break;
            }
            case GUEST_DND_FN_HG_ACK_OP:
            {
                ASSERT_GUEST_BREAK(cParms >= 2);

                VBOXDNDCBHGACKOPDATA data;
                RT_ZERO(data);
                data.hdr.uMagic = CB_MAGIC_DND_HG_ACK_OP;

                GET_CONTEXT_ID_PARM0();
                rc = HGCMSvcGetU32(&paParms[idxParm], &data.uAction); /* Get drop action. */
                ASSERT_GUEST_RC_BREAK(rc);

                DO_HOST_CALLBACK();
                break;
            }
            case GUEST_DND_FN_HG_REQ_DATA:
            {
                VBOXDNDCBHGREQDATADATA data;
                RT_ZERO(data);
                data.hdr.uMagic = CB_MAGIC_DND_HG_REQ_DATA;

                switch (pClient->uProtocolVerDeprecated)
                {
                    case 3:
                    {
                        ASSERT_GUEST_BREAK(cParms == 3);
                        GET_CONTEXT_ID_PARM0();
                        rc = HGCMSvcGetPv(&paParms[idxParm++], (void **)&data.pszFormat, &data.cbFormat);
                        ASSERT_GUEST_RC_BREAK(rc);
                        VERIFY_BUFFER_SIZE_UINT32(paParms[idxParm], data.cbFormat);
                        break;
                    }

                    case 2:
                        RT_FALL_THROUGH();
                    default:
                    {
                        ASSERT_GUEST_BREAK(cParms == 1);
                        rc = HGCMSvcGetPv(&paParms[idxParm], (void**)&data.pszFormat, &data.cbFormat);
                        ASSERT_GUEST_RC_BREAK(rc);
                        break;
                    }
                }

                DO_HOST_CALLBACK();
                break;
            }
            case GUEST_DND_FN_HG_EVT_PROGRESS:
            {
                ASSERT_GUEST_BREAK(cParms >= 3);

                VBOXDNDCBHGEVTPROGRESSDATA data;
                RT_ZERO(data);
                data.hdr.uMagic = CB_MAGIC_DND_HG_EVT_PROGRESS;

                GET_CONTEXT_ID_PARM0();
                rc = HGCMSvcGetU32(&paParms[idxParm++], &data.uStatus);
                ASSERT_GUEST_RC_BREAK(rc);
                rc = HGCMSvcGetU32(&paParms[idxParm++], &data.uPercentage);
                ASSERT_GUEST_RC_BREAK(rc);
                rc = HGCMSvcGetU32(&paParms[idxParm], &data.rc);
                ASSERT_GUEST_RC_BREAK(rc);

                DO_HOST_CALLBACK();
                break;
            }
#ifdef VBOX_WITH_DRAG_AND_DROP_GH
            case GUEST_DND_FN_GH_ACK_PENDING:
            {
                VBOXDNDCBGHACKPENDINGDATA data;
                RT_ZERO(data);
                data.hdr.uMagic = CB_MAGIC_DND_GH_ACK_PENDING;

                switch (pClient->uProtocolVerDeprecated)
                {
                    case 3:
                    {
                        ASSERT_GUEST_BREAK(cParms == 5);
                        GET_CONTEXT_ID_PARM0();
                        rc = HGCMSvcGetU32(&paParms[idxParm++], &data.uDefAction);
                        ASSERT_GUEST_RC_BREAK(rc);
                        rc = HGCMSvcGetU32(&paParms[idxParm++], &data.uAllActions);
                        ASSERT_GUEST_RC_BREAK(rc);
                        rc = HGCMSvcGetPv(&paParms[idxParm++], (void**)&data.pszFormat, &data.cbFormat);
                        ASSERT_GUEST_RC_BREAK(rc);
                        VERIFY_BUFFER_SIZE_UINT32(paParms[idxParm], data.cbFormat);
                        break;
                    }

                    case 2:
                    default:
                    {
                        ASSERT_GUEST_BREAK(cParms == 3);
                        rc = HGCMSvcGetU32(&paParms[idxParm++], &data.uDefAction);
                        ASSERT_GUEST_RC_BREAK(rc);
                        rc = HGCMSvcGetU32(&paParms[idxParm++], &data.uAllActions);
                        ASSERT_GUEST_RC_BREAK(rc);
                        rc = HGCMSvcGetPv(&paParms[idxParm], (void**)&data.pszFormat, &data.cbFormat);
                        ASSERT_GUEST_RC_BREAK(rc);
                        break;
                    }
                }

                DO_HOST_CALLBACK();
                break;
            }
            /* New since protocol v3. */
            case GUEST_DND_FN_GH_SND_DATA_HDR:
            {
                ASSERT_GUEST_BREAK(cParms == 12);

                VBOXDNDCBSNDDATAHDRDATA data;
                RT_ZERO(data);
                data.hdr.uMagic = CB_MAGIC_DND_GH_SND_DATA_HDR;

                GET_CONTEXT_ID_PARM0();
                rc = HGCMSvcGetU32(&paParms[idxParm++], &data.data.uFlags);
                ASSERT_GUEST_RC_BREAK(rc);
                rc = HGCMSvcGetU32(&paParms[idxParm++], &data.data.uScreenId);
                ASSERT_GUEST_RC_BREAK(rc);
                rc = HGCMSvcGetU64(&paParms[idxParm++], &data.data.cbTotal);
                ASSERT_GUEST_RC_BREAK(rc);
                rc = HGCMSvcGetU32(&paParms[idxParm++], &data.data.cbMeta);
                ASSERT_GUEST_RC_BREAK(rc);
                ASSERT_GUEST_BREAK(data.data.cbMeta <= data.data.cbTotal);
                rc = HGCMSvcGetPv(&paParms[idxParm++], &data.data.pvMetaFmt, &data.data.cbMetaFmt);
                ASSERT_GUEST_RC_BREAK(rc);
                VERIFY_BUFFER_SIZE_UINT32(paParms[idxParm++], data.data.cbMetaFmt);
                rc = HGCMSvcGetU64(&paParms[idxParm++], &data.data.cObjects);
                ASSERT_GUEST_RC_BREAK(rc);
                rc = HGCMSvcGetU32(&paParms[idxParm++], &data.data.enmCompression);
                ASSERT_GUEST_RC_BREAK(rc);
                rc = HGCMSvcGetU32(&paParms[idxParm++], (uint32_t *)&data.data.enmChecksumType);
                ASSERT_GUEST_RC_BREAK(rc);
                rc = HGCMSvcGetPv(&paParms[idxParm++], &data.data.pvChecksum, &data.data.cbChecksum);
                ASSERT_GUEST_RC_BREAK(rc);
                VERIFY_BUFFER_SIZE_UINT32(paParms[idxParm], data.data.cbChecksum);

                DO_HOST_CALLBACK();
                break;
            }
            case GUEST_DND_FN_GH_SND_DATA:
            {
                switch (pClient->uProtocolVerDeprecated)
                {
                    case 3:
                    {
                        ASSERT_GUEST_BREAK(cParms == 5);

                        VBOXDNDCBSNDDATADATA data;
                        RT_ZERO(data);
                        data.hdr.uMagic = CB_MAGIC_DND_GH_SND_DATA;

                        GET_CONTEXT_ID_PARM0();
                        rc = HGCMSvcGetPv(&paParms[idxParm++], (void**)&data.data.u.v3.pvData, &data.data.u.v3.cbData);
                        ASSERT_GUEST_RC_BREAK(rc);
                        VERIFY_BUFFER_SIZE_UINT32(paParms[idxParm++], data.data.u.v3.cbData);
                        rc = HGCMSvcGetPv(&paParms[idxParm++], (void**)&data.data.u.v3.pvChecksum, &data.data.u.v3.cbChecksum);
                        ASSERT_GUEST_RC_BREAK(rc);
                        VERIFY_BUFFER_SIZE_UINT32(paParms[idxParm], data.data.u.v3.cbChecksum);

                        DO_HOST_CALLBACK();
                        break;
                    }

                    case 2:
                        RT_FALL_THROUGH();
                    default:
                    {
                        ASSERT_GUEST_BREAK(cParms == 2);

                        VBOXDNDCBSNDDATADATA data;
                        RT_ZERO(data);
                        data.hdr.uMagic = CB_MAGIC_DND_GH_SND_DATA;

                        rc = HGCMSvcGetPv(&paParms[idxParm++], (void**)&data.data.u.v1.pvData, &data.data.u.v1.cbData);
                        ASSERT_GUEST_RC_BREAK(rc);
                        rc = HGCMSvcGetU32(&paParms[idxParm], &data.data.u.v1.cbTotalSize);
                        ASSERT_GUEST_RC_BREAK(rc);

                        DO_HOST_CALLBACK();
                        break;
                    }
                }
                break;
            }
            case GUEST_DND_FN_GH_SND_DIR:
            {
                ASSERT_GUEST_BREAK(cParms >= 3);

                VBOXDNDCBSNDDIRDATA data;
                RT_ZERO(data);
                data.hdr.uMagic = CB_MAGIC_DND_GH_SND_DIR;

                GET_CONTEXT_ID_PARM0();
                rc = HGCMSvcGetPv(&paParms[idxParm++], (void**)&data.pszPath, &data.cbPath);
                ASSERT_GUEST_RC_BREAK(rc);
                VERIFY_BUFFER_SIZE_UINT32(paParms[idxParm++], data.cbPath);
                rc = HGCMSvcGetU32(&paParms[idxParm], &data.fMode);
                ASSERT_GUEST_RC_BREAK(rc);

                DO_HOST_CALLBACK();
                break;
            }
            /* New since protocol v2 (>= VBox 5.0). */
            case GUEST_DND_FN_GH_SND_FILE_HDR:
            {
                ASSERT_GUEST_BREAK(cParms == 6);

                VBOXDNDCBSNDFILEHDRDATA data;
                RT_ZERO(data);
                data.hdr.uMagic = CB_MAGIC_DND_GH_SND_FILE_HDR;

                GET_CONTEXT_ID_PARM0();
                rc = HGCMSvcGetPv(&paParms[idxParm++], (void**)&data.pszFilePath, &data.cbFilePath);
                ASSERT_GUEST_RC_BREAK(rc);
                VERIFY_BUFFER_SIZE_UINT32(paParms[idxParm++], data.cbFilePath);
                rc = HGCMSvcGetU32(&paParms[idxParm++], &data.fFlags);
                ASSERT_GUEST_RC_BREAK(rc);
                rc = HGCMSvcGetU32(&paParms[idxParm++], &data.fMode);
                ASSERT_GUEST_RC_BREAK(rc);
                rc = HGCMSvcGetU64(&paParms[idxParm], &data.cbSize);
                ASSERT_GUEST_RC_BREAK(rc);

                DO_HOST_CALLBACK();
                break;
            }
            case GUEST_DND_FN_GH_SND_FILE_DATA:
            {
                switch (pClient->uProtocolVerDeprecated)
                {
                    /* Protocol v3 adds (optional) checksums. */
                    case 3:
                    {
                        ASSERT_GUEST_BREAK(cParms == 5);

                        VBOXDNDCBSNDFILEDATADATA data;
                        RT_ZERO(data);
                        data.hdr.uMagic = CB_MAGIC_DND_GH_SND_FILE_DATA;

                        GET_CONTEXT_ID_PARM0();
                        rc = HGCMSvcGetPv(&paParms[idxParm++], (void**)&data.pvData, &data.cbData);
                        ASSERT_GUEST_RC_BREAK(rc);
                        VERIFY_BUFFER_SIZE_UINT32(paParms[idxParm++], data.cbData);
                        rc = HGCMSvcGetPv(&paParms[idxParm++], (void**)&data.u.v3.pvChecksum, &data.u.v3.cbChecksum);
                        ASSERT_GUEST_RC_BREAK(rc);
                        VERIFY_BUFFER_SIZE_UINT32(paParms[idxParm], data.u.v3.cbChecksum);

                        DO_HOST_CALLBACK();
                        break;
                    }
                    /* Protocol v2 only sends the next data chunks to reduce traffic. */
                    case 2:
                    {
                        ASSERT_GUEST_BREAK(cParms == 3);

                        VBOXDNDCBSNDFILEDATADATA data;
                        RT_ZERO(data);
                        data.hdr.uMagic = CB_MAGIC_DND_GH_SND_FILE_DATA;

                        GET_CONTEXT_ID_PARM0();
                        rc = HGCMSvcGetPv(&paParms[idxParm++], (void**)&data.pvData, &data.cbData);
                        ASSERT_GUEST_RC_BREAK(rc);
                        VERIFY_BUFFER_SIZE_UINT32(paParms[idxParm], data.cbData);

                        DO_HOST_CALLBACK();
                        break;
                    }
                    /* Protocol v1 sends the file path and attributes for every file chunk (!). */
                    default:
                    {
                        ASSERT_GUEST_BREAK(cParms == 5);

                        VBOXDNDCBSNDFILEDATADATA data;
                        RT_ZERO(data);
                        data.hdr.uMagic = CB_MAGIC_DND_GH_SND_FILE_DATA;

                        rc = HGCMSvcGetPv(&paParms[idxParm++], (void**)&data.u.v1.pszFilePath, &data.u.v1.cbFilePath);
                        ASSERT_GUEST_RC_BREAK(rc);
                        VERIFY_BUFFER_SIZE_UINT32(paParms[idxParm++], data.u.v1.cbFilePath);
                        rc = HGCMSvcGetPv(&paParms[idxParm++], (void**)&data.pvData, &data.cbData);
                        ASSERT_GUEST_RC_BREAK(rc);
                        VERIFY_BUFFER_SIZE_UINT32(paParms[idxParm++], data.cbData);
                        rc = HGCMSvcGetU32(&paParms[idxParm], &data.u.v1.fMode);
                        ASSERT_GUEST_RC_BREAK(rc);

                        DO_HOST_CALLBACK();
                        break;
                    }
                }
                break;
            }
            case GUEST_DND_FN_EVT_ERROR:
            {
                ASSERT_GUEST_BREAK(cParms >= 1);

                VBOXDNDCBEVTERRORDATA data;
                RT_ZERO(data);
                data.hdr.uMagic = CB_MAGIC_DND_EVT_ERROR;

                GET_CONTEXT_ID_PARM0();
                rc = HGCMSvcGetU32(&paParms[idxParm], (uint32_t *)&data.rc);
                ASSERT_GUEST_RC_BREAK(rc);

                DO_HOST_CALLBACK();
                break;
            }
#endif /* VBOX_WITH_DRAG_AND_DROP_GH */

            default:
            {
                LogFlowFunc(("u32Function=%s (%#x), cParms=%RU32\n", DnDHostMsgToStr(u32Function), u32Function, cParms));

                /* All other messages are handled by the DnD manager. */
                rc = m_pManager->GetNextMsg(u32Function, cParms, paParms);
                if (rc == VERR_NO_DATA) /* Manager has no new messsages? Try asking the host. */
                {
                    if (m_SvcCtx.pfnHostCallback)
                    {
                        VBOXDNDCBHGGETNEXTHOSTMSGDATA data;
                        RT_ZERO(data);

                        data.hdr.uMagic = VBOX_DND_CB_MAGIC_MAKE(0 /* uFn */, 0 /* uVer */);

                        data.uMsg    = u32Function;
                        data.cParms  = cParms;
                        data.paParms = paParms;

                        rc = m_SvcCtx.pfnHostCallback(m_SvcCtx.pvHostData, u32Function, &data, sizeof(data));
                        if (RT_SUCCESS(rc))
                        {
                            cParms  = data.cParms;
                            paParms = data.paParms;
                        }
                        else
                        {
                            if (rc == VERR_CANCELLED)
                            {
                                /* Host indicated that the current operation was cancelled. Tell the guest. */
                                LogFunc(("Host indicated that operation was cancelled\n"));
                            }
                            else
                            {
                                /*
                                 * In case the guest is too fast asking for the next message
                                 * and the host did not supply it yet, just defer the client's
                                 * return until a response from the host available.
                                 */
                                LogFunc(("No new messages from the host (%Rrc), deferring request\n", rc));
                                rc = VINF_HGCM_ASYNC_EXECUTE;
                            }
                        }
                    }
                    else /* No host callback in place, so drag and drop is not supported by the host. */
                        rc = VERR_NOT_SUPPORTED;
                }
                break;
            }
        }
    }

#undef VERIFY_BUFFER_SIZE_UINT32

    /*
     * If async execution is requested, we didn't notify the guest yet about
     * completion. The client is queued into the waiters list and will be
     * notified as soon as a new event is available.
     */
    if (rc == VINF_HGCM_ASYNC_EXECUTE)
    {
        LogFunc(("Deferring client %RU32\n", idClient));

        try
        {
            AssertPtr(pClient);
            pClient->SetDeferred(callHandle, u32Function, cParms, paParms);
            m_clientQueue.push_back(idClient);
        }
        catch (std::bad_alloc &)
        {
            rc = VERR_NO_MEMORY;
            /* Don't report to guest. */
        }
    }
    else if (pClient)
    {
        /* Complete the call on the guest side. */
        pClient->Complete(callHandle, rc);
    }
    else
    {
        AssertMsgFailed(("Guest call failed with %Rrc\n", rc));
        rc = VERR_NOT_IMPLEMENTED;
    }

    LogFunc(("Returning %Rrc to guest\n", rc));
}

int DragAndDropService::hostCall(uint32_t u32Function,
                                 uint32_t cParms, VBOXHGCMSVCPARM paParms[]) RT_NOEXCEPT
{
    LogFlowFunc(("u32Function=%s (%#x), cParms=%RU32, cClients=%zu, cQueue=%zu\n",
                 DnDHostMsgToStr(u32Function), u32Function, cParms, m_clientMap.size(), m_clientQueue.size()));

    uint32_t const uMode = modeGet();

    /* Check if we've the right mode set. */
    int rc = VERR_ACCESS_DENIED; /* Play safe. */
    switch (u32Function)
    {
        /*
         * Host -> Guest mode
         */
        case HOST_DND_FN_HG_EVT_ENTER:
            RT_FALL_THROUGH();
        case HOST_DND_FN_HG_EVT_MOVE:
            RT_FALL_THROUGH();
        case HOST_DND_FN_HG_EVT_LEAVE:
            RT_FALL_THROUGH();
        case HOST_DND_FN_HG_EVT_DROPPED:
            RT_FALL_THROUGH();
        case HOST_DND_FN_HG_SND_DATA_HDR:
            RT_FALL_THROUGH();
        case HOST_DND_FN_HG_SND_DATA:
            RT_FALL_THROUGH();
        case HOST_DND_FN_HG_SND_MORE_DATA:
            RT_FALL_THROUGH();
        case HOST_DND_FN_HG_SND_DIR:
            RT_FALL_THROUGH();
        case HOST_DND_FN_HG_SND_FILE_DATA:
            RT_FALL_THROUGH();
        case HOST_DND_FN_HG_SND_FILE_HDR:
        {
            if (   uMode == VBOX_DRAG_AND_DROP_MODE_HOST_TO_GUEST
                || uMode == VBOX_DRAG_AND_DROP_MODE_BIDIRECTIONAL)
                rc = VINF_SUCCESS;
            else
            {
                LogRel2(("DnD: Host to guest mode is disabled, ignoring request from host\n"));
            }
            break;
        }

        /*
         * Guest -> Host mode
         */
        case HOST_DND_FN_GH_REQ_PENDING:
            RT_FALL_THROUGH();
        case HOST_DND_FN_GH_EVT_DROPPED:
        {
            if (   uMode == VBOX_DRAG_AND_DROP_MODE_GUEST_TO_HOST
                || uMode == VBOX_DRAG_AND_DROP_MODE_BIDIRECTIONAL)
                rc = VINF_SUCCESS;
            else
            {
                LogRel2(("DnD: Guest to host mode is disabled, ignoring request from host\n"));
            }
            break;
        }

        /*
         * Both modes
         */
        case HOST_DND_FN_CANCEL:
            if (uMode != VBOX_DRAG_AND_DROP_MODE_OFF)
                rc = VINF_SUCCESS;
            break;

        /*
         * Functions that always work.
         */
        case HOST_DND_FN_SET_MODE:
            rc = VINF_SUCCESS;
            break;

        /*
         * Forbid everything else not explicitly allowed.
         */
        default:
            break;
    }

    if (RT_FAILURE(rc))
        return rc;

    bool fSendToGuest = false; /* Whether to send the message down to the guest side or not. */

    switch (u32Function)
    {
        case HOST_DND_FN_SET_MODE:
        {
            if (cParms != 1)
                rc = VERR_INVALID_PARAMETER;
            else if (paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT)
                rc = VERR_INVALID_PARAMETER;
            else
                rc = modeSet(paParms[0].u.uint32);
            break;
        }

        case HOST_DND_FN_CANCEL:
        {
            LogFlowFunc(("Cancelling all waiting clients ...\n"));

            /* Forcefully reset the message queue, as the host has cancelled the current operation. */
            m_pManager->Reset(true /* fForce */);

            /*
             * Wake up all deferred clients and tell them to process
             * the cancelling message next.
             */
            DnDClientQueue::iterator itQueue = m_clientQueue.begin();
            while (itQueue != m_clientQueue.end())
            {
                DnDClientMap::iterator itClient = m_clientMap.find(*itQueue);
                Assert(itClient != m_clientMap.end());

                DragAndDropClient *pClient = itClient->second;
                AssertPtr(pClient);

                int rc2 = pClient->SetDeferredMsgInfo(HOST_DND_FN_CANCEL,
                                                      /* Protocol v3+ also contains the context ID. */
                                                      pClient->uProtocolVerDeprecated >= 3 ? 1 : 0);
                AssertRC(rc2);

                /* Return VERR_CANCELLED when waking up the guest side. */
                pClient->CompleteDeferred(VERR_CANCELLED);

                m_clientQueue.erase(itQueue);
                itQueue = m_clientQueue.begin();
            }

            Assert(m_clientQueue.empty());

            /* Tell the host that everything went well. */
            rc = VINF_SUCCESS;
            break;
        }

        case HOST_DND_FN_HG_EVT_ENTER:
        {
            /* Reset the message queue as a new DnD operation just began. */
            m_pManager->Reset(false /* fForce */);

            fSendToGuest = true;
            rc = VINF_SUCCESS;
            break;
        }

        default:
        {
            fSendToGuest = true;
            rc = VINF_SUCCESS;
            break;
        }
    }

    do /* goto avoidance break-loop. */
    {
        if (fSendToGuest)
        {
            if (m_clientMap.empty()) /* At least one client on the guest connected? */
            {
                /*
                 * Tell the host that the guest does not support drag'n drop.
                 * This might happen due to not installed Guest Additions or
                 * not running VBoxTray/VBoxClient.
                 */
                rc = VERR_NOT_SUPPORTED;
                break;
            }

            rc = m_pManager->AddMsg(u32Function, cParms, paParms, true /* fAppend */);
            if (RT_FAILURE(rc))
            {
                AssertMsgFailed(("Adding new message of type=%RU32 failed with rc=%Rrc\n", u32Function, rc));
                break;
            }

            /* Any clients in our queue ready for processing the next command? */
            if (m_clientQueue.empty())
            {
                LogFlowFunc(("All clients (%zu) busy -- delaying execution\n", m_clientMap.size()));
                break;
            }

            uint32_t uClientNext = m_clientQueue.front();
            DnDClientMap::iterator itClientNext = m_clientMap.find(uClientNext);
            Assert(itClientNext != m_clientMap.end());

            DragAndDropClient *pClient = itClientNext->second;
            AssertPtr(pClient);

            /*
             * Check if this was a request for getting the next host
             * message. If so, return the message ID and the parameter
             * count. The message itself has to be queued.
             */
            uint32_t uMsgClient = pClient->GetMsgType();

            uint32_t uMsgNext   = 0;
            uint32_t cParmsNext = 0;
            /* Note: We only want to peek for the next message, hence fAddRef is false. */
            int rcNext = m_pManager->GetNextMsgInfo(false /* fAddRef */, &uMsgNext, &cParmsNext);

            LogFlowFunc(("uMsgClient=%s (%#x), uMsgNext=%s (%#x), cParmsNext=%RU32, rcNext=%Rrc\n",
                         DnDGuestMsgToStr(uMsgClient), uMsgClient, DnDHostMsgToStr(uMsgNext), uMsgNext, cParmsNext, rcNext));

            if (RT_SUCCESS(rcNext))
            {
                if (uMsgClient == GUEST_DND_FN_GET_NEXT_HOST_MSG)
                {
                    rc = pClient->SetDeferredMsgInfo(uMsgNext, cParmsNext);

                    /* Note: Report the current rc back to the guest. */
                    pClient->CompleteDeferred(rc);
                }
                /*
                 * Does the message the client is waiting for match the message
                 * next in the queue? Process it right away then.
                 */
                else if (uMsgClient == uMsgNext)
                {
                    rc = m_pManager->GetNextMsg(u32Function, cParms, paParms);

                    /* Note: Report the current rc back to the guest. */
                    pClient->CompleteDeferred(rc);
                }
                else /* Should not happen; cancel the operation on the guest. */
                {
                    LogFunc(("Client ID=%RU32 in wrong state with uMsg=%RU32 (next message in queue: %RU32), cancelling\n",
                             pClient->GetClientID(), uMsgClient, uMsgNext));

                    pClient->CompleteDeferred(VERR_CANCELLED);
                }

                m_clientQueue.pop_front();
            }

        } /* fSendToGuest */

    } while (0); /* To use breaks. */

    LogFlowFuncLeaveRC(rc);
    return rc;
}

DECLCALLBACK(int) DragAndDropService::progressCallback(uint32_t uStatus, uint32_t uPercentage, int rc, void *pvUser)
{
    AssertPtrReturn(pvUser, VERR_INVALID_POINTER);

    DragAndDropService *pSelf = static_cast<DragAndDropService *>(pvUser);
    AssertPtr(pSelf);

    if (pSelf->m_SvcCtx.pfnHostCallback)
    {
        LogFlowFunc(("GUEST_DND_FN_HG_EVT_PROGRESS: uStatus=%RU32, uPercentage=%RU32, rc=%Rrc\n",
                     uStatus, uPercentage, rc));

        VBOXDNDCBHGEVTPROGRESSDATA data;
        data.hdr.uMagic = CB_MAGIC_DND_HG_EVT_PROGRESS;
        data.uPercentage  = RT_MIN(uPercentage, 100);
        data.uStatus      = uStatus;
        data.rc           = rc; /** @todo uin32_t vs. int. */

        return pSelf->m_SvcCtx.pfnHostCallback(pSelf->m_SvcCtx.pvHostData,
                                               GUEST_DND_FN_HG_EVT_PROGRESS,
                                               &data, sizeof(data));
    }

    return VINF_SUCCESS;
}

/**
 * @copydoc FNVBOXHGCMSVCLOAD
 */
extern "C" DECLCALLBACK(DECLEXPORT(int)) VBoxHGCMSvcLoad(VBOXHGCMSVCFNTABLE *pTable)
{
    return DragAndDropService::svcLoad(pTable);
}

