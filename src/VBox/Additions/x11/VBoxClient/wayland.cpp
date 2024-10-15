/* $Id: wayland.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - Wayland Desktop Environment assistant.
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

#include <iprt/asm.h>
#include <iprt/thread.h>

#include <VBox/HostServices/GuestPropertySvc.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>

#include "VBoxClient.h"
#include "clipboard.h"
#include "wayland-helper.h"

/** Polling interval for input focus monitoring task. */
#define VBCL_WAYLAND_WAIT_HOST_FOCUS_TIMEOUT_MS     (250)
/** Relax interval for input focus monitoring task. */
#define VBCL_WAYLAND_WAIT_HOST_FOCUS_RELAX_MS       (100)

/** List of available Wayland Desktop Environment helpers. Sorted in order of preference. */
static const VBCLWAYLANDHELPER *g_apWaylandHelpers[] =
{
    &g_WaylandHelperDcp,    /* Device Control Protocol helper. */
    &g_WaylandHelperGtk,    /* GTK helper. */
    NULL,                   /* Terminate list. */
};

/** Global flag to tell service to go shutdown when needed. */
static bool volatile g_fShutdown = false;

/** Selected helpers for Clipboard and Drag-and-Drop. */
static const VBCLWAYLANDHELPER *g_pWaylandHelperClipboard = NULL;
static const VBCLWAYLANDHELPER *g_pWaylandHelperDnd       = NULL;

/** Corresponding threads for host events handling. */
static RTTHREAD g_ClipboardThread;
static RTTHREAD g_DndThread;
static RTTHREAD g_HostInputFocusThread;

/**
 * Worker for Shared Clipboard events from host.
 *
 * @returns IPRT status code.
 * @param   hThreadSelf     IPRT thread handle.
 * @param   pvUser          User data (unused).
 */
static DECLCALLBACK(int) vbclWaylandClipboardWorker(RTTHREAD hThreadSelf, void *pvUser)
{
    SHCLCONTEXT ctx;
    int rc;

    RT_NOREF(pvUser);

    RT_ZERO(ctx);

    /* Connect to the host service. */
    rc = VbglR3ClipboardConnectEx(&ctx.CmdCtx, VBOX_SHCL_GF_0_CONTEXT_ID);
    /* Notify parent thread. */
    RTThreadUserSignal(hThreadSelf);

    if (RT_SUCCESS(rc))
    {
        /* Provide helper with host clipboard service connection handle. */
        g_pWaylandHelperClipboard->pfnSetClipboardCtx(&ctx.CmdCtx);

        /* Process host events. */
        while (!ASMAtomicReadBool(&g_fShutdown))
        {
            rc = VBClClipboardReadHostEvent(&ctx, g_pWaylandHelperClipboard->pfnHGClipReport,
                                            g_pWaylandHelperClipboard->pfnGHClipRead);
            if (RT_FAILURE(rc))
            {
                VBClLogInfo("cannot process host clipboard event, rc=%Rrc\n", rc);
                RTThreadSleep(RT_MS_1SEC / 2);
            }
        }

        VbglR3ClipboardDisconnectEx(&ctx.CmdCtx);
    }

    VBClLogVerbose(2, "clipboard thread, rc=%Rrc\n", rc);

    return rc;
}

/**
 * Worker for Drag-and-Drop events from host.
 *
 * @returns IPRT status code.
 * @param   hThreadSelf     IPRT thread handle.
 * @param   pvUser          User data (unused).
 */
static DECLCALLBACK(int) vbclWaylandDndWorker(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(pvUser);

    RTThreadUserSignal(hThreadSelf);
    return VINF_SUCCESS;
}

/**
 * Worker for VM window focus change polling thread.
 *
 * Some Wayland helpers need to be notified about VM
 * window focus change events. This is needed in order to
 * ask about, for example, if guest clipboard content was
 * changed since last user interaction. Such guest are not
 * able to notify host about clipboard content change and
 * needed to be asked implicitly.
 *
 * @returns IPRT status code.
 * @param   hThreadSelf     IPRT thread handle.
 * @param   pvUser          User data (unused).
 */
