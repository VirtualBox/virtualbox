/* $Id: vboxwl.cpp 114742 2026-07-21 18:04:56Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Wayland helper for grabbing input focus, drag-n-drop and clipboard sharing.
 *
 * @note Obsolete, not compiled.
 */

/*
 * Copyright (C) 2017-2026 Oracle and/or its affiliates.
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
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/getopt.h>
#include <iprt/message.h>
#include <iprt/process.h>
#include <iprt/stream.h>

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


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
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


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Unused: just for linking purposes. */
unsigned g_cRespawn = 0;

/** A session ID which will be specified in communication messages
 * with VBoxClient instance. */
static uint32_t g_idSession = 0;

/** One-shot session type. */
static vbcl_wl_session_type_t g_enmSessionType = VBCL_WL_SESSION_TYPE_INVALID;

/** Logging verbosity level. */
unsigned g_cVerbosity = 0;

/** Global flag to tell Gtk app to quit. */
static uint64_t g_nsGtkQuit = 0;

/** Gtk app thread. */
static RTTHREAD g_AppThread;

/** Gtk App window. */
static GtkWidget *g_pWindow;

/** Clipboard IPC flow object. */
vbcl::ipc::data::DataIpc *g_oDataIpc;


/*********************************************************************************************************************************
*   Temporary Hacks                                                                                                              *
*********************************************************************************************************************************/

/**
 * Duplicated, just to make this obsolete util keep building for a while longer.
 */
int VBClStartThread(PRTTHREAD phThread, PFNRTTHREAD pfnThread, const char *pszName, void *pvUser)
{
    RTTHREAD hThread = NIL_RTTHREAD;
    int rc = RTThreadCreate(&hThread, pfnThread, pvUser, 0, RTTHREADTYPE_IO,
                            RTTHREADFLAGS_WAITABLE | RTTHREADFLAGS_USER_SIGNAL_ON_TERM, pszName);
    if (RT_SUCCESS(rc))
    {
        *phThread = hThread;
        rc = RTThreadUserWait(hThread, RT_MS_30SEC /* msTimeout */);
        if (RT_SUCCESS(rc))
        {
            int rcThread = VINF_SUCCESS;
            rc = RTThreadWait(hThread, 0, &rcThread);
            if (rc == VERR_TIMEOUT)
            {
                VBClLogVerbose(1, "started %s thread\n", pszName);
                return VINF_SUCCESS;
            }

            if (RT_SUCCESS(rc))
            {
                /* Note! If we end up with VINF_SUCCESS here, it could in theorybe some
                         kind of race with a regular exit.  Though, it shouldn't since
                         we shouldn't be using that shortlived threads... */
                VBClLogError("thread '%s' failed to initialize: %Rrc\n", pszName, rcThread);
                rc = !RT_SUCCESS_NP(rcThread) ? rcThread : VERR_INTERNAL_ERROR_2;
            }
            else
                VBClLogError("Failed checking thread '%s' after initialization: %Rrc\n", pszName, rc);
        }
        else
            VBClLogError("Failed waiting (30s) for thread '%s' to initialize: %Rrc\n", pszName, rc);
    }
    else
        VBClLogError("Failed to start thread '%s': %Rrc\n", pszName, rc);
    *phThread = NIL_RTTHREAD;
    return rc;
}


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
static void vbwlGtkClipboardReadCallback(GtkClipboard *pClipboard, GtkSelectionData *pSelectionData, gpointer pvUser)
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
            rc = VbghMimeConvToVBox(pcszMimeType, pData, cbData, &pvBufOut, &cbBufOut);
            if (RT_SUCCESS(rc))
            {
                g_oDataIpc->m_pvDataBuf.set((uint64_t)pvBufOut);
                g_oDataIpc->m_cbDataBuf.set((uint64_t)cbBufOut);
                g_nsGtkQuit = RTTimeMilliTS();
            }
            else
                VBClLogError("session %u: cannot convert guest clipboard: rc=%Rrc\n", g_idSession, rc);

            g_free(pcszMimeType);
        }
        else
            VBClLogError("session %u: guest provided no target type\n",
                         g_idSession);
    }
}

/**
 * Find all the matching VBox format for given Gtk target list.
 *
 * @returns VBox clipboard format or VBOX_SHCL_FMT_NONE if no match found..
 * @param   paTargets   Array of Gtk targets to match.
 * @param   cTargets    Number of targets.
 */
