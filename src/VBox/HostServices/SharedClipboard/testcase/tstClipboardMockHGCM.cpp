/* $Id: tstClipboardMockHGCM.cpp 114971 2026-08-10 17:29:14Z andreas.loeffler@oracle.com $ */
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/HostServices/VBoxClipboardExt.h>
#include <VBox/HostServices/VBoxSharedClipboardSvc.h>
#include <VBox/VBoxGuestLib.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
# include "VBoxSharedClipboardSvc-transfers.h"
#endif
#if defined (RT_OS_LINUX) || defined (RT_OS_SOLARIS)
# include <VBox/GuestHost/SharedClipboard-x11.h>
# include <VBox/GuestHost/DisplayServerType.h>
# include <X11/Xlib.h>
# include <X11/Xatom.h>
#endif
#ifdef RT_OS_WINDOWS
# include <VBox/GuestHost/SharedClipboard-win.h>
#endif

#include <VBox/HostServices/TstHGCMMock.h>
#include <VBox/HostServices/TstHGCMMockUtils.h>

#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/utf16.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST     g_hTest;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
DECLCALLBACK(int) tstHgcmMockSvcDispatcher(void *pvExtension, uint32_t u32Function,
                                           void *pvParms, uint32_t cbParms);

/** Test dispatcher which keeps protocol-only transfer tests independent of a display server. */
static DECLCALLBACK(int) tstClipboardTransferStatusContextDispatcher(void *pvExtension, uint32_t u32Function,
                                                                      void *pvParms, uint32_t cbParms)
{
    if (   u32Function == VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT
        || u32Function == VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT
        || u32Function == VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT
        || u32Function == VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC)
        return VINF_SUCCESS;
    return tstHgcmMockSvcDispatcher(pvExtension, u32Function, pvParms, cbParms);
}
#endif


/*********************************************************************************************************************************
*   Shared Clipboard testing                                                                                                     *
*********************************************************************************************************************************/
struct CLIPBOARDTESTDESC;
/** Pointer to a test description. */
typedef CLIPBOARDTESTDESC *PTESTDESC;

struct CLIPBOARDTESTCTX;
/** Pointer to a test context. */
typedef CLIPBOARDTESTCTX *PCLIPBOARDTESTCTX;

typedef DECLCALLBACKTYPE(int, FNTESTSETUP,(PCLIPBOARDTESTCTX pTstCtx, void **ppvCtx));
/** Pointer to a test setup callback. */
typedef FNTESTSETUP *PFNTESTSETUP;

typedef DECLCALLBACKTYPE(int, FNTESTEXEC,(PCLIPBOARDTESTCTX pTstCtx, void *pvCtx));
/** Pointer to a test exec callback. */
typedef FNTESTEXEC *PFNTESTEXEC;

typedef DECLCALLBACKTYPE(int, FNTESTDESTROY,(PCLIPBOARDTESTCTX pTstCtx, void *pvCtx));
/** Pointer to a test destroy callback. */
typedef FNTESTDESTROY *PFNTESTDESTROY;

#if defined (RT_OS_LINUX) || defined (RT_OS_SOLARIS)
typedef struct CLIPBOARDTESTTASKX11
{
    /** Thread handle for the X11 "populate clipboard" thread. */
    RTTHREAD            hThread;
    /** Shutdown indicator flag. */
    volatile bool       fShutdown;
} CLIPBOARDTESTTASKX11;
#endif

/**
 * Structure for keeping a clipboard test task.
 */
typedef struct CLIPBOARDTESTTASK
{
    SHCLFORMATS enmFmtHst;
    SHCLFORMATS enmFmtGst;
    /** For testing chunked reads / writes. */
    size_t      cbChunk;
    /** Data buffer to read / write for this task.
     *  Can be NULL if not needed. */
    void       *pvData;
    /** Size (in bytes) of \a pvData. */
    size_t      cbData;
    /** Number of bytes read / written from / to \a pvData. */
    size_t      cbProcessed;
#if defined (RT_OS_LINUX) || defined (RT_OS_SOLARIS)
    /** X11-specific data */
    CLIPBOARDTESTTASKX11 X11;
#endif
} CLIPBOARDTESTTASK;
typedef CLIPBOARDTESTTASK *PCLIPBOARDTESTTASK;

/**
 * Structure for keeping a clipboard test context.
 */
typedef struct CLIPBOARDTESTCTX
{
    /** The HGCM Mock utils context. */
    TSTHGCMUTILSCTX   HGCM;
    /** Clipboard-specific task data. */
    CLIPBOARDTESTTASK Task;
    struct
    {
        /** The VbglR3 Shared Clipboard context to work on. */
        VBGLR3SHCLCMDCTX CmdCtx;
    } Guest;
} CLIPBOARDTESTCTX;

/** The one and only clipboard test context. One at a time. */
CLIPBOARDTESTCTX g_TstCtx;

/**
 * Structure for keeping a clipboard test description.
 */
typedef struct CLIPBOARDTESTDESC
{
    /** The setup callback. */
    PFNTESTSETUP         pfnSetup;
    /** The exec callback. */
    PFNTESTEXEC          pfnExec;
    /** The destruction callback. */
    PFNTESTDESTROY       pfnDestroy;
} CLIPBOARDTESTDESC;

typedef struct SHCLCONTEXT
{
} SHCLCONTEXT;


static int tstSetModeRc(PTSTHGCMMOCKSVC pSvc, uint32_t uMode, int rcExpected)
{
    VBOXHGCMSVCPARM aParms[2];
    HGCMSvcSetU32(&aParms[0], uMode);
    int rc2 = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, aParms);
    RTTESTI_CHECK_MSG_RET(rcExpected == rc2, ("Expected %Rrc, got %Rrc\n", rcExpected, rc2), rc2);
    if (RT_SUCCESS(rcExpected))
    {
        uint32_t const uModeRet = ShClSvcGetMode();
        RTTESTI_CHECK_MSG_RET(uMode == uModeRet, ("Expected mode %RU32, got %RU32\n", uMode, uModeRet), VERR_WRONG_TYPE);
    }
    return rc2;
}

static int tstClipboardSetMode(PTSTHGCMMOCKSVC pSvc, uint32_t uMode)
{
    return tstSetModeRc(pSvc, uMode, VINF_SUCCESS);
}

static bool tstClipboardGetMode(PTSTHGCMMOCKSVC pSvc, uint32_t uModeExpected)
{
    RT_NOREF(pSvc);
    RTTESTI_CHECK_RET(ShClSvcGetMode() == uModeExpected, false);
    return true;
}

