/* $Id: vboxwl.cpp 109140 2025-04-09 11:16:52Z vadim.galitsyn@oracle.com $ */
/** @file
 * Guest Additions - Helper tool for grabbing input focus and perform
 * drag-n-drop and clipboard sharing in Wayland.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#include <stdlib.h>

#include <iprt/initterm.h>
#include <iprt/errcore.h>
#include <iprt/message.h>
#include <iprt/getopt.h>
#include <iprt/stream.h>
#include <iprt/assert.h>
#include <iprt/asm.h>

#include "product-generated.h"
#include <iprt/buildconfig.h>

#include <VBox/VBoxGuestLib.h>
#include <VBox/version.h>
#include <VBox/GuestHost/mime-type-converter.h>

#include "VBoxClient.h"
#include "wayland-helper-ipc.h"
#include "vbox-gtk.h"
#include "wayland-helper.h"
#include "clipboard.h"

/** Gtk App window default width. */
#define VBOXWL_WINDOW_WIDTH                 (100)
/** Gtk App window default height. */
#define VBOXWL_WINDOW_HEIGHT                (100)
/** Gtk App window default transparency level. */
#define VBOXWL_WINDOW_ALPHA                 (.1)
/** Gtk App watchdog callback triggering interval. */
#define VBOXWL_WATCHDOG_INTERVAL_MS         (50)
/** Gtk App exit timeout. */
#define VBOXWL_EXIT_TIMEOUT_MS              (500)

#define VBOXWL_ARG_CLIP_HG_COPY_BIT         RT_BIT(0)
#define VBOXWL_ARG_CLIP_GH_ANNOUNCE_BIT     RT_BIT(1)
#define VBOXWL_ARG_CLIP_GH_COPY_BIT         RT_BIT(2)

/** Program name. */
static char *g_pszProgName;

/** Unused: just for linking purposes. */
unsigned g_cRespawn = 0;

/** A session ID which will be specified in communication messages
 * with VBoxClient instance. */
static uint32_t g_uSessionId = 0;

/** One-shot session type. */
static vbcl_wl_session_type_t g_enmSessionType = VBCL_WL_SESSION_TYPE_INVALID;

/** Logging verbosity level. */
unsigned g_cVerbosity = 0;

/** Global flag to tell Gtk app to quit. */
static uint64_t g_tsGtkQuit = 0;

/** Gtk app thread. */
static RTTHREAD g_AppThread;

/** Gtk App window. */
static GtkWidget *g_pWindow;

/** Clipboard IPC flow object. */
vbcl::ipc::clipboard::ClipboardIpc *g_oClipboardIpc;


/************************************************************************************************
 * Copy from guest clipboard.
 ***********************************************************************************************/


/**
 * A callback to read guest clipboard data.
 *
 * @param pClipboard        Pointer to Gtk clipboard object.
 * @param pSelectionData    Pointer to Gtk selection object.
 * @param pvUser            User data.
 */
static void vboxwl_gtk_clipboard_read(GtkClipboard* pClipboard,
                                      GtkSelectionData* pSelectionData,
                                      gpointer pvUser)
{
    guchar *pData;
    gint cbData = -1;
    int rc = VERR_INVALID_PARAMETER;

    RT_NOREF(pClipboard, pvUser);

    VBCL_LOG_CALLBACK;

    /* Read data from guest clipboard. */
    pData = (guchar *)gtk_selection_data_get_data_with_length(pSelectionData, &cbData);
    if (   RT_VALID_PTR(pData)
        && cbData > 0)
    {
        char *pcszMimeType = gdk_atom_name(gtk_selection_data_get_data_type(pSelectionData));
        if (RT_VALID_PTR(pcszMimeType))
        {
            void *pvBufOut = NULL;
            size_t cbBufOut = 0;

            /* Convert guest clipboard into VBox representation. */
            rc = VBoxMimeConvNativeToVBox(pcszMimeType, pData, cbData, &pvBufOut, &cbBufOut);
            if (RT_SUCCESS(rc))
            {
                g_oClipboardIpc->m_pvClipboardBuf.set((uint64_t)pvBufOut);
                g_oClipboardIpc->m_cbClipboardBuf.set((uint64_t)cbBufOut);
                g_tsGtkQuit = RTTimeMilliTS();
            }
            else
                VBClLogError("session %u: cannot convert guest clipboard: rc=%Rrc\n", g_uSessionId, rc);

            g_free(pcszMimeType);
        }
        else
            VBClLogError("session %u: guest provided no target type\n",
                         g_uSessionId);
    }
}

