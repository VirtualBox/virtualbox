/* $Id: tstClipboardHostService.cpp 115134 2026-08-27 15:09:45Z andreas.loeffler@oracle.com $ */
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

#include "../VBoxSharedClipboardSvc-internal.h"
#include "../VBoxSharedClipboardSvc-transfers.h"

#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/process.h>
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
    /** Connected synthetic HGCM client used for callback-order checks. */
    void                       *pvClient;
    /** Transport received with the latest client connection. */
    SHCLTRANSPORT               Transport;
    /** Number of connect notifications. */
    uint32_t                    cConnect;
    /** Number of disconnect notifications. */
    uint32_t                    cDisconnect;
    /** Number of synchronization notifications. */
    uint32_t                    cSync;
    /** Number of errors reported for display. */
    uint32_t                    cReportedErrors;
    /** Result code of the last error reported for display. */
    int                         rcReportedError;
    /** Number of native backend destruction notifications. */
    uint32_t                    cBackendDestroys;
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
    /** Number of deliberately failed transfer-status notifications. */
    uint32_t                    cTransferStatusFailures;
    /** Exact identity in the last transfer-status notification. */
    SHCLTRANSFERKEY             TransferKey;
    /** Direction of the last transfer-status notification. */
    SHCLTRANSFERDIR             enmTransferDir;
    /** Source of the last transfer-status notification. */
    SHCLSOURCE                  enmTransferSource;
    /** Endpoint which supplied the last transfer-status notification. */
    SHCLSOURCE                  enmTransferReplySource;
    /** Last transfer status reported by the guest. */
    SHCLTRANSFERSTATUS          enmTransferStatus;
    /** Transfer status immediately preceding the last notification. */
    SHCLTRANSFERSTATUS          enmPreviousTransferStatus;
    /** Last transfer result reported by the guest. */
    int                         rcTransfer;
    /** Transfer result immediately preceding the last notification. */
    int                         rcPreviousTransfer;
    /** Failing transfer-relative path in the last status notification. */
    char                        szTransferPath[SHCL_TRANSFER_PATH_MAX];
    /** Number of exact aggregate transfer-progress notifications. */
    uint32_t                    cTransferProgress;
    /** Number of transport-scoped transfer-reset notifications. */
    uint32_t                    cTransferResets;
    /** Exact identity in the last progress notification. */
    SHCLTRANSFERKEY             ProgressKey;
    /** Processed bytes in the last progress notification. */
    uint64_t                    cbProgressProcessed;
    /** Exact total bytes in the last progress notification. */
    uint64_t                    cbProgressTotal;
    /** Whether the next transfer-status notification shall fail. */
    bool                        fFailNextTransferStatus;
    /** Whether a terminal callback shall verify that no guest status is queued yet. */
    bool                        fCheckTerminalBeforeGuest;
    /** Whether the terminal callback ran before its guest status became visible. */
    bool                        fTerminalBeforeGuest;
#endif
} TSTCLEXT;


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** State recorded by terminal transfer callbacks. */
typedef struct TSTTRANSFERTERMINAL
{
    /** Number of completion callbacks. */
    uint32_t                    cCompleted;
    /** Number of error callbacks. */
    uint32_t                    cErrors;
    /** Result supplied to the callback. */
    int                         rcCallback;
    /** Whether the callback was invoked while the transfer lock was held. */
    bool                        fLockOwned;
} TSTTRANSFERTERMINAL;

/** Result captured by a transfer-event waiter. */
typedef struct TSTTRANSFEREVENTWAITER
{
    /** Event to wait for. */
    PSHCLEVENT                  pEvent;
    /** Result from ShClEventWaitEx(). */
    int                         rcWait;
    /** Result carried by the signalled event. */
    int                         rcEvent;
    /** Whether the waiter unexpectedly received a payload. */
    bool                        fPayload;
} TSTTRANSFEREVENTWAITER;
/** Pointer to a transfer-event waiter result. */
typedef TSTTRANSFEREVENTWAITER *PTSTTRANSFEREVENTWAITER;

/** Mock local provider state for an aggregate object-read request. */
typedef struct TSTOBJREADPROVIDER
{
    /** Total amount of mock object data. */
    uint32_t                    cbData;
    /** Current mock object offset. */
    uint32_t                    offData;
    /** Maximum size accepted by a single provider read. */
    uint32_t                    cbMaxChunkSize;
    /** Number of provider reads. */
    uint32_t                    cReads;
    /** Requested sizes of the first provider reads. */
    uint32_t                    acbReads[4];
} TSTOBJREADPROVIDER;
/** Pointer to mock aggregate object-read provider state. */
typedef TSTOBJREADPROVIDER *PTSTOBJREADPROVIDER;

#endif


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
static int tstGuestCall(void *pvClient, uint32_t uFunction, uint32_t cParms, VBOXHGCMSVCPARM *paParms);

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
 * @retval  VERR_INVALID_PARAMETER  if the callback parameters violate the test contract.
 * @retval  VERR_NO_MEMORY          when the test requests a simulated status-publication failure.
 * @retval  VERR_NOT_SUPPORTED      if @a uFunction is not implemented by the test extension.
 * @param   pvExtension             Test extension state.
 * @param   uFunction               VBOX_CLIPBOARD_EXT_FN_XXX function number.
 * @param   pvParms                 Service-extension parameters.
 * @param   cbParms                 Size of @a pvParms in bytes.
 */
static DECLCALLBACK(int) tstExtension(void *pvExtension, uint32_t uFunction, void *pvParms, uint32_t cbParms)
{
    TSTCLEXT * const pExt = (TSTCLEXT *)pvExtension;
    RTTESTI_CHECK_RET(cbParms == sizeof(SHCLEXTPARMS), VERR_INVALID_PARAMETER);
    PSHCLEXTPARMS const pParms = (PSHCLEXTPARMS)pvParms;

    switch (uFunction)
    {
        case VBOX_CLIPBOARD_EXT_FN_ERROR:
            RTTESTI_CHECK_RET(pParms->u.Error.pszMsg != NULL, VERR_INVALID_PARAMETER);
            RTTESTI_CHECK_RET(RT_FAILURE(pParms->u.Error.rc), VERR_INVALID_PARAMETER);
            pExt->cReportedErrors++;
            pExt->rcReportedError = pParms->u.Error.rc;
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT:
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY:
            if (ShClTransportIsValid(&pExt->Transport))
            {
                RT_ZERO(pExt->Transport);
                pExt->cDisconnect++;
            }
            pExt->cBackendDestroys++;
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
            RT_ZERO(pExt->Transport);
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
            return VINF_SUCCESS;
        }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        case VBOX_CLIPBOARD_EXT_FN_TRANSFER_CALLBACKS:
            RTTESTI_CHECK_RET(pParms->u.TransferCallbacks.pCallbacks != NULL, VERR_INVALID_PARAMETER);
            RT_ZERO(*pParms->u.TransferCallbacks.pCallbacks);
            pExt->cTransferCallbackQueries++;
            return VINF_SUCCESS;

        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER:
        {
            SHCLTRANSPORT const Transport = pParms->u.FileTransferData.Transport;
            RTTESTI_CHECK_RET(ShClTransportIsEqual(&pExt->Transport, &Transport), VERR_INVALID_PARAMETER);
            RTTESTI_CHECK_RET(ShClTransferKeyIsValid(&pParms->u.FileTransferData.Key),
                              VERR_INVALID_PARAMETER);
            RTTESTI_CHECK_RET(ShClTransferDirIsValid(pParms->u.FileTransferData.enmDir), VERR_INVALID_PARAMETER);
            RTTESTI_CHECK_RET(ShClSourceIsValid(pParms->u.FileTransferData.enmTransferSource), VERR_INVALID_PARAMETER);
            RTTESTI_CHECK_RET(ShClSourceIsValid(pParms->u.FileTransferData.enmReplySource), VERR_INVALID_PARAMETER);
            RTTESTI_CHECK_RET(ShClTransferStatusResultIsValid(pParms->u.FileTransferData.enmStatus,
                                                              pParms->u.FileTransferData.rcStatus),
                              VERR_INVALID_PARAMETER);

            if (   pExt->fCheckTerminalBeforeGuest
                && ShClTransferStatusIsTerminal(pParms->u.FileTransferData.enmStatus))
            {
                VBOXHGCMSVCPARM aPeek[2];
                HGCMSvcSetU32(&aPeek[0], 0);
                HGCMSvcSetU32(&aPeek[1], 0);
                int const rcPeek = tstGuestCall(pExt->pvClient, VBOX_SHCL_GUEST_FN_MSG_PEEK_NOWAIT,
                                                RT_ELEMENTS(aPeek), aPeek);
                pExt->fTerminalBeforeGuest = rcPeek == VERR_TRY_AGAIN;
                RTTESTI_CHECK_RC(rcPeek, VERR_TRY_AGAIN);
            }

            if (pExt->fFailNextTransferStatus)
            {
                pExt->fFailNextTransferStatus = false;
                pExt->cTransferStatusFailures++;
                return VERR_NO_MEMORY;
            }

            pExt->TransferKey                 = pParms->u.FileTransferData.Key;
            pExt->enmTransferDir             = pParms->u.FileTransferData.enmDir;
            pExt->enmTransferSource          = pParms->u.FileTransferData.enmTransferSource;
            pExt->enmTransferReplySource     = pParms->u.FileTransferData.enmReplySource;
            pExt->enmPreviousTransferStatus = pExt->enmTransferStatus;
            pExt->rcPreviousTransfer         = pExt->rcTransfer;
            pExt->enmTransferStatus          = pParms->u.FileTransferData.enmStatus;
            pExt->rcTransfer                 = pParms->u.FileTransferData.rcStatus;
            pExt->szTransferPath[0]          = '\0';
            if (pParms->u.FileTransferData.pszPath)
                RTTESTI_CHECK_RC_RET(RTStrCopy(pExt->szTransferPath, sizeof(pExt->szTransferPath),
                                               pParms->u.FileTransferData.pszPath),
                                     VINF_SUCCESS, VERR_INVALID_PARAMETER);
            if (pParms->u.FileTransferData.enmReplySource == SHCLSOURCE_REMOTE)
                RTTESTI_CHECK_RET(pParms->u.FileTransferData.pszPath == NULL, VERR_INVALID_PARAMETER);
            pExt->cTransferStatuses++;
            return VINF_SUCCESS;
        }

        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER_PROGRESS:
        {
            SHCLTRANSPORT const Transport = pParms->u.FileTransferProgress.Transport;
            RTTESTI_CHECK_RET(ShClTransportIsEqual(&pExt->Transport, &Transport), VERR_INVALID_PARAMETER);
            RTTESTI_CHECK_RET(ShClTransferKeyIsValid(&pParms->u.FileTransferProgress.Key),
                              VERR_INVALID_PARAMETER);
            RTTESTI_CHECK_RET(pParms->u.FileTransferProgress.cbTotal > 0, VERR_INVALID_PARAMETER);
            RTTESTI_CHECK_RET(pParms->u.FileTransferProgress.cbProcessed
                           <= pParms->u.FileTransferProgress.cbTotal, VERR_INVALID_PARAMETER);

            pExt->ProgressKey         = pParms->u.FileTransferProgress.Key;
            pExt->cbProgressProcessed = pParms->u.FileTransferProgress.cbProcessed;
            pExt->cbProgressTotal     = pParms->u.FileTransferProgress.cbTotal;
            pExt->cTransferProgress++;
            return VINF_SUCCESS;
        }

        case VBOX_CLIPBOARD_EXT_FN_FILE_TRANSFER_RESET:
        {
            SHCLTRANSPORT const Transport = pParms->u.FileTransferReset.Transport;
            RTTESTI_CHECK_RET(ShClTransportIsEqual(&pExt->Transport, &Transport), VERR_INVALID_PARAMETER);
            pExt->cTransferResets++;
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

    g_Ext.pvClient = pvClient;
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
    HGCMSvcSetU32(&aWrite[1], VBOX_SHCL_FMT_HTML);
    HGCMSvcSetPv(&aWrite[2], (void *)s_abGuestData, sizeof(s_abGuestData));
    uint32_t const cDataWritesBefore = g_Ext.cDataWrites;
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_DATA_WRITE, RT_ELEMENTS(aWrite), aWrite);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_CONTEXT);
    RTTESTI_CHECK(g_Ext.cDataWrites == cDataWritesBefore);

    HGCMSvcSetU32(&aWrite[1], VBOX_SHCL_FMT_UNICODETEXT);
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

    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_DATA_WRITE, RT_ELEMENTS(aWrite), aWrite);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(g_Ext.cDataWrites == cDataWritesBefore + 1);
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
/** Records a cancellation/completion callback and its lock context. */
static DECLCALLBACK(void) tstTransferCompleted(PSHCLTRANSFERCALLBACKCTX pCbCtx, int rcCompletion)
{
    TSTTRANSFERTERMINAL * const pState = (TSTTRANSFERTERMINAL *)pCbCtx->pvUser;
    pState->cCompleted++;
    pState->rcCallback = rcCompletion;
    pState->fLockOwned = RTCritSectIsOwner(&pCbCtx->pTransfer->CritSect);
}


