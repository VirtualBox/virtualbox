/* $Id: tstClipboardServiceHost.cpp 114967 2026-08-10 16:15:33Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard host service test case.
 */

/*
 * Copyright (C) 2011-2026 Oracle and/or its affiliates.
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

#define LOG_ENABLED
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <VBox/log.h>

#include <VBox/HostServices/VBoxClipboardExt.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/HostServices/VBoxSharedClipboardSvc.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
# include "VBoxSharedClipboardSvc-transfers.h"
#endif

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/test.h>

extern "C" DECLCALLBACK(DECLEXPORT(int)) VBoxHGCMSvcLoad (VBOXHGCMSVCFNTABLE *ptable);
/**
 * The following no-op functions which correspond to their Shared Clipboard
 * backend namesakes (ShClBackend*()) are used by the dispatcher function
 * below (tstHgcmMockSvcDispatcher()) for intercepting unused backend calls.
 *
 * Note: These host service tests exercise the HGCM service layer,
 *       not the platform clipboard backends!
 */
static bool g_fBackendConnectCalled = false;
static bool g_fBackendConnectHeadless = false;
static int tstShClBackendInit(PSHCLBACKEND pBackend, VBOXHGCMSVCFNTABLE *pTable) { pBackend->pHelpers = pTable->pHelpers; return VINF_SUCCESS; }
static void tstShClBackendDestroy(PSHCLBACKEND) { }
static int tstShClBackendConnect(PSHCLBACKEND, PSHCLCLIENT, bool fHeadless)
{
    g_fBackendConnectCalled = true;
    g_fBackendConnectHeadless = fHeadless;
    return VINF_SUCCESS;
}
static int tstShClBackendDisconnect(PSHCLBACKEND, PSHCLCLIENT) { return VINF_SUCCESS; }
static int tstShClBackendSync(PSHCLBACKEND, PSHCLCLIENT) { return VINF_SUCCESS; }
static int tstShClBackendReportFormats(PSHCLBACKEND, PSHCLCLIENT, SHCLFORMATS) { AssertFailed(); return VINF_SUCCESS; }
static int tstShClBackendReportFormatsToGuest(PSHCLBACKEND, PSHCLCLIENT, uint32_t) { AssertFailed(); return VINF_SUCCESS; }
static const void *g_pvBackendReadData = NULL;
static uint32_t g_cbBackendReadData = 0;
static SHCLFORMAT g_uBackendReadFormat = VBOX_SHCL_FMT_NONE;
static uint32_t g_cBackendReadDataCalls = 0;
static int tstShClBackendReadData(PSHCLBACKEND, PSHCLCLIENT, PSHCLCLIENTCMDCTX, SHCLFORMAT uFormat,
                                  void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    g_cBackendReadDataCalls++;
    AssertPtrReturn(g_pvBackendReadData, VERR_WRONG_ORDER);
    AssertReturn(uFormat == g_uBackendReadFormat, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbActual, VERR_INVALID_POINTER);

    *pcbActual = g_cbBackendReadData;
    if (g_cbBackendReadData <= cbData)
        memcpy(pvData, g_pvBackendReadData, g_cbBackendReadData);
    return VINF_SUCCESS;
}
static int tstShClBackendWriteData(PSHCLBACKEND, PSHCLCLIENT, PSHCLCLIENTCMDCTX, SHCLFORMAT, void *, uint32_t) { AssertFailed(); return VINF_SUCCESS; }

static SHCLCLIENT g_Client;
static VBOXHGCMSVCHELPERS g_Helpers = { NULL };

/** Simple call handle structure for the guest call completion callback */
struct VBOXHGCMCALLHANDLE_TYPEDEF
{
    /** Where to store the result code */
    int32_t rc;
};

/** Call completion callback for guest calls. */
static DECLCALLBACK(int) callComplete(VBOXHGCMCALLHANDLE callHandle, int32_t rc)
{
    callHandle->rc = rc;
    return VINF_SUCCESS;
}

/**
 * A copy of the GuestShCl::hgcmDispatcher() dispatcher routine which
 * handles callbacks from the Shared Clipboard host service.  For the
 * variety of tests here we only need ShClBackendInit() to get called to
 * setup the shared clipboard for the tests and the remainder of the
 * backend routines are routed to no-op equivalent routines.
 */
DECLCALLBACK(int) tstHgcmMockSvcDispatcher(void *pvExtension, uint32_t u32Function,
                                           void *pvParms, uint32_t cbParms)
{
    NOREF(pvExtension);
    int rc = VINF_SUCCESS;
    PSHCLEXTPARMS pParms = (PSHCLEXTPARMS)pvParms; /* pParms might be NULL, depending on the message. */

    LogFlowFunc(("u32Function=%RU32, pvParms=%p, cbParms=%RU32\n", u32Function, pvParms, cbParms));

    switch (u32Function)
    {
        case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST: // via VBOX_SHCL_GUEST_FN_REPORT_FORMATS in the guest
        {
            PSHCLCLIENT pClient = pParms->u.ReportFormats.pClient;
            SHCLFORMATS fFormats = pParms->u.ReportFormats.uFormats;

            rc = tstShClBackendReportFormats(pClient->pBackend, pClient, fFormats);
            if (RT_FAILURE(rc))
                LogRel(("Shared Clipboard: Reporting guest clipboard formats to the host failed with %Rrc\n", rc));
            break;
        }

        case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_GUEST:
        {
            PSHCLCLIENT pClient = pParms->u.ReportFormats.pClient;
            SHCLFORMATS fFormats = pParms->u.ReportFormats.uFormats;

            rc = tstShClBackendReportFormatsToGuest(pClient->pBackend, pClient, fFormats);
            break;
        }

        case VBOX_CLIPBOARD_EXT_FN_DATA_READ: // via VBOX_SHCL_GUEST_FN_DATA_READ in the guest
        {
            PSHCLCLIENT pClient = pParms->u.ReadWriteData.pClient;
            SHCLCLIENTCMDCTX cmdCtx;
            void *pvData = pParms->u.ReadWriteData.pvData;
            uint32_t cbData = pParms->u.ReadWriteData.cbData;
            SHCLFORMATS fFormats = pParms->u.ReadWriteData.uFormat;
            rc = tstShClBackendReadData(pClient->pBackend, pClient, &cmdCtx, fFormats, pvData, cbData,
                &pParms->u.ReadWriteData.cbActual);
            if (RT_SUCCESS(rc))
                LogRel2(("Shared Clipboard: Read host clipboard data (max %RU32 bytes), got %RU32 bytes\n", cbData,
                    pParms->u.ReadWriteData.cbActual));
            else
                LogRel(("Shared Clipboard: Reading host clipboard data failed with %Rrc\n", rc));
            break;
        }

        case VBOX_CLIPBOARD_EXT_FN_DATA_WRITE: // via VBOX_SHCL_GUEST_FN_DATA_WRITE in the guest
        {
            PSHCLCLIENT pClient = pParms->u.ReadWriteData.pClient;
            PSHCLCLIENTCMDCTX pCmdCtx = pParms->u.ReadWriteData.pCmdCtx;
            void *pvData = pParms->u.ReadWriteData.pvData;
            uint32_t cbData = pParms->u.ReadWriteData.cbData;
            SHCLFORMATS fFormats = pParms->u.ReadWriteData.uFormat;
            rc = tstShClBackendWriteData(pClient->pBackend, pClient, pCmdCtx, fFormats, pvData, cbData);
            if (RT_FAILURE(rc))
                LogRel(("Shared Clipboard: Writing guest clipboard data to the host failed with %Rrc\n", rc));
            /* Complete any pending events. */
            int rc2 = ShClSvcGuestDataSignal(pClient, pCmdCtx, fFormats, pvData, cbData);
            if (RT_FAILURE(rc2))
                LogRel(("Shared Clipboard: Signalling host about guest clipboard data failed with %Rrc\n", rc2));
            AssertRC(rc2);
            break;
        }

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT:
        {
            PSHCLBACKEND pBackend = pParms->u.ReadWriteData.pBackend;
            VBOXHGCMSVCFNTABLE *pTable = pParms->u.ReadWriteData.pTable;
            rc = tstShClBackendInit(pBackend, pTable);
            break;
        }

        // via VbglR3HGCMDisconnect()->...HGCMService::DisconnectClient()->...HGCMService::instanceDestroy()->...shClSvcUnload()
        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY:
        {
            PSHCLBACKEND pBackend = pParms->u.ReadWriteData.pBackend;
            tstShClBackendDestroy(pBackend);
            rc = VINF_SUCCESS;
            break;
        }

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT: // via VbglR3ClipboardConnect()->VbglR3HGCMConnect() in the guest
        {
            PSHCLBACKEND pBackend = pParms->u.ReadWriteData.pBackend;
            PSHCLCLIENT pClient = pParms->u.ReadWriteData.pClient;
            bool fHeadless = pParms->u.ReadWriteData.fHeadless;
            rc = tstShClBackendConnect(pBackend, pClient, fHeadless);
            break;
        }

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT:  // via VbglR3ClipboardDisconnect()->VbglR3HGCMDisconnect() in the guest
        {
            PSHCLCLIENT pClient = pParms->u.ReadWriteData.pClient;
            rc = tstShClBackendDisconnect(pClient->pBackend, pClient);
            break;
        }

        case VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC:
        {
            PSHCLBACKEND pBackend = pParms->u.ReadWriteData.pBackend;
            PSHCLCLIENT pClient = pParms->u.ReadWriteData.pClient;
            rc = tstShClBackendSync(pBackend, pClient);
            break;
        }

        default:
            break;
    }

    return rc;
}

