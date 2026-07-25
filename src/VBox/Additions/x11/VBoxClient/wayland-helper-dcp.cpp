/* $Id: wayland-helper-dcp.cpp 114771 2026-07-25 21:20:37Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Data Control Protocol (DCP) helper for Wayland.
 *
 * This module implements Shared Clipboard support for Wayland guests
 * using Data Control Protocol interface.
 */

/*
 * Copyright (C) 2006-2025 Oracle and/or its affiliates.
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

#include <iprt/env.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include <VBox/GuestHost/mime-type-converter.h>

#include "VBoxClient.h"
#include "clipboard.h"
#include "wayland-helper.h"
#include "wayland-helper-ipc.h"
#include "wayland-helper-xdcp-common.h"

#include "wayland-client-protocol.h"
#include "wlr-data-control-unstable-v1.h"

/**
 * A set of objects required to handle clipboard sharing over
 * Data Control Protocol. */
typedef struct
{
    vbox_wl_xdcp_base_ctx_t                     BaseCtx;

    /** Wayland Data Device object. */
    struct zwlr_data_control_device_v1          *pDataDevice;

    /** Wayland Data Control Manager object. */
    struct zwlr_data_control_manager_v1         *pDataControlManager;
} vbox_wl_dcp_ctx_t;

/** Helper context. */
static vbox_wl_dcp_ctx_t g_DcpCtx;


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
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_session_start_generic_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    vbox_wl_dcp_session_t *pSession = (vbox_wl_dcp_session_t *)pvUser;
    RT_NOREF(enmSessionType);

    VBCL_LOG_CALLBACK;

    AssertPtrReturn(pSession, VERR_INVALID_PARAMETER);
    vbcl_wayland_xdcp_session_prepare(&g_DcpCtx.BaseCtx);

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
 * @param sIface            Name of interface implemented by the object.
 * @param uVersion          Interface version.
 */
