/* $Id: wayland-helper-gtk.cpp 106821 2024-10-31 14:20:11Z vadim.galitsyn@oracle.com $ */
/** @file
 * Guest Additions - Gtk helper for Wayland.
 *
 * This module implements Shared Clipboard and Drag-n-Drop
 * support for Wayland guests using Gtk library.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#include <iprt/localipc.h>
#include <iprt/rand.h>
#include <iprt/semaphore.h>

#include <VBox/GuestHost/DisplayServerType.h>
#include <VBox/GuestHost/clipboard-helper.h>
#include <VBox/GuestHost/mime-type-converter.h>

#include "VBoxClient.h"
#include "clipboard.h"
#include "wayland-helper.h"
#include "wayland-helper-ipc.h"

#include "vbox-gtk.h"

/** Gtk session data.
 *
 * A structure which accumulates all the necessary data required to
 * maintain session between host and Wayland for clipboard sharing
 * and drag-n-drop.*/
typedef struct
{
    /* Generic VBoxClient Wayland session data (synchronization point). */
    vbcl_wl_session_t                       Base;
    /** Randomly generated session ID, should be used by
     *  both VBoxClient and vboxwl tool. */
    uint32_t                                uSessionId;
    /** IPC connection flow control between VBoxClient and vboxwl tool. */
    vbcl::ipc::data::DataIpc                *oDataIpc;
    /** IPC connection handle. */
    RTLOCALIPCSESSION                       hIpcSession;
    /** Popup window process handle. */
    RTPROCESS                               popupProc;
} vbox_wl_gtk_ipc_session_t;

/**
 * A set of objects required to handle clipboard sharing over
 * and drag-n-drop using Gtk library. */
typedef struct
{
    /** Wayland event loop thread. */
    RTTHREAD                                Thread;

    /** A flag which indicates that Wayland event loop should terminate. */
    volatile bool                           fShutdown;

    /** Communication session between host event loop and Wayland. */
    vbox_wl_gtk_ipc_session_t               Session;

    /** Connection to the host clipboard service. */
    PVBGLR3SHCLCMDCTX                       pClipboardCtx;

    /** Local IPC server object. */
    RTLOCALIPCSERVER                        hIpcServer;

    /** IPC server socket name prefix. */
    const char                              *pcszIpcSockPrefix;
} vbox_wl_gtk_ctx_t;

/** Private data for a callback when host reports clipboard formats. */
struct vbcl_wayland_hlp_gtk_clip_hg_report_priv
{
    /** Helper context. */
    vbox_wl_gtk_ctx_t *pCtx;
    /** Clipboard formats. */
    SHCLFORMATS fFormats;
};

/** Private data for a callback when host requests clipboard
 *  data in specified format. */
struct vbcl_wayland_hlp_gtk_clip_gh_read_priv
{
    /** Helper context. */
    vbox_wl_gtk_ctx_t *pCtx;
    /** Clipboard format. */
    SHCLFORMAT uFormat;
};

/** Helper context. */
static vbox_wl_gtk_ctx_t g_GtkClipCtx;

/**
 * Start popup process.
 *
 * @returns IPRT status code.
 * @param   pSession    Session data.
 */
