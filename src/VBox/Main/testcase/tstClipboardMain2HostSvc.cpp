/* $Id: tstClipboardMain2HostSvc.cpp 115102 2026-08-21 11:14:19Z andreas.loeffler@oracle.com $ */
/** @file
 * Main Shared Clipboard - Host Service integration testcase.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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
#include "GuestShClBackendPrivate.h"
#include "GuestShClConn.h"

#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/VMMDev.h>

#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/thread.h>


/**
 * @page pg_tstClipboardMain2HostSvc Main to Shared Clipboard Host Service integration testcase
 *
 * Links the production HGCM service to Main's production connection, backend
 * dispatcher.  A small adapter replaces only Console-facing notifications,
 * while a fake backend replaces the native operating-system clipboard.
 *
 * The test covers registration, connect/sync/disconnect, the real service
 * transport, messages in both directions, guest-data reply ownership and one
 * transfer handshake, including post-initialization error propagation.  VM
 * construction, public Main API objects, native clipboard contents and transfer
 * data providers are outside its scope.
 */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** State attached to a synthetic HGCM call. */
struct VBOXHGCMCALLHANDLE_TYPEDEF
{
    /** Whether the Host Service completed the call. */
    bool                        fCompleted;
    /** Completion status supplied by the Host Service. */
    int32_t                     rc;
};

/** Fake native backend context. */
struct SHCLCONTEXT
{
    /** Main connection owning the context. */
    GuestShClConn              *pConn;
};

/** Integration-test state. */
typedef struct TSTSHCLSTATE
{
    /** Whether the Host Service was loaded successfully. */
    bool                        fServiceLoaded;
    /** Whether Main's test extension was registered successfully. */
    bool                        fExtensionRegistered;
    /** Fake native backend context. */
    SHCLCONTEXT                 BackendCtx;
    /** Number of native backend initializations. */
    uint32_t                    cBackendInit;
    /** Number of native backend destructions. */
    uint32_t                    cBackendDestroy;
    /** Number of native backend connections. */
    uint32_t                    cBackendConnect;
    /** Number of native backend disconnections. */
    uint32_t                    cBackendDisconnect;
    /** Number of native backend synchronizations. */
    uint32_t                    cBackendSync;
    /** Number of guest format notifications crossing into Main. */
    uint32_t                    cGuestFormats;
    /** Last guest format mask received by Main. */
    SHCLFORMATS                 fGuestFormats;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Number of transfer callback-table requests reaching the backend. */
    uint32_t                    cTransferCallbacks;
    /** Number of transfer status notifications reaching the backend. */
    volatile uint32_t           cTransferStatuses;
    /** Session of the last transfer notification. */
    SHCLSESSIONID               idTransferSession;
    /** ID of the last transfer notification. */
    SHCLTRANSFERID              idTransfer;
    /** Generation of the last transfer notification. */
    SHCLTRANSFERGEN             uTransferGeneration;
    /** Last transfer status. */
    SHCLTRANSFERSTATUS          enmTransferStatus;
#endif
} TSTSHCLSTATE;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** State recorded while checking the post-initialization callback contract. */
typedef struct TSTTRANSFERINITIALIZED
{
    /** Result returned by the post-initialization callback. */
    int                         vrcCallback;
    /** Number of post-initialization callback invocations. */
    uint32_t                    cInitialized;
    /** Number of destruction callback invocations. */
    uint32_t                    cDestroyed;
    /** Transfer status observed by the post-initialization callback. */
    SHCLTRANSFERSTATUS          enmStatus;
    /** Whether the transfer lock was owned by the callback thread. */
    bool                        fLockOwned;
} TSTTRANSFERINITIALIZED;
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Test framework handle. */
static RTTEST                   g_hTest;
/** Loaded HGCM service table. */
static VBOXHGCMSVCFNTABLE       g_Table;
/** Helpers supplied to the HGCM service. */
static VBOXHGCMSVCHELPERS       g_Helpers;
/** Shared integration state. */
static TSTSHCLSTATE             g_State;