static void tstOperationModes(void)
{
    struct VBOXHGCMSVCPARM parms[2];
    uint32_t u32Mode;
    int rc;

    RTTestISub("Testing VBOX_SHCL_HOST_FN_SET_MODE");

    PTSTHGCMMOCKSVC pSvc = TstHgcmMockSvcInst();

    /* Reset global variable which doesn't reset itself. */
    HGCMSvcSetU32(&parms[0], VBOX_SHCL_MODE_OFF);
    rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC_OK(rc);
    u32Mode = ShClSvcGetMode();
    RTTESTI_CHECK_MSG(u32Mode == VBOX_SHCL_MODE_OFF, ("u32Mode=%u\n", (unsigned) u32Mode));

    rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_MODE, 0, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_MODE, 2, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    HGCMSvcSetU64(&parms[0], 99);
    rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    tstClipboardSetMode(pSvc, VBOX_SHCL_MODE_HOST_TO_GUEST);
    tstSetModeRc(pSvc, 99, VERR_NOT_SUPPORTED);
    tstClipboardGetMode(pSvc, VBOX_SHCL_MODE_OFF);
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
static void testSetTransferMode(void)
{
    RTTestISub("Testing VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE");

    PTSTHGCMMOCKSVC pSvc = TstHgcmMockSvcInst();

    /* Invalid parameter. */
    VBOXHGCMSVCPARM parms[2];
    HGCMSvcSetU64(&parms[0], 99);
    int rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_PARAMETER);

    /* Invalid mode. */
    HGCMSvcSetU32(&parms[0], 99);
    rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VERR_INVALID_FLAGS);

    /* Enable transfers. */
    HGCMSvcSetU32(&parms[0], VBOX_SHCL_TRANSFER_MODE_F_ENABLED);
    rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    /* Disable transfers again. */
    HGCMSvcSetU32(&parms[0], VBOX_SHCL_TRANSFER_MODE_F_NONE);
    rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, parms);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
}

/**
 * Verifies that transfer status replies use the transfer ID of the supplied
 * transfer rather than a stale transfer ID in the command context.  Also
 * verifies that callers without a local transfer can use the command context
 * unchanged.
 */