/**
 * Find first matching VBox format for given Gtk target.
 *
 * @returns VBox clipboard format or VBOX_SHCL_FMT_NONE if no match found..
 * @param   pTargets    List of Gtk targets to match.
 * @param   cTargets    Number of targets.
 */
static SHCLFORMATS vboxwl_gtk_match_formats(GdkAtom *pTargets, gint cTargets)
{
    SHCLFORMATS fFmts = VBOX_SHCL_FMT_NONE;

    for (int i = 0; i < cTargets; i++)
    {
        gchar *sTargetName = gdk_atom_name(pTargets[i]);
        if (RT_VALID_PTR(sTargetName))
        {
            fFmts |= VBoxMimeConvGetIdByMime(sTargetName);
            g_free(sTargetName);
        }
    }

    return fFmts;
}

/**
 * Find matching Gtk target for given VBox format.
 *
 * @returns Gtk target or GDK_NONE if no match found.
 * @param   pTargets    List of Gtk targets to match.
 * @param   cTargets    Number of targets.
 * @param   uFmt        VBox formats to match.
 */
static GdkAtom vboxwl_gtk_match_target(GdkAtom *pTargets, gint cTargets, SHCLFORMAT uFmt)
{
    GdkAtom match = GDK_NONE;

    for (int i = 0; i < cTargets; i++)
    {
        gchar *sTargetName = gdk_atom_name(pTargets[i]);
        if (RT_VALID_PTR(sTargetName))
        {
            if (uFmt == VBoxMimeConvGetIdByMime(sTargetName))
                match = pTargets[i];

            g_free(sTargetName);
        }
    }

    return match;
}

/**
 * Gtk callback to read guest clipboard content.
 *
 * @param pClipboard    Pointer to Gtk clipboard object.
 * @param pEvent        Pointer to Gtk clipboard event.
 * @param pvUser        User data.
 */
static DECLCALLBACK(void) vboxwl_gtk_clipboard_get(GtkClipboard *pClipboard, GdkEvent *pEvent, gpointer pvUser)
{
    GdkAtom *pTargets;
    gint cTargets;
    gboolean fRc;

    RT_NOREF(pEvent, pvUser);

    VBCL_LOG_CALLBACK;

    /* Wait for Gtk to offer available clipboard content. */
    fRc = gtk_clipboard_wait_for_targets(pClipboard, &pTargets, &cTargets);
    if (fRc)
    {
        /* Convert guest clipboard targets list into VBox representation. */
        SHCLFORMATS fFormats = vboxwl_gtk_match_formats(pTargets, cTargets);
        SHCLFORMAT uFmt;

        /* Set formats to be sent to the host. */
        g_oClipboardIpc->m_fFmts.set(fFormats);

        /* Wait for host to send clipboard format it wants to copy from guest. */
        uFmt = g_oClipboardIpc->m_uFmt.wait();
        if (uFmt != g_oClipboardIpc->m_uFmt.defaults())
        {
            /* Find target which matches to host format among reported by guest. */
            GdkAtom gtkFmt = vboxwl_gtk_match_target(pTargets, cTargets, uFmt);
            if (gtkFmt != GDK_NONE)
                gtk_clipboard_request_contents(pClipboard, gtkFmt, &vboxwl_gtk_clipboard_read, pvUser);
            else
                VBClLogVerbose(2, "session %u: will not send format 0x%x to host, not known to the guest\n",
                               g_uSessionId, uFmt);
        }
        else
            VBClLogVerbose(2, "session %u: host did not send desired clipboard format in time\n", g_uSessionId);

        g_free(pTargets);
    }
}


/************************************************************************************************
 * Paste into the guest clipboard.
 ***********************************************************************************************/


/**
 * A callback to write data to the guest clipboard.
 *
 * @param pClipboard        Pointer to Gtk clipboard object.
 * @param pSelectionData    Pointer to Gtk selection object.
 * @param info              Ignored.
 * @param pvUser            User data.
 */
