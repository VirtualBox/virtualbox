/* $Id: wayland-helper-edcp.cpp 114743 2026-07-21 18:31:58Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Ext Data Control Protocol (EDCP) helper for Wayland.
 *
 * This module implements Shared Clipboard support for Wayland guests
 * using Data Control Protocol interface.
 *
 * @note Obsolete. Compiled with 'kmk VBOX_WITH_WAYLAND_ADDITIONS_LEGACY=1'.
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

#include <errno.h>

#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include <VBox/GuestHost/mime-type-converter.h>

#include "VBoxClient.h"
#include "clipboard.h"
#include "wayland-helper.h"
#include "wayland-helper-ipc.h"
#include "wayland-helper-xdcp-common.h"

#include "wayland-client-protocol.h"
#include "ext-data-control-v1.h"

/**
 * A set of objects required to handle clipboard sharing over
 * Data Control Protocol. */
typedef struct
{
    vbox_wl_xdcp_base_ctx_t                     BaseCtx;

    /** Wayland Data Device object. */
    struct ext_data_control_device_v1           *pDataDevice;

    /** Wayland Data Control Manager object. */
    struct ext_data_control_manager_v1          *pDataControlManager;
} vbox_wl_edcp_ctx_t;

/** Helper context. */
static vbox_wl_edcp_ctx_t g_EdcpCtx;


/**********************************************************************************************************************************
 * Host Clipboard service callbacks.
 *********************************************************************************************************************************/


/**
 * Session callback: Generic session initializer.
 *
 * This callback starts new session.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type (unused).
 * @param   pvUser              User data.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_edcp_session_start_generic_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    vbox_wl_dcp_session_t *pSession = (vbox_wl_dcp_session_t *)pvUser;
    RT_NOREF(enmSessionType);

    VBCL_LOG_CALLBACK;

    AssertPtrReturn(pSession, VERR_INVALID_PARAMETER);
    vbcl_wayland_xdcp_session_prepare(&g_EdcpCtx.BaseCtx);

    return VINF_SUCCESS;
}

/**
 * Wayland registry global handler.
 *
 * This callback is triggered when Wayland Registry listener is registered.
 * Wayland client library will trigger it individually for each available global
 * object.
 *
 * @param pvUser            Context data.
 * @param pRegistry         Wayland Registry object.
 * @param uName             Numeric name of the global object.
 * @param pszIface          Name of interface implemented by the object.
 * @param uVersion          Interface version.
 */
static void vbcl_wayland_hlp_edcp_registry_global_handler(
    void *pvUser, struct wl_registry *pRegistry, uint32_t uName, const char *pszIface, uint32_t uVersion)
{
    vbox_wl_edcp_ctx_t * const pCtx = (vbox_wl_edcp_ctx_t *)pvUser;
    AssertPtrReturnVoid(pCtx);
    AssertPtrReturnVoid(pszIface);
    VBClLogVerbose(6, "Wayland interface %s (%u) v%u\n", pszIface, uName, uVersion);

    /* Call wl_registry_bind & return if matching anything we're interested in. */
    VBCL_WAYLAND_REGISTRY_MATCH_AND_BIND_AND_RET(pRegistry, pszIface, uName,    wl_seat_interface,
                                                 pCtx->BaseCtx.pSeat,           struct wl_seat *,
                                                 VBCL_WAYLAND_SEAT_VERSION_MIN);
    VBCL_WAYLAND_REGISTRY_MATCH_AND_BIND_AND_RET(pRegistry, pszIface, uName,    ext_data_control_manager_v1_interface,
                                                 pCtx->pDataControlManager,     struct ext_data_control_manager_v1 *,
                                                 VBCL_WAYLAND_ZWLR_DATA_CONTROL_MANAGER_VERSION_MIN);
}

/**
 * Wayland registry global remove handler.
 *
 * Triggered when global object is removed from Wayland registry.
 *
 * @param pvUser            Context data.
 * @param pRegistry         Wayland Registry object.
 * @param uName             Numeric name of the global object.
 */
static void vbcl_wayland_hlp_edcp_registry_global_remove_handler(
    void *pvUser, struct wl_registry *pRegistry, uint32_t uName)
{
    RT_NOREF(pvUser);
    RT_NOREF(pRegistry);
    RT_NOREF(uName);
}

/** Wayland global registry callbacks. */
static const struct wl_registry_listener g_vbcl_wayland_hlp_registry_cb =
{
    &vbcl_wayland_hlp_edcp_registry_global_handler,       /* .global */
    &vbcl_wayland_hlp_edcp_registry_global_remove_handler /* .global_remove */
};