/*********************************************************************************************************************************
*   External Symbols                                                                                                             *
*********************************************************************************************************************************/
extern "C" DECLCALLBACK(DECLEXPORT(int)) VBoxHGCMSvcLoad(VBOXHGCMSVCFNTABLE *pTable);


/*********************************************************************************************************************************
*   Fake Native Backend                                                                                                          *
*********************************************************************************************************************************/
/** @copydoc SHCLBACKENDOPS::pfnInit */
static int tstBackendInit(void)
{
    g_State.cBackendInit++;
    return VINF_SUCCESS;
}

/** @copydoc SHCLBACKENDOPS::pfnDestroy */
static void tstBackendDestroy(void)
{
    g_State.cBackendDestroy++;
}

/** @copydoc SHCLBACKENDOPS::pfnSetCallbacks */
static void tstBackendSetCallbacks(PSHCLCALLBACKS pCallbacks)
{
    RT_NOREF(pCallbacks);
}

/** @copydoc SHCLBACKENDOPS::pfnConnect */
static int tstBackendConnect(GuestShClConn *pConn, PSHCLCONTEXT *ppCtx)
{
    g_State.cBackendConnect++;
    g_State.BackendCtx.pConn = pConn;
    *ppCtx = &g_State.BackendCtx;
    return VINF_SUCCESS;
}

/** @copydoc SHCLBACKENDOPS::pfnDisconnect */
static int tstBackendDisconnect(PSHCLCONTEXT pCtx)
{
    RTTESTI_CHECK(pCtx == &g_State.BackendCtx);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    pCtx->pConn->transferDestroyAll();
#endif
    g_State.cBackendDisconnect++;
    g_State.BackendCtx.pConn = NULL;
    return VINF_SUCCESS;
}

/** @copydoc SHCLBACKENDOPS::pfnReportFormats */
static int tstBackendReportFormats(PSHCLCONTEXT pCtx, SHCLFORMATS fFormats)
{
    RTTESTI_CHECK(pCtx == &g_State.BackendCtx);
    g_State.cGuestFormats++;
    g_State.fGuestFormats = fFormats;
    return VINF_SUCCESS;
}

/** @copydoc SHCLBACKENDOPS::pfnReadData */
static int tstBackendReadData(PSHCLCONTEXT pCtx, SHCLFORMAT uFormat, void *pvData, uint32_t cbData,
                              uint32_t *pcbActual)
{
    RT_NOREF(pCtx, uFormat, pvData, cbData);
    *pcbActual = 0;
    return VERR_NO_DATA;
}

/** @copydoc SHCLBACKENDOPS::pfnWriteData */
static int tstBackendWriteData(PSHCLCONTEXT pCtx, SHCLFORMAT uFormat, void *pvData, uint32_t cbData)
{
    RT_NOREF(pCtx, uFormat, pvData, cbData);
    return VINF_SUCCESS;
}

/** @copydoc SHCLBACKENDOPS::pfnSync */
static int tstBackendSync(PSHCLCONTEXT pCtx)
{
    RTTESTI_CHECK(pCtx == &g_State.BackendCtx);
    g_State.cBackendSync++;
    return VINF_SUCCESS;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** Records and returns the configured post-initialization result. */
static DECLCALLBACK(int) tstTransferInitializedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    TSTTRANSFERINITIALIZED *pState = (TSTTRANSFERINITIALIZED *)pCbCtx->pvUser;
    RTTESTI_CHECK_RET(pState != NULL, VERR_INVALID_POINTER);
    RTTESTI_CHECK_RET(pCbCtx->cbUser == sizeof(*pState), VERR_INVALID_PARAMETER);

    pState->cInitialized++;
    pState->fLockOwned = RTCritSectIsOwner(&pCbCtx->pTransfer->CritSect);
    pState->enmStatus  = ShClTransferGetStatus(pCbCtx->pTransfer);
    return pState->vrcCallback;
}

/** Records destruction of the transfer used by the callback contract test. */
static DECLCALLBACK(void) tstTransferDestroyedCallback(PSHCLTRANSFERCALLBACKCTX pCbCtx)
{
    TSTTRANSFERINITIALIZED *pState = (TSTTRANSFERINITIALIZED *)pCbCtx->pvUser;
    RTTESTI_CHECK(pState != NULL);
    if (pState)
        pState->cDestroyed++;
}