static void testTransferStatusContextRouting(void)
{
    RTTestISub("Testing transfer status context routing");

    PTSTHGCMMOCKSVC const pSvc = TstHgcmMockSvcInst();
    SHCLTRANSFERID const idTargetTransfer  = 42;
    SHCLTRANSFERID const idAmbientTransfer = 43;

    VBGLR3SHCLCMDCTX CmdCtx;
    RT_ZERO(CmdCtx);
    SHCLTRANSFERCTX GuestTransferCtx;
    RT_ZERO(GuestTransferCtx);
    SHCLTRANSFERCTX StaleGuestTransferCtx;
    RT_ZERO(StaleGuestTransferCtx);

    HGCMCLIENTID const idNextClient       = pSvc->uNextClientId;
    bool          fConnected              = false;
    bool          fGuestCtxInit           = false;
    bool          fGuestRegistered        = false;
    bool          fStaleGuestCtxInit      = false;
    bool          fStaleGuestRegistered   = false;
    bool          fTestDispatcher         = false;
    PSHCLCLIENT   pClient                 = NULL;
    PSHCLTRANSFER pGuestTransfer          = NULL;
    PSHCLTRANSFER pStaleGuestTransfer     = NULL;
    PSHCLEVENT    pTargetEvent            = NULL;
    PSHCLEVENT    pAmbientEvent           = NULL;
    int           rc                      = VINF_SUCCESS;

    do
    {
        VBOXHGCMSVCPARM Parm;
        rc = pSvc->fnTable.pfnRegisterExtension(pSvc->fnTable.pvService,
                                                tstClipboardTransferStatusContextDispatcher, NULL);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        fTestDispatcher = true;

        rc = tstClipboardSetMode(pSvc, VBOX_SHCL_MODE_BIDIRECTIONAL);
        if (RT_FAILURE(rc))
            break;

        HGCMSvcSetU32(&Parm, VBOX_SHCL_TRANSFER_MODE_F_ENABLED);
        rc = TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, &Parm);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;

        rc = VbglR3ClipboardConnectEx(&CmdCtx, VBOX_SHCL_GF_0_CONTEXT_ID);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        fConnected = true;

        RTTESTI_CHECK_MSG_BREAK(CmdCtx.idClient < RT_ELEMENTS(pSvc->aHgcmClient),
                                ("Client ID %RU32 is out of range\n", CmdCtx.idClient));
        PTSTHGCMMOCKCLIENT const pMockClient = &pSvc->aHgcmClient[CmdCtx.idClient];
        RTTESTI_CHECK_MSG_BREAK(TstHgcmMockSvcWaitForConnect(pSvc) == pMockClient,
                                ("Unexpected mock client connected for ID %RU32\n", CmdCtx.idClient));
        pClient = (PSHCLCLIENT)pMockClient->pvClient;
        RTTESTI_CHECK_MSG_BREAK(pClient != NULL, ("Missing service client for ID %RU32\n", CmdCtx.idClient));

        PSHCLTRANSFER pHostTarget = NULL;
        rc = ShClTransferCreate(SHCLTRANSFERDIR_FROM_REMOTE, SHCLSOURCE_REMOTE, NULL, &pHostTarget);
        if (RT_SUCCESS(rc))
            rc = ShClTransferCtxRegisterById(&pClient->Transfers.Ctx, pHostTarget, idTargetTransfer);
        if (RT_FAILURE(rc))
            ShClTransferDestroy(pHostTarget);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;

        rc = ShClTransferInit(pHostTarget);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;

        PSHCLTRANSFER pHostAmbient = NULL;
        rc = ShClTransferCreate(SHCLTRANSFERDIR_FROM_REMOTE, SHCLSOURCE_REMOTE, NULL, &pHostAmbient);
        if (RT_SUCCESS(rc))
            rc = ShClTransferCtxRegisterById(&pClient->Transfers.Ctx, pHostAmbient, idAmbientTransfer);
        if (RT_FAILURE(rc))
            ShClTransferDestroy(pHostAmbient);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;

        rc = ShClTransferInit(pHostAmbient);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;

        rc = ShClTransferCtxInit(&GuestTransferCtx);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        fGuestCtxInit = true;

        rc = ShClTransferCtxBeginSession(&GuestTransferCtx, pClient->State.uSessionID);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;

        rc = ShClTransferCreate(SHCLTRANSFERDIR_FROM_REMOTE, SHCLSOURCE_REMOTE, NULL, &pGuestTransfer);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;

        rc = ShClTransferCtxRegisterById(&GuestTransferCtx, pGuestTransfer, idTargetTransfer);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        fGuestRegistered = true;

        rc = ShClTransferCtxInit(&StaleGuestTransferCtx);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        fStaleGuestCtxInit = true;

        SHCLSESSIONID const idStaleSession = pClient->State.uSessionID != 1 ? 1 : 2;
        rc = ShClTransferCtxBeginSession(&StaleGuestTransferCtx, idStaleSession);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;

        rc = ShClTransferCreate(SHCLTRANSFERDIR_FROM_REMOTE, SHCLSOURCE_REMOTE, NULL, &pStaleGuestTransfer);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;

        rc = ShClTransferCtxRegisterById(&StaleGuestTransferCtx, pStaleGuestTransfer, idTargetTransfer);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        fStaleGuestRegistered = true;

        /* Both transfers deliberately have the same valid event ID. */
        pHostTarget->Events.idNextEvent = 1234;
        pHostAmbient->Events.idNextEvent = pHostTarget->Events.idNextEvent;
        rc = ShClEventSourceGenerateAndRegisterEvent(&pHostTarget->Events, &pTargetEvent);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        rc = ShClEventSourceGenerateAndRegisterEvent(&pHostAmbient->Events, &pAmbientEvent);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        RTTESTI_CHECK_MSG_BREAK(pTargetEvent->idEvent == pAmbientEvent->idEvent,
                                ("Expected matching event IDs, got %RU32 and %RU32\n",
                                 pTargetEvent->idEvent, pAmbientEvent->idEvent));

        /*
         * The command context deliberately refers to a different live
         * transfer.  The supplied transfer must select the target transfer.
         */
        CmdCtx.idContext = VBOX_SHCL_CONTEXTID_MAKE(pClient->State.uSessionID,
                                                    idAmbientTransfer, pAmbientEvent->idEvent);
        rc = VbglR3ClipboardTransferSendStatus(&CmdCtx, pGuestTransfer,
                                               SHCLTRANSFERSTATUS_INITIALIZED, VINF_SUCCESS);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;

        /* A mismatched ambient transfer also makes its event stale. */
        int const rcTargetEvent  = ShClEventWait(pTargetEvent, 0 /* msTimeout */, NULL /* ppPayload */);
        int const rcAmbientEvent = ShClEventWait(pAmbientEvent, 0 /* msTimeout */, NULL /* ppPayload */);
        RTTESTI_CHECK_RC(rcTargetEvent,  VERR_TIMEOUT);
        RTTESTI_CHECK_RC(rcAmbientEvent, VERR_TIMEOUT);
        if (rcTargetEvent != VERR_TIMEOUT || rcAmbientEvent != VERR_TIMEOUT)
            break;

        /* A stale transfer's session must not be replaced by the ambient session. */
        rc = VbglR3ClipboardTransferSendStatus(&CmdCtx, pStaleGuestTransfer,
                                               SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);
        RTTESTI_CHECK_RC(rc, VERR_INVALID_CONTEXT);
        if (rc != VERR_INVALID_CONTEXT)
            break;
        RTTESTI_CHECK(ShClTransferCtxGetTransferById(&pClient->Transfers.Ctx, idTargetTransfer) != NULL);
        RTTESTI_CHECK(ShClTransferCtxGetTransferById(&pClient->Transfers.Ctx, idAmbientTransfer) != NULL);
        RTTESTI_CHECK_RC(ShClEventWait(pTargetEvent, 0 /* msTimeout */, NULL /* ppPayload */), VERR_TIMEOUT);
        RTTESTI_CHECK_RC(ShClEventWait(pAmbientEvent, 0 /* msTimeout */, NULL /* ppPayload */), VERR_TIMEOUT);

        RTTESTI_CHECK(ShClEventRelease(pTargetEvent) == 0);
        pTargetEvent = NULL;
        RTTESTI_CHECK(ShClEventRelease(pAmbientEvent) == 0);
        pAmbientEvent = NULL;

        rc = VbglR3ClipboardTransferSendStatus(&CmdCtx, pGuestTransfer,
                                               SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;

        PSHCLTRANSFER const pTargetAfter
            = ShClTransferCtxGetTransferById(&pClient->Transfers.Ctx, idTargetTransfer);
        PSHCLTRANSFER const pAmbientAfter
            = ShClTransferCtxGetTransferById(&pClient->Transfers.Ctx, idAmbientTransfer);
        RTTESTI_CHECK_MSG(pTargetAfter == NULL,
                          ("Target transfer %RU16 was not canceled\n", idTargetTransfer));
        RTTESTI_CHECK_MSG(pAmbientAfter != NULL,
                          ("Ambient transfer %RU16 was canceled instead\n", idAmbientTransfer));
        if (pTargetAfter != NULL || pAmbientAfter == NULL)
            break;

        /* No local transfer is available on some error paths: keep the context unchanged. */
        rc = VbglR3ClipboardTransferSendStatus(&CmdCtx, NULL,
                                               SHCLTRANSFERSTATUS_CANCELED, VERR_CANCELLED);
        RTTESTI_CHECK_RC_OK(rc);
        if (RT_FAILURE(rc))
            break;
        RTTESTI_CHECK_MSG(ShClTransferCtxGetTransferById(&pClient->Transfers.Ctx, idAmbientTransfer) == NULL,
                          ("Context transfer %RU16 was not canceled\n", idAmbientTransfer));
    } while (0);

    if (pClient)
    {
        if (pTargetEvent)
        {
            RTTESTI_CHECK(ShClEventRelease(pTargetEvent) == 0);
            pTargetEvent = NULL;
        }
        if (pAmbientEvent)
        {
            RTTESTI_CHECK(ShClEventRelease(pAmbientEvent) == 0);
            pAmbientEvent = NULL;
        }
        PSHCLTRANSFER pTransfer = ShClTransferCtxGetTransferById(&pClient->Transfers.Ctx, idTargetTransfer);
        if (pTransfer)
            ShClSvcTransferDestroy(pClient, pTransfer);
        pTransfer = ShClTransferCtxGetTransferById(&pClient->Transfers.Ctx, idAmbientTransfer);
        if (pTransfer)
            ShClSvcTransferDestroy(pClient, pTransfer);
    }
    if (fGuestCtxInit)
    {
        if (!fGuestRegistered && pGuestTransfer)
            ShClTransferDestroy(pGuestTransfer);
        ShClTransferCtxDestroy(&GuestTransferCtx);
    }
    else if (pGuestTransfer)
        ShClTransferDestroy(pGuestTransfer);

    if (fStaleGuestCtxInit)
    {
        if (!fStaleGuestRegistered && pStaleGuestTransfer)
            ShClTransferDestroy(pStaleGuestTransfer);
        ShClTransferCtxDestroy(&StaleGuestTransferCtx);
    }
    else if (pStaleGuestTransfer)
        ShClTransferDestroy(pStaleGuestTransfer);

    if (fConnected)
    {
        int const rcDisconnect = VbglR3ClipboardDisconnectEx(&CmdCtx);
        RTTESTI_CHECK_RC_OK(rcDisconnect);
        if (RT_SUCCESS(rcDisconnect))
        {
            /* This mock has four monotonically allocated client slots.  Reclaim
               the fully disconnected slot so this test does not consume one. */
            RTTESTI_CHECK(pSvc->uNextClientId == idNextClient + 1);
            pSvc->uNextClientId = idNextClient;
        }
    }

    if (fTestDispatcher)
    {
        int const rcRestore = pSvc->fnTable.pfnRegisterExtension(pSvc->fnTable.pvService,
                                                                 tstHgcmMockSvcDispatcher, NULL);
        RTTESTI_CHECK_RC_OK(rcRestore);
    }

    VBOXHGCMSVCPARM Parm;
    HGCMSvcSetU32(&Parm, VBOX_SHCL_TRANSFER_MODE_F_NONE);
    RTTESTI_CHECK_RC_OK(TstHgcmMockSvcHostCall(pSvc, NULL, VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1, &Parm));
    RTTESTI_CHECK_RC_OK(tstClipboardSetMode(pSvc, VBOX_SHCL_MODE_OFF));
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
/** Verifies that the X11 backend fails cleanly when no display is available. */
static void testX11UnavailableBackend(void)
{
    RTTestISub("Testing X11 backend without a display");

    PTSTHGCMMOCKSVC const pSvc = TstHgcmMockSvcInst();
    void *pvClient = RTMemAllocZ(pSvc->fnTable.cbClient);
    RTTESTI_CHECK_MSG_RETV(pvClient, ("Failed to allocate a service client\n"));

    const char *pszDisplay = RTEnvGet("DISPLAY");
    char *pszDisplaySaved = pszDisplay ? RTStrDup(pszDisplay) : NULL;
    if (pszDisplay && !pszDisplaySaved)
    {
        RTTESTI_CHECK_MSG(false, ("Failed to save DISPLAY before the test\n"));
        RTMemFree(pvClient);
        return;
    }

    int rc = RTEnvUnset("DISPLAY");
    if (RT_FAILURE(rc))
    {
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        RTStrFree(pszDisplaySaved);
        RTMemFree(pvClient);
        return;
    }

    rc = pSvc->fnTable.pfnConnect(pSvc->fnTable.pvService, UINT32_C(100), pvClient,
                                  VMMDEV_REQUESTOR_USR_NOT_GIVEN /* fRequestor */, false /* fRestoring */);

    int const rcRestore = pszDisplaySaved ? RTEnvSet("DISPLAY", pszDisplaySaved) : VINF_SUCCESS;
    RTStrFree(pszDisplaySaved);
    RTTESTI_CHECK_RC(rcRestore, VINF_SUCCESS);

    RTTESTI_CHECK_RC(rc, VERR_NOT_SUPPORTED);
    if (RT_SUCCESS(rc))
        RTTESTI_CHECK_RC_OK(pSvc->fnTable.pfnDisconnect(pSvc->fnTable.pvService, UINT32_C(100), pvClient));
    RTMemFree(pvClient);
}
#endif

static void testGuestSimple(void)
{
    RTTestISub("Testing client (guest) API - Simple");

    PTSTHGCMMOCKSVC pSvc = TstHgcmMockSvcInst();

    /* Preparations. */
    VBGLR3SHCLCMDCTX Ctx;
    RT_ZERO(Ctx);

    /*
     * Multiple connects / disconnects.
     */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardConnectEx(&Ctx, VBOX_SHCL_GF_0_CONTEXT_ID));
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardDisconnectEx(&Ctx));
    /* Report bogus guest features while connecting. */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardConnectEx(&Ctx, 0xdeadbeef));
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardDisconnectEx(&Ctx));

    RTTESTI_CHECK_RC_OK(VbglR3ClipboardConnectEx(&Ctx, VBOX_SHCL_GF_0_CONTEXT_ID));

    /*
     * Feature tests.
     */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardReportFeatures(Ctx.idClient, 0x0,        NULL /* pfHostFeatures */));
    /* Report bogus features to the host. */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardReportFeatures(Ctx.idClient, 0xdeadb33f, NULL /* pfHostFeatures */));

    /*
     * Access denied tests.
     */
    tstClipboardSetMode(pSvc, VBOX_SHCL_MODE_OFF);

    /* Try reading data from host. */
    uint8_t abData[32]; uint32_t cbIgnored;
    RTTESTI_CHECK_RC(VbglR3ClipboardReadData(Ctx.idClient, VBOX_SHCL_FMT_UNICODETEXT,
                                             abData, sizeof(abData), &cbIgnored), VERR_ACCESS_DENIED);
    /* Try writing data without reporting formats before (legacy). */
    RTTESTI_CHECK_RC(VbglR3ClipboardWriteData(Ctx.idClient, 0xdeadb33f, abData, sizeof(abData)), VERR_ACCESS_DENIED);
    /* Try writing data without reporting formats before. */
    RTTESTI_CHECK_RC(VbglR3ClipboardWriteDataEx(&Ctx, 0xdeadb33f, abData, sizeof(abData)), VERR_ACCESS_DENIED);
    /* Report bogus formats to the host. */
    RTTESTI_CHECK_RC(VbglR3ClipboardReportFormats(Ctx.idClient, 0xdeadb33f), VERR_ACCESS_DENIED);
    /* Report supported formats to host. */
    RTTESTI_CHECK_RC(VbglR3ClipboardReportFormats(Ctx.idClient,
                                                  VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_BITMAP | VBOX_SHCL_FMT_HTML),
                                                  VERR_ACCESS_DENIED);
    /*
     * Access allowed tests.
     */
    tstClipboardSetMode(pSvc, VBOX_SHCL_MODE_BIDIRECTIONAL);

    /* Try reading data from host. */
    int rc = VbglR3ClipboardReadData(Ctx.idClient, VBOX_SHCL_FMT_UNICODETEXT,
                                     abData, sizeof(abData), &cbIgnored);
    /*
     * The VbglR3ClipboardConnectEx() call above reaches the X11
     * ShClBackendConnect() routine which calls ShClX11ThreadStart() to start the
     * "SHCLX11" thread, an Xt thread for handling the Shared Clipboard, which
     * sits in clipThreadMain() where it loops calling XtGetSelectionValue(3X11).
     * When the "SHCLX11" thread is starting up at connect time, clipThreadMain()
     * calls clipQueryX11Targets() which calls XtGetSelectionValue(3X11) to request
     * the supported targets of the clipboard selection and then asynchronously calls
     * the specified callback, clipQueryX11TargetsCallback(), with the results.
     * Meanwhile, clipThreadMain() signals its parent and ShClX11ThreadStart() then
     * returns as does ShClBackendConnect().  Attempting to read from the clipboard
     * here before clipQueryX11TargetsCallback() returns will fail when
     * ShClBackendReadData() -> ShClX11ReadDataFromX11() ->
     * shClX11ReadDataFromX11Internal() -> ShClX11ReadDataFromX11Async() ->
     * ShClX11ReadDataFromX11Worker() finds the clipboard busy (fXtBusy == true)
     * and returns VERR_TRY_AGAIN.
     */
    if (rc == VERR_TRY_AGAIN)
    {
        RTThreadSleep(RT_MS_1SEC);
        rc = VbglR3ClipboardReadData(Ctx.idClient, VBOX_SHCL_FMT_UNICODETEXT,
                                     abData, sizeof(abData), &cbIgnored);
    }
    RTTESTI_CHECK_RC_OK(rc);
    /* Report bogus formats to the host. */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardReportFormats(Ctx.idClient, 0xdeadb33f));
    /* Report supported formats to host. */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardReportFormats(Ctx.idClient,
                                                     VBOX_SHCL_FMT_UNICODETEXT | VBOX_SHCL_FMT_BITMAP | VBOX_SHCL_FMT_HTML));
    /* Tear down. */
    RTTESTI_CHECK_RC_OK(VbglR3ClipboardDisconnectEx(&Ctx));
}