/*********************************************************************************************************************************
*   Wayland Data Control Offer Listener Callbacks (ext_data_control_offer_v1_listener)                                           *
*********************************************************************************************************************************/

/**
 * Wayland callback: Data Control Offer advertise.
 *
 * Triggered when other Wayland client advertises new clipboard content.
 *
 * @param pvUser            Context data.
 * @param pOffer            Wayland Data Control Offer object.
 * @param pcszMimeType      MIME type of newly available clipboard data.
 */
static void vbcl_wayland_hlp_edcp_data_control_offer_offer(void *pvUser, struct ext_data_control_offer_v1 *pOffer,
                                                           const char *pcszMimeType)
{
    VBClLogVerbose(3, "%s: %s\n", __func__, pcszMimeType);
    SHCLWLOFFERSLOT * const pOfferSlot = (SHCLWLOFFERSLOT *)pvUser;
    AssertPtrReturnVoid(pOfferSlot);
    VbghWaylandClipboardOfferAddMimeType(pOfferSlot, pcszMimeType, __func__);
    RT_NOREF(pOffer);
}

/** Wayland Data Control Offer interface callbacks. */
static const struct ext_data_control_offer_v1_listener g_data_control_offer_listener =
{
    &vbcl_wayland_hlp_edcp_data_control_offer_offer,
};


/**********************************************************************************************************************************
 * Wayland Data Control Device callbacks.
 *********************************************************************************************************************************/

/** Callback for use with VBClWaylandXdcpFillOurCacheFromOfferAndReport. */
static DECLCALLBACK(void)
vbclWaylandHlpEdcpOfferV1ReceiveWrapper(VBCLWLHLP_XDCP_FILL_OUR_CACHE_FROM_OFFER_AND_REPORT_ARGS_T *pArgs,
                                        const char *pszMimeType, int fdWrite)
{
    ext_data_control_offer_v1_receive((struct ext_data_control_offer_v1 *)pArgs->pvOffer, pszMimeType, fdWrite);
}

/**
 * Data Control Device offer callback.
 *
 * Triggered when other Wayland client advertises new clipboard content.
 * When this callback is triggered, a new ext_data_control_offer_v1 object
 * is created. This callback should setup listener callbacks for this object.
 *
 * @param pvUser            Context data.
 * @param pDevice           Wayland Data Control Device object.
 * @param pOffer            Wayland Data Control Offer object.
 */
static void vbclWaylandHlpEdcpDataDeviceListener_DataOffer(void *pvUser, struct ext_data_control_device_v1 *pDevice,
                                                           struct ext_data_control_offer_v1 *pOffer)
{
    /*
     * Validate input a little.
     */
    VBClLogVerbose(3, "%s: pDevice=%p pOffer=%p\n", __func__, pDevice, pOffer);
    vbox_wl_edcp_ctx_t * const pCtx = (vbox_wl_edcp_ctx_t *)pvUser;
    AssertPtrReturnVoid(pCtx);
    PSHCLCONTEXT const pShClCtx = pCtx->BaseCtx.pShClCtx;
    RT_NOREF(pDevice);
    AssertPtrReturnVoid(pOffer);

    /*
     * Fend off stuff
     */
    if (pCtx->BaseCtx.fIngnoreWlClipIn)
        VBClLogVerbose(5, "ignoring Wayland clipboard data offer, we advertising new clipboard ourselves\n");
    else if (!pShClCtx)
        VBClLogVerbose(5, "ignoring Wayland clipboard data offer - no shared clipboard context (pShClCtx)!\n");
    else
    {
        /*
         * Since we're getting offers from both the clipboard & the primary selection
         * here, before we know which it is, we record the offers being made in a
         * temporary slot in the context structure.  When _Selection or _PrimarySelection
         * is then called later, we take the appropriate action.
         */

        /* Get a slot for taking down this offer. */
        RTCritSectEnter(&pShClCtx->Wl.CritSect);
        SHCLWLOFFERSLOT * const pOfferSlot = &pShClCtx->Wl.aOurOfferSlots[pShClCtx->Wl.idxOurOfferSlots++
                                                                          % RT_ELEMENTS(pShClCtx->Wl.aOurOfferSlots)];
        for (unsigned i = 0; i < RT_ELEMENTS(pOfferSlot->aMimeTypes); i++)
        {
            pOfferSlot->aMimeTypes[i].fFlagsAndPriority = 0;
            pOfferSlot->aMimeTypes[i].pszMimeType       = NULL;
        }
        pOfferSlot->fFormats               = VBOX_SHCL_FMT_NONE;
        pOfferSlot->fHasRevisionNoMimeType = false;
        pOfferSlot->uRevision              = pShClCtx->Wl.uRevision;
        pOfferSlot->pCtx                   = &pShClCtx->Wl;
        pOfferSlot->pvOffer                = pOffer;

        RTCritSectLeave(&pShClCtx->Wl.CritSect);

        /* Register listener on the offer and get all the mime types. */
        ext_data_control_offer_v1_add_listener(pOffer, &g_data_control_offer_listener, pOfferSlot);

        /* Receive all the advertised mime types. */
        wl_display_roundtrip(pCtx->BaseCtx.pDisplay);
    }
}