/** @copydoc SHCLBACKENDOPS::pfnTransferGetCallbacks */
static void tstBackendTransferGetCallbacks(PSHCLCONTEXT pCtx, PSHCLTRANSFERCALLBACKS pCallbacks)
{
    RTTESTI_CHECK(pCtx == &g_State.BackendCtx);
    g_State.cTransferCallbacks++;
    RT_ZERO(*pCallbacks);
}

/** @copydoc SHCLBACKENDOPS::pfnTransferHandleStatusReply */
static int tstBackendTransferHandleStatusReply(PSHCLCONTEXT pCtx, PSHCLTRANSFER pTransfer,
                                                SHCLSOURCE enmSource, SHCLTRANSFERSTATUS enmStatus, int rcStatus)
{
    RT_NOREF(enmSource, rcStatus);
    RTTESTI_CHECK(pCtx == &g_State.BackendCtx);
    g_State.idTransferSession = ShClTransferGetSessionId(pTransfer);
    g_State.idTransfer = ShClTransferGetID(pTransfer);
    g_State.uTransferGeneration = ShClTransferGetGeneration(pTransfer);
    g_State.enmTransferStatus = enmStatus;
    ASMAtomicIncU32(&g_State.cTransferStatuses);
    return VINF_SUCCESS;
}
#endif

/** Backend operation table selected by Main's backend dispatcher. */
static SHCLBACKENDOPS const g_BackendOps =
{
    tstBackendInit,
    tstBackendDestroy,
    tstBackendSetCallbacks,
    tstBackendConnect,
    tstBackendDisconnect,
    tstBackendReportFormats,
    tstBackendReadData,
    tstBackendWriteData,
    tstBackendSync,
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    tstBackendTransferGetCallbacks,
    tstBackendTransferHandleStatusReply,
#endif
};

/**
 * Returns the fake native backend to production Main code.
 *
 * @returns Immutable fake backend operation table.
 */
PCSHCLBACKENDOPS ShClBackendGetOps(void)
{
    return &g_BackendOps;
}


/*********************************************************************************************************************************
*   Main Service Extension Adapter                                                                                               *
*********************************************************************************************************************************/
/**
 * Dispatches the connection-owned portion of Main's service extension.
 *
 * Console-facing notifications are reduced to deterministic sinks. Connection
 * ownership and backend dispatch use production Main code; all parameters here
 * are produced by the real Host Service.
 *
 * @returns VBox status code.
 * @param   pvExtension        GuestShClConn instance receiving the request.
 * @param   uFunction          VBOX_CLIPBOARD_EXT_FN_XXX function number.
 * @param   pvParms            Service-extension parameters.
 * @param   cbParms            Size of @a pvParms in bytes.
 */