static int vbcl_wayland_hlp_gtk_session_popup(vbox_wl_gtk_ipc_session_t *pSession)
{
    int rc = VINF_SUCCESS;

    /* Make sure valid session is in progress. */
    AssertReturn(pSession->uSessionId > 0, VERR_INVALID_PARAMETER);

    char *pszSessionId = RTStrAPrintf2("%u", pSession->uSessionId);
    if (RT_VALID_PTR(pszSessionId))
    {
        /* List of vboxwl command line arguments.*/
        const char *apszArgs[] =
        {
            VBOXWL_PATH,
            NULL,
            VBOXWL_ARG_SESSION_ID,
            pszSessionId,
            NULL,
            NULL
        };

        /* Log verbosity level to be passed to vboxwl. */
        char pszVerobsity[  VBOXWL_VERBOSITY_MAX
                          + 2 /* add space for '-' and '\0' */];
        RT_ZERO(pszVerobsity);

        /* Select vboxwl action depending on session type. */
        if      (pSession->Base.enmType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST)
            apszArgs[1] = VBOXWL_ARG_CLIP_HG_COPY;
        else if (pSession->Base.enmType == VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST)
            apszArgs[1] = VBOXWL_ARG_CLIP_GH_ANNOUNCE;
        else if (pSession->Base.enmType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST)
            apszArgs[1] = VBOXWL_ARG_CLIP_GH_COPY;
        else
            rc = VERR_INVALID_PARAMETER;

        /* Once VBoxClient was started with log verbosity level, pass the
         * same verbosity level to vboxwl as well. */
        if (   RT_SUCCESS(rc)
            && g_cVerbosity > 0)
        {
            pszVerobsity[0] = '-';

            memset(&pszVerobsity[1], 'v',
                   RT_MIN(g_cVerbosity, VBOXWL_VERBOSITY_MAX));

            /* Insert verbosity level into the rest of vboxwl
             * command line arguments. */
            apszArgs[4] = pszVerobsity;
        }

        /* Run vboxwl in background. */
        if (RT_SUCCESS(rc))
            rc = RTProcCreate(VBOXWL_PATH,
                              apszArgs, RTENV_DEFAULT,
                              RTPROC_FLAGS_SEARCH_PATH, &pSession->popupProc);

        VBClLogVerbose(2, "start '%s' command [sid=%u]: rc=%Rrc\n",
                       VBOXWL_PATH, pSession->uSessionId, rc);

        RTStrFree(pszSessionId);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/**
 * Prepare new session and start popup process.
 *
 * @returns IPRT status code.
 * @param   pSession        Session data.
 */
static int vbcl_wayland_hlp_gtk_session_prepare(vbox_wl_gtk_ipc_session_t *pSession)
{
    int rc = VINF_SUCCESS;

    /* Make sure there is no leftovers from previous session. */
    Assert(pSession->uSessionId == 0);

    /* Initialize session. */
    pSession->uSessionId = RTRandU32Ex(1, 0xFFFFFFFF);

    pSession->oDataIpc = new vbcl::ipc::data::DataIpc();
    if (RT_VALID_PTR(pSession->oDataIpc))
    {
        pSession->oDataIpc->init(vbcl::ipc::FLOW_DIRECTION_SERVER,
                                      pSession->uSessionId);
    }
    else
        rc = VERR_NO_MEMORY;

    /* Start helper tool. */
    if (RT_SUCCESS(rc))
    {
        rc = vbcl_wayland_hlp_gtk_session_popup(pSession);
        VBClLogVerbose(1, "session id=%u: started: rc=%Rrc\n",
                       pSession->uSessionId, rc);
    }

    return rc;
}

/**
 * Session callback: Generic session initializer.
 *
 * This callback starts new session.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type (unused).
 * @param   pvUser              User data.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_session_start_generic_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    VBCL_LOG_CALLBACK;

    vbox_wl_gtk_ctx_t *pCtx = (vbox_wl_gtk_ctx_t *)pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    RT_NOREF(enmSessionType);

    return vbcl_wayland_hlp_gtk_session_prepare(&pCtx->Session);
}

/**
 * Reset session, terminate popup process and free allocated resources.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type (unused).
 * @param   pvUser              User data.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_session_end_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    vbox_wl_gtk_ipc_session_t *pSession = (vbox_wl_gtk_ipc_session_t *)pvUser;
    AssertPtrReturn(pSession, VERR_INVALID_PARAMETER);

    int rc;

    RT_NOREF(enmSessionType);

    /* Make sure valid session is in progress. */
    AssertReturn(pSession->uSessionId > 0, VERR_INVALID_PARAMETER);

    rc = RTProcWait(pSession->popupProc, RTPROCWAIT_FLAGS_BLOCK, NULL);
    if (RT_FAILURE(rc))
        rc = RTProcTerminate(pSession->popupProc);
    if (RT_FAILURE(rc))
    {
        VBClLogError("session %u: unable to stop popup window process: rc=%Rrc\n",
                     pSession->uSessionId, rc);
    }

    if (RT_SUCCESS(rc))
    {
        pSession->uSessionId = 0;

        pSession->oDataIpc->reset();
        delete pSession->oDataIpc;
    }

    return rc;
}

/**
 * Session callback: Handle sessions started by host events.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type, must be verified as
 *                              a consistency check.
 * @param   pvUser              User data (IPC connection handle
 *                              to vboxwl tool).
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_worker_join_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    vbox_wl_gtk_ctx_t *pCtx = (vbox_wl_gtk_ctx_t *)pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    const vbcl::ipc::flow_t *pFlow;

    int rc = VINF_SUCCESS;

    VBCL_LOG_CALLBACK;

    /* Make sure valid session is in progress. */
    AssertReturn(pCtx->Session.uSessionId > 0, VERR_INVALID_PARAMETER);

    /* Select corresponding IPC flow depending on session type. */
    if      (enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST)
        pFlow = vbcl::ipc::data::HGCopyFlow;
    else if (enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST)
        pFlow = vbcl::ipc::data::GHAnnounceAndCopyFlow;
    else if (enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST)
        pFlow = vbcl::ipc::data::GHCopyFlow;
    else
    {
        pFlow = NULL;
        rc = VERR_INVALID_PARAMETER;
    }

    /* Proceed with selected flow. */
    if (RT_VALID_PTR(pFlow))
        rc = pCtx->Session.oDataIpc->flow(pFlow, pCtx->Session.hIpcSession);

    return rc;
}