/** Records an error callback and its lock context. */
static DECLCALLBACK(void) tstTransferError(PSHCLTRANSFERCALLBACKCTX pCbCtx, int rcError)
{
    TSTTRANSFERTERMINAL * const pState = (TSTTRANSFERTERMINAL *)pCbCtx->pvUser;
    pState->cErrors++;
    pState->rcCallback = rcError;
    pState->fLockOwned = RTCritSectIsOwner(&pCbCtx->pTransfer->CritSect);
}


/** Waits for a transfer event until cancellation wakes it. */
static DECLCALLBACK(int) tstTransferEventWaiter(RTTHREAD hThreadSelf, void *pvUser)
{
    PTSTTRANSFEREVENTWAITER const pWaiter = (PTSTTRANSFEREVENTWAITER)pvUser;
    AssertPtrReturn(pWaiter, VERR_INVALID_POINTER);
    AssertPtrReturn(pWaiter->pEvent, VERR_INVALID_POINTER);

    int rc = RTThreadUserSignal(hThreadSelf);
    AssertRCReturn(rc, rc);

    PSHCLEVENTPAYLOAD pPayload = NULL;
    pWaiter->rcWait = ShClEventWaitEx(pWaiter->pEvent, RT_MS_30SEC, &pWaiter->rcEvent, &pPayload);
    pWaiter->fPayload = pPayload != NULL;
    ShClPayloadDestroy(pPayload);
    return VINF_SUCCESS;
}


/** Checks that a transfer root immediately below a filesystem root keeps its full name. */
static void tstTransferRootsFsRoot(void)
{
    RTTestISub("Transfer root below a filesystem root");

#ifdef RT_OS_WINDOWS
    static const char s_szRoots[] = "C:\\VBoxRootFile.txt\r\n";
#else
    static const char s_szRoots[] = "/VBoxRootFile.txt\r\n";
#endif

    /* Use a remote transfer so that the synthetic root path need not exist. */
    PSHCLTRANSFER pTransfer = NULL;
    int rc = ShClTransferCreate(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE,
                                NULL /* pCallbacks */, &pTransfer);
    RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);

    rc = ShClTransferRootsSetFromStringList(pTransfer, s_szRoots, sizeof(s_szRoots));
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK(ShClTransferRootsCount(pTransfer) == 1);

        PCSHCLLISTENTRY pEntry = ShClTransferRootsEntryGet(pTransfer, 0);
        RTTESTI_CHECK(pEntry != NULL);
        if (pEntry)
            RTTESTI_CHECK_MSG(RTStrCmp(pEntry->pszName, "VBoxRootFile.txt") == 0,
                              ("pszName=%s\n", pEntry->pszName));
    }

    RTTESTI_CHECK_RC(ShClTransferDestroy(pTransfer), VINF_SUCCESS);
}


