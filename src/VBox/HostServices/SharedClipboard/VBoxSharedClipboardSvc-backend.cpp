/* $Id: VBoxSharedClipboardSvc-backend.cpp 115049 2026-08-17 15:12:59Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard Service - Backend and extension bridge handling.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD
#include <VBox/log.h>
#include <VBox/vmm/vmmr3vtable.h> /* must be included before hgcmsvc.h */

#include <iprt/errcore.h>
#include <VBox/HostServices/VBoxClipboardExt.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>

#include <iprt/assert.h>
#include <iprt/string.h>

#include "VBoxSharedClipboardSvc-internal.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int shClSvcBackendHostCallback(uint32_t u32Function, PSHCLEXTPARMS pvParms, uint32_t cbParms);
static DECLCALLBACK(int) shClSvcBackendExtensionCallback(uint32_t u32Function, uint32_t u32Format,
                                                         void *pvData, uint32_t cbData);


/**
 * Returns the Shared Clipboard backend in use.
 *
 * @returns Pointer to backend instance.
 */
PSHCLBACKEND ShClSvcGetBackend(void)
{
    return &g_ShClSvc.Backend;
}


static int shClSvcBackendHostCallback(uint32_t u32Function, PSHCLEXTPARMS pvParms, uint32_t cbParms)
{
    LogFlowFunc(("u32Function=%RU32, pvParms=%p, cbParms=%RU32\n", u32Function, pvParms, cbParms));

    int rc;
    if (g_ShClSvc.ExtState.pfnExtension)
        rc = g_ShClSvc.ExtState.pfnExtension(g_ShClSvc.ExtState.pvExtension, u32Function, pvParms, cbParms);
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("Returning rc=%Rrc\n", rc));
    return rc;
}


int shClSvcBackendInit(VBOXHGCMSVCFNTABLE *pTable)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);

    parms.u.ReadWriteData.pBackend = ShClSvcGetBackend();
    parms.u.ReadWriteData.pTable = pTable;

    /* The backend in Main calls: ShClBackendInit(ShClSvcGetBackend(), pTable); */
    return shClSvcBackendHostCallback(VBOX_CLIPBOARD_EXT_FN_BACKEND_INIT, &parms, sizeof(parms));
}


int shClSvcBackendConnect(PSHCLCLIENT pClient)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);

    parms.u.ReadWriteData.pBackend = ShClSvcGetBackend();
    parms.u.ReadWriteData.pClient = pClient;
    /* The backend in Main calls: ShClBackendConnect(pClient->pBackend, pClient); */
    return shClSvcBackendHostCallback(VBOX_CLIPBOARD_EXT_FN_BACKEND_CONNECT, &parms, sizeof(parms));
}


int shClSvcBackendSync(PSHCLCLIENT pClient)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);

    parms.u.ReadWriteData.pBackend = ShClSvcGetBackend();
    parms.u.ReadWriteData.pClient = pClient;

    /* The backend in Main calls: ShClBackendSync(pClient->pBackend, pClient); */
    return shClSvcBackendHostCallback(VBOX_CLIPBOARD_EXT_FN_BACKEND_SYNC, &parms, sizeof(parms));
}


void shClSvcBackendDisconnect(PSHCLCLIENT pClient)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);

    parms.u.ReadWriteData.pClient = pClient;

    /* The backend in Main calls: ShClBackendDisconnect(pClient->pBackend, pClient); */
    shClSvcBackendHostCallback(VBOX_CLIPBOARD_EXT_FN_BACKEND_DISCONNECT, &parms, sizeof(parms));
}


void shClSvcBackendDestroy(void)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);

    parms.u.ReadWriteData.pBackend = ShClSvcGetBackend();

    /* The backend in Main calls: ShClBackendDestroy(ShClSvcGetBackend()); */
    shClSvcBackendHostCallback(VBOX_CLIPBOARD_EXT_FN_BACKEND_DESTROY, &parms, sizeof(parms));
}


/**
 * Reports clipboard formats to the guest.
 *
 * @note    Host backend callers must check if it's active (use
 *          ShClSvcIsBackendActive) before calling to prevent mixing up the
 *          VRDE clipboard.
 *
 * @returns VBox status code.
 * @param   pClient             Client to report clipboard formats to.
 * @param   fFormats            The formats to report (VBOX_SHCL_FMT_XXX), zero
 *                              is okay (empty the clipboard).
 * @param   enmSource           Source the reported formats came from.
 *
 * @thread  Backend thread.
 */