static int setupTable(VBOXHGCMSVCFNTABLE *pTable)
{
    pTable->cbSize = sizeof(*pTable);
    pTable->u32Version = VBOX_HGCM_SVC_VERSION;
    g_Helpers.pfnCallComplete = callComplete;
    pTable->pHelpers = &g_Helpers;
    int rc = VBoxHGCMSvcLoad(pTable);
    RTTESTI_CHECK_MSG_RET(RT_SUCCESS(rc), ("rc=%Rrc\n", rc), rc);
    return pTable->pfnRegisterExtension(pTable->pvService, tstHgcmMockSvcDispatcher, NULL);
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** Issues a host clipboard data read as a guest. */
static int testHostDataReadCall(VBOXHGCMSVCFNTABLE *pTable, SHCLFORMAT uFormat,
                                void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    VBOXHGCMSVCPARM aParms[VBOX_SHCL_CPARMS_DATA_READ];
    HGCMSvcSetU32(&aParms[0], uFormat);
    HGCMSvcSetPv(&aParms[1], pvData, cbData);
    HGCMSvcSetU32(&aParms[2], 0);

    VBOXHGCMCALLHANDLE_TYPEDEF Call;
    Call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    pTable->pfnCall(NULL, &Call, 1 /* clientId */, &g_Client,
                    VBOX_SHCL_GUEST_FN_DATA_READ, RT_ELEMENTS(aParms), aParms, 0);
    if (pcbActual)
        *pcbActual = aParms[2].u.uint32;
    return Call.rc;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

static void testSetMode(void)
{
    struct VBOXHGCMSVCPARM parms[2];
    VBOXHGCMSVCFNTABLE table;
    uint32_t u32Mode;
    int rc;

    RTTestISub("Testing VBOX_SHCL_HOST_FN_SET_MODE");
    rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    RT_ZERO(g_Client);
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    /* Reset global variable which doesn't reset itself. */
    HGCMSvcSetU32(&parms[0], VBOX_SHCL_MODE_OFF);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    u32Mode = ShClSvcGetMode();
    RTTESTI_CHECK_MSG(u32Mode == VBOX_SHCL_MODE_OFF, ("u32Mode=%u\n", (unsigned) u32Mode));

    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 0, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 2, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    HGCMSvcSetU64(&parms[0], 99);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    HGCMSvcSetU32(&parms[0], VBOX_SHCL_MODE_HOST_TO_GUEST);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    u32Mode = ShClSvcGetMode();
    RTTESTI_CHECK_MSG(u32Mode == VBOX_SHCL_MODE_HOST_TO_GUEST, ("u32Mode=%u\n", (unsigned) u32Mode));

    HGCMSvcSetU32(&parms[0], 99);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_NOT_SUPPORTED);

    u32Mode = ShClSvcGetMode();
    RTTESTI_CHECK_MSG(u32Mode == VBOX_SHCL_MODE_OFF, ("u32Mode=%u\n", (unsigned) u32Mode));

    rc = table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    rc = table.pfnUnload(NULL);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
static void testSetTransferMode(void)
{
    struct VBOXHGCMSVCPARM parms[2];
    VBOXHGCMSVCFNTABLE table;

    RTTestISub("Testing VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE");
    int rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    RT_ZERO(g_Client);
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    /* Invalid parameter. */
    HGCMSvcSetU64(&parms[0], 99);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    /* Invalid mode. */
    HGCMSvcSetU32(&parms[0], 99);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_FLAGS);

    /* Enable transfers. */
    HGCMSvcSetU32(&parms[0], VBOX_SHCL_TRANSFER_MODE_F_ENABLED);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    /* Disable transfers again. */
    HGCMSvcSetU32(&parms[0], VBOX_SHCL_TRANSFER_MODE_F_NONE);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    rc = table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    rc = table.pfnUnload(NULL);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}

static void testGetTransferStatusMessage(VBOXHGCMSVCFNTABLE *pTable, SHCLSESSIONID idSessionExpected,
                                         SHCLTRANSFERID idTransferExpected, SHCLTRANSFERSTATUS enmStatusExpected,
                                         int rcTransferExpected)
{
    struct VBOXHGCMSVCPARM aStatusParms[VBOX_SHCL_CPARMS_TRANSFER_STATUS];
    VBOXHGCMCALLHANDLE_TYPEDEF call;

    HGCMSvcSetU64(&aStatusParms[0], VBOX_SHCL_HOST_MSG_TRANSFER_STATUS);
    HGCMSvcSetU32(&aStatusParms[1], 0);
    HGCMSvcSetU32(&aStatusParms[2], 0);
    HGCMSvcSetU32(&aStatusParms[3], 0);
    HGCMSvcSetU32(&aStatusParms[4], 0);

    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    pTable->pfnCall(NULL, &call, 1 /* clientId */, &g_Client,
                    VBOX_SHCL_GUEST_FN_MSG_GET, RT_ELEMENTS(aStatusParms), aStatusParms, 0);
    RTTESTI_CHECK_RC_OK(call.rc);
    uint64_t const uContext = aStatusParms[0].u.uint64;
    RTTESTI_CHECK(VBOX_SHCL_CONTEXTID_GET_SESSION(uContext) == idSessionExpected);
    RTTESTI_CHECK(VBOX_SHCL_CONTEXTID_GET_TRANSFER(uContext) == idTransferExpected);
    RTTESTI_CHECK(VBOX_SHCL_CONTEXTID_GET_EVENT(uContext) != 0);
    RTTESTI_CHECK(VBOX_SHCL_CONTEXTID_GET_EVENT(uContext) != NIL_SHCLEVENTID);
    RTTESTI_CHECK(aStatusParms[2].u.uint32 == enmStatusExpected);
    RTTESTI_CHECK((int32_t)aStatusParms[3].u.uint32 == rcTransferExpected);
}

static void testSetTransferKeyParms(VBOXHGCMSVCPARM aParms[], SHCLSESSIONID idSession,
                                    SHCLTRANSFERID idTransfer, SHCLTRANSFERGEN uGeneration)
{
    HGCMSvcSetU64(&aParms[0], VBOX_SHCL_CONTEXTID_MAKE(idSession, idTransfer, 0));
    HGCMSvcSetU64(&aParms[1], uGeneration);
}

/** Tests short and exactly-sized host clipboard data reads. */
static void testHostDataReadBufferSizing(void)
{
    VBOXHGCMSVCFNTABLE table;
    VBOXHGCMSVCPARM parms[1];
    VBOXHGCMSVCPARM aReadParms[VBOX_SHCL_CPARMS_DATA_READ];
    VBOXHGCMCALLHANDLE_TYPEDEF call;
    static uint8_t s_abData[_4K + 17];
    uint8_t abShort[_4K];
    uint8_t abExact[sizeof(s_abData)];

    RTTestISub("Testing host data read buffer sizing");
    int rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    HGCMSvcSetU32(&parms[0], VBOX_SHCL_MODE_BIDIRECTIONAL);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, RT_ELEMENTS(parms), parms);
    RTTESTI_CHECK_RC_OK(rc);

    RT_ZERO(g_Client);
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    for (uint32_t off = 0; off < sizeof(s_abData); off++)
        s_abData[off] = (uint8_t)off;
    g_pvBackendReadData = s_abData;
    g_cbBackendReadData = sizeof(s_abData);
    g_uBackendReadFormat = VBOX_SHCL_FMT_UNICODETEXT;
    g_cBackendReadDataCalls = 0;

    HGCMSvcSetU32(&aReadParms[0], g_uBackendReadFormat);
    HGCMSvcSetPv(&aReadParms[1], abShort, sizeof(abShort));
    HGCMSvcSetU32(&aReadParms[2], 0);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client,
                  VBOX_SHCL_GUEST_FN_DATA_READ, RT_ELEMENTS(aReadParms), aReadParms, 0);
    RTTESTI_CHECK_RC(call.rc, VINF_BUFFER_OVERFLOW);
    RTTESTI_CHECK(aReadParms[2].u.uint32 == sizeof(s_abData));

    HGCMSvcSetPv(&aReadParms[1], abExact, sizeof(abExact));
    HGCMSvcSetU32(&aReadParms[2], 0);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client,
                  VBOX_SHCL_GUEST_FN_DATA_READ, RT_ELEMENTS(aReadParms), aReadParms, 0);
    RTTESTI_CHECK_RC_OK(call.rc);
    RTTESTI_CHECK(aReadParms[2].u.uint32 == sizeof(s_abData));
    RTTESTI_CHECK(memcmp(abExact, s_abData, sizeof(s_abData)) == 0);
    RTTESTI_CHECK(g_cBackendReadDataCalls == 2);

    g_pvBackendReadData = NULL;
    g_cbBackendReadData = 0;
    g_uBackendReadFormat = VBOX_SHCL_FMT_NONE;
    g_cBackendReadDataCalls = 0;

    rc = table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);
    RTTESTI_CHECK_RC_OK(rc);
    rc = table.pfnUnload(NULL);
    RTTESTI_CHECK_RC_OK(rc);
}

