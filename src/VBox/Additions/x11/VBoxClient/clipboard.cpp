/** $Id: clipboard.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - Common Shared Clipboard wrapper service.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include "clipboard-x11.h"


/** Shared Clipboard context.
 *  Only one context is supported at a time for now. */
SHCLCONTEXT g_Ctx;


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

    int rc = VINF_SUCCESS;

    if (   VBClGetDisplayServerType() == VBGHDISPLAYSERVERTYPE_X11
        /* If Wayland w/ X fallback support is installed (also called XWayland), prefer using the X clipboard for now. */
        || VBClGetDisplayServerType() == VBGHDISPLAYSERVERTYPE_XWAYLAND)
    {
        rc = VBClX11ClipboardInit();
        if (RT_SUCCESS(rc))
        {
            /* Let the main thread know that it can continue spawning services. */
            RTThreadUserSignal(RTThreadSelf());

            rc = VBClX11ClipboardMain();
        }
    }
    else if (VBClGetDisplayServerType() == VBGHDISPLAYSERVERTYPE_PURE_WAYLAND)
    {
        VBClLogError("Shared Clipboard for Wayland not supported yet!\n");
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
    /* Disconnect from the host service.
     * This will also send a VBOX_SHCL_HOST_MSG_QUIT from the host so that we can break out from our message worker. */
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
    VBClX11ClipboardDestroy();

    return VINF_SUCCESS;
}

VBCLSERVICE g_SvcClipboard =
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