static void vbcl_wayland_hlp_dcp_registry_global_handler(
    void *pvUser, struct wl_registry *pRegistry, uint32_t uName, const char *sIface, uint32_t uVersion)
{
    vbox_wl_dcp_ctx_t *pCtx = (vbox_wl_dcp_ctx_t *)pvUser;

    RT_NOREF(pRegistry);
    RT_NOREF(uVersion);

    AssertPtrReturnVoid(pCtx);
    AssertPtrReturnVoid(sIface);

    /* Wrappers around 'if' statement. */
    /** @todo r=bird: Unreadable! */
         VBCL_WAYLAND_REGISTRY_ADD_MATCH(pRegistry, sIface, uName, wl_seat_interface,                       pCtx->BaseCtx.pSeat,        struct wl_seat *,                       VBCL_WAYLAND_SEAT_VERSION_MIN)
    else VBCL_WAYLAND_REGISTRY_ADD_MATCH(pRegistry, sIface, uName, zwlr_data_control_manager_v1_interface,  pCtx->pDataControlManager,  struct zwlr_data_control_manager_v1 *,  VBCL_WAYLAND_ZWLR_DATA_CONTROL_MANAGER_VERSION_MIN)
    else
        VBClLogVerbose(5, "ignoring Wayland interface %s\n", sIface);
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
static void vbcl_wayland_hlp_dcp_registry_global_remove_handler(
    void *pvUser, struct wl_registry *pRegistry, uint32_t uName)
{
    RT_NOREF(pvUser);
    RT_NOREF(pRegistry);
    RT_NOREF(uName);
}

/** Wayland global registry callbacks. */
static const struct wl_registry_listener g_vbcl_wayland_hlp_registry_cb =
{
    &vbcl_wayland_hlp_dcp_registry_global_handler,       /* .global */
    &vbcl_wayland_hlp_dcp_registry_global_remove_handler /* .global_remove */
};


/*********************************************************************************************************************************
*   Wayland Data Control Offer Listener Callbacks (zwlr_data_control_offer_v1_listener)                                          *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNVBCLWAYLANDSESSIONJOIN,
 *      Session callback: Collect clipboard format advertised by guest.}
 *
 * This callback must be executed in context of Wayland event thread
 * in order to be able to access Wayland clipboard content.
 *
 * This callback adds MIME type just advertised by Wayland into a list
 * of MIME types which in turn later will be advertised to the host.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_gh_add_fmt_cb(void *pvUser)
{
    struct vbcl_wl_dcp_enumerate_ctx * const pEnmCtx = (struct vbcl_wl_dcp_enumerate_ctx *)pvUser;
    AssertPtrReturn(pEnmCtx, VERR_INVALID_PARAMETER);
    VBClLogVerbose(3, "vbcl_wayland_hlp_edcp_gh_add_fmt_cb: %s\n", pEnmCtx->pcszMimeType);
    return vbcl_wayland_xdcp_add_fmt(pEnmCtx);
}

/**
 * Wayland callback: Data Control Offer advertise.
 *
 * Triggered when other Wayland client advertises new clipboard content.
 *
 * @param pvUser            Context data.
 * @param pOffer            Wayland Data Control Offer object.
 * @param pcszMimeType      MIME type of newly available clipboard data.
 */
static void vbcl_wayland_hlp_dcp_data_control_offer_offer(void *pvUser, struct zwlr_data_control_offer_v1 *pOffer,
                                                          const char *pcszMimeType)
{
    vbox_wl_dcp_ctx_t *pCtx = (vbox_wl_dcp_ctx_t *)pvUser;
    VBClLogVerbose(3, "%s: %s\n", __func__, pcszMimeType);
    RT_NOREF(pOffer);

    AssertPtrReturnVoid(pcszMimeType);

    struct vbcl_wl_dcp_enumerate_ctx EnmCtx;
    EnmCtx.pcszMimeType = pcszMimeType;
    EnmCtx.pSession = &pCtx->BaseCtx.Session;

    int rc = VBClWaylandSessionJoin(&pCtx->BaseCtx.Session.Base, VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST,
                                    vbcl_wayland_hlp_dcp_gh_add_fmt_cb, &EnmCtx);
    if (RT_FAILURE(rc))
        VBClLogError("cannot save formats announced by the guest, rc=%Rrc\n", rc);
}

/** Wayland Data Control Offer interface callbacks. */
static const struct zwlr_data_control_offer_v1_listener g_data_control_offer_listener =
{
    &vbcl_wayland_hlp_dcp_data_control_offer_offer,
};


/**********************************************************************************************************************************
 * Wayland Data Control Device callbacks.
 *********************************************************************************************************************************/


/**
 * Read clipboard buffer from Wayland in specified format.
 *
 * @returns IPRT status code.
 * @param   pCtx            DCP context data.
 * @param   pOffer          Data offer object.
 * @param   pszMimeType     Requested MIME type in string representation.
 */
static int vbcl_wayland_hlp_dcp_receive_offer(
    vbox_wl_dcp_ctx_t *pCtx, zwlr_data_control_offer_v1 *pOffer, char *pszMimeType)
{
    int rc = VERR_PIPE_NOT_CONNECTED;
    int aFds[2];

    AssertPtrReturn(pOffer, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszMimeType, VERR_INVALID_PARAMETER);

    if (pipe(aFds) == 0)
    {
        zwlr_data_control_offer_v1_receive(pOffer, pszMimeType, aFds[1]);

        close(aFds[1]);
        rc = vbcl_wayland_xdcp_get_guest_clipboard(aFds[0], &pCtx->BaseCtx, pszMimeType);
        close(aFds[0]);
    }
    else
        VBClLogError("cannot read MIME type '%s' from Wayland, rc=%Rrc\n", pszMimeType, rc);

    return rc;
}

/**
 * Convert list of MIME types to mask of VBox formats.
 *
 * @returns VBox formats bitmask.
 * @param   pListHead   List of MIME types in string representation.
 * @param   pOffer      Wayland offer.
 */
static SHCLFORMATS vbcl_wayland_hlp_dcp_match_formats(PRTLISTANCHOR pListHead, struct zwlr_data_control_offer_v1 *pOffer)
{
    SHCLFORMATS fFmts = VBOX_SHCL_FMT_NONE;

    AssertPtrReturn(pListHead, VBOX_SHCL_FMT_NONE);
    AssertPtrReturn(pOffer, VBOX_SHCL_FMT_NONE);

    if (!RTListIsEmpty(pListHead))
    {
        vbox_wl_dcp_mime_t *pEntry;
        RTListForEach(pListHead, pEntry, vbox_wl_dcp_mime_t, Node)
        {
            VBClLogVerbose(5, "Wayland last offer contains data in format: %s\n", pEntry->szMimeType);

            fFmts |= VbghMimeConvGetVBoxFormatByMime(pEntry->szMimeType, NULL /*pfFlagsAndPriority*/);

            /*int rc =*/ vbcl_wayland_hlp_dcp_receive_offer(&g_DcpCtx, pOffer, pEntry->szMimeType);
        }
    }

    return fFmts;
}

/**
 * @callback_method_impl{FNVBCLWAYLANDSESSIONJOIN,
 *      Session callback: Advertise clipboard to the host.}
 *
 * This callback must be executed in context of Wayland event thread
 * in order to be able to access Wayland clipboard content.
 *
 * This callback (1) coverts Wayland clipboard formats into VBox
 * representation, (2) sets formats to the session, (3) waits for
 * host to request clipboard data in certain format, and (4)
 * receives Wayland clipboard in requested format.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_gh_clip_report_cb(void *pvUser)
{
    struct zwlr_data_control_offer_v1 *pOffer = (struct zwlr_data_control_offer_v1 *)pvUser;
    AssertPtrReturn(pOffer, VERR_INVALID_PARAMETER);
    VBCL_LOG_CALLBACK;

    int rc;
    SHCLFORMATS fFmts = vbcl_wayland_hlp_dcp_match_formats(&g_DcpCtx.BaseCtx.Session.clip.mimeTypesList, pOffer);
    if (fFmts != VBOX_SHCL_FMT_NONE)
    {
        g_DcpCtx.BaseCtx.Session.clip.fFmts.set(fFmts);

        if (RT_VALID_PTR(g_DcpCtx.BaseCtx.pClipboardCtx))
        {
            rc = VbglR3ClipboardReportFormats(g_DcpCtx.BaseCtx.pClipboardCtx->idClient, fFmts);
            if (RT_FAILURE(rc))
                VBClLogError("cannot report formats to host, rc=%Rrc\n", rc);
        }
        else
        {
            VBClLogVerbose(2, "cannot announce to guest, no host service connection yet\n");
            rc = VERR_TRY_AGAIN;
        }
    }
    else
        rc = VERR_NO_DATA;

    zwlr_data_control_offer_v1_destroy(pOffer);

    VBClLogVerbose(5, "announcing fFmts=0x%x to host, rc=%Rrc\n", fFmts, rc);
    return rc;
}

/**
 * Data Control Device offer callback.
 *
 * Triggered when other Wayland client advertises new clipboard content.
 * When this callback is triggered, a new zwlr_data_control_offer_v1 object
 * is created. This callback should setup listener callbacks for this object.
 *
 * @param pvUser            Context data.
 * @param pDevice           Wayland Data Control Device object.
 * @param pOffer            Wayland Data Control Offer object.
 */
static void vbcl_wayland_hlp_dcp_data_device_data_offer(
    void *pvUser, struct zwlr_data_control_device_v1 *pDevice, struct zwlr_data_control_offer_v1 *pOffer)
{
    int rc;
    vbox_wl_dcp_ctx_t *pCtx = (vbox_wl_dcp_ctx_t *)pvUser;
    RT_NOREF(pDevice);

    VBCL_LOG_CALLBACK;

    AssertPtrReturnVoid(pCtx);
    AssertPtrReturnVoid(pOffer);

    if (pCtx->BaseCtx.fIngnoreWlClipIn)
    {
        VBClLogVerbose(5, "ignoring Wayland clipboard data offer, we advertising new clipboard ourselves\n");
        return;
    }

    rc = vbcl_wayland_session_end(&pCtx->BaseCtx.Session.Base, NULL, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = vbcl_wayland_session_start(&pCtx->BaseCtx.Session.Base,
                                        VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST,
                                        &vbcl_wayland_hlp_dcp_session_start_generic_cb,
                                        &pCtx->BaseCtx.Session);
        if (RT_SUCCESS(rc))
        {
            rc = VbghMimeConvCacheClear(pCtx->BaseCtx.hCache);
            if (RT_FAILURE(rc))
                VBClLogVerbose(5, "unable to clear clipboard cache, rc=%Rrc", rc);

            zwlr_data_control_offer_v1_add_listener(pOffer, &g_data_control_offer_listener, pvUser);

            /* Receive all the advertised mime types. */
            wl_display_roundtrip(pCtx->BaseCtx.pDisplay);

            /* Try to send an announcement to the host. */
            VBClWaylandSessionJoin(&pCtx->BaseCtx.Session.Base, VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST,
                                   vbcl_wayland_hlp_dcp_gh_clip_report_cb, pOffer);
        }
        else
            VBClLogError("unable to start session, rc=%Rrc\n", rc);
    }
    else
        VBClLogError("unable to start session, previous session is still running, rc=%Rrc\n", rc);
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
static void vbcl_wayland_hlp_dcp_data_device_selection(
    void *pvUser, struct zwlr_data_control_device_v1 *pDevice, struct zwlr_data_control_offer_v1 *pOffer)
{
    RT_NOREF(pDevice, pvUser, pOffer);
    VBCL_LOG_CALLBACK;
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
static void vbcl_wayland_hlp_dcp_data_device_finished(
    void *pvUser, struct zwlr_data_control_device_v1 *pDevice)
{
    vbox_wl_dcp_ctx_t *pCtx = (vbox_wl_dcp_ctx_t *)pvUser;

    VBCL_LOG_CALLBACK;

    AssertPtrReturnVoid(pCtx);
    AssertPtrReturnVoid(pDevice);

    zwlr_data_control_device_v1_destroy(pDevice);
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
static void vbcl_wayland_hlp_dcp_data_device_primary_selection(
    void *pvUser, struct zwlr_data_control_device_v1 *pDevice, struct zwlr_data_control_offer_v1 *pOffer)
{
    RT_NOREF(pDevice, pvUser, pOffer);
    VBCL_LOG_CALLBACK;
}


/** Data Control Device interface callbacks. */
static const struct zwlr_data_control_device_v1_listener g_data_device_listener =
{
    &vbcl_wayland_hlp_dcp_data_device_data_offer,
    &vbcl_wayland_hlp_dcp_data_device_selection,
    &vbcl_wayland_hlp_dcp_data_device_finished,
    &vbcl_wayland_hlp_dcp_data_device_primary_selection,
};


/**********************************************************************************************************************************
 * Wayland Data Control Source callbacks.
 *********************************************************************************************************************************/

/**
 * @callback_method_impl{FNVBCLWAYLANDSESSIONJOIN,
 *      Session callback: Copy clipboard to the guest.
 *
 * Worker for vbcl_wayland_hlp_dcp_data_source_send().
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
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_clip_hg_report_join3_cb(void *pvUser)
{
    struct vbcl_wl_dcp_write_ctx * const pArgs = (struct vbcl_wl_dcp_write_ctx *)pvUser;
    AssertPtrReturn(pArgs, VERR_INVALID_PARAMETER);

    int rc = vbcl_wayland_xdcp_set_guest_clipboard(pArgs->fd, &g_DcpCtx.BaseCtx, pArgs->pcszMimeType);
    g_DcpCtx.BaseCtx.fIngnoreWlClipIn = false;
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
static void vbcl_wayland_hlp_dcp_data_source_send(void *pvUser, struct zwlr_data_control_source_v1 *pDataSource,
                                                  const char *pcszMimeType, int32_t fd)
{
    /*
     * Validate input.
     */
    vbox_wl_dcp_ctx_t * const pCtx = (vbox_wl_dcp_ctx_t *)pvUser;
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
                                    vbcl_wayland_hlp_dcp_clip_hg_report_join3_cb, &Args);

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
static void vbcl_wayland_hlp_dcp_data_source_cancelled(
    void *pvData, struct zwlr_data_control_source_v1 *pDataSource)
{
    RT_NOREF(pvData);
    VBCL_LOG_CALLBACK;
    zwlr_data_control_source_v1_destroy(pDataSource);
}


/** Wayland Data Control Source interface callbacks. */
static const struct zwlr_data_control_source_v1_listener g_data_source_listener =
{
    &vbcl_wayland_hlp_dcp_data_source_send,
    &vbcl_wayland_hlp_dcp_data_source_cancelled,
};


/**********************************************************************************************************************************
 * Helper specific code and session callbacks.
 *********************************************************************************************************************************/


/**
 * Setup or reset helper context.
 *
 * This function is used on helper init and termination. In case of
 * init, memory is not initialized yet, so it needs to be zeroed.
 * In case of shutdown, memory is already initialized and previously
 * allocated resources must be freed.
 *
 * @param pCtx              Context data.
 * @param fShutdown         A flag to indicate if session resources
 *                          need to be deallocated.
 */
static void vbcl_wayland_hlp_dcp_reset_ctx(vbox_wl_dcp_ctx_t *pCtx, bool fShutdown)
{
    AssertReturnVoid(pCtx);

    vbcl_wayland_xdcp_reset_ctx(&pCtx->BaseCtx, fShutdown);

    pCtx->pDataDevice = NULL;
    pCtx->pDataControlManager = NULL;
}

/**
 * Disconnect from Wayland compositor.
 *
 * Close connection, release resources and reset context data.
 *
 * @param pCtx              Context data.
 */
static void vbcl_wayland_hlp_dcp_disconnect(vbox_wl_dcp_ctx_t *pCtx)
{
    AssertReturnVoid(pCtx);

    if (RT_VALID_PTR(pCtx->pDataControlManager))
        zwlr_data_control_manager_v1_destroy(pCtx->pDataControlManager);

    if (RT_VALID_PTR(pCtx->pDataDevice))
        zwlr_data_control_device_v1_destroy(pCtx->pDataDevice);

    if (RT_VALID_PTR(pCtx->BaseCtx.pSeat))
        wl_seat_destroy(pCtx->BaseCtx.pSeat);

    if (RT_VALID_PTR(pCtx->BaseCtx.pRegistry))
        wl_registry_destroy(pCtx->BaseCtx.pRegistry);

    if (RT_VALID_PTR(pCtx->BaseCtx.pDisplay))
        wl_display_disconnect(pCtx->BaseCtx.pDisplay);

    vbcl_wayland_hlp_dcp_reset_ctx(pCtx, true);
}

/**
 * Connect to Wayland compositor.
 *
 * Establish connection, bind to all required interfaces.
 *
 * @returns TRUE on success, FALSE otherwise.
 * @param   pCtx                Context data.
 */
static bool vbcl_wayland_hlp_dcp_connect(vbox_wl_dcp_ctx_t *pCtx)
{
    const char *csWaylandDisplay = RTEnvGet(VBCL_ENV_WAYLAND_DISPLAY);
    bool fConnected = false;

    AssertPtrReturn(pCtx, false);

    if (RT_VALID_PTR(csWaylandDisplay))
        pCtx->BaseCtx.pDisplay = wl_display_connect(csWaylandDisplay);
    else
        VBClLogError("cannot connect to Wayland compositor "
                     VBCL_ENV_WAYLAND_DISPLAY " environment variable not set\n");

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
                    pCtx->pDataDevice = zwlr_data_control_manager_v1_get_data_device(pCtx->pDataControlManager, pCtx->BaseCtx.pSeat);
                    if (RT_VALID_PTR(pCtx->pDataDevice))
                    {
                        if (RT_VALID_PTR(pCtx->pDataControlManager))
                            fConnected = true;
                        else
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

    if (!fConnected)
        vbcl_wayland_hlp_dcp_disconnect(pCtx);

    return fConnected;
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
vbcl_wayland_hlp_dcp_send_offers(const char *pcszMimeType, uint32_t fFlagsAndPriority, void *pvUser)
{
    zwlr_data_control_source_v1 *pDataSource = (zwlr_data_control_source_v1 *)pvUser;
    zwlr_data_control_source_v1_offer(pDataSource, pcszMimeType);
    RT_NOREF(fFlagsAndPriority);
}

/**
 * @callback_method_impl{FNVBCLWAYLANDSESSIONJOIN,
 *      Session callback: Advertise clipboard to the guest.
 *
 * This callback must be executed in context of Wayland event thread
 * in order to be able to inject clipboard content into Wayland.
 *
 * This callback (1) prevents Wayland event loop from processing
 * incoming clipboard advertisements before sending any data to
 * other Wayland clients (this is needed in order to avoid feedback
 * loop from our own advertisements), (2) waits for the list of clipboard
 * formats available on the host side (set by vbcl_wayland_hlp_dcp_clip_hg_report_join_cb),
 * and (3) sends data offers for available host clipboard to other clients.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_clip_hg_report_join2_cb(void *pvUser)
{
    VBClLogVerbose(3, "%s\n", __func__);
    RT_NOREF(pvUser);

    g_DcpCtx.BaseCtx.fIngnoreWlClipIn = true;

    int rc;
    SHCLFORMATS const fFmts = g_DcpCtx.BaseCtx.Session.clip.fFmts.wait();
    VBClLogVerbose(3, "%s: fFmts=%#x\n", __func__, fFmts);
    if (fFmts != g_DcpCtx.BaseCtx.Session.clip.fFmts.defaults())
    {
        zwlr_data_control_source_v1 *pDataSource
            = zwlr_data_control_manager_v1_create_data_source(g_DcpCtx.pDataControlManager);
        if (RT_VALID_PTR(pDataSource))
        {
            zwlr_data_control_source_v1_add_listener(pDataSource, &g_data_source_listener, &g_DcpCtx);

            VbghMimeConvEnumerateByVBoxFormats(fFmts, vbcl_wayland_hlp_dcp_send_offers, pDataSource);

            zwlr_data_control_device_v1_set_selection(g_DcpCtx.pDataDevice, pDataSource);
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
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_event_loop(RTTHREAD ThreadSelf, void *pvUser)
{
    vbox_wl_dcp_ctx_t * const pCtx = (vbox_wl_dcp_ctx_t *)pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_PARAMETER);

    int rc = VERR_TRY_AGAIN;
    if (vbcl_wayland_hlp_dcp_connect(pCtx))
    {
        /* Start listening Data Control Device interface. */
        if (zwlr_data_control_device_v1_add_listener(pCtx->pDataDevice, &g_data_device_listener, (void *)pCtx) == 0)
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
                                           vbcl_wayland_hlp_dcp_clip_hg_report_join2_cb, NULL);
            }

            rc = VINF_SUCCESS;
        }
        else
        {
            VBClLogError("cannot subscribe to Data Control Device events\n");
            rc = VERR_NOT_SUPPORTED;
        }

        vbcl_wayland_hlp_dcp_disconnect(pCtx);
    }
    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER,pfnProbe}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_probe(void)
{
    vbox_wl_dcp_ctx_t probeCtx;
    int fCaps = VBOX_WAYLAND_HELPER_CAP_NONE;
    VBGHDISPLAYSERVERTYPE enmDisplayServerType = VBGHDisplayServerTypeDetect();

    vbcl_wayland_hlp_dcp_reset_ctx(&probeCtx, false /* fShutdown */);
    vbcl_wayland_session_init(&probeCtx.BaseCtx.Session.Base);

    if (VBGHDisplayServerTypeIsWaylandAvailable(enmDisplayServerType))
    {
        if (vbcl_wayland_hlp_dcp_connect(&probeCtx))
        {
            fCaps |= VBOX_WAYLAND_HELPER_CAP_CLIPBOARD;
            vbcl_wayland_hlp_dcp_disconnect(&probeCtx);
        }
    }

    return fCaps;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnInit}
 */