/** Tests validation and feature gating for host clipboard data reads. */
static void testHostDataReadValidation(void)
{
    VBOXHGCMSVCFNTABLE table;
    VBOXHGCMSVCPARM Parm;
    static const char s_szUriList[] = "file:///private/host-file.txt\r\n";
    char szData[sizeof(s_szUriList)];

    RTTestISub("Testing host data read validation");
    int rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    HGCMSvcSetU32(&Parm, VBOX_SHCL_MODE_BIDIRECTIONAL);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, &Parm);
    RTTESTI_CHECK_RC_OK(rc);

    HGCMSvcSetU32(&Parm, VBOX_SHCL_TRANSFER_MODE_F_NONE);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, &Parm);
    RTTESTI_CHECK_RC_OK(rc);

    RT_ZERO(g_Client);
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    g_pvBackendReadData = s_szUriList;
    g_cbBackendReadData = sizeof(s_szUriList);
    g_uBackendReadFormat = VBOX_SHCL_FMT_UNICODETEXT;
    g_cBackendReadDataCalls = 0;

    g_Client.State.fGuestFeatures0 = VBOX_SHCL_GF_NONE;
    uint32_t cbActual = 0;
    rc = testHostDataReadCall(&table, VBOX_SHCL_FMT_UNICODETEXT, szData, sizeof(szData), &cbActual);
    RTTESTI_CHECK_RC_OK(rc);
    RTTESTI_CHECK(cbActual == sizeof(s_szUriList));
    RTTESTI_CHECK(memcmp(szData, s_szUriList, sizeof(s_szUriList)) == 0);
    RTTESTI_CHECK(g_cBackendReadDataCalls == 1);

    g_uBackendReadFormat = VBOX_SHCL_FMT_URI_LIST;
    g_cBackendReadDataCalls = 0;
    g_Client.State.fGuestFeatures0 = VBOX_SHCL_GF_0_CONTEXT_ID | VBOX_SHCL_GF_0_TRANSFERS;
    rc = testHostDataReadCall(&table, VBOX_SHCL_FMT_URI_LIST, szData, sizeof(szData), &cbActual);
    RTTESTI_CHECK_RC(rc, VERR_ACCESS_DENIED);
    RTTESTI_CHECK(g_cBackendReadDataCalls == 0);

    HGCMSvcSetU32(&Parm, VBOX_SHCL_TRANSFER_MODE_F_ENABLED);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, &Parm);
    RTTESTI_CHECK_RC_OK(rc);

    static const uint64_t s_afMissingFeatures[] =
    {
        VBOX_SHCL_GF_NONE,
        VBOX_SHCL_GF_0_CONTEXT_ID,
        VBOX_SHCL_GF_0_TRANSFERS
    };
    for (size_t i = 0; i < RT_ELEMENTS(s_afMissingFeatures); i++)
    {
        g_Client.State.fGuestFeatures0 = s_afMissingFeatures[i];
        rc = testHostDataReadCall(&table, VBOX_SHCL_FMT_URI_LIST, szData, sizeof(szData), &cbActual);
        RTTESTI_CHECK_RC(rc, VERR_ACCESS_DENIED);
        RTTESTI_CHECK(g_cBackendReadDataCalls == 0);
    }

    g_Client.State.fGuestFeatures0 = VBOX_SHCL_GF_0_CONTEXT_ID | VBOX_SHCL_GF_0_TRANSFERS;
    rc = testHostDataReadCall(&table, VBOX_SHCL_FMT_URI_LIST, szData, sizeof(szData), &cbActual);
    RTTESTI_CHECK_RC_OK(rc);
    RTTESTI_CHECK(cbActual == sizeof(s_szUriList));
    RTTESTI_CHECK(memcmp(szData, s_szUriList, sizeof(s_szUriList)) == 0);
    RTTESTI_CHECK(g_cBackendReadDataCalls == 1);

    rc = testHostDataReadCall(&table, VBOX_SHCL_FMT_URI_LIST | VBOX_SHCL_FMT_UNICODETEXT,
                              szData, sizeof(szData), &cbActual);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    RTTESTI_CHECK(g_cBackendReadDataCalls == 1);

    rc = testHostDataReadCall(&table, VBOX_SHCL_FMT_NONE, szData, sizeof(szData), &cbActual);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    RTTESTI_CHECK(g_cBackendReadDataCalls == 1);

    rc = testHostDataReadCall(&table, VBOX_SHCL_FMT_VALID_MASK + 1, szData, sizeof(szData), &cbActual);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    RTTESTI_CHECK(g_cBackendReadDataCalls == 1);

    HGCMSvcSetU32(&Parm, VBOX_SHCL_TRANSFER_MODE_F_NONE);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, &Parm);
    RTTESTI_CHECK_RC_OK(rc);

    g_pvBackendReadData = NULL;
    g_cbBackendReadData = 0;
    g_uBackendReadFormat = VBOX_SHCL_FMT_NONE;
    g_cBackendReadDataCalls = 0;

    rc = table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);
    RTTESTI_CHECK_RC_OK(rc);
    rc = table.pfnUnload(NULL);
    RTTESTI_CHECK_RC_OK(rc);
}