static SHCLFORMATS vbwlGtkMapTargetsToVBoxFormats(GdkAtom *paTargets, gint cTargets)
{
    SHCLFORMATS fFmts = VBOX_SHCL_FMT_NONE;

    for (gint i = 0; i < cTargets; i++)
    {
        gchar *pszTargetName = gdk_atom_name(paTargets[i]);
        if (RT_VALID_PTR(pszTargetName))
        {
            SHCLFORMATS const fCvtFmt = VbghMimeConvGetVBoxFormatByMime(pszTargetName, NULL /*pfFlagsAndPriority*/,
                                                                        NULL /* ppszPresistentMimeType*/);
            VBClLogVerbose(5, "session %u: %#zx/%s -> %#x\n", g_idSession, (size_t)paTargets[i], pszTargetName, fCvtFmt);
            fFmts |= fCvtFmt;
            g_free(pszTargetName);
        }
    }

    VBClLogVerbose(4, "session %u: vbwlGtkMapTargetsToVBoxFormats -> %#x\n", g_idSession, fFmts);
    return fFmts;
}

/**
 * Find matching Gtk target for given VBox format.
 *
 * @returns Gtk target or GDK_NONE if no match found.
 * @param   paTargets   Array of Gtk targets to match.
 * @param   cTargets    Number of targets.
 * @param   uFmt        VBox formats to match.
 */
static GdkAtom vbwlGtkClipboardMapFromVBoxFormat(GdkAtom *paTargets, gint cTargets, SHCLFORMAT uFmt)
{
    uint32_t uMatchPriority = 0;
    GdkAtom hMatch = GDK_NONE;

    for (int i = 0; i < cTargets; i++)
    {
        gchar *pszTargetName = gdk_atom_name(paTargets[i]);
        if (RT_VALID_PTR(pszTargetName))
        {
            uint32_t fFlagsAndPriority = 0;
            if (uFmt == VbghMimeConvGetVBoxFormatByMime(pszTargetName, &fFlagsAndPriority, NULL /*ppszPresistentMimeType*/))
            {
                if ((fFlagsAndPriority & VBGH_MIME_CONV_F_PRIORITY_MASK) > uMatchPriority)
                {
                    VBClLogVerbose(4, "session %u: uFmt %#x -> %#zx/%s prio %#x (was %#zx prio %#x)\n", g_idSession, uFmt,
                                   (size_t)paTargets[i], pszTargetName, fFlagsAndPriority, (size_t)hMatch, uMatchPriority);
                    hMatch = paTargets[i];
                    uMatchPriority = fFlagsAndPriority & VBGH_MIME_CONV_F_PRIORITY_MASK;
                }
                else
                    VBClLogVerbose(5, "session %u: uFmt %#x rejecting %#zx/%s prio %#x, have %#zx prio %#x\n", g_idSession, uFmt,
                                   (size_t)paTargets[i], pszTargetName, fFlagsAndPriority, (size_t)hMatch, uMatchPriority);
            }

            g_free(pszTargetName);
        }
    }

    return hMatch;
}

/**
 * Gtk callback to read guest clipboard content.
 *
 * Hooked up to "owner-change" for VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST
 * and VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST.
 *
 * @param pClipboard    Pointer to Gtk clipboard object.
 * @param pEvent        Pointer to Gtk clipboard event.
 * @param pvUser        User data.
 */