/** Checks transfer-event cancellation, first-wins signalling and source reset. */
static void tstTransferEventCancellation(void)
{
    RTTestISub("Transfer event cancellation");

    PSHCLTRANSFER pTransfer = NULL;
    int rc = ShClTransferCreate(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE,
                                NULL /* pCallbacks */, &pTransfer);
    RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(ShClTransferInit(pTransfer), VINF_SUCCESS);

    PSHCLEVENT pEvent = NULL;
    rc = ShClEventSourceGenerateAndRegisterEvent(&pTransfer->Events, &pEvent);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(pEvent != NULL);

    TSTTRANSFEREVENTWAITER Waiter;
    RT_ZERO(Waiter);
    Waiter.pEvent  = pEvent;
    Waiter.rcWait  = VERR_IPE_UNINITIALIZED_STATUS;
    Waiter.rcEvent = VERR_IPE_UNINITIALIZED_STATUS;

    RTTHREAD hThread = NIL_RTTHREAD;
    if (pEvent)
        rc = RTThreadCreate(&hThread, tstTransferEventWaiter, &Waiter, 0 /* cbStack */,
                            RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "shclevtwait");
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        rc = RTThreadUserWait(hThread, RT_MS_1SEC);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    if (RT_SUCCESS(rc))
        RTTESTI_CHECK_RC(ShClTransferCancel(pTransfer), VINF_SUCCESS);

    int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
    if (hThread != NIL_RTTHREAD)
    {
        rc = RTThreadWait(hThread, RT_MS_1SEC, &rcThread);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        RTTESTI_CHECK_RC(rcThread, VINF_SUCCESS);
    }
    RTTESTI_CHECK_RC(Waiter.rcWait, VERR_SHCLPB_EVENT_FAILED);
    RTTESTI_CHECK_RC(Waiter.rcEvent, VERR_CANCELLED);
    RTTESTI_CHECK(!Waiter.fPayload);

    static uint8_t const s_abLateReply[] = { 0x55, 0xaa };
    PSHCLEVENTPAYLOAD pPayload = NULL;
    RTTESTI_CHECK_RC(ShClPayloadCreateDupData(1, s_abLateReply, sizeof(s_abLateReply), &pPayload), VINF_SUCCESS);
    if (pPayload)
    {
        RTTESTI_CHECK_RC(ShClEventSignalEx(pEvent, VINF_SUCCESS, pPayload), VERR_WRONG_ORDER);
        ShClPayloadDestroy(pPayload); /* A losing producer retains its payload. */
    }

    PSHCLEVENT pLateEvent = NULL;
    RTTESTI_CHECK_RC(ShClEventSourceGenerateAndRegisterEvent(&pTransfer->Events, &pLateEvent), VERR_CANCELLED);
    RTTESTI_CHECK(pLateEvent == NULL);

    if (pEvent)
        RTTESTI_CHECK(ShClEventRelease(pEvent) == 0);
    RTTESTI_CHECK_RC(ShClTransferDestroy(pTransfer), VINF_SUCCESS);

    /* A reply already delivered when cancellation arrives remains authoritative. */
    pTransfer = NULL;
    RTTESTI_CHECK_RC_RETV(ShClTransferCreate(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE,
                                             NULL /* pCallbacks */, &pTransfer), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(ShClTransferInit(pTransfer), VINF_SUCCESS);
    pEvent = NULL;
    RTTESTI_CHECK_RC_RETV(ShClEventSourceGenerateAndRegisterEvent(&pTransfer->Events, &pEvent), VINF_SUCCESS);
    pPayload = NULL;
    RTTESTI_CHECK_RC_RETV(ShClPayloadCreateDupData(2, s_abLateReply, sizeof(s_abLateReply), &pPayload), VINF_SUCCESS);
    RTTESTI_CHECK_RC_RETV(ShClEventSignalEx(pEvent, VINF_SUCCESS, pPayload), VINF_SUCCESS);
    pPayload = NULL; /* The event owns it now. */
    RTTESTI_CHECK_RC(ShClTransferCancel(pTransfer), VINF_SUCCESS);

    int rcEvent = VERR_IPE_UNINITIALIZED_STATUS;
    RTTESTI_CHECK_RC(ShClEventWaitEx(pEvent, RT_MS_1SEC, &rcEvent, &pPayload), VINF_SUCCESS);
    RTTESTI_CHECK_RC(rcEvent, VINF_SUCCESS);
    RTTESTI_CHECK(pPayload != NULL);
    if (pPayload)
    {
        RTTESTI_CHECK(pPayload->cbData == sizeof(s_abLateReply));
        RTTESTI_CHECK(memcmp(pPayload->pvData, s_abLateReply, sizeof(s_abLateReply)) == 0);
        ShClPayloadDestroy(pPayload);
    }
    RTTESTI_CHECK(ShClEventRelease(pEvent) == 0);
    RTTESTI_CHECK_RC(ShClTransferDestroy(pTransfer), VINF_SUCCESS);

    /* Reset starts a fresh source epoch and clears the latched error. */
    SHCLEVENTSOURCE EventSource;
    RTTESTI_CHECK_RC_RETV(ShClEventSourceInit(&EventSource, 42), VINF_SUCCESS);
    RTTESTI_CHECK_RC(ShClEventSourceSignalAll(&EventSource, VERR_CANCELLED), VINF_SUCCESS);
    pEvent = NULL;
    RTTESTI_CHECK_RC(ShClEventSourceGenerateAndRegisterEvent(&EventSource, &pEvent), VERR_CANCELLED);
    RTTESTI_CHECK(pEvent == NULL);
    ShClEventSourceReset(&EventSource);
    RTTESTI_CHECK_RC_RETV(ShClEventSourceGenerateAndRegisterEvent(&EventSource, &pEvent), VINF_SUCCESS);
    RTTESTI_CHECK_RC(ShClEventSignalEx(pEvent, VERR_ACCESS_DENIED, NULL /* pPayload */), VINF_SUCCESS);
    rcEvent = VINF_SUCCESS;
    RTTESTI_CHECK_RC(ShClEventWaitEx(pEvent, RT_MS_1SEC, &rcEvent, NULL /* ppPayload */),
                      VERR_SHCLPB_EVENT_FAILED);
    RTTESTI_CHECK_RC(rcEvent, VERR_ACCESS_DENIED);
    RTTESTI_CHECK(ShClEventRelease(pEvent) == 0);
    RTTESTI_CHECK_RC(ShClEventSourceTerm(&EventSource), VINF_SUCCESS);
}


/** Checks normalized host-side transfer key helpers. */
static void tstTransferKeyHelpers(void)
{
    RTTestISub("Transfer key helpers");

    SHCLTRANSFERKEY Key;
    ShClTransferKeyInit(&Key, 42, 23, 7);
    RTTESTI_CHECK(Key.uContextId == VBOX_SHCL_CONTEXTID_MAKE(42, 23, 0));
    RTTESTI_CHECK(Key.uGeneration == 7);
    RTTESTI_CHECK(ShClTransferKeyIsValid(&Key));
    RTTESTI_CHECK(ShClTransferKeyGetSessionId(&Key) == 42);
    RTTESTI_CHECK(ShClTransferKeyGetTransferId(&Key) == 23);

    SHCLTRANSFERKEY OtherKey = Key;
    RTTESTI_CHECK(ShClTransferKeyIsEqual(&Key, &OtherKey));
    OtherKey.uContextId = VBOX_SHCL_CONTEXTID_MAKE(42, 23, 1);
    RTTESTI_CHECK(!ShClTransferKeyIsValid(&OtherKey));
    RTTESTI_CHECK(!ShClTransferKeyIsEqual(&Key, &OtherKey));

    ShClTransferKeyReset(&OtherKey);
    RTTESTI_CHECK(!ShClTransferKeyIsValid(&OtherKey));
    RTTESTI_CHECK(OtherKey.uContextId == 0);
    RTTESTI_CHECK(OtherKey.uGeneration == NIL_SHCLTRANSFERGEN);
}


/** Checks native aggregate progress accounting and terminal callback lock order. */
static void tstTransferProgressHelpers(void)
{
    RTTestISub("Transfer progress helpers");

    PSHCLTRANSFER pTransfer = NULL;
    int rc = ShClTransferCreate(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE,
                                NULL /* pCallbacks */, &pTransfer);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        uint64_t cbProcessed = UINT64_MAX;
        uint64_t cbTotal     = UINT64_MAX;
        bool fNotify = true;

        RTTESTI_CHECK_RC(ShClTransferProgressAdd(pTransfer, 1, &cbProcessed, &cbTotal, &fNotify),
                         VERR_NOT_AVAILABLE);
        RTTESTI_CHECK(cbProcessed == 0);
        RTTESTI_CHECK(cbTotal == 0);
        RTTESTI_CHECK(!fNotify);
        RTTESTI_CHECK(pTransfer->Progress.cbProcessed == 1);
        RTTESTI_CHECK_RC(ShClTransferProgressSetTotal(pTransfer, 0), VERR_TOO_MUCH_DATA);
        RTTESTI_CHECK_RC(ShClTransferProgressSetTotal(pTransfer, 1000), VINF_SUCCESS);
        RTTESTI_CHECK_RC(ShClTransferProgressSetTotal(pTransfer, 1000), VINF_SUCCESS);
        RTTESTI_CHECK_RC(ShClTransferProgressSetTotal(pTransfer, 1001), VERR_WRONG_ORDER);

        RTTESTI_CHECK_RC(ShClTransferProgressAdd(pTransfer, 1, &cbProcessed, &cbTotal, &fNotify), VINF_SUCCESS);
        RTTESTI_CHECK(cbProcessed == 2);
        RTTESTI_CHECK(cbTotal == 1000);
        RTTESTI_CHECK(fNotify); /* First known positive snapshot, still 0%. */
        RTTESTI_CHECK_RC(ShClTransferProgressAdd(pTransfer, 1, &cbProcessed, &cbTotal, &fNotify), VINF_SUCCESS);
        RTTESTI_CHECK(!fNotify);
        RTTESTI_CHECK_RC(ShClTransferProgressAdd(pTransfer, 7, &cbProcessed, &cbTotal, &fNotify), VINF_SUCCESS);
        RTTESTI_CHECK(cbProcessed == 10);
        RTTESTI_CHECK(fNotify); /* 1%. */
        RTTESTI_CHECK_RC(ShClTransferProgressAdd(pTransfer, 1, &cbProcessed, &cbTotal, &fNotify), VINF_SUCCESS);
        RTTESTI_CHECK(!fNotify);
        RTTESTI_CHECK_RC(ShClTransferProgressAdd(pTransfer, 10, &cbProcessed, &cbTotal, &fNotify), VINF_SUCCESS);
        RTTESTI_CHECK(cbProcessed == 21);
        RTTESTI_CHECK(fNotify); /* 2%. */
        RTTESTI_CHECK_RC(ShClTransferProgressAdd(pTransfer, 980, &cbProcessed, &cbTotal, &fNotify),
                         VERR_TOO_MUCH_DATA);
        RTTESTI_CHECK(pTransfer->Progress.cbProcessed == 21);
        RTTESTI_CHECK_RC(ShClTransferProgressAdd(pTransfer, 979, &cbProcessed, &cbTotal, &fNotify), VINF_SUCCESS);
        RTTESTI_CHECK(cbProcessed == cbTotal);
        RTTESTI_CHECK(fNotify); /* Last active snapshot is 99%; terminal status owns 100%. */

        ShClTransferReset(pTransfer);
        RTTESTI_CHECK(pTransfer->Progress.cbProcessed == 0);
        RTTESTI_CHECK(pTransfer->Progress.cbTotal == 0);
        RTTESTI_CHECK(!pTransfer->Progress.fTotalKnown);
        RTTESTI_CHECK(!pTransfer->Progress.fReportedAny);
        RTTESTI_CHECK(pTransfer->Progress.uLastPercent == 0);

        RTTESTI_CHECK_RC(ShClTransferProgressSetTotal(pTransfer, 1), VINF_SUCCESS);
        RTTESTI_CHECK_RC(ShClTransferProgressAdd(pTransfer, 1, &cbProcessed, &cbTotal, &fNotify), VINF_SUCCESS);
        RTTESTI_CHECK(fNotify); /* A one-byte file still gets its first active snapshot. */
        RTTESTI_CHECK(pTransfer->Progress.uLastPercent == 99);

        ShClTransferReset(pTransfer);
        pTransfer->Progress.cbProcessed = UINT64_MAX - 1;
        RTTESTI_CHECK_RC(ShClTransferProgressAdd(pTransfer, 2, &cbProcessed, &cbTotal, &fNotify),
                         VERR_OUT_OF_RANGE);
        RTTESTI_CHECK(pTransfer->Progress.cbProcessed == UINT64_MAX - 1);
        ShClTransferReset(pTransfer);

        PSHCLFSOBJINFO pObjInfo = (PSHCLFSOBJINFO)RTMemAllocZ(sizeof(*pObjInfo));
        PSHCLLISTENTRY pEntry = NULL;
        RTTESTI_CHECK(pObjInfo != NULL);
        RTTESTI_CHECK_RC(ShClTransferListEntryAlloc(&pEntry), VINF_SUCCESS);
        if (pObjInfo && pEntry)
        {
            pObjInfo->cbObject   = 1000;
            pObjInfo->Attr.fMode = RTFS_TYPE_FILE;
            rc = ShClTransferListEntryInitEx(pEntry, VBOX_SHCL_INFO_F_FSOBJINFO,
                                             "progress.bin", pObjInfo, sizeof(*pObjInfo));
            RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
            if (RT_SUCCESS(rc))
            {
                pObjInfo = NULL; /* The entry owns it now. */
                RTListAppend(&pTransfer->lstRoots.lstEntries, &pEntry->Node);
                pTransfer->lstRoots.Hdr.cEntries = 1;
                pEntry = NULL; /* The transfer owns it now. */

                RTTESTI_CHECK_RC(ShClTransferProgressSetTotalFromRoots(pTransfer), VINF_SUCCESS);
                /* Logical paths need not be transfer roots; nested flattened files
                 * are tracked independently and survive stream reopen. */
                RTTESTI_CHECK_RC(ShClTransferProgressObjRegister(pTransfer, 10, "dir/nested.bin"), VINF_SUCCESS);
                RTTESTI_CHECK_RC(ShClTransferProgressObjAdd(pTransfer, 10, 100,
                                                            &cbProcessed, &cbTotal, &fNotify), VINF_SUCCESS);
                RTTESTI_CHECK(cbProcessed == 100);
                RTTESTI_CHECK(fNotify);
                ShClTransferProgressObjUnregister(pTransfer, 10);

                /* A reopened stream starts at zero and must not count the same prefix again. */
                RTTESTI_CHECK_RC(ShClTransferProgressObjRegister(pTransfer, 11, "dir/nested.bin"), VINF_SUCCESS);
                RTTESTI_CHECK_RC(ShClTransferProgressObjAdd(pTransfer, 11, 50,
                                                            &cbProcessed, &cbTotal, &fNotify), VINF_SUCCESS);
                RTTESTI_CHECK(cbProcessed == 100);
                RTTESTI_CHECK(!fNotify);
                RTTESTI_CHECK_RC(ShClTransferProgressObjAdd(pTransfer, 11, 100,
                                                            &cbProcessed, &cbTotal, &fNotify), VINF_SUCCESS);
                RTTESTI_CHECK(cbProcessed == 150);
                RTTESTI_CHECK(fNotify);
                RTTESTI_CHECK_RC(ShClTransferProgressObjAdd(pTransfer, 11, 850,
                                                            &cbProcessed, &cbTotal, &fNotify), VINF_SUCCESS);
                RTTESTI_CHECK(cbProcessed == cbTotal);
                RTTESTI_CHECK(pTransfer->Progress.uLastPercent == 99);
                RTTESTI_CHECK_RC(ShClTransferProgressObjAdd(pTransfer, 11, 1,
                                                            &cbProcessed, &cbTotal, &fNotify), VERR_TOO_MUCH_DATA);
                ShClTransferProgressObjUnregister(pTransfer, 11);
            }
        }
        ShClTransferListEntryFree(pEntry);
        RTMemFree(pObjInfo);
        ShClTransferReset(pTransfer);
        RTTESTI_CHECK(RTListIsEmpty(&pTransfer->lstProgressObj));
        RTTESTI_CHECK_RC(ShClTransferDestroy(pTransfer), VINF_SUCCESS);
    }

    SHCLTRANSFERCALLBACKS Callbacks;
    RT_ZERO(Callbacks);
    TSTTRANSFERTERMINAL Terminal;
    RT_ZERO(Terminal);
    Callbacks.pfnOnCompleted = tstTransferCompleted;
    Callbacks.pfnOnError     = tstTransferError;
    Callbacks.pvUser         = &Terminal;
    Callbacks.cbUser         = sizeof(Terminal);

    pTransfer = NULL;
    rc = ShClTransferCreate(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE, &Callbacks, &pTransfer);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK_RC(ShClTransferInit(pTransfer), VINF_SUCCESS);
        RTTESTI_CHECK_RC(ShClTransferCancel(pTransfer), VINF_SUCCESS);
        RTTESTI_CHECK(Terminal.cCompleted == 1);
        RTTESTI_CHECK(Terminal.cErrors == 0);
        RTTESTI_CHECK(Terminal.rcCallback == VERR_CANCELLED);
        RTTESTI_CHECK(!Terminal.fLockOwned);
        RTTESTI_CHECK_RC(ShClTransferDestroy(pTransfer), VINF_SUCCESS);
    }

    RT_ZERO(Terminal);
    pTransfer = NULL;
    rc = ShClTransferCreate(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE, &Callbacks, &pTransfer);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK_RC(ShClTransferInit(pTransfer), VINF_SUCCESS);
        RTTESTI_CHECK_RC(ShClTransferError(pTransfer, VERR_GENERAL_FAILURE), VINF_SUCCESS);
        RTTESTI_CHECK(Terminal.cCompleted == 0);
        RTTESTI_CHECK(Terminal.cErrors == 1);
        RTTESTI_CHECK(Terminal.rcCallback == VERR_GENERAL_FAILURE);
        RTTESTI_CHECK(!Terminal.fLockOwned);
        RTTESTI_CHECK_RC(ShClTransferDestroy(pTransfer), VINF_SUCCESS);
    }
}


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
                                 SHCLTRANSFERDIR enmDir, SHCLTRANSFERSTATUS enmStatus, int rcTransfer)
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
    RTTESTI_CHECK(aParms[1].u.uint32 == (uint32_t)enmDir);
    RTTESTI_CHECK(aParms[2].u.uint32 == enmStatus);
    RTTESTI_CHECK((int32_t)aParms[3].u.uint32 == rcTransfer);
    RTTESTI_CHECK(aParms[4].u.uint32 == 0);
}