/**
 * Data Control Device selection callback.
 *
 * Triggered when Wayland client advertises new clipboard content.
 * In this callback, actual clipboard data is received from Wayland client.
 *
 * @param pvUser            Context data.
 * @param pDevice           Wayland Data Control Device object.
 * @param pOffer            Wayland Data Control Offer object.
 */
static void vbclWaylandHlpEdcpDataDeviceListener_Selection(void *pvUser, struct ext_data_control_device_v1 *pDevice,
                                                           struct ext_data_control_offer_v1 *pOffer)
{
    /*
     * Validate input a little.
     */
    VBClLogVerbose(3, "%s: pDevice=%p pOffer=%p\n", __func__, pDevice, pOffer);
    vbox_wl_edcp_ctx_t * const pCtx = (vbox_wl_edcp_ctx_t *)pvUser;
    AssertPtrReturnVoid(pCtx);
    PSHCLCONTEXT const pShClCtx = pCtx->BaseCtx.pShClCtx;
    RT_NOREF(pDevice);

    /*
     * Skip this if too early.
     */
    if (!pShClCtx)
        VBClLogVerbose(5, "ignoring Wayland clipboard selection - no shared clipboard context (pShClCtx)!\n");
    else if (pOffer == NULL)
    {
        LogRel4(("%s: Clipboard emptied (pOffer is NULL).\n", __func__));

        int rc = vbcl_wayland_session_end(&pCtx->BaseCtx.Session.Base, NULL, NULL);
        if (RT_SUCCESS(rc))
        {
            rc = vbcl_wayland_session_start(&pCtx->BaseCtx.Session.Base,
                                            VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST,
                                            &vbcl_wayland_hlp_edcp_session_start_generic_cb,
                                            &pCtx->BaseCtx.Session);
            if (RT_SUCCESS(rc))
            {
                RTCritSectEnter(&pShClCtx->Wl.CritSect);

                VbghWaylandClipboardResetOurState(&pShClCtx->Wl, __func__, NULL);
                rc = RTSemEventMultiSignal(pShClCtx->Wl.hOurCacheFilledEvent);
                if (RT_FAILURE(rc))
                    LogRel(("error: %s: RTSemEventMultiSignal failed: %Rrc\n", __func__, rc));

                VbglR3ClipboardReportFormats(pShClCtx->CmdCtx.idClient, VBOX_SHCL_FMT_NONE);

                RTCritSectLeave(&pShClCtx->Wl.CritSect);
            }
        }
    }
    else
    {
        /*
         * Get the last offer slot and see if it matches up to the offer,
         * that our clipboard state hasn't changed, and that it isn't our offer.
         */
        RTCritSectEnter(&pShClCtx->Wl.CritSect);
        SHCLWLOFFERSLOT * const pOfferSlot = &pShClCtx->Wl.aOurOfferSlots[  (pShClCtx->Wl.idxOurOfferSlots - 1)
                                                                          % RT_ELEMENTS(pShClCtx->Wl.aOurOfferSlots)];
        if (pOfferSlot->pvOffer != (void *)pOffer)
            VBClLogVerbose(4, "vbclWaylandHlpEdcpDataDeviceListener_Selection: Ignore - pOffer %p, expected %p!\n",
                           pOffer, pOfferSlot->pvOffer);
        else if (pOfferSlot->uRevision != pShClCtx->Wl.uRevision)
            VBClLogVerbose(4, "vbclWaylandHlpEdcpDataDeviceListener_Selection: Ignore - uRevision %RX64 -> %#RX64!\n",
                           pOfferSlot->uRevision, pShClCtx->Wl.uRevision);
        else if (pOfferSlot->fHasRevisionNoMimeType)
            VBClLogVerbose(4, "vbclWaylandHlpEdcpDataDeviceListener_Selection: Ignore our own offer.\n");
        else
        {
            RTCritSectLeave(&pShClCtx->Wl.CritSect);

            /*
             * New session.
             */
            int rc = vbcl_wayland_session_end(&pCtx->BaseCtx.Session.Base, NULL, NULL);
            if (RT_SUCCESS(rc))
            {
                rc = vbcl_wayland_session_start(&pCtx->BaseCtx.Session.Base,
                                                VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST,
                                                &vbcl_wayland_hlp_edcp_session_start_generic_cb,
                                                &pCtx->BaseCtx.Session);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Reset the common VBoxClient clipboard state.
                     */
                    uint64_t const uRevision = VBClWaylandClipboardResetOurState(pShClCtx, __func__, pOfferSlot);

                    /* Fill the cache with data and announce it to the host. */
                    VBCLWLHLP_XDCP_FILL_OUR_CACHE_FROM_OFFER_AND_REPORT_ARGS_T Args =
                    { pShClCtx, uRevision, pCtx->BaseCtx.pDisplay, pOffer, vbclWaylandHlpEdcpOfferV1ReceiveWrapper };
                    VBClWaylandSessionJoin(&pCtx->BaseCtx.Session.Base, VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST,
                                           VBClWaylandXdcpFillOurCacheFromOfferAndReport, &Args);

                    /*
                     * ??
                     */
                    /** @todo Moved this from the vbclWaylandHlpXdcpGhClipFillCacheAndReport
                     *        callback, as I cannot see how calling here or there makes much of a
                     *        difference.
                     *
                     *        However, that said, why aren't we calling at the very very end of this
                     *        function for all cases. What's the difference when fIngnoreWlClipIn is
                     *        true, pShClCtx isn't set or vbcl_wayland_session_end/start fails?? */
                    ext_data_control_offer_v1_destroy(pOffer);
                }
                else
                    VBClLogError("unable to start session, rc=%Rrc\n", rc);
            }
            else
                VBClLogError("unable to start session, previous session is still running, rc=%Rrc\n", rc);
            return;
        }
        RTCritSectLeave(&pShClCtx->Wl.CritSect);
    }
}