/**
 * Tests transfer format filtering for disabled or unsupported transfers.
 */
static void testTransferFormatFiltering(void)
{
    RTTestISub("Testing transfer format filtering");

    static const struct
    {
        const char *pszName;
        uint32_t    fTransferMode;
        uint64_t    fGuestFeatures0;
        SHCLFORMATS fExpectedHostToGuest;
        SHCLFORMATS fExpectedGuestToHost;
    } s_aTests[] =
    {
        { "disabled",      VBOX_SHCL_TRANSFER_MODE_F_NONE,
          VBOX_SHCL_GF_0_CONTEXT_ID | VBOX_SHCL_GF_0_TRANSFERS,
          VBOX_SHCL_FMT_UNICODETEXT, VBOX_SHCL_FMT_UNICODETEXT },
        { "no features",   VBOX_SHCL_TRANSFER_MODE_F_ENABLED, VBOX_SHCL_GF_NONE,
          VBOX_SHCL_FMT_UNICODETEXT, VBOX_SHCL_FMT_UNICODETEXT },
        { "transfers",     VBOX_SHCL_TRANSFER_MODE_F_ENABLED, VBOX_SHCL_GF_0_TRANSFERS,
          VBOX_SHCL_FMT_UNICODETEXT, VBOX_SHCL_FMT_UNICODETEXT },
        { "context-id",    VBOX_SHCL_TRANSFER_MODE_F_ENABLED, VBOX_SHCL_GF_0_CONTEXT_ID,
          VBOX_SHCL_FMT_UNICODETEXT, VBOX_SHCL_FMT_UNICODETEXT },
        { "7.2 transfer", VBOX_SHCL_TRANSFER_MODE_F_ENABLED,
          VBOX_SHCL_GF_0_CONTEXT_ID | VBOX_SHCL_GF_0_TRANSFERS,
          VBOX_SHCL_FMT_URI_LIST, VBOX_SHCL_FMT_URI_LIST | VBOX_SHCL_FMT_UNICODETEXT },
        { "frontend",     VBOX_SHCL_TRANSFER_MODE_F_ENABLED,
          VBOX_SHCL_GF_0_CONTEXT_ID | VBOX_SHCL_GF_0_TRANSFERS | VBOX_SHCL_GF_0_TRANSFERS_FRONTEND,
          VBOX_SHCL_FMT_URI_LIST, VBOX_SHCL_FMT_URI_LIST | VBOX_SHCL_FMT_UNICODETEXT }
    };

    SHCLFORMATS const fInput = VBOX_SHCL_FMT_URI_LIST | VBOX_SHCL_FMT_UNICODETEXT;
    for (size_t i = 0; i < RT_ELEMENTS(s_aTests); i++)
    {
        RT_ZERO(g_Client);
        g_Client.State.Transfers.uTransferMode = s_aTests[i].fTransferMode;
        g_Client.State.fGuestFeatures0 = s_aTests[i].fGuestFeatures0;
        for (unsigned iDirection = 0; iDirection < 2; iDirection++)
        {
            bool const fHostToGuest = iDirection == 0;
            SHCLFORMATS const fExpected = fHostToGuest ? s_aTests[i].fExpectedHostToGuest
                                                       : s_aTests[i].fExpectedGuestToHost;
            SHCLFORMATS const fFiltered = shClSvcHandleFormats(fHostToGuest, &g_Client, fInput);
            RTTESTI_CHECK_MSG(fFiltered == fExpected,
                              ("%s/%s fFiltered=%#x expected=%#x\n", s_aTests[i].pszName,
                               fHostToGuest ? "H2G" : "G2H", fFiltered, fExpected));
        }
    }
}


/**
 * Tests that transfer messages require both guest feature bits.
 */
static void testTransferGuestFeatures(void)
{
    struct VBOXHGCMSVCPARM parms[2];
    VBOXHGCMSVCFNTABLE table;
    VBOXHGCMCALLHANDLE_TYPEDEF call;

    RTTestISub("Testing transfer guest features");
    int rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    HGCMSvcSetU32(&parms[0], VBOX_SHCL_MODE_BIDIRECTIONAL);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);

    HGCMSvcSetU32(&parms[0], VBOX_SHCL_TRANSFER_MODE_F_ENABLED);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);

    RT_ZERO(g_Client);
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    HGCMSvcSetU64(&parms[0], VBOX_SHCL_CONTEXTID_MAKE(g_Client.State.uSessionID, 42, 0));
    HGCMSvcSetU64(&parms[1], 1);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client,
                  VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(parms), parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_NOT_IMPLEMENTED);

    g_Client.State.fGuestFeatures0 = VBOX_SHCL_GF_0_TRANSFERS;
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client,
                  VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(parms), parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_NOT_IMPLEMENTED);

    g_Client.State.fGuestFeatures0 = VBOX_SHCL_GF_0_CONTEXT_ID;
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client,
                  VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(parms), parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_ACCESS_DENIED);

    g_Client.State.fGuestFeatures0 |= VBOX_SHCL_GF_0_TRANSFERS;
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client,
                  VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(parms), parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);

    rc = table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    rc = table.pfnUnload(NULL);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}