static void vboxwl_gtk_clipboard_write(GtkClipboard *pClipboard,
                                       GtkSelectionData *pSelectionData,
                                       guint info, gpointer pvUser)
{
    GdkAtom target = gtk_selection_data_get_target(pSelectionData);
    gchar *sTargetName = gdk_atom_name(target);
    SHCLFORMAT uFmt = VBoxMimeConvGetIdByMime(sTargetName);
    int rc;

    RT_NOREF(info, pvUser);

    VBCL_LOG_CALLBACK;

    /* Set clipboard format which guest wants to send it to the host. */
    g_oClipboardIpc->m_uFmt.set(uFmt);

    /* Wait for the host to send clipboard data in requested format. */
    uint32_t cbBuf = g_oClipboardIpc->m_cbClipboardBuf.wait();
    void *pvBuf = (void *)g_oClipboardIpc->m_pvClipboardBuf.wait();

    if (   cbBuf != g_oClipboardIpc->m_cbClipboardBuf.defaults()
        && pvBuf != (void *)g_oClipboardIpc->m_pvClipboardBuf.defaults())
    {
        void *pBufOut;
        size_t cbOut;

        /* Convert clipboard data from VBox representation into guest format. */
        rc = VBoxMimeConvVBoxToNative(sTargetName, pvBuf, cbBuf, &pBufOut, &cbOut);
        if (RT_SUCCESS(rc))
        {
            gtk_selection_data_set(pSelectionData, target,  8, (const guchar *)pBufOut, cbOut);
            gtk_clipboard_store(pClipboard);

            gtk_window_iconify(GTK_WINDOW(g_pWindow));

            /* Ask Gtk to quit on the next event loop iteration. */
            g_tsGtkQuit = RTTimeMilliTS();

            VBClLogVerbose(2, "session %u: paste %u bytes of mime-type '%s' into Gtk\n",
                           g_uSessionId, cbOut, sTargetName);
        }
        else
            VBClLogError("session %u: cannot convert '%s' (%u bytes) into native representation, rc=%Rrc\n",
                         g_uSessionId, sTargetName, cbBuf, rc);
    }
    else
        VBClLogError("session %u: cannot paste '%s' into Gtk: no data\n", g_uSessionId, sTargetName);

    g_free(sTargetName);
}

/**
 * Dummy Gtk callback.
 *
 * @param pClipboard    Pointer to Gtk clipboard object.
 * @param pvUser        User data.
 */
static void vboxwl_gtk_clipboard_write_fini(GtkClipboard *pClipboard, gpointer pvUser)
{
    VBCL_LOG_CALLBACK;
    RT_NOREF(pClipboard, pvUser);
}

/**
 * Gtk clipboard target list builder,
 *
 * Triggered by VBoxMimeConvEnumerateMimeById() when matching VBox
 * clipboard formats into Gtk representation.
 *
 * @param pcszMimeType  Mime-type in Gtk representation.
 * @param pvUser        Output buffer.
 */
static DECLCALLBACK(void) vboxwl_gtk_build_target_list(const char *pcszMimeType, void *pvUser)
{
    GtkTargetList *aTargetList = (GtkTargetList *)pvUser;

    VBClLogVerbose(2, "session %u: mime-type '%s' -> guest\n", g_uSessionId, pcszMimeType);
    gtk_target_list_add(aTargetList, gdk_atom_intern(pcszMimeType, FALSE), 0, 0);
}

/**
 * Gtk callback to paste into clipboard.
 *
 * Wait for host to announce its clipboard formats and advertise
 * them to guest.
 *
 * @returns TRUE to stop other Gtk handlers from being invoked for the
 *          event. FALSE to propagate the event further.
 * @param   pSelf   Pointer to Gtk widget object.
 * @param   event   Gtk event structure.
 * @param   pvUser  User data.
 */