/**
 * Data Control Device finished callback.
 *
 * Triggered when Data Control Device object is no longer valid and
 * needs to be destroyed.
 *
 * @param pvUser            Context data.
 * @param pDevice           Wayland Data Control Device object.
 */
static void vbclWaylandHlpEdcpDataDeviceListener_Finished(void *pvUser, struct ext_data_control_device_v1 *pDevice)
{
    vbox_wl_edcp_ctx_t *pCtx = (vbox_wl_edcp_ctx_t *)pvUser;

    VBCL_LOG_CALLBACK;

    AssertPtrReturnVoid(pCtx);
    AssertPtrReturnVoid(pDevice);

    Assert(pDevice == pCtx->pDataDevice);
    ext_data_control_device_v1_destroy(pDevice);
    pCtx->pDataDevice = NULL;
}

/**
 * Data Control Device primary selection callback.
 *
 * Same as shcl_wl_data_control_device_selection, but triggered for
 * primary selection case.
 *
 * @param pvUser            Context data.
 * @param pDevice           Wayland Data Control Device object.
 * @param pOffer            Wayland Data Control Offer object.
 */
static void vbclWaylandHlpEdcpDataDeviceListener_PrimarySelection(void *pvUser, struct ext_data_control_device_v1 *pDevice,
                                                                  struct ext_data_control_offer_v1 *pOffer)
{
    VBClLogVerbose(3, "%s: pDevice=%p pOffer=%p\n", __func__, pDevice, pOffer);
    RT_NOREF(pDevice, pvUser, pOffer);
    VBCL_LOG_CALLBACK;
}


/** Data Control Device interface callbacks. */
static const struct ext_data_control_device_v1_listener g_data_device_listener =
{
    vbclWaylandHlpEdcpDataDeviceListener_DataOffer,
    vbclWaylandHlpEdcpDataDeviceListener_Selection,
    vbclWaylandHlpEdcpDataDeviceListener_Finished,
    vbclWaylandHlpEdcpDataDeviceListener_PrimarySelection,
};


/**********************************************************************************************************************************
 * Wayland Data Control Source callbacks.
 *********************************************************************************************************************************/