static void testTransferHostCancelError(void)
{
    struct VBOXHGCMSVCPARM parms[3];
    struct VBOXHGCMSVCPARM aObjCloseParms[VBOX_SHCL_CPARMS_OBJ_CLOSE];
    VBOXHGCMSVCFNTABLE table;
    VBOXHGCMCALLHANDLE_TYPEDEF call;

    RTTestISub("Testing transfer host cancel/error");
    int rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    HGCMSvcSetU32(&parms[0], VBOX_SHCL_MODE_BIDIRECTIONAL);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);

    HGCMSvcSetU32(&parms[0], VBOX_SHCL_TRANSFER_MODE_F_ENABLED);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);

    RT_ZERO(g_Client);
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));
    RTTESTI_CHECK(g_Client.State.uSessionID != 0);
    RTTESTI_CHECK(g_Client.State.uSessionID != NIL_SHCLSESSIONID);
    g_Client.State.fGuestFeatures0 |= VBOX_SHCL_GF_0_CONTEXT_ID | VBOX_SHCL_GF_0_TRANSFERS;

    struct VBOXHGCMSVCPARM aReplyParms[VBOX_SHCL_CPARMS_REPLY_MIN + 1];
    HGCMSvcSetU64(&aReplyParms[0], 0 /* no transfer context for SHCLTRANSFERSTATUS_REQUESTED */);
    HGCMSvcSetU32(&aReplyParms[1], VBOX_SHCL_TX_REPLYMSGTYPE_TRANSFER_STATUS);
    HGCMSvcSetU32(&aReplyParms[2], VINF_SUCCESS);
    HGCMSvcSetPv(&aReplyParms[3], NULL, 0);
    HGCMSvcSetU32(&aReplyParms[4], SHCLTRANSFERSTATUS_REQUESTED);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client,
                  VBOX_SHCL_GUEST_FN_REPLY, RT_ELEMENTS(aReplyParms), aReplyParms, 0);
    RTTESTI_CHECK_RC_OK(call.rc);
    PSHCLTRANSFER pTransferRequested = ShClTransferCtxGetTransferByIndex(&g_Client.Transfers.Ctx, 0);
    RTTESTI_CHECK(pTransferRequested != NULL);
    SHCLTRANSFERID const idTransferRequested = ShClTransferGetID(pTransferRequested);
    testGetTransferStatusMessage(&table, g_Client.State.uSessionID, idTransferRequested, SHCLTRANSFERSTATUS_REQUESTED, VINF_SUCCESS);
    ShClSvcTransferDestroy(&g_Client, pTransferRequested);

    SHCLSESSIONID const idSessionBeforeReset = g_Client.State.uSessionID;
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_CANCEL, 0, parms);
    RTTESTI_CHECK_RC_OK(rc);
    RTTESTI_CHECK(g_Client.State.uSessionID != idSessionBeforeReset);
    g_Client.State.fGuestFeatures0 |= VBOX_SHCL_GF_0_CONTEXT_ID | VBOX_SHCL_GF_0_TRANSFERS;

    HGCMSvcSetU32(&parms[0], 42);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_CANCEL, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    testSetTransferKeyParms(parms, g_Client.State.uSessionID, 42, 1);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_CANCEL, 2, parms);
    RTTESTI_CHECK_RC(rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);

    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_CANCEL, 3, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    PSHCLTRANSFER pTransfer;
    rc = ShClSvcTransferCreate(&g_Client, SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL,
                               NIL_SHCLTRANSFERID, &pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
    SHCLSESSIONID const idSessionCancel = ShClTransferGetSessionId(pTransfer);
    SHCLTRANSFERID const idTransferCancel = ShClTransferGetID(pTransfer);
    SHCLTRANSFERGEN const uGenerationCancel = ShClTransferGetGeneration(pTransfer);
    RTTESTI_CHECK(idSessionCancel == g_Client.State.uSessionID);
    RTTESTI_CHECK(uGenerationCancel != 0);
    RTTESTI_CHECK(uGenerationCancel != NIL_SHCLTRANSFERGEN);

    testSetTransferKeyParms(parms, idSessionCancel + 1, idTransferCancel, uGenerationCancel);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_CANCEL, 2, parms);
    RTTESTI_CHECK_RC(rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);
    RTTESTI_CHECK(ShClTransferCtxGetTransferById(&g_Client.Transfers.Ctx, idTransferCancel) != NULL);

    testSetTransferKeyParms(parms, idSessionCancel, idTransferCancel, uGenerationCancel + 1);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_CANCEL, 2, parms);
    RTTESTI_CHECK_RC(rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);
    RTTESTI_CHECK(ShClTransferCtxGetTransferById(&g_Client.Transfers.Ctx, idTransferCancel) != NULL);

    HGCMSvcSetU64(&aObjCloseParms[0], VBOX_SHCL_CONTEXTID_MAKE(idSessionCancel + 1, idTransferCancel, 0));
    HGCMSvcSetU64(&aObjCloseParms[1], 1);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client,
                  VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(aObjCloseParms), aObjCloseParms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_INVALID_CONTEXT);
    RTTESTI_CHECK(ShClTransferCtxGetTransferById(&g_Client.Transfers.Ctx, idTransferCancel) != NULL);

    testSetTransferKeyParms(parms, idSessionCancel, idTransferCancel, uGenerationCancel);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_CANCEL, 2, parms);
    RTTESTI_CHECK_RC_OK(rc);
    RTTESTI_CHECK(ShClTransferCtxGetTransferById(&g_Client.Transfers.Ctx, idTransferCancel) == NULL);
    testGetTransferStatusMessage(&table, idSessionCancel, idTransferCancel, SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);

    HGCMSvcSetU64(&aObjCloseParms[0], VBOX_SHCL_CONTEXTID_MAKE(idSessionCancel, idTransferCancel, 0));
    HGCMSvcSetU64(&aObjCloseParms[1], 1);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client,
                  VBOX_SHCL_GUEST_FN_OBJ_CLOSE, RT_ELEMENTS(aObjCloseParms), aObjCloseParms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);

    testSetTransferKeyParms(parms, g_Client.State.uSessionID, 42, 1);
    HGCMSvcSetU32(&parms[2], (uint32_t)VERR_ACCESS_DENIED);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_ERROR, 3, parms);
    RTTESTI_CHECK_RC(rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);

    rc = ShClSvcTransferCreate(&g_Client, SHCLTRANSFERDIR_TO_REMOTE, SHCLSOURCE_LOCAL,
                               NIL_SHCLTRANSFERID, &pTransfer);
    RTTESTI_CHECK_RC_OK(rc);
    SHCLSESSIONID const idSessionError = ShClTransferGetSessionId(pTransfer);
    SHCLTRANSFERID const idTransferError = ShClTransferGetID(pTransfer);
    SHCLTRANSFERGEN const uGenerationError = ShClTransferGetGeneration(pTransfer);

    testSetTransferKeyParms(parms, idSessionError, idTransferError, uGenerationError);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_ERROR, 2, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    HGCMSvcSetU32(&parms[2], (uint32_t)VERR_CANCELLED);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_ERROR, 3, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    RTTESTI_CHECK(ShClTransferCtxGetTransferById(&g_Client.Transfers.Ctx, idTransferError) != NULL);

    testSetTransferKeyParms(parms, idSessionError, idTransferError, uGenerationError + 1);
    HGCMSvcSetU32(&parms[2], (uint32_t)VERR_ACCESS_DENIED);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_ERROR, 3, parms);
    RTTESTI_CHECK_RC(rc, VERR_SHCLPB_TRANSFER_ID_NOT_FOUND);
    RTTESTI_CHECK(ShClTransferCtxGetTransferById(&g_Client.Transfers.Ctx, idTransferError) != NULL);

    testSetTransferKeyParms(parms, idSessionError, idTransferError, uGenerationError);
    HGCMSvcSetU32(&parms[2], (uint32_t)VERR_ACCESS_DENIED);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_ERROR, 3, parms);
    RTTESTI_CHECK_RC_OK(rc);
    RTTESTI_CHECK(ShClTransferCtxGetTransferById(&g_Client.Transfers.Ctx, idTransferError) == NULL);
    testGetTransferStatusMessage(&table, idSessionError, idTransferError, SHCLTRANSFERSTATUS_ERROR, VERR_ACCESS_DENIED);

    HGCMSvcSetU32(&parms[0], VBOX_SHCL_TRANSFER_MODE_F_NONE);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);

    rc = table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    rc = table.pfnUnload(NULL);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