/**
 * IPC server thread worker.
 *
 * @returns IPRT status code.
 * @param   hThreadSelf     IPRT thread handle.
 * @param   pvUser          Helper context data.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_worker(RTTHREAD hThreadSelf, void *pvUser)
{
    int rc;

    vbox_wl_gtk_ctx_t *pCtx = (vbox_wl_gtk_ctx_t *)pvUser;
    char szIpcServerName[128];

    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pCtx->pcszIpcSockPrefix, VERR_INVALID_POINTER);

    RTThreadUserSignal(hThreadSelf);

    VBClLogVerbose(1, "starting IPC\n");

    rc = vbcl_wayland_hlp_gtk_ipc_srv_name(pCtx->pcszIpcSockPrefix, szIpcServerName, sizeof(szIpcServerName));

    if (RT_SUCCESS(rc))
        rc = RTLocalIpcServerCreate(&pCtx->hIpcServer, szIpcServerName, 0);

    if (RT_SUCCESS(rc))
        rc = RTLocalIpcServerSetAccessMode(pCtx->hIpcServer, RTFS_UNIX_IRUSR | RTFS_UNIX_IWUSR);

    if (RT_SUCCESS(rc))
    {
        VBClLogVerbose(1, "started IPC server '%s'\n", szIpcServerName);

        vbcl_wayland_session_init(&pCtx->Session.Base);

        while (!ASMAtomicReadBool(&pCtx->fShutdown))
        {
            rc = RTLocalIpcServerListen(pCtx->hIpcServer, &pCtx->Session.hIpcSession);
            if (RT_SUCCESS(rc))
            {
                RTUID uUid;

                /* Authenticate remote user. Only allow connection from
                 * process who belongs to the same UID. */
                rc = RTLocalIpcSessionQueryUserId(pCtx->Session.hIpcSession, &uUid);
                if (RT_SUCCESS(rc))
                {
                    RTUID uLocalUID = geteuid();
                    if (   uLocalUID != 0
                        && uLocalUID == uUid)
                    {
                        VBClLogVerbose(1, "new IPC connection\n");

                        rc = vbcl_wayland_session_join(&pCtx->Session.Base,
                                                       &vbcl_wayland_hlp_gtk_worker_join_cb,
                                                       pCtx);

                        VBClLogVerbose(1, "IPC flow completed, rc=%Rrc\n", rc);

                        rc = vbcl_wayland_session_end(&pCtx->Session.Base,
                                                      &vbcl_wayland_hlp_gtk_session_end_cb,
                                                      &pCtx->Session);
                        VBClLogVerbose(1, "IPC session ended, rc=%Rrc\n", rc);

                    }
                    else
                        VBClLogError("incoming IPC connection rejected: UID mismatch: %d/%d\n",
                                     uLocalUID, uUid);
                }
                else
                    VBClLogError("failed to get remote IPC UID, rc=%Rrc\n", rc);

                RTLocalIpcSessionClose(pCtx->Session.hIpcSession);
            }
            else if (rc != VERR_CANCELLED)
                VBClLogVerbose(1, "IPC connection has failed, rc=%Rrc\n", rc);
        }

        rc = RTLocalIpcServerDestroy(pCtx->hIpcServer);
    }
    else
        VBClLogError("failed to start IPC, rc=%Rrc\n", rc);

    VBClLogVerbose(1, "IPC stopped\n");

    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER,pfnProbe}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_probe(void)
{
    int fCaps = VBOX_WAYLAND_HELPER_CAP_NONE;

    if (VBGHDisplayServerTypeIsGtkAvailable())
        fCaps |= VBOX_WAYLAND_HELPER_CAP_CLIPBOARD;

    return fCaps;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnInit}
 */