static DECLCALLBACK(int) tstMainExtension(void *pvExtension, uint32_t uFunction, void *pvParms, uint32_t cbParms)
{
    GuestShClConn * const pConn = (GuestShClConn *)pvExtension;
    RTTESTI_CHECK_RET(cbParms == sizeof(SHCLEXTPARMS), VERR_INVALID_PARAMETER);
    PSHCLEXTPARMS const pParms = (PSHCLEXTPARMS)pvParms;
    int vrc;
    switch (uFunction)
    {
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT:
            return pConn->initBackend();
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY:
            return pConn->destroyBackend();
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT:
        {
            SHCLTRANSPORT const Transport = ShClSvcExtGetTransport(pParms);
            return pConn->connect(&Transport);
        }
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT:
        {
            SHCLTRANSPORT const Transport = ShClSvcExtGetTransport(pParms);
            return pConn->disconnect(&Transport);
        }
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC:
            return pConn->syncBackend();
        case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST:
            return pConn->reportFormatsToBackend(pParms->u.ReportFormats.uFormats);
        case VBOX_CLIPBOARD_EXT_FN_DATA_WRITE:
        {
            SHCLGUESTDATATOKEN hToken = NULL;
            vrc = pConn->guestDataBegin(pParms->u.ReadWriteData.pCmdCtx,
                                        pParms->u.ReadWriteData.uFormat, &hToken);
            if (RT_SUCCESS(vrc))
                vrc = pConn->guestDataComplete(hToken, pParms->u.ReadWriteData.pvData,
                                               pParms->u.ReadWriteData.cbData);
            return vrc;
        }
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        case VBOX_CLIPBOARD_EXT_FN_TRANSFER_CALLBACKS:
            return pConn->transferGetCallbacks(pParms->u.TransferCallbacks.pCallbacks);
        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER:
        {
            PSHCLTRANSFER const pTransfer
                = pConn->transferGetByKeyRetained(pParms->u.FileTransferData.idSession,
                                                   pParms->u.FileTransferData.idTransfer,
                                                   pParms->u.FileTransferData.uGeneration);
            if (!pTransfer)
                return ShClTransferStatusIsTerminal(pParms->u.FileTransferData.enmStatus)
                     ? VINF_SUCCESS : VERR_INVALID_CONTEXT;
            int const vrc2 = pConn->transferHandleStatusReply(pTransfer,
                                                               pParms->u.FileTransferData.enmReplySource,
                                                               pParms->u.FileTransferData.enmStatus,
                                                               pParms->u.FileTransferData.rcStatus);
            ShClTransferRelease(pTransfer);
            return vrc2;
        }
#endif
        default:
            return VERR_NOT_SUPPORTED;
    }
}


/*********************************************************************************************************************************
*   Test Helpers                                                                                                                 *
*********************************************************************************************************************************/
/**
 * Completes one synthetic guest call.
 *
 * @returns VINF_SUCCESS.
 * @param   hCall               Synthetic call handle to complete.
 * @param   rc                  Guest-call result.
 */
static DECLCALLBACK(int) tstCallComplete(VBOXHGCMCALLHANDLE hCall, int32_t rc)
{
    hCall->fCompleted = true;
    hCall->rc = rc;
    return VINF_SUCCESS;
}

/**
 * Executes a guest call expected to complete synchronously.
 *
 * @returns Guest-call result.
 * @param   pvClient            HGCM client state.
 * @param   uFunction          Guest function number.
 * @param   cParms              Number of HGCM parameters.
 * @param   paParms             HGCM parameters.  Optional if @a cParms is zero.
 */
static int tstGuestCall(void *pvClient, uint32_t uFunction, uint32_t cParms, VBOXHGCMSVCPARM *paParms)
{
    VBOXHGCMCALLHANDLE_TYPEDEF Call;
    Call.fCompleted = false;
    Call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    g_Table.pfnCall(g_Table.pvService, &Call, 1 /* idClient */, pvClient,
                    uFunction, cParms, paParms, 0 /* tsArrival */);
    RTTESTI_CHECK_MSG_RET(Call.fCompleted, ("Guest function %RU32 did not complete\n", uFunction),
                          VERR_INTERNAL_ERROR);
    return Call.rc;
}

/**
 * Loads and registers the real Host Service with Main's test adapter.
 *
 * @returns VBox status code.
 * @param   pConn               Main connection registered as the extension target.
 */
static int tstLoad(GuestShClConn *pConn)
{
    RT_ZERO(g_Table);
    RT_ZERO(g_Helpers);
    g_Helpers.pfnCallComplete = tstCallComplete;
    g_Table.cbSize = sizeof(g_Table);
    g_Table.u32Version = VBOX_HGCM_SVC_VERSION;
    g_Table.pHelpers = &g_Helpers;

    int vrc = VBoxHGCMSvcLoad(&g_Table);
    if (RT_SUCCESS(vrc))
    {
        g_State.fServiceLoaded = true;
        vrc = g_Table.pfnRegisterExtension(g_Table.pvService, tstMainExtension, pConn);
        if (RT_SUCCESS(vrc))
            g_State.fExtensionRegistered = true;
    }
    return vrc;
}

/**
 * Enables bidirectional clipboard traffic and reports modern guest features.
 *
 * @param   pvClient            Connected HGCM client state.
 */