/* Adds a host data read request message to the client's message queue. */
static void testMsgAddReadData(PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
    int rc = ShClSvcReadDataFromGuestAsync(pClient, fFormats, NULL /* ppEvent */);
    RTTESTI_CHECK_RC_OK(rc);
}

/* Does testing of VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT, needed for providing compatibility to older Guest Additions clients. */
static void testGetHostMsgOld(void)
{
    struct VBOXHGCMSVCPARM parms[2];
    VBOXHGCMSVCFNTABLE table;
    VBOXHGCMCALLHANDLE_TYPEDEF call;
    int rc;

    RTTestISub("Setting up VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT test");
    rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));
    /* Unless we are bidirectional the host message requests will be dropped. */
    HGCMSvcSetU32(&parms[0], VBOX_SHCL_MODE_BIDIRECTIONAL);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);


    RTTestISub("Testing one format, waiting guest call.");
    RT_ZERO(g_Client);
    HGCMSvcSetU32(&parms[0], 0);
    HGCMSvcSetU32(&parms[1], 0);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT, 2, parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_IPE_UNINITIALIZED_STATUS);  /* This should get updated only when the guest call completes. */
    testMsgAddReadData(&g_Client, VBOX_SHCL_FMT_UNICODETEXT);
    RTTESTI_CHECK(parms[0].u.uint32 == VBOX_SHCL_HOST_MSG_READ_DATA);
    RTTESTI_CHECK(parms[1].u.uint32 == VBOX_SHCL_FMT_UNICODETEXT);
    RTTESTI_CHECK_RC_OK(call.rc);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT, 2, parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_IPE_UNINITIALIZED_STATUS);  /* This call should not complete yet. */
    table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);

    RTTestISub("Testing one format, no waiting guest calls.");
    RT_ZERO(g_Client);
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));
    testMsgAddReadData(&g_Client, VBOX_SHCL_FMT_HTML);
    HGCMSvcSetU32(&parms[0], 0);
    HGCMSvcSetU32(&parms[1], 0);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT, 2, parms, 0);
    RTTESTI_CHECK(parms[0].u.uint32 == VBOX_SHCL_HOST_MSG_READ_DATA);
    RTTESTI_CHECK(parms[1].u.uint32 == VBOX_SHCL_FMT_HTML);
    RTTESTI_CHECK_RC_OK(call.rc);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT, 2, parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_IPE_UNINITIALIZED_STATUS);  /* This call should not complete yet. */
    table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);

    RTTestISub("Testing two formats, waiting guest call.");
    RT_ZERO(g_Client);
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));
    HGCMSvcSetU32(&parms[0], 0);
    HGCMSvcSetU32(&parms[1], 0);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT, 2, parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_IPE_UNINITIALIZED_STATUS);  /* This should get updated only when the guest call completes. */
    testMsgAddReadData(&g_Client, VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_HTML);
    RTTESTI_CHECK(parms[0].u.uint32 == VBOX_SHCL_HOST_MSG_READ_DATA);
    RTTESTI_CHECK(parms[1].u.uint32 == VBOX_SHCL_FMT_UNICODETEXT);
    RTTESTI_CHECK_RC_OK(call.rc);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT, 2, parms, 0);
    RTTESTI_CHECK(parms[0].u.uint32 == VBOX_SHCL_HOST_MSG_READ_DATA);
    RTTESTI_CHECK(parms[1].u.uint32 == VBOX_SHCL_FMT_HTML);
    RTTESTI_CHECK_RC_OK(call.rc);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT, 2, parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_IPE_UNINITIALIZED_STATUS);  /* This call should not complete yet. */
    table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);

    RTTestISub("Testing two formats, no waiting guest calls.");
    RT_ZERO(g_Client);
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));
    testMsgAddReadData(&g_Client, VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_HTML);
    HGCMSvcSetU32(&parms[0], 0);
    HGCMSvcSetU32(&parms[1], 0);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT, 2, parms, 0);
    RTTESTI_CHECK(parms[0].u.uint32 == VBOX_SHCL_HOST_MSG_READ_DATA);
    RTTESTI_CHECK(parms[1].u.uint32 == VBOX_SHCL_FMT_UNICODETEXT);
    RTTESTI_CHECK_RC_OK(call.rc);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT, 2, parms, 0);
    RTTESTI_CHECK(parms[0].u.uint32 == VBOX_SHCL_HOST_MSG_READ_DATA);
    RTTESTI_CHECK(parms[1].u.uint32 == VBOX_SHCL_FMT_HTML);
    RTTESTI_CHECK_RC_OK(call.rc);
    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client, VBOX_SHCL_GUEST_FN_MSG_OLD_GET_WAIT, 2, parms, 0);
    RTTESTI_CHECK_RC(call.rc, VERR_IPE_UNINITIALIZED_STATUS);  /* This call should not complete yet. */
    table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);
    table.pfnUnload(NULL);
}

