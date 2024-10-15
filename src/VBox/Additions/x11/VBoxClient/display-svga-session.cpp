/* $Id: display-svga-session.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - VMSVGA Desktop Environment user session assistant.
 *
 * This service connects to VBoxDRMClient IPC server, listens for
 * its commands and reports current display offsets to it. If IPC
 * server is not available, it forks legacy 'VBoxClient --vmsvga
 * service and terminates.
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

/*
 * This service is an IPC client for VBoxDRMClient daemon. It is also
 * a proxy bridge to a Desktop Environment specific code (so called
 * Desktop Environment helpers).
 *
 * Once started, it will try to enumerate and probe all the registered
 * helpers and if appropriate helper found, it will forward incoming IPC
 * commands to it as well as send helper's commands back to VBoxDRMClient.
 * Generic helper is a special one. It will be used by default if all the
 * other helpers are failed on probe. Moreover, generic helper provides
 * helper functions that can be used by other helpers as well. For example,
 * once Gnome3 Desktop Environment is running on X11, it will be also use
 * display offsets change notification monitor of a generic helper.
 *
 * Multiple instances of this daemon are allowed to run in parallel
 * with the following limitations.
 * A single user cannot run multiple daemon instances per single TTY device,
 * however, multiple instances are allowed for the user on different
 * TTY devices (i.e. in case if user runs multiple X servers on different
 * terminals). On multiple TTY devices multiple users can run multiple
 * daemon instances (i.e. in case of "switch user" DE configuration when
 * multiple X/Wayland servers are running on separate TTY devices).
 */

#include "VBoxClient.h"
#include "display-ipc.h"
#include "display-helper.h"

#include <VBox/VBoxGuestLib.h>

#include <iprt/localipc.h>
#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/path.h>
#include <iprt/linux/sysfs.h>


/** Handle to IPC client connection. */
VBOX_DRMIPC_CLIENT g_hClient = VBOX_DRMIPC_CLIENT_INITIALIZER;

/** IPC client handle critical section. */
static RTCRITSECT g_hClientCritSect;

/** List of available Desktop Environment specific display helpers. */
static const VBCLDISPLAYHELPER *g_apDisplayHelpers[] =
{
    &g_DisplayHelperGnome3,  /* GNOME3 helper. */
    &g_DisplayHelperGeneric, /* Generic helper. */
    NULL,                    /* Terminate list. */
};

/** Selected Desktop Environment specific display helper. */
static const VBCLDISPLAYHELPER *g_pDisplayHelper = NULL;

/** IPC connection session handle. */
static RTLOCALIPCSESSION g_hSession = 0;

/**
 * Callback for display offsets change events provided by Desktop Environment specific display helper.
 *
 * @returns IPRT status code.
 * @param   cDisplays   Number of displays which have changed offset.
 * @param   aDisplays   Display data.
 */
static DECLCALLBACK(int) vbclSVGASessionDisplayOffsetChanged(uint32_t cDisplays, struct VBOX_DRMIPC_VMWRECT *aDisplays)
{
    int rc = RTCritSectEnter(&g_hClientCritSect);

    if (RT_SUCCESS(rc))
    {
        rc = vbDrmIpcReportDisplayOffsets(&g_hClient, cDisplays, aDisplays);
        int rc2 = RTCritSectLeave(&g_hClientCritSect);
        if (RT_FAILURE(rc2))
            VBClLogError("vbclSVGASessionDisplayOffsetChanged: unable to leave critical session, rc=%Rrc\n", rc2);
    }
    else
        VBClLogError("vbclSVGASessionDisplayOffsetChanged: unable to enter critical session, rc=%Rrc\n", rc);

    return rc;
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnInit}
 */