static RTUTF16 tstGetRandUtf8(void)
{
    return RTRandU32Ex(0x20, 0x7A);
}

static char *tstGenerateUtf8StringA(uint32_t uCch)
{
    char * pszRand = (char *)RTMemAlloc(uCch + 1);
    for (uint32_t i = 0; i < uCch; i++)
        pszRand[i] = tstGetRandUtf8();
    pszRand[uCch] = 0;
    return pszRand;
}

#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
static RTUTF16 tstGetRandUtf16(void)
{
    RTUTF16 wc;
    do
    {
        wc = (RTUTF16)RTRandU32Ex(1, 0xfffd);
    } while (wc >= 0xd800 && wc <= 0xdfff);
    return wc;
}

static PRTUTF16 tstGenerateUtf16StringA(uint32_t uCch)
{
    PRTUTF16 pwszRand = (PRTUTF16)RTMemAlloc((uCch + 1) * sizeof(RTUTF16));
    for (uint32_t i = 0; i < uCch; i++)
        pwszRand[i] = tstGetRandUtf16();
    pwszRand[uCch] = 0;
    return pwszRand;
}
#endif /* RT_OS_WINDOWS) || RT_OS_OS2 */

static void testHostCall(void)
{
    tstOperationModes();
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    testSetTransferMode();
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
}