static DECLCALLBACK(int) vbclWaylandHostInputFocusWorker(RTTHREAD hThreadSelf, void *pvUser)
{
    int rc;

    RT_NOREF(pvUser);

    HGCMCLIENTID idClient;

    rc = VbglR3GuestPropConnect(&idClient);

    RTThreadUserSignal(hThreadSelf);

    if (RT_SUCCESS(rc))
    {
        while (!ASMAtomicReadBool(&g_fShutdown))
        {
            static char achBuf[GUEST_PROP_MAX_NAME_LEN];
            char *pszName = NULL;
            char *pszValue = NULL;
            char *pszFlags = NULL;
            bool fWasDeleted = false;
            uint64_t u64Timestamp = 0;

            rc = VbglR3GuestPropWait(idClient, VBOX_GUI_FOCUS_CHANGE_GUEST_PROP_NAME, achBuf, sizeof(achBuf), u64Timestamp,
                                     VBCL_WAYLAND_WAIT_HOST_FOCUS_TIMEOUT_MS, &pszName, &pszValue, &u64Timestamp,
                                     &pszFlags, NULL, &fWasDeleted);
            if (RT_SUCCESS(rc))
            {
                uint32_t fFlags = 0;

                VBClLogVerbose(1, "guest property change: name: %s, val: %s, flags: %s, fWasDeleted: %RTbool\n",
                               pszName, pszValue, pszFlags, fWasDeleted);

                if (RT_SUCCESS(GuestPropValidateFlags(pszFlags, &fFlags)))
                {
                    if (RTStrNCmp(pszName, VBOX_GUI_FOCUS_CHANGE_GUEST_PROP_NAME, GUEST_PROP_MAX_NAME_LEN) == 0)
                    {
                        if (fFlags & GUEST_PROP_F_RDONLYGUEST)
                        {
                            if (RT_VALID_PTR(g_pWaylandHelperClipboard))
                            {
                                if (RTStrNCmp(pszValue, "0", GUEST_PROP_MAX_NAME_LEN) == 0)
                                {
                                    rc = g_pWaylandHelperClipboard->pfnPopup();
                                    VBClLogVerbose(1, "trigger popup, rc=%Rrc\n", rc);
                                }
                            }
                            else
                                VBClLogVerbose(1, "will not trigger popup\n");
                        }
                        else
                            VBClLogError("property has invalid attributes\n");
                    }
                    else
                        VBClLogVerbose(1, "unknown property name '%s'\n", pszName);

                } else
                    VBClLogError("guest property change: name: %s, val: %s, flags: %s, fWasDeleted: %RTbool: bad flags\n",
                                 pszName, pszValue, pszFlags, fWasDeleted);

            } else if (   rc != VERR_TIMEOUT
                     && rc != VERR_INTERRUPTED)
            {
                VBClLogError("error on waiting guest property notification, rc=%Rrc\n", rc);
                RTThreadSleep(VBCL_WAYLAND_WAIT_HOST_FOCUS_RELAX_MS);
            }
        }
    }

    return rc;
}


/**
 * @interface_method_impl{VBCLSERVICE,pfnInit}
 */