static DECLCALLBACK(int) vbclSVGASessionInit(void)
{
    int rc;
    RTLOCALIPCSESSION hSession;
    int idxDisplayHelper = 0;

    /** Custom log prefix to be used for logger instance of this process. */
    static const char *pszLogPrefix = "VBoxClient VMSVGA:";

    VBClLogSetLogPrefix(pszLogPrefix);

    rc = RTCritSectInit(&g_hClientCritSect);
    if (RT_FAILURE(rc))
    {
        VBClLogError("unable to init locking, rc=%Rrc\n", rc);
        return rc;
    }

    /* Go through list of available Desktop Environment specific helpers and try to pick up one. */
    while (g_apDisplayHelpers[idxDisplayHelper])
    {
        if (g_apDisplayHelpers[idxDisplayHelper]->pfnProbe)
        {
            VBClLogInfo("probing Desktop Environment helper '%s'\n",
                         g_apDisplayHelpers[idxDisplayHelper]->pszName);

            rc = g_apDisplayHelpers[idxDisplayHelper]->pfnProbe();

            /* Found compatible helper. */
            if (RT_SUCCESS(rc))
            {
                /* Initialize it. */
                if (g_apDisplayHelpers[idxDisplayHelper]->pfnInit)
                {
                    rc = g_apDisplayHelpers[idxDisplayHelper]->pfnInit();
                }

                /* Some helpers might have no .pfnInit(), that's ok. */
                if (RT_SUCCESS(rc))
                {
                    /* Subscribe to display offsets change event. */
                    if (g_apDisplayHelpers[idxDisplayHelper]->pfnSubscribeDisplayOffsetChangeNotification)
                    {
                        g_apDisplayHelpers[idxDisplayHelper]->
                            pfnSubscribeDisplayOffsetChangeNotification(
                                vbclSVGASessionDisplayOffsetChanged);
                    }

                    g_pDisplayHelper = g_apDisplayHelpers[idxDisplayHelper];
                    break;
                }
                else
                    VBClLogError("compatible Desktop Environment "
                                 "helper has been found, but it cannot be initialized, rc=%Rrc\n", rc);
            }
        }

        idxDisplayHelper++;
    }

    /* Make sure we found compatible Desktop Environment specific helper. */
    if (g_pDisplayHelper)
    {
        VBClLogInfo("using Desktop Environment specific display helper '%s'\n",
                    g_pDisplayHelper->pszName);
    }
    else
    {
        VBClLogError("unable to find Desktop Environment specific display helper\n");
        return VERR_NOT_IMPLEMENTED;
    }

    /* Attempt to connect to VBoxDRMClient IPC server. */
    rc = RTLocalIpcSessionConnect(&hSession, VBOX_DRMIPC_SERVER_NAME, 0);
    if (RT_SUCCESS(rc))
    {
        g_hSession = hSession;
    }
    else
        VBClLogError("unable to connect to IPC server, rc=%Rrc\n", rc);

    /* We cannot initialize ourselves, start legacy service and terminate. */
    if (RT_FAILURE(rc))
    {
        /* Free helper resources. */
        if (g_pDisplayHelper->pfnUnsubscribeDisplayOffsetChangeNotification)
            g_pDisplayHelper->pfnUnsubscribeDisplayOffsetChangeNotification();

        if (g_pDisplayHelper->pfnTerm)
        {
            rc = g_pDisplayHelper->pfnTerm();
            VBClLogInfo("helper service terminated, rc=%Rrc\n", rc);
        }

        rc = VbglR3DrmLegacyClientStart();
        VBClLogInfo("starting legacy service, rc=%Rrc\n", rc);
        /* Force return status, so parent thread wont be trying to start worker thread. */
        rc = VERR_NOT_AVAILABLE;
    }

    return rc;
}

/**
 * A callback function which is triggered on IPC data receive.
 *
 * @returns IPRT status code.
 * @param   idCmd   DRM IPC command ID.
 * @param   pvData  DRM IPC command payload.
 * @param   cbData  Size of DRM IPC command payload.
 */