/*********************************************************************************************************************************
 * Test: Guest reading from host                                                                                                 *
 ********************************************************************************************************************************/
#if defined (RT_OS_LINUX) || defined (RT_OS_SOLARIS)
/* Called from SHCLX11 thread. */
static DECLCALLBACK(int) tstTestReadFromHost_ReportFormatsCallback(PSHCLCONTEXT pCtx, uint32_t fFormats, void *pvUser)
{
    RT_NOREF(pCtx, fFormats, pvUser);

    RTTestPrintf(g_hTest, RTTESTLVL_DEBUG, "tstTestReadFromHost_SvcReportFormatsCallback: fFormats=%#x\n", fFormats);
    return VINF_SUCCESS;
}

/* Called by the backend, e.g. for X11 in the SHCLX11 thread. */
static DECLCALLBACK(int) tstTestReadFromHost_OnClipboardReadCallback(PSHCLCONTEXT pCtx,
                                                                     SHCLFORMAT uFmt, void **ppv, size_t *pcb, void *pvUser)
{
    RT_NOREF(pCtx, uFmt, pvUser);

    PCLIPBOARDTESTTASK pTask = (PCLIPBOARDTESTTASK)TstHGCMUtilsTaskGetCurrent(&g_TstCtx.HGCM)->pvUser;

    void   *pvData = NULL;
    size_t  cbData = pTask->cbData - pTask->cbProcessed;
    if (cbData)
    {
        pvData = RTMemDup((uint8_t *)pTask->pvData + pTask->cbProcessed, cbData);
        AssertPtr(pvData);
    }

    RTTestPrintf(g_hTest, RTTESTLVL_DEBUG, "Host reporting back %RU32 bytes of data\n", cbData);

    *ppv = pvData;
    *pcb = cbData;

    return VINF_SUCCESS;
}
#endif /* (RT_OS_LINUX) || defined (RT_OS_SOLARIS) */

typedef struct TSTUSERMOCK
{
#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
    SHCLX11CTX   X11Ctx;
#endif
    PSHCLCONTEXT pCtx;
} TSTUSERMOCK;
typedef TSTUSERMOCK *PTSTUSERMOCK;

static void tstTestReadFromHost_MockInit(PTSTUSERMOCK pUsrMock, const char *pszName)
{
#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
    SHCLCALLBACKS Callbacks;
    RT_ZERO(Callbacks);
    Callbacks.pfnReportFormats   = tstTestReadFromHost_ReportFormatsCallback;
    Callbacks.pfnOnClipboardRead = tstTestReadFromHost_OnClipboardReadCallback;

    pUsrMock->pCtx = (PSHCLCONTEXT)RTMemAllocZ(sizeof(SHCLCONTEXT));
    AssertPtrReturnVoid(pUsrMock->pCtx);

    ShClX11Init(&pUsrMock->X11Ctx, &Callbacks, pUsrMock->pCtx);
    ShClX11ThreadStartEx(&pUsrMock->X11Ctx, pszName, false /* fGrab */);
    /* Give the clipboard time to synchronise. */
    RTThreadSleep(500);
#else
    RT_NOREF(pUsrMock, pszName);
#endif /* RT_OS_LINUX */
}

static void tstTestReadFromHost_MockDestroy(PTSTUSERMOCK pUsrMock)
{
#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
    ShClX11ThreadStop(&pUsrMock->X11Ctx);
    ShClX11Term(&pUsrMock->X11Ctx);
    RTMemFree(pUsrMock->pCtx);
#else
    RT_NOREF(pUsrMock);
#endif
}