static DECLCALLBACK(gboolean) vboxwl_gtk_clipboard_set(GtkWidget* pSelf, GdkEventWindowState event, gpointer pvUser)
{
    SHCLFORMATS fFmts;
    GtkClipboard *pClipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

    RT_NOREF(pSelf, event, pvUser);

    VBCL_LOG_CALLBACK;

    /* Wait for host to report available clipboard formats from its buffer. */
    fFmts = g_oClipboardIpc->m_fFmts.wait();
    if (fFmts != g_oClipboardIpc->m_fFmts.defaults())
    {
        GtkTargetList *aTargetList = gtk_target_list_new(0, 0);
        GtkTargetEntry *aTargets;
        int cTargets = 0;

        /* Convert host clipboard formats bitmask into Gtk mime-types list. */
        VBoxMimeConvEnumerateMimeById(fFmts, vboxwl_gtk_build_target_list, aTargetList);

        aTargets = gtk_target_table_new_from_list(aTargetList, &cTargets);
        if (RT_VALID_PTR(aTargets))
        {
            gboolean fRc;

            /* Announce clipboard content to the guest. */
            fRc = gtk_clipboard_set_with_data(pClipboard, aTargets, cTargets,
                                              &vboxwl_gtk_clipboard_write,
                                              &vboxwl_gtk_clipboard_write_fini, NULL);
            if (!fRc)
                VBClLogVerbose(2, "session %u: cannot announce clipboard to Gtk\n", g_uSessionId);

            gtk_target_table_free(aTargets, cTargets);
        }
    }

    return TRUE;
}


/************************************************************************************************
 * Gtk App.
 ***********************************************************************************************/


/**
 * Gtk App watchdog.
 *
 * Responsible for quitting the app in the end of Gtk event loop cycle.
 *
 * @returns FALSE to stop watchdog, TRUE otherwise.
 * @param   pvUser    User data.
 */
static gboolean vboxwl_gtk_watchdog(gpointer pvUser)
{
    RT_NOREF(pvUser);

    if (   g_tsGtkQuit > 0
        && (RTTimeMilliTS() - g_tsGtkQuit) > VBOXWL_EXIT_TIMEOUT_MS)
    {
        g_application_quit(G_APPLICATION(g_application_get_default()));
    }

    return TRUE;
}

/**
 * Construct visible Gtk app window.
 *
 * @param pApp      Application object.
 * @param pvUser    User data.
 */
static DECLCALLBACK(void) vboxwl_gtk_app_start(GtkApplication* pApp, gpointer pvUser)
{
    GtkWidget *pButton, *pBox;

    /* Construct a simple window with a single button element. */
    g_pWindow = gtk_application_window_new(pApp);
    if (RT_VALID_PTR(g_pWindow))
    {
        g_signal_connect(g_pWindow, "delete_event", gtk_main_quit, NULL);

        gtk_window_set_default_size(GTK_WINDOW(g_pWindow), VBOXWL_WINDOW_WIDTH, VBOXWL_WINDOW_HEIGHT);
        gtk_window_resize(GTK_WINDOW(g_pWindow), VBOXWL_WINDOW_WIDTH, VBOXWL_WINDOW_HEIGHT);

        pBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        pButton = gtk_button_new();

        if (   RT_VALID_PTR(pBox)
            && RT_VALID_PTR(pButton))
        {
            /* Add button to the window. */
            gtk_container_add(GTK_CONTAINER(g_pWindow), pBox);
            gtk_box_pack_start(GTK_BOX(pBox), pButton, TRUE, TRUE, 0);

            /* Set elements opacity. */
            gtk_widget_set_opacity(g_pWindow, VBOXWL_WINDOW_ALPHA);
            gtk_widget_set_opacity(pButton, VBOXWL_WINDOW_ALPHA);

            /* Setup watchdog handler. */
            gdk_threads_add_timeout(VBOXWL_WATCHDOG_INTERVAL_MS, &vboxwl_gtk_watchdog, NULL);

            /* Subscribe to Gtk events depending on session type. */
            if (g_enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST)
            {
                g_signal_connect_after(g_pWindow, "window-state-event", G_CALLBACK(vboxwl_gtk_clipboard_set), pvUser);
            }
            else if (   g_enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST
                     || g_enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST)
            {
                GtkClipboard *pClipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
                g_signal_connect(pClipboard, "owner-change", G_CALLBACK(vboxwl_gtk_clipboard_get), pvUser);
            }
            else
            {
                VBClLogError("unknown session type, requesting app quit\n");
                g_tsGtkQuit = RTTimeMilliTS();
            }

            gtk_window_present(GTK_WINDOW(g_pWindow));
            gtk_widget_show_all(g_pWindow);
        }
    }
}

/**
 * Gtk App event loop handler.
 *
 * @returns IPRT status code.
 * @param   hThreadSelf     Running thread handle.
 * @param   pvUser          User data.
 */