RTDECL(int) vbcl_wayland_hlp_dcp_clip_init(void)
{
    g_DcpCtx.BaseCtx.hCache = NIL_VBGHMIMECONVCACHE;
    vbcl_wayland_hlp_dcp_reset_ctx(&g_DcpCtx, false /* fShutdown */);
    vbcl_wayland_session_init(&g_DcpCtx.BaseCtx.Session.Base);

    return vbcl_wayland_thread_start(&g_DcpCtx.BaseCtx.Thread, vbcl_wayland_hlp_dcp_event_loop, "wl-dcp", &g_DcpCtx);
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnTerm}
 */
RTDECL(int) vbcl_wayland_hlp_dcp_clip_term(void)
{
    /* Set termination flag. Wayland event loop should pick it up
       on the next iteration. */
    g_DcpCtx.BaseCtx.fShutdown = true;

    /* Wait for Wayland event loop thread to shutdown. */
    int rcThread = VINF_SUCCESS;
    int rc = RTThreadWait(g_DcpCtx.BaseCtx.Thread, RT_MS_30SEC, &rcThread);
    if (RT_SUCCESS(rc))
        VBClLogInfo("Wayland event thread exited with status, rc=%Rrc\n", rcThread);
    else
        VBClLogError("unable to stop Wayland event thread, rc=%Rrc\n", rc);

    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnSetClipboardCtx}
 */