/** Creates one registered transfer and returns the retained caller reference. */
static SHCLTRANSFERID tstTransferCreateRetained(SHCLTRANSFERDIR enmDir, SHCLSOURCE enmSource,
                                                PSHCLTRANSFER *ppTransfer)
{
    AssertPtrReturn(ppTransfer, NIL_SHCLTRANSFERID);
    *ppTransfer = NULL;

    int const rc = g_Ext.Transport.pOps->pfnTransferCreate(g_Ext.Transport.hClient,
                                                           enmDir, enmSource, NULL /* pCallbacks */,
                                                           NIL_SHCLTRANSFERID, ppTransfer);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    if (RT_FAILURE(rc))
        return NIL_SHCLTRANSFERID;

    RTTESTI_CHECK(*ppTransfer != NULL);
    if (!*ppTransfer)
        return NIL_SHCLTRANSFERID;

    SHCLTRANSFERID const idTransfer = ShClTransferGetID(*ppTransfer);
    RTTESTI_CHECK(ShClTransferIdIsValid(idTransfer));
    return idTransfer;
}


/** Creates one registered started transfer and returns the retained caller reference. */
static SHCLTRANSFERID tstTransferStartedCreateRetained(SHCLTRANSFERDIR enmDir, SHCLSOURCE enmSource,
                                                       PSHCLTRANSFER *ppTransfer)
{
    SHCLTRANSFERID const idTransfer = tstTransferCreateRetained(enmDir, enmSource, ppTransfer);
    if (!ShClTransferIdIsValid(idTransfer) || !*ppTransfer)
        return NIL_SHCLTRANSFERID;

    SHCLTXPROVIDER Provider;
    int rc = g_Ext.Transport.pOps->pfnTransferProviderInitGuest(g_Ext.Transport.hClient, &Provider);
    if (RT_SUCCESS(rc))
        rc = ShClTransferSetProvider(*ppTransfer, &Provider);
    if (   RT_SUCCESS(rc)
        && enmSource == SHCLSOURCE_LOCAL)
    {
        char szExecutable[RTPATH_MAX];
        rc = RTProcGetExecutablePath(szExecutable, sizeof(szExecutable))
           ? ShClTransferRootsSetFromPath(*ppTransfer, szExecutable) : VERR_BUFFER_OVERFLOW;
    }
    if (RT_SUCCESS(rc))
        rc = ShClTransferInit(*ppTransfer);
    if (RT_SUCCESS(rc))
        rc = ShClTransferStart(*ppTransfer);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
        return idTransfer;

    ShClTransferRelease(*ppTransfer);
    *ppTransfer = NULL;
    g_Ext.Transport.pOps->pfnTransferDestroyById(g_Ext.Transport.hClient, idTransfer);
    return NIL_SHCLTRANSFERID;
}


/** Supplies the roots already configured by the aggregate object-read test. */
static DECLCALLBACK(int) tstTransferObjReadRootListRead(PSHCLTXPROVIDERCTX pCtx)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx->pvUser, VERR_INVALID_POINTER);
    return VINF_SUCCESS;
}


/** Opens the single object exposed by the aggregate object-read mock provider. */
static DECLCALLBACK(int) tstTransferObjReadOpen(PSHCLTXPROVIDERCTX pCtx, PSHCLOBJOPENCREATEPARMS pCreateParms,
                                                PSHCLOBJHANDLE phObj)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->cbUser == sizeof(TSTOBJREADPROVIDER), VERR_INVALID_PARAMETER);
    PTSTOBJREADPROVIDER const pThis = (PTSTOBJREADPROVIDER)pCtx->pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(pCreateParms, VERR_INVALID_POINTER);
    AssertPtrReturn(phObj, VERR_INVALID_POINTER);

    pThis->offData = 0;
    pThis->cReads  = 0;
    RT_ZERO(pThis->acbReads);
    *phObj = 42;
    return VINF_SUCCESS;
}


/** Supplies one bounded chunk for the aggregate object-read test. */
static DECLCALLBACK(int) tstTransferObjReadRead(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj, void *pvData,
                                                uint32_t cbData, uint32_t fFlags, uint32_t *pcbRead)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->cbUser == sizeof(TSTOBJREADPROVIDER), VERR_INVALID_PARAMETER);
    PTSTOBJREADPROVIDER const pThis = (PTSTOBJREADPROVIDER)pCtx->pvUser;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbRead, VERR_INVALID_POINTER);
    AssertReturn(hObj == 42, VERR_INVALID_HANDLE);
    AssertReturn(fFlags == 0, VERR_INVALID_FLAGS);
    AssertReturn(cbData <= pThis->cbMaxChunkSize, VERR_BUFFER_OVERFLOW);
    AssertReturn(pThis->offData <= pThis->cbData, VERR_INTERNAL_ERROR);

    if (pThis->cReads < RT_ELEMENTS(pThis->acbReads))
        pThis->acbReads[pThis->cReads] = cbData;
    pThis->cReads++;

    uint32_t const cbRead = RT_MIN(cbData, pThis->cbData - pThis->offData);
    for (uint32_t off = 0; off < cbRead; off++)
        ((uint8_t *)pvData)[off] = (uint8_t)(pThis->offData + off);
    pThis->offData += cbRead;
    *pcbRead = cbRead;
    return VINF_SUCCESS;
}


/** Closes the single object exposed by the aggregate object-read mock provider. */
static DECLCALLBACK(int) tstTransferObjReadClose(PSHCLTXPROVIDERCTX pCtx, SHCLOBJHANDLE hObj)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx->pvUser, VERR_INVALID_POINTER);
    AssertReturn(hObj == 42, VERR_INVALID_HANDLE);
    return VINF_SUCCESS;
}


/** Checks compatibility with aggregate object reads from Guest Additions 7.2.16 and earlier. */
static void tstTransferLegacyAggregateObjRead(void *pvClient)
{
    RTTestISub("Legacy aggregate transfer object read");

    uint32_t const uOldMode = ShClSvcGetMode();
    VBOXHGCMSVCPARM ModeParm;
    HGCMSvcSetU32(&ModeParm, VBOX_SHCL_MODE_BIDIRECTIONAL);
    int rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_MODE, 1, &ModeParm);
    RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);

    PSHCLTRANSFER pTransfer = NULL;
    SHCLTRANSFERID const idTransfer
        = tstTransferCreateRetained(SHCLTRANSFERDIR_HOST_TO_GUEST, SHCLSOURCE_LOCAL, &pTransfer);
    if (!ShClTransferIdIsValid(idTransfer) || !pTransfer)
    {
        HGCMSvcSetU32(&ModeParm, uOldMode);
        RTTESTI_CHECK_RC(g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_MODE,
                                             1, &ModeParm), VINF_SUCCESS);
        return;
    }

    char szExecutable[RTPATH_MAX];
    if (!RTProcGetExecutablePath(szExecutable, sizeof(szExecutable)))
        rc = VERR_BUFFER_OVERFLOW;
    if (RT_SUCCESS(rc))
        rc = ShClTransferRootsSetFromPath(pTransfer, szExecutable);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    TSTOBJREADPROVIDER ProviderCtx;
    RT_ZERO(ProviderCtx);
    ProviderCtx.cbMaxChunkSize = pTransfer->cbMaxChunkSize;

    SHCLTXPROVIDER Provider;
    RT_ZERO(Provider);
    Provider.Interface.pfnRootListRead = tstTransferObjReadRootListRead;
    Provider.Interface.pfnObjOpen      = tstTransferObjReadOpen;
    Provider.Interface.pfnObjRead      = tstTransferObjReadRead;
    Provider.Interface.pfnObjClose     = tstTransferObjReadClose;
    Provider.pvUser                    = &ProviderCtx;
    Provider.cbUser                    = sizeof(ProviderCtx);

    if (RT_SUCCESS(rc))
        rc = ShClTransferSetProvider(pTransfer, &Provider);
    if (RT_SUCCESS(rc))
        rc = ShClTransferInit(pTransfer);
    if (RT_SUCCESS(rc))
        rc = ShClTransferStart(pTransfer);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    SHCLSESSIONID idSession = NIL_SHCLSESSIONID;
    uint32_t cbMaxChunkSize = 0;
    if (RT_SUCCESS(rc))
    {
        idSession      = ShClTransferGetSessionId(pTransfer);
        cbMaxChunkSize = pTransfer->cbMaxChunkSize;
        ShClTransferRelease(pTransfer);
        pTransfer = NULL;
    }

    SHCLOBJHANDLE hObj = NIL_SHCLOBJHANDLE;
    if (RT_SUCCESS(rc))
    {
        const char *pszPath = RTPathFilename(szExecutable);
        VBOXHGCMSVCPARM aOpen[VBOX_SHCL_CPARMS_OBJ_OPEN];
        HGCMSvcSetU64(&aOpen[0], VBOX_SHCL_CONTEXTID_MAKE(idSession, idTransfer, 0));
        HGCMSvcSetU64(&aOpen[1], NIL_SHCLOBJHANDLE);
        HGCMSvcSetPv(&aOpen[2], (void *)pszPath, (uint32_t)strlen(pszPath) + 1);
        HGCMSvcSetU32(&aOpen[3], SHCL_OBJ_CF_ACCESS_READ | SHCL_OBJ_CF_ACCESS_DENYWRITE);
        rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_OPEN, RT_ELEMENTS(aOpen), aOpen);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        if (RT_SUCCESS(rc))
            hObj = aOpen[1].u.uint64;
    }

    uint8_t *pbRead = NULL;
    uint32_t const cbRead = _128K + 1;
    if (RT_SUCCESS(rc))
    {
        RTTESTI_CHECK(cbMaxChunkSize == _64K);
        pbRead = (uint8_t *)RTMemAlloc(cbRead + 1);
        RTTESTI_CHECK(pbRead != NULL);
        if (!pbRead)
            rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc))
    {
        ProviderCtx.cbData = cbRead;
        memset(pbRead, 0xa5, cbRead + 1);

        VBOXHGCMSVCPARM aRead[VBOX_SHCL_CPARMS_OBJ_READ];
        HGCMSvcSetU64(&aRead[0], VBOX_SHCL_CONTEXTID_MAKE(idSession, idTransfer, 0));
        HGCMSvcSetU64(&aRead[1], hObj);
        HGCMSvcSetU32(&aRead[2], cbRead);
        HGCMSvcSetPv(&aRead[3], pbRead, cbRead);
        HGCMSvcSetU32(&aRead[4], 0);
        HGCMSvcSetPv(&aRead[5], NULL, 0);
        rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_READ, RT_ELEMENTS(aRead), aRead);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        if (RT_SUCCESS(rc))
        {
            RTTESTI_CHECK(aRead[2].u.uint32 == cbRead);
            RTTESTI_CHECK(ProviderCtx.offData == cbRead);
            RTTESTI_CHECK(ProviderCtx.cReads == 3);
            RTTESTI_CHECK(ProviderCtx.acbReads[0] == cbMaxChunkSize);
            RTTESTI_CHECK(ProviderCtx.acbReads[1] == cbMaxChunkSize);
            RTTESTI_CHECK(ProviderCtx.acbReads[2] == 1);

            uint32_t off = 0;
            while (off < cbRead && pbRead[off] == (uint8_t)off)
                off++;
            RTTESTI_CHECK(off == cbRead);
            RTTESTI_CHECK(pbRead[cbRead] == 0xa5);

            HGCMSvcSetU32(&aRead[2], 1);
            HGCMSvcSetPv(&aRead[3], pbRead, 1);
            rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_READ, RT_ELEMENTS(aRead), aRead);
            RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
            if (RT_SUCCESS(rc))
            {
                RTTESTI_CHECK(aRead[2].u.uint32 == 0);
                RTTESTI_CHECK(ProviderCtx.cReads == 4);
                RTTESTI_CHECK(ProviderCtx.acbReads[3] == 1);
            }
        }
    }

    if (RT_SUCCESS(rc) && hObj != NIL_SHCLOBJHANDLE)
    {
        VBOXHGCMSVCPARM aClose[VBOX_SHCL_CPARMS_OBJ_CLOSE];
        HGCMSvcSetU64(&aClose[0], VBOX_SHCL_CONTEXTID_MAKE(idSession, idTransfer, 0));
        HGCMSvcSetU64(&aClose[1], hObj);
        RTTESTI_CHECK_RC(tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_CLOSE,
                                      RT_ELEMENTS(aClose), aClose), VINF_SUCCESS);
    }

    RTMemFree(pbRead);
    if (pTransfer)
        ShClTransferRelease(pTransfer);
    ShClSvcTransferDestroyByIdEx((PSHCLCLIENT)g_Ext.Transport.hClient, idTransfer, false /* fNotifyGuest */);

    HGCMSvcSetU32(&ModeParm, uOldMode);
    RTTESTI_CHECK_RC(g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_MODE,
                                         1, &ModeParm), VINF_SUCCESS);
}


