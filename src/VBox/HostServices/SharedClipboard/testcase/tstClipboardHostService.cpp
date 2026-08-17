/* $Id: tstClipboardHostService.cpp 115060 2026-08-17 17:28:06Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Host Service testcase.
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
#include <VBox/HostServices/VBoxClipboardExt.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>

#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/test.h>


/** @page pg_tstClipboardHostService  Shared Clipboard Host Service testcase
 *
 * This is the Host Service-side unit test for the Shared Clipboard HGCM
 * boundary.  It runs the production service sources in-process through their
 * exported HGCM table.  A small extension sink records notifications and uses
 * the published service operation table; it does not emulate Main policy.
 *
 * The test checks connection ownership, host policy and feature negotiation,
 * message wakeup/removal, clipboard-data replies, representative malformed
 * guest calls, and the transfer control-plane lifecycle.  Guest parameters are
 * always treated as hostile, and rejected calls must not mutate service or
 * extension state.
 *
 * Guest Additions, native backends, format conversion, filesystem providers,
 * HTTP transport, transfer contents and VMM saved-state serialization are out
 * of scope for this compact test.
 */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** State attached to an HGCM guest-call handle. */
struct VBOXHGCMCALLHANDLE_TYPEDEF
{
    /** Whether the service completed the call. */
    bool                        fCompleted;
    /** Completion status supplied by the service. */
    int32_t                     rc;
};

/** Minimal stand-in for Main's Shared Clipboard service extension. */
typedef struct TSTCLEXT
{
    /** Transport received with the latest client connection. */
    SHCLTRANSPORT               Transport;
    /** Number of connect notifications. */
    uint32_t                    cConnect;
    /** Number of disconnect notifications. */
    uint32_t                    cDisconnect;
    /** Number of synchronization notifications. */
    uint32_t                    cSync;
    /** Number of guest format announcements. */
    uint32_t                    cGuestFormats;
    /** Number of host clipboard data reads requested by the guest. */
    uint32_t                    cDataReads;
    /** Number of guest clipboard data writes forwarded by the service. */
    uint32_t                    cDataWrites;
    /** Last format mask announced by the guest. */
    SHCLFORMATS                 fGuestFormats;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Number of transfer-status callback-table queries. */
    uint32_t                    cTransferCallbackQueries;
    /** Number of guest transfer-status notifications. */
    uint32_t                    cTransferStatuses;
    /** Session ID of the last transfer-status notification. */
    SHCLSESSIONID               idTransferSession;
    /** Transfer ID of the last transfer-status notification. */
    SHCLTRANSFERID              idTransfer;
    /** Generation of the last transfer-status notification. */
    SHCLTRANSFERGEN             uTransferGeneration;
    /** Direction of the last transfer-status notification. */
    SHCLTRANSFERDIR             enmTransferDir;
    /** Source of the last transfer-status notification. */
    SHCLSOURCE                  enmTransferSource;
    /** Last transfer status reported by the guest. */
    SHCLTRANSFERSTATUS          enmTransferStatus;
    /** Last transfer result reported by the guest. */
    int                         rcTransfer;
#endif
} TSTCLEXT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Test handle. */
static RTTEST                   g_hTest;
/** Loaded service function table. */
static VBOXHGCMSVCFNTABLE       g_Table;
/** HGCM helpers supplied to the service. */
static VBOXHGCMSVCHELPERS       g_Helpers;
/** Minimal service extension state. */
static TSTCLEXT                 g_Ext;
/** Data returned for VBOX_SHCL_GUEST_FN_DATA_READ. */
static uint8_t const            g_abHostData[] = { 't', 'e', 's', 't', '\0' };


/*********************************************************************************************************************************
*   External Symbols                                                                                                             *
*********************************************************************************************************************************/
extern "C" DECLCALLBACK(DECLEXPORT(int)) VBoxHGCMSvcLoad(VBOXHGCMSVCFNTABLE *pTable);


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
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
    hCall->rc         = rc;
    return VINF_SUCCESS;
}


/**
 * Dispatches service-extension requests without implementing a native backend.
 *
 * The test records the transport contract and provides fixed POD clipboard data;
 * platform policy and Main behavior deliberately stay outside this testcase.
 *
 * @returns VBox status code.
 * @param   pvExtension        Test extension state.
 * @param   uFunction          VBOX_CLIPBOARD_EXT_FN_XXX function number.
 * @param   pvParms            Service-extension parameters.
 * @param   cbParms            Size of @a pvParms in bytes.
 */