static void vbwlGtkClipboardGetCallback(GtkClipboard *pClipboard, GdkEvent *pEvent, gpointer pvUser)
{
    RT_NOREF(pEvent, pvUser);
    VBCL_LOG_CALLBACK;

    /* Wait for Gtk to offer available clipboard content. */
    GdkAtom *paTargets = NULL;
    gint cTargets = 0;
    gboolean fRc = gtk_clipboard_wait_for_targets(pClipboard, &paTargets, &cTargets);
    if (fRc)
    {
        /* Convert guest clipboard targets list into VBox representation. */
        SHCLFORMATS fFormats = vbwlGtkMapTargetsToVBoxFormats(paTargets, cTargets);

        /* Set formats to be sent to the host. */
        g_oDataIpc->m_fFmts.set(fFormats);

        /* Wait for host to send clipboard format it wants to copy from guest. */
        SHCLFORMAT const uFmt = g_oDataIpc->m_uFmt.wait();
        if (uFmt != g_oDataIpc->m_uFmt.defaults())
        {
            /* Find target which matches to host format among reported by guest. */
            GdkAtom gtkFmt = vbwlGtkClipboardMapFromVBoxFormat(paTargets, cTargets, uFmt);
            if (gtkFmt != GDK_NONE)
                gtk_clipboard_request_contents(pClipboard, gtkFmt, vbwlGtkClipboardReadCallback, pvUser);
            else
                VBClLogVerbose(2, "session %u: will not send format 0x%x to host, not known to the guest\n",
                               g_idSession, uFmt);
        }
        else
            VBClLogVerbose(2, "session %u: host did not send desired clipboard format in time\n", g_idSession);

        g_free(paTargets);
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
    SHCLFORMAT uFmt = VbghMimeConvGetVBoxFormatByMime(sTargetName, NULL /*pfFlagsAndPriority*/, NULL /*ppszPresistentMimeType*/);
    int rc;

    RT_NOREF(info, pvUser);

    VBCL_LOG_CALLBACK;

    /* Set clipboard format which guest wants to send it to the host. */
    g_oDataIpc->m_uFmt.set(uFmt);

    /* Wait for the host to send clipboard data in requested format. */
    uint32_t cbBuf = g_oDataIpc->m_cbDataBuf.wait();
    void *pvBuf = (void *)g_oDataIpc->m_pvDataBuf.wait();

    if (   cbBuf != g_oDataIpc->m_cbDataBuf.defaults()
        && pvBuf != (void *)g_oDataIpc->m_pvDataBuf.defaults())
    {
        void *pBufOut;
        size_t cbOut;

        /* Convert clipboard data from VBox representation into guest format. */
        rc = VbghMimeConvFromVBox(sTargetName, pvBuf, cbBuf, &pBufOut, &cbOut);
        if (RT_SUCCESS(rc))
        {
            gtk_selection_data_set(pSelectionData, target,  8, (const guchar *)pBufOut, cbOut);
            gtk_clipboard_store(pClipboard);

            gtk_window_iconify(GTK_WINDOW(g_pWindow));

            /* Ask Gtk to quit on the next event loop iteration. */
            g_nsGtkQuit = RTTimeMilliTS();

            VBClLogVerbose(2, "session %u: paste %u bytes of MIME type '%s' into Gtk\n",
                           g_idSession, cbOut, sTargetName);
        }
        else
            VBClLogError("session %u: cannot convert '%s' (%u bytes) into native representation, rc=%Rrc\n",
                         g_idSession, sTargetName, cbBuf, rc);
    }
    else
        VBClLogError("session %u: cannot paste '%s' into Gtk: no data\n", g_idSession, sTargetName);

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
 * @callback_method_impl{FNVBGHMIMECONVENUM,
 *      Gtk clipboard target list builder callback.}
 */
static DECLCALLBACK(void) vboxwl_gtk_build_target_list(const char *pcszMimeType, uint32_t fFlagsAndPriority, void *pvUser)
{
    GtkTargetList *pTargetList = (GtkTargetList *)pvUser;
    RT_NOREF(fFlagsAndPriority);

    VBClLogVerbose(2, "session %u: MIME type '%s' -> guest\n", g_idSession, pcszMimeType);
    gtk_target_list_add(pTargetList, gdk_atom_intern(pcszMimeType, FALSE), 0, 0);
}

/**
 * Gtk callback to paste into clipboard.
 *
 * Wait for host to announce its clipboard formats and advertise them to guest.
 *
 * Hooked up to "window-state-event" for
 * VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST.
 *
 * @returns TRUE to stop other Gtk handlers from being invoked for the
 *          event. FALSE to propagate the event further.
 * @param   pSelf   Pointer to Gtk widget object.
 * @param   event   Gtk event structure.
 * @param   pvUser  User data.
 */
static gboolean vbwlGtkClipboardSetCallback(GtkWidget* pSelf, GdkEventWindowState event, gpointer pvUser)
{
    SHCLFORMATS fFmts;
    GtkClipboard *pClipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

    RT_NOREF(pSelf, event, pvUser);

    VBCL_LOG_CALLBACK;

    /* Wait for host to report available clipboard formats from its buffer. */
    fFmts = g_oDataIpc->m_fFmts.wait();
    if (fFmts != g_oDataIpc->m_fFmts.defaults())
    {
        GtkTargetList *aTargetList = gtk_target_list_new(0, 0);
        GtkTargetEntry *aTargets;
        int cTargets = 0;

        /* Convert host clipboard formats bitmask into Gtk MIME types list. */
        VbghMimeConvEnumerateByVBoxFormats(fFmts, vboxwl_gtk_build_target_list, aTargetList);

        aTargets = gtk_target_table_new_from_list(aTargetList, &cTargets);
        if (RT_VALID_PTR(aTargets))
        {
            gboolean fRc;

            /* Announce clipboard content to the guest. */
            fRc = gtk_clipboard_set_with_data(pClipboard, aTargets, cTargets,
                                              &vboxwl_gtk_clipboard_write,
                                              &vboxwl_gtk_clipboard_write_fini, NULL);
            if (!fRc)
                VBClLogVerbose(2, "session %u: cannot announce clipboard to Gtk\n", g_idSession);

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

    if (   g_nsGtkQuit > 0
        && (RTTimeMilliTS() - g_nsGtkQuit) > VBOXWL_EXIT_TIMEOUT_MS)
    {
        g_application_quit(G_APPLICATION(g_application_get_default()));
    }

    return TRUE;
}

/**
 * Construct visible Gtk app window.
 *
 * Connected to "activate" on the app.
 *
 * @param pApp      Application object.
 * @param pvUser    User data.
 */
static void vbwlGtkAppStart(GtkApplication* pApp, gpointer pvUser)
{
    /* Construct a simple window with a single button element. */
    g_pWindow = gtk_application_window_new(pApp);
    if (RT_VALID_PTR(g_pWindow))
    {
        g_signal_connect(g_pWindow, "delete_event", gtk_main_quit, NULL);

        gtk_window_set_default_size(GTK_WINDOW(g_pWindow), VBOXWL_WINDOW_WIDTH, VBOXWL_WINDOW_HEIGHT);
        gtk_window_resize(GTK_WINDOW(g_pWindow), VBOXWL_WINDOW_WIDTH, VBOXWL_WINDOW_HEIGHT);

        GtkWidget *pBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        GtkWidget *pButton = gtk_button_new();

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
                g_signal_connect_after(g_pWindow, "window-state-event", G_CALLBACK(vbwlGtkClipboardSetCallback), pvUser);
            else if (   g_enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST
                     || g_enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST)
            {
                GtkClipboard *pClipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
                g_signal_connect(pClipboard, "owner-change", G_CALLBACK(vbwlGtkClipboardGetCallback), pvUser);
            }
            else
            {
                VBClLogError("unknown session type (%d), requesting app quit\n", (int)g_enmSessionType);
                g_nsGtkQuit = RTTimeMilliTS();
            }

            gtk_window_present(GTK_WINDOW(g_pWindow));
            gtk_widget_show_all(g_pWindow);
        }
    }
}

/**
 * @callback_method_impl{FNRTTHREAD,
 *      Gtk App event loop handler.}
 */
static DECLCALLBACK(int) vbwlGtkWorkerThreadProc(RTTHREAD ThreadSelf, void *pvUser)
{
    int rc;
    GtkApplication * const pApp = gtk_application_new("org.virtualbox.vboxwl", G_APPLICATION_FLAGS_NONE);
    if (RT_VALID_PTR(pApp))
    {
        /* Signal parent thread that we've initialized successfully. */
        RTThreadUserSignal(ThreadSelf);

        /* Create app visual instance when ready. */
        g_signal_connect(pApp, "activate", G_CALLBACK(vbwlGtkAppStart), pvUser);

        /* Run gtk main loop. */
        rc = g_application_run(G_APPLICATION(pApp), 0, NULL);
        /** @todo rc is not an IPRT status, but an exit status, which should be kind of
         *        compatible... It is not forwarded to anyone, but has %Rrc applied
         *        to it. */

        g_object_unref(pApp);
    }
    else
        rc = VERR_NO_MEMORY;
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
        rc = g_oDataIpc->flow(vbcl::ipc::data::HGCopyFlow, hIpcSession);
    else if (g_enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST)
        rc = g_oDataIpc->flow(vbcl::ipc::data::GHAnnounceAndCopyFlow, hIpcSession);
    else if (g_enmSessionType == VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST)
        rc = g_oDataIpc->flow(vbcl::ipc::data::GHCopyFlow, hIpcSession);

    return rc;
}

/**
 * Get IPC server socket name prefix depending on session type.
 *
 * @returns Prefix name or NULL if session type is unknown.
 */
static const char *vboxwl_ipc_srv_name_prefix(void)
{
    const char *pcszPrefix;

    switch (g_enmSessionType)
    {
        case VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST:
        case VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST:
        case VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST:
        {
            pcszPrefix = VBOXWL_SRV_NAME_PREFIX_CLIP;
            break;
        }

        default:
            pcszPrefix = NULL;
    }

    return pcszPrefix;
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
    const char *pcszPrefix = vboxwl_ipc_srv_name_prefix();

    AssertPtrReturn(phIpcSession, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcszPrefix, VERR_INVALID_POINTER);

    rc = vbcl_wayland_hlp_gtk_ipc_srv_name(pcszPrefix, szIpcServerName, sizeof(szIpcServerName));
    if (RT_SUCCESS(rc))
        rc = RTLocalIpcSessionConnect(phIpcSession, szIpcServerName, 0);

    VBClLogInfo("session %u: ipc connect: rc=%Rrc\n", g_idSession, rc);

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
    int rc = VBClStartThread(&g_AppThread, vbwlGtkWorkerThreadProc, "gtk-app", NULL); /* (IPRT wrapper function) */
    if (RT_SUCCESS(rc))
    {
        RTLOCALIPCSESSION hIpcSession = NIL_RTLOCALIPCSESSION;
        rc = vboxwl_connect_ipc(&hIpcSession);
        if (RT_SUCCESS(rc))
        {
            g_oDataIpc = new vbcl::ipc::data::DataIpc();
            if (RT_VALID_PTR(g_oDataIpc))
            {
                g_oDataIpc->init(vbcl::ipc::FLOW_DIRECTION_CLIENT, g_idSession);

                rc = vboxwl_ipc_flow(hIpcSession);
                VBClLogVerbose(2, "session %u: ended with rc=%Rrc\n", g_idSession, rc);

                /* Ask Gtk app to quit if IPC task has failed. */
                if (RT_FAILURE(rc))
                    g_nsGtkQuit = RTTimeMilliTS();

                /* Wait for app thread termination first, it uses resources we just created. */
                int rcThread = -1;
                rc = RTThreadWait(g_AppThread, RT_MS_30SEC, &rcThread);
                VBClLogInfo("session %u: gtk app exited: rc=%Rrc, rcThread=%Rrc\n", g_idSession, rc, rcThread);

                g_oDataIpc->reset();
                delete g_oDataIpc;
            }
            else
                VBClLogError("session %u: unable to create ipc clipboard object\n", g_idSession);

            rc = RTLocalIpcSessionClose(hIpcSession);
            VBClLogVerbose(1, "session %u: ipc disconnected: rc=%Rrc\n", g_idSession, rc);
        }
    }
    else
        VBClLogError("session %u: gtk app start: rc=%Rrc\n", g_idSession, rc);

    return rc;
}

/**
 * Print command line usage.
 */
static void vboxwl_usage(void)
{
    RTPrintf(VBOX_PRODUCT " %s " VBOX_VERSION_STRING "\n"
             "Copyright (C) 2005-" VBOX_C_YEAR " " VBOX_VENDOR "\n\n", RTProcShortName());

    RTPrintf("Usage: %s [ %s %s|%s|%s ] | [--help|-h] [--version|-V] [--verbose|-v]\n\n",
             RTProcShortName(), VBOXWL_ARG_SESSION_ID, VBOXWL_ARG_CLIP_HG_COPY,
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
    RTPrintf("  --verbosity=<level>  Sets the verbosity level\n");
    RTPrintf("  --version            Print version number and exit\n");
    RTPrintf("  --help               Print this message\n");
    RTPrintf("\n");
}

/**
 * Check if active Wayland session is running.
 *
 * This check is used in order to detect whether X11 or Wayland version of
 * VBoxClient should be started when user logs-in. It will print out either WL
 * or X11. Startup script(s) should rely on this output.
 */
static RTEXITCODE vboxwl_check(void)
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
    return RTEXITCODE_SUCCESS;
}


/** Initialization step shortcut macro.
 *
 * Try to run initialization function if previous step was successful and print error if it occurs.
 *
 * @param _fn       A function to call.
 * @param _error    Error message to print if function fails.
 * @todo r=bird: This macro is making the code unreadable.
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
    /* Custom log prefix to be used for logger instance of this process. */
    static const char *s_pszLogPrefix = "vboxwl:";

    /* Initialize runtime. */
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgErrorExitFailure("RTR3InitExe failed: %Rrc", rc);

    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { VBOXWL_ARG_CLIP_HG_COPY,      'p', RTGETOPT_REQ_NOTHING },
        { VBOXWL_ARG_CLIP_GH_ANNOUNCE,  'a', RTGETOPT_REQ_NOTHING },
        { VBOXWL_ARG_CLIP_GH_COPY,      'c', RTGETOPT_REQ_NOTHING },
        { VBOXWL_ARG_SESSION_ID,        's', RTGETOPT_REQ_UINT32  },
        { "--check",                    'C', RTGETOPT_REQ_NOTHING },
        { "--verbose",                  'v', RTGETOPT_REQ_NOTHING },
        { "--verbosity",                'y', RTGETOPT_REQ_UINT32  },
        { "--help",                     'h', RTGETOPT_REQ_NOTHING },
        { "--version",                  'V', RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE           GetState;
    rc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0 /* fFlags */);
    AssertRCReturn(rc, RTMsgErrorExitFailure("RTGetOptInit failed: %Rrc", rc));

    int                     ch;
    RTGETOPTUNION           ValueUnion;
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'p':
                if (   g_enmSessionType != VBCL_WL_SESSION_TYPE_INVALID
                    && g_enmSessionType != VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST)
                    return RTMsgSyntax("Session type already set (%d): " VBOXWL_ARG_CLIP_HG_COPY, g_enmSessionType);
                g_enmSessionType = VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_GUEST;
                break;

            case 'a':
                if (   g_enmSessionType != VBCL_WL_SESSION_TYPE_INVALID
                    && g_enmSessionType != VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST)
                    return RTMsgSyntax("Session type already set (%d): " VBOXWL_ARG_CLIP_GH_ANNOUNCE, g_enmSessionType);
                g_enmSessionType = VBCL_WL_CLIPBOARD_SESSION_TYPE_ANNOUNCE_TO_HOST;
                break;

            case 'c':
                if (   g_enmSessionType != VBCL_WL_SESSION_TYPE_INVALID
                    && g_enmSessionType != VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST)
                    return RTMsgSyntax("Session type already set (%d): " VBOXWL_ARG_CLIP_GH_COPY, g_enmSessionType);
                g_enmSessionType = VBCL_WL_CLIPBOARD_SESSION_TYPE_COPY_TO_HOST;
                break;

            case 's':
                g_idSession = ValueUnion.u32;
                break;

            case 'C':
                return vboxwl_check();

            case 'h':
                vboxwl_usage();
                return RTEXITCODE_SUCCESS;

            case 'V':
                RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                return RTEXITCODE_SUCCESS;

            case 'v':
                g_cVerbosity++;
                break;

            case 'y':
                g_cVerbosity = RT_MIN(ValueUnion.u32, VBOXWL_VERBOSITY_MAX);
                break;

            case VINF_GETOPT_NOT_OPTION:
            case VERR_GETOPT_UNKNOWN_OPTION:
            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    /* Check that a session ID was specified. */
    if (!g_idSession)
        return RTMsgSyntax("No session ID given!");

    /*
     * Check for Gtk.
     */
    if (!VBGHDisplayServerTypeIsGtkAvailable())
        return RTMsgErrorExitFailure("Gtk3 library is required to run this tool, but can not be found\n");

    /* Initialize VBGL. */
    rc = VbglR3InitUser();
    AssertRCReturn(rc, RTMsgErrorExitFailure("VbglR3InitUser failed: %Rrc (unable to talk with vboxguest kernel module)", rc));

    /*
     * Setup logging.
     * Note! This does not work smoothly if someone tries to set VBOXCLIENT_RELEASE_LOG_DEST
     *       to a specific file, as the we will unlink the VBoxClient log file.  The someone
     *       will also have to set the append flag to prevent this.
     */
    /** @todo r=bird: Does this need to be fatal? */
    VBOXWL_INIT(VBClLogCreateEx("", false, false),      "cannot create logger instance");
    VBOXWL_INIT(VBClLogModify("stdout", g_cVerbosity),  "cannot setup log");
    VBClLogSetLogPrefix(s_pszLogPrefix);

    /*
     * Run the command.
     */
    /** @todo r=bird: VBOXWL_INIT? */
    VBOXWL_INIT(vboxwl_run_command(),                   "cannot run command");

    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}
