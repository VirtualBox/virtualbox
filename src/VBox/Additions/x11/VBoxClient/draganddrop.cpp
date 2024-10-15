/* $Id: draganddrop.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - Common drag'n drop wrapper service.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#include <iprt/mem.h>

#include "VBox/HostServices/DragAndDropSvc.h"
#include "VBoxClient.h"
#include "draganddrop.h"


/** Pointer to the DnD interface class. */
static VBClDnDSvc *g_pSvc = NULL;


/**
 * @interface_method_impl{VBCLSERVICE,pfnInit}
 */
static DECLCALLBACK(int) vbclDnDInit(void)
{
    switch (VBClGetDisplayServerType())
    {
        case VBGHDISPLAYSERVERTYPE_X11:
            g_pSvc = new VBClX11DnDSvc();
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
static DECLCALLBACK(int) vbclDnDWorker(bool volatile *pfShutdown)
{
    AssertPtrReturn(g_pSvc, VERR_NOT_IMPLEMENTED);
    return g_pSvc->worker(pfShutdown);
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnStop}
 */
static DECLCALLBACK(void) vbclDnDStop(void)
{
    if (g_pSvc)
        g_pSvc->stop();
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnTerm}
 */
static DECLCALLBACK(int) vbclDnDTerm(void)
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

VBCLSERVICE g_SvcDragAndDrop =
{
    "dnd",                         /* szName */
    "Drag'n'Drop",                 /* pszDescription */
    ".vboxclient-draganddrop",     /* pszPidFilePathTemplate */
    NULL,                          /* pszUsage */
    NULL,                          /* pszOptions */
    NULL,                          /* pfnOption */
    vbclDnDInit,                   /* pfnInit */
    vbclDnDWorker,                 /* pfnWorker */
    vbclDnDStop,                   /* pfnStop*/
    vbclDnDTerm                    /* pfnTerm */
};

