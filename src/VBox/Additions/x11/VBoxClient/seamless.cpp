/* $Id: seamless.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - Common seamless mode wrapper service.
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


/*********************************************************************************************************************************
*   Header files                                                                                                                 *
*********************************************************************************************************************************/
#include <new>

#include <X11/Xlib.h>

#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>

#include <VBox/log.h>
#include <VBox/VBoxGuestLib.h>

#include "VBoxClient.h"
#include "seamless.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/** Pointer to the DnD interface class. */
static VBClSeamlessSvc *g_pSvc = NULL;


/*********************************************************************************************************************************
*   Common functions                                                                                                             *
*********************************************************************************************************************************/

/**
 * Reports region updates to the host.
 *
 * @param   pRects              Pointer to array of regions to report.
 * @param   cRects              Number of regions in \a pRect.
 */
void VBClSeamlessSendRegionUpdate(RTRECT *pRects, size_t cRects)
{
    if (   cRects
        && !pRects)  /* Assertion */
    {
        VBClLogError(("Region update called with NULL pointer\n"));
        return;
    }
    VbglR3SeamlessSendRects(cRects, pRects);
}


/*********************************************************************************************************************************
*   Service wrapper                                                                                                              *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{VBCLSERVICE,pfnInit}
 */
static DECLCALLBACK(int) vbclSeamlessInit(void)
{
    switch (VBClGetDisplayServerType())
    {
        case VBGHDISPLAYSERVERTYPE_X11:
            g_pSvc = new VBClX11SeamlessSvc();
            break;

        case VBGHDISPLAYSERVERTYPE_PURE_WAYLAND:
            RT_FALL_THROUGH();
        default:
            return VERR_NOT_SUPPORTED;
    }

    if (!g_pSvc)
        return VERR_NO_MEMORY;

    return g_pSvc->init();
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnWorker}
 */
static DECLCALLBACK(int) vbclSeamlessWorker(bool volatile *pfShutdown)
{
    AssertPtrReturn(g_pSvc, VERR_NOT_IMPLEMENTED);
    return g_pSvc->worker(pfShutdown);
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnStop}
 */
static DECLCALLBACK(void) vbclSeamlessStop(void)
{
    if (g_pSvc)
        g_pSvc->stop();
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnTerm}
 */
static DECLCALLBACK(int) vbclSeamlessTerm(void)
{
    int rc = VINF_SUCCESS;

    if (g_pSvc)
    {
        rc = g_pSvc->term();

        delete g_pSvc;
        g_pSvc = NULL;
    }

    return rc;
}

VBCLSERVICE g_SvcSeamless =
{
    "seamless",                 /* szName */
    "Seamless Mode Support",    /* pszDescription */
    ".vboxclient-seamless",     /* pszPidFilePathTemplate */
    NULL,                       /* pszUsage */
    NULL,                       /* pszOptions */
    NULL,                       /* pfnOption */
    vbclSeamlessInit,           /* pfnInit */
    vbclSeamlessWorker,         /* pfnWorker */
    vbclSeamlessStop,           /* pfnStop*/
    vbclSeamlessTerm            /* pfnTerm */
};