/** Checks that a guest object-write reply has one self-contained payload allocation. */
static void tstTransferObjWritePayload(void *pvClient)
{
    RTTestISub("Transfer object-write payload ownership");

    uint32_t const uOldMode = ShClSvcGetMode();
    VBOXHGCMSVCPARM ModeParm;
    HGCMSvcSetU32(&ModeParm, VBOX_SHCL_MODE_BIDIRECTIONAL);
    int rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_MODE, 1, &ModeParm);
    RTTESTI_CHECK_RC_RETV(rc, VINF_SUCCESS);

    PSHCLTRANSFER pTransfer = NULL;
    SHCLTRANSFERID const idTransfer
        = tstTransferCreateRetained(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE, &pTransfer);
    if (!ShClTransferIdIsValid(idTransfer) || !pTransfer)
    {
        HGCMSvcSetU32(&ModeParm, uOldMode);
        RTTESTI_CHECK_RC(g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_MODE,
                                             1, &ModeParm), VINF_SUCCESS);
        return;
    }

    PSHCLEVENT pEvent = NULL;
    rc = ShClEventSourceGenerateAndRegisterEvent(&pTransfer->Events, &pEvent);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        uint8_t abData[4096];
        for (size_t i = 0; i < sizeof(abData); ++i)
            abData[i] = (uint8_t)i;

        SHCLOBJHANDLE const hObj = 42;
        VBOXHGCMSVCPARM aWrite[VBOX_SHCL_CPARMS_OBJ_WRITE];
        HGCMSvcSetU64(&aWrite[0], VBOX_SHCL_CONTEXTID_MAKE(ShClTransferGetSessionId(pTransfer),
                                                           idTransfer, pEvent->idEvent));
        HGCMSvcSetU64(&aWrite[1], hObj);
        HGCMSvcSetU32(&aWrite[2], sizeof(abData));
        HGCMSvcSetPv(&aWrite[3], abData, sizeof(abData));
        HGCMSvcSetU32(&aWrite[4], 0);
        HGCMSvcSetPv(&aWrite[5], NULL, 0);

        rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_WRITE, RT_ELEMENTS(aWrite), aWrite);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        if (RT_SUCCESS(rc))
        {
            PSHCLEVENTPAYLOAD pPayload = NULL;
            rc = ShClEventWait(pEvent, RT_MS_1SEC, &pPayload);
            RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
            RTTESTI_CHECK(pPayload != NULL);
            if (RT_SUCCESS(rc) && pPayload)
            {
                RTTESTI_CHECK(pPayload->cbData == sizeof(SHCLOBJDATACHUNK));
                PSHCLOBJDATACHUNK pDataChunk = (PSHCLOBJDATACHUNK)pPayload->pvData;
                RTTESTI_CHECK(pDataChunk != NULL);
                if (pDataChunk)
                {
                    RTTESTI_CHECK(pDataChunk->uHandle == hObj);
                    RTTESTI_CHECK(pDataChunk->cbData == sizeof(abData));
                    RTTESTI_CHECK(pDataChunk->pvData == pDataChunk + 1);
                    RTTESTI_CHECK(memcmp(pDataChunk->pvData, abData, sizeof(abData)) == 0);
                }
                ShClPayloadDestroy(pPayload);
            }
        }

        RTTESTI_CHECK(ShClEventRelease(pEvent) == 0);
    }

    ShClTransferRelease(pTransfer);
    ShClSvcTransferDestroyByIdEx((PSHCLCLIENT)g_Ext.Transport.hClient, idTransfer, false /* fNotifyGuest */);

    HGCMSvcSetU32(&ModeParm, uOldMode);
    RTTESTI_CHECK_RC(g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_MODE,
                                         1, &ModeParm), VINF_SUCCESS);
}


/** Captures test status metadata from a retained transfer. */
static void tstTransferStatusInit(PSHCLSVCEXTTRANSFERSTATUS pStatus, PSHCLTRANSFER pTransfer,
                                  SHCLTRANSFERSTATUS enmStatus, int rcStatus)
{
    RT_ZERO(*pStatus);
    ShClTransferGetKey(pTransfer, &pStatus->Key);
    pStatus->enmDir            = ShClTransferGetDir(pTransfer);
    pStatus->enmTransferSource = ShClTransferGetSource(pTransfer);
    pStatus->enmReplySource    = SHCLSOURCE_LOCAL;
    pStatus->enmStatus         = enmStatus;
    pStatus->rcStatus          = rcStatus;
}


/** Synchronously notifies Main of one nonterminal transfer status. */
static void tstTransferStatusNotify(PSHCLTRANSFER pTransfer)
{
    AssertPtrReturnVoid(pTransfer);

    SHCLSVCEXTTRANSFERSTATUS Status;
    tstTransferStatusInit(&Status, pTransfer, SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
    uint32_t const cStatuses = g_Ext.cTransferStatuses;
    RTTESTI_CHECK_RC(shClSvcExtNotifyTransferStatus((PSHCLCLIENT)g_Ext.Transport.hClient, &Status), VINF_SUCCESS);
    RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatuses + 1);
}


/** Checks that a late guest reply cannot turn host cancellation into ERROR. */
static void tstTransferLateCancellationReply(void *pvClient)
{
    RTTestISub("Late guest reply after transfer cancellation");

    PSHCLTRANSFER pTransfer = NULL;
    SHCLTRANSFERID const idTransfer
        = tstTransferStartedCreateRetained(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE, &pTransfer);
    RTTESTI_CHECK(ShClTransferIdIsValid(idTransfer));
    if (!pTransfer)
        return;

    PSHCLEVENT pEvent = NULL;
    int rc = ShClEventSourceGenerateAndRegisterEvent(&pTransfer->Events, &pEvent);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    if (RT_SUCCESS(rc))
    {
        uint64_t const uContext = VBOX_SHCL_CONTEXTID_MAKE(ShClTransferGetSessionId(pTransfer),
                                                           idTransfer, pEvent->idEvent);
        rc = ShClTransferCancel(pTransfer);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        if (RT_SUCCESS(rc))
        {
            uint32_t const cErrorsBefore   = g_Ext.cReportedErrors;
            uint32_t const cStatusesBefore = g_Ext.cTransferStatuses;

            VBOXHGCMSVCPARM aReply[VBOX_SHCL_CPARMS_REPLY_MIN + 1];
            tstTransferReplyInit(aReply, uContext, SHCLTRANSFERSTATUS_CANCELED);
            HGCMSvcSetU32(&aReply[2], (uint32_t)VERR_CANCELLED);

            /* The reply handler consumes terminal transfers.  Drop the test's
             * explicit transfer reference before entering it; pEvent remains valid. */
            ShClTransferRelease(pTransfer);
            pTransfer = NULL;

            rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPLY, RT_ELEMENTS(aReply), aReply);
            RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
            RTTESTI_CHECK(g_Ext.cReportedErrors == cErrorsBefore);
            RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatusesBefore);

            PSHCLTRANSFER const pLookup
                = g_Ext.Transport.pOps->pfnTransferGetByIdRetained(g_Ext.Transport.hClient, idTransfer);
            RTTESTI_CHECK(pLookup == NULL);
            if (pLookup)
                ShClTransferRelease(pLookup);

            VBOXHGCMSVCPARM aPeek[2];
            HGCMSvcSetU32(&aPeek[0], 0);
            HGCMSvcSetU32(&aPeek[1], 0);
            rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_PEEK_NOWAIT, RT_ELEMENTS(aPeek), aPeek);
            RTTESTI_CHECK_RC(rc, VERR_TRY_AGAIN);
        }
    }

    if (pTransfer)
    {
        ShClTransferRelease(pTransfer);
        g_Ext.Transport.pOps->pfnTransferDestroyById(g_Ext.Transport.hClient, idTransfer);
    }
    if (pEvent)
        RTTESTI_CHECK(ShClEventRelease(pEvent) == 0);
}


