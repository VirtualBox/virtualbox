/* $Id: tstClipboardMain.cpp 115060 2026-08-17 17:28:06Z andreas.loeffler@oracle.com $ */
/** @file
 * Main Shared Clipboard - Connection and service-extension testcase.
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

#include <VBox/err.h>
#include <VBox/VMMDev.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>

#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/thread.h>


/**
 * @page pg_tstClipboardMain Main Shared Clipboard connection testcase
 *
 * This is the Main-side unit test for the Shared Clipboard connection boundary.
 * It compiles the production GuestShClConn and ShClBackend dispatcher against
 * two small fakes: one implements the service operation table, the other
 * implements the native backend operation table.
 *
 * The test checks backend lifetime, opaque transport identity, operation
 * forwarding and connection pinning while a guest-data token is outstanding.
 * With transfers enabled it also checks retained lookups and complete
 * session/ID/generation keys; transfer contents are not involved.
 *
 * No VM, HGCM service, native clipboard, filesystem provider or HTTP transport
 * is constructed here.  Those belong to their respective component tests.
 */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Test service session ID. */
#define TST_SHCL_SESSION_ID       UINT16_C(42)
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** Test transfer ID. */
# define TST_SHCL_TRANSFER_ID     UINT16_C(7)
/** Test transfer generation. */
# define TST_SHCL_TRANSFER_GEN    UINT64_C(0x1122334455667788)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Fake opaque service client. */
struct SHCLCLIENTOPAQUE
{
    /** Identity marker. */
    uint32_t uMagic;
};

/** Fake retained guest-data token. */
struct SHCLGUESTDATATOKENOPAQUE
{
    /** Identity marker. */
    uint32_t uMagic;
};

/** Complete definition of the otherwise opaque service command context. */
struct _SHCLCLIENTCMDCTX
{
    /** Guest context ID identifying the pending reply. */
    uint64_t uContextID;
};

/** Fake native backend connection context. */
struct SHCLCONTEXT
{
    /** Main connection owning this context. */
    GuestShClConn *pConn;
};

/** State shared by the service and native-backend fakes. */
typedef struct TSTSHCLSTATE
{
    /** Fake service client. */
    SHCLCLIENTOPAQUE           Client;
    /** Fake backend context. */
    SHCLCONTEXT                BackendCtx;
    /** Fake guest-data token. */
    SHCLGUESTDATATOKENOPAQUE   Token;
    /** Configured backend initialization result. */
    int                        vrcBackendInit;
    /** Configured backend connection result. */
    int                        vrcBackendConnect;
    /** Configured backend synchronization result. */
    int                        vrcBackendSync;
    /** Configured service guest-data-begin result. */
    int                        vrcGuestDataBegin;
    /** Whether guest-data begin returns success without a token. */
    bool                       fGuestDataNullToken;

    /** Backend initialization calls. */
    uint32_t                   cBackendInit;
    /** Backend destruction calls. */
    uint32_t                   cBackendDestroy;
    /** Backend connection calls. */
    uint32_t                   cBackendConnect;
    /** Backend disconnection calls. */
    uint32_t                   cBackendDisconnect;
    /** Backend format-report calls. */
    uint32_t                   cBackendReportFormats;
    /** Backend read calls. */
    uint32_t                   cBackendRead;
    /** Backend write calls. */
    uint32_t                   cBackendWrite;
    /** Backend synchronization calls. */
    uint32_t                   cBackendSync;
    /** Last backend format value. */
    SHCLFORMATS                fBackendFormats;
    /** Last backend format/data type. */
    SHCLFORMAT                 uBackendFormat;
    /** Last backend buffer. */
    void                      *pvBackendData;
    /** Last backend buffer size. */
    uint32_t                   cbBackendData;
    /** Most recently installed backend callback table. */
    PSHCLCALLBACKS             pBackendCallbacks;

    /** Service filter calls. */
    uint32_t                   cSvcFilter;
    /** Service guest-format report calls. */
    uint32_t                   cSvcReportFormats;
    /** Asynchronous service read calls. */
    uint32_t                   cSvcReadAsync;
    /** Synchronous service read calls. */
    uint32_t                   cSvcRead;
    /** Guest-data begin calls. */
    uint32_t                   cGuestDataBegin;
    /** Guest-data completion calls. */
    uint32_t                   cGuestDataComplete;
    /** Guest-data cancellation calls. */
    uint32_t                   cGuestDataCancel;
    /** Last service format value. */
    SHCLFORMATS                fSvcFormats;
    /** Last guest-data format. */
    SHCLFORMAT                 uGuestDataFormat;
    /** Last completed guest-data buffer. */
    void const                *pvGuestData;
    /** Last completed guest-data size. */
    uint32_t                   cbGuestData;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Fake transfer known to the service. */
    SHCLTRANSFER               Transfer;
    /** References held on the fake transfer. */
    uint32_t                   cTransferRefs;
    /** Transfer callback-table requests made to the backend. */
    uint32_t                   cBackendTransferCallbacks;
    /** Transfer status reports made to the backend. */
    uint32_t                   cBackendTransferStatus;
    /** Transfer lookup-by-ID calls. */
    uint32_t                   cSvcTransferGetById;
    /** Transfer lookup-by-key calls. */
    uint32_t                   cSvcTransferGetByKey;
    /** Transfer create calls. */
    uint32_t                   cSvcTransferCreate;
    /** Transfer initialization calls. */
    uint32_t                   cSvcTransferInit;
    /** Transfer destroy-by-ID calls. */
    uint32_t                   cSvcTransferDestroyById;
    /** Transfer destroy-all calls. */
    uint32_t                   cSvcTransferDestroyAll;
    /** Provider initialization calls. */
    uint32_t                   cSvcProviderInit;
    /** Last transfer direction. */
    SHCLTRANSFERDIR            enmTransferDir;
    /** Last transfer source. */
    SHCLSOURCE                 enmTransferSource;
    /** Last transfer status. */
    SHCLTRANSFERSTATUS         enmTransferStatus;
    /** Last transfer status result. */
    int                        vrcTransferStatus;
#endif
} TSTSHCLSTATE;