static DECLCALLBACK(int) tstExtension(void *pvExtension, uint32_t uFunction, void *pvParms, uint32_t cbParms)
{
    TSTCLEXT * const pExt = (TSTCLEXT *)pvExtension;
    RTTESTI_CHECK_RET(cbParms == sizeof(SHCLEXTPARMS), VERR_INVALID_PARAMETER);
    PSHCLEXTPARMS const pParms = (PSHCLEXTPARMS)pvParms;

    switch (uFunction)
    {
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT:
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY:
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT:
            pExt->Transport = ShClSvcExtGetTransport(pParms);
            RTTESTI_CHECK_RET(ShClTransportIsValid(&pExt->Transport), VERR_INVALID_PARAMETER);
            pExt->cConnect++;
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT:
        {
            SHCLTRANSPORT const Transport = ShClSvcExtGetTransport(pParms);
            RTTESTI_CHECK_RET(ShClTransportIsEqual(&pExt->Transport, &Transport), VERR_INVALID_PARAMETER);
            pExt->cDisconnect++;
            return VINF_SUCCESS;
        }

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC:
        {
            SHCLTRANSPORT const Transport = ShClSvcExtGetTransport(pParms);
            RTTESTI_CHECK_RET(ShClTransportIsEqual(&pExt->Transport, &Transport), VERR_INVALID_PARAMETER);
            pExt->cSync++;
            return VINF_SUCCESS;
        }

        case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST:
            pExt->cGuestFormats++;
            pExt->fGuestFormats = pParms->u.ReportFormats.uFormats;
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_DATA_READ:
            pExt->cDataReads++;
            pParms->u.ReadWriteData.cbActual = sizeof(g_abHostData);
            if (pParms->u.ReadWriteData.cbData >= sizeof(g_abHostData))
                memcpy(pParms->u.ReadWriteData.pvData, g_abHostData, sizeof(g_abHostData));
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_DATA_WRITE:
        {
            SHCLTRANSPORT const Transport = ShClSvcExtGetTransport(pParms);
            RTTESTI_CHECK_RET(ShClTransportIsEqual(&pExt->Transport, &Transport), VERR_INVALID_PARAMETER);
            pExt->cDataWrites++;

            SHCLGUESTDATATOKEN hToken;
            int rc = Transport.pOps->pfnGuestDataBegin(Transport.hClient, pParms->u.ReadWriteData.pCmdCtx,
                                                       pParms->u.ReadWriteData.uFormat, &hToken);
            if (RT_SUCCESS(rc) && hToken)
                rc = Transport.pOps->pfnGuestDataComplete(Transport.hClient, hToken,
                                                           pParms->u.ReadWriteData.pvData,
                                                           pParms->u.ReadWriteData.cbData);
            return rc;
        }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        case VBOX_CLIPBOARD_EXT_FN_TRANSFER_CALLBACKS:
            RTTESTI_CHECK_RET(pParms->u.TransferCallbacks.pCallbacks != NULL, VERR_INVALID_PARAMETER);
            RT_ZERO(*pParms->u.TransferCallbacks.pCallbacks);
            pExt->cTransferCallbackQueries++;
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER:
        {
            PSHCLTRANSFER const pTransfer = pParms->u.FileTransferData.pTransfer;
            SHCLREPLY const * const pReply = pParms->u.FileTransferData.pReply;
            RTTESTI_CHECK_RET(pTransfer != NULL, VERR_INVALID_PARAMETER);
            RTTESTI_CHECK_RET(pReply != NULL, VERR_INVALID_PARAMETER);
            RTTESTI_CHECK_RET(pReply->uType == VBOX_SHCL_TX_REPLYMSGTYPE_TRANSFER_STATUS, VERR_INVALID_PARAMETER);

            pExt->cTransferStatuses++;
            pExt->idTransferSession   = ShClTransferGetSessionId(pTransfer);
            pExt->idTransfer          = ShClTransferGetID(pTransfer);
            pExt->uTransferGeneration = ShClTransferGetGeneration(pTransfer);
            pExt->enmTransferDir      = ShClTransferGetDir(pTransfer);
            pExt->enmTransferSource   = pParms->u.FileTransferData.enmShClSource;
            pExt->enmTransferStatus   = pReply->u.TransferStatus.uStatus;
            pExt->rcTransfer          = pReply->rc;
            return VINF_SUCCESS;
        }
#endif

        default:
            return VERR_NOT_SUPPORTED;
    }
}


/**
 * Starts a guest call and returns its completion state to the caller.
 *
 * @param   pvClient            HGCM client state.
 * @param   uFunction          Guest function number.
 * @param   cParms              Number of HGCM parameters.
 * @param   paParms             HGCM parameters.  Optional if @a cParms is zero.
 * @param   pCall               Call state which remains valid until completion.
 */
static void tstGuestCallStart(void *pvClient, uint32_t uFunction, uint32_t cParms,
                              VBOXHGCMSVCPARM *paParms, VBOXHGCMCALLHANDLE_TYPEDEF *pCall)
{
    pCall->fCompleted = false;
    pCall->rc         = VERR_IPE_UNINITIALIZED_STATUS;
    g_Table.pfnCall(g_Table.pvService, pCall, 1 /* idClient */, pvClient,
                    uFunction, cParms, paParms, 0 /* tsArrival */);
}


/**
 * Executes a guest call which is expected to complete synchronously.
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
    tstGuestCallStart(pvClient, uFunction, cParms, paParms, &Call);
    RTTESTI_CHECK_MSG_RET(Call.fCompleted, ("Guest function %RU32 was not completed\n", uFunction),
                          VERR_INTERNAL_ERROR);
    return Call.rc;
}


/**
 * Executes an intentionally malformed guest call with guest assertions suppressed.
 *
 * @returns Guest-call result.
 * @param   pvClient            HGCM client state.
 * @param   uFunction          Guest function number.
 * @param   cParms              Number of HGCM parameters.
 * @param   paParms             HGCM parameters.  Optional if @a cParms is zero.
 */
static int tstGuestCallUntrusted(void *pvClient, uint32_t uFunction, uint32_t cParms, VBOXHGCMSVCPARM *paParms)
{
    int rc = RTTestIDisableAssertions();
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    int const rcCall = tstGuestCall(pvClient, uFunction, cParms, paParms);

    rc = RTTestIRestoreAssertions();
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    return rcCall;
}


/**
 * Loads the service and verifies its small HGCM registration contract.
 *
 * @returns VBox status code.
 */