/**
 * @callback_method_impl{FNVBCLWAYLANDSESSIONJOIN,
 *      Session callback: Copy clipboard to the guest.}
 *
 * Worker for vbcl_wayland_hlp_edcp_data_source_send().
 *
 * This callback must be executed in context of Wayland event thread
 * in order to be able to inject clipboard content into Wayland. It is
 * triggered when Wayland client already decided data in which format
 * it wants to request.
 *
 * This callback (1) sets requested clipboard format to the session,
 * (2) waits for clipboard data to be copied from the host, (3) converts
 * host clipboard data into guest representation, and (4) sends clipboard
 * to the guest by writing given file descriptor.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_edcp_clip_hg_report_join3_cb(void *pvUser)
{
    struct vbcl_wl_dcp_write_ctx * const pArgs = (struct vbcl_wl_dcp_write_ctx *)pvUser;
    AssertPtrReturn(pArgs, VERR_INVALID_PARAMETER);

    int rc = VBClWaylandXdcpSetGuestClipboard(pArgs->fd, &g_EdcpCtx.BaseCtx, pArgs->pcszMimeType);
    g_EdcpCtx.BaseCtx.fIngnoreWlClipIn = false;
    return rc;
}


/**
 * Wayland callback: Send clipboard data.
 *
 * Triggered when other Wayland client wants to read clipboard data from us.
 *
 * @param   pvUser          VBox private data.
 * @param   pDataSource     Wayland Data Control Source object.
 * @param   pcszMimeType    A MIME type of requested data.
 * @param   fd              A file descriptor to write clipboard content into.
 */
static void vbcl_wayland_hlp_edcp_data_source_send(void *pvUser, struct ext_data_control_source_v1 *pDataSource,
                                                   const char *pcszMimeType, int32_t fd)
{
    /*
     * Validate input.
     */
    vbox_wl_edcp_ctx_t * const pCtx = (vbox_wl_edcp_ctx_t *)pvUser;
    AssertPtrReturnVoid(pCtx);
    AssertPtrReturnVoid(pcszMimeType);
    Assert(fd >= 0);
    RT_NOREF(pDataSource);
    VBClLogVerbose(3, "%s: %s -> fd=%d\n", __func__, pcszMimeType, fd);

    /*
     * "Join" the session and do actual work in callback.
     */
    struct vbcl_wl_dcp_write_ctx Args;
    Args.pcszMimeType = pcszMimeType;
    Args.fd = fd;
    int rc = VBClWaylandSessionJoin(&pCtx->BaseCtx.Session.Base, VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST,
                                    vbcl_wayland_hlp_edcp_clip_hg_report_join3_cb, &Args);

    VBClLogVerbose(5, "%s: %s -> rc=%Rrc\n", __func__, pcszMimeType, rc);
    close(fd);
}

/**
 * Wayland data canceled callback.
 *
 * Triggered when data source was replaced by another data source
 * and no longer valid.
 *
 * @param pvData            VBox private data.
 * @param pDataSource       Wayland Data Control Source object.
 */
static void vbcl_wayland_hlp_edcp_data_source_cancelled(
    void *pvData, struct ext_data_control_source_v1 *pDataSource)
{
    RT_NOREF(pvData);
    VBCL_LOG_CALLBACK;
    ext_data_control_source_v1_destroy(pDataSource);
}


/** Wayland Data Control Source interface callbacks. */
static const struct ext_data_control_source_v1_listener g_data_source_listener =
{
    &vbcl_wayland_hlp_edcp_data_source_send,
    &vbcl_wayland_hlp_edcp_data_source_cancelled,
};


/**********************************************************************************************************************************
 * Helper specific code and session callbacks.
 *********************************************************************************************************************************/

/**
 * Initializes the context.
 */
static int vbClWaylandHlpEdcpCtxInit(vbox_wl_edcp_ctx_t *pCtx)
{
    pCtx->pDataDevice = NULL;
    pCtx->pDataControlManager = NULL;
    return VBClWaylandXdcpCtxInit(&pCtx->BaseCtx);
}

/**
 * Terminates (uninitalizes) the context.
 */
static void vbClWaylandHlpEdcpCtxTerm(vbox_wl_edcp_ctx_t *pCtx)
{
    VBClWaylandXdcpCtxTerm(&pCtx->BaseCtx);
    pCtx->pDataDevice = NULL;
    pCtx->pDataControlManager = NULL;
}

/**
 * Disconnect from Wayland compositor.
 *
 * Close connection, release resources.
 *
 * @param pCtx              Context data.
 */
static void vbcl_wayland_hlp_edcp_disconnect(vbox_wl_edcp_ctx_t *pCtx)
{
    AssertReturnVoid(pCtx);

    if (RT_VALID_PTR(pCtx->pDataControlManager))
    {
        ext_data_control_manager_v1_destroy(pCtx->pDataControlManager);
        pCtx->pDataControlManager = NULL;
    }

    if (RT_VALID_PTR(pCtx->pDataDevice))
    {
        ext_data_control_device_v1_destroy(pCtx->pDataDevice);
        pCtx->pDataDevice = NULL;
    }

    if (RT_VALID_PTR(pCtx->BaseCtx.pSeat))
    {
        wl_seat_destroy(pCtx->BaseCtx.pSeat);
        pCtx->BaseCtx.pSeat = NULL;
    }

    if (RT_VALID_PTR(pCtx->BaseCtx.pRegistry))
    {
        wl_registry_destroy(pCtx->BaseCtx.pRegistry);
        pCtx->BaseCtx.pRegistry = NULL;
    }

    if (RT_VALID_PTR(pCtx->BaseCtx.pDisplay))
    {
        wl_display_disconnect(pCtx->BaseCtx.pDisplay);
        pCtx->BaseCtx.pDisplay = NULL;
    }
}