static DECLCALLBACK(int) vboxwl_gtk_worker(RTTHREAD hThreadSelf, void *pvUser)
{
    int rc = VERR_NO_MEMORY;
    GtkApplication *pApp;

    /* Tell parent we are ready. */
    RTThreadUserSignal(hThreadSelf);

    pApp = gtk_application_new("org.virtualbox.vboxwl", G_APPLICATION_FLAGS_NONE);
    if (RT_VALID_PTR(pApp))
    {
        /* Create app visual instance when ready. */
        g_signal_connect(pApp, "activate", G_CALLBACK(vboxwl_gtk_app_start), pvUser);

        /* Run gtk main loop. */
        rc = g_application_run(G_APPLICATION (pApp), 0, NULL);

        g_object_unref(pApp);
    }

    return rc;
}


/************************************************************************************************
 * IPC handling.
 ***********************************************************************************************/


/**
 * Process IPC commands flow for session type.
 *
 * @returns IPRT status code.
 * @param   hIpcSession     IPC connection handle.
 */
static int vboxwl_ipc_flow(RTLOCALIPCSESSION hIpcSession)
{
    int rc = VERR_INVALID_PARAMETER;

    if      (g_enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST)
        rc = g_oClipboardIpc->flow(vbcl::ipc::clipboard::HGCopyFlow, hIpcSession);
    else if (g_enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST)
        rc = g_oClipboardIpc->flow(vbcl::ipc::clipboard::GHAnnounceAndCopyFlow, hIpcSession);
    else if (g_enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST)
        rc = g_oClipboardIpc->flow(vbcl::ipc::clipboard::GHCopyFlow, hIpcSession);

    return rc;
}

/**
 * Connect to VBoxClient service.
 *
 * @returns IPRT status code.
 * @param   phIpcSession    Pointer to IPC connection handle (out).
 */
static int vboxwl_connect_ipc(PRTLOCALIPCSESSION phIpcSession)
{
    int rc;
    char szIpcServerName[128];

    AssertPtrReturn(phIpcSession, VERR_INVALID_PARAMETER);

    rc = vbcl_wayland_hlp_gtk_ipc_srv_name(szIpcServerName, sizeof(szIpcServerName));
    if (RT_SUCCESS(rc))
        rc = RTLocalIpcSessionConnect(phIpcSession, szIpcServerName, 0);

    VBClLogInfo("session %u: ipc connect: rc=%Rrc\n", g_uSessionId, rc);

    return rc;
}


/************************************************************************************************
 * Generic initialization.
 ***********************************************************************************************/


/**
 * Execute requested command.
 *
 * @returns IPRT status code.
 */
static int vboxwl_run_command(void)
{
    int rc;
    int rcThread = -1;

    RTLOCALIPCSESSION hIpcSession;

    rc = VBClClipboardThreadStart(&g_AppThread, vboxwl_gtk_worker, "gtk-app", NULL);
    if (RT_SUCCESS(rc))
    {
        rc = vboxwl_connect_ipc(&hIpcSession);
        if (RT_SUCCESS(rc))
        {
            g_oClipboardIpc = new vbcl::ipc::clipboard::ClipboardIpc();
            if (RT_VALID_PTR(g_oClipboardIpc))
            {
                g_oClipboardIpc->init(vbcl::ipc::FLOW_DIRECTION_CLIENT, g_uSessionId);

                rc = vboxwl_ipc_flow(hIpcSession);
                VBClLogVerbose(2, "session %u: ended with rc=%Rrc\n", g_uSessionId, rc);

                /* Ask Gtk app to quit if IPC task has failed. */
                if (RT_FAILURE(rc))
                    g_tsGtkQuit = RTTimeMilliTS();

                /* Wait for app thread termination first, it uses resources we just created. */
                rc = RTThreadWait(g_AppThread, RT_MS_30SEC, &rcThread);
                VBClLogInfo("session %u: gtk app exited: rc=%Rrc, rcThread=%Rrc\n",
                            g_uSessionId, rc, rcThread);

                g_oClipboardIpc->reset();
                delete g_oClipboardIpc;
            }
            else
                VBClLogError("session %u: unable to create ipc clipboard object\n", g_uSessionId);

            rc = RTLocalIpcSessionClose(hIpcSession);
            VBClLogVerbose(1, "session %u: ipc disconnected: rc=%Rrc\n", g_uSessionId, rc);
        }
    }
    else
        VBClLogError("session %u: gtk app start: rc=%Rrc\n", g_uSessionId, rc);

    return rc;
}

