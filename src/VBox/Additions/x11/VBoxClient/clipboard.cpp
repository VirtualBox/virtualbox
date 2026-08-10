/* $Id: clipboard.cpp 114748 2026-07-21 20:16:49Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Common Shared Clipboard wrapper service.
 */

/*
 * Copyright (C) 2007-2026 Oracle and/or its affiliates.
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
#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>

#include <VBox/log.h>
#include <VBox/VBoxGuestLib.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/SharedClipboard-x11.h>

#include "VBoxClient.h"

#include "clipboard.h"


/** Shared Clipboard context.
 *  Only one context is supported at a time for now. */
SHCLCONTEXT g_Ctx;
/** Whether we're using wayland or not. */
static bool g_fVBClWayland = false;


/**
 * Checks whether @a enmType and environment means we should use wayland for the
 * clipboard or not.
 *
 * This is shared with the --session-detect2 command.
 */
bool VBClClipboardShouldUseWayland(VBGHDISPLAYSERVERTYPE enmType)
{
    /* This is mainly about deciding whether we should use X11 service mode
       or wayland.  Pure wayland leaves no options, of course. */
    bool fWayland = enmType == VBGHDISPLAYSERVERTYPE_PURE_WAYLAND;

    /* In case of XWayland, X11 version of VBoxClient still can
     * work, however with some DEs, such as Plasma on Wayland,
     * this will no longer work. Detect such DEs here. */
    if (enmType == VBGHDISPLAYSERVERTYPE_XWAYLAND)
    {
        const char *pszDesktopSession = RTEnvGet(VBGH_ENV_DESKTOP_SESSION);
        fWayland = RT_VALID_PTR(pszDesktopSession)
                && (   RTStrIStr(pszDesktopSession, "plasmawayland") != NULL
                    || RTStrIStr(pszDesktopSession, "plasma")        != NULL);
    }
    return fWayland;
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnInit}
 */
static DECLCALLBACK(int) vbclShClInit(void)
{
    int rc;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    rc = ShClTransferCtxInit(&g_Ctx.TransferCtx);
#else
    rc = VINF_SUCCESS;
#endif

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnWorker}
 */
static DECLCALLBACK(int) vbclShClWorker(bool volatile *pfShutdown)
{
    RT_NOREF(pfShutdown);

    int rc;
    g_fVBClWayland = false;
    VBGHDISPLAYSERVERTYPE const enmDispType = VBClGetDisplayServerTypeResolveAuto();
    if (VBClClipboardShouldUseWayland(enmDispType))
    {
        g_fVBClWayland = true;
#ifdef VBOX_WITH_WAYLAND_ADDITIONS
        rc = VBClClipboardWaylandInit(&g_Ctx);
        if (RT_SUCCESS(rc))
        {
            RTThreadUserSignal(RTThreadSelf()); /* signal the main thread that we're ready */
            rc = VBClClipboardWaylandMain(&g_Ctx, pfShutdown);
        }
#else
        VBClLogError("Wayland integration is not enabled in this build!\n");
#endif
    }
    else if (enmDispType == VBGHDISPLAYSERVERTYPE_X11 || enmDispType == VBGHDISPLAYSERVERTYPE_XWAYLAND)
    {
        rc = VBClX11ClipboardInit();
        if (RT_SUCCESS(rc))
        {
            RTThreadUserSignal(RTThreadSelf()); /* signal the main thread that we're ready */
            rc = VBClX11ClipboardMain();
        }
    }
    else
    {
        VBClLogError("Unexpected VBGHDISPLAYSERVERTYPE value: %d\n", enmDispType);
        return VINF_SUCCESS; /* prevent auto restart by daemon script */
    }

    if (RT_FAILURE(rc))
        VBClLogError("Service terminated abnormally with %Rrc\n", rc);

    if (rc == VERR_HGCM_SERVICE_NOT_FOUND)
        rc = VINF_SUCCESS; /* Prevent automatic restart by daemon script if host service not available. */

    return rc;
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnStop}
 */
static DECLCALLBACK(void) vbclShClStop(void)
{
#ifdef VBOX_WITH_WAYLAND_ADDITIONS
    if (g_fVBClWayland)
        VBClClipboardWaylandStop(&g_Ctx);
#endif

    /*
     * Disconnect from the host service.
     *
     * This will also send a VBOX_SHCL_HOST_MSG_QUIT from the host so that we
     * can break out from our message worker.
     */
    VbglR3ClipboardDisconnect(g_Ctx.CmdCtx.idClient);
    g_Ctx.CmdCtx.idClient = 0;
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnTerm}
 */
static DECLCALLBACK(int) vbclShClTerm(void)
{
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    ShClTransferCtxDestroy(&g_Ctx.TransferCtx);
#endif
    if (!g_fVBClWayland)
        VBClX11ClipboardDestroy();
#ifdef VBOX_WITH_WAYLAND_ADDITIONS
    else
        VBClClipboardWaylandTerm(&g_Ctx);
#endif

    return VINF_SUCCESS;
}

VBCLSERVICE const g_SvcClipboard =
{
    "shcl",                      /* szName */
    "Shared Clipboard",          /* pszDescription */
    ".vboxclient-clipboard",     /* pszPidFilePathTemplate */
    NULL,                        /* pszUsage */
    NULL,                        /* pszOptions */
    NULL,                        /* pfnOption */
    vbclShClInit,                /* pfnInit */
    vbclShClWorker,              /* pfnWorker */
    vbclShClStop,                /* pfnStop*/
    vbclShClTerm                 /* pfnTerm */
};

