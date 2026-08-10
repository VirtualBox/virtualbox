/* $Id: wayland-helper-gtk.cpp 114747 2026-07-21 19:17:44Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Gtk helper for Wayland.
 *
 * This module implements Shared Clipboard and Drag-n-Drop
 * support for Wayland guests using the basic protocols.
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

#include <iprt/pipe.h>
#include <iprt/string.h>

#include <VBox/GuestHost/DisplayServerType.h>

#include "VBoxClient.h"
#include "clipboard.h"
#include "wayland-helper.h"


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
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_clip_init(void)
{
    VBCL_LOG_CALLBACK;

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnTerm}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_clip_term(void)
{
    PSHCLCONTEXT const pShClCtx = &g_Ctx;

    /** @todo move this into the GuestHost code... */
    if (pShClCtx->Wl.hPipeClipboardSet != NIL_RTPIPE)
    {
        RTPipeClose(pShClCtx->Wl.hPipeClipboardSet);
        pShClCtx->Wl.hPipeClipboardSet = NIL_RTPIPE;
    }
    if (pShClCtx->Wl.hProcClipboardSet != NIL_RTPROCESS)
    {
        VbghWaylandPopupTerminateAndWaitForChild(pShClCtx->Wl.hProcClipboardSet, "--clipboard-set",
                                                 RT_MS_1SEC, RT_MS_5SEC, 3, NULL);
        pShClCtx->Wl.hProcClipboardSet = NIL_RTPROCESS;
    }

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnSetClipboardCtx}
 */
static DECLCALLBACK(void) vbcl_wayland_hlp_gtk_clip_set_ctx(PSHCLCONTEXT pCtx)
{
    RT_NOREF(pCtx);
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnPopup}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_clip_popup(void)
{
    PSHCLCONTEXT const pShClCtx = &g_Ctx;
    LogRel3(("%s:\n", __func__));
    return VBClClipboardWaylandPopupGetAll(pShClCtx);
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_CLIPBOARD,pfnHGClipReport}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_clip_hg_report(PSHCLCONTEXT pCtx, SHCLFORMATS fFormats)
{
    LogRel3(("%s:\n", __func__));
    return VBClClipboardWaylandPopupSetAll(pCtx, fFormats);
}


/***********************************************************************
 * DnD.
 **********************************************************************/

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_DND,pfnInit}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_dnd_init(void)
{
    VBCL_LOG_CALLBACK;

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBCLWAYLANDHELPER_DND,pfnTerm}
 */
static DECLCALLBACK(int) vbcl_wayland_hlp_gtk_dnd_term(void)
{
    VBCL_LOG_CALLBACK;

    return VINF_SUCCESS;
}


/** GTK helper callbacks. */
const VBCLWAYLANDHELPER g_WaylandHelperGtk =
{
    /* .pszName  = */ "wayland-gtk",
    /* .pfnProbe = */ vbcl_wayland_hlp_gtk_probe,
    /* .clip     = */
    {
        /* .pfnInit            = */ vbcl_wayland_hlp_gtk_clip_init,
        /* .pfnTerm            = */ vbcl_wayland_hlp_gtk_clip_term,
        /* .pfnSetClipboardCtx = */ vbcl_wayland_hlp_gtk_clip_set_ctx,
        /* .pfnPopup           = */ vbcl_wayland_hlp_gtk_clip_popup,
        /* .pfnHGClipReport    = */ vbcl_wayland_hlp_gtk_clip_hg_report,
        /* .pfnGHClipRead      = */ NULL,
    },
    /* .dnd      = */
    {
        /* .pfnInit = */            vbcl_wayland_hlp_gtk_dnd_init,
        /* .pfnTerm = */            vbcl_wayland_hlp_gtk_dnd_term,
    }
};