/**
 * Connect to Wayland compositor.
 *
 * Establish connection, bind to all required interfaces.
 *
 * @returns TRUE on success, FALSE otherwise.
 * @param   pCtx                Context data.
 */
static bool vbcl_wayland_hlp_edcp_connect(vbox_wl_edcp_ctx_t *pCtx)
{
    AssertPtrReturn(pCtx, false);

    const char *pszWaylandDisplay = RTEnvGet(VBCL_ENV_WAYLAND_DISPLAY);
    if (RT_VALID_PTR(pszWaylandDisplay))
        pCtx->BaseCtx.pDisplay = wl_display_connect(pszWaylandDisplay);
    else
        VBClLogError("cannot connect to Wayland compositor: " VBCL_ENV_WAYLAND_DISPLAY " environment variable not set!\n");
    if (RT_VALID_PTR(pCtx->BaseCtx.pDisplay))
    {
        pCtx->BaseCtx.pRegistry = wl_display_get_registry(pCtx->BaseCtx.pDisplay);
        if (RT_VALID_PTR(pCtx->BaseCtx.pRegistry))
        {
            wl_registry_add_listener(pCtx->BaseCtx.pRegistry, &g_vbcl_wayland_hlp_registry_cb, (void *)pCtx);
            wl_display_roundtrip(pCtx->BaseCtx.pDisplay);

            if (RT_VALID_PTR(pCtx->pDataControlManager))
            {
                if (RT_VALID_PTR(pCtx->BaseCtx.pSeat))
                {
                    pCtx->pDataDevice = ext_data_control_manager_v1_get_data_device(pCtx->pDataControlManager,
                                                                                    pCtx->BaseCtx.pSeat);
                    if (RT_VALID_PTR(pCtx->pDataDevice))
                    {
                        if (RT_VALID_PTR(pCtx->pDataControlManager))
                            return true;

                        VBClLogError("cannot get Wayland data control manager interface\n");
                    }
                    else
                        VBClLogError("cannot get Wayland data device interface\n");
                }
                else
                    VBClLogError("cannot get Wayland seat interface\n");
            }
            else
                VBClLogError("cannot get Wayland device manager interface\n");
        }
        else
            VBClLogError("cannot connect to Wayland registry\n");
    }
    else
        VBClLogError("cannot connect to Wayland compositor\n");
    vbcl_wayland_hlp_edcp_disconnect(pCtx);
    return false;
}

/**
 * @callback_method_impl{FNVBGHMIMECONVENUM,
 *      Enumeration callback used for sending clipboard offers to Wayland client. }
 *
 * When the host announces its clipboard content, this call back is used to send
 * corresponding offers to other Wayland clients.
 *
 * @note Callback must be executed in context of Wayland event thread.
 */
static DECLCALLBACK(void)
vbcl_wayland_hlp_edcp_send_offers(const char *pcszMimeType, uint32_t fFlagsAndPriority, void *pvUser)
{
    ext_data_control_source_v1 *pDataSource = (ext_data_control_source_v1 *)pvUser;
    if (!(fFlagsAndPriority & VBGH_MIME_CONV_F_RO))
    {
        VBClLogVerbose(4, "%s: %s prio %#x\n", __func__, pcszMimeType, fFlagsAndPriority);
        ext_data_control_source_v1_offer(pDataSource, pcszMimeType);
    }
}