/**
 * Print command line usage and exit.
 */
static void vboxwl_usage(void)
{
    RTPrintf(VBOX_PRODUCT " %s "
             VBOX_VERSION_STRING "\n"
             "Copyright (C) 2005-" VBOX_C_YEAR " " VBOX_VENDOR "\n\n", g_pszProgName);

    RTPrintf("Usage: %s [ %s %s|%s|%s ] | [--help|-h] [--version|-V] [--verbose|-v]\n\n",
             g_pszProgName, VBOXWL_ARG_SESSION_ID, VBOXWL_ARG_CLIP_HG_COPY,
             VBOXWL_ARG_CLIP_GH_ANNOUNCE, VBOXWL_ARG_CLIP_GH_COPY);

    /* Using '%-20s' if pretty much hardcoded here to make output look accurate. Please
     * feel free to adjust if needed later on. */
    RTPrintf("Options:\n");
    RTPrintf("  %-20s Required with --clipboad-paste or --clipboad-copy \n", VBOXWL_ARG_SESSION_ID);
    RTPrintf("                       command, used for communication with VBoxClient instance\n");

    RTPrintf("  %-20s Paste content into clipboard\n", VBOXWL_ARG_CLIP_HG_COPY);
    RTPrintf("  %-20s Announce clipboard content to the host\n", VBOXWL_ARG_CLIP_GH_ANNOUNCE);
    RTPrintf("  %-20s Copy content from clipboard\n", VBOXWL_ARG_CLIP_GH_COPY);

    RTPrintf("  --check              Check if active Wayland session is running\n");
    RTPrintf("  --verbose            Increase verbosity level\n");
    RTPrintf("  --version            Print version number and exit\n");
    RTPrintf("  --help               Print this message\n");
    RTPrintf("\n");

    exit(1);
}

/**
 * Check if active Wayland session is running.
 *
 * This check is used in order to detect whether X11 or Wayland
 * version of VBoxClient should be started when user logs-in.
 * It will print out either WL or X11 and exit. Startup script(s)
 * should rely on this output.
 */
static void vboxwl_check(void)
{
    VBGHDISPLAYSERVERTYPE enmType = VBGHDisplayServerTypeDetect();
    bool fWayland = false;

    /* In pure Wayland environment X11 version of VBoxClient will not
     * work, so fallback on Wayland version. */
    if (enmType == VBGHDISPLAYSERVERTYPE_PURE_WAYLAND)
        fWayland = true;
    else if (enmType == VBGHDISPLAYSERVERTYPE_XWAYLAND)
    {
        /* In case of XWayland, X11 version of VBoxClient still can
         * work, however with some DEs, such as Plasma on Wayland,
         * this will no longer work. Detect such DEs here. */

        /* Try to detect Plasma. */
        const char *pcszDesktopSession = RTEnvGet(VBGH_ENV_DESKTOP_SESSION);
        if (   RT_VALID_PTR(pcszDesktopSession)
            && (   RTStrIStr(pcszDesktopSession, "plasmawayland")
                || RTStrIStr(pcszDesktopSession, "plasma")))
            fWayland = true;
    }

    RTPrintf("%s\n", fWayland ? "WL" : "X11");
    exit (0);
}

/**
 * Print version and exit.
 */
static void vboxwl_version(void)
{
    RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
    exit(0);
}

/**
 * Parse command line options.
 *
 * @param argc  Number of command line arguments.
 * @param argv  List of command line arguments.
 */