/**
 * Tests late guest data replies for an expired host wait event.
 *
 * The test creates a context-ID capable client, queues an asynchronous
 * read-data request, drains the resulting READ_DATA_CID host message, and then
 * releases the associated event to simulate the host-side read timing out. It
 * finally calls ShClSvcGuestDataSignal() with the stale context ID, as a guest
 * would do when its DATA_WRITE reply arrives after the host has stopped waiting.
 *
 * This is needed because guest response timing is guest-controlled and cannot
 * be trusted. The expected behavior is to drop the
 * stale payload and report success to the service dispatcher, because there is
 * no remaining host waiter that can consume the data.
 */
static void testGuestDataSignalExpiredEvent(void)
{
    struct VBOXHGCMSVCPARM parms[1];
    struct VBOXHGCMSVCPARM aMsgParms[2];
    VBOXHGCMSVCFNTABLE table;
    VBOXHGCMCALLHANDLE_TYPEDEF call;
    int rc;

    RTTestISub("Testing late guest data for expired event");
    rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    HGCMSvcSetU32(&parms[0], VBOX_SHCL_MODE_BIDIRECTIONAL);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);

    RT_ZERO(g_Client);
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));
    g_Client.State.fGuestFeatures0 |= VBOX_SHCL_GF_0_CONTEXT_ID;

    PSHCLEVENT pEvent = NULL;
    rc = ShClSvcReadDataFromGuestAsync(&g_Client, VBOX_SHCL_FMT_UNICODETEXT, &pEvent);
    RTTESTI_CHECK_RC_OK(rc);
    RTTESTI_CHECK_RETV(pEvent != NULL);

    uint64_t const uContextID = VBOX_SHCL_CONTEXTID_MAKE(g_Client.State.uSessionID,
                                                         g_Client.EventSrc.uID, pEvent->idEvent);

    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    HGCMSvcSetU64(&aMsgParms[0], VBOX_SHCL_HOST_MSG_READ_DATA_CID);
    HGCMSvcSetU32(&aMsgParms[1], 0);
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client,
                  VBOX_SHCL_GUEST_FN_MSG_GET, 2, aMsgParms, 0);
    RTTESTI_CHECK_RC_OK(call.rc);
    RTTESTI_CHECK(aMsgParms[0].u.uint64 == uContextID);
    RTTESTI_CHECK(aMsgParms[1].u.uint32 == VBOX_SHCL_FMT_UNICODETEXT);

    RTTESTI_CHECK(ShClEventRelease(pEvent) == 0);

    SHCLCLIENTCMDCTX cmdCtx;
    RT_ZERO(cmdCtx);
    cmdCtx.uContextID = uContextID;

    char szData[] = "late data";
    rc = ShClSvcGuestDataSignal(&g_Client, &cmdCtx, VBOX_SHCL_FMT_UNICODETEXT, szData, sizeof(szData));
    RTTESTI_CHECK_RC_OK(rc);

    rc = table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    rc = table.pfnUnload(NULL);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}

/**
 * Tests guest data reply validation for still pending host wait events.
 */
static void testGuestDataSignalRejectsMismatches(void)
{
    struct VBOXHGCMSVCPARM parms[1];
    struct VBOXHGCMSVCPARM aMsgParms[2];
    VBOXHGCMSVCFNTABLE table;
    VBOXHGCMCALLHANDLE_TYPEDEF call;
    int rc;

    RTTestISub("Testing guest data context validation");
    rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    HGCMSvcSetU32(&parms[0], VBOX_SHCL_MODE_BIDIRECTIONAL);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);

    RT_ZERO(g_Client);
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));
    g_Client.State.fGuestFeatures0 |= VBOX_SHCL_GF_0_CONTEXT_ID;

    PSHCLEVENT pEvent = NULL;
    rc = ShClSvcReadDataFromGuestAsync(&g_Client, VBOX_SHCL_FMT_UNICODETEXT, &pEvent);
    RTTESTI_CHECK_RC_OK(rc);
    RTTESTI_CHECK_RETV(pEvent != NULL);

    uint64_t const uContextID = VBOX_SHCL_CONTEXTID_MAKE(g_Client.State.uSessionID,
                                                         g_Client.EventSrc.uID, pEvent->idEvent);

    call.rc = VERR_IPE_UNINITIALIZED_STATUS;
    HGCMSvcSetU64(&aMsgParms[0], VBOX_SHCL_HOST_MSG_READ_DATA_CID);
    HGCMSvcSetU32(&aMsgParms[1], 0);
    table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client,
                  VBOX_SHCL_GUEST_FN_MSG_GET, 2, aMsgParms, 0);
    RTTESTI_CHECK_RC_OK(call.rc);
    RTTESTI_CHECK(aMsgParms[0].u.uint64 == uContextID);
    RTTESTI_CHECK(aMsgParms[1].u.uint32 == VBOX_SHCL_FMT_UNICODETEXT);

    SHCLCLIENTCMDCTX cmdCtx;
    RT_ZERO(cmdCtx);
    char szData[] = "pending data";

    cmdCtx.uContextID = VBOX_SHCL_CONTEXTID_MAKE(g_Client.State.uSessionID,
                                                 g_Client.EventSrc.uID, 0);
    rc = ShClSvcGuestDataSignal(&g_Client, &cmdCtx, VBOX_SHCL_FMT_UNICODETEXT, szData, sizeof(szData));
    RTTESTI_CHECK_RC(rc, VERR_WRONG_ORDER);

    cmdCtx.uContextID = VBOX_SHCL_CONTEXTID_MAKE(g_Client.State.uSessionID + 1,
                                                 g_Client.EventSrc.uID, pEvent->idEvent);
    rc = ShClSvcGuestDataSignal(&g_Client, &cmdCtx, VBOX_SHCL_FMT_UNICODETEXT, szData, sizeof(szData));
    RTTESTI_CHECK_RC(rc, VERR_INVALID_CONTEXT);

    cmdCtx.uContextID = VBOX_SHCL_CONTEXTID_MAKE(g_Client.State.uSessionID,
                                                 g_Client.EventSrc.uID + 1, pEvent->idEvent);
    rc = ShClSvcGuestDataSignal(&g_Client, &cmdCtx, VBOX_SHCL_FMT_UNICODETEXT, szData, sizeof(szData));
    RTTESTI_CHECK_RC(rc, VERR_INVALID_CONTEXT);

    cmdCtx.uContextID = uContextID;
    rc = ShClSvcGuestDataSignal(&g_Client, &cmdCtx, VBOX_SHCL_FMT_HTML, szData, sizeof(szData));
    RTTESTI_CHECK_RC(rc, VERR_INVALID_CONTEXT);

    rc = ShClSvcGuestDataSignal(&g_Client, &cmdCtx, VBOX_SHCL_FMT_UNICODETEXT, szData, sizeof(szData));
    RTTESTI_CHECK_RC_OK(rc);
    RTTESTI_CHECK(ShClEventRelease(pEvent) == 0);

    PSHCLEVENT pMultiEvent = NULL;
    rc = ShClSvcReadDataFromGuestAsync(&g_Client, VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_HTML, &pMultiEvent);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    RTTESTI_CHECK(pMultiEvent == NULL);

    rc = table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    rc = table.pfnUnload(NULL);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}