static int tstLoadService(void)
{
    RTTestISub("HGCM service registration");

    RT_ZERO(g_Table);
    RT_ZERO(g_Helpers);
    g_Helpers.pfnCallComplete = tstCallComplete;
    g_Table.cbSize            = sizeof(g_Table);
    g_Table.u32Version        = VBOX_HGCM_SVC_VERSION;
    g_Table.pHelpers          = &g_Helpers;

    int const rc = VBoxHGCMSvcLoad(&g_Table);
    RTTESTI_CHECK_RC_RET(rc, VINF_SUCCESS, rc);
    RTTESTI_CHECK(g_Table.cbClient > 0);
    RTTESTI_CHECK(g_Table.pfnConnect != NULL);
    RTTESTI_CHECK(g_Table.pfnDisconnect != NULL);
    RTTESTI_CHECK(g_Table.pfnCall != NULL);
    RTTESTI_CHECK(g_Table.pfnHostCall != NULL);
    RTTESTI_CHECK(g_Table.pfnRegisterExtension != NULL);
    for (uintptr_t i = 0; i < RT_ELEMENTS(g_Table.acMaxClients); i++)
        RTTESTI_CHECK(g_Table.acMaxClients[i] == 1);

    RT_ZERO(g_Ext);
    return g_Table.pfnRegisterExtension(g_Table.pvService, tstExtension, &g_Ext);
}


/**
 * Checks connection ownership and the opaque transport supplied to Main.
 *
 * @param   ppvClient           Where to return the connected HGCM client state.
 */
static void tstConnection(void **ppvClient)
{
    RTTestISub("Client connection and transport");

    void *pvClient = RTMemAllocZ(g_Table.cbClient);
    void *pvOther  = RTMemAllocZ(g_Table.cbClient);
    if (!pvClient || !pvOther)
    {
        RTTestIFailed("Allocating HGCM client state failed");
        RTMemFree(pvOther);
        RTMemFree(pvClient);
        return;
    }

    int rc = g_Table.pfnConnect(g_Table.pvService, 1, pvClient, 0 /* fRequestor */, false /* fRestoring */);
    if (RT_FAILURE(rc))
    {
        RTTestIFailed("Connecting the first client failed: %Rrc", rc);
        RTMemFree(pvOther);
        RTMemFree(pvClient);
        return;
    }
    RTTESTI_CHECK(g_Ext.cConnect == 1);
    RTTESTI_CHECK(g_Ext.cSync == 1);

    rc = g_Table.pfnConnect(g_Table.pvService, 2, pvOther, 0 /* fRequestor */, false /* fRestoring */);
    RTTESTI_CHECK_RC(rc, VERR_RESOURCE_BUSY);
    RTTESTI_CHECK(g_Ext.cConnect == 1);
    if (RT_SUCCESS(rc))
    {
        int const rcDisconnect = g_Table.pfnDisconnect(g_Table.pvService, 2, pvOther);
        RTTESTI_CHECK_RC(rcDisconnect, VINF_SUCCESS);
        RTMemFree(pvOther);
        *ppvClient = pvClient;
        return;
    }
    RTMemFree(pvOther);

    *ppvClient = pvClient;
}


/**
 * Checks host policy propagation and guest feature negotiation.
 *
 * @param   pvClient            Connected HGCM client state.
 */
static void tstPolicyAndFeatures(void *pvClient)
{
    RTTestISub("Policy and feature negotiation");

    VBOXHGCMSVCPARM Parm;
    HGCMSvcSetU32(&Parm, VBOX_SHCL_MODE_BIDIRECTIONAL);
    int rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_MODE, 1, &Parm);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    SHCLFORMATS fFiltered = VBOX_SHCL_FMT_NONE;
    rc = g_Ext.Transport.pOps->pfnFilterFormats(g_Ext.Transport.hClient, true /* fHostToGuest */,
                                                VBOX_SHCL_FMT_URI_LIST, &fFiltered);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(fFiltered == VBOX_SHCL_FMT_NONE);

    uint32_t const cDataReadsBefore = g_Ext.cDataReads;
    uint8_t abData[8];
    VBOXHGCMSVCPARM aRead[3];
    HGCMSvcSetU32(&aRead[0], VBOX_SHCL_FMT_URI_LIST);
    HGCMSvcSetPv(&aRead[1], abData, sizeof(abData));
    HGCMSvcSetU32(&aRead[2], 0);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_DATA_READ, RT_ELEMENTS(aRead), aRead);
    RTTESTI_CHECK_RC(rc, VERR_ACCESS_DENIED);
    RTTESTI_CHECK(g_Ext.cDataReads == cDataReadsBefore);

    PSHCLEVENT pEvent = (PSHCLEVENT)(uintptr_t)1;
    rc = g_Ext.Transport.pOps->pfnReadDataFromGuestAsync(g_Ext.Transport.hClient,
                                                         VBOX_SHCL_FMT_URI_LIST, &pEvent);
    RTTESTI_CHECK_RC(rc, VERR_ACCESS_DENIED);
    RTTESTI_CHECK(pEvent == NULL);

    HGCMSvcSetU32(&Parm, VBOX_SHCL_TRANSFER_MODE_F_ENABLED);
    rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, &Parm);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