RTDECL(int) vbcl_wayland_hlp_gtk_clip_init(void)
{
    VBCL_LOG_CALLBACK;

    RT_ZERO(g_GtkClipCtx);

    /* Set IPC server socket name prefix before server is started. */
    g_GtkClipCtx.pcszIpcSockPrefix = VBOXWL_SRV_NAME_PREFIX_CLIP;

    return vbcl_wayland_thread_start(&g_GtkClipCtx.Thread, vbcl_wayland_hlp_gtk_worker, "wl-gtk-ipc", &g_GtkClipCtx);
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnTerm}
 */
RTDECL(int) vbcl_wayland_hlp_gtk_clip_term(void)
{
    int rc;
    int rcThread = 0;
    vbox_wl_gtk_ctx_t *pCtx = &g_GtkClipCtx;

    /* Set termination flag. */
    pCtx->fShutdown = true;

    /* Cancel IPC loop. */
    rc = RTLocalIpcServerCancel(pCtx->hIpcServer);
    if (RT_FAILURE(rc))
        VBClLogError("unable to notify IPC server about shutdown, rc=%Rrc\n", rc);

    if (RT_SUCCESS(rc))
    {
        /* Wait for Gtk event loop thread to shutdown. */
        rc = RTThreadWait(pCtx->Thread, RT_MS_30SEC, &rcThread);
        VBClLogInfo("gtk event thread exited with status, rc=%Rrc\n", rcThread);
    }
    else
        VBClLogError("unable to stop gtk thread, rc=%Rrc\n", rc);

    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnSetClipboardCtx}
 */
static DECLCALLBACK(void) vbcl_wayland_hlp_gtk_clip_set_ctx(PVBGLR3SHCLCMDCTX pCtx)
{
    g_GtkClipCtx.pClipboardCtx = pCtx;
}