int shClSvcBackendReportFormatsToGuest(PSHCLCLIENT pClient, SHCLFORMATS fFormats, SHCLSOURCE enmSource)
{
    AssertPtrReturn(pClient, VERR_INVALID_POINTER);

    LogFlowFunc(("fFormats=%#x, enmSource=%RU32\n", fFormats, enmSource));

    /*
     * Check if the service mode allows this operation and whether the guest is
     * supposed to be reading from the host. Otherwise, silently ignore reporting
     * formats and return VINF_SUCCESS in order to do not trigger client
     * termination in shClSvcConnect().
     */
    uint32_t uMode = ShClSvcGetMode();
    if (   uMode == VBOX_SHCL_MODE_BIDIRECTIONAL
        || uMode == VBOX_SHCL_MODE_HOST_TO_GUEST)
    { /* likely */ }
    else
        return VINF_SUCCESS;

    fFormats = shClSvcHandleFormats(true /* fHostToGuest */, pClient, fFormats);

    SHCLEXTPARMS parms;
    RT_ZERO(parms);

    parms.u.ReportFormats.uFormats  = fFormats;
    parms.u.ReportFormats.pClient   = pClient;
    parms.u.ReportFormats.enmSource = enmSource;

    /* The backend in Main calls: ShClBackendReportFormatsToGuest(pClient->pBackend, pClient, fFormats); */
    int rc = shClSvcBackendHostCallback(VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_GUEST, &parms, sizeof(parms));

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Reporting formats %#x to guest failed with %Rrc\n", fFormats, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}


int ShClSvcReportFormats(PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
    return shClSvcBackendReportFormatsToGuest(pClient, fFormats, SHCLSOURCE_LOCAL);
}


int shClSvcBackendReportFormatsToHost(PSHCLCLIENT pClient, SHCLFORMATS fFormats)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);

    parms.u.ReportFormats.uFormats = fFormats;
    parms.u.ReportFormats.pClient  = pClient;

    /* The backend in Main calls: ShClBackendReportFormats(pClient->pBackend, pClient, fFormats); */
    return shClSvcBackendHostCallback(VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST, &parms, sizeof(parms));
}


int shClSvcBackendReadData(PSHCLCLIENT pClient, SHCLFORMAT uFormat, void *pvData, uint32_t cbData, uint32_t *pcbActual)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);

    parms.u.ReadWriteData.uFormat  = uFormat;
    parms.u.ReadWriteData.pvData   = pvData;
    parms.u.ReadWriteData.cbData   = cbData;
    parms.u.ReadWriteData.cbActual = pcbActual ? *pcbActual : 0;
    parms.u.ReadWriteData.pClient  = pClient;

    /* Read clipboard data from the extension. */
    int rc = shClSvcBackendHostCallback(VBOX_CLIPBOARD_EXT_FN_DATA_READ, &parms, sizeof(parms));
    if (   RT_SUCCESS(rc)
        && pcbActual)
        *pcbActual = parms.u.ReadWriteData.cbActual;

    return rc;
}


static int shClSvcBackendReadVrdeData(PSHCLCLIENT pClient, SHCLFORMAT uFormat, void *pvData, uint32_t cbData)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);

    parms.u.ReadWriteData.uFormat = uFormat;
    parms.u.ReadWriteData.pvData  = pvData;
    parms.u.ReadWriteData.cbData  = cbData;
    parms.u.ReadWriteData.pClient = pClient;

    /* Read clipboard data from the VRDE extension. */
    return shClSvcBackendHostCallback(VBOX_CLIPBOARD_EXT_FN_DATA_READ_VRDE, &parms, sizeof(parms));
}