#endif

    uint64_t const fGuestFeatures0 = VBOX_SHCL_GF_0_CONTEXT_ID
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                                   | VBOX_SHCL_GF_0_TRANSFERS
#endif
                                   ;
    VBOXHGCMSVCPARM aParms[2];
    HGCMSvcSetU64(&aParms[0], fGuestFeatures0);
    HGCMSvcSetU64(&aParms[1], VBOX_SHCL_GF_1_MUST_BE_ONE);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FEATURES, RT_ELEMENTS(aParms), aParms);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(aParms[0].u.uint64 & VBOX_SHCL_HF_0_CONTEXT_ID);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    fFiltered = VBOX_SHCL_FMT_NONE;
    rc = g_Ext.Transport.pOps->pfnFilterFormats(g_Ext.Transport.hClient, true /* fHostToGuest */,
                                                VBOX_SHCL_FMT_URI_LIST, &fFiltered);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(fFiltered == VBOX_SHCL_FMT_URI_LIST);
#endif
}


/**
 * Checks representative hostile guest values at the HGCM protocol boundary.
 *
 * @param   pvClient            Connected HGCM client state.
 */
static void tstUntrustedGuestInput(void *pvClient)
{
    RTTestISub("Untrusted guest input");

    uint64_t const fGuestFeatures0 = VBOX_SHCL_GF_0_CONTEXT_ID
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                                   | VBOX_SHCL_GF_0_TRANSFERS
#endif
                                   ;
    int rc = tstGuestCall(pvClient, UINT32_MAX, 0, NULL);
    RTTESTI_CHECK_RC(rc, VERR_NOT_IMPLEMENTED);

    VBOXHGCMSVCPARM aFeatures[2];
    HGCMSvcSetU64(&aFeatures[0], fGuestFeatures0);
    rc = tstGuestCallUntrusted(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FEATURES, 1, aFeatures);
    RTTESTI_CHECK_RC(rc, VERR_WRONG_PARAMETER_COUNT);

    HGCMSvcSetU64(&aFeatures[0], fGuestFeatures0);
    HGCMSvcSetU32(&aFeatures[1], 0);
    rc = tstGuestCallUntrusted(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FEATURES, RT_ELEMENTS(aFeatures), aFeatures);
    RTTESTI_CHECK_RC(rc, VERR_WRONG_PARAMETER_TYPE);

    HGCMSvcSetU64(&aFeatures[0], fGuestFeatures0);
    HGCMSvcSetU64(&aFeatures[1], 0);
    rc = tstGuestCallUntrusted(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FEATURES, RT_ELEMENTS(aFeatures), aFeatures);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    uint32_t const cGuestFormatsBefore = g_Ext.cGuestFormats;
    VBOXHGCMSVCPARM Parm;
    HGCMSvcSetU64(&Parm, VBOX_SHCL_FMT_UNICODETEXT);
    rc = tstGuestCallUntrusted(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FORMATS, 1, &Parm);
    RTTESTI_CHECK_RC(rc, VERR_WRONG_PARAMETER_TYPE);

    HGCMSvcSetU32(&Parm, VBOX_SHCL_FMT_VALID_MASK + 1);
    rc = tstGuestCallUntrusted(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FORMATS, 1, &Parm);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_FLAGS);
    RTTESTI_CHECK(g_Ext.cGuestFormats == cGuestFormatsBefore);

    uint8_t abData[8];
    VBOXHGCMSVCPARM aRead[VBOX_SHCL_CPARMS_DATA_READ];
    HGCMSvcSetU32(&aRead[0], VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_HTML);
    HGCMSvcSetPv(&aRead[1], abData, sizeof(abData));
    HGCMSvcSetU32(&aRead[2], UINT32_MAX);
    uint32_t const cDataReadsBefore = g_Ext.cDataReads;
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_DATA_READ, RT_ELEMENTS(aRead), aRead);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    RTTESTI_CHECK(aRead[2].u.uint32 == UINT32_MAX);

    HGCMSvcSetU32(&aRead[0], VBOX_SHCL_FMT_UNICODETEXT);
    HGCMSvcSetU32(&aRead[1], 0);
    HGCMSvcSetU32(&aRead[2], UINT32_MAX);
    rc = tstGuestCallUntrusted(pvClient, VBOX_SHCL_GUEST_FN_DATA_READ, RT_ELEMENTS(aRead), aRead);
    RTTESTI_CHECK_RC(rc, VERR_WRONG_PARAMETER_TYPE);
    RTTESTI_CHECK(g_Ext.cDataReads == cDataReadsBefore);

    VBOXHGCMSVCPARM aWrite[VBOX_SHCL_CPARMS_DATA_WRITE];
    HGCMSvcSetU64(&aWrite[0], VBOX_SHCL_CONTEXTID_MAKE(UINT16_MAX, 0, 1));
    HGCMSvcSetU32(&aWrite[1], VBOX_SHCL_FMT_UNICODETEXT);
    HGCMSvcSetPv(&aWrite[2], abData, sizeof(abData));
    uint32_t const cDataWritesBefore = g_Ext.cDataWrites;
    rc = tstGuestCallUntrusted(pvClient, VBOX_SHCL_GUEST_FN_DATA_WRITE, RT_ELEMENTS(aWrite), aWrite);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_CONTEXT);

    HGCMSvcSetU32(&aWrite[1], VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_HTML);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_DATA_WRITE, RT_ELEMENTS(aWrite), aWrite);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    RTTESTI_CHECK(g_Ext.cDataWrites == cDataWritesBefore);

    rc = tstGuestCallUntrusted(pvClient, VBOX_SHCL_GUEST_FN_MSG_GET, 0, NULL);
    RTTESTI_CHECK_RC(rc, VERR_WRONG_PARAMETER_COUNT);
    HGCMSvcSetU32(&Parm, VBOX_SHCL_HOST_MSG_FORMATS_REPORT);
    rc = tstGuestCallUntrusted(pvClient, VBOX_SHCL_GUEST_FN_MSG_GET, 1, &Parm);
    RTTESTI_CHECK_RC(rc, VERR_WRONG_PARAMETER_COUNT);
}