static int tstTestReadFromHost_DoIt(PCLIPBOARDTESTCTX pCtx, PCLIPBOARDTESTTASK pTask)
{
    size_t   cbDst       = RT_MAX(_64K, pTask->cbData);
    uint8_t *pabDst      = (uint8_t *)RTMemAllocZ(cbDst);
    AssertPtrReturn(pabDst, VERR_NO_MEMORY);

    AssertPtr(pTask->pvData);                /* Racing condition with host thread? */
    Assert(pTask->cbChunk);                  /* Buggy test? */
    Assert(pTask->cbChunk <= pTask->cbData); /* Ditto. */

    size_t   cbToRead = pTask->cbData;
    switch (pTask->enmFmtGst)
    {
        case VBOX_SHCL_FMT_UNICODETEXT:
#ifndef RT_OS_WINDOWS /** @todo Not sure about OS/2. */
            cbToRead *= sizeof(RTUTF16);
#endif
            break;

        default:
            break;
    }

    PVBGLR3SHCLCMDCTX pCmdCtx = &pCtx->Guest.CmdCtx;

    /* Do random chunked reads. */
    uint32_t const cChunkedReads = RTRandU32Ex(1, 16);
    RTTestPrintf(g_hTest, RTTESTLVL_DEBUG, "%RU32 chunked reads\n", cChunkedReads);
    for (uint32_t i = 0; i < cChunkedReads; i++)
    {
        /* Note! VbglR3ClipboardReadData() currently does not support chunked reads!
          *      It in turn returns VINF_BUFFER_OVERFLOW when the supplied buffer was too small. */

        uint32_t cbChunk    = RTRandU32Ex(1, (uint32_t)(pTask->cbData / cChunkedReads));
        uint32_t cbRead     = 0;
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Guest trying to read %RU32 bytes\n", cbChunk);
        int vrc2 = VbglR3ClipboardReadData(pCmdCtx->idClient, pTask->enmFmtGst, pabDst, cbChunk, &cbRead);
        if (   vrc2   == VINF_SUCCESS
            && cbRead == 0) /* No data there yet? */
        {
            RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "No data (yet) from host\n");
            RTThreadSleep(10);
            continue;
        }
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Trying to read host clipboard data with a %RU32 byte buffer -> %Rrc (%RU32)\n", cbChunk, vrc2, cbRead);
        RTTEST_CHECK_MSG(g_hTest, vrc2 == VINF_BUFFER_OVERFLOW, (g_hTest, "Got %Rrc, expected VINF_BUFFER_OVERFLOW\n", vrc2));
    }

    /* Last read: Read the data with a buffer big enough. This must succeed. */
    RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "Reading full data (%zu)\n", pTask->cbData);
    uint32_t cbRead = 0;
    int vrc2 = VbglR3ClipboardReadData(pCmdCtx->idClient, pTask->enmFmtGst, pabDst, (uint32_t)cbDst, &cbRead);
    RTTEST_CHECK_MSG(g_hTest, vrc2 == VINF_SUCCESS, (g_hTest, "Got %Rrc, expected VINF_SUCCESS\n", vrc2));
    RTTEST_CHECK_MSG(g_hTest, cbRead == cbToRead, (g_hTest, "Read %RU32 bytes, expected %zu\n", cbRead, cbToRead));

    if (pTask->enmFmtGst == VBOX_SHCL_FMT_UNICODETEXT)
        RTTEST_CHECK_MSG(g_hTest, RTUtf16ValidateEncoding((PRTUTF16)pabDst) == VINF_SUCCESS, (g_hTest, "Read data is not valid UTF-16\n"));
    if (cbRead == cbToRead)
    {
#ifndef RT_OS_WINDOWS /** @todo Not sure about OS/2. */
        PRTUTF16 pwszSrc = NULL;
        RTTEST_CHECK(g_hTest, RT_SUCCESS(RTStrToUtf16((const char *)pTask->pvData, &pwszSrc)));
        RTTEST_CHECK_MSG(g_hTest, memcmp(pwszSrc, pabDst, cbRead) == 0, (g_hTest, "Read data does not match host data\n"));
        RTUtf16Free(pwszSrc);
#else
        RTTEST_CHECK_MSG(g_hTest, memcmp(pTask->pvData, pabDst, cbRead) == 0, (g_hTest, "Read data does not match host data\n"));
#endif
    }

    RTTestPrintf(g_hTest, RTTESTLVL_DEBUG, "Read data from host:\n%.*Rhxd\n", cbRead, pabDst);

    RTMemFree(pabDst);

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) tstTestReadFromHost_ThreadGuest(PTSTHGCMUTILSCTX pCtx, void *pvCtx)
{
    RTThreadSleep(1000); /* Fudge; wait until the host has prepared the data for the clipboard. */

    PCLIPBOARDTESTCTX  pTstCtx  = (PCLIPBOARDTESTCTX)pvCtx;
    AssertPtr(pTstCtx);

    RT_ZERO(pTstCtx->Guest.CmdCtx);
    RTTEST_CHECK_RC_OK(g_hTest, VbglR3ClipboardConnectEx(&pTstCtx->Guest.CmdCtx, VBOX_SHCL_GF_0_CONTEXT_ID));

    RTThreadSleep(1000); /* Fudge; wait until the host has prepared the data for the clipboard. */

    PCLIPBOARDTESTTASK pTstTask = (PCLIPBOARDTESTTASK)pCtx->Task.pvUser;
    AssertPtr(pTstTask);
    tstTestReadFromHost_DoIt(pTstCtx, pTstTask);

    RTTEST_CHECK_RC_OK(g_hTest, VbglR3ClipboardDisconnectEx(&pTstCtx->Guest.CmdCtx));

    /* Signal that the task ended. */
    TstHGCMUtilsTaskSignal(&pCtx->Task, VINF_SUCCESS);

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) tstTestReadFromHost_ClientConnectedCallback(PTSTHGCMUTILSCTX pCtx, PTSTHGCMMOCKCLIENT pClient,
                                                                     void *pvUser)
{
    RT_NOREF(pCtx, pClient);

    PCLIPBOARDTESTCTX pTstCtx = (PCLIPBOARDTESTCTX)pvUser;
    AssertPtr(pTstCtx); RT_NOREF(pTstCtx);

    RTTestPrintf(g_hTest, RTTESTLVL_DEBUG, "Client %RU32 connected\n", pClient->idClient);
    return VINF_SUCCESS;
}

#if defined (RT_OS_LINUX) || defined (RT_OS_SOLARIS)
/**
 * This is an X11 clipboard "copy" thread which takes ownership of the
 * clipboard, waits for requests of the clipboard selection contents from
 * X11 clients, and processes each request by copying the randomly generated
 * UTF8 data passed in to the clipboard selection before finally notifying
 * the requesting X client when complete.
 */