/** Disconnect worker arguments. */
typedef struct TSTDISCONNECTARGS
{
    /** Connection to disconnect. */
    GuestShClConn             *pConn;
    /** Service transport identifying the connection. */
    SHCLTRANSPORT              Transport;
    /** Event signalled before entering disconnect. */
    RTSEMEVENT                 hStarted;
    /** Result returned by disconnect. */
    int                        vrc;
} TSTDISCONNECTARGS;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Test framework handle. */
static RTTEST g_hTest;
/** Shared fake state. */
static TSTSHCLSTATE g_State;


/*********************************************************************************************************************************
*   Fake native backend                                                                                                          *
*********************************************************************************************************************************/
/** @copydoc SHCLBACKENDOPS::pfnInit */
static int tstBackendInit(void)
{
    g_State.cBackendInit++;
    return g_State.vrcBackendInit;
}

/** @copydoc SHCLBACKENDOPS::pfnDestroy */
static void tstBackendDestroy(void)
{
    g_State.cBackendDestroy++;
}

/** @copydoc SHCLBACKENDOPS::pfnSetCallbacks */
static void tstBackendSetCallbacks(PSHCLCALLBACKS pCallbacks)
{
    g_State.pBackendCallbacks = pCallbacks;
}

/** @copydoc SHCLBACKENDOPS::pfnConnect */
static int tstBackendConnect(GuestShClConn *pConn, PSHCLCONTEXT *ppCtx)
{
    g_State.cBackendConnect++;
    if (RT_FAILURE(g_State.vrcBackendConnect))
        return g_State.vrcBackendConnect;
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
    g_State.cBackendReportFormats++;
    g_State.fBackendFormats = fFormats;
    return VINF_SUCCESS;
}

/** @copydoc SHCLBACKENDOPS::pfnReadData */
static int tstBackendReadData(PSHCLCONTEXT pCtx, SHCLFORMAT uFormat, void *pvData, uint32_t cbData,
                              uint32_t *pcbActual)
{
    RTTESTI_CHECK(pCtx == &g_State.BackendCtx);
    g_State.cBackendRead++;
    g_State.uBackendFormat = uFormat;
    g_State.pvBackendData = pvData;
    g_State.cbBackendData = cbData;
    if (pcbActual)
        *pcbActual = cbData;
    return VINF_SUCCESS;
}

/** @copydoc SHCLBACKENDOPS::pfnWriteData */
static int tstBackendWriteData(PSHCLCONTEXT pCtx, SHCLFORMAT uFormat, void *pvData, uint32_t cbData)
{
    RTTESTI_CHECK(pCtx == &g_State.BackendCtx);
    g_State.cBackendWrite++;
    g_State.uBackendFormat = uFormat;
    g_State.pvBackendData = pvData;
    g_State.cbBackendData = cbData;
    return VINF_SUCCESS;
}