/**
 * Checks one deferred host-to-guest message from wakeup through removal.
 *
 * @param   pvClient            Connected HGCM client state.
 */
static void tstMessageQueue(void *pvClient)
{
    RTTestISub("Deferred message queue");

    VBOXHGCMSVCPARM aPeek[2];
    HGCMSvcSetU32(&aPeek[0], 0);
    HGCMSvcSetU32(&aPeek[1], 0);
    VBOXHGCMCALLHANDLE_TYPEDEF Call;
    tstGuestCallStart(pvClient, VBOX_SHCL_GUEST_FN_MSG_PEEK_WAIT, RT_ELEMENTS(aPeek), aPeek, &Call);
    RTTESTI_CHECK(!Call.fCompleted);

    VBOXHGCMSVCPARM aOtherPeek[2];
    HGCMSvcSetU32(&aOtherPeek[0], 0);
    HGCMSvcSetU32(&aOtherPeek[1], 0);
    VBOXHGCMCALLHANDLE_TYPEDEF OtherCall;
    int rc = RTTestIDisableAssertions();
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    tstGuestCallStart(pvClient, VBOX_SHCL_GUEST_FN_MSG_PEEK_WAIT,
                      RT_ELEMENTS(aOtherPeek), aOtherPeek, &OtherCall);
    rc = RTTestIRestoreAssertions();
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(OtherCall.fCompleted);
    if (OtherCall.fCompleted)
        RTTESTI_CHECK_RC(OtherCall.rc, VERR_RESOURCE_BUSY);
    else
    {
        rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_CANCEL, 0, NULL);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        return;
    }

    SHCLFORMATS fReported = VBOX_SHCL_FMT_NONE;
    rc = g_Ext.Transport.pOps->pfnReportFormatsToGuest(g_Ext.Transport.hClient,
                                                        VBOX_SHCL_FMT_UNICODETEXT, &fReported);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(Call.fCompleted);
    if (!Call.fCompleted)
    {
        rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_CANCEL, 0, NULL);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        RTTESTI_CHECK(Call.fCompleted);
        return;
    }
    RTTESTI_CHECK_RC(Call.rc, VINF_SUCCESS);
    RTTESTI_CHECK(aPeek[0].u.uint32 == VBOX_SHCL_HOST_MSG_FORMATS_REPORT);
    RTTESTI_CHECK(aPeek[1].u.uint32 == 2);
    RTTESTI_CHECK(fReported == VBOX_SHCL_FMT_UNICODETEXT);

    VBOXHGCMSVCPARM aWrongGet[2];
    HGCMSvcSetU32(&aWrongGet[0], VBOX_SHCL_HOST_MSG_QUIT);
    HGCMSvcSetU32(&aWrongGet[1], 0);
    rc = tstGuestCallUntrusted(pvClient, VBOX_SHCL_GUEST_FN_MSG_GET, RT_ELEMENTS(aWrongGet), aWrongGet);
    RTTESTI_CHECK_RC(rc, VERR_MISMATCH);

    HGCMSvcSetU32(&aPeek[0], 0);
    HGCMSvcSetU32(&aPeek[1], 0);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_PEEK_NOWAIT, RT_ELEMENTS(aPeek), aPeek);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(aPeek[0].u.uint32 == VBOX_SHCL_HOST_MSG_FORMATS_REPORT);
    RTTESTI_CHECK(aPeek[1].u.uint32 == 2);

    VBOXHGCMSVCPARM aGet[2];
    HGCMSvcSetU32(&aGet[0], VBOX_SHCL_HOST_MSG_FORMATS_REPORT);
    HGCMSvcSetU32(&aGet[1], 0);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_GET, RT_ELEMENTS(aGet), aGet);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(aGet[0].u.uint32 == VBOX_SHCL_HOST_MSG_FORMATS_REPORT);
    RTTESTI_CHECK(aGet[1].u.uint32 == VBOX_SHCL_FMT_UNICODETEXT);

    HGCMSvcSetU32(&aPeek[0], 0);
    HGCMSvcSetU32(&aPeek[1], 0);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_PEEK_NOWAIT, RT_ELEMENTS(aPeek), aPeek);
    RTTESTI_CHECK_RC(rc, VERR_TRY_AGAIN);
}


/**
 * Checks service ownership of a valid guest clipboard-data reply context.
 *
 * @param   pvClient            Connected HGCM client state.
 */
