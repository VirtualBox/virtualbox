/** $Id: clipboard.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - X11 Shared Clipboard - Main header.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_x11_VBoxClient_clipboard_h
#define GA_INCLUDED_SRC_x11_VBoxClient_clipboard_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/GuestHost/SharedClipboard-x11.h>
#include <VBox/VBoxGuestLib.h>
#include <iprt/thread.h>

/**
 * Callback to notify guest that host has new clipboard data in the specified formats.
 *
 * @returns VBox status code.
 * @param   fFormats        The formats available.
 *                          Optional and can be NULL.
 */
typedef DECLCALLBACKTYPE(int, FNHOSTCLIPREPORTFMTS, (SHCLFORMATS fFormats));
typedef FNHOSTCLIPREPORTFMTS *PFNHOSTCLIPREPORTFMTS;

/**
 * Callback to notify guest that host wants to read clipboard data in specified format.
 *
 * @returns VBox status code.
 * @param   uFmt            The format in which the data should be read
 *                          (VBOX_SHCL_FMT_XXX).
 */
typedef DECLCALLBACKTYPE(int, FNHOSTCLIPREAD, (SHCLFORMAT uFmt));
typedef FNHOSTCLIPREAD *PFNHOSTCLIPREAD;


/**
 * Struct keeping am X11 Shared Clipboard context.
 */
struct SHCLCONTEXT
{
    /** Client command context */
    VBGLR3SHCLCMDCTX     CmdCtx;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Associated transfer data. */
    SHCLTRANSFERCTX      TransferCtx;
#endif
    /** Event source for waiting for X11 request responses in the VbglR3 clipboard event loop. */
    SHCLEVENTSOURCE      EventSrc;
    union
    {
        /** X11 clipboard context. */
        SHCLX11CTX       X11;
        /** @todo Wayland clipboard context goes here. */
        /* SHCLWAYLANDCTX Wl; */
    };
};

/** Shared Clipboard context.
 *  Only one context is supported at a time for now. */
extern SHCLCONTEXT g_Ctx;

/**
 * Create thread and wait until it started.
 *
 * @returns IPRT status code.
 * @param   pThread     Pointer to thread data.
 * @param   pfnThread   Pointer to thread main loop function.
 * @param   pszName     Thread name.
 * @param   pvUser      User data.
 */
RTDECL(int) VBClClipboardThreadStart(PRTTHREAD pThread, PFNRTTHREAD pfnThread, const char *pszName, void *pvUser);

/**
 * Read and process one event from the host clipboard service.
 *
 * @returns VBox status code.
 * @param   pCtx                Host Shared Clipboard service connection context.
 * @param   pfnHGClipReport     A callback to notify guest about new content in host clipboard.
 * @param   pfnGHClipRead       A callback to notify guest when host requests guest clipboard content.
 */
RTDECL(int) VBClClipboardReadHostEvent(PSHCLCONTEXT pCtx, const PFNHOSTCLIPREPORTFMTS pfnHGClipReport,
                                              const PFNHOSTCLIPREAD pfnGHClipRead);

/**
 * Read entire host clipboard buffer in given format.
 *
 * This function will allocate clipboard buffer of necessary size and
 * place host clipboard content into it. Buffer needs to be freed by caller.
 *
 * @returns VBox status code.
 * @param   pCtx            Host Shared Clipboard service connection context.
 * @param   uFmt            Format in which data should be read.
 * @param   ppvData         Newly allocated output buffer (should be freed by caller).
 * @param   pcbData         Output buffer size.
 */
RTDECL(int) VBClClipboardReadHostClipboard(PVBGLR3SHCLCMDCTX pCtx, SHCLFORMAT uFmt, void **ppvData, uint32_t *pcbData);

#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_clipboard_h */