static void tstNegotiate(void *pvClient)
{
    VBOXHGCMSVCPARM Parm;
    HGCMSvcSetU32(&Parm, VBOX_SHCL_MODE_BIDIRECTIONAL);
    int vrc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_MODE, 1, &Parm);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    HGCMSvcSetU32(&Parm, VBOX_SHCL_TRANSFER_MODE_F_ENABLED);
    vrc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, &Parm);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
#endif

    VBOXHGCMSVCPARM aFeatures[2];
    HGCMSvcSetU64(&aFeatures[0], VBOX_SHCL_GF_0_CONTEXT_ID
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                                  | VBOX_SHCL_GF_0_TRANSFERS
#endif
                  );
    HGCMSvcSetU64(&aFeatures[1], VBOX_SHCL_GF_1_MUST_BE_ONE);
    vrc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FEATURES, RT_ELEMENTS(aFeatures), aFeatures);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RTTESTI_CHECK(g_State.cBackendSync == 1);

    HGCMSvcSetU64(&aFeatures[0], VBOX_SHCL_GF_0_CONTEXT_ID | VBOX_SHCL_GF_0_TRANSFERS);
    HGCMSvcSetU64(&aFeatures[1], VBOX_SHCL_GF_1_MUST_BE_ONE);
    vrc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FEATURES, RT_ELEMENTS(aFeatures), aFeatures);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
    RTTESTI_CHECK(g_State.cBackendSync == 1);
#endif
}


/*********************************************************************************************************************************
*   Test Cases                                                                                                                   *
*********************************************************************************************************************************/
/**
 * Checks service registration and the complete connection lifecycle.
 *
 * @param   pConn               Main connection used by the extension adapter.
 * @param   ppvClient           Where to return the connected HGCM client state.
 */
static void tstLifecycle(GuestShClConn *pConn, void **ppvClient)
{
    RTTestISub("Service and Main lifecycle");
    int vrc = tstLoad(pConn);
    RTTESTI_CHECK_RC_RETV(vrc, VINF_SUCCESS);

    void *pvClient = RTMemAllocZ(g_Table.cbClient);
    RTTESTI_CHECK_RETV(pvClient != NULL);
    vrc = g_Table.pfnConnect(g_Table.pvService, 1, pvClient, 0 /* fRequestor */, false /* fRestoring */);
    if (RT_FAILURE(vrc))
    {
        RTTestIFailed("Connecting the HGCM client failed: %Rrc", vrc);
        RTMemFree(pvClient);
        return;
    }

    RTTESTI_CHECK(pConn->isConnected());
    RTTESTI_CHECK(g_State.cBackendInit == 1);
    RTTESTI_CHECK(g_State.cBackendConnect == 1);
    RTTESTI_CHECK(g_State.cBackendSync == 1);
    *ppvClient = pvClient;
}

/**
 * Checks messages flowing from Main through the real Host Service to the guest.
 *
 * @param   pvClient            Connected HGCM client state.
 * @param   pConn               Connected Main service connection.
 */
static void tstFormats(void *pvClient, GuestShClConn *pConn)
{
    RTTestISub("Format messages in both directions");
    SHCLFORMATS fReported = VBOX_SHCL_FMT_NONE;
    int vrc = pConn->reportFormatsToGuest(VBOX_SHCL_FMT_UNICODETEXT, &fReported);
    RTTESTI_CHECK_RC_OK(vrc);
    RTTESTI_CHECK(fReported == VBOX_SHCL_FMT_UNICODETEXT);

    VBOXHGCMSVCPARM aGet[2];
    HGCMSvcSetU32(&aGet[0], VBOX_SHCL_HOST_MSG_FORMATS_REPORT);
    HGCMSvcSetU32(&aGet[1], 0);
    vrc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_GET, RT_ELEMENTS(aGet), aGet);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
    RTTESTI_CHECK(aGet[1].u.uint32 == VBOX_SHCL_FMT_UNICODETEXT);

    VBOXHGCMSVCPARM Parm;
    HGCMSvcSetU32(&Parm, VBOX_SHCL_FMT_HTML);
    vrc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FORMATS, 1, &Parm);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
    RTTESTI_CHECK(g_State.cGuestFormats == 1);
    RTTESTI_CHECK(g_State.fGuestFormats == VBOX_SHCL_FMT_HTML);
}