static DECLCALLBACK(int) vbclSVGASessionRxCallBack(uint8_t idCmd, void *pvData, uint32_t cbData)
{
    VBOXDRMIPCCLTCMD enmCmd =
        (idCmd > VBOXDRMIPCCLTCMD_INVALID && idCmd < VBOXDRMIPCCLTCMD_MAX) ?
            (VBOXDRMIPCCLTCMD)idCmd : VBOXDRMIPCCLTCMD_INVALID;

    int rc = VERR_INVALID_PARAMETER;

    AssertReturn(pvData,            VERR_INVALID_PARAMETER);
    AssertReturn(cbData,            VERR_INVALID_PARAMETER);
    AssertReturn(g_pDisplayHelper,  VERR_INVALID_PARAMETER);

    switch (enmCmd)
    {
        case VBOXDRMIPCCLTCMD_SET_PRIMARY_DISPLAY:
        {
            if (g_pDisplayHelper->pfnSetPrimaryDisplay)
            {
                PVBOX_DRMIPC_COMMAND_SET_PRIMARY_DISPLAY pCmd = (PVBOX_DRMIPC_COMMAND_SET_PRIMARY_DISPLAY)pvData;
                static uint32_t idPrimaryDisplayCached = VBOX_DRMIPC_MONITORS_MAX;

                if (   pCmd->idDisplay < VBOX_DRMIPC_MONITORS_MAX
                    && idPrimaryDisplayCached != pCmd->idDisplay)
                {
                    rc = g_pDisplayHelper->pfnSetPrimaryDisplay(pCmd->idDisplay);
                    /* Update cache. */
                    idPrimaryDisplayCached = pCmd->idDisplay;
                }
                else
                    VBClLogVerbose(1, "do not set %u as a primary display\n", pCmd->idDisplay);
            }

            break;
        }
        default:
        {
            VBClLogError("received unknown IPC command 0x%x\n", idCmd);
            break;
        }
    }

    return rc;
}

/**
 * Reconnect to DRM IPC server.
 */
static int vbclSVGASessionReconnect(void)
{
    int rc = VERR_GENERAL_FAILURE;

    rc = RTCritSectEnter(&g_hClientCritSect);
    if (RT_FAILURE(rc))
    {
        VBClLogError("unable to enter critical section on reconnect, rc=%Rrc\n", rc);
        return rc;
    }

    /* Check if session was not closed before. */
    if (RT_VALID_PTR(g_hSession))
    {
        rc = RTLocalIpcSessionClose(g_hSession);
        if (RT_FAILURE(rc))
            VBClLogError("unable to release IPC connection on reconnect, rc=%Rrc\n", rc);

        rc = vbDrmIpcClientReleaseResources(&g_hClient);
        if (RT_FAILURE(rc))
            VBClLogError("unable to release IPC session resources, rc=%Rrc\n", rc);
    }

    rc = RTLocalIpcSessionConnect(&g_hSession, VBOX_DRMIPC_SERVER_NAME, 0);
    if (RT_SUCCESS(rc))
    {
        rc = vbDrmIpcClientInit(&g_hClient, RTThreadSelf(), g_hSession, VBOX_DRMIPC_TX_QUEUE_SIZE, vbclSVGASessionRxCallBack);
        if (RT_FAILURE(rc))
            VBClLogError("unable to re-initialize IPC session, rc=%Rrc\n", rc);
    }
    else
        VBClLogError("unable to reconnect to IPC server, rc=%Rrc\n", rc);

    int rc2 = RTCritSectLeave(&g_hClientCritSect);
    if (RT_FAILURE(rc2))
        VBClLogError("unable to leave critical section on reconnect, rc=%Rrc\n", rc);

    return rc;
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnWorker}
 */