/**
 * Session callback: Announce clipboard to the host.
 *
 * This callback (1) waits for the guest to report its clipboard content
 * via IPC connection from vboxwl tool, and (2) reports these formats
 * to the host.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type, must be verified as
 *                              a consistency check.
 * @param   pvUser              User data.
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_clip_popup_join_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    int rc = (enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST)
             ? VINF_SUCCESS : VERR_WRONG_ORDER;

    VBCL_LOG_CALLBACK;

    vbox_wl_gtk_ctx_t *pCtx = (vbox_wl_gtk_ctx_t *)pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    if (RT_SUCCESS(rc))
    {
        SHCLFORMATS fFmts = pCtx->Session.oDataIpc->m_fFmts.wait();
        if (fFmts !=  pCtx->Session.oDataIpc->m_fFmts.defaults())
            rc = VbglR3ClipboardReportFormats(pCtx->pClipboardCtx->idClient, fFmts);
        else
            rc = VERR_TIMEOUT;
    }

    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnPopup}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_clip_popup(void)
{
    int rc;
    vbox_wl_gtk_ctx_t *pCtx = &g_GtkClipCtx;

    VBCL_LOG_CALLBACK;

    rc = vbcl_wayland_session_start(&pCtx->Session.Base,
                                    VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST,
                                    &vbcl_wayland_hlp_gtk_session_start_generic_cb,
                                    pCtx);
    if (RT_SUCCESS(rc))
    {
        rc = vbcl_wayland_session_join(&pCtx->Session.Base,
                                       &vbcl_wayland_hlp_gtk_clip_popup_join_cb,
                                       pCtx);
    }

    return rc;
}

/**
 * Session callback: Copy clipboard from the host.
 *
 * This callback (1) sets host clipboard formats list to the session,
 * (2) waits for guest to request clipboard in specific format, (3) read
 * host clipboard in this format, and (4) sets clipboard data to the session,
 * so Gtk event thread can inject it into the guest.
 *
 * This callback should not return until clipboard data is read from
 * the host or error occurred. It must block host events loop until
 * current host event is fully processed.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type, must be verified as
 *                              a consistency check.
 * @param   pvUser              User data (host clipboard formats).
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_clip_hg_report_join_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    struct vbcl_wayland_hlp_gtk_clip_hg_report_priv *pPriv =
        (struct vbcl_wayland_hlp_gtk_clip_hg_report_priv *)pvUser;
    AssertPtrReturn(pPriv, VERR_INVALID_POINTER);

    SHCLFORMAT uFmt;

    int rc =   (enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST)
             ? VINF_SUCCESS : VERR_WRONG_ORDER;

    VBCL_LOG_CALLBACK;

    if (RT_SUCCESS(rc))
    {
        pPriv->pCtx->Session.oDataIpc->m_fFmts.set(pPriv->fFormats);

        uFmt = pPriv->pCtx->Session.oDataIpc->m_uFmt.wait();
        if (uFmt != pPriv->pCtx->Session.oDataIpc->m_uFmt.defaults())
        {
            void *pvData;
            uint32_t cbData;

            rc = VBClClipboardReadHostClipboard(pPriv->pCtx->pClipboardCtx, uFmt, &pvData, &cbData);
            if (RT_SUCCESS(rc))
            {
                pPriv->pCtx->Session.oDataIpc->m_pvDataBuf.set((uint64_t)pvData);
                pPriv->pCtx->Session.oDataIpc->m_cbDataBuf.set((uint64_t)cbData);
            }
        }
        else
            rc = VERR_TIMEOUT;
    }

    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnHGClipReport}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_clip_hg_report(SHCLFORMATS fFormats)
{
    int rc = VERR_NO_DATA;
    vbox_wl_gtk_ctx_t *pCtx = &g_GtkClipCtx;

    VBCL_LOG_CALLBACK;

    if (fFormats != VBOX_SHCL_FMT_NONE)
    {
        rc = vbcl_wayland_session_start(&pCtx->Session.Base,
                                        VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST,
                                        &vbcl_wayland_hlp_gtk_session_start_generic_cb,
                                        pCtx);
        if (RT_SUCCESS(rc))
        {
            struct vbcl_wayland_hlp_gtk_clip_hg_report_priv priv = { pCtx, fFormats };

            rc = vbcl_wayland_session_join(&pCtx->Session.Base,
                                           &vbcl_wayland_hlp_gtk_clip_hg_report_join_cb,
                                           &priv);
        }
    }

    return rc;
}

/**
 * Session callback: Copy clipboard to the host.
 *
 * This callback sets clipboard format to the session as requested
 * by host, waits for guest clipboard data in requested format and
 * sends data to the host.
 *
 * This callback should not return until clipboard data is sent to
 * the host or error occurred. It must block host events loop until
 * current host event is fully processed.
 *
 * @returns IPRT status code.
 * @param   enmSessionType      Session type, must be verified as
 *                              a consistency check.
 * @param   pvUser              User data (requested format).
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_clip_gh_read_join_cb(
    vbcl_wl_session_type_t enmSessionType, void *pvUser)
{
    struct vbcl_wayland_hlp_gtk_clip_gh_read_priv *pPriv =
        (struct vbcl_wayland_hlp_gtk_clip_gh_read_priv *)pvUser;
    AssertPtrReturn(pPriv, VERR_INVALID_POINTER);

    int rc = (   enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST
              || enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST)
             ? VINF_SUCCESS : VERR_WRONG_ORDER;

    VBCL_LOG_CALLBACK;

    if (RT_SUCCESS(rc))
    {
        void *pvData;
        uint32_t cbData;

        /* Store requested clipboard format to the session. */
        pPriv->pCtx->Session.oDataIpc->m_uFmt.set(pPriv->uFormat);

        /* Wait for data in requested format. */
        pvData = (void *)pPriv->pCtx->Session.oDataIpc->m_pvDataBuf.wait();
        cbData = pPriv->pCtx->Session.oDataIpc->m_cbDataBuf.wait();
        if (   cbData != pPriv->pCtx->Session.oDataIpc->m_cbDataBuf.defaults()
            && pvData != (void *)pPriv->pCtx->Session.oDataIpc->m_pvDataBuf.defaults())
        {
            /* Send clipboard data to the host. */
            rc = VbglR3ClipboardWriteDataEx(pPriv->pCtx->pClipboardCtx, pPriv->uFormat, pvData, cbData);
        }
        else
            rc = VERR_TIMEOUT;
    }

    return rc;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnGHClipRead}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_clip_gh_read(SHCLFORMAT uFmt)
{
    int rc = VINF_SUCCESS;
    vbox_wl_gtk_ctx_t *pCtx = &g_GtkClipCtx;

    VBCL_LOG_CALLBACK;

    if (uFmt != VBOX_SHCL_FMT_NONE)
    {
        VBClLogVerbose(2, "host wants fmt 0x%x\n", uFmt);

        /* This callback can be called in two cases:
         *
         * 1. Guest has just announced a list of its clipboard
         *    formats to the host, and vboxwl tool is still running,
         *    IPC session is still active as well. In this case the
         *    host can immediately ask for content in specified format.
         *
         * 2. Guest has already announced list of its clipboard
         *    formats to the host some time ago, vboxwl tool is no
         *    longer running and IPC session is not active. In this
         *    case some app on the host side might want to read
         *    clipboard in specific format.
         *
         * In case (2), we need to start new IPC session and restart
         * vboxwl tool again
         */
        if (!vbcl_wayland_session_is_started(&pCtx->Session.Base))
        {
            rc = vbcl_wayland_session_start(&pCtx->Session.Base,
                                            VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST,
                                            &vbcl_wayland_hlp_gtk_session_start_generic_cb,
                                            pCtx);
        }

        if (RT_SUCCESS(rc))
        {
            struct vbcl_wayland_hlp_gtk_clip_gh_read_priv priv = { pCtx, uFmt };

            rc = vbcl_wayland_session_join(&pCtx->Session.Base,
                                           &vbcl_wayland_hlp_gtk_clip_gh_read_join_cb,
                                           &priv);
        }
    }

    VBClLogVerbose(2, "vbcl_wayland_hlp_gtk_clip_gh_read ended rc=%Rrc\n", rc);

    return rc;
}