/**
 * Checks a service-owned reply context across the real extension boundary.
 *
 * @param   pvClient            Connected HGCM client state.
 * @param   pConn               Connected Main service connection.
 */
static void tstGuestData(void *pvClient, GuestShClConn *pConn)
{
    RTTestISub("Guest data reply ownership");
    PSHCLEVENT pEvent = NULL;
    int vrc = pConn->readDataFromGuestAsync(VBOX_SHCL_FMT_UNICODETEXT, &pEvent);
    RTTESTI_CHECK_RC_RETV(vrc, VINF_SUCCESS);
    RTTESTI_CHECK_RETV(pEvent != NULL);

    VBOXHGCMSVCPARM aGet[2];
    HGCMSvcSetU64(&aGet[0], VBOX_SHCL_HOST_MSG_READ_DATA_CID);
    HGCMSvcSetU32(&aGet[1], 0);
    vrc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_GET, RT_ELEMENTS(aGet), aGet);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
    uint64_t const uContext = aGet[0].u.uint64;
    RTTESTI_CHECK(aGet[1].u.uint32 == VBOX_SHCL_FMT_UNICODETEXT);

    static uint8_t const s_abData[] = { 'm', 'a', 'i', 'n', '\0' };
    VBOXHGCMSVCPARM aWrite[VBOX_SHCL_CPARMS_DATA_WRITE];
    HGCMSvcSetU64(&aWrite[0], uContext);
    HGCMSvcSetU32(&aWrite[1], VBOX_SHCL_FMT_UNICODETEXT);
    HGCMSvcSetPv(&aWrite[2], (void *)s_abData, sizeof(s_abData));
    vrc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_DATA_WRITE, RT_ELEMENTS(aWrite), aWrite);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);

    PSHCLEVENTPAYLOAD pPayload = NULL;
    vrc = ShClEventWait(pEvent, RT_MS_1SEC, &pPayload);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
    if (pPayload)
    {
        RTTESTI_CHECK(pPayload->cbData == sizeof(s_abData));
        RTTESTI_CHECK(memcmp(pPayload->pvData, s_abData, sizeof(s_abData)) == 0);
        ShClPayloadDestroy(pPayload);
    }
    RTTESTI_CHECK(ShClEventRelease(pEvent) == 0);
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** Waits for the asynchronous service-extension transfer-status worker. */
static bool tstTransferStatusWait(uint32_t cExpected)
{
    for (uint32_t i = 0; i < 5000; i++)
    {
        if (ASMAtomicReadU32(&g_State.cTransferStatuses) >= cExpected)
            return true;
        RTThreadSleep(1);
    }
    return false;
}


/**
 * Checks post-initialization callback failure propagation across Main and the Host Service.
 *
 * @param   pvClient            Connected HGCM client state.
 * @param   pConn               Connected Main service connection.
 */