/**
 * @callback_method_impl{FNVBCLWAYLANDSESSIONJOIN,
 *      Session callback: Advertise clipboard to the guest.}
 *
 * This callback must be executed in context of Wayland event thread
 * in order to be able to inject clipboard content into Wayland.
 *
 * This callback (1) prevents Wayland event loop from processing
 * incoming clipboard advertisements before sending any data to
 * other Wayland clients (this is needed in order to avoid feedback
 * loop from our own advertisements), (2) waits for the list of clipboard
 * formats available on the host side (set by vbcl_wayland_hlp_edcp_clip_hg_report_join_cb),
 * and (3) sends data offers for available host clipboard to other clients.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_edcp_clip_hg_report_join2_cb(void *pvUser)
{
    VBClLogVerbose(3, "%s\n", __func__);
    RT_NOREF(pvUser);

    g_EdcpCtx.BaseCtx.fIngnoreWlClipIn = true;

    int rc;
    SHCLFORMATS const fFmts = g_EdcpCtx.BaseCtx.Session.clip.fFmts.wait();
    VBClLogVerbose(3, "%s: fFmts=%#x\n", __func__, fFmts);
    if (fFmts != g_EdcpCtx.BaseCtx.Session.clip.fFmts.defaults())
    {
        struct ext_data_control_source_v1 *pDataSource
            = ext_data_control_manager_v1_create_data_source(g_EdcpCtx.pDataControlManager);
        if (RT_VALID_PTR(pDataSource))
        {
            ext_data_control_source_v1_add_listener(pDataSource, &g_data_source_listener, &g_EdcpCtx);

            ext_data_control_source_v1_offer(pDataSource, VBOX_CLIPBOARD_MIME_TYPE_REVISION_NO);
            VbghMimeConvEnumerateByVBoxFormats(fFmts, vbcl_wayland_hlp_edcp_send_offers, pDataSource);

            ext_data_control_device_v1_set_selection(g_EdcpCtx.pDataDevice, pDataSource);
            /** @todo r=bird: what happens to pDataSource now? */

            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_NO_DATA;
    return rc;
}

/**
 * @callback_method_impl{FNRTTHREAD,
 *      Main loop for Wayland compositor events.}
 *
 * All requests to Wayland compositor must be performed in context
 * of this thread.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_edcp_event_loop(RTTHREAD ThreadSelf, void *pvUser)
{
    vbox_wl_edcp_ctx_t * const pCtx = (vbox_wl_edcp_ctx_t *)pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_PARAMETER);

    int rc = VERR_TRY_AGAIN;
    if (vbcl_wayland_hlp_edcp_connect(pCtx))
    {
        /* Start listening Data Control Device interface. */
        if (ext_data_control_device_v1_add_listener(pCtx->pDataDevice, &g_data_device_listener, (void *)pCtx) == 0)
        {
            /* Tell parent thread we are up and running. */
            RTThreadUserSignal(ThreadSelf);

            /*
             * The event processing loop.
             */
            while (!ASMAtomicReadBool(&pCtx->BaseCtx.fShutdown))
            {
                int rc2 = vbcl_wayland_xdcp_next_event(&pCtx->BaseCtx);
                if (   rc2 != VERR_TIMEOUT
                    && RT_FAILURE(rc2))
                    VBClLogError("cannot read event from Wayland: rc2=%Rrc\n", rc2);

                if (pCtx->BaseCtx.fSendToGuest.reset())
                    VBClWaylandSessionJoin(&pCtx->BaseCtx.Session.Base, VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST,
                                           vbcl_wayland_hlp_edcp_clip_hg_report_join2_cb, NULL);
            }

            rc = VINF_SUCCESS;
        }
        else
        {
            VBClLogError("cannot subscribe to Data Control Device events\n");
            rc = VERR_NOT_SUPPORTED;
        }

        vbcl_wayland_hlp_edcp_disconnect(pCtx);
    }
    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER,pfnProbe}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_edcp_probe(void)
{
    int fCaps = VBOX_WAYLAND_HELPER_CAP_NONE;

    VBGHDISPLAYSERVERTYPE enmDisplayServerType = VBGHDisplayServerTypeDetect();
    if (VBGHDisplayServerTypeIsWaylandAvailable(enmDisplayServerType))
    {
        vbox_wl_edcp_ctx_t probeCtx;
        vbClWaylandHlpEdcpCtxInit(&probeCtx);

        if (vbcl_wayland_hlp_edcp_connect(&probeCtx))
        {
            fCaps |= VBOX_WAYLAND_HELPER_CAP_CLIPBOARD;
            vbcl_wayland_hlp_edcp_disconnect(&probeCtx);
        }

        vbClWaylandHlpEdcpCtxTerm(&probeCtx);
    }

    return fCaps;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnInit}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_edcp_clip_init(void)
{
    int rc = vbClWaylandHlpEdcpCtxInit(&g_EdcpCtx);
    if (RT_SUCCESS(rc))
    {
        rc = VBClStartThread(&g_EdcpCtx.BaseCtx.Thread, vbcl_wayland_hlp_edcp_event_loop, "wl-edcp", &g_EdcpCtx);
        if (RT_FAILURE(rc))
            vbClWaylandHlpEdcpCtxTerm(&g_EdcpCtx);
    }
    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnTerm}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_edcp_clip_term(void)
{
    /* Set termination flag. Wayland event loop should pick it up
       on the next iteration. */
    ASMAtomicWriteBool(&g_EdcpCtx.BaseCtx.fShutdown, true);
    RTThreadPoke(g_EdcpCtx.BaseCtx.Thread);

    /* Wait for Wayland event loop thread to shutdown. */
    int rcThread = VINF_SUCCESS;
    int rc = RTThreadWait(g_EdcpCtx.BaseCtx.Thread, RT_MS_30SEC, &rcThread);
    if (RT_SUCCESS(rc))
        VBClLogInfo("Wayland event thread exited with status, rc=%Rrc\n", rcThread);
    else
        VBClLogError("unable to stop Wayland event thread, rc=%Rrc\n", rc);

    /* Final cleanup, just to be tidy. */
    if (RT_SUCCESS(rc) || rc == VERR_INVALID_HANDLE)
        vbClWaylandHlpEdcpCtxTerm(&g_EdcpCtx);
    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnSetClipboardCtx}
 */