static void vboxwl_parse_params(int argc, char *argv[])
{
    /* Parse our option(s). */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { VBOXWL_ARG_CLIP_HG_COPY,      'p', RTGETOPT_REQ_NOTHING },
        { VBOXWL_ARG_CLIP_GH_ANNOUNCE,  'a', RTGETOPT_REQ_NOTHING },
        { VBOXWL_ARG_CLIP_GH_COPY,      'c', RTGETOPT_REQ_NOTHING },
        { VBOXWL_ARG_SESSION_ID,        's', RTGETOPT_REQ_UINT32  },
        { "--check",                    'C', RTGETOPT_REQ_NOTHING },
        { "--help",                     'h', RTGETOPT_REQ_NOTHING },
        { "--version",                  'V', RTGETOPT_REQ_NOTHING },
        { "--verbose",                  'v', RTGETOPT_REQ_NOTHING },
    };

    int                     ch;
    RTGETOPTUNION           ValueUnion;
    RTGETOPTSTATE           GetState;

    int rc;
    uint8_t fArgsMask = 0;

    rc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /* fFlags */);
    if (RT_SUCCESS(rc))
    {
        while (   RT_SUCCESS(rc)
               && ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0))
        {
            switch (ch)
            {
                case 'p':
                {
                    fArgsMask |= VBOXWL_ARG_CLIP_HG_COPY_BIT;
                    break;
                }

                case 'a':
                {
                    fArgsMask |= VBOXWL_ARG_CLIP_GH_ANNOUNCE_BIT;
                    break;
                }

                case 'c':
                {
                    fArgsMask |= VBOXWL_ARG_CLIP_GH_COPY_BIT;
                    break;
                }

                case 's':
                {
                    g_uSessionId = ValueUnion.u32;
                    break;
                }

                case 'C':
                {
                    vboxwl_check();
                    break;
                }

                case 'h':
                {
                    vboxwl_usage();
                    break;
                }

                case 'V':
                {
                    vboxwl_version();
                    break;
                }

                case 'v':
                {
                    g_cVerbosity++;
                    break;
                }

                case VINF_GETOPT_NOT_OPTION:
                    break;

                case VERR_GETOPT_UNKNOWN_OPTION:
                    RT_FALL_THROUGH();
                default:
                {
                    RTPrintf("\n");
                    RTGetOptPrintError(ch, &ValueUnion);
                    RTPrintf("\n");
                }
            }
        }
    }

    /* Check if session ID was specified and command line has
     * no syntax errors. */
    if (   RT_FAILURE(rc)
        || !g_uSessionId)
    {
        vboxwl_usage();
    }

    /* Make sure only one action was specified in command line,
     * print usage and exit otherwise. */
    if      (fArgsMask == VBOXWL_ARG_CLIP_HG_COPY_BIT)
        g_enmSessionType = VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST;
    else if (fArgsMask == VBOXWL_ARG_CLIP_GH_ANNOUNCE_BIT)
        g_enmSessionType = VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST;
    else if (fArgsMask == VBOXWL_ARG_CLIP_GH_COPY_BIT)
        g_enmSessionType = VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST;
    else
        vboxwl_usage();
}

/** Initialization step shortcut macro.
 *
 * Try to run initialization function if previous step was successful and print error if it occurs.
 *
 * @param _fn       A function to call.
 * @param _error    Error message to print if function fails.
 */
#define VBOXWL_INIT(_fn, _error) \
    if (RT_SUCCESS(rc)) \
    { \
        rc = _fn; \
        if (RT_FAILURE(rc)) \
            RTPrintf("%s, rc=%Rrc\n", _error, rc); \
    }

int main(int argc, char *argv[])
{
    int rc = VINF_SUCCESS;

    /** Custom log prefix to be used for logger instance of this process. */
    static const char *pszLogPrefix = "vboxwl:";

    /* Set program name. */
    g_pszProgName = argv[0];

    /* Initialize runtime. */
    VBOXWL_INIT(RTR3InitExe(argc, &argv, 0), "cannot initialize runtime");

    /* Go through command line parameters. */
    vboxwl_parse_params(argc, argv);

    if (!VBGHDisplayServerTypeIsGtkAvailable())
    {
        RTPrintf("Gtk3 library is required to run this tool, but can not be found\n");
        return RTEXITCODE_FAILURE;
    }

    /* Initialize runtime before all else. */
    VBOXWL_INIT(VbglR3InitUser(),                       "cannot to communicate with vboxguest kernel module");
    VBOXWL_INIT(VBClLogCreateEx("", false),             "cannot create logger instance");
    VBOXWL_INIT(VBClLogModify("stdout", g_cVerbosity),  "cannot setup log");

    /* Set custom log prefix. */
    VBClLogSetLogPrefix(pszLogPrefix);

    VBOXWL_INIT(vboxwl_run_command(),                   "cannot run command");

    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}