static void tstTransferInitializedResult(void *pvClient, GuestShClConn *pConn)
{
    RTTestISub("Transfer initialized callback result");

    TSTTRANSFERINITIALIZED State;
    RT_ZERO(State);
    State.vrcCallback = VERR_SHCLPB_NO_DATA;

    SHCLTRANSFERCALLBACKS Callbacks;
    RT_ZERO(Callbacks);
    Callbacks.pfnOnInitialized = tstTransferInitializedCallback;
    Callbacks.pfnOnDestroy     = tstTransferDestroyedCallback;
    Callbacks.pvUser           = &State;
    Callbacks.cbUser           = sizeof(State);

    PSHCLTRANSFER pTransfer = NULL;
    int vrc = pConn->transferCreate(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE, &Callbacks,
                                    NIL_SHCLTRANSFERID, &pTransfer);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
    if (RT_FAILURE(vrc))
        return;

    SHCLSESSIONID const idSession  = ShClTransferGetSessionId(pTransfer);
    SHCLTRANSFERID const idTransfer = ShClTransferGetID(pTransfer);

    vrc = pConn->transferInit(pTransfer);
    RTTESTI_CHECK_RC(vrc, State.vrcCallback);
    RTTESTI_CHECK(State.cInitialized == 1);
    RTTESTI_CHECK(State.enmStatus == SHCLTRANSFERSTATUS_INITIALIZED);
    RTTESTI_CHECK(!State.fLockOwned);

    VBOXHGCMSVCPARM aStatus[VBOX_SHCL_CPARMS_TRANSFER_STATUS];
    HGCMSvcSetU64(&aStatus[0], VBOX_SHCL_HOST_MSG_TRANSFER_STATUS);
    HGCMSvcSetU32(&aStatus[1], 0);
    HGCMSvcSetU32(&aStatus[2], 0);
    HGCMSvcSetU32(&aStatus[3], 0);
    HGCMSvcSetU32(&aStatus[4], 0);
    vrc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_GET, RT_ELEMENTS(aStatus), aStatus);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
    RTTESTI_CHECK(VBOX_SHCL_CONTEXTID_GET_SESSION(aStatus[0].u.uint64) == idSession);
    RTTESTI_CHECK(VBOX_SHCL_CONTEXTID_GET_TRANSFER(aStatus[0].u.uint64) == idTransfer);
    RTTESTI_CHECK(aStatus[1].u.uint32 == SHCLTRANSFERDIR_GUEST_TO_HOST);
    RTTESTI_CHECK(aStatus[2].u.uint32 == SHCLTRANSFERSTATUS_ERROR);
    RTTESTI_CHECK((int32_t)aStatus[3].u.uint32 == State.vrcCallback);

    ShClTransferRelease(pTransfer);
    pConn->transferDestroyById(idTransfer);
    RTTESTI_CHECK(State.cDestroyed == 1);

    HGCMSvcSetU64(&aStatus[0], VBOX_SHCL_HOST_MSG_TRANSFER_STATUS);
    HGCMSvcSetU32(&aStatus[1], 0);
    HGCMSvcSetU32(&aStatus[2], 0);
    HGCMSvcSetU32(&aStatus[3], 0);
    HGCMSvcSetU32(&aStatus[4], 0);
    vrc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_GET, RT_ELEMENTS(aStatus), aStatus);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
    RTTESTI_CHECK(VBOX_SHCL_CONTEXTID_GET_SESSION(aStatus[0].u.uint64) == idSession);
    RTTESTI_CHECK(VBOX_SHCL_CONTEXTID_GET_TRANSFER(aStatus[0].u.uint64) == idTransfer);
    RTTESTI_CHECK(aStatus[2].u.uint32 == SHCLTRANSFERSTATUS_UNINITIALIZED);
}


/**
 * Checks one guest-requested transfer reaching Main and the native backend.
 *
 * @param   pvClient            Connected HGCM client state.
 */