static void tstGuestDataReply(void *pvClient)
{
    RTTestISub("Guest data reply context");

    PSHCLEVENT pEvent = NULL;
    int rc = g_Ext.Transport.pOps->pfnReadDataFromGuestAsync(g_Ext.Transport.hClient,
                                                             VBOX_SHCL_FMT_UNICODETEXT, &pEvent);
    RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);
    RTTESTI_CHECK_RETV(pEvent != NULL);

    VBOXHGCMSVCPARM aGet[2];
    HGCMSvcSetU64(&aGet[0], VBOX_SHCL_HOST_MSG_READ_DATA_CID);
    HGCMSvcSetU32(&aGet[1], 0);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_GET, RT_ELEMENTS(aGet), aGet);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    uint64_t const uContext = aGet[0].u.uint64;
    RTTESTI_CHECK(aGet[1].u.uint32 == VBOX_SHCL_FMT_UNICODETEXT);

    static uint8_t const s_abGuestData[] = { 'g', 'u', 'e', 's', 't', '\0' };
    VBOXHGCMSVCPARM aWrite[VBOX_SHCL_CPARMS_DATA_WRITE];
    HGCMSvcSetU64(&aWrite[0], uContext);
    HGCMSvcSetU32(&aWrite[1], VBOX_SHCL_FMT_UNICODETEXT);
    HGCMSvcSetPv(&aWrite[2], (void *)s_abGuestData, sizeof(s_abGuestData));
    uint32_t const cDataWritesBefore = g_Ext.cDataWrites;
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_DATA_WRITE, RT_ELEMENTS(aWrite), aWrite);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(g_Ext.cDataWrites == cDataWritesBefore + 1);

    PSHCLEVENTPAYLOAD pPayload = NULL;
    rc = ShClEventWait(pEvent, RT_MS_1SEC, &pPayload);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK(pPayload != NULL);
        if (pPayload)
        {
            RTTESTI_CHECK(pPayload->cbData == sizeof(s_abGuestData));
            RTTESTI_CHECK(memcmp(pPayload->pvData, s_abGuestData, sizeof(s_abGuestData)) == 0);
            ShClPayloadDestroy(pPayload);
        }
    }
    RTTESTI_CHECK(ShClEventRelease(pEvent) == 0);
}


/**
 * Checks the two central guest-to-host POD calls and mode enforcement.
 *
 * @param   pvClient            Connected HGCM client state.
 */
static void tstGuestPodCalls(void *pvClient)
{
    RTTestISub("Guest POD protocol");

    VBOXHGCMSVCPARM Parm;
    HGCMSvcSetU32(&Parm, VBOX_SHCL_FMT_UNICODETEXT);
    int rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FORMATS, 1, &Parm);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(g_Ext.cGuestFormats == 1);
    RTTESTI_CHECK(g_Ext.fGuestFormats == VBOX_SHCL_FMT_UNICODETEXT);

    uint8_t abData[sizeof(g_abHostData)];
    VBOXHGCMSVCPARM aRead[3];
    HGCMSvcSetU32(&aRead[0], VBOX_SHCL_FMT_UNICODETEXT);
    HGCMSvcSetPv(&aRead[1], abData, sizeof(abData));
    HGCMSvcSetU32(&aRead[2], 0);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_DATA_READ, RT_ELEMENTS(aRead), aRead);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(aRead[2].u.uint32 == sizeof(g_abHostData));
    RTTESTI_CHECK(memcmp(abData, g_abHostData, sizeof(g_abHostData)) == 0);

    uint8_t bData = 0;
    HGCMSvcSetPv(&aRead[1], &bData, sizeof(bData));
    HGCMSvcSetU32(&aRead[2], 0);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_DATA_READ, RT_ELEMENTS(aRead), aRead);
    RTTESTI_CHECK_RC(rc, VINF_BUFFER_OVERFLOW);
    RTTESTI_CHECK(aRead[2].u.uint32 == sizeof(g_abHostData));

    HGCMSvcSetU32(&Parm, VBOX_SHCL_MODE_HOST_TO_GUEST);
    rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_MODE, 1, &Parm);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    HGCMSvcSetU32(&Parm, VBOX_SHCL_FMT_UNICODETEXT);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FORMATS, 1, &Parm);
    RTTESTI_CHECK_RC(rc, VERR_ACCESS_DENIED);
    RTTESTI_CHECK(g_Ext.cGuestFormats == 1);
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** Initializes a guest transfer-status reply. */
static void tstTransferReplyInit(VBOXHGCMSVCPARM aParms[VBOX_SHCL_CPARMS_REPLY_MIN + 1],
                                 uint64_t uContext, SHCLTRANSFERSTATUS enmStatus)
{
    HGCMSvcSetU64(&aParms[0], uContext);
    HGCMSvcSetU32(&aParms[1], VBOX_SHCL_TX_REPLYMSGTYPE_TRANSFER_STATUS);
    HGCMSvcSetU32(&aParms[2], VINF_SUCCESS);
    HGCMSvcSetPv(&aParms[3], NULL, 0);
    HGCMSvcSetU32(&aParms[4], enmStatus);
}


/** Gets and checks one service-owned transfer status message. */
static void tstTransferStatusGet(void *pvClient, SHCLSESSIONID idSession, SHCLTRANSFERID idTransfer,
                                 SHCLTRANSFERSTATUS enmStatus, int rcTransfer)
{
    VBOXHGCMSVCPARM aParms[VBOX_SHCL_CPARMS_TRANSFER_STATUS];
    HGCMSvcSetU64(&aParms[0], VBOX_SHCL_HOST_MSG_TRANSFER_STATUS);
    HGCMSvcSetU32(&aParms[1], 0);
    HGCMSvcSetU32(&aParms[2], 0);
    HGCMSvcSetU32(&aParms[3], 0);
    HGCMSvcSetU32(&aParms[4], 0);

    int const rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_GET, RT_ELEMENTS(aParms), aParms);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    uint64_t const uContext = aParms[0].u.uint64;
    RTTESTI_CHECK(VBOX_SHCL_CONTEXTID_GET_SESSION(uContext) == idSession);
    RTTESTI_CHECK(VBOX_SHCL_CONTEXTID_GET_TRANSFER(uContext) == idTransfer);
    RTTESTI_CHECK(VBOX_SHCL_CONTEXTID_GET_EVENT(uContext) != 0);
    RTTESTI_CHECK(VBOX_SHCL_CONTEXTID_GET_EVENT(uContext) != NIL_SHCLEVENTID);
    RTTESTI_CHECK(aParms[1].u.uint32 == SHCLTRANSFERDIR_HOST_TO_GUEST);
    RTTESTI_CHECK(aParms[2].u.uint32 == enmStatus);
    RTTESTI_CHECK((int32_t)aParms[3].u.uint32 == rcTransfer);
    RTTESTI_CHECK(aParms[4].u.uint32 == 0);
}