static DECLCALLBACK(int) vbclWaylandInit(void)
{
    int rc = VERR_NOT_SUPPORTED;
    int idxHelper = 0;

    /** Custom log prefix to be used for logger instance of this process. */
    static const char *pszLogPrefix = "VBoxClient Wayland:";

    VBClLogSetLogPrefix(pszLogPrefix);

    /* Go through list of available helpers and try to pick up one. */
    while (g_apWaylandHelpers[idxHelper])
    {
        if (RT_VALID_PTR(g_apWaylandHelpers[idxHelper]->pfnProbe))
        {
            int fCaps = VBOX_WAYLAND_HELPER_CAP_NONE;

            VBClLogInfo("probing Wayland helper '%s'\n",
                         g_apWaylandHelpers[idxHelper]->pszName);

            fCaps = g_apWaylandHelpers[idxHelper]->pfnProbe();

            /* Try Clipboard helper. */
            if (   fCaps & VBOX_WAYLAND_HELPER_CAP_CLIPBOARD
                && !RT_VALID_PTR(g_pWaylandHelperClipboard))
            {
                if (RT_VALID_PTR(g_apWaylandHelpers[idxHelper]->pfnInit))
                {
                    rc = g_apWaylandHelpers[idxHelper]->pfnInit();
                    if (RT_SUCCESS(rc))
                        g_pWaylandHelperClipboard = g_apWaylandHelpers[idxHelper];
                    else
                        VBClLogError("Wayland helper '%s' cannot be initialized, skipping\n",
                                     g_apWaylandHelpers[idxHelper]->pszName);
                }
                else
                    VBClLogVerbose(1, "Wayland helper '%s' has no initializer, skipping\n",
                                   g_apWaylandHelpers[idxHelper]->pszName);
            }

            /* Try DnD helper. */
            if (   fCaps & VBOX_WAYLAND_HELPER_CAP_DND
                && !RT_VALID_PTR(g_pWaylandHelperDnd))
            {
                if (RT_VALID_PTR(g_apWaylandHelpers[idxHelper]->pfnInit))
                {
                    rc = g_apWaylandHelpers[idxHelper]->pfnInit();
                    if (RT_SUCCESS(rc))
                        g_pWaylandHelperDnd = g_apWaylandHelpers[idxHelper];
                    else
                        VBClLogError("Wayland helper '%s' cannot be initialized, skipping\n",
                                     g_apWaylandHelpers[idxHelper]->pszName);
                }
                else
                    VBClLogVerbose(1, "Wayland helper '%s' has no initializer, skipping\n",
                                   g_apWaylandHelpers[idxHelper]->pszName);
            }
        }

        /* See if we found all the needed helpers. */
        if (   RT_VALID_PTR(g_pWaylandHelperClipboard)
            && RT_VALID_PTR(g_pWaylandHelperDnd))
            break;

        idxHelper++;
    }

    /* Check result. */
    if (RT_VALID_PTR(g_pWaylandHelperClipboard))
        VBClLogInfo("found Wayland Shared Clipboard helper '%s'\n", g_pWaylandHelperClipboard->pszName);
    else
        VBClLogError("Wayland Shared Clipboard helper not found, clipboard sharing not possible\n");

    /* Check result. */
    if (RT_VALID_PTR(g_pWaylandHelperDnd))
        VBClLogInfo("found Wayland Drag-and-Drop helper '%s'\n", g_pWaylandHelperDnd->pszName);
    else
        VBClLogError("Wayland Drag-and-Drop helper not found, drag-and-drop not possible\n");

    return rc;
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnWorker}
 */