static DECLCALLBACK(int) tstSetClipboardContents(RTTHREAD hThread, void *pvUser)
{
    PCLIPBOARDTESTTASK pTask = (PCLIPBOARDTESTTASK)pvUser;

    Display *pX11Display = XOpenDisplay(NULL);
    if (!pX11Display)
    {
        RTTestPrintf(g_hTest, RTTESTLVL_FAILURE, "XOpenDisplay(3X11) failed\n");
        return VERR_NOT_AVAILABLE;
    }

    /* Create an unmapped subwindow which will own the "CLIPBOARD" selection and
     * will receive messages from X clients accessing the clipboard. */
    Window window = XCreateSimpleWindow(pX11Display, XDefaultRootWindow(pX11Display), 0, 0, 1, 1, 0, 0, 0);

    /* Populate the atom identifiers needed for our clipboard operations. */
    Atom clipboard   = XInternAtom(pX11Display, "CLIPBOARD", False);
    Atom targets     = XInternAtom(pX11Display, "TARGETS", False);
    Atom utf8_string = XInternAtom(pX11Display, "UTF8_STRING", False);

    /* In order to copy data to the clipboard we must first have our window
     * become the owner of the "CLIPBOARD" selection. */
    XSetSelectionOwner(pX11Display, clipboard, window, CurrentTime);
    if (XGetSelectionOwner(pX11Display, clipboard) != window)
    {
        RTTestPrintf(g_hTest, RTTESTLVL_FAILURE,
                     "XSetSelectionOwner(3X11): failed to become owner of clipboard selection\n");
        return VERR_ACCESS_DENIED;
    }

    /* Initial setup completed, notify our callee that we are now ready for action. */
    RTThreadUserSignal(hThread);

    while (!ASMAtomicReadBool(&pTask->X11.fShutdown))
    {
        /* Check the event queue to see if any events have been received from the X
         * server. */
        if (XPending(pX11Display) > 0)
        {
            /* Copy the first event from the event queue into the specified XEvent
             * structure and remove it from the queue. */
            XEvent event;
            XNextEvent(pX11Display, &event);

            /* X11 clients which want to paste the contents of the clipboard selection
             * call XConvertSelection(3X11) which the X server responds to by sending
             * a 'SelectionRequest' event to the X11 client which currently owns the
             * clipboard selection. This happens in our case via: shClSvcConnect() ->
             * ShClBackendConnect() -> ShClX11ThreadStart() -> ShClX11ThreadStartEx() ->
             * clipThreadMain() -> clipQueryX11Targets() -> XtGetSelectionValue().
             * XtGetSelectionValue() calls XConvertSelection() internally. */
            if (event.type == SelectionRequest)
            {
                /* The 'SelectionRequest' event contains details of the X11 client requestor
                 * such as their 'window' ('requestor'), the desired format of the clipboard
                 * contents ('target'), and which property on their window that they would
                 * like the clipboard contents copied to ('property'). We use this data to
                 * populate a 'SelectionNotify' event which we send back to the requestor. */
                XSelectionRequestEvent *pReq = &event.xselectionrequest;
                XSelectionEvent selectionNotifyEvent = { 0 };

                selectionNotifyEvent.type      = SelectionNotify;
                selectionNotifyEvent.display   = pReq->display;
                selectionNotifyEvent.requestor = pReq->requestor;
                selectionNotifyEvent.selection = pReq->selection;
                selectionNotifyEvent.target    = pReq->target;
                selectionNotifyEvent.property  = pReq->property;
                selectionNotifyEvent.time      = pReq->time;

                /* X11 clients typically send an initial 'SelectionRequest' event containing
                 * a 'target' containing the "TARGETS" atom to request a list of valid
                 * supported target atoms. After taking ownership of the "CLIPBOARD"
                 * selection other X11 clients may also send a 'SelectionRequest' event with
                 * a 'target' of the "TARGETS" atom. */
                if (selectionNotifyEvent.target == targets)
                {
                    Atom supported[] = { targets, utf8_string };
                    XChangeProperty(pX11Display, pReq->requestor, pReq->property, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)supported, RT_ELEMENTS(supported));
                }
                else if (selectionNotifyEvent.target == utf8_string)
                {
                    /* Update the property which the requestor chose to contain the clipboard
                     * selection contents on the requetor's window with the UTF8-formatted
                     * contents of the clipboard. */
                    XChangeProperty(pX11Display, pReq->requestor, pReq->property, pReq->target, 8,
                        PropModeReplace, (unsigned char *)pTask->pvData, pTask->cbData);
                }
                else
                {
                    /* We don't support the requested format. Note that if the size of the
                     * request to the X server exceeds the maximum request size described in the
                     * X Consortium's Inter-Client Communication Conventions Manual (ICCM)
                     * section 2.5 'Large Data Transfers' then the requestor will send a target
                     * of type 'INCR' meaning the data will be sent incrementally. The Xlib
                     * Programming Manual describes how the maximum request size can be
                     * calculated:
                     *   maxsize = XExtendedMaxRequestSize();
                     *   if (!maxsize) maxsize = XMaxRequestSize();
                     *   maxsize *= 4
                     * On Solaris 11.4 and various Linux distros the lower bound is 64K * 4 which
                     * is greater than the possible maximum size passed in here of 8K so there is
                     * no need to include support for the 'INCR' target here.
                     */
                    selectionNotifyEvent.property = None;
                }

                /* Send a 'SelectionNotify' event to the requestor of the clipboard
                 * selection contents with either the list of supported targets or else the
                 * clipboard contents in their chosen property. */
                XSendEvent(pX11Display, selectionNotifyEvent.requestor, True, 0, (XEvent *)&selectionNotifyEvent);
                XFlush(pX11Display);
            }
        }
        else
            RTThreadSleep(RT_MS_1SEC / 2);
    }

    XDestroyWindow(pX11Display, window);
    XCloseDisplay(pX11Display);

    return VINF_SUCCESS;
}
#endif

static DECLCALLBACK(int) tstTestReadFromHostSetup(PCLIPBOARDTESTCTX pTstCtx, void **ppvCtx)
{
    RT_NOREF(ppvCtx);
    int rc = VINF_SUCCESS;

    /* Set the right clipboard mode, so that the guest can read from the host. */
    tstClipboardSetMode(TstHgcmMockSvcInst(), VBOX_SHCL_MODE_BIDIRECTIONAL);

    /* Start the host thread first, so that the guest thread can connect to it later. */
    TSTHGCMUTILSHOSTCALLBACKS HostCallbacks;
    RT_ZERO(HostCallbacks);
    HostCallbacks.pfnOnClientConnected = tstTestReadFromHost_ClientConnectedCallback;
    TstHGCMUtilsHostThreadStart(&pTstCtx->HGCM, &HostCallbacks, pTstCtx /* pvUser */);

    PCLIPBOARDTESTTASK pTask  = &pTstCtx->Task;
    AssertPtr(pTask);
    pTask->enmFmtGst   = VBOX_SHCL_FMT_UNICODETEXT;
    pTask->enmFmtHst   = pTask->enmFmtGst;
    pTask->cbChunk     = RTRandU32Ex(1, 512);
    pTask->cbData      = RT_ALIGN_32(pTask->cbChunk * RTRandU32Ex(1, 16), 2);
    Assert(pTask->cbData % sizeof(RTUTF16) == 0);
#if !defined(RT_OS_WINDOWS) && !defined(RT_OS_OS2)
    pTask->pvData      = tstGenerateUtf8StringA(pTask->cbData);
    pTask->cbData++; /* Add terminating zero. */
    pTask->X11.fShutdown = false;
    rc = RTThreadCreate(&pTask->X11.hThread, tstSetClipboardContents, pTask, 0, RTTHREADTYPE_DEFAULT,
                        RTTHREADFLAGS_WAITABLE, "X11Copy");
    if (RT_SUCCESS(rc))
        rc = RTThreadUserWait(pTask->X11.hThread, RT_MS_5SEC);
    if (RT_FAILURE(rc))
        return VERR_NOT_SUPPORTED;

#else
    pTask->pvData      = tstGenerateUtf16StringA((uint32_t)(pTask->cbData /* We use bytes == chars here */));
    pTask->cbData     *= sizeof(RTUTF16);
    pTask->cbData     += sizeof(RTUTF16); /* Add terminating zero. */
#endif
    pTask->cbProcessed = 0;

#if defined (RT_OS_LINUX) || defined (RT_OS_SOLARIS)
    /* Initialize the Shared Clipboard backend callbacks. */
    PSHCLBACKEND pBackend = ShClSvcGetBackend();

    SHCLCALLBACKS ShClCallbacks;
    RT_ZERO(ShClCallbacks);
    ShClCallbacks.pfnReportFormats   = tstTestReadFromHost_ReportFormatsCallback;
    ShClCallbacks.pfnOnClipboardRead = tstTestReadFromHost_OnClipboardReadCallback;
    ShClBackendSetCallbacks(pBackend, &ShClCallbacks);
#elif defined (RT_OS_WINDOWS)
    rc = ShClWinOpen(GetDesktopWindow());
    if (RT_SUCCESS(rc))
    {
        rc = ShClWinDataWrite(CF_UNICODETEXT, pTask->pvData, (uint32_t)pTask->cbData);
        ShClWinClose();
    }
#endif /* defined (RT_OS_LINUX) || defined (RT_OS_SOLARIS) */

    RTTestPrintf(g_hTest, RTTESTLVL_DEBUG, "Host data (%RU32):\n%.*Rhxd\n", pTask->cbData, pTask->cbData, pTask->pvData);
    return rc;
}