static DECLCALLBACK(int) vbclSVGASessionWorker(bool volatile *pfShutdown)
{
    int             rc = VINF_SUCCESS;

    /* Notify parent thread that we started successfully. */
    rc = RTThreadUserSignal(RTThreadSelf());
    if (RT_FAILURE(rc))
        VBClLogError("unable to notify parent thread about successful start\n");

    rc = RTCritSectEnter(&g_hClientCritSect);

    if (RT_FAILURE(rc))
    {
        VBClLogError("unable to enter critical section on worker start, rc=%Rrc\n", rc);
        return rc;
    }

    rc = vbDrmIpcClientInit(&g_hClient, RTThreadSelf(), g_hSession, VBOX_DRMIPC_TX_QUEUE_SIZE, vbclSVGASessionRxCallBack);
    int rc2 = RTCritSectLeave(&g_hClientCritSect);
    if (RT_FAILURE(rc2))
        VBClLogError("unable to leave critical section on worker start, rc=%Rrc\n", rc);

    if (RT_FAILURE(rc))
    {
        VBClLogError("cannot initialize IPC session, rc=%Rrc\n", rc);
        return rc;
    }

    for (;;)
    {
        rc = vbDrmIpcConnectionHandler(&g_hClient);

        /* Try to shutdown thread as soon as possible. */
        if (ASMAtomicReadBool(pfShutdown))
        {
            /* Shutdown requested. */
            break;
        }

        /* Normal case, there was no incoming messages for a while. */
        if (rc == VERR_TIMEOUT)
        {
            continue;
        }
        else if (RT_FAILURE(rc))
        {
            VBClLogError("unable to handle IPC connection, rc=%Rrc\n", rc);

            /* Relax a bit before spinning the loop. */
            RTThreadSleep(VBOX_DRMIPC_RX_RELAX_MS);
            /* Try to reconnect to server. */
            rc = vbclSVGASessionReconnect();
        }
    }

    /* Check if session was not closed before. */
    if (RT_VALID_PTR(g_hSession))
    {
        rc2 = RTCritSectEnter(&g_hClientCritSect);
        if (RT_SUCCESS(rc2))
        {
            rc2 = vbDrmIpcClientReleaseResources(&g_hClient);
            if (RT_FAILURE(rc2))
                VBClLogError("cannot release IPC session resources, rc=%Rrc\n", rc2);

            rc2 = RTCritSectLeave(&g_hClientCritSect);
            if (RT_FAILURE(rc2))
                VBClLogError("unable to leave critical section on worker end, rc=%Rrc\n", rc);
        }
        else
            VBClLogError("unable to enter critical section on worker end, rc=%Rrc\n", rc);
    }

    return rc;
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnStop}
 */
static DECLCALLBACK(void) vbclSVGASessionStop(void)
{
    int rc;

    /* Check if session was not closed before. */
    if (!RT_VALID_PTR(g_hSession))
        return;

    /* Attempt to release any waiting syscall related to RTLocalIpcSessionXXX(). */
    rc = RTLocalIpcSessionFlush(g_hSession);
    if (RT_FAILURE(rc))
        VBClLogError("unable to flush data to IPC connection, rc=%Rrc\n", rc);

    rc = RTLocalIpcSessionCancel(g_hSession);
    if (RT_FAILURE(rc))
        VBClLogError("unable to cancel IPC session, rc=%Rrc\n", rc);
}

/**
 * @interface_method_impl{VBCLSERVICE,pfnTerm}
 */
static DECLCALLBACK(int) vbclSVGASessionTerm(void)
{
    int rc = VINF_SUCCESS;

    if (g_hSession)
    {
        rc = RTLocalIpcSessionClose(g_hSession);
        g_hSession = 0;

        if (RT_FAILURE(rc))
            VBClLogError("unable to close IPC connection, rc=%Rrc\n", rc);
    }

    if (g_pDisplayHelper)
    {
        if (g_pDisplayHelper->pfnUnsubscribeDisplayOffsetChangeNotification)
            g_pDisplayHelper->pfnUnsubscribeDisplayOffsetChangeNotification();

        if (g_pDisplayHelper->pfnTerm)
        {
            rc = g_pDisplayHelper->pfnTerm();
            if (RT_FAILURE(rc))
                VBClLogError("unable to terminate Desktop Environment helper '%s', rc=%Rrc\n",
                             rc, g_pDisplayHelper->pszName);
        }
    }

    return VINF_SUCCESS;
}

VBCLSERVICE g_SvcDisplaySVGASession =
{
    "vmsvga-session",                   /* szName */
    "VMSVGA display assistant",         /* pszDescription */
    ".vboxclient-vmsvga-session",       /* pszPidFilePathTemplate */
    NULL,                               /* pszUsage */
    NULL,                               /* pszOptions */
    NULL,                               /* pfnOption */
    vbclSVGASessionInit,                /* pfnInit */
    vbclSVGASessionWorker,              /* pfnWorker */
    vbclSVGASessionStop,                /* pfnStop */
    vbclSVGASessionTerm,                /* pfnTerm */
};