/** Checks that a late guest teardown error cannot replace host completion. */
static void tstTransferLateGuestError(void *pvClient)
{
    RTTestISub("Late guest error after transfer completion");

    PSHCLCLIENT const pClient = (PSHCLCLIENT)g_Ext.Transport.hClient;
    PSHCLTRANSFER pTransfer = NULL;
    SHCLTRANSFERID const idTransfer
        = tstTransferStartedCreateRetained(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE, &pTransfer);
    RTTESTI_CHECK(ShClTransferIdIsValid(idTransfer));
    if (pTransfer)
    {
        SHCLSESSIONID const idSession = ShClTransferGetSessionId(pTransfer);
        SHCLTRANSFERGEN const uGeneration = ShClTransferGetGeneration(pTransfer);

        tstTransferStatusNotify(pTransfer);
        int rc = ShClTransferComplete(pTransfer);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

        uint32_t const cStatusesBeforeComplete = g_Ext.cTransferStatuses;
        rc = ShClSvcTransferReportStatus(pClient, pTransfer, SHCLTRANSFERSTATUS_COMPLETED, VINF_SUCCESS, NULL /* pszPath */);
        RTTESTI_CHECK_RC_OK(rc);
        RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatusesBeforeComplete + 1);
        RTTESTI_CHECK(ShClTransferKeyGetSessionId(&g_Ext.TransferKey) == idSession);
        RTTESTI_CHECK(ShClTransferKeyGetTransferId(&g_Ext.TransferKey) == idTransfer);
        RTTESTI_CHECK(g_Ext.TransferKey.uGeneration == uGeneration);
        RTTESTI_CHECK(g_Ext.enmTransferStatus == SHCLTRANSFERSTATUS_COMPLETED);
        RTTESTI_CHECK(g_Ext.rcTransfer == VINF_SUCCESS);
        tstTransferStatusGet(pvClient, idSession, idTransfer, SHCLTRANSFERDIR_GUEST_TO_HOST,
                             SHCLTRANSFERSTATUS_COMPLETED, VINF_SUCCESS);

        ShClTransferRelease(pTransfer);
        pTransfer = NULL;

        uint32_t const cErrorsBefore = g_Ext.cReportedErrors;
        uint32_t const cStatusesBeforeError = g_Ext.cTransferStatuses;
        VBOXHGCMSVCPARM aReply[VBOX_SHCL_CPARMS_REPLY_MIN + 1];
        tstTransferReplyInit(aReply, VBOX_SHCL_CONTEXTID_MAKE(idSession, idTransfer, 0),
                             SHCLTRANSFERSTATUS_ERROR);
        HGCMSvcSetU32(&aReply[2], (uint32_t)VERR_NOT_FOUND);
        rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPLY, RT_ELEMENTS(aReply), aReply);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        RTTESTI_CHECK(g_Ext.cReportedErrors == cErrorsBefore);
        RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatusesBeforeError);
        RTTESTI_CHECK(g_Ext.enmTransferStatus == SHCLTRANSFERSTATUS_COMPLETED);
        RTTESTI_CHECK(g_Ext.rcTransfer == VINF_SUCCESS);

        PSHCLTRANSFER const pLookup
            = g_Ext.Transport.pOps->pfnTransferGetByIdRetained(g_Ext.Transport.hClient, idTransfer);
        RTTESTI_CHECK(pLookup == NULL);
        if (pLookup)
            ShClTransferRelease(pLookup);

        VBOXHGCMSVCPARM aPeek[2];
        HGCMSvcSetU32(&aPeek[0], 0);
        HGCMSvcSetU32(&aPeek[1], 0);
        rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_PEEK_NOWAIT, RT_ELEMENTS(aPeek), aPeek);
        RTTESTI_CHECK_RC(rc, VERR_TRY_AGAIN);
    }
}


/**
 * Checks that a native transfer error path survives the immutable extension snapshot.
 *
 * @param   pvClient            Synthetic guest client.
 */
static void tstTransferNativeErrorPath(void *pvClient)
{
    RTTestISub("Native transfer error path");

    PSHCLCLIENT const pClient = (PSHCLCLIENT)g_Ext.Transport.hClient;
    PSHCLTRANSFER pTransfer = NULL;
    SHCLTRANSFERID const idTransfer
        = tstTransferStartedCreateRetained(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE, &pTransfer);
    RTTESTI_CHECK(ShClTransferIdIsValid(idTransfer));
    if (pTransfer)
    {
        SHCLSESSIONID const idSession = ShClTransferGetSessionId(pTransfer);
        tstTransferStatusNotify(pTransfer);

        int rc = ShClTransferError(pTransfer, VERR_ACCESS_DENIED);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        uint32_t const cStatuses = g_Ext.cTransferStatuses;
        rc = ShClSvcTransferReportStatus(pClient, pTransfer, SHCLTRANSFERSTATUS_ERROR, VERR_ACCESS_DENIED, "dir/denied.bin");
        RTTESTI_CHECK_RC_OK(rc);
        RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatuses + 1);
        RTTESTI_CHECK(g_Ext.enmTransferReplySource == SHCLSOURCE_LOCAL);
        RTTESTI_CHECK(g_Ext.enmTransferStatus == SHCLTRANSFERSTATUS_ERROR);
        RTTESTI_CHECK(g_Ext.rcTransfer == VERR_ACCESS_DENIED);
        RTTESTI_CHECK(strcmp(g_Ext.szTransferPath, "dir/denied.bin") == 0);

        tstTransferStatusGet(pvClient, idSession, idTransfer, SHCLTRANSFERDIR_GUEST_TO_HOST,
                             SHCLTRANSFERSTATUS_ERROR, VERR_ACCESS_DENIED);

        ShClTransferRelease(pTransfer);
        pTransfer = NULL;
        ShClSvcTransferDestroyByIdEx(pClient, idTransfer, false /* fNotifyGuest */);
    }
}


/** Checks that native cancellation reaches Main before its guest acknowledgement can detach the transfer. */
static void tstTransferNativeCancellationOrder(void *pvClient)
{
    RTTestISub("Native transfer cancellation publication order");

    PSHCLCLIENT const pClient = (PSHCLCLIENT)g_Ext.Transport.hClient;
    PSHCLTRANSFER pTransfer = NULL;
    SHCLTRANSFERID const idTransfer
        = tstTransferStartedCreateRetained(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE, &pTransfer);
    RTTESTI_CHECK(ShClTransferIdIsValid(idTransfer));
    if (pTransfer)
    {
        SHCLSESSIONID const idSession = ShClTransferGetSessionId(pTransfer);
        SHCLTRANSFERGEN const uGeneration = ShClTransferGetGeneration(pTransfer);
        tstTransferStatusNotify(pTransfer);

        VBOXHGCMSVCPARM aPeek[2];
        HGCMSvcSetU32(&aPeek[0], 0);
        HGCMSvcSetU32(&aPeek[1], 0);
        int rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_PEEK_NOWAIT, RT_ELEMENTS(aPeek), aPeek);
        RTTESTI_CHECK_RC(rc, VERR_TRY_AGAIN);

        rc = ShClTransferCancel(pTransfer);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        uint32_t const cStatuses = g_Ext.cTransferStatuses;
        g_Ext.fTerminalBeforeGuest = false;
        g_Ext.fCheckTerminalBeforeGuest = true;
        rc = ShClSvcTransferReportStatus(pClient, pTransfer, SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED, NULL /* pszPath */);
        g_Ext.fCheckTerminalBeforeGuest = false;
        RTTESTI_CHECK_RC_OK(rc);
        RTTESTI_CHECK(g_Ext.fTerminalBeforeGuest);
        RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatuses + 1);
        RTTESTI_CHECK(ShClTransferKeyGetSessionId(&g_Ext.TransferKey) == idSession);
        RTTESTI_CHECK(ShClTransferKeyGetTransferId(&g_Ext.TransferKey) == idTransfer);
        RTTESTI_CHECK(g_Ext.TransferKey.uGeneration == uGeneration);
        RTTESTI_CHECK(g_Ext.enmTransferStatus == SHCLTRANSFERSTATUS_CANCELED);
        RTTESTI_CHECK(g_Ext.rcTransfer == VERR_CANCELLED);

        tstTransferStatusGet(pvClient, idSession, idTransfer, SHCLTRANSFERDIR_GUEST_TO_HOST,
                             SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);

        ShClTransferRelease(pTransfer);
        pTransfer = NULL;
        ShClSvcTransferDestroyByIdEx(pClient, idTransfer, false /* fNotifyGuest */);
    }
}


/** Checks synchronous reset fallback after one terminal Main callback failure. */
static void tstTransferNotificationFailure(void)
{
    RTTestISub("Transfer notification failure");

    PSHCLCLIENT const pClient = (PSHCLCLIENT)g_Ext.Transport.hClient;
    PSHCLTRANSFER pTransfer = NULL;
    SHCLTRANSFERID const idTransfer
        = tstTransferStartedCreateRetained(SHCLTRANSFERDIR_HOST_TO_GUEST, SHCLSOURCE_LOCAL, &pTransfer);
    RTTESTI_CHECK(ShClTransferIdIsValid(idTransfer));
    if (pTransfer)
    {
        tstTransferStatusNotify(pTransfer);
        int const rc = ShClTransferCancel(pTransfer);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        if (RT_SUCCESS(rc))
        {
            SHCLSVCEXTTRANSFERSTATUS Status;
            tstTransferStatusInit(&Status, pTransfer, SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);
            uint32_t const cStatuses = g_Ext.cTransferStatuses;
            uint32_t const cFailures = g_Ext.cTransferStatusFailures;
            uint32_t const cResets   = g_Ext.cTransferResets;
            g_Ext.fFailNextTransferStatus = true;
            RTTESTI_CHECK_RC(shClSvcExtNotifyTransferStatus(pClient, &Status), VERR_NO_MEMORY);
            RTTESTI_CHECK(!g_Ext.fFailNextTransferStatus);
            RTTESTI_CHECK(g_Ext.cTransferStatusFailures == cFailures + 1);
            RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatuses);
            RTTESTI_CHECK(g_Ext.cTransferResets == cResets + 1);
        }

        ShClTransferRelease(pTransfer);
        ShClSvcTransferDestroyByIdEx(pClient, idTransfer, false /* fNotifyGuest */);
    }
}