/** @copydoc SHCLBACKENDOPS::pfnSync */
static int tstBackendSync(PSHCLCONTEXT pCtx)
{
    RTTESTI_CHECK(pCtx == &g_State.BackendCtx);
    g_State.cBackendSync++;
    return g_State.vrcBackendSync;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** @copydoc SHCLBACKENDOPS::pfnTransferGetCallbacks */
static void tstBackendTransferGetCallbacks(PSHCLCONTEXT pCtx, PSHCLTRANSFERCALLBACKS pCallbacks)
{
    RTTESTI_CHECK(pCtx == &g_State.BackendCtx);
    g_State.cBackendTransferCallbacks++;
    RT_ZERO(*pCallbacks);
    pCallbacks->pvUser = &g_State;
    pCallbacks->cbUser = sizeof(g_State);
}

/** @copydoc SHCLBACKENDOPS::pfnTransferHandleStatusReply */
static int tstBackendTransferHandleStatusReply(PSHCLCONTEXT pCtx, PSHCLTRANSFER pTransfer,
                                                SHCLSOURCE enmSource, SHCLTRANSFERSTATUS enmStatus, int vrcStatus)
{
    RTTESTI_CHECK(pCtx == &g_State.BackendCtx);
    RTTESTI_CHECK(pTransfer == &g_State.Transfer);
    g_State.cBackendTransferStatus++;
    g_State.enmTransferSource = enmSource;
    g_State.enmTransferStatus = enmStatus;
    g_State.vrcTransferStatus = vrcStatus;
    return VINF_SUCCESS;
}
#endif

/** Fake backend operation table selected by ShClBackend. */
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
 * Supplies the fake backend to ShClBackend.
 *
 * @returns Immutable fake backend operation table.
 */
PCSHCLBACKENDOPS ShClBackendGetOps(void)
{
    return &g_BackendOps;
}


/*********************************************************************************************************************************
*   Fake service endpoint                                                                                                        *
*********************************************************************************************************************************/
/** Checks that an operation was made against the fake service client. */
static void tstSvcCheckClient(SHCLCLIENTHANDLE hClient)
{
    RTTESTI_CHECK(hClient == &g_State.Client);
}

/** @copydoc SHCLSVCOPS::pfnFilterFormats */
static DECLCALLBACK(int) tstSvcFilterFormats(SHCLCLIENTHANDLE hClient, bool fHostToGuest,
                                             SHCLFORMATS fFormats, SHCLFORMATS *pfFiltered)
{
    tstSvcCheckClient(hClient);
    g_State.cSvcFilter++;
    g_State.fSvcFormats = fFormats;
    *pfFiltered = fHostToGuest ? fFormats : fFormats & ~VBOX_SHCL_FMT_HTML;
    return VINF_SUCCESS;
}

/** @copydoc SHCLSVCOPS::pfnReportFormatsToGuest */
static DECLCALLBACK(int) tstSvcReportFormatsToGuest(SHCLCLIENTHANDLE hClient, SHCLFORMATS fFormats,
                                                    SHCLFORMATS *pfReported)
{
    tstSvcCheckClient(hClient);
    g_State.cSvcReportFormats++;
    g_State.fSvcFormats = fFormats;
    if (pfReported)
        *pfReported = fFormats;
    return VINF_SUCCESS;
}

/** @copydoc SHCLSVCOPS::pfnReadDataFromGuestAsync */
static DECLCALLBACK(int) tstSvcReadDataFromGuestAsync(SHCLCLIENTHANDLE hClient, SHCLFORMATS fFormats,
                                                      PSHCLEVENT *ppEvent)
{
    tstSvcCheckClient(hClient);
    g_State.cSvcReadAsync++;
    g_State.fSvcFormats = fFormats;
    RTTESTI_CHECK(ppEvent == NULL);
    return VINF_SUCCESS;
}

/** @copydoc SHCLSVCOPS::pfnReadDataFromGuest */
static DECLCALLBACK(int) tstSvcReadDataFromGuest(SHCLCLIENTHANDLE hClient, SHCLFORMAT uFormat,
                                                 void **ppvData, uint32_t *pcbData)
{
    static char const s_achData[] = "guest";
    tstSvcCheckClient(hClient);
    g_State.cSvcRead++;
    g_State.uGuestDataFormat = uFormat;
    *ppvData = RTMemDup(s_achData, sizeof(s_achData));
    if (!*ppvData)
        return VERR_NO_MEMORY;
    *pcbData = sizeof(s_achData);
    return VINF_SUCCESS;
}

/** @copydoc SHCLSVCOPS::pfnGuestDataBegin */
static DECLCALLBACK(int) tstSvcGuestDataBegin(SHCLCLIENTHANDLE hClient, PSHCLCLIENTCMDCTX pCmdCtx,
                                              SHCLFORMAT uFormat, PSHCLGUESTDATATOKEN phToken)
{
    tstSvcCheckClient(hClient);
    RTTESTI_CHECK(pCmdCtx != NULL);
    g_State.cGuestDataBegin++;
    g_State.uGuestDataFormat = uFormat;
    if (RT_SUCCESS(g_State.vrcGuestDataBegin) && !g_State.fGuestDataNullToken)
        *phToken = &g_State.Token;
    return g_State.vrcGuestDataBegin;
}

/** @copydoc SHCLSVCOPS::pfnGuestDataComplete */
static DECLCALLBACK(int) tstSvcGuestDataComplete(SHCLCLIENTHANDLE hClient, SHCLGUESTDATATOKEN hToken,
                                                 void const *pvData, uint32_t cbData)
{
    tstSvcCheckClient(hClient);
    RTTESTI_CHECK(hToken == &g_State.Token);
    g_State.cGuestDataComplete++;
    g_State.pvGuestData = pvData;
    g_State.cbGuestData = cbData;
    return VINF_SUCCESS;
}

/** @copydoc SHCLSVCOPS::pfnGuestDataCancel */
static DECLCALLBACK(void) tstSvcGuestDataCancel(SHCLCLIENTHANDLE hClient, SHCLGUESTDATATOKEN hToken)
{
    tstSvcCheckClient(hClient);
    RTTESTI_CHECK(hToken == &g_State.Token);
    g_State.cGuestDataCancel++;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** Retains the fake transfer without depending on the GuestHost transfer data plane. */
static void tstTransferRetain(void)
{
    ASMAtomicIncU32(&g_State.cTransferRefs);
}

/** @copydoc SHCLSVCOPS::pfnTransferGetByIdRetained */
static DECLCALLBACK(PSHCLTRANSFER) tstSvcTransferGetByIdRetained(SHCLCLIENTHANDLE hClient,
                                                                 SHCLTRANSFERID idTransfer)
{
    tstSvcCheckClient(hClient);
    g_State.cSvcTransferGetById++;
    if (idTransfer != TST_SHCL_TRANSFER_ID)
        return NULL;
    tstTransferRetain();
    return &g_State.Transfer;
}

/** @copydoc SHCLSVCOPS::pfnTransferGetByKeyRetained */
static DECLCALLBACK(PSHCLTRANSFER) tstSvcTransferGetByKeyRetained(SHCLCLIENTHANDLE hClient,
                                                                  SHCLSESSIONID idSession,
                                                                  SHCLTRANSFERID idTransfer,
                                                                  SHCLTRANSFERGEN uGeneration)
{
    tstSvcCheckClient(hClient);
    g_State.cSvcTransferGetByKey++;
    if (   idSession != TST_SHCL_SESSION_ID
        || idTransfer != TST_SHCL_TRANSFER_ID
        || uGeneration != TST_SHCL_TRANSFER_GEN)
        return NULL;
    tstTransferRetain();
    return &g_State.Transfer;
}

/** @copydoc SHCLSVCOPS::pfnTransferCreate */
static DECLCALLBACK(int) tstSvcTransferCreate(SHCLCLIENTHANDLE hClient, SHCLTRANSFERDIR enmDir,
                                              SHCLSOURCE enmSource, PSHCLTRANSFERCALLBACKS pCallbacks,
                                              SHCLTRANSFERID idTransfer, PSHCLTRANSFER *ppTransfer)
{
    tstSvcCheckClient(hClient);
    RTTESTI_CHECK(pCallbacks != NULL);
    RTTESTI_CHECK(idTransfer == TST_SHCL_TRANSFER_ID);
    g_State.cSvcTransferCreate++;
    g_State.enmTransferDir = enmDir;
    g_State.enmTransferSource = enmSource;
    tstTransferRetain();
    *ppTransfer = &g_State.Transfer;
    return VINF_SUCCESS;
}

/** @copydoc SHCLSVCOPS::pfnTransferInit */
static DECLCALLBACK(int) tstSvcTransferInit(SHCLCLIENTHANDLE hClient, PSHCLTRANSFER pTransfer)
{
    tstSvcCheckClient(hClient);
    RTTESTI_CHECK(pTransfer == &g_State.Transfer);
    g_State.cSvcTransferInit++;
    return VINF_SUCCESS;
}

/** @copydoc SHCLSVCOPS::pfnTransferDestroyById */
static DECLCALLBACK(void) tstSvcTransferDestroyById(SHCLCLIENTHANDLE hClient, SHCLTRANSFERID idTransfer)
{
    tstSvcCheckClient(hClient);
    RTTESTI_CHECK(idTransfer == TST_SHCL_TRANSFER_ID);
    g_State.cSvcTransferDestroyById++;
}

/** @copydoc SHCLSVCOPS::pfnTransferDestroyAll */
static DECLCALLBACK(void) tstSvcTransferDestroyAll(SHCLCLIENTHANDLE hClient)
{
    tstSvcCheckClient(hClient);
    g_State.cSvcTransferDestroyAll++;
}

/** @copydoc SHCLSVCOPS::pfnTransferProviderInitGuest */
static DECLCALLBACK(int) tstSvcTransferProviderInitGuest(SHCLCLIENTHANDLE hClient, PSHCLTXPROVIDER pProvider)
{
    tstSvcCheckClient(hClient);
    g_State.cSvcProviderInit++;
    RT_ZERO(*pProvider);
    pProvider->enmSource = SHCLSOURCE_REMOTE;
    pProvider->pvUser = &g_State;
    pProvider->cbUser = sizeof(g_State);
    return VINF_SUCCESS;
}
#endif

/** Fake service operation table. */
static SHCLSVCOPS const g_SvcOps =
{
    sizeof(SHCLSVCOPS),
    tstSvcFilterFormats,
    tstSvcReportFormatsToGuest,
    tstSvcReadDataFromGuestAsync,
    tstSvcReadDataFromGuest,
    tstSvcGuestDataBegin,
    tstSvcGuestDataComplete,
    tstSvcGuestDataCancel,
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    tstSvcTransferGetByIdRetained,
    tstSvcTransferGetByKeyRetained,
    tstSvcTransferCreate,
    tstSvcTransferInit,
    tstSvcTransferDestroyById,
    tstSvcTransferDestroyAll,
    tstSvcTransferProviderInitGuest,
#endif
};

/** Returns the fake service transport. */
static SHCLTRANSPORT tstTransport(void)
{
    SHCLTRANSPORT Transport;
    Transport.hClient = &g_State.Client;
    Transport.pOps = &g_SvcOps;
    return Transport;
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS


/*********************************************************************************************************************************
*   Minimal transfer metadata implementation                                                                                     *
*********************************************************************************************************************************/
/* These functions model the transfer metadata contract used by Main.  The
 * testcase deliberately does not link the transfer data-plane implementation. */
bool ShClTransferStatusResultIsValid(SHCLTRANSFERSTATUS enmStatus, int vrcTransfer)
{
    switch (enmStatus)
    {
        case SHCLTRANSFERSTATUS_REQUESTED:
        case SHCLTRANSFERSTATUS_INITIALIZED:
        case SHCLTRANSFERSTATUS_UNINITIALIZED:
        case SHCLTRANSFERSTATUS_STARTED:
        case SHCLTRANSFERSTATUS_COMPLETED:
            return RT_SUCCESS(vrcTransfer);

        case SHCLTRANSFERSTATUS_CANCELED:
            return vrcTransfer == VERR_CANCELLED;

        case SHCLTRANSFERSTATUS_KILLED:
        case SHCLTRANSFERSTATUS_ERROR:
            return RT_FAILURE(vrcTransfer);

        default:
            return false;
    }
}

uint32_t ShClTransferRelease(PSHCLTRANSFER pTransfer)
{
    RTTESTI_CHECK(pTransfer == &g_State.Transfer);
    uint32_t const cRefs = ASMAtomicDecU32(&g_State.cTransferRefs);
    RTTESTI_CHECK(cRefs < UINT32_MAX);
    return cRefs;
}

SHCLTRANSFERID ShClTransferGetID(PSHCLTRANSFER pTransfer)
{
    RTTESTI_CHECK(pTransfer == &g_State.Transfer);
    return TST_SHCL_TRANSFER_ID;
}

SHCLSESSIONID ShClTransferGetSessionId(PSHCLTRANSFER pTransfer)
{
    RTTESTI_CHECK(pTransfer == &g_State.Transfer);
    return TST_SHCL_SESSION_ID;
}

SHCLTRANSFERGEN ShClTransferGetGeneration(PSHCLTRANSFER pTransfer)
{
    RTTESTI_CHECK(pTransfer == &g_State.Transfer);
    return TST_SHCL_TRANSFER_GEN;
}
#endif


/*********************************************************************************************************************************
*   Helpers                                                                                                                      *
*********************************************************************************************************************************/
/** Resets the fake state to successful defaults. */
static void tstStateReset(void)
{
    RT_ZERO(g_State);
    g_State.Client.uMagic = UINT32_C(0x434c4950);
    g_State.Token.uMagic = UINT32_C(0x544f4b4e);
    g_State.vrcBackendInit = VINF_SUCCESS;
    g_State.vrcBackendConnect = VINF_SUCCESS;
    g_State.vrcBackendSync = VINF_SUCCESS;
    g_State.vrcGuestDataBegin = VINF_SUCCESS;
}

/** Initializes and connects a test connection. */
static void tstConnect(GuestShClConn &Conn, SHCLTRANSPORT *pTransport)
{
    *pTransport = tstTransport();
    RTTESTI_CHECK_RC(Conn.initBackend(), VINF_SUCCESS);
    RTTESTI_CHECK_RC(Conn.connect(pTransport), VINF_SUCCESS);
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** Disconnects and destroys a test connection. */
static void tstDisconnect(GuestShClConn &Conn, PCSHCLTRANSPORT pTransport)
{
    if (Conn.isConnected())
        RTTESTI_CHECK_RC(Conn.disconnect(pTransport), VINF_SUCCESS);
    RTTESTI_CHECK_RC(Conn.destroyBackend(), VINF_SUCCESS);
}
#endif

/** Executes disconnect on a worker thread. */
static DECLCALLBACK(int) tstDisconnectThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf);
    TSTDISCONNECTARGS *pArgs = (TSTDISCONNECTARGS *)pvUser;
    RTSemEventSignal(pArgs->hStarted);
    pArgs->vrc = pArgs->pConn->disconnect(&pArgs->Transport);
    return pArgs->vrc;
}


/*********************************************************************************************************************************
*   Testcases                                                                                                                    *
*********************************************************************************************************************************/
/** Tests backend initialization and destruction ownership. */
static void tstBackendLifecycle(void)
{
    RTTestISub("Backend lifecycle");
    tstStateReset();

    GuestShClConn Conn(NULL);
    g_State.vrcBackendInit = VERR_NOT_SUPPORTED;
    RTTESTI_CHECK_RC(Conn.initBackend(), VERR_NOT_SUPPORTED);
    RTTESTI_CHECK(g_State.cBackendInit == 1);

    g_State.vrcBackendInit = VINF_SUCCESS;
    RTTESTI_CHECK_RC(Conn.initBackend(), VINF_SUCCESS);
    RTTESTI_CHECK_RC(Conn.initBackend(), VINF_SUCCESS);
    RTTESTI_CHECK(g_State.cBackendInit == 2);

    SHCLCALLBACKS Callbacks;
    RT_ZERO(Callbacks);
    Conn.setBackendCallbacks(&Callbacks);
    RTTESTI_CHECK(g_State.pBackendCallbacks == &Callbacks);

    RTTESTI_CHECK_RC(Conn.destroyBackend(), VINF_SUCCESS);
    RTTESTI_CHECK_RC(Conn.destroyBackend(), VINF_SUCCESS);
    RTTESTI_CHECK(g_State.cBackendDestroy == 1);
}

/** Tests transport identity and service/backend forwarding. */
static void tstConnectionAndForwarding(void)
{
    RTTestISub("Connection and forwarding");
    tstStateReset();

    GuestShClConn Conn(NULL);
    SHCLTRANSPORT Transport = tstTransport();
    SHCLTRANSPORT Invalid = Transport;
    Invalid.pOps = NULL;
    RTTESTI_CHECK_RC(RTTestIDisableAssertions(), VINF_SUCCESS);
    RTTESTI_CHECK_RC(Conn.connect(&Invalid), VERR_INVALID_HANDLE);
    RTTESTI_CHECK_RC(RTTestIRestoreAssertions(), VINF_SUCCESS);

    RTTESTI_CHECK_RC(Conn.initBackend(), VINF_SUCCESS);
    g_State.vrcBackendConnect = VERR_NOT_AVAILABLE;
    RTTESTI_CHECK_RC(Conn.connect(&Transport), VERR_NOT_AVAILABLE);
    RTTESTI_CHECK(!Conn.isConnected());
    g_State.vrcBackendConnect = VINF_SUCCESS;
    RTTESTI_CHECK_RC(Conn.connect(&Transport), VINF_SUCCESS);
    RTTESTI_CHECK(Conn.matches(&Transport));
    RTTESTI_CHECK_RC(Conn.connect(&Transport), VERR_RESOURCE_BUSY);

    SHCLTRANSPORT Wrong = Transport;
    SHCLCLIENTOPAQUE OtherClient = { UINT32_C(0xdeadbeef) };
    Wrong.hClient = &OtherClient;
    RTTESTI_CHECK_RC(Conn.disconnect(&Wrong), VERR_INVALID_HANDLE);

    SHCLFORMATS fReported = 0;
    RTTESTI_CHECK_RC(Conn.reportFormatsToGuest(VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_HTML, &fReported),
                     VINF_SUCCESS);
    RTTESTI_CHECK(fReported == (VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_HTML));
    RTTESTI_CHECK(g_State.cSvcReportFormats == 1);

    RTTESTI_CHECK_RC(Conn.readDataFromGuestAsync(VBOX_SHCL_FMT_UNICODETEXT, NULL), VINF_SUCCESS);

    void *pvData = NULL;
    uint32_t cbData = 0;
    RTTESTI_CHECK_RC(Conn.readDataFromGuest(VBOX_SHCL_FMT_UNICODETEXT, &pvData, &cbData), VINF_SUCCESS);
    RTTESTI_CHECK(cbData == sizeof("guest"));
    RTTESTI_CHECK(pvData && !memcmp(pvData, "guest", sizeof("guest")));
    RTMemFree(pvData);

    RTTESTI_CHECK_RC(Conn.reportFormatsToBackend(VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_HTML), VINF_SUCCESS);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    RTTESTI_CHECK(g_State.cSvcFilter == 1);
    RTTESTI_CHECK(g_State.fBackendFormats == VBOX_SHCL_FMT_UNICODETEXT);
#else
    RTTESTI_CHECK(g_State.fBackendFormats == (VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_HTML));
#endif

    uint8_t abData[8];
    uint32_t cbActual = 0;
    RTTESTI_CHECK_RC(Conn.readDataFromBackend(VBOX_SHCL_FMT_UNICODETEXT, abData, sizeof(abData), &cbActual),
                     VINF_SUCCESS);
    RTTESTI_CHECK(cbActual == sizeof(abData));
    RTTESTI_CHECK_RC(Conn.writeDataToBackend(VBOX_SHCL_FMT_UNICODETEXT, abData, sizeof(abData)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(Conn.syncBackend(), VINF_SUCCESS);

    RTTESTI_CHECK_RC(Conn.disconnect(&Transport), VINF_SUCCESS);
    RTTESTI_CHECK_RC(Conn.reportFormatsToBackend(VBOX_SHCL_FMT_UNICODETEXT), VERR_SHCLPB_NO_DATA);
    RTTESTI_CHECK_RC(Conn.destroyBackend(), VINF_SUCCESS);
}

/** Tests retained guest-data tokens and disconnect draining. */
static void tstGuestDataTokens(void)
{
    RTTestISub("Guest-data token ownership");
    tstStateReset();

    GuestShClConn Conn(NULL);
    SHCLTRANSPORT Transport;
    tstConnect(Conn, &Transport);
    SHCLCLIENTCMDCTX CmdCtx = { UINT64_C(0x1234) };

    SHCLGUESTDATATOKEN hToken = NULL;
    g_State.fGuestDataNullToken = true;
    RTTESTI_CHECK_RC(Conn.guestDataBegin(&CmdCtx, VBOX_SHCL_FMT_UNICODETEXT, &hToken), VINF_SUCCESS);
    RTTESTI_CHECK(hToken == NULL);

    g_State.fGuestDataNullToken = false;
    g_State.vrcGuestDataBegin = VERR_INVALID_CONTEXT;
    RTTESTI_CHECK_RC(Conn.guestDataBegin(&CmdCtx, VBOX_SHCL_FMT_UNICODETEXT, &hToken), VERR_INVALID_CONTEXT);

    g_State.vrcGuestDataBegin = VINF_SUCCESS;
    RTTESTI_CHECK_RC(Conn.guestDataBegin(&CmdCtx, VBOX_SHCL_FMT_UNICODETEXT, &hToken), VINF_SUCCESS);
    RTTESTI_CHECK(hToken == &g_State.Token);
    Conn.guestDataCancel(hToken);
    RTTESTI_CHECK(g_State.cGuestDataCancel == 1);

    hToken = NULL;
    RTTESTI_CHECK_RC(Conn.guestDataBegin(&CmdCtx, VBOX_SHCL_FMT_HTML, &hToken), VINF_SUCCESS);

    TSTDISCONNECTARGS Args;
    Args.pConn = &Conn;
    Args.Transport = Transport;
    Args.hStarted = NIL_RTSEMEVENT;
    Args.vrc = VERR_IPE_UNINITIALIZED_STATUS;
    RTTESTI_CHECK_RC(RTSemEventCreate(&Args.hStarted), VINF_SUCCESS);
    RTTHREAD hThread = NIL_RTTHREAD;
    RTTESTI_CHECK_RC(RTThreadCreate(&hThread, tstDisconnectThread, &Args, 0, RTTHREADTYPE_DEFAULT,
                                    RTTHREADFLAGS_WAITABLE, "shcl-discon"), VINF_SUCCESS);
    RTTESTI_CHECK_RC(RTSemEventWait(Args.hStarted, RT_MS_5SEC), VINF_SUCCESS);

    for (uint32_t i = 0; i < 5000 && Conn.isConnected(); i++)
        RTThreadSleep(1);
    RTTESTI_CHECK(!Conn.isConnected());
    RTTESTI_CHECK(g_State.cBackendDisconnect == 0);

    static char const s_achReply[] = "reply";
    RTTESTI_CHECK_RC(Conn.guestDataComplete(hToken, s_achReply, sizeof(s_achReply)), VINF_SUCCESS);
    int vrcThread = VERR_IPE_UNINITIALIZED_STATUS;
    RTTESTI_CHECK_RC(RTThreadWait(hThread, RT_MS_5SEC, &vrcThread), VINF_SUCCESS);
    RTTESTI_CHECK_RC(vrcThread, VINF_SUCCESS);
    RTTESTI_CHECK(g_State.cBackendDisconnect == 1);
    RTTESTI_CHECK(g_State.cGuestDataComplete == 1);
    RTTESTI_CHECK(g_State.pvGuestData == s_achReply);
    RTTESTI_CHECK_RC(RTSemEventDestroy(Args.hStarted), VINF_SUCCESS);
    RTTESTI_CHECK_RC(Conn.destroyBackend(), VINF_SUCCESS);
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** Tests Main's transfer metadata and full-key forwarding. */
static void tstTransfers(void)
{
    RTTestISub("Transfer metadata forwarding");
    tstStateReset();

    GuestShClConn Conn(NULL);
    SHCLTRANSPORT Transport;
    tstConnect(Conn, &Transport);

    PSHCLTRANSFER pTransfer = Conn.transferGetByIdRetained(TST_SHCL_TRANSFER_ID);
    RTTESTI_CHECK(pTransfer == &g_State.Transfer);
    if (pTransfer)
        ShClTransferRelease(pTransfer);
    RTTESTI_CHECK(Conn.transferGetByIdRetained(TST_SHCL_TRANSFER_ID + 1) == NULL);

    pTransfer = Conn.transferGetByKeyRetained(TST_SHCL_SESSION_ID, TST_SHCL_TRANSFER_ID,
                                               TST_SHCL_TRANSFER_GEN + 1);
    RTTESTI_CHECK(pTransfer == NULL);
    pTransfer = Conn.transferGetByKeyRetained(TST_SHCL_SESSION_ID, TST_SHCL_TRANSFER_ID,
                                               TST_SHCL_TRANSFER_GEN);
    RTTESTI_CHECK(pTransfer == &g_State.Transfer);
    if (pTransfer)
        ShClTransferRelease(pTransfer);

    SHCLTRANSFERCALLBACKS Callbacks;
    RT_ZERO(Callbacks);
    RTTESTI_CHECK_RC(Conn.transferGetCallbacks(&Callbacks), VINF_SUCCESS);
    RTTESTI_CHECK(Callbacks.pvUser == &g_State);

    pTransfer = NULL;
    RTTESTI_CHECK_RC(Conn.transferCreate(SHCLTRANSFERDIR_HOST_TO_GUEST, SHCLSOURCE_LOCAL, &Callbacks,
                                        TST_SHCL_TRANSFER_ID, &pTransfer), VINF_SUCCESS);
    RTTESTI_CHECK(pTransfer == &g_State.Transfer);
    if (pTransfer)
        ShClTransferRelease(pTransfer);
    RTTESTI_CHECK_RC(Conn.transferInit(&g_State.Transfer), VINF_SUCCESS);
    RTTESTI_CHECK_RC(Conn.transferHandleStatusReply(&g_State.Transfer, SHCLSOURCE_REMOTE,
                                                   SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS), VINF_SUCCESS);
    RTTESTI_CHECK(g_State.enmTransferStatus == SHCLTRANSFERSTATUS_REQUESTED);

    SHCLTXPROVIDER Provider;
    RT_ZERO(Provider);
    RTTESTI_CHECK_RC(Conn.transferProviderInitGuest(&Provider), VINF_SUCCESS);
    RTTESTI_CHECK(Provider.enmSource == SHCLSOURCE_REMOTE);
    Conn.transferDestroyById(TST_SHCL_TRANSFER_ID);

    RTTESTI_CHECK(g_State.cTransferRefs == 0);
    tstDisconnect(Conn, &Transport);
    RTTESTI_CHECK(g_State.cSvcTransferDestroyAll == 1);
}
#endif


/** Testcase entry point. */
int main(void)
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstClipboardMain", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    tstBackendLifecycle();
    tstConnectionAndForwarding();
    tstGuestDataTokens();
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    tstTransfers();
#endif

    return RTTestSummaryAndDestroy(g_hTest);
}