static DECLCALLBACK(int) vbclWaylandWorker(bool volatile *pfShutdown)
{
    int rc = VINF_SUCCESS;

    RT_NOREF(pfShutdown);

    VBClLogVerbose(1, "starting wayland worker thread\n");

    /* Start event loop for clipboard events processing from host. */
    if (RT_VALID_PTR(g_pWaylandHelperClipboard))
    {
        rc = VBClClipboardThreadStart(&g_ClipboardThread, vbclWaylandClipboardWorker, "wl-clip", NULL);
        VBClLogVerbose(1, "clipboard thread started, rc=%Rrc\n", rc);
    }

    /* Start event loop for DnD events processing from host. */
    if (   RT_SUCCESS(rc)
        && RT_VALID_PTR(g_pWaylandHelperDnd))
    {
        rc = VBClClipboardThreadStart(&g_DndThread, vbclWaylandDndWorker, "wl-dnd", NULL);
        VBClLogVerbose(1, "DnD thread started, rc=%Rrc\n", rc);
    }

    /* Start polling host input focus events. */
    if (RT_SUCCESS(rc))
    {
        rc = VBClClipboardThreadStart(&g_HostInputFocusThread, vbclWaylandHostInputFocusWorker, "wl-focus", NULL);
        VBClLogVerbose(1, "host input focus polling thread started, rc=%Rrc\n", rc);
    }

    /* Notify parent thread that we are successfully started. */
    RTThreadUserSignal(RTThreadSelf());

    if (RT_SUCCESS(rc))
    {
        int rcThread = VINF_SUCCESS;

        if (RT_VALID_PTR(g_pWaylandHelperClipboard))
        {
            rc = RTThreadWait(g_ClipboardThread, RT_INDEFINITE_WAIT, &rcThread);
            VBClLogVerbose(1, "clipboard thread finished, rc=%Rrc, rcThread=%Rrc\n", rc, rcThread);
        }

        if (   RT_SUCCESS(rc)
            && RT_VALID_PTR(g_pWaylandHelperDnd))
        {
            rc = RTThreadWait(g_DndThread, RT_INDEFINITE_WAIT, &rcThread);
            VBClLogVerbose(1, "DnD thread finished, rc=%Rrc, rcThread=%Rrc\n", rc, rcThread);
        }

        if (RT_SUCCESS(rc))
        {
            rc = RTThreadWait(g_HostInputFocusThread, RT_INDEFINITE_WAIT, &rcThread);
            VBClLogVerbose(1, "host input focus polling thread finished, rc=%Rrc, rcThread=%Rrc\n", rc, rcThread);
        }
    }

    VBClLogVerbose(1, "wayland worker thread finished, rc=%Rrc\n", rc);

    return rc;
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnStop}
 */
static DECLCALLBACK(void) vbclWaylandStop(void)
{
    VBClLogVerbose(1, "terminating wayland service: clipboard & DnD host event loops\n");

    /* This callback can be called twice (not good, needs to be fixed). Already was shut down? */
    if (ASMAtomicReadBool(&g_fShutdown))
        return;

    ASMAtomicWriteBool(&g_fShutdown, true);

    if (RT_VALID_PTR(g_pWaylandHelperClipboard))
        RTThreadPoke(g_ClipboardThread);

    if (RT_VALID_PTR(g_pWaylandHelperDnd))
        RTThreadPoke(g_DndThread);
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnTerm}
 */
static DECLCALLBACK(int) vbclWaylandTerm(void)
{
    int rc = VINF_SUCCESS;

    VBClLogVerbose(1, "shutting down wayland service: clipboard & DnD helpers\n");

    if (   RT_VALID_PTR(g_pWaylandHelperClipboard)
        && RT_VALID_PTR(g_pWaylandHelperClipboard->pfnTerm))
        rc = g_pWaylandHelperClipboard->pfnTerm();

    if (   RT_SUCCESS(rc)
        && RT_VALID_PTR(g_pWaylandHelperDnd)
        && RT_VALID_PTR(g_pWaylandHelperDnd->pfnTerm))
        rc = g_pWaylandHelperDnd->pfnTerm();

    return rc;
}

VBCLSERVICE g_SvcWayland =
{
    "wayland",              /* szName */
    "Wayland assistant",    /* pszDescription */
    ".vboxclient-wayland",  /* pszPidFilePathTemplate */
    NULL,                   /* pszUsage */
    NULL,                   /* pszOptions */
    NULL,                   /* pfnOption */
    vbclWaylandInit,        /* pfnInit */
    vbclWaylandWorker,      /* pfnWorker */
    vbclWaylandStop,        /* pfnStop */
    vbclWaylandTerm,        /* pfnTerm */
};