static DECLCALLBACK(void) vbcl_wayland_hlp_edcp_clip_set_ctx(PSHCLCONTEXT pCtx)
{
    g_EdcpCtx.BaseCtx.pShClCtx = pCtx;
}

/**
 * @callback_method_impl{FNVBCLWAYLANDSESSIONJOIN,
 *      Session callback: Copy clipboard from the host.}
 *
 * This callback (1) sets host clipboard formats list to the session,
 * (2) asks Wayland event thread to advertise these formats to the guest,
 * (3) waits for guest to request clipboard in specific format.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_edcp_clip_hg_report_join_cb(void *pvUser)
{
    AssertPtrReturn(pvUser, VERR_INVALID_PARAMETER);
    SHCLFORMATS const fFmts = *(SHCLFORMATS const *)pvUser;
    VBClLogVerbose(3, "%s: %#x\n", __func__, fFmts);
    return VBClWaylandXdcpReportHostClipboardFormats(&g_EdcpCtx.BaseCtx, fFmts);
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnHGClipReport}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_edcp_clip_hg_report(PSHCLCONTEXT pCtx, SHCLFORMATS fFormats)
{
    RT_NOREF(pCtx);
    VBClLogVerbose(3, "%s: %#x\n", __func__, fFormats);

    int rc;
    if (fFormats != VBOX_SHCL_FMT_NONE)
    {
        /** @todo r=bird: Abstract all these three operations into a single
         *        VBClWaylandSessionReStart/Whatever, reducing code duplication
         *        optimizing state transitions. */
        rc = vbcl_wayland_session_end(&g_EdcpCtx.BaseCtx.Session.Base, NULL, NULL);
        if (RT_SUCCESS(rc))
        {
            rc = vbcl_wayland_session_start(&g_EdcpCtx.BaseCtx.Session.Base,
                                            VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST,
                                            &vbcl_wayland_hlp_edcp_session_start_generic_cb,
                                            &g_EdcpCtx.BaseCtx.Session);
            if (RT_SUCCESS(rc))
                rc = VBClWaylandSessionJoin(&g_EdcpCtx.BaseCtx.Session.Base, VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST,
                                            vbcl_wayland_hlp_edcp_clip_hg_report_join_cb, &fFormats);
        }
        else
            VBClLogError("unable to start session, previous session is still running rc=%Rrc\n", rc);
    }
    else
    {
        /** @todo r=bird: if the current session is copy-to-guest, end it. Can we tell
         *        the rest of wayland that we no longer have any clipboard data?  */
        rc = VERR_NO_DATA;
    }
    return rc;
}

/** Ext data control protocol helper callbacks. */
const VBCLWAYLANDHELPER g_WaylandHelperEdcp =
{
    /* .pszName  = */ "wayland-ext-dcp",
    /* .pfnProbe = */ vbcl_wayland_hlp_edcp_probe,
    /* .clip     = */
    {
        /* .pfnInit            = */ vbcl_wayland_hlp_edcp_clip_init,
        /* .pfnTerm            = */ vbcl_wayland_hlp_edcp_clip_term,
        /* .pfnSetClipboardCtx = */ vbcl_wayland_hlp_edcp_clip_set_ctx,
        /* .pfnPopup           = */ NULL,
        /* .pfnHGClipReport    = */ vbcl_wayland_hlp_edcp_clip_hg_report,
        /* .pfnGHClipRead      = */ NULL,
    },
    /* .dnd      = */
    {
        /* .pfnInit = */            NULL,
        /* .pfnTerm = */            NULL,
    },
};