/**
 * Tests guest DATA_WRITE format validation before forwarding data to the backend.
 */
static void testGuestDataWriteRejectsInvalidFormats(void)
{
    struct VBOXHGCMSVCPARM parms[1];
    struct VBOXHGCMSVCPARM aWriteParms[VBOX_SHCL_CPARMS_DATA_WRITE];
    static const SHCLFORMAT s_aInvalidFormats[] =
    {
        VBOX_SHCL_FMT_NONE,
        VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_HTML,
        VBOX_SHCL_FMT_VALID_MASK + 1
    };
    VBOXHGCMSVCFNTABLE table;
    VBOXHGCMCALLHANDLE_TYPEDEF call;
    char szData[] = "invalid format data";
    int rc;

    RTTestISub("Testing guest DATA_WRITE invalid format rejection");
    rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    HGCMSvcSetU32(&parms[0], VBOX_SHCL_MODE_BIDIRECTIONAL);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);

    RT_ZERO(g_Client);
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));
    g_Client.State.fGuestFeatures0 |= VBOX_SHCL_GF_0_CONTEXT_ID;

    for (size_t i = 0; i < RT_ELEMENTS(s_aInvalidFormats); i++)
    {
        RTTestISubF("Testing guest DATA_WRITE invalid format %#x", s_aInvalidFormats[i]);
        HGCMSvcSetU64(&aWriteParms[0], VBOX_SHCL_CONTEXTID_MAKE(g_Client.State.uSessionID,
                                                                 g_Client.EventSrc.uID, 1));
        HGCMSvcSetU32(&aWriteParms[1], s_aInvalidFormats[i]);
        aWriteParms[2].type = VBOX_HGCM_SVC_PARM_PTR;
        aWriteParms[2].u.pointer.addr = szData;
        aWriteParms[2].u.pointer.size = sizeof(szData);

        call.rc = VERR_IPE_UNINITIALIZED_STATUS;
        table.pfnCall(NULL, &call, 1 /* clientId */, &g_Client,
                      VBOX_SHCL_GUEST_FN_DATA_WRITE, RT_ELEMENTS(aWriteParms), aWriteParms, 0);
        RTTESTI_CHECK_RC(call.rc, VERR_INVALID_PARAMETER);
    }

    rc = table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    rc = table.pfnUnload(NULL);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}

/**
 * Tests VBOX_SHCL_HOST_FN_SET_HEADLESS host calls.
 *
 * This host function is deprecated but intentionally kept for older Guest
 * Additions and compatibility with the historic service protocol.
 */
static void testSetHeadless(void)
{
    struct VBOXHGCMSVCPARM parms[2];
    VBOXHGCMSVCFNTABLE table;
    bool fHeadless;
    int rc;

    RTTestISub("Testing HOST_FN_SET_HEADLESS");
    rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    RT_ZERO(g_Client);
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    /* Reset global variable which doesn't reset itself. */
    HGCMSvcSetU32(&parms[0], false);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS,
                           1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    fHeadless = ShClSvcGetHeadless();
    RTTESTI_CHECK_MSG(fHeadless == false, ("fHeadless=%RTbool\n", fHeadless));
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS,
                           0, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS,
                           2, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    HGCMSvcSetU64(&parms[0], 99);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS,
                           1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);
    HGCMSvcSetU32(&parms[0], true);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS,
                           1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    fHeadless = ShClSvcGetHeadless();
    RTTESTI_CHECK_MSG(fHeadless == true, ("fHeadless=%RTbool\n", fHeadless));
    HGCMSvcSetU32(&parms[0], 99);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS,
                           1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    fHeadless = ShClSvcGetHeadless();
    RTTESTI_CHECK_MSG(fHeadless == true, ("fHeadless=%RTbool\n", fHeadless));
    rc = table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    rc = table.pfnUnload(NULL);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}

static void testHeadlessBackendConnect(void)
{
    struct VBOXHGCMSVCPARM parms[1];
    VBOXHGCMSVCFNTABLE table;
    int rc;

    RTTestISub("Testing HOST_FN_SET_HEADLESS backend connect propagation");
    rc = setupTable(&table);
    RTTESTI_CHECK_MSG_RETV(RT_SUCCESS(rc), ("rc=%Rrc\n", rc));

    HGCMSvcSetU32(&parms[0], false);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    RT_ZERO(g_Client);
    g_fBackendConnectCalled = false;
    g_fBackendConnectHeadless = true;
    rc = table.pfnConnect(NULL, 1 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_RC_OK(rc);
    RTTESTI_CHECK(g_fBackendConnectCalled);
    RTTESTI_CHECK_MSG(g_fBackendConnectHeadless == false,
                      ("g_fBackendConnectHeadless=%RTbool\n", g_fBackendConnectHeadless));
    rc = table.pfnDisconnect(NULL, 1 /* clientId */, &g_Client);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    HGCMSvcSetU32(&parms[0], true);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    RT_ZERO(g_Client);
    g_fBackendConnectCalled = false;
    g_fBackendConnectHeadless = false;
    rc = table.pfnConnect(NULL, 2 /* clientId */, &g_Client, 0, 0);
    RTTESTI_CHECK_RC_OK(rc);
    RTTESTI_CHECK(g_fBackendConnectCalled);
    RTTESTI_CHECK_MSG(g_fBackendConnectHeadless == true,
                      ("g_fBackendConnectHeadless=%RTbool\n", g_fBackendConnectHeadless));
    rc = table.pfnDisconnect(NULL, 2 /* clientId */, &g_Client);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    HGCMSvcSetU32(&parms[0], false);
    rc = table.pfnHostCall(NULL, VBOX_SHCL_HOST_FN_SET_HEADLESS, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);

    rc = table.pfnUnload(NULL);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}

static void testHostCall(void)
{
    testSetMode();
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    testSetTransferMode();
    testTransferFormatFiltering();
    testTransferGuestFeatures();
    testTransferHostCancelError();
    testHostDataReadBufferSizing();
    testHostDataReadValidation();
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
    testSetHeadless();
    testHeadlessBackendConnect();
    testGuestDataSignalExpiredEvent();
    testGuestDataSignalRejectsMismatches();
    testGuestDataWriteRejectsInvalidFormats();
}

int main(int argc, char *argv[])
{
    /*
     * Init the runtime, test and say hello.
     */
    const char *pcszExecName;
    NOREF(argc);
    pcszExecName = strrchr(argv[0], '/');
    pcszExecName = pcszExecName ? pcszExecName + 1 : argv[0];
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate(pcszExecName, &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

    /* Don't let assertions in the host service panic (core dump) the test cases. */
    RTAssertSetMayPanic(false);

    /*
     * Run the tests.
     */
    testHostCall();
    testGetHostMsgOld();

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
int ShClBackendTransferHandleStatusReply(PSHCLBACKEND, PSHCLCLIENT, PSHCLTRANSFER, SHCLSOURCE, SHCLTRANSFERSTATUS, int) { return VINF_SUCCESS; }
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
