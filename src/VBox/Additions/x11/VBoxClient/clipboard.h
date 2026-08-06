/* $Id: clipboard.h 114867 2026-08-06 15:19:51Z andreas.loeffler@oracle.com $ */
/** @file
 * Guest Additions - X11 Shared Clipboard - Main header.
 */

/*
 * Copyright (C) 2020-2026 Oracle and/or its affiliates.
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
#include <VBox/GuestHost/SharedClipboard-Wayland.h>
#include <VBox/VBoxGuestLib.h>
#include <iprt/thread.h>

/**
 * Called upon receiving a new clipboard format report from the host.
 *
 * @returns VBox status code.
 * @param   pCtx            Our shared clipboard context structure.
 * @param   fFormats        The formats available (VBOX_SHCL_FMT_XXX).
 */
typedef DECLCALLBACKTYPE(int, FNHOSTCLIPREPORTFMTS, (PSHCLCONTEXT pCtx, SHCLFORMATS fFormats));
/** Pointer to FNHOSTCLIPREPORTFMTS. */
typedef FNHOSTCLIPREPORTFMTS *PFNHOSTCLIPREPORTFMTS;

/**
 * Called upon receiving a read clipboard query from the host.
 *
 * @returns VBox status code.
 * @param   pCtx            Our shared clipboard context structure.
 * @param   uFmt            The format in which the data should be read (a
 *                          single VBOX_SHCL_FMT_XXX).
 */
typedef DECLCALLBACKTYPE(int, FNHOSTCLIPREAD, (PSHCLCONTEXT pCtx, SHCLFORMAT uFmt));
/** Pointer to FNHOSTCLIPREAD. */
typedef FNHOSTCLIPREAD *PFNHOSTCLIPREAD;

/**
 * The VBoxClient Shared Clipboard context structure.
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
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    /** Guest-side asynchronous X11 HTTP file-transfer state. */
    SHCLX11TRANSFERSTATE X11TransferState;
#endif
    union
    {
        /** X11 clipboard context. */
        SHCLX11CTX       X11;
        /** Wayland clipboard context. */
        SHCLWAYLANDCTX Wl;
    };
};

/** Checks if the revision indicates other side's clipboard ownership. */
#define SHCLWLCTX_REV_IS_OTHER(a_uRevision) (((a_uRevision) & 1) == 0)
/** Checks if the revision indicates our side's clipboard ownership. */
#define SHCLWLCTX_REV_IS_OUR(a_uRevision)   (((a_uRevision) & 1) == 1)

/** MIME type used for storing SHCLCONTEXT::Wl::uRevision and flag the (wayland)
 *  clipboard as our. */
#define VBOX_CLIPBOARD_MIME_TYPE_REVISION_NO    "application/x.virtualbox.vboxclient.revno"

/** Shared Clipboard context.
 *  Only one context is supported at a time for now. */
extern SHCLCONTEXT g_Ctx;

bool VBClClipboardShouldUseWayland(VBGHDISPLAYSERVERTYPE enmType);

int VBClClipboardReadHostEvent(PSHCLCONTEXT pCtx, PFNHOSTCLIPREPORTFMTS pfnHGClipReport, PFNHOSTCLIPREAD pfnGHClipRead);
int VBClClipboardReadHostClipboard(PSHCLCONTEXT pCtx, SHCLFORMAT uFmt, void **ppvData, uint32_t *pcbData);

int      VBClWaylandClipboardQueryHostData(PSHCLCONTEXT pCtx, const char *pszMimeType, void **ppvOutData, size_t *pcbOutData);
uint64_t VBClWaylandClipboardResetOurState(PSHCLCONTEXT pCtx, const char *pszCaller, struct SHCLWLOFFERSLOT *pOffer);

struct RTHANDLE;
int     VBClClipboardSerializeCache(SHCLCACHE const *pCache, SHCLFORMATS fFormats, struct RTHANDLE const *pHandleDst,
                                    RTMSINTERVAL cMsTimeout);
int     VBClClipboardDeserializeCache(struct RTHANDLE const *pHandleSrc, PSHCLCACHE pCache, SHCLFORMATS *pfFormats,
                                      RTMSINTERVAL cMsTimeout);

/* clipboard-x11.cpp */
int  VBClX11ClipboardInit(void);
int  VBClX11ClipboardDestroy(void);
int  VBClX11ClipboardMain(void);

/* clipboard-wayland.cpp */
int  VBClClipboardWaylandInit(SHCLCONTEXT *pCtx);
int  VBClClipboardWaylandMain(SHCLCONTEXT *pCtx, bool volatile *pfShutdown);
void VBClClipboardWaylandStop(SHCLCONTEXT *pCtx);
void VBClClipboardWaylandTerm(SHCLCONTEXT *pCtx);

/* clipboard-wayland-popup.cpp */
int  VBClClipboardWaylandPopupGetAll(SHCLCONTEXT *pCtx);
int  VBClClipboardWaylandPopupSetAll(SHCLCONTEXT *pCtx, SHCLFORMATS fFormats);


#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_clipboard_h */