/***********************************************************************
 * DnD.
 **********************************************************************/

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_DND,pfnInit}
 */
RTDECL(int) vbcl_wayland_hlp_gtk_dnd_init(void)
{
    VBCL_LOG_CALLBACK;

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_DND,pfnTerm}
 */
RTDECL(int) vbcl_wayland_hlp_gtk_dnd_term(void)
{
    VBCL_LOG_CALLBACK;

    return VINF_SUCCESS;
}


static const VBCLWAYLANDHELPER_CLIPBOARD g_WaylandHelperGtkClip =
{
    vbcl_wayland_hlp_gtk_clip_init,             /* .pfnInit */
    vbcl_wayland_hlp_gtk_clip_term,             /* .pfnTerm */
    vbcl_wayland_hlp_gtk_clip_set_ctx,          /* .pfnSetClipboardCtx */
    vbcl_wayland_hlp_gtk_clip_popup,            /* .pfnPopup */
    vbcl_wayland_hlp_gtk_clip_hg_report,        /* .pfnHGClipReport */
    vbcl_wayland_hlp_gtk_clip_gh_read,          /* .pfnGHClipRead */
};

static const VBCLWAYLANDHELPER_DND g_WaylandHelperGtkDnD =
{
    vbcl_wayland_hlp_gtk_dnd_init,              /* .pfnInit */
    vbcl_wayland_hlp_gtk_dnd_term,              /* .pfnTerm */
};

/* Helper callbacks. */
const VBCLWAYLANDHELPER g_WaylandHelperGtk =
{
    "wayland-gtk",                              /* .pszName */
    vbcl_wayland_hlp_gtk_probe,                 /* .pfnProbe */
    g_WaylandHelperGtkClip,                     /* .clip */
    g_WaylandHelperGtkDnD,                      /* .dnd */
};