static DECLCALLBACK(int) tstTestReadFromHostExec(PCLIPBOARDTESTCTX pTstCtx, void *pvCtx)
{
    RT_NOREF(pvCtx);

    RTTestISub("Testing guest reading from the host clipboard");

    TstHGCMUtilsGuestThreadStart(&pTstCtx->HGCM, tstTestReadFromHost_ThreadGuest, pTstCtx);

    PTSTHGCMUTILSTASK pHGCMTask = (PTSTHGCMUTILSTASK)TstHGCMUtilsTaskGetCurrent(&pTstCtx->HGCM);

    bool fUseMock = false;
    TSTUSERMOCK UsrMock;
    if (fUseMock)
        tstTestReadFromHost_MockInit(&UsrMock, "tstX11Hst");

    /* Wait until the task has been finished. */
    TstHGCMUtilsTaskWait(pHGCMTask, RT_MS_30SEC);

    if (fUseMock)
        tstTestReadFromHost_MockDestroy(&UsrMock);

    return VINF_SUCCESS;
}

#if defined (RT_OS_LINUX) || defined (RT_OS_SOLARIS)
/**
 * Stops the X11 "copy" thread (tstSetClipboardContents()).
 *
 * @return VBox status code.
 * @param  pTstCtx              A pointer to a clipboard test task which contains
 *                              the X11 "copy" thread's details.
 */
static int tstClipboardCopyThreadStop(PCLIPBOARDTESTCTX pTstCtx)
{
    PCLIPBOARDTESTTASK pTask = &pTstCtx->Task;
    ASMAtomicWriteBool(&pTask->X11.fShutdown, true);

    int rcThread;
    int rc = RTThreadWait(pTask->X11.hThread, RT_MS_30SEC, &rcThread);
    if (RT_SUCCESS(rc))
        rc = rcThread;
    if (RT_SUCCESS(rc))
        pTask->X11.hThread = NIL_RTTHREAD;

    return rc;
}
#endif

static DECLCALLBACK(int) tstTestReadFromHostDestroy(PCLIPBOARDTESTCTX pTstCtx, void *pvCtx)
{
    RT_NOREF(pvCtx);
    int vrc;

#if defined (RT_OS_LINUX) || defined (RT_OS_SOLARIS)
    vrc = tstClipboardCopyThreadStop(pTstCtx);
    if (RT_FAILURE(vrc))
        RTTestPrintf(g_hTest, RTTESTLVL_FAILURE, "tstSetClipboardContents() failed: rc=%Rrc\n", vrc);
#endif

    vrc = TstHGCMUtilsGuestThreadStop(&pTstCtx->HGCM);
    AssertRC(vrc);

    vrc = TstHGCMUtilsHostThreadStop(&pTstCtx->HGCM);
    AssertRC(vrc);

    return vrc;
}


/*********************************************************************************************************************************
*   Main                                                                                                                         *
*********************************************************************************************************************************/

/** Test definition table. */
CLIPBOARDTESTDESC g_aTests[] =
{
    /* Tests guest reading clipboard data from the host.  */
    { tstTestReadFromHostSetup,       tstTestReadFromHostExec,      tstTestReadFromHostDestroy }
};
/** Number of tests defined. */
unsigned g_cTests = RT_ELEMENTS(g_aTests);

static int tstOne(PTESTDESC pTstDesc)
{
    PCLIPBOARDTESTCTX pTstCtx = &g_TstCtx;

    void *pvCtx;
    int rc = pTstDesc->pfnSetup(pTstCtx, &pvCtx);
    if (RT_SUCCESS(rc))
    {
        rc = pTstDesc->pfnExec(pTstCtx, pvCtx);

        int rc2 = pTstDesc->pfnDestroy(pTstCtx, pvCtx);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    return rc;
}

int main()
{
    /*
     * Init the runtime, test and say hello.
     */
    RTEXITCODE rcExit = RTTestInitAndCreate("tstClipboardMockHGCM", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);

    PTSTHGCMMOCKSVC const pSvc = TstHgcmMockSvcInst();
    TstHgcmMockSvcCreate(pSvc);
    TstHgcmMockSvcStart(pSvc);

    RT_ZERO(g_TstCtx);

    PTSTHGCMUTILSCTX pCtx = &g_TstCtx.HGCM;
    TstHGCMUtilsCtxInit(pCtx, pSvc);

    PTSTHGCMUTILSTASK pTask = (PTSTHGCMUTILSTASK)TstHGCMUtilsTaskGetCurrent(pCtx);
    TstHGCMUtilsTaskInit(pTask);
    pTask->pvUser = &g_TstCtx.Task;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    testTransferStatusContextRouting();
#endif

#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
    testX11UnavailableBackend();
#endif

    /*
     * Run the remaining guest/backend tests only when an X11 display is
     * available on Unix systems.
     */
#if defined (RT_OS_LINUX) || defined (RT_OS_SOLARIS)
    VBGHDISPLAYSERVERTYPE const enmDisplayType = VBGHDisplayServerTypeDetect();
    RTTestPrintf(g_hTest, RTTESTLVL_DEBUG, "Display server type = %s\n", VBGHDisplayServerTypeToStr(enmDisplayType));
    if (enmDisplayType == VBGHDISPLAYSERVERTYPE_X11)
#endif
    {
        for (unsigned i = 0; i < RT_ELEMENTS(g_aTests); i++)
            tstOne(&g_aTests[i]);

        testGuestSimple();
    }

    testHostCall();

    TstHGCMUtilsTaskDestroy(pTask);

    TstHgcmMockSvcStop(pSvc);
    TstHgcmMockSvcDestroy(pSvc);

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(g_hTest);
}