static void tstTransfer(void *pvClient)
{
    RTTestISub("Transfer control-plane handshake");
    VBOXHGCMSVCPARM aReply[VBOX_SHCL_CPARMS_REPLY_MIN + 1];
    HGCMSvcSetU64(&aReply[0], 0);
    HGCMSvcSetU32(&aReply[1], VBOX_SHCL_TX_REPLYMSGTYPE_TRANSFER_STATUS);
    HGCMSvcSetU32(&aReply[2], VINF_SUCCESS);
    HGCMSvcSetPv(&aReply[3], NULL, 0);
    HGCMSvcSetU32(&aReply[4], SHCLTRANSFERSTATUS_REQUESTED);
    int vrc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPLY, RT_ELEMENTS(aReply), aReply);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
    RTTESTI_CHECK(g_State.cTransferCallbacks == 1);
    RTTESTI_CHECK(tstTransferStatusWait(1));
    RTTESTI_CHECK(g_State.idTransferSession != NIL_SHCLSESSIONID);
    RTTESTI_CHECK(g_State.idTransfer != NIL_SHCLTRANSFERID);
    RTTESTI_CHECK(g_State.uTransferGeneration != NIL_SHCLTRANSFERGEN);
    RTTESTI_CHECK(g_State.enmTransferStatus == SHCLTRANSFERSTATUS_REQUESTED);

    VBOXHGCMSVCPARM aStatus[VBOX_SHCL_CPARMS_TRANSFER_STATUS];
    HGCMSvcSetU64(&aStatus[0], VBOX_SHCL_HOST_MSG_TRANSFER_STATUS);
    HGCMSvcSetU32(&aStatus[1], 0);
    HGCMSvcSetU32(&aStatus[2], 0);
    HGCMSvcSetU32(&aStatus[3], 0);
    HGCMSvcSetU32(&aStatus[4], 0);
    vrc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_GET, RT_ELEMENTS(aStatus), aStatus);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
    RTTESTI_CHECK(VBOX_SHCL_CONTEXTID_GET_SESSION(aStatus[0].u.uint64) == g_State.idTransferSession);
    RTTESTI_CHECK(VBOX_SHCL_CONTEXTID_GET_TRANSFER(aStatus[0].u.uint64) == g_State.idTransfer);
    RTTESTI_CHECK(aStatus[2].u.uint32 == SHCLTRANSFERSTATUS_REQUESTED);

    VBOXHGCMSVCPARM aCancel[2];
    HGCMSvcSetU64(&aCancel[0], VBOX_SHCL_CONTEXTID_MAKE(g_State.idTransferSession, g_State.idTransfer, 0));
    HGCMSvcSetU64(&aCancel[1], g_State.uTransferGeneration);
    vrc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_CANCEL, RT_ELEMENTS(aCancel), aCancel);
    RTTESTI_CHECK_RC_OK(vrc);

    HGCMSvcSetU64(&aStatus[0], VBOX_SHCL_HOST_MSG_TRANSFER_STATUS);
    HGCMSvcSetU32(&aStatus[1], 0);
    HGCMSvcSetU32(&aStatus[2], 0);
    HGCMSvcSetU32(&aStatus[3], 0);
    HGCMSvcSetU32(&aStatus[4], 0);
    vrc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_GET, RT_ELEMENTS(aStatus), aStatus);
    RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
    RTTESTI_CHECK(aStatus[2].u.uint32 == SHCLTRANSFERSTATUS_CANCELED);
}
#endif

/**
 * Disconnects Main, unregisters its extension and unloads the Host Service.
 *
 * @param   pvClient            Connected HGCM client state.  May be NULL.
 * @param   pConn               Main service connection.
 */
static void tstShutdown(void *pvClient, GuestShClConn *pConn)
{
    RTTestISub("Orderly shutdown");
    if (pvClient)
    {
        int const vrc = g_Table.pfnDisconnect(g_Table.pvService, 1, pvClient);
        RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
        RTMemFree(pvClient);
    }
    RTTESTI_CHECK(!pConn->isConnected());
    RTTESTI_CHECK(g_State.cBackendDisconnect == 1);

    if (g_State.fExtensionRegistered)
    {
        int const vrc = g_Table.pfnRegisterExtension(g_Table.pvService, NULL, NULL);
        RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
        g_State.fExtensionRegistered = false;
        RTTESTI_CHECK(g_State.cBackendDestroy == 1);
    }
    if (g_State.fServiceLoaded)
    {
        int const vrc = g_Table.pfnUnload(g_Table.pvService);
        RTTESTI_CHECK_RC(vrc, VINF_SUCCESS);
        g_State.fServiceLoaded = false;
    }
}

/** Testcase entry point. */
int main(void)
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstClipboardMain2HostSvc", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    RT_ZERO(g_State);
    try
    {
        GuestShClConn Conn(NULL);
        void *pvClient = NULL;
        tstLifecycle(&Conn, &pvClient);
        if (pvClient)
        {
            tstNegotiate(pvClient);
            tstFormats(pvClient, &Conn);
            tstGuestData(pvClient, &Conn);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            tstTransferInitializedResult(pvClient, &Conn);
            tstTransfer(pvClient);
#endif
        }
        tstShutdown(pvClient, &Conn);
    }
    catch (int vrc)
    {
        RTTestIFailed("Constructing Main's clipboard connection failed: %Rrc", vrc);
    }

    return RTTestSummaryAndDestroy(g_hTest);
}