/** Checks that ordinary native cleanup reports a terminal status synchronously. */
static void tstTransferCleanupNotification(void *pvClient)
{
    RTTestISub("Transfer cleanup notification");

    PSHCLCLIENT const pClient = (PSHCLCLIENT)g_Ext.Transport.hClient;
    PSHCLTRANSFER pTransfer = NULL;
    SHCLTRANSFERID const idTransfer
        = tstTransferCreateRetained(SHCLTRANSFERDIR_HOST_TO_GUEST, SHCLSOURCE_LOCAL, &pTransfer);
    RTTESTI_CHECK(ShClTransferIdIsValid(idTransfer));
    if (pTransfer)
    {
        SHCLSESSIONID const idSession = ShClTransferGetSessionId(pTransfer);
        SHCLTRANSFERGEN const uGeneration = ShClTransferGetGeneration(pTransfer);
        tstTransferStatusNotify(pTransfer);
        uint32_t const cStatuses = g_Ext.cTransferStatuses;

        ShClTransferRelease(pTransfer);
        g_Ext.Transport.pOps->pfnTransferDestroyById(g_Ext.Transport.hClient, idTransfer);
        tstTransferStatusGet(pvClient, idSession, idTransfer, SHCLTRANSFERDIR_HOST_TO_GUEST,
                             SHCLTRANSFERSTATUS_UNINITIALIZED, VINF_SUCCESS);
        RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatuses + 1);
        RTTESTI_CHECK(ShClTransferKeyGetSessionId(&g_Ext.TransferKey) == idSession);
        RTTESTI_CHECK(ShClTransferKeyGetTransferId(&g_Ext.TransferKey) == idTransfer);
        RTTESTI_CHECK(g_Ext.TransferKey.uGeneration == uGeneration);
        RTTESTI_CHECK(g_Ext.enmTransferStatus == SHCLTRANSFERSTATUS_UNINITIALIZED);
        RTTESTI_CHECK(g_Ext.rcTransfer == VINF_SUCCESS);
        RTTESTI_CHECK(g_Ext.enmTransferSource == SHCLSOURCE_LOCAL);
        RTTESTI_CHECK(g_Ext.enmTransferReplySource == SHCLSOURCE_LOCAL);

        PSHCLTRANSFER const pLookup
            = g_Ext.Transport.pOps->pfnTransferGetByIdRetained(g_Ext.Transport.hClient, idTransfer);
        RTTESTI_CHECK(pLookup == NULL);
        if (pLookup)
            ShClTransferRelease(pLookup);
    }

    /* Creating a later transfer reaps completed transfers without publishing
     * a second terminal status. */
    pTransfer = NULL;
    SHCLTRANSFERID const idCompletedTransfer
        = tstTransferStartedCreateRetained(SHCLTRANSFERDIR_GUEST_TO_HOST, SHCLSOURCE_REMOTE, &pTransfer);
    RTTESTI_CHECK(ShClTransferIdIsValid(idCompletedTransfer));
    if (pTransfer)
    {
        SHCLSESSIONID const idSession = ShClTransferGetSessionId(pTransfer);
        SHCLTRANSFERGEN const uGeneration = ShClTransferGetGeneration(pTransfer);
        tstTransferStatusNotify(pTransfer);
        int rc = ShClTransferComplete(pTransfer);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        if (RT_SUCCESS(rc))
            rc = ShClSvcTransferReportStatus(pClient, pTransfer, SHCLTRANSFERSTATUS_COMPLETED, VINF_SUCCESS, NULL /* pszPath */);
        RTTESTI_CHECK_RC_OK(rc);
        tstTransferStatusGet(pvClient, idSession, idCompletedTransfer, SHCLTRANSFERDIR_GUEST_TO_HOST,
                             SHCLTRANSFERSTATUS_COMPLETED, VINF_SUCCESS);

        ShClTransferRelease(pTransfer);
        pTransfer = NULL;

        uint32_t const cStatuses = g_Ext.cTransferStatuses;
        PSHCLTRANSFER pTriggerTransfer = NULL;
        SHCLTRANSFERID const idTriggerTransfer
            = tstTransferCreateRetained(SHCLTRANSFERDIR_HOST_TO_GUEST, SHCLSOURCE_LOCAL, &pTriggerTransfer);
        RTTESTI_CHECK(ShClTransferIdIsValid(idTriggerTransfer));
        RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatuses);
        RTTESTI_CHECK(g_Ext.enmTransferStatus == SHCLTRANSFERSTATUS_COMPLETED);
        RTTESTI_CHECK(ShClTransferKeyGetSessionId(&g_Ext.TransferKey) == idSession);
        RTTESTI_CHECK(ShClTransferKeyGetTransferId(&g_Ext.TransferKey) == idCompletedTransfer);
        RTTESTI_CHECK(g_Ext.TransferKey.uGeneration == uGeneration);

        PSHCLTRANSFER const pLookup
            = g_Ext.Transport.pOps->pfnTransferGetByIdRetained(g_Ext.Transport.hClient, idCompletedTransfer);
        RTTESTI_CHECK(pLookup == NULL);
        if (pLookup)
            ShClTransferRelease(pLookup);

        VBOXHGCMSVCPARM aPeek[2];
        HGCMSvcSetU32(&aPeek[0], 0);
        HGCMSvcSetU32(&aPeek[1], 0);
        rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_MSG_PEEK_NOWAIT, RT_ELEMENTS(aPeek), aPeek);
        RTTESTI_CHECK_RC(rc, VERR_TRY_AGAIN);

        if (pTriggerTransfer)
        {
            ShClTransferRelease(pTriggerTransfer);
            ShClSvcTransferDestroyByIdEx(pClient, idTriggerTransfer, false /* fNotifyGuest */);
        }
    }
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

    tstTransferLegacyAggregateObjRead(pvClient);
    tstTransferObjWritePayload(pvClient);
    tstTransferLateGuestError(pvClient);
    tstTransferNativeErrorPath(pvClient);
    tstTransferNativeCancellationOrder(pvClient);
    tstTransferLateCancellationReply(pvClient);
    tstTransferNotificationFailure();
    tstTransferCleanupNotification(pvClient);

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
    SHCLSESSIONID const idSession = ShClTransferKeyGetSessionId(&g_Ext.TransferKey);
    RTTESTI_CHECK(idSession != NIL_SHCLSESSIONID);
    RTTESTI_CHECK(ShClTransferKeyGetTransferId(&g_Ext.TransferKey) != NIL_SHCLTRANSFERID);
    RTTESTI_CHECK(g_Ext.TransferKey.uGeneration != NIL_SHCLTRANSFERGEN);
    RTTESTI_CHECK(g_Ext.enmTransferDir == SHCLTRANSFERDIR_HOST_TO_GUEST);
    RTTESTI_CHECK(g_Ext.enmTransferSource == SHCLSOURCE_LOCAL);
    RTTESTI_CHECK(g_Ext.enmTransferReplySource == SHCLSOURCE_REMOTE);
    RTTESTI_CHECK(g_Ext.enmTransferStatus == SHCLTRANSFERSTATUS_REQUESTED);
    RTTESTI_CHECK(g_Ext.rcTransfer == VINF_SUCCESS);

    uint32_t const cQueriesAfterRequest = g_Ext.cTransferCallbackQueries;
    uint32_t const cStatusesAfterRequest = g_Ext.cTransferStatuses;
    tstTransferReplyInit(aReply, VBOX_SHCL_CONTEXTID_MAKE(idSession, ShClTransferKeyGetTransferId(&g_Ext.TransferKey) + 1, 0),
                         SHCLTRANSFERSTATUS_REQUESTED);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPLY, RT_ELEMENTS(aReply), aReply);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_CONTEXT);
    RTTESTI_CHECK(g_Ext.cTransferCallbackQueries == cQueriesAfterRequest);
    RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatusesAfterRequest);

    HGCMSvcSetU64(&aObjectClose[0], VBOX_SHCL_CONTEXTID_MAKE(idSession + 1, ShClTransferKeyGetTransferId(&g_Ext.TransferKey), 0));
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(aObjectClose), aObjectClose);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_CONTEXT);
    HGCMSvcSetU64(&aObjectClose[0], VBOX_SHCL_CONTEXTID_MAKE(idSession, ShClTransferKeyGetTransferId(&g_Ext.TransferKey) + 1, 0));
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(aObjectClose), aObjectClose);
    RTTESTI_CHECK_RC(rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);

    tstTransferStatusGet(pvClient, idSession, ShClTransferKeyGetTransferId(&g_Ext.TransferKey), SHCLTRANSFERDIR_HOST_TO_GUEST,
                         SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);

    PSHCLTRANSFER pTransfer = g_Ext.Transport.pOps->pfnTransferGetByIdRetained(g_Ext.Transport.hClient,
                                                                               ShClTransferKeyGetTransferId(&g_Ext.TransferKey));
    RTTESTI_CHECK(pTransfer != NULL);
    if (pTransfer)
    {
        /* An exact-pointer detach must not claim a newer/different object which
         * happens to resolve through the same transfer ID. */
        PSHCLTRANSFER pImpostor = NULL;
        rc = ShClTransferCreate(ShClTransferGetDir(pTransfer), ShClTransferGetSource(pTransfer),
                                NULL /* pCallbacks */, &pImpostor);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        if (RT_SUCCESS(rc))
        {
            ShClTransferKeyInit(&pImpostor->State.Key, NIL_SHCLSESSIONID, ShClTransferGetID(pTransfer),
                                NIL_SHCLTRANSFERGEN);
            RTTESTI_CHECK(shClSvcTransferDetach((PSHCLCLIENT)g_Ext.Transport.hClient, pImpostor) == NULL);
            PSHCLTRANSFER const pStillRegistered
                = g_Ext.Transport.pOps->pfnTransferGetByIdRetained(g_Ext.Transport.hClient,
                                                                   ShClTransferGetID(pTransfer));
            RTTESTI_CHECK(pStillRegistered == pTransfer);
            if (pStillRegistered)
                ShClTransferRelease(pStillRegistered);
            ShClTransferKeyReset(&pImpostor->State.Key);
            RTTESTI_CHECK_RC(ShClTransferDestroy(pImpostor), VINF_SUCCESS);
        }

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

        PSHCLFSOBJINFO pProgressInfo = (PSHCLFSOBJINFO)RTMemAllocZ(sizeof(*pProgressInfo));
        PSHCLLISTENTRY pProgressEntry = NULL;
        RTTESTI_CHECK(pProgressInfo != NULL);
        RTTESTI_CHECK_RC(ShClTransferListEntryAlloc(&pProgressEntry), VINF_SUCCESS);
        if (pProgressInfo && pProgressEntry)
        {
            pProgressInfo->cbObject   = 1000;
            pProgressInfo->Attr.fMode = RTFS_TYPE_FILE;
            rc = ShClTransferListEntryInitEx(pProgressEntry, VBOX_SHCL_INFO_F_FSOBJINFO,
                                             "progress.bin", pProgressInfo, sizeof(*pProgressInfo));
            RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
            if (RT_SUCCESS(rc))
            {
                pProgressInfo = NULL; /* The entry owns it now. */
                RTListAppend(&pTransfer->lstRoots.lstEntries, &pProgressEntry->Node);
                pTransfer->lstRoots.Hdr.cEntries = 1;
                pProgressEntry = NULL; /* The transfer owns it now. */

                SHCLTXPROVIDER Provider;
                rc = g_Ext.Transport.pOps->pfnTransferProviderInitGuest(g_Ext.Transport.hClient, &Provider);
                RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
                if (RT_SUCCESS(rc))
                    rc = ShClTransferSetProvider(pTransfer, &Provider);
                RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
                if (RT_SUCCESS(rc))
                    rc = ShClTransferInit(pTransfer);
                RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

                SHCLOBJHANDLE const hProgressObj = 42;
                RTTESTI_CHECK_RC(ShClTransferProgressSetTotalFromRoots(pTransfer), VINF_SUCCESS);
                RTTESTI_CHECK_RC(ShClTransferProgressObjRegister(pTransfer, hProgressObj, "progress.bin"),
                                 VINF_SUCCESS);
                uint32_t const cProgressBefore = g_Ext.cTransferProgress;
                ShClSvcTransferReportProgress((PSHCLCLIENT)g_Ext.Transport.hClient, pTransfer, hProgressObj, 1);
                RTTESTI_CHECK(g_Ext.cTransferProgress == cProgressBefore + 1);
                SHCLTRANSFERKEY TransferKey;
                ShClTransferGetKey(pTransfer, &TransferKey);
                RTTESTI_CHECK(ShClTransferKeyIsEqual(&g_Ext.ProgressKey, &TransferKey));
                RTTESTI_CHECK(g_Ext.cbProgressProcessed == 1);
                RTTESTI_CHECK(g_Ext.cbProgressTotal == 1000);
                RTTESTI_CHECK_RC(ShClTransferStart(pTransfer), VINF_SUCCESS);
                ShClSvcTransferReportProgress((PSHCLCLIENT)g_Ext.Transport.hClient, pTransfer, hProgressObj, 1);
                RTTESTI_CHECK(g_Ext.cTransferProgress == cProgressBefore + 1);
                ShClSvcTransferReportProgress((PSHCLCLIENT)g_Ext.Transport.hClient, pTransfer, hProgressObj, 8);
                RTTESTI_CHECK(g_Ext.cTransferProgress == cProgressBefore + 2);
                RTTESTI_CHECK(g_Ext.cbProgressProcessed == 10);
                RTTESTI_CHECK(g_Ext.cbProgressTotal == 1000);
                ShClTransferProgressObjUnregister(pTransfer, hProgressObj);
            }
        }
        ShClTransferListEntryFree(pProgressEntry);
        RTMemFree(pProgressInfo);
    }

    SHCLTRANSFERKEY const CancelKey = g_Ext.TransferKey;
    SHCLTRANSFERID const idCancelTransfer = ShClTransferKeyGetTransferId(&CancelKey);
    VBOXHGCMSVCPARM aCancel[2];
    HGCMSvcSetU64(&aCancel[0], CancelKey.uContextId);
    HGCMSvcSetU64(&aCancel[1], CancelKey.uGeneration + 1);
    rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_CANCEL, RT_ELEMENTS(aCancel), aCancel);
    RTTESTI_CHECK_RC(rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);

    if (pTransfer)
    {
        ShClTransferRelease(pTransfer);
        pTransfer = NULL;
    }

    HGCMSvcSetU64(&aCancel[1], CancelKey.uGeneration);
    uint32_t const cStatusesBeforeCancel = g_Ext.cTransferStatuses;
    rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_CANCEL, RT_ELEMENTS(aCancel), aCancel);
    RTTESTI_CHECK_RC_OK(rc);
    tstTransferStatusGet(pvClient, idSession, idCancelTransfer, SHCLTRANSFERDIR_HOST_TO_GUEST,
                         SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);
    RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatusesBeforeCancel + 1);
    RTTESTI_CHECK(ShClTransferKeyIsEqual(&g_Ext.TransferKey, &CancelKey));
    RTTESTI_CHECK(g_Ext.enmTransferStatus == SHCLTRANSFERSTATUS_CANCELED);
    RTTESTI_CHECK(g_Ext.rcTransfer == VERR_CANCELLED);
    RTTESTI_CHECK(g_Ext.enmTransferSource == SHCLSOURCE_LOCAL);
    RTTESTI_CHECK(g_Ext.enmTransferReplySource == SHCLSOURCE_LOCAL);
    pTransfer = g_Ext.Transport.pOps->pfnTransferGetByIdRetained(g_Ext.Transport.hClient, idCancelTransfer);
    RTTESTI_CHECK(pTransfer == NULL);
    if (pTransfer)
        ShClTransferRelease(pTransfer);

    HGCMSvcSetU64(&aObjectClose[0], VBOX_SHCL_CONTEXTID_MAKE(idSession, idCancelTransfer, 0));
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(aObjectClose), aObjectClose);
    RTTESTI_CHECK_RC(rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);
    rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_CANCEL, RT_ELEMENTS(aCancel), aCancel);
    RTTESTI_CHECK_RC(rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);

    /* A data-message failure after lookup must publish terminal ERROR before
     * ordinary detached cleanup follows with UNINITIALIZED. */
    uint32_t const cStatusesBeforeErrorRequest = g_Ext.cTransferStatuses;
    tstTransferReplyInit(aReply, 0, SHCLTRANSFERSTATUS_REQUESTED);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPLY, RT_ELEMENTS(aReply), aReply);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatusesBeforeErrorRequest + 1);

    SHCLTRANSFERKEY const ErrorKey = g_Ext.TransferKey;
    SHCLSESSIONID const idErrorSession = ShClTransferKeyGetSessionId(&ErrorKey);
    SHCLTRANSFERID const idErrorTransfer = ShClTransferKeyGetTransferId(&ErrorKey);
    tstTransferStatusGet(pvClient, idErrorSession, idErrorTransfer, SHCLTRANSFERDIR_HOST_TO_GUEST,
                         SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);

    HGCMSvcSetU64(&aObjectClose[0], VBOX_SHCL_CONTEXTID_MAKE(idErrorSession, idErrorTransfer, 0));
    HGCMSvcSetU32(&aObjectClose[1], 1); /* Deliberately wrong handle type. */
    uint32_t const cStatusesBeforeError = g_Ext.cTransferStatuses;
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(aObjectClose), aObjectClose);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    tstTransferStatusGet(pvClient, idErrorSession, idErrorTransfer, SHCLTRANSFERDIR_HOST_TO_GUEST,
                         SHCLTRANSFERSTATUS_ERROR, VERR_INVALID_PARAMETER);
    RTTESTI_CHECK(g_Ext.cTransferStatuses == cStatusesBeforeError + 2);
    RTTESTI_CHECK(ShClTransferKeyIsEqual(&g_Ext.TransferKey, &ErrorKey));
    RTTESTI_CHECK(g_Ext.enmPreviousTransferStatus == SHCLTRANSFERSTATUS_ERROR);
    RTTESTI_CHECK(g_Ext.rcPreviousTransfer == VERR_INVALID_PARAMETER);
    RTTESTI_CHECK(g_Ext.enmTransferStatus == SHCLTRANSFERSTATUS_UNINITIALIZED);
    RTTESTI_CHECK(g_Ext.rcTransfer == VINF_SUCCESS);
    RTTESTI_CHECK(g_Ext.enmTransferSource == SHCLSOURCE_LOCAL);
    RTTESTI_CHECK(g_Ext.enmTransferReplySource == SHCLSOURCE_LOCAL);
    pTransfer = g_Ext.Transport.pOps->pfnTransferGetByIdRetained(g_Ext.Transport.hClient, idErrorTransfer);
    RTTESTI_CHECK(pTransfer == NULL);
    if (pTransfer)
        ShClTransferRelease(pTransfer);

    /* Disabling transfers synchronously resets Main before destroying the
     * service-owned transfer set. */
    PSHCLTRANSFER pModeResetTransfer = NULL;
    SHCLTRANSFERID const idModeResetTransfer
        = tstTransferCreateRetained(SHCLTRANSFERDIR_HOST_TO_GUEST, SHCLSOURCE_LOCAL,
                                    &pModeResetTransfer);
    if (pModeResetTransfer)
    {
        tstTransferStatusNotify(pModeResetTransfer);
        ShClTransferRelease(pModeResetTransfer);
        pModeResetTransfer = NULL;
    }
    uint32_t const cResetsBeforeModeDisable = g_Ext.cTransferResets;
    HGCMSvcSetU32(&Parm, VBOX_SHCL_TRANSFER_MODE_F_NONE);
    rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, &Parm);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(g_Ext.cTransferResets == cResetsBeforeModeDisable + 1);
    if (ShClTransferIdIsValid(idModeResetTransfer))
    {
        pTransfer = g_Ext.Transport.pOps->pfnTransferGetByIdRetained(g_Ext.Transport.hClient,
                                                                     idModeResetTransfer);
        RTTESTI_CHECK(pTransfer == NULL);
        if (pTransfer)
            ShClTransferRelease(pTransfer);
    }

    HGCMSvcSetU32(&Parm, VBOX_SHCL_TRANSFER_MODE_F_ENABLED);
    rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, &Parm);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    /* The legacy parameterless host cancel resets the complete client session. */
    PSHCLTRANSFER pLegacyResetTransfer = NULL;
    SHCLTRANSFERID const idLegacyResetTransfer
        = tstTransferCreateRetained(SHCLTRANSFERDIR_HOST_TO_GUEST, SHCLSOURCE_LOCAL,
                                    &pLegacyResetTransfer);
    if (pLegacyResetTransfer)
    {
        tstTransferStatusNotify(pLegacyResetTransfer);
        ShClTransferRelease(pLegacyResetTransfer);
        pLegacyResetTransfer = NULL;
    }
    PSHCLCLIENT const pClient = (PSHCLCLIENT)g_Ext.Transport.hClient;
    SHCLSESSIONID const idLegacySession = pClient->State.uSessionID;
    uint32_t const cResetsBeforeLegacyCancel = g_Ext.cTransferResets;
    rc = g_Table.pfnHostCall(g_Table.pvService, VBOX_SHCL_HOST_FN_CANCEL, 0, NULL);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(g_Ext.cTransferResets == cResetsBeforeLegacyCancel + 1);
    RTTESTI_CHECK(pClient->State.uSessionID != idLegacySession);
    if (ShClTransferIdIsValid(idLegacyResetTransfer))
    {
        pTransfer = g_Ext.Transport.pOps->pfnTransferGetByIdRetained(g_Ext.Transport.hClient,
                                                                     idLegacyResetTransfer);
        RTTESTI_CHECK(pTransfer == NULL);
        if (pTransfer)
            ShClTransferRelease(pTransfer);
    }

    HGCMSvcSetU64(&aFeatures[0], VBOX_SHCL_GF_0_CONTEXT_ID | VBOX_SHCL_GF_0_TRANSFERS);
    HGCMSvcSetU64(&aFeatures[1], VBOX_SHCL_GF_1_MUST_BE_ONE);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FEATURES,
                      RT_ELEMENTS(aFeatures), aFeatures);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    /* A guest-reported fatal clipboard error begins another service session. */
    PSHCLTRANSFER pGuestResetTransfer = NULL;
    SHCLTRANSFERID const idGuestResetTransfer
        = tstTransferCreateRetained(SHCLTRANSFERDIR_HOST_TO_GUEST, SHCLSOURCE_LOCAL,
                                    &pGuestResetTransfer);
    if (pGuestResetTransfer)
    {
        tstTransferStatusNotify(pGuestResetTransfer);
        ShClTransferRelease(pGuestResetTransfer);
        pGuestResetTransfer = NULL;
    }
    SHCLSESSIONID const idGuestSession = pClient->State.uSessionID;
    VBOXHGCMSVCPARM aGuestError[VBOX_SHCL_CPARMS_ERROR];
    HGCMSvcSetU64(&aGuestError[0], VBOX_SHCL_CONTEXTID_MAKE(idGuestSession, 0, 0));
    HGCMSvcSetU32(&aGuestError[1], (uint32_t)VERR_GENERAL_FAILURE);
    uint32_t const cResetsBeforeGuestError = g_Ext.cTransferResets;
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_ERROR,
                      RT_ELEMENTS(aGuestError), aGuestError);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    RTTESTI_CHECK(g_Ext.cTransferResets == cResetsBeforeGuestError + 1);
    RTTESTI_CHECK(pClient->State.uSessionID != idGuestSession);
    if (ShClTransferIdIsValid(idGuestResetTransfer))
    {
        pTransfer = g_Ext.Transport.pOps->pfnTransferGetByIdRetained(g_Ext.Transport.hClient,
                                                                     idGuestResetTransfer);
        RTTESTI_CHECK(pTransfer == NULL);
        if (pTransfer)
            ShClTransferRelease(pTransfer);
    }

    HGCMSvcSetU64(&aFeatures[0], VBOX_SHCL_GF_0_CONTEXT_ID | VBOX_SHCL_GF_0_TRANSFERS);
    HGCMSvcSetU64(&aFeatures[1], VBOX_SHCL_GF_1_MUST_BE_ONE);
    rc = tstGuestCall(pvClient, VBOX_SHCL_GUEST_FN_REPORT_FEATURES,
                      RT_ELEMENTS(aFeatures), aFeatures);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
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
    RTTESTI_CHECK(g_Ext.cBackendDestroys == 1);
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
            tstTransferRootsFsRoot();
            tstTransferEventCancellation();
            tstTransferKeyHelpers();
            tstTransferProgressHelpers();
            tstTransfers(pvClient);
#endif
        }
        tstShutdown(pvClient);
    }
    else
        RTTestIFailed("Loading the host service failed: %Rrc", rc);

    return RTTestSummaryAndDestroy(g_hTest);
}