/**
 * Checks transfer gates, hostile identifiers and one complete service-owned lifecycle.
 *
 * @param   pvClient            Connected HGCM client state.
 */
static void tstTransfers(void *pvClient)
{
    RTTestISub("Clipboard transfer protocol");

    VBOXHGCMSVCPARM aObjectClose[VBOX_SHCL_CPARMS_OBJ_CLOSE];
    HGCMSvcSetU64(&aObjectClose[0], VBOX_SHCL_CONTEXTID_MAKE(UINT16_MAX, 42, 0));
    HGCMSvcSetU64(&aObjectClose[1], 1);

    VBOXHGCMSVCPARM aFeatures[2];
    VBOXHGCMSVCPARM Parm;
    HGCMSvcSetU64(&aFeatures[0], VBOX_SHCL_GF_0_CONTEXT_ID);
    HGCMSvcSetU64(&aFeatures[1], VBOX_SHCL_GF_1_MUST_BE_ONE);
    int rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FEATURES,
                          RT_ELEMENTS(aFeatures), aFeatures);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    HGCMSvcSetU32(&Parm, VBOX_SHCL_TRANSFER_MODE_F_ENABLED);
    rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, &Parm);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    PSHCLTRANSFER pDeniedTransfer = (PSHCLTRANSFER)(uintptr_t)1;
    rc = g_Ext.Transport.pOps->pfnTransferCreate(g_Ext.Transport.hClient, SHCLTRANSFERDIR_GUEST_TO_HOST,
                                                  SHCLSOURCE_REMOTE, NULL, NIL_SHCLTRANSFERID, &pDeniedTransfer);
    RTTESTI_CHECK_RC(rc, VERR_ACCESS_DENIED);
    RTTESTI_CHECK(pDeniedTransfer == NULL);

    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(aObjectClose), aObjectClose);
    RTTESTI_CHECK_RC(rc, VERR_ACCESS_DENIED);

    HGCMSvcSetU64(&aFeatures[0], VBOX_SHCL_GF_0_CONTEXT_ID | VBOX_SHCL_GF_0_TRANSFERS);
    HGCMSvcSetU64(&aFeatures[1], VBOX_SHCL_GF_1_MUST_BE_ONE);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FEATURES,
                      RT_ELEMENTS(aFeatures), aFeatures);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    HGCMSvcSetU32(&Parm, VBOX_SHCL_TRANSFER_MODE_F_NONE);
    rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, &Parm);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    pDeniedTransfer = (PSHCLTRANSFER)(uintptr_t)1;
    rc = g_Ext.Transport.pOps->pfnTransferCreate(g_Ext.Transport.hClient, SHCLTRANSFERDIR_GUEST_TO_HOST,
                                                  SHCLSOURCE_REMOTE, NULL, NIL_SHCLTRANSFERID, &pDeniedTransfer);
    RTTESTI_CHECK_RC(rc, VERR_ACCESS_DENIED);
    RTTESTI_CHECK(pDeniedTransfer == NULL);

    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(aObjectClose), aObjectClose);
    RTTESTI_CHECK_RC(rc, VERR_ACCESS_DENIED);

    HGCMSvcSetU32(&Parm, VBOX_SHCL_TRANSFER_MODE_F_ENABLED);
    rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, &Parm);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    rc = tstGuestCallUntrusted(pvClient, VBOX_SHCL_GUEST_FN_OBJ_CLOSE, 0, NULL);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    HGCMSvcSetU32(&aObjectClose[0], 0);
    rc = tstGuestCallUntrusted(pvClient, VBOX_SHCL_GUEST_FN_OBJ_CLOSE,
                               RT_ELEMENTS(aObjectClose), aObjectClose);
    RTTESTI_CHECK_RC(rc, VERR_WRONG_PARAMETER_TYPE);

    HGCMSvcSetU64(&aObjectClose[0], 0);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(aObjectClose), aObjectClose);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_CONTEXT);

    VBOXHGCMSVCPARM aReply[VBOX_SHCL_CPARMS_REPLY_MIN + 1];
    tstTransferReplyInit(aReply, 0, SHCLTRANSFERSTATUS_REQUESTED);
    uint32_t const cQueriesBefore = g_Ext.cTransferCallbackQueries;
    uint32_t const cStatusesBefore = g_Ext.cTransferStatuses;
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPLY, VBOX_SHCL_CPARMS_REPLY_MIN, aReply);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    RTTESTI_CHECK(g_Ext.cTransferCallbackQueries == cQueriesBefore);
    RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatusesBefore);

    tstTransferReplyInit(aReply, 0, SHCLTRANSFERSTATUS_REQUESTED);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPLY, RT_ELEMENTS(aReply), aReply);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(g_Ext.cTransferCallbackQueries == cQueriesBefore + 1);
    RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatusesBefore + 1);
    SHCLSESSIONID const idSession = g_Ext.idTransferSession;
    RTTESTI_CHECK(idSession != NIL_SHCLSESSIONID);
    RTTESTI_CHECK(g_Ext.idTransfer != NIL_SHCLTRANSFERID);
    RTTESTI_CHECK(g_Ext.uTransferGeneration != NIL_SHCLTRANSFERGEN);
    RTTESTI_CHECK(g_Ext.enmTransferDir == SHCLTRANSFERDIR_HOST_TO_GUEST);
    RTTESTI_CHECK(g_Ext.enmTransferSource == SHCLSOURCE_REMOTE);
    RTTESTI_CHECK(g_Ext.enmTransferStatus == SHCLTRANSFERSTATUS_REQUESTED);
    RTTESTI_CHECK(g_Ext.rcTransfer == VINF_SUCCESS);

    uint32_t const cQueriesAfterRequest = g_Ext.cTransferCallbackQueries;
    uint32_t const cStatusesAfterRequest = g_Ext.cTransferStatuses;
    tstTransferReplyInit(aReply, VBOX_SHCL_CONTEXTID_MAKE(idSession, g_Ext.idTransfer + 1, 0),
                         SHCLTRANSFERSTATUS_REQUESTED);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPLY, RT_ELEMENTS(aReply), aReply);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_CONTEXT);
    RTTESTI_CHECK(g_Ext.cTransferCallbackQueries == cQueriesAfterRequest);
    RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatusesAfterRequest);

    HGCMSvcSetU64(&aObjectClose[0], VBOX_SHCL_CONTEXTID_MAKE(idSession + 1, g_Ext.idTransfer, 0));
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(aObjectClose), aObjectClose);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_CONTEXT);
    HGCMSvcSetU64(&aObjectClose[0], VBOX_SHCL_CONTEXTID_MAKE(idSession, g_Ext.idTransfer + 1, 0));
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(aObjectClose), aObjectClose);
    RTTESTI_CHECK_RC(rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);

    tstTransferStatusGet(pvClient, idSession, g_Ext.idTransfer, SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);

    PSHCLTRANSFER pTransfer = g_Ext.Transport.pOps->pfnTransferGetByIdRetained(g_Ext.Transport.hClient,
                                                                               g_Ext.idTransfer);
    RTTESTI_CHECK(pTransfer != NULL);
    if (pTransfer)
    {
        HGCMSvcSetU64(&aFeatures[0], VBOX_SHCL_GF_0_CONTEXT_ID);
        HGCMSvcSetU64(&aFeatures[1], VBOX_SHCL_GF_1_MUST_BE_ONE);
        rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FEATURES,
                          RT_ELEMENTS(aFeatures), aFeatures);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        rc = g_Ext.Transport.pOps->pfnTransferInit(g_Ext.Transport.hClient, pTransfer);
        RTTESTI_CHECK_RC(rc, VERR_ACCESS_DENIED);

        HGCMSvcSetU64(&aFeatures[0], VBOX_SHCL_GF_0_CONTEXT_ID | VBOX_SHCL_GF_0_TRANSFERS);
        HGCMSvcSetU64(&aFeatures[1], VBOX_SHCL_GF_1_MUST_BE_ONE);
        rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FEATURES,
                          RT_ELEMENTS(aFeatures), aFeatures);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        ShClTransferRelease(pTransfer);
    }

    VBOXHGCMSVCPARM aCancel[2];
    HGCMSvcSetU64(&aCancel[0], VBOX_SHCL_CONTEXTID_MAKE(idSession, g_Ext.idTransfer, 0));
    HGCMSvcSetU64(&aCancel[1], g_Ext.uTransferGeneration + 1);
    rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_CANCEL, RT_ELEMENTS(aCancel), aCancel);
    RTTESTI_CHECK_RC(rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);

    HGCMSvcSetU64(&aCancel[1], g_Ext.uTransferGeneration);
    rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_CANCEL, RT_ELEMENTS(aCancel), aCancel);
    RTTESTI_CHECK_RC_OK(rc);
    tstTransferStatusGet(pvClient, idSession, g_Ext.idTransfer, SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);

    HGCMSvcSetU64(&aObjectClose[0], VBOX_SHCL_CONTEXTID_MAKE(idSession, g_Ext.idTransfer, 0));
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(aObjectClose), aObjectClose);
    RTTESTI_CHECK_RC(rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);
    rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_CANCEL, RT_ELEMENTS(aCancel), aCancel);
    RTTESTI_CHECK_RC(rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


/**
 * Disconnects the client and unloads the service.
 *
 * @param   pvClient            Connected HGCM client state.  May be NULL.
 */
static void tstShutdown(void *pvClient)
{
    RTTestISub("Service shutdown");

    int rc;
    if (pvClient)
    {
        rc = g_Table.pfnDisconnect(g_Table.pvService, 1, pvClient);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        RTTESTI_CHECK(g_Ext.cDisconnect == 1);
        RTMemFree(pvClient);
    }

    rc = g_Table.pfnRegisterExtension(g_Table.pvService, NULL, NULL);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    rc = g_Table.pfnUnload(g_Table.pvService);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}


/** Testcase entry point. */
int main(void)
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstClipboardHostService", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    void *pvClient = NULL;
    int const rc = tstLoadService();
    if (RT_SUCCESS(rc))
    {
        tstConnection(&pvClient);
        if (pvClient)
        {
            tstPolicyAndFeatures(pvClient);
            tstUntrustedGuestInput(pvClient);
            tstMessageQueue(pvClient);
            tstGuestDataReply(pvClient);
            tstGuestPodCalls(pvClient);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            tstTransfers(pvClient);
#endif
        }
        tstShutdown(pvClient);
    }
    else
        RTTestIFailed("Loading the host service failed: %Rrc", rc);

    return RTTestSummaryAndDestroy(g_hTest);
}