int shClSvcBackendWriteData(PSHCLCLIENT pClient, PSHCLCLIENTCMDCTX pCmdCtx, SHCLFORMAT uFormat, void *pvData, uint32_t cbData)
{
    SHCLEXTPARMS parms;
    RT_ZERO(parms);

    parms.u.ReadWriteData.uFormat = uFormat;
    parms.u.ReadWriteData.pvData  = pvData;
    parms.u.ReadWriteData.cbData  = cbData;
    parms.u.ReadWriteData.pClient = pClient;
    parms.u.ReadWriteData.pCmdCtx = pCmdCtx;

    /* The backend in Main calls: ShClBackendWriteData(pClient->pBackend, pClient, pCmdCtx, fFormats, pvData, cbData); */
    return shClSvcBackendHostCallback(VBOX_CLIPBOARD_EXT_FN_DATA_WRITE, &parms, sizeof(parms));
}


static DECLCALLBACK(int) shClSvcBackendExtensionCallback(uint32_t u32Function, uint32_t u32Format, void *pvData, uint32_t cbData)
{
    LogFlowFunc(("u32Function=%RU32\n", u32Function));

    int rc = VINF_SUCCESS;

    shClSvcLock();

    /* Figure out if the client in charge for the service extension still is connected. */
    PSHCLCLIENT pClient = g_ShClSvc.pActiveClient;
    if (pClient)
    {
        switch (u32Function)
        {
            /* The service extension announces formats to the host. */
            case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST:
            {
                LogFlowFunc(("VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST: fReadingData=%RTbool\n",
                             g_ShClSvc.ExtState.fReadingData));
                if (!g_ShClSvc.ExtState.fReadingData)
                    rc = shClSvcBackendReportFormatsToGuest(pClient, u32Format, SHCLSOURCE_REMOTE);
                else
                {
                    g_ShClSvc.ExtState.fDelayedAnnouncement = true;
                    g_ShClSvc.ExtState.fDelayedFormats = u32Format;
                    rc = VINF_SUCCESS;
                }
                break;
            }

            /* The service extension wants read data from the guest. */
            case VBOX_CLIPBOARD_EXT_FN_DATA_READ:
            {
                rc = shClSvcBackendReadVrdeData(pClient, u32Format, pvData, cbData);
                break;
            }

            default:
                /* Just skip other messages. */
                break;
        }
    }
    else
        rc = VERR_NOT_FOUND;

    shClSvcUnlock();

    LogFlowFuncLeaveRC(rc);
    return rc;
}


DECLCALLBACK(int) shClSvcRegisterExtension(void *, PFNHGCMSVCEXT pfnExtension, void *pvExtension)
{
    LogFlowFunc(("pfnExtension=%p\n", pfnExtension));

    SHCLEXTPARMS parms;
    RT_ZERO(parms);

    /*
     * Reference counting for service extension registration is done a few
     * layers up (in ConsoleVRDPServer::ClipboardCreate()).
     */

    shClSvcLock();
    int rc = VINF_SUCCESS;

    if (pfnExtension)
    {
        /* Install extension. */
        g_ShClSvc.ExtState.pfnExtension = pfnExtension;
        g_ShClSvc.ExtState.pvExtension  = pvExtension;

        parms.u.SetCallback.pfnCallback = shClSvcBackendExtensionCallback;

        rc = shClSvcBackendHostCallback(VBOX_CLIPBOARD_EXT_FN_SET_CALLBACK, &parms, sizeof(parms));

        LogRel2(("Shared Clipboard: registered service extension\n"));
    }
    else
    {
        (void) shClSvcBackendHostCallback(VBOX_CLIPBOARD_EXT_FN_SET_CALLBACK, &parms, sizeof(parms));

        /*
         * When a guest VM using the Shared Clipboard shuts down Console::i_powerDown()
         * will call HGCMHostUnregisterServiceExtension() and then VMMDev::hgcmShutdown()
         * shortly thereafter. The former call lands here to unregister the extension and
         * the latter calls shClSvcUnload() to unload the SharedClipboardSvc.so shared object
         * and tear down the backend infrastructure via ShClBackendDestroy(). Unregistering
         * the extension disables the host callback which means shClSvcBackendHostCallback() isn't
         * able to call ShClBackendDestroy() in shClSvcUnload() so we do that here while the
         * host callback is still available.
         */
        shClSvcBackendDestroy();

        /* Uninstall extension. */
        g_ShClSvc.ExtState.pvExtension  = NULL;
        g_ShClSvc.ExtState.pfnExtension = NULL;

        LogRel2(("Shared Clipboard: de-registered service extension\n"));
    }

    shClSvcUnlock();

    return rc;
}