static DECLCALLBACK(void) vbcl_wayland_hlp_dcp_clip_set_ctx(PVBGLR3SHCLCMDCTX pCtx)
{
    g_DcpCtx.BaseCtx.pClipboardCtx = pCtx;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnPopup}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_clip_popup(void)
{
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNVBCLWAYLANDSESSIONJOIN,
 *      Session callback: Copy clipboard from the host.
 *
 * This callback (1) sets host clipboard formats list to the session,
 * (2) asks Wayland event thread to advertise these formats to the guest,
 * (3) waits for guest to request clipboard in specific format, (4) read
 * host clipboard in this format, and (5) sets clipboard data to the session,
 * so Wayland events thread can inject it into the guest.
 *
 * This callback should not return until clipboard data is read from
 * the host or error occurred. It must block host events loop until
 * current host event is fully processed.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_clip_hg_report_join_cb(void *pvUser)
{
    AssertPtrReturn(pvUser, VERR_INVALID_PARAMETER);
    SHCLFORMATS const fFmts = *(SHCLFORMATS const *)pvUser;
    VBClLogVerbose(3, "%s: %#x\n", __func__, fFmts);
    return vbcl_wayland_xdcp_get_host_clipboard(&g_DcpCtx.BaseCtx, fFmts);
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnHGClipReport}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_clip_hg_report(SHCLFORMATS fFormats)
{
    VBClLogVerbose(3, "%s: %#x\n", __func__, fFormats);

    int rc;
    if (fFormats != VBOX_SHCL_FMT_NONE)
    {
        rc = vbcl_wayland_session_end(&g_DcpCtx.BaseCtx.Session.Base, NULL, NULL);
        if (RT_SUCCESS(rc))
        {
            rc = vbcl_wayland_session_start(&g_DcpCtx.BaseCtx.Session.Base,
                                            VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST,
                                            &vbcl_wayland_hlp_dcp_session_start_generic_cb,
                                            &g_DcpCtx.BaseCtx.Session);

            if (RT_SUCCESS(rc))
                rc = VBClWaylandSessionJoin(&g_DcpCtx.BaseCtx.Session.Base, VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST,
                                            vbcl_wayland_hlp_dcp_clip_hg_report_join_cb, &fFormats);
        }
        else
            VBClLogError("unable to start session, previous session is still running rc=%Rrc\n", rc);
    }
    else
    {
        /** @todo r=bird: if the current session is copy-to-copy, end it. Can we tell
         *        the rest of wayland that we no longer have any clipboard data?  */
        rc = VERR_NO_DATA;
    }
    return rc;
}

/**
 * @callback_method_impl{FNVBCLWAYLANDSESSIONJOIN,
 *      Session callback: Copy clipboard to the host.}
 *
 * This callback sets clipboard format to the session as requested
 * by host, waits for guest clipboard data in requested format and
 * sends data to the host.
 *
 * This callback should not return until clipboard data is sent to
 * the host or error occurred. It must block host events loop until
 * current host event is fully processed.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_clip_gh_read_join_cb(void *pvUser)
{
    AssertPtr(pvUser);
    SHCLFORMAT const uFmt = *(SHCLFORMAT *)pvUser;
    VBClLogVerbose(3, "%s: %#x\n", __func__, uFmt);
    return vbcl_wayland_xdcp_set_host_clipboard(&g_DcpCtx.BaseCtx, uFmt);
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnGHClipRead}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_dcp_clip_gh_read(SHCLFORMAT uFmt)
{
    VBClLogVerbose(3, "%s: uFmt=%#x\n", __func__, uFmt);
    return VBClWaylandSessionJoin(&g_DcpCtx.BaseCtx.Session.Base, VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST,
                                  vbcl_wayland_hlp_dcp_clip_gh_read_join_cb, &uFmt);
}

/** Legacy data control protocol helper callbacks. */
const VBCLWAYLANDHELPER g_WaylandHelperDcp =
{
    /* .pszName  = */ "wayland-dcp-legacy",
    /* .pfnProbe = */ vbcl_wayland_hlp_dcp_probe,
    /* .clip     = */
    {
        /* .pfnInit            = */ vbcl_wayland_hlp_dcp_clip_init,
        /* .pfnTerm            = */ vbcl_wayland_hlp_dcp_clip_term,
        /* .pfnSetClipboardCtx = */ vbcl_wayland_hlp_dcp_clip_set_ctx,
        /* .pfnPopup           = */ vbcl_wayland_hlp_dcp_clip_popup,
        /* .pfnHGClipReport    = */ vbcl_wayland_hlp_dcp_clip_hg_report,
        /* .pfnGHClipRead      = */ vbcl_wayland_hlp_dcp_clip_gh_read,
    },
    /* .dnd      = */
    {
        /* .pfnInit = */            NULL,
        /* .pfnTerm = */            NULL,
    },
};
